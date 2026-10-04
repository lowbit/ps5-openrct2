/* Scripted controller input for console tests (see pad_script.c). */
#ifndef PS5_PAD_SCRIPT_H
#define PS5_PAD_SCRIPT_H

#include <stddef.h>
#include <stdint.h>

/* The start of a controller state as ScePad reports it (PS5_PadData in SDL's PS5 driver). */
typedef struct
{
    uint32_t buttons;
    uint8_t left_x, left_y;
    uint8_t right_x, right_y;
    uint8_t l2, r2;
    uint16_t padding;
    float orientation[4];
    float velocity[3];
    float acceleration[3];
    uint8_t touch_fingers;
    uint8_t touch_padding[7];
    struct
    {
        uint16_t x, y;
        uint8_t finger;
        uint8_t padding[3];
    } touch[2];
} ps5_pad_t;

_Static_assert(offsetof(ps5_pad_t, touch_fingers) == 52, "ScePad touch data offset");
_Static_assert(offsetof(ps5_pad_t, touch) == 60, "ScePad touch point offset");

/* Adds the steps of a script. */
void pad_script_add(const char *script);

/* Applies the steps active at this frame to a state read from the controller. */
void pad_script_apply(int frame, ps5_pad_t *pad);

#endif
