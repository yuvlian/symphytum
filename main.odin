package main

import "base:runtime"
import "core:log"
import "core:thread"
import "shared:il2cure/e9c2da9/console"
import "shared:il2cure/e9c2da9/extra"
import "shared:il2cure/e9c2da9/hook"
import "shared:il2cure/e9c2da9/il2cpp"
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

	loggers := console.create_loggers(cfg.LOG_FILE_NAME)
	defer console.destroy_loggers(loggers)

	context.logger = console.choose_logger(loggers)
	context.logger.lowest_level = cfg.get_log_level()

	patches.detour_logger = context.logger

	if err := console.init("Symphytum"); err != nil {
		log.warnf("console attach failed: %v (file log still active)", err)
	}
	defer console.uninit()

	extra.spin_until_ga_load()

	if !il2cpp.init() {
		log.fatal("il2cpp.init failed")
		extra.exit_if_ctrl_c()
		return
	}

	installers := []proc() -> bool {
		patches.install_uri_redirect,
		patches.install_host_override,
		patches.install_cert_unpin,
		patches.install_force_fp,
		patches.install_force_auto,
		patches.install_force_count,
	}

	applied := 0
	for install in installers {
		if install() {
			applied += 1
		}
	}

	log.infof("%v patches active", applied)

	extra.exit_if_ctrl_c()
	hook.uninstall_all()
	il2cpp.shutdown()
}
