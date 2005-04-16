/*
 * include/asm-v850/v850e_uarta.h -- original V850E on-chip UART
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

/* This is the original V850E UART implementation is called just `UART' in
   the docs, but we name this header file <asm/v850e_uarta.h> because the
   name <asm/v850e_uart.h> is used for the common driver that handles both
   `UART' and `UARTB' implementations.  */

#ifndef __V850_V850E_UARTA_H__
#define __V850_V850E_UARTA_H__


/* Raw hardware interface.  */

/* The base address of the UART control registers for channel N.
   The default is the address used on the V850E/MA1.  */
#ifndef V850E_UART_BASE_ADDR
#define V850E_UART_BASE_ADDR(n)		(0xFFFFFA00 + 0x10 * (n))
#endif 

/* Addresses of specific UART control registers for channel N.
   The defaults are the addresses used on the V850E/MA1; if a platform
   wants to redefine any of these, it must redefine them all.  */
#ifndef V850E_UART_ASIM_ADDR
#define V850E_UART_ASIM_ADDR(n)		(V850E_UART_BASE_ADDR(n) + 0x0)
#define V850E_UART_RXB_ADDR(n)		(V850E_UART_BASE_ADDR(n) + 0x2)
#define V850E_UART_ASIS_ADDR(n)		(V850E_UART_BASE_ADDR(n) + 0x3)
#define V850E_UART_TXB_ADDR(n)		(V850E_UART_BASE_ADDR(n) + 0x4)
#define V850E_UART_ASIF_ADDR(n)		(V850E_UART_BASE_ADDR(n) + 0x5)
#define V850E_UART_CKSR_ADDR(n)		(V850E_UART_BASE_ADDR(n) + 0x6)
#define V850E_UART_BRGC_ADDR(n)		(V850E_UART_BASE_ADDR(n) + 0x7)
#endif

/* UART config registers.  */
#define V850E_UART_ASIM(n)	(*(volatile u8 *)V850E_UART_ASIM_ADDR(n))
/* Control bits for config registers.  */
#define V850E_UART_ASIM_CAE	0x80 /* clock enable */
#define V850E_UART_ASIM_TXE	0x40 /* transmit enable */
#define V850E_UART_ASIM_RXE	0x20 /* receive enable */
#define V850E_UART_ASIM_PS_MASK	0x18 /* mask covering parity-select bits */
#define V850E_UART_ASIM_PS_NONE	0x00 /* no parity */
#define V850E_UART_ASIM_PS_ZERO	0x08 /* zero parity */
#define V850E_UART_ASIM_PS_ODD	0x10 /* odd parity */
#define V850E_UART_ASIM_PS_EVEN	0x18 /* even parity */
#define V850E_UART_ASIM_CL_8	0x04 /* char len is 8 bits (otherwise, 7) */
#define V850E_UART_ASIM_SL_2	0x02 /* 2 stop bits (otherwise, 1) */
#define V850E_UART_ASIM_ISRM	0x01 /* generate INTSR interrupt on errors
					(otherwise, generate INTSER) */

/* UART serial interface status registers.  */
#define V850E_UART_ASIS(n)	(*(volatile u8 *)V850E_UART_ASIS_ADDR(n))
/* Control bits for status registers.  */
#define V850E_UART_ASIS_PE	0x04 /* parity error */
#define V850E_UART_ASIS_FE	0x02 /* framing error */
#define V850E_UART_ASIS_OVE	0x01 /* overrun error */

/* UART serial interface transmission status registers.  */
#define V850E_UART_ASIF(n)	(*(volatile u8 *)V850E_UART_ASIF_ADDR(n))
#define V850E_UART_ASIF_TXBF	0x02 /* transmit buffer flag (data in TXB) */
#define V850E_UART_ASIF_TXSF	0x01 /* transmit shift flag (sending data) */

/* UART receive buffer register.  */
#define V850E_UART_RXB(n)	(*(volatile u8 *)V850E_UART_RXB_ADDR(n))

/* UART transmit buffer register.  */
#define V850E_UART_TXB(n)	(*(volatile u8 *)V850E_UART_TXB_ADDR(n))

/* UART baud-rate generator control registers.  */
#define V850E_UART_CKSR(n)	(*(volatile u8 *)V850E_UART_CKSR_ADDR(n))
#define V850E_UART_CKSR_MAX	11
#define V850E_UART_BRGC(n)	(*(volatile u8 *)V850E_UART_BRGC_ADDR(n))
#define V850E_UART_BRGC_MIN	8


#ifndef V850E_UART_CKSR_MAX_FREQ
#define V850E_UART_CKSR_MAX_FREQ (25*1000*1000)
#endif

/* Calculate the minimum value for CKSR on this processor.  */
static inline unsigned v850e_uart_cksr_min (void)
{
	int min = 0;
	unsigned freq = V850E_UART_BASE_FREQ;
	while (freq > V850E_UART_CKSR_MAX_FREQ) {
		freq >>= 1;
		min++;
	}
	return min;
}


/* Slightly abstract interface used by driver.  */


/* Interrupts used by the UART.  */

/* Received when the most recently transmitted character has been sent.  */
#define V850E_UART_TX_IRQ(chan)		IRQ_INTST (chan)
/* Received when a new character has been received.  */
#define V850E_UART_RX_IRQ(chan)		IRQ_INTSR (chan)


/* UART clock generator interface.  */

/* This type encapsulates a particular uart frequency.  */
typedef struct {
	unsigned clk_divlog2;
	unsigned brgen_count;
} v850e_uart_speed_t;

/* Calculate a uart speed from BAUD for this uart.  */
static inline v850e_uart_speed_t v850e_uart_calc_speed (unsigned baud)
{
	v850e_uart_speed_t speed;

	/* Calculate the log2 clock divider and baud-rate counter values
	   (note that the UART divides the resulting clock by 2, so
	   multiply BAUD by 2 here to compensate).  */
	calc_counter_params (V850E_UART_BASE_FREQ, baud * 2,
			     v850e_uart_cksr_min(),
			     V850E_UART_CKSR_MAX, 8/*bits*/,
			     &speed.clk_divlog2, &speed.brgen_count);

	return speed;
}

/* Return the current speed of uart channel CHAN.  */
static inline v850e_uart_speed_t v850e_uart_speed (unsigned chan)
{
	v850e_uart_speed_t speed;
	speed.clk_divlog2 = V850E_UART_CKSR (chan);
	speed.brgen_count = V850E_UART_BRGC (chan);
	return speed;
}

/* Set the current speed of uart channel CHAN.  */
static inline void v850e_uart_set_speed(unsigned chan,v850e_uart_speed_t speed)
{
	V850E_UART_CKSR (chan) = speed.clk_divlog2;
	V850E_UART_BRGC (chan) = speed.brgen_count;
}

static inline int
v850e_uart_speed_eq (v850e_uart_speed_t speed1, v850e_uart_speed_t speed2)
{
	return speed1.clk_divlog2 == speed2.clk_divlog2
		&& speed1.brgen_count == speed2.brgen_count;
}

/* Minimum baud rate possible.  */
#define v850e_uart_min_baud() \
   ((V850E_UART_BASE_FREQ >> V850E_UART_CKSR_MAX) / (2 * 255) + 1)

/* Maximum baud rate possible.  The error is quite high at max, though.  */
#define v850e_uart_max_baud() \
   ((V850E_UART_BASE_FREQ >> v850e_uart_cksr_min()) / (2 *V850E_UART_BRGC_MIN))

/* The `maximum' clock rate the uart can used, which is wanted (though not
   really used in any useful way) by the serial framework.  */
#define v850e_uart_max_clock() \
   ((V850E_UART_BASE_FREQ >> v850e_uart_cksr_min()) / 2)


/* UART configuration interface.  */

/* Type of the uart config register; must be a scalar.  */
typedef u16 v850e_uart_config_t;

/* The uart hardware config register for channel CHAN.  */
#define V850E_UART_CONFIG(chan)		V850E_UART_ASIM (chan)

/* This config bit set if the uart is enabled.  */
#define V850E_UART_CONFIG_ENABLED	V850E_UART_ASIM_CAE
/* If the uart _isn't_ enabled, store this value to it to do so.  */
#define V850E_UART_CONFIG_INIT		V850E_UART_ASIM_CAE
/* Store this config value to disable the uart channel completely.  */
#define V850E_UART_CONFIG_FINI		0

/* Setting/clearing these bits enable/disable TX/RX, respectively (but
   otherwise generally leave things running).  */
#define V850E_UART_CONFIG_RX_ENABLE	V850E_UART_ASIM_RXE
#define V850E_UART_CONFIG_TX_ENABLE	V850E_UART_ASIM_TXE

/* These masks define which config bits affect TX/RX modes, respectively.  */
#define V850E_UART_CONFIG_RX_BITS \
  (V850E_UART_ASIM_PS_MASK | V850E_UART_ASIM_CL_8 | V850E_UART_ASIM_ISRM)
#define V850E_UART_CONFIG_TX_BITS \
  (V850E_UART_ASIM_PS_MASK | V850E_UART_ASIM_CL_8 | V850E_UART_ASIM_SL_2)

static inline v850e_uart_config_t v850e_uart_calc_config (unsigned cflags)
{
	v850e_uart_config_t config = 0;

	/* Figure out new configuration of control register.  */
	if (cflags & CSTOPB)
		/* Number of stop bits, 1 or 2.  */
		config |= V850E_UART_ASIM_SL_2;
	if ((cflags & CSIZE) == CS8)
		/* Number of data bits, 7 or 8.  */
		config |= V850E_UART_ASIM_CL_8;
	if (! (cflags & PARENB))
		/* No parity check/generation.  */
		config |= V850E_UART_ASIM_PS_NONE;
	else if (cflags & PARODD)
		/* Odd parity check/generation.  */
		config |= V850E_UART_ASIM_PS_ODD;
	else
		/* Even parity check/generation.  */
		config |= V850E_UART_ASIM_PS_EVEN;
	if (cflags & CREAD)
		/* Reading enabled.  */
		config |= V850E_UART_ASIM_RXE;

	config |= V850E_UART_ASIM_CAE;
	config |= V850E_UART_ASIM_TXE; /* Writing is always enabled.  */
	config |= V850E_UART_ASIM_ISRM; /* Errors generate a read-irq.  */

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
	register unsigned count = 1 << speed.clk_divlog2;
	while (--count != 0)
		/* nothing */;
}


/* RX/TX interface.  */

/* Return true if all characters awaiting transmission on uart channel N
   have been transmitted.  */
#define v850e_uart_xmit_done(n)						      \
   (! (V850E_UART_ASIF(n) & V850E_UART_ASIF_TXBF))
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
#define v850e_uart_putc(chan, ch)	(V850E_UART_TXB(chan) = (ch))

/* Return latest character read on channel CHAN.  */
#define v850e_uart_getc(chan)		V850E_UART_RXB (chan)

/* Return bit-mask of uart error status.  */
#define v850e_uart_err(chan)		V850E_UART_ASIS (chan)
/* Various error bits set in the error result.  */
#define V850E_UART_ERR_OVERRUN		V850E_UART_ASIS_OVE
#define V850E_UART_ERR_FRAME		V850E_UART_ASIS_FE
#define V850E_UART_ERR_PARITY		V850E_UART_ASIS_PE


#endif /* __V850_V850E_UARTA_H__ */
