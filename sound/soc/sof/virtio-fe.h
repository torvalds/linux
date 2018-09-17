/* SPDX-License-Identifier: (GPL-2.0 OR BSD-3-Clause) */
/*
 * This file is provided under a dual BSD/GPLv2 license.  When using or
 * redistributing this file, you may do so under either license.
 *
 * Copyright(c) 2018 Intel Corporation. All rights reserved.
 *
 * Author: Libin Yang <libin.yang@intel.com>
 */

#ifndef __SOUND_SOC_SOF_VIRTIO_FE_H
#define __SOUND_SOC_SOF_VIRTIO_FE_H

#include <linux/virtio.h>
#include <linux/virtio_audio.h>
#include <linux/virtio_ids.h>
#include <linux/virtio_config.h>
#include <linux/virtio_types.h>
#include <uapi/linux/virtio_ring.h>

struct sof_virtio_priv {
	struct snd_sof_pdata *sof_pdata;
	struct platform_device *pdev_pcm;
};

struct snd_sof_dsp_ops snd_sof_vfe_ops;

#endif	/* __SOUND_SOC_SOF_VIRTIO_FE_H */
