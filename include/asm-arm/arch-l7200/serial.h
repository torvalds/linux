/*
 * linux/include/asm-arm/arch-l7200/serial.h
 *
 * Copyright (c) 2000 Rob Scott (rscott@mtrob.fdns.net)
 *                    Steve Hill (sjhill@cotw.com)
 *
 * Changelog:
 *  03-20-2000  SJH     Created
 *  03-26-2000  SJH     Added flags for serial ports
 *  03-27-2000  SJH     Corrected BASE_BAUD value
 *  04-14-2000  RS      Made register addr dependent on IO_BASE
 *  05-03-2000  SJH     Complete rewrite
 *  05-09-2000	SJH	Stripped out architecture specific serial stuff
 *                      and placed it in a separate file
 *  07-28-2000	SJH	Moved base baud rate variable
 */
#ifndef __ASM_ARCH_SERIAL_H
#define __ASM_ARCH_SERIAL_H

/*
 * This assumes you have a 3.6864 MHz clock for your UART.
 */
#define BASE_BAUD	3686400

/*
 * Standard COM flags
 */
#define STD_COM_FLAGS (ASYNC_BOOT_AUTOCONF | ASYNC_SKIP_TEST)

#define STD_SERIAL_PORT_DEFNS		\
	/* MAGIC UART CLK   PORT       IRQ     FLAGS */			\
	{ 0, BASE_BAUD, UART1_BASE, IRQ_UART_1, STD_COM_FLAGS },  /* ttyLU0 */ \
	{ 0, BASE_BAUD, UART2_BASE, IRQ_UART_2, STD_COM_FLAGS },  /* ttyLU1 */ \

#define EXTRA_SERIAL_PORT_DEFNS

#endif
