package patches

import "shared:il2cure/e9c2da9/hook"

// no point of logging this, if it works then
// the game will just be visible lol
install_no_exit :: proc() {
	hook.hook_iat_all("KERNEL32.dll", "ExitProcess", rawptr(ep_stub))
}

ep_stub :: proc "system" (u_exit_code: u32) {
}
