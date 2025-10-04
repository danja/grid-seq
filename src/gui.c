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
#include "launchpad.h"

#include <lv2/core/lv2.h>
#include <lv2/ui/ui.h>
#include <lv2/urid/urid.h>

#include <cairo/cairo.h>
#include <cairo/cairo-xlib.h>
#include <X11/Xlib.h>
#include <X11/Xutil.h>

#include <stdlib.h>
#include <string.h>
#include <stdio.h>

#define UI_URI PLUGIN_URI "#ui"
#define WINDOW_WIDTH 480
#define WINDOW_HEIGHT 480
#define GRID_MARGIN 20
#define GRID_SPACING 2

// Custom URI for grid updates
#define GRID_SEQ_URI PLUGIN_URI "#"
#define GRID_TOGGLE_URI GRID_SEQ_URI "gridToggle"

typedef struct {
    Display* display;
    Window window;
    cairo_surface_t* surface;
    cairo_t* cr;

    LV2UI_Write_Function write_function;
    LV2UI_Controller controller;

    GridSeqState state;

    int cell_size;
    int idle_counter;
    bool mapped;
    bool first_idle;
    Atom wm_delete_window;

    LaunchpadController* launchpad;
    uint8_t prev_step;
    float prev_grid_changed;
} GridSeqUI;

static void draw_grid(GridSeqUI* ui) {
    cairo_t* cr = ui->cr;

    // Clear background
    cairo_set_source_rgb(cr, 0.1, 0.1, 0.1);
    cairo_paint(cr);

    int grid_width = WINDOW_WIDTH - 2 * GRID_MARGIN;
    ui->cell_size = (grid_width - (GRID_SIZE - 1) * GRID_SPACING) / GRID_SIZE;

    // Draw grid cells
    for (int x = 0; x < GRID_SIZE; x++) {
        for (int y = 0; y < GRID_SIZE; y++) {
            int px = GRID_MARGIN + x * (ui->cell_size + GRID_SPACING);
            int py = GRID_MARGIN + y * (ui->cell_size + GRID_SPACING);

            // Check if this step is active
            bool is_active = ui->state.grid[x][GRID_SIZE - 1 - y];

            // Check if this is the current step
            bool is_current = (x == ui->state.current_step);

            if (is_active) {
                // Active cell - bright green
                cairo_set_source_rgb(cr, 0.2, 0.8, 0.3);
            } else if (is_current) {
                // Current step but not active - dim highlight
                cairo_set_source_rgb(cr, 0.3, 0.3, 0.4);
            } else {
                // Inactive cell - dark gray
                cairo_set_source_rgb(cr, 0.2, 0.2, 0.2);
            }

            cairo_rectangle(cr, px, py, ui->cell_size, ui->cell_size);
            cairo_fill(cr);

            // Highlight current step with border
            if (is_current) {
                cairo_set_source_rgb(cr, 0.8, 0.8, 0.2);
                cairo_set_line_width(cr, 2);
                cairo_rectangle(cr, px, py, ui->cell_size, ui->cell_size);
                cairo_stroke(cr);
            }
        }
    }

    // Flush to display
    cairo_surface_flush(ui->surface);
    XFlush(ui->display);
}

static LV2UI_Handle instantiate(
    const LV2UI_Descriptor* descriptor,
    const char* plugin_uri,
    const char* bundle_path,
    LV2UI_Write_Function write_function,
    LV2UI_Controller controller,
    LV2UI_Widget* widget,
    const LV2_Feature* const* features
) {
    (void)descriptor;
    (void)plugin_uri;
    (void)bundle_path;

    GridSeqUI* ui = (GridSeqUI*)calloc(1, sizeof(GridSeqUI));
    if (!ui) return NULL;

    ui->write_function = write_function;
    ui->controller = controller;

    // Initialize state
    state_init(&ui->state, 48000.0);

    // Load test pattern (same as plugin)
    state_toggle_step(&ui->state, 0, 0);
    state_toggle_step(&ui->state, 1, 2);
    state_toggle_step(&ui->state, 2, 4);
    state_toggle_step(&ui->state, 3, 5);
    state_toggle_step(&ui->state, 4, 7);

    // Create X11 window
    ui->display = XOpenDisplay(NULL);
    if (!ui->display) {
        free(ui);
        return NULL;
    }

    int screen = DefaultScreen(ui->display);
    ui->window = XCreateSimpleWindow(
        ui->display,
        RootWindow(ui->display, screen),
        0, 0, WINDOW_WIDTH, WINDOW_HEIGHT, 1,
        BlackPixel(ui->display, screen),
        BlackPixel(ui->display, screen)
    );

    // Don't let window manager close the window (would kill host)
    ui->wm_delete_window = XInternAtom(ui->display, "WM_DELETE_WINDOW", False);
    XSetWMProtocols(ui->display, ui->window, &ui->wm_delete_window, 1);

    // Set window to not accept keyboard focus (prevents blocking DAW shortcuts)
    XWMHints hints;
    hints.flags = InputHint;
    hints.input = False;
    XSetWMHints(ui->display, ui->window, &hints);

    XSelectInput(ui->display, ui->window,
                 ExposureMask | ButtonPressMask | StructureNotifyMask);

    // Map the window FIRST
    XMapWindow(ui->display, ui->window);

    // Wait for window to be viewable
    XWindowAttributes attrs;
    int attempts = 0;
    while (attempts++ < 100) {
        XGetWindowAttributes(ui->display, ui->window, &attrs);
        if (attrs.map_state == IsViewable) break;
        XSync(ui->display, False);
    }

    // Now create Cairo surface
    ui->surface = cairo_xlib_surface_create(
        ui->display, ui->window,
        DefaultVisual(ui->display, screen),
        WINDOW_WIDTH, WINDOW_HEIGHT
    );

    ui->cr = cairo_create(ui->surface);

    // Initialize state
    ui->idle_counter = 0;
    ui->mapped = true;
    ui->first_idle = true;

    // Force initial draw
    draw_grid(ui);
    XFlush(ui->display);

    // Don't initialize Launchpad here - it's handled by the plugin via MIDI routing
    // This prevents conflicts and ghost LED events
    ui->launchpad = NULL;

    ui->prev_step = 0;
    ui->prev_grid_changed = 0.0f;

    // Return the X11 window as the widget
    *widget = (LV2UI_Widget)(unsigned long)ui->window;

    return (LV2UI_Handle)ui;
}

static void cleanup(LV2UI_Handle handle) {
    GridSeqUI* ui = (GridSeqUI*)handle;

    if (ui->launchpad) {
        launchpad_cleanup(ui->launchpad);
    }

    if (ui->cr) cairo_destroy(ui->cr);
    if (ui->surface) cairo_surface_destroy(ui->surface);
    if (ui->display) {
        if (ui->window) XDestroyWindow(ui->display, ui->window);
        XCloseDisplay(ui->display);
    }

    free(ui);
}

static void port_event(
    LV2UI_Handle handle,
    uint32_t port_index,
    uint32_t buffer_size,
    uint32_t format,
    const void* buffer
) {
    GridSeqUI* ui = (GridSeqUI*)handle;
    (void)buffer_size;
    (void)format;

    // Handle current step updates from plugin
    if (port_index == 5 && buffer) {  // PORT_CURRENT_STEP
        float step = *(const float*)buffer;
        if (step >= 0 && step < GRID_SIZE) {
            ui->state.current_step = (uint8_t)step;

            // Update Launchpad LEDs if current step changed
            if (ui->launchpad && ui->state.current_step != ui->prev_step) {
                launchpad_update_grid(ui->launchpad, &ui->state);
                ui->prev_step = ui->state.current_step;
            }
        }
    }

    // Handle grid changed counter from plugin
    if (port_index == 6 && buffer) {  // PORT_GRID_CHANGED
        float grid_changed = *(const float*)buffer;

        // Note: Currently the GUI maintains its own grid state which can get out of sync
        // if the Launchpad hardware changes the pattern (since that goes through the plugin).
        // The counter tells us something changed, but not what.
        // TODO: Implement proper state sync using LV2 State extension or Atom messages
        if (grid_changed != ui->prev_grid_changed) {
            ui->prev_grid_changed = grid_changed;
        }
    }
}

static int idle(LV2UI_Handle handle) {
    GridSeqUI* ui = (GridSeqUI*)handle;

    // Force draw on first few idle calls to ensure startup rendering
    static int draw_count = 0;
    if (ui->first_idle) {
        draw_grid(ui);
        if (++draw_count > 3) {
            ui->first_idle = false;
        }
    }

    XEvent event;
    while (XPending(ui->display)) {
        XNextEvent(ui->display, &event);

        switch (event.type) {
            case Expose:
                draw_grid(ui);
                break;

            case ClientMessage:
                // Handle window close request
                if ((Atom)event.xclient.data.l[0] == ui->wm_delete_window) {
                    // Don't close - just hide (host will handle cleanup)
                    XUnmapWindow(ui->display, ui->window);
                }
                break;

            case ButtonPress: {
                int mx = event.xbutton.x;
                int my = event.xbutton.y;

                // Calculate which cell was clicked
                int x = (mx - GRID_MARGIN) / (ui->cell_size + GRID_SPACING);
                int y = (my - GRID_MARGIN) / (ui->cell_size + GRID_SPACING);

                if (x >= 0 && x < GRID_SIZE && y >= 0 && y < GRID_SIZE) {
                    // Flip Y coordinate (grid is drawn top to bottom)
                    int grid_y = GRID_SIZE - 1 - y;

                    // Toggle in local state for immediate visual feedback
                    state_toggle_step(&ui->state, x, grid_y);
                    draw_grid(ui);

                    // Send to plugin via control ports (plugin handles Launchpad LEDs)
                    float fx = (float)x;
                    float fy = (float)grid_y;
                    ui->write_function(ui->controller, 3, sizeof(float), 0, &fx);
                    ui->write_function(ui->controller, 4, sizeof(float), 0, &fy);
                }
                break;
            }
        }
    }

    // Poll Launchpad for button presses
    // Note: Currently disabled because Launchpad is routed through plugin MIDI input
    // The plugin receives button presses and updates its state, but GUI needs sync
    // This will be handled via the grid_changed port mechanism
    /*
    if (ui->launchpad) {
        if (launchpad_poll_input(ui->launchpad, &ui->state)) {
            draw_grid(ui);
            launchpad_update_grid(ui->launchpad, &ui->state);
        }
    }
    */

    // Redraw periodically to update current step indicator
    if (++ui->idle_counter > 5) {
        draw_grid(ui);
        ui->idle_counter = 0;
    }

    return 0;
}

static int show(LV2UI_Handle handle) {
    GridSeqUI* ui = (GridSeqUI*)handle;

    // Force initial draw when shown
    XClearArea(ui->display, ui->window, 0, 0, 0, 0, True);
    XFlush(ui->display);
    draw_grid(ui);

    return 0;
}

static int hide(LV2UI_Handle handle) {
    (void)handle;
    return 0;
}

static const void* extension_data(const char* uri) {
    static const LV2UI_Idle_Interface idle_interface = { idle };
    static const LV2UI_Show_Interface show_interface = { show, hide };

    if (strcmp(uri, LV2_UI__idleInterface) == 0) {
        return &idle_interface;
    }
    if (strcmp(uri, LV2_UI__showInterface) == 0) {
        return &show_interface;
    }

    return NULL;
}

static const LV2UI_Descriptor descriptor = {
    .URI = UI_URI,
    .instantiate = instantiate,
    .cleanup = cleanup,
    .port_event = port_event,
    .extension_data = extension_data
};

LV2_SYMBOL_EXPORT
const LV2UI_Descriptor* lv2ui_descriptor(uint32_t index) {
    return (index == 0) ? &descriptor : NULL;
}
