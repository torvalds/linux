// SPDX-License-Identifier: GPL-2.0-only
// Copyright(c) 2024 Advanced Micro Devices, Inc.

/*
 *  acp-sdw-mach-common - Common machine driver helper functions for
 *  legacy(No DSP) stack and SOF stack.
 */

#include <linux/device.h>
#include <linux/module.h>
#include "soc_amd_sdw_common.h"

int get_acp63_cpu_pin_id(u32 sdw_link_id, int be_id, int *cpu_pin_id, struct device *dev)
{
	switch (sdw_link_id) {
	case AMD_SDW0:
		switch (be_id) {
		case SOC_SDW_JACK_OUT_DAI_ID:
			*cpu_pin_id = ACP63_SW0_AUDIO0_TX;
			break;
		case SOC_SDW_JACK_IN_DAI_ID:
			*cpu_pin_id = ACP63_SW0_AUDIO0_RX;
			break;
		case SOC_SDW_AMP_OUT_DAI_ID:
			*cpu_pin_id = ACP63_SW0_AUDIO1_TX;
			break;
		case SOC_SDW_AMP_IN_DAI_ID:
			*cpu_pin_id = ACP63_SW0_AUDIO1_RX;
			break;
		case SOC_SDW_DMIC_DAI_ID:
			*cpu_pin_id = ACP63_SW0_AUDIO2_RX;
			break;
		default:
			dev_err(dev, "Invalid be id:%d\n", be_id);
			return -EINVAL;
		}
		break;
	case AMD_SDW1:
		switch (be_id) {
		case SOC_SDW_JACK_OUT_DAI_ID:
		case SOC_SDW_AMP_OUT_DAI_ID:
			*cpu_pin_id = ACP63_SW1_AUDIO0_TX;
			break;
		case SOC_SDW_JACK_IN_DAI_ID:
		case SOC_SDW_AMP_IN_DAI_ID:
		case SOC_SDW_DMIC_DAI_ID:
			*cpu_pin_id = ACP63_SW1_AUDIO0_RX;
			break;
		default:
			dev_err(dev, "invalid be_id:%d\n", be_id);
			return -EINVAL;
		}
		break;
	default:
		dev_err(dev, "Invalid link id:%d\n", sdw_link_id);
		return -EINVAL;
	}
	return 0;
}
EXPORT_SYMBOL_NS_GPL(get_acp63_cpu_pin_id, SND_SOC_AMD_SDW_MACH);

MODULE_DESCRIPTION("AMD SoundWire Common Machine driver");
MODULE_AUTHOR("Vijendar Mukunda <Vijendar.Mukunda@amd.com>");
MODULE_LICENSE("GPL");
