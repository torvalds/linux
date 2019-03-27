/*-
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD
 *
 * Copyright (C) 2012 Ben Gray <bgray@freebsd.org>.
 * Copyright (C) 2018 The FreeBSD Foundation.
 *
 * This software was developed by Arshan Khanifar <arshankhanifar@gmail.com>
 * under sponsorship from the FreeBSD Foundation.
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
 *
 * $FreeBSD$
 */

#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");

/*
 * USB-To-Ethernet adapter driver for Microchip's LAN78XX and related families.
 *
 * USB 3.1 to 10/100/1000 Mbps Ethernet
 * LAN7800 http://www.microchip.com/wwwproducts/en/LAN7800
 *
 * USB 2.0 to 10/100/1000 Mbps Ethernet
 * LAN7850 http://www.microchip.com/wwwproducts/en/LAN7850
 *
 * USB 2 to 10/100/1000 Mbps Ethernet with built-in USB hub
 * LAN7515 (no datasheet available, but probes and functions as LAN7800)
 *
 * This driver is based on the if_smsc driver, with lan78xx-specific
 * functionality modelled on Microchip's Linux lan78xx driver.
 *
 * UNIMPLEMENTED FEATURES
 * ------------------
 * A number of features supported by the lan78xx are not yet implemented in
 * this driver:
 *
 * - RX/TX checksum offloading: Nothing has been implemented yet for
 *   TX checksumming. RX checksumming works with ICMP messages, but is broken
 *   for TCP/UDP packets.
 * - Direct address translation filtering: Implemented but untested.
 * - VLAN tag removal.
 * - Support for USB interrupt endpoints.
 * - Latency Tolerance Messaging (LTM) support.
 * - TCP LSO support.
 *
 */

#include <sys/param.h>
#include <sys/bus.h>
#include <sys/callout.h>
#include <sys/condvar.h>
#include <sys/kernel.h>
#include <sys/lock.h>
#include <sys/malloc.h>
#include <sys/module.h>
#include <sys/mutex.h>
#include <sys/priv.h>
#include <sys/queue.h>
#include <sys/random.h>
#include <sys/socket.h>
#include <sys/stddef.h>
#include <sys/stdint.h>
#include <sys/sx.h>
#include <sys/sysctl.h>
#include <sys/systm.h>
#include <sys/unistd.h>

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

#define USB_DEBUG_VAR lan78xx_debug
#include <dev/usb/usb_debug.h>
#include <dev/usb/usb_process.h>

#include <dev/usb/net/usb_ethernet.h>

#include <dev/usb/net/if_mugereg.h>

#ifdef USB_DEBUG
static int muge_debug = 0;

SYSCTL_NODE(_hw_usb, OID_AUTO, muge, CTLFLAG_RW, 0,
    "Microchip LAN78xx USB-GigE");
SYSCTL_INT(_hw_usb_muge, OID_AUTO, debug, CTLFLAG_RWTUN, &muge_debug, 0,
    "Debug level");
#endif

#define MUGE_DEFAULT_RX_CSUM_ENABLE (false)
#define MUGE_DEFAULT_TX_CSUM_ENABLE (false)
#define MUGE_DEFAULT_TSO_CSUM_ENABLE (false)

/* Supported Vendor and Product IDs. */
static const struct usb_device_id lan78xx_devs[] = {
#define MUGE_DEV(p,i) { USB_VPI(USB_VENDOR_SMC2, USB_PRODUCT_SMC2_##p, i) }
	MUGE_DEV(LAN7800_ETH, 0),
	MUGE_DEV(LAN7801_ETH, 0),
	MUGE_DEV(LAN7850_ETH, 0),
#undef MUGE_DEV
};

#ifdef USB_DEBUG
#define muge_dbg_printf(sc, fmt, args...) \
do { \
	if (muge_debug > 0) \
		device_printf((sc)->sc_ue.ue_dev, "debug: " fmt, ##args); \
} while(0)
#else
#define muge_dbg_printf(sc, fmt, args...) do { } while (0)
#endif

#define muge_warn_printf(sc, fmt, args...) \
	device_printf((sc)->sc_ue.ue_dev, "warning: " fmt, ##args)

#define muge_err_printf(sc, fmt, args...) \
	device_printf((sc)->sc_ue.ue_dev, "error: " fmt, ##args)

#define ETHER_IS_ZERO(addr) \
	(!(addr[0] | addr[1] | addr[2] | addr[3] | addr[4] | addr[5]))

#define ETHER_IS_VALID(addr) \
	(!ETHER_IS_MULTICAST(addr) && !ETHER_IS_ZERO(addr))

/* USB endpoints. */

enum {
	MUGE_BULK_DT_RD,
	MUGE_BULK_DT_WR,
#if 0 /* Ignore interrupt endpoints for now as we poll on MII status. */
	MUGE_INTR_DT_WR,
	MUGE_INTR_DT_RD,
#endif
	MUGE_N_TRANSFER,
};

struct muge_softc {
	struct usb_ether	sc_ue;
	struct mtx		sc_mtx;
	struct usb_xfer		*sc_xfer[MUGE_N_TRANSFER];
	int			sc_phyno;

	/* Settings for the mac control (MAC_CSR) register. */
	uint32_t		sc_rfe_ctl;
	uint32_t		sc_mdix_ctl;
	uint16_t		chipid;
	uint16_t		chiprev;
	uint32_t		sc_mchash_table[ETH_DP_SEL_VHF_HASH_LEN];
	uint32_t		sc_pfilter_table[MUGE_NUM_PFILTER_ADDRS_][2];

	uint32_t		sc_flags;
#define	MUGE_FLAG_LINK		0x0001
#define	MUGE_FLAG_INIT_DONE	0x0002
};

#define MUGE_IFACE_IDX		0

#define MUGE_LOCK(_sc)			mtx_lock(&(_sc)->sc_mtx)
#define MUGE_UNLOCK(_sc)		mtx_unlock(&(_sc)->sc_mtx)
#define MUGE_LOCK_ASSERT(_sc, t)	mtx_assert(&(_sc)->sc_mtx, t)

static device_probe_t muge_probe;
static device_attach_t muge_attach;
static device_detach_t muge_detach;

static usb_callback_t muge_bulk_read_callback;
static usb_callback_t muge_bulk_write_callback;

static miibus_readreg_t lan78xx_miibus_readreg;
static miibus_writereg_t lan78xx_miibus_writereg;
static miibus_statchg_t lan78xx_miibus_statchg;

static int muge_attach_post_sub(struct usb_ether *ue);
static uether_fn_t muge_attach_post;
static uether_fn_t muge_init;
static uether_fn_t muge_stop;
static uether_fn_t muge_start;
static uether_fn_t muge_tick;
static uether_fn_t muge_setmulti;
static uether_fn_t muge_setpromisc;

static int muge_ifmedia_upd(struct ifnet *);
static void muge_ifmedia_sts(struct ifnet *, struct ifmediareq *);

static int lan78xx_chip_init(struct muge_softc *sc);
static int muge_ioctl(struct ifnet *ifp, u_long cmd, caddr_t data);

static const struct usb_config muge_config[MUGE_N_TRANSFER] = {

	[MUGE_BULK_DT_WR] = {
		.type = UE_BULK,
		.endpoint = UE_ADDR_ANY,
		.direction = UE_DIR_OUT,
		.frames = 16,
		.bufsize = 16 * (MCLBYTES + 16),
		.flags = {.pipe_bof = 1,.force_short_xfer = 1,},
		.callback = muge_bulk_write_callback,
		.timeout = 10000,	/* 10 seconds */
	},

	[MUGE_BULK_DT_RD] = {
		.type = UE_BULK,
		.endpoint = UE_ADDR_ANY,
		.direction = UE_DIR_IN,
		.bufsize = 20480,	/* bytes */
		.flags = {.pipe_bof = 1,.short_xfer_ok = 1,},
		.callback = muge_bulk_read_callback,
		.timeout = 0,	/* no timeout */
	},
	/*
	 * The chip supports interrupt endpoints, however they aren't
	 * needed as we poll on the MII status.
	 */
};

static const struct usb_ether_methods muge_ue_methods = {
	.ue_attach_post = muge_attach_post,
	.ue_attach_post_sub = muge_attach_post_sub,
	.ue_start = muge_start,
	.ue_ioctl = muge_ioctl,
	.ue_init = muge_init,
	.ue_stop = muge_stop,
	.ue_tick = muge_tick,
	.ue_setmulti = muge_setmulti,
	.ue_setpromisc = muge_setpromisc,
	.ue_mii_upd = muge_ifmedia_upd,
	.ue_mii_sts = muge_ifmedia_sts,
};

/**
 *	lan78xx_read_reg - Read a 32-bit register on the device
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
lan78xx_read_reg(struct muge_softc *sc, uint32_t off, uint32_t *data)
{
	struct usb_device_request req;
	uint32_t buf;
	usb_error_t err;

	MUGE_LOCK_ASSERT(sc, MA_OWNED);

	req.bmRequestType = UT_READ_VENDOR_DEVICE;
	req.bRequest = UVR_READ_REG;
	USETW(req.wValue, 0);
	USETW(req.wIndex, off);
	USETW(req.wLength, 4);

	err = uether_do_request(&sc->sc_ue, &req, &buf, 1000);
	if (err != 0)
		muge_warn_printf(sc, "Failed to read register 0x%0x\n", off);
	*data = le32toh(buf);
	return (err);
}

/**
 *	lan78xx_write_reg - Write a 32-bit register on the device
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
lan78xx_write_reg(struct muge_softc *sc, uint32_t off, uint32_t data)
{
	struct usb_device_request req;
	uint32_t buf;
	usb_error_t err;

	MUGE_LOCK_ASSERT(sc, MA_OWNED);

	buf = htole32(data);

	req.bmRequestType = UT_WRITE_VENDOR_DEVICE;
	req.bRequest = UVR_WRITE_REG;
	USETW(req.wValue, 0);
	USETW(req.wIndex, off);
	USETW(req.wLength, 4);

	err = uether_do_request(&sc->sc_ue, &req, &buf, 1000);
	if (err != 0)
		muge_warn_printf(sc, "Failed to write register 0x%0x\n", off);
	return (err);
}

/**
 *	lan78xx_wait_for_bits - Poll on a register value until bits are cleared
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
lan78xx_wait_for_bits(struct muge_softc *sc, uint32_t reg, uint32_t bits)
{
	usb_ticks_t start_ticks;
	const usb_ticks_t max_ticks = USB_MS_TO_TICKS(1000);
	uint32_t val;
	int err;

	MUGE_LOCK_ASSERT(sc, MA_OWNED);

	start_ticks = (usb_ticks_t)ticks;
	do {
		if ((err = lan78xx_read_reg(sc, reg, &val)) != 0)
			return (err);
		if (!(val & bits))
			return (0);
		uether_pause(&sc->sc_ue, hz / 100);
	} while (((usb_ticks_t)(ticks - start_ticks)) < max_ticks);

	return (USB_ERR_TIMEOUT);
}

/**
 *	lan78xx_eeprom_read_raw - Read the attached EEPROM
 *	@sc: soft context
 *	@off: the eeprom address offset
 *	@buf: stores the bytes
 *	@buflen: the number of bytes to read
 *
 *	Simply reads bytes from an attached eeprom.
 *
 *	LOCKING:
 *	The function takes and releases the device lock if not already held.
 *
 *	RETURNS:
 *	0 on success, or a USB_ERR_?? error code on failure.
 */
static int
lan78xx_eeprom_read_raw(struct muge_softc *sc, uint16_t off, uint8_t *buf,
    uint16_t buflen)
{
	usb_ticks_t start_ticks;
	const usb_ticks_t max_ticks = USB_MS_TO_TICKS(1000);
	int err, locked;
	uint32_t val, saved;
	uint16_t i;

	locked = mtx_owned(&sc->sc_mtx); /* XXX */
	if (!locked)
		MUGE_LOCK(sc);

	if (sc->chipid == ETH_ID_REV_CHIP_ID_7800_) {
		/* EEDO/EECLK muxed with LED0/LED1 on LAN7800. */
		err = lan78xx_read_reg(sc, ETH_HW_CFG, &val);
		saved = val;

		val &= ~(ETH_HW_CFG_LEDO_EN_ | ETH_HW_CFG_LED1_EN_);
		err = lan78xx_write_reg(sc, ETH_HW_CFG, val);
	}

	err = lan78xx_wait_for_bits(sc, ETH_E2P_CMD, ETH_E2P_CMD_BUSY_);
	if (err != 0) {
		muge_warn_printf(sc, "eeprom busy, failed to read data\n");
		goto done;
	}

	/* Start reading the bytes, one at a time. */
	for (i = 0; i < buflen; i++) {
		val = ETH_E2P_CMD_BUSY_ | ETH_E2P_CMD_READ_;
		val |= (ETH_E2P_CMD_ADDR_MASK_ & (off + i));
		if ((err = lan78xx_write_reg(sc, ETH_E2P_CMD, val)) != 0)
			goto done;

		start_ticks = (usb_ticks_t)ticks;
		do {
			if ((err = lan78xx_read_reg(sc, ETH_E2P_CMD, &val)) !=
			    0)
				goto done;
			if (!(val & ETH_E2P_CMD_BUSY_) ||
			    (val & ETH_E2P_CMD_TIMEOUT_))
				break;

			uether_pause(&sc->sc_ue, hz / 100);
		} while (((usb_ticks_t)(ticks - start_ticks)) < max_ticks);

		if (val & (ETH_E2P_CMD_BUSY_ | ETH_E2P_CMD_TIMEOUT_)) {
			muge_warn_printf(sc, "eeprom command failed\n");
			err = USB_ERR_IOERROR;
			break;
		}

		if ((err = lan78xx_read_reg(sc, ETH_E2P_DATA, &val)) != 0)
			goto done;

		buf[i] = (val & 0xff);
	}

done:
	if (!locked)
		MUGE_UNLOCK(sc);
	if (sc->chipid == ETH_ID_REV_CHIP_ID_7800_) {
		/* Restore saved LED configuration. */
		lan78xx_write_reg(sc, ETH_HW_CFG, saved);
	}
	return (err);
}

static bool
lan78xx_eeprom_present(struct muge_softc *sc)
{
	int ret;
	uint8_t sig;

	ret = lan78xx_eeprom_read_raw(sc, ETH_E2P_INDICATOR_OFFSET, &sig, 1);
	return (ret == 0 && sig == ETH_E2P_INDICATOR);
}

/**
 *	lan78xx_otp_read_raw
 *	@sc: soft context
 *	@off: the otp address offset
 *	@buf: stores the bytes
 *	@buflen: the number of bytes to read
 *
 *	Simply reads bytes from the OTP.
 *
 *	LOCKING:
 *	The function takes and releases the device lock if not already held.
 *
 *	RETURNS:
 *	0 on success, or a USB_ERR_?? error code on failure.
 *
 */
static int
lan78xx_otp_read_raw(struct muge_softc *sc, uint16_t off, uint8_t *buf,
    uint16_t buflen)
{
	int locked, err;
	uint32_t val;
	uint16_t i;
	locked = mtx_owned(&sc->sc_mtx);
	if (!locked)
		MUGE_LOCK(sc);

	err = lan78xx_read_reg(sc, OTP_PWR_DN, &val);

	/* Checking if bit is set. */
	if (val & OTP_PWR_DN_PWRDN_N) {
		/* Clear it, then wait for it to be cleared. */
		lan78xx_write_reg(sc, OTP_PWR_DN, 0);
		err = lan78xx_wait_for_bits(sc, OTP_PWR_DN, OTP_PWR_DN_PWRDN_N);
		if (err != 0) {
			muge_warn_printf(sc, "OTP off? failed to read data\n");
			goto done;
		}
	}
	/* Start reading the bytes, one at a time. */
	for (i = 0; i < buflen; i++) {
		err = lan78xx_write_reg(sc, OTP_ADDR1,
		    ((off + i) >> 8) & OTP_ADDR1_15_11);
		err = lan78xx_write_reg(sc, OTP_ADDR2,
		    ((off + i) & OTP_ADDR2_10_3));
		err = lan78xx_write_reg(sc, OTP_FUNC_CMD, OTP_FUNC_CMD_READ_);
		err = lan78xx_write_reg(sc, OTP_CMD_GO, OTP_CMD_GO_GO_);

		err = lan78xx_wait_for_bits(sc, OTP_STATUS, OTP_STATUS_BUSY_);
		if (err != 0) {
			muge_warn_printf(sc, "OTP busy failed to read data\n");
			goto done;
		}

		if ((err = lan78xx_read_reg(sc, OTP_RD_DATA, &val)) != 0)
			goto done;

		buf[i] = (uint8_t)(val & 0xff);
	}

done:
	if (!locked)
		MUGE_UNLOCK(sc);
	return (err);
}

/**
 *	lan78xx_otp_read
 *	@sc: soft context
 *	@off: the otp address offset
 *	@buf: stores the bytes
 *	@buflen: the number of bytes to read
 *
 *	Simply reads bytes from the otp.
 *
 *	LOCKING:
 *	The function takes and releases device lock if it is not already held.
 *
 *	RETURNS:
 *	0 on success, or a USB_ERR_?? error code on failure.
 */
static int
lan78xx_otp_read(struct muge_softc *sc, uint16_t off, uint8_t *buf,
    uint16_t buflen)
{
	uint8_t sig;
	int err;

	err = lan78xx_otp_read_raw(sc, OTP_INDICATOR_OFFSET, &sig, 1);
	if (err == 0) {
		if (sig == OTP_INDICATOR_1) {
		} else if (sig == OTP_INDICATOR_2) {
			off += 0x100; /* XXX */
		} else {
			err = -EINVAL;
		}
		if (!err)
			err = lan78xx_otp_read_raw(sc, off, buf, buflen);
	}
	return (err);
}

/**
 *	lan78xx_setmacaddress - Set the mac address in the device
 *	@sc: driver soft context
 *	@addr: pointer to array contain at least 6 bytes of the mac
 *
 *	LOCKING:
 *	Should be called with the MUGE lock held.
 *
 *	RETURNS:
 *	Returns 0 on success or a negative error code.
 */
static int
lan78xx_setmacaddress(struct muge_softc *sc, const uint8_t *addr)
{
	int err;
	uint32_t val;

	muge_dbg_printf(sc,
	    "setting mac address to %02x:%02x:%02x:%02x:%02x:%02x\n",
	    addr[0], addr[1], addr[2], addr[3], addr[4], addr[5]);

	MUGE_LOCK_ASSERT(sc, MA_OWNED);

	val = (addr[3] << 24) | (addr[2] << 16) | (addr[1] << 8) | addr[0];
	if ((err = lan78xx_write_reg(sc, ETH_RX_ADDRL, val)) != 0)
		goto done;

	val = (addr[5] << 8) | addr[4];
	err = lan78xx_write_reg(sc, ETH_RX_ADDRH, val);

done:
	return (err);
}

/**
 *	lan78xx_set_rx_max_frame_length
 *	@sc: driver soft context
 *	@size: pointer to array contain at least 6 bytes of the mac
 *
 *	Sets the maximum frame length to be received. Frames bigger than
 *	this size are aborted.
 *
 *	RETURNS:
 *	Returns 0 on success or a negative error code.
 */
static int
lan78xx_set_rx_max_frame_length(struct muge_softc *sc, int size)
{
	int err = 0;
	uint32_t buf;
	bool rxenabled;

	/* First we have to disable rx before changing the length. */
	err = lan78xx_read_reg(sc, ETH_MAC_RX, &buf);
	rxenabled = ((buf & ETH_MAC_RX_EN_) != 0);

	if (rxenabled) {
		buf &= ~ETH_MAC_RX_EN_;
		err = lan78xx_write_reg(sc, ETH_MAC_RX, buf);
	}

	/* Setting max frame length. */
	buf &= ~ETH_MAC_RX_MAX_FR_SIZE_MASK_;
	buf |= (((size + 4) << ETH_MAC_RX_MAX_FR_SIZE_SHIFT_) &
	    ETH_MAC_RX_MAX_FR_SIZE_MASK_);
	err = lan78xx_write_reg(sc, ETH_MAC_RX, buf);

	/* If it were enabled before, we enable it back. */

	if (rxenabled) {
		buf |= ETH_MAC_RX_EN_;
		err = lan78xx_write_reg(sc, ETH_MAC_RX, buf);
	}

	return (0);
}

/**
 *	lan78xx_miibus_readreg - Read a MII/MDIO register
 *	@dev: usb ether device
 *	@phy: the number of phy reading from
 *	@reg: the register address
 *
 *	LOCKING:
 *	Takes and releases the device mutex lock if not already held.
 *
 *	RETURNS:
 *	Returns the 16-bits read from the MII register, if this function fails
 *	0 is returned.
 */
static int
lan78xx_miibus_readreg(device_t dev, int phy, int reg) {

	struct muge_softc *sc = device_get_softc(dev);
	int locked;
	uint32_t addr, val;

	val = 0;
	locked = mtx_owned(&sc->sc_mtx);
	if (!locked)
		MUGE_LOCK(sc);

	if (lan78xx_wait_for_bits(sc, ETH_MII_ACC, ETH_MII_ACC_MII_BUSY_) !=
	    0) {
		muge_warn_printf(sc, "MII is busy\n");
		goto done;
	}

	addr = (phy << 11) | (reg << 6) |
	    ETH_MII_ACC_MII_READ_ | ETH_MII_ACC_MII_BUSY_;
	lan78xx_write_reg(sc, ETH_MII_ACC, addr);

	if (lan78xx_wait_for_bits(sc, ETH_MII_ACC, ETH_MII_ACC_MII_BUSY_) !=
	    0) {
		muge_warn_printf(sc, "MII read timeout\n");
		goto done;
	}

	lan78xx_read_reg(sc, ETH_MII_DATA, &val);
	val = le32toh(val);

done:
	if (!locked)
		MUGE_UNLOCK(sc);

	return (val & 0xFFFF);
}

/**
 *	lan78xx_miibus_writereg - Writes a MII/MDIO register
 *	@dev: usb ether device
 *	@phy: the number of phy writing to
 *	@reg: the register address
 *	@val: the value to write
 *
 *	Attempts to write a PHY register through the usb controller registers.
 *
 *	LOCKING:
 *	Takes and releases the device mutex lock if not already held.
 *
 *	RETURNS:
 *	Always returns 0 regardless of success or failure.
 */
static int
lan78xx_miibus_writereg(device_t dev, int phy, int reg, int val)
{
	struct muge_softc *sc = device_get_softc(dev);
	int locked;
	uint32_t addr;

	if (sc->sc_phyno != phy)
		return (0);

	locked = mtx_owned(&sc->sc_mtx);
	if (!locked)
		MUGE_LOCK(sc);

	if (lan78xx_wait_for_bits(sc, ETH_MII_ACC, ETH_MII_ACC_MII_BUSY_) !=
	    0) {
		muge_warn_printf(sc, "MII is busy\n");
		goto done;
	}

	val = htole32(val);
	lan78xx_write_reg(sc, ETH_MII_DATA, val);

	addr = (phy << 11) | (reg << 6) |
	    ETH_MII_ACC_MII_WRITE_ | ETH_MII_ACC_MII_BUSY_;
	lan78xx_write_reg(sc, ETH_MII_ACC, addr);

	if (lan78xx_wait_for_bits(sc, ETH_MII_ACC, ETH_MII_ACC_MII_BUSY_) != 0)
		muge_warn_printf(sc, "MII write timeout\n");

done:
	if (!locked)
		MUGE_UNLOCK(sc);
	return (0);
}

/*
 *	lan78xx_miibus_statchg - Called to detect phy status change
 *	@dev: usb ether device
 *
 *	This function is called periodically by the system to poll for status
 *	changes of the link.
 *
 *	LOCKING:
 *	Takes and releases the device mutex lock if not already held.
 */
static void
lan78xx_miibus_statchg(device_t dev)
{
	struct muge_softc *sc = device_get_softc(dev);
	struct mii_data *mii = uether_getmii(&sc->sc_ue);
	struct ifnet *ifp;
	int locked;
	int err;
	uint32_t flow = 0;
	uint32_t fct_flow = 0;

	locked = mtx_owned(&sc->sc_mtx);
	if (!locked)
		MUGE_LOCK(sc);

	ifp = uether_getifp(&sc->sc_ue);
	if (mii == NULL || ifp == NULL ||
	    (ifp->if_drv_flags & IFF_DRV_RUNNING) == 0)
		goto done;

	/* Use the MII status to determine link status */
	sc->sc_flags &= ~MUGE_FLAG_LINK;
	if ((mii->mii_media_status & (IFM_ACTIVE | IFM_AVALID)) ==
	    (IFM_ACTIVE | IFM_AVALID)) {
		muge_dbg_printf(sc, "media is active\n");
		switch (IFM_SUBTYPE(mii->mii_media_active)) {
		case IFM_10_T:
		case IFM_100_TX:
			sc->sc_flags |= MUGE_FLAG_LINK;
			muge_dbg_printf(sc, "10/100 ethernet\n");
			break;
		case IFM_1000_T:
			sc->sc_flags |= MUGE_FLAG_LINK;
			muge_dbg_printf(sc, "Gigabit ethernet\n");
			break;
		default:
			break;
		}
	}
	/* Lost link, do nothing. */
	if ((sc->sc_flags & MUGE_FLAG_LINK) == 0) {
		muge_dbg_printf(sc, "link flag not set\n");
		goto done;
	}

	err = lan78xx_read_reg(sc, ETH_FCT_FLOW, &fct_flow);
	if (err) {
		muge_warn_printf(sc,
		   "failed to read initial flow control thresholds, error %d\n",
		    err);
		goto done;
	}

	/* Enable/disable full duplex operation and TX/RX pause. */
	if ((IFM_OPTIONS(mii->mii_media_active) & IFM_FDX) != 0) {
		muge_dbg_printf(sc, "full duplex operation\n");

		/* Enable transmit MAC flow control function. */
		if ((IFM_OPTIONS(mii->mii_media_active) & IFM_ETH_TXPAUSE) != 0)
			flow |= ETH_FLOW_CR_TX_FCEN_ | 0xFFFF;

		if ((IFM_OPTIONS(mii->mii_media_active) & IFM_ETH_RXPAUSE) != 0)
			flow |= ETH_FLOW_CR_RX_FCEN_;
	}

	/* XXX Flow control settings obtained from Microchip's driver. */
	switch(usbd_get_speed(sc->sc_ue.ue_udev)) {
	case USB_SPEED_SUPER:
		fct_flow = 0x817;
		break;
	case USB_SPEED_HIGH:
		fct_flow = 0x211;
		break;
	default:
		break;
	}

	err += lan78xx_write_reg(sc, ETH_FLOW, flow);
	err += lan78xx_write_reg(sc, ETH_FCT_FLOW, fct_flow);
	if (err)
		muge_warn_printf(sc, "media change failed, error %d\n", err);

done:
	if (!locked)
		MUGE_UNLOCK(sc);
}

/*
 *	lan78xx_set_mdix_auto - Configure the device to enable automatic
 *	crossover and polarity detection.  LAN7800 provides HP Auto-MDIX
 *	functionality for seamless crossover and polarity detection.
 *
 *	@sc: driver soft context
 *
 *	LOCKING:
 *	Takes and releases the device mutex lock if not already held.
 */
static void
lan78xx_set_mdix_auto(struct muge_softc *sc)
{
	uint32_t buf, err;

	err = lan78xx_miibus_writereg(sc->sc_ue.ue_dev, sc->sc_phyno,
	    MUGE_EXT_PAGE_ACCESS, MUGE_EXT_PAGE_SPACE_1);

	buf = lan78xx_miibus_readreg(sc->sc_ue.ue_dev, sc->sc_phyno,
	    MUGE_EXT_MODE_CTRL);
	buf &= ~MUGE_EXT_MODE_CTRL_MDIX_MASK_;
	buf |= MUGE_EXT_MODE_CTRL_AUTO_MDIX_;

	lan78xx_miibus_readreg(sc->sc_ue.ue_dev, sc->sc_phyno, MII_BMCR);
	err += lan78xx_miibus_writereg(sc->sc_ue.ue_dev, sc->sc_phyno,
	    MUGE_EXT_MODE_CTRL, buf);

	err += lan78xx_miibus_writereg(sc->sc_ue.ue_dev, sc->sc_phyno,
	    MUGE_EXT_PAGE_ACCESS, MUGE_EXT_PAGE_SPACE_0);

	if (err != 0)
		muge_warn_printf(sc, "error setting PHY's MDIX status\n");

	sc->sc_mdix_ctl = buf;
}

/**
 *	lan78xx_phy_init - Initialises the in-built MUGE phy
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
lan78xx_phy_init(struct muge_softc *sc)
{
	muge_dbg_printf(sc, "Initializing PHY.\n");
	uint16_t bmcr;
	usb_ticks_t start_ticks;
	const usb_ticks_t max_ticks = USB_MS_TO_TICKS(1000);

	MUGE_LOCK_ASSERT(sc, MA_OWNED);

	/* Reset phy and wait for reset to complete. */
	lan78xx_miibus_writereg(sc->sc_ue.ue_dev, sc->sc_phyno, MII_BMCR,
	    BMCR_RESET);

	start_ticks = ticks;
	do {
		uether_pause(&sc->sc_ue, hz / 100);
		bmcr = lan78xx_miibus_readreg(sc->sc_ue.ue_dev, sc->sc_phyno,
		    MII_BMCR);
	} while ((bmcr & BMCR_RESET) && ((ticks - start_ticks) < max_ticks));

	if (((usb_ticks_t)(ticks - start_ticks)) >= max_ticks) {
		muge_err_printf(sc, "PHY reset timed-out\n");
		return (EIO);
	}

	/* Setup phy to interrupt upon link down or autoneg completion. */
	lan78xx_miibus_readreg(sc->sc_ue.ue_dev, sc->sc_phyno,
	    MUGE_PHY_INTR_STAT);
	lan78xx_miibus_writereg(sc->sc_ue.ue_dev, sc->sc_phyno,
	    MUGE_PHY_INTR_MASK,
	    (MUGE_PHY_INTR_ANEG_COMP | MUGE_PHY_INTR_LINK_CHANGE));

	/* Enable Auto-MDIX for crossover and polarity detection. */
	lan78xx_set_mdix_auto(sc);

	/* Enable all modes. */
	lan78xx_miibus_writereg(sc->sc_ue.ue_dev, sc->sc_phyno, MII_ANAR,
	    ANAR_10 | ANAR_10_FD | ANAR_TX | ANAR_TX_FD |
	    ANAR_CSMA | ANAR_FC | ANAR_PAUSE_ASYM);

	/* Restart auto-negotation. */
	bmcr |= BMCR_STARTNEG;
	bmcr |= BMCR_AUTOEN;
	lan78xx_miibus_writereg(sc->sc_ue.ue_dev, sc->sc_phyno, MII_BMCR, bmcr);
	bmcr = lan78xx_miibus_readreg(sc->sc_ue.ue_dev, sc->sc_phyno, MII_BMCR);
	return (0);
}

/**
 *	lan78xx_chip_init - Initialises the chip after power on
 *	@sc: driver soft context
 *
 *	This initialisation sequence is modelled on the procedure in the Linux
 *	driver.
 *
 *	RETURNS:
 *	Returns 0 on success or an error code on failure.
 */
static int
lan78xx_chip_init(struct muge_softc *sc)
{
	int err;
	uint32_t buf;
	uint32_t burst_cap;

	MUGE_LOCK_ASSERT(sc, MA_OWNED);

	/* Enter H/W config mode. */
	lan78xx_write_reg(sc, ETH_HW_CFG, ETH_HW_CFG_LRST_);

	if ((err = lan78xx_wait_for_bits(sc, ETH_HW_CFG, ETH_HW_CFG_LRST_)) !=
	    0) {
		muge_warn_printf(sc,
		    "timed-out waiting for lite reset to complete\n");
		goto init_failed;
	}

	/* Set the mac address. */
	if ((err = lan78xx_setmacaddress(sc, sc->sc_ue.ue_eaddr)) != 0) {
		muge_warn_printf(sc, "failed to set the MAC address\n");
		goto init_failed;
	}

	/* Read and display the revision register. */
	if ((err = lan78xx_read_reg(sc, ETH_ID_REV, &buf)) < 0) {
		muge_warn_printf(sc, "failed to read ETH_ID_REV (err = %d)\n",
		    err);
		goto init_failed;
	}
	sc->chipid = (buf & ETH_ID_REV_CHIP_ID_MASK_) >> 16;
	sc->chiprev = buf & ETH_ID_REV_CHIP_REV_MASK_;
	switch (sc->chipid) {
	case ETH_ID_REV_CHIP_ID_7800_:
	case ETH_ID_REV_CHIP_ID_7850_:
		break;
	default:
		muge_warn_printf(sc, "Chip ID 0x%04x not yet supported\n",
		    sc->chipid);
		goto init_failed;
	}
	device_printf(sc->sc_ue.ue_dev, "Chip ID 0x%04x rev %04x\n", sc->chipid,
	    sc->chiprev);

	/* Respond to BULK-IN tokens with a NAK when RX FIFO is empty. */
	if ((err = lan78xx_read_reg(sc, ETH_USB_CFG0, &buf)) != 0) {
		muge_warn_printf(sc, "failed to read ETH_USB_CFG0 (err=%d)\n", err);
		goto init_failed;
	}
	buf |= ETH_USB_CFG_BIR_;
	lan78xx_write_reg(sc, ETH_USB_CFG0, buf);

	/*
	 * XXX LTM support will go here.
	 */

	/* Configuring the burst cap. */
	switch (usbd_get_speed(sc->sc_ue.ue_udev)) {
	case USB_SPEED_SUPER:
		burst_cap = MUGE_DEFAULT_BURST_CAP_SIZE/MUGE_SS_USB_PKT_SIZE;
		break;
	case USB_SPEED_HIGH:
		burst_cap = MUGE_DEFAULT_BURST_CAP_SIZE/MUGE_HS_USB_PKT_SIZE;
		break;
	default:
		burst_cap = MUGE_DEFAULT_BURST_CAP_SIZE/MUGE_FS_USB_PKT_SIZE;
	}

	lan78xx_write_reg(sc, ETH_BURST_CAP, burst_cap);

	/* Set the default bulk in delay (same value from Linux driver). */
	lan78xx_write_reg(sc, ETH_BULK_IN_DLY, MUGE_DEFAULT_BULK_IN_DELAY);

	/* Multiple ethernet frames per USB packets. */
	err = lan78xx_read_reg(sc, ETH_HW_CFG, &buf);
	buf |= ETH_HW_CFG_MEF_;
	err = lan78xx_write_reg(sc, ETH_HW_CFG, buf);

	/* Enable burst cap. */
	if ((err = lan78xx_read_reg(sc, ETH_USB_CFG0, &buf)) < 0) {
		muge_warn_printf(sc, "failed to read ETH_USB_CFG0 (err=%d)\n",
		    err);
		goto init_failed;
	}
	buf |= ETH_USB_CFG_BCE_;
	err = lan78xx_write_reg(sc, ETH_USB_CFG0, buf);

	/*
	 * Set FCL's RX and TX FIFO sizes: according to data sheet this is
	 * already the default value. But we initialize it to the same value
	 * anyways, as that's what the Linux driver does.
	 *
	 */
	buf = (MUGE_MAX_RX_FIFO_SIZE - 512) / 512;
	err = lan78xx_write_reg(sc, ETH_FCT_RX_FIFO_END, buf);

	buf = (MUGE_MAX_TX_FIFO_SIZE - 512) / 512;
	err = lan78xx_write_reg(sc, ETH_FCT_TX_FIFO_END, buf);

	/* Enabling interrupts. (Not using them for now) */
	err = lan78xx_write_reg(sc, ETH_INT_STS, ETH_INT_STS_CLEAR_ALL_);

	/*
	 * Initializing flow control registers to 0.  These registers are
	 * properly set is handled in link-reset function in the Linux driver.
	 */
	err = lan78xx_write_reg(sc, ETH_FLOW, 0);
	err = lan78xx_write_reg(sc, ETH_FCT_FLOW, 0);

	/*
	 * Settings for the RFE, we enable broadcast and destination address
	 * perfect filtering.
	 */
	err = lan78xx_read_reg(sc, ETH_RFE_CTL, &buf);
	buf |= ETH_RFE_CTL_BCAST_EN_ | ETH_RFE_CTL_DA_PERFECT_;
	err = lan78xx_write_reg(sc, ETH_RFE_CTL, buf);

	/*
	 * At this point the Linux driver writes multicast tables, and enables
	 * checksum engines. But in FreeBSD that gets done in muge_init,
	 * which gets called when the interface is brought up.
	 */

	/* Reset the PHY. */
	lan78xx_write_reg(sc, ETH_PMT_CTL, ETH_PMT_CTL_PHY_RST_);
	if ((err = lan78xx_wait_for_bits(sc, ETH_PMT_CTL,
	    ETH_PMT_CTL_PHY_RST_)) != 0) {
		muge_warn_printf(sc,
		    "timed-out waiting for phy reset to complete\n");
		goto init_failed;
	}

	err = lan78xx_read_reg(sc, ETH_MAC_CR, &buf);
	if (sc->chipid == ETH_ID_REV_CHIP_ID_7800_ &&
	    !lan78xx_eeprom_present(sc)) {
		/* Set automatic duplex and speed on LAN7800 without EEPROM. */
		buf |= ETH_MAC_CR_AUTO_DUPLEX_ | ETH_MAC_CR_AUTO_SPEED_;
	}
	err = lan78xx_write_reg(sc, ETH_MAC_CR, buf);

	/*
	 * Enable PHY interrupts (Not really getting used for now)
	 * ETH_INT_EP_CTL: interrupt endpoint control register
	 * phy events cause interrupts to be issued
	 */
	err = lan78xx_read_reg(sc, ETH_INT_EP_CTL, &buf);
	buf |= ETH_INT_ENP_PHY_INT;
	err = lan78xx_write_reg(sc, ETH_INT_EP_CTL, buf);

	/*
	 * Enables mac's transmitter.  It will transmit frames from the buffer
	 * onto the cable.
	 */
	err = lan78xx_read_reg(sc, ETH_MAC_TX, &buf);
	buf |= ETH_MAC_TX_TXEN_;
	err = lan78xx_write_reg(sc, ETH_MAC_TX, buf);

	/* FIFO is capable of transmitting frames to MAC. */
	err = lan78xx_read_reg(sc, ETH_FCT_TX_CTL, &buf);
	buf |= ETH_FCT_TX_CTL_EN_;
	err = lan78xx_write_reg(sc, ETH_FCT_TX_CTL, buf);

	/*
	 * Set max frame length.  In linux this is dev->mtu (which by default
	 * is 1500) + VLAN_ETH_HLEN = 1518.
	 */
	err = lan78xx_set_rx_max_frame_length(sc, ETHER_MAX_LEN);

	/* Initialise the PHY. */
	if ((err = lan78xx_phy_init(sc)) != 0)
		goto init_failed;

	/* Enable MAC RX. */
	err = lan78xx_read_reg(sc, ETH_MAC_RX, &buf);
	buf |= ETH_MAC_RX_EN_;
	err = lan78xx_write_reg(sc, ETH_MAC_RX, buf);

	/* Enable FIFO controller RX. */
	err = lan78xx_read_reg(sc, ETH_FCT_RX_CTL, &buf);
	buf |= ETH_FCT_TX_CTL_EN_;
	err = lan78xx_write_reg(sc, ETH_FCT_RX_CTL, buf);

	sc->sc_flags |= MUGE_FLAG_INIT_DONE;
	return (0);

init_failed:
	muge_err_printf(sc, "lan78xx_chip_init failed (err=%d)\n", err);
	return (err);
}

static void
muge_bulk_read_callback(struct usb_xfer *xfer, usb_error_t error)
{
	struct muge_softc *sc = usbd_xfer_softc(xfer);
	struct usb_ether *ue = &sc->sc_ue;
	struct ifnet *ifp = uether_getifp(ue);
	struct mbuf *m;
	struct usb_page_cache *pc;
	uint16_t pktlen;
	uint32_t rx_cmd_a, rx_cmd_b;
	uint16_t rx_cmd_c;
	int off;
	int actlen;

	usbd_xfer_status(xfer, &actlen, NULL, NULL, NULL);
	muge_dbg_printf(sc, "rx : actlen %d\n", actlen);

	switch (USB_GET_STATE(xfer)) {
	case USB_ST_TRANSFERRED:

		/*
		 * There is always a zero length frame after bringing the
		 * interface up.
		 */
		if (actlen < (sizeof(rx_cmd_a) + ETHER_CRC_LEN))
			goto tr_setup;

		/*
		 * There may be multiple packets in the USB frame.  Each will
		 * have a header and each needs to have its own mbuf allocated
		 * and populated for it.
		 */
		pc = usbd_xfer_get_frame(xfer, 0);
		off = 0;

		while (off < actlen) {

			/* The frame header is aligned on a 4 byte boundary. */
			off = ((off + 0x3) & ~0x3);

			/* Extract RX CMD A. */
			if (off + sizeof(rx_cmd_a) > actlen)
				goto tr_setup;
			usbd_copy_out(pc, off, &rx_cmd_a, sizeof(rx_cmd_a));
			off += (sizeof(rx_cmd_a));
			rx_cmd_a = le32toh(rx_cmd_a);


			/* Extract RX CMD B. */
			if (off + sizeof(rx_cmd_b) > actlen)
				goto tr_setup;
			usbd_copy_out(pc, off, &rx_cmd_b, sizeof(rx_cmd_b));
			off += (sizeof(rx_cmd_b));
			rx_cmd_b = le32toh(rx_cmd_b);


			/* Extract RX CMD C. */
			if (off + sizeof(rx_cmd_c) > actlen)
				goto tr_setup;
			usbd_copy_out(pc, off, &rx_cmd_c, sizeof(rx_cmd_c));
			off += (sizeof(rx_cmd_c));
			rx_cmd_c = le16toh(rx_cmd_c);

			if (off > actlen)
				goto tr_setup;

			pktlen = (rx_cmd_a & RX_CMD_A_LEN_MASK_);

			muge_dbg_printf(sc,
			    "rx_cmd_a 0x%08x rx_cmd_b 0x%08x rx_cmd_c 0x%04x "
			    " pktlen %d actlen %d off %d\n",
			    rx_cmd_a, rx_cmd_b, rx_cmd_c, pktlen, actlen, off);

			if (rx_cmd_a & RX_CMD_A_RED_) {
				muge_dbg_printf(sc,
				     "rx error (hdr 0x%08x)\n", rx_cmd_a);
				if_inc_counter(ifp, IFCOUNTER_IERRORS, 1);
			} else {
				/* Ethernet frame too big or too small? */
				if ((pktlen < ETHER_HDR_LEN) ||
				    (pktlen > (actlen - off)))
					goto tr_setup;

				/* Create a new mbuf to store the packet. */
				m = uether_newbuf();
				if (m == NULL) {
					muge_warn_printf(sc,
					    "failed to create new mbuf\n");
					if_inc_counter(ifp, IFCOUNTER_IQDROPS,
					    1);
					goto tr_setup;
				}

				usbd_copy_out(pc, off, mtod(m, uint8_t *),
				    pktlen);

				/*
				 * Check if RX checksums are computed, and
				 * offload them
				 */
				if ((ifp->if_capabilities & IFCAP_RXCSUM) &&
				    !(rx_cmd_a & RX_CMD_A_ICSM_)) {
					struct ether_header *eh;
					eh = mtod(m, struct ether_header *);
					/*
					 * Remove the extra 2 bytes of the csum
					 *
					 * The checksum appears to be
					 * simplistically calculated over the
					 * protocol headers up to the end of the
					 * eth frame.  Which means if the eth
					 * frame is padded the csum calculation
					 * is incorrectly performed over the
					 * padding bytes as well.  Therefore to
					 * be safe we ignore the H/W csum on
					 * frames less than or equal to
					 * 64 bytes.
					 *
					 * Protocols checksummed:
					 * TCP, UDP, ICMP, IGMP, IP
					 */
					if (pktlen > ETHER_MIN_LEN) {
						m->m_pkthdr.csum_flags |=
						    CSUM_DATA_VALID;

						/*
						 * Copy the checksum from the
						 * last 2 bytes of the transfer
						 * and put in the csum_data
						 * field.
						 */
						usbd_copy_out(pc,
						    (off + pktlen),
						    &m->m_pkthdr.csum_data, 2);

						/*
						 * The data is copied in network
						 * order, but the csum algorithm
						 * in the kernel expects it to
						 * be in host network order.
						 */
						m->m_pkthdr.csum_data =
						   ntohs(m->m_pkthdr.csum_data);

						muge_dbg_printf(sc,
						    "RX checksum offloaded (0x%04x)\n",
						    m->m_pkthdr.csum_data);
					}
				}

				/* Enqueue the mbuf on the receive queue. */
				if (pktlen < (4 + ETHER_HDR_LEN)) {
					m_freem(m);
					goto tr_setup;
				}
				/* Remove 4 trailing bytes */
				uether_rxmbuf(ue, m, pktlen - 4);
			}

			/*
			 * Update the offset to move to the next potential
			 * packet.
			 */
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
			muge_warn_printf(sc, "bulk read error, %s\n",
			    usbd_errstr(error));
			usbd_xfer_set_stall(xfer);
			goto tr_setup;
		}
		return;
	}
}

/**
 *	muge_bulk_write_callback - Write callback used to send ethernet frame(s)
 *	@xfer: the USB transfer
 *	@error: error code if the transfers is in an errored state
 *
 *	The main write function that pulls ethernet frames off the queue and
 *	sends them out.
 *
 */
static void
muge_bulk_write_callback(struct usb_xfer *xfer, usb_error_t error)
{
	struct muge_softc *sc = usbd_xfer_softc(xfer);
	struct ifnet *ifp = uether_getifp(&sc->sc_ue);
	struct usb_page_cache *pc;
	struct mbuf *m;
	int nframes;
	uint32_t frm_len = 0, tx_cmd_a = 0, tx_cmd_b = 0;

	switch (USB_GET_STATE(xfer)) {
	case USB_ST_TRANSFERRED:
		muge_dbg_printf(sc,
		    "USB TRANSFER status: USB_ST_TRANSFERRED\n");
		ifp->if_drv_flags &= ~IFF_DRV_OACTIVE;
		/* FALLTHROUGH */
	case USB_ST_SETUP:
		muge_dbg_printf(sc, "USB TRANSFER status: USB_ST_SETUP\n");
tr_setup:
		if ((sc->sc_flags & MUGE_FLAG_LINK) == 0 ||
			(ifp->if_drv_flags & IFF_DRV_OACTIVE) != 0) {
			muge_dbg_printf(sc,
			    "sc->sc_flags & MUGE_FLAG_LINK: %d\n",
			    (sc->sc_flags & MUGE_FLAG_LINK));
			muge_dbg_printf(sc,
			    "ifp->if_drv_flags & IFF_DRV_OACTIVE: %d\n",
			    (ifp->if_drv_flags & IFF_DRV_OACTIVE));
			muge_dbg_printf(sc,
			    "USB TRANSFER not sending: no link or controller is busy \n");
			/*
			 * Don't send anything if there is no link or
			 * controller is busy.
			 */
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

			/*
			 * Each frame is prefixed with two 32-bit values
			 * describing the length of the packet and buffer.
			 */
			tx_cmd_a = (m->m_pkthdr.len & TX_CMD_A_LEN_MASK_) |
			     TX_CMD_A_FCS_;
			tx_cmd_a = htole32(tx_cmd_a);
			usbd_copy_in(pc, 0, &tx_cmd_a, sizeof(tx_cmd_a));

			tx_cmd_b = 0;

			/* TCP LSO Support will probably be implemented here. */
			tx_cmd_b = htole32(tx_cmd_b);
			usbd_copy_in(pc, 4, &tx_cmd_b, sizeof(tx_cmd_b));

			frm_len += 8;

			/* Next copy in the actual packet */
			usbd_m_copy_in(pc, frm_len, m, 0, m->m_pkthdr.len);
			frm_len += m->m_pkthdr.len;

			if_inc_counter(ifp, IFCOUNTER_OPACKETS, 1);

			/*
			 * If there's a BPF listener, bounce a copy of this
			 * frame to it.
			 */
			BPF_MTAP(ifp, m);
			m_freem(m);

			/* Set frame length. */
			usbd_xfer_set_frame_len(xfer, nframes, frm_len);
		}

		muge_dbg_printf(sc, "USB TRANSFER nframes: %d\n", nframes);
		if (nframes != 0) {
			muge_dbg_printf(sc, "USB TRANSFER submit attempt\n");
			usbd_xfer_set_frames(xfer, nframes);
			usbd_transfer_submit(xfer);
			ifp->if_drv_flags |= IFF_DRV_OACTIVE;
		}
		return;

	default:
		if_inc_counter(ifp, IFCOUNTER_OERRORS, 1);
		ifp->if_drv_flags &= ~IFF_DRV_OACTIVE;

		if (error != USB_ERR_CANCELLED) {
			muge_err_printf(sc,
			    "usb error on tx: %s\n", usbd_errstr(error));
			usbd_xfer_set_stall(xfer);
			goto tr_setup;
		}
		return;
	}
}

#ifdef FDT
/**
 *	muge_fdt_find_eth_node - find descendant node with required compatibility
 *	@start: start node
 *	@compatible: compatible string used to identify the node
 *
 *	Loop through all descendant nodes and return first match with required
 *	compatibility.
 *
 *	RETURNS:
 *	Returns node's phandle on success -1 otherwise
 */
static phandle_t
muge_fdt_find_eth_node(phandle_t start, const char *compatible)
{
	phandle_t child, node;

	/* Traverse through entire tree to find usb ethernet nodes. */
	for (node = OF_child(start); node != 0; node = OF_peer(node)) {
		if (ofw_bus_node_is_compatible(node, compatible))
			return (node);
		child = muge_fdt_find_eth_node(node, compatible);
		if (child != -1)
			return (child);
	}

	return (-1);
}

/**
 *	muge_fdt_read_mac_property - read MAC address from node
 *	@node: USB device node
 *	@mac: memory to store MAC address to
 *
 *	Check for common properties that might contain MAC address
 *	passed by boot loader.
 *
 *	RETURNS:
 *	Returns 0 on success, error code otherwise
 */
static int
muge_fdt_read_mac_property(phandle_t node, unsigned char *mac)
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
 *	muge_fdt_find_mac - read MAC address from node
 *	@compatible: compatible string for DTB node in the form "usb[N]NNN,[M]MMM"
 *	    where NNN is vendor id and MMM is product id
 *	@mac: memory to store MAC address to
 *
 *	Tries to find matching node in DTS and obtain MAC address info from it
 *
 *	RETURNS:
 *	Returns 0 on success, error code otherwise
 */
static int
muge_fdt_find_mac(const char *compatible, unsigned char *mac)
{
	phandle_t node, root;

	root = OF_finddevice("/");
	node = muge_fdt_find_eth_node(root, compatible);
	if (node != -1) {
		if (muge_fdt_read_mac_property(node, mac) == 0)
			return (0);
	}

	return (ENXIO);
}
#endif

/**
 *	muge_set_mac_addr - Initiailizes NIC MAC address
 *	@ue: the USB ethernet device
 *
 *	Tries to obtain MAC address from number of sources: registers,
 *	EEPROM, DTB blob. If all sources fail - generates random MAC.
 */
static void
muge_set_mac_addr(struct usb_ether *ue)
{
	struct muge_softc *sc = uether_getsc(ue);
	uint32_t mac_h, mac_l;
#ifdef FDT
	char compatible[16];
	struct usb_attach_arg *uaa = device_get_ivars(ue->ue_dev);
#endif

	memset(sc->sc_ue.ue_eaddr, 0xff, ETHER_ADDR_LEN);

	uint32_t val;
	lan78xx_read_reg(sc, 0, &val);

	/* Read current MAC address from RX_ADDRx registers. */
	if ((lan78xx_read_reg(sc, ETH_RX_ADDRL, &mac_l) == 0) &&
	    (lan78xx_read_reg(sc, ETH_RX_ADDRH, &mac_h) == 0)) {
		sc->sc_ue.ue_eaddr[5] = (uint8_t)((mac_h >> 8) & 0xff);
		sc->sc_ue.ue_eaddr[4] = (uint8_t)((mac_h) & 0xff);
		sc->sc_ue.ue_eaddr[3] = (uint8_t)((mac_l >> 24) & 0xff);
		sc->sc_ue.ue_eaddr[2] = (uint8_t)((mac_l >> 16) & 0xff);
		sc->sc_ue.ue_eaddr[1] = (uint8_t)((mac_l >> 8) & 0xff);
		sc->sc_ue.ue_eaddr[0] = (uint8_t)((mac_l) & 0xff);
	}

	/* If RX_ADDRx did not provide a valid MAC address, try EEPROM. */
	if (ETHER_IS_VALID(sc->sc_ue.ue_eaddr)) {
		muge_dbg_printf(sc, "MAC assigned from registers\n");
		return;
	}

	if ((lan78xx_eeprom_present(sc) &&
	    lan78xx_eeprom_read_raw(sc, ETH_E2P_MAC_OFFSET,
	    sc->sc_ue.ue_eaddr, ETHER_ADDR_LEN) == 0) ||
	    (lan78xx_otp_read(sc, OTP_MAC_OFFSET,
	    sc->sc_ue.ue_eaddr, ETHER_ADDR_LEN) == 0)) {
		if (ETHER_IS_VALID(sc->sc_ue.ue_eaddr)) {
			muge_dbg_printf(sc, "MAC read from EEPROM\n");
			return;
		}
	}

#ifdef FDT
	snprintf(compatible, sizeof(compatible), "usb%x,%x",
	    uaa->info.idVendor, uaa->info.idProduct);
	if (muge_fdt_find_mac(compatible, sc->sc_ue.ue_eaddr) == 0) {
		muge_dbg_printf(sc, "MAC assigned from FDT blob\n");
		return;
	}
#endif

	muge_dbg_printf(sc, "MAC assigned randomly\n");
	arc4rand(sc->sc_ue.ue_eaddr, ETHER_ADDR_LEN, 0);
	sc->sc_ue.ue_eaddr[0] &= ~0x01;	/* unicast */
	sc->sc_ue.ue_eaddr[0] |= 0x02;	/* locally administered */
}

/**
 *	muge_attach_post - Called after the driver attached to the USB interface
 *	@ue: the USB ethernet device
 *
 *	This is where the chip is intialised for the first time.  This is
 *	different from the muge_init() function in that that one is designed to
 *	setup the H/W to match the UE settings and can be called after a reset.
 *
 */
static void
muge_attach_post(struct usb_ether *ue)
{
	struct muge_softc *sc = uether_getsc(ue);

	muge_dbg_printf(sc, "Calling muge_attach_post.\n");

	/* Setup some of the basics */
	sc->sc_phyno = 1;

	muge_set_mac_addr(ue);

	/* Initialise the chip for the first time */
	lan78xx_chip_init(sc);
}

/**
 *	muge_attach_post_sub - Called after attach to the USB interface
 *	@ue: the USB ethernet device
 *
 *	Most of this is boilerplate code and copied from the base USB ethernet
 *	driver.  It has been overriden so that we can indicate to the system
 *	that the chip supports H/W checksumming.
 *
 *	RETURNS:
 *	Returns 0 on success or a negative error code.
 */
static int
muge_attach_post_sub(struct usb_ether *ue)
{
	struct muge_softc *sc;
	struct ifnet *ifp;
	int error;

	sc = uether_getsc(ue);
	muge_dbg_printf(sc, "Calling muge_attach_post_sub.\n");
	ifp = ue->ue_ifp;
	ifp->if_flags = IFF_BROADCAST | IFF_SIMPLEX | IFF_MULTICAST;
	ifp->if_start = uether_start;
	ifp->if_ioctl = muge_ioctl;
	ifp->if_init = uether_init;
	IFQ_SET_MAXLEN(&ifp->if_snd, ifqmaxlen);
	ifp->if_snd.ifq_drv_maxlen = ifqmaxlen;
	IFQ_SET_READY(&ifp->if_snd);

	/*
	 * The chip supports TCP/UDP checksum offloading on TX and RX paths,
	 * however currently only RX checksum is supported in the driver
	 * (see top of file).
	 */
	ifp->if_hwassist = 0;
	if (MUGE_DEFAULT_RX_CSUM_ENABLE)
		ifp->if_capabilities |= IFCAP_RXCSUM;

	if (MUGE_DEFAULT_TX_CSUM_ENABLE)
		ifp->if_capabilities |= IFCAP_TXCSUM;

	/*
	 * In the Linux driver they also enable scatter/gather (NETIF_F_SG)
	 * here, that's something related to socket buffers used in Linux.
	 * FreeBSD doesn't have that as an interface feature.
	 */
	if (MUGE_DEFAULT_TSO_CSUM_ENABLE)
		ifp->if_capabilities |= IFCAP_TSO4 | IFCAP_TSO6;

#if 0
	/* TX checksuming is disabled since not yet implemented. */
	ifp->if_capabilities |= IFCAP_TXCSUM;
	ifp->if_capenable |= IFCAP_TXCSUM;
	ifp->if_hwassist = CSUM_TCP | CSUM_UDP;
#endif

	ifp->if_capenable = ifp->if_capabilities;

	mtx_lock(&Giant);
	error = mii_attach(ue->ue_dev, &ue->ue_miibus, ifp,
		uether_ifmedia_upd, ue->ue_methods->ue_mii_sts,
		BMSR_DEFCAPMASK, sc->sc_phyno, MII_OFFSET_ANY, 0);
	mtx_unlock(&Giant);

	return (0);
}

/**
 *	muge_start - Starts communication with the LAN78xx chip
 *	@ue: USB ether interface
 */
static void
muge_start(struct usb_ether *ue)
{
	struct muge_softc *sc = uether_getsc(ue);

	/*
	 * Start the USB transfers, if not already started.
	 */
	usbd_transfer_start(sc->sc_xfer[MUGE_BULK_DT_RD]);
	usbd_transfer_start(sc->sc_xfer[MUGE_BULK_DT_WR]);
}

/**
 *	muge_ioctl - ioctl function for the device
 *	@ifp: interface pointer
 *	@cmd: the ioctl command
 *	@data: data passed in the ioctl call, typically a pointer to struct
 *	ifreq.
 *
 *	The ioctl routine is overridden to detect change requests for the H/W
 *	checksum capabilities.
 *
 *	RETURNS:
 *	0 on success and an error code on failure.
 */
static int
muge_ioctl(struct ifnet *ifp, u_long cmd, caddr_t data)
{
	struct usb_ether *ue = ifp->if_softc;
	struct muge_softc *sc;
	struct ifreq *ifr;
	int rc;
	int mask;
	int reinit;

	if (cmd == SIOCSIFCAP) {
		sc = uether_getsc(ue);
		ifr = (struct ifreq *)data;

		MUGE_LOCK(sc);

		rc = 0;
		reinit = 0;

		mask = ifr->ifr_reqcap ^ ifp->if_capenable;

		/* Modify the RX CSUM enable bits. */
		if ((mask & IFCAP_RXCSUM) != 0 &&
			(ifp->if_capabilities & IFCAP_RXCSUM) != 0) {
			ifp->if_capenable ^= IFCAP_RXCSUM;

			if (ifp->if_drv_flags & IFF_DRV_RUNNING) {
				ifp->if_drv_flags &= ~IFF_DRV_RUNNING;
				reinit = 1;
			}
		}

		MUGE_UNLOCK(sc);
		if (reinit)
			uether_init(ue);

	} else {
		rc = uether_ioctl(ifp, cmd, data);
	}

	return (rc);
}

/**
 *	muge_reset - Reset the SMSC chip
 *	@sc: device soft context
 *
 *	LOCKING:
 *	Should be called with the SMSC lock held.
 */
static void
muge_reset(struct muge_softc *sc)
{
	struct usb_config_descriptor *cd;
	usb_error_t err;

	cd = usbd_get_config_descriptor(sc->sc_ue.ue_udev);

	err = usbd_req_set_config(sc->sc_ue.ue_udev, &sc->sc_mtx,
	    cd->bConfigurationValue);
	if (err)
		muge_warn_printf(sc, "reset failed (ignored)\n");

	/* Wait a little while for the chip to get its brains in order. */
	uether_pause(&sc->sc_ue, hz / 100);

	/* Reinitialize controller to achieve full reset. */
	lan78xx_chip_init(sc);
}

/**
 * muge_set_addr_filter
 *
 *	@sc: device soft context
 *	@index: index of the entry to the perfect address table
 *	@addr: address to be written
 *
 */
static void
muge_set_addr_filter(struct muge_softc *sc, int index,
    uint8_t addr[ETHER_ADDR_LEN])
{
	uint32_t tmp;

	if ((sc) && (index > 0) && (index < MUGE_NUM_PFILTER_ADDRS_)) {
		tmp = addr[3];
		tmp |= addr[2] | (tmp << 8);
		tmp |= addr[1] | (tmp << 8);
		tmp |= addr[0] | (tmp << 8);
		sc->sc_pfilter_table[index][1] = tmp;
		tmp = addr[5];
		tmp |= addr[4] | (tmp << 8);
		tmp |= ETH_MAF_HI_VALID_ | ETH_MAF_HI_TYPE_DST_;
		sc->sc_pfilter_table[index][0] = tmp;
	}
}

/**
 *	lan78xx_dataport_write - write to the selected RAM
 *	@sc: The device soft context.
 *	@ram_select: Select which RAM to access.
 *	@addr: Starting address to write to.
 *	@buf: word-sized buffer to write to RAM, starting at @addr.
 *	@length: length of @buf
 *
 *
 *	RETURNS:
 *	0 if write successful.
 */
static int
lan78xx_dataport_write(struct muge_softc *sc, uint32_t ram_select,
    uint32_t addr, uint32_t length, uint32_t *buf)
{
	uint32_t dp_sel;
	int i, ret;

	MUGE_LOCK_ASSERT(sc, MA_OWNED);
	ret = lan78xx_wait_for_bits(sc, ETH_DP_SEL, ETH_DP_SEL_DPRDY_);
	if (ret < 0)
		goto done;

	ret = lan78xx_read_reg(sc, ETH_DP_SEL, &dp_sel);

	dp_sel &= ~ETH_DP_SEL_RSEL_MASK_;
	dp_sel |= ram_select;

	ret = lan78xx_write_reg(sc, ETH_DP_SEL, dp_sel);

	for (i = 0; i < length; i++) {
		ret = lan78xx_write_reg(sc, ETH_DP_ADDR, addr + i);
		ret = lan78xx_write_reg(sc, ETH_DP_DATA, buf[i]);
		ret = lan78xx_write_reg(sc, ETH_DP_CMD, ETH_DP_CMD_WRITE_);
		ret = lan78xx_wait_for_bits(sc, ETH_DP_SEL, ETH_DP_SEL_DPRDY_);
		if (ret != 0)
			goto done;
	}

done:
	return (ret);
}

/**
 * muge_multicast_write
 * @sc: device's soft context
 *
 * Writes perfect addres filters and hash address filters to their
 * corresponding registers and RAMs.
 *
 */
static void
muge_multicast_write(struct muge_softc *sc)
{
	int i, ret;
	lan78xx_dataport_write(sc, ETH_DP_SEL_RSEL_VLAN_DA_,
	    ETH_DP_SEL_VHF_VLAN_LEN, ETH_DP_SEL_VHF_HASH_LEN,
	    sc->sc_mchash_table);

	for (i = 1; i < MUGE_NUM_PFILTER_ADDRS_; i++) {
		ret = lan78xx_write_reg(sc, PFILTER_HI(i), 0);
		ret = lan78xx_write_reg(sc, PFILTER_LO(i),
		    sc->sc_pfilter_table[i][1]);
		ret = lan78xx_write_reg(sc, PFILTER_HI(i),
		    sc->sc_pfilter_table[i][0]);
	}
}

/**
 *	muge_hash - Calculate the hash of a mac address
 *	@addr: The mac address to calculate the hash on
 *
 *	This function is used when configuring a range of multicast mac
 *	addresses to filter on.  The hash of the mac address is put in the
 *	device's mac hash table.
 *
 *	RETURNS:
 *	Returns a value from 0-63 value which is the hash of the mac address.
 */
static inline uint32_t
muge_hash(uint8_t addr[ETHER_ADDR_LEN])
{
	return (ether_crc32_be(addr, ETHER_ADDR_LEN) >> 23) & 0x1ff;
}

/**
 *	muge_setmulti - Setup multicast
 *	@ue: usb ethernet device context
 *
 *	Tells the device to either accept frames with a multicast mac address,
 *	a select group of m'cast mac addresses or just the devices mac address.
 *
 *	LOCKING:
 *	Should be called with the MUGE lock held.
 */
static void
muge_setmulti(struct usb_ether *ue)
{
	struct muge_softc *sc = uether_getsc(ue);
	struct ifnet *ifp = uether_getifp(ue);
	uint8_t i, *addr;
	struct ifmultiaddr *ifma;

	MUGE_LOCK_ASSERT(sc, MA_OWNED);

	sc->sc_rfe_ctl &= ~(ETH_RFE_CTL_UCAST_EN_ | ETH_RFE_CTL_MCAST_EN_ |
		ETH_RFE_CTL_DA_PERFECT_ | ETH_RFE_CTL_MCAST_HASH_);

	/* Initialize hash filter table. */
	for (i = 0; i < ETH_DP_SEL_VHF_HASH_LEN; i++)
		sc->sc_mchash_table[i] = 0;

	/* Initialize perfect filter table. */
	for (i = 1; i < MUGE_NUM_PFILTER_ADDRS_; i++) {
		sc->sc_pfilter_table[i][0] =
		sc->sc_pfilter_table[i][1] = 0;
	}

	sc->sc_rfe_ctl |= ETH_RFE_CTL_BCAST_EN_;

	if (ifp->if_flags & IFF_PROMISC) {
		muge_dbg_printf(sc, "promiscuous mode enabled\n");
		sc->sc_rfe_ctl |= ETH_RFE_CTL_MCAST_EN_ | ETH_RFE_CTL_UCAST_EN_;
	} else if (ifp->if_flags & IFF_ALLMULTI){
		muge_dbg_printf(sc, "receive all multicast enabled\n");
		sc->sc_rfe_ctl |= ETH_RFE_CTL_MCAST_EN_;
	} else {
		/* Lock the mac address list before hashing each of them. */
		if_maddr_rlock(ifp);
		if (!CK_STAILQ_EMPTY(&ifp->if_multiaddrs)) {
			i = 1;
			CK_STAILQ_FOREACH(ifma, &ifp->if_multiaddrs,
			    ifma_link) {
				/* First fill up the perfect address table. */
				addr = LLADDR((struct sockaddr_dl *)
				    ifma->ifma_addr);
				if (i < 33 /* XXX */) {
					muge_set_addr_filter(sc, i, addr);
				} else {
					uint32_t bitnum = muge_hash(addr);
					sc->sc_mchash_table[bitnum / 32] |=
					    (1 << (bitnum % 32));
					sc->sc_rfe_ctl |=
					    ETH_RFE_CTL_MCAST_HASH_;
				}
				i++;
			}
		}
		if_maddr_runlock(ifp);
		muge_multicast_write(sc);
	}
	lan78xx_write_reg(sc, ETH_RFE_CTL, sc->sc_rfe_ctl);
}

/**
 *	muge_setpromisc - Enables/disables promiscuous mode
 *	@ue: usb ethernet device context
 *
 *	LOCKING:
 *	Should be called with the MUGE lock held.
 */
static void
muge_setpromisc(struct usb_ether *ue)
{
	struct muge_softc *sc = uether_getsc(ue);
	struct ifnet *ifp = uether_getifp(ue);

	muge_dbg_printf(sc, "promiscuous mode %sabled\n",
	    (ifp->if_flags & IFF_PROMISC) ? "en" : "dis");

	MUGE_LOCK_ASSERT(sc, MA_OWNED);

	if (ifp->if_flags & IFF_PROMISC)
		sc->sc_rfe_ctl |= ETH_RFE_CTL_MCAST_EN_ | ETH_RFE_CTL_UCAST_EN_;
	else
		sc->sc_rfe_ctl &= ~(ETH_RFE_CTL_MCAST_EN_);

	lan78xx_write_reg(sc, ETH_RFE_CTL, sc->sc_rfe_ctl);
}

/**
 *	muge_sethwcsum - Enable or disable H/W UDP and TCP checksumming
 *	@sc: driver soft context
 *
 *	LOCKING:
 *	Should be called with the MUGE lock held.
 *
 *	RETURNS:
 *	Returns 0 on success or a negative error code.
 */
static int muge_sethwcsum(struct muge_softc *sc)
{
	struct ifnet *ifp = uether_getifp(&sc->sc_ue);
	int err;

	if (!ifp)
		return (-EIO);

	MUGE_LOCK_ASSERT(sc, MA_OWNED);

	if (ifp->if_capabilities & IFCAP_RXCSUM) {
		sc->sc_rfe_ctl |= ETH_RFE_CTL_IGMP_COE_ | ETH_RFE_CTL_ICMP_COE_;
		sc->sc_rfe_ctl |= ETH_RFE_CTL_TCPUDP_COE_ | ETH_RFE_CTL_IP_COE_;
	} else {
		sc->sc_rfe_ctl &=
		    ~(ETH_RFE_CTL_IGMP_COE_ | ETH_RFE_CTL_ICMP_COE_);
		sc->sc_rfe_ctl &=
		     ~(ETH_RFE_CTL_TCPUDP_COE_ | ETH_RFE_CTL_IP_COE_);
	}

	sc->sc_rfe_ctl &= ~ETH_RFE_CTL_VLAN_FILTER_;

	err = lan78xx_write_reg(sc, ETH_RFE_CTL, sc->sc_rfe_ctl);

	if (err != 0) {
		muge_warn_printf(sc, "failed to write ETH_RFE_CTL (err=%d)\n",
		    err);
		return (err);
	}

	return (0);
}

/**
 *	muge_ifmedia_upd - Set media options
 *	@ifp: interface pointer
 *
 *	Basically boilerplate code that simply calls the mii functions to set
 *	the media options.
 *
 *	LOCKING:
 *	The device lock must be held before this function is called.
 *
 *	RETURNS:
 *	Returns 0 on success or a negative error code.
 */
static int
muge_ifmedia_upd(struct ifnet *ifp)
{
	struct muge_softc *sc = ifp->if_softc;
	muge_dbg_printf(sc, "Calling muge_ifmedia_upd.\n");
	struct mii_data *mii = uether_getmii(&sc->sc_ue);
	struct mii_softc *miisc;
	int err;

	MUGE_LOCK_ASSERT(sc, MA_OWNED);

	LIST_FOREACH(miisc, &mii->mii_phys, mii_list)
		PHY_RESET(miisc);
	err = mii_mediachg(mii);
	return (err);
}

/**
 *	muge_init - Initialises the LAN95xx chip
 *	@ue: USB ether interface
 *
 *	Called when the interface is brought up (i.e. ifconfig ue0 up), this
 *	initialise the interface and the rx/tx pipes.
 *
 *	LOCKING:
 *	Should be called with the MUGE lock held.
 */
static void
muge_init(struct usb_ether *ue)
{
	struct muge_softc *sc = uether_getsc(ue);
	muge_dbg_printf(sc, "Calling muge_init.\n");
	struct ifnet *ifp = uether_getifp(ue);
	MUGE_LOCK_ASSERT(sc, MA_OWNED);

	if (lan78xx_setmacaddress(sc, IF_LLADDR(ifp)))
		muge_dbg_printf(sc, "setting MAC address failed\n");

	if ((ifp->if_drv_flags & IFF_DRV_RUNNING) != 0)
		return;

	/* Cancel pending I/O. */
	muge_stop(ue);

	/* Reset the ethernet interface. */
	muge_reset(sc);

	/* Load the multicast filter. */
	muge_setmulti(ue);

	/* TCP/UDP checksum offload engines. */
	muge_sethwcsum(sc);

	usbd_xfer_set_stall(sc->sc_xfer[MUGE_BULK_DT_WR]);

	/* Indicate we are up and running. */
	ifp->if_drv_flags |= IFF_DRV_RUNNING;

	/* Switch to selected media. */
	muge_ifmedia_upd(ifp);
	muge_start(ue);
}

/**
 *	muge_stop - Stops communication with the LAN78xx chip
 *	@ue: USB ether interface
 */
static void
muge_stop(struct usb_ether *ue)
{
	struct muge_softc *sc = uether_getsc(ue);
	struct ifnet *ifp = uether_getifp(ue);

	MUGE_LOCK_ASSERT(sc, MA_OWNED);

	ifp->if_drv_flags &= ~(IFF_DRV_RUNNING | IFF_DRV_OACTIVE);
	sc->sc_flags &= ~MUGE_FLAG_LINK;

	/*
	 * Stop all the transfers, if not already stopped.
	 */
	usbd_transfer_stop(sc->sc_xfer[MUGE_BULK_DT_WR]);
	usbd_transfer_stop(sc->sc_xfer[MUGE_BULK_DT_RD]);
}

/**
 *	muge_tick - Called periodically to monitor the state of the LAN95xx chip
 *	@ue: USB ether interface
 *
 *	Simply calls the mii status functions to check the state of the link.
 *
 *	LOCKING:
 *	Should be called with the MUGE lock held.
 */
static void
muge_tick(struct usb_ether *ue)
{

	struct muge_softc *sc = uether_getsc(ue);
	struct mii_data *mii = uether_getmii(&sc->sc_ue);

	MUGE_LOCK_ASSERT(sc, MA_OWNED);

	mii_tick(mii);
	if ((sc->sc_flags & MUGE_FLAG_LINK) == 0) {
		lan78xx_miibus_statchg(ue->ue_dev);
		if ((sc->sc_flags & MUGE_FLAG_LINK) != 0)
			muge_start(ue);
	}
}

/**
 *	muge_ifmedia_sts - Report current media status
 *	@ifp: inet interface pointer
 *	@ifmr: interface media request
 *
 *	Call the mii functions to get the media status.
 *
 *	LOCKING:
 *	Internally takes and releases the device lock.
 */
static void
muge_ifmedia_sts(struct ifnet *ifp, struct ifmediareq *ifmr)
{
	struct muge_softc *sc = ifp->if_softc;
	struct mii_data *mii = uether_getmii(&sc->sc_ue);

	MUGE_LOCK(sc);
	mii_pollstat(mii);
	ifmr->ifm_active = mii->mii_media_active;
	ifmr->ifm_status = mii->mii_media_status;
	MUGE_UNLOCK(sc);
}

/**
 *	muge_probe - Probe the interface.
 *	@dev: muge device handle
 *
 *	Checks if the device is a match for this driver.
 *
 *	RETURNS:
 *	Returns 0 on success or an error code on failure.
 */
static int
muge_probe(device_t dev)
{
	struct usb_attach_arg *uaa = device_get_ivars(dev);

	if (uaa->usb_mode != USB_MODE_HOST)
		return (ENXIO);
	if (uaa->info.bConfigIndex != MUGE_CONFIG_INDEX)
		return (ENXIO);
	if (uaa->info.bIfaceIndex != MUGE_IFACE_IDX)
		return (ENXIO);
	return (usbd_lookup_id_by_uaa(lan78xx_devs, sizeof(lan78xx_devs), uaa));
}

/**
 *	muge_attach - Attach the interface.
 *	@dev: muge device handle
 *
 *	Allocate softc structures, do ifmedia setup and ethernet/BPF attach.
 *
 *	RETURNS:
 *	Returns 0 on success or a negative error code.
 */
static int
muge_attach(device_t dev)
{
	struct usb_attach_arg *uaa = device_get_ivars(dev);
	struct muge_softc *sc = device_get_softc(dev);
	struct usb_ether *ue = &sc->sc_ue;
	uint8_t iface_index;
	int err;

	sc->sc_flags = USB_GET_DRIVER_INFO(uaa);

	device_set_usb_desc(dev);

	mtx_init(&sc->sc_mtx, device_get_nameunit(dev), NULL, MTX_DEF);

	/* Setup the endpoints for the Microchip LAN78xx device. */
	iface_index = MUGE_IFACE_IDX;
	err = usbd_transfer_setup(uaa->device, &iface_index, sc->sc_xfer,
	    muge_config, MUGE_N_TRANSFER, sc, &sc->sc_mtx);
	if (err) {
		device_printf(dev, "error: allocating USB transfers failed\n");
		goto err;
	}

	ue->ue_sc = sc;
	ue->ue_dev = dev;
	ue->ue_udev = uaa->device;
	ue->ue_mtx = &sc->sc_mtx;
	ue->ue_methods = &muge_ue_methods;

	err = uether_ifattach(ue);
	if (err) {
		device_printf(dev, "error: could not attach interface\n");
		goto err_usbd;
	}

	/* Wait for lan78xx_chip_init from post-attach callback to complete. */
	uether_ifattach_wait(ue);
	if (!(sc->sc_flags & MUGE_FLAG_INIT_DONE))
		goto err_attached;

	return (0);

err_attached:
	uether_ifdetach(ue);
err_usbd:
	usbd_transfer_unsetup(sc->sc_xfer, MUGE_N_TRANSFER);
err:
	mtx_destroy(&sc->sc_mtx);
	return (ENXIO);
}

/**
 *	muge_detach - Detach the interface.
 *	@dev: muge device handle
 *
 *	RETURNS:
 *	Returns 0.
 */
static int
muge_detach(device_t dev)
{

	struct muge_softc *sc = device_get_softc(dev);
	struct usb_ether *ue = &sc->sc_ue;

	usbd_transfer_unsetup(sc->sc_xfer, MUGE_N_TRANSFER);
	uether_ifdetach(ue);
	mtx_destroy(&sc->sc_mtx);

	return (0);
}

static device_method_t muge_methods[] = {
	/* Device interface */
	DEVMETHOD(device_probe, muge_probe),
	DEVMETHOD(device_attach, muge_attach),
	DEVMETHOD(device_detach, muge_detach),

	/* Bus interface */
	DEVMETHOD(bus_print_child, bus_generic_print_child),
	DEVMETHOD(bus_driver_added, bus_generic_driver_added),

	/* MII interface */
	DEVMETHOD(miibus_readreg, lan78xx_miibus_readreg),
	DEVMETHOD(miibus_writereg, lan78xx_miibus_writereg),
	DEVMETHOD(miibus_statchg, lan78xx_miibus_statchg),

	DEVMETHOD_END
};

static driver_t muge_driver = {
	.name = "muge",
	.methods = muge_methods,
	.size = sizeof(struct muge_softc),
};

static devclass_t muge_devclass;

DRIVER_MODULE(muge, uhub, muge_driver, muge_devclass, NULL, 0);
DRIVER_MODULE(miibus, muge, miibus_driver, miibus_devclass, 0, 0);
MODULE_DEPEND(muge, uether, 1, 1, 1);
MODULE_DEPEND(muge, usb, 1, 1, 1);
MODULE_DEPEND(muge, ether, 1, 1, 1);
MODULE_DEPEND(muge, miibus, 1, 1, 1);
MODULE_VERSION(muge, 1);
USB_PNP_HOST_INFO(lan78xx_devs);
