/* SPDX-License-Identifier: (GPL-2.0-only OR BSD-3-Clause) */
/*
 * This file is provided under a dual BSD/GPLv2 license.  When using or
 * redistributing this file, you may do so under either license.
 *
 * Copyright(c) 2019-2020 Intel Corporation. All rights reserved.
 *
 * Author: Cezary Rojewski <cezary.rojewski@intel.com>
 */

#ifndef __SOF_PROBE_H
#define __SOF_PROBE_H

#include <sound/sof/header.h>

struct snd_sof_dev;

#define SOF_PROBE_INVALID_NODE_ID UINT_MAX

struct sof_probe_dma {
	unsigned int stream_tag;
	unsigned int dma_buffer_size;
} __packed;

enum sof_connection_purpose {
	SOF_CONNECTION_PURPOSE_EXTRACT = 1,
	SOF_CONNECTION_PURPOSE_INJECT,
};

struct sof_probe_point_desc {
	unsigned int buffer_id;
	unsigned int purpose;
	unsigned int stream_tag;
} __packed;

struct sof_ipc_probe_dma_add_params {
	struct sof_ipc_cmd_hdr hdr;
	unsigned int num_elems;
	struct sof_probe_dma dma[];
} __packed;

struct sof_ipc_probe_info_params {
	struct sof_ipc_reply rhdr;
	unsigned int num_elems;
	union {
		struct sof_probe_dma dma[0];
		struct sof_probe_point_desc desc[0];
	};
} __packed;

struct sof_ipc_probe_dma_remove_params {
	struct sof_ipc_cmd_hdr hdr;
	unsigned int num_elems;
	unsigned int stream_tag[];
} __packed;

struct sof_ipc_probe_point_add_params {
	struct sof_ipc_cmd_hdr hdr;
	unsigned int num_elems;
	struct sof_probe_point_desc desc[];
} __packed;

struct sof_ipc_probe_point_remove_params {
	struct sof_ipc_cmd_hdr hdr;
	unsigned int num_elems;
	unsigned int buffer_id[];
} __packed;

int sof_ipc_probe_init(struct snd_sof_dev *sdev,
		u32 stream_tag, size_t buffer_size);
int sof_ipc_probe_deinit(struct snd_sof_dev *sdev);
int sof_ipc_probe_dma_info(struct snd_sof_dev *sdev,
		struct sof_probe_dma **dma, size_t *num_dma);
int sof_ipc_probe_dma_add(struct snd_sof_dev *sdev,
		struct sof_probe_dma *dma, size_t num_dma);
int sof_ipc_probe_dma_remove(struct snd_sof_dev *sdev,
		unsigned int *stream_tag, size_t num_stream_tag);
int sof_ipc_probe_points_info(struct snd_sof_dev *sdev,
		struct sof_probe_point_desc **desc, size_t *num_desc);
int sof_ipc_probe_points_add(struct snd_sof_dev *sdev,
		struct sof_probe_point_desc *desc, size_t num_desc);
int sof_ipc_probe_points_remove(struct snd_sof_dev *sdev,
		unsigned int *buffer_id, size_t num_buffer_id);

#endif
