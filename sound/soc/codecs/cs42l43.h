/* SPDX-License-Identifier: GPL-2.0 */
/*
 * CS42L43 CODEC driver internal data
 *
 * Copyright (C) 2022-2023 Cirrus Logic, Inc. and
 *                         Cirrus Logic International Semiconductor Ltd.
 */

#ifndef CS42L43_ASOC_INT_H
#define CS42L43_ASOC_INT_H

#include <linux/completion.h>
#include <linux/mutex.h>
#include <linux/types.h>
#include <linux/workqueue.h>
#include <sound/pcm.h>

#define CS42L43_INTERNAL_SYSCLK		24576000
#define CS42L43_DEFAULT_SLOTS		0x3F

#define CS42L43_PLL_TIMEOUT_MS		200
#define CS42L43_SPK_TIMEOUT_MS		100
#define CS42L43_HP_TIMEOUT_MS		2000
#define CS42L43_LOAD_TIMEOUT_MS		1000

#define CS42L43_HP_ILIMIT_BACKOFF_MS	1000
#define CS42L43_HP_ILIMIT_DECAY_MS	300
#define CS42L43_HP_ILIMIT_MAX_COUNT	4

#define CS42L43_ASP_MAX_CHANNELS	6
#define CS42L43_N_EQ_COEFFS		15

#define CS42L43_N_BUTTONS	6

struct clk;
struct device;

struct snd_soc_component;
struct snd_soc_jack;

struct cs42l43;

struct cs42l43_codec {
	struct device *dev;
	struct cs42l43 *core;
	struct snd_soc_component *component;

	struct clk *mclk;

	int n_slots;
	int slot_width;
	int tx_slots[CS42L43_ASP_MAX_CHANNELS];
	int rx_slots[CS42L43_ASP_MAX_CHANNELS];
	struct snd_pcm_hw_constraint_list constraint;

	u32 eq_coeffs[CS42L43_N_EQ_COEFFS];

	unsigned int refclk_src;
	unsigned int refclk_freq;
	struct completion pll_ready;

	unsigned int decim_cache[4];
	unsigned int adc_ena;
	unsigned int hp_ena;

	struct completion hp_startup;
	struct completion hp_shutdown;
	struct completion spkr_shutdown;
	struct completion spkl_shutdown;
	struct completion spkr_startup;
	struct completion spkl_startup;
	// Lock to ensure speaker VU updates don't clash
	struct mutex spk_vu_lock;

	// Lock for all jack detect operations
	struct mutex jack_lock;
	struct snd_soc_jack *jack_hp;

	bool use_ring_sense;
	unsigned int tip_debounce_ms;
	unsigned int tip_fall_db_ms;
	unsigned int tip_rise_db_ms;
	unsigned int bias_low;
	unsigned int bias_sense_ua;
	unsigned int bias_ramp_ms;
	unsigned int detect_us;
	unsigned int buttons[CS42L43_N_BUTTONS];

	struct delayed_work tip_sense_work;
	struct delayed_work bias_sense_timeout;
	struct delayed_work button_press_work;
	struct work_struct button_release_work;
	struct completion type_detect;
	struct completion load_detect;

	bool load_detect_running;
	bool button_detect_running;
	bool jack_present;
	int jack_override;
	bool suspend_jack_debounce;

	struct work_struct hp_ilimit_work;
	struct delayed_work hp_ilimit_clear_work;
	bool hp_ilimited;
	int hp_ilimit_count;

	struct snd_kcontrol *kctl[5];
};

#if IS_REACHABLE(CONFIG_SND_SOC_CS42L43_SDW)

int cs42l43_sdw_add_peripheral(struct snd_pcm_substream *substream,
			       struct snd_pcm_hw_params *params,
			       struct snd_soc_dai *dai);
int cs42l43_sdw_remove_peripheral(struct snd_pcm_substream *substream,
				  struct snd_soc_dai *dai);
int cs42l43_sdw_set_stream(struct snd_soc_dai *dai, void *sdw_stream, int direction);

#else

static inline int cs42l43_sdw_add_peripheral(struct snd_pcm_substream *substream,
					     struct snd_pcm_hw_params *params,
					     struct snd_soc_dai *dai)
{
	return -EINVAL;
}

#define cs42l43_sdw_remove_peripheral NULL
#define cs42l43_sdw_set_stream NULL

#endif

int cs42l43_set_jack(struct snd_soc_component *component,
		     struct snd_soc_jack *jack, void *d);
void cs42l43_bias_sense_timeout(struct work_struct *work);
void cs42l43_tip_sense_work(struct work_struct *work);
void cs42l43_button_press_work(struct work_struct *work);
void cs42l43_button_release_work(struct work_struct *work);
irqreturn_t cs42l43_bias_detect_clamp(int irq, void *data);
irqreturn_t cs42l43_button_press(int irq, void *data);
irqreturn_t cs42l43_button_release(int irq, void *data);
irqreturn_t cs42l43_tip_sense(int irq, void *data);
int cs42l43_jack_get(struct snd_kcontrol *kcontrol, struct snd_ctl_elem_value *ucontrol);
int cs42l43_jack_put(struct snd_kcontrol *kcontrol, struct snd_ctl_elem_value *ucontrol);

extern const struct soc_enum cs42l43_jack_enum;

#endif /* CS42L43_ASOC_INT_H */
