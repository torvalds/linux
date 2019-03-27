/*-
 * Copyright (C) 2008 MARVELL INTERNATIONAL LTD.
 * All rights reserved.
 *
 * Developed by Semihalf.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 * 3. Neither the name of MARVELL nor the names of contributors
 *    may be used to endorse or promote products derived from this software
 *    without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY AUTHOR AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL AUTHOR OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 */

#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");

#include <sys/param.h>
#include <sys/systm.h>
#include <machine/bus.h>

#include "opt_platform.h"

/* Prototypes for all the bus_space structure functions */
bs_protos(generic);

/*
 * The bus space tag.  This is constant for all instances, so
 * we never have to explicitly "create" it.
 */
static struct bus_space arm_base_bus_space __aligned(CACHE_LINE_SIZE) = {
	/* privdata is whatever the implementer wants; unused in base tag */
	.bs_privdata	= NULL,

	/* mapping/unmapping */
	.bs_map		= generic_bs_map,
	.bs_unmap	= generic_bs_unmap,
	.bs_subregion	= generic_bs_subregion,

	/* allocation/deallocation */
	.bs_alloc	= generic_bs_alloc,
	.bs_free	= generic_bs_free,

	/* barrier */
	.bs_barrier	= generic_bs_barrier,

	/* read (single) */
	.bs_r_1		= NULL,	/* Use inline code in bus.h */
	.bs_r_2		= NULL,	/* Use inline code in bus.h */
	.bs_r_4		= NULL,	/* Use inline code in bus.h */
	.bs_r_8		= NULL,	/* Use inline code in bus.h */

	/* read multiple */
	.bs_rm_1	= generic_bs_rm_1,
	.bs_rm_2	= generic_bs_rm_2,
	.bs_rm_4	= generic_bs_rm_4,
	.bs_rm_8	= BS_UNIMPLEMENTED,

	/* read region */
	.bs_rr_1	= generic_bs_rr_1,
	.bs_rr_2	= generic_bs_rr_2,
	.bs_rr_4	= generic_bs_rr_4,
	.bs_rr_8	= BS_UNIMPLEMENTED,

	/* write (single) */
	.bs_w_1		= NULL,	/* Use inline code in bus.h */
	.bs_w_2		= NULL,	/* Use inline code in bus.h */
	.bs_w_4		= NULL,	/* Use inline code in bus.h */
	.bs_w_8		= NULL,	/* Use inline code in bus.h */

	/* write multiple */
	.bs_wm_1	= generic_bs_wm_1,
	.bs_wm_2	= generic_bs_wm_2,
	.bs_wm_4	= generic_bs_wm_4,
	.bs_wm_8	= BS_UNIMPLEMENTED,

	/* write region */
	.bs_wr_1	= generic_bs_wr_1,
	.bs_wr_2	= generic_bs_wr_2,
	.bs_wr_4	= generic_bs_wr_4,
	.bs_wr_8	= BS_UNIMPLEMENTED,

	/* set multiple */
	.bs_sm_1	= BS_UNIMPLEMENTED,
	.bs_sm_2	= BS_UNIMPLEMENTED,
	.bs_sm_4	= BS_UNIMPLEMENTED,
	.bs_sm_8	= BS_UNIMPLEMENTED,

	/* set region */
	.bs_sr_1	= generic_bs_sr_1,
	.bs_sr_2	= generic_bs_sr_2,
	.bs_sr_4	= generic_bs_sr_4,
	.bs_sr_8	= BS_UNIMPLEMENTED,

	/* copy */
	.bs_c_1		= BS_UNIMPLEMENTED,
	.bs_c_2		= generic_bs_c_2,
	.bs_c_4		= BS_UNIMPLEMENTED,
	.bs_c_8		= BS_UNIMPLEMENTED,

	/* read stream (single) */
	.bs_r_1_s	= NULL,   /* Use inline code in bus.h */
	.bs_r_2_s	= NULL,   /* Use inline code in bus.h */
	.bs_r_4_s	= NULL,   /* Use inline code in bus.h */
	.bs_r_8_s	= NULL,   /* Use inline code in bus.h */

	/* read multiple stream */
	.bs_rm_1_s	= generic_bs_rm_1,
	.bs_rm_2_s	= generic_bs_rm_2,
	.bs_rm_4_s	= generic_bs_rm_4,
	.bs_rm_8_s	= BS_UNIMPLEMENTED,

	/* read region stream */
	.bs_rr_1_s	= generic_bs_rr_1,
	.bs_rr_2_s	= generic_bs_rr_2,
	.bs_rr_4_s	= generic_bs_rr_4,
	.bs_rr_8_s	= BS_UNIMPLEMENTED,

	/* write stream (single) */
	.bs_w_1_s	= NULL,   /* Use inline code in bus.h */
	.bs_w_2_s	= NULL,   /* Use inline code in bus.h */
	.bs_w_4_s	= NULL,   /* Use inline code in bus.h */
	.bs_w_8_s	= NULL,   /* Use inline code in bus.h */

	/* write multiple stream */
	.bs_wm_1_s	= generic_bs_wm_1,
	.bs_wm_2_s	= generic_bs_wm_2,
	.bs_wm_4_s	= generic_bs_wm_4,
	.bs_wm_8_s	= BS_UNIMPLEMENTED,

	/* write region stream */
	.bs_wr_1_s	= generic_bs_wr_1,
	.bs_wr_2_s	= generic_bs_wr_2,
	.bs_wr_4_s	= generic_bs_wr_4,
	.bs_wr_8_s	= BS_UNIMPLEMENTED,
};

#ifdef FDT
bus_space_tag_t fdtbus_bs_tag = &arm_base_bus_space;
#endif

#if __ARM_ARCH < 6
bus_space_tag_t arm_base_bs_tag = &arm_base_bus_space;
#endif
