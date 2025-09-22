/*	$OpenBSD: cgtwelvereg.h,v 1.3 2024/10/22 21:50:02 jsg Exp $	*/

/*
 * Copyright (c) 2002 Miodrag Vallat.  All rights reserved.
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
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR ``AS IS'' AND ANY EXPRESS OR
 * IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED
 * WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
 * DISCLAIMED.  IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR ANY DIRECT,
 * INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES
 * (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR
 * SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT,
 * STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN
 * ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
 * POSSIBILITY OF SUCH DAMAGE.
 */

/*
 * cgtwelve (GS) accelerated 24-bit framebuffer driver.
 *
 * Memory layout and scarce register information from SMI's cg12reg.h
 */

#define	CG12_HEIGHT		900
#define	CG12_WIDTH		1152

#define	CG12_HEIGHT_HR		1024
#define	CG12_WIDTH_HR		1280

/* offsets from the card mapping */
#define	CG12_OFF_DPU		0x040100
#define	CG12_OFF_APU		0x040200
#define	CG12_OFF_DAC		0x040300
#define	CG12_OFF_OVERLAY0	0x700000
#define	CG12_OFF_OVERLAY1	0x780000
#define	CG12_OFF_INTEN		0xc00000

#define	CG12_OFF_OVERLAY0_HR	0xe00000
#define	CG12_OFF_OVERLAY1_HR	0xf00000
#define	CG12_OFF_INTEN_HR	0x800000

/* sizes of various parts */
#define	CG12_SIZE_DPU		0x000100
#define	CG12_SIZE_APU		0x000100
#define	CG12_SIZE_DAC		0x000400
#define	CG12_SIZE_OVERLAY	0x020000
#define	CG12_SIZE_ENABLE	0x020000
#define	CG12_SIZE_COLOR8	0x100000
#define	CG12_SIZE_COLOR24	0x400000

#define	CG12_SIZE_OVERLAY_HR	0x030000
#define	CG12_SIZE_ENABLE_HR	0x030000
#define	CG12_SIZE_COLOR8_HR	0x180000
#define	CG12_SIZE_COLOR24_HR	0x600000

/*
 * The "direct port access" register constants.
 * All HACCESS values include noHSTXY, noHCLIP, and SWAP.
 */

#define	CG12_HPAGE_OVERLAY	0x00000700	/* overlay page */
#define	CG12_HPAGE_OVERLAY_HR	0x00000e00
#define	CG12_HACCESS_OVERLAY	0x00000020	/* 1bit/pixel */
#define	CG12_PLN_SL_OVERLAY	0x00000017	/* plane 23 */
#define	CG12_PLN_WR_OVERLAY	0x00800000	/* write mask */
#define	CG12_PLN_RD_OVERLAY	0xffffffff	/* read mask */

#define	CG12_HPAGE_ENABLE	0x00000700	/* overlay page */
#define	CG12_HPAGE_ENABLE_HR	0x00000e00
#define	CG12_HACCESS_ENABLE	0x00000020	/* 1bit/pixel */
#define	CG12_PLN_SL_ENABLE	0x00000016	/* plane 22 */
#define	CG12_PLN_WR_ENABLE	0x00400000
#define	CG12_PLN_RD_ENABLE	0xffffffff

#define	CG12_HPAGE_24BIT	0x00000500	/* intensity page */
#define	CG12_HPAGE_24BIT_HR	0x00000a00
#define	CG12_HACCESS_24BIT	0x00000025	/* 32bits/pixel */
#define	CG12_PLN_SL_24BIT	0x00000000	/* planes 0-31 */
#define	CG12_PLN_WR_24BIT	0x00ffffff
#define	CG12_PLN_RD_24BIT	0x00ffffff

#define	CG12_HPAGE_8BIT		0x00000500	/* intensity page */
#define	CG12_HPAGE_8BIT_HR	0x00000a00
#define	CG12_HACCESS_8BIT	0x00000023	/* 8bits/pixel */
#define	CG12_PLN_SL_8BIT	0x00000000	/* planes 0-7 */
#define	CG12_PLN_WR_8BIT	0x00ffffff
#define	CG12_PLN_RD_8BIT	0x000000ff

#define	CG12_HPAGE_WID		0x00000700	/* overlay page */
#define	CG12_HPAGE_WID_HR	0x00000e00
#define	CG12_HACCESS_WID	0x00000023	/* 8bits/pixel */
#define	CG12_PLN_SL_WID		0x00000010	/* planes 16-23 */
#define	CG12_PLN_WR_WID		0x003f0000
#define	CG12_PLN_RD_WID		0x003f0000

#define	CG12_HPAGE_ZBUF		0x00000000	/* depth page */
#define	CG12_HPAGE_ZBUF_HR	0x00000000
#define	CG12_HACCESS_ZBUF	0x00000024	/* 16bits/pixel */
#define	CG12_PLN_SL_ZBUF	0x00000060
#define	CG12_PLN_WR_ZBUF	0xffffffff
#define	CG12_PLN_RD_ZBUF	0xffffffff

/* Direct Port Unit */
struct cgtwelve_dpu {
	u_int32_t	r[8];
	u_int32_t	reload_ctl;
	u_int32_t	reload_stb;
	u_int32_t	alu_ctl;
	u_int32_t	blu_ctl;
	u_int32_t	control;
	u_int32_t	xleft;
	u_int32_t	shift0;
	u_int32_t	shift1;
	u_int32_t	zoom;
	u_int32_t	bsr;
	u_int32_t	color0;
	u_int32_t	color1;
	u_int32_t	compout;
	u_int32_t	pln_rd_msk_host;
	u_int32_t	pln_wr_msk_host;
	u_int32_t	pln_rd_msk_local;
	u_int32_t	pln_wr_msk_local;
	u_int32_t	scis_ctl;
	u_int32_t	csr;
	u_int32_t	pln_reg_sl;
	u_int32_t	pln_sl_host;
	u_int32_t	pln_sl_local0;
	u_int32_t	pln_sl_local1;
	u_int32_t	broadcast;
};

/* APU */
struct cgtwelve_apu {   
	u_int32_t	imsg0;
	u_int32_t	msg0;
	u_int32_t	imsg1;
	u_int32_t	msg1;
	u_int32_t	ien0;
	u_int32_t	ien1;
	u_int32_t	iclear;
	u_int32_t	istatus;
	u_int32_t	cfcnt;
	u_int32_t	cfwptr;
	u_int32_t	cfrptr;
	u_int32_t	cfilev0;
	u_int32_t	cfilev1;
	u_int32_t	rfcnt;
	u_int32_t	rfwptr;
	u_int32_t	rfrptr;
	u_int32_t	rfilev0;
	u_int32_t	rfilev1;
	u_int32_t	size;
	u_int32_t	res0;
	u_int32_t	res1;
	u_int32_t	res2;
	u_int32_t	haccess;
	u_int32_t	hpage;
	u_int32_t	laccess;
	u_int32_t	lpage;
	u_int32_t	maccess;
	u_int32_t	ppage;
	u_int32_t	dwg_ctl;
	u_int32_t	sam;
	u_int32_t	sgn;
	u_int32_t	length;
	u_int32_t	dwg[8];
	u_int32_t	reload_ctl;
	u_int32_t	reload_stb;
	u_int32_t	c_xleft;
	u_int32_t	c_ytop;
	u_int32_t	c_xright;
	u_int32_t	c_ybot;
	u_int32_t	f_xleft;
	u_int32_t	f_xright;
	u_int32_t	x_dst;
	u_int32_t	y_dst;
	u_int32_t	dst_ctl;
	u_int32_t	morigin;
	u_int32_t	vsg_ctl;
	u_int32_t	h_sync;
	u_int32_t	hblank;
	u_int32_t	v_sync;
	u_int32_t	vblank;
	u_int32_t	vdpyint;
	u_int32_t	vssyncs;
	u_int32_t	hdelays;
	u_int32_t	stdaddr;
	u_int32_t	hpitches;
	u_int32_t	zoom;
	u_int32_t	test;
};

struct cgtwelve_dac {
	u_int32_t	addr_lo;
	u_int8_t	pad1[0x100 - 4];
	u_int32_t	addr_hi;
	u_int8_t	pad2[0x100 - 4];
	u_int32_t	control;
	u_int8_t	pad3[0x100 - 4];
	u_int32_t	color;
	u_int8_t	pad4[0x100 - 4];
};
