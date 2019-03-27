/*	$NetBSD: instr.h,v 1.4 2005/12/11 12:18:43 christos Exp $ */
/* $FreeBSD$ */

/*-
 * SPDX-License-Identifier: BSD-3-Clause
 *
 * Copyright (c) 1992, 1993
 *	The Regents of the University of California.  All rights reserved.
 *
 * This software was developed by the Computer Systems Engineering group
 * at Lawrence Berkeley Laboratory under DARPA contract BG 91-66 and
 * contributed to Berkeley.
 *
 * All advertising materials mentioning features or use of this software
 * must display the following acknowledgement:
 *	This product includes software developed by the University of
 *	California, Lawrence Berkeley Laboratory.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 * 3. Neither the name of the University nor the names of its contributors
 *    may be used to endorse or promote products derived from this software
 *    without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE REGENTS AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE REGENTS OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 *
 *	@(#)instr.h	8.1 (Berkeley) 6/11/93
 */

/*
 * An instruction.
 */
union instr {
	int	i_int;			/* as a whole */
 
	/*
	 * Any instruction type.
	 */
	struct {
		u_int	i_opcd:6;	/* first-level decode */
		u_int	:25;
		u_int	i_rc:1;
	} i_any;

	/*
	 * Format A
	 */
	struct {
		u_int	i_opcd:6;
		u_int	i_frt:5;
		u_int	i_fra:5;
		u_int	i_frb:5;
		u_int	i_frc:5;
		u_int	i_xo:5;
		u_int	i_rc:1;
	} i_a;

	/*
	 * Format B
	 */
	struct {
		u_int	i_opcd:6;
		int	i_bo:5;
		int	i_bi:5;
		int	i_bd:14;
		int	i_aa:1;
		int	i_lk:1;
	} i_b;

	/*
	 * Format D
	 */
	struct {
		u_int	i_opcd:6;
		u_int	i_rs:5;
		u_int	i_ra:5;
		int	i_d:16;
	} i_d;

	/*
	 * Format DE
	 */
	struct {
		u_int	i_opcd:6;
		u_int	i_rs:5;
		u_int	i_ra:5;
		int	i_d:12;
		u_int	i_xo:4;
	} i_de;

	/*
	 * Format I
	 */
	struct {
		u_int	i_opcd:6;
		int	i_li:24;
		int	i_aa:1;
		int	i_lk:1;
	} i_i;

	/*
	 * Format M
	 */
	struct {
		u_int	i_opcd:6;
		u_int	i_rs:5;
		u_int	i_ra:5;
		u_int	i_rb:5;
		int	i_mb:5;
		int	i_me:5;
		u_int	i_rc:1;
	} i_m;

	/*
	 * Format MD
	 */
	struct {
		u_int	i_opcd:6;
		u_int	i_rs:5;
		u_int	i_ra:5;
		u_int	i_rb:5;
		int	i_sh1_5:5;
		int	i_mb:6;
		u_int	i_xo:3;
		int	i_sh0:2;
		u_int	i_rc:1;
	} i_md;

	/*
	 * Format MDS
	 */
	struct {
		u_int	i_opcd:6;
		u_int	i_rs:5;
		u_int	i_ra:5;
		u_int	i_rb:5;
		int	i_sh:5;
		int	i_mb:6;
		u_int	i_xo:4;
		u_int	i_rc:1;
	} i_mds;


	/*
	 * Format S
	 */
	struct {
		u_int	i_opcd:6;
		int	:24;
		int	i_i:1;
		int	:1;
	} i_s;

	/*
	 * Format X
	 */
	struct {
		u_int	i_opcd:6;
		u_int	i_rs:5;
		u_int	i_ra:5;
		u_int	i_rb:5;
		u_int	i_xo:10;
		u_int	i_rc:1;
	} i_x;

	/*
	 * Format XFL
	 */
	struct {
		u_int	i_opcd:6;
		int	:1;
		int	i_flm:8;
		int	:1;
		int	i_frb:5;
		u_int	i_xo:10;
		int	:1;
	} i_xfl;

	/*
	 * Format XFX
	 */
	struct {
		u_int	i_opcd:6;
		int	i_dcrn:10;
		u_int	i_xo:10;
		int	:1;
	} i_xfx;

	/*
	 * Format XL
	 */
	struct {
		u_int	i_opcd:6;
		int	i_bt:5;
		int	i_ba:5;
		int	i_bb:5;
		u_int	i_xo:10;
		int	i_lk:1;
	} i_xl;

	/*
	 * Format XS
	 */
	struct {
		u_int	i_opcd:6;
		u_int	i_rs:5;
		u_int	i_ra:5;
		int	i_sh0_4:5;
		u_int	i_xo:9;
		int	i_sh5:1;
		u_int	i_rc:1;
	} i_xs;

};

#define	i_rt	i_rs

/*
 * Primary opcode numbers:
 */

#define	OPC_TDI		0x02
#define	OPC_TWI		0x03
#define	OPC_MULLI	0x07
#define	OPC_SUBFIC	0x08
#define	OPC_BCE		0x09
#define	OPC_CMPLI	0x0a
#define	OPC_CMPI	0x0b
#define	OPC_ADDIC	0x0c
#define	OPC_ADDIC_DOT	0x0d
#define	OPC_ADDI	0x0e
#define	OPC_ADDIS	0x0f
#define	OPC_BC		0x10
#define	OPC_SC		0x11
#define	OPC_B		0x12
#define	OPC_branch_19	0x13
#define	OPC_RLWIMI	0x14
#define	OPC_RLWINM	0x15
#define	OPC_BE		0x16
#define	OPC_RLWNM	0x17
#define	OPC_ORI		0x18
#define	OPC_ORIS	0x19
#define	OPC_XORI	0x1a
#define	OPC_XORIS	0x1b
#define	OPC_ANDI	0x1c
#define	OPC_ANDIS	0x1d
#define	OPC_dwe_rot_30	0x1e
#define	OPC_integer_31	0x1f
#define	OPC_LWZ		0x20
#define	OPC_LWZU	0x21
#define	OPC_LBZ		0x22
#define	OPC_LBZU	0x23
#define	OPC_STW		0x24
#define	OPC_STWU	0x25
#define	OPC_STB		0x26
#define	OPC_STBU	0x27
#define	OPC_LHZ		0x28
#define	OPC_LHZU	0x29
#define	OPC_LHA		0x2a
#define	OPC_LHAU	0x2b
#define	OPC_STH		0x2c
#define	OPC_STHU	0x2d
#define	OPC_LMW		0x2e
#define	OPC_STMW	0x2f
#define	OPC_LFS		0x30
#define	OPC_LFSU	0x31
#define	OPC_LFD		0x32
#define	OPC_LFDU	0x33
#define	OPC_STFS	0x34
#define	OPC_STFSU	0x35
#define	OPC_STFD	0x36
#define	OPC_STFDU	0x37
#define	OPC_load_st_58	0x3a
#define	OPC_sp_fp_59	0x3b
#define	OPC_load_st_62	0x3e
#define	OPC_dp_fp_63	0x3f

/*
 * Opcode 31 sub-types (FP only)
 */
#define	OPC31_TW	0x004
#define	OPC31_LFSX	0x217
#define	OPC31_LFSUX	0x237
#define	OPC31_LFDX	0x257
#define	OPC31_LFDUX	0x277
#define	OPC31_STFSX	0x297
#define	OPC31_STFSUX	0x2b7
#define	OPC31_STFDX	0x2d7
#define	OPC31_STFDUX	0x2f7
#define	OPC31_STFIWX	0x3d7

/* Mask for all valid indexed FP load/store ops (except stfiwx) */
#define	OPC31_FPMASK	0x31f
#define	OPC31_FPOP	0x217

/*
 * Opcode 59 sub-types:
 */

#define	OPC59_FDIVS	0x12
#define	OPC59_FSUBS	0x14
#define	OPC59_FADDS	0x15
#define	OPC59_FSQRTS	0x16
#define	OPC59_FRES	0x18
#define	OPC59_FMULS	0x19
#define	OPC59_FMSUBS	0x1c
#define	OPC59_FMADDS	0x1d
#define	OPC59_FNMSUBS	0x1e
#define	OPC59_FNMADDS	0x1f

/*
 * Opcode 62 sub-types:
 */
#define	OPC62_LDE	0x0
#define	OPC62_LDEU	0x1
#define	OPC62_LFSE	0x4
#define	OPC62_LFSEU	0x5
#define	OPC62_LFDE	0x6
#define	OPC62_LFDEU	0x7
#define	OPC62_STDE	0x8
#define	OPC62_STDEU	0x9
#define	OPC62_STFSE	0xc
#define	OPC62_STFSEU	0xd
#define	OPC62_STFDE	0xe
#define	OPC62_STFDEU	0xf

/*
 * Opcode 63 sub-types:
 *
 * (The first group are masks....)
 */

#define	OPC63M_MASK	0x10
#define	OPC63M_FDIV	0x12
#define	OPC63M_FSUB	0x14
#define	OPC63M_FADD	0x15
#define	OPC63M_FSQRT	0x16
#define	OPC63M_FSEL	0x17
#define	OPC63M_FMUL	0x19
#define	OPC63M_FRSQRTE	0x1a
#define	OPC63M_FMSUB	0x1c
#define	OPC63M_FMADD	0x1d
#define	OPC63M_FNMSUB	0x1e
#define	OPC63M_FNMADD	0x1f

#define	OPC63_FCMPU	0x00
#define	OPC63_FRSP	0x0c
#define	OPC63_FCTIW	0x0e
#define	OPC63_FCTIWZ	0x0f
#define	OPC63_FCMPO	0x20
#define	OPC63_MTFSB1	0x26
#define	OPC63_FNEG	0x28
#define	OPC63_MCRFS	0x40
#define	OPC63_MTFSB0	0x46
#define	OPC63_FMR	0x48
#define	OPC63_MTFSFI	0x86
#define	OPC63_FNABS	0x88
#define	OPC63_FABS	0x108
#define	OPC63_MFFS	0x247
#define	OPC63_MTFSF	0x2c7
#define	OPC63_FCTID	0x32e
#define	OPC63_FCTIDZ	0x32f
#define	OPC63_FCFID	0x34e

/*
 * FPU data types.
 */
#define FTYPE_LNG	-1	/* data = 64-bit signed long integer */		
#define	FTYPE_INT	0	/* data = 32-bit signed integer */
#define	FTYPE_SNG	1	/* data = 32-bit float */
#define	FTYPE_DBL	2	/* data = 64-bit double */

