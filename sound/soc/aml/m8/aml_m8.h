#ifndef AML_M8_H
#define AML_M8_H

struct aml_audio_private_data {
    int bias_level;
    int clock_en;
	int gpio_hp_det;
	bool det_pol_inv;
    int sleep_time;
	int gpio_mute;
    int gpio_i2s_m;
    int gpio_i2s_s;
    int gpio_i2s_r;
    int gpio_i2s_o;
	bool mute_inv;
	struct pinctrl *pin_ctl;
    int hp_last_state;
    bool hp_det_status;
    unsigned int hp_val_h;
    unsigned int hp_val_l;
    unsigned int mic_val;
    unsigned int hp_detal;
    unsigned int hp_adc_ch;

    bool mic_det;
    bool hp_disable;
    

    int timer_en;
    int detect_flag;
    struct timer_list timer;
    struct work_struct work;
    struct mutex lock;
    struct snd_soc_jack jack;
    void* data;

	struct switch_dev sdev; // for android
	struct switch_dev mic_sdev; // for android
};

void aml_spdif_pinmux_init(struct device *pdev);

void aml_spdif_pinmux_deinit(struct device *pdev);
#endif

