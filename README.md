# symphytum

a simple "mod" for hololive Dreams that lets you play on private servers and full perfect any song, using https://github.com/yuvlian/il2cure

## wyg?

- **private servers**: redirect traffic, disable cert pinning
- **full perfect**: are you bad at video game? me too! this thing can play the game for you~

## reqs

- odin https://github.com/odin-lang/Odin/releases
- git, for installing il2cure (see `deps.ps1`)

## quick start

clone repo then just run `deps.ps1` and `build.ps1`. after that copy the `symphytum.dll` to same folder as `game.exe`, then rename the dll to `umpdc.dll`.

for configuration, you can copy `Symphytum.json` too and modify as needed.

you can also get prebuilt from https://github.com/yuvlian/symphytum/releases/

## packages

| package | what it does |
|---------|--------------|
| `main.odin` | dll entry |
| `cfg/` | json config |
| `patches/` | patch source files |

## license

MIT
