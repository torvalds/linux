/*
 * Cronyx-Tau32-PCI adapter driver for FreeBSD.
 *
 * Copyright (C) 2003-2005 Cronyx Engineering.
 * Copyright (C) 2003-2005 Kurakin Roman, <rik@FreeBSD.org>
 *
 * This software is distributed with NO WARRANTIES, not even the implied
 * warranties for MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 *
 * Authors grant any other persons or organisations a permission to use,
 * modify and redistribute this software in source and binary forms,
 * as long as this message is kept with the software, all derivative
 * works or modified versions.
 *
 * $Cronyx: if_ce.c,v 1.9.2.8 2005/11/21 14:17:44 rik Exp $
 */

#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");

#include <sys/param.h>

#if __FreeBSD_version >= 500000
#   define NPCI 1
#else
#   include "pci.h"
#endif

#if NPCI > 0

#include <sys/ucred.h>
#include <sys/priv.h>
#include <sys/proc.h>
#include <sys/systm.h>
#include <sys/mbuf.h>
#include <sys/kernel.h>
#include <sys/module.h>
#include <sys/conf.h>
#include <sys/malloc.h>
#include <sys/socket.h>
#include <sys/sockio.h>
#if __FreeBSD_version >= 504000
#include <sys/sysctl.h>
#endif
#include <sys/tty.h>
#include <sys/bus.h>
#include <vm/vm.h>
#include <vm/pmap.h>
#include <net/if.h>
#include <net/if_var.h>
#if __FreeBSD_version > 501000
#   include <dev/pci/pcivar.h>
#   include <dev/pci/pcireg.h>
#else
#   include <pci/pcivar.h>
#   include <pci/pcireg.h>
#endif
#include <machine/bus.h>
#include <sys/rman.h>
#include "opt_ng_cronyx.h"
#ifdef NETGRAPH_CRONYX
#   include "opt_netgraph.h"
#   ifndef NETGRAPH
#	error #option	NETGRAPH missed from configuration
#   endif
#   include <netgraph/ng_message.h>
#   include <netgraph/netgraph.h>
#   include <dev/ce/ng_ce.h>
#else
#   include <net/if_types.h>
#   include <net/if_sppp.h>
#   define PP_CISCO IFF_LINK2
#   include <net/bpf.h>
#endif
#include <dev/cx/machdep.h>
#include <dev/ce/ceddk.h>
#include <machine/cserial.h>
#include <machine/resource.h>

/* If we don't have Cronyx's sppp version, we don't have fr support via sppp */
#ifndef PP_FR
#define PP_FR 0
#endif

#ifndef IFP2SP
#define IFP2SP(ifp)	((struct sppp*)ifp)
#endif
#ifndef SP2IFP
#define SP2IFP(sp)	((struct ifnet*)sp)
#endif

#ifndef PCIR_BAR
#define PCIR_BAR(x)	(PCIR_MAPS + (x) * 4)
#endif

/* define as our previous return value */
#ifndef BUS_PROBE_DEFAULT
#define BUS_PROBE_DEFAULT 0
#endif

#define CE_DEBUG(d,s)	({if (d->chan->debug) {\
				printf ("%s: ", d->name); printf s;}})
#define CE_DEBUG2(d,s)	({if (d->chan->debug>1) {\
				printf ("%s: ", d->name); printf s;}})

#ifndef IF_DRAIN
#define IF_DRAIN(ifq) do {		\
	struct mbuf *m;			\
	for (;;) {			\
		IF_DEQUEUE(ifq, m);	\
		if (m == NULL)		\
			break;		\
		m_freem(m);		\
	}				\
} while (0)
#endif

#ifndef _IF_QLEN
#define _IF_QLEN(ifq)	((ifq)->ifq_len)
#endif

#ifndef callout_drain
#define callout_drain callout_stop
#endif

#define CE_LOCK_NAME		"ceX"

#define CE_LOCK(_bd)		mtx_lock (&(_bd)->ce_mtx)
#define CE_UNLOCK(_bd)		mtx_unlock (&(_bd)->ce_mtx)
#define CE_LOCK_ASSERT(_bd)	mtx_assert (&(_bd)->ce_mtx, MA_OWNED)

#define CDEV_MAJOR	185

static	int ce_probe		__P((device_t));
static	int ce_attach		__P((device_t));
static	int ce_detach		__P((device_t));

static	device_method_t ce_methods[] = {
	/* Device interface */
	DEVMETHOD(device_probe,		ce_probe),
	DEVMETHOD(device_attach,	ce_attach),
	DEVMETHOD(device_detach,	ce_detach),

	DEVMETHOD_END
};

typedef struct _ce_dma_mem_t {
	unsigned long	phys;
	void		*virt;
	size_t		size;
#if __FreeBSD_version >= 500000
	bus_dma_tag_t	dmat;
	bus_dmamap_t	mapp;
#endif
} ce_dma_mem_t;

typedef struct _drv_t {
	char	name [8];
	int	running;
	ce_board_t	*board;
	ce_chan_t	*chan;
	struct ifqueue	rqueue;
#ifdef NETGRAPH
	char	nodename [NG_NODESIZE];
	hook_p	hook;
	hook_p	debug_hook;
	node_p	node;
	struct	ifqueue queue;
	struct	ifqueue hi_queue;
#else
	struct	ifnet *ifp;
#endif
	short	timeout;
	struct	callout timeout_handle;
#if __FreeBSD_version >= 500000
	struct	cdev *devt;
#else /* __FreeBSD_version < 500000 */
	dev_t	devt;
#endif
	ce_dma_mem_t dmamem;
} drv_t;

typedef	struct _bdrv_t {
	ce_board_t	*board;
	struct resource *ce_res;
	struct resource *ce_irq;
	void		*ce_intrhand;
	ce_dma_mem_t	dmamem;
	drv_t		channel [NCHAN];
#if __FreeBSD_version >= 504000
	struct mtx	ce_mtx;
#endif
} bdrv_t;

static	driver_t ce_driver = {
	"ce",
	ce_methods,
	sizeof(bdrv_t),
};

static	devclass_t ce_devclass;

static void ce_receive (ce_chan_t *c, unsigned char *data, int len);
static void ce_transmit (ce_chan_t *c, void *attachment, int len);
static void ce_error (ce_chan_t *c, int data);
static void ce_up (drv_t *d);
static void ce_start (drv_t *d);
static void ce_down (drv_t *d);
static void ce_watchdog (drv_t *d);
static void ce_watchdog_timer (void *arg);
#ifdef NETGRAPH
extern struct ng_type typestruct;
#else
static void ce_ifstart (struct ifnet *ifp);
static void ce_tlf (struct sppp *sp);
static void ce_tls (struct sppp *sp);
static int ce_sioctl (struct ifnet *ifp, u_long cmd, caddr_t data);
static void ce_initialize (void *softc);
#endif

static ce_board_t *adapter [NBRD];
static drv_t *channel [NBRD*NCHAN];
static struct callout led_timo [NBRD];
static struct callout timeout_handle;

static int ce_destroy = 0;

#if __FreeBSD_version < 500000
static int ce_open (dev_t dev, int oflags, int devtype, struct proc *p);
static int ce_close (dev_t dev, int fflag, int devtype, struct proc *p);
static int ce_ioctl (dev_t dev, u_long cmd, caddr_t data, int flag, struct proc *p);
#else
static int ce_open (struct cdev *dev, int oflags, int devtype, struct thread *td);
static int ce_close (struct cdev *dev, int fflag, int devtype, struct thread *td);
static int ce_ioctl (struct cdev *dev, u_long cmd, caddr_t data, int flag, struct thread *td);
#endif
#if __FreeBSD_version < 500000
static struct cdevsw ce_cdevsw = {
	ce_open,	ce_close,	noread,		nowrite,
	ce_ioctl,	nopoll,		nommap,		nostrategy,
	"ce",		CDEV_MAJOR,	nodump,		nopsize,
	D_NAGGED,	-1
	};
#elif __FreeBSD_version == 500000
static struct cdevsw ce_cdevsw = {
	ce_open,	ce_close,	noread,		nowrite,
	ce_ioctl,	nopoll,		nommap,		nostrategy,
	"ce",		CDEV_MAJOR,	nodump,		nopsize,
	D_NAGGED,
	};
#elif __FreeBSD_version <= 501000
static struct cdevsw ce_cdevsw = {
	.d_open	    = ce_open,
	.d_close    = ce_close,
	.d_read     = noread,
	.d_write    = nowrite,
	.d_ioctl    = ce_ioctl,
	.d_poll     = nopoll,
	.d_mmap	    = nommap,
	.d_strategy = nostrategy,
	.d_name     = "ce",
	.d_maj	    = CDEV_MAJOR,
	.d_dump     = nodump,
	.d_flags    = D_NAGGED,
};
#elif __FreeBSD_version < 502103
static struct cdevsw ce_cdevsw = {
	.d_open     = ce_open,
	.d_close    = ce_close,
	.d_ioctl    = ce_ioctl,
	.d_name     = "ce",
	.d_maj	    = CDEV_MAJOR,
	.d_flags    = D_NAGGED,
};
#elif __FreeBSD_version < 600000
static struct cdevsw ce_cdevsw = {
	.d_version  = D_VERSION,
	.d_open     = ce_open,
	.d_close    = ce_close,
	.d_ioctl    = ce_ioctl,
	.d_name     = "ce",
	.d_maj	    = CDEV_MAJOR,
	.d_flags    = D_NEEDGIANT,
};
#else /* __FreeBSD_version >= 600000 */
static struct cdevsw ce_cdevsw = {
	.d_version  = D_VERSION,
	.d_open     = ce_open,
	.d_close    = ce_close,
	.d_ioctl    = ce_ioctl,
	.d_name     = "ce",
};
#endif

/*
 * Make an mbuf from data.
 */
static struct mbuf *makembuf (void *buf, unsigned len)
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

static int ce_probe (device_t dev)
{
	if ((pci_get_vendor (dev) == TAU32_PCI_VENDOR_ID) &&
	    (pci_get_device (dev) == TAU32_PCI_DEVICE_ID)) {
		device_set_desc (dev, "Cronyx-Tau32-PCI serial adapter");
		return BUS_PROBE_DEFAULT;
	}
	return ENXIO;
}

static void ce_timeout (void *arg)
{
	drv_t *d;
	int s, i, k;

	for (i = 0; i < NBRD; ++i) {
		if (adapter[i] == NULL)
			continue;
		for (k = 0; k < NCHAN; ++k) {
			s = splimp ();
			if (ce_destroy) {
				splx (s);
				return;
			}
			d = channel[i * NCHAN + k];
			if (!d) {
				splx (s);
				continue;
			}
			CE_LOCK ((bdrv_t *)d->board->sys);
			switch (d->chan->type) {
			case T_E1:
				ce_e1_timer (d->chan);
				break;
			default:
				break;
			}
			CE_UNLOCK ((bdrv_t *)d->board->sys);
			splx (s);
		}
	}
	s = splimp ();
	if (!ce_destroy)
		callout_reset (&timeout_handle, hz, ce_timeout, 0);
	splx (s);
}

static void ce_led_off (void *arg)
{
	ce_board_t *b = arg;
	bdrv_t *bd = (bdrv_t *) b->sys;
	int s;
	s = splimp ();
	if (ce_destroy) {
		splx (s);
		return;
	}
	CE_LOCK (bd);
	TAU32_LedSet (b->ddk.pControllerObject, 0);
	CE_UNLOCK (bd);
	splx (s);
}

static void ce_intr (void *arg)
{
	bdrv_t *bd = arg;
	ce_board_t *b = bd->board;
	int s;
	int i;
#if __FreeBSD_version >= 500000 && defined NETGRAPH
	int error;
#endif
	s = splimp ();
	if (ce_destroy) {
		splx (s);
		return;
	}
	CE_LOCK (bd);
	/* Turn LED on. */
	TAU32_LedSet (b->ddk.pControllerObject, 1);

	TAU32_HandleInterrupt (b->ddk.pControllerObject);

	/* Turn LED off 50 msec later. */
	callout_reset (&led_timo[b->num], hz/20, ce_led_off, b);
	CE_UNLOCK (bd);
	splx (s);

	/* Pass packets in a lock-free state */
	for (i = 0; i < NCHAN && b->chan[i].type; i++) {
		drv_t *d = b->chan[i].sys;
		struct mbuf *m;
		if (!d || !d->running)
			continue;
		while (_IF_QLEN(&d->rqueue)) {
			IF_DEQUEUE (&d->rqueue,m);
			if (!m)
				continue;
#ifdef NETGRAPH
			if (d->hook) {
#if __FreeBSD_version >= 500000
				NG_SEND_DATA_ONLY (error, d->hook, m);
#else
				ng_queue_data (d->hook, m, 0);
#endif
			} else {
				IF_DRAIN (&d->rqueue);
			}
#else
			sppp_input (d->ifp, m);	
#endif
		}
	}
}

#if __FreeBSD_version >= 500000
static void
ce_bus_dmamap_addr (void *arg, bus_dma_segment_t *segs, int nseg, int error)
{
	unsigned long *addr;

	if (error)
		return;

	KASSERT(nseg == 1, ("too many DMA segments, %d should be 1", nseg));
	addr = arg;
	*addr = segs->ds_addr;
}

#ifndef BUS_DMA_ZERO
#define BUS_DMA_ZERO 0
#endif

static int
ce_bus_dma_mem_alloc (int bnum, int cnum, ce_dma_mem_t *dmem)
{
	int error;

	error = bus_dma_tag_create (NULL, 16, 0, BUS_SPACE_MAXADDR_32BIT,
		BUS_SPACE_MAXADDR, NULL, NULL, dmem->size, 1,
		dmem->size, 0,
#if __FreeBSD_version >= 502000
		NULL, NULL,
#endif
		&dmem->dmat);
	if (error) {
		if (cnum >= 0)	printf ("ce%d-%d: ", bnum, cnum);
		else		printf ("ce%d: ", bnum);
		printf ("couldn't allocate tag for dma memory\n");
 		return 0;
	}
	error = bus_dmamem_alloc (dmem->dmat, (void **)&dmem->virt,
		BUS_DMA_NOWAIT | BUS_DMA_ZERO, &dmem->mapp);
	if (error) {
		if (cnum >= 0)	printf ("ce%d-%d: ", bnum, cnum);
		else		printf ("ce%d: ", bnum);
		printf ("couldn't allocate mem for dma memory\n");
		bus_dma_tag_destroy (dmem->dmat);
 		return 0;
	}
	error = bus_dmamap_load (dmem->dmat, dmem->mapp, dmem->virt,
		dmem->size, ce_bus_dmamap_addr, &dmem->phys, 0);
	if (error) {
		if (cnum >= 0)	printf ("ce%d-%d: ", bnum, cnum);
		else		printf ("ce%d: ", bnum);
		printf ("couldn't load mem map for dma memory\n");
		bus_dmamem_free (dmem->dmat, dmem->virt, dmem->mapp);
		bus_dma_tag_destroy (dmem->dmat);
 		return 0;
	}
#if __FreeBSD_version >= 502000
	bzero (dmem->virt, dmem->size);
#endif
	return 1;
}

static void
ce_bus_dma_mem_free (ce_dma_mem_t *dmem)
{
	bus_dmamap_unload (dmem->dmat, dmem->mapp);
	bus_dmamem_free (dmem->dmat, dmem->virt, dmem->mapp);
	bus_dma_tag_destroy (dmem->dmat);
}
#else
static int
ce_bus_dma_mem_alloc (int bnum, int cnum, ce_dma_mem_t *dmem)
{
	dmem->virt = contigmalloc (dmem->size, M_DEVBUF, M_WAITOK,
				   0x100000, 0xffffffff, 16, 0);
	if (dmem->virt == NULL) {
		if (cnum >= 0)	printf ("ce%d-%d: ", bnum, cnum);
		else		printf ("ce%d: ", bnum);
		printf ("couldn't allocate dma memory\n");
 		return 0;
	}
	dmem->phys = vtophys (dmem->virt);
	bzero (dmem->virt, dmem->size);
	return 1;
}

static void
ce_bus_dma_mem_free (ce_dma_mem_t *dmem)
{
	contigfree (dmem->virt, dmem->size, M_DEVBUF);
}
#endif

/*
 * Called if the probe succeeded.
 */
static int ce_attach (device_t dev)
{
	bdrv_t *bd = device_get_softc (dev);
	int unit = device_get_unit (dev);
#if __FreeBSD_version >= 504000
	char *ce_ln = CE_LOCK_NAME;
#endif
	vm_offset_t vbase;
	int rid, error;
	ce_board_t *b;
	ce_chan_t *c;
	drv_t *d;
	int s;
		
	b = malloc (sizeof(ce_board_t), M_DEVBUF, M_WAITOK);
	if (!b) {
		printf ("ce%d: couldn't allocate memory\n", unit);
		return (ENXIO);
	}
	bzero (b, sizeof(ce_board_t));

	b->ddk.sys = &b;

#if __FreeBSD_version >= 440000
	pci_enable_busmaster (dev);
#endif

	bd->dmamem.size = TAU32_ControllerObjectSize;
	if (! ce_bus_dma_mem_alloc (unit, -1, &bd->dmamem)) {
		free (b, M_DEVBUF);
		return (ENXIO);
	}
	b->ddk.pControllerObject = bd->dmamem.virt;

	bd->board = b;
	b->sys = bd;
	rid = PCIR_BAR(0);
	bd->ce_res = bus_alloc_resource (dev, SYS_RES_MEMORY, &rid,
			0, ~0, 1, RF_ACTIVE);
	if (! bd->ce_res) {
		printf ("ce%d: cannot map memory\n", unit);
		ce_bus_dma_mem_free (&bd->dmamem);
		free (b, M_DEVBUF);
		return (ENXIO);
	}
	vbase = (vm_offset_t) rman_get_virtual (bd->ce_res);

	b->ddk.PciBar1VirtualAddress = (void *)vbase;
	b->ddk.ControllerObjectPhysicalAddress = bd->dmamem.phys;
	b->ddk.pErrorNotifyCallback = ce_error_callback;
	b->ddk.pStatusNotifyCallback = ce_status_callback;
	b->num = unit;

	TAU32_BeforeReset(&b->ddk);
	pci_write_config (dev, TAU32_PCI_RESET_ADDRESS, TAU32_PCI_RESET_ON, 4);
	pci_write_config (dev, TAU32_PCI_RESET_ADDRESS, TAU32_PCI_RESET_OFF, 4);

	if(!TAU32_Initialize(&b->ddk, 0))
	{
		printf ("ce%d: init adapter error 0x%08x, bus dead bits 0x%08lx\n",
			unit, b->ddk.InitErrors, b->ddk.DeadBits);
		bus_release_resource (dev, SYS_RES_MEMORY, PCIR_BAR(0), bd->ce_res);
		ce_bus_dma_mem_free (&bd->dmamem);
		free (b, M_DEVBUF);
		return (ENXIO);
	}

	s = splimp ();

	ce_init_board (b);

	rid = 0;
	bd->ce_irq = bus_alloc_resource (dev, SYS_RES_IRQ, &rid, 0, ~0, 1,
			RF_SHAREABLE | RF_ACTIVE);
	if (! bd->ce_irq) {
		printf ("ce%d: cannot map interrupt\n", unit);
		bus_release_resource (dev, SYS_RES_MEMORY, PCIR_BAR(0), bd->ce_res);
		ce_bus_dma_mem_free (&bd->dmamem);
		free (b, M_DEVBUF);
		splx (s);
		return (ENXIO);
	}
#if __FreeBSD_version >= 500000
	callout_init (&led_timo[unit], 1);
#else
	callout_init (&led_timo[unit]);
#endif
	error  = bus_setup_intr (dev, bd->ce_irq,
#if __FreeBSD_version >= 500013
				INTR_TYPE_NET|INTR_MPSAFE,
#else
				INTR_TYPE_NET,
#endif
				NULL, ce_intr, bd, &bd->ce_intrhand);
	if (error) {
		printf ("ce%d: cannot set up irq\n", unit);
		bus_release_resource (dev, SYS_RES_IRQ, 0, bd->ce_irq);
		bus_release_resource (dev, SYS_RES_MEMORY,
				PCIR_BAR(0), bd->ce_res);
		ce_bus_dma_mem_free (&bd->dmamem);
		free (b, M_DEVBUF);
		splx (s);
		return (ENXIO);
 	}

	switch (b->ddk.Model) {
	case 1:		strcpy (b->name, TAU32_BASE_NAME);	break;
	case 2:		strcpy (b->name, TAU32_LITE_NAME);	break;
	case 3:		strcpy (b->name, TAU32_ADPCM_NAME);	break;
	default:	strcpy (b->name, TAU32_UNKNOWN_NAME);	break;
	}

	printf ("ce%d: %s\n", unit, b->name);

	for (c = b->chan; c < b->chan + NCHAN; ++c) {
		c->num = (c - b->chan);
		c->board = b;

		d = &bd->channel[c->num];
		d->dmamem.size = sizeof(ce_buf_t);
		if (! ce_bus_dma_mem_alloc (unit, c->num, &d->dmamem))
			continue;

		channel [b->num * NCHAN + c->num] = d;
		sprintf (d->name, "ce%d.%d", b->num, c->num);
		d->board = b;
		d->chan = c;
		c->sys = d;
	}

	for (c = b->chan; c < b->chan + NCHAN; ++c) {
		if (c->sys == NULL)
			continue;
		d = c->sys;

		callout_init (&d->timeout_handle, 1);
#ifdef NETGRAPH
		if (ng_make_node_common (&typestruct, &d->node) != 0) {
			printf ("%s: cannot make common node\n", d->name);
			d->node = NULL;
			continue;
		}
#if __FreeBSD_version >= 500000
		NG_NODE_SET_PRIVATE (d->node, d);
#else
		d->node->private = d;
#endif
		sprintf (d->nodename, "%s%d", NG_CE_NODE_TYPE,
			 c->board->num * NCHAN + c->num);
		if (ng_name_node (d->node, d->nodename)) {
			printf ("%s: cannot name node\n", d->nodename);
#if __FreeBSD_version >= 500000
			NG_NODE_UNREF (d->node);
#else
			ng_rmnode (d->node);
			ng_unref (d->node);
#endif
			continue;
		}
		d->queue.ifq_maxlen	= ifqmaxlen;
		d->hi_queue.ifq_maxlen	= ifqmaxlen;
		d->rqueue.ifq_maxlen	= ifqmaxlen;
#if __FreeBSD_version >= 500000
		mtx_init (&d->queue.ifq_mtx, "ce_queue", NULL, MTX_DEF);
		mtx_init (&d->hi_queue.ifq_mtx, "ce_queue_hi", NULL, MTX_DEF);
		mtx_init (&d->rqueue.ifq_mtx, "ce_rqueue", NULL, MTX_DEF);
#endif		
#else /*NETGRAPH*/
#if __FreeBSD_version >= 600031
		d->ifp = if_alloc(IFT_PPP);
#else
		d->ifp = malloc (sizeof(struct sppp), M_DEVBUF, M_WAITOK);
		bzero (d->ifp, sizeof(struct sppp));
#endif
		if (!d->ifp) {
			printf ("%s: cannot if_alloc() interface\n", d->name);
			continue;
		}
		d->ifp->if_softc	= d;
#if __FreeBSD_version > 501000
		if_initname (d->ifp, "ce", b->num * NCHAN + c->num);
#else
		d->ifp->if_unit		= b->num * NCHAN + c->num;
		d->ifp->if_name		= "ce";
#endif
		d->ifp->if_mtu		= PP_MTU;
		d->ifp->if_flags	= IFF_POINTOPOINT | IFF_MULTICAST;
		d->ifp->if_ioctl	= ce_sioctl;
		d->ifp->if_start	= ce_ifstart;
		d->ifp->if_init		= ce_initialize;
		d->rqueue.ifq_maxlen	= ifqmaxlen;
#if __FreeBSD_version >= 500000
		mtx_init (&d->rqueue.ifq_mtx, "ce_rqueue", NULL, MTX_DEF);
#endif		
		sppp_attach (d->ifp);
		if_attach (d->ifp);
		IFP2SP(d->ifp)->pp_tlf	= ce_tlf;
		IFP2SP(d->ifp)->pp_tls	= ce_tls;
		/* If BPF is in the kernel, call the attach for it.
		 * The header size of PPP or Cisco/HDLC is 4 bytes. */
		bpfattach (d->ifp, DLT_PPP, 4);
#endif /*NETGRAPH*/
		ce_start_chan (c, 1, 1, d->dmamem.virt, d->dmamem.phys);

		/* Register callback functions. */
		ce_register_transmit (c, &ce_transmit);
		ce_register_receive (c, &ce_receive);
		ce_register_error (c, &ce_error);
		d->devt = make_dev (&ce_cdevsw, b->num*NCHAN+c->num, UID_ROOT,
				GID_WHEEL, 0600, "ce%d", b->num*NCHAN+c->num);
	}

#if __FreeBSD_version >= 504000
	ce_ln[2] = '0' + unit;
	mtx_init (&bd->ce_mtx, ce_ln, MTX_NETWORK_LOCK, MTX_DEF|MTX_RECURSE);
#endif
	CE_LOCK (bd);
	TAU32_EnableInterrupts(b->ddk.pControllerObject);
	adapter[unit] = b;
	CE_UNLOCK (bd);
	splx (s);

	return 0;
}

static int ce_detach (device_t dev)
{
	bdrv_t *bd = device_get_softc (dev);
	ce_board_t *b = bd->board;
	ce_chan_t *c;
	int s;

#if __FreeBSD_version >= 504000
	KASSERT (mtx_initialized (&bd->ce_mtx), ("ce mutex not initialized"));
#endif
	s = splimp ();
	CE_LOCK (bd);
	/* Check if the device is busy (open). */
	for (c = b->chan; c < b->chan + NCHAN; ++c) {
		drv_t *d = (drv_t*) c->sys;

		/* XXX Non existen chan! */
		if (! d || ! d->chan)
			continue;
		if (d->running) {
			CE_UNLOCK (bd);
			splx (s);
			return EBUSY;
		}
	}

	/* Ok, we can unload driver */
	/* At first we should disable interrupts */
	ce_destroy = 1;
	TAU32_DisableInterrupts(b->ddk.pControllerObject);

	callout_stop (&led_timo[b->num]);

	for (c = b->chan; c < b->chan + NCHAN; ++c) {
		drv_t *d = (drv_t*) c->sys;

		if (! d || ! d->chan)
			continue;
		callout_stop (&d->timeout_handle);
#ifndef NETGRAPH
		/* Detach from the packet filter list of interfaces. */
		bpfdetach (d->ifp);

		/* Detach from the sync PPP list. */
		sppp_detach (d->ifp);

		/* Detach from the system list of interfaces. */
		if_detach (d->ifp);
#if __FreeBSD_version > 600031
		if_free(d->ifp);
#else
		free (d->ifp, M_DEVBUF);
#endif

		IF_DRAIN (&d->rqueue);
#if __FreeBSD_version >= 500000
		mtx_destroy (&d->rqueue.ifq_mtx);
#endif
#else
#if __FreeBSD_version >= 500000
		if (d->node) {
			ng_rmnode_self (d->node);
			NG_NODE_UNREF (d->node);
			d->node = NULL;
		}
		IF_DRAIN (&d->rqueue);
		mtx_destroy (&d->queue.ifq_mtx);
		mtx_destroy (&d->hi_queue.ifq_mtx);
		mtx_destroy (&d->rqueue.ifq_mtx);
#else
		ng_rmnode (d->node);
		d->node = 0;
#endif
#endif
		destroy_dev (d->devt);
	}

	CE_UNLOCK (bd);
	splx (s);

	callout_drain (&led_timo[b->num]);

	/* Disable the interrupt request. */
	bus_teardown_intr (dev, bd->ce_irq, bd->ce_intrhand);
	bus_release_resource (dev, SYS_RES_IRQ, 0, bd->ce_irq);
	TAU32_DestructiveHalt (b->ddk.pControllerObject, 0);
	bus_release_resource (dev, SYS_RES_MEMORY, PCIR_BAR(0), bd->ce_res);

	for (c = b->chan; c < b->chan + NCHAN; ++c) {
		drv_t *d = (drv_t*) c->sys;

		if (! d || ! d->chan)
			continue;
		callout_drain (&d->timeout_handle);
		channel [b->num * NCHAN + c->num] = NULL;
		/* Deallocate buffers. */
		ce_bus_dma_mem_free (&d->dmamem);
	}
	adapter [b->num] = NULL;
	ce_bus_dma_mem_free (&bd->dmamem);
	free (b, M_DEVBUF);
#if __FreeBSD_version >= 504000
	mtx_destroy (&bd->ce_mtx);
#endif
	return 0;
}

#ifndef NETGRAPH
static void ce_ifstart (struct ifnet *ifp)
{
	drv_t *d = ifp->if_softc;
	bdrv_t *bd = d->board->sys;

	CE_LOCK (bd);
	ce_start (d);
	CE_UNLOCK (bd);
}

static void ce_tlf (struct sppp *sp)
{
	drv_t *d = SP2IFP(sp)->if_softc;

	CE_DEBUG2 (d, ("ce_tlf\n"));
	sp->pp_down (sp);
}

static void ce_tls (struct sppp *sp)
{
	drv_t *d = SP2IFP(sp)->if_softc;

	CE_DEBUG2 (d, ("ce_tls\n"));
	sp->pp_up (sp);
}

/*
 * Process an ioctl request.
 */
static int ce_sioctl (struct ifnet *ifp, u_long cmd, caddr_t data)
{
	drv_t *d = ifp->if_softc;
	bdrv_t *bd = d->board->sys;
	int error, s, was_up, should_be_up;

#if __FreeBSD_version >= 600034
	was_up = (ifp->if_drv_flags & IFF_DRV_RUNNING) != 0;
#else
	was_up = (ifp->if_flags & IFF_RUNNING) != 0;
#endif
	error = sppp_ioctl (ifp, cmd, data);

	if (error)
		return error;

	if (! (ifp->if_flags & IFF_DEBUG))
		d->chan->debug = 0;
	else
		d->chan->debug = d->chan->debug_shadow;

	switch (cmd) {
	default:	   CE_DEBUG2 (d, ("ioctl 0x%lx\n", cmd));   return 0;
	case SIOCADDMULTI: CE_DEBUG2 (d, ("ioctl SIOCADDMULTI\n")); return 0;
	case SIOCDELMULTI: CE_DEBUG2 (d, ("ioctl SIOCDELMULTI\n")); return 0;
	case SIOCSIFFLAGS: CE_DEBUG2 (d, ("ioctl SIOCSIFFLAGS\n")); break;
	case SIOCSIFADDR:  CE_DEBUG2 (d, ("ioctl SIOCSIFADDR\n"));  break;
	}

	/* We get here only in case of SIFFLAGS or SIFADDR. */
	s = splimp ();
	CE_LOCK (bd);
#if __FreeBSD_version >= 600034
	should_be_up = (ifp->if_drv_flags & IFF_DRV_RUNNING) != 0;
#else
	should_be_up = (ifp->if_flags & IFF_RUNNING) != 0;
#endif
	if (! was_up && should_be_up) {
		/* Interface goes up -- start it. */
		ce_up (d);
		ce_start (d);
	} else if (was_up && ! should_be_up) {
		/* Interface is going down -- stop it. */
/*		if ((IFP2SP(ifp)->pp_flags & PP_FR) || (ifp->if_flags & PP_CISCO))*/
		ce_down (d);
	}
	CE_DEBUG (d, ("ioctl 0x%lx p4\n", cmd));
	CE_UNLOCK (bd);
	splx (s);
	return 0;
}

/*
 * Initialization of interface.
 * It seems to be never called by upper level?
 */
static void ce_initialize (void *softc)
{
	drv_t *d = softc;

	CE_DEBUG (d, ("ce_initialize\n"));
}
#endif /*NETGRAPH*/

/*
 * Stop the interface.  Called on splimp().
 */
static void ce_down (drv_t *d)
{
	CE_DEBUG (d, ("ce_down\n"));
	/* Interface is going down -- stop it. */
	ce_set_dtr (d->chan, 0);
	ce_set_rts (d->chan, 0);

	d->running = 0;
	callout_stop (&d->timeout_handle);
}

/*
 * Start the interface.  Called on splimp().
 */
static void ce_up (drv_t *d)
{
	CE_DEBUG (d, ("ce_up\n"));
	ce_set_dtr (d->chan, 1);
	ce_set_rts (d->chan, 1);

	d->running = 1;
}

/*
 * Start output on the interface.  Get another datagram to send
 * off of the interface queue, and copy it to the interface
 * before starting the output.
 */
static void ce_send (drv_t *d)
{
	struct mbuf *m;
	u_short len;

	CE_DEBUG2 (d, ("ce_send\n"));

	/* No output if the interface is down. */
	if (! d->running)
		return;

	while (ce_transmit_space (d->chan)) {
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
#if __FreeBSD_version >= 500000
		BPF_MTAP (d->ifp, m);
#else
		if (d->ifp->if_bpf)
			bpf_mtap (d->ifp, m);
#endif
#endif
#if __FreeBSD_version >= 490000
		len = m_length (m, NULL);
#else
		len = m->m_pkthdr.len;
#endif
		if (len >= BUFSZ)
			printf ("%s: too long packet: %d bytes: ",
				d->name, len);
		else if (! m->m_next)
			ce_send_packet (d->chan, (u_char*) mtod (m, caddr_t), len, 0);
		else {
			ce_buf_item_t *item = (ce_buf_item_t*)d->chan->tx_queue;
			m_copydata (m, 0, len, item->buf);
			ce_send_packet (d->chan, item->buf, len, 0);
		}
		m_freem (m);
		/* Set up transmit timeout, if the transmit ring is not empty.*/
		d->timeout = 10;
	}
#ifndef NETGRAPH
#if __FreeBSD_version >= 600034
	d->ifp->if_flags |= IFF_DRV_OACTIVE;
#else
	d->ifp->if_flags |= IFF_OACTIVE;
#endif
#endif
}

/*
 * Start output on the interface.
 * Always called on splimp().
 */
static void ce_start (drv_t *d)
{
	if (d->running) {
		if (! d->chan->dtr)
			ce_set_dtr (d->chan, 1);
		if (! d->chan->rts)
			ce_set_rts (d->chan, 1);
		ce_send (d);
		callout_reset (&d->timeout_handle, hz, ce_watchdog_timer, d);
	}
}

/*
 * Handle transmit timeouts.
 * Recover after lost transmit interrupts.
 * Always called on splimp().
 */
static void ce_watchdog (drv_t *d)
{
	CE_DEBUG (d, ("device timeout\n"));
	if (d->running) {
		ce_set_dtr (d->chan, 0);
		ce_set_rts (d->chan, 0);
/*		ce_stop_chan (d->chan);*/
/*		ce_start_chan (d->chan, 1, 1, 0, 0);*/
		ce_set_dtr (d->chan, 1);
		ce_set_rts (d->chan, 1);
		ce_start (d);
	}
}

static void ce_watchdog_timer (void *arg)
{
	drv_t *d = arg;
	bdrv_t *bd = d->board->sys;

	CE_LOCK(bd);
	if (d->timeout == 1)
		ce_watchdog (d);
	if (d->timeout)
		d->timeout--;
	callout_reset (&d->timeout_handle, hz, ce_watchdog_timer, d);
	CE_UNLOCK(bd);
}

static void ce_transmit (ce_chan_t *c, void *attachment, int len)
{
	drv_t *d = c->sys;

	d->timeout = 0;
#ifndef NETGRAPH
	if_inc_counter(d->ifp, IFCOUNTER_OPACKETS, 1);
#if __FreeBSD_version >=  600034
	d->ifp->if_flags &= ~IFF_DRV_OACTIVE;
#else
	d->ifp->if_flags &= ~IFF_OACTIVE;
#endif
#endif
	ce_start (d);
}

static void ce_receive (ce_chan_t *c, unsigned char *data, int len)
{
	drv_t *d = c->sys;
	struct mbuf *m;

	if (! d->running)
		return;

	m = makembuf (data, len);
	if (! m) {
		CE_DEBUG (d, ("no memory for packet\n"));
#ifndef NETGRAPH
		if_inc_counter(d->ifp, IFCOUNTER_IQDROPS, 1);
#endif
		return;
	}
	if (c->debug > 1)
		m_print (m, 0);
#ifdef NETGRAPH
	m->m_pkthdr.rcvif = 0;
	IF_ENQUEUE(&d->rqueue, m);
#else
	if_inc_counter(d->ifp, IFCOUNTER_IPACKETS, 1);
	m->m_pkthdr.rcvif = d->ifp;
	/* Check if there's a BPF listener on this interface.
	 * If so, hand off the raw packet to bpf. */
	BPF_MTAP(d->ifp, m);
	IF_ENQUEUE(&d->rqueue, m);
#endif
}

static void ce_error (ce_chan_t *c, int data)
{
	drv_t *d = c->sys;

	switch (data) {
	case CE_FRAME:
		CE_DEBUG (d, ("frame error\n"));
#ifndef NETGRAPH
		if_inc_counter(d->ifp, IFCOUNTER_IERRORS, 1);
#endif
		break;
	case CE_CRC:
		CE_DEBUG (d, ("crc error\n"));
#ifndef NETGRAPH
		if_inc_counter(d->ifp, IFCOUNTER_IERRORS, 1);
#endif
		break;
	case CE_OVERRUN:
		CE_DEBUG (d, ("overrun error\n"));
#ifndef NETGRAPH
		if_inc_counter(d->ifp, IFCOUNTER_COLLISIONS, 1);
		if_inc_counter(d->ifp, IFCOUNTER_IERRORS, 1);
#endif
		break;
	case CE_OVERFLOW:
		CE_DEBUG (d, ("overflow error\n"));
#ifndef NETGRAPH
		if_inc_counter(d->ifp, IFCOUNTER_IERRORS, 1);
#endif
		break;
	case CE_UNDERRUN:
		CE_DEBUG (d, ("underrun error\n"));
		d->timeout = 0;
#ifndef NETGRAPH
		if_inc_counter(d->ifp, IFCOUNTER_OERRORS, 1);
#if __FreeBSD_version >= 600034
		d->ifp->if_flags &= ~IFF_DRV_OACTIVE;
#else
		d->ifp->if_flags &= ~IFF_OACTIVE;
#endif
#endif
		ce_start (d);
		break;
	default:
		CE_DEBUG (d, ("error #%d\n", data));
		break;
	}
}

/*
 * You also need read, write, open, close routines.
 * This should get you started
 */
#if __FreeBSD_version < 500000
static int ce_open (dev_t dev, int oflags, int devtype, struct proc *p)
#else
static int ce_open (struct cdev *dev, int oflags, int devtype, struct thread *td)
#endif
{
	int unit = dev2unit (dev);
	drv_t *d;

	if (unit >= NBRD*NCHAN || ! (d = channel[unit]))
		return ENXIO;
	CE_DEBUG2 (d, ("ce_open\n"));
	return 0;
}

/*
 * Only called on the LAST close.
 */
#if __FreeBSD_version < 500000
static int ce_close (dev_t dev, int fflag, int devtype, struct proc *p)
#else
static int ce_close (struct cdev *dev, int fflag, int devtype, struct thread *td)
#endif
{
	drv_t *d = channel [dev2unit (dev)];

	CE_DEBUG2 (d, ("ce_close\n"));
	return 0;
}

static int ce_modem_status (ce_chan_t *c)
{
	drv_t *d = c->sys;
	bdrv_t *bd = d->board->sys;
	int status, s;

	status = d->running ? TIOCM_LE : 0;
	s = splimp ();
	CE_LOCK (bd);
	if (ce_get_cd  (c)) status |= TIOCM_CD;
	if (ce_get_cts (c)) status |= TIOCM_CTS;
	if (ce_get_dsr (c)) status |= TIOCM_DSR;
	if (c->dtr)	    status |= TIOCM_DTR;
	if (c->rts)	    status |= TIOCM_RTS;
	CE_UNLOCK (bd);
	splx (s);
	return status;
}

#if __FreeBSD_version < 500000
static int ce_ioctl (dev_t dev, u_long cmd, caddr_t data, int flag, struct proc *p)
#else
static int ce_ioctl (struct cdev *dev, u_long cmd, caddr_t data, int flag, struct thread *td)
#endif
{
	drv_t *d = channel [dev2unit (dev)];
	bdrv_t *bd = d->board->sys;
	ce_chan_t *c = d->chan;
	struct serial_statistics *st;
	struct e1_statistics *opte1;
	int error, s;
	char mask[16];

	switch (cmd) {
	case SERIAL_GETREGISTERED:
		CE_DEBUG2 (d, ("ioctl: getregistered\n"));
		bzero (mask, sizeof(mask));
		for (s=0; s<NBRD*NCHAN; ++s)
			if (channel [s])
				mask [s/8] |= 1 << (s & 7);
		bcopy (mask, data, sizeof (mask));
		return 0;

#ifndef NETGRAPH
	case SERIAL_GETPROTO:
		CE_DEBUG2 (d, ("ioctl: getproto\n"));
		strcpy ((char*)data, (IFP2SP(d->ifp)->pp_flags & PP_FR) ? "fr" :
			(d->ifp->if_flags & PP_CISCO) ? "cisco" : "ppp");
		return 0;

	case SERIAL_SETPROTO:
		CE_DEBUG2 (d, ("ioctl: setproto\n"));
		/* Only for superuser! */
#if __FreeBSD_version < 500000
		error = suser (p);
#elif __FreeBSD_version < 700000
		error = suser (td);
#else
		error = priv_check (td, PRIV_DRIVER);
#endif
		if (error)
			return error;
#if __FreeBSD_version >= 600034
		if (d->ifp->if_flags & IFF_DRV_RUNNING)
#else
		if (d->ifp->if_flags & IFF_RUNNING)
#endif
			return EBUSY;
		if (! strcmp ("cisco", (char*)data)) {
			IFP2SP(d->ifp)->pp_flags &= ~(PP_FR);
			IFP2SP(d->ifp)->pp_flags |= PP_KEEPALIVE;
			d->ifp->if_flags |= PP_CISCO;
#if PP_FR != 0
		} else if (! strcmp ("fr", (char*)data)) {
			d->ifp->if_flags &= ~(PP_CISCO);
			IFP2SP(d->ifp)->pp_flags |= PP_FR | PP_KEEPALIVE;
#endif
		} else if (! strcmp ("ppp", (char*)data)) {
			IFP2SP(d->ifp)->pp_flags &= ~PP_FR;
			IFP2SP(d->ifp)->pp_flags &= ~PP_KEEPALIVE;
			d->ifp->if_flags &= ~(PP_CISCO);
		} else
			return EINVAL;
		return 0;

	case SERIAL_GETKEEPALIVE:
		CE_DEBUG2 (d, ("ioctl: getkeepalive\n"));
		if ((IFP2SP(d->ifp)->pp_flags & PP_FR) ||
			(d->ifp->if_flags & PP_CISCO))
			return EINVAL;
		*(int*)data = (IFP2SP(d->ifp)->pp_flags & PP_KEEPALIVE) ? 1 : 0;
		return 0;

	case SERIAL_SETKEEPALIVE:
		CE_DEBUG2 (d, ("ioctl: setkeepalive\n"));
		/* Only for superuser! */
#if __FreeBSD_version < 500000
		error = suser (p);
#elif __FreeBSD_version < 700000
		error = suser (td);
#else
		error = priv_check (td, PRIV_DRIVER);
#endif
		if (error)
			return error;
		if ((IFP2SP(d->ifp)->pp_flags & PP_FR) ||
			(d->ifp->if_flags & PP_CISCO))
			return EINVAL;
		s = splimp ();
		CE_LOCK (bd);
		if (*(int*)data)
			IFP2SP(d->ifp)->pp_flags |= PP_KEEPALIVE;
		else
			IFP2SP(d->ifp)->pp_flags &= ~PP_KEEPALIVE;
		CE_UNLOCK (bd);
		splx (s);
		return 0;
#endif /*NETGRAPH*/

	case SERIAL_GETMODE:
		CE_DEBUG2 (d, ("ioctl: getmode\n"));
		*(int*)data = SERIAL_HDLC;
		return 0;

	case SERIAL_SETMODE:
		/* Only for superuser! */
#if __FreeBSD_version < 500000
		error = suser (p);
#elif __FreeBSD_version < 700000
		error = suser (td);
#else
		error = priv_check (td, PRIV_DRIVER);
#endif
		if (error)
			return error;
		if (*(int*)data != SERIAL_HDLC)
			return EINVAL;
		return 0;

	case SERIAL_GETCFG:
		CE_DEBUG2 (d, ("ioctl: getcfg\n"));
		*(char*)data = 'c';
		return 0;

	case SERIAL_SETCFG:
		CE_DEBUG2 (d, ("ioctl: setcfg\n"));
#if __FreeBSD_version < 500000
		error = suser (p);
#elif __FreeBSD_version < 700000
		error = suser (td);
#else
		error = priv_check (td, PRIV_DRIVER);
#endif
		if (error)
			return error;
		if (*((char*)data) != 'c')
			return EINVAL;
		return 0;

	case SERIAL_GETSTAT:
		CE_DEBUG2 (d, ("ioctl: getstat\n"));
		st = (struct serial_statistics*) data;
		st->rintr  = c->rintr;
		st->tintr  = c->tintr;
		st->mintr  = 0;
		st->ibytes = c->ibytes;
		st->ipkts  = c->ipkts;
		st->obytes = c->obytes;
		st->opkts  = c->opkts;
		st->ierrs  = c->overrun + c->frame + c->crc;
		st->oerrs  = c->underrun;
		return 0;

	case SERIAL_GETESTAT:
		CE_DEBUG2 (d, ("ioctl: getestat\n"));
		if (c->type != T_E1)
			return EINVAL;
		opte1 = (struct e1_statistics*) data;

		opte1->status	   = 0;
		if (c->status & ESTS_NOALARM)
			opte1->status |= E1_NOALARM;
		if (c->status & ESTS_LOS)
			opte1->status |= E1_LOS;
		if (c->status & ESTS_LOF)
			opte1->status |= E1_LOF;
		if (c->status & ESTS_AIS)
			opte1->status |= E1_AIS;
		if (c->status & ESTS_LOMF)
			opte1->status |= E1_LOMF;
		if (c->status & ESTS_AIS16)
			opte1->status |= E1_AIS16;
		if (c->status & ESTS_FARLOF)
			opte1->status |= E1_FARLOF;
		if (c->status & ESTS_FARLOMF)
			opte1->status |= E1_FARLOMF;
		if (c->status & ESTS_TSTREQ)
			opte1->status |= E1_TSTREQ;
		if (c->status & ESTS_TSTERR)
			opte1->status |= E1_TSTERR;

		opte1->cursec	    = c->cursec;
		opte1->totsec	    = c->totsec + c->cursec;

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

		opte1->total.bpv    = c->total.bpv   + c->currnt.bpv;
		opte1->total.fse    = c->total.fse   + c->currnt.fse;
		opte1->total.crce   = c->total.crce  + c->currnt.crce;
		opte1->total.rcrce  = c->total.rcrce + c->currnt.rcrce;
		opte1->total.uas    = c->total.uas   + c->currnt.uas;
		opte1->total.les    = c->total.les   + c->currnt.les;
		opte1->total.es	    = c->total.es    + c->currnt.es;
		opte1->total.bes    = c->total.bes   + c->currnt.bes;
		opte1->total.ses    = c->total.ses   + c->currnt.ses;
		opte1->total.oofs   = c->total.oofs  + c->currnt.oofs;
		opte1->total.css    = c->total.css   + c->currnt.css;
		opte1->total.dm	    = c->total.dm    + c->currnt.dm;
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
		CE_DEBUG2 (d, ("ioctl: clrstat\n"));
		/* Only for superuser! */
#if __FreeBSD_version < 500000
		error = suser (p);
#elif __FreeBSD_version < 700000
		error = suser (td);
#else
		error = priv_check (td, PRIV_DRIVER);
#endif
		if (error)
			return error;
		c->rintr    = 0;
		c->tintr    = 0;
		c->ibytes   = 0;
		c->obytes   = 0;
		c->ipkts    = 0;
		c->opkts    = 0;
		c->overrun  = 0;
		c->frame    = 0;
		c->crc	    = 0;
		c->underrun = 0;
		bzero (&c->currnt, sizeof (c->currnt));
		bzero (&c->total, sizeof (c->total));
		bzero (c->interval, sizeof (c->interval));
		return 0;

	case SERIAL_GETLOOP:
		CE_DEBUG2 (d, ("ioctl: getloop\n"));
		if (c->type != T_E1)
			return EINVAL;
		*(int*)data = c->lloop;
		return 0;

	case SERIAL_SETLOOP:
		CE_DEBUG2 (d, ("ioctl: setloop\n"));
		if (c->type != T_E1)
			return EINVAL;
		/* Only for superuser! */
#if __FreeBSD_version < 500000
		error = suser (p);
#elif __FreeBSD_version < 700000
		error = suser (td);
#else
		error = priv_check (td, PRIV_DRIVER);
#endif
		if (error)
			return error;
		s = splimp ();
		CE_LOCK (bd);
		ce_set_lloop (c, *(int*)data);
		CE_UNLOCK (bd);
		splx (s);
		return 0;

	case SERIAL_GETRLOOP:
		CE_DEBUG2 (d, ("ioctl: getrloop\n"));
		if (c->type != T_E1)
			return EINVAL;
		*(int*)data = c->rloop;
		return 0;

	case SERIAL_SETRLOOP:
		CE_DEBUG2 (d, ("ioctl: setloop\n"));
		if (c->type != T_E1)
			return EINVAL;
		/* Only for superuser! */
#if __FreeBSD_version < 500000
		error = suser (p);
#elif __FreeBSD_version < 700000
		error = suser (td);
#else
		error = priv_check (td, PRIV_DRIVER);
#endif
		if (error)
			return error;
		s = splimp ();
		CE_LOCK (bd);
		ce_set_rloop (c, *(int*)data);
		CE_UNLOCK (bd);
		splx (s);
		return 0;

	case SERIAL_GETDEBUG:
		CE_DEBUG2 (d, ("ioctl: getdebug\n"));
		*(int*)data = d->chan->debug;
		return 0;

	case SERIAL_SETDEBUG:
		CE_DEBUG2 (d, ("ioctl: setdebug\n"));
		/* Only for superuser! */
#if __FreeBSD_version < 500000
		error = suser (p);
#elif __FreeBSD_version < 700000
		error = suser (td);
#else
		error = priv_check (td, PRIV_DRIVER);
#endif
		if (error)
			return error;
#ifndef	NETGRAPH
		/*
		 * The debug_shadow is always greater than zero for logic 
		 * simplicity.  For switching debug off the IFF_DEBUG is
		 * responsible.
		 */
		d->chan->debug_shadow = (*(int*)data) ? (*(int*)data) : 1;
		if (d->ifp->if_flags & IFF_DEBUG)
			d->chan->debug = d->chan->debug_shadow;
#else
		d->chan->debug = *(int*)data;
#endif
		return 0;

	case SERIAL_GETBAUD:
		CE_DEBUG2 (d, ("ioctl: getbaud\n"));
		*(long*)data = c->baud;
		return 0;

	case SERIAL_SETBAUD:
		CE_DEBUG2 (d, ("ioctl: setbaud\n"));
		if (c->type != T_E1 || !c->unfram)
			return EINVAL;
		/* Only for superuser! */
#if __FreeBSD_version < 500000
		error = suser (p);
#elif __FreeBSD_version < 700000
		error = suser (td);
#else
		error = priv_check (td, PRIV_DRIVER);
#endif
		if (error)
			return error;
		s = splimp ();
		CE_LOCK (bd);
		ce_set_baud (c, *(long*)data);
		CE_UNLOCK (bd);
		splx (s);
		return 0;

	case SERIAL_GETTIMESLOTS:
		CE_DEBUG2 (d, ("ioctl: gettimeslots\n"));
		if ((c->type != T_E1 || c->unfram) && c->type != T_DATA)
			return EINVAL;
		*(u_long*)data = c->ts;
		return 0;

	case SERIAL_SETTIMESLOTS:
		CE_DEBUG2 (d, ("ioctl: settimeslots\n"));
		/* Only for superuser! */
#if __FreeBSD_version < 500000
		error = suser (p);
#elif __FreeBSD_version < 700000
		error = suser (td);
#else
		error = priv_check (td, PRIV_DRIVER);
#endif
		if (error)
			return error;
		if ((c->type != T_E1 || c->unfram) && c->type != T_DATA)
			return EINVAL;
		s = splimp ();
		CE_LOCK (bd);
		ce_set_ts (c, *(u_long*)data);
		CE_UNLOCK (bd);
		splx (s);
		return 0;

	case SERIAL_GETHIGAIN:
		CE_DEBUG2 (d, ("ioctl: gethigain\n"));
		if (c->type != T_E1)
			return EINVAL;
		*(int*)data = c->higain;
		return 0;

	case SERIAL_SETHIGAIN:
		CE_DEBUG2 (d, ("ioctl: sethigain\n"));
		if (c->type != T_E1)
			return EINVAL;
		/* Only for superuser! */
#if __FreeBSD_version < 500000
		error = suser (p);
#elif __FreeBSD_version < 700000
		error = suser (td);
#else
		error = priv_check (td, PRIV_DRIVER);
#endif
		if (error)
			return error;
		s = splimp ();
		CE_LOCK (bd);
		ce_set_higain (c, *(int*)data);
		CE_UNLOCK (bd);
		splx (s);
		return 0;

	case SERIAL_GETPHONY:
		CE_DEBUG2 (d, ("ioctl: getphony\n"));
		*(int*)data = c->phony;
		return 0;

	case SERIAL_SETPHONY:
		CE_DEBUG2 (d, ("ioctl: setphony\n"));
		/* Only for superuser! */
#if __FreeBSD_version < 500000
		error = suser (p);
#elif __FreeBSD_version < 700000
		error = suser (td);
#else
		error = priv_check (td, PRIV_DRIVER);
#endif
		if (error)
			return error;
		s = splimp ();
		CE_LOCK (bd);
		ce_set_phony (c, *(int*)data);
		CE_UNLOCK (bd);
		splx (s);
		return 0;

	case SERIAL_GETUNFRAM:
		CE_DEBUG2 (d, ("ioctl: getunfram\n"));
		if (c->type != T_E1 || c->num != 0)
			return EINVAL;
		*(int*)data = c->unfram;
		return 0;

	case SERIAL_SETUNFRAM:
		CE_DEBUG2 (d, ("ioctl: setunfram\n"));
		if (c->type != T_E1 || c->num != 0)
			return EINVAL;
		/* Only for superuser! */
#if __FreeBSD_version < 500000
		error = suser (p);
#elif __FreeBSD_version < 700000
		error = suser (td);
#else
		error = priv_check (td, PRIV_DRIVER);
#endif
		if (error)
			return error;
		s = splimp ();
		CE_LOCK (bd);
		ce_set_unfram (c, *(int*)data);
		CE_UNLOCK (bd);
		splx (s);
		return 0;

	case SERIAL_GETSCRAMBLER:
		CE_DEBUG2 (d, ("ioctl: getscrambler\n"));
		if (!c->unfram)
			return EINVAL;
		*(int*)data = c->scrambler;
		return 0;

	case SERIAL_SETSCRAMBLER:
		CE_DEBUG2 (d, ("ioctl: setscrambler\n"));
		/* Only for superuser! */
#if __FreeBSD_version < 500000
		error = suser (p);
#elif __FreeBSD_version < 700000
		error = suser (td);
#else
		error = priv_check (td, PRIV_DRIVER);
#endif
		if (error)
			return error;
		if (!c->unfram)
			return EINVAL;
		s = splimp ();
		CE_LOCK (bd);
		ce_set_scrambler (c, *(int*)data);
		CE_UNLOCK (bd);
		splx (s);
		return 0;

	case SERIAL_GETMONITOR:
		CE_DEBUG2 (d, ("ioctl: getmonitor\n"));
		if (c->type != T_E1)
			return EINVAL;
		*(int*)data = c->monitor;
		return 0;

	case SERIAL_SETMONITOR:
		CE_DEBUG2 (d, ("ioctl: setmonitor\n"));
		/* Only for superuser! */
#if __FreeBSD_version < 500000
		error = suser (p);
#elif __FreeBSD_version < 700000
		error = suser (td);
#else
		error = priv_check (td, PRIV_DRIVER);
#endif
		if (error)
			return error;
		if (c->type != T_E1)
			return EINVAL;
		s = splimp ();
		CE_LOCK (bd);
		ce_set_monitor (c, *(int*)data);
		CE_UNLOCK (bd);
		splx (s);
		return 0;

	case SERIAL_GETUSE16:
		CE_DEBUG2 (d, ("ioctl: getuse16\n"));
		if (c->type != T_E1 || c->unfram)
			return EINVAL;
		*(int*)data = c->use16;
		return 0;

	case SERIAL_SETUSE16:
		CE_DEBUG2 (d, ("ioctl: setuse16\n"));
		/* Only for superuser! */
#if __FreeBSD_version < 500000
		error = suser (p);
#elif __FreeBSD_version < 700000
		error = suser (td);
#else
		error = priv_check (td, PRIV_DRIVER);
#endif
		if (error)
			return error;
		if (c->type != T_E1)
			return EINVAL;
		s = splimp ();
		CE_LOCK (bd);
		ce_set_use16 (c, *(int*)data);
		CE_UNLOCK (bd);
		splx (s);
		return 0;

	case SERIAL_GETCRC4:
		CE_DEBUG2 (d, ("ioctl: getcrc4\n"));
		if (c->type != T_E1 || c->unfram)
			return EINVAL;
		*(int*)data = c->crc4;
		return 0;

	case SERIAL_SETCRC4:
		CE_DEBUG2 (d, ("ioctl: setcrc4\n"));
		/* Only for superuser! */
#if __FreeBSD_version < 500000
		error = suser (p);
#elif __FreeBSD_version < 700000
		error = suser (td);
#else
		error = priv_check (td, PRIV_DRIVER);
#endif
		if (error)
			return error;
		if (c->type != T_E1 || c->unfram)
			return EINVAL;
		s = splimp ();
		CE_LOCK (bd);
		ce_set_crc4 (c, *(int*)data);
		CE_UNLOCK (bd);
		splx (s);
		return 0;

	case SERIAL_GETCLK:
		CE_DEBUG2 (d, ("ioctl: getclk\n"));
		if (c->type != T_E1)
			return EINVAL;
		switch (c->gsyn) {
		default:	*(int*)data = E1CLK_INTERNAL;		break;
		case GSYN_RCV:	*(int*)data = E1CLK_RECEIVE;		break;
		case GSYN_RCV0:	*(int*)data = E1CLK_RECEIVE_CHAN0;	break;
		case GSYN_RCV1:	*(int*)data = E1CLK_RECEIVE_CHAN1;	break;
		}
		return 0;

	case SERIAL_SETCLK:
		CE_DEBUG2 (d, ("ioctl: setclk\n"));
		/* Only for superuser! */
#if __FreeBSD_version < 500000
		error = suser (p);
#elif __FreeBSD_version < 700000
		error = suser (td);
#else
		error = priv_check (td, PRIV_DRIVER);
#endif
		if (error)
			return error;
		if (c->type != T_E1)
			return EINVAL;
		s = splimp ();
		CE_LOCK (bd);
		switch (*(int*)data) {
		default:		  ce_set_gsyn (c, GSYN_INT);  break;
		case E1CLK_RECEIVE:	  ce_set_gsyn (c, GSYN_RCV);  break;
		case E1CLK_RECEIVE_CHAN0: ce_set_gsyn (c, GSYN_RCV0); break;
		case E1CLK_RECEIVE_CHAN1: ce_set_gsyn (c, GSYN_RCV1); break;
		}
		CE_UNLOCK (bd);
		splx (s);
		return 0;

#if 0
	case SERIAL_RESET:
		CE_DEBUG2 (d, ("ioctl: reset\n"));
		/* Only for superuser! */
#if __FreeBSD_version < 500000
		error = suser (p);
#elif __FreeBSD_version < 700000
		error = suser (td);
#else
		error = priv_check (td, PRIV_DRIVER);
#endif
		if (error)
			return error;
		s = splimp ();
		CE_LOCK (bd);
/*		ce_reset (c->board, 0, 0);*/
		CE_UNLOCK (bd);
		splx (s);
		return 0;

	case SERIAL_HARDRESET:
		CE_DEBUG2 (d, ("ioctl: hardreset\n"));
		/* Only for superuser! */
#if __FreeBSD_version < 500000
		error = suser (p);
#elif __FreeBSD_version < 700000
		error = suser (td);
#else
		error = priv_check (td, PRIV_DRIVER);
#endif
		if (error)
			return error;
		s = splimp ();
		CE_LOCK (bd);
		/* hard_reset (c->board); */
		CE_UNLOCK (bd);
		splx (s);
		return 0;
#endif

	case SERIAL_GETCABLE:
		CE_DEBUG2 (d, ("ioctl: getcable\n"));
		if (c->type != T_E1)
			return EINVAL;
		s = splimp ();
		CE_LOCK (bd);
		*(int*)data = CABLE_TP;
		CE_UNLOCK (bd);
		splx (s);
		return 0;

	case SERIAL_GETDIR:
		CE_DEBUG2 (d, ("ioctl: getdir\n"));
		if (c->type != T_E1 && c->type != T_DATA)
			return EINVAL;
		*(int*)data = c->dir;
		return 0;

	case SERIAL_SETDIR:
		CE_DEBUG2 (d, ("ioctl: setdir\n"));
		/* Only for superuser! */
#if __FreeBSD_version < 500000
		error = suser (p);
#elif __FreeBSD_version < 700000
		error = suser (td);
#else
		error = priv_check (td, PRIV_DRIVER);
#endif
		if (error)
			return error;
		s = splimp ();
		CE_LOCK (bd);
		ce_set_dir (c, *(int*)data);
		CE_UNLOCK (bd);
		splx (s);
		return 0;

	case TIOCSDTR:		/* Set DTR */
		s = splimp ();
		CE_LOCK (bd);
		ce_set_dtr (c, 1);
		CE_UNLOCK (bd);
		splx (s);
		return 0;

	case TIOCCDTR:		/* Clear DTR */
		s = splimp ();
		CE_LOCK (bd);
		ce_set_dtr (c, 0);
		CE_UNLOCK (bd);
		splx (s);
		return 0;

	case TIOCMSET:		/* Set DTR/RTS */
		s = splimp ();
		CE_LOCK (bd);
		ce_set_dtr (c, (*(int*)data & TIOCM_DTR) ? 1 : 0);
		ce_set_rts (c, (*(int*)data & TIOCM_RTS) ? 1 : 0);
		CE_UNLOCK (bd);
		splx (s);
		return 0;

	case TIOCMBIS:		/* Add DTR/RTS */
		s = splimp ();
		CE_LOCK (bd);
		if (*(int*)data & TIOCM_DTR) ce_set_dtr (c, 1);
		if (*(int*)data & TIOCM_RTS) ce_set_rts (c, 1);
		CE_UNLOCK (bd);
		splx (s);
		return 0;

	case TIOCMBIC:		/* Clear DTR/RTS */
		s = splimp ();
		CE_LOCK (bd);
		if (*(int*)data & TIOCM_DTR) ce_set_dtr (c, 0);
		if (*(int*)data & TIOCM_RTS) ce_set_rts (c, 0);
		CE_UNLOCK (bd);
		splx (s);
		return 0;

	case TIOCMGET:		/* Get modem status */
		*(int*)data = ce_modem_status (c);
		return 0;
	}
	return ENOTTY;
}

#ifdef NETGRAPH
#if __FreeBSD_version >= 500000
static int ng_ce_constructor (node_p node)
{
	drv_t *d = NG_NODE_PRIVATE (node);
#else
static int ng_ce_constructor (node_p *node)
{
	drv_t *d = (*node)->private;
#endif
	CE_DEBUG (d, ("Constructor\n"));
	return EINVAL;
}

static int ng_ce_newhook (node_p node, hook_p hook, const char *name)
{
	int s;
#if __FreeBSD_version >= 500000
	drv_t *d = NG_NODE_PRIVATE (node);
#else
	drv_t *d = node->private;
#endif
	bdrv_t *bd = d->board->sys;

	CE_DEBUG (d, ("Newhook\n"));
	/* Attach debug hook */
	if (strcmp (name, NG_CE_HOOK_DEBUG) == 0) {
#if __FreeBSD_version >= 500000
		NG_HOOK_SET_PRIVATE (hook, NULL);
#else
		hook->private = 0;
#endif
		d->debug_hook = hook;
		return 0;
	}

	/* Check for raw hook */
	if (strcmp (name, NG_CE_HOOK_RAW) != 0)
		return EINVAL;

#if __FreeBSD_version >= 500000
	NG_HOOK_SET_PRIVATE (hook, d);
#else
	hook->private = d;
#endif
	d->hook = hook;
	s = splimp ();
	CE_LOCK (bd);
	ce_up (d);
	CE_UNLOCK (bd);
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

static int print_modems (char *s, ce_chan_t *c, int need_header)
{
	int status = ce_modem_status (c);
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

static int print_stats (char *s, ce_chan_t *c, int need_header)
{
	int length = 0;

	if (need_header)
		length += sprintf (s + length, "  Rintr   Tintr   Mintr   Ibytes   Ipkts   Ierrs   Obytes   Opkts   Oerrs\n");
	length += sprintf (s + length, "%7ld %7ld %7ld %8lu %7ld %7ld %8lu %7ld %7ld\n",
		c->rintr, c->tintr, 0l, (unsigned long) c->ibytes,
		c->ipkts, c->overrun + c->frame + c->crc,
		(unsigned long) c->obytes, c->opkts, c->underrun);
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

	if	(n >= 1000000) n = (n+500) / 1000 * 1000;
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

static int print_e1_stats (char *s, ce_chan_t *c)
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

static int print_chan (char *s, ce_chan_t *c)
{
	drv_t *d = c->sys;
	int length = 0;

	length += sprintf (s + length, "ce%d", c->board->num * NCHAN + c->num);
	if (d->chan->debug)
		length += sprintf (s + length, " debug=%d", d->chan->debug);

	if (c->board->mux) {
		length += sprintf (s + length, " cfg=C");
	} else {
		length += sprintf (s + length, " cfg=A");
	}

	if (c->baud)
		length += sprintf (s + length, " %ld", c->baud);
	else
		length += sprintf (s + length, " extclock");

	if (c->type == T_E1)
		switch (c->gsyn) {
		case GSYN_INT   : length += sprintf (s + length, " syn=int");     break;
		case GSYN_RCV   : length += sprintf (s + length, " syn=rcv");     break;
		case GSYN_RCV0  : length += sprintf (s + length, " syn=rcv0");    break;
		case GSYN_RCV1  : length += sprintf (s + length, " syn=rcv1");    break;
		}
	if (c->type == T_E1)
		length += sprintf (s + length, " higain=%s", c->higain ? "on" : "off");

	length += sprintf (s + length, " loop=%s", c->lloop ? "on" : "off");

	if (c->type == T_E1)
		length += sprintf (s + length, " ts=%s", format_timeslots (c->ts));
	length += sprintf (s + length, "\n");
	return length;
}

#if __FreeBSD_version >= 500000
static int ng_ce_rcvmsg (node_p node, item_p item, hook_p lasthook)
{
	drv_t *d = NG_NODE_PRIVATE (node);
	struct ng_mesg *msg;
#else
static int ng_ce_rcvmsg (node_p node, struct ng_mesg *msg,
	const char *retaddr, struct ng_mesg **rptr)
{
	drv_t *d = node->private;
#endif
	struct ng_mesg *resp = NULL;
	int error = 0;

	CE_DEBUG (d, ("Rcvmsg\n"));
#if __FreeBSD_version >= 500000
	NGI_GET_MSG (item, msg);
#endif
	switch (msg->header.typecookie) {
	default:
		error = EINVAL;
		break;

	case NGM_CE_COOKIE:
		printf ("Not implemented yet\n");
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

#if __FreeBSD_version >= 500000	
			NG_MKRESPONSE (resp, msg, dl, M_NOWAIT);
			if (! resp) {
				error = ENOMEM;
				break;
			}
#else
			resp = malloc (M_NETGRAPH, M_NOWAIT);
			if (! resp) {
				error = ENOMEM;
				break;
			}
			bzero (resp, dl);
#endif
			s = (resp)->data;
			if (d) {
			l += print_chan (s + l, d->chan);
			l += print_stats (s + l, d->chan, 1);
			l += print_modems (s + l, d->chan, 1);
			l += print_e1_stats (s + l, d->chan);
			} else
				l += sprintf (s + l, "Error: node not connect to channel");
#if __FreeBSD_version < 500000
			(resp)->header.version = NG_VERSION;
			(resp)->header.arglen = strlen (s) + 1;
			(resp)->header.token = msg->header.token;
			(resp)->header.typecookie = NGM_CE_COOKIE;
			(resp)->header.cmd = msg->header.cmd;
#endif
			strncpy ((resp)->header.cmdstr, "status", NG_CMDSTRSIZ);
			}
			break;
		}
		break;
	}
#if __FreeBSD_version >= 500000
	NG_RESPOND_MSG (error, node, item, resp);
	NG_FREE_MSG (msg);
#else
	*rptr = resp;
	free (msg, M_NETGRAPH);
#endif
	return error;
}

#if __FreeBSD_version >= 500000
static int ng_ce_rcvdata (hook_p hook, item_p item)
{
	drv_t *d = NG_NODE_PRIVATE (NG_HOOK_NODE(hook));
	struct mbuf *m;
#if __FreeBSD_version < 502120
	meta_p meta;
#else
	struct ng_tag_prio *ptag;
#endif
#else
static int ng_ce_rcvdata (hook_p hook, struct mbuf *m, meta_p meta)
{
	drv_t *d = hook->node->private;
#endif
	bdrv_t *bd = d->board->sys;
	struct ifqueue *q;
	int s;

	CE_DEBUG2 (d, ("Rcvdata\n"));
#if __FreeBSD_version >= 500000
	NGI_GET_M (item, m);
#if __FreeBSD_version < 502120
	NGI_GET_META (item, meta);
#endif
	NG_FREE_ITEM (item);
	if (! NG_HOOK_PRIVATE (hook) || ! d) {
		NG_FREE_M (m);
#if __FreeBSD_version < 502120
		NG_FREE_META (meta);
#endif
#else
	if (! hook->private || ! d) {
		NG_FREE_DATA (m,meta);
#endif
		return ENETDOWN;
	}

#if __FreeBSD_version >= 502120
	/* Check for high priority data */
	if ((ptag = (struct ng_tag_prio *)m_tag_locate(m, NGM_GENERIC_COOKIE,
	    NG_TAG_PRIO, NULL)) != NULL && (ptag->priority > NG_PRIO_CUTOFF) )
		q = &d->hi_queue;
	else
		q = &d->queue;
#else
	q = (meta && meta->priority > 0) ? &d->hi_queue : &d->queue;
#endif

	s = splimp ();
	CE_LOCK (bd);
#if __FreeBSD_version >= 500000
	IF_LOCK (q);
	if (_IF_QFULL (q)) {
		IF_UNLOCK (q);
		CE_UNLOCK (bd);
		splx (s);
		NG_FREE_M (m);
#if __FreeBSD_version < 502120
		NG_FREE_META (meta);
#endif
		return ENOBUFS;
	}
	_IF_ENQUEUE (q, m);
	IF_UNLOCK (q);
#else
	if (IF_QFULL (q)) {
		IF_DROP (q);
		CE_UNLOCK (bd);
		splx (s);
		NG_FREE_DATA (m, meta);
		return ENOBUFS;
	}
	IF_ENQUEUE (q, m);
#endif
	ce_start (d);
	CE_UNLOCK (bd);
	splx (s);
	return 0;
}

static int ng_ce_rmnode (node_p node)
{
#if __FreeBSD_version >= 500000
	drv_t *d = NG_NODE_PRIVATE (node);

	CE_DEBUG (d, ("Rmnode\n"));
	if (d && d->running) {
		bdrv_t *bd = d->board->sys;
		int s = splimp ();
		CE_LOCK (bd);
		ce_down (d);
		CE_UNLOCK (bd);
		splx (s);
	}
#ifdef	KLD_MODULE
#if __FreeBSD_version >= 502120
	if (node->nd_flags & NGF_REALLY_DIE) {
#else
	if (node->nd_flags & NG_REALLY_DIE) {
#endif
		NG_NODE_SET_PRIVATE (node, NULL);
		NG_NODE_UNREF (node);
	}
#if __FreeBSD_version >= 502120
	NG_NODE_REVIVE(node);		/* Persistent node */
#else
	node->nd_flags &= ~NG_INVALID;
#endif
#endif
#else /* __FreeBSD_version < 500000 */
	drv_t *d = node->private;

	if (d && d->running) {
		bdrv_t *bd = d->board->sys;
		int s = splimp ();
		CE_LOCK (bd);
		ce_down (d);
		CE_UNLOCK (bd);
		splx (s);
	}

	node->flags |= NG_INVALID;
	ng_cutlinks (node);
#ifdef	KLD_MODULE
	ng_unname (node);
	ng_unref (node);
#endif
#endif
	return 0;
}

static int ng_ce_connect (hook_p hook)
{
#if __FreeBSD_version >= 500000
	drv_t *d = NG_NODE_PRIVATE (NG_HOOK_NODE (hook));
#else
	drv_t *d = hook->node->private;
#endif

	if (d) {
		CE_DEBUG (d, ("Connect\n"));
		callout_reset (&d->timeout_handle, hz, ce_watchdog_timer, d);
	}
	
	return 0;
}

static int ng_ce_disconnect (hook_p hook)
{
#if __FreeBSD_version >= 500000
	drv_t *d = NG_NODE_PRIVATE (NG_HOOK_NODE (hook));
#else
	drv_t *d = hook->node->private;
#endif

	if (d) {
		CE_DEBUG (d, ("Disconnect\n"));
#if __FreeBSD_version >= 500000
		if (NG_HOOK_PRIVATE (hook))
#else
		if (hook->private)
#endif
		{
			bdrv_t *bd = d->board->sys;
			int s = splimp ();
			CE_LOCK (bd);
			ce_down (d);
			CE_UNLOCK (bd);
			splx (s);
		}
		/* If we were wait it than it reasserted now, just stop it. */
		if (!callout_drain (&d->timeout_handle))
			callout_stop (&d->timeout_handle);
	}
	return 0;
}
#endif

static int ce_modevent (module_t mod, int type, void *unused)
{
#if __FreeBSD_version < 500000
	dev_t dev;
	struct cdevsw *cdsw;
#endif
	static int load_count = 0;

#if __FreeBSD_version < 500000
	dev = makedev (CDEV_MAJOR, 0);
#endif

	switch (type) {
	case MOD_LOAD:
#if __FreeBSD_version < 500000
		if (dev != NODEV &&
		    (cdsw = devsw (dev)) &&
		    cdsw->d_maj == CDEV_MAJOR) {
			printf ("Tau32-PCI driver is already in system\n");
			return (ENXIO);
		}
#endif
#if __FreeBSD_version >= 500000 && defined NETGRAPH
		if (ng_newtype (&typestruct))
			printf ("Failed to register ng_ce\n");
#endif
		++load_count;
#if __FreeBSD_version <= 500000
		cdevsw_add (&ce_cdevsw);
#endif
#if __FreeBSD_version >= 500000
		callout_init (&timeout_handle, 1);
#else
		callout_init (&timeout_handle);
#endif
		callout_reset (&timeout_handle, hz*5, ce_timeout, 0);
		break;
	case MOD_UNLOAD:
		if (load_count == 1) {
			printf ("Removing device entry for Tau32-PCI\n");
#if __FreeBSD_version <= 500000
			cdevsw_remove (&ce_cdevsw);
#endif
#if __FreeBSD_version >= 500000 && defined NETGRAPH
			ng_rmtype (&typestruct);
#endif			
		}
		/* If we were wait it than it reasserted now, just stop it.
		 * Actually we shouldn't get this condition. But code could be
		 * changed in the future, so just be a litle paranoid.
		 */
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
#if __FreeBSD_version >= 502100
static struct ng_type typestruct = {
	.version	= NG_ABI_VERSION,
	.name		= NG_CE_NODE_TYPE,
	.constructor	= ng_ce_constructor,
	.rcvmsg		= ng_ce_rcvmsg,
	.shutdown	= ng_ce_rmnode,
	.newhook	= ng_ce_newhook,
	.connect	= ng_ce_connect,
	.rcvdata	= ng_ce_rcvdata,
	.disconnect	= ng_ce_disconnect,
};
#else /* __FreeBSD_version < 502100 */
static struct ng_type typestruct = {
#if __FreeBSD_version >= 500000
	NG_ABI_VERSION,
#else
	NG_VERSION,
#endif
	NG_CE_NODE_TYPE,
	ce_modevent,
	ng_ce_constructor,
	ng_ce_rcvmsg,
	ng_ce_rmnode,
	ng_ce_newhook,
	NULL,
	ng_ce_connect,
	ng_ce_rcvdata,
#if __FreeBSD_version < 500000
	NULL,
#endif
	ng_ce_disconnect,
	NULL
};
#endif /* __FreeBSD_version < 502100 */

#endif /*NETGRAPH*/

#if __FreeBSD_version >= 500000
#ifdef NETGRAPH
MODULE_DEPEND (ng_ce, netgraph, NG_ABI_VERSION, NG_ABI_VERSION, NG_ABI_VERSION);
#else
MODULE_DEPEND (ce, sppp, 1, 1, 1);
#endif
#ifdef KLD_MODULE
DRIVER_MODULE (cemod, pci, ce_driver, ce_devclass, ce_modevent, NULL);
#else
DRIVER_MODULE (ce, pci, ce_driver, ce_devclass, ce_modevent, NULL);
#endif
#else /* if __FreeBSD_version < 500000*/
#ifdef NETGRAPH
DRIVER_MODULE (ce, pci, ce_driver, ce_devclass, ng_mod_event, &typestruct);
#else
DRIVER_MODULE (ce, pci, ce_driver, ce_devclass, ce_modevent, NULL);
#endif
#endif /* __FreeBSD_version < 500000 */
#endif /* NPCI */
