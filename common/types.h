#ifndef __TYPES_H__
#define __TYPES_H__

#include <string.h> /* For memcpy() */

#ifdef __cplusplus
extern "C" {
#endif

#ifndef _uint8_DEFINED
#define _uint8_DEFINED

#  if (__STDC_VERSION__ >= 199901L) || \
      (__cplusplus      >= 201103L) || (_MSC_VER >= 1600)
#  include <stdint.h>
    typedef uint8_t   uint8;
    typedef uint16_t  uint16;
    typedef uint32_t  uint32; /*
    typedef uint64_t  uint64; */
    typedef int8_t     int8;
    typedef int16_t    int16;
    typedef int32_t    int32; /*
    typedef int64_t    int64;  */
#  else
    typedef unsigned char       uint8;
    typedef unsigned short      uint16;
    typedef unsigned int        uint32; /*
    typedef unsigned long long  uint64;  C99 */
    typedef signed char          int8;
    typedef signed short         int16;
    typedef signed int           int32; /*
    typedef signed long long     int64;  C99 */
#  endif

#endif

#ifndef TRUE
#define TRUE  1
#define FALSE 0
#endif

#ifndef BOOL
#define BOOL int
#endif

#ifdef SV_USE_FLOATS
typedef float real;
#else 
typedef double real;
#endif

/*
 * Endian
 */

/* Don't forget LSB_FIRST in m6502.h */
#ifdef __BIG_ENDIAN_BITFIELD
static uint16 SV_Swap16(uint16 x)
{
    return (uint16)((x << 8) | (x >> 8));
}

static uint32 SV_Swap32(uint32 x)
{
    return (uint32)((x << 24) | ((x << 8) & 0x00FF0000) |
        ((x >> 8) & 0x0000FF00) | (x >> 24));
}

static double SV_SwapDouble(const double x)
{
    double res;
    char *dinp = (char*)&x;
    char *dout = (char*)&res;
    dout[0] = dinp[7]; dout[1] = dinp[6];
    dout[2] = dinp[5]; dout[3] = dinp[4];
    dout[4] = dinp[3]; dout[5] = dinp[2];
    dout[6] = dinp[1]; dout[7] = dinp[0];
    return res;
}
#define SV_SwapLE16(X)     SV_Swap16(X)
#define SV_SwapLE32(X)     SV_Swap32(X)
#define SV_SwapLEDouble(X) SV_SwapDouble(X)
#else
#define SV_SwapLE16(X)     (X)
#define SV_SwapLE32(X)     (X)
#define SV_SwapLEDouble(X) (X)
#endif

/*
 * File
 */

#define WRITE_BOOL(x, fp) do { \
    uint8 _ = x ? 1 : 0; \
    fwrite(&_, 1, 1, fp); } while (0)
#define  READ_BOOL(x, fp) do { \
    uint8 _; \
     fread(&_, 1, 1, fp); \
    x = _ ? TRUE : FALSE; } while (0)

#define WRITE_uint8(x, fp)        do { \
    fwrite(&x, sizeof(x), 1, fp); } while (0)
#define  READ_uint8(x, fp)        do { \
     fread(&x, sizeof(x), 1, fp); } while (0)

#define WRITE_int8(x, fp)  WRITE_uint8(x, fp)
#define  READ_int8(x, fp)   READ_uint8(x, fp)

#define WRITE_uint16(x, fp)       do { \
    uint16 _ = SV_SwapLE16(x); \
    fwrite(&_, sizeof(x), 1, fp); } while (0)
#define  READ_uint16(x, fp)       do { \
     fread(&x, sizeof(x), 1, fp); \
    x = SV_SwapLE16(x);           } while (0)

#define WRITE_int16(x, fp)  WRITE_uint16(x, fp)
#define  READ_int16(x, fp)   READ_uint16(x, fp)

#define WRITE_uint32(x, fp)       do { \
    uint32 _ = SV_SwapLE32(x); \
    fwrite(&_, sizeof(x), 1, fp); } while (0)
#define  READ_uint32(x, fp)       do { \
     fread(&x, sizeof(x), 1, fp); \
    x = SV_SwapLE32(x);           } while (0)

#define WRITE_int32(x, fp)  WRITE_uint32(x, fp)
#define  READ_int32(x, fp)   READ_uint32(x, fp)

#define WRITE_real(x, fp)         do { \
    double _ = SV_SwapLEDouble(x); \
    fwrite(&_, sizeof(_), 1, fp); } while (0)
#define  READ_real(x, fp)         do { \
    double _; \
     fread(&_, sizeof(_), 1, fp); \
    x = (real)SV_SwapLEDouble(_); } while (0)

/*
 * Buffer
 */

#define WRITE_BUF_BOOL(x, buf) do { \
    uint8 _ = x ? 1 : 0; \
    memcpy(buf, &_, 1); \
    buf += 1; } while (0)
#define  READ_BUF_BOOL(x, buf) do { \
    uint8 _; \
    memcpy(&_, buf, 1); \
    buf += 1; \
    x = _ ? TRUE : FALSE; } while (0)

#define WRITE_BUF_uint8(x, buf)        do { \
    memcpy(buf, &x, sizeof(x)); \
    buf += sizeof(x); } while (0)
#define  READ_BUF_uint8(x, buf)        do { \
    memcpy(&x, buf, sizeof(x)); \
    buf += sizeof(x); } while (0)

#define WRITE_BUF_int8(x, buf)  WRITE_BUF_uint8(x, buf)
#define  READ_BUF_int8(x, buf)   READ_BUF_uint8(x, buf)

#define WRITE_BUF_uint16(x, buf)       do { \
    memcpy(buf, &x, sizeof(x)); \
    buf += sizeof(x); } while (0)
#define  READ_BUF_uint16(x, buf)       do { \
    memcpy(&x, buf, sizeof(x)); \
    buf += sizeof(x); } while (0)

#define WRITE_BUF_int16(x, buf)  WRITE_BUF_uint16(x, buf)
#define  READ_BUF_int16(x, buf)   READ_BUF_uint16(x, buf)

#define WRITE_BUF_uint32(x, buf)       do { \
    memcpy(buf, &x, sizeof(x)); \
    buf += sizeof(x); } while (0)
#define  READ_BUF_uint32(x, buf)       do { \
    memcpy(&x, buf, sizeof(x)); \
    buf += sizeof(x); } while (0)

#define WRITE_BUF_int32(x, buf)  WRITE_BUF_uint32(x, buf)
#define  READ_BUF_int32(x, buf)   READ_BUF_uint32(x, buf)

#define WRITE_BUF_real(x, buf)         do { \
    memcpy(buf, &x, sizeof(x)); \
    buf += sizeof(x); } while (0)
#define  READ_BUF_real(x, buf)         do { \
    memcpy(&x, buf, sizeof(x)); \
    buf += sizeof(x); } while (0)

#ifdef __cplusplus
}
#endif

#endif
