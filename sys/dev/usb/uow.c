/*	$OpenBSD: uow.c,v 1.38 2024/05/23 03:21:09 jsg Exp $	*/

/*
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
 * Maxim/Dallas DS2490 USB 1-Wire adapter driver.
 */

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/device.h>

#include <dev/onewire/onewirereg.h>
#include <dev/onewire/onewirevar.h>

#include <dev/usb/usb.h>
#include <dev/usb/usbdevs.h>
#include <dev/usb/usbdi.h>

#include <dev/usb/uowreg.h>

#define UOW_TIMEOUT	1000	/* ms */

struct uow_softc {
	struct device		 sc_dev;

	struct onewire_bus	 sc_ow_bus;
	struct device		*sc_ow_dev;

	struct usbd_device	*sc_udev;
	struct usbd_interface	*sc_iface;
	struct usbd_pipe	*sc_ph_ibulk;
	struct usbd_pipe	*sc_ph_obulk;
	struct usbd_pipe	*sc_ph_intr;
	u_int8_t		 sc_regs[DS2490_NREGS];
	struct usbd_xfer	*sc_xfer_in;
	struct usbd_xfer	*sc_xfer_out;
	u_int8_t		 sc_fifo[DS2490_DATAFIFOSIZE];
};

int uow_match(struct device *, void *, void *); 
void uow_attach(struct device *, struct device *, void *); 
int uow_detach(struct device *, int); 
int uow_activate(struct device *, int); 

struct cfdriver uow_cd = { 
	NULL, "uow", DV_DULL 
}; 

const struct cfattach uow_ca = { 
	sizeof(struct uow_softc), 
	uow_match, 
	uow_attach, 
	uow_detach, 
	uow_activate, 
};

/* List of supported devices */
static const struct usb_devno uow_devs[] = {
	{ USB_VENDOR_DALLAS,		USB_PRODUCT_DALLAS_USB_FOB_IBUTTON }
};

int	uow_ow_reset(void *);
int	uow_ow_bit(void *, int);
int	uow_ow_read_byte(void *);
void	uow_ow_write_byte(void *, int);
void	uow_ow_read_block(void *, void *, int);
void	uow_ow_write_block(void *, const void *, int);
void	uow_ow_matchrom(void *, u_int64_t);
int	uow_ow_search(void *, u_int64_t *, int, u_int64_t);

int	uow_cmd(struct uow_softc *, int, int, int);
#define uow_ctlcmd(s, c, p)	uow_cmd((s), DS2490_CONTROL_CMD, (c), (p))
#define uow_commcmd(s, c, p)	uow_cmd((s), DS2490_COMM_CMD, (c), (p))
#define uow_modecmd(s, c, p)	uow_cmd((s), DS2490_MODE_CMD, (c), (p))

void	uow_intr(struct usbd_xfer *, void *, usbd_status);
int	uow_read(struct uow_softc *, void *, int);
int	uow_write(struct uow_softc *, const void *, int);
int	uow_reset(struct uow_softc *);

int
uow_match(struct device *parent, void *match, void *aux)
{
	struct usb_attach_arg *uaa = aux;

	if (uaa->iface == NULL || uaa->configno != DS2490_USB_CONFIG)
		return (UMATCH_NONE);

	return ((usb_lookup(uow_devs, uaa->vendor, uaa->product) != NULL) ?
	    UMATCH_VENDOR_PRODUCT : UMATCH_NONE);
}

void
uow_attach(struct device *parent, struct device *self, void *aux)
{
	struct uow_softc *sc = (struct uow_softc *)self;
	struct usb_attach_arg *uaa = aux;
	usb_interface_descriptor_t *id;
	usb_endpoint_descriptor_t *ed;
	int ep_ibulk = -1, ep_obulk = -1, ep_intr = -1;
	struct onewirebus_attach_args oba;
	usbd_status error;
	int i;

	sc->sc_udev = uaa->device;

	/* Get interface handle */
	if ((error = usbd_device2interface_handle(sc->sc_udev,
	    DS2490_USB_IFACE, &sc->sc_iface)) != 0) {
		printf("%s: failed to get iface %d: %s\n",
		    sc->sc_dev.dv_xname, DS2490_USB_IFACE,
		    usbd_errstr(error));
		return;
	}

	/* Find endpoints */
	id = usbd_get_interface_descriptor(sc->sc_iface);
	for (i = 0; i < id->bNumEndpoints; i++) {
		ed = usbd_interface2endpoint_descriptor(sc->sc_iface, i);
		if (ed == NULL) {
			printf("%s: failed to get endpoint %d descriptor\n",
			    sc->sc_dev.dv_xname, i);
			return;
		}

		if (UE_GET_DIR(ed->bEndpointAddress) == UE_DIR_IN &&
		    UE_GET_XFERTYPE(ed->bmAttributes) == UE_BULK)
			ep_ibulk = ed->bEndpointAddress;
		if (UE_GET_DIR(ed->bEndpointAddress) == UE_DIR_OUT &&
		    UE_GET_XFERTYPE(ed->bmAttributes) == UE_BULK)
			ep_obulk = ed->bEndpointAddress;
		if (UE_GET_DIR(ed->bEndpointAddress) == UE_DIR_IN &&
		    UE_GET_XFERTYPE(ed->bmAttributes) == UE_INTERRUPT)
			ep_intr = ed->bEndpointAddress;
	}
	if (ep_ibulk == -1 || ep_obulk == -1 || ep_intr == -1) {
		printf("%s: missing endpoint: ibulk %d, obulk %d, intr %d\n",
		   sc->sc_dev.dv_xname, ep_ibulk, ep_obulk, ep_intr);
		return;
	}

	/* Open pipes */
	if ((error = usbd_open_pipe(sc->sc_iface, ep_ibulk, USBD_EXCLUSIVE_USE,
	    &sc->sc_ph_ibulk)) != 0) {
		printf("%s: failed to open bulk-in pipe: %s\n",
		    sc->sc_dev.dv_xname, usbd_errstr(error));
		return;
	}
	if ((error = usbd_open_pipe(sc->sc_iface, ep_obulk, USBD_EXCLUSIVE_USE,
	    &sc->sc_ph_obulk)) != 0) {
		printf("%s: failed to open bulk-out pipe: %s\n",
		    sc->sc_dev.dv_xname, usbd_errstr(error));
		goto fail;
	}
	if ((error = usbd_open_pipe_intr(sc->sc_iface, ep_intr,
	    USBD_SHORT_XFER_OK, &sc->sc_ph_intr, sc,
	    sc->sc_regs, sizeof(sc->sc_regs), uow_intr,
	    USBD_DEFAULT_INTERVAL)) != 0) {
		printf("%s: failed to open intr pipe: %s\n",
		    sc->sc_dev.dv_xname, usbd_errstr(error));
		goto fail;
	}

	/* Allocate xfers for bulk transfers */
	if ((sc->sc_xfer_in = usbd_alloc_xfer(sc->sc_udev)) == NULL) {
		printf("%s: failed to alloc bulk-in xfer\n",
		    sc->sc_dev.dv_xname);
		goto fail;
	}
	if ((sc->sc_xfer_out = usbd_alloc_xfer(sc->sc_udev)) == NULL) {
		printf("%s: failed to alloc bulk-out xfer\n",
		    sc->sc_dev.dv_xname);
		goto fail;
	}

	memset(sc->sc_fifo, 0xff, sizeof(sc->sc_fifo));

	/* Reset device */
	uow_reset(sc);

	/* Attach 1-Wire bus */
	sc->sc_ow_bus.bus_cookie = sc;
	sc->sc_ow_bus.bus_reset = uow_ow_reset;
	sc->sc_ow_bus.bus_bit = uow_ow_bit;
	sc->sc_ow_bus.bus_read_byte = uow_ow_read_byte;
	sc->sc_ow_bus.bus_write_byte = uow_ow_write_byte;
	sc->sc_ow_bus.bus_read_block = uow_ow_read_block;
	sc->sc_ow_bus.bus_write_block = uow_ow_write_block;
	sc->sc_ow_bus.bus_matchrom = uow_ow_matchrom;
#if 0
	sc->sc_ow_bus.bus_search = uow_ow_search;
#endif

	bzero(&oba, sizeof(oba));
	oba.oba_bus = &sc->sc_ow_bus;
	sc->sc_ow_dev = config_found(self, &oba, onewirebus_print);

	return;

fail:
	if (sc->sc_ph_ibulk != NULL) {
		usbd_close_pipe(sc->sc_ph_ibulk);
		sc->sc_ph_ibulk = NULL;
	}
	if (sc->sc_ph_obulk != NULL) {
		usbd_close_pipe(sc->sc_ph_obulk);
		sc->sc_ph_obulk = NULL;
	}
	if (sc->sc_ph_intr != NULL) {
		usbd_close_pipe(sc->sc_ph_intr);
		sc->sc_ph_intr = NULL;
	}
	if (sc->sc_xfer_in != NULL) {
		usbd_free_xfer(sc->sc_xfer_in);
		sc->sc_xfer_in = NULL;
	}
	if (sc->sc_xfer_out != NULL) {
		usbd_free_xfer(sc->sc_xfer_out);
		sc->sc_xfer_out = NULL;
	}
}

int
uow_detach(struct device *self, int flags)
{
	struct uow_softc *sc = (struct uow_softc *)self;
	int rv = 0, s;

	s = splusb();

	if (sc->sc_ph_ibulk != NULL)
		usbd_close_pipe(sc->sc_ph_ibulk);
	if (sc->sc_ph_obulk != NULL)
		usbd_close_pipe(sc->sc_ph_obulk);
	if (sc->sc_ph_intr != NULL)
		usbd_close_pipe(sc->sc_ph_intr);

	if (sc->sc_xfer_in != NULL)
		usbd_free_xfer(sc->sc_xfer_in);
	if (sc->sc_xfer_out != NULL)
		usbd_free_xfer(sc->sc_xfer_out);

	if (sc->sc_ow_dev != NULL)
		rv = config_detach(sc->sc_ow_dev, flags);

	splx(s);

	return (rv);
}

int
uow_activate(struct device *self, int act)
{
	struct uow_softc *sc = (struct uow_softc *)self;
	int rv = 0;

	switch (act) {
	case DVACT_DEACTIVATE:
		if (sc->sc_ow_dev != NULL)
			rv = config_deactivate(sc->sc_ow_dev);
		usbd_deactivate(sc->sc_udev);
		break;
	}

	return (rv);
}

int
uow_ow_reset(void *arg)
{
	struct uow_softc *sc = arg;

	if (uow_commcmd(sc, DS2490_COMM_1WIRE_RESET | DS2490_BIT_IM, 0) != 0)
		return (1);

	/* XXX: check presence pulse */
	return (0);
}

int
uow_ow_bit(void *arg, int value)
{
	struct uow_softc *sc = arg;
	u_int8_t data;

	if (uow_commcmd(sc, DS2490_COMM_BIT_IO | DS2490_BIT_IM |
	    (value ? DS2490_BIT_D : 0), 0) != 0)
		return (1);
	if (uow_read(sc, &data, 1) != 1)
		return (1);

	return (data);
}

int
uow_ow_read_byte(void *arg)
{
	struct uow_softc *sc = arg;
	u_int8_t data;

	if (uow_commcmd(sc, DS2490_COMM_BYTE_IO | DS2490_BIT_IM, 0xff) != 0)
		return (-1);
	if (uow_read(sc, &data, 1) != 1)
		return (-1);

	return (data);
}

void
uow_ow_write_byte(void *arg, int value)
{
	struct uow_softc *sc = arg;
	u_int8_t data;

	if (uow_commcmd(sc, DS2490_COMM_BYTE_IO | DS2490_BIT_IM, value) != 0)
		return;
	uow_read(sc, &data, sizeof(data));
}

void
uow_ow_read_block(void *arg, void *buf, int len)
{
	struct uow_softc *sc = arg;

	if (uow_write(sc, sc->sc_fifo, len) != 0)
		return;
	if (uow_commcmd(sc, DS2490_COMM_BLOCK_IO | DS2490_BIT_IM, len) != 0)
		return;
	uow_read(sc, buf, len);
}

void
uow_ow_write_block(void *arg, const void *buf, int len)
{
	struct uow_softc *sc = arg;

	if (uow_write(sc, buf, len) != 0)
		return;
	if (uow_commcmd(sc, DS2490_COMM_BLOCK_IO | DS2490_BIT_IM, len) != 0)
		return;
}

void
uow_ow_matchrom(void *arg, u_int64_t rom)
{
	struct uow_softc *sc = arg;
	u_int8_t data[8];
	int i;

	for (i = 0; i < 8; i++)
		data[i] = (rom >> (i * 8)) & 0xff;

	if (uow_write(sc, data, 8) != 0)
		return;
	if (uow_commcmd(sc, DS2490_COMM_MATCH_ACCESS | DS2490_BIT_IM,
	    ONEWIRE_CMD_MATCH_ROM) != 0)
		return;
}

int
uow_ow_search(void *arg, u_int64_t *buf, int size, u_int64_t startrom)
{
	struct uow_softc *sc = arg;
	u_int8_t data[8];
	int i, rv;

	for (i = 0; i < 8; i++)
		data[i] = (startrom >> (i * 8)) & 0xff;

	if (uow_write(sc, data, 8) != 0)
		return (-1);
	if (uow_commcmd(sc, DS2490_COMM_SEARCH_ACCESS | DS2490_BIT_IM |
	    DS2490_BIT_SM | DS2490_BIT_RST | DS2490_BIT_F, size << 8 |
	    ONEWIRE_CMD_SEARCH_ROM) != 0)
		return (-1);

	if ((rv = uow_read(sc, buf, size * 8)) == -1)
		return (-1);

	return (rv / 8);
}

int
uow_cmd(struct uow_softc *sc, int type, int cmd, int param)
{
	usb_device_request_t req;
	usbd_status error;

	req.bmRequestType = UT_WRITE_VENDOR_DEVICE;
	req.bRequest = type;
	USETW(req.wValue, cmd);
	USETW(req.wIndex, param);
	USETW(req.wLength, 0);
	if ((error = usbd_do_request(sc->sc_udev, &req, NULL)) != 0) {
		printf("%s: cmd failed, type 0x%02x, cmd 0x%04x, "
		    "param 0x%04x: %s\n", sc->sc_dev.dv_xname, type, cmd,
		    param, usbd_errstr(error));
		if (cmd != DS2490_CTL_RESET_DEVICE)
			uow_reset(sc);
		return (1);
	}

again:
	if (tsleep_nsec(sc->sc_regs, PRIBIO, "uowcmd",
	    MSEC_TO_NSEC(UOW_TIMEOUT)) != 0) {
		printf("%s: cmd timeout, type 0x%02x, cmd 0x%04x, "
		    "param 0x%04x\n", sc->sc_dev.dv_xname, type, cmd,
		    param);
		return (1);
	}
	if ((sc->sc_regs[DS2490_ST_STFL] & DS2490_ST_STFL_IDLE) == 0)
		goto again;

	return (0);
}

void
uow_intr(struct usbd_xfer *xfer, void *priv, usbd_status status)
{
	struct uow_softc *sc = priv;

	if (status != USBD_NORMAL_COMPLETION) {
		if (status == USBD_NOT_STARTED || status == USBD_CANCELLED)
			return;
		if (status == USBD_STALLED)
			usbd_clear_endpoint_stall_async(sc->sc_ph_intr);
		return;
	}

	wakeup(sc->sc_regs);
}

int
uow_read(struct uow_softc *sc, void *buf, int len)
{
	usbd_status error;
	int count;

	/* XXX: implement FIFO status monitoring */
	if (len > DS2490_DATAFIFOSIZE) {
		printf("%s: read %d bytes, xfer too big\n",
		    sc->sc_dev.dv_xname, len);
		return (-1);
	}

	usbd_setup_xfer(sc->sc_xfer_in, sc->sc_ph_ibulk, sc, buf, len,
	    USBD_SHORT_XFER_OK | USBD_SYNCHRONOUS, UOW_TIMEOUT, NULL);
	error = usbd_transfer(sc->sc_xfer_in);
	if (error != 0) {
		printf("%s: read failed, len %d: %s\n",
		    sc->sc_dev.dv_xname, len, usbd_errstr(error));
		uow_reset(sc);
		return (-1);
	}

	usbd_get_xfer_status(sc->sc_xfer_in, NULL, NULL, &count, &error);
	return (count);
}

int
uow_write(struct uow_softc *sc, const void *buf, int len)
{
	usbd_status error;

	/* XXX: implement FIFO status monitoring */
	if (len > DS2490_DATAFIFOSIZE) {
		printf("%s: write %d bytes, xfer too big\n",
		    sc->sc_dev.dv_xname, len);
		return (1);
	}

	usbd_setup_xfer(sc->sc_xfer_out, sc->sc_ph_obulk, sc, (void *)buf,
	    len, USBD_SYNCHRONOUS, UOW_TIMEOUT, NULL);
	error = usbd_transfer(sc->sc_xfer_out);
	if (error != 0) {
		printf("%s: write failed, len %d: %s\n",
		    sc->sc_dev.dv_xname, len, usbd_errstr(error));
		uow_reset(sc);
		return (1);
	}

	return (0);
}

int
uow_reset(struct uow_softc *sc)
{
	return (uow_ctlcmd(sc, DS2490_CTL_RESET_DEVICE, 0));
}
