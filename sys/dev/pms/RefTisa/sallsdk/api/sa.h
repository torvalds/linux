/*******************************************************************************
*Copyright (c) 2014 PMC-Sierra, Inc.  All rights reserved. 
*
*Redistribution and use in source and binary forms, with or without modification, are permitted provided 
*that the following conditions are met: 
*1. Redistributions of source code must retain the above copyright notice, this list of conditions and the
*following disclaimer. 
*2. Redistributions in binary form must reproduce the above copyright notice, 
*this list of conditions and the following disclaimer in the documentation and/or other materials provided
*with the distribution. 
*
*THIS SOFTWARE IS PROVIDED BY THE AUTHOR AND CONTRIBUTORS ``AS IS'' AND ANY EXPRESS OR IMPLIED 
*WARRANTIES,INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS
*FOR A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE AUTHOR OR CONTRIBUTORS BE LIABLE
*FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT 
*NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR 
*BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT 
*LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS 
*SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE
*
* $FreeBSD$
*
********************************************************************************/
/*******************************************************************************/
/*! \file sa.h
 *  \brief The file defines the constants, data structure, and functions defined by LL API
 */
/******************************************************************************/

#ifndef  __SA_H__
#define __SA_H__

#include <dev/pms/RefTisa/sallsdk/api/sa_spec.h>
#include <dev/pms/RefTisa/sallsdk/api/sa_err.h>

/* TestBase needed to have the 'Multi-Data fetch disable' feature */
#define SA_CONFIG_MDFD_REGISTRY

#define OSSA_OFFSET_OF(STRUCT_TYPE, FEILD)              \
        (bitptr)&(((STRUCT_TYPE *)0)->FEILD)

#if defined(SA_CPU_LITTLE_ENDIAN)

#define OSSA_WRITE_LE_16(AGROOT, DMA_ADDR, OFFSET, VALUE16)     \
        (*((bit16 *)(((bit8 *)DMA_ADDR)+(OFFSET)))) = (bit16)(VALUE16);

#define OSSA_WRITE_LE_32(AGROOT, DMA_ADDR, OFFSET, VALUE32)     \
        (*((bit32 *)(((bit8 *)DMA_ADDR)+(OFFSET)))) = (bit32)(VALUE32);

#define OSSA_READ_LE_16(AGROOT, ADDR16, DMA_ADDR, OFFSET)       \
        (*((bit16 *)ADDR16)) = (*((bit16 *)(((bit8 *)DMA_ADDR)+(OFFSET))))

#define OSSA_READ_LE_32(AGROOT, ADDR32, DMA_ADDR, OFFSET)       \
        (*((bit32 *)ADDR32)) = (*((bit32 *)(((bit8 *)DMA_ADDR)+(OFFSET))))

#define OSSA_WRITE_BE_16(AGROOT, DMA_ADDR, OFFSET, VALUE16)     \
        (*((bit8 *)(((bit8 *)DMA_ADDR)+(OFFSET))))   = (bit8)((((bit16)VALUE16)>>8)&0xFF);  \
        (*((bit8 *)(((bit8 *)DMA_ADDR)+(OFFSET)+1))) = (bit8)(((bit16)VALUE16)&0xFF);

#define OSSA_WRITE_BE_32(AGROOT, DMA_ADDR, OFFSET, VALUE32)     \
        (*((bit8 *)(((bit8 *)DMA_ADDR)+(OFFSET))))   = (bit8)((((bit32)VALUE32)>>24)&0xFF); \
        (*((bit8 *)(((bit8 *)DMA_ADDR)+(OFFSET)+1))) = (bit8)((((bit32)VALUE32)>>16)&0xFF); \
        (*((bit8 *)(((bit8 *)DMA_ADDR)+(OFFSET)+2))) = (bit8)((((bit32)VALUE32)>>8)&0xFF);  \
        (*((bit8 *)(((bit8 *)DMA_ADDR)+(OFFSET)+3))) = (bit8)(((bit32)VALUE32)&0xFF);

#define OSSA_READ_BE_16(AGROOT, ADDR16, DMA_ADDR, OFFSET)       \
        (*(bit8 *)(((bit8 *)ADDR16)+1)) = (*((bit8 *)(((bit8 *)DMA_ADDR)+(OFFSET))));   \
        (*(bit8 *)(((bit8 *)ADDR16)))   = (*((bit8 *)(((bit8 *)DMA_ADDR)+(OFFSET)+1)));

#define OSSA_READ_BE_32(AGROOT, ADDR32, DMA_ADDR, OFFSET)       \
        (*(bit8 *)(((bit8 *)ADDR32)+3)) = (*((bit8 *)(((bit8 *)DMA_ADDR)+(OFFSET))));   \
        (*(bit8 *)(((bit8 *)ADDR32)+2)) = (*((bit8 *)(((bit8 *)DMA_ADDR)+(OFFSET)+1))); \
        (*(bit8 *)(((bit8 *)ADDR32)+1)) = (*((bit8 *)(((bit8 *)DMA_ADDR)+(OFFSET)+2))); \
        (*(bit8 *)(((bit8 *)ADDR32)))   = (*((bit8 *)(((bit8 *)DMA_ADDR)+(OFFSET)+3)));

#define OSSA_WRITE_BYTE_STRING(AGROOT, DEST_ADDR, SRC_ADDR, LEN)                        \
        si_memcpy(DEST_ADDR, SRC_ADDR, LEN);


#elif defined(SA_CPU_BIG_ENDIAN)

#define OSSA_WRITE_LE_16(AGROOT, DMA_ADDR, OFFSET, VALUE16)     \
        (*((bit8 *)(((bit8 *)DMA_ADDR)+(OFFSET)+1))) = (bit8)((((bit16)VALUE16)>>8)&0xFF);   \
        (*((bit8 *)(((bit8 *)DMA_ADDR)+(OFFSET))))   = (bit8)(((bit16)VALUE16)&0xFF);

#define OSSA_WRITE_LE_32(AGROOT, DMA_ADDR, OFFSET, VALUE32)     \
        (*((bit8 *)(((bit8 *)DMA_ADDR)+(OFFSET)+3))) = (bit8)((((bit32)VALUE32)>>24)&0xFF);  \
        (*((bit8 *)(((bit8 *)DMA_ADDR)+(OFFSET)+2))) = (bit8)((((bit32)VALUE32)>>16)&0xFF);  \
        (*((bit8 *)(((bit8 *)DMA_ADDR)+(OFFSET)+1))) = (bit8)((((bit32)VALUE32)>>8)&0xFF);   \
        (*((bit8 *)(((bit8 *)DMA_ADDR)+(OFFSET))))   = (bit8)(((bit32)VALUE32)&0xFF);

#define OSSA_READ_LE_16(AGROOT, ADDR16, DMA_ADDR, OFFSET)       \
        (*(bit8 *)(((bit8 *)ADDR16)+1)) = (*((bit8 *)(((bit8 *)DMA_ADDR)+(OFFSET))));   \
        (*(bit8 *)(((bit8 *)ADDR16)))   = (*((bit8 *)(((bit8 *)DMA_ADDR)+(OFFSET)+1)));

#define OSSA_READ_LE_32(AGROOT, ADDR32, DMA_ADDR, OFFSET)       \
        (*((bit8 *)(((bit8 *)ADDR32)+3))) = (*((bit8 *)(((bit8 *)DMA_ADDR)+(OFFSET))));   \
        (*((bit8 *)(((bit8 *)ADDR32)+2))) = (*((bit8 *)(((bit8 *)DMA_ADDR)+(OFFSET)+1))); \
        (*((bit8 *)(((bit8 *)ADDR32)+1))) = (*((bit8 *)(((bit8 *)DMA_ADDR)+(OFFSET)+2))); \
        (*((bit8 *)(((bit8 *)ADDR32))))   = (*((bit8 *)(((bit8 *)DMA_ADDR)+(OFFSET)+3)));

#define OSSA_WRITE_BE_16(AGROOT, DMA_ADDR, OFFSET, VALUE16)         \
        (*((bit16 *)(((bit8 *)DMA_ADDR)+(OFFSET)))) = (bit16)(VALUE16);

#define OSSA_WRITE_BE_32(AGROOT, DMA_ADDR, OFFSET, VALUE32)         \
        (*((bit32 *)(((bit8 *)DMA_ADDR)+(OFFSET)))) = (bit32)(VALUE32);

#define OSSA_READ_BE_16(AGROOT, ADDR16, DMA_ADDR, OFFSET)           \
        (*((bit16 *)ADDR16)) = (*((bit16 *)(((bit8 *)DMA_ADDR)+(OFFSET))));

#define OSSA_READ_BE_32(AGROOT, ADDR32, DMA_ADDR, OFFSET)           \
        (*((bit32 *)ADDR32)) = (*((bit32 *)(((bit8 *)DMA_ADDR)+(OFFSET))));

#define OSSA_WRITE_BYTE_STRING(AGROOT, DEST_ADDR, SRC_ADDR, LEN)    \
        si_memcpy(DEST_ADDR, SRC_ADDR, LEN);

#else

#error (Host CPU endianess undefined!!)

#endif

#define AGSA_WRITE_SGL(sglDest, sgLower, sgUpper, len, extReserved)     \
        OSSA_WRITE_LE_32(agRoot, sglDest, 0, sgLower);                  \
        OSSA_WRITE_LE_32(agRoot, sglDest, 4, sgUpper);                  \
        OSSA_WRITE_LE_32(agRoot, sglDest, 8, len);                      \
        OSSA_WRITE_LE_32(agRoot, sglDest, 12, extReserved);


/**************************************************************************
 *                        define byte swap macro                          *
 **************************************************************************/
/*! \def AGSA_FLIP_2_BYTES(_x)
* \brief AGSA_FLIP_2_BYTES macro
*
* use to flip two bytes
*/
#define AGSA_FLIP_2_BYTES(_x) ((bit16)(((((bit16)(_x))&0x00FF)<<8)|  \
                                     ((((bit16)(_x))&0xFF00)>>8)))

/*! \def AGSA_FLIP_4_BYTES(_x)
* \brief AGSA_FLIP_4_BYTES macro
*
* use to flip four bytes
*/
#define AGSA_FLIP_4_BYTES(_x) ((bit32)(((((bit32)(_x))&0x000000FF)<<24)|  \
                                     ((((bit32)(_x))&0x0000FF00)<<8)|   \
                                     ((((bit32)(_x))&0x00FF0000)>>8)|   \
                                     ((((bit32)(_x))&0xFF000000)>>24)))


#if defined(SA_CPU_LITTLE_ENDIAN)

/*! \def LEBIT16_TO_BIT16(_x)
* \brief LEBIT16_TO_BIT16 macro
*
* use to convert little endian bit16 to host bit16
*/
#ifndef LEBIT16_TO_BIT16
#define LEBIT16_TO_BIT16(_x)   (_x)
#endif

/*! \def BIT16_TO_LEBIT16(_x)
* \brief BIT16_TO_LEBIT16 macro
*
* use to convert host bit16 to little endian bit16
*/
#ifndef BIT16_TO_LEBIT16
#define BIT16_TO_LEBIT16(_x)   (_x)
#endif

/*! \def BEBIT16_TO_BIT16(_x)
* \brief BEBIT16_TO_BIT16 macro
*
* use to convert big endian bit16 to host bit16
*/
#ifndef BEBIT16_TO_BIT16
#define BEBIT16_TO_BIT16(_x)   AGSA_FLIP_2_BYTES(_x)
#endif

/*! \def BIT16_TO_BEBIT16(_x)
* \brief BIT16_TO_BEBIT16 macro
*
* use to convert host bit16 to big endian bit16
*/
#ifndef BIT16_TO_BEBIT16
#define BIT16_TO_BEBIT16(_x)   AGSA_FLIP_2_BYTES(_x)
#endif

/*! \def LEBIT32_TO_BIT32(_x)
* \brief LEBIT32_TO_BIT32 macro
*
* use to convert little endian bit32 to host bit32
*/
#ifndef LEBIT32_TO_BIT32
#define LEBIT32_TO_BIT32(_x)   (_x)
#endif

/*! \def BIT32_TO_LEBIT32(_x)
* \brief BIT32_TO_LEBIT32 macro
*
* use to convert host bit32 to little endian bit32
*/
#ifndef BIT32_TO_LEBIT32
#define BIT32_TO_LEBIT32(_x)   (_x)
#endif

/*! \def BEBIT32_TO_BIT32(_x)
* \brief BEBIT32_TO_BIT32 macro
*
* use to convert big endian bit32 to host bit32
*/
#ifndef BEBIT32_TO_BIT32
#define BEBIT32_TO_BIT32(_x)   AGSA_FLIP_4_BYTES(_x)
#endif

/*! \def BIT32_TO_BEBIT32(_x)
* \brief BIT32_TO_BEBIT32 macro
*
* use to convert host bit32 to big endian bit32
*/
#ifndef BIT32_TO_BEBIT32
#define BIT32_TO_BEBIT32(_x)   AGSA_FLIP_4_BYTES(_x)
#endif


/*
 * bit8 to Byte[x] of bit32
 */
#ifndef BIT8_TO_BIT32_B0
#define BIT8_TO_BIT32_B0(_x)   ((bit32)(_x))
#endif

#ifndef BIT8_TO_BIT32_B1
#define BIT8_TO_BIT32_B1(_x)   (((bit32)(_x)) << 8)
#endif

#ifndef BIT8_TO_BIT32_B2
#define BIT8_TO_BIT32_B2(_x)   (((bit32)(_x)) << 16)
#endif

#ifndef BIT8_TO_BIT32_B3
#define BIT8_TO_BIT32_B3(_x)   (((bit32)(_x)) << 24)
#endif

/*
 * Byte[x] of bit32 to bit8
 */
#ifndef BIT32_B0_TO_BIT8
#define BIT32_B0_TO_BIT8(_x)   ((bit8)(((bit32)(_x)) & 0x000000FF))
#endif

#ifndef BIT32_B1_TO_BIT8
#define BIT32_B1_TO_BIT8(_x)   ((bit8)((((bit32)(_x)) & 0x0000FF00) >> 8))
#endif

#ifndef BIT32_B2_TO_BIT8
#define BIT32_B2_TO_BIT8(_x)   ((bit8)((((bit32)(_x)) & 0x00FF0000) >> 16))
#endif

#ifndef BIT32_B3_TO_BIT8
#define BIT32_B3_TO_BIT8(_x)   ((bit8)((((bit32)(_x)) & 0xFF000000) >> 24))
#endif

#elif defined(SA_CPU_BIG_ENDIAN)

/*! \def LEBIT16_TO_BIT16(_x)
* \brief LEBIT16_TO_BIT16 macro
*
* use to convert little endian bit16 to host bit16
*/
#ifndef LEBIT16_TO_BIT16
#define LEBIT16_TO_BIT16(_x)   AGSA_FLIP_2_BYTES(_x)
#endif

/*! \def BIT16_TO_LEBIT16(_x)
* \brief BIT16_TO_LEBIT16 macro
*
* use to convert host bit16 to little endian bit16
*/
#ifndef BIT16_TO_LEBIT16
#define BIT16_TO_LEBIT16(_x)   AGSA_FLIP_2_BYTES(_x)
#endif

/*! \def BEBIT16_TO_BIT16(_x)
* \brief BEBIT16_TO_BIT16 macro
*
* use to convert big endian bit16 to host bit16
*/
#ifndef BEBIT16_TO_BIT16
#define BEBIT16_TO_BIT16(_x)   (_x)
#endif

/*! \def BIT16_TO_BEBIT16(_x)
* \brief BIT16_TO_BEBIT16 macro
*
* use to convert host bit16 to big endian bit16
*/
#ifndef BIT16_TO_BEBIT16
#define BIT16_TO_BEBIT16(_x)   (_x)
#endif

/*! \def LEBIT32_TO_BIT32(_x)
* \brief LEBIT32_TO_BIT32 macro
*
* use to convert little endian bit32 to host bit32
*/
#ifndef LEBIT32_TO_BIT32
#define LEBIT32_TO_BIT32(_x)   AGSA_FLIP_4_BYTES(_x)
#endif

/*! \def BIT32_TO_LEBIT32(_x)
* \brief BIT32_TO_LEBIT32 macro
*
* use to convert host bit32 to little endian bit32
*/
#ifndef BIT32_TO_LEBIT32
#define BIT32_TO_LEBIT32(_x)   AGSA_FLIP_4_BYTES(_x)
#endif

/*! \def BEBIT32_TO_BIT32(_x)
* \brief BEBIT32_TO_BIT32 macro
*
* use to convert big endian bit32 to host bit32
*/
#ifndef BEBIT32_TO_BIT32
#define BEBIT32_TO_BIT32(_x)   (_x)
#endif

/*! \def BIT32_TO_BEBIT32(_x)
* \brief BIT32_TO_BEBIT32 macro
*
* use to convert host bit32 to big endian bit32
*/
#ifndef BIT32_TO_BEBIT32
#define BIT32_TO_BEBIT32(_x)   (_x)
#endif


/*
 * bit8 to Byte[x] of bit32
 */
#ifndef BIT8_TO_BIT32_B0
#define BIT8_TO_BIT32_B0(_x)   (((bit32)(_x)) << 24)
#endif

#ifndef BIT8_TO_BIT32_B1
#define BIT8_TO_BIT32_B1(_x)   (((bit32)(_x)) << 16)
#endif

#ifndef BIT8_TO_BIT32_B2
#define BIT8_TO_BIT32_B2(_x)   (((bit32)(_x)) << 8)
#endif

#ifndef BIT8_TO_BIT32_B3
#define BIT8_TO_BIT32_B3(_x)   ((bit32)(_x))
#endif

/*
 * Byte[x] of bit32 to bit8
 */
#ifndef BIT32_B0_TO_BIT8
#define BIT32_B0_TO_BIT8(_x)   ((bit8)((((bit32)(_x)) & 0xFF000000) >> 24))
#endif

#ifndef BIT32_B1_TO_BIT8
#define BIT32_B1_TO_BIT8(_x)   ((bit8)((((bit32)(_x)) & 0x00FF0000) >> 16))
#endif

#ifndef BIT32_B2_TO_BIT8
#define BIT32_B2_TO_BIT8(_x)   ((bit8)((((bit32)(_x)) & 0x0000FF00) >> 8))
#endif

#ifndef BIT32_B3_TO_BIT8
#define BIT32_B3_TO_BIT8(_x)   ((bit8)(((bit32)(_x)) & 0x000000FF))
#endif

#else

#error No definition of SA_CPU_BIG_ENDIAN or SA_CPU_LITTLE_ENDIAN

#endif


#if defined(SA_DMA_LITTLE_ENDIAN)

/*
 * ** bit32 to bit32
 * */
#ifndef DMA_BIT32_TO_BIT32
#define DMA_BIT32_TO_BIT32(_x)   (_x)
#endif

#ifndef DMA_LEBIT32_TO_BIT32
#define DMA_LEBIT32_TO_BIT32(_x) (_x)
#endif

#ifndef DMA_BEBIT32_TO_BIT32
#define DMA_BEBIT32_TO_BIT32(_x) AGSA_FLIP_4_BYTES(_x)
#endif

#ifndef BIT32_TO_DMA_BIT32
#define BIT32_TO_DMA_BIT32(_x)   (_x)
#endif

#ifndef BIT32_TO_DMA_LEBIT32
#define BIT32_TO_DMA_LEBIT32(_x) (_x)
#endif

#ifndef BIT32_TO_DMA_BEBIT32
#define BIT32_TO_DMA_BEBIT32(_x) AGSA_FLIP_4_BYTES(_x)
#endif


/*
 * ** bit16 to bit16
 * */
#ifndef DMA_BIT16_TO_BIT16
#define DMA_BIT16_TO_BIT16(_x)   (_x)
#endif

#ifndef DMA_LEBIT16_TO_BIT16
#define DMA_LEBIT16_TO_BIT16(_x) (_x)
#endif

#ifndef DMA_BEBIT16_TO_BIT16
#define DMA_BEBIT16_TO_BIT16(_x) AGSA_FLIP_2_BYTES(_x)
#endif

#ifndef BIT16_TO_DMA_BIT16
#define BIT16_TO_DMA_BIT16(_x)   (_x)
#endif

#ifndef BIT16_TO_DMA_LEBIT16
#define BIT16_TO_DMA_LEBIT16(_x) (_x)
#endif

#ifndef BIT16_TO_DMA_BEBIT16
#define BIT16_TO_DMA_BEBIT16(_x) AGSA_FLIP_2_BYTES(_x)
#endif

#if defined(SA_CPU_LITTLE_ENDIAN)

#ifndef BEBIT32_TO_DMA_BEBIT32
#define BEBIT32_TO_DMA_BEBIT32(_x) (_x)
#endif

#ifndef LEBIT32_TO_DMA_LEBIT32
#define LEBIT32_TO_DMA_LEBIT32(_x) (_x)
#endif

#ifndef DMA_LEBIT32_TO_LEBIT32
#define DMA_LEBIT32_TO_LEBIT32(_x) (_x)
#endif

#ifndef DMA_BEBIT32_TO_BEBIT32
#define DMA_BEBIT32_TO_BEBIT32(_x) (_x)
#endif

/*
 * ** bit16 to bit16
 * */
#ifndef BEBIT16_TO_DMA_BEBIT16
#define BEBIT16_TO_DMA_BEBIT16(_x) (_x)
#endif

#ifndef LEBIT16_TO_DMA_LEBIT16
#define LEBIT16_TO_DMA_LEBIT16(_x) (_x)
#endif

#ifndef DMA_LEBIT16_TO_LEBIT16
#define DMA_LEBIT16_TO_LEBIT16(_x) (_x)
#endif

#ifndef DMA_BEBIT16_TO_BEBIT16
#define DMA_BEBIT16_TO_BEBIT16(_x) (_x)
#endif

#else   /* defined(SA_CPU_BIG_ENDIAN) */


/*
 * ** bit32 to bit32
 * */
#ifndef BEBIT32_TO_DMA_BEBIT32
#define BEBIT32_TO_DMA_BEBIT32(_x) AGSA_FLIP_4_BYTES(_x)
#endif

#ifndef LEBIT32_TO_DMA_LEBIT32
#define LEBIT32_TO_DMA_LEBIT32(_x) AGSA_FLIP_4_BYTES(_x)
#endif

#ifndef DMA_LEBIT32_TO_LEBIT32
#define DMA_LEBIT32_TO_LEBIT32(_x) AGSA_FLIP_4_BYTES(_x)
#endif

#ifndef DMA_BEBIT32_TO_BEBIT32
#define DMA_BEBIT32_TO_BEBIT32(_x) AGSA_FLIP_4_BYTES(_x)
#endif

/*
 * ** bit16 to bit16
 * */
#ifndef BEBIT16_TO_DMA_BEBIT16
#define BEBIT16_TO_DMA_BEBIT16(_x) AGSA_FLIP_2_BYTES(_x)
#endif

#ifndef LEBIT16_TO_DMA_LEBIT16
#define LEBIT16_TO_DMA_LEBIT16(_x) AGSA_FLIP_2_BYTES(_x)
#endif

#ifndef DMA_LEBIT16_TO_LEBIT16
#define DMA_LEBIT16_TO_LEBIT16(_x) AGSA_FLIP_2_BYTES(_x)
#endif

#ifndef DMA_BEBIT16_TO_BEBIT16
#define DMA_BEBIT16_TO_BEBIT16(_x) AGSA_FLIP_2_BYTES(_x)
#endif

#endif

/*
 * bit8 to Byte[x] of bit32
 */
#ifndef BIT8_TO_DMA_BIT32_B0
#define BIT8_TO_DMA_BIT32_B0(_x)   ((bit32)(_x))
#endif

#ifndef BIT8_TO_DMA_BIT32_B1
#define BIT8_TO_DMA_BIT32_B1(_x)   (((bit32)(_x)) << 8)
#endif

#ifndef BIT8_TO_DMA_BIT32_B2
#define BIT8_TO_DMA_BIT32_B2(_x)   (((bit32)(_x)) << 16)
#endif

#ifndef BIT8_TO_DMA_BIT32_B3
#define BIT8_TO_DMA_BIT32_B3(_x)   (((bit32)(_x)) << 24)
#endif

/*
 * Byte[x] of bit32 to bit8
 */
#ifndef DMA_BIT32_B0_TO_BIT8
#define DMA_BIT32_B0_TO_BIT8(_x)   ((bit8)(((bit32)(_x)) & 0x000000FF))
#endif

#ifndef DMA_BIT32_B1_TO_BIT8
#define DMA_BIT32_B1_TO_BIT8(_x)   ((bit8)((((bit32)(_x)) & 0x0000FF00) >> 8))
#endif

#ifndef DMA_BIT32_B2_TO_BIT8
#define DMA_BIT32_B2_TO_BIT8(_x)   ((bit8)((((bit32)(_x)) & 0x00FF0000) >> 16))
#endif

#ifndef DMA_BIT32_B3_TO_BIT8
#define DMA_BIT32_B3_TO_BIT8(_x)   ((bit8)((((bit32)(_x)) & 0xFF000000) >> 24))
#endif

/*|                                                                   |
  | end of DMA access macros for LITTLE ENDIAN                        |
  ---------------------------------------------------------------------
 */

#elif defined(SA_DMA_BIG_ENDIAN)                /* DMA big endian */

/*--------------------------------------------------------------------
 | DMA buffer access macros for BIG ENDIAN                           |
 |                                                                   |
 */

/* bit32 to bit32 */
#ifndef DMA_BEBIT32_TO_BIT32
#define DMA_BEBIT32_TO_BIT32(_x)   (_x)
#endif

#ifndef DMA_LEBIT32_TO_BIT32
#define DMA_LEBIT32_TO_BIT32(_x) AGSA_FLIP_4_BYTES(_x)
#endif

#ifndef BIT32_TO_DMA_BIT32
#define BIT32_TO_DMA_BIT32(_x)   (_x)
#endif

#ifndef BIT32_TO_DMA_LEBIT32
#define BIT32_TO_DMA_LEBIT32(_x) AGSA_FLIP_4_BYTES(_x)
#endif

#ifndef BIT32_TO_DMA_BEBIT32
#define BIT32_TO_DMA_BEBIT32(_x) (_x)
#endif

/* bit16 to bit16 */
#ifndef DMA_BEBIT16_TO_BIT16
#define DMA_BEBIT16_TO_BIT16(_x)   (_x)
#endif

#ifndef DMA_LEBIT16_TO_BIT16
#define DMA_LEBIT16_TO_BIT16(_x) AGSA_FLIP_2_BYTES(_x)
#endif

#ifndef BIT16_TO_DMA_BIT16
#define BIT16_TO_DMA_BIT16(_x)   (_x)
#endif

#ifndef BIT16_TO_DMA_LEBIT16
#define BIT16_TO_DMA_LEBIT16(_x) AGSA_FLIP_2_BYTES(_x)
#endif

#ifndef BIT16_TO_DMA_BEBIT16
#define BIT16_TO_DMA_BEBIT16(_x) (_x)
#endif


#if defined(SA_CPU_LITTLE_ENDIAN)           /* CPU little endain */

/* bit32 to bit32 */
#ifndef BEBIT32_TO_DMA_BEBIT32
#define BEBIT32_TO_DMA_BEBIT32(_x) AGSA_FLIP_4_BYTES(_x)
#endif

#ifndef LEBIT32_TO_DMA_LEBIT32
#define LEBIT32_TO_DMA_LEBIT32(_x) AGSA_FLIP_4_BYTES(_x)
#endif

#ifndef DMA_LEBIT32_TO_LEBIT32
#define DMA_LEBIT32_TO_LEBIT32(_x) AGSA_FLIP_4_BYTES(_x)
#endif

#ifndef DMA_BEBIT32_TO_BEBIT32
#define DMA_BEBIT32_TO_BEBIT32(_x) AGSA_FLIP_4_BYTES(_x)
#endif

/* bit16 to bit16 */
#ifndef BEBIT16_TO_DMA_BEBIT16
#define BEBIT16_TO_DMA_BEBIT16(_x) AGSA_FLIP_2_BYTES(_x)
#endif

#ifndef LEBIT16_TO_DMA_LEBIT16
#define LEBIT16_TO_DMA_LEBIT16(_x) AGSA_FLIP_2_BYTES(_x)
#endif

#ifndef DMA_LEBIT16_TO_LEBIT16
#define DMA_LEBIT16_TO_LEBIT16(_x) AGSA_FLIP_2_BYTES(_x)
#endif

#ifndef DMA_BEBIT16_TO_BEBIT16
#define DMA_BEBIT16_TO_BEBIT16(_x) AGSA_FLIP_2_BYTES(_x)
#endif


#else   /* defined(SA_CPU_BIG_ENDIAN) */

/* bit32 to bit32 */
#ifndef BEBIT32_TO_DMA_BEBIT32
#define BEBIT32_TO_DMA_BEBIT32(_x) (_x)
#endif

#ifndef LEBIT32_TO_DMA_LEBIT32
#define LEBIT32_TO_DMA_LEBIT32(_x) (_x)
#endif

#ifndef DMA_LEBIT32_TO_LEBIT32
#define DMA_LEBIT32_TO_LEBIT32(_x) (_x)
#endif

#ifndef DMA_BEBIT32_TO_BEBIT32
#define DMA_BEBIT32_TO_BEBIT32(_x) (_x)
#endif

/* bit16 to bit16 */
#ifndef BEBIT16_TO_DMA_BEBIT16
#define BEBIT16_TO_DMA_BEBIT16(_x) (_x)
#endif

#ifndef LEBIT16_TO_DMA_LEBIT16
#define LEBIT16_TO_DMA_LEBIT16(_x) (_x)
#endif

#ifndef DMA_LEBIT16_TO_LEBIT16
#define DMA_LEBIT16_TO_LEBIT16(_x) (_x)
#endif

#ifndef DMA_BEBIT16_TO_BEBIT16
#define DMA_BEBIT16_TO_BEBIT16(_x) (_x)
#endif

#endif

/*
 * bit8 to Byte[x] of bit32
 */
#ifndef BIT8_TO_DMA_BIT32_B0
#define BIT8_TO_DMA_BIT32_B0(_x)   (((bit32)(_x)) << 24)
#endif

#ifndef BIT8_TO_DMA_BIT32_B1
#define BIT8_TO_DMA_BIT32_B1(_x)   (((bit32)(_x)) << 16)
#endif

#ifndef BIT8_TO_DMA_BIT32_B2
#define BIT8_TO_DMA_BIT32_B2(_x)   (((bit32)(_x)) << 8)
#endif

#ifndef BIT8_TO_DMA_BIT32_B3
#define BIT8_TO_DMA_BIT32_B3(_x)   ((bit32)(_x))
#endif

/*
 * ** Byte[x] of bit32 to bit8
 * */
#ifndef DMA_BIT32_B0_TO_BIT8
#define DMA_BIT32_B0_TO_BIT8(_x)   ((bit8)((((bit32)(_x)) & 0xFF000000) >> 24))
#endif

#ifndef DMA_BIT32_B1_TO_BIT8
#define DMA_BIT32_B1_TO_BIT8(_x)   ((bit8)((((bit32)(_x)) & 0x00FF0000) >> 16))
#endif

#ifndef DMA_BIT32_B2_TO_BIT8
#define DMA_BIT32_B2_TO_BIT8(_x)   ((bit8)((((bit32)(_x)) & 0x0000FF00) >> 8))
#endif

#ifndef DMA_BIT32_B3_TO_BIT8
#define DMA_BIT32_B3_TO_BIT8(_x)   ((bit8)(((bit32)(_x)) & 0x000000FF))
#endif

/*|                                                                   |
  | end of DMA access macros for BIG ENDIAN                           |
  ---------------------------------------------------------------------
*/
#else

#error No definition of SA_DMA_BIG_ENDIAN or SA_DMA_LITTLE_ENDIAN

#endif  /* DMA endian */
/*
 * End of DMA buffer access macros                                   *
 *                                                                    *
 **********************************************************************
 */

/************************************************************************************
 *                                                                                  *
 *               Constants defined for LL Layer starts                              *
 *                                                                                  *
 ************************************************************************************/

/*********************************************************
 *   sTSDK LL revision and Interface revision, FW version
 *********************************************************/

#define FW_THIS_VERSION_SPC12G 0x03060005

#define FW_THIS_VERSION_SPC6G  0x02092400
#define FW_THIS_VERSION_SPC    0x01110000


#define STSDK_LL_INTERFACE_VERSION                  0x20A
#define STSDK_LL_OLD_INTERFACE_VERSION              0x1                   /* SPC and SPCv before 02030401 */
#define STSDK_LL_VERSION                            FW_THIS_VERSION_SPC6G /**< current sTSDK version */
#define MAX_FW_VERSION_SUPPORTED                    FW_THIS_VERSION_SPC6G /**< FW */
#define MATCHING_V_FW_VERSION                       FW_THIS_VERSION_SPC6G /**< current V  matching FW version */
#define MIN_FW_SPCVE_VERSION_SUPPORTED              0x02000000            /**< 2.00 FW */

#define STSDK_LL_12G_INTERFACE_VERSION              0x302
#define STSDK_LL_12G_VERSION                        FW_THIS_VERSION_SPC12G /**< current sTSDK version */
#define MAX_FW_12G_VERSION_SUPPORTED                FW_THIS_VERSION_SPC12G /**< FW */
#define MATCHING_12G_V_FW_VERSION                   FW_THIS_VERSION_SPC12G /**< current V  matching FW version */
#define MIN_FW_12G_SPCVE_VERSION_SUPPORTED          0x03000000             /**< 3.00 FW */

#define STSDK_LL_SPC_VERSION                        0x01100000          /**< current SPC FW version supported */
#define MATCHING_SPC_FW_VERSION                     FW_THIS_VERSION_SPC /**< current SPC matching FW version */
#define MIN_FW_SPC_VERSION_SUPPORTED                0x01062502          /**< 1.06d FW */

#define STSDK_LL_INTERFACE_VERSION_IGNORE_MASK      0xF00
/*************************************************
 *   constants for API return values
 *************************************************/
#define AGSA_RC_SUCCESS                             0x00     /**< Successful function return value */
#define AGSA_RC_FAILURE                             0x01     /**< Failed function return value */
#define AGSA_RC_BUSY                                0x02     /**< Busy function return value */
/* current only return from saGetControllerInfo() and saGetControllerStatus() */
#define AGSA_RC_HDA_NO_FW_RUNNING                   0x03     /**< HDA mode and no FW running */
#define AGSA_RC_FW_NOT_IN_READY_STATE               0x04     /**< FW not in ready state */
/* current only return from saInitialize() for version checking */
#define AGSA_RC_VERSION_INCOMPATIBLE                0x05     /**< Version mismatch */
#define AGSA_RC_VERSION_UNTESTED                    0x06     /**< Version not tested */
#define AGSA_RC_NOT_SUPPORTED                       0x07     /**< Operation not supported on the current hardware */
#define AGSA_RC_COMPLETE                            0x08

/*************************************************
 *   constants for type field in agsaMem_t
 *************************************************/
#define AGSA_CACHED_MEM                             0x00     /**< CACHED memory type */
#define AGSA_DMA_MEM                                0x01     /**< DMA memory type */
#define AGSA_CACHED_DMA_MEM                         0x02     /**< CACHED DMA memory type */

#ifdef SA_ENABLE_TRACE_FUNCTIONS
#ifdef FAST_IO_TEST
#define AGSA_NUM_MEM_CHUNKS                 (12 + AGSA_MAX_INBOUND_Q + AGSA_MAX_OUTBOUND_Q)       /**< max # of memory chunks supported */
#else
#define AGSA_NUM_MEM_CHUNKS                 (11 + AGSA_MAX_INBOUND_Q + AGSA_MAX_OUTBOUND_Q)       /**< max # of memory chunks supported */
#endif
#else
#ifdef FAST_IO_TEST
#define AGSA_NUM_MEM_CHUNKS                 (11 + AGSA_MAX_INBOUND_Q + AGSA_MAX_OUTBOUND_Q)       /**< max # of memory chunks supported */
#else
#define AGSA_NUM_MEM_CHUNKS                 (10 + AGSA_MAX_INBOUND_Q + AGSA_MAX_OUTBOUND_Q)       /**< max # of memory chunks supported */
#endif
#endif /* END SA_ENABLE_TRACE_FUNCTIONS */


/**********************************
 * default constant for phy count
 **********************************/
#define AGSA_MAX_VALID_PHYS                         16  /* was 8 for SPC */   /**< max # of phys supported by the hardware */

/************************************
 * default constant for Esgl entries
 ************************************/
#define MAX_ESGL_ENTRIES                            10    /**< max # of extended SG list entry */

/*******************************************
 * constant for max inbound/outbound queues
 *******************************************/
#define AGSA_MAX_INBOUND_Q                          64    /**< max # of inbound queue */
#define AGSA_MAX_OUTBOUND_Q                         64    /**< max # of outbound queue */
#define AGSA_MAX_BEST_INBOUND_Q                     16    /* Max inbound Q number with good IO performance */

/****************************
 *   Phy Control constants
 ****************************/
#define AGSA_PHY_LINK_RESET                         0x01
#define AGSA_PHY_HARD_RESET                         0x02
#define AGSA_PHY_GET_ERROR_COUNTS                   0x03 /* SPC only used in original saLocalPhyControl */
#define AGSA_PHY_CLEAR_ERROR_COUNTS                 0x04 /* SPC only */
#define AGSA_PHY_GET_BW_COUNTS                      0x05 /* SPC only */
#define AGSA_PHY_NOTIFY_ENABLE_SPINUP               0x10
#define AGSA_PHY_BROADCAST_ASYNCH_EVENT             0x12
#define AGSA_PHY_COMINIT_OOB                        0x20

#define AGSA_SAS_PHY_ERR_COUNTERS_PAGE      0x01 /* retrieve the SAS PHY error counters */
#define AGSA_SAS_PHY_ERR_COUNTERS_CLR_PAGE  0x02 /* retrieve the SAS PHY error counters After capturing the errors, the hardware error counters are cleared and restarted. */
#define AGSA_SAS_PHY_BW_COUNTERS_PAGE       0x03 /* retrieve the SAS PHY transmit and receive bandwidth counters. */
#define AGSA_SAS_PHY_ANALOG_SETTINGS_PAGE   0x04 /* retrieve the SAS PHY analog settings  */
#define AGSA_SAS_PHY_GENERAL_STATUS_PAGE    0x05 /* retrieve the SAS PHY general status for the PHY specified in the phyID parameter   */
#define AGSA_PHY_SNW3_PAGE                  0x06
#define AGSA_PHY_RATE_CONTROL_PAGE          0x07 /* Used to set several rate control parameters. */
#define AGSA_SAS_PHY_MISC_PAGE              0x08
#define AGSA_SAS_PHY_OPEN_REJECT_RETRY_BACKOFF_THRESHOLD_PAGE     0x08 /* Used to set retry and backoff threshold  parameters. */

/*****************
 * HW Reset
 *****************/
#define AGSA_CHIP_RESET                             0x00     /**< flag to reset hard reset */
#define AGSA_SOFT_RESET                             0x01     /**< flag to reset the controller chip */

/***************************************
 * Discovery Types
 ***************************************/
#define AG_SA_DISCOVERY_TYPE_SAS                    0x00     /**< flag to discover SAS devices */
#define AG_SA_DISCOVERY_TYPE_SATA                   0x01     /**< flag to discover SATA devices */

/***************************************
 * Discovery Options
 ***************************************/
#define AG_SA_DISCOVERY_OPTION_FULL_START           0x00     /**< flag to start full discovery */
#define AG_SA_DISCOVERY_OPTION_INCREMENTAL_START    0x01     /**< flag to start incremental discovery */
#define AG_SA_DISCOVERY_OPTION_ABORT                0x02     /**< flag to abort a discovery */

/****************************************************************
 * SSP/SMP/SATA Request type
 ****************************************************************/
/* bit31-28 - request type
   bit27-16 - reserved
   bit15-10 - SATA ATAP
   bit9-8   - direction
   bit7     - AUTO
   bit6     - reserved
   bit5     - EXT
   bit4     - MSG
   bit3-0   - Initiator, target or task mode (1 to 8)
   */
#define AGSA_REQTYPE_MASK                           0xF0000000  /**< request type mask */
#define AGSA_REQ_TYPE_UNKNOWN                       0x00000000  /**< unknown request type */
#define AGSA_SSP_REQTYPE                            0x80000000
#define AGSA_SMP_REQTYPE                            0x40000000
#define AGSA_SATA_REQTYPE                           0x20000000

#define AGSA_DIR_MASK                               0x00000300
#define AGSA_AUTO_MASK                              0x00000080
#define AGSA_SATA_ATAP_MASK                         0x0000FC00

#define AGSA_DIR_NONE                               0x00000000
#define AGSA_DIR_CONTROLLER_TO_HOST                 0x00000100  /**< used to be called AGSA_DIR_READ */
#define AGSA_DIR_HOST_TO_CONTROLLER                 0x00000200  /**< used to be called AGSA_DIR_WRITE */

/* bit definition - AUTO mode */
#define AGSA_AUTO_GOOD_RESPONSE                     0x00000080

/* request type - not bit difination */
#define AGSA_SSP_INIT                               0x00000001
#define AGSA_SSP_TGT_MODE                           0x00000003
#define AGSA_SSP_TASK_MGNT                          0x00000005
#define AGSA_SSP_TGT_RSP                            0x00000006
#define AGSA_SMP_INIT                               0x00000007
#define AGSA_SMP_TGT                                0x00000008

/* request type for SSP Initiator and extend */
#define AGSA_SSP_INIT_EXT                           (AGSA_SSP_INIT | AGSA_SSP_EXT_BIT)

/* request type for SSP Initiator and indirect */
#define AGSA_SSP_INIT_INDIRECT                      (AGSA_SSP_INIT | AGSA_SSP_INDIRECT_BIT)

/* bit definition */
#define AGSA_MSG                                    0x00000010
#define AGSA_SSP_EXT_BIT                            0x00000020
#define AGSA_SSP_INDIRECT_BIT                       0x00000040
#define AGSA_MSG_BIT                                AGSA_MSG >> 2

/* agsaSSPIniEncryptIOStartCmd_t dirMTlr bits*/
#define AGSA_INDIRECT_CDB_BIT                       0x00000008
#define AGSA_SKIP_MASK_BIT                          0x00000010
#define AGSA_ENCRYPT_BIT                            0x00000020
#define AGSA_DIF_BIT                                0x00000040
#define AGSA_DIF_LA_BIT                             0x00000080
#define AGSA_DIRECTION_BITS                         0x00000300
#define AGSA_SKIP_MASK_OFFSET_BITS                  0x0F000000
#define AGSA_SSP_INFO_LENGTH_BITS                   0xF0000000

/*  agsaSSPTgtIOStartCmd_t INITagAgrDir bits */
#define AGSA_SSP_TGT_BITS_INI_TAG                   0xFFFF0000 /* 16 31  */
#define AGSA_SSP_TGT_BITS_ODS                       0x00008000 /* 15 */
#define AGSA_SSP_TGT_BITS_DEE_DIF                   0x00004000 /* 14 */
#define AGSA_SSP_TGT_BITS_DEE                       0x00002000 /* 13 14 */
#define AGSA_SSP_TGT_BITS_R                         0x00001000 /* 12 */
#define AGSA_SSP_TGT_BITS_DAD                       0x00000600 /* 11 10 */
#define AGSA_SSP_TGT_BITS_DIR                       0x00000300 /* 8 9 */
#define AGSA_SSP_TGT_BITS_DIR_IN                    0x00000100 /* 8 9 */
#define AGSA_SSP_TGT_BITS_DIR_OUT                   0x00000200 /* 8 9 */
#define AGSA_SSP_TGT_BITS_AGR                       0x00000080 /* 7 */
#define AGSA_SSP_TGT_BITS_RDF                       0x00000040 /* 6 */
#define AGSA_SSP_TGT_BITS_RTE                       0x00000030 /* 4 5 */
#define AGSA_SSP_TGT_BITS_AN                        0x00000006 /* 2 3 */


/* agsaSSPIniEncryptIOStartCmd_t DIF_flags bit definitions */
#define AGSA_DIF_UPDATE_BITS                        0xFC000000
#define AGSA_DIF_VERIFY_BITS                        0x03F00000
#define AGSA_DIF_BLOCK_SIZE_BITS                    0x000F0000
#define AGSA_DIF_ENABLE_BLOCK_COUNT_BIT             0x00000040
#define AGSA_DIF_CRC_SEED_BIT                       0x00000020
#define AGSA_DIF_CRC_INVERT_BIT                     0x00000010
#define AGSA_DIF_CRC_VERIFY_BIT                     0x00000008
#define AGSA_DIF_OP_BITS                            0x00000007

#define AGSA_DIF_OP_INSERT                          0x00000000
#define AGSA_DIF_OP_VERIFY_AND_FORWARD              0x00000001
#define AGSA_DIF_OP_VERIFY_AND_DELETE               0x00000002
#define AGSA_DIF_OP_VERIFY_AND_REPLACE              0x00000003
#define AGSA_DIF_OP_RESERVED2                       0x00000004
#define AGSA_DIF_OP_VERIFY_UDT_REPLACE_CRC          0x00000005
#define AGSA_DIF_OP_RESERVED3                       0x00000006
#define AGSA_DIF_OP_REPLACE_UDT_REPLACE_CRC         0x00000007


/* agsaSSPIniEncryptIOStartCmd_t EncryptFlagsLo bit definitions */
#define AGSA_ENCRYPT_DEK_BITS                       0xFFFFFF000
#define AGSA_ENCRYPT_SKIP_DIF_BIT                   0x000000010
#define AGSA_ENCRYPT_KEY_TABLE_BITS                 0x00000000C
#define AGSA_ENCRYPT_KEY_TAG_BIT                    0x000000002

/* Cipher mode to be used for this I/O. */
#define AGSA_ENCRYPT_ECB_Mode                       0
#define AGSA_ENCRYPT_XTS_Mode                       0x6

/* agsaSSPIniEncryptIOStartCmd_t EncryptFlagsHi bit definitions */
#define AGSA_ENCRYPT_KEK_SELECT_BITS                0x0000000E0
#define AGSA_ENCRYPT_SECTOR_SIZE_BITS               0x00000001F

/* defined in the sTSDK spec. */
#define AGSA_SSP_INIT_NONDATA                       (AGSA_SSP_REQTYPE | AGSA_DIR_NONE | AGSA_SSP_INIT)  /**< SSP initiator non data request type */
#define AGSA_SSP_INIT_READ                          (AGSA_SSP_REQTYPE | AGSA_DIR_CONTROLLER_TO_HOST | AGSA_SSP_INIT)  /**< SSP initiator read request type */
#define AGSA_SSP_INIT_WRITE                         (AGSA_SSP_REQTYPE | AGSA_DIR_HOST_TO_CONTROLLER | AGSA_SSP_INIT)  /**< SSP initiator write request type */
#define AGSA_SSP_TGT_READ_DATA                      (AGSA_SSP_REQTYPE | AGSA_DIR_HOST_TO_CONTROLLER | AGSA_SSP_TGT_MODE)  /**< SSP target read data request type */
#define AGSA_SSP_TGT_READ                           (AGSA_SSP_REQTYPE | AGSA_DIR_HOST_TO_CONTROLLER | AGSA_SSP_TGT_MODE)  /**< SSP target read data request type */
#define AGSA_SSP_TGT_READ_GOOD_RESP                 (AGSA_SSP_REQTYPE | AGSA_DIR_HOST_TO_CONTROLLER | AGSA_SSP_TGT_MODE | AGSA_AUTO_GOOD_RESPONSE)  /**< SSP target read data with automatic good response request type */
#define AGSA_SSP_TGT_WRITE_DATA                     (AGSA_SSP_REQTYPE | AGSA_DIR_CONTROLLER_TO_HOST | AGSA_SSP_TGT_MODE)  /**< SSP target write data request type */
#define AGSA_SSP_TGT_WRITE                          (AGSA_SSP_REQTYPE | AGSA_DIR_CONTROLLER_TO_HOST | AGSA_SSP_TGT_MODE)  /**< SSP target write data request type */
#define AGSA_SSP_TGT_WRITE_GOOD_RESP                (AGSA_SSP_REQTYPE | AGSA_DIR_CONTROLLER_TO_HOST | AGSA_SSP_TGT_MODE  | AGSA_AUTO_GOOD_RESPONSE) /**< SSP target write data request type with automatic good response request type*/
#define AGSA_SSP_TASK_MGNT_REQ                      (AGSA_SSP_REQTYPE | AGSA_SSP_TASK_MGNT)  /**< SSP task management request type */
#define AGSA_SSP_TGT_CMD_OR_TASK_RSP                (AGSA_SSP_REQTYPE | AGSA_SSP_TGT_RSP)  /**< SSP command or task management response request type */
#define AGSA_SMP_INIT_REQ                           (AGSA_SMP_REQTYPE | AGSA_SMP_INIT)  /**< SMP initiator request type */
#define AGSA_SMP_TGT_RESPONSE                       (AGSA_SMP_REQTYPE | AGSA_SMP_TGT)  /**< SMP target response request type */
#define AGSA_SSP_INIT_READ_M                        (AGSA_SSP_REQTYPE | AGSA_DIR_CONTROLLER_TO_HOST | AGSA_SSP_INIT | AGSA_MSG)
#define AGSA_SSP_INIT_WRITE_M                       (AGSA_SSP_REQTYPE | AGSA_DIR_HOST_TO_CONTROLLER | AGSA_SSP_INIT | AGSA_MSG)
#define AGSA_SSP_TASK_MGNT_REQ_M                    (AGSA_SSP_REQTYPE | AGSA_SSP_TASK_MGNT                          | AGSA_MSG)
#define AGSA_SSP_INIT_READ_EXT                      (AGSA_SSP_REQTYPE | AGSA_DIR_CONTROLLER_TO_HOST | AGSA_SSP_INIT_EXT)  /**< SSP initiator read request Ext type */
#define AGSA_SSP_INIT_WRITE_EXT                     (AGSA_SSP_REQTYPE | AGSA_DIR_HOST_TO_CONTROLLER | AGSA_SSP_INIT_EXT)  /**< SSP initiator write request Ext type */

#define AGSA_SSP_INIT_READ_INDIRECT                 (AGSA_SSP_REQTYPE | AGSA_DIR_CONTROLLER_TO_HOST | AGSA_SSP_INIT_INDIRECT)  /**< SSP initiator read request indirect type */
#define AGSA_SSP_INIT_WRITE_INDIRECT                (AGSA_SSP_REQTYPE | AGSA_DIR_HOST_TO_CONTROLLER | AGSA_SSP_INIT_INDIRECT)  /**< SSP initiator write request indirect type */

#define AGSA_SSP_INIT_READ_INDIRECT_M               (AGSA_SSP_REQTYPE | AGSA_DIR_CONTROLLER_TO_HOST | AGSA_SSP_INIT_INDIRECT | AGSA_MSG)  /**< SSP initiator read request indirect type */
#define AGSA_SSP_INIT_WRITE_INDIRECT_M              (AGSA_SSP_REQTYPE | AGSA_DIR_HOST_TO_CONTROLLER | AGSA_SSP_INIT_INDIRECT | AGSA_MSG)  /**< SSP initiator write request indirect type */
#define AGSA_SSP_INIT_READ_EXT_M                    (AGSA_SSP_REQTYPE | AGSA_DIR_CONTROLLER_TO_HOST | AGSA_SSP_INIT_EXT | AGSA_MSG)
#define AGSA_SSP_INIT_WRITE_EXT_M                   (AGSA_SSP_REQTYPE | AGSA_DIR_HOST_TO_CONTROLLER | AGSA_SSP_INIT_EXT | AGSA_MSG)

#define AGSA_SMP_IOCTL_REQUEST			    		0xFFFFFFFF

#define AGSA_SATA_ATAP_SRST_ASSERT                  0x00000400
#define AGSA_SATA_ATAP_SRST_DEASSERT                0x00000800
#define AGSA_SATA_ATAP_EXECDEVDIAG                  0x00000C00
#define AGSA_SATA_ATAP_NON_DATA                     0x00001000
#define AGSA_SATA_ATAP_PIO                          0x00001400
#define AGSA_SATA_ATAP_DMA                          0x00001800
#define AGSA_SATA_ATAP_NCQ                          0x00001C00
#define AGSA_SATA_ATAP_PKT_DEVRESET                 0x00002000
#define AGSA_SATA_ATAP_PKT                          0x00002400

#define AGSA_SATA_PROTOCOL_NON_DATA                 (AGSA_SATA_REQTYPE | AGSA_DIR_NONE  | AGSA_SATA_ATAP_NON_DATA)
#define AGSA_SATA_PROTOCOL_PIO_READ                 (AGSA_SATA_REQTYPE | AGSA_DIR_CONTROLLER_TO_HOST | AGSA_SATA_ATAP_PIO)  /**< SATA PIO read request type */
#define AGSA_SATA_PROTOCOL_DMA_READ                 (AGSA_SATA_REQTYPE | AGSA_DIR_CONTROLLER_TO_HOST | AGSA_SATA_ATAP_DMA)  /**< SATA DMA read request type */
#define AGSA_SATA_PROTOCOL_FPDMA_READ               (AGSA_SATA_REQTYPE | AGSA_DIR_CONTROLLER_TO_HOST | AGSA_SATA_ATAP_NCQ)  /**< SATA FDMA read request type */
#define AGSA_SATA_PROTOCOL_PIO_WRITE                (AGSA_SATA_REQTYPE | AGSA_DIR_HOST_TO_CONTROLLER | AGSA_SATA_ATAP_PIO)  /**< SATA PIO read request type */
#define AGSA_SATA_PROTOCOL_DMA_WRITE                (AGSA_SATA_REQTYPE | AGSA_DIR_HOST_TO_CONTROLLER | AGSA_SATA_ATAP_DMA)  /**< SATA DMA read request type */
#define AGSA_SATA_PROTOCOL_FPDMA_WRITE              (AGSA_SATA_REQTYPE | AGSA_DIR_HOST_TO_CONTROLLER | AGSA_SATA_ATAP_NCQ)  /**< SATA FDMA read request type */
#define AGSA_SATA_PROTOCOL_DEV_RESET                (AGSA_SATA_REQTYPE | AGSA_DIR_NONE  | AGSA_SATA_ATAP_PKT_DEVRESET)  /**< SATA device reset request type */
#define AGSA_SATA_PROTOCOL_SRST_ASSERT              (AGSA_SATA_REQTYPE | AGSA_DIR_NONE  | AGSA_SATA_ATAP_SRST_ASSERT)  /**< SATA device reset assert */
#define AGSA_SATA_PROTOCOL_SRST_DEASSERT            (AGSA_SATA_REQTYPE | AGSA_DIR_NONE  | AGSA_SATA_ATAP_SRST_DEASSERT)  /**< SATA device reset deassert */
#define AGSA_SATA_PROTOCOL_D2H_PKT                  (AGSA_SATA_REQTYPE | AGSA_DIR_CONTROLLER_TO_HOST | AGSA_SATA_ATAP_PKT)
#define AGSA_SATA_PROTOCOL_H2D_PKT                  (AGSA_SATA_REQTYPE | AGSA_DIR_HOST_TO_CONTROLLER | AGSA_SATA_ATAP_PKT)
#define AGSA_SATA_PROTOCOL_NON_PKT                  (AGSA_SATA_REQTYPE | AGSA_DIR_NONE | AGSA_SATA_ATAP_PKT)


#define AGSA_SATA_PROTOCOL_NON_DATA_M               (AGSA_SATA_REQTYPE | AGSA_DIR_NONE          | AGSA_SATA_ATAP_NON_DATA | AGSA_MSG)
#define AGSA_SATA_PROTOCOL_PIO_READ_M               (AGSA_SATA_REQTYPE | AGSA_DIR_CONTROLLER_TO_HOST | AGSA_SATA_ATAP_PIO | AGSA_MSG)  /**< SATA PIO read request type */
#define AGSA_SATA_PROTOCOL_DMA_READ_M               (AGSA_SATA_REQTYPE | AGSA_DIR_CONTROLLER_TO_HOST | AGSA_SATA_ATAP_DMA | AGSA_MSG)  /**< SATA DMA read request type */
#define AGSA_SATA_PROTOCOL_FPDMA_READ_M             (AGSA_SATA_REQTYPE | AGSA_DIR_CONTROLLER_TO_HOST | AGSA_SATA_ATAP_NCQ | AGSA_MSG)  /**< SATA FDMA read request type */
#define AGSA_SATA_PROTOCOL_PIO_WRITE_M              (AGSA_SATA_REQTYPE | AGSA_DIR_HOST_TO_CONTROLLER | AGSA_SATA_ATAP_PIO | AGSA_MSG)  /**< SATA PIO read request type */
#define AGSA_SATA_PROTOCOL_DMA_WRITE_M              (AGSA_SATA_REQTYPE | AGSA_DIR_HOST_TO_CONTROLLER | AGSA_SATA_ATAP_DMA | AGSA_MSG)  /**< SATA DMA read request type */
#define AGSA_SATA_PROTOCOL_FPDMA_WRITE_M            (AGSA_SATA_REQTYPE | AGSA_DIR_HOST_TO_CONTROLLER | AGSA_SATA_ATAP_NCQ | AGSA_MSG)  /**< SATA FDMA read request type */
#define AGSA_SATA_PROTOCOL_D2H_PKT_M                (AGSA_SATA_REQTYPE | AGSA_DIR_CONTROLLER_TO_HOST | AGSA_SATA_ATAP_PKT | AGSA_MSG)
#define AGSA_SATA_PROTOCOL_H2D_PKT_M                (AGSA_SATA_REQTYPE | AGSA_DIR_HOST_TO_CONTROLLER | AGSA_SATA_ATAP_PKT | AGSA_MSG)
#define AGSA_SATA_PROTOCOL_NON_PKT_M                (AGSA_SATA_REQTYPE | AGSA_DIR_NONE               | AGSA_SATA_ATAP_PKT | AGSA_MSG)
/* TestBase */
#define AGSA_SATA_PROTOCOL_DEV_RESET_M              (AGSA_SATA_REQTYPE | AGSA_DIR_NONE  | AGSA_SATA_ATAP_PKT_DEVRESET     | AGSA_MSG)  /**< SATA device reset request type */



#define AGSA_INTERRUPT_HANDLE_ALL_CHANNELS          0xFFFFFFFF    /**< flag indicates handles interrupts for all channles */

/****************************************************************************
** INBOUND Queue related macros
****************************************************************************/
#define AGSA_IBQ_PRIORITY_NORMAL                    0x0
#define AGSA_IBQ_PRIORITY_HIGH                      0x1

/****************************************************************************
** Phy properties related macros
****************************************************************************/
/* link rate */
#define AGSA_PHY_MAX_LINK_RATE_MASK                 0x0000000F /* bits 0-3 */
#define AGSA_PHY_MAX_LINK_RATE_1_5G                 0x00000001 /* 0001b */
#define AGSA_PHY_MAX_LINK_RATE_3_0G                 0x00000002 /* 0010b */
#define AGSA_PHY_MAX_LINK_RATE_6_0G                 0x00000004 /* 0100b */
#define AGSA_PHY_MAX_LINK_RATE_12_0G                0x00000008 /* 1000b */

/* SAS/SATA mode */
#define AGSA_PHY_MODE_MASK                          0x00000030 /* bits 4-5 */
#define AGSA_PHY_MODE_SAS                           0x00000010 /* 01b */
#define AGSA_PHY_MODE_SATA                          0x00000020 /* 10b */

/* control spin-up hold */
#define AGSA_PHY_SPIN_UP_HOLD_MASK                  0x00000040 /* bit6 */
#define AGSA_PHY_SPIN_UP_HOLD_ON                    0x00000040 /* 1b */
#define AGSA_PHY_SPIN_UP_HOLD_OFF                   0x00000000 /* 0b */

/****************************************************************************
** Device Info related macros
****************************************************************************/
/* S (SAS/SATA) */
#define AGSA_DEV_INFO_SASSATA_MASK                  0x00000010 /* bit 4 */
#define AGSA_DEV_INFO_SASSATA_SAS                   0x00000010 /* 1b */
#define AGSA_DEV_INFO_SASSATA_SATA                  0x00000000 /* 0b */

/* Rate (link-rate) */
#define AGSA_DEV_INFO_RATE_MASK                     0x0000000F /* bits 0-3 */
#define AGSA_DEV_INFO_RATE_1_5G                     0x00000008 /* 8h */
#define AGSA_DEV_INFO_RATE_3_0G                     0x00000009 /* 9h */
#define AGSA_DEV_INFO_RATE_6_0G                     0x0000000A /* Ah */
#define AGSA_DEV_INFO_RATE_12_0G                    0x0000000B /* Bh */

/* devType */
#define AGSA_DEV_INFO_DEV_TYPE_MASK                 0x000000E0 /* bits 5-7 */
#define AGSA_DEV_INFO_DEV_TYPE_END_DEVICE           0x00000020 /* 001b */
#define AGSA_DEV_INFO_DEV_TYPE_EDGE_EXP_DEVICE      0x00000040 /* 010b */
#define AGSA_DEV_INFO_DEV_TYPE_FANOUT_EXP_DEVICE    0x00000060 /* 011b */

/*****************************************************************************
** SAS TM Function definitions see SAS spec p308 Table 105 (Revision 7)
*****************************************************************************/
#define AGSA_ABORT_TASK                             0x01
#define AGSA_ABORT_TASK_SET                         0x02
#define AGSA_CLEAR_TASK_SET                         0x04
#define AGSA_LOGICAL_UNIT_RESET                     0x08
#define AGSA_IT_NEXUS_RESET                         0x10
#define AGSA_CLEAR_ACA                              0x40
#define AGSA_QUERY_TASK                             0x80
#define AGSA_QUERY_TASK_SET                         0x81
#define AGSA_QUERY_UNIT_ATTENTION                   0x82

/*****************************************************************************
** SAS TM Function Response Code see SAS spec p312 Table 111 (Revision 7)
*****************************************************************************/
#define AGSA_TASK_MANAGEMENT_FUNCTION_COMPLETE      0x0
#define AGSA_INVALID_FRAME                          0x2
#define AGSA_TASK_MANAGEMENT_FUNCTION_NOT_SUPPORTED 0x4
#define AGSA_TASK_MANAGEMENT_FUNCTION_FAILED        0x5
#define AGSA_TASK_MANAGEMENT_FUNCTION_SUCCEEDED     0x8
#define AGSA_INCORRECT_LOGICAL_UNIT_NUMBER          0x9
/* SAS spec 9.2.2.5.3 p356 Table 128 (Revision 9e) */
#define AGSA_OVERLAPPED_TAG_ATTEMPTED               0xA

#define AGSA_SATA_BSY_OVERRIDE                      0x00080000
#define AGSA_SATA_CLOSE_CLEAR_AFFILIATION           0x00400000

#define AGSA_MAX_SMPPAYLOAD_VIA_SFO                 40
#define AGSA_MAX_SSPPAYLOAD_VIA_SFO                 36

/* SATA Initiator Request option field defintion */
#define AGSA_RETURN_D2H_FIS_GOOD_COMPLETION         0x000001
#define AGSA_SATA_ENABLE_ENCRYPTION                 0x000004
#define AGSA_SATA_ENABLE_DIF                        0x000008
#define AGSA_SATA_SKIP_QWORD                        0xFFFF00

/* SAS Initiator Request flag definitions */
/* Bits 0,1 use TLR_MASK */

#define AGSA_SAS_ENABLE_ENCRYPTION                  0x0004
#define AGSA_SAS_ENABLE_DIF                         0x0008

#ifdef SAFLAG_USE_DIF_ENC_IOMB
#define AGSA_SAS_USE_DIF_ENC_OPSTART                0x0010
#endif /* SAFLAG_USE_DIF_ENC_IOMB */

#define AGSA_SAS_ENABLE_SKIP_MASK                   0x0010
#define AGSA_SAS_SKIP_MASK_OFFSET                   0xFFE0

/****************************************************************************
** SMP Phy control Phy Operation field
****************************************************************************/
#define AGSA_PHY_CONTROL_LINK_RESET_OP              0x1
#define AGSA_PHY_CONTROL_HARD_RESET_OP              0x2
#define AGSA_PHY_CONTROL_DISABLE                    0x3
#define AGSA_PHY_CONTROL_CLEAR_ERROR_LOG_OP         0x5
#define AGSA_PHY_CONTROL_CLEAR_AFFILIATION          0x6
#define AGSA_PHY_CONTROL_XMIT_SATA_PS_SIGNAL        0x7

/****************************************************************************
** SAS Diagnostic Operation code
****************************************************************************/
#define AGSA_SAS_DIAG_START                         0x1
#define AGSA_SAS_DIAG_END                           0x0

/****************************************************************************
** Port Control constants
****************************************************************************/
#define AGSA_PORT_SET_SMP_PHY_WIDTH                 0x1
#define AGSA_PORT_SET_PORT_RECOVERY_TIME            0x2
#define AGSA_PORT_IO_ABORT                          0x3
#define AGSA_PORT_SET_PORT_RESET_TIME               0x4
#define AGSA_PORT_HARD_RESET                        0x5
#define AGSA_PORT_CLEAN_UP                          0x6
#define AGSA_STOP_PORT_RECOVERY_TIMER               0x7

/* Device State */
#define SA_DS_OPERATIONAL                           0x1
#define SA_DS_PORT_IN_RESET                         0x2
#define SA_DS_IN_RECOVERY                           0x3
#define SA_DS_IN_ERROR                              0x4
#define SA_DS_NON_OPERATIONAL                       0x7

/************************************************************************************
 *                                                                                  *
 *               Constants defined for LL Layer ends                                *
 *                                                                                  *
 ************************************************************************************/

/************************************************************************************
 *                                                                                  *
 *               Constants defined for OS Layer starts                              *
 *                                                                                  *
 ************************************************************************************/
/*****************************************
 *  ossaXXX return values
 ******************************************/
/* common for all ossaXXX CB */
#define OSSA_SUCCESS                                0x00   /**< flag indicates successful callback status */
#define OSSA_FAILURE                                0x01   /**< flag indicates failed callback status */

/* ossaHwCB() */
#define OSSA_RESET_PENDING                          0x03   /**< flag indicates reset pending callback status */
#define OSSA_CHIP_FAILED                            0x04   /**< flag indicates chip failed callback status */
#define OSSA_FREEZE_FAILED                          0x05   /**< flag indicates freeze failed callback status */

/* ossaLocalPhyControl() */
#define OSSA_PHY_CONTROL_FAILURE                    0x03   /**< flag indicates phy Control operation failure */

/* ossaDeviceRegisterCB() */
#define OSSA_FAILURE_OUT_OF_RESOURCE                0x01   /**< flag indicates failed callback status */
#define OSSA_FAILURE_DEVICE_ALREADY_REGISTERED      0x02   /**< flag indicates failed callback status */
#define OSSA_FAILURE_INVALID_PHY_ID                 0x03   /**< flag indicates failed callback status */
#define OSSA_FAILURE_PHY_ID_ALREADY_REGISTERED      0x04   /**< flag indicates failed callback status */
#define OSSA_FAILURE_PORT_ID_OUT_OF_RANGE           0x05   /**< flag indicates failed callback status */
#define OSSA_FAILURE_PORT_NOT_VALID_STATE           0x06   /**< flag indicates failed callback status */
#define OSSA_FAILURE_DEVICE_TYPE_NOT_VALID          0x07   /**< flag indicates failed callback status */
#define OSSA_ERR_DEVICE_HANDLE_UNAVAILABLE          0x1020
#define OSSA_ERR_DEVICE_ALREADY_REGISTERED          0x1021
#define OSSA_ERR_DEVICE_TYPE_NOT_VALID              0x1022

#define OSSA_MPI_ERR_DEVICE_ACCEPT_PENDING          0x1027 /**/

#define OSSA_ERR_PORT_INVALID                       0x1041
#define OSSA_ERR_PORT_STATE_NOT_VALID               0x1042

#define OSSA_ERR_PORT_SMP_PHY_WIDTH_EXCEED          0x1045

#define OSSA_ERR_PHY_ID_INVALID                     0x1061
#define OSSA_ERR_PHY_ID_ALREADY_REGISTERED          0x1062



/* ossaDeregisterDeviceCB() */
#define OSSA_INVALID_HANDLE                         0x02   /**< flag indicates failed callback status */
#define OSSA_ERR_DEVICE_HANDLE_INVALID              0x1023 /* MPI_ERR_DEVICE_HANDLE_INVALID The device handle associated with DEVICE_ID does not exist. */
#define OSSA_ERR_DEVICE_BUSY                        0x1024 /* MPI_ERR_DEVICE_BUSY Device has outstanding I/Os. */


#define OSSA_RC_ACCEPT                              0x00   /**< flag indicates the result of the callback function */
#define OSSA_RC_REJECT                              0x01   /**< flag indicates the result of the callback function */

/* ossaSetDeviceStateCB() */
#define OSSA_INVALID_STATE                          0x0001
#define OSSA_ERR_DEVICE_NEW_STATE_INVALID           0x1025
#define OSSA_ERR_DEVICE_STATE_CHANGE_NOT_ALLOWED    0x1026
#define OSSA_ERR_DEVICE_STATE_INVALID               0x0049

/* status of ossaSASDiagExecuteCB() */
#define OSSA_DIAG_SUCCESS                           0x00 /* Successful SAS diagnostic command. */
#define OSSA_DIAG_INVALID_COMMAND                   0x01 /* Invalid SAS diagnostic command. */
#define OSSA_REGISTER_ACCESS_TIMEOUT                0x02 /* Register access has been timed-out. This is applicable only to the SPCv controller. */
#define OSSA_DIAG_FAIL                              0x02 /* SAS diagnostic command failed. This is applicable only to the SPC controller. */
#define OSSA_DIAG_NOT_IN_DIAGNOSTIC_MODE            0x03 /* Attempted to execute SAS diagnostic command but PHY is not in diagnostic mode */
#define OSSA_DIAG_INVALID_PHY                       0x04 /* Attempted to execute SAS diagnostic command on an invalid/out-of-range PHY. */
#define OSSA_MEMORY_ALLOC_FAILURE                   0x05 /* Memory allocation failed in diagnostic. This is applicable only to the SPCv controller. */


/* status of ossaSASDiagStartEndCB() */
#define OSSA_DIAG_SE_SUCCESS                        0x00
#define OSSA_DIAG_SE_INVALID_PHY_ID                 0x01
#define OSSA_DIAG_PHY_NOT_DISABLED                  0x02
#define OSSA_DIAG_OTHER_FAILURE                     0x03 /* SPC */
#define OSSA_DIAG_OPCODE_INVALID                    0x03

/* status of ossaPortControlCB() */
#define OSSA_PORT_CONTROL_FAILURE                   0x03

#define OSSA_MPI_ERR_PORT_IO_RESOURCE_UNAVAILABLE   0x1004
#define OSSA_MPI_ERR_PORT_INVALID                   0x1041 /**/
#define OSSA_MPI_ERR_PORT_OP_NOT_IN_USE             0x1043 /**/
#define OSSA_MPI_ERR_PORT_OP_NOT_SUPPORTED          0x1044 /**/
#define OSSA_MPI_ERR_PORT_SMP_WIDTH_EXCEEDED        0x1045 /**/
#define OSSA_MPI_ERR_PORT_NOT_IN_CORRECT_STATE      0x1047 /**/

/*regDumpNum of agsaRegDumpInfo_t */
#define GET_GSM_SM_INFO                             0x02
#define GET_IOST_RB_INFO                            0x03

/************************************************************************************
 *               HW Events
 ************************************************************************************/
#define OSSA_HW_EVENT_RESET_START                   0x01   /**< flag indicates reset started event */
#define OSSA_HW_EVENT_RESET_COMPLETE                0x02   /**< flag indicates chip reset completed event */
#define OSSA_HW_EVENT_PHY_STOP_STATUS               0x03   /**< flag indicates phy stop event status */
#define OSSA_HW_EVENT_SAS_PHY_UP                    0x04   /**< flag indicates SAS link up event */
#define OSSA_HW_EVENT_SATA_PHY_UP                   0x05   /**< flag indicates SATA link up event */
#define OSSA_HW_EVENT_SATA_SPINUP_HOLD              0x06   /**< flag indicates SATA spinup hold event */
#define OSSA_HW_EVENT_PHY_DOWN                      0x07   /**< flag indicates link down event */

#define OSSA_HW_EVENT_BROADCAST_CHANGE              0x09   /**< flag indicates broadcast change event */
/* not used spcv 0x0A*/
#define OSSA_HW_EVENT_PHY_ERROR                     0x0A   /**< flag indicates link error event */
#define OSSA_HW_EVENT_BROADCAST_SES                 0x0B   /**< flag indicates broadcast change (SES) event */
#define OSSA_HW_EVENT_PHY_ERR_INBOUND_CRC           0x0C
#define OSSA_HW_EVENT_HARD_RESET_RECEIVED           0x0D   /**< flag indicates hardware reset received event */
/* not used spcv 0x0E*/
#define OSSA_HW_EVENT_MALFUNCTION                   0x0E   /**< flag indicates unrecoverable Error */
#define OSSA_HW_EVENT_ID_FRAME_TIMEOUT              0x0F   /**< flag indicates ID Frame Timeout event */
#define OSSA_HW_EVENT_BROADCAST_EXP                 0x10   /**< flag indicates broadcast (EXPANDER) event */
/* not used spcv 0x11*/
#define OSSA_HW_EVENT_PHY_START_STATUS              0x11   /**< flag indicates phy start event status */
#define OSSA_HW_EVENT_PHY_ERR_INVALID_DWORD         0x12   /**< flag indicates Link error invalid DWORD */
#define OSSA_HW_EVENT_PHY_ERR_DISPARITY_ERROR       0x13   /**< flag indicates Phy error disparity */
#define OSSA_HW_EVENT_PHY_ERR_CODE_VIOLATION        0x14   /**< flag indicates Phy error code violation */
#define OSSA_HW_EVENT_PHY_ERR_LOSS_OF_DWORD_SYNCH   0x15   /**< flag indicates Link error loss of DWORD synch */
#define OSSA_HW_EVENT_PHY_ERR_PHY_RESET_FAILED      0x16   /**< flag indicates Link error phy reset failed */
#define OSSA_HW_EVENT_PORT_RECOVERY_TIMER_TMO       0x17   /**< flag indicates Port Recovery timeout */
#define OSSA_HW_EVENT_PORT_RECOVER                  0x18   /**< flag indicates Port Recovery */
#define OSSA_HW_EVENT_PORT_RESET_TIMER_TMO          0x19   /**< flag indicates Port Reset Timer out */
#define OSSA_HW_EVENT_PORT_RESET_COMPLETE           0x20   /**< flag indicates Port Reset Complete */
#define OSSA_HW_EVENT_BROADCAST_ASYNCH_EVENT        0x21   /**< flag indicates Broadcast Asynch Event */
#define OSSA_HW_EVENT_IT_NEXUS_LOSS                 0x22   /**< Custom: H/W event for IT Nexus Loss */

#define OSSA_HW_EVENT_OPEN_RETRY_BACKOFF_THR_ADJUSTED 0x25

#define OSSA_HW_EVENT_ENCRYPTION                    0x83   /**< TSDK internal flag indicating that an encryption event occurred */
#define OSSA_HW_EVENT_MODE                          0x84   /**< TSDK internal flag indicating that a controller mode page operation completed */
#define OSSA_HW_EVENT_SECURITY_MODE                 0x85   /**< TSDK internal flag indicating that saEncryptSetMode() completed */


/* port state */
#define OSSA_PORT_NOT_ESTABLISHED                   0x00   /**< flag indicates port is not established */
#define OSSA_PORT_VALID                             0x01   /**< flag indicates port valid */
#define OSSA_PORT_LOSTCOMM                          0x02   /**< flag indicates port lost communication */
#define OSSA_PORT_IN_RESET                          0x04   /**< flag indicates port in reset state */
#define OSSA_PORT_3RDPARTY_RESET                    0x07   /**< flag indicates port in 3rd party reset state */
#define OSSA_PORT_INVALID                           0x08   /**< flag indicates port invalid */

/* status for agsaHWEventMode_t */
#define OSSA_CTL_SUCCESS                            0x0000
#define OSSA_CTL_INVALID_CONFIG_PAGE                0x1001
#define OSSA_CTL_INVALID_PARAM_IN_CONFIG_PAGE       0x1002
#define OSSA_CTL_INVALID_ENCRYPTION_SECURITY_MODE   0x1003
#define OSSA_CTL_RESOURCE_NOT_AVAILABLE             0x1004
#define OSSA_CTL_CONTROLLER_NOT_IDLE                0x1005
// #define OSSA_CTL_NVM_MEMORY_ACCESS_ERR              0x100B
#define OSSA_CTL_OPERATOR_AUTHENTICATION_FAILURE    0x100XX



/************************************************************************************
 *               General Events value
 ************************************************************************************/
#define OSSA_INBOUND_V_BIT_NOT_SET                  0x01
#define OSSA_INBOUND_OPC_NOT_SUPPORTED              0x02
#define OSSA_INBOUND_IOMB_INVALID_OBID              0x03

/************************************************************************************
 *               FW Flash Update status values
 ************************************************************************************/
#define OSSA_FLASH_UPDATE_COMPLETE_PENDING_REBOOT   0x00   /**< flag indicates fw flash update completed */
#define OSSA_FLASH_UPDATE_IN_PROGRESS               0x01   /**< flag indicates fw flash update in progress */
#define OSSA_FLASH_UPDATE_HDR_ERR                   0x02   /**< flag indicates fw flash header error */
#define OSSA_FLASH_UPDATE_OFFSET_ERR                0x03   /**< flag indicates fw flash offset error */
#define OSSA_FLASH_UPDATE_CRC_ERR                   0x04   /**< flag indicates fw flash CRC error */
#define OSSA_FLASH_UPDATE_LENGTH_ERR                0x05   /**< flag indicates fw flash length error */
#define OSSA_FLASH_UPDATE_HW_ERR                    0x06   /**< flag indicates fw flash HW error */
#define OSSA_FLASH_UPDATE_HMAC_ERR                  0x0E   /**< flag indicates fw flash Firmware image HMAC authentication failure.*/

#define OSSA_FLASH_UPDATE_DNLD_NOT_SUPPORTED        0x10   /**< flag indicates fw flash down load not supported */
#define OSSA_FLASH_UPDATE_DISABLED                  0x11   /**< flag indicates fw flash Update disabled */
#define OSSA_FLASH_FWDNLD_DEVICE_UNSUPPORT          0x12   /**< flag indicates fw flash Update disabled */

/************************************************************************************
*               Discovery status values
************************************************************************************/
#define OSSA_DISCOVER_STARTED                       0x00   /**< flag indicates discover started */
#define OSSA_DISCOVER_FOUND_DEVICE                  0x01   /**< flag indicates discovery found a new device */
#define OSSA_DISCOVER_REMOVED_DEVICE                0x02   /**< flag indicates discovery found a device removed */
#define OSSA_DISCOVER_COMPLETE                      0x03   /**< flag indicates discover completed */
#define OSSA_DISCOVER_ABORT                         0x04   /**< flag indicates discover error12 */
#define OSSA_DISCOVER_ABORT_ERROR_1                 0x05   /**< flag indicates discover error1 */
#define OSSA_DISCOVER_ABORT_ERROR_2                 0x06   /**< flag indicates discover error2 */
#define OSSA_DISCOVER_ABORT_ERROR_3                 0x07   /**< flag indicates discover error3 */
#define OSSA_DISCOVER_ABORT_ERROR_4                 0x08   /**< flag indicates discover error4 */
#define OSSA_DISCOVER_ABORT_ERROR_5                 0x09   /**< flag indicates discover error5 */
#define OSSA_DISCOVER_ABORT_ERROR_6                 0x0A   /**< flag indicates discover error6 */
#define OSSA_DISCOVER_ABORT_ERROR_7                 0x0B   /**< flag indicates discover error7 */
#define OSSA_DISCOVER_ABORT_ERROR_8                 0x0C   /**< flag indicates discover error8 */
#define OSSA_DISCOVER_ABORT_ERROR_9                 0x0D   /**< flag indicates discover error9 */

/***********************************************************************************
 *                        Log Debug Levels
 ***********************************************************************************/
#define OSSA_DEBUG_LEVEL_0                          0x00   /**< debug level 0 */
#define OSSA_DEBUG_LEVEL_1                          0x01   /**< debug level 1 */
#define OSSA_DEBUG_LEVEL_2                          0x02   /**< debug level 2 */
#define OSSA_DEBUG_LEVEL_3                          0x03   /**< debug level 3 */
#define OSSA_DEBUG_LEVEL_4                          0x04   /**< debug level 4 */

#define OSSA_DEBUG_PRINT_INVALID_NUMBER             0xFFFFFFFF   /**< the number won't be printed by OS layer */

#define OSSA_FRAME_TYPE_SSP_CMD                     0x06   /**< flag indicates received frame is SSP command */
#define OSSA_FRAME_TYPE_SSP_TASK                    0x16   /**< flag indicates received frame is SSP task management */

/* Event Source Type of saRegisterEventCallback() */
#define OSSA_EVENT_SOURCE_DEVICE_HANDLE_ADDED       0x00
#define OSSA_EVENT_SOURCE_DEVICE_HANDLE_REMOVED     0x01

/* Status of Get Device Info CB */
#define OSSA_DEV_INFO_INVALID_HANDLE                0x01
#define OSSA_DEV_INFO_NO_EXTENDED_INFO              0x02
#define OSSA_DEV_INFO_SAS_EXTENDED_INFO             0x03
#define OSSA_DEV_INFO_SATA_EXTENDED_INFO            0x04

/* Diagnostic Command Type */
#define AGSA_CMD_TYPE_DIAG_OPRN_PERFORM             0x00
#define AGSA_CMD_TYPE_DIAG_OPRN_STOP                0x01
#define AGSA_CMD_TYPE_DIAG_THRESHOLD_SPECIFY        0x02
#define AGSA_CMD_TYPE_DIAG_RECEIVE_ENABLE           0x03
#define AGSA_CMD_TYPE_DIAG_REPORT_GET               0x04
#define AGSA_CMD_TYPE_DIAG_ERR_CNT_RESET            0x05

/* Command Description for CMD_TYPE DIAG_OPRN_PERFORM, DIAG_OPRN_STOP, THRESHOLD_SPECIFY */
#define AGSA_CMD_DESC_PRBS                          0x00
#define AGSA_CMD_DESC_CJTPAT                        0x01
#define AGSA_CMD_DESC_USR_PATTERNS                  0x02
#define AGSA_CMD_DESC_PRBS_ERR_INSERT               0x08
#define AGSA_CMD_DESC_PRBS_INVERT                   0x09
#define AGSA_CMD_DESC_CJTPAT_INVERT                 0x0A
#define AGSA_CMD_DESC_CODE_VIOL_INSERT              0x0B
#define AGSA_CMD_DESC_DISP_ERR_INSERT               0x0C
#define AGSA_CMD_DESC_SSPA_PERF_EVENT_1             0x0E
#define AGSA_CMD_DESC_LINE_SIDE_ANA_LPBK            0x10
#define AGSA_CMD_DESC_LINE_SIDE_DIG_LPBK            0x11
#define AGSA_CMD_DESC_SYS_SIDE_ANA_LPBK             0x12

/* Command Description for CMD_TYPE DIAG_REPORT_GET and ERR_CNT_RESET */
#define AGSA_CMD_DESC_PRBS_ERR_CNT                  0x00
#define AGSA_CMD_DESC_CODE_VIOL_ERR_CNT             0x01
#define AGSA_CMD_DESC_DISP_ERR_CNT                  0x02
#define AGSA_CMD_DESC_LOST_DWD_SYNC_CNT             0x05
#define AGSA_CMD_DESC_INVALID_DWD_CNT               0x06
#define AGSA_CMD_DESC_CODE_VIOL_ERR_CNT_THHD        0x09
#define AGSA_CMD_DESC_DISP_ERR_CNT_THHD             0x0A
#define AGSA_CMD_DESC_SSPA_PERF_CNT                 0x0B
#define AGSA_CMD_DESC_PHY_RST_CNT                   0x0C
#define AGSA_CMD_DESC_SSPA_PERF_1_THRESHOLD         0x0E

#define AGSA_CMD_DESC_CODE_VIOL_ERR_THHD            0x19
#define AGSA_CMD_DESC_DISP_ERR_THHD                 0x1A
#define AGSA_CMD_DESC_RX_LINK_BANDWIDTH             0x1B
#define AGSA_CMD_DESC_TX_LINK_BANDWIDTH             0x1C
#define AGSA_CMD_DESC_ALL                           0x1F

/* NVMDevice type */
#define AGSA_NVMD_TWI_DEVICES                       0x00
#define AGSA_NVMD_CONFIG_SEEPROM                    0x01
#define AGSA_NVMD_VPD_FLASH                         0x04
#define AGSA_NVMD_AAP1_REG_FLASH                    0x05
#define AGSA_NVMD_IOP_REG_FLASH                     0x06
#define AGSA_NVMD_EXPANSION_ROM                     0x07
#define AGSA_NVMD_REG_FLASH                         0x05


/* GET/SET NVMD Data Response errors */
#define OSSA_NVMD_SUCCESS                           0x0000
#define OSSA_NVMD_MODE_ERROR                        0x0001
#define OSSA_NVMD_LENGTH_ERROR                      0x0002
#define OSSA_NVMD_TWI_ADDRESS_SIZE_ERROR            0x0005
#define OSSA_NVMD_TWI_NACK_ERROR                    0x2001
#define OSSA_NVMD_TWI_LOST_ARB_ERROR                0x2002
#define OSSA_NVMD_TWI_TIMEOUT_ERROR                 0x2021
#define OSSA_NVMD_TWI_BUS_NACK_ERROR                0x2081
#define OSSA_NVMD_TWI_ARB_FAILED_ERROR              0x2082
#define OSSA_NVMD_TWI_BUS_TIMEOUT_ERROR             0x20FF
#define OSSA_NVMD_FLASH_PARTITION_NUM_ERROR         0x9001
#define OSSA_NVMD_FLASH_LENGTH_TOOBIG_ERROR         0x9002
#define OSSA_NVMD_FLASH_PROGRAM_ERROR               0x9003
#define OSSA_NVMD_FLASH_DEVICEID_ERROR              0x9004
#define OSSA_NVMD_FLASH_VENDORID_ERROR              0x9005
#define OSSA_NVMD_FLASH_ERASE_TIMEOUT_ERROR         0x9006
#define OSSA_NVMD_FLASH_ERASE_ERROR                 0x9007
#define OSSA_NVMD_FLASH_BUSY_ERROR                  0x9008
#define OSSA_NVMD_FLASH_NOT_SUPPORT_DEVICE_ERROR    0x9009
#define OSSA_NVMD_FLASH_CFI_INF_ERROR               0x900A
#define OSSA_NVMD_FLASH_MORE_ERASE_BLOCK_ERROR      0x900B
#define OSSA_NVMD_FLASH_READ_ONLY_ERROR             0x900C
#define OSSA_NVMD_FLASH_MAP_TYPE_ERROR              0x900D
#define OSSA_NVMD_FLASH_MAP_DISABLE_ERROR           0x900E

/************************************************************
* ossaHwCB Encryption encryptOperation of agsaHWEventEncrypt_t
************************************************************/
#define OSSA_HW_ENCRYPT_KEK_UPDATE                      0x0000
#define OSSA_HW_ENCRYPT_KEK_UPDATE_AND_STORE            0x0001
#define OSSA_HW_ENCRYPT_KEK_INVALIDTE                   0x0002
#define OSSA_HW_ENCRYPT_DEK_UPDATE                      0x0003
#define OSSA_HW_ENCRYPT_DEK_INVALIDTE                   0x0004
#define OSSA_HW_ENCRYPT_OPERATOR_MANAGEMENT             0x0005
#define OSSA_HW_ENCRYPT_TEST_EXECUTE                    0x0006
#define OSSA_HW_ENCRYPT_SET_OPERATOR                    0x0007
#define OSSA_HW_ENCRYPT_GET_OPERATOR                    0x0008


/************************************************************
* ossaHwCB Encryption status of agsaHWEventEncrypt_t
************************************************************/
/* KEK and DEK managment status from PM */
#define OSSA_INVALID_ENCRYPTION_SECURITY_MODE           0x1003
#define OSSA_KEK_MGMT_SUBOP_NOT_SUPPORTED_              0x2000     /*not in PM 101222*/
#define OSSA_DEK_MGMT_SUBOP_NOT_SUPPORTED               0x2000
#define OSSA_MPI_ENC_ERR_ILLEGAL_DEK_PARAM              0x2001
#define OSSA_MPI_ERR_DEK_MANAGEMENT_DEK_UNWRAP_FAIL     0x2002
#define OSSA_MPI_ENC_ERR_ILLEGAL_KEK_PARAM              0x2021
#define OSSA_MPI_ERR_KEK_MANAGEMENT_KEK_UNWRAP_FAIL     0x2022
#define OSSA_MPI_ERR_KEK_MANAGEMENT_NVRAM_OPERATION_FAIL 0x2023

/*encrypt operator management response status */
#define OSSA_OPR_MGMT_OP_NOT_SUPPORTED                  0x2060
#define OSSA_MPI_ENC_ERR_OPR_PARAM_ILLEGAL              0x2061
#define OSSA_MPI_ENC_ERR_OPR_ID_NOT_FOUND               0x2062
#define OSSA_MPI_ENC_ERR_OPR_ROLE_NOT_MATCH             0x2063
#define OSSA_MPI_ENC_ERR_OPR_MAX_NUM_EXCEEDED           0x2064

/*encrypt saSetOperator() response status */
#define OSSA_MPI_ENC_ERR_CONTROLLER_NOT_IDLE            0x1005
#define OSSA_MPI_ENC_NVM_MEM_ACCESS_ERR                 0x100B

/* agsaEncryptSMX | agsaEncryptCipherMode == cipherMode for saEncryptSetMode()*/
/* Make sure all definitions are unique bits */
#define agsaEncryptSMF                            0x00000000
#define agsaEncryptSMA                            0x00000100
#define agsaEncryptSMB                            0x00000200
#define agsaEncryptReturnSMF                    (1 << 12)
#define agsaEncryptAuthorize                    (1 << 13)

/*
Bits 16-23: Allowable Cipher Mode(ACM)
Bit 16: Enable AES ECB. If set to 1, AES ECB is enable. If set to 0, AES ECB is disabled.
Bit 22: Enable AES XTS. If set to 1, AES XTS is enable. If set to 0, AES XTS is disabled.
*/
#define agsaEncryptAcmMask                        0x00ff0000
#define agsaEncryptEnableAES_ECB                (1 << 16)
#define agsaEncryptEnableAES_XTS                (1 << 22)



#define agsaEncryptCipherModeECB                  0x00000001
#define agsaEncryptCipherModeXTS                  0x00000002



#define agsaEncryptStatusNoNVRAM                  0x00000001
#define agsaEncryptStatusNVRAMErr                 0x00000002

/*

Bin    Hex  Sector      Total
00000 :0x0  512B        512
11000 :0x1  520B        520
00010 :0x2  4K          4096
00011 :0x3  4K+64B      4160
00100 :0x4  4K+128B     4224

11000 :0x18 512+8B      520
11001 :0x19 520+8B      528
11010 :0x1A 4K+8B       4104
11011 :0x1B 4K+64B+8B   4168
11100 :0x1C 4K+128B+8B  4232

*/

#define agsaEncryptSectorSize512                        0
/*  define agsaEncryptSectorSize520                     1 Not supported */
#define agsaEncryptSectorSize4096                       2
#define agsaEncryptSectorSize4160                       3
#define agsaEncryptSectorSize4224                       4

#define agsaEncryptDIFSectorSize520                     (agsaEncryptSectorSize512  | 0x18)
#define agsaEncryptDIFSectorSize528                     ( 0x19)
#define agsaEncryptDIFSectorSize4104                    (agsaEncryptSectorSize4096 | 0x18)
#define agsaEncryptDIFSectorSize4168                    (agsaEncryptSectorSize4160 | 0x18)
#define agsaEncryptDIFSectorSize4232                    (agsaEncryptSectorSize4224 | 0x18)


#define AGSA_ENCRYPT_STORE_NVRAM                         1

/************************************************************
* ossaHwCB Mode page event definitions
************************************************************/
#define agsaModePageGet                                    1
#define agsaModePageSet                                    2

/************************************************************
* saSgpio() SGPIO Function and Register type
************************************************************/
#define AGSA_READ_SGPIO_REGISTER                         0x02
#define AGSA_WRITE_SGPIO_REGISTER                        0x82

#define AGSA_SGPIO_CONFIG_REG                            0x0
#define AGSA_SGPIO_DRIVE_BY_DRIVE_RECEIVE_REG            0x1
#define AGSA_SGPIO_GENERAL_PURPOSE_RECEIVE_REG           0x2
#define AGSA_SGPIO_DRIVE_BY_DRIVE_TRANSMIT_REG           0x3
#define AGSA_SGPIO_GENERAL_PURPOSE_TRANSMIT_REG          0x4

/************************************************************
* ossaSGpioCB() Function result
************************************************************/
#define OSSA_SGPIO_COMMAND_SUCCESS                          0x00
#define OSSA_SGPIO_CMD_ERROR_WRONG_FRAME_TYPE               0x01
#define OSSA_SGPIO_CMD_ERROR_WRONG_REG_TYPE                 0x02
#define OSSA_SGPIO_CMD_ERROR_WRONG_REG_INDEX                0x03
#define OSSA_SGPIO_CMD_ERROR_WRONG_REG_COUNT                0x04
#define OSSA_SGPIO_CMD_ERROR_WRONG_FRAME_REG_TYPE           0x05
#define OSSA_SGPIO_CMD_ERROR_WRONG_FUNCTION                 0x06
#define OSSA_SGPIO_CMD_ERROR_WRONG_FRAME_TYPE_REG_INDEX     0x19
#define OSSA_SGPIO_CMD_ERROR_WRONG_FRAME_TYPE_REG_CNT       0x81
#define OSSA_SGPIO_CMD_ERROR_WRONG_REG_TYPE_REG_INDEX       0x1A
#define OSSA_SGPIO_CMD_ERROR_WRONG_REG_TYPE_REG_COUNT       0x82
#define OSSA_SGPIO_CMD_ERROR_WRONG_REG_INDEX_REG_COUNT      0x83
#define OSSA_SGPIO_CMD_ERROR_WRONG_FRAME_REG_TYPE_REG_INDEX 0x1D
#define OSSA_SGPIO_CMD_ERROR_WRONG_ALL_HEADER_PARAMS        0x9D

#define OSSA_SGPIO_MAX_READ_DATA_COUNT                      0x0D
#define OSSA_SGPIO_MAX_WRITE_DATA_COUNT                     0x0C

/************************************************************
* ossaGetDFEDataCB() status
************************************************************/
#define OSSA_DFE_MPI_IO_SUCCESS                         0x0000
#define OSSA_DFE_DATA_OVERFLOW                          0x0002
#define OSSA_DFE_MPI_ERR_RESOURCE_UNAVAILABLE           0x1004
#define OSSA_DFE_CHANNEL_DOWN                           0x100E
#define OSSA_DFE_MEASUREMENT_IN_PROGRESS                0x100F
#define OSSA_DFE_CHANNEL_INVALID                        0x1010
#define OSSA_DFE_DMA_FAILURE                            0x1011

/************************************************************************************
 *                                                                                  *
 *               Constants defined for OS Layer ends                                *
 *                                                                                  *
 ************************************************************************************/

/************************************************************************************
 *                                                                                  *
 *               Data Structures Defined for LL API start                           *
 *                                                                                  *
 ************************************************************************************/
/** \brief data structure stores OS specific and LL specific context
 *
 * The agsaContext_t data structure contains two generic pointers,
 * also known as handles, which are used to store OS Layer-specific and
 * LL Layer-specific contexts. Only the handle specific to a layer can
 * be modified by the layer. The other layer's handle must be returned
 * unmodified when communicating between the layers.

 * A layer's handle is typically typecast to an instance of a layer-specific
 * data structure. The layer can use its handle to point to any data type
 * that is to be associated with a function call. A handle provides a way
 * to uniquely identify responses when multiple calls to the same function
 * are necessary.
 *
 */
typedef struct agsaContext_s
{
  void  *osData; /**< Pointer-sized value used internally by the OS Layer */
  void  *sdkData; /**< Pointer-sized value used internally by the LL Layer */
} agsaContext_t;

/** \brief hold points to global data strutures used by the LL and OS Layers
 *
 * The agsaRoot_t data structure is used to hold pointer-sized values for
 * internal use by the LL and OS Layers. It is intended that the
 * sdkData element of the agsaRoot_t data structure be used to
 * identify an instance of the hardware context. The sdkData
 * element is set by the LL Layer in the saHwInitialize()
 * function and returned to the OS Layer in the agsaRoot_t data
 * structure
 */
typedef agsaContext_t agsaRoot_t;

/** \brief holds the pointers to the device data structure used by the LL and OS Layers
 *
 * The agsaDevHandle_t data structure is the device instance handle.
 * It holds pointer-sized values used internally by each of the LL and
 * OS Layers. It is intended that the agsaDevHandle_t data
 * structure be used to identify a specific device instance. A
 * device instance is uniquely identified by its device handle.
 */
typedef agsaContext_t agsaDevHandle_t;

/** \brief holds the pointers to the port data structure used by the LL and
 *  OS Layers
 *
 * The agsaPortContext_t data structure is used to describe an instance of
 * SAS port or SATA port. It holds pointer-sized values used
 * internally by each of the LL and OS Layers.
 *
 * When connected to other SAS end-devices or expanders, each instance of
 * agsaPortContext_t represents a SAS local narrow-port or
 * wide-port.
 *
 * When connected to SATA device, each instance of agsaPortContext_t
 * represents a local SATA port.
 *
 */
typedef agsaContext_t agsaPortContext_t;

/** \brief data structure pointer to IO request structure
 *
 * It is intended that the agsaIORequest_t structure be used to
 * uniquely identify each I/O Request for either target or
 * initiator. The OS Layer is responsible for allocating and
 * managing agsaIORequest_t structures. The LL Layer uses each
 * structure only between calls to: saSSPStart() and
 * ossaSSPCompleted(), saSATAStart() and ossaSATACompleted(),
 * saSMPStart() and ossaSMPCompleted()
 *
 */
typedef agsaContext_t agsaIORequest_t;

/** \brief handle to access frame
 *
 * This data structure is the handle to access frame
 */
typedef void *agsaFrameHandle_t;

/** \brief describe a SAS ReCofiguration structure in the SAS/SATA hardware
 *
 * Describe a SAS ReConfiguration in the SAS/SATA hardware
 *
 */
typedef struct agsaSASReconfig_s {
  bit32     flags;                 /* flag to indicate a change to the default parameter
                                      bit31-30:reserved
                                      bit29:   a change to the default SAS/SATA ports is requested
                                      bit28:   the OPEN REJECT (RETRY) in command phase is requested
                                      bit27:   the OPEN REJECT (RETRY) in data phase is requested
                                      bit26:   REJECT will be mapped into OPEN REJECT
                                      bit25:   delay for SATA Head-of-Line blocking detection timeout
                                      bit24-00:reserved */
  bit16     reserved0;             /* reserved */
  bit8      reserved1;             /* reserved */
  bit8      maxPorts;              /* This field is valid if bit 29 of the flags field is set to 1 */
  bit16     openRejectRetriesCmd;  /* This field is valid if bit 28 of the flags field is set to 1 */
  bit16     openRejectRetriesData; /* This field is valid if bit 27 of the flags field is set to 1.*/
  bit16     reserved2;             /* reserved */
  bit16     sataHolTmo;            /* This field is valid if bit 25 of the flags field is set to 1 */
} agsaSASReconfig_t;

/** \brief describe a Phy Analog Setup registers for a Controller in the SAS/SATA hardware
 *
 * Describe a Phy Analog Setup registers for a controller in the SAS/SATA hardware
 *
 */
typedef struct agsaPhyAnalogSetupRegisters_s
{
  bit32     spaRegister0;
  bit32     spaRegister1;
  bit32     spaRegister2;
  bit32     spaRegister3;
  bit32     spaRegister4;
  bit32     spaRegister5;
  bit32     spaRegister6;
  bit32     spaRegister7;
  bit32     spaRegister8;
  bit32     spaRegister9;
} agsaPhyAnalogSetupRegisters_t;

#define MAX_INDEX 10

/** \brief
 *
 */
typedef struct agsaPhyAnalogSetupTable_s
{
  agsaPhyAnalogSetupRegisters_t     phyAnalogSetupRegisters[MAX_INDEX];
} agsaPhyAnalogSetupTable_t;

/** \brief describe a Phy Analog Setting
 *
 * Describe a Phy Analog Setup registers for a controller in the SAS/SATA hardware
 *
 */
typedef struct agsaPhyAnalogSettingsPage_s
{
  bit32   Dword0;
  bit32   Dword1;
  bit32   Dword2;
  bit32   Dword3;
  bit32   Dword4;
  bit32   Dword5;
  bit32   Dword6;
  bit32   Dword7;
  bit32   Dword8;
  bit32   Dword9;
} agsaPhyAnalogSettingsPage_t;


/** \brief describe a Open reject retry backoff threshold page
 *
 * Describe a Open reject retry backoff threshold registers in the SAS/SATA hardware
 *
 */
typedef struct agsaSASPhyOpenRejectRetryBackOffThresholdPage_s
{
  bit32   Dword0;
  bit32   Dword1;
  bit32   Dword2;
  bit32   Dword3;
} agsaSASPhyOpenRejectRetryBackOffThresholdPage_t;

/** \brief describe a Phy Rate Control
 *  4.56  agsaPhyRateControlPage_t
 *  Description
 *  This profile page is used to read or set several rate control
 *  parameters. The page code for this profile page is 0x07. This page can
 *  be READ by issuing saGetPhyProfile(). It can be read anytime and there
 *  is no need to quiesce the I/O to the controller.
 *  Related parameters can be modified by issuing saSetPhyProfile() before
 *  calling saPhyStart() to the PHY.
 *  Note: This page is applicable only to the SPCv controller.
 *  Usage
 *  Initiator and target.
 */
typedef struct agsaPhyRateControlPage_s
{
  bit32 Dword0;
  bit32 Dword1;
  bit32 Dword2;
} agsaPhyRateControlPage_t;

/**
 *  Dword0 Bits 0-11: ALIGN_RATE(ALNR). Align Insertion rate is 2 in every
 *  ALIGN_RATE+1 DWord. The default value results in the standard compliant
 *  value of 2/256. This rate applies to out of connection, SMP and SSP
 *  connections. The default value is 0x0ff. Other bits are reserved.
 *  Dword1 Bits 0 -11: STP_ALIGN_RATE(STPALNR) Align Insertion rate is 2 in
 *  every ALIGN_RATE+1 DWords. Default value results in standard compliant
 *  value of 2/256. This rate applies to out of STP connections. The default
 *  value is 0x0ff. Other bits are reserved.
 *  Dword2 Bits 0-7: SSP_FRAME_RATE(SSPFRMR) The number of idle DWords
 *  between each SSP frame. 0 means no idle cycles. The default value is
 *  0x0. Other bits are reserved.
**/

/** \brief describe a Register Dump information for a Controller in the SAS/SATA hardware
 *
 * Describe a register dump information for a controller in the SAS/SATA hardware
 *
 */
typedef struct agsaRegDumpInfo_s
{
  bit8    regDumpSrc;
  bit8    regDumpNum;
  bit8    reserved[2];
  bit32   regDumpOffset;
  bit32   directLen;
  void    *directData;
  bit32   indirectAddrUpper32;
  bit32   indirectAddrLower32;
  bit32   indirectLen;
} agsaRegDumpInfo_t;

/*
7 :  SPC GSM register at [MEMBASE-III SHIFT =  0x00_0000]
8 :  SPC GSM register at [MEMBASE-III SHIFT =  0x05_0000]
9 :  BDMA GSM register at [MEMBASE-III SHIFT =  0x01_0000]
10:  PCIe APP GSM register at [MEMBASE-III SHIFT =  0x01_0000]
11:  PCIe PHY GSM register at [MEMBASE-III SHIFT =  0x01_0000]
12:  PCIe CORE GSM register at [MEMBASE-III SHIFT =  0x01_0000]
13:  OSSP GSM register at [MEMBASE-III SHIFT =  0x02_0000]
14:  SSPA GSM register at [MEMBASE-III SHIFT =  0x03_0000]
15:  SSPA GSM register at [MEMBASE-III SHIFT =  0x04_0000]
16:  HSST GSM register at [MEMBASE-III SHIFT =  0x02_0000]
17:  LMS_DSS(A) GSM register at [MEMBASE-III SHIFT =  0x03_0000]
18:  SSPL_6G GSM register at [MEMBASE-III SHIFT =  0x03_0000]
19:  HSST(A) GSM register at [MEMBASE-III SHIFT =  0x03_0000]
20:  LMS_DSS(A) GSM register at [MEMBASE-III SHIFT =  0x04_0000]
21:  SSPL_6G GSM register at [MEMBASE-III SHIFT =  0x04_0000]
22:  HSST(A) GSM register at [MEMBASE-III SHIFT =  0x04_0000]
23:  MBIC IOP GSM register at [MEMBASE-III SHIFT =  0x06_0000]
24:  MBIC AAP1 GSM register at [MEMBASE-III SHIFT =  0x07_0000]
25:  SPBC GSM register at [MEMBASE-III SHIFT =  0x09_0000]
26:  GSM GSM register at [MEMBASE-III SHIFT =  0x70_0000]
*/

#define TYPE_GSM_SPACE        1
#define TYPE_QUEUE            2
#define TYPE_FATAL            3
#define TYPE_NON_FATAL        4
#define TYPE_INBOUND_QUEUE    5 
#define TYPE_OUTBOUND_QUEUE   6 


#define BAR_SHIFT_GSM_OFFSET  0x400000

#define ONE_MEGABYTE  0x100000
#define SIXTYFOURKBYTE   (1024 * 64)



#define TYPE_INBOUND          1
#define TYPE_OUTBOUND         2
	
typedef struct
{
  bit32  DataType;
  union
  {
    struct
    {
      bit32  directLen;
      bit32  directOffset;
      bit32  readLen;
      void  *directData;
    }gsmBuf;

    struct
    {
      bit16  queueType;
      bit16  queueIndex;
      bit32  directLen;
      void  *directData;
    }queueBuf;

    struct
    {
      bit32  directLen;
      bit32  directOffset;
      bit32  readLen;
      void  *directData;
    }dataBuf;
  } BufferType;
} agsaForensicData_t;

/** \brief describe a NVMData for a Controller in the SAS/SATA hardware
 *
 * Describe a NVMData for a controller in the SAS/SATA hardware
 *
 */
typedef struct agsaNVMDData_s
{
  bit32   indirectPayload      :1;
  bit32   reserved             :7;
  bit32   TWIDeviceAddress     :8;
  bit32   TWIBusNumber         :4;
  bit32   TWIDevicePageSize    :4;
  bit32   TWIDeviceAddressSize :4;
  bit32   NVMDevice            :4;
  bit32   directLen            :8;
  bit32   dataOffsetAddress    :24;
  void   *directData;
  bit32   indirectAddrUpper32;
  bit32   indirectAddrLower32;
  bit32   indirectLen;
  bit32   signature;
} agsaNVMDData_t;


/* status of ossaPCIeDiagExecuteCB() is shared with ossaSASDiagExecuteCB() */
#define OSSA_PCIE_DIAG_SUCCESS                                          0x0000
#define OSSA_PCIE_DIAG_INVALID_COMMAND                                  0x0001
#define OSSA_PCIE_DIAG_INTERNAL_FAILURE                                 0x0002
#define OSSA_PCIE_DIAG_INVALID_CMD_TYPE                                 0x1006
#define OSSA_PCIE_DIAG_INVALID_CMD_DESC                                 0x1007
#define OSSA_PCIE_DIAG_INVALID_PCIE_ADDR                                0x1008
#define OSSA_PCIE_DIAG_INVALID_BLOCK_SIZE                               0x1009
#define OSSA_PCIE_DIAG_LENGTH_NOT_BLOCK_SIZE_ALIGNED                    0x100A
#define OSSA_PCIE_DIAG_IO_XFR_ERROR_DIF_MISMATCH                        0x3000
#define OSSA_PCIE_DIAG_IO_XFR_ERROR_DIF_APPLICATION_TAG_MISMATCH        0x3001
#define OSSA_PCIE_DIAG_IO_XFR_ERROR_DIF_REFERENCE_TAG_MISMATCH          0x3002
#define OSSA_PCIE_DIAG_IO_XFR_ERROR_DIF_CRC_MISMATCH                    0x3003
#define OSSA_PCIE_DIAG_MPI_ERR_INVALID_LENGTH                           0x0042
#define OSSA_PCIE_DIAG_MPI_ERR_IO_RESOURCE_UNAVAILABLE                  0x1004
#define OSSA_PCIE_DIAG_MPI_ERR_CONTROLLER_NOT_IDLE                      0x1005


typedef struct agsaPCIeDiagExecute_s
{
  bit32 command;
  bit32 flags;
  bit16 initialIOSeed;
  bit16 reserved;
  bit32 rdAddrLower;
  bit32 rdAddrUpper;
  bit32 wrAddrLower;
  bit32 wrAddrUpper;
  bit32 len;
  bit32 pattern;
  bit8  udtArray[6];
  bit8  udrtArray[6];
} agsaPCIeDiagExecute_t;


/** \brief agsaPCIeDiagResponse_t
 *
 *  status of ossaPCIeDiagExecuteCB()
 *  The agsaPCIeDiagResponse_t structure is a parameter passed to
 *   ossaPCIeDiagExecuteCB()
 * to contain a PCIe Diagnostic command response.
 */

typedef struct agsaPCIeDiagResponse_s {
  bit32  ERR_BLKH;
  bit32  ERR_BLKL;
  bit32  DWord8;
  bit32  DWord9;
  bit32  DWord10;
  bit32  DWord11;
  bit32  DIF_ERR;
} agsaPCIeDiagResponse_t;


/** \brief describe a fatal error information for a Controller in the SAS/SATA hardware
 *
 * Describe a fatal error information for a controller in the SAS/SATA hardware
 *
 */
typedef struct agsaFatalErrorInfo_s
{
  bit32   errorInfo0;
  bit32   errorInfo1;
  bit32   errorInfo2;
  bit32   errorInfo3;
  bit32   regDumpBusBaseNum0;
  bit32   regDumpOffset0;
  bit32   regDumpLen0;
  bit32   regDumpBusBaseNum1;
  bit32   regDumpOffset1;
  bit32   regDumpLen1;
} agsaFatalErrorInfo_t;

/** \brief describe a information for a Event in the SAS/SATA hardware
 *
 * Describe a general information for a Event in the SAS/SATA hardware
 *
 */
typedef struct agsaEventSource_s
{
  agsaPortContext_t *agPortContext;
  bit32                   event;
  bit32                   param;
} agsaEventSource_t;

/** \brief describe a information for a Controller in the SAS/SATA hardware
 *
 * Describe a general information for a controller in the SAS/SATA hardware
 *
 */
typedef struct agsaControllerInfo_s
{
  bit32     signature;        /* coherent controller information */
  bit32     fwInterfaceRev;   /* host and controller interface version */
  bit32     hwRevision;       /* controller HW Revision number */
  bit32     fwRevision;       /* controller FW Revision number */
  bit32     ilaRevision;      /* controller ILA Revision number */
  bit32     maxPendingIO;     /* maximum number of outstanding I/Os supported */
  bit32     maxDevices;       /* Maximum Device Supported by controller */
  bit32     maxSgElements;    /* maximum number of SG elements supported */
  bit32     queueSupport;     /* maximum number of IQ and OQ supported
                               bit31-19 reserved
                               bit18    interrupt coalescing
                               bit17    reserved
                               bit16    high priority IQ supported
                               bit15-08 maximum number of OQ
                               bit07-00 maximum number of IQ */
  bit8      phyCount;         /* number of phy available in the controller */
  bit8      controllerSetting;/* Controller setting
                               bit07-04 reserved
                               bit03-00 HDA setting */
  bit8      PCILinkRate;      /* PCI generation 1/2/3 2.5g/5g/8g  */
  bit8      PCIWidth;         /* PCI number of lanes */
  bit32     sasSpecsSupport;  /* the supported SAS spec. */
  bit32     sdkInterfaceRev;  /* sdk interface reversion */
  bit32     sdkRevision;      /* sdk reversion */
} agsaControllerInfo_t;

/** \brief describe a status for a Controller in the SAS/SATA hardware
 *
 * Describe a general status for a controller in the SAS/SATA hardware
 *
 */
typedef struct agsaControllerStatus_s
{
  agsaFatalErrorInfo_t fatalErrorInfo; /* fatal error information */
  bit32     interfaceState;            /* host and controller interface state
                                          bit02-00 state of host and controller
                                          bit16-03 reserved
                                          bit31-16 detail of error based on error state */
  bit32     iqFreezeState0;            /* freeze state of 1st set of IQ */
  bit32     iqFreezeState1;            /* freeze state of 2nd set of IQ */
  bit32     tickCount0;                /* tick count in second for internal CPU-0 */
  bit32     tickCount1;                /* tick count in second for internal CPU-1 */
  bit32     tickCount2;                /* tick count in second for internal CPU-2 */
  bit32     phyStatus[8];              /* status of phy 0 to phy 15 */
  bit32     recoverableErrorInfo[8];   /* controller specific recoverable error information */
  bit32     bootStatus;
  bit16     bootComponentState[8];

} agsaControllerStatus_t;

/** \brief describe a GPIO Event Setup Infomation in the SAS/SATA hardware
 *
 * Describe a configuration for a GPIO Event Setup Infomation in the SAS/SATA hardware
 *
 */
typedef struct agsaGpioEventSetupInfo_s
{
  bit32         gpioPinMask;
  bit32         gpioEventLevel;
  bit32         gpioEventRisingEdge;
  bit32         gpioEventFallingEdge;
} agsaGpioEventSetupInfo_t;

/** \brief describe a GPIO Pin Setup Infomation in the SAS/SATA hardware
 *
 * Describe a configuration for a GPIO Pin Setup Infomation in the SAS/SATA hardware
 *
 */
typedef struct agsaGpioPinSetupInfo_t
{
  bit32         gpioPinMask;
  bit32         gpioInputEnabled;
  bit32         gpioTypePart1;
  bit32         gpioTypePart2;
} agsaGpioPinSetupInfo_t;

/** \brief describe a serial GPIO operation in the SAS/SATA hardware
 *
 * Describe a configuration for a GPIO write Setup Infomation in the SAS/SATA hardware
 *
 */
typedef struct agsaGpioWriteSetupInfo_s
{
  bit32         gpioWritemask; 
  bit32         gpioWriteVal;
}agsaGpioWriteSetupInfo_t;

/** \brief describe a GPIO Read Infomation in the SAS/SATA hardware
 *
 * Describe a configuration for a GPIO read Infomation in the SAS/SATA hardware
 *
 */
typedef struct agsaGpioReadInfo_s
{
  bit32         gpioReadValue; 
  bit32         gpioInputEnabled; /* GPIOIE */
  bit32         gpioEventLevelChangePart1; /* GPIEVCHANGE (pins 11-0) */
  bit32         gpioEventLevelChangePart2; /* GPIEVCHANGE (pins 23-20) */
  bit32         gpioEventRisingEdgePart1; /* GPIEVRISE (pins 11-0) */
  bit32         gpioEventRisingEdgePart2; /* GPIEVRISE (pins 23-20) */
  bit32         gpioEventFallingEdgePart1; /* GPIEVALL (pins 11-0) */
  bit32         gpioEventFallingEdgePart2; /* GPIEVALL (pins 23-20) */
}agsaGpioReadInfo_t;

/** \brief describe a serial GPIO request and response in the SAS/SATA hardware
 *
 * Describe the fields required for serial GPIO request and response in the SAS/SATA hardware
 *
 */
typedef struct agsaSGpioReqResponse_s
{
    bit8 smpFrameType;                                      /* 0x40 for request, 0x41 for response*/
    bit8 function;                                          /* 0x02 for read, 0x82 for write */
    bit8 registerType;                                      /* used only in request */
    bit8 registerIndex;                                     /* used only in request */
    bit8 registerCount;                                     /* used only in request */
    bit8 functionResult;                                    /* used only in response */
    bit32 readWriteData[OSSA_SGPIO_MAX_READ_DATA_COUNT];    /* write data for request; read data for response */
} agsaSGpioReqResponse_t;


/** \brief describe a serial GPIO operation response in the SAS/SATA hardware
 *
 * Describe the fields required for serial GPIO operations response in the SAS/SATA hardware
 *
 */
typedef struct agsaSGpioCfg0
{
    bit8 reserved1;
    bit8 version:4;
    bit8 reserved2:4;
    bit8 gpRegisterCount:4;
    bit8 cfgRegisterCount:3;
    bit8 gpioEnable:1;
    bit8 supportedDriveCount;
} agsaSGpioCfg0_t;

/** \brief SGPIO configuration register 1
 *
 * These fields constitute SGPIO configuration register 1, as defined by SFF-8485 spec
 *
 */
typedef struct agsaSGpioCfg1{
    bit8 reserved;
    bit8 blinkGenA:4;
    bit8 blinkGenB:4;
    bit8 maxActOn:4;
    bit8 forceActOff:4;
    bit8 stretchActOn:4;
    bit8 stretchActOff:4;
} agsaSGpioCfg1_t;

/** \brief describe a configuration for a PHY in the SAS/SATA hardware
 *
 * Describe a configuration for a PHY in the SAS/SATA hardware
 *
 */
typedef struct agsaPhyConfig_s
{
  bit32   phyProperties;
                      /**< b31-b8 reserved */
                      /**< b16-b19 SSC Disable */
                      /**< b15-b8 phy analog setup index */
                      /**< b7     phy analog setup enable */
                      /**< b6     Control spin up hold */
                      /**< b5-b4  SAS/SATA mode, bit4 - SAS, bit5 - SATA, 11b - Auto mode */
                      /**< b3-b0  Max. Link Rate, bit0 - 1.5Gb/s, bit1 - 3.0Gb/s,
                                  bit2 - 6.0Gb/s, bit3 - reserved */
} agsaPhyConfig_t;


/** \brief Structure is used as a parameter passed in saLocalPhyControlCB() to describe the error counter
 *
 * Description
 * This profile page is used to read or set the SNW-3 PHY capabilities of a
 * SAS PHY. This page can be read by calling saGetPhyProfile(). It can be
 * read anytime and there is no need to quiesce he I/O to the controller.
 * The format of the 32-bit SNW3 is the same as defined in the SAS 2
 * specification.
 * Local SNW3 can be modified by calling saSetPhyProfile() before
 * saPhyStart() to the PHY. REQUESTED LOGICAL LINK RATE is reserved.
 * The SPCv will calculate the PARITY field.

 * Note: This page is applicable only to the SPCv controller.
 * Usage
 * Initiator and target.
 */

typedef struct agsaPhySNW3Page_s
{
  bit32   LSNW3;
  bit32   RSNW3;
} agsaPhySNW3Page_t;

/** \brief structure describe error counters of a PHY in the SAS/SATA
 *
 * Structure is used as a parameter passed in saLocalPhyControlCB()
 * to describe the error counter
 *
 */
typedef struct agsaPhyErrCounters_s
{
  bit32   invalidDword;             /* Number of invalid dwords that have been
                                       received outside of phy reset sequences.*/
  bit32   runningDisparityError;    /* Number of dwords containing running disparity
                                       errors that have been received outside of phy
                                       reset sequences.*/
  bit32   lossOfDwordSynch;         /* Number of times the phy has restarted the link
                                       reset sequence because it lost dword synchronization.*/
  bit32   phyResetProblem;          /* Number of times the phy did not obtain dword
                                       synchronization during the final SAS speed
                                       negotiation window.*/
  bit32   elasticityBufferOverflow; /* Number of times the phys receive elasticity
                                       buffer has overflowed.*/
  bit32   receivedErrorPrimitive;   /* Number of times the phy received an ERROR primitive */
  bit32   inboundCRCError;          /* Number of inbound CRC Error */
  bit32   codeViolation;            /* Number of code violation */
} agsaPhyErrCounters_t;


/** \brief
 * used in saGetPhyProfile
 */
typedef struct agsaPhyErrCountersPage_s
{
  bit32   invalidDword;
  bit32   runningDisparityError;
  bit32   codeViolation;
  bit32   lossOfDwordSynch;
  bit32   phyResetProblem;
  bit32   inboundCRCError;
} agsaPhyErrCountersPage_t;

/** \brief structure describes bandwidth counters of a PHY in the SAS/SATA
 *
 * Structure is used as a parameter passed in saGetPhyProfile()
 * to describe the error counter
 *
 */

typedef struct agsaPhyBWCountersPage_s
{
  bit32   TXBWCounter;
  bit32   RXBWCounter;
} agsaPhyBWCountersPage_t;



/** \brief structure describe hardware configuration
 *
 * Structure is used as a parameter passed in saInitialize() to describe the
 * configuration used during hardware initialization
 *
 */
typedef struct agsaHwConfig_s
{
  bit32   phyCount;                     /**< Number of PHYs that are to be configured
                                         and initialized.  */
  bit32   hwInterruptCoalescingTimer;   /**< Host Interrupt CoalescingTimer */
  bit32   hwInterruptCoalescingControl; /**< Host Interrupt CoalescingControl */
  bit32   intReassertionOption;         /**< Interrupt Ressertion Option */
  bit32   hwOption;                     /** PCAD64 on 64 bit addressing */

  agsaPhyAnalogSetupTable_t phyAnalogConfig; /**< Phy Analog Setting Table */
} agsaHwConfig_t;

/** \brief structure describe software configuration
 *
 * Structure is used as a parameter passed in saInitialize() to describe the
 * configuration used during software initialization
 *
 */
typedef struct agsaSwConfig_s
{
  bit32   maxActiveIOs;                 /**< Maximum active I/O requests supported */
  bit32   numDevHandles;                /**< Number of SAS/SATA device handles allocated
                                         in the pool */
  bit32   smpReqTimeout;                /**< SMP request time out in millisecond */
  bit32   numberOfEventRegClients;      /**< Maximum number of OS Layer clients for the event
                                             registration defined by saRegisterEventCallback() */
  bit32   sizefEventLog1;               /**< Size of Event Log 1 */
  bit32   sizefEventLog2;               /**< Size of Event Log 2 */
  bit32   eventLog1Option;              /**< Option of Event Log 1 */
  bit32   eventLog2Option;              /**< Option of Event Log 2 */

  bit32   fatalErrorInterruptEnable:1;  /**< 0 Fatal Error Iterrupt Enable */
  bit32   sgpioSupportEnable:1;         /**< 1 SGPIO Support Enable */
  bit32   fatalErrorInterruptVector:8;  /**< 2-9  Fatal Error Interrupt Vector */
  bit32   max_MSI_InterruptVectors:8;   /**< 10-18 Maximum MSI Interrupt Vectors */
  bit32   max_MSIX_InterruptVectors:8;  /**< 18-25 Maximum MSIX Interrupt Vectors */
  bit32   legacyInt_X:1;                /**< 26 Support Legacy Interrupt */
  bit32   hostDirectAccessSupport:1;    /**< 27 Support HDA mode */
  bit32   hostDirectAccessMode:2;       /**< 28-29 HDA mode: 00b - HDA SoftReset, 01b - HDA Normal */
  bit32   enableDIF:1;                  /**< 30 */
  bit32   enableEncryption:1;           /**< 31 */
#ifdef SA_CONFIG_MDFD_REGISTRY
  bit32   disableMDF;                   /*disable MDF*/
#endif
  bit32   param1;                       /**< parameter1 */
  bit32   param2;                       /**< parameter2 */
  void    *param3;                      /**< parameter3 */
  void    *param4;                      /**< paramater4 */
  bit32   stallUsec;
  bit32   FWConfig;
  bit32   PortRecoveryResetTimer;
  void    *mpiContextTable;             /** Pointer to a table that contains agsaMPIContext_t
                                            entries. This table is used to fill in MPI table
                                            fields. Values in this table are written to MPI table last.
                                            Any previous values in MPI table are overwritten by values
                                            in this table. */

  bit32   mpiContextTablelen;           /** Number of agsaMPIContext_t entries in mpiContextTable */

#if defined(SALLSDK_DEBUG)
  bit32   sallDebugLevel;               /**< Low Layer debug level */
#endif

#ifdef SA_ENABLE_PCI_TRIGGER
  bit32   PCI_trigger;
#endif /* SA_ENABLE_PCI_TRIGGER */

#ifdef SA_ENABLE_TRACE_FUNCTIONS
  bit32 TraceDestination;
  bit32 TraceBufferSize;
  bit32 TraceMask;
#endif /* SA_ENABLE_TRACE_FUNCTIONS */
} agsaSwConfig_t;


typedef struct agsaQueueInbound_s
{
  bit32   elementCount:16;  /* Maximum number of elements in the queue (queue depth).
                               A value of zero indicates that the host disabled this queue.*/
  bit32   elementSize:16;   /* Size of each element in the queue in bytes.*/
  bit32   priority:2;       /* Queue priority:
                                    00: normal priority
                                    01: high priority
                                    10: reserved
                                    11: reserved */
  bit32   reserved:30;
} agsaQueueInbound_t;

typedef struct agsaQueueOutbound_s
{
  bit32   elementCount:16;          /* Maximum number of elements in the queue (queue depth).
                                       A value of zero indicates that the host disabled
                                       this queue.*/
  bit32   elementSize:16;           /* Size of each element in the queue in bytes.*/
  bit32   interruptDelay:16;        /* Time, in usec, to delay interrupts to the host.
                                       Zero means not to delay based on time. An
                                       interrupt is passed to the host when either of
                                       the interruptDelay or interruptCount parameters
                                       is satisfied. Default value is 0.*/
  bit32   interruptCount:16;        /* Number of interrupts required before passing to
                                       the host. Zero means not to coalesce based on count. */
  bit32   interruptVectorIndex:8;   /* MSI/MSI-X interrupt vector index. For MSI, when
                                       Multiple Messages is enabled, this field is the
                                       index to the MSI vectors derived from a single
                                       Message Address and multiple Message Data.
                                       For MSI-X, this field is the index to the
                                       MSI-X Table Structure. */
  bit32   interruptEnable:1;        /* 0b: No interrupt to host (host polling)
                                       1b: Interrupt enabled */
  bit32   reserved:23;

} agsaQueueOutbound_t;

typedef struct agsaPhyCalibrationTbl_s
{
  bit32   txPortConfig1;            /* transmitter per port configuration 1 SAS_SATA G1 */
  bit32   txPortConfig2;            /* transmitter per port configuration 2 SAS_SATA G1*/
  bit32   txPortConfig3;            /* transmitter per port configuration 3 SAS_SATA G1*/
  bit32   txConfig1;                /* transmitter configuration 1 */
  bit32   rvPortConfig1;            /* reveiver per port configuration 1 SAS_SATA G1G2 */
  bit32   rvPortConfig2;            /* reveiver per port configuration 2 SAS_SATA G3 */
  bit32   rvConfig1;                /* reveiver per configuration 1 */
  bit32   rvConfig2;                /* reveiver per configuration 2 */
  bit32   reserved[2];              /* reserved */
} agsaPhyCalibrationTbl_t;

typedef struct agsaQueueConfig_s
{
  bit16   numInboundQueues;
  bit16   numOutboundQueues;
  bit8    sasHwEventQueue[AGSA_MAX_VALID_PHYS];
  bit8    sataNCQErrorEventQueue[AGSA_MAX_VALID_PHYS];
  bit8    tgtITNexusEventQueue[AGSA_MAX_VALID_PHYS];
  bit8    tgtSSPEventQueue[AGSA_MAX_VALID_PHYS];
  bit8    tgtSMPEventQueue[AGSA_MAX_VALID_PHYS];
  bit8    iqNormalPriorityProcessingDepth;
  bit8    iqHighPriorityProcessingDepth;
  bit8    generalEventQueue;
  bit8    tgtDeviceRemovedEventQueue;
  bit32   queueOption;
  agsaQueueInbound_t  inboundQueues[AGSA_MAX_INBOUND_Q];
  agsaQueueOutbound_t outboundQueues[AGSA_MAX_OUTBOUND_Q];
} agsaQueueConfig_t;

#define OQ_SHARE_PATH_BIT 0x00000001

typedef struct agsaFwImg_s
{
  bit8    *aap1Img;             /**< AAP1 Image */
  bit32   aap1Len;              /**< AAP1 Image Length */
  bit8    *ilaImg;              /**< ILA Image */
  bit32   ilaLen;               /**< ILA Image Length */
  bit8    *iopImg;              /**< IOP Image */
  bit32   iopLen;               /**< IOP Image Length */
  bit8    *istrImg;             /**< Init String */
  bit32   istrLen;              /**< Init String Length */
} agsaFwImg_t;

/** \brief generic memory descriptor
 *
 * a generic memory descriptor used for describing a memory requirement in a structure
 *
 */
typedef struct agsaMem_s
{
  void    *virtPtr;             /**< Virtual pointer to the memory chunk */
  void    *osHandle;            /**< Handle used for OS to free memory */
  bit32   phyAddrUpper;         /**< Upper 32 bits of physical address */
  bit32   phyAddrLower;         /**< Lower 32 bits of physical address */
  bit32   totalLength;          /**< Total length in bytes allocated */
  bit32   numElements;          /**< Number of elements */
  bit32   singleElementLength;  /**< Size in bytes of an element */
  bit32   alignment;            /**< Alignment in bytes needed. A value of one indicates
                                     no specific alignment requirement */
  bit32   type;                 /**< DMA or Cache */
  bit32   reserved;             /**< reserved */
} agsaMem_t;

/** \brief specify the controller Event Log for the SAS/SATA LL Layer
 *
 * data structure used in the saGetControllerEventLogInfo() function calls
 *
 */
typedef struct agsaControllerEventLog_s
{
  agsaMem_t   eventLog1;
  agsaMem_t   eventLog2;
  bit32       eventLog1Option;
  bit32       eventLog2Option;
} agsaControllerEventLog_t;

/* Log Option - bit3-0 */
#define DISABLE_LOGGING 0x0
#define CRITICAL_ERROR  0x1
#define WARNING         0x2
#define NOTICE          0x3
#define INFORMATION     0x4
#define DEBUGGING       0x5

/** \brief specify the SAS Diagnostic Parameters for the SAS/SATA LL Layer
 *
 * data structure used in the saGetRequirements() and the saInitialize() function calls
 *
 */
typedef struct agsaSASDiagExecute_s
{
  bit32 command;
  bit32 param0;
  bit32 param1;
  bit32 param2;
  bit32 param3;
  bit32 param4;
  bit32 param5;
} agsaSASDiagExecute_t;


/** \brief  for the SAS/SATA LL Layer
 *
 *  This data structure contains the general status of a SAS Phy.
 *  Section 4.60
 */
typedef struct agsaSASPhyGeneralStatusPage_s
{
  bit32 Dword0;
  bit32 Dword1;
} agsaSASPhyGeneralStatusPage_t;


/** \brief specify the memory allocation requirement for the SAS/SATA LL Layer
 *
 * data structure used in the saGetRequirements() and the saInitialize() function calls
 *
 */
typedef struct agsaMemoryRequirement_s
{
  bit32       count;                         /**< The number of memory chunks used
                                                  in the agMemory table */
  agsaMem_t   agMemory[AGSA_NUM_MEM_CHUNKS]; /**< The structure that defines the memory
                                                  requirement structure */
} agsaMemoryRequirement_t;


/** \brief describe a SAS address and PHY Identifier
 *
 * This structure is used
 *
 */
typedef struct agsaSASAddressID_s
{
  bit8   sasAddressLo[4];     /**< HOST SAS address lower part */
  bit8   sasAddressHi[4];     /**< HOST SAS address higher part */
  bit8   phyIdentifier;    /**< PHY IDENTIFIER of the PHY */
} agsaSASAddressID_t;

/** \brief data structure provides some information about a SATA device
 *
 * data structure provides some information about a SATA device discovered
 * following the SATA discovery.
 *
 */
typedef struct agsaDeviceInfo_s
{
  bit16   smpTimeout;
  bit16   it_NexusTimeout;
  bit16   firstBurstSize;
  bit8    reserved;
    /* Not Used */
  bit8    devType_S_Rate;
    /* Bit 6-7: reserved
       Bit 4-5: Two-bit flag to specify a SSP/SMP, or directly attached SATA or STP device
                00: STP device
                01: SSP or SMP device
                10: Direct SATA device
       Bit 0-3: Connection Rate field when opening the device.
                Code Description:
                08h:  1.5 Gbps
                09h:  3.0 Gbps
                0ah:  6.0 Gbps
                All others Reserved
    */
  bit8    sasAddressHi[4];
  bit8    sasAddressLo[4];
  bit32   flag;
/*
flag
Bit 0: Retry flag.
      1b: enable SAS TLR (Transport Layer Retry).
      0b: disable SAS TLR (Transport Layer Retry).
          When used during device registration, it is recommended that TLR is
          enabled, i.e. set the bit to 1.
Bit 1: Priority setting for AWT (Arbitration Wait Time) for this device.
      0b: Default setting (recommended). Actual AWT value TBD.
      1b: Increase priority. Actual AWT value TBD.
Bit 2-3: Reserved
Bit 4-11: Zero-based PHY identifier. This field is used only if bits 4-5 in devType_S_Rate are set to 10b
          which indicates a directly-attached SATA drive.
Bit 12-15: Reserved
Bit 16-19 : Maximum Connection Number. This field specifies the maximum number of connections that
            can be established with the device concurrently. This field is set to the lowest port width along the pathway
            from the controller to the device. This is applicable only to the SPCv controller.
            However, for backward compatibility reasons, if this field is set to zero, it is treated as 1 so that the controller
            can establish at least one connection.
Bit 20: Initiator Role
        This bit indicates whether the device has SSP initiator role capability. This is applicable only to the SPCv controller.
      0b : The device has no SSP initiator capability.
      1b : The device has SSP initiator capability.
Bit 21: ATAPI Device Flag. (Only applies to the SPCv) Flag to indicate ATAPI protocol support
      0b : Device does not support ATAPI protocol.
      1b : Device supports ATAPI protocol.
Bit 22-31: Reserved
*/
} agsaDeviceInfo_t;


#define DEV_INFO_MASK       0xFF
#define DEV_INFO_MCN_SHIFT  16
#define DEV_INFO_IR_SHIFT   20

#define RETRY_DEVICE_FLAG            (1 << SHIFT0)
#define AWT_DEVICE_FLAG              (1 << SHIFT1)
#define SSP_DEVICE_FLAG              (1 << SHIFT20)
#define ATAPI_DEVICE_FLAG                 0x200000 /* bit21  */
#define XFER_RDY_PRIORTY_DEVICE_FLAG (1 << SHIFT22)


#define DEV_LINK_RATE 0x3F

#define SA_DEVINFO_GET_SAS_ADDRESSLO(devInfo) \
  DMA_BEBIT32_TO_BIT32(*(bit32 *)(devInfo)->sasAddressLo)

#define SA_DEVINFO_GET_SAS_ADDRESSHI(devInfo) \
  DMA_BEBIT32_TO_BIT32(*(bit32 *)(devInfo)->sasAddressHi)

#define SA_DEVINFO_GET_DEVICETTYPE(devInfo) \
  (((devInfo)->devType_S_Rate & 0xC0) >> 5)

#define SA_DEVINFO_PUT_SAS_ADDRESSLO(devInfo, src32) \
  *(bit32 *)((devInfo)->sasAddressLo) = BIT32_TO_DMA_BEBIT32(src32)

#define SA_DEVINFO_PUT_SAS_ADDRESSHI(devInfo, src32) \
  *(bit32 *)((devInfo)->sasAddressHi) = BIT32_TO_DMA_BEBIT32(src32)

/** \brief data structure provides some information about a SATA device
 *
 * data structure provides some information about a SATA device discovered
 * following the SATA discovery.
 *
 */
typedef struct agsaSATADeviceInfo_s
{
  agsaDeviceInfo_t          commonDevInfo;          /**< The general/common part of the
                                                         SAS/SATA device information */
  bit8                      connection;             /**< How device is connected:
                                                           0: Direct attached.
                                                           1: Behind Port Multiplier,
                                                              portMultiplierField is valid.
                                                           2: STP, stpPhyIdentifier is valid */

  bit8                      portMultiplierField;    /**< The first 4 bits indicate that
                                                         the Port Multiplier field is defined
                                                         by SATA-II. This field is valid only
                                                         if the connection field above is
                                                         set to 1 */

  bit8                      stpPhyIdentifier;       /**< PHY ID of the STP PHY. Valid only if
                                                         connection field is set to 2 (STP). */

  bit8                      reserved;
  bit8                      signature[8];           /**< The signature of SATA in Task
                                                         File registers following power up.
                                                         Only five bytes are defined by ATA.
                                                         The added three bytes are for
                                                         alignment purposes */
} agsaSATADeviceInfo_t;

/** \brief data structure provides some information about a SAS device
 *
 * data structure provides some information about a SAS device discovered
 * following the SAS discovery
 *
 */
typedef struct agsaSASDeviceInfo_s
{
  agsaDeviceInfo_t  commonDevInfo;          /**< The general/common part of the SAS/SATA
                                                 device information */
  bit8              initiator_ssp_stp_smp;  /**< SAS initiator capabilities */
                                            /* b4-7: reserved */
                                            /* b3:   SSP initiator port */
                                            /* b2:   STP initiator port */
                                            /* b1:   SMP initiator port */
                                            /* b0:   reserved */
  bit8              target_ssp_stp_smp;     /**< SAS target capabilities */
                                            /* b4-7: reserved */
                                            /* b3:   SSP target port */
                                            /* b2:   STP target port */
                                            /* b1:   SMP target port */
                                            /* b0:   reserved */
  bit32             numOfPhys;              /**< Number of PHYs in the device */
  bit8              phyIdentifier;          /**< PHY IDENTIFIER in IDENTIFY address
                                                 frame as defined by the SAS
                                                 specification. */
} agsaSASDeviceInfo_t;

#define SA_SASDEV_SSP_BIT         SA_IDFRM_SSP_BIT  /* SSP Initiator port */
#define SA_SASDEV_STP_BIT         SA_IDFRM_STP_BIT  /* STP Initiator port */
#define SA_SASDEV_SMP_BIT         SA_IDFRM_SMP_BIT  /* SMP Initiator port */
#define SA_SASDEV_SATA_BIT        SA_IDFRM_SATA_BIT /* SATA device, valid in the discovery response only */

#define SA_SASDEV_IS_SSP_INITIATOR(sasDev) \
  (((sasDev)->initiator_ssp_stp_smp & SA_SASDEV_SSP_BIT) == SA_SASDEV_SSP_BIT)

#define SA_SASDEV_IS_STP_INITIATOR(sasDev) \
  (((sasDev)->initiator_ssp_stp_smp & SA_SASDEV_STP_BIT) == SA_SASDEV_STP_BIT)

#define SA_SASDEV_IS_SMP_INITIATOR(sasDev) \
  (((sasDev)->initiator_ssp_stp_smp & SA_SASDEV_SMP_BIT) == SA_SASDEV_SMP_BIT)

#define SA_SASDEV_IS_SSP_TARGET(sasDev) \
  (((sasDev)->target_ssp_stp_smp & SA_SASDEV_SSP_BIT) == SA_SASDEV_SSP_BIT)

#define SA_SASDEV_IS_STP_TARGET(sasDev) \
  (((sasDev)->target_ssp_stp_smp & SA_SASDEV_STP_BIT) == SA_SASDEV_STP_BIT)

#define SA_SASDEV_IS_SMP_TARGET(sasDev) \
  (((sasDev)->target_ssp_stp_smp & SA_SASDEV_SMP_BIT) == SA_SASDEV_SMP_BIT)

#define SA_SASDEV_IS_SATA_DEVICE(sasDev) \
  (((sasDev)->target_ssp_stp_smp & SA_SASDEV_SATA_BIT) == SA_SASDEV_SATA_BIT)




/** \brief the data structure describe SG list
 *
 * the data structure describe SG list
 *
 */
typedef struct _SASG_DESCRIPTOR
{
  bit32   sgLower;  /**< Lower 32 bits of data area physical address */
  bit32   sgUpper;  /**< Upper 32 bits of data area physical address */
  bit32   len;      /**< Total data length in bytes */
} SASG_DESCRIPTOR, * PSASG_DESCRIPTOR;

/** \brief data structure used to pass information about the scatter-gather list to the LL Layer
 *
 * The ESGL pages are uncached, have a configurable number of SGL
 * of (min, max) = (1, 10), and are 16-byte aligned. Although
 * the application can configure the page size, the size must be
 * incremented in TBD-byte increments. Refer the hardware
 * documentation for more detail on the format of ESGL
 * structure.
 *
 */
typedef struct agsaSgl_s
{
  bit32             sgLower;     /**< Lower 32 bits of data area physical address */
  bit32             sgUpper;     /**< Upper 32 bits of data area physical address */
  bit32             len;         /**< Total data length in bytes */
  bit32             extReserved; /**< bit31 is for extended sgl list */
} agsaSgl_t;

/** \brief data structure is used to pass information about the extended
 *  scatter-gather list (ESGL) to the LL Layer
 *
 * The agsaEsgl_t data structure is used to pass information about the
 * extended scatter-gather list (ESGL) to the LL Layer.
 *
 * When ESGL is used, its starting address is specified the first descriptor
 * entry (i.e. descriptor[0]) in agsaSgl_t structure.
 *
 * The ESGL pages are uncached, have a fixed number of SGL of 10, and are
 * 16-byte aligned. Refer the hardware documentation for more
 * detail on ESGL.
 *
 */
typedef struct agsaEsgl_s
{
  agsaSgl_t descriptor[MAX_ESGL_ENTRIES];
} agsaEsgl_t;

/** \brief data structure describes an SSP Command INFORMATION UNIT
 *
 * data structure describes an SSP Command INFORMATION UNIT used for SSP command and is part of
 * the SSP frame.
 *
 * Currently, Additional CDB length is supported to 16 bytes
 *
 */
#define MAX_CDB_LEN 32
typedef struct agsaSSPCmdInfoUnitExt_s
{
  bit8  lun[8];
  bit8  reserved1;
  bit8  efb_tp_taskAttribute;
  bit8  reserved2;
  bit8  additionalCdbLen;
  bit8  cdb[MAX_CDB_LEN];
} agsaSSPCmdInfoUnitExt_t ;

#define DIF_UDT_SIZE                6

/* difAction in agsaDif_t */
#define AGSA_DIF_INSERT                     0
#define AGSA_DIF_VERIFY_FORWARD             1
#define AGSA_DIF_VERIFY_DELETE              2
#define AGSA_DIF_VERIFY_REPLACE             3
#define AGSA_DIF_VERIFY_UDT_REPLACE_CRC     5
#define AGSA_DIF_REPLACE_UDT_REPLACE_CRC    7

#define agsaDIFSectorSize512                0
#define agsaDIFSectorSize520                1
#define agsaDIFSectorSize4096               2
#define agsaDIFSectorSize4160               3



typedef struct agsaDif_s
{
  agBOOLEAN enableDIFPerLA;
  bit32 flags;
  bit16 initialIOSeed;
  bit16 reserved;
  bit32 DIFPerLAAddrLo;
  bit32 DIFPerLAAddrHi;
  bit16 DIFPerLARegion0SecCount;
  bit16 Reserved2;
  bit8 udtArray[DIF_UDT_SIZE];
  bit8 udrtArray[DIF_UDT_SIZE];
} agsaDif_t;


/* From LL SDK2 */
#define DIF_FLAG_BITS_ACTION            0x00000007  /* 0-2*/
#define DIF_FLAG_BITS_CRC_VER           0x00000008  /* 3 */
#define DIF_FLAG_BITS_CRC_INV           0x00000010  /* 4 */
#define DIF_FLAG_BITS_CRC_SEED          0x00000020  /* 5 */
#define DIF_FLAG_BITS_UDT_REF_TAG       0x00000040  /* 6 */
#define DIF_FLAG_BITS_UDT_APP_TAG       0x00000080  /* 7 */
#define DIF_FLAG_BITS_UDTR_REF_BLKCOUNT 0x00000100  /* 8 */
#define DIF_FLAG_BITS_UDTR_APP_BLKCOUNT 0x00000200  /* 9 */
#define DIF_FLAG_BITS_CUST_APP_TAG      0x00000C00  /* 10 11*/
#define DIF_FLAG_BITS_EPRC              0x00001000  /* 12 */
#define DIF_FLAG_BITS_Reserved          0x0000E000  /* 13 14 15*/
#define DIF_FLAG_BITS_BLOCKSIZE_MASK    0x00070000  /* 16 17 18 */
#define DIF_FLAG_BITS_BLOCKSIZE_SHIFT   16
#define DIF_FLAG_BITS_BLOCKSIZE_512     0x00000000  /* */
#define DIF_FLAG_BITS_BLOCKSIZE_520     0x00010000  /* 16 */
#define DIF_FLAG_BITS_BLOCKSIZE_4096    0x00020000  /* 17 */
#define DIF_FLAG_BITS_BLOCKSIZE_4160    0x00030000  /* 16 17 */
#define DIF_FLAG_BITS_UDTVMASK          0x03F00000  /* 20 21 22 23 24 25 */
#define DIF_FLAG_BITS_UDTV_SHIFT        20
#define DIF_FLAG_BITS_UDTUPMASK         0xF6000000  /* 26 27 28 29 30 31  */
#define DIF_FLAG_BITS_UDTUPSHIFT        26

typedef struct agsaEncryptDek_s
{
    bit32          dekTable;
    bit32          dekIndex;
} agsaEncryptDek_t;

typedef struct agsaEncrypt_s
{
    agsaEncryptDek_t dekInfo;
    bit32           kekIndex;
    agBOOLEAN       keyTagCheck;
    agBOOLEAN       enableEncryptionPerLA; /* new */
    bit32           sectorSizeIndex;
    bit32           cipherMode;
    bit32           keyTag_W0;
    bit32           keyTag_W1;
    bit32           tweakVal_W0;
    bit32           tweakVal_W1;
    bit32           tweakVal_W2;
    bit32           tweakVal_W3;
    bit32           EncryptionPerLAAddrLo; /* new */
    bit32           EncryptionPerLAAddrHi; /* new */
    bit16           EncryptionPerLRegion0SecCount; /* new */
    bit16           reserved;
} agsaEncrypt_t;

/** \brief data structure describes a SAS SSP command request to be sent to the target device
 *
 * data structure describes a SAS SSP command request to be sent to the
 * target device. This structure limits the CDB length in SSP
 * command up to 16 bytes long.
 *
 * This data structure is one instance of the generic request issued to
 * saSSPStart() and is passed as an agsaSASRequestBody_t .
 *
 */
typedef struct agsaSSPInitiatorRequest_s
{
  agsaSgl_t              agSgl;             /**< This structure is used to define either
                                                 an ESGL list or a single SGL for the SSP
                                                 command operation */
  bit32                  dataLength;        /**< Total data length in bytes */
  bit16                  firstBurstSize;    /**< First Burst Size field as defined by
                                                 SAS specification */
  bit16                  flag;              /**< bit1-0 TLR as SAS specification
                                                 bit31-2 reserved */
  agsaSSPCmdInfoUnit_t   sspCmdIU;          /**< Structure containing SSP Command
                                                 INFORMATION UNIT */
  agsaDif_t               dif;
  agsaEncrypt_t           encrypt;
#ifdef SA_TESTBASE_EXTRA
  /* Added by TestBase */
  bit16                   bstIndex;
#endif /*  SA_TESTBASE_EXTRA */
} agsaSSPInitiatorRequest_t;

/** \brief data structure describes a SAS SSP command request Ext to be sent to the target device
 *
 * data structure describes a SAS SSP command request to be sent to the
 * target device. This structure support the CDB length in SSP
 * command more than 16 bytes long.
 *
 * This data structure is one instance of the generic request issued to
 * saSSPStart() and is passed as an agsaSASRequestBody_t .
 *
 */
typedef struct agsaSSPInitiatorRequestExt_s
{
  agsaSgl_t              agSgl;             /**< This structure is used to define either
                                                 an ESGL list or a single SGL for the SSP
                                                 command operation */
  bit32                   dataLength;
  bit16                   firstBurstSize;
  bit16                   flag;
  agsaSSPCmdInfoUnitExt_t sspCmdIUExt;
  agsaDif_t               dif;
  agsaEncrypt_t           encrypt;
} agsaSSPInitiatorRequestExt_t;


typedef struct agsaSSPInitiatorRequestIndirect_s
{
  agsaSgl_t              agSgl;             /**< This structure is used to define either
                                                 an ESGL list or a single SGL for the SSP
                                                 command operation */
  bit32                   dataLength;
  bit16                   firstBurstSize;
  bit16                   flag;
  bit32                   sspInitiatorReqAddrUpper32; /**< The upper 32 bits of the 64-bit physical  DMA address of the SSP initiator request buffer */
  bit32                   sspInitiatorReqAddrLower32; /**< The lower 32 bits of the 64-bit physical  DMA address of the SSP initiator request buffer */
  bit32                   sspInitiatorReqLen;         /**< Specifies the length of the SSP initiator request in bytes */
  agsaDif_t               dif;
  agsaEncrypt_t           encrypt;

}agsaSSPInitiatorRequestIndirect_t;




/** \brief data structure describes a SAS SSP target read and write request
 *
 * The agsaSSPTargetRequest_t data structure describes a SAS SSP target read
 * and write request to be issued on the port. It includes the
 * length of the data to be received or sent, an offset into the
 * data block where the transfer is to start, and a list of
 * scatter-gather buffers.
 *
 * This data structure is one instance of the generic request issued
 * to saSSPStart() and is passed as an agsaSASRequestBody_t .
 *
 */
/** bit definitions for sspOption
    Bit 0-1: Transport Layer Retry setting for other phase:
    00b: No retry
    01b: Retry on ACK/NAK timeout
    10b: Retry on NAK received
    11b: Retry on both ACK/NAK timeout and NAK received
    Bit 2-3: Transport Layer Retry setting for data phase:
    00b: No retry
    01b: Retry on ACK/NAK timeout
    10b: Retry on NAK received
    11b: Retry on both ACK/NAK timeout and NAK received
    Bit 4:  Retry Data Frame. Valid only on write command. Indicates whether Target supports RTL for this particular IO.
    1b: enabled
    0b: disabled
    Bit 5: Auto good response on successful read (data transfer from target to initiator) request.
    1b: Enabled
    0b: Disabled
    Bits 6-15 : Reserved.
 */
typedef struct agsaSSPTargetRequest_s
{
  agsaSgl_t     agSgl;        /**< This structure is used to define either an ESGL list or
                                 a single SGL for the target read or write operation */
  bit32         dataLength;   /**< Specifies the amount of data to be sent in this data phase */
  bit32         offset;       /**< Specifies the offset into the overall data block
                                 where this data phase is to begin */
  bit16         agTag;        /**< Tag from ossaSSPReqReceived(). */
  bit16         sspOption;    /**< SSP option for retry */
  agsaDif_t     dif;
} agsaSSPTargetRequest_t;

#define SSP_OPTION_BITS 0x3F  /**< bit5-AGR, bit4-RDF bit3,2-RTE, bit1,0-AN */
#define SSP_OPTION_ODS 0x8000 /**< bit15-ODS */

#define SSP_OPTION_OTHR_NO_RETRY                  0
#define SSP_OPTION_OTHR_RETRY_ON_ACK_NAK_TIMEOUT  1
#define SSP_OPTION_OTHR_RETRY_ON_NAK_RECEIVED     2
#define SSP_OPTION_OTHR_RETRY_ON_BOTH_ACK_NAK_TIMEOUT_AND_NAK_RECEIVED  3

#define SSP_OPTION_DATA_NO_RETRY                   0
#define SSP_OPTION_DATA_RETRY_ON_ACK_NAK_TIMEOUT   1
#define SSP_OPTION_DATA_RETRY_ON_NAK_RECEIVED      2
#define SSP_OPTION_DATA_RETRY_ON_BOTH_ACK_NAK_TIMEOUT_AND_NAK_RECEIVED  3

#define SSP_OPTION_RETRY_DATA_FRAME_ENABLED (1 << SHIFT4)
#define SSP_OPTION_AUTO_GOOD_RESPONSE       (1 << SHIFT5)
#define SSP_OPTION_ENCRYPT                  (1 << SHIFT6)
#define SSP_OPTION_DIF                      (1 << SHIFT7)
#define SSP_OPTION_OVERRIDE_DEVICE_STATE     (1 << SHIFT15)


/** \brief data structure describes a SAS SSP target response to be issued
 *  on the port
 *
 * This data structure is one instance of the generic request issued to
 * saSSPStart() and is passed as an agsaSASRequestBody_t
 *
 */
typedef struct agsaSSPTargetResponse_s
{
  bit32       agTag;            /**< Tag from ossaSSPReqReceived(). */
  void        *frameBuf;
  bit32       respBufLength;    /**< Specifies the length of the Response buffer */
  bit32       respBufUpper;     /**< Upper 32 bit of physical address of OS Layer
                                     allocated the Response buffer
                                     (agsaSSPResponseInfoUnit_t).
                                     Valid only when respBufLength is not zero  */
  bit32       respBufLower;     /**< Lower 32 bit of physical address of OS Layer
                                     allocated the Response buffer
                                     (agsaSSPResponseInfoUnit_t).
                                     Valid only when respBufLength is not zero  */
  bit32       respOption;       /**< Bit 0-1: ACK and NAK retry option:
                                     00b: No retry
                                     01b: Retry on ACK/NAK timeout
                                     10b: Retry on NAK received
                                     11b: Retry on both ACK/NAK timeout and NAK received */
} agsaSSPTargetResponse_t;

#define RESP_OPTION_BITS 0x3    /** bit0-1 */
#define RESP_OPTION_ODS 0x8000  /** bit15 */

/** \brief data structure describes a SMP request or response frame to be sent on the SAS port
 *
 * The agsaSMPFrame_t data structure describes a SMP request or response
 * frame to be issued or sent on the SAS port.
 *
 * This data structure is one instance of the generic request issued to
 * saSMPStart() and is passed as an agsaSASRequestBody_t .
 *
 */
typedef struct agsaSMPFrame_s
{
  void                  *outFrameBuf;        /**< if payload is less than 32 bytes,A virtual
                                               frameBuf can be used. instead of physical
                                               address. Set to NULL and use physical
                                               address if payload is > 32 bytes */
  bit32                 outFrameAddrUpper32; /**< The upper 32 bits of the 64-bit physical
                                               DMA address of the SMP frame buffer */
  bit32                 outFrameAddrLower32; /**< The lower 32 bits of the 64-bit physical
                                               DMA address of the SMP frame buffer */
  bit32                 outFrameLen;         /**< Specifies the length of the SMP request
                                               frame excluding the CRC field in bytes */
  bit32                 inFrameAddrUpper32;  /**< The upper 32 bits of the 64-bit phsical address
                                               of DMA address of response SMP Frame buffer */
  bit32                 inFrameAddrLower32;  /**< The lower 32 bits of the 64-bit phsical address
                                               of DMA address of response SMP Frame buffer */
  bit32                 inFrameLen;          /**< Specifies the length of the SMP response
                                               frame excluding the CRC field in bytes */
  bit32                 expectedRespLen;     /**< Specifies the length of SMP Response */
  bit32                 flag;                /** For the SPCv controller:
                                                 Bit 0: Indirect Response (IR). This indicates
                                                        direct or indirect mode for SMP response frame
                                                        to be received.
                                                    0b: Direct mode
                                                    1b: Indirect mode

                                                 Bit 1: Indirect Payload (IP). This indicates
                                                        direct or indirect mode for SMP request frame
                                                        to be sent.
                                                    0b: Direct mode
                                                    1b: Indirect mode

                                                 Bits 2-31: Reserved
                                                For the SPC controller: This is not applicable.
                                                */

} agsaSMPFrame_t;

#define smpFrameFlagDirectResponse   0
#define smpFrameFlagIndirectResponse 1
#define smpFrameFlagDirectPayload    0
#define smpFrameFlagIndirectPayload  2

/** \brief union data structure specifies a request
 *
 * union data structure specifies a request
 */
typedef union agsaSASRequestBody_u
{
  agsaSSPInitiatorRequest_t                 sspInitiatorReq;  /**< Structure containing the SSP initiator request, Support up to 16 bytes CDB */
  agsaSSPInitiatorRequestExt_t           sspInitiatorReqExt;  /**< Structure containing the SSP initiator request for CDB > 16 bytes */
  agsaSSPInitiatorRequestIndirect_t sspInitiatorReqIndirect;  /**< Structure containing the SSP indirect initiator request */
  agsaSSPTargetRequest_t                       sspTargetReq;  /**< Structure containing the SSP Target request */
  agsaSSPScsiTaskMgntReq_t                   sspTaskMgntReq;  /**< Structure containing the SSP SCSI Task Management request */
  agsaSSPTargetResponse_t                 sspTargetResponse;  /**< Structure containing the SSP Target response. */
  agsaSMPFrame_t                                   smpFrame;  /**< Structure containing SMP request or response frame */
}agsaSASRequestBody_t;




/** \brief data structure describes an STP or direct connect SATA command
 *
 * The agsaSATAInitiatorRequest_t data structure describes an STP or direct
 * connect SATA command request to be sent to the device and
 * passed as a parameter to saSATAStart() function.
 *
 * This structure is an encapsulation of SATA FIS (Frame Information
 * Structures), which enables the execution of ATA command
 * descriptor using SATA transport
 *
 */
typedef struct agsaSATAInitiatorRequest_s
{
  agsaSgl_t         agSgl;      /**< This structure is used to define either an ESGL
                                     list or a single SGL for operation that involves
                                     DMA transfer */

  bit32             dataLength; /**< Total data length in bytes */

  bit32             option;     /**< Operational option, defined using the bit field.
                                     b7-1: reserved
                                     b0:   AGSA-STP-CLOSE-CLEAR-AFFILIATION */

  agsaSATAHostFis_t fis;        /**< The FIS request */
  agsaDif_t         dif;
  agsaEncrypt_t     encrypt;
  bit8              scsiCDB[16];
#ifdef SA_TESTBASE_EXTRA
  /* Added by TestBase */
  bit16             bstIndex;
#endif /*  SA_TESTBASE_EXTRA */
} agsaSATAInitiatorRequest_t;


/* controller Configuration page */
#define AGSA_SAS_PROTOCOL_TIMER_CONFIG_PAGE   0x04
#define AGSA_INTERRUPT_CONFIGURATION_PAGE     0x05
#define AGSA_IO_GENERAL_CONFIG_PAGE           0x06 
#define AGSA_ENCRYPTION_GENERAL_CONFIG_PAGE   0x20
#define AGSA_ENCRYPTION_DEK_CONFIG_PAGE       0x21
#define AGSA_ENCRYPTION_CONTROL_PARM_PAGE     0x22
#define AGSA_ENCRYPTION_HMAC_CONFIG_PAGE      0x23

#ifdef HIALEAH_ENCRYPTION
typedef struct agsaEncryptGeneralPage_s {
  bit32             numberOfKeksPageCode;           /* 0x20 */
  bit32             KeyCardIdKekIndex;
  bit32             KeyCardId3_0;
  bit32             KeyCardId7_4;
  bit32             KeyCardId11_8;
} agsaEncryptGeneralPage_t;
#else
typedef struct agsaEncryptGeneralPage_s {
  bit32             pageCode;           /* 0x20 */
  bit32             numberOfDeks;
} agsaEncryptGeneralPage_t;
#endif /* HIALEAH_ENCRYPTION */

#define AGSA_ENC_CONFIG_PAGE_KEK_NUMBER 0x0000FF00
#define AGSA_ENC_CONFIG_PAGE_KEK_SHIFT  8

/* sTSDK 4.14   */
typedef struct agsaEncryptDekConfigPage_s {
  bit32             pageCode;
  bit32             table0AddrLo;
  bit32             table0AddrHi;
  bit32             table0Entries;
  bit32             table0BFES;
  bit32             table1AddrLo;
  bit32             table1AddrHi;
  bit32             table1Entries;
  bit32             table1BFES;
} agsaEncryptDekConfigPage_t;

#define AGSA_ENC_DEK_CONFIG_PAGE_DEK_TABLE_NUMBER 0xF0000000
#define AGSA_ENC_DEK_CONFIG_PAGE_DEK_TABLE_SHIFT SHIFT28
#define AGSA_ENC_DEK_CONFIG_PAGE_DEK_CACHE_WAY    0x0F000000
#define AGSA_ENC_DEK_CONFIG_PAGE_DEK_CACHE_SHIFT SHIFT24

/*sTSDK 4.18   */
/* CCS (Current Crypto Services)  and NOPR (Number of Operators) are valid only in GET_CONTROLLER_CONFIG */
/* NAR, CORCAP and USRCAP are valid only when AUT==1 */
typedef struct agsaEncryptControlParamPage_s {
  bit32          pageCode;           /* 0x22 */
  bit32          CORCAP;             /* Crypto Officer Role Capabilities */
  bit32          USRCAP;             /* User Role Capabilities */
  bit32          CCS;                /* Current Crypto Services */
  bit32          NOPR;               /* Number of Operators */
} agsaEncryptControlParamPage_t;

typedef struct agsaEncryptInfo_s {
  bit32          encryptionCipherMode;
  bit32          encryptionSecurityMode;
  bit32          status;
  bit32          flag;
} agsaEncryptInfo_t;


#define OperatorAuthenticationEnable_AUT 1
#define ReturnToFactoryMode_ARF          2

/*sTSDK 4.19   */
typedef struct agsaEncryptSelfTestBitMap_s {
	bit32		AES_Test;
	bit32		KEY_WRAP_Test;
	bit32		HMAC_Test;
} agsaEncryptSelfTestBitMap_t;

typedef struct  agsaEncryptSelfTestStatusBitMap_s{
	bit32		AES_Status;
	bit32		KEY_WRAP_Status;
	bit32		HMAC_Status;
} agsaEncryptSelfTestStatusBitMap_t;

typedef struct agsaEncryptHMACTestDescriptor_s
{
  bit32   Dword0;
  bit32   MsgAddrLo;
  bit32   MsgAddrHi;
  bit32   MsgLen;
  bit32   DigestAddrLo;
  bit32   DigestAddrHi;
  bit32   KeyAddrLo;
  bit32   KeyAddrHi;
  bit32   KeyLen;
} agsaEncryptHMACTestDescriptor_t;

typedef struct agsaEncryptHMACTestResult_s
{
  bit32   Dword0;
  bit32   Dword[12];
} agsaEncryptHMACTestResult_t;

typedef struct agsaEncryptSHATestDescriptor_s
{
  bit32   Dword0;
  bit32   MsgAddrLo;
  bit32   MsgAddrHi;
  bit32   MsgLen;
  bit32   DigestAddrLo;
  bit32   DigestAddrHi;
} agsaEncryptSHATestDescriptor_t;

typedef struct agsaEncryptSHATestResult_s
{
  bit32   Dword0;
  bit32   Dword[12];
} agsaEncryptSHATestResult_t;

/* types of self test */
#define AGSA_BIST_TEST      0x1
#define AGSA_HMAC_TEST      0x2
#define AGSA_SHA_TEST       0x3


/*sTSDK  4.13  */
typedef struct agsaEncryptDekBlob_s {
    bit8           dekBlob[80];
} agsaEncryptDekBlob_t;

typedef struct agsaEncryptKekBlob_s {
    bit8           kekBlob[48];
} agsaEncryptKekBlob_t;

/*sTSDK  4.45  */
typedef struct agsaEncryptHMACConfigPage_s
{
  bit32  PageCode;
  bit32  CustomerTag;
  bit32  KeyAddrLo;
  bit32  KeyAddrHi;
} agsaEncryptHMACConfigPage_t;

/*sTSDK  4.38  */
#define AGSA_ID_SIZE 31
typedef struct agsaID_s {
   bit8   ID[AGSA_ID_SIZE];
}agsaID_t;


#define SA_OPR_MGMNT_FLAG_MASK  0x00003000
#define SA_OPR_MGMNT_FLAG_SHIFT 12

/* */
typedef struct agsaSASPhyMiscPage_s {
  bit32 Dword0;
  bit32 Dword1;
} agsaSASPhyMiscPage_t ;


typedef struct agsaHWEventEncrypt_s {
    bit32          encryptOperation;
    bit32          status;
    bit32          eq; /* error qualifier */
    bit32          info;
    void           *handle;
    void           *param;
} agsaHWEventEncrypt_t;

/*sTSDK  4.32  */
typedef struct agsaHWEventMode_s {
    bit32          modePageOperation;
    bit32          status;
    bit32          modePageLen;
    void           *modePage;
    void           *context;
} agsaHWEventMode_t;

/*sTSDK  4.33  */
typedef struct agsaInterruptConfigPage_s {
  bit32 pageCode;
  bit32 vectorMask0;
  bit32 vectorMask1;
  bit32 ICTC0;
  bit32 ICTC1;
  bit32 ICTC2;
  bit32 ICTC3;
  bit32 ICTC4;
  bit32 ICTC5;
  bit32 ICTC6;
  bit32 ICTC7;
} agsaInterruptConfigPage_t;
typedef struct agsaIoGeneralPage_s {
  bit32 pageCode;           /* 0x06 */
  bit32 ActiveMask;
  bit32 QrntTime;
} agsaIoGeneralPage_t;

/* \brief data structure defines detail information about Agilent Error
* Detection Code (DIF) errors.
*
* The  agsaDifDetails_t data structure defines detail information about
* PMC Error Detection Code (DIF) error.  Please refer to the latest T10 SBC
* and SPC draft/specification for the definition of the Protection
* Information.
*
* This structure is filled by the function saGetDifErrorDetails().
*/

typedef struct agsaDifDetails_s {
    bit32               UpperLBA;
    bit32               LowerLBA;
    bit8                sasAddressHi[4];
    bit8                sasAddressLo[4];
    bit32               ExpectedCRCUDT01;
    bit32               ExpectedUDT2345;
    bit32               ActualCRCUDT01;
    bit32               ActualUDT2345;
    bit32               DIFErrDevID;
    bit32               ErrBoffsetEDataLen;
    void * frame;
} agsaDifDetails_t;

/** \brief data structure for SAS protocol timer configuration page.
 *
 */
typedef struct  agsaSASProtocolTimerConfigurationPage_s{
  bit32 pageCode;                        /* 0 */
  bit32 MST_MSI;                         /* 1 */
  bit32 STP_SSP_MCT_TMO;                 /* 2 */
  bit32 STP_FRM_TMO;                     /* 3 */
  bit32 STP_IDLE_TMO;                    /* 4 */
  bit32 OPNRJT_RTRY_INTVL;               /* 5 */
  bit32 Data_Cmd_OPNRJT_RTRY_TMO;        /* 6 */
  bit32 Data_Cmd_OPNRJT_RTRY_THR;        /* 7 */
  bit32 MAX_AIP;                         /* 8 */
} agsaSASProtocolTimerConfigurationPage_t;


/** \brief data structure for firmware flash update saFwFlashUpdate().
 *
 * The agsaUpdateFwFlash data structure specifies a request to saFwFlashUpdate()
 */
typedef struct agsaUpdateFwFlash_s
{
  bit32     currentImageOffset;
  bit32     currentImageLen;
  bit32     totalImageLen;
  agsaSgl_t agSgl;
} agsaUpdateFwFlash_t;



/** \brief data structure for extended firmware flash update saFwFlashExtUpdate().
 *
 * The agsaFlashExtExecute_s data structure specifies a request to saFwFlashExtUpdate()
 */
typedef struct agsaFlashExtExecute_s
{
  bit32     command;
  bit32     partOffset;
  bit32     dataLen;
  agsaSgl_t *agSgl;
} agsaFlashExtExecute_t;


/** \brief data structure for firmware flash update saFwFlashUpdate().
 *
 * The agsaFlashExtResponse_t data structure specifies a request to ossaFlashExtExecuteCB().()
 */
typedef struct agsaFlashExtResponse_s
{
  bit32     epart_size;
  bit32     epart_sect_size;
} agsaFlashExtResponse_t;


/** \brief data structure for set fields in MPI table.
 *  The agsaMPIContext_t data structure is used to set fields in MPI table.
 *  For details of MPI table, refer to PM8001 Tachyon SPC 8x6G Programmers'
 *  Manual PMC-2080222 or PM8008/PM8009/PM8018 Tachyon SPCv/SPCve/SPCv+ Programmers Manual
 *  PMC-2091148/PMC-2102373.
    sTSDK  section 4.39
 */

typedef struct agsaMPIContext_s
{
  bit32   MPITableType;
  bit32   offset;
  bit32   value;
} agsaMPIContext_t;

#define AGSA_MPI_MAIN_CONFIGURATION_TABLE             1
#define AGSA_MPI_GENERAL_STATUS_TABLE                 2
#define AGSA_MPI_INBOUND_QUEUE_CONFIGURATION_TABLE    3
#define AGSA_MPI_OUTBOUND_QUEUE_CONFIGURATION_TABLE   4
#define AGSA_MPI_SAS_PHY_ANALOG_SETUP_TABLE           5
#define AGSA_MPI_INTERRUPT_VECTOR_TABLE               6
#define AGSA_MPI_PER_SAS_PHY_ATTRIBUTE_TABLE          7
#define AGSA_MPI_OUTBOUND_QUEUE_FAILOVER_TABLE        8


/************************************************************/
/*This flag and datastructure are specific for fw profiling, Now defined as compiler flag*/
//#define SPC_ENABLE_PROFILE

#ifdef SPC_ENABLE_PROFILE
typedef struct agsaFwProfile_s
{
  bit32     tcid;
  bit32     processor;
  bit32     cmd;
  bit32     len;
  bit32     codeStartAdd;
  bit32     codeEndAdd;
  agsaSgl_t agSgl;
} agsaFwProfile_t;
#endif
/************************************************************/
/** \brief Callback definition for .ossaDeviceRegistration
 *
 */
typedef  void (*ossaDeviceRegistrationCB_t)(
  agsaRoot_t          *agRoot,
  agsaContext_t       *agContext,
  bit32               status,
  agsaDevHandle_t     *agDevHandle,
  bit32               deviceID
  );

/** \brief Callback definition for
 *
 */
typedef void (*ossaDeregisterDeviceHandleCB_t)(
  agsaRoot_t          *agRoot,
  agsaContext_t       *agContext,
  agsaDevHandle_t     *agDevHandle,
  bit32               status
  );

/** \brief Callback definition for
 *
 */
typedef void (*ossaGenericCB_t)(void);


/** \brief Callback definition for abort SMP SSP SATA callback
 *
 */
typedef void (*ossaGenericAbortCB_t)(
  agsaRoot_t        *agRoot,
  agsaIORequest_t   *agIORequest,
  bit32             flag,
  bit32             status
  );


typedef void (*ossaLocalPhyControlCB_t)(
  agsaRoot_t      *agRoot,
  agsaContext_t   *agContext,
  bit32           phyId,
  bit32           phyOperation,
  bit32           status,
  void            *parm
  );


/** \brief Callback definition for
 *
 */
typedef void (*ossaSATACompletedCB_t)(
  agsaRoot_t          *agRoot,
  agsaIORequest_t     *agIORequest,
  bit32               agIOStatus,
  void                *agFirstDword,
  bit32               agIOInfoLen,
  void                *agParam
  );


/** \brief Callback definition for
 *
 */
typedef void (*ossaSMPCompletedCB_t)(
  agsaRoot_t            *agRoot,
  agsaIORequest_t       *agIORequest,
  bit32                 agIOStatus,
  bit32                 agIOInfoLen,
  agsaFrameHandle_t     agFrameHandle
  );


/** \brief Callback definition for
 *
 */
typedef  void (*ossaSSPCompletedCB_t)(
  agsaRoot_t            *agRoot,
  agsaIORequest_t       *agIORequest,
  bit32                 agIOStatus,
  bit32                 agIOInfoLen,
  void                  *agParam,
  bit16                 sspTag,
  bit32                 agOtherInfo
  );

/** \brief Callback definition for
 *
 */
typedef void (*ossaSetDeviceInfoCB_t) (
                                agsaRoot_t        *agRoot,
                                agsaContext_t     *agContext,
                                agsaDevHandle_t   *agDevHandle,
                                bit32             status,
                                bit32             option,
                                bit32             param
                                );

typedef struct agsaOffloadDifDetails_s
{
  bit32 ExpectedCRCUDT01;
  bit32 ExpectedUDT2345;
  bit32 ActualCRCUDT01;
  bit32 ActualUDT2345;
  bit32 DIFErr;
  bit32 ErrBoffset;
} agsaOffloadDifDetails_t;

typedef struct agsaDifEncPayload_s
{
  agsaSgl_t      SrcSgl;
  bit32          SrcDL;
  agsaSgl_t      DstSgl;
  bit32          DstDL;
  agsaDif_t      dif;
  agsaEncrypt_t  encrypt;
} agsaDifEncPayload_t;

typedef void (*ossaVhistCaptureCB_t) (
        agsaRoot_t    *agRoot,
        agsaContext_t *agContext,
        bit32         status,
        bit32         len);

typedef void (*ossaDIFEncryptionOffloadStartCB_t) (
  agsaRoot_t                *agRoot,
  agsaContext_t             *agContext,
  bit32                     status,
  agsaOffloadDifDetails_t   *agsaOffloadDifDetails
  );

#define SA_RESERVED_REQUEST_COUNT 16

#ifdef SA_FW_TIMER_READS_STATUS
#define SA_FW_TIMER_READS_STATUS_INTERVAL 20
#endif /* SA_FW_TIMER_READS_STATUS */

#define SIZE_DW                         4     /**< Size in bytes */
#define SIZE_QW                         8     /**< Size in bytes */

#define PCIBAR0                         0     /**< PCI Base Address 0 */
#define PCIBAR1                         1     /**< PCI Base Address 1 */
#define PCIBAR2                         2     /**< PCI Base Address 2 */
#define PCIBAR3                         3     /**< PCI Base Address 3 */
#define PCIBAR4                         4     /**< PCI Base Address 4 */
#define PCIBAR5                         5     /**< PCI Base Address 5 */

/** \brief describe an element of SPC-SPCV converter
 *
 * This structure is used
 *
 */
typedef struct agsaBarOffset_s
{
  bit32 Generic;    /* */
  bit32 Bar;        /* */
  bit32 Offset;     /* */
  bit32 Length;     /* */
} agsaBarOffset_t;

typedef union agsabit32bit64_U
{
  bit32 S32[2];
  bit64 B64;
} agsabit32bit64;

/*
The agsaIOErrorEventStats_t data structure is used as parameter in ossaGetIOErrorStatsCB(),ossaGetIOEventStatsCB().
This data structure contains the number of IO error and event.
*/
typedef struct agsaIOErrorEventStats_s
{
   bit32  agOSSA_IO_COMPLETED_ERROR_SCSI_STATUS;
   bit32  agOSSA_IO_ABORTED;
   bit32  agOSSA_IO_OVERFLOW;
   bit32  agOSSA_IO_UNDERFLOW;
   bit32  agOSSA_IO_FAILED;
   bit32  agOSSA_IO_ABORT_RESET;
   bit32  agOSSA_IO_NOT_VALID;
   bit32  agOSSA_IO_NO_DEVICE;
   bit32  agOSSA_IO_ILLEGAL_PARAMETER;
   bit32  agOSSA_IO_LINK_FAILURE;
   bit32  agOSSA_IO_PROG_ERROR;
   bit32  agOSSA_IO_DIF_IN_ERROR;
   bit32  agOSSA_IO_DIF_OUT_ERROR;
   bit32  agOSSA_IO_ERROR_HW_TIMEOUT;
   bit32  agOSSA_IO_XFER_ERROR_BREAK;
   bit32  agOSSA_IO_XFER_ERROR_PHY_NOT_READY;
   bit32  agOSSA_IO_OPEN_CNX_ERROR_PROTOCOL_NOT_SUPPORTED;
   bit32  agOSSA_IO_OPEN_CNX_ERROR_ZONE_VIOLATION;
   bit32  agOSSA_IO_OPEN_CNX_ERROR_BREAK;
   bit32  agOSSA_IO_OPEN_CNX_ERROR_IT_NEXUS_LOSS;
   bit32  agOSSA_IO_OPEN_CNX_ERROR_BAD_DESTINATION;
   bit32  agOSSA_IO_OPEN_CNX_ERROR_CONNECTION_RATE_NOT_SUPPORTED;
   bit32  agOSSA_IO_OPEN_CNX_ERROR_STP_RESOURCES_BUSY;
   bit32  agOSSA_IO_OPEN_CNX_ERROR_WRONG_DESTINATION;
   bit32  agOSSA_IO_OPEN_CNX_ERROR_UNKNOWN_ERROR;
   bit32  agOSSA_IO_XFER_ERROR_NAK_RECEIVED;
   bit32  agOSSA_IO_XFER_ERROR_ACK_NAK_TIMEOUT;
   bit32  agOSSA_IO_XFER_ERROR_PEER_ABORTED;
   bit32  agOSSA_IO_XFER_ERROR_RX_FRAME;
   bit32  agOSSA_IO_XFER_ERROR_DMA;
   bit32  agOSSA_IO_XFER_ERROR_CREDIT_TIMEOUT;
   bit32  agOSSA_IO_XFER_ERROR_SATA_LINK_TIMEOUT;
   bit32  agOSSA_IO_XFER_ERROR_SATA;
   bit32  agOSSA_IO_XFER_ERROR_ABORTED_DUE_TO_SRST;
   bit32  agOSSA_IO_XFER_ERROR_REJECTED_NCQ_MODE;
   bit32  agOSSA_IO_XFER_ERROR_ABORTED_NCQ_MODE;
   bit32  agOSSA_IO_XFER_OPEN_RETRY_TIMEOUT;
   bit32  agOSSA_IO_XFER_SMP_RESP_CONNECTION_ERROR;
   bit32  agOSSA_IO_XFER_ERROR_UNEXPECTED_PHASE;
   bit32  agOSSA_IO_XFER_ERROR_XFER_RDY_OVERRUN;
   bit32  agOSSA_IO_XFER_ERROR_XFER_RDY_NOT_EXPECTED;
   bit32  agOSSA_IO_XFER_ERROR_CMD_ISSUE_ACK_NAK_TIMEOUT;
   bit32  agOSSA_IO_XFER_ERROR_CMD_ISSUE_BREAK_BEFORE_ACK_NAK;
   bit32  agOSSA_IO_XFER_ERROR_CMD_ISSUE_PHY_DOWN_BEFORE_ACK_NAK;
   bit32  agOSSA_IO_XFER_ERROR_OFFSET_MISMATCH;
   bit32  agOSSA_IO_XFER_ERROR_XFER_ZERO_DATA_LEN;
   bit32  agOSSA_IO_XFER_CMD_FRAME_ISSUED;
   bit32  agOSSA_IO_ERROR_INTERNAL_SMP_RESOURCE;
   bit32  agOSSA_IO_PORT_IN_RESET;
   bit32  agOSSA_IO_DS_NON_OPERATIONAL;
   bit32  agOSSA_IO_DS_IN_RECOVERY;
   bit32  agOSSA_IO_TM_TAG_NOT_FOUND;
   bit32  agOSSA_IO_XFER_PIO_SETUP_ERROR;
   bit32  agOSSA_IO_SSP_EXT_IU_ZERO_LEN_ERROR;
   bit32  agOSSA_IO_DS_IN_ERROR;
   bit32  agOSSA_IO_OPEN_CNX_ERROR_HW_RESOURCE_BUSY;
   bit32  agOSSA_IO_ABORT_IN_PROGRESS;
   bit32  agOSSA_IO_ABORT_DELAYED;
   bit32  agOSSA_IO_INVALID_LENGTH;
   bit32  agOSSA_IO_OPEN_CNX_ERROR_HW_RESOURCE_BUSY_ALT;
   bit32  agOSSA_IO_OPEN_CNX_ERROR_IT_NEXUS_LOSS_OPEN_RETRY_BACKOFF_THRESHOLD_REACHED;
   bit32  agOSSA_IO_OPEN_CNX_ERROR_IT_NEXUS_LOSS_OPEN_TMO;
   bit32  agOSSA_IO_OPEN_CNX_ERROR_IT_NEXUS_LOSS_NO_DEST;
   bit32  agOSSA_IO_OPEN_CNX_ERROR_IT_NEXUS_LOSS_OPEN_COLLIDE;
   bit32  agOSSA_IO_OPEN_CNX_ERROR_IT_NEXUS_LOSS_PATHWAY_BLOCKED;
   bit32  agOSSA_IO_DS_INVALID;
   bit32  agOSSA_IO_XFER_READ_COMPL_ERR;
   bit32  agOSSA_IO_XFER_ERR_LAST_PIO_DATAIN_CRC_ERR;
   bit32  agOSSA_IO_XFR_ERROR_INTERNAL_CRC_ERROR;
   bit32  agOSSA_MPI_IO_RQE_BUSY_FULL;
   bit32  agOSSA_MPI_ERR_IO_RESOURCE_UNAVAILABLE;
   bit32  agOSSA_MPI_ERR_ATAPI_DEVICE_BUSY;
   bit32  agOSSA_IO_XFR_ERROR_DEK_KEY_CACHE_MISS;
   bit32  agOSSA_IO_XFR_ERROR_DEK_KEY_TAG_MISMATCH;
   bit32  agOSSA_IO_XFR_ERROR_CIPHER_MODE_INVALID;
   bit32  agOSSA_IO_XFR_ERROR_DEK_IV_MISMATCH;
   bit32  agOSSA_IO_XFR_ERROR_DEK_RAM_INTERFACE_ERROR;
   bit32  agOSSA_IO_XFR_ERROR_INTERNAL_RAM;
   bit32  agOSSA_IO_XFR_ERROR_DIF_MISMATCH;
   bit32  agOSSA_IO_XFR_ERROR_DIF_APPLICATION_TAG_MISMATCH;
   bit32  agOSSA_IO_XFR_ERROR_DIF_REFERENCE_TAG_MISMATCH;
   bit32  agOSSA_IO_XFR_ERROR_DIF_CRC_MISMATCH;
   bit32  agOSSA_IO_XFR_ERROR_INVALID_SSP_RSP_FRAME;
   bit32  agOSSA_IO_XFER_ERR_EOB_DATA_OVERRUN;
   bit32  agOSSA_IO_XFR_ERROR_DEK_INDEX_OUT_OF_BOUNDS;
   bit32  agOSSA_IO_OPEN_CNX_ERROR_OPEN_PREEMPTED;
   bit32  agOSSA_IO_XFR_ERROR_DEK_ILLEGAL_TABLE;
   bit32  agOSSA_IO_XFER_ERROR_DIF_INTERNAL_ERROR;
   bit32  agOSSA_MPI_ERR_OFFLOAD_DIF_OR_ENC_NOT_ENABLED;
   bit32  agOSSA_IO_XFER_ERROR_DMA_ACTIVATE_TIMEOUT;
   bit32  agOSSA_IO_UNKNOWN_ERROR;
} agsaIOErrorEventStats_t;


/************************************************************************************
 *                                                                                  *
 *               Data Structures Defined for LL API ends                            *
 *                                                                                  *
 ************************************************************************************/
#ifdef SALL_API_TEST
typedef struct agsaIOCountInfo_s
{
  bit32 numSSPStarted;    // saSSPStart()
  bit32 numSSPAborted;    // saSSPAbort()
  bit32 numSSPAbortedCB;  // ossaSSPAbortCB()
  bit32 numSSPCompleted;  // includes success and aborted IOs
  bit32 numSMPStarted;    // saSMPStart()
  bit32 numSMPAborted;    // saSMPAbort()
  bit32 numSMPAbortedCB;  // ossaSMPAbortCB()
  bit32 numSMPCompleted;  // includes success and aborted IOs
  bit32 numSataStarted;   // saSATAStart()
  bit32 numSataAborted;   // saSATAAbort()
  bit32 numSataAbortedCB; // ossaSATAAbortCB()
  bit32 numSataCompleted; // includes success and aborted IOs
  bit32 numEchoSent;      // saEchoCommand()
  bit32 numEchoCB;        // ossaEchoCB()
  bit32 numUNKNWRespIOMB; // unknow Response IOMB received
  bit32 numOurIntCount;   //InterruptHandler() counter
  bit32 numSpuriousInt;   //spurious interrupts
//  bit32 numSpInts[64];    //spuriours interrupts count for each OBQ (PI=CI)
//  bit32 numSpInts1[64];   //spuriours interrupts count for each OBQ (PI!=CI)
} agsaIOCountInfo_t;

/* Total IO Counter */
#define LL_COUNTERS 17
/* Counter Bit Map */
#define COUNTER_SSP_START       0x000001
#define COUNTER_SSP_ABORT       0x000002
#define COUNTER_SSPABORT_CB     0x000004
#define COUNTER_SSP_COMPLETEED  0x000008
#define COUNTER_SMP_START       0x000010
#define COUNTER_SMP_ABORT       0x000020
#define COUNTER_SMPABORT_CB     0x000040
#define COUNTER_SMP_COMPLETEED  0x000080
#define COUNTER_SATA_START      0x000100
#define COUNTER_SATA_ABORT      0x000200
#define COUNTER_SATAABORT_CB    0x000400
#define COUNTER_SATA_COMPLETEED 0x000800
#define COUNTER_ECHO_SENT       0x001000
#define COUNTER_ECHO_CB         0x002000
#define COUNTER_UNKWN_IOMB      0x004000
#define COUNTER_OUR_INT         0x008000
#define COUNTER_SPUR_INT        0x010000
#define ALL_COUNTERS            0xFFFFFF

typedef union agsaLLCountInfo_s
{
  agsaIOCountInfo_t IOCounter;
  bit32 arrayIOCounter[LL_COUNTERS];
} agsaLLCountInfo_t;

#endif /* SALL_API_TEST */

#define MAX_IO_DEVICE_ENTRIES  4096            /**< Maximum Device Entries */


#ifdef SA_ENABLE_POISION_TLP
#define SA_PTNFE_POISION_TLP 1 /* Enable if one  */
#else /* SA_ENABLE_POISION_TLP */
#define SA_PTNFE_POISION_TLP 0 /* Disable if zero default setting */
#endif /* SA_ENABLE_POISION_TLP */

#ifdef SA_DISABLE_MDFD
#define SA_MDFD_MULTI_DATA_FETCH 1 /* Disable if one  */
#else /* SA_DISABLE_MDFD */
#define SA_MDFD_MULTI_DATA_FETCH 0 /* Enable if zero default setting */
#endif /* SA_DISABLE_MDFD */

#ifdef SA_ENABLE_ARBTE
#define SA_ARBTE 1  /* Enable if one  */
#else /* SA_ENABLE_ARBTE */
#define SA_ARBTE 0  /* Disable if zero default setting */
#endif /* SA_ENABLE_ARBTE */

#ifdef SA_DISABLE_OB_COAL
#define SA_OUTBOUND_COALESCE 0 /* Disable if zero */
#else /* SA_DISABLE_OB_COAL */
#define SA_OUTBOUND_COALESCE 1 /* Enable if one default setting */
#endif /* SA_DISABLE_OB_COAL */


/***********************************************************************************
 *                                                                                 *
 *              The OS Layer Functions Declarations start                          *
 *                                                                                 *
 ***********************************************************************************/
#include "saosapi.h"
/***********************************************************************************
 *                                                                                 *
 *              The OS Layer Functions Declarations end                            *
 *                                                                                 *
 ***********************************************************************************/

/***********************************************************************************
 *                                                                                 *
 *              The LL Layer Functions Declarations start                          *
 *                                                                                 *
 ***********************************************************************************/

#ifdef FAST_IO_TEST
/* needs to be allocated by the xPrepare() caller, one struct per IO */
typedef struct agsaFastCBBuf_s
{
  void  *cb;
  void  *cbArg;
  void  *pSenseData;
  bit8  *senseLen;
  /* internal */
  void  *oneDeviceData; /* tdsaDeviceData_t */
} agsaFastCBBuf_t;

typedef struct agsaFastCommand_s
{
  /* in */
  void        *agRoot;
  /* modified by TD tiFastPrepare() */
  void        *devHandle;    /* agsaDevHandle_t* */
  void        *agSgl;        /* agsaSgl_t* */
  bit32       dataLength;
  bit32       extDataLength;
  bit8        additionalCdbLen;
  bit8        *cdb;
  bit8        *lun;
  /* modified by TD tiFastPrepare() */
  bit8        taskAttribute; /* TD_xxx */
  bit16       flag;          /* TLR_MASK */
  bit32       agRequestType;
  bit32       queueNum;
  agsaFastCBBuf_t *safb;
} agsaFastCommand_t;
#endif



/* Enable test by setting bits in gFPGA_TEST */

#define  EnableFPGA_TEST_ICCcontrol            0x01
#define  EnableFPGA_TEST_ReadDEV               0x02
#define  EnableFPGA_TEST_WriteCALAll           0x04
#define  EnableFPGA_TEST_ReconfigSASParams     0x08
#define  EnableFPGA_TEST_LocalPhyControl       0x10
#define  EnableFPGA_TEST_PortControl           0x20


/*
PM8001/PM8008/PM8009/PM8018 sTSDK Low-Level Architecture Specification
SDK2
3.3 Encryption Status Definitions
Encryption engine generated errors.
Table 7 Encryption Engine Generated Errors
Error Definition
*/

/*
PM 1.01
section 4.26.12.6 Encryption Errors
Table 51 lists initialization errors related to encryption functionality. For information on errors reported
for inbound IOMB commands, refer to the corresponding outbound response sections. The error codes
listed in Table 51 are reported in the Scratchpad 3 Register.
*/
#define OSSA_ENCRYPT_ENGINE_FAILURE_MASK        0x00FF0000    /* Encrypt Engine failed the BIST Test */
#define OSSA_ENCRYPT_SEEPROM_NOT_FOUND          0x01  /* SEEPROM is not installed. This condition is reported based on the bootstrap pin setting. */
#define OSSA_ENCRYPT_SEEPROM_IPW_RD_ACCESS_TMO  0x02  /* SEEPROM access timeout detected while reading initialization password or Allowable Cipher Modes. */
#define OSSA_ENCRYPT_SEEPROM_IPW_RD_CRC_ERR     0x03  /* CRC Error detected when reading initialization password or Allowable Cipher Modes.  */
#define OSSA_ENCRYPT_SEEPROM_IPW_INVALID        0x04  /* Initialization password read from SEEPROM doesn't match any valid password value. This could also mean SEEPROM is blank.  */
#define OSSA_ENCRYPT_SEEPROM_WR_ACCESS_TMO      0x05  /* access timeout detected while writing initialization password or Allowable Cipher Modes.  */
#define OSSA_ENCRYPT_FLASH_ACCESS_TMO           0x20  /* Timeout while reading flash memory. */
#define OSSA_ENCRYPT_FLASH_SECTOR_ERASE_TMO     0x21  /* Flash sector erase timeout while writing to flash memory. */
#define OSSA_ENCRYPT_FLASH_SECTOR_ERASE_ERR     0x22  /* Flash sector erase failure while writing to flash memory. */
#define OSSA_ENCRYPT_FLASH_ECC_CHECK_ERR        0x23  /* Flash ECC check failure. */
#define OSSA_ENCRYPT_FLASH_NOT_INSTALLED        0x24  /* Flash memory not installed, this error is only detected in Security Mode B.  */
#define OSSA_ENCRYPT_INITIAL_KEK_NOT_FOUND      0x40  /* Initial KEK is not found in the flash memory. This error is only detected in Security Mode B. */
#define OSSA_ENCRYPT_AES_BIST_ERR               0x41  /* Built-In Test Failure */
#define OSSA_ENCRYPT_KWP_BIST_FAILURE           0x42  /* Built-In Test Failed on Key Wrap Engine */

/* 0x01:ENC_ERR_SEEPROM_NOT_INSTALLED */
/* 0x02:ENC_ERR_SEEPROM_IPW_RD_ACCESS_TMO */
/* 0x03:ENC_ERR_SEEPROM_IPW_RD_CRC_ERR */
/* 0x04:ENC_ERR_SEEPROM_IPW_INVALID */
/* 0x05:ENC_ERR_SEEPROM_WR_ACCESS_TMO */
/* 0x20:ENC_ERR_FLASH_ACCESS_TMO */
/* 0x21:ENC_ERR_FLASH_SECTOR_ERASE_TMO */
/* 0x22:ENC_ERR_FLASH_SECTOR_ERASE_FAILURE */
/* 0x23:ENC_ERR_FLASH_ECC_CHECK_FAILURE */
/* 0x24:ENC_ERR_FLASH_NOT_INSTALLED */
/* 0x40:ENC_ERR_INITIAL_KEK_NOT_FOUND */
/* 0x41:ENC_ERR_AES_BIST_FAILURE */
/* 0x42:ENC_ERR_KWP_BIST_FAILURE */

/*
This field indicates self test failure in DIF engine bits [27:24].
*/

#define OSSA_DIF_ENGINE_FAILURE_MASK        0x0F000000    /* DIF Engine failed the BIST Test */

#define OSSA_DIF_ENGINE_0_BIST_FAILURE           0x1  /* DIF Engine 0 failed the BIST Test */
#define OSSA_DIF_ENGINE_1_BIST_FAILURE           0x2  /* DIF Engine 1 failed the BIST Test */
#define OSSA_DIF_ENGINE_2_BIST_FAILURE           0x4  /* DIF Engine 2 failed the BIST Test */
#define OSSA_DIF_ENGINE_3_BIST_FAILURE           0x8  /* DIF Engine 3 failed the BIST Test */

#define SA_ROLE_CAPABILITIES_CSP 0x001
#define SA_ROLE_CAPABILITIES_OPR 0x002
#define SA_ROLE_CAPABILITIES_SCO 0x004
#define SA_ROLE_CAPABILITIES_STS 0x008
#define SA_ROLE_CAPABILITIES_TST 0x010
#define SA_ROLE_CAPABILITIES_KEK 0x020
#define SA_ROLE_CAPABILITIES_DEK 0x040
#define SA_ROLE_CAPABILITIES_IOS 0x080
#define SA_ROLE_CAPABILITIES_FWU 0x100
#define SA_ROLE_CAPABILITIES_PRM 0x200


#include "saapi.h"
/***********************************************************************************
 *                                                                                 *
 *              The LL Layer Functions Declarations end                            *
 *                                                                                 *
 ***********************************************************************************/

#endif  /*__SA_H__ */
