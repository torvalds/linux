/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Copyright (C) 2015-2019 Intel Corp. All rights reserved
 * Copyright (C) 2021-2022 Linaro Ltd
 */
#ifndef __RPMB_H__
#define __RPMB_H__

#include <linux/device.h>
#include <linux/types.h>

/**
 * enum rpmb_type - type of underlying storage technology
 *
 * @RPMB_TYPE_EMMC  : emmc (JESD84-B50.1)
 * @RPMB_TYPE_UFS   : UFS (JESD220)
 * @RPMB_TYPE_NVME  : NVM Express
 */
enum rpmb_type {
	RPMB_TYPE_EMMC,
	RPMB_TYPE_UFS,
	RPMB_TYPE_NVME,
};

/**
 * struct rpmb_descr - RPMB description provided by the underlying block device
 *
 * @type             : block device type
 * @route_frames     : routes frames to and from the RPMB device
 * @dev_id           : unique device identifier read from the hardware
 * @dev_id_len       : length of unique device identifier
 * @reliable_wr_count: number of sectors that can be written in one access
 * @capacity         : capacity of the device in units of 128K
 *
 * @dev_id is intended to be used as input when deriving the authenticaion key.
 */
struct rpmb_descr {
	enum rpmb_type type;
	int (*route_frames)(struct device *dev, u8 *req, unsigned int req_len,
			    u8 *resp, unsigned int resp_len);
	u8 *dev_id;
	size_t dev_id_len;
	u16 reliable_wr_count;
	u16 capacity;
};

/**
 * struct rpmb_dev - device which can support RPMB partition
 *
 * @dev              : device
 * @id               : device_id
 * @list_node        : linked list node
 * @descr            : RPMB description
 */
struct rpmb_dev {
	struct device dev;
	int id;
	struct list_head list_node;
	struct rpmb_descr descr;
};

#define to_rpmb_dev(x)		container_of((x), struct rpmb_dev, dev)

#if IS_ENABLED(CONFIG_RPMB)
struct rpmb_dev *rpmb_dev_get(struct rpmb_dev *rdev);
void rpmb_dev_put(struct rpmb_dev *rdev);
struct rpmb_dev *rpmb_dev_find_device(const void *data,
				      const struct rpmb_dev *start,
				      int (*match)(struct device *dev,
						   const void *data));
int rpmb_interface_register(struct class_interface *intf);
void rpmb_interface_unregister(struct class_interface *intf);
struct rpmb_dev *rpmb_dev_register(struct device *dev,
				   struct rpmb_descr *descr);
int rpmb_dev_unregister(struct rpmb_dev *rdev);

int rpmb_route_frames(struct rpmb_dev *rdev, u8 *req,
		      unsigned int req_len, u8 *resp, unsigned int resp_len);

#else
static inline struct rpmb_dev *rpmb_dev_get(struct rpmb_dev *rdev)
{
	return NULL;
}

static inline void rpmb_dev_put(struct rpmb_dev *rdev) { }

static inline struct rpmb_dev *
rpmb_dev_find_device(const void *data, const struct rpmb_dev *start,
		     int (*match)(struct device *dev, const void *data))
{
	return NULL;
}

static inline int rpmb_interface_register(struct class_interface *intf)
{
	return -EOPNOTSUPP;
}

static inline void rpmb_interface_unregister(struct class_interface *intf)
{
}

static inline struct rpmb_dev *
rpmb_dev_register(struct device *dev, struct rpmb_descr *descr)
{
	return NULL;
}

static inline int rpmb_dev_unregister(struct rpmb_dev *dev)
{
	return 0;
}

static inline int rpmb_route_frames(struct rpmb_dev *rdev, u8 *req,
				    unsigned int req_len, u8 *resp,
				    unsigned int resp_len)
{
	return -EOPNOTSUPP;
}
#endif /* CONFIG_RPMB */

#endif /* __RPMB_H__ */
