/*	$OpenBSD: uonerng.c,v 1.7 2024/05/23 03:21:09 jsg Exp $ */
/*
 * Copyright (C) 2015 Devin Reade <gdr@gno.org>
 * Copyright (C) 2015 Sean Levy <attila@stalphonsos.com>
 * Copyright (c) 2007 Marc Balmer <mbalmer@openbsd.org>
 * Copyright (c) 2006 Alexander Yurchenko <grange@openbsd.org>
 * Copyright (c) 1998 The NetBSD Foundation, Inc.
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
 * Moonbase Otago OneRNG TRNG.  Note that the encoded vendor for this
 * device is OpenMoko as OpenMoko has made its device ranges available
 * for other open source / open hardware vendors.
 *
 * Product information can be found here:
 *     http://onerng.info/onerng
 *
 * Based on the ualea(4), uow(4), and umodem(4) source code.
 */

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/device.h>
#include <sys/timeout.h>
#include <machine/bus.h>

#include <dev/usb/usb.h>
#include <dev/usb/usbdi.h>
#include <dev/usb/usbdevs.h>
#include <dev/usb/usbcdc.h>

/*
 * The OneRNG is documented to provide ~350kbits/s of entropy at
 * ~7.8 bits/byte, and when used at a lower rate providing close
 * to 8 bits/byte.
 *
 * Although this driver is able to consume the data at the full rate,
 * we tune this down to 10kbit/s as the OpenBSD RNG is better off
 * with small amounts of input at a time so as to not saturate the
 * input queue and mute other sources of entropy.
 *
 * Furthermore, unlike other implementations, for us there is no benefit
 * to discarding the initial bytes retrieved from the OneRNG, regardless
 * of the quality of the data. (Empirical tests suggest that the initial
 * quality is fine, anyway.)
 */
#define ONERNG_BUFSIZ		128
#define ONERNG_MSECS		100

#define ONERNG_TIMEOUT  	1000	/* ms */

/*
 * Define ONERNG_MEASURE_RATE to periodically log rate at which we provide
 * random data to the kernel.
 */
#ifdef ONERNG_MEASURE_RATE
#define ONERNG_RATE_SECONDS 30
#endif

/* OneRNG operational modes */
#define ONERNG_OP_ENABLE	"cmdO\n" /* start emitting data */
#define ONERNG_OP_DISABLE	"cmdo\n" /* stop emitting data */
#define ONERNG_OP_FLUSH_ENTROPY	"cmdw\n"

/* permits extracting the firmware in order to check the crypto signature */
#define ONERNG_OP_EXTRACT_FIRMWARE "cmdX\n"

/*
 * Noise sources include an avalanche circuit and an RF circuit.
 * There is also a whitener to provide a uniform distribution.
 * Different combinations are possible.
 */
#define ONERNG_AVALANCHE_WHITENER	"cmd0\n" /* device default */
#define ONERNG_AVALANCHE		"cmd1\n"
#define ONERNG_AVALANCHE_RF_WHITENER	"cmd2\n"
#define ONERNG_AVALANCHE_RF		"cmd3\n"
#define ONERNG_SILENT			"cmd4\n" /* none; necessary for cmdX */
#define ONERNG_SILENT2			"cmd5\n"
#define ONERNG_RF_WHITENER		"cmd6\n"
#define ONERNG_RF			"cmd7\n"


#define ONERNG_IFACE_CTRL_INDEX	0
#define ONERNG_IFACE_DATA_INDEX	1

#define DEVNAME(_sc) ((_sc)->sc_dev.dv_xname)

struct uonerng_softc {
	struct	  device sc_dev;
	struct	  usbd_device *sc_udev;

	int	  sc_ctl_iface_no;			/* control */
	struct	  usbd_interface *sc_data_iface;	/* data */

	struct	  usbd_pipe *sc_inpipe;
	struct	  usbd_pipe *sc_outpipe;

	struct	  timeout sc_timeout;
	struct	  usb_task sc_task;
	struct	  usbd_xfer *sc_xfer;
	int      *sc_buf;
#ifdef ONERNG_MEASURE_RATE
	struct	  timeval sc_start;
	struct	  timeval sc_cur;
	int	  sc_counted_bytes;
#endif
	u_char	  sc_dtr;			/* current DTR state */
	u_char	  sc_rts;			/* current RTS state */
	u_char	  sc_first_run;
};

int uonerng_match(struct device *, void *, void *);
void uonerng_attach(struct device *, struct device *, void *);
int uonerng_detach(struct device *, int);
void uonerng_task(void *);
void uonerng_timeout(void *);
int uonerng_enable(struct uonerng_softc *sc);
void uonerng_cleanup(struct uonerng_softc *sc);
usbd_status uonerng_set_line_state(struct uonerng_softc *sc);
usbd_status uonerng_rts(struct uonerng_softc *sc, int onoff);

struct cfdriver uonerng_cd = {
	NULL, "uonerng", DV_DULL
};

const struct cfattach uonerng_ca = {
	sizeof(struct uonerng_softc), uonerng_match, uonerng_attach, uonerng_detach
};

int
uonerng_match(struct device *parent, void *match, void *aux)
{
	struct usb_attach_arg *uaa = aux;

	if (uaa->iface == NULL)
		return UMATCH_NONE;

	if (uaa->vendor != USB_VENDOR_OPENMOKO2 ||
	    uaa->product != USB_PRODUCT_OPENMOKO2_ONERNG)
		return UMATCH_NONE;

	return UMATCH_VENDOR_PRODUCT;
}

void
uonerng_attach(struct device *parent, struct device *self, void *aux)
{
	struct uonerng_softc *sc = (struct uonerng_softc *)self;
	struct usb_attach_arg *uaa = aux;
	struct usbd_interface *iface = uaa->iface;
	usb_interface_descriptor_t *id;
	usb_endpoint_descriptor_t *ed;
	int ep_ibulk = -1, ep_obulk = -1;
	usbd_status err;
	int i;

	sc->sc_udev = uaa->device;
	sc->sc_dtr = -1;
	sc->sc_rts = -1;
	sc->sc_first_run = 1;

	usb_init_task(&sc->sc_task, uonerng_task, sc, USB_TASK_TYPE_GENERIC);

	/* locate the control interface number and the data interface */
	err = usbd_device2interface_handle(sc->sc_udev,
	    ONERNG_IFACE_CTRL_INDEX, &iface);
	if (err || iface == NULL) {
		printf("%s: failed to locate control interface, err=%s\n",
		    DEVNAME(sc), usbd_errstr(err));
		goto fail;
	}
	id = usbd_get_interface_descriptor(iface);
	if (id != NULL &&
	    id->bInterfaceClass == UICLASS_CDC &&
	    id->bInterfaceSubClass == UISUBCLASS_ABSTRACT_CONTROL_MODEL &&
	    id->bInterfaceProtocol == UIPROTO_CDC_AT) {
		sc->sc_ctl_iface_no = id->bInterfaceNumber;
	} else {
		printf("%s: control interface number not found\n",
		    DEVNAME(sc));
		goto fail;
	}

	err = usbd_device2interface_handle(sc->sc_udev,
	    ONERNG_IFACE_DATA_INDEX, &sc->sc_data_iface);
	if (err || sc->sc_data_iface == NULL) {
		printf("%s: failed to locate data interface, err=%s\n",
		    DEVNAME(sc), usbd_errstr(err));
		goto fail;
	}

	/* Find the bulk endpoints */
	id = usbd_get_interface_descriptor(sc->sc_data_iface);
	if (id == NULL ||
	    id->bInterfaceClass != UICLASS_CDC_DATA ||
	    id->bInterfaceSubClass != UISUBCLASS_DATA) {
		printf("%s: no data interface descriptor\n", DEVNAME(sc));
		goto fail;
	}
	for (i = 0; i < id->bNumEndpoints; i++) {
		ed = usbd_interface2endpoint_descriptor(sc->sc_data_iface, i);
		if (ed == NULL) {
			printf("%s: no endpoint descriptor for %d\n",
			    DEVNAME(sc), i);
			goto fail;
		}
		if (UE_GET_DIR(ed->bEndpointAddress) == UE_DIR_IN &&
		    UE_GET_XFERTYPE(ed->bmAttributes) == UE_BULK) {
                        ep_ibulk = ed->bEndpointAddress;
                } else if (UE_GET_DIR(ed->bEndpointAddress) == UE_DIR_OUT &&
			   UE_GET_XFERTYPE(ed->bmAttributes) == UE_BULK) {
                        ep_obulk = ed->bEndpointAddress;
                }
        }

	if (ep_ibulk == -1) {
		printf("%s: Could not find data bulk in\n", DEVNAME(sc));
		goto fail;
	}
	if (ep_obulk == -1) {
		printf("%s: Could not find data bulk out\n", DEVNAME(sc));
		goto fail;
	}

	/* Open pipes */
	err = usbd_open_pipe(sc->sc_data_iface, ep_ibulk,
	    USBD_EXCLUSIVE_USE, &sc->sc_inpipe);
	if (err) {
		printf("%s: failed to open bulk-in pipe: %s\n",
		    DEVNAME(sc), usbd_errstr(err));
		goto fail;
	}
	err = usbd_open_pipe(sc->sc_data_iface, ep_obulk,
	    USBD_EXCLUSIVE_USE, &sc->sc_outpipe);
	if (err) {
		printf("%s: failed to open bulk-out pipe: %s\n",
		    DEVNAME(sc), usbd_errstr(err));
		goto fail;
	}

	/* Allocate xfer/buffer for bulk transfers */
	sc->sc_xfer = usbd_alloc_xfer(sc->sc_udev);
	if (sc->sc_xfer == NULL) {
		printf("%s: could not alloc xfer\n", DEVNAME(sc));
		goto fail;
	}
	sc->sc_buf = usbd_alloc_buffer(sc->sc_xfer, ONERNG_BUFSIZ);
	if (sc->sc_buf == NULL) {
		printf("%s: could not alloc %d-byte buffer\n", DEVNAME(sc),
		    ONERNG_BUFSIZ);
		goto fail;
	}

	if (uonerng_enable(sc) != 0) {
		goto fail;
	}

	timeout_set(&sc->sc_timeout, uonerng_timeout, sc);

	/* get the initial random data as early as possible */
	uonerng_task(sc);

	usb_add_task(sc->sc_udev, &sc->sc_task);
	return;

 fail:
	usbd_deactivate(sc->sc_udev);
	uonerng_cleanup(sc);
}

int
uonerng_enable(struct uonerng_softc *sc)
{
	int err;

	if ((err = uonerng_rts(sc, 0))) {
		printf("%s: failed to clear RTS: %s\n", DEVNAME(sc),
		    usbd_errstr(err));
		return (1);
	}

	usbd_setup_xfer(sc->sc_xfer, sc->sc_outpipe, sc,
	    ONERNG_AVALANCHE_WHITENER, sizeof(ONERNG_AVALANCHE_WHITENER),
	    USBD_SYNCHRONOUS, ONERNG_TIMEOUT, NULL);
	if ((err = usbd_transfer(sc->sc_xfer))) {
		printf("%s: failed to set operating mode: %s\n",
		    DEVNAME(sc), usbd_errstr(err));
		return (1);
	}

	usbd_setup_xfer(sc->sc_xfer, sc->sc_outpipe, sc,
	    ONERNG_OP_ENABLE, sizeof(ONERNG_OP_ENABLE),
	    USBD_SYNCHRONOUS, ONERNG_TIMEOUT, NULL);
	if ((err = usbd_transfer(sc->sc_xfer))) {
		printf("%s: failed to enable device: %s\n",
		    DEVNAME(sc), usbd_errstr(err));
		return (1);
	}

	return (0);
}

int
uonerng_detach(struct device *self, int flags)
{
	struct uonerng_softc *sc = (struct uonerng_softc *)self;

	usb_rem_task(sc->sc_udev, &sc->sc_task);
	if (timeout_initialized(&sc->sc_timeout)) {
		timeout_del(&sc->sc_timeout);
	}
	uonerng_cleanup(sc);
	return (0);
}

void
uonerng_cleanup(struct uonerng_softc *sc)
{
	if (sc->sc_inpipe != NULL) {
		usbd_close_pipe(sc->sc_inpipe);
		sc->sc_inpipe = NULL;
	}
	if (sc->sc_outpipe != NULL) {
		usbd_close_pipe(sc->sc_outpipe);
		sc->sc_outpipe = NULL;
	}

	/* usbd_free_xfer will also free the buffer if necessary */
	if (sc->sc_xfer != NULL) {
		usbd_free_xfer(sc->sc_xfer);
		sc->sc_xfer = NULL;
	}
}

usbd_status
uonerng_rts(struct uonerng_softc *sc, int onoff)
{
	if (sc->sc_rts == onoff)
		return USBD_NORMAL_COMPLETION;
	sc->sc_rts = onoff;

	return uonerng_set_line_state(sc);
}

usbd_status
uonerng_set_line_state(struct uonerng_softc *sc)
{
	usb_device_request_t req;
	int ls;

	ls = (sc->sc_dtr ? UCDC_LINE_DTR : 0) |
	     (sc->sc_rts ? UCDC_LINE_RTS : 0);
	req.bmRequestType = UT_WRITE_CLASS_INTERFACE;
	req.bRequest = UCDC_SET_CONTROL_LINE_STATE;
	USETW(req.wValue, ls);
	USETW(req.wIndex, sc->sc_ctl_iface_no);
	USETW(req.wLength, 0);

	return usbd_do_request(sc->sc_udev, &req, 0);
}

void
uonerng_task(void *arg)
{
	struct uonerng_softc *sc = (struct uonerng_softc *) arg;
	usbd_status error;
	u_int32_t len, int_count, i;
#ifdef ONERNG_MEASURE_RATE
	time_t elapsed;
	int rate;
#endif

	usbd_setup_xfer(sc->sc_xfer, sc->sc_inpipe, NULL, sc->sc_buf,
	    ONERNG_BUFSIZ,
	    USBD_SHORT_XFER_OK | USBD_SYNCHRONOUS | USBD_NO_COPY,
	    ONERNG_TIMEOUT, NULL);
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

#ifdef ONERNG_MEASURE_RATE
	if (sc->sc_first_run) {
		sc->sc_counted_bytes = 0;
		getmicrotime(&(sc->sc_start));
	}
	sc->sc_counted_bytes += len;
	getmicrotime(&(sc->sc_cur));
	elapsed = sc->sc_cur.tv_sec - sc->sc_start.tv_sec;
	if (elapsed >= ONERNG_RATE_SECONDS) {
		rate = (8 * sc->sc_counted_bytes) / (elapsed * 1024);
		printf("%s: transfer rate = %d kb/s\n", DEVNAME(sc), rate);

		/* set up for next measurement */
		sc->sc_counted_bytes = 0;
		getmicrotime(&(sc->sc_start));
	}
#endif

	int_count = len / sizeof(int);
	for (i = 0; i < int_count; i++) {
		enqueue_randomness(sc->sc_buf[i]);
	}
bail:

	if (sc->sc_first_run) {
		sc->sc_first_run = 0;
	} else {
		timeout_add_msec(&sc->sc_timeout, ONERNG_MSECS);
	}
}

void
uonerng_timeout(void *arg)
{
	struct uonerng_softc *sc = arg;

	usb_add_task(sc->sc_udev, &sc->sc_task);
}
