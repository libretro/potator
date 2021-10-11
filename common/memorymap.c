#include "memorymap.h"

#include "controls.h"
#include "sound.h"
#include "timer.h"
#include "./m6502/m6502.h"

#include <stdlib.h>
#include <string.h>

static uint8 *lowerRam;
static uint8 *upperRam;
static uint8 *regs;
static const uint8 *programRom;
static const uint8 *lowerRomBank;
static const uint8 *upperRomBank;

static uint32 programRomSize;

static BOOL dma_finished = FALSE;
static BOOL timer_shot   = FALSE;
static BOOL isMAGNUM     = FALSE;

static void check_irq(void)
{
    BOOL irq = (timer_shot && ((regs[BANK] >> 1) & 1))
          || (dma_finished && ((regs[BANK] >> 2) & 1));

    void m6502_set_irq_line(BOOL); // watara.c
    m6502_set_irq_line(irq);
}

void memorymap_set_dma_finished(void)
{
    dma_finished = TRUE;
    check_irq();
}

void memorymap_set_timer_shot(void)
{
    timer_shot = TRUE;
    check_irq();
}

void memorymap_init(void)
{
    lowerRam = (uint8*)malloc(0x2000);
    upperRam = (uint8*)malloc(0x2000);
    regs     = (uint8*)malloc(0x2000);
}

void memorymap_reset(void)
{
    lowerRomBank = programRom + 0x0000;
    //  size -> upperRomBank:
    //  16KB ->   0KB (min in theory)
    //  32KB ->  16KB (min in practice)
    //  48KB ->  32KB
    //  64KB ->  48KB (max in practice)
    //  80KB ->  64KB
    //  96KB ->  80KB
    // 112KB ->  96KB
    // 128KB -> 112KB (max in theory)
    // 512KB -- 'Journey to the West' is supported! (MAGNUM cartridge)
    upperRomBank = programRom + (programRomSize - 0x4000);

    memset(lowerRam, 0x00, 0x2000);
    memset(upperRam, 0x00, 0x2000);
    memset(regs,     0x00, 0x2000);

    dma_finished = FALSE;
    timer_shot   = FALSE;
}

void memorymap_done(void)
{
    free(lowerRam); lowerRam = NULL;
    free(upperRam); upperRam = NULL;
    free(regs);     regs     = NULL;
}

uint8 memorymap_registers_read(uint32 Addr)
{
    uint8 data = regs[Addr & 0x1fff];
    switch (Addr & 0x1fff) {
        case 0x20:
            return controls_read();
        case 0x21:
            //data &= ~0xf;
            // Not used. Pass Link Port Probe (WaTest.bin from Wataroo)
            data |= regs[0x22] & 0xf;
            break;
        case 0x24:
            timer_shot = FALSE;
            check_irq();
            break;
        case 0x25:
            dma_finished = FALSE;
            check_irq();
            break;
        case 0x27:
            data &= ~3;
            if (timer_shot) {
                data |= 1;
            }
            if (dma_finished) {
                data |= 2;
            }
            break;
    }
    return data;
}

// General Purpose DMA (for 'Journey to the West')
// Pass only the first test (WaTest.bin from Wataroo)
typedef struct {
    uint16 caddr;
    uint16 vaddr;
    BOOL cpu2vram;
    uint16 length;
} GENERIC_DMA;
static GENERIC_DMA dma;

static void dma_write(uint32 Addr, uint8 Value)
{
    switch (Addr & 0x1fff) {
        case 0x08: // CBUS LO
            dma.caddr = Value;
            break;
        case 0x09: // CBUS HI
            dma.caddr |= (Value << 8);
            break;
        case 0x0a: // VBUS LO
            dma.vaddr = Value;
            break;
        case 0x0b: // VBUS HI
            dma.vaddr |= (Value << 8);
            dma.vaddr &= 0x1fff;
            dma.cpu2vram = ((Value >> 6) & 1) == 1;
            break;
        case 0x0c: // LEN
            dma.length = Value ? Value * 16 : 4096;
            break;
        case 0x0d: // Request
            if (Value & 0x80) {
                int i;
                for (i = 0; i < dma.length; i++) {
                    if (dma.cpu2vram) {
                        upperRam[dma.vaddr + i] = Rd6502(dma.caddr + i);
                    }
                    else {
                        Wr6502(dma.caddr + i, upperRam[dma.vaddr + i]);
                    }
                }
            }
            break;
    }
}

static void update_lowerRomBank(void)
{
    uint32 bankOffset = 0;
    if (isMAGNUM) {
        bankOffset = (((regs[BANK] & 0x20) << 9) | ((regs[0x21] & 0xf) << 15));
    }
    else {
        bankOffset =  ((regs[BANK] & 0xe0) << 9);
    }
    lowerRomBank = programRom + bankOffset % programRomSize;
}

void memorymap_registers_write(uint32 Addr, uint8 Value)
{
    regs[Addr & 0x1fff] = Value;
    switch (Addr & 0x1fff) {
        case 0x21:
            // MAGNUM cartridge && Output (Link Port Data Direction)
            if (isMAGNUM && regs[0x22] == 0) {
                update_lowerRomBank();
                check_irq();
            }
            break;
        case 0x23:
            timer_write(Value);
            break;
        case 0x26:
            update_lowerRomBank();
            check_irq();
            break;
        case 0x10: case 0x11: case 0x12: case 0x13:
        case 0x14: case 0x15: case 0x16: case 0x17:
            sound_wave_write(((Addr & 0x4) >> 2), Addr & 3, Value);
            break;
        case 0x18:
        case 0x19:
        case 0x1a:
        case 0x1b:
        case 0x1c:
            sound_dma_write(Addr & 0x07, Value);
            break;
        case 0x28:
        case 0x29:
        case 0x2a:
            sound_noise_write(Addr & 0x07, Value);
            break;
        case 0x08: case 0x09: case 0x0a: case 0x0b: case 0x0c: case 0x0d:
            dma_write(Addr, Value);
            break;
    }
}

void Wr6502(register word Addr, register byte Value)
{
    Addr &= 0xffff;
    switch (Addr >> 12) {
        case 0x0:
        case 0x1:
            lowerRam[Addr] = Value;
            return;
        case 0x2:
        case 0x3:
            memorymap_registers_write(Addr, Value);
            return;
        case 0x4:
        case 0x5:
            upperRam[Addr & 0x1fff] = Value;
            return;
    }
}

byte Rd6502(register word Addr)
{
    Addr &= 0xffff;
    switch (Addr >> 12) {
        case 0x0:
        case 0x1:
            return lowerRam[Addr];
        case 0x2:
        case 0x3:
            return memorymap_registers_read(Addr);
        case 0x4:
        case 0x5:
            return upperRam[Addr & 0x1fff];
        case 0x6:
        case 0x7:
            return Addr >> 8; // Not usable
        case 0x8:
        case 0x9:
        case 0xa:
        case 0xb:
            return lowerRomBank[Addr & 0x3fff];
        case 0xc:
        case 0xd:
        case 0xe:
        case 0xf:
            return upperRomBank[Addr & 0x3fff];
    }
    return 0xff;
}

BOOL memorymap_load(const uint8 *rom, uint32 size)
{
    if ((size & 0x3fff) || size == 0 || rom == NULL) {
        return FALSE;
    }
    programRomSize = size;
    programRom = rom;
    isMAGNUM = size > 131072;
    return TRUE;
}

void memorymap_save_state(FILE *fp)
{
    uint8 ibank = 0;
    fwrite(regs,     0x2000, 1, fp);
    fwrite(lowerRam, 0x2000, 1, fp);
    fwrite(upperRam, 0x2000, 1, fp);

    ibank = (uint8)((lowerRomBank - programRom) / 0x4000);
    WRITE_uint8(ibank, fp);

    WRITE_BOOL(dma_finished, fp);
    WRITE_BOOL(timer_shot,   fp);
}

void memorymap_load_state(FILE *fp)
{
    uint8 ibank = 0;
    fread(regs,     0x2000, 1, fp);
    fread(lowerRam, 0x2000, 1, fp);
    fread(upperRam, 0x2000, 1, fp);

    READ_uint8(ibank, fp);
    lowerRomBank = programRom + ibank * 0x4000;

    READ_BOOL(dma_finished, fp);
    READ_BOOL(timer_shot,   fp);
}

uint32 memorymap_save_state_buf_size(void)
{
    return 0x2000 +
           0x2000 +
           0x2000 +
           sizeof(uint8) +
           sizeof(uint8) +
           sizeof(uint8);
}

void memorymap_save_state_buf(uint8 *data)
{
    uint8 ibank = 0;

    memcpy(data, regs, 0x2000);
    data += 0x2000;

    memcpy(data, lowerRam, 0x2000);
    data += 0x2000;

    memcpy(data, upperRam, 0x2000);
    data += 0x2000;

    ibank = (uint8)((lowerRomBank - programRom) / 0x4000);
    WRITE_BUF_uint8(ibank, data);

    WRITE_BUF_BOOL(dma_finished, data);
    WRITE_BUF_BOOL(timer_shot,   data);
}

void memorymap_load_state_buf(const uint8 *data)
{
    uint8 ibank = 0;

    memcpy(regs, data, 0x2000);
    data += 0x2000;

    memcpy(lowerRam, data, 0x2000);
    data += 0x2000;

    memcpy(upperRam, data, 0x2000);
    data += 0x2000;

    READ_BUF_uint8(ibank, data);
    lowerRomBank = programRom + ibank * 0x4000;

    READ_BUF_BOOL(dma_finished, data);
    READ_BUF_BOOL(timer_shot,   data);
}

uint8 *memorymap_getLowerRamPointer(void)
{
    return lowerRam;
}

uint8 *memorymap_getUpperRamPointer(void)
{
    return upperRam;
}

uint8 *memorymap_getRegisters(void)
{
    return regs;
}

const uint8 *memorymap_getRomPointer(void)
{
    return programRom;
}

const uint8 *memorymap_getLowerRomBank(void)
{
    return lowerRomBank;
}

const uint8 *memorymap_getUpperRomBank(void)
{
    return upperRomBank;
}
