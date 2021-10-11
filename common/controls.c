#include "controls.h"

static uint8 controls_state;

void controls_reset(void)
{
    controls_state = 0;
}

uint8 controls_read(void)
{
    return controls_state ^ 0xff;
}

void controls_state_write(uint8 data)
{
    controls_state = data;
}
