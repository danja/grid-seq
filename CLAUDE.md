# grid-seq Development Guide

## Project Context
Grid-seq is an LV2 MIDI sequencer plugin with hardware controller support (Novation Launchpad Mini Mk3). Target: Reaper on Ubuntu x64.

## Code Standards

### Language & Compiler
- **C99** standard strictly
- GCC/Clang with `-Wall -Wextra -pedantic`
- No C++ dependencies in core plugin

### Naming Conventions
```c
// Functions: snake_case with module prefix
void sequencer_init(Sequencer* seq);
void grid_seq_process(GridSeq* gs, uint32_t n_samples);

// Types: PascalCase with typedef
typedef struct {
    bool grid[8][8];
} GridSeqState;

// Constants: SCREAMING_SNAKE_CASE
#define GRID_SIZE 8
#define BASE_NOTE_C2 36

// File-scope static: s_ prefix
static bool s_initialized = false;
```

### File Organization
- **Headers**: Single responsibility, include guards
- **Source**: Matching .c for each .h
- **Implementation order**: Static helpers first, public API last

### Memory Management
```c
// Always check allocations
void* ptr = malloc(size);
if (!ptr) {
    return ERROR_OUT_OF_MEMORY;
}

// Cleanup patterns
void sequencer_cleanup(Sequencer* seq) {
    if (!seq) return;
    
    // Free in reverse allocation order
    free(seq->buffer);
    seq->buffer = NULL;
}

// Use const for immutable pointers
void process_grid(const bool grid[8][8]);
```

## LV2 Specific Conventions

### Plugin Structure
```c
// Use standard LV2 pattern
typedef struct {
    // Ports (MIDI, Control)
    const LV2_Atom_Sequence* midi_in;
    LV2_Atom_Sequence* midi_out;
    
    // State
    GridSeqState state;
    
    // URIDs (for ATOM)
    LV2_URID_Map* map;
    struct {
        LV2_URID midi_MidiEvent;
        LV2_URID atom_Blank;
        // ...
    } uris;
} GridSeq;
```

### TTL Files
- Use consistent URI: `http://github.com/[user]/grid-seq`
- Include proper metadata (name, author, license)
- Declare all ports explicitly
- Use standard LV2 categories: `lv2:InstrumentPlugin`

### MIDI Event Handling
```c
// Use lv2_atom_sequence_next for iteration
LV2_ATOM_SEQUENCE_FOREACH(midi_in, ev) {
    if (ev->body.type == uris->midi_MidiEvent) {
        const uint8_t* msg = (const uint8_t*)(ev + 1);
        // Process MIDI message
    }
}

// Write MIDI events with proper timing
lv2_atom_forge_frame_time(&forge, frame_time);
lv2_atom_forge_atom(&forge, 3, uris->midi_MidiEvent);
lv2_atom_forge_write(&forge, midi_data, 3);
```

## Launchpad Protocol

### Mode Selection
- Use **Programmer Mode** (0x7F) for full control
- SysEx: `F0 00 20 29 02 0D 0E 01 F7` enters Programmer mode
- Disable via: `F0 00 20 29 02 0D 0E 00 F7`

### MIDI Mapping (Programmer Mode)
```c
// 8x8 Grid: Notes 11-88
#define LP_GRID_NOTE(x, y) (11 + (x) + (y) * 10)

// Top row: CC 91-98
#define LP_TOP_CC(x) (91 + (x))

// Right column: Scene launch CCs
const uint8_t LP_SCENE_CCS[] = {89, 79, 69, 59, 49, 39, 29, 19};
```

### LED Control
```c
// Static color: Channel 1, Note On
// note_num = pad position, velocity = color palette index
uint8_t msg[] = {0x90, note_num, color_index};

// Flashing: Channel 2 (0x91)
// Pulsing: Channel 3 (0x92)
```

## Build System (Meson)

### Standard Structure
```meson
project('grid-seq', 'c',
  version: '0.1.0',
  default_options: ['c_std=c99', 'warning_level=3']
)

# Dependencies
lv2_dep = dependency('lv2')
alsa_dep = dependency('alsa')
cairo_dep = dependency('cairo')

# Plugin
shared_library('grid_seq',
  sources: ['src/grid_seq.c', 'src/sequencer.c'],
  dependencies: [lv2_dep, alsa_dep],
  install: true,
  install_dir: get_option('libdir') / 'lv2/grid-seq.lv2'
)
```

### Build Commands
```bash
meson setup build
meson compile -C build
meson test -C build          # Run tests
meson install -C build       # System install

# Or user install:
meson install -C build --destdir ~/.lv2
```

## Testing Approach

### Unit Tests
```c
// Use simple assert-based testing
void test_grid_toggle(void) {
    GridSeqState state = {0};
    sequencer_toggle_step(&state, 2, 3);
    assert(state.grid[2][3] == true);
    
    sequencer_toggle_step(&state, 2, 3);
    assert(state.grid[2][3] == false);
}
```

### Integration Testing
- Test in Reaper with known MIDI monitor
- Verify timing against metronome
- Check pattern persistence across plugin reload

## Common Patterns

### Error Handling
```c
typedef enum {
    GS_OK = 0,
    GS_ERROR_NULL_POINTER,
    GS_ERROR_INVALID_PARAM,
    GS_ERROR_USB_FAILED
} GridSeqError;

// Return status codes
GridSeqError sequencer_init(Sequencer** out) {
    if (!out) return GS_ERROR_NULL_POINTER;
    
    Sequencer* seq = calloc(1, sizeof(Sequencer));
    if (!seq) return GS_ERROR_OUT_OF_MEMORY;
    
    *out = seq;
    return GS_OK;
}
```

### State Management
```c
// Use LV2 State extension for persistence
static LV2_State_Status save(
    LV2_Handle instance,
    LV2_State_Store_Function store,
    LV2_State_Handle handle,
    uint32_t flags,
    const LV2_Feature* const* features)
{
    GridSeq* gs = (GridSeq*)instance;
    
    // Store grid as blob
    store(handle, gs->uris.grid_state,
          gs->state.grid, sizeof(gs->state.grid),
          gs->uris.atom_Chunk,
          LV2_STATE_IS_POD | LV2_STATE_IS_PORTABLE);
    
    return LV2_STATE_SUCCESS;
}
```

## Documentation

### Code Comments
```c
/**
 * Initialize the sequencer engine.
 * 
 * @param seq Pointer to sequencer instance
 * @param sample_rate Host sample rate in Hz
 * @return GS_OK on success, error code otherwise
 */
GridSeqError sequencer_init(Sequencer* seq, double sample_rate);
```

### User Documentation
- **README.md**: Installation, dependencies, quick start
- **manual.md**: Complete usage guide with screenshots
- **CHANGELOG.md**: Version history (keep updated)

## USB/MIDI Threading

### Thread Safety
```c
// Use atomic operations or mutexes for shared state
#include <stdatomic.h>

typedef struct {
    atomic_bool connected;
    // Launchpad state
} LaunchpadController;

// Or pthread mutex:
pthread_mutex_t state_mutex = PTHREAD_MUTEX_INITIALIZER;

void update_grid(GridSeqState* state, int x, int y) {
    pthread_mutex_lock(&state_mutex);
    state->grid[x][y] = !state->grid[x][y];
    pthread_mutex_unlock(&state_mutex);
}
```

### USB MIDI Thread Pattern
```c
// Separate thread for USB communication
void* launchpad_thread(void* arg) {
    LaunchpadController* lp = (LaunchpadController*)arg;
    
    while (atomic_load(&lp->running)) {
        // Poll USB MIDI events
        // Update shared state with locking
        usleep(1000); // 1ms poll rate
    }
    
    return NULL;
}
```

## Performance Considerations

### Audio Thread Rules
- **Never** allocate/free in process callback
- **Never** block on I/O or locks
- Pre-allocate all buffers in `instantiate()`
- Use lock-free structures for thread communication

### Optimization Tips
```c
// Cache values, avoid repeated calculations
static inline uint32_t samples_per_step(double rate, double bpm) {
    // Called once per tempo change, not per sample
    return (uint32_t)((60.0 / bpm) * rate / 4.0);
}

// Use lookup tables for MIDI note numbers
static const uint8_t GRID_TO_NOTE[8] = {
    36, 37, 38, 39, 40, 41, 42, 43  // C2-G2
};
```

## Debug Helpers

### Logging
```c
// Use LV2 log extension or fprintf for development
#ifdef DEBUG
  #define LOG_DEBUG(fmt, ...) \
    fprintf(stderr, "[grid-seq] " fmt "\n", ##__VA_ARGS__)
#else
  #define LOG_DEBUG(fmt, ...)
#endif

LOG_DEBUG("Step %d triggered note %d", step, note);
```

## Git Commit Messages

Format:
```
<type>: <subject>

<body>

<footer>
```

Types: `feat`, `fix`, `docs`, `style`, `refactor`, `test`, `chore`

Example:
```
feat: implement launchpad LED feedback

- Add RGB color mapping from palette
- Implement current step indicator
- Support for flashing mode

Closes #12
```

## Dependencies to Track

Core:
- lv2 >= 1.18.0
- alsa-lib >= 1.2.0
- cairo >= 1.16.0 (for GUI)
- libusb-1.0 or rtmidi (Launchpad)

Build:
- meson >= 0.56.0
- ninja >= 1.10.0
- pkg-config

## File Header Template

```c
/*
 * grid-seq - Grid-based MIDI sequencer LV2 plugin
 * 
 * Copyright (C) 2025 [Author Name]
 * 
 * Permission to use, copy, modify, and/or distribute this software
 * for any purpose with or without fee is hereby granted.
 * 
 * THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL
 * WARRANTIES WITH REGARD TO THIS SOFTWARE.
 */

#ifndef GRID_SEQ_SEQUENCER_H
#define GRID_SEQ_SEQUENCER_H

#include <stdint.h>
#include <stdbool.h>

// ... declarations ...

#endif // GRID_SEQ_SEQUENCER_H
```
