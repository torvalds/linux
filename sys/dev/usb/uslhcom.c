/*	$OpenBSD: uslhcom.c,v 1.10 2024/05/23 03:21:09 jsg Exp $	*/

/*
 * Copyright (c) 2015 SASANO Takayoshi <uaa@openbsd.org>
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
 * Device driver for Silicon Labs CP2110 USB HID-UART bridge.
 */

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/malloc.h>
#include <sys/tty.h>
#include <sys/device.h>

#include <dev/usb/usb.h>
#include <dev/usb/usbdi.h>
#include <dev/usb/usbdevs.h>

#include <dev/usb/usbhid.h>
#include <dev/usb/uhidev.h>

#include <dev/usb/ucomvar.h>
#include <dev/usb/uslhcomreg.h>

#ifdef USLHCOM_DEBUG
#define	DPRINTF(x)	if (uslhcomdebug) printf x
#else
#define	DPRINTF(x)
#endif

struct uslhcom_softc {
	struct uhidev		 sc_hdev;
	struct usbd_device	*sc_udev;

	u_char			*sc_ibuf;
	u_int			 sc_icnt;

	u_char			 sc_lsr;
	u_char			 sc_msr;

	struct device		*sc_subdev;
};

void		uslhcom_get_status(void *, int, u_char *, u_char *);
void		uslhcom_set(void *, int, int, int);
int		uslhcom_param(void *, int, struct termios *);
int		uslhcom_open(void *, int);
void		uslhcom_close(void *, int);
void		uslhcom_write(void *, int, u_char *, u_char *, u_int32_t *);
void		uslhcom_read(void *, int, u_char **, u_int32_t *);
void		uslhcom_intr(struct uhidev *, void *, u_int);

int		uslhcom_match(struct device *, void *, void *);
void		uslhcom_attach(struct device *, struct device *, void *);
int		uslhcom_detach(struct device *, int);

int		uslhcom_uart_endis(struct uslhcom_softc *, int);
int		uslhcom_clear_fifo(struct uslhcom_softc *, int);
int		uslhcom_get_version(struct uslhcom_softc *, struct uslhcom_version_info *);
int		uslhcom_get_uart_status(struct uslhcom_softc *, struct uslhcom_uart_status *);
int		uslhcom_set_break(struct uslhcom_softc *, int);
int		uslhcom_set_config(struct uslhcom_softc *, struct uslhcom_uart_config *);
void		uslhcom_set_baud_rate(struct uslhcom_uart_config *, u_int32_t);
int		uslhcom_create_config(struct uslhcom_uart_config *, struct termios *);
int		uslhcom_setup(struct uslhcom_softc *, struct uslhcom_uart_config *);

const struct ucom_methods uslhcom_methods = {
	uslhcom_get_status,
	uslhcom_set,
	uslhcom_param,
	NULL,
	uslhcom_open,
	uslhcom_close,
	uslhcom_read,
	uslhcom_write,
};

static const struct usb_devno uslhcom_devs[] = {
	{ USB_VENDOR_SILABS, USB_PRODUCT_SILABS_CP2110 },
};

struct cfdriver uslhcom_cd = {
	NULL, "uslhcom", DV_DULL
};

const struct cfattach uslhcom_ca = {
	sizeof(struct uslhcom_softc),
	uslhcom_match, uslhcom_attach, uslhcom_detach
};

/* ----------------------------------------------------------------------
 * driver entry points
 */

int
uslhcom_match(struct device *parent, void *match, void *aux)
{
	struct uhidev_attach_arg *uha = aux;

	/* use all report IDs */
	if (!UHIDEV_CLAIM_MULTIPLE_REPORTID(uha))
		return UMATCH_NONE;

	return (usb_lookup(uslhcom_devs,
			   uha->uaa->vendor, uha->uaa->product) != NULL ?
		UMATCH_VENDOR_PRODUCT : UMATCH_NONE);
}

void
uslhcom_attach(struct device *parent, struct device *self, void *aux)
{
	struct uslhcom_softc *sc = (struct uslhcom_softc *)self;
	struct uhidev_attach_arg *uha = aux;
	struct usbd_device *dev = uha->parent->sc_udev;
	struct ucom_attach_args uca;
	struct uslhcom_version_info version;
	int err, repid, size, rsize;
	void *desc;

	sc->sc_hdev.sc_intr = uslhcom_intr;
	sc->sc_hdev.sc_parent = uha->parent;
	sc->sc_hdev.sc_report_id = uha->reportid;

	uhidev_get_report_desc(uha->parent, &desc, &size);
	for (repid = 0; repid < uha->parent->sc_nrepid; repid++) {
		rsize = hid_report_size(desc, size, hid_input, repid);
		if (sc->sc_hdev.sc_isize < rsize) sc->sc_hdev.sc_isize = rsize;
		rsize = hid_report_size(desc, size, hid_output, repid);
		if (sc->sc_hdev.sc_osize < rsize) sc->sc_hdev.sc_osize = rsize;
		rsize = hid_report_size(desc, size, hid_feature, repid);
		if (sc->sc_hdev.sc_fsize < rsize) sc->sc_hdev.sc_fsize = rsize;
	}

	printf("\n");

	sc->sc_udev = dev;

	err = uhidev_open(&sc->sc_hdev);
	if (err) {
		DPRINTF(("uslhcom_attach: uhidev_open %d\n", err));
		return;
	}

	DPRINTF(("uslhcom_attach: sc %p opipe %p ipipe %p report_id %d\n",
		 sc, sc->sc_hdev.sc_parent->sc_opipe,
		 sc->sc_hdev.sc_parent->sc_ipipe, uha->reportid));
	DPRINTF(("uslhcom_attach: isize %d osize %d fsize %d\n",
		 sc->sc_hdev.sc_isize, sc->sc_hdev.sc_osize,
		 sc->sc_hdev.sc_fsize));

	uslhcom_uart_endis(sc, UART_DISABLE);
	uslhcom_get_version(sc, &version);
	printf("%s: pid %#x rev %#x\n", sc->sc_hdev.sc_dev.dv_xname,
	       version.product_id, version.product_revision);

	/* setup ucom layer */
	bzero(&uca, sizeof uca);
	uca.portno = UCOM_UNK_PORTNO;
	uca.bulkin = uca.bulkout = -1;
	uca.ibufsize = uca.ibufsizepad = 0;
	uca.obufsize = sc->sc_hdev.sc_osize;
	uca.opkthdrlen = USLHCOM_TX_HEADER_SIZE;
	uca.uhidev = sc->sc_hdev.sc_parent;
	uca.device = uha->uaa->device;
	uca.iface = uha->uaa->iface;
	uca.methods = &uslhcom_methods;
	uca.arg = sc;
	uca.info = NULL;

	sc->sc_subdev = config_found_sm(self, &uca, ucomprint, ucomsubmatch);
}

int
uslhcom_detach(struct device *self, int flags)
{
	struct uslhcom_softc *sc = (struct uslhcom_softc *)self;

	DPRINTF(("uslhcom_detach: sc=%p flags=%d\n", sc, flags));
	if (sc->sc_subdev != NULL) {
		config_detach(sc->sc_subdev, flags);
		sc->sc_subdev = NULL;
	}

	if (sc->sc_hdev.sc_state & UHIDEV_OPEN)
		uhidev_close(&sc->sc_hdev);

	return 0;
}

/* ----------------------------------------------------------------------
 * low level I/O
 */

int
uslhcom_uart_endis(struct uslhcom_softc *sc, int enable)
{
	int len;
	u_char val;

	len = sizeof(val);
	val = enable;

	return uhidev_set_report(sc->sc_hdev.sc_parent, UHID_FEATURE_REPORT,
				 GET_SET_UART_ENABLE, &val, len) != len;
}

int
uslhcom_clear_fifo(struct uslhcom_softc *sc, int fifo)
{
	int len;
	u_char val;

	len = sizeof(val);
	val = fifo;

	return uhidev_set_report(sc->sc_hdev.sc_parent, UHID_FEATURE_REPORT,
				 SET_CLEAR_FIFOS, &val, len) != len;
}

int
uslhcom_get_version(struct uslhcom_softc *sc, struct uslhcom_version_info *version)
{
	int len;

	len = sizeof(*version);

	return uhidev_get_report(sc->sc_hdev.sc_parent, UHID_FEATURE_REPORT,
				 GET_VERSION, version, len) < len;
}

int
uslhcom_get_uart_status(struct uslhcom_softc *sc, struct uslhcom_uart_status *status)
{
	int len;

	len = sizeof(*status);

	return uhidev_get_report(sc->sc_hdev.sc_parent, UHID_FEATURE_REPORT,
				 GET_UART_STATUS, status, len) < len;
}

int
uslhcom_set_break(struct uslhcom_softc *sc, int onoff)
{
	int len, reportid;
	u_char val;

	len = sizeof(val);

	if (onoff) {
		val = 0; /* send break until SET_STOP_LINE_BREAK */
		reportid = SET_TRANSMIT_LINE_BREAK;
	} else {
		val = 0; /* any value can be accepted */
		reportid = SET_STOP_LINE_BREAK;
	}

	return uhidev_set_report(sc->sc_hdev.sc_parent, UHID_FEATURE_REPORT,
				 reportid, &val, len) != len;
}

int
uslhcom_set_config(struct uslhcom_softc *sc, struct uslhcom_uart_config *config)
{
	int len;

	len = sizeof(*config);

	return uhidev_set_report(sc->sc_hdev.sc_parent, UHID_FEATURE_REPORT,
				 GET_SET_UART_CONFIG, config, len) != len;
}

void
uslhcom_set_baud_rate(struct uslhcom_uart_config *config, u_int32_t baud_rate)
{
	config->baud_rate[0] = baud_rate >> 24;
	config->baud_rate[1] = baud_rate >> 16;
	config->baud_rate[2] = baud_rate >> 8;
	config->baud_rate[3] = baud_rate >> 0;
}

int
uslhcom_create_config(struct uslhcom_uart_config *config, struct termios *t)
{
	if (t->c_ospeed < UART_CONFIG_BAUD_RATE_MIN ||
	    t->c_ospeed > UART_CONFIG_BAUD_RATE_MAX)
		return EINVAL;

	uslhcom_set_baud_rate(config, t->c_ospeed);

	if (ISSET(t->c_cflag, PARENB)) {
		if (ISSET(t->c_cflag, PARODD))
			config->parity = UART_CONFIG_PARITY_ODD;
		else
			config->parity = UART_CONFIG_PARITY_EVEN;
	} else
		config->parity = UART_CONFIG_PARITY_NONE;

	if (ISSET(t->c_cflag, CRTSCTS))
		config->data_control = UART_CONFIG_DATA_CONTROL_HARD;
	else
		config->data_control = UART_CONFIG_DATA_CONTROL_NONE;

	switch (ISSET(t->c_cflag, CSIZE)) {
	case CS5:
		config->data_bits = UART_CONFIG_DATA_BITS_5;
		break;
	case CS6:
		config->data_bits = UART_CONFIG_DATA_BITS_6;
		break;
	case CS7:
		config->data_bits = UART_CONFIG_DATA_BITS_7;
		break;
	case CS8:
		config->data_bits = UART_CONFIG_DATA_BITS_8;
		break;
	default:
		return EINVAL;
	}

	if (ISSET(t->c_cflag, CSTOPB))
		config->stop_bits = UART_CONFIG_STOP_BITS_2;
	else
		config->stop_bits = UART_CONFIG_STOP_BITS_1;

	return 0;
}

int
uslhcom_setup(struct uslhcom_softc *sc, struct uslhcom_uart_config *config)
{
	struct uslhcom_uart_status status;

	if (uslhcom_uart_endis(sc, UART_DISABLE))
		return EIO;

	if (uslhcom_set_config(sc, config))
		return EIO;

	if (uslhcom_clear_fifo(sc, CLEAR_TX_FIFO | CLEAR_RX_FIFO))
		return EIO;

	if (uslhcom_get_uart_status(sc, &status))
		return EIO;

	if (uslhcom_uart_endis(sc, UART_ENABLE))
		return EIO;

	return 0;
}

/* ----------------------------------------------------------------------
 * methods for ucom
 */

void
uslhcom_get_status(void *arg, int portno, u_char *rlsr, u_char *rmsr)
{
	struct uslhcom_softc *sc = arg;

	if (usbd_is_dying(sc->sc_udev))
		return;

	*rlsr = sc->sc_lsr;
	*rmsr = sc->sc_msr;
}

void
uslhcom_set(void *arg, int portno, int reg, int onoff)
{
	struct uslhcom_softc *sc = arg;

	if (usbd_is_dying(sc->sc_udev))
		return;

	switch (reg) {
	case UCOM_SET_DTR:
	case UCOM_SET_RTS:
		/* no support, do nothing */
		break;
	case UCOM_SET_BREAK:
		uslhcom_set_break(sc, onoff);
		break;
	}
}

int
uslhcom_param(void *arg, int portno, struct termios *t)
{
	struct uslhcom_softc *sc = arg;
	struct uslhcom_uart_config config;
	int ret;

	if (usbd_is_dying(sc->sc_udev))
		return 0;

	ret = uslhcom_create_config(&config, t);
	if (ret)
		return ret;

	ret = uslhcom_setup(sc, &config);
	if (ret)
		return ret;

	return 0;
}

int
uslhcom_open(void *arg, int portno)
{
	struct uslhcom_softc *sc = arg;
	struct uslhcom_uart_config config;
	int ret;

	if (usbd_is_dying(sc->sc_udev))
		return EIO;

	sc->sc_ibuf = malloc(sc->sc_hdev.sc_isize, M_USBDEV, M_WAITOK);

	uslhcom_set_baud_rate(&config, 9600);
	config.parity = UART_CONFIG_PARITY_NONE;
	config.data_control = UART_CONFIG_DATA_CONTROL_NONE;
	config.data_bits = UART_CONFIG_DATA_BITS_8;
	config.stop_bits = UART_CONFIG_STOP_BITS_1;

	ret = uslhcom_set_config(sc, &config);
	if (ret)
		return ret;

	return 0;
}

void
uslhcom_close(void *arg, int portno)
{
	struct uslhcom_softc *sc = arg;
	int s;

	if (usbd_is_dying(sc->sc_udev))
		return;

	uslhcom_uart_endis(sc, UART_DISABLE);

	s = splusb();
	if (sc->sc_ibuf != NULL) {
		free(sc->sc_ibuf, M_USBDEV, sc->sc_hdev.sc_isize);
		sc->sc_ibuf = NULL;
	}
	splx(s);
}

void
uslhcom_read(void *arg, int portno, u_char **ptr, u_int32_t *cnt)
{
	struct uslhcom_softc *sc = arg;

	*ptr = sc->sc_ibuf;
	*cnt = sc->sc_icnt;
}

void
uslhcom_write(void *arg, int portno, u_char *to, u_char *data, u_int32_t *cnt)
{
	bcopy(data, &to[USLHCOM_TX_HEADER_SIZE], *cnt);
	to[0] = *cnt;	/* add Report ID (= transmit length) */
	*cnt += USLHCOM_TX_HEADER_SIZE;
}

void
uslhcom_intr(struct uhidev *addr, void *ibuf, u_int len)
{
	extern void ucomreadcb(struct usbd_xfer *, void *, usbd_status);
	struct uslhcom_softc *sc = (struct uslhcom_softc *)addr;
	int s;

	if (sc->sc_ibuf == NULL)
		return;

	s = spltty();
	sc->sc_icnt = len;	/* Report ID is already stripped */
	bcopy(ibuf, sc->sc_ibuf, len);
	ucomreadcb(addr->sc_parent->sc_ixfer, sc->sc_subdev,
		   USBD_NORMAL_COMPLETION);
	splx(s);
}
