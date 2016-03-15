#ifndef ODROID_AUDIO_H
#define ODROID_AUDIO_H

struct odroid_audio_private_data {
	int bias_level;
	int clock_en;
	int gpio_i2s_m;
	int gpio_i2s_s;
	int gpio_i2s_r;
	int gpio_i2s_o;
	const char *pinctrl_name;
	struct pinctrl *pin_ctl;
	void* data;
};

void aml_spdif_pinmux_init(struct device *pdev);

void aml_spdif_pinmux_deinit(struct device *pdev);
#endif
