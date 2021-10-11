#ifndef __SOUND_H__
#define __SOUND_H__

#include "types.h"

#include <stdio.h>

void sound_reset(void);
/*!
 * Generate U8 (0 - 45), 2 channels.
 * \param len in bytes.
 */
void sound_stream_update(uint8 *stream, uint32 len);
void sound_decrement(void);
void sound_wave_write(int which, int offset, uint8 data);
void sound_dma_write(int offset, uint8 data);
void sound_noise_write(int offset, uint8 data);

void sound_save_state(FILE *fp);
void sound_load_state(FILE *fp);

uint32 sound_save_state_buf_size(void);
void sound_save_state_buf(uint8 *data);
void sound_load_state_buf(const uint8 *data);

#endif
