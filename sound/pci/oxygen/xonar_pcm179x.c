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
 *   SPI 0 -> 1st PCM1796 (front)
 *   SPI 1 -> 2nd PCM1796 (surround)
 *   SPI 2 -> 3rd PCM1796 (center/LFE)
 *   SPI 4 -> 4th PCM1796 (back)
 *
 *   GPIO 2 -> M0 of CS5381
 *   GPIO 3 -> M1 of CS5381
 *   GPIO 5 <- external power present (D2X only)
 *   GPIO 7 -> ALT
 *   GPIO 8 -> enable output to speakers
 *
 * CM9780:
 *
 *   LINE_OUT -> input of ADC
 *
 *   AUX_IN   <- aux
 *   VIDEO_IN <- CD
 *   FMIC_IN  <- mic
 *
 *   GPO 0 -> route line-in (0) or AC97 output (1) to CS5381 input
 */

/*
 * Xonar HDAV1.3 (Deluxe)
 * ----------------------
 *
 * CMI8788:
 *
 *   I²C <-> PCM1796 (addr 1001100) (front)
 *
 *   GPI 0 <- external power present
 *
 *   GPIO 0 -> enable HDMI (0) or speaker (1) output
 *   GPIO 2 -> M0 of CS5381
 *   GPIO 3 -> M1 of CS5381
 *   GPIO 4 <- daughterboard detection
 *   GPIO 5 <- daughterboard detection
 *   GPIO 6 -> ?
 *   GPIO 7 -> ?
 *   GPIO 8 -> route input jack to line-in (0) or mic-in (1)
 *
 *   UART <-> HDMI controller
 *
 * CM9780:
 *
 *   LINE_OUT -> input of ADC
 *
 *   AUX_IN <- aux
 *   CD_IN  <- CD
 *   MIC_IN <- mic
 *
 *   GPO 0 -> route line-in (0) or AC97 output (1) to CS5381 input
 *
 * no daughterboard
 * ----------------
 *
 *   GPIO 4 <- 1
 *
 * H6 daughterboard
 * ----------------
 *
 *   GPIO 4 <- 0
 *   GPIO 5 <- 0
 *
 *   I²C <-> PCM1796 (addr 1001101) (surround)
 *       <-> PCM1796 (addr 1001110) (center/LFE)
 *       <-> PCM1796 (addr 1001111) (back)
 *
 * unknown daughterboard
 * ---------------------
 *
 *   GPIO 4 <- 0
 *   GPIO 5 <- 1
 *
 *   I²C <-> CS4362A (addr 0011000) (surround, center/LFE, back)
 */

/*
 * Xonar Essence ST (Deluxe)/STX
 * -----------------------------
 *
 * CMI8788:
 *
 *   I²C <-> PCM1792A (addr 1001100)
 *       <-> CS2000 (addr 1001110) (ST only)
 *
 *   ADC1 MCLK -> REF_CLK of CS2000 (ST only)
 *
 *   GPI 0 <- external power present (STX only)
 *
 *   GPIO 0 -> enable output to speakers
 *   GPIO 1 -> route HP to front panel (0) or rear jack (1)
 *   GPIO 2 -> M0 of CS5381
 *   GPIO 3 -> M1 of CS5381
 *   GPIO 4 <- daughterboard detection
 *   GPIO 5 <- daughterboard detection
 *   GPIO 6 -> ?
 *   GPIO 7 -> route output to speaker jacks (0) or HP (1)
 *   GPIO 8 -> route input jack to line-in (0) or mic-in (1)
 *
 * PCM1792A:
 *
 *   SCK <- CLK_OUT of CS2000 (ST only)
 *
 * CM9780:
 *
 *   LINE_OUT -> input of ADC
 *
 *   AUX_IN <- aux
 *   MIC_IN <- mic
 *
 *   GPO 0 -> route line-in (0) or AC97 output (1) to CS5381 input
 *
 * H6 daughterboard
 * ----------------
 *
 * GPIO 4 <- 0
 * GPIO 5 <- 0
 */

/*
 * Xonar Xense
 * -----------
 *
 * CMI8788:
 *
 *   I²C <-> PCM1796 (addr 1001100) (front)
 *       <-> CS4362A (addr 0011000) (surround, center/LFE, back)
 *       <-> CS2000 (addr 1001110)
 *
 *   ADC1 MCLK -> REF_CLK of CS2000
 *
 *   GPI 0 <- external power present
 *
 *   GPIO 0 -> enable output
 *   GPIO 1 -> route HP to front panel (0) or rear jack (1)
 *   GPIO 2 -> M0 of CS5381
 *   GPIO 3 -> M1 of CS5381
 *   GPIO 4 -> enable output
 *   GPIO 5 -> enable output
 *   GPIO 6 -> ?
 *   GPIO 7 -> route output to HP (0) or speaker (1)
 *   GPIO 8 -> route input jack to mic-in (0) or line-in (1)
 *
 * CM9780:
 *
 *   LINE_OUT -> input of ADC
 *
 *   AUX_IN   <- aux
 *   VIDEO_IN <- ?
 *   FMIC_IN  <- mic
 *
 *   GPO 0 -> route line-in (0) or AC97 output (1) to CS5381 input
 *   GPO 1 -> route mic-in from input jack (0) or front panel header (1)
 */

#include <linux/pci.h>
#include <linux/delay.h>
#include <linux/mutex.h>
#include <sound/ac97_codec.h>
#include <sound/control.h>
#include <sound/core.h>
#include <sound/info.h>
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
#define GPIO_HDAV_MAGIC		0x00c0

#define GPIO_DB_MASK		0x0030
#define GPIO_DB_H6		0x0000

#define GPIO_ST_OUTPUT_ENABLE	0x0001
#define GPIO_ST_HP_REAR		0x0002
#define GPIO_ST_MAGIC		0x0040
#define GPIO_ST_HP		0x0080

#define I2C_DEVICE_PCM1796(i)	(0x98 + ((i) << 1))	/* 10011, ii, /W=0 */
#define I2C_DEVICE_CS2000	0x9c			/* 100111, 0, /W=0 */

#define PCM1796_REG_BASE	16


struct xonar_pcm179x {
	struct xonar_generic generic;
	unsigned int dacs;
	u8 pcm1796_regs[4][5];
	unsigned int current_rate;
	bool os_128;
	bool hp_active;
	s8 hp_gain_offset;
	bool has_cs2000;
	u8 cs2000_regs[0x1f];
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
	struct xonar_pcm179x *data = chip->model_data;

	if ((chip->model.function_flags & OXYGEN_FUNCTION_2WIRE_SPI_MASK) ==
	    OXYGEN_FUNCTION_SPI)
		pcm1796_write_spi(chip, codec, reg, value);
	else
		pcm1796_write_i2c(chip, codec, reg, value);
	if ((unsigned int)(reg - PCM1796_REG_BASE)
	    < ARRAY_SIZE(data->pcm1796_regs[codec]))
		data->pcm1796_regs[codec][reg - PCM1796_REG_BASE] = value;
}

static void pcm1796_write_cached(struct oxygen *chip, unsigned int codec,
				 u8 reg, u8 value)
{
	struct xonar_pcm179x *data = chip->model_data;

	if (value != data->pcm1796_regs[codec][reg - PCM1796_REG_BASE])
		pcm1796_write(chip, codec, reg, value);
}

static void cs2000_write(struct oxygen *chip, u8 reg, u8 value)
{
	struct xonar_pcm179x *data = chip->model_data;

	oxygen_write_i2c(chip, I2C_DEVICE_CS2000, reg, value);
	data->cs2000_regs[reg] = value;
}

static void cs2000_write_cached(struct oxygen *chip, u8 reg, u8 value)
{
	struct xonar_pcm179x *data = chip->model_data;

	if (value != data->cs2000_regs[reg])
		cs2000_write(chip, reg, value);
}

static void pcm1796_registers_init(struct oxygen *chip)
{
	struct xonar_pcm179x *data = chip->model_data;
	unsigned int i;
	s8 gain_offset;

	gain_offset = data->hp_active ? data->hp_gain_offset : 0;
	for (i = 0; i < data->dacs; ++i) {
		/* set ATLD before ATL/ATR */
		pcm1796_write(chip, i, 18,
			      data->pcm1796_regs[0][18 - PCM1796_REG_BASE]);
		pcm1796_write(chip, i, 16, chip->dac_volume[i * 2]
			      + gain_offset);
		pcm1796_write(chip, i, 17, chip->dac_volume[i * 2 + 1]
			      + gain_offset);
		pcm1796_write(chip, i, 19,
			      data->pcm1796_regs[0][19 - PCM1796_REG_BASE]);
		pcm1796_write(chip, i, 20,
			      data->pcm1796_regs[0][20 - PCM1796_REG_BASE]);
		pcm1796_write(chip, i, 21, 0);
	}
}

static void pcm1796_init(struct oxygen *chip)
{
	struct xonar_pcm179x *data = chip->model_data;

	data->pcm1796_regs[0][18 - PCM1796_REG_BASE] = PCM1796_MUTE |
		PCM1796_DMF_DISABLED | PCM1796_FMT_24_LJUST | PCM1796_ATLD;
	data->pcm1796_regs[0][19 - PCM1796_REG_BASE] =
		PCM1796_FLT_SHARP | PCM1796_ATS_1;
	data->pcm1796_regs[0][20 - PCM1796_REG_BASE] = PCM1796_OS_64;
	pcm1796_registers_init(chip);
	data->current_rate = 48000;
}

static void xonar_d2_init(struct oxygen *chip)
{
	struct xonar_pcm179x *data = chip->model_data;

	data->generic.anti_pop_delay = 300;
	data->generic.output_enable_bit = GPIO_D2_OUTPUT_ENABLE;
	data->dacs = 4;

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

	pcm1796_init(chip);

	oxygen_set_bits16(chip, OXYGEN_GPIO_CONTROL,
			  GPIO_HDAV_MAGIC | GPIO_INPUT_ROUTE);
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

	data->generic.output_enable_bit = GPIO_ST_OUTPUT_ENABLE;
	data->dacs = chip->model.private_data ? 4 : 1;
	data->hp_gain_offset = 2*-18;

	pcm1796_init(chip);

	oxygen_set_bits16(chip, OXYGEN_GPIO_CONTROL,
			  GPIO_INPUT_ROUTE | GPIO_ST_HP_REAR |
			  GPIO_ST_MAGIC | GPIO_ST_HP);
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
	cs2000_write(chip, CS2000_FUN_CFG_1,
		     data->cs2000_regs[CS2000_FUN_CFG_1]);
	cs2000_write(chip, CS2000_FUN_CFG_2, 0);
	cs2000_write(chip, CS2000_GLOBAL_CFG, CS2000_EN_DEV_CFG_2);
}

static void xonar_st_init(struct oxygen *chip)
{
	struct xonar_pcm179x *data = chip->model_data;

	data->generic.anti_pop_delay = 100;
	data->has_cs2000 = 1;
	data->cs2000_regs[CS2000_FUN_CFG_1] = CS2000_REF_CLK_DIV_1;

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
	data->generic.anti_pop_delay = 800;
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
	pcm1796_registers_init(chip);
	xonar_enable_output(chip);
}

static void xonar_hdav_resume(struct oxygen *chip)
{
	struct xonar_hdav *data = chip->model_data;

	pcm1796_registers_init(chip);
	xonar_hdmi_resume(chip, &data->hdmi);
	xonar_enable_output(chip);
}

static void xonar_stx_resume(struct oxygen *chip)
{
	pcm1796_registers_init(chip);
	xonar_enable_output(chip);
}

static void xonar_st_resume(struct oxygen *chip)
{
	cs2000_registers_init(chip);
	xonar_stx_resume(chip);
}

static unsigned int mclk_from_rate(struct oxygen *chip, unsigned int rate)
{
	struct xonar_pcm179x *data = chip->model_data;

	if (rate <= 32000)
		return OXYGEN_I2S_MCLK_512;
	else if (rate <= 48000 && data->os_128)
		return OXYGEN_I2S_MCLK_512;
	else if (rate <= 96000)
		return OXYGEN_I2S_MCLK_256;
	else
		return OXYGEN_I2S_MCLK_128;
}

static unsigned int get_pcm1796_i2s_mclk(struct oxygen *chip,
					 unsigned int channel,
					 struct snd_pcm_hw_params *params)
{
	if (channel == PCM_MULTICH)
		return mclk_from_rate(chip, params_rate(params));
	else
		return oxygen_default_i2s_mclk(chip, channel, params);
}

static void update_pcm1796_oversampling(struct oxygen *chip)
{
	struct xonar_pcm179x *data = chip->model_data;
	unsigned int i;
	u8 reg;

	if (data->current_rate <= 32000)
		reg = PCM1796_OS_128;
	else if (data->current_rate <= 48000 && data->os_128)
		reg = PCM1796_OS_128;
	else if (data->current_rate <= 96000 || data->os_128)
		reg = PCM1796_OS_64;
	else
		reg = PCM1796_OS_32;
	for (i = 0; i < data->dacs; ++i)
		pcm1796_write_cached(chip, i, 20, reg);
}

static void set_pcm1796_params(struct oxygen *chip,
			       struct snd_pcm_hw_params *params)
{
	struct xonar_pcm179x *data = chip->model_data;

	data->current_rate = params_rate(params);
	update_pcm1796_oversampling(chip);
}

static void update_pcm1796_volume(struct oxygen *chip)
{
	struct xonar_pcm179x *data = chip->model_data;
	unsigned int i;
	s8 gain_offset;

	gain_offset = data->hp_active ? data->hp_gain_offset : 0;
	for (i = 0; i < data->dacs; ++i) {
		pcm1796_write_cached(chip, i, 16, chip->dac_volume[i * 2]
				     + gain_offset);
		pcm1796_write_cached(chip, i, 17, chip->dac_volume[i * 2 + 1]
				     + gain_offset);
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
		pcm1796_write_cached(chip, i, 18, value);
}

static void update_cs2000_rate(struct oxygen *chip, unsigned int rate)
{
	struct xonar_pcm179x *data = chip->model_data;
	u8 rate_mclk, reg;

	switch (rate) {
		/* XXX Why is the I2S A MCLK half the actual I2S MCLK? */
	case 32000:
		rate_mclk = OXYGEN_RATE_32000 | OXYGEN_I2S_MCLK_256;
		break;
	case 44100:
		if (data->os_128)
			rate_mclk = OXYGEN_RATE_44100 | OXYGEN_I2S_MCLK_256;
		else
			rate_mclk = OXYGEN_RATE_44100 | OXYGEN_I2S_MCLK_128;
		break;
	default: /* 48000 */
		if (data->os_128)
			rate_mclk = OXYGEN_RATE_48000 | OXYGEN_I2S_MCLK_256;
		else
			rate_mclk = OXYGEN_RATE_48000 | OXYGEN_I2S_MCLK_128;
		break;
	case 64000:
		rate_mclk = OXYGEN_RATE_32000 | OXYGEN_I2S_MCLK_256;
		break;
	case 88200:
		rate_mclk = OXYGEN_RATE_44100 | OXYGEN_I2S_MCLK_256;
		break;
	case 96000:
		rate_mclk = OXYGEN_RATE_48000 | OXYGEN_I2S_MCLK_256;
		break;
	case 176400:
		rate_mclk = OXYGEN_RATE_44100 | OXYGEN_I2S_MCLK_256;
		break;
	case 192000:
		rate_mclk = OXYGEN_RATE_48000 | OXYGEN_I2S_MCLK_256;
		break;
	}
	oxygen_write16_masked(chip, OXYGEN_I2S_A_FORMAT, rate_mclk,
			      OXYGEN_I2S_RATE_MASK | OXYGEN_I2S_MCLK_MASK);
	if ((rate_mclk & OXYGEN_I2S_MCLK_MASK) <= OXYGEN_I2S_MCLK_128)
		reg = CS2000_REF_CLK_DIV_1;
	else
		reg = CS2000_REF_CLK_DIV_2;
	cs2000_write_cached(chip, CS2000_FUN_CFG_1, reg);
}

static void set_st_params(struct oxygen *chip,
			  struct snd_pcm_hw_params *params)
{
	update_cs2000_rate(chip, params_rate(params));
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

static int rolloff_info(struct snd_kcontrol *ctl,
			struct snd_ctl_elem_info *info)
{
	static const char *const names[2] = {
		"Sharp Roll-off", "Slow Roll-off"
	};

	info->type = SNDRV_CTL_ELEM_TYPE_ENUMERATED;
	info->count = 1;
	info->value.enumerated.items = 2;
	if (info->value.enumerated.item >= 2)
		info->value.enumerated.item = 1;
	strcpy(info->value.enumerated.name, names[info->value.enumerated.item]);
	return 0;
}

static int rolloff_get(struct snd_kcontrol *ctl,
		       struct snd_ctl_elem_value *value)
{
	struct oxygen *chip = ctl->private_data;
	struct xonar_pcm179x *data = chip->model_data;

	value->value.enumerated.item[0] =
		(data->pcm1796_regs[0][19 - PCM1796_REG_BASE] &
		 PCM1796_FLT_MASK) != PCM1796_FLT_SHARP;
	return 0;
}

static int rolloff_put(struct snd_kcontrol *ctl,
		       struct snd_ctl_elem_value *value)
{
	struct oxygen *chip = ctl->private_data;
	struct xonar_pcm179x *data = chip->model_data;
	unsigned int i;
	int changed;
	u8 reg;

	mutex_lock(&chip->mutex);
	reg = data->pcm1796_regs[0][19 - PCM1796_REG_BASE];
	reg &= ~PCM1796_FLT_MASK;
	if (!value->value.enumerated.item[0])
		reg |= PCM1796_FLT_SHARP;
	else
		reg |= PCM1796_FLT_SLOW;
	changed = reg != data->pcm1796_regs[0][19 - PCM1796_REG_BASE];
	if (changed) {
		for (i = 0; i < data->dacs; ++i)
			pcm1796_write(chip, i, 19, reg);
	}
	mutex_unlock(&chip->mutex);
	return changed;
}

static const struct snd_kcontrol_new rolloff_control = {
	.iface = SNDRV_CTL_ELEM_IFACE_MIXER,
	.name = "DAC Filter Playback Enum",
	.info = rolloff_info,
	.get = rolloff_get,
	.put = rolloff_put,
};

static int os_128_info(struct snd_kcontrol *ctl, struct snd_ctl_elem_info *info)
{
	static const char *const names[2] = { "64x", "128x" };

	info->type = SNDRV_CTL_ELEM_TYPE_ENUMERATED;
	info->count = 1;
	info->value.enumerated.items = 2;
	if (info->value.enumerated.item >= 2)
		info->value.enumerated.item = 1;
	strcpy(info->value.enumerated.name, names[info->value.enumerated.item]);
	return 0;
}

static int os_128_get(struct snd_kcontrol *ctl,
		      struct snd_ctl_elem_value *value)
{
	struct oxygen *chip = ctl->private_data;
	struct xonar_pcm179x *data = chip->model_data;

	value->value.enumerated.item[0] = data->os_128;
	return 0;
}

static int os_128_put(struct snd_kcontrol *ctl,
		      struct snd_ctl_elem_value *value)
{
	struct oxygen *chip = ctl->private_data;
	struct xonar_pcm179x *data = chip->model_data;
	int changed;

	mutex_lock(&chip->mutex);
	changed = value->value.enumerated.item[0] != data->os_128;
	if (changed) {
		data->os_128 = value->value.enumerated.item[0];
		if (data->has_cs2000)
			update_cs2000_rate(chip, data->current_rate);
		oxygen_write16_masked(chip, OXYGEN_I2S_MULTICH_FORMAT,
				      mclk_from_rate(chip, data->current_rate),
				      OXYGEN_I2S_MCLK_MASK);
		update_pcm1796_oversampling(chip);
	}
	mutex_unlock(&chip->mutex);
	return changed;
}

static const struct snd_kcontrol_new os_128_control = {
	.iface = SNDRV_CTL_ELEM_IFACE_MIXER,
	.name = "DAC Oversampling Playback Enum",
	.info = os_128_info,
	.get = os_128_get,
	.put = os_128_put,
};

static const struct snd_kcontrol_new hdav_hdmi_control = {
	.iface = SNDRV_CTL_ELEM_IFACE_MIXER,
	.name = "HDMI Playback Switch",
	.info = snd_ctl_boolean_mono_info,
	.get = xonar_gpio_bit_switch_get,
	.put = xonar_gpio_bit_switch_put,
	.private_value = GPIO_HDAV_OUTPUT_ENABLE | XONAR_GPIO_BIT_INVERT,
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
	struct xonar_pcm179x *data = chip->model_data;
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
	data->hp_active = gpio & GPIO_ST_HP;
	update_pcm1796_volume(chip);
	mutex_unlock(&chip->mutex);
	return gpio != gpio_old;
}

static int st_hp_volume_offset_info(struct snd_kcontrol *ctl,
				    struct snd_ctl_elem_info *info)
{
	static const char *const names[3] = {
		"< 64 ohms", "64-300 ohms", "300-600 ohms"
	};

	info->type = SNDRV_CTL_ELEM_TYPE_ENUMERATED;
	info->count = 1;
	info->value.enumerated.items = 3;
	if (info->value.enumerated.item > 2)
		info->value.enumerated.item = 2;
	strcpy(info->value.enumerated.name, names[info->value.enumerated.item]);
	return 0;
}

static int st_hp_volume_offset_get(struct snd_kcontrol *ctl,
				   struct snd_ctl_elem_value *value)
{
	struct oxygen *chip = ctl->private_data;
	struct xonar_pcm179x *data = chip->model_data;

	mutex_lock(&chip->mutex);
	if (data->hp_gain_offset < 2*-6)
		value->value.enumerated.item[0] = 0;
	else if (data->hp_gain_offset < 0)
		value->value.enumerated.item[0] = 1;
	else
		value->value.enumerated.item[0] = 2;
	mutex_unlock(&chip->mutex);
	return 0;
}


static int st_hp_volume_offset_put(struct snd_kcontrol *ctl,
				   struct snd_ctl_elem_value *value)
{
	static const s8 offsets[] = { 2*-18, 2*-6, 0 };
	struct oxygen *chip = ctl->private_data;
	struct xonar_pcm179x *data = chip->model_data;
	s8 offset;
	int changed;

	if (value->value.enumerated.item[0] > 2)
		return -EINVAL;
	offset = offsets[value->value.enumerated.item[0]];
	mutex_lock(&chip->mutex);
	changed = offset != data->hp_gain_offset;
	if (changed) {
		data->hp_gain_offset = offset;
		update_pcm1796_volume(chip);
	}
	mutex_unlock(&chip->mutex);
	return changed;
}

static const struct snd_kcontrol_new st_controls[] = {
	{
		.iface = SNDRV_CTL_ELEM_IFACE_MIXER,
		.name = "Analog Output",
		.info = st_output_switch_info,
		.get = st_output_switch_get,
		.put = st_output_switch_put,
	},
	{
		.iface = SNDRV_CTL_ELEM_IFACE_MIXER,
		.name = "Headphones Impedance Playback Enum",
		.info = st_hp_volume_offset_info,
		.get = st_hp_volume_offset_get,
		.put = st_hp_volume_offset_put,
	},
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

static int add_pcm1796_controls(struct oxygen *chip)
{
	int err;

	err = snd_ctl_add(chip->card, snd_ctl_new1(&rolloff_control, chip));
	if (err < 0)
		return err;
	err = snd_ctl_add(chip->card, snd_ctl_new1(&os_128_control, chip));
	if (err < 0)
		return err;
	return 0;
}

static int xonar_d2_mixer_init(struct oxygen *chip)
{
	int err;

	err = snd_ctl_add(chip->card, snd_ctl_new1(&alt_switch, chip));
	if (err < 0)
		return err;
	err = add_pcm1796_controls(chip);
	if (err < 0)
		return err;
	return 0;
}

static int xonar_hdav_mixer_init(struct oxygen *chip)
{
	int err;

	err = snd_ctl_add(chip->card, snd_ctl_new1(&hdav_hdmi_control, chip));
	if (err < 0)
		return err;
	err = add_pcm1796_controls(chip);
	if (err < 0)
		return err;
	return 0;
}

static int xonar_st_mixer_init(struct oxygen *chip)
{
	unsigned int i;
	int err;

	for (i = 0; i < ARRAY_SIZE(st_controls); ++i) {
		err = snd_ctl_add(chip->card,
				  snd_ctl_new1(&st_controls[i], chip));
		if (err < 0)
			return err;
	}
	err = add_pcm1796_controls(chip);
	if (err < 0)
		return err;
	return 0;
}

static void dump_pcm1796_registers(struct oxygen *chip,
				   struct snd_info_buffer *buffer)
{
	struct xonar_pcm179x *data = chip->model_data;
	unsigned int dac, i;

	for (dac = 0; dac < data->dacs; ++dac) {
		snd_iprintf(buffer, "\nPCM1796 %u:", dac + 1);
		for (i = 0; i < 5; ++i)
			snd_iprintf(buffer, " %02x",
				    data->pcm1796_regs[dac][i]);
	}
	snd_iprintf(buffer, "\n");
}

static void dump_cs2000_registers(struct oxygen *chip,
				  struct snd_info_buffer *buffer)
{
	struct xonar_pcm179x *data = chip->model_data;
	unsigned int i;

	if (data->has_cs2000) {
		snd_iprintf(buffer, "\nCS2000:\n00:   ");
		for (i = 1; i < 0x10; ++i)
			snd_iprintf(buffer, " %02x", data->cs2000_regs[i]);
		snd_iprintf(buffer, "\n10:");
		for (i = 0x10; i < 0x1f; ++i)
			snd_iprintf(buffer, " %02x", data->cs2000_regs[i]);
		snd_iprintf(buffer, "\n");
	}
}

static void dump_st_registers(struct oxygen *chip,
			      struct snd_info_buffer *buffer)
{
	dump_pcm1796_registers(chip, buffer);
	dump_cs2000_registers(chip, buffer);
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
	.get_i2s_mclk = get_pcm1796_i2s_mclk,
	.set_dac_params = set_pcm1796_params,
	.set_adc_params = xonar_set_cs53x1_params,
	.update_dac_volume = update_pcm1796_volume,
	.update_dac_mute = update_pcm1796_mute,
	.dump_registers = dump_pcm1796_registers,
	.dac_tlv = pcm1796_db_scale,
	.model_data_size = sizeof(struct xonar_pcm179x),
	.device_config = PLAYBACK_0_TO_I2S |
			 PLAYBACK_1_TO_SPDIF |
			 CAPTURE_0_FROM_I2S_2 |
			 CAPTURE_1_FROM_SPDIF |
			 MIDI_OUTPUT |
			 MIDI_INPUT |
			 AC97_CD_INPUT,
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
	.mixer_init = xonar_hdav_mixer_init,
	.cleanup = xonar_hdav_cleanup,
	.suspend = xonar_hdav_suspend,
	.resume = xonar_hdav_resume,
	.pcm_hardware_filter = xonar_hdmi_pcm_hardware_filter,
	.get_i2s_mclk = get_pcm1796_i2s_mclk,
	.set_dac_params = set_hdav_params,
	.set_adc_params = xonar_set_cs53x1_params,
	.update_dac_volume = update_pcm1796_volume,
	.update_dac_mute = update_pcm1796_mute,
	.uart_input = xonar_hdmi_uart_input,
	.ac97_switch = xonar_line_mic_ac97_switch,
	.dump_registers = dump_pcm1796_registers,
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
	.mixer_init = xonar_st_mixer_init,
	.cleanup = xonar_st_cleanup,
	.suspend = xonar_st_suspend,
	.resume = xonar_st_resume,
	.get_i2s_mclk = get_pcm1796_i2s_mclk,
	.set_dac_params = set_st_params,
	.set_adc_params = xonar_set_cs53x1_params,
	.update_dac_volume = update_pcm1796_volume,
	.update_dac_mute = update_pcm1796_mute,
	.ac97_switch = xonar_line_mic_ac97_switch,
	.dump_registers = dump_st_registers,
	.dac_tlv = pcm1796_db_scale,
	.model_data_size = sizeof(struct xonar_pcm179x),
	.device_config = PLAYBACK_0_TO_I2S |
			 PLAYBACK_1_TO_SPDIF |
			 CAPTURE_0_FROM_I2S_2 |
			 AC97_FMIC_SWITCH,
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
	case 0x835e:
		snd_printk(KERN_ERR "the HDAV1.3 Slim is not supported\n");
		return -ENODEV;
	default:
		return -EINVAL;
	}
	return 0;
}
