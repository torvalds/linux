/* SPDX-License-Identifier: BSD-3-Clause */
/*
 * Remote processor messaging
 *
 * Copyright (C) 2011 Texas Instruments, Inc.
 * Copyright (C) 2011 Google, Inc.
 * All rights reserved.
 */

#ifndef _LINUX_RPMSG_H
#define _LINUX_RPMSG_H

#include <linux/types.h>
#include <linux/device.h>
#include <linux/err.h>
#include <linux/mod_devicetable.h>
#include <linux/kref.h>
#include <linux/mutex.h>
#include <linux/poll.h>
#include <linux/rpmsg/byteorder.h>
#include <uapi/linux/rpmsg.h>

struct rpmsg_device;
struct rpmsg_endpoint;
struct rpmsg_device_ops;
struct rpmsg_endpoint_ops;

/**
 * struct rpmsg_channel_info - channel info representation
 * @name: name of service
 * @src: local address
 * @dst: destination address
 */
struct rpmsg_channel_info {
	char name[RPMSG_NAME_SIZE];
	u32 src;
	u32 dst;
};

/**
 * rpmsg_device - device that belong to the rpmsg bus
 * @dev: the device struct
 * @id: device id (used to match between rpmsg drivers and devices)
 * @driver_override: driver name to force a match
 * @src: local address
 * @dst: destination address
 * @ept: the rpmsg endpoint of this channel
 * @announce: if set, rpmsg will announce the creation/removal of this channel
 * @little_endian: True if transport is using little endian byte representation
 */
struct rpmsg_device {
	struct device dev;
	struct rpmsg_device_id id;
	char *driver_override;
	u32 src;
	u32 dst;
	struct rpmsg_endpoint *ept;
	bool announce;
	bool little_endian;

	const struct rpmsg_device_ops *ops;
};

typedef int (*rpmsg_rx_cb_t)(struct rpmsg_device *, void *, int, void *, u32);

/**
 * struct rpmsg_endpoint - binds a local rpmsg address to its user
 * @rpdev: rpmsg channel device
 * @refcount: when this drops to zero, the ept is deallocated
 * @cb: rx callback handler
 * @cb_lock: must be taken before accessing/changing @cb
 * @addr: local rpmsg address
 * @priv: private data for the driver's use
 *
 * In essence, an rpmsg endpoint represents a listener on the rpmsg bus, as
 * it binds an rpmsg address with an rx callback handler.
 *
 * Simple rpmsg drivers shouldn't use this struct directly, because
 * things just work: every rpmsg driver provides an rx callback upon
 * registering to the bus, and that callback is then bound to its rpmsg
 * address when the driver is probed. When relevant inbound messages arrive
 * (i.e. messages which their dst address equals to the src address of
 * the rpmsg channel), the driver's handler is invoked to process it.
 *
 * More complicated drivers though, that do need to allocate additional rpmsg
 * addresses, and bind them to different rx callbacks, must explicitly
 * create additional endpoints by themselves (see rpmsg_create_ept()).
 */
struct rpmsg_endpoint {
	struct rpmsg_device *rpdev;
	struct kref refcount;
	rpmsg_rx_cb_t cb;
	struct mutex cb_lock;
	u32 addr;
	void *priv;

	const struct rpmsg_endpoint_ops *ops;
};

/**
 * struct rpmsg_driver - rpmsg driver struct
 * @drv: underlying device driver
 * @id_table: rpmsg ids serviced by this driver
 * @probe: invoked when a matching rpmsg channel (i.e. device) is found
 * @remove: invoked when the rpmsg channel is removed
 * @callback: invoked when an inbound message is received on the channel
 */
struct rpmsg_driver {
	struct device_driver drv;
	const struct rpmsg_device_id *id_table;
	int (*probe)(struct rpmsg_device *dev);
	void (*remove)(struct rpmsg_device *dev);
	int (*callback)(struct rpmsg_device *, void *, int, void *, u32);
};

static inline u16 rpmsg16_to_cpu(struct rpmsg_device *rpdev, __rpmsg16 val)
{
	if (!rpdev)
		return __rpmsg16_to_cpu(rpmsg_is_little_endian(), val);
	else
		return __rpmsg16_to_cpu(rpdev->little_endian, val);
}

static inline __rpmsg16 cpu_to_rpmsg16(struct rpmsg_device *rpdev, u16 val)
{
	if (!rpdev)
		return __cpu_to_rpmsg16(rpmsg_is_little_endian(), val);
	else
		return __cpu_to_rpmsg16(rpdev->little_endian, val);
}

static inline u32 rpmsg32_to_cpu(struct rpmsg_device *rpdev, __rpmsg32 val)
{
	if (!rpdev)
		return __rpmsg32_to_cpu(rpmsg_is_little_endian(), val);
	else
		return __rpmsg32_to_cpu(rpdev->little_endian, val);
}

static inline __rpmsg32 cpu_to_rpmsg32(struct rpmsg_device *rpdev, u32 val)
{
	if (!rpdev)
		return __cpu_to_rpmsg32(rpmsg_is_little_endian(), val);
	else
		return __cpu_to_rpmsg32(rpdev->little_endian, val);
}

static inline u64 rpmsg64_to_cpu(struct rpmsg_device *rpdev, __rpmsg64 val)
{
	if (!rpdev)
		return __rpmsg64_to_cpu(rpmsg_is_little_endian(), val);
	else
		return __rpmsg64_to_cpu(rpdev->little_endian, val);
}

static inline __rpmsg64 cpu_to_rpmsg64(struct rpmsg_device *rpdev, u64 val)
{
	if (!rpdev)
		return __cpu_to_rpmsg64(rpmsg_is_little_endian(), val);
	else
		return __cpu_to_rpmsg64(rpdev->little_endian, val);
}

#if IS_ENABLED(CONFIG_RPMSG)

int rpmsg_register_device(struct rpmsg_device *rpdev);
int rpmsg_unregister_device(struct device *parent,
			    struct rpmsg_channel_info *chinfo);
int __register_rpmsg_driver(struct rpmsg_driver *drv, struct module *owner);
void unregister_rpmsg_driver(struct rpmsg_driver *drv);
void rpmsg_destroy_ept(struct rpmsg_endpoint *);
struct rpmsg_endpoint *rpmsg_create_ept(struct rpmsg_device *,
					rpmsg_rx_cb_t cb, void *priv,
					struct rpmsg_channel_info chinfo);

int rpmsg_send(struct rpmsg_endpoint *ept, void *data, int len);
int rpmsg_sendto(struct rpmsg_endpoint *ept, void *data, int len, u32 dst);
int rpmsg_send_offchannel(struct rpmsg_endpoint *ept, u32 src, u32 dst,
			  void *data, int len);

int rpmsg_trysend(struct rpmsg_endpoint *ept, void *data, int len);
int rpmsg_trysendto(struct rpmsg_endpoint *ept, void *data, int len, u32 dst);
int rpmsg_trysend_offchannel(struct rpmsg_endpoint *ept, u32 src, u32 dst,
			     void *data, int len);

__poll_t rpmsg_poll(struct rpmsg_endpoint *ept, struct file *filp,
			poll_table *wait);

ssize_t rpmsg_get_mtu(struct rpmsg_endpoint *ept);

#else

static inline int rpmsg_register_device(struct rpmsg_device *rpdev)
{
	return -ENXIO;
}

static inline int rpmsg_unregister_device(struct device *parent,
					  struct rpmsg_channel_info *chinfo)
{
	/* This shouldn't be possible */
	WARN_ON(1);

	return -ENXIO;
}

static inline int __register_rpmsg_driver(struct rpmsg_driver *drv,
					  struct module *owner)
{
	/* This shouldn't be possible */
	WARN_ON(1);

	return -ENXIO;
}

static inline void unregister_rpmsg_driver(struct rpmsg_driver *drv)
{
	/* This shouldn't be possible */
	WARN_ON(1);
}

static inline void rpmsg_destroy_ept(struct rpmsg_endpoint *ept)
{
	/* This shouldn't be possible */
	WARN_ON(1);
}

static inline struct rpmsg_endpoint *rpmsg_create_ept(struct rpmsg_device *rpdev,
						      rpmsg_rx_cb_t cb,
						      void *priv,
						      struct rpmsg_channel_info chinfo)
{
	/* This shouldn't be possible */
	WARN_ON(1);

	return ERR_PTR(-ENXIO);
}

static inline int rpmsg_send(struct rpmsg_endpoint *ept, void *data, int len)
{
	/* This shouldn't be possible */
	WARN_ON(1);

	return -ENXIO;
}

static inline int rpmsg_sendto(struct rpmsg_endpoint *ept, void *data, int len,
			       u32 dst)
{
	/* This shouldn't be possible */
	WARN_ON(1);

	return -ENXIO;

}

static inline int rpmsg_send_offchannel(struct rpmsg_endpoint *ept, u32 src,
					u32 dst, void *data, int len)
{
	/* This shouldn't be possible */
	WARN_ON(1);

	return -ENXIO;
}

static inline int rpmsg_trysend(struct rpmsg_endpoint *ept, void *data, int len)
{
	/* This shouldn't be possible */
	WARN_ON(1);

	return -ENXIO;
}

static inline int rpmsg_trysendto(struct rpmsg_endpoint *ept, void *data,
				  int len, u32 dst)
{
	/* This shouldn't be possible */
	WARN_ON(1);

	return -ENXIO;
}

static inline int rpmsg_trysend_offchannel(struct rpmsg_endpoint *ept, u32 src,
					   u32 dst, void *data, int len)
{
	/* This shouldn't be possible */
	WARN_ON(1);

	return -ENXIO;
}

static inline __poll_t rpmsg_poll(struct rpmsg_endpoint *ept,
				      struct file *filp, poll_table *wait)
{
	/* This shouldn't be possible */
	WARN_ON(1);

	return 0;
}

static inline ssize_t rpmsg_get_mtu(struct rpmsg_endpoint *ept)
{
	/* This shouldn't be possible */
	WARN_ON(1);

	return -ENXIO;
}

#endif /* IS_ENABLED(CONFIG_RPMSG) */

/* use a macro to avoid include chaining to get THIS_MODULE */
#define register_rpmsg_driver(drv) \
	__register_rpmsg_driver(drv, THIS_MODULE)

/**
 * module_rpmsg_driver() - Helper macro for registering an rpmsg driver
 * @__rpmsg_driver: rpmsg_driver struct
 *
 * Helper macro for rpmsg drivers which do not do anything special in module
 * init/exit. This eliminates a lot of boilerplate.  Each module may only
 * use this macro once, and calling it replaces module_init() and module_exit()
 */
#define module_rpmsg_driver(__rpmsg_driver) \
	module_driver(__rpmsg_driver, register_rpmsg_driver, \
			unregister_rpmsg_driver)

#endif /* _LINUX_RPMSG_H */
