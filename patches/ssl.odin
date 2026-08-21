package patches

import "base:runtime"
import "core:log"
import "shared:il2cure/e9c2da9/il2cpp"
import "../cfg"

ssl_init_trampoline: rawptr

install_cert_unpin :: proc() -> bool {
	if !cfg.cfg.disable_cert_pin {
		log.debug("cert pinning disabled. install skipped.")
		return false
	}

	tr, msg := install_patch(
		"Cysharp.Net.Http.NativeHttpHandlerCore", "Initialize", 2,
		rawptr(ssl_init_detour),
	)

	if msg_val, ok := msg.?; ok {
		log.errorf("%v. install failed.", msg_val)
		delete(msg_val)
		return false
	}

	ssl_init_trampoline = tr
	log.info("install finished.")
	return true
}

// System.Nullable<bool>
NullableBool :: struct #packed {
	value:     bool,
	has_value: bool,
}

ssl_init_detour :: proc "c" (
	this:     il2cpp.Il2CppObject,
	ctx:      rawptr,
	settings: il2cpp.Il2CppObject,
	method:   il2cpp.Il2CppMethod,
) {
	context = runtime.default_context()
	context.logger = detour_logger

	if cfg.cfg.disable_cert_pin && settings != 0 {
		class := il2cpp.vm_il2cpp_object_get_class(settings)
		set_skip, ssk := il2cpp.class_method(class, "set_SkipCertificateVerification", 1)

		if ssk && set_skip != 0 {
			nb := NullableBool {true, true}
			args := [1]uintptr {uintptr(&nb)}
			exc := il2cpp.Il2CppException(0)

			il2cpp.vm_il2cpp_runtime_invoke(
				set_skip,
				settings,
				cast(^rawptr)(raw_data(args[:])),
				&exc,
			)

			if exc != 0 {
				// can probably just print the exception msg?
				log.error("exception setting SkipCertificateVerification")
			} else {
				log.info("set SkipCertificateVerification to true")
			}
		} else {
			log.error("set_SkipCertificateVerification not found on settings")
		}
	}

	original := (proc "c" (
		il2cpp.Il2CppObject,
		rawptr,
		il2cpp.Il2CppObject,
		il2cpp.Il2CppMethod,
	))(ssl_init_trampoline)

	if original != nil {
		original(this, ctx, settings, method)
	}
}
