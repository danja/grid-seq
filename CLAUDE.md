# LV2 Plugin Development Guide

This document is divided into two parts:
1. **General LV2 plugin development practices** - applicable to any LV2 plugin project
2. **Grid-seq specific implementation details** - specific to this project

---

# Part 1: General LV2 Plugin Development

## Code Standards

### Language & Compiler
- **C99** standard strictly
- GCC/Clang with `-Wall -Wextra -pedantic`
- No C++ dependencies in core plugin

### Naming Conventions
```c
// Functions: snake_case with module prefix
void sequencer_init(Sequencer* seq);
void plugin_process(Plugin* p, uint32_t n_samples);

// Types: PascalCase with typedef
typedef struct {
    bool active;
    int value;
} PluginState;

// Constants: SCREAMING_SNAKE_CASE
#define BUFFER_SIZE 1024
#define DEFAULT_TEMPO 120.0

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
void cleanup(Plugin* p) {
    if (!p) return;

    // Free in reverse allocation order
    free(p->buffer);
    p->buffer = NULL;
}

// Use const for immutable pointers
void process_data(const float* input);
```

## LV2 Plugin Structure

### Basic Plugin Pattern
```c
typedef struct {
    // Ports (MIDI, Control, Audio)
    const LV2_Atom_Sequence* midi_in;
    LV2_Atom_Sequence* midi_out;

    // State
    PluginState state;

    // URIDs (for ATOM messages)
    LV2_URID_Map* map;
    struct {
        LV2_URID midi_MidiEvent;
        LV2_URID atom_Blank;
        LV2_URID atom_Object;
    } uris;

    // Atom forge for output
    LV2_Atom_Forge forge;
} Plugin;
```

### Standard LV2 Callbacks
```c
static LV2_Handle instantiate(
    const LV2_Descriptor* descriptor,
    double rate,
    const char* bundle_path,
    const LV2_Feature* const* features)
{
    Plugin* p = calloc(1, sizeof(Plugin));
    if (!p) return NULL;

    // Get required features
    for (int i = 0; features[i]; i++) {
        if (strcmp(features[i]->URI, LV2_URID__map) == 0) {
            p->map = (LV2_URID_Map*)features[i]->data;
        }
    }

    if (!p->map) {
        free(p);
        return NULL;
    }

    // Map URIDs
    p->uris.midi_MidiEvent = p->map->map(p->map->handle, LV2_MIDI__MidiEvent);

    // Initialize forge
    lv2_atom_forge_init(&p->forge, p->map);

    return (LV2_Handle)p;
}

static void connect_port(LV2_Handle instance, uint32_t port, void* data) {
    Plugin* p = (Plugin*)instance;
    // Map port indices to pointers
    switch (port) {
        case PORT_MIDI_IN:
            p->midi_in = (const LV2_Atom_Sequence*)data;
            break;
        // ...
    }
}

static void activate(LV2_Handle instance) {
    Plugin* p = (Plugin*)instance;
    // Reset state when transport starts
    p->state.frame_counter = 0;
}

static void run(LV2_Handle instance, uint32_t n_samples) {
    Plugin* p = (Plugin*)instance;
    // Main processing loop
}

static void deactivate(LV2_Handle instance) {
    Plugin* p = (Plugin*)instance;
    // Stop processing
}

static void cleanup(LV2_Handle instance) {
    free(instance);
}
```

## MIDI Event Handling

### Reading MIDI Events
```c
// Iterate through incoming MIDI events
LV2_ATOM_SEQUENCE_FOREACH(midi_in, ev) {
    if (ev->body.type == uris->midi_MidiEvent) {
        const uint8_t* msg = (const uint8_t*)(ev + 1);

        // Parse MIDI message
        uint8_t status = msg[0] & 0xF0;
        uint8_t channel = msg[0] & 0x0F;

        switch (status) {
            case 0x90:  // Note On
                if (msg[2] > 0) {  // velocity > 0
                    uint8_t note = msg[1];
                    uint8_t velocity = msg[2];
                    // Handle note on
                }
                break;

            case 0x80:  // Note Off
                // Handle note off
                break;

            case 0xB0:  // Control Change
                uint8_t cc = msg[1];
                uint8_t value = msg[2];
                // Handle CC
                break;
        }
    }
}
```

### Writing MIDI Events
```c
// Setup forge for output
const uint32_t capacity = midi_out->atom.size;
lv2_atom_forge_set_buffer(&forge, (uint8_t*)midi_out, capacity);

// Start sequence
LV2_Atom_Forge_Frame frame;
lv2_atom_forge_sequence_head(&forge, &frame, 0);

// Write MIDI message with frame-accurate timing
lv2_atom_forge_frame_time(&forge, frame_offset);
uint8_t midi_msg[3] = {0x90, note, velocity};  // Note On
lv2_atom_forge_atom(&forge, 3, uris->midi_MidiEvent);
lv2_atom_forge_write(&forge, midi_msg, 3);

// End sequence
lv2_atom_forge_pop(&forge, &frame);
```

## Plugin/UI Communication

### Control Ports for Simple Values
Control ports are best for continuous/persistent values:
```c
// Plugin updates control port every run() cycle
*output_port = current_value;

// UI reads via port_event callback
static void port_event(
    LV2UI_Handle handle,
    uint32_t port_index,
    uint32_t buffer_size,
    uint32_t format,
    const void* buffer)
{
    UI* ui = (UI*)handle;
    if (port_index == PORT_VALUE) {
        float value = *(const float*)buffer;
        // Update UI display
    }
}
```

### Bit-Packed Control Ports for Grid Data
For efficient boolean grid synchronization:
```c
// Plugin side - pack 8 bools into one float
for (int x = 0; x < GRID_COLS; x++) {
    uint8_t row_value = 0;
    for (int y = 0; y < 8; y++) {
        if (grid[x][y]) {
            row_value |= (1 << y);
        }
    }
    *grid_row_port[x] = (float)row_value;
}

// UI side - unpack
uint8_t row_value = (uint8_t)(*(const float*)buffer);
for (int y = 0; y < 8; y++) {
    ui->grid[x][y] = (row_value & (1 << y)) != 0;
}
```

**Benefits:**
- Simple, well-supported by all hosts
- Efficient (8 bools in 1 float)
- Atomic updates
- Reliable delivery via port subscription

### MIDI Messages for Discrete UI Events

**IMPORTANT**: For UI buttons that should trigger discrete actions (like pitch shift up/down), send MIDI messages to the plugin's MIDI input, NOT control port signals.

```c
// UI button handler - send MIDI CC message
void button_clicked(UI* ui, uint8_t cc_number) {
    // Create atom sequence with MIDI message
    uint8_t buf[128];
    lv2_atom_forge_set_buffer(&ui->forge, buf, sizeof(buf));

    LV2_Atom_Forge_Frame frame;
    lv2_atom_forge_sequence_head(&ui->forge, &frame, 0);
    lv2_atom_forge_frame_time(&ui->forge, 0);

    uint8_t midi_msg[3] = {0xB0, cc_number, 127};  // CC message
    lv2_atom_forge_atom(&ui->forge, 3, ui->midi_MidiEvent);
    lv2_atom_forge_write(&ui->forge, midi_msg, 3);

    lv2_atom_forge_pop(&ui->forge, &frame);

    // Write to plugin's MIDI input port (port 0)
    ui->write_function(ui->controller, 0,
                      lv2_atom_total_size(&((LV2_Atom_Sequence*)buf)->atom),
                      ui->urid_atom_sequence, buf);
}
```

**Why MIDI instead of control ports for buttons?**
- MIDI messages are **discrete events** - they happen once per button press
- Control ports are **persistent values** - they stay set across multiple audio buffers
- Using control ports for buttons causes issues:
  - Value persists, triggering action repeatedly every audio buffer
  - Detecting "value changed" with `prev_value` doesn't work reliably
  - Resetting the port value causes race conditions

### Port Subscription Pattern
```c
// UI instantiate - subscribe to output ports
if (ui->port_subscribe) {
    // Subscribe to control port updates
    ui->port_subscribe->subscribe(ui->port_subscribe->handle,
                                  PORT_INDEX, 0, NULL);
}

// UI cleanup - unsubscribe
if (ui->port_subscribe) {
    ui->port_subscribe->unsubscribe(ui->port_subscribe->handle,
                                    PORT_INDEX, 0, NULL);
}
```

## X11/Cairo UI Implementation

### Basic X11 UI Structure
```c
typedef struct {
    Display* display;
    Window window;
    Visual* visual;
    int screen;
    cairo_surface_t* surface;

    LV2UI_Write_Function write_function;
    LV2UI_Controller controller;

    bool needs_redraw;
} X11UI;

static LV2UI_Handle ui_instantiate(...) {
    X11UI* ui = calloc(1, sizeof(X11UI));

    // Open X11 display
    ui->display = XOpenDisplay(NULL);
    ui->screen = DefaultScreen(ui->display);
    ui->visual = DefaultVisual(ui->display, ui->screen);

    // Create window
    XSetWindowAttributes attrs;
    attrs.background_pixel = BlackPixel(ui->display, ui->screen);
    attrs.event_mask = ButtonPressMask | ExposureMask;

    ui->window = XCreateWindow(ui->display, parent,
                               0, 0, WIDTH, HEIGHT, 0,
                               CopyFromParent, InputOutput, CopyFromParent,
                               CWBackPixel | CWEventMask, &attrs);

    // Create Cairo surface
    ui->surface = cairo_xlib_surface_create(ui->display, ui->window,
                                           ui->visual, WIDTH, HEIGHT);

    XMapWindow(ui->display, ui->window);

    *widget = (LV2UI_Widget)(uintptr_t)ui->window;
    return ui;
}

static void ui_cleanup(LV2UI_Handle handle) {
    X11UI* ui = (X11UI*)handle;

    if (ui->surface) cairo_surface_destroy(ui->surface);
    if (ui->window) XDestroyWindow(ui->display, ui->window);
    if (ui->display) XCloseDisplay(ui->display);

    free(ui);
}
```

### Drawing with Cairo
```c
static void draw(X11UI* ui) {
    cairo_t* cr = cairo_create(ui->surface);

    // Clear background
    cairo_set_source_rgb(cr, 0.1, 0.1, 0.1);
    cairo_paint(cr);

    // Draw shapes
    cairo_set_source_rgb(cr, 0.8, 0.8, 0.2);
    cairo_rectangle(cr, x, y, width, height);
    cairo_fill(cr);

    // Draw text
    cairo_set_source_rgb(cr, 1.0, 1.0, 1.0);
    cairo_select_font_face(cr, "Sans", CAIRO_FONT_SLANT_NORMAL,
                          CAIRO_FONT_WEIGHT_BOLD);
    cairo_set_font_size(cr, 14);
    cairo_move_to(cr, x, y);
    cairo_show_text(cr, "Label");

    cairo_destroy(cr);
    ui->needs_redraw = false;
}
```

### Event Handling with Idle Interface
```c
static int ui_idle(LV2UI_Handle handle) {
    X11UI* ui = (X11UI*)handle;

    // Process X11 events
    while (XPending(ui->display) > 0) {
        XEvent event;
        XNextEvent(ui->display, &event);

        switch (event.type) {
            case ButtonPress:
                handle_click(ui, event.xbutton.x, event.xbutton.y);
                break;

            case Expose:
                ui->needs_redraw = true;
                break;
        }
    }

    // Redraw if needed
    if (ui->needs_redraw) {
        draw(ui);
        XFlush(ui->display);
    }

    return 0;
}

static const LV2UI_Idle_Interface idle_iface = { ui_idle };

static const void* ui_extension_data(const char* uri) {
    if (!strcmp(uri, LV2_UI__idleInterface)) {
        return &idle_iface;
    }
    return NULL;
}
```

## TTL Manifest Files

### Plugin TTL Structure
```ttl
@prefix lv2:  <http://lv2plug.in/ns/lv2core#> .
@prefix atom: <http://lv2plug.in/ns/ext/atom#> .
@prefix midi: <http://lv2plug.in/ns/ext/midi#> .
@prefix urid: <http://lv2plug.in/ns/ext/urid#> .

<http://github.com/user/plugin>
    a lv2:Plugin, lv2:InstrumentPlugin ;
    lv2:project <http://github.com/user/plugin> ;
    lv2:requiredFeature urid:map ;
    lv2:optionalFeature lv2:hardRTCapable ;

    lv2:port [
        a lv2:InputPort, atom:AtomPort ;
        atom:bufferType atom:Sequence ;
        atom:supports midi:MidiEvent ;
        lv2:index 0 ;
        lv2:symbol "midi_in" ;
        lv2:name "MIDI In" ;
    ] , [
        a lv2:OutputPort, atom:AtomPort ;
        atom:bufferType atom:Sequence ;
        atom:supports midi:MidiEvent ;
        lv2:index 1 ;
        lv2:symbol "midi_out" ;
        lv2:name "MIDI Out" ;
    ] .
```

## Performance Considerations

### Audio Thread Rules
- **Never** allocate/free in `run()` callback
- **Never** block on I/O or locks
- Pre-allocate all buffers in `instantiate()`
- Use lock-free structures for thread communication
- Keep `run()` deterministic and fast

### Optimization Tips
```c
// Cache calculated values
static inline uint32_t samples_per_beat(double rate, double bpm) {
    return (uint32_t)((60.0 / bpm) * rate);
}

// Use lookup tables
static const uint8_t SCALE_NOTES[7] = {0, 2, 4, 5, 7, 9, 11};

// Avoid repeated calculations in loops
uint32_t step_frames = samples_per_beat(rate, bpm) / 4;
for (int i = 0; i < n; i++) {
    // Use step_frames, don't recalculate
}
```

## Build System (Meson)

### Standard meson.build
```meson
project('plugin', 'c',
  version: '0.1.0',
  default_options: ['c_std=c99', 'warning_level=3']
)

# Dependencies
lv2_dep = dependency('lv2')

# Plugin
shared_library('plugin',
  sources: ['src/plugin.c'],
  dependencies: [lv2_dep],
  install: true,
  install_dir: get_option('libdir') / 'lv2/plugin.lv2'
)

# UI (optional)
if get_option('build_ui')
  cairo_dep = dependency('cairo')
  x11_dep = dependency('x11')

  shared_library('plugin_ui',
    sources: ['src/ui.c'],
    dependencies: [lv2_dep, cairo_dep, x11_dep],
    install: true,
    install_dir: get_option('libdir') / 'lv2/plugin.lv2'
  )
endif
```

### Build Commands
```bash
# Setup build directory
meson setup build

# Compile
meson compile -C build

# Install to system
sudo meson install -C build

# Install to user directory (preferred for development)
cp build/plugin.so ~/.lv2/plugin.lv2/
cp build/plugin_ui.so ~/.lv2/plugin.lv2/
```

## Testing and Debugging

### Debug Logging
```c
#ifdef DEBUG
  #define LOG_DEBUG(fmt, ...) \
    fprintf(stderr, "[plugin] " fmt "\n", ##__VA_ARGS__)
#else
  #define LOG_DEBUG(fmt, ...)
#endif

LOG_DEBUG("Processing %d samples at BPM %.2f", n_samples, bpm);
```

### Integration Testing
- Test in DAW (Reaper, Ardour, etc.)
- Use MIDI monitor to verify output
- Test with various buffer sizes
- Verify timing accuracy with metronome
- Check plugin reload/state persistence

## File Header Template
```c
/*
 * plugin-name - Description
 *
 * Copyright (C) 2025 Author Name
 *
 * Permission to use, copy, modify, and/or distribute this software
 * for any purpose with or without fee is hereby granted.
 *
 * THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL
 * WARRANTIES WITH REGARD TO THIS SOFTWARE.
 */

#ifndef PLUGIN_NAME_H
#define PLUGIN_NAME_H

#include <stdint.h>
#include <stdbool.h>

// Declarations...

#endif // PLUGIN_NAME_H
```

---

# Part 2: Grid-Seq Project Specifics

## Project Overview
Grid-seq is an LV2 MIDI step sequencer with:
- 16-step sequences with adjustable length (1-16)
- 128-note pitch range (full MIDI) with 8-note visible window
- Hardware controller support (Novation Launchpad Mini Mk3)
- X11/Cairo GUI with pattern editing
- Pitch shifting and pattern controls
- Target platform: Reaper on Ubuntu x64

## Architecture

### Core Components
```
src/
├── grid_seq.c       # Main plugin (LV2 callbacks, MIDI I/O)
├── sequencer.c/h    # Sequencer engine (step advancing, note generation)
├── state.c/h        # State management (grid, tempo, playback)
├── launchpad.c/h    # Launchpad protocol (MIDI mapping, LED control)
└── gui_x11.c        # X11/Cairo UI implementation

include/grid_seq/
└── common.h         # Shared constants and macros
```

### Port Layout
```c
typedef enum {
    PORT_MIDI_IN = 0,           // MIDI input (from Launchpad + UI)
    PORT_MIDI_OUT = 1,          // MIDI output (note events)
    PORT_LAUNCHPAD_OUT = 2,     // Launchpad LED control
    PORT_GRID_X = 3,            // Grid cell X coordinate (control)
    PORT_GRID_Y = 4,            // Grid cell Y coordinate (control)
    PORT_CURRENT_STEP = 5,      // Current playback step (output)
    PORT_GRID_CHANGED = 6,      // Change counter (output)
    PORT_NOTIFY = 7,            // UI notifications (atom)
    PORT_GRID_ROW_0..15 = 8-23, // Bit-packed grid rows (output)
    PORT_SEQUENCE_LENGTH = 24,  // Sequence length 1-16 (input)
    PORT_MIDI_FILTER = 25       // Note-On only filter (input)
} PortIndex;
```

## State Management

### Grid State Structure
```c
typedef struct {
    bool grid[MAX_GRID_SIZE][GRID_PITCH_RANGE];  // 16x128 step/note grid
    uint8_t pitch_offset;        // Base MIDI note (0-120, shows 8 notes)
    uint8_t hardware_page;       // Launchpad page (0=steps 0-7, 1=steps 8-15)
    uint8_t sequence_length;     // Active steps (1-16)
    uint8_t current_step;        // Current playback position
    uint8_t previous_step;       // Previous step (for change detection)
    bool playing;                // Transport state
    uint64_t frame_counter;      // Sample-accurate position
    uint32_t frames_per_step;    // Calculated from tempo
    double sample_rate;
    double tempo_bpm;
    bool first_run;              // First step trigger flag
} GridSeqState;

// Constants
#define MAX_GRID_SIZE 16         // Maximum sequence length
#define MIN_SEQUENCE_LENGTH 1
#define MAX_SEQUENCE_LENGTH 16
#define GRID_PITCH_RANGE 128     // Full MIDI note range
#define GRID_VISIBLE_ROWS 8      // Notes shown at once
#define DEFAULT_PITCH_OFFSET 36  // C2 (MIDI note 36)
```

### Pitch Offset Windowing
The plugin maintains a full 128-note MIDI range but displays only 8 notes at a time:

```c
// Plugin tracks full range
bool grid[16][128];  // All MIDI notes 0-127

// UI shows 8-note window based on pitch_offset
// pitch_offset = 36 → shows MIDI notes 36-43 (C2-G2)
// pitch_offset = 60 → shows MIDI notes 60-67 (C4-G4)

// When UI sends coordinates (x=5, y=3), plugin adds offset:
uint8_t absolute_note = pitch_offset + y;  // e.g., 36 + 3 = 39 (Eb2)
grid[x][absolute_note] = !grid[x][absolute_note];
```

### Note Timing Pattern
Frame-accurate 50% gate length using boundary detection:

```c
// Before processing
uint64_t old_step_frame = frame_counter % frames_per_step;
bool was_before_half = old_step_frame < (frames_per_step / 2);

// Process step boundary → Note On
if (sequencer_advance(&state, n_samples)) {
    sequencer_process_step(&state, &forge, &uris, 0);
}

// Check for 50% crossing → Note Off
uint64_t new_step_frame = frame_counter % frames_per_step;
bool is_after_half = new_step_frame >= (frames_per_step / 2);

if (was_before_half && is_after_half) {
    uint64_t half_point = (frame_counter / frames_per_step) * frames_per_step
                         + (frames_per_step / 2);
    uint32_t offset = (uint32_t)(half_point - old_frame);
    sequencer_process_note_offs(&state, &forge, &uris, offset);
}
```

## Launchpad Integration

### Programmer Mode Protocol
```c
// Enter Programmer Mode (full control)
uint8_t enter_sysex[] = {0xF0, 0x00, 0x20, 0x29, 0x02, 0x0D, 0x0E, 0x01, 0xF7};

// Exit Programmer Mode
uint8_t exit_sysex[] = {0xF0, 0x00, 0x20, 0x29, 0x02, 0x0D, 0x0E, 0x00, 0xF7};
```

### MIDI Mapping
```c
// 8x8 Grid pads: MIDI Notes 11-88
#define LP_GRID_NOTE(x, y) (11 + (x) + (y) * 10)

// Example: pad at (3, 5) = note 11 + 3 + 50 = 64

// Control buttons:
#define LP_CC_LEFT 93   // Left arrow
#define LP_CC_RIGHT 94  // Right arrow
#define LP_CC_DOWN 91   // Pitch shift down
#define LP_CC_UP 92     // Pitch shift up
```

### LED Control
```c
// LED commands use Note On channel 1 (0x90)
uint8_t led_msg[3] = {0x90, note_number, color_palette_index};

// Standard colors
#define LP_COLOR_OFF 0
#define LP_COLOR_GREEN 21
#define LP_COLOR_GREEN_DIM 23
#define LP_COLOR_YELLOW 13
#define LP_COLOR_WHITE 3

// Update grid LED
send_launchpad_led(gs, &forge, LP_GRID_NOTE(x, y), color);

// Update CC button LED
uint8_t cc_led[3] = {0xB0, cc_number, color};
```

### Hardware Page Switching
Launchpad shows 8 of 16 steps at a time:
```c
// CC 93 (left arrow) - go to page 0 (steps 0-7)
if (cc == 93 && gs->state.hardware_page > 0) {
    gs->state.hardware_page = 0;
}

// CC 94 (right arrow) - go to page 1 (steps 8-15)
if (cc == 94 && gs->state.sequence_length > 8 && gs->state.hardware_page == 0) {
    gs->state.hardware_page = 1;
}

// LED update shows current page
uint8_t page_offset = gs->state.hardware_page * 8;
for (uint8_t x = 0; x < 8; x++) {
    uint8_t actual_step = page_offset + x;
    // Display grid[actual_step][note]
}
```

## UI Implementation

### Button Layout (Vertical Column)
```c
// Right side of window, top to bottom:
// [S] - Settings (sequence length, MIDI filter)
// [R] - Reset/Query Launchpad
// [?] - Device Query
// [C] - Clear Pattern
// [H] - Home (re-center pitch to C2)
// [+] - Pitch Shift Up
// [-] - Pitch Shift Down
```

### UI Button Signaling
**IMPORTANT**: Pitch shift buttons send MIDI CC messages, not control port values:

```c
// + button clicked → send CC 92 (up)
void pitch_up_clicked(UI* ui) {
    // Create MIDI CC message
    uint8_t buf[128];
    lv2_atom_forge_set_buffer(&ui->forge, buf, sizeof(buf));

    LV2_Atom_Forge_Frame frame;
    lv2_atom_forge_sequence_head(&ui->forge, &frame, 0);
    lv2_atom_forge_frame_time(&ui->forge, 0);

    uint8_t midi_msg[3] = {0xB0, 92, 127};  // CC 92 = pitch up
    lv2_atom_forge_atom(&ui->forge, 3, ui->midi_MidiEvent);
    lv2_atom_forge_write(&ui->forge, midi_msg, 3);

    lv2_atom_forge_pop(&ui->forge, &frame);

    // Write to plugin MIDI input (port 0)
    ui->write_function(ui->controller, 0,
                      lv2_atom_total_size(&((LV2_Atom_Sequence*)buf)->atom),
                      ui->urid_atom_sequence, buf);
}

// - button clicked → send CC 91 (down)
// (same pattern with CC 91)
```

This approach ensures UI buttons work exactly like Launchpad buttons (discrete events, not persistent values).

### Grid Visualization
```c
// Draw grid with visible window
for (int x = 0; x < ui->state.sequence_length; x++) {
    for (int y = 0; y < GRID_VISIBLE_ROWS; y++) {
        // Y is flipped for display (0 at bottom)
        int display_y = GRID_VISIBLE_ROWS - 1 - y;
        bool active = ui->state.grid[x][display_y];

        // Highlight current step
        if (x == ui->state.current_step) {
            color = active ? YELLOW : GREEN_DIM;
        } else {
            color = active ? ACTIVE_COLOR : INACTIVE_COLOR;
        }

        cairo_rectangle(cr, px, py, cell_size, cell_size);
        cairo_fill(cr);
    }
}
```

## Special Signaling Conventions

### Control Port Signals (PORT_GRID_X)
```c
// Grid toggle: x >= 0 (step index)
// Special signals (negative values):
#define SIGNAL_RESET -100.0f       // Reset Launchpad
#define SIGNAL_QUERY -200.0f       // Device query
#define SIGNAL_CLEAR -300.0f       // Clear pattern
#define SIGNAL_RECENTER -400.0f    // Reset pitch to C2
// Note: -500/-600 (pitch up/down) removed - now use MIDI CC 91/92
```

## Build and Installation

### Development Workflow
```bash
# Compile
meson compile -C build

# Install to user directory (no sudo needed)
cp build/grid_seq.so ~/.lv2/grid-seq.lv2/
cp build/grid_seq_ui.so ~/.lv2/grid-seq.lv2/

# Verify installation
lv2ls | grep grid-seq

# Test in Reaper
# 1. Insert plugin on track
# 2. Route Launchpad MIDI to track input
# 3. Route track output to Launchpad MIDI out (for LEDs)
# 4. Monitor output with MIDI monitor or instrument
```

### File Locations
```
~/.lv2/grid-seq.lv2/
├── grid_seq.so          # Plugin binary
├── grid_seq_ui.so       # UI binary
├── manifest.ttl         # LV2 manifest
└── grid_seq.ttl         # Plugin description
```

## Dependencies

### Required
- lv2 >= 1.18.0 (plugin framework)
- cairo >= 1.16.0 (UI rendering)
- x11 (UI windowing)

### Build Tools
- meson >= 0.56.0
- ninja >= 1.10.0
- gcc/clang with C99 support
- pkg-config

## Key Lessons Learned

### 1. Control Ports vs MIDI Events for UI Buttons
- **Control ports**: Persistent values, good for continuous data (sliders, displays)
- **MIDI events**: Discrete actions, perfect for buttons (triggers, toggles)
- Button repeat problem solution: Send MIDI CC from UI instead of control port signals

### 2. Grid Windowing Pattern
- Full 128-note internal representation
- 8-note visible window (pitch_offset)
- UI sends window-relative coordinates (0-7)
- Plugin adds pitch_offset to get absolute MIDI note
- Allows full MIDI range with simple UI

### 3. Dual Output Ports for Hardware Controllers
- `midi_out`: Musical note events
- `launchpad_out`: LED control messages
- Separate ports prevent LED commands from reaching instruments
- Both receive SysEx mode switching on init

### 4. Frame-Accurate Note Timing
- Track position within step: `frame_counter % frames_per_step`
- Detect boundary crossings for precise Note Off timing
- Works across any buffer size
- No stuck notes from missed boundaries

### 5. Launchpad Programmer Mode
- Must send mode SysEx to both MIDI ports
- Send on first `run()` call after `activate()`
- LED updates use Note On channel 1 (0x90)
- Control buttons use CC messages (0xB0)
