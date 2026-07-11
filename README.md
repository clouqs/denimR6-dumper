# denimR6-dumper

Reliable Rainbow Six Siege offset dumper using signature scanning and heap-based ViewData discovery.

## Requirements

- Windows 10/11 x64
- CMake 3.22+
- MSVC (Visual Studio 2022+) or compatible C++20 compiler
- Rainbow Six Siege launched with BattlEye disabled
- Active match (shooting range, custom game, or bot match)

## Quick Start

### 1. Launch R6 with BattlEye disabled

Add this to your launch arguments in Ubisoft Connect:

```
-belaunch -be
```

Or launch directly:

```
RainbowSix.exe -belaunch -be
```

### 2. Enter a match

Managers and ViewData are often null on the main menu. Load into shooting range or a bot custom game before running the dumper.

### 3. Build

```powershell
cmake -S . -B build -G "Visual Studio 17 2022" -A x64
cmake --build build --config Release
```

### 4. Run

```powershell
.\build\Release\denimR6-dumper.exe
```

Output is written to `build/Release/output/offsets.json` and `offsets.h`.

## How It Works

```
RainbowSix.exe (.text)  -->  AOB signature scan  -->  RIP-relative global RVAs
Heap memory             -->  ViewData float scan   -->  Runtime camera base
GameManager pointer     -->  Entity chain walk     -->  Struct offset validation
```

### Phase 1: Global managers

Scans `RainbowSix.exe` for community-validated byte patterns and resolves x64 RIP-relative references to produce stable RVAs for:

- GameManager (critical)
- NetworkManager
- ProfileManager
- GlowManager
- RoundManager
- FovManager

Signatures live in [`signatures/globals.json`](signatures/globals.json).

### Phase 2: ViewData (heap scan)

ViewData is heap-allocated and changes every session. The dumper scans committed memory regions for a known float anchor pattern, then validates camera position and view-projection matrix via a W2S projection test.

Config: [`signatures/heap.json`](signatures/heap.json)

| Offset | Field |
|--------|-------|
| `+0x190` | Camera position (Vector3) |
| `+0x250` | View-projection matrix (4x4) |
| `+0x2A4` | Float pattern anchor |

### Phase 3: Entity struct offsets

Struct member offsets are semi-static and stored in [`structs/offsets.json`](structs/offsets.json). After resolving GameManager, the dumper walks the entity list and validates that at least one entity has a sane position near the camera.

## Exit Codes

| Code | Meaning |
|------|---------|
| `0` | Full success |
| `1` | Runtime error (process not found, file I/O) |
| `2` | Critical validation failed (GameManager or ViewData) |
| `3` | Partial success (globals + ViewData exported, entity structs need update) |

## Post-Patch Workflow

When Ubisoft ships a new build:

1. Launch R6 with `-belaunch -be` and enter shooting range
2. Run `denimR6-dumper.exe`
3. Check the console output for `NOT FOUND` or `INVALID` entries

### Signature broke?

1. Open the updated `RainbowSix.exe` in Ghidra or IDA
2. Find the manager global reference (search for known strings or cross-references)
3. Copy a new AOB pattern with enough unique bytes
4. Update the entry in `signatures/globals.json`
5. Re-run the dumper

### Struct offset broke?

1. Attach ReClass.NET or Cheat Engine to R6 (BE disabled)
2. Navigate to GameManager → entity list → entity object
3. Re-discover position, health, team offsets
4. Update `structs/offsets.json`
5. Re-run the dumper

Community CE Lua scanners on UnknownCheats (R6S SIG OFFSET FINDER v4.0, ViewData scanner) are useful as reference when your signatures break.

## Output Format

**offsets.json** — full dump with metadata, RVAs, runtime pointers, validation status.

**offsets.h** — `constexpr` header for direct inclusion in an external cheat:

```cpp
#include "offsets.h"

// moduleBase + Offsets::GameManager -> global pointer
// viewDataBase + Offsets::ViewData_CameraPosition -> camera Vector3
```

## Project Layout

```
denimR6-dumper/
├── signatures/
│   ├── globals.json     # .text AOB signatures
│   └── heap.json        # ViewData heap pattern
├── structs/
│   └── offsets.json     # Entity struct member offsets
├── src/                 # C++ source
├── output/              # Generated dumps (gitignored)
└── build/               # CMake build output
```

## Notes

- Usermode `ReadProcessMemory` is sufficient with BattlEye disabled — no kernel driver needed for this workflow.
- Do not run the dumper on the main menu; results will be invalid.
- Entity struct offsets are placeholders from community RE — validate and adjust for your game build via ReClass if entity validation fails.
