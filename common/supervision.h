/**
 * \file supervision.h
 */

#ifndef __SUPERVISION_H__
#define __SUPERVISION_H__

#include "types.h"

#ifdef __cplusplus
extern "C" {
#endif

#define SV_CORE_VERSION 0x01000005U
#define SV_CORE_VERSION_MAJOR ((SV_CORE_VERSION >> 24) & 0xFF)
#define SV_CORE_VERSION_MINOR ((SV_CORE_VERSION >> 12) & 0xFFF)
#define SV_CORE_VERSION_PATCH ((SV_CORE_VERSION >>  0) & 0xFFF)

/*! Screen width.  */
#define SV_W 160
/*! Screen height. */
#define SV_H 160
/*!
 * \sa supervision_set_map_func()
 */
typedef uint16 (*SV_MapRGBFunc)(uint8 r, uint8 g, uint8 b);
/*!
 * \sa supervision_set_color_scheme()
 */
enum SV_COLOR {
      SV_COLOR_SCHEME_DEFAULT = 0
    , SV_COLOR_SCHEME_AMBER
    , SV_COLOR_SCHEME_GREEN
    , SV_COLOR_SCHEME_BLUE
    , SV_COLOR_SCHEME_BGB
    , SV_COLOR_SCHEME_WATAROO
    , SV_COLOR_SCHEME_GB_DMG
    , SV_COLOR_SCHEME_GB_POCKET
    , SV_COLOR_SCHEME_GB_LIGHT
    , SV_COLOR_SCHEME_BLOSSOM_PINK
    , SV_COLOR_SCHEME_BUBBLES_BLUE
    , SV_COLOR_SCHEME_BUTTERCUP_GREEN
    , SV_COLOR_SCHEME_DIGIVICE
    , SV_COLOR_SCHEME_GAME_COM
    , SV_COLOR_SCHEME_GAMEKING
    , SV_COLOR_SCHEME_GAME_MASTER
    , SV_COLOR_SCHEME_GOLDEN_WILD
    , SV_COLOR_SCHEME_GREENSCALE
    , SV_COLOR_SCHEME_HOKAGE_ORANGE
    , SV_COLOR_SCHEME_LABO_FAWN
    , SV_COLOR_SCHEME_LEGENDARY_SUPER_SAIYAN
    , SV_COLOR_SCHEME_MICROVISION
    , SV_COLOR_SCHEME_MILLION_LIVE_GOLD
    , SV_COLOR_SCHEME_ODYSSEY_GOLD
    , SV_COLOR_SCHEME_SHINY_SKY_BLUE
    , SV_COLOR_SCHEME_SLIME_BLUE
    , SV_COLOR_SCHEME_TI_83
    , SV_COLOR_SCHEME_TRAVEL_WOOD
    , SV_COLOR_SCHEME_VIRTUAL_BOY

    , SV_COLOR_SCHEME_COUNT
};

/*!
 * \sa supervision_set_ghosting()
 */
#define SV_GHOSTING_MAX 8
 /*!
  * \sa supervision_update_sound()
  */
#define SV_SAMPLE_RATE 44100

void supervision_init(void);
void supervision_reset(void);
void supervision_done(void);
/*!
 * \return TRUE - success, FALSE - error
 */
BOOL supervision_load(const uint8 *rom, uint32 romSize);
void supervision_exec(uint16 *backbuffer, BOOL skipFrame);
void supervision_exec_ex(uint16 *backbuffer, int16 backbufferWidth, BOOL skipFrame);

/*!
 * \param data Bits 0-7: Right, Left, Down, Up, B, A, Select, Start.
 */
void supervision_set_input(uint8 data);
/*!
 * \param func Default: RGB888 -> RGB555 (RGBA5551), R - least significant.
 */
void supervision_set_map_func(SV_MapRGBFunc func);
/*!
 * \param colorSheme in range [0, SV_COLOR_SCHEME_COUNT - 1] or SV_COLOR_* constants.
 * \sa SV_COLOR
 */
void supervision_set_color_scheme(int colorScheme);
/*!
 * Add ghosting (blur). It reduces flickering.
 * \param frameCount in range [0, SV_GHOSTING_MAX]. 0 - disable.
 */
void supervision_set_ghosting(int frameCount);
/*!
 * Generate U8 (0 - 45), 2 channels.
 * \param len in bytes.
 */
void supervision_update_sound(uint8 *stream, uint32 len);

/*!
 * Save state to '{statePath}{id}.svst' if id >= 0, otherwise '{statePath}'.
 * \return TRUE - success, FALSE - error
 */
BOOL supervision_save_state(const char *statePath, int8 id);
/*!
 * Load state from '{statePath}{id}.svst' if id >= 0, otherwise '{statePath}'.
 * \return TRUE - success, FALSE - error
 */
BOOL supervision_load_state(const char *statePath, int8 id);

uint32 supervision_save_state_buf_size(void);
BOOL supervision_save_state_buf(uint8 *data, uint32 size);
BOOL supervision_load_state_buf(const uint8 *data, uint32 size);

#ifdef __cplusplus
}
#endif

#endif
