/*
 * grid-seq - Grid-based MIDI sequencer LV2 plugin
 *
 * Copyright (C) 2025 Danny
 *
 * Permission to use, copy, modify, and/or distribute this software
 * for any purpose with or without fee is hereby granted.
 *
 * THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL
 * WARRANTIES WITH REGARD TO THIS SOFTWARE.
 */

#include "grid_seq/common.h"
#include "state.h"
#include "sequencer.h"
#include "launchpad.h"

#include <lv2/core/lv2.h>
#include <lv2/atom/atom.h>
#include <lv2/atom/forge.h>
#include <lv2/atom/util.h>
#include <lv2/urid/urid.h>
#include <lv2/midi/midi.h>
#include <lv2/time/time.h>

#include <stdlib.h>
#include <string.h>
#include <stdio.h>

typedef enum {
    PORT_MIDI_IN = 0,
    PORT_MIDI_OUT = 1,
    PORT_LAUNCHPAD_OUT = 2,
    PORT_GRID_X = 3,
    PORT_GRID_Y = 4,
    PORT_CURRENT_STEP = 5,
    PORT_GRID_CHANGED = 6,
    PORT_NOTIFY = 7
} PortIndex;

typedef struct {
    // Ports
    const LV2_Atom_Sequence* midi_in;
    LV2_Atom_Sequence* midi_out;
    LV2_Atom_Sequence* launchpad_out;
    LV2_Atom_Sequence* notify;
    const float* grid_x;
    const float* grid_y;
    float* current_step;
    float* grid_changed;

    // Features
    LV2_URID_Map* map;

    // URIDs
    LV2_URID midi_MidiEvent;
    LV2_URID atom_Blank;
    LV2_URID atom_Object;
    LV2_URID atom_Int;
    LV2_URID time_Position;
    LV2_URID time_beatsPerMinute;
    LV2_URID time_speed;
    LV2_URID gridState;
    LV2_URID cellX;
    LV2_URID cellY;
    LV2_URID cellValue;

    // State
    GridSeqState state;
    SequencerURIDs seq_uris;

    // Atom forge
    LV2_Atom_Forge forge;

    // Previous grid control values
    float prev_grid_x;
    float prev_grid_y;

    // Launchpad state
    bool launchpad_mode_entered;
    uint8_t prev_led_step;
    bool grid_dirty;

    // Separate forge for Launchpad
    LV2_Atom_Forge launchpad_forge;

    // Separate forge for UI notifications
    LV2_Atom_Forge notify_forge;

    // Grid change counter
    uint32_t grid_change_counter;

    // Track last toggled cell for UI notification
    int8_t last_toggled_x;
    int8_t last_toggled_y;
} GridSeq;

static LV2_Handle instantiate(
    const LV2_Descriptor* descriptor,
    double rate,
    const char* bundle_path,
    const LV2_Feature* const* features
) {
    (void)descriptor;
    (void)bundle_path;

    GridSeq* gs = (GridSeq*)calloc(1, sizeof(GridSeq));
    if (!gs) return NULL;

    // Get URID map feature
    for (int i = 0; features[i]; i++) {
        if (strcmp(features[i]->URI, LV2_URID__map) == 0) {
            gs->map = (LV2_URID_Map*)features[i]->data;
        }
    }

    if (!gs->map) {
        free(gs);
        return NULL;
    }

    // Map URIDs
    gs->midi_MidiEvent = gs->map->map(gs->map->handle, LV2_MIDI__MidiEvent);
    gs->atom_Blank = gs->map->map(gs->map->handle, LV2_ATOM__Blank);
    gs->atom_Object = gs->map->map(gs->map->handle, LV2_ATOM__Object);
    gs->atom_Int = gs->map->map(gs->map->handle, LV2_ATOM__Int);
    gs->time_Position = gs->map->map(gs->map->handle, LV2_TIME__Position);
    gs->time_beatsPerMinute = gs->map->map(gs->map->handle, LV2_TIME__beatsPerMinute);
    gs->time_speed = gs->map->map(gs->map->handle, LV2_TIME__speed);
    gs->gridState = gs->map->map(gs->map->handle, GRID_SEQ__gridState);
    gs->cellX = gs->map->map(gs->map->handle, GRID_SEQ__cellX);
    gs->cellY = gs->map->map(gs->map->handle, GRID_SEQ__cellY);
    gs->cellValue = gs->map->map(gs->map->handle, GRID_SEQ__cellValue);

    gs->seq_uris.midi_MidiEvent = gs->midi_MidiEvent;

    // Initialize state
    state_init(&gs->state, rate);

    // Initialize atom forges
    lv2_atom_forge_init(&gs->forge, gs->map);
    lv2_atom_forge_init(&gs->launchpad_forge, gs->map);
    lv2_atom_forge_init(&gs->notify_forge, gs->map);

    // Set up a simple test pattern - single note per step for easy testing
    state_toggle_step(&gs->state, 0, 0);  // Step 0: C2
    state_toggle_step(&gs->state, 1, 2);  // Step 1: D2
    state_toggle_step(&gs->state, 2, 4);  // Step 2: E2
    state_toggle_step(&gs->state, 3, 5);  // Step 3: F2
    state_toggle_step(&gs->state, 4, 7);  // Step 4: G2
    // Steps 5-7 silent

    // Initialize previous control values
    gs->prev_grid_x = -1.0f;
    gs->prev_grid_y = -1.0f;

    // Initialize Launchpad state
    gs->launchpad_mode_entered = false;
    gs->prev_led_step = 0;
    gs->grid_dirty = true;

    // Initialize toggle tracking
    gs->last_toggled_x = -1;
    gs->last_toggled_y = -1;

    return (LV2_Handle)gs;
}

static void connect_port(
    LV2_Handle instance,
    uint32_t port,
    void* data
) {
    GridSeq* gs = (GridSeq*)instance;

    switch ((PortIndex)port) {
        case PORT_MIDI_IN:
            gs->midi_in = (const LV2_Atom_Sequence*)data;
            break;
        case PORT_MIDI_OUT:
            gs->midi_out = (LV2_Atom_Sequence*)data;
            break;
        case PORT_LAUNCHPAD_OUT:
            gs->launchpad_out = (LV2_Atom_Sequence*)data;
            break;
        case PORT_NOTIFY:
            gs->notify = (LV2_Atom_Sequence*)data;
            break;
        case PORT_GRID_X:
            gs->grid_x = (const float*)data;
            break;
        case PORT_GRID_Y:
            gs->grid_y = (const float*)data;
            break;
        case PORT_CURRENT_STEP:
            gs->current_step = (float*)data;
            break;
        case PORT_GRID_CHANGED:
            gs->grid_changed = (float*)data;
            break;
    }
}

static void send_sysex_programmer_mode(GridSeq* gs, LV2_Atom_Forge* forge, bool enter) {
    // SysEx: F0 00 20 29 02 0D 0E [01/00] F7
    uint8_t sysex[] = {0xF0, 0x00, 0x20, 0x29, 0x02, 0x0D, 0x0E, enter ? 0x01 : 0x00, 0xF7};

    lv2_atom_forge_frame_time(forge, 0);
    lv2_atom_forge_atom(forge, sizeof(sysex), gs->midi_MidiEvent);
    lv2_atom_forge_write(forge, sysex, sizeof(sysex));
}

static void send_launchpad_led(GridSeq* gs, LV2_Atom_Forge* forge, uint8_t note, uint8_t color) {
    uint8_t msg[3] = {0x90, note, color};

    lv2_atom_forge_frame_time(forge, 0);
    lv2_atom_forge_atom(forge, 3, gs->midi_MidiEvent);
    lv2_atom_forge_write(forge, msg, 3);
}

static void send_grid_state_update(GridSeq* gs, LV2_Atom_Forge* forge, uint8_t x, uint8_t y, bool value) {
    // Manually construct the object to avoid forge frame issues
    // Structure: Event header + Object header + 3 properties (each: key URID + Int atom)

    // Calculate sizes
    const uint32_t int_size = sizeof(LV2_Atom_Int);
    const uint32_t prop_size = sizeof(LV2_Atom_Property_Body) + int_size;
    const uint32_t obj_body_size = 3 * prop_size;  // 3 properties
    const uint32_t obj_size = sizeof(LV2_Atom_Object_Body) + obj_body_size;

    // Write event timestamp
    lv2_atom_forge_beat_time(forge, 0.0);

    // Write object header manually
    LV2_Atom_Object obj_header = {
        {obj_size, forge->Object},
        {0, gs->gridState}
    };
    lv2_atom_forge_raw(forge, &obj_header, sizeof(obj_header));
    lv2_atom_forge_pad(forge, sizeof(obj_header));

    // Write property 1: cellX = x
    LV2_Atom_Property_Body prop1 = {gs->cellX, 0, {int_size, gs->map->map(gs->map->handle, LV2_ATOM__Int)}};
    lv2_atom_forge_raw(forge, &prop1, sizeof(prop1));
    lv2_atom_forge_pad(forge, sizeof(prop1));
    int32_t x_val = x;
    lv2_atom_forge_raw(forge, &x_val, sizeof(x_val));
    lv2_atom_forge_pad(forge, sizeof(x_val));

    // Write property 2: cellY = y
    LV2_Atom_Property_Body prop2 = {gs->cellY, 0, {int_size, gs->atom_Int}};
    lv2_atom_forge_raw(forge, &prop2, sizeof(prop2));
    lv2_atom_forge_pad(forge, sizeof(prop2));
    int32_t y_val = y;
    lv2_atom_forge_raw(forge, &y_val, sizeof(y_val));
    lv2_atom_forge_pad(forge, sizeof(y_val));

    // Write property 3: cellValue = value
    LV2_Atom_Property_Body prop3 = {gs->cellValue, 0, {int_size, gs->atom_Int}};
    lv2_atom_forge_raw(forge, &prop3, sizeof(prop3));
    lv2_atom_forge_pad(forge, sizeof(prop3));
    int32_t val = value ? 1 : 0;
    lv2_atom_forge_raw(forge, &val, sizeof(val));
    lv2_atom_forge_pad(forge, sizeof(val));

    fprintf(stderr, "grid-seq: Manually wrote object for x=%d y=%d val=%d\n", x, y, value);
}

static void update_launchpad_leds(GridSeq* gs, LV2_Atom_Forge* forge) {
    for (uint8_t x = 0; x < GRID_SIZE; x++) {
        for (uint8_t y = 0; y < GRID_SIZE; y++) {
            uint8_t note = lp_grid_to_note(x, y);
            uint8_t color;

            if (x == gs->state.current_step) {
                color = gs->state.grid[x][y] ? LP_COLOR_YELLOW : LP_COLOR_GREEN_DIM;
            } else {
                color = gs->state.grid[x][y] ? LP_COLOR_GREEN : LP_COLOR_OFF;
            }

            send_launchpad_led(gs, forge, note, color);
        }
    }
}

static void activate(LV2_Handle instance) {
    GridSeq* gs = (GridSeq*)instance;
    gs->state.playing = true;
    gs->state.frame_counter = 0;
    gs->state.current_step = 0;
    gs->state.previous_step = GRID_SIZE - 1;  // Set to last step so first step triggers
    gs->state.first_run = true;
    gs->grid_dirty = true;  // Force LED update on first run
}

static void run(LV2_Handle instance, uint32_t n_samples) {
    GridSeq* gs = (GridSeq*)instance;

    // Process incoming MIDI and Time position
    LV2_ATOM_SEQUENCE_FOREACH(gs->midi_in, ev) {
        // Check for time position (tempo/BPM)
        if (ev->body.type == gs->atom_Object || ev->body.type == gs->atom_Blank) {
            const LV2_Atom_Object* obj = (const LV2_Atom_Object*)&ev->body;

            if (obj->body.otype == gs->time_Position) {
                // Extract BPM
                const LV2_Atom* bpm_atom = NULL;
                const LV2_Atom* speed_atom = NULL;

                lv2_atom_object_get(obj,
                    gs->time_beatsPerMinute, &bpm_atom,
                    gs->time_speed, &speed_atom,
                    0);

                // Update BPM
                if (bpm_atom && bpm_atom->type == gs->map->map(gs->map->handle, LV2_ATOM__Float)) {
                    float bpm = ((const LV2_Atom_Float*)bpm_atom)->body;
                    if (bpm > 0) {
                        state_update_tempo(&gs->state, bpm);
                    }
                }

                // Update transport state (playing/stopped)
                if (speed_atom && speed_atom->type == gs->map->map(gs->map->handle, LV2_ATOM__Float)) {
                    float speed = ((const LV2_Atom_Float*)speed_atom)->body;
                    bool was_playing = gs->state.playing;
                    gs->state.playing = (speed > 0.0f);

                    if (!was_playing && gs->state.playing) {
                        // Started playing - reset frame counter
                        gs->state.frame_counter = 0;
                        gs->state.current_step = 0;
                    }
                }
            }
        }

        if (ev->body.type == gs->midi_MidiEvent) {
            const uint8_t* msg = (const uint8_t*)(ev + 1);

            // Note On (0x90)
            if ((msg[0] & 0xF0) == 0x90 && msg[2] > 0) {
                uint8_t note = msg[1];

                // Check if it's a grid button
                if (note >= 11 && note <= 88) {
                    uint8_t x, y;
                    lp_note_to_grid(note, &x, &y);

                    if (x < GRID_SIZE && y < GRID_SIZE) {
                        state_toggle_step(&gs->state, x, y);
                        gs->grid_dirty = true;
                        gs->grid_change_counter++;
                        gs->last_toggled_x = x;
                        gs->last_toggled_y = y;
                    }
                }
            }
        }
    }

    // Check for grid toggle from UI
    if (gs->grid_x && gs->grid_y) {
        float x = *gs->grid_x;
        float y = *gs->grid_y;

        // If values changed and are valid, toggle the grid cell
        if ((x != gs->prev_grid_x || y != gs->prev_grid_y) &&
            x >= 0 && x < GRID_SIZE && y >= 0 && y < GRID_SIZE) {
            state_toggle_step(&gs->state, (uint8_t)x, (uint8_t)y);
            gs->prev_grid_x = x;
            gs->prev_grid_y = y;
            gs->grid_dirty = true;
            gs->grid_change_counter++;
            gs->last_toggled_x = (int8_t)x;
            gs->last_toggled_y = (int8_t)y;
        }
    }

    // Setup forge for MIDI notes output
    const uint32_t out_capacity = gs->midi_out->atom.size;
    lv2_atom_forge_set_buffer(&gs->forge,
                              (uint8_t*)gs->midi_out,
                              out_capacity);

    // Setup forge for Launchpad control output
    const uint32_t lp_capacity = gs->launchpad_out->atom.size;
    lv2_atom_forge_set_buffer(&gs->launchpad_forge,
                              (uint8_t*)gs->launchpad_out,
                              lp_capacity);

    // Setup forge for UI notifications
    const uint32_t notify_capacity = gs->notify->atom.size;
    lv2_atom_forge_set_buffer(&gs->notify_forge,
                              (uint8_t*)gs->notify,
                              notify_capacity);
    fprintf(stderr, "grid-seq: notify forge buf=%p capacity=%u offset=%u\n",
            (void*)gs->notify_forge.buf, notify_capacity, gs->notify_forge.offset);

    // Update output ports BEFORE processing
    if (gs->current_step) {
        *gs->current_step = (float)gs->state.current_step;
    }
    if (gs->grid_changed) {
        *gs->grid_changed = (float)(gs->grid_change_counter % 1000000);
    }

    // Start MIDI note sequence
    LV2_Atom_Forge_Frame frame;
    lv2_atom_forge_sequence_head(&gs->forge, &frame, 0);

    // Start Launchpad control sequence
    LV2_Atom_Forge_Frame lp_frame;
    lv2_atom_forge_sequence_head(&gs->launchpad_forge, &lp_frame, 0);

    // Start UI notification sequence (use frame time like MIDI and Launchpad)
    LV2_Atom_Forge_Frame notify_frame;
    lv2_atom_forge_sequence_head(&gs->notify_forge, &notify_frame, 0);

    // Enter Programmer Mode on first run
    if (!gs->launchpad_mode_entered) {
        send_sysex_programmer_mode(gs, &gs->launchpad_forge, true);
        gs->launchpad_mode_entered = true;
        gs->grid_dirty = true;
    }

    // Always trigger first step on first run
    if (gs->state.first_run) {
        sequencer_process_step(&gs->state, &gs->forge, &gs->seq_uris, 0);
        gs->state.first_run = false;
    }
    // Check if we crossed a step boundary
    else if (sequencer_advance(&gs->state, n_samples)) {
        sequencer_process_step(&gs->state, &gs->forge, &gs->seq_uris, 0);
        gs->grid_dirty = true;  // Update LEDs when step changes
    }

    // End MIDI note sequence
    lv2_atom_forge_pop(&gs->forge, &frame);

    // Update Launchpad LEDs if grid changed or step changed
    if (gs->grid_dirty || gs->state.current_step != gs->prev_led_step) {
        update_launchpad_leds(gs, &gs->launchpad_forge);
        gs->grid_dirty = false;
        gs->prev_led_step = gs->state.current_step;
    }

    // Send full grid state to UI if anything changed
    if (gs->last_toggled_x >= 0 && gs->last_toggled_y >= 0) {
        // Prepare grid data
        uint8_t grid_data[64];
        for (int x = 0; x < 8; x++) {
            for (int y = 0; y < 8; y++) {
                grid_data[x * 8 + y] = gs->state.grid[x][y] ? 1 : 0;
            }
        }

        // Write using frame_time (frames, not beats) to match working MIDI pattern
        fprintf(stderr, "grid-seq: About to write grid, gridState URID=%u\n", gs->gridState);

        LV2_Atom_Forge_Ref ref1 = lv2_atom_forge_frame_time(&gs->notify_forge, 0);
        fprintf(stderr, "grid-seq: frame_time returned %lu\n", (unsigned long)ref1);

        LV2_Atom_Forge_Ref ref2 = lv2_atom_forge_atom(&gs->notify_forge, 64, gs->gridState);
        fprintf(stderr, "grid-seq: forge_atom returned %lu\n", (unsigned long)ref2);

        LV2_Atom_Forge_Ref ref3 = lv2_atom_forge_write(&gs->notify_forge, grid_data, 64);
        fprintf(stderr, "grid-seq: forge_write returned %lu\n", (unsigned long)ref3);

        fprintf(stderr, "grid-seq: Sent full grid state to UI\n");
        gs->last_toggled_x = -1;
        gs->last_toggled_y = -1;
    }

    // End Launchpad control sequence
    lv2_atom_forge_pop(&gs->launchpad_forge, &lp_frame);

    // End UI notification sequence
    lv2_atom_forge_pop(&gs->notify_forge, &notify_frame);
}

static void deactivate(LV2_Handle instance) {
    GridSeq* gs = (GridSeq*)instance;
    gs->state.playing = false;
}

static void cleanup(LV2_Handle instance) {
    free(instance);
}

static const LV2_Descriptor descriptor = {
    .URI = PLUGIN_URI,
    .instantiate = instantiate,
    .connect_port = connect_port,
    .activate = activate,
    .run = run,
    .deactivate = deactivate,
    .cleanup = cleanup,
    .extension_data = NULL
};

LV2_SYMBOL_EXPORT
const LV2_Descriptor* lv2_descriptor(uint32_t index) {
    return (index == 0) ? &descriptor : NULL;
}
