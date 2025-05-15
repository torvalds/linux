/* SPDX-License-Identifier: GPL-2.0-only
 *  Copyright (c) 2024 Advanced Micro Devices, Inc. All rights reserved
 */

/*
 *  soc_amd_sdw_common.h - prototypes for common helpers
 */

#ifndef SOC_AMD_SDW_COMMON_H
#define SOC_AMD_SDW_COMMON_H

#include <linux/bits.h>
#include <linux/types.h>
#include <sound/soc.h>
#include <sound/soc_sdw_utils.h>

#define ACP63_SDW_MAX_CPU_DAIS		8
#define ACP63_SDW_MAX_LINKS		2

#define AMD_SDW_MAX_GROUPS		9
#define ACP63_PCI_REV			0x63
#define ACP70_PCI_REV			0x70
#define ACP71_PCI_REV			0x71
#define SOC_JACK_JDSRC(quirk)		((quirk) & GENMASK(3, 0))
#define ASOC_SDW_FOUR_SPK		BIT(4)
#define ASOC_SDW_ACP_DMIC		BIT(5)
#define ASOC_SDW_CODEC_SPKR		BIT(15)

#define AMD_SDW0	0
#define AMD_SDW1	1
#define ACP63_SW0_AUDIO0_TX	0
#define ACP63_SW0_AUDIO1_TX	1
#define ACP63_SW0_AUDIO2_TX	2

#define ACP63_SW0_AUDIO0_RX	3
#define ACP63_SW0_AUDIO1_RX	4
#define ACP63_SW0_AUDIO2_RX	5

#define ACP63_SW1_AUDIO0_TX	0
#define ACP63_SW1_AUDIO0_RX	1

#define ACP_DMIC_BE_ID		4

#define ACP70_SW_AUDIO0_TX	0
#define ACP70_SW_AUDIO1_TX	1
#define ACP70_SW_AUDIO2_TX	2

#define ACP70_SW_AUDIO0_RX	3
#define ACP70_SW_AUDIO1_RX	4
#define ACP70_SW_AUDIO2_RX	5

struct amd_mc_ctx {
	unsigned int acp_rev;
	unsigned int max_sdw_links;
};

int get_acp63_cpu_pin_id(u32 sdw_link_id, int be_id, int *cpu_pin_id, struct device *dev);
int get_acp70_cpu_pin_id(u32 sdw_link_id, int be_id, int *cpu_pin_id, struct device *dev);

#endif
