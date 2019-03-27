/*	$OpenBSD: sbbc.c,v 1.7 2009/11/09 17:53:39 nicm Exp $	*/
/*-
 * SPDX-License-Identifier: (ISC AND BSD-2-Clause-FreeBSD)
 *
 * Copyright (c) 2008 Mark Kettenis
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
/*-
 * Copyright (c) 2010 Marius Strobl <marius@FreeBSD.org>
 * All rights reserved.
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

#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/bus.h>
#include <sys/clock.h>
#include <sys/endian.h>
#include <sys/kernel.h>
#include <sys/lock.h>
#include <sys/module.h>
#include <sys/mutex.h>
#include <sys/resource.h>
#include <sys/rman.h>

#include <dev/ofw/ofw_bus.h>
#include <dev/ofw/openfirm.h>

#include <machine/bus.h>
#include <machine/cpu.h>
#include <machine/resource.h>

#include <dev/pci/pcireg.h>
#include <dev/pci/pcivar.h>
#include <dev/uart/uart.h>
#include <dev/uart/uart_cpu.h>
#include <dev/uart/uart_bus.h>

#include "clock_if.h"
#include "uart_if.h"

#define	SBBC_PCI_BAR		PCIR_BAR(0)
#define	SBBC_PCI_VENDOR		0x108e
#define	SBBC_PCI_PRODUCT	0xc416

#define	SBBC_REGS_OFFSET	0x800000
#define	SBBC_REGS_SIZE		0x6230
#define	SBBC_EPLD_OFFSET	0x8e0000
#define	SBBC_EPLD_SIZE		0x20
#define	SBBC_SRAM_OFFSET	0x900000
#define	SBBC_SRAM_SIZE		0x20000	/* 128KB SRAM */

#define	SBBC_PCI_INT_STATUS	0x2320
#define	SBBC_PCI_INT_ENABLE	0x2330
#define	SBBC_PCI_ENABLE_INT_A	0x11

#define	SBBC_EPLD_INTERRUPT	0x13
#define	SBBC_EPLD_INTERRUPT_ON	0x01

#define	SBBC_SRAM_CONS_IN		0x00000001
#define	SBBC_SRAM_CONS_OUT		0x00000002
#define	SBBC_SRAM_CONS_BRK		0x00000004
#define	SBBC_SRAM_CONS_SPACE_IN		0x00000008
#define	SBBC_SRAM_CONS_SPACE_OUT	0x00000010

#define	SBBC_TAG_KEY_SIZE	8
#define	SBBC_TAG_KEY_SCSOLIE	"SCSOLIE"	/* SC -> OS int. enable */
#define	SBBC_TAG_KEY_SCSOLIR	"SCSOLIR"	/* SC -> OS int. reason */
#define	SBBC_TAG_KEY_SOLCONS	"SOLCONS"	/* OS console buffer */
#define	SBBC_TAG_KEY_SOLSCIE	"SOLSCIE"	/* OS -> SC int. enable */
#define	SBBC_TAG_KEY_SOLSCIR	"SOLSCIR"	/* OS -> SC int. reason */
#define	SBBC_TAG_KEY_TODDATA	"TODDATA"	/* OS TOD struct */
#define	SBBC_TAG_OFF(x)		offsetof(struct sbbc_sram_tag, x)

struct sbbc_sram_tag {
	char		tag_key[SBBC_TAG_KEY_SIZE];
	uint32_t	tag_size;
	uint32_t	tag_offset;
} __packed;

#define	SBBC_TOC_MAGIC		"TOCSRAM"
#define	SBBC_TOC_MAGIC_SIZE	8
#define	SBBC_TOC_TAGS_MAX	32
#define	SBBC_TOC_OFF(x)		offsetof(struct sbbc_sram_toc, x)

struct sbbc_sram_toc {
	char			toc_magic[SBBC_TOC_MAGIC_SIZE];
	uint8_t			toc_reserved;
	uint8_t			toc_type;
	uint16_t		toc_version;
	uint32_t		toc_ntags;
	struct sbbc_sram_tag	toc_tag[SBBC_TOC_TAGS_MAX];
} __packed;

#define	SBBC_TOD_MAGIC		0x54443100	/* "TD1" */
#define	SBBC_TOD_VERSION	1
#define	SBBC_TOD_OFF(x)		offsetof(struct sbbc_sram_tod, x)

struct sbbc_sram_tod {
	uint32_t	tod_magic;
	uint32_t	tod_version;
	uint64_t	tod_time;
	uint64_t	tod_skew;
	uint32_t	tod_reserved;
	uint32_t	tod_heartbeat;
	uint32_t	tod_timeout;
} __packed;

#define	SBBC_CONS_MAGIC		0x434f4e00	/* "CON" */
#define	SBBC_CONS_VERSION	1
#define	SBBC_CONS_OFF(x)	offsetof(struct sbbc_sram_cons, x)

struct sbbc_sram_cons {
	uint32_t cons_magic;
	uint32_t cons_version;
	uint32_t cons_size;

	uint32_t cons_in_begin;
	uint32_t cons_in_end;
	uint32_t cons_in_rdptr;
	uint32_t cons_in_wrptr;

	uint32_t cons_out_begin;
	uint32_t cons_out_end;
	uint32_t cons_out_rdptr;
	uint32_t cons_out_wrptr;
} __packed;

struct sbbc_softc {
	struct resource *sc_res;
};

#define	SBBC_READ_N(wdth, offs)						\
	bus_space_read_ ## wdth((bst), (bsh), (offs))
#define	SBBC_WRITE_N(wdth, offs, val)					\
	bus_space_write_ ## wdth((bst), (bsh), (offs), (val))

#define	SBBC_READ_1(offs)						\
	SBBC_READ_N(1, (offs))
#define	SBBC_READ_2(offs)						\
	bswap16(SBBC_READ_N(2, (offs)))
#define	SBBC_READ_4(offs)						\
	bswap32(SBBC_READ_N(4, (offs)))
#define	SBBC_READ_8(offs)						\
	bswap64(SBBC_READ_N(8, (offs)))
#define	SBBC_WRITE_1(offs, val)						\
	SBBC_WRITE_N(1, (offs), (val))
#define	SBBC_WRITE_2(offs, val)						\
	SBBC_WRITE_N(2, (offs), bswap16(val))
#define	SBBC_WRITE_4(offs, val)						\
	SBBC_WRITE_N(4, (offs), bswap32(val))
#define	SBBC_WRITE_8(offs, val)						\
	SBBC_WRITE_N(8, (offs), bswap64(val))

#define	SBBC_REGS_READ_1(offs)						\
	SBBC_READ_1((offs) + SBBC_REGS_OFFSET)
#define	SBBC_REGS_READ_2(offs)						\
	SBBC_READ_2((offs) + SBBC_REGS_OFFSET)
#define	SBBC_REGS_READ_4(offs)						\
	SBBC_READ_4((offs) + SBBC_REGS_OFFSET)
#define	SBBC_REGS_READ_8(offs)						\
	SBBC_READ_8((offs) + SBBC_REGS_OFFSET)
#define	SBBC_REGS_WRITE_1(offs, val)					\
	SBBC_WRITE_1((offs) + SBBC_REGS_OFFSET, (val))
#define	SBBC_REGS_WRITE_2(offs, val)					\
	SBBC_WRITE_2((offs) + SBBC_REGS_OFFSET, (val))
#define	SBBC_REGS_WRITE_4(offs, val)					\
	SBBC_WRITE_4((offs) + SBBC_REGS_OFFSET, (val))
#define	SBBC_REGS_WRITE_8(offs, val)					\
	SBBC_WRITE_8((offs) + SBBC_REGS_OFFSET, (val))

#define	SBBC_EPLD_READ_1(offs)						\
	SBBC_READ_1((offs) + SBBC_EPLD_OFFSET)
#define	SBBC_EPLD_READ_2(offs)						\
	SBBC_READ_2((offs) + SBBC_EPLD_OFFSET)
#define	SBBC_EPLD_READ_4(offs)						\
	SBBC_READ_4((offs) + SBBC_EPLD_OFFSET)
#define	SBBC_EPLD_READ_8(offs)						\
	SBBC_READ_8((offs) + SBBC_EPLD_OFFSET)
#define	SBBC_EPLD_WRITE_1(offs, val)					\
	SBBC_WRITE_1((offs) + SBBC_EPLD_OFFSET, (val))
#define	SBBC_EPLD_WRITE_2(offs, val)					\
	SBBC_WRITE_2((offs) + SBBC_EPLD_OFFSET, (val))
#define	SBBC_EPLD_WRITE_4(offs, val)					\
	SBBC_WRITE_4((offs) + SBBC_EPLD_OFFSET, (val))
#define	SBBC_EPLD_WRITE_8(offs, val)					\
	SBBC_WRITE_8((offs) + SBBC_EPLD_OFFSET, (val))

#define	SBBC_SRAM_READ_1(offs)						\
	SBBC_READ_1((offs) + SBBC_SRAM_OFFSET)
#define	SBBC_SRAM_READ_2(offs)						\
	SBBC_READ_2((offs) + SBBC_SRAM_OFFSET)
#define	SBBC_SRAM_READ_4(offs)						\
	SBBC_READ_4((offs) + SBBC_SRAM_OFFSET)
#define	SBBC_SRAM_READ_8(offs)						\
	SBBC_READ_8((offs) + SBBC_SRAM_OFFSET)
#define	SBBC_SRAM_WRITE_1(offs, val)					\
	SBBC_WRITE_1((offs) + SBBC_SRAM_OFFSET, (val))
#define	SBBC_SRAM_WRITE_2(offs, val)					\
	SBBC_WRITE_2((offs) + SBBC_SRAM_OFFSET, (val))
#define	SBBC_SRAM_WRITE_4(offs, val)					\
	SBBC_WRITE_4((offs) + SBBC_SRAM_OFFSET, (val))
#define	SBBC_SRAM_WRITE_8(offs, val)					\
	SBBC_WRITE_8((offs) + SBBC_SRAM_OFFSET, (val))

#define	SUNW_SETCONSINPUT	"SUNW,set-console-input"
#define	SUNW_SETCONSINPUT_CLNT	"CON_CLNT"
#define	SUNW_SETCONSINPUT_OBP	"CON_OBP"

static u_int sbbc_console;

static uint32_t	sbbc_scsolie;
static uint32_t	sbbc_scsolir;
static uint32_t	sbbc_solcons;
static uint32_t	sbbc_solscie;
static uint32_t	sbbc_solscir;
static uint32_t	sbbc_toddata;

/*
 * internal helpers
 */
static int sbbc_parse_toc(bus_space_tag_t bst, bus_space_handle_t bsh);
static inline void sbbc_send_intr(bus_space_tag_t bst,
    bus_space_handle_t bsh);
static const char *sbbc_serengeti_set_console_input(char *new);

/*
 * SBBC PCI interface
 */
static bus_activate_resource_t sbbc_bus_activate_resource;
static bus_adjust_resource_t sbbc_bus_adjust_resource;
static bus_deactivate_resource_t sbbc_bus_deactivate_resource;
static bus_alloc_resource_t sbbc_bus_alloc_resource;
static bus_release_resource_t sbbc_bus_release_resource;
static bus_get_resource_list_t sbbc_bus_get_resource_list;
static bus_setup_intr_t sbbc_bus_setup_intr;
static bus_teardown_intr_t sbbc_bus_teardown_intr;

static device_attach_t sbbc_pci_attach;
static device_probe_t sbbc_pci_probe;

static clock_gettime_t sbbc_tod_gettime;
static clock_settime_t sbbc_tod_settime;

static device_method_t sbbc_pci_methods[] = {
	/* Device interface */
	DEVMETHOD(device_probe,		sbbc_pci_probe),
	DEVMETHOD(device_attach,	sbbc_pci_attach),

	DEVMETHOD(bus_alloc_resource,	sbbc_bus_alloc_resource),
	DEVMETHOD(bus_activate_resource,sbbc_bus_activate_resource),
	DEVMETHOD(bus_deactivate_resource,sbbc_bus_deactivate_resource),
	DEVMETHOD(bus_adjust_resource,	sbbc_bus_adjust_resource),
	DEVMETHOD(bus_release_resource,	sbbc_bus_release_resource),
	DEVMETHOD(bus_setup_intr,	sbbc_bus_setup_intr),
	DEVMETHOD(bus_teardown_intr,	sbbc_bus_teardown_intr),
	DEVMETHOD(bus_get_resource,	bus_generic_rl_get_resource),
	DEVMETHOD(bus_get_resource_list, sbbc_bus_get_resource_list),

	/* clock interface */
	DEVMETHOD(clock_gettime,	sbbc_tod_gettime),
	DEVMETHOD(clock_settime,	sbbc_tod_settime),

	DEVMETHOD_END
};

static devclass_t sbbc_devclass;

DEFINE_CLASS_0(sbbc, sbbc_driver, sbbc_pci_methods, sizeof(struct sbbc_softc));
DRIVER_MODULE(sbbc, pci, sbbc_driver, sbbc_devclass, NULL, NULL);

static int
sbbc_pci_probe(device_t dev)
{

	if (pci_get_vendor(dev) == SBBC_PCI_VENDOR &&
	    pci_get_device(dev) == SBBC_PCI_PRODUCT) {
		device_set_desc(dev, "Sun BootBus controller");
		return (BUS_PROBE_DEFAULT);
	}
	return (ENXIO);
}

static int
sbbc_pci_attach(device_t dev)
{
	struct sbbc_softc *sc;
	struct timespec ts;
	device_t child;
	bus_space_tag_t bst;
	bus_space_handle_t bsh;
	phandle_t node;
	int error, rid;
	uint32_t val;

	/* Nothing to do if we're not the chosen one. */
	if ((node = OF_finddevice("/chosen")) == -1) {
		device_printf(dev, "failed to find /chosen\n");
		return (ENXIO);
	}
	if (OF_getprop(node, "iosram", &node, sizeof(node)) == -1) {
		device_printf(dev, "failed to get iosram\n");
		return (ENXIO);
	}
	if (node != ofw_bus_get_node(dev))
		return (0);

	sc = device_get_softc(dev);
	rid = SBBC_PCI_BAR;
	sc->sc_res = bus_alloc_resource_any(dev, SYS_RES_MEMORY, &rid,
	    RF_ACTIVE);
	if (sc->sc_res == NULL) {
		device_printf(dev, "failed to allocate resources\n");
		return (ENXIO);
	}
	bst = rman_get_bustag(sc->sc_res);
	bsh = rman_get_bushandle(sc->sc_res);
	if (sbbc_console != 0) {
		/* Once again the interrupt pin isn't set. */
		if (pci_get_intpin(dev) == 0)
			pci_set_intpin(dev, 1);
		child = device_add_child(dev, NULL, -1);
		if (child == NULL)
			device_printf(dev, "failed to add UART device\n");
		error = bus_generic_attach(dev);
		if (error != 0)
			device_printf(dev, "failed to attach UART device\n");
	} else {
		error = sbbc_parse_toc(bst, bsh);
		if (error != 0) {
			device_printf(dev, "failed to parse TOC\n");
			if (sbbc_console != 0) {
				bus_release_resource(dev, SYS_RES_MEMORY, rid,
				    sc->sc_res);
				return (error);
			}
		}
	}
	if (sbbc_toddata != 0) {
		if ((val = SBBC_SRAM_READ_4(sbbc_toddata +
		    SBBC_TOD_OFF(tod_magic))) != SBBC_TOD_MAGIC)
			device_printf(dev, "invalid TOD magic %#x\n", val);
		else if ((val = SBBC_SRAM_READ_4(sbbc_toddata +
		    SBBC_TOD_OFF(tod_version))) < SBBC_TOD_VERSION)
			device_printf(dev, "invalid TOD version %#x\n", val);
		else {
			clock_register(dev, 1000000); /* 1 sec. resolution */
			if (bootverbose) {
				sbbc_tod_gettime(dev, &ts);
				device_printf(dev,
				    "current time: %ld.%09ld\n",
				    (long)ts.tv_sec, ts.tv_nsec);
			}
		}
	}
	return (0);
}

/*
 * Note that the bus methods don't pass-through the uart(4) requests but act
 * as if they would come from sbbc(4) in order to avoid complications with
 * pci(4) (actually, uart(4) isn't a real child but rather a function of
 * sbbc(4) anyway).
 */

static struct resource *
sbbc_bus_alloc_resource(device_t dev, device_t child __unused, int type,
    int *rid, rman_res_t start, rman_res_t end, rman_res_t count, u_int flags)
{
	struct sbbc_softc *sc;

	sc = device_get_softc(dev);
	switch (type) {
	case SYS_RES_IRQ:
		return (bus_generic_alloc_resource(dev, dev, type, rid, start,
		    end, count, flags));
	case SYS_RES_MEMORY:
		return (sc->sc_res);
	default:
		return (NULL);
	}
}

static int
sbbc_bus_activate_resource(device_t bus, device_t child, int type, int rid,
    struct resource *res)
{

	if (type == SYS_RES_MEMORY)
		return (0);
	return (bus_generic_activate_resource(bus, child, type, rid, res));
}

static int
sbbc_bus_deactivate_resource(device_t bus, device_t child, int type, int rid,
    struct resource *res)
{

	if (type == SYS_RES_MEMORY)
		return (0);
	return (bus_generic_deactivate_resource(bus, child, type, rid, res));
}

static int
sbbc_bus_adjust_resource(device_t bus __unused, device_t child __unused,
    int type __unused, struct resource *res __unused, rman_res_t start __unused,
    rman_res_t end __unused)
{

	return (ENXIO);
}

static int
sbbc_bus_release_resource(device_t dev, device_t child __unused, int type,
    int rid, struct resource *res)
{

	if (type == SYS_RES_IRQ)
		return (bus_generic_release_resource(dev, dev, type, rid,
		    res));
	return (0);
}

static struct resource_list *
sbbc_bus_get_resource_list(device_t dev, device_t child __unused)
{

	return (bus_generic_get_resource_list(dev, dev));
}

static int
sbbc_bus_setup_intr(device_t dev, device_t child __unused,
    struct resource *res, int flags, driver_filter_t *filt,
    driver_intr_t *intr, void *arg, void **cookiep)
{

	return (bus_generic_setup_intr(dev, dev, res, flags, filt, intr, arg,
	    cookiep));
}

static int
sbbc_bus_teardown_intr(device_t dev, device_t child __unused,
    struct resource *res, void *cookie)
{

	return (bus_generic_teardown_intr(dev, dev, res, cookie));
}

/*
 * internal helpers
 */
static int
sbbc_parse_toc(bus_space_tag_t bst, bus_space_handle_t bsh)
{
	char buf[MAX(SBBC_TAG_KEY_SIZE, SBBC_TOC_MAGIC_SIZE)];
	bus_size_t tag;
	phandle_t node;
	uint32_t off, sram_toc;
	u_int i, tags;

	if ((node = OF_finddevice("/chosen")) == -1)
		return (ENXIO);
	/* SRAM TOC offset defaults to 0. */
	if (OF_getprop(node, "iosram-toc", &sram_toc, sizeof(sram_toc)) <= 0)
		sram_toc = 0;

	bus_space_read_region_1(bst, bsh, SBBC_SRAM_OFFSET + sram_toc +
	    SBBC_TOC_OFF(toc_magic), buf, SBBC_TOC_MAGIC_SIZE);
	buf[SBBC_TOC_MAGIC_SIZE - 1] = '\0';
	if (strcmp(buf, SBBC_TOC_MAGIC) != 0)
		return (ENXIO);

	tags = SBBC_SRAM_READ_4(sram_toc + SBBC_TOC_OFF(toc_ntags));
	for (i = 0; i < tags; i++) {
		tag = sram_toc + SBBC_TOC_OFF(toc_tag) +
		    i * sizeof(struct sbbc_sram_tag);
		bus_space_read_region_1(bst, bsh, SBBC_SRAM_OFFSET + tag +
		    SBBC_TAG_OFF(tag_key), buf, SBBC_TAG_KEY_SIZE);
		buf[SBBC_TAG_KEY_SIZE - 1] = '\0';
		off = SBBC_SRAM_READ_4(tag + SBBC_TAG_OFF(tag_offset));
		if (strcmp(buf, SBBC_TAG_KEY_SCSOLIE) == 0)
			sbbc_scsolie = off;
		else if (strcmp(buf, SBBC_TAG_KEY_SCSOLIR) == 0)
			sbbc_scsolir = off;
		else if (strcmp(buf, SBBC_TAG_KEY_SOLCONS) == 0)
			sbbc_solcons = off;
		else if (strcmp(buf, SBBC_TAG_KEY_SOLSCIE) == 0)
			sbbc_solscie = off;
		else if (strcmp(buf, SBBC_TAG_KEY_SOLSCIR) == 0)
			sbbc_solscir = off;
		else if (strcmp(buf, SBBC_TAG_KEY_TODDATA) == 0)
			sbbc_toddata = off;
	}
	return (0);
}

static const char *
sbbc_serengeti_set_console_input(char *new)
{
	struct {
		cell_t name;
		cell_t nargs;
		cell_t nreturns;
		cell_t new;
		cell_t old;
	} args = {
		(cell_t)SUNW_SETCONSINPUT,
		1,
		1,
	};

	args.new = (cell_t)new;
	if (ofw_entry(&args) == -1)
		return (NULL);
	return ((const char *)args.old);
}

static inline void
sbbc_send_intr(bus_space_tag_t bst, bus_space_handle_t bsh)
{

	SBBC_EPLD_WRITE_1(SBBC_EPLD_INTERRUPT, SBBC_EPLD_INTERRUPT_ON);
	bus_space_barrier(bst, bsh, SBBC_EPLD_OFFSET + SBBC_EPLD_INTERRUPT, 1,
	    BUS_SPACE_BARRIER_READ | BUS_SPACE_BARRIER_WRITE);
}

/*
 * TOD interface
 */
static int
sbbc_tod_gettime(device_t dev, struct timespec *ts)
{
	struct sbbc_softc *sc;
	bus_space_tag_t bst;
	bus_space_handle_t bsh;

	sc = device_get_softc(dev);
	bst = rman_get_bustag(sc->sc_res);
	bsh = rman_get_bushandle(sc->sc_res);

	ts->tv_sec = SBBC_SRAM_READ_8(sbbc_toddata + SBBC_TOD_OFF(tod_time)) +
	    SBBC_SRAM_READ_8(sbbc_toddata + SBBC_TOD_OFF(tod_skew));
	ts->tv_nsec = 0;
	return (0);
}

static int
sbbc_tod_settime(device_t dev, struct timespec *ts)
{
	struct sbbc_softc *sc;
	bus_space_tag_t bst;
	bus_space_handle_t bsh;

	sc = device_get_softc(dev);
	bst = rman_get_bustag(sc->sc_res);
	bsh = rman_get_bushandle(sc->sc_res);

	SBBC_SRAM_WRITE_8(sbbc_toddata + SBBC_TOD_OFF(tod_skew), ts->tv_sec -
	    SBBC_SRAM_READ_8(sbbc_toddata + SBBC_TOD_OFF(tod_time)));
	return (0);
}

/*
 * UART bus front-end
 */
static device_probe_t sbbc_uart_sbbc_probe;

static device_method_t sbbc_uart_sbbc_methods[] = {
	/* Device interface */
	DEVMETHOD(device_probe,		sbbc_uart_sbbc_probe),
	DEVMETHOD(device_attach,	uart_bus_attach),
	DEVMETHOD(device_detach,	uart_bus_detach),

	DEVMETHOD_END
};

DEFINE_CLASS_0(uart, sbbc_uart_driver, sbbc_uart_sbbc_methods,
    sizeof(struct uart_softc));
DRIVER_MODULE(uart, sbbc, sbbc_uart_driver, uart_devclass, NULL, NULL);

static int
sbbc_uart_sbbc_probe(device_t dev)
{
	struct uart_softc *sc;

	sc = device_get_softc(dev);
	sc->sc_class = &uart_sbbc_class;
	device_set_desc(dev, "Serengeti console");
	return (uart_bus_probe(dev, 0, 0, 0, SBBC_PCI_BAR, 0, 0));
}

/*
 * Low-level UART interface
 */
static int sbbc_uart_probe(struct uart_bas *bas);
static void sbbc_uart_init(struct uart_bas *bas, int baudrate, int databits,
    int stopbits, int parity);
static void sbbc_uart_term(struct uart_bas *bas);
static void sbbc_uart_putc(struct uart_bas *bas, int c);
static int sbbc_uart_rxready(struct uart_bas *bas);
static int sbbc_uart_getc(struct uart_bas *bas, struct mtx *hwmtx);

static struct uart_ops sbbc_uart_ops = {
	.probe = sbbc_uart_probe,
	.init = sbbc_uart_init,
	.term = sbbc_uart_term,
	.putc = sbbc_uart_putc,
	.rxready = sbbc_uart_rxready,
	.getc = sbbc_uart_getc,
};

static int
sbbc_uart_probe(struct uart_bas *bas)
{
	bus_space_tag_t bst;
	bus_space_handle_t bsh;
	int error;

	sbbc_console = 1;
	bst = bas->bst;
	bsh = bas->bsh;
	error = sbbc_parse_toc(bst, bsh);
	if (error != 0)
		return (error);

	if (sbbc_scsolie == 0 || sbbc_scsolir == 0 || sbbc_solcons == 0 ||
	    sbbc_solscie == 0 || sbbc_solscir == 0)
		return (ENXIO);

	if (SBBC_SRAM_READ_4(sbbc_solcons + SBBC_CONS_OFF(cons_magic)) !=
	    SBBC_CONS_MAGIC || SBBC_SRAM_READ_4(sbbc_solcons +
	    SBBC_CONS_OFF(cons_version)) < SBBC_CONS_VERSION)
		return (ENXIO);
	return (0);
}

static void
sbbc_uart_init(struct uart_bas *bas, int baudrate __unused,
    int databits __unused, int stopbits __unused, int parity __unused)
{
	bus_space_tag_t bst;
	bus_space_handle_t bsh;

	bst = bas->bst;
	bsh = bas->bsh;

	/* Enable output to and space in from the SC interrupts. */
	SBBC_SRAM_WRITE_4(sbbc_solscie, SBBC_SRAM_READ_4(sbbc_solscie) |
	    SBBC_SRAM_CONS_OUT | SBBC_SRAM_CONS_SPACE_IN);
	uart_barrier(bas);

	/* Take over the console input. */
	sbbc_serengeti_set_console_input(SUNW_SETCONSINPUT_CLNT);
}

static void
sbbc_uart_term(struct uart_bas *bas __unused)
{

	/* Give back the console input. */
	sbbc_serengeti_set_console_input(SUNW_SETCONSINPUT_OBP);
}

static void
sbbc_uart_putc(struct uart_bas *bas, int c)
{
	bus_space_tag_t bst;
	bus_space_handle_t bsh;
	uint32_t wrptr;

	bst = bas->bst;
	bsh = bas->bsh;

	wrptr = SBBC_SRAM_READ_4(sbbc_solcons +
	    SBBC_CONS_OFF(cons_out_wrptr));
	SBBC_SRAM_WRITE_1(sbbc_solcons + wrptr, c);
	uart_barrier(bas);
	if (++wrptr == SBBC_SRAM_READ_4(sbbc_solcons +
	    SBBC_CONS_OFF(cons_out_end)))
		wrptr = SBBC_SRAM_READ_4(sbbc_solcons +
		    SBBC_CONS_OFF(cons_out_begin));
	SBBC_SRAM_WRITE_4(sbbc_solcons + SBBC_CONS_OFF(cons_out_wrptr),
	    wrptr);
	uart_barrier(bas);

	SBBC_SRAM_WRITE_4(sbbc_solscir, SBBC_SRAM_READ_4(sbbc_solscir) |
	    SBBC_SRAM_CONS_OUT);
	uart_barrier(bas);
	sbbc_send_intr(bst, bsh);
}

static int
sbbc_uart_rxready(struct uart_bas *bas)
{
	bus_space_tag_t bst;
	bus_space_handle_t bsh;

	bst = bas->bst;
	bsh = bas->bsh;

	if (SBBC_SRAM_READ_4(sbbc_solcons + SBBC_CONS_OFF(cons_in_rdptr)) ==
	    SBBC_SRAM_READ_4(sbbc_solcons + SBBC_CONS_OFF(cons_in_wrptr)))
		return (0);
	return (1);
}

static int
sbbc_uart_getc(struct uart_bas *bas, struct mtx *hwmtx)
{
	bus_space_tag_t bst;
	bus_space_handle_t bsh;
	int c;
	uint32_t rdptr;

	bst = bas->bst;
	bsh = bas->bsh;

	uart_lock(hwmtx);

	while (sbbc_uart_rxready(bas) == 0) {
		uart_unlock(hwmtx);
		DELAY(4);
		uart_lock(hwmtx);
	}

	rdptr = SBBC_SRAM_READ_4(sbbc_solcons + SBBC_CONS_OFF(cons_in_rdptr));
	c = SBBC_SRAM_READ_1(sbbc_solcons + rdptr);
	uart_barrier(bas);
	if (++rdptr == SBBC_SRAM_READ_4(sbbc_solcons +
	    SBBC_CONS_OFF(cons_in_end)))
		rdptr = SBBC_SRAM_READ_4(sbbc_solcons +
		    SBBC_CONS_OFF(cons_in_begin));
	SBBC_SRAM_WRITE_4(sbbc_solcons + SBBC_CONS_OFF(cons_in_rdptr),
	    rdptr);
	uart_barrier(bas);
	SBBC_SRAM_WRITE_4(sbbc_solscir, SBBC_SRAM_READ_4(sbbc_solscir) |
	    SBBC_SRAM_CONS_SPACE_IN);
	uart_barrier(bas);
	sbbc_send_intr(bst, bsh);

	uart_unlock(hwmtx);
	return (c);
}

/*
 * High-level UART interface
 */
static int sbbc_uart_bus_attach(struct uart_softc *sc);
static int sbbc_uart_bus_detach(struct uart_softc *sc);
static int sbbc_uart_bus_flush(struct uart_softc *sc, int what);
static int sbbc_uart_bus_getsig(struct uart_softc *sc);
static int sbbc_uart_bus_ioctl(struct uart_softc *sc, int request,
    intptr_t data);
static int sbbc_uart_bus_ipend(struct uart_softc *sc);
static int sbbc_uart_bus_param(struct uart_softc *sc, int baudrate,
    int databits, int stopbits, int parity);
static int sbbc_uart_bus_probe(struct uart_softc *sc);
static int sbbc_uart_bus_receive(struct uart_softc *sc);
static int sbbc_uart_bus_setsig(struct uart_softc *sc, int sig);
static int sbbc_uart_bus_transmit(struct uart_softc *sc);

static kobj_method_t sbbc_uart_methods[] = {
	KOBJMETHOD(uart_attach,		sbbc_uart_bus_attach),
	KOBJMETHOD(uart_detach,		sbbc_uart_bus_detach),
	KOBJMETHOD(uart_flush,		sbbc_uart_bus_flush),
	KOBJMETHOD(uart_getsig,		sbbc_uart_bus_getsig),
	KOBJMETHOD(uart_ioctl,		sbbc_uart_bus_ioctl),
	KOBJMETHOD(uart_ipend,		sbbc_uart_bus_ipend),
	KOBJMETHOD(uart_param,		sbbc_uart_bus_param),
	KOBJMETHOD(uart_probe,		sbbc_uart_bus_probe),
	KOBJMETHOD(uart_receive,	sbbc_uart_bus_receive),
	KOBJMETHOD(uart_setsig,		sbbc_uart_bus_setsig),
	KOBJMETHOD(uart_transmit,	sbbc_uart_bus_transmit),

	DEVMETHOD_END
};

struct uart_class uart_sbbc_class = {
	"sbbc",
	sbbc_uart_methods,
	sizeof(struct uart_softc),
	.uc_ops = &sbbc_uart_ops,
	.uc_range = 1,
	.uc_rclk = 0x5bbc,	/* arbitrary */
	.uc_rshift = 0
};

#define	SIGCHG(c, i, s, d)						\
	if ((c) != 0) {							\
		i |= (((i) & (s)) != 0) ? (s) : (s) | (d);		\
	} else {							\
		i = (((i) & (s)) != 0) ? ((i) & ~(s)) | (d) : (i);	\
	}

static int
sbbc_uart_bus_attach(struct uart_softc *sc)
{
	struct uart_bas *bas;
	bus_space_tag_t bst;
	bus_space_handle_t bsh;
	uint32_t wrptr;

	bas = &sc->sc_bas;
	bst = bas->bst;
	bsh = bas->bsh;

	uart_lock(sc->sc_hwmtx);

	/*
	 * Let the current output drain before enabling interrupts.  Not
	 * doing so tends to cause lost output when turning them on.
	 */
	wrptr = SBBC_SRAM_READ_4(sbbc_solcons +
	    SBBC_CONS_OFF(cons_out_wrptr));
	while (SBBC_SRAM_READ_4(sbbc_solcons +
	    SBBC_CONS_OFF(cons_out_rdptr)) != wrptr);
		cpu_spinwait();

	/* Clear and acknowledge possibly outstanding interrupts. */
	SBBC_SRAM_WRITE_4(sbbc_scsolir, 0);
	uart_barrier(bas);
	SBBC_REGS_WRITE_4(SBBC_PCI_INT_STATUS,
	    SBBC_SRAM_READ_4(sbbc_scsolir));
	uart_barrier(bas);
	/* Enable PCI interrupts. */
	SBBC_REGS_WRITE_4(SBBC_PCI_INT_ENABLE, SBBC_PCI_ENABLE_INT_A);
	uart_barrier(bas);
	/* Enable input from and output to SC as well as break interrupts. */
	SBBC_SRAM_WRITE_4(sbbc_scsolie, SBBC_SRAM_READ_4(sbbc_scsolie) |
	    SBBC_SRAM_CONS_IN | SBBC_SRAM_CONS_BRK |
	    SBBC_SRAM_CONS_SPACE_OUT);
	uart_barrier(bas);

	uart_unlock(sc->sc_hwmtx);
	return (0);
}

static int
sbbc_uart_bus_detach(struct uart_softc *sc)
{

	/* Give back the console input. */
	sbbc_serengeti_set_console_input(SUNW_SETCONSINPUT_OBP);
	return (0);
}

static int
sbbc_uart_bus_flush(struct uart_softc *sc, int what)
{
	struct uart_bas *bas;
	bus_space_tag_t bst;
	bus_space_handle_t bsh;

	bas = &sc->sc_bas;
	bst = bas->bst;
	bsh = bas->bsh;

	if ((what & UART_FLUSH_TRANSMITTER) != 0)
		return (ENODEV);
	if ((what & UART_FLUSH_RECEIVER) != 0) {
		SBBC_SRAM_WRITE_4(sbbc_solcons +
		    SBBC_CONS_OFF(cons_in_rdptr),
		    SBBC_SRAM_READ_4(sbbc_solcons +
		    SBBC_CONS_OFF(cons_in_wrptr)));
		uart_barrier(bas);
	}
	return (0);
}

static int
sbbc_uart_bus_getsig(struct uart_softc *sc)
{
	uint32_t dummy, new, old, sig;

	do {
		old = sc->sc_hwsig;
		sig = old;
		dummy = 0;
		SIGCHG(dummy, sig, SER_CTS, SER_DCTS);
		SIGCHG(dummy, sig, SER_DCD, SER_DDCD);
		SIGCHG(dummy, sig, SER_DSR, SER_DDSR);
		new = sig & ~SER_MASK_DELTA;
	} while (!atomic_cmpset_32(&sc->sc_hwsig, old, new));
	return (sig);
}

static int
sbbc_uart_bus_ioctl(struct uart_softc *sc, int request, intptr_t data)
{
	int error;

	error = 0;
	uart_lock(sc->sc_hwmtx);
	switch (request) {
	case UART_IOCTL_BAUD:
		*(int*)data = 9600;	/* arbitrary */
		break;
	default:
		error = EINVAL;
		break;
	}
	uart_unlock(sc->sc_hwmtx);
	return (error);
}

static int
sbbc_uart_bus_ipend(struct uart_softc *sc)
{
	struct uart_bas *bas;
	bus_space_tag_t bst;
	bus_space_handle_t bsh;
	int ipend;
	uint32_t reason, status;

	bas = &sc->sc_bas;
	bst = bas->bst;
	bsh = bas->bsh;

	uart_lock(sc->sc_hwmtx);
	status = SBBC_REGS_READ_4(SBBC_PCI_INT_STATUS);
	if (status == 0) {
		uart_unlock(sc->sc_hwmtx);
		return (0);
	}

	/*
	 * Unfortunately, we can't use compare and swap for non-cachable
	 * memory.
	 */
	reason = SBBC_SRAM_READ_4(sbbc_scsolir);
	SBBC_SRAM_WRITE_4(sbbc_scsolir, 0);
	uart_barrier(bas);
	/* Acknowledge the interrupt. */
	SBBC_REGS_WRITE_4(SBBC_PCI_INT_STATUS, status);
	uart_barrier(bas);

	uart_unlock(sc->sc_hwmtx);

	ipend = 0;
	if ((reason & SBBC_SRAM_CONS_IN) != 0)
		ipend |= SER_INT_RXREADY;
	if ((reason & SBBC_SRAM_CONS_BRK) != 0)
		ipend |= SER_INT_BREAK;
	if ((reason & SBBC_SRAM_CONS_SPACE_OUT) != 0 &&
	    SBBC_SRAM_READ_4(sbbc_solcons + SBBC_CONS_OFF(cons_out_rdptr)) ==
	    SBBC_SRAM_READ_4(sbbc_solcons + SBBC_CONS_OFF(cons_out_wrptr)))
		ipend |= SER_INT_TXIDLE;
	return (ipend);
}

static int
sbbc_uart_bus_param(struct uart_softc *sc __unused, int baudrate __unused,
    int databits __unused, int stopbits __unused, int parity __unused)
{

	return (0);
}

static int
sbbc_uart_bus_probe(struct uart_softc *sc)
{
	struct uart_bas *bas;
	bus_space_tag_t bst;
	bus_space_handle_t bsh;

	if (sbbc_console != 0) {
		bas = &sc->sc_bas;
		bst = bas->bst;
		bsh = bas->bsh;
		sc->sc_rxfifosz = SBBC_SRAM_READ_4(sbbc_solcons +
		    SBBC_CONS_OFF(cons_in_end)) - SBBC_SRAM_READ_4(sbbc_solcons +
		    SBBC_CONS_OFF(cons_in_begin)) - 1;
		sc->sc_txfifosz = SBBC_SRAM_READ_4(sbbc_solcons +
		    SBBC_CONS_OFF(cons_out_end)) - SBBC_SRAM_READ_4(sbbc_solcons +
		    SBBC_CONS_OFF(cons_out_begin)) - 1;
		return (0);
	}
	return (ENXIO);
}

static int
sbbc_uart_bus_receive(struct uart_softc *sc)
{
	struct uart_bas *bas;
	bus_space_tag_t bst;
	bus_space_handle_t bsh;
	int c;
	uint32_t end, rdptr, wrptr;

	bas = &sc->sc_bas;
	bst = bas->bst;
	bsh = bas->bsh;

	uart_lock(sc->sc_hwmtx);

	end = SBBC_SRAM_READ_4(sbbc_solcons + SBBC_CONS_OFF(cons_in_end));
	rdptr = SBBC_SRAM_READ_4(sbbc_solcons + SBBC_CONS_OFF(cons_in_rdptr));
	wrptr = SBBC_SRAM_READ_4(sbbc_solcons + SBBC_CONS_OFF(cons_in_wrptr));
	while (rdptr != wrptr) {
		if (uart_rx_full(sc) != 0) {
			sc->sc_rxbuf[sc->sc_rxput] = UART_STAT_OVERRUN;
			break;
		}
		c = SBBC_SRAM_READ_1(sbbc_solcons + rdptr);
		uart_rx_put(sc, c);
		if (++rdptr == end)
			rdptr = SBBC_SRAM_READ_4(sbbc_solcons +
			    SBBC_CONS_OFF(cons_in_begin));
	}
	uart_barrier(bas);
	SBBC_SRAM_WRITE_4(sbbc_solcons + SBBC_CONS_OFF(cons_in_rdptr),
	    rdptr);
	uart_barrier(bas);
	SBBC_SRAM_WRITE_4(sbbc_solscir, SBBC_SRAM_READ_4(sbbc_solscir) |
	    SBBC_SRAM_CONS_SPACE_IN);
	uart_barrier(bas);
	sbbc_send_intr(bst, bsh);

	uart_unlock(sc->sc_hwmtx);
	return (0);
}

static int
sbbc_uart_bus_setsig(struct uart_softc *sc, int sig)
{
	struct uart_bas *bas;
	uint32_t new, old;

	bas = &sc->sc_bas;
	do {
		old = sc->sc_hwsig;
		new = old;
		if ((sig & SER_DDTR) != 0) {
			SIGCHG(sig & SER_DTR, new, SER_DTR, SER_DDTR);
		}
		if ((sig & SER_DRTS) != 0) {
			SIGCHG(sig & SER_RTS, new, SER_RTS, SER_DRTS);
		}
	} while (!atomic_cmpset_32(&sc->sc_hwsig, old, new));
	return (0);
}

static int
sbbc_uart_bus_transmit(struct uart_softc *sc)
{
	struct uart_bas *bas;
	bus_space_tag_t bst;
	bus_space_handle_t bsh;
	int i;
	uint32_t end, wrptr;

	bas = &sc->sc_bas;
	bst = bas->bst;
	bsh = bas->bsh;

	uart_lock(sc->sc_hwmtx);

	end = SBBC_SRAM_READ_4(sbbc_solcons + SBBC_CONS_OFF(cons_out_end));
	wrptr = SBBC_SRAM_READ_4(sbbc_solcons +
	    SBBC_CONS_OFF(cons_out_wrptr));
	for (i = 0; i < sc->sc_txdatasz; i++) {
		SBBC_SRAM_WRITE_1(sbbc_solcons + wrptr, sc->sc_txbuf[i]);
		if (++wrptr == end)
			wrptr = SBBC_SRAM_READ_4(sbbc_solcons +
			    SBBC_CONS_OFF(cons_out_begin));
	}
	uart_barrier(bas);
	SBBC_SRAM_WRITE_4(sbbc_solcons + SBBC_CONS_OFF(cons_out_wrptr),
	    wrptr);
	uart_barrier(bas);
	SBBC_SRAM_WRITE_4(sbbc_solscir, SBBC_SRAM_READ_4(sbbc_solscir) |
	    SBBC_SRAM_CONS_OUT);
	uart_barrier(bas);
	sbbc_send_intr(bst, bsh);
	sc->sc_txbusy = 1;

	uart_unlock(sc->sc_hwmtx);
	return (0);
}
