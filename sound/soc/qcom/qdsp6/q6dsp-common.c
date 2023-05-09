// SPDX-License-Identifier: GPL-2.0
// Copyright (c) 2011-2017, The Linux Foundation. All rights reserved.
// Copyright (c) 2018, Linaro Limited

#include "q6dsp-common.h"
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/string.h>
#include <linux/errno.h>

int q6dsp_map_channels(u8 ch_map[PCM_MAX_NUM_CHANNEL], int ch)
{
	memset(ch_map, 0, PCM_MAX_NUM_CHANNEL);

	switch (ch) {
	case 1:
		ch_map[0] = PCM_CHANNEL_FC;
		break;
	case 2:
		ch_map[0] = PCM_CHANNEL_FL;
		ch_map[1] = PCM_CHANNEL_FR;
		break;
	case 3:
		ch_map[0] = PCM_CHANNEL_FL;
		ch_map[1] = PCM_CHANNEL_FR;
		ch_map[2] = PCM_CHANNEL_FC;
		break;
	case 4:
		ch_map[0] = PCM_CHANNEL_FL;
		ch_map[1] = PCM_CHANNEL_FR;
		ch_map[2] = PCM_CHANNEL_LS;
		ch_map[3] = PCM_CHANNEL_RS;
		break;
	case 5:
		ch_map[0] = PCM_CHANNEL_FL;
		ch_map[1] = PCM_CHANNEL_FR;
		ch_map[2] = PCM_CHANNEL_FC;
		ch_map[3] = PCM_CHANNEL_LS;
		ch_map[4] = PCM_CHANNEL_RS;
		break;
	case 6:
		ch_map[0] = PCM_CHANNEL_FL;
		ch_map[1] = PCM_CHANNEL_FR;
		ch_map[2] = PCM_CHANNEL_LFE;
		ch_map[3] = PCM_CHANNEL_FC;
		ch_map[4] = PCM_CHANNEL_LS;
		ch_map[5] = PCM_CHANNEL_RS;
		break;
	case 8:
		ch_map[0] = PCM_CHANNEL_FL;
		ch_map[1] = PCM_CHANNEL_FR;
		ch_map[2] = PCM_CHANNEL_LFE;
		ch_map[3] = PCM_CHANNEL_FC;
		ch_map[4] = PCM_CHANNEL_LS;
		ch_map[5] = PCM_CHANNEL_RS;
		ch_map[6] = PCM_CHANNEL_LB;
		ch_map[7] = PCM_CHANNEL_RB;
		break;
	default:
		return -EINVAL;
	}

	return 0;
}
EXPORT_SYMBOL_GPL(q6dsp_map_channels);

int q6dsp_get_channel_allocation(int channels)
{
	int channel_allocation;

	/* HDMI spec CEA-861-E: Table 28 Audio InfoFrame Data Byte 4 */
	switch (channels) {
	case 2:
		channel_allocation = 0;
		break;
	case 3:
		channel_allocation = 0x02;
		break;
	case 4:
		channel_allocation = 0x06;
		break;
	case 5:
		channel_allocation = 0x0A;
		break;
	case 6:
		channel_allocation = 0x0B;
		break;
	case 7:
		channel_allocation = 0x12;
		break;
	case 8:
		channel_allocation = 0x13;
		break;
	default:
		return -EINVAL;
	}

	return channel_allocation;
}
EXPORT_SYMBOL_GPL(q6dsp_get_channel_allocation);
MODULE_LICENSE("GPL v2");
