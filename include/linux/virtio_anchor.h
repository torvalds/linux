/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _LINUX_VIRTIO_ANCHOR_H
#define _LINUX_VIRTIO_ANCHOR_H

#ifdef CONFIG_VIRTIO_ANCHOR
struct virtio_device;

bool virtio_require_restricted_mem_acc(struct virtio_device *dev);
extern bool (*virtio_check_mem_acc_cb)(struct virtio_device *dev);

static inline void virtio_set_mem_acc_cb(bool (*func)(struct virtio_device *))
{
	virtio_check_mem_acc_cb = func;
}
#else
#define virtio_set_mem_acc_cb(func) do { } while (0)
#endif

#endif /* _LINUX_VIRTIO_ANCHOR_H */
