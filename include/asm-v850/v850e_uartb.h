/*
 * include/asm-v850/v850e_uartb.h -- V850E on-chip `UARTB' UART
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

/* The V850E UARTB is basically a superset of the original V850E UART, but
   even where it's the same, the names and details have changed a bit.
   It's similar enough to use the same driver (v850e_uart.c), but the
   details have been abstracted slightly to do so.  */

#ifndef __V850_V850E_UARTB_H__
#define __V850_V850E_UARTB_H__


/* Raw hardware interface.  */

#define V850E_UARTB_BASE_ADDR(n)	(0xFFFFFA00 + 0x10 * (n))

/* Addresses of specific UART control registers for channel N.  */
#define V850E_UARTB_CTL0_ADDR(n)	(V850E_UARTB_BASE_ADDR(n) + 0x0)
#define V850E_UARTB_CTL2_ADDR(n)	(V850E_UARTB_BASE_ADDR(n) + 0x2)
#define V850E_UARTB_STR_ADDR(n)		(V850E_UARTB_BASE_ADDR(n) + 0x4)
#define V850E_UARTB_RX_ADDR(n)		(V850E_UARTB_BASE_ADDR(n) + 0x6)
#define V850E_UARTB_RXAP_ADDR(n)	(V850E_UARTB_BASE_ADDR(n) + 0x6)
#define V850E_UARTB_TX_ADDR(n)		(V850E_UARTB_BASE_ADDR(n) + 0x8)
#define V850E_UARTB_FIC0_ADDR(n)	(V850E_UARTB_BASE_ADDR(n) + 0xA)
#define V850E_UARTB_FIC1_ADDR(n)	(V850E_UARTB_BASE_ADDR(n) + 0xB)
#define V850E_UARTB_FIC2_ADDR(n)	(V850E_UARTB_BASE_ADDR(n) + 0xC)
#define V850E_UARTB_FIS0_ADDR(n)	(V850E_UARTB_BASE_ADDR(n) + 0xE)
#define V850E_UARTB_FIS1_ADDR(n)	(V850E_UARTB_BASE_ADDR(n) + 0xF)

/* UARTB control register 0 (general config).  */
#define V850E_UARTB_CTL0(n)	(*(volatile u8 *)V850E_UARTB_CTL0_ADDR(n))
/* Control bits for config registers.  */
#define V850E_UARTB_CTL0_PWR		0x80	/* clock enable */
#define V850E_UARTB_CTL0_TXE		0x40	/* transmit enable */
#define V850E_UARTB_CTL0_RXE		0x20	/* receive enable */
#define V850E_UARTB_CTL0_DIR		0x10	/*  */
#define V850E_UARTB_CTL0_PS1		0x08	/* parity */
#define V850E_UARTB_CTL0_PS0		0x04	/* parity */
#define V850E_UARTB_CTL0_CL		0x02	/* char len 1:8bit, 0:7bit */
#define V850E_UARTB_CTL0_SL		0x01	/* stop bit 1:2bit, 0:1bit */
#define V850E_UARTB_CTL0_PS_MASK	0x0C	/* mask covering parity bits */
#define V850E_UARTB_CTL0_PS_NONE	0x00	/* no parity */
#define V850E_UARTB_CTL0_PS_ZERO	0x04	/* zero parity */
#define V850E_UARTB_CTL0_PS_ODD		0x08	/* odd parity */
#define V850E_UARTB_CTL0_PS_EVEN	0x0C	/* even parity */
#define V850E_UARTB_CTL0_CL_8		0x02	/* char len 1:8bit, 0:7bit */
#define V850E_UARTB_CTL0_SL_2		0x01	/* stop bit 1:2bit, 0:1bit */

/* UARTB control register 2 (clock divider).  */
#define V850E_UARTB_CTL2(n)	(*(volatile u16 *)V850E_UARTB_CTL2_ADDR(n))
#define V850E_UARTB_CTL2_MIN	4
#define V850E_UARTB_CTL2_MAX	0xFFFF

/* UARTB serial interface status register.  */
#define V850E_UARTB_STR(n)	(*(volatile u8 *)V850E_UARTB_STR_ADDR(n))
/* Control bits for status registers.  */
#define V850E_UARTB_STR_TSF	0x80	/* UBTX or FIFO exist data  */
#define V850E_UARTB_STR_OVF	0x08	/* overflow error */
#define V850E_UARTB_STR_PE	0x04	/* parity error */
#define V850E_UARTB_STR_FE	0x02	/* framing error */
#define V850E_UARTB_STR_OVE	0x01	/* overrun error */

/* UARTB receive data register.  */
#define V850E_UARTB_RX(n)	(*(volatile u8 *)V850E_UARTB_RX_ADDR(n))
#define V850E_UARTB_RXAP(n)	(*(volatile u16 *)V850E_UARTB_RXAP_ADDR(n))
/* Control bits for status registers.  */
#define V850E_UARTB_RXAP_PEF	0x0200 /* parity error */
#define V850E_UARTB_RXAP_FEF	0x0100 /* framing error */

/* UARTB transmit data register.  */
#define V850E_UARTB_TX(n)	(*(volatile u8 *)V850E_UARTB_TX_ADDR(n))

/* UARTB FIFO control register 0.  */
#define V850E_UARTB_FIC0(n)	(*(volatile u8 *)V850E_UARTB_FIC0_ADDR(n))

/* UARTB FIFO control register 1.  */
#define V850E_UARTB_FIC1(n)	(*(volatile u8 *)V850E_UARTB_FIC1_ADDR(n))

/* UARTB FIFO control register 2.  */
#define V850E_UARTB_FIC2(n)	(*(volatile u16 *)V850E_UARTB_FIC2_ADDR(n))

/* UARTB FIFO status register 0.  */
#define V850E_UARTB_FIS0(n)	(*(volatile u8 *)V850E_UARTB_FIS0_ADDR(n))

/* UARTB FIFO status register 1.  */
#define V850E_UARTB_FIS1(n)	(*(volatile u8 *)V850E_UARTB_FIS1_ADDR(n))


/* Slightly abstract interface used by driver.  */


/* Interrupts used by the UART.  */

/* Received when the most recently transmitted character has been sent.  */
#define V850E_UART_TX_IRQ(chan)		IRQ_INTUBTIT (chan)
/* Received when a new character has been received.  */
#define V850E_UART_RX_IRQ(chan)		IRQ_INTUBTIR (chan)

/* Use by serial driver for information purposes.  */
#define V850E_UART_BASE_ADDR(chan)	V850E_UARTB_BASE_ADDR(chan)


/* UART clock generator interface.  */

/* This type encapsulates a particular uart frequency.  */
typedef u16 v850e_uart_speed_t;

/* Calculate a uart speed from BAUD for this uart.  */
static inline v850e_uart_speed_t v850e_uart_calc_speed (unsigned baud)
{
	v850e_uart_speed_t speed;

	/*
	 * V850E/ME2 UARTB baud rate is determined by the value of UBCTL2
	 * fx = V850E_UARTB_BASE_FREQ = CPU_CLOCK_FREQ/4
	 * baud = fx / 2*speed   [ speed >= 4 ]
	 */
	speed = V850E_UARTB_CTL2_MIN;
	while (((V850E_UARTB_BASE_FREQ / 2) / speed ) > baud)
		speed++;

	return speed;
}

/* Return the current speed of uart channel CHAN.  */
#define v850e_uart_speed(chan)		    V850E_UARTB_CTL2 (chan)

/* Set the current speed of uart channel CHAN.  */
#define v850e_uart_set_speed(chan, speed)   (V850E_UARTB_CTL2 (chan) = (speed))

/* Return true if SPEED1 and SPEED2 are the same.  */
#define v850e_uart_speed_eq(speed1, speed2) ((speed1) == (speed2))

/* Minimum baud rate possible.  */
#define v850e_uart_min_baud() \
   ((V850E_UARTB_BASE_FREQ / 2) / V850E_UARTB_CTL2_MAX)

/* Maximum baud rate possible.  The error is quite high at max, though.  */
#define v850e_uart_max_baud() \
   ((V850E_UARTB_BASE_FREQ / 2) / V850E_UARTB_CTL2_MIN)

/* The `maximum' clock rate the uart can used, which is wanted (though not
   really used in any useful way) by the serial framework.  */
#define v850e_uart_max_clock() \
   (V850E_UARTB_BASE_FREQ / 2)


/* UART configuration interface.  */

/* Type of the uart config register; must be a scalar.  */
typedef u16 v850e_uart_config_t;

/* The uart hardware config register for channel CHAN.  */
#define V850E_UART_CONFIG(chan)		V850E_UARTB_CTL0 (chan)

/* This config bit set if the uart is enabled.  */
#define V850E_UART_CONFIG_ENABLED	V850E_UARTB_CTL0_PWR
/* If the uart _isn't_ enabled, store this value to it to do so.  */
#define V850E_UART_CONFIG_INIT		V850E_UARTB_CTL0_PWR
/* Store this config value to disable the uart channel completely.  */
#define V850E_UART_CONFIG_FINI		0

/* Setting/clearing these bits enable/disable TX/RX, respectively (but
   otherwise generally leave things running).  */
#define V850E_UART_CONFIG_RX_ENABLE	V850E_UARTB_CTL0_RXE
#define V850E_UART_CONFIG_TX_ENABLE	V850E_UARTB_CTL0_TXE

/* These masks define which config bits affect TX/RX modes, respectively.  */
#define V850E_UART_CONFIG_RX_BITS \
  (V850E_UARTB_CTL0_PS_MASK | V850E_UARTB_CTL0_CL_8)
#define V850E_UART_CONFIG_TX_BITS \
  (V850E_UARTB_CTL0_PS_MASK | V850E_UARTB_CTL0_CL_8 | V850E_UARTB_CTL0_SL_2)

static inline v850e_uart_config_t v850e_uart_calc_config (unsigned cflags)
{
	v850e_uart_config_t config = 0;

	/* Figure out new configuration of control register.  */
	if (cflags & CSTOPB)
		/* Number of stop bits, 1 or 2.  */
		config |= V850E_UARTB_CTL0_SL_2;
	if ((cflags & CSIZE) == CS8)
		/* Number of data bits, 7 or 8.  */
		config |= V850E_UARTB_CTL0_CL_8;
	if (! (cflags & PARENB))
		/* No parity check/generation.  */
		config |= V850E_UARTB_CTL0_PS_NONE;
	else if (cflags & PARODD)
		/* Odd parity check/generation.  */
		config |= V850E_UARTB_CTL0_PS_ODD;
	else
		/* Even parity check/generation.  */
		config |= V850E_UARTB_CTL0_PS_EVEN;
	if (cflags & CREAD)
		/* Reading enabled.  */
		config |= V850E_UARTB_CTL0_RXE;

	config |= V850E_UARTB_CTL0_PWR;
	config |= V850E_UARTB_CTL0_TXE; /* Writing is always enabled.  */
	config |= V850E_UARTB_CTL0_DIR; /* LSB first.  */

	return config;
}

/* This should delay as long as necessary for a recently written config
   setting to settle, before we turn the uart back on.  */
static inline void
v850e_uart_config_delay (v850e_uart_config_t config, v850e_uart_speed_t speed)
{
	/* The UART may not be reset properly unless we wait at least 2
	   `basic-clocks' until turning on the TXE/RXE bits again.
	   A `basic clock' is the clock used by the baud-rate generator,
	   i.e., the cpu clock divided by the 2^new_clk_divlog2.
	   The loop takes 2 insns, so loop CYCLES / 2 times.  */
	register unsigned count = 1 << speed;
	while (--count != 0)
		/* nothing */;
}


/* RX/TX interface.  */

/* Return true if all characters awaiting transmission on uart channel N
   have been transmitted.  */
#define v850e_uart_xmit_done(n)						      \
   (! (V850E_UARTB_STR(n) & V850E_UARTB_STR_TSF))
/* Wait for this to be true.  */
#define v850e_uart_wait_for_xmit_done(n)				      \
   do { } while (! v850e_uart_xmit_done (n))

/* Return true if uart channel N is ready to transmit a character.  */
#define v850e_uart_xmit_ok(n)						      \
   (v850e_uart_xmit_done(n) && v850e_uart_cts(n))
/* Wait for this to be true.  */
#define v850e_uart_wait_for_xmit_ok(n)					      \
   do { } while (! v850e_uart_xmit_ok (n))

/* Write character CH to uart channel CHAN.  */
#define v850e_uart_putc(chan, ch)	(V850E_UARTB_TX(chan) = (ch))

/* Return latest character read on channel CHAN.  */
#define v850e_uart_getc(chan)		V850E_UARTB_RX (chan)

/* Return bit-mask of uart error status.  */
#define v850e_uart_err(chan)		V850E_UARTB_STR (chan)
/* Various error bits set in the error result.  */
#define V850E_UART_ERR_OVERRUN		V850E_UARTB_STR_OVE
#define V850E_UART_ERR_FRAME		V850E_UARTB_STR_FE
#define V850E_UART_ERR_PARITY		V850E_UARTB_STR_PE


#endif /* __V850_V850E_UARTB_H__ */
