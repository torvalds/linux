/*
 * Lowlevel hardware stuff for the MIPS based Cobalt microservers.
 *
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file "COPYING" in the main directory of this archive
 * for more details.
 *
 * Copyright (C) 1997 Cobalt Microserver
 * Copyright (C) 1997, 2003 Ralf Baechle
 * Copyright (C) 2001, 2002, 2003 Liam Davies (ldavies@agile.tv)
 */
#ifndef __ASM_COBALT_H
#define __ASM_COBALT_H

/*
 * The Cobalt board ID information.
 */
extern int cobalt_board_id;

#define COBALT_BRD_ID_QUBE1    0x3
#define COBALT_BRD_ID_RAQ1     0x4
#define COBALT_BRD_ID_QUBE2    0x5
#define COBALT_BRD_ID_RAQ2     0x6

#define COBALT_KEY_PORT		((~*(volatile unsigned int *) CKSEG1ADDR(0x1d000000) >> 24) & COBALT_KEY_MASK)
# define COBALT_KEY_CLEAR	(1 << 1)
# define COBALT_KEY_LEFT	(1 << 2)
# define COBALT_KEY_UP		(1 << 3)
# define COBALT_KEY_DOWN	(1 << 4)
# define COBALT_KEY_RIGHT	(1 << 5)
# define COBALT_KEY_ENTER	(1 << 6)
# define COBALT_KEY_SELECT	(1 << 7)
# define COBALT_KEY_MASK	0xfe

#endif /* __ASM_COBALT_H */
