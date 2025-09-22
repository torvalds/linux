/*	$OpenBSD: if_cue.c,v 1.81 2024/05/23 03:21:08 jsg Exp $ */
/*	$NetBSD: if_cue.c,v 1.40 2002/07/11 21:14:26 augustss Exp $	*/
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
 * $FreeBSD: src/sys/dev/usb/if_cue.c,v 1.4 2000/01/16 22:45:06 wpaul Exp $
 */

/*
 * CATC USB-EL1210A USB to ethernet driver. Used in the CATC Netmate
 * adapters and others.
 *
 * Written by Bill Paul <wpaul@ee.columbia.edu>
 * Electrical Engineering Department
 * Columbia University, New York City
 */

/*
 * The CATC USB-EL1210A provides USB ethernet support at 10Mbps. The
 * RX filter uses a 512-bit multicast hash table, single perfect entry
 * for the station address, and promiscuous mode. Unlike the ADMtek
 * and KLSI chips, the CATC ASIC supports read and write combining
 * mode where multiple packets can be transferred using a single bulk
 * transaction, which helps performance a great deal.
 */

/*
 * Ported to NetBSD and somewhat rewritten by Lennart Augustsson.
 */

#include "bpfilter.h"

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/sockio.h>
#include <sys/mbuf.h>
#include <sys/timeout.h>
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

#include <dev/usb/if_cuereg.h>

#ifdef CUE_DEBUG
#define DPRINTF(x)	do { if (cuedebug) printf x; } while (0)
#define DPRINTFN(n,x)	do { if (cuedebug >= (n)) printf x; } while (0)
int	cuedebug = 0;
#else
#define DPRINTF(x)
#define DPRINTFN(n,x)
#endif

/*
 * Various supported device vendors/products.
 */
struct usb_devno cue_devs[] = {
	{ USB_VENDOR_CATC, USB_PRODUCT_CATC_NETMATE },
	{ USB_VENDOR_CATC, USB_PRODUCT_CATC_NETMATE2 },
	{ USB_VENDOR_SMARTBRIDGES, USB_PRODUCT_SMARTBRIDGES_SMARTLINK },
	/* Belkin F5U111 adapter covered by NETMATE entry */
};

int cue_match(struct device *, void *, void *);
void cue_attach(struct device *, struct device *, void *);
int cue_detach(struct device *, int);

struct cfdriver cue_cd = {
	NULL, "cue", DV_IFNET
};

const struct cfattach cue_ca = {
	sizeof(struct cue_softc), cue_match, cue_attach, cue_detach
};

int cue_open_pipes(struct cue_softc *);
int cue_tx_list_init(struct cue_softc *);
int cue_rx_list_init(struct cue_softc *);
int cue_newbuf(struct cue_softc *, struct cue_chain *, struct mbuf *);
int cue_send(struct cue_softc *, struct mbuf *, int);
void cue_rxeof(struct usbd_xfer *, void *, usbd_status);
void cue_txeof(struct usbd_xfer *, void *, usbd_status);
void cue_tick(void *);
void cue_tick_task(void *);
void cue_start(struct ifnet *);
int cue_ioctl(struct ifnet *, u_long, caddr_t);
void cue_init(void *);
void cue_stop(struct cue_softc *);
void cue_watchdog(struct ifnet *);

void cue_setmulti(struct cue_softc *);
void cue_reset(struct cue_softc *);

int cue_csr_read_1(struct cue_softc *, int);
int cue_csr_write_1(struct cue_softc *, int, int);
int cue_csr_read_2(struct cue_softc *, int);
#if 0
int cue_csr_write_2(struct cue_softc *, int, int);
#endif
int cue_mem(struct cue_softc *, int, int, void *, int);
int cue_getmac(struct cue_softc *, void *);

#define CUE_SETBIT(sc, reg, x)				\
	cue_csr_write_1(sc, reg, cue_csr_read_1(sc, reg) | (x))

#define CUE_CLRBIT(sc, reg, x)				\
	cue_csr_write_1(sc, reg, cue_csr_read_1(sc, reg) & ~(x))

int
cue_csr_read_1(struct cue_softc *sc, int reg)
{
	usb_device_request_t	req;
	usbd_status		err;
	u_int8_t		val = 0;

	if (usbd_is_dying(sc->cue_udev))
		return (0);

	req.bmRequestType = UT_READ_VENDOR_DEVICE;
	req.bRequest = CUE_CMD_READREG;
	USETW(req.wValue, 0);
	USETW(req.wIndex, reg);
	USETW(req.wLength, 1);

	err = usbd_do_request(sc->cue_udev, &req, &val);

	if (err) {
		DPRINTF(("%s: cue_csr_read_1: reg=0x%x err=%s\n",
			 sc->cue_dev.dv_xname, reg, usbd_errstr(err)));
		return (0);
	}

	DPRINTFN(10,("%s: cue_csr_read_1 reg=0x%x val=0x%x\n",
		     sc->cue_dev.dv_xname, reg, val));

	return (val);
}

int
cue_csr_read_2(struct cue_softc *sc, int reg)
{
	usb_device_request_t	req;
	usbd_status		err;
	uWord			val;

	if (usbd_is_dying(sc->cue_udev))
		return (0);

	req.bmRequestType = UT_READ_VENDOR_DEVICE;
	req.bRequest = CUE_CMD_READREG;
	USETW(req.wValue, 0);
	USETW(req.wIndex, reg);
	USETW(req.wLength, 2);

	err = usbd_do_request(sc->cue_udev, &req, &val);

	DPRINTFN(10,("%s: cue_csr_read_2 reg=0x%x val=0x%x\n",
		     sc->cue_dev.dv_xname, reg, UGETW(val)));

	if (err) {
		DPRINTF(("%s: cue_csr_read_2: reg=0x%x err=%s\n",
			 sc->cue_dev.dv_xname, reg, usbd_errstr(err)));
		return (0);
	}

	return (UGETW(val));
}

int
cue_csr_write_1(struct cue_softc *sc, int reg, int val)
{
	usb_device_request_t	req;
	usbd_status		err;

	if (usbd_is_dying(sc->cue_udev))
		return (0);

	DPRINTFN(10,("%s: cue_csr_write_1 reg=0x%x val=0x%x\n",
		     sc->cue_dev.dv_xname, reg, val));

	req.bmRequestType = UT_WRITE_VENDOR_DEVICE;
	req.bRequest = CUE_CMD_WRITEREG;
	USETW(req.wValue, val);
	USETW(req.wIndex, reg);
	USETW(req.wLength, 0);

	err = usbd_do_request(sc->cue_udev, &req, NULL);

	if (err) {
		DPRINTF(("%s: cue_csr_write_1: reg=0x%x err=%s\n",
			 sc->cue_dev.dv_xname, reg, usbd_errstr(err)));
		return (-1);
	}

	DPRINTFN(20,("%s: cue_csr_write_1, after reg=0x%x val=0x%x\n",
		     sc->cue_dev.dv_xname, reg, cue_csr_read_1(sc, reg)));

	return (0);
}

#if 0
int
cue_csr_write_2(struct cue_softc *sc, int reg, int aval)
{
	usb_device_request_t	req;
	usbd_status		err;
	uWord			val;
	int			s;

	if (usbd_is_dying(sc->cue_udev))
		return (0);

	DPRINTFN(10,("%s: cue_csr_write_2 reg=0x%x val=0x%x\n",
		     sc->cue_dev.dv_xname, reg, aval));

	USETW(val, aval);
	req.bmRequestType = UT_WRITE_VENDOR_DEVICE;
	req.bRequest = CUE_CMD_WRITEREG;
	USETW(req.wValue, val);
	USETW(req.wIndex, reg);
	USETW(req.wLength, 0);

	err = usbd_do_request(sc->cue_udev, &req, NULL);

	if (err) {
		DPRINTF(("%s: cue_csr_write_2: reg=0x%x err=%s\n",
			 sc->cue_dev.dv_xname, reg, usbd_errstr(err)));
		return (-1);
	}

	return (0);
}
#endif

int
cue_mem(struct cue_softc *sc, int cmd, int addr, void *buf, int len)
{
	usb_device_request_t	req;
	usbd_status		err;

	DPRINTFN(10,("%s: cue_mem cmd=0x%x addr=0x%x len=%d\n",
		     sc->cue_dev.dv_xname, cmd, addr, len));

	if (cmd == CUE_CMD_READSRAM)
		req.bmRequestType = UT_READ_VENDOR_DEVICE;
	else
		req.bmRequestType = UT_WRITE_VENDOR_DEVICE;
	req.bRequest = cmd;
	USETW(req.wValue, 0);
	USETW(req.wIndex, addr);
	USETW(req.wLength, len);

	err = usbd_do_request(sc->cue_udev, &req, buf);

	if (err) {
		DPRINTF(("%s: cue_csr_mem: addr=0x%x err=%s\n",
			 sc->cue_dev.dv_xname, addr, usbd_errstr(err)));
		return (-1);
	}

	return (0);
}

int
cue_getmac(struct cue_softc *sc, void *buf)
{
	usb_device_request_t	req;
	usbd_status		err;

	DPRINTFN(10,("%s: cue_getmac\n", sc->cue_dev.dv_xname));

	req.bmRequestType = UT_READ_VENDOR_DEVICE;
	req.bRequest = CUE_CMD_GET_MACADDR;
	USETW(req.wValue, 0);
	USETW(req.wIndex, 0);
	USETW(req.wLength, ETHER_ADDR_LEN);

	err = usbd_do_request(sc->cue_udev, &req, buf);

	if (err) {
		printf("%s: read MAC address failed\n",
		       sc->cue_dev.dv_xname);
		return (-1);
	}

	return (0);
}

#define CUE_BITS	9

void
cue_setmulti(struct cue_softc *sc)
{
	struct arpcom		*ac = &sc->arpcom;
	struct ifnet		*ifp;
	struct ether_multi	*enm;
	struct ether_multistep	step;
	u_int32_t		h, i;

	ifp = GET_IFP(sc);

	DPRINTFN(2,("%s: cue_setmulti if_flags=0x%x\n",
		    sc->cue_dev.dv_xname, ifp->if_flags));

	if (ifp->if_flags & IFF_PROMISC || ac->ac_multirangecnt > 0) {
		ifp->if_flags |= IFF_ALLMULTI;
		for (i = 0; i < CUE_MCAST_TABLE_LEN; i++)
			sc->cue_mctab[i] = 0xFF;
		cue_mem(sc, CUE_CMD_WRITESRAM, CUE_MCAST_TABLE_ADDR,
		    &sc->cue_mctab, CUE_MCAST_TABLE_LEN);
		return;
	}

	/* first, zot all the existing hash bits */
	for (i = 0; i < CUE_MCAST_TABLE_LEN; i++)
		sc->cue_mctab[i] = 0;

	/* now program new ones */
	ETHER_FIRST_MULTI(step, ac, enm);
	while (enm != NULL) {
		h = ether_crc32_le(enm->enm_addrlo, ETHER_ADDR_LEN) &
		    ((1 << CUE_BITS) - 1);
		sc->cue_mctab[h >> 3] |= 1 << (h & 0x7);
		ETHER_NEXT_MULTI(step, enm);
	}

	ifp->if_flags &= ~IFF_ALLMULTI;

	/*
	 * Also include the broadcast address in the filter
	 * so we can receive broadcast frames.
	 */
	if (ifp->if_flags & IFF_BROADCAST) {
		h = ether_crc32_le(etherbroadcastaddr, ETHER_ADDR_LEN) &
		    ((1 << CUE_BITS) - 1);
		sc->cue_mctab[h >> 3] |= 1 << (h & 0x7);
	}

	cue_mem(sc, CUE_CMD_WRITESRAM, CUE_MCAST_TABLE_ADDR,
	    &sc->cue_mctab, CUE_MCAST_TABLE_LEN);
}

void
cue_reset(struct cue_softc *sc)
{
	usb_device_request_t	req;
	usbd_status		err;

	DPRINTFN(2,("%s: cue_reset\n", sc->cue_dev.dv_xname));

	if (usbd_is_dying(sc->cue_udev))
		return;

	req.bmRequestType = UT_WRITE_VENDOR_DEVICE;
	req.bRequest = CUE_CMD_RESET;
	USETW(req.wValue, 0);
	USETW(req.wIndex, 0);
	USETW(req.wLength, 0);

	err = usbd_do_request(sc->cue_udev, &req, NULL);

	if (err)
		printf("%s: reset failed\n", sc->cue_dev.dv_xname);

	/* Wait a little while for the chip to get its brains in order. */
	usbd_delay_ms(sc->cue_udev, 1);
}

/*
 * Probe for a CATC chip.
 */
int
cue_match(struct device *parent, void *match, void *aux)
{
	struct usb_attach_arg	*uaa = aux;

	if (uaa->iface == NULL || uaa->configno != CUE_CONFIG_NO)
		return (UMATCH_NONE);

	return (usb_lookup(cue_devs, uaa->vendor, uaa->product) != NULL ?
	    UMATCH_VENDOR_PRODUCT : UMATCH_NONE);
}

/*
 * Attach the interface. Allocate softc structures, do ifmedia
 * setup and ethernet/BPF attach.
 */
void
cue_attach(struct device *parent, struct device *self, void *aux)
{
	struct cue_softc	*sc = (struct cue_softc *)self;
	struct usb_attach_arg	*uaa = aux;
	int			s;
	u_char			eaddr[ETHER_ADDR_LEN];
	struct usbd_device	*dev = uaa->device;
	struct usbd_interface	*iface;
	usbd_status		err;
	struct ifnet		*ifp;
	usb_interface_descriptor_t	*id;
	usb_endpoint_descriptor_t	*ed;
	int			i;

	DPRINTFN(5,(" : cue_attach: sc=%p, dev=%p", sc, dev));

	sc->cue_udev = dev;
	sc->cue_product = uaa->product;
	sc->cue_vendor = uaa->vendor;

	usb_init_task(&sc->cue_tick_task, cue_tick_task, sc,
	    USB_TASK_TYPE_GENERIC);
	usb_init_task(&sc->cue_stop_task, (void (*)(void *))cue_stop, sc,
	    USB_TASK_TYPE_GENERIC);

	err = usbd_device2interface_handle(dev, CUE_IFACE_IDX, &iface);
	if (err) {
		printf("%s: getting interface handle failed\n",
		    sc->cue_dev.dv_xname);
		return;
	}

	sc->cue_iface = iface;
	id = usbd_get_interface_descriptor(iface);

	/* Find endpoints. */
	for (i = 0; i < id->bNumEndpoints; i++) {
		ed = usbd_interface2endpoint_descriptor(iface, i);
		if (ed == NULL) {
			printf("%s: couldn't get ep %d\n",
			    sc->cue_dev.dv_xname, i);
			return;
		}
		if (UE_GET_DIR(ed->bEndpointAddress) == UE_DIR_IN &&
		    UE_GET_XFERTYPE(ed->bmAttributes) == UE_BULK) {
			sc->cue_ed[CUE_ENDPT_RX] = ed->bEndpointAddress;
		} else if (UE_GET_DIR(ed->bEndpointAddress) == UE_DIR_OUT &&
			   UE_GET_XFERTYPE(ed->bmAttributes) == UE_BULK) {
			sc->cue_ed[CUE_ENDPT_TX] = ed->bEndpointAddress;
		} else if (UE_GET_DIR(ed->bEndpointAddress) == UE_DIR_IN &&
			   UE_GET_XFERTYPE(ed->bmAttributes) == UE_INTERRUPT) {
			sc->cue_ed[CUE_ENDPT_INTR] = ed->bEndpointAddress;
		}
	}

#if 0
	/* Reset the adapter. */
	cue_reset(sc);
#endif
	/*
	 * Get station address.
	 */
	cue_getmac(sc, &eaddr);

	s = splnet();

	/*
	 * A CATC chip was detected. Inform the world.
	 */
	printf("%s: address %s\n", sc->cue_dev.dv_xname,
	    ether_sprintf(eaddr));

	bcopy(eaddr, (char *)&sc->arpcom.ac_enaddr, ETHER_ADDR_LEN);

	/* Initialize interface info.*/
	ifp = GET_IFP(sc);
	ifp->if_softc = sc;
	ifp->if_flags = IFF_BROADCAST | IFF_SIMPLEX | IFF_MULTICAST;
	ifp->if_ioctl = cue_ioctl;
	ifp->if_start = cue_start;
	ifp->if_watchdog = cue_watchdog;
	strlcpy(ifp->if_xname, sc->cue_dev.dv_xname, IFNAMSIZ);

	/* Attach the interface. */
	if_attach(ifp);
	ether_ifattach(ifp);

	timeout_set(&sc->cue_stat_ch, cue_tick, sc);

	splx(s);
}

int
cue_detach(struct device *self, int flags)
{
	struct cue_softc	*sc = (struct cue_softc *)self;
	struct ifnet		*ifp = GET_IFP(sc);
	int			s;

	DPRINTFN(2,("%s: %s: enter\n", sc->cue_dev.dv_xname, __func__));

	if (timeout_initialized(&sc->cue_stat_ch))
		timeout_del(&sc->cue_stat_ch);

	/*
	 * Remove any pending task.  It cannot be executing because it run
	 * in the same thread as detach.
	 */
	usb_rem_task(sc->cue_udev, &sc->cue_tick_task);
	usb_rem_task(sc->cue_udev, &sc->cue_stop_task);

	s = splusb();

	if (ifp->if_flags & IFF_RUNNING)
		cue_stop(sc);

	if (ifp->if_softc != NULL) {
		ether_ifdetach(ifp);
		if_detach(ifp);
	}

#ifdef DIAGNOSTIC
	if (sc->cue_ep[CUE_ENDPT_TX] != NULL ||
	    sc->cue_ep[CUE_ENDPT_RX] != NULL ||
	    sc->cue_ep[CUE_ENDPT_INTR] != NULL)
		printf("%s: detach has active endpoints\n",
		       sc->cue_dev.dv_xname);
#endif

	splx(s);

	return (0);
}

/*
 * Initialize an RX descriptor and attach an MBUF cluster.
 */
int
cue_newbuf(struct cue_softc *sc, struct cue_chain *c, struct mbuf *m)
{
	struct mbuf		*m_new = NULL;

	if (m == NULL) {
		MGETHDR(m_new, M_DONTWAIT, MT_DATA);
		if (m_new == NULL) {
			printf("%s: no memory for rx list "
			    "-- packet dropped!\n", sc->cue_dev.dv_xname);
			return (ENOBUFS);
		}

		MCLGET(m_new, M_DONTWAIT);
		if (!(m_new->m_flags & M_EXT)) {
			printf("%s: no memory for rx list "
			    "-- packet dropped!\n", sc->cue_dev.dv_xname);
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
	c->cue_mbuf = m_new;

	return (0);
}

int
cue_rx_list_init(struct cue_softc *sc)
{
	struct cue_cdata	*cd;
	struct cue_chain	*c;
	int			i;

	cd = &sc->cue_cdata;
	for (i = 0; i < CUE_RX_LIST_CNT; i++) {
		c = &cd->cue_rx_chain[i];
		c->cue_sc = sc;
		c->cue_idx = i;
		if (cue_newbuf(sc, c, NULL) == ENOBUFS)
			return (ENOBUFS);
		if (c->cue_xfer == NULL) {
			c->cue_xfer = usbd_alloc_xfer(sc->cue_udev);
			if (c->cue_xfer == NULL)
				return (ENOBUFS);
			c->cue_buf = usbd_alloc_buffer(c->cue_xfer, CUE_BUFSZ);
			if (c->cue_buf == NULL) {
				usbd_free_xfer(c->cue_xfer);
				return (ENOBUFS);
			}
		}
	}

	return (0);
}

int
cue_tx_list_init(struct cue_softc *sc)
{
	struct cue_cdata	*cd;
	struct cue_chain	*c;
	int			i;

	cd = &sc->cue_cdata;
	for (i = 0; i < CUE_TX_LIST_CNT; i++) {
		c = &cd->cue_tx_chain[i];
		c->cue_sc = sc;
		c->cue_idx = i;
		c->cue_mbuf = NULL;
		if (c->cue_xfer == NULL) {
			c->cue_xfer = usbd_alloc_xfer(sc->cue_udev);
			if (c->cue_xfer == NULL)
				return (ENOBUFS);
			c->cue_buf = usbd_alloc_buffer(c->cue_xfer, CUE_BUFSZ);
			if (c->cue_buf == NULL) {
				usbd_free_xfer(c->cue_xfer);
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
cue_rxeof(struct usbd_xfer *xfer, void *priv, usbd_status status)
{
	struct cue_chain	*c = priv;
	struct cue_softc	*sc = c->cue_sc;
	struct ifnet		*ifp = GET_IFP(sc);
	struct mbuf_list	ml = MBUF_LIST_INITIALIZER();
	struct mbuf		*m;
	int			total_len = 0;
	u_int16_t		len;
	int			s;

	DPRINTFN(10,("%s: %s: enter status=%d\n", sc->cue_dev.dv_xname,
		     __func__, status));

	if (usbd_is_dying(sc->cue_udev))
		return;

	if (!(ifp->if_flags & IFF_RUNNING))
		return;

	if (status != USBD_NORMAL_COMPLETION) {
		if (status == USBD_NOT_STARTED || status == USBD_CANCELLED)
			return;
		sc->cue_rx_errs++;
		if (usbd_ratecheck(&sc->cue_rx_notice)) {
			printf("%s: %u usb errors on rx: %s\n",
			    sc->cue_dev.dv_xname, sc->cue_rx_errs,
			    usbd_errstr(status));
			sc->cue_rx_errs = 0;
		}
		if (status == USBD_STALLED)
			usbd_clear_endpoint_stall_async(sc->cue_ep[CUE_ENDPT_RX]);
		goto done;
	}

	usbd_get_xfer_status(xfer, NULL, NULL, &total_len, NULL);

	memcpy(mtod(c->cue_mbuf, char *), c->cue_buf, total_len);

	m = c->cue_mbuf;
	len = UGETW(mtod(m, u_int8_t *));

	/* No errors; receive the packet. */
	total_len = len;

	if (len < sizeof(struct ether_header)) {
		ifp->if_ierrors++;
		goto done;
	}

	m_adj(m, sizeof(u_int16_t));
	m->m_pkthdr.len = m->m_len = total_len;
	ml_enqueue(&ml, m);

	if (cue_newbuf(sc, c, NULL) == ENOBUFS) {
		ifp->if_ierrors++;
		goto done;
	}

	s = splnet();
	if_input(ifp, &ml);
	splx(s);

done:
	/* Setup new transfer. */
	usbd_setup_xfer(c->cue_xfer, sc->cue_ep[CUE_ENDPT_RX],
	    c, c->cue_buf, CUE_BUFSZ, USBD_SHORT_XFER_OK | USBD_NO_COPY,
	    USBD_NO_TIMEOUT, cue_rxeof);
	usbd_transfer(c->cue_xfer);

	DPRINTFN(10,("%s: %s: start rx\n", sc->cue_dev.dv_xname,
		    __func__));
}

/*
 * A frame was downloaded to the chip. It's safe for us to clean up
 * the list buffers.
 */
void
cue_txeof(struct usbd_xfer *xfer, void *priv, usbd_status status)
{
	struct cue_chain	*c = priv;
	struct cue_softc	*sc = c->cue_sc;
	struct ifnet		*ifp = GET_IFP(sc);
	int			s;

	if (usbd_is_dying(sc->cue_udev))
		return;

	s = splnet();

	DPRINTFN(10,("%s: %s: enter status=%d\n", sc->cue_dev.dv_xname,
		    __func__, status));

	ifp->if_timer = 0;
	ifq_clr_oactive(&ifp->if_snd);

	if (status != USBD_NORMAL_COMPLETION) {
		if (status == USBD_NOT_STARTED || status == USBD_CANCELLED) {
			splx(s);
			return;
		}
		ifp->if_oerrors++;
		printf("%s: usb error on tx: %s\n", sc->cue_dev.dv_xname,
		    usbd_errstr(status));
		if (status == USBD_STALLED)
			usbd_clear_endpoint_stall_async(sc->cue_ep[CUE_ENDPT_TX]);
		splx(s);
		return;
	}

	m_freem(c->cue_mbuf);
	c->cue_mbuf = NULL;

	if (ifq_empty(&ifp->if_snd) == 0)
		cue_start(ifp);

	splx(s);
}

void
cue_tick(void *xsc)
{
	struct cue_softc	*sc = xsc;

	if (sc == NULL)
		return;

	if (usbd_is_dying(sc->cue_udev))
		return;

	DPRINTFN(2,("%s: %s: enter\n", sc->cue_dev.dv_xname, __func__));

	/* Perform statistics update in process context. */
	usb_add_task(sc->cue_udev, &sc->cue_tick_task);
}

void
cue_tick_task(void *xsc)
{
	struct cue_softc	*sc = xsc;
	struct ifnet		*ifp;

	if (usbd_is_dying(sc->cue_udev))
		return;

	DPRINTFN(2,("%s: %s: enter\n", sc->cue_dev.dv_xname, __func__));

	ifp = GET_IFP(sc);

	ifp->if_collisions += cue_csr_read_2(sc, CUE_TX_SINGLECOLL);
	ifp->if_collisions += cue_csr_read_2(sc, CUE_TX_MULTICOLL);
	ifp->if_collisions += cue_csr_read_2(sc, CUE_TX_EXCESSCOLL);

	if (cue_csr_read_2(sc, CUE_RX_FRAMEERR))
		ifp->if_ierrors++;
}

int
cue_send(struct cue_softc *sc, struct mbuf *m, int idx)
{
	int			total_len;
	struct cue_chain	*c;
	usbd_status		err;

	c = &sc->cue_cdata.cue_tx_chain[idx];

	/*
	 * Copy the mbuf data into a contiguous buffer, leaving two
	 * bytes at the beginning to hold the frame length.
	 */
	m_copydata(m, 0, m->m_pkthdr.len, c->cue_buf + 2);
	c->cue_mbuf = m;

	total_len = m->m_pkthdr.len + 2;

	DPRINTFN(10,("%s: %s: total_len=%d\n",
		     sc->cue_dev.dv_xname, __func__, total_len));

	/* The first two bytes are the frame length */
	c->cue_buf[0] = (u_int8_t)m->m_pkthdr.len;
	c->cue_buf[1] = (u_int8_t)(m->m_pkthdr.len >> 8);

	/* XXX 10000 */
	usbd_setup_xfer(c->cue_xfer, sc->cue_ep[CUE_ENDPT_TX],
	    c, c->cue_buf, total_len, USBD_NO_COPY, 10000, cue_txeof);

	/* Transmit */
	err = usbd_transfer(c->cue_xfer);
	if (err != USBD_IN_PROGRESS) {
		printf("%s: cue_send error=%s\n", sc->cue_dev.dv_xname,
		       usbd_errstr(err));
		/* Stop the interface from process context. */
		usb_add_task(sc->cue_udev, &sc->cue_stop_task);
		return (EIO);
	}

	sc->cue_cdata.cue_tx_cnt++;

	return (0);
}

void
cue_start(struct ifnet *ifp)
{
	struct cue_softc	*sc = ifp->if_softc;
	struct mbuf		*m_head = NULL;

	if (usbd_is_dying(sc->cue_udev))
		return;

	DPRINTFN(10,("%s: %s: enter\n", sc->cue_dev.dv_xname,__func__));

	if (ifq_is_oactive(&ifp->if_snd))
		return;

	m_head = ifq_deq_begin(&ifp->if_snd);
	if (m_head == NULL)
		return;

	if (cue_send(sc, m_head, 0)) {
		ifq_deq_rollback(&ifp->if_snd, m_head);
		ifq_set_oactive(&ifp->if_snd);
		return;
	}

	ifq_deq_commit(&ifp->if_snd, m_head);

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
cue_init(void *xsc)
{
	struct cue_softc	*sc = xsc;
	struct ifnet		*ifp = GET_IFP(sc);
	int			i, s, ctl;
	u_char			*eaddr;

	if (usbd_is_dying(sc->cue_udev))
		return;

	DPRINTFN(10,("%s: %s: enter\n", sc->cue_dev.dv_xname,__func__));

	if (ifp->if_flags & IFF_RUNNING)
		return;

	s = splnet();

	/*
	 * Cancel pending I/O and free all RX/TX buffers.
	 */
#if 1
	cue_reset(sc);
#endif

	/* Set advanced operation modes. */
	cue_csr_write_1(sc, CUE_ADVANCED_OPMODES,
	    CUE_AOP_EMBED_RXLEN | 0x03); /* 1 wait state */

	eaddr = sc->arpcom.ac_enaddr;
	/* Set MAC address */
	for (i = 0; i < ETHER_ADDR_LEN; i++)
		cue_csr_write_1(sc, CUE_PAR0 - i, eaddr[i]);

	/* Enable RX logic. */
	ctl = CUE_ETHCTL_RX_ON | CUE_ETHCTL_MCAST_ON;
	if (ifp->if_flags & IFF_PROMISC)
		ctl |= CUE_ETHCTL_PROMISC;
	cue_csr_write_1(sc, CUE_ETHCTL, ctl);

	/* Init TX ring. */
	if (cue_tx_list_init(sc) == ENOBUFS) {
		printf("%s: tx list init failed\n", sc->cue_dev.dv_xname);
		splx(s);
		return;
	}

	/* Init RX ring. */
	if (cue_rx_list_init(sc) == ENOBUFS) {
		printf("%s: rx list init failed\n", sc->cue_dev.dv_xname);
		splx(s);
		return;
	}

	/* Load the multicast filter. */
	cue_setmulti(sc);

	/*
	 * Set the number of RX and TX buffers that we want
	 * to reserve inside the ASIC.
	 */
	cue_csr_write_1(sc, CUE_RX_BUFPKTS, CUE_RX_FRAMES);
	cue_csr_write_1(sc, CUE_TX_BUFPKTS, CUE_TX_FRAMES);

	/* Set advanced operation modes. */
	cue_csr_write_1(sc, CUE_ADVANCED_OPMODES,
	    CUE_AOP_EMBED_RXLEN | 0x01); /* 1 wait state */

	/* Program the LED operation. */
	cue_csr_write_1(sc, CUE_LEDCTL, CUE_LEDCTL_FOLLOW_LINK);

	if (sc->cue_ep[CUE_ENDPT_RX] == NULL) {
		if (cue_open_pipes(sc)) {
			splx(s);
			return;
		}
	}

	ifp->if_flags |= IFF_RUNNING;
	ifq_clr_oactive(&ifp->if_snd);

	splx(s);

	timeout_add_sec(&sc->cue_stat_ch, 1);
}

int
cue_open_pipes(struct cue_softc *sc)
{
	struct cue_chain	*c;
	usbd_status		err;
	int			i;

	/* Open RX and TX pipes. */
	err = usbd_open_pipe(sc->cue_iface, sc->cue_ed[CUE_ENDPT_RX],
	    USBD_EXCLUSIVE_USE, &sc->cue_ep[CUE_ENDPT_RX]);
	if (err) {
		printf("%s: open rx pipe failed: %s\n",
		    sc->cue_dev.dv_xname, usbd_errstr(err));
		return (EIO);
	}
	err = usbd_open_pipe(sc->cue_iface, sc->cue_ed[CUE_ENDPT_TX],
	    USBD_EXCLUSIVE_USE, &sc->cue_ep[CUE_ENDPT_TX]);
	if (err) {
		printf("%s: open tx pipe failed: %s\n",
		    sc->cue_dev.dv_xname, usbd_errstr(err));
		return (EIO);
	}

	/* Start up the receive pipe. */
	for (i = 0; i < CUE_RX_LIST_CNT; i++) {
		c = &sc->cue_cdata.cue_rx_chain[i];
		usbd_setup_xfer(c->cue_xfer, sc->cue_ep[CUE_ENDPT_RX],
		    c, c->cue_buf, CUE_BUFSZ,
		    USBD_SHORT_XFER_OK | USBD_NO_COPY, USBD_NO_TIMEOUT,
		    cue_rxeof);
		usbd_transfer(c->cue_xfer);
	}

	return (0);
}

int
cue_ioctl(struct ifnet *ifp, u_long command, caddr_t data)
{
	struct cue_softc	*sc = ifp->if_softc;
	int			s, error = 0;

	if (usbd_is_dying(sc->cue_udev))
		return ENXIO;

	s = splnet();

	switch(command) {
	case SIOCSIFADDR:
		ifp->if_flags |= IFF_UP;
		cue_init(sc);
		break;

	case SIOCSIFFLAGS:
		if (ifp->if_flags & IFF_UP) {
			if (ifp->if_flags & IFF_RUNNING &&
			    ifp->if_flags & IFF_PROMISC &&
			    !(sc->cue_if_flags & IFF_PROMISC)) {
				CUE_SETBIT(sc, CUE_ETHCTL, CUE_ETHCTL_PROMISC);
				cue_setmulti(sc);
			} else if (ifp->if_flags & IFF_RUNNING &&
			    !(ifp->if_flags & IFF_PROMISC) &&
			    sc->cue_if_flags & IFF_PROMISC) {
				CUE_CLRBIT(sc, CUE_ETHCTL, CUE_ETHCTL_PROMISC);
				cue_setmulti(sc);
			} else if (!(ifp->if_flags & IFF_RUNNING))
				cue_init(sc);
		} else {
			if (ifp->if_flags & IFF_RUNNING)
				cue_stop(sc);
		}
		sc->cue_if_flags = ifp->if_flags;
		error = 0;
		break;

	default:
		error = ether_ioctl(ifp, &sc->arpcom, command, data);
	}

	if (error == ENETRESET) {
		if (ifp->if_flags & IFF_RUNNING)
			cue_setmulti(sc);
		error = 0;
	}

	splx(s);
	return (error);
}

void
cue_watchdog(struct ifnet *ifp)
{
	struct cue_softc	*sc = ifp->if_softc;
	struct cue_chain	*c;
	usbd_status		stat;
	int			s;

	DPRINTFN(5,("%s: %s: enter\n", sc->cue_dev.dv_xname,__func__));

	if (usbd_is_dying(sc->cue_udev))
		return;

	ifp->if_oerrors++;
	printf("%s: watchdog timeout\n", sc->cue_dev.dv_xname);

	s = splusb();
	c = &sc->cue_cdata.cue_tx_chain[0];
	usbd_get_xfer_status(c->cue_xfer, NULL, NULL, NULL, &stat);
	cue_txeof(c->cue_xfer, c, stat);

	if (ifq_empty(&ifp->if_snd) == 0)
		cue_start(ifp);
	splx(s);
}

/*
 * Stop the adapter and free any mbufs allocated to the
 * RX and TX lists.
 */
void
cue_stop(struct cue_softc *sc)
{
	usbd_status		err;
	struct ifnet		*ifp;
	int			i;

	DPRINTFN(10,("%s: %s: enter\n", sc->cue_dev.dv_xname,__func__));

	ifp = GET_IFP(sc);
	ifp->if_timer = 0;
	ifp->if_flags &= ~IFF_RUNNING;
	ifq_clr_oactive(&ifp->if_snd);

	cue_csr_write_1(sc, CUE_ETHCTL, 0);
	cue_reset(sc);
	timeout_del(&sc->cue_stat_ch);

	/* Stop transfers. */
	if (sc->cue_ep[CUE_ENDPT_RX] != NULL) {
		err = usbd_close_pipe(sc->cue_ep[CUE_ENDPT_RX]);
		if (err) {
			printf("%s: close rx pipe failed: %s\n",
			sc->cue_dev.dv_xname, usbd_errstr(err));
		}
		sc->cue_ep[CUE_ENDPT_RX] = NULL;
	}

	if (sc->cue_ep[CUE_ENDPT_TX] != NULL) {
		err = usbd_close_pipe(sc->cue_ep[CUE_ENDPT_TX]);
		if (err) {
			printf("%s: close tx pipe failed: %s\n",
			    sc->cue_dev.dv_xname, usbd_errstr(err));
		}
		sc->cue_ep[CUE_ENDPT_TX] = NULL;
	}

	if (sc->cue_ep[CUE_ENDPT_INTR] != NULL) {
		err = usbd_close_pipe(sc->cue_ep[CUE_ENDPT_INTR]);
		if (err) {
			printf("%s: close intr pipe failed: %s\n",
			    sc->cue_dev.dv_xname, usbd_errstr(err));
		}
		sc->cue_ep[CUE_ENDPT_INTR] = NULL;
	}

	/* Free RX resources. */
	for (i = 0; i < CUE_RX_LIST_CNT; i++) {
		if (sc->cue_cdata.cue_rx_chain[i].cue_mbuf != NULL) {
			m_freem(sc->cue_cdata.cue_rx_chain[i].cue_mbuf);
			sc->cue_cdata.cue_rx_chain[i].cue_mbuf = NULL;
		}
		if (sc->cue_cdata.cue_rx_chain[i].cue_xfer != NULL) {
			usbd_free_xfer(sc->cue_cdata.cue_rx_chain[i].cue_xfer);
			sc->cue_cdata.cue_rx_chain[i].cue_xfer = NULL;
		}
	}

	/* Free TX resources. */
	for (i = 0; i < CUE_TX_LIST_CNT; i++) {
		if (sc->cue_cdata.cue_tx_chain[i].cue_mbuf != NULL) {
			m_freem(sc->cue_cdata.cue_tx_chain[i].cue_mbuf);
			sc->cue_cdata.cue_tx_chain[i].cue_mbuf = NULL;
		}
		if (sc->cue_cdata.cue_tx_chain[i].cue_xfer != NULL) {
			usbd_free_xfer(sc->cue_cdata.cue_tx_chain[i].cue_xfer);
			sc->cue_cdata.cue_tx_chain[i].cue_xfer = NULL;
		}
	}
}
