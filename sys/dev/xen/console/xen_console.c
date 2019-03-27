/*
 * Copyright (c) 2015 Julien Grall <julien.grall@citrix.com>
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
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR AND CONTRIBUTORS AS IS'' AND
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
#include <sys/module.h>
#include <sys/systm.h>
#include <sys/consio.h>
#include <sys/priv.h>
#include <sys/proc.h>
#include <sys/uio.h>
#include <sys/tty.h>
#include <sys/systm.h>
#include <sys/taskqueue.h>
#include <sys/conf.h>
#include <sys/kernel.h>
#include <sys/bus.h>
#include <sys/cons.h>
#include <sys/kdb.h>
#include <sys/proc.h>

#include <machine/stdarg.h>

#include <vm/vm.h>
#include <vm/pmap.h>

#include <xen/xen-os.h>
#include <xen/hypervisor.h>
#include <xen/xen_intr.h>
#include <xen/interface/io/console.h>

#include "opt_ddb.h"
#include "opt_printf.h"

#ifdef DDB
#include <ddb/ddb.h>
#endif

static char driver_name[] = "xc";

struct xencons_priv;

typedef void xencons_early_init_t(struct xencons_priv *cons);
typedef int xencons_init_t(device_t dev, struct tty *tp,
    driver_intr_t intr_handler);
typedef int xencons_read_t(struct xencons_priv *cons, char *buffer,
    unsigned int size);
typedef int xencons_write_t(struct xencons_priv *cons, const char *buffer,
    unsigned int size);

struct xencons_ops {
	/*
	 * Called by the low-level driver during early boot.
	 * Only the minimal set up to get a console should be done here.
	 */
	xencons_early_init_t	*early_init;
	/* Prepare the console to be fully use */
	xencons_init_t		*init;
	/* Read/write helpers */
	xencons_read_t		*read;
	xencons_write_t		*write;
};

struct xencons_priv {
	/* Mutex to protect the shared ring and the internal buffers */
	struct mtx			mtx;
	/* Interrupt handler used for notify the backend */
	xen_intr_handle_t		intr_handle;
	/* KDB internal state */
#ifdef KDB
	int				altbrk;
#endif
	/* Status of the tty */
	bool				opened;
	/* Callout used when the write buffer is full */
	struct callout			callout;

	/* Internal buffers must be used with mtx locked */
#define WBUF_SIZE     4096
#define WBUF_MASK(_i) ((_i)&(WBUF_SIZE-1))
	char				wbuf[WBUF_SIZE];
	unsigned int			wc, wp; /* Consumer/producer wbuf */

#define RBUF_SIZE     1024
#define RBUF_MASK(_i) ((_i)&(RBUF_SIZE-1))
	char				rbuf[RBUF_SIZE];
	unsigned int			rc, rp; /* Consumer/producer rbuf */

	/* Pointer to the console operations */
	const struct xencons_ops	*ops;

	/*
	 * Ring specific fields
	 * XXX: make an union?
	 */
	/* Event channel number for early notification (PV only) */
	uint32_t			evtchn;
	/* Console shared page */
	struct xencons_interface	*intf;
};

/*
 * Data for the main console
 * Necessary to support low-level console driver
 */
static struct xencons_priv main_cons;

#define XC_POLLTIME 	(hz/10)

/*----------------------------- Debug function ------------------------------*/
struct putchar_arg {
	char	*buf;
	size_t	size;
	size_t	n_next;
};

static void
putchar(int c, void *arg)
{
	struct putchar_arg *pca;

	pca = (struct putchar_arg *)arg;

	if (pca->buf == NULL) {
		/*
		 * We have no buffer, output directly to the
		 * console char by char.
		 */
		HYPERVISOR_console_write((char *)&c, 1);
	} else {
		pca->buf[pca->n_next++] = c;
		if ((pca->size == pca->n_next) || (c = '\0')) {
			/* Flush the buffer */
			HYPERVISOR_console_write(pca->buf, pca->n_next);
			pca->n_next = 0;
		}
	}
}

void
xc_printf(const char *fmt, ...)
{
	va_list ap;
	struct putchar_arg pca;
#ifdef PRINTF_BUFR_SIZE
	char buf[PRINTF_BUFR_SIZE];

	pca.buf = buf;
	pca.size = sizeof(buf);
	pca.n_next = 0;
#else
	pca.buf = NULL;
	pca.size = 0;
#endif

	KASSERT((xen_domain()), ("call to xc_printf from non Xen guest"));

	va_start(ap, fmt);
	kvprintf(fmt, putchar, &pca, 10, ap);
	va_end(ap);

#ifdef PRINTF_BUFR_SIZE
	if (pca.n_next != 0)
		HYPERVISOR_console_write(buf, pca.n_next);
#endif
}

/*---------------------- Helpers for the console lock -----------------------*/
/*
 * The lock is not used when the kernel is panicing as it will never recover
 * and we want to output no matter what it costs.
 */
static inline void xencons_lock(struct xencons_priv *cons)
{

	if (panicstr == NULL)
		mtx_lock_spin(&cons->mtx);

}

static inline void xencons_unlock(struct xencons_priv *cons)
{

	if (panicstr == NULL)
		mtx_unlock_spin(&cons->mtx);
}

#define xencons_lock_assert(cons)	mtx_assert(&(cons)->mtx, MA_OWNED)

/*------------------ Helpers for the hypervisor console ---------------------*/
static void
xencons_early_init_hypervisor(struct xencons_priv *cons)
{
	/*
	 * Nothing to setup for the low-level console when using
	 * the hypervisor console.
	 */
}

static int
xencons_init_hypervisor(device_t dev, struct tty *tp,
    driver_intr_t intr_handler)
{
	struct xencons_priv *cons;
	int err;

	cons = tty_softc(tp);

	err = xen_intr_bind_virq(dev, VIRQ_CONSOLE, 0, NULL,
	    intr_handler, tp, INTR_TYPE_TTY | INTR_MPSAFE, &cons->intr_handle);
	if (err != 0)
		device_printf(dev, "Can't register console interrupt\n");

	return (err);
}

static int
xencons_write_hypervisor(struct xencons_priv *cons, const char *buffer,
    unsigned int size)
{

	HYPERVISOR_console_io(CONSOLEIO_write, size, buffer);

	return (size);
}

static int
xencons_read_hypervisor(struct xencons_priv *cons, char *buffer,
    unsigned int size)
{

	xencons_lock_assert(cons);

	return (HYPERVISOR_console_io(CONSOLEIO_read, size, buffer));
}

static const struct xencons_ops xencons_hypervisor_ops = {
	.early_init	= xencons_early_init_hypervisor,
	.init		= xencons_init_hypervisor,
	.read		= xencons_read_hypervisor,
	.write		= xencons_write_hypervisor,
};

/*------------------ Helpers for the ring console ---------------------------*/
static void
xencons_early_init_ring(struct xencons_priv *cons)
{
	cons->intf = pmap_mapdev_attr(ptoa(xen_get_console_mfn()), PAGE_SIZE,
	    PAT_WRITE_BACK);
	cons->evtchn = xen_get_console_evtchn();
}

static int
xencons_init_ring(device_t dev, struct tty *tp, driver_intr_t intr_handler)
{
	struct xencons_priv *cons;
	int err;

	cons = tty_softc(tp);

	if (cons->evtchn == 0)
		return (ENODEV);

	err = xen_intr_bind_local_port(dev, cons->evtchn, NULL,
	    intr_handler, tp, INTR_TYPE_TTY | INTR_MPSAFE, &cons->intr_handle);
	if (err != 0)
		return (err);

	return (0);
}

static void
xencons_notify_ring(struct xencons_priv *cons)
{
	/*
	 * The console may be used before the ring interrupt is properly
	 * initialized.
	 * If so, fallback to directly use the event channel hypercall.
	 */
	if (__predict_true(cons->intr_handle != NULL))
		xen_intr_signal(cons->intr_handle);
	else {
		struct evtchn_send send = {
			.port = cons->evtchn
		};

		HYPERVISOR_event_channel_op(EVTCHNOP_send, &send);
	}
}

static int
xencons_write_ring(struct xencons_priv *cons, const char *buffer,
    unsigned int size)
{
	struct xencons_interface *intf;
	XENCONS_RING_IDX wcons, wprod;
	int sent;

	intf = cons->intf;

	xencons_lock_assert(cons);

	wcons = intf->out_cons;
	wprod = intf->out_prod;

	mb();
	KASSERT((wprod - wcons) <= sizeof(intf->out),
		("console send ring inconsistent"));

	for (sent = 0; sent < size; sent++, wprod++) {
		if ((wprod - wcons) >= sizeof(intf->out))
			break;
		intf->out[MASK_XENCONS_IDX(wprod, intf->out)] = buffer[sent];
	}

	wmb();
	intf->out_prod = wprod;

	xencons_notify_ring(cons);

	return (sent);
}

static int
xencons_read_ring(struct xencons_priv *cons, char *buffer, unsigned int size)
{
	struct xencons_interface *intf;
	XENCONS_RING_IDX rcons, rprod;
	unsigned int rsz;

	intf = cons->intf;

	xencons_lock_assert(cons);

	rcons = intf->in_cons;
	rprod = intf->in_prod;
	rmb();

	for (rsz = 0; rsz < size; rsz++, rcons++) {
		if (rprod == rcons)
			break;
		buffer[rsz] = intf->in[MASK_XENCONS_IDX(rcons, intf->in)];
	}

	wmb();
	intf->in_cons = rcons;

	/* No need to notify the backend if nothing has been read */
	if (rsz != 0)
		xencons_notify_ring(cons);

	return (rsz);
}

static const struct xencons_ops xencons_ring_ops = {
	.early_init	= xencons_early_init_ring,
	.init		= xencons_init_ring,
	.read		= xencons_read_ring,
	.write		= xencons_write_ring,
};

/*------------------ Common implementation of the console -------------------*/

/*
 * Called by the low-level driver during early boot to initialize the
 * main console driver.
 * Only the minimal set up to get a console should be done here.
 */
static void
xencons_early_init(void)
{

	mtx_init(&main_cons.mtx, "XCONS LOCK", NULL, MTX_SPIN);

	if (xen_get_console_evtchn() == 0)
		main_cons.ops = &xencons_hypervisor_ops;
	else
		main_cons.ops = &xencons_ring_ops;

	main_cons.ops->early_init(&main_cons);
}

/*
 * Receive character from the console and put them in the internal buffer
 * XXX: Handle overflow of the internal buffer
 */
static void
xencons_rx(struct xencons_priv *cons)
{
	char buf[16];
	int sz;

	xencons_lock(cons);
	while ((sz = cons->ops->read(cons, buf, sizeof(buf))) > 0) {
		int i;

		for (i = 0; i < sz; i++)
			cons->rbuf[RBUF_MASK(cons->rp++)] = buf[i];
	}
	xencons_unlock(cons);
}

/* Return true if the write buffer is full */
static bool
xencons_tx_full(struct xencons_priv *cons)
{
	unsigned int used;

	xencons_lock(cons);
	used = cons->wp - cons->wc;
	xencons_unlock(cons);

	return (used >= WBUF_SIZE);
}

static void
xencons_tx_flush(struct xencons_priv *cons, int force)
{
	int        sz;

	xencons_lock(cons);
	while (cons->wc != cons->wp) {
		int sent;
		sz = cons->wp - cons->wc;
		if (sz > (WBUF_SIZE - WBUF_MASK(cons->wc)))
			sz = WBUF_SIZE - WBUF_MASK(cons->wc);
		sent = cons->ops->write(cons, &cons->wbuf[WBUF_MASK(cons->wc)],
		    sz);

		/*
		 * The other end may not have been initialized. Ignore
		 * the force.
		 */
		if (__predict_false(sent < 0))
			break;

		/*
		 * If force is set, spin until the console data is
		 * flushed through the domain controller.
		 */
		if (sent == 0 && __predict_true(!force))
			break;

		cons->wc += sent;
	}
	xencons_unlock(cons);
}

static bool
xencons_putc(struct xencons_priv *cons, int c, bool force_flush)
{

	xencons_lock(cons);
	if ((cons->wp - cons->wc) < WBUF_SIZE)
		cons->wbuf[WBUF_MASK(cons->wp++)] = c;
	xencons_unlock(cons);

	xencons_tx_flush(cons, force_flush);

	return (xencons_tx_full(cons));
}

static int
xencons_getc(struct xencons_priv *cons)
{
	int ret;

	xencons_lock(cons);
	if (cons->rp != cons->rc) {
		/* We need to return only one char */
		ret = (int)cons->rbuf[RBUF_MASK(cons->rc)];
		cons->rc++;
	} else {
		ret = -1;
	}

	xencons_unlock(cons);

	return (ret);
}

static bool
xencons_tx(struct tty *tp)
{
	bool cons_full;
	char c;
	struct xencons_priv *cons;

	cons = tty_softc(tp);

	tty_lock_assert(tp, MA_OWNED);

	/*
	 * Don't transmit any character if the buffer is full. Otherwise,
	 * characters may be lost
	 */
	if (xencons_tx_full(cons))
		return (false);

	cons_full = false;
	while (!cons_full && ttydisc_getc(tp, &c, 1) == 1)
		cons_full = xencons_putc(cons, c, false);

	return (!cons_full);
}

static void
xencons_intr(void *arg)
{
	struct tty *tp;
	struct xencons_priv *cons;
	int ret;

	tp = arg;
	cons = tty_softc(tp);

	/*
	 * The input will be used by the low-level console when KDB is active
	 */
	if (kdb_active)
		return;

	/*
	 * It's not necessary to retrieve input when the tty is not opened
	 */
	if (!cons->opened)
		return;

	xencons_rx(cons);

	tty_lock(tp);
	while ((ret = xencons_getc(cons)) != -1) {
#ifdef KDB
		kdb_alt_break(ret, &cons->altbrk);
#endif
		ttydisc_rint(tp, ret, 0);
	}
	ttydisc_rint_done(tp);
	tty_unlock(tp);

	/* Try to flush remaining characters if necessary */
	xencons_tx_flush(cons, 0);
}

/*
 * Helpers to call while shutting down:
 *	- Force flush all output
 */
static void
xencons_shutdown(void *arg, int howto)
{
	struct tty *tp;

	tp = arg;

	xencons_tx_flush(tty_softc(tp), 1);
}

/*---------------------- Low-level console driver ---------------------------*/
static void
xencons_cnprobe(struct consdev *cp)
{

	if (!xen_domain())
		return;

	cp->cn_pri = CN_REMOTE;
	sprintf(cp->cn_name, "%s0", driver_name);
}

static void
xencons_cninit(struct consdev *cp)
{

	xencons_early_init();
}

static void
xencons_cnterm(struct consdev *cp)
{
}

static void
xencons_cngrab(struct consdev *cp)
{
}

static void
xencons_cnungrab(struct consdev *cp)
{
}

static int
xencons_cngetc(struct consdev *dev)
{

	xencons_rx(&main_cons);

	return (xencons_getc(&main_cons));
}

static void
xencons_cnputc(struct consdev *dev, int c)
{
	/*
	 * The low-level console is used by KDB and panic. We have to ensure
	 * that any character sent will be seen by the backend.
	 */
	xencons_putc(&main_cons, c, true);
}

CONSOLE_DRIVER(xencons);

/*----------------------------- TTY driver ---------------------------------*/

static int
xencons_tty_open(struct tty *tp)
{
	struct xencons_priv *cons;

	cons = tty_softc(tp);

	cons->opened = true;

	return (0);
}

static void
xencons_tty_close(struct tty *tp)
{
	struct xencons_priv *cons;

	cons = tty_softc(tp);

	cons->opened = false;
}

static void
xencons_timeout(void *v)
{
	struct tty *tp;
	struct xencons_priv *cons;

	tp = v;
	cons = tty_softc(tp);

	if (!xencons_tx(tp))
		callout_reset(&cons->callout, XC_POLLTIME,
		    xencons_timeout, tp);
}

static void
xencons_tty_outwakeup(struct tty *tp)
{
	struct xencons_priv *cons;

	cons = tty_softc(tp);

	callout_stop(&cons->callout);

	if (!xencons_tx(tp))
		callout_reset(&cons->callout, XC_POLLTIME,
		    xencons_timeout, tp);
}

static struct ttydevsw xencons_ttydevsw = {
        .tsw_flags	= TF_NOPREFIX,
        .tsw_open	= xencons_tty_open,
        .tsw_close	= xencons_tty_close,
        .tsw_outwakeup	= xencons_tty_outwakeup,
};

/*------------------------ Main console driver ------------------------------*/
static void
xencons_identify(driver_t *driver, device_t parent)
{
	device_t child;

	if (main_cons.ops == NULL)
		return;

	child = BUS_ADD_CHILD(parent, 0, driver_name, 0);
}

static int
xencons_probe(device_t dev)
{

	device_set_desc(dev, "Xen Console");
	return (BUS_PROBE_NOWILDCARD);
}

static int
xencons_attach(device_t dev)
{
	struct tty *tp;
	/*
	 * The main console is already allocated statically in order to
	 * support low-level console
	 */
	struct xencons_priv *cons;
	int err;

	cons = &main_cons;

	tp = tty_alloc(&xencons_ttydevsw, cons);
	tty_makedev(tp, NULL, "%s%r", driver_name, 0);
	device_set_softc(dev, tp);

	callout_init_mtx(&cons->callout, tty_getlock(tp), 0);

	err = cons->ops->init(dev, tp, xencons_intr);
	if (err != 0) {
		device_printf(dev, "Unable to initialize the console (%d)\n",
		    err);
		return (err);
	}

	/* register handler to flush console on shutdown */
	if ((EVENTHANDLER_REGISTER(shutdown_post_sync, xencons_shutdown,
	    tp, SHUTDOWN_PRI_DEFAULT)) == NULL)
		device_printf(dev, "shutdown event registration failed!\n");

	return (0);
}

static int
xencons_resume(device_t dev)
{
	struct xencons_priv *cons;
	struct tty *tp;
	int err;

	tp = device_get_softc(dev);
	cons = tty_softc(tp);
	xen_intr_unbind(&cons->intr_handle);

	err = cons->ops->init(dev, tp, xencons_intr);
	if (err != 0) {
		device_printf(dev, "Unable to resume the console (%d)\n", err);
		return (err);
	}

	return (0);
}

static devclass_t xencons_devclass;

static device_method_t xencons_methods[] = {
	DEVMETHOD(device_identify, xencons_identify),
	DEVMETHOD(device_probe, xencons_probe),
	DEVMETHOD(device_attach, xencons_attach),
	DEVMETHOD(device_resume, xencons_resume),

	DEVMETHOD_END
};

static driver_t xencons_driver = {
	driver_name,
	xencons_methods,
	0,
};

DRIVER_MODULE(xc, xenpv, xencons_driver, xencons_devclass, 0, 0);
