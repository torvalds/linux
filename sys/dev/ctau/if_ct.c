/*-
 * Cronyx-Tau adapter driver for FreeBSD.
 * Supports PPP/HDLC and Cisco/HDLC protocol in synchronous mode,
 * and asynchronous channels with full modem control.
 * Keepalive protocol implemented in both Cisco and PPP modes.
 *
 * Copyright (C) 1994-2002 Cronyx Engineering.
 * Author: Serge Vakulenko, <vak@cronyx.ru>
 *
 * Copyright (C) 1999-2004 Cronyx Engineering.
 * Author: Roman Kurakin, <rik@cronyx.ru>
 *
 * This software is distributed with NO WARRANTIES, not even the implied
 * warranties for MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 *
 * Authors grant any other persons or organisations a permission to use,
 * modify and redistribute this software in source and binary forms,
 * as long as this message is kept with the software, all derivative
 * works or modified versions.
 *
 * Cronyx Id: if_ct.c,v 1.1.2.31 2004/06/23 17:09:13 rik Exp $
 */

#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");

#include <sys/param.h>
#include <sys/proc.h>
#include <sys/systm.h>
#include <sys/kernel.h>
#include <sys/module.h>
#include <sys/mbuf.h>
#include <sys/sockio.h>
#include <sys/malloc.h>
#include <sys/priv.h>
#include <sys/socket.h>
#include <sys/sysctl.h>
#include <sys/conf.h>
#include <sys/errno.h>
#include <sys/tty.h>
#include <sys/bus.h>
#include <machine/bus.h>
#include <sys/rman.h>
#include <isa/isavar.h>
#include <sys/interrupt.h>
#include <vm/vm.h>
#include <vm/pmap.h>
#include <net/if.h>
#include <net/if_var.h>
#include <machine/cpufunc.h>
#include <machine/cserial.h>
#include <machine/resource.h>
#include <dev/cx/machdep.h>
#include <dev/ctau/ctddk.h>
#include <dev/cx/cronyxfw.h>
#include "opt_ng_cronyx.h"
#ifdef NETGRAPH_CRONYX
#   include "opt_netgraph.h"
#   include <netgraph/ng_message.h>
#   include <netgraph/netgraph.h>
#   include <dev/ctau/ng_ct.h>
#else
#   include <net/if_types.h>
#   include <net/if_sppp.h>
#   define PP_CISCO IFF_LINK2
#   include <net/bpf.h>
#endif
 
#define NCTAU 1

/* If we don't have Cronyx's sppp version, we don't have fr support via sppp */
#ifndef PP_FR
#define PP_FR 0
#endif

#define CT_DEBUG(d,s)	({if (d->chan->debug) {\
				printf ("%s: ", d->name); printf s;}})
#define CT_DEBUG2(d,s)	({if (d->chan->debug>1) {\
				printf ("%s: ", d->name); printf s;}})

#define CT_LOCK_NAME	"ctX"

#define CT_LOCK(_bd)		mtx_lock (&(_bd)->ct_mtx)
#define CT_UNLOCK(_bd)		mtx_unlock (&(_bd)->ct_mtx)
#define CT_LOCK_ASSERT(_bd)	mtx_assert (&(_bd)->ct_mtx, MA_OWNED)

static void ct_identify		__P((driver_t *, device_t));
static int ct_probe		__P((device_t));
static int ct_attach		__P((device_t));
static int ct_detach		__P((device_t));

static device_method_t ct_isa_methods [] = {
	DEVMETHOD(device_identify,	ct_identify),
	DEVMETHOD(device_probe,		ct_probe),
	DEVMETHOD(device_attach,	ct_attach),
	DEVMETHOD(device_detach,	ct_detach),

	DEVMETHOD_END
};

typedef struct _ct_dma_mem_t {
	unsigned long	phys;
	void		*virt;
	size_t		size;
	bus_dma_tag_t	dmat;
	bus_dmamap_t	mapp;
} ct_dma_mem_t;

typedef struct _drv_t {
	char name [8];
	ct_chan_t *chan;
	ct_board_t *board;
	struct _bdrv_t *bd;
	ct_dma_mem_t dmamem;
	int running;
#ifdef NETGRAPH
	char	nodename [NG_NODESIZ];
	hook_p	hook;
	hook_p	debug_hook;
	node_p	node;
	struct	ifqueue queue;
	struct	ifqueue hi_queue;
#else
	struct	ifqueue queue;
	struct	ifnet *ifp;
#endif
	short	timeout;
	struct	callout timeout_handle;
	struct	cdev *devt;
} drv_t;

typedef struct _bdrv_t {
	ct_board_t	*board;
	struct resource	*base_res;
	struct resource	*drq_res;
	struct resource	*irq_res;
	int		base_rid;
	int		drq_rid;
	int		irq_rid;
	void		*intrhand;
	drv_t		channel [NCHAN];
	struct mtx	ct_mtx;
} bdrv_t;

static driver_t ct_isa_driver = {
	"ct",
	ct_isa_methods,
	sizeof (bdrv_t),
};

static devclass_t ct_devclass;

static void ct_receive (ct_chan_t *c, char *data, int len);
static void ct_transmit (ct_chan_t *c, void *attachment, int len);
static void ct_error (ct_chan_t *c, int data);
static void ct_up (drv_t *d);
static void ct_start (drv_t *d);
static void ct_down (drv_t *d);
static void ct_watchdog (drv_t *d);
static void ct_watchdog_timer (void *arg);
#ifdef NETGRAPH
extern struct ng_type typestruct;
#else
static void ct_ifstart (struct ifnet *ifp);
static void ct_tlf (struct sppp *sp);
static void ct_tls (struct sppp *sp);
static int ct_sioctl (struct ifnet *ifp, u_long cmd, caddr_t data);
static void ct_initialize (void *softc);
#endif

static ct_board_t *adapter [NCTAU];
static drv_t *channel [NCTAU*NCHAN];
static struct callout led_timo [NCTAU];
static struct callout timeout_handle;

static int ct_open (struct cdev *dev, int oflags, int devtype, struct thread *td);
static int ct_close (struct cdev *dev, int fflag, int devtype, struct thread *td);
static int ct_ioctl (struct cdev *dev, u_long cmd, caddr_t data, int flag, struct thread *td);
static struct cdevsw ct_cdevsw = {
	.d_version  = D_VERSION,
	.d_open     = ct_open,
	.d_close    = ct_close,
	.d_ioctl    = ct_ioctl,
	.d_name     = "ct",
};

/*
 * Make an mbuf from data.
 */
static struct mbuf *makembuf (void *buf, u_int len)
{
	struct mbuf *m;

	MGETHDR (m, M_NOWAIT, MT_DATA);
	if (! m)
		return 0;
	if (!(MCLGET(m, M_NOWAIT))) {
		m_freem (m);
		return 0;
	}
	m->m_pkthdr.len = m->m_len = len;
	bcopy (buf, mtod (m, caddr_t), len);
	return m;
}

static void ct_timeout (void *arg)
{
	drv_t *d;
	int s, i, k;

	for (i = 0; i < NCTAU; ++i) {
		if (adapter[i] == NULL)
			continue;
		for (k = 0; k < NCHAN; k++) {
			d = channel[i * NCHAN + k];
			if (! d)
				continue;
			if (d->chan->mode != M_G703)
				continue;
			s = splimp ();
			CT_LOCK ((bdrv_t *)d->bd);
			ct_g703_timer (d->chan);
			CT_UNLOCK ((bdrv_t *)d->bd);
			splx (s);
		}
	}
	
	callout_reset (&timeout_handle, hz, ct_timeout, 0);
}

static void ct_led_off (void *arg)
{
	ct_board_t *b = arg;
	bdrv_t *bd = ((drv_t *)b->chan->sys)->bd;
	int s = splimp ();

	CT_LOCK (bd);
	ct_led (b, 0);
	CT_UNLOCK (bd);
	splx (s);
}

/*
 * Activate interrupt handler from DDK.
 */
static void ct_intr (void *arg)
{
	bdrv_t *bd = arg;
	ct_board_t *b = bd->board;
#ifndef NETGRAPH
	int i;
#endif
	int s = splimp ();

	CT_LOCK (bd);
	/* Turn LED on. */
	ct_led (b, 1);

	ct_int_handler (b);

	/* Turn LED off 50 msec later. */
	callout_reset (&led_timo[b->num], hz/20, ct_led_off, b);
	CT_UNLOCK (bd);
	splx (s);

#ifndef NETGRAPH
	/* Pass packets in a lock-free state */
	for (i = 0; i < NCHAN && b->chan[i].type; i++) {
		drv_t *d = b->chan[i].sys;
		struct mbuf *m;
		if (!d || !d->running)
			continue;
		while (_IF_QLEN(&d->queue)) {
			IF_DEQUEUE (&d->queue,m);
			if (!m)
				continue;
			sppp_input (d->ifp, m);	
		}
	}
#endif
}

static int probe_irq (ct_board_t *b, int irq)
{
	int mask, busy, cnt;

	/* Clear pending irq, if any. */
	ct_probe_irq (b, -irq);
	DELAY (100);
	for (cnt=0; cnt<5; ++cnt) {
		/* Get the mask of pending irqs, assuming they are busy.
		 * Activate the adapter on given irq. */
		busy = ct_probe_irq (b, irq);
		DELAY (1000);

		/* Get the mask of active irqs.
		 * Deactivate our irq. */
		mask = ct_probe_irq (b, -irq);
		DELAY (100);
		if ((mask & ~busy) == 1 << irq) {
			ct_probe_irq (b, 0);
			/* printf ("ct%d: irq %d ok, mask=0x%04x, busy=0x%04x\n",
				b->num, irq, mask, busy); */
			return 1;
		}
	}
	/* printf ("ct%d: irq %d not functional, mask=0x%04x, busy=0x%04x\n",
		b->num, irq, mask, busy); */
	ct_probe_irq (b, 0);
	return 0;
}

static	short porttab [] = {
		0x200, 0x220, 0x240, 0x260, 0x280, 0x2a0, 0x2c0, 0x2e0,
		0x300, 0x320, 0x340, 0x360, 0x380, 0x3a0, 0x3c0, 0x3e0, 0
	};
static	char dmatab [] = { 7, 6, 5, 0 };
static	char irqtab [] = { 5, 10, 11, 7, 3, 15, 12, 0 };

static int ct_is_free_res (device_t dev, int rid, int type, rman_res_t start,
	rman_res_t end, rman_res_t count)
{
	struct resource *res;
	
	if (!(res = bus_alloc_resource (dev, type, &rid, start, end, count, 0)))
		return 0;
		
	bus_release_resource (dev, type, rid, res);
	
	return 1;
}

static void ct_identify (driver_t *driver, device_t dev)
{
	rman_res_t iobase, rescount;
	int devcount;
	device_t *devices;
	device_t child;
	devclass_t my_devclass;
	int i, k;

	if ((my_devclass = devclass_find ("ct")) == NULL)
		return;

	devclass_get_devices (my_devclass, &devices, &devcount);

	if (devcount == 0) {
		/* We should find all devices by our self. We could alter other
		 * devices, but we don't have a choise
		 */
		for (i = 0; (iobase = porttab [i]) != 0; i++) {
			if (!ct_is_free_res (dev, 0, SYS_RES_IOPORT,
			    iobase, iobase + NPORT, NPORT))
				continue;
			if (ct_probe_board (iobase, -1, -1) == 0)
				continue;
			
			devcount++;
			child = BUS_ADD_CHILD (dev, ISA_ORDER_SPECULATIVE, "ct",
			    -1);

			if (child == NULL)
				return;

			device_set_desc_copy (child, "Cronyx Tau-ISA");
			device_set_driver (child, driver);
			bus_set_resource (child, SYS_RES_IOPORT, 0,
			    iobase, NPORT);

			if (devcount >= NCTAU)
				break;
		}
	} else {
		static	short porttab [] = {
			0x200, 0x220, 0x240, 0x260, 0x280, 0x2a0, 0x2c0, 0x2e0,
			0x300, 0x320, 0x340, 0x360, 0x380, 0x3a0, 0x3c0, 0x3e0, 0
		};
		/* Lets check user choise.
		 */
		for (k = 0; k < devcount; k++) {
			if (bus_get_resource (devices[k], SYS_RES_IOPORT, 0,
			    &iobase, &rescount) != 0)
				continue;

			for (i = 0; porttab [i] != 0; i++) {
				if (porttab [i] != iobase)
					continue;
					
				if (!ct_is_free_res (devices[k], 0, SYS_RES_IOPORT,
				    iobase, iobase + NPORT, NPORT))
					continue;

				if (ct_probe_board (iobase, -1, -1) == 0)
					continue;
				porttab [i] = -1;
				device_set_desc_copy (devices[k], "Cronyx Tau-ISA");
				break;
			}
			if (porttab [i] == 0) {
				device_delete_child (
				    device_get_parent (devices[k]),
				    devices [k]);
				devices[k] = 0;
				continue;
			}
		}
		for (k = 0; k < devcount; k++) {
			if (devices[k] == 0)
				continue;
			if (bus_get_resource (devices[k], SYS_RES_IOPORT, 0,
			    &iobase, &rescount) == 0)
				continue;
			for (i = 0; (iobase = porttab [i]) != 0; i++) {
				if (porttab [i] == -1)
					continue;
				if (!ct_is_free_res (devices[k], 0, SYS_RES_IOPORT,
				    iobase, iobase + NPORT, NPORT))
					continue;
				if (ct_probe_board (iobase, -1, -1) == 0)
					continue;
			
				bus_set_resource (devices[k], SYS_RES_IOPORT, 0,
				    iobase, NPORT);
				porttab [i] = -1;
				device_set_desc_copy (devices[k], "Cronyx Tau-ISA");
				break;
			}
			if (porttab [i] == 0) {
				device_delete_child (
				    device_get_parent (devices[k]),
				    devices [k]);
			}
		}		
		free (devices, M_TEMP);
	}
	
	return;
}

static int ct_probe (device_t dev)
{
	int unit = device_get_unit (dev);
	rman_res_t iobase, rescount;

	if (!device_get_desc (dev) ||
	    strcmp (device_get_desc (dev), "Cronyx Tau-ISA"))
		return ENXIO;

/*	KASSERT ((bd != NULL), ("ct%d: NULL device softc\n", unit));*/
	if (bus_get_resource (dev, SYS_RES_IOPORT, 0, &iobase, &rescount) != 0) {
		printf ("ct%d: Couldn't get IOPORT\n", unit);
		return ENXIO;
	}

	if (!ct_is_free_res (dev, 0, SYS_RES_IOPORT,
	    iobase, iobase + NPORT, NPORT)) {
		printf ("ct%d: Resource IOPORT isn't free\n", unit);
		return ENXIO;
	}
		
	if (!ct_probe_board (iobase, -1, -1)) {
		printf ("ct%d: probing for Tau-ISA at %jx faild\n", unit, iobase);
		return ENXIO;
	}
	
	return 0;
}

static void
ct_bus_dmamap_addr (void *arg, bus_dma_segment_t *segs, int nseg, int error)
{
	unsigned long *addr;

	if (error)
		return;

	KASSERT(nseg == 1, ("too many DMA segments, %d should be 1", nseg));
	addr = arg;
	*addr = segs->ds_addr;
}

static int
ct_bus_dma_mem_alloc (int bnum, int cnum, ct_dma_mem_t *dmem)
{
	int error;

	error = bus_dma_tag_create (NULL, 16, 0, BUS_SPACE_MAXADDR_24BIT,
		BUS_SPACE_MAXADDR, NULL, NULL, dmem->size, 1,
		dmem->size, 0, NULL, NULL, &dmem->dmat);
	if (error) {
		if (cnum >= 0)	printf ("ct%d-%d: ", bnum, cnum);
		else		printf ("ct%d: ", bnum);
		printf ("couldn't allocate tag for dma memory\n");
 		return 0;
	}
	error = bus_dmamem_alloc (dmem->dmat, (void **)&dmem->virt,
		BUS_DMA_NOWAIT | BUS_DMA_ZERO, &dmem->mapp);
	if (error) {
		if (cnum >= 0)	printf ("ct%d-%d: ", bnum, cnum);
		else		printf ("ct%d: ", bnum);
		printf ("couldn't allocate mem for dma memory\n");
		bus_dma_tag_destroy (dmem->dmat);
 		return 0;
	}
	error = bus_dmamap_load (dmem->dmat, dmem->mapp, dmem->virt,
		dmem->size, ct_bus_dmamap_addr, &dmem->phys, 0);
	if (error) {
		if (cnum >= 0)	printf ("ct%d-%d: ", bnum, cnum);
		else		printf ("ct%d: ", bnum);
		printf ("couldn't load mem map for dma memory\n");
		bus_dmamem_free (dmem->dmat, dmem->virt, dmem->mapp);
		bus_dma_tag_destroy (dmem->dmat);
 		return 0;
	}
	return 1;
}

static void
ct_bus_dma_mem_free (ct_dma_mem_t *dmem)
{
	bus_dmamap_unload (dmem->dmat, dmem->mapp);
	bus_dmamem_free (dmem->dmat, dmem->virt, dmem->mapp);
	bus_dma_tag_destroy (dmem->dmat);
}

/*
 * The adapter is present, initialize the driver structures.
 */
static int ct_attach (device_t dev)
{
	bdrv_t *bd = device_get_softc (dev);
	rman_res_t iobase, drq, irq, rescount;
	int unit = device_get_unit (dev);
	char *ct_ln = CT_LOCK_NAME;
	ct_board_t *b;
	ct_chan_t *c;
	drv_t *d;
	int i;
	int s;

	KASSERT ((bd != NULL), ("ct%d: NULL device softc\n", unit));
	
	bus_get_resource (dev, SYS_RES_IOPORT, 0, &iobase, &rescount);
	bd->base_rid = 0;
	bd->base_res = bus_alloc_resource (dev, SYS_RES_IOPORT, &bd->base_rid,
		iobase, iobase + NPORT, NPORT, RF_ACTIVE);
	if (! bd->base_res) {
		printf ("ct%d: cannot alloc base address\n", unit);
		return ENXIO;
	}
	
	if (bus_get_resource (dev, SYS_RES_DRQ, 0, &drq, &rescount) != 0) {
		for (i = 0; (drq = dmatab [i]) != 0; i++) {
			if (!ct_is_free_res (dev, 0, SYS_RES_DRQ,
			    drq, drq + 1, 1))
				continue;
			bus_set_resource (dev, SYS_RES_DRQ, 0, drq, 1);
			break;
		}
		
		if (dmatab[i] == 0) {	
			bus_release_resource (dev, SYS_RES_IOPORT, bd->base_rid,
				bd->base_res);
			printf ("ct%d: Couldn't get DRQ\n", unit);
			return ENXIO;
		}
	}
	
	bd->drq_rid = 0;
	bd->drq_res = bus_alloc_resource (dev, SYS_RES_DRQ, &bd->drq_rid,
		drq, drq + 1, 1, RF_ACTIVE);
	if (! bd->drq_res) {
		printf ("ct%d: cannot allocate drq\n", unit);
		bus_release_resource (dev, SYS_RES_IOPORT, bd->base_rid,
			bd->base_res);
		return ENXIO;
	}	
	
	if (bus_get_resource (dev, SYS_RES_IRQ, 0, &irq, &rescount) != 0) {
		for (i = 0; (irq = irqtab [i]) != 0; i++) {
			if (!ct_is_free_res (dev, 0, SYS_RES_IRQ,
			    irq, irq + 1, 1))
				continue;
			bus_set_resource (dev, SYS_RES_IRQ, 0, irq, 1);
			break;
		}
		
		if (irqtab[i] == 0) {	
			bus_release_resource (dev, SYS_RES_DRQ, bd->drq_rid,
				bd->drq_res);
			bus_release_resource (dev, SYS_RES_IOPORT, bd->base_rid,
				bd->base_res);
			printf ("ct%d: Couldn't get IRQ\n", unit);
			return ENXIO;
		}
	}
	
	bd->irq_rid = 0;
	bd->irq_res = bus_alloc_resource (dev, SYS_RES_IRQ, &bd->irq_rid,
		irq, irq + 1, 1, RF_ACTIVE);
	if (! bd->irq_res) {
		printf ("ct%d: Couldn't allocate irq\n", unit);
		bus_release_resource (dev, SYS_RES_DRQ, bd->drq_rid,
			bd->drq_res);
		bus_release_resource (dev, SYS_RES_IOPORT, bd->base_rid,
			bd->base_res);
		return ENXIO;
	}
	
	b = malloc (sizeof (ct_board_t), M_DEVBUF, M_WAITOK);
	if (!b) {
		printf ("ct:%d: Couldn't allocate memory\n", unit);
		return (ENXIO);
	}
	adapter[unit] = b;
	bzero (b, sizeof(ct_board_t));
	
	if (! ct_open_board (b, unit, iobase, irq, drq)) {
		printf ("ct%d: error loading firmware\n", unit);
		free (b, M_DEVBUF);
		bus_release_resource (dev, SYS_RES_IRQ, bd->irq_rid,
			bd->irq_res);
		bus_release_resource (dev, SYS_RES_DRQ, bd->drq_rid,
			bd->drq_res);
		bus_release_resource (dev, SYS_RES_IOPORT, bd->base_rid,
			bd->base_res);
 		return ENXIO;
	}

	bd->board = b;
	
	ct_ln[2] = '0' + unit;
	mtx_init (&bd->ct_mtx, ct_ln, MTX_NETWORK_LOCK, MTX_DEF|MTX_RECURSE);
	if (! probe_irq (b, irq)) {
		printf ("ct%d: irq %jd not functional\n", unit, irq);
		bd->board = 0;
		adapter [unit] = 0;
		free (b, M_DEVBUF);
		bus_release_resource (dev, SYS_RES_IRQ, bd->irq_rid,
			bd->irq_res);
		bus_release_resource (dev, SYS_RES_DRQ, bd->drq_rid,
			bd->drq_res);
		bus_release_resource (dev, SYS_RES_IOPORT, bd->base_rid,
			bd->base_res);
		mtx_destroy (&bd->ct_mtx);
 		return ENXIO;
	}
	
	callout_init (&led_timo[unit], 1);
	s = splimp ();
	if (bus_setup_intr (dev, bd->irq_res,
			   INTR_TYPE_NET|INTR_MPSAFE,
			   NULL, ct_intr, bd, &bd->intrhand)) {
		printf ("ct%d: Can't setup irq %jd\n", unit, irq);
		bd->board = 0;
		adapter [unit] = 0;
		free (b, M_DEVBUF);
		bus_release_resource (dev, SYS_RES_IRQ, bd->irq_rid,
			bd->irq_res);
		bus_release_resource (dev, SYS_RES_DRQ, bd->drq_rid,
			bd->drq_res);
		bus_release_resource (dev, SYS_RES_IOPORT, bd->base_rid,
			bd->base_res);
		mtx_destroy (&bd->ct_mtx);
		splx (s);
 		return ENXIO;		
	}

	CT_LOCK (bd);
	ct_init_board (b, b->num, b->port, irq, drq, b->type, b->osc);
	ct_setup_board (b, 0, 0, 0);
	CT_UNLOCK (bd);

	printf ("ct%d: <Cronyx-%s>, clock %s MHz\n", b->num, b->name,
		b->osc == 20000000 ? "20" : "16.384");

	for (c = b->chan; c < b->chan + NCHAN; ++c) {
		d = &bd->channel[c->num];
		d->dmamem.size = sizeof(ct_buf_t);
		if (! ct_bus_dma_mem_alloc (unit, c->num, &d->dmamem))
			continue;
		d->board = b;
		d->chan = c;
		d->bd = bd;
		c->sys = d;
		channel [b->num*NCHAN + c->num] = d;
		sprintf (d->name, "ct%d.%d", b->num, c->num);
		callout_init (&d->timeout_handle, 1);

#ifdef NETGRAPH
		if (ng_make_node_common (&typestruct, &d->node) != 0) {
			printf ("%s: cannot make common node\n", d->name);
			channel [b->num*NCHAN + c->num] = 0;
			c->sys = 0;		
			ct_bus_dma_mem_free (&d->dmamem);
			continue;
		}
		NG_NODE_SET_PRIVATE (d->node, d);
		sprintf (d->nodename, "%s%d", NG_CT_NODE_TYPE,
			 c->board->num*NCHAN + c->num);
		if (ng_name_node (d->node, d->nodename)) {
			printf ("%s: cannot name node\n", d->nodename);
			NG_NODE_UNREF (d->node);
			channel [b->num*NCHAN + c->num] = 0;
			c->sys = 0;		
			ct_bus_dma_mem_free (&d->dmamem);
			continue;
		}
		d->queue.ifq_maxlen = ifqmaxlen;
		d->hi_queue.ifq_maxlen = ifqmaxlen;
		mtx_init (&d->queue.ifq_mtx, "ct_queue", NULL, MTX_DEF);
		mtx_init (&d->hi_queue.ifq_mtx, "ct_queue_hi", NULL, MTX_DEF);
#else /*NETGRAPH*/
		d->ifp = if_alloc(IFT_PPP);
		if (d->ifp == NULL) {
			printf ("%s: cannot if_alloc common interface\n",
			    d->name);
			channel [b->num*NCHAN + c->num] = 0;
			c->sys = 0;		
			ct_bus_dma_mem_free (&d->dmamem);
			continue;
		}
		d->ifp->if_softc	= d;
		if_initname (d->ifp, "ct", b->num * NCHAN + c->num);
		d->ifp->if_mtu		= PP_MTU;
		d->ifp->if_flags	= IFF_POINTOPOINT | IFF_MULTICAST;
		d->ifp->if_ioctl	= ct_sioctl;
		d->ifp->if_start	= ct_ifstart;
		d->ifp->if_init		= ct_initialize;
		d->queue.ifq_maxlen	= NBUF;
		mtx_init (&d->queue.ifq_mtx, "ct_queue", NULL, MTX_DEF);
		sppp_attach (d->ifp);
		if_attach (d->ifp);
		IFP2SP(d->ifp)->pp_tlf	= ct_tlf;
		IFP2SP(d->ifp)->pp_tls	= ct_tls;
		/* If BPF is in the kernel, call the attach for it.
		 * Header size is 4 bytes. */
		bpfattach (d->ifp, DLT_PPP, 4);
#endif /*NETGRAPH*/
		CT_LOCK (bd);
		ct_start_chan (c, d->dmamem.virt, d->dmamem.phys);
		ct_register_receive (c, &ct_receive);
		ct_register_transmit (c, &ct_transmit);
		ct_register_error (c, &ct_error);
		CT_UNLOCK (bd);
		d->devt = make_dev (&ct_cdevsw, b->num*NCHAN+c->num, UID_ROOT,
				GID_WHEEL, 0600, "ct%d", b->num*NCHAN+c->num);
	}
	splx (s);
	
	return 0;
}

static int ct_detach (device_t dev)
{
	bdrv_t *bd = device_get_softc (dev);
	ct_board_t *b = bd->board;
	ct_chan_t *c;
	int s;
	
	KASSERT (mtx_initialized (&bd->ct_mtx), ("ct mutex not initialized"));

	s = splimp ();
	CT_LOCK (bd);
	/* Check if the device is busy (open). */
	for (c = b->chan; c < b->chan + NCHAN; ++c) {
		drv_t *d = (drv_t*) c->sys;

		if (!d || !d->chan->type)
			continue;

		if (d->running) {
			CT_UNLOCK (bd);
			splx (s);
			return EBUSY;
		}
	}

	/* Deactivate the timeout routine. */
	callout_stop (&led_timo[b->num]);

	CT_UNLOCK (bd);
	
	bus_teardown_intr (dev, bd->irq_res, bd->intrhand);
	bus_release_resource (dev, SYS_RES_IRQ, bd->irq_rid, bd->irq_res);
	
	bus_release_resource (dev, SYS_RES_DRQ, bd->drq_rid, bd->drq_res);
	
	bus_release_resource (dev, SYS_RES_IOPORT, bd->base_rid, bd->base_res);

	CT_LOCK (bd);
	ct_close_board (b);
	CT_UNLOCK (bd);

	/* Detach the interfaces, free buffer memory. */
	for (c = b->chan; c < b->chan + NCHAN; ++c) {
		drv_t *d = (drv_t*) c->sys;

		if (!d || !d->chan->type)
			continue;

		callout_stop (&d->timeout_handle);
#ifdef NETGRAPH
		if (d->node) {
			ng_rmnode_self (d->node);
			NG_NODE_UNREF (d->node);
			d->node = NULL;
		}
		mtx_destroy (&d->queue.ifq_mtx);
		mtx_destroy (&d->hi_queue.ifq_mtx);
#else
		/* Detach from the packet filter list of interfaces. */
		bpfdetach (d->ifp);

		/* Detach from the sync PPP list. */
		sppp_detach (d->ifp);

		if_detach (d->ifp);
		if_free (d->ifp);
		IF_DRAIN (&d->queue);
		mtx_destroy (&d->queue.ifq_mtx);
#endif		
		destroy_dev (d->devt);
	}

	CT_LOCK (bd);
	ct_led_off (b);
	CT_UNLOCK (bd);
	callout_drain (&led_timo[b->num]);
	splx (s);
	
	for (c = b->chan; c < b->chan + NCHAN; ++c) {
		drv_t *d = (drv_t*) c->sys;

		if (!d || !d->chan->type)
			continue;
		callout_drain(&d->timeout_handle);
		
		/* Deallocate buffers. */
		ct_bus_dma_mem_free (&d->dmamem);
	}
	bd->board = NULL;
	adapter [b->num] = NULL;
	free (b, M_DEVBUF);
	
	mtx_destroy (&bd->ct_mtx);

	return 0;	
}

#ifndef NETGRAPH
static void ct_ifstart (struct ifnet *ifp)
{
	drv_t *d = ifp->if_softc;
	bdrv_t *bd = d->bd;
	
	CT_LOCK (bd);
	ct_start (d);
	CT_UNLOCK (bd);
}

static void ct_tlf (struct sppp *sp)
{
	drv_t *d = SP2IFP(sp)->if_softc;

	CT_DEBUG (d, ("ct_tlf\n"));
/*	ct_set_dtr (d->chan, 0);*/
/*	ct_set_rts (d->chan, 0);*/
	if (!(sp->pp_flags & PP_FR) && !(d->ifp->if_flags & PP_CISCO))
		sp->pp_down (sp);
}

static void ct_tls (struct sppp *sp)
{
	drv_t *d = SP2IFP(sp)->if_softc;

	CT_DEBUG (d, ("ct_tls\n"));
	if (!(sp->pp_flags & PP_FR) && !(d->ifp->if_flags & PP_CISCO))
		sp->pp_up (sp);
}

/*
 * Initialization of interface.
 * Ii seems to be never called by upper level.
 */
static void ct_initialize (void *softc)
{
	drv_t *d = softc;

	CT_DEBUG (d, ("ct_initialize\n"));
}

/*
 * Process an ioctl request.
 */
static int ct_sioctl (struct ifnet *ifp, u_long cmd, caddr_t data)
{
	drv_t *d = ifp->if_softc;
	bdrv_t *bd = d->bd;
	int error, s, was_up, should_be_up;

	was_up = (ifp->if_drv_flags & IFF_DRV_RUNNING) != 0;
	error = sppp_ioctl (ifp, cmd, data);
	if (error)
		return error;

	if (! (ifp->if_flags & IFF_DEBUG))
		d->chan->debug = 0;
	else
		d->chan->debug = d->chan->debug_shadow;

	switch (cmd) {
	default:	   CT_DEBUG2 (d, ("ioctl 0x%lx\n", cmd)); return 0;
	case SIOCADDMULTI: CT_DEBUG2 (d, ("SIOCADDMULTI\n"));     return 0;
	case SIOCDELMULTI: CT_DEBUG2 (d, ("SIOCDELMULTI\n"));     return 0;
	case SIOCSIFFLAGS: CT_DEBUG2 (d, ("SIOCSIFFLAGS\n"));     break;
	case SIOCSIFADDR:  CT_DEBUG2 (d, ("SIOCSIFADDR\n"));      break;
	}

	/* We get here only in case of SIFFLAGS or SIFADDR. */
	s = splimp ();
	CT_LOCK (bd);
	should_be_up = (ifp->if_drv_flags & IFF_DRV_RUNNING) != 0;
	if (! was_up && should_be_up) {
		/* Interface goes up -- start it. */
		ct_up (d);
		ct_start (d);
	} else if (was_up && ! should_be_up) {
		/* Interface is going down -- stop it. */
		/* if ((IFP2SP(d->ifp)->pp_flags & PP_FR) || (ifp->if_flags & PP_CISCO))*/
		ct_down (d);
	}
	CT_UNLOCK (bd);
	splx (s);
	return 0;
}
#endif /*NETGRAPH*/

/*
 * Stop the interface.  Called on splimp().
 */
static void ct_down (drv_t *d)
{
	int s = splimp ();
	CT_DEBUG (d, ("ct_down\n"));
	ct_set_dtr (d->chan, 0);
	ct_set_rts (d->chan, 0);
	d->running = 0;
	callout_stop (&d->timeout_handle);
	splx (s);
}

/*
 * Start the interface.  Called on splimp().
 */
static void ct_up (drv_t *d)
{
	int s = splimp ();
	CT_DEBUG (d, ("ct_up\n"));
	ct_set_dtr (d->chan, 1);
	ct_set_rts (d->chan, 1);
	d->running = 1;
	splx (s);
}

/*
 * Start output on the (slave) interface.  Get another datagram to send
 * off of the interface queue, and copy it to the interface
 * before starting the output.
 */
static void ct_send (drv_t *d)
{
	struct mbuf *m;
	u_short len;

	CT_DEBUG2 (d, ("ct_send, tn=%d\n", d->chan->tn));

	/* No output if the interface is down. */
	if (! d->running)
		return;

	/* No output if the modem is off. */
	if (! ct_get_dsr (d->chan) && !ct_get_loop (d->chan))
		return;

	while (ct_buf_free (d->chan)) {
		/* Get the packet to send. */
#ifdef NETGRAPH
		IF_DEQUEUE (&d->hi_queue, m);
		if (! m)
			IF_DEQUEUE (&d->queue, m);
#else
		m = sppp_dequeue (d->ifp);
#endif
		if (! m)
			return;
#ifndef NETGRAPH
		BPF_MTAP (d->ifp, m);
#endif
		len = m_length (m, NULL);
		if (! m->m_next)
			ct_send_packet (d->chan, (u_char*)mtod (m, caddr_t),
				len, 0);
		else {
			m_copydata (m, 0, len, d->chan->tbuf[d->chan->te]);
			ct_send_packet (d->chan, d->chan->tbuf[d->chan->te],
				len, 0);
		}
		m_freem (m);

		/* Set up transmit timeout, if the transmit ring is not empty.
		 * Transmit timeout is 10 seconds. */
		d->timeout = 10;
	}
#ifndef NETGRAPH
	d->ifp->if_drv_flags |= IFF_DRV_OACTIVE;
#endif
}

/*
 * Start output on the interface.
 * Always called on splimp().
 */
static void ct_start (drv_t *d)
{
	int s = splimp ();

	if (d->running) {
		if (! d->chan->dtr)
			ct_set_dtr (d->chan, 1);
		if (! d->chan->rts)
			ct_set_rts (d->chan, 1);
		ct_send (d);
		callout_reset (&d->timeout_handle, hz, ct_watchdog_timer, d);
	}

	splx (s);
}

/*
 * Handle transmit timeouts.
 * Recover after lost transmit interrupts.
 * Always called on splimp().
 */
static void ct_watchdog (drv_t *d)
{

	CT_DEBUG (d, ("device timeout\n"));
	if (d->running) {
		ct_setup_chan (d->chan);
		ct_start_chan (d->chan, 0, 0);
		ct_set_dtr (d->chan, 1);
		ct_set_rts (d->chan, 1);
		ct_start (d);
	}
}

static void ct_watchdog_timer (void *arg)
{
	drv_t *d = arg;
	bdrv_t *bd = d->bd;

	CT_LOCK (bd);
	if (d->timeout == 1)
		ct_watchdog (d);
	if (d->timeout)
		d->timeout--;
	callout_reset (&d->timeout_handle, hz, ct_watchdog_timer, d);
	CT_UNLOCK (bd);
}

/*
 * Transmit callback function.
 */
static void ct_transmit (ct_chan_t *c, void *attachment, int len)
{
	drv_t *d = c->sys;

	if (!d)
		return;
	d->timeout = 0;
#ifndef NETGRAPH
	if_inc_counter(d->ifp, IFCOUNTER_OPACKETS, 1);
	d->ifp->if_drv_flags &= ~IFF_DRV_OACTIVE;
#endif
	ct_start (d);
}

/*
 * Process the received packet.
 */
static void ct_receive (ct_chan_t *c, char *data, int len)
{
	drv_t *d = c->sys;
	struct mbuf *m;
#ifdef NETGRAPH
	int error;
#endif

	if (!d || !d->running)
		return;

	m = makembuf (data, len);
	if (! m) {
		CT_DEBUG (d, ("no memory for packet\n"));
#ifndef NETGRAPH
		if_inc_counter(d->ifp, IFCOUNTER_IQDROPS, 1);
#endif
		return;
	}
	if (c->debug > 1)
		m_print (m, 0);
#ifdef NETGRAPH
	m->m_pkthdr.rcvif = 0;
	NG_SEND_DATA_ONLY (error, d->hook, m);
#else
	if_inc_counter(d->ifp, IFCOUNTER_IPACKETS, 1);
	m->m_pkthdr.rcvif = d->ifp;
	/* Check if there's a BPF listener on this interface.
	 * If so, hand off the raw packet to bpf. */
	BPF_MTAP(d->ifp, m);
	IF_ENQUEUE (&d->queue, m);
#endif
}

/*
 * Error callback function.
 */
static void ct_error (ct_chan_t *c, int data)
{
	drv_t *d = c->sys;

	if (!d)
		return;

	switch (data) {
	case CT_FRAME:
		CT_DEBUG (d, ("frame error\n"));
#ifndef NETGRAPH
		if_inc_counter(d->ifp, IFCOUNTER_IERRORS, 1);
#endif
		break;
	case CT_CRC:
		CT_DEBUG (d, ("crc error\n"));
#ifndef NETGRAPH
		if_inc_counter(d->ifp, IFCOUNTER_IERRORS, 1);
#endif
		break;
	case CT_OVERRUN:
		CT_DEBUG (d, ("overrun error\n"));
#ifndef NETGRAPH
		if_inc_counter(d->ifp, IFCOUNTER_COLLISIONS, 1);
		if_inc_counter(d->ifp, IFCOUNTER_IERRORS, 1);
#endif
		break;
	case CT_OVERFLOW:
		CT_DEBUG (d, ("overflow error\n"));
#ifndef NETGRAPH
		if_inc_counter(d->ifp, IFCOUNTER_IERRORS, 1);
#endif
		break;
	case CT_UNDERRUN:
		CT_DEBUG (d, ("underrun error\n"));
		d->timeout = 0;
#ifndef NETGRAPH
		if_inc_counter(d->ifp, IFCOUNTER_OERRORS, 1);
		d->ifp->if_drv_flags &= ~IFF_DRV_OACTIVE;
#endif
		ct_start (d);
		break;
	default:
		CT_DEBUG (d, ("error #%d\n", data));
	}
}

static int ct_open (struct cdev *dev, int oflags, int devtype, struct thread *td)
{
	drv_t *d;

	if (dev2unit(dev) >= NCTAU*NCHAN || ! (d = channel[dev2unit(dev)]))
		return ENXIO;
		
	CT_DEBUG2 (d, ("ct_open\n"));
	return 0;
}

static int ct_close (struct cdev *dev, int fflag, int devtype, struct thread *td)
{
	drv_t *d = channel [dev2unit(dev)];

	if (!d)
		return 0;

	CT_DEBUG2 (d, ("ct_close\n"));
	return 0;
}

static int ct_modem_status (ct_chan_t *c)
{
	drv_t *d = c->sys;
	bdrv_t *bd;
	int status, s;

	if (!d)
		return 0;

	bd = d->bd;
	
	status = d->running ? TIOCM_LE : 0;
	s = splimp ();
	CT_LOCK (bd);
	if (ct_get_cd  (c)) status |= TIOCM_CD;
	if (ct_get_cts (c)) status |= TIOCM_CTS;
	if (ct_get_dsr (c)) status |= TIOCM_DSR;
	if (c->dtr)	    status |= TIOCM_DTR;
	if (c->rts)	    status |= TIOCM_RTS;
	CT_UNLOCK (bd);
	splx (s);
	return status;
}

/*
 * Process an ioctl request on /dev/cronyx/ctauN.
 */
static int ct_ioctl (struct cdev *dev, u_long cmd, caddr_t data, int flag, struct thread *td)
{
	drv_t *d = channel [dev2unit (dev)];
	bdrv_t *bd;
	ct_chan_t *c;
	struct serial_statistics *st;
	struct e1_statistics *opte1;
	int error, s;
	char mask[16];

	if (!d || !d->chan)
		return 0;

	bd = d->bd;
	c = d->chan;

	switch (cmd) {
	case SERIAL_GETREGISTERED:
		bzero (mask, sizeof(mask));
		for (s=0; s<NCTAU*NCHAN; ++s)
			if (channel [s])
				mask [s/8] |= 1 << (s & 7);
		bcopy (mask, data, sizeof (mask));
		return 0;

#ifndef NETGRAPH
	case SERIAL_GETPROTO:
		strcpy ((char*)data, (IFP2SP(d->ifp)->pp_flags & PP_FR) ? "fr" :
			(d->ifp->if_flags & PP_CISCO) ? "cisco" : "ppp");
		return 0;

	case SERIAL_SETPROTO:
		/* Only for superuser! */
		error = priv_check (td, PRIV_DRIVER);
		if (error)
			return error;
		if (d->ifp->if_drv_flags & IFF_DRV_RUNNING)
			return EBUSY;
		if (! strcmp ("cisco", (char*)data)) {
			IFP2SP(d->ifp)->pp_flags &= ~(PP_FR);
			IFP2SP(d->ifp)->pp_flags |= PP_KEEPALIVE;
			d->ifp->if_flags |= PP_CISCO;
		} else if (! strcmp ("fr", (char*)data)) {
			d->ifp->if_flags &= ~(PP_CISCO);
			IFP2SP(d->ifp)->pp_flags |= PP_FR | PP_KEEPALIVE;
		} else if (! strcmp ("ppp", (char*)data)) {
			IFP2SP(d->ifp)->pp_flags &= ~(PP_FR | PP_KEEPALIVE);
			d->ifp->if_flags &= ~(PP_CISCO);
		} else
			return EINVAL;
		return 0;

	case SERIAL_GETKEEPALIVE:
		if ((IFP2SP(d->ifp)->pp_flags & PP_FR) ||
			(d->ifp->if_flags & PP_CISCO))
			return EINVAL;
		*(int*)data = (IFP2SP(d->ifp)->pp_flags & PP_KEEPALIVE) ? 1 : 0;
		return 0;

	case SERIAL_SETKEEPALIVE:
		/* Only for superuser! */
		error = priv_check (td, PRIV_DRIVER);
		if (error)
			return error;
		if ((IFP2SP(d->ifp)->pp_flags & PP_FR) ||
			(d->ifp->if_flags & PP_CISCO))
			return EINVAL;
		if (*(int*)data)
			IFP2SP(d->ifp)->pp_flags |= PP_KEEPALIVE;
		else
			IFP2SP(d->ifp)->pp_flags &= ~PP_KEEPALIVE;
		return 0;
#endif /*NETGRAPH*/

	case SERIAL_GETMODE:
		*(int*)data = SERIAL_HDLC;
		return 0;

	case SERIAL_GETCFG:
		if (c->mode == M_HDLC)
			return EINVAL;
		switch (ct_get_config (c->board)) {
		default:    *(char*)data = 'a'; break;
		case CFG_B: *(char*)data = 'b'; break;
		case CFG_C: *(char*)data = 'c'; break;
		}
		return 0;

	case SERIAL_SETCFG:
		/* Only for superuser! */
		error = priv_check (td, PRIV_DRIVER);
		if (error)
			return error;
		if (c->mode == M_HDLC)
			return EINVAL;
		s = splimp ();
		CT_LOCK (bd);
		switch (*(char*)data) {
		case 'a': ct_set_config (c->board, CFG_A); break;
		case 'b': ct_set_config (c->board, CFG_B); break;
		case 'c': ct_set_config (c->board, CFG_C); break;
		}
		CT_UNLOCK (bd);
		splx (s);
		return 0;

	case SERIAL_GETSTAT:
		st = (struct serial_statistics*) data;
		st->rintr  = c->rintr;
		st->tintr  = c->tintr;
		st->mintr  = c->mintr;
		st->ibytes = c->ibytes;
		st->ipkts  = c->ipkts;
		st->ierrs  = c->ierrs;
		st->obytes = c->obytes;
		st->opkts  = c->opkts;
		st->oerrs  = c->oerrs;
		return 0;

	case SERIAL_GETESTAT:
		opte1 = (struct e1_statistics*)data;
		opte1->status	   = c->status;
		opte1->cursec	   = c->cursec;
		opte1->totsec	   = c->totsec + c->cursec;

		opte1->currnt.bpv   = c->currnt.bpv;
		opte1->currnt.fse   = c->currnt.fse;
		opte1->currnt.crce  = c->currnt.crce;
		opte1->currnt.rcrce = c->currnt.rcrce;
		opte1->currnt.uas   = c->currnt.uas;
		opte1->currnt.les   = c->currnt.les;
		opte1->currnt.es    = c->currnt.es;
		opte1->currnt.bes   = c->currnt.bes;
		opte1->currnt.ses   = c->currnt.ses;
		opte1->currnt.oofs  = c->currnt.oofs;
		opte1->currnt.css   = c->currnt.css;
		opte1->currnt.dm    = c->currnt.dm;

		opte1->total.bpv   = c->total.bpv   + c->currnt.bpv;
		opte1->total.fse   = c->total.fse   + c->currnt.fse;
		opte1->total.crce  = c->total.crce  + c->currnt.crce;
		opte1->total.rcrce = c->total.rcrce + c->currnt.rcrce;
		opte1->total.uas   = c->total.uas   + c->currnt.uas;
		opte1->total.les   = c->total.les   + c->currnt.les;
		opte1->total.es	   = c->total.es    + c->currnt.es;
		opte1->total.bes   = c->total.bes   + c->currnt.bes;
		opte1->total.ses   = c->total.ses   + c->currnt.ses;
		opte1->total.oofs  = c->total.oofs  + c->currnt.oofs;
		opte1->total.css   = c->total.css   + c->currnt.css;
		opte1->total.dm	   = c->total.dm    + c->currnt.dm;
		for (s=0; s<48; ++s) {
			opte1->interval[s].bpv   = c->interval[s].bpv;
			opte1->interval[s].fse   = c->interval[s].fse;
			opte1->interval[s].crce  = c->interval[s].crce;
			opte1->interval[s].rcrce = c->interval[s].rcrce;
			opte1->interval[s].uas   = c->interval[s].uas;
			opte1->interval[s].les   = c->interval[s].les;
			opte1->interval[s].es	 = c->interval[s].es;
			opte1->interval[s].bes   = c->interval[s].bes;
			opte1->interval[s].ses   = c->interval[s].ses;
			opte1->interval[s].oofs  = c->interval[s].oofs;
			opte1->interval[s].css   = c->interval[s].css;
			opte1->interval[s].dm	 = c->interval[s].dm;
		}
		return 0;

	case SERIAL_CLRSTAT:
		/* Only for superuser! */
		error = priv_check (td, PRIV_DRIVER);
		if (error)
			return error;
		c->rintr = 0;
		c->tintr = 0;
		c->mintr = 0;
		c->ibytes = 0;
		c->ipkts = 0;
		c->ierrs = 0;
		c->obytes = 0;
		c->opkts = 0;
		c->oerrs = 0;
		bzero (&c->currnt, sizeof (c->currnt));
		bzero (&c->total, sizeof (c->total));
		bzero (c->interval, sizeof (c->interval));
		return 0;

	case SERIAL_GETBAUD:
		*(long*)data = ct_get_baud(c);
		return 0;

	case SERIAL_SETBAUD:
		/* Only for superuser! */
		error = priv_check (td, PRIV_DRIVER);
		if (error)
			return error;
		s = splimp ();
		CT_LOCK (bd);
		ct_set_baud (c, *(long*)data);
		CT_UNLOCK (bd);
		splx (s);
		return 0;

	case SERIAL_GETLOOP:
		*(int*)data = ct_get_loop (c);
		return 0;

	case SERIAL_SETLOOP:
		/* Only for superuser! */
		error = priv_check (td, PRIV_DRIVER);
		if (error)
			return error;
		s = splimp ();
		CT_LOCK (bd);
		ct_set_loop (c, *(int*)data);
		CT_UNLOCK (bd);
		splx (s);
		return 0;

	case SERIAL_GETDPLL:
		if (c->mode == M_E1 || c->mode == M_G703)
			return EINVAL;
		*(int*)data = ct_get_dpll (c);
		return 0;

	case SERIAL_SETDPLL:
		/* Only for superuser! */
		error = priv_check (td, PRIV_DRIVER);
		if (error)
			return error;
		if (c->mode == M_E1 || c->mode == M_G703)
			return EINVAL;
		s = splimp ();
		CT_LOCK (bd);
		ct_set_dpll (c, *(int*)data);
		CT_UNLOCK (bd);
		splx (s);
		return 0;

	case SERIAL_GETNRZI:
		if (c->mode == M_E1 || c->mode == M_G703)
			return EINVAL;
		*(int*)data = ct_get_nrzi (c);
		return 0;

	case SERIAL_SETNRZI:
		/* Only for superuser! */
		error = priv_check (td, PRIV_DRIVER);
		if (error)
			return error;
		if (c->mode == M_E1 || c->mode == M_G703)
			return EINVAL;
		s = splimp ();
		CT_LOCK (bd);
		ct_set_nrzi (c, *(int*)data);
		CT_UNLOCK (bd);
		splx (s);
		return 0;

	case SERIAL_GETDEBUG:
		*(int*)data = c->debug;
		return 0;

	case SERIAL_SETDEBUG:
		/* Only for superuser! */
		error = priv_check (td, PRIV_DRIVER);
		if (error)
			return error;
#ifndef	NETGRAPH
		/*
		 * The debug_shadow is always greater than zero for logic 
		 * simplicity.  For switching debug off the IFF_DEBUG is
		 * responsible.
		 */
		c->debug_shadow = (*(int*)data) ? (*(int*)data) : 1;
		if (d->ifp->if_flags & IFF_DEBUG)
			c->debug = c->debug_shadow;
#else
		c->debug = *(int*)data;
#endif
		return 0;

	case SERIAL_GETHIGAIN:
		if (c->mode != M_E1)
			return EINVAL;
		*(int*)data = ct_get_higain (c);
		return 0;

	case SERIAL_SETHIGAIN:
		/* Only for superuser! */
		error = priv_check (td, PRIV_DRIVER);
		if (error)
			return error;
		s = splimp ();
		CT_LOCK (bd);
		ct_set_higain (c, *(int*)data);
		CT_UNLOCK (bd);
		splx (s);
		return 0;

	case SERIAL_GETPHONY:
		CT_DEBUG2 (d, ("ioctl: getphony\n"));
		if (c->mode != M_E1)
			return EINVAL;
		*(int*)data = c->gopt.phony;
		return 0;

	case SERIAL_SETPHONY:
		CT_DEBUG2 (d, ("ioctl: setphony\n"));
		if (c->mode != M_E1)
			return EINVAL;
		/* Only for superuser! */
		error = priv_check (td, PRIV_DRIVER);
		if (error)
			return error;
		s = splimp ();
		CT_LOCK (bd);
		ct_set_phony (c, *(int*)data);
		CT_UNLOCK (bd);
		splx (s);
		return 0;

	case SERIAL_GETCLK:
		if (c->mode != M_E1 && c->mode != M_G703)
			return EINVAL;
		switch (ct_get_clk(c)) {
		default:	 *(int*)data = E1CLK_INTERNAL;		break;
		case GCLK_RCV:   *(int*)data = E1CLK_RECEIVE;		break;
		case GCLK_RCLKO: *(int*)data = c->num ?
			E1CLK_RECEIVE_CHAN0 : E1CLK_RECEIVE_CHAN1;	break;
		}
		return 0;

	case SERIAL_SETCLK:
		/* Only for superuser! */
		error = priv_check (td, PRIV_DRIVER);
		if (error)
			return error;
		s = splimp ();
		CT_LOCK (bd);
		switch (*(int*)data) {
		default:		    ct_set_clk (c, GCLK_INT);   break;
		case E1CLK_RECEIVE:	    ct_set_clk (c, GCLK_RCV);   break;
		case E1CLK_RECEIVE_CHAN0:
		case E1CLK_RECEIVE_CHAN1:
					    ct_set_clk (c, GCLK_RCLKO); break;
		}
		CT_UNLOCK (bd);
		splx (s);
		return 0;

	case SERIAL_GETTIMESLOTS:
		if (c->mode != M_E1)
			return EINVAL;
		*(long*)data = ct_get_ts (c);
		return 0;

	case SERIAL_SETTIMESLOTS:
		/* Only for superuser! */
		error = priv_check (td, PRIV_DRIVER);
		if (error)
			return error;
		s = splimp ();
		CT_LOCK (bd);
		ct_set_ts (c, *(long*)data);
		CT_UNLOCK (bd);
		splx (s);
		return 0;

	case SERIAL_GETSUBCHAN:
		if (c->mode != M_E1)
			return EINVAL;
		*(long*)data = ct_get_subchan (c->board);
		return 0;

	case SERIAL_SETSUBCHAN:
		/* Only for superuser! */
		error = priv_check (td, PRIV_DRIVER);
		if (error)
			return error;
		s = splimp ();
		CT_LOCK (bd);
		ct_set_subchan (c->board, *(long*)data);
		CT_UNLOCK (bd);
		splx (s);
		return 0;

	case SERIAL_GETINVCLK:
	case SERIAL_GETINVTCLK:
		if (c->mode == M_E1 || c->mode == M_G703)
			return EINVAL;
		*(int*)data = ct_get_invtxc (c);
		return 0;

	case SERIAL_GETINVRCLK:
		if (c->mode == M_E1 || c->mode == M_G703)
			return EINVAL;
		*(int*)data = ct_get_invrxc (c);
		return 0;

	case SERIAL_SETINVCLK:
	case SERIAL_SETINVTCLK:
		/* Only for superuser! */
		error = priv_check (td, PRIV_DRIVER);
		if (error)
			return error;
		if (c->mode == M_E1 || c->mode == M_G703)
			return EINVAL;
		s = splimp ();
		CT_LOCK (bd);
		ct_set_invtxc (c, *(int*)data);
		CT_UNLOCK (bd);
		splx (s);
		return 0;

	case SERIAL_SETINVRCLK:
		/* Only for superuser! */
		error = priv_check (td, PRIV_DRIVER);
		if (error)
			return error;
		if (c->mode == M_E1 || c->mode == M_G703)
			return EINVAL;
		s = splimp ();
		CT_LOCK (bd);
		ct_set_invrxc (c, *(int*)data);
		CT_UNLOCK (bd);
		splx (s);
		return 0;

	case SERIAL_GETLEVEL:
		if (c->mode != M_G703)
			return EINVAL;
		s = splimp ();
		CT_LOCK (bd);
		*(int*)data = ct_get_lq (c);
		CT_UNLOCK (bd);
		splx (s);
		return 0;

	case TIOCSDTR:	/* Set DTR */
		s = splimp ();
		CT_LOCK (bd);
		ct_set_dtr (c, 1);
		CT_UNLOCK (bd);
		splx (s);
		return 0;

	case TIOCCDTR:	/* Clear DTR */
		s = splimp ();
		CT_LOCK (bd);
		ct_set_dtr (c, 0);
		CT_UNLOCK (bd);
		splx (s);
		return 0;

	case TIOCMSET:	/* Set DTR/RTS */
		s = splimp ();
		CT_LOCK (bd);
		ct_set_dtr (c, (*(int*)data & TIOCM_DTR) ? 1 : 0);
		ct_set_rts (c, (*(int*)data & TIOCM_RTS) ? 1 : 0);
		CT_UNLOCK (bd);
		splx (s);
		return 0;

	case TIOCMBIS:	/* Add DTR/RTS */
		s = splimp ();
		CT_LOCK (bd);
		if (*(int*)data & TIOCM_DTR) ct_set_dtr (c, 1);
		if (*(int*)data & TIOCM_RTS) ct_set_rts (c, 1);
		CT_UNLOCK (bd);
		splx (s);
		return 0;

	case TIOCMBIC:	/* Clear DTR/RTS */
		s = splimp ();
		CT_LOCK (bd);
		if (*(int*)data & TIOCM_DTR) ct_set_dtr (c, 0);
		if (*(int*)data & TIOCM_RTS) ct_set_rts (c, 0);
		CT_UNLOCK (bd);
		splx (s);
		return 0;

	case TIOCMGET:	/* Get modem status */
		*(int*)data = ct_modem_status (c);
		return 0;
	}
	return ENOTTY;
}

#ifdef NETGRAPH
static int ng_ct_constructor (node_p node)
{
	drv_t *d = NG_NODE_PRIVATE (node);
	CT_DEBUG (d, ("Constructor\n"));
	return EINVAL;
}

static int ng_ct_newhook (node_p node, hook_p hook, const char *name)
{
	int s;
	drv_t *d = NG_NODE_PRIVATE (node);

	if (!d)
		return EINVAL;
		
	bdrv_t *bd = d->bd;
	
	/* Attach debug hook */
	if (strcmp (name, NG_CT_HOOK_DEBUG) == 0) {
		NG_HOOK_SET_PRIVATE (hook, NULL);
		d->debug_hook = hook;
		return 0;
	}

	/* Check for raw hook */
	if (strcmp (name, NG_CT_HOOK_RAW) != 0)
		return EINVAL;

	NG_HOOK_SET_PRIVATE (hook, d);
	d->hook = hook;
	s = splimp ();
	CT_LOCK (bd);
	ct_up (d);
	CT_UNLOCK (bd);
	splx (s);
	return 0;
}

static char *format_timeslots (u_long s)
{
	static char buf [100];
	char *p = buf;
	int i;

	for (i=1; i<32; ++i)
		if ((s >> i) & 1) {
			int prev = (i > 1)  & (s >> (i-1));
			int next = (i < 31) & (s >> (i+1));

			if (prev) {
				if (next)
					continue;
				*p++ = '-';
			} else if (p > buf)
				*p++ = ',';

			if (i >= 10)
				*p++ = '0' + i / 10;
			*p++ = '0' + i % 10;
		}
	*p = 0;
	return buf;
}

static int print_modems (char *s, ct_chan_t *c, int need_header)
{
	int status = ct_modem_status (c);
	int length = 0;

	if (need_header)
		length += sprintf (s + length, "  LE   DTR  DSR  RTS  CTS  CD\n");
	length += sprintf (s + length, "%4s %4s %4s %4s %4s %4s\n",
		status & TIOCM_LE  ? "On" : "-",
		status & TIOCM_DTR ? "On" : "-",
		status & TIOCM_DSR ? "On" : "-",
		status & TIOCM_RTS ? "On" : "-",
		status & TIOCM_CTS ? "On" : "-",
		status & TIOCM_CD  ? "On" : "-");
	return length;
}

static int print_stats (char *s, ct_chan_t *c, int need_header)
{
	struct serial_statistics st;
	int length = 0;

	st.rintr  = c->rintr;
	st.tintr  = c->tintr;
	st.mintr  = c->mintr;
	st.ibytes = c->ibytes;
	st.ipkts  = c->ipkts;
	st.ierrs  = c->ierrs;
	st.obytes = c->obytes;
	st.opkts  = c->opkts;
	st.oerrs  = c->oerrs;
	if (need_header)
		length += sprintf (s + length, "  Rintr   Tintr   Mintr   Ibytes   Ipkts   Ierrs   Obytes   Opkts   Oerrs\n");
	length += sprintf (s + length, "%7ld %7ld %7ld %8ld %7ld %7ld %8ld %7ld %7ld\n",
		st.rintr, st.tintr, st.mintr, st.ibytes, st.ipkts,
		st.ierrs, st.obytes, st.opkts, st.oerrs);
	return length;
}

static char *format_e1_status (u_char status)
{
	static char buf [80];

	if (status & E1_NOALARM)
		return "Ok";
	buf[0] = 0;
	if (status & E1_LOS)     strcat (buf, ",LOS");
	if (status & E1_AIS)     strcat (buf, ",AIS");
	if (status & E1_LOF)     strcat (buf, ",LOF");
	if (status & E1_LOMF)    strcat (buf, ",LOMF");
	if (status & E1_FARLOF)  strcat (buf, ",FARLOF");
	if (status & E1_AIS16)   strcat (buf, ",AIS16");
	if (status & E1_FARLOMF) strcat (buf, ",FARLOMF");
	if (status & E1_TSTREQ)  strcat (buf, ",TSTREQ");
	if (status & E1_TSTERR)  strcat (buf, ",TSTERR");
	if (buf[0] == ',')
		return buf+1;
	return "Unknown";
}

static int print_frac (char *s, int leftalign, u_long numerator, u_long divider)
{
	int n, length = 0;

	if (numerator < 1 || divider < 1) {
		length += sprintf (s+length, leftalign ? "/-   " : "    -");
		return length;
	}
	n = (int) (0.5 + 1000.0 * numerator / divider);
	if (n < 1000) {
		length += sprintf (s+length, leftalign ? "/.%-3d" : " .%03d", n);
		return length;
	}
	*(s + length) = leftalign ? '/' : ' ';
	length ++;

	if     (n >= 1000000) n = (n+500) / 1000 * 1000;
	else if (n >= 100000)  n = (n+50)  / 100 * 100;
	else if (n >= 10000)   n = (n+5)   / 10 * 10;

	switch (n) {
	case 1000:    length += printf (s+length, ".999"); return length;
	case 10000:   n = 9990;   break;
	case 100000:  n = 99900;  break;
	case 1000000: n = 999000; break;
	}
	if (n < 10000)	      length += sprintf (s+length, "%d.%d", n/1000, n/10%100);
	else if (n < 100000)  length += sprintf (s+length, "%d.%d", n/1000, n/100%10);
	else if (n < 1000000) length += sprintf (s+length, "%d.", n/1000);
	else		      length += sprintf (s+length, "%d", n/1000);

	return length;
}

static int print_e1_stats (char *s, ct_chan_t *c)
{
	struct e1_counters total;
	u_long totsec;
	int length = 0;

	totsec		= c->totsec + c->cursec;
	total.bpv	= c->total.bpv   + c->currnt.bpv;
	total.fse	= c->total.fse   + c->currnt.fse;
	total.crce	= c->total.crce  + c->currnt.crce;
	total.rcrce	= c->total.rcrce + c->currnt.rcrce;
	total.uas	= c->total.uas   + c->currnt.uas;
	total.les	= c->total.les   + c->currnt.les;
	total.es	= c->total.es    + c->currnt.es;
	total.bes	= c->total.bes   + c->currnt.bes;
	total.ses	= c->total.ses   + c->currnt.ses;
	total.oofs	= c->total.oofs  + c->currnt.oofs;
	total.css	= c->total.css   + c->currnt.css;
	total.dm	= c->total.dm    + c->currnt.dm;

	length += sprintf (s + length, " Unav/Degr  Bpv/Fsyn  CRC/RCRC  Err/Lerr  Sev/Bur   Oof/Slp  Status\n");

	/* Unavailable seconds, degraded minutes */
	length += print_frac (s + length, 0, c->currnt.uas, c->cursec);
	length += print_frac (s + length, 1, 60 * c->currnt.dm, c->cursec);

	/* Bipolar violations, frame sync errors */
	length += print_frac (s + length, 0, c->currnt.bpv, c->cursec);
	length += print_frac (s + length, 1, c->currnt.fse, c->cursec);

	/* CRC errors, remote CRC errors (E-bit) */
	length += print_frac (s + length, 0, c->currnt.crce, c->cursec);
	length += print_frac (s + length, 1, c->currnt.rcrce, c->cursec);

	/* Errored seconds, line errored seconds */
	length += print_frac (s + length, 0, c->currnt.es, c->cursec);
	length += print_frac (s + length, 1, c->currnt.les, c->cursec);

	/* Severely errored seconds, burst errored seconds */
	length += print_frac (s + length, 0, c->currnt.ses, c->cursec);
	length += print_frac (s + length, 1, c->currnt.bes, c->cursec);

	/* Out of frame seconds, controlled slip seconds */
	length += print_frac (s + length, 0, c->currnt.oofs, c->cursec);
	length += print_frac (s + length, 1, c->currnt.css, c->cursec);

	length += sprintf (s + length, " %s\n", format_e1_status (c->status));

	/* Print total statistics. */
	length += print_frac (s + length, 0, total.uas, totsec);
	length += print_frac (s + length, 1, 60 * total.dm, totsec);

	length += print_frac (s + length, 0, total.bpv, totsec);
	length += print_frac (s + length, 1, total.fse, totsec);

	length += print_frac (s + length, 0, total.crce, totsec);
	length += print_frac (s + length, 1, total.rcrce, totsec);

	length += print_frac (s + length, 0, total.es, totsec);
	length += print_frac (s + length, 1, total.les, totsec);

	length += print_frac (s + length, 0, total.ses, totsec);
	length += print_frac (s + length, 1, total.bes, totsec);

	length += print_frac (s + length, 0, total.oofs, totsec);
	length += print_frac (s + length, 1, total.css, totsec);

	length += sprintf (s + length, " -- Total\n");
	return length;
}

static int print_chan (char *s, ct_chan_t *c)
{
	drv_t *d = c->sys;
	bdrv_t *bd = d->bd;
	int length = 0;

	length += sprintf (s + length, "ct%d", c->board->num * NCHAN + c->num);
	if (d->chan->debug)
		length += sprintf (s + length, " debug=%d", d->chan->debug);

	switch (ct_get_config (c->board)) {
	case CFG_A:	length += sprintf (s + length, " cfg=A");	break;
	case CFG_B:	length += sprintf (s + length, " cfg=B");	break;
	case CFG_C:	length += sprintf (s + length, " cfg=C");	break;
	default:	length += sprintf (s + length, " cfg=unknown"); break;
	}

	if (ct_get_baud (c))
		length += sprintf (s + length, " %ld", ct_get_baud (c));
	else
		length += sprintf (s + length, " extclock");

	if (c->mode == M_E1 || c->mode == M_G703)
		switch (ct_get_clk(c)) {
		case GCLK_INT   : length += sprintf (s + length, " syn=int");     break;
		case GCLK_RCV   : length += sprintf (s + length, " syn=rcv");     break;
		case GCLK_RCLKO  : length += sprintf (s + length, " syn=xrcv");    break;
		}
	if (c->mode == M_HDLC) {
		length += sprintf (s + length, " dpll=%s",   ct_get_dpll (c)   ? "on" : "off");
		length += sprintf (s + length, " nrzi=%s",   ct_get_nrzi (c)   ? "on" : "off");
		length += sprintf (s + length, " invtclk=%s", ct_get_invtxc (c) ? "on" : "off");
		length += sprintf (s + length, " invrclk=%s", ct_get_invrxc (c) ? "on" : "off");
	}
	if (c->mode == M_E1)
		length += sprintf (s + length, " higain=%s", ct_get_higain (c)? "on" : "off");

	length += sprintf (s + length, " loop=%s", ct_get_loop (c) ? "on" : "off");

	if (c->mode == M_E1)
		length += sprintf (s + length, " ts=%s", format_timeslots (ct_get_ts(c)));
	if (c->mode == M_E1 && ct_get_config (c->board) != CFG_A)
		length += sprintf (s + length, " pass=%s", format_timeslots (ct_get_subchan(c->board)));
	if (c->mode == M_G703) {
		int lq, x;

		x = splimp ();
		CT_LOCK (bd);
		lq = ct_get_lq (c);
		CT_UNLOCK (bd);
		splx (x);
		length += sprintf (s + length, " (level=-%.1fdB)", lq / 10.0);
	}
	length += sprintf (s + length, "\n");
	return length;
}

static int ng_ct_rcvmsg (node_p node, item_p item, hook_p lasthook)
{
	drv_t *d = NG_NODE_PRIVATE (node);
	struct ng_mesg *msg;
	struct ng_mesg *resp = NULL;
	int error = 0;

	if (!d)
		return EINVAL;
		
	CT_DEBUG (d, ("Rcvmsg\n"));
	NGI_GET_MSG (item, msg);
	switch (msg->header.typecookie) {
	default:
		error = EINVAL;
		break;

	case NGM_CT_COOKIE:
		printf ("Don't forget to implement\n");
		error = EINVAL;
		break;

	case NGM_GENERIC_COOKIE:
		switch (msg->header.cmd) {
		default:
			error = EINVAL;
			break;

		case NGM_TEXT_STATUS: {
			char *s;
			int l = 0;
			int dl = sizeof (struct ng_mesg) + 730;

			NG_MKRESPONSE (resp, msg, dl, M_NOWAIT);
			if (! resp) {
				error = ENOMEM;
				break;
			}
			s = (resp)->data;
			l += print_chan (s + l, d->chan);
			l += print_stats (s + l, d->chan, 1);
			l += print_modems (s + l, d->chan, 1);
			l += print_e1_stats (s + l, d->chan);
			strncpy ((resp)->header.cmdstr, "status", NG_CMDSTRSIZ);
			}
			break;
		}
		break;
	}
	NG_RESPOND_MSG (error, node, item, resp);
	NG_FREE_MSG (msg);
	return error;
}

static int ng_ct_rcvdata (hook_p hook, item_p item)
{
	drv_t *d = NG_NODE_PRIVATE (NG_HOOK_NODE(hook));
	struct mbuf *m;
	struct ng_tag_prio *ptag;
	bdrv_t *bd;
	struct ifqueue *q;
	int s;

	if (!d)
		return ENETDOWN;
		
	bd = d->bd;
	NGI_GET_M (item, m);
	NG_FREE_ITEM (item);
	if (! NG_HOOK_PRIVATE (hook) || ! d) {
		NG_FREE_M (m);
		return ENETDOWN;
	}

	/* Check for high priority data */
	if ((ptag = (struct ng_tag_prio *)m_tag_locate(m, NGM_GENERIC_COOKIE,
	    NG_TAG_PRIO, NULL)) != NULL && (ptag->priority > NG_PRIO_CUTOFF) )
		q = &d->hi_queue;
	else
		q = &d->queue;

	s = splimp ();
	CT_LOCK (bd);
	IF_LOCK (q);
	if (_IF_QFULL (q)) {
		IF_UNLOCK (q);
		CT_UNLOCK (bd);
		splx (s);
		NG_FREE_M (m);
		return ENOBUFS;
	}
	_IF_ENQUEUE (q, m);
	IF_UNLOCK (q);
	ct_start (d);
	CT_UNLOCK (bd);
	splx (s);
	return 0;
}

static int ng_ct_rmnode (node_p node)
{
	drv_t *d = NG_NODE_PRIVATE (node);
	bdrv_t *bd;

	CT_DEBUG (d, ("Rmnode\n"));
	if (d && d->running) {
		bd = d->bd;
		int s = splimp ();
		CT_LOCK (bd);
		ct_down (d);
		CT_UNLOCK (bd);
		splx (s);
	}
#ifdef	KLD_MODULE
	if (node->nd_flags & NGF_REALLY_DIE) {
		NG_NODE_SET_PRIVATE (node, NULL);
		NG_NODE_UNREF (node);
	}
	NG_NODE_REVIVE(node);		/* Persistant node */
#endif
	return 0;
}

static int ng_ct_connect (hook_p hook)
{
	drv_t *d = NG_NODE_PRIVATE (NG_HOOK_NODE (hook));

	if (!d)
		return 0;
		
	callout_reset (&d->timeout_handle, hz, ct_watchdog_timer, d);
	return 0;
}

static int ng_ct_disconnect (hook_p hook)
{
	drv_t *d = NG_NODE_PRIVATE (NG_HOOK_NODE (hook));
	bdrv_t *bd;
	
	if (!d)
		return 0;
	
	bd = d->bd;
	
	CT_LOCK (bd);
	if (NG_HOOK_PRIVATE (hook))
		ct_down (d);
	CT_UNLOCK (bd);
	/* If we were wait it than it reasserted now, just stop it. */
	if (!callout_drain (&d->timeout_handle))
		callout_stop (&d->timeout_handle);
	return 0;
}
#endif

static int ct_modevent (module_t mod, int type, void *unused)
{
	static int load_count = 0;

	switch (type) {
	case MOD_LOAD:
#ifdef NETGRAPH
		if (ng_newtype (&typestruct))
			printf ("Failed to register ng_ct\n");
#endif
		++load_count;
		callout_init (&timeout_handle, 1);
		callout_reset (&timeout_handle, hz*5, ct_timeout, 0);
		break;
	case MOD_UNLOAD:
		if (load_count == 1) {
			printf ("Removing device entry for Tau-ISA\n");
#ifdef NETGRAPH
			ng_rmtype (&typestruct);
#endif			
		}
		/* If we were wait it than it reasserted now, just stop it. */
		if (!callout_drain (&timeout_handle))
			callout_stop (&timeout_handle);
		--load_count;
		break;
	case MOD_SHUTDOWN:
		break;
	}
	return 0;
}

#ifdef NETGRAPH
static struct ng_type typestruct = {
	.version	= NG_ABI_VERSION,
	.name		= NG_CT_NODE_TYPE,
	.constructor	= ng_ct_constructor,
	.rcvmsg		= ng_ct_rcvmsg,
	.shutdown	= ng_ct_rmnode,
	.newhook	= ng_ct_newhook,
	.connect	= ng_ct_connect,
	.rcvdata	= ng_ct_rcvdata,
	.disconnect	= ng_ct_disconnect,
};
#endif /*NETGRAPH*/

#ifdef NETGRAPH
MODULE_DEPEND (ng_ct, netgraph, NG_ABI_VERSION, NG_ABI_VERSION, NG_ABI_VERSION);
#else
MODULE_DEPEND (ct, sppp, 1, 1, 1);
#endif
DRIVER_MODULE (ct, isa, ct_isa_driver, ct_devclass, ct_modevent, NULL);
MODULE_VERSION (ct, 1);
