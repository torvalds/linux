/*-
 * Copyright (c) 2015-2016 Kevin Lo <kevlo@FreeBSD.org>
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

#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/bus.h>
#include <sys/condvar.h>
#include <sys/kernel.h>
#include <sys/lock.h>
#include <sys/module.h>
#include <sys/mutex.h>
#include <sys/socket.h>
#include <sys/sysctl.h>
#include <sys/unistd.h>

#include <net/if.h>
#include <net/if_var.h>

#include <dev/usb/usb.h>
#include <dev/usb/usbdi.h>
#include <dev/usb/usbdi_util.h>
#include "usbdevs.h"

#define USB_DEBUG_VAR	ure_debug
#include <dev/usb/usb_debug.h>
#include <dev/usb/usb_process.h>

#include <dev/usb/net/usb_ethernet.h>
#include <dev/usb/net/if_urereg.h>

#ifdef USB_DEBUG
static int ure_debug = 0;

static SYSCTL_NODE(_hw_usb, OID_AUTO, ure, CTLFLAG_RW, 0, "USB ure");
SYSCTL_INT(_hw_usb_ure, OID_AUTO, debug, CTLFLAG_RWTUN, &ure_debug, 0,
    "Debug level");
#endif

/*
 * Various supported device vendors/products.
 */
static const STRUCT_USB_HOST_ID ure_devs[] = {
#define	URE_DEV(v,p,i)	{ USB_VPI(USB_VENDOR_##v, USB_PRODUCT_##v##_##p, i) }
	URE_DEV(LENOVO, RTL8153, 0),
	URE_DEV(LENOVO, TBT3LAN, 0),
	URE_DEV(LENOVO, ONELINK, 0),
	URE_DEV(LENOVO, USBCLAN, 0),
	URE_DEV(NVIDIA, RTL8153, 0),
	URE_DEV(REALTEK, RTL8152, URE_FLAG_8152),
	URE_DEV(REALTEK, RTL8153, 0),
	URE_DEV(TPLINK, RTL8153, 0),
#undef URE_DEV
};

static device_probe_t ure_probe;
static device_attach_t ure_attach;
static device_detach_t ure_detach;

static usb_callback_t ure_bulk_read_callback;
static usb_callback_t ure_bulk_write_callback;

static miibus_readreg_t ure_miibus_readreg;
static miibus_writereg_t ure_miibus_writereg;
static miibus_statchg_t ure_miibus_statchg;

static uether_fn_t ure_attach_post;
static uether_fn_t ure_init;
static uether_fn_t ure_stop;
static uether_fn_t ure_start;
static uether_fn_t ure_tick;
static uether_fn_t ure_rxfilter;

static int	ure_ctl(struct ure_softc *, uint8_t, uint16_t, uint16_t,
		    void *, int);
static int	ure_read_mem(struct ure_softc *, uint16_t, uint16_t, void *,
		    int);
static int	ure_write_mem(struct ure_softc *, uint16_t, uint16_t, void *,
		    int);
static uint8_t	ure_read_1(struct ure_softc *, uint16_t, uint16_t);
static uint16_t	ure_read_2(struct ure_softc *, uint16_t, uint16_t);
static uint32_t	ure_read_4(struct ure_softc *, uint16_t, uint16_t);
static int	ure_write_1(struct ure_softc *, uint16_t, uint16_t, uint32_t);
static int	ure_write_2(struct ure_softc *, uint16_t, uint16_t, uint32_t);
static int	ure_write_4(struct ure_softc *, uint16_t, uint16_t, uint32_t);
static uint16_t	ure_ocp_reg_read(struct ure_softc *, uint16_t);
static void	ure_ocp_reg_write(struct ure_softc *, uint16_t, uint16_t);

static void	ure_read_chipver(struct ure_softc *);
static int	ure_attach_post_sub(struct usb_ether *);
static void	ure_reset(struct ure_softc *);
static int	ure_ifmedia_upd(struct ifnet *);
static void	ure_ifmedia_sts(struct ifnet *, struct ifmediareq *);
static int	ure_ioctl(struct ifnet *, u_long, caddr_t);
static void	ure_rtl8152_init(struct ure_softc *);
static void	ure_rtl8153_init(struct ure_softc *);
static void	ure_disable_teredo(struct ure_softc *);
static void	ure_init_fifo(struct ure_softc *);

static const struct usb_config ure_config[URE_N_TRANSFER] = {
	[URE_BULK_DT_WR] = {
		.type = UE_BULK,
		.endpoint = UE_ADDR_ANY,
		.direction = UE_DIR_OUT,
		.bufsize = MCLBYTES,
		.flags = {.pipe_bof = 1,.force_short_xfer = 1,},
		.callback = ure_bulk_write_callback,
		.timeout = 10000,	/* 10 seconds */
	},
	[URE_BULK_DT_RD] = {
		.type = UE_BULK,
		.endpoint = UE_ADDR_ANY,
		.direction = UE_DIR_IN,
		.bufsize = 16384,
		.flags = {.pipe_bof = 1,.short_xfer_ok = 1,},
		.callback = ure_bulk_read_callback,
		.timeout = 0,	/* no timeout */
	},
};

static device_method_t ure_methods[] = {
	/* Device interface. */
	DEVMETHOD(device_probe, ure_probe),
	DEVMETHOD(device_attach, ure_attach),
	DEVMETHOD(device_detach, ure_detach),

	/* MII interface. */
	DEVMETHOD(miibus_readreg, ure_miibus_readreg),
	DEVMETHOD(miibus_writereg, ure_miibus_writereg),
	DEVMETHOD(miibus_statchg, ure_miibus_statchg),

	DEVMETHOD_END
};

static driver_t ure_driver = {
	.name = "ure",
	.methods = ure_methods,
	.size = sizeof(struct ure_softc),
};

static devclass_t ure_devclass;

DRIVER_MODULE(ure, uhub, ure_driver, ure_devclass, NULL, NULL);
DRIVER_MODULE(miibus, ure, miibus_driver, miibus_devclass, NULL, NULL);
MODULE_DEPEND(ure, uether, 1, 1, 1);
MODULE_DEPEND(ure, usb, 1, 1, 1);
MODULE_DEPEND(ure, ether, 1, 1, 1);
MODULE_DEPEND(ure, miibus, 1, 1, 1);
MODULE_VERSION(ure, 1);
USB_PNP_HOST_INFO(ure_devs);

static const struct usb_ether_methods ure_ue_methods = {
	.ue_attach_post = ure_attach_post,
	.ue_attach_post_sub = ure_attach_post_sub,
	.ue_start = ure_start,
	.ue_init = ure_init,
	.ue_stop = ure_stop,
	.ue_tick = ure_tick,
	.ue_setmulti = ure_rxfilter,
	.ue_setpromisc = ure_rxfilter,
	.ue_mii_upd = ure_ifmedia_upd,
	.ue_mii_sts = ure_ifmedia_sts,
};

static int
ure_ctl(struct ure_softc *sc, uint8_t rw, uint16_t val, uint16_t index,
    void *buf, int len)
{
	struct usb_device_request req;

	URE_LOCK_ASSERT(sc, MA_OWNED);

	if (rw == URE_CTL_WRITE)
		req.bmRequestType = UT_WRITE_VENDOR_DEVICE;
	else
		req.bmRequestType = UT_READ_VENDOR_DEVICE;
	req.bRequest = UR_SET_ADDRESS;
	USETW(req.wValue, val);
	USETW(req.wIndex, index);
	USETW(req.wLength, len);

	return (uether_do_request(&sc->sc_ue, &req, buf, 1000));
}

static int
ure_read_mem(struct ure_softc *sc, uint16_t addr, uint16_t index,
    void *buf, int len)
{

	return (ure_ctl(sc, URE_CTL_READ, addr, index, buf, len));
}

static int
ure_write_mem(struct ure_softc *sc, uint16_t addr, uint16_t index,
    void *buf, int len)
{

	return (ure_ctl(sc, URE_CTL_WRITE, addr, index, buf, len));
}

static uint8_t
ure_read_1(struct ure_softc *sc, uint16_t reg, uint16_t index)
{
	uint32_t val;
	uint8_t temp[4];
	uint8_t shift;

	shift = (reg & 3) << 3;
	reg &= ~3;
	
	ure_read_mem(sc, reg, index, &temp, 4);
	val = UGETDW(temp);
	val >>= shift;

	return (val & 0xff);
}

static uint16_t
ure_read_2(struct ure_softc *sc, uint16_t reg, uint16_t index)
{
	uint32_t val;
	uint8_t temp[4];
	uint8_t shift;

	shift = (reg & 2) << 3;
	reg &= ~3;

	ure_read_mem(sc, reg, index, &temp, 4);
	val = UGETDW(temp);
	val >>= shift;

	return (val & 0xffff);
}

static uint32_t
ure_read_4(struct ure_softc *sc, uint16_t reg, uint16_t index)
{
	uint8_t temp[4];

	ure_read_mem(sc, reg, index, &temp, 4);
	return (UGETDW(temp));
}

static int
ure_write_1(struct ure_softc *sc, uint16_t reg, uint16_t index, uint32_t val)
{
	uint16_t byen;
	uint8_t temp[4];
	uint8_t shift;

	byen = URE_BYTE_EN_BYTE;
	shift = reg & 3;
	val &= 0xff;

	if (reg & 3) {
		byen <<= shift;
		val <<= (shift << 3);
		reg &= ~3;
	}

	USETDW(temp, val);
	return (ure_write_mem(sc, reg, index | byen, &temp, 4));
}

static int
ure_write_2(struct ure_softc *sc, uint16_t reg, uint16_t index, uint32_t val)
{
	uint16_t byen;
	uint8_t temp[4];
	uint8_t shift;

	byen = URE_BYTE_EN_WORD;
	shift = reg & 2;
	val &= 0xffff;

	if (reg & 2) {
		byen <<= shift;
		val <<= (shift << 3);
		reg &= ~3;
	}

	USETDW(temp, val);
	return (ure_write_mem(sc, reg, index | byen, &temp, 4));
}

static int
ure_write_4(struct ure_softc *sc, uint16_t reg, uint16_t index, uint32_t val)
{
	uint8_t temp[4];

	USETDW(temp, val);
	return (ure_write_mem(sc, reg, index | URE_BYTE_EN_DWORD, &temp, 4));
}

static uint16_t
ure_ocp_reg_read(struct ure_softc *sc, uint16_t addr)
{
	uint16_t reg;

	ure_write_2(sc, URE_PLA_OCP_GPHY_BASE, URE_MCU_TYPE_PLA, addr & 0xf000);
	reg = (addr & 0x0fff) | 0xb000;

	return (ure_read_2(sc, reg, URE_MCU_TYPE_PLA));
}

static void
ure_ocp_reg_write(struct ure_softc *sc, uint16_t addr, uint16_t data)
{
	uint16_t reg;

	ure_write_2(sc, URE_PLA_OCP_GPHY_BASE, URE_MCU_TYPE_PLA, addr & 0xf000);
	reg = (addr & 0x0fff) | 0xb000;

	ure_write_2(sc, reg, URE_MCU_TYPE_PLA, data);
}

static int
ure_miibus_readreg(device_t dev, int phy, int reg)
{
	struct ure_softc *sc;
	uint16_t val;
	int locked;

	sc = device_get_softc(dev);
	locked = mtx_owned(&sc->sc_mtx);
	if (!locked)
		URE_LOCK(sc);

	/* Let the rgephy driver read the URE_GMEDIASTAT register. */
	if (reg == URE_GMEDIASTAT) {
		if (!locked)
			URE_UNLOCK(sc);
		return (ure_read_1(sc, URE_GMEDIASTAT, URE_MCU_TYPE_PLA));
	}

	val = ure_ocp_reg_read(sc, URE_OCP_BASE_MII + reg * 2);

	if (!locked)
		URE_UNLOCK(sc);
	return (val);
}

static int
ure_miibus_writereg(device_t dev, int phy, int reg, int val)
{
	struct ure_softc *sc;
	int locked;

	sc = device_get_softc(dev);
	if (sc->sc_phyno != phy)
		return (0);

	locked = mtx_owned(&sc->sc_mtx);
	if (!locked)
		URE_LOCK(sc);
	
	ure_ocp_reg_write(sc, URE_OCP_BASE_MII + reg * 2, val);

	if (!locked)
		URE_UNLOCK(sc);
	return (0);
}

static void
ure_miibus_statchg(device_t dev)
{
	struct ure_softc *sc;
	struct mii_data *mii;
	struct ifnet *ifp;
	int locked;

	sc = device_get_softc(dev);
	mii = GET_MII(sc);
	locked = mtx_owned(&sc->sc_mtx);
	if (!locked)
		URE_LOCK(sc);

	ifp = uether_getifp(&sc->sc_ue);
	if (mii == NULL || ifp == NULL ||
	    (ifp->if_drv_flags & IFF_DRV_RUNNING) == 0)
		goto done;

	sc->sc_flags &= ~URE_FLAG_LINK;
	if ((mii->mii_media_status & (IFM_ACTIVE | IFM_AVALID)) ==
	    (IFM_ACTIVE | IFM_AVALID)) {
		switch (IFM_SUBTYPE(mii->mii_media_active)) {
		case IFM_10_T:
		case IFM_100_TX:
			sc->sc_flags |= URE_FLAG_LINK;
			break;
		case IFM_1000_T:
			if ((sc->sc_flags & URE_FLAG_8152) != 0)
				break;
			sc->sc_flags |= URE_FLAG_LINK;
			break;
		default:
			break;
		}
	}

	/* Lost link, do nothing. */
	if ((sc->sc_flags & URE_FLAG_LINK) == 0)
		goto done;
done:
	if (!locked)
		URE_UNLOCK(sc);
}

/*
 * Probe for a RTL8152/RTL8153 chip.
 */
static int
ure_probe(device_t dev)
{
	struct usb_attach_arg *uaa;

	uaa = device_get_ivars(dev);
	if (uaa->usb_mode != USB_MODE_HOST)
		return (ENXIO);
	if (uaa->info.bConfigIndex != URE_CONFIG_IDX)
		return (ENXIO);
	if (uaa->info.bIfaceIndex != URE_IFACE_IDX)
		return (ENXIO);

	return (usbd_lookup_id_by_uaa(ure_devs, sizeof(ure_devs), uaa));
}

/*
 * Attach the interface. Allocate softc structures, do ifmedia
 * setup and ethernet/BPF attach.
 */
static int
ure_attach(device_t dev)
{
	struct usb_attach_arg *uaa = device_get_ivars(dev);
	struct ure_softc *sc = device_get_softc(dev);
	struct usb_ether *ue = &sc->sc_ue;
	uint8_t iface_index;
	int error;

	sc->sc_flags = USB_GET_DRIVER_INFO(uaa);
	device_set_usb_desc(dev);
	mtx_init(&sc->sc_mtx, device_get_nameunit(dev), NULL, MTX_DEF);

	iface_index = URE_IFACE_IDX;
	error = usbd_transfer_setup(uaa->device, &iface_index, sc->sc_xfer,
	    ure_config, URE_N_TRANSFER, sc, &sc->sc_mtx);
	if (error != 0) {
		device_printf(dev, "allocating USB transfers failed\n");
		goto detach;
	}

	ue->ue_sc = sc;
	ue->ue_dev = dev;
	ue->ue_udev = uaa->device;
	ue->ue_mtx = &sc->sc_mtx;
	ue->ue_methods = &ure_ue_methods;

	error = uether_ifattach(ue);
	if (error != 0) {
		device_printf(dev, "could not attach interface\n");
		goto detach;
	}
	return (0);			/* success */

detach:
	ure_detach(dev);
	return (ENXIO);			/* failure */
}

static int
ure_detach(device_t dev)
{
	struct ure_softc *sc = device_get_softc(dev);
	struct usb_ether *ue = &sc->sc_ue;

	usbd_transfer_unsetup(sc->sc_xfer, URE_N_TRANSFER);
	uether_ifdetach(ue);
	mtx_destroy(&sc->sc_mtx);

	return (0);
}

static void
ure_bulk_read_callback(struct usb_xfer *xfer, usb_error_t error)
{
	struct ure_softc *sc = usbd_xfer_softc(xfer);
	struct usb_ether *ue = &sc->sc_ue;
	struct ifnet *ifp = uether_getifp(ue);
	struct usb_page_cache *pc;
	struct ure_rxpkt pkt;
	int actlen, len;

	usbd_xfer_status(xfer, &actlen, NULL, NULL, NULL);

	switch (USB_GET_STATE(xfer)) {
	case USB_ST_TRANSFERRED:
		if (actlen < (int)(sizeof(pkt))) {
			if_inc_counter(ifp, IFCOUNTER_IERRORS, 1);
			goto tr_setup;
		}
		pc = usbd_xfer_get_frame(xfer, 0);
		usbd_copy_out(pc, 0, &pkt, sizeof(pkt));
		len = le32toh(pkt.ure_pktlen) & URE_RXPKT_LEN_MASK;
		len -= ETHER_CRC_LEN;
		if (actlen < (int)(len + sizeof(pkt))) {
			if_inc_counter(ifp, IFCOUNTER_IERRORS, 1);
			goto tr_setup;
		}

		uether_rxbuf(ue, pc, sizeof(pkt), len);
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
ure_bulk_write_callback(struct usb_xfer *xfer, usb_error_t error)
{
	struct ure_softc *sc = usbd_xfer_softc(xfer);
	struct ifnet *ifp = uether_getifp(&sc->sc_ue);
	struct usb_page_cache *pc;
	struct mbuf *m;
	struct ure_txpkt txpkt;
	int len, pos;

	switch (USB_GET_STATE(xfer)) {
	case USB_ST_TRANSFERRED:
		DPRINTFN(11, "transfer complete\n");
		ifp->if_drv_flags &= ~IFF_DRV_OACTIVE;
		/* FALLTHROUGH */
	case USB_ST_SETUP:
tr_setup:
		if ((sc->sc_flags & URE_FLAG_LINK) == 0 ||
		    (ifp->if_drv_flags & IFF_DRV_OACTIVE) != 0) {
			/*
			 * don't send anything if there is no link !
			 */
			return;
		}
		IFQ_DRV_DEQUEUE(&ifp->if_snd, m);
		if (m == NULL)
			break;
		pos = 0;
		len = m->m_pkthdr.len;
		pc = usbd_xfer_get_frame(xfer, 0);
		memset(&txpkt, 0, sizeof(txpkt));
		txpkt.ure_pktlen = htole32((len & URE_TXPKT_LEN_MASK) |
		    URE_TKPKT_TX_FS | URE_TKPKT_TX_LS);
		usbd_copy_in(pc, pos, &txpkt, sizeof(txpkt));
		pos += sizeof(txpkt);
		usbd_m_copy_in(pc, pos, m, 0, m->m_pkthdr.len);
		pos += m->m_pkthdr.len;

		if_inc_counter(ifp, IFCOUNTER_OPACKETS, 1);

		/*
		 * If there's a BPF listener, bounce a copy
		 * of this frame to him.
		 */
		BPF_MTAP(ifp, m);

		m_freem(m);

		/* Set frame length. */
		usbd_xfer_set_frame_len(xfer, 0, pos);

		usbd_transfer_submit(xfer);
		ifp->if_drv_flags |= IFF_DRV_OACTIVE;
		return;
	default:			/* Error */
		DPRINTFN(11, "transfer error, %s\n",
		    usbd_errstr(error));

		if_inc_counter(ifp, IFCOUNTER_OERRORS, 1);
		ifp->if_drv_flags &= ~IFF_DRV_OACTIVE;

		if (error != USB_ERR_CANCELLED) {
			/* try to clear stall first */
			usbd_xfer_set_stall(xfer);
			goto tr_setup;
		}
		return;
	}
}

static void
ure_read_chipver(struct ure_softc *sc)
{
	uint16_t ver;

	ver = ure_read_2(sc, URE_PLA_TCR1, URE_MCU_TYPE_PLA) & URE_VERSION_MASK;
	switch (ver) {
	case 0x4c00:
		sc->sc_chip |= URE_CHIP_VER_4C00;
		break;
	case 0x4c10:
		sc->sc_chip |= URE_CHIP_VER_4C10;
		break;
	case 0x5c00:
		sc->sc_chip |= URE_CHIP_VER_5C00;
		break;
	case 0x5c10:
		sc->sc_chip |= URE_CHIP_VER_5C10;
		break;
	case 0x5c20:
		sc->sc_chip |= URE_CHIP_VER_5C20;
		break;
	case 0x5c30:
		sc->sc_chip |= URE_CHIP_VER_5C30;
		break;
	default:
		device_printf(sc->sc_ue.ue_dev,
		    "unknown version 0x%04x\n", ver);
		break;
	}
}

static void
ure_attach_post(struct usb_ether *ue)
{
	struct ure_softc *sc = uether_getsc(ue);

	sc->sc_phyno = 0;

	/* Determine the chip version. */
	ure_read_chipver(sc);

	/* Initialize controller and get station address. */
	if (sc->sc_flags & URE_FLAG_8152)
		ure_rtl8152_init(sc);
	else
		ure_rtl8153_init(sc);

	if (sc->sc_chip & URE_CHIP_VER_4C00)
		ure_read_mem(sc, URE_PLA_IDR, URE_MCU_TYPE_PLA,
		    ue->ue_eaddr, 8);
	else
		ure_read_mem(sc, URE_PLA_BACKUP, URE_MCU_TYPE_PLA,
		    ue->ue_eaddr, 8);
}

static int
ure_attach_post_sub(struct usb_ether *ue)
{
	struct ure_softc *sc;
	struct ifnet *ifp;
	int error;

	sc = uether_getsc(ue);
	ifp = ue->ue_ifp;
	ifp->if_flags = IFF_BROADCAST | IFF_SIMPLEX | IFF_MULTICAST;
	ifp->if_start = uether_start;
	ifp->if_ioctl = ure_ioctl;
	ifp->if_init = uether_init;
	IFQ_SET_MAXLEN(&ifp->if_snd, ifqmaxlen);
	ifp->if_snd.ifq_drv_maxlen = ifqmaxlen;
	IFQ_SET_READY(&ifp->if_snd);

	mtx_lock(&Giant);
	error = mii_attach(ue->ue_dev, &ue->ue_miibus, ifp,
	    uether_ifmedia_upd, ue->ue_methods->ue_mii_sts,
	    BMSR_DEFCAPMASK, sc->sc_phyno, MII_OFFSET_ANY, 0);
	mtx_unlock(&Giant);

	return (error);
}

static void
ure_init(struct usb_ether *ue)
{
	struct ure_softc *sc = uether_getsc(ue);
	struct ifnet *ifp = uether_getifp(ue);

	URE_LOCK_ASSERT(sc, MA_OWNED);

	if ((ifp->if_drv_flags & IFF_DRV_RUNNING) != 0)
		return;

	/* Cancel pending I/O. */
	ure_stop(ue);

	ure_reset(sc);

	/* Set MAC address. */
	ure_write_mem(sc, URE_PLA_IDR, URE_MCU_TYPE_PLA | URE_BYTE_EN_SIX_BYTES,
	    IF_LLADDR(ifp), 8);

	/* Reset the packet filter. */
	ure_write_2(sc, URE_PLA_FMC, URE_MCU_TYPE_PLA,
	    ure_read_2(sc, URE_PLA_FMC, URE_MCU_TYPE_PLA) &
	    ~URE_FMC_FCR_MCU_EN);
	ure_write_2(sc, URE_PLA_FMC, URE_MCU_TYPE_PLA,
	    ure_read_2(sc, URE_PLA_FMC, URE_MCU_TYPE_PLA) |
	    URE_FMC_FCR_MCU_EN);
	    
	/* Enable transmit and receive. */
	ure_write_1(sc, URE_PLA_CR, URE_MCU_TYPE_PLA,
	    ure_read_1(sc, URE_PLA_CR, URE_MCU_TYPE_PLA) | URE_CR_RE |
	    URE_CR_TE);

	ure_write_2(sc, URE_PLA_MISC_1, URE_MCU_TYPE_PLA,
	    ure_read_2(sc, URE_PLA_MISC_1, URE_MCU_TYPE_PLA) &
	    ~URE_RXDY_GATED_EN);

	/*  Configure RX filters. */
	ure_rxfilter(ue);

	usbd_xfer_set_stall(sc->sc_xfer[URE_BULK_DT_WR]);

	/* Indicate we are up and running. */
	ifp->if_drv_flags |= IFF_DRV_RUNNING;

	/* Switch to selected media. */
	ure_ifmedia_upd(ifp);
}

static void
ure_tick(struct usb_ether *ue)
{
	struct ure_softc *sc = uether_getsc(ue);
	struct mii_data *mii = GET_MII(sc);

	URE_LOCK_ASSERT(sc, MA_OWNED);

	mii_tick(mii);
	if ((sc->sc_flags & URE_FLAG_LINK) == 0
	    && mii->mii_media_status & IFM_ACTIVE &&
	    IFM_SUBTYPE(mii->mii_media_active) != IFM_NONE) {
		sc->sc_flags |= URE_FLAG_LINK;
		ure_start(ue);
	}
}

/*
 * Program the 64-bit multicast hash filter.
 */
static void
ure_rxfilter(struct usb_ether *ue)
{
	struct ure_softc *sc = uether_getsc(ue);
	struct ifnet *ifp = uether_getifp(ue);
	struct ifmultiaddr *ifma;
	uint32_t h, rxmode;
	uint32_t hashes[2] = { 0, 0 };

	URE_LOCK_ASSERT(sc, MA_OWNED);

	rxmode = URE_RCR_APM;
	if (ifp->if_flags & IFF_BROADCAST)
		 rxmode |= URE_RCR_AB;
	if (ifp->if_flags & (IFF_ALLMULTI | IFF_PROMISC)) {
		if (ifp->if_flags & IFF_PROMISC)
			rxmode |= URE_RCR_AAP;
		rxmode |= URE_RCR_AM;
		hashes[0] = hashes[1] = 0xffffffff;
		goto done;
	}

	rxmode |= URE_RCR_AM;
	if_maddr_rlock(ifp);
	CK_STAILQ_FOREACH(ifma, &ifp->if_multiaddrs, ifma_link) {
		if (ifma->ifma_addr->sa_family != AF_LINK)
			continue;
		h = ether_crc32_be(LLADDR((struct sockaddr_dl *)
		ifma->ifma_addr), ETHER_ADDR_LEN) >> 26;
		if (h < 32)
			hashes[0] |= (1 << h);
		else
			hashes[1] |= (1 << (h - 32));
	}
	if_maddr_runlock(ifp);

	h = bswap32(hashes[0]);
	hashes[0] = bswap32(hashes[1]);
	hashes[1] = h;
	rxmode |= URE_RCR_AM;

done:
	ure_write_4(sc, URE_PLA_MAR0, URE_MCU_TYPE_PLA, hashes[0]);
	ure_write_4(sc, URE_PLA_MAR4, URE_MCU_TYPE_PLA, hashes[1]);
	ure_write_4(sc, URE_PLA_RCR, URE_MCU_TYPE_PLA, rxmode);
}

static void
ure_start(struct usb_ether *ue)
{
	struct ure_softc *sc = uether_getsc(ue);

	/*
	 * start the USB transfers, if not already started:
	 */
	usbd_transfer_start(sc->sc_xfer[URE_BULK_DT_RD]);
	usbd_transfer_start(sc->sc_xfer[URE_BULK_DT_WR]);
}

static void
ure_reset(struct ure_softc *sc)
{
	int i;

	ure_write_1(sc, URE_PLA_CR, URE_MCU_TYPE_PLA, URE_CR_RST);

	for (i = 0; i < URE_TIMEOUT; i++) {
		if (!(ure_read_1(sc, URE_PLA_CR, URE_MCU_TYPE_PLA) &
		    URE_CR_RST))
			break;
		uether_pause(&sc->sc_ue, hz / 100);
	}
	if (i == URE_TIMEOUT)
		device_printf(sc->sc_ue.ue_dev, "reset never completed\n");
}

/*
 * Set media options.
 */
static int
ure_ifmedia_upd(struct ifnet *ifp)
{
	struct ure_softc *sc = ifp->if_softc;
	struct mii_data *mii = GET_MII(sc);
	struct mii_softc *miisc;
	int error;

	URE_LOCK_ASSERT(sc, MA_OWNED);

	LIST_FOREACH(miisc, &mii->mii_phys, mii_list)
		PHY_RESET(miisc);
	error = mii_mediachg(mii);
	return (error);
}

/*
 * Report current media status.
 */
static void
ure_ifmedia_sts(struct ifnet *ifp, struct ifmediareq *ifmr)
{
	struct ure_softc *sc;
	struct mii_data *mii;

	sc = ifp->if_softc;
	mii = GET_MII(sc);

	URE_LOCK(sc);
	mii_pollstat(mii);
	ifmr->ifm_active = mii->mii_media_active;
	ifmr->ifm_status = mii->mii_media_status;
	URE_UNLOCK(sc);
}

static int
ure_ioctl(struct ifnet *ifp, u_long cmd, caddr_t data)
{
	struct usb_ether *ue = ifp->if_softc;
	struct ure_softc *sc;
	struct ifreq *ifr;
	int error, mask, reinit;

	sc = uether_getsc(ue);
	ifr = (struct ifreq *)data;
	error = 0;
	reinit = 0;
	if (cmd == SIOCSIFCAP) {
		URE_LOCK(sc);
		mask = ifr->ifr_reqcap ^ ifp->if_capenable;
		if (reinit > 0 && ifp->if_drv_flags & IFF_DRV_RUNNING)
			ifp->if_drv_flags &= ~IFF_DRV_RUNNING;
		else
			reinit = 0;
		URE_UNLOCK(sc);
		if (reinit > 0)
			uether_init(ue);
	} else
		error = uether_ioctl(ifp, cmd, data);

	return (error);
}

static void
ure_rtl8152_init(struct ure_softc *sc)
{
	uint32_t pwrctrl;

	/* Disable ALDPS. */
	ure_ocp_reg_write(sc, URE_OCP_ALDPS_CONFIG, URE_ENPDNPS | URE_LINKENA |
	    URE_DIS_SDSAVE);
	uether_pause(&sc->sc_ue, hz / 50);

	if (sc->sc_chip & URE_CHIP_VER_4C00) {
		ure_write_2(sc, URE_PLA_LED_FEATURE, URE_MCU_TYPE_PLA,
		    ure_read_2(sc, URE_PLA_LED_FEATURE, URE_MCU_TYPE_PLA) &
		    ~URE_LED_MODE_MASK);
	}

	ure_write_2(sc, URE_USB_UPS_CTRL, URE_MCU_TYPE_USB,
	    ure_read_2(sc, URE_USB_UPS_CTRL, URE_MCU_TYPE_USB) &
	    ~URE_POWER_CUT);
	ure_write_2(sc, URE_USB_PM_CTRL_STATUS, URE_MCU_TYPE_USB,
	    ure_read_2(sc, URE_USB_PM_CTRL_STATUS, URE_MCU_TYPE_USB) &
	    ~URE_RESUME_INDICATE);

	ure_write_2(sc, URE_PLA_PHY_PWR, URE_MCU_TYPE_PLA,
	    ure_read_2(sc, URE_PLA_PHY_PWR, URE_MCU_TYPE_PLA) |
	    URE_TX_10M_IDLE_EN | URE_PFM_PWM_SWITCH);
	pwrctrl = ure_read_4(sc, URE_PLA_MAC_PWR_CTRL, URE_MCU_TYPE_PLA);
	pwrctrl &= ~URE_MCU_CLK_RATIO_MASK;
	pwrctrl |= URE_MCU_CLK_RATIO | URE_D3_CLK_GATED_EN;
	ure_write_4(sc, URE_PLA_MAC_PWR_CTRL, URE_MCU_TYPE_PLA, pwrctrl);
	ure_write_2(sc, URE_PLA_GPHY_INTR_IMR, URE_MCU_TYPE_PLA,
	    URE_GPHY_STS_MSK | URE_SPEED_DOWN_MSK | URE_SPDWN_RXDV_MSK |
	    URE_SPDWN_LINKCHG_MSK);

	/* Disable Rx aggregation. */
	ure_write_2(sc, URE_USB_USB_CTRL, URE_MCU_TYPE_USB,
	    ure_read_2(sc, URE_USB_USB_CTRL, URE_MCU_TYPE_USB) |
	    URE_RX_AGG_DISABLE);

        /* Disable ALDPS. */
	ure_ocp_reg_write(sc, URE_OCP_ALDPS_CONFIG, URE_ENPDNPS | URE_LINKENA |
	    URE_DIS_SDSAVE);
	uether_pause(&sc->sc_ue, hz / 50);

	ure_init_fifo(sc);

	ure_write_1(sc, URE_USB_TX_AGG, URE_MCU_TYPE_USB,
	    URE_TX_AGG_MAX_THRESHOLD);
	ure_write_4(sc, URE_USB_RX_BUF_TH, URE_MCU_TYPE_USB, URE_RX_THR_HIGH);
	ure_write_4(sc, URE_USB_TX_DMA, URE_MCU_TYPE_USB,
	    URE_TEST_MODE_DISABLE | URE_TX_SIZE_ADJUST1);
}

static void
ure_rtl8153_init(struct ure_softc *sc)
{
	uint16_t val;
	uint8_t u1u2[8];
	int i;

	/* Disable ALDPS. */
	ure_ocp_reg_write(sc, URE_OCP_POWER_CFG,
	    ure_ocp_reg_read(sc, URE_OCP_POWER_CFG) & ~URE_EN_ALDPS);
	uether_pause(&sc->sc_ue, hz / 50);

	memset(u1u2, 0x00, sizeof(u1u2));
	ure_write_mem(sc, URE_USB_TOLERANCE,
	    URE_MCU_TYPE_USB | URE_BYTE_EN_SIX_BYTES, u1u2, sizeof(u1u2));

        for (i = 0; i < URE_TIMEOUT; i++) {
		if (ure_read_2(sc, URE_PLA_BOOT_CTRL, URE_MCU_TYPE_PLA) &
		    URE_AUTOLOAD_DONE)
			break;
		uether_pause(&sc->sc_ue, hz / 100);
	}
	if (i == URE_TIMEOUT)
		device_printf(sc->sc_ue.ue_dev,
		    "timeout waiting for chip autoload\n");

        for (i = 0; i < URE_TIMEOUT; i++) {
		val = ure_ocp_reg_read(sc, URE_OCP_PHY_STATUS) &
		    URE_PHY_STAT_MASK;
		if (val == URE_PHY_STAT_LAN_ON || val == URE_PHY_STAT_PWRDN)
			break;
		uether_pause(&sc->sc_ue, hz / 100);
	}
	if (i == URE_TIMEOUT)
		device_printf(sc->sc_ue.ue_dev,
		    "timeout waiting for phy to stabilize\n");
	
	ure_write_2(sc, URE_USB_U2P3_CTRL, URE_MCU_TYPE_USB,
	    ure_read_2(sc, URE_USB_U2P3_CTRL, URE_MCU_TYPE_USB) &
	    ~URE_U2P3_ENABLE);

	if (sc->sc_chip & URE_CHIP_VER_5C10) {
		val = ure_read_2(sc, URE_USB_SSPHYLINK2, URE_MCU_TYPE_USB);
		val &= ~URE_PWD_DN_SCALE_MASK;
		val |= URE_PWD_DN_SCALE(96);
		ure_write_2(sc, URE_USB_SSPHYLINK2, URE_MCU_TYPE_USB, val);

		ure_write_1(sc, URE_USB_USB2PHY, URE_MCU_TYPE_USB,
		    ure_read_1(sc, URE_USB_USB2PHY, URE_MCU_TYPE_USB) |
		    URE_USB2PHY_L1 | URE_USB2PHY_SUSPEND);
	} else if (sc->sc_chip & URE_CHIP_VER_5C20) {
		ure_write_1(sc, URE_PLA_DMY_REG0, URE_MCU_TYPE_PLA,
		    ure_read_1(sc, URE_PLA_DMY_REG0, URE_MCU_TYPE_PLA) &
		    ~URE_ECM_ALDPS);
	}
	if (sc->sc_chip & (URE_CHIP_VER_5C20 | URE_CHIP_VER_5C30)) {
		val = ure_read_1(sc, URE_USB_CSR_DUMMY1, URE_MCU_TYPE_USB);
		if (ure_read_2(sc, URE_USB_BURST_SIZE, URE_MCU_TYPE_USB) ==
		    0)
			val &= ~URE_DYNAMIC_BURST;
		else
			val |= URE_DYNAMIC_BURST;
		ure_write_1(sc, URE_USB_CSR_DUMMY1, URE_MCU_TYPE_USB, val);
	}

	ure_write_1(sc, URE_USB_CSR_DUMMY2, URE_MCU_TYPE_USB,
	    ure_read_1(sc, URE_USB_CSR_DUMMY2, URE_MCU_TYPE_USB) |
	    URE_EP4_FULL_FC);
	
	ure_write_2(sc, URE_USB_WDT11_CTRL, URE_MCU_TYPE_USB,
	    ure_read_2(sc, URE_USB_WDT11_CTRL, URE_MCU_TYPE_USB) &
	    ~URE_TIMER11_EN);

	ure_write_2(sc, URE_PLA_LED_FEATURE, URE_MCU_TYPE_PLA,
	    ure_read_2(sc, URE_PLA_LED_FEATURE, URE_MCU_TYPE_PLA) &
	    ~URE_LED_MODE_MASK);
	    
	if ((sc->sc_chip & URE_CHIP_VER_5C10) &&
	    usbd_get_speed(sc->sc_ue.ue_udev) != USB_SPEED_SUPER)
		val = URE_LPM_TIMER_500MS;
	else
		val = URE_LPM_TIMER_500US;
	ure_write_1(sc, URE_USB_LPM_CTRL, URE_MCU_TYPE_USB,
	    val | URE_FIFO_EMPTY_1FB | URE_ROK_EXIT_LPM);

	val = ure_read_2(sc, URE_USB_AFE_CTRL2, URE_MCU_TYPE_USB);
	val &= ~URE_SEN_VAL_MASK;
	val |= URE_SEN_VAL_NORMAL | URE_SEL_RXIDLE;
	ure_write_2(sc, URE_USB_AFE_CTRL2, URE_MCU_TYPE_USB, val);

	ure_write_2(sc, URE_USB_CONNECT_TIMER, URE_MCU_TYPE_USB, 0x0001);

	ure_write_2(sc, URE_USB_POWER_CUT, URE_MCU_TYPE_USB,
	    ure_read_2(sc, URE_USB_POWER_CUT, URE_MCU_TYPE_USB) &
	    ~(URE_PWR_EN | URE_PHASE2_EN));
	ure_write_2(sc, URE_USB_MISC_0, URE_MCU_TYPE_USB,
	    ure_read_2(sc, URE_USB_MISC_0, URE_MCU_TYPE_USB) &
	    ~URE_PCUT_STATUS);

	memset(u1u2, 0xff, sizeof(u1u2));
	ure_write_mem(sc, URE_USB_TOLERANCE,
	    URE_MCU_TYPE_USB | URE_BYTE_EN_SIX_BYTES, u1u2, sizeof(u1u2));

	ure_write_2(sc, URE_PLA_MAC_PWR_CTRL, URE_MCU_TYPE_PLA,
	    URE_ALDPS_SPDWN_RATIO);
	ure_write_2(sc, URE_PLA_MAC_PWR_CTRL2, URE_MCU_TYPE_PLA,
	    URE_EEE_SPDWN_RATIO);
	ure_write_2(sc, URE_PLA_MAC_PWR_CTRL3, URE_MCU_TYPE_PLA,
	    URE_PKT_AVAIL_SPDWN_EN | URE_SUSPEND_SPDWN_EN |
	    URE_U1U2_SPDWN_EN | URE_L1_SPDWN_EN);
	ure_write_2(sc, URE_PLA_MAC_PWR_CTRL4, URE_MCU_TYPE_PLA,
	    URE_PWRSAVE_SPDWN_EN | URE_RXDV_SPDWN_EN | URE_TX10MIDLE_EN |
	    URE_TP100_SPDWN_EN | URE_TP500_SPDWN_EN | URE_TP1000_SPDWN_EN |
	    URE_EEE_SPDWN_EN);

	val = ure_read_2(sc, URE_USB_U2P3_CTRL, URE_MCU_TYPE_USB);
	if (!(sc->sc_chip & (URE_CHIP_VER_5C00 | URE_CHIP_VER_5C10)))
		val |= URE_U2P3_ENABLE;
	else
		val &= ~URE_U2P3_ENABLE;
	ure_write_2(sc, URE_USB_U2P3_CTRL, URE_MCU_TYPE_USB, val);

	memset(u1u2, 0x00, sizeof(u1u2));
        ure_write_mem(sc, URE_USB_TOLERANCE,
	    URE_MCU_TYPE_USB | URE_BYTE_EN_SIX_BYTES, u1u2, sizeof(u1u2));

	/* Disable ALDPS. */
	ure_ocp_reg_write(sc, URE_OCP_POWER_CFG,
	    ure_ocp_reg_read(sc, URE_OCP_POWER_CFG) & ~URE_EN_ALDPS);
	uether_pause(&sc->sc_ue, hz / 50);

	ure_init_fifo(sc);

	/* Disable Rx aggregation. */
	ure_write_2(sc, URE_USB_USB_CTRL, URE_MCU_TYPE_USB,
	    ure_read_2(sc, URE_USB_USB_CTRL, URE_MCU_TYPE_USB) |
	    URE_RX_AGG_DISABLE);

	val = ure_read_2(sc, URE_USB_U2P3_CTRL, URE_MCU_TYPE_USB);
	if (!(sc->sc_chip & (URE_CHIP_VER_5C00 | URE_CHIP_VER_5C10)))
		val |= URE_U2P3_ENABLE;
	else
		val &= ~URE_U2P3_ENABLE;
	ure_write_2(sc, URE_USB_U2P3_CTRL, URE_MCU_TYPE_USB, val);

	memset(u1u2, 0xff, sizeof(u1u2));
	ure_write_mem(sc, URE_USB_TOLERANCE,
	    URE_MCU_TYPE_USB | URE_BYTE_EN_SIX_BYTES, u1u2, sizeof(u1u2));
}

static void
ure_stop(struct usb_ether *ue)
{
	struct ure_softc *sc = uether_getsc(ue);
	struct ifnet *ifp = uether_getifp(ue);

	URE_LOCK_ASSERT(sc, MA_OWNED);

	ifp->if_drv_flags &= ~(IFF_DRV_RUNNING | IFF_DRV_OACTIVE);
	sc->sc_flags &= ~URE_FLAG_LINK;

	/*
	 * stop all the transfers, if not already stopped:
	 */
	usbd_transfer_stop(sc->sc_xfer[URE_BULK_DT_WR]);
	usbd_transfer_stop(sc->sc_xfer[URE_BULK_DT_RD]);
}

static void
ure_disable_teredo(struct ure_softc *sc)
{

	ure_write_4(sc, URE_PLA_TEREDO_CFG, URE_MCU_TYPE_PLA,
	    ure_read_4(sc, URE_PLA_TEREDO_CFG, URE_MCU_TYPE_PLA) & 
	    ~(URE_TEREDO_SEL | URE_TEREDO_RS_EVENT_MASK | URE_OOB_TEREDO_EN));
	ure_write_2(sc, URE_PLA_WDT6_CTRL, URE_MCU_TYPE_PLA,
	    URE_WDT6_SET_MODE);
	ure_write_2(sc, URE_PLA_REALWOW_TIMER, URE_MCU_TYPE_PLA, 0);
	ure_write_4(sc, URE_PLA_TEREDO_TIMER, URE_MCU_TYPE_PLA, 0);
}

static void
ure_init_fifo(struct ure_softc *sc)
{
	uint32_t rx_fifo1, rx_fifo2;
	int i;

	ure_write_2(sc, URE_PLA_MISC_1, URE_MCU_TYPE_PLA,
	    ure_read_2(sc, URE_PLA_MISC_1, URE_MCU_TYPE_PLA) |
	    URE_RXDY_GATED_EN);

	ure_disable_teredo(sc);

	ure_write_4(sc, URE_PLA_RCR, URE_MCU_TYPE_PLA,
	    ure_read_4(sc, URE_PLA_RCR, URE_MCU_TYPE_PLA) &
	    ~URE_RCR_ACPT_ALL);

	if (!(sc->sc_flags & URE_FLAG_8152)) {
		if (sc->sc_chip & (URE_CHIP_VER_5C00 | URE_CHIP_VER_5C10 |
		    URE_CHIP_VER_5C20)) {
				ure_ocp_reg_write(sc, URE_OCP_ADC_CFG,
				    URE_CKADSEL_L | URE_ADC_EN | URE_EN_EMI_L);
		}
		if (sc->sc_chip & URE_CHIP_VER_5C00) {
			ure_ocp_reg_write(sc, URE_OCP_EEE_CFG,
			    ure_ocp_reg_read(sc, URE_OCP_EEE_CFG) & 
			    ~URE_CTAP_SHORT_EN);
		}
		ure_ocp_reg_write(sc, URE_OCP_POWER_CFG,
		    ure_ocp_reg_read(sc, URE_OCP_POWER_CFG) |
		    URE_EEE_CLKDIV_EN);
		ure_ocp_reg_write(sc, URE_OCP_DOWN_SPEED,
		    ure_ocp_reg_read(sc, URE_OCP_DOWN_SPEED) |
		    URE_EN_10M_BGOFF);
		ure_ocp_reg_write(sc, URE_OCP_POWER_CFG,
		    ure_ocp_reg_read(sc, URE_OCP_POWER_CFG) |
		    URE_EN_10M_PLLOFF);
		ure_ocp_reg_write(sc, URE_OCP_SRAM_ADDR, URE_SRAM_IMPEDANCE);
		ure_ocp_reg_write(sc, URE_OCP_SRAM_DATA, 0x0b13);
		ure_write_2(sc, URE_PLA_PHY_PWR, URE_MCU_TYPE_PLA,
		    ure_read_2(sc, URE_PLA_PHY_PWR, URE_MCU_TYPE_PLA) |
		    URE_PFM_PWM_SWITCH);

		/* Enable LPF corner auto tune. */
		ure_ocp_reg_write(sc, URE_OCP_SRAM_ADDR, URE_SRAM_LPF_CFG);
		ure_ocp_reg_write(sc, URE_OCP_SRAM_DATA, 0xf70f);

		/* Adjust 10M amplitude. */
		ure_ocp_reg_write(sc, URE_OCP_SRAM_ADDR, URE_SRAM_10M_AMP1);
		ure_ocp_reg_write(sc, URE_OCP_SRAM_DATA, 0x00af);
		ure_ocp_reg_write(sc, URE_OCP_SRAM_ADDR, URE_SRAM_10M_AMP2);
		ure_ocp_reg_write(sc, URE_OCP_SRAM_DATA, 0x0208);
	}

	ure_reset(sc);

	ure_write_1(sc, URE_PLA_CR, URE_MCU_TYPE_PLA, 0);

	ure_write_1(sc, URE_PLA_OOB_CTRL, URE_MCU_TYPE_PLA,
	    ure_read_1(sc, URE_PLA_OOB_CTRL, URE_MCU_TYPE_PLA) &
	    ~URE_NOW_IS_OOB);

	ure_write_2(sc, URE_PLA_SFF_STS_7, URE_MCU_TYPE_PLA,
	    ure_read_2(sc, URE_PLA_SFF_STS_7, URE_MCU_TYPE_PLA) &
	    ~URE_MCU_BORW_EN);
	for (i = 0; i < URE_TIMEOUT; i++) {
		if (ure_read_1(sc, URE_PLA_OOB_CTRL, URE_MCU_TYPE_PLA) &
		    URE_LINK_LIST_READY)
			break;
		uether_pause(&sc->sc_ue, hz / 100);
	}
	if (i == URE_TIMEOUT)
		device_printf(sc->sc_ue.ue_dev,
		    "timeout waiting for OOB control\n");
	ure_write_2(sc, URE_PLA_SFF_STS_7, URE_MCU_TYPE_PLA,
	    ure_read_2(sc, URE_PLA_SFF_STS_7, URE_MCU_TYPE_PLA) |
	    URE_RE_INIT_LL);
	for (i = 0; i < URE_TIMEOUT; i++) {
		if (ure_read_1(sc, URE_PLA_OOB_CTRL, URE_MCU_TYPE_PLA) &
		    URE_LINK_LIST_READY)
			break;
		uether_pause(&sc->sc_ue, hz / 100);
	}
	if (i == URE_TIMEOUT)
		device_printf(sc->sc_ue.ue_dev,
		    "timeout waiting for OOB control\n");

	ure_write_2(sc, URE_PLA_CPCR, URE_MCU_TYPE_PLA,
	    ure_read_2(sc, URE_PLA_CPCR, URE_MCU_TYPE_PLA) &
	    ~URE_CPCR_RX_VLAN);
	ure_write_2(sc, URE_PLA_TCR0, URE_MCU_TYPE_PLA,
	    ure_read_2(sc, URE_PLA_TCR0, URE_MCU_TYPE_PLA) |
	    URE_TCR0_AUTO_FIFO);

	/* Configure Rx FIFO threshold. */
	ure_write_4(sc, URE_PLA_RXFIFO_CTRL0, URE_MCU_TYPE_PLA,
	    URE_RXFIFO_THR1_NORMAL);
	if (usbd_get_speed(sc->sc_ue.ue_udev) == USB_SPEED_FULL) {
		rx_fifo1 = URE_RXFIFO_THR2_FULL;
		rx_fifo2 = URE_RXFIFO_THR3_FULL;
	} else {
		rx_fifo1 = URE_RXFIFO_THR2_HIGH;
		rx_fifo2 = URE_RXFIFO_THR3_HIGH;
	}
	ure_write_4(sc, URE_PLA_RXFIFO_CTRL1, URE_MCU_TYPE_PLA, rx_fifo1);
	ure_write_4(sc, URE_PLA_RXFIFO_CTRL2, URE_MCU_TYPE_PLA, rx_fifo2);

	/* Configure Tx FIFO threshold. */
	ure_write_4(sc, URE_PLA_TXFIFO_CTRL, URE_MCU_TYPE_PLA,
	    URE_TXFIFO_THR_NORMAL);
}
