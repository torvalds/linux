/*	$OpenBSD: pluartvar.h,v 1.5 2022/06/27 13:03:32 anton Exp $	*/
/*
 * Copyright (c) 2014 Patrick Wildt <patrick@blueri.se>
 * Copyright (c) 2005 Dale Rahn <drahn@dalerahn.com>
 *
 * Permission to use, copy, modify, and distribute this software for any
 * purpose with or without fee is hereby granted, provided that the above
 * copyright notice and this permission notice appear in all copies.
 *
 * THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
 * WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF
 * MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR
 * ANY SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
 * WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN
 * ACTION OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF
 * OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
 */

struct pluart_softc {
	struct device	sc_dev;
	bus_space_tag_t sc_iot;
	bus_space_handle_t sc_ioh;
	struct soft_intrhand *sc_si;
	void *sc_irq;
	struct tty	*sc_tty;
	struct timeout	sc_diag_tmo;
	struct timeout	sc_dtr_tmo;
	int		sc_overflows;
	int		sc_floods;
	int		sc_errors;
	int		sc_halt;
	u_int16_t	sc_ucr1;
	u_int16_t	sc_ucr2;
	u_int16_t	sc_ucr3;
	u_int16_t	sc_ucr4;
	u_int8_t	sc_hwflags;
	u_int8_t	sc_hwrev;
#define COM_HW_NOIEN    0x01
#define COM_HW_FIFO     0x02
#define COM_HW_SIR      0x20
#define COM_HW_CONSOLE  0x40
#define COM_HW_SBSA	0x80
	u_int8_t	sc_swflags;
#define COM_SW_SOFTCAR  0x01
#define COM_SW_CLOCAL   0x02
#define COM_SW_CRTSCTS  0x04
#define COM_SW_MDMBUF   0x08
#define COM_SW_PPS      0x10
	int		sc_fifolen;
	int		sc_imsc;
	int		sc_clkfreq;

	u_int8_t	sc_initialize;
	u_int8_t	sc_cua;
	u_int16_t 	*sc_ibuf, *sc_ibufp, *sc_ibufhigh, *sc_ibufend;
#define UART_IBUFSIZE 128
#define UART_IHIGHWATER 100
	u_int16_t		sc_ibufs[2][UART_IBUFSIZE];

	struct clk	*sc_clk;
};

void	pluart_attach_common(struct pluart_softc *, int);
int	pluart_intr(void *);

int	pluartcnattach(bus_space_tag_t iot, bus_addr_t iobase, int rate,
	    tcflag_t cflag);
