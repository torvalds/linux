/*	$OpenBSD: ukspan.c,v 1.4 2022/04/09 20:07:44 naddy Exp $ */

/*
 * Copyright (c) 2019 Cody Cutler <ccutler@csail.mit.edu>
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
 * I don't know of any technical documentation for the Keyspan USA-19HS. I
 * inspected the Linux driver (drivers/usb/serial/keyspan_usa90msg.h) to learn
 * the device message format and the procedure for setting the baud rate.
 */

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/tty.h>

#include <dev/usb/usb.h>
#include <dev/usb/usbdevs.h>
#include <dev/usb/usbdi.h>
#include <dev/usb/usbdi_util.h>

#include <dev/usb/ucomvar.h>

/*#define UKSPAN_DEBUG */

#ifdef UKSPAN_DEBUG
	#define DPRINTF(x...)	do { printf(x); } while (0)
#else
	#define DPRINTF(x...)	do { ; } while (0)
#endif

#define UKSPAN_PARITY_NONE	0x0
#define UKSPAN_PARITY_ODD	0x08
#define UKSPAN_PARITY_EVEN	0x18

#define UKSPAN_DATA_5		0x0
#define UKSPAN_DATA_6		0x1
#define UKSPAN_DATA_7		0x2
#define UKSPAN_DATA_8		0x3

#define UKSPAN_STOP_1		0x0
#define UKSPAN_STOP_2		0x4

#define UKSPAN_MAGIC		0x2

#define UKSPAN_CLOCK		14769231

/*
 * The following USB indexes and endpoint addresses may be specific to the
 * Keyspan USA19HS device
 */
#define UKSPAN_CONFIG_IDX	1
#define UKSPAN_IFACE_IDX	0

#define UKSPAN_EA_BULKIN	(UE_DIR_IN  | 1)
#define UKSPAN_EA_BULKOUT	(UE_DIR_OUT | 1)
#define UKSPAN_EA_CONFIGIN	(UE_DIR_IN  | 2)
#define UKSPAN_EA_CONFIGOUT	(UE_DIR_OUT | 2)

/* Sent to device on control out endpoint */
struct ukspan_cmsg {
	uint8_t	setclock;
	uint8_t	baudlo;
	uint8_t	baudhi;
	uint8_t	setlcr;
	uint8_t	lcr;
	uint8_t	setrxmode;
	uint8_t	rxmode;
	uint8_t	settxmode;
	uint8_t	txmode;
	uint8_t	settxflowcontrol;
	uint8_t	txflowcontrol;
	uint8_t	setrxflowcontrol;
	uint8_t	rxflowcontrol;
	uint8_t	sendxoff;
	uint8_t	sendxon;
	uint8_t	xonchar;
	uint8_t	xoffchar;
	uint8_t	sendchar;
	uint8_t	txchar;
	uint8_t	setrts;
	uint8_t	rts;
	uint8_t	setdtr;
	uint8_t	dtr;

	uint8_t	rxforwardingchars;
	uint8_t	rxforwardingtimeoutms;
	uint8_t	txacksetting;

	uint8_t	portenabled;
	uint8_t	txflush;
	uint8_t	txbreak;
	uint8_t	loopbackmode;

	uint8_t	rxflush;
	uint8_t	rxforward;
	uint8_t	cancelrxoff;
	uint8_t	returnstatus;
} __packed;

/* Received from device on control in endpoint */
struct ukspan_smsg {
	uint8_t msr;
	uint8_t	cts;
	uint8_t	dcd;
	uint8_t	dsr;
	uint8_t	ri;
	uint8_t	txxoff;
	uint8_t	rxbreak;
	uint8_t	rxoverrun;
	uint8_t	rxparity;
	uint8_t	rxframe;
	uint8_t	portstate;
	uint8_t	messageack;
	uint8_t	charack;
	uint8_t	controlresp;
} __packed;

struct ukspan_softc {
	struct device sc_dev;
	struct usbd_device *udev;
	struct usbd_interface *iface;
	struct usbd_pipe *cout_pipe;
	struct usbd_pipe *cin_pipe;
	struct usbd_xfer *ixfer;
	struct usbd_xfer *oxfer;
	struct device *ucom_dev;
	struct ukspan_smsg smsg;
	struct ukspan_cmsg cmsg;
	u_char lsr;
	u_char msr;
};

int  ukspan_match(struct device *, void *, void *);
void ukspan_attach(struct device *, struct device *, void *);
int  ukspan_detach(struct device *, int);

void ukspan_close(void *, int);
int  ukspan_open(void *, int);
int  ukspan_param(void *, int, struct termios *);
void ukspan_set(void *, int, int, int);
void ukspan_get_status(void *, int, u_char *, u_char *);

void ukspan_cmsg_init(bool, struct ukspan_cmsg *);
int  ukspan_cmsg_send(struct ukspan_softc *);
void ukspan_incb(struct usbd_xfer *, void *, usbd_status);
void ukspan_outcb(struct usbd_xfer *, void *, usbd_status);
void ukspan_destroy(struct ukspan_softc *);

struct cfdriver ukspan_cd = {
	NULL, "ukspan", DV_DULL
};

const struct cfattach ukspan_ca = {
	sizeof(struct ukspan_softc), ukspan_match, ukspan_attach,
	ukspan_detach
};

static const struct usb_devno ukspan_devs[] = {
	{ USB_VENDOR_KEYSPAN, USB_PRODUCT_KEYSPAN_USA19HS },
};

static const struct ucom_methods ukspan_methods = {
	.ucom_get_status = ukspan_get_status,
	.ucom_set = ukspan_set,
	.ucom_param = ukspan_param,
	.ucom_ioctl = NULL,
	.ucom_open = ukspan_open,
	.ucom_close = ukspan_close,
	.ucom_read = NULL,
	.ucom_write = NULL,
};

int
ukspan_match(struct device *parent, void *match, void *aux)
{
	struct usb_attach_arg *uaa = aux;

	if (uaa->iface != NULL)
		return UMATCH_NONE;

	int found = usb_lookup(ukspan_devs, uaa->vendor, uaa->product) != NULL;
	return found ? UMATCH_VENDOR_PRODUCT : UMATCH_NONE;
}

void
ukspan_attach(struct device *parent, struct device *self, void *aux)
{
	struct ukspan_softc *sc = (struct ukspan_softc *)self;
	struct usb_attach_arg *uaa = aux;
	struct usbd_device *dev = uaa->device;
	struct ucom_attach_args uca = {0};
	usb_endpoint_descriptor_t *ed;
	const char *devname = sc->sc_dev.dv_xname;
	usbd_status err;
	int t1, t2, t3, t4;

	DPRINTF("attach\n");

	sc->udev = dev;
	sc->cin_pipe = sc->cout_pipe = NULL;
	sc->ixfer = sc->oxfer = NULL;
	sc->ucom_dev = NULL;

	/*
	 * Switch to configuration 1 where the transfer mode of the input
	 * endpoints is bulk instead of interrupt, as ucom expects
	 */
	err = usbd_set_config_index(sc->udev, UKSPAN_CONFIG_IDX, 1);
	if (err) {
		printf("%s: set config failed\n", devname);
		goto fail;
	}

	err = usbd_device2interface_handle(sc->udev, UKSPAN_IFACE_IDX,
	    &sc->iface);
	if (err) {
		printf("%s: get interface failed\n", devname);
		goto fail;
	}

	ed = usbd_get_endpoint_descriptor(sc->iface, UKSPAN_EA_BULKIN);
	t1 = UE_GET_XFERTYPE(ed->bmAttributes);
	uca.ibufsize = UGETW(ed->wMaxPacketSize);
	uca.bulkin = UKSPAN_EA_BULKIN;

	ed = usbd_get_endpoint_descriptor(sc->iface, UKSPAN_EA_BULKOUT);
	t2 = UE_GET_XFERTYPE(ed->bmAttributes);
	uca.obufsize = UGETW(ed->wMaxPacketSize);
	uca.bulkout = UKSPAN_EA_BULKOUT;

	ed = usbd_get_endpoint_descriptor(sc->iface, UKSPAN_EA_CONFIGIN);
	t3 = UE_GET_XFERTYPE(ed->bmAttributes);
	if (UGETW(ed->wMaxPacketSize) < sizeof(struct ukspan_smsg)) {
		printf("%s: in config packet size too small\n", devname);
		goto fail;
	}

	ed = usbd_get_endpoint_descriptor(sc->iface, UKSPAN_EA_CONFIGOUT);
	t4 = UE_GET_XFERTYPE(ed->bmAttributes);
	if (UGETW(ed->wMaxPacketSize) < sizeof(struct ukspan_cmsg)) {
		printf("%s: out config packet size too small\n", devname);
		goto fail;
	}

	if (t1 != UE_BULK || t2 != UE_BULK || t3 != UE_BULK || t4 != UE_BULK) {
		printf("%s: unexpected xfertypes %x %x %x %x != %x\n", devname,
		    t1, t2, t3, t4, UE_BULK);
		goto fail;
	}

	/* Resource acquisition starts here */
	err = usbd_open_pipe(sc->iface, UKSPAN_EA_CONFIGOUT, USBD_EXCLUSIVE_USE,
	    &sc->cout_pipe);
	if (err) {
		printf("%s: failed to create control out pipe\n", devname);
		goto fail;
	}

	err = usbd_open_pipe(sc->iface, UKSPAN_EA_CONFIGIN, USBD_EXCLUSIVE_USE,
	    &sc->cin_pipe);
	if (err) {
		printf("%s: failed to create control out pipe\n", devname);
		goto fail;
	}

	sc->ixfer = usbd_alloc_xfer(sc->udev);
	sc->oxfer = usbd_alloc_xfer(sc->udev);
	if (!sc->ixfer || !sc->oxfer) {
		printf("%s: failed to allocate xfers\n", devname);
		goto fail;
	}

	usbd_setup_xfer(sc->ixfer, sc->cin_pipe, sc, &sc->smsg,
	    sizeof(sc->smsg), 0, USBD_NO_TIMEOUT, ukspan_incb);
	err = usbd_transfer(sc->ixfer);
	if (err && err != USBD_IN_PROGRESS) {
		printf("%s: failed to start ixfer\n", devname);
		goto fail;
	}

	uca.portno = UCOM_UNK_PORTNO;
	uca.ibufsizepad = uca.ibufsize;
	uca.opkthdrlen = 0;
	uca.device = dev;
	uca.iface = sc->iface;
	uca.methods = &ukspan_methods;
	uca.arg = sc;
	uca.info = NULL;

	sc->ucom_dev = config_found_sm(self, &uca, ucomprint, ucomsubmatch);

	DPRINTF("attach done\n");

	return;
fail:
	ukspan_destroy(sc);
	usbd_deactivate(sc->udev);
}

int
ukspan_detach(struct device *self, int flags)
{
	struct ukspan_softc *sc = (struct ukspan_softc *)self;
	DPRINTF("detach\n");

	ukspan_destroy(sc);

	if (sc->ucom_dev) {
		config_detach(sc->ucom_dev, flags);
		sc->ucom_dev = NULL;
	}
	return 0;
}

void
ukspan_outcb(struct usbd_xfer *xfer, void *priv, usbd_status status)
{
	struct ukspan_softc *sc = priv;
	const char *devname = sc->sc_dev.dv_xname;

	DPRINTF("outcb\n");

	if (usbd_is_dying(sc->udev)) {
		DPRINTF("usb dying\n");
		return;
	}
	if (status != USBD_NORMAL_COMPLETION) {
		printf("%s: oxfer failed\n", devname);
		return;
	}
}

void
ukspan_incb(struct usbd_xfer *xfer, void *priv, usbd_status status)
{
	struct ukspan_softc *sc = priv;
	const char *devname = sc->sc_dev.dv_xname;
	const struct ukspan_smsg *smsg = &sc->smsg;
	usbd_status err;
	u_int32_t len;

	DPRINTF("incb\n");

	if (usbd_is_dying(sc->udev)) {
		printf("%s: usb dying\n", devname);
		return;
	}
	if (!sc->cin_pipe || !sc->ixfer) {
		printf("%s: no cin_pipe, but not dying?\n", devname);
		return;
	}
	if (status != USBD_NORMAL_COMPLETION) {
		if (status != USBD_NOT_STARTED && status != USBD_CANCELLED)
			printf("%s: ixfer failed\n", devname);
		return;
	}

	usbd_get_xfer_status(xfer, NULL, NULL, &len, NULL);
	if (len < sizeof(struct ukspan_smsg)) {
		printf("%s: short read\n", devname);
		return;
	}

	/* The device provides the actual MSR register */
	sc->msr = smsg->msr;
	/* But not LSR... */
	sc->lsr = (smsg->rxoverrun ? ULSR_OE : 0) |
		  (smsg->rxparity  ? ULSR_PE : 0) |
		  (smsg->rxframe   ? ULSR_FE : 0) |
		  (smsg->rxbreak   ? ULSR_BI : 0);
	ucom_status_change((struct ucom_softc *)sc->ucom_dev);

	usbd_setup_xfer(sc->ixfer, sc->cin_pipe, sc, &sc->smsg,
	    sizeof(sc->smsg), USBD_SHORT_XFER_OK, USBD_NO_TIMEOUT,
	    ukspan_incb);
	err = usbd_transfer(sc->ixfer);
	if (err && err != USBD_IN_PROGRESS)
		printf("%s: usbd transfer failed\n", devname);
}

void
ukspan_get_status(void *addr, int portno, u_char *lsr, u_char *msr)
{
	struct ukspan_softc *sc = addr;
	DPRINTF("get status\n");
	if (lsr)
		*lsr = sc->lsr;
	if (msr)
		*msr = sc->msr;
}

void
ukspan_cmsg_init(bool opening, struct ukspan_cmsg *omsg)
{
	bzero(omsg, sizeof(*omsg));

	omsg->xonchar = 17;
	omsg->xoffchar = 19;

	omsg->rxforwardingchars = 16;
	omsg->rxforwardingtimeoutms = 16;
	omsg->txacksetting = 0;
	omsg->txbreak = 0;
	if (opening) {
		omsg->portenabled = 1;
		omsg->rxflush = 1;
	}
}

int
ukspan_cmsg_send(struct ukspan_softc *sc)
{
	const char *devname = sc->sc_dev.dv_xname;
	usbd_status err;

	usbd_setup_xfer(sc->oxfer, sc->cout_pipe, sc, &sc->cmsg,
	    sizeof(sc->cmsg), USBD_SYNCHRONOUS, USBD_NO_TIMEOUT, ukspan_outcb);
	err = usbd_transfer(sc->oxfer);
	if (err != USBD_NORMAL_COMPLETION) {
		printf("%s: control xfer failed\n", devname);
		return EIO;
	}
	return 0;
}

void
ukspan_set(void *addr, int portno, int reg, int onoff)
{
	struct ukspan_softc *sc = addr;
	const char *devname = sc->sc_dev.dv_xname;
	DPRINTF("set %#x = %#x\n", reg, onoff);
	int flag = !!onoff;
	switch (reg) {
	case UCOM_SET_DTR:
		sc->cmsg.setdtr = 1;
		sc->cmsg.dtr = flag;
		break;
	case UCOM_SET_RTS:
		sc->cmsg.setrts = 1;
		sc->cmsg.rts = flag;
		break;
	case UCOM_SET_BREAK:
		sc->cmsg.txbreak = flag;
		break;
	default:
		printf("%s: unhandled reg %#x\n", devname, reg);
		return;
	}
	ukspan_cmsg_send(sc);
}

int
ukspan_param(void *addr, int portno, struct termios *ti)
{
	struct ukspan_softc *sc = addr;
	const char *devname = sc->sc_dev.dv_xname;
	struct ukspan_cmsg *cmsg = &sc->cmsg;
	speed_t baud;
	tcflag_t cflag;
	u_int32_t div;
	u_int8_t lcr;

	DPRINTF("param: %#x %#x %#x\n", ti->c_ospeed, ti->c_cflag, ti->c_iflag);

	/* Set baud */
	div = 1;
	baud = ti->c_ospeed;
	switch (baud) {
	case B300:
	case B600:
	case B1200:
	case B2400:
	case B4800:
	case B9600:
	case B19200:
	case B38400:
	case B57600:
	case B115200:
	case B230400:
		div = UKSPAN_CLOCK / (baud * 16);
		break;
	default:
		printf("%s: unexpected baud: %d\n", devname, baud);
		return EINVAL;
	}

	cmsg->setclock = 1;
	cmsg->baudlo = div & 0xff;
	cmsg->baudhi = div >> 8;

	cmsg->setrxmode = 1;
	cmsg->settxmode = 1;
	if (baud > 57600)
		cmsg->rxmode = cmsg->txmode = UKSPAN_MAGIC;
	else
		cmsg->rxmode = cmsg->txmode = 0;

	/* Set parity, data, and stop bits */
	cflag = ti->c_cflag;
	if ((cflag & CIGNORE) == 0) {
		if (cflag & PARENB)
			lcr = (cflag & PARODD) ? UKSPAN_PARITY_ODD :
			    UKSPAN_PARITY_EVEN;
		else
			lcr = UKSPAN_PARITY_NONE;
		switch (cflag & CSIZE) {
		case CS5:
			lcr |= UKSPAN_DATA_5;
			break;
		case CS6:
			lcr |= UKSPAN_DATA_6;
			break;
		case CS7:
			lcr |= UKSPAN_DATA_7;
			break;
		case CS8:
			lcr |= UKSPAN_DATA_8;
			break;
		}

		lcr |= (cflag & CSTOPB) ? UKSPAN_STOP_2 : UKSPAN_STOP_1;

		cmsg->setlcr = 1;
		cmsg->lcr = lcr;
	}

	/* XXX flow control? */

	ukspan_cmsg_send(sc);
	return 0;
}

int
ukspan_open(void *addr, int portno)
{
	struct ukspan_softc *sc = addr;
	int ret;

	DPRINTF("open\n");
	if (usbd_is_dying(sc->udev)) {
		DPRINTF("usb dying\n");
		return ENXIO;
	}

	ukspan_cmsg_init(true, &sc->cmsg);
	ret = ukspan_cmsg_send(sc);
	return ret;
}

void
ukspan_close(void *addr, int portno)
{
	struct ukspan_softc *sc = addr;
	DPRINTF("close\n");
	if (usbd_is_dying(sc->udev)) {
		DPRINTF("usb dying\n");
		return;
	}
	ukspan_cmsg_init(false, &sc->cmsg);
	ukspan_cmsg_send(sc);
}

void
ukspan_destroy(struct ukspan_softc *sc)
{
	DPRINTF("destroy\n");
	if (sc->cin_pipe) {
		usbd_close_pipe(sc->cin_pipe);
		sc->cin_pipe = NULL;
	}
	if (sc->cout_pipe) {
		usbd_close_pipe(sc->cout_pipe);
		sc->cout_pipe = NULL;
	}
	if (sc->oxfer) {
		usbd_free_xfer(sc->oxfer);
		sc->oxfer = NULL;
	}
	if (sc->ixfer) {
		usbd_free_xfer(sc->ixfer);
		sc->ixfer = NULL;
	}
}
