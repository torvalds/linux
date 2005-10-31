/*
 * include/linux/fsl_devices.h
 *
 * Definitions for any platform device related flags or structures for
 * Freescale processor devices
 *
 * Maintainer: Kumar Gala (kumar.gala@freescale.com)
 *
 * Copyright 2004 Freescale Semiconductor, Inc
 *
 * This program is free software; you can redistribute  it and/or modify it
 * under  the terms of  the GNU General  Public License as published by the
 * Free Software Foundation;  either version 2 of the  License, or (at your
 * option) any later version.
 */

#ifdef __KERNEL__
#ifndef _FSL_DEVICE_H_
#define _FSL_DEVICE_H_

#include <linux/types.h>

/*
 * Some conventions on how we handle peripherals on Freescale chips
 *
 * unique device: a platform_device entry in fsl_plat_devs[] plus
 * associated device information in its platform_data structure.
 *
 * A chip is described by a set of unique devices.
 *
 * Each sub-arch has its own master list of unique devices and
 * enumerates them by enum fsl_devices in a sub-arch specific header
 *
 * The platform data structure is broken into two parts.  The
 * first is device specific information that help identify any
 * unique features of a peripheral.  The second is any
 * information that may be defined by the board or how the device
 * is connected externally of the chip.
 *
 * naming conventions:
 * - platform data structures: <driver>_platform_data
 * - platform data device flags: FSL_<driver>_DEV_<FLAG>
 * - platform data board flags: FSL_<driver>_BRD_<FLAG>
 *
 */

struct gianfar_platform_data {
	/* device specific information */
	u32 device_flags;

	/* board specific information */
	u32 board_flags;
	const char *bus_id;
	u8 mac_addr[6];
};

struct gianfar_mdio_data {
	/* device specific information */
	u32 paddr;

	/* board specific information */
	int irq[32];
};

/* Flags related to gianfar device features */
#define FSL_GIANFAR_DEV_HAS_GIGABIT		0x00000001
#define FSL_GIANFAR_DEV_HAS_COALESCE		0x00000002
#define FSL_GIANFAR_DEV_HAS_RMON		0x00000004
#define FSL_GIANFAR_DEV_HAS_MULTI_INTR		0x00000008
#define FSL_GIANFAR_DEV_HAS_CSUM		0x00000010
#define FSL_GIANFAR_DEV_HAS_VLAN		0x00000020
#define FSL_GIANFAR_DEV_HAS_EXTENDED_HASH	0x00000040
#define FSL_GIANFAR_DEV_HAS_PADDING		0x00000080

/* Flags in gianfar_platform_data */
#define FSL_GIANFAR_BRD_HAS_PHY_INTR	0x00000001 /* set or use a timer */
#define FSL_GIANFAR_BRD_IS_REDUCED	0x00000002 /* Set if RGMII, RMII */

struct fsl_i2c_platform_data {
	/* device specific information */
	u32 device_flags;
};

/* Flags related to I2C device features */
#define FSL_I2C_DEV_SEPARATE_DFSRR	0x00000001
#define FSL_I2C_DEV_CLOCK_5200		0x00000002

#endif				/* _FSL_DEVICE_H_ */
#endif				/* __KERNEL__ */
