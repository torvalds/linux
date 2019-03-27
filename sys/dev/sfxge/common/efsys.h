/*-
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD
 *
 * Copyright (c) 2010-2016 Solarflare Communications Inc.
 * All rights reserved.
 *
 * This software was developed in part by Philip Paeps under contract for
 * Solarflare Communications, Inc.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are met:
 *
 * 1. Redistributions of source code must retain the above copyright notice,
 *    this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright notice,
 *    this list of conditions and the following disclaimer in the documentation
 *    and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
 * AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO,
 * THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR
 * PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT OWNER OR
 * CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL,
 * EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO,
 * PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS;
 * OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY,
 * WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR
 * OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE,
 * EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 *
 * The views and conclusions contained in the software and documentation are
 * those of the authors and should not be interpreted as representing official
 * policies, either expressed or implied, of the FreeBSD Project.
 *
 * $FreeBSD$
 */

#ifndef	_SYS_EFSYS_H
#define	_SYS_EFSYS_H

#ifdef	__cplusplus
extern "C" {
#endif

#include <sys/param.h>
#include <sys/bus.h>
#include <sys/endian.h>
#include <sys/lock.h>
#include <sys/malloc.h>
#include <sys/mbuf.h>
#include <sys/mutex.h>
#include <sys/rwlock.h>
#include <sys/sdt.h>
#include <sys/systm.h>

#include <machine/bus.h>
#include <machine/endian.h>

#define	EFSYS_HAS_UINT64 1
#if defined(__x86_64__)
#define	EFSYS_USE_UINT64 1
#else
#define	EFSYS_USE_UINT64 0
#endif
#define	EFSYS_HAS_SSE2_M128 0
#if _BYTE_ORDER == _BIG_ENDIAN
#define	EFSYS_IS_BIG_ENDIAN 1
#define	EFSYS_IS_LITTLE_ENDIAN 0
#elif _BYTE_ORDER == _LITTLE_ENDIAN
#define	EFSYS_IS_BIG_ENDIAN 0
#define	EFSYS_IS_LITTLE_ENDIAN 1
#endif
#include "efx_types.h"

/* Common code requires this */
#if __FreeBSD_version < 800068
#define	memmove(d, s, l) bcopy(s, d, l)
#endif

#ifndef B_FALSE
#define	B_FALSE	FALSE
#endif
#ifndef B_TRUE
#define	B_TRUE	TRUE
#endif

#ifndef IS_P2ALIGNED
#define	IS_P2ALIGNED(v, a)	((((uintptr_t)(v)) & ((uintptr_t)(a) - 1)) == 0)
#endif

#ifndef P2ROUNDUP
#define	P2ROUNDUP(x, align)	(-(-(x) & -(align)))
#endif

#ifndef P2ALIGN
#define	P2ALIGN(_x, _a)		((_x) & -(_a))
#endif

#ifndef IS2P
#define	ISP2(x)			(((x) & ((x) - 1)) == 0)
#endif

#if defined(__x86_64__) && __FreeBSD_version >= 1000000

#define	SFXGE_USE_BUS_SPACE_8		1

#if !defined(bus_space_read_stream_8)

#define	bus_space_read_stream_8(t, h, o)				\
	bus_space_read_8((t), (h), (o))

#define	bus_space_write_stream_8(t, h, o, v)				\
	bus_space_write_8((t), (h), (o), (v))

#endif

#endif

#define	ENOTACTIVE EINVAL

/* Memory type to use on FreeBSD */
MALLOC_DECLARE(M_SFXGE);

/* Machine dependend prefetch wrappers */
#if defined(__i386__) || defined(__amd64__)
static __inline void
prefetch_read_many(void *addr)
{

	__asm__(
	    "prefetcht0 (%0)"
	    :
	    : "r" (addr));
}

static __inline void
prefetch_read_once(void *addr)
{

	__asm__(
	    "prefetchnta (%0)"
	    :
	    : "r" (addr));
}
#elif defined(__sparc64__)
static __inline void
prefetch_read_many(void *addr)
{

	__asm__(
	    "prefetch [%0], 0"
	    :
	    : "r" (addr));
}

static __inline void
prefetch_read_once(void *addr)
{

	__asm__(
	    "prefetch [%0], 1"
	    :
	    : "r" (addr));
}
#else
static __inline void
prefetch_read_many(void *addr)
{

}

static __inline void
prefetch_read_once(void *addr)
{

}
#endif

#if defined(__i386__) || defined(__amd64__)
#include <vm/vm.h>
#include <vm/pmap.h>
#endif
static __inline void
sfxge_map_mbuf_fast(bus_dma_tag_t tag, bus_dmamap_t map,
		    struct mbuf *m, bus_dma_segment_t *seg)
{
#if defined(__i386__) || defined(__amd64__)
	seg->ds_addr = pmap_kextract(mtod(m, vm_offset_t));
	seg->ds_len = m->m_len;
#else
	int nsegstmp;

	bus_dmamap_load_mbuf_sg(tag, map, m, seg, &nsegstmp, 0);
#endif
}

/* Code inclusion options */


#define	EFSYS_OPT_NAMES 1

#define	EFSYS_OPT_SIENA 1
#define	EFSYS_OPT_HUNTINGTON 1
#define	EFSYS_OPT_MEDFORD 1
#define	EFSYS_OPT_MEDFORD2 1
#ifdef DEBUG
#define	EFSYS_OPT_CHECK_REG 1
#else
#define	EFSYS_OPT_CHECK_REG 0
#endif

#define	EFSYS_OPT_MCDI 1
#define	EFSYS_OPT_MCDI_LOGGING 0
#define	EFSYS_OPT_MCDI_PROXY_AUTH 0

#define	EFSYS_OPT_MAC_STATS 1

#define	EFSYS_OPT_LOOPBACK 0

#define	EFSYS_OPT_MON_MCDI 0
#define	EFSYS_OPT_MON_STATS 0

#define	EFSYS_OPT_PHY_STATS 1
#define	EFSYS_OPT_BIST 1
#define	EFSYS_OPT_PHY_LED_CONTROL 1
#define	EFSYS_OPT_PHY_FLAGS 0

#define	EFSYS_OPT_VPD 1
#define	EFSYS_OPT_NVRAM 1
#define	EFSYS_OPT_BOOTCFG 0
#define	EFSYS_OPT_IMAGE_LAYOUT 0

#define	EFSYS_OPT_DIAG 0
#define	EFSYS_OPT_RX_SCALE 1
#define	EFSYS_OPT_QSTATS 1
#define	EFSYS_OPT_FILTER 1
#define	EFSYS_OPT_RX_SCATTER 0

#define	EFSYS_OPT_EV_PREFETCH 0

#define	EFSYS_OPT_DECODE_INTR_FATAL 1

#define	EFSYS_OPT_LICENSING 0

#define	EFSYS_OPT_ALLOW_UNCONFIGURED_NIC 0

#define	EFSYS_OPT_RX_PACKED_STREAM 0

#define	EFSYS_OPT_RX_ES_SUPER_BUFFER 0

#define	EFSYS_OPT_TUNNEL 0

#define	EFSYS_OPT_FW_SUBVARIANT_AWARE 0

/* ID */

typedef struct __efsys_identifier_s	efsys_identifier_t;

/* PROBE */

#ifndef DTRACE_PROBE

#define	EFSYS_PROBE(_name)

#define	EFSYS_PROBE1(_name, _type1, _arg1)

#define	EFSYS_PROBE2(_name, _type1, _arg1, _type2, _arg2)

#define	EFSYS_PROBE3(_name, _type1, _arg1, _type2, _arg2,		\
	    _type3, _arg3)

#define	EFSYS_PROBE4(_name, _type1, _arg1, _type2, _arg2,		\
	    _type3, _arg3, _type4, _arg4)

#define	EFSYS_PROBE5(_name, _type1, _arg1, _type2, _arg2,		\
	    _type3, _arg3, _type4, _arg4, _type5, _arg5)

#define	EFSYS_PROBE6(_name, _type1, _arg1, _type2, _arg2,		\
	    _type3, _arg3, _type4, _arg4, _type5, _arg5,		\
	    _type6, _arg6)

#define	EFSYS_PROBE7(_name, _type1, _arg1, _type2, _arg2,		\
	    _type3, _arg3, _type4, _arg4, _type5, _arg5,		\
	    _type6, _arg6, _type7, _arg7)

#else /* DTRACE_PROBE */

#define	EFSYS_PROBE(_name)						\
	DTRACE_PROBE(_name)

#define	EFSYS_PROBE1(_name, _type1, _arg1)				\
	DTRACE_PROBE1(_name, _type1, _arg1)

#define	EFSYS_PROBE2(_name, _type1, _arg1, _type2, _arg2)		\
	DTRACE_PROBE2(_name, _type1, _arg1, _type2, _arg2)

#define	EFSYS_PROBE3(_name, _type1, _arg1, _type2, _arg2,		\
	    _type3, _arg3)						\
	DTRACE_PROBE3(_name, _type1, _arg1, _type2, _arg2,		\
	    _type3, _arg3)

#define	EFSYS_PROBE4(_name, _type1, _arg1, _type2, _arg2,		\
	    _type3, _arg3, _type4, _arg4)				\
	DTRACE_PROBE4(_name, _type1, _arg1, _type2, _arg2,		\
	    _type3, _arg3, _type4, _arg4)

#ifdef DTRACE_PROBE5
#define	EFSYS_PROBE5(_name, _type1, _arg1, _type2, _arg2,		\
	    _type3, _arg3, _type4, _arg4, _type5, _arg5)		\
	DTRACE_PROBE5(_name, _type1, _arg1, _type2, _arg2,		\
	    _type3, _arg3, _type4, _arg4, _type5, _arg5)
#else
#define	EFSYS_PROBE5(_name, _type1, _arg1, _type2, _arg2,		\
	    _type3, _arg3, _type4, _arg4, _type5, _arg5)		\
	DTRACE_PROBE4(_name, _type1, _arg1, _type2, _arg2,		\
	    _type3, _arg3, _type4, _arg4)
#endif

#ifdef DTRACE_PROBE6
#define	EFSYS_PROBE6(_name, _type1, _arg1, _type2, _arg2,		\
	    _type3, _arg3, _type4, _arg4, _type5, _arg5,		\
	    _type6, _arg6)						\
	DTRACE_PROBE6(_name, _type1, _arg1, _type2, _arg2,		\
	    _type3, _arg3, _type4, _arg4, _type5, _arg5,		\
	    _type6, _arg6)
#else
#define	EFSYS_PROBE6(_name, _type1, _arg1, _type2, _arg2,		\
	    _type3, _arg3, _type4, _arg4, _type5, _arg5,		\
	    _type6, _arg6)						\
	EFSYS_PROBE5(_name, _type1, _arg1, _type2, _arg2,		\
	    _type3, _arg3, _type4, _arg4, _type5, _arg5)
#endif

#ifdef DTRACE_PROBE7
#define	EFSYS_PROBE7(_name, _type1, _arg1, _type2, _arg2,		\
	    _type3, _arg3, _type4, _arg4, _type5, _arg5,		\
	    _type6, _arg6, _type7, _arg7)				\
	DTRACE_PROBE7(_name, _type1, _arg1, _type2, _arg2,		\
	    _type3, _arg3, _type4, _arg4, _type5, _arg5,		\
	    _type6, _arg6, _type7, _arg7)
#else
#define	EFSYS_PROBE7(_name, _type1, _arg1, _type2, _arg2,		\
	    _type3, _arg3, _type4, _arg4, _type5, _arg5,		\
	    _type6, _arg6, _type7, _arg7)				\
	EFSYS_PROBE6(_name, _type1, _arg1, _type2, _arg2,		\
	    _type3, _arg3, _type4, _arg4, _type5, _arg5,		\
	    _type6, _arg6)
#endif

#endif /* DTRACE_PROBE */

/* DMA */

typedef uint64_t		efsys_dma_addr_t;

typedef struct efsys_mem_s {
	bus_dma_tag_t		esm_tag;
	bus_dmamap_t		esm_map;
	caddr_t			esm_base;
	efsys_dma_addr_t	esm_addr;
	size_t			esm_size;
} efsys_mem_t;

#define	EFSYS_MEM_SIZE(_esmp)						\
	((_esmp)->esm_size)

#define	EFSYS_MEM_ADDR(_esmp)						\
	((_esmp)->esm_addr)

#define	EFSYS_MEM_IS_NULL(_esmp)					\
	((_esmp)->esm_base == NULL)


#define	EFSYS_MEM_ZERO(_esmp, _size)					\
	do {								\
		(void) memset((_esmp)->esm_base, 0, (_size));		\
									\
	_NOTE(CONSTANTCONDITION)					\
	} while (B_FALSE)

#define	EFSYS_MEM_READD(_esmp, _offset, _edp)				\
	do {								\
		uint32_t *addr;						\
									\
		_NOTE(CONSTANTCONDITION)				\
		KASSERT(IS_P2ALIGNED(_offset, sizeof (efx_dword_t)),	\
		    ("not power of 2 aligned"));			\
									\
		addr = (void *)((_esmp)->esm_base + (_offset));		\
									\
		(_edp)->ed_u32[0] = *addr;				\
									\
		EFSYS_PROBE2(mem_readd, unsigned int, (_offset),	\
		    uint32_t, (_edp)->ed_u32[0]);			\
									\
	_NOTE(CONSTANTCONDITION)					\
	} while (B_FALSE)

#if defined(__x86_64__)
#define	EFSYS_MEM_READQ(_esmp, _offset, _eqp)				\
	do {								\
		uint64_t *addr;						\
									\
		_NOTE(CONSTANTCONDITION)				\
		KASSERT(IS_P2ALIGNED(_offset, sizeof (efx_qword_t)),	\
		    ("not power of 2 aligned"));			\
									\
		addr = (void *)((_esmp)->esm_base + (_offset));		\
									\
		(_eqp)->eq_u64[0] = *addr;				\
									\
		EFSYS_PROBE3(mem_readq, unsigned int, (_offset),	\
		    uint32_t, (_eqp)->eq_u32[1],			\
		    uint32_t, (_eqp)->eq_u32[0]);			\
									\
	_NOTE(CONSTANTCONDITION)					\
	} while (B_FALSE)
#else
#define	EFSYS_MEM_READQ(_esmp, _offset, _eqp)				\
	do {								\
		uint32_t *addr;						\
									\
		_NOTE(CONSTANTCONDITION)				\
		KASSERT(IS_P2ALIGNED(_offset, sizeof (efx_qword_t)),	\
		    ("not power of 2 aligned"));			\
									\
		addr = (void *)((_esmp)->esm_base + (_offset));		\
									\
		(_eqp)->eq_u32[0] = *addr++;				\
		(_eqp)->eq_u32[1] = *addr;				\
									\
		EFSYS_PROBE3(mem_readq, unsigned int, (_offset),	\
		    uint32_t, (_eqp)->eq_u32[1],			\
		    uint32_t, (_eqp)->eq_u32[0]);			\
									\
	_NOTE(CONSTANTCONDITION)					\
	} while (B_FALSE)
#endif

#if defined(__x86_64__)
#define	EFSYS_MEM_READO(_esmp, _offset, _eop)				\
	do {								\
		uint64_t *addr;						\
									\
		_NOTE(CONSTANTCONDITION)				\
		KASSERT(IS_P2ALIGNED(_offset, sizeof (efx_oword_t)),	\
		    ("not power of 2 aligned"));			\
									\
		addr = (void *)((_esmp)->esm_base + (_offset));		\
									\
		(_eop)->eo_u64[0] = *addr++;				\
		(_eop)->eo_u64[1] = *addr;				\
									\
		EFSYS_PROBE5(mem_reado, unsigned int, (_offset),	\
		    uint32_t, (_eop)->eo_u32[3],			\
		    uint32_t, (_eop)->eo_u32[2],			\
		    uint32_t, (_eop)->eo_u32[1],			\
		    uint32_t, (_eop)->eo_u32[0]);			\
									\
	_NOTE(CONSTANTCONDITION)					\
	} while (B_FALSE)
#else
#define	EFSYS_MEM_READO(_esmp, _offset, _eop)				\
	do {								\
		uint32_t *addr;						\
									\
		_NOTE(CONSTANTCONDITION)				\
		KASSERT(IS_P2ALIGNED(_offset, sizeof (efx_oword_t)),	\
		    ("not power of 2 aligned"));			\
									\
		addr = (void *)((_esmp)->esm_base + (_offset));		\
									\
		(_eop)->eo_u32[0] = *addr++;				\
		(_eop)->eo_u32[1] = *addr++;				\
		(_eop)->eo_u32[2] = *addr++;				\
		(_eop)->eo_u32[3] = *addr;				\
									\
		EFSYS_PROBE5(mem_reado, unsigned int, (_offset),	\
		    uint32_t, (_eop)->eo_u32[3],			\
		    uint32_t, (_eop)->eo_u32[2],			\
		    uint32_t, (_eop)->eo_u32[1],			\
		    uint32_t, (_eop)->eo_u32[0]);			\
									\
	_NOTE(CONSTANTCONDITION)					\
	} while (B_FALSE)
#endif

#define	EFSYS_MEM_WRITED(_esmp, _offset, _edp)				\
	do {								\
		uint32_t *addr;						\
									\
		_NOTE(CONSTANTCONDITION)				\
		KASSERT(IS_P2ALIGNED(_offset, sizeof (efx_dword_t)),	\
		    ("not power of 2 aligned"));			\
									\
		EFSYS_PROBE2(mem_writed, unsigned int, (_offset),	\
		    uint32_t, (_edp)->ed_u32[0]);			\
									\
		addr = (void *)((_esmp)->esm_base + (_offset));		\
									\
		*addr = (_edp)->ed_u32[0];				\
									\
	_NOTE(CONSTANTCONDITION)					\
	} while (B_FALSE)

#if defined(__x86_64__)
#define	EFSYS_MEM_WRITEQ(_esmp, _offset, _eqp)				\
	do {								\
		uint64_t *addr;						\
									\
		_NOTE(CONSTANTCONDITION)				\
		KASSERT(IS_P2ALIGNED(_offset, sizeof (efx_qword_t)),	\
		    ("not power of 2 aligned"));			\
									\
		EFSYS_PROBE3(mem_writeq, unsigned int, (_offset),	\
		    uint32_t, (_eqp)->eq_u32[1],			\
		    uint32_t, (_eqp)->eq_u32[0]);			\
									\
		addr = (void *)((_esmp)->esm_base + (_offset));		\
									\
		*addr   = (_eqp)->eq_u64[0];				\
									\
	_NOTE(CONSTANTCONDITION)					\
	} while (B_FALSE)

#else
#define	EFSYS_MEM_WRITEQ(_esmp, _offset, _eqp)				\
	do {								\
		uint32_t *addr;						\
									\
		_NOTE(CONSTANTCONDITION)				\
		KASSERT(IS_P2ALIGNED(_offset, sizeof (efx_qword_t)),	\
		    ("not power of 2 aligned"));			\
									\
		EFSYS_PROBE3(mem_writeq, unsigned int, (_offset),	\
		    uint32_t, (_eqp)->eq_u32[1],			\
		    uint32_t, (_eqp)->eq_u32[0]);			\
									\
		addr = (void *)((_esmp)->esm_base + (_offset));		\
									\
		*addr++ = (_eqp)->eq_u32[0];				\
		*addr   = (_eqp)->eq_u32[1];				\
									\
	_NOTE(CONSTANTCONDITION)					\
	} while (B_FALSE)
#endif

#if defined(__x86_64__)
#define	EFSYS_MEM_WRITEO(_esmp, _offset, _eop)				\
	do {								\
		uint64_t *addr;						\
									\
		_NOTE(CONSTANTCONDITION)				\
		KASSERT(IS_P2ALIGNED(_offset, sizeof (efx_oword_t)),	\
		    ("not power of 2 aligned"));			\
									\
		EFSYS_PROBE5(mem_writeo, unsigned int, (_offset),	\
		    uint32_t, (_eop)->eo_u32[3],			\
		    uint32_t, (_eop)->eo_u32[2],			\
		    uint32_t, (_eop)->eo_u32[1],			\
		    uint32_t, (_eop)->eo_u32[0]);			\
									\
		addr = (void *)((_esmp)->esm_base + (_offset));		\
									\
		*addr++ = (_eop)->eo_u64[0];				\
		*addr   = (_eop)->eo_u64[1];				\
									\
	_NOTE(CONSTANTCONDITION)					\
	} while (B_FALSE)
#else
#define	EFSYS_MEM_WRITEO(_esmp, _offset, _eop)				\
	do {								\
		uint32_t *addr;						\
									\
		_NOTE(CONSTANTCONDITION)				\
		KASSERT(IS_P2ALIGNED(_offset, sizeof (efx_oword_t)),	\
		    ("not power of 2 aligned"));			\
									\
		EFSYS_PROBE5(mem_writeo, unsigned int, (_offset),	\
		    uint32_t, (_eop)->eo_u32[3],			\
		    uint32_t, (_eop)->eo_u32[2],			\
		    uint32_t, (_eop)->eo_u32[1],			\
		    uint32_t, (_eop)->eo_u32[0]);			\
									\
		addr = (void *)((_esmp)->esm_base + (_offset));		\
									\
		*addr++ = (_eop)->eo_u32[0];				\
		*addr++ = (_eop)->eo_u32[1];				\
		*addr++ = (_eop)->eo_u32[2];				\
		*addr   = (_eop)->eo_u32[3];				\
									\
	_NOTE(CONSTANTCONDITION)					\
	} while (B_FALSE)
#endif

/* BAR */

#define	SFXGE_LOCK_NAME_MAX	16

typedef struct efsys_bar_s {
	struct mtx		esb_lock;
	char			esb_lock_name[SFXGE_LOCK_NAME_MAX];
	bus_space_tag_t		esb_tag;
	bus_space_handle_t	esb_handle;
	int			esb_rid;
	struct resource		*esb_res;
} efsys_bar_t;

#define	SFXGE_BAR_LOCK_INIT(_esbp, _ifname)				\
	do {								\
		snprintf((_esbp)->esb_lock_name,			\
			 sizeof((_esbp)->esb_lock_name),		\
			 "%s:bar", (_ifname));				\
		mtx_init(&(_esbp)->esb_lock, (_esbp)->esb_lock_name,	\
			 NULL, MTX_DEF);				\
	_NOTE(CONSTANTCONDITION)					\
	} while (B_FALSE)
#define	SFXGE_BAR_LOCK_DESTROY(_esbp)					\
	mtx_destroy(&(_esbp)->esb_lock)
#define	SFXGE_BAR_LOCK(_esbp)						\
	mtx_lock(&(_esbp)->esb_lock)
#define	SFXGE_BAR_UNLOCK(_esbp)						\
	mtx_unlock(&(_esbp)->esb_lock)

#define	EFSYS_BAR_READD(_esbp, _offset, _edp, _lock)			\
	do {								\
		_NOTE(CONSTANTCONDITION)				\
		KASSERT(IS_P2ALIGNED(_offset, sizeof (efx_dword_t)),	\
		    ("not power of 2 aligned"));			\
									\
		_NOTE(CONSTANTCONDITION)				\
		if (_lock)						\
			SFXGE_BAR_LOCK(_esbp);				\
									\
		(_edp)->ed_u32[0] = bus_space_read_stream_4(		\
		    (_esbp)->esb_tag, (_esbp)->esb_handle,		\
		    (_offset));						\
									\
		EFSYS_PROBE2(bar_readd, unsigned int, (_offset),	\
		    uint32_t, (_edp)->ed_u32[0]);			\
									\
		_NOTE(CONSTANTCONDITION)				\
		if (_lock)						\
			SFXGE_BAR_UNLOCK(_esbp);			\
	_NOTE(CONSTANTCONDITION)					\
	} while (B_FALSE)

#if defined(SFXGE_USE_BUS_SPACE_8)
#define	EFSYS_BAR_READQ(_esbp, _offset, _eqp)				\
	do {								\
		_NOTE(CONSTANTCONDITION)				\
		KASSERT(IS_P2ALIGNED(_offset, sizeof (efx_qword_t)),	\
		    ("not power of 2 aligned"));			\
									\
		SFXGE_BAR_LOCK(_esbp);					\
									\
		(_eqp)->eq_u64[0] = bus_space_read_stream_8(		\
		    (_esbp)->esb_tag, (_esbp)->esb_handle,		\
		    (_offset));						\
									\
		EFSYS_PROBE3(bar_readq, unsigned int, (_offset),	\
		    uint32_t, (_eqp)->eq_u32[1],			\
		    uint32_t, (_eqp)->eq_u32[0]);			\
									\
		SFXGE_BAR_UNLOCK(_esbp);				\
	_NOTE(CONSTANTCONDITION)					\
	} while (B_FALSE)

#define	EFSYS_BAR_READO(_esbp, _offset, _eop, _lock)			\
	do {								\
		_NOTE(CONSTANTCONDITION)				\
		KASSERT(IS_P2ALIGNED(_offset, sizeof (efx_oword_t)),	\
		    ("not power of 2 aligned"));			\
									\
		_NOTE(CONSTANTCONDITION)				\
		if (_lock)						\
			SFXGE_BAR_LOCK(_esbp);				\
									\
		(_eop)->eo_u64[0] = bus_space_read_stream_8(		\
		    (_esbp)->esb_tag, (_esbp)->esb_handle,		\
		    (_offset));						\
		(_eop)->eo_u64[1] = bus_space_read_stream_8(		\
		    (_esbp)->esb_tag, (_esbp)->esb_handle,		\
		    (_offset) + 8);					\
									\
		EFSYS_PROBE5(bar_reado, unsigned int, (_offset),	\
		    uint32_t, (_eop)->eo_u32[3],			\
		    uint32_t, (_eop)->eo_u32[2],			\
		    uint32_t, (_eop)->eo_u32[1],			\
		    uint32_t, (_eop)->eo_u32[0]);			\
									\
		_NOTE(CONSTANTCONDITION)				\
		if (_lock)						\
			SFXGE_BAR_UNLOCK(_esbp);			\
	_NOTE(CONSTANTCONDITION)					\
	} while (B_FALSE)

#else
#define	EFSYS_BAR_READQ(_esbp, _offset, _eqp)				\
	do {								\
		_NOTE(CONSTANTCONDITION)				\
		KASSERT(IS_P2ALIGNED(_offset, sizeof (efx_qword_t)),	\
		    ("not power of 2 aligned"));			\
									\
		SFXGE_BAR_LOCK(_esbp);					\
									\
		(_eqp)->eq_u32[0] = bus_space_read_stream_4(		\
		    (_esbp)->esb_tag, (_esbp)->esb_handle,		\
		    (_offset));						\
		(_eqp)->eq_u32[1] = bus_space_read_stream_4(		\
		    (_esbp)->esb_tag, (_esbp)->esb_handle,		\
		    (_offset) + 4);					\
									\
		EFSYS_PROBE3(bar_readq, unsigned int, (_offset),	\
		    uint32_t, (_eqp)->eq_u32[1],			\
		    uint32_t, (_eqp)->eq_u32[0]);			\
									\
		SFXGE_BAR_UNLOCK(_esbp);				\
	_NOTE(CONSTANTCONDITION)					\
	} while (B_FALSE)

#define	EFSYS_BAR_READO(_esbp, _offset, _eop, _lock)			\
	do {								\
		_NOTE(CONSTANTCONDITION)				\
		KASSERT(IS_P2ALIGNED(_offset, sizeof (efx_oword_t)),	\
		    ("not power of 2 aligned"));			\
									\
		_NOTE(CONSTANTCONDITION)				\
		if (_lock)						\
			SFXGE_BAR_LOCK(_esbp);				\
									\
		(_eop)->eo_u32[0] = bus_space_read_stream_4(		\
		    (_esbp)->esb_tag, (_esbp)->esb_handle,		\
		    (_offset));						\
		(_eop)->eo_u32[1] = bus_space_read_stream_4(		\
		    (_esbp)->esb_tag, (_esbp)->esb_handle,		\
		    (_offset) + 4);					\
		(_eop)->eo_u32[2] = bus_space_read_stream_4(		\
		    (_esbp)->esb_tag, (_esbp)->esb_handle,		\
		    (_offset) + 8);					\
		(_eop)->eo_u32[3] = bus_space_read_stream_4(		\
		    (_esbp)->esb_tag, (_esbp)->esb_handle,		\
		    (_offset) + 12);					\
									\
		EFSYS_PROBE5(bar_reado, unsigned int, (_offset),	\
		    uint32_t, (_eop)->eo_u32[3],			\
		    uint32_t, (_eop)->eo_u32[2],			\
		    uint32_t, (_eop)->eo_u32[1],			\
		    uint32_t, (_eop)->eo_u32[0]);			\
									\
		_NOTE(CONSTANTCONDITION)				\
		if (_lock)						\
			SFXGE_BAR_UNLOCK(_esbp);			\
	_NOTE(CONSTANTCONDITION)					\
	} while (B_FALSE)
#endif

#define	EFSYS_BAR_WRITED(_esbp, _offset, _edp, _lock)			\
	do {								\
		_NOTE(CONSTANTCONDITION)				\
		KASSERT(IS_P2ALIGNED(_offset, sizeof (efx_dword_t)),	\
		    ("not power of 2 aligned"));			\
									\
		_NOTE(CONSTANTCONDITION)				\
		if (_lock)						\
			SFXGE_BAR_LOCK(_esbp);				\
									\
		EFSYS_PROBE2(bar_writed, unsigned int, (_offset),	\
		    uint32_t, (_edp)->ed_u32[0]);			\
									\
		/*							\
		 * Make sure that previous writes to the dword have	\
		 * been done. It should be cheaper than barrier just	\
		 * after the write below.				\
		 */							\
		bus_space_barrier((_esbp)->esb_tag, (_esbp)->esb_handle,\
		    (_offset), sizeof (efx_dword_t),			\
		    BUS_SPACE_BARRIER_WRITE);				\
		bus_space_write_stream_4((_esbp)->esb_tag,		\
		    (_esbp)->esb_handle,				\
		    (_offset), (_edp)->ed_u32[0]);			\
									\
		_NOTE(CONSTANTCONDITION)				\
		if (_lock)						\
			SFXGE_BAR_UNLOCK(_esbp);			\
	_NOTE(CONSTANTCONDITION)					\
	} while (B_FALSE)

#if defined(SFXGE_USE_BUS_SPACE_8)
#define	EFSYS_BAR_WRITEQ(_esbp, _offset, _eqp)				\
	do {								\
		_NOTE(CONSTANTCONDITION)				\
		KASSERT(IS_P2ALIGNED(_offset, sizeof (efx_qword_t)),	\
		    ("not power of 2 aligned"));			\
									\
		SFXGE_BAR_LOCK(_esbp);					\
									\
		EFSYS_PROBE3(bar_writeq, unsigned int, (_offset),	\
		    uint32_t, (_eqp)->eq_u32[1],			\
		    uint32_t, (_eqp)->eq_u32[0]);			\
									\
		/*							\
		 * Make sure that previous writes to the qword have	\
		 * been done. It should be cheaper than barrier just	\
		 * after the write below.				\
		 */							\
		bus_space_barrier((_esbp)->esb_tag, (_esbp)->esb_handle,\
		    (_offset), sizeof (efx_qword_t),			\
		    BUS_SPACE_BARRIER_WRITE);				\
		bus_space_write_stream_8((_esbp)->esb_tag,		\
		    (_esbp)->esb_handle,				\
		    (_offset), (_eqp)->eq_u64[0]);			\
									\
		SFXGE_BAR_UNLOCK(_esbp);				\
	_NOTE(CONSTANTCONDITION)					\
	} while (B_FALSE)
#else
#define	EFSYS_BAR_WRITEQ(_esbp, _offset, _eqp)				\
	do {								\
		_NOTE(CONSTANTCONDITION)				\
		KASSERT(IS_P2ALIGNED(_offset, sizeof (efx_qword_t)),	\
		    ("not power of 2 aligned"));			\
									\
		SFXGE_BAR_LOCK(_esbp);					\
									\
		EFSYS_PROBE3(bar_writeq, unsigned int, (_offset),	\
		    uint32_t, (_eqp)->eq_u32[1],			\
		    uint32_t, (_eqp)->eq_u32[0]);			\
									\
		/*							\
		 * Make sure that previous writes to the qword have	\
		 * been done. It should be cheaper than barrier just	\
		 * after the last write below.				\
		 */							\
		bus_space_barrier((_esbp)->esb_tag, (_esbp)->esb_handle,\
		    (_offset), sizeof (efx_qword_t),			\
		    BUS_SPACE_BARRIER_WRITE);				\
		bus_space_write_stream_4((_esbp)->esb_tag,		\
		    (_esbp)->esb_handle,				\
		    (_offset), (_eqp)->eq_u32[0]);			\
		/*							\
		 * It should be guaranteed that the last dword comes	\
		 * the last, so barrier entire qword to be sure that	\
		 * neither above nor below writes are reordered.	\
		 */							\
		bus_space_barrier((_esbp)->esb_tag, (_esbp)->esb_handle,\
		    (_offset), sizeof (efx_qword_t),			\
		    BUS_SPACE_BARRIER_WRITE);				\
		bus_space_write_stream_4((_esbp)->esb_tag,		\
		    (_esbp)->esb_handle,				\
		    (_offset) + 4, (_eqp)->eq_u32[1]);			\
									\
		SFXGE_BAR_UNLOCK(_esbp);				\
	_NOTE(CONSTANTCONDITION)					\
	} while (B_FALSE)
#endif

/*
 * Guarantees 64bit aligned 64bit writes to write combined BAR mapping
 * (required by PIO hardware)
 */
#define	EFSYS_BAR_WC_WRITEQ(_esbp, _offset, _eqp)			\
	do {								\
		_NOTE(CONSTANTCONDITION)				\
		KASSERT(IS_P2ALIGNED(_offset, sizeof (efx_qword_t)),	\
		    ("not power of 2 aligned"));			\
									\
		(void) (_esbp);						\
									\
		/* FIXME: Perform a 64-bit write */			\
		KASSERT(0, ("not implemented"));			\
									\
	_NOTE(CONSTANTCONDITION)					\
	} while (B_FALSE)

#if defined(SFXGE_USE_BUS_SPACE_8)
#define	EFSYS_BAR_WRITEO(_esbp, _offset, _eop, _lock)			\
	do {								\
		_NOTE(CONSTANTCONDITION)				\
		KASSERT(IS_P2ALIGNED(_offset, sizeof (efx_oword_t)),	\
		    ("not power of 2 aligned"));			\
									\
		_NOTE(CONSTANTCONDITION)				\
		if (_lock)						\
			SFXGE_BAR_LOCK(_esbp);				\
									\
		EFSYS_PROBE5(bar_writeo, unsigned int, (_offset),	\
		    uint32_t, (_eop)->eo_u32[3],			\
		    uint32_t, (_eop)->eo_u32[2],			\
		    uint32_t, (_eop)->eo_u32[1],			\
		    uint32_t, (_eop)->eo_u32[0]);			\
									\
		/*							\
		 * Make sure that previous writes to the oword have	\
		 * been done. It should be cheaper than barrier just	\
		 * after the last write below.				\
		 */							\
		bus_space_barrier((_esbp)->esb_tag, (_esbp)->esb_handle,\
		    (_offset), sizeof (efx_oword_t),			\
		    BUS_SPACE_BARRIER_WRITE);				\
		bus_space_write_stream_8((_esbp)->esb_tag,		\
		    (_esbp)->esb_handle,				\
		    (_offset), (_eop)->eo_u64[0]);			\
		/*							\
		 * It should be guaranteed that the last qword comes	\
		 * the last, so barrier entire oword to be sure that	\
		 * neither above nor below writes are reordered.	\
		 */							\
		bus_space_barrier((_esbp)->esb_tag, (_esbp)->esb_handle,\
		    (_offset), sizeof (efx_oword_t),			\
		    BUS_SPACE_BARRIER_WRITE);				\
		bus_space_write_stream_8((_esbp)->esb_tag,		\
		    (_esbp)->esb_handle,				\
		    (_offset) + 8, (_eop)->eo_u64[1]);			\
									\
		_NOTE(CONSTANTCONDITION)				\
		if (_lock)						\
			SFXGE_BAR_UNLOCK(_esbp);			\
	_NOTE(CONSTANTCONDITION)					\
	} while (B_FALSE)

#else
#define	EFSYS_BAR_WRITEO(_esbp, _offset, _eop, _lock)			\
	do {								\
		_NOTE(CONSTANTCONDITION)				\
		KASSERT(IS_P2ALIGNED(_offset, sizeof (efx_oword_t)),	\
		    ("not power of 2 aligned"));			\
									\
		_NOTE(CONSTANTCONDITION)				\
		if (_lock)						\
			SFXGE_BAR_LOCK(_esbp);				\
									\
		EFSYS_PROBE5(bar_writeo, unsigned int, (_offset),	\
		    uint32_t, (_eop)->eo_u32[3],			\
		    uint32_t, (_eop)->eo_u32[2],			\
		    uint32_t, (_eop)->eo_u32[1],			\
		    uint32_t, (_eop)->eo_u32[0]);			\
									\
		/*							\
		 * Make sure that previous writes to the oword have	\
		 * been done. It should be cheaper than barrier just	\
		 * after the last write below.				\
		 */							\
		bus_space_barrier((_esbp)->esb_tag, (_esbp)->esb_handle,\
		    (_offset), sizeof (efx_oword_t),			\
		    BUS_SPACE_BARRIER_WRITE);				\
		bus_space_write_stream_4((_esbp)->esb_tag,		\
		    (_esbp)->esb_handle,				\
		    (_offset), (_eop)->eo_u32[0]);			\
		bus_space_write_stream_4((_esbp)->esb_tag,		\
		    (_esbp)->esb_handle,				\
		    (_offset) + 4, (_eop)->eo_u32[1]);			\
		bus_space_write_stream_4((_esbp)->esb_tag,		\
		    (_esbp)->esb_handle,				\
		    (_offset) + 8, (_eop)->eo_u32[2]);			\
		/*							\
		 * It should be guaranteed that the last dword comes	\
		 * the last, so barrier entire oword to be sure that	\
		 * neither above nor below writes are reordered.	\
		 */							\
		bus_space_barrier((_esbp)->esb_tag, (_esbp)->esb_handle,\
		    (_offset), sizeof (efx_oword_t),			\
		    BUS_SPACE_BARRIER_WRITE);				\
		bus_space_write_stream_4((_esbp)->esb_tag,		\
		    (_esbp)->esb_handle,				\
		    (_offset) + 12, (_eop)->eo_u32[3]);			\
									\
		_NOTE(CONSTANTCONDITION)				\
		if (_lock)						\
			SFXGE_BAR_UNLOCK(_esbp);			\
	_NOTE(CONSTANTCONDITION)					\
	} while (B_FALSE)
#endif

/* Use the standard octo-word write for doorbell writes */
#define	EFSYS_BAR_DOORBELL_WRITEO(_esbp, _offset, _eop)			\
	do {								\
		EFSYS_BAR_WRITEO((_esbp), (_offset), (_eop), B_FALSE);	\
	_NOTE(CONSTANTCONDITION)					\
	} while (B_FALSE)

/* SPIN */

#define	EFSYS_SPIN(_us)							\
	do {								\
		DELAY(_us);						\
	_NOTE(CONSTANTCONDITION)					\
	} while (B_FALSE)

#define	EFSYS_SLEEP	EFSYS_SPIN

/* BARRIERS */

#define	EFSYS_MEM_READ_BARRIER()	rmb()
#define	EFSYS_PIO_WRITE_BARRIER()

/* DMA SYNC */
#define	EFSYS_DMA_SYNC_FOR_KERNEL(_esmp, _offset, _size)		\
	do {								\
		bus_dmamap_sync((_esmp)->esm_tag,			\
		    (_esmp)->esm_map,					\
		    BUS_DMASYNC_POSTREAD);				\
	_NOTE(CONSTANTCONDITION)					\
	} while (B_FALSE)

#define	EFSYS_DMA_SYNC_FOR_DEVICE(_esmp, _offset, _size)		\
	do {								\
		bus_dmamap_sync((_esmp)->esm_tag,			\
		    (_esmp)->esm_map,					\
		    BUS_DMASYNC_PREWRITE);				\
	_NOTE(CONSTANTCONDITION)					\
	} while (B_FALSE)

/* TIMESTAMP */

typedef	clock_t	efsys_timestamp_t;

#define	EFSYS_TIMESTAMP(_usp)						\
	do {								\
		clock_t now;						\
									\
		now = ticks;						\
		*(_usp) = now * hz / 1000000;				\
	_NOTE(CONSTANTCONDITION)					\
	} while (B_FALSE)

/* KMEM */

#define	EFSYS_KMEM_ALLOC(_esip, _size, _p)				\
	do {								\
		(_esip) = (_esip);					\
		/*							\
		 * The macro is used in non-sleepable contexts, for	\
		 * example, holding a mutex.				\
		 */							\
		(_p) = malloc((_size), M_SFXGE, M_NOWAIT|M_ZERO);	\
	_NOTE(CONSTANTCONDITION)					\
	} while (B_FALSE)

#define	EFSYS_KMEM_FREE(_esip, _size, _p)				\
	do {								\
		(void) (_esip);						\
		(void) (_size);						\
		free((_p), M_SFXGE);					\
	_NOTE(CONSTANTCONDITION)					\
	} while (B_FALSE)

/* LOCK */

typedef struct efsys_lock_s {
	struct mtx	lock;
	char		lock_name[SFXGE_LOCK_NAME_MAX];
} efsys_lock_t;

#define	SFXGE_EFSYS_LOCK_INIT(_eslp, _ifname, _label)			\
	do {								\
		efsys_lock_t *__eslp = (_eslp);				\
									\
		snprintf((__eslp)->lock_name,				\
			 sizeof((__eslp)->lock_name),			\
			 "%s:%s", (_ifname), (_label));			\
		mtx_init(&(__eslp)->lock, (__eslp)->lock_name,		\
			 NULL, MTX_DEF);				\
	} while (B_FALSE)
#define	SFXGE_EFSYS_LOCK_DESTROY(_eslp)					\
	mtx_destroy(&(_eslp)->lock)
#define	SFXGE_EFSYS_LOCK(_eslp)						\
	mtx_lock(&(_eslp)->lock)
#define	SFXGE_EFSYS_UNLOCK(_eslp)					\
	mtx_unlock(&(_eslp)->lock)
#define	SFXGE_EFSYS_LOCK_ASSERT_OWNED(_eslp)				\
	mtx_assert(&(_eslp)->lock, MA_OWNED)

typedef int efsys_lock_state_t;

#define	EFSYS_LOCK_MAGIC	0x000010c4

#define	EFSYS_LOCK(_lockp, _state)					\
	do {								\
		SFXGE_EFSYS_LOCK(_lockp);				\
		(_state) = EFSYS_LOCK_MAGIC;				\
	_NOTE(CONSTANTCONDITION)					\
	} while (B_FALSE)

#define	EFSYS_UNLOCK(_lockp, _state)					\
	do {								\
		if ((_state) != EFSYS_LOCK_MAGIC)			\
			KASSERT(B_FALSE, ("not locked"));		\
		SFXGE_EFSYS_UNLOCK(_lockp);				\
	_NOTE(CONSTANTCONDITION)					\
	} while (B_FALSE)

/* STAT */

typedef uint64_t		efsys_stat_t;

#define	EFSYS_STAT_INCR(_knp, _delta) 					\
	do {								\
		*(_knp) += (_delta);					\
	_NOTE(CONSTANTCONDITION)					\
	} while (B_FALSE)

#define	EFSYS_STAT_DECR(_knp, _delta) 					\
	do {								\
		*(_knp) -= (_delta);					\
	_NOTE(CONSTANTCONDITION)					\
	} while (B_FALSE)

#define	EFSYS_STAT_SET(_knp, _val)					\
	do {								\
		*(_knp) = (_val);					\
	_NOTE(CONSTANTCONDITION)					\
	} while (B_FALSE)

#define	EFSYS_STAT_SET_QWORD(_knp, _valp)				\
	do {								\
		*(_knp) = le64toh((_valp)->eq_u64[0]);			\
	_NOTE(CONSTANTCONDITION)					\
	} while (B_FALSE)

#define	EFSYS_STAT_SET_DWORD(_knp, _valp)				\
	do {								\
		*(_knp) = le32toh((_valp)->ed_u32[0]);			\
	_NOTE(CONSTANTCONDITION)					\
	} while (B_FALSE)

#define	EFSYS_STAT_INCR_QWORD(_knp, _valp)				\
	do {								\
		*(_knp) += le64toh((_valp)->eq_u64[0]);			\
	_NOTE(CONSTANTCONDITION)					\
	} while (B_FALSE)

#define	EFSYS_STAT_SUBR_QWORD(_knp, _valp)				\
	do {								\
		*(_knp) -= le64toh((_valp)->eq_u64[0]);			\
	_NOTE(CONSTANTCONDITION)					\
	} while (B_FALSE)

/* ERR */

extern void	sfxge_err(efsys_identifier_t *, unsigned int,
		    uint32_t, uint32_t);

#if EFSYS_OPT_DECODE_INTR_FATAL
#define	EFSYS_ERR(_esip, _code, _dword0, _dword1)			\
	do {								\
		sfxge_err((_esip), (_code), (_dword0), (_dword1));	\
	_NOTE(CONSTANTCONDITION)					\
	} while (B_FALSE)
#endif

/* ASSERT */

#define	EFSYS_ASSERT(_exp) do {						\
	if (!(_exp))							\
		panic("%s", #_exp);					\
	} while (0)

#define	EFSYS_ASSERT3(_x, _op, _y, _t) do {				\
	const _t __x = (_t)(_x);					\
	const _t __y = (_t)(_y);					\
	if (!(__x _op __y))						\
		panic("assertion failed at %s:%u", __FILE__, __LINE__);	\
	} while(0)

#define	EFSYS_ASSERT3U(_x, _op, _y)	EFSYS_ASSERT3(_x, _op, _y, uint64_t)
#define	EFSYS_ASSERT3S(_x, _op, _y)	EFSYS_ASSERT3(_x, _op, _y, int64_t)
#define	EFSYS_ASSERT3P(_x, _op, _y)	EFSYS_ASSERT3(_x, _op, _y, uintptr_t)

/* ROTATE */

#define	EFSYS_HAS_ROTL_DWORD 0

#ifdef	__cplusplus
}
#endif

#endif	/* _SYS_EFSYS_H */
