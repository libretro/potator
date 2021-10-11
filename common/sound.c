#include "sound.h"

#include "memorymap.h"
#include "./m6502/m6502.h"

#include "supervision.h" // SV_SAMPLE_RATE

#include <string.h>

#define UNSCALED_CLOCK 4000000

typedef struct {
    uint8 reg[4];
    int on;
    uint8 waveform, volume;
    uint16 pos, size;
    uint16 count;
} SVISION_CHANNEL;
SVISION_CHANNEL m_channel[2];
// For clear sound (no grating), sync with m_channel
SVISION_CHANNEL ch[2];

typedef struct  {
    uint8 reg[3];
    int on, right, left, play;
    uint8 type; // 6 - 7-Bit, 14 - 15-Bit
    uint16 state;
    uint8 value, volume;
    uint16 count;
    real pos, step;
} SVISION_NOISE;
SVISION_NOISE m_noise;

typedef struct  {
    uint8 reg[5];
    int on, right, left;
    uint32 ca14to16;
    uint16 start;
    uint16 size;
    real pos, step;
} SVISION_DMA;
SVISION_DMA m_dma;

void sound_reset(void)
{
    memset(m_channel, 0, sizeof(m_channel));
    memset(&m_noise,  0, sizeof(m_noise)  );
    memset(&m_dma,    0, sizeof(m_dma)    );

    memset(ch,        0, sizeof(ch)       );
}

void sound_stream_update(uint8 *stream, uint32 len)
{
    size_t i, j;
    SVISION_CHANNEL *channel;
    uint8 s = 0;
    uint8 *left  = stream + 0;
    uint8 *right = stream + 1;

    for (i = 0; i < len >> 1; i++, left += 2, right += 2) {
        *left = *right = 0;

        for (channel = m_channel, j = 0; j < 2; j++, channel++) {
            if (ch[j].size != 0) {
                if (ch[j].on || channel->count != 0) {
                    BOOL on = FALSE;
                    switch (ch[j].waveform) {
                        case 0: // 12.5%
                            on = ch[j].pos < (28 * ch[j].size) >> 5;
                            break;
                        case 1: // 25%
                            on = ch[j].pos < (24 * ch[j].size) >> 5;
                            break;
                        case 2: // 50%
                            on = ch[j].pos < ch[j].size / 2;
                            break;
                        case 3: // 75%
                            on = ch[j].pos < ch[j].size / 4;
                            // MESS/MAME:  <= (9 * ch[j].size) >> 5;
                            break;
                    }
                    s = on ? ch[j].volume : 0;
                    if (j == 0) {
                        *right += s;
                    }
                    else {
                        *left += s;
                    }
                }
                ch[j].pos++;
                if (ch[j].pos >= ch[j].size) {
                    ch[j].pos = 0;
#ifndef SV_DISABLE_SUPER_DUPER_WAVE
                    // Transition from off to on
                    if (channel->on) {
                        memcpy(&ch[j], channel, sizeof(ch[j]));
                        channel->on = FALSE;
                    }
#endif
                }
            }
        }

        if (m_noise.on && (m_noise.play || m_noise.count != 0)) {
            s = m_noise.value * m_noise.volume;
            if (m_noise.left)
                *left += s;
            if (m_noise.right)
                *right += s;
            m_noise.pos += m_noise.step;
            while (m_noise.pos >= 1.0) { // if/while difference - Pacific Battle
                // LFSR: x^2 + x + 1
                uint16 feedback;
                m_noise.value = m_noise.state & 1;
                feedback = ((m_noise.state >> 1) ^ m_noise.state) & 0x0001;
                feedback <<= m_noise.type;
                m_noise.state = (m_noise.state >> 1) | feedback;
                m_noise.pos -= 1.0;
            }
        }

        if (m_dma.on) {
            uint8 sample;
            uint16 addr = m_dma.start + (uint16)m_dma.pos / 2;
            if (addr >= 0x8000 && addr < 0xc000) {
                sample = memorymap_getRomPointer()[(addr & 0x3fff) | m_dma.ca14to16];
            }
            else {
                sample = Rd6502(addr);
            }
            if (((uint16)m_dma.pos) & 1)
                s = (sample & 0xf);
            else
                s = (sample & 0xf0) >> 4;
            if (m_dma.left)
                *left += s;
            if (m_dma.right)
                *right += s;
            m_dma.pos += m_dma.step;
            if (m_dma.pos >= m_dma.size) {
                m_dma.on = FALSE;
                memorymap_set_dma_finished();
            }
        }
    }
}

void sound_decrement(void)
{
    if (m_channel[0].count > 0)
        m_channel[0].count--;
    if (m_channel[1].count > 0)
        m_channel[1].count--;
    if (m_noise.count > 0)
        m_noise.count--;
}

void sound_wave_write(int which, int offset, uint8 data)
{
    SVISION_CHANNEL *channel = &m_channel[which];

    channel->reg[offset] = data;
    switch (offset) {
        case 0:
        case 1: {
            uint16 size;
            size = channel->reg[0] | ((channel->reg[1] & 7) << 8);
            // if size == 0 then channel->size == 0
            channel->size = (uint16)((real)SV_SAMPLE_RATE * ((size + 1) << 5) / UNSCALED_CLOCK);
            channel->pos = 0;
#ifndef SV_DISABLE_SUPER_DUPER_WAVE
            // Popo Team
            if (channel->count != 0 || ch[which].size == 0 || channel->size == 0) {
                ch[which].size = channel->size;
                if (channel->count == 0)
                    ch[which].pos = 0;
            }
#else
            memcpy(&ch[which], channel, sizeof(ch[which]));
#endif
        }
            break;
        case 2:
            channel->on       =  data & 0x40;
            channel->waveform = (data & 0x30) >> 4;
            channel->volume   =  data & 0x0f;
#ifndef SV_DISABLE_SUPER_DUPER_WAVE
            if (!channel->on || ch[which].size == 0 || channel->size == 0) {
                uint16 pos = ch[which].pos;
                memcpy(&ch[which], channel, sizeof(ch[which]));
                if (channel->count != 0) // Journey to the West
                    ch[which].pos = pos;
            }
#else
            memcpy(&ch[which], channel, sizeof(ch[which]));
#endif
            break;
        case 3:
            channel->count = data + 1;
#ifndef SV_DISABLE_SUPER_DUPER_WAVE
            ch[which].size = channel->size; // Sonny Xpress!
#endif
            break;
    }
}

void sound_dma_write(int offset, uint8 data)
{
    m_dma.reg[offset] = data;
    switch (offset) {
        case 0:
        case 1:
            m_dma.start = (m_dma.reg[0] | (m_dma.reg[1] << 8));
            break;
        case 2:
            m_dma.size = (data ? data : 0x100) * 32; // Number of 4-bit samples
            break;
        case 3:
            // Test games: Classic Casino, SSSnake
            m_dma.step = UNSCALED_CLOCK / ((real)SV_SAMPLE_RATE * (256 << (data & 3)));
            // MESS/MAME. Wrong
            //m_dma.step  = UNSCALED_CLOCK / (256.0 * SV_SAMPLE_RATE * (1 + (data & 3)));
            m_dma.right = data & 4;
            m_dma.left  = data & 8;
            m_dma.ca14to16 = ((data & 0x70) >> 4) << 14;
            break;
        case 4:
            m_dma.on = data & 0x80;
            if (m_dma.on) {
                m_dma.pos = 0.0;
            }
            break;
    }
}

void sound_noise_write(int offset, uint8 data)
{
    m_noise.reg[offset] = data;
    switch (offset) {
        case 0: {
            // Wataroo >= v0.7.1.0
            uint32 divisor = 8 << (data >> 4);
            // EQU_Watara.asm from Wataroo
            //uint32 divisor = 16 << (data >> 4);
            //if ((data >> 4) == 0) divisor = 8; // 500KHz are too many anyway
            //else if ((data >> 4) > 0xd) divisor >>= 2;
            m_noise.step = UNSCALED_CLOCK / ((real)SV_SAMPLE_RATE * divisor);
            // MESS/MAME. Wrong
            //m_noise.step = UNSCALED_CLOCK / (256.0 * SV_SAMPLE_RATE * (1 + (data >> 4)));
            m_noise.volume = data & 0xf;
        }
            break;
        case 1:
            m_noise.count = data + 1;
            break;
        case 2:
            m_noise.type  = (data & 1) ? 14 : 6;
            m_noise.play  =  data & 2;
            m_noise.right =  data & 4;
            m_noise.left  =  data & 8;
            m_noise.on    =  data & 0x10; /* honey bee start */
            m_noise.state = 1;
            break;
    }
    m_noise.pos = 0.0;
}

// X-Macros

#define EXPAND_CHANNEL \
    X(BOOL, on) \
    X(uint8, waveform) \
    X(uint8, volume) \
    X(uint16, pos) \
    X(uint16, size) \
    X(uint16, count)

#define EXPAND_NOISE \
    X(BOOL, on) \
    X(BOOL, right) \
    X(BOOL, left) \
    X(BOOL, play) \
    X(uint8, type) \
    X(uint16, state) \
    X(uint8, value) \
    X(uint8, volume) \
    X(uint16, count) \
    X(real, pos) \
    X(real, step)

#define EXPAND_DMA \
    X(BOOL, on) \
    X(BOOL, right) \
    X(BOOL, left) \
    X(uint32, ca14to16) \
    X(uint16, start) \
    X(uint16, size) \
    X(real, pos) \
    X(real, step)

void sound_save_state(FILE *fp)
{
    int i;
    for (i = 0; i < 2; i++) {
        fwrite(m_channel[i].reg, sizeof(m_channel[i].reg), 1, fp);
#define X(type, member) WRITE_##type(m_channel[i].member, fp);
        EXPAND_CHANNEL
#undef X
    }

    fwrite(m_noise.reg, sizeof(m_noise.reg), 1, fp);
#define X(type, member) WRITE_##type(m_noise.member, fp);
    EXPAND_NOISE
#undef X
    fwrite(m_dma.reg, sizeof(m_dma.reg), 1, fp);
#define X(type, member) WRITE_##type(m_dma.member, fp);
    EXPAND_DMA
#undef X
}

void sound_load_state(FILE *fp)
{
    int i;

    sound_reset();

    for (i = 0; i < 2; i++) {
        fread(m_channel[i].reg, sizeof(m_channel[i].reg), 1, fp);
#define X(type, member) READ_##type(m_channel[i].member, fp);
        EXPAND_CHANNEL
#undef X
    }
 
    fread(m_noise.reg, sizeof(m_noise.reg), 1, fp);
#define X(type, member) READ_##type(m_noise.member, fp);
    EXPAND_NOISE
#undef X
    fread(m_dma.reg, sizeof(m_dma.reg), 1, fp);
#define X(type, member) READ_##type(m_dma.member, fp);
    EXPAND_DMA
#undef X
}

uint32 sound_save_state_buf_size(void)
{
    uint32 size = 0;
    int i;

    for (i = 0; i < 2; i++) {
        size += sizeof(m_channel[i].reg);
        size += sizeof(uint8);
        size += sizeof(uint8);
        size += sizeof(uint8);
        size += sizeof(uint16);
        size += sizeof(uint16);
        size += sizeof(uint16);
    }

    size += sizeof(m_noise.reg);
    size += sizeof(uint8);
    size += sizeof(uint8);
    size += sizeof(uint8);
    size += sizeof(uint8);
    size += sizeof(uint8);
    size += sizeof(uint16);
    size += sizeof(uint8);
    size += sizeof(uint8);
    size += sizeof(uint16);
    size += sizeof(real);
    size += sizeof(real);

    size += sizeof(m_dma.reg);

    size += sizeof(uint8);
    size += sizeof(uint8);
    size += sizeof(uint8);
    size += sizeof(uint32);
    size += sizeof(uint16);
    size += sizeof(uint16);
    size += sizeof(real);
    size += sizeof(real);

    return size;
}

void sound_save_state_buf(uint8 *data)
{
    int i;
    for (i = 0; i < 2; i++) {
        memcpy(data, m_channel[i].reg, sizeof(m_channel[i].reg));
        data += sizeof(m_channel[i].reg);
#define X(type, member) WRITE_BUF_##type(m_channel[i].member, data);
        EXPAND_CHANNEL
#undef X
    }

    memcpy(data, m_noise.reg, sizeof(m_noise.reg));
    data += sizeof(m_noise.reg);
#define X(type, member) WRITE_BUF_##type(m_noise.member, data);
    EXPAND_NOISE
#undef X

    memcpy(data, m_dma.reg, sizeof(m_dma.reg));
    data += sizeof(m_dma.reg);
#define X(type, member) WRITE_BUF_##type(m_dma.member, data);
    EXPAND_DMA
#undef X
}

void sound_load_state_buf(const uint8 *data)
{
    int i;

    sound_reset();

    for (i = 0; i < 2; i++) {
        memcpy(m_channel[i].reg, data, sizeof(m_channel[i].reg));
        data += sizeof(m_channel[i].reg);
#define X(type, member) READ_BUF_##type(m_channel[i].member, data);
        EXPAND_CHANNEL
#undef X
    }

    memcpy(m_noise.reg, data, sizeof(m_noise.reg));
    data += sizeof(m_noise.reg);
#define X(type, member) READ_BUF_##type(m_noise.member, data);
    EXPAND_NOISE
#undef X

    memcpy(m_dma.reg, data, sizeof(m_dma.reg));
    data += sizeof(m_dma.reg);
#define X(type, member) READ_BUF_##type(m_dma.member, data);
    EXPAND_DMA
#undef X
}
