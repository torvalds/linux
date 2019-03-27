/*-
 * Copyright (c) 2002-2008 Sam Leffler, Errno Consulting
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer,
 *    without modification.
 * 2. Redistributions in binary form must reproduce at minimum a disclaimer
 *    similar to the "NO WARRANTY" disclaimer below ("Disclaimer") and any
 *    redistribution must be conditioned upon including a substantially
 *    similar Disclaimer requirement for further binary redistribution.
 *
 * NO WARRANTY
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 * ``AS IS'' AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 * LIMITED TO, THE IMPLIED WARRANTIES OF NONINFRINGEMENT, MERCHANTIBILITY
 * AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL
 * THE COPYRIGHT HOLDERS OR CONTRIBUTORS BE LIABLE FOR SPECIAL, EXEMPLARY,
 * OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 * SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
 * INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER
 * IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 * ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF
 * THE POSSIBILITY OF SUCH DAMAGES.
 *
 * $FreeBSD$
 */
#ifndef _ATH_AH_OSDEP_H_
#define _ATH_AH_OSDEP_H_
/*
 * Atheros Hardware Access Layer (HAL) OS Dependent Definitions.
 */
#include <sys/param.h>

#include <strings.h>

/*
 * Bus i/o type definitions.
 */
typedef void *HAL_SOFTC;
typedef int HAL_BUS_TAG;
typedef void *HAL_BUS_HANDLE;
typedef uint32_t HAL_DMA_ADDR;

#define	OS_DELAY(_n)	DELAY(_n)
#define	OS_INLINE	__inline
#define	OS_MEMZERO(_a, _size)		bzero((_a), (_size))
#define	OS_MEMCPY(_dst, _src, _size)	bcopy((_src), (_dst), (_size))
#define	OS_MACEQU(_a, _b) \
	(bcmp((_a), (_b), IEEE80211_ADDR_LEN) == 0)

struct ath_hal;
extern 	u_int32_t OS_GETUPTIME(struct ath_hal *);
extern	void OS_REG_WRITE(struct ath_hal *, u_int32_t, u_int32_t);
extern	u_int32_t OS_REG_READ(struct ath_hal *, u_int32_t);
extern	void OS_MARK(struct ath_hal *, u_int id, u_int32_t value);
#define	OS_GETUPTIME(_ah)	0
#define	OS_REG_WRITE(_ah, _reg, _val)
#define	OS_REG_READ(_ah, _reg)	0
#define	OS_MARK(_ah, _id, _v)
#define __packed __attribute__((__packed__))

/*
 * Linux/BSD gcc compatibility shims.
 */
#ifndef __printflike
#define	__printflike(_a,_b) \
	__attribute__ ((__format__ (__printf__, _a, _b)))
#endif
#include <stdarg.h>
#ifndef __va_list
#define	__va_list	va_list
#endif
#define	OS_INLINE	__inline
#endif /* _ATH_AH_OSDEP_H_ */
