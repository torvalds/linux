/* SPDX-License-Identifier: GPL-2.0+ */
/*
 * Core interface for Renesas Synchronization Management Unit (SMU) devices.
 *
 * Copyright (C) 2021 Integrated Device Technology, Inc., a Renesas Company.
 */

#ifndef __LINUX_MFD_RSMU_H
#define __LINUX_MFD_RSMU_H

/* The supported devices are ClockMatrix, Sabre and SnowLotus */
enum rsmu_type {
	RSMU_CM		= 0x34000,
	RSMU_SABRE	= 0x33810,
	RSMU_SL		= 0x19850,
};

/**
 *
 * struct rsmu_ddata - device data structure for sub devices.
 *
 * @dev:    i2c/spi device.
 * @regmap: i2c/spi bus access.
 * @lock:   mutex used by sub devices to make sure a series of
 *          bus access requests are not interrupted.
 * @type:   RSMU device type.
 * @page:   i2c/spi bus driver internal use only.
 */
struct rsmu_ddata {
	struct device *dev;
	struct regmap *regmap;
	struct mutex lock;
	enum rsmu_type type;
	u16 page;
};
#endif /*  __LINUX_MFD_RSMU_H */
