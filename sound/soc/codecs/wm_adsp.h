/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * wm_adsp.h  --  Wolfson ADSP support
 *
 * Copyright 2012 Wolfson Microelectronics plc
 *
 * Author: Mark Brown <broonie@opensource.wolfsonmicro.com>
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

#define CS_ADSP2_REGION_0 BIT(0)
#define CS_ADSP2_REGION_1 BIT(1)
#define CS_ADSP2_REGION_2 BIT(2)
#define CS_ADSP2_REGION_3 BIT(3)
#define CS_ADSP2_REGION_4 BIT(4)
#define CS_ADSP2_REGION_5 BIT(5)
#define CS_ADSP2_REGION_6 BIT(6)
#define CS_ADSP2_REGION_7 BIT(7)
#define CS_ADSP2_REGION_8 BIT(8)
#define CS_ADSP2_REGION_9 BIT(9)
#define CS_ADSP2_REGION_1_9 (CS_ADSP2_REGION_1 | \
		CS_ADSP2_REGION_2 | CS_ADSP2_REGION_3 | \
		CS_ADSP2_REGION_4 | CS_ADSP2_REGION_5 | \
		CS_ADSP2_REGION_6 | CS_ADSP2_REGION_7 | \
		CS_ADSP2_REGION_8 | CS_ADSP2_REGION_9)
#define CS_ADSP2_REGION_ALL (CS_ADSP2_REGION_0 | CS_ADSP2_REGION_1_9)

struct cs_dsp_region {
	int type;
	unsigned int base;
};

struct cs_dsp_alg_region {
	struct list_head list;
	unsigned int alg;
	int type;
	unsigned int base;
};

struct wm_adsp_compr;
struct wm_adsp_compr_buf;
struct cs_dsp_ops;
struct cs_dsp_client_ops;

struct cs_dsp_coeff_ctl {
	const char *fw_name;
	/* Subname is needed to match with firmware */
	const char *subname;
	unsigned int subname_len;
	struct cs_dsp_alg_region alg_region;
	struct cs_dsp *dsp;
	unsigned int enabled:1;
	struct list_head list;
	void *cache;
	unsigned int offset;
	size_t len;
	unsigned int set:1;
	unsigned int flags;
	unsigned int type;

	void *priv;
};

struct cs_dsp {
	const char *name;
	int rev;
	int num;
	int type;
	struct device *dev;
	struct regmap *regmap;

	const struct cs_dsp_ops *ops;
	const struct cs_dsp_client_ops *client_ops;

	unsigned int base;
	unsigned int base_sysinfo;
	unsigned int sysclk_reg;
	unsigned int sysclk_mask;
	unsigned int sysclk_shift;

	struct list_head alg_regions;

	const char *fw_name;
	unsigned int fw_id;
	unsigned int fw_id_version;
	unsigned int fw_vendor_id;

	const struct cs_dsp_region *mem;
	int num_mems;

	int fw_ver;

	bool booted;
	bool running;

	struct list_head ctl_list;

	struct mutex pwr_lock;

	unsigned int lock_regions;

#ifdef CONFIG_DEBUG_FS
	struct dentry *debugfs_root;
	char *wmfw_file_name;
	char *bin_file_name;
#endif
};

struct wm_adsp {
	struct cs_dsp cs_dsp;
	const char *part;
	const char *fwf_name;
	struct snd_soc_component *component;

	unsigned int sys_config_size;

	int fw;

	struct work_struct boot_work;

	bool preloaded;
	bool fatal_error;

	struct list_head compr_list;
	struct list_head buffer_list;
};

struct cs_dsp_ops {
	bool (*validate_version)(struct cs_dsp *dsp, unsigned int version);
	unsigned int (*parse_sizes)(struct cs_dsp *dsp,
				    const char * const file,
				    unsigned int pos,
				    const struct firmware *firmware);
	int (*setup_algs)(struct cs_dsp *dsp);
	unsigned int (*region_to_reg)(struct cs_dsp_region const *mem,
				      unsigned int offset);

	void (*show_fw_status)(struct cs_dsp *dsp);
	void (*stop_watchdog)(struct cs_dsp *dsp);

	int (*enable_memory)(struct cs_dsp *dsp);
	void (*disable_memory)(struct cs_dsp *dsp);
	int (*lock_memory)(struct cs_dsp *dsp, unsigned int lock_regions);

	int (*enable_core)(struct cs_dsp *dsp);
	void (*disable_core)(struct cs_dsp *dsp);

	int (*start_core)(struct cs_dsp *dsp);
	void (*stop_core)(struct cs_dsp *dsp);
};

#define WM_ADSP1(wname, num) \
	SND_SOC_DAPM_PGA_E(wname, SND_SOC_NOPM, num, 0, NULL, 0, \
		wm_adsp1_event, SND_SOC_DAPM_POST_PMU | SND_SOC_DAPM_PRE_PMD)

#define WM_ADSP2_PRELOAD_SWITCH(wname, num) \
	SOC_SINGLE_EXT(wname " Preload Switch", SND_SOC_NOPM, num, 1, 0, \
		wm_adsp2_preloader_get, wm_adsp2_preloader_put)

#define WM_ADSP2(wname, num, event_fn) \
	SND_SOC_DAPM_SPK(wname " Preload", NULL), \
{	.id = snd_soc_dapm_supply, .name = wname " Preloader", \
	.reg = SND_SOC_NOPM, .shift = num, .event = event_fn, \
	.event_flags = SND_SOC_DAPM_PRE_PMU | SND_SOC_DAPM_PRE_PMD, \
	.subseq = 100, /* Ensure we run after SYSCLK supply widget */ }, \
{	.id = snd_soc_dapm_out_drv, .name = wname, \
	.reg = SND_SOC_NOPM, .shift = num, .event = wm_adsp_event, \
	.event_flags = SND_SOC_DAPM_POST_PMU | SND_SOC_DAPM_PRE_PMD }

#define WM_ADSP_FW_CONTROL(dspname, num) \
	SOC_ENUM_EXT(dspname " Firmware", wm_adsp_fw_enum[num], \
		     wm_adsp_fw_get, wm_adsp_fw_put)

extern const struct soc_enum wm_adsp_fw_enum[];

int wm_adsp1_init(struct wm_adsp *dsp);
int wm_adsp2_init(struct wm_adsp *dsp);
void wm_adsp2_remove(struct wm_adsp *dsp);
int wm_adsp2_component_probe(struct wm_adsp *dsp, struct snd_soc_component *component);
int wm_adsp2_component_remove(struct wm_adsp *dsp, struct snd_soc_component *component);
int wm_halo_init(struct wm_adsp *dsp);

int wm_adsp1_event(struct snd_soc_dapm_widget *w,
		   struct snd_kcontrol *kcontrol, int event);

int wm_adsp_early_event(struct snd_soc_dapm_widget *w,
			struct snd_kcontrol *kcontrol, int event);

irqreturn_t wm_adsp2_bus_error(int irq, void *data);
irqreturn_t wm_halo_bus_error(int irq, void *data);
irqreturn_t wm_halo_wdt_expire(int irq, void *data);

int wm_adsp_event(struct snd_soc_dapm_widget *w,
		  struct snd_kcontrol *kcontrol, int event);

int wm_adsp2_set_dspclk(struct snd_soc_dapm_widget *w, unsigned int freq);

int wm_adsp2_preloader_get(struct snd_kcontrol *kcontrol,
			   struct snd_ctl_elem_value *ucontrol);
int wm_adsp2_preloader_put(struct snd_kcontrol *kcontrol,
			   struct snd_ctl_elem_value *ucontrol);
int wm_adsp_fw_get(struct snd_kcontrol *kcontrol,
		   struct snd_ctl_elem_value *ucontrol);
int wm_adsp_fw_put(struct snd_kcontrol *kcontrol,
		   struct snd_ctl_elem_value *ucontrol);

int wm_adsp_compr_open(struct wm_adsp *dsp, struct snd_compr_stream *stream);
int wm_adsp_compr_free(struct snd_soc_component *component,
		       struct snd_compr_stream *stream);
int wm_adsp_compr_set_params(struct snd_soc_component *component,
			     struct snd_compr_stream *stream,
			     struct snd_compr_params *params);
int wm_adsp_compr_get_caps(struct snd_soc_component *component,
			   struct snd_compr_stream *stream,
			   struct snd_compr_caps *caps);
int wm_adsp_compr_trigger(struct snd_soc_component *component,
			  struct snd_compr_stream *stream, int cmd);
int wm_adsp_compr_handle_irq(struct wm_adsp *dsp);
int wm_adsp_compr_pointer(struct snd_soc_component *component,
			  struct snd_compr_stream *stream,
			  struct snd_compr_tstamp *tstamp);
int wm_adsp_compr_copy(struct snd_soc_component *component,
		       struct snd_compr_stream *stream,
		       char __user *buf, size_t count);
int wm_adsp_write_ctl(struct wm_adsp *dsp, const char *name,  int type,
		      unsigned int alg, void *buf, size_t len);
int wm_adsp_read_ctl(struct wm_adsp *dsp, const char *name,  int type,
		      unsigned int alg, void *buf, size_t len);

struct cs_dsp_client_ops {
	int (*control_add)(struct cs_dsp_coeff_ctl *ctl);
	void (*control_remove)(struct cs_dsp_coeff_ctl *ctl);
	int (*post_run)(struct cs_dsp *dsp);
	void (*post_stop)(struct cs_dsp *dsp);
	void (*watchdog_expired)(struct cs_dsp *dsp);
};

#endif
