#ifndef _LINUX_VIRTIO_CONFIG_H
#define _LINUX_VIRTIO_CONFIG_H
/* Virtio devices use a standardized configuration space to define their
 * features and pass configuration information, but each implementation can
 * store and access that space differently. */
#include <linux/types.h>

/* Status byte for guest to report progress, and synchronize config. */
/* We have seen device and processed generic fields (VIRTIO_CONFIG_F_VIRTIO) */
#define VIRTIO_CONFIG_S_ACKNOWLEDGE	1
/* We have found a driver for the device. */
#define VIRTIO_CONFIG_S_DRIVER		2
/* Driver has used its parts of the config, and is happy */
#define VIRTIO_CONFIG_S_DRIVER_OK	4
/* We've given up on this device. */
#define VIRTIO_CONFIG_S_FAILED		0x80

/* Feature byte (actually 7 bits availabe): */
/* Requirements/features of the virtio implementation. */
#define VIRTIO_CONFIG_F_VIRTIO 1
/* Requirements/features of the virtqueue (may have more than one). */
#define VIRTIO_CONFIG_F_VIRTQUEUE 2

#ifdef __KERNEL__
struct virtio_device;

/**
 * virtio_config_ops - operations for configuring a virtio device
 * @find: search for the next configuration field of the given type.
 *	vdev: the virtio_device
 *	type: the feature type
 *	len: the (returned) length of the field if found.
 *	Returns a token if found, or NULL.  Never returnes the same field twice
 *	(ie. it's used up).
 * @get: read the value of a configuration field after find().
 *	vdev: the virtio_device
 *	token: the token returned from find().
 *	buf: the buffer to write the field value into.
 *	len: the length of the buffer (given by find()).
 *	Note that contents are conventionally little-endian.
 * @set: write the value of a configuration field after find().
 *	vdev: the virtio_device
 *	token: the token returned from find().
 *	buf: the buffer to read the field value from.
 *	len: the length of the buffer (given by find()).
 *	Note that contents are conventionally little-endian.
 * @get_status: read the status byte
 *	vdev: the virtio_device
 *	Returns the status byte
 * @set_status: write the status byte
 *	vdev: the virtio_device
 *	status: the new status byte
 * @find_vq: find the first VIRTIO_CONFIG_F_VIRTQUEUE and create a virtqueue.
 *	vdev: the virtio_device
 *	callback: the virqtueue callback
 *	Returns the new virtqueue or ERR_PTR().
 * @del_vq: free a virtqueue found by find_vq().
 */
struct virtio_config_ops
{
	void *(*find)(struct virtio_device *vdev, u8 type, unsigned *len);
	void (*get)(struct virtio_device *vdev, void *token,
		    void *buf, unsigned len);
	void (*set)(struct virtio_device *vdev, void *token,
		    const void *buf, unsigned len);
	u8 (*get_status)(struct virtio_device *vdev);
	void (*set_status)(struct virtio_device *vdev, u8 status);
	struct virtqueue *(*find_vq)(struct virtio_device *vdev,
				     bool (*callback)(struct virtqueue *));
	void (*del_vq)(struct virtqueue *vq);
};

/**
 * virtio_config_val - get a single virtio config and mark it used.
 * @config: the virtio config space
 * @type: the type to search for.
 * @val: a pointer to the value to fill in.
 *
 * Once used, the config type is marked with VIRTIO_CONFIG_F_USED so it can't
 * be found again.  This version does endian conversion. */
#define virtio_config_val(vdev, type, v) ({				\
	int _err = __virtio_config_val((vdev),(type),(v),sizeof(*(v))); \
									\
	BUILD_BUG_ON(sizeof(*(v)) != 1 && sizeof(*(v)) != 2		\
		     && sizeof(*(v)) != 4 && sizeof(*(v)) != 8);	\
	if (!_err) {							\
		switch (sizeof(*(v))) {					\
		case 2: le16_to_cpus((__u16 *) v); break;		\
		case 4: le32_to_cpus((__u32 *) v); break;		\
		case 8: le64_to_cpus((__u64 *) v); break;		\
		}							\
	}								\
	_err;								\
})

int __virtio_config_val(struct virtio_device *dev,
			u8 type, void *val, size_t size);

/**
 * virtio_use_bit - helper to use a feature bit in a bitfield value.
 * @dev: the virtio device
 * @token: the token as returned from vdev->config->find().
 * @len: the length of the field.
 * @bitnum: the bit to test.
 *
 * If handed a NULL token, it returns false, otherwise returns bit status.
 * If it's one, it sets the mirroring acknowledgement bit. */
int virtio_use_bit(struct virtio_device *vdev,
		   void *token, unsigned int len, unsigned int bitnum);
#endif /* __KERNEL__ */
#endif /* _LINUX_VIRTIO_CONFIG_H */
