# grid-seq Implementation Plan

## Overview
Grid-seq is an 8x8 grid-based MIDI sequencer implemented as an LV2 plugin with dual interfaces: a GUI and Novation Launchpad Mini Mk3 hardware controller support.

## Project Goals

### Core Features (Phase 1)
- 8x8 grid sequencer (64 steps)
- MIDI note output: C2 upward (vertical axis = pitch, semitones)
- Time steps: 2 bars horizontal (8 steps per bar)
- Meta controls via top row + right column buttons
- Dual interface: GUI + Launchpad Mini Mk3
- Host: Reaper on Ubuntu x64

### Future Extension Points
- Multiple pages/patterns
- Variable step length and time division
- Velocity control per step
- Note length/gate control
- Scale modes and key transposition
- Pattern chaining
- Additional controller support
- MIDI CC output modes

## Technical Architecture

### LV2 Plugin Structure
```
Plugin Type: LV2 Instrument Plugin
MIDI Input: Host tempo/transport
MIDI Output: Note events
UI: External (X11) + Hardware Controller
```

### Core Components

#### 1. LV2 Plugin Core (`grid_seq.c`)
- LV2 descriptor implementation
- MIDI event processing
- Transport/tempo synchronization
- Pattern state management
- Port management (MIDI in/out, control ports)

#### 2. Sequencer Engine (`sequencer.c/h`)
- 8x8 grid state (64 booleans)
- Current step tracking
- Timing/clock division
- Note on/off generation
- Pattern buffer management

#### 3. GUI Module (`gui.c/h`)
- X11/Cairo or LV2 UI toolkit
- 8x8 grid rendering
- Meta control buttons
- Visual feedback (current step, active notes)
- Host communication

#### 4. Launchpad Controller (`launchpad.c/h`)
- USB MIDI communication
- Launchpad Mini Mk3 protocol (see programmer reference)
- LED feedback (mirroring GUI state)
- Button event handling
- Mode selection (Session/Programmer)

#### 5. Shared State (`state.h`)
```c
typedef struct {
    bool grid[8][8];           // Step activation
    uint8_t base_note;         // C2 (36)
    uint8_t current_step;      // 0-7
    double beats_per_bar;
    double sample_rate;
    bool playing;
} GridSeqState;
```

## Directory Structure

```
grid-seq/
├── src/
│   ├── grid_seq.c           # Main LV2 plugin
│   ├── sequencer.c/h        # Sequencer engine
│   ├── gui.c/h              # GUI implementation
│   ├── launchpad.c/h        # Launchpad controller
│   ├── midi.c/h             # MIDI utilities
│   └── state.c/h            # State management
├── include/
│   └── grid_seq/
│       └── common.h         # Shared definitions
├── ui/
│   └── resources/           # UI assets if needed
├── ttl/
│   ├── grid_seq.ttl         # LV2 plugin description
│   └── manifest.ttl         # LV2 manifest
├── test/
│   └── test_sequencer.c     # Unit tests
├── docs/
│   ├── manual.md            # User manual
│   └── launchpad-protocol.md  # Controller protocol notes
├── build/
│   └── (generated)
├── CLAUDE.md                # Development conventions
├── README.md                # Build/install instructions
├── Makefile                 # Build system
└── meson.build              # Alternative build (recommended)
```

## Development Phases

### Phase 0: Project Setup
- [ ] Create directory structure
- [ ] Setup build system (Meson recommended for LV2)
- [ ] Create CLAUDE.md with conventions
- [ ] Initialize documentation files
- [ ] Setup version control structure

### Phase 1: Core Sequencer (Minimal Viable)
- [ ] Implement basic LV2 plugin shell
- [ ] Create sequencer engine with 8x8 grid
- [ ] Implement transport synchronization
- [ ] Generate MIDI note events
- [ ] Create basic TTL files
- [ ] Test in Reaper

### Phase 2: GUI Implementation
- [ ] Choose UI framework (pugl recommended)
- [ ] Implement 8x8 grid visualization
- [ ] Add meta control buttons (top + right)
- [ ] Current step indicator
- [ ] Mouse interaction for toggling steps
- [ ] State persistence

### Phase 3: Launchpad Integration
- [ ] Implement Launchpad Mini Mk3 protocol
- [ ] USB MIDI detection and connection
- [ ] Button mapping (8x8 grid + controls)
- [ ] LED feedback synchronization
- [ ] Mode switching (Programmer mode)

### Phase 4: Polish & Documentation
- [ ] Pattern save/load
- [ ] Preset system
- [ ] Complete user manual
- [ ] Build instructions
- [ ] Performance optimization
- [ ] Bug fixes

## Technical Specifications

### MIDI Implementation
- **Note Range**: C2 (MIDI 36) to G2 (MIDI 43) for 8 rows
- **Timing**: 2 bars = 8 beats (assuming 4/4), 8 steps = 1 beat per step
- **Clock**: Synchronized to host BPM
- **Note Duration**: 1/16th note (configurable in future)

### Launchpad Mapping (Programmer Mode)
Based on provided documentation:
- **8x8 Grid**: Notes 11-88 (see programmer mode layout)
- **Top Row**: CCs 91-98 (meta functions)
- **Right Column**: CCs 89, 79, 69, 59, 49, 39, 29, 19 (Scene launches)
- **LED Control**: RGB palette via Note velocity or SysEx
- **Mode**: Programmer mode (0x7F) for full control

### LV2 Ports
```turtle
# MIDI Input (control/transport)
lv2:InputPort, atom:AtomPort ;

# MIDI Output (notes)
lv2:OutputPort, atom:AtomPort ;

# Control Ports
- Play/Stop
- BPM (optional, usually from host)
- Base Note (C2 default)
- Pattern Select (future)
```

## Build Dependencies

### Required Libraries
- **LV2 SDK**: Core LV2 plugin development
- **ALSA**: Linux MIDI
- **libusb** or **rtmidi**: USB MIDI for Launchpad
- **Cairo + X11** or **pugl**: GUI rendering
- **libserd/libsord**: TTL parsing (optional)

### Build Tools
- **Meson** + **Ninja**: Recommended build system
- **GCC** or **Clang**: C compiler
- **pkg-config**: Dependency detection

## Testing Strategy

### Unit Tests
- Sequencer grid state management
- MIDI event generation
- Timing accuracy
- Pattern save/load

### Integration Tests
- Reaper plugin loading
- MIDI routing
- Transport synchronization
- GUI responsiveness

### Hardware Tests
- Launchpad detection
- Button mapping
- LED synchronization
- USB stability

## Development Conventions (for CLAUDE.md)

### Code Style
- C99 standard
- 4-space indentation
- Snake_case for functions/variables
- PascalCase for types
- Comprehensive comments for public APIs

### LV2 Conventions
- Follow LV2 specification strictly
- Use proper URI schemes
- Implement state extension for persistence
- Time/tempo map for synchronization

### Git Workflow
- Feature branches
- Descriptive commit messages
- Tag releases (v0.1.0, etc.)

## Installation Target

```bash
# System installation path
/usr/lib/lv2/grid-seq.lv2/

# User installation path (recommended for development)
~/.lv2/grid-seq.lv2/

# Contents:
grid-seq.lv2/
├── grid_seq.so          # Plugin binary
├── grid_seq_ui.so       # UI binary (optional separate)
├── manifest.ttl
└── grid_seq.ttl
```

## References & Resources

1. **LV2 Specification**: https://lv2plug.in/
2. **Launchpad Programmer Reference**: Provided PDF documents
3. **Reaper LV2 Support**: https://www.reaper.fm/
4. **Example Plugins**: lv2/eg-* examples in LV2 SDK

## Risk Mitigation

### Technical Risks
- **Launchpad USB Latency**: Use dedicated MIDI thread
- **GUI Performance**: Optimize redraws, use dirty regions
- **Timing Accuracy**: Use sample-accurate MIDI scheduling
- **Cross-platform**: Keep platform-specific code isolated

### Development Risks
- **LV2 Complexity**: Start with minimal example, iterate
- **Hardware Availability**: Implement GUI first, hardware second
- **Scope Creep**: Maintain clear phase boundaries

## Success Criteria (Phase 1)

- [ ] Plugin loads in Reaper without crashes
- [ ] 8x8 grid stores and displays step data
- [ ] MIDI notes generated on correct pitch/timing
- [ ] GUI allows toggling steps with mouse
- [ ] Playback follows host transport
- [ ] Can create simple 2-bar patterns

## Next Steps

1. Review this plan with stakeholders
2. Create CLAUDE.md with detailed conventions
3. Setup project structure
4. Begin Phase 0 implementation
5. Establish testing environment with Reaper
