/***********************license start***************
 * Copyright (c) 2003-2010  Cavium Inc. (support@cavium.com). All rights
 * reserved.
 *
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are
 * met:
 *
 *   * Redistributions of source code must retain the above copyright
 *     notice, this list of conditions and the following disclaimer.
 *
 *   * Redistributions in binary form must reproduce the above
 *     copyright notice, this list of conditions and the following
 *     disclaimer in the documentation and/or other materials provided
 *     with the distribution.

 *   * Neither the name of Cavium Inc. nor the names of
 *     its contributors may be used to endorse or promote products
 *     derived from this software without specific prior written
 *     permission.

 * This Software, including technical data, may be subject to U.S. export  control
 * laws, including the U.S. Export Administration Act and its  associated
 * regulations, and may be subject to export or import  regulations in other
 * countries.

 * TO THE MAXIMUM EXTENT PERMITTED BY LAW, THE SOFTWARE IS PROVIDED "AS IS"
 * AND WITH ALL FAULTS AND CAVIUM INC. MAKES NO PROMISES, REPRESENTATIONS OR
 * WARRANTIES, EITHER EXPRESS, IMPLIED, STATUTORY, OR OTHERWISE, WITH RESPECT TO
 * THE SOFTWARE, INCLUDING ITS CONDITION, ITS CONFORMITY TO ANY REPRESENTATION OR
 * DESCRIPTION, OR THE EXISTENCE OF ANY LATENT OR PATENT DEFECTS, AND CAVIUM
 * SPECIFICALLY DISCLAIMS ALL IMPLIED (IF ANY) WARRANTIES OF TITLE,
 * MERCHANTABILITY, NONINFRINGEMENT, FITNESS FOR A PARTICULAR PURPOSE, LACK OF
 * VIRUSES, ACCURACY OR COMPLETENESS, QUIET ENJOYMENT, QUIET POSSESSION OR
 * CORRESPONDENCE TO DESCRIPTION. THE ENTIRE  RISK ARISING OUT OF USE OR
 * PERFORMANCE OF THE SOFTWARE LIES WITH YOU.
 ***********************license end**************************************/



/**
 * @file
 *
 * This file defines macros for use in determining the current calling ABI.
 *
 * <hr>$Revision: 70030 $<hr>
*/

#ifndef __CVMX_ABI_H__
#define __CVMX_ABI_H__

#if defined(__FreeBSD__) && defined(_KERNEL)
#include <machine/endian.h>
#else
#ifndef __U_BOOT__
#include <endian.h>
#endif
#endif

#ifdef	__cplusplus
extern "C" {
#endif

/* Check for N32 ABI, defined for 32-bit Simple Exec applications
   and Linux N32 ABI.*/
#if (defined _ABIN32 && _MIPS_SIM == _ABIN32)
#define CVMX_ABI_N32
/* Check for N64 ABI, defined for 64-bit Linux toolchain. */
#elif (defined _ABI64 && _MIPS_SIM == _ABI64)
#define CVMX_ABI_N64
/* Check for O32 ABI, defined for Linux 032 ABI, not supported yet. */
#elif (defined _ABIO32 && _MIPS_SIM == _ABIO32)
#define CVMX_ABI_O32
/* Check for EABI ABI, defined for 64-bit Simple Exec applications. */
#else
#define CVMX_ABI_EABI
#endif

#ifndef __BYTE_ORDER
    #if defined(__BIG_ENDIAN) && !defined(__LITTLE_ENDIAN)
        #define __BYTE_ORDER __BIG_ENDIAN
    #elif !defined(__BIG_ENDIAN) && defined(__LITTLE_ENDIAN)
        #define __BYTE_ORDER __LITTLE_ENDIAN
        #define __BIG_ENDIAN 4321
    #elif !defined(__BIG_ENDIAN) && !defined(__LITTLE_ENDIAN)
        #define __BIG_ENDIAN 4321
        #define __BYTE_ORDER __BIG_ENDIAN
    #else
        #error Unable to determine Endian mode
    #endif
#endif

/* For compatibility with Linux definitions... */
#if __BYTE_ORDER == __BIG_ENDIAN
# ifndef __BIG_ENDIAN_BITFIELD
#  define __BIG_ENDIAN_BITFIELD
# endif
#else
# ifndef __LITTLE_ENDIAN_BITFIELD
#  define __LITTLE_ENDIAN_BITFIELD
# endif
#endif
#if defined(__BIG_ENDIAN_BITFIELD) && defined(__LITTLE_ENDIAN_BITFIELD)
# error Cannot define both __BIG_ENDIAN_BITFIELD and __LITTLE_ENDIAN_BITFIELD
#endif

#ifdef	__cplusplus
}
#endif

#endif /* __CVMX_ABI_H__ */
