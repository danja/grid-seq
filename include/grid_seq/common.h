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

#ifndef GRID_SEQ_COMMON_H
#define GRID_SEQ_COMMON_H

#include <stdint.h>
#include <stdbool.h>

#define GRID_SIZE 8
#define BASE_NOTE_C2 36

#define PLUGIN_URI "http://github.com/danny/grid-seq"

typedef enum {
    GS_OK = 0,
    GS_ERROR_NULL_POINTER,
    GS_ERROR_INVALID_PARAM,
    GS_ERROR_OUT_OF_MEMORY
} GridSeqError;

#endif // GRID_SEQ_COMMON_H
