/*
 * card driver for models with CS4398/CS4362A DACs (Xonar D1/DX)
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
 * Xonar D1/DX
 * -----------
 *
 * CMI8788:
 *
 * I²C <-> CS4398 (front)
 *     <-> CS4362A (surround, center/LFE, back)
 *
 * GPI 0 <- external power present (DX only)
 *
 * GPIO 0 -> enable output to speakers
 * GPIO 1 -> enable front panel I/O
 * GPIO 2 -> M0 of CS5361
 * GPIO 3 -> M1 of CS5361
 * GPIO 8 -> route input jack to line-in (0) or mic-in (1)
 *
 * CS4398:
 *
 * AD0 <- 1
 * AD1 <- 1
 *
 * CS4362A:
 *
 * AD0 <- 0
 */

#include <linux/pci.h>
#include <linux/delay.h>
#include <sound/ac97_codec.h>
#include <sound/control.h>
#include <sound/core.h>
#include <sound/pcm.h>
#include <sound/pcm_params.h>
#include <sound/tlv.h>
#include "xonar.h"
#include "cs4398.h"
#include "cs4362a.h"

#define GPI_EXT_POWER		0x01
#define GPIO_D1_OUTPUT_ENABLE	0x0001
#define GPIO_D1_FRONT_PANEL	0x0002
#define GPIO_D1_INPUT_ROUTE	0x0100

#define I2C_DEVICE_CS4398	0x9e	/* 10011, AD1=1, AD0=1, /W=0 */
#define I2C_DEVICE_CS4362A	0x30	/* 001100, AD0=0, /W=0 */

struct xonar_cs43xx {
	struct xonar_generic generic;
	u8 cs4398_fm;
	u8 cs4362a_fm;
	u8 cs4362a_fm_c;
};

static void cs4398_write(struct oxygen *chip, u8 reg, u8 value)
{
	oxygen_write_i2c(chip, I2C_DEVICE_CS4398, reg, value);
}

static void cs4362a_write(struct oxygen *chip, u8 reg, u8 value)
{
	oxygen_write_i2c(chip, I2C_DEVICE_CS4362A, reg, value);
}

static void update_cs4362a_volumes(struct oxygen *chip)
{
	u8 mute;

	mute = chip->dac_mute ? CS4362A_MUTE : 0;
	cs4362a_write(chip, 7, (127 - chip->dac_volume[2]) | mute);
	cs4362a_write(chip, 8, (127 - chip->dac_volume[3]) | mute);
	cs4362a_write(chip, 10, (127 - chip->dac_volume[4]) | mute);
	cs4362a_write(chip, 11, (127 - chip->dac_volume[5]) | mute);
	cs4362a_write(chip, 13, (127 - chip->dac_volume[6]) | mute);
	cs4362a_write(chip, 14, (127 - chip->dac_volume[7]) | mute);
}

static void update_cs43xx_volume(struct oxygen *chip)
{
	cs4398_write(chip, 5, (127 - chip->dac_volume[0]) * 2);
	cs4398_write(chip, 6, (127 - chip->dac_volume[1]) * 2);
	update_cs4362a_volumes(chip);
}

static void update_cs43xx_mute(struct oxygen *chip)
{
	u8 reg;

	reg = CS4398_MUTEP_LOW | CS4398_PAMUTE;
	if (chip->dac_mute)
		reg |= CS4398_MUTE_B | CS4398_MUTE_A;
	cs4398_write(chip, 4, reg);
	update_cs4362a_volumes(chip);
}

static void cs43xx_init(struct oxygen *chip)
{
	struct xonar_cs43xx *data = chip->model_data;

	/* set CPEN (control port mode) and power down */
	cs4398_write(chip, 8, CS4398_CPEN | CS4398_PDN);
	cs4362a_write(chip, 0x01, CS4362A_PDN | CS4362A_CPEN);
	/* configure */
	cs4398_write(chip, 2, data->cs4398_fm);
	cs4398_write(chip, 3, CS4398_ATAPI_B_R | CS4398_ATAPI_A_L);
	cs4398_write(chip, 7, CS4398_RMP_DN | CS4398_RMP_UP |
		     CS4398_ZERO_CROSS | CS4398_SOFT_RAMP);
	cs4362a_write(chip, 0x02, CS4362A_DIF_LJUST);
	cs4362a_write(chip, 0x03, CS4362A_MUTEC_6 | CS4362A_AMUTE |
		      CS4362A_RMP_UP | CS4362A_ZERO_CROSS | CS4362A_SOFT_RAMP);
	cs4362a_write(chip, 0x04, CS4362A_RMP_DN | CS4362A_DEM_NONE);
	cs4362a_write(chip, 0x05, 0);
	cs4362a_write(chip, 0x06, data->cs4362a_fm);
	cs4362a_write(chip, 0x09, data->cs4362a_fm_c);
	cs4362a_write(chip, 0x0c, data->cs4362a_fm);
	update_cs43xx_volume(chip);
	update_cs43xx_mute(chip);
	/* clear power down */
	cs4398_write(chip, 8, CS4398_CPEN);
	cs4362a_write(chip, 0x01, CS4362A_CPEN);
}

static void xonar_d1_init(struct oxygen *chip)
{
	struct xonar_cs43xx *data = chip->model_data;

	data->generic.anti_pop_delay = 800;
	data->generic.output_enable_bit = GPIO_D1_OUTPUT_ENABLE;
	data->cs4398_fm = CS4398_FM_SINGLE | CS4398_DEM_NONE | CS4398_DIF_LJUST;
	data->cs4362a_fm = CS4362A_FM_SINGLE |
		CS4362A_ATAPI_B_R | CS4362A_ATAPI_A_L;
	data->cs4362a_fm_c = data->cs4362a_fm;

	oxygen_write16(chip, OXYGEN_2WIRE_BUS_STATUS,
		       OXYGEN_2WIRE_LENGTH_8 |
		       OXYGEN_2WIRE_INTERRUPT_MASK |
		       OXYGEN_2WIRE_SPEED_FAST);

	cs43xx_init(chip);

	oxygen_set_bits16(chip, OXYGEN_GPIO_CONTROL,
			  GPIO_D1_FRONT_PANEL | GPIO_D1_INPUT_ROUTE);
	oxygen_clear_bits16(chip, OXYGEN_GPIO_DATA,
			    GPIO_D1_FRONT_PANEL | GPIO_D1_INPUT_ROUTE);

	xonar_init_cs53x1(chip);
	xonar_enable_output(chip);

	snd_component_add(chip->card, "CS4398");
	snd_component_add(chip->card, "CS4362A");
	snd_component_add(chip->card, "CS5361");
}

static void xonar_dx_init(struct oxygen *chip)
{
	struct xonar_cs43xx *data = chip->model_data;

	data->generic.ext_power_reg = OXYGEN_GPI_DATA;
	data->generic.ext_power_int_reg = OXYGEN_GPI_INTERRUPT_MASK;
	data->generic.ext_power_bit = GPI_EXT_POWER;
	xonar_init_ext_power(chip);
	xonar_d1_init(chip);
}

static void xonar_d1_cleanup(struct oxygen *chip)
{
	xonar_disable_output(chip);
	cs4362a_write(chip, 0x01, CS4362A_PDN | CS4362A_CPEN);
	oxygen_clear_bits8(chip, OXYGEN_FUNCTION, OXYGEN_FUNCTION_RESET_CODEC);
}

static void xonar_d1_suspend(struct oxygen *chip)
{
	xonar_d1_cleanup(chip);
}

static void xonar_d1_resume(struct oxygen *chip)
{
	oxygen_set_bits8(chip, OXYGEN_FUNCTION, OXYGEN_FUNCTION_RESET_CODEC);
	msleep(1);
	cs43xx_init(chip);
	xonar_enable_output(chip);
}

static void set_cs43xx_params(struct oxygen *chip,
			      struct snd_pcm_hw_params *params)
{
	struct xonar_cs43xx *data = chip->model_data;
	u8 cs4398_fm, cs4362a_fm;

	if (params_rate(params) <= 50000) {
		cs4398_fm = CS4398_FM_SINGLE;
		cs4362a_fm = CS4362A_FM_SINGLE;
	} else if (params_rate(params) <= 100000) {
		cs4398_fm = CS4398_FM_DOUBLE;
		cs4362a_fm = CS4362A_FM_DOUBLE;
	} else {
		cs4398_fm = CS4398_FM_QUAD;
		cs4362a_fm = CS4362A_FM_QUAD;
	}
	data->cs4398_fm = CS4398_DEM_NONE | CS4398_DIF_LJUST | cs4398_fm;
	data->cs4362a_fm =
		(data->cs4362a_fm & ~CS4362A_FM_MASK) | cs4362a_fm;
	data->cs4362a_fm_c =
		(data->cs4362a_fm_c & ~CS4362A_FM_MASK) | cs4362a_fm;
	cs4398_write(chip, 2, data->cs4398_fm);
	cs4362a_write(chip, 0x06, data->cs4362a_fm);
	cs4362a_write(chip, 0x09, data->cs4362a_fm_c);
	cs4362a_write(chip, 0x0c, data->cs4362a_fm);
}

static void update_cs43xx_center_lfe_mix(struct oxygen *chip, bool mixed)
{
	struct xonar_cs43xx *data = chip->model_data;

	data->cs4362a_fm_c &= ~CS4362A_ATAPI_MASK;
	if (mixed)
		data->cs4362a_fm_c |= CS4362A_ATAPI_B_LR | CS4362A_ATAPI_A_LR;
	else
		data->cs4362a_fm_c |= CS4362A_ATAPI_B_R | CS4362A_ATAPI_A_L;
	cs4362a_write(chip, 0x09, data->cs4362a_fm_c);
}

static const struct snd_kcontrol_new front_panel_switch = {
	.iface = SNDRV_CTL_ELEM_IFACE_MIXER,
	.name = "Front Panel Switch",
	.info = snd_ctl_boolean_mono_info,
	.get = xonar_gpio_bit_switch_get,
	.put = xonar_gpio_bit_switch_put,
	.private_value = GPIO_D1_FRONT_PANEL,
};

static void xonar_d1_line_mic_ac97_switch(struct oxygen *chip,
					  unsigned int reg, unsigned int mute)
{
	if (reg == AC97_LINE) {
		spin_lock_irq(&chip->reg_lock);
		oxygen_write16_masked(chip, OXYGEN_GPIO_DATA,
				      mute ? GPIO_D1_INPUT_ROUTE : 0,
				      GPIO_D1_INPUT_ROUTE);
		spin_unlock_irq(&chip->reg_lock);
	}
}

static const DECLARE_TLV_DB_SCALE(cs4362a_db_scale, -6000, 100, 0);

static int xonar_d1_control_filter(struct snd_kcontrol_new *template)
{
	if (!strncmp(template->name, "CD Capture ", 11))
		return 1; /* no CD input */
	return 0;
}

static int xonar_d1_mixer_init(struct oxygen *chip)
{
	return snd_ctl_add(chip->card, snd_ctl_new1(&front_panel_switch, chip));
}

static const struct oxygen_model model_xonar_d1 = {
	.longname = "Asus Virtuoso 100",
	.chip = "AV200",
	.init = xonar_d1_init,
	.control_filter = xonar_d1_control_filter,
	.mixer_init = xonar_d1_mixer_init,
	.cleanup = xonar_d1_cleanup,
	.suspend = xonar_d1_suspend,
	.resume = xonar_d1_resume,
	.set_dac_params = set_cs43xx_params,
	.set_adc_params = xonar_set_cs53x1_params,
	.update_dac_volume = update_cs43xx_volume,
	.update_dac_mute = update_cs43xx_mute,
	.update_center_lfe_mix = update_cs43xx_center_lfe_mix,
	.ac97_switch = xonar_d1_line_mic_ac97_switch,
	.dac_tlv = cs4362a_db_scale,
	.model_data_size = sizeof(struct xonar_cs43xx),
	.device_config = PLAYBACK_0_TO_I2S |
			 PLAYBACK_1_TO_SPDIF |
			 CAPTURE_0_FROM_I2S_2,
	.dac_channels = 8,
	.dac_volume_min = 127 - 60,
	.dac_volume_max = 127,
	.function_flags = OXYGEN_FUNCTION_2WIRE,
	.dac_i2s_format = OXYGEN_I2S_FORMAT_LJUST,
	.adc_i2s_format = OXYGEN_I2S_FORMAT_LJUST,
};

int __devinit get_xonar_cs43xx_model(struct oxygen *chip,
				     const struct pci_device_id *id)
{
	switch (id->subdevice) {
	case 0x834f:
		chip->model = model_xonar_d1;
		chip->model.shortname = "Xonar D1";
		break;
	case 0x8275:
	case 0x8327:
		chip->model = model_xonar_d1;
		chip->model.shortname = "Xonar DX";
		chip->model.init = xonar_dx_init;
		break;
	default:
		return -EINVAL;
	}
	return 0;
}
