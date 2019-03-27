/*-
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD
 *
 * Copyright (c) 2013 Rui Paulo <rpaulo@FreeBSD.org>
 * Copyright (c) 2017 Manuel Stuehn
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
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR ``AS IS'' AND ANY EXPRESS OR
 * IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED
 * WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
 * DISCLAIMED.  IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR ANY DIRECT,
 * INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES
 * (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR
 * SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT,
 * STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN
 * ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
 * POSSIBILITY OF SUCH DAMAGE.
 */
#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");

#include <sys/poll.h>
#include <sys/time.h>
#include <sys/uio.h>
#include <sys/param.h>
#include <sys/systm.h>
#include <sys/fcntl.h>
#include <sys/bus.h>
#include <sys/conf.h>
#include <sys/kernel.h>
#include <sys/module.h>
#include <sys/malloc.h>
#include <sys/rman.h>
#include <sys/types.h>
#include <sys/sysctl.h>
#include <sys/event.h>
#include <sys/selinfo.h>
#include <machine/bus.h>
#include <machine/cpu.h>
#include <machine/frame.h>
#include <machine/intr.h>
#include <machine/atomic.h>

#include <dev/ofw/openfirm.h>
#include <dev/ofw/ofw_bus.h>
#include <dev/ofw/ofw_bus_subr.h>

#include <arm/ti/ti_prcm.h>
#include <arm/ti/ti_pruss.h>

#ifdef DEBUG
#define	DPRINTF(fmt, ...)	do {	\
	printf("%s: ", __func__);	\
	printf(fmt, __VA_ARGS__);	\
} while (0)
#else
#define	DPRINTF(fmt, ...)
#endif

static d_open_t			ti_pruss_irq_open;
static d_read_t			ti_pruss_irq_read;
static d_poll_t			ti_pruss_irq_poll;

static device_probe_t		ti_pruss_probe;
static device_attach_t		ti_pruss_attach;
static device_detach_t		ti_pruss_detach;
static void			ti_pruss_intr(void *);
static d_open_t			ti_pruss_open;
static d_mmap_t			ti_pruss_mmap;
static void 			ti_pruss_irq_kqread_detach(struct knote *);
static int 			ti_pruss_irq_kqevent(struct knote *, long);
static d_kqfilter_t		ti_pruss_irq_kqfilter;
static void			ti_pruss_privdtor(void *data);

#define	TI_PRUSS_PRU_IRQS 2
#define	TI_PRUSS_HOST_IRQS 8
#define	TI_PRUSS_IRQS (TI_PRUSS_HOST_IRQS+TI_PRUSS_PRU_IRQS)
#define	TI_PRUSS_EVENTS 64
#define	NOT_SET_STR "NONE"
#define	TI_TS_ARRAY 16

struct ctl
{
	size_t cnt;
	size_t idx;
};

struct ts_ring_buf
{
	struct ctl ctl;
	uint64_t ts[TI_TS_ARRAY];
};

struct ti_pruss_irqsc
{
	struct mtx		sc_mtx;
	struct cdev		*sc_pdev;
	struct selinfo		sc_selinfo;
	int8_t			channel;
	int8_t			last;
	int8_t			event;
	bool			enable;
	struct ts_ring_buf	tstamps;
};

static struct cdevsw ti_pruss_cdevirq = {
	.d_version =	D_VERSION,
	.d_name =	"ti_pruss_irq",
	.d_open =	ti_pruss_irq_open,
	.d_read =	ti_pruss_irq_read,
	.d_poll =	ti_pruss_irq_poll,
	.d_kqfilter =	ti_pruss_irq_kqfilter,
};

struct ti_pruss_softc {
	struct mtx		sc_mtx;
	struct resource 	*sc_mem_res;
	struct resource 	*sc_irq_res[TI_PRUSS_HOST_IRQS];
	void            	*sc_intr[TI_PRUSS_HOST_IRQS];
	struct ti_pruss_irqsc	sc_irq_devs[TI_PRUSS_IRQS];
	bus_space_tag_t		sc_bt;
	bus_space_handle_t	sc_bh;
	struct cdev		*sc_pdev;
	struct selinfo		sc_selinfo;
	bool			sc_glob_irqen;
};

static struct cdevsw ti_pruss_cdevsw = {
	.d_version =	D_VERSION,
	.d_name =	"ti_pruss",
	.d_open =	ti_pruss_open,
	.d_mmap =	ti_pruss_mmap,
};

static device_method_t ti_pruss_methods[] = {
	DEVMETHOD(device_probe,		ti_pruss_probe),
	DEVMETHOD(device_attach,	ti_pruss_attach),
	DEVMETHOD(device_detach,	ti_pruss_detach),

	DEVMETHOD_END
};

static driver_t ti_pruss_driver = {
	"ti_pruss",
	ti_pruss_methods,
	sizeof(struct ti_pruss_softc)
};

static devclass_t ti_pruss_devclass;

DRIVER_MODULE(ti_pruss, simplebus, ti_pruss_driver, ti_pruss_devclass, 0, 0);
MODULE_DEPEND(ti_pruss, ti_prcm, 1, 1, 1);

static struct resource_spec ti_pruss_irq_spec[] = {
	{ SYS_RES_IRQ,	    0,  RF_ACTIVE },
	{ SYS_RES_IRQ,	    1,  RF_ACTIVE },
	{ SYS_RES_IRQ,	    2,  RF_ACTIVE },
	{ SYS_RES_IRQ,	    3,  RF_ACTIVE },
	{ SYS_RES_IRQ,	    4,  RF_ACTIVE },
	{ SYS_RES_IRQ,	    5,  RF_ACTIVE },
	{ SYS_RES_IRQ,	    6,  RF_ACTIVE },
	{ SYS_RES_IRQ,	    7,  RF_ACTIVE },
	{ -1,               0,  0 }
};
CTASSERT(TI_PRUSS_HOST_IRQS == nitems(ti_pruss_irq_spec) - 1);

static int
ti_pruss_irq_open(struct cdev *dev, int oflags, int devtype, struct thread *td)
{
	struct ctl* irqs;
	struct ti_pruss_irqsc *sc;
	sc = dev->si_drv1;

	irqs = malloc(sizeof(struct ctl), M_DEVBUF, M_WAITOK);
	if (!irqs)
	    return (ENOMEM);

	irqs->cnt = sc->tstamps.ctl.cnt;
	irqs->idx = sc->tstamps.ctl.idx;

	return devfs_set_cdevpriv(irqs, ti_pruss_privdtor);
}

static void
ti_pruss_privdtor(void *data)
{
    free(data, M_DEVBUF);
}

static int
ti_pruss_irq_poll(struct cdev *dev, int events, struct thread *td)
{
	struct ctl* irqs;
	struct ti_pruss_irqsc *sc;
	sc = dev->si_drv1;

	devfs_get_cdevpriv((void**)&irqs);

	if (events & (POLLIN | POLLRDNORM)) {
		if (sc->tstamps.ctl.cnt != irqs->cnt)
			return events & (POLLIN | POLLRDNORM);
		else
			selrecord(td, &sc->sc_selinfo);
	}
	return 0;
}

static int
ti_pruss_irq_read(struct cdev *cdev, struct uio *uio, int ioflag)
{
	const size_t ts_len = sizeof(uint64_t);
	struct ti_pruss_irqsc* irq;
	struct ctl* priv;
	int error = 0;
	size_t idx;
	ssize_t level;

	irq = cdev->si_drv1;

	if (uio->uio_resid < ts_len)
		return (EINVAL);

	error = devfs_get_cdevpriv((void**)&priv);
	if (error)
	    return (error);

	mtx_lock(&irq->sc_mtx);

	if (irq->tstamps.ctl.cnt - priv->cnt > TI_TS_ARRAY)
	{
		priv->cnt = irq->tstamps.ctl.cnt;
		priv->idx = irq->tstamps.ctl.idx;
		mtx_unlock(&irq->sc_mtx);
		return (ENXIO);
	}

	do {
		idx = priv->idx;
		level = irq->tstamps.ctl.idx - idx;
		if (level < 0)
			level += TI_TS_ARRAY;

		if (level == 0) {
			if (ioflag & O_NONBLOCK) {
				mtx_unlock(&irq->sc_mtx);
				return (EWOULDBLOCK);
			}

			error = msleep(irq, &irq->sc_mtx, PCATCH | PDROP,
				"pruirq", 0);
			if (error)
				return error;

			mtx_lock(&irq->sc_mtx);
		}
	}while(level == 0);

	mtx_unlock(&irq->sc_mtx);

	error = uiomove(&irq->tstamps.ts[idx], ts_len, uio);

	if (++idx == TI_TS_ARRAY)
		idx = 0;
	priv->idx = idx;

	atomic_add_32(&priv->cnt, 1);

	return (error);
}

static struct ti_pruss_irq_arg {
	int 		       irq;
	struct ti_pruss_softc *sc;
} ti_pruss_irq_args[TI_PRUSS_IRQS];

static __inline uint32_t
ti_pruss_reg_read(struct ti_pruss_softc *sc, uint32_t reg)
{
	return (bus_space_read_4(sc->sc_bt, sc->sc_bh, reg));
}

static __inline void
ti_pruss_reg_write(struct ti_pruss_softc *sc, uint32_t reg, uint32_t val)
{
	bus_space_write_4(sc->sc_bt, sc->sc_bh, reg, val);
}

static __inline void
ti_pruss_interrupts_clear(struct ti_pruss_softc *sc)
{
	/* disable global interrupt */
	ti_pruss_reg_write(sc, PRUSS_INTC_GER, 0 );

	/* clear all events */
	ti_pruss_reg_write(sc, PRUSS_INTC_SECR0, 0xFFFFFFFF);
	ti_pruss_reg_write(sc, PRUSS_INTC_SECR1, 0xFFFFFFFF);

	/* disable all host interrupts */
	ti_pruss_reg_write(sc, PRUSS_INTC_HIER, 0);
}

static __inline int
ti_pruss_interrupts_enable(struct ti_pruss_softc *sc, int8_t irq, bool enable)
{
	if (enable && ((sc->sc_irq_devs[irq].channel == -1) ||
	    (sc->sc_irq_devs[irq].event== -1)))
	{
		device_printf( sc->sc_pdev->si_drv1,
			"Interrupt chain not fully configured, not possible to enable\n" );
		return (EINVAL);
	}

	sc->sc_irq_devs[irq].enable = enable;

	if (sc->sc_irq_devs[irq].sc_pdev) {
		destroy_dev(sc->sc_irq_devs[irq].sc_pdev);
		sc->sc_irq_devs[irq].sc_pdev = NULL;
	}

	if (enable) {
		sc->sc_irq_devs[irq].sc_pdev = make_dev(&ti_pruss_cdevirq, 0, UID_ROOT, GID_WHEEL,
		    0600, "pruss%d.irq%d", device_get_unit(sc->sc_pdev->si_drv1), irq);
		sc->sc_irq_devs[irq].sc_pdev->si_drv1 = &sc->sc_irq_devs[irq];

		sc->sc_irq_devs[irq].tstamps.ctl.idx = 0;
	}

	uint32_t reg = enable ? PRUSS_INTC_HIEISR : PRUSS_INTC_HIDISR;
	ti_pruss_reg_write(sc, reg, sc->sc_irq_devs[irq].channel);

	reg = enable ? PRUSS_INTC_EISR : PRUSS_INTC_EICR;
	ti_pruss_reg_write(sc, reg, sc->sc_irq_devs[irq].event );

	return (0);
}

static __inline void
ti_pruss_map_write(struct ti_pruss_softc *sc, uint32_t basereg, uint8_t index, uint8_t content)
{
	const size_t regadr = basereg + index & ~0x03;
	const size_t bitpos = (index & 0x03) * 8;
	uint32_t rmw = ti_pruss_reg_read(sc, regadr);
	rmw = (rmw & ~( 0xF << bitpos)) | ( (content & 0xF) << bitpos);
	ti_pruss_reg_write(sc, regadr, rmw);
}

static int
ti_pruss_event_map( SYSCTL_HANDLER_ARGS )
{
	struct ti_pruss_softc *sc;
	const int8_t irq = arg2;
	int err;
	char event[sizeof(NOT_SET_STR)];

	sc = arg1;

	if(sc->sc_irq_devs[irq].event == -1)
		bcopy(NOT_SET_STR, event, sizeof(event));
	else
		snprintf(event, sizeof(event), "%d", sc->sc_irq_devs[irq].event);

	err = sysctl_handle_string(oidp, event, sizeof(event), req);
	if(err != 0)
		return (err);

	if (req->newptr) {  // write event
		if (strcmp(NOT_SET_STR, event) == 0) {
			ti_pruss_interrupts_enable(sc, irq, false);
			sc->sc_irq_devs[irq].event = -1;
		} else {
			if (sc->sc_irq_devs[irq].channel == -1) {
				device_printf( sc->sc_pdev->si_drv1,
					"corresponding channel not configured\n");
				return (ENXIO);
			}

			const int8_t channelnr = sc->sc_irq_devs[irq].channel;
			const int8_t eventnr = strtol( event, NULL, 10 ); // TODO: check if strol is valid
			if (eventnr > TI_PRUSS_EVENTS || eventnr < 0) {
				device_printf( sc->sc_pdev->si_drv1,
					"Event number %d not valid (0 - %d)",
					channelnr, TI_PRUSS_EVENTS -1);
				return (EINVAL);
			}

			sc->sc_irq_devs[irq].channel = channelnr;
			sc->sc_irq_devs[irq].event = eventnr;

			// event[nr] <= channel
			ti_pruss_map_write(sc, PRUSS_INTC_CMR_BASE,
			    eventnr, channelnr);
		}
	}
	return (err);
}

static int
ti_pruss_channel_map(SYSCTL_HANDLER_ARGS)
{
	struct ti_pruss_softc *sc;
	int err;
	char channel[sizeof(NOT_SET_STR)];
	const int8_t irq = arg2;

	sc = arg1;

	if (sc->sc_irq_devs[irq].channel == -1)
		bcopy(NOT_SET_STR, channel, sizeof(channel));
	else
		snprintf(channel, sizeof(channel), "%d", sc->sc_irq_devs[irq].channel);

	err = sysctl_handle_string(oidp, channel, sizeof(channel), req);
	if (err != 0)
		return (err);

	if (req->newptr) { // write event
		if (strcmp(NOT_SET_STR, channel) == 0) {
			ti_pruss_interrupts_enable(sc, irq, false);
			ti_pruss_reg_write(sc, PRUSS_INTC_HIDISR,
			    sc->sc_irq_devs[irq].channel);
			sc->sc_irq_devs[irq].channel = -1;
		} else {
			const int8_t channelnr = strtol(channel, NULL, 10); // TODO: check if strol is valid
			if (channelnr > TI_PRUSS_IRQS || channelnr < 0)
			{
				device_printf(sc->sc_pdev->si_drv1,
					"Channel number %d not valid (0 - %d)",
					channelnr, TI_PRUSS_IRQS-1);
				return (EINVAL);
			}

			sc->sc_irq_devs[irq].channel = channelnr;
			sc->sc_irq_devs[irq].last = -1;

			// channel[nr] <= irqnr
			ti_pruss_map_write(sc, PRUSS_INTC_HMR_BASE,
				irq, channelnr);
		}
	}

	return (err);
}

static int
ti_pruss_interrupt_enable(SYSCTL_HANDLER_ARGS)
{
	struct ti_pruss_softc *sc;
	int err;
	bool irqenable;
	const int8_t irq = arg2;

	sc = arg1;
	irqenable = sc->sc_irq_devs[arg2].enable;

	err = sysctl_handle_bool(oidp, &irqenable, arg2, req);
	if (err != 0)
		return (err);

	if (req->newptr) // write enable
		return ti_pruss_interrupts_enable(sc, irq, irqenable);

	return (err);
}

static int
ti_pruss_global_interrupt_enable(SYSCTL_HANDLER_ARGS)
{
	struct ti_pruss_softc *sc;
	int err;
	bool glob_irqen;

	sc = arg1;
	glob_irqen = sc->sc_glob_irqen;

	err = sysctl_handle_bool(oidp, &glob_irqen, arg2, req);
	if (err != 0)
		return (err);

	if (req->newptr) {
		sc->sc_glob_irqen = glob_irqen;
		ti_pruss_reg_write(sc, PRUSS_INTC_GER, glob_irqen);
	}

	return (err);
}
static int
ti_pruss_probe(device_t dev)
{

	if (!ofw_bus_status_okay(dev))
		return (ENXIO);

	if (ofw_bus_is_compatible(dev, "ti,pruss-v1") ||
	    ofw_bus_is_compatible(dev, "ti,pruss-v2")) {
		device_set_desc(dev, "TI Programmable Realtime Unit Subsystem");
		return (BUS_PROBE_DEFAULT);
	}

	return (ENXIO);
}

static int
ti_pruss_attach(device_t dev)
{
	struct ti_pruss_softc *sc;
	int rid, i;

	if (ti_prcm_clk_enable(PRUSS_CLK) != 0) {
		device_printf(dev, "could not enable PRUSS clock\n");
		return (ENXIO);
	}
	sc = device_get_softc(dev);
	rid = 0;
	mtx_init(&sc->sc_mtx, "TI PRUSS", NULL, MTX_DEF);
	sc->sc_mem_res = bus_alloc_resource_any(dev, SYS_RES_MEMORY, &rid,
	    RF_ACTIVE);
	if (sc->sc_mem_res == NULL) {
		device_printf(dev, "could not allocate memory resource\n");
		return (ENXIO);
	}

	struct sysctl_ctx_list *clist = device_get_sysctl_ctx(dev);
	if (!clist)
		return (EINVAL);

	struct sysctl_oid *poid;
	poid = device_get_sysctl_tree( dev );
	if (!poid)
		return (EINVAL);

	sc->sc_glob_irqen = false;
	struct sysctl_oid *irq_root = SYSCTL_ADD_NODE(clist, SYSCTL_CHILDREN(poid),
	    OID_AUTO, "irq", CTLFLAG_RD, 0,
	    "PRUSS Host Interrupts");
	SYSCTL_ADD_PROC(clist, SYSCTL_CHILDREN(poid), OID_AUTO,
	    "global_interrupt_enable", CTLFLAG_RW | CTLTYPE_U8,
	    sc, 0, ti_pruss_global_interrupt_enable,
	    "CU", "Global interrupt enable");

	sc->sc_bt = rman_get_bustag(sc->sc_mem_res);
	sc->sc_bh = rman_get_bushandle(sc->sc_mem_res);
	if (bus_alloc_resources(dev, ti_pruss_irq_spec, sc->sc_irq_res) != 0) {
		device_printf(dev, "could not allocate interrupt resource\n");
		ti_pruss_detach(dev);
		return (ENXIO);
	}

	ti_pruss_interrupts_clear(sc);

	for (i = 0; i < TI_PRUSS_IRQS; i++) {
		char name[8];
		snprintf(name, sizeof(name), "%d", i);

		struct sysctl_oid *irq_nodes = SYSCTL_ADD_NODE(clist, SYSCTL_CHILDREN(irq_root),
		    OID_AUTO, name, CTLFLAG_RD, 0,
		    "PRUSS Interrupts");
		SYSCTL_ADD_PROC(clist, SYSCTL_CHILDREN(irq_nodes), OID_AUTO,
		    "channel", CTLFLAG_RW | CTLTYPE_STRING, sc, i, ti_pruss_channel_map,
		    "A", "Channel attached to this irq");
		SYSCTL_ADD_PROC(clist, SYSCTL_CHILDREN(irq_nodes), OID_AUTO,
		    "event", CTLFLAG_RW | CTLTYPE_STRING, sc, i, ti_pruss_event_map,
		    "A", "Event attached to this irq");
		SYSCTL_ADD_PROC(clist, SYSCTL_CHILDREN(irq_nodes), OID_AUTO,
		    "enable", CTLFLAG_RW | CTLTYPE_U8, sc, i, ti_pruss_interrupt_enable,
		    "CU", "Enable/Disable interrupt");

		sc->sc_irq_devs[i].event = -1;
		sc->sc_irq_devs[i].channel = -1;
		sc->sc_irq_devs[i].tstamps.ctl.idx = 0;

		if (i < TI_PRUSS_HOST_IRQS) {
			ti_pruss_irq_args[i].irq = i;
			ti_pruss_irq_args[i].sc = sc;
			if (bus_setup_intr(dev, sc->sc_irq_res[i],
			    INTR_MPSAFE | INTR_TYPE_MISC,
			    NULL, ti_pruss_intr, &ti_pruss_irq_args[i],
			    &sc->sc_intr[i]) != 0) {
				device_printf(dev,
				    "unable to setup the interrupt handler\n");
				ti_pruss_detach(dev);

				return (ENXIO);
			}
			mtx_init(&sc->sc_irq_devs[i].sc_mtx, "TI PRUSS IRQ", NULL, MTX_DEF);
			knlist_init_mtx(&sc->sc_irq_devs[i].sc_selinfo.si_note, &sc->sc_irq_devs[i].sc_mtx);
		}
	}

	if (ti_pruss_reg_read(sc, PRUSS_AM33XX_INTC) == PRUSS_AM33XX_REV)
		device_printf(dev, "AM33xx PRU-ICSS\n");

	sc->sc_pdev = make_dev(&ti_pruss_cdevsw, 0, UID_ROOT, GID_WHEEL,
	    0600, "pruss%d", device_get_unit(dev));
	sc->sc_pdev->si_drv1 = dev;

	/*  Acc. to datasheet always write 1 to polarity registers */
	ti_pruss_reg_write(sc, PRUSS_INTC_SIPR0, 0xFFFFFFFF);
	ti_pruss_reg_write(sc, PRUSS_INTC_SIPR1, 0xFFFFFFFF);

	/* Acc. to datasheet always write 0 to event type registers */
	ti_pruss_reg_write(sc, PRUSS_INTC_SITR0, 0);
	ti_pruss_reg_write(sc, PRUSS_INTC_SITR1, 0);

	return (0);
}

static int
ti_pruss_detach(device_t dev)
{
	struct ti_pruss_softc *sc = device_get_softc(dev);

	ti_pruss_interrupts_clear(sc);

	for (int i = 0; i < TI_PRUSS_HOST_IRQS; i++) {
		ti_pruss_interrupts_enable( sc, i, false );

		if (sc->sc_intr[i])
			bus_teardown_intr(dev, sc->sc_irq_res[i], sc->sc_intr[i]);
		if (sc->sc_irq_res[i])
			bus_release_resource(dev, SYS_RES_IRQ,
			    rman_get_rid(sc->sc_irq_res[i]),
			    sc->sc_irq_res[i]);
		knlist_clear(&sc->sc_irq_devs[i].sc_selinfo.si_note, 0);
		mtx_lock(&sc->sc_irq_devs[i].sc_mtx);
		if (!knlist_empty(&sc->sc_irq_devs[i].sc_selinfo.si_note))
			printf("IRQ %d KQueue not empty!\n", i );
		mtx_unlock(&sc->sc_irq_devs[i].sc_mtx);
		knlist_destroy(&sc->sc_irq_devs[i].sc_selinfo.si_note);
		mtx_destroy(&sc->sc_irq_devs[i].sc_mtx);
	}

	mtx_destroy(&sc->sc_mtx);
	if (sc->sc_mem_res)
		bus_release_resource(dev, SYS_RES_MEMORY, rman_get_rid(sc->sc_mem_res),
		    sc->sc_mem_res);
	if (sc->sc_pdev)
		destroy_dev(sc->sc_pdev);

	return (0);
}

static void
ti_pruss_intr(void *arg)
{
	int val;
	struct ti_pruss_irq_arg *iap = arg;
	struct ti_pruss_softc *sc = iap->sc;
	/*
	 * Interrupts pr1_host_intr[0:7] are mapped to
	 * Host-2 to Host-9 of PRU-ICSS IRQ-controller.
	 */
	const int pru_int = iap->irq + TI_PRUSS_PRU_IRQS;
	const int pru_int_mask = (1 << pru_int);
	const int pru_channel = sc->sc_irq_devs[pru_int].channel;
	const int pru_event = sc->sc_irq_devs[pru_channel].event;

	val = ti_pruss_reg_read(sc, PRUSS_INTC_HIER);
	if (!(val & pru_int_mask))
		return;

	ti_pruss_reg_write(sc, PRUSS_INTC_HIDISR, pru_int);
	ti_pruss_reg_write(sc, PRUSS_INTC_SICR, pru_event);
	ti_pruss_reg_write(sc, PRUSS_INTC_HIEISR, pru_int);

	struct ti_pruss_irqsc* irq = &sc->sc_irq_devs[pru_channel];
	size_t wr = irq->tstamps.ctl.idx;

	struct timespec ts;
	nanouptime(&ts);
	irq->tstamps.ts[wr] = ts.tv_sec * 1000000000 + ts.tv_nsec;

	if (++wr == TI_TS_ARRAY)
		wr = 0;
	atomic_add_32(&irq->tstamps.ctl.cnt, 1);

	irq->tstamps.ctl.idx = wr;

	KNOTE_UNLOCKED(&irq->sc_selinfo.si_note, pru_int);
	wakeup(irq);
	selwakeup(&irq->sc_selinfo);
}

static int
ti_pruss_open(struct cdev *cdev __unused, int oflags __unused,
    int devtype __unused, struct thread *td __unused)
{
	return (0);
}

static int
ti_pruss_mmap(struct cdev *cdev, vm_ooffset_t offset, vm_paddr_t *paddr,
    int nprot, vm_memattr_t *memattr)
{
	device_t dev = cdev->si_drv1;
	struct ti_pruss_softc *sc = device_get_softc(dev);

	if (offset >= rman_get_size(sc->sc_mem_res))
		return (ENOSPC);
	*paddr = rman_get_start(sc->sc_mem_res) + offset;
	*memattr = VM_MEMATTR_UNCACHEABLE;

	return (0);
}

static struct filterops ti_pruss_kq_read = {
	.f_isfd = 1,
	.f_detach = ti_pruss_irq_kqread_detach,
	.f_event = ti_pruss_irq_kqevent,
};

static void
ti_pruss_irq_kqread_detach(struct knote *kn)
{
	struct ti_pruss_irqsc *sc = kn->kn_hook;

	knlist_remove(&sc->sc_selinfo.si_note, kn, 0);
}

static int
ti_pruss_irq_kqevent(struct knote *kn, long hint)
{
    struct ti_pruss_irqsc* irq_sc;
    int notify;

    irq_sc = kn->kn_hook;

    if (hint > 0)
        kn->kn_data = hint - 2;

    if (hint > 0 || irq_sc->last > 0)
        notify = 1;
    else
        notify = 0;

    irq_sc->last = hint;

    return (notify);
}

static int
ti_pruss_irq_kqfilter(struct cdev *cdev, struct knote *kn)
{
	struct ti_pruss_irqsc *sc = cdev->si_drv1;

	switch (kn->kn_filter) {
	case EVFILT_READ:
		kn->kn_hook = sc;
		kn->kn_fop = &ti_pruss_kq_read;
		knlist_add(&sc->sc_selinfo.si_note, kn, 0);
		break;
	default:
		return (EINVAL);
	}

	return (0);
}
