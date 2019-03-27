/*-
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD
 *
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

#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");

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
 *
 *
 *
 * H/W TCP & UDP Checksum Offloading
 * ---------------------------------
 * The chip supports both tx and rx offloading of UDP & TCP checksums, this
 * feature can be dynamically enabled/disabled.  
 *
 * RX checksuming is performed across bytes after the IPv4 header to the end of
 * the Ethernet frame, this means if the frame is padded with non-zero values
 * the H/W checksum will be incorrect, however the rx code compensates for this.
 *
 * TX checksuming is more complicated, the device requires a special header to
 * be prefixed onto the start of the frame which indicates the start and end
 * positions of the UDP or TCP frame.  This requires the driver to manually
 * go through the packet data and decode the headers prior to sending.
 * On Linux they generally provide cues to the location of the csum and the
 * area to calculate it over, on FreeBSD we seem to have to do it all ourselves,
 * hence this is not as optimal and therefore h/w tX checksum is currently not
 * implemented.
 *
 */
#include <sys/stdint.h>
#include <sys/stddef.h>
#include <sys/param.h>
#include <sys/queue.h>
#include <sys/types.h>
#include <sys/systm.h>
#include <sys/kernel.h>
#include <sys/bus.h>
#include <sys/module.h>
#include <sys/lock.h>
#include <sys/mutex.h>
#include <sys/condvar.h>
#include <sys/socket.h>
#include <sys/sysctl.h>
#include <sys/sx.h>
#include <sys/unistd.h>
#include <sys/callout.h>
#include <sys/malloc.h>
#include <sys/priv.h>
#include <sys/random.h>

#include <net/if.h>
#include <net/if_var.h>

#include <netinet/in.h>
#include <netinet/ip.h>

#include "opt_platform.h"

#ifdef FDT
#include <dev/fdt/fdt_common.h>
#include <dev/ofw/ofw_bus.h>
#include <dev/ofw/ofw_bus_subr.h>
#endif

#include <dev/usb/usb.h>
#include <dev/usb/usbdi.h>
#include <dev/usb/usbdi_util.h>
#include "usbdevs.h"

#define	USB_DEBUG_VAR smsc_debug
#include <dev/usb/usb_debug.h>
#include <dev/usb/usb_process.h>

#include <dev/usb/net/usb_ethernet.h>

#include <dev/usb/net/if_smscreg.h>

#ifdef USB_DEBUG
static int smsc_debug = 0;

SYSCTL_NODE(_hw_usb, OID_AUTO, smsc, CTLFLAG_RW, 0, "USB smsc");
SYSCTL_INT(_hw_usb_smsc, OID_AUTO, debug, CTLFLAG_RWTUN, &smsc_debug, 0,
    "Debug level");
#endif

/*
 * Various supported device vendors/products.
 */
static const struct usb_device_id smsc_devs[] = {
#define	SMSC_DEV(p,i) { USB_VPI(USB_VENDOR_SMC2, USB_PRODUCT_SMC2_##p, i) }
	SMSC_DEV(LAN89530_ETH, 0),
	SMSC_DEV(LAN9500_ETH, 0),
	SMSC_DEV(LAN9500_ETH_2, 0),
	SMSC_DEV(LAN9500A_ETH, 0),
	SMSC_DEV(LAN9500A_ETH_2, 0),
	SMSC_DEV(LAN9505_ETH, 0),
	SMSC_DEV(LAN9505A_ETH, 0),
	SMSC_DEV(LAN9514_ETH, 0),
	SMSC_DEV(LAN9514_ETH_2, 0),
	SMSC_DEV(LAN9530_ETH, 0),
	SMSC_DEV(LAN9730_ETH, 0),
	SMSC_DEV(LAN9500_SAL10, 0),
	SMSC_DEV(LAN9505_SAL10, 0),
	SMSC_DEV(LAN9500A_SAL10, 0),
	SMSC_DEV(LAN9505A_SAL10, 0),
	SMSC_DEV(LAN9514_SAL10, 0),
	SMSC_DEV(LAN9500A_HAL, 0),
	SMSC_DEV(LAN9505A_HAL, 0),
#undef SMSC_DEV
};


#ifdef USB_DEBUG
#define smsc_dbg_printf(sc, fmt, args...) \
	do { \
		if (smsc_debug > 0) \
			device_printf((sc)->sc_ue.ue_dev, "debug: " fmt, ##args); \
	} while(0)
#else
#define smsc_dbg_printf(sc, fmt, args...) do { } while (0)
#endif

#define smsc_warn_printf(sc, fmt, args...) \
	device_printf((sc)->sc_ue.ue_dev, "warning: " fmt, ##args)

#define smsc_err_printf(sc, fmt, args...) \
	device_printf((sc)->sc_ue.ue_dev, "error: " fmt, ##args)
	

#define ETHER_IS_ZERO(addr) \
	(!(addr[0] | addr[1] | addr[2] | addr[3] | addr[4] | addr[5]))
	
#define ETHER_IS_VALID(addr) \
	(!ETHER_IS_MULTICAST(addr) && !ETHER_IS_ZERO(addr))
	
static device_probe_t smsc_probe;
static device_attach_t smsc_attach;
static device_detach_t smsc_detach;

static usb_callback_t smsc_bulk_read_callback;
static usb_callback_t smsc_bulk_write_callback;

static miibus_readreg_t smsc_miibus_readreg;
static miibus_writereg_t smsc_miibus_writereg;
static miibus_statchg_t smsc_miibus_statchg;

#if __FreeBSD_version > 1000000
static int smsc_attach_post_sub(struct usb_ether *ue);
#endif
static uether_fn_t smsc_attach_post;
static uether_fn_t smsc_init;
static uether_fn_t smsc_stop;
static uether_fn_t smsc_start;
static uether_fn_t smsc_tick;
static uether_fn_t smsc_setmulti;
static uether_fn_t smsc_setpromisc;

static int	smsc_ifmedia_upd(struct ifnet *);
static void	smsc_ifmedia_sts(struct ifnet *, struct ifmediareq *);

static int smsc_chip_init(struct smsc_softc *sc);
static int smsc_ioctl(struct ifnet *ifp, u_long cmd, caddr_t data);

static const struct usb_config smsc_config[SMSC_N_TRANSFER] = {

	[SMSC_BULK_DT_WR] = {
		.type = UE_BULK,
		.endpoint = UE_ADDR_ANY,
		.direction = UE_DIR_OUT,
		.frames = 16,
		.bufsize = 16 * (MCLBYTES + 16),
		.flags = {.pipe_bof = 1,.force_short_xfer = 1,},
		.callback = smsc_bulk_write_callback,
		.timeout = 10000,	/* 10 seconds */
	},

	[SMSC_BULK_DT_RD] = {
		.type = UE_BULK,
		.endpoint = UE_ADDR_ANY,
		.direction = UE_DIR_IN,
		.bufsize = 20480,	/* bytes */
		.flags = {.pipe_bof = 1,.short_xfer_ok = 1,},
		.callback = smsc_bulk_read_callback,
		.timeout = 0,	/* no timeout */
	},

	/* The SMSC chip supports an interrupt endpoints, however they aren't
	 * needed as we poll on the MII status.
	 */
};

static const struct usb_ether_methods smsc_ue_methods = {
	.ue_attach_post = smsc_attach_post,
#if __FreeBSD_version > 1000000
	.ue_attach_post_sub = smsc_attach_post_sub,
#endif
	.ue_start = smsc_start,
	.ue_ioctl = smsc_ioctl,
	.ue_init = smsc_init,
	.ue_stop = smsc_stop,
	.ue_tick = smsc_tick,
	.ue_setmulti = smsc_setmulti,
	.ue_setpromisc = smsc_setpromisc,
	.ue_mii_upd = smsc_ifmedia_upd,
	.ue_mii_sts = smsc_ifmedia_sts,
};

/**
 *	smsc_read_reg - Reads a 32-bit register on the device
 *	@sc: driver soft context
 *	@off: offset of the register
 *	@data: pointer a value that will be populated with the register value
 *	
 *	LOCKING:
 *	The device lock must be held before calling this function.
 *
 *	RETURNS:
 *	0 on success, a USB_ERR_?? error code on failure.
 */
static int
smsc_read_reg(struct smsc_softc *sc, uint32_t off, uint32_t *data)
{
	struct usb_device_request req;
	uint32_t buf;
	usb_error_t err;

	SMSC_LOCK_ASSERT(sc, MA_OWNED);

	req.bmRequestType = UT_READ_VENDOR_DEVICE;
	req.bRequest = SMSC_UR_READ_REG;
	USETW(req.wValue, 0);
	USETW(req.wIndex, off);
	USETW(req.wLength, 4);

	err = uether_do_request(&sc->sc_ue, &req, &buf, 1000);
	if (err != 0)
		smsc_warn_printf(sc, "Failed to read register 0x%0x\n", off);

	*data = le32toh(buf);
	
	return (err);
}

/**
 *	smsc_write_reg - Writes a 32-bit register on the device
 *	@sc: driver soft context
 *	@off: offset of the register
 *	@data: the 32-bit value to write into the register
 *	
 *	LOCKING:
 *	The device lock must be held before calling this function.
 *
 *	RETURNS:
 *	0 on success, a USB_ERR_?? error code on failure.
 */
static int
smsc_write_reg(struct smsc_softc *sc, uint32_t off, uint32_t data)
{
	struct usb_device_request req;
	uint32_t buf;
	usb_error_t err;

	SMSC_LOCK_ASSERT(sc, MA_OWNED);
	
	buf = htole32(data);

	req.bmRequestType = UT_WRITE_VENDOR_DEVICE;
	req.bRequest = SMSC_UR_WRITE_REG;
	USETW(req.wValue, 0);
	USETW(req.wIndex, off);
	USETW(req.wLength, 4);

	err = uether_do_request(&sc->sc_ue, &req, &buf, 1000);
	if (err != 0)
		smsc_warn_printf(sc, "Failed to write register 0x%0x\n", off);

	return (err);
}

/**
 *	smsc_wait_for_bits - Polls on a register value until bits are cleared
 *	@sc: soft context
 *	@reg: offset of the register
 *	@bits: if the bits are clear the function returns
 *
 *	LOCKING:
 *	The device lock must be held before calling this function.
 *
 *	RETURNS:
 *	0 on success, or a USB_ERR_?? error code on failure.
 */
static int
smsc_wait_for_bits(struct smsc_softc *sc, uint32_t reg, uint32_t bits)
{
	usb_ticks_t start_ticks;
	const usb_ticks_t max_ticks = USB_MS_TO_TICKS(1000);
	uint32_t val;
	int err;
	
	SMSC_LOCK_ASSERT(sc, MA_OWNED);

	start_ticks = (usb_ticks_t)ticks;
	do {
		if ((err = smsc_read_reg(sc, reg, &val)) != 0)
			return (err);
		if (!(val & bits))
			return (0);
		
		uether_pause(&sc->sc_ue, hz / 100);
	} while (((usb_ticks_t)(ticks - start_ticks)) < max_ticks);

	return (USB_ERR_TIMEOUT);
}

/**
 *	smsc_eeprom_read - Reads the attached EEPROM
 *	@sc: soft context
 *	@off: the eeprom address offset
 *	@buf: stores the bytes
 *	@buflen: the number of bytes to read
 *
 *	Simply reads bytes from an attached eeprom.
 *
 *	LOCKING:
 *	The function takes and releases the device lock if it is not already held.
 *
 *	RETURNS:
 *	0 on success, or a USB_ERR_?? error code on failure.
 */
static int
smsc_eeprom_read(struct smsc_softc *sc, uint16_t off, uint8_t *buf, uint16_t buflen)
{
	usb_ticks_t start_ticks;
	const usb_ticks_t max_ticks = USB_MS_TO_TICKS(1000);
	int err;
	int locked;
	uint32_t val;
	uint16_t i;

	locked = mtx_owned(&sc->sc_mtx);
	if (!locked)
		SMSC_LOCK(sc);

	err = smsc_wait_for_bits(sc, SMSC_EEPROM_CMD, SMSC_EEPROM_CMD_BUSY);
	if (err != 0) {
		smsc_warn_printf(sc, "eeprom busy, failed to read data\n");
		goto done;
	}

	/* start reading the bytes, one at a time */
	for (i = 0; i < buflen; i++) {
	
		val = SMSC_EEPROM_CMD_BUSY | (SMSC_EEPROM_CMD_ADDR_MASK & (off + i));
		if ((err = smsc_write_reg(sc, SMSC_EEPROM_CMD, val)) != 0)
			goto done;
		
		start_ticks = (usb_ticks_t)ticks;
		do {
			if ((err = smsc_read_reg(sc, SMSC_EEPROM_CMD, &val)) != 0)
				goto done;
			if (!(val & SMSC_EEPROM_CMD_BUSY) || (val & SMSC_EEPROM_CMD_TIMEOUT))
				break;

			uether_pause(&sc->sc_ue, hz / 100);
		} while (((usb_ticks_t)(ticks - start_ticks)) < max_ticks);

		if (val & (SMSC_EEPROM_CMD_BUSY | SMSC_EEPROM_CMD_TIMEOUT)) {
			smsc_warn_printf(sc, "eeprom command failed\n");
			err = USB_ERR_IOERROR;
			break;
		}
			
		if ((err = smsc_read_reg(sc, SMSC_EEPROM_DATA, &val)) != 0)
			goto done;

		buf[i] = (val & 0xff);
	}
	
done:
	if (!locked)
		SMSC_UNLOCK(sc);

	return (err);
}

/**
 *	smsc_miibus_readreg - Reads a MII/MDIO register
 *	@dev: usb ether device
 *	@phy: the number of phy reading from
 *	@reg: the register address
 *
 *	Attempts to read a phy register over the MII bus.
 *
 *	LOCKING:
 *	Takes and releases the device mutex lock if not already held.
 *
 *	RETURNS:
 *	Returns the 16-bits read from the MII register, if this function fails 0
 *	is returned.
 */
static int
smsc_miibus_readreg(device_t dev, int phy, int reg)
{
	struct smsc_softc *sc = device_get_softc(dev);
	int locked;
	uint32_t addr;
	uint32_t val = 0;

	locked = mtx_owned(&sc->sc_mtx);
	if (!locked)
		SMSC_LOCK(sc);

	if (smsc_wait_for_bits(sc, SMSC_MII_ADDR, SMSC_MII_BUSY) != 0) {
		smsc_warn_printf(sc, "MII is busy\n");
		goto done;
	}

	addr = (phy << 11) | (reg << 6) | SMSC_MII_READ | SMSC_MII_BUSY;
	smsc_write_reg(sc, SMSC_MII_ADDR, addr);

	if (smsc_wait_for_bits(sc, SMSC_MII_ADDR, SMSC_MII_BUSY) != 0)
		smsc_warn_printf(sc, "MII read timeout\n");

	smsc_read_reg(sc, SMSC_MII_DATA, &val);
	val = le32toh(val);
	
done:
	if (!locked)
		SMSC_UNLOCK(sc);

	return (val & 0xFFFF);
}

/**
 *	smsc_miibus_writereg - Writes a MII/MDIO register
 *	@dev: usb ether device
 *	@phy: the number of phy writing to
 *	@reg: the register address
 *	@val: the value to write
 *
 *	Attempts to write a phy register over the MII bus.
 *
 *	LOCKING:
 *	Takes and releases the device mutex lock if not already held.
 *
 *	RETURNS:
 *	Always returns 0 regardless of success or failure.
 */
static int
smsc_miibus_writereg(device_t dev, int phy, int reg, int val)
{
	struct smsc_softc *sc = device_get_softc(dev);
	int locked;
	uint32_t addr;

	if (sc->sc_phyno != phy)
		return (0);

	locked = mtx_owned(&sc->sc_mtx);
	if (!locked)
		SMSC_LOCK(sc);

	if (smsc_wait_for_bits(sc, SMSC_MII_ADDR, SMSC_MII_BUSY) != 0) {
		smsc_warn_printf(sc, "MII is busy\n");
		goto done;
	}

	val = htole32(val);
	smsc_write_reg(sc, SMSC_MII_DATA, val);

	addr = (phy << 11) | (reg << 6) | SMSC_MII_WRITE | SMSC_MII_BUSY;
	smsc_write_reg(sc, SMSC_MII_ADDR, addr);

	if (smsc_wait_for_bits(sc, SMSC_MII_ADDR, SMSC_MII_BUSY) != 0)
		smsc_warn_printf(sc, "MII write timeout\n");

done:
	if (!locked)
		SMSC_UNLOCK(sc);
	return (0);
}



/**
 *	smsc_miibus_statchg - Called to detect phy status change
 *	@dev: usb ether device
 *
 *	This function is called periodically by the system to poll for status
 *	changes of the link.
 *
 *	LOCKING:
 *	Takes and releases the device mutex lock if not already held.
 */
static void
smsc_miibus_statchg(device_t dev)
{
	struct smsc_softc *sc = device_get_softc(dev);
	struct mii_data *mii = uether_getmii(&sc->sc_ue);
	struct ifnet *ifp;
	int locked;
	int err;
	uint32_t flow;
	uint32_t afc_cfg;

	locked = mtx_owned(&sc->sc_mtx);
	if (!locked)
		SMSC_LOCK(sc);

	ifp = uether_getifp(&sc->sc_ue);
	if (mii == NULL || ifp == NULL ||
	    (ifp->if_drv_flags & IFF_DRV_RUNNING) == 0)
		goto done;

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
		goto done;
	}
	
	err = smsc_read_reg(sc, SMSC_AFC_CFG, &afc_cfg);
	if (err) {
		smsc_warn_printf(sc, "failed to read initial AFC_CFG, error %d\n", err);
		goto done;
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
	
done:
	if (!locked)
		SMSC_UNLOCK(sc);
}

/**
 *	smsc_ifmedia_upd - Set media options
 *	@ifp: interface pointer
 *
 *	Basically boilerplate code that simply calls the mii functions to set the
 *	media options.
 *
 *	LOCKING:
 *	The device lock must be held before this function is called.
 *
 *	RETURNS:
 *	Returns 0 on success or a negative error code.
 */
static int
smsc_ifmedia_upd(struct ifnet *ifp)
{
	struct smsc_softc *sc = ifp->if_softc;
	struct mii_data *mii = uether_getmii(&sc->sc_ue);
	struct mii_softc *miisc;
	int err;

	SMSC_LOCK_ASSERT(sc, MA_OWNED);

	LIST_FOREACH(miisc, &mii->mii_phys, mii_list)
		PHY_RESET(miisc);
	err = mii_mediachg(mii);
	return (err);
}

/**
 *	smsc_ifmedia_sts - Report current media status
 *	@ifp: inet interface pointer
 *	@ifmr: interface media request
 *
 *	Basically boilerplate code that simply calls the mii functions to get the
 *	media status.
 *
 *	LOCKING:
 *	Internally takes and releases the device lock.
 */
static void
smsc_ifmedia_sts(struct ifnet *ifp, struct ifmediareq *ifmr)
{
	struct smsc_softc *sc = ifp->if_softc;
	struct mii_data *mii = uether_getmii(&sc->sc_ue);

	SMSC_LOCK(sc);
	mii_pollstat(mii);
	ifmr->ifm_active = mii->mii_media_active;
	ifmr->ifm_status = mii->mii_media_status;
	SMSC_UNLOCK(sc);
}

/**
 *	smsc_hash - Calculate the hash of a mac address
 *	@addr: The mac address to calculate the hash on
 *
 *	This function is used when configuring a range of m'cast mac addresses to
 *	filter on.  The hash of the mac address is put in the device's mac hash
 *	table.
 *
 *	RETURNS:
 *	Returns a value from 0-63 value which is the hash of the mac address.
 */
static inline uint32_t
smsc_hash(uint8_t addr[ETHER_ADDR_LEN])
{
	return (ether_crc32_be(addr, ETHER_ADDR_LEN) >> 26) & 0x3f;
}

/**
 *	smsc_setmulti - Setup multicast
 *	@ue: usb ethernet device context
 *
 *	Tells the device to either accept frames with a multicast mac address, a
 *	select group of m'cast mac addresses or just the devices mac address.
 *
 *	LOCKING:
 *	Should be called with the SMSC lock held.
 */
static void
smsc_setmulti(struct usb_ether *ue)
{
	struct smsc_softc *sc = uether_getsc(ue);
	struct ifnet *ifp = uether_getifp(ue);
	struct ifmultiaddr *ifma;
	uint32_t hashtbl[2] = { 0, 0 };
	uint32_t hash;

	SMSC_LOCK_ASSERT(sc, MA_OWNED);

	if (ifp->if_flags & (IFF_ALLMULTI | IFF_PROMISC)) {
		smsc_dbg_printf(sc, "receive all multicast enabled\n");
		sc->sc_mac_csr |= SMSC_MAC_CSR_MCPAS;
		sc->sc_mac_csr &= ~SMSC_MAC_CSR_HPFILT;
		
	} else {
		/* Take the lock of the mac address list before hashing each of them */
		if_maddr_rlock(ifp);

		if (!CK_STAILQ_EMPTY(&ifp->if_multiaddrs)) {
			/* We are filtering on a set of address so calculate hashes of each
			 * of the address and set the corresponding bits in the register.
			 */
			sc->sc_mac_csr |= SMSC_MAC_CSR_HPFILT;
			sc->sc_mac_csr &= ~(SMSC_MAC_CSR_PRMS | SMSC_MAC_CSR_MCPAS);
		
			CK_STAILQ_FOREACH(ifma, &ifp->if_multiaddrs, ifma_link) {
				if (ifma->ifma_addr->sa_family != AF_LINK)
					continue;

				hash = smsc_hash(LLADDR((struct sockaddr_dl *)ifma->ifma_addr));
				hashtbl[hash >> 5] |= 1 << (hash & 0x1F);
			}
		} else {
			/* Only receive packets with destination set to our mac address */
			sc->sc_mac_csr &= ~(SMSC_MAC_CSR_MCPAS | SMSC_MAC_CSR_HPFILT);
		}

		if_maddr_runlock(ifp);
		
		/* Debug */
		if (sc->sc_mac_csr & SMSC_MAC_CSR_HPFILT)
			smsc_dbg_printf(sc, "receive select group of macs\n");
		else
			smsc_dbg_printf(sc, "receive own packets only\n");
	}

	/* Write the hash table and mac control registers */
	smsc_write_reg(sc, SMSC_HASHH, hashtbl[1]);
	smsc_write_reg(sc, SMSC_HASHL, hashtbl[0]);
	smsc_write_reg(sc, SMSC_MAC_CSR, sc->sc_mac_csr);
}


/**
 *	smsc_setpromisc - Enables/disables promiscuous mode
 *	@ue: usb ethernet device context
 *
 *	LOCKING:
 *	Should be called with the SMSC lock held.
 */
static void
smsc_setpromisc(struct usb_ether *ue)
{
	struct smsc_softc *sc = uether_getsc(ue);
	struct ifnet *ifp = uether_getifp(ue);

	smsc_dbg_printf(sc, "promiscuous mode %sabled\n",
	                (ifp->if_flags & IFF_PROMISC) ? "en" : "dis");

	SMSC_LOCK_ASSERT(sc, MA_OWNED);

	if (ifp->if_flags & IFF_PROMISC)
		sc->sc_mac_csr |= SMSC_MAC_CSR_PRMS;
	else
		sc->sc_mac_csr &= ~SMSC_MAC_CSR_PRMS;

	smsc_write_reg(sc, SMSC_MAC_CSR, sc->sc_mac_csr);
}


/**
 *	smsc_sethwcsum - Enable or disable H/W UDP and TCP checksumming
 *	@sc: driver soft context
 *
 *	LOCKING:
 *	Should be called with the SMSC lock held.
 *
 *	RETURNS:
 *	Returns 0 on success or a negative error code.
 */
static int smsc_sethwcsum(struct smsc_softc *sc)
{
	struct ifnet *ifp = uether_getifp(&sc->sc_ue);
	uint32_t val;
	int err;

	if (!ifp)
		return (-EIO);

	SMSC_LOCK_ASSERT(sc, MA_OWNED);

	err = smsc_read_reg(sc, SMSC_COE_CTRL, &val);
	if (err != 0) {
		smsc_warn_printf(sc, "failed to read SMSC_COE_CTRL (err=%d)\n", err);
		return (err);
	}

	/* Enable/disable the Rx checksum */
	if ((ifp->if_capabilities & ifp->if_capenable) & IFCAP_RXCSUM)
		val |= SMSC_COE_CTRL_RX_EN;
	else
		val &= ~SMSC_COE_CTRL_RX_EN;

	/* Enable/disable the Tx checksum (currently not supported) */
	if ((ifp->if_capabilities & ifp->if_capenable) & IFCAP_TXCSUM)
		val |= SMSC_COE_CTRL_TX_EN;
	else
		val &= ~SMSC_COE_CTRL_TX_EN;

	err = smsc_write_reg(sc, SMSC_COE_CTRL, val);
	if (err != 0) {
		smsc_warn_printf(sc, "failed to write SMSC_COE_CTRL (err=%d)\n", err);
		return (err);
	}

	return (0);
}

/**
 *	smsc_setmacaddress - Sets the mac address in the device
 *	@sc: driver soft context
 *	@addr: pointer to array contain at least 6 bytes of the mac
 *
 *	Writes the MAC address into the device, usually the MAC is programmed with
 *	values from the EEPROM.
 *
 *	LOCKING:
 *	Should be called with the SMSC lock held.
 *
 *	RETURNS:
 *	Returns 0 on success or a negative error code.
 */
static int
smsc_setmacaddress(struct smsc_softc *sc, const uint8_t *addr)
{
	int err;
	uint32_t val;

	smsc_dbg_printf(sc, "setting mac address to %02x:%02x:%02x:%02x:%02x:%02x\n",
	                addr[0], addr[1], addr[2], addr[3], addr[4], addr[5]);

	SMSC_LOCK_ASSERT(sc, MA_OWNED);

	val = (addr[3] << 24) | (addr[2] << 16) | (addr[1] << 8) | addr[0];
	if ((err = smsc_write_reg(sc, SMSC_MAC_ADDRL, val)) != 0)
		goto done;
		
	val = (addr[5] << 8) | addr[4];
	err = smsc_write_reg(sc, SMSC_MAC_ADDRH, val);
	
done:
	return (err);
}

/**
 *	smsc_reset - Reset the SMSC chip
 *	@sc: device soft context
 *
 *	LOCKING:
 *	Should be called with the SMSC lock held.
 */
static void
smsc_reset(struct smsc_softc *sc)
{
	struct usb_config_descriptor *cd;
	usb_error_t err;

	cd = usbd_get_config_descriptor(sc->sc_ue.ue_udev);

	err = usbd_req_set_config(sc->sc_ue.ue_udev, &sc->sc_mtx,
	                          cd->bConfigurationValue);
	if (err)
		smsc_warn_printf(sc, "reset failed (ignored)\n");

	/* Wait a little while for the chip to get its brains in order. */
	uether_pause(&sc->sc_ue, hz / 100);

	/* Reinitialize controller to achieve full reset. */
	smsc_chip_init(sc);
}


/**
 *	smsc_init - Initialises the LAN95xx chip
 *	@ue: USB ether interface
 *
 *	Called when the interface is brought up (i.e. ifconfig ue0 up), this
 *	initialise the interface and the rx/tx pipes.
 *
 *	LOCKING:
 *	Should be called with the SMSC lock held.
 */
static void
smsc_init(struct usb_ether *ue)
{
	struct smsc_softc *sc = uether_getsc(ue);
	struct ifnet *ifp = uether_getifp(ue);

	SMSC_LOCK_ASSERT(sc, MA_OWNED);

	if (smsc_setmacaddress(sc, IF_LLADDR(ifp)))
		smsc_dbg_printf(sc, "setting MAC address failed\n");

	if ((ifp->if_drv_flags & IFF_DRV_RUNNING) != 0)
		return;

	/* Cancel pending I/O */
	smsc_stop(ue);

#if __FreeBSD_version <= 1000000
	/* On earlier versions this was the first place we could tell the system
	 * that we supported h/w csuming, however this is only called after the
	 * the interface has been brought up - not ideal.  
	 */
	if (!(ifp->if_capabilities & IFCAP_RXCSUM)) {
		ifp->if_capabilities |= IFCAP_RXCSUM;
		ifp->if_capenable |= IFCAP_RXCSUM;
		ifp->if_hwassist = 0;
	}
	
	/* TX checksuming is disabled for now
	ifp->if_capabilities |= IFCAP_TXCSUM;
	ifp->if_capenable |= IFCAP_TXCSUM;
	ifp->if_hwassist = CSUM_TCP | CSUM_UDP;
	*/
#endif

	/* Reset the ethernet interface. */
	smsc_reset(sc);

	/* Load the multicast filter. */
	smsc_setmulti(ue);

	/* TCP/UDP checksum offload engines. */
	smsc_sethwcsum(sc);

	usbd_xfer_set_stall(sc->sc_xfer[SMSC_BULK_DT_WR]);

	/* Indicate we are up and running. */
	ifp->if_drv_flags |= IFF_DRV_RUNNING;

	/* Switch to selected media. */
	smsc_ifmedia_upd(ifp);
	smsc_start(ue);
}

/**
 *	smsc_bulk_read_callback - Read callback used to process the USB URB
 *	@xfer: the USB transfer
 *	@error: 
 *
 *	Reads the URB data which can contain one or more ethernet frames, the
 *	frames are copyed into a mbuf and given to the system.
 *
 *	LOCKING:
 *	No locking required, doesn't access internal driver settings.
 */
static void
smsc_bulk_read_callback(struct usb_xfer *xfer, usb_error_t error)
{
	struct smsc_softc *sc = usbd_xfer_softc(xfer);
	struct usb_ether *ue = &sc->sc_ue;
	struct ifnet *ifp = uether_getifp(ue);
	struct mbuf *m;
	struct usb_page_cache *pc;
	uint32_t rxhdr;
	uint16_t pktlen;
	int off;
	int actlen;

	usbd_xfer_status(xfer, &actlen, NULL, NULL, NULL);
	smsc_dbg_printf(sc, "rx : actlen %d\n", actlen);

	switch (USB_GET_STATE(xfer)) {
	case USB_ST_TRANSFERRED:
	
		/* There is always a zero length frame after bringing the IF up */
		if (actlen < (sizeof(rxhdr) + ETHER_CRC_LEN))
			goto tr_setup;

		/* There maybe multiple packets in the USB frame, each will have a 
		 * header and each needs to have it's own mbuf allocated and populated
		 * for it.
		 */
		pc = usbd_xfer_get_frame(xfer, 0);
		off = 0;
		
		while (off < actlen) {
		
			/* The frame header is always aligned on a 4 byte boundary */
			off = ((off + 0x3) & ~0x3);

			usbd_copy_out(pc, off, &rxhdr, sizeof(rxhdr));
			off += (sizeof(rxhdr) + ETHER_ALIGN);
			rxhdr = le32toh(rxhdr);
		
			pktlen = (uint16_t)SMSC_RX_STAT_FRM_LENGTH(rxhdr);
			
			smsc_dbg_printf(sc, "rx : rxhdr 0x%08x : pktlen %d : actlen %d : "
			                "off %d\n", rxhdr, pktlen, actlen, off);

			
			if (rxhdr & SMSC_RX_STAT_ERROR) {
				smsc_dbg_printf(sc, "rx error (hdr 0x%08x)\n", rxhdr);
				if_inc_counter(ifp, IFCOUNTER_IERRORS, 1);
				if (rxhdr & SMSC_RX_STAT_COLLISION)
					if_inc_counter(ifp, IFCOUNTER_COLLISIONS, 1);
			} else {

				/* Check if the ethernet frame is too big or too small */
				if ((pktlen < ETHER_HDR_LEN) || (pktlen > (actlen - off)))
					goto tr_setup;
			
				/* Create a new mbuf to store the packet in */
				m = uether_newbuf();
				if (m == NULL) {
					smsc_warn_printf(sc, "failed to create new mbuf\n");
					if_inc_counter(ifp, IFCOUNTER_IQDROPS, 1);
					goto tr_setup;
				}
				
				usbd_copy_out(pc, off, mtod(m, uint8_t *), pktlen);

				/* Check if RX TCP/UDP checksumming is being offloaded */
				if ((ifp->if_capenable & IFCAP_RXCSUM) != 0) {

					struct ether_header *eh;

					eh = mtod(m, struct ether_header *);
				
					/* Remove the extra 2 bytes of the csum */
					pktlen -= 2;

					/* The checksum appears to be simplistically calculated
					 * over the udp/tcp header and data up to the end of the
					 * eth frame.  Which means if the eth frame is padded
					 * the csum calculation is incorrectly performed over
					 * the padding bytes as well. Therefore to be safe we
					 * ignore the H/W csum on frames less than or equal to
					 * 64 bytes.
					 *
					 * Ignore H/W csum for non-IPv4 packets.
					 */
					if ((be16toh(eh->ether_type) == ETHERTYPE_IP) &&
					    (pktlen > ETHER_MIN_LEN)) {
						struct ip *ip;

						ip = (struct ip *)(eh + 1);
						if ((ip->ip_v == IPVERSION) &&
						    ((ip->ip_p == IPPROTO_TCP) ||
						     (ip->ip_p == IPPROTO_UDP))) {
							/* Indicate the UDP/TCP csum has been calculated */
							m->m_pkthdr.csum_flags |= CSUM_DATA_VALID;

							/* Copy the TCP/UDP checksum from the last 2 bytes
							 * of the transfer and put in the csum_data field.
							 */
							usbd_copy_out(pc, (off + pktlen),
							              &m->m_pkthdr.csum_data, 2);

							/* The data is copied in network order, but the
							 * csum algorithm in the kernel expects it to be
							 * in host network order.
							 */
							m->m_pkthdr.csum_data = ntohs(m->m_pkthdr.csum_data);

							smsc_dbg_printf(sc, "RX checksum offloaded (0x%04x)\n",
							                m->m_pkthdr.csum_data);
						}
					}
					
					/* Need to adjust the offset as well or we'll be off
					 * by 2 because the csum is removed from the packet
					 * length.
					 */
					off += 2;
				}
			
				/* Finally enqueue the mbuf on the receive queue */
				/* Remove 4 trailing bytes */
				if (pktlen < (4 + ETHER_HDR_LEN)) {
					m_freem(m);
					goto tr_setup;
				}
				uether_rxmbuf(ue, m, pktlen - 4);
			}

			/* Update the offset to move to the next potential packet */
			off += pktlen;
		}
	
		/* FALLTHROUGH */
		
	case USB_ST_SETUP:
tr_setup:
		usbd_xfer_set_frame_len(xfer, 0, usbd_xfer_max_len(xfer));
		usbd_transfer_submit(xfer);
		uether_rxflush(ue);
		return;

	default:
		if (error != USB_ERR_CANCELLED) {
			smsc_warn_printf(sc, "bulk read error, %s\n", usbd_errstr(error));
			usbd_xfer_set_stall(xfer);
			goto tr_setup;
		}
		return;
	}
}

/**
 *	smsc_bulk_write_callback - Write callback used to send ethernet frame(s)
 *	@xfer: the USB transfer
 *	@error: error code if the transfers is in an errored state
 *
 *	The main write function that pulls ethernet frames off the queue and sends
 *	them out.
 *
 *	LOCKING:
 *	
 */
static void
smsc_bulk_write_callback(struct usb_xfer *xfer, usb_error_t error)
{
	struct smsc_softc *sc = usbd_xfer_softc(xfer);
	struct ifnet *ifp = uether_getifp(&sc->sc_ue);
	struct usb_page_cache *pc;
	struct mbuf *m;
	uint32_t txhdr;
	uint32_t frm_len = 0;
	int nframes;

	switch (USB_GET_STATE(xfer)) {
	case USB_ST_TRANSFERRED:
		ifp->if_drv_flags &= ~IFF_DRV_OACTIVE;
		/* FALLTHROUGH */

	case USB_ST_SETUP:
tr_setup:
		if ((sc->sc_flags & SMSC_FLAG_LINK) == 0 ||
			(ifp->if_drv_flags & IFF_DRV_OACTIVE) != 0) {
			/* Don't send anything if there is no link or controller is busy. */
			return;
		}

		for (nframes = 0; nframes < 16 &&
		    !IFQ_DRV_IS_EMPTY(&ifp->if_snd); nframes++) {
			IFQ_DRV_DEQUEUE(&ifp->if_snd, m);
			if (m == NULL)
				break;
			usbd_xfer_set_frame_offset(xfer, nframes * MCLBYTES,
			    nframes);
			frm_len = 0;
			pc = usbd_xfer_get_frame(xfer, nframes);

			/* Each frame is prefixed with two 32-bit values describing the
			 * length of the packet and buffer.
			 */
			txhdr = SMSC_TX_CTRL_0_BUF_SIZE(m->m_pkthdr.len) | 
					SMSC_TX_CTRL_0_FIRST_SEG | SMSC_TX_CTRL_0_LAST_SEG;
			txhdr = htole32(txhdr);
			usbd_copy_in(pc, 0, &txhdr, sizeof(txhdr));
			
			txhdr = SMSC_TX_CTRL_1_PKT_LENGTH(m->m_pkthdr.len);
			txhdr = htole32(txhdr);
			usbd_copy_in(pc, 4, &txhdr, sizeof(txhdr));
			
			frm_len += 8;

			/* Next copy in the actual packet */
			usbd_m_copy_in(pc, frm_len, m, 0, m->m_pkthdr.len);
			frm_len += m->m_pkthdr.len;

			if_inc_counter(ifp, IFCOUNTER_OPACKETS, 1);

			/* If there's a BPF listener, bounce a copy of this frame to him */
			BPF_MTAP(ifp, m);

			m_freem(m);

			/* Set frame length. */
			usbd_xfer_set_frame_len(xfer, nframes, frm_len);
		}
		if (nframes != 0) {
			usbd_xfer_set_frames(xfer, nframes);
			usbd_transfer_submit(xfer);
			ifp->if_drv_flags |= IFF_DRV_OACTIVE;
		}
		return;

	default:
		if_inc_counter(ifp, IFCOUNTER_OERRORS, 1);
		ifp->if_drv_flags &= ~IFF_DRV_OACTIVE;
		
		if (error != USB_ERR_CANCELLED) {
			smsc_err_printf(sc, "usb error on tx: %s\n", usbd_errstr(error));
			usbd_xfer_set_stall(xfer);
			goto tr_setup;
		}
		return;
	}
}

/**
 *	smsc_tick - Called periodically to monitor the state of the LAN95xx chip
 *	@ue: USB ether interface
 *
 *	Simply calls the mii status functions to check the state of the link.
 *
 *	LOCKING:
 *	Should be called with the SMSC lock held.
 */
static void
smsc_tick(struct usb_ether *ue)
{
	struct smsc_softc *sc = uether_getsc(ue);
	struct mii_data *mii = uether_getmii(&sc->sc_ue);

	SMSC_LOCK_ASSERT(sc, MA_OWNED);

	mii_tick(mii);
	if ((sc->sc_flags & SMSC_FLAG_LINK) == 0) {
		smsc_miibus_statchg(ue->ue_dev);
		if ((sc->sc_flags & SMSC_FLAG_LINK) != 0)
			smsc_start(ue);
	}
}

/**
 *	smsc_start - Starts communication with the LAN95xx chip
 *	@ue: USB ether interface
 *
 *	
 *
 */
static void
smsc_start(struct usb_ether *ue)
{
	struct smsc_softc *sc = uether_getsc(ue);

	/*
	 * start the USB transfers, if not already started:
	 */
	usbd_transfer_start(sc->sc_xfer[SMSC_BULK_DT_RD]);
	usbd_transfer_start(sc->sc_xfer[SMSC_BULK_DT_WR]);
}

/**
 *	smsc_stop - Stops communication with the LAN95xx chip
 *	@ue: USB ether interface
 *
 *	
 *
 */
static void
smsc_stop(struct usb_ether *ue)
{
	struct smsc_softc *sc = uether_getsc(ue);
	struct ifnet *ifp = uether_getifp(ue);

	SMSC_LOCK_ASSERT(sc, MA_OWNED);

	ifp->if_drv_flags &= ~(IFF_DRV_RUNNING | IFF_DRV_OACTIVE);
	sc->sc_flags &= ~SMSC_FLAG_LINK;

	/*
	 * stop all the transfers, if not already stopped:
	 */
	usbd_transfer_stop(sc->sc_xfer[SMSC_BULK_DT_WR]);
	usbd_transfer_stop(sc->sc_xfer[SMSC_BULK_DT_RD]);
}

/**
 *	smsc_phy_init - Initialises the in-built SMSC phy
 *	@sc: driver soft context
 *
 *	Resets the PHY part of the chip and then initialises it to default
 *	values.  The 'link down' and 'auto-negotiation complete' interrupts
 *	from the PHY are also enabled, however we don't monitor the interrupt
 *	endpoints for the moment.
 *
 *	RETURNS:
 *	Returns 0 on success or EIO if failed to reset the PHY.
 */
static int
smsc_phy_init(struct smsc_softc *sc)
{
	int bmcr;
	usb_ticks_t start_ticks;
	const usb_ticks_t max_ticks = USB_MS_TO_TICKS(1000);

	SMSC_LOCK_ASSERT(sc, MA_OWNED);

	/* Reset phy and wait for reset to complete */
	smsc_miibus_writereg(sc->sc_ue.ue_dev, sc->sc_phyno, MII_BMCR, BMCR_RESET);

	start_ticks = ticks;
	do {
		uether_pause(&sc->sc_ue, hz / 100);
		bmcr = smsc_miibus_readreg(sc->sc_ue.ue_dev, sc->sc_phyno, MII_BMCR);
	} while ((bmcr & BMCR_RESET) && ((ticks - start_ticks) < max_ticks));

	if (((usb_ticks_t)(ticks - start_ticks)) >= max_ticks) {
		smsc_err_printf(sc, "PHY reset timed-out");
		return (EIO);
	}

	smsc_miibus_writereg(sc->sc_ue.ue_dev, sc->sc_phyno, MII_ANAR,
	                     ANAR_10 | ANAR_10_FD | ANAR_TX | ANAR_TX_FD |  /* all modes */
	                     ANAR_CSMA | 
	                     ANAR_FC |
	                     ANAR_PAUSE_ASYM);

	/* Setup the phy to interrupt when the link goes down or autoneg completes */
	smsc_miibus_readreg(sc->sc_ue.ue_dev, sc->sc_phyno, SMSC_PHY_INTR_STAT);
	smsc_miibus_writereg(sc->sc_ue.ue_dev, sc->sc_phyno, SMSC_PHY_INTR_MASK,
	                     (SMSC_PHY_INTR_ANEG_COMP | SMSC_PHY_INTR_LINK_DOWN));
	
	/* Restart auto-negotation */
	bmcr = smsc_miibus_readreg(sc->sc_ue.ue_dev, sc->sc_phyno, MII_BMCR);
	bmcr |= BMCR_STARTNEG;
	smsc_miibus_writereg(sc->sc_ue.ue_dev, sc->sc_phyno, MII_BMCR, bmcr);
	
	return (0);
}


/**
 *	smsc_chip_init - Initialises the chip after power on
 *	@sc: driver soft context
 *
 *	This initialisation sequence is modelled on the procedure in the Linux
 *	driver.
 *
 *	RETURNS:
 *	Returns 0 on success or an error code on failure.
 */
static int
smsc_chip_init(struct smsc_softc *sc)
{
	int err;
	int locked;
	uint32_t reg_val;
	int burst_cap;

	locked = mtx_owned(&sc->sc_mtx);
	if (!locked)
		SMSC_LOCK(sc);

	/* Enter H/W config mode */
	smsc_write_reg(sc, SMSC_HW_CFG, SMSC_HW_CFG_LRST);

	if ((err = smsc_wait_for_bits(sc, SMSC_HW_CFG, SMSC_HW_CFG_LRST)) != 0) {
		smsc_warn_printf(sc, "timed-out waiting for reset to complete\n");
		goto init_failed;
	}

	/* Reset the PHY */
	smsc_write_reg(sc, SMSC_PM_CTRL, SMSC_PM_CTRL_PHY_RST);

	if ((err = smsc_wait_for_bits(sc, SMSC_PM_CTRL, SMSC_PM_CTRL_PHY_RST)) != 0) {
		smsc_warn_printf(sc, "timed-out waiting for phy reset to complete\n");
		goto init_failed;
	}

	/* Set the mac address */
	if ((err = smsc_setmacaddress(sc, sc->sc_ue.ue_eaddr)) != 0) {
		smsc_warn_printf(sc, "failed to set the MAC address\n");
		goto init_failed;
	}

	/* Don't know what the HW_CFG_BIR bit is, but following the reset sequence
	 * as used in the Linux driver.
	 */
	if ((err = smsc_read_reg(sc, SMSC_HW_CFG, &reg_val)) != 0) {
		smsc_warn_printf(sc, "failed to read HW_CFG: %d\n", err);
		goto init_failed;
	}
	reg_val |= SMSC_HW_CFG_BIR;
	smsc_write_reg(sc, SMSC_HW_CFG, reg_val);

	/* There is a so called 'turbo mode' that the linux driver supports, it
	 * seems to allow you to jam multiple frames per Rx transaction.  By default
	 * this driver supports that and therefore allows multiple frames per URB.
	 *
	 * The xfer buffer size needs to reflect this as well, therefore based on
	 * the calculations in the Linux driver the RX bufsize is set to 18944,
	 *     bufsz = (16 * 1024 + 5 * 512)
	 *
	 * Burst capability is the number of URBs that can be in a burst of data/
	 * ethernet frames.
	 */
	if (usbd_get_speed(sc->sc_ue.ue_udev) == USB_SPEED_HIGH)
		burst_cap = 37;
	else
		burst_cap = 128;

	smsc_write_reg(sc, SMSC_BURST_CAP, burst_cap);

	/* Set the default bulk in delay (magic value from Linux driver) */
	smsc_write_reg(sc, SMSC_BULK_IN_DLY, 0x00002000);



	/*
	 * Initialise the RX interface
	 */
	if ((err = smsc_read_reg(sc, SMSC_HW_CFG, &reg_val)) < 0) {
		smsc_warn_printf(sc, "failed to read HW_CFG: (err = %d)\n", err);
		goto init_failed;
	}

	/* Adjust the packet offset in the buffer (designed to try and align IP
	 * header on 4 byte boundary)
	 */
	reg_val &= ~SMSC_HW_CFG_RXDOFF;
	reg_val |= (ETHER_ALIGN << 9) & SMSC_HW_CFG_RXDOFF;
	
	/* The following setings are used for 'turbo mode', a.k.a multiple frames
	 * per Rx transaction (again info taken form Linux driver).
	 */
	reg_val |= (SMSC_HW_CFG_MEF | SMSC_HW_CFG_BCE);

	smsc_write_reg(sc, SMSC_HW_CFG, reg_val);

	/* Clear the status register ? */
	smsc_write_reg(sc, SMSC_INTR_STATUS, 0xffffffff);

	/* Read and display the revision register */
	if ((err = smsc_read_reg(sc, SMSC_ID_REV, &sc->sc_rev_id)) < 0) {
		smsc_warn_printf(sc, "failed to read ID_REV (err = %d)\n", err);
		goto init_failed;
	}

	device_printf(sc->sc_ue.ue_dev, "chip 0x%04lx, rev. %04lx\n", 
	    (sc->sc_rev_id & SMSC_ID_REV_CHIP_ID_MASK) >> 16, 
	    (sc->sc_rev_id & SMSC_ID_REV_CHIP_REV_MASK));

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
	 * Initialise the PHY
	 */
	if ((err = smsc_phy_init(sc)) != 0)
		goto init_failed;


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

	if (!locked)
		SMSC_UNLOCK(sc);

	return (0);
	
init_failed:
	if (!locked)
		SMSC_UNLOCK(sc);

	smsc_err_printf(sc, "smsc_chip_init failed (err=%d)\n", err);
	return (err);
}


/**
 *	smsc_ioctl - ioctl function for the device
 *	@ifp: interface pointer
 *	@cmd: the ioctl command
 *	@data: data passed in the ioctl call, typically a pointer to struct ifreq.
 *	
 *	The ioctl routine is overridden to detect change requests for the H/W
 *	checksum capabilities.
 *
 *	RETURNS:
 *	0 on success and an error code on failure.
 */
static int
smsc_ioctl(struct ifnet *ifp, u_long cmd, caddr_t data)
{
	struct usb_ether *ue = ifp->if_softc;
	struct smsc_softc *sc;
	struct ifreq *ifr;
	int rc;
	int mask;
	int reinit;
	
	if (cmd == SIOCSIFCAP) {

		sc = uether_getsc(ue);
		ifr = (struct ifreq *)data;

		SMSC_LOCK(sc);

		rc = 0;
		reinit = 0;

		mask = ifr->ifr_reqcap ^ ifp->if_capenable;

		/* Modify the RX CSUM enable bits */
		if ((mask & IFCAP_RXCSUM) != 0 &&
		    (ifp->if_capabilities & IFCAP_RXCSUM) != 0) {
			ifp->if_capenable ^= IFCAP_RXCSUM;
			
			if (ifp->if_drv_flags & IFF_DRV_RUNNING) {
				ifp->if_drv_flags &= ~IFF_DRV_RUNNING;
				reinit = 1;
			}
		}
		
		SMSC_UNLOCK(sc);
		if (reinit)
#if __FreeBSD_version > 1000000
			uether_init(ue);
#else
			ifp->if_init(ue);
#endif

	} else {
		rc = uether_ioctl(ifp, cmd, data);
	}

	return (rc);
}

#ifdef FDT
/*
 * This is FreeBSD-specific compatibility strings for RPi/RPi2
 */
static phandle_t
smsc_fdt_find_eth_node(phandle_t start)
{
	phandle_t child, node;

	/* Traverse through entire tree to find usb ethernet nodes. */
	for (node = OF_child(start); node != 0; node = OF_peer(node)) {
		if ((ofw_bus_node_is_compatible(node, "net,ethernet") &&
		    ofw_bus_node_is_compatible(node, "usb,device")) ||
		    ofw_bus_node_is_compatible(node, "usb424,ec00"))
			return (node);
		child = smsc_fdt_find_eth_node(node);
		if (child != -1)
			return (child);
	}

	return (-1);
}

/*
 * Check if node's path is <*>/usb/hub/ethernet
 */
static int
smsc_fdt_is_usb_eth(phandle_t node)
{
	char name[16];
	int len;

	memset(name, 0, sizeof(name));
	len = OF_getprop(node, "name", name, sizeof(name));
	if (len <= 0)
		return (0);

	if (strcmp(name, "ethernet"))
		return (0);

	node = OF_parent(node);
	if (node == -1)
		return (0);
	len = OF_getprop(node, "name", name, sizeof(name));
	if (len <= 0)
		return (0);

	if (strcmp(name, "hub"))
		return (0);

	node = OF_parent(node);
	if (node == -1)
		return (0);
	len = OF_getprop(node, "name", name, sizeof(name));
	if (len <= 0)
		return (0);

	if (strcmp(name, "usb"))
		return (0);

	return (1);
}

static phandle_t
smsc_fdt_find_eth_node_by_path(phandle_t start)
{
	phandle_t child, node;

	/* Traverse through entire tree to find usb ethernet nodes. */
	for (node = OF_child(start); node != 0; node = OF_peer(node)) {
		if (smsc_fdt_is_usb_eth(node))
			return (node);
		child = smsc_fdt_find_eth_node_by_path(node);
		if (child != -1)
			return (child);
	}

	return (-1);
}

/*
 * Look through known names that can contain mac address
 * return 0 if valid MAC address has been found
 */
static int
smsc_fdt_read_mac_property(phandle_t node, unsigned char *mac)
{
	int len;

	/* Check if there is property */
	if ((len = OF_getproplen(node, "local-mac-address")) > 0) {
		if (len != ETHER_ADDR_LEN)
			return (EINVAL);

		OF_getprop(node, "local-mac-address", mac,
		    ETHER_ADDR_LEN);
		return (0);
	}

	if ((len = OF_getproplen(node, "mac-address")) > 0) {
		if (len != ETHER_ADDR_LEN)
			return (EINVAL);

		OF_getprop(node, "mac-address", mac,
		    ETHER_ADDR_LEN);
		return (0);
	}

	return (ENXIO);
}

/**
 * Get MAC address from FDT blob.  Firmware or loader should fill
 * mac-address or local-mac-address property.  Returns 0 if MAC address
 * obtained, error code otherwise.
 */
static int
smsc_fdt_find_mac(unsigned char *mac)
{
	phandle_t node, root;

	root = OF_finddevice("/");
	node = smsc_fdt_find_eth_node(root);
	if (node != -1) {
		if (smsc_fdt_read_mac_property(node, mac) == 0)
			return (0);
	}

	/*
	 * If it's not FreeBSD FDT blob for RPi, try more
	 *     generic .../usb/hub/ethernet
	 */
	node = smsc_fdt_find_eth_node_by_path(root);

	if (node != -1)
		return smsc_fdt_read_mac_property(node, mac);

	return (ENXIO);
}
#endif

/**
 *	smsc_attach_post - Called after the driver attached to the USB interface
 *	@ue: the USB ethernet device
 *
 *	This is where the chip is intialised for the first time.  This is different
 *	from the smsc_init() function in that that one is designed to setup the
 *	H/W to match the UE settings and can be called after a reset.
 *
 *
 */
static void
smsc_attach_post(struct usb_ether *ue)
{
	struct smsc_softc *sc = uether_getsc(ue);
	uint32_t mac_h, mac_l;
	int err;

	smsc_dbg_printf(sc, "smsc_attach_post\n");

	/* Setup some of the basics */
	sc->sc_phyno = 1;


	/* Attempt to get the mac address, if an EEPROM is not attached this
	 * will just return FF:FF:FF:FF:FF:FF, so in such cases we invent a MAC
	 * address based on urandom.
	 */
	memset(sc->sc_ue.ue_eaddr, 0xff, ETHER_ADDR_LEN);
	
	/* Check if there is already a MAC address in the register */
	if ((smsc_read_reg(sc, SMSC_MAC_ADDRL, &mac_l) == 0) &&
	    (smsc_read_reg(sc, SMSC_MAC_ADDRH, &mac_h) == 0)) {
		sc->sc_ue.ue_eaddr[5] = (uint8_t)((mac_h >> 8) & 0xff);
		sc->sc_ue.ue_eaddr[4] = (uint8_t)((mac_h) & 0xff);
		sc->sc_ue.ue_eaddr[3] = (uint8_t)((mac_l >> 24) & 0xff);
		sc->sc_ue.ue_eaddr[2] = (uint8_t)((mac_l >> 16) & 0xff);
		sc->sc_ue.ue_eaddr[1] = (uint8_t)((mac_l >> 8) & 0xff);
		sc->sc_ue.ue_eaddr[0] = (uint8_t)((mac_l) & 0xff);
	}
	
	/* MAC address is not set so try to read from EEPROM, if that fails generate
	 * a random MAC address.
	 */
	if (!ETHER_IS_VALID(sc->sc_ue.ue_eaddr)) {

		err = smsc_eeprom_read(sc, 0x01, sc->sc_ue.ue_eaddr, ETHER_ADDR_LEN);
#ifdef FDT
		if ((err != 0) || (!ETHER_IS_VALID(sc->sc_ue.ue_eaddr)))
			err = smsc_fdt_find_mac(sc->sc_ue.ue_eaddr);
#endif
		if ((err != 0) || (!ETHER_IS_VALID(sc->sc_ue.ue_eaddr))) {
			read_random(sc->sc_ue.ue_eaddr, ETHER_ADDR_LEN);
			sc->sc_ue.ue_eaddr[0] &= ~0x01;     /* unicast */
			sc->sc_ue.ue_eaddr[0] |=  0x02;     /* locally administered */
		}
	}
	
	/* Initialise the chip for the first time */
	smsc_chip_init(sc);
}


/**
 *	smsc_attach_post_sub - Called after the driver attached to the USB interface
 *	@ue: the USB ethernet device
 *
 *	Most of this is boilerplate code and copied from the base USB ethernet
 *	driver.  It has been overriden so that we can indicate to the system that
 *	the chip supports H/W checksumming.
 *
 *	RETURNS:
 *	Returns 0 on success or a negative error code.
 */
#if __FreeBSD_version > 1000000
static int
smsc_attach_post_sub(struct usb_ether *ue)
{
	struct smsc_softc *sc;
	struct ifnet *ifp;
	int error;

	sc = uether_getsc(ue);
	ifp = ue->ue_ifp;
	ifp->if_flags = IFF_BROADCAST | IFF_SIMPLEX | IFF_MULTICAST;
	ifp->if_start = uether_start;
	ifp->if_ioctl = smsc_ioctl;
	ifp->if_init = uether_init;
	IFQ_SET_MAXLEN(&ifp->if_snd, ifqmaxlen);
	ifp->if_snd.ifq_drv_maxlen = ifqmaxlen;
	IFQ_SET_READY(&ifp->if_snd);

	/* The chip supports TCP/UDP checksum offloading on TX and RX paths, however
	 * currently only RX checksum is supported in the driver (see top of file).
	 */
	ifp->if_capabilities |= IFCAP_RXCSUM | IFCAP_VLAN_MTU;
	ifp->if_hwassist = 0;
	
	/* TX checksuming is disabled (for now?)
	ifp->if_capabilities |= IFCAP_TXCSUM;
	ifp->if_capenable |= IFCAP_TXCSUM;
	ifp->if_hwassist = CSUM_TCP | CSUM_UDP;
	*/

	ifp->if_capenable = ifp->if_capabilities;

	mtx_lock(&Giant);
	error = mii_attach(ue->ue_dev, &ue->ue_miibus, ifp,
	    uether_ifmedia_upd, ue->ue_methods->ue_mii_sts,
	    BMSR_DEFCAPMASK, sc->sc_phyno, MII_OFFSET_ANY, 0);
	mtx_unlock(&Giant);

	return (error);
}
#endif /* __FreeBSD_version > 1000000 */


/**
 *	smsc_probe - Probe the interface. 
 *	@dev: smsc device handle
 *
 *	Checks if the device is a match for this driver.
 *
 *	RETURNS:
 *	Returns 0 on success or an error code on failure.
 */
static int
smsc_probe(device_t dev)
{
	struct usb_attach_arg *uaa = device_get_ivars(dev);

	if (uaa->usb_mode != USB_MODE_HOST)
		return (ENXIO);
	if (uaa->info.bConfigIndex != SMSC_CONFIG_INDEX)
		return (ENXIO);
	if (uaa->info.bIfaceIndex != SMSC_IFACE_IDX)
		return (ENXIO);

	return (usbd_lookup_id_by_uaa(smsc_devs, sizeof(smsc_devs), uaa));
}


/**
 *	smsc_attach - Attach the interface. 
 *	@dev: smsc device handle
 *
 *	Allocate softc structures, do ifmedia setup and ethernet/BPF attach.
 *
 *	RETURNS:
 *	Returns 0 on success or a negative error code.
 */
static int
smsc_attach(device_t dev)
{
	struct usb_attach_arg *uaa = device_get_ivars(dev);
	struct smsc_softc *sc = device_get_softc(dev);
	struct usb_ether *ue = &sc->sc_ue;
	uint8_t iface_index;
	int err;

	sc->sc_flags = USB_GET_DRIVER_INFO(uaa);

	device_set_usb_desc(dev);

	mtx_init(&sc->sc_mtx, device_get_nameunit(dev), NULL, MTX_DEF);

	/* Setup the endpoints for the SMSC LAN95xx device(s) */
	iface_index = SMSC_IFACE_IDX;
	err = usbd_transfer_setup(uaa->device, &iface_index, sc->sc_xfer,
	                          smsc_config, SMSC_N_TRANSFER, sc, &sc->sc_mtx);
	if (err) {
		device_printf(dev, "error: allocating USB transfers failed\n");
		goto detach;
	}

	ue->ue_sc = sc;
	ue->ue_dev = dev;
	ue->ue_udev = uaa->device;
	ue->ue_mtx = &sc->sc_mtx;
	ue->ue_methods = &smsc_ue_methods;

	err = uether_ifattach(ue);
	if (err) {
		device_printf(dev, "error: could not attach interface\n");
		goto detach;
	}
	return (0);			/* success */

detach:
	smsc_detach(dev);
	return (ENXIO);		/* failure */
}

/**
 *	smsc_detach - Detach the interface. 
 *	@dev: smsc device handle
 *
 *	RETURNS:
 *	Returns 0.
 */
static int
smsc_detach(device_t dev)
{
	struct smsc_softc *sc = device_get_softc(dev);
	struct usb_ether *ue = &sc->sc_ue;

	usbd_transfer_unsetup(sc->sc_xfer, SMSC_N_TRANSFER);
	uether_ifdetach(ue);
	mtx_destroy(&sc->sc_mtx);

	return (0);
}

static device_method_t smsc_methods[] = {
	/* Device interface */
	DEVMETHOD(device_probe, smsc_probe),
	DEVMETHOD(device_attach, smsc_attach),
	DEVMETHOD(device_detach, smsc_detach),

	/* bus interface */
	DEVMETHOD(bus_print_child, bus_generic_print_child),
	DEVMETHOD(bus_driver_added, bus_generic_driver_added),

	/* MII interface */
	DEVMETHOD(miibus_readreg, smsc_miibus_readreg),
	DEVMETHOD(miibus_writereg, smsc_miibus_writereg),
	DEVMETHOD(miibus_statchg, smsc_miibus_statchg),

	DEVMETHOD_END
};

static driver_t smsc_driver = {
	.name = "smsc",
	.methods = smsc_methods,
	.size = sizeof(struct smsc_softc),
};

static devclass_t smsc_devclass;

DRIVER_MODULE(smsc, uhub, smsc_driver, smsc_devclass, NULL, 0);
DRIVER_MODULE(miibus, smsc, miibus_driver, miibus_devclass, 0, 0);
MODULE_DEPEND(smsc, uether, 1, 1, 1);
MODULE_DEPEND(smsc, usb, 1, 1, 1);
MODULE_DEPEND(smsc, ether, 1, 1, 1);
MODULE_DEPEND(smsc, miibus, 1, 1, 1);
MODULE_VERSION(smsc, 1);
USB_PNP_HOST_INFO(smsc_devs);
