/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _LINUX_VDPA_H
#define _LINUX_VDPA_H

#include <linux/kernel.h>
#include <linux/device.h>
#include <linux/interrupt.h>
#include <linux/vhost_iotlb.h>

/**
 * vDPA callback definition.
 * @callback: interrupt callback function
 * @private: the data passed to the callback function
 */
struct vdpa_callback {
	irqreturn_t (*callback)(void *data);
	void *private;
};

/**
 * vDPA device - representation of a vDPA device
 * @dev: underlying device
 * @dma_dev: the actual device that is performing DMA
 * @config: the configuration ops for this device.
 * @index: device index
 */
struct vdpa_device {
	struct device dev;
	struct device *dma_dev;
	const struct vdpa_config_ops *config;
	unsigned int index;
};

/**
 * vDPA_config_ops - operations for configuring a vDPA device.
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
 *				@state: virtqueue state (last_avail_idx)
 *				Returns integer: success (0) or error (< 0)
 * @get_vq_state:		Get the state for a virtqueue
 *				@vdev: vdpa device
 *				@idx: virtqueue index
 *				Returns virtqueue state (last_avail_idx)
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
	int (*set_vq_state)(struct vdpa_device *vdev, u16 idx, u64 state);
	u64 (*get_vq_state)(struct vdpa_device *vdev, u16 idx);

	/* Device ops */
	u16 (*get_vq_align)(struct vdpa_device *vdev);
	u64 (*get_features)(struct vdpa_device *vdev);
	int (*set_features)(struct vdpa_device *vdev, u64 features);
	void (*set_config_cb)(struct vdpa_device *vdev,
			      struct vdpa_callback *cb);
	u16 (*get_vq_num_max)(struct vdpa_device *vdev);
	u32 (*get_device_id)(struct vdpa_device *vdev);
	u32 (*get_vendor_id)(struct vdpa_device *vdev);
	u8 (*get_status)(struct vdpa_device *vdev);
	void (*set_status)(struct vdpa_device *vdev, u8 status);
	void (*get_config)(struct vdpa_device *vdev, unsigned int offset,
			   void *buf, unsigned int len);
	void (*set_config)(struct vdpa_device *vdev, unsigned int offset,
			   const void *buf, unsigned int len);
	u32 (*get_generation)(struct vdpa_device *vdev);

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
					size_t size);

#define vdpa_alloc_device(dev_struct, member, parent, config)   \
			  container_of(__vdpa_alloc_device( \
				       parent, config, \
				       sizeof(dev_struct) + \
				       BUILD_BUG_ON_ZERO(offsetof( \
				       dev_struct, member))), \
				       dev_struct, member)

int vdpa_register_device(struct vdpa_device *vdev);
void vdpa_unregister_device(struct vdpa_device *vdev);

/**
 * vdpa_driver - operations for a vDPA driver
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
#endif /* _LINUX_VDPA_H */
