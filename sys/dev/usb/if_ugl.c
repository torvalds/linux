/*	$OpenBSD: if_ugl.c,v 1.28 2024/05/23 03:21:09 jsg Exp $	*/
/*	$NetBSD: if_upl.c,v 1.19 2002/07/11 21:14:26 augustss Exp $	*/
/*
 * Copyright (c) 2013 SASANO Takayoshi <uaa@uaa.org.uk>
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
 * Genesys Logic GL620USB-A driver
 *   This driver is based on Prolific PL2301/PL2302 driver (if_upl.c).
 */

#include <bpfilter.h>

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/timeout.h>
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
#include <dev/usb/usbdevs.h>

#define UGL_INTR_PKTLEN		8
#define UGL_BULK_PKTLEN		64

/***/

#define UGL_INTR_INTERVAL	20

#define UGL_MAX_MTU		1514
#define UGL_BUFSZ		roundup(sizeof(struct ugl_packet), UGL_BULK_PKTLEN)

#define UGL_RX_FRAMES		1	/* must be one */
#define UGL_TX_FRAMES		1	/* must be one */

#define UGL_RX_LIST_CNT		1
#define UGL_TX_LIST_CNT		1

#define UGL_ENDPT_RX		0x0
#define UGL_ENDPT_TX		0x1
#define UGL_ENDPT_INTR		0x2
#define UGL_ENDPT_MAX		0x3

struct ugl_softc;

struct ugl_packet {
	uDWord			pkt_count;
	uDWord			pkt_length;
	char			pkt_data[UGL_MAX_MTU];
} __packed;

struct ugl_chain {
	struct ugl_softc	*ugl_sc;
	struct usbd_xfer	*ugl_xfer;
	struct ugl_packet	*ugl_buf;
	struct mbuf		*ugl_mbuf;
	int			ugl_idx;
};

struct ugl_cdata {
	struct ugl_chain	ugl_tx_chain[UGL_TX_LIST_CNT];
	struct ugl_chain	ugl_rx_chain[UGL_RX_LIST_CNT];
	int			ugl_tx_prod;
	int			ugl_tx_cons;
	int			ugl_tx_cnt;
	int			ugl_rx_prod;
};

struct ugl_softc {
	struct device		sc_dev;

	struct arpcom		sc_arpcom;
#define GET_IFP(sc) (&(sc)->sc_arpcom.ac_if)
	struct timeout		sc_stat_ch;

	struct usbd_device	*sc_udev;
	struct usbd_interface	*sc_iface;
	int			sc_ed[UGL_ENDPT_MAX];
	struct usbd_pipe	*sc_ep[UGL_ENDPT_MAX];
	struct ugl_cdata	sc_cdata;

	uByte			sc_ibuf[UGL_INTR_PKTLEN];

	u_int			sc_rx_errs;
	struct timeval		sc_rx_notice;
	u_int			sc_intr_errs;
};

#ifdef UGL_DEBUG
#define DPRINTF(x)	do { if (ugldebug) printf x; } while (0)
#define DPRINTFN(n,x)	do { if (ugldebug >= (n)) printf x; } while (0)
int	ugldebug = 0;
#else
#define DPRINTF(x)
#define DPRINTFN(n,x)
#endif

/*
 * Various supported device vendors/products.
 */
struct usb_devno ugl_devs[] = {
	{ USB_VENDOR_GENESYS, USB_PRODUCT_GENESYS_GL620USB_A },
};

int ugl_match(struct device *, void *, void *);
void ugl_attach(struct device *, struct device *, void *);
int ugl_detach(struct device *, int);

struct cfdriver ugl_cd = {
	NULL, "ugl", DV_IFNET
};

const struct cfattach ugl_ca = {
	sizeof(struct ugl_softc), ugl_match, ugl_attach, ugl_detach
};

int ugl_openpipes(struct ugl_softc *);
int ugl_tx_list_init(struct ugl_softc *);
int ugl_rx_list_init(struct ugl_softc *);
int ugl_newbuf(struct ugl_softc *, struct ugl_chain *, struct mbuf *);
int ugl_send(struct ugl_softc *, struct mbuf *, int);
void ugl_intr(struct usbd_xfer *, void *, usbd_status);
void ugl_rxeof(struct usbd_xfer *, void *, usbd_status);
void ugl_txeof(struct usbd_xfer *, void *, usbd_status);
void ugl_start(struct ifnet *);
int ugl_ioctl(struct ifnet *, u_long, caddr_t);
void ugl_init(void *);
void ugl_stop(struct ugl_softc *);
void ugl_watchdog(struct ifnet *);

/*
 * Probe for a Genesys Logic chip.
 */
int
ugl_match(struct device *parent, void *match, void *aux)
{
	struct usb_attach_arg		*uaa = aux;

	if (uaa->iface == NULL || uaa->configno != 1)
		return (UMATCH_NONE);

	return (usb_lookup(ugl_devs, uaa->vendor, uaa->product) != NULL ?
	    UMATCH_VENDOR_PRODUCT_CONF_IFACE : UMATCH_NONE);
}

void
ugl_attach(struct device *parent, struct device *self, void *aux)
{
	struct ugl_softc	*sc = (struct ugl_softc *)self;
	struct usb_attach_arg	*uaa = aux;
	int			s;
	struct usbd_device	*dev = uaa->device;
	struct usbd_interface	*iface = uaa->iface;
	struct ifnet		*ifp = GET_IFP(sc);
	usb_interface_descriptor_t	*id;
	usb_endpoint_descriptor_t	*ed;
	int			i;

	DPRINTFN(5,(" : ugl_attach: sc=%p, dev=%p", sc, dev));

	sc->sc_udev = dev;
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
			sc->sc_ed[UGL_ENDPT_RX] = ed->bEndpointAddress;
		} else if (UE_GET_DIR(ed->bEndpointAddress) == UE_DIR_OUT &&
			   UE_GET_XFERTYPE(ed->bmAttributes) == UE_BULK) {
			sc->sc_ed[UGL_ENDPT_TX] = ed->bEndpointAddress;
		} else if (UE_GET_DIR(ed->bEndpointAddress) == UE_DIR_IN &&
			   UE_GET_XFERTYPE(ed->bmAttributes) == UE_INTERRUPT) {
			sc->sc_ed[UGL_ENDPT_INTR] = ed->bEndpointAddress;
		}
	}

	if (sc->sc_ed[UGL_ENDPT_RX] == 0 || sc->sc_ed[UGL_ENDPT_TX] == 0 ||
	    sc->sc_ed[UGL_ENDPT_INTR] == 0) {
		printf("%s: missing endpoint\n", sc->sc_dev.dv_xname);
		return;
	}

	s = splnet();

	ether_fakeaddr(ifp);
	printf("%s: address %s\n",
	    sc->sc_dev.dv_xname, ether_sprintf(sc->sc_arpcom.ac_enaddr));

	/* Initialize interface info.*/
	ifp->if_softc = sc;
	ifp->if_hardmtu = UGL_MAX_MTU;
	ifp->if_flags = IFF_BROADCAST | IFF_SIMPLEX | IFF_MULTICAST;
	ifp->if_ioctl = ugl_ioctl;
	ifp->if_start = ugl_start;
	ifp->if_watchdog = ugl_watchdog;
	strlcpy(ifp->if_xname, sc->sc_dev.dv_xname, IFNAMSIZ);

	/* Attach the interface. */
	if_attach(ifp);
	ether_ifattach(ifp);

	splx(s);
}

int
ugl_detach(struct device *self, int flags)
{
	struct ugl_softc	*sc = (struct ugl_softc *)self;
	struct ifnet		*ifp = GET_IFP(sc);
	int			s;

	DPRINTFN(2,("%s: %s: enter\n", sc->sc_dev.dv_xname, __func__));

	s = splusb();

	if (ifp->if_flags & IFF_RUNNING)
		ugl_stop(sc);

	if (ifp->if_softc != NULL)
		if_detach(ifp);

#ifdef DIAGNOSTIC
	if (sc->sc_ep[UGL_ENDPT_TX] != NULL ||
	    sc->sc_ep[UGL_ENDPT_RX] != NULL ||
	    sc->sc_ep[UGL_ENDPT_INTR] != NULL)
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
ugl_newbuf(struct ugl_softc *sc, struct ugl_chain *c, struct mbuf *m)
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

	c->ugl_mbuf = m_new;

	return (0);
}

int
ugl_rx_list_init(struct ugl_softc *sc)
{
	struct ugl_cdata	*cd;
	struct ugl_chain	*c;
	int			i;

	DPRINTFN(5,("%s: %s: enter\n", sc->sc_dev.dv_xname, __func__));

	cd = &sc->sc_cdata;
	for (i = 0; i < UGL_RX_LIST_CNT; i++) {
		c = &cd->ugl_rx_chain[i];
		c->ugl_sc = sc;
		c->ugl_idx = i;
		if (ugl_newbuf(sc, c, NULL) == ENOBUFS)
			return (ENOBUFS);
		if (c->ugl_xfer == NULL) {
			c->ugl_xfer = usbd_alloc_xfer(sc->sc_udev);
			if (c->ugl_xfer == NULL)
				return (ENOBUFS);
			c->ugl_buf = usbd_alloc_buffer(c->ugl_xfer, UGL_BUFSZ);
			if (c->ugl_buf == NULL) {
				usbd_free_xfer(c->ugl_xfer);
				return (ENOBUFS);
			}
		}
	}

	return (0);
}

int
ugl_tx_list_init(struct ugl_softc *sc)
{
	struct ugl_cdata	*cd;
	struct ugl_chain	*c;
	int			i;

	DPRINTFN(5,("%s: %s: enter\n", sc->sc_dev.dv_xname, __func__));

	cd = &sc->sc_cdata;
	for (i = 0; i < UGL_TX_LIST_CNT; i++) {
		c = &cd->ugl_tx_chain[i];
		c->ugl_sc = sc;
		c->ugl_idx = i;
		c->ugl_mbuf = NULL;
		if (c->ugl_xfer == NULL) {
			c->ugl_xfer = usbd_alloc_xfer(sc->sc_udev);
			if (c->ugl_xfer == NULL)
				return (ENOBUFS);
			c->ugl_buf = usbd_alloc_buffer(c->ugl_xfer, UGL_BUFSZ);
			if (c->ugl_buf == NULL) {
				usbd_free_xfer(c->ugl_xfer);
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
ugl_rxeof(struct usbd_xfer *xfer, void *priv, usbd_status status)
{
	struct ugl_chain	*c = priv;
	struct ugl_softc	*sc = c->ugl_sc;
	struct ifnet		*ifp = GET_IFP(sc);
	struct mbuf_list	ml = MBUF_LIST_INITIALIZER();
	struct mbuf		*m;
	int			total_len = 0;
	unsigned int		packet_len, packet_count;
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
			usbd_clear_endpoint_stall_async(sc->sc_ep[UGL_ENDPT_RX]);
		goto done;
	}

	usbd_get_xfer_status(xfer, NULL, NULL, &total_len, NULL);

	DPRINTFN(9,("%s: %s: enter status=%d length=%d\n",
		    sc->sc_dev.dv_xname, __func__, status, total_len));

	if (total_len < offsetof(struct ugl_packet, pkt_data)) {
		printf("%s: bad header (length=%d)\n",
		    sc->sc_dev.dv_xname, total_len);

		goto done;
	}

	packet_count = UGETDW(c->ugl_buf->pkt_count);
	if (packet_count != UGL_RX_FRAMES) {
		printf("%s: bad packet count (%d)\n",
		    sc->sc_dev.dv_xname, packet_count);

		if (packet_count == 0)
			goto done;
	}

	packet_len = UGETDW(c->ugl_buf->pkt_length);
	if (total_len < packet_len) {
		printf("%s: bad packet size(%d), length=%d\n",
		    sc->sc_dev.dv_xname, packet_len, total_len);

		if (packet_len == 0)
			goto done;
	}

	m = c->ugl_mbuf;
	memcpy(mtod(c->ugl_mbuf, char *), c->ugl_buf->pkt_data, packet_len);

	m->m_pkthdr.len = m->m_len = packet_len;
	ml_enqueue(&ml, m);

	if (ugl_newbuf(sc, c, NULL) == ENOBUFS) {
		ifp->if_ierrors++;
		goto done;
	}

	s = splnet();
	if_input(ifp, &ml);
	splx(s);

 done:
	/* Setup new transfer. */
	usbd_setup_xfer(c->ugl_xfer, sc->sc_ep[UGL_ENDPT_RX],
	    c, c->ugl_buf, UGL_BUFSZ, USBD_SHORT_XFER_OK | USBD_NO_COPY,
	    USBD_NO_TIMEOUT, ugl_rxeof);
	usbd_transfer(c->ugl_xfer);

	DPRINTFN(10,("%s: %s: start rx\n", sc->sc_dev.dv_xname,
		    __func__));
}

/*
 * A frame was downloaded to the chip. It's safe for us to clean up
 * the list buffers.
 */
void
ugl_txeof(struct usbd_xfer *xfer, void *priv, usbd_status status)
{
	struct ugl_chain	*c = priv;
	struct ugl_softc	*sc = c->ugl_sc;
	struct ifnet		*ifp = GET_IFP(sc);
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
			usbd_clear_endpoint_stall_async(sc->sc_ep[UGL_ENDPT_TX]);
		splx(s);
		return;
	}

	m_freem(c->ugl_mbuf);
	c->ugl_mbuf = NULL;

	if (ifq_empty(&ifp->if_snd) == 0)
		ugl_start(ifp);

	splx(s);
}

int
ugl_send(struct ugl_softc *sc, struct mbuf *m, int idx)
{
	int			total_len;
	struct ugl_chain	*c;
	usbd_status		err;

	c = &sc->sc_cdata.ugl_tx_chain[idx];

	/*
	 * Copy the mbuf data into a contiguous buffer, leaving two
	 * bytes at the beginning to hold the frame length.
	 */
	USETDW(c->ugl_buf->pkt_count, UGL_TX_FRAMES);
	USETDW(c->ugl_buf->pkt_length, m->m_pkthdr.len);
	m_copydata(m, 0, m->m_pkthdr.len, c->ugl_buf->pkt_data);
	c->ugl_mbuf = m;

	total_len = offsetof(struct ugl_packet, pkt_data[m->m_pkthdr.len]);

	DPRINTFN(10,("%s: %s: total_len=%d\n",
		     sc->sc_dev.dv_xname, __func__, total_len));

	usbd_setup_xfer(c->ugl_xfer, sc->sc_ep[UGL_ENDPT_TX],
	    c, c->ugl_buf, total_len, USBD_FORCE_SHORT_XFER | USBD_NO_COPY,
	    USBD_DEFAULT_TIMEOUT, ugl_txeof);

	/* Transmit */
	err = usbd_transfer(c->ugl_xfer);
	if (err != USBD_IN_PROGRESS) {
		printf("%s: ugl_send error=%s\n", sc->sc_dev.dv_xname,
		       usbd_errstr(err));
		c->ugl_mbuf = NULL;
		ugl_stop(sc);
		return (EIO);
	}

	sc->sc_cdata.ugl_tx_cnt++;

	return (0);
}

void
ugl_start(struct ifnet *ifp)
{
	struct ugl_softc	*sc = ifp->if_softc;
	struct mbuf		*m_head = NULL;

	if (usbd_is_dying(sc->sc_udev))
		return;

	DPRINTFN(10,("%s: %s: enter\n", sc->sc_dev.dv_xname,__func__));

	if (ifq_is_oactive(&ifp->if_snd))
		return;

	m_head = ifq_dequeue(&ifp->if_snd);
	if (m_head == NULL)
		return;

	if (ugl_send(sc, m_head, 0)) {
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
ugl_init(void *xsc)
{
	struct ugl_softc	*sc = xsc;
	struct ifnet		*ifp = GET_IFP(sc);
	int			s;

	if (usbd_is_dying(sc->sc_udev))
		return;

	DPRINTFN(10,("%s: %s: enter\n", sc->sc_dev.dv_xname,__func__));

	s = splnet();

	/* Init TX ring. */
	if (ugl_tx_list_init(sc) == ENOBUFS) {
		printf("%s: tx list init failed\n", sc->sc_dev.dv_xname);
		splx(s);
		return;
	}

	/* Init RX ring. */
	if (ugl_rx_list_init(sc) == ENOBUFS) {
		printf("%s: rx list init failed\n", sc->sc_dev.dv_xname);
		splx(s);
		return;
	}

	if (sc->sc_ep[UGL_ENDPT_RX] == NULL) {
		if (ugl_openpipes(sc)) {
			splx(s);
			return;
		}
	}

	ifp->if_flags |= IFF_RUNNING;
	ifq_clr_oactive(&ifp->if_snd);

	splx(s);
}

int
ugl_openpipes(struct ugl_softc *sc)
{
	struct ugl_chain	*c;
	usbd_status		err;
	int			i;

	/* Open RX and TX pipes. */
	err = usbd_open_pipe(sc->sc_iface, sc->sc_ed[UGL_ENDPT_RX],
	    USBD_EXCLUSIVE_USE, &sc->sc_ep[UGL_ENDPT_RX]);
	if (err) {
		printf("%s: open rx pipe failed: %s\n",
		    sc->sc_dev.dv_xname, usbd_errstr(err));
		return (EIO);
	}
	err = usbd_open_pipe(sc->sc_iface, sc->sc_ed[UGL_ENDPT_TX],
	    USBD_EXCLUSIVE_USE, &sc->sc_ep[UGL_ENDPT_TX]);
	if (err) {
		printf("%s: open tx pipe failed: %s\n",
		    sc->sc_dev.dv_xname, usbd_errstr(err));
		return (EIO);
	}
	err = usbd_open_pipe_intr(sc->sc_iface, sc->sc_ed[UGL_ENDPT_INTR],
	    0, &sc->sc_ep[UGL_ENDPT_INTR], sc,
	    sc->sc_ibuf, UGL_INTR_PKTLEN, ugl_intr,
	    UGL_INTR_INTERVAL);
	if (err) {
		printf("%s: open intr pipe failed: %s\n",
		    sc->sc_dev.dv_xname, usbd_errstr(err));
		return (EIO);
	}

	/* Start up the receive pipe. */
	for (i = 0; i < UGL_RX_LIST_CNT; i++) {
		c = &sc->sc_cdata.ugl_rx_chain[i];
		usbd_setup_xfer(c->ugl_xfer, sc->sc_ep[UGL_ENDPT_RX],
		    c, c->ugl_buf, UGL_BUFSZ,
		    USBD_SHORT_XFER_OK | USBD_NO_COPY, USBD_NO_TIMEOUT,
		    ugl_rxeof);
		usbd_transfer(c->ugl_xfer);
	}

	return (0);
}

void
ugl_intr(struct usbd_xfer *xfer, void *priv, usbd_status status)
{
	struct ugl_softc	*sc = priv;
	struct ifnet		*ifp = GET_IFP(sc);
	int			i;

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
			usbd_clear_endpoint_stall_async(sc->sc_ep[UGL_ENDPT_RX]);
		return;
	}

	DPRINTFN(10,("%s: %s:", sc->sc_dev.dv_xname, __func__));
	for (i = 0; i < UGL_INTR_PKTLEN; i++)
		DPRINTFN(10,(" 0x%02x", sc->sc_ibuf[i]));
	DPRINTFN(10,("\n"));

}

int
ugl_ioctl(struct ifnet *ifp, u_long command, caddr_t data)
{
	struct ugl_softc	*sc = ifp->if_softc;
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
			ugl_init(sc);
		break;

	case SIOCSIFFLAGS:
		if (ifp->if_flags & IFF_UP) {
			if (ifp->if_flags & IFF_RUNNING)
				error = ENETRESET;
			else
				ugl_init(sc);
		} else {
			if (ifp->if_flags & IFF_RUNNING)
				ugl_stop(sc);
		}
		break;

	default:
		error = ether_ioctl(ifp, &sc->sc_arpcom, command, data);
		break;
	}

	if (error == ENETRESET)
		error = 0;

	splx(s);
	return (error);
}

void
ugl_watchdog(struct ifnet *ifp)
{
	struct ugl_softc	*sc = ifp->if_softc;

	if (usbd_is_dying(sc->sc_udev))
		return;

	ifp->if_oerrors++;
	printf("%s: watchdog timeout\n", sc->sc_dev.dv_xname);
}

/*
 * Stop the adapter and free any mbufs allocated to the
 * RX and TX lists.
 */
void
ugl_stop(struct ugl_softc *sc)
{
	struct ifnet		*ifp;
	int			i;

	DPRINTFN(10,("%s: %s: enter\n", sc->sc_dev.dv_xname,__func__));

	ifp = GET_IFP(sc);
	ifp->if_timer = 0;
	ifp->if_flags &= ~IFF_RUNNING;
	ifq_clr_oactive(&ifp->if_snd);

	/* Stop transfers. */
	if (sc->sc_ep[UGL_ENDPT_RX] != NULL) {
		usbd_close_pipe(sc->sc_ep[UGL_ENDPT_RX]);
		sc->sc_ep[UGL_ENDPT_RX] = NULL;
	}

	if (sc->sc_ep[UGL_ENDPT_TX] != NULL) {
		usbd_close_pipe(sc->sc_ep[UGL_ENDPT_TX]);
		sc->sc_ep[UGL_ENDPT_TX] = NULL;
	}

	if (sc->sc_ep[UGL_ENDPT_INTR] != NULL) {
		usbd_close_pipe(sc->sc_ep[UGL_ENDPT_INTR]);
		sc->sc_ep[UGL_ENDPT_INTR] = NULL;
	}

	/* Free RX resources. */
	for (i = 0; i < UGL_RX_LIST_CNT; i++) {
		if (sc->sc_cdata.ugl_rx_chain[i].ugl_mbuf != NULL) {
			m_freem(sc->sc_cdata.ugl_rx_chain[i].ugl_mbuf);
			sc->sc_cdata.ugl_rx_chain[i].ugl_mbuf = NULL;
		}
		if (sc->sc_cdata.ugl_rx_chain[i].ugl_xfer != NULL) {
			usbd_free_xfer(sc->sc_cdata.ugl_rx_chain[i].ugl_xfer);
			sc->sc_cdata.ugl_rx_chain[i].ugl_xfer = NULL;
		}
	}

	/* Free TX resources. */
	for (i = 0; i < UGL_TX_LIST_CNT; i++) {
		if (sc->sc_cdata.ugl_tx_chain[i].ugl_mbuf != NULL) {
			m_freem(sc->sc_cdata.ugl_tx_chain[i].ugl_mbuf);
			sc->sc_cdata.ugl_tx_chain[i].ugl_mbuf = NULL;
		}
		if (sc->sc_cdata.ugl_tx_chain[i].ugl_xfer != NULL) {
			usbd_free_xfer(sc->sc_cdata.ugl_tx_chain[i].ugl_xfer);
			sc->sc_cdata.ugl_tx_chain[i].ugl_xfer = NULL;
		}
	}
}
