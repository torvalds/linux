/* SPDX-License-Identifier: (GPL-2.0 WITH Linux-syscall-note) OR BSD-3-Clause */
/*
 * Definitions for virtio-pmem devices.
 *
 * Copyright (C) 2019 Red Hat, Inc.
 *
 * Author(s): Pankaj Gupta <pagupta@redhat.com>
 */

#ifndef _UAPI_LINUX_VIRTIO_PMEM_H
#define _UAPI_LINUX_VIRTIO_PMEM_H

#include <linux/types.h>
#include <linux/virtio_ids.h>
#include <linux/virtio_config.h>

/* Feature bits */
/* guest physical address range will be indicated as shared memory region 0 */
#define VIRTIO_PMEM_F_SHMEM_REGION 0

/* shmid of the shared memory region corresponding to the pmem */
#define VIRTIO_PMEM_SHMEM_REGION_ID 0

struct virtio_pmem_config {
	__le64 start;
	__le64 size;
};

#define VIRTIO_PMEM_REQ_TYPE_FLUSH      0

struct virtio_pmem_resp {
	/* Host return status corresponding to flush request */
	__le32 ret;
};

struct virtio_pmem_req {
	/* command type */
	__le32 type;
};

#endif
