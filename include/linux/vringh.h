/*
 * Linux host-side vring helpers; for when the kernel needs to access
 * someone else's vring.
 *
 * Copyright IBM Corporation, 2013.
 * Parts taken from drivers/vhost/vhost.c Copyright 2009 Red Hat, Inc.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA 02111-1307, USA.
 *
 * Written by: Rusty Russell <rusty@rustcorp.com.au>
 */
#ifndef _LINUX_VRINGH_H
#define _LINUX_VRINGH_H
#include <uapi/linux/virtio_ring.h>
#include <linux/virtio_byteorder.h>
#include <linux/uio.h>
#include <linux/slab.h>
#include <asm/barrier.h>

/* virtio_ring with information needed for host access. */
struct vringh {
	/* Everything is little endian */
	bool little_endian;

	/* Guest publishes used event idx (note: we always do). */
	bool event_indices;

	/* Can we get away with weak barriers? */
	bool weak_barriers;

	/* Last available index we saw (ie. where we're up to). */
	u16 last_avail_idx;

	/* Last index we used. */
	u16 last_used_idx;

	/* How many descriptors we've completed since last need_notify(). */
	u32 completed;

	/* The vring (note: it may contain user pointers!) */
	struct vring vring;

	/* The function to call to notify the guest about added buffers */
	void (*notify)(struct vringh *);
};

/**
 * struct vringh_config_ops - ops for creating a host vring from a virtio driver
 * @find_vrhs: find the host vrings and instantiate them
 *	vdev: the virtio_device
 *	nhvrs: the number of host vrings to find
 *	hvrs: on success, includes new host vrings
 *	callbacks: array of driver callbacks, for each host vring
 *		include a NULL entry for vqs that do not need a callback
 *	Returns 0 on success or error status
 * @del_vrhs: free the host vrings found by find_vrhs().
 */
struct virtio_device;
typedef void vrh_callback_t(struct virtio_device *, struct vringh *);
struct vringh_config_ops {
	int (*find_vrhs)(struct virtio_device *vdev, unsigned nhvrs,
			 struct vringh *vrhs[], vrh_callback_t *callbacks[]);
	void (*del_vrhs)(struct virtio_device *vdev);
};

/* The memory the vring can access, and what offset to apply. */
struct vringh_range {
	u64 start, end_incl;
	u64 offset;
};

/**
 * struct vringh_iov - iovec mangler.
 *
 * Mangles iovec in place, and restores it.
 * Remaining data is iov + i, of used - i elements.
 */
struct vringh_iov {
	struct iovec *iov;
	size_t consumed; /* Within iov[i] */
	unsigned i, used, max_num;
};

/**
 * struct vringh_iov - kvec mangler.
 *
 * Mangles kvec in place, and restores it.
 * Remaining data is iov + i, of used - i elements.
 */
struct vringh_kiov {
	struct kvec *iov;
	size_t consumed; /* Within iov[i] */
	unsigned i, used, max_num;
};

/* Flag on max_num to indicate we're kmalloced. */
#define VRINGH_IOV_ALLOCATED 0x8000000

/* Helpers for userspace vrings. */
int vringh_init_user(struct vringh *vrh, u64 features,
		     unsigned int num, bool weak_barriers,
		     struct vring_desc __user *desc,
		     struct vring_avail __user *avail,
		     struct vring_used __user *used);

static inline void vringh_iov_init(struct vringh_iov *iov,
				   struct iovec *iovec, unsigned num)
{
	iov->used = iov->i = 0;
	iov->consumed = 0;
	iov->max_num = num;
	iov->iov = iovec;
}

static inline void vringh_iov_reset(struct vringh_iov *iov)
{
	iov->iov[iov->i].iov_len += iov->consumed;
	iov->iov[iov->i].iov_base -= iov->consumed;
	iov->consumed = 0;
	iov->i = 0;
}

static inline void vringh_iov_cleanup(struct vringh_iov *iov)
{
	if (iov->max_num & VRINGH_IOV_ALLOCATED)
		kfree(iov->iov);
	iov->max_num = iov->used = iov->i = iov->consumed = 0;
	iov->iov = NULL;
}

/* Convert a descriptor into iovecs. */
int vringh_getdesc_user(struct vringh *vrh,
			struct vringh_iov *riov,
			struct vringh_iov *wiov,
			bool (*getrange)(struct vringh *vrh,
					 u64 addr, struct vringh_range *r),
			u16 *head);

/* Copy bytes from readable vsg, consuming it (and incrementing wiov->i). */
ssize_t vringh_iov_pull_user(struct vringh_iov *riov, void *dst, size_t len);

/* Copy bytes into writable vsg, consuming it (and incrementing wiov->i). */
ssize_t vringh_iov_push_user(struct vringh_iov *wiov,
			     const void *src, size_t len);

/* Mark a descriptor as used. */
int vringh_complete_user(struct vringh *vrh, u16 head, u32 len);
int vringh_complete_multi_user(struct vringh *vrh,
			       const struct vring_used_elem used[],
			       unsigned num_used);

/* Pretend we've never seen descriptor (for easy error handling). */
void vringh_abandon_user(struct vringh *vrh, unsigned int num);

/* Do we need to fire the eventfd to notify the other side? */
int vringh_need_notify_user(struct vringh *vrh);

bool vringh_notify_enable_user(struct vringh *vrh);
void vringh_notify_disable_user(struct vringh *vrh);

/* Helpers for kernelspace vrings. */
int vringh_init_kern(struct vringh *vrh, u64 features,
		     unsigned int num, bool weak_barriers,
		     struct vring_desc *desc,
		     struct vring_avail *avail,
		     struct vring_used *used);

static inline void vringh_kiov_init(struct vringh_kiov *kiov,
				    struct kvec *kvec, unsigned num)
{
	kiov->used = kiov->i = 0;
	kiov->consumed = 0;
	kiov->max_num = num;
	kiov->iov = kvec;
}

static inline void vringh_kiov_reset(struct vringh_kiov *kiov)
{
	kiov->iov[kiov->i].iov_len += kiov->consumed;
	kiov->iov[kiov->i].iov_base -= kiov->consumed;
	kiov->consumed = 0;
	kiov->i = 0;
}

static inline void vringh_kiov_cleanup(struct vringh_kiov *kiov)
{
	if (kiov->max_num & VRINGH_IOV_ALLOCATED)
		kfree(kiov->iov);
	kiov->max_num = kiov->used = kiov->i = kiov->consumed = 0;
	kiov->iov = NULL;
}

int vringh_getdesc_kern(struct vringh *vrh,
			struct vringh_kiov *riov,
			struct vringh_kiov *wiov,
			u16 *head,
			gfp_t gfp);

ssize_t vringh_iov_pull_kern(struct vringh_kiov *riov, void *dst, size_t len);
ssize_t vringh_iov_push_kern(struct vringh_kiov *wiov,
			     const void *src, size_t len);
void vringh_abandon_kern(struct vringh *vrh, unsigned int num);
int vringh_complete_kern(struct vringh *vrh, u16 head, u32 len);

bool vringh_notify_enable_kern(struct vringh *vrh);
void vringh_notify_disable_kern(struct vringh *vrh);

int vringh_need_notify_kern(struct vringh *vrh);

/* Notify the guest about buffers added to the used ring */
static inline void vringh_notify(struct vringh *vrh)
{
	if (vrh->notify)
		vrh->notify(vrh);
}

static inline u16 vringh16_to_cpu(const struct vringh *vrh, __virtio16 val)
{
	return __virtio16_to_cpu(vrh->little_endian, val);
}

static inline __virtio16 cpu_to_vringh16(const struct vringh *vrh, u16 val)
{
	return __cpu_to_virtio16(vrh->little_endian, val);
}

static inline u32 vringh32_to_cpu(const struct vringh *vrh, __virtio32 val)
{
	return __virtio32_to_cpu(vrh->little_endian, val);
}

static inline __virtio32 cpu_to_vringh32(const struct vringh *vrh, u32 val)
{
	return __cpu_to_virtio32(vrh->little_endian, val);
}

static inline u64 vringh64_to_cpu(const struct vringh *vrh, __virtio64 val)
{
	return __virtio64_to_cpu(vrh->little_endian, val);
}

static inline __virtio64 cpu_to_vringh64(const struct vringh *vrh, u64 val)
{
	return __cpu_to_virtio64(vrh->little_endian, val);
}
#endif /* _LINUX_VRINGH_H */
