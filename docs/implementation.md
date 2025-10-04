# grid-seq Implementation Guide

This document describes the technical architecture and implementation details of the grid-seq LV2 plugin.

## Overview

grid-seq is a real-time MIDI step sequencer implemented as an LV2 plugin with a separate UI component. The architecture follows LV2 conventions for plugin/UI separation and uses control ports for state synchronization.

## Architecture

```
┌─────────────────────┐
│   LV2 Host (DAW)    │
│   - Transport       │
│   - MIDI routing    │
└──────────┬──────────┘
           │
    ┌──────┴──────┐
    │             │
┌───▼────────┐  ┌▼────────────┐
│  Plugin    │  │  UI (X11)   │
│  (DSP)     │  │  - Cairo    │
│            │  │  - GUI grid │
└───┬───┬────┘  └─────────────┘
    │   │
    │   └─────────────┐
    │                 │
┌───▼──────┐   ┌──────▼────────┐
│ MIDI Out │   │ Launchpad Out │
│ (Notes)  │   │ (LED Control) │
└──────────┘   └───────────────┘
```

## Core Components

### 1. Plugin (grid_seq.c)

**Responsibilities:**
- Process audio/MIDI in real-time (audio thread)
- Handle transport sync (BPM, play/stop)
- Generate MIDI note events
- Control Launchpad LEDs
- Maintain grid state
- Write state to output ports for UI sync

**Port Structure:**
```c
PORT_MIDI_IN         (0)  - Atom input (MIDI + Time)
PORT_MIDI_OUT        (1)  - Atom output (MIDI notes)
PORT_LAUNCHPAD_OUT   (2)  - Atom output (SysEx + LED control)
PORT_GRID_X          (3)  - Control input (toggle X coord)
PORT_GRID_Y          (4)  - Control input (toggle Y coord)
PORT_CURRENT_STEP    (5)  - Control output (playback position)
PORT_GRID_CHANGED    (6)  - Control output (change counter)
PORT_NOTIFY          (7)  - Atom output (UI notifications)
PORT_GRID_ROW_0..7   (8-15) - Control output (grid row bit masks)
```

**Key Functions:**

- `instantiate()`: Initialize plugin state, map URIDs, set up forges
- `connect_port()`: Connect host port buffers to plugin pointers
- `run()`: Main processing loop (called per audio cycle)
  - Read transport position and tempo
  - Advance sequencer timing
  - Generate MIDI Note On/Off events
  - Update Launchpad LEDs
  - Pack grid state into row ports
- `activate()`: Reset playback state when host starts
- `deactivate()`: Send all Note Offs when host stops

### 2. Sequencer (sequencer.c)

**Timing Model:**
- 16th note grid (4 steps per beat)
- Frame-based timing using `frame_counter` and `frames_per_step`
- Tempo updates recalculate `frames_per_step`

**Note Scheduling:**

```c
// Step timing
frames_per_step = (sample_rate * 60.0) / (bpm * 4.0)

// Note On: Sent at step boundary (frame_counter % frames_per_step == 0)
// Note Off: Sent at 50% point (frame_counter % frames_per_step == frames_per_step/2)
```

**Key Functions:**

- `sequencer_advance()`: Increment frame counter, detect step boundaries
- `sequencer_process_step()`: Send Note On events for active cells in current column
- `sequencer_process_note_offs()`: Send Note Off for all active notes

**Note Off Timing Implementation:**
```c
// In run() function:
uint64_t old_step_frame = old_frame % frames_per_step;
bool was_before_half = old_step_frame < (frames_per_step / 2);

// After advancing...
uint64_t new_step_frame = new_frame % frames_per_step;
bool is_after_half = new_step_frame >= (frames_per_step / 2);

if (was_before_half && is_after_half) {
    // Calculate exact frame offset to 50% point
    uint64_t half_point = (current_step_start) + (frames_per_step / 2);
    uint32_t offset = half_point - old_frame;
    sequencer_process_note_offs(state, forge, uris, offset);
}
```

### 3. State Management (state.c/state.h)

**GridSeqState Structure:**
```c
typedef struct {
    bool grid[8][8];           // Pattern data
    uint8_t base_note;         // Starting MIDI note (C2 = 36)
    uint8_t current_step;      // Playback position (0-7)
    uint8_t previous_step;     // For LED updates
    double sample_rate;        // Host sample rate
    uint64_t frame_counter;    // Total frames since start
    uint64_t frames_per_step;  // Frames per 16th note
    bool playing;              // Transport state
    bool active_notes[128];    // Which MIDI notes are on
} GridSeqState;
```

**Key Functions:**

- `state_init()`: Initialize with sample rate
- `state_update_tempo()`: Recalculate timing on BPM change
- `state_toggle_step()`: Flip grid cell state

### 4. GUI (gui.c)

**Responsibilities:**
- Render 8x8 grid with Cairo
- Handle mouse clicks for step toggling
- Subscribe to plugin output ports
- Reconstruct grid state from port data
- Display current step indicator

**Port Subscription:**
The GUI subscribes to:
- Port 5 (current_step): Updates playback indicator
- Ports 8-15 (grid_row_0..7): Reconstructs full grid state

**Grid State Sync:**
```c
// Plugin packs each row into 0-255:
for (int x = 0; x < 8; x++) {
    uint8_t row_value = 0;
    for (int y = 0; y < 8; y++) {
        if (grid[x][y]) {
            row_value |= (1 << y);  // Set bit y
        }
    }
    *grid_row[x] = (float)row_value;
}

// GUI unpacks:
if (port_index >= 8 && port_index <= 15) {
    int x = port_index - 8;
    uint8_t row_value = (uint8_t)(*buffer);
    for (int y = 0; y < 8; y++) {
        grid[x][y] = (row_value & (1 << y)) != 0;
    }
}
```

**Event Handling:**
- `idle()`: Called periodically by host
  - Poll X11 events
  - Redraw when current step changes
- `port_event()`: Called when subscribed ports update
  - Unpack grid row data
  - Trigger redraw

**Keyboard Focus Prevention:**
```c
// Prevent GUI from stealing DAW keyboard shortcuts
XWMHints hints;
hints.flags = InputHint;
hints.input = False;  // Don't accept keyboard input
XSetWMHints(display, window, &hints);
```

### 5. Launchpad Integration (launchpad.c)

**Protocol:**
- Uses Programmer Mode (SysEx: `F0 00 20 29 02 0D 0E 01 F7`)
- Grid pads: MIDI notes 11-88 (8×10 layout, using 8×8 subset)
- LED control: Note On messages (velocity = color palette index)

**Color Mapping:**
```c
#define LP_COLOR_OFF     0
#define LP_COLOR_GREEN   21   // Active step
#define LP_COLOR_YELLOW  13   // Current step (static)
#define LP_COLOR_YELLOW_PULSE 49  // Current step (pulsing)
```

**LED Update Logic:**
```c
for (x = 0; x < 8; x++) {
    for (y = 0; y < 8; y++) {
        bool is_active = grid[x][y];
        bool is_current = (x == current_step);

        if (is_current) {
            color = is_active ? LP_COLOR_YELLOW_PULSE : LP_COLOR_YELLOW;
        } else {
            color = is_active ? LP_COLOR_GREEN : LP_COLOR_OFF;
        }

        send_led_control(note, color);
    }
}
```

**Input Handling:**
Launchpad button presses arrive as MIDI Note On messages on PORT_MIDI_IN. The plugin:
1. Decodes note number to grid coordinates
2. Toggles grid state
3. Writes updated row to control ports (for GUI sync)
4. Updates grid_changed counter
5. Updates Launchpad LEDs

## Data Flow

### GUI Click → Plugin Update

```
1. User clicks GUI cell (x, y)
2. GUI toggles local state immediately (instant visual feedback)
3. GUI writes x → PORT_GRID_X, y → PORT_GRID_Y
4. Host delivers control values to plugin
5. Plugin toggles grid[x][y]
6. Plugin packs grid row into PORT_GRID_ROW_x
7. Host delivers port update to GUI (via subscription)
8. GUI unpacks and syncs (confirms toggle)
```

### Launchpad Press → GUI Update

```
1. User presses Launchpad pad
2. Hardware sends MIDI Note On → PORT_MIDI_IN
3. Plugin receives in run(), decodes coordinates
4. Plugin toggles grid[x][y]
5. Plugin packs updated row → PORT_GRID_ROW_x
6. Plugin updates Launchpad LEDs
7. Host delivers port update to GUI
8. GUI unpacks and redraws
```

### Transport Play → Note Generation

```
1. Host writes Time:Position atom to PORT_MIDI_IN
   - time:speed (0.0 = stopped, 1.0 = playing)
   - time:beatsPerMinute
2. Plugin reads in run()
3. If speed > 0: playing = true
4. Calculate frames_per_step from BPM
5. Each run() cycle:
   - Advance frame_counter by n_samples
   - Check if crossed step boundary
   - If yes: send Note On events, update current_step
   - Check if crossed 50% point
   - If yes: send Note Off events
```

## LV2 Atom Usage

### MIDI Events
```c
// Writing MIDI (3-byte message)
lv2_atom_forge_frame_time(&forge, frame_offset);
lv2_atom_forge_atom(&forge, 3, uris->midi_MidiEvent);
lv2_atom_forge_write(&forge, midi_data, 3);
```

### Reading Transport
```c
LV2_ATOM_SEQUENCE_FOREACH(midi_in, ev) {
    if (ev->body.type == time_Position) {
        const LV2_Atom_Object* obj = (LV2_Atom_Object*)&ev->body;

        // Extract BPM
        const LV2_Atom* bpm = lv2_atom_object_get(obj, time_beatsPerMinute, NULL);
        if (bpm && bpm->type == atom_Float) {
            state_update_tempo(&state, ((LV2_Atom_Float*)bpm)->body);
        }

        // Extract speed (play/stop)
        const LV2_Atom* speed = lv2_atom_object_get(obj, time_speed, NULL);
        if (speed && speed->type == atom_Float) {
            playing = (((LV2_Atom_Float*)speed)->body > 0.0f);
        }
    }
}
```

## Timing Precision

**Frame-accurate MIDI:**
- All MIDI events timestamped with frame offset within current cycle
- Step boundaries calculated precisely: `frame_offset = step_frame - old_frame`
- Note Offs scheduled at exact 50% point

**Edge Cases:**
- Multiple events in single cycle: Each gets correct frame offset
- BPM changes: `frames_per_step` recalculated immediately
- Transport stop: All active notes sent Note Off immediately

## Thread Safety

**Audio Thread (Plugin):**
- `run()` must be real-time safe
- No memory allocation
- No blocking I/O
- All buffers pre-allocated in `instantiate()`

**GUI Thread:**
- Runs in separate process/thread (LV2 UI)
- Communicates via LV2 port subscription
- Control port writes are atomic (single float)

**No Shared Memory:**
- Plugin and UI maintain separate grid state copies
- Synchronization via control ports only
- No race conditions

## Build System

**Meson Configuration:**
```meson
# Two shared libraries
shared_library('grid_seq',      # Plugin
  plugin_sources,
  dependencies: [lv2_dep],
  install_dir: 'lv2/grid-seq.lv2'
)

shared_library('grid_seq_ui',   # UI
  ui_sources,
  dependencies: [lv2_dep, cairo_dep, x11_dep],
  install_dir: 'lv2/grid-seq.lv2'
)

# TTL metadata
install_data(['manifest.ttl', 'grid_seq.ttl'],
  install_dir: 'lv2/grid-seq.lv2'
)
```

## Performance Characteristics

**CPU Usage:**
- Minimal: Only processes active steps
- Scales with pattern complexity (max 64 notes)
- LED updates: Once per step change (not per sample)
- GUI redraws: ~20 Hz (idle callback)

**Latency:**
- Sub-sample accurate note timing
- Limited by host block size
- Launchpad LED latency: ~10ms (MIDI + USB)

## Debugging Tips

**MIDI Monitor:**
- Use host's MIDI monitor to verify note events
- Check Note Off matches Note On
- Verify timing against metronome

**Port Values:**
- Inspect `PORT_CURRENT_STEP` for playback position
- Check `PORT_GRID_CHANGED` increments on edits
- Verify `PORT_GRID_ROW_x` values (0-255 bit masks)

**Common Issues:**
- Notes stuck on: Check Note Off timing, verify `active_notes[]` state
- GUI out of sync: Confirm port subscription (indices 8-15)
- Launchpad not responding: Check Programmer Mode SysEx sent
- Transport not working: Verify `time:speed` and `time:beatsPerMinute` in Position atom

## Future Enhancements

Potential areas for extension:

1. **LV2 State Extension**: Save/restore patterns with project
2. **Multiple Banks**: Switch between 8+ patterns
3. **Note Length Control**: Per-step gate length (currently fixed 50%)
4. **Velocity Control**: Per-step velocity (currently fixed 100)
5. **Swing/Humanization**: Timing variations
6. **MIDI Learn**: Map external controllers to parameters
7. **Pattern Chaining**: Sequence multiple 8-step patterns
8. **Scale Quantization**: Constrain notes to musical scales

## References

- [LV2 Specification](https://lv2plug.in/ns/)
- [LV2 Atom Extension](http://lv2plug.in/ns/ext/atom)
- [LV2 Time Extension](http://lv2plug.in/ns/ext/time)
- [Novation Launchpad Programmer's Reference](https://customer.novationmusic.com/en/support/downloads?product=Launchpad%20Mini%20MK3)
