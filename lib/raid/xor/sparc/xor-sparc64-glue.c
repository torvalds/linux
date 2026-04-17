// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * High speed xor_block operation for RAID4/5 utilizing the
 * UltraSparc Visual Instruction Set and Niagara block-init
 * twin-load instructions.
 *
 * Copyright (C) 1997, 1999 Jakub Jelinek (jj@ultra.linux.cz)
 * Copyright (C) 2006 David S. Miller <davem@davemloft.net>
 */

#include "xor_impl.h"
#include "xor_arch.h"

void xor_vis_2(unsigned long bytes, unsigned long * __restrict p1,
	       const unsigned long * __restrict p2);
void xor_vis_3(unsigned long bytes, unsigned long * __restrict p1,
	       const unsigned long * __restrict p2,
	       const unsigned long * __restrict p3);
void xor_vis_4(unsigned long bytes, unsigned long * __restrict p1,
	       const unsigned long * __restrict p2,
	       const unsigned long * __restrict p3,
	       const unsigned long * __restrict p4);
void xor_vis_5(unsigned long bytes, unsigned long * __restrict p1,
	       const unsigned long * __restrict p2,
	       const unsigned long * __restrict p3,
	       const unsigned long * __restrict p4,
	       const unsigned long * __restrict p5);

/* XXX Ugh, write cheetah versions... -DaveM */

DO_XOR_BLOCKS(vis, xor_vis_2, xor_vis_3, xor_vis_4, xor_vis_5);

struct xor_block_template xor_block_VIS = {
        .name		= "VIS",
	.xor_gen	= xor_gen_vis,
};

void xor_niagara_2(unsigned long bytes, unsigned long * __restrict p1,
		   const unsigned long * __restrict p2);
void xor_niagara_3(unsigned long bytes, unsigned long * __restrict p1,
		   const unsigned long * __restrict p2,
		   const unsigned long * __restrict p3);
void xor_niagara_4(unsigned long bytes, unsigned long * __restrict p1,
		   const unsigned long * __restrict p2,
		   const unsigned long * __restrict p3,
		   const unsigned long * __restrict p4);
void xor_niagara_5(unsigned long bytes, unsigned long * __restrict p1,
		   const unsigned long * __restrict p2,
		   const unsigned long * __restrict p3,
		   const unsigned long * __restrict p4,
		   const unsigned long * __restrict p5);

DO_XOR_BLOCKS(niagara, xor_niagara_2, xor_niagara_3, xor_niagara_4,
		xor_niagara_5);

struct xor_block_template xor_block_niagara = {
        .name		= "Niagara",
	.xor_gen	= xor_gen_niagara,
};
