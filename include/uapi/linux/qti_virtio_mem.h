/* SPDX-License-Identifier: GPL-2.0-only WITH Linux-syscall-note */
/*
 * Copyright (c) 2022 Qualcomm Innovation Center, Inc. All rights reserved.
 */

#ifndef _UAPI_LINUX_QTI_VIRTIO_MEM_H
#define _UAPI_LINUX_QTI_VIRTIO_MEM_H

#include <linux/ioctl.h>
#include <linux/types.h>

#define QTI_VIRTIO_MEM_IOC_MAGIC 'M'

#define QTI_VIRTIO_MEM_IOC_MAX_NAME_LEN 128
struct qti_virtio_mem_ioc_hint_create_arg {
	char name[QTI_VIRTIO_MEM_IOC_MAX_NAME_LEN];
	__s64 size;
	__u32 fd;
	__u32 reserved0;
	__u64 reserved1;
};

#define QTI_VIRTIO_MEM_IOC_HINT_CREATE				\
	_IOWR(QTI_VIRTIO_MEM_IOC_MAGIC, 0,			\
		struct qti_virtio_mem_ioc_hint_create_arg)


#endif /* _UAPI_LINUX_QTI_VIRTIO_MEM_H */
