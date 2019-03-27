/*-
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD
 *
 * Copyright (c) 1999 Seigo Tanimura
 * Copyright (c) 1999 Ville-Pertti Keinonen
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

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/kernel.h>
#include <sys/bus.h>
#include <sys/malloc.h>
#include <sys/module.h>
#include <machine/resource.h>
#include <machine/bus.h>
#include <sys/rman.h>

#ifdef HAVE_KERNEL_OPTION_HEADERS
#include "opt_snd.h"
#endif

#include <dev/sound/pcm/sound.h>
#include <dev/sound/chip.h>
#include "bus_if.h"

#include <isa/isavar.h>
#include <isa/isa_common.h>

SND_DECLARE_FILE("$FreeBSD$");

#define LOGICALID_NOPNP 0
#define LOGICALID_PCM   0x0000561e
#define LOGICALID_OPL   0x0300561e
#define LOGICALID_MIDI  0x0400561e

/* PnP IDs */
static struct isa_pnp_id gusc_ids[] = {
	{LOGICALID_PCM,  "GRV0000 Gravis UltraSound PnP PCM"},	/* GRV0000 */
	{LOGICALID_OPL,  "GRV0003 Gravis UltraSound PnP OPL"},	/* GRV0003 */
	{LOGICALID_MIDI, "GRV0004 Gravis UltraSound PnP MIDI"},	/* GRV0004 */
};

/* Interrupt handler.  */
struct gusc_ihandler {
	void (*intr)(void *);
	void *arg;
};

/* Here is the parameter structure per a device. */
struct gusc_softc {
	device_t dev; /* device */
	int io_rid[3]; /* io port rids */
	struct resource *io[3]; /* io port resources */
	int io_alloced[3]; /* io port alloc flag */
	int irq_rid; /* irq rids */
	struct resource *irq; /* irq resources */
	int irq_alloced; /* irq alloc flag */
	int drq_rid[2]; /* drq rids */
	struct resource *drq[2]; /* drq resources */
	int drq_alloced[2]; /* drq alloc flag */

	/* Interrupts are shared (XXX non-PnP only?) */
	struct gusc_ihandler midi_intr;
	struct gusc_ihandler pcm_intr;
};

typedef struct gusc_softc *sc_p;

static int gusc_probe(device_t dev);
static int gusc_attach(device_t dev);
static int gusisa_probe(device_t dev);
static void gusc_intr(void *);
static struct resource *gusc_alloc_resource(device_t bus, device_t child, int type, int *rid,
					    rman_res_t start, rman_res_t end, rman_res_t count, u_int flags);
static int gusc_release_resource(device_t bus, device_t child, int type, int rid,
				   struct resource *r);

static device_t find_masterdev(sc_p scp);
static int alloc_resource(sc_p scp);
static int release_resource(sc_p scp);

static devclass_t gusc_devclass;

static int
gusc_probe(device_t dev)
{
	device_t child;
	u_int32_t logical_id;
	char *s;
	struct sndcard_func *func;
	int ret;

	logical_id = isa_get_logicalid(dev);
	s = NULL;

	/* Check isapnp ids */
	if (logical_id != 0 && (ret = ISA_PNP_PROBE(device_get_parent(dev), dev, gusc_ids)) != 0)
		return (ret);
	else {
		if (logical_id == 0)
			return gusisa_probe(dev);
	}

	switch (logical_id) {
	case LOGICALID_PCM:
		s = "Gravis UltraSound Plug & Play PCM";
		func = malloc(sizeof(struct sndcard_func), M_DEVBUF, M_NOWAIT | M_ZERO);
		if (func == NULL)
			return (ENOMEM);
		func->func = SCF_PCM;
		child = device_add_child(dev, "pcm", -1);
		device_set_ivars(child, func);
		break;
	case LOGICALID_OPL:
		s = "Gravis UltraSound Plug & Play OPL";
		func = malloc(sizeof(struct sndcard_func), M_DEVBUF, M_NOWAIT | M_ZERO);
		if (func == NULL)
			return (ENOMEM);
		func->func = SCF_SYNTH;
		child = device_add_child(dev, "midi", -1);
		device_set_ivars(child, func);
		break;
	case LOGICALID_MIDI:
		s = "Gravis UltraSound Plug & Play MIDI";
		func = malloc(sizeof(struct sndcard_func), M_DEVBUF, M_NOWAIT | M_ZERO);
		if (func == NULL)
			return (ENOMEM);
		func->func = SCF_MIDI;
		child = device_add_child(dev, "midi", -1);
		device_set_ivars(child, func);
		break;
	}

	if (s != NULL) {
		device_set_desc(dev, s);
		return (0);
	}

	return (ENXIO);
}

static void
port_wr(struct resource *r, int i, unsigned char v)
{
	bus_space_write_1(rman_get_bustag(r), rman_get_bushandle(r), i, v);
}

static int
port_rd(struct resource *r, int i)
{
	return bus_space_read_1(rman_get_bustag(r), rman_get_bushandle(r), i);
}

/*
 * Probe for an old (non-PnP) GUS card on the ISA bus.
 */

static int
gusisa_probe(device_t dev)
{
	device_t child;
	struct resource *res, *res2;
	int base, rid, rid2, s, flags;
	unsigned char val;

	base = isa_get_port(dev);
	flags = device_get_flags(dev);
	rid = 1;
	res = bus_alloc_resource(dev, SYS_RES_IOPORT, &rid, base + 0x100,
				 base + 0x107, 8, RF_ACTIVE);

	if (res == NULL)
		return ENXIO;

	res2 = NULL;

	/*
	 * Check for the presence of some GUS card.  Reset the card,
	 * then see if we can access the memory on it.
	 */

	port_wr(res, 3, 0x4c);
	port_wr(res, 5, 0);
	DELAY(30 * 1000);

	port_wr(res, 3, 0x4c);
	port_wr(res, 5, 1);
	DELAY(30 * 1000);

	s = splhigh();

	/* Write to DRAM.  */

	port_wr(res, 3, 0x43);		/* Register select */
	port_wr(res, 4, 0);		/* Low addr */
	port_wr(res, 5, 0);		/* Med addr */

	port_wr(res, 3, 0x44);		/* Register select */
	port_wr(res, 4, 0);		/* High addr */
	port_wr(res, 7, 0x55);		/* DRAM */

	/* Read from DRAM.  */

	port_wr(res, 3, 0x43);		/* Register select */
	port_wr(res, 4, 0);		/* Low addr */
	port_wr(res, 5, 0);		/* Med addr */

	port_wr(res, 3, 0x44);		/* Register select */
	port_wr(res, 4, 0);		/* High addr */
	val = port_rd(res, 7);		/* DRAM */

	splx(s);

	if (val != 0x55)
		goto fail;

	rid2 = 0;
	res2 = bus_alloc_resource(dev, SYS_RES_IOPORT, &rid2, base, base, 1,
				  RF_ACTIVE);

	if (res2 == NULL)
		goto fail;

	s = splhigh();
	port_wr(res2, 0x0f, 0x20);
	val = port_rd(res2, 0x0f);
	splx(s);

	if (val == 0xff || (val & 0x06) == 0)
		val = 0;
	else {
		val = port_rd(res2, 0x506);	/* XXX Out of range.  */
		if (val == 0xff)
			val = 0;
	}

	bus_release_resource(dev, SYS_RES_IOPORT, rid2, res2);
	bus_release_resource(dev, SYS_RES_IOPORT, rid, res);

	if (val >= 10) {
		struct sndcard_func *func;

		/* Looks like a GUS MAX.  Set the rest of the resources.  */

		bus_set_resource(dev, SYS_RES_IOPORT, 2, base + 0x10c, 8);

		if (flags & DV_F_DUAL_DMA)
			bus_set_resource(dev, SYS_RES_DRQ, 1,
					 flags & DV_F_DRQ_MASK, 1);

		/* We can support the CS4231 and MIDI devices.  */

		func = malloc(sizeof(struct sndcard_func), M_DEVBUF, M_NOWAIT | M_ZERO);
		if (func == NULL)
			return ENOMEM;
		func->func = SCF_MIDI;
		child = device_add_child(dev, "midi", -1);
		device_set_ivars(child, func);

		func = malloc(sizeof(struct sndcard_func), M_DEVBUF, M_NOWAIT | M_ZERO);
		if (func == NULL)
			printf("xxx: gus pcm not attached, out of memory\n");
		else {
			func->func = SCF_PCM;
			child = device_add_child(dev, "pcm", -1);
			device_set_ivars(child, func);
		}
		device_set_desc(dev, "Gravis UltraSound MAX");
		return 0;
	} else {

		/*
		 * TODO: Support even older GUS cards.  MIDI should work on
		 * all models.
		 */
		return ENXIO;
	}

fail:
	bus_release_resource(dev, SYS_RES_IOPORT, rid, res);
	return ENXIO;
}

static int
gusc_attach(device_t dev)
{
	sc_p scp;
	void *ih;

	scp = device_get_softc(dev);

	bzero(scp, sizeof(*scp));

	scp->dev = dev;
	if (alloc_resource(scp)) {
		release_resource(scp);
		return (ENXIO);
	}

	if (scp->irq != NULL)
		snd_setup_intr(dev, scp->irq, 0, gusc_intr, scp, &ih);
	bus_generic_attach(dev);

	return (0);
}

/*
 * Handle interrupts on GUS devices until there aren't any left.
 */
static void
gusc_intr(void *arg)
{
	sc_p scp = (sc_p)arg;
	int did_something;

	do {
		did_something = 0;
		if (scp->pcm_intr.intr != NULL &&
		    (port_rd(scp->io[2], 2) & 1)) {
			(*scp->pcm_intr.intr)(scp->pcm_intr.arg);
			did_something = 1;
		}
		if (scp->midi_intr.intr != NULL &&
		    (port_rd(scp->io[1], 0) & 0x80)) {
			(*scp->midi_intr.intr)(scp->midi_intr.arg);
			did_something = 1;
		}
	} while (did_something != 0);
}

static struct resource *
gusc_alloc_resource(device_t bus, device_t child, int type, int *rid,
		    rman_res_t start, rman_res_t end, rman_res_t count, u_int flags)
{
	sc_p scp;
	int *alloced, rid_max, alloced_max;
	struct resource **res;

	scp = device_get_softc(bus);
	switch (type) {
	case SYS_RES_IOPORT:
		alloced = scp->io_alloced;
		res = scp->io;
		rid_max = 2;
		alloced_max = 2; /* pcm + midi (more to include synth) */
		break;
	case SYS_RES_IRQ:
		alloced = &scp->irq_alloced;
		res = &scp->irq;
		rid_max = 0;
		alloced_max = 2; /* pcm and midi share the single irq. */
		break;
	case SYS_RES_DRQ:
		alloced = scp->drq_alloced;
		res = scp->drq;
		rid_max = 1;
		alloced_max = 1;
		break;
	default:
		return (NULL);
	}

	if (*rid > rid_max || alloced[*rid] == alloced_max)
		return (NULL);

	alloced[*rid]++;
	return (res[*rid]);
}

static int
gusc_release_resource(device_t bus, device_t child, int type, int rid,
			struct resource *r)
{
	sc_p scp;
	int *alloced, rid_max;

	scp = device_get_softc(bus);
	switch (type) {
	case SYS_RES_IOPORT:
		alloced = scp->io_alloced;
		rid_max = 2;
		break;
	case SYS_RES_IRQ:
		alloced = &scp->irq_alloced;
		rid_max = 0;
		break;
	case SYS_RES_DRQ:
		alloced = scp->drq_alloced;
		rid_max = 1;
		break;
	default:
		return (1);
	}

	if (rid > rid_max || alloced[rid] == 0)
		return (1);

	alloced[rid]--;
	return (0);
}

static int
gusc_setup_intr(device_t dev, device_t child, struct resource *irq, int flags,
		driver_filter_t *filter,
		driver_intr_t *intr, void *arg, void **cookiep)
{
	sc_p scp = (sc_p)device_get_softc(dev);
	devclass_t devclass;

	if (filter != NULL) {
		printf("gusc.c: we cannot use a filter here\n");
		return (EINVAL);
	}
	devclass = device_get_devclass(child);
	if (strcmp(devclass_get_name(devclass), "midi") == 0) {
		scp->midi_intr.intr = intr;
		scp->midi_intr.arg = arg;
		return 0;
	} else if (strcmp(devclass_get_name(devclass), "pcm") == 0) {
		scp->pcm_intr.intr = intr;
		scp->pcm_intr.arg = arg;
		return 0;
	}
	return bus_generic_setup_intr(dev, child, irq, flags,
				filter,
				intr, arg, cookiep);
}

static device_t
find_masterdev(sc_p scp)
{
	int i, units;
	devclass_t devclass;
	device_t dev;

	devclass = device_get_devclass(scp->dev);
	units = devclass_get_maxunit(devclass);
	dev = NULL;
	for (i = 0 ; i < units ; i++) {
		dev = devclass_get_device(devclass, i);
		if (isa_get_vendorid(dev) == isa_get_vendorid(scp->dev)
		    && isa_get_logicalid(dev) == LOGICALID_PCM
		    && isa_get_serial(dev) == isa_get_serial(scp->dev))
			break;
	}
	if (i == units)
		return (NULL);

	return (dev);
}

static int io_range[3]  = {0x10, 0x8  , 0x4  };
static int io_offset[3] = {0x0 , 0x100, 0x10c};
static int
alloc_resource(sc_p scp)
{
	int i, base, lid, flags;
	device_t dev;

	flags = 0;
	if (isa_get_vendorid(scp->dev))
		lid = isa_get_logicalid(scp->dev);
	else {
		lid = LOGICALID_NOPNP;
		flags = device_get_flags(scp->dev);
	}
	switch(lid) {
	case LOGICALID_PCM:
	case LOGICALID_NOPNP:		/* XXX Non-PnP */
		if (lid == LOGICALID_NOPNP)
			base = isa_get_port(scp->dev);
		else
			base = 0;
		for (i = 0 ; i < nitems(scp->io); i++) {
			if (scp->io[i] == NULL) {
				scp->io_rid[i] = i;
				if (base == 0)
					scp->io[i] =
					    bus_alloc_resource_anywhere(scp->dev,
					    	    			SYS_RES_IOPORT,
					    	    			&scp->io_rid[i],
									io_range[i],
									RF_ACTIVE);
				else
					scp->io[i] = bus_alloc_resource(scp->dev, SYS_RES_IOPORT, &scp->io_rid[i],
									base + io_offset[i],
									base + io_offset[i] + io_range[i] - 1
									, io_range[i], RF_ACTIVE);
				if (scp->io[i] == NULL)
					return (1);
				scp->io_alloced[i] = 0;
			}
		}
		if (scp->irq == NULL) {
			scp->irq_rid = 0;
			scp->irq = 
				bus_alloc_resource_any(scp->dev, SYS_RES_IRQ, 
						       &scp->irq_rid,
						       RF_ACTIVE|RF_SHAREABLE);
			if (scp->irq == NULL)
				return (1);
			scp->irq_alloced = 0;
		}
		for (i = 0 ; i < nitems(scp->drq); i++) {
			if (scp->drq[i] == NULL) {
				scp->drq_rid[i] = i;
				if (base == 0 || i == 0)
					scp->drq[i] = 
						bus_alloc_resource_any(
							scp->dev, SYS_RES_DRQ,
							&scp->drq_rid[i],
							RF_ACTIVE);
				else if ((flags & DV_F_DUAL_DMA) != 0)
					/* XXX The secondary drq is specified in the flag. */
					scp->drq[i] = bus_alloc_resource(scp->dev, SYS_RES_DRQ, &scp->drq_rid[i],
									 flags & DV_F_DRQ_MASK,
									 flags & DV_F_DRQ_MASK, 1, RF_ACTIVE);
				if (scp->drq[i] == NULL)
					return (1);
				scp->drq_alloced[i] = 0;
			}
		}
		break;
	case LOGICALID_OPL:
		if (scp->io[0] == NULL) {
			scp->io_rid[0] = 0;
			scp->io[0] = bus_alloc_resource_anywhere(scp->dev,
								 SYS_RES_IOPORT,
								 &scp->io_rid[0],
								 io_range[0],
								 RF_ACTIVE);
			if (scp->io[0] == NULL)
				return (1);
			scp->io_alloced[0] = 0;
		}
		break;
	case LOGICALID_MIDI:
		if (scp->io[0] == NULL) {
			scp->io_rid[0] = 0;
			scp->io[0] = bus_alloc_resource_anywhere(scp->dev,
								 SYS_RES_IOPORT,
								 &scp->io_rid[0],
								 io_range[0],
								 RF_ACTIVE);
			if (scp->io[0] == NULL)
				return (1);
			scp->io_alloced[0] = 0;
		}
		if (scp->irq == NULL) {
			/* The irq is shared with pcm audio. */
			dev = find_masterdev(scp);
			if (dev == NULL)
				return (1);
			scp->irq_rid = 0;
			scp->irq = BUS_ALLOC_RESOURCE(dev, NULL, SYS_RES_IRQ, &scp->irq_rid,
						      0, ~0, 1, RF_ACTIVE | RF_SHAREABLE);
			if (scp->irq == NULL)
				return (1);
			scp->irq_alloced = 0;
		}
		break;
	}
	return (0);
}

static int
release_resource(sc_p scp)
{
	int i, lid;
	device_t dev;

	if (isa_get_vendorid(scp->dev))
		lid = isa_get_logicalid(scp->dev);
	else
		lid = LOGICALID_NOPNP;

	switch(lid) {
	case LOGICALID_PCM:
	case LOGICALID_NOPNP:		/* XXX Non-PnP */
		for (i = 0 ; i < nitems(scp->io); i++) {
			if (scp->io[i] != NULL) {
				bus_release_resource(scp->dev, SYS_RES_IOPORT, scp->io_rid[i], scp->io[i]);
				scp->io[i] = NULL;
			}
		}
		if (scp->irq != NULL) {
			bus_release_resource(scp->dev, SYS_RES_IRQ, scp->irq_rid, scp->irq);
			scp->irq = NULL;
		}
		for (i = 0 ; i < nitems(scp->drq); i++) {
			if (scp->drq[i] != NULL) {
				bus_release_resource(scp->dev, SYS_RES_DRQ, scp->drq_rid[i], scp->drq[i]);
				scp->drq[i] = NULL;
			}
		}
		break;
	case LOGICALID_OPL:
		if (scp->io[0] != NULL) {
			bus_release_resource(scp->dev, SYS_RES_IOPORT, scp->io_rid[0], scp->io[0]);
			scp->io[0] = NULL;
		}
		break;
	case LOGICALID_MIDI:
		if (scp->io[0] != NULL) {
			bus_release_resource(scp->dev, SYS_RES_IOPORT, scp->io_rid[0], scp->io[0]);
			scp->io[0] = NULL;
		}
		if (scp->irq != NULL) {
			/* The irq is shared with pcm audio. */
			dev = find_masterdev(scp);
			if (dev == NULL)
				return (1);
			BUS_RELEASE_RESOURCE(dev, NULL, SYS_RES_IOPORT, scp->irq_rid, scp->irq);
			scp->irq = NULL;
		}
		break;
	}
	return (0);
}

static device_method_t gusc_methods[] = {
	/* Device interface */
	DEVMETHOD(device_probe,		gusc_probe),
	DEVMETHOD(device_attach,	gusc_attach),
	DEVMETHOD(device_detach,	bus_generic_detach),
	DEVMETHOD(device_shutdown,	bus_generic_shutdown),
	DEVMETHOD(device_suspend,	bus_generic_suspend),
	DEVMETHOD(device_resume,	bus_generic_resume),

	/* Bus interface */
	DEVMETHOD(bus_alloc_resource,	gusc_alloc_resource),
	DEVMETHOD(bus_release_resource,	gusc_release_resource),
	DEVMETHOD(bus_activate_resource, bus_generic_activate_resource),
	DEVMETHOD(bus_deactivate_resource, bus_generic_deactivate_resource),
	DEVMETHOD(bus_setup_intr,	gusc_setup_intr),
	DEVMETHOD(bus_teardown_intr,	bus_generic_teardown_intr),

	DEVMETHOD_END
};

static driver_t gusc_driver = {
	"gusc",
	gusc_methods,
	sizeof(struct gusc_softc),
};

/*
 * gusc can be attached to an isa bus.
 */
DRIVER_MODULE(snd_gusc, isa, gusc_driver, gusc_devclass, 0, 0);
DRIVER_MODULE(snd_gusc, acpi, gusc_driver, gusc_devclass, 0, 0);
MODULE_DEPEND(snd_gusc, sound, SOUND_MINVER, SOUND_PREFVER, SOUND_MAXVER);
MODULE_VERSION(snd_gusc, 1);
ISA_PNP_INFO(gusc_ids);
