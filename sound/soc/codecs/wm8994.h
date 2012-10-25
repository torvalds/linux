/*
 * wm8994.h  --  WM8994 Soc Audio driver
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#ifndef _WM8994_H
#define _WM8994_H

#include <sound/soc.h>
#include <linux/firmware.h>
#include <linux/completion.h>
#include <linux/workqueue.h>

#include "wm_hubs.h"

/* Sources for AIF1/2 SYSCLK - use with set_dai_sysclk() */
#define WM8994_SYSCLK_MCLK1 1
#define WM8994_SYSCLK_MCLK2 2
#define WM8994_SYSCLK_FLL1  3
#define WM8994_SYSCLK_FLL2  4

/* OPCLK is also configured with set_dai_sysclk, specify division*10 as rate. */
#define WM8994_SYSCLK_OPCLK 5

#define WM8994_FLL1 1
#define WM8994_FLL2 2

#define WM8994_FLL_SRC_MCLK1    1
#define WM8994_FLL_SRC_MCLK2    2
#define WM8994_FLL_SRC_LRCLK    3
#define WM8994_FLL_SRC_BCLK     4
#define WM8994_FLL_SRC_INTERNAL 5

enum wm8994_vmid_mode {
	WM8994_VMID_NORMAL,
	WM8994_VMID_FORCE,
};

typedef void (*wm8958_micdet_cb)(u16 status, void *data);

int wm8994_mic_detect(struct snd_soc_codec *codec, struct snd_soc_jack *jack,
		      int micbias);
int wm8958_mic_detect(struct snd_soc_codec *codec, struct snd_soc_jack *jack,
		      wm8958_micdet_cb cb, void *cb_data);

int wm8994_vmid_mode(struct snd_soc_codec *codec, enum wm8994_vmid_mode mode);

int wm8958_aif_ev(struct snd_soc_dapm_widget *w,
		  struct snd_kcontrol *kcontrol, int event);

void wm8958_dsp2_init(struct snd_soc_codec *codec);

struct wm8994_micdet {
	struct snd_soc_jack *jack;
	bool detecting;
};

/* codec private data */
struct wm8994_fll_config {
	int src;
	int in;
	int out;
};

#define WM8994_NUM_DRC 3
#define WM8994_NUM_EQ  3

struct wm8994;

struct wm8994_priv {
	struct wm_hubs_data hubs;
	struct wm8994 *wm8994;
	int sysclk[2];
	int sysclk_rate[2];
	int mclk[2];
	int aifclk[2];
	int channels[2];
	struct wm8994_fll_config fll[2], fll_suspend[2];
	struct completion fll_locked[2];
	bool fll_locked_irq;
	bool fll_byp;
	bool clk_has_run;

	int vmid_refcount;
	int active_refcount;
	enum wm8994_vmid_mode vmid_mode;

	int dac_rates[2];
	int lrclk_shared[2];

	int mbc_ena[3];
	int hpf1_ena[3];
	int hpf2_ena[3];
	int vss_ena[3];
	int enh_eq_ena[3];

	/* Platform dependant DRC configuration */
	const char **drc_texts;
	int drc_cfg[WM8994_NUM_DRC];
	struct soc_enum drc_enum;

	/* Platform dependant ReTune mobile configuration */
	int num_retune_mobile_texts;
	const char **retune_mobile_texts;
	int retune_mobile_cfg[WM8994_NUM_EQ];
	struct soc_enum retune_mobile_enum;

	/* Platform dependant MBC configuration */
	int mbc_cfg;
	const char **mbc_texts;
	struct soc_enum mbc_enum;

	/* Platform dependant VSS configuration */
	int vss_cfg;
	const char **vss_texts;
	struct soc_enum vss_enum;

	/* Platform dependant VSS HPF configuration */
	int vss_hpf_cfg;
	const char **vss_hpf_texts;
	struct soc_enum vss_hpf_enum;

	/* Platform dependant enhanced EQ configuration */
	int enh_eq_cfg;
	const char **enh_eq_texts;
	struct soc_enum enh_eq_enum;

	struct mutex accdet_lock;
	struct wm8994_micdet micdet[2];
	struct delayed_work mic_work;
	bool mic_detecting;
	bool jack_mic;
	int btn_mask;
	bool jackdet;
	int jackdet_mode;
	struct delayed_work jackdet_bootstrap;

	wm8958_micdet_cb jack_cb;
	void *jack_cb_data;
	int micdet_irq;

	int revision;
	struct wm8994_pdata *pdata;

	unsigned int aif1clk_enable:1;
	unsigned int aif2clk_enable:1;

	unsigned int aif1clk_disable:1;
	unsigned int aif2clk_disable:1;

	int dsp_active;
	const struct firmware *cur_fw;
	const struct firmware *mbc;
	const struct firmware *mbc_vss;
	const struct firmware *enh_eq;
};

#endif
