/*-
 * SPDX-License-Identifier: BSD-3-Clause
 *
 * Copyright (c) 2006, Cisco Systems, Inc.
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without 
 * modification, are permitted provided that the following conditions 
 * are met:
 *
 * 1. Redistributions of source code must retain the above copyright 
 *    notice, this list of conditions and the following disclaimer. 
 * 2. Redistributions in binary form must reproduce the above copyright 
 *    notice, this list of conditions and the following disclaimer in the 
 *    documentation and/or other materials provided with the distribution. 
 * 3. Neither the name of Cisco Systems, Inc. nor the names of its contributors 
 *    may be used to endorse or promote products derived from this software 
 *    without specific prior written permission. 
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS" 
 * AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE 
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE 
 * ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT OWNER OR CONTRIBUTORS BE 
 * LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR 
 * CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF 
 * SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS 
 * INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN 
 * CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) 
 * ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE 
 * POSSIBILITY OF SUCH DAMAGE.
 */

#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");

#include <sys/param.h>
#include <sys/module.h>
#include <sys/systm.h>
#include <sys/mbuf.h>
#include <sys/malloc.h>
#include <sys/kernel.h>
#include <sys/socket.h>
#include <sys/queue.h>

#include <machine/vmparam.h>
#include <vm/vm.h>
#include <vm/pmap.h>

#include <machine/bus.h>
#include <machine/resource.h>
#include <machine/frame.h>

#include <sys/bus.h>
#include <sys/rman.h>

#include <machine/intr_machdep.h>

#include <machine/xen-os.h>
#include <machine/hypervisor.h>
#include <machine/hypervisor-ifs.h>
#include <machine/xen_intr.h>
#include <machine/evtchn.h>
#include <machine/xenbus.h>
#include <machine/gnttab.h>
#include <machine/xen-public/memory.h>
#include <machine/xen-public/io/pciif.h>

#include <sys/pciio.h>
#include <dev/pci/pcivar.h>
#include "pcib_if.h"

#ifdef XEN_PCIDEV_FE_DEBUG
#define DPRINTF(fmt, args...) \
    printf("pcifront (%s:%d): " fmt, __FUNCTION__, __LINE__, ##args)
#else
#define DPRINTF(fmt, args...) ((void)0)
#endif
#define WPRINTF(fmt, args...) \
    printf("pcifront (%s:%d): " fmt, __FUNCTION__, __LINE__, ##args)

#define INVALID_GRANT_REF (0)
#define INVALID_EVTCHN    (-1)
#define virt_to_mfn(x) (vtophys(x) >> PAGE_SHIFT)

struct pcifront_device {
	STAILQ_ENTRY(pcifront_device) next;

	struct xenbus_device *xdev;

	int unit;
	int evtchn;
	int gnt_ref;

	/* Lock this when doing any operations in sh_info */
	struct mtx sh_info_lock;
	struct xen_pci_sharedinfo *sh_info;

	device_t ndev;

	int ref_cnt;
};

static STAILQ_HEAD(pcifront_dlist, pcifront_device) pdev_list = STAILQ_HEAD_INITIALIZER(pdev_list);

struct xpcib_softc {
	int domain;
	int bus;
	struct pcifront_device *pdev;
};

/* Allocate a PCI device structure */
static struct pcifront_device *
alloc_pdev(struct xenbus_device *xdev)
{
	struct pcifront_device *pdev = NULL;
	int err, unit;

	err = sscanf(xdev->nodename, "device/pci/%d", &unit);
	if (err != 1) {
		if (err == 0)
			err = -EINVAL;
		xenbus_dev_fatal(pdev->xdev, err, "Error scanning pci device instance number");
		goto out;
	}

	pdev = (struct pcifront_device *)malloc(sizeof(struct pcifront_device), M_DEVBUF, M_NOWAIT);
	if (pdev == NULL) {
		err = -ENOMEM;
		xenbus_dev_fatal(xdev, err, "Error allocating pcifront_device struct");
		goto out;
	}
	pdev->unit = unit;
	pdev->xdev = xdev;
	pdev->ref_cnt = 1;

	pdev->sh_info = (struct xen_pci_sharedinfo *)malloc(PAGE_SIZE, M_DEVBUF, M_NOWAIT);
	if (pdev->sh_info == NULL) {
		free(pdev, M_DEVBUF);
		pdev = NULL;
		err = -ENOMEM;
		xenbus_dev_fatal(xdev, err, "Error allocating sh_info struct");
		goto out;
	}
	pdev->sh_info->flags = 0;

	xdev->data = pdev;

	mtx_init(&pdev->sh_info_lock, "info_lock", "pci shared dev info lock", MTX_DEF);

	pdev->evtchn = INVALID_EVTCHN;
	pdev->gnt_ref = INVALID_GRANT_REF;

	STAILQ_INSERT_TAIL(&pdev_list, pdev, next);

	DPRINTF("Allocated pdev @ 0x%p (unit=%d)\n", pdev, unit);

 out:
	return pdev;
}

/* Hold a reference to a pcifront device */
static void
get_pdev(struct pcifront_device *pdev)
{
	pdev->ref_cnt++;
}

/* Release a reference to a pcifront device */
static void
put_pdev(struct pcifront_device *pdev)
{
	if (--pdev->ref_cnt > 0)
		return;

	DPRINTF("freeing pdev @ 0x%p (ref_cnt=%d)\n", pdev, pdev->ref_cnt);

	if (pdev->evtchn != INVALID_EVTCHN)
		xenbus_free_evtchn(pdev->xdev, pdev->evtchn);

	if (pdev->gnt_ref != INVALID_GRANT_REF)
		gnttab_end_foreign_access(pdev->gnt_ref, 0, (void *)pdev->sh_info);

	pdev->xdev->data = NULL;

	free(pdev, M_DEVBUF);
}


/* Write to the xenbus info needed by backend */
static int
pcifront_publish_info(struct pcifront_device *pdev)
{
	int err = 0;
	struct xenbus_transaction *trans;

	err = xenbus_grant_ring(pdev->xdev, virt_to_mfn(pdev->sh_info));
	if (err < 0) {
		WPRINTF("error granting access to ring page\n");
		goto out;
	}

	pdev->gnt_ref = err;

	err = xenbus_alloc_evtchn(pdev->xdev, &pdev->evtchn);
	if (err)
		goto out;

 do_publish:
	trans = xenbus_transaction_start();
	if (IS_ERR(trans)) {
		xenbus_dev_fatal(pdev->xdev, err,
						 "Error writing configuration for backend "
						 "(start transaction)");
		goto out;
	}

	err = xenbus_printf(trans, pdev->xdev->nodename,
						"pci-op-ref", "%u", pdev->gnt_ref);
	if (!err)
		err = xenbus_printf(trans, pdev->xdev->nodename,
							"event-channel", "%u", pdev->evtchn);
	if (!err)
		err = xenbus_printf(trans, pdev->xdev->nodename,
							"magic", XEN_PCI_MAGIC);
	if (!err)
		err = xenbus_switch_state(pdev->xdev, trans,
								  XenbusStateInitialised);

	if (err) {
		xenbus_transaction_end(trans, 1);
		xenbus_dev_fatal(pdev->xdev, err,
						 "Error writing configuration for backend");
		goto out;
	} else {
		err = xenbus_transaction_end(trans, 0);
		if (err == -EAGAIN)
			goto do_publish;
		else if (err) {
			xenbus_dev_fatal(pdev->xdev, err,
							 "Error completing transaction for backend");
			goto out;
		}
	}

 out:
	return err;
}

/* The backend is now connected so complete the connection process on our side */
static int
pcifront_connect(struct pcifront_device *pdev)
{
	device_t nexus;
	devclass_t nexus_devclass;

	/* We will add our device as a child of the nexus0 device */
	if (!(nexus_devclass = devclass_find("nexus")) ||
		!(nexus = devclass_get_device(nexus_devclass, 0))) {
		WPRINTF("could not find nexus0!\n");
		return -1;
	}

	/* Create a newbus device representing this frontend instance */
	pdev->ndev = BUS_ADD_CHILD(nexus, 0, "xpcife", pdev->unit);
	if (!pdev->ndev) {
		WPRINTF("could not create xpcife%d!\n", pdev->unit);
		return -EFAULT;
	}
	get_pdev(pdev);
	device_set_ivars(pdev->ndev, pdev);

	/* Good to go connected now */
	xenbus_switch_state(pdev->xdev, NULL, XenbusStateConnected);

	printf("pcifront: connected to %s\n", pdev->xdev->nodename);

	mtx_lock(&Giant);
	device_probe_and_attach(pdev->ndev);
	mtx_unlock(&Giant);

	return 0;
}

/* The backend is closing so process a disconnect */
static int
pcifront_disconnect(struct pcifront_device *pdev)
{
	int err = 0;
	XenbusState prev_state;

	prev_state = xenbus_read_driver_state(pdev->xdev->nodename);

	if (prev_state < XenbusStateClosing) {
		err = xenbus_switch_state(pdev->xdev, NULL, XenbusStateClosing);
		if (!err && prev_state == XenbusStateConnected) {
			/* TODO - need to detach the newbus devices */
		}
	}

	return err;
}

/* Process a probe from the xenbus */
static int
pcifront_probe(struct xenbus_device *xdev,
			   const struct xenbus_device_id *id)
{
	int err = 0;
	struct pcifront_device *pdev;

	DPRINTF("xenbus probing\n");

	if ((pdev = alloc_pdev(xdev)) == NULL)
		goto out;

	err = pcifront_publish_info(pdev);

 out:
	if (err)
		put_pdev(pdev);
	return err;
}

/* Remove the xenbus PCI device */
static int
pcifront_remove(struct xenbus_device *xdev)
{
	DPRINTF("removing xenbus device node (%s)\n", xdev->nodename);
	if (xdev->data)
		put_pdev(xdev->data);
	return 0;
}

/* Called by xenbus when our backend node changes state */
static void
pcifront_backend_changed(struct xenbus_device *xdev,
						 XenbusState be_state)
{
	struct pcifront_device *pdev = xdev->data;

	switch (be_state) {
	case XenbusStateClosing:
		DPRINTF("backend closing (%s)\n", xdev->nodename);
		pcifront_disconnect(pdev);
		break;

	case XenbusStateClosed:
		DPRINTF("backend closed (%s)\n", xdev->nodename);
		pcifront_disconnect(pdev);
		break;

	case XenbusStateConnected:
		DPRINTF("backend connected (%s)\n", xdev->nodename);
		pcifront_connect(pdev);
		break;
		
	default:
		break;
	}
}

/* Process PCI operation */
static int
do_pci_op(struct pcifront_device *pdev, struct xen_pci_op *op)
{
	int err = 0;
	struct xen_pci_op *active_op = &pdev->sh_info->op;
	evtchn_port_t port = pdev->evtchn;
	time_t timeout;

	mtx_lock(&pdev->sh_info_lock);

	memcpy(active_op, op, sizeof(struct xen_pci_op));

	/* Go */
	wmb();
	set_bit(_XEN_PCIF_active, (unsigned long *)&pdev->sh_info->flags);
	notify_remote_via_evtchn(port);

	timeout = time_uptime + 2;

	clear_evtchn(port);

	/* Spin while waiting for the answer */
	while (test_bit
	       (_XEN_PCIF_active, (unsigned long *)&pdev->sh_info->flags)) {
		int err = HYPERVISOR_poll(&port, 1, 3 * hz);
		if (err)
			panic("Failed HYPERVISOR_poll: err=%d", err);
		clear_evtchn(port);
		if (time_uptime > timeout) {
			WPRINTF("pciback not responding!!!\n");
			clear_bit(_XEN_PCIF_active,
				  (unsigned long *)&pdev->sh_info->flags);
			err = XEN_PCI_ERR_dev_not_found;
			goto out;
		}
	}

	memcpy(op, active_op, sizeof(struct xen_pci_op));

	err = op->err;
 out:
	mtx_unlock(&pdev->sh_info_lock);
	return err;
}

/* ** XenBus Driver registration ** */

static struct xenbus_device_id pcifront_ids[] = {
	{ "pci" },
	{ "" }
};

static struct xenbus_driver pcifront = {
	.name = "pcifront",
	.ids = pcifront_ids,
	.probe = pcifront_probe,
	.remove = pcifront_remove,
	.otherend_changed = pcifront_backend_changed,
};

/* Register the driver with xenbus during sys init */
static void
pcifront_init(void *unused)
{
	if ((xen_start_info->flags & SIF_INITDOMAIN))
		return;

	DPRINTF("xenbus registering\n");

	xenbus_register_frontend(&pcifront);
}

SYSINIT(pciif, SI_SUB_PSEUDO, SI_ORDER_ANY, pcifront_init, NULL)


/* Newbus xpcife device driver probe */
static int
xpcife_probe(device_t dev)
{
#ifdef XEN_PCIDEV_FE_DEBUG
	struct pcifront_device *pdev = (struct pcifront_device *)device_get_ivars(dev);
	DPRINTF("xpcife probe (unit=%d)\n", pdev->unit);
#endif
	return (BUS_PROBE_NOWILDCARD);
}

/* Newbus xpcife device driver attach */
static int
xpcife_attach(device_t dev) 
{
	struct pcifront_device *pdev = (struct pcifront_device *)device_get_ivars(dev);
	int i, num_roots, len, err;
	char str[64];
	unsigned int domain, bus;

	DPRINTF("xpcife attach (unit=%d)\n", pdev->unit);

	err = xenbus_scanf(NULL, pdev->xdev->otherend,
					   "root_num", "%d", &num_roots);
	if (err != 1) {
		if (err == 0)
			err = -EINVAL;
		xenbus_dev_fatal(pdev->xdev, err,
						 "Error reading number of PCI roots");
		goto out;
	}

	/* Add a pcib device for each root */
	for (i = 0; i < num_roots; i++) {
		device_t child;

		len = snprintf(str, sizeof(str), "root-%d", i);
		if (unlikely(len >= (sizeof(str) - 1))) {
			err = -ENOMEM;
			goto out;
		}

		err = xenbus_scanf(NULL, pdev->xdev->otherend, str,
						   "%x:%x", &domain, &bus);
		if (err != 2) {
			if (err >= 0)
				err = -EINVAL;
			xenbus_dev_fatal(pdev->xdev, err,
							 "Error reading PCI root %d", i);
			goto out;
		}
		err = 0;
		if (domain != pdev->xdev->otherend_id) {
			err = -EINVAL;
			xenbus_dev_fatal(pdev->xdev, err,
							 "Domain mismatch %d != %d", domain, pdev->xdev->otherend_id);
			goto out;
		}
		
		child = device_add_child(dev, "pcib", bus);
		if (!child) {
			err = -ENOMEM;
			xenbus_dev_fatal(pdev->xdev, err,
							 "Unable to create pcib%d", bus);
			goto out;
		}
	}

 out:
	return bus_generic_attach(dev);
}

static devclass_t xpcife_devclass;

static device_method_t xpcife_methods[] = {
	/* Device interface */
	DEVMETHOD(device_probe, xpcife_probe),
	DEVMETHOD(device_attach, xpcife_attach),
	DEVMETHOD(device_detach,	bus_generic_detach),
	DEVMETHOD(device_shutdown,	bus_generic_shutdown),
	DEVMETHOD(device_suspend,	bus_generic_suspend),
	DEVMETHOD(device_resume,	bus_generic_resume),
    /* Bus interface */
    DEVMETHOD(bus_alloc_resource,	bus_generic_alloc_resource),
    DEVMETHOD(bus_release_resource,	bus_generic_release_resource),
    DEVMETHOD(bus_activate_resource,	bus_generic_activate_resource),
    DEVMETHOD(bus_deactivate_resource,	bus_generic_deactivate_resource),
    DEVMETHOD(bus_setup_intr,		bus_generic_setup_intr),
    DEVMETHOD(bus_teardown_intr,	bus_generic_teardown_intr),

	DEVMETHOD_END
};

static driver_t xpcife_driver = {
	"xpcife",
	xpcife_methods,
	0,
};

DRIVER_MODULE(xpcife, nexus, xpcife_driver, xpcife_devclass, 0, 0);


/* Newbus xen pcib device driver probe */
static int
xpcib_probe(device_t dev)
{
	struct xpcib_softc *sc = (struct xpcib_softc *)device_get_softc(dev);
	struct pcifront_device *pdev = (struct pcifront_device *)device_get_ivars(device_get_parent(dev));

	DPRINTF("xpcib probe (bus=%d)\n", device_get_unit(dev));

	sc->domain = pdev->xdev->otherend_id;
	sc->bus = device_get_unit(dev);
	sc->pdev = pdev;
	
	return 0;
}

/* Newbus xen pcib device driver attach */
static int
xpcib_attach(device_t dev) 
{
	struct xpcib_softc *sc = (struct xpcib_softc *)device_get_softc(dev);

	DPRINTF("xpcib attach (bus=%d)\n", sc->bus);

	device_add_child(dev, "pci", -1);
	return bus_generic_attach(dev);
}

static int
xpcib_read_ivar(device_t dev, device_t child, int which, uintptr_t *result)
{
	struct xpcib_softc *sc = (struct xpcib_softc *)device_get_softc(dev);
	switch (which) {
	case  PCIB_IVAR_BUS:
		*result = sc->bus;
		return 0;
	}
	return ENOENT;
}

/* Return the number of slots supported */
static int
xpcib_maxslots(device_t dev)
{
	return 31;
}

#define PCI_DEVFN(slot,func)	((((slot) & 0x1f) << 3) | ((func) & 0x07))

/* Read configuration space register */
static u_int32_t
xpcib_read_config(device_t dev, int bus, int slot, int func,
				  int reg, int bytes)
{
	struct xpcib_softc *sc = (struct xpcib_softc *)device_get_softc(dev);
	struct xen_pci_op op = {
		.cmd    = XEN_PCI_OP_conf_read,
		.domain = sc->domain,
		.bus    = sc->bus,
		.devfn  = PCI_DEVFN(slot, func),
		.offset = reg,
		.size   = bytes,
	};
	int err;

	err = do_pci_op(sc->pdev, &op);
	
	DPRINTF("read config (b=%d, s=%d, f=%d, reg=%d, len=%d, val=%x, err=%d)\n",
			bus, slot, func, reg, bytes, op.value, err);

	if (err)
		op.value = ~0;

	return op.value;
}

/* Write configuration space register */
static void
xpcib_write_config(device_t dev, int bus, int slot, int func,
				   int reg, u_int32_t data, int bytes)
{
	struct xpcib_softc *sc = (struct xpcib_softc *)device_get_softc(dev);
	struct xen_pci_op op = {
		.cmd    = XEN_PCI_OP_conf_write,
		.domain = sc->domain,
		.bus    = sc->bus,
		.devfn  = PCI_DEVFN(slot, func),
		.offset = reg,
		.size   = bytes,
		.value  = data,
	};
	int err;

	err = do_pci_op(sc->pdev, &op);

	DPRINTF("write config (b=%d, s=%d, f=%d, reg=%d, len=%d, val=%x, err=%d)\n",
			bus, slot, func, reg, bytes, data, err);
}

static int
xpcib_route_interrupt(device_t pcib, device_t dev, int pin)
{
	struct pci_devinfo *dinfo = device_get_ivars(dev);
	pcicfgregs *cfg = &dinfo->cfg;

	DPRINTF("route intr (pin=%d, line=%d)\n", pin, cfg->intline);

	return cfg->intline;
}

static device_method_t xpcib_methods[] = {
    /* Device interface */
    DEVMETHOD(device_probe,		xpcib_probe),
    DEVMETHOD(device_attach,		xpcib_attach),
    DEVMETHOD(device_detach,		bus_generic_detach),
    DEVMETHOD(device_shutdown,		bus_generic_shutdown),
    DEVMETHOD(device_suspend,		bus_generic_suspend),
    DEVMETHOD(device_resume,		bus_generic_resume),

    /* Bus interface */
    DEVMETHOD(bus_read_ivar,		xpcib_read_ivar),
    DEVMETHOD(bus_alloc_resource,	bus_generic_alloc_resource),
    DEVMETHOD(bus_activate_resource,	bus_generic_activate_resource),
    DEVMETHOD(bus_release_resource,	bus_generic_release_resource),
    DEVMETHOD(bus_deactivate_resource,	bus_generic_deactivate_resource),
    DEVMETHOD(bus_setup_intr,		bus_generic_setup_intr),
    DEVMETHOD(bus_teardown_intr,	bus_generic_teardown_intr),

    /* pcib interface */
    DEVMETHOD(pcib_maxslots,		xpcib_maxslots),
    DEVMETHOD(pcib_read_config,		xpcib_read_config),
    DEVMETHOD(pcib_write_config,	xpcib_write_config),
    DEVMETHOD(pcib_route_interrupt,	xpcib_route_interrupt),
	DEVMETHOD(pcib_request_feature,	pcib_request_feature_allow),

    DEVMETHOD_END
};

static devclass_t xpcib_devclass;

DEFINE_CLASS_0(pcib, xpcib_driver, xpcib_methods, sizeof(struct xpcib_softc));
DRIVER_MODULE(pcib, xpcife, xpcib_driver, xpcib_devclass, 0, 0);

/*
 * Local variables:
 * mode: C
 * c-set-style: "BSD"
 * c-basic-offset: 4
 * tab-width: 4
 * indent-tabs-mode: t
 * End:
 */
