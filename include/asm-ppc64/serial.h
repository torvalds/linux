/*
 * include/asm-ppc64/serial.h
 */
#ifndef _PPC64_SERIAL_H
#define _PPC64_SERIAL_H

/*
 * This assumes you have a 1.8432 MHz clock for your UART.
 *
 * It'd be nice if someone built a serial card with a 24.576 MHz
 * clock, since the 16550A is capable of handling a top speed of 1.5
 * megabits/second; but this requires the faster clock.
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version
 * 2 of the License, or (at your option) any later version.
 */

/* Default baud base if not found in device-tree */
#define BASE_BAUD ( 1843200 / 16 )

#endif /* _PPC64_SERIAL_H */
