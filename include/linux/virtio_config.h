#ifndef _LINUX_VIRTIO_CONFIG_H
#define _LINUX_VIRTIO_CONFIG_H
/* This header, excluding the #ifdef __KERNEL__ part, is BSD licensed so
 * anyone can use the definitions to implement compatible drivers/servers.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 * 3. Neither the name of IBM nor the names of its contributors
 *    may be used to endorse or promote products derived from this software
 *    without specific prior written permission.
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL IBM OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE. */

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
#include <linux/err.h>
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
 * @request_vqs: request the specified number of virtqueues
 *	vdev: the virtio_device
 *	max_vqs: the max number of virtqueues we want
 *      If supplied, must call before any virtqueues are instantiated.
 *      To modify the max number of virtqueues after request_vqs has been
 *      called, call free_vqs and then request_vqs with a new value.
 * @free_vqs: cleanup resources allocated by request_vqs
 *	vdev: the virtio_device
 *      If supplied, must call after all virtqueues have been deleted.
 * @reset: reset the device
 *	vdev: the virtio device
 *	After this, status and feature negotiation must be done again
 *	Device must not be reset from its vq/config callbacks, or in
 *	parallel with being added/removed.
 * @find_vqs: find virtqueues and instantiate them.
 *	vdev: the virtio_device
 *	nvqs: the number of virtqueues to find
 *	vqs: on success, includes new virtqueues
 *	callbacks: array of callbacks, for each virtqueue
 *	names: array of virtqueue names (mainly for debugging)
 *	Returns 0 on success or error status
 * @del_vqs: free virtqueues found by find_vqs().
 * @get_features: get the array of feature bits for this device.
 *	vdev: the virtio_device
 *	Returns the first 32 feature bits (all we currently need).
 * @finalize_features: confirm what device features we'll be using.
 *	vdev: the virtio_device
 *	This gives the final feature bits for the device: it can change
 *	the dev->feature bits if it wants.
 */
typedef void vq_callback_t(struct virtqueue *);
struct virtio_config_ops {
	void (*get)(struct virtio_device *vdev, unsigned offset,
		    void *buf, unsigned len);
	void (*set)(struct virtio_device *vdev, unsigned offset,
		    const void *buf, unsigned len);
	u8 (*get_status)(struct virtio_device *vdev);
	void (*set_status)(struct virtio_device *vdev, u8 status);
	void (*reset)(struct virtio_device *vdev);
	int (*find_vqs)(struct virtio_device *, unsigned nvqs,
			struct virtqueue *vqs[],
			vq_callback_t *callbacks[],
			const char *names[]);
	void (*del_vqs)(struct virtio_device *);
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
	else
		BUG_ON(fbit >= 32);

	if (fbit < VIRTIO_TRANSPORT_F_START)
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

#define virtio_config_val_len(vdev, fbit, offset, v, len) \
	virtio_config_buf((vdev), (fbit), (offset), (v), (len))

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

static inline
struct virtqueue *virtio_find_single_vq(struct virtio_device *vdev,
					vq_callback_t *c, const char *n)
{
	vq_callback_t *callbacks[] = { c };
	const char *names[] = { n };
	struct virtqueue *vq;
	int err = vdev->config->find_vqs(vdev, 1, &vq, callbacks, names);
	if (err < 0)
		return ERR_PTR(err);
	return vq;
}
#endif /* __KERNEL__ */
#endif /* _LINUX_VIRTIO_CONFIG_H */
