/*
 * card driver for models with PCM1796 DACs (Xonar D2/D2X/HDAV1.3/ST/STX)
 *
 * Copyright (c) Clemens Ladisch <clemens@ladisch.de>
 *
 *
 *  This driver is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License, version 2.
 *
 *  This driver is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this driver; if not, see <http://www.gnu.org/licenses/>.
 */

/*
 * Xonar D2/D2X
 * ------------
 *
 * CMI8788:
 *
 * SPI 0 -> 1st PCM1796 (front)
 * SPI 1 -> 2nd PCM1796 (surround)
 * SPI 2 -> 3rd PCM1796 (center/LFE)
 * SPI 4 -> 4th PCM1796 (back)
 *
 * GPIO 2 -> M0 of CS5381
 * GPIO 3 -> M1 of CS5381
 * GPIO 5 <- external power present (D2X only)
 * GPIO 7 -> ALT
 * GPIO 8 -> enable output to speakers
 *
 * CM9780:
 *
 * GPO 0 -> route line-in (0) or AC97 output (1) to CS5381 input
 */

/*
 * Xonar HDAV1.3 (Deluxe)
 * ----------------------
 *
 * CMI8788:
 *
 * I²C <-> PCM1796 (front)
 *
 * GPI 0 <- external power present
 *
 * GPIO 0 -> enable output to speakers
 * GPIO 2 -> M0 of CS5381
 * GPIO 3 -> M1 of CS5381
 * GPIO 8 -> route input jack to line-in (0) or mic-in (1)
 *
 * TXD -> HDMI controller
 * RXD <- HDMI controller
 *
 * PCM1796 front: AD1,0 <- 0,0
 *
 * CM9780:
 *
 * GPO 0 -> route line-in (0) or AC97 output (1) to CS5381 input
 *
 * no daughterboard
 * ----------------
 *
 * GPIO 4 <- 1
 *
 * H6 daughterboard
 * ----------------
 *
 * GPIO 4 <- 0
 * GPIO 5 <- 0
 *
 * I²C <-> PCM1796 (surround)
 *     <-> PCM1796 (center/LFE)
 *     <-> PCM1796 (back)
 *
 * PCM1796 surround:   AD1,0 <- 0,1
 * PCM1796 center/LFE: AD1,0 <- 1,0
 * PCM1796 back:       AD1,0 <- 1,1
 *
 * unknown daughterboard
 * ---------------------
 *
 * GPIO 4 <- 0
 * GPIO 5 <- 1
 *
 * I²C <-> CS4362A (surround, center/LFE, back)
 *
 * CS4362A: AD0 <- 0
 */

/*
 * Xonar Essence ST (Deluxe)/STX
 * -----------------------------
 *
 * CMI8788:
 *
 * I²C <-> PCM1792A
 *     <-> CS2000 (ST only)
 *
 * ADC1 MCLK -> REF_CLK of CS2000 (ST only)
 *
 * GPI 0 <- external power present (STX only)
 *
 * GPIO 0 -> enable output to speakers
 * GPIO 1 -> route HP to front panel (0) or rear jack (1)
 * GPIO 2 -> M0 of CS5381
 * GPIO 3 -> M1 of CS5381
 * GPIO 7 -> route output to speaker jacks (0) or HP (1)
 * GPIO 8 -> route input jack to line-in (0) or mic-in (1)
 *
 * PCM1792A:
 *
 * AD1,0 <- 0,0
 * SCK <- CLK_OUT of CS2000 (ST only)
 *
 * CS2000:
 *
 * AD0 <- 0
 *
 * CM9780:
 *
 * GPO 0 -> route line-in (0) or AC97 output (1) to CS5381 input
 *
 * H6 daughterboard
 * ----------------
 *
 * GPIO 4 <- 0
 * GPIO 5 <- 0
 */

#include <linux/pci.h>
#include <linux/delay.h>
#include <linux/mutex.h>
#include <sound/ac97_codec.h>
#include <sound/control.h>
#include <sound/core.h>
#include <sound/pcm.h>
#include <sound/pcm_params.h>
#include <sound/tlv.h>
#include "xonar.h"
#include "cm9780.h"
#include "pcm1796.h"
#include "cs2000.h"


#define GPIO_D2X_EXT_POWER	0x0020
#define GPIO_D2_ALT		0x0080
#define GPIO_D2_OUTPUT_ENABLE	0x0100

#define GPI_EXT_POWER		0x01
#define GPIO_INPUT_ROUTE	0x0100

#define GPIO_HDAV_OUTPUT_ENABLE	0x0001

#define GPIO_DB_MASK		0x0030
#define GPIO_DB_H6		0x0000

#define GPIO_ST_OUTPUT_ENABLE	0x0001
#define GPIO_ST_HP_REAR		0x0002
#define GPIO_ST_HP		0x0080

#define I2C_DEVICE_PCM1796(i)	(0x98 + ((i) << 1))	/* 10011, ii, /W=0 */
#define I2C_DEVICE_CS2000	0x9c			/* 100111, 0, /W=0 */


struct xonar_pcm179x {
	struct xonar_generic generic;
	unsigned int dacs;
	u8 oversampling;
	u8 cs2000_fun_cfg_1;
};

struct xonar_hdav {
	struct xonar_pcm179x pcm179x;
	struct xonar_hdmi hdmi;
};


static inline void pcm1796_write_spi(struct oxygen *chip, unsigned int codec,
				     u8 reg, u8 value)
{
	/* maps ALSA channel pair number to SPI output */
	static const u8 codec_map[4] = {
		0, 1, 2, 4
	};
	oxygen_write_spi(chip, OXYGEN_SPI_TRIGGER  |
			 OXYGEN_SPI_DATA_LENGTH_2 |
			 OXYGEN_SPI_CLOCK_160 |
			 (codec_map[codec] << OXYGEN_SPI_CODEC_SHIFT) |
			 OXYGEN_SPI_CEN_LATCH_CLOCK_HI,
			 (reg << 8) | value);
}

static inline void pcm1796_write_i2c(struct oxygen *chip, unsigned int codec,
				     u8 reg, u8 value)
{
	oxygen_write_i2c(chip, I2C_DEVICE_PCM1796(codec), reg, value);
}

static void pcm1796_write(struct oxygen *chip, unsigned int codec,
			  u8 reg, u8 value)
{
	if ((chip->model.function_flags & OXYGEN_FUNCTION_2WIRE_SPI_MASK) ==
	    OXYGEN_FUNCTION_SPI)
		pcm1796_write_spi(chip, codec, reg, value);
	else
		pcm1796_write_i2c(chip, codec, reg, value);
}

static void cs2000_write(struct oxygen *chip, u8 reg, u8 value)
{
	oxygen_write_i2c(chip, I2C_DEVICE_CS2000, reg, value);
}

static void update_pcm1796_volume(struct oxygen *chip)
{
	struct xonar_pcm179x *data = chip->model_data;
	unsigned int i;

	for (i = 0; i < data->dacs; ++i) {
		pcm1796_write(chip, i, 16, chip->dac_volume[i * 2]);
		pcm1796_write(chip, i, 17, chip->dac_volume[i * 2 + 1]);
	}
}

static void update_pcm1796_mute(struct oxygen *chip)
{
	struct xonar_pcm179x *data = chip->model_data;
	unsigned int i;
	u8 value;

	value = PCM1796_DMF_DISABLED | PCM1796_FMT_24_LJUST | PCM1796_ATLD;
	if (chip->dac_mute)
		value |= PCM1796_MUTE;
	for (i = 0; i < data->dacs; ++i)
		pcm1796_write(chip, i, 18, value);
}

static void pcm1796_init(struct oxygen *chip)
{
	struct xonar_pcm179x *data = chip->model_data;
	unsigned int i;

	for (i = 0; i < data->dacs; ++i) {
		pcm1796_write(chip, i, 19, PCM1796_FLT_SHARP | PCM1796_ATS_1);
		pcm1796_write(chip, i, 20, data->oversampling);
		pcm1796_write(chip, i, 21, 0);
	}
	update_pcm1796_mute(chip); /* set ATLD before ATL/ATR */
	update_pcm1796_volume(chip);
}

static void xonar_d2_init(struct oxygen *chip)
{
	struct xonar_pcm179x *data = chip->model_data;

	data->generic.anti_pop_delay = 300;
	data->generic.output_enable_bit = GPIO_D2_OUTPUT_ENABLE;
	data->dacs = 4;
	data->oversampling = PCM1796_OS_64;

	pcm1796_init(chip);

	oxygen_set_bits16(chip, OXYGEN_GPIO_CONTROL, GPIO_D2_ALT);
	oxygen_clear_bits16(chip, OXYGEN_GPIO_DATA, GPIO_D2_ALT);

	oxygen_ac97_set_bits(chip, 0, CM9780_JACK, CM9780_FMIC2MIC);

	xonar_init_cs53x1(chip);
	xonar_enable_output(chip);

	snd_component_add(chip->card, "PCM1796");
	snd_component_add(chip->card, "CS5381");
}

static void xonar_d2x_init(struct oxygen *chip)
{
	struct xonar_pcm179x *data = chip->model_data;

	data->generic.ext_power_reg = OXYGEN_GPIO_DATA;
	data->generic.ext_power_int_reg = OXYGEN_GPIO_INTERRUPT_MASK;
	data->generic.ext_power_bit = GPIO_D2X_EXT_POWER;
	oxygen_clear_bits16(chip, OXYGEN_GPIO_CONTROL, GPIO_D2X_EXT_POWER);
	xonar_init_ext_power(chip);
	xonar_d2_init(chip);
}

static void xonar_hdav_init(struct oxygen *chip)
{
	struct xonar_hdav *data = chip->model_data;

	oxygen_write16(chip, OXYGEN_2WIRE_BUS_STATUS,
		       OXYGEN_2WIRE_LENGTH_8 |
		       OXYGEN_2WIRE_INTERRUPT_MASK |
		       OXYGEN_2WIRE_SPEED_FAST);

	data->pcm179x.generic.anti_pop_delay = 100;
	data->pcm179x.generic.output_enable_bit = GPIO_HDAV_OUTPUT_ENABLE;
	data->pcm179x.generic.ext_power_reg = OXYGEN_GPI_DATA;
	data->pcm179x.generic.ext_power_int_reg = OXYGEN_GPI_INTERRUPT_MASK;
	data->pcm179x.generic.ext_power_bit = GPI_EXT_POWER;
	data->pcm179x.dacs = chip->model.private_data ? 4 : 1;
	data->pcm179x.oversampling = PCM1796_OS_64;

	pcm1796_init(chip);

	oxygen_set_bits16(chip, OXYGEN_GPIO_CONTROL, GPIO_INPUT_ROUTE);
	oxygen_clear_bits16(chip, OXYGEN_GPIO_DATA, GPIO_INPUT_ROUTE);

	xonar_init_cs53x1(chip);
	xonar_init_ext_power(chip);
	xonar_hdmi_init(chip, &data->hdmi);
	xonar_enable_output(chip);

	snd_component_add(chip->card, "PCM1796");
	snd_component_add(chip->card, "CS5381");
}

static void xonar_st_init_i2c(struct oxygen *chip)
{
	oxygen_write16(chip, OXYGEN_2WIRE_BUS_STATUS,
		       OXYGEN_2WIRE_LENGTH_8 |
		       OXYGEN_2WIRE_INTERRUPT_MASK |
		       OXYGEN_2WIRE_SPEED_FAST);
}

static void xonar_st_init_common(struct oxygen *chip)
{
	struct xonar_pcm179x *data = chip->model_data;

	data->generic.anti_pop_delay = 100;
	data->generic.output_enable_bit = GPIO_ST_OUTPUT_ENABLE;
	data->dacs = chip->model.private_data ? 4 : 1;
	data->oversampling = PCM1796_OS_64;

	pcm1796_init(chip);

	oxygen_set_bits16(chip, OXYGEN_GPIO_CONTROL,
			  GPIO_INPUT_ROUTE | GPIO_ST_HP_REAR | GPIO_ST_HP);
	oxygen_clear_bits16(chip, OXYGEN_GPIO_DATA,
			    GPIO_INPUT_ROUTE | GPIO_ST_HP_REAR | GPIO_ST_HP);

	xonar_init_cs53x1(chip);
	xonar_enable_output(chip);

	snd_component_add(chip->card, "PCM1792A");
	snd_component_add(chip->card, "CS5381");
}

static void cs2000_registers_init(struct oxygen *chip)
{
	struct xonar_pcm179x *data = chip->model_data;

	cs2000_write(chip, CS2000_GLOBAL_CFG, CS2000_FREEZE);
	cs2000_write(chip, CS2000_DEV_CTRL, 0);
	cs2000_write(chip, CS2000_DEV_CFG_1,
		     CS2000_R_MOD_SEL_1 |
		     (0 << CS2000_R_SEL_SHIFT) |
		     CS2000_AUX_OUT_SRC_REF_CLK |
		     CS2000_EN_DEV_CFG_1);
	cs2000_write(chip, CS2000_DEV_CFG_2,
		     (0 << CS2000_LOCK_CLK_SHIFT) |
		     CS2000_FRAC_N_SRC_STATIC);
	cs2000_write(chip, CS2000_RATIO_0 + 0, 0x00); /* 1.0 */
	cs2000_write(chip, CS2000_RATIO_0 + 1, 0x10);
	cs2000_write(chip, CS2000_RATIO_0 + 2, 0x00);
	cs2000_write(chip, CS2000_RATIO_0 + 3, 0x00);
	cs2000_write(chip, CS2000_FUN_CFG_1, data->cs2000_fun_cfg_1);
	cs2000_write(chip, CS2000_FUN_CFG_2, 0);
	cs2000_write(chip, CS2000_GLOBAL_CFG, CS2000_EN_DEV_CFG_2);
}

static void xonar_st_init(struct oxygen *chip)
{
	struct xonar_pcm179x *data = chip->model_data;

	data->cs2000_fun_cfg_1 = CS2000_REF_CLK_DIV_1;

	oxygen_write16(chip, OXYGEN_I2S_A_FORMAT,
		       OXYGEN_RATE_48000 | OXYGEN_I2S_FORMAT_I2S |
		       OXYGEN_I2S_MCLK_128 | OXYGEN_I2S_BITS_16 |
		       OXYGEN_I2S_MASTER | OXYGEN_I2S_BCLK_64);

	xonar_st_init_i2c(chip);
	cs2000_registers_init(chip);
	xonar_st_init_common(chip);

	snd_component_add(chip->card, "CS2000");
}

static void xonar_stx_init(struct oxygen *chip)
{
	struct xonar_pcm179x *data = chip->model_data;

	xonar_st_init_i2c(chip);
	data->generic.ext_power_reg = OXYGEN_GPI_DATA;
	data->generic.ext_power_int_reg = OXYGEN_GPI_INTERRUPT_MASK;
	data->generic.ext_power_bit = GPI_EXT_POWER;
	xonar_init_ext_power(chip);
	xonar_st_init_common(chip);
}

static void xonar_d2_cleanup(struct oxygen *chip)
{
	xonar_disable_output(chip);
}

static void xonar_hdav_cleanup(struct oxygen *chip)
{
	xonar_hdmi_cleanup(chip);
	xonar_disable_output(chip);
	msleep(2);
}

static void xonar_st_cleanup(struct oxygen *chip)
{
	xonar_disable_output(chip);
}

static void xonar_d2_suspend(struct oxygen *chip)
{
	xonar_d2_cleanup(chip);
}

static void xonar_hdav_suspend(struct oxygen *chip)
{
	xonar_hdav_cleanup(chip);
}

static void xonar_st_suspend(struct oxygen *chip)
{
	xonar_st_cleanup(chip);
}

static void xonar_d2_resume(struct oxygen *chip)
{
	pcm1796_init(chip);
	xonar_enable_output(chip);
}

static void xonar_hdav_resume(struct oxygen *chip)
{
	struct xonar_hdav *data = chip->model_data;

	pcm1796_init(chip);
	xonar_hdmi_resume(chip, &data->hdmi);
	xonar_enable_output(chip);
}

static void xonar_stx_resume(struct oxygen *chip)
{
	pcm1796_init(chip);
	xonar_enable_output(chip);
}

static void xonar_st_resume(struct oxygen *chip)
{
	cs2000_registers_init(chip);
	xonar_stx_resume(chip);
}

static void set_pcm1796_params(struct oxygen *chip,
			       struct snd_pcm_hw_params *params)
{
	struct xonar_pcm179x *data = chip->model_data;
	unsigned int i;

	data->oversampling =
		params_rate(params) >= 96000 ? PCM1796_OS_32 : PCM1796_OS_64;
	for (i = 0; i < data->dacs; ++i)
		pcm1796_write(chip, i, 20, data->oversampling);
}

static void set_cs2000_params(struct oxygen *chip,
			      struct snd_pcm_hw_params *params)
{
	/* XXX Why is the I2S A MCLK half the actual I2S multich MCLK? */
	static const u8 rate_mclks[] = {
		[OXYGEN_RATE_32000] = OXYGEN_RATE_32000 | OXYGEN_I2S_MCLK_128,
		[OXYGEN_RATE_44100] = OXYGEN_RATE_44100 | OXYGEN_I2S_MCLK_128,
		[OXYGEN_RATE_48000] = OXYGEN_RATE_48000 | OXYGEN_I2S_MCLK_128,
		[OXYGEN_RATE_64000] = OXYGEN_RATE_32000 | OXYGEN_I2S_MCLK_256,
		[OXYGEN_RATE_88200] = OXYGEN_RATE_44100 | OXYGEN_I2S_MCLK_256,
		[OXYGEN_RATE_96000] = OXYGEN_RATE_48000 | OXYGEN_I2S_MCLK_256,
		[OXYGEN_RATE_176400] = OXYGEN_RATE_44100 | OXYGEN_I2S_MCLK_256,
		[OXYGEN_RATE_192000] = OXYGEN_RATE_48000 | OXYGEN_I2S_MCLK_256,
	};
	struct xonar_pcm179x *data = chip->model_data;
	unsigned int rate_index;
	u8 rate_mclk;

	rate_index = oxygen_read16(chip, OXYGEN_I2S_MULTICH_FORMAT)
		& OXYGEN_I2S_RATE_MASK;
	rate_mclk = rate_mclks[rate_index];
	oxygen_write16_masked(chip, OXYGEN_I2S_A_FORMAT, rate_mclk,
			      OXYGEN_I2S_RATE_MASK | OXYGEN_I2S_MCLK_MASK);
	if ((rate_mclk & OXYGEN_I2S_MCLK_MASK) <= OXYGEN_I2S_MCLK_128)
		data->cs2000_fun_cfg_1 = CS2000_REF_CLK_DIV_1;
	else
		data->cs2000_fun_cfg_1 = CS2000_REF_CLK_DIV_2;
	cs2000_write(chip, CS2000_FUN_CFG_1, data->cs2000_fun_cfg_1);
}

static void set_st_params(struct oxygen *chip,
			  struct snd_pcm_hw_params *params)
{
	set_cs2000_params(chip, params);
	set_pcm1796_params(chip, params);
}

static void set_hdav_params(struct oxygen *chip,
			    struct snd_pcm_hw_params *params)
{
	struct xonar_hdav *data = chip->model_data;

	set_pcm1796_params(chip, params);
	xonar_set_hdmi_params(chip, &data->hdmi, params);
}

static const struct snd_kcontrol_new alt_switch = {
	.iface = SNDRV_CTL_ELEM_IFACE_MIXER,
	.name = "Analog Loopback Switch",
	.info = snd_ctl_boolean_mono_info,
	.get = xonar_gpio_bit_switch_get,
	.put = xonar_gpio_bit_switch_put,
	.private_value = GPIO_D2_ALT,
};

static int st_output_switch_info(struct snd_kcontrol *ctl,
				 struct snd_ctl_elem_info *info)
{
	static const char *const names[3] = {
		"Speakers", "Headphones", "FP Headphones"
	};

	info->type = SNDRV_CTL_ELEM_TYPE_ENUMERATED;
	info->count = 1;
	info->value.enumerated.items = 3;
	if (info->value.enumerated.item >= 3)
		info->value.enumerated.item = 2;
	strcpy(info->value.enumerated.name, names[info->value.enumerated.item]);
	return 0;
}

static int st_output_switch_get(struct snd_kcontrol *ctl,
				struct snd_ctl_elem_value *value)
{
	struct oxygen *chip = ctl->private_data;
	u16 gpio;

	gpio = oxygen_read16(chip, OXYGEN_GPIO_DATA);
	if (!(gpio & GPIO_ST_HP))
		value->value.enumerated.item[0] = 0;
	else if (gpio & GPIO_ST_HP_REAR)
		value->value.enumerated.item[0] = 1;
	else
		value->value.enumerated.item[0] = 2;
	return 0;
}


static int st_output_switch_put(struct snd_kcontrol *ctl,
				struct snd_ctl_elem_value *value)
{
	struct oxygen *chip = ctl->private_data;
	u16 gpio_old, gpio;

	mutex_lock(&chip->mutex);
	gpio_old = oxygen_read16(chip, OXYGEN_GPIO_DATA);
	gpio = gpio_old;
	switch (value->value.enumerated.item[0]) {
	case 0:
		gpio &= ~(GPIO_ST_HP | GPIO_ST_HP_REAR);
		break;
	case 1:
		gpio |= GPIO_ST_HP | GPIO_ST_HP_REAR;
		break;
	case 2:
		gpio = (gpio | GPIO_ST_HP) & ~GPIO_ST_HP_REAR;
		break;
	}
	oxygen_write16(chip, OXYGEN_GPIO_DATA, gpio);
	mutex_unlock(&chip->mutex);
	return gpio != gpio_old;
}

static const struct snd_kcontrol_new st_output_switch = {
	.iface = SNDRV_CTL_ELEM_IFACE_MIXER,
	.name = "Analog Output",
	.info = st_output_switch_info,
	.get = st_output_switch_get,
	.put = st_output_switch_put,
};

static void xonar_line_mic_ac97_switch(struct oxygen *chip,
				       unsigned int reg, unsigned int mute)
{
	if (reg == AC97_LINE) {
		spin_lock_irq(&chip->reg_lock);
		oxygen_write16_masked(chip, OXYGEN_GPIO_DATA,
				      mute ? GPIO_INPUT_ROUTE : 0,
				      GPIO_INPUT_ROUTE);
		spin_unlock_irq(&chip->reg_lock);
	}
}

static const DECLARE_TLV_DB_SCALE(pcm1796_db_scale, -6000, 50, 0);

static int xonar_d2_control_filter(struct snd_kcontrol_new *template)
{
	if (!strncmp(template->name, "CD Capture ", 11))
		/* CD in is actually connected to the video in pin */
		template->private_value ^= AC97_CD ^ AC97_VIDEO;
	return 0;
}

static int xonar_st_control_filter(struct snd_kcontrol_new *template)
{
	if (!strncmp(template->name, "CD Capture ", 11))
		return 1; /* no CD input */
	return 0;
}

static int xonar_d2_mixer_init(struct oxygen *chip)
{
	return snd_ctl_add(chip->card, snd_ctl_new1(&alt_switch, chip));
}

static int xonar_st_mixer_init(struct oxygen *chip)
{
	return snd_ctl_add(chip->card, snd_ctl_new1(&st_output_switch, chip));
}

static const struct oxygen_model model_xonar_d2 = {
	.longname = "Asus Virtuoso 200",
	.chip = "AV200",
	.init = xonar_d2_init,
	.control_filter = xonar_d2_control_filter,
	.mixer_init = xonar_d2_mixer_init,
	.cleanup = xonar_d2_cleanup,
	.suspend = xonar_d2_suspend,
	.resume = xonar_d2_resume,
	.set_dac_params = set_pcm1796_params,
	.set_adc_params = xonar_set_cs53x1_params,
	.update_dac_volume = update_pcm1796_volume,
	.update_dac_mute = update_pcm1796_mute,
	.dac_tlv = pcm1796_db_scale,
	.model_data_size = sizeof(struct xonar_pcm179x),
	.device_config = PLAYBACK_0_TO_I2S |
			 PLAYBACK_1_TO_SPDIF |
			 CAPTURE_0_FROM_I2S_2 |
			 CAPTURE_1_FROM_SPDIF |
			 MIDI_OUTPUT |
			 MIDI_INPUT,
	.dac_channels = 8,
	.dac_volume_min = 255 - 2*60,
	.dac_volume_max = 255,
	.misc_flags = OXYGEN_MISC_MIDI,
	.function_flags = OXYGEN_FUNCTION_SPI |
			  OXYGEN_FUNCTION_ENABLE_SPI_4_5,
	.dac_i2s_format = OXYGEN_I2S_FORMAT_LJUST,
	.adc_i2s_format = OXYGEN_I2S_FORMAT_LJUST,
};

static const struct oxygen_model model_xonar_hdav = {
	.longname = "Asus Virtuoso 200",
	.chip = "AV200",
	.init = xonar_hdav_init,
	.cleanup = xonar_hdav_cleanup,
	.suspend = xonar_hdav_suspend,
	.resume = xonar_hdav_resume,
	.pcm_hardware_filter = xonar_hdmi_pcm_hardware_filter,
	.set_dac_params = set_hdav_params,
	.set_adc_params = xonar_set_cs53x1_params,
	.update_dac_volume = update_pcm1796_volume,
	.update_dac_mute = update_pcm1796_mute,
	.uart_input = xonar_hdmi_uart_input,
	.ac97_switch = xonar_line_mic_ac97_switch,
	.dac_tlv = pcm1796_db_scale,
	.model_data_size = sizeof(struct xonar_hdav),
	.device_config = PLAYBACK_0_TO_I2S |
			 PLAYBACK_1_TO_SPDIF |
			 CAPTURE_0_FROM_I2S_2 |
			 CAPTURE_1_FROM_SPDIF,
	.dac_channels = 8,
	.dac_volume_min = 255 - 2*60,
	.dac_volume_max = 255,
	.misc_flags = OXYGEN_MISC_MIDI,
	.function_flags = OXYGEN_FUNCTION_2WIRE,
	.dac_i2s_format = OXYGEN_I2S_FORMAT_LJUST,
	.adc_i2s_format = OXYGEN_I2S_FORMAT_LJUST,
};

static const struct oxygen_model model_xonar_st = {
	.longname = "Asus Virtuoso 100",
	.chip = "AV200",
	.init = xonar_st_init,
	.control_filter = xonar_st_control_filter,
	.mixer_init = xonar_st_mixer_init,
	.cleanup = xonar_st_cleanup,
	.suspend = xonar_st_suspend,
	.resume = xonar_st_resume,
	.set_dac_params = set_st_params,
	.set_adc_params = xonar_set_cs53x1_params,
	.update_dac_volume = update_pcm1796_volume,
	.update_dac_mute = update_pcm1796_mute,
	.ac97_switch = xonar_line_mic_ac97_switch,
	.dac_tlv = pcm1796_db_scale,
	.model_data_size = sizeof(struct xonar_pcm179x),
	.device_config = PLAYBACK_0_TO_I2S |
			 PLAYBACK_1_TO_SPDIF |
			 CAPTURE_0_FROM_I2S_2,
	.dac_channels = 2,
	.dac_volume_min = 255 - 2*60,
	.dac_volume_max = 255,
	.function_flags = OXYGEN_FUNCTION_2WIRE,
	.dac_i2s_format = OXYGEN_I2S_FORMAT_LJUST,
	.adc_i2s_format = OXYGEN_I2S_FORMAT_LJUST,
};

int __devinit get_xonar_pcm179x_model(struct oxygen *chip,
				      const struct pci_device_id *id)
{
	switch (id->subdevice) {
	case 0x8269:
		chip->model = model_xonar_d2;
		chip->model.shortname = "Xonar D2";
		break;
	case 0x82b7:
		chip->model = model_xonar_d2;
		chip->model.shortname = "Xonar D2X";
		chip->model.init = xonar_d2x_init;
		break;
	case 0x8314:
		chip->model = model_xonar_hdav;
		oxygen_clear_bits16(chip, OXYGEN_GPIO_CONTROL, GPIO_DB_MASK);
		switch (oxygen_read16(chip, OXYGEN_GPIO_DATA) & GPIO_DB_MASK) {
		default:
			chip->model.shortname = "Xonar HDAV1.3";
			break;
		case GPIO_DB_H6:
			chip->model.shortname = "Xonar HDAV1.3+H6";
			chip->model.private_data = 1;
			break;
		}
		break;
	case 0x835d:
		chip->model = model_xonar_st;
		oxygen_clear_bits16(chip, OXYGEN_GPIO_CONTROL, GPIO_DB_MASK);
		switch (oxygen_read16(chip, OXYGEN_GPIO_DATA) & GPIO_DB_MASK) {
		default:
			chip->model.shortname = "Xonar ST";
			break;
		case GPIO_DB_H6:
			chip->model.shortname = "Xonar ST+H6";
			chip->model.dac_channels = 8;
			chip->model.private_data = 1;
			break;
		}
		break;
	case 0x835c:
		chip->model = model_xonar_st;
		chip->model.shortname = "Xonar STX";
		chip->model.init = xonar_stx_init;
		chip->model.resume = xonar_stx_resume;
		chip->model.set_dac_params = set_pcm1796_params;
		break;
	default:
		return -EINVAL;
	}
	return 0;
}
