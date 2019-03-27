/*	$NetBSD: bus.h,v 1.11 2003/07/28 17:35:54 thorpej Exp $	*/

/*-
 * SPDX-License-Identifier: BSD-2-Clause-NetBSD AND BSD-4-Clause
 *
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

#define BUS_SPACE_ALIGNED_POINTER(p, t) ALIGNED_POINTER(p, t)

#define BUS_SPACE_MAXADDR_24BIT	0xFFFFFFUL
#define BUS_SPACE_MAXADDR_32BIT 0xFFFFFFFFUL
#define BUS_SPACE_MAXSIZE_24BIT	0xFFFFFFUL
#define BUS_SPACE_MAXSIZE_32BIT	0xFFFFFFFFUL

#ifdef __powerpc64__
#define BUS_SPACE_MAXADDR 	0xFFFFFFFFFFFFFFFFUL
#define BUS_SPACE_MAXSIZE 	0xFFFFFFFFFFFFFFFFUL
#else
#ifdef BOOKE
#define BUS_SPACE_MAXADDR 	0xFFFFFFFFFULL
#define BUS_SPACE_MAXSIZE 	0xFFFFFFFFUL
#else
#define BUS_SPACE_MAXADDR 	0xFFFFFFFFUL
#define BUS_SPACE_MAXSIZE 	0xFFFFFFFFUL
#endif
#endif

#define	BUS_SPACE_MAP_CACHEABLE		0x01
#define	BUS_SPACE_MAP_LINEAR		0x02
#define	BUS_SPACE_MAP_PREFETCHABLE     	0x04

#define	BUS_SPACE_UNRESTRICTED	(~0)

#define	BUS_SPACE_BARRIER_READ	0x01
#define	BUS_SPACE_BARRIER_WRITE	0x02

struct bus_space_access;

struct bus_space {
	/* mapping/unmapping */
	int	(*bs_map)(bus_addr_t, bus_size_t, int,
	    bus_space_handle_t *);
	void	(*bs_unmap)(bus_size_t);
	int	(*bs_subregion)(bus_space_handle_t, bus_size_t,
	    bus_size_t, bus_space_handle_t *);

	/* allocation/deallocation */
	int	(*bs_alloc)(bus_addr_t, bus_addr_t, bus_size_t,
	    bus_size_t, bus_size_t, int, bus_addr_t *, bus_space_handle_t *);
	void	(*bs_free)(bus_space_handle_t, bus_size_t);

	void	(*bs_barrier)(bus_space_handle_t, bus_size_t,
	    bus_size_t, int);

	/* Read single. */
	uint8_t (*bs_r_1)(bus_space_handle_t, bus_size_t);
	uint16_t (*bs_r_2)(bus_space_handle_t, bus_size_t);
	uint32_t (*bs_r_4)(bus_space_handle_t, bus_size_t);
	uint64_t (*bs_r_8)(bus_space_handle_t, bus_size_t);

	uint16_t (*bs_r_s_2)(bus_space_handle_t, bus_size_t);
	uint32_t (*bs_r_s_4)(bus_space_handle_t, bus_size_t);
	uint64_t (*bs_r_s_8)(bus_space_handle_t, bus_size_t);

	/* read multiple */
	void	(*bs_rm_1)(bus_space_handle_t, bus_size_t, uint8_t *,
	    bus_size_t);
	void	(*bs_rm_2)(bus_space_handle_t, bus_size_t, uint16_t *,
	    bus_size_t);
	void	(*bs_rm_4)(bus_space_handle_t, bus_size_t, uint32_t *,
	    bus_size_t);
	void	(*bs_rm_8)(bus_space_handle_t, bus_size_t, uint64_t *,
	    bus_size_t);

	void	(*bs_rm_s_2)(bus_space_handle_t, bus_size_t, uint16_t *,
	    bus_size_t);
	void	(*bs_rm_s_4)(bus_space_handle_t, bus_size_t, uint32_t *,
	    bus_size_t);
	void	(*bs_rm_s_8)(bus_space_handle_t, bus_size_t, uint64_t *,
	    bus_size_t);

	/* read region */
	void	(*bs_rr_1)(bus_space_handle_t, bus_size_t, uint8_t *,
	    bus_size_t);
	void	(*bs_rr_2)(bus_space_handle_t, bus_size_t, uint16_t *,
	    bus_size_t);
	void	(*bs_rr_4)(bus_space_handle_t, bus_size_t, uint32_t *,
	    bus_size_t);
	void	(*bs_rr_8)(bus_space_handle_t, bus_size_t, uint64_t *,
	    bus_size_t);

	void	(*bs_rr_s_2)(bus_space_handle_t, bus_size_t, uint16_t *,
	    bus_size_t);
	void	(*bs_rr_s_4)(bus_space_handle_t, bus_size_t, uint32_t *,
	    bus_size_t);
	void	(*bs_rr_s_8)(bus_space_handle_t, bus_size_t, uint64_t *,
	    bus_size_t);

	/* write */
	void	(*bs_w_1)(bus_space_handle_t, bus_size_t, uint8_t);
	void	(*bs_w_2)(bus_space_handle_t, bus_size_t, uint16_t);
	void	(*bs_w_4)(bus_space_handle_t, bus_size_t, uint32_t);
	void	(*bs_w_8)(bus_space_handle_t, bus_size_t, uint64_t);

	void	(*bs_w_s_2)(bus_space_handle_t, bus_size_t, uint16_t);
	void	(*bs_w_s_4)(bus_space_handle_t, bus_size_t, uint32_t);
	void	(*bs_w_s_8)(bus_space_handle_t, bus_size_t, uint64_t);

	/* write multiple */
	void	(*bs_wm_1)(bus_space_handle_t, bus_size_t,
	    const uint8_t *, bus_size_t);
	void	(*bs_wm_2)(bus_space_handle_t, bus_size_t,
	    const uint16_t *, bus_size_t);
	void	(*bs_wm_4)(bus_space_handle_t, bus_size_t,
	    const uint32_t *, bus_size_t);
	void	(*bs_wm_8)(bus_space_handle_t, bus_size_t,
	    const uint64_t *, bus_size_t);

	void	(*bs_wm_s_2)(bus_space_handle_t, bus_size_t,
	    const uint16_t *, bus_size_t);
	void	(*bs_wm_s_4)(bus_space_handle_t, bus_size_t,
	    const uint32_t *, bus_size_t);
	void	(*bs_wm_s_8)(bus_space_handle_t, bus_size_t,
	    const uint64_t *, bus_size_t);

	/* write region */
	void	(*bs_wr_1)(bus_space_handle_t, bus_size_t,
	    const uint8_t *, bus_size_t);
	void	(*bs_wr_2)(bus_space_handle_t, bus_size_t,
	    const uint16_t *, bus_size_t);
	void	(*bs_wr_4)(bus_space_handle_t, bus_size_t,
	    const uint32_t *, bus_size_t);
	void	(*bs_wr_8)(bus_space_handle_t, bus_size_t,
	    const uint64_t *, bus_size_t);

	void	(*bs_wr_s_2)(bus_space_handle_t, bus_size_t,
	    const uint16_t *, bus_size_t);
	void	(*bs_wr_s_4)(bus_space_handle_t, bus_size_t,
	    const uint32_t *, bus_size_t);
	void	(*bs_wr_s_8)(bus_space_handle_t, bus_size_t,
	    const uint64_t *, bus_size_t);

	/* set multiple */
	void	(*bs_sm_1)(bus_space_handle_t, bus_size_t, uint8_t,
	    bus_size_t);
	void	(*bs_sm_2)(bus_space_handle_t, bus_size_t, uint16_t,
	    bus_size_t);
	void	(*bs_sm_4)(bus_space_handle_t, bus_size_t, uint32_t,
	    bus_size_t);
	void	(*bs_sm_8)(bus_space_handle_t, bus_size_t, uint64_t,
	    bus_size_t);

	void	(*bs_sm_s_2)(bus_space_handle_t, bus_size_t, uint16_t,
	    bus_size_t);
	void	(*bs_sm_s_4)(bus_space_handle_t, bus_size_t, uint32_t,
	    bus_size_t);
	void	(*bs_sm_s_8)(bus_space_handle_t, bus_size_t, uint64_t,
	    bus_size_t);

	/* set region */
	void	(*bs_sr_1)(bus_space_handle_t, bus_size_t, uint8_t,
	    bus_size_t);
	void	(*bs_sr_2)(bus_space_handle_t, bus_size_t, uint16_t,
	    bus_size_t);
	void	(*bs_sr_4)(bus_space_handle_t, bus_size_t, uint32_t,
	    bus_size_t);
	void	(*bs_sr_8)(bus_space_handle_t, bus_size_t, uint64_t,
	    bus_size_t);

	void	(*bs_sr_s_2)(bus_space_handle_t, bus_size_t, uint16_t,
	    bus_size_t);
	void	(*bs_sr_s_4)(bus_space_handle_t, bus_size_t, uint32_t,
	    bus_size_t);
	void	(*bs_sr_s_8)(bus_space_handle_t, bus_size_t, uint64_t,
	    bus_size_t);

	/* copy region */
	void	(*bs_cr_1)(bus_space_handle_t, bus_size_t,
	    bus_space_handle_t, bus_size_t, bus_size_t);
	void	(*bs_cr_2)(bus_space_handle_t, bus_size_t,
	    bus_space_handle_t, bus_size_t, bus_size_t);
	void	(*bs_cr_4)(bus_space_handle_t, bus_size_t,
	    bus_space_handle_t, bus_size_t, bus_size_t);
	void	(*bs_cr_8)(bus_space_handle_t, bus_size_t,
	    bus_space_handle_t, bus_size_t, bus_size_t);

	void	(*bs_cr_s_2)(bus_space_handle_t, bus_size_t,
	    bus_space_handle_t, bus_size_t, bus_size_t);
	void	(*bs_cr_s_4)(bus_space_handle_t, bus_size_t,
	    bus_space_handle_t, bus_size_t, bus_size_t);
	void	(*bs_cr_s_8)(bus_space_handle_t, bus_size_t,
	    bus_space_handle_t, bus_size_t, bus_size_t);
};

extern struct bus_space bs_be_tag;
extern struct bus_space bs_le_tag;

#define	__bs_c(a,b)		__CONCAT(a,b)
#define	__bs_opname(op,size)	__bs_c(__bs_c(__bs_c(bs_,op),_),size)

#define	__bs_rs(sz, t, h, o)						\
	(*(t)->__bs_opname(r,sz))(h, o)
#define	__bs_ws(sz, t, h, o, v)				\
	(*(t)->__bs_opname(w,sz))(h, o, v)
#define	__bs_nonsingle(type, sz, t, h, o, a, c)				\
	(*(t)->__bs_opname(type,sz))(h, o, a, c)
#define	__bs_set(type, sz, t, h, o, v, c)				\
	(*(t)->__bs_opname(type,sz))(h, o, v, c)
#define	__bs_copy(sz, t, h1, o1, h2, o2, cnt)				\
	(*(t)->__bs_opname(c,sz))(h1, o1, h2, o2, cnt)

/*
 * Mapping and unmapping operations.
 */
#define bus_space_map(t, a, s, c, hp) (*(t)->bs_map)(a, s, c, hp)
#define bus_space_unmap(t, h, s)	(*(t)->bs_unmap)(h, s)
#define	bus_space_subregion(t, h, o, s, hp)	(*(t)->bs_subregion)(h, o, s, hp)

/*
 * Allocation and deallocation operations.
 */
#define	bus_space_alloc(t, rs, re, s, a, b, c, ap, hp)	\
	(*(t)->bs_alloc)(rs, re, s, a, b, c, ap, hp)
#define	bus_space_free(t, h, s)				\
	(*(t)->bs_free)(h, s)

/*
 * Bus barrier operations.
 */
#define	bus_space_barrier(t, h, o, l, f)	(*(t)->bs_barrier)(h, o, l, f)

/*
 * Bus read (single) operations.
 */
#define	bus_space_read_1(t, h, o)	__bs_rs(1,t,h,o)
#define	bus_space_read_2(t, h, o)	__bs_rs(2,t,h,o)
#define	bus_space_read_4(t, h, o)	__bs_rs(4,t,h,o)
#define	bus_space_read_8(t, h, o)	__bs_rs(8,t,h,o)

#define bus_space_read_stream_1 bus_space_read_1
#define	bus_space_read_stream_2(t, h, o)	__bs_rs(s_2,t,h,o)
#define	bus_space_read_stream_4(t, h, o)	__bs_rs(s_4,t,h,o)
#define	bus_space_read_stream_8(t, h, o)	__bs_rs(s_8,t,h,o)

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

#define bus_space_read_multi_stream_1 bus_space_read_multi_1
#define	bus_space_read_multi_stream_2(t, h, o, a, c)			\
	__bs_nonsingle(rm,s_2,(t),(h),(o),(a),(c))
#define	bus_space_read_multi_stream_4(t, h, o, a, c)			\
	__bs_nonsingle(rm,s_4,(t),(h),(o),(a),(c))
#define	bus_space_read_multi_stream_8(t, h, o, a, c)			\
	__bs_nonsingle(rm,s_8,(t),(h),(o),(a),(c))

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

#define bus_space_read_region_stream_1 bus_space_read_region_1
#define	bus_space_read_region_stream_2(t, h, o, a, c)			\
	__bs_nonsingle(rr,s_2,(t),(h),(o),(a),(c))
#define	bus_space_read_region_stream_4(t, h, o, a, c)			\
	__bs_nonsingle(rr,s_4,(t),(h),(o),(a),(c))
#define	bus_space_read_region_stream_8(t, h, o, a, c)			\
	__bs_nonsingle(rr,s_8,(t),(h),(o),(a),(c))

/*
 * Bus write (single) operations.
 */
#define	bus_space_write_1(t, h, o, v)	__bs_ws(1,(t),(h),(o),(v))
#define	bus_space_write_2(t, h, o, v)	__bs_ws(2,(t),(h),(o),(v))
#define	bus_space_write_4(t, h, o, v)	__bs_ws(4,(t),(h),(o),(v))
#define	bus_space_write_8(t, h, o, v)	__bs_ws(8,(t),(h),(o),(v))

#define bus_space_write_stream_1 bus_space_write_1
#define	bus_space_write_stream_2(t, h, o, v)	__bs_ws(s_2,(t),(h),(o),(v))
#define	bus_space_write_stream_4(t, h, o, v)	__bs_ws(s_4,(t),(h),(o),(v))
#define	bus_space_write_stream_8(t, h, o, v)	__bs_ws(s_8,(t),(h),(o),(v))

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

#define bus_space_write_multi_stream_1 bus_space_write_multi_1
#define	bus_space_write_multi_stream_2(t, h, o, a, c)			\
	__bs_nonsingle(wm,s_2,(t),(h),(o),(a),(c))
#define	bus_space_write_multi_stream_4(t, h, o, a, c)			\
	__bs_nonsingle(wm,s_4,(t),(h),(o),(a),(c))
#define	bus_space_write_multi_stream_8(t, h, o, a, c)			\
	__bs_nonsingle(wm,s_8,(t),(h),(o),(a),(c))

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

#define bus_space_write_region_stream_1 bus_space_write_region_1
#define	bus_space_write_region_stream_2(t, h, o, a, c)			\
	__bs_nonsingle(wr,s_2,(t),(h),(o),(a),(c))
#define	bus_space_write_region_stream_4(t, h, o, a, c)			\
	__bs_nonsingle(wr,s_4,(t),(h),(o),(a),(c))
#define	bus_space_write_region_stream_8(t, h, o, a, c)			\
	__bs_nonsingle(wr,s_8,(t),(h),(o),(a),(c))

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

#define bus_space_set_multi_stream_1 bus_space_set_multi_1
#define	bus_space_set_multi_stream_2(t, h, o, v, c)			\
	__bs_set(sm,s_2,(t),(h),(o),(v),(c))
#define	bus_space_set_multi_stream_4(t, h, o, v, c)			\
	__bs_set(sm,s_4,(t),(h),(o),(v),(c))
#define	bus_space_set_multi_stream_8(t, h, o, v, c)			\
	__bs_set(sm,s_8,(t),(h),(o),(v),(c))

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

#define bus_space_set_region_stream_1 bus_space_set_region_1
#define	bus_space_set_region_stream_2(t, h, o, v, c)			\
	__bs_set(sr,s_2,(t),(h),(o),(v),(c))
#define	bus_space_set_region_stream_4(t, h, o, v, c)			\
	__bs_set(sr,s_4,(t),(h),(o),(v),(c))
#define	bus_space_set_region_stream_8(t, h, o, v, c)			\
	__bs_set(sr,s_8,(t),(h),(o),(v),(c))

#if 0
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

#define bus_space_copy_region_stream_1 bus_space_copy_region_1
#define	bus_space_copy_region_stream_2(t, h1, o1, h2, o2, c)			\
	__bs_copy(s_2, t, h1, o1, h2, o2, c)
#define	bus_space_copy_region_stream_4(t, h1, o1, h2, o2, c)			\
	__bs_copy(s_4, t, h1, o1, h2, o2, c)
#define	bus_space_copy_region_stream_8(t, h1, o1, h2, o2, c)			\
	__bs_copy(s_8, t, h1, o1, h2, o2, c)
#endif

#include <machine/bus_dma.h>

#endif /* _MACHINE_BUS_H_ */
