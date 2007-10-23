#ifndef _LINUX_VIRTIO_RING_H
#define _LINUX_VIRTIO_RING_H
/* An interface for efficient virtio implementation, currently for use by KVM
 * and lguest, but hopefully others soon.  Do NOT change this since it will
 * break existing servers and clients.
 *
 * This header is BSD licensed so anyone can use the definitions to implement
 * compatible drivers/servers.
 *
 * Copyright Rusty Russell IBM Corporation 2007. */
#include <linux/types.h>

/* This marks a buffer as continuing via the next field. */
#define VRING_DESC_F_NEXT	1
/* This marks a buffer as write-only (otherwise read-only). */
#define VRING_DESC_F_WRITE	2

/* This means don't notify other side when buffer added. */
#define VRING_USED_F_NO_NOTIFY	1
/* This means don't interrupt guest when buffer consumed. */
#define VRING_AVAIL_F_NO_INTERRUPT	1

/* Virtio ring descriptors: 16 bytes.  These can chain together via "next". */
struct vring_desc
{
	/* Address (guest-physical). */
	__u64 addr;
	/* Length. */
	__u32 len;
	/* The flags as indicated above. */
	__u16 flags;
	/* We chain unused descriptors via this, too */
	__u16 next;
};

struct vring_avail
{
	__u16 flags;
	__u16 idx;
	__u16 ring[];
};

/* u32 is used here for ids for padding reasons. */
struct vring_used_elem
{
	/* Index of start of used descriptor chain. */
	__u32 id;
	/* Total length of the descriptor chain which was used (written to) */
	__u32 len;
};

struct vring_used
{
	__u16 flags;
	__u16 idx;
	struct vring_used_elem ring[];
};

struct vring {
	unsigned int num;

	struct vring_desc *desc;

	struct vring_avail *avail;

	struct vring_used *used;
};

/* The standard layout for the ring is a continuous chunk of memory which looks
 * like this.  The used fields will be aligned to a "num+1" boundary.
 *
 * struct vring
 * {
 *	// The actual descriptors (16 bytes each)
 *	struct vring_desc desc[num];
 *
 *	// A ring of available descriptor heads with free-running index.
 *	__u16 avail_flags;
 *	__u16 avail_idx;
 *	__u16 available[num];
 *
 *	// Padding so a correctly-chosen num value will cache-align used_idx.
 *	char pad[sizeof(struct vring_desc) - sizeof(avail_flags)];
 *
 *	// A ring of used descriptor heads with free-running index.
 *	__u16 used_flags;
 *	__u16 used_idx;
 *	struct vring_used_elem used[num];
 * };
 */
static inline void vring_init(struct vring *vr, unsigned int num, void *p)
{
	vr->num = num;
	vr->desc = p;
	vr->avail = p + num*sizeof(struct vring);
	vr->used = p + (num+1)*(sizeof(struct vring) + sizeof(__u16));
}

static inline unsigned vring_size(unsigned int num)
{
	return (num + 1) * (sizeof(struct vring_desc) + sizeof(__u16))
		+ sizeof(__u32) + num * sizeof(struct vring_used_elem);
}

#ifdef __KERNEL__
#include <linux/irqreturn.h>
struct virtio_device;
struct virtqueue;

struct virtqueue *vring_new_virtqueue(unsigned int num,
				      struct virtio_device *vdev,
				      void *pages,
				      void (*notify)(struct virtqueue *vq),
				      bool (*callback)(struct virtqueue *vq));
void vring_del_virtqueue(struct virtqueue *vq);

irqreturn_t vring_interrupt(int irq, void *_vq);
#endif /* __KERNEL__ */
#endif /* _LINUX_VIRTIO_RING_H */
