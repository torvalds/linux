#ifndef _LINUX_VIRTIO_H
#define _LINUX_VIRTIO_H
/* Everything a virtio driver needs to work with any particular virtio
 * implementation. */
#include <linux/types.h>
#include <linux/scatterlist.h>
#include <linux/spinlock.h>
#include <linux/device.h>
#include <linux/mod_devicetable.h>

/**
 * virtqueue - a queue to register buffers for sending or receiving.
 * @callback: the function to call when buffers are consumed (can be NULL).
 * @vdev: the virtio device this queue was created for.
 * @vq_ops: the operations for this virtqueue (see below).
 * @priv: a pointer for the virtqueue implementation to use.
 */
struct virtqueue
{
	void (*callback)(struct virtqueue *vq);
	struct virtio_device *vdev;
	struct virtqueue_ops *vq_ops;
	void *priv;
};

/**
 * virtqueue_ops - operations for virtqueue abstraction layer
 * @add_buf: expose buffer to other end
 *	vq: the struct virtqueue we're talking about.
 *	sg: the description of the buffer(s).
 *	out_num: the number of sg readable by other side
 *	in_num: the number of sg which are writable (after readable ones)
 *	data: the token identifying the buffer.
 *      Returns 0 or an error.
 * @kick: update after add_buf
 *	vq: the struct virtqueue
 *	After one or more add_buf calls, invoke this to kick the other side.
 * @get_buf: get the next used buffer
 *	vq: the struct virtqueue we're talking about.
 *	len: the length written into the buffer
 *	Returns NULL or the "data" token handed to add_buf.
 * @disable_cb: disable callbacks
 *	vq: the struct virtqueue we're talking about.
 * @enable_cb: restart callbacks after disable_cb.
 *	vq: the struct virtqueue we're talking about.
 *	This re-enables callbacks; it returns "false" if there are pending
 *	buffers in the queue, to detect a possible race between the driver
 *	checking for more work, and enabling callbacks.
 *
 * Locking rules are straightforward: the driver is responsible for
 * locking.  No two operations may be invoked simultaneously.
 *
 * All operations can be called in any context.
 */
struct virtqueue_ops {
	int (*add_buf)(struct virtqueue *vq,
		       struct scatterlist sg[],
		       unsigned int out_num,
		       unsigned int in_num,
		       void *data);

	void (*kick)(struct virtqueue *vq);

	void *(*get_buf)(struct virtqueue *vq, unsigned int *len);

	void (*disable_cb)(struct virtqueue *vq);
	bool (*enable_cb)(struct virtqueue *vq);
};

/**
 * virtio_device - representation of a device using virtio
 * @index: unique position on the virtio bus
 * @dev: underlying device.
 * @id: the device type identification (used to match it with a driver).
 * @config: the configuration ops for this device.
 * @priv: private pointer for the driver's use.
 */
struct virtio_device
{
	int index;
	struct device dev;
	struct virtio_device_id id;
	struct virtio_config_ops *config;
	void *priv;
};

int register_virtio_device(struct virtio_device *dev);
void unregister_virtio_device(struct virtio_device *dev);

/**
 * virtio_driver - operations for a virtio I/O driver
 * @driver: underlying device driver (populate name and owner).
 * @id_table: the ids serviced by this driver.
 * @probe: the function to call when a device is found.  Returns a token for
 *    remove, or PTR_ERR().
 * @remove: the function when a device is removed.
 * @config_changed: optional function to call when the device configuration
 *    changes; may be called in interrupt context.
 */
struct virtio_driver {
	struct device_driver driver;
	const struct virtio_device_id *id_table;
	int (*probe)(struct virtio_device *dev);
	void (*remove)(struct virtio_device *dev);
	void (*config_changed)(struct virtio_device *dev);
};

int register_virtio_driver(struct virtio_driver *drv);
void unregister_virtio_driver(struct virtio_driver *drv);
#endif /* _LINUX_VIRTIO_H */
