/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * rt766-sdca.h -- RT766 SDCA ALSA SoC audio driver header
 *
 * Copyright(c) 2026 Realtek Semiconductor Corp.
 */

#ifndef __RT766_H__
#define __RT766_H__

#include <linux/hid.h>
#include <linux/pm.h>
#include <linux/regmap.h>
#include <linux/soundwire/sdw.h>
#include <linux/soundwire/sdw_type.h>
#include <sound/soc.h>
#include <linux/workqueue.h>

struct  rt766_sdca_priv {
	struct regmap *regmap;
	struct snd_soc_component *component;
	struct sdw_slave *slave;
	bool hw_init;
	bool first_hw_init;
	struct snd_soc_jack *hs_jack;
	struct mutex disable_irq_lock; /* SDCA irq lock protection */
	bool disable_irq;
	int jack_type;
	bool fu41_dapm_mute;
	bool fu41_mixer_l_mute;
	bool fu41_mixer_r_mute;
	bool fu113_dapm_mute;
	bool fu113_mixer_mute[4];
	bool fu21_dapm_mute;
	bool fu21_mixer_l_mute;
	bool fu21_mixer_r_mute;
	bool fu36_dapm_mute;
	bool fu36_mixer_l_mute;
	bool fu36_mixer_r_mute;
	struct sdca_function_data *uaj_func_data;
	struct sdca_function_data *sm_func_data;
	struct sdca_function_data *sa_func_data;
	struct sdca_function_data *hid_func_data;
	struct sdca_interrupt_info *irq_info;
	struct hid_device *hid;
};

/* vendor registers */
#define RT766_VERSION_ID	0xc404
#define RT766_DEV_ID1		0xc405
#define RT766_DEV_ID0		0xc406
#define RT766_BOND_LATCH_ID	0xc407

#define RT766_HP_POWER_STATE		0x1000004
#define RT766_HP_FSM_CTL2_1		0x100000d

/* MCU Patch address */
#define RT766_MCU_PATCH_ADDR1_START	0x10010000
#define RT766_MCU_PATCH_ADDR1_END	0x10011fff
#define RT766_MCU_PATCH_ADDR2_START	0x10020000
#define RT766_MCU_PATCH_ADDR2_END	0x10023fff

/* Buffer address for HID */
#define RT766_BUF_ADDR_HID1	0x44030000
#define RT766_BUF_ADDR_HID2	0x44030020

/* SDCA (Channel) */
#define RT766_CH_1	0x01
#define RT766_CH_2	0x02
#define RT766_CH_3	0x03
#define RT766_CH_4	0x04

/* RT766 SDCA Control - function number */
#define RT766_FUNC_NUM_UAJ 0x01
#define RT766_FUNC_NUM_MIC 0x02
#define RT766_FUNC_NUM_HID 0x03
#define RT766_FUNC_NUM_AMP 0x04

/* RT766 SDCA entity */
#define RT766_SDCA_ENT_0		0x00
#define RT766_SDCA_ENT_HID101		0x01
#define RT766_SDCA_ENT_GE49		0x49
#define RT766_SDCA_ENT_USER_FU41	0x05
#define RT766_SDCA_ENT_USER_FU36	0x0f
#define RT766_SDCA_ENT_USER_FU21	0x03
#define RT766_SDCA_ENT_USER_FU113	0x30
#define RT766_SDCA_ENT_PDE23		0x33
#define RT766_SDCA_ENT_PDE47		0x28
#define RT766_SDCA_ENT_PDE11		0x2a
#define RT766_SDCA_ENT_PDE34		0x29
#define RT766_SDCA_ENT_CS41		0x01
#define RT766_SDCA_ENT_CS36		0x11
#define RT766_SDCA_ENT_CS113		0x12
#define RT766_SDCA_ENT_CS21		0x21
#define RT766_SDCA_ENT_PLATFORM_FU33	0x44
#define RT766_SDCA_ENT_PPU21		0x04

/* sample frequency index */
#define RT766_SDCA_RATE_44100HZ		0x08
#define RT766_SDCA_RATE_48000HZ		0x09
#define RT766_SDCA_RATE_96000HZ		0x0b
#define RT766_SDCA_RATE_192000HZ	0x0d

/* SDCA Register macros */
#define RT766_MUTE_REG(func, fu, ch) \
	SDW_SDCA_CTL(RT766_FUNC_NUM_##func, RT766_SDCA_ENT_##fu, SDCA_CTL_FU_MUTE, RT766_CH_##ch)

#define RT766_VOLUME_REG(func, fu, ch) \
	SDW_SDCA_CTL(RT766_FUNC_NUM_##func, RT766_SDCA_ENT_##fu, SDCA_CTL_FU_CHANNEL_VOLUME, RT766_CH_##ch)

#define RT766_GAIN_REG(func, fu, ch) \
	SDW_SDCA_CTL(RT766_FUNC_NUM_##func, RT766_SDCA_ENT_##fu, SDCA_CTL_FU_GAIN, RT766_CH_##ch)

#define RT766_PDE_REQ_REG(func, pde) \
	SDW_SDCA_CTL(RT766_FUNC_NUM_##func, RT766_SDCA_ENT_##pde, SDCA_CTL_PDE_REQUESTED_PS, 0)

#define RT766_PDE_ACTUAL_REG(func, pde) \
	SDW_SDCA_CTL(RT766_FUNC_NUM_##func, RT766_SDCA_ENT_##pde, SDCA_CTL_PDE_ACTUAL_PS, 0)

#define RT766_FUNC_STATUS_REG(func) \
	SDW_SDCA_CTL(RT766_FUNC_NUM_##func, RT766_SDCA_ENT_0, SDCA_CTL_ENTITY_0_FUNCTION_STATUS, 0)

#define RT766_SDCA_CTL(func, ent, ctl) \
	SDW_SDCA_CTL(RT766_FUNC_NUM_##func, RT766_SDCA_ENT_##ent, ctl, 0)

enum {
	RT766_AIF1,
	RT766_AIF2,
	RT766_AIF3,
};

enum {
	RT766_DAI_UAJ,
	RT766_DAI_AMP,
	RT766_DAI_MIC,
};

int rt766_sdca_io_init(struct device *dev, struct sdw_slave *slave);
int rt766_sdca_init(struct device *dev, struct regmap *regmap, struct sdw_slave *slave);
#endif /* __RT766_H__ */
