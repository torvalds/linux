/*-
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD
 *
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
#include <sys/cdefs.h>
#include <sys/param.h>
#include <sys/systm.h>
#include <sys/endian.h>
#include <sys/linker_set.h>

#include <machine/bus.h>

/*
 * Bus i/o type definitions.
 */
typedef void *HAL_SOFTC;
typedef bus_space_tag_t HAL_BUS_TAG;
typedef bus_space_handle_t HAL_BUS_HANDLE;

/*
 * Although the underlying hardware may support 64 bit DMA, the
 * current Atheros hardware only supports 32 bit addressing.
 */
typedef uint32_t HAL_DMA_ADDR;

/*
 * Linker set writearounds for chip and RF backend registration.
 */
#define	OS_DATA_SET(set, item)	DATA_SET(set, item)
#define	OS_SET_DECLARE(set, ptype)	SET_DECLARE(set, ptype)
#define	OS_SET_FOREACH(pvar, set)	SET_FOREACH(pvar, set)

/*
 * Delay n microseconds.
 */
#define	OS_DELAY(_n)	DELAY(_n)

#define	OS_INLINE	__inline
#define	OS_MEMZERO(_a, _n)	bzero((_a), (_n))
#define	OS_MEMCPY(_d, _s, _n)	memcpy(_d,_s,_n)
#define	OS_MEMCMP(_a, _b, _l)	memcmp((_a), (_b), (_l))

#define	abs(_a)		__builtin_abs(_a)

struct ath_hal;

/*
 * The hardware registers are native little-endian byte order.
 * Big-endian hosts are handled by enabling hardware byte-swap
 * of register reads and writes at reset.  But the PCI clock
 * domain registers are not byte swapped!  Thus, on big-endian
 * platforms we have to explicitly byte-swap those registers.
 * OS_REG_UNSWAPPED identifies the registers that need special handling.
 *
 * This is not currently used by the FreeBSD HAL osdep code; the HAL
 * currently does not configure hardware byteswapping for register space
 * accesses and instead does it through the FreeBSD bus space code.
 */
#if _BYTE_ORDER == _BIG_ENDIAN
#define	OS_REG_UNSWAPPED(_reg) \
	(((_reg) >= 0x4000 && (_reg) < 0x5000) || \
	 ((_reg) >= 0x7000 && (_reg) < 0x8000))
#else /* _BYTE_ORDER == _LITTLE_ENDIAN */
#define	OS_REG_UNSWAPPED(_reg)	(0)
#endif /* _BYTE_ORDER */

/*
 * For USB/SDIO support (where access latencies are quite high);
 * some write accesses may be buffered and then flushed when
 * either a read is done, or an explicit flush is done.
 *
 * These are simply placeholders for now.
 */
#define	OS_REG_WRITE_BUFFER_ENABLE(_ah)		\
	    do { } while (0)
#define	OS_REG_WRITE_BUFFER_DISABLE(_ah)	\
	    do { } while (0)
#define	OS_REG_WRITE_BUFFER_FLUSH(_ah)		\
	    do { } while (0)

/*
 * Read and write barriers.  Some platforms require more strongly ordered
 * operations and unfortunately most of the HAL is written assuming everything
 * is either an x86 or the bus layer will do the barriers for you.
 *
 * Read barriers should occur before each read, and write barriers
 * occur after each write.
 *
 * Later on for SDIO/USB parts we will methodize this and make them no-ops;
 * register accesses will go via USB commands.
 */
#define	OS_BUS_BARRIER_READ	BUS_SPACE_BARRIER_READ
#define	OS_BUS_BARRIER_WRITE	BUS_SPACE_BARRIER_WRITE
#define	OS_BUS_BARRIER_RW \
	    (BUS_SPACE_BARRIER_READ | BUS_SPACE_BARRIER_WRITE)
#define	OS_BUS_BARRIER(_ah, _start, _len, _t) \
	bus_space_barrier((bus_space_tag_t)(_ah)->ah_st,	\
	    (bus_space_handle_t)(_ah)->ah_sh, (_start), (_len), (_t))
#define	OS_BUS_BARRIER_REG(_ah, _reg, _t) \
	OS_BUS_BARRIER((_ah), (_reg), 4, (_t))

/*
 * Register read/write operations are handled through
 * platform-dependent routines.
 */
#define	OS_REG_WRITE(_ah, _reg, _val)	ath_hal_reg_write(_ah, _reg, _val)
#define	OS_REG_READ(_ah, _reg)		ath_hal_reg_read(_ah, _reg)

extern	void ath_hal_reg_write(struct ath_hal *ah, u_int reg, u_int32_t val);
extern	u_int32_t ath_hal_reg_read(struct ath_hal *ah, u_int reg);

#ifdef AH_DEBUG_ALQ
extern	void OS_MARK(struct ath_hal *, u_int id, u_int32_t value);
#else
#define	OS_MARK(_ah, _id, _v)
#endif

#endif /* _ATH_AH_OSDEP_H_ */
