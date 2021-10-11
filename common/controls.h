#ifndef __CONTROLS_H__
#define __CONTROLS_H__

#include "types.h"

void controls_reset(void);
uint8 controls_read(void);
void controls_state_write(uint8 data);

#endif
