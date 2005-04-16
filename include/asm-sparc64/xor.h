/*
 * include/asm-sparc64/xor.h
 *
 * High speed xor_block operation for RAID4/5 utilizing the
 * UltraSparc Visual Instruction Set.
 *
 * Copyright (C) 1997, 1999 Jakub Jelinek (jj@ultra.linux.cz)
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2, or (at your option)
 * any later version.
 *
 * You should have received a copy of the GNU General Public License
 * (for example /usr/src/linux/COPYING); if not, write to the Free
 * Software Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
 */

#include <asm/pstate.h>
#include <asm/asi.h>

extern void xor_vis_2(unsigned long, unsigned long *, unsigned long *);
extern void xor_vis_3(unsigned long, unsigned long *, unsigned long *,
		      unsigned long *);
extern void xor_vis_4(unsigned long, unsigned long *, unsigned long *,
		      unsigned long *, unsigned long *);
extern void xor_vis_5(unsigned long, unsigned long *, unsigned long *,
		      unsigned long *, unsigned long *, unsigned long *);

/* XXX Ugh, write cheetah versions... -DaveM */

static struct xor_block_template xor_block_VIS = {
        .name	= "VIS",
        .do_2	= xor_vis_2,
        .do_3	= xor_vis_3,
        .do_4	= xor_vis_4,
        .do_5	= xor_vis_5,
};

#define XOR_TRY_TEMPLATES       xor_speed(&xor_block_VIS)
