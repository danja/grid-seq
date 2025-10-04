# Launchpad Mini Mk3 Protocol Quick Reference

## USB MIDI Interfaces

The Launchpad Mini Mk3 presents **two USB MIDI ports**:

1. **DAW In/Out** - For DAW integration (Ableton Live, etc.)
2. **MIDI In/Out** - For custom control and Programmer mode

Use the **MIDI In/Out interface** for grid-seq.

## Mode Selection

### Enter Programmer Mode
```
SysEx: F0 00 20 29 02 0D 0E 01 F7
Hex:   240 0 32 41 2 13 14 1 247
```

### Exit Programmer Mode (Live Mode)
```
SysEx: F0 00 20 29 02 0D 0E 00 F7
Hex:   240 0 32 41 2 13 14 0 247
```

Programmer mode gives full control of all pads and buttons with MIDI note/CC messages.

## Programmer Mode Layout

### 8×8 Grid (Note Numbers)
```
┌────┬────┬────┬────┬────┬────┬────┬────┐
│ 81 │ 82 │ 83 │ 84 │ 85 │ 86 │ 87 │ 88 │  Row 8
├────┼────┼────┼────┼────┼────┼────┼────┤
│ 71 │ 72 │ 73 │ 74 │ 75 │ 76 │ 77 │ 78 │  Row 7
├────┼────┼────┼────┼────┼────┼────┼────┤
│ 61 │ 62 │ 63 │ 64 │ 65 │ 66 │ 67 │ 68 │  Row 6
├────┼────┼────┼────┼────┼────┼────┼────┤
│ 51 │ 52 │ 53 │ 54 │ 55 │ 56 │ 57 │ 58 │  Row 5
├────┼────┼────┼────┼────┼────┼────┼────┤
│ 41 │ 42 │ 43 │ 44 │ 45 │ 46 │ 47 │ 48 │  Row 4
├────┼────┼────┼────┼────┼────┼────┼────┤
│ 31 │ 32 │ 33 │ 34 │ 35 │ 36 │ 37 │ 38 │  Row 3
├────┼────┼────┼────┼────┼────┼────┼────┤
│ 21 │ 22 │ 23 │ 24 │ 25 │ 26 │ 27 │ 28 │  Row 2
├────┼────┼────┼────┼────┼────┼────┼────┤
│ 11 │ 12 │ 13 │ 14 │ 15 │ 16 │ 17 │ 18 │  Row 1
└────┴────┴────┴────┴────┴────┴────┴────┘
```

Formula: `note = 11 + x + (y * 10)` where x,y ∈ [0,7]

### Top Row (Control Changes)
```
CC 91 │ CC 92 │ CC 93 │ CC 94 │ CC 95 │ CC 96 │ CC 97 │ CC 98 │ CC 99
```

### Right Column (Control Changes)
```
CC 89  (top)
CC 79
CC 69
CC 59
CC 49
CC 39
CC 29
CC 19  (bottom)
```

### Logo LED
- **Note**: 27 (0x1B)
- **CC**: 99 (0x63)

## LED Control

### Static Color (Channel 1)
```
MIDI: 90 <note> <velocity>
Hex:  144 <note> <color_index>
```

### Flashing Color (Channel 2)
```
MIDI: 91 <note> <velocity>
Hex:  145 <note> <color_B>
```
Flashes between static color (A) and color_B at 50% duty cycle.

### Pulsing Color (Channel 3)
```
MIDI: 92 <note> <velocity>
Hex:  146 <note> <color_index>
```
Pulses from 25% to 100% brightness over 2 beats.

### Turn LED Off
```
MIDI: 90 <note> 00
OR:   80 <note> 00  (Note Off)
```

## Color Palette (127 colors)

Extract from palette (decimal velocities):

| Index | Color | Index | Color | Index | Color | Index | Color |
|-------|-------|-------|-------|-------|-------|-------|-------|
| 0 | Black | 5 | Red | 13 | Yellow | 21 | Green |
| 37 | Cyan | 45 | Blue | 53 | Magenta | 3 | White |
| 1-127 | Various RGB combinations |

**Key Colors for Sequencer:**
- **0**: Off
- **5**: Bright Red (active step)
- **13**: Yellow (current step indicator)
- **21**: Green (enabled step, not playing)
- **45**: Blue (meta function)
- **3**: White (dimmer, background)

See programmer reference PDF page 11 for full palette.

## RGB Color Control (SysEx)

For custom RGB colors beyond the 127-color palette:

```
SysEx: F0 00 20 29 02 0D 03 03 <led_index> <red> <green> <blue> F7
```
- `<led_index>`: Same as note number (11-88 for grid)
- `<red>`, `<green>`, `<blue>`: 0-127 (0=off, 127=max)

Example (set pad 11 to pure red):
```
F0 00 20 29 02 0D 03 03 0B 7F 00 00 F7
```

## C Code Helpers

```c
// Calculate grid note number
#define LP_GRID_NOTE(x, y) (11 + (x) + ((y) * 10))

// Top row CC numbers
#define LP_TOP_CC(x) (91 + (x))

// Right column CC numbers
static const uint8_t LP_SCENE_CCS[8] = {
    89, 79, 69, 59, 49, 39, 29, 19
};

// Send static color to grid pad
void lp_set_pad_color(int x, int y, uint8_t color) {
    uint8_t note = LP_GRID_NOTE(x, y);
    uint8_t msg[3] = {0x90, note, color};
    midi_send(midi_out, msg, 3);
}

// Clear all LEDs
void lp_clear_all(void) {
    // Clear grid
    for (int y = 0; y < 8; y++) {
        for (int x = 0; x < 8; x++) {
            uint8_t note = LP_GRID_NOTE(x, y);
            uint8_t msg[3] = {0x90, note, 0};
            midi_send(midi_out, msg, 3);
        }
    }
    
    // Clear top row
    for (int x = 0; x < 8; x++) {
        uint8_t cc = LP_TOP_CC(x);
        uint8_t msg[3] = {0xB0, cc, 0};
        midi_send(midi_out, msg, 3);
    }
    
    // Clear right column
    for (int i = 0; i < 8; i++) {
        uint8_t msg[3] = {0xB0, LP_SCENE_CCS[i], 0};
        midi_send(midi_out, msg, 3);
    }
}

// Flash current step indicator
void lp_flash_step(int step, uint8_t color_a, uint8_t color_b) {
    uint8_t note = LP_GRID_NOTE(step, 0); // Assuming bottom row
    
    // Set static color first
    uint8_t msg1[3] = {0x90, note, color_a};
    midi_send(midi_out, msg1, 3);
    
    // Set flashing color
    uint8_t msg2[3] = {0x91, note, color_b};
    midi_send(midi_out, msg2, 3);
}
```

## Device Detection

### USB IDs
- **Vendor ID**: 0x1235 (Focusrite/Novation)
- **Product ID**: 0x0113 (Launchpad Mini Mk3)

### Device Inquiry (SysEx)
```
Request:  F0 7E 7F 06 01 F7
Response: F0 7E 00 06 02 00 20 29 13 01 00 00 <version> F7
```

## Recommended Implementation Pattern

```c
typedef struct {
    snd_rawmidi_t* midi_in;
    snd_rawmidi_t* midi_out;
    bool connected;
    uint8_t grid_state[8][8];  // LED state cache
} LaunchpadController;

// Initialize Launchpad
int lp_init(LaunchpadController* lp) {
    // Open ALSA MIDI
    int err = snd_rawmidi_open(
        &lp->midi_in, &lp->midi_out,
        "hw:Launchpad", SND_RAWMIDI_NONBLOCK
    );
    
    if (err < 0) return -1;
    
    // Enter Programmer mode
    uint8_t prog_mode[] = {0xF0, 0x00, 0x20, 0x29, 0x02, 0x0D, 0x0E, 0x01, 0xF7};
    snd_rawmidi_write(lp->midi_out, prog_mode, 9);
    
    // Clear all LEDs
    lp_clear_all(lp);
    
    lp->connected = true;
    return 0;
}

// Handle incoming button press
void lp_process_input(LaunchpadController* lp) {
    uint8_t buffer[256];
    int count = snd_rawmidi_read(lp->midi_in, buffer, sizeof(buffer));
    
    for (int i = 0; i < count; i += 3) {
        uint8_t status = buffer[i];
        uint8_t data1 = buffer[i + 1];
        uint8_t data2 = buffer[i + 2];
        
        if ((status & 0xF0) == 0x90) {  // Note On
            // Grid button pressed
            int note = data1;
            int velocity = data2;
            
            if (velocity > 0) {
                // Button press
                int x = (note % 10) - 1;
                int y = (note / 10) - 1;
                // Handle grid press at (x, y)
            }
        }
        else if ((status & 0xF0) == 0xB0) {  // Control Change
            // Top row or right column button
            // Handle CC data1 press/release
        }
    }
}
```

## Timing Considerations

- **USB Poll Rate**: ~1ms typical
- **LED Update Rate**: Max ~200Hz (don't spam)
- **Flashing/Pulsing**: Synced to MIDI clock (or 120 BPM default)

## Common Pitfalls

1. **Mode Confusion**: Always confirm Programmer mode is active
2. **MIDI Interface**: Use MIDI In/Out, not DAW In/Out
3. **Note Off**: Can use Note On with velocity 0 or Note Off (0x80)
4. **LED Persistence**: LEDs remember state; clear on disconnect
5. **Thread Safety**: Read input on separate thread from audio callback

## References

- Full protocol: Launchpad Mini [MK3] Programmer's Reference Manual
- User guide: Launchpad_Mini_User_Guide_EN.pdf
- LV2 MIDI: http://lv2plug.in/ns/ext/midi
