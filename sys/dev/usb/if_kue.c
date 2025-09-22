/*	$OpenBSD: if_kue.c,v 1.93 2024/05/23 03:21:08 jsg Exp $ */
/*	$NetBSD: if_kue.c,v 1.50 2002/07/16 22:00:31 augustss Exp $	*/
/*
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
 *
 * $FreeBSD: src/sys/dev/usb/if_kue.c,v 1.14 2000/01/14 01:36:15 wpaul Exp $
 */

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

/*
 * Ported to NetBSD and somewhat rewritten by Lennart Augustsson.
 */

#include "bpfilter.h"

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/sockio.h>
#include <sys/mbuf.h>
#include <sys/malloc.h>
#include <sys/device.h>

#include <net/if.h>

#if NBPFILTER > 0
#include <net/bpf.h>
#endif

#include <netinet/in.h>
#include <netinet/if_ether.h>

#include <dev/usb/usb.h>
#include <dev/usb/usbdi.h>
#include <dev/usb/usbdi_util.h>
#include <dev/usb/usbdevs.h>

#include <dev/usb/if_kuereg.h>
#include <dev/usb/if_kuevar.h>

#ifdef KUE_DEBUG
#define DPRINTF(x)	do { if (kuedebug) printf x; } while (0)
#define DPRINTFN(n,x)	do { if (kuedebug >= (n)) printf x; } while (0)
int	kuedebug = 0;
#else
#define DPRINTF(x)
#define DPRINTFN(n,x)
#endif

/*
 * Various supported device vendors/products.
 */
const struct usb_devno kue_devs[] = {
	{ USB_VENDOR_3COM, USB_PRODUCT_3COM_3C19250 },
	{ USB_VENDOR_3COM, USB_PRODUCT_3COM_3C460 },
	{ USB_VENDOR_ABOCOM, USB_PRODUCT_ABOCOM_URE450 },
	{ USB_VENDOR_ADS, USB_PRODUCT_ADS_UBS10BT },
	{ USB_VENDOR_ADS, USB_PRODUCT_ADS_UBS10BTX },
	{ USB_VENDOR_AOX, USB_PRODUCT_AOX_USB101 },
	{ USB_VENDOR_ASANTE, USB_PRODUCT_ASANTE_EA },
	{ USB_VENDOR_ATEN, USB_PRODUCT_ATEN_UC10T },
	{ USB_VENDOR_ATEN, USB_PRODUCT_ATEN_DSB650C },
	{ USB_VENDOR_COREGA, USB_PRODUCT_COREGA_ETHER_USB_T },
	{ USB_VENDOR_DLINK, USB_PRODUCT_DLINK_DSB650C },
	{ USB_VENDOR_ENTREGA, USB_PRODUCT_ENTREGA_E45 },
	{ USB_VENDOR_ENTREGA, USB_PRODUCT_ENTREGA_XX1 },
	{ USB_VENDOR_ENTREGA, USB_PRODUCT_ENTREGA_XX2 },
	{ USB_VENDOR_IODATA, USB_PRODUCT_IODATA_USBETT },
	{ USB_VENDOR_JATON, USB_PRODUCT_JATON_EDA },
	{ USB_VENDOR_KINGSTON, USB_PRODUCT_KINGSTON_XX1 },
	{ USB_VENDOR_KLSI, USB_PRODUCT_KLSI_DUH3E10BT },
	{ USB_VENDOR_KLSI, USB_PRODUCT_KLSI_DUH3E10BTN },
	{ USB_VENDOR_LINKSYS, USB_PRODUCT_LINKSYS_USB10T },
	{ USB_VENDOR_MOBILITY, USB_PRODUCT_MOBILITY_EA },
	{ USB_VENDOR_NETGEAR, USB_PRODUCT_NETGEAR_EA101 },
	{ USB_VENDOR_NETGEAR, USB_PRODUCT_NETGEAR_EA101X },
	{ USB_VENDOR_PERACOM, USB_PRODUCT_PERACOM_ENET },
	{ USB_VENDOR_PERACOM, USB_PRODUCT_PERACOM_ENET2 },
	{ USB_VENDOR_PERACOM, USB_PRODUCT_PERACOM_ENET3 },
	{ USB_VENDOR_PORTGEAR, USB_PRODUCT_PORTGEAR_EA8 },
	{ USB_VENDOR_PORTGEAR, USB_PRODUCT_PORTGEAR_EA9 },
	{ USB_VENDOR_PORTSMITH, USB_PRODUCT_PORTSMITH_EEA },
	{ USB_VENDOR_SHARK, USB_PRODUCT_SHARK_PA },
	{ USB_VENDOR_SILICOM, USB_PRODUCT_SILICOM_U2E },
	{ USB_VENDOR_SILICOM, USB_PRODUCT_SILICOM_GPE },
	{ USB_VENDOR_SMC, USB_PRODUCT_SMC_2102USB },
};

int kue_match(struct device *, void *, void *);
void kue_attach(struct device *, struct device *, void *);
int kue_detach(struct device *, int);

struct cfdriver kue_cd = {
	NULL, "kue", DV_IFNET
};

const struct cfattach kue_ca = {
	sizeof(struct kue_softc), kue_match, kue_attach, kue_detach
};

int kue_tx_list_init(struct kue_softc *);
int kue_rx_list_init(struct kue_softc *);
int kue_newbuf(struct kue_softc *, struct kue_chain *,struct mbuf *);
int kue_send(struct kue_softc *, struct mbuf *, int);
int kue_open_pipes(struct kue_softc *);
void kue_rxeof(struct usbd_xfer *, void *, usbd_status);
void kue_txeof(struct usbd_xfer *, void *, usbd_status);
void kue_start(struct ifnet *);
int kue_ioctl(struct ifnet *, u_long, caddr_t);
void kue_init(void *);
void kue_stop(struct kue_softc *);
void kue_watchdog(struct ifnet *);

void kue_setmulti(struct kue_softc *);
void kue_reset(struct kue_softc *);

usbd_status kue_ctl(struct kue_softc *, int, u_int8_t,
			   u_int16_t, void *, u_int32_t);
usbd_status kue_setword(struct kue_softc *, u_int8_t, u_int16_t);
int kue_load_fw(struct kue_softc *);
void kue_attachhook(struct device *);

usbd_status
kue_setword(struct kue_softc *sc, u_int8_t breq, u_int16_t word)
{
	usb_device_request_t	req;

	DPRINTFN(10,("%s: %s: enter\n", sc->kue_dev.dv_xname,__func__));

	req.bmRequestType = UT_WRITE_VENDOR_DEVICE;
	req.bRequest = breq;
	USETW(req.wValue, word);
	USETW(req.wIndex, 0);
	USETW(req.wLength, 0);

	return (usbd_do_request(sc->kue_udev, &req, NULL));
}

usbd_status
kue_ctl(struct kue_softc *sc, int rw, u_int8_t breq, u_int16_t val,
	void *data, u_int32_t len)
{
	usb_device_request_t	req;

	DPRINTFN(10,("%s: %s: enter, len=%d\n", sc->kue_dev.dv_xname,
		     __func__, len));

	if (rw == KUE_CTL_WRITE)
		req.bmRequestType = UT_WRITE_VENDOR_DEVICE;
	else
		req.bmRequestType = UT_READ_VENDOR_DEVICE;

	req.bRequest = breq;
	USETW(req.wValue, val);
	USETW(req.wIndex, 0);
	USETW(req.wLength, len);

	return (usbd_do_request(sc->kue_udev, &req, data));
}

int
kue_load_fw(struct kue_softc *sc)
{
	usb_device_descriptor_t *dd;
	usbd_status		err;
	struct kue_firmware	*fw;
	u_char			*buf;
	size_t			buflen;

	DPRINTFN(1,("%s: %s: enter\n", sc->kue_dev.dv_xname, __func__));

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
	if ((dd = usbd_get_device_descriptor(sc->kue_udev)) == NULL)
		return (EIO);
	if (UGETW(dd->bcdDevice) >= KUE_WARM_REV) {
		printf("%s: warm boot, no firmware download\n",
		       sc->kue_dev.dv_xname);
		return (0);
	}

	err = loadfirmware("kue", &buf, &buflen);
	if (err) {
		printf("%s: failed loadfirmware of file %s: errno %d\n",
		    sc->kue_dev.dv_xname, "kue", err);
		return (err);
	}
	fw = (struct kue_firmware *)buf;

	printf("%s: cold boot, downloading firmware\n",
	       sc->kue_dev.dv_xname);

	/* Load code segment */
	DPRINTFN(1,("%s: kue_load_fw: download code_seg\n",
		    sc->kue_dev.dv_xname));
	err = kue_ctl(sc, KUE_CTL_WRITE, KUE_CMD_SEND_SCAN,
	    0, (void *)&fw->data[0], ntohl(fw->codeseglen));
	if (err) {
		printf("%s: failed to load code segment: %s\n",
		    sc->kue_dev.dv_xname, usbd_errstr(err));
		free(buf, M_DEVBUF, buflen);
		return (EIO);
	}

	/* Load fixup segment */
	DPRINTFN(1,("%s: kue_load_fw: download fix_seg\n",
		    sc->kue_dev.dv_xname));
	err = kue_ctl(sc, KUE_CTL_WRITE, KUE_CMD_SEND_SCAN,
	    0, (void *)&fw->data[ntohl(fw->codeseglen)], ntohl(fw->fixseglen));
	if (err) {
		printf("%s: failed to load fixup segment: %s\n",
		    sc->kue_dev.dv_xname, usbd_errstr(err));
		free(buf, M_DEVBUF, buflen);
		return (EIO);
	}

	/* Send trigger command. */
	DPRINTFN(1,("%s: kue_load_fw: download trig_seg\n",
		    sc->kue_dev.dv_xname));
	err = kue_ctl(sc, KUE_CTL_WRITE, KUE_CMD_SEND_SCAN,
	    0, (void *)&fw->data[ntohl(fw->codeseglen) + ntohl(fw->fixseglen)],
	    ntohl(fw->trigseglen));
	if (err) {
		printf("%s: failed to load trigger segment: %s\n",
		    sc->kue_dev.dv_xname, usbd_errstr(err));
		free(buf, M_DEVBUF, buflen);
		return (EIO);
	}
	free(buf, M_DEVBUF, buflen);

	usbd_delay_ms(sc->kue_udev, 10);

	/*
	 * Reload device descriptor.
	 * Why? The chip without the firmware loaded returns
	 * one revision code. The chip with the firmware
	 * loaded and running returns a *different* revision
	 * code. This confuses the quirk mechanism, which is
	 * dependent on the revision data.
	 */
	(void)usbd_reload_device_desc(sc->kue_udev);

	DPRINTFN(1,("%s: %s: done\n", sc->kue_dev.dv_xname, __func__));

	/* Reset the adapter. */
	kue_reset(sc);

	return (0);
}

void
kue_setmulti(struct kue_softc *sc)
{
	struct arpcom		*ac = &sc->arpcom;
	struct ifnet		*ifp = GET_IFP(sc);
	struct ether_multi	*enm;
	struct ether_multistep	step;
	int			i;

	DPRINTFN(5,("%s: %s: enter\n", sc->kue_dev.dv_xname, __func__));

	if (ifp->if_flags & IFF_PROMISC || ac->ac_multirangecnt > 0) {
allmulti:
		ifp->if_flags |= IFF_ALLMULTI;
		sc->kue_rxfilt |= KUE_RXFILT_ALLMULTI;
		sc->kue_rxfilt &= ~KUE_RXFILT_MULTICAST;
		kue_setword(sc, KUE_CMD_SET_PKT_FILTER, sc->kue_rxfilt);
		return;
	}

	sc->kue_rxfilt &= ~KUE_RXFILT_ALLMULTI;

	i = 0;
	ETHER_FIRST_MULTI(step, ac, enm);
	while (enm != NULL) {
		if (i == KUE_MCFILTCNT(sc))
			goto allmulti;

		memcpy(KUE_MCFILT(sc, i), enm->enm_addrlo, ETHER_ADDR_LEN);
		ETHER_NEXT_MULTI(step, enm);
		i++;
	}

	ifp->if_flags &= ~IFF_ALLMULTI;

	sc->kue_rxfilt |= KUE_RXFILT_MULTICAST;
	kue_ctl(sc, KUE_CTL_WRITE, KUE_CMD_SET_MCAST_FILTERS,
	    i, sc->kue_mcfilters, i * ETHER_ADDR_LEN);

	kue_setword(sc, KUE_CMD_SET_PKT_FILTER, sc->kue_rxfilt);
}

/*
 * Issue a SET_CONFIGURATION command to reset the MAC. This should be
 * done after the firmware is loaded into the adapter in order to
 * bring it into proper operation.
 */
void
kue_reset(struct kue_softc *sc)
{
	DPRINTFN(5,("%s: %s: enter\n", sc->kue_dev.dv_xname, __func__));

	if (usbd_set_config_no(sc->kue_udev, KUE_CONFIG_NO, 1) ||
	    usbd_device2interface_handle(sc->kue_udev, KUE_IFACE_IDX,
					 &sc->kue_iface))
		printf("%s: reset failed\n", sc->kue_dev.dv_xname);

	/* Wait a little while for the chip to get its brains in order. */
	usbd_delay_ms(sc->kue_udev, 10);
}

/*
 * Probe for a KLSI chip.
 */
int
kue_match(struct device *parent, void *match, void *aux)
{
	struct usb_attach_arg	*uaa = aux;

	DPRINTFN(25,("kue_match: enter\n"));

	if (uaa->iface != NULL)
		return (UMATCH_NONE);

	return (usb_lookup(kue_devs, uaa->vendor, uaa->product) != NULL ?
	    UMATCH_VENDOR_PRODUCT : UMATCH_NONE);
}

void
kue_attachhook(struct device *self)
{
	struct kue_softc	*sc = (struct kue_softc *)self;
	int			s;
	struct ifnet		*ifp;
	struct usbd_device	*dev = sc->kue_udev;
	struct usbd_interface	*iface;
	usbd_status		err;
	usb_interface_descriptor_t	*id;
	usb_endpoint_descriptor_t	*ed;
	int			i;

	/* Load the firmware into the NIC. */
	if (kue_load_fw(sc)) {
		printf("%s: loading firmware failed\n",
		    sc->kue_dev.dv_xname);
		return;
	}

	err = usbd_device2interface_handle(dev, KUE_IFACE_IDX, &iface);
	if (err) {
		printf("%s: getting interface handle failed\n",
		    sc->kue_dev.dv_xname);
		return;
	}

	sc->kue_iface = iface;
	id = usbd_get_interface_descriptor(iface);

	/* Find endpoints. */
	for (i = 0; i < id->bNumEndpoints; i++) {
		ed = usbd_interface2endpoint_descriptor(iface, i);
		if (ed == NULL) {
			printf("%s: couldn't get ep %d\n",
			    sc->kue_dev.dv_xname, i);
			return;
		}
		if (UE_GET_DIR(ed->bEndpointAddress) == UE_DIR_IN &&
		    UE_GET_XFERTYPE(ed->bmAttributes) == UE_BULK) {
			sc->kue_ed[KUE_ENDPT_RX] = ed->bEndpointAddress;
		} else if (UE_GET_DIR(ed->bEndpointAddress) == UE_DIR_OUT &&
			   UE_GET_XFERTYPE(ed->bmAttributes) == UE_BULK) {
			sc->kue_ed[KUE_ENDPT_TX] = ed->bEndpointAddress;
		} else if (UE_GET_DIR(ed->bEndpointAddress) == UE_DIR_IN &&
			   UE_GET_XFERTYPE(ed->bmAttributes) == UE_INTERRUPT) {
			sc->kue_ed[KUE_ENDPT_INTR] = ed->bEndpointAddress;
		}
	}

	if (sc->kue_ed[KUE_ENDPT_RX] == 0 || sc->kue_ed[KUE_ENDPT_TX] == 0) {
		printf("%s: missing endpoint\n", sc->kue_dev.dv_xname);
		return;
	}

	/* Read ethernet descriptor */
	err = kue_ctl(sc, KUE_CTL_READ, KUE_CMD_GET_ETHER_DESCRIPTOR,
	    0, &sc->kue_desc, sizeof(sc->kue_desc));
	if (err) {
		printf("%s: could not read Ethernet descriptor\n",
		    sc->kue_dev.dv_xname);
		return;
	}

	sc->kue_mcfilters = mallocarray(KUE_MCFILTCNT(sc), ETHER_ADDR_LEN,
	    M_USBDEV, M_NOWAIT);
	if (sc->kue_mcfilters == NULL) {
		printf("%s: no memory for multicast filter buffer\n",
		    sc->kue_dev.dv_xname);
		return;
	}
	sc->kue_mcfilterslen = KUE_MCFILTCNT(sc);

	s = splnet();

	/*
	 * A KLSI chip was detected. Inform the world.
	 */
	printf("%s: address %s\n", sc->kue_dev.dv_xname,
	    ether_sprintf(sc->kue_desc.kue_macaddr));

	bcopy(sc->kue_desc.kue_macaddr,
	    (char *)&sc->arpcom.ac_enaddr, ETHER_ADDR_LEN);

	/* Initialize interface info.*/
	ifp = GET_IFP(sc);
	ifp->if_softc = sc;
	ifp->if_flags = IFF_BROADCAST | IFF_SIMPLEX | IFF_MULTICAST;
	ifp->if_ioctl = kue_ioctl;
	ifp->if_start = kue_start;
	ifp->if_watchdog = kue_watchdog;
	strlcpy(ifp->if_xname, sc->kue_dev.dv_xname, IFNAMSIZ);

	/* Attach the interface. */
	if_attach(ifp);
	ether_ifattach(ifp);

	sc->kue_attached = 1;
	splx(s);

}

/*
 * Attach the interface. Allocate softc structures, do
 * setup and ethernet/BPF attach.
 */
void
kue_attach(struct device *parent, struct device *self, void *aux)
{
	struct kue_softc	*sc = (struct kue_softc *)self;
	struct usb_attach_arg	*uaa = aux;
	struct usbd_device	*dev = uaa->device;
	usbd_status		err;

	DPRINTFN(5,(" : kue_attach: sc=%p, dev=%p", sc, dev));

	err = usbd_set_config_no(dev, KUE_CONFIG_NO, 1);
	if (err) {
		printf("%s: setting config no failed\n",
		    sc->kue_dev.dv_xname);
		return;
	}

	sc->kue_udev = dev;
	sc->kue_product = uaa->product;
	sc->kue_vendor = uaa->vendor;

	config_mountroot(self, kue_attachhook);
}

int
kue_detach(struct device *self, int flags)
{
	struct kue_softc	*sc = (struct kue_softc *)self;
	struct ifnet		*ifp = GET_IFP(sc);
	int			s;

	/* Detached before attached finished, so just bail out. */
	if (!sc->kue_attached)
		return (0);

	s = splusb();		/* XXX why? */

	if (sc->kue_mcfilters != NULL) {
		free(sc->kue_mcfilters, M_USBDEV, sc->kue_mcfilterslen);
		sc->kue_mcfilters = NULL;
	}

	if (ifp->if_flags & IFF_RUNNING)
		kue_stop(sc);

	if (ifp->if_softc != NULL) {
		ether_ifdetach(ifp);
		if_detach(ifp);
	}

#ifdef DIAGNOSTIC
	if (sc->kue_ep[KUE_ENDPT_TX] != NULL ||
	    sc->kue_ep[KUE_ENDPT_RX] != NULL ||
	    sc->kue_ep[KUE_ENDPT_INTR] != NULL)
		printf("%s: detach has active endpoints\n",
		       sc->kue_dev.dv_xname);
#endif

	sc->kue_attached = 0;
	splx(s);

	return (0);
}

/*
 * Initialize an RX descriptor and attach an MBUF cluster.
 */
int
kue_newbuf(struct kue_softc *sc, struct kue_chain *c, struct mbuf *m)
{
	struct mbuf		*m_new = NULL;

	DPRINTFN(10,("%s: %s: enter\n", sc->kue_dev.dv_xname,__func__));

	if (m == NULL) {
		MGETHDR(m_new, M_DONTWAIT, MT_DATA);
		if (m_new == NULL) {
			printf("%s: no memory for rx list "
			    "-- packet dropped!\n", sc->kue_dev.dv_xname);
			return (ENOBUFS);
		}

		MCLGET(m_new, M_DONTWAIT);
		if (!(m_new->m_flags & M_EXT)) {
			printf("%s: no memory for rx list "
			    "-- packet dropped!\n", sc->kue_dev.dv_xname);
			m_freem(m_new);
			return (ENOBUFS);
		}
		m_new->m_len = m_new->m_pkthdr.len = MCLBYTES;
	} else {
		m_new = m;
		m_new->m_len = m_new->m_pkthdr.len = MCLBYTES;
		m_new->m_data = m_new->m_ext.ext_buf;
	}

	c->kue_mbuf = m_new;

	return (0);
}

int
kue_rx_list_init(struct kue_softc *sc)
{
	struct kue_cdata	*cd;
	struct kue_chain	*c;
	int			i;

	DPRINTFN(5,("%s: %s: enter\n", sc->kue_dev.dv_xname, __func__));

	cd = &sc->kue_cdata;
	for (i = 0; i < KUE_RX_LIST_CNT; i++) {
		c = &cd->kue_rx_chain[i];
		c->kue_sc = sc;
		c->kue_idx = i;
		if (kue_newbuf(sc, c, NULL) == ENOBUFS)
			return (ENOBUFS);
		if (c->kue_xfer == NULL) {
			c->kue_xfer = usbd_alloc_xfer(sc->kue_udev);
			if (c->kue_xfer == NULL)
				return (ENOBUFS);
			c->kue_buf = usbd_alloc_buffer(c->kue_xfer, KUE_BUFSZ);
			if (c->kue_buf == NULL)
				return (ENOBUFS); /* XXX free xfer */
		}
	}

	return (0);
}

int
kue_tx_list_init(struct kue_softc *sc)
{
	struct kue_cdata	*cd;
	struct kue_chain	*c;
	int			i;

	DPRINTFN(5,("%s: %s: enter\n", sc->kue_dev.dv_xname, __func__));

	cd = &sc->kue_cdata;
	for (i = 0; i < KUE_TX_LIST_CNT; i++) {
		c = &cd->kue_tx_chain[i];
		c->kue_sc = sc;
		c->kue_idx = i;
		c->kue_mbuf = NULL;
		if (c->kue_xfer == NULL) {
			c->kue_xfer = usbd_alloc_xfer(sc->kue_udev);
			if (c->kue_xfer == NULL)
				return (ENOBUFS);
			c->kue_buf = usbd_alloc_buffer(c->kue_xfer, KUE_BUFSZ);
			if (c->kue_buf == NULL)
				return (ENOBUFS);
		}
	}

	return (0);
}

/*
 * A frame has been uploaded: pass the resulting mbuf chain up to
 * the higher level protocols.
 */
void
kue_rxeof(struct usbd_xfer *xfer, void *priv, usbd_status status)
{
	struct kue_chain	*c = priv;
	struct kue_softc	*sc = c->kue_sc;
	struct ifnet		*ifp = GET_IFP(sc);
	struct mbuf_list	ml = MBUF_LIST_INITIALIZER();
	struct mbuf		*m;
	int			total_len = 0;
	int			s;

	DPRINTFN(10,("%s: %s: enter status=%d\n", sc->kue_dev.dv_xname,
		     __func__, status));

	if (usbd_is_dying(sc->kue_udev))
		return;

	if (!(ifp->if_flags & IFF_RUNNING))
		return;

	if (status != USBD_NORMAL_COMPLETION) {
		if (status == USBD_NOT_STARTED || status == USBD_CANCELLED)
			return;
		sc->kue_rx_errs++;
		if (usbd_ratecheck(&sc->kue_rx_notice)) {
			printf("%s: %u usb errors on rx: %s\n",
			    sc->kue_dev.dv_xname, sc->kue_rx_errs,
			    usbd_errstr(status));
			sc->kue_rx_errs = 0;
		}
		if (status == USBD_STALLED)
			usbd_clear_endpoint_stall_async(sc->kue_ep[KUE_ENDPT_RX]);
		goto done;
	}

	usbd_get_xfer_status(xfer, NULL, NULL, &total_len, NULL);

	DPRINTFN(10,("%s: %s: total_len=%d len=%d\n", sc->kue_dev.dv_xname,
		     __func__, total_len,
		     UGETW(mtod(c->kue_mbuf, u_int8_t *))));

	if (total_len <= 1)
		goto done;

	m = c->kue_mbuf;
	/* copy data to mbuf */
	memcpy(mtod(m, char *), c->kue_buf, total_len);

	/* No errors; receive the packet. */
	total_len = UGETW(mtod(m, u_int8_t *));
	m_adj(m, sizeof(u_int16_t));

	if (total_len < sizeof(struct ether_header)) {
		ifp->if_ierrors++;
		goto done;
	}

	m->m_pkthdr.len = m->m_len = total_len;
	ml_enqueue(&ml, m);

	if (kue_newbuf(sc, c, NULL) == ENOBUFS) {
		ifp->if_ierrors++;
		goto done;
	}

	s = splnet();
	if_input(ifp, &ml);
	splx(s);

 done:

	/* Setup new transfer. */
	usbd_setup_xfer(c->kue_xfer, sc->kue_ep[KUE_ENDPT_RX],
	    c, c->kue_buf, KUE_BUFSZ, USBD_SHORT_XFER_OK | USBD_NO_COPY,
	    USBD_NO_TIMEOUT, kue_rxeof);
	usbd_transfer(c->kue_xfer);

	DPRINTFN(10,("%s: %s: start rx\n", sc->kue_dev.dv_xname,
		    __func__));
}

/*
 * A frame was downloaded to the chip. It's safe for us to clean up
 * the list buffers.
 */

void
kue_txeof(struct usbd_xfer *xfer, void *priv, usbd_status status)
{
	struct kue_chain	*c = priv;
	struct kue_softc	*sc = c->kue_sc;
	struct ifnet		*ifp = GET_IFP(sc);
	int			s;

	if (usbd_is_dying(sc->kue_udev))
		return;

	s = splnet();

	DPRINTFN(10,("%s: %s: enter status=%d\n", sc->kue_dev.dv_xname,
		    __func__, status));

	ifp->if_timer = 0;
	ifq_clr_oactive(&ifp->if_snd);

	if (status != USBD_NORMAL_COMPLETION) {
		if (status == USBD_NOT_STARTED || status == USBD_CANCELLED) {
			splx(s);
			return;
		}
		ifp->if_oerrors++;
		printf("%s: usb error on tx: %s\n", sc->kue_dev.dv_xname,
		    usbd_errstr(status));
		if (status == USBD_STALLED)
			usbd_clear_endpoint_stall_async(sc->kue_ep[KUE_ENDPT_TX]);
		splx(s);
		return;
	}

	m_freem(c->kue_mbuf);
	c->kue_mbuf = NULL;

	if (ifq_empty(&ifp->if_snd) == 0)
		kue_start(ifp);

	splx(s);
}

int
kue_send(struct kue_softc *sc, struct mbuf *m, int idx)
{
	int			total_len;
	struct kue_chain	*c;
	usbd_status		err;

	DPRINTFN(10,("%s: %s: enter\n", sc->kue_dev.dv_xname,__func__));

	c = &sc->kue_cdata.kue_tx_chain[idx];

	/*
	 * Copy the mbuf data into a contiguous buffer, leaving two
	 * bytes at the beginning to hold the frame length.
	 */
	m_copydata(m, 0, m->m_pkthdr.len, c->kue_buf + 2);
	c->kue_mbuf = m;

	total_len = m->m_pkthdr.len + 2;
	/* XXX what's this? */
	total_len += 64 - (total_len % 64);

	/* Frame length is specified in the first 2 bytes of the buffer. */
	c->kue_buf[0] = (u_int8_t)m->m_pkthdr.len;
	c->kue_buf[1] = (u_int8_t)(m->m_pkthdr.len >> 8);

	usbd_setup_xfer(c->kue_xfer, sc->kue_ep[KUE_ENDPT_TX],
	    c, c->kue_buf, total_len, USBD_NO_COPY, USBD_DEFAULT_TIMEOUT,
	    kue_txeof);

	/* Transmit */
	err = usbd_transfer(c->kue_xfer);
	if (err != USBD_IN_PROGRESS) {
		printf("%s: kue_send error=%s\n", sc->kue_dev.dv_xname,
		       usbd_errstr(err));
		c->kue_mbuf = NULL;
		kue_stop(sc);
		return (EIO);
	}

	sc->kue_cdata.kue_tx_cnt++;

	return (0);
}

void
kue_start(struct ifnet *ifp)
{
	struct kue_softc	*sc = ifp->if_softc;
	struct mbuf		*m_head = NULL;

	DPRINTFN(10,("%s: %s: enter\n", sc->kue_dev.dv_xname,__func__));

	if (usbd_is_dying(sc->kue_udev))
		return;

	if (ifq_is_oactive(&ifp->if_snd))
		return;

	m_head = ifq_dequeue(&ifp->if_snd);
	if (m_head == NULL)
		return;

	if (kue_send(sc, m_head, 0)) {
		m_freem(m_head);
		ifq_set_oactive(&ifp->if_snd);
		return;
	}

#if NBPFILTER > 0
	/*
	 * If there's a BPF listener, bounce a copy of this frame
	 * to him.
	 */
	if (ifp->if_bpf)
		bpf_mtap(ifp->if_bpf, m_head, BPF_DIRECTION_OUT);
#endif

	ifq_set_oactive(&ifp->if_snd);

	/*
	 * Set a timeout in case the chip goes out to lunch.
	 */
	ifp->if_timer = 6;
}

void
kue_init(void *xsc)
{
	struct kue_softc	*sc = xsc;
	struct ifnet		*ifp = GET_IFP(sc);
	int			s;
	u_char			*eaddr;

	DPRINTFN(5,("%s: %s: enter\n", sc->kue_dev.dv_xname,__func__));

	if (ifp->if_flags & IFF_RUNNING)
		return;

	s = splnet();

	eaddr = sc->arpcom.ac_enaddr;
	/* Set MAC address */
	kue_ctl(sc, KUE_CTL_WRITE, KUE_CMD_SET_MAC, 0, eaddr, ETHER_ADDR_LEN);

	sc->kue_rxfilt = KUE_RXFILT_UNICAST | KUE_RXFILT_BROADCAST;

	 /* If we want promiscuous mode, set the allframes bit. */
	if (ifp->if_flags & IFF_PROMISC)
		sc->kue_rxfilt |= KUE_RXFILT_PROMISC;

	kue_setword(sc, KUE_CMD_SET_PKT_FILTER, sc->kue_rxfilt);

	/* I'm not sure how to tune these. */
#if 0
	/*
	 * Leave this one alone for now; setting it
	 * wrong causes lockups on some machines/controllers.
	 */
	kue_setword(sc, KUE_CMD_SET_SOFS, 1);
#endif
	kue_setword(sc, KUE_CMD_SET_URB_SIZE, 64);

	/* Init TX ring. */
	if (kue_tx_list_init(sc) == ENOBUFS) {
		printf("%s: tx list init failed\n", sc->kue_dev.dv_xname);
		splx(s);
		return;
	}

	/* Init RX ring. */
	if (kue_rx_list_init(sc) == ENOBUFS) {
		printf("%s: rx list init failed\n", sc->kue_dev.dv_xname);
		splx(s);
		return;
	}

	/* Load the multicast filter. */
	kue_setmulti(sc);

	if (sc->kue_ep[KUE_ENDPT_RX] == NULL) {
		if (kue_open_pipes(sc)) {
			splx(s);
			return;
		}
	}

	ifp->if_flags |= IFF_RUNNING;
	ifq_clr_oactive(&ifp->if_snd);

	splx(s);
}

int
kue_open_pipes(struct kue_softc *sc)
{
	usbd_status		err;
	struct kue_chain	*c;
	int			i;

	DPRINTFN(5,("%s: %s: enter\n", sc->kue_dev.dv_xname,__func__));

	/* Open RX and TX pipes. */
	err = usbd_open_pipe(sc->kue_iface, sc->kue_ed[KUE_ENDPT_RX],
	    USBD_EXCLUSIVE_USE, &sc->kue_ep[KUE_ENDPT_RX]);
	if (err) {
		printf("%s: open rx pipe failed: %s\n",
		    sc->kue_dev.dv_xname, usbd_errstr(err));
		return (EIO);
	}

	err = usbd_open_pipe(sc->kue_iface, sc->kue_ed[KUE_ENDPT_TX],
	    USBD_EXCLUSIVE_USE, &sc->kue_ep[KUE_ENDPT_TX]);
	if (err) {
		printf("%s: open tx pipe failed: %s\n",
		    sc->kue_dev.dv_xname, usbd_errstr(err));
		return (EIO);
	}

	/* Start up the receive pipe. */
	for (i = 0; i < KUE_RX_LIST_CNT; i++) {
		c = &sc->kue_cdata.kue_rx_chain[i];
		usbd_setup_xfer(c->kue_xfer, sc->kue_ep[KUE_ENDPT_RX],
		    c, c->kue_buf, KUE_BUFSZ,
		    USBD_SHORT_XFER_OK | USBD_NO_COPY, USBD_NO_TIMEOUT,
		    kue_rxeof);
		DPRINTFN(5,("%s: %s: start read\n", sc->kue_dev.dv_xname,
			    __func__));
		usbd_transfer(c->kue_xfer);
	}

	return (0);
}

int
kue_ioctl(struct ifnet *ifp, u_long command, caddr_t data)
{
	struct kue_softc	*sc = ifp->if_softc;
	int			s, error = 0;

	DPRINTFN(5,("%s: %s: enter\n", sc->kue_dev.dv_xname,__func__));

	if (usbd_is_dying(sc->kue_udev))
		return ENXIO;

#ifdef DIAGNOSTIC
	if (!curproc) {
		printf("%s: no proc!!\n", sc->kue_dev.dv_xname);
		return EIO;
	}
#endif

	s = splnet();

	switch(command) {
	case SIOCSIFADDR:
		ifp->if_flags |= IFF_UP;
		kue_init(sc);
		break;

	case SIOCSIFFLAGS:
		if (ifp->if_flags & IFF_UP) {
			if (ifp->if_flags & IFF_RUNNING &&
			    ifp->if_flags & IFF_PROMISC &&
			    !(sc->kue_if_flags & IFF_PROMISC)) {
				sc->kue_rxfilt |= KUE_RXFILT_PROMISC;
				kue_setword(sc, KUE_CMD_SET_PKT_FILTER,
				    sc->kue_rxfilt);
			} else if (ifp->if_flags & IFF_RUNNING &&
			    !(ifp->if_flags & IFF_PROMISC) &&
			    sc->kue_if_flags & IFF_PROMISC) {
				sc->kue_rxfilt &= ~KUE_RXFILT_PROMISC;
				kue_setword(sc, KUE_CMD_SET_PKT_FILTER,
				    sc->kue_rxfilt);
			} else if (!(ifp->if_flags & IFF_RUNNING))
				kue_init(sc);
		} else {
			if (ifp->if_flags & IFF_RUNNING)
				kue_stop(sc);
		}
		sc->kue_if_flags = ifp->if_flags;
		error = 0;
		break;

	default:
		error = ether_ioctl(ifp, &sc->arpcom, command, data);
	}

	if (error == ENETRESET) {
		if (ifp->if_flags & IFF_RUNNING)
			kue_setmulti(sc);
		error = 0;
	}

	splx(s);
	return (error);
}

void
kue_watchdog(struct ifnet *ifp)
{
	struct kue_softc	*sc = ifp->if_softc;
	struct kue_chain	*c;
	usbd_status		stat;
	int			s;

	DPRINTFN(5,("%s: %s: enter\n", sc->kue_dev.dv_xname,__func__));

	if (usbd_is_dying(sc->kue_udev))
		return;

	ifp->if_oerrors++;
	printf("%s: watchdog timeout\n", sc->kue_dev.dv_xname);

	s = splusb();
	c = &sc->kue_cdata.kue_tx_chain[0];
	usbd_get_xfer_status(c->kue_xfer, NULL, NULL, NULL, &stat);
	kue_txeof(c->kue_xfer, c, stat);

	if (ifq_empty(&ifp->if_snd) == 0)
		kue_start(ifp);
	splx(s);
}

/*
 * Stop the adapter and free any mbufs allocated to the
 * RX and TX lists.
 */
void
kue_stop(struct kue_softc *sc)
{
	usbd_status		err;
	struct ifnet		*ifp;
	int			i;

	DPRINTFN(5,("%s: %s: enter\n", sc->kue_dev.dv_xname,__func__));

	ifp = GET_IFP(sc);
	ifp->if_timer = 0;
	ifp->if_flags &= ~IFF_RUNNING;
	ifq_clr_oactive(&ifp->if_snd);

	/* Stop transfers. */
	if (sc->kue_ep[KUE_ENDPT_RX] != NULL) {
		err = usbd_close_pipe(sc->kue_ep[KUE_ENDPT_RX]);
		if (err) {
			printf("%s: close rx pipe failed: %s\n",
			    sc->kue_dev.dv_xname, usbd_errstr(err));
		}
		sc->kue_ep[KUE_ENDPT_RX] = NULL;
	}

	if (sc->kue_ep[KUE_ENDPT_TX] != NULL) {
		err = usbd_close_pipe(sc->kue_ep[KUE_ENDPT_TX]);
		if (err) {
			printf("%s: close tx pipe failed: %s\n",
			    sc->kue_dev.dv_xname, usbd_errstr(err));
		}
		sc->kue_ep[KUE_ENDPT_TX] = NULL;
	}

	if (sc->kue_ep[KUE_ENDPT_INTR] != NULL) {
		err = usbd_close_pipe(sc->kue_ep[KUE_ENDPT_INTR]);
		if (err) {
			printf("%s: close intr pipe failed: %s\n",
			    sc->kue_dev.dv_xname, usbd_errstr(err));
		}
		sc->kue_ep[KUE_ENDPT_INTR] = NULL;
	}

	/* Free RX resources. */
	for (i = 0; i < KUE_RX_LIST_CNT; i++) {
		if (sc->kue_cdata.kue_rx_chain[i].kue_mbuf != NULL) {
			m_freem(sc->kue_cdata.kue_rx_chain[i].kue_mbuf);
			sc->kue_cdata.kue_rx_chain[i].kue_mbuf = NULL;
		}
		if (sc->kue_cdata.kue_rx_chain[i].kue_xfer != NULL) {
			usbd_free_xfer(sc->kue_cdata.kue_rx_chain[i].kue_xfer);
			sc->kue_cdata.kue_rx_chain[i].kue_xfer = NULL;
		}
	}

	/* Free TX resources. */
	for (i = 0; i < KUE_TX_LIST_CNT; i++) {
		if (sc->kue_cdata.kue_tx_chain[i].kue_mbuf != NULL) {
			m_freem(sc->kue_cdata.kue_tx_chain[i].kue_mbuf);
			sc->kue_cdata.kue_tx_chain[i].kue_mbuf = NULL;
		}
		if (sc->kue_cdata.kue_tx_chain[i].kue_xfer != NULL) {
			usbd_free_xfer(sc->kue_cdata.kue_tx_chain[i].kue_xfer);
			sc->kue_cdata.kue_tx_chain[i].kue_xfer = NULL;
		}
	}
}
