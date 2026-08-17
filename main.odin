package main

import "base:runtime"
import "core:log"
import "core:os"
import "core:thread"
import "shared:il2cure/console"
import "shared:il2cure/extra"
import "shared:il2cure/il2cpp"
import "cfg"
import "patches"

main :: proc () {
	if runtime.dll_forward_reason == .Process_Attach {
		patches.install_no_exit()
		thread.create_and_start(mod_thread)
	}
}

mod_thread :: proc () {
	cfg.load()

	console_logger := log.create_console_logger()
	defer log.destroy_console_logger(console_logger)

	logger := console_logger
	file_logger: log.Logger
	have_file := false

	if f, ferr := os.open(cfg.LOG_FILE_NAME, {.Write, .Append, .Create}); ferr == nil {
		file_logger = log.create_file_logger(f)
		logger = log.create_multi_logger(console_logger, file_logger)
		have_file = true
	}

	defer if have_file {
		log.destroy_file_logger(file_logger)
		log.destroy_multi_logger(logger)
	}

	context.logger = logger
	context.logger.lowest_level = cfg.get_log_level()

	patches.detour_logger = logger

	if err := console.init("Symphytum"); err != nil {
		log.warnf("console attach failed: %v (file log still active)", err)
	}
	defer console.uninit()

	extra.spin_until_ga_load()

	if !il2cpp.init() {
		log.fatal("il2cpp.init failed")
		extra.hang()
		return
	}
	defer il2cpp.shutdown()

	applied: int
	applied += 1 if patches.install_uri_redirect() else 0
	applied += 1 if patches.install_host_override() else 0
	applied += 1 if patches.install_cert_unpin() else 0
	applied += 1 if patches.install_force_fp() else 0
	applied += 1 if patches.install_force_auto() else 0
	applied += 1 if patches.install_force_count() else 0

	log.infof("%v patches active", applied)
	extra.hang()
}
