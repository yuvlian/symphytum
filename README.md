# symphytum

a fork of https://github.com/yuvlian/myosotis for holodori PS https://github.com/yuvlian/symphytum-server

## what r the patches

1. **request** - intercepts `HttpRequestMessage.set_RequestUri` to redirect game/asset requests to custom servers configured in `symphytum.ini`.

2. **ssl** - intercepts `Api.GetSSLRootCertificates` and `NativeHttpHandlerCore.Initialize` (`SkipCertificateVerification`) to bypass SSL certificate pinning.

3. **crypto** - bypasses custom gRPC payload encryption and wrapper headers by hooking `DefaultMarshallerFactory.Encrypt`/`Decrypt` and its nested `Header` struct methods, enabling communication using raw/unencrypted Protobuf.

## building

requires zig 0.17 or newer. prebuilt available: https://github.com/yuvlian/symphytum/releases

```bat
build.bat            :: build everything
build.bat dll        :: build symphytum.dll only
```

build output will be in `./build`

## usage

1. make sure .dll and .ini same dir
2. configure config (.ini) as needed
3. inject to the game

## how le hooks work

il2cpp's managed-to-managed calls use baked-in direct native call addresses

the compiled caller jumps straight to the method's native body, never reading
`MethodInfo->methodPointer`

overwriting `methodPointer` alone only intercepts
`runtime_invoke` based calls (reflection)

we solve this with inline hooking. patch the native code body at `methodPointer` with a jump to our stub, so both direct managed calls and `runtime_invoke` paths are intercepted

but, some hooks need to call the original after running, we build a trampoline

two patch sizes are used:
- 5-byte relative jump (`E9 rel32`) when possible,
- 12-byte absolute jump (`mov rax, imm64; jmp rax`) otherwise.

if the length decoder can't safely cover the patch size (unknown instruction before the boundary), the inline patch is skipped

for property getters and constructors that are heavily optimized and folded via identical COMDAT folding (ICF) in the compiled dll, we hook the unique non-folded methods (such as the specific constructor or custom getters) to prevent ICF collisions from hijacking unrelated game subsystems

## project structure

```
cpp/
├── build/                  (gitignored) default build output dir
├── include/
│   └── patches/            header files for the patches
├── src/
│   ├── dllmain.cpp         dll entrypoint
│   ├── init.cpp            dll patches initializer
│   ├── log.cpp             file & console logger
│   ├── config.cpp          symphytum.ini stuff
│   ├── hook.cpp            inline hook helper with trampoline
│   ├── il2cpp/
│   │   ├── pe.cpp          PE parser
│   │   ├── scan.cpp        byte-pattern CALL/LEA scanner
│   │   ├── il2cpp_names.cpp canonical -> obfuscated resolver
│   │   └── il2cpp.cpp      typed il2cpp C API bridge
│   └── patches/
│       ├── request.cpp     http redirect patch
│       ├── ssl.cpp         cert pinning patch
│       └── crypto.cpp      packet encryption patch
├── build.bat               build script
├── symphytum.ini           example symphytum.ini config
└── .gitignore              self explanatory
```
