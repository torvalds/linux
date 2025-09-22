/*	$OpenBSD: cyreg.h,v 1.9 2024/09/01 03:08:56 jsg Exp $	*/
/*	$FreeBSD: cyreg.h,v 1.1 1995/07/05 12:15:51 bde Exp $	*/

/*-
 * Copyright (c) 1995 Bruce Evans.
 * All rights reserved.
 *
 * Modified by Timo Rossi, 1996
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 * 3. Neither the name of the author nor the names of contributors
 *    may be used to endorse or promote products derived from this software
 *    without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE AUTHOR OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 */

#ifndef _DEV_IC_CYREG_H_
#define _DEV_IC_CYREG_H_

#include <sys/timeout.h>

/*
 * Definitions for Cyclades Cyclom-Y serial boards.
 */

#define	CY8_SVCACKR		0x100
#define	CY8_SVCACKT		0x200
#define	CY8_SVCACKM		0x300

/* twice this in PCI mode (shifted BUSTYPE bits left) */
#define	CY_CD1400_MEMSPACING	0x400

/* adjustment value for accessing the last 4 cd1400s on Cyclom-32 */
#define CY32_ADDR_FIX           0xe00

#define	CY16_RESET		0x1400
#define	CY_CLEAR_INTR		0x1800	/* intr ack address */

#define	CY_MAX_CD1400s		8	/* for Cyclom-32 */

/* I/O location for enabling interrupts on PCI Cyclom cards */
#define CY_PCI_INTENA           0x68
#define CY_PCI_INTENA_9050      0x4c

/* Cyclom-Y Custom Register for PLX ID (PCI only) */
#define CY_PLX_VER             0x3400          /* PLX version */
#define CY_PLX_9050            0x0b
#define CY_PLX_9060            0x0c
#define CY_PLX_9080            0x0d

#define CY_CLOCK		25000000	/* baud rate clock */
#define CY_CLOCK_60		60000000	/* baud rate clock for newer cd1400s */

/*
 * bustype is actually the shift count for the offset
 * ISA card addresses are multiplied by 2 (shifted 1 bit)
 * and PCI addresses multiplied by 4 (shifted 2 bits)
 */
#define CY_BUSTYPE_ISA 0
#define CY_BUSTYPE_PCI 1

#define RX_FIFO_THRESHOLD  6

/* Automatic RTS (or actually DTR, the RTS and DTR lines need to be exchanged)
 * handshake threshold used if CY_HW_RTS is defined
 */
#define RX_DTR_THRESHOLD   9

/*
 * Maximum number of ports per card 
 */
#define	CY_MAX_PORTS		(CD1400_NO_OF_CHANNELS * CY_MAX_CD1400s)

/*
 * Port number on card encoded in low 5 bits
 * card number in next 2 bits (only space for 4 cards)
 * high bit reserved for dialout flag
 */
#define CY_PORT(x) (minor(x) & 0xf)
#define CY_CARD(x) ((minor(x) >> 5) & 3)
#define CY_DIALOUT(x) ((minor(x) & 0x80) != 0)
#define CY_DIALIN(x) (!CY_DIALOUT(x))

/*
 * read/write cd1400 registers (when cy_port-structure is available)
 */
#define cd_read_reg(cy,reg) bus_space_read_1(cy->cy_memt, cy->cy_memh, \
			  cy->cy_chip_offs+(((reg<<1))<<cy->cy_bustype))

#define cd_write_reg(cy,reg,val) bus_space_write_1(cy->cy_memt, cy->cy_memh, \
			  cy->cy_chip_offs+(((reg<<1))<<cy->cy_bustype), \
			  (val))

/*
 * read/write cd1400 registers (when sc_softc-structure is available)
 */
#define cd_read_reg_sc(sc,chip,reg) bus_space_read_1(sc->sc_memt, \
				 sc->sc_memh, \
				 sc->sc_cd1400_offs[chip]+\
				 (((reg<<1))<<sc->sc_bustype))

#define cd_write_reg_sc(sc,chip,reg,val) bus_space_write_1(sc->sc_memt, \
				 sc->sc_memh, \
				 sc->sc_cd1400_offs[chip]+\
				 (((reg<<1))<<sc->sc_bustype), \
				 (val))

/*
 * ibuf is a simple ring buffer. It is always used two
 * bytes at a time (status and data)
 */
#define IBUF_SIZE (2*512)

/* software state for one port */
struct cy_port {
	int			 cy_port_num;
	bus_space_tag_t		 cy_memt;
	bus_space_handle_t	 cy_memh;
	int			 cy_chip_offs;
	int			 cy_bustype;
	int			 cy_clock;
	struct tty		*cy_tty;
	int			 cy_openflags;
	int			 cy_fifo_overruns;
	int			 cy_ibuf_overruns;
	u_char			 cy_channel_control;	/* last CCR channel
							 * control command
							 * bits */
	u_char			 cy_carrier_stat;	/* copied from MSVR2 */
	u_char			 cy_flags;
	u_char			*cy_ibuf, *cy_ibuf_end;
	u_char			*cy_ibuf_rd_ptr, *cy_ibuf_wr_ptr;
#ifdef CY_DEBUG1
	int			 cy_rx_int_count;
	int			 cy_tx_int_count;
	int			 cy_modem_int_count;
	int			 cy_start_count;
#endif /* CY_DEBUG1 */
};

#define CYF_CARRIER_CHANGED  0x01
#define CYF_START_BREAK      0x02
#define CYF_END_BREAK        0x04
#define CYF_STOP             0x08
#define CYF_SEND_NUL         0x10
#define CYF_START            0x20

/* software state for one card */
struct cy_softc {
	struct device		 sc_dev;
	struct timeout		 sc_poll_to;
	int			 sc_events;
	void			*sc_ih;
	bus_space_tag_t		 sc_memt;
	bus_space_handle_t	 sc_memh;
	int			 sc_bustype;
	int			 sc_nports; /* number of ports on this card */
	int			 sc_cd1400_offs[CY_MAX_CD1400s];
	struct cy_port		 sc_ports[CY_MAX_PORTS];
	int			 sc_nr_cd1400s;
#ifdef CY_DEBUG1
	int			 sc_poll_count1;
	int			 sc_poll_count2;
#endif
};

int	cy_probe_common(bus_space_tag_t, bus_space_handle_t, int);
void	cy_attach(struct device *, struct device *);
int	cy_intr(void *);

#endif	/* _DEV_IC_CYREG_H_ */
