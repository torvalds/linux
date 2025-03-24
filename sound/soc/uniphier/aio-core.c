// SPDX-License-Identifier: GPL-2.0
//
// Socionext UniPhier AIO ALSA common driver.
//
// Copyright (c) 2016-2018 Socionext Inc.

#include <linux/bitfield.h>
#include <linux/errno.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <sound/core.h>
#include <sound/pcm.h>
#include <sound/pcm_params.h>
#include <sound/soc.h>

#include "aio.h"
#include "aio-reg.h"

static u64 rb_cnt(u64 wr, u64 rd, u64 len)
{
	if (rd <= wr)
		return wr - rd;
	else
		return len - (rd - wr);
}

static u64 rb_cnt_to_end(u64 wr, u64 rd, u64 len)
{
	if (rd <= wr)
		return wr - rd;
	else
		return len - rd;
}

static u64 rb_space(u64 wr, u64 rd, u64 len)
{
	if (rd <= wr)
		return len - (wr - rd) - 8;
	else
		return rd - wr - 8;
}

static u64 rb_space_to_end(u64 wr, u64 rd, u64 len)
{
	if (rd > wr)
		return rd - wr - 8;
	else if (rd > 0)
		return len - wr;
	else
		return len - wr - 8;
}

u64 aio_rb_cnt(struct uniphier_aio_sub *sub)
{
	return rb_cnt(sub->wr_offs, sub->rd_offs, sub->compr_bytes);
}

u64 aio_rbt_cnt_to_end(struct uniphier_aio_sub *sub)
{
	return rb_cnt_to_end(sub->wr_offs, sub->rd_offs, sub->compr_bytes);
}

u64 aio_rb_space(struct uniphier_aio_sub *sub)
{
	return rb_space(sub->wr_offs, sub->rd_offs, sub->compr_bytes);
}

u64 aio_rb_space_to_end(struct uniphier_aio_sub *sub)
{
	return rb_space_to_end(sub->wr_offs, sub->rd_offs, sub->compr_bytes);
}

/**
 * aio_iecout_set_enable - setup IEC output via SoC glue
 * @chip: the AIO chip pointer
 * @enable: false to stop the output, true to start
 *
 * Set enabled or disabled S/PDIF signal output to out of SoC via AOnIEC pins.
 * This function need to call at driver startup.
 *
 * The regmap of SoC glue is specified by 'socionext,syscon' optional property
 * of DT. This function has no effect if no property.
 */
void aio_iecout_set_enable(struct uniphier_aio_chip *chip, bool enable)
{
	struct regmap *r = chip->regmap_sg;

	if (!r)
		return;

	regmap_write(r, SG_AOUTEN, (enable) ? ~0 : 0);
}

/**
 * aio_chip_set_pll - set frequency to audio PLL
 * @chip: the AIO chip pointer
 * @pll_id: PLL
 * @freq: frequency in Hz, 0 is ignored
 *
 * Sets frequency of audio PLL. This function can be called anytime,
 * but it takes time till PLL is locked.
 *
 * Return: Zero if successful, otherwise a negative value on error.
 */
int aio_chip_set_pll(struct uniphier_aio_chip *chip, int pll_id,
		     unsigned int freq)
{
	struct device *dev = &chip->pdev->dev;
	struct regmap *r = chip->regmap;
	int shift;
	u32 v;

	/* Not change */
	if (freq == 0)
		return 0;

	switch (pll_id) {
	case AUD_PLL_A1:
		shift = 0;
		break;
	case AUD_PLL_F1:
		shift = 1;
		break;
	case AUD_PLL_A2:
		shift = 2;
		break;
	case AUD_PLL_F2:
		shift = 3;
		break;
	default:
		dev_err(dev, "PLL(%d) not supported\n", pll_id);
		return -EINVAL;
	}

	switch (freq) {
	case 36864000:
		v = A2APLLCTR1_APLLX_36MHZ;
		break;
	case 33868800:
		v = A2APLLCTR1_APLLX_33MHZ;
		break;
	default:
		dev_err(dev, "PLL frequency not supported(%d)\n", freq);
		return -EINVAL;
	}
	chip->plls[pll_id].freq = freq;

	regmap_update_bits(r, A2APLLCTR1, A2APLLCTR1_APLLX_MASK << shift,
			   v << shift);

	return 0;
}

/**
 * aio_chip_init - initialize AIO whole settings
 * @chip: the AIO chip pointer
 *
 * Sets AIO fixed and whole device settings to AIO.
 * This function need to call once at driver startup.
 *
 * The register area that is changed by this function is shared by all
 * modules of AIO. But there is not race condition since this function
 * has always set the same initialize values.
 */
void aio_chip_init(struct uniphier_aio_chip *chip)
{
	struct regmap *r = chip->regmap;

	regmap_update_bits(r, A2APLLCTR0,
			   A2APLLCTR0_APLLXPOW_MASK,
			   A2APLLCTR0_APLLXPOW_PWON);

	regmap_update_bits(r, A2EXMCLKSEL0,
			   A2EXMCLKSEL0_EXMCLK_MASK,
			   A2EXMCLKSEL0_EXMCLK_OUTPUT);

	regmap_update_bits(r, A2AIOINPUTSEL, A2AIOINPUTSEL_RXSEL_MASK,
			   A2AIOINPUTSEL_RXSEL_PCMI1_HDMIRX1 |
			   A2AIOINPUTSEL_RXSEL_PCMI2_SIF |
			   A2AIOINPUTSEL_RXSEL_PCMI3_EVEA |
			   A2AIOINPUTSEL_RXSEL_IECI1_HDMIRX1);

	if (chip->chip_spec->addr_ext)
		regmap_update_bits(r, CDA2D_TEST, CDA2D_TEST_DDR_MODE_MASK,
				   CDA2D_TEST_DDR_MODE_EXTON0);
	else
		regmap_update_bits(r, CDA2D_TEST, CDA2D_TEST_DDR_MODE_MASK,
				   CDA2D_TEST_DDR_MODE_EXTOFF1);
}

/**
 * aio_init - initialize AIO substream
 * @sub: the AIO substream pointer
 *
 * Sets fixed settings of each AIO substreams.
 * This function need to call once at substream startup.
 *
 * Return: Zero if successful, otherwise a negative value on error.
 */
int aio_init(struct uniphier_aio_sub *sub)
{
	struct device *dev = &sub->aio->chip->pdev->dev;
	struct regmap *r = sub->aio->chip->regmap;

	regmap_write(r, A2RBNMAPCTR0(sub->swm->rb.hw),
		     MAPCTR0_EN | sub->swm->rb.map);
	regmap_write(r, A2CHNMAPCTR0(sub->swm->ch.hw),
		     MAPCTR0_EN | sub->swm->ch.map);

	switch (sub->swm->type) {
	case PORT_TYPE_I2S:
	case PORT_TYPE_SPDIF:
	case PORT_TYPE_EVE:
		if (sub->swm->dir == PORT_DIR_INPUT) {
			regmap_write(r, A2IIFNMAPCTR0(sub->swm->iif.hw),
				     MAPCTR0_EN | sub->swm->iif.map);
			regmap_write(r, A2IPORTNMAPCTR0(sub->swm->iport.hw),
				     MAPCTR0_EN | sub->swm->iport.map);
		} else {
			regmap_write(r, A2OIFNMAPCTR0(sub->swm->oif.hw),
				     MAPCTR0_EN | sub->swm->oif.map);
			regmap_write(r, A2OPORTNMAPCTR0(sub->swm->oport.hw),
				     MAPCTR0_EN | sub->swm->oport.map);
		}
		break;
	case PORT_TYPE_CONV:
		regmap_write(r, A2OIFNMAPCTR0(sub->swm->oif.hw),
			     MAPCTR0_EN | sub->swm->oif.map);
		regmap_write(r, A2OPORTNMAPCTR0(sub->swm->oport.hw),
			     MAPCTR0_EN | sub->swm->oport.map);
		regmap_write(r, A2CHNMAPCTR0(sub->swm->och.hw),
			     MAPCTR0_EN | sub->swm->och.map);
		regmap_write(r, A2IIFNMAPCTR0(sub->swm->iif.hw),
			     MAPCTR0_EN | sub->swm->iif.map);
		break;
	default:
		dev_err(dev, "Unknown port type %d.\n", sub->swm->type);
		return -EINVAL;
	}

	return 0;
}

/**
 * aio_port_reset - reset AIO port block
 * @sub: the AIO substream pointer
 *
 * Resets the digital signal input/output port block of AIO.
 */
void aio_port_reset(struct uniphier_aio_sub *sub)
{
	struct regmap *r = sub->aio->chip->regmap;

	if (sub->swm->dir == PORT_DIR_OUTPUT) {
		regmap_write(r, AOUTRSTCTR0, BIT(sub->swm->oport.map));
		regmap_write(r, AOUTRSTCTR1, BIT(sub->swm->oport.map));
	} else {
		regmap_update_bits(r, IPORTMXRSTCTR(sub->swm->iport.map),
				   IPORTMXRSTCTR_RSTPI_MASK,
				   IPORTMXRSTCTR_RSTPI_RESET);
		regmap_update_bits(r, IPORTMXRSTCTR(sub->swm->iport.map),
				   IPORTMXRSTCTR_RSTPI_MASK,
				   IPORTMXRSTCTR_RSTPI_RELEASE);
	}
}

/**
 * aio_port_set_ch - set channels of LPCM
 * @sub: the AIO substream pointer, PCM substream only
 *
 * Set suitable slot selecting to input/output port block of AIO.
 *
 * This function may return error if non-PCM substream.
 *
 * Return: Zero if successful, otherwise a negative value on error.
 */
static int aio_port_set_ch(struct uniphier_aio_sub *sub)
{
	struct regmap *r = sub->aio->chip->regmap;
	static const u32 slotsel_2ch[] = {
		0, 0, 0, 0, 0,
	};
	static const u32 slotsel_multi[] = {
		OPORTMXTYSLOTCTR_SLOTSEL_SLOT0,
		OPORTMXTYSLOTCTR_SLOTSEL_SLOT1,
		OPORTMXTYSLOTCTR_SLOTSEL_SLOT2,
		OPORTMXTYSLOTCTR_SLOTSEL_SLOT3,
		OPORTMXTYSLOTCTR_SLOTSEL_SLOT4,
	};
	u32 mode;
	const u32 *slotsel;
	int i;

	switch (params_channels(&sub->params)) {
	case 8:
	case 6:
		mode = OPORTMXTYSLOTCTR_MODE;
		slotsel = slotsel_multi;
		break;
	case 2:
		mode = 0;
		slotsel = slotsel_2ch;
		break;
	default:
		return -EINVAL;
	}

	for (i = 0; i < AUD_MAX_SLOTSEL; i++) {
		regmap_update_bits(r, OPORTMXTYSLOTCTR(sub->swm->oport.map, i),
				   OPORTMXTYSLOTCTR_MODE, mode);
		regmap_update_bits(r, OPORTMXTYSLOTCTR(sub->swm->oport.map, i),
				   OPORTMXTYSLOTCTR_SLOTSEL_MASK, slotsel[i]);
	}

	return 0;
}

/**
 * aio_port_set_rate - set sampling rate of LPCM
 * @sub: the AIO substream pointer, PCM substream only
 * @rate: Sampling rate in Hz.
 *
 * Set suitable I2S format settings to input/output port block of AIO.
 * Parameter is specified by hw_params().
 *
 * This function may return error if non-PCM substream.
 *
 * Return: Zero if successful, otherwise a negative value on error.
 */
static int aio_port_set_rate(struct uniphier_aio_sub *sub, int rate)
{
	struct regmap *r = sub->aio->chip->regmap;
	struct device *dev = &sub->aio->chip->pdev->dev;
	u32 v;

	if (sub->swm->dir == PORT_DIR_OUTPUT) {
		switch (rate) {
		case 8000:
			v = OPORTMXCTR1_FSSEL_8;
			break;
		case 11025:
			v = OPORTMXCTR1_FSSEL_11_025;
			break;
		case 12000:
			v = OPORTMXCTR1_FSSEL_12;
			break;
		case 16000:
			v = OPORTMXCTR1_FSSEL_16;
			break;
		case 22050:
			v = OPORTMXCTR1_FSSEL_22_05;
			break;
		case 24000:
			v = OPORTMXCTR1_FSSEL_24;
			break;
		case 32000:
			v = OPORTMXCTR1_FSSEL_32;
			break;
		case 44100:
			v = OPORTMXCTR1_FSSEL_44_1;
			break;
		case 48000:
			v = OPORTMXCTR1_FSSEL_48;
			break;
		case 88200:
			v = OPORTMXCTR1_FSSEL_88_2;
			break;
		case 96000:
			v = OPORTMXCTR1_FSSEL_96;
			break;
		case 176400:
			v = OPORTMXCTR1_FSSEL_176_4;
			break;
		case 192000:
			v = OPORTMXCTR1_FSSEL_192;
			break;
		default:
			dev_err(dev, "Rate not supported(%d)\n", rate);
			return -EINVAL;
		}

		regmap_update_bits(r, OPORTMXCTR1(sub->swm->oport.map),
				   OPORTMXCTR1_FSSEL_MASK, v);
	} else {
		switch (rate) {
		case 8000:
			v = IPORTMXCTR1_FSSEL_8;
			break;
		case 11025:
			v = IPORTMXCTR1_FSSEL_11_025;
			break;
		case 12000:
			v = IPORTMXCTR1_FSSEL_12;
			break;
		case 16000:
			v = IPORTMXCTR1_FSSEL_16;
			break;
		case 22050:
			v = IPORTMXCTR1_FSSEL_22_05;
			break;
		case 24000:
			v = IPORTMXCTR1_FSSEL_24;
			break;
		case 32000:
			v = IPORTMXCTR1_FSSEL_32;
			break;
		case 44100:
			v = IPORTMXCTR1_FSSEL_44_1;
			break;
		case 48000:
			v = IPORTMXCTR1_FSSEL_48;
			break;
		case 88200:
			v = IPORTMXCTR1_FSSEL_88_2;
			break;
		case 96000:
			v = IPORTMXCTR1_FSSEL_96;
			break;
		case 176400:
			v = IPORTMXCTR1_FSSEL_176_4;
			break;
		case 192000:
			v = IPORTMXCTR1_FSSEL_192;
			break;
		default:
			dev_err(dev, "Rate not supported(%d)\n", rate);
			return -EINVAL;
		}

		regmap_update_bits(r, IPORTMXCTR1(sub->swm->iport.map),
				   IPORTMXCTR1_FSSEL_MASK, v);
	}

	return 0;
}

/**
 * aio_port_set_fmt - set format of I2S data
 * @sub: the AIO substream pointer, PCM substream only
 * This parameter has no effect if substream is I2S or PCM.
 *
 * Set suitable I2S format settings to input/output port block of AIO.
 * Parameter is specified by set_fmt().
 *
 * This function may return error if non-PCM substream.
 *
 * Return: Zero if successful, otherwise a negative value on error.
 */
static int aio_port_set_fmt(struct uniphier_aio_sub *sub)
{
	struct regmap *r = sub->aio->chip->regmap;
	struct device *dev = &sub->aio->chip->pdev->dev;
	u32 v;

	if (sub->swm->dir == PORT_DIR_OUTPUT) {
		switch (sub->aio->fmt) {
		case SND_SOC_DAIFMT_LEFT_J:
			v = OPORTMXCTR1_I2SLRSEL_LEFT;
			break;
		case SND_SOC_DAIFMT_RIGHT_J:
			v = OPORTMXCTR1_I2SLRSEL_RIGHT;
			break;
		case SND_SOC_DAIFMT_I2S:
			v = OPORTMXCTR1_I2SLRSEL_I2S;
			break;
		default:
			dev_err(dev, "Format is not supported(%d)\n",
				sub->aio->fmt);
			return -EINVAL;
		}

		v |= OPORTMXCTR1_OUTBITSEL_24;
		regmap_update_bits(r, OPORTMXCTR1(sub->swm->oport.map),
				   OPORTMXCTR1_I2SLRSEL_MASK |
				   OPORTMXCTR1_OUTBITSEL_MASK, v);
	} else {
		switch (sub->aio->fmt) {
		case SND_SOC_DAIFMT_LEFT_J:
			v = IPORTMXCTR1_LRSEL_LEFT;
			break;
		case SND_SOC_DAIFMT_RIGHT_J:
			v = IPORTMXCTR1_LRSEL_RIGHT;
			break;
		case SND_SOC_DAIFMT_I2S:
			v = IPORTMXCTR1_LRSEL_I2S;
			break;
		default:
			dev_err(dev, "Format is not supported(%d)\n",
				sub->aio->fmt);
			return -EINVAL;
		}

		v |= IPORTMXCTR1_OUTBITSEL_24 |
			IPORTMXCTR1_CHSEL_ALL;
		regmap_update_bits(r, IPORTMXCTR1(sub->swm->iport.map),
				   IPORTMXCTR1_LRSEL_MASK |
				   IPORTMXCTR1_OUTBITSEL_MASK |
				   IPORTMXCTR1_CHSEL_MASK, v);
	}

	return 0;
}

/**
 * aio_port_set_clk - set clock and divider of AIO port block
 * @sub: the AIO substream pointer
 *
 * Set suitable PLL clock divider and relational settings to
 * input/output port block of AIO. Parameters are specified by
 * set_sysclk() and set_pll().
 *
 * Return: Zero if successful, otherwise a negative value on error.
 */
static int aio_port_set_clk(struct uniphier_aio_sub *sub)
{
	struct uniphier_aio_chip *chip = sub->aio->chip;
	struct device *dev = &sub->aio->chip->pdev->dev;
	struct regmap *r = sub->aio->chip->regmap;
	static const u32 v_pll[] = {
		OPORTMXCTR2_ACLKSEL_A1, OPORTMXCTR2_ACLKSEL_F1,
		OPORTMXCTR2_ACLKSEL_A2, OPORTMXCTR2_ACLKSEL_F2,
		OPORTMXCTR2_ACLKSEL_A2PLL,
		OPORTMXCTR2_ACLKSEL_RX1,
	};
	static const u32 v_div[] = {
		OPORTMXCTR2_DACCKSEL_1_2, OPORTMXCTR2_DACCKSEL_1_3,
		OPORTMXCTR2_DACCKSEL_1_1, OPORTMXCTR2_DACCKSEL_2_3,
	};
	u32 v;

	if (sub->swm->dir == PORT_DIR_OUTPUT) {
		if (sub->swm->type == PORT_TYPE_I2S) {
			if (sub->aio->pll_out >= ARRAY_SIZE(v_pll)) {
				dev_err(dev, "PLL(%d) is invalid\n",
					sub->aio->pll_out);
				return -EINVAL;
			}
			if (sub->aio->plldiv >= ARRAY_SIZE(v_div)) {
				dev_err(dev, "PLL divider(%d) is invalid\n",
					sub->aio->plldiv);
				return -EINVAL;
			}

			v = v_pll[sub->aio->pll_out] |
				OPORTMXCTR2_MSSEL_MASTER |
				v_div[sub->aio->plldiv];

			switch (chip->plls[sub->aio->pll_out].freq) {
			case 0:
			case 36864000:
			case 33868800:
				v |= OPORTMXCTR2_EXTLSIFSSEL_36;
				break;
			default:
				v |= OPORTMXCTR2_EXTLSIFSSEL_24;
				break;
			}
		} else if (sub->swm->type == PORT_TYPE_EVE) {
			v = OPORTMXCTR2_ACLKSEL_A2PLL |
				OPORTMXCTR2_MSSEL_MASTER |
				OPORTMXCTR2_EXTLSIFSSEL_36 |
				OPORTMXCTR2_DACCKSEL_1_2;
		} else if (sub->swm->type == PORT_TYPE_SPDIF) {
			if (sub->aio->pll_out >= ARRAY_SIZE(v_pll)) {
				dev_err(dev, "PLL(%d) is invalid\n",
					sub->aio->pll_out);
				return -EINVAL;
			}
			v = v_pll[sub->aio->pll_out] |
				OPORTMXCTR2_MSSEL_MASTER |
				OPORTMXCTR2_DACCKSEL_1_2;

			switch (chip->plls[sub->aio->pll_out].freq) {
			case 0:
			case 36864000:
			case 33868800:
				v |= OPORTMXCTR2_EXTLSIFSSEL_36;
				break;
			default:
				v |= OPORTMXCTR2_EXTLSIFSSEL_24;
				break;
			}
		} else {
			v = OPORTMXCTR2_ACLKSEL_A1 |
				OPORTMXCTR2_MSSEL_MASTER |
				OPORTMXCTR2_EXTLSIFSSEL_36 |
				OPORTMXCTR2_DACCKSEL_1_2;
		}
		regmap_write(r, OPORTMXCTR2(sub->swm->oport.map), v);
	} else {
		v = IPORTMXCTR2_ACLKSEL_A1 |
			IPORTMXCTR2_MSSEL_SLAVE |
			IPORTMXCTR2_EXTLSIFSSEL_36 |
			IPORTMXCTR2_DACCKSEL_1_2;
		regmap_write(r, IPORTMXCTR2(sub->swm->iport.map), v);
	}

	return 0;
}

/**
 * aio_port_set_param - set parameters of AIO port block
 * @sub: the AIO substream pointer
 * @pass_through: Zero if sound data is LPCM, otherwise if data is not LPCM.
 * This parameter has no effect if substream is I2S or PCM.
 * @params: hardware parameters of ALSA
 *
 * Set suitable setting to input/output port block of AIO to process the
 * specified in params.
 *
 * Return: Zero if successful, otherwise a negative value on error.
 */
int aio_port_set_param(struct uniphier_aio_sub *sub, int pass_through,
		       const struct snd_pcm_hw_params *params)
{
	struct regmap *r = sub->aio->chip->regmap;
	unsigned int rate;
	u32 v;
	int ret;

	if (!pass_through) {
		if (sub->swm->type == PORT_TYPE_EVE ||
		    sub->swm->type == PORT_TYPE_CONV) {
			rate = 48000;
		} else {
			rate = params_rate(params);
		}

		ret = aio_port_set_ch(sub);
		if (ret)
			return ret;

		ret = aio_port_set_rate(sub, rate);
		if (ret)
			return ret;

		ret = aio_port_set_fmt(sub);
		if (ret)
			return ret;
	}

	ret = aio_port_set_clk(sub);
	if (ret)
		return ret;

	if (sub->swm->dir == PORT_DIR_OUTPUT) {
		if (pass_through)
			v = OPORTMXCTR3_SRCSEL_STREAM |
				OPORTMXCTR3_VALID_STREAM;
		else
			v = OPORTMXCTR3_SRCSEL_PCM |
				OPORTMXCTR3_VALID_PCM;

		v |= OPORTMXCTR3_IECTHUR_IECOUT |
			OPORTMXCTR3_PMSEL_PAUSE |
			OPORTMXCTR3_PMSW_MUTE_OFF;
		regmap_write(r, OPORTMXCTR3(sub->swm->oport.map), v);
	} else {
		regmap_write(r, IPORTMXACLKSEL0EX(sub->swm->iport.map),
			     IPORTMXACLKSEL0EX_ACLKSEL0EX_INTERNAL);
		regmap_write(r, IPORTMXEXNOE(sub->swm->iport.map),
			     IPORTMXEXNOE_PCMINOE_INPUT);
	}

	return 0;
}

/**
 * aio_port_set_enable - start or stop of AIO port block
 * @sub: the AIO substream pointer
 * @enable: zero to stop the block, otherwise to start
 *
 * Start or stop the signal input/output port block of AIO.
 */
void aio_port_set_enable(struct uniphier_aio_sub *sub, int enable)
{
	struct regmap *r = sub->aio->chip->regmap;

	if (sub->swm->dir == PORT_DIR_OUTPUT) {
		regmap_write(r, OPORTMXPATH(sub->swm->oport.map),
			     sub->swm->oif.map);

		regmap_update_bits(r, OPORTMXMASK(sub->swm->oport.map),
				   OPORTMXMASK_IUDXMSK_MASK |
				   OPORTMXMASK_IUXCKMSK_MASK |
				   OPORTMXMASK_DXMSK_MASK |
				   OPORTMXMASK_XCKMSK_MASK,
				   OPORTMXMASK_IUDXMSK_OFF |
				   OPORTMXMASK_IUXCKMSK_OFF |
				   OPORTMXMASK_DXMSK_OFF |
				   OPORTMXMASK_XCKMSK_OFF);

		if (enable)
			regmap_write(r, AOUTENCTR0, BIT(sub->swm->oport.map));
		else
			regmap_write(r, AOUTENCTR1, BIT(sub->swm->oport.map));
	} else {
		regmap_update_bits(r, IPORTMXMASK(sub->swm->iport.map),
				   IPORTMXMASK_IUXCKMSK_MASK |
				   IPORTMXMASK_XCKMSK_MASK,
				   IPORTMXMASK_IUXCKMSK_OFF |
				   IPORTMXMASK_XCKMSK_OFF);

		if (enable)
			regmap_update_bits(r,
					   IPORTMXCTR2(sub->swm->iport.map),
					   IPORTMXCTR2_REQEN_MASK,
					   IPORTMXCTR2_REQEN_ENABLE);
		else
			regmap_update_bits(r,
					   IPORTMXCTR2(sub->swm->iport.map),
					   IPORTMXCTR2_REQEN_MASK,
					   IPORTMXCTR2_REQEN_DISABLE);
	}
}

/**
 * aio_port_get_volume - get volume of AIO port block
 * @sub: the AIO substream pointer
 *
 * Return: current volume, range is 0x0000 - 0xffff
 */
int aio_port_get_volume(struct uniphier_aio_sub *sub)
{
	struct regmap *r = sub->aio->chip->regmap;
	u32 v;

	regmap_read(r, OPORTMXTYVOLGAINSTATUS(sub->swm->oport.map, 0), &v);

	return FIELD_GET(OPORTMXTYVOLGAINSTATUS_CUR_MASK, v);
}

/**
 * aio_port_set_volume - set volume of AIO port block
 * @sub: the AIO substream pointer
 * @vol: target volume, range is 0x0000 - 0xffff.
 *
 * Change digital volume and perfome fade-out/fade-in effect for specified
 * output slot of port. Gained PCM value can calculate as the following:
 *   Gained = Original * vol / 0x4000
 */
void aio_port_set_volume(struct uniphier_aio_sub *sub, int vol)
{
	struct regmap *r = sub->aio->chip->regmap;
	int oport_map = sub->swm->oport.map;
	int cur, diff, slope = 0, fs;

	if (sub->swm->dir == PORT_DIR_INPUT)
		return;

	cur = aio_port_get_volume(sub);
	diff = abs(vol - cur);
	fs = params_rate(&sub->params);
	if (fs)
		slope = diff / AUD_VOL_FADE_TIME * 1000 / fs;
	slope = max(1, slope);

	regmap_update_bits(r, OPORTMXTYVOLPARA1(oport_map, 0),
			   OPORTMXTYVOLPARA1_SLOPEU_MASK, slope << 16);
	regmap_update_bits(r, OPORTMXTYVOLPARA2(oport_map, 0),
			   OPORTMXTYVOLPARA2_TARGET_MASK, vol);

	if (cur < vol)
		regmap_update_bits(r, OPORTMXTYVOLPARA2(oport_map, 0),
				   OPORTMXTYVOLPARA2_FADE_MASK,
				   OPORTMXTYVOLPARA2_FADE_FADEIN);
	else
		regmap_update_bits(r, OPORTMXTYVOLPARA2(oport_map, 0),
				   OPORTMXTYVOLPARA2_FADE_MASK,
				   OPORTMXTYVOLPARA2_FADE_FADEOUT);

	regmap_write(r, AOUTFADECTR0, BIT(oport_map));
}

/**
 * aio_if_set_param - set parameters of AIO DMA I/F block
 * @sub: the AIO substream pointer
 * @pass_through: Zero if sound data is LPCM, otherwise if data is not LPCM.
 * This parameter has no effect if substream is I2S or PCM.
 *
 * Set suitable setting to DMA interface block of AIO to process the
 * specified in settings.
 *
 * Return: Zero if successful, otherwise a negative value on error.
 */
int aio_if_set_param(struct uniphier_aio_sub *sub, int pass_through)
{
	struct regmap *r = sub->aio->chip->regmap;
	u32 memfmt, v;

	if (sub->swm->dir == PORT_DIR_OUTPUT) {
		if (pass_through) {
			v = PBOUTMXCTR0_ENDIAN_0123 |
				PBOUTMXCTR0_MEMFMT_STREAM;
		} else {
			switch (params_channels(&sub->params)) {
			case 2:
				memfmt = PBOUTMXCTR0_MEMFMT_2CH;
				break;
			case 6:
				memfmt = PBOUTMXCTR0_MEMFMT_6CH;
				break;
			case 8:
				memfmt = PBOUTMXCTR0_MEMFMT_8CH;
				break;
			default:
				return -EINVAL;
			}
			v = PBOUTMXCTR0_ENDIAN_3210 | memfmt;
		}

		regmap_write(r, PBOUTMXCTR0(sub->swm->oif.map), v);
		regmap_write(r, PBOUTMXCTR1(sub->swm->oif.map), 0);
	} else {
		regmap_write(r, PBINMXCTR(sub->swm->iif.map),
			     PBINMXCTR_NCONNECT_CONNECT |
			     PBINMXCTR_INOUTSEL_IN |
			     (sub->swm->iport.map << PBINMXCTR_PBINSEL_SHIFT) |
			     PBINMXCTR_ENDIAN_3210 |
			     PBINMXCTR_MEMFMT_D0);
	}

	return 0;
}

/**
 * aio_oport_set_stream_type - set parameters of AIO playback port block
 * @sub: the AIO substream pointer
 * @pc: Pc type of IEC61937
 *
 * Set special setting to output port block of AIO to output the stream
 * via S/PDIF.
 *
 * Return: Zero if successful, otherwise a negative value on error.
 */
int aio_oport_set_stream_type(struct uniphier_aio_sub *sub,
			      enum IEC61937_PC pc)
{
	struct regmap *r = sub->aio->chip->regmap;
	u32 repet = 0, pause = OPORTMXPAUDAT_PAUSEPC_CMN;
	int ret;

	switch (pc) {
	case IEC61937_PC_AC3:
		repet = OPORTMXREPET_STRLENGTH_AC3 |
			OPORTMXREPET_PMLENGTH_AC3;
		pause |= OPORTMXPAUDAT_PAUSEPD_AC3;
		break;
	case IEC61937_PC_MPA:
		repet = OPORTMXREPET_STRLENGTH_MPA |
			OPORTMXREPET_PMLENGTH_MPA;
		pause |= OPORTMXPAUDAT_PAUSEPD_MPA;
		break;
	case IEC61937_PC_MP3:
		repet = OPORTMXREPET_STRLENGTH_MP3 |
			OPORTMXREPET_PMLENGTH_MP3;
		pause |= OPORTMXPAUDAT_PAUSEPD_MP3;
		break;
	case IEC61937_PC_DTS1:
		repet = OPORTMXREPET_STRLENGTH_DTS1 |
			OPORTMXREPET_PMLENGTH_DTS1;
		pause |= OPORTMXPAUDAT_PAUSEPD_DTS1;
		break;
	case IEC61937_PC_DTS2:
		repet = OPORTMXREPET_STRLENGTH_DTS2 |
			OPORTMXREPET_PMLENGTH_DTS2;
		pause |= OPORTMXPAUDAT_PAUSEPD_DTS2;
		break;
	case IEC61937_PC_DTS3:
		repet = OPORTMXREPET_STRLENGTH_DTS3 |
			OPORTMXREPET_PMLENGTH_DTS3;
		pause |= OPORTMXPAUDAT_PAUSEPD_DTS3;
		break;
	case IEC61937_PC_AAC:
		repet = OPORTMXREPET_STRLENGTH_AAC |
			OPORTMXREPET_PMLENGTH_AAC;
		pause |= OPORTMXPAUDAT_PAUSEPD_AAC;
		break;
	case IEC61937_PC_PAUSE:
		/* Do nothing */
		break;
	}

	ret = regmap_write(r, OPORTMXREPET(sub->swm->oport.map), repet);
	if (ret)
		return ret;
	
	ret = regmap_write(r, OPORTMXPAUDAT(sub->swm->oport.map), pause);
	if (ret)
		return ret;

	return 0;
}

/**
 * aio_src_reset - reset AIO SRC block
 * @sub: the AIO substream pointer
 *
 * Resets the digital signal input/output port with sampling rate converter
 * block of AIO.
 * This function has no effect if substream is not supported rate converter.
 */
void aio_src_reset(struct uniphier_aio_sub *sub)
{
	struct regmap *r = sub->aio->chip->regmap;

	if (sub->swm->dir != PORT_DIR_OUTPUT)
		return;

	regmap_write(r, AOUTSRCRSTCTR0, BIT(sub->swm->oport.map));
	regmap_write(r, AOUTSRCRSTCTR1, BIT(sub->swm->oport.map));
}

/**
 * aio_src_set_param - set parameters of AIO SRC block
 * @sub: the AIO substream pointer
 * @params: hardware parameters of ALSA
 *
 * Set suitable setting to input/output port with sampling rate converter
 * block of AIO to process the specified in params.
 * This function has no effect if substream is not supported rate converter.
 *
 * Return: Zero if successful, otherwise a negative value on error.
 */
int aio_src_set_param(struct uniphier_aio_sub *sub,
		      const struct snd_pcm_hw_params *params)
{
	struct regmap *r = sub->aio->chip->regmap;
	u32 v;
	int ret;

	if (sub->swm->dir != PORT_DIR_OUTPUT)
		return 0;

	ret = regmap_write(r, OPORTMXSRC1CTR(sub->swm->oport.map),
		     OPORTMXSRC1CTR_THMODE_SRC |
		     OPORTMXSRC1CTR_SRCPATH_CALC |
		     OPORTMXSRC1CTR_SYNC_ASYNC |
		     OPORTMXSRC1CTR_FSIIPSEL_INNER |
		     OPORTMXSRC1CTR_FSISEL_ACLK);
	if (ret)
		return ret;

	switch (params_rate(params)) {
	default:
	case 48000:
		v = OPORTMXRATE_I_ACLKSEL_APLLA1 |
			OPORTMXRATE_I_MCKSEL_36 |
			OPORTMXRATE_I_FSSEL_48;
		break;
	case 44100:
		v = OPORTMXRATE_I_ACLKSEL_APLLA2 |
			OPORTMXRATE_I_MCKSEL_33 |
			OPORTMXRATE_I_FSSEL_44_1;
		break;
	case 32000:
		v = OPORTMXRATE_I_ACLKSEL_APLLA1 |
			OPORTMXRATE_I_MCKSEL_36 |
			OPORTMXRATE_I_FSSEL_32;
		break;
	}


	ret = regmap_write(r, OPORTMXRATE_I(sub->swm->oport.map),
		     v | OPORTMXRATE_I_ACLKSRC_APLL |
		     OPORTMXRATE_I_LRCKSTP_STOP);
	if (ret)
		return ret;

	ret = regmap_update_bits(r, OPORTMXRATE_I(sub->swm->oport.map),
			   OPORTMXRATE_I_LRCKSTP_MASK,
			   OPORTMXRATE_I_LRCKSTP_START);
	if (ret)
		return ret;

	return 0;
}

int aio_srcif_set_param(struct uniphier_aio_sub *sub)
{
	struct regmap *r = sub->aio->chip->regmap;

	regmap_write(r, PBINMXCTR(sub->swm->iif.map),
		     PBINMXCTR_NCONNECT_CONNECT |
		     PBINMXCTR_INOUTSEL_OUT |
		     (sub->swm->oport.map << PBINMXCTR_PBINSEL_SHIFT) |
		     PBINMXCTR_ENDIAN_3210 |
		     PBINMXCTR_MEMFMT_D0);

	return 0;
}

int aio_srcch_set_param(struct uniphier_aio_sub *sub)
{
	struct regmap *r = sub->aio->chip->regmap;

	regmap_write(r, CDA2D_CHMXCTRL1(sub->swm->och.map),
		     CDA2D_CHMXCTRL1_INDSIZE_INFINITE);

	regmap_write(r, CDA2D_CHMXSRCAMODE(sub->swm->och.map),
		     CDA2D_CHMXAMODE_ENDIAN_3210 |
		     CDA2D_CHMXAMODE_AUPDT_FIX |
		     CDA2D_CHMXAMODE_TYPE_NORMAL);

	regmap_write(r, CDA2D_CHMXDSTAMODE(sub->swm->och.map),
		     CDA2D_CHMXAMODE_ENDIAN_3210 |
		     CDA2D_CHMXAMODE_AUPDT_INC |
		     CDA2D_CHMXAMODE_TYPE_RING |
		     (sub->swm->och.map << CDA2D_CHMXAMODE_RSSEL_SHIFT));

	return 0;
}

void aio_srcch_set_enable(struct uniphier_aio_sub *sub, int enable)
{
	struct regmap *r = sub->aio->chip->regmap;
	u32 v;

	if (enable)
		v = CDA2D_STRT0_STOP_START;
	else
		v = CDA2D_STRT0_STOP_STOP;

	regmap_write(r, CDA2D_STRT0,
		     v | BIT(sub->swm->och.map));
}

int aiodma_ch_set_param(struct uniphier_aio_sub *sub)
{
	struct regmap *r = sub->aio->chip->regmap;
	u32 v;

	regmap_write(r, CDA2D_CHMXCTRL1(sub->swm->ch.map),
		     CDA2D_CHMXCTRL1_INDSIZE_INFINITE);

	v = CDA2D_CHMXAMODE_ENDIAN_3210 |
		CDA2D_CHMXAMODE_AUPDT_INC |
		CDA2D_CHMXAMODE_TYPE_NORMAL |
		(sub->swm->rb.map << CDA2D_CHMXAMODE_RSSEL_SHIFT);
	if (sub->swm->dir == PORT_DIR_OUTPUT)
		regmap_write(r, CDA2D_CHMXSRCAMODE(sub->swm->ch.map), v);
	else
		regmap_write(r, CDA2D_CHMXDSTAMODE(sub->swm->ch.map), v);

	return 0;
}

void aiodma_ch_set_enable(struct uniphier_aio_sub *sub, int enable)
{
	struct regmap *r = sub->aio->chip->regmap;

	if (enable) {
		regmap_write(r, CDA2D_STRT0,
			     CDA2D_STRT0_STOP_START | BIT(sub->swm->ch.map));

		regmap_update_bits(r, INTRBIM(0),
				   BIT(sub->swm->rb.map),
				   BIT(sub->swm->rb.map));
	} else {
		regmap_write(r, CDA2D_STRT0,
			     CDA2D_STRT0_STOP_STOP | BIT(sub->swm->ch.map));

		regmap_update_bits(r, INTRBIM(0),
				   BIT(sub->swm->rb.map),
				   0);
	}
}

static u64 aiodma_rb_get_rp(struct uniphier_aio_sub *sub)
{
	struct regmap *r = sub->aio->chip->regmap;
	u32 pos_u, pos_l;
	int i;

	regmap_write(r, CDA2D_RDPTRLOAD,
		     CDA2D_RDPTRLOAD_LSFLAG_STORE | BIT(sub->swm->rb.map));
	/* Wait for setup */
	for (i = 0; i < 6; i++)
		regmap_read(r, CDA2D_RBMXRDPTR(sub->swm->rb.map), &pos_l);

	regmap_read(r, CDA2D_RBMXRDPTR(sub->swm->rb.map), &pos_l);
	regmap_read(r, CDA2D_RBMXRDPTRU(sub->swm->rb.map), &pos_u);
	pos_u = FIELD_GET(CDA2D_RBMXPTRU_PTRU_MASK, pos_u);

	return ((u64)pos_u << 32) | pos_l;
}

static void aiodma_rb_set_rp(struct uniphier_aio_sub *sub, u64 pos)
{
	struct regmap *r = sub->aio->chip->regmap;
	u32 tmp;
	int i;

	regmap_write(r, CDA2D_RBMXRDPTR(sub->swm->rb.map), (u32)pos);
	regmap_write(r, CDA2D_RBMXRDPTRU(sub->swm->rb.map), (u32)(pos >> 32));
	regmap_write(r, CDA2D_RDPTRLOAD, BIT(sub->swm->rb.map));
	/* Wait for setup */
	for (i = 0; i < 6; i++)
		regmap_read(r, CDA2D_RBMXRDPTR(sub->swm->rb.map), &tmp);
}

static u64 aiodma_rb_get_wp(struct uniphier_aio_sub *sub)
{
	struct regmap *r = sub->aio->chip->regmap;
	u32 pos_u, pos_l;
	int i;

	regmap_write(r, CDA2D_WRPTRLOAD,
		     CDA2D_WRPTRLOAD_LSFLAG_STORE | BIT(sub->swm->rb.map));
	/* Wait for setup */
	for (i = 0; i < 6; i++)
		regmap_read(r, CDA2D_RBMXWRPTR(sub->swm->rb.map), &pos_l);

	regmap_read(r, CDA2D_RBMXWRPTR(sub->swm->rb.map), &pos_l);
	regmap_read(r, CDA2D_RBMXWRPTRU(sub->swm->rb.map), &pos_u);
	pos_u = FIELD_GET(CDA2D_RBMXPTRU_PTRU_MASK, pos_u);

	return ((u64)pos_u << 32) | pos_l;
}

static void aiodma_rb_set_wp(struct uniphier_aio_sub *sub, u64 pos)
{
	struct regmap *r = sub->aio->chip->regmap;
	u32 tmp;
	int i;

	regmap_write(r, CDA2D_RBMXWRPTR(sub->swm->rb.map),
		     lower_32_bits(pos));
	regmap_write(r, CDA2D_RBMXWRPTRU(sub->swm->rb.map),
		     upper_32_bits(pos));
	regmap_write(r, CDA2D_WRPTRLOAD, BIT(sub->swm->rb.map));
	/* Wait for setup */
	for (i = 0; i < 6; i++)
		regmap_read(r, CDA2D_RBMXWRPTR(sub->swm->rb.map), &tmp);
}

int aiodma_rb_set_threshold(struct uniphier_aio_sub *sub, u64 size, u32 th)
{
	struct regmap *r = sub->aio->chip->regmap;

	if (size <= th)
		return -EINVAL;

	regmap_write(r, CDA2D_RBMXBTH(sub->swm->rb.map), th);
	regmap_write(r, CDA2D_RBMXRTH(sub->swm->rb.map), th);

	return 0;
}

int aiodma_rb_set_buffer(struct uniphier_aio_sub *sub, u64 start, u64 end,
			 int period)
{
	struct regmap *r = sub->aio->chip->regmap;
	u64 size = end - start;
	int ret;

	if (end < start || period < 0)
		return -EINVAL;

	regmap_write(r, CDA2D_RBMXCNFG(sub->swm->rb.map), 0);
	regmap_write(r, CDA2D_RBMXBGNADRS(sub->swm->rb.map),
		     lower_32_bits(start));
	regmap_write(r, CDA2D_RBMXBGNADRSU(sub->swm->rb.map),
		     upper_32_bits(start));
	regmap_write(r, CDA2D_RBMXENDADRS(sub->swm->rb.map),
		     lower_32_bits(end));
	regmap_write(r, CDA2D_RBMXENDADRSU(sub->swm->rb.map),
		     upper_32_bits(end));

	regmap_write(r, CDA2D_RBADRSLOAD, BIT(sub->swm->rb.map));

	ret = aiodma_rb_set_threshold(sub, size, 2 * period);
	if (ret)
		return ret;

	if (sub->swm->dir == PORT_DIR_OUTPUT) {
		aiodma_rb_set_rp(sub, start);
		aiodma_rb_set_wp(sub, end - period);

		regmap_update_bits(r, CDA2D_RBMXIE(sub->swm->rb.map),
				   CDA2D_RBMXIX_SPACE,
				   CDA2D_RBMXIX_SPACE);
	} else {
		aiodma_rb_set_rp(sub, end - period);
		aiodma_rb_set_wp(sub, start);

		regmap_update_bits(r, CDA2D_RBMXIE(sub->swm->rb.map),
				   CDA2D_RBMXIX_REMAIN,
				   CDA2D_RBMXIX_REMAIN);
	}

	sub->threshold = 2 * period;
	sub->rd_offs = 0;
	sub->wr_offs = 0;
	sub->rd_org = 0;
	sub->wr_org = 0;
	sub->rd_total = 0;
	sub->wr_total = 0;

	return 0;
}

void aiodma_rb_sync(struct uniphier_aio_sub *sub, u64 start, u64 size,
		    int period)
{
	if (sub->swm->dir == PORT_DIR_OUTPUT) {
		sub->rd_offs = aiodma_rb_get_rp(sub) - start;

		if (sub->use_mmap) {
			sub->threshold = 2 * period;
			aiodma_rb_set_threshold(sub, size, 2 * period);

			sub->wr_offs = sub->rd_offs - period;
			if (sub->rd_offs < period)
				sub->wr_offs += size;
		}
		aiodma_rb_set_wp(sub, sub->wr_offs + start);
	} else {
		sub->wr_offs = aiodma_rb_get_wp(sub) - start;

		if (sub->use_mmap) {
			sub->threshold = 2 * period;
			aiodma_rb_set_threshold(sub, size, 2 * period);

			sub->rd_offs = sub->wr_offs - period;
			if (sub->wr_offs < period)
				sub->rd_offs += size;
		}
		aiodma_rb_set_rp(sub, sub->rd_offs + start);
	}

	sub->rd_total += sub->rd_offs - sub->rd_org;
	if (sub->rd_offs < sub->rd_org)
		sub->rd_total += size;
	sub->wr_total += sub->wr_offs - sub->wr_org;
	if (sub->wr_offs < sub->wr_org)
		sub->wr_total += size;

	sub->rd_org = sub->rd_offs;
	sub->wr_org = sub->wr_offs;
}

bool aiodma_rb_is_irq(struct uniphier_aio_sub *sub)
{
	struct regmap *r = sub->aio->chip->regmap;
	u32 ir;

	regmap_read(r, CDA2D_RBMXIR(sub->swm->rb.map), &ir);

	if (sub->swm->dir == PORT_DIR_OUTPUT)
		return !!(ir & CDA2D_RBMXIX_SPACE);
	else
		return !!(ir & CDA2D_RBMXIX_REMAIN);
}

void aiodma_rb_clear_irq(struct uniphier_aio_sub *sub)
{
	struct regmap *r = sub->aio->chip->regmap;

	if (sub->swm->dir == PORT_DIR_OUTPUT)
		regmap_write(r, CDA2D_RBMXIR(sub->swm->rb.map),
			     CDA2D_RBMXIX_SPACE);
	else
		regmap_write(r, CDA2D_RBMXIR(sub->swm->rb.map),
			     CDA2D_RBMXIX_REMAIN);
}
