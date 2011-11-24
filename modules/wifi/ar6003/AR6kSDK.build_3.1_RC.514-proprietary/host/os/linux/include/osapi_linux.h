//------------------------------------------------------------------------------
// This file contains the definitions of the basic atheros data types.
// It is used to map the data types in atheros files to a platform specific
// type.
// Copyright (c) 2004-2010 Atheros Communications Inc.
// All rights reserved.
//
// 
// The software source and binaries included in this development package are
// licensed, not sold. You, or your company, received the package under one
// or more license agreements. The rights granted to you are specifically
// listed in these license agreement(s). All other rights remain with Atheros
// Communications, Inc., its subsidiaries, or the respective owner including
// those listed on the included copyright notices.  Distribution of any
// portion of this package must be in strict compliance with the license
// agreement(s) terms.
// </copyright>
// 
// <summary>
// 	Wifi driver for AR6002
// </summary>
//
//
// Author(s): ="Atheros"
//------------------------------------------------------------------------------

#ifndef _OSAPI_LINUX_H_
#define _OSAPI_LINUX_H_


#ifdef __GNUC__
#define __ATTRIB_PACK           __attribute__ ((packed))
#define __ATTRIB_PRINTF         __attribute__ ((format (printf, 1, 2)))
#define __ATTRIB_NORETURN       __attribute__ ((noreturn))
#ifndef INLINE
#define INLINE                  __inline__
#endif
#else /* Not GCC */
#define __ATTRIB_PACK
#define __ATTRIB_PRINTF
#define __ATTRIB_NORETURN
#ifndef INLINE
#define INLINE                  __inline
#endif
#endif /* End __GNUC__ */

#define PREPACK
#define POSTPACK                __ATTRIB_PACK

#define A_MEMCPY(dst, src, len)         memcpy((dst), (src), (len))
#define A_MEMZERO(addr, len)            memset((addr), 0, (len))
#define A_MEMCMP(addr1, addr2, len)     memcmp((addr1), (addr2), (len))
#define A_MALLOC(size)                  malloc(size)
#define A_FREE(addr)                    free(addr)

#ifdef ANDROID
#ifndef err
#include <errno.h>
#define err(_s, args...) do { \
    fprintf(stderr, "%s: line %d ", __FILE__, __LINE__); \
    fprintf(stderr, args); fprintf(stderr, ": %d\n", errno); \
    exit(_s); } while (0)
#endif
#else
#include <err.h>
#endif


#endif /* _OSAPI_LINUX_H_ */
