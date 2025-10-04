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

#include <lv2/core/lv2.h>
#include <lv2/atom/atom.h>
#include <lv2/atom/forge.h>
#include <lv2/atom/util.h>
#include <lv2/urid/urid.h>
#include <lv2/midi/midi.h>

#include <stdlib.h>
#include <string.h>

typedef enum {
    PORT_MIDI_IN = 0,
    PORT_MIDI_OUT = 1,
    PORT_GRID_X = 2,
    PORT_GRID_Y = 3,
    PORT_CURRENT_STEP = 4
} PortIndex;

typedef struct {
    // Ports
    const LV2_Atom_Sequence* midi_in;
    LV2_Atom_Sequence* midi_out;
    const float* grid_x;
    const float* grid_y;
    float* current_step;

    // Features
    LV2_URID_Map* map;

    // URIDs
    LV2_URID midi_MidiEvent;
    LV2_URID atom_Blank;
    LV2_URID atom_Object;

    // State
    GridSeqState state;
    SequencerURIDs seq_uris;

    // Atom forge
    LV2_Atom_Forge forge;

    // Previous grid control values
    float prev_grid_x;
    float prev_grid_y;
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

    gs->seq_uris.midi_MidiEvent = gs->midi_MidiEvent;

    // Initialize state
    state_init(&gs->state, rate);

    // Initialize atom forge
    lv2_atom_forge_init(&gs->forge, gs->map);

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
        case PORT_GRID_X:
            gs->grid_x = (const float*)data;
            break;
        case PORT_GRID_Y:
            gs->grid_y = (const float*)data;
            break;
        case PORT_CURRENT_STEP:
            gs->current_step = (float*)data;
            break;
    }
}

static void activate(LV2_Handle instance) {
    GridSeq* gs = (GridSeq*)instance;
    gs->state.playing = true;
    gs->state.frame_counter = 0;
    gs->state.current_step = 0;
    gs->state.previous_step = GRID_SIZE - 1;  // Set to last step so first step triggers
    gs->state.first_run = true;
}

static void run(LV2_Handle instance, uint32_t n_samples) {
    GridSeq* gs = (GridSeq*)instance;

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
        }
    }

    // Setup forge to write to output
    const uint32_t out_capacity = gs->midi_out->atom.size;
    lv2_atom_forge_set_buffer(&gs->forge,
                              (uint8_t*)gs->midi_out,
                              out_capacity);

    // Start sequence
    LV2_Atom_Forge_Frame frame;
    lv2_atom_forge_sequence_head(&gs->forge, &frame, 0);

    // Always trigger first step on first run
    if (gs->state.first_run) {
        sequencer_process_step(&gs->state, &gs->forge, &gs->seq_uris, 0);
        gs->state.first_run = false;
    }
    // Check if we crossed a step boundary
    else if (sequencer_advance(&gs->state, n_samples)) {
        sequencer_process_step(&gs->state, &gs->forge, &gs->seq_uris, 0);
    }

    // End sequence
    lv2_atom_forge_pop(&gs->forge, &frame);

    // Update current step output port
    if (gs->current_step) {
        *gs->current_step = (float)gs->state.current_step;
    }
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
