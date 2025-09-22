/*	$OpenBSD: if_uaq.c,v 1.6 2024/05/23 03:21:08 jsg Exp $	*/
/*-
 * Copyright (c) 2021 Jonathan Matthew <jonathan@d14n.org>
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
 */

#include "bpfilter.h"
#include "vlan.h"

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/sockio.h>
#include <sys/mbuf.h>
#include <sys/device.h>

#include <machine/bus.h>

#include <net/if.h>
#include <net/if_media.h>

#if NBPFILTER > 0
#include <net/bpf.h>
#endif

#include <netinet/in.h>
#include <netinet/if_ether.h>

#include <dev/usb/usb.h>
#include <dev/usb/usbdi.h>
#include <dev/usb/usbdi_util.h>
#include <dev/usb/usbdivar.h>
#include <dev/usb/usbdevs.h>

#ifdef UAQ_DEBUG
#define DPRINTF(x)	do { if (uaqdebug) printf x; } while (0)
#define DPRINTFN(n,x)	do { if (uaqdebug >= (n)) printf x; } while (0)
int	uaqdebug = 0;
#else
#define DPRINTF(x)
#define DPRINTFN(n,x)
#endif

#define UAQ_ENDPT_RX		0
#define UAQ_ENDPT_TX		1
#define UAQ_ENDPT_INTR		2
#define UAQ_ENDPT_MAX		3

#define UAQ_TX_LIST_CNT		1
#define UAQ_RX_LIST_CNT		1
#define UAQ_TX_BUF_ALIGN	8
#define UAQ_RX_BUF_ALIGN	8

#define UAQ_TX_BUFSZ		16384
#define UAQ_RX_BUFSZ		(62 * 1024)

#define UAQ_CTL_READ		1
#define UAQ_CTL_WRITE		2

#define UAQ_MCAST_FILTER_SIZE	8

/* control commands */
#define UAQ_CMD_ACCESS_MAC	0x01
#define UAQ_CMD_FLASH_PARAM	0x20
#define UAQ_CMD_PHY_POWER	0x31
#define UAQ_CMD_WOL_CFG		0x60
#define UAQ_CMD_PHY_OPS		0x61

/* SFR registers */
#define UAQ_SFR_GENERAL_STATUS	0x03
#define UAQ_SFR_CHIP_STATUS	0x05
#define UAQ_SFR_RX_CTL		0x0B
#define  UAQ_SFR_RX_CTL_STOP	0x0000
#define  UAQ_SFR_RX_CTL_PRO	0x0001
#define  UAQ_SFR_RX_CTL_AMALL	0x0002
#define  UAQ_SFR_RX_CTL_AB	0x0008
#define  UAQ_SFR_RX_CTL_AM	0x0010
#define  UAQ_SFR_RX_CTL_START	0x0080
#define  UAQ_SFR_RX_CTL_IPE	0x0200
#define UAQ_SFR_IPG_0		0x0D
#define UAQ_SFR_NODE_ID		0x10
#define UAQ_SFR_MCAST_FILTER	0x16
#define UAQ_SFR_MEDIUM_STATUS_MODE 0x22
#define  UAQ_SFR_MEDIUM_XGMIIMODE	0x0001
#define  UAQ_SFR_MEDIUM_FULL_DUPLEX	0x0002
#define  UAQ_SFR_MEDIUM_RXFLOW_CTRLEN	0x0010
#define  UAQ_SFR_MEDIUM_TXFLOW_CTRLEN	0x0020
#define  UAQ_SFR_MEDIUM_JUMBO_EN	0x0040
#define  UAQ_SFR_MEDIUM_RECEIVE_EN	0x0100
#define UAQ_SFR_MONITOR_MODE	0x24
#define  UAQ_SFR_MONITOR_MODE_EPHYRW	0x01
#define  UAQ_SFR_MONITOR_MODE_RWLC	0x02
#define  UAQ_SFR_MONITOR_MODE_RWMP	0x04
#define  UAQ_SFR_MONITOR_MODE_RWWF	0x08
#define  UAQ_SFR_MONITOR_MODE_RW_FLAG	0x10
#define  UAQ_SFR_MONITOR_MODE_PMEPOL	0x20
#define  UAQ_SFR_MONITOR_MODE_PMETYPE	0x40
#define UAQ_SFR_RX_BULKIN_QCTRL 0x2E
#define UAQ_SFR_RXCOE_CTL	0x34
#define  UAQ_SFR_RXCOE_IP		0x01
#define  UAQ_SFR_RXCOE_TCP		0x02
#define  UAQ_SFR_RXCOE_UDP		0x04
#define  UAQ_SFR_RXCOE_ICMP		0x08
#define  UAQ_SFR_RXCOE_IGMP		0x10
#define  UAQ_SFR_RXCOE_TCPV6		0x20
#define  UAQ_SFR_RXCOE_UDPV6		0x40
#define  UAQ_SFR_RXCOE_ICMV6		0x80
#define UAQ_SFR_TXCOE_CTL	0x35
#define  UAQ_SFR_TXCOE_IP		0x01
#define  UAQ_SFR_TXCOE_TCP		0x02
#define  UAQ_SFR_TXCOE_UDP		0x04
#define  UAQ_SFR_TXCOE_ICMP		0x08
#define  UAQ_SFR_TXCOE_IGMP		0x10
#define  UAQ_SFR_TXCOE_TCPV6		0x20
#define  UAQ_SFR_TXCOE_UDPV6		0x40
#define  UAQ_SFR_TXCOE_ICMV6		0x80
#define UAQ_SFR_BM_INT_MASK	0x41
#define UAQ_SFR_BMRX_DMA_CTRL	0x43
#define  UAQ_SFR_BMRX_DMA_EN	0x80
#define UAQ_SFR_BMTX_DMA_CTRL	0x46
#define UAQ_SFR_PAUSE_WATERLVL_LOW 0x54
#define UAQ_SFR_ARC_CTRL	0x9E
#define UAQ_SFR_SWP_CTRL	0xB1
#define UAQ_SFR_TX_PAUSE_RESEND_T 0xB2
#define UAQ_SFR_ETH_MAC_PATH	0xB7
#define  UAQ_SFR_RX_PATH_READY	0x01
#define UAQ_SFR_BULK_OUT_CTRL	0xB9
#define  UAQ_SFR_BULK_OUT_FLUSH_EN	0x01
#define  UAQ_SFR_BULK_OUT_EFF_EN	0x02

#define UAQ_FW_VER_MAJOR	0xDA
#define UAQ_FW_VER_MINOR	0xDB
#define UAQ_FW_VER_REV		0xDC

/* phy ops */
#define UAQ_PHY_ADV_100M	(1 << 0)
#define UAQ_PHY_ADV_1G		(1 << 1)
#define UAQ_PHY_ADV_2_5G	(1 << 2)
#define UAQ_PHY_ADV_5G		(1 << 3)
#define UAQ_PHY_ADV_MASK	0x0F

#define UAQ_PHY_PAUSE		(1 << 16)
#define UAQ_PHY_ASYM_PAUSE	(1 << 17)
#define UAQ_PHY_LOW_POWER	(1 << 18)
#define UAQ_PHY_POWER_EN	(1 << 19)
#define UAQ_PHY_WOL		(1 << 20)
#define UAQ_PHY_DOWNSHIFT	(1 << 21)

#define UAQ_PHY_DSH_RETRY_SHIFT	0x18
#define UAQ_PHY_DSH_RETRY_MASK	0xF000000

/* status */
#define UAQ_STATUS_LINK		0x8000
#define UAQ_STATUS_SPEED_MASK	0x7F00
#define UAQ_STATUS_SPEED_SHIFT	8
#define UAQ_STATUS_SPEED_5G	0x000F
#define UAQ_STATUS_SPEED_2_5G	0x0010
#define UAQ_STATUS_SPEED_1G	0x0011
#define UAQ_STATUS_SPEED_100M	0x0013

/* rx descriptor */
#define UAQ_RX_HDR_COUNT_MASK	0x1FFF
#define UAQ_RX_HDR_OFFSET_MASK	0xFFFFE000
#define UAQ_RX_HDR_OFFSET_SHIFT	13

/* rx packet descriptor */
#define UAQ_RX_PKT_L4_ERR	0x01
#define UAQ_RX_PKT_L3_ERR	0x02
#define UAQ_RX_PKT_L4_MASK	0x1C
#define UAQ_RX_PKT_L4_UDP	0x04
#define UAQ_RX_PKT_L4_TCP	0x10
#define UAQ_RX_PKT_L3_MASK	0x60
#define UAQ_RX_PKT_L3_IP	0x20
#define UAQ_RX_PKT_L3_IP6	0x40
#define UAQ_RX_PKT_VLAN		0x400
#define UAQ_RX_PKT_RX_OK	0x800
#define UAQ_RX_PKT_DROP		0x80000000
#define UAQ_RX_PKT_LEN_MASK	0x7FFF0000
#define UAQ_RX_PKT_LEN_SHIFT	16
#define UAQ_RX_PKT_VLAN_SHIFT	32

/* tx packet descriptor */
#define UAQ_TX_PKT_LEN_MASK	0x1FFFFF
#define UAQ_TX_PKT_DROP_PADD	(1 << 28)
#define UAQ_TX_PKT_VLAN		(1 << 29)
#define UAQ_TX_PKT_VLAN_MASK	0xFFFF
#define UAQ_TX_PKT_VLAN_SHIFT	0x30


struct uaq_chain {
	struct uaq_softc	*uc_sc;
	struct usbd_xfer	*uc_xfer;
	char			*uc_buf;
	uint32_t		 uc_cnt;
	uint32_t		 uc_buflen;
	uint32_t		 uc_bufmax;
	SLIST_ENTRY(uaq_chain)	 uc_list;
	uint8_t			 uc_idx;
};

struct uaq_cdata {
	struct uaq_chain	 uaq_rx_chain[UAQ_RX_LIST_CNT];
	struct uaq_chain	 uaq_tx_chain[UAQ_TX_LIST_CNT];
	SLIST_HEAD(uaq_list_head, uaq_chain) uaq_tx_free;
};

struct uaq_softc {
	struct device		 sc_dev;
	struct usbd_device	*sc_udev;

	struct usbd_interface	*sc_iface;
	struct usb_task		 sc_link_task;
	struct timeval		 sc_rx_notice;
	int			 sc_ed[UAQ_ENDPT_MAX];
	struct usbd_pipe	*sc_ep[UAQ_ENDPT_MAX];
	int			 sc_out_frame_size;

	struct arpcom		 sc_ac;
	struct ifmedia		 sc_ifmedia;

	struct uaq_cdata	 sc_cdata;
	uint64_t		 sc_link_status;
	int			 sc_link_speed;

	uint32_t		 sc_phy_cfg;
	uint16_t		 sc_rxctl;
};

const struct usb_devno uaq_devs[] = {
	{ USB_VENDOR_AQUANTIA, USB_PRODUCT_AQUANTIA_AQC111 },
	{ USB_VENDOR_ASIX, USB_PRODUCT_ASIX_ASIX111 },
	{ USB_VENDOR_ASIX, USB_PRODUCT_ASIX_ASIX112 },
	{ USB_VENDOR_TRENDNET, USB_PRODUCT_TRENDNET_TUCET5G },
	{ USB_VENDOR_QNAP, USB_PRODUCT_QNAP_UC5G1T },
};

int		uaq_match(struct device *, void *, void *);
void		uaq_attach(struct device *, struct device *, void *);
int		uaq_detach(struct device *, int);

int		uaq_ctl(struct uaq_softc *, uint8_t, uint8_t, uint16_t,
		    uint16_t, void *, int);
int		uaq_read_mem(struct uaq_softc *, uint8_t, uint16_t, uint16_t,
		    void *, int);
int		uaq_write_mem(struct uaq_softc *, uint8_t, uint16_t, uint16_t,
		    void *, int);
uint8_t		uaq_read_1(struct uaq_softc *, uint8_t, uint16_t, uint16_t);
uint16_t	uaq_read_2(struct uaq_softc *, uint8_t, uint16_t, uint16_t);
uint32_t	uaq_read_4(struct uaq_softc *, uint8_t, uint16_t, uint16_t);
int		uaq_write_1(struct uaq_softc *, uint8_t, uint16_t, uint16_t,
		    uint32_t);
int		uaq_write_2(struct uaq_softc *, uint8_t, uint16_t, uint16_t,
		    uint32_t);
int		uaq_write_4(struct uaq_softc *, uint8_t, uint16_t, uint16_t,
		    uint32_t);

int		uaq_ifmedia_upd(struct ifnet *);
void		uaq_ifmedia_sts(struct ifnet *, struct ifmediareq *);
void		uaq_add_media_types(struct uaq_softc *);
void		uaq_iff(struct uaq_softc *);

void		uaq_init(void *);
int		uaq_ioctl(struct ifnet *, u_long, caddr_t);
int		uaq_xfer_list_init(struct uaq_softc *, struct uaq_chain *,
		    uint32_t, int);
void		uaq_xfer_list_free(struct uaq_softc *, struct uaq_chain *, int);

void		uaq_stop(struct uaq_softc *);
void		uaq_link(struct uaq_softc *);
void		uaq_intr(struct usbd_xfer *, void *, usbd_status);
void		uaq_start(struct ifnet *);
void		uaq_rxeof(struct usbd_xfer *, void *, usbd_status);
void		uaq_txeof(struct usbd_xfer *, void *, usbd_status);
void		uaq_watchdog(struct ifnet *);
void		uaq_reset(struct uaq_softc *);

int		uaq_encap_txpkt(struct uaq_softc *, struct mbuf *, char *,
		    uint32_t);
int		uaq_encap_xfer(struct uaq_softc *, struct uaq_chain *);

struct cfdriver uaq_cd = {
	NULL, "uaq", DV_IFNET
};

const struct cfattach uaq_ca = {
	sizeof(struct uaq_softc), uaq_match, uaq_attach, uaq_detach
};

int
uaq_ctl(struct uaq_softc *sc, uint8_t rw, uint8_t cmd, uint16_t val,
    uint16_t index, void *buf, int len)
{
	usb_device_request_t	req;
	usbd_status		err;

	if (usbd_is_dying(sc->sc_udev))
		return 0;

	if (rw == UAQ_CTL_WRITE)
		req.bmRequestType = UT_WRITE_VENDOR_DEVICE;
	else
		req.bmRequestType = UT_READ_VENDOR_DEVICE;
	req.bRequest = cmd;
	USETW(req.wValue, val);
	USETW(req.wIndex, index);
	USETW(req.wLength, len);

	DPRINTFN(5, ("uaq_ctl: rw %d, val 0x%04hx, index 0x%04hx, len %d\n",
	    rw, val, index, len));
	err = usbd_do_request(sc->sc_udev, &req, buf);
	if (err) {
		DPRINTF(("uaq_ctl: error %d\n", err));
		return -1;
	}

	return 0;
}

int
uaq_read_mem(struct uaq_softc *sc, uint8_t cmd, uint16_t addr, uint16_t index,
    void *buf, int len)
{
	return (uaq_ctl(sc, UAQ_CTL_READ, cmd, addr, index, buf, len));
}

int
uaq_write_mem(struct uaq_softc *sc, uint8_t cmd, uint16_t addr, uint16_t index,
    void *buf, int len)
{
	return (uaq_ctl(sc, UAQ_CTL_WRITE, cmd, addr, index, buf, len));
}

uint8_t
uaq_read_1(struct uaq_softc *sc, uint8_t cmd, uint16_t reg, uint16_t index)
{
	uint8_t		val;

	uaq_read_mem(sc, cmd, reg, index, &val, 1);
	DPRINTFN(4, ("uaq_read_1: cmd %x reg %x index %x = %x\n", cmd, reg,
	    index, val));
	return (val);
}

uint16_t
uaq_read_2(struct uaq_softc *sc, uint8_t cmd, uint16_t reg, uint16_t index)
{
	uint16_t	val;

	uaq_read_mem(sc, cmd, reg, index, &val, 2);
	DPRINTFN(4, ("uaq_read_2: cmd %x reg %x index %x = %x\n", cmd, reg,
	    index, UGETW(&val)));

	return (UGETW(&val));
}

uint32_t
uaq_read_4(struct uaq_softc *sc, uint8_t cmd, uint16_t reg, uint16_t index)
{
	uint32_t	val;

	uaq_read_mem(sc, cmd, reg, index, &val, 4);
	DPRINTFN(4, ("uaq_read_4: cmd %x reg %x index %x = %x\n", cmd, reg,
	    index, UGETDW(&val)));
	return (UGETDW(&val));
}

int
uaq_write_1(struct uaq_softc *sc, uint8_t cmd, uint16_t reg, uint16_t index,
    uint32_t val)
{
	uint8_t		temp;

	DPRINTFN(4, ("uaq_write_1: cmd %x reg %x index %x: %x\n", cmd, reg,
	    index, val));
	temp = val & 0xff;
	return (uaq_write_mem(sc, cmd, reg, index, &temp, 1));
}

int
uaq_write_2(struct uaq_softc *sc, uint8_t cmd, uint16_t reg, uint16_t index,
    uint32_t val)
{
	uint16_t	temp;

	DPRINTFN(4, ("uaq_write_2: cmd %x reg %x index %x: %x\n", cmd, reg,
	    index, val));
	USETW(&temp, val & 0xffff);
	return (uaq_write_mem(sc, cmd, reg, index, &temp, 2));
}

int
uaq_write_4(struct uaq_softc *sc, uint8_t cmd, uint16_t reg, uint16_t index,
    uint32_t val)
{
	uint8_t	temp[4];

	DPRINTFN(4, ("uaq_write_4: cmd %x reg %x index %x: %x\n", cmd, reg,
	    index, val));
	USETDW(temp, val);
	return (uaq_write_mem(sc, cmd, reg, index, &temp, 4));
}

int
uaq_match(struct device *parent, void *match, void *aux)
{
	struct usb_attach_arg	*uaa = aux;

	if (uaa->iface == NULL || uaa->configno != 1)
		return (UMATCH_NONE);

	return (usb_lookup(uaq_devs, uaa->vendor, uaa->product) != NULL ?
	    UMATCH_VENDOR_PRODUCT_CONF_IFACE : UMATCH_NONE);
}

void
uaq_attach(struct device *parent, struct device *self, void *aux)
{
	struct uaq_softc		*sc = (struct uaq_softc *)self;
	struct usb_attach_arg		*uaa = aux;
	usb_interface_descriptor_t	*id;
	usb_endpoint_descriptor_t	*ed;
	struct ifnet			*ifp;
	int				i, s;

	sc->sc_udev = uaa->device;
	sc->sc_iface = uaa->iface;

	usb_init_task(&sc->sc_link_task, (void (*)(void *))uaq_link, sc,
	    USB_TASK_TYPE_GENERIC);

	id = usbd_get_interface_descriptor(sc->sc_iface);

	for (i = 0; i < id->bNumEndpoints; i++) {
		ed = usbd_interface2endpoint_descriptor(sc->sc_iface, i);
		if (!ed) {
			printf("%s: couldn't get ep %d\n",
			    sc->sc_dev.dv_xname, i);
			return;
		}
		if (UE_GET_DIR(ed->bEndpointAddress) == UE_DIR_IN &&
		    UE_GET_XFERTYPE(ed->bmAttributes) == UE_BULK) {
			sc->sc_ed[UAQ_ENDPT_RX] = ed->bEndpointAddress;
		} else if (UE_GET_DIR(ed->bEndpointAddress) == UE_DIR_OUT &&
		    UE_GET_XFERTYPE(ed->bmAttributes) == UE_BULK) {
			sc->sc_ed[UAQ_ENDPT_TX] = ed->bEndpointAddress;
			sc->sc_out_frame_size = UGETW(ed->wMaxPacketSize);
		} else if (UE_GET_DIR(ed->bEndpointAddress) == UE_DIR_IN &&
		    UE_GET_XFERTYPE(ed->bmAttributes) == UE_INTERRUPT) {
			sc->sc_ed[UAQ_ENDPT_INTR] = ed->bEndpointAddress;
		}
	}

	if ((sc->sc_ed[UAQ_ENDPT_RX] == 0) ||
	    (sc->sc_ed[UAQ_ENDPT_TX] == 0) ||
	    (sc->sc_ed[UAQ_ENDPT_INTR] == 0)) {
		printf("%s: missing one or more endpoints (%d, %d, %d)\n",
		    sc->sc_dev.dv_xname, sc->sc_ed[UAQ_ENDPT_RX],
		    sc->sc_ed[UAQ_ENDPT_TX], sc->sc_ed[UAQ_ENDPT_INTR]);
		return;
	}

	s = splnet();

	printf("%s: ver %u.%u.%u", sc->sc_dev.dv_xname,
	    uaq_read_1(sc, UAQ_CMD_ACCESS_MAC, UAQ_FW_VER_MAJOR, 1) & 0x7f,
	    uaq_read_1(sc, UAQ_CMD_ACCESS_MAC, UAQ_FW_VER_MINOR, 1),
	    uaq_read_1(sc, UAQ_CMD_ACCESS_MAC, UAQ_FW_VER_REV, 1));

	uaq_read_mem(sc, UAQ_CMD_FLASH_PARAM, 0, 0, &sc->sc_ac.ac_enaddr,
	    ETHER_ADDR_LEN);
	printf(", address %s\n", ether_sprintf(sc->sc_ac.ac_enaddr));

	ifp = &sc->sc_ac.ac_if;
	ifp->if_softc = sc;
	strlcpy(ifp->if_xname, sc->sc_dev.dv_xname, IFNAMSIZ);
	ifp->if_flags = IFF_BROADCAST | IFF_SIMPLEX | IFF_MULTICAST;
	ifp->if_ioctl = uaq_ioctl;
	ifp->if_start = uaq_start;
	ifp->if_watchdog = uaq_watchdog;

	ifp->if_capabilities = IFCAP_VLAN_MTU | IFCAP_CSUM_IPv4 |
	    IFCAP_CSUM_TCPv4 | IFCAP_CSUM_UDPv4;

#if NVLAN > 0
	ifp->if_capabilities |= IFCAP_VLAN_HWTAGGING;
#endif

	ifmedia_init(&sc->sc_ifmedia, IFM_IMASK, uaq_ifmedia_upd,
	    uaq_ifmedia_sts);
	uaq_add_media_types(sc);
	ifmedia_add(&sc->sc_ifmedia, IFM_ETHER | IFM_AUTO, 0, NULL);
	ifmedia_set(&sc->sc_ifmedia, IFM_ETHER | IFM_AUTO);
	sc->sc_ifmedia.ifm_media = sc->sc_ifmedia.ifm_cur->ifm_media;

	if_attach(ifp);
	ether_ifattach(ifp);

	splx(s);
}

int
uaq_detach(struct device *self, int flags)
{
	struct uaq_softc	*sc = (struct uaq_softc *)self;
	struct ifnet		*ifp = &sc->sc_ac.ac_if;
	int			s;

	if (sc->sc_ep[UAQ_ENDPT_TX] != NULL)
		usbd_abort_pipe(sc->sc_ep[UAQ_ENDPT_TX]);
	if (sc->sc_ep[UAQ_ENDPT_RX] != NULL)
		usbd_abort_pipe(sc->sc_ep[UAQ_ENDPT_RX]);
	if (sc->sc_ep[UAQ_ENDPT_INTR] != NULL)
		usbd_abort_pipe(sc->sc_ep[UAQ_ENDPT_INTR]);

	s = splusb();

	usb_rem_task(sc->sc_udev, &sc->sc_link_task);

	usb_detach_wait(&sc->sc_dev);

	if (ifp->if_flags & IFF_RUNNING)
		uaq_stop(sc);

	if (ifp->if_softc != NULL) {
		ether_ifdetach(ifp);
		if_detach(ifp);
	}

	splx(s);

	return 0;
}

int
uaq_ifmedia_upd(struct ifnet *ifp)
{
	struct uaq_softc	*sc = ifp->if_softc;
	struct ifmedia		*ifm = &sc->sc_ifmedia;
	int			 auto_adv;

	if (IFM_TYPE(ifm->ifm_media) != IFM_ETHER)
		return (EINVAL);

	auto_adv = UAQ_PHY_ADV_100M | UAQ_PHY_ADV_1G;
	if (sc->sc_udev->speed == USB_SPEED_SUPER)
		auto_adv |= UAQ_PHY_ADV_2_5G | UAQ_PHY_ADV_5G;

	sc->sc_phy_cfg &= ~(UAQ_PHY_ADV_MASK);
	sc->sc_phy_cfg |= UAQ_PHY_PAUSE | UAQ_PHY_ASYM_PAUSE |
	    UAQ_PHY_DOWNSHIFT | (3 << UAQ_PHY_DSH_RETRY_SHIFT);

	switch (IFM_SUBTYPE(ifm->ifm_media)) {
	case IFM_AUTO:
		sc->sc_phy_cfg |= auto_adv;
		break;
	case IFM_5000_T:
		sc->sc_phy_cfg |= UAQ_PHY_ADV_5G;
		break;
	case IFM_2500_T:
		sc->sc_phy_cfg |= UAQ_PHY_ADV_2_5G;
		break;
	case IFM_1000_T:
		sc->sc_phy_cfg |= UAQ_PHY_ADV_1G;
		break;
	case IFM_100_TX:
		sc->sc_phy_cfg |= UAQ_PHY_ADV_100M;
		break;
	default:
		printf("%s: unsupported media type\n", sc->sc_dev.dv_xname);
		return (EINVAL);
	}

	DPRINTFN(1, ("%s: phy cfg %x\n", sc->sc_dev.dv_xname, sc->sc_phy_cfg));
	uaq_write_4(sc, UAQ_CMD_PHY_OPS, 0, 0, sc->sc_phy_cfg);
	return (0);
}

void
uaq_ifmedia_sts(struct ifnet *ifp, struct ifmediareq *ifmr)
{
	struct uaq_softc	*sc = ifp->if_softc;

	ifmr->ifm_status = IFM_AVALID;
	if (sc->sc_link_speed > 0) {
		ifmr->ifm_status |= IFM_ACTIVE;
		ifmr->ifm_active = IFM_ETHER | IFM_FDX;
		switch (sc->sc_link_speed) {
		case UAQ_STATUS_SPEED_5G:
			ifmr->ifm_active |= IFM_5000_T;
			break;
		case UAQ_STATUS_SPEED_2_5G:
			ifmr->ifm_active |= IFM_2500_T;
			break;
		case UAQ_STATUS_SPEED_1G:
			ifmr->ifm_active |= IFM_1000_T;
			break;
		case UAQ_STATUS_SPEED_100M:
			ifmr->ifm_active |= IFM_100_TX;
			break;
		default:
			break;
		}
	}
}

void
uaq_add_media_types(struct uaq_softc *sc)
{
	ifmedia_add(&sc->sc_ifmedia, IFM_ETHER | IFM_100_TX, 0, NULL);
	ifmedia_add(&sc->sc_ifmedia, IFM_ETHER | IFM_100_TX | IFM_FDX, 0,
	    NULL);
	ifmedia_add(&sc->sc_ifmedia, IFM_ETHER | IFM_1000_T, 0, NULL);
	ifmedia_add(&sc->sc_ifmedia, IFM_ETHER | IFM_1000_T | IFM_FDX, 0,
	    NULL);
	/* only add 2.5G and 5G if at super speed */
	ifmedia_add(&sc->sc_ifmedia, IFM_ETHER | IFM_2500_T, 0, NULL);
	ifmedia_add(&sc->sc_ifmedia, IFM_ETHER | IFM_2500_T | IFM_FDX, 0,
	    NULL);
	ifmedia_add(&sc->sc_ifmedia, IFM_ETHER | IFM_5000_T, 0, NULL);
	ifmedia_add(&sc->sc_ifmedia, IFM_ETHER | IFM_5000_T | IFM_FDX, 0,
	    NULL);
}

void
uaq_iff(struct uaq_softc *sc)
{
	struct ifnet		*ifp = &sc->sc_ac.ac_if;
	struct ether_multi	*enm;
	struct ether_multistep	step;
	uint8_t			filter[UAQ_MCAST_FILTER_SIZE];
	uint32_t		hash;

	if (usbd_is_dying(sc->sc_udev))
		return;

	sc->sc_rxctl &= ~(UAQ_SFR_RX_CTL_PRO | UAQ_SFR_RX_CTL_AMALL |
	    UAQ_SFR_RX_CTL_AM);
	ifp->if_flags &= ~IFF_ALLMULTI;

	if (ifp->if_flags & IFF_PROMISC) {
		ifp->if_flags |= IFF_ALLMULTI;
		sc->sc_rxctl |= UAQ_SFR_RX_CTL_PRO;
	} else if (sc->sc_ac.ac_multirangecnt > 0) {
		ifp->if_flags |= IFF_ALLMULTI;
		sc->sc_rxctl |= UAQ_SFR_RX_CTL_AMALL;
	} else {
		sc->sc_rxctl |= UAQ_SFR_RX_CTL_AM;

		bzero(filter, sizeof(filter));
		ETHER_FIRST_MULTI(step, &sc->sc_ac, enm);
		while (enm != NULL) {
			hash = ether_crc32_be(enm->enm_addrlo, ETHER_ADDR_LEN)
			    >> 26;
			filter[hash >> 3] |= (1 << (hash & 7));
			ETHER_NEXT_MULTI(step, enm);
		}

		uaq_write_mem(sc, UAQ_CMD_ACCESS_MAC, UAQ_SFR_MCAST_FILTER,
		    UAQ_MCAST_FILTER_SIZE, filter, UAQ_MCAST_FILTER_SIZE);
	}

	DPRINTFN(1, ("%s: rxctl = %x\n", sc->sc_dev.dv_xname, sc->sc_rxctl));
	uaq_write_2(sc, UAQ_CMD_ACCESS_MAC, UAQ_SFR_RX_CTL, 2, sc->sc_rxctl);
}

void
uaq_reset(struct uaq_softc *sc)
{
	uint8_t mode;

	sc->sc_phy_cfg = UAQ_PHY_POWER_EN;
	uaq_write_4(sc, UAQ_CMD_PHY_OPS, 0, 0, sc->sc_phy_cfg);

	uaq_write_mem(sc, UAQ_CMD_ACCESS_MAC, UAQ_SFR_NODE_ID, 0,
	    sc->sc_ac.ac_enaddr, ETHER_ADDR_LEN);
	uaq_write_mem(sc, UAQ_CMD_ACCESS_MAC, UAQ_SFR_NODE_ID, ETHER_ADDR_LEN,
	    sc->sc_ac.ac_enaddr, ETHER_ADDR_LEN);

	uaq_write_1(sc, UAQ_CMD_ACCESS_MAC, UAQ_SFR_BM_INT_MASK, 0, 0xff);
	uaq_write_1(sc, UAQ_CMD_ACCESS_MAC, UAQ_SFR_SWP_CTRL, 0, 0);

	mode = uaq_read_1(sc, UAQ_CMD_ACCESS_MAC, UAQ_SFR_MONITOR_MODE, 1);
	mode &= ~(UAQ_SFR_MONITOR_MODE_EPHYRW | UAQ_SFR_MONITOR_MODE_RWLC |
	    UAQ_SFR_MONITOR_MODE_RWMP | UAQ_SFR_MONITOR_MODE_RWWF |
	    UAQ_SFR_MONITOR_MODE_RW_FLAG);
	uaq_write_1(sc, UAQ_CMD_ACCESS_MAC, UAQ_SFR_MONITOR_MODE, 1, mode);

	sc->sc_link_status = 0;
	sc->sc_link_speed = 0;
}

void
uaq_init(void *xsc)
{
	struct uaq_softc	*sc = xsc;
	struct uaq_chain	*c;
	struct ifnet		*ifp = &sc->sc_ac.ac_if;
	usbd_status		err;
	int			s, i;

	s = splnet();

	uaq_stop(sc);

	uaq_reset(sc);

	if (uaq_xfer_list_init(sc, sc->sc_cdata.uaq_rx_chain,
		UAQ_RX_BUFSZ, UAQ_RX_LIST_CNT) == ENOBUFS) {
		printf("%s: rx list init failed\n", sc->sc_dev.dv_xname);
		splx(s);
		return;
	}

	if (uaq_xfer_list_init(sc, sc->sc_cdata.uaq_tx_chain,
		UAQ_TX_BUFSZ, UAQ_TX_LIST_CNT) == ENOBUFS) {
		printf("%s: tx list init failed\n", sc->sc_dev.dv_xname);
		splx(s);
		return;
	}

	SLIST_INIT(&sc->sc_cdata.uaq_tx_free);
	for (i = 0; i < UAQ_TX_LIST_CNT; i++)
		SLIST_INSERT_HEAD(&sc->sc_cdata.uaq_tx_free,
		    &sc->sc_cdata.uaq_tx_chain[i], uc_list);

	err = usbd_open_pipe(sc->sc_iface, sc->sc_ed[UAQ_ENDPT_RX],
	    USBD_EXCLUSIVE_USE, &sc->sc_ep[UAQ_ENDPT_RX]);
	if (err) {
		printf("%s: open rx pipe failed: %s\n",
		    sc->sc_dev.dv_xname, usbd_errstr(err));
		splx(s);
		return;
	}

	err = usbd_open_pipe(sc->sc_iface, sc->sc_ed[UAQ_ENDPT_TX],
	    USBD_EXCLUSIVE_USE, &sc->sc_ep[UAQ_ENDPT_TX]);
	if (err) {
		printf("%s: open tx pipe failed: %s\n",
		    sc->sc_dev.dv_xname, usbd_errstr(err));
		splx(s);
		return;
	}

	for (i = 0; i < UAQ_RX_LIST_CNT; i++) {
		c = &sc->sc_cdata.uaq_rx_chain[i];
		usbd_setup_xfer(c->uc_xfer, sc->sc_ep[UAQ_ENDPT_RX],
		    c, c->uc_buf, c->uc_bufmax,
		    USBD_SHORT_XFER_OK | USBD_NO_COPY,
		    USBD_NO_TIMEOUT, uaq_rxeof);
		usbd_transfer(c->uc_xfer);
	}

	err = usbd_open_pipe_intr(sc->sc_iface, sc->sc_ed[UAQ_ENDPT_INTR],
	    0, &sc->sc_ep[UAQ_ENDPT_INTR], sc,
	    &sc->sc_link_status, sizeof(sc->sc_link_status), uaq_intr,
	    USBD_DEFAULT_INTERVAL);
	if (err) {
		printf("%s: couldn't open interrupt pipe\n",
		    sc->sc_dev.dv_xname);
		splx(s);
		return;
	}

	uaq_iff(sc);

	uaq_ifmedia_upd(ifp);

	ifp->if_flags |= IFF_RUNNING;
	ifq_clr_oactive(&ifp->if_snd);

	splx(s);
}

int
uaq_ioctl(struct ifnet *ifp, u_long cmd, caddr_t data)
{
	struct uaq_softc	*sc = ifp->if_softc;
	struct ifreq		*ifr = (struct ifreq *)data;
	int			s, error = 0;

	s = splnet();

	switch (cmd) {
	case SIOCSIFADDR:
		ifp->if_flags |= IFF_UP;
		if (!(ifp->if_flags & IFF_RUNNING))
			uaq_init(sc);
		break;

	case SIOCSIFFLAGS:
		if (ifp->if_flags & IFF_UP) {
			if (ifp->if_flags & IFF_RUNNING)
				error = ENETRESET;
			else
				uaq_init(sc);
		} else {
			if (ifp->if_flags & IFF_RUNNING)
				uaq_stop(sc);
		}
		break;

	case SIOCGIFMEDIA:
	case SIOCSIFMEDIA:
		error = ifmedia_ioctl(ifp, ifr, &sc->sc_ifmedia, cmd);
		break;

	default:
		error = ether_ioctl(ifp, &sc->sc_ac, cmd, data);
	}

	if (error == ENETRESET) {
		if (ifp->if_flags & IFF_RUNNING)
			uaq_iff(sc);
		error = 0;
	}

	splx(s);

	return (error);
}

int
uaq_xfer_list_init(struct uaq_softc *sc, struct uaq_chain *ch,
    uint32_t bufsize, int listlen)
{
	struct uaq_chain	*c;
	int			i;

	for (i = 0; i < listlen; i++) {
		c = &ch[i];
		c->uc_sc = sc;
		c->uc_idx = i;
		c->uc_buflen = 0;
		c->uc_bufmax = bufsize;
		c->uc_cnt = 0;
		if (c->uc_xfer == NULL) {
			c->uc_xfer = usbd_alloc_xfer(sc->sc_udev);
			if (c->uc_xfer == NULL)
				return (ENOBUFS);

			c->uc_buf = usbd_alloc_buffer(c->uc_xfer, c->uc_bufmax);
			if (c->uc_buf == NULL) {
				usbd_free_xfer(c->uc_xfer);
				c->uc_xfer = NULL;
				return (ENOBUFS);
			}
		}
	}

	return (0);
}

void
uaq_xfer_list_free(struct uaq_softc *sc, struct uaq_chain *ch, int listlen)
{
	int	i;

	for (i = 0; i < listlen; i++) {
		if (ch[i].uc_buf != NULL) {
			ch[i].uc_buf = NULL;
		}
		ch[i].uc_cnt = 0;
		if (ch[i].uc_xfer != NULL) {
			usbd_free_xfer(ch[i].uc_xfer);
			ch[i].uc_xfer = NULL;
		}
	}
}

void
uaq_stop(struct uaq_softc *sc)
{
	struct uaq_cdata	*cd;
	struct ifnet		*ifp;
	usbd_status		err;

	ifp = &sc->sc_ac.ac_if;
	ifp->if_timer = 0;
	ifp->if_flags &= ~IFF_RUNNING;
	ifq_clr_oactive(&ifp->if_snd);

	sc->sc_link_status = 0;
	sc->sc_link_speed = 0;

	if (sc->sc_ep[UAQ_ENDPT_RX] != NULL) {
		err = usbd_close_pipe(sc->sc_ep[UAQ_ENDPT_RX]);
		if (err) {
			printf("%s: close rx pipe failed: %s\n",
			    sc->sc_dev.dv_xname, usbd_errstr(err));
		}
		sc->sc_ep[UAQ_ENDPT_RX] = NULL;
	}

	if (sc->sc_ep[UAQ_ENDPT_TX] != NULL) {
		err = usbd_close_pipe(sc->sc_ep[UAQ_ENDPT_TX]);
		if (err) {
			printf("%s: close tx pipe failed: %s\n",
			    sc->sc_dev.dv_xname, usbd_errstr(err));
		}
		sc->sc_ep[UAQ_ENDPT_TX] = NULL;
	}

	if (sc->sc_ep[UAQ_ENDPT_INTR] != NULL) {
		err = usbd_close_pipe(sc->sc_ep[UAQ_ENDPT_INTR]);
		if (err) {
			printf("%s: close intr pipe failed: %s\n",
			    sc->sc_dev.dv_xname, usbd_errstr(err));
		}
		sc->sc_ep[UAQ_ENDPT_INTR] = NULL;
	}

	cd = &sc->sc_cdata;
	uaq_xfer_list_free(sc, cd->uaq_rx_chain, UAQ_RX_LIST_CNT);
	uaq_xfer_list_free(sc, cd->uaq_tx_chain, UAQ_TX_LIST_CNT);
}

void
uaq_link(struct uaq_softc *sc)
{
	if (sc->sc_link_speed > 0) {
		uint8_t resend[3] = { 0, 0xf8, 7 };
		uint8_t qctrl[5] = { 7, 0x00, 0x01, 0x1e, 0xff };
		uint8_t ipg = 0;

		switch (sc->sc_link_speed) {
		case UAQ_STATUS_SPEED_100M:
			resend[1] = 0xfb;
			resend[2] = 0x4;
			break;

		case UAQ_STATUS_SPEED_5G:
			ipg = 5;
			break;
		}

		uaq_write_1(sc, UAQ_CMD_ACCESS_MAC, UAQ_SFR_IPG_0, 1, ipg);

		uaq_write_mem(sc, UAQ_CMD_ACCESS_MAC, UAQ_SFR_TX_PAUSE_RESEND_T,
		    3, resend, 3);
		uaq_write_mem(sc, UAQ_CMD_ACCESS_MAC, UAQ_SFR_RX_BULKIN_QCTRL,
		    5, qctrl, 5);
		uaq_write_2(sc, UAQ_CMD_ACCESS_MAC, UAQ_SFR_PAUSE_WATERLVL_LOW,
		    2, 0x0810);

		uaq_write_1(sc, UAQ_CMD_ACCESS_MAC, UAQ_SFR_BMRX_DMA_CTRL, 1,
		    0);
		uaq_write_1(sc, UAQ_CMD_ACCESS_MAC, UAQ_SFR_BMTX_DMA_CTRL, 1,
		    0);
		uaq_write_1(sc, UAQ_CMD_ACCESS_MAC, UAQ_SFR_ARC_CTRL, 1, 0);

		sc->sc_rxctl = UAQ_SFR_RX_CTL_IPE | UAQ_SFR_RX_CTL_AB;
		uaq_write_2(sc, UAQ_CMD_ACCESS_MAC, UAQ_SFR_RX_CTL, 2,
		    sc->sc_rxctl);

		uaq_write_1(sc, UAQ_CMD_ACCESS_MAC, UAQ_SFR_ETH_MAC_PATH, 1,
		    UAQ_SFR_RX_PATH_READY);

		uaq_write_1(sc, UAQ_CMD_ACCESS_MAC, UAQ_SFR_BULK_OUT_CTRL, 1,
		    UAQ_SFR_BULK_OUT_EFF_EN);

		uaq_write_2(sc, UAQ_CMD_ACCESS_MAC, UAQ_SFR_MEDIUM_STATUS_MODE,
		    2, 0);
		uaq_write_2(sc, UAQ_CMD_ACCESS_MAC, UAQ_SFR_MEDIUM_STATUS_MODE,
		    2, UAQ_SFR_MEDIUM_XGMIIMODE | UAQ_SFR_MEDIUM_FULL_DUPLEX |
		    UAQ_SFR_MEDIUM_RECEIVE_EN | UAQ_SFR_MEDIUM_RXFLOW_CTRLEN |
		    UAQ_SFR_MEDIUM_TXFLOW_CTRLEN);	/* JUMBO_EN */

		uaq_write_1(sc, UAQ_CMD_ACCESS_MAC, UAQ_SFR_RXCOE_CTL, 1,
		    UAQ_SFR_RXCOE_IP | UAQ_SFR_RXCOE_TCP | UAQ_SFR_RXCOE_UDP |
		    UAQ_SFR_RXCOE_TCPV6 | UAQ_SFR_RXCOE_UDPV6);
		uaq_write_1(sc, UAQ_CMD_ACCESS_MAC, UAQ_SFR_TXCOE_CTL, 1,
		    UAQ_SFR_TXCOE_IP | UAQ_SFR_TXCOE_TCP | UAQ_SFR_TXCOE_UDP |
		    UAQ_SFR_TXCOE_TCPV6 | UAQ_SFR_TXCOE_UDPV6);

		sc->sc_rxctl |= UAQ_SFR_RX_CTL_START;
		uaq_write_2(sc, UAQ_CMD_ACCESS_MAC, UAQ_SFR_RX_CTL, 2,
		    sc->sc_rxctl);
	} else {
		uint16_t mode;

		mode = uaq_read_2(sc, UAQ_CMD_ACCESS_MAC,
		    UAQ_SFR_MEDIUM_STATUS_MODE, 2);
		mode &= ~UAQ_SFR_MEDIUM_RECEIVE_EN;
		uaq_write_2(sc, UAQ_CMD_ACCESS_MAC, UAQ_SFR_MEDIUM_STATUS_MODE,
		    2, mode);

		sc->sc_rxctl &= ~UAQ_SFR_RX_CTL_START;
		uaq_write_2(sc, UAQ_CMD_ACCESS_MAC, UAQ_SFR_RX_CTL, 2,
		    sc->sc_rxctl);

		uaq_write_1(sc, UAQ_CMD_ACCESS_MAC, UAQ_SFR_BULK_OUT_CTRL, 1,
		    UAQ_SFR_BULK_OUT_FLUSH_EN | UAQ_SFR_BULK_OUT_EFF_EN);

		uaq_write_1(sc, UAQ_CMD_ACCESS_MAC, UAQ_SFR_BULK_OUT_CTRL, 1,
		    UAQ_SFR_BULK_OUT_EFF_EN);
	}
}

void
uaq_intr(struct usbd_xfer *xfer, void *priv, usbd_status status)
{
	struct uaq_softc	*sc = priv;
	struct ifnet		*ifp = &sc->sc_ac.ac_if;
	uint64_t		linkstatus;
	uint64_t		baudrate;
	int			link_state;

	if (status == USBD_CANCELLED)
		return;

	if (status != USBD_NORMAL_COMPLETION) {
		DPRINTFN(2, ("uaq_intr: status=%d\n", status));
		if (status == USBD_STALLED)
			usbd_clear_endpoint_stall_async(
			    sc->sc_ep[UAQ_ENDPT_INTR]);
		return;
	}

	linkstatus = letoh64(sc->sc_link_status);
	DPRINTFN(1, ("uaq_intr: link status %llx\n", linkstatus));

	if (linkstatus & UAQ_STATUS_LINK) {
		link_state = LINK_STATE_FULL_DUPLEX;
		sc->sc_link_speed = (linkstatus & UAQ_STATUS_SPEED_MASK)
		    >> UAQ_STATUS_SPEED_SHIFT;
		switch (sc->sc_link_speed) {
		case UAQ_STATUS_SPEED_5G:
			baudrate = IF_Gbps(5);
			break;
		case UAQ_STATUS_SPEED_2_5G:
			baudrate = IF_Mbps(2500);
			break;
		case UAQ_STATUS_SPEED_1G:
			baudrate = IF_Gbps(1);
			break;
		case UAQ_STATUS_SPEED_100M:
			baudrate = IF_Mbps(100);
			break;
		default:
			baudrate = 0;
			break;
		}

		ifp->if_baudrate = baudrate;
	} else {
		link_state = LINK_STATE_DOWN;
		sc->sc_link_speed = 0;
	}

	if (link_state != ifp->if_link_state) {
		ifp->if_link_state = link_state;
		if_link_state_change(ifp);
		usb_add_task(sc->sc_udev, &sc->sc_link_task);
	}
}

void
uaq_rxeof(struct usbd_xfer *xfer, void *priv, usbd_status status)
{
	struct uaq_chain	*c = (struct uaq_chain *)priv;
	struct uaq_softc	*sc = c->uc_sc;
	struct ifnet		*ifp = &sc->sc_ac.ac_if;
	uint8_t			*buf;
	uint64_t		*pdesc;
	uint64_t		desc;
	uint32_t		total_len;
	struct mbuf_list	ml = MBUF_LIST_INITIALIZER();
	struct mbuf		*m;
	int			pktlen, s;
	int			count, offset;

	if (usbd_is_dying(sc->sc_udev))
		return;

	if (!(ifp->if_flags & IFF_RUNNING))
		return;

	if (status != USBD_NORMAL_COMPLETION) {
		if (status == USBD_NOT_STARTED || status == USBD_CANCELLED)
			return;
		if (usbd_ratecheck(&sc->sc_rx_notice)) {
			printf("%s: usb errors on rx: %s\n",
				sc->sc_dev.dv_xname, usbd_errstr(status));
		}
		if (status == USBD_STALLED)
			usbd_clear_endpoint_stall_async(
			    sc->sc_ep[UAQ_ENDPT_RX]);
		goto done;
	}

	usbd_get_xfer_status(xfer, NULL, (void **)&buf, &total_len, NULL);
	DPRINTFN(3, ("received %d bytes\n", total_len));
	if ((total_len & 7) != 0) {
		printf("%s: weird rx transfer length %d\n",
		    sc->sc_dev.dv_xname, total_len);
		goto done;
	}

	pdesc = (uint64_t *)(buf + (total_len - sizeof(desc)));
	desc = lemtoh64(pdesc);

	count = desc & UAQ_RX_HDR_COUNT_MASK;
	if (count == 0)
		goto done;

	/* get offset of packet headers */
	offset = total_len - ((count + 1) * sizeof(desc));
	if (offset != ((desc & UAQ_RX_HDR_OFFSET_MASK) >>
	    UAQ_RX_HDR_OFFSET_SHIFT)) {
		printf("%s: offset mismatch, got %d expected %lld\n",
		    sc->sc_dev.dv_xname, offset,
		    desc >> UAQ_RX_HDR_OFFSET_SHIFT);
		goto done;
	}
	if (offset < 0 || offset > total_len) {
		printf("%s: offset %d outside buffer (%d)\n",
		    sc->sc_dev.dv_xname, offset, total_len);
		goto done;
	}

	pdesc = (uint64_t *)(buf + offset);
	total_len = offset;

	while (count-- > 0) {
		desc = lemtoh64(pdesc);
		pdesc++;

		pktlen = (desc & UAQ_RX_PKT_LEN_MASK) >> UAQ_RX_PKT_LEN_SHIFT;
		if (pktlen > total_len) {
			DPRINTFN(2, ("not enough bytes for this packet\n"));
			ifp->if_ierrors++;
			goto done;
		}

		m = m_devget(buf + 2, pktlen - 2, ETHER_ALIGN);
		if (m == NULL) {
			DPRINTFN(2, ("m_devget failed for this packet\n"));
			ifp->if_ierrors++;
			goto done;
		}

		if ((desc & UAQ_RX_PKT_L3_ERR) == 0)
			m->m_pkthdr.csum_flags |= M_IPV4_CSUM_IN_OK;

		if ((desc & UAQ_RX_PKT_L4_ERR) == 0)
			m->m_pkthdr.csum_flags |= M_TCP_CSUM_IN_OK |
			    M_UDP_CSUM_IN_OK;

#if NVLAN > 0
		if (desc & UAQ_RX_PKT_VLAN) {
			m->m_pkthdr.ether_vtag = (desc >> UAQ_RX_PKT_VLAN_SHIFT) &
			    0xfff;
			m->m_flags |= M_VLANTAG;
		}
#endif
		ml_enqueue(&ml, m);

		total_len -= roundup(pktlen, UAQ_RX_BUF_ALIGN);
		buf += roundup(pktlen, UAQ_RX_BUF_ALIGN);
	}

done:
	s = splnet();
	if_input(ifp, &ml);
	splx(s);
	memset(c->uc_buf, 0, UAQ_RX_BUFSZ);

	usbd_setup_xfer(xfer, sc->sc_ep[UAQ_ENDPT_RX], c, c->uc_buf,
	    UAQ_RX_BUFSZ, USBD_SHORT_XFER_OK | USBD_NO_COPY,
	    USBD_NO_TIMEOUT, uaq_rxeof);
	usbd_transfer(xfer);
}


void
uaq_watchdog(struct ifnet *ifp)
{
	struct uaq_softc	*sc = ifp->if_softc;
	struct uaq_chain	*c;
	usbd_status		err;
	int			i, s;

	ifp->if_timer = 0;

	if (usbd_is_dying(sc->sc_udev))
		return;

	if ((ifp->if_flags & (IFF_RUNNING|IFF_UP)) != (IFF_RUNNING|IFF_UP))
		return;

	sc = ifp->if_softc;
	s = splnet();

	ifp->if_oerrors++;
	DPRINTF(("%s: watchdog timeout\n", sc->sc_dev.dv_xname));

	for (i = 0; i < UAQ_TX_LIST_CNT; i++) {
		c = &sc->sc_cdata.uaq_tx_chain[i];
		if (c->uc_cnt > 0) {
			usbd_get_xfer_status(c->uc_xfer, NULL, NULL, NULL,
			    &err);
			uaq_txeof(c->uc_xfer, c, err);
		}
	}

	if (ifq_is_oactive(&ifp->if_snd))
		ifq_restart(&ifp->if_snd);
	splx(s);
}

void
uaq_txeof(struct usbd_xfer *xfer, void *priv, usbd_status status)
{
	struct uaq_softc	*sc;
	struct uaq_chain	*c;
	struct ifnet		*ifp;
	int			s;

	c = priv;
	sc = c->uc_sc;
	ifp = &sc->sc_ac.ac_if;

	if (usbd_is_dying(sc->sc_udev))
		return;

	if (status != USBD_NORMAL_COMPLETION)
		DPRINTF(("%s: %s uc_idx=%u : %s\n", sc->sc_dev.dv_xname,
			__func__, c->uc_idx, usbd_errstr(status)));
	else
		DPRINTF(("%s: txeof\n", sc->sc_dev.dv_xname));

	s = splnet();

	c->uc_cnt = 0;
	c->uc_buflen = 0;

	SLIST_INSERT_HEAD(&sc->sc_cdata.uaq_tx_free, c, uc_list);

	if (status != USBD_NORMAL_COMPLETION) {
		if (status == USBD_NOT_STARTED || status == USBD_CANCELLED) {
			splx(s);
			return;
		}

		ifp->if_oerrors++;
		printf("%s: usb error on tx: %s\n", sc->sc_dev.dv_xname,
		    usbd_errstr(status));

		if (status == USBD_STALLED)
			usbd_clear_endpoint_stall_async(
			    sc->sc_ep[UAQ_ENDPT_TX]);
		splx(s);
		return;
	}

	ifp->if_timer = 0;
	if (ifq_is_oactive(&ifp->if_snd))
		ifq_restart(&ifp->if_snd);
	splx(s);
}

void
uaq_start(struct ifnet *ifp)
{
	struct uaq_softc	*sc = ifp->if_softc;
	struct uaq_cdata	*cd = &sc->sc_cdata;
	struct uaq_chain	*c;
	struct mbuf		*m = NULL;
	int			s, mlen;

	if ((sc->sc_link_speed == 0) ||
		(ifp->if_flags & (IFF_RUNNING|IFF_UP)) !=
		    (IFF_RUNNING|IFF_UP)) {
		return;
	}

	s = splnet();

	c = SLIST_FIRST(&cd->uaq_tx_free);
	while (c != NULL) {
		m = ifq_deq_begin(&ifp->if_snd);
		if (m == NULL)
			break;

		mlen = m->m_pkthdr.len;

		/* Discard packet larger than buffer. */
		if (mlen + sizeof(uint64_t) >= c->uc_bufmax) {
			ifq_deq_commit(&ifp->if_snd, m);
			m_freem(m);
			ifp->if_oerrors++;
			continue;
		}

		/* Append packet to current buffer. */
		mlen = uaq_encap_txpkt(sc, m, c->uc_buf + c->uc_buflen,
		    c->uc_bufmax - c->uc_buflen);
		if (mlen <= 0) {
			ifq_deq_rollback(&ifp->if_snd, m);
			break;
		}

		ifq_deq_commit(&ifp->if_snd, m);
		c->uc_cnt += 1;
		c->uc_buflen += mlen;

#if NBPFILTER > 0
		if (ifp->if_bpf)
			bpf_mtap(ifp->if_bpf, m, BPF_DIRECTION_OUT);
#endif

		m_freem(m);
	}

	if (c != NULL) {
		/* Send current buffer unless empty */
		if (c->uc_buflen > 0 && c->uc_cnt > 0) {
			SLIST_REMOVE_HEAD(&cd->uaq_tx_free, uc_list);
			if (uaq_encap_xfer(sc, c)) {
				SLIST_INSERT_HEAD(&cd->uaq_tx_free, c,
				    uc_list);
			}
			c = SLIST_FIRST(&cd->uaq_tx_free);

			ifp->if_timer = 5;
			if (c == NULL)
				ifq_set_oactive(&ifp->if_snd);
		}
	}

	splx(s);
}

int
uaq_encap_txpkt(struct uaq_softc *sc, struct mbuf *m, char *buf,
    uint32_t maxlen)
{
	uint64_t		desc;
	int			padded;

	desc = m->m_pkthdr.len;
	padded = roundup(m->m_pkthdr.len, UAQ_TX_BUF_ALIGN);
	if (((padded + sizeof(desc)) % sc->sc_out_frame_size) == 0) {
		desc |= UAQ_TX_PKT_DROP_PADD;
		padded += 8;
	}

	if (padded + sizeof(desc) > maxlen)
		return (-1);

#if NVLAN > 0
	if (m->m_flags & M_VLANTAG)
		desc |= (((uint64_t)m->m_pkthdr.ether_vtag) <<
		    UAQ_TX_PKT_VLAN_SHIFT) | UAQ_TX_PKT_VLAN;
#endif

	htolem64((uint64_t *)buf, desc);
	m_copydata(m, 0, m->m_pkthdr.len, buf + sizeof(desc));
	return (padded + sizeof(desc));
}

int
uaq_encap_xfer(struct uaq_softc *sc, struct uaq_chain *c)
{
	usbd_status	err;

	usbd_setup_xfer(c->uc_xfer, sc->sc_ep[UAQ_ENDPT_TX], c, c->uc_buf,
	    c->uc_buflen, USBD_FORCE_SHORT_XFER | USBD_NO_COPY, 10000,
	    uaq_txeof);

	err = usbd_transfer(c->uc_xfer);
	if (err != USBD_IN_PROGRESS) {
		c->uc_cnt = 0;
		c->uc_buflen = 0;
		uaq_stop(sc);
		return (EIO);
	}

	return (0);
}
