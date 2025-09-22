/*	$OpenBSD: if_url.c,v 1.90 2024/05/23 03:21:09 jsg Exp $ */
/*	$NetBSD: if_url.c,v 1.6 2002/09/29 10:19:21 martin Exp $	*/
/*
 * Copyright (c) 2001, 2002
 *     Shingo WATANABE <nabe@nabechan.org>.  All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 * 3. Neither the name of the author nor the names of any co-contributors
 *    may be used to endorse or promote products derived from this software
 *    without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE AUTHOR OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 *
 */

/*
 * The RTL8150L(Realtek USB to fast ethernet controller) spec can be found at
 *   ftp://ftp.realtek.com.tw/lancard/data_sheet/8150/8150v14.pdf
 *   ftp://152.104.125.40/lancard/data_sheet/8150/8150v14.pdf
 */

/*
 * TODO:
 *	Interrupt Endpoint support
 *	External PHYs
 */

#include "bpfilter.h"

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/rwlock.h>
#include <sys/mbuf.h>

#include <sys/device.h>

#include <net/if.h>
#include <net/if_media.h>

#if NBPFILTER > 0
#include <net/bpf.h>
#endif

#include <netinet/in.h>
#include <netinet/if_ether.h>

#include <dev/mii/mii.h>
#include <dev/mii/miivar.h>
#include <dev/mii/urlphyreg.h>

#include <dev/usb/usb.h>
#include <dev/usb/usbdi.h>
#include <dev/usb/usbdi_util.h>
#include <dev/usb/usbdevs.h>

#include <dev/usb/if_urlreg.h>

int url_match(struct device *, void *, void *);
void url_attach(struct device *, struct device *, void *);
int url_detach(struct device *, int);

struct cfdriver url_cd = {
	NULL, "url", DV_IFNET
};

const struct cfattach url_ca = {
	sizeof(struct url_softc), url_match, url_attach, url_detach
};

int url_openpipes(struct url_softc *);
int url_rx_list_init(struct url_softc *);
int url_tx_list_init(struct url_softc *);
int url_newbuf(struct url_softc *, struct url_chain *, struct mbuf *);
void url_start(struct ifnet *);
int url_send(struct url_softc *, struct mbuf *, int);
void url_txeof(struct usbd_xfer *, void *, usbd_status);
void url_rxeof(struct usbd_xfer *, void *, usbd_status);
void url_tick(void *);
void url_tick_task(void *);
int url_ioctl(struct ifnet *, u_long, caddr_t);
void url_stop_task(struct url_softc *);
void url_stop(struct ifnet *, int);
void url_watchdog(struct ifnet *);
int url_ifmedia_change(struct ifnet *);
void url_ifmedia_status(struct ifnet *, struct ifmediareq *);
void url_lock_mii(struct url_softc *);
void url_unlock_mii(struct url_softc *);
int url_int_miibus_readreg(struct device *, int, int);
void url_int_miibus_writereg(struct device *, int, int, int);
void url_miibus_statchg(struct device *);
int url_init(struct ifnet *);
void url_iff(struct url_softc *);
void url_reset(struct url_softc *);

int url_csr_read_1(struct url_softc *, int);
int url_csr_read_2(struct url_softc *, int);
int url_csr_write_1(struct url_softc *, int, int);
int url_csr_write_2(struct url_softc *, int, int);
int url_csr_write_4(struct url_softc *, int, int);
int url_mem(struct url_softc *, int, int, void *, int);

/* Macros */
#ifdef URL_DEBUG
#define DPRINTF(x)	do { if (urldebug) printf x; } while (0)
#define DPRINTFN(n,x)	do { if (urldebug >= (n)) printf x; } while (0)
int urldebug = 0;
#else
#define DPRINTF(x)
#define DPRINTFN(n,x)
#endif

#define	URL_SETBIT(sc, reg, x)	\
	url_csr_write_1(sc, reg, url_csr_read_1(sc, reg) | (x))

#define	URL_SETBIT2(sc, reg, x)	\
	url_csr_write_2(sc, reg, url_csr_read_2(sc, reg) | (x))

#define	URL_CLRBIT(sc, reg, x)	\
	url_csr_write_1(sc, reg, url_csr_read_1(sc, reg) & ~(x))

#define	URL_CLRBIT2(sc, reg, x)	\
	url_csr_write_2(sc, reg, url_csr_read_2(sc, reg) & ~(x))

static const struct url_type {
	struct usb_devno url_dev;
	u_int16_t url_flags;
#define URL_EXT_PHY	0x0001
} url_devs [] = {
	{{ USB_VENDOR_ABOCOM, USB_PRODUCT_ABOCOM_LCS8138TX}, 0},
	{{ USB_VENDOR_MELCO, USB_PRODUCT_MELCO_LUAKTX }, 0},
	{{ USB_VENDOR_MICRONET, USB_PRODUCT_MICRONET_SP128AR}, 0},
	{{ USB_VENDOR_OQO, USB_PRODUCT_OQO_ETHER01}, 0},
	{{ USB_VENDOR_REALTEK, USB_PRODUCT_REALTEK_RTL8150}, 0},
	{{ USB_VENDOR_REALTEK, USB_PRODUCT_REALTEK_RTL8151}, 0},
	{{ USB_VENDOR_ZYXEL, USB_PRODUCT_ZYXEL_PRESTIGE}, 0}
};
#define url_lookup(v, p) ((struct url_type *)usb_lookup(url_devs, v, p))


/* Probe */
int
url_match(struct device *parent, void *match, void *aux)
{
	struct usb_attach_arg *uaa = aux;

	if (uaa->iface == NULL || uaa->configno != URL_CONFIG_NO)
		return (UMATCH_NONE);

	return (url_lookup(uaa->vendor, uaa->product) != NULL ?
		UMATCH_VENDOR_PRODUCT : UMATCH_NONE);
}
/* Attach */
void
url_attach(struct device *parent, struct device *self, void *aux)
{
	struct url_softc *sc = (struct url_softc *)self;
	struct usb_attach_arg *uaa = aux;
	struct usbd_device *dev = uaa->device;
	struct usbd_interface *iface;
	usbd_status err;
	usb_interface_descriptor_t *id;
	usb_endpoint_descriptor_t *ed;
	char *devname = sc->sc_dev.dv_xname;
	struct ifnet *ifp;
	struct mii_data *mii;
	u_char eaddr[ETHER_ADDR_LEN];
	int i, s;

	sc->sc_udev = dev;

	usb_init_task(&sc->sc_tick_task, url_tick_task, sc,
	    USB_TASK_TYPE_GENERIC);
	rw_init(&sc->sc_mii_lock, "urlmii");
	usb_init_task(&sc->sc_stop_task, (void (*)(void *)) url_stop_task, sc,
	    USB_TASK_TYPE_GENERIC);

	/* get control interface */
	err = usbd_device2interface_handle(dev, URL_IFACE_INDEX, &iface);
	if (err) {
		printf("%s: failed to get interface, err=%s\n", devname,
		       usbd_errstr(err));
		goto bad;
	}

	sc->sc_ctl_iface = iface;
	sc->sc_flags = url_lookup(uaa->vendor, uaa->product)->url_flags;

	/* get interface descriptor */
	id = usbd_get_interface_descriptor(sc->sc_ctl_iface);

	/* find endpoints */
	sc->sc_bulkin_no = sc->sc_bulkout_no = sc->sc_intrin_no = -1;
	for (i = 0; i < id->bNumEndpoints; i++) {
		ed = usbd_interface2endpoint_descriptor(sc->sc_ctl_iface, i);
		if (ed == NULL) {
			printf("%s: couldn't get endpoint %d\n", devname, i);
			goto bad;
		}
		if (UE_GET_XFERTYPE(ed->bmAttributes) == UE_BULK &&
		    UE_GET_DIR(ed->bEndpointAddress) == UE_DIR_IN)
			sc->sc_bulkin_no = ed->bEndpointAddress; /* RX */
		else if (UE_GET_XFERTYPE(ed->bmAttributes) == UE_BULK &&
			 UE_GET_DIR(ed->bEndpointAddress) == UE_DIR_OUT)
			sc->sc_bulkout_no = ed->bEndpointAddress; /* TX */
		else if (UE_GET_XFERTYPE(ed->bmAttributes) == UE_INTERRUPT &&
			 UE_GET_DIR(ed->bEndpointAddress) == UE_DIR_IN)
			sc->sc_intrin_no = ed->bEndpointAddress; /* Status */
	}

	if (sc->sc_bulkin_no == -1 || sc->sc_bulkout_no == -1 ||
	    sc->sc_intrin_no == -1) {
		printf("%s: missing endpoint\n", devname);
		goto bad;
	}

	s = splnet();

	/* reset the adapter */
	url_reset(sc);

	/* Get Ethernet Address */
	err = url_mem(sc, URL_CMD_READMEM, URL_IDR0, (void *)eaddr,
		      ETHER_ADDR_LEN);
	if (err) {
		printf("%s: read MAC address failed\n", devname);
		splx(s);
		goto bad;
	}

	/* Print Ethernet Address */
	printf("%s: address %s\n", devname, ether_sprintf(eaddr));

	bcopy(eaddr, (char *)&sc->sc_ac.ac_enaddr, ETHER_ADDR_LEN);
	/* initialize interface information */
	ifp = GET_IFP(sc);
	ifp->if_softc = sc;
	strlcpy(ifp->if_xname, devname, IFNAMSIZ);
	ifp->if_flags = IFF_BROADCAST | IFF_SIMPLEX | IFF_MULTICAST;
	ifp->if_start = url_start;
	ifp->if_ioctl = url_ioctl;
	ifp->if_watchdog = url_watchdog;

	ifp->if_capabilities = IFCAP_VLAN_MTU;

	/*
	 * Do ifmedia setup.
	 */
	mii = &sc->sc_mii;
	mii->mii_ifp = ifp;
	mii->mii_readreg = url_int_miibus_readreg;
	mii->mii_writereg = url_int_miibus_writereg;
#if 0
	if (sc->sc_flags & URL_EXT_PHY) {
		mii->mii_readreg = url_ext_miibus_readreg;
		mii->mii_writereg = url_ext_miibus_writereg;
	}
#endif
	mii->mii_statchg = url_miibus_statchg;
	mii->mii_flags = MIIF_AUTOTSLEEP;
	ifmedia_init(&mii->mii_media, 0,
		     url_ifmedia_change, url_ifmedia_status);
	mii_attach(self, mii, 0xffffffff, MII_PHY_ANY, MII_OFFSET_ANY, 0);
	if (LIST_FIRST(&mii->mii_phys) == NULL) {
		ifmedia_add(&mii->mii_media, IFM_ETHER | IFM_NONE, 0, NULL);
		ifmedia_set(&mii->mii_media, IFM_ETHER | IFM_NONE);
	} else
		ifmedia_set(&mii->mii_media, IFM_ETHER | IFM_AUTO);

	/* attach the interface */
	if_attach(ifp);
	ether_ifattach(ifp);

	timeout_set(&sc->sc_stat_ch, url_tick, sc);

	splx(s);

	return;

 bad:
	usbd_deactivate(sc->sc_udev);
}

/* detach */
int
url_detach(struct device *self, int flags)
{
	struct url_softc *sc = (struct url_softc *)self;
	struct ifnet *ifp = GET_IFP(sc);
	int s;

	DPRINTF(("%s: %s: enter\n", sc->sc_dev.dv_xname, __func__));

	if (timeout_initialized(&sc->sc_stat_ch))
		timeout_del(&sc->sc_stat_ch);

	/* Remove any pending tasks */
	usb_rem_task(sc->sc_udev, &sc->sc_tick_task);
	usb_rem_task(sc->sc_udev, &sc->sc_stop_task);

	s = splusb();

	if (--sc->sc_refcnt >= 0) {
		/* Wait for processes to go away */
		usb_detach_wait(&sc->sc_dev);
	}

	if (ifp->if_flags & IFF_RUNNING)
		url_stop(GET_IFP(sc), 1);

	mii_detach(&sc->sc_mii, MII_PHY_ANY, MII_OFFSET_ANY);
	ifmedia_delete_instance(&sc->sc_mii.mii_media, IFM_INST_ANY);
	if (ifp->if_softc != NULL) {
		ether_ifdetach(ifp);
		if_detach(ifp);
	}

#ifdef DIAGNOSTIC
	if (sc->sc_pipe_tx != NULL)
		printf("%s: detach has active tx endpoint.\n",
		       sc->sc_dev.dv_xname);
	if (sc->sc_pipe_rx != NULL)
		printf("%s: detach has active rx endpoint.\n",
		       sc->sc_dev.dv_xname);
	if (sc->sc_pipe_intr != NULL)
		printf("%s: detach has active intr endpoint.\n",
		       sc->sc_dev.dv_xname);
#endif

	splx(s);

	return (0);
}

/* read/write memory */
int
url_mem(struct url_softc *sc, int cmd, int offset, void *buf, int len)
{
	usb_device_request_t req;
	usbd_status err;

	if (sc == NULL)
		return (0);

	DPRINTFN(0x200,
		("%s: %s: enter\n", sc->sc_dev.dv_xname, __func__));

	if (usbd_is_dying(sc->sc_udev))
		return (0);

	if (cmd == URL_CMD_READMEM)
		req.bmRequestType = UT_READ_VENDOR_DEVICE;
	else
		req.bmRequestType = UT_WRITE_VENDOR_DEVICE;
	req.bRequest = URL_REQ_MEM;
	USETW(req.wValue, offset);
	USETW(req.wIndex, 0x0000);
	USETW(req.wLength, len);

	sc->sc_refcnt++;
	err = usbd_do_request(sc->sc_udev, &req, buf);
	if (--sc->sc_refcnt < 0)
		usb_detach_wakeup(&sc->sc_dev);
	if (err) {
		DPRINTF(("%s: url_mem(): %s failed. off=%04x, err=%d\n",
			 sc->sc_dev.dv_xname,
			 cmd == URL_CMD_READMEM ? "read" : "write",
			 offset, err));
	}

	return (err);
}

/* read 1byte from register */
int
url_csr_read_1(struct url_softc *sc, int reg)
{
	u_int8_t val = 0;

	DPRINTFN(0x100,
		 ("%s: %s: enter\n", sc->sc_dev.dv_xname, __func__));

	return (url_mem(sc, URL_CMD_READMEM, reg, &val, 1) ? 0 : val);
}

/* read 2bytes from register */
int
url_csr_read_2(struct url_softc *sc, int reg)
{
	uWord val;

	DPRINTFN(0x100,
		 ("%s: %s: enter\n", sc->sc_dev.dv_xname, __func__));

	USETW(val, 0);
	return (url_mem(sc, URL_CMD_READMEM, reg, &val, 2) ? 0 : UGETW(val));
}

/* write 1byte to register */
int
url_csr_write_1(struct url_softc *sc, int reg, int aval)
{
	u_int8_t val = aval;

	DPRINTFN(0x100,
		 ("%s: %s: enter\n", sc->sc_dev.dv_xname, __func__));

	return (url_mem(sc, URL_CMD_WRITEMEM, reg, &val, 1) ? -1 : 0);
}

/* write 2bytes to register */
int
url_csr_write_2(struct url_softc *sc, int reg, int aval)
{
	uWord val;

	DPRINTFN(0x100,
		 ("%s: %s: enter\n", sc->sc_dev.dv_xname, __func__));

	USETW(val, aval);

	return (url_mem(sc, URL_CMD_WRITEMEM, reg, &val, 2) ? -1 : 0);
}

/* write 4bytes to register */
int
url_csr_write_4(struct url_softc *sc, int reg, int aval)
{
	uDWord val;

	DPRINTFN(0x100,
		 ("%s: %s: enter\n", sc->sc_dev.dv_xname, __func__));

	USETDW(val, aval);

	return (url_mem(sc, URL_CMD_WRITEMEM, reg, &val, 4) ? -1 : 0);
}

int
url_init(struct ifnet *ifp)
{
	struct url_softc *sc = ifp->if_softc;
	struct mii_data *mii = GET_MII(sc);
	u_char *eaddr;
	int i, s;

	DPRINTF(("%s: %s: enter\n", sc->sc_dev.dv_xname, __func__));

	s = splnet();

	/* Cancel pending I/O and free all TX/RX buffers */
	url_stop(ifp, 1);

	eaddr = sc->sc_ac.ac_enaddr;
	for (i = 0; i < ETHER_ADDR_LEN; i++)
		url_csr_write_1(sc, URL_IDR0 + i, eaddr[i]);

	/* Init transmission control register */
	URL_CLRBIT(sc, URL_TCR,
		   URL_TCR_TXRR1 | URL_TCR_TXRR0 |
		   URL_TCR_IFG1 | URL_TCR_IFG0 |
		   URL_TCR_NOCRC);

	/* Init receive control register */
	URL_SETBIT2(sc, URL_RCR, URL_RCR_TAIL);

	/* Initialize transmit ring */
	if (url_tx_list_init(sc) == ENOBUFS) {
		printf("%s: tx list init failed\n", sc->sc_dev.dv_xname);
		splx(s);
		return (EIO);
	}

	/* Initialize receive ring */
	if (url_rx_list_init(sc) == ENOBUFS) {
		printf("%s: rx list init failed\n", sc->sc_dev.dv_xname);
		splx(s);
		return (EIO);
	}

	/* Program promiscuous mode and multicast filters */
	url_iff(sc);

	/* Enable RX and TX */
	URL_SETBIT(sc, URL_CR, URL_CR_TE | URL_CR_RE);

	mii_mediachg(mii);

	if (sc->sc_pipe_tx == NULL || sc->sc_pipe_rx == NULL) {
		if (url_openpipes(sc)) {
			splx(s);
			return (EIO);
		}
	}

	ifp->if_flags |= IFF_RUNNING;
	ifq_clr_oactive(&ifp->if_snd);

	splx(s);

	timeout_add_sec(&sc->sc_stat_ch, 1);

	return (0);
}

void
url_reset(struct url_softc *sc)
{
	int i;

	DPRINTF(("%s: %s: enter\n", sc->sc_dev.dv_xname, __func__));

	if (usbd_is_dying(sc->sc_udev))
		return;

	URL_SETBIT(sc, URL_CR, URL_CR_SOFT_RST);

	for (i = 0; i < URL_TX_TIMEOUT; i++) {
		if (!(url_csr_read_1(sc, URL_CR) & URL_CR_SOFT_RST))
			break;
		delay(10);	/* XXX */
	}

	delay(10000);		/* XXX */
}

void
url_iff(struct url_softc *sc)
{
	struct ifnet *ifp = GET_IFP(sc);
	struct arpcom *ac = &sc->sc_ac;
	struct ether_multi *enm;
	struct ether_multistep step;
	u_int32_t hashes[2];
	u_int16_t rcr;
	int h = 0;

	DPRINTF(("%s: %s: enter\n", sc->sc_dev.dv_xname, __func__));

	if (usbd_is_dying(sc->sc_udev))
		return;

	rcr = url_csr_read_2(sc, URL_RCR);
	rcr &= ~(URL_RCR_AAM | URL_RCR_AAP | URL_RCR_AB | URL_RCR_AD |
	    URL_RCR_AM);
	bzero(hashes, sizeof(hashes));
	ifp->if_flags &= ~IFF_ALLMULTI;

	/*
	 * Always accept broadcast frames.
	 * Always accept frames destined to our station address.
	 */
	rcr |= URL_RCR_AB | URL_RCR_AD;

	if (ifp->if_flags & IFF_PROMISC || ac->ac_multirangecnt > 0) {
		ifp->if_flags |= IFF_ALLMULTI;
		rcr |= URL_RCR_AAM;
		if (ifp->if_flags & IFF_PROMISC)
			rcr |= URL_RCR_AAP;
	} else {
		rcr |= URL_RCR_AM;

		/* now program new ones */
		ETHER_FIRST_MULTI(step, ac, enm);
		while (enm != NULL) {
			h = ether_crc32_be(enm->enm_addrlo,
			    ETHER_ADDR_LEN) >> 26;

			if (h < 32)
				hashes[0] |= (1 << h);
			else
				hashes[1] |= (1 << (h - 32));

			ETHER_NEXT_MULTI(step, enm);
		}
	}

	url_csr_write_4(sc, URL_MAR0, hashes[0]);
	url_csr_write_4(sc, URL_MAR4, hashes[1]);
	url_csr_write_2(sc, URL_RCR, rcr);
}

int
url_openpipes(struct url_softc *sc)
{
	struct url_chain *c;
	usbd_status err;
	int i;
	int error = 0;

	if (usbd_is_dying(sc->sc_udev))
		return (EIO);

	sc->sc_refcnt++;

	/* Open RX pipe */
	err = usbd_open_pipe(sc->sc_ctl_iface, sc->sc_bulkin_no,
			     USBD_EXCLUSIVE_USE, &sc->sc_pipe_rx);
	if (err) {
		printf("%s: open rx pipe failed: %s\n",
		       sc->sc_dev.dv_xname, usbd_errstr(err));
		error = EIO;
		goto done;
	}

	/* Open TX pipe */
	err = usbd_open_pipe(sc->sc_ctl_iface, sc->sc_bulkout_no,
			     USBD_EXCLUSIVE_USE, &sc->sc_pipe_tx);
	if (err) {
		printf("%s: open tx pipe failed: %s\n",
		       sc->sc_dev.dv_xname, usbd_errstr(err));
		error = EIO;
		goto done;
	}

#if 0
	/* XXX: interrupt endpoint is not yet supported */
	/* Open Interrupt pipe */
	err = usbd_open_pipe_intr(sc->sc_ctl_iface, sc->sc_intrin_no,
				  0, &sc->sc_pipe_intr, sc,
				  &sc->sc_cdata.url_ibuf, URL_INTR_PKGLEN,
				  url_intr, URL_INTR_INTERVAL);
	if (err) {
		printf("%s: open intr pipe failed: %s\n",
		       sc->sc_dev.dv_xname, usbd_errstr(err));
		error = EIO;
		goto done;
	}
#endif


	/* Start up the receive pipe. */
	for (i = 0; i < URL_RX_LIST_CNT; i++) {
		c = &sc->sc_cdata.url_rx_chain[i];
		usbd_setup_xfer(c->url_xfer, sc->sc_pipe_rx,
				c, c->url_buf, URL_BUFSZ,
				USBD_SHORT_XFER_OK | USBD_NO_COPY,
				USBD_NO_TIMEOUT, url_rxeof);
		(void)usbd_transfer(c->url_xfer);
		DPRINTF(("%s: %s: start read\n", sc->sc_dev.dv_xname,
			 __func__));
	}

 done:
	if (--sc->sc_refcnt < 0)
		usb_detach_wakeup(&sc->sc_dev);

	return (error);
}

int
url_newbuf(struct url_softc *sc, struct url_chain *c, struct mbuf *m)
{
	struct mbuf *m_new = NULL;

	DPRINTF(("%s: %s: enter\n", sc->sc_dev.dv_xname, __func__));

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

	m_adj(m_new, ETHER_ALIGN);
	c->url_mbuf = m_new;

	return (0);
}


int
url_rx_list_init(struct url_softc *sc)
{
	struct url_cdata *cd;
	struct url_chain *c;
	int i;

	DPRINTF(("%s: %s: enter\n", sc->sc_dev.dv_xname, __func__));

	cd = &sc->sc_cdata;
	for (i = 0; i < URL_RX_LIST_CNT; i++) {
		c = &cd->url_rx_chain[i];
		c->url_sc = sc;
		c->url_idx = i;
		if (url_newbuf(sc, c, NULL) == ENOBUFS)
			return (ENOBUFS);
		if (c->url_xfer == NULL) {
			c->url_xfer = usbd_alloc_xfer(sc->sc_udev);
			if (c->url_xfer == NULL)
				return (ENOBUFS);
			c->url_buf = usbd_alloc_buffer(c->url_xfer, URL_BUFSZ);
			if (c->url_buf == NULL) {
				usbd_free_xfer(c->url_xfer);
				return (ENOBUFS);
			}
		}
	}

	return (0);
}

int
url_tx_list_init(struct url_softc *sc)
{
	struct url_cdata *cd;
	struct url_chain *c;
	int i;

	DPRINTF(("%s: %s: enter\n", sc->sc_dev.dv_xname, __func__));

	cd = &sc->sc_cdata;
	for (i = 0; i < URL_TX_LIST_CNT; i++) {
		c = &cd->url_tx_chain[i];
		c->url_sc = sc;
		c->url_idx = i;
		c->url_mbuf = NULL;
		if (c->url_xfer == NULL) {
			c->url_xfer = usbd_alloc_xfer(sc->sc_udev);
			if (c->url_xfer == NULL)
				return (ENOBUFS);
			c->url_buf = usbd_alloc_buffer(c->url_xfer, URL_BUFSZ);
			if (c->url_buf == NULL) {
				usbd_free_xfer(c->url_xfer);
				return (ENOBUFS);
			}
		}
	}

	return (0);
}

void
url_start(struct ifnet *ifp)
{
	struct url_softc *sc = ifp->if_softc;
	struct mbuf *m_head = NULL;

	DPRINTF(("%s: %s: enter, link=%d\n", sc->sc_dev.dv_xname,
		 __func__, sc->sc_link));

	if (usbd_is_dying(sc->sc_udev))
		return;

	if (!sc->sc_link)
		return;

	if (ifq_is_oactive(&ifp->if_snd))
		return;

	m_head = ifq_deq_begin(&ifp->if_snd);
	if (m_head == NULL)
		return;

	if (url_send(sc, m_head, 0)) {
		ifq_deq_rollback(&ifp->if_snd, m_head);
		ifq_set_oactive(&ifp->if_snd);
		return;
	}

	ifq_deq_commit(&ifp->if_snd, m_head);

#if NBPFILTER > 0
	if (ifp->if_bpf)
		bpf_mtap(ifp->if_bpf, m_head, BPF_DIRECTION_OUT);
#endif

	ifq_set_oactive(&ifp->if_snd);

	/* Set a timeout in case the chip goes out to lunch. */
	ifp->if_timer = 5;
}

int
url_send(struct url_softc *sc, struct mbuf *m, int idx)
{
	int total_len;
	struct url_chain *c;
	usbd_status err;

	DPRINTF(("%s: %s: enter\n", sc->sc_dev.dv_xname,__func__));

	c = &sc->sc_cdata.url_tx_chain[idx];

	/* Copy the mbuf data into a contiguous buffer */
	m_copydata(m, 0, m->m_pkthdr.len, c->url_buf);
	c->url_mbuf = m;
	total_len = m->m_pkthdr.len;

	if (total_len < URL_MIN_FRAME_LEN) {
		bzero(c->url_buf + total_len, URL_MIN_FRAME_LEN - total_len);
		total_len = URL_MIN_FRAME_LEN;
	}
	usbd_setup_xfer(c->url_xfer, sc->sc_pipe_tx, c, c->url_buf, total_len,
			USBD_FORCE_SHORT_XFER | USBD_NO_COPY,
			URL_TX_TIMEOUT, url_txeof);

	/* Transmit */
	sc->sc_refcnt++;
	err = usbd_transfer(c->url_xfer);
	if (--sc->sc_refcnt < 0)
		usb_detach_wakeup(&sc->sc_dev);
	if (err != USBD_IN_PROGRESS) {
		printf("%s: url_send error=%s\n", sc->sc_dev.dv_xname,
		       usbd_errstr(err));
		/* Stop the interface */
		usb_add_task(sc->sc_udev, &sc->sc_stop_task);
		return (EIO);
	}

	DPRINTF(("%s: %s: send %d bytes\n", sc->sc_dev.dv_xname,
		 __func__, total_len));

	sc->sc_cdata.url_tx_cnt++;

	return (0);
}

void
url_txeof(struct usbd_xfer *xfer, void *priv, usbd_status status)
{
	struct url_chain *c = priv;
	struct url_softc *sc = c->url_sc;
	struct ifnet *ifp = GET_IFP(sc);
	int s;

	if (usbd_is_dying(sc->sc_udev))
		return;

	s = splnet();

	DPRINTF(("%s: %s: enter\n", sc->sc_dev.dv_xname, __func__));

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
		if (status == USBD_STALLED) {
			sc->sc_refcnt++;
			usbd_clear_endpoint_stall_async(sc->sc_pipe_tx);
			if (--sc->sc_refcnt < 0)
				usb_detach_wakeup(&sc->sc_dev);
		}
		splx(s);
		return;
	}

	m_freem(c->url_mbuf);
	c->url_mbuf = NULL;

	if (ifq_empty(&ifp->if_snd) == 0)
		url_start(ifp);

	splx(s);
}

void
url_rxeof(struct usbd_xfer *xfer, void *priv, usbd_status status)
{
	struct url_chain *c = priv;
	struct url_softc *sc = c->url_sc;
	struct ifnet *ifp = GET_IFP(sc);
	struct mbuf_list ml = MBUF_LIST_INITIALIZER();
	struct mbuf *m;
	u_int32_t total_len;
	url_rxhdr_t rxhdr;
	int s;

	DPRINTF(("%s: %s: enter\n", sc->sc_dev.dv_xname,__func__));

	if (usbd_is_dying(sc->sc_udev))
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
		if (status == USBD_STALLED) {
			sc->sc_refcnt++;
			usbd_clear_endpoint_stall_async(sc->sc_pipe_rx);
			if (--sc->sc_refcnt < 0)
				usb_detach_wakeup(&sc->sc_dev);
		}
		goto done;
	}

	usbd_get_xfer_status(xfer, NULL, NULL, &total_len, NULL);

	memcpy(mtod(c->url_mbuf, char *), c->url_buf, total_len);

	if (total_len <= ETHER_CRC_LEN) {
		ifp->if_ierrors++;
		goto done;
	}

	memcpy(&rxhdr, c->url_buf + total_len - ETHER_CRC_LEN, sizeof(rxhdr));

	DPRINTF(("%s: RX Status: %dbytes%s%s%s%s packets\n",
		 sc->sc_dev.dv_xname,
		 UGETW(rxhdr) & URL_RXHDR_BYTEC_MASK,
		 UGETW(rxhdr) & URL_RXHDR_VALID_MASK ? ", Valid" : "",
		 UGETW(rxhdr) & URL_RXHDR_RUNTPKT_MASK ? ", Runt" : "",
		 UGETW(rxhdr) & URL_RXHDR_PHYPKT_MASK ? ", Physical match" : "",
		 UGETW(rxhdr) & URL_RXHDR_MCASTPKT_MASK ? ", Multicast" : ""));

	if ((UGETW(rxhdr) & URL_RXHDR_VALID_MASK) == 0) {
		ifp->if_ierrors++;
		goto done;
	}

	total_len -= ETHER_CRC_LEN;

	m = c->url_mbuf;
	m->m_pkthdr.len = m->m_len = total_len;
	ml_enqueue(&ml, m);

	if (url_newbuf(sc, c, NULL) == ENOBUFS) {
		ifp->if_ierrors++;
		goto done;
	}

	DPRINTF(("%s: %s: deliver %d\n", sc->sc_dev.dv_xname,
		 __func__, m->m_len));

	s = splnet();
	if_input(ifp, &ml);
	splx(s);

 done:
	/* Setup new transfer */
	usbd_setup_xfer(xfer, sc->sc_pipe_rx, c, c->url_buf, URL_BUFSZ,
			USBD_SHORT_XFER_OK | USBD_NO_COPY,
			USBD_NO_TIMEOUT, url_rxeof);
	sc->sc_refcnt++;
	usbd_transfer(xfer);
	if (--sc->sc_refcnt < 0)
		usb_detach_wakeup(&sc->sc_dev);

	DPRINTF(("%s: %s: start rx\n", sc->sc_dev.dv_xname, __func__));
}

int
url_ioctl(struct ifnet *ifp, u_long cmd, caddr_t data)
{
	struct url_softc *sc = ifp->if_softc;
	struct ifreq *ifr = (struct ifreq *)data;
	int s, error = 0;

	DPRINTF(("%s: %s: enter\n", sc->sc_dev.dv_xname, __func__));

	if (usbd_is_dying(sc->sc_udev))
		return ENXIO;

	s = splnet();

	switch (cmd) {
	case SIOCSIFADDR:
		ifp->if_flags |= IFF_UP;
		if (!(ifp->if_flags & IFF_RUNNING))
			url_init(ifp);
		break;

	case SIOCSIFFLAGS:
		if (ifp->if_flags & IFF_UP) {
			if (ifp->if_flags & IFF_RUNNING)
				error = ENETRESET;
			else
				url_init(ifp);
		} else {
			if (ifp->if_flags & IFF_RUNNING)
				url_stop(ifp, 1);
		}
		break;

	case SIOCGIFMEDIA:
	case SIOCSIFMEDIA:
		error = ifmedia_ioctl(ifp, ifr, &sc->sc_mii.mii_media, cmd);
		break;

	default:
		error = ether_ioctl(ifp, &sc->sc_ac, cmd, data);
	}

	if (error == ENETRESET) {
		if (ifp->if_flags & IFF_RUNNING)
			url_iff(sc);
		error = 0;
	}

	splx(s);
	return (error);
}

void
url_watchdog(struct ifnet *ifp)
{
	struct url_softc *sc = ifp->if_softc;
	struct url_chain *c;
	usbd_status stat;
	int s;

	DPRINTF(("%s: %s: enter\n", sc->sc_dev.dv_xname, __func__));

	ifp->if_oerrors++;
	printf("%s: watchdog timeout\n", sc->sc_dev.dv_xname);

	s = splusb();
	c = &sc->sc_cdata.url_tx_chain[0];
	usbd_get_xfer_status(c->url_xfer, NULL, NULL, NULL, &stat);
	url_txeof(c->url_xfer, c, stat);

	if (ifq_empty(&ifp->if_snd) == 0)
		url_start(ifp);
	splx(s);
}

void
url_stop_task(struct url_softc *sc)
{
	url_stop(GET_IFP(sc), 1);
}

/* Stop the adapter and free any mbufs allocated to the RX and TX lists. */
void
url_stop(struct ifnet *ifp, int disable)
{
	struct url_softc *sc = ifp->if_softc;
	usbd_status err;
	int i;

	DPRINTF(("%s: %s: enter\n", sc->sc_dev.dv_xname, __func__));

	ifp->if_timer = 0;
	ifp->if_flags &= ~IFF_RUNNING;
	ifq_clr_oactive(&ifp->if_snd);

	url_reset(sc);

	timeout_del(&sc->sc_stat_ch);

	/* Stop transfers */
	/* RX endpoint */
	if (sc->sc_pipe_rx != NULL) {
		err = usbd_close_pipe(sc->sc_pipe_rx);
		if (err)
			printf("%s: close rx pipe failed: %s\n",
			       sc->sc_dev.dv_xname, usbd_errstr(err));
		sc->sc_pipe_rx = NULL;
	}

	/* TX endpoint */
	if (sc->sc_pipe_tx != NULL) {
		err = usbd_close_pipe(sc->sc_pipe_tx);
		if (err)
			printf("%s: close tx pipe failed: %s\n",
			       sc->sc_dev.dv_xname, usbd_errstr(err));
		sc->sc_pipe_tx = NULL;
	}

#if 0
	/* XXX: Interrupt endpoint is not yet supported!! */
	/* Interrupt endpoint */
	if (sc->sc_pipe_intr != NULL) {
		err = usbd_close_pipe(sc->sc_pipe_intr);
		if (err)
			printf("%s: close intr pipe failed: %s\n",
			       sc->sc_dev.dv_xname, usbd_errstr(err));
		sc->sc_pipe_intr = NULL;
	}
#endif

	/* Free RX resources. */
	for (i = 0; i < URL_RX_LIST_CNT; i++) {
		if (sc->sc_cdata.url_rx_chain[i].url_mbuf != NULL) {
			m_freem(sc->sc_cdata.url_rx_chain[i].url_mbuf);
			sc->sc_cdata.url_rx_chain[i].url_mbuf = NULL;
		}
		if (sc->sc_cdata.url_rx_chain[i].url_xfer != NULL) {
			usbd_free_xfer(sc->sc_cdata.url_rx_chain[i].url_xfer);
			sc->sc_cdata.url_rx_chain[i].url_xfer = NULL;
		}
	}

	/* Free TX resources. */
	for (i = 0; i < URL_TX_LIST_CNT; i++) {
		if (sc->sc_cdata.url_tx_chain[i].url_mbuf != NULL) {
			m_freem(sc->sc_cdata.url_tx_chain[i].url_mbuf);
			sc->sc_cdata.url_tx_chain[i].url_mbuf = NULL;
		}
		if (sc->sc_cdata.url_tx_chain[i].url_xfer != NULL) {
			usbd_free_xfer(sc->sc_cdata.url_tx_chain[i].url_xfer);
			sc->sc_cdata.url_tx_chain[i].url_xfer = NULL;
		}
	}

	sc->sc_link = 0;
}

/* Set media options */
int
url_ifmedia_change(struct ifnet *ifp)
{
	struct url_softc *sc = ifp->if_softc;
	struct mii_data *mii = GET_MII(sc);

	DPRINTF(("%s: %s: enter\n", sc->sc_dev.dv_xname, __func__));

	if (usbd_is_dying(sc->sc_udev))
		return (0);

	sc->sc_link = 0;
	if (mii->mii_instance) {
		struct mii_softc *miisc;
		LIST_FOREACH(miisc, &mii->mii_phys, mii_list)
			mii_phy_reset(miisc);
	}

	return (mii_mediachg(mii));
}

/* Report current media status. */
void
url_ifmedia_status(struct ifnet *ifp, struct ifmediareq *ifmr)
{
	struct url_softc *sc = ifp->if_softc;
	struct mii_data *mii = GET_MII(sc);

	DPRINTF(("%s: %s: enter\n", sc->sc_dev.dv_xname, __func__));

	if (usbd_is_dying(sc->sc_udev))
		return;

	if ((ifp->if_flags & IFF_RUNNING) == 0) {
		ifmr->ifm_active = IFM_ETHER | IFM_NONE;
		ifmr->ifm_status = 0;
		return;
	}

	mii_pollstat(mii);
	ifmr->ifm_active = mii->mii_media_active;
	ifmr->ifm_status = mii->mii_media_status;
}

void
url_tick(void *xsc)
{
	struct url_softc *sc = xsc;

	if (sc == NULL)
		return;

	DPRINTFN(0xff, ("%s: %s: enter\n", sc->sc_dev.dv_xname,
			__func__));

	if (usbd_is_dying(sc->sc_udev))
		return;

	/* Perform periodic stuff in process context */
	usb_add_task(sc->sc_udev, &sc->sc_tick_task);
}

void
url_tick_task(void *xsc)
{
	struct url_softc *sc = xsc;
	struct ifnet *ifp;
	struct mii_data *mii;
	int s;

	if (sc == NULL)
		return;

	DPRINTFN(0xff, ("%s: %s: enter\n", sc->sc_dev.dv_xname,
			__func__));

	if (usbd_is_dying(sc->sc_udev))
		return;

	ifp = GET_IFP(sc);
	mii = GET_MII(sc);

	if (mii == NULL)
		return;

	s = splnet();

	mii_tick(mii);
	if (!sc->sc_link && mii->mii_media_status & IFM_ACTIVE &&
	    IFM_SUBTYPE(mii->mii_media_active) != IFM_NONE) {
		DPRINTF(("%s: %s: got link\n",
			 sc->sc_dev.dv_xname, __func__));
		sc->sc_link++;
		if (ifq_empty(&ifp->if_snd) == 0)
			   url_start(ifp);
	}

	timeout_add_sec(&sc->sc_stat_ch, 1);

	splx(s);
}

/* Get exclusive access to the MII registers */
void
url_lock_mii(struct url_softc *sc)
{
	DPRINTFN(0xff, ("%s: %s: enter\n", sc->sc_dev.dv_xname,
			__func__));

	sc->sc_refcnt++;
	rw_enter_write(&sc->sc_mii_lock);
}

void
url_unlock_mii(struct url_softc *sc)
{
	DPRINTFN(0xff, ("%s: %s: enter\n", sc->sc_dev.dv_xname,
		       __func__));

	rw_exit_write(&sc->sc_mii_lock);
	if (--sc->sc_refcnt < 0)
		usb_detach_wakeup(&sc->sc_dev);
}

int
url_int_miibus_readreg(struct device *dev, int phy, int reg)
{
	struct url_softc *sc;
	u_int16_t val;

	if (dev == NULL)
		return (0);

	sc = (void *)dev;

	DPRINTFN(0xff, ("%s: %s: enter, phy=%d reg=0x%04x\n",
		 sc->sc_dev.dv_xname, __func__, phy, reg));

	if (usbd_is_dying(sc->sc_udev)) {
#ifdef DIAGNOSTIC
		printf("%s: %s: dying\n", sc->sc_dev.dv_xname,
		       __func__);
#endif
		return (0);
	}

	/* XXX: one PHY only for the RTL8150 internal PHY */
	if (phy != 0) {
		DPRINTFN(0xff, ("%s: %s: phy=%d is not supported\n",
			 sc->sc_dev.dv_xname, __func__, phy));
		return (0);
	}

	url_lock_mii(sc);

	switch (reg) {
	case MII_BMCR:		/* Control Register */
		reg = URL_BMCR;
		break;
	case MII_BMSR:		/* Status Register */
		reg = URL_BMSR;
		break;
	case MII_PHYIDR1:
	case MII_PHYIDR2:
		val = 0;
		goto R_DONE;
		break;
	case MII_ANAR:		/* Autonegotiation advertisement */
		reg = URL_ANAR;
		break;
	case MII_ANLPAR:	/* Autonegotiation link partner abilities */
		reg = URL_ANLP;
		break;
	case URLPHY_MSR:	/* Media Status Register */
		reg = URL_MSR;
		break;
	default:
		printf("%s: %s: bad register %04x\n",
		       sc->sc_dev.dv_xname, __func__, reg);
		val = 0;
		goto R_DONE;
		break;
	}

	if (reg == URL_MSR)
		val = url_csr_read_1(sc, reg);
	else
		val = url_csr_read_2(sc, reg);

 R_DONE:
	DPRINTFN(0xff, ("%s: %s: phy=%d reg=0x%04x => 0x%04x\n",
		 sc->sc_dev.dv_xname, __func__, phy, reg, val));

	url_unlock_mii(sc);
	return (val);
}

void
url_int_miibus_writereg(struct device *dev, int phy, int reg, int data)
{
	struct url_softc *sc;

	if (dev == NULL)
		return;

	sc = (void *)dev;

	DPRINTFN(0xff, ("%s: %s: enter, phy=%d reg=0x%04x data=0x%04x\n",
		 sc->sc_dev.dv_xname, __func__, phy, reg, data));

	if (usbd_is_dying(sc->sc_udev)) {
#ifdef DIAGNOSTIC
		printf("%s: %s: dying\n", sc->sc_dev.dv_xname,
		       __func__);
#endif
		return;
	}

	/* XXX: one PHY only for the RTL8150 internal PHY */
	if (phy != 0) {
		DPRINTFN(0xff, ("%s: %s: phy=%d is not supported\n",
			 sc->sc_dev.dv_xname, __func__, phy));
		return;
	}

	url_lock_mii(sc);

	switch (reg) {
	case MII_BMCR:		/* Control Register */
		reg = URL_BMCR;
		break;
	case MII_BMSR:		/* Status Register */
		reg = URL_BMSR;
		break;
	case MII_PHYIDR1:
	case MII_PHYIDR2:
		goto W_DONE;
		break;
	case MII_ANAR:		/* Autonegotiation advertisement */
		reg = URL_ANAR;
		break;
	case MII_ANLPAR:	/* Autonegotiation link partner abilities */
		reg = URL_ANLP;
		break;
	case URLPHY_MSR:	/* Media Status Register */
		reg = URL_MSR;
		break;
	default:
		printf("%s: %s: bad register %04x\n",
		       sc->sc_dev.dv_xname, __func__, reg);
		goto W_DONE;
		break;
	}

	if (reg == URL_MSR)
		url_csr_write_1(sc, reg, data);
	else
		url_csr_write_2(sc, reg, data);
 W_DONE:

	url_unlock_mii(sc);
	return;
}

void
url_miibus_statchg(struct device *dev)
{
#ifdef URL_DEBUG
	struct url_softc *sc;

	if (dev == NULL)
		return;

	sc = (void *)dev;
	DPRINTF(("%s: %s: enter\n", sc->sc_dev.dv_xname, __func__));
#endif
	/* Nothing to do */
}

#if 0
/*
 * external PHYs support, but not test.
 */
int
url_ext_miibus_redreg(struct device *dev, int phy, int reg)
{
	struct url_softc *sc = (void *)dev;
	u_int16_t val;

	DPRINTF(("%s: %s: enter, phy=%d reg=0x%04x\n",
		 sc->sc_dev.dv_xname, __func__, phy, reg));

	if (usbd_is_dying(sc->sc_udev)) {
#ifdef DIAGNOSTIC
		printf("%s: %s: dying\n", sc->sc_dev.dv_xname,
		       __func__);
#endif
		return (0);
	}

	url_lock_mii(sc);

	url_csr_write_1(sc, URL_PHYADD, phy & URL_PHYADD_MASK);
	/*
	 * RTL8150L will initiate a MII management data transaction
	 * if PHYCNT_OWN bit is set 1 by software. After transaction,
	 * this bit is auto cleared by TRL8150L.
	 */
	url_csr_write_1(sc, URL_PHYCNT,
			(reg | URL_PHYCNT_PHYOWN) & ~URL_PHYCNT_RWCR);
	for (i = 0; i < URL_TIMEOUT; i++) {
		if ((url_csr_read_1(sc, URL_PHYCNT) & URL_PHYCNT_PHYOWN) == 0)
			break;
	}
	if (i == URL_TIMEOUT) {
		printf("%s: MII read timed out\n", sc->sc_dev.dv_xname);
	}

	val = url_csr_read_2(sc, URL_PHYDAT);

	DPRINTF(("%s: %s: phy=%d reg=0x%04x => 0x%04x\n",
		 sc->sc_dev.dv_xname, __func__, phy, reg, val));

	url_unlock_mii(sc);
	return (val);
}

void
url_ext_miibus_writereg(struct device *dev, int phy, int reg, int data)
{
	struct url_softc *sc = (void *)dev;

	DPRINTF(("%s: %s: enter, phy=%d reg=0x%04x data=0x%04x\n",
		 sc->sc_dev.dv_xname, __func__, phy, reg, data));

	if (usbd_is_dying(sc->sc_udev)) {
#ifdef DIAGNOSTIC
		printf("%s: %s: dying\n", sc->sc_dev.dv_xname,
		       __func__);
#endif
		return;
	}

	url_lock_mii(sc);

	url_csr_write_2(sc, URL_PHYDAT, data);
	url_csr_write_1(sc, URL_PHYADD, phy);
	url_csr_write_1(sc, URL_PHYCNT, reg | URL_PHYCNT_RWCR);	/* Write */

	for (i=0; i < URL_TIMEOUT; i++) {
		if (url_csr_read_1(sc, URL_PHYCNT) & URL_PHYCNT_PHYOWN)
			break;
	}

	if (i == URL_TIMEOUT) {
		printf("%s: MII write timed out\n",
		       sc->sc_dev.dv_xname);
	}

	url_unlock_mii(sc);
	return;
}
#endif

