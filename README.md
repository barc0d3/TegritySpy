# TegritySpy

TegritySpy is a standalone Windows 10/11 utility for monitoring the global OutputDebugString stream.  
It captures messages from any process via the `DBWIN_BUFFER` mechanism and displays them in a clean,  
dark-mode, split-pane interface for real-time analysis.

## Features

- **System-wide capture** — Reads the shared `DBWIN_BUFFER` without attaching a debugger.
- **Split channels** — Automatically routes tagged messages (e.g. `[TC PROBE]`) to a separate pane.
- **Dark mode UI** — Immersive dark theme with Fluent icon buttons.
- **Clipboard integration** — One-click copy of all output from a pane.
- **Clear controls** — Wipe both panes instantly.
- **Threaded listener** — Runs independently of the UI thread for smooth rendering.
- **Portable** — Single EXE, no dependencies beyond the Windows API.

## Use Cases

- Debugging multi-process systems without attaching Visual Studio.
- Monitoring engine logs, driver debug output, or custom probes in real time.
- Reverse-engineering or QA testing where log visibility is critical.

## Building

TegritySpy is pure Win32 C++.

1. Open the solution in Visual Studio 2019 or later.
2. Build as a standard Windows desktop application (Unicode, x64 or x86).
3. The resulting `TegritySpy.exe` is portable — no installer required.

### Requirements

- Windows 10 or 11 (dark mode enabled for immersive appearance).
- Visual Studio with Desktop Development for C++ workload.

## Usage

1. Launch TegritySpy.
2. Start any application that emits OutputDebugString calls.
3. Watch logs appear in the left (debug) or right (probe) pane.
4. Use the copy buttons to export logs, or the clear button to reset.

Messages are routed to the **probe** pane if they contain `[TC PROBE]` or `[Listener]` by default.

## License

MIT License — see below.

---

## MIT License
