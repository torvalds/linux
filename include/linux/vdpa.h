/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _LINUX_VDPA_H
#define _LINUX_VDPA_H

#include <linux/kernel.h>
#include <linux/device.h>
#include <linux/interrupt.h>
#include <linux/vhost_iotlb.h>
#include <linux/virtio_net.h>
#include <linux/if_ether.h>

/**
 * struct vdpa_callback - vDPA callback definition.
 * @callback: interrupt callback function
 * @private: the data passed to the callback function
 * @trigger: the eventfd for the callback (Optional).
 *           When it is set, the vDPA driver must guarantee that
 *           signaling it is functional equivalent to triggering
 *           the callback. Then vDPA parent can signal it directly
 *           instead of triggering the callback.
 */
struct vdpa_callback {
	irqreturn_t (*callback)(void *data);
	void *private;
	struct eventfd_ctx *trigger;
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
 * @driver_override: driver name to force a match; do not set directly,
 *                   because core frees it; use driver_set_override() to
 *                   set or clear it.
 * @config: the configuration ops for this device.
 * @cf_lock: Protects get and set access to configuration layout.
 * @index: device index
 * @features_valid: were features initialized? for legacy guests
 * @ngroups: the number of virtqueue groups
 * @nas: the number of address spaces
 * @use_va: indicate whether virtual address must be used by this device
 * @nvqs: maximum number of supported virtqueues
 * @mdev: management device pointer; caller must setup when registering device as part
 *	  of dev_add() mgmtdev ops callback before invoking _vdpa_register_device().
 */
struct vdpa_device {
	struct device dev;
	struct device *dma_dev;
	const char *driver_override;
	const struct vdpa_config_ops *config;
	struct rw_semaphore cf_lock; /* Protects get/set config */
	unsigned int index;
	bool features_valid;
	bool use_va;
	u32 nvqs;
	struct vdpa_mgmt_dev *mdev;
	unsigned int ngroups;
	unsigned int nas;
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

struct vdpa_dev_set_config {
	u64 device_features;
	struct {
		u8 mac[ETH_ALEN];
		u16 mtu;
		u16 max_vq_pairs;
	} net;
	u64 mask;
};

/**
 * struct vdpa_map_file - file area for device memory mapping
 * @file: vma->vm_file for the mapping
 * @offset: mapping offset in the vm_file
 */
struct vdpa_map_file {
	struct file *file;
	u64 offset;
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
 * @kick_vq_with_data:		Kick the virtqueue and supply extra data
 *				(only if VIRTIO_F_NOTIFICATION_DATA is negotiated)
 *				@vdev: vdpa device
 *				@data for split virtqueue:
 *				16 bits vqn and 16 bits next available index.
 *				@data for packed virtqueue:
 *				16 bits vqn, 15 least significant bits of
 *				next available index and 1 bit next_wrap.
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
 * @get_vendor_vq_stats:	Get the vendor statistics of a device.
 *				@vdev: vdpa device
 *				@idx: virtqueue index
 *				@msg: socket buffer holding stats message
 *				@extack: extack for reporting error messages
 *				Returns integer: success (0) or error (< 0)
 * @get_vq_notification:	Get the notification area for a virtqueue (optional)
 *				@vdev: vdpa device
 *				@idx: virtqueue index
 *				Returns the notification area
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
 * @get_vq_group:		Get the group id for a specific
 *				virtqueue (optional)
 *				@vdev: vdpa device
 *				@idx: virtqueue index
 *				Returns u32: group id for this virtqueue
 * @get_vq_desc_group:		Get the group id for the descriptor table of
 *				a specific virtqueue (optional)
 *				@vdev: vdpa device
 *				@idx: virtqueue index
 *				Returns u32: group id for the descriptor table
 *				portion of this virtqueue. Could be different
 *				than the one from @get_vq_group, in which case
 *				the access to the descriptor table can be
 *				confined to a separate asid, isolating from
 *				the virtqueue's buffer address access.
 * @get_device_features:	Get virtio features supported by the device
 *				@vdev: vdpa device
 *				Returns the virtio features support by the
 *				device
 * @get_backend_features:	Get parent-specific backend features (optional)
 *				Returns the vdpa features supported by the
 *				device.
 * @set_driver_features:	Set virtio features supported by the driver
 *				@vdev: vdpa device
 *				@features: feature support by the driver
 *				Returns integer: success (0) or error (< 0)
 * @get_driver_features:	Get the virtio driver features in action
 *				@vdev: vdpa device
 *				Returns the virtio features accepted
 * @set_config_cb:		Set the config interrupt callback
 *				@vdev: vdpa device
 *				@cb: virtio-vdev interrupt callback structure
 * @get_vq_num_max:		Get the max size of virtqueue
 *				@vdev: vdpa device
 *				Returns u16: max size of virtqueue
 * @get_vq_num_min:		Get the min size of virtqueue (optional)
 *				@vdev: vdpa device
 *				Returns u16: min size of virtqueue
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
 * @compat_reset:		Reset device with compatibility quirks to
 *				accommodate older userspace. Only needed by
 *				parent driver which used to have bogus reset
 *				behaviour, and has to maintain such behaviour
 *				for compatibility with older userspace.
 *				Historically compliant driver only has to
 *				implement .reset, Historically non-compliant
 *				driver should implement both.
 *				@vdev: vdpa device
 *				@flags: compatibility quirks for reset
 *				Returns integer: success (0) or error (< 0)
 * @suspend:			Suspend the device (optional)
 *				@vdev: vdpa device
 *				Returns integer: success (0) or error (< 0)
 * @resume:			Resume the device (optional)
 *				@vdev: vdpa device
 *				Returns integer: success (0) or error (< 0)
 * @get_config_size:		Get the size of the configuration space includes
 *				fields that are conditional on feature bits.
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
 * @set_vq_affinity:		Set the affinity of virtqueue (optional)
 *				@vdev: vdpa device
 *				@idx: virtqueue index
 *				@cpu_mask: the affinity mask
 *				Returns integer: success (0) or error (< 0)
 * @get_vq_affinity:		Get the affinity of virtqueue (optional)
 *				@vdev: vdpa device
 *				@idx: virtqueue index
 *				Returns the affinity mask
 * @set_group_asid:		Set address space identifier for a
 *				virtqueue group (optional)
 *				@vdev: vdpa device
 *				@group: virtqueue group
 *				@asid: address space id for this group
 *				Returns integer: success (0) or error (< 0)
 * @set_map:			Set device memory mapping (optional)
 *				Needed for device that using device
 *				specific DMA translation (on-chip IOMMU)
 *				@vdev: vdpa device
 *				@asid: address space identifier
 *				@iotlb: vhost memory mapping to be
 *				used by the vDPA
 *				Returns integer: success (0) or error (< 0)
 * @dma_map:			Map an area of PA to IOVA (optional)
 *				Needed for device that using device
 *				specific DMA translation (on-chip IOMMU)
 *				and preferring incremental map.
 *				@vdev: vdpa device
 *				@asid: address space identifier
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
 *				@asid: address space identifier
 *				@iova: iova to be unmapped
 *				@size: size of the area
 *				Returns integer: success (0) or error (< 0)
 * @reset_map:			Reset device memory mapping to the default
 *				state (optional)
 *				Needed for devices that are using device
 *				specific DMA translation and prefer mapping
 *				to be decoupled from the virtio life cycle,
 *				i.e. device .reset op does not reset mapping
 *				@vdev: vdpa device
 *				@asid: address space identifier
 *				Returns integer: success (0) or error (< 0)
 * @get_vq_dma_dev:		Get the dma device for a specific
 *				virtqueue (optional)
 *				@vdev: vdpa device
 *				@idx: virtqueue index
 *				Returns pointer to structure device or error (NULL)
 * @bind_mm:			Bind the device to a specific address space
 *				so the vDPA framework can use VA when this
 *				callback is implemented. (optional)
 *				@vdev: vdpa device
 *				@mm: address space to bind
 * @unbind_mm:			Unbind the device from the address space
 *				bound using the bind_mm callback. (optional)
 *				@vdev: vdpa device
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
	void (*kick_vq_with_data)(struct vdpa_device *vdev, u32 data);
	void (*set_vq_cb)(struct vdpa_device *vdev, u16 idx,
			  struct vdpa_callback *cb);
	void (*set_vq_ready)(struct vdpa_device *vdev, u16 idx, bool ready);
	bool (*get_vq_ready)(struct vdpa_device *vdev, u16 idx);
	int (*set_vq_state)(struct vdpa_device *vdev, u16 idx,
			    const struct vdpa_vq_state *state);
	int (*get_vq_state)(struct vdpa_device *vdev, u16 idx,
			    struct vdpa_vq_state *state);
	int (*get_vendor_vq_stats)(struct vdpa_device *vdev, u16 idx,
				   struct sk_buff *msg,
				   struct netlink_ext_ack *extack);
	struct vdpa_notification_area
	(*get_vq_notification)(struct vdpa_device *vdev, u16 idx);
	/* vq irq is not expected to be changed once DRIVER_OK is set */
	int (*get_vq_irq)(struct vdpa_device *vdev, u16 idx);

	/* Device ops */
	u32 (*get_vq_align)(struct vdpa_device *vdev);
	u32 (*get_vq_group)(struct vdpa_device *vdev, u16 idx);
	u32 (*get_vq_desc_group)(struct vdpa_device *vdev, u16 idx);
	u64 (*get_device_features)(struct vdpa_device *vdev);
	u64 (*get_backend_features)(const struct vdpa_device *vdev);
	int (*set_driver_features)(struct vdpa_device *vdev, u64 features);
	u64 (*get_driver_features)(struct vdpa_device *vdev);
	void (*set_config_cb)(struct vdpa_device *vdev,
			      struct vdpa_callback *cb);
	u16 (*get_vq_num_max)(struct vdpa_device *vdev);
	u16 (*get_vq_num_min)(struct vdpa_device *vdev);
	u32 (*get_device_id)(struct vdpa_device *vdev);
	u32 (*get_vendor_id)(struct vdpa_device *vdev);
	u8 (*get_status)(struct vdpa_device *vdev);
	void (*set_status)(struct vdpa_device *vdev, u8 status);
	int (*reset)(struct vdpa_device *vdev);
	int (*compat_reset)(struct vdpa_device *vdev, u32 flags);
#define VDPA_RESET_F_CLEAN_MAP 1
	int (*suspend)(struct vdpa_device *vdev);
	int (*resume)(struct vdpa_device *vdev);
	size_t (*get_config_size)(struct vdpa_device *vdev);
	void (*get_config)(struct vdpa_device *vdev, unsigned int offset,
			   void *buf, unsigned int len);
	void (*set_config)(struct vdpa_device *vdev, unsigned int offset,
			   const void *buf, unsigned int len);
	u32 (*get_generation)(struct vdpa_device *vdev);
	struct vdpa_iova_range (*get_iova_range)(struct vdpa_device *vdev);
	int (*set_vq_affinity)(struct vdpa_device *vdev, u16 idx,
			       const struct cpumask *cpu_mask);
	const struct cpumask *(*get_vq_affinity)(struct vdpa_device *vdev,
						 u16 idx);

	/* DMA ops */
	int (*set_map)(struct vdpa_device *vdev, unsigned int asid,
		       struct vhost_iotlb *iotlb);
	int (*dma_map)(struct vdpa_device *vdev, unsigned int asid,
		       u64 iova, u64 size, u64 pa, u32 perm, void *opaque);
	int (*dma_unmap)(struct vdpa_device *vdev, unsigned int asid,
			 u64 iova, u64 size);
	int (*reset_map)(struct vdpa_device *vdev, unsigned int asid);
	int (*set_group_asid)(struct vdpa_device *vdev, unsigned int group,
			      unsigned int asid);
	struct device *(*get_vq_dma_dev)(struct vdpa_device *vdev, u16 idx);
	int (*bind_mm)(struct vdpa_device *vdev, struct mm_struct *mm);
	void (*unbind_mm)(struct vdpa_device *vdev);

	/* Free device resources */
	void (*free)(struct vdpa_device *vdev);
};

struct vdpa_device *__vdpa_alloc_device(struct device *parent,
					const struct vdpa_config_ops *config,
					unsigned int ngroups, unsigned int nas,
					size_t size, const char *name,
					bool use_va);

/**
 * vdpa_alloc_device - allocate and initilaize a vDPA device
 *
 * @dev_struct: the type of the parent structure
 * @member: the name of struct vdpa_device within the @dev_struct
 * @parent: the parent device
 * @config: the bus operations that is supported by this device
 * @ngroups: the number of virtqueue groups supported by this device
 * @nas: the number of address spaces
 * @name: name of the vdpa device
 * @use_va: indicate whether virtual address must be used by this device
 *
 * Return allocated data structure or ERR_PTR upon error
 */
#define vdpa_alloc_device(dev_struct, member, parent, config, ngroups, nas, \
			  name, use_va) \
			  container_of((__vdpa_alloc_device( \
				       parent, config, ngroups, nas, \
				       (sizeof(dev_struct) + \
				       BUILD_BUG_ON_ZERO(offsetof( \
				       dev_struct, member))), name, use_va)), \
				       dev_struct, member)

int vdpa_register_device(struct vdpa_device *vdev, u32 nvqs);
void vdpa_unregister_device(struct vdpa_device *vdev);

int _vdpa_register_device(struct vdpa_device *vdev, u32 nvqs);
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

static inline int vdpa_reset(struct vdpa_device *vdev, u32 flags)
{
	const struct vdpa_config_ops *ops = vdev->config;
	int ret;

	down_write(&vdev->cf_lock);
	vdev->features_valid = false;
	if (ops->compat_reset && flags)
		ret = ops->compat_reset(vdev, flags);
	else
		ret = ops->reset(vdev);
	up_write(&vdev->cf_lock);
	return ret;
}

static inline int vdpa_set_features_unlocked(struct vdpa_device *vdev, u64 features)
{
	const struct vdpa_config_ops *ops = vdev->config;
	int ret;

	vdev->features_valid = true;
	ret = ops->set_driver_features(vdev, features);

	return ret;
}

static inline int vdpa_set_features(struct vdpa_device *vdev, u64 features)
{
	int ret;

	down_write(&vdev->cf_lock);
	ret = vdpa_set_features_unlocked(vdev, features);
	up_write(&vdev->cf_lock);

	return ret;
}

void vdpa_get_config(struct vdpa_device *vdev, unsigned int offset,
		     void *buf, unsigned int len);
void vdpa_set_config(struct vdpa_device *dev, unsigned int offset,
		     const void *buf, unsigned int length);
void vdpa_set_status(struct vdpa_device *vdev, u8 status);

/**
 * struct vdpa_mgmtdev_ops - vdpa device ops
 * @dev_add: Add a vdpa device using alloc and register
 *	     @mdev: parent device to use for device addition
 *	     @name: name of the new vdpa device
 *	     @config: config attributes to apply to the device under creation
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
	int (*dev_add)(struct vdpa_mgmt_dev *mdev, const char *name,
		       const struct vdpa_dev_set_config *config);
	void (*dev_del)(struct vdpa_mgmt_dev *mdev, struct vdpa_device *dev);
};

/**
 * struct vdpa_mgmt_dev - vdpa management device
 * @device: Management parent device
 * @ops: operations supported by management device
 * @id_table: Pointer to device id table of supported ids
 * @config_attr_mask: bit mask of attributes of type enum vdpa_attr that
 *		      management device support during dev_add callback
 * @list: list entry
 * @supported_features: features supported by device
 * @max_supported_vqs: maximum number of virtqueues supported by device
 */
struct vdpa_mgmt_dev {
	struct device *device;
	const struct vdpa_mgmtdev_ops *ops;
	struct virtio_device_id *id_table;
	u64 config_attr_mask;
	struct list_head list;
	u64 supported_features;
	u32 max_supported_vqs;
};

int vdpa_mgmtdev_register(struct vdpa_mgmt_dev *mdev);
void vdpa_mgmtdev_unregister(struct vdpa_mgmt_dev *mdev);

#endif /* _LINUX_VDPA_H */
