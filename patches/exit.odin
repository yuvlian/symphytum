package patches

import "core:log"
import "shared:il2cure/hook"

install_no_exit :: proc() {
	_, ok := hook.hook_iat_all("KERNEL32.dll", "ExitProcess", rawptr(ep_stub))
	if ok {
		log.info("install_no_exit finished.")
	} else {
		log.error("ExitProcess not in any import table by name. install_no_exit failed.")
	}
}

ep_stub :: proc "system" (u_exit_code: u32) {
}
