/*	$OpenBSD: usb.c,v 1.134 2024/12/22 22:36:23 kirill Exp $	*/
/*	$NetBSD: usb.c,v 1.77 2003/01/01 00:10:26 thorpej Exp $	*/

/*
 * Copyright (c) 1998, 2002 The NetBSD Foundation, Inc.
 * All rights reserved.
 *
 * This code is derived from software contributed to The NetBSD Foundation
 * by Lennart Augustsson (lennart@augustsson.net) at
 * Carlstedt Research & Technology.
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
 * THIS SOFTWARE IS PROVIDED BY THE NETBSD FOUNDATION, INC. AND CONTRIBUTORS
 * ``AS IS'' AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED
 * TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR
 * PURPOSE ARE DISCLAIMED.  IN NO EVENT SHALL THE FOUNDATION OR CONTRIBUTORS
 * BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
 * CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 * SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
 * INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
 * CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 * ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
 * POSSIBILITY OF SUCH DAMAGE.
 */

/*
 * USB specifications and other documentation can be found at
 * https://www.usb.org/documents
 */

#include "ohci.h"
#include "uhci.h"
#include "ehci.h"

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/malloc.h>
#include <sys/device.h>
#include <sys/kthread.h>
#include <sys/conf.h>
#include <sys/fcntl.h>
#include <sys/time.h>
#include <sys/rwlock.h>

#include <dev/usb/usb.h>
#include <dev/usb/usbdi.h>

#include <machine/bus.h>

#include <dev/usb/usbdivar.h>
#include <dev/usb/usb_mem.h>
#include <dev/usb/usbpcap.h>

#ifdef USB_DEBUG
#define DPRINTF(x)	do { if (usbdebug) printf x; } while (0)
#define DPRINTFN(n,x)	do { if (usbdebug>(n)) printf x; } while (0)
int	usbdebug = 0;
#if defined(UHCI_DEBUG) && NUHCI > 0
extern int	uhcidebug;
#endif
#if defined(OHCI_DEBUG) && NOHCI > 0
extern int	ohcidebug;
#endif
#if defined(EHCI_DEBUG) && NEHCI > 0
extern int	ehcidebug;
#endif
/*
 * 0  - do usual exploration
 * !0 - do no exploration
 */
int	usb_noexplore = 0;
#else
#define DPRINTF(x)
#define DPRINTFN(n,x)
#endif

struct usb_softc {
	struct device	 sc_dev;	/* base device */
	struct usbd_bus  *sc_bus;	/* USB controller */
	struct usbd_port sc_port;	/* dummy port for root hub */
	int		 sc_speed;

	struct usb_task	 sc_explore_task;

	struct timeval	 sc_ptime;
};

struct rwlock usbpalock;

TAILQ_HEAD(, usb_task) usb_abort_tasks;
TAILQ_HEAD(, usb_task) usb_explore_tasks;
TAILQ_HEAD(, usb_task) usb_generic_tasks;

static int usb_nbuses = 0;
static int usb_run_tasks, usb_run_abort_tasks;
int explore_pending;
const char *usbrev_str[] = USBREV_STR;

void		 usb_explore(void *);
void		 usb_create_task_threads(void *);
void		 usb_task_thread(void *);
struct proc	*usb_task_thread_proc = NULL;
void		 usb_abort_task_thread(void *);
struct proc	*usb_abort_task_thread_proc = NULL;

void		 usb_fill_udc_task(void *);
void		 usb_fill_udf_task(void *);

int		 usb_match(struct device *, void *, void *);
void		 usb_attach(struct device *, struct device *, void *);
int		 usb_detach(struct device *, int);
int		 usb_activate(struct device *, int);

int		 usb_attach_roothub(struct usb_softc *);
void		 usb_detach_roothub(struct usb_softc *);

struct cfdriver usb_cd = {
	NULL, "usb", DV_DULL
};

const struct cfattach usb_ca = {
	sizeof(struct usb_softc), usb_match, usb_attach, usb_detach,
	usb_activate,
};

int
usb_match(struct device *parent, void *match, void *aux)
{
	return (1);
}

void
usb_attach(struct device *parent, struct device *self, void *aux)
{
	struct usb_softc *sc = (struct usb_softc *)self;
	int usbrev;

	if (usb_nbuses == 0) {
		rw_init(&usbpalock, "usbpalock");
		TAILQ_INIT(&usb_abort_tasks);
		TAILQ_INIT(&usb_explore_tasks);
		TAILQ_INIT(&usb_generic_tasks);
		usb_run_tasks = usb_run_abort_tasks = 1;
		kthread_create_deferred(usb_create_task_threads, NULL);
	}
	usb_nbuses++;

	sc->sc_bus = aux;
	sc->sc_bus->usbctl = self;
	sc->sc_port.power = USB_MAX_POWER;

	usbrev = sc->sc_bus->usbrev;
	printf(": USB revision %s", usbrev_str[usbrev]);
	switch (usbrev) {
	case USBREV_1_0:
	case USBREV_1_1:
		sc->sc_speed = USB_SPEED_FULL;
		break;
	case USBREV_2_0:
		sc->sc_speed = USB_SPEED_HIGH;
		break;
	case USBREV_3_0:
		sc->sc_speed = USB_SPEED_SUPER;
		break;
	default:
		printf(", not supported\n");
		sc->sc_bus->dying = 1;
		return;
	}
	printf("\n");

#if NBPFILTER > 0
	sc->sc_bus->bpfif = bpfsattach(&sc->sc_bus->bpf, sc->sc_dev.dv_xname,
	    DLT_USBPCAP, sizeof(struct usbpcap_pkt_hdr));
#endif

	/* Make sure not to use tsleep() if we are cold booting. */
	if (cold)
		sc->sc_bus->use_polling++;

	/* Don't let hub interrupts cause explore until ready. */
	sc->sc_bus->flags |= USB_BUS_CONFIG_PENDING;

	/* explore task */
	usb_init_task(&sc->sc_explore_task, usb_explore, sc,
	    USB_TASK_TYPE_EXPLORE);

	sc->sc_bus->soft = softintr_establish(IPL_SOFTUSB,
	    sc->sc_bus->methods->soft_intr, sc->sc_bus);
	if (sc->sc_bus->soft == NULL) {
		printf("%s: can't register softintr\n", sc->sc_dev.dv_xname);
		sc->sc_bus->dying = 1;
		return;
	}

	if (!usb_attach_roothub(sc)) {
		struct usbd_device *dev = sc->sc_bus->root_hub;
#if 1
		/*
		 * Turning this code off will delay attachment of USB devices
		 * until the USB task thread is running, which means that
		 * the keyboard will not work until after cold boot.
		 */
		if (cold && (sc->sc_dev.dv_cfdata->cf_flags & 1))
			dev->hub->explore(sc->sc_bus->root_hub);
#endif
	}

	if (cold)
		sc->sc_bus->use_polling--;

	if (!sc->sc_bus->dying) {
		getmicrouptime(&sc->sc_ptime);
		if (sc->sc_bus->usbrev == USBREV_2_0)
			explore_pending++;
		config_pending_incr();
		usb_needs_explore(sc->sc_bus->root_hub, 1);
	}
}

int
usb_attach_roothub(struct usb_softc *sc)
{
	struct usbd_device *dev;

	if (usbd_new_device(&sc->sc_dev, sc->sc_bus, 0, sc->sc_speed, 0,
	    &sc->sc_port)) {
		printf("%s: root hub problem\n", sc->sc_dev.dv_xname);
		sc->sc_bus->dying = 1;
		return (1);
	}

	dev = sc->sc_port.device;
	if (dev->hub == NULL) {
		printf("%s: root device is not a hub\n", sc->sc_dev.dv_xname);
		sc->sc_bus->dying = 1;
		return (1);
	}
	sc->sc_bus->root_hub = dev;

	return (0);
}

void
usb_detach_roothub(struct usb_softc *sc)
{
	/*
	 * To avoid races with the usb task thread, mark the root hub
	 * as disconnecting and schedule an exploration task to detach
	 * it.
	 */
	sc->sc_bus->flags |= USB_BUS_DISCONNECTING;
	/*
	 * Reset the dying flag in case it has been set by the interrupt
	 * handler when unplugging an HC card otherwise the task won't be
	 * scheduled.  This is safe since a dead HC should not trigger
	 * new interrupt.
	 */
	sc->sc_bus->dying = 0;
	usb_needs_explore(sc->sc_bus->root_hub, 0);

	usb_wait_task(sc->sc_bus->root_hub, &sc->sc_explore_task);

	sc->sc_bus->root_hub = NULL;
}

void
usb_create_task_threads(void *arg)
{
	if (kthread_create(usb_abort_task_thread, NULL,
	    &usb_abort_task_thread_proc, "usbatsk"))
		panic("unable to create usb abort task thread");

	if (kthread_create(usb_task_thread, NULL,
	    &usb_task_thread_proc, "usbtask"))
		panic("unable to create usb task thread");
}

/*
 * Add a task to be performed by the task thread.  This function can be
 * called from any context and the task will be executed in a process
 * context ASAP.
 */
void
usb_add_task(struct usbd_device *dev, struct usb_task *task)
{
	int s;

	/*
	 * If the thread detaching ``dev'' is sleeping, waiting
	 * for all submitted transfers to finish, we must be able
	 * to enqueue abort tasks.  Otherwise timeouts can't give
	 * back submitted transfers to the stack.
	 */
	if (usbd_is_dying(dev) && (task->type != USB_TASK_TYPE_ABORT))
		return;

	DPRINTFN(2,("%s: task=%p state=%d type=%d\n", __func__, task,
	    task->state, task->type));

	s = splusb();
	if (!(task->state & USB_TASK_STATE_ONQ)) {
		switch (task->type) {
		case USB_TASK_TYPE_ABORT:
			TAILQ_INSERT_TAIL(&usb_abort_tasks, task, next);
			break;
		case USB_TASK_TYPE_EXPLORE:
			TAILQ_INSERT_TAIL(&usb_explore_tasks, task, next);
			break;
		case USB_TASK_TYPE_GENERIC:
			TAILQ_INSERT_TAIL(&usb_generic_tasks, task, next);
			break;
		}
		task->state |= USB_TASK_STATE_ONQ;
		task->dev = dev;
	}
	if (task->type == USB_TASK_TYPE_ABORT)
		wakeup(&usb_run_abort_tasks);
	else
		wakeup(&usb_run_tasks);
	splx(s);
}

void
usb_rem_task(struct usbd_device *dev, struct usb_task *task)
{
	int s;

	if (!(task->state & USB_TASK_STATE_ONQ))
		return;

	DPRINTFN(2,("%s: task=%p state=%d type=%d\n", __func__, task,
	    task->state, task->type));

	s = splusb();

	switch (task->type) {
	case USB_TASK_TYPE_ABORT:
		TAILQ_REMOVE(&usb_abort_tasks, task, next);
		break;
	case USB_TASK_TYPE_EXPLORE:
		TAILQ_REMOVE(&usb_explore_tasks, task, next);
		break;
	case USB_TASK_TYPE_GENERIC:
		TAILQ_REMOVE(&usb_generic_tasks, task, next);
		break;
	}
	task->state &= ~USB_TASK_STATE_ONQ;
	if (task->state == USB_TASK_STATE_NONE)
		wakeup(task);

	splx(s);
}

void
usb_wait_task(struct usbd_device *dev, struct usb_task *task)
{
	int s;

	DPRINTFN(2,("%s: task=%p state=%d type=%d\n", __func__, task,
	    task->state, task->type));

	if (task->state == USB_TASK_STATE_NONE)
		return;

	s = splusb();
	while (task->state != USB_TASK_STATE_NONE) {
		DPRINTF(("%s: waiting for task to complete\n", __func__));
		tsleep_nsec(task, PWAIT, "endtask", INFSLP);
	}
	splx(s);
}

void
usb_rem_wait_task(struct usbd_device *dev, struct usb_task *task)
{
	usb_rem_task(dev, task);
	usb_wait_task(dev, task);
}

void
usb_task_thread(void *arg)
{
	struct usb_task *task;
	int s;

	DPRINTF(("usb_task_thread: start\n"));

	s = splusb();
	while (usb_run_tasks) {
		if ((task = TAILQ_FIRST(&usb_explore_tasks)) != NULL)
			TAILQ_REMOVE(&usb_explore_tasks, task, next);
		else if ((task = TAILQ_FIRST(&usb_generic_tasks)) != NULL)
			TAILQ_REMOVE(&usb_generic_tasks, task, next);
		else {
			tsleep_nsec(&usb_run_tasks, PWAIT, "usbtsk", INFSLP);
			continue;
		}
		/*
		 * Set the state run bit before clearing the onq bit.
		 * This avoids state == none between dequeue and
		 * execution, which could cause usb_wait_task() to do
		 * the wrong thing.
		 */
		task->state |= USB_TASK_STATE_RUN;
		task->state &= ~USB_TASK_STATE_ONQ;
		/* Don't actually execute the task if dying. */
		if (!usbd_is_dying(task->dev)) {
			splx(s);
			task->fun(task->arg);
			s = splusb();
		}
		task->state &= ~USB_TASK_STATE_RUN;
		if (task->state == USB_TASK_STATE_NONE)
			wakeup(task);
	}
	splx(s);

	kthread_exit(0);
}

/*
 * This thread is ONLY for the HCI drivers to be able to abort xfers.
 * Synchronous xfers sleep the task thread, so the aborts need to happen
 * in a different thread.
 */
void
usb_abort_task_thread(void *arg)
{
	struct usb_task *task;
	int s;

	DPRINTF(("usb_xfer_abort_thread: start\n"));

	s = splusb();
	while (usb_run_abort_tasks) {
		if ((task = TAILQ_FIRST(&usb_abort_tasks)) != NULL)
			TAILQ_REMOVE(&usb_abort_tasks, task, next);
		else {
			tsleep_nsec(&usb_run_abort_tasks, PWAIT, "usbatsk",
			    INFSLP);
			continue;
		}
		/*
		 * Set the state run bit before clearing the onq bit.
		 * This avoids state == none between dequeue and
		 * execution, which could cause usb_wait_task() to do
		 * the wrong thing.
		 */
		task->state |= USB_TASK_STATE_RUN;
		task->state &= ~USB_TASK_STATE_ONQ;
		splx(s);
		task->fun(task->arg);
		s = splusb();
		task->state &= ~USB_TASK_STATE_RUN;
		if (task->state == USB_TASK_STATE_NONE)
			wakeup(task);
	}
	splx(s);

	kthread_exit(0);
}

int
usbctlprint(void *aux, const char *pnp)
{
	/* only "usb"es can attach to host controllers */
	if (pnp)
		printf("usb at %s", pnp);

	return (UNCONF);
}

int
usbopen(dev_t dev, int flag, int mode, struct proc *p)
{
	int unit = minor(dev);
	struct usb_softc *sc;

	if (unit >= usb_cd.cd_ndevs)
		return (ENXIO);
	sc = usb_cd.cd_devs[unit];
	if (sc == NULL)
		return (ENXIO);

	if (sc->sc_bus->dying)
		return (EIO);

	return (0);
}

int
usbclose(dev_t dev, int flag, int mode, struct proc *p)
{
	return (0);
}

void
usb_fill_udc_task(void *arg)
{
	struct usb_device_cdesc *udc = (struct usb_device_cdesc *)arg;
	struct usb_softc *sc;
	struct usbd_device *dev;
	int addr = udc->udc_addr, cdesc_len;
	usb_config_descriptor_t *cdesc;

	/* check that the bus and device are still present */
	if (udc->udc_bus >= usb_cd.cd_ndevs)
		return;
	sc = usb_cd.cd_devs[udc->udc_bus];
	if (sc == NULL)
		return;
	dev = sc->sc_bus->devices[udc->udc_addr];
	if (dev == NULL)
		return;

	cdesc = usbd_get_cdesc(sc->sc_bus->devices[addr],
	    udc->udc_config_index, &cdesc_len);
	if (cdesc == NULL)
		return;
	udc->udc_desc = *cdesc;
	free(cdesc, M_TEMP, cdesc_len);
}

void
usb_fill_udf_task(void *arg)
{
	struct usb_device_fdesc *udf = (struct usb_device_fdesc *)arg;
	struct usb_softc *sc;
	struct usbd_device *dev;
	int addr = udf->udf_addr;
	usb_config_descriptor_t *cdesc;

	/* check that the bus and device are still present */
	if (udf->udf_bus >= usb_cd.cd_ndevs)
		return;
	sc = usb_cd.cd_devs[udf->udf_bus];
	if (sc == NULL)
		return;
	dev = sc->sc_bus->devices[udf->udf_addr];
	if (dev == NULL)
		return;

	cdesc = usbd_get_cdesc(sc->sc_bus->devices[addr],
	    udf->udf_config_index, &udf->udf_size);
	udf->udf_data = (char *)cdesc;
}

int
usbioctl(dev_t devt, u_long cmd, caddr_t data, int flag, struct proc *p)
{
	struct usb_softc *sc;
	int unit = minor(devt);
	int error;

	sc = usb_cd.cd_devs[unit];

	if (sc->sc_bus->dying)
		return (EIO);

	error = 0;
	switch (cmd) {
#ifdef USB_DEBUG
	case USB_SETDEBUG:
		/* only root can access to these debug flags */
		if ((error = suser(curproc)) != 0)
			return (error);
		if (!(flag & FWRITE))
			return (EBADF);
		usbdebug  = ((*(unsigned int *)data) & 0x000000ff);
#if defined(UHCI_DEBUG) && NUHCI > 0
		uhcidebug = ((*(unsigned int *)data) & 0x0000ff00) >> 8;
#endif
#if defined(OHCI_DEBUG) && NOHCI > 0
		ohcidebug = ((*(unsigned int *)data) & 0x00ff0000) >> 16;
#endif
#if defined(EHCI_DEBUG) && NEHCI > 0
		ehcidebug = ((*(unsigned int *)data) & 0xff000000) >> 24;
#endif
		break;
#endif /* USB_DEBUG */
	case USB_REQUEST:
	{
		struct usb_ctl_request *ur = (void *)data;
		size_t len = UGETW(ur->ucr_request.wLength), mlen;
		struct iovec iov;
		struct uio uio;
		void *ptr = NULL;
		int addr = ur->ucr_addr;
		usbd_status err;

		if (!(flag & FWRITE))
			return (EBADF);

		DPRINTF(("%s: USB_REQUEST addr=%d len=%zu\n", __func__, addr, len));
		/* Avoid requests that would damage the bus integrity. */
		if ((ur->ucr_request.bmRequestType == UT_WRITE_DEVICE &&
		     ur->ucr_request.bRequest == UR_SET_ADDRESS) ||
		    (ur->ucr_request.bmRequestType == UT_WRITE_DEVICE &&
		     ur->ucr_request.bRequest == UR_SET_CONFIG) ||
		    (ur->ucr_request.bmRequestType == UT_WRITE_INTERFACE &&
		     ur->ucr_request.bRequest == UR_SET_INTERFACE))
			return (EINVAL);

		if (len > 32767)
			return (EINVAL);
		if (addr < 0 || addr >= USB_MAX_DEVICES)
			return (EINVAL);
		if (sc->sc_bus->devices[addr] == NULL)
			return (ENXIO);
		if (len != 0) {
			iov.iov_base = (caddr_t)ur->ucr_data;
			iov.iov_len = len;
			uio.uio_iov = &iov;
			uio.uio_iovcnt = 1;
			uio.uio_resid = len;
			uio.uio_offset = 0;
			uio.uio_segflg = UIO_USERSPACE;
			uio.uio_rw =
				ur->ucr_request.bmRequestType & UT_READ ?
				UIO_READ : UIO_WRITE;
			uio.uio_procp = p;
			if ((ptr = malloc(len, M_TEMP, M_NOWAIT)) == NULL) {
				error = ENOMEM;
				goto ret;
			}
			if (uio.uio_rw == UIO_WRITE) {
				error = uiomove(ptr, len, &uio);
				if (error)
					goto ret;
			}
		}
		err = usbd_do_request_flags(sc->sc_bus->devices[addr],
			  &ur->ucr_request, ptr, ur->ucr_flags,
			  &ur->ucr_actlen, USBD_DEFAULT_TIMEOUT);
		if (err) {
			error = EIO;
			goto ret;
		}
		/* Only if USBD_SHORT_XFER_OK is set. */
		mlen = len;
		if (mlen > ur->ucr_actlen)
			mlen = ur->ucr_actlen;
		if (mlen != 0) {
			if (uio.uio_rw == UIO_READ) {
				error = uiomove(ptr, mlen, &uio);
				if (error)
					goto ret;
			}
		}
	ret:
		free(ptr, M_TEMP, len);
		return (error);
	}

	case USB_DEVICEINFO:
	{
		struct usb_device_info *di = (void *)data;
		int addr = di->udi_addr;
		struct usbd_device *dev;

		if (addr < 1 || addr >= USB_MAX_DEVICES)
			return (EINVAL);

		dev = sc->sc_bus->devices[addr];
		if (dev == NULL)
			return (ENXIO);

		usbd_fill_deviceinfo(dev, di);
		break;
	}

	case USB_DEVICESTATS:
		*(struct usb_device_stats *)data = sc->sc_bus->stats;
		break;

	case USB_DEVICE_GET_DDESC:
	{
		struct usb_device_ddesc *udd = (struct usb_device_ddesc *)data;
		int addr = udd->udd_addr;
		struct usbd_device *dev;

		if (addr < 1 || addr >= USB_MAX_DEVICES)
			return (EINVAL);

		dev = sc->sc_bus->devices[addr];
		if (dev == NULL)
			return (ENXIO);

		udd->udd_bus = unit;

		udd->udd_desc = *usbd_get_device_descriptor(dev);
		break;
	}

	case USB_DEVICE_GET_CDESC:
	{
		struct usb_device_cdesc *udc = (struct usb_device_cdesc *)data;
		int addr = udc->udc_addr;
		struct usb_task udc_task;

		if (addr < 1 || addr >= USB_MAX_DEVICES)
			return (EINVAL);
		if (sc->sc_bus->devices[addr] == NULL)
			return (ENXIO);

		udc->udc_bus = unit;

		udc->udc_desc.bLength = 0;
		usb_init_task(&udc_task, usb_fill_udc_task, udc,
		    USB_TASK_TYPE_GENERIC);
		usb_add_task(sc->sc_bus->root_hub, &udc_task);
		usb_wait_task(sc->sc_bus->root_hub, &udc_task);
		if (udc->udc_desc.bLength == 0)
			return (EINVAL);
		break;
	}

	case USB_DEVICE_GET_FDESC:
	{
		struct usb_device_fdesc *udf = (struct usb_device_fdesc *)data;
		int addr = udf->udf_addr;
		struct usb_task udf_task;
		struct usb_device_fdesc save_udf;
		usb_config_descriptor_t *cdesc;
		struct iovec iov;
		struct uio uio;
		size_t len, cdesc_len;

		if (addr < 1 || addr >= USB_MAX_DEVICES)
			return (EINVAL);
		if (sc->sc_bus->devices[addr] == NULL)
			return (ENXIO);

		udf->udf_bus = unit;

		save_udf = *udf;
		udf->udf_data = NULL;
		usb_init_task(&udf_task, usb_fill_udf_task, udf,
		    USB_TASK_TYPE_GENERIC);
		usb_add_task(sc->sc_bus->root_hub, &udf_task);
		usb_wait_task(sc->sc_bus->root_hub, &udf_task);
		len = cdesc_len = udf->udf_size;
		cdesc = (usb_config_descriptor_t *)udf->udf_data;
		*udf = save_udf;
		if (cdesc == NULL)
			return (EINVAL);
		if (len > udf->udf_size)
			len = udf->udf_size;
		iov.iov_base = (caddr_t)udf->udf_data;
		iov.iov_len = len;
		uio.uio_iov = &iov;
		uio.uio_iovcnt = 1;
		uio.uio_resid = len;
		uio.uio_offset = 0;
		uio.uio_segflg = UIO_USERSPACE;
		uio.uio_rw = UIO_READ;
		uio.uio_procp = p;
		error = uiomove((void *)cdesc, len, &uio);
		free(cdesc, M_TEMP, cdesc_len);
		return (error);
	}

	default:
		return (EINVAL);
	}
	return (0);
}

/*
 * Explore device tree from the root.  We need mutual exclusion to this
 * hub while traversing the device tree, but this is guaranteed since this
 * function is only called from the task thread, with one exception:
 * usb_attach() calls this function, but there shouldn't be anything else
 * trying to explore this hub at that time.
 */
void
usb_explore(void *v)
{
	struct usb_softc *sc = v;
	struct timeval now, waited;
	int pwrdly, waited_ms;

	DPRINTFN(2,("%s: %s\n", __func__, sc->sc_dev.dv_xname));
#ifdef USB_DEBUG
	if (usb_noexplore)
		return;
#endif

	if (sc->sc_bus->dying)
		return;

	if (sc->sc_bus->flags & USB_BUS_CONFIG_PENDING) {
		/*
		 * If this is a low/full speed hub and there is a high
		 * speed hub that hasn't explored yet, reschedule this
		 * task, allowing the high speed explore task to run.
		 */
		if (sc->sc_bus->usbrev < USBREV_2_0 && explore_pending > 0) {
			usb_add_task(sc->sc_bus->root_hub,
			    &sc->sc_explore_task);
			return;
		}

		/*
		 * Wait for power to stabilize.
		 */
		getmicrouptime(&now);
		timersub(&now, &sc->sc_ptime, &waited);
		waited_ms = waited.tv_sec * 1000 + waited.tv_usec / 1000;

		pwrdly = sc->sc_bus->root_hub->hub->powerdelay +
		    USB_EXTRA_POWER_UP_TIME;
		if (pwrdly > waited_ms)
			usb_delay_ms(sc->sc_bus, pwrdly - waited_ms);
	}

	if (sc->sc_bus->flags & USB_BUS_DISCONNECTING) {
		/* Prevent new tasks from being scheduled. */
		sc->sc_bus->dying = 1;

		/* Make all devices disconnect. */
		if (sc->sc_port.device != NULL) {
			usbd_detach(sc->sc_port.device, (struct device *)sc);
			sc->sc_port.device = NULL;
		}

		sc->sc_bus->flags &= ~USB_BUS_DISCONNECTING;
	} else {
		sc->sc_bus->root_hub->hub->explore(sc->sc_bus->root_hub);
	}

	if (sc->sc_bus->flags & USB_BUS_CONFIG_PENDING) {
		DPRINTF(("%s: %s: first explore done\n", __func__,
		    sc->sc_dev.dv_xname));
		if (sc->sc_bus->usbrev == USBREV_2_0 && explore_pending)
			explore_pending--;
		config_pending_decr();
		sc->sc_bus->flags &= ~(USB_BUS_CONFIG_PENDING);
	}
}

void
usb_needs_explore(struct usbd_device *dev, int first_explore)
{
	struct usb_softc *usbctl = (struct usb_softc *)dev->bus->usbctl;

	DPRINTFN(3,("%s: %s\n", usbctl->sc_dev.dv_xname, __func__));

	if (!first_explore && (dev->bus->flags & USB_BUS_CONFIG_PENDING)) {
		DPRINTF(("%s: %s: not exploring before first explore\n",
		    __func__, usbctl->sc_dev.dv_xname));
		return;
	}

	usb_add_task(dev, &usbctl->sc_explore_task);
}

void
usb_needs_reattach(struct usbd_device *dev)
{
	DPRINTFN(2,("usb_needs_reattach\n"));
	dev->powersrc->reattach = 1;
	usb_needs_explore(dev, 0);
}

void
usb_schedsoftintr(struct usbd_bus *bus)
{
	DPRINTFN(10,("%s: polling=%d\n", __func__, bus->use_polling));

	/* In case usb(4) is disabled */
	if (bus->soft == NULL)
		return;

	if (bus->use_polling) {
		bus->methods->soft_intr(bus);
	} else {
		softintr_schedule(bus->soft);
	}
}

int
usb_activate(struct device *self, int act)
{
	struct usb_softc *sc = (struct usb_softc *)self;
	int rv = 0;

	switch (act) {
	case DVACT_QUIESCE:
		if (sc->sc_bus->root_hub != NULL)
			usb_detach_roothub(sc);
		break;
	case DVACT_RESUME:
		sc->sc_bus->dying = 0;
		break;
	case DVACT_WAKEUP:
		sc->sc_bus->use_polling++;
		if (!usb_attach_roothub(sc))
			usb_needs_explore(sc->sc_bus->root_hub, 0);
		sc->sc_bus->use_polling--;
		break;
	default:
		rv = config_activate_children(self, act);
		break;
	}
	return (rv);
}

int
usb_detach(struct device *self, int flags)
{
	struct usb_softc *sc = (struct usb_softc *)self;

	if (sc->sc_bus->root_hub != NULL) {
		usb_detach_roothub(sc);

		if (--usb_nbuses == 0) {
			usb_run_tasks = usb_run_abort_tasks = 0;
			wakeup(&usb_run_abort_tasks);
			wakeup(&usb_run_tasks);
		}
	}

	if (sc->sc_bus->soft != NULL) {
		softintr_disestablish(sc->sc_bus->soft);
		sc->sc_bus->soft = NULL;
	}

#if NBPFILTER > 0
	bpfsdetach(sc->sc_bus->bpfif);
#endif
	return (0);
}

void
usb_tap(struct usbd_bus *bus, struct usbd_xfer *xfer, uint8_t dir)
{
#if NBPFILTER > 0
	struct usb_softc *sc = (struct usb_softc *)bus->usbctl;
	usb_endpoint_descriptor_t *ed = xfer->pipe->endpoint->edesc;
	union {
		struct usbpcap_ctl_hdr		uch;
		struct usbpcap_iso_hdr_full	uih;
	} h;
	struct usbpcap_pkt_hdr *uph = &h.uch.uch_hdr;
	uint32_t nframes, psize;
	unsigned int bpfdir;
	void *data = NULL;
	size_t flen;
	caddr_t bpf;
	int i;

	bpf = bus->bpf;
	if (bpf == NULL)
		return;

	switch (UE_GET_XFERTYPE(ed->bmAttributes)) {
	case UE_CONTROL:
		/* Control transfer headers include an extra byte */
		uph->uph_hlen = htole16(sizeof(struct usbpcap_ctl_hdr));
		uph->uph_xfertype = USBPCAP_TRANSFER_CONTROL;
		break;
	case UE_ISOCHRONOUS:
		nframes = xfer->nframes;
		/*
		 * All our drivers use a fixed size (psize) for
		 * ISOCHRONOUS packets. Calculate it to determine the
		 * correct offset below.
		 */
		psize = xfer->length / nframes;
#ifdef DIAGNOSTIC
		if (nframes > _USBPCAP_MAX_ISOFRAMES) {
			printf("%s: too many frames: %d > %d\n", __func__,
			    xfer->nframes, _USBPCAP_MAX_ISOFRAMES);
			nframes = _USBPCAP_MAX_ISOFRAMES;
		}
#endif
		/* Isochronous transfer headers include space for one frame */
		flen = (nframes - 1) * sizeof(struct usbpcap_iso_pkt);
		uph->uph_hlen = htole16(sizeof(struct usbpcap_iso_hdr) + flen);
		uph->uph_xfertype = USBPCAP_TRANSFER_ISOCHRONOUS;
		h.uih.uih_startframe = 0; /* not yet used */
		h.uih.uih_nframes = nframes;
		h.uih.uih_errors = 0; /* we don't have per-frame error */
		for (i = 0; i < nframes; i++) {
			/*
			 * We can't use length, because IN frame may
			 * have shorter length of packet whan expected.
			 */
			h.uih.uih_frames[i].uip_offset = i * psize;
			h.uih.uih_frames[i].uip_length = xfer->frlengths[i];
			/* See above, we don't have per-frame error */
			h.uih.uih_frames[i].uip_status = 0;
		}
		break;
	case UE_BULK:
		uph->uph_hlen = htole16(sizeof(*uph));
		uph->uph_xfertype = USBPCAP_TRANSFER_BULK;
		break;
	case UE_INTERRUPT:
		uph->uph_hlen = htole16(sizeof(*uph));
		uph->uph_xfertype = USBPCAP_TRANSFER_INTERRUPT;
		break;
	default:
		return;
	}

	uph->uph_id = 0; /* not yet used */
	uph->uph_status = htole32(xfer->status);
	uph->uph_function = 0; /* not yet used */
	uph->uph_bus = htole32(sc->sc_dev.dv_unit);
	uph->uph_devaddr = htole16(xfer->device->address);
	uph->uph_epaddr = ed->bEndpointAddress;
	uph->uph_info = 0;

	/* Outgoing control requests start with a STAGE dump. */
	if ((xfer->rqflags & URQ_REQUEST) && (dir == USBTAP_DIR_OUT)) {
		h.uch.uch_stage = USBPCAP_CONTROL_STAGE_SETUP;
		uph->uph_dlen = sizeof(usb_device_request_t);
		bpf_tap_hdr(bpf, uph, uph->uph_hlen, &xfer->request,
		    uph->uph_dlen, BPF_DIRECTION_OUT);
	}

	if (dir == USBTAP_DIR_OUT) {
		bpfdir = BPF_DIRECTION_OUT;
		if (!usbd_xfer_isread(xfer)) {
			data = KERNADDR(&xfer->dmabuf, 0);
			uph->uph_dlen = xfer->length;
			if (xfer->rqflags & URQ_REQUEST)
				h.uch.uch_stage = USBPCAP_CONTROL_STAGE_DATA;
		} else {
			data = NULL;
			uph->uph_dlen = 0;
			if (xfer->rqflags & URQ_REQUEST)
				h.uch.uch_stage = USBPCAP_CONTROL_STAGE_STATUS;
		}
	} else { /* USBTAP_DIR_IN */
		bpfdir = BPF_DIRECTION_IN;
		uph->uph_info = USBPCAP_INFO_DIRECTION_IN;
		if (usbd_xfer_isread(xfer)) {
			data = KERNADDR(&xfer->dmabuf, 0);
			uph->uph_dlen = xfer->actlen;
			if (xfer->rqflags & URQ_REQUEST)
				h.uch.uch_stage = USBPCAP_CONTROL_STAGE_DATA;
		} else {
			data = NULL;
			uph->uph_dlen = 0;
			if (xfer->rqflags & URQ_REQUEST)
				h.uch.uch_stage = USBPCAP_CONTROL_STAGE_STATUS;
		}
	}

	/* ISOCHRONOUS IN from device may have gaps, use full buffer */
	if (bpfdir == BPF_DIRECTION_IN && uph->uph_dlen > 0 &&
	    uph->uph_xfertype == USBPCAP_TRANSFER_ISOCHRONOUS) {
		uph->uph_dlen = xfer->length;
	}

	/* Dump bulk/intr/iso data, ctrl DATA or STATUS stage. */
	bpf_tap_hdr(bpf, uph, uph->uph_hlen, data, uph->uph_dlen, bpfdir);

	/* Incoming control requests with DATA need a STATUS stage. */
	if ((xfer->rqflags & URQ_REQUEST) && (dir == USBTAP_DIR_IN) &&
	    (h.uch.uch_stage == USBPCAP_CONTROL_STAGE_DATA)) {
		h.uch.uch_stage = USBPCAP_CONTROL_STAGE_STATUS;
		uph->uph_dlen = 0;
		bpf_tap_hdr(bpf, uph, uph->uph_hlen, NULL, 0, BPF_DIRECTION_IN);
	}
#endif
}
