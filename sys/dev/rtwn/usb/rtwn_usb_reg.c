/*	$OpenBSD: if_urtwn.c,v 1.16 2011/02/10 17:26:40 jakemsr Exp $	*/

/*-
 * Copyright (c) 2010 Damien Bergamini <damien.bergamini@free.fr>
 * Copyright (c) 2014 Kevin Lo <kevlo@FreeBSD.org>
 * Copyright (c) 2015-2016 Andriy Voskoboinyk <avos@FreeBSD.org>
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

#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");

#include <sys/param.h>
#include <sys/lock.h>
#include <sys/mutex.h>
#include <sys/mbuf.h>
#include <sys/kernel.h>
#include <sys/socket.h>
#include <sys/systm.h>
#include <sys/malloc.h>
#include <sys/queue.h>
#include <sys/taskqueue.h>
#include <sys/bus.h>
#include <sys/endian.h>

#include <net/if.h>
#include <net/ethernet.h>
#include <net/if_media.h>

#include <net80211/ieee80211_var.h>
#include <net80211/ieee80211_radiotap.h>

#include <dev/usb/usb.h>
#include <dev/usb/usbdi.h>

#include <dev/rtwn/if_rtwnvar.h>
#include <dev/rtwn/if_rtwn_debug.h>

#include <dev/rtwn/usb/rtwn_usb_var.h>
#include <dev/rtwn/usb/rtwn_usb_reg.h>

static int	rtwn_do_request(struct rtwn_softc *,
		    struct usb_device_request *, void *);
static int	rtwn_usb_read_region_1(struct rtwn_softc *,
		    uint16_t, uint8_t *, int);

/* USB Requests. */
#define R92C_REQ_REGS		0x05


static int
rtwn_do_request(struct rtwn_softc *sc, struct usb_device_request *req,
    void *data)
{
	struct rtwn_usb_softc *uc = RTWN_USB_SOFTC(sc);
	usb_error_t err;
	int ntries = 10;

	RTWN_ASSERT_LOCKED(sc);

	while (ntries--) {
		err = usbd_do_request_flags(uc->uc_udev, &sc->sc_mtx,
		    req, data, 0, NULL, 250 /* ms */);
		if (err == USB_ERR_NORMAL_COMPLETION)
			return (0);

		RTWN_DPRINTF(sc, RTWN_DEBUG_USB,
		    "%s: control request failed, %s (retries left: %d)\n",
		    __func__, usbd_errstr(err), ntries);
		if (err == USB_ERR_NOT_CONFIGURED)
			return (ENXIO);

		usb_pause_mtx(&sc->sc_mtx, hz / 100);
	}
	return (EIO);
}

/* export for rtwn_fw_write_block() */
int
rtwn_usb_write_region_1(struct rtwn_softc *sc, uint16_t addr, uint8_t *buf,
    int len)
{
	usb_device_request_t req;

	req.bmRequestType = UT_WRITE_VENDOR_DEVICE;
	req.bRequest = R92C_REQ_REGS;
	USETW(req.wValue, addr);
	USETW(req.wIndex, 0);
	USETW(req.wLength, len);
	return (rtwn_do_request(sc, &req, buf));
}

int
rtwn_usb_write_1(struct rtwn_softc *sc, uint16_t addr, uint8_t val)
{
	return (rtwn_usb_write_region_1(sc, addr, &val, sizeof(val)));
}

int
rtwn_usb_write_2(struct rtwn_softc *sc, uint16_t addr, uint16_t val)
{
	val = htole16(val);
	return (rtwn_usb_write_region_1(sc, addr, (uint8_t *)&val, sizeof(val)));
}

int
rtwn_usb_write_4(struct rtwn_softc *sc, uint16_t addr, uint32_t val)
{
	val = htole32(val);
	return (rtwn_usb_write_region_1(sc, addr, (uint8_t *)&val, sizeof(val)));
}

static int
rtwn_usb_read_region_1(struct rtwn_softc *sc, uint16_t addr, uint8_t *buf,
    int len)
{
	usb_device_request_t req;

	req.bmRequestType = UT_READ_VENDOR_DEVICE;
	req.bRequest = R92C_REQ_REGS;
	USETW(req.wValue, addr);
	USETW(req.wIndex, 0);
	USETW(req.wLength, len);
	return (rtwn_do_request(sc, &req, buf));
}

uint8_t
rtwn_usb_read_1(struct rtwn_softc *sc, uint16_t addr)
{
	uint8_t val;

	if (rtwn_usb_read_region_1(sc, addr, &val, 1) != 0)
		return (0xff);
	return (val);
}

uint16_t
rtwn_usb_read_2(struct rtwn_softc *sc, uint16_t addr)
{
	uint16_t val;

	if (rtwn_usb_read_region_1(sc, addr, (uint8_t *)&val, 2) != 0)
		return (0xffff);
	return (le16toh(val));
}

uint32_t
rtwn_usb_read_4(struct rtwn_softc *sc, uint16_t addr)
{
	uint32_t val;

	if (rtwn_usb_read_region_1(sc, addr, (uint8_t *)&val, 4) != 0)
		return (0xffffffff);
	return (le32toh(val));
}

void
rtwn_usb_delay(struct rtwn_softc *sc, int usec)
{

	/* 1ms delay as default is too big. */
	if (usec < 1000)
		DELAY(usec);
	else
		usb_pause_mtx(&sc->sc_mtx, msecs_to_ticks(usec / 1000));
}
