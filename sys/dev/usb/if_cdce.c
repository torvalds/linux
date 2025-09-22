/*	$OpenBSD: if_cdce.c,v 1.83 2024/05/23 03:21:08 jsg Exp $ */

/*
 * Copyright (c) 1997, 1998, 1999, 2000-2003 Bill Paul <wpaul@windriver.com>
 * Copyright (c) 2003 Craig Boston
 * Copyright (c) 2004 Daniel Hartmeier
 * All rights reserved.
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
 * ARE DISCLAIMED.  IN NO EVENT SHALL Bill Paul, THE VOICES IN HIS HEAD OR
 * THE CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL,
 * EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO,
 * PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS;
 * OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY,
 * WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR
 * OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF
 * ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

/*
 * USB Communication Device Class (Ethernet Networking Control Model)
 * https://www.usb.org/sites/default/files/CDC1.2_WMC1.1_012011.zip
 *
 */

#include <bpfilter.h>

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/sockio.h>
#include <sys/mbuf.h>
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
#include <dev/usb/usbcdc.h>

#include <dev/usb/if_cdcereg.h>

#ifdef CDCE_DEBUG
#define DPRINTFN(n, x)	do { if (cdcedebug > (n)) printf x; } while (0)
int cdcedebug = 0;
#else
#define DPRINTFN(n, x)
#endif
#define DPRINTF(x)	DPRINTFN(0, x)

int	 cdce_tx_list_init(struct cdce_softc *);
int	 cdce_rx_list_init(struct cdce_softc *);
int	 cdce_newbuf(struct cdce_softc *, struct cdce_chain *,
		    struct mbuf *);
int	 cdce_encap(struct cdce_softc *, struct mbuf *, int);
void	 cdce_rxeof(struct usbd_xfer *, void *, usbd_status);
void	 cdce_txeof(struct usbd_xfer *, void *, usbd_status);
void	 cdce_start(struct ifnet *);
int	 cdce_ioctl(struct ifnet *, u_long, caddr_t);
void	 cdce_init(void *);
void	 cdce_watchdog(struct ifnet *);
void	 cdce_stop(struct cdce_softc *);
void	 cdce_intr(struct usbd_xfer *, void *, usbd_status);

const struct cdce_type cdce_devs[] = {
    {{ USB_VENDOR_ACERLABS, USB_PRODUCT_ACERLABS_M5632 }, 0 },
    {{ USB_VENDOR_PROLIFIC, USB_PRODUCT_PROLIFIC_PL2501 }, 0 },
    {{ USB_VENDOR_SHARP, USB_PRODUCT_SHARP_SL5500 }, CDCE_CRC32 },
    {{ USB_VENDOR_SHARP, USB_PRODUCT_SHARP_A300 }, CDCE_CRC32 },
    {{ USB_VENDOR_SHARP, USB_PRODUCT_SHARP_SL5600 }, CDCE_CRC32 },
    {{ USB_VENDOR_SHARP, USB_PRODUCT_SHARP_C700 }, CDCE_CRC32 },
    {{ USB_VENDOR_SHARP, USB_PRODUCT_SHARP_C750 }, CDCE_CRC32 },
    {{ USB_VENDOR_MOTOROLA2, USB_PRODUCT_MOTOROLA2_USBLAN }, CDCE_CRC32 },
    {{ USB_VENDOR_MOTOROLA2, USB_PRODUCT_MOTOROLA2_USBLAN2 }, CDCE_CRC32 },
    {{ USB_VENDOR_GMATE, USB_PRODUCT_GMATE_YP3X00 }, 0 },
    {{ USB_VENDOR_COMPAQ, USB_PRODUCT_COMPAQ_IPAQLINUX }, 0 },
    {{ USB_VENDOR_AMBIT, USB_PRODUCT_AMBIT_NTL_250 }, CDCE_SWAPUNION },
};
#define cdce_lookup(v, p) \
    ((const struct cdce_type *)usb_lookup(cdce_devs, v, p))

int cdce_match(struct device *, void *, void *);
void cdce_attach(struct device *, struct device *, void *);
int cdce_detach(struct device *, int);

struct cfdriver cdce_cd = {
	NULL, "cdce", DV_IFNET
};

const struct cfattach cdce_ca = {
	sizeof(struct cdce_softc), cdce_match, cdce_attach, cdce_detach
};

int
cdce_match(struct device *parent, void *match, void *aux)
{
	struct usb_attach_arg *uaa = aux;
	usb_interface_descriptor_t *id;

	if (uaa->iface == NULL)
		return (UMATCH_NONE);

	id = usbd_get_interface_descriptor(uaa->iface);
	if (id == NULL)
		return (UMATCH_NONE);

	if (cdce_lookup(uaa->vendor, uaa->product) != NULL)
		return (UMATCH_VENDOR_PRODUCT);

	if (id->bInterfaceClass == UICLASS_CDC &&
	    (id->bInterfaceSubClass ==
	    UISUBCLASS_ETHERNET_NETWORKING_CONTROL_MODEL ||
	    id->bInterfaceSubClass == UISUBCLASS_MOBILE_DIRECT_LINE_MODEL))
		return (UMATCH_IFACECLASS_GENERIC);

	return (UMATCH_NONE);
}

void
cdce_attach(struct device *parent, struct device *self, void *aux)
{
	struct cdce_softc		*sc = (struct cdce_softc *)self;
	struct usb_attach_arg		*uaa = aux;
	int				 s;
	struct ifnet			*ifp = GET_IFP(sc);
	struct usbd_device		*dev = uaa->device;
	const struct cdce_type		*t;
	usb_interface_descriptor_t	*id;
	usb_endpoint_descriptor_t	*ed;
	struct usb_cdc_union_descriptor	*ud;
	struct usb_cdc_ethernet_descriptor *ethd;
	usb_config_descriptor_t		*cd;
	const usb_descriptor_t		*desc;
	struct usbd_desc_iter		 iter;
	usb_string_descriptor_t		 eaddr_str;
	int				 i, j, numalts, len;
	int				 ctl_ifcno = -1;
	int				 data_ifcno = -1;

	sc->cdce_udev = uaa->device;
	sc->cdce_ctl_iface = uaa->iface;
	id = usbd_get_interface_descriptor(sc->cdce_ctl_iface);
	ctl_ifcno = id->bInterfaceNumber;

	t = cdce_lookup(uaa->vendor, uaa->product);
	if (t)
		sc->cdce_flags = t->cdce_flags;

	/* Get the data interface no. and capabilities */
	ethd = NULL;
	usbd_desc_iter_init(dev, &iter);
	desc = usbd_desc_iter_next(&iter);
	while (desc) {
		if (desc->bDescriptorType != UDESC_CS_INTERFACE) {
			desc = usbd_desc_iter_next(&iter);
			continue;
		}
		switch(desc->bDescriptorSubtype) {
		case UDESCSUB_CDC_UNION:
			ud = (struct usb_cdc_union_descriptor *)desc; 
			if ((sc->cdce_flags & CDCE_SWAPUNION) == 0 &&
			    ud->bMasterInterface == ctl_ifcno)
				data_ifcno = ud->bSlaveInterface[0];
			if ((sc->cdce_flags & CDCE_SWAPUNION) &&
			    ud->bSlaveInterface[0] == ctl_ifcno)
				data_ifcno = ud->bMasterInterface;
			break;
		case UDESCSUB_CDC_ENF:
			if (ethd) {
				printf("%s: ", sc->cdce_dev.dv_xname);
				printf("extra ethernet descriptor\n");
				return;
			}
			ethd = (struct usb_cdc_ethernet_descriptor *)desc;
			break;
		}
		desc = usbd_desc_iter_next(&iter);
	}

	if (data_ifcno == -1) {
		DPRINTF(("cdce_attach: no union interface\n"));
		sc->cdce_data_iface = sc->cdce_ctl_iface;
	} else {
		DPRINTF(("cdce_attach: union interface: ctl=%d, data=%d\n",
		    ctl_ifcno, data_ifcno));
		for (i = 0; i < uaa->nifaces; i++) {
			if (usbd_iface_claimed(sc->cdce_udev, i))
				continue;
			id = usbd_get_interface_descriptor(uaa->ifaces[i]);
			if (id != NULL && id->bInterfaceNumber == data_ifcno) {
				sc->cdce_data_iface = uaa->ifaces[i];
				usbd_claim_iface(sc->cdce_udev, i);
			}
		}
	}

	if (sc->cdce_data_iface == NULL) {
		printf("%s: no data interface\n", sc->cdce_dev.dv_xname);
		return;
	}

	id = usbd_get_interface_descriptor(sc->cdce_ctl_iface);
	sc->cdce_intr_no = -1;
	for (i = 0; i < id->bNumEndpoints && sc->cdce_intr_no == -1; i++) {
		ed = usbd_interface2endpoint_descriptor(sc->cdce_ctl_iface, i);
		if (!ed) {
			printf("%s: no descriptor for interrupt endpoint %d\n",
			    sc->cdce_dev.dv_xname, i);
			return;
		}
		if (UE_GET_DIR(ed->bEndpointAddress) == UE_DIR_IN &&
		    UE_GET_XFERTYPE(ed->bmAttributes) == UE_INTERRUPT) {
			sc->cdce_intr_no = ed->bEndpointAddress;
			sc->cdce_intr_size = sizeof(sc->cdce_intr_buf);
		}
	}

	id = usbd_get_interface_descriptor(sc->cdce_data_iface);
	cd = usbd_get_config_descriptor(sc->cdce_udev);
	numalts = usbd_get_no_alts(cd, id->bInterfaceNumber);

	for (j = 0; j < numalts; j++) {
		if (usbd_set_interface(sc->cdce_data_iface, j)) {
			printf("%s: interface alternate setting %d failed\n", 
			    sc->cdce_dev.dv_xname, j);
			return;
		} 
		/* Find endpoints. */
		id = usbd_get_interface_descriptor(sc->cdce_data_iface);
		sc->cdce_bulkin_no = sc->cdce_bulkout_no = -1;
		for (i = 0; i < id->bNumEndpoints; i++) {
			ed = usbd_interface2endpoint_descriptor(
			    sc->cdce_data_iface, i);
			if (!ed) {
				printf("%s: no descriptor for bulk endpoint "
				    "%d\n", sc->cdce_dev.dv_xname, i);
				return;
			}
			if (UE_GET_DIR(ed->bEndpointAddress) == UE_DIR_IN &&
			    UE_GET_XFERTYPE(ed->bmAttributes) == UE_BULK) {
				sc->cdce_bulkin_no = ed->bEndpointAddress;
			} else if (
			    UE_GET_DIR(ed->bEndpointAddress) == UE_DIR_OUT &&
			    UE_GET_XFERTYPE(ed->bmAttributes) == UE_BULK) {
				sc->cdce_bulkout_no = ed->bEndpointAddress;
			}
#ifdef CDCE_DEBUG
			else if (
			    UE_GET_DIR(ed->bEndpointAddress) != UE_DIR_IN &&
			    UE_GET_XFERTYPE(ed->bmAttributes) != UE_INTERRUPT) {
				printf("%s: unexpected endpoint, ep=%x attr=%x"
				    "\n", sc->cdce_dev.dv_xname,
				    ed->bEndpointAddress, ed->bmAttributes);
			}
#endif
		}
		if ((sc->cdce_bulkin_no != -1) && (sc->cdce_bulkout_no != -1)) {
			DPRINTF(("cdce_attach: intr=0x%x, in=0x%x, out=0x%x\n",
			    sc->cdce_intr_no, sc->cdce_bulkin_no,
			    sc->cdce_bulkout_no));
			goto found;
		}
	}
	
	if (sc->cdce_bulkin_no == -1) {
		printf("%s: could not find data bulk in\n",
		    sc->cdce_dev.dv_xname);
		return;
	}
	if (sc->cdce_bulkout_no == -1 ) {
		printf("%s: could not find data bulk out\n",
		    sc->cdce_dev.dv_xname);
		return;
	}

found:
	s = splnet();

	if (!ethd || usbd_get_string_desc(sc->cdce_udev, ethd->iMacAddress, 0,
	    &eaddr_str, &len)) {
		ether_fakeaddr(ifp);
	} else {
		for (i = 0; i < ETHER_ADDR_LEN * 2; i++) {
			int c = UGETW(eaddr_str.bString[i]);

			if ('0' <= c && c <= '9')
				c -= '0';
			else if ('A' <= c && c <= 'F')
				c -= 'A' - 10;
			else if ('a' <= c && c <= 'f')
				c -= 'a' - 10;
			c &= 0xf;
			if (i % 2 == 0)
				c <<= 4;
			sc->cdce_arpcom.ac_enaddr[i / 2] |= c;
		}
	}

	printf("%s: address %s\n", sc->cdce_dev.dv_xname,
	    ether_sprintf(sc->cdce_arpcom.ac_enaddr));

	ifp->if_softc = sc;
	ifp->if_flags = IFF_BROADCAST | IFF_SIMPLEX | IFF_MULTICAST;
	ifp->if_ioctl = cdce_ioctl;
	ifp->if_start = cdce_start;
	ifp->if_watchdog = cdce_watchdog;
	strlcpy(ifp->if_xname, sc->cdce_dev.dv_xname, IFNAMSIZ);

	if_attach(ifp);
	ether_ifattach(ifp);

	sc->cdce_attached = 1;
	splx(s);
}

int
cdce_detach(struct device *self, int flags)
{
	struct cdce_softc	*sc = (struct cdce_softc *)self;	
	struct ifnet		*ifp = GET_IFP(sc);
	int			 s;

	if (!sc->cdce_attached)
		return (0);

	s = splusb();

	if (ifp->if_flags & IFF_RUNNING)
		cdce_stop(sc);

	if (ifp->if_softc != NULL) {
		ether_ifdetach(ifp);
		if_detach(ifp);
	}

	sc->cdce_attached = 0;
	splx(s);

	return (0);
}

void
cdce_start(struct ifnet *ifp)
{
	struct cdce_softc	*sc = ifp->if_softc;
	struct mbuf		*m_head = NULL;

	if (usbd_is_dying(sc->cdce_udev) || ifq_is_oactive(&ifp->if_snd))
		return;

	m_head = ifq_dequeue(&ifp->if_snd);
	if (m_head == NULL)
		return;

	if (cdce_encap(sc, m_head, 0)) {
		m_freem(m_head);
		ifq_set_oactive(&ifp->if_snd);
		return;
	}

#if NBPFILTER > 0
	if (ifp->if_bpf)
		bpf_mtap(ifp->if_bpf, m_head, BPF_DIRECTION_OUT);
#endif

	ifq_set_oactive(&ifp->if_snd);

	ifp->if_timer = 6;
}

int
cdce_encap(struct cdce_softc *sc, struct mbuf *m, int idx)
{
	struct cdce_chain	*c;
	usbd_status		 err;
	int			 extra = 0;

	c = &sc->cdce_cdata.cdce_tx_chain[idx];

	m_copydata(m, 0, m->m_pkthdr.len, c->cdce_buf);
	if (sc->cdce_flags & CDCE_CRC32) {
		/* Some devices want a 32-bit CRC appended to every frame */
		u_int32_t crc;

		crc = ether_crc32_le(c->cdce_buf, m->m_pkthdr.len) ^ ~0U;
		bcopy(&crc, c->cdce_buf + m->m_pkthdr.len, 4);
		extra = 4;
	}
	c->cdce_mbuf = m;

	usbd_setup_xfer(c->cdce_xfer, sc->cdce_bulkout_pipe, c, c->cdce_buf,
	    m->m_pkthdr.len + extra, USBD_FORCE_SHORT_XFER | USBD_NO_COPY,
	    10000, cdce_txeof);
	err = usbd_transfer(c->cdce_xfer);
	if (err != USBD_IN_PROGRESS) {
		c->cdce_mbuf = NULL;
		cdce_stop(sc);
		return (EIO);
	}

	sc->cdce_cdata.cdce_tx_cnt++;

	return (0);
}

void
cdce_stop(struct cdce_softc *sc)
{
	usbd_status	 err;
	struct ifnet	*ifp = GET_IFP(sc);
	int		 i;

	ifp->if_timer = 0;
	ifp->if_flags &= ~IFF_RUNNING;
	ifq_clr_oactive(&ifp->if_snd);

	if (sc->cdce_bulkin_pipe != NULL) {
		err = usbd_close_pipe(sc->cdce_bulkin_pipe);
		if (err)
			printf("%s: close rx pipe failed: %s\n",
			    sc->cdce_dev.dv_xname, usbd_errstr(err));
		sc->cdce_bulkin_pipe = NULL;
	}

	if (sc->cdce_bulkout_pipe != NULL) {
		err = usbd_close_pipe(sc->cdce_bulkout_pipe);
		if (err)
			printf("%s: close tx pipe failed: %s\n",
			    sc->cdce_dev.dv_xname, usbd_errstr(err));
		sc->cdce_bulkout_pipe = NULL;
	}

	if (sc->cdce_intr_pipe != NULL) {
		err = usbd_close_pipe(sc->cdce_intr_pipe);
		if (err)
			printf("%s: close interrupt pipe failed: %s\n",
			    sc->cdce_dev.dv_xname, usbd_errstr(err));
		sc->cdce_intr_pipe = NULL;
	}

	for (i = 0; i < CDCE_RX_LIST_CNT; i++) {
		if (sc->cdce_cdata.cdce_rx_chain[i].cdce_mbuf != NULL) {
			m_freem(sc->cdce_cdata.cdce_rx_chain[i].cdce_mbuf);
			sc->cdce_cdata.cdce_rx_chain[i].cdce_mbuf = NULL;
		}
		if (sc->cdce_cdata.cdce_rx_chain[i].cdce_xfer != NULL) {
			usbd_free_xfer(sc->cdce_cdata.cdce_rx_chain[i].cdce_xfer);
			sc->cdce_cdata.cdce_rx_chain[i].cdce_xfer = NULL;
		}
	}

	for (i = 0; i < CDCE_TX_LIST_CNT; i++) {
		if (sc->cdce_cdata.cdce_tx_chain[i].cdce_mbuf != NULL) {
			m_freem(sc->cdce_cdata.cdce_tx_chain[i].cdce_mbuf);
			sc->cdce_cdata.cdce_tx_chain[i].cdce_mbuf = NULL;
		}
		if (sc->cdce_cdata.cdce_tx_chain[i].cdce_xfer != NULL) {
			usbd_free_xfer(
			    sc->cdce_cdata.cdce_tx_chain[i].cdce_xfer);
			sc->cdce_cdata.cdce_tx_chain[i].cdce_xfer = NULL;
		}
	}
}

int
cdce_ioctl(struct ifnet *ifp, u_long command, caddr_t data)
{
	struct cdce_softc	*sc = ifp->if_softc;
	int			 s, error = 0;

	if (usbd_is_dying(sc->cdce_udev))
		return ENXIO;

	s = splnet();

	switch(command) {
	case SIOCSIFADDR:
		ifp->if_flags |= IFF_UP;
		if (!(ifp->if_flags & IFF_RUNNING))
			cdce_init(sc);
		break;

	case SIOCSIFFLAGS:
		if (ifp->if_flags & IFF_UP) {
			if (ifp->if_flags & IFF_RUNNING)
				error = ENETRESET;
			else
				cdce_init(sc);
		} else {
			if (ifp->if_flags & IFF_RUNNING)
				cdce_stop(sc);
		}
		break;

	default:
		error = ether_ioctl(ifp, &sc->cdce_arpcom, command, data);
		break;
	}

	if (error == ENETRESET)
		error = 0;

	splx(s);
	return (error);
}

void
cdce_watchdog(struct ifnet *ifp)
{
	struct cdce_softc	*sc = ifp->if_softc;

	if (usbd_is_dying(sc->cdce_udev))
		return;

	ifp->if_oerrors++;
	printf("%s: watchdog timeout\n", sc->cdce_dev.dv_xname);
}

void
cdce_init(void *xsc)
{
	struct cdce_softc	*sc = xsc;
	struct ifnet		*ifp = GET_IFP(sc);
	struct cdce_chain	*c;
	usbd_status		 err;
	int			 s, i;

	s = splnet();

	if (sc->cdce_intr_no != -1 && sc->cdce_intr_pipe == NULL) {
		DPRINTFN(1, ("cdce_init: establish interrupt pipe\n"));
		err = usbd_open_pipe_intr(sc->cdce_ctl_iface, sc->cdce_intr_no,
		    USBD_SHORT_XFER_OK, &sc->cdce_intr_pipe, sc,
		    &sc->cdce_intr_buf, sc->cdce_intr_size, cdce_intr,
		    USBD_DEFAULT_INTERVAL);
		if (err) {
			printf("%s: open interrupt pipe failed: %s\n",
			    sc->cdce_dev.dv_xname, usbd_errstr(err));
			splx(s);
			return;
		}
	}

	if (cdce_tx_list_init(sc) == ENOBUFS) {
		printf("%s: tx list init failed\n", sc->cdce_dev.dv_xname);
		splx(s);
		return;
	}

	if (cdce_rx_list_init(sc) == ENOBUFS) {
		printf("%s: rx list init failed\n", sc->cdce_dev.dv_xname);
		splx(s);
		return;
	}

	/* Maybe set multicast / broadcast here??? */

	err = usbd_open_pipe(sc->cdce_data_iface, sc->cdce_bulkin_no,
	    USBD_EXCLUSIVE_USE, &sc->cdce_bulkin_pipe);
	if (err) {
		printf("%s: open rx pipe failed: %s\n", sc->cdce_dev.dv_xname,
		    usbd_errstr(err));
		splx(s);
		return;
	}

	err = usbd_open_pipe(sc->cdce_data_iface, sc->cdce_bulkout_no,
	    USBD_EXCLUSIVE_USE, &sc->cdce_bulkout_pipe);
	if (err) {
		printf("%s: open tx pipe failed: %s\n", sc->cdce_dev.dv_xname,
		    usbd_errstr(err));
		splx(s);
		return;
	}

	for (i = 0; i < CDCE_RX_LIST_CNT; i++) {
		c = &sc->cdce_cdata.cdce_rx_chain[i];
		usbd_setup_xfer(c->cdce_xfer, sc->cdce_bulkin_pipe, c,
		    c->cdce_buf, CDCE_BUFSZ, USBD_SHORT_XFER_OK | USBD_NO_COPY,
		    USBD_NO_TIMEOUT, cdce_rxeof);
		usbd_transfer(c->cdce_xfer);
	}

	ifp->if_flags |= IFF_RUNNING;
	ifq_clr_oactive(&ifp->if_snd);

	splx(s);
}

int
cdce_newbuf(struct cdce_softc *sc, struct cdce_chain *c, struct mbuf *m)
{
	struct mbuf	*m_new = NULL;

	if (m == NULL) {
		MGETHDR(m_new, M_DONTWAIT, MT_DATA);
		if (m_new == NULL) {
			printf("%s: no memory for rx list "
			    "-- packet dropped!\n", sc->cdce_dev.dv_xname);
			return (ENOBUFS);
		}
		MCLGET(m_new, M_DONTWAIT);
		if (!(m_new->m_flags & M_EXT)) {
			printf("%s: no memory for rx list "
			    "-- packet dropped!\n", sc->cdce_dev.dv_xname);
			m_freem(m_new);
			return (ENOBUFS);
		}
		m_new->m_len = m_new->m_pkthdr.len = MCLBYTES;
	} else {
		m_new = m;
		m_new->m_len = m_new->m_pkthdr.len = MCLBYTES;
		m_new->m_data = m_new->m_ext.ext_buf;
	}

	m_adj(m_new, ETHER_ALIGN);
	c->cdce_mbuf = m_new;
	return (0);
}

int
cdce_rx_list_init(struct cdce_softc *sc)
{
	struct cdce_cdata	*cd;
	struct cdce_chain	*c;
	int			 i;

	cd = &sc->cdce_cdata;
	for (i = 0; i < CDCE_RX_LIST_CNT; i++) {
		c = &cd->cdce_rx_chain[i];
		c->cdce_sc = sc;
		c->cdce_idx = i;
		if (cdce_newbuf(sc, c, NULL) == ENOBUFS)
			return (ENOBUFS);
		if (c->cdce_xfer == NULL) {
			c->cdce_xfer = usbd_alloc_xfer(sc->cdce_udev);
			if (c->cdce_xfer == NULL)
				return (ENOBUFS);
			c->cdce_buf = usbd_alloc_buffer(c->cdce_xfer,
			    CDCE_BUFSZ);
			if (c->cdce_buf == NULL)
				return (ENOBUFS);
		}
	}

	return (0);
}

int
cdce_tx_list_init(struct cdce_softc *sc)
{
	struct cdce_cdata	*cd;
	struct cdce_chain	*c;
	int			 i;

	cd = &sc->cdce_cdata;
	for (i = 0; i < CDCE_TX_LIST_CNT; i++) {
		c = &cd->cdce_tx_chain[i];
		c->cdce_sc = sc;
		c->cdce_idx = i;
		c->cdce_mbuf = NULL;
		if (c->cdce_xfer == NULL) {
			c->cdce_xfer = usbd_alloc_xfer(sc->cdce_udev);
			if (c->cdce_xfer == NULL)
				return (ENOBUFS);
			c->cdce_buf = usbd_alloc_buffer(c->cdce_xfer,
			    CDCE_BUFSZ);
			if (c->cdce_buf == NULL)
				return (ENOBUFS);
		}
	}

	return (0);
}

void
cdce_rxeof(struct usbd_xfer *xfer, void *priv, usbd_status status)
{
	struct cdce_chain	*c = priv;
	struct cdce_softc	*sc = c->cdce_sc;
	struct ifnet		*ifp = GET_IFP(sc);
	struct mbuf		*m;
	struct mbuf_list	 ml = MBUF_LIST_INITIALIZER();
	int			 total_len = 0;
	int			 s;

	if (usbd_is_dying(sc->cdce_udev) || !(ifp->if_flags & IFF_RUNNING))
		return;

	if (status != USBD_NORMAL_COMPLETION) {
		if (status == USBD_NOT_STARTED || status == USBD_CANCELLED)
			return;
		if (sc->cdce_rxeof_errors == 0)
			printf("%s: usb error on rx: %s\n",
			    sc->cdce_dev.dv_xname, usbd_errstr(status));
		if (status == USBD_STALLED)
			usbd_clear_endpoint_stall_async(sc->cdce_bulkin_pipe);
		DELAY(sc->cdce_rxeof_errors * 10000);
		if (sc->cdce_rxeof_errors++ > 10) {
			printf("%s: too many errors, disabling\n",
			    sc->cdce_dev.dv_xname);
			usbd_deactivate(sc->cdce_udev);
			return;
		}
		goto done;
	}

	sc->cdce_rxeof_errors = 0;

	usbd_get_xfer_status(xfer, NULL, NULL, &total_len, NULL);
	if (sc->cdce_flags & CDCE_CRC32)
		total_len -= 4;	/* Strip off added CRC */
	if (total_len <= 1)
		goto done;

	m = c->cdce_mbuf;
	memcpy(mtod(m, char *), c->cdce_buf, total_len);

	if (total_len < sizeof(struct ether_header)) {
		ifp->if_ierrors++;
		goto done;
	}

	m->m_pkthdr.len = m->m_len = total_len;
	ml_enqueue(&ml, m);

	if (cdce_newbuf(sc, c, NULL) == ENOBUFS) {
		ifp->if_ierrors++;
		goto done;
	}

	s = splnet();
	if_input(ifp, &ml);
	splx(s);

done:
	/* Setup new transfer. */
	usbd_setup_xfer(c->cdce_xfer, sc->cdce_bulkin_pipe, c, c->cdce_buf,
	    CDCE_BUFSZ, USBD_SHORT_XFER_OK | USBD_NO_COPY, USBD_NO_TIMEOUT,
	    cdce_rxeof);
	usbd_transfer(c->cdce_xfer);
}

void
cdce_txeof(struct usbd_xfer *xfer, void *priv, usbd_status status)
{
	struct cdce_chain	*c = priv;
	struct cdce_softc	*sc = c->cdce_sc;
	struct ifnet		*ifp = GET_IFP(sc);
	usbd_status		 err;
	int			 s;

	if (usbd_is_dying(sc->cdce_udev))
		return;

	s = splnet();

	ifp->if_timer = 0;
	ifq_clr_oactive(&ifp->if_snd);

	if (status != USBD_NORMAL_COMPLETION) {
		if (status == USBD_NOT_STARTED || status == USBD_CANCELLED) {
			splx(s);
			return;
		}
		ifp->if_oerrors++;
		printf("%s: usb error on tx: %s\n", sc->cdce_dev.dv_xname,
		    usbd_errstr(status));
		if (status == USBD_STALLED)
			usbd_clear_endpoint_stall_async(sc->cdce_bulkout_pipe);
		splx(s);
		return;
	}

	usbd_get_xfer_status(c->cdce_xfer, NULL, NULL, NULL, &err);

	if (c->cdce_mbuf != NULL) {
		m_freem(c->cdce_mbuf);
		c->cdce_mbuf = NULL;
	}

	if (err)
		ifp->if_oerrors++;

	if (ifq_empty(&ifp->if_snd) == 0)
		cdce_start(ifp);

	splx(s);
}

void
cdce_intr(struct usbd_xfer *xfer, void *addr, usbd_status status)
{
	struct cdce_softc	*sc = addr;
	struct usb_cdc_notification *buf = &sc->cdce_intr_buf;
	struct usb_cdc_connection_speed	*speed;
	u_int32_t		 count;

	if (status == USBD_CANCELLED)
		return;

	if (status != USBD_NORMAL_COMPLETION) {
		DPRINTFN(2, ("cdce_intr: status=%d\n", status));
		if (status == USBD_STALLED)
			usbd_clear_endpoint_stall_async(sc->cdce_intr_pipe);
		return;
	}

	usbd_get_xfer_status(xfer, NULL, NULL, &count, NULL);

	if (buf->bmRequestType == UCDC_NOTIFICATION) {
		switch (buf->bNotification) {
		case UCDC_N_NETWORK_CONNECTION:
			DPRINTFN(1, ("cdce_intr: network %s\n",
			    UGETW(buf->wValue) ? "connected" : "disconnected"));
			break;
		case UCDC_N_CONNECTION_SPEED_CHANGE:
			speed = (struct usb_cdc_connection_speed *)&buf->data;
			DPRINTFN(1, ("cdce_intr: up=%d, down=%d\n",
			    UGETDW(speed->dwUSBitRate),
			    UGETDW(speed->dwDSBitRate)));
			break;
		default:
			DPRINTF(("cdce_intr: bNotification 0x%x\n",
			    buf->bNotification));
		}
	}
#ifdef CDCE_DEBUG
	else {
		printf("cdce_intr: bmRequestType=%d ", buf->bmRequestType);
		printf("wValue=%d wIndex=%d wLength=%d\n", UGETW(buf->wValue),
		    UGETW(buf->wIndex), UGETW(buf->wLength));
	}
#endif
}
