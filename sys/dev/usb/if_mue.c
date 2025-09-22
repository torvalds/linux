/*	$OpenBSD: if_mue.c,v 1.12 2024/05/23 03:21:08 jsg Exp $	*/

/*
 * Copyright (c) 2018 Kevin Lo <kevlo@openbsd.org>
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

/* Driver for Microchip LAN7500/LAN7800 chipsets. */

#include "bpfilter.h"

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/sockio.h>
#include <sys/rwlock.h>
#include <sys/mbuf.h>

#include <sys/device.h>

#include <machine/bus.h>

#include <net/if.h>
#include <net/if_dl.h>
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
#include <dev/usb/usbdivar.h>
#include <dev/usb/usbdevs.h>

#include <dev/usb/if_muereg.h>

#ifdef MUE_DEBUG
#define DPRINTF(x)	do { if (muedebug) printf x; } while (0)
#define DPRINTFN(n,x)	do { if (muedebug >= (n)) printf x; } while (0)
int	muedebug = 0;
#else
#define DPRINTF(x)
#define DPRINTFN(n,x)
#endif

/*
 * Various supported device vendors/products.
 */
struct mue_type {
	struct usb_devno	mue_dev;
	uint16_t		mue_flags;
#define LAN7500	0x0001		/* LAN7500 */
};

const struct mue_type mue_devs[] = {
	{ { USB_VENDOR_SMC2, USB_PRODUCT_SMC2_LAN7500 }, LAN7500 },
	{ { USB_VENDOR_SMC2, USB_PRODUCT_SMC2_LAN7505 }, LAN7500 },
	{ { USB_VENDOR_SMC2, USB_PRODUCT_SMC2_LAN7800 }, 0 },
	{ { USB_VENDOR_SMC2, USB_PRODUCT_SMC2_LAN7801 }, 0 },
	{ { USB_VENDOR_SMC2, USB_PRODUCT_SMC2_LAN7850 }, 0 }
};

#define mue_lookup(v, p)	((struct mue_type *)usb_lookup(mue_devs, v, p))

int	mue_match(struct device *, void *, void *);
void	mue_attach(struct device *, struct device *, void *);
int	mue_detach(struct device *, int);
 
struct cfdriver mue_cd = {
	NULL, "mue", DV_IFNET
};
	  
const struct cfattach mue_ca = {
	sizeof(struct mue_softc), mue_match, mue_attach, mue_detach
};

uint32_t	mue_csr_read(struct mue_softc *, uint32_t);
int		mue_csr_write(struct mue_softc *, uint32_t, uint32_t);

void		mue_lock_mii(struct mue_softc *);
void		mue_unlock_mii(struct mue_softc *);

int		mue_mii_wait(struct mue_softc *);
int		mue_miibus_readreg(struct device *, int, int);
void		mue_miibus_writereg(struct device *, int, int, int);
void		mue_miibus_statchg(struct device *);
int		mue_ifmedia_upd(struct ifnet *);
void		mue_ifmedia_sts(struct ifnet *, struct ifmediareq *);

int		mue_eeprom_wait(struct mue_softc *);
uint8_t		mue_eeprom_getbyte(struct mue_softc *, int, uint8_t *);
int		mue_read_eeprom(struct mue_softc *, caddr_t, int, int);
int		mue_dataport_wait(struct mue_softc *);
void		mue_dataport_write(struct mue_softc *, uint32_t, uint32_t,
		    uint32_t, uint32_t *);
void		mue_init_ltm(struct mue_softc *);
int		mue_chip_init(struct mue_softc *);
void		mue_set_macaddr(struct mue_softc *);

int		mue_rx_list_init(struct mue_softc *);
int		mue_tx_list_init(struct mue_softc *);
int		mue_open_pipes(struct mue_softc *);
int		mue_encap(struct mue_softc *, struct mbuf *, int);
void		mue_iff(struct mue_softc *);
void		mue_rxeof(struct usbd_xfer *, void *, usbd_status);
void		mue_txeof(struct usbd_xfer *, void *, usbd_status);

void		mue_init(void *);
int		mue_ioctl(struct ifnet *, u_long, caddr_t);
void		mue_watchdog(struct ifnet *);
void		mue_reset(struct mue_softc *);
void		mue_start(struct ifnet *);
void		mue_stop(struct mue_softc *);
void		mue_tick(void *);
void		mue_tick_task(void *);

#define MUE_SETBIT(sc, reg, x)	\
	mue_csr_write(sc, reg, mue_csr_read(sc, reg) | (x))

#define MUE_CLRBIT(sc, reg, x)	\
	mue_csr_write(sc, reg, mue_csr_read(sc, reg) & ~(x))

#if defined(__arm__) || defined(__arm64__)

#include <dev/ofw/openfirm.h>

void
mue_enaddr_OF(struct mue_softc *sc)
{
	char *device = "/axi/usb/hub/ethernet";
	char prop[64];
	int node;

	if (sc->mue_dev.dv_unit != 0)
		return;

	/* Get the Raspberry Pi MAC address from FDT. */
	if ((node = OF_finddevice("/aliases")) == -1)
		return;
	if (OF_getprop(node, "ethernet0", prop, sizeof(prop)) > 0 ||
	    OF_getprop(node, "ethernet", prop, sizeof(prop)) > 0)
		device = prop;

	if ((node = OF_finddevice(device)) == -1)
		return;
	if (OF_getprop(node, "local-mac-address", sc->arpcom.ac_enaddr,
	    sizeof(sc->arpcom.ac_enaddr)) != sizeof(sc->arpcom.ac_enaddr)) {
		OF_getprop(node, "mac-address", sc->arpcom.ac_enaddr,
		    sizeof(sc->arpcom.ac_enaddr));
	}
}
#else
#define mue_enaddr_OF(x) do {} while(0)
#endif

uint32_t
mue_csr_read(struct mue_softc *sc, uint32_t reg)
{
	usb_device_request_t req;
	usbd_status err;
	uDWord val;

	if (usbd_is_dying(sc->mue_udev))
		return (0);

	USETDW(val, 0);
	req.bmRequestType = UT_READ_VENDOR_DEVICE;
	req.bRequest = MUE_UR_READREG;
	USETW(req.wValue, 0);
	USETW(req.wIndex, reg);
	USETW(req.wLength, 4);

	err = usbd_do_request(sc->mue_udev, &req, &val);
	if (err) {
		DPRINTF(("%s: mue_csr_read: reg=0x%x err=%s\n",
		    sc->mue_dev.dv_xname, reg, usbd_errstr(err)));
		return (0);
	}

	return (UGETDW(val));
}

int
mue_csr_write(struct mue_softc *sc, uint32_t reg, uint32_t aval)
{
	usb_device_request_t req;
	usbd_status err;
	uDWord val;

	if (usbd_is_dying(sc->mue_udev))
		return (0);

	USETDW(val, aval);
	req.bmRequestType = UT_WRITE_VENDOR_DEVICE;
	req.bRequest = MUE_UR_WRITEREG;
	USETW(req.wValue, 0);
	USETW(req.wIndex, reg);
	USETW(req.wLength, 4);

	err = usbd_do_request(sc->mue_udev, &req, &val);
	if (err) {
		DPRINTF(("%s: mue_csr_write: reg=0x%x err=%s\n",
		    sc->mue_dev.dv_xname, reg, usbd_errstr(err)));
		return (-1);
	}

	return (0);
}

/* 
 * Get exclusive access to the MII registers.
 */
void
mue_lock_mii(struct mue_softc *sc)
{
	sc->mue_refcnt++;
	rw_enter_write(&sc->mue_mii_lock);
}

void
mue_unlock_mii(struct mue_softc *sc)
{
	rw_exit_write(&sc->mue_mii_lock);
	if (--sc->mue_refcnt < 0)
		usb_detach_wakeup(&sc->mue_dev);
}

/*
 * Wait for the MII to become ready.
 */
int
mue_mii_wait(struct mue_softc *sc)
{
	int ntries;	

	for (ntries = 0; ntries < 100; ntries++) {
		if (!(mue_csr_read(sc, MUE_MII_ACCESS) & MUE_MII_ACCESS_BUSY))
			return (0);
		DELAY(5);
	}

	printf("%s: MII timed out\n", sc->mue_dev.dv_xname); 
	return (1);
}

int
mue_miibus_readreg(struct device *dev, int phy, int reg)
{
	struct mue_softc *sc = (void *)dev;
	uint32_t val;

	if (usbd_is_dying(sc->mue_udev))
		return (0);

	if (sc->mue_phyno != phy)
		return (0);

	mue_lock_mii(sc);
	if (mue_mii_wait(sc) != 0)
		return (0);

	mue_csr_write(sc, MUE_MII_ACCESS, MUE_MII_ACCESS_READ |
	    MUE_MII_ACCESS_BUSY | MUE_MII_ACCESS_REGADDR(reg) |
	    MUE_MII_ACCESS_PHYADDR(phy));

	if (mue_mii_wait(sc) != 0)
		printf("%s: MII read timed out\n", sc->mue_dev.dv_xname);

	val = mue_csr_read(sc, MUE_MII_DATA);
	mue_unlock_mii(sc);
	return (val & 0xffff);
}

void
mue_miibus_writereg(struct device *dev, int phy, int reg, int data)
{
	struct mue_softc *sc = (void *)dev;

	if (usbd_is_dying(sc->mue_udev))
		return;

	if (sc->mue_phyno != phy)
		return;

	mue_lock_mii(sc);
	if (mue_mii_wait(sc) != 0)
		return;

	mue_csr_write(sc, MUE_MII_DATA, data);
	mue_csr_write(sc, MUE_MII_ACCESS, MUE_MII_ACCESS_WRITE |
	    MUE_MII_ACCESS_BUSY | MUE_MII_ACCESS_REGADDR(reg) |
	    MUE_MII_ACCESS_PHYADDR(phy));

	if (mue_mii_wait(sc) != 0)
		printf("%s: MII write timed out\n", sc->mue_dev.dv_xname);

	mue_unlock_mii(sc);
}

void
mue_miibus_statchg(struct device *dev)
{
	struct mue_softc *sc = (void *)dev;
	struct mii_data *mii = GET_MII(sc);
	struct ifnet *ifp = GET_IFP(sc);
	uint32_t flow, threshold;

	if (mii == NULL || ifp == NULL ||
	    (ifp->if_flags & IFF_RUNNING) == 0)
		return;

	sc->mue_link = 0;
	if ((mii->mii_media_status & (IFM_ACTIVE | IFM_AVALID)) ==
	    (IFM_ACTIVE | IFM_AVALID)) {
		switch (IFM_SUBTYPE(mii->mii_media_active)) {
		case IFM_10_T:
		case IFM_100_TX:
		case IFM_1000_T:
			sc->mue_link++;
			break;
		default:
			break;
		}
	}

	/* Lost link, do nothing. */
	if (sc->mue_link == 0)
		return;

	if (!(sc->mue_flags & LAN7500)) {
		if (sc->mue_udev->speed == USB_SPEED_SUPER) {
			if (IFM_SUBTYPE(mii->mii_media_active) == IFM_1000_T) {
				/* Disable U2 and enable U1. */
				MUE_CLRBIT(sc, MUE_USB_CFG1,
				    MUE_USB_CFG1_DEV_U2_INIT_EN);
				MUE_SETBIT(sc, MUE_USB_CFG1,
				    MUE_USB_CFG1_DEV_U1_INIT_EN);
			} else {
				/* Enable U1 and U2. */
				MUE_SETBIT(sc, MUE_USB_CFG1,
				    MUE_USB_CFG1_DEV_U1_INIT_EN |
				    MUE_USB_CFG1_DEV_U2_INIT_EN);
			}
		}
	}

	threshold = 0;
	flow = 0;
	if (IFM_OPTIONS(mii->mii_media_active) & IFM_FDX) {
		if (IFM_OPTIONS(mii->mii_media_active) & IFM_ETH_TXPAUSE) {
			flow |= MUE_FLOW_TX_FCEN | MUE_FLOW_PAUSE_TIME;

			/* XXX magic numbers come from Linux driver. */
			if (sc->mue_flags & LAN7500) {
				threshold = 0x820;
			} else {
				threshold =
				    (sc->mue_udev->speed == USB_SPEED_SUPER) ?
				    0x817 : 0x211;
			}
		}
		if (IFM_OPTIONS(mii->mii_media_active) & IFM_ETH_RXPAUSE)
			flow |= MUE_FLOW_RX_FCEN;
	}
	mue_csr_write(sc, (sc->mue_flags & LAN7500) ?
	    MUE_FCT_FLOW : MUE_7800_FCT_FLOW, threshold);

	/* Threshold value should be set before enabling flow. */
	mue_csr_write(sc, MUE_FLOW, flow);
}

/*
 * Set media options.
 */
int
mue_ifmedia_upd(struct ifnet *ifp)
{
	struct mue_softc *sc = ifp->if_softc;
	struct mii_data *mii = GET_MII(sc);

	if (mii->mii_instance) {
		struct mii_softc *miisc;
		LIST_FOREACH(miisc, &mii->mii_phys, mii_list)
			mii_phy_reset(miisc);
	}
	return (mii_mediachg(mii));
}

/*
 * Report current media status.
 */
void
mue_ifmedia_sts(struct ifnet *ifp, struct ifmediareq *ifmr)
{
	struct mue_softc *sc = ifp->if_softc;
	struct mii_data *mii = GET_MII(sc);

	mii_pollstat(mii);
	ifmr->ifm_active = mii->mii_media_active;
	ifmr->ifm_status = mii->mii_media_status;
}

int
mue_eeprom_wait(struct mue_softc *sc)
{
	uint32_t val;
	int ntries;	

	for (ntries = 0; ntries < 100; ntries++) {
		val = mue_csr_read(sc, MUE_E2P_CMD);
		if (!(val & MUE_E2P_CMD_BUSY) || (val & MUE_E2P_CMD_TIMEOUT))
			return (0);
		DELAY(5);
	}

	return (1);
}

uint8_t
mue_eeprom_getbyte(struct mue_softc *sc, int addr, uint8_t *dest)
{
	uint32_t byte = 0;
	int ntries;

	for (ntries = 0; ntries < 100; ntries++) {
		if (!(mue_csr_read(sc, MUE_E2P_CMD) & MUE_E2P_CMD_BUSY))
			break;
		DELAY(5);
	}

	if (ntries == 100) {
		printf("%s: EEPROM failed to come ready\n",
		    sc->mue_dev.dv_xname); 
		return (ETIMEDOUT);
	}

	mue_csr_write(sc, MUE_E2P_CMD, MUE_E2P_CMD_READ | MUE_E2P_CMD_BUSY |
	    (addr & MUE_E2P_CMD_ADDR_MASK));

	if (mue_eeprom_wait(sc) != 0) {
		printf("%s: EEPROM read timed out\n", sc->mue_dev.dv_xname);
		return (ETIMEDOUT);
	}

	byte = mue_csr_read(sc, MUE_E2P_DATA);
	*dest = byte & 0xff;

	return (0);
}

int
mue_read_eeprom(struct mue_softc *sc, caddr_t dest, int off, int cnt)
{
	uint32_t val;
	uint8_t byte = 0;
	int i, err = 0;

	/* 
	 * EEPROM pins are muxed with the LED function on LAN7800 device.
	 */
	val = mue_csr_read(sc, MUE_HW_CFG);
	if (sc->mue_product == USB_PRODUCT_SMC2_LAN7800) {
		MUE_CLRBIT(sc, MUE_HW_CFG,
		    MUE_HW_CFG_LED0_EN | MUE_HW_CFG_LED1_EN);
	}

	for (i = 0; i < cnt; i++) {
		err = mue_eeprom_getbyte(sc, off + i, &byte);
		if (err)
			break;
		*(dest + i) = byte;
	}

	if (sc->mue_product == USB_PRODUCT_SMC2_LAN7800)
		mue_csr_write(sc, MUE_HW_CFG, val);

	return (err ? 1 : 0);
}

int             
mue_dataport_wait(struct mue_softc *sc)
{
	int ntries;	

	for (ntries = 0; ntries < 100; ntries++) {
		if (mue_csr_read(sc, MUE_DP_SEL) & MUE_DP_SEL_DPRDY)
			return (0);
		DELAY(5);
	}

	printf("%s: dataport timed out\n", sc->mue_dev.dv_xname); 
	return (1);
}

void
mue_dataport_write(struct mue_softc *sc, uint32_t sel, uint32_t addr,
    uint32_t cnt, uint32_t *data)
{
	int i;

	if (mue_dataport_wait(sc) != 0)
		return;

	mue_csr_write(sc, MUE_DP_SEL,
	    (mue_csr_read(sc, MUE_DP_SEL) & ~MUE_DP_SEL_RSEL_MASK) | sel);

	for (i = 0; i < cnt; i++) {
		mue_csr_write(sc, MUE_DP_ADDR, addr + i);
		mue_csr_write(sc, MUE_DP_DATA, data[i]);
		mue_csr_write(sc, MUE_DP_CMD, MUE_DP_CMD_WRITE);
		if (mue_dataport_wait(sc) != 0)
			return;
	}
}

void
mue_init_ltm(struct mue_softc *sc)
{
	uint8_t idx[6] = { 0 };
	int i;

	if (mue_csr_read(sc, MUE_USB_CFG1) & MUE_USB_CFG1_LTM_ENABLE) {
		uint8_t temp[2];

		if (mue_read_eeprom(sc, (caddr_t)&temp, MUE_EE_LTM_OFFSET, 2)) {
			if (temp[0] != 24)
				goto done;
			mue_read_eeprom(sc, (caddr_t)&idx, temp[1] << 1, 24);
		}
	}
done:
	for (i = 0; i < sizeof(idx); i++)
		mue_csr_write(sc, MUE_LTM_INDEX(i), idx[i]);
}

int
mue_chip_init(struct mue_softc *sc)
{
	uint32_t val;
	int ntries;

	if (sc->mue_flags & LAN7500) {
		for (ntries = 0; ntries < 100; ntries++) {
			if (mue_csr_read(sc, MUE_PMT_CTL) & MUE_PMT_CTL_READY)
				break;
			DELAY(1000);	/* 1 msec */
		}
		if (ntries == 100) {
			printf("%s: timeout waiting for device ready\n",
			    sc->mue_dev.dv_xname);
			return (ETIMEDOUT);
		}
	}

	MUE_SETBIT(sc, MUE_HW_CFG, MUE_HW_CFG_LRST);

	for (ntries = 0; ntries < 1000; ntries++) {
		if (!(mue_csr_read(sc, MUE_HW_CFG) & MUE_HW_CFG_LRST))
			break;
		DELAY(5);
	}
	if (ntries == 1000) {
		printf("%s: timeout on lite software reset\n",
		    sc->mue_dev.dv_xname);
		return (ETIMEDOUT);
	}

	/* Respond to the IN token with a NAK. */
	if (sc->mue_flags & LAN7500)
		MUE_SETBIT(sc, MUE_HW_CFG, MUE_HW_CFG_BIR);
	else
		MUE_SETBIT(sc, MUE_USB_CFG0, MUE_USB_CFG0_BIR);

	if (sc->mue_flags & LAN7500) {
		mue_csr_write(sc, MUE_BURST_CAP,
		    (sc->mue_udev->speed == USB_SPEED_HIGH) ?
		    MUE_BURST_MIN_BUFSZ : MUE_BURST_MAX_BUFSZ);
		mue_csr_write(sc, MUE_BULK_IN_DELAY, MUE_DEFAULT_BULKIN_DELAY);

		MUE_SETBIT(sc, MUE_HW_CFG, MUE_HW_CFG_BCE | MUE_HW_CFG_MEF);

		/* Set undocumented FIFO sizes. */
		mue_csr_write(sc, MUE_FCT_RX_FIFO_END, 0x27);
		mue_csr_write(sc, MUE_FCT_TX_FIFO_END, 0x17);
	} else {
		/* Init LTM. */
		mue_init_ltm(sc);

		val = (sc->mue_udev->speed == USB_SPEED_SUPER) ?
		    MUE_7800_BURST_MIN_BUFSZ : MUE_7800_BURST_MAX_BUFSZ;
		mue_csr_write(sc, MUE_7800_BURST_CAP, val);
		mue_csr_write(sc, MUE_7800_BULK_IN_DELAY,
		    MUE_7800_DEFAULT_BULKIN_DELAY);

		MUE_SETBIT(sc, MUE_HW_CFG, MUE_HW_CFG_MEF);
		MUE_SETBIT(sc, MUE_USB_CFG0, MUE_USB_CFG0_BCE);
	}

	mue_csr_write(sc, MUE_INT_STATUS, 0xffffffff);
	mue_csr_write(sc, (sc->mue_flags & LAN7500) ?
	    MUE_FCT_FLOW : MUE_7800_FCT_FLOW, 0);
	mue_csr_write(sc, MUE_FLOW, 0);
 
	/* Reset PHY. */
	MUE_SETBIT(sc, MUE_PMT_CTL, MUE_PMT_CTL_PHY_RST);
	for (ntries = 0; ntries < 100; ntries++) {
		val = mue_csr_read(sc, MUE_PMT_CTL);
		if (!(val & MUE_PMT_CTL_PHY_RST) && (val & MUE_PMT_CTL_READY))
			break;
		DELAY(10000);
	}
	if (ntries == 100) {
		printf("%s: timeout waiting for PHY reset\n",
		    sc->mue_dev.dv_xname);
		return (ETIMEDOUT);
	}

	/* LAN7801 only has RGMII mode. */
	if (sc->mue_product == USB_PRODUCT_SMC2_LAN7801)
		MUE_CLRBIT(sc, MUE_MAC_CR, MUE_MAC_CR_GMII_EN);

	if (sc->mue_flags & LAN7500 || !sc->mue_eeprom_present) {
		/* Allow MAC to detect speed and duplex from PHY. */
		MUE_SETBIT(sc, MUE_MAC_CR, MUE_MAC_CR_AUTO_SPEED |
		    MUE_MAC_CR_AUTO_DUPLEX);
	}

	MUE_SETBIT(sc, MUE_MAC_TX, MUE_MAC_TX_TXEN);
	MUE_SETBIT(sc, (sc->mue_flags & LAN7500) ?
	    MUE_FCT_TX_CTL : MUE_7800_FCT_TX_CTL, MUE_FCT_TX_CTL_EN);

	/* Set the maximum frame size. */
	MUE_CLRBIT(sc, MUE_MAC_RX, MUE_MAC_RX_RXEN);
	MUE_SETBIT(sc, MUE_MAC_RX, MUE_MAC_RX_MAX_LEN(ETHER_MAX_LEN));
	MUE_SETBIT(sc, MUE_MAC_RX, MUE_MAC_RX_RXEN);

	MUE_SETBIT(sc, (sc->mue_flags & LAN7500) ?
	    MUE_FCT_RX_CTL : MUE_7800_FCT_RX_CTL, MUE_FCT_RX_CTL_EN);

	/* Enable LEDs. */
	if (sc->mue_product == USB_PRODUCT_SMC2_LAN7800 &&
	    sc->mue_eeprom_present == 0) {
		MUE_SETBIT(sc, MUE_HW_CFG,
		    MUE_HW_CFG_LED0_EN | MUE_HW_CFG_LED1_EN);
	}

	return (0);
}

void
mue_set_macaddr(struct mue_softc *sc)
{
	struct ifnet *ifp = &sc->arpcom.ac_if;
	const uint8_t *eaddr = LLADDR(ifp->if_sadl);
	uint32_t val, reg;

	reg = (sc->mue_flags & LAN7500) ? MUE_ADDR_FILTX : MUE_7800_ADDR_FILTX;

	val = (eaddr[3] << 24) | (eaddr[2] << 16) | (eaddr[1] << 8) | eaddr[0];
	mue_csr_write(sc, MUE_RX_ADDRL, val);
	mue_csr_write(sc, reg + 4, val);
	val = (eaddr[5] << 8) | eaddr[4];
	mue_csr_write(sc, MUE_RX_ADDRH, val);
	mue_csr_write(sc, reg, val | MUE_ADDR_FILTX_VALID);
}

/* 
 * Probe for a Microchip chip.
 */
int
mue_match(struct device *parent, void *match, void *aux)
{
	struct usb_attach_arg *uaa = aux;

	if (uaa->iface == NULL || uaa->configno != 1)
		return (UMATCH_NONE);

	return (mue_lookup(uaa->vendor, uaa->product) != NULL ?
	    UMATCH_VENDOR_PRODUCT_CONF_IFACE : UMATCH_NONE);
}

void
mue_attach(struct device *parent, struct device *self, void *aux)
{
	struct mue_softc *sc = (struct mue_softc *)self;
	struct usb_attach_arg *uaa = aux;
	usb_interface_descriptor_t *id;
	usb_endpoint_descriptor_t *ed;
	struct mii_data	*mii;
	struct ifnet *ifp;
	int i, s;

	sc->mue_udev = uaa->device;
	sc->mue_iface = uaa->iface;
	sc->mue_product = uaa->product;
	sc->mue_flags = mue_lookup(uaa->vendor, uaa->product)->mue_flags;

	usb_init_task(&sc->mue_tick_task, mue_tick_task, sc,
	    USB_TASK_TYPE_GENERIC);
	rw_init(&sc->mue_mii_lock, "muemii");
	usb_init_task(&sc->mue_stop_task, (void (*)(void *))mue_stop, sc,
	    USB_TASK_TYPE_GENERIC);

	/* Decide on what our bufsize will be. */
	if (sc->mue_flags & LAN7500)
		sc->mue_bufsz = (sc->mue_udev->speed == USB_SPEED_HIGH) ?
		    MUE_MAX_BUFSZ : MUE_MIN_BUFSZ;
	else
		sc->mue_bufsz = MUE_7800_BUFSZ;

	/* Find endpoints. */
	id = usbd_get_interface_descriptor(sc->mue_iface);
	for (i = 0; i < id->bNumEndpoints; i++) {
		ed = usbd_interface2endpoint_descriptor(sc->mue_iface, i);
		if (ed == NULL) {
			printf("%s: couldn't get ep %d\n",
			    sc->mue_dev.dv_xname, i);
			return;
		}
		if (UE_GET_DIR(ed->bEndpointAddress) == UE_DIR_IN &&
		    UE_GET_XFERTYPE(ed->bmAttributes) == UE_BULK) {
			sc->mue_ed[MUE_ENDPT_RX] = ed->bEndpointAddress;
		} else if (UE_GET_DIR(ed->bEndpointAddress) == UE_DIR_OUT &&
			   UE_GET_XFERTYPE(ed->bmAttributes) == UE_BULK) {
			sc->mue_ed[MUE_ENDPT_TX] = ed->bEndpointAddress;
		} else if (UE_GET_DIR(ed->bEndpointAddress) == UE_DIR_IN &&
			   UE_GET_XFERTYPE(ed->bmAttributes) == UE_INTERRUPT) {
			sc->mue_ed[MUE_ENDPT_INTR] = ed->bEndpointAddress;
		}
	}

	s = splnet();

	sc->mue_phyno = 1;

	/* Check if the EEPROM programmed indicator is present. */
	mue_read_eeprom(sc, (caddr_t)&i, MUE_EE_IND_OFFSET, 1);
	sc->mue_eeprom_present = (i == MUE_EEPROM_INDICATOR) ? 1 : 0;

	if (mue_chip_init(sc) != 0) {
		printf("%s: chip initialization failed\n",
		    sc->mue_dev.dv_xname);
		splx(s);
		return;
	}

	/* Get station address from the EEPROM. */
	if (sc->mue_eeprom_present) {
		if (mue_read_eeprom(sc, (caddr_t)&sc->arpcom.ac_enaddr,
		    MUE_EE_MAC_OFFSET, ETHER_ADDR_LEN)) {
			printf("%s: failed to read station address\n",
			    sc->mue_dev.dv_xname);
			splx(s);
			return;
		}
	} else
		mue_enaddr_OF(sc);

	/* A Microchip chip was detected.  Inform the world. */
	printf("%s:", sc->mue_dev.dv_xname);
	if (sc->mue_flags & LAN7500)
		printf(" LAN7500");
	else
		printf(" LAN7800");
	printf(", address %s\n", ether_sprintf(sc->arpcom.ac_enaddr));

	/* Initialize interface info.*/
	ifp = GET_IFP(sc);
	ifp->if_softc = sc;
	ifp->if_flags = IFF_BROADCAST | IFF_SIMPLEX | IFF_MULTICAST;
	ifp->if_ioctl = mue_ioctl;
	ifp->if_start = mue_start;
	ifp->if_watchdog = mue_watchdog;
	strlcpy(ifp->if_xname, sc->mue_dev.dv_xname, IFNAMSIZ);

	ifp->if_capabilities = IFCAP_VLAN_MTU;

	/* Initialize MII/media info. */
	mii = GET_MII(sc);
	mii->mii_ifp = ifp;
	mii->mii_readreg = mue_miibus_readreg;
	mii->mii_writereg = mue_miibus_writereg;
	mii->mii_statchg = mue_miibus_statchg;
	mii->mii_flags = MIIF_AUTOTSLEEP;

	ifmedia_init(&mii->mii_media, 0, mue_ifmedia_upd, mue_ifmedia_sts);
	mii_attach(self, mii, 0xffffffff, MII_PHY_ANY, MII_OFFSET_ANY,
	    MIIF_DOPAUSE);

	if (LIST_FIRST(&mii->mii_phys) == NULL) {
		ifmedia_add(&mii->mii_media, IFM_ETHER | IFM_NONE, 0, NULL);
		ifmedia_set(&mii->mii_media, IFM_ETHER | IFM_NONE);
	} else
		ifmedia_set(&mii->mii_media, IFM_ETHER | IFM_AUTO);

	/* Attach the interface. */
	if_attach(ifp);
	ether_ifattach(ifp);

	timeout_set(&sc->mue_stat_ch, mue_tick, sc);

	splx(s);
}

int
mue_detach(struct device *self, int flags)
{
	struct mue_softc *sc = (struct mue_softc *)self;
	struct ifnet *ifp = GET_IFP(sc);
	int s;

	if (timeout_initialized(&sc->mue_stat_ch))
		timeout_del(&sc->mue_stat_ch);

	if (sc->mue_ep[MUE_ENDPT_TX] != NULL)
		usbd_abort_pipe(sc->mue_ep[MUE_ENDPT_TX]);
	if (sc->mue_ep[MUE_ENDPT_RX] != NULL)
		usbd_abort_pipe(sc->mue_ep[MUE_ENDPT_RX]);
	if (sc->mue_ep[MUE_ENDPT_INTR] != NULL)
		usbd_abort_pipe(sc->mue_ep[MUE_ENDPT_INTR]);

	/*
	 * Remove any pending tasks.  They cannot be executing because they run
	 * in the same thread as detach.
	 */
	usb_rem_task(sc->mue_udev, &sc->mue_tick_task);
	usb_rem_task(sc->mue_udev, &sc->mue_stop_task);

	s = splusb();

	if (--sc->mue_refcnt >= 0) {
		/* Wait for processes to go away */
		usb_detach_wait(&sc->mue_dev);
	}

	if (ifp->if_flags & IFF_RUNNING)
		mue_stop(sc);

	mii_detach(&sc->mue_mii, MII_PHY_ANY, MII_OFFSET_ANY);
	ifmedia_delete_instance(&sc->mue_mii.mii_media, IFM_INST_ANY);
	if (ifp->if_softc != NULL) {
		ether_ifdetach(ifp);
		if_detach(ifp);
	}

	splx(s);

	return (0);
}

int
mue_rx_list_init(struct mue_softc *sc)
{
	struct mue_cdata *cd;
	struct mue_chain *c;
	int i;

	DPRINTF(("%s: %s: enter\n", sc->mue_dev.dv_xname, __func__));

	cd = &sc->mue_cdata;
	for (i = 0; i < MUE_RX_LIST_CNT; i++) {
		c = &cd->mue_rx_chain[i];
		c->mue_sc = sc;
		c->mue_idx = i;
		c->mue_mbuf = NULL;
		if (c->mue_xfer == NULL) {
			c->mue_xfer = usbd_alloc_xfer(sc->mue_udev);
			if (c->mue_xfer == NULL)
				return (ENOBUFS);
			c->mue_buf = usbd_alloc_buffer(c->mue_xfer,
			    sc->mue_bufsz);
			if (c->mue_buf == NULL) {
				usbd_free_xfer(c->mue_xfer);
				return (ENOBUFS);
			}
		}
	}

	return (0);
}

int
mue_tx_list_init(struct mue_softc *sc)
{
	struct mue_cdata *cd;
	struct mue_chain *c;
	int i;

	DPRINTF(("%s: %s: enter\n", sc->mue_dev.dv_xname, __func__));

	cd = &sc->mue_cdata;
	for (i = 0; i < MUE_TX_LIST_CNT; i++) {
		c = &cd->mue_tx_chain[i];
		c->mue_sc = sc;
		c->mue_idx = i;
		c->mue_mbuf = NULL;
		if (c->mue_xfer == NULL) {
			c->mue_xfer = usbd_alloc_xfer(sc->mue_udev);
			if (c->mue_xfer == NULL)
				return (ENOBUFS);
			c->mue_buf = usbd_alloc_buffer(c->mue_xfer,
			    sc->mue_bufsz);
			if (c->mue_buf == NULL) {
				usbd_free_xfer(c->mue_xfer);
				return (ENOBUFS);
			}
		}
	}

	return (0);
}

int
mue_open_pipes(struct mue_softc *sc)
{
	struct mue_chain *c;
	usbd_status err;
	int i;

	/* Open RX and TX pipes. */
	err = usbd_open_pipe(sc->mue_iface, sc->mue_ed[MUE_ENDPT_RX],
	    USBD_EXCLUSIVE_USE, &sc->mue_ep[MUE_ENDPT_RX]);
	if (err) {
		printf("%s: open rx pipe failed: %s\n",
		    sc->mue_dev.dv_xname, usbd_errstr(err));
		return (EIO);
	}
	err = usbd_open_pipe(sc->mue_iface, sc->mue_ed[MUE_ENDPT_TX],
	    USBD_EXCLUSIVE_USE, &sc->mue_ep[MUE_ENDPT_TX]);
	if (err) {
		printf("%s: open tx pipe failed: %s\n",
		    sc->mue_dev.dv_xname, usbd_errstr(err));
		return (EIO);
	}

	/* Start up the receive pipe. */
	for (i = 0; i < MUE_RX_LIST_CNT; i++) {
		c = &sc->mue_cdata.mue_rx_chain[i];
		usbd_setup_xfer(c->mue_xfer, sc->mue_ep[MUE_ENDPT_RX],
		    c, c->mue_buf, sc->mue_bufsz,
		    USBD_SHORT_XFER_OK | USBD_NO_COPY, USBD_NO_TIMEOUT,
		    mue_rxeof);
		usbd_transfer(c->mue_xfer);
	}

	return (0);
}

int
mue_encap(struct mue_softc *sc, struct mbuf *m, int idx)
{
	struct mue_chain *c;
	usbd_status err;
	struct mue_txbuf_hdr hdr;
	int length;

	c = &sc->mue_cdata.mue_tx_chain[idx];

	hdr.tx_cmd_a = htole32((m->m_pkthdr.len & MUE_TX_CMD_A_LEN_MASK) |
	    MUE_TX_CMD_A_FCS);
	/* Disable segmentation offload. */
	hdr.tx_cmd_b = htole32(0);
	memcpy(c->mue_buf, &hdr, sizeof(hdr)); 
	length = sizeof(hdr);

	m_copydata(m, 0, m->m_pkthdr.len, c->mue_buf + length);
	length += m->m_pkthdr.len;

	c->mue_mbuf = m;

	usbd_setup_xfer(c->mue_xfer, sc->mue_ep[MUE_ENDPT_TX],
	    c, c->mue_buf, length, USBD_FORCE_SHORT_XFER | USBD_NO_COPY,
	    10000, mue_txeof);

	/* Transmit */
	err = usbd_transfer(c->mue_xfer);
	if (err != USBD_IN_PROGRESS) {
		c->mue_mbuf = NULL;
		mue_stop(sc);
		return(EIO);
	}

	sc->mue_cdata.mue_tx_cnt++;

	return(0);
}

void
mue_iff(struct mue_softc *sc)
{
	struct ifnet *ifp = GET_IFP(sc);
	struct arpcom *ac = &sc->arpcom;
	struct ether_multi *enm;
	struct ether_multistep step;
	uint32_t h = 0, hashtbl[MUE_DP_SEL_VHF_HASH_LEN], reg, rxfilt;

	if (usbd_is_dying(sc->mue_udev))
		return;

	reg = (sc->mue_flags & LAN7500) ? MUE_RFE_CTL : MUE_7800_RFE_CTL;
	rxfilt = mue_csr_read(sc, reg);
	rxfilt &= ~(MUE_RFE_CTL_PERFECT | MUE_RFE_CTL_MULTICAST_HASH |
	    MUE_RFE_CTL_UNICAST | MUE_RFE_CTL_MULTICAST);
	memset(hashtbl, 0, sizeof(hashtbl));
	ifp->if_flags &= ~IFF_ALLMULTI;

	/* Always accept broadcast frames. */
	rxfilt |= MUE_RFE_CTL_BROADCAST;

	if (ifp->if_flags & IFF_PROMISC || ac->ac_multirangecnt > 0) {
		ifp->if_flags |= IFF_ALLMULTI;
		rxfilt |= MUE_RFE_CTL_MULTICAST;
		if (ifp->if_flags & IFF_PROMISC)
			rxfilt |= MUE_RFE_CTL_UNICAST | MUE_RFE_CTL_MULTICAST;
	} else {
		rxfilt |= MUE_RFE_CTL_PERFECT | MUE_RFE_CTL_MULTICAST_HASH;

		/* Now program new ones. */
		ETHER_FIRST_MULTI(step, ac, enm);
		while (enm != NULL) {
			h = ether_crc32_be(enm->enm_addrlo,
			    ETHER_ADDR_LEN) >> 23;
			hashtbl[h / 32] |= 1 << (h % 32); 
			ETHER_NEXT_MULTI(step, enm);
		}
	}

	mue_dataport_write(sc, MUE_DP_SEL_VHF, MUE_DP_SEL_VHF_VLAN_LEN,
	    MUE_DP_SEL_VHF_HASH_LEN, hashtbl);
	mue_csr_write(sc, reg, rxfilt);
}

void
mue_rxeof(struct usbd_xfer *xfer, void *priv, usbd_status status)
{
	struct mue_chain *c = (struct mue_chain *)priv;
	struct mue_softc *sc = c->mue_sc;
	struct ifnet *ifp = GET_IFP(sc);
	struct mbuf_list ml = MBUF_LIST_INITIALIZER();
	struct mbuf *m;
	struct mue_rxbuf_hdr hdr;
	u_char *buf = c->mue_buf;
	uint32_t total_len;
	int pktlen = 0;
	int s;

	if (usbd_is_dying(sc->mue_udev))
		return;

	if (!(ifp->if_flags & IFF_RUNNING))
		return;

	if (status != USBD_NORMAL_COMPLETION) {
		if (status == USBD_NOT_STARTED || status == USBD_CANCELLED)
			return;
		if (usbd_ratecheck(&sc->mue_rx_notice)) {
			printf("%s: usb errors on rx: %s\n",
			    sc->mue_dev.dv_xname, usbd_errstr(status));
		}
		if (status == USBD_STALLED)
			usbd_clear_endpoint_stall_async(sc->mue_ep[MUE_ENDPT_RX]);
		goto done;
	}

	usbd_get_xfer_status(xfer, NULL, NULL, &total_len, NULL);

	do {
		if (total_len < sizeof(hdr)) {
			ifp->if_ierrors++;
			goto done;
		}

		buf += pktlen;

		memcpy(&hdr, buf, sizeof(hdr));
		total_len -= sizeof(hdr);

		if (letoh32(hdr.rx_cmd_a) & MUE_RX_CMD_A_RED) {
			ifp->if_ierrors++;
			goto done;
		}

		pktlen = letoh32(hdr.rx_cmd_a) & MUE_RX_CMD_A_LEN_MASK;
		if (sc->mue_flags & LAN7500)
			pktlen -= 2;

		if (pktlen > total_len) {
			ifp->if_ierrors++;
			goto done;
		}

		buf += sizeof(hdr);

		if (total_len < pktlen)
			total_len = 0;
		else
			total_len -= pktlen;

		m = m_devget(buf, pktlen - ETHER_CRC_LEN, ETHER_ALIGN);
		if (m == NULL) {
			DPRINTF(("unable to allocate mbuf for next packet\n"));
			ifp->if_ierrors++;
			goto done;
		}
		ml_enqueue(&ml, m);
	} while (total_len > 0);

done:
	s = splnet();
	if_input(ifp, &ml);
	splx(s);

	memset(c->mue_buf, 0, sc->mue_bufsz);

	/* Setup new transfer. */
	usbd_setup_xfer(xfer, sc->mue_ep[MUE_ENDPT_RX],
	    c, c->mue_buf, sc->mue_bufsz, USBD_SHORT_XFER_OK | USBD_NO_COPY,
	    USBD_NO_TIMEOUT, mue_rxeof);
	usbd_transfer(xfer);

	DPRINTFN(10,("%s: %s: start rx\n", sc->mue_dev.dv_xname, __func__));
}

void
mue_txeof(struct usbd_xfer *xfer, void *priv, usbd_status status)
{
	struct mue_chain *c = priv;
	struct mue_softc *sc = c->mue_sc;
	struct ifnet *ifp = GET_IFP(sc);
	int s;

	if (usbd_is_dying(sc->mue_udev))
		return;

	s = splnet();

	DPRINTFN(10,("%s: %s: enter status=%d\n", sc->mue_dev.dv_xname,
	    __func__, status));

	if (status != USBD_NORMAL_COMPLETION) {
		if (status == USBD_NOT_STARTED || status == USBD_CANCELLED) {
			splx(s);
			return;
		}
		ifp->if_oerrors++;
		printf("%s: usb error on tx: %s\n", sc->mue_dev.dv_xname,
		    usbd_errstr(status));
		if (status == USBD_STALLED)
			usbd_clear_endpoint_stall_async(sc->mue_ep[MUE_ENDPT_TX]);
		splx(s);
		return;
	}

	ifp->if_timer = 0;
	ifq_clr_oactive(&ifp->if_snd);

	m_freem(c->mue_mbuf);
	c->mue_mbuf = NULL;

	if (ifq_empty(&ifp->if_snd) == 0)
		mue_start(ifp);

	splx(s);
}

void
mue_init(void *xsc)
{
	struct mue_softc *sc = xsc;
	struct ifnet *ifp = GET_IFP(sc);
	int s;

	s = splnet();

	/* Cancel pending I/O and free all TX/RX buffers. */
	mue_reset(sc);

	/* Set MAC address. */
	mue_set_macaddr(sc);

	/* Init RX ring. */
	if (mue_rx_list_init(sc) == ENOBUFS) {
		printf("%s: rx list init failed\n", sc->mue_dev.dv_xname);
		splx(s);
		return;
	}

	/* Init TX ring. */
	if (mue_tx_list_init(sc) == ENOBUFS) {
		printf("%s: tx list init failed\n", sc->mue_dev.dv_xname);
		splx(s);
		return;
	}

	/* Program promiscuous mode and multicast filters. */
	mue_iff(sc);

	if (mue_open_pipes(sc) != 0) {
		splx(s);
		return;
	}

	sc->mue_link = 0;
	ifp->if_flags |= IFF_RUNNING;
	ifq_clr_oactive(&ifp->if_snd);

	splx(s);

	timeout_add_sec(&sc->mue_stat_ch, 1);
}

int
mue_ioctl(struct ifnet *ifp, u_long cmd, caddr_t data)
{
	struct mue_softc *sc = ifp->if_softc;
	struct ifreq *ifr = (struct ifreq *)data;
	int s, error = 0;

	s = splnet();

	switch(cmd) {
	case SIOCSIFADDR:
		ifp->if_flags |= IFF_UP;
		if (!(ifp->if_flags & IFF_RUNNING))
			mue_init(sc);
		break;
	case SIOCSIFFLAGS:
		if (ifp->if_flags & IFF_UP) {
			if (ifp->if_flags & IFF_RUNNING)
				error = ENETRESET;
			else
				mue_init(sc);
		} else {
			if (ifp->if_flags & IFF_RUNNING)
				mue_stop(sc);
		}
		break;
	case SIOCGIFMEDIA:
	case SIOCSIFMEDIA:
		error = ifmedia_ioctl(ifp, ifr, &sc->mue_mii.mii_media, cmd);
		break;
	default:
		error = ether_ioctl(ifp, &sc->arpcom, cmd, data);
	}

	if (error == ENETRESET) {
		if (ifp->if_flags & IFF_RUNNING)
			mue_iff(sc);
		error = 0;
	}

	splx(s);

	return(error);
}

void
mue_watchdog(struct ifnet *ifp)
{
	struct mue_softc *sc = ifp->if_softc;
	struct mue_chain *c;
	usbd_status stat;
	int s;

	ifp->if_oerrors++;
	printf("%s: watchdog timeout\n", sc->mue_dev.dv_xname);

	s = splusb();
	c = &sc->mue_cdata.mue_tx_chain[0];
	usbd_get_xfer_status(c->mue_xfer, NULL, NULL, NULL, &stat);
	mue_txeof(c->mue_xfer, c, stat);

	if (!ifq_empty(&ifp->if_snd))
		mue_start(ifp);
	splx(s);
}

void
mue_reset(struct mue_softc *sc)
{
	if (usbd_is_dying(sc->mue_udev))
		return;

	/* Wait a little while for the chip to get its brains in order. */
	DELAY(1000);
}

void
mue_start(struct ifnet *ifp)
{
	struct mue_softc *sc = ifp->if_softc;
	struct mbuf *m_head = NULL;

	if (!sc->mue_link)
		return;

	if (ifq_is_oactive(&ifp->if_snd))
		return;

	m_head = ifq_dequeue(&ifp->if_snd);
	if (m_head == NULL)
		return;

	if (mue_encap(sc, m_head, 0)) {
		m_freem(m_head);
		ifq_set_oactive(&ifp->if_snd);
		return;
	}

	/* If there's a BPF listener, bounce a copy of this frame to him. */
#if NBPFILTER > 0
	if (ifp->if_bpf)
		bpf_mtap(ifp->if_bpf, m_head, BPF_DIRECTION_OUT);
#endif

	ifq_set_oactive(&ifp->if_snd);

	/* Set a timeout in case the chip goes out to lunch. */
	ifp->if_timer = 5;
}

void
mue_stop(struct mue_softc *sc)
{
	struct ifnet *ifp;
	usbd_status err;
	int i;

	ifp = GET_IFP(sc);
	ifp->if_timer = 0;
	ifp->if_flags &= ~IFF_RUNNING;
	ifq_clr_oactive(&ifp->if_snd);

	timeout_del(&sc->mue_stat_ch);

	/* Stop transfers. */
	if (sc->mue_ep[MUE_ENDPT_RX] != NULL) {
		err = usbd_close_pipe(sc->mue_ep[MUE_ENDPT_RX]);
		if (err) {
			printf("%s: close rx pipe failed: %s\n",
			    sc->mue_dev.dv_xname, usbd_errstr(err));
		}
		sc->mue_ep[MUE_ENDPT_RX] = NULL;
	}

	if (sc->mue_ep[MUE_ENDPT_TX] != NULL) {
		err = usbd_close_pipe(sc->mue_ep[MUE_ENDPT_TX]);
		if (err) {
			printf("%s: close tx pipe failed: %s\n",
			    sc->mue_dev.dv_xname, usbd_errstr(err));
		}
		sc->mue_ep[MUE_ENDPT_TX] = NULL;
	}

	if (sc->mue_ep[MUE_ENDPT_INTR] != NULL) {
		err = usbd_close_pipe(sc->mue_ep[MUE_ENDPT_INTR]);
		if (err) {
			printf("%s: close intr pipe failed: %s\n",
			    sc->mue_dev.dv_xname, usbd_errstr(err));
		}
		sc->mue_ep[MUE_ENDPT_INTR] = NULL;
	}

	/* Free RX resources. */
	for (i = 0; i < MUE_RX_LIST_CNT; i++) {
		if (sc->mue_cdata.mue_rx_chain[i].mue_mbuf != NULL) {
			m_freem(sc->mue_cdata.mue_rx_chain[i].mue_mbuf);
			sc->mue_cdata.mue_rx_chain[i].mue_mbuf = NULL;
		}
		if (sc->mue_cdata.mue_rx_chain[i].mue_xfer != NULL) {
			usbd_free_xfer(sc->mue_cdata.mue_rx_chain[i].mue_xfer);
			sc->mue_cdata.mue_rx_chain[i].mue_xfer = NULL;
		}
	}

	/* Free TX resources. */
	for (i = 0; i < MUE_TX_LIST_CNT; i++) {
		if (sc->mue_cdata.mue_tx_chain[i].mue_mbuf != NULL) {
			m_freem(sc->mue_cdata.mue_tx_chain[i].mue_mbuf);
			sc->mue_cdata.mue_tx_chain[i].mue_mbuf = NULL;
		}
		if (sc->mue_cdata.mue_tx_chain[i].mue_xfer != NULL) {
			usbd_free_xfer(sc->mue_cdata.mue_tx_chain[i].mue_xfer);
			sc->mue_cdata.mue_tx_chain[i].mue_xfer = NULL;
		}
	}

	sc->mue_link = 0;
}

void
mue_tick(void *xsc)
{
	struct mue_softc *sc = xsc;

	if (sc == NULL)
		return;

	if (usbd_is_dying(sc->mue_udev))
		return;

	/* Perform periodic stuff in process context. */
	usb_add_task(sc->mue_udev, &sc->mue_tick_task);
}

void
mue_tick_task(void *xsc)
{
	struct mue_softc *sc =xsc;
	struct mii_data *mii;
	int s;

	if (sc == NULL)
		return;

	if (usbd_is_dying(sc->mue_udev))
		return;

	mii = GET_MII(sc);

	s = splnet();
	mii_tick(mii);
	if (sc->mue_link == 0)
		mue_miibus_statchg(&sc->mue_dev);
	timeout_add_sec(&sc->mue_stat_ch, 1);
	splx(s);
}
