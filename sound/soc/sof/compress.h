/* SPDX-License-Identifier: (GPL-2.0-only OR BSD-3-Clause) */
/*
 * This file is provided under a dual BSD/GPLv2 license.  When using or
 * redistributing this file, you may do so under either license.
 *
 * Copyright(c) 2019-2020 Intel Corporation. All rights reserved.
 *
 * Author: Cezary Rojewski <cezary.rojewski@intel.com>
 */

#ifndef __SOF_COMPRESS_H
#define __SOF_COMPRESS_H

#include <sound/compress_driver.h>

extern struct snd_compress_ops sof_probe_compressed_ops;

int sof_probe_compr_open(struct snd_compr_stream *cstream,
		struct snd_soc_dai *dai);
int sof_probe_compr_free(struct snd_compr_stream *cstream,
		struct snd_soc_dai *dai);
int sof_probe_compr_set_params(struct snd_compr_stream *cstream,
		struct snd_compr_params *params, struct snd_soc_dai *dai);
int sof_probe_compr_trigger(struct snd_compr_stream *cstream, int cmd,
		struct snd_soc_dai *dai);
int sof_probe_compr_pointer(struct snd_compr_stream *cstream,
		struct snd_compr_tstamp *tstamp, struct snd_soc_dai *dai);
int sof_probe_compr_copy(struct snd_soc_component *component,
			 struct snd_compr_stream *cstream,
			 char __user *buf, size_t count);

#endif
