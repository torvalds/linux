/*
 *  linux/include/asm-arm/arch-arc/irqs.h
 *
 *  Copyright (C) 1996 Russell King, Dave Gilbert
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 *  Modifications:
 *   04-04-1998 PJB     Merged arc and a5k versions
 */


#if defined(CONFIG_ARCH_A5K)
#define IRQ_PRINTER             0
#define IRQ_BATLOW              1
#define IRQ_FLOPPYINDEX         2
#define IRQ_FLOPPYDISK          12
#elif defined(CONFIG_ARCH_ARC)
#define IRQ_PRINTERBUSY         0
#define IRQ_SERIALRING          1
#define IRQ_PRINTERACK          2
#define IRQ_FLOPPYCHANGED       12
#endif

#define IRQ_VSYNCPULSE          3
#define IRQ_POWERON             4
#define IRQ_TIMER0              5
#define IRQ_TIMER1              6
#define IRQ_IMMEDIATE           7
#define IRQ_EXPCARDFIQ          8
#define IRQ_SOUNDCHANGE         9
#define IRQ_SERIALPORT          10
#define IRQ_HARDDISK            11
#define IRQ_EXPANSIONCARD       13
#define IRQ_KEYBOARDTX          14
#define IRQ_KEYBOARDRX          15

#if defined(CONFIG_ARCH_A5K)
#define FIQ_SERIALPORT          4
#elif defined(CONFIG_ARCH_ARC)
#define FIQ_FLOPPYIRQ           1
#define FIQ_FD1772              FIQ_FLOPPYIRQ
#endif

#define FIQ_FLOPPYDATA          0
#define FIQ_ECONET              2
#define FIQ_EXPANSIONCARD       6
#define FIQ_FORCE               7

#define IRQ_TIMER               IRQ_TIMER0

/*
 * This is the offset of the FIQ "IRQ" numbers
 */
#define FIQ_START               64

#define irq_cannonicalize(i)    (i)

