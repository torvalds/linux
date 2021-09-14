// SPDX-License-Identifier: GPL-2.0
//
// cs35l41.c -- CS35l41 ALSA SoC audio driver
//
// Copyright 2017-2021 Cirrus Logic, Inc.
//
// Author: David Rhodes <david.rhodes@cirrus.com>

#include <linux/delay.h>
#include <linux/err.h>
#include <linux/init.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/moduleparam.h>
#include <linux/of_device.h>
#include <linux/property.h>
#include <linux/slab.h>
#include <sound/initval.h>
#include <sound/pcm.h>
#include <sound/pcm_params.h>
#include <sound/soc.h>
#include <sound/soc-dapm.h>
#include <sound/tlv.h>

#include "cs35l41.h"

static const char * const cs35l41_supplies[CS35L41_NUM_SUPPLIES] = {
	"VA",
	"VP",
};

struct cs35l41_pll_sysclk_config {
	int freq;
	int clk_cfg;
};

static const struct cs35l41_pll_sysclk_config cs35l41_pll_sysclk[] = {
	{ 32768,	0x00 },
	{ 8000,		0x01 },
	{ 11025,	0x02 },
	{ 12000,	0x03 },
	{ 16000,	0x04 },
	{ 22050,	0x05 },
	{ 24000,	0x06 },
	{ 32000,	0x07 },
	{ 44100,	0x08 },
	{ 48000,	0x09 },
	{ 88200,	0x0A },
	{ 96000,	0x0B },
	{ 128000,	0x0C },
	{ 176400,	0x0D },
	{ 192000,	0x0E },
	{ 256000,	0x0F },
	{ 352800,	0x10 },
	{ 384000,	0x11 },
	{ 512000,	0x12 },
	{ 705600,	0x13 },
	{ 750000,	0x14 },
	{ 768000,	0x15 },
	{ 1000000,	0x16 },
	{ 1024000,	0x17 },
	{ 1200000,	0x18 },
	{ 1411200,	0x19 },
	{ 1500000,	0x1A },
	{ 1536000,	0x1B },
	{ 2000000,	0x1C },
	{ 2048000,	0x1D },
	{ 2400000,	0x1E },
	{ 2822400,	0x1F },
	{ 3000000,	0x20 },
	{ 3072000,	0x21 },
	{ 3200000,	0x22 },
	{ 4000000,	0x23 },
	{ 4096000,	0x24 },
	{ 4800000,	0x25 },
	{ 5644800,	0x26 },
	{ 6000000,	0x27 },
	{ 6144000,	0x28 },
	{ 6250000,	0x29 },
	{ 6400000,	0x2A },
	{ 6500000,	0x2B },
	{ 6750000,	0x2C },
	{ 7526400,	0x2D },
	{ 8000000,	0x2E },
	{ 8192000,	0x2F },
	{ 9600000,	0x30 },
	{ 11289600,	0x31 },
	{ 12000000,	0x32 },
	{ 12288000,	0x33 },
	{ 12500000,	0x34 },
	{ 12800000,	0x35 },
	{ 13000000,	0x36 },
	{ 13500000,	0x37 },
	{ 19200000,	0x38 },
	{ 22579200,	0x39 },
	{ 24000000,	0x3A },
	{ 24576000,	0x3B },
	{ 25000000,	0x3C },
	{ 25600000,	0x3D },
	{ 26000000,	0x3E },
	{ 27000000,	0x3F },
};

struct cs35l41_fs_mon_config {
	int freq;
	unsigned int fs1;
	unsigned int fs2;
};

static const struct cs35l41_fs_mon_config cs35l41_fs_mon[] = {
	{ 32768,	2254,	3754 },
	{ 8000,		9220,	15364 },
	{ 11025,	6148,	10244 },
	{ 12000,	6148,	10244 },
	{ 16000,	4612,	7684 },
	{ 22050,	3076,	5124 },
	{ 24000,	3076,	5124 },
	{ 32000,	2308,	3844 },
	{ 44100,	1540,	2564 },
	{ 48000,	1540,	2564 },
	{ 88200,	772,	1284 },
	{ 96000,	772,	1284 },
	{ 128000,	580,	964 },
	{ 176400,	388,	644 },
	{ 192000,	388,	644 },
	{ 256000,	292,	484 },
	{ 352800,	196,	324 },
	{ 384000,	196,	324 },
	{ 512000,	148,	244 },
	{ 705600,	100,	164 },
	{ 750000,	100,	164 },
	{ 768000,	100,	164 },
	{ 1000000,	76,	124 },
	{ 1024000,	76,	124 },
	{ 1200000,	64,	104 },
	{ 1411200,	52,	84 },
	{ 1500000,	52,	84 },
	{ 1536000,	52,	84 },
	{ 2000000,	40,	64 },
	{ 2048000,	40,	64 },
	{ 2400000,	34,	54 },
	{ 2822400,	28,	44 },
	{ 3000000,	28,	44 },
	{ 3072000,	28,	44 },
	{ 3200000,	27,	42 },
	{ 4000000,	22,	34 },
	{ 4096000,	22,	34 },
	{ 4800000,	19,	29 },
	{ 5644800,	16,	24 },
	{ 6000000,	16,	24 },
	{ 6144000,	16,	24 },
};

static const unsigned char cs35l41_bst_k1_table[4][5] = {
	{ 0x24, 0x32, 0x32, 0x4F, 0x57 },
	{ 0x24, 0x32, 0x32, 0x4F, 0x57 },
	{ 0x40, 0x32, 0x32, 0x4F, 0x57 },
	{ 0x40, 0x32, 0x32, 0x4F, 0x57 }
};

static const unsigned char cs35l41_bst_k2_table[4][5] = {
	{ 0x24, 0x49, 0x66, 0xA3, 0xEA },
	{ 0x24, 0x49, 0x66, 0xA3, 0xEA },
	{ 0x48, 0x49, 0x66, 0xA3, 0xEA },
	{ 0x48, 0x49, 0x66, 0xA3, 0xEA }
};

static const unsigned char cs35l41_bst_slope_table[4] = {
					0x75, 0x6B, 0x3B, 0x28};

static int cs35l41_get_fs_mon_config_index(int freq)
{
	int i;

	for (i = 0; i < ARRAY_SIZE(cs35l41_fs_mon); i++) {
		if (cs35l41_fs_mon[i].freq == freq)
			return i;
	}

	return -EINVAL;
}

static const DECLARE_TLV_DB_RANGE(dig_vol_tlv,
		0, 0, TLV_DB_SCALE_ITEM(TLV_DB_GAIN_MUTE, 0, 1),
		1, 913, TLV_DB_MINMAX_ITEM(-10200, 1200));
static DECLARE_TLV_DB_SCALE(amp_gain_tlv, 0, 1, 1);

static const struct snd_kcontrol_new dre_ctrl =
	SOC_DAPM_SINGLE("Switch", CS35L41_PWR_CTRL3, 20, 1, 0);

static const char * const cs35l41_pcm_sftramp_text[] =  {
	"Off", ".5ms", "1ms", "2ms", "4ms", "8ms", "15ms", "30ms"};

static SOC_ENUM_SINGLE_DECL(pcm_sft_ramp,
			    CS35L41_AMP_DIG_VOL_CTRL, 0,
			    cs35l41_pcm_sftramp_text);

static const char * const cs35l41_pcm_source_texts[] = {"ASP", "DSP"};
static const unsigned int cs35l41_pcm_source_values[] = {0x08, 0x32};
static SOC_VALUE_ENUM_SINGLE_DECL(cs35l41_pcm_source_enum,
				CS35L41_DAC_PCM1_SRC,
				0, CS35L41_ASP_SOURCE_MASK,
				cs35l41_pcm_source_texts,
				cs35l41_pcm_source_values);

static const struct snd_kcontrol_new pcm_source_mux =
	SOC_DAPM_ENUM("PCM Source", cs35l41_pcm_source_enum);

static const char * const cs35l41_tx_input_texts[] = {"Zero", "ASPRX1",
							"ASPRX2", "VMON",
							"IMON", "VPMON",
							"VBSTMON",
							"DSPTX1", "DSPTX2"};
static const unsigned int cs35l41_tx_input_values[] = {0x00,
						CS35L41_INPUT_SRC_ASPRX1,
						CS35L41_INPUT_SRC_ASPRX2,
						CS35L41_INPUT_SRC_VMON,
						CS35L41_INPUT_SRC_IMON,
						CS35L41_INPUT_SRC_VPMON,
						CS35L41_INPUT_SRC_VBSTMON,
						CS35L41_INPUT_DSP_TX1,
						CS35L41_INPUT_DSP_TX2};

static SOC_VALUE_ENUM_SINGLE_DECL(cs35l41_asptx1_enum,
				CS35L41_ASP_TX1_SRC,
				0, CS35L41_ASP_SOURCE_MASK,
				cs35l41_tx_input_texts,
				cs35l41_tx_input_values);

static const struct snd_kcontrol_new asp_tx1_mux =
	SOC_DAPM_ENUM("ASPTX1 SRC", cs35l41_asptx1_enum);

static SOC_VALUE_ENUM_SINGLE_DECL(cs35l41_asptx2_enum,
				CS35L41_ASP_TX2_SRC,
				0, CS35L41_ASP_SOURCE_MASK,
				cs35l41_tx_input_texts,
				cs35l41_tx_input_values);

static const struct snd_kcontrol_new asp_tx2_mux =
	SOC_DAPM_ENUM("ASPTX2 SRC", cs35l41_asptx2_enum);

static SOC_VALUE_ENUM_SINGLE_DECL(cs35l41_asptx3_enum,
				CS35L41_ASP_TX3_SRC,
				0, CS35L41_ASP_SOURCE_MASK,
				cs35l41_tx_input_texts,
				cs35l41_tx_input_values);

static const struct snd_kcontrol_new asp_tx3_mux =
	SOC_DAPM_ENUM("ASPTX3 SRC", cs35l41_asptx3_enum);

static SOC_VALUE_ENUM_SINGLE_DECL(cs35l41_asptx4_enum,
				CS35L41_ASP_TX4_SRC,
				0, CS35L41_ASP_SOURCE_MASK,
				cs35l41_tx_input_texts,
				cs35l41_tx_input_values);

static const struct snd_kcontrol_new asp_tx4_mux =
	SOC_DAPM_ENUM("ASPTX4 SRC", cs35l41_asptx4_enum);

static const struct snd_kcontrol_new cs35l41_aud_controls[] = {
	SOC_SINGLE_SX_TLV("Digital PCM Volume", CS35L41_AMP_DIG_VOL_CTRL,
		      3, 0x4CF, 0x391, dig_vol_tlv),
	SOC_SINGLE_TLV("Analog PCM Volume", CS35L41_AMP_GAIN_CTRL, 5, 0x14, 0,
			amp_gain_tlv),
	SOC_ENUM("PCM Soft Ramp", pcm_sft_ramp),
	SOC_SINGLE("HW Noise Gate Enable", CS35L41_NG_CFG, 8, 63, 0),
	SOC_SINGLE("HW Noise Gate Delay", CS35L41_NG_CFG, 4, 7, 0),
	SOC_SINGLE("HW Noise Gate Threshold", CS35L41_NG_CFG, 0, 7, 0),
	SOC_SINGLE("Aux Noise Gate CH1 Enable",
				CS35L41_MIXER_NGATE_CH1_CFG, 16, 1, 0),
	SOC_SINGLE("Aux Noise Gate CH1 Entry Delay",
				CS35L41_MIXER_NGATE_CH1_CFG, 8, 15, 0),
	SOC_SINGLE("Aux Noise Gate CH1 Threshold",
				CS35L41_MIXER_NGATE_CH1_CFG, 0, 7, 0),
	SOC_SINGLE("Aux Noise Gate CH2 Entry Delay",
				CS35L41_MIXER_NGATE_CH2_CFG, 8, 15, 0),
	SOC_SINGLE("Aux Noise Gate CH2 Enable",
				CS35L41_MIXER_NGATE_CH2_CFG, 16, 1, 0),
	SOC_SINGLE("Aux Noise Gate CH2 Threshold",
				CS35L41_MIXER_NGATE_CH2_CFG, 0, 7, 0),
	SOC_SINGLE("SCLK Force", CS35L41_SP_FORMAT, CS35L41_SCLK_FRC_SHIFT, 1, 0),
	SOC_SINGLE("LRCLK Force", CS35L41_SP_FORMAT, CS35L41_LRCLK_FRC_SHIFT, 1, 0),
	SOC_SINGLE("Invert Class D", CS35L41_AMP_DIG_VOL_CTRL,
				CS35L41_AMP_INV_PCM_SHIFT, 1, 0),
	SOC_SINGLE("Amp Gain ZC", CS35L41_AMP_GAIN_CTRL,
				CS35L41_AMP_GAIN_ZC_SHIFT, 1, 0),
};

static const struct cs35l41_otp_map_element_t *cs35l41_find_otp_map(u32 otp_id)
{
	int i;

	for (i = 0; i < ARRAY_SIZE(cs35l41_otp_map_map); i++) {
		if (cs35l41_otp_map_map[i].id == otp_id)
			return &cs35l41_otp_map_map[i];
	}

	return NULL;
}

static int cs35l41_otp_unpack(void *data)
{
	const struct cs35l41_otp_map_element_t *otp_map_match;
	const struct cs35l41_otp_packed_element_t *otp_map;
	struct cs35l41_private *cs35l41 = data;
	int bit_offset, word_offset, ret, i;
	unsigned int orig_spi_freq;
	unsigned int bit_sum = 8;
	u32 otp_val, otp_id_reg;
	u32 *otp_mem;

	otp_mem = kmalloc_array(CS35L41_OTP_SIZE_WORDS, sizeof(*otp_mem),
							GFP_KERNEL);
	if (!otp_mem)
		return -ENOMEM;

	ret = regmap_read(cs35l41->regmap, CS35L41_OTPID, &otp_id_reg);
	if (ret < 0) {
		dev_err(cs35l41->dev, "Read OTP ID failed\n");
		ret = -EINVAL;
		goto err_otp_unpack;
	}

	otp_map_match = cs35l41_find_otp_map(otp_id_reg);

	if (!otp_map_match) {
		dev_err(cs35l41->dev, "OTP Map matching ID %d not found\n",
				otp_id_reg);
		ret = -EINVAL;
		goto err_otp_unpack;
	}

	if (cs35l41->otp_setup)
		cs35l41->otp_setup(cs35l41, true, &orig_spi_freq);

	ret = regmap_bulk_read(cs35l41->regmap, CS35L41_OTP_MEM0, otp_mem,
						CS35L41_OTP_SIZE_WORDS);
	if (ret < 0) {
		dev_err(cs35l41->dev, "Read OTP Mem failed\n");
		ret = -EINVAL;
		goto err_otp_unpack;
	}

	if (cs35l41->otp_setup)
		cs35l41->otp_setup(cs35l41, false, &orig_spi_freq);

	otp_map = otp_map_match->map;

	bit_offset = otp_map_match->bit_offset;
	word_offset = otp_map_match->word_offset;

	ret = regmap_write(cs35l41->regmap, CS35L41_TEST_KEY_CTL, 0x00000055);
	if (ret < 0) {
		dev_err(cs35l41->dev, "Write Unlock key failed 1/2\n");
		ret = -EINVAL;
		goto err_otp_unpack;
	}
	ret = regmap_write(cs35l41->regmap, CS35L41_TEST_KEY_CTL, 0x000000AA);
	if (ret < 0) {
		dev_err(cs35l41->dev, "Write Unlock key failed 2/2\n");
		ret = -EINVAL;
		goto err_otp_unpack;
	}

	for (i = 0; i < otp_map_match->num_elements; i++) {
		dev_dbg(cs35l41->dev,
			   "bitoffset= %d, word_offset=%d, bit_sum mod 32=%d\n",
					 bit_offset, word_offset, bit_sum % 32);
		if (bit_offset + otp_map[i].size - 1 >= 32) {
			otp_val = (otp_mem[word_offset] &
					GENMASK(31, bit_offset)) >>
					bit_offset;
			otp_val |= (otp_mem[++word_offset] &
					GENMASK(bit_offset +
						otp_map[i].size - 33, 0)) <<
					(32 - bit_offset);
			bit_offset += otp_map[i].size - 32;
		} else {

			otp_val = (otp_mem[word_offset] &
				GENMASK(bit_offset + otp_map[i].size - 1,
					bit_offset)) >>	bit_offset;
			bit_offset += otp_map[i].size;
		}
		bit_sum += otp_map[i].size;

		if (bit_offset == 32) {
			bit_offset = 0;
			word_offset++;
		}

		if (otp_map[i].reg != 0) {
			ret = regmap_update_bits(cs35l41->regmap,
						otp_map[i].reg,
						GENMASK(otp_map[i].shift +
							otp_map[i].size - 1,
						otp_map[i].shift),
						otp_val << otp_map[i].shift);
			if (ret < 0) {
				dev_err(cs35l41->dev, "Write OTP val failed\n");
				ret = -EINVAL;
				goto err_otp_unpack;
			}
		}
	}

	ret = regmap_write(cs35l41->regmap, CS35L41_TEST_KEY_CTL, 0x000000CC);
	if (ret < 0) {
		dev_err(cs35l41->dev, "Write Lock key failed 1/2\n");
		ret = -EINVAL;
		goto err_otp_unpack;
	}
	ret = regmap_write(cs35l41->regmap, CS35L41_TEST_KEY_CTL, 0x00000033);
	if (ret < 0) {
		dev_err(cs35l41->dev, "Write Lock key failed 2/2\n");
		ret = -EINVAL;
		goto err_otp_unpack;
	}
	ret = 0;

err_otp_unpack:
	kfree(otp_mem);
	return ret;
}

static irqreturn_t cs35l41_irq(int irq, void *data)
{
	struct cs35l41_private *cs35l41 = data;
	unsigned int status[4] = { 0, 0, 0, 0 };
	unsigned int masks[4] = { 0, 0, 0, 0 };
	int ret = IRQ_NONE;
	unsigned int i;

	for (i = 0; i < ARRAY_SIZE(status); i++) {
		regmap_read(cs35l41->regmap,
			    CS35L41_IRQ1_STATUS1 + (i * CS35L41_REGSTRIDE),
			    &status[i]);
		regmap_read(cs35l41->regmap,
			    CS35L41_IRQ1_MASK1 + (i * CS35L41_REGSTRIDE),
			    &masks[i]);
	}

	/* Check to see if unmasked bits are active */
	if (!(status[0] & ~masks[0]) && !(status[1] & ~masks[1]) &&
		!(status[2] & ~masks[2]) && !(status[3] & ~masks[3]))
		return IRQ_NONE;

	if (status[3] & CS35L41_OTP_BOOT_DONE) {
		regmap_update_bits(cs35l41->regmap, CS35L41_IRQ1_MASK4,
				CS35L41_OTP_BOOT_DONE, CS35L41_OTP_BOOT_DONE);
	}

	/*
	 * The following interrupts require a
	 * protection release cycle to get the
	 * speaker out of Safe-Mode.
	 */
	if (status[0] & CS35L41_AMP_SHORT_ERR) {
		dev_crit_ratelimited(cs35l41->dev, "Amp short error\n");
		regmap_write(cs35l41->regmap, CS35L41_IRQ1_STATUS1,
					CS35L41_AMP_SHORT_ERR);
		regmap_write(cs35l41->regmap, CS35L41_PROTECT_REL_ERR_IGN, 0);
		regmap_update_bits(cs35l41->regmap, CS35L41_PROTECT_REL_ERR_IGN,
					CS35L41_AMP_SHORT_ERR_RLS,
					CS35L41_AMP_SHORT_ERR_RLS);
		regmap_update_bits(cs35l41->regmap, CS35L41_PROTECT_REL_ERR_IGN,
					CS35L41_AMP_SHORT_ERR_RLS, 0);
		ret = IRQ_HANDLED;
	}

	if (status[0] & CS35L41_TEMP_WARN) {
		dev_crit_ratelimited(cs35l41->dev, "Over temperature warning\n");
		regmap_write(cs35l41->regmap, CS35L41_IRQ1_STATUS1,
					CS35L41_TEMP_WARN);
		regmap_write(cs35l41->regmap, CS35L41_PROTECT_REL_ERR_IGN, 0);
		regmap_update_bits(cs35l41->regmap, CS35L41_PROTECT_REL_ERR_IGN,
					CS35L41_TEMP_WARN_ERR_RLS,
					CS35L41_TEMP_WARN_ERR_RLS);
		regmap_update_bits(cs35l41->regmap, CS35L41_PROTECT_REL_ERR_IGN,
					CS35L41_TEMP_WARN_ERR_RLS, 0);
		ret = IRQ_HANDLED;
	}

	if (status[0] & CS35L41_TEMP_ERR) {
		dev_crit_ratelimited(cs35l41->dev, "Over temperature error\n");
		regmap_write(cs35l41->regmap, CS35L41_IRQ1_STATUS1,
					CS35L41_TEMP_ERR);
		regmap_write(cs35l41->regmap, CS35L41_PROTECT_REL_ERR_IGN, 0);
		regmap_update_bits(cs35l41->regmap, CS35L41_PROTECT_REL_ERR_IGN,
					CS35L41_TEMP_ERR_RLS,
					CS35L41_TEMP_ERR_RLS);
		regmap_update_bits(cs35l41->regmap, CS35L41_PROTECT_REL_ERR_IGN,
					CS35L41_TEMP_ERR_RLS, 0);
		ret = IRQ_HANDLED;
	}

	if (status[0] & CS35L41_BST_OVP_ERR) {
		dev_crit_ratelimited(cs35l41->dev, "VBST Over Voltage error\n");
		regmap_update_bits(cs35l41->regmap, CS35L41_PWR_CTRL2,
					CS35L41_BST_EN_MASK, 0);
		regmap_write(cs35l41->regmap, CS35L41_IRQ1_STATUS1,
					CS35L41_BST_OVP_ERR);
		regmap_write(cs35l41->regmap, CS35L41_PROTECT_REL_ERR_IGN, 0);
		regmap_update_bits(cs35l41->regmap, CS35L41_PROTECT_REL_ERR_IGN,
					CS35L41_BST_OVP_ERR_RLS,
					CS35L41_BST_OVP_ERR_RLS);
		regmap_update_bits(cs35l41->regmap, CS35L41_PROTECT_REL_ERR_IGN,
					CS35L41_BST_OVP_ERR_RLS, 0);
		regmap_update_bits(cs35l41->regmap, CS35L41_PWR_CTRL2,
					CS35L41_BST_EN_MASK,
					CS35L41_BST_EN_DEFAULT <<
					CS35L41_BST_EN_SHIFT);
		ret = IRQ_HANDLED;
	}

	if (status[0] & CS35L41_BST_DCM_UVP_ERR) {
		dev_crit_ratelimited(cs35l41->dev, "DCM VBST Under Voltage Error\n");
		regmap_update_bits(cs35l41->regmap, CS35L41_PWR_CTRL2,
					CS35L41_BST_EN_MASK, 0);
		regmap_write(cs35l41->regmap, CS35L41_IRQ1_STATUS1,
					CS35L41_BST_DCM_UVP_ERR);
		regmap_write(cs35l41->regmap, CS35L41_PROTECT_REL_ERR_IGN, 0);
		regmap_update_bits(cs35l41->regmap, CS35L41_PROTECT_REL_ERR_IGN,
					CS35L41_BST_UVP_ERR_RLS,
					CS35L41_BST_UVP_ERR_RLS);
		regmap_update_bits(cs35l41->regmap, CS35L41_PROTECT_REL_ERR_IGN,
					CS35L41_BST_UVP_ERR_RLS, 0);
		regmap_update_bits(cs35l41->regmap, CS35L41_PWR_CTRL2,
					CS35L41_BST_EN_MASK,
					CS35L41_BST_EN_DEFAULT <<
					CS35L41_BST_EN_SHIFT);
		ret = IRQ_HANDLED;
	}

	if (status[0] & CS35L41_BST_SHORT_ERR) {
		dev_crit_ratelimited(cs35l41->dev, "LBST error: powering off!\n");
		regmap_update_bits(cs35l41->regmap, CS35L41_PWR_CTRL2,
					CS35L41_BST_EN_MASK, 0);
		regmap_write(cs35l41->regmap, CS35L41_IRQ1_STATUS1,
					CS35L41_BST_SHORT_ERR);
		regmap_write(cs35l41->regmap, CS35L41_PROTECT_REL_ERR_IGN, 0);
		regmap_update_bits(cs35l41->regmap, CS35L41_PROTECT_REL_ERR_IGN,
					CS35L41_BST_SHORT_ERR_RLS,
					CS35L41_BST_SHORT_ERR_RLS);
		regmap_update_bits(cs35l41->regmap, CS35L41_PROTECT_REL_ERR_IGN,
					CS35L41_BST_SHORT_ERR_RLS, 0);
		regmap_update_bits(cs35l41->regmap, CS35L41_PWR_CTRL2,
					CS35L41_BST_EN_MASK,
					CS35L41_BST_EN_DEFAULT <<
					CS35L41_BST_EN_SHIFT);
		ret = IRQ_HANDLED;
	}

	return ret;
}

static const struct reg_sequence cs35l41_pup_patch[] = {
	{ 0x00000040, 0x00000055 },
	{ 0x00000040, 0x000000AA },
	{ 0x00002084, 0x002F1AA0 },
	{ 0x00000040, 0x000000CC },
	{ 0x00000040, 0x00000033 },
};

static const struct reg_sequence cs35l41_pdn_patch[] = {
	{ 0x00000040, 0x00000055 },
	{ 0x00000040, 0x000000AA },
	{ 0x00002084, 0x002F1AA3 },
	{ 0x00000040, 0x000000CC },
	{ 0x00000040, 0x00000033 },
};

static int cs35l41_main_amp_event(struct snd_soc_dapm_widget *w,
		struct snd_kcontrol *kcontrol, int event)
{
	struct snd_soc_component *component =
		snd_soc_dapm_to_component(w->dapm);
	struct cs35l41_private *cs35l41 =
		snd_soc_component_get_drvdata(component);
	unsigned int val;
	int ret = 0;
	bool pdn;

	switch (event) {
	case SND_SOC_DAPM_POST_PMU:
		regmap_multi_reg_write_bypassed(cs35l41->regmap,
					cs35l41_pup_patch,
					ARRAY_SIZE(cs35l41_pup_patch));

		regmap_update_bits(cs35l41->regmap, CS35L41_PWR_CTRL1,
				CS35L41_GLOBAL_EN_MASK,
				1 << CS35L41_GLOBAL_EN_SHIFT);

		usleep_range(1000, 1100);
		break;
	case SND_SOC_DAPM_POST_PMD:
		regmap_update_bits(cs35l41->regmap, CS35L41_PWR_CTRL1,
				CS35L41_GLOBAL_EN_MASK, 0);

		pdn = false;
		ret = regmap_read_poll_timeout(cs35l41->regmap, CS35L41_IRQ1_STATUS1,
					       val, val &  CS35L41_PDN_DONE_MASK,
					       1000, 100000);
		if (ret)
			dev_warn(cs35l41->dev, "PDN failed: %d\n", ret);

		regmap_write(cs35l41->regmap, CS35L41_IRQ1_STATUS1,
				CS35L41_PDN_DONE_MASK);

		regmap_multi_reg_write_bypassed(cs35l41->regmap,
					cs35l41_pdn_patch,
					ARRAY_SIZE(cs35l41_pdn_patch));
		break;
	default:
		dev_err(cs35l41->dev, "Invalid event = 0x%x\n", event);
		ret = -EINVAL;
	}
	return ret;
}

static const struct snd_soc_dapm_widget cs35l41_dapm_widgets[] = {
	SND_SOC_DAPM_OUTPUT("SPK"),

	SND_SOC_DAPM_AIF_IN("ASPRX1", NULL, 0, CS35L41_SP_ENABLES, 16, 0),
	SND_SOC_DAPM_AIF_IN("ASPRX2", NULL, 0, CS35L41_SP_ENABLES, 17, 0),
	SND_SOC_DAPM_AIF_OUT("ASPTX1", NULL, 0, CS35L41_SP_ENABLES, 0, 0),
	SND_SOC_DAPM_AIF_OUT("ASPTX2", NULL, 0, CS35L41_SP_ENABLES, 1, 0),
	SND_SOC_DAPM_AIF_OUT("ASPTX3", NULL, 0, CS35L41_SP_ENABLES, 2, 0),
	SND_SOC_DAPM_AIF_OUT("ASPTX4", NULL, 0, CS35L41_SP_ENABLES, 3, 0),

	SND_SOC_DAPM_ADC("VMON ADC", NULL, CS35L41_PWR_CTRL2, 12, 0),
	SND_SOC_DAPM_ADC("IMON ADC", NULL, CS35L41_PWR_CTRL2, 13, 0),
	SND_SOC_DAPM_ADC("VPMON ADC", NULL, CS35L41_PWR_CTRL2, 8, 0),
	SND_SOC_DAPM_ADC("VBSTMON ADC", NULL, CS35L41_PWR_CTRL2, 9, 0),
	SND_SOC_DAPM_ADC("TEMPMON ADC", NULL, CS35L41_PWR_CTRL2, 10, 0),
	SND_SOC_DAPM_ADC("CLASS H", NULL, CS35L41_PWR_CTRL3, 4, 0),

	SND_SOC_DAPM_OUT_DRV_E("Main AMP", CS35L41_PWR_CTRL2, 0, 0, NULL, 0,
				cs35l41_main_amp_event,
				SND_SOC_DAPM_POST_PMD |	SND_SOC_DAPM_POST_PMU),

	SND_SOC_DAPM_INPUT("VP"),
	SND_SOC_DAPM_INPUT("VBST"),
	SND_SOC_DAPM_INPUT("ISENSE"),
	SND_SOC_DAPM_INPUT("VSENSE"),
	SND_SOC_DAPM_INPUT("TEMP"),

	SND_SOC_DAPM_MUX("ASP TX1 Source", SND_SOC_NOPM, 0, 0, &asp_tx1_mux),
	SND_SOC_DAPM_MUX("ASP TX2 Source", SND_SOC_NOPM, 0, 0, &asp_tx2_mux),
	SND_SOC_DAPM_MUX("ASP TX3 Source", SND_SOC_NOPM, 0, 0, &asp_tx3_mux),
	SND_SOC_DAPM_MUX("ASP TX4 Source", SND_SOC_NOPM, 0, 0, &asp_tx4_mux),
	SND_SOC_DAPM_MUX("PCM Source", SND_SOC_NOPM, 0, 0, &pcm_source_mux),
	SND_SOC_DAPM_SWITCH("DRE", SND_SOC_NOPM, 0, 0, &dre_ctrl),
};

static const struct snd_soc_dapm_route cs35l41_audio_map[] = {

	{"ASP TX1 Source", "VMON", "VMON ADC"},
	{"ASP TX1 Source", "IMON", "IMON ADC"},
	{"ASP TX1 Source", "VPMON", "VPMON ADC"},
	{"ASP TX1 Source", "VBSTMON", "VBSTMON ADC"},
	{"ASP TX1 Source", "ASPRX1", "ASPRX1" },
	{"ASP TX1 Source", "ASPRX2", "ASPRX2" },
	{"ASP TX2 Source", "VMON", "VMON ADC"},
	{"ASP TX2 Source", "IMON", "IMON ADC"},
	{"ASP TX2 Source", "VPMON", "VPMON ADC"},
	{"ASP TX2 Source", "VBSTMON", "VBSTMON ADC"},
	{"ASP TX2 Source", "ASPRX1", "ASPRX1" },
	{"ASP TX2 Source", "ASPRX2", "ASPRX2" },
	{"ASP TX3 Source", "VMON", "VMON ADC"},
	{"ASP TX3 Source", "IMON", "IMON ADC"},
	{"ASP TX3 Source", "VPMON", "VPMON ADC"},
	{"ASP TX3 Source", "VBSTMON", "VBSTMON ADC"},
	{"ASP TX3 Source", "ASPRX1", "ASPRX1" },
	{"ASP TX3 Source", "ASPRX2", "ASPRX2" },
	{"ASP TX4 Source", "VMON", "VMON ADC"},
	{"ASP TX4 Source", "IMON", "IMON ADC"},
	{"ASP TX4 Source", "VPMON", "VPMON ADC"},
	{"ASP TX4 Source", "VBSTMON", "VBSTMON ADC"},
	{"ASP TX4 Source", "ASPRX1", "ASPRX1" },
	{"ASP TX4 Source", "ASPRX2", "ASPRX2" },
	{"ASPTX1", NULL, "ASP TX1 Source"},
	{"ASPTX2", NULL, "ASP TX2 Source"},
	{"ASPTX3", NULL, "ASP TX3 Source"},
	{"ASPTX4", NULL, "ASP TX4 Source"},
	{"AMP Capture", NULL, "ASPTX1"},
	{"AMP Capture", NULL, "ASPTX2"},
	{"AMP Capture", NULL, "ASPTX3"},
	{"AMP Capture", NULL, "ASPTX4"},

	{"VMON ADC", NULL, "VSENSE"},
	{"IMON ADC", NULL, "ISENSE"},
	{"VPMON ADC", NULL, "VP"},
	{"TEMPMON ADC", NULL, "TEMP"},
	{"VBSTMON ADC", NULL, "VBST"},

	{"ASPRX1", NULL, "AMP Playback"},
	{"ASPRX2", NULL, "AMP Playback"},
	{"DRE", "Switch", "CLASS H"},
	{"Main AMP", NULL, "CLASS H"},
	{"Main AMP", NULL, "DRE"},
	{"SPK", NULL, "Main AMP"},

	{"PCM Source", "ASP", "ASPRX1"},
	{"CLASS H", NULL, "PCM Source"},

};

static int cs35l41_set_channel_map(struct snd_soc_dai *dai, unsigned int tx_num,
				unsigned int *tx_slot, unsigned int rx_num,
				unsigned int *rx_slot)
{
	struct cs35l41_private *cs35l41 =
			snd_soc_component_get_drvdata(dai->component);
	int i;

	if (tx_num > 4 || rx_num > 2)
		return -EINVAL;

	for (i = 0; i < rx_num; i++) {
		dev_dbg(cs35l41->dev, "%s: rx slot %d position = %d\n",
				__func__, i, rx_slot[i]);
		regmap_update_bits(cs35l41->regmap, CS35L41_SP_FRAME_RX_SLOT,
				0x3F << (i * 8), rx_slot[i] << (i * 8));
	}

	for (i = 0; i < tx_num; i++) {
		dev_dbg(cs35l41->dev, "%s: tx slot %d position = %d\n",
				__func__, i, tx_slot[i]);
		regmap_update_bits(cs35l41->regmap, CS35L41_SP_FRAME_TX_SLOT,
				0x3F << (i * 8), tx_slot[i] << (i * 8));
	}

	return 0;
}

static int cs35l41_set_dai_fmt(struct snd_soc_dai *codec_dai, unsigned int fmt)
{
	struct cs35l41_private *cs35l41 =
			snd_soc_component_get_drvdata(codec_dai->component);
	unsigned int asp_fmt, lrclk_fmt, sclk_fmt, clock_provider;

	switch (fmt & SND_SOC_DAIFMT_CLOCK_PROVIDER_MASK) {
	case SND_SOC_DAIFMT_CBP_CFP:
		clock_provider = 1;
		break;
	case SND_SOC_DAIFMT_CBC_CFC:
		clock_provider = 0;
		break;
	default:
		dev_warn(cs35l41->dev,
			"%s: Mixed provider/consumer mode unsupported\n",
								__func__);
		return -EINVAL;
	}

	regmap_update_bits(cs35l41->regmap, CS35L41_SP_FORMAT,
				CS35L41_SCLK_MSTR_MASK,
				clock_provider << CS35L41_SCLK_MSTR_SHIFT);
	regmap_update_bits(cs35l41->regmap, CS35L41_SP_FORMAT,
				CS35L41_LRCLK_MSTR_MASK,
				clock_provider << CS35L41_LRCLK_MSTR_SHIFT);

	switch (fmt & SND_SOC_DAIFMT_FORMAT_MASK) {
	case SND_SOC_DAIFMT_DSP_A:
		asp_fmt = 0;
		break;
	case SND_SOC_DAIFMT_I2S:
		asp_fmt = 2;
		break;
	default:
		dev_warn(cs35l41->dev,
			"%s: Invalid or unsupported DAI format\n", __func__);
		return -EINVAL;
	}

	regmap_update_bits(cs35l41->regmap, CS35L41_SP_FORMAT,
					CS35L41_ASP_FMT_MASK,
					asp_fmt << CS35L41_ASP_FMT_SHIFT);

	switch (fmt & SND_SOC_DAIFMT_INV_MASK) {
	case SND_SOC_DAIFMT_NB_IF:
		lrclk_fmt = 1;
		sclk_fmt = 0;
		break;
	case SND_SOC_DAIFMT_IB_NF:
		lrclk_fmt = 0;
		sclk_fmt = 1;
		break;
	case SND_SOC_DAIFMT_IB_IF:
		lrclk_fmt = 1;
		sclk_fmt = 1;
		break;
	case SND_SOC_DAIFMT_NB_NF:
		lrclk_fmt = 0;
		sclk_fmt = 0;
		break;
	default:
		dev_warn(cs35l41->dev,
			"%s: Invalid DAI clock INV\n", __func__);
		return -EINVAL;
	}

	regmap_update_bits(cs35l41->regmap, CS35L41_SP_FORMAT,
				CS35L41_LRCLK_INV_MASK,
				lrclk_fmt << CS35L41_LRCLK_INV_SHIFT);
	regmap_update_bits(cs35l41->regmap, CS35L41_SP_FORMAT,
				CS35L41_SCLK_INV_MASK,
				sclk_fmt << CS35L41_SCLK_INV_SHIFT);

	return 0;
}

struct cs35l41_global_fs_config {
	int rate;
	int fs_cfg;
};

static const struct cs35l41_global_fs_config cs35l41_fs_rates[] = {
	{ 12000,	0x01 },
	{ 24000,	0x02 },
	{ 48000,	0x03 },
	{ 96000,	0x04 },
	{ 192000,	0x05 },
	{ 11025,	0x09 },
	{ 22050,	0x0A },
	{ 44100,	0x0B },
	{ 88200,	0x0C },
	{ 176400,	0x0D },
	{ 8000,		0x11 },
	{ 16000,	0x12 },
	{ 32000,	0x13 },
};

static int cs35l41_pcm_hw_params(struct snd_pcm_substream *substream,
				 struct snd_pcm_hw_params *params,
				 struct snd_soc_dai *dai)
{
	struct cs35l41_private *cs35l41 =
			snd_soc_component_get_drvdata(dai->component);
	unsigned int rate = params_rate(params);
	u8 asp_wl;
	int i;

	for (i = 0; i < ARRAY_SIZE(cs35l41_fs_rates); i++) {
		if (rate == cs35l41_fs_rates[i].rate)
			break;
	}

	if (i >= ARRAY_SIZE(cs35l41_fs_rates)) {
		dev_err(cs35l41->dev, "%s: Unsupported rate: %u\n",
						__func__, rate);
		return -EINVAL;
	}

	asp_wl = params_width(params);

	if (i < ARRAY_SIZE(cs35l41_fs_rates))
		regmap_update_bits(cs35l41->regmap, CS35L41_GLOBAL_CLK_CTRL,
			CS35L41_GLOBAL_FS_MASK,
			cs35l41_fs_rates[i].fs_cfg << CS35L41_GLOBAL_FS_SHIFT);

	if (substream->stream == SNDRV_PCM_STREAM_PLAYBACK) {
		regmap_update_bits(cs35l41->regmap, CS35L41_SP_FORMAT,
				CS35L41_ASP_WIDTH_RX_MASK,
				asp_wl << CS35L41_ASP_WIDTH_RX_SHIFT);
		regmap_update_bits(cs35l41->regmap, CS35L41_SP_RX_WL,
				CS35L41_ASP_RX_WL_MASK,
				asp_wl << CS35L41_ASP_RX_WL_SHIFT);
	} else {
		regmap_update_bits(cs35l41->regmap, CS35L41_SP_FORMAT,
				CS35L41_ASP_WIDTH_TX_MASK,
				asp_wl << CS35L41_ASP_WIDTH_TX_SHIFT);
		regmap_update_bits(cs35l41->regmap, CS35L41_SP_TX_WL,
				CS35L41_ASP_TX_WL_MASK,
				asp_wl << CS35L41_ASP_TX_WL_SHIFT);
	}

	return 0;
}

static int cs35l41_get_clk_config(int freq)
{
	int i;

	for (i = 0; i < ARRAY_SIZE(cs35l41_pll_sysclk); i++) {
		if (cs35l41_pll_sysclk[i].freq == freq)
			return cs35l41_pll_sysclk[i].clk_cfg;
	}

	return -EINVAL;
}

static const unsigned int cs35l41_src_rates[] = {
	8000, 12000, 11025, 16000, 22050, 24000, 32000,
	44100, 48000, 88200, 96000, 176400, 192000
};

static const struct snd_pcm_hw_constraint_list cs35l41_constraints = {
	.count = ARRAY_SIZE(cs35l41_src_rates),
	.list = cs35l41_src_rates,
};

static int cs35l41_pcm_startup(struct snd_pcm_substream *substream,
			       struct snd_soc_dai *dai)
{
	if (substream->runtime)
		return snd_pcm_hw_constraint_list(substream->runtime, 0,
				SNDRV_PCM_HW_PARAM_RATE, &cs35l41_constraints);
	return 0;
}

static int cs35l41_component_set_sysclk(struct snd_soc_component *component,
				int clk_id, int source, unsigned int freq,
				int dir)
{
	struct cs35l41_private *cs35l41 =
				       snd_soc_component_get_drvdata(component);
	int extclk_cfg, clksrc;

	switch (clk_id) {
	case CS35L41_CLKID_SCLK:
		clksrc = CS35L41_PLLSRC_SCLK;
		break;
	case CS35L41_CLKID_LRCLK:
		clksrc = CS35L41_PLLSRC_LRCLK;
		break;
	case CS35L41_CLKID_MCLK:
		clksrc = CS35L41_PLLSRC_MCLK;
		break;
	default:
		dev_err(cs35l41->dev, "Invalid CLK Config\n");
		return -EINVAL;
	}

	extclk_cfg = cs35l41_get_clk_config(freq);

	if (extclk_cfg < 0) {
		dev_err(cs35l41->dev, "Invalid CLK Config: %d, freq: %u\n",
			extclk_cfg, freq);
		return -EINVAL;
	}

	regmap_update_bits(cs35l41->regmap, CS35L41_PLL_CLK_CTRL,
			CS35L41_PLL_OPENLOOP_MASK,
			1 << CS35L41_PLL_OPENLOOP_SHIFT);
	regmap_update_bits(cs35l41->regmap, CS35L41_PLL_CLK_CTRL,
			CS35L41_REFCLK_FREQ_MASK,
			extclk_cfg << CS35L41_REFCLK_FREQ_SHIFT);
	regmap_update_bits(cs35l41->regmap, CS35L41_PLL_CLK_CTRL,
			CS35L41_PLL_CLK_EN_MASK,
			0 << CS35L41_PLL_CLK_EN_SHIFT);
	regmap_update_bits(cs35l41->regmap, CS35L41_PLL_CLK_CTRL,
			CS35L41_PLL_CLK_SEL_MASK, clksrc);
	regmap_update_bits(cs35l41->regmap, CS35L41_PLL_CLK_CTRL,
			CS35L41_PLL_OPENLOOP_MASK,
			0 << CS35L41_PLL_OPENLOOP_SHIFT);
	regmap_update_bits(cs35l41->regmap, CS35L41_PLL_CLK_CTRL,
			CS35L41_PLL_CLK_EN_MASK,
			1 << CS35L41_PLL_CLK_EN_SHIFT);

	return 0;
}

static int cs35l41_dai_set_sysclk(struct snd_soc_dai *dai,
					int clk_id, unsigned int freq, int dir)
{
	struct cs35l41_private *cs35l41 =
				  snd_soc_component_get_drvdata(dai->component);
	unsigned int fs1_val;
	unsigned int fs2_val;
	unsigned int val;
	int fsIndex;

	fsIndex = cs35l41_get_fs_mon_config_index(freq);
	if (fsIndex < 0) {
		dev_err(cs35l41->dev, "Invalid CLK Config freq: %u\n", freq);
		return -EINVAL;
	}

	dev_dbg(cs35l41->dev, "Set DAI sysclk %d\n", freq);
	if (freq <= 6144000) {
		/* Use the lookup table */
		fs1_val = cs35l41_fs_mon[fsIndex].fs1;
		fs2_val = cs35l41_fs_mon[fsIndex].fs2;
	} else {
		/* Use hard-coded values */
		fs1_val = 0x10;
		fs2_val = 0x24;
	}

	val = fs1_val;
	val |= (fs2_val << CS35L41_FS2_WINDOW_SHIFT) & CS35L41_FS2_WINDOW_MASK;
	regmap_write(cs35l41->regmap, CS35L41_TST_FS_MON0, val);

	return 0;
}

static int cs35l41_boost_config(struct cs35l41_private *cs35l41,
		int boost_ind, int boost_cap, int boost_ipk)
{
	unsigned char bst_lbst_val, bst_cbst_range, bst_ipk_scaled;
	struct regmap *regmap = cs35l41->regmap;
	struct device *dev = cs35l41->dev;
	int ret;

	switch (boost_ind) {
	case 1000:	/* 1.0 uH */
		bst_lbst_val = 0;
		break;
	case 1200:	/* 1.2 uH */
		bst_lbst_val = 1;
		break;
	case 1500:	/* 1.5 uH */
		bst_lbst_val = 2;
		break;
	case 2200:	/* 2.2 uH */
		bst_lbst_val = 3;
		break;
	default:
		dev_err(dev, "Invalid boost inductor value: %d nH\n",
				boost_ind);
		return -EINVAL;
	}

	switch (boost_cap) {
	case 0 ... 19:
		bst_cbst_range = 0;
		break;
	case 20 ... 50:
		bst_cbst_range = 1;
		break;
	case 51 ... 100:
		bst_cbst_range = 2;
		break;
	case 101 ... 200:
		bst_cbst_range = 3;
		break;
	default:	/* 201 uF and greater */
		bst_cbst_range = 4;
	}

	ret = regmap_update_bits(regmap, CS35L41_BSTCVRT_COEFF,
			CS35L41_BST_K1_MASK,
			cs35l41_bst_k1_table[bst_lbst_val][bst_cbst_range]
				<< CS35L41_BST_K1_SHIFT);
	if (ret) {
		dev_err(dev, "Failed to write boost K1 coefficient\n");
		return ret;
	}

	ret = regmap_update_bits(regmap, CS35L41_BSTCVRT_COEFF,
			CS35L41_BST_K2_MASK,
			cs35l41_bst_k2_table[bst_lbst_val][bst_cbst_range]
				<< CS35L41_BST_K2_SHIFT);
	if (ret) {
		dev_err(dev, "Failed to write boost K2 coefficient\n");
		return ret;
	}

	ret = regmap_update_bits(regmap, CS35L41_BSTCVRT_SLOPE_LBST,
			CS35L41_BST_SLOPE_MASK,
			cs35l41_bst_slope_table[bst_lbst_val]
				<< CS35L41_BST_SLOPE_SHIFT);
	if (ret) {
		dev_err(dev, "Failed to write boost slope coefficient\n");
		return ret;
	}

	ret = regmap_update_bits(regmap, CS35L41_BSTCVRT_SLOPE_LBST,
			CS35L41_BST_LBST_VAL_MASK,
			bst_lbst_val << CS35L41_BST_LBST_VAL_SHIFT);
	if (ret) {
		dev_err(dev, "Failed to write boost inductor value\n");
		return ret;
	}

	if ((boost_ipk < 1600) || (boost_ipk > 4500)) {
		dev_err(dev, "Invalid boost inductor peak current: %d mA\n",
				boost_ipk);
		return -EINVAL;
	}
	bst_ipk_scaled = ((boost_ipk - 1600) / 50) + 0x10;

	ret = regmap_update_bits(regmap, CS35L41_BSTCVRT_PEAK_CUR,
			CS35L41_BST_IPK_MASK,
			bst_ipk_scaled << CS35L41_BST_IPK_SHIFT);
	if (ret) {
		dev_err(dev, "Failed to write boost inductor peak current\n");
		return ret;
	}

	return 0;
}

static int cs35l41_set_pdata(struct cs35l41_private *cs35l41)
{
	int ret;

	/* Set Platform Data */
	/* Required */
	if (cs35l41->pdata.bst_ipk &&
	    cs35l41->pdata.bst_ind && cs35l41->pdata.bst_cap) {
		ret = cs35l41_boost_config(cs35l41, cs35l41->pdata.bst_ind,
					cs35l41->pdata.bst_cap,
					cs35l41->pdata.bst_ipk);
		if (ret) {
			dev_err(cs35l41->dev, "Error in Boost DT config\n");
			return ret;
		}
	} else {
		dev_err(cs35l41->dev, "Incomplete Boost component DT config\n");
		return -EINVAL;
	}

	/* Optional */
	if (cs35l41->pdata.dout_hiz <= CS35L41_ASP_DOUT_HIZ_MASK &&
	    cs35l41->pdata.dout_hiz >= 0)
		regmap_update_bits(cs35l41->regmap, CS35L41_SP_HIZ_CTRL,
				CS35L41_ASP_DOUT_HIZ_MASK,
				cs35l41->pdata.dout_hiz);

	return 0;
}

static int cs35l41_irq_gpio_config(struct cs35l41_private *cs35l41)
{
	struct cs35l41_irq_cfg *irq_gpio_cfg1 = &cs35l41->pdata.irq_config1;
	struct cs35l41_irq_cfg *irq_gpio_cfg2 = &cs35l41->pdata.irq_config2;
	int irq_pol = IRQF_TRIGGER_NONE;

	if (irq_gpio_cfg1->irq_pol_inv)
		regmap_update_bits(cs35l41->regmap,
					CS35L41_GPIO1_CTRL1,
					CS35L41_GPIO_POL_MASK,
					CS35L41_GPIO_POL_MASK);
	if (irq_gpio_cfg1->irq_out_en)
		regmap_update_bits(cs35l41->regmap,
					CS35L41_GPIO1_CTRL1,
					CS35L41_GPIO_DIR_MASK,
					0);
	if (irq_gpio_cfg1->irq_src_sel)
		regmap_update_bits(cs35l41->regmap,
					CS35L41_GPIO_PAD_CONTROL,
					CS35L41_GPIO1_CTRL_MASK,
					irq_gpio_cfg1->irq_src_sel <<
					CS35L41_GPIO1_CTRL_SHIFT);

	if (irq_gpio_cfg2->irq_pol_inv)
		regmap_update_bits(cs35l41->regmap,
					CS35L41_GPIO2_CTRL1,
					CS35L41_GPIO_POL_MASK,
					CS35L41_GPIO_POL_MASK);
	if (irq_gpio_cfg2->irq_out_en)
		regmap_update_bits(cs35l41->regmap,
					CS35L41_GPIO2_CTRL1,
					CS35L41_GPIO_DIR_MASK,
					0);
	if (irq_gpio_cfg2->irq_src_sel)
		regmap_update_bits(cs35l41->regmap,
					CS35L41_GPIO_PAD_CONTROL,
					CS35L41_GPIO2_CTRL_MASK,
					irq_gpio_cfg2->irq_src_sel <<
					CS35L41_GPIO2_CTRL_SHIFT);

	if ((irq_gpio_cfg2->irq_src_sel ==
			(CS35L41_GPIO_CTRL_ACTV_LO | CS35L41_VALID_PDATA)) ||
		(irq_gpio_cfg2->irq_src_sel ==
			(CS35L41_GPIO_CTRL_OPEN_INT | CS35L41_VALID_PDATA)))
		irq_pol = IRQF_TRIGGER_LOW;
	else if (irq_gpio_cfg2->irq_src_sel ==
			(CS35L41_GPIO_CTRL_ACTV_HI | CS35L41_VALID_PDATA))
		irq_pol = IRQF_TRIGGER_HIGH;

	return irq_pol;
}

static const struct snd_soc_dai_ops cs35l41_ops = {
	.startup = cs35l41_pcm_startup,
	.set_fmt = cs35l41_set_dai_fmt,
	.hw_params = cs35l41_pcm_hw_params,
	.set_sysclk = cs35l41_dai_set_sysclk,
	.set_channel_map = cs35l41_set_channel_map,
};

static struct snd_soc_dai_driver cs35l41_dai[] = {
	{
		.name = "cs35l41-pcm",
		.id = 0,
		.playback = {
			.stream_name = "AMP Playback",
			.channels_min = 1,
			.channels_max = 2,
			.rates = SNDRV_PCM_RATE_KNOT,
			.formats = CS35L41_RX_FORMATS,
		},
		.capture = {
			.stream_name = "AMP Capture",
			.channels_min = 1,
			.channels_max = 8,
			.rates = SNDRV_PCM_RATE_KNOT,
			.formats = CS35L41_TX_FORMATS,
		},
		.ops = &cs35l41_ops,
		.symmetric_rate = 1,
	},
};

static const struct snd_soc_component_driver soc_component_dev_cs35l41 = {
	.name = "cs35l41-codec",

	.dapm_widgets = cs35l41_dapm_widgets,
	.num_dapm_widgets = ARRAY_SIZE(cs35l41_dapm_widgets),
	.dapm_routes = cs35l41_audio_map,
	.num_dapm_routes = ARRAY_SIZE(cs35l41_audio_map),

	.controls = cs35l41_aud_controls,
	.num_controls = ARRAY_SIZE(cs35l41_aud_controls),
	.set_sysclk = cs35l41_component_set_sysclk,
};

static int cs35l41_handle_pdata(struct device *dev,
				  struct cs35l41_platform_data *pdata,
				  struct cs35l41_private *cs35l41)
{
	struct cs35l41_irq_cfg *irq_gpio1_config = &pdata->irq_config1;
	struct cs35l41_irq_cfg *irq_gpio2_config = &pdata->irq_config2;
	unsigned int val;
	int ret;

	ret = device_property_read_u32(dev, "cirrus,boost-peak-milliamp", &val);
	if (ret >= 0)
		pdata->bst_ipk = val;

	ret = device_property_read_u32(dev, "cirrus,boost-ind-nanohenry", &val);
	if (ret >= 0)
		pdata->bst_ind = val;

	ret = device_property_read_u32(dev, "cirrus,boost-cap-microfarad", &val);
	if (ret >= 0)
		pdata->bst_cap = val;

	ret = device_property_read_u32(dev, "cirrus,asp-sdout-hiz", &val);
	if (ret >= 0)
		pdata->dout_hiz = val;
	else
		pdata->dout_hiz = -1;

	/* GPIO1 Pin Config */
	irq_gpio1_config->irq_pol_inv = device_property_read_bool(dev,
					"cirrus,gpio1-polarity-invert");
	irq_gpio1_config->irq_out_en = device_property_read_bool(dev,
					"cirrus,gpio1-output-enable");
	ret = device_property_read_u32(dev, "cirrus,gpio1-src-select",
				&val);
	if (ret >= 0) {
		val |= CS35L41_VALID_PDATA;
		irq_gpio1_config->irq_src_sel = val;
	}

	/* GPIO2 Pin Config */
	irq_gpio2_config->irq_pol_inv = device_property_read_bool(dev,
					"cirrus,gpio2-polarity-invert");
	irq_gpio2_config->irq_out_en = device_property_read_bool(dev,
					"cirrus,gpio2-output-enable");
	ret = device_property_read_u32(dev, "cirrus,gpio2-src-select",
				&val);
	if (ret >= 0) {
		val |= CS35L41_VALID_PDATA;
		irq_gpio2_config->irq_src_sel = val;
	}

	return 0;
}

static const struct reg_sequence cs35l41_reva0_errata_patch[] = {
	{ 0x00000040,			 0x00005555 },
	{ 0x00000040,			 0x0000AAAA },
	{ 0x00003854,			 0x05180240 },
	{ CS35L41_VIMON_SPKMON_RESYNC,	 0x00000000 },
	{ 0x00004310,			 0x00000000 },
	{ CS35L41_VPVBST_FS_SEL,	 0x00000000 },
	{ CS35L41_OTP_TRIM_30,		 0x9091A1C8 },
	{ 0x00003014,			 0x0200EE0E },
	{ CS35L41_BSTCVRT_DCM_CTRL,	 0x00000051 },
	{ 0x00000054,			 0x00000004 },
	{ CS35L41_IRQ1_DB3,		 0x00000000 },
	{ CS35L41_IRQ2_DB3,		 0x00000000 },
	{ CS35L41_DSP1_YM_ACCEL_PL0_PRI, 0x00000000 },
	{ CS35L41_DSP1_XM_ACCEL_PL0_PRI, 0x00000000 },
	{ 0x00000040,			 0x0000CCCC },
	{ 0x00000040,			 0x00003333 },
};

static const struct reg_sequence cs35l41_revb0_errata_patch[] = {
	{ 0x00000040,			 0x00005555 },
	{ 0x00000040,			 0x0000AAAA },
	{ CS35L41_VIMON_SPKMON_RESYNC,	 0x00000000 },
	{ 0x00004310,			 0x00000000 },
	{ CS35L41_VPVBST_FS_SEL,	 0x00000000 },
	{ CS35L41_BSTCVRT_DCM_CTRL,	 0x00000051 },
	{ CS35L41_DSP1_YM_ACCEL_PL0_PRI, 0x00000000 },
	{ CS35L41_DSP1_XM_ACCEL_PL0_PRI, 0x00000000 },
	{ 0x00000040,			 0x0000CCCC },
	{ 0x00000040,			 0x00003333 },
};

static const struct reg_sequence cs35l41_revb2_errata_patch[] = {
	{ 0x00000040,			 0x00005555 },
	{ 0x00000040,			 0x0000AAAA },
	{ CS35L41_VIMON_SPKMON_RESYNC,	 0x00000000 },
	{ 0x00004310,			 0x00000000 },
	{ CS35L41_VPVBST_FS_SEL,	 0x00000000 },
	{ CS35L41_BSTCVRT_DCM_CTRL,	 0x00000051 },
	{ CS35L41_DSP1_YM_ACCEL_PL0_PRI, 0x00000000 },
	{ CS35L41_DSP1_XM_ACCEL_PL0_PRI, 0x00000000 },
	{ 0x00000040,			 0x0000CCCC },
	{ 0x00000040,			 0x00003333 },
};

int cs35l41_probe(struct cs35l41_private *cs35l41,
				struct cs35l41_platform_data *pdata)
{
	u32 regid, reg_revid, i, mtl_revid, int_status, chipid_match;
	int irq_pol = 0;
	int timeout;
	int ret;

	if (pdata) {
		cs35l41->pdata = *pdata;
	} else {
		ret = cs35l41_handle_pdata(cs35l41->dev, &cs35l41->pdata,
					     cs35l41);
		if (ret != 0)
			return ret;
	}

	for (i = 0; i < CS35L41_NUM_SUPPLIES; i++)
		cs35l41->supplies[i].supply = cs35l41_supplies[i];

	ret = devm_regulator_bulk_get(cs35l41->dev, CS35L41_NUM_SUPPLIES,
					cs35l41->supplies);
	if (ret != 0) {
		dev_err(cs35l41->dev,
			"Failed to request core supplies: %d\n",
			ret);
		return ret;
	}

	ret = regulator_bulk_enable(CS35L41_NUM_SUPPLIES, cs35l41->supplies);
	if (ret != 0) {
		dev_err(cs35l41->dev,
			"Failed to enable core supplies: %d\n", ret);
		return ret;
	}

	/* returning NULL can be an option if in stereo mode */
	cs35l41->reset_gpio = devm_gpiod_get_optional(cs35l41->dev, "reset",
							GPIOD_OUT_LOW);
	if (IS_ERR(cs35l41->reset_gpio)) {
		ret = PTR_ERR(cs35l41->reset_gpio);
		cs35l41->reset_gpio = NULL;
		if (ret == -EBUSY) {
			dev_info(cs35l41->dev,
				 "Reset line busy, assuming shared reset\n");
		} else {
			dev_err(cs35l41->dev,
				"Failed to get reset GPIO: %d\n", ret);
			goto err;
		}
	}
	if (cs35l41->reset_gpio) {
		/* satisfy minimum reset pulse width spec */
		usleep_range(2000, 2100);
		gpiod_set_value_cansleep(cs35l41->reset_gpio, 1);
	}

	usleep_range(2000, 2100);

	timeout = 100;
	do {
		if (timeout == 0) {
			dev_err(cs35l41->dev,
				"Timeout waiting for OTP_BOOT_DONE\n");
			ret = -EBUSY;
			goto err;
		}
		usleep_range(1000, 1100);
		regmap_read(cs35l41->regmap, CS35L41_IRQ1_STATUS4, &int_status);
		timeout--;
	} while (!(int_status & CS35L41_OTP_BOOT_DONE));

	regmap_read(cs35l41->regmap, CS35L41_IRQ1_STATUS3, &int_status);
	if (int_status & CS35L41_OTP_BOOT_ERR) {
		dev_err(cs35l41->dev, "OTP Boot error\n");
		ret = -EINVAL;
		goto err;
	}

	ret = regmap_read(cs35l41->regmap, CS35L41_DEVID, &regid);
	if (ret < 0) {
		dev_err(cs35l41->dev, "Get Device ID failed\n");
		goto err;
	}

	ret = regmap_read(cs35l41->regmap, CS35L41_REVID, &reg_revid);
	if (ret < 0) {
		dev_err(cs35l41->dev, "Get Revision ID failed\n");
		goto err;
	}

	mtl_revid = reg_revid & CS35L41_MTLREVID_MASK;

	/* CS35L41 will have even MTLREVID
	 * CS35L41R will have odd MTLREVID
	 */
	chipid_match = (mtl_revid % 2) ? CS35L41R_CHIP_ID : CS35L41_CHIP_ID;
	if (regid != chipid_match) {
		dev_err(cs35l41->dev, "CS35L41 Device ID (%X). Expected ID %X\n",
			regid, chipid_match);
		ret = -ENODEV;
		goto err;
	}

	switch (reg_revid) {
	case CS35L41_REVID_A0:
		ret = regmap_register_patch(cs35l41->regmap,
				cs35l41_reva0_errata_patch,
				ARRAY_SIZE(cs35l41_reva0_errata_patch));
		if (ret < 0) {
			dev_err(cs35l41->dev,
				"Failed to apply A0 errata patch %d\n", ret);
			goto err;
		}
		break;
	case CS35L41_REVID_B0:
		ret = regmap_register_patch(cs35l41->regmap,
				cs35l41_revb0_errata_patch,
				ARRAY_SIZE(cs35l41_revb0_errata_patch));
		if (ret < 0) {
			dev_err(cs35l41->dev,
				"Failed to apply B0 errata patch %d\n", ret);
			goto err;
		}
		break;
	case CS35L41_REVID_B2:
		ret = regmap_register_patch(cs35l41->regmap,
				cs35l41_revb2_errata_patch,
				ARRAY_SIZE(cs35l41_revb2_errata_patch));
		if (ret < 0) {
			dev_err(cs35l41->dev,
				"Failed to apply B2 errata patch %d\n", ret);
			goto err;
		}
		break;
	}

	irq_pol = cs35l41_irq_gpio_config(cs35l41);

	/* Set interrupt masks for critical errors */
	regmap_write(cs35l41->regmap, CS35L41_IRQ1_MASK1,
			CS35L41_INT1_MASK_DEFAULT);

	ret = devm_request_threaded_irq(cs35l41->dev, cs35l41->irq, NULL,
			cs35l41_irq, IRQF_ONESHOT | IRQF_SHARED | irq_pol,
			"cs35l41", cs35l41);

	/* CS35L41 needs INT for PDN_DONE */
	if (ret != 0) {
		dev_err(cs35l41->dev, "Failed to request IRQ: %d\n", ret);
		ret = -ENODEV;
		goto err;
	}

	ret = cs35l41_otp_unpack(cs35l41);
	if (ret < 0) {
		dev_err(cs35l41->dev, "OTP Unpack failed\n");
		goto err;
	}

	ret = regmap_write(cs35l41->regmap, CS35L41_DSP1_CCM_CORE_CTRL, 0);
	if (ret < 0) {
		dev_err(cs35l41->dev, "Write CCM_CORE_CTRL failed\n");
		goto err;
	}

	ret = regmap_update_bits(cs35l41->regmap, CS35L41_PWR_CTRL2,
				 CS35L41_AMP_EN_MASK, 0);
	if (ret < 0) {
		dev_err(cs35l41->dev, "Write CS35L41_PWR_CTRL2 failed\n");
		goto err;
	}

	ret = regmap_update_bits(cs35l41->regmap, CS35L41_AMP_GAIN_CTRL,
				 CS35L41_AMP_GAIN_PCM_MASK, 0);
	if (ret < 0) {
		dev_err(cs35l41->dev, "Write CS35L41_AMP_GAIN_CTRL failed\n");
		goto err;
	}

	ret = cs35l41_set_pdata(cs35l41);
	if (ret < 0) {
		dev_err(cs35l41->dev, "%s: Set pdata failed\n", __func__);
		goto err;
	}

	ret = devm_snd_soc_register_component(cs35l41->dev,
					&soc_component_dev_cs35l41,
					cs35l41_dai, ARRAY_SIZE(cs35l41_dai));
	if (ret < 0) {
		dev_err(cs35l41->dev, "%s: Register codec failed\n", __func__);
		goto err;
	}

	dev_info(cs35l41->dev, "Cirrus Logic CS35L41 (%x), Revision: %02X\n",
			regid, reg_revid);

	return 0;

err:
	regulator_bulk_disable(CS35L41_NUM_SUPPLIES, cs35l41->supplies);
	gpiod_set_value_cansleep(cs35l41->reset_gpio, 0);
	return ret;
}

int cs35l41_remove(struct cs35l41_private *cs35l41)
{
	regmap_write(cs35l41->regmap, CS35L41_IRQ1_MASK1, 0xFFFFFFFF);
	regulator_bulk_disable(CS35L41_NUM_SUPPLIES, cs35l41->supplies);
	gpiod_set_value_cansleep(cs35l41->reset_gpio, 0);
	return 0;
}

MODULE_DESCRIPTION("ASoC CS35L41 driver");
MODULE_AUTHOR("David Rhodes, Cirrus Logic Inc, <david.rhodes@cirrus.com>");
MODULE_LICENSE("GPL");
