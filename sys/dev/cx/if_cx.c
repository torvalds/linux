/*-
 * Cronyx-Sigma adapter driver for FreeBSD.
 * Supports PPP/HDLC and Cisco/HDLC protocol in synchronous mode,
 * and asynchronous channels with full modem control.
 * Keepalive protocol implemented in both Cisco and PPP modes.
 *
 * Copyright (C) 1994-2002 Cronyx Engineering.
 * Author: Serge Vakulenko, <vak@cronyx.ru>
 *
 * Copyright (C) 1999-2004 Cronyx Engineering.
 * Rewritten on DDK, ported to NETGRAPH, rewritten for FreeBSD 3.x-5.x by
 * Kurakin Roman, <rik@cronyx.ru>
 *
 * This software is distributed with NO WARRANTIES, not even the implied
 * warranties for MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 *
 * Authors grant any other persons or organisations a permission to use,
 * modify and redistribute this software in source and binary forms,
 * as long as this message is kept with the software, all derivative
 * works or modified versions.
 *
 * Cronyx Id: if_cx.c,v 1.1.2.34 2004/06/23 17:09:13 rik Exp $
 */

#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");

#include <sys/param.h>

#include <sys/systm.h>
#include <sys/kernel.h>
#include <sys/module.h>
#include <sys/priv.h>
#include <sys/proc.h>
#include <sys/mbuf.h>
#include <sys/sockio.h>
#include <sys/malloc.h>
#include <sys/socket.h>
#include <sys/sysctl.h>
#include <sys/conf.h>
#include <sys/errno.h>
#include <sys/serial.h>
#include <sys/tty.h>
#include <sys/bus.h>
#include <machine/bus.h>
#include <sys/rman.h>
#include <isa/isavar.h>
#include <sys/fcntl.h>
#include <sys/interrupt.h>
#include <vm/vm.h>
#include <vm/pmap.h>
#include <net/if.h>
#include <net/if_var.h>
#include <machine/cpufunc.h>
#include <machine/cserial.h>
#include <machine/resource.h>
#include <dev/cx/machdep.h>
#include <dev/cx/cxddk.h>
#include <dev/cx/cronyxfw.h>
#include "opt_ng_cronyx.h"
#ifdef NETGRAPH_CRONYX
#   include "opt_netgraph.h"
#   include <netgraph/ng_message.h>
#   include <netgraph/netgraph.h>
#   include <dev/cx/ng_cx.h>
#else
#   include <net/if_types.h>
#   include <net/if_sppp.h>
#   define PP_CISCO IFF_LINK2
#   include <net/bpf.h>
#endif

#define NCX	1

/* If we don't have Cronyx's sppp version, we don't have fr support via sppp */
#ifndef PP_FR
#define PP_FR 0
#endif

#define CX_DEBUG(d,s)	({if (d->chan->debug) {\
				printf ("%s: ", d->name); printf s;}})
#define CX_DEBUG2(d,s)	({if (d->chan->debug>1) {\
				printf ("%s: ", d->name); printf s;}})

#define CX_LOCK_NAME	"cxX"

#define CX_LOCK(_bd)		mtx_lock (&(_bd)->cx_mtx)
#define CX_UNLOCK(_bd)		mtx_unlock (&(_bd)->cx_mtx)
#define CX_LOCK_ASSERT(_bd)	mtx_assert (&(_bd)->cx_mtx, MA_OWNED)

typedef struct _async_q {
	int beg;
	int end;
	#define BF_SZ 14400
	int buf[BF_SZ+1];
} async_q;

#define AQ_GSZ(q)	((BF_SZ + (q)->end - (q)->beg)%BF_SZ)
#define AQ_PUSH(q,c)	{*((q)->buf + (q)->end) = c;\
			(q)->end = ((q)->end + 1)%BF_SZ;}
#define AQ_POP(q,c)	{c = *((q)->buf + (q)->beg);\
			(q)->beg = ((q)->beg + 1)%BF_SZ;}

static void cx_identify		__P((driver_t *, device_t));
static int cx_probe		__P((device_t));
static int cx_attach		__P((device_t));
static int cx_detach		__P((device_t));
static t_open_t			cx_topen;
static t_modem_t		cx_tmodem;
static t_close_t		cx_tclose;

static device_method_t cx_isa_methods [] = {
	DEVMETHOD(device_identify,	cx_identify),
	DEVMETHOD(device_probe,		cx_probe),
	DEVMETHOD(device_attach,	cx_attach),
	DEVMETHOD(device_detach,	cx_detach),

	DEVMETHOD_END
};

typedef struct _cx_dma_mem_t {
	unsigned long	phys;
	void		*virt;
	size_t		size;
	bus_dma_tag_t	dmat;
	bus_dmamap_t	mapp;
} cx_dma_mem_t;

typedef struct _drv_t {
	char name [8];
	cx_chan_t *chan;
	cx_board_t *board;
	cx_dma_mem_t dmamem;
	struct tty *tty;
	struct callout dcd_timeout_handle;
	unsigned callout;
	unsigned lock;
	int open_dev;
	int cd;
	int running;
#ifdef NETGRAPH
	char	nodename [NG_NODESIZ];
	hook_p	hook;
	hook_p	debug_hook;
	node_p	node;
	struct	ifqueue lo_queue;
	struct	ifqueue hi_queue;
#else
	struct	ifqueue queue;
	struct	ifnet *ifp;
#endif
	short	timeout;
	struct	callout timeout_handle;
	struct	cdev *devt;
	async_q	aqueue;
#define CX_READ 1
#define CX_WRITE 2
	int intr_action;
	short atimeout;
} drv_t;

typedef struct _bdrv_t {
	cx_board_t	*board;
	struct resource	*base_res;
	struct resource	*drq_res;
	struct resource	*irq_res;
	int		base_rid;
	int		drq_rid;
	int		irq_rid;
	void		*intrhand;
	drv_t		channel [NCHAN];
	struct mtx	cx_mtx;
} bdrv_t;

static driver_t cx_isa_driver = {
	"cx",
	cx_isa_methods,
	sizeof (bdrv_t),
};

static devclass_t cx_devclass;

extern long csigma_fw_len;
extern const char *csigma_fw_version;
extern const char *csigma_fw_date;
extern const char *csigma_fw_copyright;
extern const cr_dat_tst_t csigma_fw_tvec[];
extern const u_char csigma_fw_data[];
static void cx_oproc (struct tty *tp);
static int cx_param (struct tty *tp, struct termios *t);
static void cx_stop (struct tty *tp, int flag);
static void cx_receive (cx_chan_t *c, char *data, int len);
static void cx_transmit (cx_chan_t *c, void *attachment, int len);
static void cx_error (cx_chan_t *c, int data);
static void cx_modem (cx_chan_t *c);
static void cx_up (drv_t *d);
static void cx_start (drv_t *d);
static void cx_softintr (void *);
static void *cx_fast_ih;
static void cx_down (drv_t *d);
static void cx_watchdog (drv_t *d);
static void cx_watchdog_timer (void *arg);
static void cx_carrier (void *arg);

#ifdef NETGRAPH
extern struct ng_type typestruct;
#else
static void cx_ifstart (struct ifnet *ifp);
static void cx_tlf (struct sppp *sp);
static void cx_tls (struct sppp *sp);
static int cx_sioctl (struct ifnet *ifp, u_long cmd, caddr_t data);
static void cx_initialize (void *softc);
#endif

static cx_board_t *adapter [NCX];
static drv_t *channel [NCX*NCHAN];
static struct callout led_timo [NCX];
static struct callout timeout_handle;

static int cx_open (struct cdev *dev, int flag, int mode, struct thread *td);
static int cx_close (struct cdev *dev, int flag, int mode, struct thread *td);
static int cx_ioctl (struct cdev *dev, u_long cmd, caddr_t data, int flag, struct thread *td);
static struct cdevsw cx_cdevsw = {
	.d_version  = D_VERSION,
	.d_open     = cx_open,
	.d_close    = cx_close,
	.d_ioctl    = cx_ioctl,
	.d_name     = "cx",
	.d_flags    = D_TTY,
};

static int MY_SOFT_INTR;

/*
 * Make an mbuf from data.
 */
static struct mbuf *makembuf (void *buf, u_int len)
{
	struct mbuf *m, *o, *p;

	MGETHDR (m, M_NOWAIT, MT_DATA);

	if (! m)
		return 0;

	if (len >= MINCLSIZE)
		MCLGET (m, M_NOWAIT);

	m->m_pkthdr.len = len;
	m->m_len = 0;

	p = m;
	while (len) {
		u_int n = M_TRAILINGSPACE (p);
		if (n > len)
			n = len;
		if (! n) {
			/* Allocate new mbuf. */
			o = p;
			MGET (p, M_NOWAIT, MT_DATA);
			if (! p) {
				m_freem (m);
				return 0;
			}
			if (len >= MINCLSIZE)
				MCLGET (p, M_NOWAIT);
			p->m_len = 0;
			o->m_next = p;

			n = M_TRAILINGSPACE (p);
			if (n > len)
				n = len;
		}
		bcopy (buf, mtod (p, caddr_t) + p->m_len, n);

		p->m_len += n;
		buf = n + (char*) buf;
		len -= n;
	}
	return m;
}

/*
 * Recover after lost transmit interrupts.
 */
static void cx_timeout (void *arg)
{
	drv_t *d;
	int s, i, k;

	for (i = 0; i < NCX; i++) {
		if (adapter[i] == NULL)
			continue;
		for (k = 0; k < NCHAN; ++k) {
			d = channel[i * NCHAN + k];
			if (! d)
				continue;
			s = splhigh ();
			CX_LOCK ((bdrv_t *)d->board->sys);
			if (d->atimeout == 1 && d->tty && d->tty->t_state & TS_BUSY) {
				d->tty->t_state &= ~TS_BUSY;
				if (d->tty->t_dev) {
					d->intr_action |= CX_WRITE;
					MY_SOFT_INTR = 1;
					swi_sched (cx_fast_ih, 0);
				}
				CX_DEBUG (d, ("cx_timeout\n"));
			}
			if (d->atimeout)
				d->atimeout--;
			CX_UNLOCK ((bdrv_t *)d->board->sys);
			splx (s);
		}
	}
	callout_reset (&timeout_handle, hz*5, cx_timeout, 0);
}

static void cx_led_off (void *arg)
{
	cx_board_t *b = arg;
	bdrv_t *bd = b->sys;
	int s;

	s = splhigh ();
	CX_LOCK (bd);
	cx_led (b, 0);
	CX_UNLOCK (bd);
	splx (s);
}

/*
 * Activate interrupt handler from DDK.
 */
static void cx_intr (void *arg)
{
	bdrv_t *bd = arg;
	cx_board_t *b = bd->board;
#ifndef NETGRAPH
	int i;
#endif
	int s = splhigh ();

	CX_LOCK (bd);
	/* Turn LED on. */
	cx_led (b, 1);

	cx_int_handler (b);

	/* Turn LED off 50 msec later. */
	callout_reset (&led_timo[b->num], hz/20, cx_led_off, b);
	CX_UNLOCK (bd);
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

static int probe_irq (cx_board_t *b, int irq)
{
	int mask, busy, cnt;

	/* Clear pending irq, if any. */
	cx_probe_irq (b, -irq);
	DELAY (100);
	for (cnt=0; cnt<5; ++cnt) {
		/* Get the mask of pending irqs, assuming they are busy.
		 * Activate the adapter on given irq. */
		busy = cx_probe_irq (b, irq);
		DELAY (100);

		/* Get the mask of active irqs.
		 * Deactivate our irq. */
		mask = cx_probe_irq (b, -irq);
		DELAY (100);
		if ((mask & ~busy) == 1 << irq) {
			cx_probe_irq (b, 0);
			/* printf ("cx%d: irq %d ok, mask=0x%04x, busy=0x%04x\n",
				b->num, irq, mask, busy); */
			return 1;
		}
	}
	/* printf ("cx%d: irq %d not functional, mask=0x%04x, busy=0x%04x\n",
		b->num, irq, mask, busy); */
	cx_probe_irq (b, 0);
	return 0;
}

static short porttab [] = {
	0x200, 0x220, 0x240, 0x260, 0x280, 0x2a0, 0x2c0, 0x2e0,
	0x300, 0x320, 0x340, 0x360, 0x380, 0x3a0, 0x3c0, 0x3e0, 0
};
static char dmatab [] = { 7, 6, 5, 0 };
static char irqtab [] = { 5, 10, 11, 7, 3, 15, 12, 0 };

static int cx_is_free_res (device_t dev, int rid, int type, rman_res_t start,
	rman_res_t end, rman_res_t count)
{
	struct resource *res;
	
	if (!(res = bus_alloc_resource (dev, type, &rid, start, end, count, 0)))
		return 0;
		
	bus_release_resource (dev, type, rid, res);
	
	return 1;
}

static void cx_identify (driver_t *driver, device_t dev)
{
	rman_res_t iobase, rescount;
	int devcount;
	device_t *devices;
	device_t child;
	devclass_t my_devclass;
	int i, k;

	if ((my_devclass = devclass_find ("cx")) == NULL)
		return;

	devclass_get_devices (my_devclass, &devices, &devcount);

	if (devcount == 0) {
		/* We should find all devices by our self. We could alter other
		 * devices, but we don't have a choise
		 */
		for (i = 0; (iobase = porttab [i]) != 0; i++) {
			if (!cx_is_free_res (dev, 0, SYS_RES_IOPORT,
			    iobase, iobase + NPORT, NPORT))
				continue;
			if (cx_probe_board (iobase, -1, -1) == 0)
				continue;
			
			devcount++;

			child = BUS_ADD_CHILD (dev, ISA_ORDER_SPECULATIVE, "cx",
			    -1);

			if (child == NULL)
				return;

			device_set_desc_copy (child, "Cronyx Sigma");
			device_set_driver (child, driver);
			bus_set_resource (child, SYS_RES_IOPORT, 0,
			    iobase, NPORT);

			if (devcount >= NCX)
				break;
		}
	} else {
		static short porttab [] = {
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
				if (!cx_is_free_res (devices[k], 0, SYS_RES_IOPORT,
				    iobase, iobase + NPORT, NPORT))
					continue;
				if (cx_probe_board (iobase, -1, -1) == 0)
					continue;
				porttab [i] = -1;
				device_set_desc_copy (devices[k], "Cronyx Sigma");
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
				if (porttab [i] == -1) {
					continue;
				}
				if (!cx_is_free_res (devices[k], 0, SYS_RES_IOPORT,
				    iobase, iobase + NPORT, NPORT))
					continue;
				if (cx_probe_board (iobase, -1, -1) == 0)
					continue;
			
				bus_set_resource (devices[k], SYS_RES_IOPORT, 0,
				    iobase, NPORT);
				porttab [i] = -1;
				device_set_desc_copy (devices[k], "Cronyx Sigma");
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

static int cx_probe (device_t dev)
{
	int unit = device_get_unit (dev);
	int i;
	rman_res_t iobase, rescount;

	if (!device_get_desc (dev) ||
	    strcmp (device_get_desc (dev), "Cronyx Sigma"))
		return ENXIO;
	
	if (bus_get_resource (dev, SYS_RES_IOPORT, 0, &iobase, &rescount) != 0) {
		printf ("cx%d: Couldn't get IOPORT\n", unit);
		return ENXIO;
	}

	if (!cx_is_free_res (dev, 0, SYS_RES_IOPORT,
	    iobase, iobase + NPORT, NPORT)) {
		printf ("cx%d: Resource IOPORT isn't free %lx\n", unit, iobase);
		return ENXIO;
	}
		
	for (i = 0; porttab [i] != 0; i++) {
		if (porttab [i] == iobase) {
			porttab [i] = -1;
			break;
		}
	}
	
	if (porttab [i] == 0) {
		return ENXIO;
	}
	
	if (!cx_probe_board (iobase, -1, -1)) {
		printf ("cx%d: probing for Sigma at %lx faild\n", unit, iobase);
		return ENXIO;
	}
	
	return 0;
}

static void
cx_bus_dmamap_addr (void *arg, bus_dma_segment_t *segs, int nseg, int error)
{
	unsigned long *addr;

	if (error)
		return;

	KASSERT(nseg == 1, ("too many DMA segments, %d should be 1", nseg));
	addr = arg;
	*addr = segs->ds_addr;
}

static int
cx_bus_dma_mem_alloc (int bnum, int cnum, cx_dma_mem_t *dmem)
{
	int error;

	error = bus_dma_tag_create (NULL, 16, 0, BUS_SPACE_MAXADDR_24BIT,
		BUS_SPACE_MAXADDR, NULL, NULL, dmem->size, 1,
		dmem->size, 0, NULL, NULL, &dmem->dmat);
	if (error) {
		if (cnum >= 0)	printf ("cx%d-%d: ", bnum, cnum);
		else		printf ("cx%d: ", bnum);
		printf ("couldn't allocate tag for dma memory\n");
 		return 0;
	}
	error = bus_dmamem_alloc (dmem->dmat, (void **)&dmem->virt,
		BUS_DMA_NOWAIT | BUS_DMA_ZERO, &dmem->mapp);
	if (error) {
		if (cnum >= 0)	printf ("cx%d-%d: ", bnum, cnum);
		else		printf ("cx%d: ", bnum);
		printf ("couldn't allocate mem for dma memory\n");
		bus_dma_tag_destroy (dmem->dmat);
 		return 0;
	}
	error = bus_dmamap_load (dmem->dmat, dmem->mapp, dmem->virt,
		dmem->size, cx_bus_dmamap_addr, &dmem->phys, 0);
	if (error) {
		if (cnum >= 0)	printf ("cx%d-%d: ", bnum, cnum);
		else		printf ("cx%d: ", bnum);
		printf ("couldn't load mem map for dma memory\n");
		bus_dmamem_free (dmem->dmat, dmem->virt, dmem->mapp);
		bus_dma_tag_destroy (dmem->dmat);
 		return 0;
	}
	return 1;
}

static void
cx_bus_dma_mem_free (cx_dma_mem_t *dmem)
{
	bus_dmamap_unload (dmem->dmat, dmem->mapp);
	bus_dmamem_free (dmem->dmat, dmem->virt, dmem->mapp);
	bus_dma_tag_destroy (dmem->dmat);
}

/*
 * The adapter is present, initialize the driver structures.
 */
static int cx_attach (device_t dev)
{
	bdrv_t *bd = device_get_softc (dev);
	rman_res_t iobase, drq, irq, rescount;
	int unit = device_get_unit (dev);
	char *cx_ln = CX_LOCK_NAME;
	cx_board_t *b;
	cx_chan_t *c;
	drv_t *d;
	int i;
	int s;

	KASSERT ((bd != NULL), ("cx%d: NULL device softc\n", unit));
	
	bus_get_resource (dev, SYS_RES_IOPORT, 0, &iobase, &rescount);
	bd->base_rid = 0;
	bd->base_res = bus_alloc_resource (dev, SYS_RES_IOPORT, &bd->base_rid,
		iobase, iobase + NPORT, NPORT, RF_ACTIVE);
	if (! bd->base_res) {
		printf ("cx%d: cannot allocate base address\n", unit);
		return ENXIO;
	}
	
	if (bus_get_resource (dev, SYS_RES_DRQ, 0, &drq, &rescount) != 0) {
		for (i = 0; (drq = dmatab [i]) != 0; i++) {
			if (!cx_is_free_res (dev, 0, SYS_RES_DRQ,
			    drq, drq + 1, 1))
				continue;
			bus_set_resource (dev, SYS_RES_DRQ, 0, drq, 1);
			break;
		}
		
		if (dmatab[i] == 0) {	
			bus_release_resource (dev, SYS_RES_IOPORT, bd->base_rid,
				bd->base_res);
			printf ("cx%d: Couldn't get DRQ\n", unit);
			return ENXIO;
		}
	}
	
	bd->drq_rid = 0;
	bd->drq_res = bus_alloc_resource (dev, SYS_RES_DRQ, &bd->drq_rid,
		drq, drq + 1, 1, RF_ACTIVE);
	if (! bd->drq_res) {
		printf ("cx%d: cannot allocate drq\n", unit);
		bus_release_resource (dev, SYS_RES_IOPORT, bd->base_rid,
			bd->base_res);
		return ENXIO;
	}	
	
	if (bus_get_resource (dev, SYS_RES_IRQ, 0, &irq, &rescount) != 0) {
		for (i = 0; (irq = irqtab [i]) != 0; i++) {
			if (!cx_is_free_res (dev, 0, SYS_RES_IRQ,
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
			printf ("cx%d: Couldn't get IRQ\n", unit);
			return ENXIO;
		}
	}
	
	bd->irq_rid = 0;
	bd->irq_res = bus_alloc_resource (dev, SYS_RES_IRQ, &bd->irq_rid,
		irq, irq + 1, 1, RF_ACTIVE);
	if (! bd->irq_res) {
		printf ("cx%d: Couldn't allocate irq\n", unit);
		bus_release_resource (dev, SYS_RES_DRQ, bd->drq_rid,
			bd->drq_res);
		bus_release_resource (dev, SYS_RES_IOPORT, bd->base_rid,
			bd->base_res);
		return ENXIO;
	}
	
	b = malloc (sizeof (cx_board_t), M_DEVBUF, M_WAITOK);
	if (!b) {
		printf ("cx:%d: Couldn't allocate memory\n", unit);
		return (ENXIO);
	}
	adapter[unit] = b;
	bzero (b, sizeof(cx_board_t));
	
	if (! cx_open_board (b, unit, iobase, irq, drq)) {
		printf ("cx%d: error loading firmware\n", unit);
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
	
	cx_ln[2] = '0' + unit;
	mtx_init (&bd->cx_mtx, cx_ln, MTX_NETWORK_LOCK, MTX_DEF|MTX_RECURSE);
	if (! probe_irq (b, irq)) {
		printf ("cx%d: irq %ld not functional\n", unit, irq);
		bd->board = 0;
		adapter [unit] = 0;
		mtx_destroy (&bd->cx_mtx);
		free (b, M_DEVBUF);
		bus_release_resource (dev, SYS_RES_IRQ, bd->irq_rid,
			bd->irq_res);
		bus_release_resource (dev, SYS_RES_DRQ, bd->drq_rid,
			bd->drq_res);
		bus_release_resource (dev, SYS_RES_IOPORT, bd->base_rid,
			bd->base_res);
 		return ENXIO;
	}
	b->sys = bd;
	callout_init (&led_timo[b->num], 1);
	s = splhigh ();
	if (bus_setup_intr (dev, bd->irq_res,
			   INTR_TYPE_NET|INTR_MPSAFE,
			   NULL, cx_intr, bd, &bd->intrhand)) {
		printf ("cx%d: Can't setup irq %ld\n", unit, irq);
		bd->board = 0;
		b->sys = 0;
		adapter [unit] = 0;
		mtx_destroy (&bd->cx_mtx);
		free (b, M_DEVBUF);
		bus_release_resource (dev, SYS_RES_IRQ, bd->irq_rid,
			bd->irq_res);
		bus_release_resource (dev, SYS_RES_DRQ, bd->drq_rid,
			bd->drq_res);
		bus_release_resource (dev, SYS_RES_IOPORT, bd->base_rid,
			bd->base_res);
		splx (s);
 		return ENXIO;		
	}
	
	CX_LOCK (bd);
	cx_init (b, b->num, b->port, irq, drq);
	cx_setup_board (b, 0, 0, 0);
	CX_UNLOCK (bd);

	printf ("cx%d: <Cronyx-Sigma-%s>\n", b->num, b->name);

	for (c=b->chan; c<b->chan+NCHAN; ++c) {
		if (c->type == T_NONE)
			continue;
		d = &bd->channel[c->num];
		d->dmamem.size = sizeof(cx_buf_t);
		if (! cx_bus_dma_mem_alloc (unit, c->num, &d->dmamem))
			continue;
		d->board = b;
		d->chan = c;
		d->open_dev = 0;
		c->sys = d;
		channel [b->num*NCHAN + c->num] = d;
		sprintf (d->name, "cx%d.%d", b->num, c->num);

		switch (c->type) {
		case T_SYNC_RS232:
		case T_SYNC_V35:
		case T_SYNC_RS449:
		case T_UNIV:
		case T_UNIV_RS232:
		case T_UNIV_RS449:
		case T_UNIV_V35:
		callout_init (&d->timeout_handle, 1);
#ifdef NETGRAPH
		if (ng_make_node_common (&typestruct, &d->node) != 0) {
			printf ("%s: cannot make common node\n", d->name);
			channel [b->num*NCHAN + c->num] = 0;
			c->sys = 0;
			cx_bus_dma_mem_free (&d->dmamem);
			continue;
		}
		NG_NODE_SET_PRIVATE (d->node, d);
		sprintf (d->nodename, "%s%d", NG_CX_NODE_TYPE,
			 c->board->num*NCHAN + c->num);
		if (ng_name_node (d->node, d->nodename)) {
			printf ("%s: cannot name node\n", d->nodename);
			NG_NODE_UNREF (d->node);
			channel [b->num*NCHAN + c->num] = 0;
			c->sys = 0;
			cx_bus_dma_mem_free (&d->dmamem);
			continue;
		}
		d->lo_queue.ifq_maxlen = ifqmaxlen;
		d->hi_queue.ifq_maxlen = ifqmaxlen;
		mtx_init (&d->lo_queue.ifq_mtx, "cx_queue_lo", NULL, MTX_DEF);
		mtx_init (&d->hi_queue.ifq_mtx, "cx_queue_hi", NULL, MTX_DEF);
#else /*NETGRAPH*/
		d->ifp = if_alloc(IFT_PPP);
		if (d->ifp == NULL) {
			printf ("%s: cannot if_alloc() common interface\n",
			    d->name);
			channel [b->num*NCHAN + c->num] = 0;
			c->sys = 0;
			cx_bus_dma_mem_free (&d->dmamem);
			continue;
		}
		d->ifp->if_softc	= d;
		if_initname (d->ifp, "cx", b->num * NCHAN + c->num);
		d->ifp->if_mtu		= PP_MTU;
		d->ifp->if_flags	= IFF_POINTOPOINT | IFF_MULTICAST;
		d->ifp->if_ioctl	= cx_sioctl;
		d->ifp->if_start	= cx_ifstart;
		d->ifp->if_init		= cx_initialize;
		d->queue.ifq_maxlen	= 2;
		mtx_init (&d->queue.ifq_mtx, "cx_queue", NULL, MTX_DEF);
		sppp_attach (d->ifp);
		if_attach (d->ifp);
		IFP2SP(d->ifp)->pp_tlf	= cx_tlf;
		IFP2SP(d->ifp)->pp_tls	= cx_tls;
		/* If BPF is in the kernel, call the attach for it.
		 * Size of PPP header is 4 bytes. */
		bpfattach (d->ifp, DLT_PPP, 4);
#endif /*NETGRAPH*/
		}
		d->tty = ttyalloc ();
		d->tty->t_open	= cx_topen;
		d->tty->t_close	= cx_tclose;
		d->tty->t_param	= cx_param;
		d->tty->t_stop	= cx_stop;
		d->tty->t_modem	= cx_tmodem;
		d->tty->t_oproc	= cx_oproc;
		d->tty->t_sc	= d;
		CX_LOCK (bd);
		cx_start_chan (c, d->dmamem.virt, d->dmamem.phys);
		cx_register_receive (c, &cx_receive);
		cx_register_transmit (c, &cx_transmit);
		cx_register_error (c, &cx_error);
		cx_register_modem (c, &cx_modem);
		CX_UNLOCK (bd);

		ttycreate(d->tty, TS_CALLOUT, "x%r%r", b->num, c->num);
		d->devt = make_dev (&cx_cdevsw, b->num*NCHAN + c->num + 64, UID_ROOT, GID_WHEEL, 0600, "cx%d", b->num*NCHAN + c->num);
		d->devt->si_drv1 = d;
		callout_init (&d->dcd_timeout_handle, 1);
	}
	splx (s);

	return 0;
}

static int cx_detach (device_t dev)
{
	bdrv_t *bd = device_get_softc (dev);
	cx_board_t *b = bd->board;
	cx_chan_t *c;
	int s;
	
	KASSERT (mtx_initialized (&bd->cx_mtx), ("cx mutex not initialized"));

	s = splhigh ();
	CX_LOCK (bd);
	/* Check if the device is busy (open). */
	for (c = b->chan; c < b->chan + NCHAN; ++c) {
		drv_t *d = (drv_t*) c->sys;

		if (!d || d->chan->type == T_NONE)
			continue;
		if (d->lock) {
			CX_UNLOCK (bd);
			splx (s);
			return EBUSY;
		}
		if (c->mode == M_ASYNC && d->tty && (d->tty->t_state & TS_ISOPEN) &&
		    (d->open_dev|0x2)) {
			CX_UNLOCK (bd);
			splx (s);
			return EBUSY;
		}
		if (d->running) {
			CX_UNLOCK (bd);
			splx (s);
			return EBUSY;
		}
	}

	/* Deactivate the timeout routine. And soft interrupt*/
	callout_stop (&led_timo[b->num]);

	for (c = b->chan; c < b->chan + NCHAN; ++c) {
		drv_t *d = c->sys;

		if (!d || d->chan->type == T_NONE)
			continue;

		callout_stop (&d->dcd_timeout_handle);
	}
	CX_UNLOCK (bd);
	bus_teardown_intr (dev, bd->irq_res, bd->intrhand);
	bus_release_resource (dev, SYS_RES_IRQ, bd->irq_rid, bd->irq_res);
	
	bus_release_resource (dev, SYS_RES_DRQ, bd->drq_rid, bd->drq_res);
	
	bus_release_resource (dev, SYS_RES_IOPORT, bd->base_rid, bd->base_res);

	CX_LOCK (bd);
	cx_close_board (b);

	/* Detach the interfaces, free buffer memory. */
	for (c = b->chan; c < b->chan + NCHAN; ++c) {
		drv_t *d = (drv_t*) c->sys;

		if (!d || d->chan->type == T_NONE)
			continue;
			
		if (d->tty) {
			ttyfree (d->tty);
			d->tty = NULL;
		}

		callout_stop (&d->timeout_handle);
#ifdef NETGRAPH
		if (d->node) {
			ng_rmnode_self (d->node);
			NG_NODE_UNREF (d->node);
			d->node = NULL;
		}
		mtx_destroy (&d->lo_queue.ifq_mtx);
		mtx_destroy (&d->hi_queue.ifq_mtx);
#else
		/* Detach from the packet filter list of interfaces. */
		bpfdetach (d->ifp);
		/* Detach from the sync PPP list. */
		sppp_detach (d->ifp);

		if_detach (d->ifp);
		if_free(d->ifp);
		/* XXXRIK: check interconnection with irq handler */
		IF_DRAIN (&d->queue);
		mtx_destroy (&d->queue.ifq_mtx);
#endif		
		destroy_dev (d->devt);
	}

	cx_led_off (b);
	CX_UNLOCK (bd);
	callout_drain (&led_timo[b->num]);
	for (c = b->chan; c < b->chan + NCHAN; ++c) {
		drv_t *d = c->sys;

		if (!d || d->chan->type == T_NONE)
			continue;

		callout_drain (&d->dcd_timeout_handle);
		callout_drain (&d->timeout_handle);
	}
	splx (s);
	
	s = splhigh ();
	for (c = b->chan; c < b->chan + NCHAN; ++c) {
		drv_t *d = (drv_t*) c->sys;

		if (!d || d->chan->type == T_NONE)
			continue;
		
		/* Deallocate buffers. */
		cx_bus_dma_mem_free (&d->dmamem);
	}
	bd->board = NULL;
	adapter [b->num] = NULL;
	free (b, M_DEVBUF);
	splx (s);

	mtx_destroy (&bd->cx_mtx);
	
	return 0;	
}

#ifndef NETGRAPH
static void cx_ifstart (struct ifnet *ifp)
{
	drv_t *d = ifp->if_softc;
	bdrv_t *bd = d->board->sys;

	CX_LOCK (bd);
	cx_start (d);
	CX_UNLOCK (bd);
}

static void cx_tlf (struct sppp *sp)
{
	drv_t *d = SP2IFP(sp)->if_softc;

	CX_DEBUG (d, ("cx_tlf\n"));
/*	cx_set_dtr (d->chan, 0);*/
/*	cx_set_rts (d->chan, 0);*/
	if (!(IFP2SP(d->ifp)->pp_flags & PP_FR) && !(d->ifp->if_flags & PP_CISCO))
		sp->pp_down (sp);
}

static void cx_tls (struct sppp *sp)
{
	drv_t *d = SP2IFP(sp)->if_softc;

	CX_DEBUG (d, ("cx_tls\n"));
	if (!(IFP2SP(d->ifp)->pp_flags & PP_FR) && !(d->ifp->if_flags & PP_CISCO))
		sp->pp_up (sp);
}

/*
 * Initialization of interface.
 * It seems to be never called by upper level.
 */
static void cx_initialize (void *softc)
{
	drv_t *d = softc;

	CX_DEBUG (d, ("cx_initialize\n"));
}

/*
 * Process an ioctl request.
 */
static int cx_sioctl (struct ifnet *ifp, u_long cmd, caddr_t data)
{
	drv_t *d = ifp->if_softc;
	bdrv_t *bd = d->board->sys;
	int error, s, was_up, should_be_up;

	/* No socket ioctls while the channel is in async mode. */
	if (d->chan->type == T_NONE || d->chan->mode == M_ASYNC)
		return EBUSY;

	/* Socket ioctls on slave subchannels are not allowed. */
	was_up = (ifp->if_drv_flags & IFF_DRV_RUNNING) != 0;
	error = sppp_ioctl (ifp, cmd, data);
	if (error)
		return error;

	s = splhigh ();
	CX_LOCK (bd);
	if (! (ifp->if_flags & IFF_DEBUG))
		d->chan->debug = 0;
	else
		d->chan->debug = d->chan->debug_shadow;
	CX_UNLOCK (bd);
	splx (s);

	switch (cmd) {
	default:	   CX_DEBUG2 (d, ("ioctl 0x%lx\n", cmd)); return 0;
	case SIOCADDMULTI: CX_DEBUG2 (d, ("SIOCADDMULTI\n"));     return 0;
	case SIOCDELMULTI: CX_DEBUG2 (d, ("SIOCDELMULTI\n"));     return 0;
	case SIOCSIFFLAGS: CX_DEBUG2 (d, ("SIOCSIFFLAGS\n"));     break;
	case SIOCSIFADDR:  CX_DEBUG2 (d, ("SIOCSIFADDR\n"));      break;
	}

	/* We get here only in case of SIFFLAGS or SIFADDR. */
	s = splhigh ();
	CX_LOCK (bd);
	should_be_up = (ifp->if_drv_flags & IFF_DRV_RUNNING) != 0;
	if (!was_up && should_be_up) {
		/* Interface goes up -- start it. */
		cx_up (d);
		cx_start (d);
	} else if (was_up && !should_be_up) {
		/* Interface is going down -- stop it. */
		/* if ((IFP2SP(d->ifp)->pp_flags & PP_FR) || (ifp->if_flags & PP_CISCO))*/
		cx_down (d);
	}
	CX_UNLOCK (bd);
	splx (s);
	return 0;
}
#endif /*NETGRAPH*/

/*
 * Stop the interface.  Called on splimp().
 */
static void cx_down (drv_t *d)
{
	int s = splhigh ();
	CX_DEBUG (d, ("cx_down\n"));
	cx_set_dtr (d->chan, 0);
	cx_set_rts (d->chan, 0);
	d->running = 0;
	callout_stop (&d->timeout_handle);
	splx (s);
}

/*
 * Start the interface.  Called on splimp().
 */
static void cx_up (drv_t *d)
{
	int s = splhigh ();
	CX_DEBUG (d, ("cx_up\n"));
	cx_set_dtr (d->chan, 1);
	cx_set_rts (d->chan, 1);
	d->running = 1;
	splx (s);
}

/*
 * Start output on the (slave) interface.  Get another datagram to send
 * off of the interface queue, and copy it to the interface
 * before starting the output.
 */
static void cx_send (drv_t *d)
{
	struct mbuf *m;
	u_short len;

	CX_DEBUG2 (d, ("cx_send\n"));

	/* No output if the interface is down. */
	if (! d->running)
		return;

	/* No output if the modem is off. */
	if (! cx_get_dsr (d->chan) && ! cx_get_loop(d->chan))
		return;

	if (cx_buf_free (d->chan)) {
		/* Get the packet to send. */
#ifdef NETGRAPH
		IF_DEQUEUE (&d->hi_queue, m);
		if (! m)
			IF_DEQUEUE (&d->lo_queue, m);
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
			cx_send_packet (d->chan, (u_char*)mtod (m, caddr_t),
				len, 0);
		else {
			u_char buf [DMABUFSZ];
			m_copydata (m, 0, len, buf);
			cx_send_packet (d->chan, buf, len, 0);
		}
		m_freem (m);

		/* Set up transmit timeout, 10 seconds. */
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
static void cx_start (drv_t *d)
{
	int s = splhigh ();
	if (d->running) {
		if (! d->chan->dtr)
			cx_set_dtr (d->chan, 1);
		if (! d->chan->rts)
			cx_set_rts (d->chan, 1);
		cx_send (d);
		callout_reset (&d->timeout_handle, hz, cx_watchdog_timer, d);
	}
	splx (s);
}

/*
 * Handle transmit timeouts.
 * Recover after lost transmit interrupts.
 * Always called on splimp().
 */
static void cx_watchdog (drv_t *d)
{
	
	CX_DEBUG (d, ("device timeout\n"));
	if (d->running) {
		cx_setup_chan (d->chan);
		cx_start_chan (d->chan, 0, 0);
		cx_set_dtr (d->chan, 1);
		cx_set_rts (d->chan, 1);
		cx_start (d);
	}
}

static void cx_watchdog_timer (void *arg)
{
	drv_t *d = arg;
	bdrv_t *bd = d->board->sys;

	CX_LOCK (bd);
	if (d->timeout == 1)
		cx_watchdog (d);
	if (d->timeout)
		d->timeout--;
	callout_reset (&d->timeout_handle, hz, cx_watchdog_timer, d);
	CX_UNLOCK (bd);
}

/*
 * Transmit callback function.
 */
static void cx_transmit (cx_chan_t *c, void *attachment, int len)
{
	drv_t *d = c->sys;

	if (!d)
		return;
		
	if (c->mode == M_ASYNC && d->tty) {
		d->tty->t_state &= ~(TS_BUSY | TS_FLUSH);
		d->atimeout = 0;
		if (d->tty->t_dev) {
			d->intr_action |= CX_WRITE;
			MY_SOFT_INTR = 1;
			swi_sched (cx_fast_ih, 0);
		}
		return;
	}
	d->timeout = 0;
#ifndef NETGRAPH
	if_inc_counter(d->ifp, IFCOUNTER_OPACKETS, 1);
	d->ifp->if_drv_flags &= ~IFF_DRV_OACTIVE;
#endif
	cx_start (d);
}

/*
 * Process the received packet.
 */
static void cx_receive (cx_chan_t *c, char *data, int len)
{
	drv_t *d = c->sys;
	struct mbuf *m;
	char *cc = data;
#ifdef NETGRAPH
	int error;
#endif

	if (!d)
		return;
		
	if (c->mode == M_ASYNC && d->tty) {
		if (d->tty->t_state & TS_ISOPEN) {
			async_q *q = &d->aqueue;
			int size = BF_SZ - 1 - AQ_GSZ (q);

			if (len <= 0 && !size)
				return;

			if (len > size) {
				c->ierrs++;
				cx_error (c, CX_OVERRUN);
				len = size - 1;
			}

			while (len--) {
				AQ_PUSH (q, *(unsigned char *)cc);
				cc++;
			}

			d->intr_action |= CX_READ;
			MY_SOFT_INTR = 1;
			swi_sched (cx_fast_ih, 0);
		}
		return;
	}
	if (! d->running)
		return;

	m = makembuf (data, len);
	if (! m) {
		CX_DEBUG (d, ("no memory for packet\n"));
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

#define CONDITION(t,tp) (!(t->c_iflag & (ICRNL | IGNCR | IMAXBEL | INLCR | ISTRIP | IXON))\
	    && (!(tp->t_iflag & BRKINT) || (tp->t_iflag & IGNBRK))\
	    && (!(tp->t_iflag & PARMRK)\
		|| (tp->t_iflag & (IGNPAR | IGNBRK)) == (IGNPAR | IGNBRK))\
	    && !(t->c_lflag & (ECHO | ICANON | IEXTEN | ISIG | PENDIN))\
	    && linesw[tp->t_line]->l_rint == ttyinput)

/*
 * Error callback function.
 */
static void cx_error (cx_chan_t *c, int data)
{
	drv_t *d = c->sys;
	async_q *q;

	if (!d)
		return;

	q = &(d->aqueue);

	switch (data) {
	case CX_FRAME:
		CX_DEBUG (d, ("frame error\n"));
		if (c->mode == M_ASYNC && d->tty && (d->tty->t_state & TS_ISOPEN)
			&& (AQ_GSZ (q) < BF_SZ - 1)
			&& (!CONDITION((&d->tty->t_termios), (d->tty))
			|| !(d->tty->t_iflag & (IGNPAR | PARMRK)))) {
			AQ_PUSH (q, TTY_FE);
			d->intr_action |= CX_READ;
			MY_SOFT_INTR = 1;
			swi_sched (cx_fast_ih, 0);
		}
#ifndef NETGRAPH
		else
			if_inc_counter(d->ifp, IFCOUNTER_IERRORS, 1);
#endif
		break;
	case CX_CRC:
		CX_DEBUG (d, ("crc error\n"));
		if (c->mode == M_ASYNC && d->tty && (d->tty->t_state & TS_ISOPEN)
			&& (AQ_GSZ (q) < BF_SZ - 1)
			&& (!CONDITION((&d->tty->t_termios), (d->tty))
			|| !(d->tty->t_iflag & INPCK)
			|| !(d->tty->t_iflag & (IGNPAR | PARMRK)))) {
			AQ_PUSH (q, TTY_PE);
			d->intr_action |= CX_READ;
			MY_SOFT_INTR = 1;
			swi_sched (cx_fast_ih, 0);
		}
#ifndef NETGRAPH
		else
			if_inc_counter(d->ifp, IFCOUNTER_IERRORS, 1);
#endif
		break;
	case CX_OVERRUN:
		CX_DEBUG (d, ("overrun error\n"));
#ifdef TTY_OE
		if (c->mode == M_ASYNC && d->tty && (d->tty->t_state & TS_ISOPEN)
			&& (AQ_GSZ (q) < BF_SZ - 1)
			&& (!CONDITION((&d->tty->t_termios), (d->tty)))) {
			AQ_PUSH (q, TTY_OE);
			d->intr_action |= CX_READ;
			MY_SOFT_INTR = 1;
			swi_sched (cx_fast_ih, 0);
		}
#endif
#ifndef NETGRAPH
		else {
			if_inc_counter(d->ifp, IFCOUNTER_COLLISIONS, 1);
			if_inc_counter(d->ifp, IFCOUNTER_IERRORS, 1);
		}
#endif
		break;
	case CX_OVERFLOW:
		CX_DEBUG (d, ("overflow error\n"));
#ifndef NETGRAPH
		if (c->mode != M_ASYNC)
			if_inc_counter(d->ifp, IFCOUNTER_IERRORS, 1);
#endif
		break;
	case CX_UNDERRUN:
		CX_DEBUG (d, ("underrun error\n"));
		if (c->mode != M_ASYNC) {
			d->timeout = 0;
#ifndef NETGRAPH
			if_inc_counter(d->ifp, IFCOUNTER_OERRORS, 1);
			d->ifp->if_drv_flags &= ~IFF_DRV_OACTIVE;
#endif
			cx_start (d);
		}
		break;
	case CX_BREAK:
		CX_DEBUG (d, ("break error\n"));
		if (c->mode == M_ASYNC && d->tty && (d->tty->t_state & TS_ISOPEN)
			&& (AQ_GSZ (q) < BF_SZ - 1)
			&& (!CONDITION((&d->tty->t_termios), (d->tty))
			|| !(d->tty->t_iflag & (IGNBRK | BRKINT | PARMRK)))) {
			AQ_PUSH (q, TTY_BI);
			d->intr_action |= CX_READ;
			MY_SOFT_INTR = 1;
			swi_sched (cx_fast_ih, 0);
		}
#ifndef NETGRAPH
		else
			if_inc_counter(d->ifp, IFCOUNTER_IERRORS, 1);
#endif
		break;
	default:
		CX_DEBUG (d, ("error #%d\n", data));
	}
}

static int cx_topen (struct tty *tp, struct cdev *dev)
{
	bdrv_t *bd;
	drv_t *d;

	d = tp->t_sc;
	CX_DEBUG2 (d, ("cx_open (serial)\n"));

	bd = d->board->sys;

	if (d->chan->mode != M_ASYNC)
		return (EBUSY);

	d->open_dev |= 0x2;
	CX_LOCK (bd);
	cx_start_chan (d->chan, 0, 0);
	cx_set_dtr (d->chan, 1);
	cx_set_rts (d->chan, 1);
	d->cd = cx_get_cd (d->chan);
	CX_UNLOCK (bd);

	CX_DEBUG2 (d, ("cx_open done\n"));

	return 0;
}

static void cx_tclose (struct tty *tp)
{
	drv_t *d;
	bdrv_t *bd;

	d = tp->t_sc;
	CX_DEBUG2 (d, ("cx_close\n"));
	bd = d->board->sys;
	CX_LOCK (bd);
	/* Disable receiver.
	 * Transmitter continues sending the queued data. */
	cx_enable_receive (d->chan, 0);
	CX_UNLOCK (bd);
	d->open_dev &= ~0x2;
}

static int cx_tmodem (struct tty *tp, int sigon, int sigoff)
{
	drv_t *d;
	bdrv_t *bd;

	d = tp->t_sc;
	bd = d->board->sys;

	CX_LOCK (bd);
	if (!sigon && !sigoff) {
		if (cx_get_dsr (d->chan)) sigon |= SER_DSR;
		if (cx_get_cd  (d->chan)) sigon |= SER_DCD;
		if (cx_get_cts (d->chan)) sigon |= SER_CTS;
		if (d->chan->dtr)	  sigon |= SER_DTR;
		if (d->chan->rts)	  sigon |= SER_RTS;
		CX_UNLOCK (bd);
		return sigon;
	}

	if (sigon & SER_DTR)
		cx_set_dtr (d->chan, 1);
	if (sigoff & SER_DTR)
		cx_set_dtr (d->chan, 0);
	if (sigon & SER_RTS)
		cx_set_rts (d->chan, 1);
	if (sigoff & SER_RTS)
		cx_set_rts (d->chan, 0);
	CX_UNLOCK (bd);

	return (0);
}

static int cx_open (struct cdev *dev, int flag, int mode, struct thread *td)
{
	int unit;
	drv_t *d;

	d = dev->si_drv1;
	unit = d->chan->num;

	CX_DEBUG2 (d, ("cx_open unit=%d, flag=0x%x, mode=0x%x\n",
		    unit, flag, mode));

	d->open_dev |= 0x1;

	CX_DEBUG2 (d, ("cx_open done\n"));

	return 0;
}

static int cx_close (struct cdev *dev, int flag, int mode, struct thread *td)
{
	drv_t *d;

	d = dev->si_drv1;
	CX_DEBUG2 (d, ("cx_close\n"));
	d->open_dev &= ~0x1;
	return 0;
}

static int cx_modem_status (drv_t *d)
{
	bdrv_t *bd = d->board->sys;
	int status = 0, s = splhigh ();
	CX_LOCK (bd);
	/* Already opened by someone or network interface is up? */
	if ((d->chan->mode == M_ASYNC && d->tty && (d->tty->t_state & TS_ISOPEN) &&
	    (d->open_dev|0x2)) || (d->chan->mode != M_ASYNC && d->running))
		status = TIOCM_LE;	/* always enabled while open */

	if (cx_get_dsr (d->chan)) status |= TIOCM_DSR;
	if (cx_get_cd  (d->chan)) status |= TIOCM_CD;
	if (cx_get_cts (d->chan)) status |= TIOCM_CTS;
	if (d->chan->dtr)	  status |= TIOCM_DTR;
	if (d->chan->rts)	  status |= TIOCM_RTS;
	CX_UNLOCK (bd);
	splx (s);
	return status;
}

static int cx_ioctl (struct cdev *dev, u_long cmd, caddr_t data, int flag, struct thread *td)
{
	drv_t *d;
	bdrv_t *bd;
	cx_chan_t *c;
	struct serial_statistics *st;
	int error, s;
	char mask[16];

	d = dev->si_drv1;
	c = d->chan;
		
	bd = d->board->sys;
	
	switch (cmd) {
	case SERIAL_GETREGISTERED:
		CX_DEBUG2 (d, ("ioctl: getregistered\n"));
		bzero (mask, sizeof(mask));
		for (s=0; s<NCX*NCHAN; ++s)
			if (channel [s])
				mask [s/8] |= 1 << (s & 7);
		bcopy (mask, data, sizeof (mask));
		return 0;

	case SERIAL_GETPORT:
		CX_DEBUG2 (d, ("ioctl: getport\n"));
		s = splhigh ();
		CX_LOCK (bd);
		*(int *)data = cx_get_port (c);
		CX_UNLOCK (bd);
		splx (s);
		if (*(int *)data<0)
			return (EINVAL);
		else
			return 0;

	case SERIAL_SETPORT:
		CX_DEBUG2 (d, ("ioctl: setproto\n"));
		/* Only for superuser! */
		error = priv_check (td, PRIV_DRIVER);
		if (error)
			return error;

		s = splhigh ();
		CX_LOCK (bd);
		cx_set_port (c, *(int *)data);
		CX_UNLOCK (bd);
		splx (s);
		return 0;

#ifndef NETGRAPH
	case SERIAL_GETPROTO:
		CX_DEBUG2 (d, ("ioctl: getproto\n"));
		s = splhigh ();
		CX_LOCK (bd);
		strcpy ((char*)data, (c->mode == M_ASYNC) ? "async" :
			(IFP2SP(d->ifp)->pp_flags & PP_FR) ? "fr" :
			(d->ifp->if_flags & PP_CISCO) ? "cisco" : "ppp");
		CX_UNLOCK (bd);
		splx (s);
		return 0;

	case SERIAL_SETPROTO:
		CX_DEBUG2 (d, ("ioctl: setproto\n"));
		/* Only for superuser! */
		error = priv_check (td, PRIV_DRIVER);
		if (error)
			return error;
		if (c->mode == M_ASYNC)
			return EBUSY;
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
		CX_DEBUG2 (d, ("ioctl: getkeepalive\n"));
		if ((IFP2SP(d->ifp)->pp_flags & PP_FR) ||
		    (d->ifp->if_flags & PP_CISCO) ||
		    (c->mode == M_ASYNC))
			return EINVAL;
		s = splhigh ();
		CX_LOCK (bd);
		*(int*)data = (IFP2SP(d->ifp)->pp_flags & PP_KEEPALIVE) ? 1 : 0;
		CX_UNLOCK (bd);
		splx (s);
		return 0;

	case SERIAL_SETKEEPALIVE:
		CX_DEBUG2 (d, ("ioctl: setkeepalive\n"));
		/* Only for superuser! */
		error = priv_check (td, PRIV_DRIVER);
		if (error)
			return error;
		if ((IFP2SP(d->ifp)->pp_flags & PP_FR) ||
			(d->ifp->if_flags & PP_CISCO))
			return EINVAL;
		s = splhigh ();
		CX_LOCK (bd);
		if (*(int*)data)
			IFP2SP(d->ifp)->pp_flags |= PP_KEEPALIVE;
		else
			IFP2SP(d->ifp)->pp_flags &= ~PP_KEEPALIVE;
		CX_UNLOCK (bd);
		splx (s);
		return 0;
#endif /*NETGRAPH*/

	case SERIAL_GETMODE:
		CX_DEBUG2 (d, ("ioctl: getmode\n"));
		s = splhigh ();
		CX_LOCK (bd);
		*(int*)data = (c->mode == M_ASYNC) ?
			SERIAL_ASYNC : SERIAL_HDLC;
		CX_UNLOCK (bd);
		splx (s);
		return 0;

	case SERIAL_SETMODE:
		CX_DEBUG2 (d, ("ioctl: setmode\n"));
		/* Only for superuser! */
		error = priv_check (td, PRIV_DRIVER);
		if (error)
			return error;

		/* Somebody is waiting for carrier? */
		if (d->lock)
			return EBUSY;
		/* /dev/ttyXX is already opened by someone? */
		if (c->mode == M_ASYNC && d->tty && (d->tty->t_state & TS_ISOPEN) &&
		    (d->open_dev|0x2))
			return EBUSY;
		/* Network interface is up?
		 * Cannot change to async mode. */
		if (c->mode != M_ASYNC && d->running &&
		    (*(int*)data == SERIAL_ASYNC))
			return EBUSY;

		s = splhigh ();
		CX_LOCK (bd);
		if (c->mode == M_HDLC && *(int*)data == SERIAL_ASYNC) {
			cx_set_mode (c, M_ASYNC);
			cx_enable_receive (c, 0);
			cx_enable_transmit (c, 0);
		} else if (c->mode == M_ASYNC && *(int*)data == SERIAL_HDLC) {
			if (d->ifp->if_flags & IFF_DEBUG)
				c->debug = c->debug_shadow;
			cx_set_mode (c, M_HDLC);
			cx_enable_receive (c, 1);
			cx_enable_transmit (c, 1);
		}
		CX_UNLOCK (bd);
		splx (s);
		return 0;

	case SERIAL_GETSTAT:
		CX_DEBUG2 (d, ("ioctl: getestat\n"));
		st = (struct serial_statistics*) data;
		s = splhigh ();
		CX_LOCK (bd);
		st->rintr  = c->rintr;
		st->tintr  = c->tintr;
		st->mintr  = c->mintr;
		st->ibytes = c->ibytes;
		st->ipkts  = c->ipkts;
		st->ierrs  = c->ierrs;
		st->obytes = c->obytes;
		st->opkts  = c->opkts;
		st->oerrs  = c->oerrs;
		CX_UNLOCK (bd);
		splx (s);
		return 0;

	case SERIAL_CLRSTAT:
		CX_DEBUG2 (d, ("ioctl: clrstat\n"));
		/* Only for superuser! */
		error = priv_check (td, PRIV_DRIVER);
		if (error)
			return error;
		s = splhigh ();
		CX_LOCK (bd);
		c->rintr = 0;
		c->tintr = 0;
		c->mintr = 0;
		c->ibytes = 0;
		c->ipkts = 0;
		c->ierrs = 0;
		c->obytes = 0;
		c->opkts = 0;
		c->oerrs = 0;
		CX_UNLOCK (bd);
		splx (s);
		return 0;

	case SERIAL_GETBAUD:
		CX_DEBUG2 (d, ("ioctl: getbaud\n"));
		if (c->mode == M_ASYNC)
			return EINVAL;
		s = splhigh ();
		CX_LOCK (bd);
		*(long*)data = cx_get_baud(c);
		CX_UNLOCK (bd);
		splx (s);
		return 0;

	case SERIAL_SETBAUD:
		CX_DEBUG2 (d, ("ioctl: setbaud\n"));
		/* Only for superuser! */
		error = priv_check (td, PRIV_DRIVER);
		if (error)
			return error;
		if (c->mode == M_ASYNC)
			return EINVAL;
		s = splhigh ();
		CX_LOCK (bd);
		cx_set_baud (c, *(long*)data);
		CX_UNLOCK (bd);
		splx (s);
		return 0;

	case SERIAL_GETLOOP:
		CX_DEBUG2 (d, ("ioctl: getloop\n"));
		if (c->mode == M_ASYNC)
			return EINVAL;
		s = splhigh ();
		CX_LOCK (bd);
		*(int*)data = cx_get_loop (c);
		CX_UNLOCK (bd);
		splx (s);
		return 0;

	case SERIAL_SETLOOP:
		CX_DEBUG2 (d, ("ioctl: setloop\n"));
		/* Only for superuser! */
		error = priv_check (td, PRIV_DRIVER);
		if (error)
			return error;
		if (c->mode == M_ASYNC)
			return EINVAL;
		s = splhigh ();
		CX_LOCK (bd);
		cx_set_loop (c, *(int*)data);
		CX_UNLOCK (bd);
		splx (s);
		return 0;

	case SERIAL_GETDPLL:
		CX_DEBUG2 (d, ("ioctl: getdpll\n"));
		if (c->mode == M_ASYNC)
			return EINVAL;
		s = splhigh ();
		CX_LOCK (bd);
		*(int*)data = cx_get_dpll (c);
		CX_UNLOCK (bd);
		splx (s);
		return 0;

	case SERIAL_SETDPLL:
		CX_DEBUG2 (d, ("ioctl: setdpll\n"));
		/* Only for superuser! */
		error = priv_check (td, PRIV_DRIVER);
		if (error)
			return error;
		if (c->mode == M_ASYNC)
			return EINVAL;
		s = splhigh ();
		CX_LOCK (bd);
		cx_set_dpll (c, *(int*)data);
		CX_UNLOCK (bd);
		splx (s);
		return 0;

	case SERIAL_GETNRZI:
		CX_DEBUG2 (d, ("ioctl: getnrzi\n"));
		if (c->mode == M_ASYNC)
			return EINVAL;
		s = splhigh ();
		CX_LOCK (bd);
		*(int*)data = cx_get_nrzi (c);
		CX_UNLOCK (bd);
		splx (s);
		return 0;

	case SERIAL_SETNRZI:
		CX_DEBUG2 (d, ("ioctl: setnrzi\n"));
		/* Only for superuser! */
		error = priv_check (td, PRIV_DRIVER);
		if (error)
			return error;
		if (c->mode == M_ASYNC)
			return EINVAL;
		s = splhigh ();
		CX_LOCK (bd);
		cx_set_nrzi (c, *(int*)data);
		CX_UNLOCK (bd);
		splx (s);
		return 0;

	case SERIAL_GETDEBUG:
		CX_DEBUG2 (d, ("ioctl: getdebug\n"));
		s = splhigh ();
		CX_LOCK (bd);
		*(int*)data = c->debug;
		CX_UNLOCK (bd);
		splx (s);
		return 0;

	case SERIAL_SETDEBUG:
		CX_DEBUG2 (d, ("ioctl: setdebug\n"));
		/* Only for superuser! */
		error = priv_check (td, PRIV_DRIVER);
		if (error)
			return error;
		s = splhigh ();
		CX_LOCK (bd);
#ifndef	NETGRAPH
		if (c->mode == M_ASYNC) {
			c->debug = *(int*)data;
		} else {
			/*
			 * The debug_shadow is always greater than zero for
			 * logic simplicity.  For switching debug off the
			 * IFF_DEBUG is responsible (for !M_ASYNC mode).
			 */
			c->debug_shadow = (*(int*)data) ? (*(int*)data) : 1;
			if (d->ifp->if_flags & IFF_DEBUG)
				c->debug = c->debug_shadow;
		}
#else
		c->debug = *(int*)data;
#endif
		CX_UNLOCK (bd);
		splx (s);
		return 0;
	}

	switch (cmd) {
	case TIOCSDTR:	/* Set DTR */
		CX_DEBUG2 (d, ("ioctl: tiocsdtr\n"));
		s = splhigh ();
		CX_LOCK (bd);
		cx_set_dtr (c, 1);
		CX_UNLOCK (bd);
		splx (s);
		return 0;

	case TIOCCDTR:	/* Clear DTR */
		CX_DEBUG2 (d, ("ioctl: tioccdtr\n"));
		s = splhigh ();
		CX_LOCK (bd);
		cx_set_dtr (c, 0);
		CX_UNLOCK (bd);
		splx (s);
		return 0;

	case TIOCMSET:	/* Set DTR/RTS */
		CX_DEBUG2 (d, ("ioctl: tiocmset\n"));
		s = splhigh ();
		CX_LOCK (bd);
		cx_set_dtr (c, (*(int*)data & TIOCM_DTR) ? 1 : 0);
		cx_set_rts (c, (*(int*)data & TIOCM_RTS) ? 1 : 0);
		CX_UNLOCK (bd);
		splx (s);
		return 0;

	case TIOCMBIS:	/* Add DTR/RTS */
		CX_DEBUG2 (d, ("ioctl: tiocmbis\n"));
		s = splhigh ();
		CX_LOCK (bd);
		if (*(int*)data & TIOCM_DTR) cx_set_dtr (c, 1);
		if (*(int*)data & TIOCM_RTS) cx_set_rts (c, 1);
		CX_UNLOCK (bd);
		splx (s);
		return 0;

	case TIOCMBIC:	/* Clear DTR/RTS */
		CX_DEBUG2 (d, ("ioctl: tiocmbic\n"));
		s = splhigh ();
		CX_LOCK (bd);
		if (*(int*)data & TIOCM_DTR) cx_set_dtr (c, 0);
		if (*(int*)data & TIOCM_RTS) cx_set_rts (c, 0);
		CX_UNLOCK (bd);
		splx (s);
		return 0;

	case TIOCMGET:	/* Get modem status */
		CX_DEBUG2 (d, ("ioctl: tiocmget\n"));
		*(int*)data = cx_modem_status (d);
		return 0;

	}

	CX_DEBUG2 (d, ("ioctl: 0x%lx\n", cmd));
	return ENOTTY;
}

void cx_softintr (void *unused)
{
	drv_t *d;
	bdrv_t *bd;
	async_q *q;
	int i, s, ic, k;
	while (MY_SOFT_INTR) {
		MY_SOFT_INTR = 0;
		for (i=0; i<NCX*NCHAN; ++i) {
			d = channel [i];
			if (!d || !d->chan || d->chan->type == T_NONE
			    || d->chan->mode != M_ASYNC || !d->tty
			    || !d->tty->t_dev)
				continue;
			bd = d->board->sys;
			s = splhigh ();
			CX_LOCK (bd);
			if (d->intr_action & CX_READ) {
				q = &(d->aqueue);
				if (d->tty->t_state & TS_CAN_BYPASS_L_RINT) {
					k = AQ_GSZ(q);
					if (d->tty->t_rawq.c_cc + k >
						d->tty->t_ihiwat
					    && (d->tty->t_cflag & CRTS_IFLOW
						|| d->tty->t_iflag & IXOFF)
					    && !(d->tty->t_state & TS_TBLOCK))
						ttyblock(d->tty);
					d->tty->t_rawcc += k;
					while (k>0) {
						k--;
						AQ_POP (q, ic);
						CX_UNLOCK (bd);
						splx (s);
						putc (ic, &d->tty->t_rawq);
						s = splhigh ();
						CX_LOCK (bd);
					}
					ttwakeup(d->tty);
					if (d->tty->t_state & TS_TTSTOP
					    && (d->tty->t_iflag & IXANY
						|| d->tty->t_cc[VSTART] ==
						d->tty->t_cc[VSTOP])) {
						d->tty->t_state &= ~TS_TTSTOP;
						d->tty->t_lflag &= ~FLUSHO;
						d->intr_action |= CX_WRITE;
					}
				} else {
					while (q->end != q->beg) {
						AQ_POP (q, ic);
						CX_UNLOCK (bd);
						splx (s);
						ttyld_rint (d->tty, ic);
						s = splhigh ();
						CX_LOCK (bd);
					}
				}
				d->intr_action &= ~CX_READ;
			}
			splx (s);
			CX_UNLOCK (bd);

			s = splhigh ();
			CX_LOCK (bd);
			if (d->intr_action & CX_WRITE) {
				if (d->tty->t_line)
					ttyld_start (d->tty);
				else
					cx_oproc (d->tty);
				d->intr_action &= ~CX_WRITE;
			}
			CX_UNLOCK (bd);
			splx (s);

		}
	}
}

/*
 * Fill transmitter buffer with data.
 */
static void cx_oproc (struct tty *tp)
{
	int s, k;
	drv_t *d;
	bdrv_t *bd;
	static u_char buf[DMABUFSZ];
	u_char *p;
	u_short len = 0, sublen = 0;

	d = tp->t_sc;
	bd = d->board->sys;

	CX_DEBUG2 (d, ("cx_oproc\n"));

	s = splhigh ();
	CX_LOCK (bd);

	if (tp->t_cflag & CRTSCTS && (tp->t_state & TS_TBLOCK) && d->chan->rts)
		cx_set_rts (d->chan, 0);
	else if (tp->t_cflag & CRTSCTS && ! (tp->t_state & TS_TBLOCK) && ! d->chan->rts)
		cx_set_rts (d->chan, 1);

	if (! (tp->t_state & (TS_TIMEOUT | TS_TTSTOP))) {
		/* Start transmitter. */
		cx_enable_transmit (d->chan, 1);

		/* Is it busy? */
		if (! cx_buf_free (d->chan)) {
			tp->t_state |= TS_BUSY;
			CX_UNLOCK (bd);
			splx (s);
			return;
		}
		if (tp->t_iflag & IXOFF) {
			p = (buf + (DMABUFSZ/2));
			sublen = q_to_b (&tp->t_outq, p, (DMABUFSZ/2));
			k = sublen;
			while (k--) {
				/* Send XON/XOFF out of band. */
				if (*p == tp->t_cc[VSTOP]) {
					cx_xflow_ctl (d->chan, 0);
					p++;
					continue;
				}
				if (*p == tp->t_cc[VSTART]) {
					cx_xflow_ctl (d->chan, 1);
					p++;
					continue;
				}
				buf[len] = *p;
				len++;
				p++;
			}
		} else {
			p = buf;
			len = q_to_b (&tp->t_outq, p, (DMABUFSZ/2));
		}
		if (len) {
			cx_send_packet (d->chan, buf, len, 0);
			tp->t_state |= TS_BUSY;
			d->atimeout = 10;
			CX_DEBUG2 (d, ("out %d bytes\n", len));
		}
	}
	ttwwakeup (tp);
	CX_UNLOCK (bd);
	splx (s);
}

static int cx_param (struct tty *tp, struct termios *t)
{
	drv_t *d;
	bdrv_t *bd;
	int s, bits, parity;

	d = tp->t_sc;
	bd = d->board->sys;
	
	s = splhigh ();
	CX_LOCK (bd);
	if (t->c_ospeed == 0) {
		/* Clear DTR and RTS. */
		cx_set_dtr (d->chan, 0);
		CX_UNLOCK (bd);
		splx (s);
		CX_DEBUG2 (d, ("cx_param (hangup)\n"));
		return 0;
	}
	CX_DEBUG2 (d, ("cx_param\n"));

	/* Check requested parameters. */
	if (t->c_ospeed < 300 || t->c_ospeed > 256*1024) {
		CX_UNLOCK (bd);
		splx (s);
		return EINVAL;
	}
	if (t->c_ispeed && (t->c_ispeed < 300 || t->c_ispeed > 256*1024)) {
		CX_UNLOCK (bd);
		splx (s);
		return EINVAL;
	}

  	/* And copy them to tty and channel structures. */
	tp->t_ispeed = t->c_ispeed = tp->t_ospeed = t->c_ospeed;
	tp->t_cflag = t->c_cflag;

	/* Set character length and parity mode. */
	switch (t->c_cflag & CSIZE) {
	default:
	case CS8: bits = 8; break;
	case CS7: bits = 7; break;
	case CS6: bits = 6; break;
	case CS5: bits = 5; break;
	}

	parity = ((t->c_cflag & PARENB) ? 1 : 0) *
		 (1 + ((t->c_cflag & PARODD) ? 0 : 1));

	/* Set current channel number. */
	if (! d->chan->dtr)
		cx_set_dtr (d->chan, 1);

	ttyldoptim (tp);
	cx_set_async_param (d->chan, t->c_ospeed, bits, parity, (t->c_cflag & CSTOPB),
		!(t->c_cflag & PARENB), (t->c_cflag & CRTSCTS),
		(t->c_iflag & IXON), (t->c_iflag & IXANY),
		t->c_cc[VSTART], t->c_cc[VSTOP]);
	CX_UNLOCK (bd);
	splx (s);
	return 0;
}

/*
 * Stop output on a line
 */
static void cx_stop (struct tty *tp, int flag)
{
	drv_t *d;
	bdrv_t *bd;
	int s;

	d = tp->t_sc;
	bd = d->board->sys;
	
	s = splhigh ();
	CX_LOCK (bd);
	if (tp->t_state & TS_BUSY) {
		/* Stop transmitter */
		CX_DEBUG2 (d, ("cx_stop\n"));
		cx_transmitter_ctl (d->chan, 0);
	}
	CX_UNLOCK (bd);
	splx (s);
}

/*
 * Process the (delayed) carrier signal setup.
 */
static void cx_carrier (void *arg)
{
	drv_t *d = arg;
	bdrv_t *bd = d->board->sys;
	cx_chan_t *c = d->chan;
	int s, cd;

	s = splhigh ();
	CX_LOCK (bd);
	cd = cx_get_cd (c);
	if (d->cd != cd) {
		if (cd) {
			CX_DEBUG (d, ("carrier on\n"));
			d->cd = 1;
			CX_UNLOCK (bd);
			splx (s);
			if (d->tty)
				ttyld_modem(d->tty, 1);
		} else {
			CX_DEBUG (d, ("carrier loss\n"));
			d->cd = 0;
			CX_UNLOCK (bd);
			splx (s);
			if (d->tty)
				ttyld_modem(d->tty, 0);
		}
	} else {
		CX_UNLOCK (bd);
		splx (s);
	}
}

/*
 * Modem signal callback function.
 */
static void cx_modem (cx_chan_t *c)
{
	drv_t *d = c->sys;

	if (!d || c->mode != M_ASYNC)
		return;
	/* Handle carrier detect/loss. */
	/* Carrier changed - delay processing DCD for a while
	 * to give both sides some time to initialize. */
	callout_reset (&d->dcd_timeout_handle, hz/2, cx_carrier, d);
}

#ifdef NETGRAPH
static int ng_cx_constructor (node_p node)
{
	drv_t *d = NG_NODE_PRIVATE (node);
	CX_DEBUG (d, ("Constructor\n"));
	return EINVAL;
}

static int ng_cx_newhook (node_p node, hook_p hook, const char *name)
{
	int s;
	drv_t *d = NG_NODE_PRIVATE (node);
	bdrv_t *bd = d->board->sys;

	if (d->chan->mode == M_ASYNC)
		return EINVAL;

	/* Attach debug hook */
	if (strcmp (name, NG_CX_HOOK_DEBUG) == 0) {
		NG_HOOK_SET_PRIVATE (hook, NULL);
		d->debug_hook = hook;
		return 0;
	}

	/* Check for raw hook */
	if (strcmp (name, NG_CX_HOOK_RAW) != 0)
		return EINVAL;

	NG_HOOK_SET_PRIVATE (hook, d);
	d->hook = hook;
	s = splhigh ();
	CX_LOCK (bd);
	cx_up (d);
	CX_UNLOCK (bd);
	splx (s);
	return 0;
}

static int print_modems (char *s, cx_chan_t *c, int need_header)
{
	int status = cx_modem_status (c->sys);
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

static int print_stats (char *s, cx_chan_t *c, int need_header)
{
	int length = 0;

	if (need_header)
		length += sprintf (s + length, "  Rintr   Tintr   Mintr   Ibytes   Ipkts   Ierrs   Obytes   Opkts   Oerrs\n");
	length += sprintf (s + length, "%7ld %7ld %7ld %8ld %7ld %7ld %8ld %7ld %7ld\n",
		c->rintr, c->tintr, c->mintr, c->ibytes, c->ipkts,
		c->ierrs, c->obytes, c->opkts, c->oerrs);
	return length;
}

static int print_chan (char *s, cx_chan_t *c)
{
	drv_t *d = c->sys;
	int length = 0;

	length += sprintf (s + length, "cx%d", c->board->num * NCHAN + c->num);
	if (d->chan->debug)
		length += sprintf (s + length, " debug=%d", d->chan->debug);

	if (cx_get_baud (c))
		length += sprintf (s + length, " %ld", cx_get_baud (c));
	else
		length += sprintf (s + length, " extclock");

	if (c->mode == M_HDLC) {
		length += sprintf (s + length, " dpll=%s", cx_get_dpll (c) ? "on" : "off");
		length += sprintf (s + length, " nrzi=%s", cx_get_nrzi (c) ? "on" : "off");
	}

	length += sprintf (s + length, " loop=%s", cx_get_loop (c) ? "on\n" : "off\n");
	return length;
}

static int ng_cx_rcvmsg (node_p node, item_p item, hook_p lasthook)
{
	drv_t *d = NG_NODE_PRIVATE (node);
	struct ng_mesg *msg;
	struct ng_mesg *resp = NULL;
	int error = 0;

	if (!d)
		return EINVAL;
		
	CX_DEBUG (d, ("Rcvmsg\n"));
	NGI_GET_MSG (item, msg);
	switch (msg->header.typecookie) {
	default:
		error = EINVAL;
		break;

	case NGM_CX_COOKIE:
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
			bzero (resp, dl);
			s = (resp)->data;
			l += print_chan (s + l, d->chan);
			l += print_stats (s + l, d->chan, 1);
			l += print_modems (s + l, d->chan, 1);
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

static int ng_cx_rcvdata (hook_p hook, item_p item)
{
	drv_t *d = NG_NODE_PRIVATE (NG_HOOK_NODE(hook));
	struct mbuf *m;
	struct ng_tag_prio *ptag;
	bdrv_t *bd;
	struct ifqueue *q;
	int s;

	NGI_GET_M (item, m);
	NG_FREE_ITEM (item);
	if (! NG_HOOK_PRIVATE (hook) || ! d) {
		NG_FREE_M (m);
		return ENETDOWN;
	}

	bd = d->board->sys;
	/* Check for high priority data */
	if ((ptag = (struct ng_tag_prio *)m_tag_locate(m, NGM_GENERIC_COOKIE,
	    NG_TAG_PRIO, NULL)) != NULL && (ptag->priority > NG_PRIO_CUTOFF) )
		q = &d->hi_queue;
	else
		q = &d->lo_queue;

	s = splhigh ();
	CX_LOCK (bd);
	IF_LOCK (q);
	if (_IF_QFULL (q)) {
		IF_UNLOCK (q);
		CX_UNLOCK (bd);
		splx (s);
		NG_FREE_M (m);
		return ENOBUFS;
	}
	_IF_ENQUEUE (q, m);
	IF_UNLOCK (q);
	cx_start (d);
	CX_UNLOCK (bd);
	splx (s);
	return 0;
}

static int ng_cx_rmnode (node_p node)
{
	drv_t *d = NG_NODE_PRIVATE (node);
	bdrv_t *bd;

	CX_DEBUG (d, ("Rmnode\n"));
	if (d && d->running) {
		int s = splhigh ();
		bd = d->board->sys;
		CX_LOCK (bd);
		cx_down (d);
		CX_UNLOCK (bd);
		splx (s);
	}
#ifdef	KLD_MODULE
	if (node->nd_flags & NGF_REALLY_DIE) {
		NG_NODE_SET_PRIVATE (node, NULL);
		NG_NODE_UNREF (node);
	}
	NG_NODE_REVIVE(node);		/* Persistent node */
#endif
	return 0;
}

static int ng_cx_connect (hook_p hook)
{
	drv_t *d = NG_NODE_PRIVATE (NG_HOOK_NODE (hook));

	callout_reset (&d->timeout_handle, hz, cx_watchdog_timer, d);
	return 0;
}

static int ng_cx_disconnect (hook_p hook)
{
	drv_t *d = NG_NODE_PRIVATE (NG_HOOK_NODE (hook));
	bdrv_t *bd = d->board->sys;
	int s;

	s = splhigh ();
	CX_LOCK (bd);
	if (NG_HOOK_PRIVATE (hook))
		cx_down (d);
	CX_UNLOCK (bd);
	splx (s);
	/* If we were wait it than it reasserted now, just stop it. */
	if (!callout_drain (&d->timeout_handle))
		callout_stop (&d->timeout_handle);
	return 0;
}
#endif /*NETGRAPH*/

static int cx_modevent (module_t mod, int type, void *unused)
{
	static int load_count = 0;

	switch (type) {
	case MOD_LOAD:
#ifdef NETGRAPH
		if (ng_newtype (&typestruct))
			printf ("Failed to register ng_cx\n");
#endif
		++load_count;

		callout_init (&timeout_handle, 1);
		callout_reset (&timeout_handle, hz*5, cx_timeout, 0);
		/* Software interrupt. */
		swi_add(&tty_intr_event, "cx", cx_softintr, NULL, SWI_TTY,
		    INTR_MPSAFE, &cx_fast_ih);
		break;
	case MOD_UNLOAD:
		if (load_count == 1) {
			printf ("Removing device entry for Sigma\n");
#ifdef NETGRAPH
			ng_rmtype (&typestruct);
#endif			
		}
		/* If we were wait it than it reasserted now, just stop it. */
		if (!callout_drain (&timeout_handle))
			callout_stop (&timeout_handle);
		swi_remove (cx_fast_ih);
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
	.name		= NG_CX_NODE_TYPE,
	.constructor	= ng_cx_constructor,
	.rcvmsg		= ng_cx_rcvmsg,
	.shutdown	= ng_cx_rmnode,
	.newhook	= ng_cx_newhook,
	.connect	= ng_cx_connect,
	.rcvdata	= ng_cx_rcvdata,
	.disconnect	= ng_cx_disconnect,
};
#endif /*NETGRAPH*/

#ifdef NETGRAPH
MODULE_DEPEND (ng_cx, netgraph, NG_ABI_VERSION, NG_ABI_VERSION, NG_ABI_VERSION);
#else
MODULE_DEPEND (isa_cx, sppp, 1, 1, 1);
#endif
DRIVER_MODULE (cx, isa, cx_isa_driver, cx_devclass, cx_modevent, NULL);
MODULE_VERSION (cx, 1);
