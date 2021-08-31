/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _LINUX_VDPA_H
#define _LINUX_VDPA_H

#include <linux/kernel.h>
#include <linux/device.h>
#include <linux/interrupt.h>
#include <linux/vhost_iotlb.h>

/**
 * struct vdpa_calllback - vDPA callback definition.
 * @callback: interrupt callback function
 * @private: the data passed to the callback function
 */
struct vdpa_callback {
	irqreturn_t (*callback)(void *data);
	void *private;
};

/**
 * struct vdpa_notification_area - vDPA notification area
 * @addr: base address of the notification area
 * @size: size of the notification area
 */
struct vdpa_notification_area {
	resource_size_t addr;
	resource_size_t size;
};

/**
 * struct vdpa_vq_state_split - vDPA split virtqueue state
 * @avail_index: available index
 */
struct vdpa_vq_state_split {
	u16	avail_index;
};

/**
 * struct vdpa_vq_state_packed - vDPA packed virtqueue state
 * @last_avail_counter: last driver ring wrap counter observed by device
 * @last_avail_idx: device available index
 * @last_used_counter: device ring wrap counter
 * @last_used_idx: used index
 */
struct vdpa_vq_state_packed {
	u16	last_avail_counter:1;
	u16	last_avail_idx:15;
	u16	last_used_counter:1;
	u16	last_used_idx:15;
};

struct vdpa_vq_state {
	union {
		struct vdpa_vq_state_split split;
		struct vdpa_vq_state_packed packed;
	};
};

struct vdpa_mgmt_dev;

/**
 * struct vdpa_device - representation of a vDPA device
 * @dev: underlying device
 * @dma_dev: the actual device that is performing DMA
 * @config: the configuration ops for this device.
 * @index: device index
 * @features_valid: were features initialized? for legacy guests
 * @nvqs: maximum number of supported virtqueues
 * @mdev: management device pointer; caller must setup when registering device as part
 *	  of dev_add() mgmtdev ops callback before invoking _vdpa_register_device().
 */
struct vdpa_device {
	struct device dev;
	struct device *dma_dev;
	const struct vdpa_config_ops *config;
	unsigned int index;
	bool features_valid;
	int nvqs;
	struct vdpa_mgmt_dev *mdev;
};

/**
 * struct vdpa_iova_range - the IOVA range support by the device
 * @first: start of the IOVA range
 * @last: end of the IOVA range
 */
struct vdpa_iova_range {
	u64 first;
	u64 last;
};

/**
 * struct vdpa_config_ops - operations for configuring a vDPA device.
 * Note: vDPA device drivers are required to implement all of the
 * operations unless it is mentioned to be optional in the following
 * list.
 *
 * @set_vq_address:		Set the address of virtqueue
 *				@vdev: vdpa device
 *				@idx: virtqueue index
 *				@desc_area: address of desc area
 *				@driver_area: address of driver area
 *				@device_area: address of device area
 *				Returns integer: success (0) or error (< 0)
 * @set_vq_num:			Set the size of virtqueue
 *				@vdev: vdpa device
 *				@idx: virtqueue index
 *				@num: the size of virtqueue
 * @kick_vq:			Kick the virtqueue
 *				@vdev: vdpa device
 *				@idx: virtqueue index
 * @set_vq_cb:			Set the interrupt callback function for
 *				a virtqueue
 *				@vdev: vdpa device
 *				@idx: virtqueue index
 *				@cb: virtio-vdev interrupt callback structure
 * @set_vq_ready:		Set ready status for a virtqueue
 *				@vdev: vdpa device
 *				@idx: virtqueue index
 *				@ready: ready (true) not ready(false)
 * @get_vq_ready:		Get ready status for a virtqueue
 *				@vdev: vdpa device
 *				@idx: virtqueue index
 *				Returns boolean: ready (true) or not (false)
 * @set_vq_state:		Set the state for a virtqueue
 *				@vdev: vdpa device
 *				@idx: virtqueue index
 *				@state: pointer to set virtqueue state (last_avail_idx)
 *				Returns integer: success (0) or error (< 0)
 * @get_vq_state:		Get the state for a virtqueue
 *				@vdev: vdpa device
 *				@idx: virtqueue index
 *				@state: pointer to returned state (last_avail_idx)
 * @get_vq_notification:	Get the notification area for a virtqueue
 *				@vdev: vdpa device
 *				@idx: virtqueue index
 *				Returns the notifcation area
 * @get_vq_irq:			Get the irq number of a virtqueue (optional,
 *				but must implemented if require vq irq offloading)
 *				@vdev: vdpa device
 *				@idx: virtqueue index
 *				Returns int: irq number of a virtqueue,
 *				negative number if no irq assigned.
 * @get_vq_align:		Get the virtqueue align requirement
 *				for the device
 *				@vdev: vdpa device
 *				Returns virtqueue algin requirement
 * @get_features:		Get virtio features supported by the device
 *				@vdev: vdpa device
 *				Returns the virtio features support by the
 *				device
 * @set_features:		Set virtio features supported by the driver
 *				@vdev: vdpa device
 *				@features: feature support by the driver
 *				Returns integer: success (0) or error (< 0)
 * @set_config_cb:		Set the config interrupt callback
 *				@vdev: vdpa device
 *				@cb: virtio-vdev interrupt callback structure
 * @get_vq_num_max:		Get the max size of virtqueue
 *				@vdev: vdpa device
 *				Returns u16: max size of virtqueue
 * @get_device_id:		Get virtio device id
 *				@vdev: vdpa device
 *				Returns u32: virtio device id
 * @get_vendor_id:		Get id for the vendor that provides this device
 *				@vdev: vdpa device
 *				Returns u32: virtio vendor id
 * @get_status:			Get the device status
 *				@vdev: vdpa device
 *				Returns u8: virtio device status
 * @set_status:			Set the device status
 *				@vdev: vdpa device
 *				@status: virtio device status
 * @reset:			Reset device
 *				@vdev: vdpa device
 *				Returns integer: success (0) or error (< 0)
 * @get_config_size:		Get the size of the configuration space
 *				@vdev: vdpa device
 *				Returns size_t: configuration size
 * @get_config:			Read from device specific configuration space
 *				@vdev: vdpa device
 *				@offset: offset from the beginning of
 *				configuration space
 *				@buf: buffer used to read to
 *				@len: the length to read from
 *				configuration space
 * @set_config:			Write to device specific configuration space
 *				@vdev: vdpa device
 *				@offset: offset from the beginning of
 *				configuration space
 *				@buf: buffer used to write from
 *				@len: the length to write to
 *				configuration space
 * @get_generation:		Get device config generation (optional)
 *				@vdev: vdpa device
 *				Returns u32: device generation
 * @get_iova_range:		Get supported iova range (optional)
 *				@vdev: vdpa device
 *				Returns the iova range supported by
 *				the device.
 * @set_map:			Set device memory mapping (optional)
 *				Needed for device that using device
 *				specific DMA translation (on-chip IOMMU)
 *				@vdev: vdpa device
 *				@iotlb: vhost memory mapping to be
 *				used by the vDPA
 *				Returns integer: success (0) or error (< 0)
 * @dma_map:			Map an area of PA to IOVA (optional)
 *				Needed for device that using device
 *				specific DMA translation (on-chip IOMMU)
 *				and preferring incremental map.
 *				@vdev: vdpa device
 *				@iova: iova to be mapped
 *				@size: size of the area
 *				@pa: physical address for the map
 *				@perm: device access permission (VHOST_MAP_XX)
 *				Returns integer: success (0) or error (< 0)
 * @dma_unmap:			Unmap an area of IOVA (optional but
 *				must be implemented with dma_map)
 *				Needed for device that using device
 *				specific DMA translation (on-chip IOMMU)
 *				and preferring incremental unmap.
 *				@vdev: vdpa device
 *				@iova: iova to be unmapped
 *				@size: size of the area
 *				Returns integer: success (0) or error (< 0)
 * @free:			Free resources that belongs to vDPA (optional)
 *				@vdev: vdpa device
 */
struct vdpa_config_ops {
	/* Virtqueue ops */
	int (*set_vq_address)(struct vdpa_device *vdev,
			      u16 idx, u64 desc_area, u64 driver_area,
			      u64 device_area);
	void (*set_vq_num)(struct vdpa_device *vdev, u16 idx, u32 num);
	void (*kick_vq)(struct vdpa_device *vdev, u16 idx);
	void (*set_vq_cb)(struct vdpa_device *vdev, u16 idx,
			  struct vdpa_callback *cb);
	void (*set_vq_ready)(struct vdpa_device *vdev, u16 idx, bool ready);
	bool (*get_vq_ready)(struct vdpa_device *vdev, u16 idx);
	int (*set_vq_state)(struct vdpa_device *vdev, u16 idx,
			    const struct vdpa_vq_state *state);
	int (*get_vq_state)(struct vdpa_device *vdev, u16 idx,
			    struct vdpa_vq_state *state);
	struct vdpa_notification_area
	(*get_vq_notification)(struct vdpa_device *vdev, u16 idx);
	/* vq irq is not expected to be changed once DRIVER_OK is set */
	int (*get_vq_irq)(struct vdpa_device *vdv, u16 idx);

	/* Device ops */
	u32 (*get_vq_align)(struct vdpa_device *vdev);
	u64 (*get_features)(struct vdpa_device *vdev);
	int (*set_features)(struct vdpa_device *vdev, u64 features);
	void (*set_config_cb)(struct vdpa_device *vdev,
			      struct vdpa_callback *cb);
	u16 (*get_vq_num_max)(struct vdpa_device *vdev);
	u32 (*get_device_id)(struct vdpa_device *vdev);
	u32 (*get_vendor_id)(struct vdpa_device *vdev);
	u8 (*get_status)(struct vdpa_device *vdev);
	void (*set_status)(struct vdpa_device *vdev, u8 status);
	int (*reset)(struct vdpa_device *vdev);
	size_t (*get_config_size)(struct vdpa_device *vdev);
	void (*get_config)(struct vdpa_device *vdev, unsigned int offset,
			   void *buf, unsigned int len);
	void (*set_config)(struct vdpa_device *vdev, unsigned int offset,
			   const void *buf, unsigned int len);
	u32 (*get_generation)(struct vdpa_device *vdev);
	struct vdpa_iova_range (*get_iova_range)(struct vdpa_device *vdev);

	/* DMA ops */
	int (*set_map)(struct vdpa_device *vdev, struct vhost_iotlb *iotlb);
	int (*dma_map)(struct vdpa_device *vdev, u64 iova, u64 size,
		       u64 pa, u32 perm);
	int (*dma_unmap)(struct vdpa_device *vdev, u64 iova, u64 size);

	/* Free device resources */
	void (*free)(struct vdpa_device *vdev);
};

struct vdpa_device *__vdpa_alloc_device(struct device *parent,
					const struct vdpa_config_ops *config,
					size_t size, const char *name);

/**
 * vdpa_alloc_device - allocate and initilaize a vDPA device
 *
 * @dev_struct: the type of the parent structure
 * @member: the name of struct vdpa_device within the @dev_struct
 * @parent: the parent device
 * @config: the bus operations that is supported by this device
 * @name: name of the vdpa device
 *
 * Return allocated data structure or ERR_PTR upon error
 */
#define vdpa_alloc_device(dev_struct, member, parent, config, name)   \
			  container_of(__vdpa_alloc_device( \
				       parent, config, \
				       sizeof(dev_struct) + \
				       BUILD_BUG_ON_ZERO(offsetof( \
				       dev_struct, member)), name), \
				       dev_struct, member)

int vdpa_register_device(struct vdpa_device *vdev, int nvqs);
void vdpa_unregister_device(struct vdpa_device *vdev);

int _vdpa_register_device(struct vdpa_device *vdev, int nvqs);
void _vdpa_unregister_device(struct vdpa_device *vdev);

/**
 * struct vdpa_driver - operations for a vDPA driver
 * @driver: underlying device driver
 * @probe: the function to call when a device is found.  Returns 0 or -errno.
 * @remove: the function to call when a device is removed.
 */
struct vdpa_driver {
	struct device_driver driver;
	int (*probe)(struct vdpa_device *vdev);
	void (*remove)(struct vdpa_device *vdev);
};

#define vdpa_register_driver(drv) \
	__vdpa_register_driver(drv, THIS_MODULE)
int __vdpa_register_driver(struct vdpa_driver *drv, struct module *owner);
void vdpa_unregister_driver(struct vdpa_driver *drv);

#define module_vdpa_driver(__vdpa_driver) \
	module_driver(__vdpa_driver, vdpa_register_driver,	\
		      vdpa_unregister_driver)

static inline struct vdpa_driver *drv_to_vdpa(struct device_driver *driver)
{
	return container_of(driver, struct vdpa_driver, driver);
}

static inline struct vdpa_device *dev_to_vdpa(struct device *_dev)
{
	return container_of(_dev, struct vdpa_device, dev);
}

static inline void *vdpa_get_drvdata(const struct vdpa_device *vdev)
{
	return dev_get_drvdata(&vdev->dev);
}

static inline void vdpa_set_drvdata(struct vdpa_device *vdev, void *data)
{
	dev_set_drvdata(&vdev->dev, data);
}

static inline struct device *vdpa_get_dma_dev(struct vdpa_device *vdev)
{
	return vdev->dma_dev;
}

static inline int vdpa_reset(struct vdpa_device *vdev)
{
	const struct vdpa_config_ops *ops = vdev->config;

	vdev->features_valid = false;
	return ops->reset(vdev);
}

static inline int vdpa_set_features(struct vdpa_device *vdev, u64 features)
{
	const struct vdpa_config_ops *ops = vdev->config;

	vdev->features_valid = true;
	return ops->set_features(vdev, features);
}

static inline void vdpa_get_config(struct vdpa_device *vdev,
				   unsigned int offset, void *buf,
				   unsigned int len)
{
	const struct vdpa_config_ops *ops = vdev->config;

	/*
	 * Config accesses aren't supposed to trigger before features are set.
	 * If it does happen we assume a legacy guest.
	 */
	if (!vdev->features_valid)
		vdpa_set_features(vdev, 0);
	ops->get_config(vdev, offset, buf, len);
}

/**
 * struct vdpa_mgmtdev_ops - vdpa device ops
 * @dev_add: Add a vdpa device using alloc and register
 *	     @mdev: parent device to use for device addition
 *	     @name: name of the new vdpa device
 *	     Driver need to add a new device using _vdpa_register_device()
 *	     after fully initializing the vdpa device. Driver must return 0
 *	     on success or appropriate error code.
 * @dev_del: Remove a vdpa device using unregister
 *	     @mdev: parent device to use for device removal
 *	     @dev: vdpa device to remove
 *	     Driver need to remove the specified device by calling
 *	     _vdpa_unregister_device().
 */
struct vdpa_mgmtdev_ops {
	int (*dev_add)(struct vdpa_mgmt_dev *mdev, const char *name);
	void (*dev_del)(struct vdpa_mgmt_dev *mdev, struct vdpa_device *dev);
};

struct vdpa_mgmt_dev {
	struct device *device;
	const struct vdpa_mgmtdev_ops *ops;
	const struct virtio_device_id *id_table; /* supported ids */
	struct list_head list;
};

int vdpa_mgmtdev_register(struct vdpa_mgmt_dev *mdev);
void vdpa_mgmtdev_unregister(struct vdpa_mgmt_dev *mdev);

#endif /* _LINUX_VDPA_H */
