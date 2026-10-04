/* Scripted controller input for console tests. A script is a list of steps separated by ';',
 * each START:INPUTS:COUNT, holding INPUTS for COUNT frames from frame START. INPUTS are joined
 * with '+': button names (CROSS, CIRCLE, SQUARE, TRIANGLE, L1, R1, L2, R2, L3, R3, OPTIONS,
 * TOUCHPAD, UP, DOWN, LEFT, RIGHT), stick tilts from -127 to 127 (LX=, LY=, RX=, RY=) and a
 * finger on the touchpad (TX= from 0 to 1919, TY= from 0 to 1079). Steps are applied on top of
 * the real controller's state, so a person can still take over. */
#include <stdlib.h>
#include <string.h>

#include "pad_script.h"

#define MAX_STEPS 128

typedef struct
{
    int start, count;
    uint32_t buttons;
    int lx, ly, rx, ry; /* 0: untouched */
    int touch_x, touch_y; /* -1: no finger */
} step_t;

static const struct
{
    const char *name;
    uint32_t bit;
} buttons[] = {
    { "L3", 0x0002 },     { "R3", 0x0004 },     { "OPTIONS", 0x0008 },   { "UP", 0x0010 },
    { "RIGHT", 0x0020 },  { "DOWN", 0x0040 },   { "LEFT", 0x0080 },      { "L2", 0x0100 },
    { "R2", 0x0200 },     { "L1", 0x0400 },     { "R1", 0x0800 },        { "TRIANGLE", 0x1000 },
    { "CIRCLE", 0x2000 }, { "CROSS", 0x4000 },  { "SQUARE", 0x8000 },    { "TOUCHPAD", 0x100000 },
};

static step_t steps[MAX_STEPS];
static int step_count;

static void parse_input(step_t *step, const char *input)
{
    for (size_t i = 0; i < sizeof(buttons) / sizeof(buttons[0]); i++)
        if (strcmp(input, buttons[i].name) == 0)
            step->buttons |= buttons[i].bit;
    if (strncmp(input, "LX=", 3) == 0)
        step->lx = atoi(input + 3);
    else if (strncmp(input, "LY=", 3) == 0)
        step->ly = atoi(input + 3);
    else if (strncmp(input, "RX=", 3) == 0)
        step->rx = atoi(input + 3);
    else if (strncmp(input, "RY=", 3) == 0)
        step->ry = atoi(input + 3);
    else if (strncmp(input, "TX=", 3) == 0)
        step->touch_x = atoi(input + 3);
    else if (strncmp(input, "TY=", 3) == 0)
        step->touch_y = atoi(input + 3);
}

void pad_script_add(const char *script)
{
    char *copy = strdup(script);
    char *save_step;
    if (copy == NULL)
        return;
    for (char *entry = strtok_r(copy, ";", &save_step); entry != NULL && step_count < MAX_STEPS;
         entry = strtok_r(NULL, ";", &save_step))
    {
        step_t *step = &steps[step_count];
        char *save_field;
        int field = 0;
        memset(step, 0, sizeof(*step));
        step->touch_x = step->touch_y = -1;
        for (char *text = strtok_r(entry, ":", &save_field); text != NULL; text = strtok_r(NULL, ":", &save_field), field++)
        {
            if (field == 0)
                step->start = atoi(text);
            else if (field == 1)
            {
                char *save_input;
                for (char *input = strtok_r(text, "+", &save_input); input != NULL; input = strtok_r(NULL, "+", &save_input))
                    parse_input(step, input);
            }
            else
                step->count = atoi(text);
        }
        if (step->count <= 0)
            step->count = 1;
        step_count++;
    }
    free(copy);
}

static uint8_t tilt(int value)
{
    value += 128;
    return (uint8_t)(value < 0 ? 0 : value > 255 ? 255 : value);
}

void pad_script_apply(int frame, ps5_pad_t *pad)
{
    for (int i = 0; i < step_count; i++)
    {
        const step_t *step = &steps[i];
        if (frame < step->start || frame >= step->start + step->count)
            continue;
        pad->buttons |= step->buttons;
        if (step->buttons & 0x0100)
            pad->l2 = 255;
        if (step->buttons & 0x0200)
            pad->r2 = 255;
        if (step->lx)
            pad->left_x = tilt(step->lx);
        if (step->ly)
            pad->left_y = tilt(step->ly);
        if (step->rx)
            pad->right_x = tilt(step->rx);
        if (step->ry)
            pad->right_y = tilt(step->ry);
        if (step->touch_x >= 0 && step->touch_y >= 0)
        {
            pad->touch_fingers = 1;
            pad->touch[0].x = (uint16_t)step->touch_x;
            pad->touch[0].y = (uint16_t)step->touch_y;
            pad->touch[0].finger = 0;
        }
    }
}
