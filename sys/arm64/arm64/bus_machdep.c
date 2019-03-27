/*-
 * Copyright (c) 2014 Andrew Turner
 * All rights reserved.
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
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE AUTHOR OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 *
 */

#include "opt_platform.h"

#include <sys/param.h>
__FBSDID("$FreeBSD$");

#include <vm/vm.h>
#include <vm/pmap.h>

#include <machine/bus.h>

uint8_t  generic_bs_r_1(void *, bus_space_handle_t, bus_size_t);
uint16_t generic_bs_r_2(void *, bus_space_handle_t, bus_size_t);
uint32_t generic_bs_r_4(void *, bus_space_handle_t, bus_size_t);
uint64_t generic_bs_r_8(void *, bus_space_handle_t, bus_size_t);

void generic_bs_rm_1(void *, bus_space_handle_t, bus_size_t, uint8_t *,
    bus_size_t);
void generic_bs_rm_2(void *, bus_space_handle_t, bus_size_t, uint16_t *,
    bus_size_t);
void generic_bs_rm_4(void *, bus_space_handle_t, bus_size_t, uint32_t *,
    bus_size_t);
void generic_bs_rm_8(void *, bus_space_handle_t, bus_size_t, uint64_t *,
    bus_size_t);

void generic_bs_rr_1(void *, bus_space_handle_t, bus_size_t, uint8_t *,
    bus_size_t);
void generic_bs_rr_2(void *, bus_space_handle_t, bus_size_t, uint16_t *,
    bus_size_t);
void generic_bs_rr_4(void *, bus_space_handle_t, bus_size_t, uint32_t *,
    bus_size_t);
void generic_bs_rr_8(void *, bus_space_handle_t, bus_size_t, uint64_t *,
    bus_size_t);

void generic_bs_w_1(void *, bus_space_handle_t, bus_size_t, uint8_t);
void generic_bs_w_2(void *, bus_space_handle_t, bus_size_t, uint16_t);
void generic_bs_w_4(void *, bus_space_handle_t, bus_size_t, uint32_t);
void generic_bs_w_8(void *, bus_space_handle_t, bus_size_t, uint64_t);

void generic_bs_wm_1(void *, bus_space_handle_t, bus_size_t, const uint8_t *,
    bus_size_t);
void generic_bs_wm_2(void *, bus_space_handle_t, bus_size_t, const uint16_t *,
    bus_size_t);
void generic_bs_wm_4(void *, bus_space_handle_t, bus_size_t, const uint32_t *,
    bus_size_t);
void generic_bs_wm_8(void *, bus_space_handle_t, bus_size_t, const uint64_t *,
    bus_size_t);

void generic_bs_wr_1(void *, bus_space_handle_t, bus_size_t, const uint8_t *,
    bus_size_t);
void generic_bs_wr_2(void *, bus_space_handle_t, bus_size_t, const uint16_t *,
    bus_size_t);
void generic_bs_wr_4(void *, bus_space_handle_t, bus_size_t, const uint32_t *,
    bus_size_t);
void generic_bs_wr_8(void *, bus_space_handle_t, bus_size_t, const uint64_t *,
    bus_size_t);

static int
generic_bs_map(void *t, bus_addr_t bpa, bus_size_t size, int flags,
    bus_space_handle_t *bshp)
{
	void *va;

	va = pmap_mapdev(bpa, size);
	if (va == NULL)
		return (ENOMEM);
	*bshp = (bus_space_handle_t)va;
	return (0);
}

static void
generic_bs_unmap(void *t, bus_space_handle_t bsh, bus_size_t size)
{

	pmap_unmapdev(bsh, size);
}

static void
generic_bs_barrier(void *t, bus_space_handle_t bsh, bus_size_t offset,
    bus_size_t size, int flags)
{
}

static int
generic_bs_subregion(void *t, bus_space_handle_t bsh, bus_size_t offset,
    bus_size_t size, bus_space_handle_t *nbshp)
{

	*nbshp = bsh + offset;
	return (0);
}

struct bus_space memmap_bus = {
	/* cookie */
	.bs_cookie = NULL,

	/* mapping/unmapping */
	.bs_map = generic_bs_map,
	.bs_unmap = generic_bs_unmap,
	.bs_subregion = generic_bs_subregion,

	/* allocation/deallocation */
	.bs_alloc = NULL,
	.bs_free = NULL,

	/* barrier */
	.bs_barrier = generic_bs_barrier,

	/* read single */
	.bs_r_1 = generic_bs_r_1,
	.bs_r_2 = generic_bs_r_2,
	.bs_r_4 = generic_bs_r_4,
	.bs_r_8 = generic_bs_r_8,

	/* read multiple */
	.bs_rm_1 = generic_bs_rm_1,
	.bs_rm_2 = generic_bs_rm_2,
	.bs_rm_4 = generic_bs_rm_4,
	.bs_rm_8 = generic_bs_rm_8,

	/* read region */
	.bs_rr_1 = generic_bs_rr_1,
	.bs_rr_2 = generic_bs_rr_2,
	.bs_rr_4 = generic_bs_rr_4,
	.bs_rr_8 = generic_bs_rr_8,

	/* write single */
	.bs_w_1 = generic_bs_w_1,
	.bs_w_2 = generic_bs_w_2,
	.bs_w_4 = generic_bs_w_4,
	.bs_w_8 = generic_bs_w_8,

	/* write multiple */
	.bs_wm_1 = generic_bs_wm_1,
	.bs_wm_2 = generic_bs_wm_2,
	.bs_wm_4 = generic_bs_wm_4,
	.bs_wm_8 = generic_bs_wm_8,

	/* write region */
	.bs_wr_1 = generic_bs_wr_1,
	.bs_wr_2 = generic_bs_wr_2,
	.bs_wr_4 = generic_bs_wr_4,
	.bs_wr_8 = generic_bs_wr_8,

	/* set multiple */
	.bs_sm_1 = NULL,
	.bs_sm_2 = NULL,
	.bs_sm_4 = NULL,
	.bs_sm_8 = NULL,

	/* set region */
	.bs_sr_1 = NULL,
	.bs_sr_2 = NULL,
	.bs_sr_4 = NULL,
	.bs_sr_8 = NULL,

	/* copy */
	.bs_c_1 = NULL,
	.bs_c_2 = NULL,
	.bs_c_4 = NULL,
	.bs_c_8 = NULL,

	/* read single stream */
	.bs_r_1_s = NULL,
	.bs_r_2_s = NULL,
	.bs_r_4_s = NULL,
	.bs_r_8_s = NULL,

	/* read multiple stream */
	.bs_rm_1_s = generic_bs_rm_1,
	.bs_rm_2_s = generic_bs_rm_2,
	.bs_rm_4_s = generic_bs_rm_4,
	.bs_rm_8_s = generic_bs_rm_8,

	/* read region stream */
	.bs_rr_1_s = NULL,
	.bs_rr_2_s = NULL,
	.bs_rr_4_s = NULL,
	.bs_rr_8_s = NULL,

	/* write single stream */
	.bs_w_1_s = NULL,
	.bs_w_2_s = NULL,
	.bs_w_4_s = NULL,
	.bs_w_8_s = NULL,

	/* write multiple stream */
	.bs_wm_1_s = generic_bs_wm_1,
	.bs_wm_2_s = generic_bs_wm_2,
	.bs_wm_4_s = generic_bs_wm_4,
	.bs_wm_8_s = generic_bs_wm_8,

	/* write region stream */
	.bs_wr_1_s = NULL,
	.bs_wr_2_s = NULL,
	.bs_wr_4_s = NULL,
	.bs_wr_8_s = NULL,
};

#ifdef FDT
bus_space_tag_t fdtbus_bs_tag = &memmap_bus;
#endif
