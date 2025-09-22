/*	$OpenBSD: cn30xxuart.c,v 1.13 2022/04/06 18:59:27 naddy Exp $	*/

/*
 * Copyright (c) 2001-2004 Opsycon AB  (www.opsycon.se / www.opsycon.com)
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR ``AS IS'' AND ANY EXPRESS
 * OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED
 * WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR ANY
 * DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 *
 */

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/device.h>
#include <sys/tty.h>
#include <sys/conf.h>

#include <dev/ofw/fdt.h>
#include <dev/ofw/openfirm.h>

#include <machine/octeonreg.h>
#include <machine/octeonvar.h>
#include <machine/autoconf.h>
#include <machine/bus.h>
#include <machine/fdt.h>

#include <dev/ic/comreg.h>
#include <dev/ic/comvar.h>
#include <dev/cons.h>

#include <octeon/dev/iobusvar.h>
#include <octeon/dev/cn30xxuartreg.h>

#define OCTEON_UART_FIFO_SIZE		64

int	cn30xxuart_probe(struct device *, void *, void *);
void	cn30xxuart_attach(struct device *, struct device *, void *);
int	cn30xxuart_intr(void *);

const struct cfattach octuart_ca = {
	sizeof(struct com_softc), cn30xxuart_probe, cn30xxuart_attach
};

extern struct cfdriver com_cd;

cons_decl(octuart);

#define  USR_TXFIFO_NOTFULL		2

/* XXX: What is this used for? Removed from stand/boot/uart.c -r1.2 */
static int delay_changed = 1;
int cn30xxuart_delay(void);
void cn30xxuart_wait_txhr_empty(int);

uint8_t	 uartbus_read_1(bus_space_tag_t, bus_space_handle_t, bus_size_t);
void	 uartbus_write_1(bus_space_tag_t, bus_space_handle_t, bus_size_t,
	    uint8_t);

bus_space_t uartbus_tag = {
	.bus_base = PHYS_TO_XKPHYS(0, CCA_NC),
	.bus_private = NULL,
	._space_read_1 = uartbus_read_1,
	._space_write_1 = uartbus_write_1,
	._space_map = iobus_space_map,
	._space_unmap = iobus_space_unmap
};

void
com_fdt_init_cons(void)
{
	comconsiot = &uartbus_tag;
	comconsaddr = OCTEON_UART0_BASE;
	comconsfreq = octeon_ioclock_speed();
	comconsrate = B115200;
	comconscflag = (TTYDEF_CFLAG & ~(CSIZE | PARENB)) | CS8;
}

int
cn30xxuart_probe(struct device *parent, void *match, void *aux)
{
	struct fdt_attach_args *faa = aux;

	return OF_is_compatible(faa->fa_node, "cavium,octeon-3860-uart");
}

void
cn30xxuart_attach(struct device *parent, struct device *self, void *aux)
{
	struct fdt_attach_args *faa = aux;
	struct com_softc *sc = (void *)self;
	int console = 0;

	if (faa->fa_nreg != 1)
		return;

	if (comconsiot == &uartbus_tag && comconsaddr == faa->fa_reg[0].addr)
		console = 1;

	sc->sc_hwflags = 0;
	sc->sc_swflags = 0;
	sc->sc_frequency = octeon_ioclock_speed();
	sc->sc_uarttype = COM_UART_16550A;
	sc->sc_fifolen = OCTEON_UART_FIFO_SIZE;

	if (!console || comconsattached) {
		sc->sc_iot = &uartbus_tag;
		sc->sc_iobase = faa->fa_reg[0].addr;
		if (bus_space_map(sc->sc_iot, sc->sc_iobase, COM_NPORTS, 0,
		    &sc->sc_ioh)) {
			printf(": could not map UART registers\n");
			return;
		}
	} else {
		/* Reuse the early console settings. */
		sc->sc_iot = comconsiot;
		sc->sc_iobase = comconsaddr;
		if (comcnattach(sc->sc_iot, sc->sc_iobase, comconsrate,
		    sc->sc_frequency, comconscflag))
			panic("could not set up serial console");
		sc->sc_ioh = comconsioh;
	}

	com_attach_subr(sc);

	octeon_intr_establish_fdt(faa->fa_node, IPL_TTY, cn30xxuart_intr, sc,
	    sc->sc_dev.dv_xname);
}

int
cn30xxuart_intr(void *arg)
{
	comintr(arg);

	/*
	 * Always return non-zero to prevent console clutter about spurious
	 * interrupts. comstart() enables the transmitter holding register
	 * empty interrupt before adding data to the FIFO, which can trigger
	 * a premature interrupt on the primary CPU in a multiprocessor system.
	 */
	return 1;
}

/*
 * Early console routines.
 */

int
cn30xxuart_delay(void)
{
	int divisor;
	u_char lcr;
	static int d = 0;

	if (!delay_changed)
		return d;
	delay_changed = 0;
	lcr = octeon_xkphys_read_8(MIO_UART0_LCR);
	octeon_xkphys_write_8(MIO_UART0_LCR, lcr | LCR_DLAB);
	divisor = octeon_xkphys_read_8(MIO_UART0_DLL) |
		octeon_xkphys_read_8(MIO_UART0_DLH) << 8;
	octeon_xkphys_write_8(MIO_UART0_LCR, lcr);

	return 10; /* return an approx delay value */
}

void
cn30xxuart_wait_txhr_empty(int d)
{
	while (((octeon_xkphys_read_8(MIO_UART0_LSR) & LSR_TXRDY) == 0) &&
	    ((octeon_xkphys_read_8(MIO_UART0_USR) & USR_TXFIFO_NOTFULL) == 0))
		delay(d);
}

void
octuartcninit(struct consdev *consdev)
{
}

void
octuartcnprobe(struct consdev *consdev)
{
}

void
octuartcnpollc(dev_t dev, int c)
{
}

void
octuartcnputc(dev_t dev, int c)
{
	int d;

	/* 1/10th the time to transmit 1 character (estimate). */
	d = cn30xxuart_delay();
	cn30xxuart_wait_txhr_empty(d);
	octeon_xkphys_write_8(MIO_UART0_RBR, (uint8_t)c);
	cn30xxuart_wait_txhr_empty(d);
}

int
octuartcngetc(dev_t dev)
{
	int c, d;

	/* 1/10th the time to transmit 1 character (estimate). */
	d = cn30xxuart_delay();

	while ((octeon_xkphys_read_8(MIO_UART0_LSR) & LSR_RXRDY) == 0)
		delay(d);

	c = (uint8_t)octeon_xkphys_read_8(MIO_UART0_RBR);

	return (c);
}

/*
 * Bus access routines. These let com(4) work with the 64-bit registers.
 */

uint8_t
uartbus_read_1(bus_space_tag_t tag, bus_space_handle_t handle, bus_size_t off)
{
	return *(volatile uint64_t *)(handle + (off << 3));
}

void
uartbus_write_1(bus_space_tag_t tag, bus_space_handle_t handle, bus_size_t off,
    uint8_t value)
{
	volatile uint64_t *reg = (uint64_t *)(handle + (off << 3));

	*reg = value;
	(void)*reg;
}
