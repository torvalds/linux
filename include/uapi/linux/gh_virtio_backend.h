/* SPDX-License-Identifier: GPL-2.0-only WITH Linux-syscall-note */
/*
 * Copyright (c) 2020-2021, The Linux Foundation. All rights reserved.
 */

#ifndef _UAPI_LINUX_VIRTIO_BACKEND_H
#define _UAPI_LINUX_VIRTIO_BACKEND_H

#include <linux/virtio_types.h>

#define VIRTIO_BE_IOC_MAGIC	0xBC

#define VBE_ASSIGN_IOEVENTFD	1
#define VBE_DEASSIGN_IOEVENTFD	2

#define VBE_ASSIGN_IRQFD	1
#define VBE_DEASSIGN_IRQFD	2

#define EVENT_NEW_BUFFER	1
#define EVENT_RESET_RQST	2
#define EVENT_INTERRUPT_ACK	4
#define EVENT_DRIVER_OK		8
#define EVENT_DRIVER_FAILED	0x10
#define EVENT_MODULE_EXIT	0x20
#define EVENT_VM_EXIT		0x40
#define EVENT_APP_EXIT		0x100

/*
 *  IOCTLs supported by virtio backend driver
 */
#define GH_GET_SHARED_MEMORY_SIZE	_IOR(VIRTIO_BE_IOC_MAGIC, 1, __u64)
#define GH_IOEVENTFD		_IOW(VIRTIO_BE_IOC_MAGIC, 2, \
						struct virtio_eventfd)
#define GH_IRQFD			_IOW(VIRTIO_BE_IOC_MAGIC, 3, \
						struct virtio_irqfd)
#define GH_WAIT_FOR_EVENT        _IOWR(VIRTIO_BE_IOC_MAGIC, 4, \
						struct virtio_event)
#define GH_SET_DEVICE_FEATURES        _IOW(VIRTIO_BE_IOC_MAGIC, 5, \
						struct virtio_dev_features)
#define GH_SET_QUEUE_NUM_MAX        _IOW(VIRTIO_BE_IOC_MAGIC, 6, \
						struct virtio_queue_max)
#define GH_SET_DEVICE_CONFIG_DATA        _IOW(VIRTIO_BE_IOC_MAGIC, 7, \
						struct virtio_config_data)
#define GH_GET_DRIVER_CONFIG_DATA        _IOWR(VIRTIO_BE_IOC_MAGIC, 8, \
						struct virtio_config_data)
#define GH_GET_QUEUE_INFO        _IOWR(VIRTIO_BE_IOC_MAGIC, 9, \
						struct virtio_queue_info)
#define GH_GET_DRIVER_FEATURES        _IOWR(VIRTIO_BE_IOC_MAGIC, 10, \
						struct virtio_driver_features)
#define GH_ACK_DRIVER_OK        _IOWR(VIRTIO_BE_IOC_MAGIC, 11, __u32)
#define GH_SET_APP_READY	_IO(VIRTIO_BE_IOC_MAGIC, 12)
#define GH_ACK_RESET		_IOW(VIRTIO_BE_IOC_MAGIC, 13, struct virtio_ack_reset)

struct virtio_ack_reset {
	__u32 label;
	__u32 reserved;
};

struct virtio_driver_features {
	__u32 label;
	__u32 reserved;
	__u32 features_sel;
	__u32 features;
};

struct virtio_queue_info {
	__u32 label;
	__u32 queue_sel;
	__u32 queue_num;
	__u32 queue_ready;
	__u64 queue_desc;
	__u64 queue_driver;
	__u64 queue_device;
};

struct virtio_config_data {
	__u32 label;
	__u32 config_size;
	__u64 config_data;
};

struct virtio_dev_features {
	__u32 label;
	__u32 reserved;
	__u32 features_sel;
	__u32 features;
};

struct virtio_queue_max {
	__u32 label;
	__u32 reserved;
	__u32 queue_sel;
	__u32 queue_num_max;
};

struct virtio_event {
	__u32 label;
	__u32 event;
	__u32 event_data;
	__u32 reserved;
};

struct virtio_eventfd {
	__u32 label;
	__u32 flags;
	__u32 queue_num;
	__s32 fd;
};

struct virtio_irqfd {
	__u32 label;
	__u32 flags;
	__s32 fd;
	__u32 reserved;
};

#endif /* _UAPI_LINUX_VIRTIO_BACKEND_H */
