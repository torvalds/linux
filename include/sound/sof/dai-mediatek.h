/* SPDX-License-Identifier: (GPL-2.0-only OR BSD-3-Clause) */
/*
 * Copyright(c) 2021 Mediatek Corporation. All rights reserved.
 *
 * Author: Bo Pan <bo.pan@mediatek.com>
 */

#ifndef __INCLUDE_SOUND_SOF_DAI_MEDIATEK_H__
#define __INCLUDE_SOUND_SOF_DAI_MEDIATEK_H__

#include <sound/sof/header.h>

struct sof_ipc_dai_mtk_afe_params {
	struct sof_ipc_hdr hdr;
	u32 channels;
	u32 rate;
	u32 format;
	u32 stream_id;
	u32 reserved[4]; /* reserve for future */
} __packed;

#endif

