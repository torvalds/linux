#ifndef _LINUX_VIRTIO_CONFIG_H
#define _LINUX_VIRTIO_CONFIG_H
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

#ifdef __KERNEL__
#include <linux/virtio.h>

/**
 * virtio_config_ops - operations for configuring a virtio device
 * @feature: search for a feature in this config
 *	vdev: the virtio_device
 *	bit: the feature bit
 *	Returns true if the feature is supported.  Acknowledges the feature
 *	so the host can see it.
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
 */
struct virtio_config_ops
{
	bool (*feature)(struct virtio_device *vdev, unsigned bit);
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
};

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
	virtio_config_buf((vdev), (fbit), (offset), (v), sizeof(v))

static inline int virtio_config_buf(struct virtio_device *vdev,
				    unsigned int fbit,
				    unsigned int offset,
				    void *buf, unsigned len)
{
	if (!vdev->config->feature(vdev, fbit))
		return -ENOENT;

	vdev->config->get(vdev, offset, buf, len);
	return 0;
}
#endif /* __KERNEL__ */
#endif /* _LINUX_VIRTIO_CONFIG_H */
