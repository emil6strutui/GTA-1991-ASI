# Agent Notes

This folder is the `1991` ASI mod. Treat it as the code you are changing.

## Project Layout

- `source/` contains the mod implementation.
- `1991.sln` and `1991.vcxproj` build the ASI plugin.
- `bin/GTA-SA/{Debug,Release}/1991.SA.asi` is the build output.
- `../gta-reversed/` is a local reference implementation of GTA San Andreas internals. Use it to inspect original game behavior, class layouts, task logic, hook targets, and call flow before patching or reimplementing behavior.
- `%PLUGIN_SDK_DIR%` points to the local plugin-sdk checkout, currently expected at `E:\Main GTA 1991\plugin-sdk`. Use plugin-sdk headers/libs as the API surface for writing mod code.
- `E:\GTA RE Projects\sources` points to the location where the GTA 3, GTA VC and GTA LCS reversed code folders are located. Use this for validating implementations and research as well in case something is not found in the gta sa reversed code.
## Coding Guidance

- Write changes in `1991`; do not modify `../gta-reversed` or `%PLUGIN_SDK_DIR%` unless explicitly asked.
- Prefer plugin-sdk types, helpers, and hook/patch utilities when implementing San Andreas code.
- Use `gta-reversed` to verify semantics when touching game systems such as tasks, animation, HUD, markers, RenderWare, or ped/player logic.
- The user has an IDA database for the GTA San Andreas 1.0 US executable. If `gta-reversed`, plugin-sdk, or the other reversed projects are missing any required behavior, contain only an address/unreversed stub, or leave call semantics uncertain, ask the user for the relevant IDA pseudocode and/or assembly before guessing the implementation.
- Keep the project targeting GTA San Andreas 1.0 US: `PLUGIN_SGV_10US`, `GTASA`, Win32, v143.
- If new `.cpp` or `.h` files are added, include them in `1991.vcxproj` and `1991.vcxproj.filters`.

## Build

Build with Visual Studio/MSBuild using the existing project configurations:

```powershell
& 'C:\Program Files\Microsoft Visual Studio\2022\Community\MSBuild\Current\Bin\MSBuild.exe' '.\1991.sln' /p:Configuration=Debug /p:Platform=GTA-SA
```

Use `Release GTA-SA|Win32` for release builds. The project links against `%PLUGIN_SDK_DIR%\output\lib` and includes `%PLUGIN_SDK_DIR%\Plugin_SA`, `game_sa`, RenderWare, and shared headers.
