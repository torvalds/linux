/*	$OpenBSD: ucycom.c,v 1.42 2024/05/23 03:21:09 jsg Exp $	*/
/*	$NetBSD: ucycom.c,v 1.3 2005/08/05 07:27:47 skrll Exp $	*/

/*
 * Copyright (c) 2005 The NetBSD Foundation, Inc.
 * All rights reserved.
 *
 * This code is derived from software contributed to The NetBSD Foundation
 * by Nick Hudson
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
 * This code is based on the ucom driver.
 */

/*
 * Device driver for Cypress CY7C637xx and CY7C640/1xx series USB to
 * RS232 bridges.
 */

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/malloc.h>
#include <sys/device.h>
#include <sys/tty.h>

#include <dev/usb/usb.h>
#include <dev/usb/usbhid.h>

#include <dev/usb/usbdi.h>
#include <dev/usb/usbdevs.h>
#include <dev/usb/uhidev.h>

#include <dev/usb/ucomvar.h>

#ifdef UCYCOM_DEBUG
#define DPRINTF(x)	if (ucycomdebug) printf x
#define DPRINTFN(n, x)	if (ucycomdebug > (n)) printf x
int	ucycomdebug = 200;
#else
#define DPRINTF(x)
#define DPRINTFN(n,x)
#endif

/* Configuration Byte */
#define UCYCOM_RESET		0x80
#define UCYCOM_PARITY_TYPE_MASK	0x20
#define  UCYCOM_PARITY_ODD	 0x20
#define  UCYCOM_PARITY_EVEN	 0x00
#define UCYCOM_PARITY_MASK	0x10
#define  UCYCOM_PARITY_ON	 0x10
#define  UCYCOM_PARITY_OFF	 0x00
#define UCYCOM_STOP_MASK	0x08
#define  UCYCOM_STOP_BITS_2	 0x08
#define  UCYCOM_STOP_BITS_1	 0x00
#define UCYCOM_DATA_MASK	0x03
#define  UCYCOM_DATA_BITS_8	 0x03
#define  UCYCOM_DATA_BITS_7	 0x02
#define  UCYCOM_DATA_BITS_6	 0x01
#define  UCYCOM_DATA_BITS_5	 0x00

/* Modem (Input) status byte */
#define UCYCOM_RI	0x80
#define UCYCOM_DCD	0x40
#define UCYCOM_DSR	0x20
#define UCYCOM_CTS	0x10
#define UCYCOM_ERROR	0x08
#define UCYCOM_LMASK	0x07

/* Modem (Output) control byte */
#define UCYCOM_DTR	0x20
#define UCYCOM_RTS	0x10
#define UCYCOM_ORESET	0x08

struct ucycom_softc {
	struct uhidev		 sc_hdev;
	struct usbd_device	*sc_udev;

	/* uhidev parameters */
	size_t			 sc_flen;	/* feature report length */
	size_t			 sc_ilen;	/* input report length */
	size_t			 sc_olen;	/* output report length */

	uint8_t			*sc_obuf;

	uint8_t			*sc_ibuf;
	uint32_t		 sc_icnt;

	/* settings */
	uint32_t		 sc_baud;
	uint8_t			 sc_cfg;	/* Data format */
	uint8_t			 sc_mcr;	/* Modem control */
	uint8_t			 sc_msr;	/* Modem status */
	uint8_t			 sc_newmsr;	/* from HID intr */
	int			 sc_swflags;

	struct device		*sc_subdev;
};

/* Callback routines */
void	ucycom_set(void *, int, int, int);
int	ucycom_param(void *, int, struct termios *);
void	ucycom_get_status(void *, int, u_char *, u_char *);
int	ucycom_open(void *, int);
void	ucycom_close(void *, int);
void	ucycom_write(void *, int, u_char *, u_char *, u_int32_t *);
void	ucycom_read(void *, int, u_char **, u_int32_t *);

const struct ucom_methods ucycom_methods = {
	NULL, /* ucycom_get_status, */
	ucycom_set,
	ucycom_param,
	NULL,
	ucycom_open,
	ucycom_close,
	ucycom_read,
	ucycom_write,
};

void ucycom_intr(struct uhidev *, void *, u_int);

const struct usb_devno ucycom_devs[] = {
	{ USB_VENDOR_CYPRESS, USB_PRODUCT_CYPRESS_USBRS232 },
	{ USB_VENDOR_DELORME, USB_PRODUCT_DELORME_EMUSB },
	{ USB_VENDOR_DELORME, USB_PRODUCT_DELORME_EMLT20 },
};

int ucycom_match(struct device *, void *, void *);
void ucycom_attach(struct device *, struct device *, void *);
int ucycom_detach(struct device *, int);

struct cfdriver ucycom_cd = {
	NULL, "ucycom", DV_DULL
};

const struct cfattach ucycom_ca = {
	sizeof(struct ucycom_softc), ucycom_match, ucycom_attach, ucycom_detach
};

int
ucycom_match(struct device *parent, void *match, void *aux)
{
	struct uhidev_attach_arg *uha = aux;

	if (UHIDEV_CLAIM_MULTIPLE_REPORTID(uha))
		return (UMATCH_NONE);

	return (usb_lookup(ucycom_devs, uha->uaa->vendor, uha->uaa->product) != NULL ?
	    UMATCH_VENDOR_PRODUCT : UMATCH_NONE);
}

void
ucycom_attach(struct device *parent, struct device *self, void *aux)
{
	struct ucycom_softc *sc = (struct ucycom_softc *)self;
	struct usb_attach_arg *uaa = aux;
	struct uhidev_attach_arg *uha = (struct uhidev_attach_arg *)uaa;
	struct usbd_device *dev = uha->parent->sc_udev;
	struct ucom_attach_args uca;
	int size, repid, err;
	void *desc;

	sc->sc_hdev.sc_intr = ucycom_intr;
	sc->sc_hdev.sc_parent = uha->parent;
	sc->sc_hdev.sc_report_id = uha->reportid;

	uhidev_get_report_desc(uha->parent, &desc, &size);
	repid = uha->reportid;
	sc->sc_ilen = hid_report_size(desc, size, hid_input, repid);
	sc->sc_olen = hid_report_size(desc, size, hid_output, repid);
	sc->sc_flen = hid_report_size(desc, size, hid_feature, repid);

	DPRINTF(("ucycom_open: olen %d ilen %d flen %d\n", sc->sc_ilen,
	    sc->sc_olen, sc->sc_flen));

	printf("\n");

	sc->sc_udev = dev;

	err = uhidev_open(&sc->sc_hdev);
	if (err) {
		DPRINTF(("ucycom_open: uhidev_open %d\n", err));
		return;
	}

	DPRINTF(("ucycom attach: sc %p opipe %p ipipe %p report_id %d\n",
	    sc, sc->sc_hdev.sc_parent->sc_opipe, sc->sc_hdev.sc_parent->sc_ipipe,
	    uha->reportid));

	/* bulkin, bulkout set above */
	bzero(&uca, sizeof uca);
	uca.bulkin = uca.bulkout = -1;
	uca.ibufsize = sc->sc_ilen - 1;
	uca.obufsize = sc->sc_olen - 1;
	uca.ibufsizepad = 1;
	uca.opkthdrlen = 0;
	uca.uhidev = sc->sc_hdev.sc_parent;
	uca.device = uaa->device;
	uca.iface = uaa->iface;
	uca.methods = &ucycom_methods;
	uca.arg = sc;
	uca.info = NULL;

	sc->sc_subdev = config_found_sm(self, &uca, ucomprint, ucomsubmatch);
	DPRINTF(("ucycom_attach: complete %p\n", sc->sc_subdev));
}

void
ucycom_get_status(void *addr, int portno, u_char *lsr, u_char *msr)
{
	struct ucycom_softc *sc = addr;

	DPRINTF(("ucycom_get_status:\n"));

#if 0
	if (lsr != NULL)
		*lsr = sc->sc_lsr;
#endif
	if (msr != NULL)
		*msr = sc->sc_msr;
}

int
ucycom_open(void *addr, int portno)
{
	struct ucycom_softc *sc = addr;
	struct termios t;
	int err;

	DPRINTF(("ucycom_open: complete\n"));

	if (usbd_is_dying(sc->sc_udev))
		return (EIO);

	/* Allocate an output report buffer */
	sc->sc_obuf = malloc(sc->sc_olen, M_USBDEV, M_WAITOK | M_ZERO);

	/* Allocate an input report buffer */
	sc->sc_ibuf = malloc(sc->sc_ilen, M_USBDEV, M_WAITOK);

	DPRINTF(("ucycom_open: sc->sc_ibuf=%p sc->sc_obuf=%p \n",
	    sc->sc_ibuf, sc->sc_obuf));

	t.c_ospeed = 9600;
	t.c_cflag = CSTOPB | CS8;
	(void)ucycom_param(sc, portno, &t);

	sc->sc_mcr = UCYCOM_DTR | UCYCOM_RTS;
	sc->sc_obuf[0] = sc->sc_mcr;
	err = uhidev_write(sc->sc_hdev.sc_parent, sc->sc_obuf, sc->sc_olen);
	if (err) {
		DPRINTF(("ucycom_open: set RTS err=%d\n", err));
		return (EIO);
	}

	return (0);
}

void
ucycom_close(void *addr, int portno)
{
	struct ucycom_softc *sc = addr;
	int s;

	if (usbd_is_dying(sc->sc_udev))
		return;

	s = splusb();
	if (sc->sc_obuf != NULL) {
		free(sc->sc_obuf, M_USBDEV, sc->sc_olen);
		sc->sc_obuf = NULL;
	}
	if (sc->sc_ibuf != NULL) {
		free(sc->sc_ibuf, M_USBDEV, sc->sc_ilen);
		sc->sc_ibuf = NULL;
	}
	splx(s);
}

void
ucycom_read(void *addr, int portno, u_char **ptr, u_int32_t *count)
{
	struct ucycom_softc *sc = addr;

	if (sc->sc_newmsr ^ sc->sc_msr) {
		DPRINTF(("ucycom_read: msr %d new %d\n",
		    sc->sc_msr, sc->sc_newmsr));
		sc->sc_msr = sc->sc_newmsr;
		ucom_status_change((struct ucom_softc *)sc->sc_subdev);
	}

	DPRINTF(("ucycom_read: buf %p chars %d\n", sc->sc_ibuf, sc->sc_icnt));
	*ptr = sc->sc_ibuf;
	*count = sc->sc_icnt;
}

void
ucycom_write(void *addr, int portno, u_char *to, u_char *data, u_int32_t *cnt)
{
	struct ucycom_softc *sc = addr;
	u_int32_t len;
#ifdef UCYCOM_DEBUG
	u_int32_t want = *cnt;
#endif

	/*
	 * The 8 byte output report uses byte 0 for control and byte
	 * count.
	 *
	 * The 32 byte output report uses byte 0 for control. Byte 1
	 * is used for byte count.
	 */
	len = sc->sc_olen;
	memset(to, 0, len);
	switch (sc->sc_olen) {
	case 8:
		to[0] = *cnt | sc->sc_mcr;
		memcpy(&to[1], data, *cnt);
		DPRINTF(("ucycomstart(8): to[0] = %d | %d = %d\n",
		    *cnt, sc->sc_mcr, to[0]));
		break;

	case 32:
		to[0] = sc->sc_mcr;
		to[1] = *cnt;
		memcpy(&to[2], data, *cnt);
		DPRINTF(("ucycomstart(32): to[0] = %d\nto[1] = %d\n",
		    to[0], to[1]));
		break;
	}

#ifdef UCYCOM_DEBUG
	if (ucycomdebug > 5) {
		int i;

		if (len != 0) {
			DPRINTF(("ucycomstart: to[0..%d) =", len-1));
			for (i = 0; i < len; i++)
				DPRINTF((" %02x", to[i]));
			DPRINTF(("\n"));
		}
	}
#endif
	*cnt = len;

	DPRINTFN(4,("ucycomstart: req %d chars did %d chars\n", want, len));
}

int
ucycom_param(void *addr, int portno, struct termios *t)
{
	struct ucycom_softc *sc = addr;
	uint8_t report[5];
	size_t rlen;
	uint32_t baud = 0;
	uint8_t cfg;

	if (usbd_is_dying(sc->sc_udev))
		return (EIO);

	switch (t->c_ospeed) {
	case 600:
	case 1200:
	case 2400:
	case 4800:
	case 9600:
	case 19200:
	case 38400:
	case 57600:
#if 0
	/*
	 * Stock chips only support standard baud rates in the 600 - 57600
	 * range, but higher rates can be achieved using custom firmware.
	 */
	case 115200:
	case 153600:
	case 192000:
#endif
		baud = t->c_ospeed;
		break;
	default:
		return (EINVAL);
	}

	if (t->c_cflag & CIGNORE) {
		cfg = sc->sc_cfg;
	} else {
		cfg = 0;
		switch (t->c_cflag & CSIZE) {
		case CS8:
			cfg |= UCYCOM_DATA_BITS_8;
			break;
		case CS7:
			cfg |= UCYCOM_DATA_BITS_7;
			break;
		case CS6:
			cfg |= UCYCOM_DATA_BITS_6;
			break;
		case CS5:
			cfg |= UCYCOM_DATA_BITS_5;
			break;
		default:
			return (EINVAL);
		}
		cfg |= ISSET(t->c_cflag, CSTOPB) ?
		    UCYCOM_STOP_BITS_2 : UCYCOM_STOP_BITS_1;
		cfg |= ISSET(t->c_cflag, PARENB) ?
		    UCYCOM_PARITY_ON : UCYCOM_PARITY_OFF;
		cfg |= ISSET(t->c_cflag, PARODD) ?
		    UCYCOM_PARITY_ODD : UCYCOM_PARITY_EVEN;
	}

	DPRINTF(("ucycom_param: setting %d baud, %d-%c-%d (%d)\n", baud,
	    5 + (cfg & UCYCOM_DATA_MASK),
	    (cfg & UCYCOM_PARITY_MASK) ?
		((cfg & UCYCOM_PARITY_TYPE_MASK) ? 'O' : 'E') : 'N',
	    (cfg & UCYCOM_STOP_MASK) ? 2 : 1, cfg));

	report[0] = baud & 0xff;
	report[1] = (baud >> 8) & 0xff;
	report[2] = (baud >> 16) & 0xff;
	report[3] = (baud >> 24) & 0xff;
	report[4] = cfg;
	rlen = MIN(sc->sc_flen, sizeof(report));
	if (uhidev_set_report(sc->sc_hdev.sc_parent, UHID_FEATURE_REPORT,
	    sc->sc_hdev.sc_report_id, report, rlen) != rlen)
		return EIO;
	sc->sc_baud = baud;
	return (0);
}

void
ucycom_intr(struct uhidev *addr, void *ibuf, u_int len)
{
	extern void ucomreadcb(struct usbd_xfer *, void *, usbd_status);
	struct ucycom_softc *sc = (struct ucycom_softc *)addr;
	uint8_t *cp = ibuf;
	int n, st, s;

	/* not accepting data anymore.. */
	if (sc->sc_ibuf == NULL)
		return;

	/* We understand 8 byte and 32 byte input records */
	switch (len) {
	case 8:
		n = cp[0] & UCYCOM_LMASK;
		st = cp[0] & ~UCYCOM_LMASK;
		cp++;
		break;

	case 32:
		st = cp[0];
		n = cp[1];
		cp += 2;
		break;

	default:
		DPRINTFN(3,("ucycom_intr: Unknown input report length\n"));
		return;
	}

#ifdef UCYCOM_DEBUG
	if (ucycomdebug > 5) {
		u_int32_t i;

		if (n != 0) {
			DPRINTF(("ucycom_intr: ibuf[0..%d) =", n));
			for (i = 0; i < n; i++)
				DPRINTF((" %02x", cp[i]));
			DPRINTF(("\n"));
		}
	}
#endif

	if (n > 0 || st != sc->sc_msr) {
		s = spltty();
		sc->sc_newmsr = st;
		bcopy(cp, sc->sc_ibuf, n);
		sc->sc_icnt = n;
		ucomreadcb(addr->sc_parent->sc_ixfer, sc->sc_subdev,
		    USBD_NORMAL_COMPLETION);
		splx(s);
	}
}

void
ucycom_set(void *addr, int portno, int reg, int onoff)
{
	struct ucycom_softc *sc = addr;
	int err;

	switch (reg) {
	case UCOM_SET_DTR:
		if (onoff)
			SET(sc->sc_mcr, UCYCOM_DTR);
		else
			CLR(sc->sc_mcr, UCYCOM_DTR);
		break;
	case UCOM_SET_RTS:
		if (onoff)
			SET(sc->sc_mcr, UCYCOM_RTS);
		else
			CLR(sc->sc_mcr, UCYCOM_RTS);
		break;
	case UCOM_SET_BREAK:
		break;
	}

	memset(sc->sc_obuf, 0, sc->sc_olen);
	sc->sc_obuf[0] = sc->sc_mcr;

	err = uhidev_write(sc->sc_hdev.sc_parent, sc->sc_obuf, sc->sc_olen);
	if (err)
		DPRINTF(("ucycom_set_status: err=%d\n", err));
}

int
ucycom_detach(struct device *self, int flags)
{
	struct ucycom_softc *sc = (struct ucycom_softc *)self;

	DPRINTF(("ucycom_detach: sc=%p flags=%d\n", sc, flags));
	if (sc->sc_subdev != NULL) {
		config_detach(sc->sc_subdev, flags);
		sc->sc_subdev = NULL;
	}

	if (sc->sc_hdev.sc_state & UHIDEV_OPEN)
		uhidev_close(&sc->sc_hdev);

	return (0);
}
