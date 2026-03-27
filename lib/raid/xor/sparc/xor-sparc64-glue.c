// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * High speed xor_block operation for RAID4/5 utilizing the
 * UltraSparc Visual Instruction Set and Niagara block-init
 * twin-load instructions.
 *
 * Copyright (C) 1997, 1999 Jakub Jelinek (jj@ultra.linux.cz)
 * Copyright (C) 2006 David S. Miller <davem@davemloft.net>
 */

#include <linux/raid/xor_impl.h>
#include <asm/xor.h>

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

struct xor_block_template xor_block_VIS = {
        .name	= "VIS",
        .do_2	= xor_vis_2,
        .do_3	= xor_vis_3,
        .do_4	= xor_vis_4,
        .do_5	= xor_vis_5,
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

struct xor_block_template xor_block_niagara = {
        .name	= "Niagara",
        .do_2	= xor_niagara_2,
        .do_3	= xor_niagara_3,
        .do_4	= xor_niagara_4,
        .do_5	= xor_niagara_5,
};
