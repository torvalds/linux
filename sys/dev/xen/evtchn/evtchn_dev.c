/******************************************************************************
 * evtchn.c
 *
 * Driver for receiving and demuxing event-channel signals.
 *
 * Copyright (c) 2004-2005, K A Fraser
 * Multi-process extensions Copyright (c) 2004, Steven Smith
 * FreeBSD port Copyright (c) 2014, Roger Pau Monné
 * Fetched from git://git.kernel.org/pub/scm/linux/kernel/git/torvalds/linux.git
 * File: drivers/xen/evtchn.c
 * Git commit: 0dc0064add422bc0ef5165ebe9ece3052bbd457d
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License version 2
 * as published by the Free Software Foundation; or, when distributed
 * separately from the Linux kernel or incorporated into other
 * software packages, subject to the following license:
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this source file (the "Software"), to deal in the Software without
 * restriction, including without limitation the rights to use, copy, modify,
 * merge, publish, distribute, sublicense, and/or sell copies of the Software,
 * and to permit persons to whom the Software is furnished to do so, subject to
 * the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
 * FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS
 * IN THE SOFTWARE.
 */

#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/uio.h>
#include <sys/bus.h>
#include <sys/malloc.h>
#include <sys/kernel.h>
#include <sys/lock.h>
#include <sys/mutex.h>
#include <sys/sx.h>
#include <sys/selinfo.h>
#include <sys/poll.h>
#include <sys/conf.h>
#include <sys/fcntl.h>
#include <sys/ioccom.h>
#include <sys/rman.h>
#include <sys/tree.h>
#include <sys/module.h>
#include <sys/filio.h>
#include <sys/vnode.h>

#include <machine/intr_machdep.h>
#include <machine/xen/synch_bitops.h>

#include <xen/xen-os.h>
#include <xen/evtchn.h>
#include <xen/xen_intr.h>

#include <xen/evtchn/evtchnvar.h>

MALLOC_DEFINE(M_EVTCHN, "evtchn_dev", "Xen event channel user-space device");

struct user_evtchn;

static int evtchn_cmp(struct user_evtchn *u1, struct user_evtchn *u2);

RB_HEAD(evtchn_tree, user_evtchn);

struct per_user_data {
	struct mtx bind_mutex; /* serialize bind/unbind operations */
	struct evtchn_tree evtchns;

	/* Notification ring, accessed via /dev/xen/evtchn. */
#define EVTCHN_RING_SIZE     (PAGE_SIZE / sizeof(evtchn_port_t))
#define EVTCHN_RING_MASK(_i) ((_i)&(EVTCHN_RING_SIZE-1))
	evtchn_port_t *ring;
	unsigned int ring_cons, ring_prod, ring_overflow;
	struct sx ring_cons_mutex; /* protect against concurrent readers */
	struct mtx ring_prod_mutex; /* product against concurrent interrupts */
	struct selinfo ev_rsel;
};

struct user_evtchn {
	RB_ENTRY(user_evtchn) node;
	struct per_user_data *user;
	evtchn_port_t port;
	xen_intr_handle_t handle;
	bool enabled;
};

RB_GENERATE_STATIC(evtchn_tree, user_evtchn, node, evtchn_cmp);

static device_t evtchn_dev;

static d_read_t      evtchn_read;
static d_write_t     evtchn_write;
static d_ioctl_t     evtchn_ioctl;
static d_poll_t      evtchn_poll;
static d_open_t      evtchn_open;

static void evtchn_release(void *arg);

static struct cdevsw evtchn_devsw = {
	.d_version = D_VERSION,
	.d_open = evtchn_open,
	.d_read = evtchn_read,
	.d_write = evtchn_write,
	.d_ioctl = evtchn_ioctl,
	.d_poll = evtchn_poll,
	.d_name = "evtchn",
};

/*------------------------- Red-black tree helpers ---------------------------*/
static int
evtchn_cmp(struct user_evtchn *u1, struct user_evtchn *u2)
{

	return (u1->port - u2->port);
}

static struct user_evtchn *
find_evtchn(struct per_user_data *u, evtchn_port_t port)
{
	struct user_evtchn tmp = {
		.port = port,
	};

	return (RB_FIND(evtchn_tree, &u->evtchns, &tmp));
}

/*--------------------------- Interrupt handlers -----------------------------*/
static int
evtchn_filter(void *arg)
{
	struct user_evtchn *evtchn;

	evtchn = arg;

	if (!evtchn->enabled && bootverbose) {
		device_printf(evtchn_dev,
		    "Received upcall for disabled event channel %d\n",
		    evtchn->port);
	}

	evtchn_mask_port(evtchn->port);
	evtchn->enabled = false;

	return (FILTER_SCHEDULE_THREAD);
}

static void
evtchn_interrupt(void *arg)
{
	struct user_evtchn *evtchn;
	struct per_user_data *u;

	evtchn = arg;
	u = evtchn->user;

	/*
	 * Protect against concurrent events using this handler
	 * on different CPUs.
	 */
	mtx_lock(&u->ring_prod_mutex);
	if ((u->ring_prod - u->ring_cons) < EVTCHN_RING_SIZE) {
		u->ring[EVTCHN_RING_MASK(u->ring_prod)] = evtchn->port;
		wmb(); /* Ensure ring contents visible */
		if (u->ring_cons == u->ring_prod++) {
			wakeup(u);
			selwakeup(&u->ev_rsel);
		}
	} else
		u->ring_overflow = 1;
	mtx_unlock(&u->ring_prod_mutex);
}

/*------------------------- Character device methods -------------------------*/
static int
evtchn_open(struct cdev *dev, int flag, int otyp, struct thread *td)
{
	struct per_user_data *u;
	int error;

	u = malloc(sizeof(*u), M_EVTCHN, M_WAITOK | M_ZERO);
	u->ring = malloc(PAGE_SIZE, M_EVTCHN, M_WAITOK | M_ZERO);

	/* Initialize locks */
	mtx_init(&u->bind_mutex, "evtchn_bind_mutex", NULL, MTX_DEF);
	sx_init(&u->ring_cons_mutex, "evtchn_ringc_sx");
	mtx_init(&u->ring_prod_mutex, "evtchn_ringp_mutex", NULL, MTX_DEF);

	/* Initialize red-black tree. */
	RB_INIT(&u->evtchns);

	/* Assign the allocated per_user_data to this open instance. */
	error = devfs_set_cdevpriv(u, evtchn_release);
	if (error != 0) {
		mtx_destroy(&u->bind_mutex);
		mtx_destroy(&u->ring_prod_mutex);
		sx_destroy(&u->ring_cons_mutex);
		free(u->ring, M_EVTCHN);
		free(u, M_EVTCHN);
	}

	return (error);
}

static void
evtchn_release(void *arg)
{
	struct per_user_data *u;
	struct user_evtchn *evtchn, *tmp;

	u = arg;

	seldrain(&u->ev_rsel);

	RB_FOREACH_SAFE(evtchn, evtchn_tree, &u->evtchns, tmp) {
		xen_intr_unbind(&evtchn->handle);

		RB_REMOVE(evtchn_tree, &u->evtchns, evtchn);
		free(evtchn, M_EVTCHN);
	}

	mtx_destroy(&u->bind_mutex);
	mtx_destroy(&u->ring_prod_mutex);
	sx_destroy(&u->ring_cons_mutex);
	free(u->ring, M_EVTCHN);
	free(u, M_EVTCHN);
}

static int
evtchn_read(struct cdev *dev, struct uio *uio, int ioflag)
{
	int error, count;
	unsigned int c, p, bytes1 = 0, bytes2 = 0;
	struct per_user_data *u;

	error = devfs_get_cdevpriv((void **)&u);
	if (error != 0)
		return (EINVAL);

	/* Whole number of ports. */
	count = uio->uio_resid;
	count &= ~(sizeof(evtchn_port_t)-1);

	if (count == 0)
		return (0);

	if (count > PAGE_SIZE)
		count = PAGE_SIZE;

	sx_xlock(&u->ring_cons_mutex);
	for (;;) {
		error = EFBIG;
		if (u->ring_overflow)
			goto unlock_out;

		c = u->ring_cons;
		p = u->ring_prod;
		if (c != p)
			break;

		if (ioflag & IO_NDELAY) {
			sx_xunlock(&u->ring_cons_mutex);
			return (EWOULDBLOCK);
		}

		error = sx_sleep(u, &u->ring_cons_mutex, PCATCH, "evtchw", 0);
		if ((error != 0) && (error != EWOULDBLOCK))
			return (error);
	}

	/* Byte lengths of two chunks. Chunk split (if any) is at ring wrap. */
	if (((c ^ p) & EVTCHN_RING_SIZE) != 0) {
		bytes1 = (EVTCHN_RING_SIZE - EVTCHN_RING_MASK(c)) *
		    sizeof(evtchn_port_t);
		bytes2 = EVTCHN_RING_MASK(p) * sizeof(evtchn_port_t);
	} else {
		bytes1 = (p - c) * sizeof(evtchn_port_t);
		bytes2 = 0;
	}

	/* Truncate chunks according to caller's maximum byte count. */
	if (bytes1 > count) {
		bytes1 = count;
		bytes2 = 0;
	} else if ((bytes1 + bytes2) > count) {
		bytes2 = count - bytes1;
	}

	error = EFAULT;
	rmb(); /* Ensure that we see the port before we copy it. */

	if (uiomove(&u->ring[EVTCHN_RING_MASK(c)], bytes1, uio) ||
	    ((bytes2 != 0) && uiomove(&u->ring[0], bytes2, uio)))
		goto unlock_out;

	u->ring_cons += (bytes1 + bytes2) / sizeof(evtchn_port_t);
	error = 0;

unlock_out:
	sx_xunlock(&u->ring_cons_mutex);
	return (error);
}

static int
evtchn_write(struct cdev *dev, struct uio *uio, int ioflag)
{
	int error, i, count;
	evtchn_port_t *kbuf;
	struct per_user_data *u;

	error = devfs_get_cdevpriv((void **)&u);
	if (error != 0)
		return (EINVAL);

	kbuf = malloc(PAGE_SIZE, M_EVTCHN, M_WAITOK);

	count = uio->uio_resid;
	/* Whole number of ports. */
	count &= ~(sizeof(evtchn_port_t)-1);

	error = 0;
	if (count == 0)
		goto out;

	if (count > PAGE_SIZE)
		count = PAGE_SIZE;

	error = uiomove(kbuf, count, uio);
	if (error != 0)
		goto out;

	mtx_lock(&u->bind_mutex);

	for (i = 0; i < (count/sizeof(evtchn_port_t)); i++) {
		evtchn_port_t port = kbuf[i];
		struct user_evtchn *evtchn;

		evtchn = find_evtchn(u, port);
		if (evtchn && !evtchn->enabled) {
			evtchn->enabled = true;
			evtchn_unmask_port(evtchn->port);
		}
	}

	mtx_unlock(&u->bind_mutex);
	error = 0;

out:
	free(kbuf, M_EVTCHN);
	return (error);
}

static inline int
evtchn_bind_user_port(struct per_user_data *u, struct user_evtchn *evtchn)
{
	int error;

	evtchn->port = xen_intr_port(evtchn->handle);
	evtchn->user = u;
	evtchn->enabled = true;
	mtx_lock(&u->bind_mutex);
	RB_INSERT(evtchn_tree, &u->evtchns, evtchn);
	mtx_unlock(&u->bind_mutex);
	error = xen_intr_add_handler(device_get_nameunit(evtchn_dev),
	    evtchn_filter, evtchn_interrupt, evtchn,
	    INTR_TYPE_MISC | INTR_MPSAFE, evtchn->handle);
	if (error != 0) {
		xen_intr_unbind(&evtchn->handle);
		mtx_lock(&u->bind_mutex);
		RB_REMOVE(evtchn_tree, &u->evtchns, evtchn);
		mtx_unlock(&u->bind_mutex);
		free(evtchn, M_EVTCHN);
	}
	return (error);
}

static int
evtchn_ioctl(struct cdev *dev, unsigned long cmd, caddr_t arg,
    int mode, struct thread *td __unused)
{
	struct per_user_data *u;
	int error;

	error = devfs_get_cdevpriv((void **)&u);
	if (error != 0)
		return (EINVAL);

	switch (cmd) {
	case IOCTL_EVTCHN_BIND_VIRQ: {
		struct ioctl_evtchn_bind_virq *bind;
		struct user_evtchn *evtchn;

		evtchn = malloc(sizeof(*evtchn), M_EVTCHN, M_WAITOK | M_ZERO);

		bind = (struct ioctl_evtchn_bind_virq *)arg;

		error = xen_intr_bind_virq(evtchn_dev, bind->virq, 0,
		    NULL, NULL, NULL, 0, &evtchn->handle);
		if (error != 0) {
			free(evtchn, M_EVTCHN);
			break;
		}
		error = evtchn_bind_user_port(u, evtchn);
		if (error != 0)
			break;
		bind->port = evtchn->port;
		break;
	}

	case IOCTL_EVTCHN_BIND_INTERDOMAIN: {
		struct ioctl_evtchn_bind_interdomain *bind;
		struct user_evtchn *evtchn;

		evtchn = malloc(sizeof(*evtchn), M_EVTCHN, M_WAITOK | M_ZERO);

		bind = (struct ioctl_evtchn_bind_interdomain *)arg;

		error = xen_intr_bind_remote_port(evtchn_dev,
		    bind->remote_domain, bind->remote_port, NULL,
		    NULL, NULL, 0, &evtchn->handle);
		if (error != 0) {
			free(evtchn, M_EVTCHN);
			break;
		}
		error = evtchn_bind_user_port(u, evtchn);
		if (error != 0)
			break;
		bind->port = evtchn->port;
		break;
	}

	case IOCTL_EVTCHN_BIND_UNBOUND_PORT: {
		struct ioctl_evtchn_bind_unbound_port *bind;
		struct user_evtchn *evtchn;

		evtchn = malloc(sizeof(*evtchn), M_EVTCHN, M_WAITOK | M_ZERO);

		bind = (struct ioctl_evtchn_bind_unbound_port *)arg;

		error = xen_intr_alloc_and_bind_local_port(evtchn_dev,
		    bind->remote_domain, NULL, NULL, NULL, 0, &evtchn->handle);
		if (error != 0) {
			free(evtchn, M_EVTCHN);
			break;
		}
		error = evtchn_bind_user_port(u, evtchn);
		if (error != 0)
			break;
		bind->port = evtchn->port;
		break;
	}

	case IOCTL_EVTCHN_UNBIND: {
		struct ioctl_evtchn_unbind *unbind;
		struct user_evtchn *evtchn;

		unbind = (struct ioctl_evtchn_unbind *)arg;

		mtx_lock(&u->bind_mutex);
		evtchn = find_evtchn(u, unbind->port);
		if (evtchn == NULL) {
			error = ENOTCONN;
			break;
		}
		RB_REMOVE(evtchn_tree, &u->evtchns, evtchn);
		mtx_unlock(&u->bind_mutex);

		xen_intr_unbind(&evtchn->handle);
		free(evtchn, M_EVTCHN);
		error = 0;
		break;
	}

	case IOCTL_EVTCHN_NOTIFY: {
		struct ioctl_evtchn_notify *notify;
		struct user_evtchn *evtchn;

		notify = (struct ioctl_evtchn_notify *)arg;

		mtx_lock(&u->bind_mutex);
		evtchn = find_evtchn(u, notify->port);
		if (evtchn == NULL) {
			error = ENOTCONN;
			break;
		}

		xen_intr_signal(evtchn->handle);
		mtx_unlock(&u->bind_mutex);
		error = 0;
		break;
	}

	case IOCTL_EVTCHN_RESET: {
		/* Initialise the ring to empty. Clear errors. */
		sx_xlock(&u->ring_cons_mutex);
		mtx_lock(&u->ring_prod_mutex);
		u->ring_cons = u->ring_prod = u->ring_overflow = 0;
		mtx_unlock(&u->ring_prod_mutex);
		sx_xunlock(&u->ring_cons_mutex);
		error = 0;
		break;
	}

	case FIONBIO:
	case FIOASYNC:
		/* Handled in an upper layer */
		error = 0;
		break;

	default:
		error = ENOTTY;
		break;
	}

	return (error);
}

static int
evtchn_poll(struct cdev *dev, int events, struct thread *td)
{
	struct per_user_data *u;
	int error, mask;

	error = devfs_get_cdevpriv((void **)&u);
	if (error != 0)
		return (POLLERR);

	/* we can always write */
	mask = events & (POLLOUT | POLLWRNORM);

	mtx_lock(&u->ring_prod_mutex);
	if (events & (POLLIN | POLLRDNORM)) {
		if (u->ring_cons != u->ring_prod) {
			mask |= events & (POLLIN | POLLRDNORM);
		} else {
			/* Record that someone is waiting */
			selrecord(td, &u->ev_rsel);
		}
	}
	mtx_unlock(&u->ring_prod_mutex);

	return (mask);
}

/*------------------ Private Device Attachment Functions  --------------------*/
static void
evtchn_identify(driver_t *driver, device_t parent)
{

	KASSERT((xen_domain()),
	    ("Trying to attach evtchn device on non Xen domain"));

	evtchn_dev = BUS_ADD_CHILD(parent, 0, "evtchn", 0);
	if (evtchn_dev == NULL)
		panic("unable to attach evtchn user-space device");
}

static int
evtchn_probe(device_t dev)
{

	device_set_desc(dev, "Xen event channel user-space device");
	return (BUS_PROBE_NOWILDCARD);
}

static int
evtchn_attach(device_t dev)
{

	make_dev_credf(MAKEDEV_ETERNAL, &evtchn_devsw, 0, NULL, UID_ROOT,
	    GID_WHEEL, 0600, "xen/evtchn");
	return (0);
}

/*-------------------- Private Device Attachment Data  -----------------------*/
static device_method_t evtchn_methods[] = {
	DEVMETHOD(device_identify, evtchn_identify),
	DEVMETHOD(device_probe, evtchn_probe),
	DEVMETHOD(device_attach, evtchn_attach),

	DEVMETHOD_END
};

static driver_t evtchn_driver = {
	"evtchn",
	evtchn_methods,
	0,
};

devclass_t evtchn_devclass;

DRIVER_MODULE(evtchn, xenpv, evtchn_driver, evtchn_devclass, 0, 0);
MODULE_DEPEND(evtchn, xenpv, 1, 1, 1);
