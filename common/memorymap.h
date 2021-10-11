#ifndef __MEMORYMAP_H__
#define __MEMORYMAP_H__

#include "types.h"

#include <stdio.h>

enum {
      XSIZE = 0x00 /*
    , YSIZE = 0x01  */
    , XPOS  = 0x02
    , YPOS  = 0x03
    , BANK  = 0x26
};

void memorymap_set_dma_finished(void);
void memorymap_set_timer_shot(void);

void memorymap_init(void);
void memorymap_reset(void);
void memorymap_done(void);
uint8 memorymap_registers_read(uint32 Addr);
void memorymap_registers_write(uint32 Addr, uint8 Value);
BOOL memorymap_load(const uint8 *rom, uint32 size);

void memorymap_save_state(FILE *fp);
void memorymap_load_state(FILE *fp);

uint32 memorymap_save_state_buf_size(void);
void memorymap_save_state_buf(uint8 *data);
void memorymap_load_state_buf(const uint8 *data);

uint8 *memorymap_getLowerRamPointer(void);
uint8 *memorymap_getUpperRamPointer(void);
uint8 *memorymap_getRegisters(void);
const uint8 *memorymap_getRomPointer(void);
const uint8 *memorymap_getLowerRomBank(void);
const uint8 *memorymap_getUpperRomBank(void);

#endif
