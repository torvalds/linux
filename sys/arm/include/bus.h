/*	$NetBSD: bus.h,v 1.11 2003/07/28 17:35:54 thorpej Exp $	*/

/*-
 * Copyright (c) 1996, 1997, 1998, 2001 The NetBSD Foundation, Inc.
 * All rights reserved.
 *
 * This code is derived from software contributed to The NetBSD Foundation
 * by Jason R. Thorpe of the Numerical Aerospace Simulation Facility,
 * NASA Ames Research Center.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE NETBSD FOUNDATION, INC. AND CONTRIBUTORS
 * ``AS IS'' AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED
 * TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR
 * PURPOSE ARE DISCLAIMED.  IN NO EVENT SHALL THE FOUNDATION OR CONTRIBUTORS
 * BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
 * CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 * SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
 * INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
 * CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 * ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
 * POSSIBILITY OF SUCH DAMAGE.
 */

/*-
 * SPDX-License-Identifier: BSD-4-Clause
 *
 * Copyright (c) 1996 Charles M. Hannum.  All rights reserved.
 * Copyright (c) 1996 Christopher G. Demetriou.  All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 * 3. All advertising materials mentioning features or use of this software
 *    must display the following acknowledgement:
 *      This product includes software developed by Christopher G. Demetriou
 *	for the NetBSD Project.
 * 4. The name of the author may not be used to endorse or promote products
 *    derived from this software without specific prior written permission
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR ``AS IS'' AND ANY EXPRESS OR
 * IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES
 * OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED.
 * IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR ANY DIRECT, INDIRECT,
 * INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT
 * NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 * DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
 * THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF
 * THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 *
 * $FreeBSD$
 */

#ifndef _MACHINE_BUS_H_
#define _MACHINE_BUS_H_

#include <machine/_bus.h>

/*
 *	int bus_space_map  (bus_space_tag_t t, bus_addr_t addr,
 *	    bus_size_t size, int flags, bus_space_handle_t *bshp);
 *
 * Map a region of bus space.
 */

#define	BUS_SPACE_MAP_CACHEABLE		0x01
#define	BUS_SPACE_MAP_LINEAR		0x02
#define	BUS_SPACE_MAP_PREFETCHABLE     	0x04

/*
 * Bus space for ARM.
 *
 * The functions used most often are grouped together at the beginning to ensure
 * that all the data fits into a single cache line.  The inline implementations
 * of single read/write access these values a lot.
 */
struct bus_space {
	/* Read/write single and barrier: the most commonly used functions. */
	uint8_t	 (*bs_r_1)(bus_space_tag_t, bus_space_handle_t, bus_size_t);
	uint32_t (*bs_r_4)(bus_space_tag_t, bus_space_handle_t, bus_size_t);
	void	 (*bs_w_1)(bus_space_tag_t, bus_space_handle_t,
			   bus_size_t, uint8_t);
	void	 (*bs_w_4)(bus_space_tag_t, bus_space_handle_t,
			   bus_size_t, uint32_t);
	void	 (*bs_barrier)(bus_space_tag_t, bus_space_handle_t,
			       bus_size_t, bus_size_t, int);

	/* Backlink to parent (if copied), and implementation private data. */
	struct bus_space *bs_parent;
	void		 *bs_privdata;

	/* mapping/unmapping */
	int		(*bs_map) (bus_space_tag_t, bus_addr_t, bus_size_t,
			    int, bus_space_handle_t *);
	void		(*bs_unmap) (bus_space_tag_t, bus_space_handle_t, bus_size_t);
	int		(*bs_subregion) (bus_space_tag_t, bus_space_handle_t,
			    bus_size_t, bus_size_t, bus_space_handle_t *);

	/* allocation/deallocation */
	int		(*bs_alloc) (bus_space_tag_t, bus_addr_t, bus_addr_t,
			    bus_size_t, bus_size_t, bus_size_t, int,
			    bus_addr_t *, bus_space_handle_t *);
	void		(*bs_free) (bus_space_tag_t, bus_space_handle_t,
			    bus_size_t);

	/* Read single, the less commonly used functions. */
	uint16_t	(*bs_r_2) (bus_space_tag_t, bus_space_handle_t, bus_size_t);
	uint64_t	(*bs_r_8) (bus_space_tag_t, bus_space_handle_t, bus_size_t);

	/* read multiple */
	void		(*bs_rm_1) (bus_space_tag_t, bus_space_handle_t, bus_size_t,
	    uint8_t *, bus_size_t);
	void		(*bs_rm_2) (bus_space_tag_t, bus_space_handle_t, bus_size_t,
	    uint16_t *, bus_size_t);
	void		(*bs_rm_4) (bus_space_tag_t, bus_space_handle_t,
			    bus_size_t, uint32_t *, bus_size_t);
	void		(*bs_rm_8) (bus_space_tag_t, bus_space_handle_t,
			    bus_size_t, uint64_t *, bus_size_t);

	/* read region */
	void		(*bs_rr_1) (bus_space_tag_t, bus_space_handle_t,
			    bus_size_t, uint8_t *, bus_size_t);
	void		(*bs_rr_2) (bus_space_tag_t, bus_space_handle_t,
			    bus_size_t, uint16_t *, bus_size_t);
	void		(*bs_rr_4) (bus_space_tag_t, bus_space_handle_t,
			    bus_size_t, uint32_t *, bus_size_t);
	void		(*bs_rr_8) (bus_space_tag_t, bus_space_handle_t,
			    bus_size_t, uint64_t *, bus_size_t);

	/* Write single, the less commonly used functions. */
	void		(*bs_w_2) (bus_space_tag_t, bus_space_handle_t,
			    bus_size_t, uint16_t);
	void		(*bs_w_8) (bus_space_tag_t, bus_space_handle_t,
			    bus_size_t, uint64_t);

	/* write multiple */
	void		(*bs_wm_1) (bus_space_tag_t, bus_space_handle_t,
			    bus_size_t, const uint8_t *, bus_size_t);
	void		(*bs_wm_2) (bus_space_tag_t, bus_space_handle_t,
			    bus_size_t, const uint16_t *, bus_size_t);
	void		(*bs_wm_4) (bus_space_tag_t, bus_space_handle_t,
			    bus_size_t, const uint32_t *, bus_size_t);
	void		(*bs_wm_8) (bus_space_tag_t, bus_space_handle_t,
			    bus_size_t, const uint64_t *, bus_size_t);

	/* write region */
	void		(*bs_wr_1) (bus_space_tag_t, bus_space_handle_t,
			    bus_size_t, const uint8_t *, bus_size_t);
	void		(*bs_wr_2) (bus_space_tag_t, bus_space_handle_t,
			    bus_size_t, const uint16_t *, bus_size_t);
	void		(*bs_wr_4) (bus_space_tag_t, bus_space_handle_t,
			    bus_size_t, const uint32_t *, bus_size_t);
	void		(*bs_wr_8) (bus_space_tag_t, bus_space_handle_t,
			    bus_size_t, const uint64_t *, bus_size_t);

	/* set multiple */
	void		(*bs_sm_1) (bus_space_tag_t, bus_space_handle_t,
			    bus_size_t, uint8_t, bus_size_t);
	void		(*bs_sm_2) (bus_space_tag_t, bus_space_handle_t,
			    bus_size_t, uint16_t, bus_size_t);
	void		(*bs_sm_4) (bus_space_tag_t, bus_space_handle_t,
			    bus_size_t, uint32_t, bus_size_t);
	void		(*bs_sm_8) (bus_space_tag_t, bus_space_handle_t,
			    bus_size_t, uint64_t, bus_size_t);

	/* set region */
	void		(*bs_sr_1) (bus_space_tag_t, bus_space_handle_t,
			    bus_size_t, uint8_t, bus_size_t);
	void		(*bs_sr_2) (bus_space_tag_t, bus_space_handle_t,
			    bus_size_t, uint16_t, bus_size_t);
	void		(*bs_sr_4) (bus_space_tag_t, bus_space_handle_t,
			    bus_size_t, uint32_t, bus_size_t);
	void		(*bs_sr_8) (bus_space_tag_t, bus_space_handle_t,
			    bus_size_t, uint64_t, bus_size_t);

	/* copy */
	void		(*bs_c_1) (bus_space_tag_t, bus_space_handle_t, bus_size_t,
			    bus_space_handle_t, bus_size_t, bus_size_t);
	void		(*bs_c_2) (bus_space_tag_t, bus_space_handle_t, bus_size_t,
			    bus_space_handle_t, bus_size_t, bus_size_t);
	void		(*bs_c_4) (bus_space_tag_t, bus_space_handle_t, bus_size_t,
			    bus_space_handle_t, bus_size_t, bus_size_t);
	void		(*bs_c_8) (bus_space_tag_t, bus_space_handle_t, bus_size_t,
			    bus_space_handle_t, bus_size_t, bus_size_t);

	/* read stream (single) */
	uint8_t	(*bs_r_1_s) (bus_space_tag_t, bus_space_handle_t, bus_size_t);
	uint16_t	(*bs_r_2_s) (bus_space_tag_t, bus_space_handle_t, bus_size_t);
	uint32_t	(*bs_r_4_s) (bus_space_tag_t, bus_space_handle_t, bus_size_t);
	uint64_t	(*bs_r_8_s) (bus_space_tag_t, bus_space_handle_t, bus_size_t);

	/* read multiple stream */
	void		(*bs_rm_1_s) (bus_space_tag_t, bus_space_handle_t, bus_size_t,
	    uint8_t *, bus_size_t);
	void		(*bs_rm_2_s) (bus_space_tag_t, bus_space_handle_t, bus_size_t,
	    uint16_t *, bus_size_t);
	void		(*bs_rm_4_s) (bus_space_tag_t, bus_space_handle_t,
			    bus_size_t, uint32_t *, bus_size_t);
	void		(*bs_rm_8_s) (bus_space_tag_t, bus_space_handle_t,
			    bus_size_t, uint64_t *, bus_size_t);

	/* read region stream */
	void		(*bs_rr_1_s) (bus_space_tag_t, bus_space_handle_t,
			    bus_size_t, uint8_t *, bus_size_t);
	void		(*bs_rr_2_s) (bus_space_tag_t, bus_space_handle_t,
			    bus_size_t, uint16_t *, bus_size_t);
	void		(*bs_rr_4_s) (bus_space_tag_t, bus_space_handle_t,
			    bus_size_t, uint32_t *, bus_size_t);
	void		(*bs_rr_8_s) (bus_space_tag_t, bus_space_handle_t,
			    bus_size_t, uint64_t *, bus_size_t);

	/* write stream (single) */
	void		(*bs_w_1_s) (bus_space_tag_t, bus_space_handle_t,
			    bus_size_t, uint8_t);
	void		(*bs_w_2_s) (bus_space_tag_t, bus_space_handle_t,
			    bus_size_t, uint16_t);
	void		(*bs_w_4_s) (bus_space_tag_t, bus_space_handle_t,
			    bus_size_t, uint32_t);
	void		(*bs_w_8_s) (bus_space_tag_t, bus_space_handle_t,
			    bus_size_t, uint64_t);

	/* write multiple stream */
	void		(*bs_wm_1_s) (bus_space_tag_t, bus_space_handle_t,
			    bus_size_t, const uint8_t *, bus_size_t);
	void		(*bs_wm_2_s) (bus_space_tag_t, bus_space_handle_t,
			    bus_size_t, const uint16_t *, bus_size_t);
	void		(*bs_wm_4_s) (bus_space_tag_t, bus_space_handle_t,
			    bus_size_t, const uint32_t *, bus_size_t);
	void		(*bs_wm_8_s) (bus_space_tag_t, bus_space_handle_t,
			    bus_size_t, const uint64_t *, bus_size_t);

	/* write region stream */
	void		(*bs_wr_1_s) (bus_space_tag_t, bus_space_handle_t,
			    bus_size_t, const uint8_t *, bus_size_t);
	void		(*bs_wr_2_s) (bus_space_tag_t, bus_space_handle_t,
			    bus_size_t, const uint16_t *, bus_size_t);
	void		(*bs_wr_4_s) (bus_space_tag_t, bus_space_handle_t,
			    bus_size_t, const uint32_t *, bus_size_t);
	void		(*bs_wr_8_s) (bus_space_tag_t, bus_space_handle_t,
			    bus_size_t, const uint64_t *, bus_size_t);
};

#if __ARM_ARCH < 6
extern bus_space_tag_t arm_base_bs_tag;
#endif

/*
 * Utility macros; INTERNAL USE ONLY.
 */
#define	__bs_c(a,b)		__CONCAT(a,b)
#define	__bs_opname(op,size)	__bs_c(__bs_c(__bs_c(bs_,op),_),size)

#define	__bs_nonsingle(type, sz, t, h, o, a, c)				\
	(*(t)->__bs_opname(type,sz))((t), h, o, a, c)
#define	__bs_set(type, sz, t, h, o, v, c)				\
	(*(t)->__bs_opname(type,sz))((t), h, o, v, c)
#define	__bs_copy(sz, t, h1, o1, h2, o2, cnt)				\
	(*(t)->__bs_opname(c,sz))((t), h1, o1, h2, o2, cnt)

#define	__bs_opname_s(op,size)	__bs_c(__bs_c(__bs_c(__bs_c(bs_,op),_),size),_s)
#define	__bs_rs_s(sz, t, h, o)						\
	(*(t)->__bs_opname_s(r,sz))((t), h, o)
#define	__bs_ws_s(sz, t, h, o, v)					\
	(*(t)->__bs_opname_s(w,sz))((t), h, o, v)
#define	__bs_nonsingle_s(type, sz, t, h, o, a, c)			\
	(*(t)->__bs_opname_s(type,sz))((t), h, o, a, c)


#define __generate_inline_bs_rs(IFN, MBR, TYP)					\
	static inline TYP						\
	IFN(bus_space_tag_t t, bus_space_handle_t h, bus_size_t o)	\
	{								\
									\
		if (__predict_true(t->MBR == NULL))			\
			return (*(volatile TYP *)(h + o));		\
		else							\
			return (t->MBR(t, h, o));		\
	}

#define __generate_inline_bs_ws(IFN, MBR, TYP)					\
	static inline void						\
	IFN(bus_space_tag_t t, bus_space_handle_t h, bus_size_t o, TYP v)\
	{								\
									\
		if (__predict_true(t->MBR == NULL))			\
			*(volatile TYP *)(h + o) = v;			\
		else							\
			t->MBR(t, h, o, v);			\
	}

/*
 * Mapping and unmapping operations.
 */
#define	bus_space_map(t, a, s, c, hp)					\
	(*(t)->bs_map)((t), (a), (s), (c), (hp))
#define	bus_space_unmap(t, h, s)					\
	(*(t)->bs_unmap)((t), (h), (s))
#define	bus_space_subregion(t, h, o, s, hp)				\
	(*(t)->bs_subregion)((t), (h), (o), (s), (hp))


/*
 * Allocation and deallocation operations.
 */
#define	bus_space_alloc(t, rs, re, s, a, b, c, ap, hp)			\
	(*(t)->bs_alloc)((t), (rs), (re), (s), (a), (b),	\
	    (c), (ap), (hp))
#define	bus_space_free(t, h, s)						\
	(*(t)->bs_free)((t), (h), (s))

/*
 * Bus barrier operations.
 */
#define	bus_space_barrier(t, h, o, l, f)				\
	(*(t)->bs_barrier)((t), (h), (o), (l), (f))

#define	BUS_SPACE_BARRIER_READ	0x01
#define	BUS_SPACE_BARRIER_WRITE	0x02

/*
 * Bus read (single) operations.
 */
__generate_inline_bs_rs(bus_space_read_1, bs_r_1, uint8_t);
__generate_inline_bs_rs(bus_space_read_2, bs_r_2, uint16_t);
__generate_inline_bs_rs(bus_space_read_4, bs_r_4, uint32_t);
__generate_inline_bs_rs(bus_space_read_8, bs_r_8, uint64_t);

__generate_inline_bs_rs(bus_space_read_stream_1, bs_r_1_s, uint8_t);
__generate_inline_bs_rs(bus_space_read_stream_2, bs_r_2_s, uint16_t);
__generate_inline_bs_rs(bus_space_read_stream_4, bs_r_4_s, uint32_t);
__generate_inline_bs_rs(bus_space_read_stream_8, bs_r_8_s, uint64_t);

/*
 * Bus read multiple operations.
 */
#define	bus_space_read_multi_1(t, h, o, a, c)				\
	__bs_nonsingle(rm,1,(t),(h),(o),(a),(c))
#define	bus_space_read_multi_2(t, h, o, a, c)				\
	__bs_nonsingle(rm,2,(t),(h),(o),(a),(c))
#define	bus_space_read_multi_4(t, h, o, a, c)				\
	__bs_nonsingle(rm,4,(t),(h),(o),(a),(c))
#define	bus_space_read_multi_8(t, h, o, a, c)				\
	__bs_nonsingle(rm,8,(t),(h),(o),(a),(c))

#define	bus_space_read_multi_stream_1(t, h, o, a, c)			\
	__bs_nonsingle_s(rm,1,(t),(h),(o),(a),(c))
#define	bus_space_read_multi_stream_2(t, h, o, a, c)			\
	__bs_nonsingle_s(rm,2,(t),(h),(o),(a),(c))
#define	bus_space_read_multi_stream_4(t, h, o, a, c)			\
	__bs_nonsingle_s(rm,4,(t),(h),(o),(a),(c))
#define	bus_space_read_multi_stream_8(t, h, o, a, c)			\
	__bs_nonsingle_s(rm,8,(t),(h),(o),(a),(c))


/*
 * Bus read region operations.
 */
#define	bus_space_read_region_1(t, h, o, a, c)				\
	__bs_nonsingle(rr,1,(t),(h),(o),(a),(c))
#define	bus_space_read_region_2(t, h, o, a, c)				\
	__bs_nonsingle(rr,2,(t),(h),(o),(a),(c))
#define	bus_space_read_region_4(t, h, o, a, c)				\
	__bs_nonsingle(rr,4,(t),(h),(o),(a),(c))
#define	bus_space_read_region_8(t, h, o, a, c)				\
	__bs_nonsingle(rr,8,(t),(h),(o),(a),(c))

#define	bus_space_read_region_stream_1(t, h, o, a, c)			\
	__bs_nonsingle_s(rr,1,(t),(h),(o),(a),(c))
#define	bus_space_read_region_stream_2(t, h, o, a, c)			\
	__bs_nonsingle_s(rr,2,(t),(h),(o),(a),(c))
#define	bus_space_read_region_stream_4(t, h, o, a, c)			\
	__bs_nonsingle_s(rr,4,(t),(h),(o),(a),(c))
#define	bus_space_read_region_stream_8(t, h, o, a, c)			\
	__bs_nonsingle_s(rr,8,(t),(h),(o),(a),(c))


/*
 * Bus write (single) operations.
 */
__generate_inline_bs_ws(bus_space_write_1, bs_w_1, uint8_t);
__generate_inline_bs_ws(bus_space_write_2, bs_w_2, uint16_t);
__generate_inline_bs_ws(bus_space_write_4, bs_w_4, uint32_t);
__generate_inline_bs_ws(bus_space_write_8, bs_w_8, uint64_t);

__generate_inline_bs_ws(bus_space_write_stream_1, bs_w_1_s, uint8_t);
__generate_inline_bs_ws(bus_space_write_stream_2, bs_w_2_s, uint16_t);
__generate_inline_bs_ws(bus_space_write_stream_4, bs_w_4_s, uint32_t);
__generate_inline_bs_ws(bus_space_write_stream_8, bs_w_8_s, uint64_t);


/*
 * Bus write multiple operations.
 */
#define	bus_space_write_multi_1(t, h, o, a, c)				\
	__bs_nonsingle(wm,1,(t),(h),(o),(a),(c))
#define	bus_space_write_multi_2(t, h, o, a, c)				\
	__bs_nonsingle(wm,2,(t),(h),(o),(a),(c))
#define	bus_space_write_multi_4(t, h, o, a, c)				\
	__bs_nonsingle(wm,4,(t),(h),(o),(a),(c))
#define	bus_space_write_multi_8(t, h, o, a, c)				\
	__bs_nonsingle(wm,8,(t),(h),(o),(a),(c))

#define	bus_space_write_multi_stream_1(t, h, o, a, c)			\
	__bs_nonsingle_s(wm,1,(t),(h),(o),(a),(c))
#define	bus_space_write_multi_stream_2(t, h, o, a, c)			\
	__bs_nonsingle_s(wm,2,(t),(h),(o),(a),(c))
#define	bus_space_write_multi_stream_4(t, h, o, a, c)			\
	__bs_nonsingle_s(wm,4,(t),(h),(o),(a),(c))
#define	bus_space_write_multi_stream_8(t, h, o, a, c)			\
	__bs_nonsingle_s(wm,8,(t),(h),(o),(a),(c))


/*
 * Bus write region operations.
 */
#define	bus_space_write_region_1(t, h, o, a, c)				\
	__bs_nonsingle(wr,1,(t),(h),(o),(a),(c))
#define	bus_space_write_region_2(t, h, o, a, c)				\
	__bs_nonsingle(wr,2,(t),(h),(o),(a),(c))
#define	bus_space_write_region_4(t, h, o, a, c)				\
	__bs_nonsingle(wr,4,(t),(h),(o),(a),(c))
#define	bus_space_write_region_8(t, h, o, a, c)				\
	__bs_nonsingle(wr,8,(t),(h),(o),(a),(c))

#define	bus_space_write_region_stream_1(t, h, o, a, c)			\
	__bs_nonsingle_s(wr,1,(t),(h),(o),(a),(c))
#define	bus_space_write_region_stream_2(t, h, o, a, c)			\
	__bs_nonsingle_s(wr,2,(t),(h),(o),(a),(c))
#define	bus_space_write_region_stream_4(t, h, o, a, c)			\
	__bs_nonsingle_s(wr,4,(t),(h),(o),(a),(c))
#define	bus_space_write_region_stream_8(t, h, o, a, c)			\
	__bs_nonsingle_s(wr,8,(t),(h),(o),(a),(c))


/*
 * Set multiple operations.
 */
#define	bus_space_set_multi_1(t, h, o, v, c)				\
	__bs_set(sm,1,(t),(h),(o),(v),(c))
#define	bus_space_set_multi_2(t, h, o, v, c)				\
	__bs_set(sm,2,(t),(h),(o),(v),(c))
#define	bus_space_set_multi_4(t, h, o, v, c)				\
	__bs_set(sm,4,(t),(h),(o),(v),(c))
#define	bus_space_set_multi_8(t, h, o, v, c)				\
	__bs_set(sm,8,(t),(h),(o),(v),(c))


/*
 * Set region operations.
 */
#define	bus_space_set_region_1(t, h, o, v, c)				\
	__bs_set(sr,1,(t),(h),(o),(v),(c))
#define	bus_space_set_region_2(t, h, o, v, c)				\
	__bs_set(sr,2,(t),(h),(o),(v),(c))
#define	bus_space_set_region_4(t, h, o, v, c)				\
	__bs_set(sr,4,(t),(h),(o),(v),(c))
#define	bus_space_set_region_8(t, h, o, v, c)				\
	__bs_set(sr,8,(t),(h),(o),(v),(c))


/*
 * Copy operations.
 */
#define	bus_space_copy_region_1(t, h1, o1, h2, o2, c)				\
	__bs_copy(1, t, h1, o1, h2, o2, c)
#define	bus_space_copy_region_2(t, h1, o1, h2, o2, c)				\
	__bs_copy(2, t, h1, o1, h2, o2, c)
#define	bus_space_copy_region_4(t, h1, o1, h2, o2, c)				\
	__bs_copy(4, t, h1, o1, h2, o2, c)
#define	bus_space_copy_region_8(t, h1, o1, h2, o2, c)				\
	__bs_copy(8, t, h1, o1, h2, o2, c)

/*
 * Macros to provide prototypes for all the functions used in the
 * bus_space structure
 */

#define bs_map_proto(f)							\
int	__bs_c(f,_bs_map) (bus_space_tag_t t, bus_addr_t addr,		\
	    bus_size_t size, int cacheable, bus_space_handle_t *bshp);

#define bs_unmap_proto(f)						\
void	__bs_c(f,_bs_unmap) (bus_space_tag_t t, bus_space_handle_t bsh,		\
	    bus_size_t size);

#define bs_subregion_proto(f)						\
int	__bs_c(f,_bs_subregion) (bus_space_tag_t t, bus_space_handle_t bsh,	\
	    bus_size_t offset, bus_size_t size, 			\
	    bus_space_handle_t *nbshp);

#define bs_alloc_proto(f)						\
int	__bs_c(f,_bs_alloc) (bus_space_tag_t t, bus_addr_t rstart,		\
	    bus_addr_t rend, bus_size_t size, bus_size_t align,		\
	    bus_size_t boundary, int cacheable, bus_addr_t *addrp,	\
	    bus_space_handle_t *bshp);

#define bs_free_proto(f)						\
void	__bs_c(f,_bs_free) (bus_space_tag_t t, bus_space_handle_t bsh,	\
	    bus_size_t size);

#define bs_mmap_proto(f)						\
int	__bs_c(f,_bs_mmap) (struct cdev *, vm_offset_t, vm_paddr_t *, int);

#define bs_barrier_proto(f)						\
void	__bs_c(f,_bs_barrier) (bus_space_tag_t t, bus_space_handle_t bsh,	\
	    bus_size_t offset, bus_size_t len, int flags);

#define	bs_r_1_proto(f)							\
uint8_t	__bs_c(f,_bs_r_1) (bus_space_tag_t t, bus_space_handle_t bsh,	\
		    bus_size_t offset);

#define	bs_r_2_proto(f)							\
uint16_t	__bs_c(f,_bs_r_2) (bus_space_tag_t t, bus_space_handle_t bsh,	\
		    bus_size_t offset);

#define	bs_r_4_proto(f)							\
uint32_t	__bs_c(f,_bs_r_4) (bus_space_tag_t t, bus_space_handle_t bsh,	\
		    bus_size_t offset);

#define	bs_r_8_proto(f)							\
uint64_t	__bs_c(f,_bs_r_8) (bus_space_tag_t t, bus_space_handle_t bsh,	\
		    bus_size_t offset);

#define	bs_r_1_s_proto(f)						\
uint8_t	__bs_c(f,_bs_r_1_s) (bus_space_tag_t t, bus_space_handle_t bsh,	\
		    bus_size_t offset);

#define	bs_r_2_s_proto(f)						\
uint16_t	__bs_c(f,_bs_r_2_s) (bus_space_tag_t t, bus_space_handle_t bsh,	\
		    bus_size_t offset);

#define	bs_r_4_s_proto(f)						\
uint32_t	__bs_c(f,_bs_r_4_s) (bus_space_tag_t t, bus_space_handle_t bsh,	\
		    bus_size_t offset);

#define	bs_w_1_proto(f)							\
void	__bs_c(f,_bs_w_1) (bus_space_tag_t t, bus_space_handle_t bsh,		\
	    bus_size_t offset, uint8_t value);

#define	bs_w_2_proto(f)							\
void	__bs_c(f,_bs_w_2) (bus_space_tag_t t, bus_space_handle_t bsh,		\
	    bus_size_t offset, uint16_t value);

#define	bs_w_4_proto(f)							\
void	__bs_c(f,_bs_w_4) (bus_space_tag_t t, bus_space_handle_t bsh,		\
	    bus_size_t offset, uint32_t value);

#define	bs_w_8_proto(f)							\
void	__bs_c(f,_bs_w_8) (bus_space_tag_t t, bus_space_handle_t bsh,		\
	    bus_size_t offset, uint64_t value);

#define	bs_w_1_s_proto(f)						\
void	__bs_c(f,_bs_w_1_s) (bus_space_tag_t t, bus_space_handle_t bsh,		\
	    bus_size_t offset, uint8_t value);

#define	bs_w_2_s_proto(f)						\
void	__bs_c(f,_bs_w_2_s) (bus_space_tag_t t, bus_space_handle_t bsh,		\
	    bus_size_t offset, uint16_t value);

#define	bs_w_4_s_proto(f)						\
void	__bs_c(f,_bs_w_4_s) (bus_space_tag_t t, bus_space_handle_t bsh,		\
	    bus_size_t offset, uint32_t value);

#define	bs_rm_1_proto(f)						\
void	__bs_c(f,_bs_rm_1) (bus_space_tag_t t, bus_space_handle_t bsh,	\
	    bus_size_t offset, uint8_t *addr, bus_size_t count);

#define	bs_rm_2_proto(f)						\
void	__bs_c(f,_bs_rm_2) (bus_space_tag_t t, bus_space_handle_t bsh,	\
	    bus_size_t offset, uint16_t *addr, bus_size_t count);

#define	bs_rm_4_proto(f)						\
void	__bs_c(f,_bs_rm_4) (bus_space_tag_t t, bus_space_handle_t bsh,	\
	    bus_size_t offset, uint32_t *addr, bus_size_t count);

#define	bs_rm_8_proto(f)						\
void	__bs_c(f,_bs_rm_8) (bus_space_tag_t t, bus_space_handle_t bsh,	\
	    bus_size_t offset, uint64_t *addr, bus_size_t count);

#define	bs_wm_1_proto(f)						\
void	__bs_c(f,_bs_wm_1) (bus_space_tag_t t, bus_space_handle_t bsh,	\
	    bus_size_t offset, const uint8_t *addr, bus_size_t count);

#define	bs_wm_2_proto(f)						\
void	__bs_c(f,_bs_wm_2) (bus_space_tag_t t, bus_space_handle_t bsh,	\
	    bus_size_t offset, const uint16_t *addr, bus_size_t count);

#define	bs_wm_4_proto(f)						\
void	__bs_c(f,_bs_wm_4) (bus_space_tag_t t, bus_space_handle_t bsh,	\
	    bus_size_t offset, const uint32_t *addr, bus_size_t count);

#define	bs_wm_8_proto(f)						\
void	__bs_c(f,_bs_wm_8) (bus_space_tag_t t, bus_space_handle_t bsh,	\
	    bus_size_t offset, const uint64_t *addr, bus_size_t count);

#define	bs_rr_1_proto(f)						\
void	__bs_c(f, _bs_rr_1) (bus_space_tag_t t, bus_space_handle_t bsh,	\
	    bus_size_t offset, uint8_t *addr, bus_size_t count);

#define	bs_rr_2_proto(f)						\
void	__bs_c(f, _bs_rr_2) (bus_space_tag_t t, bus_space_handle_t bsh,	\
	    bus_size_t offset, uint16_t *addr, bus_size_t count);

#define	bs_rr_4_proto(f)						\
void	__bs_c(f, _bs_rr_4) (bus_space_tag_t t, bus_space_handle_t bsh,	\
	    bus_size_t offset, uint32_t *addr, bus_size_t count);

#define	bs_rr_8_proto(f)						\
void	__bs_c(f, _bs_rr_8) (bus_space_tag_t t, bus_space_handle_t bsh,	\
	    bus_size_t offset, uint64_t *addr, bus_size_t count);

#define	bs_wr_1_proto(f)						\
void	__bs_c(f, _bs_wr_1) (bus_space_tag_t t, bus_space_handle_t bsh,	\
	    bus_size_t offset, const uint8_t *addr, bus_size_t count);

#define	bs_wr_2_proto(f)						\
void	__bs_c(f, _bs_wr_2) (bus_space_tag_t t, bus_space_handle_t bsh,	\
	    bus_size_t offset, const uint16_t *addr, bus_size_t count);

#define	bs_wr_4_proto(f)						\
void	__bs_c(f, _bs_wr_4) (bus_space_tag_t t, bus_space_handle_t bsh,	\
	    bus_size_t offset, const uint32_t *addr, bus_size_t count);

#define	bs_wr_8_proto(f)						\
void	__bs_c(f, _bs_wr_8) (bus_space_tag_t t, bus_space_handle_t bsh,	\
	    bus_size_t offset, const uint64_t *addr, bus_size_t count);

#define	bs_sm_1_proto(f)						\
void	__bs_c(f,_bs_sm_1) (bus_space_tag_t t, bus_space_handle_t bsh,	\
	    bus_size_t offset, uint8_t value, bus_size_t count);

#define	bs_sm_2_proto(f)						\
void	__bs_c(f,_bs_sm_2) (bus_space_tag_t t, bus_space_handle_t bsh,	\
	    bus_size_t offset, uint16_t value, bus_size_t count);

#define	bs_sm_4_proto(f)						\
void	__bs_c(f,_bs_sm_4) (bus_space_tag_t t, bus_space_handle_t bsh,	\
	    bus_size_t offset, uint32_t value, bus_size_t count);

#define	bs_sm_8_proto(f)						\
void	__bs_c(f,_bs_sm_8) (bus_space_tag_t t, bus_space_handle_t bsh,	\
	    bus_size_t offset, uint64_t value, bus_size_t count);

#define	bs_sr_1_proto(f)						\
void	__bs_c(f,_bs_sr_1) (bus_space_tag_t t, bus_space_handle_t bsh,	\
	    bus_size_t offset, uint8_t value, bus_size_t count);

#define	bs_sr_2_proto(f)						\
void	__bs_c(f,_bs_sr_2) (bus_space_tag_t t, bus_space_handle_t bsh,	\
	    bus_size_t offset, uint16_t value, bus_size_t count);

#define	bs_sr_4_proto(f)						\
void	__bs_c(f,_bs_sr_4) (bus_space_tag_t t, bus_space_handle_t bsh,	\
	    bus_size_t offset, uint32_t value, bus_size_t count);

#define	bs_sr_8_proto(f)						\
void	__bs_c(f,_bs_sr_8) (bus_space_tag_t t, bus_space_handle_t bsh,	\
	    bus_size_t offset, uint64_t value, bus_size_t count);

#define	bs_c_1_proto(f)							\
void	__bs_c(f,_bs_c_1) (bus_space_tag_t t, bus_space_handle_t bsh1,	\
	    bus_size_t offset1, bus_space_handle_t bsh2,		\
	    bus_size_t offset2, bus_size_t count);

#define	bs_c_2_proto(f)							\
void	__bs_c(f,_bs_c_2) (bus_space_tag_t t, bus_space_handle_t bsh1,	\
	    bus_size_t offset1, bus_space_handle_t bsh2,		\
	    bus_size_t offset2, bus_size_t count);

#define	bs_c_4_proto(f)							\
void	__bs_c(f,_bs_c_4) (bus_space_tag_t t, bus_space_handle_t bsh1,	\
	    bus_size_t offset1, bus_space_handle_t bsh2,		\
	    bus_size_t offset2, bus_size_t count);

#define	bs_c_8_proto(f)							\
void	__bs_c(f,_bs_c_8) (bus_space_tag_t t, bus_space_handle_t bsh1,	\
	    bus_size_t offset1, bus_space_handle_t bsh2,		\
	    bus_size_t offset2, bus_size_t count);

#define bs_protos(f)		\
bs_map_proto(f);		\
bs_unmap_proto(f);		\
bs_subregion_proto(f);		\
bs_alloc_proto(f);		\
bs_free_proto(f);		\
bs_mmap_proto(f);		\
bs_barrier_proto(f);		\
bs_r_1_proto(f);		\
bs_r_2_proto(f);		\
bs_r_4_proto(f);		\
bs_r_8_proto(f);		\
bs_r_1_s_proto(f);		\
bs_r_2_s_proto(f);		\
bs_r_4_s_proto(f);		\
bs_w_1_proto(f);		\
bs_w_2_proto(f);		\
bs_w_4_proto(f);		\
bs_w_8_proto(f);		\
bs_w_1_s_proto(f);		\
bs_w_2_s_proto(f);		\
bs_w_4_s_proto(f);		\
bs_rm_1_proto(f);		\
bs_rm_2_proto(f);		\
bs_rm_4_proto(f);		\
bs_rm_8_proto(f);		\
bs_wm_1_proto(f);		\
bs_wm_2_proto(f);		\
bs_wm_4_proto(f);		\
bs_wm_8_proto(f);		\
bs_rr_1_proto(f);		\
bs_rr_2_proto(f);		\
bs_rr_4_proto(f);		\
bs_rr_8_proto(f);		\
bs_wr_1_proto(f);		\
bs_wr_2_proto(f);		\
bs_wr_4_proto(f);		\
bs_wr_8_proto(f);		\
bs_sm_1_proto(f);		\
bs_sm_2_proto(f);		\
bs_sm_4_proto(f);		\
bs_sm_8_proto(f);		\
bs_sr_1_proto(f);		\
bs_sr_2_proto(f);		\
bs_sr_4_proto(f);		\
bs_sr_8_proto(f);		\
bs_c_1_proto(f);		\
bs_c_2_proto(f);		\
bs_c_4_proto(f);		\
bs_c_8_proto(f);

void generic_bs_unimplemented(void);
#define	BS_UNIMPLEMENTED	(void *)generic_bs_unimplemented

#define BUS_SPACE_ALIGNED_POINTER(p, t) ALIGNED_POINTER(p, t)

#define BUS_SPACE_MAXADDR_24BIT	0xFFFFFF
#define BUS_SPACE_MAXADDR_32BIT 0xFFFFFFFF
#define BUS_SPACE_MAXADDR 	0xFFFFFFFF
#define BUS_SPACE_MAXSIZE_24BIT	0xFFFFFF
#define BUS_SPACE_MAXSIZE_32BIT	0xFFFFFFFF
#define BUS_SPACE_MAXSIZE 	0xFFFFFFFF

#define BUS_SPACE_UNRESTRICTED	(~0)

#include <machine/bus_dma.h>

/*
 * Get the physical address of a bus space memory-mapped resource.
 * Doing this as a macro is a temporary solution until a more robust fix is
 * designed.  It also serves to mark the locations needing that fix.
 */
#define BUS_SPACE_PHYSADDR(res, offs) \
	((u_int)(rman_get_start(res)+(offs)))

#endif /* _MACHINE_BUS_H_ */
