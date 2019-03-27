/* Copyright (c) 2008-2012 Freescale Semiconductor, Inc
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are met:
 *     * Redistributions of source code must retain the above copyright
 *       notice, this list of conditions and the following disclaimer.
 *     * Redistributions in binary form must reproduce the above copyright
 *       notice, this list of conditions and the following disclaimer in the
 *       documentation and/or other materials provided with the distribution.
 *     * Neither the name of Freescale Semiconductor nor the
 *       names of its contributors may be used to endorse or promote products
 *       derived from this software without specific prior written permission.
 *
 *
 * ALTERNATIVELY, this software may be distributed under the terms of the
 * GNU General Public License ("GPL") as published by the Free Software
 * Foundation, either version 2 of that License or (at your option) any
 * later version.
 *
 * THIS SOFTWARE IS PROVIDED BY Freescale Semiconductor ``AS IS'' AND ANY
 * EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED
 * WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
 * DISCLAIMED. IN NO EVENT SHALL Freescale Semiconductor BE LIABLE FOR ANY
 * DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES
 * (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES;
 * LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND
 * ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS
 * SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */


/**************************************************************************//**
 @File          dpaa_ext.h

 @Description   DPAA Application Programming Interface.
*//***************************************************************************/
#ifndef __DPAA_EXT_H
#define __DPAA_EXT_H

#include "std_ext.h"
#include "error_ext.h"


/**************************************************************************//**
 @Group         DPAA_grp Data Path Acceleration Architecture API

 @Description   DPAA API functions, definitions and enums.

 @{
*//***************************************************************************/

#if defined(__MWERKS__) && !defined(__GNUC__)
#pragma pack(push,1)
#endif /* defined(__MWERKS__) && ... */

#include <machine/endian.h>

#define __BYTE_ORDER__ BYTE_ORDER
#define __ORDER_BIG_ENDIAN__	BIG_ENDIAN

/**************************************************************************//**
 @Description   Frame descriptor
*//***************************************************************************/
typedef _Packed struct t_DpaaFD {
#if __BYTE_ORDER__ == __ORDER_BIG_ENDIAN__
    volatile uint8_t liodn;
    volatile uint8_t bpid;
    volatile uint8_t elion;
    volatile uint8_t addrh;
    volatile uint32_t addrl;
#else
    volatile uint32_t addrl;
    volatile uint8_t addrh;
    volatile uint8_t elion;
    volatile uint8_t bpid;
    volatile uint8_t liodn;
 #endif
    volatile uint32_t    length;            /**< Frame length */
    volatile uint32_t    status;            /**< FD status */
} _PackedType t_DpaaFD;

/**************************************************************************//**
 @Description   enum for defining frame format
*//***************************************************************************/
typedef enum e_DpaaFDFormatType {
    e_DPAA_FD_FORMAT_TYPE_SHORT_SBSF  = 0x0,   /**< Simple frame Single buffer; Offset and
                                                    small length (9b OFFSET, 20b LENGTH) */
    e_DPAA_FD_FORMAT_TYPE_LONG_SBSF   = 0x2,   /**< Simple frame, single buffer; big length
                                                    (29b LENGTH ,No OFFSET) */
    e_DPAA_FD_FORMAT_TYPE_SHORT_MBSF  = 0x4,   /**< Simple frame, Scatter Gather table; Offset
                                                    and small length (9b OFFSET, 20b LENGTH) */
    e_DPAA_FD_FORMAT_TYPE_LONG_MBSF   = 0x6,   /**< Simple frame, Scatter Gather table;
                                                    big length (29b LENGTH ,No OFFSET) */
    e_DPAA_FD_FORMAT_TYPE_COMPOUND    = 0x1,   /**< Compound Frame (29b CONGESTION-WEIGHT
                                                    No LENGTH or OFFSET) */
    e_DPAA_FD_FORMAT_TYPE_DUMMY
} e_DpaaFDFormatType;

/**************************************************************************//**
 @Collection   Frame descriptor macros
*//***************************************************************************/
#define DPAA_FD_DD_MASK       0xc0000000           /**< FD DD field mask */
#define DPAA_FD_PID_MASK      0x3f000000           /**< FD PID field mask */
#define DPAA_FD_ELIODN_MASK   0x0000f000           /**< FD ELIODN field mask */
#define DPAA_FD_BPID_MASK     0x00ff0000           /**< FD BPID field mask */
#define DPAA_FD_ADDRH_MASK    0x000000ff           /**< FD ADDRH field mask */
#define DPAA_FD_ADDRL_MASK    0xffffffff           /**< FD ADDRL field mask */
#define DPAA_FD_FORMAT_MASK   0xe0000000           /**< FD FORMAT field mask */
#define DPAA_FD_OFFSET_MASK   0x1ff00000           /**< FD OFFSET field mask */
#define DPAA_FD_LENGTH_MASK   0x000fffff           /**< FD LENGTH field mask */

#define DPAA_FD_GET_ADDRH(fd)         ((t_DpaaFD *)fd)->addrh                       /**< Macro to get FD ADDRH field */
#define DPAA_FD_GET_ADDRL(fd)         ((t_DpaaFD *)fd)->addrl                                           /**< Macro to get FD ADDRL field */
#define DPAA_FD_GET_PHYS_ADDR(fd)     ((physAddress_t)(((uint64_t)DPAA_FD_GET_ADDRH(fd) << 32) | (uint64_t)DPAA_FD_GET_ADDRL(fd))) /**< Macro to get FD ADDR field */
#define DPAA_FD_GET_FORMAT(fd)        ((((t_DpaaFD *)fd)->length & DPAA_FD_FORMAT_MASK) >> (31-2))      /**< Macro to get FD FORMAT field */
#define DPAA_FD_GET_OFFSET(fd)        ((((t_DpaaFD *)fd)->length & DPAA_FD_OFFSET_MASK) >> (31-11))     /**< Macro to get FD OFFSET field */
#define DPAA_FD_GET_LENGTH(fd)        (((t_DpaaFD *)fd)->length & DPAA_FD_LENGTH_MASK)                  /**< Macro to get FD LENGTH field */
#define DPAA_FD_GET_STATUS(fd)        ((t_DpaaFD *)fd)->status                                          /**< Macro to get FD STATUS field */
#define DPAA_FD_GET_ADDR(fd)          XX_PhysToVirt(DPAA_FD_GET_PHYS_ADDR(fd))                          /**< Macro to get FD ADDR (virtual) */

#define DPAA_FD_SET_ADDRH(fd,val)     ((t_DpaaFD *)fd)->addrh = (val)            /**< Macro to set FD ADDRH field */
#define DPAA_FD_SET_ADDRL(fd,val)     ((t_DpaaFD *)fd)->addrl = (val)                                   /**< Macro to set FD ADDRL field */
#define DPAA_FD_SET_ADDR(fd,val)                            \
do {                                                        \
    uint64_t physAddr = (uint64_t)(XX_VirtToPhys(val));     \
    DPAA_FD_SET_ADDRH(fd, ((uint32_t)(physAddr >> 32)));    \
    DPAA_FD_SET_ADDRL(fd, (uint32_t)physAddr);              \
} while (0)                                                                                             /**< Macro to set FD ADDR field */
#define DPAA_FD_SET_FORMAT(fd,val)    (((t_DpaaFD *)fd)->length = ((((t_DpaaFD *)fd)->length & ~DPAA_FD_FORMAT_MASK) | (((val)  << (31-2))& DPAA_FD_FORMAT_MASK)))  /**< Macro to set FD FORMAT field */
#define DPAA_FD_SET_OFFSET(fd,val)    (((t_DpaaFD *)fd)->length = ((((t_DpaaFD *)fd)->length & ~DPAA_FD_OFFSET_MASK) | (((val) << (31-11))& DPAA_FD_OFFSET_MASK) )) /**< Macro to set FD OFFSET field */
#define DPAA_FD_SET_LENGTH(fd,val)    (((t_DpaaFD *)fd)->length = (((t_DpaaFD *)fd)->length & ~DPAA_FD_LENGTH_MASK) | ((val) & DPAA_FD_LENGTH_MASK))                /**< Macro to set FD LENGTH field */
#define DPAA_FD_SET_STATUS(fd,val)    ((t_DpaaFD *)fd)->status = (val)                                  /**< Macro to set FD STATUS field */
/* @} */

/**************************************************************************//**
 @Description   Frame Scatter/Gather Table Entry
*//***************************************************************************/
typedef _Packed struct t_DpaaSGTE {
    volatile uint32_t    addrh;        /**< Buffer Address high */
    volatile uint32_t    addrl;        /**< Buffer Address low */
    volatile uint32_t    length;       /**< Buffer length */
    volatile uint32_t    offset;       /**< SGTE offset */
} _PackedType t_DpaaSGTE;

#define DPAA_NUM_OF_SG_TABLE_ENTRY 16

/**************************************************************************//**
 @Description   Frame Scatter/Gather Table
*//***************************************************************************/
typedef _Packed struct t_DpaaSGT {
    t_DpaaSGTE    tableEntry[DPAA_NUM_OF_SG_TABLE_ENTRY];
                                    /**< Structure that holds information about
                                         a single S/G entry. */
} _PackedType t_DpaaSGT;

/**************************************************************************//**
 @Description   Compound Frame Table
*//***************************************************************************/
typedef _Packed struct t_DpaaCompTbl {
    t_DpaaSGTE    outputBuffInfo;   /**< Structure that holds information about
                                         the compound-frame output buffer;
                                         NOTE: this may point to a S/G table */
    t_DpaaSGTE    inputBuffInfo;    /**< Structure that holds information about
                                         the compound-frame input buffer;
                                         NOTE: this may point to a S/G table */
} _PackedType t_DpaaCompTbl;

/**************************************************************************//**
 @Collection   Frame Scatter/Gather Table Entry macros
*//***************************************************************************/
#define DPAA_SGTE_ADDRH_MASK    0x000000ff           /**< SGTE ADDRH field mask */
#define DPAA_SGTE_ADDRL_MASK    0xffffffff           /**< SGTE ADDRL field mask */
#define DPAA_SGTE_E_MASK        0x80000000           /**< SGTE Extension field mask */
#define DPAA_SGTE_F_MASK        0x40000000           /**< SGTE Final field mask */
#define DPAA_SGTE_LENGTH_MASK   0x3fffffff           /**< SGTE LENGTH field mask */
#define DPAA_SGTE_BPID_MASK     0x00ff0000           /**< SGTE BPID field mask */
#define DPAA_SGTE_OFFSET_MASK   0x00001fff           /**< SGTE OFFSET field mask */

#define DPAA_SGTE_GET_ADDRH(sgte)         (((t_DpaaSGTE *)sgte)->addrh & DPAA_SGTE_ADDRH_MASK)              /**< Macro to get SGTE ADDRH field */
#define DPAA_SGTE_GET_ADDRL(sgte)         ((t_DpaaSGTE *)sgte)->addrl                                       /**< Macro to get SGTE ADDRL field */
#define DPAA_SGTE_GET_PHYS_ADDR(sgte)     ((physAddress_t)(((uint64_t)DPAA_SGTE_GET_ADDRH(sgte) << 32) | (uint64_t)DPAA_SGTE_GET_ADDRL(sgte))) /**< Macro to get FD ADDR field */
#define DPAA_SGTE_GET_EXTENSION(sgte)     ((((t_DpaaSGTE *)sgte)->length & DPAA_SGTE_E_MASK) >> (31-0))     /**< Macro to get SGTE EXTENSION field */
#define DPAA_SGTE_GET_FINAL(sgte)         ((((t_DpaaSGTE *)sgte)->length & DPAA_SGTE_F_MASK) >> (31-1))     /**< Macro to get SGTE FINAL field */
#define DPAA_SGTE_GET_LENGTH(sgte)        (((t_DpaaSGTE *)sgte)->length & DPAA_SGTE_LENGTH_MASK)            /**< Macro to get SGTE LENGTH field */
#define DPAA_SGTE_GET_BPID(sgte)          ((((t_DpaaSGTE *)sgte)->offset & DPAA_SGTE_BPID_MASK) >> (31-15)) /**< Macro to get SGTE BPID field */
#define DPAA_SGTE_GET_OFFSET(sgte)        (((t_DpaaSGTE *)sgte)->offset & DPAA_SGTE_OFFSET_MASK)            /**< Macro to get SGTE OFFSET field */
#define DPAA_SGTE_GET_ADDR(sgte)          XX_PhysToVirt(DPAA_SGTE_GET_PHYS_ADDR(sgte))

#define DPAA_SGTE_SET_ADDRH(sgte,val)     (((t_DpaaSGTE *)sgte)->addrh = ((((t_DpaaSGTE *)sgte)->addrh & ~DPAA_SGTE_ADDRH_MASK) | ((val) & DPAA_SGTE_ADDRH_MASK))) /**< Macro to set SGTE ADDRH field */
#define DPAA_SGTE_SET_ADDRL(sgte,val)     ((t_DpaaSGTE *)sgte)->addrl = (val)                                 /**< Macro to set SGTE ADDRL field */
#define DPAA_SGTE_SET_ADDR(sgte,val)                            \
do {                                                            \
    uint64_t physAddr = (uint64_t)(XX_VirtToPhys(val));         \
    DPAA_SGTE_SET_ADDRH(sgte, ((uint32_t)(physAddr >> 32)));    \
    DPAA_SGTE_SET_ADDRL(sgte, (uint32_t)physAddr);              \
} while (0)                                                                                                 /**< Macro to set SGTE ADDR field */
#define DPAA_SGTE_SET_EXTENSION(sgte,val) (((t_DpaaSGTE *)sgte)->length = ((((t_DpaaSGTE *)sgte)->length & ~DPAA_SGTE_E_MASK) | (((val)  << (31-0))& DPAA_SGTE_E_MASK)))            /**< Macro to set SGTE EXTENSION field */
#define DPAA_SGTE_SET_FINAL(sgte,val)     (((t_DpaaSGTE *)sgte)->length = ((((t_DpaaSGTE *)sgte)->length & ~DPAA_SGTE_F_MASK) | (((val)  << (31-1))& DPAA_SGTE_F_MASK)))            /**< Macro to set SGTE FINAL field */
#define DPAA_SGTE_SET_LENGTH(sgte,val)    (((t_DpaaSGTE *)sgte)->length = (((t_DpaaSGTE *)sgte)->length & ~DPAA_SGTE_LENGTH_MASK) | ((val) & DPAA_SGTE_LENGTH_MASK))                /**< Macro to set SGTE LENGTH field */
#define DPAA_SGTE_SET_BPID(sgte,val)      (((t_DpaaSGTE *)sgte)->offset = ((((t_DpaaSGTE *)sgte)->offset & ~DPAA_SGTE_BPID_MASK) | (((val)  << (31-15))& DPAA_SGTE_BPID_MASK)))     /**< Macro to set SGTE BPID field */
#define DPAA_SGTE_SET_OFFSET(sgte,val)    (((t_DpaaSGTE *)sgte)->offset = ((((t_DpaaSGTE *)sgte)->offset & ~DPAA_SGTE_OFFSET_MASK) | (((val) << (31-31))& DPAA_SGTE_OFFSET_MASK) )) /**< Macro to set SGTE OFFSET field */
/* @} */

#if defined(__MWERKS__) && !defined(__GNUC__)
#pragma pack(pop)
#endif /* defined(__MWERKS__) && ... */

#define DPAA_LIODN_DONT_OVERRIDE    (-1)

/** @} */ /* end of DPAA_grp group */


#endif /* __DPAA_EXT_H */
