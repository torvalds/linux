/*
 * include/linux/phy.h
 *
 * Framework and drivers for configuring and reading different PHYs
 * Based on code in sungem_phy.c and gianfar_phy.c
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

#include <linux/spinlock.h>
#include <linux/device.h>
#include <linux/ethtool.h>
#include <linux/mii.h>
#include <linux/timer.h>
#include <linux/workqueue.h>
#include <linux/mod_devicetable.h>

#include <linux/atomic.h>

#define PHY_BASIC_FEATURES	(SUPPORTED_10baseT_Half | \
				 SUPPORTED_10baseT_Full | \
				 SUPPORTED_100baseT_Half | \
				 SUPPORTED_100baseT_Full | \
				 SUPPORTED_Autoneg | \
				 SUPPORTED_TP | \
				 SUPPORTED_MII)

#define PHY_GBIT_FEATURES	(PHY_BASIC_FEATURES | \
				 SUPPORTED_1000baseT_Half | \
				 SUPPORTED_1000baseT_Full)

/*
 * Set phydev->irq to PHY_POLL if interrupts are not supported,
 * or not desired for this PHY.  Set to PHY_IGNORE_INTERRUPT if
 * the attached driver handles the interrupt
 */
#define PHY_POLL		-1
#define PHY_IGNORE_INTERRUPT	-2

#define PHY_HAS_INTERRUPT	0x00000001
#define PHY_HAS_MAGICANEG	0x00000002

/* Interface Mode definitions */
typedef enum {
	PHY_INTERFACE_MODE_NA,
	PHY_INTERFACE_MODE_MII,
	PHY_INTERFACE_MODE_GMII,
	PHY_INTERFACE_MODE_SGMII,
	PHY_INTERFACE_MODE_TBI,
	PHY_INTERFACE_MODE_RMII,
	PHY_INTERFACE_MODE_RGMII,
	PHY_INTERFACE_MODE_RGMII_ID,
	PHY_INTERFACE_MODE_RGMII_RXID,
	PHY_INTERFACE_MODE_RGMII_TXID,
	PHY_INTERFACE_MODE_RTBI,
	PHY_INTERFACE_MODE_SMII,
} phy_interface_t;


#define PHY_INIT_TIMEOUT	100000
#define PHY_STATE_TIME		1
#define PHY_FORCE_TIMEOUT	10
#define PHY_AN_TIMEOUT		10

#define PHY_MAX_ADDR	32

/* Used when trying to connect to a specific phy (mii bus id:phy device id) */
#define PHY_ID_FMT "%s:%02x"

/*
 * Need to be a little smaller than phydev->dev.bus_id to leave room
 * for the ":%02x"
 */
#define MII_BUS_ID_SIZE	(20 - 3)

/* Or MII_ADDR_C45 into regnum for read/write on mii_bus to enable the 21 bit
   IEEE 802.3ae clause 45 addressing mode used by 10GIGE phy chips. */
#define MII_ADDR_C45 (1<<30)

/*
 * The Bus class for PHYs.  Devices which provide access to
 * PHYs should register using this structure
 */
struct mii_bus {
	const char *name;
	char id[MII_BUS_ID_SIZE];
	void *priv;
	int (*read)(struct mii_bus *bus, int phy_id, int regnum);
	int (*write)(struct mii_bus *bus, int phy_id, int regnum, u16 val);
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
	struct phy_device *phy_map[PHY_MAX_ADDR];

	/* PHY addresses to be ignored when probing */
	u32 phy_mask;

	/*
	 * Pointer to an array of interrupts, each PHY's
	 * interrupt at the index matching its address
	 */
	int *irq;
};
#define to_mii_bus(d) container_of(d, struct mii_bus, dev)

struct mii_bus *mdiobus_alloc(void);
int mdiobus_register(struct mii_bus *bus);
void mdiobus_unregister(struct mii_bus *bus);
void mdiobus_free(struct mii_bus *bus);
struct phy_device *mdiobus_scan(struct mii_bus *bus, int addr);
int mdiobus_read(struct mii_bus *bus, int addr, u32 regnum);
int mdiobus_write(struct mii_bus *bus, int addr, u32 regnum, u16 val);


#define PHY_INTERRUPT_DISABLED	0x0
#define PHY_INTERRUPT_ENABLED	0x80000000

/* PHY state machine states:
 *
 * DOWN: PHY device and driver are not ready for anything.  probe
 * should be called if and only if the PHY is in this state,
 * given that the PHY device exists.
 * - PHY driver probe function will, depending on the PHY, set
 * the state to STARTING or READY
 *
 * STARTING:  PHY device is coming up, and the ethernet driver is
 * not ready.  PHY drivers may set this in the probe function.
 * If they do, they are responsible for making sure the state is
 * eventually set to indicate whether the PHY is UP or READY,
 * depending on the state when the PHY is done starting up.
 * - PHY driver will set the state to READY
 * - start will set the state to PENDING
 *
 * READY: PHY is ready to send and receive packets, but the
 * controller is not.  By default, PHYs which do not implement
 * probe will be set to this state by phy_probe().  If the PHY
 * driver knows the PHY is ready, and the PHY state is STARTING,
 * then it sets this STATE.
 * - start will set the state to UP
 *
 * PENDING: PHY device is coming up, but the ethernet driver is
 * ready.  phy_start will set this state if the PHY state is
 * STARTING.
 * - PHY driver will set the state to UP when the PHY is ready
 *
 * UP: The PHY and attached device are ready to do work.
 * Interrupts should be started here.
 * - timer moves to AN
 *
 * AN: The PHY is currently negotiating the link state.  Link is
 * therefore down for now.  phy_timer will set this state when it
 * detects the state is UP.  config_aneg will set this state
 * whenever called with phydev->autoneg set to AUTONEG_ENABLE.
 * - If autonegotiation finishes, but there's no link, it sets
 *   the state to NOLINK.
 * - If aneg finishes with link, it sets the state to RUNNING,
 *   and calls adjust_link
 * - If autonegotiation did not finish after an arbitrary amount
 *   of time, autonegotiation should be tried again if the PHY
 *   supports "magic" autonegotiation (back to AN)
 * - If it didn't finish, and no magic_aneg, move to FORCING.
 *
 * NOLINK: PHY is up, but not currently plugged in.
 * - If the timer notes that the link comes back, we move to RUNNING
 * - config_aneg moves to AN
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
 * - timer will set CHANGELINK if we're polling (this ensures the
 *   link state is polled every other cycle of this state machine,
 *   which makes it every other second)
 * - irq will set CHANGELINK
 * - config_aneg will set AN
 * - phy_stop moves to HALTED
 *
 * CHANGELINK: PHY experienced a change in link state
 * - timer moves to RUNNING if link
 * - timer moves to NOLINK if the link is down
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
	PHY_DOWN=0,
	PHY_STARTING,
	PHY_READY,
	PHY_PENDING,
	PHY_UP,
	PHY_AN,
	PHY_RUNNING,
	PHY_NOLINK,
	PHY_FORCING,
	PHY_CHANGELINK,
	PHY_HALTED,
	PHY_RESUMING
};

struct sk_buff;

/* phy_device: An instance of a PHY
 *
 * drv: Pointer to the driver for this PHY instance
 * bus: Pointer to the bus this PHY is on
 * dev: driver model device structure for this PHY
 * phy_id: UID for this device found during discovery
 * state: state of the PHY for management purposes
 * dev_flags: Device-specific flags used by the PHY driver.
 * addr: Bus address of PHY
 * link_timeout: The number of timer firings to wait before the
 * giving up on the current attempt at acquiring a link
 * irq: IRQ number of the PHY's interrupt (-1 if none)
 * phy_timer: The timer for handling the state machine
 * phy_queue: A work_queue for the interrupt
 * attached_dev: The attached enet driver's device instance ptr
 * adjust_link: Callback for the enet controller to respond to
 * changes in the link state.
 * adjust_state: Callback for the enet driver to respond to
 * changes in the state machine.
 *
 * speed, duplex, pause, supported, advertising, and
 * autoneg are used like in mii_if_info
 *
 * interrupts currently only supports enabled or disabled,
 * but could be changed in the future to support enabling
 * and disabling specific interrupts
 *
 * Contains some infrastructure for polling and interrupt
 * handling, as well as handling shifts in PHY hardware state
 */
struct phy_device {
	/* Information about the PHY type */
	/* And management functions */
	struct phy_driver *drv;

	struct mii_bus *bus;

	struct device dev;

	u32 phy_id;

	enum phy_state state;

	u32 dev_flags;

	phy_interface_t interface;

	/* Bus address of the PHY (0-31) */
	int addr;

	/*
	 * forced speed & duplex (no autoneg)
	 * partner speed & duplex & pause (autoneg)
	 */
	int speed;
	int duplex;
	int pause;
	int asym_pause;

	/* The most recently read link state */
	int link;

	/* Enabled Interrupts */
	u32 interrupts;

	/* Union of PHY and Attached devices' supported modes */
	/* See mii.h for more info */
	u32 supported;
	u32 advertising;

	int autoneg;

	int link_timeout;

	/*
	 * Interrupt number for this PHY
	 * -1 means no interrupt
	 */
	int irq;

	/* private data pointer */
	/* For use by PHYs to maintain extra state */
	void *priv;

	/* Interrupt and Polling infrastructure */
	struct work_struct phy_queue;
	struct delayed_work state_queue;
	atomic_t irq_disable;

	struct mutex lock;

	struct net_device *attached_dev;

	void (*adjust_link)(struct net_device *dev);

	void (*adjust_state)(struct net_device *dev);
};
#define to_phy_device(d) container_of(d, struct phy_device, dev)

/* struct phy_driver: Driver structure for a particular PHY type
 *
 * phy_id: The result of reading the UID registers of this PHY
 *   type, and ANDing them with the phy_id_mask.  This driver
 *   only works for PHYs with IDs which match this field
 * name: The friendly name of this PHY type
 * phy_id_mask: Defines the important bits of the phy_id
 * features: A list of features (speed, duplex, etc) supported
 *   by this PHY
 * flags: A bitfield defining certain other features this PHY
 *   supports (like interrupts)
 *
 * The drivers must implement config_aneg and read_status.  All
 * other functions are optional. Note that none of these
 * functions should be called from interrupt time.  The goal is
 * for the bus read/write functions to be able to block when the
 * bus transaction is happening, and be freed up by an interrupt
 * (The MPC85xx has this ability, though it is not currently
 * supported in the driver).
 */
struct phy_driver {
	u32 phy_id;
	char *name;
	unsigned int phy_id_mask;
	u32 features;
	u32 flags;

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

	struct device_driver driver;
};
#define to_phy_driver(d) container_of(d, struct phy_driver, driver)

#define PHY_ANY_ID "MATCH ANY PHY"
#define PHY_ANY_UID 0xffffffff

/* A Structure for boards to register fixups with the PHY Lib */
struct phy_fixup {
	struct list_head list;
	char bus_id[20];
	u32 phy_uid;
	u32 phy_uid_mask;
	int (*run)(struct phy_device *phydev);
};

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
	return mdiobus_read(phydev->bus, phydev->addr, regnum);
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
	return mdiobus_write(phydev->bus, phydev->addr, regnum, val);
}

int get_phy_id(struct mii_bus *bus, int addr, u32 *phy_id);
struct phy_device* get_phy_device(struct mii_bus *bus, int addr);
int phy_device_register(struct phy_device *phy);
int phy_init_hw(struct phy_device *phydev);
struct phy_device * phy_attach(struct net_device *dev,
		const char *bus_id, u32 flags, phy_interface_t interface);
struct phy_device *phy_find_first(struct mii_bus *bus);
int phy_connect_direct(struct net_device *dev, struct phy_device *phydev,
		void (*handler)(struct net_device *), u32 flags,
		phy_interface_t interface);
struct phy_device * phy_connect(struct net_device *dev, const char *bus_id,
		void (*handler)(struct net_device *), u32 flags,
		phy_interface_t interface);
void phy_disconnect(struct phy_device *phydev);
void phy_detach(struct phy_device *phydev);
void phy_start(struct phy_device *phydev);
void phy_stop(struct phy_device *phydev);
int phy_start_aneg(struct phy_device *phydev);

int phy_stop_interrupts(struct phy_device *phydev);

static inline int phy_read_status(struct phy_device *phydev) {
	return phydev->drv->read_status(phydev);
}

int genphy_restart_aneg(struct phy_device *phydev);
int genphy_config_aneg(struct phy_device *phydev);
int genphy_update_link(struct phy_device *phydev);
int genphy_read_status(struct phy_device *phydev);
int genphy_suspend(struct phy_device *phydev);
int genphy_resume(struct phy_device *phydev);
void phy_driver_unregister(struct phy_driver *drv);
int phy_driver_register(struct phy_driver *new_driver);
void phy_state_machine(struct work_struct *work);
void phy_start_machine(struct phy_device *phydev,
		void (*handler)(struct net_device *));
void phy_stop_machine(struct phy_device *phydev);
int phy_ethtool_sset(struct phy_device *phydev, struct ethtool_cmd *cmd);
int phy_ethtool_gset(struct phy_device *phydev, struct ethtool_cmd *cmd);
int phy_mii_ioctl(struct phy_device *phydev,
		struct ifreq *ifr, int cmd);
int phy_start_interrupts(struct phy_device *phydev);
void phy_print_status(struct phy_device *phydev);
void phy_device_free(struct phy_device *phydev);

int phy_register_fixup(const char *bus_id, u32 phy_uid, u32 phy_uid_mask,
		int (*run)(struct phy_device *));
int phy_register_fixup_for_id(const char *bus_id,
		int (*run)(struct phy_device *));
int phy_register_fixup_for_uid(u32 phy_uid, u32 phy_uid_mask,
		int (*run)(struct phy_device *));
int phy_scan_fixups(struct phy_device *phydev);

int __init mdio_bus_init(void);
void mdio_bus_exit(void);

extern struct bus_type mdio_bus_type;
#endif /* __PHY_H */
