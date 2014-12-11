#ifndef _LINUX_VIRTIO_CONFIG_H
#define _LINUX_VIRTIO_CONFIG_H

#include <linux/err.h>
#include <linux/bug.h>
#include <linux/virtio.h>
#include <linux/virtio_byteorder.h>
#include <uapi/linux/virtio_config.h>

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
 *	Device must not be reset from its vq/config callbacks, or in
 *	parallel with being added/removed.
 * @find_vqs: find virtqueues and instantiate them.
 *	vdev: the virtio_device
 *	nvqs: the number of virtqueues to find
 *	vqs: on success, includes new virtqueues
 *	callbacks: array of callbacks, for each virtqueue
 *		include a NULL entry for vqs that do not need a callback
 *	names: array of virtqueue names (mainly for debugging)
 *		include a NULL entry for vqs unused by driver
 *	Returns 0 on success or error status
 * @del_vqs: free virtqueues found by find_vqs().
 * @get_features: get the array of feature bits for this device.
 *	vdev: the virtio_device
 *	Returns the first 32 feature bits (all we currently need).
 * @finalize_features: confirm what device features we'll be using.
 *	vdev: the virtio_device
 *	This gives the final feature bits for the device: it can change
 *	the dev->feature bits if it wants.
 *	Returns 0 on success or error status
 * @bus_name: return the bus name associated with the device
 *	vdev: the virtio_device
 *      This returns a pointer to the bus name a la pci_name from which
 *      the caller can then copy.
 * @set_vq_affinity: set the affinity for a virtqueue.
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
	u64 (*get_features)(struct virtio_device *vdev);
	int (*finalize_features)(struct virtio_device *vdev);
	const char *(*bus_name)(struct virtio_device *vdev);
	int (*set_vq_affinity)(struct virtqueue *vq, int cpu);
};

/* If driver didn't advertise the feature, it will never appear. */
void virtio_check_driver_offered_feature(const struct virtio_device *vdev,
					 unsigned int fbit);

/**
 * __virtio_test_bit - helper to test feature bits. For use by transports.
 *                     Devices should normally use virtio_has_feature,
 *                     which includes more checks.
 * @vdev: the device
 * @fbit: the feature bit
 */
static inline bool __virtio_test_bit(const struct virtio_device *vdev,
				     unsigned int fbit)
{
	/* Did you forget to fix assumptions on max features? */
	if (__builtin_constant_p(fbit))
		BUILD_BUG_ON(fbit >= 64);
	else
		BUG_ON(fbit >= 64);

	return vdev->features & BIT_ULL(fbit);
}

/**
 * __virtio_set_bit - helper to set feature bits. For use by transports.
 * @vdev: the device
 * @fbit: the feature bit
 */
static inline void __virtio_set_bit(struct virtio_device *vdev,
				    unsigned int fbit)
{
	/* Did you forget to fix assumptions on max features? */
	if (__builtin_constant_p(fbit))
		BUILD_BUG_ON(fbit >= 64);
	else
		BUG_ON(fbit >= 64);

	vdev->features |= BIT_ULL(fbit);
}

/**
 * __virtio_clear_bit - helper to clear feature bits. For use by transports.
 * @vdev: the device
 * @fbit: the feature bit
 */
static inline void __virtio_clear_bit(struct virtio_device *vdev,
				      unsigned int fbit)
{
	/* Did you forget to fix assumptions on max features? */
	if (__builtin_constant_p(fbit))
		BUILD_BUG_ON(fbit >= 64);
	else
		BUG_ON(fbit >= 64);

	vdev->features &= ~BIT_ULL(fbit);
}

/**
 * virtio_has_feature - helper to determine if this device has this feature.
 * @vdev: the device
 * @fbit: the feature bit
 */
static inline bool virtio_has_feature(const struct virtio_device *vdev,
				      unsigned int fbit)
{
	if (fbit < VIRTIO_TRANSPORT_F_START)
		virtio_check_driver_offered_feature(vdev, fbit);

	return __virtio_test_bit(vdev, fbit);
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

/**
 * virtio_device_ready - enable vq use in probe function
 * @vdev: the device
 *
 * Driver must call this to use vqs in the probe function.
 *
 * Note: vqs are enabled automatically after probe returns.
 */
static inline
void virtio_device_ready(struct virtio_device *dev)
{
	unsigned status = dev->config->get_status(dev);

	BUG_ON(status & VIRTIO_CONFIG_S_DRIVER_OK);
	dev->config->set_status(dev, status | VIRTIO_CONFIG_S_DRIVER_OK);
}

static inline
const char *virtio_bus_name(struct virtio_device *vdev)
{
	if (!vdev->config->bus_name)
		return "virtio";
	return vdev->config->bus_name(vdev);
}

/**
 * virtqueue_set_affinity - setting affinity for a virtqueue
 * @vq: the virtqueue
 * @cpu: the cpu no.
 *
 * Pay attention the function are best-effort: the affinity hint may not be set
 * due to config support, irq type and sharing.
 *
 */
static inline
int virtqueue_set_affinity(struct virtqueue *vq, int cpu)
{
	struct virtio_device *vdev = vq->vdev;
	if (vdev->config->set_vq_affinity)
		return vdev->config->set_vq_affinity(vq, cpu);
	return 0;
}

/* Memory accessors */
static inline u16 virtio16_to_cpu(struct virtio_device *vdev, __virtio16 val)
{
	return __virtio16_to_cpu(virtio_has_feature(vdev, VIRTIO_F_VERSION_1), val);
}

static inline __virtio16 cpu_to_virtio16(struct virtio_device *vdev, u16 val)
{
	return __cpu_to_virtio16(virtio_has_feature(vdev, VIRTIO_F_VERSION_1), val);
}

static inline u32 virtio32_to_cpu(struct virtio_device *vdev, __virtio32 val)
{
	return __virtio32_to_cpu(virtio_has_feature(vdev, VIRTIO_F_VERSION_1), val);
}

static inline __virtio32 cpu_to_virtio32(struct virtio_device *vdev, u32 val)
{
	return __cpu_to_virtio32(virtio_has_feature(vdev, VIRTIO_F_VERSION_1), val);
}

static inline u64 virtio64_to_cpu(struct virtio_device *vdev, __virtio64 val)
{
	return __virtio64_to_cpu(virtio_has_feature(vdev, VIRTIO_F_VERSION_1), val);
}

static inline __virtio64 cpu_to_virtio64(struct virtio_device *vdev, u64 val)
{
	return __cpu_to_virtio64(virtio_has_feature(vdev, VIRTIO_F_VERSION_1), val);
}

/* Config space accessors. */
#define virtio_cread(vdev, structname, member, ptr)			\
	do {								\
		/* Must match the member's type, and be integer */	\
		if (!typecheck(typeof((((structname*)0)->member)), *(ptr))) \
			(*ptr) = 1;					\
									\
		switch (sizeof(*ptr)) {					\
		case 1:							\
			*(ptr) = virtio_cread8(vdev,			\
					       offsetof(structname, member)); \
			break;						\
		case 2:							\
			*(ptr) = virtio_cread16(vdev,			\
						offsetof(structname, member)); \
			break;						\
		case 4:							\
			*(ptr) = virtio_cread32(vdev,			\
						offsetof(structname, member)); \
			break;						\
		case 8:							\
			*(ptr) = virtio_cread64(vdev,			\
						offsetof(structname, member)); \
			break;						\
		default:						\
			BUG();						\
		}							\
	} while(0)

/* Config space accessors. */
#define virtio_cwrite(vdev, structname, member, ptr)			\
	do {								\
		/* Must match the member's type, and be integer */	\
		if (!typecheck(typeof((((structname*)0)->member)), *(ptr))) \
			BUG_ON((*ptr) == 1);				\
									\
		switch (sizeof(*ptr)) {					\
		case 1:							\
			virtio_cwrite8(vdev,				\
				       offsetof(structname, member),	\
				       *(ptr));				\
			break;						\
		case 2:							\
			virtio_cwrite16(vdev,				\
					offsetof(structname, member),	\
					*(ptr));			\
			break;						\
		case 4:							\
			virtio_cwrite32(vdev,				\
					offsetof(structname, member),	\
					*(ptr));			\
			break;						\
		case 8:							\
			virtio_cwrite64(vdev,				\
					offsetof(structname, member),	\
					*(ptr));			\
			break;						\
		default:						\
			BUG();						\
		}							\
	} while(0)

static inline u8 virtio_cread8(struct virtio_device *vdev, unsigned int offset)
{
	u8 ret;
	vdev->config->get(vdev, offset, &ret, sizeof(ret));
	return ret;
}

static inline void virtio_cread_bytes(struct virtio_device *vdev,
				      unsigned int offset,
				      void *buf, size_t len)
{
	int i;

	for (i = 0; i < len; i++)
		vdev->config->get(vdev, offset + i, buf + i, 1);
}

static inline void virtio_cwrite8(struct virtio_device *vdev,
				  unsigned int offset, u8 val)
{
	vdev->config->set(vdev, offset, &val, sizeof(val));
}

static inline u16 virtio_cread16(struct virtio_device *vdev,
				 unsigned int offset)
{
	u16 ret;
	vdev->config->get(vdev, offset, &ret, sizeof(ret));
	return virtio16_to_cpu(vdev, (__force __virtio16)ret);
}

static inline void virtio_cwrite16(struct virtio_device *vdev,
				   unsigned int offset, u16 val)
{
	val = (__force u16)cpu_to_virtio16(vdev, val);
	vdev->config->set(vdev, offset, &val, sizeof(val));
}

static inline u32 virtio_cread32(struct virtio_device *vdev,
				 unsigned int offset)
{
	u32 ret;
	vdev->config->get(vdev, offset, &ret, sizeof(ret));
	return virtio32_to_cpu(vdev, (__force __virtio32)ret);
}

static inline void virtio_cwrite32(struct virtio_device *vdev,
				   unsigned int offset, u32 val)
{
	val = (__force u32)cpu_to_virtio32(vdev, val);
	vdev->config->set(vdev, offset, &val, sizeof(val));
}

static inline u64 virtio_cread64(struct virtio_device *vdev,
				 unsigned int offset)
{
	u64 ret;
	vdev->config->get(vdev, offset, &ret, sizeof(ret));
	return virtio64_to_cpu(vdev, (__force __virtio64)ret);
}

static inline void virtio_cwrite64(struct virtio_device *vdev,
				   unsigned int offset, u64 val)
{
	val = (__force u64)cpu_to_virtio64(vdev, val);
	vdev->config->set(vdev, offset, &val, sizeof(val));
}

/* Conditional config space accessors. */
#define virtio_cread_feature(vdev, fbit, structname, member, ptr)	\
	({								\
		int _r = 0;						\
		if (!virtio_has_feature(vdev, fbit))			\
			_r = -ENOENT;					\
		else							\
			virtio_cread((vdev), structname, member, ptr);	\
		_r;							\
	})

#endif /* _LINUX_VIRTIO_CONFIG_H */
