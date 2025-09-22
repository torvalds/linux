/*	$OpenBSD: urng.c,v 1.11 2024/05/23 03:21:09 jsg Exp $ */

/*
 * Copyright (c) 2017 Jasper Lievisse Adriaanse <jasper@openbsd.org>
 * Copyright (c) 2017 Aaron Bieber <abieber@openbsd.org>
 * Copyright (C) 2015 Sean Levy <attila@stalphonsos.com>
 * Copyright (c) 2007 Marc Balmer <mbalmer@openbsd.org>
 * Copyright (c) 2006 Alexander Yurchenko <grange@openbsd.org>
 *
 * Permission to use, copy, modify, and distribute this software for any
 * purpose with or without fee is hereby granted, provided that the above
 * copyright notice and this permission notice appear in all copies.
 *
 * THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
 * WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF
 * MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR
 * ANY SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
 * WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN
 * ACTION OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF
 * OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
 */

/*
 * Universal TRNG driver for a collection of TRNG devices:
 * - ChaosKey TRNG
 *   http://altusmetrum.org/ChaosKey/
 * - Alea II TRNG.  Produces 100kbit/sec of entropy by black magic
 *   http://www.araneus.fi/products/alea2/en/
 */

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/device.h>
#include <sys/timeout.h>

#include <dev/usb/usb.h>
#include <dev/usb/usbdi.h>
#include <dev/usb/usbdevs.h>

#define DEVNAME(_sc) ((_sc)->sc_dev.dv_xname)

#ifdef URNG_DEBUG
#define DPRINTF(x)	printf x
#else
#define DPRINTF(x)
#endif

/*
 * Define URNG_MEASURE_RATE to periodically log rate at which we provide
 * random data to the kernel.
 */
#ifdef URNG_MEASURE_RATE
#define URNG_RATE_SECONDS 30
#endif

struct urng_chip {
	int	bufsiz;
	int	endpoint;
	int	ctl_iface_idx;
	int	msecs;
	int	read_timeout;
};

struct urng_softc {
	struct  device		 sc_dev;
	struct  usbd_device	*sc_udev;
	struct  usbd_pipe	*sc_inpipe;
	struct  timeout 	 sc_timeout;
	struct  usb_task	 sc_task;
	struct  usbd_xfer	*sc_xfer;
	struct	urng_chip	 sc_chip;
	int     		*sc_buf;
	int			 sc_product;
#ifdef URNG_MEASURE_RATE
	struct	timeval		 sc_start;
	struct	timeval 	 sc_cur;
	int			 sc_counted_bytes;
	u_char			 sc_first_run;
#endif
};

int urng_match(struct device *, void *, void *);
void urng_attach(struct device *, struct device *, void *);
int urng_detach(struct device *, int);
void urng_task(void *);
void urng_timeout(void *);

struct cfdriver urng_cd = {
	NULL, "urng", DV_DULL
};

const struct cfattach urng_ca = {
	sizeof(struct urng_softc), urng_match, urng_attach, urng_detach
};

struct urng_type {
	struct usb_devno	urng_dev;
	struct urng_chip	urng_chip;
};

static const struct urng_type urng_devs[] = {
	{ { USB_VENDOR_OPENMOKO2, USB_PRODUCT_OPENMOKO2_CHAOSKEY },
	  {64, 5, 0, 100, 5000} },
	{ { USB_VENDOR_ARANEUS, USB_PRODUCT_ARANEUS_ALEA },
	  {128, 1, 0, 100, 5000} },
};
#define urng_lookup(v, p) ((struct urng_type *)usb_lookup(urng_devs, v, p))

int
urng_match(struct device *parent, void *match, void *aux)
{
	struct usb_attach_arg *uaa = aux;

	if (uaa->iface == NULL)
		return (UMATCH_NONE);

	if (urng_lookup(uaa->vendor, uaa->product) != NULL)
		return (UMATCH_VENDOR_PRODUCT);

	return (UMATCH_NONE);
}

void
urng_attach(struct device *parent, struct device *self, void *aux)
{
	struct urng_softc *sc = (struct urng_softc *)self;
	struct usb_attach_arg *uaa = aux;
	usb_interface_descriptor_t *id;
	usb_endpoint_descriptor_t *ed;
	int ep_ibulk = -1;
	usbd_status error;
	int i, ep_addr;

	sc->sc_udev = uaa->device;
	sc->sc_chip = urng_lookup(uaa->vendor, uaa->product)->urng_chip;
	sc->sc_product = uaa->product;
#ifdef URNG_MEASURE_RATE
	sc->sc_first_run = 1;
#endif

	DPRINTF(("%s: bufsiz: %d, endpoint: %d ctl iface: %d, msecs: %d, read_timeout: %d\n",
		DEVNAME(sc),
		sc->sc_chip.bufsiz,
		sc->sc_chip.endpoint,
		sc->sc_chip.ctl_iface_idx,
		sc->sc_chip.msecs,
		sc->sc_chip.read_timeout));

	/* Find the bulk endpoints. */
	id = usbd_get_interface_descriptor(uaa->iface);
	for (i = 0; i < id->bNumEndpoints; i++) {
		ed = usbd_interface2endpoint_descriptor(uaa->iface, i);
		if (ed == NULL) {
			printf("%s: failed to get endpoint %d descriptor\n",
			    DEVNAME(sc), i);
			goto fail;
		}
		if (UE_GET_DIR(ed->bEndpointAddress) == UE_DIR_IN &&
		    UE_GET_XFERTYPE(ed->bmAttributes) == UE_BULK) {
		    	ep_addr = UE_GET_ADDR(ed->bEndpointAddress);

			DPRINTF(("%s: bulk endpoint %d\n",
			    DEVNAME(sc), ep_addr));

			if (ep_addr == sc->sc_chip.endpoint) {
				ep_ibulk = ed->bEndpointAddress;
				break;
			}
		}
	}

	if (ep_ibulk == -1) {
		printf("%s: missing bulk input endpoint\n", DEVNAME(sc));
		goto fail;
	}

	/* Open the pipes. */
	error = usbd_open_pipe(uaa->iface, ep_ibulk, USBD_EXCLUSIVE_USE,
		    &sc->sc_inpipe);
	if (error) {
		printf("%s: failed to open bulk-in pipe: %s\n",
				DEVNAME(sc), usbd_errstr(error));
		goto fail;
	}

	/* Allocate the transfer buffers. */
	sc->sc_xfer = usbd_alloc_xfer(sc->sc_udev);
	if (sc->sc_xfer == NULL) {
		printf("%s: could not alloc xfer\n", DEVNAME(sc));
		goto fail;
	}

	sc->sc_buf = usbd_alloc_buffer(sc->sc_xfer, sc->sc_chip.bufsiz);
	if (sc->sc_buf == NULL) {
		printf("%s: could not alloc %d-byte buffer\n", DEVNAME(sc),
				sc->sc_chip.bufsiz);
		goto fail;
	}

	/* And off we go! */
	usb_init_task(&sc->sc_task, urng_task, sc, USB_TASK_TYPE_GENERIC);
	timeout_set(&sc->sc_timeout, urng_timeout, sc);
	usb_add_task(sc->sc_udev, &sc->sc_task);

	return;

fail:
	usbd_deactivate(sc->sc_udev);
}

int
urng_detach(struct device *self, int flags)
{
	struct urng_softc *sc = (struct urng_softc *)self;

	usb_rem_task(sc->sc_udev, &sc->sc_task);

	if (timeout_initialized(&sc->sc_timeout))
		timeout_del(&sc->sc_timeout);

	if (sc->sc_xfer != NULL) {
		usbd_free_xfer(sc->sc_xfer);
		sc->sc_xfer = NULL;
	}

	if (sc->sc_inpipe != NULL) {
		usbd_close_pipe(sc->sc_inpipe);
		sc->sc_inpipe = NULL;
	}

	return (0);
}


void
urng_task(void *arg)
{
	struct urng_softc *sc = (struct urng_softc *)arg;
	usbd_status error;
	u_int32_t len, i;
#ifdef URNG_MEASURE_RATE
	time_t elapsed;
	int rate;
#endif
	usbd_setup_xfer(sc->sc_xfer, sc->sc_inpipe, NULL, sc->sc_buf,
	    sc->sc_chip.bufsiz, USBD_SHORT_XFER_OK | USBD_SYNCHRONOUS,
	    sc->sc_chip.read_timeout, NULL);

	error = usbd_transfer(sc->sc_xfer);
	if (error) {
		printf("%s: xfer failed: %s\n", DEVNAME(sc),
		    usbd_errstr(error));
		goto bail;
	}

	usbd_get_xfer_status(sc->sc_xfer, NULL, NULL, &len, NULL);
	if (len < sizeof(int)) {
		printf("%s: xfer too short (%u bytes) - dropping\n",
		    DEVNAME(sc), len);
		goto bail;
	}

#ifdef URNG_MEASURE_RATE
	if (sc->sc_first_run) {
		sc->sc_counted_bytes = 0;
		getmicrotime(&(sc->sc_start));
	}
	sc->sc_counted_bytes += len;
	getmicrotime(&(sc->sc_cur));
	elapsed = sc->sc_cur.tv_sec - sc->sc_start.tv_sec;
	if (elapsed >= URNG_RATE_SECONDS) {
		rate = (8 * sc->sc_counted_bytes) / (elapsed * 1024);
		printf("%s: transfer rate = %d kb/s\n", DEVNAME(sc), rate);

		/* set up for next measurement */
		sc->sc_counted_bytes = 0;
		getmicrotime(&(sc->sc_start));
	}
#endif

	len /= sizeof(int);
	for (i = 0; i < len; i++) {
		enqueue_randomness(sc->sc_buf[i]);
	}
bail:
#ifdef URNG_MEASURE_RATE
	if (sc->sc_first_run) {
		sc->sc_first_run = 0;
	}
#endif

	timeout_add_msec(&sc->sc_timeout, sc->sc_chip.msecs);
}

void
urng_timeout(void *arg)
{
	struct urng_softc *sc = arg;

	usb_add_task(sc->sc_udev, &sc->sc_task);
}
