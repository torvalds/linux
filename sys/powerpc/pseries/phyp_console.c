/*-
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD
 *
 * Copyright (C) 2011 by Nathan Whitehorn. All rights reserved.
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
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR ``AS IS'' AND ANY EXPRESS OR
 * IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES
 * OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED.
 * IN NO EVENT SHALL TOOLS GMBH BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
 * SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO,
 * PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS;
 * OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY,
 * WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR
 * OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF
 * ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");

#include <sys/param.h>
#include <sys/kdb.h>
#include <sys/kernel.h>
#include <sys/priv.h>
#include <sys/systm.h>
#include <sys/module.h>
#include <sys/types.h>
#include <sys/conf.h>
#include <sys/cons.h>
#include <sys/tty.h>
#include <machine/bus.h>

#include <dev/ofw/openfirm.h>
#include <dev/ofw/ofw_bus.h>
#include <dev/ofw/ofw_bus_subr.h>
#include <dev/uart/uart.h>
#include <dev/uart/uart_cpu.h>
#include <dev/uart/uart_bus.h>

#include "phyp-hvcall.h"
#include "uart_if.h"

struct uart_phyp_softc {
	device_t dev;
	phandle_t node;
	int vtermid;

	struct tty *tp;
	struct resource *irqres;
	int irqrid;
	struct callout callout;
	void *sc_icookie;
	int polltime;

	struct mtx sc_mtx;
	int protocol;

	union {
		uint64_t u64[2];
		char str[16];
	} phyp_inbuf;
	uint64_t inbuflen;
	uint8_t outseqno;
};

static struct uart_phyp_softc	*console_sc = NULL;
#if defined(KDB)
static int			alt_break_state;
#endif

enum {
	HVTERM1, HVTERMPROT
};

#define VS_DATA_PACKET_HEADER		0xff
#define VS_CONTROL_PACKET_HEADER	0xfe
#define  VSV_SET_MODEM_CTL		0x01
#define  VSV_MODEM_CTL_UPDATE		0x02
#define  VSV_RENEGOTIATE_CONNECTION	0x03
#define VS_QUERY_PACKET_HEADER		0xfd
#define  VSV_SEND_VERSION_NUMBER	0x01
#define  VSV_SEND_MODEM_CTL_STATUS	0x02
#define VS_QUERY_RESPONSE_PACKET_HEADER	0xfc

static int uart_phyp_probe(device_t dev);
static int uart_phyp_attach(device_t dev);
static void uart_phyp_intr(void *v);

static device_method_t uart_phyp_methods[] = {
	/* Device interface */
	DEVMETHOD(device_probe,		uart_phyp_probe),
	DEVMETHOD(device_attach,	uart_phyp_attach),

	DEVMETHOD_END
};

static driver_t uart_phyp_driver = {
	"uart",
	uart_phyp_methods,
	sizeof(struct uart_phyp_softc),
};
 
DRIVER_MODULE(uart_phyp, vdevice, uart_phyp_driver, uart_devclass, 0, 0);

static cn_probe_t uart_phyp_cnprobe;
static cn_init_t uart_phyp_cninit;
static cn_term_t uart_phyp_cnterm;
static cn_getc_t uart_phyp_cngetc;
static cn_putc_t uart_phyp_cnputc;
static cn_grab_t uart_phyp_cngrab;
static cn_ungrab_t uart_phyp_cnungrab;

CONSOLE_DRIVER(uart_phyp);

static void uart_phyp_ttyoutwakeup(struct tty *tp);

static struct ttydevsw uart_phyp_tty_class = {
	.tsw_flags	= TF_INITLOCK|TF_CALLOUT,
	.tsw_outwakeup	= uart_phyp_ttyoutwakeup,
};

static int
uart_phyp_probe_node(struct uart_phyp_softc *sc)
{
	phandle_t node = sc->node;
	uint32_t reg;
	char buf[64];

	sc->inbuflen = 0;
	sc->outseqno = 0;

	if (OF_getprop(node, "name", buf, sizeof(buf)) <= 0)
		return (ENXIO);
	if (strcmp(buf, "vty") != 0)
		return (ENXIO);

	if (OF_getprop(node, "device_type", buf, sizeof(buf)) <= 0)
		return (ENXIO);
	if (strcmp(buf, "serial") != 0)
		return (ENXIO);

	reg = -1;
	OF_getencprop(node, "reg", &reg, sizeof(reg));
	if (reg == -1)
		return (ENXIO);
	sc->vtermid = reg;
	sc->node = node;

	if (OF_getprop(node, "compatible", buf, sizeof(buf)) <= 0)
		return (ENXIO);
	if (strcmp(buf, "hvterm1") == 0) {
		sc->protocol = HVTERM1;
		return (0);
	} else if (strcmp(buf, "hvterm-protocol") == 0) {
		sc->protocol = HVTERMPROT;
		return (0);
	}

	return (ENXIO);
}

static int
uart_phyp_probe(device_t dev)
{
	const char *name;
	struct uart_phyp_softc sc;
	int err;

	name = ofw_bus_get_name(dev);
	if (name == NULL || strcmp(name, "vty") != 0)
		return (ENXIO);

	sc.node = ofw_bus_get_node(dev);
	err = uart_phyp_probe_node(&sc);
	if (err != 0)
		return (err);

	device_set_desc(dev, "POWER Hypervisor Virtual Serial Port");

	return (err);
}

static void
uart_phyp_cnprobe(struct consdev *cp)
{
	char buf[64];
	ihandle_t stdout;
	phandle_t input, chosen;
	static struct uart_phyp_softc sc;

	if ((chosen = OF_finddevice("/chosen")) == -1)
		goto fail;

	/* Check if OF has an active stdin/stdout */
	input = -1;
	if (OF_getencprop(chosen, "stdout", &stdout,
	    sizeof(stdout)) == sizeof(stdout) && stdout != 0)
		input = OF_instance_to_package(stdout);
	if (input == -1)
		goto fail;

	if (OF_getprop(input, "device_type", buf, sizeof(buf)) == -1)
		goto fail;
	if (strcmp(buf, "serial") != 0)
		goto fail;

	sc.node = input;
	if (uart_phyp_probe_node(&sc) != 0)
		goto fail;
	mtx_init(&sc.sc_mtx, "uart_phyp", NULL, MTX_SPIN | MTX_QUIET |
	    MTX_NOWITNESS);

	cp->cn_pri = CN_NORMAL;
	console_sc = &sc;
	return;
	
fail:
	cp->cn_pri = CN_DEAD;
	return;
}

static int
uart_phyp_attach(device_t dev)
{
	struct uart_phyp_softc *sc;
	int unit;

	sc = device_get_softc(dev);
	sc->dev = dev;
	sc->node = ofw_bus_get_node(dev);
	uart_phyp_probe_node(sc);

	unit = device_get_unit(dev);
	sc->tp = tty_alloc(&uart_phyp_tty_class, sc);
	mtx_init(&sc->sc_mtx, device_get_nameunit(dev), NULL,
	    MTX_SPIN | MTX_QUIET | MTX_NOWITNESS);

	if (console_sc != NULL && console_sc->vtermid == sc->vtermid) {
		sc->outseqno = console_sc->outseqno;
		console_sc = sc;
		sprintf(uart_phyp_consdev.cn_name, "ttyu%r", unit);
		tty_init_console(sc->tp, 0);
	}

	sc->irqrid = 0;
	sc->irqres = bus_alloc_resource_any(dev, SYS_RES_IRQ, &sc->irqrid,
	    RF_ACTIVE | RF_SHAREABLE);
	if (sc->irqres != NULL) {
		bus_setup_intr(dev, sc->irqres, INTR_TYPE_TTY | INTR_MPSAFE,
		    NULL, uart_phyp_intr, sc, &sc->sc_icookie);
	} else {
		callout_init(&sc->callout, 1);
		sc->polltime = hz / 20;
		if (sc->polltime < 1)
			sc->polltime = 1;
		callout_reset(&sc->callout, sc->polltime, uart_phyp_intr, sc);
	}

	tty_makedev(sc->tp, NULL, "u%r", unit);

	return (0);
}

static void
uart_phyp_cninit(struct consdev *cp)
{

	strcpy(cp->cn_name, "phypcons");
}

static void
uart_phyp_cnterm(struct consdev *cp)
{
}

static int
uart_phyp_get(struct uart_phyp_softc *sc, void *buffer, size_t bufsize)
{
	int err;
	int hdr = 0;

	uart_lock(&sc->sc_mtx);
	if (sc->inbuflen == 0) {
		err = phyp_pft_hcall(H_GET_TERM_CHAR, sc->vtermid,
		    0, 0, 0, &sc->inbuflen, &sc->phyp_inbuf.u64[0],
		    &sc->phyp_inbuf.u64[1]);
		if (err != H_SUCCESS) {
			uart_unlock(&sc->sc_mtx);
			return (-1);
		}
		hdr = 1; 
	}

	if (sc->inbuflen == 0) {
		uart_unlock(&sc->sc_mtx);
		return (0);
	}

	if (bufsize > sc->inbuflen)
		bufsize = sc->inbuflen;

	if ((sc->protocol == HVTERMPROT) && (hdr == 1)) {
		sc->inbuflen = sc->inbuflen - 4;
		/* The VTERM protocol has a 4 byte header, skip it here. */
		memmove(&sc->phyp_inbuf.str[0], &sc->phyp_inbuf.str[4],
		    sc->inbuflen);
	}

	memcpy(buffer, sc->phyp_inbuf.str, bufsize);
	sc->inbuflen -= bufsize;
	if (sc->inbuflen > 0)
		memmove(&sc->phyp_inbuf.str[0], &sc->phyp_inbuf.str[bufsize],
		    sc->inbuflen);

	uart_unlock(&sc->sc_mtx);
	return (bufsize);
}

static int
uart_phyp_put(struct uart_phyp_softc *sc, void *buffer, size_t bufsize)
{
	uint16_t seqno;
	uint64_t len = 0;
	int	err;

	union {
		uint64_t u64[2];
		char bytes[16];
	} cbuf;

	uart_lock(&sc->sc_mtx);
	switch (sc->protocol) {
	case HVTERM1:
		if (bufsize > 16)
			bufsize = 16;
		memcpy(&cbuf, buffer, bufsize);
		len = bufsize;
		break;
	case HVTERMPROT:
		if (bufsize > 12)
			bufsize = 12;
		seqno = sc->outseqno++;
		cbuf.bytes[0] = VS_DATA_PACKET_HEADER;
		cbuf.bytes[1] = 4 + bufsize; /* total length, max 16 bytes */
		cbuf.bytes[2] = (seqno >> 8) & 0xff;
		cbuf.bytes[3] = seqno & 0xff;
		memcpy(&cbuf.bytes[4], buffer, bufsize);
		len = 4 + bufsize;
		break;
	}

	do {
	    err = phyp_hcall(H_PUT_TERM_CHAR, sc->vtermid, len, cbuf.u64[0],
			    cbuf.u64[1]);
		DELAY(100);
	} while (err == H_BUSY);

	uart_unlock(&sc->sc_mtx);

	return (bufsize);
}

static int
uart_phyp_cngetc(struct consdev *cp)
{
	unsigned char c;
	int retval;

	retval = uart_phyp_get(console_sc, &c, 1);
	if (retval != 1)
		return (-1);
#if defined(KDB)
	kdb_alt_break(c, &alt_break_state);
#endif

	return (c);
}

static void
uart_phyp_cnputc(struct consdev *cp, int c)
{
	unsigned char ch = c;
	uart_phyp_put(console_sc, &ch, 1);
}

static void
uart_phyp_cngrab(struct consdev *cp)
{
}

static void
uart_phyp_cnungrab(struct consdev *cp)
{
}

static void
uart_phyp_ttyoutwakeup(struct tty *tp)
{
	struct uart_phyp_softc *sc;
	char buffer[8];
	int len;

	sc = tty_softc(tp);
	
	while ((len = ttydisc_getc(tp, buffer, sizeof(buffer))) != 0)
		uart_phyp_put(sc, buffer, len);
}

static void
uart_phyp_intr(void *v)
{
	struct uart_phyp_softc *sc = v;
	struct tty *tp = sc->tp;
	unsigned char c;
	int len;

	tty_lock(tp);
	while ((len = uart_phyp_get(sc, &c, 1)) > 0)
		ttydisc_rint(tp, c, 0);
	ttydisc_rint_done(tp);
	tty_unlock(tp);

	if (sc->irqres == NULL)
		callout_reset(&sc->callout, sc->polltime, uart_phyp_intr, sc);
}

