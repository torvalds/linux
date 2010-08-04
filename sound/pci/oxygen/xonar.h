#ifndef XONAR_H_INCLUDED
#define XONAR_H_INCLUDED

#include "oxygen.h"

struct xonar_generic {
	unsigned int anti_pop_delay;
	u16 output_enable_bit;
	u8 ext_power_reg;
	u8 ext_power_int_reg;
	u8 ext_power_bit;
	u8 has_power;
};

struct xonar_hdmi {
	u8 params[5];
};

/* generic helper functions */

void xonar_enable_output(struct oxygen *chip);
void xonar_disable_output(struct oxygen *chip);
void xonar_init_ext_power(struct oxygen *chip);
void xonar_init_cs53x1(struct oxygen *chip);
void xonar_set_cs53x1_params(struct oxygen *chip,
			     struct snd_pcm_hw_params *params);
int xonar_gpio_bit_switch_get(struct snd_kcontrol *ctl,
			      struct snd_ctl_elem_value *value);
int xonar_gpio_bit_switch_put(struct snd_kcontrol *ctl,
			      struct snd_ctl_elem_value *value);

/* model-specific card drivers */

int get_xonar_pcm179x_model(struct oxygen *chip,
			    const struct pci_device_id *id);
int get_xonar_cs43xx_model(struct oxygen *chip,
			   const struct pci_device_id *id);
int get_xonar_wm87x6_model(struct oxygen *chip,
			   const struct pci_device_id *id);

/* HDMI helper functions */

void xonar_hdmi_init(struct oxygen *chip, struct xonar_hdmi *data);
void xonar_hdmi_cleanup(struct oxygen *chip);
void xonar_hdmi_resume(struct oxygen *chip, struct xonar_hdmi *hdmi);
void xonar_hdmi_pcm_hardware_filter(unsigned int channel,
				    struct snd_pcm_hardware *hardware);
void xonar_set_hdmi_params(struct oxygen *chip, struct xonar_hdmi *hdmi,
			   struct snd_pcm_hw_params *params);
void xonar_hdmi_uart_input(struct oxygen *chip);

#endif
