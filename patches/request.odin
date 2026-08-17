package patches

import "base:runtime"
import "core:log"
import "core:strings"
import "shared:il2cure/il2cpp"
import "shared:il2cure/unity"
import "../cfg"

request_trampoline: rawptr
host_trampoline: rawptr

// make HttpRequestMessage.set_RequestUri use our host
install_uri_redirect :: proc() -> bool {
	if !cfg.cfg.redirect_game_requests && !cfg.cfg.redirect_asset_requests {
		log.debug("redirect disabled. install_uri_redirect skipped.")
		return false
	}

	tr, msg := install_patch(
		"System.Net.Http.HttpRequestMessage", "set_RequestUri", 1,
		rawptr(request_detour),
	)

	if msg_val, ok := msg.?; ok {
		log.errorf("%v. install_uri_redirect failed.", msg_val)
		delete(msg_val)
		return false
	}

	request_trampoline = tr
	log.info("install_uri_redirect finished.")
	return true
}

// override GetHost in grpc channel
install_host_override :: proc() -> bool {
	if !cfg.cfg.redirect_game_requests {
		log.debug("host override disabled. install_host_override skipped.")
		return false
	}

	tr, msg := install_patch(
		"Vision.Common.Network.Api", "GetHost", 0,
		rawptr(host_detour),
	)

	if msg_val, ok := msg.?; ok {
		log.errorf("%v. install_host_override failed.", msg_val)
		delete(msg_val)
		return false
	}

	host_trampoline = tr
	log.info("install_host_override finished.")
	return true
}

host_detour :: proc "c" (
	this:   il2cpp.Il2CppObject,
	method: il2cpp.Il2CppMethod,
) -> il2cpp.Il2CppString {
	context = runtime.default_context()
	context.logger = detour_logger

	if cfg.cfg.redirect_game_requests {
		if host := origin_of(cfg.cfg.game_server); host != "" {
			log.infof("server host override: GetHost() -> %v", host)
			return il2cpp.string_new(host)
		}
	}

	original := (proc "c" (
		il2cpp.Il2CppObject,
		il2cpp.Il2CppMethod,
	) -> il2cpp.Il2CppString)(host_trampoline)

	if original == nil {
		return 0
	}

	return original(this, method)
}

// returns "scheme://host[:port]"
origin_of :: proc(url: string) -> string {
	scheme_end := strings.index(url, "://")
	if scheme_end < 0 {
		return url
	}
	auth_start := scheme_end + 3
	end := len(url)
	if slash := strings.index(url[auth_start:], "/"); slash >= 0 {
		end = auth_start + slash
	}
	return url[:end]
}

// rewrite the stored RequestUri to the configured server,
// falling back to the original value when rewriting is impossible
request_detour :: proc "c" (
	this:   il2cpp.Il2CppObject,
	value:  il2cpp.Il2CppObject,
	method: il2cpp.Il2CppMethod,
) {
	context = runtime.default_context()
	context.logger = detour_logger

	original := (proc "c" (
		il2cpp.Il2CppObject,
		il2cpp.Il2CppObject,
		il2cpp.Il2CppMethod,
	))(request_trampoline)

	if value == 0 {
		original(this, 0, method)
		return
	}

	abs, ok := unity.invoke_named(value, "System.Uri", "get_AbsoluteUri")
	if !ok || abs == 0 {
		original(this, value, method)
		return
	}

	url, uok := il2cpp.string_to_utf8(il2cpp.Il2CppString(abs))
	if !uok {
		original(this, value, method)
		return
	}
	defer delete(url)

	rewritten := rewrite_url(url, cfg.cfg)

	if rewritten == "" {
		log.infof(
			"request url %s (no redirect: %v/%v)",
			url,
			cfg.cfg.redirect_game_requests,
			is_asset_request(url),
		)
		original(this, value, method)
		return
	}

	log.infof("request redirect %s -> %s", url, rewritten)

	ns := il2cpp.string_new(rewritten)
	new_uri, _ := unity.invoke_named(
		0,
		"System.Uri",
		".ctor",
		[]string {"System.String"},
		[]uintptr {uintptr(&ns)},
	)
	if new_uri == 0 {
		original(this, value, method)
		return
	}

	original(this, new_uri, method)
}

is_asset_request :: proc(url: string) -> bool {
	return strings.contains(url, "asset")
}

rewrite_url :: proc(url: string, conf: cfg.Config) -> string {
	if is_asset_request(url) {
		if !conf.redirect_asset_requests {
			return ""
		}
		return swap_authority(url, conf.asset_server)
	}
	if !conf.redirect_game_requests {
		return ""
	}
	return swap_authority(url, conf.game_server)
}

swap_authority :: proc(url, base: string) -> string {
	if base == "" {
		return url
	}

	scheme_end := strings.index(url, "://")
	if scheme_end < 0 {
		return url
	}

	auth_start := scheme_end + 3
	path_start := strings.index(url[auth_start:], "/")
	trimmed := strings.trim_suffix(base, "/")

	if path_start < 0 {
		return trimmed
	}

	return strings.concatenate({trimmed, url[auth_start + path_start:]}, context.temp_allocator)
}
