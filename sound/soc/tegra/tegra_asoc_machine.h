/* SPDX-License-Identifier: GPL-2.0-only */

#ifndef __TEGRA_ASOC_MACHINE_H__
#define __TEGRA_ASOC_MACHINE_H__

struct clk;
struct gpio_desc;
struct snd_soc_card;
struct snd_soc_jack;
struct platform_device;
struct snd_soc_jack_gpio;
struct snd_soc_pcm_runtime;

struct tegra_asoc_data {
	unsigned int (*mclk_rate)(unsigned int srate);
	struct snd_soc_card *card;
	unsigned int mclk_id;
	bool hp_jack_gpio_active_low;
	bool add_common_dapm_widgets;
	bool add_common_controls;
	bool add_common_snd_ops;
	bool add_headset_jack;
	bool add_mic_jack;
	bool add_hp_jack;
	bool set_ac97;
};

struct tegra_machine {
	struct clk *clk_pll_a_out0;
	struct clk *clk_pll_a;
	struct clk *clk_cdev1;
	unsigned int set_baseclock;
	unsigned int set_mclk;
	const struct tegra_asoc_data *asoc;
	struct gpio_desc *gpiod_ext_mic_en;
	struct gpio_desc *gpiod_int_mic_en;
	struct gpio_desc *gpiod_spkr_en;
	struct gpio_desc *gpiod_mic_det;
	struct gpio_desc *gpiod_ear_sel;
	struct gpio_desc *gpiod_hp_mute;
	struct gpio_desc *gpiod_hp_det;
	struct snd_soc_jack *mic_jack;
	struct snd_soc_jack_gpio *hp_jack_gpio;
};

int tegra_asoc_machine_probe(struct platform_device *pdev);
int tegra_asoc_machine_init(struct snd_soc_pcm_runtime *rtd);

#endif
