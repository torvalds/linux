/*	$OpenBSD: if_smsc.c,v 1.39 2024/05/23 03:21:08 jsg Exp $	*/
/* $FreeBSD: src/sys/dev/usb/net/if_smsc.c,v 1.1 2012/08/15 04:03:55 gonzo Exp $ */
/*-
 * Copyright (c) 2012
 *	Ben Gray <bgray@freebsd.org>.
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
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR ``AS IS'' AND ANY EXPRESS OR
 * IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES
 * OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED.
 * IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR ANY DIRECT, INDIRECT,
 * INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT
 * NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 * DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
 * THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF
 * THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

/*
 * SMSC LAN9xxx devices (http://www.smsc.com/)
 * 
 * The LAN9500 & LAN9500A devices are stand-alone USB to Ethernet chips that
 * support USB 2.0 and 10/100 Mbps Ethernet.
 *
 * The LAN951x devices are an integrated USB hub and USB to Ethernet adapter.
 * The driver only covers the Ethernet part, the standard USB hub driver
 * supports the hub part.
 *
 * This driver is closely modelled on the Linux driver written and copyrighted
 * by SMSC.
 *
 * H/W TCP & UDP Checksum Offloading
 * ---------------------------------
 * The chip supports both tx and rx offloading of UDP & TCP checksums, this
 * feature can be dynamically enabled/disabled.  
 *
 * RX checksumming is performed across bytes after the IPv4 header to the end of
 * the Ethernet frame, this means if the frame is padded with non-zero values
 * the H/W checksum will be incorrect, however the rx code compensates for this.
 *
 * TX checksumming is more complicated, the device requires a special header to
 * be prefixed onto the start of the frame which indicates the start and end
 * positions of the UDP or TCP frame.  This requires the driver to manually
 * go through the packet data and decode the headers prior to sending.
 * On Linux they generally provide cues to the location of the csum and the
 * area to calculate it over, on FreeBSD we seem to have to do it all ourselves,
 * hence this is not as optimal and therefore h/w tX checksum is currently not
 * implemented.
 */

#include "bpfilter.h"

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/sockio.h>
#include <sys/rwlock.h>
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

#include <dev/mii/miivar.h>

#include <dev/usb/usb.h>
#include <dev/usb/usbdi.h>
#include <dev/usb/usbdi_util.h>
#include <dev/usb/usbdivar.h>
#include <dev/usb/usbdevs.h>

#include "if_smscreg.h"

/*
 * Various supported device vendors/products.
 */
static const struct usb_devno smsc_devs[] = {
	{ USB_VENDOR_SMC2,	USB_PRODUCT_SMC2_LAN89530 },
	{ USB_VENDOR_SMC2,	USB_PRODUCT_SMC2_LAN9530 },
	{ USB_VENDOR_SMC2,	USB_PRODUCT_SMC2_LAN9730 },
	{ USB_VENDOR_SMC2,	USB_PRODUCT_SMC2_SMSC9500 },
	{ USB_VENDOR_SMC2,	USB_PRODUCT_SMC2_SMSC9500A },
	{ USB_VENDOR_SMC2,	USB_PRODUCT_SMC2_SMSC9500A_ALT },
	{ USB_VENDOR_SMC2,	USB_PRODUCT_SMC2_SMSC9500A_HAL },
	{ USB_VENDOR_SMC2,	USB_PRODUCT_SMC2_SMSC9500A_SAL10 },
	{ USB_VENDOR_SMC2,	USB_PRODUCT_SMC2_SMSC9500_ALT },
	{ USB_VENDOR_SMC2,	USB_PRODUCT_SMC2_SMSC9500_SAL10 },
	{ USB_VENDOR_SMC2,	USB_PRODUCT_SMC2_SMSC9505 },
	{ USB_VENDOR_SMC2,	USB_PRODUCT_SMC2_SMSC9505A },
	{ USB_VENDOR_SMC2,	USB_PRODUCT_SMC2_SMSC9505A_HAL },
	{ USB_VENDOR_SMC2,	USB_PRODUCT_SMC2_SMSC9505A_SAL10 },
	{ USB_VENDOR_SMC2,	USB_PRODUCT_SMC2_SMSC9505_SAL10 },
	{ USB_VENDOR_SMC2,	USB_PRODUCT_SMC2_SMSC9512_14 },
	{ USB_VENDOR_SMC2,	USB_PRODUCT_SMC2_SMSC9512_14_ALT },
	{ USB_VENDOR_SMC2,	USB_PRODUCT_SMC2_SMSC9512_14_SAL10 }
};

#ifdef SMSC_DEBUG
static int smsc_debug = 0;
#define smsc_dbg_printf(sc, fmt, args...) \
	do { \
		if (smsc_debug > 0) \
			printf("debug: " fmt, ##args); \
	} while(0)
#else
#define smsc_dbg_printf(sc, fmt, args...)
#endif

#define smsc_warn_printf(sc, fmt, args...) \
	printf("%s: warning: " fmt, (sc)->sc_dev.dv_xname, ##args)

#define smsc_err_printf(sc, fmt, args...) \
	printf("%s: error: " fmt, (sc)->sc_dev.dv_xname, ##args)

int		 smsc_chip_init(struct smsc_softc *sc);
int		 smsc_ioctl(struct ifnet *ifp, u_long cmd, caddr_t data);
void		 smsc_iff(struct smsc_softc *);
int		 smsc_setmacaddress(struct smsc_softc *, const uint8_t *);

int		 smsc_match(struct device *, void *, void *);
void		 smsc_attach(struct device *, struct device *, void *);
int		 smsc_detach(struct device *, int);

void		 smsc_init(void *);
void		 smsc_stop(struct smsc_softc *);
void		 smsc_start(struct ifnet *);
void		 smsc_reset(struct smsc_softc *);

void		 smsc_tick(void *);
void		 smsc_tick_task(void *);
void		 smsc_miibus_statchg(struct device *);
int		 smsc_miibus_readreg(struct device *, int, int);
void		 smsc_miibus_writereg(struct device *, int, int, int);
int		 smsc_ifmedia_upd(struct ifnet *);
void		 smsc_ifmedia_sts(struct ifnet *, struct ifmediareq *);
void		 smsc_lock_mii(struct smsc_softc *sc);
void		 smsc_unlock_mii(struct smsc_softc *sc);

int		 smsc_tx_list_init(struct smsc_softc *);
int		 smsc_rx_list_init(struct smsc_softc *);
int		 smsc_encap(struct smsc_softc *, struct mbuf *, int);
void		 smsc_rxeof(struct usbd_xfer *, void *, usbd_status);
void		 smsc_txeof(struct usbd_xfer *, void *, usbd_status);

int		 smsc_read_reg(struct smsc_softc *, uint32_t, uint32_t *);
int		 smsc_write_reg(struct smsc_softc *, uint32_t, uint32_t);
int		 smsc_wait_for_bits(struct smsc_softc *, uint32_t, uint32_t);
int		 smsc_sethwcsum(struct smsc_softc *);

struct cfdriver smsc_cd = {
	NULL, "smsc", DV_IFNET
};

const struct cfattach smsc_ca = {
	sizeof(struct smsc_softc), smsc_match, smsc_attach, smsc_detach,
};

#if defined(__arm__) || defined(__arm64__)

#include <dev/ofw/openfirm.h>

void
smsc_enaddr_OF(struct smsc_softc *sc)
{
	char *device = "/axi/usb/hub/ethernet";
	char prop[128];
	int node;

	if (sc->sc_dev.dv_unit != 0)
		return;

	/*
	 * Get the Raspberry Pi MAC address from FDT.  This is all
	 * much more complicated than strictly needed since the
	 * firmware device tree keeps changing as drivers get
	 * upstreamed.  Sigh.
	 * 
	 * Ultimately this should just use the "ethernet0" alias and
	 * the "local-mac-address" property.
	 */

	if ((node = OF_finddevice("/aliases")) == -1)
		return;
	if (OF_getprop(node, "ethernet0", prop, sizeof(prop)) > 0 ||
	    OF_getprop(node, "ethernet", prop, sizeof(prop)) > 0)
		device = prop;

	if ((node = OF_finddevice(device)) == -1)
		return;
	if (OF_getprop(node, "local-mac-address", sc->sc_ac.ac_enaddr,
	    sizeof(sc->sc_ac.ac_enaddr)) != sizeof(sc->sc_ac.ac_enaddr)) {
		OF_getprop(node, "mac-address", sc->sc_ac.ac_enaddr,
		    sizeof(sc->sc_ac.ac_enaddr));
	}
}
#else
#define smsc_enaddr_OF(x) do {} while(0)
#endif

int
smsc_read_reg(struct smsc_softc *sc, uint32_t off, uint32_t *data)
{
	usb_device_request_t req;
	uint32_t buf;
	usbd_status err;

	req.bmRequestType = UT_READ_VENDOR_DEVICE;
	req.bRequest = SMSC_UR_READ_REG;
	USETW(req.wValue, 0);
	USETW(req.wIndex, off);
	USETW(req.wLength, 4);

	err = usbd_do_request(sc->sc_udev, &req, &buf);
	if (err != 0)
		smsc_warn_printf(sc, "Failed to read register 0x%0x\n", off);

	*data = letoh32(buf);
	
	return (err);
}

int
smsc_write_reg(struct smsc_softc *sc, uint32_t off, uint32_t data)
{
	usb_device_request_t req;
	uint32_t buf;
	usbd_status err;

	buf = htole32(data);

	req.bmRequestType = UT_WRITE_VENDOR_DEVICE;
	req.bRequest = SMSC_UR_WRITE_REG;
	USETW(req.wValue, 0);
	USETW(req.wIndex, off);
	USETW(req.wLength, 4);

	err = usbd_do_request(sc->sc_udev, &req, &buf);
	if (err != 0)
		smsc_warn_printf(sc, "Failed to write register 0x%0x\n", off);

	return (err);
}

int
smsc_wait_for_bits(struct smsc_softc *sc, uint32_t reg, uint32_t bits)
{
	uint32_t val;
	int err, i;

	for (i = 0; i < 100; i++) {
		if ((err = smsc_read_reg(sc, reg, &val)) != 0)
			return (err);
		if (!(val & bits))
			return (0);
		DELAY(5);
	}

	return (1);
}

int
smsc_miibus_readreg(struct device *dev, int phy, int reg)
{
	struct smsc_softc *sc = (struct smsc_softc *)dev;
	uint32_t addr;
	uint32_t val = 0;

	smsc_lock_mii(sc);
	if (smsc_wait_for_bits(sc, SMSC_MII_ADDR, SMSC_MII_BUSY) != 0) {
		smsc_warn_printf(sc, "MII is busy\n");
		goto done;
	}

	addr = (phy << 11) | (reg << 6) | SMSC_MII_READ;
	smsc_write_reg(sc, SMSC_MII_ADDR, addr);

	if (smsc_wait_for_bits(sc, SMSC_MII_ADDR, SMSC_MII_BUSY) != 0)
		smsc_warn_printf(sc, "MII read timeout\n");

	smsc_read_reg(sc, SMSC_MII_DATA, &val);

done:
	smsc_unlock_mii(sc);
	return (val & 0xFFFF);
}

void
smsc_miibus_writereg(struct device *dev, int phy, int reg, int val)
{
	struct smsc_softc *sc = (struct smsc_softc *)dev;
	uint32_t addr;

	if (sc->sc_phyno != phy)
		return;

	smsc_lock_mii(sc);
	if (smsc_wait_for_bits(sc, SMSC_MII_ADDR, SMSC_MII_BUSY) != 0) {
		smsc_warn_printf(sc, "MII is busy\n");
		smsc_unlock_mii(sc);
		return;
	}

	smsc_write_reg(sc, SMSC_MII_DATA, val);

	addr = (phy << 11) | (reg << 6) | SMSC_MII_WRITE;
	smsc_write_reg(sc, SMSC_MII_ADDR, addr);
	smsc_unlock_mii(sc);

	if (smsc_wait_for_bits(sc, SMSC_MII_ADDR, SMSC_MII_BUSY) != 0)
		smsc_warn_printf(sc, "MII write timeout\n");
}

void
smsc_miibus_statchg(struct device *dev)
{
	struct smsc_softc *sc = (struct smsc_softc *)dev;
	struct mii_data *mii = &sc->sc_mii;
	struct ifnet *ifp = &sc->sc_ac.ac_if;
	int err;
	uint32_t flow;
	uint32_t afc_cfg;

	if (mii == NULL || ifp == NULL ||
	    (ifp->if_flags & IFF_RUNNING) == 0)
		return;

	/* Use the MII status to determine link status */
	sc->sc_flags &= ~SMSC_FLAG_LINK;
	if ((mii->mii_media_status & (IFM_ACTIVE | IFM_AVALID)) ==
	    (IFM_ACTIVE | IFM_AVALID)) {
		switch (IFM_SUBTYPE(mii->mii_media_active)) {
			case IFM_10_T:
			case IFM_100_TX:
				sc->sc_flags |= SMSC_FLAG_LINK;
				break;
			case IFM_1000_T:
				/* Gigabit ethernet not supported by chipset */
				break;
			default:
				break;
		}
	}

	/* Lost link, do nothing. */
	if ((sc->sc_flags & SMSC_FLAG_LINK) == 0) {
		smsc_dbg_printf(sc, "link flag not set\n");
		return;
	}
	
	err = smsc_read_reg(sc, SMSC_AFC_CFG, &afc_cfg);
	if (err) {
		smsc_warn_printf(sc, "failed to read initial AFC_CFG, "
		    "error %d\n", err);
		return;
	}
	
	/* Enable/disable full duplex operation and TX/RX pause */
	if ((IFM_OPTIONS(mii->mii_media_active) & IFM_FDX) != 0) {
		smsc_dbg_printf(sc, "full duplex operation\n");
		sc->sc_mac_csr &= ~SMSC_MAC_CSR_RCVOWN;
		sc->sc_mac_csr |= SMSC_MAC_CSR_FDPX;

		if ((IFM_OPTIONS(mii->mii_media_active) & IFM_ETH_RXPAUSE) != 0)
			flow = 0xffff0002;
		else
			flow = 0;
			
		if ((IFM_OPTIONS(mii->mii_media_active) & IFM_ETH_TXPAUSE) != 0)
			afc_cfg |= 0xf;
		else
			afc_cfg &= ~0xf;
		
	} else {
		smsc_dbg_printf(sc, "half duplex operation\n");
		sc->sc_mac_csr &= ~SMSC_MAC_CSR_FDPX;
		sc->sc_mac_csr |= SMSC_MAC_CSR_RCVOWN;
		
		flow = 0;
		afc_cfg |= 0xf;
	}

	err = smsc_write_reg(sc, SMSC_MAC_CSR, sc->sc_mac_csr);
	err += smsc_write_reg(sc, SMSC_FLOW, flow);
	err += smsc_write_reg(sc, SMSC_AFC_CFG, afc_cfg);
	if (err)
		smsc_warn_printf(sc, "media change failed, error %d\n", err);
}

int
smsc_ifmedia_upd(struct ifnet *ifp)
{
	struct smsc_softc *sc = ifp->if_softc;
	struct mii_data *mii = &sc->sc_mii;
	int err;

	if (mii->mii_instance) {
		struct mii_softc *miisc;

		LIST_FOREACH(miisc, &mii->mii_phys, mii_list)
			mii_phy_reset(miisc);
	}
	err = mii_mediachg(mii);
	return (err);
}

void
smsc_ifmedia_sts(struct ifnet *ifp, struct ifmediareq *ifmr)
{
	struct smsc_softc *sc = ifp->if_softc;
	struct mii_data *mii = &sc->sc_mii;

	mii_pollstat(mii);
	
	ifmr->ifm_active = mii->mii_media_active;
	ifmr->ifm_status = mii->mii_media_status;
}

static inline uint32_t
smsc_hash(uint8_t addr[ETHER_ADDR_LEN])
{
	return (ether_crc32_be(addr, ETHER_ADDR_LEN) >> 26) & 0x3f;
}

void
smsc_iff(struct smsc_softc *sc)
{
	struct ifnet		*ifp = &sc->sc_ac.ac_if;
	struct arpcom		*ac = &sc->sc_ac;
	struct ether_multi	*enm;
	struct ether_multistep	 step;
	uint32_t		 hashtbl[2] = { 0, 0 };
	uint32_t		 hash;

	if (usbd_is_dying(sc->sc_udev))
		return;

	sc->sc_mac_csr &= ~(SMSC_MAC_CSR_HPFILT | SMSC_MAC_CSR_MCPAS |
	    SMSC_MAC_CSR_PRMS);
	ifp->if_flags &= ~IFF_ALLMULTI;

	if (ifp->if_flags & IFF_PROMISC || ac->ac_multirangecnt > 0) {
		ifp->if_flags |= IFF_ALLMULTI;
		sc->sc_mac_csr |= SMSC_MAC_CSR_MCPAS;
		if (ifp->if_flags & IFF_PROMISC)
			sc->sc_mac_csr |= SMSC_MAC_CSR_PRMS;
	} else {
		sc->sc_mac_csr |= SMSC_MAC_CSR_HPFILT;

		ETHER_FIRST_MULTI(step, ac, enm);
		while (enm != NULL) {
			hash = smsc_hash(enm->enm_addrlo);

			hashtbl[hash >> 5] |= 1 << (hash & 0x1F);

			ETHER_NEXT_MULTI(step, enm);
		}
	}

	/* Debug */
	if (sc->sc_mac_csr & SMSC_MAC_CSR_MCPAS)
		smsc_dbg_printf(sc, "receive all multicast enabled\n");
	else if (sc->sc_mac_csr & SMSC_MAC_CSR_HPFILT)
		smsc_dbg_printf(sc, "receive select group of macs\n");

	/* Write the hash table and mac control registers */
	smsc_write_reg(sc, SMSC_HASHH, hashtbl[1]);
	smsc_write_reg(sc, SMSC_HASHL, hashtbl[0]);
	smsc_write_reg(sc, SMSC_MAC_CSR, sc->sc_mac_csr);
}

int
smsc_sethwcsum(struct smsc_softc *sc)
{
	struct ifnet *ifp = &sc->sc_ac.ac_if;
	uint32_t val;
	int err;

	if (!ifp)
		return (-EIO);

	err = smsc_read_reg(sc, SMSC_COE_CTRL, &val);
	if (err != 0) {
		smsc_warn_printf(sc, "failed to read SMSC_COE_CTRL (err=%d)\n",
		    err);
		return (err);
	}

	/* Enable/disable the Rx checksum */
	if (ifp->if_capabilities & IFCAP_CSUM_IPv4)
		val |= SMSC_COE_CTRL_RX_EN;
	else
		val &= ~SMSC_COE_CTRL_RX_EN;

	/* Enable/disable the Tx checksum (currently not supported) */
	if (ifp->if_capabilities & IFCAP_CSUM_IPv4)
		val |= SMSC_COE_CTRL_TX_EN;
	else
		val &= ~SMSC_COE_CTRL_TX_EN;

	err = smsc_write_reg(sc, SMSC_COE_CTRL, val);
	if (err != 0) {
		smsc_warn_printf(sc, "failed to write SMSC_COE_CTRL (err=%d)\n",
		    err);
		return (err);
	}

	return (0);
}

int
smsc_setmacaddress(struct smsc_softc *sc, const uint8_t *addr)
{
	int err;
	uint32_t val;

	smsc_dbg_printf(sc, "setting mac address to "
	    "%02x:%02x:%02x:%02x:%02x:%02x\n",
	    addr[0], addr[1], addr[2], addr[3], addr[4], addr[5]);

	val = (addr[3] << 24) | (addr[2] << 16) | (addr[1] << 8) | addr[0];
	if ((err = smsc_write_reg(sc, SMSC_MAC_ADDRL, val)) != 0)
		goto done;
		
	val = (addr[5] << 8) | addr[4];
	err = smsc_write_reg(sc, SMSC_MAC_ADDRH, val);
	
done:
	return (err);
}

void
smsc_reset(struct smsc_softc *sc)
{
	if (usbd_is_dying(sc->sc_udev))
		return;

	/* Wait a little while for the chip to get its brains in order. */
	DELAY(1000);

	/* Reinitialize controller to achieve full reset. */
	smsc_chip_init(sc);
}

void
smsc_init(void *xsc)
{
	struct smsc_softc	*sc = xsc;
	struct ifnet		*ifp = &sc->sc_ac.ac_if;
	struct smsc_chain	*c;
	usbd_status		 err;
	int			 s, i;
	
	s = splnet();

	/* Cancel pending I/O */
	smsc_stop(sc);

	/* Reset the ethernet interface. */
	smsc_reset(sc);

	/* Init RX ring. */
	if (smsc_rx_list_init(sc) == ENOBUFS) {
		printf("%s: rx list init failed\n", sc->sc_dev.dv_xname);
		splx(s);
		return;
	}

	/* Init TX ring. */
	if (smsc_tx_list_init(sc) == ENOBUFS) {
		printf("%s: tx list init failed\n", sc->sc_dev.dv_xname);
		splx(s);
		return;
	}

	/* Program promiscuous mode and multicast filters. */
	smsc_iff(sc);

	/* Open RX and TX pipes. */
	err = usbd_open_pipe(sc->sc_iface, sc->sc_ed[SMSC_ENDPT_RX],
	    USBD_EXCLUSIVE_USE, &sc->sc_ep[SMSC_ENDPT_RX]);
	if (err) {
		printf("%s: open rx pipe failed: %s\n",
		    sc->sc_dev.dv_xname, usbd_errstr(err));
		splx(s);
		return;
	}

	err = usbd_open_pipe(sc->sc_iface, sc->sc_ed[SMSC_ENDPT_TX],
	    USBD_EXCLUSIVE_USE, &sc->sc_ep[SMSC_ENDPT_TX]);
	if (err) {
		printf("%s: open tx pipe failed: %s\n",
		    sc->sc_dev.dv_xname, usbd_errstr(err));
		splx(s);
		return;
	}

	/* Start up the receive pipe. */
	for (i = 0; i < SMSC_RX_LIST_CNT; i++) {
		c = &sc->sc_cdata.rx_chain[i];
		usbd_setup_xfer(c->sc_xfer, sc->sc_ep[SMSC_ENDPT_RX],
		    c, c->sc_buf, sc->sc_bufsz,
		    USBD_SHORT_XFER_OK | USBD_NO_COPY,
		    USBD_NO_TIMEOUT, smsc_rxeof);
		usbd_transfer(c->sc_xfer);
	}

	/* TCP/UDP checksum offload engines. */
	smsc_sethwcsum(sc);

	/* Indicate we are up and running. */
	ifp->if_flags |= IFF_RUNNING;
	ifq_clr_oactive(&ifp->if_snd);

	timeout_add_sec(&sc->sc_stat_ch, 1);

	splx(s);
}

void
smsc_start(struct ifnet *ifp)
{
	struct smsc_softc	*sc = ifp->if_softc;
	struct mbuf		*m_head = NULL;

	/* Don't send anything if there is no link or controller is busy. */
	if ((sc->sc_flags & SMSC_FLAG_LINK) == 0 ||
		ifq_is_oactive(&ifp->if_snd)) {
		return;
	}

	m_head = ifq_dequeue(&ifp->if_snd);
	if (m_head == NULL)
		return;

	if (smsc_encap(sc, m_head, 0)) {
		m_freem(m_head);
		ifq_set_oactive(&ifp->if_snd);
		return;
	}

#if NBPFILTER > 0
	if (ifp->if_bpf)
		bpf_mtap(ifp->if_bpf, m_head, BPF_DIRECTION_OUT);
#endif
	ifq_set_oactive(&ifp->if_snd);
}

void
smsc_tick(void *xsc)
{
	struct smsc_softc *sc = xsc;

	if (sc == NULL)
		return;

	if (usbd_is_dying(sc->sc_udev))
		return;

	usb_add_task(sc->sc_udev, &sc->sc_tick_task);
}

void
smsc_stop(struct smsc_softc *sc)
{
	usbd_status		err;
	struct ifnet		*ifp;
	int			i;

	smsc_reset(sc);

	ifp = &sc->sc_ac.ac_if;
	ifp->if_timer = 0;
	ifp->if_flags &= ~IFF_RUNNING;
	ifq_clr_oactive(&ifp->if_snd);

	timeout_del(&sc->sc_stat_ch);

	/* Stop transfers. */
	if (sc->sc_ep[SMSC_ENDPT_RX] != NULL) {
		err = usbd_close_pipe(sc->sc_ep[SMSC_ENDPT_RX]);
		if (err) {
			printf("%s: close rx pipe failed: %s\n",
			    sc->sc_dev.dv_xname, usbd_errstr(err));
		}
		sc->sc_ep[SMSC_ENDPT_RX] = NULL;
	}

	if (sc->sc_ep[SMSC_ENDPT_TX] != NULL) {
		err = usbd_close_pipe(sc->sc_ep[SMSC_ENDPT_TX]);
		if (err) {
			printf("%s: close tx pipe failed: %s\n",
			    sc->sc_dev.dv_xname, usbd_errstr(err));
		}
		sc->sc_ep[SMSC_ENDPT_TX] = NULL;
	}

	if (sc->sc_ep[SMSC_ENDPT_INTR] != NULL) {
		err = usbd_close_pipe(sc->sc_ep[SMSC_ENDPT_INTR]);
		if (err) {
			printf("%s: close intr pipe failed: %s\n",
			    sc->sc_dev.dv_xname, usbd_errstr(err));
		}
		sc->sc_ep[SMSC_ENDPT_INTR] = NULL;
	}

	/* Free RX resources. */
	for (i = 0; i < SMSC_RX_LIST_CNT; i++) {
		if (sc->sc_cdata.rx_chain[i].sc_mbuf != NULL) {
			m_freem(sc->sc_cdata.rx_chain[i].sc_mbuf);
			sc->sc_cdata.rx_chain[i].sc_mbuf = NULL;
		}
		if (sc->sc_cdata.rx_chain[i].sc_xfer != NULL) {
			usbd_free_xfer(sc->sc_cdata.rx_chain[i].sc_xfer);
			sc->sc_cdata.rx_chain[i].sc_xfer = NULL;
		}
	}

	/* Free TX resources. */
	for (i = 0; i < SMSC_TX_LIST_CNT; i++) {
		if (sc->sc_cdata.tx_chain[i].sc_mbuf != NULL) {
			m_freem(sc->sc_cdata.tx_chain[i].sc_mbuf);
			sc->sc_cdata.tx_chain[i].sc_mbuf = NULL;
		}
		if (sc->sc_cdata.tx_chain[i].sc_xfer != NULL) {
			usbd_free_xfer(sc->sc_cdata.tx_chain[i].sc_xfer);
			sc->sc_cdata.tx_chain[i].sc_xfer = NULL;
		}
	}
}

int
smsc_chip_init(struct smsc_softc *sc)
{
	int err;
	uint32_t reg_val;
	int burst_cap;

	/* Enter H/W config mode */
	smsc_write_reg(sc, SMSC_HW_CFG, SMSC_HW_CFG_LRST);

	if ((err = smsc_wait_for_bits(sc, SMSC_HW_CFG,
	    SMSC_HW_CFG_LRST)) != 0) {
		smsc_warn_printf(sc, "timed-out waiting for reset to "
		    "complete\n");
		goto init_failed;
	}

	/* Reset the PHY */
	smsc_write_reg(sc, SMSC_PM_CTRL, SMSC_PM_CTRL_PHY_RST);

	if ((err = smsc_wait_for_bits(sc, SMSC_PM_CTRL,
	    SMSC_PM_CTRL_PHY_RST)) != 0) {
		smsc_warn_printf(sc, "timed-out waiting for phy reset to "
		    "complete\n");
		goto init_failed;
	}
	usbd_delay_ms(sc->sc_udev, 40);

	/* Set the mac address */
	if ((err = smsc_setmacaddress(sc, sc->sc_ac.ac_enaddr)) != 0) {
		smsc_warn_printf(sc, "failed to set the MAC address\n");
		goto init_failed;
	}

	/*
	 * Don't know what the HW_CFG_BIR bit is, but following the reset
	 * sequence as used in the Linux driver.
	 */
	if ((err = smsc_read_reg(sc, SMSC_HW_CFG, &reg_val)) != 0) {
		smsc_warn_printf(sc, "failed to read HW_CFG: %d\n", err);
		goto init_failed;
	}
	reg_val |= SMSC_HW_CFG_BIR;
	smsc_write_reg(sc, SMSC_HW_CFG, reg_val);

	/*
	 * There is a so called 'turbo mode' that the linux driver supports, it
	 * seems to allow you to jam multiple frames per Rx transaction.
	 * By default this driver supports that and therefore allows multiple
	 * frames per URB.
	 *
	 * The xfer buffer size needs to reflect this as well, therefore based
	 * on the calculations in the Linux driver the RX bufsize is set to
	 * 18944,
	 *     bufsz = (16 * 1024 + 5 * 512)
	 *
	 * Burst capability is the number of URBs that can be in a burst of
	 * data/ethernet frames.
	 */
#ifdef SMSC_TURBO
	if (sc->sc_udev->speed == USB_SPEED_HIGH)
		burst_cap = 37;
	else
		burst_cap = 128;
#else
	burst_cap = 0;
#endif

	smsc_write_reg(sc, SMSC_BURST_CAP, burst_cap);

	/* Set the default bulk in delay (magic value from Linux driver) */
	smsc_write_reg(sc, SMSC_BULK_IN_DLY, 0x00002000);



	/*
	 * Initialise the RX interface
	 */
	if ((err = smsc_read_reg(sc, SMSC_HW_CFG, &reg_val)) < 0) {
		smsc_warn_printf(sc, "failed to read HW_CFG: (err = %d)\n",
		    err);
		goto init_failed;
	}

	/*
	 * The following settings are used for 'turbo mode', a.k.a multiple
	 * frames per Rx transaction (again info taken form Linux driver).
	 */
#ifdef SMSC_TURBO
	reg_val |= (SMSC_HW_CFG_MEF | SMSC_HW_CFG_BCE);
#endif

	smsc_write_reg(sc, SMSC_HW_CFG, reg_val);

	/* Clear the status register ? */
	smsc_write_reg(sc, SMSC_INTR_STATUS, 0xffffffff);

	/* Read and display the revision register */
	if ((err = smsc_read_reg(sc, SMSC_ID_REV, &sc->sc_rev_id)) < 0) {
		smsc_warn_printf(sc, "failed to read ID_REV (err = %d)\n", err);
		goto init_failed;
	}

	/* GPIO/LED setup */
	reg_val = SMSC_LED_GPIO_CFG_SPD_LED | SMSC_LED_GPIO_CFG_LNK_LED | 
	          SMSC_LED_GPIO_CFG_FDX_LED;
	smsc_write_reg(sc, SMSC_LED_GPIO_CFG, reg_val);

	/*
	 * Initialise the TX interface
	 */
	smsc_write_reg(sc, SMSC_FLOW, 0);

	smsc_write_reg(sc, SMSC_AFC_CFG, AFC_CFG_DEFAULT);

	/* Read the current MAC configuration */
	if ((err = smsc_read_reg(sc, SMSC_MAC_CSR, &sc->sc_mac_csr)) < 0) {
		smsc_warn_printf(sc, "failed to read MAC_CSR (err=%d)\n", err);
		goto init_failed;
	}
	
	/* Vlan */
	smsc_write_reg(sc, SMSC_VLAN1, (uint32_t)ETHERTYPE_VLAN);

	/*
	 * Start TX
	 */
	sc->sc_mac_csr |= SMSC_MAC_CSR_TXEN;
	smsc_write_reg(sc, SMSC_MAC_CSR, sc->sc_mac_csr);
	smsc_write_reg(sc, SMSC_TX_CFG, SMSC_TX_CFG_ON);

	/*
	 * Start RX
	 */
	sc->sc_mac_csr |= SMSC_MAC_CSR_RXEN;
	smsc_write_reg(sc, SMSC_MAC_CSR, sc->sc_mac_csr);

	return (0);
	
init_failed:
	smsc_err_printf(sc, "smsc_chip_init failed (err=%d)\n", err);
	return (err);
}

int
smsc_ioctl(struct ifnet *ifp, u_long cmd, caddr_t data)
{
	struct smsc_softc	*sc = ifp->if_softc;
	struct ifreq		*ifr = (struct ifreq *)data;
	int			s, error = 0;

	s = splnet();

	switch(cmd) {
	case SIOCSIFADDR:
		ifp->if_flags |= IFF_UP;
		if (!(ifp->if_flags & IFF_RUNNING))
			smsc_init(sc);
		break;

	case SIOCSIFFLAGS:
		if (ifp->if_flags & IFF_UP) {
			if (ifp->if_flags & IFF_RUNNING)
				error = ENETRESET;
			else
				smsc_init(sc);
		} else {
			if (ifp->if_flags & IFF_RUNNING)
				smsc_stop(sc);
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
			smsc_iff(sc);
		error = 0;
	}

	splx(s);
	return(error);
}

int
smsc_match(struct device *parent, void *match, void *aux)
{
	struct usb_attach_arg *uaa = aux;

	if (uaa->iface == NULL || uaa->configno != 1)
		return UMATCH_NONE;

	return (usb_lookup(smsc_devs, uaa->vendor, uaa->product) != NULL) ?
	    UMATCH_VENDOR_PRODUCT_CONF_IFACE : UMATCH_NONE;
}

void
smsc_attach(struct device *parent, struct device *self, void *aux)
{
	struct smsc_softc *sc = (struct smsc_softc *)self;
	struct usb_attach_arg *uaa = aux;
	usb_interface_descriptor_t *id;
	usb_endpoint_descriptor_t *ed;
	struct mii_data *mii;
	struct ifnet *ifp;
	uint32_t mac_h, mac_l;
	int s, i;

	sc->sc_udev = uaa->device;
	sc->sc_iface = uaa->iface;

	/* Setup the endpoints for the SMSC LAN95xx device(s) */
	usb_init_task(&sc->sc_tick_task, smsc_tick_task, sc,
	    USB_TASK_TYPE_GENERIC);
	rw_init(&sc->sc_mii_lock, "smscmii");
	usb_init_task(&sc->sc_stop_task, (void (*)(void *))smsc_stop, sc,
	    USB_TASK_TYPE_GENERIC);

	id = usbd_get_interface_descriptor(sc->sc_iface);

	if (sc->sc_udev->speed >= USB_SPEED_HIGH)
		sc->sc_bufsz = SMSC_MAX_BUFSZ;
	else
		sc->sc_bufsz = SMSC_MIN_BUFSZ;

	/* Find endpoints. */
	for (i = 0; i < id->bNumEndpoints; i++) {
		ed = usbd_interface2endpoint_descriptor(sc->sc_iface, i);
		if (!ed) {
			printf("%s: couldn't get ep %d\n",
			    sc->sc_dev.dv_xname, i);
			return;
		}
		if (UE_GET_DIR(ed->bEndpointAddress) == UE_DIR_IN &&
		    UE_GET_XFERTYPE(ed->bmAttributes) == UE_BULK) {
			sc->sc_ed[SMSC_ENDPT_RX] = ed->bEndpointAddress;
		} else if (UE_GET_DIR(ed->bEndpointAddress) == UE_DIR_OUT &&
			   UE_GET_XFERTYPE(ed->bmAttributes) == UE_BULK) {
			sc->sc_ed[SMSC_ENDPT_TX] = ed->bEndpointAddress;
		} else if (UE_GET_DIR(ed->bEndpointAddress) == UE_DIR_IN &&
			   UE_GET_XFERTYPE(ed->bmAttributes) == UE_INTERRUPT) {
			sc->sc_ed[SMSC_ENDPT_INTR] = ed->bEndpointAddress;
		}
	}

	s = splnet();

	ifp = &sc->sc_ac.ac_if;
	ifp->if_softc = sc;
	strlcpy(ifp->if_xname, sc->sc_dev.dv_xname, IFNAMSIZ);
	ifp->if_flags = IFF_BROADCAST | IFF_SIMPLEX | IFF_MULTICAST;
	ifp->if_ioctl = smsc_ioctl;
	ifp->if_start = smsc_start;
	ifp->if_capabilities = IFCAP_VLAN_MTU;

	/* Setup some of the basics */
	sc->sc_phyno = 1;

	/*
	 * Attempt to get the mac address, if an EEPROM is not attached this
	 * will just return FF:FF:FF:FF:FF:FF, so in such cases we invent a MAC
	 * address based on urandom.
	 */
	memset(sc->sc_ac.ac_enaddr, 0xff, ETHER_ADDR_LEN);
	
	/* Check if there is already a MAC address in the register */
	if ((smsc_read_reg(sc, SMSC_MAC_ADDRL, &mac_l) == 0) &&
	    (smsc_read_reg(sc, SMSC_MAC_ADDRH, &mac_h) == 0)) {
		sc->sc_ac.ac_enaddr[5] = (uint8_t)((mac_h >> 8) & 0xff);
		sc->sc_ac.ac_enaddr[4] = (uint8_t)((mac_h) & 0xff);
		sc->sc_ac.ac_enaddr[3] = (uint8_t)((mac_l >> 24) & 0xff);
		sc->sc_ac.ac_enaddr[2] = (uint8_t)((mac_l >> 16) & 0xff);
		sc->sc_ac.ac_enaddr[1] = (uint8_t)((mac_l >> 8) & 0xff);
		sc->sc_ac.ac_enaddr[0] = (uint8_t)((mac_l) & 0xff);
	}

	smsc_enaddr_OF(sc);
	
	printf("%s: address %s\n", sc->sc_dev.dv_xname,
	    ether_sprintf(sc->sc_ac.ac_enaddr));
	
	/* Initialise the chip for the first time */
	smsc_chip_init(sc);

	/* Initialize MII/media info. */
	mii = &sc->sc_mii;
	mii->mii_ifp = ifp;
	mii->mii_readreg = smsc_miibus_readreg;
	mii->mii_writereg = smsc_miibus_writereg;
	mii->mii_statchg = smsc_miibus_statchg;
	mii->mii_flags = MIIF_AUTOTSLEEP;

	ifmedia_init(&mii->mii_media, 0, smsc_ifmedia_upd, smsc_ifmedia_sts);
	mii_attach(self, mii, 0xffffffff, MII_PHY_ANY, MII_OFFSET_ANY, 0);

	if (LIST_FIRST(&mii->mii_phys) == NULL) {
		ifmedia_add(&mii->mii_media, IFM_ETHER | IFM_NONE, 0, NULL);
		ifmedia_set(&mii->mii_media, IFM_ETHER | IFM_NONE);
	} else
		ifmedia_set(&mii->mii_media, IFM_ETHER | IFM_AUTO);

	if_attach(ifp);
	ether_ifattach(ifp);

	timeout_set(&sc->sc_stat_ch, smsc_tick, sc);

	splx(s);
}

int
smsc_detach(struct device *self, int flags)
{
	struct smsc_softc *sc = (struct smsc_softc *)self;
	struct ifnet *ifp = &sc->sc_ac.ac_if;
	int s;

	if (timeout_initialized(&sc->sc_stat_ch))
		timeout_del(&sc->sc_stat_ch);

	if (sc->sc_ep[SMSC_ENDPT_TX] != NULL)
		usbd_abort_pipe(sc->sc_ep[SMSC_ENDPT_TX]);
	if (sc->sc_ep[SMSC_ENDPT_RX] != NULL)
		usbd_abort_pipe(sc->sc_ep[SMSC_ENDPT_RX]);
	if (sc->sc_ep[SMSC_ENDPT_INTR] != NULL)
		usbd_abort_pipe(sc->sc_ep[SMSC_ENDPT_INTR]);

	/*
	 * Remove any pending tasks.  They cannot be executing because they run
	 * in the same thread as detach.
	 */
	usb_rem_task(sc->sc_udev, &sc->sc_tick_task);
	usb_rem_task(sc->sc_udev, &sc->sc_stop_task);

	s = splusb();

	if (--sc->sc_refcnt >= 0) {
		/* Wait for processes to go away */
		usb_detach_wait(&sc->sc_dev);
	}

	if (ifp->if_flags & IFF_RUNNING)
		smsc_stop(sc);

	mii_detach(&sc->sc_mii, MII_PHY_ANY, MII_OFFSET_ANY);
	ifmedia_delete_instance(&sc->sc_mii.mii_media, IFM_INST_ANY);
	if (ifp->if_softc != NULL) {
		ether_ifdetach(ifp);
		if_detach(ifp);
	}

#ifdef DIAGNOSTIC
	if (sc->sc_ep[SMSC_ENDPT_TX] != NULL ||
	    sc->sc_ep[SMSC_ENDPT_RX] != NULL ||
	    sc->sc_ep[SMSC_ENDPT_INTR] != NULL)
		printf("%s: detach has active endpoints\n",
		    sc->sc_dev.dv_xname);
#endif

	splx(s);

	return (0);
}

void
smsc_tick_task(void *xsc)
{
	int			 s;
	struct smsc_softc	*sc = xsc;
	struct mii_data		*mii;

	if (sc == NULL)
		return;

	if (usbd_is_dying(sc->sc_udev))
		return;
	mii = &sc->sc_mii;
	if (mii == NULL)
		return;

	s = splnet();

	mii_tick(mii);
	if ((sc->sc_flags & SMSC_FLAG_LINK) == 0)
		smsc_miibus_statchg(&sc->sc_dev);
	timeout_add_sec(&sc->sc_stat_ch, 1);

	splx(s);
}

void
smsc_lock_mii(struct smsc_softc *sc)
{
	sc->sc_refcnt++;
	rw_enter_write(&sc->sc_mii_lock);
}

void
smsc_unlock_mii(struct smsc_softc *sc)
{
	rw_exit_write(&sc->sc_mii_lock);
	if (--sc->sc_refcnt < 0)
		usb_detach_wakeup(&sc->sc_dev);
}

void
smsc_rxeof(struct usbd_xfer *xfer, void *priv, usbd_status status)
{
	struct smsc_chain	*c = (struct smsc_chain *)priv;
	struct smsc_softc	*sc = c->sc_sc;
	struct ifnet		*ifp = &sc->sc_ac.ac_if;
	u_char			*buf = c->sc_buf;
	uint32_t		total_len;
	uint16_t		pktlen = 0;
	struct mbuf_list	ml = MBUF_LIST_INITIALIZER();
	struct mbuf		*m;
	int			s;
	uint32_t		rxhdr;

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
			usbd_clear_endpoint_stall_async(sc->sc_ep[SMSC_ENDPT_RX]);
		goto done;
	}

	usbd_get_xfer_status(xfer, NULL, NULL, &total_len, NULL);
	smsc_dbg_printf(sc, "xfer status total_len %d\n", total_len);

	do {
		if (total_len < sizeof(rxhdr)) {
			smsc_dbg_printf(sc, "total_len %d < sizeof(rxhdr) %d\n",
			    total_len, sizeof(rxhdr));
			ifp->if_ierrors++;
			goto done;
		}

		buf += pktlen;

		memcpy(&rxhdr, buf, sizeof(rxhdr));
		rxhdr = letoh32(rxhdr);
		total_len -= sizeof(rxhdr);

		if (rxhdr & SMSC_RX_STAT_ERROR) {
			smsc_dbg_printf(sc, "rx error (hdr 0x%08x)\n", rxhdr);
			ifp->if_ierrors++;
			goto done;
		}

		pktlen = (uint16_t)SMSC_RX_STAT_FRM_LENGTH(rxhdr);
		smsc_dbg_printf(sc, "rxeof total_len %d pktlen %d rxhdr "
		    "0x%08x\n", total_len, pktlen, rxhdr);
		if (pktlen > total_len) {
			smsc_dbg_printf(sc, "pktlen %d > total_len %d\n",
			    pktlen, total_len);
			ifp->if_ierrors++;
			goto done;
		}

		buf += sizeof(rxhdr);

		if (total_len < pktlen)
			total_len = 0;
		else
			total_len -= pktlen;
		
		m = m_devget(buf, pktlen, ETHER_ALIGN);
		if (m == NULL) {
			smsc_dbg_printf(sc, "m_devget returned NULL\n");
			ifp->if_ierrors++;
			goto done;
		}

		ml_enqueue(&ml, m);
	} while (total_len > 0);

done:
	s = splnet();
	if_input(ifp, &ml);
	splx(s);
	memset(c->sc_buf, 0, sc->sc_bufsz);

	/* Setup new transfer. */
	usbd_setup_xfer(xfer, sc->sc_ep[SMSC_ENDPT_RX],
	    c, c->sc_buf, sc->sc_bufsz,
	    USBD_SHORT_XFER_OK | USBD_NO_COPY,
	    USBD_NO_TIMEOUT, smsc_rxeof);
	usbd_transfer(xfer);

	return;
}

void
smsc_txeof(struct usbd_xfer *xfer, void *priv, usbd_status status)
{
	struct smsc_softc	*sc;
	struct smsc_chain	*c;
	struct ifnet		*ifp;
	int			s;

	c = priv;
	sc = c->sc_sc;
	ifp = &sc->sc_ac.ac_if;

	if (usbd_is_dying(sc->sc_udev))
		return;

	s = splnet();

	if (status != USBD_NORMAL_COMPLETION) {
		if (status == USBD_NOT_STARTED || status == USBD_CANCELLED) {
			splx(s);
			return;
		}
		ifp->if_oerrors++;
		printf("%s: usb error on tx: %s\n", sc->sc_dev.dv_xname,
		    usbd_errstr(status));
		if (status == USBD_STALLED)
			usbd_clear_endpoint_stall_async(sc->sc_ep[SMSC_ENDPT_TX]);
		splx(s);
		return;
	}

	ifp->if_timer = 0;
	ifq_clr_oactive(&ifp->if_snd);

	m_freem(c->sc_mbuf);
	c->sc_mbuf = NULL;

	if (ifq_empty(&ifp->if_snd) == 0)
		smsc_start(ifp);

	splx(s);
}

int
smsc_tx_list_init(struct smsc_softc *sc)
{
	struct smsc_cdata *cd;
	struct smsc_chain *c;
	int i;

	cd = &sc->sc_cdata;
	for (i = 0; i < SMSC_TX_LIST_CNT; i++) {
		c = &cd->tx_chain[i];
		c->sc_sc = sc;
		c->sc_idx = i;
		c->sc_mbuf = NULL;
		if (c->sc_xfer == NULL) {
			c->sc_xfer = usbd_alloc_xfer(sc->sc_udev);
			if (c->sc_xfer == NULL)
				return (ENOBUFS);
			c->sc_buf = usbd_alloc_buffer(c->sc_xfer,
			    sc->sc_bufsz);
			if (c->sc_buf == NULL) {
				usbd_free_xfer(c->sc_xfer);
				return (ENOBUFS);
			}
		}
	}

	return (0);
}

int
smsc_rx_list_init(struct smsc_softc *sc)
{
	struct smsc_cdata *cd;
	struct smsc_chain *c;
	int i;

	cd = &sc->sc_cdata;
	for (i = 0; i < SMSC_RX_LIST_CNT; i++) {
		c = &cd->rx_chain[i];
		c->sc_sc = sc;
		c->sc_idx = i;
		c->sc_mbuf = NULL;
		if (c->sc_xfer == NULL) {
			c->sc_xfer = usbd_alloc_xfer(sc->sc_udev);
			if (c->sc_xfer == NULL)
				return (ENOBUFS);
			c->sc_buf = usbd_alloc_buffer(c->sc_xfer,
			    sc->sc_bufsz);
			if (c->sc_buf == NULL) {
				usbd_free_xfer(c->sc_xfer);
				return (ENOBUFS);
			}
		}
	}

	return (0);
}

int
smsc_encap(struct smsc_softc *sc, struct mbuf *m, int idx)
{
	struct smsc_chain	*c;
	usbd_status		 err;
	uint32_t		 txhdr;
	uint32_t		 frm_len = 0;

	c = &sc->sc_cdata.tx_chain[idx];

	/*
	 * Each frame is prefixed with two 32-bit values describing the
	 * length of the packet and buffer.
	 */
	txhdr = SMSC_TX_CTRL_0_BUF_SIZE(m->m_pkthdr.len) | 
			SMSC_TX_CTRL_0_FIRST_SEG | SMSC_TX_CTRL_0_LAST_SEG;
	txhdr = htole32(txhdr);
	memcpy(c->sc_buf, &txhdr, sizeof(txhdr));
	
	txhdr = SMSC_TX_CTRL_1_PKT_LENGTH(m->m_pkthdr.len);
	txhdr = htole32(txhdr);
	memcpy(c->sc_buf + 4, &txhdr, sizeof(txhdr));
	
	frm_len += 8;

	/* Next copy in the actual packet */
	m_copydata(m, 0, m->m_pkthdr.len, c->sc_buf + frm_len);
	frm_len += m->m_pkthdr.len;

	c->sc_mbuf = m;

	usbd_setup_xfer(c->sc_xfer, sc->sc_ep[SMSC_ENDPT_TX],
	    c, c->sc_buf, frm_len, USBD_FORCE_SHORT_XFER | USBD_NO_COPY,
	    10000, smsc_txeof);

	err = usbd_transfer(c->sc_xfer);
	if (err != USBD_IN_PROGRESS) {
		c->sc_mbuf = NULL;
		smsc_stop(sc);
		return (EIO);
	}

	sc->sc_cdata.tx_cnt++;

	return (0);
}
