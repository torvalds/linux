#ifndef _LINUX_VIRTIO_RING_H
#define _LINUX_VIRTIO_RING_H

#include <asm/barrier.h>
#include <linux/irqreturn.h>
#include <uapi/linux/virtio_ring.h>

/*
 * Barriers in virtio are tricky.  Non-SMP virtio guests can't assume
 * they're not on an SMP host system, so they need to assume real
 * barriers.  Non-SMP virtio hosts could skip the barriers, but does
 * anyone care?
 *
 * For virtio_pci on SMP, we don't need to order with respect to MMIO
 * accesses through relaxed memory I/O windows, so virt_mb() et al are
 * sufficient.
 *
 * For using virtio to talk to real devices (eg. other heterogeneous
 * CPUs) we do need real barriers.  In theory, we could be using both
 * kinds of virtio, so it's a runtime decision, and the branch is
 * actually quite cheap.
 */

static inline void virtio_mb(bool weak_barriers)
{
	if (weak_barriers)
		virt_mb();
	else
		mb();
}

static inline void virtio_rmb(bool weak_barriers)
{
	if (weak_barriers)
		virt_rmb();
	else
		rmb();
}

static inline void virtio_wmb(bool weak_barriers)
{
	if (weak_barriers)
		virt_wmb();
	else
		wmb();
}

static inline void virtio_store_mb(bool weak_barriers,
				   __virtio16 *p, __virtio16 v)
{
	if (weak_barriers) {
		virt_store_mb(*p, v);
	} else {
		WRITE_ONCE(*p, v);
		mb();
	}
}

struct virtio_device;
struct virtqueue;

struct virtqueue *vring_new_virtqueue(unsigned int index,
				      unsigned int num,
				      unsigned int vring_align,
				      struct virtio_device *vdev,
				      bool weak_barriers,
				      void *pages,
				      bool (*notify)(struct virtqueue *vq),
				      void (*callback)(struct virtqueue *vq),
				      const char *name);
void vring_del_virtqueue(struct virtqueue *vq);
/* Filter out transport-specific feature bits. */
void vring_transport_features(struct virtio_device *vdev);

irqreturn_t vring_interrupt(int irq, void *_vq);
#endif /* _LINUX_VIRTIO_RING_H */
