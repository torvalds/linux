/*
 * Framework and drivers for configuring and reading different PHYs
 * Based on code in sungem_phy.c and (long-removed) gianfar_phy.c
 *
 * Author: Andy Fleming
 *
 * Copyright (c) 2004 Freescale Semiconductor, Inc.
 *
 * This program is free software; you can redistribute  it and/or modify it
 * under  the terms of  the GNU General  Public License as published by the
 * Free Software Foundation;  either version 2 of the  License, or (at your
 * option) any later version.
 *
 */

#ifndef __PHY_H
#define __PHY_H

#include <linux/compiler.h>
#include <linux/spinlock.h>
#include <linux/ethtool.h>
#include <linux/linkmode.h>
#include <linux/mdio.h>
#include <linux/mii.h>
#include <linux/module.h>
#include <linux/timer.h>
#include <linux/workqueue.h>
#include <linux/mod_devicetable.h>

#include <linux/atomic.h>

#define PHY_DEFAULT_FEATURES	(SUPPORTED_Autoneg | \
				 SUPPORTED_TP | \
				 SUPPORTED_MII)

#define PHY_10BT_FEATURES	(SUPPORTED_10baseT_Half | \
				 SUPPORTED_10baseT_Full)

#define PHY_100BT_FEATURES	(SUPPORTED_100baseT_Half | \
				 SUPPORTED_100baseT_Full)

#define PHY_1000BT_FEATURES	(SUPPORTED_1000baseT_Half | \
				 SUPPORTED_1000baseT_Full)

extern __ETHTOOL_DECLARE_LINK_MODE_MASK(phy_basic_features) __ro_after_init;
extern __ETHTOOL_DECLARE_LINK_MODE_MASK(phy_basic_t1_features) __ro_after_init;
extern __ETHTOOL_DECLARE_LINK_MODE_MASK(phy_gbit_features) __ro_after_init;
extern __ETHTOOL_DECLARE_LINK_MODE_MASK(phy_gbit_fibre_features) __ro_after_init;
extern __ETHTOOL_DECLARE_LINK_MODE_MASK(phy_gbit_all_ports_features) __ro_after_init;
extern __ETHTOOL_DECLARE_LINK_MODE_MASK(phy_10gbit_features) __ro_after_init;
extern __ETHTOOL_DECLARE_LINK_MODE_MASK(phy_10gbit_fec_features) __ro_after_init;
extern __ETHTOOL_DECLARE_LINK_MODE_MASK(phy_10gbit_full_features) __ro_after_init;

#define PHY_BASIC_FEATURES ((unsigned long *)&phy_basic_features)
#define PHY_BASIC_T1_FEATURES ((unsigned long *)&phy_basic_t1_features)
#define PHY_GBIT_FEATURES ((unsigned long *)&phy_gbit_features)
#define PHY_GBIT_FIBRE_FEATURES ((unsigned long *)&phy_gbit_fibre_features)
#define PHY_GBIT_ALL_PORTS_FEATURES ((unsigned long *)&phy_gbit_all_ports_features)
#define PHY_10GBIT_FEATURES ((unsigned long *)&phy_10gbit_features)
#define PHY_10GBIT_FEC_FEATURES ((unsigned long *)&phy_10gbit_fec_features)
#define PHY_10GBIT_FULL_FEATURES ((unsigned long *)&phy_10gbit_full_features)

extern const int phy_10_100_features_array[4];
extern const int phy_basic_t1_features_array[2];
extern const int phy_gbit_features_array[2];
extern const int phy_10gbit_features_array[1];

/*
 * Set phydev->irq to PHY_POLL if interrupts are not supported,
 * or not desired for this PHY.  Set to PHY_IGNORE_INTERRUPT if
 * the attached driver handles the interrupt
 */
#define PHY_POLL		-1
#define PHY_IGNORE_INTERRUPT	-2

#define PHY_IS_INTERNAL		0x00000001
#define PHY_RST_AFTER_CLK_EN	0x00000002
#define MDIO_DEVICE_IS_PHY	0x80000000

/* Interface Mode definitions */
typedef enum {
	PHY_INTERFACE_MODE_NA,
	PHY_INTERFACE_MODE_INTERNAL,
	PHY_INTERFACE_MODE_MII,
	PHY_INTERFACE_MODE_GMII,
	PHY_INTERFACE_MODE_SGMII,
	PHY_INTERFACE_MODE_TBI,
	PHY_INTERFACE_MODE_REVMII,
	PHY_INTERFACE_MODE_RMII,
	PHY_INTERFACE_MODE_RGMII,
	PHY_INTERFACE_MODE_RGMII_ID,
	PHY_INTERFACE_MODE_RGMII_RXID,
	PHY_INTERFACE_MODE_RGMII_TXID,
	PHY_INTERFACE_MODE_RTBI,
	PHY_INTERFACE_MODE_SMII,
	PHY_INTERFACE_MODE_XGMII,
	PHY_INTERFACE_MODE_MOCA,
	PHY_INTERFACE_MODE_QSGMII,
	PHY_INTERFACE_MODE_TRGMII,
	PHY_INTERFACE_MODE_1000BASEX,
	PHY_INTERFACE_MODE_2500BASEX,
	PHY_INTERFACE_MODE_RXAUI,
	PHY_INTERFACE_MODE_XAUI,
	/* 10GBASE-KR, XFI, SFI - single lane 10G Serdes */
	PHY_INTERFACE_MODE_10GKR,
	PHY_INTERFACE_MODE_MAX,
} phy_interface_t;

/**
 * phy_supported_speeds - return all speeds currently supported by a phy device
 * @phy: The phy device to return supported speeds of.
 * @speeds: buffer to store supported speeds in.
 * @size: size of speeds buffer.
 *
 * Description: Returns the number of supported speeds, and fills
 * the speeds buffer with the supported speeds. If speeds buffer is
 * too small to contain all currently supported speeds, will return as
 * many speeds as can fit.
 */
unsigned int phy_supported_speeds(struct phy_device *phy,
				      unsigned int *speeds,
				      unsigned int size);

/**
 * phy_modes - map phy_interface_t enum to device tree binding of phy-mode
 * @interface: enum phy_interface_t value
 *
 * Description: maps 'enum phy_interface_t' defined in this file
 * into the device tree binding of 'phy-mode', so that Ethernet
 * device driver can get phy interface from device tree.
 */
static inline const char *phy_modes(phy_interface_t interface)
{
	switch (interface) {
	case PHY_INTERFACE_MODE_NA:
		return "";
	case PHY_INTERFACE_MODE_INTERNAL:
		return "internal";
	case PHY_INTERFACE_MODE_MII:
		return "mii";
	case PHY_INTERFACE_MODE_GMII:
		return "gmii";
	case PHY_INTERFACE_MODE_SGMII:
		return "sgmii";
	case PHY_INTERFACE_MODE_TBI:
		return "tbi";
	case PHY_INTERFACE_MODE_REVMII:
		return "rev-mii";
	case PHY_INTERFACE_MODE_RMII:
		return "rmii";
	case PHY_INTERFACE_MODE_RGMII:
		return "rgmii";
	case PHY_INTERFACE_MODE_RGMII_ID:
		return "rgmii-id";
	case PHY_INTERFACE_MODE_RGMII_RXID:
		return "rgmii-rxid";
	case PHY_INTERFACE_MODE_RGMII_TXID:
		return "rgmii-txid";
	case PHY_INTERFACE_MODE_RTBI:
		return "rtbi";
	case PHY_INTERFACE_MODE_SMII:
		return "smii";
	case PHY_INTERFACE_MODE_XGMII:
		return "xgmii";
	case PHY_INTERFACE_MODE_MOCA:
		return "moca";
	case PHY_INTERFACE_MODE_QSGMII:
		return "qsgmii";
	case PHY_INTERFACE_MODE_TRGMII:
		return "trgmii";
	case PHY_INTERFACE_MODE_1000BASEX:
		return "1000base-x";
	case PHY_INTERFACE_MODE_2500BASEX:
		return "2500base-x";
	case PHY_INTERFACE_MODE_RXAUI:
		return "rxaui";
	case PHY_INTERFACE_MODE_XAUI:
		return "xaui";
	case PHY_INTERFACE_MODE_10GKR:
		return "10gbase-kr";
	default:
		return "unknown";
	}
}


#define PHY_INIT_TIMEOUT	100000
#define PHY_STATE_TIME		1
#define PHY_FORCE_TIMEOUT	10

#define PHY_MAX_ADDR	32

/* Used when trying to connect to a specific phy (mii bus id:phy device id) */
#define PHY_ID_FMT "%s:%02x"

#define MII_BUS_ID_SIZE	61

/* Or MII_ADDR_C45 into regnum for read/write on mii_bus to enable the 21 bit
   IEEE 802.3ae clause 45 addressing mode used by 10GIGE phy chips. */
#define MII_ADDR_C45 (1<<30)

struct device;
struct phylink;
struct sk_buff;

/*
 * The Bus class for PHYs.  Devices which provide access to
 * PHYs should register using this structure
 */
struct mii_bus {
	struct module *owner;
	const char *name;
	char id[MII_BUS_ID_SIZE];
	void *priv;
	int (*read)(struct mii_bus *bus, int addr, int regnum);
	int (*write)(struct mii_bus *bus, int addr, int regnum, u16 val);
	int (*reset)(struct mii_bus *bus);

	/*
	 * A lock to ensure that only one thing can read/write
	 * the MDIO bus at a time
	 */
	struct mutex mdio_lock;

	struct device *parent;
	enum {
		MDIOBUS_ALLOCATED = 1,
		MDIOBUS_REGISTERED,
		MDIOBUS_UNREGISTERED,
		MDIOBUS_RELEASED,
	} state;
	struct device dev;

	/* list of all PHYs on bus */
	struct mdio_device *mdio_map[PHY_MAX_ADDR];

	/* PHY addresses to be ignored when probing */
	u32 phy_mask;

	/* PHY addresses to ignore the TA/read failure */
	u32 phy_ignore_ta_mask;

	/*
	 * An array of interrupts, each PHY's interrupt at the index
	 * matching its address
	 */
	int irq[PHY_MAX_ADDR];

	/* GPIO reset pulse width in microseconds */
	int reset_delay_us;
	/* RESET GPIO descriptor pointer */
	struct gpio_desc *reset_gpiod;
};
#define to_mii_bus(d) container_of(d, struct mii_bus, dev)

struct mii_bus *mdiobus_alloc_size(size_t);
static inline struct mii_bus *mdiobus_alloc(void)
{
	return mdiobus_alloc_size(0);
}

int __mdiobus_register(struct mii_bus *bus, struct module *owner);
#define mdiobus_register(bus) __mdiobus_register(bus, THIS_MODULE)
void mdiobus_unregister(struct mii_bus *bus);
void mdiobus_free(struct mii_bus *bus);
struct mii_bus *devm_mdiobus_alloc_size(struct device *dev, int sizeof_priv);
static inline struct mii_bus *devm_mdiobus_alloc(struct device *dev)
{
	return devm_mdiobus_alloc_size(dev, 0);
}

void devm_mdiobus_free(struct device *dev, struct mii_bus *bus);
struct phy_device *mdiobus_scan(struct mii_bus *bus, int addr);

#define PHY_INTERRUPT_DISABLED	false
#define PHY_INTERRUPT_ENABLED	true

/* PHY state machine states:
 *
 * DOWN: PHY device and driver are not ready for anything.  probe
 * should be called if and only if the PHY is in this state,
 * given that the PHY device exists.
 * - PHY driver probe function will set the state to READY
 *
 * READY: PHY is ready to send and receive packets, but the
 * controller is not.  By default, PHYs which do not implement
 * probe will be set to this state by phy_probe().
 * - start will set the state to UP
 *
 * UP: The PHY and attached device are ready to do work.
 * Interrupts should be started here.
 * - timer moves to NOLINK or RUNNING
 *
 * NOLINK: PHY is up, but not currently plugged in.
 * - irq or timer will set RUNNING if link comes back
 * - phy_stop moves to HALTED
 *
 * FORCING: PHY is being configured with forced settings
 * - if link is up, move to RUNNING
 * - If link is down, we drop to the next highest setting, and
 *   retry (FORCING) after a timeout
 * - phy_stop moves to HALTED
 *
 * RUNNING: PHY is currently up, running, and possibly sending
 * and/or receiving packets
 * - irq or timer will set NOLINK if link goes down
 * - phy_stop moves to HALTED
 *
 * HALTED: PHY is up, but no polling or interrupts are done. Or
 * PHY is in an error state.
 *
 * - phy_start moves to RESUMING
 *
 * RESUMING: PHY was halted, but now wants to run again.
 * - If we are forcing, or aneg is done, timer moves to RUNNING
 * - If aneg is not done, timer moves to AN
 * - phy_stop moves to HALTED
 */
enum phy_state {
	PHY_DOWN = 0,
	PHY_READY,
	PHY_HALTED,
	PHY_UP,
	PHY_RUNNING,
	PHY_NOLINK,
	PHY_FORCING,
	PHY_RESUMING
};

/**
 * struct phy_c45_device_ids - 802.3-c45 Device Identifiers
 * @devices_in_package: Bit vector of devices present.
 * @device_ids: The device identifer for each present device.
 */
struct phy_c45_device_ids {
	u32 devices_in_package;
	u32 device_ids[8];
};

/* phy_device: An instance of a PHY
 *
 * drv: Pointer to the driver for this PHY instance
 * phy_id: UID for this device found during discovery
 * c45_ids: 802.3-c45 Device Identifers if is_c45.
 * is_c45:  Set to true if this phy uses clause 45 addressing.
 * is_internal: Set to true if this phy is internal to a MAC.
 * is_pseudo_fixed_link: Set to true if this phy is an Ethernet switch, etc.
 * has_fixups: Set to true if this phy has fixups/quirks.
 * suspended: Set to true if this phy has been suspended successfully.
 * sysfs_links: Internal boolean tracking sysfs symbolic links setup/removal.
 * loopback_enabled: Set true if this phy has been loopbacked successfully.
 * state: state of the PHY for management purposes
 * dev_flags: Device-specific flags used by the PHY driver.
 * link_timeout: The number of timer firings to wait before the
 * giving up on the current attempt at acquiring a link
 * irq: IRQ number of the PHY's interrupt (-1 if none)
 * phy_timer: The timer for handling the state machine
 * attached_dev: The attached enet driver's device instance ptr
 * adjust_link: Callback for the enet controller to respond to
 * changes in the link state.
 *
 * speed, duplex, pause, supported, advertising, lp_advertising,
 * and autoneg are used like in mii_if_info
 *
 * interrupts currently only supports enabled or disabled,
 * but could be changed in the future to support enabling
 * and disabling specific interrupts
 *
 * Contains some infrastructure for polling and interrupt
 * handling, as well as handling shifts in PHY hardware state
 */
struct phy_device {
	struct mdio_device mdio;

	/* Information about the PHY type */
	/* And management functions */
	struct phy_driver *drv;

	u32 phy_id;

	struct phy_c45_device_ids c45_ids;
	unsigned is_c45:1;
	unsigned is_internal:1;
	unsigned is_pseudo_fixed_link:1;
	unsigned has_fixups:1;
	unsigned suspended:1;
	unsigned sysfs_links:1;
	unsigned loopback_enabled:1;

	unsigned autoneg:1;
	/* The most recently read link state */
	unsigned link:1;

	/* Interrupts are enabled */
	unsigned interrupts:1;

	enum phy_state state;

	u32 dev_flags;

	phy_interface_t interface;

	/*
	 * forced speed & duplex (no autoneg)
	 * partner speed & duplex & pause (autoneg)
	 */
	int speed;
	int duplex;
	int pause;
	int asym_pause;

	/* Union of PHY and Attached devices' supported link modes */
	/* See ethtool.h for more info */
	__ETHTOOL_DECLARE_LINK_MODE_MASK(supported);
	__ETHTOOL_DECLARE_LINK_MODE_MASK(advertising);
	__ETHTOOL_DECLARE_LINK_MODE_MASK(lp_advertising);

	/* Energy efficient ethernet modes which should be prohibited */
	u32 eee_broken_modes;

	int link_timeout;

#ifdef CONFIG_LED_TRIGGER_PHY
	struct phy_led_trigger *phy_led_triggers;
	unsigned int phy_num_led_triggers;
	struct phy_led_trigger *last_triggered;

	struct phy_led_trigger *led_link_trigger;
#endif

	/*
	 * Interrupt number for this PHY
	 * -1 means no interrupt
	 */
	int irq;

	/* private data pointer */
	/* For use by PHYs to maintain extra state */
	void *priv;

	/* Interrupt and Polling infrastructure */
	struct delayed_work state_queue;

	struct mutex lock;

	struct phylink *phylink;
	struct net_device *attached_dev;

	u8 mdix;
	u8 mdix_ctrl;

	void (*phy_link_change)(struct phy_device *, bool up, bool do_carrier);
	void (*adjust_link)(struct net_device *dev);
};
#define to_phy_device(d) container_of(to_mdio_device(d), \
				      struct phy_device, mdio)

/* struct phy_driver: Driver structure for a particular PHY type
 *
 * driver_data: static driver data
 * phy_id: The result of reading the UID registers of this PHY
 *   type, and ANDing them with the phy_id_mask.  This driver
 *   only works for PHYs with IDs which match this field
 * name: The friendly name of this PHY type
 * phy_id_mask: Defines the important bits of the phy_id
 * features: A mandatory list of features (speed, duplex, etc)
 *   supported by this PHY
 * flags: A bitfield defining certain other features this PHY
 *   supports (like interrupts)
 *
 * All functions are optional. If config_aneg or read_status
 * are not implemented, the phy core uses the genphy versions.
 * Note that none of these functions should be called from
 * interrupt time. The goal is for the bus read/write functions
 * to be able to block when the bus transaction is happening,
 * and be freed up by an interrupt (The MPC85xx has this ability,
 * though it is not currently supported in the driver).
 */
struct phy_driver {
	struct mdio_driver_common mdiodrv;
	u32 phy_id;
	char *name;
	u32 phy_id_mask;
	const unsigned long * const features;
	u32 flags;
	const void *driver_data;

	/*
	 * Called to issue a PHY software reset
	 */
	int (*soft_reset)(struct phy_device *phydev);

	/*
	 * Called to initialize the PHY,
	 * including after a reset
	 */
	int (*config_init)(struct phy_device *phydev);

	/*
	 * Called during discovery.  Used to set
	 * up device-specific structures, if any
	 */
	int (*probe)(struct phy_device *phydev);

	/*
	 * Probe the hardware to determine what abilities it has.
	 * Should only set phydev->supported.
	 */
	int (*get_features)(struct phy_device *phydev);

	/* PHY Power Management */
	int (*suspend)(struct phy_device *phydev);
	int (*resume)(struct phy_device *phydev);

	/*
	 * Configures the advertisement and resets
	 * autonegotiation if phydev->autoneg is on,
	 * forces the speed to the current settings in phydev
	 * if phydev->autoneg is off
	 */
	int (*config_aneg)(struct phy_device *phydev);

	/* Determines the auto negotiation result */
	int (*aneg_done)(struct phy_device *phydev);

	/* Determines the negotiated speed and duplex */
	int (*read_status)(struct phy_device *phydev);

	/* Clears any pending interrupts */
	int (*ack_interrupt)(struct phy_device *phydev);

	/* Enables or disables interrupts */
	int (*config_intr)(struct phy_device *phydev);

	/*
	 * Checks if the PHY generated an interrupt.
	 * For multi-PHY devices with shared PHY interrupt pin
	 */
	int (*did_interrupt)(struct phy_device *phydev);

	/* Clears up any memory if needed */
	void (*remove)(struct phy_device *phydev);

	/* Returns true if this is a suitable driver for the given
	 * phydev.  If NULL, matching is based on phy_id and
	 * phy_id_mask.
	 */
	int (*match_phy_device)(struct phy_device *phydev);

	/* Handles ethtool queries for hardware time stamping. */
	int (*ts_info)(struct phy_device *phydev, struct ethtool_ts_info *ti);

	/* Handles SIOCSHWTSTAMP ioctl for hardware time stamping. */
	int  (*hwtstamp)(struct phy_device *phydev, struct ifreq *ifr);

	/*
	 * Requests a Rx timestamp for 'skb'. If the skb is accepted,
	 * the phy driver promises to deliver it using netif_rx() as
	 * soon as a timestamp becomes available. One of the
	 * PTP_CLASS_ values is passed in 'type'. The function must
	 * return true if the skb is accepted for delivery.
	 */
	bool (*rxtstamp)(struct phy_device *dev, struct sk_buff *skb, int type);

	/*
	 * Requests a Tx timestamp for 'skb'. The phy driver promises
	 * to deliver it using skb_complete_tx_timestamp() as soon as a
	 * timestamp becomes available. One of the PTP_CLASS_ values
	 * is passed in 'type'.
	 */
	void (*txtstamp)(struct phy_device *dev, struct sk_buff *skb, int type);

	/* Some devices (e.g. qnap TS-119P II) require PHY register changes to
	 * enable Wake on LAN, so set_wol is provided to be called in the
	 * ethernet driver's set_wol function. */
	int (*set_wol)(struct phy_device *dev, struct ethtool_wolinfo *wol);

	/* See set_wol, but for checking whether Wake on LAN is enabled. */
	void (*get_wol)(struct phy_device *dev, struct ethtool_wolinfo *wol);

	/*
	 * Called to inform a PHY device driver when the core is about to
	 * change the link state. This callback is supposed to be used as
	 * fixup hook for drivers that need to take action when the link
	 * state changes. Drivers are by no means allowed to mess with the
	 * PHY device structure in their implementations.
	 */
	void (*link_change_notify)(struct phy_device *dev);

	/*
	 * Phy specific driver override for reading a MMD register.
	 * This function is optional for PHY specific drivers.  When
	 * not provided, the default MMD read function will be used
	 * by phy_read_mmd(), which will use either a direct read for
	 * Clause 45 PHYs or an indirect read for Clause 22 PHYs.
	 *  devnum is the MMD device number within the PHY device,
	 *  regnum is the register within the selected MMD device.
	 */
	int (*read_mmd)(struct phy_device *dev, int devnum, u16 regnum);

	/*
	 * Phy specific driver override for writing a MMD register.
	 * This function is optional for PHY specific drivers.  When
	 * not provided, the default MMD write function will be used
	 * by phy_write_mmd(), which will use either a direct write for
	 * Clause 45 PHYs, or an indirect write for Clause 22 PHYs.
	 *  devnum is the MMD device number within the PHY device,
	 *  regnum is the register within the selected MMD device.
	 *  val is the value to be written.
	 */
	int (*write_mmd)(struct phy_device *dev, int devnum, u16 regnum,
			 u16 val);

	int (*read_page)(struct phy_device *dev);
	int (*write_page)(struct phy_device *dev, int page);

	/* Get the size and type of the eeprom contained within a plug-in
	 * module */
	int (*module_info)(struct phy_device *dev,
			   struct ethtool_modinfo *modinfo);

	/* Get the eeprom information from the plug-in module */
	int (*module_eeprom)(struct phy_device *dev,
			     struct ethtool_eeprom *ee, u8 *data);

	/* Get statistics from the phy using ethtool */
	int (*get_sset_count)(struct phy_device *dev);
	void (*get_strings)(struct phy_device *dev, u8 *data);
	void (*get_stats)(struct phy_device *dev,
			  struct ethtool_stats *stats, u64 *data);

	/* Get and Set PHY tunables */
	int (*get_tunable)(struct phy_device *dev,
			   struct ethtool_tunable *tuna, void *data);
	int (*set_tunable)(struct phy_device *dev,
			    struct ethtool_tunable *tuna,
			    const void *data);
	int (*set_loopback)(struct phy_device *dev, bool enable);
};
#define to_phy_driver(d) container_of(to_mdio_common_driver(d),		\
				      struct phy_driver, mdiodrv)

#define PHY_ANY_ID "MATCH ANY PHY"
#define PHY_ANY_UID 0xffffffff

#define PHY_ID_MATCH_EXACT(id) .phy_id = (id), .phy_id_mask = GENMASK(31, 0)
#define PHY_ID_MATCH_MODEL(id) .phy_id = (id), .phy_id_mask = GENMASK(31, 4)
#define PHY_ID_MATCH_VENDOR(id) .phy_id = (id), .phy_id_mask = GENMASK(31, 10)

/* A Structure for boards to register fixups with the PHY Lib */
struct phy_fixup {
	struct list_head list;
	char bus_id[MII_BUS_ID_SIZE + 3];
	u32 phy_uid;
	u32 phy_uid_mask;
	int (*run)(struct phy_device *phydev);
};

const char *phy_speed_to_str(int speed);
const char *phy_duplex_to_str(unsigned int duplex);

/* A structure for mapping a particular speed and duplex
 * combination to a particular SUPPORTED and ADVERTISED value
 */
struct phy_setting {
	u32 speed;
	u8 duplex;
	u8 bit;
};

const struct phy_setting *
phy_lookup_setting(int speed, int duplex, const unsigned long *mask,
		   bool exact);
size_t phy_speeds(unsigned int *speeds, size_t size,
		  unsigned long *mask);
void of_set_phy_supported(struct phy_device *phydev);
void of_set_phy_eee_broken(struct phy_device *phydev);

/**
 * phy_is_started - Convenience function to check whether PHY is started
 * @phydev: The phy_device struct
 */
static inline bool phy_is_started(struct phy_device *phydev)
{
	return phydev->state >= PHY_UP;
}

void phy_resolve_aneg_linkmode(struct phy_device *phydev);

/**
 * phy_read - Convenience function for reading a given PHY register
 * @phydev: the phy_device struct
 * @regnum: register number to read
 *
 * NOTE: MUST NOT be called from interrupt context,
 * because the bus read/write functions may wait for an interrupt
 * to conclude the operation.
 */
static inline int phy_read(struct phy_device *phydev, u32 regnum)
{
	return mdiobus_read(phydev->mdio.bus, phydev->mdio.addr, regnum);
}

/**
 * __phy_read - convenience function for reading a given PHY register
 * @phydev: the phy_device struct
 * @regnum: register number to read
 *
 * The caller must have taken the MDIO bus lock.
 */
static inline int __phy_read(struct phy_device *phydev, u32 regnum)
{
	return __mdiobus_read(phydev->mdio.bus, phydev->mdio.addr, regnum);
}

/**
 * phy_write - Convenience function for writing a given PHY register
 * @phydev: the phy_device struct
 * @regnum: register number to write
 * @val: value to write to @regnum
 *
 * NOTE: MUST NOT be called from interrupt context,
 * because the bus read/write functions may wait for an interrupt
 * to conclude the operation.
 */
static inline int phy_write(struct phy_device *phydev, u32 regnum, u16 val)
{
	return mdiobus_write(phydev->mdio.bus, phydev->mdio.addr, regnum, val);
}

/**
 * __phy_write - Convenience function for writing a given PHY register
 * @phydev: the phy_device struct
 * @regnum: register number to write
 * @val: value to write to @regnum
 *
 * The caller must have taken the MDIO bus lock.
 */
static inline int __phy_write(struct phy_device *phydev, u32 regnum, u16 val)
{
	return __mdiobus_write(phydev->mdio.bus, phydev->mdio.addr, regnum,
			       val);
}

/**
 * phy_read_mmd - Convenience function for reading a register
 * from an MMD on a given PHY.
 * @phydev: The phy_device struct
 * @devad: The MMD to read from
 * @regnum: The register on the MMD to read
 *
 * Same rules as for phy_read();
 */
int phy_read_mmd(struct phy_device *phydev, int devad, u32 regnum);

/**
 * __phy_read_mmd - Convenience function for reading a register
 * from an MMD on a given PHY.
 * @phydev: The phy_device struct
 * @devad: The MMD to read from
 * @regnum: The register on the MMD to read
 *
 * Same rules as for __phy_read();
 */
int __phy_read_mmd(struct phy_device *phydev, int devad, u32 regnum);

/**
 * phy_write_mmd - Convenience function for writing a register
 * on an MMD on a given PHY.
 * @phydev: The phy_device struct
 * @devad: The MMD to write to
 * @regnum: The register on the MMD to read
 * @val: value to write to @regnum
 *
 * Same rules as for phy_write();
 */
int phy_write_mmd(struct phy_device *phydev, int devad, u32 regnum, u16 val);

/**
 * __phy_write_mmd - Convenience function for writing a register
 * on an MMD on a given PHY.
 * @phydev: The phy_device struct
 * @devad: The MMD to write to
 * @regnum: The register on the MMD to read
 * @val: value to write to @regnum
 *
 * Same rules as for __phy_write();
 */
int __phy_write_mmd(struct phy_device *phydev, int devad, u32 regnum, u16 val);

int __phy_modify_changed(struct phy_device *phydev, u32 regnum, u16 mask,
			 u16 set);
int phy_modify_changed(struct phy_device *phydev, u32 regnum, u16 mask,
		       u16 set);
int __phy_modify(struct phy_device *phydev, u32 regnum, u16 mask, u16 set);
int phy_modify(struct phy_device *phydev, u32 regnum, u16 mask, u16 set);

int __phy_modify_mmd_changed(struct phy_device *phydev, int devad, u32 regnum,
			     u16 mask, u16 set);
int phy_modify_mmd_changed(struct phy_device *phydev, int devad, u32 regnum,
			   u16 mask, u16 set);
int __phy_modify_mmd(struct phy_device *phydev, int devad, u32 regnum,
		     u16 mask, u16 set);
int phy_modify_mmd(struct phy_device *phydev, int devad, u32 regnum,
		   u16 mask, u16 set);

/**
 * __phy_set_bits - Convenience function for setting bits in a PHY register
 * @phydev: the phy_device struct
 * @regnum: register number to write
 * @val: bits to set
 *
 * The caller must have taken the MDIO bus lock.
 */
static inline int __phy_set_bits(struct phy_device *phydev, u32 regnum, u16 val)
{
	return __phy_modify(phydev, regnum, 0, val);
}

/**
 * __phy_clear_bits - Convenience function for clearing bits in a PHY register
 * @phydev: the phy_device struct
 * @regnum: register number to write
 * @val: bits to clear
 *
 * The caller must have taken the MDIO bus lock.
 */
static inline int __phy_clear_bits(struct phy_device *phydev, u32 regnum,
				   u16 val)
{
	return __phy_modify(phydev, regnum, val, 0);
}

/**
 * phy_set_bits - Convenience function for setting bits in a PHY register
 * @phydev: the phy_device struct
 * @regnum: register number to write
 * @val: bits to set
 */
static inline int phy_set_bits(struct phy_device *phydev, u32 regnum, u16 val)
{
	return phy_modify(phydev, regnum, 0, val);
}

/**
 * phy_clear_bits - Convenience function for clearing bits in a PHY register
 * @phydev: the phy_device struct
 * @regnum: register number to write
 * @val: bits to clear
 */
static inline int phy_clear_bits(struct phy_device *phydev, u32 regnum, u16 val)
{
	return phy_modify(phydev, regnum, val, 0);
}

/**
 * __phy_set_bits_mmd - Convenience function for setting bits in a register
 * on MMD
 * @phydev: the phy_device struct
 * @devad: the MMD containing register to modify
 * @regnum: register number to modify
 * @val: bits to set
 *
 * The caller must have taken the MDIO bus lock.
 */
static inline int __phy_set_bits_mmd(struct phy_device *phydev, int devad,
		u32 regnum, u16 val)
{
	return __phy_modify_mmd(phydev, devad, regnum, 0, val);
}

/**
 * __phy_clear_bits_mmd - Convenience function for clearing bits in a register
 * on MMD
 * @phydev: the phy_device struct
 * @devad: the MMD containing register to modify
 * @regnum: register number to modify
 * @val: bits to clear
 *
 * The caller must have taken the MDIO bus lock.
 */
static inline int __phy_clear_bits_mmd(struct phy_device *phydev, int devad,
		u32 regnum, u16 val)
{
	return __phy_modify_mmd(phydev, devad, regnum, val, 0);
}

/**
 * phy_set_bits_mmd - Convenience function for setting bits in a register
 * on MMD
 * @phydev: the phy_device struct
 * @devad: the MMD containing register to modify
 * @regnum: register number to modify
 * @val: bits to set
 */
static inline int phy_set_bits_mmd(struct phy_device *phydev, int devad,
		u32 regnum, u16 val)
{
	return phy_modify_mmd(phydev, devad, regnum, 0, val);
}

/**
 * phy_clear_bits_mmd - Convenience function for clearing bits in a register
 * on MMD
 * @phydev: the phy_device struct
 * @devad: the MMD containing register to modify
 * @regnum: register number to modify
 * @val: bits to clear
 */
static inline int phy_clear_bits_mmd(struct phy_device *phydev, int devad,
		u32 regnum, u16 val)
{
	return phy_modify_mmd(phydev, devad, regnum, val, 0);
}

/**
 * phy_interrupt_is_valid - Convenience function for testing a given PHY irq
 * @phydev: the phy_device struct
 *
 * NOTE: must be kept in sync with addition/removal of PHY_POLL and
 * PHY_IGNORE_INTERRUPT
 */
static inline bool phy_interrupt_is_valid(struct phy_device *phydev)
{
	return phydev->irq != PHY_POLL && phydev->irq != PHY_IGNORE_INTERRUPT;
}

/**
 * phy_polling_mode - Convenience function for testing whether polling is
 * used to detect PHY status changes
 * @phydev: the phy_device struct
 */
static inline bool phy_polling_mode(struct phy_device *phydev)
{
	return phydev->irq == PHY_POLL;
}

/**
 * phy_is_internal - Convenience function for testing if a PHY is internal
 * @phydev: the phy_device struct
 */
static inline bool phy_is_internal(struct phy_device *phydev)
{
	return phydev->is_internal;
}

/**
 * phy_interface_mode_is_rgmii - Convenience function for testing if a
 * PHY interface mode is RGMII (all variants)
 * @mode: the phy_interface_t enum
 */
static inline bool phy_interface_mode_is_rgmii(phy_interface_t mode)
{
	return mode >= PHY_INTERFACE_MODE_RGMII &&
		mode <= PHY_INTERFACE_MODE_RGMII_TXID;
};

/**
 * phy_interface_mode_is_8023z() - does the phy interface mode use 802.3z
 *   negotiation
 * @mode: one of &enum phy_interface_t
 *
 * Returns true if the phy interface mode uses the 16-bit negotiation
 * word as defined in 802.3z. (See 802.3-2015 37.2.1 Config_Reg encoding)
 */
static inline bool phy_interface_mode_is_8023z(phy_interface_t mode)
{
	return mode == PHY_INTERFACE_MODE_1000BASEX ||
	       mode == PHY_INTERFACE_MODE_2500BASEX;
}

/**
 * phy_interface_is_rgmii - Convenience function for testing if a PHY interface
 * is RGMII (all variants)
 * @phydev: the phy_device struct
 */
static inline bool phy_interface_is_rgmii(struct phy_device *phydev)
{
	return phy_interface_mode_is_rgmii(phydev->interface);
};

/*
 * phy_is_pseudo_fixed_link - Convenience function for testing if this
 * PHY is the CPU port facing side of an Ethernet switch, or similar.
 * @phydev: the phy_device struct
 */
static inline bool phy_is_pseudo_fixed_link(struct phy_device *phydev)
{
	return phydev->is_pseudo_fixed_link;
}

int phy_save_page(struct phy_device *phydev);
int phy_select_page(struct phy_device *phydev, int page);
int phy_restore_page(struct phy_device *phydev, int oldpage, int ret);
int phy_read_paged(struct phy_device *phydev, int page, u32 regnum);
int phy_write_paged(struct phy_device *phydev, int page, u32 regnum, u16 val);
int phy_modify_paged(struct phy_device *phydev, int page, u32 regnum,
		     u16 mask, u16 set);

struct phy_device *phy_device_create(struct mii_bus *bus, int addr, int phy_id,
				     bool is_c45,
				     struct phy_c45_device_ids *c45_ids);
#if IS_ENABLED(CONFIG_PHYLIB)
struct phy_device *get_phy_device(struct mii_bus *bus, int addr, bool is_c45);
int phy_device_register(struct phy_device *phy);
void phy_device_free(struct phy_device *phydev);
#else
static inline
struct phy_device *get_phy_device(struct mii_bus *bus, int addr, bool is_c45)
{
	return NULL;
}

static inline int phy_device_register(struct phy_device *phy)
{
	return 0;
}

static inline void phy_device_free(struct phy_device *phydev) { }
#endif /* CONFIG_PHYLIB */
void phy_device_remove(struct phy_device *phydev);
int phy_init_hw(struct phy_device *phydev);
int phy_suspend(struct phy_device *phydev);
int phy_resume(struct phy_device *phydev);
int __phy_resume(struct phy_device *phydev);
int phy_loopback(struct phy_device *phydev, bool enable);
struct phy_device *phy_attach(struct net_device *dev, const char *bus_id,
			      phy_interface_t interface);
struct phy_device *phy_find_first(struct mii_bus *bus);
int phy_attach_direct(struct net_device *dev, struct phy_device *phydev,
		      u32 flags, phy_interface_t interface);
int phy_connect_direct(struct net_device *dev, struct phy_device *phydev,
		       void (*handler)(struct net_device *),
		       phy_interface_t interface);
struct phy_device *phy_connect(struct net_device *dev, const char *bus_id,
			       void (*handler)(struct net_device *),
			       phy_interface_t interface);
void phy_disconnect(struct phy_device *phydev);
void phy_detach(struct phy_device *phydev);
void phy_start(struct phy_device *phydev);
void phy_stop(struct phy_device *phydev);
int phy_start_aneg(struct phy_device *phydev);
int phy_aneg_done(struct phy_device *phydev);
int phy_speed_down(struct phy_device *phydev, bool sync);
int phy_speed_up(struct phy_device *phydev);

int phy_restart_aneg(struct phy_device *phydev);
int phy_reset_after_clk_enable(struct phy_device *phydev);

static inline void phy_device_reset(struct phy_device *phydev, int value)
{
	mdio_device_reset(&phydev->mdio, value);
}

#define phydev_err(_phydev, format, args...)	\
	dev_err(&_phydev->mdio.dev, format, ##args)

#define phydev_info(_phydev, format, args...)	\
	dev_info(&_phydev->mdio.dev, format, ##args)

#define phydev_warn(_phydev, format, args...)	\
	dev_warn(&_phydev->mdio.dev, format, ##args)

#define phydev_dbg(_phydev, format, args...)	\
	dev_dbg(&_phydev->mdio.dev, format, ##args)

static inline const char *phydev_name(const struct phy_device *phydev)
{
	return dev_name(&phydev->mdio.dev);
}

void phy_attached_print(struct phy_device *phydev, const char *fmt, ...)
	__printf(2, 3);
void phy_attached_info(struct phy_device *phydev);

/* Clause 22 PHY */
int genphy_config_init(struct phy_device *phydev);
int genphy_setup_forced(struct phy_device *phydev);
int genphy_restart_aneg(struct phy_device *phydev);
int genphy_config_eee_advert(struct phy_device *phydev);
int genphy_config_aneg(struct phy_device *phydev);
int genphy_aneg_done(struct phy_device *phydev);
int genphy_update_link(struct phy_device *phydev);
int genphy_read_status(struct phy_device *phydev);
int genphy_suspend(struct phy_device *phydev);
int genphy_resume(struct phy_device *phydev);
int genphy_loopback(struct phy_device *phydev, bool enable);
int genphy_soft_reset(struct phy_device *phydev);
static inline int genphy_no_soft_reset(struct phy_device *phydev)
{
	return 0;
}
static inline int genphy_no_ack_interrupt(struct phy_device *phydev)
{
	return 0;
}
static inline int genphy_no_config_intr(struct phy_device *phydev)
{
	return 0;
}
int genphy_read_mmd_unsupported(struct phy_device *phdev, int devad,
				u16 regnum);
int genphy_write_mmd_unsupported(struct phy_device *phdev, int devnum,
				 u16 regnum, u16 val);

/* Clause 45 PHY */
int genphy_c45_restart_aneg(struct phy_device *phydev);
int genphy_c45_check_and_restart_aneg(struct phy_device *phydev, bool restart);
int genphy_c45_aneg_done(struct phy_device *phydev);
int genphy_c45_read_link(struct phy_device *phydev);
int genphy_c45_read_lpa(struct phy_device *phydev);
int genphy_c45_read_pma(struct phy_device *phydev);
int genphy_c45_pma_setup_forced(struct phy_device *phydev);
int genphy_c45_an_config_aneg(struct phy_device *phydev);
int genphy_c45_an_disable_aneg(struct phy_device *phydev);
int genphy_c45_read_mdix(struct phy_device *phydev);
int genphy_c45_pma_read_abilities(struct phy_device *phydev);
int genphy_c45_read_status(struct phy_device *phydev);

/* The gen10g_* functions are the old Clause 45 stub */
int gen10g_config_aneg(struct phy_device *phydev);

static inline int phy_read_status(struct phy_device *phydev)
{
	if (!phydev->drv)
		return -EIO;

	if (phydev->drv->read_status)
		return phydev->drv->read_status(phydev);
	else
		return genphy_read_status(phydev);
}

void phy_driver_unregister(struct phy_driver *drv);
void phy_drivers_unregister(struct phy_driver *drv, int n);
int phy_driver_register(struct phy_driver *new_driver, struct module *owner);
int phy_drivers_register(struct phy_driver *new_driver, int n,
			 struct module *owner);
void phy_state_machine(struct work_struct *work);
void phy_mac_interrupt(struct phy_device *phydev);
void phy_start_machine(struct phy_device *phydev);
void phy_stop_machine(struct phy_device *phydev);
int phy_ethtool_sset(struct phy_device *phydev, struct ethtool_cmd *cmd);
void phy_ethtool_ksettings_get(struct phy_device *phydev,
			       struct ethtool_link_ksettings *cmd);
int phy_ethtool_ksettings_set(struct phy_device *phydev,
			      const struct ethtool_link_ksettings *cmd);
int phy_mii_ioctl(struct phy_device *phydev, struct ifreq *ifr, int cmd);
void phy_request_interrupt(struct phy_device *phydev);
void phy_print_status(struct phy_device *phydev);
int phy_set_max_speed(struct phy_device *phydev, u32 max_speed);
void phy_remove_link_mode(struct phy_device *phydev, u32 link_mode);
void phy_support_sym_pause(struct phy_device *phydev);
void phy_support_asym_pause(struct phy_device *phydev);
void phy_set_sym_pause(struct phy_device *phydev, bool rx, bool tx,
		       bool autoneg);
void phy_set_asym_pause(struct phy_device *phydev, bool rx, bool tx);
bool phy_validate_pause(struct phy_device *phydev,
			struct ethtool_pauseparam *pp);

int phy_register_fixup(const char *bus_id, u32 phy_uid, u32 phy_uid_mask,
		       int (*run)(struct phy_device *));
int phy_register_fixup_for_id(const char *bus_id,
			      int (*run)(struct phy_device *));
int phy_register_fixup_for_uid(u32 phy_uid, u32 phy_uid_mask,
			       int (*run)(struct phy_device *));

int phy_unregister_fixup(const char *bus_id, u32 phy_uid, u32 phy_uid_mask);
int phy_unregister_fixup_for_id(const char *bus_id);
int phy_unregister_fixup_for_uid(u32 phy_uid, u32 phy_uid_mask);

int phy_init_eee(struct phy_device *phydev, bool clk_stop_enable);
int phy_get_eee_err(struct phy_device *phydev);
int phy_ethtool_set_eee(struct phy_device *phydev, struct ethtool_eee *data);
int phy_ethtool_get_eee(struct phy_device *phydev, struct ethtool_eee *data);
int phy_ethtool_set_wol(struct phy_device *phydev, struct ethtool_wolinfo *wol);
void phy_ethtool_get_wol(struct phy_device *phydev,
			 struct ethtool_wolinfo *wol);
int phy_ethtool_get_link_ksettings(struct net_device *ndev,
				   struct ethtool_link_ksettings *cmd);
int phy_ethtool_set_link_ksettings(struct net_device *ndev,
				   const struct ethtool_link_ksettings *cmd);
int phy_ethtool_nway_reset(struct net_device *ndev);

#if IS_ENABLED(CONFIG_PHYLIB)
int __init mdio_bus_init(void);
void mdio_bus_exit(void);
#endif

/* Inline function for use within net/core/ethtool.c (built-in) */
static inline int phy_ethtool_get_strings(struct phy_device *phydev, u8 *data)
{
	if (!phydev->drv)
		return -EIO;

	mutex_lock(&phydev->lock);
	phydev->drv->get_strings(phydev, data);
	mutex_unlock(&phydev->lock);

	return 0;
}

static inline int phy_ethtool_get_sset_count(struct phy_device *phydev)
{
	int ret;

	if (!phydev->drv)
		return -EIO;

	if (phydev->drv->get_sset_count &&
	    phydev->drv->get_strings &&
	    phydev->drv->get_stats) {
		mutex_lock(&phydev->lock);
		ret = phydev->drv->get_sset_count(phydev);
		mutex_unlock(&phydev->lock);

		return ret;
	}

	return -EOPNOTSUPP;
}

static inline int phy_ethtool_get_stats(struct phy_device *phydev,
					struct ethtool_stats *stats, u64 *data)
{
	if (!phydev->drv)
		return -EIO;

	mutex_lock(&phydev->lock);
	phydev->drv->get_stats(phydev, stats, data);
	mutex_unlock(&phydev->lock);

	return 0;
}

extern struct bus_type mdio_bus_type;

struct mdio_board_info {
	const char	*bus_id;
	char		modalias[MDIO_NAME_SIZE];
	int		mdio_addr;
	const void	*platform_data;
};

#if IS_ENABLED(CONFIG_MDIO_DEVICE)
int mdiobus_register_board_info(const struct mdio_board_info *info,
				unsigned int n);
#else
static inline int mdiobus_register_board_info(const struct mdio_board_info *i,
					      unsigned int n)
{
	return 0;
}
#endif


/**
 * module_phy_driver() - Helper macro for registering PHY drivers
 * @__phy_drivers: array of PHY drivers to register
 *
 * Helper macro for PHY drivers which do not do anything special in module
 * init/exit. Each module may only use this macro once, and calling it
 * replaces module_init() and module_exit().
 */
#define phy_module_driver(__phy_drivers, __count)			\
static int __init phy_module_init(void)					\
{									\
	return phy_drivers_register(__phy_drivers, __count, THIS_MODULE); \
}									\
module_init(phy_module_init);						\
static void __exit phy_module_exit(void)				\
{									\
	phy_drivers_unregister(__phy_drivers, __count);			\
}									\
module_exit(phy_module_exit)

#define module_phy_driver(__phy_drivers)				\
	phy_module_driver(__phy_drivers, ARRAY_SIZE(__phy_drivers))

bool phy_driver_is_genphy(struct phy_device *phydev);
bool phy_driver_is_genphy_10g(struct phy_device *phydev);

#endif /* __PHY_H */
