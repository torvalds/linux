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
#include <sound/compress_driver.h>

#include "wmfw.h"

/* Return values for wm_adsp_compr_handle_irq */
#define WM_ADSP_COMPR_OK                 0
#define WM_ADSP_COMPR_VOICE_TRIGGER      1

struct wm_adsp_region {
	int type;
	unsigned int base;
};

struct wm_adsp_alg_region {
	struct list_head list;
	unsigned int alg;
	int type;
	unsigned int base;
};

struct wm_adsp_compr;
struct wm_adsp_compr_buf;

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

	unsigned int fw_id;
	unsigned int fw_id_version;

	const struct wm_adsp_region *mem;
	int num_mems;

	int fw;
	int fw_ver;

	bool booted;
	bool running;

	struct list_head ctl_list;

	struct work_struct boot_work;

	struct wm_adsp_compr *compr;
	struct wm_adsp_compr_buf *buffer;

	struct mutex pwr_lock;

#ifdef CONFIG_DEBUG_FS
	struct dentry *debugfs_root;
	char *wmfw_file_name;
	char *bin_file_name;
#endif

};

#define WM_ADSP1(wname, num) \
	SND_SOC_DAPM_PGA_E(wname, SND_SOC_NOPM, num, 0, NULL, 0, \
		wm_adsp1_event, SND_SOC_DAPM_POST_PMU | SND_SOC_DAPM_PRE_PMD)

#define WM_ADSP2(wname, num, event_fn) \
{	.id = snd_soc_dapm_supply, .name = wname " Preloader", \
	.reg = SND_SOC_NOPM, .shift = num, .event = event_fn, \
	.event_flags = SND_SOC_DAPM_PRE_PMU | SND_SOC_DAPM_PRE_PMD, \
	.subseq = 100, /* Ensure we run after SYSCLK supply widget */ }, \
{	.id = snd_soc_dapm_out_drv, .name = wname, \
	.reg = SND_SOC_NOPM, .shift = num, .event = wm_adsp2_event, \
	.event_flags = SND_SOC_DAPM_POST_PMU | SND_SOC_DAPM_PRE_PMD }

extern const struct snd_kcontrol_new wm_adsp_fw_controls[];

int wm_adsp1_init(struct wm_adsp *dsp);
int wm_adsp2_init(struct wm_adsp *dsp);
void wm_adsp2_remove(struct wm_adsp *dsp);
int wm_adsp2_codec_probe(struct wm_adsp *dsp, struct snd_soc_codec *codec);
int wm_adsp2_codec_remove(struct wm_adsp *dsp, struct snd_soc_codec *codec);
int wm_adsp1_event(struct snd_soc_dapm_widget *w,
		   struct snd_kcontrol *kcontrol, int event);
int wm_adsp2_early_event(struct snd_soc_dapm_widget *w,
			 struct snd_kcontrol *kcontrol, int event,
			 unsigned int freq);
int wm_adsp2_event(struct snd_soc_dapm_widget *w,
		   struct snd_kcontrol *kcontrol, int event);

extern int wm_adsp_compr_open(struct wm_adsp *dsp,
			      struct snd_compr_stream *stream);
extern int wm_adsp_compr_free(struct snd_compr_stream *stream);
extern int wm_adsp_compr_set_params(struct snd_compr_stream *stream,
				    struct snd_compr_params *params);
extern int wm_adsp_compr_get_caps(struct snd_compr_stream *stream,
				  struct snd_compr_caps *caps);
extern int wm_adsp_compr_trigger(struct snd_compr_stream *stream, int cmd);
extern int wm_adsp_compr_handle_irq(struct wm_adsp *dsp);
extern int wm_adsp_compr_pointer(struct snd_compr_stream *stream,
				 struct snd_compr_tstamp *tstamp);
extern int wm_adsp_compr_copy(struct snd_compr_stream *stream,
			      char __user *buf, size_t count);

#endif
