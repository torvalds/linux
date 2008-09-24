#ifndef _LINUX_VIRTIO_CONFIG_H
#define _LINUX_VIRTIO_CONFIG_H
/* This header, excluding the #ifdef __KERNEL__ part, is BSD licensed so
 * anyone can use the definitions to implement compatible drivers/servers. */

/* Virtio devices use a standardized configuration space to define their
 * features and pass configuration information, but each implementation can
 * store and access that space differently. */
#include <linux/types.h>

/* Status byte for guest to report progress, and synchronize features. */
/* We have seen device and processed generic fields (VIRTIO_CONFIG_F_VIRTIO) */
#define VIRTIO_CONFIG_S_ACKNOWLEDGE	1
/* We have found a driver for the device. */
#define VIRTIO_CONFIG_S_DRIVER		2
/* Driver has used its parts of the config, and is happy */
#define VIRTIO_CONFIG_S_DRIVER_OK	4
/* We've given up on this device. */
#define VIRTIO_CONFIG_S_FAILED		0x80

/* Some virtio feature bits (currently bits 28 through 31) are reserved for the
 * transport being used (eg. virtio_ring), the rest are per-device feature
 * bits. */
#define VIRTIO_TRANSPORT_F_START	28
#define VIRTIO_TRANSPORT_F_END		32

/* Do we get callbacks when the ring is completely used, even if we've
 * suppressed them? */
#define VIRTIO_F_NOTIFY_ON_EMPTY	24

#ifdef __KERNEL__
#include <linux/virtio.h>

/**
 * virtio_config_ops - operations for configuring a virtio device
 * @get: read the value of a configuration field
 *	vdev: the virtio_device
 *	offset: the offset of the configuration field
 *	buf: the buffer to write the field value into.
 *	len: the length of the buffer
 * @set: write the value of a configuration field
 *	vdev: the virtio_device
 *	offset: the offset of the configuration field
 *	buf: the buffer to read the field value from.
 *	len: the length of the buffer
 * @get_status: read the status byte
 *	vdev: the virtio_device
 *	Returns the status byte
 * @set_status: write the status byte
 *	vdev: the virtio_device
 *	status: the new status byte
 * @reset: reset the device
 *	vdev: the virtio device
 *	After this, status and feature negotiation must be done again
 * @find_vq: find a virtqueue and instantiate it.
 *	vdev: the virtio_device
 *	index: the 0-based virtqueue number in case there's more than one.
 *	callback: the virqtueue callback
 *	Returns the new virtqueue or ERR_PTR() (eg. -ENOENT).
 * @del_vq: free a virtqueue found by find_vq().
 * @get_features: get the array of feature bits for this device.
 *	vdev: the virtio_device
 *	Returns the first 32 feature bits (all we currently need).
 * @finalize_features: confirm what device features we'll be using.
 *	vdev: the virtio_device
 *	This gives the final feature bits for the device: it can change
 *	the dev->feature bits if it wants.
 */
struct virtio_config_ops
{
	void (*get)(struct virtio_device *vdev, unsigned offset,
		    void *buf, unsigned len);
	void (*set)(struct virtio_device *vdev, unsigned offset,
		    const void *buf, unsigned len);
	u8 (*get_status)(struct virtio_device *vdev);
	void (*set_status)(struct virtio_device *vdev, u8 status);
	void (*reset)(struct virtio_device *vdev);
	struct virtqueue *(*find_vq)(struct virtio_device *vdev,
				     unsigned index,
				     void (*callback)(struct virtqueue *));
	void (*del_vq)(struct virtqueue *vq);
	u32 (*get_features)(struct virtio_device *vdev);
	void (*finalize_features)(struct virtio_device *vdev);
};

/* If driver didn't advertise the feature, it will never appear. */
void virtio_check_driver_offered_feature(const struct virtio_device *vdev,
					 unsigned int fbit);

/**
 * virtio_has_feature - helper to determine if this device has this feature.
 * @vdev: the device
 * @fbit: the feature bit
 */
static inline bool virtio_has_feature(const struct virtio_device *vdev,
				      unsigned int fbit)
{
	/* Did you forget to fix assumptions on max features? */
	if (__builtin_constant_p(fbit))
		BUILD_BUG_ON(fbit >= 32);

	virtio_check_driver_offered_feature(vdev, fbit);
	return test_bit(fbit, vdev->features);
}

/**
 * virtio_config_val - look for a feature and get a virtio config entry.
 * @vdev: the virtio device
 * @fbit: the feature bit
 * @offset: the type to search for.
 * @val: a pointer to the value to fill in.
 *
 * The return value is -ENOENT if the feature doesn't exist.  Otherwise
 * the config value is copied into whatever is pointed to by v. */
#define virtio_config_val(vdev, fbit, offset, v) \
	virtio_config_buf((vdev), (fbit), (offset), (v), sizeof(*v))

static inline int virtio_config_buf(struct virtio_device *vdev,
				    unsigned int fbit,
				    unsigned int offset,
				    void *buf, unsigned len)
{
	if (!virtio_has_feature(vdev, fbit))
		return -ENOENT;

	vdev->config->get(vdev, offset, buf, len);
	return 0;
}
#endif /* __KERNEL__ */
#endif /* _LINUX_VIRTIO_CONFIG_H */
