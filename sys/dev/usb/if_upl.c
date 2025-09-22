/*	$OpenBSD: if_upl.c,v 1.80 2024/05/23 03:21:09 jsg Exp $ */
/*	$NetBSD: if_upl.c,v 1.19 2002/07/11 21:14:26 augustss Exp $	*/
/*
 * Copyright (c) 2000 The NetBSD Foundation, Inc.
 * All rights reserved.
 *
 * This code is derived from software contributed to The NetBSD Foundation
 * by Lennart Augustsson (lennart@augustsson.net) at
 * Carlstedt Research & Technology.
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
 * Prolific PL2301/PL2302 driver
 */

#include "bpfilter.h"

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/timeout.h>
#include <sys/sockio.h>
#include <sys/mbuf.h>

#include <sys/device.h>

#include <net/if.h>
#include <net/if_types.h>

#if NBPFILTER > 0
#include <net/bpf.h>
#endif

#include <netinet/in.h>
#include <netinet/if_ether.h>

#include <dev/usb/usb.h>
#include <dev/usb/usbdi.h>
#include <dev/usb/usbdevs.h>

/*
 * 7  6  5  4  3  2  1  0
 * tx rx 1  0
 * 1110 0000 rxdata
 * 1010 0000 idle
 * 0010 0000 tx over
 * 0110      tx over + rxd
 */

#define UPL_RXDATA		0x40
#define UPL_TXOK		0x80

#define UPL_INTR_PKTLEN		1

#define UPL_CONFIG_NO		1
#define UPL_IFACE_IDX		0

/***/

#define UPL_INTR_INTERVAL	20

#define UPL_BUFSZ		1024

#define UPL_RX_FRAMES		1
#define UPL_TX_FRAMES		1

#define UPL_RX_LIST_CNT		1
#define UPL_TX_LIST_CNT		1

#define UPL_ENDPT_RX		0x0
#define UPL_ENDPT_TX		0x1
#define UPL_ENDPT_INTR		0x2
#define UPL_ENDPT_MAX		0x3

struct upl_softc;

struct upl_chain {
	struct upl_softc	*upl_sc;
	struct usbd_xfer	*upl_xfer;
	char			*upl_buf;
	struct mbuf		*upl_mbuf;
	int			upl_idx;
};

struct upl_cdata {
	struct upl_chain	upl_tx_chain[UPL_TX_LIST_CNT];
	struct upl_chain	upl_rx_chain[UPL_RX_LIST_CNT];
	int			upl_tx_prod;
	int			upl_tx_cons;
	int			upl_tx_cnt;
	int			upl_rx_prod;
};

struct upl_softc {
	struct device		sc_dev;

	struct ifnet		sc_if;
	struct timeout		sc_stat_ch;

	struct usbd_device	*sc_udev;
	struct usbd_interface	*sc_iface;
	int			sc_ed[UPL_ENDPT_MAX];
	struct usbd_pipe	*sc_ep[UPL_ENDPT_MAX];
	struct upl_cdata	sc_cdata;

	uByte			sc_ibuf;

	u_int			sc_rx_errs;
	struct timeval		sc_rx_notice;
	u_int			sc_intr_errs;
};

#ifdef UPL_DEBUG
#define DPRINTF(x)	do { if (upldebug) printf x; } while (0)
#define DPRINTFN(n,x)	do { if (upldebug >= (n)) printf x; } while (0)
int	upldebug = 0;
#else
#define DPRINTF(x)
#define DPRINTFN(n,x)
#endif

/*
 * Various supported device vendors/products.
 */
struct usb_devno upl_devs[] = {
	{ USB_VENDOR_PROLIFIC, USB_PRODUCT_PROLIFIC_PL2301 },
	{ USB_VENDOR_PROLIFIC, USB_PRODUCT_PROLIFIC_PL2302 }
};

int upl_match(struct device *, void *, void *);
void upl_attach(struct device *, struct device *, void *);
int upl_detach(struct device *, int);

struct cfdriver upl_cd = {
	NULL, "upl", DV_IFNET
};

const struct cfattach upl_ca = {
	sizeof(struct upl_softc), upl_match, upl_attach, upl_detach
};

int upl_openpipes(struct upl_softc *);
int upl_tx_list_init(struct upl_softc *);
int upl_rx_list_init(struct upl_softc *);
int upl_newbuf(struct upl_softc *, struct upl_chain *, struct mbuf *);
int upl_send(struct upl_softc *, struct mbuf *, int);
void upl_intr(struct usbd_xfer *, void *, usbd_status);
void upl_rxeof(struct usbd_xfer *, void *, usbd_status);
void upl_txeof(struct usbd_xfer *, void *, usbd_status);
void upl_start(struct ifnet *);
int upl_ioctl(struct ifnet *, u_long, caddr_t);
void upl_init(void *);
void upl_stop(struct upl_softc *);
void upl_watchdog(struct ifnet *);

int upl_output(struct ifnet *, struct mbuf *, struct sockaddr *,
		      struct rtentry *);

/*
 * Probe for a Prolific chip.
 */
int
upl_match(struct device *parent, void *match, void *aux)
{
	struct usb_attach_arg		*uaa = aux;

	if (uaa->iface == NULL || uaa->configno != UPL_CONFIG_NO)
		return (UMATCH_NONE);

	return (usb_lookup(upl_devs, uaa->vendor, uaa->product) != NULL ?
	    UMATCH_VENDOR_PRODUCT : UMATCH_NONE);
}

void
upl_attach(struct device *parent, struct device *self, void *aux)
{
	struct upl_softc	*sc = (struct upl_softc *)self;
	struct usb_attach_arg	*uaa = aux;
	int			s;
	struct usbd_device	*dev = uaa->device;
	struct usbd_interface	*iface;
	usbd_status		err;
	struct ifnet		*ifp;
	usb_interface_descriptor_t	*id;
	usb_endpoint_descriptor_t	*ed;
	int			i;

	DPRINTFN(5,(" : upl_attach: sc=%p, dev=%p", sc, dev));

	sc->sc_udev = dev;

	err = usbd_device2interface_handle(dev, UPL_IFACE_IDX, &iface);
	if (err) {
		printf("%s: getting interface handle failed\n",
		    sc->sc_dev.dv_xname);
		return;
	}

	sc->sc_iface = iface;
	id = usbd_get_interface_descriptor(iface);

	/* Find endpoints. */
	for (i = 0; i < id->bNumEndpoints; i++) {
		ed = usbd_interface2endpoint_descriptor(iface, i);
		if (ed == NULL) {
			printf("%s: couldn't get ep %d\n",
			    sc->sc_dev.dv_xname, i);
			return;
		}
		if (UE_GET_DIR(ed->bEndpointAddress) == UE_DIR_IN &&
		    UE_GET_XFERTYPE(ed->bmAttributes) == UE_BULK) {
			sc->sc_ed[UPL_ENDPT_RX] = ed->bEndpointAddress;
		} else if (UE_GET_DIR(ed->bEndpointAddress) == UE_DIR_OUT &&
			   UE_GET_XFERTYPE(ed->bmAttributes) == UE_BULK) {
			sc->sc_ed[UPL_ENDPT_TX] = ed->bEndpointAddress;
		} else if (UE_GET_DIR(ed->bEndpointAddress) == UE_DIR_IN &&
			   UE_GET_XFERTYPE(ed->bmAttributes) == UE_INTERRUPT) {
			sc->sc_ed[UPL_ENDPT_INTR] = ed->bEndpointAddress;
		}
	}

	if (sc->sc_ed[UPL_ENDPT_RX] == 0 || sc->sc_ed[UPL_ENDPT_TX] == 0 ||
	    sc->sc_ed[UPL_ENDPT_INTR] == 0) {
		printf("%s: missing endpoint\n", sc->sc_dev.dv_xname);
		return;
	}

	s = splnet();

	/* Initialize interface info.*/
	ifp = &sc->sc_if;
	ifp->if_softc = sc;
	ifp->if_mtu = UPL_BUFSZ;
	ifp->if_hardmtu = UPL_BUFSZ;
	ifp->if_flags = IFF_POINTOPOINT | IFF_SIMPLEX;
	ifp->if_ioctl = upl_ioctl;
	ifp->if_start = upl_start;
	ifp->if_watchdog = upl_watchdog;
	strlcpy(ifp->if_xname, sc->sc_dev.dv_xname, IFNAMSIZ);

	ifp->if_type = IFT_OTHER;
	ifp->if_addrlen = 0;
	ifp->if_hdrlen = 0;
	ifp->if_output = upl_output;
	ifp->if_baudrate = IF_Mbps(12);

	/* Attach the interface. */
	if_attach(ifp);
	if_alloc_sadl(ifp);

	splx(s);
}

int
upl_detach(struct device *self, int flags)
{
	struct upl_softc	*sc = (struct upl_softc *)self;
	struct ifnet		*ifp = &sc->sc_if;
	int			s;

	DPRINTFN(2,("%s: %s: enter\n", sc->sc_dev.dv_xname, __func__));

	s = splusb();

	if (ifp->if_flags & IFF_RUNNING)
		upl_stop(sc);

	if (ifp->if_softc != NULL)
		if_detach(ifp);

#ifdef DIAGNOSTIC
	if (sc->sc_ep[UPL_ENDPT_TX] != NULL ||
	    sc->sc_ep[UPL_ENDPT_RX] != NULL ||
	    sc->sc_ep[UPL_ENDPT_INTR] != NULL)
		printf("%s: detach has active endpoints\n",
		       sc->sc_dev.dv_xname);
#endif

	splx(s);

	return (0);
}

/*
 * Initialize an RX descriptor and attach an MBUF cluster.
 */
int
upl_newbuf(struct upl_softc *sc, struct upl_chain *c, struct mbuf *m)
{
	struct mbuf		*m_new = NULL;

	DPRINTFN(8,("%s: %s: enter\n", sc->sc_dev.dv_xname, __func__));

	if (m == NULL) {
		MGETHDR(m_new, M_DONTWAIT, MT_DATA);
		if (m_new == NULL) {
			printf("%s: no memory for rx list "
			    "-- packet dropped!\n", sc->sc_dev.dv_xname);
			return (ENOBUFS);
		}

		MCLGET(m_new, M_DONTWAIT);
		if (!(m_new->m_flags & M_EXT)) {
			printf("%s: no memory for rx list "
			    "-- packet dropped!\n", sc->sc_dev.dv_xname);
			m_freem(m_new);
			return (ENOBUFS);
		}
		m_new->m_len = m_new->m_pkthdr.len = MCLBYTES;
	} else {
		m_new = m;
		m_new->m_len = m_new->m_pkthdr.len = MCLBYTES;
		m_new->m_data = m_new->m_ext.ext_buf;
	}

	c->upl_mbuf = m_new;

	return (0);
}

int
upl_rx_list_init(struct upl_softc *sc)
{
	struct upl_cdata	*cd;
	struct upl_chain	*c;
	int			i;

	DPRINTFN(5,("%s: %s: enter\n", sc->sc_dev.dv_xname, __func__));

	cd = &sc->sc_cdata;
	for (i = 0; i < UPL_RX_LIST_CNT; i++) {
		c = &cd->upl_rx_chain[i];
		c->upl_sc = sc;
		c->upl_idx = i;
		if (upl_newbuf(sc, c, NULL) == ENOBUFS)
			return (ENOBUFS);
		if (c->upl_xfer == NULL) {
			c->upl_xfer = usbd_alloc_xfer(sc->sc_udev);
			if (c->upl_xfer == NULL)
				return (ENOBUFS);
			c->upl_buf = usbd_alloc_buffer(c->upl_xfer, UPL_BUFSZ);
			if (c->upl_buf == NULL) {
				usbd_free_xfer(c->upl_xfer);
				return (ENOBUFS);
			}
		}
	}

	return (0);
}

int
upl_tx_list_init(struct upl_softc *sc)
{
	struct upl_cdata	*cd;
	struct upl_chain	*c;
	int			i;

	DPRINTFN(5,("%s: %s: enter\n", sc->sc_dev.dv_xname, __func__));

	cd = &sc->sc_cdata;
	for (i = 0; i < UPL_TX_LIST_CNT; i++) {
		c = &cd->upl_tx_chain[i];
		c->upl_sc = sc;
		c->upl_idx = i;
		c->upl_mbuf = NULL;
		if (c->upl_xfer == NULL) {
			c->upl_xfer = usbd_alloc_xfer(sc->sc_udev);
			if (c->upl_xfer == NULL)
				return (ENOBUFS);
			c->upl_buf = usbd_alloc_buffer(c->upl_xfer, UPL_BUFSZ);
			if (c->upl_buf == NULL) {
				usbd_free_xfer(c->upl_xfer);
				return (ENOBUFS);
			}
		}
	}

	return (0);
}

/*
 * A frame has been uploaded: pass the resulting mbuf chain up to
 * the higher level protocols.
 */
void
upl_rxeof(struct usbd_xfer *xfer, void *priv, usbd_status status)
{
	struct upl_chain	*c = priv;
	struct upl_softc	*sc = c->upl_sc;
	struct ifnet		*ifp = &sc->sc_if;
	struct mbuf_list	ml = MBUF_LIST_INITIALIZER();
	struct mbuf		*m;
	int			total_len = 0;
	int			s;

	if (usbd_is_dying(sc->sc_udev))
		return;

	if (!(ifp->if_flags & IFF_RUNNING))
		return;

	if (status != USBD_NORMAL_COMPLETION) {
		if (status == USBD_NOT_STARTED || status == USBD_CANCELLED)
			return;
		sc->sc_rx_errs++;
		if (usbd_ratecheck(&sc->sc_rx_notice)) {
			printf("%s: %u usb errors on rx: %s\n",
			    sc->sc_dev.dv_xname, sc->sc_rx_errs,
			    usbd_errstr(status));
			sc->sc_rx_errs = 0;
		}
		if (status == USBD_STALLED)
			usbd_clear_endpoint_stall_async(sc->sc_ep[UPL_ENDPT_RX]);
		goto done;
	}

	usbd_get_xfer_status(xfer, NULL, NULL, &total_len, NULL);

	DPRINTFN(9,("%s: %s: enter status=%d length=%d\n",
		    sc->sc_dev.dv_xname, __func__, status, total_len));

	m = c->upl_mbuf;
	memcpy(mtod(c->upl_mbuf, char *), c->upl_buf, total_len);

	m->m_pkthdr.len = m->m_len = total_len;
	ml_enqueue(&ml, m);

	if (upl_newbuf(sc, c, NULL) == ENOBUFS) {
		ifp->if_ierrors++;
		goto done;
	}

	s = splnet();
	if_input(ifp, &ml);
	splx(s);
 done:
#if 1
	/* Setup new transfer. */
	usbd_setup_xfer(c->upl_xfer, sc->sc_ep[UPL_ENDPT_RX],
	    c, c->upl_buf, UPL_BUFSZ, USBD_SHORT_XFER_OK | USBD_NO_COPY,
	    USBD_NO_TIMEOUT, upl_rxeof);
	usbd_transfer(c->upl_xfer);

	DPRINTFN(10,("%s: %s: start rx\n", sc->sc_dev.dv_xname,
		    __func__));
#endif
}

/*
 * A frame was downloaded to the chip. It's safe for us to clean up
 * the list buffers.
 */
void
upl_txeof(struct usbd_xfer *xfer, void *priv, usbd_status status)
{
	struct upl_chain	*c = priv;
	struct upl_softc	*sc = c->upl_sc;
	struct ifnet		*ifp = &sc->sc_if;
	int			s;

	if (usbd_is_dying(sc->sc_udev))
		return;

	s = splnet();

	DPRINTFN(10,("%s: %s: enter status=%d\n", sc->sc_dev.dv_xname,
		    __func__, status));

	ifp->if_timer = 0;
	ifq_clr_oactive(&ifp->if_snd);

	if (status != USBD_NORMAL_COMPLETION) {
		if (status == USBD_NOT_STARTED || status == USBD_CANCELLED) {
			splx(s);
			return;
		}
		ifp->if_oerrors++;
		printf("%s: usb error on tx: %s\n", sc->sc_dev.dv_xname,
		    usbd_errstr(status));
		if (status == USBD_STALLED)
			usbd_clear_endpoint_stall_async(sc->sc_ep[UPL_ENDPT_TX]);
		splx(s);
		return;
	}

	m_freem(c->upl_mbuf);
	c->upl_mbuf = NULL;

	if (ifq_empty(&ifp->if_snd) == 0)
		upl_start(ifp);

	splx(s);
}

int
upl_send(struct upl_softc *sc, struct mbuf *m, int idx)
{
	int			total_len;
	struct upl_chain	*c;
	usbd_status		err;

	c = &sc->sc_cdata.upl_tx_chain[idx];

	/*
	 * Copy the mbuf data into a contiguous buffer, leaving two
	 * bytes at the beginning to hold the frame length.
	 */
	m_copydata(m, 0, m->m_pkthdr.len, c->upl_buf);
	c->upl_mbuf = m;

	total_len = m->m_pkthdr.len;

	DPRINTFN(10,("%s: %s: total_len=%d\n",
		     sc->sc_dev.dv_xname, __func__, total_len));

	usbd_setup_xfer(c->upl_xfer, sc->sc_ep[UPL_ENDPT_TX],
	    c, c->upl_buf, total_len, USBD_NO_COPY, USBD_DEFAULT_TIMEOUT,
	    upl_txeof);

	/* Transmit */
	err = usbd_transfer(c->upl_xfer);
	if (err != USBD_IN_PROGRESS) {
		printf("%s: upl_send error=%s\n", sc->sc_dev.dv_xname,
		       usbd_errstr(err));
		c->upl_mbuf = NULL;
		upl_stop(sc);
		return (EIO);
	}

	sc->sc_cdata.upl_tx_cnt++;

	return (0);
}

void
upl_start(struct ifnet *ifp)
{
	struct upl_softc	*sc = ifp->if_softc;
	struct mbuf		*m_head = NULL;

	if (usbd_is_dying(sc->sc_udev))
		return;

	DPRINTFN(10,("%s: %s: enter\n", sc->sc_dev.dv_xname,__func__));

	if (ifq_is_oactive(&ifp->if_snd))
		return;

	m_head = ifq_dequeue(&ifp->if_snd);
	if (m_head == NULL)
		return;

	if (upl_send(sc, m_head, 0)) {
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
	ifp->if_timer = 5;
}

void
upl_init(void *xsc)
{
	struct upl_softc	*sc = xsc;
	struct ifnet		*ifp = &sc->sc_if;
	int			s;

	if (usbd_is_dying(sc->sc_udev))
		return;

	DPRINTFN(10,("%s: %s: enter\n", sc->sc_dev.dv_xname,__func__));

	s = splnet();

	/* Init TX ring. */
	if (upl_tx_list_init(sc) == ENOBUFS) {
		printf("%s: tx list init failed\n", sc->sc_dev.dv_xname);
		splx(s);
		return;
	}

	/* Init RX ring. */
	if (upl_rx_list_init(sc) == ENOBUFS) {
		printf("%s: rx list init failed\n", sc->sc_dev.dv_xname);
		splx(s);
		return;
	}

	if (sc->sc_ep[UPL_ENDPT_RX] == NULL) {
		if (upl_openpipes(sc)) {
			splx(s);
			return;
		}
	}

	ifp->if_flags |= IFF_RUNNING;
	ifq_clr_oactive(&ifp->if_snd);

	splx(s);
}

int
upl_openpipes(struct upl_softc *sc)
{
	struct upl_chain	*c;
	usbd_status		err;
	int			i;

	/* Open RX and TX pipes. */
	err = usbd_open_pipe(sc->sc_iface, sc->sc_ed[UPL_ENDPT_RX],
	    USBD_EXCLUSIVE_USE, &sc->sc_ep[UPL_ENDPT_RX]);
	if (err) {
		printf("%s: open rx pipe failed: %s\n",
		    sc->sc_dev.dv_xname, usbd_errstr(err));
		return (EIO);
	}
	err = usbd_open_pipe(sc->sc_iface, sc->sc_ed[UPL_ENDPT_TX],
	    USBD_EXCLUSIVE_USE, &sc->sc_ep[UPL_ENDPT_TX]);
	if (err) {
		printf("%s: open tx pipe failed: %s\n",
		    sc->sc_dev.dv_xname, usbd_errstr(err));
		return (EIO);
	}
	err = usbd_open_pipe_intr(sc->sc_iface, sc->sc_ed[UPL_ENDPT_INTR],
	    0, &sc->sc_ep[UPL_ENDPT_INTR], sc,
	    &sc->sc_ibuf, UPL_INTR_PKTLEN, upl_intr,
	    UPL_INTR_INTERVAL);
	if (err) {
		printf("%s: open intr pipe failed: %s\n",
		    sc->sc_dev.dv_xname, usbd_errstr(err));
		return (EIO);
	}


#if 1
	/* Start up the receive pipe. */
	for (i = 0; i < UPL_RX_LIST_CNT; i++) {
		c = &sc->sc_cdata.upl_rx_chain[i];
		usbd_setup_xfer(c->upl_xfer, sc->sc_ep[UPL_ENDPT_RX],
		    c, c->upl_buf, UPL_BUFSZ,
		    USBD_SHORT_XFER_OK | USBD_NO_COPY, USBD_NO_TIMEOUT,
		    upl_rxeof);
		usbd_transfer(c->upl_xfer);
	}
#endif

	return (0);
}

void
upl_intr(struct usbd_xfer *xfer, void *priv, usbd_status status)
{
	struct upl_softc	*sc = priv;
	struct ifnet		*ifp = &sc->sc_if;
	uByte			stat;

	DPRINTFN(15,("%s: %s: enter\n", sc->sc_dev.dv_xname,__func__));

	if (usbd_is_dying(sc->sc_udev))
		return;

	if (!(ifp->if_flags & IFF_RUNNING))
		return;

	if (status != USBD_NORMAL_COMPLETION) {
		if (status == USBD_NOT_STARTED || status == USBD_CANCELLED) {
			return;
		}
		sc->sc_intr_errs++;
		if (usbd_ratecheck(&sc->sc_rx_notice)) {
			printf("%s: %u usb errors on intr: %s\n",
			    sc->sc_dev.dv_xname, sc->sc_rx_errs,
			    usbd_errstr(status));
			sc->sc_intr_errs = 0;
		}
		if (status == USBD_STALLED)
			usbd_clear_endpoint_stall_async(sc->sc_ep[UPL_ENDPT_RX]);
		return;
	}

	stat = sc->sc_ibuf;

	if (stat == 0)
		return;

	DPRINTFN(10,("%s: %s: stat=0x%02x\n", sc->sc_dev.dv_xname,
		     __func__, stat));

}

int
upl_ioctl(struct ifnet *ifp, u_long command, caddr_t data)
{
	struct upl_softc	*sc = ifp->if_softc;
	struct ifreq		*ifr = (struct ifreq *)data;
	int			s, error = 0;

	if (usbd_is_dying(sc->sc_udev))
		return ENXIO;

	DPRINTFN(5,("%s: %s: cmd=0x%08lx\n",
		    sc->sc_dev.dv_xname, __func__, command));

	s = splnet();

	switch(command) {
	case SIOCSIFADDR:
		ifp->if_flags |= IFF_UP;
		if (!(ifp->if_flags & IFF_RUNNING))
			upl_init(sc);
		break;

	case SIOCSIFFLAGS:
		if (ifp->if_flags & IFF_UP) {
			if (ifp->if_flags & IFF_RUNNING)
				error = ENETRESET;
			else
				upl_init(sc);
		} else {
			if (ifp->if_flags & IFF_RUNNING)
				upl_stop(sc);
		}
		break;

	case SIOCSIFMTU:
		if (ifr->ifr_mtu < ETHERMIN || ifr->ifr_mtu > ifp->if_hardmtu)
			error = EINVAL;
		else
			ifp->if_mtu = ifr->ifr_mtu;
		break;

	default:
		error = ENOTTY;
	}

	if (error == ENETRESET)
		error = 0;

	splx(s);
	return (error);
}

void
upl_watchdog(struct ifnet *ifp)
{
	struct upl_softc	*sc = ifp->if_softc;

	DPRINTFN(5,("%s: %s: enter\n", sc->sc_dev.dv_xname,__func__));

	if (usbd_is_dying(sc->sc_udev))
		return;

	ifp->if_oerrors++;
	printf("%s: watchdog timeout\n", sc->sc_dev.dv_xname);

	upl_stop(sc);
	upl_init(sc);

	if (ifq_empty(&ifp->if_snd) == 0)
		upl_start(ifp);
}

/*
 * Stop the adapter and free any mbufs allocated to the
 * RX and TX lists.
 */
void
upl_stop(struct upl_softc *sc)
{
	usbd_status		err;
	struct ifnet		*ifp;
	int			i;

	DPRINTFN(10,("%s: %s: enter\n", sc->sc_dev.dv_xname,__func__));

	ifp = &sc->sc_if;
	ifp->if_timer = 0;
	ifp->if_flags &= ~IFF_RUNNING;
	ifq_clr_oactive(&ifp->if_snd);

	/* Stop transfers. */
	if (sc->sc_ep[UPL_ENDPT_RX] != NULL) {
		err = usbd_close_pipe(sc->sc_ep[UPL_ENDPT_RX]);
		if (err) {
			printf("%s: close rx pipe failed: %s\n",
			sc->sc_dev.dv_xname, usbd_errstr(err));
		}
		sc->sc_ep[UPL_ENDPT_RX] = NULL;
	}

	if (sc->sc_ep[UPL_ENDPT_TX] != NULL) {
		err = usbd_close_pipe(sc->sc_ep[UPL_ENDPT_TX]);
		if (err) {
			printf("%s: close tx pipe failed: %s\n",
			    sc->sc_dev.dv_xname, usbd_errstr(err));
		}
		sc->sc_ep[UPL_ENDPT_TX] = NULL;
	}

	if (sc->sc_ep[UPL_ENDPT_INTR] != NULL) {
		err = usbd_close_pipe(sc->sc_ep[UPL_ENDPT_INTR]);
		if (err) {
			printf("%s: close intr pipe failed: %s\n",
			    sc->sc_dev.dv_xname, usbd_errstr(err));
		}
		sc->sc_ep[UPL_ENDPT_INTR] = NULL;
	}

	/* Free RX resources. */
	for (i = 0; i < UPL_RX_LIST_CNT; i++) {
		if (sc->sc_cdata.upl_rx_chain[i].upl_mbuf != NULL) {
			m_freem(sc->sc_cdata.upl_rx_chain[i].upl_mbuf);
			sc->sc_cdata.upl_rx_chain[i].upl_mbuf = NULL;
		}
		if (sc->sc_cdata.upl_rx_chain[i].upl_xfer != NULL) {
			usbd_free_xfer(sc->sc_cdata.upl_rx_chain[i].upl_xfer);
			sc->sc_cdata.upl_rx_chain[i].upl_xfer = NULL;
		}
	}

	/* Free TX resources. */
	for (i = 0; i < UPL_TX_LIST_CNT; i++) {
		if (sc->sc_cdata.upl_tx_chain[i].upl_mbuf != NULL) {
			m_freem(sc->sc_cdata.upl_tx_chain[i].upl_mbuf);
			sc->sc_cdata.upl_tx_chain[i].upl_mbuf = NULL;
		}
		if (sc->sc_cdata.upl_tx_chain[i].upl_xfer != NULL) {
			usbd_free_xfer(sc->sc_cdata.upl_tx_chain[i].upl_xfer);
			sc->sc_cdata.upl_tx_chain[i].upl_xfer = NULL;
		}
	}
}

int
upl_output(struct ifnet *ifp, struct mbuf *m, struct sockaddr *dst,
	   struct rtentry *rt0)
{
	return (if_enqueue(ifp, m));
}
