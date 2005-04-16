/*
 *  linux/include/asm-arm/arch-arc/oldlatches.h
 *
 *  Copyright (C) 1996 Russell King, Dave Gilbert
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 *  Modifications:
 *   04-04-1998	PJB/RMK	Merged arc and a5k versions
 */
#ifndef _ASM_ARCH_OLDLATCH_H
#define _ASM_ARCH_OLDLATCH_H

#define LATCHA_FDSEL0    (1<<0)
#define LATCHA_FDSEL1    (1<<1)
#define LATCHA_FDSEL2    (1<<2)
#define LATCHA_FDSEL3    (1<<3)
#define LATCHA_FDSELALL  (0xf)
#define LATCHA_SIDESEL   (1<<4)
#define LATCHA_MOTOR     (1<<5)
#define LATCHA_INUSE     (1<<6)
#define LATCHA_CHANGERST (1<<7)

#define LATCHB_FDCDENSITY  (1<<1)
#define LATCHB_FDCRESET    (1<<3)
#define LATCHB_PRINTSTROBE (1<<4)

/* newval=(oldval & mask)|newdata */
void oldlatch_bupdate(unsigned char mask,unsigned char newdata);

/* newval=(oldval & mask)|newdata */
void oldlatch_aupdate(unsigned char mask,unsigned char newdata);

#endif

