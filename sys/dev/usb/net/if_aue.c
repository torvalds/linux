/*-
 * SPDX-License-Identifier: BSD-4-Clause
 *
 * Copyright (c) 1997, 1998, 1999, 2000
 *	Bill Paul <wpaul@ee.columbia.edu>.  All rights reserved.
 *
 * Copyright (c) 2006
 *      Alfred Perlstein <alfred@FreeBSD.org>. All rights reserved.
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
 * ADMtek AN986 Pegasus and AN8511 Pegasus II USB to ethernet driver.
 * Datasheet is available from http://www.admtek.com.tw.
 *
 * Written by Bill Paul <wpaul@ee.columbia.edu>
 * Electrical Engineering Department
 * Columbia University, New York City
 *
 * SMP locking by Alfred Perlstein <alfred@FreeBSD.org>.
 * RED Inc.
 */

/*
 * The Pegasus chip uses four USB "endpoints" to provide 10/100 ethernet
 * support: the control endpoint for reading/writing registers, burst
 * read endpoint for packet reception, burst write for packet transmission
 * and one for "interrupts." The chip uses the same RX filter scheme
 * as the other ADMtek ethernet parts: one perfect filter entry for the
 * the station address and a 64-bit multicast hash table. The chip supports
 * both MII and HomePNA attachments.
 *
 * Since the maximum data transfer speed of USB is supposed to be 12Mbps,
 * you're never really going to get 100Mbps speeds from this device. I
 * think the idea is to allow the device to connect to 10 or 100Mbps
 * networks, not necessarily to provide 100Mbps performance. Also, since
 * the controller uses an external PHY chip, it's possible that board
 * designers might simply choose a 10Mbps PHY.
 *
 * Registers are accessed using uether_do_request(). Packet
 * transfers are done using usbd_transfer() and friends.
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

#define	USB_DEBUG_VAR aue_debug
#include <dev/usb/usb_debug.h>
#include <dev/usb/usb_process.h>

#include <dev/usb/net/usb_ethernet.h>
#include <dev/usb/net/if_auereg.h>

#ifdef USB_DEBUG
static int aue_debug = 0;

static SYSCTL_NODE(_hw_usb, OID_AUTO, aue, CTLFLAG_RW, 0, "USB aue");
SYSCTL_INT(_hw_usb_aue, OID_AUTO, debug, CTLFLAG_RWTUN, &aue_debug, 0,
    "Debug level");
#endif

/*
 * Various supported device vendors/products.
 */
static const STRUCT_USB_HOST_ID aue_devs[] = {
#define	AUE_DEV(v,p,i) { USB_VPI(USB_VENDOR_##v, USB_PRODUCT_##v##_##p, i) }
    AUE_DEV(3COM, 3C460B, AUE_FLAG_PII),
    AUE_DEV(ABOCOM, DSB650TX_PNA, 0),
    AUE_DEV(ABOCOM, UFE1000, AUE_FLAG_LSYS),
    AUE_DEV(ABOCOM, XX10, 0),
    AUE_DEV(ABOCOM, XX1, AUE_FLAG_PNA | AUE_FLAG_PII),
    AUE_DEV(ABOCOM, XX2, AUE_FLAG_PII),
    AUE_DEV(ABOCOM, XX4, AUE_FLAG_PNA),
    AUE_DEV(ABOCOM, XX5, AUE_FLAG_PNA),
    AUE_DEV(ABOCOM, XX6, AUE_FLAG_PII),
    AUE_DEV(ABOCOM, XX7, AUE_FLAG_PII),
    AUE_DEV(ABOCOM, XX8, AUE_FLAG_PII),
    AUE_DEV(ABOCOM, XX9, AUE_FLAG_PNA),
    AUE_DEV(ACCTON, SS1001, AUE_FLAG_PII),
    AUE_DEV(ACCTON, USB320_EC, 0),
    AUE_DEV(ADMTEK, PEGASUSII_2, AUE_FLAG_PII),
    AUE_DEV(ADMTEK, PEGASUSII_3, AUE_FLAG_PII),
    AUE_DEV(ADMTEK, PEGASUSII_4, AUE_FLAG_PII),
    AUE_DEV(ADMTEK, PEGASUSII, AUE_FLAG_PII),
    AUE_DEV(ADMTEK, PEGASUS, AUE_FLAG_PNA | AUE_FLAG_DUAL_PHY),
    AUE_DEV(AEI, FASTETHERNET, AUE_FLAG_PII),
    AUE_DEV(ALLIEDTELESYN, ATUSB100, AUE_FLAG_PII),
    AUE_DEV(ATEN, UC110T, AUE_FLAG_PII),
    AUE_DEV(BELKIN, USB2LAN, AUE_FLAG_PII),
    AUE_DEV(BILLIONTON, USB100, 0),
    AUE_DEV(BILLIONTON, USBE100, AUE_FLAG_PII),
    AUE_DEV(BILLIONTON, USBEL100, 0),
    AUE_DEV(BILLIONTON, USBLP100, AUE_FLAG_PNA),
    AUE_DEV(COREGA, FETHER_USB_TXS, AUE_FLAG_PII),
    AUE_DEV(COREGA, FETHER_USB_TX, 0),
    AUE_DEV(DLINK, DSB650TX1, AUE_FLAG_LSYS),
    AUE_DEV(DLINK, DSB650TX2, AUE_FLAG_LSYS | AUE_FLAG_PII),
    AUE_DEV(DLINK, DSB650TX3, AUE_FLAG_LSYS | AUE_FLAG_PII),
    AUE_DEV(DLINK, DSB650TX4, AUE_FLAG_LSYS | AUE_FLAG_PII),
    AUE_DEV(DLINK, DSB650TX_PNA, AUE_FLAG_PNA),
    AUE_DEV(DLINK, DSB650TX, AUE_FLAG_LSYS),
    AUE_DEV(DLINK, DSB650, AUE_FLAG_LSYS),
    AUE_DEV(ELCON, PLAN, AUE_FLAG_PNA | AUE_FLAG_PII),
    AUE_DEV(ELECOM, LDUSB20, AUE_FLAG_PII),
    AUE_DEV(ELECOM, LDUSBLTX, AUE_FLAG_PII),
    AUE_DEV(ELECOM, LDUSBTX0, 0),
    AUE_DEV(ELECOM, LDUSBTX1, AUE_FLAG_LSYS),
    AUE_DEV(ELECOM, LDUSBTX2, 0),
    AUE_DEV(ELECOM, LDUSBTX3, AUE_FLAG_LSYS),
    AUE_DEV(ELSA, USB2ETHERNET, 0),
    AUE_DEV(GIGABYTE, GNBR402W, 0),
    AUE_DEV(HAWKING, UF100, AUE_FLAG_PII),
    AUE_DEV(HP, HN210E, AUE_FLAG_PII),
    AUE_DEV(IODATA, USBETTXS, AUE_FLAG_PII),
    AUE_DEV(IODATA, USBETTX, 0),
    AUE_DEV(KINGSTON, KNU101TX, 0),
    AUE_DEV(LINKSYS, USB100H1, AUE_FLAG_LSYS | AUE_FLAG_PNA),
    AUE_DEV(LINKSYS, USB100TX, AUE_FLAG_LSYS),
    AUE_DEV(LINKSYS, USB10TA, AUE_FLAG_LSYS),
    AUE_DEV(LINKSYS, USB10TX1, AUE_FLAG_LSYS | AUE_FLAG_PII),
    AUE_DEV(LINKSYS, USB10TX2, AUE_FLAG_LSYS | AUE_FLAG_PII),
    AUE_DEV(LINKSYS, USB10T, AUE_FLAG_LSYS),
    AUE_DEV(MELCO, LUA2TX5, AUE_FLAG_PII),
    AUE_DEV(MELCO, LUATX1, 0),
    AUE_DEV(MELCO, LUATX5, 0),
    AUE_DEV(MICROSOFT, MN110, AUE_FLAG_PII),
    AUE_DEV(NETGEAR, FA101, AUE_FLAG_PII),
    AUE_DEV(SIEMENS, SPEEDSTREAM, AUE_FLAG_PII),
    AUE_DEV(SIIG2, USBTOETHER, AUE_FLAG_PII),
    AUE_DEV(SMARTBRIDGES, SMARTNIC, AUE_FLAG_PII),
    AUE_DEV(SMC, 2202USB, 0),
    AUE_DEV(SMC, 2206USB, AUE_FLAG_PII),
    AUE_DEV(SOHOWARE, NUB100, 0),
    AUE_DEV(SOHOWARE, NUB110, AUE_FLAG_PII),
#undef AUE_DEV
};

/* prototypes */

static device_probe_t aue_probe;
static device_attach_t aue_attach;
static device_detach_t aue_detach;
static miibus_readreg_t aue_miibus_readreg;
static miibus_writereg_t aue_miibus_writereg;
static miibus_statchg_t aue_miibus_statchg;

static usb_callback_t aue_intr_callback;
static usb_callback_t aue_bulk_read_callback;
static usb_callback_t aue_bulk_write_callback;

static uether_fn_t aue_attach_post;
static uether_fn_t aue_init;
static uether_fn_t aue_stop;
static uether_fn_t aue_start;
static uether_fn_t aue_tick;
static uether_fn_t aue_setmulti;
static uether_fn_t aue_setpromisc;

static uint8_t	aue_csr_read_1(struct aue_softc *, uint16_t);
static uint16_t	aue_csr_read_2(struct aue_softc *, uint16_t);
static void	aue_csr_write_1(struct aue_softc *, uint16_t, uint8_t);
static void	aue_csr_write_2(struct aue_softc *, uint16_t, uint16_t);
static uint16_t	aue_eeprom_getword(struct aue_softc *, int);
static void	aue_reset(struct aue_softc *);
static void	aue_reset_pegasus_II(struct aue_softc *);

static int	aue_ifmedia_upd(struct ifnet *);
static void	aue_ifmedia_sts(struct ifnet *, struct ifmediareq *);

static const struct usb_config aue_config[AUE_N_TRANSFER] = {

	[AUE_BULK_DT_WR] = {
		.type = UE_BULK,
		.endpoint = UE_ADDR_ANY,
		.direction = UE_DIR_OUT,
		.bufsize = (MCLBYTES + 2),
		.flags = {.pipe_bof = 1,.force_short_xfer = 1,},
		.callback = aue_bulk_write_callback,
		.timeout = 10000,	/* 10 seconds */
	},

	[AUE_BULK_DT_RD] = {
		.type = UE_BULK,
		.endpoint = UE_ADDR_ANY,
		.direction = UE_DIR_IN,
		.bufsize = (MCLBYTES + 4 + ETHER_CRC_LEN),
		.flags = {.pipe_bof = 1,.short_xfer_ok = 1,},
		.callback = aue_bulk_read_callback,
	},

	[AUE_INTR_DT_RD] = {
		.type = UE_INTERRUPT,
		.endpoint = UE_ADDR_ANY,
		.direction = UE_DIR_IN,
		.flags = {.pipe_bof = 1,.short_xfer_ok = 1,},
		.bufsize = 0,	/* use wMaxPacketSize */
		.callback = aue_intr_callback,
	},
};

static device_method_t aue_methods[] = {
	/* Device interface */
	DEVMETHOD(device_probe, aue_probe),
	DEVMETHOD(device_attach, aue_attach),
	DEVMETHOD(device_detach, aue_detach),

	/* MII interface */
	DEVMETHOD(miibus_readreg, aue_miibus_readreg),
	DEVMETHOD(miibus_writereg, aue_miibus_writereg),
	DEVMETHOD(miibus_statchg, aue_miibus_statchg),

	DEVMETHOD_END
};

static driver_t aue_driver = {
	.name = "aue",
	.methods = aue_methods,
	.size = sizeof(struct aue_softc)
};

static devclass_t aue_devclass;

DRIVER_MODULE(aue, uhub, aue_driver, aue_devclass, NULL, 0);
DRIVER_MODULE(miibus, aue, miibus_driver, miibus_devclass, 0, 0);
MODULE_DEPEND(aue, uether, 1, 1, 1);
MODULE_DEPEND(aue, usb, 1, 1, 1);
MODULE_DEPEND(aue, ether, 1, 1, 1);
MODULE_DEPEND(aue, miibus, 1, 1, 1);
MODULE_VERSION(aue, 1);
USB_PNP_HOST_INFO(aue_devs);

static const struct usb_ether_methods aue_ue_methods = {
	.ue_attach_post = aue_attach_post,
	.ue_start = aue_start,
	.ue_init = aue_init,
	.ue_stop = aue_stop,
	.ue_tick = aue_tick,
	.ue_setmulti = aue_setmulti,
	.ue_setpromisc = aue_setpromisc,
	.ue_mii_upd = aue_ifmedia_upd,
	.ue_mii_sts = aue_ifmedia_sts,
};

#define	AUE_SETBIT(sc, reg, x) \
	aue_csr_write_1(sc, reg, aue_csr_read_1(sc, reg) | (x))

#define	AUE_CLRBIT(sc, reg, x) \
	aue_csr_write_1(sc, reg, aue_csr_read_1(sc, reg) & ~(x))

static uint8_t
aue_csr_read_1(struct aue_softc *sc, uint16_t reg)
{
	struct usb_device_request req;
	usb_error_t err;
	uint8_t val;

	req.bmRequestType = UT_READ_VENDOR_DEVICE;
	req.bRequest = AUE_UR_READREG;
	USETW(req.wValue, 0);
	USETW(req.wIndex, reg);
	USETW(req.wLength, 1);

	err = uether_do_request(&sc->sc_ue, &req, &val, 1000);
	if (err)
		return (0);
	return (val);
}

static uint16_t
aue_csr_read_2(struct aue_softc *sc, uint16_t reg)
{
	struct usb_device_request req;
	usb_error_t err;
	uint16_t val;

	req.bmRequestType = UT_READ_VENDOR_DEVICE;
	req.bRequest = AUE_UR_READREG;
	USETW(req.wValue, 0);
	USETW(req.wIndex, reg);
	USETW(req.wLength, 2);

	err = uether_do_request(&sc->sc_ue, &req, &val, 1000);
	if (err)
		return (0);
	return (le16toh(val));
}

static void
aue_csr_write_1(struct aue_softc *sc, uint16_t reg, uint8_t val)
{
	struct usb_device_request req;

	req.bmRequestType = UT_WRITE_VENDOR_DEVICE;
	req.bRequest = AUE_UR_WRITEREG;
	req.wValue[0] = val;
	req.wValue[1] = 0;
	USETW(req.wIndex, reg);
	USETW(req.wLength, 1);

	if (uether_do_request(&sc->sc_ue, &req, &val, 1000)) {
		/* error ignored */
	}
}

static void
aue_csr_write_2(struct aue_softc *sc, uint16_t reg, uint16_t val)
{
	struct usb_device_request req;

	req.bmRequestType = UT_WRITE_VENDOR_DEVICE;
	req.bRequest = AUE_UR_WRITEREG;
	USETW(req.wValue, val);
	USETW(req.wIndex, reg);
	USETW(req.wLength, 2);

	val = htole16(val);

	if (uether_do_request(&sc->sc_ue, &req, &val, 1000)) {
		/* error ignored */
	}
}

/*
 * Read a word of data stored in the EEPROM at address 'addr.'
 */
static uint16_t
aue_eeprom_getword(struct aue_softc *sc, int addr)
{
	int i;

	aue_csr_write_1(sc, AUE_EE_REG, addr);
	aue_csr_write_1(sc, AUE_EE_CTL, AUE_EECTL_READ);

	for (i = 0; i != AUE_TIMEOUT; i++) {
		if (aue_csr_read_1(sc, AUE_EE_CTL) & AUE_EECTL_DONE)
			break;
		if (uether_pause(&sc->sc_ue, hz / 100))
			break;
	}

	if (i == AUE_TIMEOUT)
		device_printf(sc->sc_ue.ue_dev, "EEPROM read timed out\n");

	return (aue_csr_read_2(sc, AUE_EE_DATA));
}

/*
 * Read station address(offset 0) from the EEPROM.
 */
static void
aue_read_mac(struct aue_softc *sc, uint8_t *eaddr)
{
	int i, offset;
	uint16_t word;

	for (i = 0, offset = 0; i < ETHER_ADDR_LEN / 2; i++) {
		word = aue_eeprom_getword(sc, offset + i);
		eaddr[i * 2] = (uint8_t)word;
		eaddr[i * 2 + 1] = (uint8_t)(word >> 8);
	}
}

static int
aue_miibus_readreg(device_t dev, int phy, int reg)
{
	struct aue_softc *sc = device_get_softc(dev);
	int i, locked;
	uint16_t val = 0;

	locked = mtx_owned(&sc->sc_mtx);
	if (!locked)
		AUE_LOCK(sc);

	/*
	 * The Am79C901 HomePNA PHY actually contains two transceivers: a 1Mbps
	 * HomePNA PHY and a 10Mbps full/half duplex ethernet PHY with NWAY
	 * autoneg. However in the ADMtek adapter, only the 1Mbps PHY is
	 * actually connected to anything, so we ignore the 10Mbps one. It
	 * happens to be configured for MII address 3, so we filter that out.
	 */
	if (sc->sc_flags & AUE_FLAG_DUAL_PHY) {
		if (phy == 3)
			goto done;
#if 0
		if (phy != 1)
			goto done;
#endif
	}
	aue_csr_write_1(sc, AUE_PHY_ADDR, phy);
	aue_csr_write_1(sc, AUE_PHY_CTL, reg | AUE_PHYCTL_READ);

	for (i = 0; i != AUE_TIMEOUT; i++) {
		if (aue_csr_read_1(sc, AUE_PHY_CTL) & AUE_PHYCTL_DONE)
			break;
		if (uether_pause(&sc->sc_ue, hz / 100))
			break;
	}

	if (i == AUE_TIMEOUT)
		device_printf(sc->sc_ue.ue_dev, "MII read timed out\n");

	val = aue_csr_read_2(sc, AUE_PHY_DATA);

done:
	if (!locked)
		AUE_UNLOCK(sc);
	return (val);
}

static int
aue_miibus_writereg(device_t dev, int phy, int reg, int data)
{
	struct aue_softc *sc = device_get_softc(dev);
	int i;
	int locked;

	if (phy == 3)
		return (0);

	locked = mtx_owned(&sc->sc_mtx);
	if (!locked)
		AUE_LOCK(sc);

	aue_csr_write_2(sc, AUE_PHY_DATA, data);
	aue_csr_write_1(sc, AUE_PHY_ADDR, phy);
	aue_csr_write_1(sc, AUE_PHY_CTL, reg | AUE_PHYCTL_WRITE);

	for (i = 0; i != AUE_TIMEOUT; i++) {
		if (aue_csr_read_1(sc, AUE_PHY_CTL) & AUE_PHYCTL_DONE)
			break;
		if (uether_pause(&sc->sc_ue, hz / 100))
			break;
	}

	if (i == AUE_TIMEOUT)
		device_printf(sc->sc_ue.ue_dev, "MII write timed out\n");

	if (!locked)
		AUE_UNLOCK(sc);
	return (0);
}

static void
aue_miibus_statchg(device_t dev)
{
	struct aue_softc *sc = device_get_softc(dev);
	struct mii_data *mii = GET_MII(sc);
	int locked;

	locked = mtx_owned(&sc->sc_mtx);
	if (!locked)
		AUE_LOCK(sc);

	AUE_CLRBIT(sc, AUE_CTL0, AUE_CTL0_RX_ENB | AUE_CTL0_TX_ENB);
	if (IFM_SUBTYPE(mii->mii_media_active) == IFM_100_TX)
		AUE_SETBIT(sc, AUE_CTL1, AUE_CTL1_SPEEDSEL);
	else
		AUE_CLRBIT(sc, AUE_CTL1, AUE_CTL1_SPEEDSEL);

	if ((mii->mii_media_active & IFM_GMASK) == IFM_FDX)
		AUE_SETBIT(sc, AUE_CTL1, AUE_CTL1_DUPLEX);
	else
		AUE_CLRBIT(sc, AUE_CTL1, AUE_CTL1_DUPLEX);

	AUE_SETBIT(sc, AUE_CTL0, AUE_CTL0_RX_ENB | AUE_CTL0_TX_ENB);

	/*
	 * Set the LED modes on the LinkSys adapter.
	 * This turns on the 'dual link LED' bin in the auxmode
	 * register of the Broadcom PHY.
	 */
	if (sc->sc_flags & AUE_FLAG_LSYS) {
		uint16_t auxmode;

		auxmode = aue_miibus_readreg(dev, 0, 0x1b);
		aue_miibus_writereg(dev, 0, 0x1b, auxmode | 0x04);
	}
	if (!locked)
		AUE_UNLOCK(sc);
}

#define	AUE_BITS	6
static void
aue_setmulti(struct usb_ether *ue)
{
	struct aue_softc *sc = uether_getsc(ue);
	struct ifnet *ifp = uether_getifp(ue);
	struct ifmultiaddr *ifma;
	uint32_t h = 0;
	uint32_t i;
	uint8_t hashtbl[8] = { 0, 0, 0, 0, 0, 0, 0, 0 };

	AUE_LOCK_ASSERT(sc, MA_OWNED);

	if (ifp->if_flags & IFF_ALLMULTI || ifp->if_flags & IFF_PROMISC) {
		AUE_SETBIT(sc, AUE_CTL0, AUE_CTL0_ALLMULTI);
		return;
	}

	AUE_CLRBIT(sc, AUE_CTL0, AUE_CTL0_ALLMULTI);

	/* now program new ones */
	if_maddr_rlock(ifp);
	CK_STAILQ_FOREACH(ifma, &ifp->if_multiaddrs, ifma_link) {
		if (ifma->ifma_addr->sa_family != AF_LINK)
			continue;
		h = ether_crc32_le(LLADDR((struct sockaddr_dl *)
		    ifma->ifma_addr), ETHER_ADDR_LEN) & ((1 << AUE_BITS) - 1);
		hashtbl[(h >> 3)] |=  1 << (h & 0x7);
	}
	if_maddr_runlock(ifp);

	/* write the hashtable */
	for (i = 0; i != 8; i++)
		aue_csr_write_1(sc, AUE_MAR0 + i, hashtbl[i]);
}

static void
aue_reset_pegasus_II(struct aue_softc *sc)
{
	/* Magic constants taken from Linux driver. */
	aue_csr_write_1(sc, AUE_REG_1D, 0);
	aue_csr_write_1(sc, AUE_REG_7B, 2);
#if 0
	if ((sc->sc_flags & HAS_HOME_PNA) && mii_mode)
		aue_csr_write_1(sc, AUE_REG_81, 6);
	else
#endif
		aue_csr_write_1(sc, AUE_REG_81, 2);
}

static void
aue_reset(struct aue_softc *sc)
{
	int i;

	AUE_SETBIT(sc, AUE_CTL1, AUE_CTL1_RESETMAC);

	for (i = 0; i != AUE_TIMEOUT; i++) {
		if (!(aue_csr_read_1(sc, AUE_CTL1) & AUE_CTL1_RESETMAC))
			break;
		if (uether_pause(&sc->sc_ue, hz / 100))
			break;
	}

	if (i == AUE_TIMEOUT)
		device_printf(sc->sc_ue.ue_dev, "reset failed\n");

	/*
	 * The PHY(s) attached to the Pegasus chip may be held
	 * in reset until we flip on the GPIO outputs. Make sure
	 * to set the GPIO pins high so that the PHY(s) will
	 * be enabled.
	 *
	 * NOTE: We used to force all of the GPIO pins low first and then
	 * enable the ones we want. This has been changed to better
	 * match the ADMtek's reference design to avoid setting the
	 * power-down configuration line of the PHY at the same time
	 * it is reset.
	 */
	aue_csr_write_1(sc, AUE_GPIO0, AUE_GPIO_SEL0|AUE_GPIO_SEL1);
	aue_csr_write_1(sc, AUE_GPIO0, AUE_GPIO_SEL0|AUE_GPIO_SEL1|AUE_GPIO_OUT0);

	if (sc->sc_flags & AUE_FLAG_LSYS) {
		/* Grrr. LinkSys has to be different from everyone else. */
		aue_csr_write_1(sc, AUE_GPIO0, AUE_GPIO_SEL0|AUE_GPIO_SEL1);
		aue_csr_write_1(sc, AUE_GPIO0,
		    AUE_GPIO_SEL0|AUE_GPIO_SEL1|AUE_GPIO_OUT0);
	}
	if (sc->sc_flags & AUE_FLAG_PII)
		aue_reset_pegasus_II(sc);

	/* Wait a little while for the chip to get its brains in order: */
	uether_pause(&sc->sc_ue, hz / 100);
}

static void
aue_attach_post(struct usb_ether *ue)
{
	struct aue_softc *sc = uether_getsc(ue);

	/* reset the adapter */
	aue_reset(sc);

	/* get station address from the EEPROM */
	aue_read_mac(sc, ue->ue_eaddr);
}

/*
 * Probe for a Pegasus chip.
 */
static int
aue_probe(device_t dev)
{
	struct usb_attach_arg *uaa = device_get_ivars(dev);

	if (uaa->usb_mode != USB_MODE_HOST)
		return (ENXIO);
	if (uaa->info.bConfigIndex != AUE_CONFIG_INDEX)
		return (ENXIO);
	if (uaa->info.bIfaceIndex != AUE_IFACE_IDX)
		return (ENXIO);
	/*
	 * Belkin USB Bluetooth dongles of the F8T012xx1 model series conflict
	 * with older Belkin USB2LAN adapters.  Skip if_aue if we detect one of
	 * the devices that look like Bluetooth adapters.
	 */
	if (uaa->info.idVendor == USB_VENDOR_BELKIN &&
	    uaa->info.idProduct == USB_PRODUCT_BELKIN_F8T012 &&
	    uaa->info.bcdDevice == 0x0413)
		return (ENXIO);

	return (usbd_lookup_id_by_uaa(aue_devs, sizeof(aue_devs), uaa));
}

/*
 * Attach the interface. Allocate softc structures, do ifmedia
 * setup and ethernet/BPF attach.
 */
static int
aue_attach(device_t dev)
{
	struct usb_attach_arg *uaa = device_get_ivars(dev);
	struct aue_softc *sc = device_get_softc(dev);
	struct usb_ether *ue = &sc->sc_ue;
	uint8_t iface_index;
	int error;

	sc->sc_flags = USB_GET_DRIVER_INFO(uaa);

	if (uaa->info.bcdDevice >= 0x0201) {
		/* XXX currently undocumented */
		sc->sc_flags |= AUE_FLAG_VER_2;
	}

	device_set_usb_desc(dev);
	mtx_init(&sc->sc_mtx, device_get_nameunit(dev), NULL, MTX_DEF);

	iface_index = AUE_IFACE_IDX;
	error = usbd_transfer_setup(uaa->device, &iface_index,
	    sc->sc_xfer, aue_config, AUE_N_TRANSFER,
	    sc, &sc->sc_mtx);
	if (error) {
		device_printf(dev, "allocating USB transfers failed\n");
		goto detach;
	}

	ue->ue_sc = sc;
	ue->ue_dev = dev;
	ue->ue_udev = uaa->device;
	ue->ue_mtx = &sc->sc_mtx;
	ue->ue_methods = &aue_ue_methods;

	error = uether_ifattach(ue);
	if (error) {
		device_printf(dev, "could not attach interface\n");
		goto detach;
	}
	return (0);			/* success */

detach:
	aue_detach(dev);
	return (ENXIO);			/* failure */
}

static int
aue_detach(device_t dev)
{
	struct aue_softc *sc = device_get_softc(dev);
	struct usb_ether *ue = &sc->sc_ue;

	usbd_transfer_unsetup(sc->sc_xfer, AUE_N_TRANSFER);
	uether_ifdetach(ue);
	mtx_destroy(&sc->sc_mtx);

	return (0);
}

static void
aue_intr_callback(struct usb_xfer *xfer, usb_error_t error)
{
	struct aue_softc *sc = usbd_xfer_softc(xfer);
	struct ifnet *ifp = uether_getifp(&sc->sc_ue);
	struct aue_intrpkt pkt;
	struct usb_page_cache *pc;
	int actlen;

	usbd_xfer_status(xfer, &actlen, NULL, NULL, NULL);

	switch (USB_GET_STATE(xfer)) {
	case USB_ST_TRANSFERRED:

		if ((ifp->if_drv_flags & IFF_DRV_RUNNING) &&
		    actlen >= (int)sizeof(pkt)) {

			pc = usbd_xfer_get_frame(xfer, 0);
			usbd_copy_out(pc, 0, &pkt, sizeof(pkt));

			if (pkt.aue_txstat0)
				if_inc_counter(ifp, IFCOUNTER_OERRORS, 1);
			if (pkt.aue_txstat0 & (AUE_TXSTAT0_LATECOLL |
			    AUE_TXSTAT0_EXCESSCOLL))
				if_inc_counter(ifp, IFCOUNTER_COLLISIONS, 1);
		}
		/* FALLTHROUGH */
	case USB_ST_SETUP:
tr_setup:
		usbd_xfer_set_frame_len(xfer, 0, usbd_xfer_max_len(xfer));
		usbd_transfer_submit(xfer);
		return;

	default:			/* Error */
		if (error != USB_ERR_CANCELLED) {
			/* try to clear stall first */
			usbd_xfer_set_stall(xfer);
			goto tr_setup;
		}
		return;
	}
}

static void
aue_bulk_read_callback(struct usb_xfer *xfer, usb_error_t error)
{
	struct aue_softc *sc = usbd_xfer_softc(xfer);
	struct usb_ether *ue = &sc->sc_ue;
	struct ifnet *ifp = uether_getifp(ue);
	struct aue_rxpkt stat;
	struct usb_page_cache *pc;
	int actlen;

	usbd_xfer_status(xfer, &actlen, NULL, NULL, NULL);
	pc = usbd_xfer_get_frame(xfer, 0);

	switch (USB_GET_STATE(xfer)) {
	case USB_ST_TRANSFERRED:
		DPRINTFN(11, "received %d bytes\n", actlen);

		if (sc->sc_flags & AUE_FLAG_VER_2) {

			if (actlen == 0) {
				if_inc_counter(ifp, IFCOUNTER_IERRORS, 1);
				goto tr_setup;
			}
		} else {

			if (actlen <= (int)(sizeof(stat) + ETHER_CRC_LEN)) {
				if_inc_counter(ifp, IFCOUNTER_IERRORS, 1);
				goto tr_setup;
			}
			usbd_copy_out(pc, actlen - sizeof(stat), &stat,
			    sizeof(stat));

			/*
			 * turn off all the non-error bits in the rx status
			 * word:
			 */
			stat.aue_rxstat &= AUE_RXSTAT_MASK;
			if (stat.aue_rxstat) {
				if_inc_counter(ifp, IFCOUNTER_IERRORS, 1);
				goto tr_setup;
			}
			/* No errors; receive the packet. */
			actlen -= (sizeof(stat) + ETHER_CRC_LEN);
		}
		uether_rxbuf(ue, pc, 0, actlen);

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
aue_bulk_write_callback(struct usb_xfer *xfer, usb_error_t error)
{
	struct aue_softc *sc = usbd_xfer_softc(xfer);
	struct ifnet *ifp = uether_getifp(&sc->sc_ue);
	struct usb_page_cache *pc;
	struct mbuf *m;
	uint8_t buf[2];
	int actlen;

	usbd_xfer_status(xfer, &actlen, NULL, NULL, NULL);
	pc = usbd_xfer_get_frame(xfer, 0);

	switch (USB_GET_STATE(xfer)) {
	case USB_ST_TRANSFERRED:
		DPRINTFN(11, "transfer of %d bytes complete\n", actlen);
		if_inc_counter(ifp, IFCOUNTER_OPACKETS, 1);

		/* FALLTHROUGH */
	case USB_ST_SETUP:
tr_setup:
		if ((sc->sc_flags & AUE_FLAG_LINK) == 0) {
			/*
			 * don't send anything if there is no link !
			 */
			return;
		}
		IFQ_DRV_DEQUEUE(&ifp->if_snd, m);

		if (m == NULL)
			return;
		if (m->m_pkthdr.len > MCLBYTES)
			m->m_pkthdr.len = MCLBYTES;
		if (sc->sc_flags & AUE_FLAG_VER_2) {

			usbd_xfer_set_frame_len(xfer, 0, m->m_pkthdr.len);

			usbd_m_copy_in(pc, 0, m, 0, m->m_pkthdr.len);

		} else {

			usbd_xfer_set_frame_len(xfer, 0, (m->m_pkthdr.len + 2));

			/*
		         * The ADMtek documentation says that the
		         * packet length is supposed to be specified
		         * in the first two bytes of the transfer,
		         * however it actually seems to ignore this
		         * info and base the frame size on the bulk
		         * transfer length.
		         */
			buf[0] = (uint8_t)(m->m_pkthdr.len);
			buf[1] = (uint8_t)(m->m_pkthdr.len >> 8);

			usbd_copy_in(pc, 0, buf, 2);
			usbd_m_copy_in(pc, 2, m, 0, m->m_pkthdr.len);
		}

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
aue_tick(struct usb_ether *ue)
{
	struct aue_softc *sc = uether_getsc(ue);
	struct mii_data *mii = GET_MII(sc);

	AUE_LOCK_ASSERT(sc, MA_OWNED);

	mii_tick(mii);
	if ((sc->sc_flags & AUE_FLAG_LINK) == 0
	    && mii->mii_media_status & IFM_ACTIVE &&
	    IFM_SUBTYPE(mii->mii_media_active) != IFM_NONE) {
		sc->sc_flags |= AUE_FLAG_LINK;
		aue_start(ue);
	}
}

static void
aue_start(struct usb_ether *ue)
{
	struct aue_softc *sc = uether_getsc(ue);

	/*
	 * start the USB transfers, if not already started:
	 */
	usbd_transfer_start(sc->sc_xfer[AUE_INTR_DT_RD]);
	usbd_transfer_start(sc->sc_xfer[AUE_BULK_DT_RD]);
	usbd_transfer_start(sc->sc_xfer[AUE_BULK_DT_WR]);
}

static void
aue_init(struct usb_ether *ue)
{
	struct aue_softc *sc = uether_getsc(ue);
	struct ifnet *ifp = uether_getifp(ue);
	int i;

	AUE_LOCK_ASSERT(sc, MA_OWNED);

	/*
	 * Cancel pending I/O
	 */
	aue_reset(sc);

	/* Set MAC address */
	for (i = 0; i != ETHER_ADDR_LEN; i++)
		aue_csr_write_1(sc, AUE_PAR0 + i, IF_LLADDR(ifp)[i]);

	/* update promiscuous setting */
	aue_setpromisc(ue);

	/* Load the multicast filter. */
	aue_setmulti(ue);

	/* Enable RX and TX */
	aue_csr_write_1(sc, AUE_CTL0, AUE_CTL0_RXSTAT_APPEND | AUE_CTL0_RX_ENB);
	AUE_SETBIT(sc, AUE_CTL0, AUE_CTL0_TX_ENB);
	AUE_SETBIT(sc, AUE_CTL2, AUE_CTL2_EP3_CLR);

	usbd_xfer_set_stall(sc->sc_xfer[AUE_BULK_DT_WR]);

	ifp->if_drv_flags |= IFF_DRV_RUNNING;
	aue_start(ue);
}

static void
aue_setpromisc(struct usb_ether *ue)
{
	struct aue_softc *sc = uether_getsc(ue);
	struct ifnet *ifp = uether_getifp(ue);

	AUE_LOCK_ASSERT(sc, MA_OWNED);

	/* if we want promiscuous mode, set the allframes bit: */
	if (ifp->if_flags & IFF_PROMISC)
		AUE_SETBIT(sc, AUE_CTL2, AUE_CTL2_RX_PROMISC);
	else
		AUE_CLRBIT(sc, AUE_CTL2, AUE_CTL2_RX_PROMISC);
}

/*
 * Set media options.
 */
static int
aue_ifmedia_upd(struct ifnet *ifp)
{
	struct aue_softc *sc = ifp->if_softc;
	struct mii_data *mii = GET_MII(sc);
	struct mii_softc *miisc;
	int error;

	AUE_LOCK_ASSERT(sc, MA_OWNED);

        sc->sc_flags &= ~AUE_FLAG_LINK;
	LIST_FOREACH(miisc, &mii->mii_phys, mii_list)
		PHY_RESET(miisc);
	error = mii_mediachg(mii);
	return (error);
}

/*
 * Report current media status.
 */
static void
aue_ifmedia_sts(struct ifnet *ifp, struct ifmediareq *ifmr)
{
	struct aue_softc *sc = ifp->if_softc;
	struct mii_data *mii = GET_MII(sc);

	AUE_LOCK(sc);
	mii_pollstat(mii);
	ifmr->ifm_active = mii->mii_media_active;
	ifmr->ifm_status = mii->mii_media_status;
	AUE_UNLOCK(sc);
}

/*
 * Stop the adapter and free any mbufs allocated to the
 * RX and TX lists.
 */
static void
aue_stop(struct usb_ether *ue)
{
	struct aue_softc *sc = uether_getsc(ue);
	struct ifnet *ifp = uether_getifp(ue);

	AUE_LOCK_ASSERT(sc, MA_OWNED);

	ifp->if_drv_flags &= ~IFF_DRV_RUNNING;
	sc->sc_flags &= ~AUE_FLAG_LINK;

	/*
	 * stop all the transfers, if not already stopped:
	 */
	usbd_transfer_stop(sc->sc_xfer[AUE_BULK_DT_WR]);
	usbd_transfer_stop(sc->sc_xfer[AUE_BULK_DT_RD]);
	usbd_transfer_stop(sc->sc_xfer[AUE_INTR_DT_RD]);

	aue_csr_write_1(sc, AUE_CTL0, 0);
	aue_csr_write_1(sc, AUE_CTL1, 0);
	aue_reset(sc);
}
