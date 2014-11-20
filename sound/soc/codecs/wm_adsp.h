/*
 * wm_adsp.h  --  Wolfson ADSP support
 *
 * Copyright 2012 Wolfson Microelectronics plc
 *
 * Author: Mark Brown <broonie@opensource.wolfsonmicro.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#ifndef __WM_ADSP_H
#define __WM_ADSP_H

#include <sound/soc.h>
#include <sound/soc-dapm.h>

#include "wmfw.h"

struct regulator;

struct wm_adsp_region {
	int type;
	unsigned int base;
};

struct wm_adsp_alg_region {
	struct list_head list;
	unsigned int alg;
	int type;
	unsigned int base;
	size_t len;
};

struct wm_adsp {
	const char *part;
	int num;
	int type;
	struct device *dev;
	struct regmap *regmap;
	struct snd_soc_card *card;

	int base;
	int sysclk_reg;
	int sysclk_mask;
	int sysclk_shift;

	struct list_head alg_regions;

	int fw_id;

	const struct wm_adsp_region *mem;
	int num_mems;

	int fw;
	bool running;

	struct regulator *dvfs;

	struct list_head ctl_list;

	struct work_struct boot_work;
};

#define WM_ADSP1(wname, num) \
	SND_SOC_DAPM_PGA_E(wname, SND_SOC_NOPM, num, 0, NULL, 0, \
		wm_adsp1_event, SND_SOC_DAPM_POST_PMU | SND_SOC_DAPM_PRE_PMD)

#define WM_ADSP2(wname, num) \
{	.id = snd_soc_dapm_dai_link, .name = wname " Preloader", \
	.reg = SND_SOC_NOPM, .shift = num, .event = wm_adsp2_early_event, \
	.event_flags = SND_SOC_DAPM_PRE_PMU }, \
{	.id = snd_soc_dapm_out_drv, .name = wname, \
	.reg = SND_SOC_NOPM, .shift = num, .event = wm_adsp2_event, \
	.event_flags = SND_SOC_DAPM_POST_PMU | SND_SOC_DAPM_PRE_PMD }

extern const struct snd_kcontrol_new wm_adsp1_fw_controls[];
extern const struct snd_kcontrol_new wm_adsp2_fw_controls[];

int wm_adsp1_init(struct wm_adsp *adsp);
int wm_adsp2_init(struct wm_adsp *adsp, bool dvfs);
int wm_adsp1_event(struct snd_soc_dapm_widget *w,
		   struct snd_kcontrol *kcontrol, int event);
int wm_adsp2_early_event(struct snd_soc_dapm_widget *w,
			 struct snd_kcontrol *kcontrol, int event);
int wm_adsp2_event(struct snd_soc_dapm_widget *w,
		   struct snd_kcontrol *kcontrol, int event);

#endif
