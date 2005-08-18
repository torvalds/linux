/*
 * include/asm-sh/serial.h
 *
 * Configuration details for 8250, 16450, 16550, etc. serial ports
 */

#ifndef _ASM_SERIAL_H
#define _ASM_SERIAL_H

#include <linux/config.h>
#include <linux/kernel.h>

#ifdef CONFIG_SH_EC3104
#include <asm/serial-ec3104.h>
#elif defined (CONFIG_SH_BIGSUR)
#include <asm/serial-bigsur.h>
#else
/*
 * This assumes you have a 1.8432 MHz clock for your UART.
 *
 * It'd be nice if someone built a serial card with a 24.576 MHz
 * clock, since the 16550A is capable of handling a top speed of 1.5
 * megabits/second; but this requires the faster clock.
 */
#define BASE_BAUD ( 1843200 / 16 )

#define STD_COM_FLAGS (ASYNC_BOOT_AUTOCONF | ASYNC_SKIP_TEST)

#ifdef CONFIG_HD64465
#include <asm/hd64465.h>

#define SERIAL_PORT_DFNS                   \
        /* UART CLK   PORT IRQ     FLAGS        */                      \
        { 0, BASE_BAUD, 0x3F8, HD64465_IRQ_UART, STD_COM_FLAGS }  /* ttyS0 */

#else

#define SERIAL_PORT_DFNS			\
	/* UART CLK   PORT IRQ     FLAGS        */			\
	{ 0, BASE_BAUD, 0x3F8, 4, STD_COM_FLAGS },	/* ttyS0 */	\
	{ 0, BASE_BAUD, 0x2F8, 3, STD_COM_FLAGS }	/* ttyS1 */

#endif

#endif
#endif /* _ASM_SERIAL_H */
