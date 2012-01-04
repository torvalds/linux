#ifndef _LINUX_VIRTIO_H
#define _LINUX_VIRTIO_H
/* Everything a virtio driver needs to work with any particular virtio
 * implementation. */
#include <linux/types.h>
#include <linux/scatterlist.h>
#include <linux/spinlock.h>
#include <linux/device.h>
#include <linux/mod_devicetable.h>
#include <linux/gfp.h>

/**
 * virtqueue - a queue to register buffers for sending or receiving.
 * @list: the chain of virtqueues for this device
 * @callback: the function to call when buffers are consumed (can be NULL).
 * @name: the name of this virtqueue (mainly for debugging)
 * @vdev: the virtio device this queue was created for.
 * @priv: a pointer for the virtqueue implementation to use.
 */
struct virtqueue {
	struct list_head list;
	void (*callback)(struct virtqueue *vq);
	const char *name;
	struct virtio_device *vdev;
	void *priv;
};

/**
 * operations for virtqueue
 * virtqueue_add_buf: expose buffer to other end
 *	vq: the struct virtqueue we're talking about.
 *	sg: the description of the buffer(s).
 *	out_num: the number of sg readable by other side
 *	in_num: the number of sg which are writable (after readable ones)
 *	data: the token identifying the buffer.
 *	gfp: how to do memory allocations (if necessary).
 *      Returns remaining capacity of queue (sg segments) or a negative error.
 * virtqueue_kick: update after add_buf
 *	vq: the struct virtqueue
 *	After one or more add_buf calls, invoke this to kick the other side.
 * virtqueue_get_buf: get the next used buffer
 *	vq: the struct virtqueue we're talking about.
 *	len: the length written into the buffer
 *	Returns NULL or the "data" token handed to add_buf.
 * virtqueue_disable_cb: disable callbacks
 *	vq: the struct virtqueue we're talking about.
 *	Note that this is not necessarily synchronous, hence unreliable and only
 *	useful as an optimization.
 * virtqueue_enable_cb: restart callbacks after disable_cb.
 *	vq: the struct virtqueue we're talking about.
 *	This re-enables callbacks; it returns "false" if there are pending
 *	buffers in the queue, to detect a possible race between the driver
 *	checking for more work, and enabling callbacks.
 * virtqueue_enable_cb_delayed: restart callbacks after disable_cb.
 *	vq: the struct virtqueue we're talking about.
 *	This re-enables callbacks but hints to the other side to delay
 *	interrupts until most of the available buffers have been processed;
 *	it returns "false" if there are many pending buffers in the queue,
 *	to detect a possible race between the driver checking for more work,
 *	and enabling callbacks.
 * virtqueue_detach_unused_buf: detach first unused buffer
 * 	vq: the struct virtqueue we're talking about.
 * 	Returns NULL or the "data" token handed to add_buf
 * virtqueue_get_vring_size: return the size of the virtqueue's vring
 *	vq: the struct virtqueue containing the vring of interest.
 *	Returns the size of the vring.
 *
 * Locking rules are straightforward: the driver is responsible for
 * locking.  No two operations may be invoked simultaneously, with the exception
 * of virtqueue_disable_cb.
 *
 * All operations can be called in any context.
 */

int virtqueue_add_buf_gfp(struct virtqueue *vq,
			  struct scatterlist sg[],
			  unsigned int out_num,
			  unsigned int in_num,
			  void *data,
			  gfp_t gfp);

static inline int virtqueue_add_buf(struct virtqueue *vq,
				    struct scatterlist sg[],
				    unsigned int out_num,
				    unsigned int in_num,
				    void *data)
{
	return virtqueue_add_buf_gfp(vq, sg, out_num, in_num, data, GFP_ATOMIC);
}

void virtqueue_kick(struct virtqueue *vq);

void *virtqueue_get_buf(struct virtqueue *vq, unsigned int *len);

void virtqueue_disable_cb(struct virtqueue *vq);

bool virtqueue_enable_cb(struct virtqueue *vq);

bool virtqueue_enable_cb_delayed(struct virtqueue *vq);

void *virtqueue_detach_unused_buf(struct virtqueue *vq);

unsigned int virtqueue_get_vring_size(struct virtqueue *vq);

/**
 * virtio_device - representation of a device using virtio
 * @index: unique position on the virtio bus
 * @dev: underlying device.
 * @id: the device type identification (used to match it with a driver).
 * @config: the configuration ops for this device.
 * @vqs: the list of virtqueues for this device.
 * @features: the features supported by both driver and device.
 * @priv: private pointer for the driver's use.
 */
struct virtio_device {
	int index;
	struct device dev;
	struct virtio_device_id id;
	struct virtio_config_ops *config;
	struct list_head vqs;
	/* Note that this is a Linux set_bit-style bitmap. */
	unsigned long features[1];
	void *priv;
};

#define dev_to_virtio(dev) container_of(dev, struct virtio_device, dev)
int register_virtio_device(struct virtio_device *dev);
void unregister_virtio_device(struct virtio_device *dev);

/**
 * virtio_driver - operations for a virtio I/O driver
 * @driver: underlying device driver (populate name and owner).
 * @id_table: the ids serviced by this driver.
 * @feature_table: an array of feature numbers supported by this driver.
 * @feature_table_size: number of entries in the feature table array.
 * @probe: the function to call when a device is found.  Returns 0 or -errno.
 * @remove: the function to call when a device is removed.
 * @config_changed: optional function to call when the device configuration
 *    changes; may be called in interrupt context.
 */
struct virtio_driver {
	struct device_driver driver;
	const struct virtio_device_id *id_table;
	const unsigned int *feature_table;
	unsigned int feature_table_size;
	int (*probe)(struct virtio_device *dev);
	void (*remove)(struct virtio_device *dev);
	void (*config_changed)(struct virtio_device *dev);
};

int register_virtio_driver(struct virtio_driver *drv);
void unregister_virtio_driver(struct virtio_driver *drv);
#endif /* _LINUX_VIRTIO_H */
