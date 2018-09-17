/* SPDX-License-Identifier: (GPL-2.0 OR BSD-3-Clause) */
/*
 * This file is provided under a dual BSD/GPLv2 license.  When using or
 * redistributing this file, you may do so under either license.
 *
 * Copyright(c) 2018 Intel Corporation. All rights reserved.
 *
 * Author: Libin Yang <libin.yang@intel.com>
 */

#ifndef __SOUND_SOC_SOF_VIRTIO_BE_H
#define __SOUND_SOC_SOF_VIRTIO_BE_H

#include <linux/vbs/vbs.h>

struct sof_vbe;

/* Virtio Backend */
struct sof_vbe {
	struct snd_sof_dev *sdev;

	struct virtio_dev_info dev_info;
	struct virtio_vq_info vqs[SOF_VIRTIO_NUM_OF_VQS];

	int vm_id;	/* vm id number */

	/* the comp_ids for this vm audio */
	int comp_id_begin;
	int comp_id_end;

	spinlock_t posn_lock; /* lock for position update */

	struct list_head client_list;
	struct list_head posn_list;

	struct list_head list;
};

struct sof_vbe_client {
	struct sof_vbe *vbe;
	int vhm_client_id;
	int max_vcpu;
	struct vhm_request *req_buf;
	struct list_head list;
};

struct snd_sof_dev *sof_virtio_get_sof(void);
int sof_vbe_register(struct snd_sof_dev *sdev, struct sof_vbe **svbe);
int sof_vbe_register_client(struct sof_vbe *vbe);

#endif	/* __SOUND_SOC_SOF_VIRTIO_BE_H */
