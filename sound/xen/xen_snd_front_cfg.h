/* SPDX-License-Identifier: GPL-2.0 OR MIT */

/*
 * Xen para-virtual sound device
 *
 * Copyright (C) 2016-2018 EPAM Systems Inc.
 *
 * Author: Oleksandr Andrushchenko <oleksandr_andrushchenko@epam.com>
 */

#ifndef __XEN_SND_FRONT_CFG_H
#define __XEN_SND_FRONT_CFG_H

#include <sound/core.h>
#include <sound/pcm.h>

struct xen_snd_front_info;

struct xen_front_cfg_stream {
	int index;
	char *xenstore_path;
	struct snd_pcm_hardware pcm_hw;
};

struct xen_front_cfg_pcm_instance {
	char name[80];
	int device_id;
	struct snd_pcm_hardware pcm_hw;
	int  num_streams_pb;
	struct xen_front_cfg_stream *streams_pb;
	int  num_streams_cap;
	struct xen_front_cfg_stream *streams_cap;
};

struct xen_front_cfg_card {
	char name_short[32];
	char name_long[80];
	struct snd_pcm_hardware pcm_hw;
	int num_pcm_instances;
	struct xen_front_cfg_pcm_instance *pcm_instances;
};

int xen_snd_front_cfg_card(struct xen_snd_front_info *front_info,
			   int *stream_cnt);

#endif /* __XEN_SND_FRONT_CFG_H */
