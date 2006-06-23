/*
 * include/asm-v850/v850e_uart.h -- common V850E on-chip UART driver
 *
 *  Copyright (C) 2001,02,03  NEC Electronics Corporation
 *  Copyright (C) 2001,02,03  Miles Bader <miles@gnu.org>
 *
 * This file is subject to the terms and conditions of the GNU General
 * Public License.  See the file COPYING in the main directory of this
 * archive for more details.
 *
 * Written by Miles Bader <miles@gnu.org>
 */

/* There's not actually a single UART implementation used by V850E CPUs,
   but rather a series of implementations that are all `close' to one
   another.  This file corresponds to the single driver which handles all
   of them.  */

#ifndef __V850_V850E_UART_H__
#define __V850_V850E_UART_H__

#include <linux/termios.h>

#include <asm/v850e_utils.h>
#include <asm/types.h>
#include <asm/machdep.h>	/* Pick up chip-specific defs.  */


/* Include model-specific definitions.  */
#ifdef CONFIG_V850E_UART
# ifdef CONFIG_V850E_UARTB
#  include <asm-v850/v850e_uartb.h>
# else
#  include <asm-v850/v850e_uarta.h> /* original V850E UART */
# endif
#endif


/* Optional capabilities some hardware provides.  */

/* This UART doesn't implement RTS/CTS by default, but some platforms
   implement them externally, so check to see if <asm/machdep.h> defined
   anything.  */
#ifdef V850E_UART_CTS
#define v850e_uart_cts(n)		V850E_UART_CTS(n)
#else
#define v850e_uart_cts(n)		(1)
#endif

/* Do the same for RTS.  */
#ifdef V850E_UART_SET_RTS
#define v850e_uart_set_rts(n,v)		V850E_UART_SET_RTS(n,v)
#else
#define v850e_uart_set_rts(n,v)		((void)0)
#endif


/* This is the serial channel to use for the boot console (if desired).  */
#ifndef V850E_UART_CONSOLE_CHANNEL
# define V850E_UART_CONSOLE_CHANNEL 0
#endif


#ifndef __ASSEMBLY__

/* Setup a console using channel 0 of the builtin uart.  */
extern void v850e_uart_cons_init (unsigned chan);

/* Configure and turn on uart channel CHAN, using the termios `control
   modes' bits in CFLAGS, and a baud-rate of BAUD.  */
void v850e_uart_configure (unsigned chan, unsigned cflags, unsigned baud);

#endif /* !__ASSEMBLY__ */


#endif /* __V850_V850E_UART_H__ */
