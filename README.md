# grid-seq

Grid-based MIDI sequencer LV2 plugin with Novation Launchpad Mini Mk3 support.

## Features

- 8x8 step grid sequencer
- 8 steps × 8 note pitches (C2-G2 by default)
- Visual GUI with Cairo/X11
- Hardware control via Novation Launchpad Mini Mk3
- Host transport sync (tempo and play/stop)
- Real-time LED feedback
- 50% gate length for punchy rhythms

## Requirements

### Runtime
- LV2 host (tested with Reaper on Ubuntu)
- Cairo graphics library
- X11
- (Optional) Novation Launchpad Mini Mk3 for hardware control

### Build
- Meson >= 0.56.0
- Ninja
- GCC or Clang with C99 support
- pkg-config
- Development headers for:
  - lv2 (>= 1.18.0)
  - cairo (>= 1.16.0)
  - x11

## Building

```bash
# Configure build
meson setup build

# Compile
meson compile -C build

# Run tests (if available)
meson test -C build
```

## Installation

### User Installation (Recommended)
Install to your user LV2 directory:

```bash
# Copy plugin bundle to user directory
cp build/grid_seq.so ~/.lv2/grid-seq.lv2/
cp build/grid_seq_ui.so ~/.lv2/grid-seq.lv2/
cp ttl/*.ttl ~/.lv2/grid-seq.lv2/
```

### System Installation
Requires root/sudo:

```bash
sudo meson install -C build
```

This installs to `/usr/local/lib/x86_64-linux-gnu/lv2/grid-seq.lv2/` (or equivalent on your system).

## Usage

1. Load the plugin in your LV2 host (e.g., Reaper)
2. Route MIDI from the plugin's output to an instrument
3. (Optional) Connect Launchpad Mini Mk3:
   - Route Launchpad MIDI output to plugin's "MIDI In" port
   - Route plugin's "Launchpad Control" output to Launchpad MIDI input
4. Click cells in the GUI or press pads on the Launchpad to program patterns
5. Press play in your host - the sequencer follows transport

### GUI Controls
- **Click grid cells**: Toggle steps on/off
- **Current step indicator**: Yellow border shows playback position
- **Active steps**: Bright green
- **Inactive steps**: Dark gray

### Launchpad Controls
- **Grid pads**: Toggle steps (same layout as GUI)
- **Current step**: Pulsing yellow
- **Active steps**: Green
- **Inactive steps**: Off

## Architecture

See [docs/implementation.md](docs/implementation.md) for technical details.

## License

ISC License - see source file headers for details.

## Development

See [CLAUDE.md](CLAUDE.md) for coding standards and development guidelines.