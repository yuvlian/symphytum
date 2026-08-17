package cfg

import "core:encoding/json"
import "core:log"
import "core:os"

CONFIG_FILE_NAME :: "Symphytum.json"
LOG_FILE_NAME    :: "Symphytum.log"

Config :: struct {
	log_level:               uint,
	redirect_game_requests:  bool,
	redirect_asset_requests: bool,
	disable_cert_pin:        bool,
	autoplay:                bool,
	game_server:             string,
	asset_server:            string,
}

cfg := Config {
	log_level               = 1,
	redirect_game_requests  = true,
	redirect_asset_requests = false,
	disable_cert_pin        = true,
	autoplay                = false,
	game_server             = "https://127.0.0.1:3000/",
	asset_server            = "https://127.0.0.1:3000/",
}

get_log_level :: proc () -> log.Level {
	switch cfg.log_level {
	case 0:
		return log.Level.Debug
	case 1:
		return log.Level.Info
	case 2:
		return log.Level.Warning
	case 3:
		return log.Level.Error
	case:
		return log.Level.Fatal
	}
}

load :: proc() {
	c := cfg
	data, err := os.read_entire_file(CONFIG_FILE_NAME, context.allocator)
	if err != nil {
		log.infof("failed to read %s, using defaults. err=%v", CONFIG_FILE_NAME, err)
		return
	}
	defer delete(data)
	if err := json.unmarshal(data, &c); err != nil {
		log.errorf("failed to parse %s, using defaults. err=%v", CONFIG_FILE_NAME, err)
		return
	}
	log.infof("loaded %s (log_level=%v)", CONFIG_FILE_NAME, c.log_level)
	cfg = c
}
