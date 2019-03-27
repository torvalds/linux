/*-
 * SPDX-License-Identifier: BSD-4-Clause
 *
 * Copyright (c) 1997, 1998, 1999, 2000
 *	Bill Paul <wpaul@ee.columbia.edu>.  All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 * 3. All advertising materials mentioning features or use of this software
 *    must display the following acknowledgement:
 *	This product includes software developed by Bill Paul.
 * 4. Neither the name of the author nor the names of any co-contributors
 *    may be used to endorse or promote products derived from this software
 *    without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY Bill Paul AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL Bill Paul OR THE VOICES IN HIS HEAD
 * BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
 * CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 * SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
 * INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
 * CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 * ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF
 * THE POSSIBILITY OF SUCH DAMAGE.
 */

#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");

/*
 * Kawasaki LSI KL5KUSB101B USB to ethernet adapter driver.
 *
 * Written by Bill Paul <wpaul@ee.columbia.edu>
 * Electrical Engineering Department
 * Columbia University, New York City
 */

/*
 * The KLSI USB to ethernet adapter chip contains an USB serial interface,
 * ethernet MAC and embedded microcontroller (called the QT Engine).
 * The chip must have firmware loaded into it before it will operate.
 * Packets are passed between the chip and host via bulk transfers.
 * There is an interrupt endpoint mentioned in the software spec, however
 * it's currently unused. This device is 10Mbps half-duplex only, hence
 * there is no media selection logic. The MAC supports a 128 entry
 * multicast filter, though the exact size of the filter can depend
 * on the firmware. Curiously, while the software spec describes various
 * ethernet statistics counters, my sample adapter and firmware combination
 * claims not to support any statistics counters at all.
 *
 * Note that once we load the firmware in the device, we have to be
 * careful not to load it again: if you restart your computer but
 * leave the adapter attached to the USB controller, it may remain
 * powered on and retain its firmware. In this case, we don't need
 * to load the firmware a second time.
 *
 * Special thanks to Rob Furr for providing an ADS Technologies
 * adapter for development and testing. No monkeys were harmed during
 * the development of this driver.
 */

#include <sys/stdint.h>
#include <sys/stddef.h>
#include <sys/param.h>
#include <sys/queue.h>
#include <sys/types.h>
#include <sys/systm.h>
#include <sys/socket.h>
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

#include <net/if.h>
#include <net/if_var.h>

#include <dev/usb/usb.h>
#include <dev/usb/usbdi.h>
#include <dev/usb/usbdi_util.h>
#include "usbdevs.h"

#define	USB_DEBUG_VAR kue_debug
#include <dev/usb/usb_debug.h>
#include <dev/usb/usb_process.h>

#include <dev/usb/net/usb_ethernet.h>
#include <dev/usb/net/if_kuereg.h>
#include <dev/usb/net/if_kuefw.h>

/*
 * Various supported device vendors/products.
 */
static const STRUCT_USB_HOST_ID kue_devs[] = {
#define	KUE_DEV(v,p) { USB_VP(USB_VENDOR_##v, USB_PRODUCT_##v##_##p) }
	KUE_DEV(3COM, 3C19250),
	KUE_DEV(3COM, 3C460),
	KUE_DEV(ABOCOM, URE450),
	KUE_DEV(ADS, UBS10BT),
	KUE_DEV(ADS, UBS10BTX),
	KUE_DEV(AOX, USB101),
	KUE_DEV(ASANTE, EA),
	KUE_DEV(ATEN, DSB650C),
	KUE_DEV(ATEN, UC10T),
	KUE_DEV(COREGA, ETHER_USB_T),
	KUE_DEV(DLINK, DSB650C),
	KUE_DEV(ENTREGA, E45),
	KUE_DEV(ENTREGA, XX1),
	KUE_DEV(ENTREGA, XX2),
	KUE_DEV(IODATA, USBETT),
	KUE_DEV(JATON, EDA),
	KUE_DEV(KINGSTON, XX1),
	KUE_DEV(KLSI, DUH3E10BT),
	KUE_DEV(KLSI, DUH3E10BTN),
	KUE_DEV(LINKSYS, USB10T),
	KUE_DEV(MOBILITY, EA),
	KUE_DEV(NETGEAR, EA101),
	KUE_DEV(NETGEAR, EA101X),
	KUE_DEV(PERACOM, ENET),
	KUE_DEV(PERACOM, ENET2),
	KUE_DEV(PERACOM, ENET3),
	KUE_DEV(PORTGEAR, EA8),
	KUE_DEV(PORTGEAR, EA9),
	KUE_DEV(PORTSMITH, EEA),
	KUE_DEV(SHARK, PA),
	KUE_DEV(SILICOM, GPE),
	KUE_DEV(SILICOM, U2E),
	KUE_DEV(SMC, 2102USB),
#undef KUE_DEV
};

/* prototypes */

static device_probe_t kue_probe;
static device_attach_t kue_attach;
static device_detach_t kue_detach;

static usb_callback_t kue_bulk_read_callback;
static usb_callback_t kue_bulk_write_callback;

static uether_fn_t kue_attach_post;
static uether_fn_t kue_init;
static uether_fn_t kue_stop;
static uether_fn_t kue_start;
static uether_fn_t kue_setmulti;
static uether_fn_t kue_setpromisc;

static int	kue_do_request(struct kue_softc *,
		    struct usb_device_request *, void *);
static int	kue_setword(struct kue_softc *, uint8_t, uint16_t);
static int	kue_ctl(struct kue_softc *, uint8_t, uint8_t, uint16_t,
		    void *, int);
static int	kue_load_fw(struct kue_softc *);
static void	kue_reset(struct kue_softc *);

#ifdef USB_DEBUG
static int kue_debug = 0;

static SYSCTL_NODE(_hw_usb, OID_AUTO, kue, CTLFLAG_RW, 0, "USB kue");
SYSCTL_INT(_hw_usb_kue, OID_AUTO, debug, CTLFLAG_RWTUN, &kue_debug, 0,
    "Debug level");
#endif

static const struct usb_config kue_config[KUE_N_TRANSFER] = {

	[KUE_BULK_DT_WR] = {
		.type = UE_BULK,
		.endpoint = UE_ADDR_ANY,
		.direction = UE_DIR_OUT,
		.bufsize = (MCLBYTES + 2 + 64),
		.flags = {.pipe_bof = 1,},
		.callback = kue_bulk_write_callback,
		.timeout = 10000,	/* 10 seconds */
	},

	[KUE_BULK_DT_RD] = {
		.type = UE_BULK,
		.endpoint = UE_ADDR_ANY,
		.direction = UE_DIR_IN,
		.bufsize = (MCLBYTES + 2),
		.flags = {.pipe_bof = 1,.short_xfer_ok = 1,},
		.callback = kue_bulk_read_callback,
		.timeout = 0,	/* no timeout */
	},
};

static device_method_t kue_methods[] = {
	/* Device interface */
	DEVMETHOD(device_probe, kue_probe),
	DEVMETHOD(device_attach, kue_attach),
	DEVMETHOD(device_detach, kue_detach),

	DEVMETHOD_END
};

static driver_t kue_driver = {
	.name = "kue",
	.methods = kue_methods,
	.size = sizeof(struct kue_softc),
};

static devclass_t kue_devclass;

DRIVER_MODULE(kue, uhub, kue_driver, kue_devclass, NULL, 0);
MODULE_DEPEND(kue, uether, 1, 1, 1);
MODULE_DEPEND(kue, usb, 1, 1, 1);
MODULE_DEPEND(kue, ether, 1, 1, 1);
MODULE_VERSION(kue, 1);
USB_PNP_HOST_INFO(kue_devs);

static const struct usb_ether_methods kue_ue_methods = {
	.ue_attach_post = kue_attach_post,
	.ue_start = kue_start,
	.ue_init = kue_init,
	.ue_stop = kue_stop,
	.ue_setmulti = kue_setmulti,
	.ue_setpromisc = kue_setpromisc,
};

/*
 * We have a custom do_request function which is almost like the
 * regular do_request function, except it has a much longer timeout.
 * Why? Because we need to make requests over the control endpoint
 * to download the firmware to the device, which can take longer
 * than the default timeout.
 */
static int
kue_do_request(struct kue_softc *sc, struct usb_device_request *req,
    void *data)
{
	usb_error_t err;

	err = uether_do_request(&sc->sc_ue, req, data, 60000);

	return (err);
}

static int
kue_setword(struct kue_softc *sc, uint8_t breq, uint16_t word)
{
	struct usb_device_request req;

	req.bmRequestType = UT_WRITE_VENDOR_DEVICE;
	req.bRequest = breq;
	USETW(req.wValue, word);
	USETW(req.wIndex, 0);
	USETW(req.wLength, 0);

	return (kue_do_request(sc, &req, NULL));
}

static int
kue_ctl(struct kue_softc *sc, uint8_t rw, uint8_t breq,
    uint16_t val, void *data, int len)
{
	struct usb_device_request req;

	if (rw == KUE_CTL_WRITE)
		req.bmRequestType = UT_WRITE_VENDOR_DEVICE;
	else
		req.bmRequestType = UT_READ_VENDOR_DEVICE;


	req.bRequest = breq;
	USETW(req.wValue, val);
	USETW(req.wIndex, 0);
	USETW(req.wLength, len);

	return (kue_do_request(sc, &req, data));
}

static int
kue_load_fw(struct kue_softc *sc)
{
	struct usb_device_descriptor *dd;
	uint16_t hwrev;
	usb_error_t err;

	dd = usbd_get_device_descriptor(sc->sc_ue.ue_udev);
	hwrev = UGETW(dd->bcdDevice);

	/*
	 * First, check if we even need to load the firmware.
	 * If the device was still attached when the system was
	 * rebooted, it may already have firmware loaded in it.
	 * If this is the case, we don't need to do it again.
	 * And in fact, if we try to load it again, we'll hang,
	 * so we have to avoid this condition if we don't want
	 * to look stupid.
	 *
	 * We can test this quickly by checking the bcdRevision
	 * code. The NIC will return a different revision code if
	 * it's probed while the firmware is still loaded and
	 * running.
	 */
	if (hwrev == 0x0202)
		return(0);

	/* Load code segment */
	err = kue_ctl(sc, KUE_CTL_WRITE, KUE_CMD_SEND_SCAN,
	    0, kue_code_seg, sizeof(kue_code_seg));
	if (err) {
		device_printf(sc->sc_ue.ue_dev, "failed to load code segment: %s\n",
		    usbd_errstr(err));
		return(ENXIO);
	}

	/* Load fixup segment */
	err = kue_ctl(sc, KUE_CTL_WRITE, KUE_CMD_SEND_SCAN,
	    0, kue_fix_seg, sizeof(kue_fix_seg));
	if (err) {
		device_printf(sc->sc_ue.ue_dev, "failed to load fixup segment: %s\n",
		    usbd_errstr(err));
		return(ENXIO);
	}

	/* Send trigger command. */
	err = kue_ctl(sc, KUE_CTL_WRITE, KUE_CMD_SEND_SCAN,
	    0, kue_trig_seg, sizeof(kue_trig_seg));
	if (err) {
		device_printf(sc->sc_ue.ue_dev, "failed to load trigger segment: %s\n",
		    usbd_errstr(err));
		return(ENXIO);
	}

	return (0);
}

static void
kue_setpromisc(struct usb_ether *ue)
{
	struct kue_softc *sc = uether_getsc(ue);
	struct ifnet *ifp = uether_getifp(ue);

	KUE_LOCK_ASSERT(sc, MA_OWNED);

	if (ifp->if_flags & IFF_PROMISC)
		sc->sc_rxfilt |= KUE_RXFILT_PROMISC;
	else
		sc->sc_rxfilt &= ~KUE_RXFILT_PROMISC;

	kue_setword(sc, KUE_CMD_SET_PKT_FILTER, sc->sc_rxfilt);
}

static void
kue_setmulti(struct usb_ether *ue)
{
	struct kue_softc *sc = uether_getsc(ue);
	struct ifnet *ifp = uether_getifp(ue);
	struct ifmultiaddr *ifma;
	int i = 0;

	KUE_LOCK_ASSERT(sc, MA_OWNED);

	if (ifp->if_flags & IFF_ALLMULTI || ifp->if_flags & IFF_PROMISC) {
		sc->sc_rxfilt |= KUE_RXFILT_ALLMULTI;
		sc->sc_rxfilt &= ~KUE_RXFILT_MULTICAST;
		kue_setword(sc, KUE_CMD_SET_PKT_FILTER, sc->sc_rxfilt);
		return;
	}

	sc->sc_rxfilt &= ~KUE_RXFILT_ALLMULTI;

	if_maddr_rlock(ifp);
	CK_STAILQ_FOREACH(ifma, &ifp->if_multiaddrs, ifma_link)
	{
		if (ifma->ifma_addr->sa_family != AF_LINK)
			continue;
		/*
		 * If there are too many addresses for the
		 * internal filter, switch over to allmulti mode.
		 */
		if (i == KUE_MCFILTCNT(sc))
			break;
		memcpy(KUE_MCFILT(sc, i),
		    LLADDR((struct sockaddr_dl *)ifma->ifma_addr),
		    ETHER_ADDR_LEN);
		i++;
	}
	if_maddr_runlock(ifp);

	if (i == KUE_MCFILTCNT(sc))
		sc->sc_rxfilt |= KUE_RXFILT_ALLMULTI;
	else {
		sc->sc_rxfilt |= KUE_RXFILT_MULTICAST;
		kue_ctl(sc, KUE_CTL_WRITE, KUE_CMD_SET_MCAST_FILTERS,
		    i, sc->sc_mcfilters, i * ETHER_ADDR_LEN);
	}

	kue_setword(sc, KUE_CMD_SET_PKT_FILTER, sc->sc_rxfilt);
}

/*
 * Issue a SET_CONFIGURATION command to reset the MAC. This should be
 * done after the firmware is loaded into the adapter in order to
 * bring it into proper operation.
 */
static void
kue_reset(struct kue_softc *sc)
{
	struct usb_config_descriptor *cd;
	usb_error_t err;

	cd = usbd_get_config_descriptor(sc->sc_ue.ue_udev);

	err = usbd_req_set_config(sc->sc_ue.ue_udev, &sc->sc_mtx,
	    cd->bConfigurationValue);
	if (err)
		DPRINTF("reset failed (ignored)\n");

	/* wait a little while for the chip to get its brains in order */
	uether_pause(&sc->sc_ue, hz / 100);
}

static void
kue_attach_post(struct usb_ether *ue)
{
	struct kue_softc *sc = uether_getsc(ue);
	int error;

	/* load the firmware into the NIC */
	error = kue_load_fw(sc);
	if (error) {
		device_printf(sc->sc_ue.ue_dev, "could not load firmware\n");
		/* ignore the error */
	}

	/* reset the adapter */
	kue_reset(sc);

	/* read ethernet descriptor */
	kue_ctl(sc, KUE_CTL_READ, KUE_CMD_GET_ETHER_DESCRIPTOR,
	    0, &sc->sc_desc, sizeof(sc->sc_desc));

	/* copy in ethernet address */
	memcpy(ue->ue_eaddr, sc->sc_desc.kue_macaddr, sizeof(ue->ue_eaddr));
}

/*
 * Probe for a KLSI chip.
 */
static int
kue_probe(device_t dev)
{
	struct usb_attach_arg *uaa = device_get_ivars(dev);

	if (uaa->usb_mode != USB_MODE_HOST)
		return (ENXIO);
	if (uaa->info.bConfigIndex != KUE_CONFIG_IDX)
		return (ENXIO);
	if (uaa->info.bIfaceIndex != KUE_IFACE_IDX)
		return (ENXIO);

	return (usbd_lookup_id_by_uaa(kue_devs, sizeof(kue_devs), uaa));
}

/*
 * Attach the interface. Allocate softc structures, do
 * setup and ethernet/BPF attach.
 */
static int
kue_attach(device_t dev)
{
	struct usb_attach_arg *uaa = device_get_ivars(dev);
	struct kue_softc *sc = device_get_softc(dev);
	struct usb_ether *ue = &sc->sc_ue;
	uint8_t iface_index;
	int error;

	device_set_usb_desc(dev);
	mtx_init(&sc->sc_mtx, device_get_nameunit(dev), NULL, MTX_DEF);

	iface_index = KUE_IFACE_IDX;
	error = usbd_transfer_setup(uaa->device, &iface_index,
	    sc->sc_xfer, kue_config, KUE_N_TRANSFER, sc, &sc->sc_mtx);
	if (error) {
		device_printf(dev, "allocating USB transfers failed\n");
		goto detach;
	}

	sc->sc_mcfilters = malloc(KUE_MCFILTCNT(sc) * ETHER_ADDR_LEN,
	    M_USBDEV, M_WAITOK);
	if (sc->sc_mcfilters == NULL) {
		device_printf(dev, "failed allocating USB memory\n");
		goto detach;
	}

	ue->ue_sc = sc;
	ue->ue_dev = dev;
	ue->ue_udev = uaa->device;
	ue->ue_mtx = &sc->sc_mtx;
	ue->ue_methods = &kue_ue_methods;

	error = uether_ifattach(ue);
	if (error) {
		device_printf(dev, "could not attach interface\n");
		goto detach;
	}
	return (0);			/* success */

detach:
	kue_detach(dev);
	return (ENXIO);			/* failure */
}

static int
kue_detach(device_t dev)
{
	struct kue_softc *sc = device_get_softc(dev);
	struct usb_ether *ue = &sc->sc_ue;

	usbd_transfer_unsetup(sc->sc_xfer, KUE_N_TRANSFER);
	uether_ifdetach(ue);
	mtx_destroy(&sc->sc_mtx);
	free(sc->sc_mcfilters, M_USBDEV);

	return (0);
}

/*
 * A frame has been uploaded: pass the resulting mbuf chain up to
 * the higher level protocols.
 */
static void
kue_bulk_read_callback(struct usb_xfer *xfer, usb_error_t error)
{
	struct kue_softc *sc = usbd_xfer_softc(xfer);
	struct usb_ether *ue = &sc->sc_ue;
	struct ifnet *ifp = uether_getifp(ue);
	struct usb_page_cache *pc;
	uint8_t buf[2];
	int len;
	int actlen;

	usbd_xfer_status(xfer, &actlen, NULL, NULL, NULL);

	switch (USB_GET_STATE(xfer)) {
	case USB_ST_TRANSFERRED:

		if (actlen <= (int)(2 + sizeof(struct ether_header))) {
			if_inc_counter(ifp, IFCOUNTER_IERRORS, 1);
			goto tr_setup;
		}
		pc = usbd_xfer_get_frame(xfer, 0);
		usbd_copy_out(pc, 0, buf, 2);
		actlen -= 2;
		len = buf[0] | (buf[1] << 8);
		len = min(actlen, len);

		uether_rxbuf(ue, pc, 2, len);
		/* FALLTHROUGH */
	case USB_ST_SETUP:
tr_setup:
		usbd_xfer_set_frame_len(xfer, 0, usbd_xfer_max_len(xfer));
		usbd_transfer_submit(xfer);
		uether_rxflush(ue);
		return;

	default:			/* Error */
		DPRINTF("bulk read error, %s\n",
		    usbd_errstr(error));

		if (error != USB_ERR_CANCELLED) {
			/* try to clear stall first */
			usbd_xfer_set_stall(xfer);
			goto tr_setup;
		}
		return;

	}
}

static void
kue_bulk_write_callback(struct usb_xfer *xfer, usb_error_t error)
{
	struct kue_softc *sc = usbd_xfer_softc(xfer);
	struct ifnet *ifp = uether_getifp(&sc->sc_ue);
	struct usb_page_cache *pc;
	struct mbuf *m;
	int total_len;
	int temp_len;
	uint8_t buf[2];

	switch (USB_GET_STATE(xfer)) {
	case USB_ST_TRANSFERRED:
		DPRINTFN(11, "transfer complete\n");
		if_inc_counter(ifp, IFCOUNTER_OPACKETS, 1);

		/* FALLTHROUGH */
	case USB_ST_SETUP:
tr_setup:
		IFQ_DRV_DEQUEUE(&ifp->if_snd, m);

		if (m == NULL)
			return;
		if (m->m_pkthdr.len > MCLBYTES)
			m->m_pkthdr.len = MCLBYTES;
		temp_len = (m->m_pkthdr.len + 2);
		total_len = (temp_len + (64 - (temp_len % 64)));

		/* the first two bytes are the frame length */

		buf[0] = (uint8_t)(m->m_pkthdr.len);
		buf[1] = (uint8_t)(m->m_pkthdr.len >> 8);

		pc = usbd_xfer_get_frame(xfer, 0);
		usbd_copy_in(pc, 0, buf, 2);
		usbd_m_copy_in(pc, 2, m, 0, m->m_pkthdr.len);

		usbd_frame_zero(pc, temp_len, total_len - temp_len);
		usbd_xfer_set_frame_len(xfer, 0, total_len);

		/*
		 * if there's a BPF listener, bounce a copy
		 * of this frame to him:
		 */
		BPF_MTAP(ifp, m);

		m_freem(m);

		usbd_transfer_submit(xfer);

		return;

	default:			/* Error */
		DPRINTFN(11, "transfer error, %s\n",
		    usbd_errstr(error));

		if_inc_counter(ifp, IFCOUNTER_OERRORS, 1);

		if (error != USB_ERR_CANCELLED) {
			/* try to clear stall first */
			usbd_xfer_set_stall(xfer);
			goto tr_setup;
		}
		return;

	}
}

static void
kue_start(struct usb_ether *ue)
{
	struct kue_softc *sc = uether_getsc(ue);

	/*
	 * start the USB transfers, if not already started:
	 */
	usbd_transfer_start(sc->sc_xfer[KUE_BULK_DT_RD]);
	usbd_transfer_start(sc->sc_xfer[KUE_BULK_DT_WR]);
}

static void
kue_init(struct usb_ether *ue)
{
	struct kue_softc *sc = uether_getsc(ue);
	struct ifnet *ifp = uether_getifp(ue);

	KUE_LOCK_ASSERT(sc, MA_OWNED);

	/* set MAC address */
	kue_ctl(sc, KUE_CTL_WRITE, KUE_CMD_SET_MAC,
	    0, IF_LLADDR(ifp), ETHER_ADDR_LEN);

	/* I'm not sure how to tune these. */
#if 0
	/*
	 * Leave this one alone for now; setting it
	 * wrong causes lockups on some machines/controllers.
	 */
	kue_setword(sc, KUE_CMD_SET_SOFS, 1);
#endif
	kue_setword(sc, KUE_CMD_SET_URB_SIZE, 64);

	/* load the multicast filter */
	kue_setpromisc(ue);

	usbd_xfer_set_stall(sc->sc_xfer[KUE_BULK_DT_WR]);

	ifp->if_drv_flags |= IFF_DRV_RUNNING;
	kue_start(ue);
}

static void
kue_stop(struct usb_ether *ue)
{
	struct kue_softc *sc = uether_getsc(ue);
	struct ifnet *ifp = uether_getifp(ue);

	KUE_LOCK_ASSERT(sc, MA_OWNED);

	ifp->if_drv_flags &= ~IFF_DRV_RUNNING;

	/*
	 * stop all the transfers, if not already stopped:
	 */
	usbd_transfer_stop(sc->sc_xfer[KUE_BULK_DT_WR]);
	usbd_transfer_stop(sc->sc_xfer[KUE_BULK_DT_RD]);
}
