/*-
 * SPDX-License-Identifier: BSD-4-Clause
 *
 * Copyright (C) 2003
 * 	Hidetoshi Shimokawa. All rights reserved.
 * 
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 * 3. All advertising materials mentioning features or use of this software
 *    must display the following acknowledgement:
 *
 *	This product includes software developed by Hidetoshi Shimokawa.
 *
 * 4. Neither the name of the author nor the names of its contributors
 *    may be used to endorse or promote products derived from this software
 *    without specific prior written permission.
 * 
 * THIS SOFTWARE IS PROVIDED BY THE REGENTS AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE REGENTS OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 * 
 * $Id: dcons_crom.c,v 1.8 2003/10/23 15:47:21 simokawa Exp $
 * $FreeBSD$
 */

#include <sys/param.h>
#include <sys/kernel.h>
#include <sys/module.h>
#include <sys/systm.h>
#include <sys/types.h>
#include <sys/conf.h>
#include <sys/malloc.h>

#include <sys/bus.h>
#include <machine/bus.h>

#include <dev/firewire/firewire.h>
#include <dev/firewire/firewirereg.h>
#include <dev/firewire/iec13213.h>
#include <dev/dcons/dcons.h>
#include <dev/dcons/dcons_os.h>

#include <sys/cons.h>

#if (defined(__i386__) || defined(__amd64__))
#include <vm/vm.h>
#include <vm/vm_param.h>
#include <vm/pmap.h>
#include <machine/segments.h> /* for idt */
#endif

static bus_addr_t dcons_paddr;

static int force_console = 0;
TUNABLE_INT("hw.firewire.dcons_crom.force_console", &force_console);

#define ADDR_HI(x)	(((x) >> 24) & 0xffffff)
#define ADDR_LO(x)	((x) & 0xffffff)

struct dcons_crom_softc {
        struct firewire_dev_comm fd;
	struct crom_chunk unit;
	struct crom_chunk spec;
	struct crom_chunk ver;
	bus_dma_tag_t dma_tag;
	bus_dmamap_t dma_map;
	bus_addr_t bus_addr;
	eventhandler_tag ehand;
};

static void
dcons_crom_identify(driver_t *driver, device_t parent)
{
	BUS_ADD_CHILD(parent, 0, "dcons_crom", device_get_unit(parent));
}

static int
dcons_crom_probe(device_t dev)
{
	device_t pa;

	pa = device_get_parent(dev);
	if(device_get_unit(dev) != device_get_unit(pa)){
		return(ENXIO);
	}

	device_set_desc(dev, "dcons configuration ROM");
	return (0);
}

#if (defined(__i386__) || defined(__amd64__))
static void
dcons_crom_expose_idt(struct dcons_crom_softc *sc)
{
	static off_t idt_paddr;

	/* XXX */
#ifdef __amd64__
	idt_paddr = (char *)idt - (char *)KERNBASE;
#else /* __i386__ */
	idt_paddr = (off_t)pmap_kextract((vm_offset_t)idt);
#endif

	crom_add_entry(&sc->unit, DCONS_CSR_KEY_RESET_HI, ADDR_HI(idt_paddr));
	crom_add_entry(&sc->unit, DCONS_CSR_KEY_RESET_LO, ADDR_LO(idt_paddr));
}
#endif

static void
dcons_crom_post_busreset(void *arg)
{
	struct dcons_crom_softc *sc;
	struct crom_src *src;
	struct crom_chunk *root;

	sc = (struct dcons_crom_softc *) arg;
	src = sc->fd.fc->crom_src;
	root = sc->fd.fc->crom_root;

	bzero(&sc->unit, sizeof(struct crom_chunk));

	crom_add_chunk(src, root, &sc->unit, CROM_UDIR);
	crom_add_entry(&sc->unit, CSRKEY_SPEC, CSRVAL_VENDOR_PRIVATE);
	crom_add_simple_text(src, &sc->unit, &sc->spec, "FreeBSD");
	crom_add_entry(&sc->unit, CSRKEY_VER, DCONS_CSR_VAL_VER);
	crom_add_simple_text(src, &sc->unit, &sc->ver, "dcons");
	crom_add_entry(&sc->unit, DCONS_CSR_KEY_HI, ADDR_HI(dcons_paddr));
	crom_add_entry(&sc->unit, DCONS_CSR_KEY_LO, ADDR_LO(dcons_paddr));
#if (defined(__i386__) || defined(__amd64__))
	dcons_crom_expose_idt(sc);
#endif
}

static void
dmamap_cb(void *arg, bus_dma_segment_t *segments, int seg, int error)
{
	struct dcons_crom_softc *sc;

	if (error)
		printf("dcons_dmamap_cb: error=%d\n", error);

	sc = (struct dcons_crom_softc *)arg;
	sc->bus_addr = segments[0].ds_addr;

	bus_dmamap_sync(sc->dma_tag, sc->dma_map, BUS_DMASYNC_PREWRITE);
	device_printf(sc->fd.dev,
	    "bus_addr 0x%jx\n", (uintmax_t)sc->bus_addr);
	if (dcons_paddr != 0) {
		/* XXX */
		device_printf(sc->fd.dev, "dcons_paddr is already set\n");
		return;
	}
	dcons_conf->dma_tag = sc->dma_tag;
	dcons_conf->dma_map = sc->dma_map;
	dcons_paddr = sc->bus_addr;

	/* Force to be the high-level console */
	if (force_console)
		cnselect(dcons_conf->cdev);
}

static void
dcons_crom_poll(void *p, int arg)
{
	struct dcons_crom_softc *sc = (struct dcons_crom_softc *) p;

	sc->fd.fc->poll(sc->fd.fc, -1, -1);
}

static int
dcons_crom_attach(device_t dev)
{
	struct dcons_crom_softc *sc;
	int error;

	if (dcons_conf->buf == NULL)
		return (ENXIO);
        sc = (struct dcons_crom_softc *) device_get_softc(dev);
	sc->fd.fc = device_get_ivars(dev);
	sc->fd.dev = dev;
	sc->fd.post_explore = NULL;
	sc->fd.post_busreset = (void *) dcons_crom_post_busreset;

	/* map dcons buffer */
	error = bus_dma_tag_create(
		/*parent*/ sc->fd.fc->dmat,
		/*alignment*/ sizeof(u_int32_t),
		/*boundary*/ 0,
		/*lowaddr*/ BUS_SPACE_MAXADDR,
		/*highaddr*/ BUS_SPACE_MAXADDR,
		/*filter*/NULL, /*filterarg*/NULL,
		/*maxsize*/ dcons_conf->size,
		/*nsegments*/ 1,
		/*maxsegsz*/ BUS_SPACE_MAXSIZE_32BIT,
		/*flags*/ BUS_DMA_ALLOCNOW,
		/*lockfunc*/busdma_lock_mutex,
		/*lockarg*/&Giant,
		&sc->dma_tag);
	if (error != 0)
		return (error);
	error = bus_dmamap_create(sc->dma_tag, BUS_DMA_COHERENT, &sc->dma_map);
	if (error != 0)
		return (error);
	error = bus_dmamap_load(sc->dma_tag, sc->dma_map,
	    (void *)dcons_conf->buf, dcons_conf->size,
	    dmamap_cb, sc, 0);
	if (error != 0)
		return (error);
	sc->ehand = EVENTHANDLER_REGISTER(dcons_poll, dcons_crom_poll,
			 (void *)sc, 0);
	return (0);
}

static int
dcons_crom_detach(device_t dev)
{
	struct dcons_crom_softc *sc;

        sc = (struct dcons_crom_softc *) device_get_softc(dev);
	sc->fd.post_busreset = NULL;

	if (sc->ehand)
		EVENTHANDLER_DEREGISTER(dcons_poll, sc->ehand);

	/* XXX */
	if (dcons_conf->dma_tag == sc->dma_tag)
		dcons_conf->dma_tag = NULL;

	bus_dmamap_unload(sc->dma_tag, sc->dma_map);
	bus_dmamap_destroy(sc->dma_tag, sc->dma_map);
	bus_dma_tag_destroy(sc->dma_tag);

	return 0;
}

static devclass_t dcons_crom_devclass;

static device_method_t dcons_crom_methods[] = {
	/* device interface */
	DEVMETHOD(device_identify,	dcons_crom_identify),
	DEVMETHOD(device_probe,		dcons_crom_probe),
	DEVMETHOD(device_attach,	dcons_crom_attach),
	DEVMETHOD(device_detach,	dcons_crom_detach),
	{ 0, 0 }
};

static driver_t dcons_crom_driver = {
	"dcons_crom",
	dcons_crom_methods,
	sizeof(struct dcons_crom_softc),
};

DRIVER_MODULE(dcons_crom, firewire, dcons_crom_driver,
					dcons_crom_devclass, 0, 0);
MODULE_VERSION(dcons_crom, 1);
MODULE_DEPEND(dcons_crom, dcons,
	DCONS_VERSION, DCONS_VERSION, DCONS_VERSION);
MODULE_DEPEND(dcons_crom, firewire, 1, 1, 1);
