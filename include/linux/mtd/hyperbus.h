/* SPDX-License-Identifier: GPL-2.0
 *
 * Copyright (C) 2019 Texas Instruments Incorporated - https://www.ti.com/
 */

#ifndef __LINUX_MTD_HYPERBUS_H__
#define __LINUX_MTD_HYPERBUS_H__

#include <linux/mtd/map.h>

/* HyperBus command bits */
#define HYPERBUS_RW	0x80	/* R/W# */
#define HYPERBUS_RW_WRITE 0
#define HYPERBUS_RW_READ 0x80
#define HYPERBUS_AS	0x40	/* Address Space */
#define HYPERBUS_AS_MEM	0
#define HYPERBUS_AS_REG	0x40
#define HYPERBUS_BT	0x20	/* Burst Type */
#define HYPERBUS_BT_WRAPPED 0
#define HYPERBUS_BT_LINEAR 0x20

enum hyperbus_memtype {
	HYPERFLASH,
	HYPERRAM,
};

/**
 * struct hyperbus_device - struct representing HyperBus slave device
 * @map: map_info struct for accessing MMIO HyperBus flash memory
 * @np: pointer to HyperBus slave device node
 * @mtd: pointer to MTD struct
 * @ctlr: pointer to HyperBus controller struct
 * @memtype: type of memory device: HyperFlash or HyperRAM
 * @priv: pointer to controller specific per device private data
 */

struct hyperbus_device {
	struct map_info map;
	struct device_node *np;
	struct mtd_info *mtd;
	struct hyperbus_ctlr *ctlr;
	enum hyperbus_memtype memtype;
	void *priv;
};

/**
 * struct hyperbus_ops - struct representing custom HyperBus operations
 * @read16: read 16 bit of data from flash in a single burst. Used to read
 *          from non default address space, such as ID/CFI space
 * @write16: write 16 bit of data to flash in a single burst. Used to
 *           send cmd to flash or write single 16 bit word at a time.
 * @copy_from: copy data from flash memory
 * @copy_to: copy data to flash memory
 * @calibrate: calibrate HyperBus controller
 */

struct hyperbus_ops {
	u16 (*read16)(struct hyperbus_device *hbdev, unsigned long addr);
	void (*write16)(struct hyperbus_device *hbdev,
			unsigned long addr, u16 val);
	void (*copy_from)(struct hyperbus_device *hbdev, void *to,
			  unsigned long from, ssize_t len);
	void (*copy_to)(struct hyperbus_device *dev, unsigned long to,
			const void *from, ssize_t len);
	int (*calibrate)(struct hyperbus_device *dev);
};

/**
 * struct hyperbus_ctlr - struct representing HyperBus controller
 * @dev: pointer to HyperBus controller device
 * @calibrated: flag to indicate ctlr calibration sequence is complete
 * @ops: HyperBus controller ops
 */
struct hyperbus_ctlr {
	struct device *dev;
	bool calibrated;

	const struct hyperbus_ops *ops;
};

/**
 * hyperbus_register_device - probe and register a HyperBus slave memory device
 * @hbdev: hyperbus_device struct with dev, np and ctlr field populated
 *
 * Return: 0 for success, others for failure.
 */
int hyperbus_register_device(struct hyperbus_device *hbdev);

/**
 * hyperbus_unregister_device - deregister HyperBus slave memory device
 * @hbdev: hyperbus_device to be unregistered
 *
 * Return: 0 for success, others for failure.
 */
int hyperbus_unregister_device(struct hyperbus_device *hbdev);

#endif /* __LINUX_MTD_HYPERBUS_H__ */
