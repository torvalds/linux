/*
 *  linux/include/asm-arm/arch-rpc/irqs.h
 *
 *  Copyright (C) 1996 Russell King
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#define IRQ_PRINTER		0
#define IRQ_BATLOW		1
#define IRQ_FLOPPYINDEX		2
#define IRQ_VSYNCPULSE		3
#define IRQ_POWERON		4
#define IRQ_TIMER0		5
#define IRQ_TIMER1		6
#define IRQ_IMMEDIATE		7
#define IRQ_EXPCARDFIQ		8
#define IRQ_HARDDISK		9
#define IRQ_SERIALPORT		10
#define IRQ_FLOPPYDISK		12
#define IRQ_EXPANSIONCARD	13
#define IRQ_KEYBOARDTX		14
#define IRQ_KEYBOARDRX		15

#define IRQ_DMA0		16
#define IRQ_DMA1		17
#define IRQ_DMA2		18
#define IRQ_DMA3		19
#define IRQ_DMAS0		20
#define IRQ_DMAS1		21

#define FIQ_FLOPPYDATA		0
#define FIQ_ECONET		2
#define FIQ_SERIALPORT		4
#define FIQ_EXPANSIONCARD	6
#define FIQ_FORCE		7

/*
 * This is the offset of the FIQ "IRQ" numbers
 */
#define FIQ_START		64

#define IRQ_TIMER		IRQ_TIMER0

