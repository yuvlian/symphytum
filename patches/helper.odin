package patches

import "core:fmt"
import "core:log"
import "shared:il2cure/2eff70d/hook"
import "shared:il2cure/2eff70d/il2cpp"

// set in main
detour_logger: log.Logger

install_patch :: proc (
	class, method: string,
	argc:          i32,
	detour:        rawptr,
) -> (rawptr, Maybe(string)) {
	full := fmt.tprintf("%s::%s", class, method)
	m, fok := il2cpp.find_method(full, argc)
	if !fok {
		return nil, fmt.aprintf("could not resolve %s (argc %v)", full, argc)
	}
	tramp, orig := hook.hook_inline(m, detour, il2cpp.default_offsets())
	if tramp == nil && orig == nil {
		return nil, fmt.aprintf("could not inline-hook %s", full)
	}
	return tramp, nil
}
