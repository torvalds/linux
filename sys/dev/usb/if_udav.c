/*	$OpenBSD: if_udav.c,v 1.86 2024/05/23 03:21:09 jsg Exp $ */
/*	$NetBSD: if_udav.c,v 1.3 2004/04/23 17:25:25 itojun Exp $	*/
/*	$nabe: if_udav.c,v 1.3 2003/08/21 16:57:19 nabe Exp $	*/
/*
 * Copyright (c) 2003
 *     Shingo WATANABE <nabe@nabechan.org>.  All rights reserved.
 * Copyright (c) 2014
 *     Takayoshi SASANO <uaa@uaa.org.uk> (RD9700 support)
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
 * DM9601(DAVICOM USB to Ethernet MAC Controller with Integrated 10/100 PHY)
 * The spec can be found at the following url.
 *  http://www.meworks.net/userfile/24247/DM9601-DS-P03-102908.pdf
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

#include <dev/mii/miivar.h>

#include <dev/usb/usb.h>
#include <dev/usb/usbdi.h>
#include <dev/usb/usbdi_util.h>
#include <dev/usb/usbdevs.h>

#include <dev/usb/if_udavreg.h>

int udav_match(struct device *, void *, void *);
void udav_attach(struct device *, struct device *, void *);
int udav_detach(struct device *, int);

struct cfdriver udav_cd = {
	NULL, "udav", DV_IFNET
};

const struct cfattach udav_ca = {
	sizeof(struct udav_softc), udav_match, udav_attach, udav_detach
};

int udav_openpipes(struct udav_softc *);
int udav_rx_list_init(struct udav_softc *);
int udav_tx_list_init(struct udav_softc *);
int udav_newbuf(struct udav_softc *, struct udav_chain *, struct mbuf *);
void udav_start(struct ifnet *);
int udav_send(struct udav_softc *, struct mbuf *, int);
void udav_txeof(struct usbd_xfer *, void *, usbd_status);
void udav_rxeof(struct usbd_xfer *, void *, usbd_status);
void udav_tick(void *);
void udav_tick_task(void *);
int udav_ioctl(struct ifnet *, u_long, caddr_t);
void udav_stop_task(struct udav_softc *);
void udav_stop(struct ifnet *, int);
void udav_watchdog(struct ifnet *);
int udav_ifmedia_change(struct ifnet *);
void udav_ifmedia_status(struct ifnet *, struct ifmediareq *);
void udav_lock_mii(struct udav_softc *);
void udav_unlock_mii(struct udav_softc *);
int udav_miibus_readreg(struct device *, int, int);
void udav_miibus_writereg(struct device *, int, int, int);
void udav_miibus_statchg(struct device *);
int udav_init(struct ifnet *);
void udav_iff(struct udav_softc *);
void udav_reset(struct udav_softc *);

int udav_csr_read(struct udav_softc *, int, void *, int);
int udav_csr_write(struct udav_softc *, int, void *, int);
int udav_csr_read1(struct udav_softc *, int);
int udav_csr_write1(struct udav_softc *, int, unsigned char);

#if 0
int udav_mem_read(struct udav_softc *, int, void *, int);
int udav_mem_write(struct udav_softc *, int, void *, int);
int udav_mem_write1(struct udav_softc *, int, unsigned char);
#endif

/* Macros */
#ifdef UDAV_DEBUG
#define DPRINTF(x)	do { if (udavdebug) printf x; } while(0)
#define DPRINTFN(n,x)	do { if (udavdebug >= (n)) printf x; } while(0)
int udavdebug = 0;
#else
#define DPRINTF(x)
#define DPRINTFN(n,x)
#endif

#define	UDAV_SETBIT(sc, reg, x)	\
	udav_csr_write1(sc, reg, udav_csr_read1(sc, reg) | (x))

#define	UDAV_CLRBIT(sc, reg, x)	\
	udav_csr_write1(sc, reg, udav_csr_read1(sc, reg) & ~(x))

static const struct udav_type {
	struct usb_devno udav_dev;
	u_int16_t udav_flags;
#define UDAV_EXT_PHY	0x0001
#define UDAV_RD9700	0x0002
} udav_devs [] = {
	{{ USB_VENDOR_COREGA, USB_PRODUCT_COREGA_FETHER_USB_TXC }, 0 },
	{{ USB_VENDOR_DAVICOM, USB_PRODUCT_DAVICOM_DM9601 }, 0 },
	{{ USB_VENDOR_DAVICOM, USB_PRODUCT_DAVICOM_WK668 }, 0 },
	{{ USB_VENDOR_SHANTOU, USB_PRODUCT_SHANTOU_DM9601 }, 0 },
	{{ USB_VENDOR_SHANTOU, USB_PRODUCT_SHANTOU_ST268 }, 0 },
	{{ USB_VENDOR_SHANTOU, USB_PRODUCT_SHANTOU_ZT6688 }, 0 },
	{{ USB_VENDOR_SHANTOU, USB_PRODUCT_SHANTOU_ADM8515 }, 0 },
	{{ USB_VENDOR_UNKNOWN4, USB_PRODUCT_UNKNOWN4_DM9601 }, 0 },
	{{ USB_VENDOR_UNKNOWN6, USB_PRODUCT_UNKNOWN6_DM9601 }, 0 },
	{{ USB_VENDOR_UNKNOWN4, USB_PRODUCT_UNKNOWN4_RD9700 }, UDAV_RD9700 },
};
#define udav_lookup(v, p) ((struct udav_type *)usb_lookup(udav_devs, v, p))


/* Probe */
int
udav_match(struct device *parent, void *match, void *aux)
{
	struct usb_attach_arg *uaa = aux;

	if (uaa->iface == NULL || uaa->configno != 1)
		return (UMATCH_NONE);

	return (udav_lookup(uaa->vendor, uaa->product) != NULL ?
		UMATCH_VENDOR_PRODUCT_CONF_IFACE : UMATCH_NONE);
}

/* Attach */
void
udav_attach(struct device *parent, struct device *self, void *aux)
{
	struct udav_softc *sc = (struct udav_softc *)self;
	struct usb_attach_arg *uaa = aux;
	struct usbd_device *dev = uaa->device;
	struct usbd_interface *iface = uaa->iface;
	usbd_status err;
	usb_interface_descriptor_t *id;
	usb_endpoint_descriptor_t *ed;
	char *devname = sc->sc_dev.dv_xname;
	struct ifnet *ifp;
	struct mii_data *mii;
	u_char eaddr[ETHER_ADDR_LEN];
	int i, s;

	printf("%s: ", devname);

	sc->sc_udev = dev;

	usb_init_task(&sc->sc_tick_task, udav_tick_task, sc,
	    USB_TASK_TYPE_GENERIC);
	rw_init(&sc->sc_mii_lock, "udavmii");
	usb_init_task(&sc->sc_stop_task, (void (*)(void *)) udav_stop_task, sc,
	    USB_TASK_TYPE_GENERIC);

	sc->sc_ctl_iface = iface;
	sc->sc_flags = udav_lookup(uaa->vendor, uaa->product)->udav_flags;

	/* get interface descriptor */
	id = usbd_get_interface_descriptor(sc->sc_ctl_iface);

	/* find endpoints */
	sc->sc_bulkin_no = sc->sc_bulkout_no = sc->sc_intrin_no = -1;
	for (i = 0; i < id->bNumEndpoints; i++) {
		ed = usbd_interface2endpoint_descriptor(sc->sc_ctl_iface, i);
		if (ed == NULL) {
			printf("couldn't get endpoint %d\n", i);
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
		printf("missing endpoint\n");
		goto bad;
	}

	s = splnet();

	/* reset the adapter */
	udav_reset(sc);

	/* Get Ethernet Address */
	err = udav_csr_read(sc, UDAV_PAR, (void *)eaddr, ETHER_ADDR_LEN);
	if (err) {
		printf("read MAC address failed\n");
		splx(s);
		goto bad;
	}

	/* Print Ethernet Address */
	printf("address %s\n", ether_sprintf(eaddr));

        bcopy(eaddr, (char *)&sc->sc_ac.ac_enaddr, ETHER_ADDR_LEN);

	/* initialize interface information */
	ifp = GET_IFP(sc);
	ifp->if_softc = sc;
	strlcpy(ifp->if_xname, devname, IFNAMSIZ);
	ifp->if_flags = IFF_BROADCAST | IFF_SIMPLEX | IFF_MULTICAST;
	ifp->if_start = udav_start;
	ifp->if_ioctl = udav_ioctl;
	ifp->if_watchdog = udav_watchdog;

	/*
	 * Do ifmedia setup.
	 */
	mii = &sc->sc_mii;
	mii->mii_ifp = ifp;
	mii->mii_readreg = udav_miibus_readreg;
	mii->mii_writereg = udav_miibus_writereg;
	mii->mii_statchg = udav_miibus_statchg;
	mii->mii_flags = MIIF_AUTOTSLEEP;
	ifmedia_init(&mii->mii_media, 0,
		     udav_ifmedia_change, udav_ifmedia_status);
	if (sc->sc_flags & UDAV_RD9700) {
		/* no MII-PHY */
		ifmedia_add(&mii->mii_media, IFM_ETHER | IFM_NONE, 0, NULL);
		ifmedia_set(&mii->mii_media, IFM_ETHER | IFM_NONE);
	} else {
		mii_attach(self, mii, 0xffffffff, 
			   MII_PHY_ANY, MII_OFFSET_ANY, 0);
		if (LIST_FIRST(&mii->mii_phys) == NULL) {
			ifmedia_add(&mii->mii_media, IFM_ETHER | IFM_NONE,
				    0, NULL);
			ifmedia_set(&mii->mii_media, IFM_ETHER | IFM_NONE);
		} else
			ifmedia_set(&mii->mii_media, IFM_ETHER | IFM_AUTO);
	}

	/* attach the interface */
	if_attach(ifp);
	ether_ifattach(ifp);

	timeout_set(&sc->sc_stat_ch, udav_tick, sc);

	splx(s);

	return;

 bad:
	usbd_deactivate(sc->sc_udev);
}

/* detach */
int
udav_detach(struct device *self, int flags)
{
	struct udav_softc *sc = (struct udav_softc *)self;
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
		udav_stop(GET_IFP(sc), 1);

	if (!(sc->sc_flags & UDAV_RD9700))
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

#if 0
/* read memory */
int
udav_mem_read(struct udav_softc *sc, int offset, void *buf, int len)
{
	usb_device_request_t req;
	usbd_status err;

	if (sc == NULL)
		return (0);

	DPRINTFN(0x200,
		("%s: %s: enter\n", sc->sc_dev.dv_xname, __func__));

	if (usbd_is_dying(sc->sc_udev))
		return (0);

	offset &= 0xffff;
	len &= 0xff;

	req.bmRequestType = UT_READ_VENDOR_DEVICE;
	req.bRequest = UDAV_REQ_MEM_READ;
	USETW(req.wValue, 0x0000);
	USETW(req.wIndex, offset);
	USETW(req.wLength, len);

	sc->sc_refcnt++;
	err = usbd_do_request(sc->sc_udev, &req, buf);
	if (--sc->sc_refcnt < 0)
		usb_detach_wakeup(&sc->sc_dev);
	if (err) {
		DPRINTF(("%s: %s: read failed. off=%04x, err=%d\n",
			 sc->sc_dev.dv_xname, __func__, offset, err));
	}

	return (err);
}

/* write memory */
int
udav_mem_write(struct udav_softc *sc, int offset, void *buf, int len)
{
	usb_device_request_t req;
	usbd_status err;

	if (sc == NULL)
		return (0);

	DPRINTFN(0x200,
		("%s: %s: enter\n", sc->sc_dev.dv_xname, __func__));

	if (usbd_is_dying(sc->sc_udev))
		return (0);

	offset &= 0xffff;
	len &= 0xff;

	req.bmRequestType = UT_WRITE_VENDOR_DEVICE;
	req.bRequest = UDAV_REQ_MEM_WRITE;
	USETW(req.wValue, 0x0000);
	USETW(req.wIndex, offset);
	USETW(req.wLength, len);

	sc->sc_refcnt++;
	err = usbd_do_request(sc->sc_udev, &req, buf);
	if (--sc->sc_refcnt < 0)
		usb_detach_wakeup(&sc->sc_dev);
	if (err) {
		DPRINTF(("%s: %s: write failed. off=%04x, err=%d\n",
			 sc->sc_dev.dv_xname, __func__, offset, err));
	}

	return (err);
}

/* write memory */
int
udav_mem_write1(struct udav_softc *sc, int offset, unsigned char ch)
{
	usb_device_request_t req;
	usbd_status err;

	if (sc == NULL)
		return (0);

	DPRINTFN(0x200,
		("%s: %s: enter\n", sc->sc_dev.dv_xname, __func__));

	if (usbd_is_dying(sc->sc_udev))
		return (0);

	offset &= 0xffff;

	req.bmRequestType = UT_WRITE_VENDOR_DEVICE;
	req.bRequest = UDAV_REQ_MEM_WRITE1;
	USETW(req.wValue, ch);
	USETW(req.wIndex, offset);
	USETW(req.wLength, 0x0000);

	sc->sc_refcnt++;
	err = usbd_do_request(sc->sc_udev, &req, NULL);
	if (--sc->sc_refcnt < 0)
		usb_detach_wakeup(&sc->sc_dev);
	if (err) {
		DPRINTF(("%s: %s: write failed. off=%04x, err=%d\n",
			 sc->sc_dev.dv_xname, __func__, offset, err));
	}

	return (err);
}
#endif

/* read register(s) */
int
udav_csr_read(struct udav_softc *sc, int offset, void *buf, int len)
{
	usb_device_request_t req;
	usbd_status err;

	if (sc == NULL)
		return (0);

	DPRINTFN(0x200,
		("%s: %s: enter\n", sc->sc_dev.dv_xname, __func__));

	if (usbd_is_dying(sc->sc_udev))
		return (0);

	offset &= 0xff;
	len &= 0xff;

	req.bmRequestType = UT_READ_VENDOR_DEVICE;
	req.bRequest = UDAV_REQ_REG_READ;
	USETW(req.wValue, 0x0000);
	USETW(req.wIndex, offset);
	USETW(req.wLength, len);

	sc->sc_refcnt++;
	err = usbd_do_request(sc->sc_udev, &req, buf);
	if (--sc->sc_refcnt < 0)
		usb_detach_wakeup(&sc->sc_dev);
	if (err) {
		DPRINTF(("%s: %s: read failed. off=%04x, err=%d\n",
			 sc->sc_dev.dv_xname, __func__, offset, err));
	}

	return (err);
}

/* write register(s) */
int
udav_csr_write(struct udav_softc *sc, int offset, void *buf, int len)
{
	usb_device_request_t req;
	usbd_status err;

	if (sc == NULL)
		return (0);

	DPRINTFN(0x200,
		("%s: %s: enter\n", sc->sc_dev.dv_xname, __func__));

	if (usbd_is_dying(sc->sc_udev))
		return (0);

	offset &= 0xff;
	len &= 0xff;

	req.bmRequestType = UT_WRITE_VENDOR_DEVICE;
	req.bRequest = UDAV_REQ_REG_WRITE;
	USETW(req.wValue, 0x0000);
	USETW(req.wIndex, offset);
	USETW(req.wLength, len);

	sc->sc_refcnt++;
	err = usbd_do_request(sc->sc_udev, &req, buf);
	if (--sc->sc_refcnt < 0)
		usb_detach_wakeup(&sc->sc_dev);
	if (err) {
		DPRINTF(("%s: %s: write failed. off=%04x, err=%d\n",
			 sc->sc_dev.dv_xname, __func__, offset, err));
	}

	return (err);
}

int
udav_csr_read1(struct udav_softc *sc, int offset)
{
	u_int8_t val = 0;
	
	if (sc == NULL)
		return (0);

	DPRINTFN(0x200,
		("%s: %s: enter\n", sc->sc_dev.dv_xname, __func__));

	return (udav_csr_read(sc, offset, &val, 1) ? 0 : val);
}

/* write a register */
int
udav_csr_write1(struct udav_softc *sc, int offset, unsigned char ch)
{
	usb_device_request_t req;
	usbd_status err;

	if (sc == NULL)
		return (0);

	DPRINTFN(0x200,
		("%s: %s: enter\n", sc->sc_dev.dv_xname, __func__));

	if (usbd_is_dying(sc->sc_udev))
		return (0);

	offset &= 0xff;

	req.bmRequestType = UT_WRITE_VENDOR_DEVICE;
	req.bRequest = UDAV_REQ_REG_WRITE1;
	USETW(req.wValue, ch);
	USETW(req.wIndex, offset);
	USETW(req.wLength, 0x0000);

	sc->sc_refcnt++;
	err = usbd_do_request(sc->sc_udev, &req, NULL);
	if (--sc->sc_refcnt < 0)
		usb_detach_wakeup(&sc->sc_dev);
	if (err) {
		DPRINTF(("%s: %s: write failed. off=%04x, err=%d\n",
			 sc->sc_dev.dv_xname, __func__, offset, err));
	}

	return (err);
}

int
udav_init(struct ifnet *ifp)
{
	struct udav_softc *sc = ifp->if_softc;
	struct mii_data *mii = GET_MII(sc);
	u_char *eaddr;
	int s;

	DPRINTF(("%s: %s: enter\n", sc->sc_dev.dv_xname, __func__));

	s = splnet();

	/* Cancel pending I/O and free all TX/RX buffers */
	udav_stop(ifp, 1);

        eaddr = sc->sc_ac.ac_enaddr;
	udav_csr_write(sc, UDAV_PAR, eaddr, ETHER_ADDR_LEN);

	/* Initialize network control register */
	/*  Disable loopback  */
	UDAV_CLRBIT(sc, UDAV_NCR, UDAV_NCR_LBK0 | UDAV_NCR_LBK1);

	/* Initialize RX control register */
	UDAV_SETBIT(sc, UDAV_RCR, UDAV_RCR_DIS_LONG | UDAV_RCR_DIS_CRC);

	/* Initialize transmit ring */
	if (udav_tx_list_init(sc) == ENOBUFS) {
		printf("%s: tx list init failed\n", sc->sc_dev.dv_xname);
		splx(s);
		return (EIO);
	}

	/* Initialize receive ring */
	if (udav_rx_list_init(sc) == ENOBUFS) {
		printf("%s: rx list init failed\n", sc->sc_dev.dv_xname);
		splx(s);
		return (EIO);
	}

	/* Program promiscuous mode and multicast filters */
	udav_iff(sc);

	/* Enable RX */
	UDAV_SETBIT(sc, UDAV_RCR, UDAV_RCR_RXEN);

	/* clear POWER_DOWN state of internal PHY */
	UDAV_SETBIT(sc, UDAV_GPCR, UDAV_GPCR_GEP_CNTL0);
	UDAV_CLRBIT(sc, UDAV_GPR, UDAV_GPR_GEPIO0);

	if (!(sc->sc_flags & UDAV_RD9700))
		mii_mediachg(mii);

	if (sc->sc_pipe_tx == NULL || sc->sc_pipe_rx == NULL) {
		if (udav_openpipes(sc)) {
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
udav_reset(struct udav_softc *sc)
{
	int i;

	DPRINTF(("%s: %s: enter\n", sc->sc_dev.dv_xname, __func__));

	if (usbd_is_dying(sc->sc_udev))
		return;

	/* Select PHY */
#if 1
	/*
	 * XXX: force select internal phy.
	 *	external phy routines are not tested.
	 */
	UDAV_CLRBIT(sc, UDAV_NCR, UDAV_NCR_EXT_PHY);
#else
	if (sc->sc_flags & UDAV_EXT_PHY) {
		UDAV_SETBIT(sc, UDAV_NCR, UDAV_NCR_EXT_PHY);
	} else {
		UDAV_CLRBIT(sc, UDAV_NCR, UDAV_NCR_EXT_PHY);
	}
#endif

	UDAV_SETBIT(sc, UDAV_NCR, UDAV_NCR_RST);

	for (i = 0; i < UDAV_TX_TIMEOUT; i++) {
		if (!(udav_csr_read1(sc, UDAV_NCR) & UDAV_NCR_RST))
			break;
		delay(10);	/* XXX */
	}
	delay(10000);		/* XXX */
}

#define UDAV_BITS	6

void
udav_iff(struct udav_softc *sc)
{
	struct ifnet *ifp = GET_IFP(sc);
	struct arpcom *ac = &sc->sc_ac;
	struct ether_multi *enm;
	struct ether_multistep step;
	u_int8_t hashes[8];
	int h = 0;

	DPRINTF(("%s: %s: enter\n", sc->sc_dev.dv_xname, __func__));

	if (usbd_is_dying(sc->sc_udev))
		return;

	UDAV_CLRBIT(sc, UDAV_RCR, UDAV_RCR_ALL | UDAV_RCR_PRMSC);
	memset(hashes, 0x00, sizeof(hashes));
	ifp->if_flags &= ~IFF_ALLMULTI;

	if (ifp->if_flags & IFF_PROMISC || ac->ac_multirangecnt > 0) {
		ifp->if_flags |= IFF_ALLMULTI;
		UDAV_SETBIT(sc, UDAV_RCR, UDAV_RCR_ALL);
		if (ifp->if_flags & IFF_PROMISC)
			UDAV_SETBIT(sc, UDAV_RCR, UDAV_RCR_PRMSC);
	} else {
		hashes[7] |= 0x80;	/* broadcast address */

		/* now program new ones */
		ETHER_FIRST_MULTI(step, ac, enm);
		while (enm != NULL) {
			h = ether_crc32_le(enm->enm_addrlo, ETHER_ADDR_LEN) &
			    ((1 << UDAV_BITS) - 1);

			hashes[h>>3] |= 1 << (h & 0x7);

			ETHER_NEXT_MULTI(step, enm);
		}
	}

	udav_csr_write(sc, UDAV_MAR, hashes, sizeof(hashes));
}

int
udav_openpipes(struct udav_softc *sc)
{
	struct udav_chain *c;
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
				  &sc->sc_cdata.udav_ibuf, UDAV_INTR_PKGLEN,
				  udav_intr, UDAV_INTR_INTERVAL);
	if (err) {
		printf("%s: open intr pipe failed: %s\n",
		       sc->sc_dev.dv_xname, usbd_errstr(err));
		error = EIO;
		goto done;
	}
#endif


	/* Start up the receive pipe. */
	for (i = 0; i < UDAV_RX_LIST_CNT; i++) {
		c = &sc->sc_cdata.udav_rx_chain[i];
		usbd_setup_xfer(c->udav_xfer, sc->sc_pipe_rx,
				c, c->udav_buf, UDAV_BUFSZ,
				USBD_SHORT_XFER_OK | USBD_NO_COPY,
				USBD_NO_TIMEOUT, udav_rxeof);
		(void)usbd_transfer(c->udav_xfer);
		DPRINTF(("%s: %s: start read\n", sc->sc_dev.dv_xname,
			 __func__));
	}

 done:
	if (--sc->sc_refcnt < 0)
		usb_detach_wakeup(&sc->sc_dev);

	return (error);
}

int
udav_newbuf(struct udav_softc *sc, struct udav_chain *c, struct mbuf *m)
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
	c->udav_mbuf = m_new;

	return (0);
}


int
udav_rx_list_init(struct udav_softc *sc)
{
	struct udav_cdata *cd;
	struct udav_chain *c;
	int i;

	DPRINTF(("%s: %s: enter\n", sc->sc_dev.dv_xname, __func__));

	cd = &sc->sc_cdata;
	for (i = 0; i < UDAV_RX_LIST_CNT; i++) {
		c = &cd->udav_rx_chain[i];
		c->udav_sc = sc;
		c->udav_idx = i;
		if (udav_newbuf(sc, c, NULL) == ENOBUFS)
			return (ENOBUFS);
		if (c->udav_xfer == NULL) {
			c->udav_xfer = usbd_alloc_xfer(sc->sc_udev);
			if (c->udav_xfer == NULL)
				return (ENOBUFS);
			c->udav_buf = usbd_alloc_buffer(c->udav_xfer, UDAV_BUFSZ);
			if (c->udav_buf == NULL) {
				usbd_free_xfer(c->udav_xfer);
				return (ENOBUFS);
			}
		}
	}

	return (0);
}

int
udav_tx_list_init(struct udav_softc *sc)
{
	struct udav_cdata *cd;
	struct udav_chain *c;
	int i;

	DPRINTF(("%s: %s: enter\n", sc->sc_dev.dv_xname, __func__));

	cd = &sc->sc_cdata;
	for (i = 0; i < UDAV_TX_LIST_CNT; i++) {
		c = &cd->udav_tx_chain[i];
		c->udav_sc = sc;
		c->udav_idx = i;
		c->udav_mbuf = NULL;
		if (c->udav_xfer == NULL) {
			c->udav_xfer = usbd_alloc_xfer(sc->sc_udev);
			if (c->udav_xfer == NULL)
				return (ENOBUFS);
			c->udav_buf = usbd_alloc_buffer(c->udav_xfer, UDAV_BUFSZ);
			if (c->udav_buf == NULL) {
				usbd_free_xfer(c->udav_xfer);
				return (ENOBUFS);
			}
		}
	}

	return (0);
}

void
udav_start(struct ifnet *ifp)
{
	struct udav_softc *sc = ifp->if_softc;
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

	if (udav_send(sc, m_head, 0)) {
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
udav_send(struct udav_softc *sc, struct mbuf *m, int idx)
{
	int total_len;
	struct udav_chain *c;
	usbd_status err;

	DPRINTF(("%s: %s: enter\n", sc->sc_dev.dv_xname,__func__));

	c = &sc->sc_cdata.udav_tx_chain[idx];

	/* Copy the mbuf data into a contiguous buffer */
	/*  first 2 bytes are packet length */
	m_copydata(m, 0, m->m_pkthdr.len, c->udav_buf + 2);
	c->udav_mbuf = m;
	total_len = m->m_pkthdr.len;
	if (total_len < UDAV_MIN_FRAME_LEN) {
		memset(c->udav_buf + 2 + total_len, 0,
		    UDAV_MIN_FRAME_LEN - total_len);
		total_len = UDAV_MIN_FRAME_LEN;
	}

	/* Frame length is specified in the first 2bytes of the buffer */
	c->udav_buf[0] = (u_int8_t)total_len;
	c->udav_buf[1] = (u_int8_t)(total_len >> 8);
	total_len += 2;

	usbd_setup_xfer(c->udav_xfer, sc->sc_pipe_tx, c, c->udav_buf, total_len,
			USBD_FORCE_SHORT_XFER | USBD_NO_COPY,
			UDAV_TX_TIMEOUT, udav_txeof);

	/* Transmit */
	sc->sc_refcnt++;
	err = usbd_transfer(c->udav_xfer);
	if (--sc->sc_refcnt < 0)
		usb_detach_wakeup(&sc->sc_dev);
	if (err != USBD_IN_PROGRESS) {
		printf("%s: udav_send error=%s\n", sc->sc_dev.dv_xname,
		       usbd_errstr(err));
		/* Stop the interface */
		usb_add_task(sc->sc_udev, &sc->sc_stop_task);
		return (EIO);
	}

	DPRINTF(("%s: %s: send %d bytes\n", sc->sc_dev.dv_xname,
		 __func__, total_len));

	sc->sc_cdata.udav_tx_cnt++;

	return (0);
}

void
udav_txeof(struct usbd_xfer *xfer, void *priv, usbd_status status)
{
	struct udav_chain *c = priv;
	struct udav_softc *sc = c->udav_sc;
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

	m_freem(c->udav_mbuf);
	c->udav_mbuf = NULL;

	if (ifq_empty(&ifp->if_snd) == 0)
		udav_start(ifp);

	splx(s);
}

void
udav_rxeof(struct usbd_xfer *xfer, void *priv, usbd_status status)
{
	struct udav_chain *c = priv;
	struct udav_softc *sc = c->udav_sc;
	struct ifnet *ifp = GET_IFP(sc);
	struct udav_rx_hdr *h;
	struct mbuf_list ml = MBUF_LIST_INITIALIZER();
	struct mbuf *m;
	u_int32_t total_len;
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

	if (total_len < UDAV_RX_HDRLEN) {
		ifp->if_ierrors++;
		goto done;
	}
	
	h = (struct udav_rx_hdr *)c->udav_buf;
	total_len = UGETW(h->length) - ETHER_CRC_LEN;
	
	DPRINTF(("%s: RX Status: 0x%02x\n", sc->sc_dev.dv_xname, h->pktstat));

	if (h->pktstat & UDAV_RSR_LCS) {
		ifp->if_collisions++;
		goto done;
	}

	/* RX status may still be correct but total_len is bogus */
	if (total_len < sizeof(struct ether_header) ||
	    h->pktstat & UDAV_RSR_ERR ||
	    total_len > UDAV_BUFSZ ) {
		ifp->if_ierrors++;
		goto done;
	}

	/* copy data to mbuf */
	m = c->udav_mbuf;
	memcpy(mtod(m, char *), c->udav_buf + UDAV_RX_HDRLEN, total_len);

	m->m_pkthdr.len = m->m_len = total_len;
	ml_enqueue(&ml, m);

	if (udav_newbuf(sc, c, NULL) == ENOBUFS) {
		ifp->if_ierrors++;
		goto done;
	}

	s = splnet();
	if_input(ifp, &ml);
	splx(s);

 done:
	/* Setup new transfer */
	usbd_setup_xfer(xfer, sc->sc_pipe_rx, c, c->udav_buf, UDAV_BUFSZ,
			USBD_SHORT_XFER_OK | USBD_NO_COPY,
			USBD_NO_TIMEOUT, udav_rxeof);
	sc->sc_refcnt++;
	usbd_transfer(xfer);
	if (--sc->sc_refcnt < 0)
		usb_detach_wakeup(&sc->sc_dev);

	DPRINTF(("%s: %s: start rx\n", sc->sc_dev.dv_xname, __func__));
}

int
udav_ioctl(struct ifnet *ifp, u_long cmd, caddr_t data)
{
	struct udav_softc *sc = ifp->if_softc;
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
			udav_init(ifp);
		break;

	case SIOCSIFFLAGS:
		if (ifp->if_flags & IFF_UP) {
			if (ifp->if_flags & IFF_RUNNING)
				error = ENETRESET;
			else
				udav_init(ifp);
		} else {
			if (ifp->if_flags & IFF_RUNNING)
				udav_stop(ifp, 1);
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
			udav_iff(sc);
		error = 0;
	}

	splx(s);
	return (error);
}

void
udav_watchdog(struct ifnet *ifp)
{
	struct udav_softc *sc = ifp->if_softc;
	struct udav_chain *c;
	usbd_status stat;
	int s;

	DPRINTF(("%s: %s: enter\n", sc->sc_dev.dv_xname, __func__));

	ifp->if_oerrors++;
	printf("%s: watchdog timeout\n", sc->sc_dev.dv_xname);

	s = splusb();
	c = &sc->sc_cdata.udav_tx_chain[0];
	usbd_get_xfer_status(c->udav_xfer, NULL, NULL, NULL, &stat);
	udav_txeof(c->udav_xfer, c, stat);

	if (ifq_empty(&ifp->if_snd) == 0)
		udav_start(ifp);
	splx(s);
}

void
udav_stop_task(struct udav_softc *sc)
{
	udav_stop(GET_IFP(sc), 1);
}

/* Stop the adapter and free any mbufs allocated to the RX and TX lists. */
void
udav_stop(struct ifnet *ifp, int disable)
{
	struct udav_softc *sc = ifp->if_softc;
	usbd_status err;
	int i;

	DPRINTF(("%s: %s: enter\n", sc->sc_dev.dv_xname, __func__));

	ifp->if_timer = 0;
	ifp->if_flags &= ~IFF_RUNNING;
	ifq_clr_oactive(&ifp->if_snd);

	udav_reset(sc);

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
	for (i = 0; i < UDAV_RX_LIST_CNT; i++) {
		if (sc->sc_cdata.udav_rx_chain[i].udav_mbuf != NULL) {
			m_freem(sc->sc_cdata.udav_rx_chain[i].udav_mbuf);
			sc->sc_cdata.udav_rx_chain[i].udav_mbuf = NULL;
		}
		if (sc->sc_cdata.udav_rx_chain[i].udav_xfer != NULL) {
			usbd_free_xfer(sc->sc_cdata.udav_rx_chain[i].udav_xfer);
			sc->sc_cdata.udav_rx_chain[i].udav_xfer = NULL;
		}
	}

	/* Free TX resources. */
	for (i = 0; i < UDAV_TX_LIST_CNT; i++) {
		if (sc->sc_cdata.udav_tx_chain[i].udav_mbuf != NULL) {
			m_freem(sc->sc_cdata.udav_tx_chain[i].udav_mbuf);
			sc->sc_cdata.udav_tx_chain[i].udav_mbuf = NULL;
		}
		if (sc->sc_cdata.udav_tx_chain[i].udav_xfer != NULL) {
			usbd_free_xfer(sc->sc_cdata.udav_tx_chain[i].udav_xfer);
			sc->sc_cdata.udav_tx_chain[i].udav_xfer = NULL;
		}
	}

	sc->sc_link = 0;
}

/* Set media options */
int
udav_ifmedia_change(struct ifnet *ifp)
{
	struct udav_softc *sc = ifp->if_softc;
	struct mii_data *mii = GET_MII(sc);

	DPRINTF(("%s: %s: enter\n", sc->sc_dev.dv_xname, __func__));

	if (usbd_is_dying(sc->sc_udev))
		return (0);

	sc->sc_link = 0;

	if (sc->sc_flags & UDAV_RD9700)
		return (0);

	if (mii->mii_instance) {
		struct mii_softc *miisc;
		LIST_FOREACH(miisc, &mii->mii_phys, mii_list)
			mii_phy_reset(miisc);
	}

	return (mii_mediachg(mii));
}

/* Report current media status. */
void
udav_ifmedia_status(struct ifnet *ifp, struct ifmediareq *ifmr)
{
	struct udav_softc *sc = ifp->if_softc;
	struct mii_data *mii = GET_MII(sc);

	DPRINTF(("%s: %s: enter\n", sc->sc_dev.dv_xname, __func__));

	if (usbd_is_dying(sc->sc_udev))
		return;

	if ((ifp->if_flags & IFF_RUNNING) == 0) {
		ifmr->ifm_active = IFM_ETHER | IFM_NONE;
		ifmr->ifm_status = 0;
		return;
	}

	if (sc->sc_flags & UDAV_RD9700) {
		ifmr->ifm_active = IFM_ETHER | IFM_10_T;
		ifmr->ifm_status = IFM_AVALID;
		if (sc->sc_link) ifmr->ifm_status |= IFM_ACTIVE;
		return;
	}

	mii_pollstat(mii);
	ifmr->ifm_active = mii->mii_media_active;
	ifmr->ifm_status = mii->mii_media_status;
}

void
udav_tick(void *xsc)
{
	struct udav_softc *sc = xsc;

	if (sc == NULL)
		return;

	DPRINTFN(0xff, ("%s: %s: enter\n", sc->sc_dev.dv_xname,
			__func__));

	/* Perform periodic stuff in process context */
	usb_add_task(sc->sc_udev, &sc->sc_tick_task);
}

void
udav_tick_task(void *xsc)
{
	struct udav_softc *sc = xsc;
	struct ifnet *ifp;
	struct mii_data *mii;
	int s, sts;

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

	if (sc->sc_flags & UDAV_RD9700) {
		sts = udav_csr_read1(sc, UDAV_NSR) & UDAV_NSR_LINKST;
		if (!sts)
			sc->sc_link = 0;
	} else {
		mii_tick(mii);
		sts = (mii->mii_media_status & IFM_ACTIVE &&
		       IFM_SUBTYPE(mii->mii_media_active) != IFM_NONE) ? 1 : 0;
	}

	if (!sc->sc_link && sts) {
		DPRINTF(("%s: %s: got link\n",
			 sc->sc_dev.dv_xname, __func__));
		sc->sc_link++;
		if (ifq_empty(&ifp->if_snd) == 0)
			   udav_start(ifp);
	}

	timeout_add_sec(&sc->sc_stat_ch, 1);

	splx(s);
}

/* Get exclusive access to the MII registers */
void
udav_lock_mii(struct udav_softc *sc)
{
	DPRINTFN(0xff, ("%s: %s: enter\n", sc->sc_dev.dv_xname,
			__func__));

	sc->sc_refcnt++;
	rw_enter_write(&sc->sc_mii_lock);
}

void
udav_unlock_mii(struct udav_softc *sc)
{
	DPRINTFN(0xff, ("%s: %s: enter\n", sc->sc_dev.dv_xname,
		       __func__));

	rw_exit_write(&sc->sc_mii_lock);
	if (--sc->sc_refcnt < 0)
		usb_detach_wakeup(&sc->sc_dev);
}

int
udav_miibus_readreg(struct device *dev, int phy, int reg)
{
	struct udav_softc *sc;
	u_int8_t val[2];
	u_int16_t data16;

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

	/* XXX: one PHY only for the internal PHY */
	if (phy != 0) {
		DPRINTFN(0xff, ("%s: %s: phy=%d is not supported\n",
			 sc->sc_dev.dv_xname, __func__, phy));
		return (0);
	}

	udav_lock_mii(sc);

	/* select internal PHY and set PHY register address */
	udav_csr_write1(sc, UDAV_EPAR,
			UDAV_EPAR_PHY_ADR0 | (reg & UDAV_EPAR_EROA_MASK));

	/* select PHY operation and start read command */
	udav_csr_write1(sc, UDAV_EPCR, UDAV_EPCR_EPOS | UDAV_EPCR_ERPRR);

	/* XXX: should be wait? */

	/* end read command */
	UDAV_CLRBIT(sc, UDAV_EPCR, UDAV_EPCR_ERPRR);

	/* retrieve the result from data registers */
	udav_csr_read(sc, UDAV_EPDRL, val, 2);

	udav_unlock_mii(sc);

	data16 = val[0] | (val[1] << 8);

	DPRINTFN(0xff, ("%s: %s: phy=%d reg=0x%04x => 0x%04x\n",
		 sc->sc_dev.dv_xname, __func__, phy, reg, data16));

	return (data16);
}

void
udav_miibus_writereg(struct device *dev, int phy, int reg, int data)
{
	struct udav_softc *sc;
	u_int8_t val[2];

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

	/* XXX: one PHY only for the internal PHY */
	if (phy != 0) {
		DPRINTFN(0xff, ("%s: %s: phy=%d is not supported\n",
			 sc->sc_dev.dv_xname, __func__, phy));
		return;
	}

	udav_lock_mii(sc);

	/* select internal PHY and set PHY register address */
	udav_csr_write1(sc, UDAV_EPAR,
			UDAV_EPAR_PHY_ADR0 | (reg & UDAV_EPAR_EROA_MASK));

	/* put the value to the data registers */
	val[0] = data & 0xff;
	val[1] = (data >> 8) & 0xff;
	udav_csr_write(sc, UDAV_EPDRL, val, 2);

	/* select PHY operation and start write command */
	udav_csr_write1(sc, UDAV_EPCR, UDAV_EPCR_EPOS | UDAV_EPCR_ERPRW);

	/* XXX: should be wait? */

	/* end write command */
	UDAV_CLRBIT(sc, UDAV_EPCR, UDAV_EPCR_ERPRW);

	udav_unlock_mii(sc);

	return;
}

void
udav_miibus_statchg(struct device *dev)
{
#ifdef UDAV_DEBUG
	struct udav_softc *sc;

	if (dev == NULL)
		return;

	sc = (void *)dev;
	DPRINTF(("%s: %s: enter\n", sc->sc_dev.dv_xname, __func__));
#endif
	/* Nothing to do */
}
