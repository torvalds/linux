/* $FreeBSD$ */
/*-
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD
 *
 * Copyright (c) 2006-2008 Hans Petter Selasky. All rights reserved.
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
 *
 *
 * usb_dev.c - An abstraction layer for creating devices under /dev/...
 */

#ifdef USB_GLOBAL_INCLUDE_FILE
#include USB_GLOBAL_INCLUDE_FILE
#else
#include <sys/stdint.h>
#include <sys/stddef.h>
#include <sys/param.h>
#include <sys/queue.h>
#include <sys/types.h>
#include <sys/systm.h>
#include <sys/kernel.h>
#include <sys/bus.h>
#include <sys/module.h>
#include <sys/lock.h>
#include <sys/mutex.h>
#include <sys/condvar.h>
#include <sys/sysctl.h>
#include <sys/sx.h>
#include <sys/unistd.h>
#include <sys/callout.h>
#include <sys/malloc.h>
#include <sys/priv.h>
#include <sys/vnode.h>
#include <sys/conf.h>
#include <sys/fcntl.h>

#include <dev/usb/usb.h>
#include <dev/usb/usb_ioctl.h>
#include <dev/usb/usbdi.h>
#include <dev/usb/usbdi_util.h>

#define	USB_DEBUG_VAR usb_fifo_debug

#include <dev/usb/usb_core.h>
#include <dev/usb/usb_dev.h>
#include <dev/usb/usb_mbuf.h>
#include <dev/usb/usb_process.h>
#include <dev/usb/usb_device.h>
#include <dev/usb/usb_debug.h>
#include <dev/usb/usb_busdma.h>
#include <dev/usb/usb_generic.h>
#include <dev/usb/usb_dynamic.h>
#include <dev/usb/usb_util.h>

#include <dev/usb/usb_controller.h>
#include <dev/usb/usb_bus.h>

#include <sys/filio.h>
#include <sys/ttycom.h>
#include <sys/syscallsubr.h>

#include <machine/stdarg.h>
#endif			/* USB_GLOBAL_INCLUDE_FILE */

#if USB_HAVE_UGEN

#ifdef USB_DEBUG
static int usb_fifo_debug = 0;

static SYSCTL_NODE(_hw_usb, OID_AUTO, dev, CTLFLAG_RW, 0, "USB device");
SYSCTL_INT(_hw_usb_dev, OID_AUTO, debug, CTLFLAG_RWTUN,
    &usb_fifo_debug, 0, "Debug Level");
#endif

#if ((__FreeBSD_version >= 700001) || (__FreeBSD_version == 0) || \
     ((__FreeBSD_version >= 600034) && (__FreeBSD_version < 700000)))
#define	USB_UCRED struct ucred *ucred,
#else
#define	USB_UCRED
#endif

/* prototypes */

static int	usb_fifo_open(struct usb_cdev_privdata *, 
		    struct usb_fifo *, int);
static void	usb_fifo_close(struct usb_fifo *, int);
static void	usb_dev_init(void *);
static void	usb_dev_init_post(void *);
static void	usb_dev_uninit(void *);
static int	usb_fifo_uiomove(struct usb_fifo *, void *, int,
		    struct uio *);
static void	usb_fifo_check_methods(struct usb_fifo_methods *);
static struct	usb_fifo *usb_fifo_alloc(struct mtx *);
static struct	usb_endpoint *usb_dev_get_ep(struct usb_device *, uint8_t,
		    uint8_t);
static void	usb_loc_fill(struct usb_fs_privdata *,
		    struct usb_cdev_privdata *);
static void	usb_close(void *);
static usb_error_t usb_ref_device(struct usb_cdev_privdata *, struct usb_cdev_refdata *, int);
static usb_error_t usb_usb_ref_device(struct usb_cdev_privdata *, struct usb_cdev_refdata *);
static void	usb_unref_device(struct usb_cdev_privdata *, struct usb_cdev_refdata *);

static d_open_t usb_open;
static d_ioctl_t usb_ioctl;
static d_read_t usb_read;
static d_write_t usb_write;
static d_poll_t usb_poll;
static d_kqfilter_t usb_kqfilter;

static d_ioctl_t usb_static_ioctl;

static usb_fifo_open_t usb_fifo_dummy_open;
static usb_fifo_close_t usb_fifo_dummy_close;
static usb_fifo_ioctl_t usb_fifo_dummy_ioctl;
static usb_fifo_cmd_t usb_fifo_dummy_cmd;

/* character device structure used for devices (/dev/ugenX.Y and /dev/uXXX) */
struct cdevsw usb_devsw = {
	.d_version = D_VERSION,
	.d_open = usb_open,
	.d_ioctl = usb_ioctl,
	.d_name = "usbdev",
	.d_flags = D_TRACKCLOSE,
	.d_read = usb_read,
	.d_write = usb_write,
	.d_poll = usb_poll,
	.d_kqfilter = usb_kqfilter,
};

static struct cdev* usb_dev = NULL;

/* character device structure used for /dev/usb */
static struct cdevsw usb_static_devsw = {
	.d_version = D_VERSION,
	.d_ioctl = usb_static_ioctl,
	.d_name = "usb"
};

static TAILQ_HEAD(, usb_symlink) usb_sym_head;
static struct sx usb_sym_lock;

struct mtx usb_ref_lock;

/*------------------------------------------------------------------------*
 *	usb_loc_fill
 *
 * This is used to fill out a usb_cdev_privdata structure based on the
 * device's address as contained in usb_fs_privdata.
 *------------------------------------------------------------------------*/
static void
usb_loc_fill(struct usb_fs_privdata* pd, struct usb_cdev_privdata *cpd)
{
	cpd->bus_index = pd->bus_index;
	cpd->dev_index = pd->dev_index;
	cpd->ep_addr = pd->ep_addr;
	cpd->fifo_index = pd->fifo_index;
}

/*------------------------------------------------------------------------*
 *	usb_ref_device
 *
 * This function is used to atomically refer an USB device by its
 * device location. If this function returns success the USB device
 * will not disappear until the USB device is unreferenced.
 *
 * Return values:
 *  0: Success, refcount incremented on the given USB device.
 *  Else: Failure.
 *------------------------------------------------------------------------*/
static usb_error_t
usb_ref_device(struct usb_cdev_privdata *cpd, 
    struct usb_cdev_refdata *crd, int need_uref)
{
	struct usb_fifo **ppf;
	struct usb_fifo *f;

	DPRINTFN(2, "cpd=%p need uref=%d\n", cpd, need_uref);

	/* clear all refs */
	memset(crd, 0, sizeof(*crd));

	mtx_lock(&usb_ref_lock);
	cpd->bus = devclass_get_softc(usb_devclass_ptr, cpd->bus_index);
	if (cpd->bus == NULL) {
		DPRINTFN(2, "no bus at %u\n", cpd->bus_index);
		goto error;
	}
	cpd->udev = cpd->bus->devices[cpd->dev_index];
	if (cpd->udev == NULL) {
		DPRINTFN(2, "no device at %u\n", cpd->dev_index);
		goto error;
	}
	if (cpd->udev->state == USB_STATE_DETACHED &&
	    (need_uref != 2)) {
		DPRINTFN(2, "device is detached\n");
		goto error;
	}
	if (need_uref) {
		DPRINTFN(2, "ref udev - needed\n");

		if (cpd->udev->refcount == USB_DEV_REF_MAX) {
			DPRINTFN(2, "no dev ref\n");
			goto error;
		}
		cpd->udev->refcount++;

		mtx_unlock(&usb_ref_lock);

		/*
		 * We need to grab the enumeration SX-lock before
		 * grabbing the FIFO refs to avoid deadlock at detach!
		 */
		crd->do_unlock = usbd_enum_lock_sig(cpd->udev);

		mtx_lock(&usb_ref_lock);

		/* 
		 * Set "is_uref" after grabbing the default SX lock
		 */
		crd->is_uref = 1;

		/* check for signal */
		if (crd->do_unlock > 1) {
			crd->do_unlock = 0;
			goto error;
		}
	}

	/* check if we are doing an open */
	if (cpd->fflags == 0) {
		/* use zero defaults */
	} else {
		/* check for write */
		if (cpd->fflags & FWRITE) {
			ppf = cpd->udev->fifo;
			f = ppf[cpd->fifo_index + USB_FIFO_TX];
			crd->txfifo = f;
			crd->is_write = 1;	/* ref */
			if (f == NULL || f->refcount == USB_FIFO_REF_MAX)
				goto error;
			if (f->curr_cpd != cpd)
				goto error;
			/* check if USB-FS is active */
			if (f->fs_ep_max != 0) {
				crd->is_usbfs = 1;
			}
		}

		/* check for read */
		if (cpd->fflags & FREAD) {
			ppf = cpd->udev->fifo;
			f = ppf[cpd->fifo_index + USB_FIFO_RX];
			crd->rxfifo = f;
			crd->is_read = 1;	/* ref */
			if (f == NULL || f->refcount == USB_FIFO_REF_MAX)
				goto error;
			if (f->curr_cpd != cpd)
				goto error;
			/* check if USB-FS is active */
			if (f->fs_ep_max != 0) {
				crd->is_usbfs = 1;
			}
		}
	}

	/* when everything is OK we increment the refcounts */
	if (crd->is_write) {
		DPRINTFN(2, "ref write\n");
		crd->txfifo->refcount++;
	}
	if (crd->is_read) {
		DPRINTFN(2, "ref read\n");
		crd->rxfifo->refcount++;
	}
	mtx_unlock(&usb_ref_lock);

	return (0);

error:
	if (crd->do_unlock)
		usbd_enum_unlock(cpd->udev);

	if (crd->is_uref) {
		if (--(cpd->udev->refcount) == 0)
			cv_broadcast(&cpd->udev->ref_cv);
	}
	mtx_unlock(&usb_ref_lock);
	DPRINTFN(2, "fail\n");

	/* clear all refs */
	memset(crd, 0, sizeof(*crd));

	return (USB_ERR_INVAL);
}

/*------------------------------------------------------------------------*
 *	usb_usb_ref_device
 *
 * This function is used to upgrade an USB reference to include the
 * USB device reference on a USB location.
 *
 * Return values:
 *  0: Success, refcount incremented on the given USB device.
 *  Else: Failure.
 *------------------------------------------------------------------------*/
static usb_error_t
usb_usb_ref_device(struct usb_cdev_privdata *cpd,
    struct usb_cdev_refdata *crd)
{
	/*
	 * Check if we already got an USB reference on this location:
	 */
	if (crd->is_uref)
		return (0);		/* success */

	/*
	 * To avoid deadlock at detach we need to drop the FIFO ref
	 * and re-acquire a new ref!
	 */
	usb_unref_device(cpd, crd);

	return (usb_ref_device(cpd, crd, 1 /* need uref */));
}

/*------------------------------------------------------------------------*
 *	usb_unref_device
 *
 * This function will release the reference count by one unit for the
 * given USB device.
 *------------------------------------------------------------------------*/
static void
usb_unref_device(struct usb_cdev_privdata *cpd,
    struct usb_cdev_refdata *crd)
{

	DPRINTFN(2, "cpd=%p is_uref=%d\n", cpd, crd->is_uref);

	if (crd->do_unlock)
		usbd_enum_unlock(cpd->udev);

	mtx_lock(&usb_ref_lock);
	if (crd->is_read) {
		if (--(crd->rxfifo->refcount) == 0) {
			cv_signal(&crd->rxfifo->cv_drain);
		}
		crd->is_read = 0;
	}
	if (crd->is_write) {
		if (--(crd->txfifo->refcount) == 0) {
			cv_signal(&crd->txfifo->cv_drain);
		}
		crd->is_write = 0;
	}
	if (crd->is_uref) {
		crd->is_uref = 0;
		if (--(cpd->udev->refcount) == 0)
			cv_broadcast(&cpd->udev->ref_cv);
	}
	mtx_unlock(&usb_ref_lock);
}

static struct usb_fifo *
usb_fifo_alloc(struct mtx *mtx)
{
	struct usb_fifo *f;

	f = malloc(sizeof(*f), M_USBDEV, M_WAITOK | M_ZERO);
	if (f != NULL) {
		cv_init(&f->cv_io, "FIFO-IO");
		cv_init(&f->cv_drain, "FIFO-DRAIN");
		f->priv_mtx = mtx;
		f->refcount = 1;
		knlist_init_mtx(&f->selinfo.si_note, mtx);
	}
	return (f);
}

/*------------------------------------------------------------------------*
 *	usb_fifo_create
 *------------------------------------------------------------------------*/
static int
usb_fifo_create(struct usb_cdev_privdata *cpd,
    struct usb_cdev_refdata *crd)
{
	struct usb_device *udev = cpd->udev;
	struct usb_fifo *f;
	struct usb_endpoint *ep;
	uint8_t n;
	uint8_t is_tx;
	uint8_t is_rx;
	uint8_t no_null;
	uint8_t is_busy;
	int e = cpd->ep_addr;

	is_tx = (cpd->fflags & FWRITE) ? 1 : 0;
	is_rx = (cpd->fflags & FREAD) ? 1 : 0;
	no_null = 1;
	is_busy = 0;

	/* Preallocated FIFO */
	if (e < 0) {
		DPRINTFN(5, "Preallocated FIFO\n");
		if (is_tx) {
			f = udev->fifo[cpd->fifo_index + USB_FIFO_TX];
			if (f == NULL)
				return (EINVAL);
			crd->txfifo = f;
		}
		if (is_rx) {
			f = udev->fifo[cpd->fifo_index + USB_FIFO_RX];
			if (f == NULL)
				return (EINVAL);
			crd->rxfifo = f;
		}
		return (0);
	}

	KASSERT(e >= 0 && e <= 15, ("endpoint %d out of range", e));

	/* search for a free FIFO slot */
	DPRINTFN(5, "Endpoint device, searching for 0x%02x\n", e);
	for (n = 0;; n += 2) {

		if (n == USB_FIFO_MAX) {
			if (no_null) {
				no_null = 0;
				n = 0;
			} else {
				/* end of FIFOs reached */
				DPRINTFN(5, "out of FIFOs\n");
				return (ENOMEM);
			}
		}
		/* Check for TX FIFO */
		if (is_tx) {
			f = udev->fifo[n + USB_FIFO_TX];
			if (f != NULL) {
				if (f->dev_ep_index != e) {
					/* wrong endpoint index */
					continue;
				}
				if (f->curr_cpd != NULL) {
					/* FIFO is opened */
					is_busy = 1;
					continue;
				}
			} else if (no_null) {
				continue;
			}
		}
		/* Check for RX FIFO */
		if (is_rx) {
			f = udev->fifo[n + USB_FIFO_RX];
			if (f != NULL) {
				if (f->dev_ep_index != e) {
					/* wrong endpoint index */
					continue;
				}
				if (f->curr_cpd != NULL) {
					/* FIFO is opened */
					is_busy = 1;
					continue;
				}
			} else if (no_null) {
				continue;
			}
		}
		break;
	}

	if (no_null == 0) {
		if (e >= (USB_EP_MAX / 2)) {
			/* we don't create any endpoints in this range */
			DPRINTFN(5, "ep out of range\n");
			return (is_busy ? EBUSY : EINVAL);
		}
	}

	if ((e != 0) && is_busy) {
		/*
		 * Only the default control endpoint is allowed to be
		 * opened multiple times!
		 */
		DPRINTFN(5, "busy\n");
		return (EBUSY);
	}

	/* Check TX FIFO */
	if (is_tx &&
	    (udev->fifo[n + USB_FIFO_TX] == NULL)) {
		ep = usb_dev_get_ep(udev, e, USB_FIFO_TX);
		DPRINTFN(5, "dev_get_endpoint(%d, 0x%x)\n", e, USB_FIFO_TX);
		if (ep == NULL) {
			DPRINTFN(5, "dev_get_endpoint returned NULL\n");
			return (EINVAL);
		}
		f = usb_fifo_alloc(&udev->device_mtx);
		if (f == NULL) {
			DPRINTFN(5, "could not alloc tx fifo\n");
			return (ENOMEM);
		}
		/* update some fields */
		f->fifo_index = n + USB_FIFO_TX;
		f->dev_ep_index = e;
		f->priv_sc0 = ep;
		f->methods = &usb_ugen_methods;
		f->iface_index = ep->iface_index;
		f->udev = udev;
		mtx_lock(&usb_ref_lock);
		udev->fifo[n + USB_FIFO_TX] = f;
		mtx_unlock(&usb_ref_lock);
	}
	/* Check RX FIFO */
	if (is_rx &&
	    (udev->fifo[n + USB_FIFO_RX] == NULL)) {

		ep = usb_dev_get_ep(udev, e, USB_FIFO_RX);
		DPRINTFN(5, "dev_get_endpoint(%d, 0x%x)\n", e, USB_FIFO_RX);
		if (ep == NULL) {
			DPRINTFN(5, "dev_get_endpoint returned NULL\n");
			return (EINVAL);
		}
		f = usb_fifo_alloc(&udev->device_mtx);
		if (f == NULL) {
			DPRINTFN(5, "could not alloc rx fifo\n");
			return (ENOMEM);
		}
		/* update some fields */
		f->fifo_index = n + USB_FIFO_RX;
		f->dev_ep_index = e;
		f->priv_sc0 = ep;
		f->methods = &usb_ugen_methods;
		f->iface_index = ep->iface_index;
		f->udev = udev;
		mtx_lock(&usb_ref_lock);
		udev->fifo[n + USB_FIFO_RX] = f;
		mtx_unlock(&usb_ref_lock);
	}
	if (is_tx) {
		crd->txfifo = udev->fifo[n + USB_FIFO_TX];
	}
	if (is_rx) {
		crd->rxfifo = udev->fifo[n + USB_FIFO_RX];
	}
	/* fill out fifo index */
	DPRINTFN(5, "fifo index = %d\n", n);
	cpd->fifo_index = n;

	/* complete */

	return (0);
}

void
usb_fifo_free(struct usb_fifo *f)
{
	uint8_t n;

	if (f == NULL) {
		/* be NULL safe */
		return;
	}
	/* destroy symlink devices, if any */
	for (n = 0; n != 2; n++) {
		if (f->symlink[n]) {
			usb_free_symlink(f->symlink[n]);
			f->symlink[n] = NULL;
		}
	}
	mtx_lock(&usb_ref_lock);

	/* delink ourselves to stop calls from userland */
	if ((f->fifo_index < USB_FIFO_MAX) &&
	    (f->udev != NULL) &&
	    (f->udev->fifo[f->fifo_index] == f)) {
		f->udev->fifo[f->fifo_index] = NULL;
	} else {
		DPRINTFN(0, "USB FIFO %p has not been linked\n", f);
	}

	/* decrease refcount */
	f->refcount--;
	/* need to wait until all callers have exited */
	while (f->refcount != 0) {
		mtx_unlock(&usb_ref_lock);	/* avoid LOR */
		mtx_lock(f->priv_mtx);
		/* prevent write flush, if any */
		f->flag_iserror = 1;
		/* get I/O thread out of any sleep state */
		if (f->flag_sleeping) {
			f->flag_sleeping = 0;
			cv_broadcast(&f->cv_io);
		}
		mtx_unlock(f->priv_mtx);
		mtx_lock(&usb_ref_lock);

		/*
		 * Check if the "f->refcount" variable reached zero
		 * during the unlocked time before entering wait:
		 */
		if (f->refcount == 0)
			break;

		/* wait for sync */
		cv_wait(&f->cv_drain, &usb_ref_lock);
	}
	mtx_unlock(&usb_ref_lock);

	/* take care of closing the device here, if any */
	usb_fifo_close(f, 0);

	cv_destroy(&f->cv_io);
	cv_destroy(&f->cv_drain);

	knlist_clear(&f->selinfo.si_note, 0);
	seldrain(&f->selinfo);
	knlist_destroy(&f->selinfo.si_note);

	free(f, M_USBDEV);
}

static struct usb_endpoint *
usb_dev_get_ep(struct usb_device *udev, uint8_t ep_index, uint8_t dir)
{
	struct usb_endpoint *ep;
	uint8_t ep_dir;

	if (ep_index == 0) {
		ep = &udev->ctrl_ep;
	} else {
		if (dir == USB_FIFO_RX) {
			if (udev->flags.usb_mode == USB_MODE_HOST) {
				ep_dir = UE_DIR_IN;
			} else {
				ep_dir = UE_DIR_OUT;
			}
		} else {
			if (udev->flags.usb_mode == USB_MODE_HOST) {
				ep_dir = UE_DIR_OUT;
			} else {
				ep_dir = UE_DIR_IN;
			}
		}
		ep = usbd_get_ep_by_addr(udev, ep_index | ep_dir);
	}

	if (ep == NULL) {
		/* if the endpoint does not exist then return */
		return (NULL);
	}
	if (ep->edesc == NULL) {
		/* invalid endpoint */
		return (NULL);
	}
	return (ep);			/* success */
}

/*------------------------------------------------------------------------*
 *	usb_fifo_open
 *
 * Returns:
 * 0: Success
 * Else: Failure
 *------------------------------------------------------------------------*/
static int
usb_fifo_open(struct usb_cdev_privdata *cpd, 
    struct usb_fifo *f, int fflags)
{
	int err;

	if (f == NULL) {
		/* no FIFO there */
		DPRINTFN(2, "no FIFO\n");
		return (ENXIO);
	}
	/* remove FWRITE and FREAD flags */
	fflags &= ~(FWRITE | FREAD);

	/* set correct file flags */
	if ((f->fifo_index & 1) == USB_FIFO_TX) {
		fflags |= FWRITE;
	} else {
		fflags |= FREAD;
	}

	/* check if we are already opened */
	/* we don't need any locks when checking this variable */
	if (f->curr_cpd != NULL) {
		err = EBUSY;
		goto done;
	}

	/* reset short flag before open */
	f->flag_short = 0;

	/* call open method */
	err = (f->methods->f_open) (f, fflags);
	if (err) {
		goto done;
	}
	mtx_lock(f->priv_mtx);

	/* reset sleep flag */
	f->flag_sleeping = 0;

	/* reset error flag */
	f->flag_iserror = 0;

	/* reset complete flag */
	f->flag_iscomplete = 0;

	/* reset select flag */
	f->flag_isselect = 0;

	/* reset flushing flag */
	f->flag_flushing = 0;

	/* reset ASYNC proc flag */
	f->async_p = NULL;

	mtx_lock(&usb_ref_lock);
	/* flag the fifo as opened to prevent others */
	f->curr_cpd = cpd;
	mtx_unlock(&usb_ref_lock);

	/* reset queue */
	usb_fifo_reset(f);

	mtx_unlock(f->priv_mtx);
done:
	return (err);
}

/*------------------------------------------------------------------------*
 *	usb_fifo_reset
 *------------------------------------------------------------------------*/
void
usb_fifo_reset(struct usb_fifo *f)
{
	struct usb_mbuf *m;

	if (f == NULL) {
		return;
	}
	while (1) {
		USB_IF_DEQUEUE(&f->used_q, m);
		if (m) {
			USB_IF_ENQUEUE(&f->free_q, m);
		} else {
			break;
		}
	}
	/* reset have fragment flag */
	f->flag_have_fragment = 0;
}

/*------------------------------------------------------------------------*
 *	usb_fifo_close
 *------------------------------------------------------------------------*/
static void
usb_fifo_close(struct usb_fifo *f, int fflags)
{
	int err;

	/* check if we are not opened */
	if (f->curr_cpd == NULL) {
		/* nothing to do - already closed */
		return;
	}
	mtx_lock(f->priv_mtx);

	/* clear current cdev private data pointer */
	mtx_lock(&usb_ref_lock);
	f->curr_cpd = NULL;
	mtx_unlock(&usb_ref_lock);

	/* check if we are watched by kevent */
	KNOTE_LOCKED(&f->selinfo.si_note, 0);

	/* check if we are selected */
	if (f->flag_isselect) {
		selwakeup(&f->selinfo);
		f->flag_isselect = 0;
	}
	/* check if a thread wants SIGIO */
	if (f->async_p != NULL) {
		PROC_LOCK(f->async_p);
		kern_psignal(f->async_p, SIGIO);
		PROC_UNLOCK(f->async_p);
		f->async_p = NULL;
	}
	/* remove FWRITE and FREAD flags */
	fflags &= ~(FWRITE | FREAD);

	/* flush written data, if any */
	if ((f->fifo_index & 1) == USB_FIFO_TX) {

		if (!f->flag_iserror) {

			/* set flushing flag */
			f->flag_flushing = 1;

			/* get the last packet in */
			if (f->flag_have_fragment) {
				struct usb_mbuf *m;
				f->flag_have_fragment = 0;
				USB_IF_DEQUEUE(&f->free_q, m);
				if (m) {
					USB_IF_ENQUEUE(&f->used_q, m);
				}
			}

			/* start write transfer, if not already started */
			(f->methods->f_start_write) (f);

			/* check if flushed already */
			while (f->flag_flushing &&
			    (!f->flag_iserror)) {
				/* wait until all data has been written */
				f->flag_sleeping = 1;
				err = cv_timedwait_sig(&f->cv_io, f->priv_mtx,
				    USB_MS_TO_TICKS(USB_DEFAULT_TIMEOUT));
				if (err) {
					DPRINTF("signal received\n");
					break;
				}
			}
		}
		fflags |= FWRITE;

		/* stop write transfer, if not already stopped */
		(f->methods->f_stop_write) (f);
	} else {
		fflags |= FREAD;

		/* stop write transfer, if not already stopped */
		(f->methods->f_stop_read) (f);
	}

	/* check if we are sleeping */
	if (f->flag_sleeping) {
		DPRINTFN(2, "Sleeping at close!\n");
	}
	mtx_unlock(f->priv_mtx);

	/* call close method */
	(f->methods->f_close) (f, fflags);

	DPRINTF("closed\n");
}

/*------------------------------------------------------------------------*
 *	usb_open - cdev callback
 *------------------------------------------------------------------------*/
static int
usb_open(struct cdev *dev, int fflags, int devtype, struct thread *td)
{
	struct usb_fs_privdata* pd = (struct usb_fs_privdata*)dev->si_drv1;
	struct usb_cdev_refdata refs;
	struct usb_cdev_privdata *cpd;
	int err;

	DPRINTFN(2, "%s fflags=0x%08x\n", devtoname(dev), fflags);

	KASSERT(fflags & (FREAD|FWRITE), ("invalid open flags"));
	if (((fflags & FREAD) && !(pd->mode & FREAD)) ||
	    ((fflags & FWRITE) && !(pd->mode & FWRITE))) {
		DPRINTFN(2, "access mode not supported\n");
		return (EPERM);
	}

	cpd = malloc(sizeof(*cpd), M_USBDEV, M_WAITOK | M_ZERO);

	usb_loc_fill(pd, cpd);
	err = usb_ref_device(cpd, &refs, 1);
	if (err) {
		DPRINTFN(2, "cannot ref device\n");
		free(cpd, M_USBDEV);
		return (ENXIO);
	}
	cpd->fflags = fflags;	/* access mode for open lifetime */

	/* create FIFOs, if any */
	err = usb_fifo_create(cpd, &refs);
	/* check for error */
	if (err) {
		DPRINTFN(2, "cannot create fifo\n");
		usb_unref_device(cpd, &refs);
		free(cpd, M_USBDEV);
		return (err);
	}
	if (fflags & FREAD) {
		err = usb_fifo_open(cpd, refs.rxfifo, fflags);
		if (err) {
			DPRINTFN(2, "read open failed\n");
			usb_unref_device(cpd, &refs);
			free(cpd, M_USBDEV);
			return (err);
		}
	}
	if (fflags & FWRITE) {
		err = usb_fifo_open(cpd, refs.txfifo, fflags);
		if (err) {
			DPRINTFN(2, "write open failed\n");
			if (fflags & FREAD) {
				usb_fifo_close(refs.rxfifo, fflags);
			}
			usb_unref_device(cpd, &refs);
			free(cpd, M_USBDEV);
			return (err);
		}
	}
	usb_unref_device(cpd, &refs);
	devfs_set_cdevpriv(cpd, usb_close);

	return (0);
}

/*------------------------------------------------------------------------*
 *	usb_close - cdev callback
 *------------------------------------------------------------------------*/
static void
usb_close(void *arg)
{
	struct usb_cdev_refdata refs;
	struct usb_cdev_privdata *cpd = arg;
	int err;

	DPRINTFN(2, "cpd=%p\n", cpd);

	err = usb_ref_device(cpd, &refs,
	    2 /* uref and allow detached state */);
	if (err) {
		DPRINTFN(2, "Cannot grab USB reference when "
		    "closing USB file handle\n");
		goto done;
	}
	if (cpd->fflags & FREAD) {
		usb_fifo_close(refs.rxfifo, cpd->fflags);
	}
	if (cpd->fflags & FWRITE) {
		usb_fifo_close(refs.txfifo, cpd->fflags);
	}
	usb_unref_device(cpd, &refs);
done:
	free(cpd, M_USBDEV);
}

static void
usb_dev_init(void *arg)
{
	mtx_init(&usb_ref_lock, "USB ref mutex", NULL, MTX_DEF);
	sx_init(&usb_sym_lock, "USB sym mutex");
	TAILQ_INIT(&usb_sym_head);

	/* check the UGEN methods */
	usb_fifo_check_methods(&usb_ugen_methods);
}

SYSINIT(usb_dev_init, SI_SUB_KLD, SI_ORDER_FIRST, usb_dev_init, NULL);

static void
usb_dev_init_post(void *arg)
{
	/*
	 * Create /dev/usb - this is needed for usbconfig(8), which
	 * needs a well-known device name to access.
	 */
	usb_dev = make_dev(&usb_static_devsw, 0, UID_ROOT, GID_OPERATOR,
	    0644, USB_DEVICE_NAME);
	if (usb_dev == NULL) {
		DPRINTFN(0, "Could not create usb bus device\n");
	}
}

SYSINIT(usb_dev_init_post, SI_SUB_KICK_SCHEDULER, SI_ORDER_FIRST, usb_dev_init_post, NULL);

static void
usb_dev_uninit(void *arg)
{
	if (usb_dev != NULL) {
		destroy_dev(usb_dev);
		usb_dev = NULL;
	}
	mtx_destroy(&usb_ref_lock);
	sx_destroy(&usb_sym_lock);
}

SYSUNINIT(usb_dev_uninit, SI_SUB_KICK_SCHEDULER, SI_ORDER_ANY, usb_dev_uninit, NULL);

static int
usb_ioctl_f_sub(struct usb_fifo *f, u_long cmd, void *addr,
    struct thread *td)
{
	int error = 0;

	switch (cmd) {
	case FIODTYPE:
		*(int *)addr = 0;	/* character device */
		break;

	case FIONBIO:
		/* handled by upper FS layer */
		break;

	case FIOASYNC:
		if (*(int *)addr) {
			if (f->async_p != NULL) {
				error = EBUSY;
				break;
			}
			f->async_p = USB_TD_GET_PROC(td);
		} else {
			f->async_p = NULL;
		}
		break;

		/* XXX this is not the most general solution */
	case TIOCSPGRP:
		if (f->async_p == NULL) {
			error = EINVAL;
			break;
		}
		if (*(int *)addr != USB_PROC_GET_GID(f->async_p)) {
			error = EPERM;
			break;
		}
		break;
	default:
		return (ENOIOCTL);
	}
	DPRINTFN(3, "cmd 0x%lx = %d\n", cmd, error);
	return (error);
}

/*------------------------------------------------------------------------*
 *	usb_ioctl - cdev callback
 *------------------------------------------------------------------------*/
static int
usb_ioctl(struct cdev *dev, u_long cmd, caddr_t addr, int fflag, struct thread* td)
{
	struct usb_cdev_refdata refs;
	struct usb_cdev_privdata* cpd;
	struct usb_fifo *f;
	int fflags;
	int err;

	DPRINTFN(2, "cmd=0x%lx\n", cmd);

	err = devfs_get_cdevpriv((void **)&cpd);
	if (err != 0)
		return (err);

	/* 
	 * Performance optimisation: We try to check for IOCTL's that
	 * don't need the USB reference first. Then we grab the USB
	 * reference if we need it!
	 */
	err = usb_ref_device(cpd, &refs, 0 /* no uref */ );
	if (err)
		return (ENXIO);

	fflags = cpd->fflags;

	f = NULL;			/* set default value */
	err = ENOIOCTL;			/* set default value */

	if (fflags & FWRITE) {
		f = refs.txfifo;
		err = usb_ioctl_f_sub(f, cmd, addr, td);
	}
	if (fflags & FREAD) {
		f = refs.rxfifo;
		err = usb_ioctl_f_sub(f, cmd, addr, td);
	}
	KASSERT(f != NULL, ("fifo not found"));
	if (err != ENOIOCTL)
		goto done;

	err = (f->methods->f_ioctl) (f, cmd, addr, fflags);

	DPRINTFN(2, "f_ioctl cmd 0x%lx = %d\n", cmd, err);

	if (err != ENOIOCTL)
		goto done;

	if (usb_usb_ref_device(cpd, &refs)) {
		/* we lost the reference */
		return (ENXIO);
	}

	err = (f->methods->f_ioctl_post) (f, cmd, addr, fflags);

	DPRINTFN(2, "f_ioctl_post cmd 0x%lx = %d\n", cmd, err);

	if (err == ENOIOCTL)
		err = ENOTTY;

	if (err)
		goto done;

	/* Wait for re-enumeration, if any */

	while (f->udev->re_enumerate_wait != USB_RE_ENUM_DONE) {

		usb_unref_device(cpd, &refs);

		usb_pause_mtx(NULL, hz / 128);

		while (usb_ref_device(cpd, &refs, 1 /* need uref */)) {
			if (usb_ref_device(cpd, &refs, 0)) {
				/* device no longer exists */
				return (ENXIO);
			}
			usb_unref_device(cpd, &refs);
			usb_pause_mtx(NULL, hz / 128);
		}
	}

done:
	usb_unref_device(cpd, &refs);
	return (err);
}

static void
usb_filter_detach(struct knote *kn)
{
	struct usb_fifo *f = kn->kn_hook;
	knlist_remove(&f->selinfo.si_note, kn, 0);
}

static int
usb_filter_write(struct knote *kn, long hint)
{
	struct usb_cdev_privdata* cpd;
	struct usb_fifo *f;
	struct usb_mbuf *m;

	DPRINTFN(2, "\n");

	f = kn->kn_hook;

	USB_MTX_ASSERT(f->priv_mtx, MA_OWNED);

	cpd = f->curr_cpd;
	if (cpd == NULL) {
		m = (void *)1;
	} else if (f->fs_ep_max == 0) {
		if (f->flag_iserror) {
			/* we got an error */
			m = (void *)1;
		} else {
			if (f->queue_data == NULL) {
				/*
				 * start write transfer, if not
				 * already started
				 */
				(f->methods->f_start_write) (f);
			}
			/* check if any packets are available */
			USB_IF_POLL(&f->free_q, m);
		}
	} else {
		if (f->flag_iscomplete) {
			m = (void *)1;
		} else {
			m = NULL;
		}
	}
	return (m ? 1 : 0);
}

static int
usb_filter_read(struct knote *kn, long hint)
{
	struct usb_cdev_privdata* cpd;
	struct usb_fifo *f;
	struct usb_mbuf *m;

	DPRINTFN(2, "\n");

	f = kn->kn_hook;

	USB_MTX_ASSERT(f->priv_mtx, MA_OWNED);

	cpd = f->curr_cpd;
	if (cpd == NULL) {
		m = (void *)1;
	} else if (f->fs_ep_max == 0) {
		if (f->flag_iserror) {
			/* we have an error */
			m = (void *)1;
		} else {
			if (f->queue_data == NULL) {
				/*
				 * start read transfer, if not
				 * already started
				 */
				(f->methods->f_start_read) (f);
			}
			/* check if any packets are available */
			USB_IF_POLL(&f->used_q, m);

			/* start reading data, if any */
			if (m == NULL)
				(f->methods->f_start_read) (f);
		}
	} else {
		if (f->flag_iscomplete) {
			m = (void *)1;
		} else {
			m = NULL;
		}
	}
	return (m ? 1 : 0);
}

static struct filterops usb_filtops_write = {
	.f_isfd = 1,
	.f_detach = usb_filter_detach,
	.f_event = usb_filter_write,
};

static struct filterops usb_filtops_read = {
	.f_isfd = 1,
	.f_detach = usb_filter_detach,
	.f_event = usb_filter_read,
};


/* ARGSUSED */
static int
usb_kqfilter(struct cdev* dev, struct knote *kn)
{
	struct usb_cdev_refdata refs;
	struct usb_cdev_privdata* cpd;
	struct usb_fifo *f;
	int fflags;
	int err = EINVAL;

	DPRINTFN(2, "\n");

	if (devfs_get_cdevpriv((void **)&cpd) != 0 ||
	    usb_ref_device(cpd, &refs, 0) != 0)
		return (ENXIO);

	fflags = cpd->fflags;

	/* Figure out who needs service */
	switch (kn->kn_filter) {
	case EVFILT_WRITE:
		if (fflags & FWRITE) {
			f = refs.txfifo;
			kn->kn_fop = &usb_filtops_write;
			err = 0;
		}
		break;
	case EVFILT_READ:
		if (fflags & FREAD) {
			f = refs.rxfifo;
			kn->kn_fop = &usb_filtops_read;
			err = 0;
		}
		break;
	default:
		err = EOPNOTSUPP;
		break;
	}

	if (err == 0) {
		kn->kn_hook = f;
		mtx_lock(f->priv_mtx);
		knlist_add(&f->selinfo.si_note, kn, 1);
		mtx_unlock(f->priv_mtx);
	}

	usb_unref_device(cpd, &refs);
	return (err);
}

/* ARGSUSED */
static int
usb_poll(struct cdev* dev, int events, struct thread* td)
{
	struct usb_cdev_refdata refs;
	struct usb_cdev_privdata* cpd;
	struct usb_fifo *f;
	struct usb_mbuf *m;
	int fflags, revents;

	if (devfs_get_cdevpriv((void **)&cpd) != 0 ||
	    usb_ref_device(cpd, &refs, 0) != 0)
		return (events &
		    (POLLHUP|POLLIN|POLLRDNORM|POLLOUT|POLLWRNORM));

	fflags = cpd->fflags;

	/* Figure out who needs service */
	revents = 0;
	if ((events & (POLLOUT | POLLWRNORM)) &&
	    (fflags & FWRITE)) {

		f = refs.txfifo;

		mtx_lock(f->priv_mtx);

		if (!refs.is_usbfs) {
			if (f->flag_iserror) {
				/* we got an error */
				m = (void *)1;
			} else {
				if (f->queue_data == NULL) {
					/*
					 * start write transfer, if not
					 * already started
					 */
					(f->methods->f_start_write) (f);
				}
				/* check if any packets are available */
				USB_IF_POLL(&f->free_q, m);
			}
		} else {
			if (f->flag_iscomplete) {
				m = (void *)1;
			} else {
				m = NULL;
			}
		}

		if (m) {
			revents |= events & (POLLOUT | POLLWRNORM);
		} else {
			f->flag_isselect = 1;
			selrecord(td, &f->selinfo);
		}

		mtx_unlock(f->priv_mtx);
	}
	if ((events & (POLLIN | POLLRDNORM)) &&
	    (fflags & FREAD)) {

		f = refs.rxfifo;

		mtx_lock(f->priv_mtx);

		if (!refs.is_usbfs) {
			if (f->flag_iserror) {
				/* we have an error */
				m = (void *)1;
			} else {
				if (f->queue_data == NULL) {
					/*
					 * start read transfer, if not
					 * already started
					 */
					(f->methods->f_start_read) (f);
				}
				/* check if any packets are available */
				USB_IF_POLL(&f->used_q, m);
			}
		} else {
			if (f->flag_iscomplete) {
				m = (void *)1;
			} else {
				m = NULL;
			}
		}

		if (m) {
			revents |= events & (POLLIN | POLLRDNORM);
		} else {
			f->flag_isselect = 1;
			selrecord(td, &f->selinfo);

			if (!refs.is_usbfs) {
				/* start reading data */
				(f->methods->f_start_read) (f);
			}
		}

		mtx_unlock(f->priv_mtx);
	}
	usb_unref_device(cpd, &refs);
	return (revents);
}

static int
usb_read(struct cdev *dev, struct uio *uio, int ioflag)
{
	struct usb_cdev_refdata refs;
	struct usb_cdev_privdata* cpd;
	struct usb_fifo *f;
	struct usb_mbuf *m;
	int io_len;
	int err;
	uint8_t tr_data = 0;

	err = devfs_get_cdevpriv((void **)&cpd);
	if (err != 0)
		return (err);

	err = usb_ref_device(cpd, &refs, 0 /* no uref */ );
	if (err)
		return (ENXIO);

	f = refs.rxfifo;
	if (f == NULL) {
		/* should not happen */
		usb_unref_device(cpd, &refs);
		return (EPERM);
	}

	mtx_lock(f->priv_mtx);

	/* check for permanent read error */
	if (f->flag_iserror) {
		err = EIO;
		goto done;
	}
	/* check if USB-FS interface is active */
	if (refs.is_usbfs) {
		/*
		 * The queue is used for events that should be
		 * retrieved using the "USB_FS_COMPLETE" ioctl.
		 */
		err = EINVAL;
		goto done;
	}
	while (uio->uio_resid > 0) {

		USB_IF_DEQUEUE(&f->used_q, m);

		if (m == NULL) {

			/* start read transfer, if not already started */

			(f->methods->f_start_read) (f);

			if (ioflag & IO_NDELAY) {
				if (tr_data) {
					/* return length before error */
					break;
				}
				err = EWOULDBLOCK;
				break;
			}
			DPRINTF("sleeping\n");

			err = usb_fifo_wait(f);
			if (err) {
				break;
			}
			continue;
		}
		if (f->methods->f_filter_read) {
			/*
			 * Sometimes it is convenient to process data at the
			 * expense of a userland process instead of a kernel
			 * process.
			 */
			(f->methods->f_filter_read) (f, m);
		}
		tr_data = 1;

		io_len = MIN(m->cur_data_len, uio->uio_resid);

		DPRINTFN(2, "transfer %d bytes from %p\n",
		    io_len, m->cur_data_ptr);

		err = usb_fifo_uiomove(f,
		    m->cur_data_ptr, io_len, uio);

		m->cur_data_len -= io_len;
		m->cur_data_ptr += io_len;

		if (m->cur_data_len == 0) {

			uint8_t last_packet;

			last_packet = m->last_packet;

			USB_IF_ENQUEUE(&f->free_q, m);

			if (last_packet) {
				/* keep framing */
				break;
			}
		} else {
			USB_IF_PREPEND(&f->used_q, m);
		}

		if (err) {
			break;
		}
	}
done:
	mtx_unlock(f->priv_mtx);

	usb_unref_device(cpd, &refs);

	return (err);
}

static int
usb_write(struct cdev *dev, struct uio *uio, int ioflag)
{
	struct usb_cdev_refdata refs;
	struct usb_cdev_privdata* cpd;
	struct usb_fifo *f;
	struct usb_mbuf *m;
	uint8_t *pdata;
	int io_len;
	int err;
	uint8_t tr_data = 0;

	DPRINTFN(2, "\n");

	err = devfs_get_cdevpriv((void **)&cpd);
	if (err != 0)
		return (err);

	err = usb_ref_device(cpd, &refs, 0 /* no uref */ );
	if (err)
		return (ENXIO);

	f = refs.txfifo;
	if (f == NULL) {
		/* should not happen */
		usb_unref_device(cpd, &refs);
		return (EPERM);
	}

	mtx_lock(f->priv_mtx);

	/* check for permanent write error */
	if (f->flag_iserror) {
		err = EIO;
		goto done;
	}
	/* check if USB-FS interface is active */
	if (refs.is_usbfs) {
		/*
		 * The queue is used for events that should be
		 * retrieved using the "USB_FS_COMPLETE" ioctl.
		 */
		err = EINVAL;
		goto done;
	}
	if (f->queue_data == NULL) {
		/* start write transfer, if not already started */
		(f->methods->f_start_write) (f);
	}
	/* we allow writing zero length data */
	do {
		USB_IF_DEQUEUE(&f->free_q, m);

		if (m == NULL) {

			if (ioflag & IO_NDELAY) {
				if (tr_data) {
					/* return length before error */
					break;
				}
				err = EWOULDBLOCK;
				break;
			}
			DPRINTF("sleeping\n");

			err = usb_fifo_wait(f);
			if (err) {
				break;
			}
			continue;
		}
		tr_data = 1;

		if (f->flag_have_fragment == 0) {
			USB_MBUF_RESET(m);
			io_len = m->cur_data_len;
			pdata = m->cur_data_ptr;
			if (io_len > uio->uio_resid)
				io_len = uio->uio_resid;
			m->cur_data_len = io_len;
		} else {
			io_len = m->max_data_len - m->cur_data_len;
			pdata = m->cur_data_ptr + m->cur_data_len;
			if (io_len > uio->uio_resid)
				io_len = uio->uio_resid;
			m->cur_data_len += io_len;
		}

		DPRINTFN(2, "transfer %d bytes to %p\n",
		    io_len, pdata);

		err = usb_fifo_uiomove(f, pdata, io_len, uio);

		if (err) {
			f->flag_have_fragment = 0;
			USB_IF_ENQUEUE(&f->free_q, m);
			break;
		}

		/* check if the buffer is ready to be transmitted */

		if ((f->flag_write_defrag == 0) ||
		    (m->cur_data_len == m->max_data_len)) {
			f->flag_have_fragment = 0;

			/*
			 * Check for write filter:
			 *
			 * Sometimes it is convenient to process data
			 * at the expense of a userland process
			 * instead of a kernel process.
			 */
			if (f->methods->f_filter_write) {
				(f->methods->f_filter_write) (f, m);
			}

			/* Put USB mbuf in the used queue */
			USB_IF_ENQUEUE(&f->used_q, m);

			/* Start writing data, if not already started */
			(f->methods->f_start_write) (f);
		} else {
			/* Wait for more data or close */
			f->flag_have_fragment = 1;
			USB_IF_PREPEND(&f->free_q, m);
		}

	} while (uio->uio_resid > 0);
done:
	mtx_unlock(f->priv_mtx);

	usb_unref_device(cpd, &refs);

	return (err);
}

int
usb_static_ioctl(struct cdev *dev, u_long cmd, caddr_t data, int fflag,
    struct thread *td)
{
	union {
		struct usb_read_dir *urd;
		void* data;
	} u;
	int err;

	u.data = data;
	switch (cmd) {
		case USB_READ_DIR:
			err = usb_read_symlink(u.urd->urd_data,
			    u.urd->urd_startentry, u.urd->urd_maxlen);
			break;
		case USB_DEV_QUIRK_GET:
		case USB_QUIRK_NAME_GET:
		case USB_DEV_QUIRK_ADD:
		case USB_DEV_QUIRK_REMOVE:
			err = usb_quirk_ioctl_p(cmd, data, fflag, td);
			break;
		case USB_GET_TEMPLATE:
			*(int *)data = usb_template;
			err = 0;
			break;
		case USB_SET_TEMPLATE:
			err = priv_check(curthread, PRIV_DRIVER);
			if (err)
				break;
			usb_template = *(int *)data;
			break;
		default:
			err = ENOTTY;
			break;
	}
	return (err);
}

static int
usb_fifo_uiomove(struct usb_fifo *f, void *cp,
    int n, struct uio *uio)
{
	int error;

	mtx_unlock(f->priv_mtx);

	/*
	 * "uiomove()" can sleep so one needs to make a wrapper,
	 * exiting the mutex and checking things:
	 */
	error = uiomove(cp, n, uio);

	mtx_lock(f->priv_mtx);

	return (error);
}

int
usb_fifo_wait(struct usb_fifo *f)
{
	int err;

	USB_MTX_ASSERT(f->priv_mtx, MA_OWNED);

	if (f->flag_iserror) {
		/* we are gone */
		return (EIO);
	}
	f->flag_sleeping = 1;

	err = cv_wait_sig(&f->cv_io, f->priv_mtx);

	if (f->flag_iserror) {
		/* we are gone */
		err = EIO;
	}
	return (err);
}

void
usb_fifo_signal(struct usb_fifo *f)
{
	if (f->flag_sleeping) {
		f->flag_sleeping = 0;
		cv_broadcast(&f->cv_io);
	}
}

void
usb_fifo_wakeup(struct usb_fifo *f)
{
	usb_fifo_signal(f);

	KNOTE_LOCKED(&f->selinfo.si_note, 0);

	if (f->flag_isselect) {
		selwakeup(&f->selinfo);
		f->flag_isselect = 0;
	}
	if (f->async_p != NULL) {
		PROC_LOCK(f->async_p);
		kern_psignal(f->async_p, SIGIO);
		PROC_UNLOCK(f->async_p);
	}
}

static int
usb_fifo_dummy_open(struct usb_fifo *fifo, int fflags)
{
	return (0);
}

static void
usb_fifo_dummy_close(struct usb_fifo *fifo, int fflags)
{
	return;
}

static int
usb_fifo_dummy_ioctl(struct usb_fifo *fifo, u_long cmd, void *addr, int fflags)
{
	return (ENOIOCTL);
}

static void
usb_fifo_dummy_cmd(struct usb_fifo *fifo)
{
	fifo->flag_flushing = 0;	/* not flushing */
}

static void
usb_fifo_check_methods(struct usb_fifo_methods *pm)
{
	/* check that all callback functions are OK */

	if (pm->f_open == NULL)
		pm->f_open = &usb_fifo_dummy_open;

	if (pm->f_close == NULL)
		pm->f_close = &usb_fifo_dummy_close;

	if (pm->f_ioctl == NULL)
		pm->f_ioctl = &usb_fifo_dummy_ioctl;

	if (pm->f_ioctl_post == NULL)
		pm->f_ioctl_post = &usb_fifo_dummy_ioctl;

	if (pm->f_start_read == NULL)
		pm->f_start_read = &usb_fifo_dummy_cmd;

	if (pm->f_stop_read == NULL)
		pm->f_stop_read = &usb_fifo_dummy_cmd;

	if (pm->f_start_write == NULL)
		pm->f_start_write = &usb_fifo_dummy_cmd;

	if (pm->f_stop_write == NULL)
		pm->f_stop_write = &usb_fifo_dummy_cmd;
}

/*------------------------------------------------------------------------*
 *	usb_fifo_attach
 *
 * The following function will create a duplex FIFO.
 *
 * Return values:
 * 0: Success.
 * Else: Failure.
 *------------------------------------------------------------------------*/
int
usb_fifo_attach(struct usb_device *udev, void *priv_sc,
    struct mtx *priv_mtx, struct usb_fifo_methods *pm,
    struct usb_fifo_sc *f_sc, uint16_t unit, int16_t subunit,
    uint8_t iface_index, uid_t uid, gid_t gid, int mode)
{
	struct usb_fifo *f_tx;
	struct usb_fifo *f_rx;
	char devname[32];
	uint8_t n;

	f_sc->fp[USB_FIFO_TX] = NULL;
	f_sc->fp[USB_FIFO_RX] = NULL;

	if (pm == NULL)
		return (EINVAL);

	/* check the methods */
	usb_fifo_check_methods(pm);

	if (priv_mtx == NULL)
		priv_mtx = &Giant;

	/* search for a free FIFO slot */
	for (n = 0;; n += 2) {

		if (n == USB_FIFO_MAX) {
			/* end of FIFOs reached */
			return (ENOMEM);
		}
		/* Check for TX FIFO */
		if (udev->fifo[n + USB_FIFO_TX] != NULL) {
			continue;
		}
		/* Check for RX FIFO */
		if (udev->fifo[n + USB_FIFO_RX] != NULL) {
			continue;
		}
		break;
	}

	f_tx = usb_fifo_alloc(priv_mtx);
	f_rx = usb_fifo_alloc(priv_mtx);

	if ((f_tx == NULL) || (f_rx == NULL)) {
		usb_fifo_free(f_tx);
		usb_fifo_free(f_rx);
		return (ENOMEM);
	}
	/* initialise FIFO structures */

	f_tx->fifo_index = n + USB_FIFO_TX;
	f_tx->dev_ep_index = -1;
	f_tx->priv_sc0 = priv_sc;
	f_tx->methods = pm;
	f_tx->iface_index = iface_index;
	f_tx->udev = udev;

	f_rx->fifo_index = n + USB_FIFO_RX;
	f_rx->dev_ep_index = -1;
	f_rx->priv_sc0 = priv_sc;
	f_rx->methods = pm;
	f_rx->iface_index = iface_index;
	f_rx->udev = udev;

	f_sc->fp[USB_FIFO_TX] = f_tx;
	f_sc->fp[USB_FIFO_RX] = f_rx;

	mtx_lock(&usb_ref_lock);
	udev->fifo[f_tx->fifo_index] = f_tx;
	udev->fifo[f_rx->fifo_index] = f_rx;
	mtx_unlock(&usb_ref_lock);

	for (n = 0; n != 4; n++) {

		if (pm->basename[n] == NULL) {
			continue;
		}
		if (subunit < 0) {
			if (snprintf(devname, sizeof(devname),
			    "%s%u%s", pm->basename[n],
			    unit, pm->postfix[n] ?
			    pm->postfix[n] : "")) {
				/* ignore */
			}
		} else {
			if (snprintf(devname, sizeof(devname),
			    "%s%u.%d%s", pm->basename[n],
			    unit, subunit, pm->postfix[n] ?
			    pm->postfix[n] : "")) {
				/* ignore */
			}
		}

		/*
		 * Distribute the symbolic links into two FIFO structures:
		 */
		if (n & 1) {
			f_rx->symlink[n / 2] =
			    usb_alloc_symlink(devname);
		} else {
			f_tx->symlink[n / 2] =
			    usb_alloc_symlink(devname);
		}

		/* Create the device */
		f_sc->dev = usb_make_dev(udev, devname, -1,
		    f_tx->fifo_index & f_rx->fifo_index,
		    FREAD|FWRITE, uid, gid, mode);
	}

	DPRINTFN(2, "attached %p/%p\n", f_tx, f_rx);
	return (0);
}

/*------------------------------------------------------------------------*
 *	usb_fifo_alloc_buffer
 *
 * Return values:
 * 0: Success
 * Else failure
 *------------------------------------------------------------------------*/
int
usb_fifo_alloc_buffer(struct usb_fifo *f, usb_size_t bufsize,
    uint16_t nbuf)
{
	usb_fifo_free_buffer(f);

	/* allocate an endpoint */
	f->free_q.ifq_maxlen = nbuf;
	f->used_q.ifq_maxlen = nbuf;

	f->queue_data = usb_alloc_mbufs(
	    M_USBDEV, &f->free_q, bufsize, nbuf);

	if ((f->queue_data == NULL) && bufsize && nbuf) {
		return (ENOMEM);
	}
	return (0);			/* success */
}

/*------------------------------------------------------------------------*
 *	usb_fifo_free_buffer
 *
 * This function will free the buffers associated with a FIFO. This
 * function can be called multiple times in a row.
 *------------------------------------------------------------------------*/
void
usb_fifo_free_buffer(struct usb_fifo *f)
{
	if (f->queue_data) {
		/* free old buffer */
		free(f->queue_data, M_USBDEV);
		f->queue_data = NULL;
	}
	/* reset queues */

	memset(&f->free_q, 0, sizeof(f->free_q));
	memset(&f->used_q, 0, sizeof(f->used_q));
}

void
usb_fifo_detach(struct usb_fifo_sc *f_sc)
{
	if (f_sc == NULL) {
		return;
	}
	usb_fifo_free(f_sc->fp[USB_FIFO_TX]);
	usb_fifo_free(f_sc->fp[USB_FIFO_RX]);

	f_sc->fp[USB_FIFO_TX] = NULL;
	f_sc->fp[USB_FIFO_RX] = NULL;

	usb_destroy_dev(f_sc->dev);

	f_sc->dev = NULL;

	DPRINTFN(2, "detached %p\n", f_sc);
}

usb_size_t
usb_fifo_put_bytes_max(struct usb_fifo *f)
{
	struct usb_mbuf *m;
	usb_size_t len;

	USB_IF_POLL(&f->free_q, m);

	if (m) {
		len = m->max_data_len;
	} else {
		len = 0;
	}
	return (len);
}

/*------------------------------------------------------------------------*
 *	usb_fifo_put_data
 *
 * what:
 *  0 - normal operation
 *  1 - set last packet flag to enforce framing
 *------------------------------------------------------------------------*/
void
usb_fifo_put_data(struct usb_fifo *f, struct usb_page_cache *pc,
    usb_frlength_t offset, usb_frlength_t len, uint8_t what)
{
	struct usb_mbuf *m;
	usb_frlength_t io_len;

	while (len || (what == 1)) {

		USB_IF_DEQUEUE(&f->free_q, m);

		if (m) {
			USB_MBUF_RESET(m);

			io_len = MIN(len, m->cur_data_len);

			usbd_copy_out(pc, offset, m->cur_data_ptr, io_len);

			m->cur_data_len = io_len;
			offset += io_len;
			len -= io_len;

			if ((len == 0) && (what == 1)) {
				m->last_packet = 1;
			}
			USB_IF_ENQUEUE(&f->used_q, m);

			usb_fifo_wakeup(f);

			if ((len == 0) || (what == 1)) {
				break;
			}
		} else {
			break;
		}
	}
}

void
usb_fifo_put_data_linear(struct usb_fifo *f, void *ptr,
    usb_size_t len, uint8_t what)
{
	struct usb_mbuf *m;
	usb_size_t io_len;

	while (len || (what == 1)) {

		USB_IF_DEQUEUE(&f->free_q, m);

		if (m) {
			USB_MBUF_RESET(m);

			io_len = MIN(len, m->cur_data_len);

			memcpy(m->cur_data_ptr, ptr, io_len);

			m->cur_data_len = io_len;
			ptr = USB_ADD_BYTES(ptr, io_len);
			len -= io_len;

			if ((len == 0) && (what == 1)) {
				m->last_packet = 1;
			}
			USB_IF_ENQUEUE(&f->used_q, m);

			usb_fifo_wakeup(f);

			if ((len == 0) || (what == 1)) {
				break;
			}
		} else {
			break;
		}
	}
}

uint8_t
usb_fifo_put_data_buffer(struct usb_fifo *f, void *ptr, usb_size_t len)
{
	struct usb_mbuf *m;

	USB_IF_DEQUEUE(&f->free_q, m);

	if (m) {
		m->cur_data_len = len;
		m->cur_data_ptr = ptr;
		USB_IF_ENQUEUE(&f->used_q, m);
		usb_fifo_wakeup(f);
		return (1);
	}
	return (0);
}

void
usb_fifo_put_data_error(struct usb_fifo *f)
{
	f->flag_iserror = 1;
	usb_fifo_wakeup(f);
}

/*------------------------------------------------------------------------*
 *	usb_fifo_get_data
 *
 * what:
 *  0 - normal operation
 *  1 - only get one "usb_mbuf"
 *
 * returns:
 *  0 - no more data
 *  1 - data in buffer
 *------------------------------------------------------------------------*/
uint8_t
usb_fifo_get_data(struct usb_fifo *f, struct usb_page_cache *pc,
    usb_frlength_t offset, usb_frlength_t len, usb_frlength_t *actlen,
    uint8_t what)
{
	struct usb_mbuf *m;
	usb_frlength_t io_len;
	uint8_t tr_data = 0;

	actlen[0] = 0;

	while (1) {

		USB_IF_DEQUEUE(&f->used_q, m);

		if (m) {

			tr_data = 1;

			io_len = MIN(len, m->cur_data_len);

			usbd_copy_in(pc, offset, m->cur_data_ptr, io_len);

			len -= io_len;
			offset += io_len;
			actlen[0] += io_len;
			m->cur_data_ptr += io_len;
			m->cur_data_len -= io_len;

			if ((m->cur_data_len == 0) || (what == 1)) {
				USB_IF_ENQUEUE(&f->free_q, m);

				usb_fifo_wakeup(f);

				if (what == 1) {
					break;
				}
			} else {
				USB_IF_PREPEND(&f->used_q, m);
			}
		} else {

			if (tr_data) {
				/* wait for data to be written out */
				break;
			}
			if (f->flag_flushing) {
				/* check if we should send a short packet */
				if (f->flag_short != 0) {
					f->flag_short = 0;
					tr_data = 1;
					break;
				}
				/* flushing complete */
				f->flag_flushing = 0;
				usb_fifo_wakeup(f);
			}
			break;
		}
		if (len == 0) {
			break;
		}
	}
	return (tr_data);
}

uint8_t
usb_fifo_get_data_linear(struct usb_fifo *f, void *ptr,
    usb_size_t len, usb_size_t *actlen, uint8_t what)
{
	struct usb_mbuf *m;
	usb_size_t io_len;
	uint8_t tr_data = 0;

	actlen[0] = 0;

	while (1) {

		USB_IF_DEQUEUE(&f->used_q, m);

		if (m) {

			tr_data = 1;

			io_len = MIN(len, m->cur_data_len);

			memcpy(ptr, m->cur_data_ptr, io_len);

			len -= io_len;
			ptr = USB_ADD_BYTES(ptr, io_len);
			actlen[0] += io_len;
			m->cur_data_ptr += io_len;
			m->cur_data_len -= io_len;

			if ((m->cur_data_len == 0) || (what == 1)) {
				USB_IF_ENQUEUE(&f->free_q, m);

				usb_fifo_wakeup(f);

				if (what == 1) {
					break;
				}
			} else {
				USB_IF_PREPEND(&f->used_q, m);
			}
		} else {

			if (tr_data) {
				/* wait for data to be written out */
				break;
			}
			if (f->flag_flushing) {
				/* check if we should send a short packet */
				if (f->flag_short != 0) {
					f->flag_short = 0;
					tr_data = 1;
					break;
				}
				/* flushing complete */
				f->flag_flushing = 0;
				usb_fifo_wakeup(f);
			}
			break;
		}
		if (len == 0) {
			break;
		}
	}
	return (tr_data);
}

uint8_t
usb_fifo_get_data_buffer(struct usb_fifo *f, void **pptr, usb_size_t *plen)
{
	struct usb_mbuf *m;

	USB_IF_POLL(&f->used_q, m);

	if (m) {
		*plen = m->cur_data_len;
		*pptr = m->cur_data_ptr;

		return (1);
	}
	return (0);
}

void
usb_fifo_get_data_error(struct usb_fifo *f)
{
	f->flag_iserror = 1;
	usb_fifo_wakeup(f);
}

/*------------------------------------------------------------------------*
 *	usb_alloc_symlink
 *
 * Return values:
 * NULL: Failure
 * Else: Pointer to symlink entry
 *------------------------------------------------------------------------*/
struct usb_symlink *
usb_alloc_symlink(const char *target)
{
	struct usb_symlink *ps;

	ps = malloc(sizeof(*ps), M_USBDEV, M_WAITOK);
	if (ps == NULL) {
		return (ps);
	}
	/* XXX no longer needed */
	strlcpy(ps->src_path, target, sizeof(ps->src_path));
	ps->src_len = strlen(ps->src_path);
	strlcpy(ps->dst_path, target, sizeof(ps->dst_path));
	ps->dst_len = strlen(ps->dst_path);

	sx_xlock(&usb_sym_lock);
	TAILQ_INSERT_TAIL(&usb_sym_head, ps, sym_entry);
	sx_unlock(&usb_sym_lock);
	return (ps);
}

/*------------------------------------------------------------------------*
 *	usb_free_symlink
 *------------------------------------------------------------------------*/
void
usb_free_symlink(struct usb_symlink *ps)
{
	if (ps == NULL) {
		return;
	}
	sx_xlock(&usb_sym_lock);
	TAILQ_REMOVE(&usb_sym_head, ps, sym_entry);
	sx_unlock(&usb_sym_lock);

	free(ps, M_USBDEV);
}

/*------------------------------------------------------------------------*
 *	usb_read_symlink
 *
 * Return value:
 * 0: Success
 * Else: Failure
 *------------------------------------------------------------------------*/
int
usb_read_symlink(uint8_t *user_ptr, uint32_t startentry, uint32_t user_len)
{
	struct usb_symlink *ps;
	uint32_t temp;
	uint32_t delta = 0;
	uint8_t len;
	int error = 0;

	sx_xlock(&usb_sym_lock);

	TAILQ_FOREACH(ps, &usb_sym_head, sym_entry) {

		/*
		 * Compute total length of source and destination symlink
		 * strings pluss one length byte and two NUL bytes:
		 */
		temp = ps->src_len + ps->dst_len + 3;

		if (temp > 255) {
			/*
			 * Skip entry because this length cannot fit
			 * into one byte:
			 */
			continue;
		}
		if (startentry != 0) {
			/* decrement read offset */
			startentry--;
			continue;
		}
		if (temp > user_len) {
			/* out of buffer space */
			break;
		}
		len = temp;

		/* copy out total length */

		error = copyout(&len,
		    USB_ADD_BYTES(user_ptr, delta), 1);
		if (error) {
			break;
		}
		delta += 1;

		/* copy out source string */

		error = copyout(ps->src_path,
		    USB_ADD_BYTES(user_ptr, delta), ps->src_len);
		if (error) {
			break;
		}
		len = 0;
		delta += ps->src_len;
		error = copyout(&len,
		    USB_ADD_BYTES(user_ptr, delta), 1);
		if (error) {
			break;
		}
		delta += 1;

		/* copy out destination string */

		error = copyout(ps->dst_path,
		    USB_ADD_BYTES(user_ptr, delta), ps->dst_len);
		if (error) {
			break;
		}
		len = 0;
		delta += ps->dst_len;
		error = copyout(&len,
		    USB_ADD_BYTES(user_ptr, delta), 1);
		if (error) {
			break;
		}
		delta += 1;

		user_len -= temp;
	}

	/* a zero length entry indicates the end */

	if ((user_len != 0) && (error == 0)) {

		len = 0;

		error = copyout(&len,
		    USB_ADD_BYTES(user_ptr, delta), 1);
	}
	sx_unlock(&usb_sym_lock);
	return (error);
}

void
usb_fifo_set_close_zlp(struct usb_fifo *f, uint8_t onoff)
{
	if (f == NULL)
		return;

	/* send a Zero Length Packet, ZLP, before close */
	f->flag_short = onoff;
}

void
usb_fifo_set_write_defrag(struct usb_fifo *f, uint8_t onoff)
{
	if (f == NULL)
		return;

	/* defrag written data */
	f->flag_write_defrag = onoff;
	/* reset defrag state */
	f->flag_have_fragment = 0;
}

void *
usb_fifo_softc(struct usb_fifo *f)
{
	return (f->priv_sc0);
}
#endif	/* USB_HAVE_UGEN */
