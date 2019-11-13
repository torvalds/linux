// SPDX-License-Identifier: GPL-2.0
// Copyright (c) 2016, The Linux Foundation. All rights reserved.

#include <linux/module.h>
#include <linux/err.h>
#include <linux/kernel.h>
#include <linux/delay.h>
#include <linux/types.h>
#include <linux/clk.h>
#include <linux/of.h>
#include <linux/platform_device.h>
#include <linux/regmap.h>
#include <linux/mfd/syscon.h>
#include <sound/soc.h>
#include <sound/pcm.h>
#include <sound/pcm_params.h>
#include <sound/tlv.h>

#define LPASS_CDC_CLK_RX_RESET_CTL		(0x000)
#define LPASS_CDC_CLK_TX_RESET_B1_CTL		(0x004)
#define CLK_RX_RESET_B1_CTL_TX1_RESET_MASK	BIT(0)
#define CLK_RX_RESET_B1_CTL_TX2_RESET_MASK	BIT(1)
#define LPASS_CDC_CLK_DMIC_B1_CTL		(0x008)
#define DMIC_B1_CTL_DMIC0_CLK_SEL_MASK		GENMASK(3, 1)
#define DMIC_B1_CTL_DMIC0_CLK_SEL_DIV2		(0x0 << 1)
#define DMIC_B1_CTL_DMIC0_CLK_SEL_DIV3		(0x1 << 1)
#define DMIC_B1_CTL_DMIC0_CLK_SEL_DIV4		(0x2 << 1)
#define DMIC_B1_CTL_DMIC0_CLK_SEL_DIV6		(0x3 << 1)
#define DMIC_B1_CTL_DMIC0_CLK_SEL_DIV16		(0x4 << 1)
#define DMIC_B1_CTL_DMIC0_CLK_EN_MASK		BIT(0)
#define DMIC_B1_CTL_DMIC0_CLK_EN_ENABLE		BIT(0)

#define LPASS_CDC_CLK_RX_I2S_CTL		(0x00C)
#define RX_I2S_CTL_RX_I2S_MODE_MASK		BIT(5)
#define RX_I2S_CTL_RX_I2S_MODE_16		BIT(5)
#define RX_I2S_CTL_RX_I2S_MODE_32		0
#define RX_I2S_CTL_RX_I2S_FS_RATE_MASK		GENMASK(2, 0)
#define RX_I2S_CTL_RX_I2S_FS_RATE_F_8_KHZ	0x0
#define RX_I2S_CTL_RX_I2S_FS_RATE_F_16_KHZ	0x1
#define RX_I2S_CTL_RX_I2S_FS_RATE_F_32_KHZ	0x2
#define RX_I2S_CTL_RX_I2S_FS_RATE_F_48_KHZ	0x3
#define RX_I2S_CTL_RX_I2S_FS_RATE_F_96_KHZ	0x4
#define RX_I2S_CTL_RX_I2S_FS_RATE_F_192_KHZ	0x5
#define LPASS_CDC_CLK_TX_I2S_CTL		(0x010)
#define TX_I2S_CTL_TX_I2S_MODE_MASK		BIT(5)
#define TX_I2S_CTL_TX_I2S_MODE_16		BIT(5)
#define TX_I2S_CTL_TX_I2S_MODE_32		0
#define TX_I2S_CTL_TX_I2S_FS_RATE_MASK		GENMASK(2, 0)
#define TX_I2S_CTL_TX_I2S_FS_RATE_F_8_KHZ	0x0
#define TX_I2S_CTL_TX_I2S_FS_RATE_F_16_KHZ	0x1
#define TX_I2S_CTL_TX_I2S_FS_RATE_F_32_KHZ	0x2
#define TX_I2S_CTL_TX_I2S_FS_RATE_F_48_KHZ	0x3
#define TX_I2S_CTL_TX_I2S_FS_RATE_F_96_KHZ	0x4
#define TX_I2S_CTL_TX_I2S_FS_RATE_F_192_KHZ	0x5

#define LPASS_CDC_CLK_OTHR_RESET_B1_CTL		(0x014)
#define LPASS_CDC_CLK_TX_CLK_EN_B1_CTL		(0x018)
#define LPASS_CDC_CLK_OTHR_CTL			(0x01C)
#define LPASS_CDC_CLK_RX_B1_CTL			(0x020)
#define LPASS_CDC_CLK_MCLK_CTL			(0x024)
#define MCLK_CTL_MCLK_EN_MASK			BIT(0)
#define MCLK_CTL_MCLK_EN_ENABLE			BIT(0)
#define MCLK_CTL_MCLK_EN_DISABLE		0
#define LPASS_CDC_CLK_PDM_CTL			(0x028)
#define LPASS_CDC_CLK_PDM_CTL_PDM_EN_MASK	BIT(0)
#define LPASS_CDC_CLK_PDM_CTL_PDM_EN		BIT(0)
#define LPASS_CDC_CLK_PDM_CTL_PDM_CLK_SEL_MASK	BIT(1)
#define LPASS_CDC_CLK_PDM_CTL_PDM_CLK_SEL_FB	BIT(1)
#define LPASS_CDC_CLK_PDM_CTL_PDM_CLK_PDM_CLK	0

#define LPASS_CDC_CLK_SD_CTL			(0x02C)
#define LPASS_CDC_RX1_B1_CTL			(0x040)
#define LPASS_CDC_RX2_B1_CTL			(0x060)
#define LPASS_CDC_RX3_B1_CTL			(0x080)
#define LPASS_CDC_RX1_B2_CTL			(0x044)
#define LPASS_CDC_RX2_B2_CTL			(0x064)
#define LPASS_CDC_RX3_B2_CTL			(0x084)
#define LPASS_CDC_RX1_B3_CTL			(0x048)
#define LPASS_CDC_RX2_B3_CTL			(0x068)
#define LPASS_CDC_RX3_B3_CTL			(0x088)
#define LPASS_CDC_RX1_B4_CTL			(0x04C)
#define LPASS_CDC_RX2_B4_CTL			(0x06C)
#define LPASS_CDC_RX3_B4_CTL			(0x08C)
#define LPASS_CDC_RX1_B5_CTL			(0x050)
#define LPASS_CDC_RX2_B5_CTL			(0x070)
#define LPASS_CDC_RX3_B5_CTL			(0x090)
#define LPASS_CDC_RX1_B6_CTL			(0x054)
#define RXn_B6_CTL_MUTE_MASK			BIT(0)
#define RXn_B6_CTL_MUTE_ENABLE			BIT(0)
#define RXn_B6_CTL_MUTE_DISABLE			0
#define LPASS_CDC_RX2_B6_CTL			(0x074)
#define LPASS_CDC_RX3_B6_CTL			(0x094)
#define LPASS_CDC_RX1_VOL_CTL_B1_CTL		(0x058)
#define LPASS_CDC_RX2_VOL_CTL_B1_CTL		(0x078)
#define LPASS_CDC_RX3_VOL_CTL_B1_CTL		(0x098)
#define LPASS_CDC_RX1_VOL_CTL_B2_CTL		(0x05C)
#define LPASS_CDC_RX2_VOL_CTL_B2_CTL		(0x07C)
#define LPASS_CDC_RX3_VOL_CTL_B2_CTL		(0x09C)
#define LPASS_CDC_TOP_GAIN_UPDATE		(0x0A0)
#define LPASS_CDC_TOP_CTL			(0x0A4)
#define TOP_CTL_DIG_MCLK_FREQ_MASK		BIT(0)
#define TOP_CTL_DIG_MCLK_FREQ_F_12_288MHZ	0
#define TOP_CTL_DIG_MCLK_FREQ_F_9_6MHZ		BIT(0)

#define LPASS_CDC_DEBUG_DESER1_CTL		(0x0E0)
#define LPASS_CDC_DEBUG_DESER2_CTL		(0x0E4)
#define LPASS_CDC_DEBUG_B1_CTL_CFG		(0x0E8)
#define LPASS_CDC_DEBUG_B2_CTL_CFG		(0x0EC)
#define LPASS_CDC_DEBUG_B3_CTL_CFG		(0x0F0)
#define LPASS_CDC_IIR1_GAIN_B1_CTL		(0x100)
#define LPASS_CDC_IIR2_GAIN_B1_CTL		(0x140)
#define LPASS_CDC_IIR1_GAIN_B2_CTL		(0x104)
#define LPASS_CDC_IIR2_GAIN_B2_CTL		(0x144)
#define LPASS_CDC_IIR1_GAIN_B3_CTL		(0x108)
#define LPASS_CDC_IIR2_GAIN_B3_CTL		(0x148)
#define LPASS_CDC_IIR1_GAIN_B4_CTL		(0x10C)
#define LPASS_CDC_IIR2_GAIN_B4_CTL		(0x14C)
#define LPASS_CDC_IIR1_GAIN_B5_CTL		(0x110)
#define LPASS_CDC_IIR2_GAIN_B5_CTL		(0x150)
#define LPASS_CDC_IIR1_GAIN_B6_CTL		(0x114)
#define LPASS_CDC_IIR2_GAIN_B6_CTL		(0x154)
#define LPASS_CDC_IIR1_GAIN_B7_CTL		(0x118)
#define LPASS_CDC_IIR2_GAIN_B7_CTL		(0x158)
#define LPASS_CDC_IIR1_GAIN_B8_CTL		(0x11C)
#define LPASS_CDC_IIR2_GAIN_B8_CTL		(0x15C)
#define LPASS_CDC_IIR1_CTL			(0x120)
#define LPASS_CDC_IIR2_CTL			(0x160)
#define LPASS_CDC_IIR1_GAIN_TIMER_CTL		(0x124)
#define LPASS_CDC_IIR2_GAIN_TIMER_CTL		(0x164)
#define LPASS_CDC_IIR1_COEF_B1_CTL		(0x128)
#define LPASS_CDC_IIR2_COEF_B1_CTL		(0x168)
#define LPASS_CDC_IIR1_COEF_B2_CTL		(0x12C)
#define LPASS_CDC_IIR2_COEF_B2_CTL		(0x16C)
#define LPASS_CDC_CONN_RX1_B1_CTL		(0x180)
#define LPASS_CDC_CONN_RX1_B2_CTL		(0x184)
#define LPASS_CDC_CONN_RX1_B3_CTL		(0x188)
#define LPASS_CDC_CONN_RX2_B1_CTL		(0x18C)
#define LPASS_CDC_CONN_RX2_B2_CTL		(0x190)
#define LPASS_CDC_CONN_RX2_B3_CTL		(0x194)
#define LPASS_CDC_CONN_RX3_B1_CTL		(0x198)
#define LPASS_CDC_CONN_RX3_B2_CTL		(0x19C)
#define LPASS_CDC_CONN_TX_B1_CTL		(0x1A0)
#define LPASS_CDC_CONN_EQ1_B1_CTL		(0x1A8)
#define LPASS_CDC_CONN_EQ1_B2_CTL		(0x1AC)
#define LPASS_CDC_CONN_EQ1_B3_CTL		(0x1B0)
#define LPASS_CDC_CONN_EQ1_B4_CTL		(0x1B4)
#define LPASS_CDC_CONN_EQ2_B1_CTL		(0x1B8)
#define LPASS_CDC_CONN_EQ2_B2_CTL		(0x1BC)
#define LPASS_CDC_CONN_EQ2_B3_CTL		(0x1C0)
#define LPASS_CDC_CONN_EQ2_B4_CTL		(0x1C4)
#define LPASS_CDC_CONN_TX_I2S_SD1_CTL		(0x1C8)
#define LPASS_CDC_TX1_VOL_CTL_TIMER		(0x280)
#define LPASS_CDC_TX2_VOL_CTL_TIMER		(0x2A0)
#define LPASS_CDC_TX1_VOL_CTL_GAIN		(0x284)
#define LPASS_CDC_TX2_VOL_CTL_GAIN		(0x2A4)
#define LPASS_CDC_TX1_VOL_CTL_CFG		(0x288)
#define TX_VOL_CTL_CFG_MUTE_EN_MASK		BIT(0)
#define TX_VOL_CTL_CFG_MUTE_EN_ENABLE		BIT(0)

#define LPASS_CDC_TX2_VOL_CTL_CFG		(0x2A8)
#define LPASS_CDC_TX1_MUX_CTL			(0x28C)
#define TX_MUX_CTL_CUT_OFF_FREQ_MASK		GENMASK(5, 4)
#define TX_MUX_CTL_CUT_OFF_FREQ_SHIFT		4
#define TX_MUX_CTL_CF_NEG_3DB_4HZ		(0x0 << 4)
#define TX_MUX_CTL_CF_NEG_3DB_75HZ		(0x1 << 4)
#define TX_MUX_CTL_CF_NEG_3DB_150HZ		(0x2 << 4)
#define TX_MUX_CTL_HPF_BP_SEL_MASK		BIT(3)
#define TX_MUX_CTL_HPF_BP_SEL_BYPASS		BIT(3)
#define TX_MUX_CTL_HPF_BP_SEL_NO_BYPASS		0

#define LPASS_CDC_TX2_MUX_CTL			(0x2AC)
#define LPASS_CDC_TX1_CLK_FS_CTL		(0x290)
#define LPASS_CDC_TX2_CLK_FS_CTL		(0x2B0)
#define LPASS_CDC_TX1_DMIC_CTL			(0x294)
#define LPASS_CDC_TX2_DMIC_CTL			(0x2B4)
#define TXN_DMIC_CTL_CLK_SEL_MASK		GENMASK(2, 0)
#define TXN_DMIC_CTL_CLK_SEL_DIV2		0x0
#define TXN_DMIC_CTL_CLK_SEL_DIV3		0x1
#define TXN_DMIC_CTL_CLK_SEL_DIV4		0x2
#define TXN_DMIC_CTL_CLK_SEL_DIV6		0x3
#define TXN_DMIC_CTL_CLK_SEL_DIV16		0x4

#define MSM8916_WCD_DIGITAL_RATES (SNDRV_PCM_RATE_8000 | \
				   SNDRV_PCM_RATE_16000 | \
				   SNDRV_PCM_RATE_32000 | \
				   SNDRV_PCM_RATE_48000)
#define MSM8916_WCD_DIGITAL_FORMATS (SNDRV_PCM_FMTBIT_S16_LE |\
				     SNDRV_PCM_FMTBIT_S32_LE)

/* Codec supports 2 IIR filters */
enum {
	IIR1 = 0,
	IIR2,
	IIR_MAX,
};

/* Codec supports 5 bands */
enum {
	BAND1 = 0,
	BAND2,
	BAND3,
	BAND4,
	BAND5,
	BAND_MAX,
};

#define WCD_IIR_FILTER_SIZE	(sizeof(u32)*BAND_MAX)

#define WCD_IIR_FILTER_CTL(xname, iidx, bidx) \
{       .iface = SNDRV_CTL_ELEM_IFACE_MIXER, .name = xname, \
	.info = wcd_iir_filter_info, \
	.get = msm8x16_wcd_get_iir_band_audio_mixer, \
	.put = msm8x16_wcd_put_iir_band_audio_mixer, \
	.private_value = (unsigned long)&(struct wcd_iir_filter_ctl) { \
		.iir_idx = iidx, \
		.band_idx = bidx, \
		.bytes_ext = {.max = WCD_IIR_FILTER_SIZE, }, \
	} \
}

struct wcd_iir_filter_ctl {
	unsigned int iir_idx;
	unsigned int band_idx;
	struct soc_bytes_ext bytes_ext;
};

struct msm8916_wcd_digital_priv {
	struct clk *ahbclk, *mclk;
};

static const unsigned long rx_gain_reg[] = {
	LPASS_CDC_RX1_VOL_CTL_B2_CTL,
	LPASS_CDC_RX2_VOL_CTL_B2_CTL,
	LPASS_CDC_RX3_VOL_CTL_B2_CTL,
};

static const unsigned long tx_gain_reg[] = {
	LPASS_CDC_TX1_VOL_CTL_GAIN,
	LPASS_CDC_TX2_VOL_CTL_GAIN,
};

static const char *const rx_mix1_text[] = {
	"ZERO", "IIR1", "IIR2", "RX1", "RX2", "RX3"
};

static const char * const rx_mix2_text[] = {
	"ZERO", "IIR1", "IIR2"
};

static const char *const dec_mux_text[] = {
	"ZERO", "ADC1", "ADC2", "ADC3", "DMIC1", "DMIC2"
};

static const char *const cic_mux_text[] = { "AMIC", "DMIC" };

/* RX1 MIX1 */
static const struct soc_enum rx_mix1_inp_enum[] = {
	SOC_ENUM_SINGLE(LPASS_CDC_CONN_RX1_B1_CTL, 0, 6, rx_mix1_text),
	SOC_ENUM_SINGLE(LPASS_CDC_CONN_RX1_B1_CTL, 3, 6, rx_mix1_text),
	SOC_ENUM_SINGLE(LPASS_CDC_CONN_RX1_B2_CTL, 0, 6, rx_mix1_text),
};

/* RX2 MIX1 */
static const struct soc_enum rx2_mix1_inp_enum[] = {
	SOC_ENUM_SINGLE(LPASS_CDC_CONN_RX2_B1_CTL, 0, 6, rx_mix1_text),
	SOC_ENUM_SINGLE(LPASS_CDC_CONN_RX2_B1_CTL, 3, 6, rx_mix1_text),
	SOC_ENUM_SINGLE(LPASS_CDC_CONN_RX2_B2_CTL, 0, 6, rx_mix1_text),
};

/* RX3 MIX1 */
static const struct soc_enum rx3_mix1_inp_enum[] = {
	SOC_ENUM_SINGLE(LPASS_CDC_CONN_RX3_B1_CTL, 0, 6, rx_mix1_text),
	SOC_ENUM_SINGLE(LPASS_CDC_CONN_RX3_B1_CTL, 3, 6, rx_mix1_text),
	SOC_ENUM_SINGLE(LPASS_CDC_CONN_RX3_B2_CTL, 0, 6, rx_mix1_text),
};

/* RX1 MIX2 */
static const struct soc_enum rx_mix2_inp1_chain_enum =
	SOC_ENUM_SINGLE(LPASS_CDC_CONN_RX1_B3_CTL,
		0, 3, rx_mix2_text);

/* RX2 MIX2 */
static const struct soc_enum rx2_mix2_inp1_chain_enum =
	SOC_ENUM_SINGLE(LPASS_CDC_CONN_RX2_B3_CTL,
		0, 3, rx_mix2_text);

/* DEC */
static const struct soc_enum dec1_mux_enum = SOC_ENUM_SINGLE(
				LPASS_CDC_CONN_TX_B1_CTL, 0, 6, dec_mux_text);
static const struct soc_enum dec2_mux_enum = SOC_ENUM_SINGLE(
				LPASS_CDC_CONN_TX_B1_CTL, 3, 6, dec_mux_text);

/* CIC */
static const struct soc_enum cic1_mux_enum = SOC_ENUM_SINGLE(
				LPASS_CDC_TX1_MUX_CTL, 0, 2, cic_mux_text);
static const struct soc_enum cic2_mux_enum = SOC_ENUM_SINGLE(
				LPASS_CDC_TX2_MUX_CTL, 0, 2, cic_mux_text);

/* RDAC2 MUX */
static const struct snd_kcontrol_new dec1_mux = SOC_DAPM_ENUM(
				"DEC1 MUX Mux", dec1_mux_enum);
static const struct snd_kcontrol_new dec2_mux = SOC_DAPM_ENUM(
				"DEC2 MUX Mux",	dec2_mux_enum);
static const struct snd_kcontrol_new cic1_mux = SOC_DAPM_ENUM(
				"CIC1 MUX Mux", cic1_mux_enum);
static const struct snd_kcontrol_new cic2_mux = SOC_DAPM_ENUM(
				"CIC2 MUX Mux",	cic2_mux_enum);
static const struct snd_kcontrol_new rx_mix1_inp1_mux = SOC_DAPM_ENUM(
				"RX1 MIX1 INP1 Mux", rx_mix1_inp_enum[0]);
static const struct snd_kcontrol_new rx_mix1_inp2_mux = SOC_DAPM_ENUM(
				"RX1 MIX1 INP2 Mux", rx_mix1_inp_enum[1]);
static const struct snd_kcontrol_new rx_mix1_inp3_mux = SOC_DAPM_ENUM(
				"RX1 MIX1 INP3 Mux", rx_mix1_inp_enum[2]);
static const struct snd_kcontrol_new rx2_mix1_inp1_mux = SOC_DAPM_ENUM(
				"RX2 MIX1 INP1 Mux", rx2_mix1_inp_enum[0]);
static const struct snd_kcontrol_new rx2_mix1_inp2_mux = SOC_DAPM_ENUM(
				"RX2 MIX1 INP2 Mux", rx2_mix1_inp_enum[1]);
static const struct snd_kcontrol_new rx2_mix1_inp3_mux = SOC_DAPM_ENUM(
				"RX2 MIX1 INP3 Mux", rx2_mix1_inp_enum[2]);
static const struct snd_kcontrol_new rx3_mix1_inp1_mux = SOC_DAPM_ENUM(
				"RX3 MIX1 INP1 Mux", rx3_mix1_inp_enum[0]);
static const struct snd_kcontrol_new rx3_mix1_inp2_mux = SOC_DAPM_ENUM(
				"RX3 MIX1 INP2 Mux", rx3_mix1_inp_enum[1]);
static const struct snd_kcontrol_new rx3_mix1_inp3_mux = SOC_DAPM_ENUM(
				"RX3 MIX1 INP3 Mux", rx3_mix1_inp_enum[2]);
static const struct snd_kcontrol_new rx1_mix2_inp1_mux = SOC_DAPM_ENUM(
				"RX1 MIX2 INP1 Mux", rx_mix2_inp1_chain_enum);
static const struct snd_kcontrol_new rx2_mix2_inp1_mux = SOC_DAPM_ENUM(
				"RX2 MIX2 INP1 Mux", rx2_mix2_inp1_chain_enum);

/* Digital Gain control -38.4 dB to +38.4 dB in 0.3 dB steps */
static const DECLARE_TLV_DB_SCALE(digital_gain, -3840, 30, 0);

/* Cutoff Freq for High Pass Filter at -3dB */
static const char * const hpf_cutoff_text[] = {
	"4Hz", "75Hz", "150Hz",
};

static SOC_ENUM_SINGLE_DECL(tx1_hpf_cutoff_enum, LPASS_CDC_TX1_MUX_CTL, 4,
			    hpf_cutoff_text);
static SOC_ENUM_SINGLE_DECL(tx2_hpf_cutoff_enum, LPASS_CDC_TX2_MUX_CTL, 4,
			    hpf_cutoff_text);

/* cut off for dc blocker inside rx chain */
static const char * const dc_blocker_cutoff_text[] = {
	"4Hz", "75Hz", "150Hz",
};

static SOC_ENUM_SINGLE_DECL(rx1_dcb_cutoff_enum, LPASS_CDC_RX1_B4_CTL, 0,
			    dc_blocker_cutoff_text);
static SOC_ENUM_SINGLE_DECL(rx2_dcb_cutoff_enum, LPASS_CDC_RX2_B4_CTL, 0,
			    dc_blocker_cutoff_text);
static SOC_ENUM_SINGLE_DECL(rx3_dcb_cutoff_enum, LPASS_CDC_RX3_B4_CTL, 0,
			    dc_blocker_cutoff_text);

static int msm8x16_wcd_codec_set_iir_gain(struct snd_soc_dapm_widget *w,
		struct snd_kcontrol *kcontrol, int event)
{
	struct snd_soc_component *component =
			snd_soc_dapm_to_component(w->dapm);
	int value = 0, reg = 0;

	switch (event) {
	case SND_SOC_DAPM_POST_PMU:
		if (w->shift == 0)
			reg = LPASS_CDC_IIR1_GAIN_B1_CTL;
		else if (w->shift == 1)
			reg = LPASS_CDC_IIR2_GAIN_B1_CTL;
		value = snd_soc_component_read32(component, reg);
		snd_soc_component_write(component, reg, value);
		break;
	default:
		break;
	}
	return 0;
}

static uint32_t get_iir_band_coeff(struct snd_soc_component *component,
				   int iir_idx, int band_idx,
				   int coeff_idx)
{
	uint32_t value = 0;

	/* Address does not automatically update if reading */
	snd_soc_component_write(component,
		(LPASS_CDC_IIR1_COEF_B1_CTL + 64 * iir_idx),
		((band_idx * BAND_MAX + coeff_idx)
		* sizeof(uint32_t)) & 0x7F);

	value |= snd_soc_component_read32(component,
		(LPASS_CDC_IIR1_COEF_B2_CTL + 64 * iir_idx));

	snd_soc_component_write(component,
		(LPASS_CDC_IIR1_COEF_B1_CTL + 64 * iir_idx),
		((band_idx * BAND_MAX + coeff_idx)
		* sizeof(uint32_t) + 1) & 0x7F);

	value |= (snd_soc_component_read32(component,
		(LPASS_CDC_IIR1_COEF_B2_CTL + 64 * iir_idx)) << 8);

	snd_soc_component_write(component,
		(LPASS_CDC_IIR1_COEF_B1_CTL + 64 * iir_idx),
		((band_idx * BAND_MAX + coeff_idx)
		* sizeof(uint32_t) + 2) & 0x7F);

	value |= (snd_soc_component_read32(component,
		(LPASS_CDC_IIR1_COEF_B2_CTL + 64 * iir_idx)) << 16);

	snd_soc_component_write(component,
		(LPASS_CDC_IIR1_COEF_B1_CTL + 64 * iir_idx),
		((band_idx * BAND_MAX + coeff_idx)
		* sizeof(uint32_t) + 3) & 0x7F);

	/* Mask bits top 2 bits since they are reserved */
	value |= ((snd_soc_component_read32(component,
		 (LPASS_CDC_IIR1_COEF_B2_CTL + 64 * iir_idx)) & 0x3f) << 24);
	return value;

}

static int msm8x16_wcd_get_iir_band_audio_mixer(
					struct snd_kcontrol *kcontrol,
					struct snd_ctl_elem_value *ucontrol)
{

	struct snd_soc_component *component =
			snd_soc_kcontrol_component(kcontrol);
	struct wcd_iir_filter_ctl *ctl =
			(struct wcd_iir_filter_ctl *)kcontrol->private_value;
	struct soc_bytes_ext *params = &ctl->bytes_ext;
	int iir_idx = ctl->iir_idx;
	int band_idx = ctl->band_idx;
	u32 coeff[BAND_MAX];

	coeff[0] = get_iir_band_coeff(component, iir_idx, band_idx, 0);
	coeff[1] = get_iir_band_coeff(component, iir_idx, band_idx, 1);
	coeff[2] = get_iir_band_coeff(component, iir_idx, band_idx, 2);
	coeff[3] = get_iir_band_coeff(component, iir_idx, band_idx, 3);
	coeff[4] = get_iir_band_coeff(component, iir_idx, band_idx, 4);

	memcpy(ucontrol->value.bytes.data, &coeff[0], params->max);

	return 0;
}

static void set_iir_band_coeff(struct snd_soc_component *component,
				int iir_idx, int band_idx,
				uint32_t value)
{
	snd_soc_component_write(component,
		(LPASS_CDC_IIR1_COEF_B2_CTL + 64 * iir_idx),
		(value & 0xFF));

	snd_soc_component_write(component,
		(LPASS_CDC_IIR1_COEF_B2_CTL + 64 * iir_idx),
		(value >> 8) & 0xFF);

	snd_soc_component_write(component,
		(LPASS_CDC_IIR1_COEF_B2_CTL + 64 * iir_idx),
		(value >> 16) & 0xFF);

	/* Mask top 2 bits, 7-8 are reserved */
	snd_soc_component_write(component,
		(LPASS_CDC_IIR1_COEF_B2_CTL + 64 * iir_idx),
		(value >> 24) & 0x3F);
}

static int msm8x16_wcd_put_iir_band_audio_mixer(
					struct snd_kcontrol *kcontrol,
					struct snd_ctl_elem_value *ucontrol)
{
	struct snd_soc_component *component =
			snd_soc_kcontrol_component(kcontrol);
	struct wcd_iir_filter_ctl *ctl =
			(struct wcd_iir_filter_ctl *)kcontrol->private_value;
	struct soc_bytes_ext *params = &ctl->bytes_ext;
	int iir_idx = ctl->iir_idx;
	int band_idx = ctl->band_idx;
	u32 coeff[BAND_MAX];

	memcpy(&coeff[0], ucontrol->value.bytes.data, params->max);

	/* Mask top bit it is reserved */
	/* Updates addr automatically for each B2 write */
	snd_soc_component_write(component,
		(LPASS_CDC_IIR1_COEF_B1_CTL + 64 * iir_idx),
		(band_idx * BAND_MAX * sizeof(uint32_t)) & 0x7F);

	set_iir_band_coeff(component, iir_idx, band_idx, coeff[0]);
	set_iir_band_coeff(component, iir_idx, band_idx, coeff[1]);
	set_iir_band_coeff(component, iir_idx, band_idx, coeff[2]);
	set_iir_band_coeff(component, iir_idx, band_idx, coeff[3]);
	set_iir_band_coeff(component, iir_idx, band_idx, coeff[4]);

	return 0;
}

static int wcd_iir_filter_info(struct snd_kcontrol *kcontrol,
				struct snd_ctl_elem_info *ucontrol)
{
	struct wcd_iir_filter_ctl *ctl =
		(struct wcd_iir_filter_ctl *)kcontrol->private_value;
	struct soc_bytes_ext *params = &ctl->bytes_ext;

	ucontrol->type = SNDRV_CTL_ELEM_TYPE_BYTES;
	ucontrol->count = params->max;

	return 0;
}

static const struct snd_kcontrol_new msm8916_wcd_digital_snd_controls[] = {
	SOC_SINGLE_S8_TLV("RX1 Digital Volume", LPASS_CDC_RX1_VOL_CTL_B2_CTL,
			  -128, 127, digital_gain),
	SOC_SINGLE_S8_TLV("RX2 Digital Volume", LPASS_CDC_RX2_VOL_CTL_B2_CTL,
			  -128, 127, digital_gain),
	SOC_SINGLE_S8_TLV("RX3 Digital Volume", LPASS_CDC_RX3_VOL_CTL_B2_CTL,
			  -128, 127, digital_gain),
	SOC_SINGLE_S8_TLV("TX1 Digital Volume", LPASS_CDC_TX1_VOL_CTL_GAIN,
			  -128, 127, digital_gain),
	SOC_SINGLE_S8_TLV("TX2 Digital Volume", LPASS_CDC_TX2_VOL_CTL_GAIN,
			  -128, 127, digital_gain),
	SOC_ENUM("TX1 HPF Cutoff", tx1_hpf_cutoff_enum),
	SOC_ENUM("TX2 HPF Cutoff", tx2_hpf_cutoff_enum),
	SOC_SINGLE("TX1 HPF Switch", LPASS_CDC_TX1_MUX_CTL, 3, 1, 0),
	SOC_SINGLE("TX2 HPF Switch", LPASS_CDC_TX2_MUX_CTL, 3, 1, 0),
	SOC_ENUM("RX1 DCB Cutoff", rx1_dcb_cutoff_enum),
	SOC_ENUM("RX2 DCB Cutoff", rx2_dcb_cutoff_enum),
	SOC_ENUM("RX3 DCB Cutoff", rx3_dcb_cutoff_enum),
	SOC_SINGLE("RX1 DCB Switch", LPASS_CDC_RX1_B5_CTL, 2, 1, 0),
	SOC_SINGLE("RX2 DCB Switch", LPASS_CDC_RX2_B5_CTL, 2, 1, 0),
	SOC_SINGLE("RX3 DCB Switch", LPASS_CDC_RX3_B5_CTL, 2, 1, 0),
	SOC_SINGLE("RX1 Mute Switch", LPASS_CDC_RX1_B6_CTL, 0, 1, 0),
	SOC_SINGLE("RX2 Mute Switch", LPASS_CDC_RX2_B6_CTL, 0, 1, 0),
	SOC_SINGLE("RX3 Mute Switch", LPASS_CDC_RX3_B6_CTL, 0, 1, 0),

	SOC_SINGLE("IIR1 Band1 Switch", LPASS_CDC_IIR1_CTL, 0, 1, 0),
	SOC_SINGLE("IIR1 Band2 Switch", LPASS_CDC_IIR1_CTL, 1, 1, 0),
	SOC_SINGLE("IIR1 Band3 Switch", LPASS_CDC_IIR1_CTL, 2, 1, 0),
	SOC_SINGLE("IIR1 Band4 Switch", LPASS_CDC_IIR1_CTL, 3, 1, 0),
	SOC_SINGLE("IIR1 Band5 Switch", LPASS_CDC_IIR1_CTL, 4, 1, 0),
	SOC_SINGLE("IIR2 Band1 Switch", LPASS_CDC_IIR2_CTL, 0, 1, 0),
	SOC_SINGLE("IIR2 Band2 Switch", LPASS_CDC_IIR2_CTL, 1, 1, 0),
	SOC_SINGLE("IIR2 Band3 Switch", LPASS_CDC_IIR2_CTL, 2, 1, 0),
	SOC_SINGLE("IIR2 Band4 Switch", LPASS_CDC_IIR2_CTL, 3, 1, 0),
	SOC_SINGLE("IIR2 Band5 Switch", LPASS_CDC_IIR2_CTL, 4, 1, 0),
	WCD_IIR_FILTER_CTL("IIR1 Band1", IIR1, BAND1),
	WCD_IIR_FILTER_CTL("IIR1 Band2", IIR1, BAND2),
	WCD_IIR_FILTER_CTL("IIR1 Band3", IIR1, BAND3),
	WCD_IIR_FILTER_CTL("IIR1 Band4", IIR1, BAND4),
	WCD_IIR_FILTER_CTL("IIR1 Band5", IIR1, BAND5),
	WCD_IIR_FILTER_CTL("IIR2 Band1", IIR2, BAND1),
	WCD_IIR_FILTER_CTL("IIR2 Band2", IIR2, BAND2),
	WCD_IIR_FILTER_CTL("IIR2 Band3", IIR2, BAND3),
	WCD_IIR_FILTER_CTL("IIR2 Band4", IIR2, BAND4),
	WCD_IIR_FILTER_CTL("IIR2 Band5", IIR2, BAND5),
	SOC_SINGLE_SX_TLV("IIR1 INP1 Volume", LPASS_CDC_IIR1_GAIN_B1_CTL,
			0,  -84, 40, digital_gain),
	SOC_SINGLE_SX_TLV("IIR1 INP2 Volume", LPASS_CDC_IIR1_GAIN_B2_CTL,
			0,  -84, 40, digital_gain),
	SOC_SINGLE_SX_TLV("IIR1 INP3 Volume", LPASS_CDC_IIR1_GAIN_B3_CTL,
			0,  -84, 40, digital_gain),
	SOC_SINGLE_SX_TLV("IIR1 INP4 Volume", LPASS_CDC_IIR1_GAIN_B4_CTL,
			0,  -84,	40, digital_gain),
	SOC_SINGLE_SX_TLV("IIR2 INP1 Volume", LPASS_CDC_IIR2_GAIN_B1_CTL,
			0,  -84, 40, digital_gain),
	SOC_SINGLE_SX_TLV("IIR2 INP2 Volume", LPASS_CDC_IIR2_GAIN_B2_CTL,
			0,  -84, 40, digital_gain),
	SOC_SINGLE_SX_TLV("IIR2 INP3 Volume", LPASS_CDC_IIR2_GAIN_B3_CTL,
			0,  -84, 40, digital_gain),
	SOC_SINGLE_SX_TLV("IIR2 INP4 Volume", LPASS_CDC_IIR2_GAIN_B4_CTL,
			0,  -84, 40, digital_gain),

};

static int msm8916_wcd_digital_enable_interpolator(
						struct snd_soc_dapm_widget *w,
						struct snd_kcontrol *kcontrol,
						int event)
{
	struct snd_soc_component *component = snd_soc_dapm_to_component(w->dapm);

	switch (event) {
	case SND_SOC_DAPM_POST_PMU:
		/* apply the digital gain after the interpolator is enabled */
		usleep_range(10000, 10100);
		snd_soc_component_write(component, rx_gain_reg[w->shift],
			      snd_soc_component_read32(component, rx_gain_reg[w->shift]));
		break;
	}
	return 0;
}

static int msm8916_wcd_digital_enable_dec(struct snd_soc_dapm_widget *w,
					  struct snd_kcontrol *kcontrol,
					  int event)
{
	struct snd_soc_component *component = snd_soc_dapm_to_component(w->dapm);
	unsigned int decimator = w->shift + 1;
	u16 dec_reset_reg, tx_vol_ctl_reg, tx_mux_ctl_reg;
	u8 dec_hpf_cut_of_freq;

	dec_reset_reg = LPASS_CDC_CLK_TX_RESET_B1_CTL;
	tx_vol_ctl_reg = LPASS_CDC_TX1_VOL_CTL_CFG + 32 * (decimator - 1);
	tx_mux_ctl_reg = LPASS_CDC_TX1_MUX_CTL + 32 * (decimator - 1);

	switch (event) {
	case SND_SOC_DAPM_PRE_PMU:
		/* Enable TX digital mute */
		snd_soc_component_update_bits(component, tx_vol_ctl_reg,
				    TX_VOL_CTL_CFG_MUTE_EN_MASK,
				    TX_VOL_CTL_CFG_MUTE_EN_ENABLE);
		dec_hpf_cut_of_freq = snd_soc_component_read32(component, tx_mux_ctl_reg) &
					TX_MUX_CTL_CUT_OFF_FREQ_MASK;
		dec_hpf_cut_of_freq >>= TX_MUX_CTL_CUT_OFF_FREQ_SHIFT;
		if (dec_hpf_cut_of_freq != TX_MUX_CTL_CF_NEG_3DB_150HZ) {
			/* set cut of freq to CF_MIN_3DB_150HZ (0x1) */
			snd_soc_component_update_bits(component, tx_mux_ctl_reg,
					    TX_MUX_CTL_CUT_OFF_FREQ_MASK,
					    TX_MUX_CTL_CF_NEG_3DB_150HZ);
		}
		break;
	case SND_SOC_DAPM_POST_PMU:
		/* enable HPF */
		snd_soc_component_update_bits(component, tx_mux_ctl_reg,
				    TX_MUX_CTL_HPF_BP_SEL_MASK,
				    TX_MUX_CTL_HPF_BP_SEL_NO_BYPASS);
		/* apply the digital gain after the decimator is enabled */
		snd_soc_component_write(component, tx_gain_reg[w->shift],
			      snd_soc_component_read32(component, tx_gain_reg[w->shift]));
		snd_soc_component_update_bits(component, tx_vol_ctl_reg,
				    TX_VOL_CTL_CFG_MUTE_EN_MASK, 0);
		break;
	case SND_SOC_DAPM_PRE_PMD:
		snd_soc_component_update_bits(component, tx_vol_ctl_reg,
				    TX_VOL_CTL_CFG_MUTE_EN_MASK,
				    TX_VOL_CTL_CFG_MUTE_EN_ENABLE);
		snd_soc_component_update_bits(component, tx_mux_ctl_reg,
				    TX_MUX_CTL_HPF_BP_SEL_MASK,
				    TX_MUX_CTL_HPF_BP_SEL_BYPASS);
		break;
	case SND_SOC_DAPM_POST_PMD:
		snd_soc_component_update_bits(component, dec_reset_reg, 1 << w->shift,
				    1 << w->shift);
		snd_soc_component_update_bits(component, dec_reset_reg, 1 << w->shift, 0x0);
		snd_soc_component_update_bits(component, tx_mux_ctl_reg,
				    TX_MUX_CTL_HPF_BP_SEL_MASK,
				    TX_MUX_CTL_HPF_BP_SEL_BYPASS);
		snd_soc_component_update_bits(component, tx_vol_ctl_reg,
				    TX_VOL_CTL_CFG_MUTE_EN_MASK, 0);
		break;
	}

	return 0;
}

static int msm8916_wcd_digital_enable_dmic(struct snd_soc_dapm_widget *w,
					   struct snd_kcontrol *kcontrol,
					   int event)
{
	struct snd_soc_component *component = snd_soc_dapm_to_component(w->dapm);
	unsigned int dmic;
	int ret;
	/* get dmic number out of widget name */
	char *dmic_num = strpbrk(w->name, "12");

	if (dmic_num == NULL) {
		dev_err(component->dev, "Invalid DMIC\n");
		return -EINVAL;
	}
	ret = kstrtouint(dmic_num, 10, &dmic);
	if (ret < 0 || dmic > 2) {
		dev_err(component->dev, "Invalid DMIC line on the component\n");
		return -EINVAL;
	}

	switch (event) {
	case SND_SOC_DAPM_PRE_PMU:
		snd_soc_component_update_bits(component, LPASS_CDC_CLK_DMIC_B1_CTL,
				    DMIC_B1_CTL_DMIC0_CLK_SEL_MASK,
				    DMIC_B1_CTL_DMIC0_CLK_SEL_DIV3);
		switch (dmic) {
		case 1:
			snd_soc_component_update_bits(component, LPASS_CDC_TX1_DMIC_CTL,
					    TXN_DMIC_CTL_CLK_SEL_MASK,
					    TXN_DMIC_CTL_CLK_SEL_DIV3);
			break;
		case 2:
			snd_soc_component_update_bits(component, LPASS_CDC_TX2_DMIC_CTL,
					    TXN_DMIC_CTL_CLK_SEL_MASK,
					    TXN_DMIC_CTL_CLK_SEL_DIV3);
			break;
		}
		break;
	}

	return 0;
}

static const char * const iir_inp1_text[] = {
	"ZERO", "DEC1", "DEC2", "RX1", "RX2", "RX3"
};

static const struct soc_enum iir1_inp1_mux_enum =
	SOC_ENUM_SINGLE(LPASS_CDC_CONN_EQ1_B1_CTL,
		0, 6, iir_inp1_text);

static const struct soc_enum iir2_inp1_mux_enum =
	SOC_ENUM_SINGLE(LPASS_CDC_CONN_EQ2_B1_CTL,
		0, 6, iir_inp1_text);

static const struct snd_kcontrol_new iir1_inp1_mux =
	SOC_DAPM_ENUM("IIR1 INP1 Mux", iir1_inp1_mux_enum);

static const struct snd_kcontrol_new iir2_inp1_mux =
	SOC_DAPM_ENUM("IIR2 INP1 Mux", iir2_inp1_mux_enum);

static const struct snd_soc_dapm_widget msm8916_wcd_digital_dapm_widgets[] = {
	/*RX stuff */
	SND_SOC_DAPM_AIF_IN("I2S RX1", NULL, 0, SND_SOC_NOPM, 0, 0),
	SND_SOC_DAPM_AIF_IN("I2S RX2", NULL, 0, SND_SOC_NOPM, 0, 0),
	SND_SOC_DAPM_AIF_IN("I2S RX3", NULL, 0, SND_SOC_NOPM, 0, 0),

	SND_SOC_DAPM_OUTPUT("PDM_RX1"),
	SND_SOC_DAPM_OUTPUT("PDM_RX2"),
	SND_SOC_DAPM_OUTPUT("PDM_RX3"),

	SND_SOC_DAPM_INPUT("LPASS_PDM_TX"),

	SND_SOC_DAPM_MIXER("RX1 MIX1", SND_SOC_NOPM, 0, 0, NULL, 0),
	SND_SOC_DAPM_MIXER("RX2 MIX1", SND_SOC_NOPM, 0, 0, NULL, 0),
	SND_SOC_DAPM_MIXER("RX3 MIX1", SND_SOC_NOPM, 0, 0, NULL, 0),

	/* Interpolator */
	SND_SOC_DAPM_MIXER_E("RX1 INT", LPASS_CDC_CLK_RX_B1_CTL, 0, 0, NULL,
			     0, msm8916_wcd_digital_enable_interpolator,
			     SND_SOC_DAPM_POST_PMU | SND_SOC_DAPM_POST_PMD),
	SND_SOC_DAPM_MIXER_E("RX2 INT", LPASS_CDC_CLK_RX_B1_CTL, 1, 0, NULL,
			     0, msm8916_wcd_digital_enable_interpolator,
			     SND_SOC_DAPM_POST_PMU | SND_SOC_DAPM_POST_PMD),
	SND_SOC_DAPM_MIXER_E("RX3 INT", LPASS_CDC_CLK_RX_B1_CTL, 2, 0, NULL,
			     0, msm8916_wcd_digital_enable_interpolator,
			     SND_SOC_DAPM_POST_PMU | SND_SOC_DAPM_POST_PMD),
	SND_SOC_DAPM_MUX("RX1 MIX1 INP1", SND_SOC_NOPM, 0, 0,
			 &rx_mix1_inp1_mux),
	SND_SOC_DAPM_MUX("RX1 MIX1 INP2", SND_SOC_NOPM, 0, 0,
			 &rx_mix1_inp2_mux),
	SND_SOC_DAPM_MUX("RX1 MIX1 INP3", SND_SOC_NOPM, 0, 0,
			 &rx_mix1_inp3_mux),
	SND_SOC_DAPM_MUX("RX2 MIX1 INP1", SND_SOC_NOPM, 0, 0,
			 &rx2_mix1_inp1_mux),
	SND_SOC_DAPM_MUX("RX2 MIX1 INP2", SND_SOC_NOPM, 0, 0,
			 &rx2_mix1_inp2_mux),
	SND_SOC_DAPM_MUX("RX2 MIX1 INP3", SND_SOC_NOPM, 0, 0,
			 &rx2_mix1_inp3_mux),
	SND_SOC_DAPM_MUX("RX3 MIX1 INP1", SND_SOC_NOPM, 0, 0,
			 &rx3_mix1_inp1_mux),
	SND_SOC_DAPM_MUX("RX3 MIX1 INP2", SND_SOC_NOPM, 0, 0,
			 &rx3_mix1_inp2_mux),
	SND_SOC_DAPM_MUX("RX3 MIX1 INP3", SND_SOC_NOPM, 0, 0,
			 &rx3_mix1_inp3_mux),
	SND_SOC_DAPM_MUX("RX1 MIX2 INP1", SND_SOC_NOPM, 0, 0,
			 &rx1_mix2_inp1_mux),
	SND_SOC_DAPM_MUX("RX2 MIX2 INP1", SND_SOC_NOPM, 0, 0,
			 &rx2_mix2_inp1_mux),

	SND_SOC_DAPM_MUX("CIC1 MUX", SND_SOC_NOPM, 0, 0, &cic1_mux),
	SND_SOC_DAPM_MUX("CIC2 MUX", SND_SOC_NOPM, 0, 0, &cic2_mux),
	/* TX */
	SND_SOC_DAPM_MIXER("ADC1", SND_SOC_NOPM, 0, 0, NULL, 0),
	SND_SOC_DAPM_MIXER("ADC2", SND_SOC_NOPM, 0, 0, NULL, 0),
	SND_SOC_DAPM_MIXER("ADC3", SND_SOC_NOPM, 0, 0, NULL, 0),

	SND_SOC_DAPM_MUX_E("DEC1 MUX", LPASS_CDC_CLK_TX_CLK_EN_B1_CTL, 0, 0,
			   &dec1_mux, msm8916_wcd_digital_enable_dec,
			   SND_SOC_DAPM_PRE_PMU | SND_SOC_DAPM_POST_PMU |
			   SND_SOC_DAPM_PRE_PMD | SND_SOC_DAPM_POST_PMD),
	SND_SOC_DAPM_MUX_E("DEC2 MUX", LPASS_CDC_CLK_TX_CLK_EN_B1_CTL, 1, 0,
			   &dec2_mux, msm8916_wcd_digital_enable_dec,
			   SND_SOC_DAPM_PRE_PMU | SND_SOC_DAPM_POST_PMU |
			   SND_SOC_DAPM_PRE_PMD | SND_SOC_DAPM_POST_PMD),
	SND_SOC_DAPM_AIF_OUT("I2S TX1", NULL, 0, SND_SOC_NOPM, 0, 0),
	SND_SOC_DAPM_AIF_OUT("I2S TX2", NULL, 0, SND_SOC_NOPM, 0, 0),
	SND_SOC_DAPM_AIF_OUT("I2S TX3", NULL, 0, SND_SOC_NOPM, 0, 0),

	/* Digital Mic Inputs */
	SND_SOC_DAPM_ADC_E("DMIC1", NULL, SND_SOC_NOPM, 0, 0,
			   msm8916_wcd_digital_enable_dmic,
			   SND_SOC_DAPM_PRE_PMU | SND_SOC_DAPM_POST_PMD),
	SND_SOC_DAPM_ADC_E("DMIC2", NULL, SND_SOC_NOPM, 0, 0,
			   msm8916_wcd_digital_enable_dmic,
			   SND_SOC_DAPM_PRE_PMU | SND_SOC_DAPM_POST_PMD),
	SND_SOC_DAPM_SUPPLY("DMIC_CLK", LPASS_CDC_CLK_DMIC_B1_CTL, 0, 0,
			    NULL, 0),
	SND_SOC_DAPM_SUPPLY("RX_I2S_CLK", LPASS_CDC_CLK_RX_I2S_CTL,
			    4, 0, NULL, 0),
	SND_SOC_DAPM_SUPPLY("TX_I2S_CLK", LPASS_CDC_CLK_TX_I2S_CTL, 4, 0,
			    NULL, 0),

	SND_SOC_DAPM_SUPPLY("MCLK", SND_SOC_NOPM, 0, 0, NULL, 0),
	SND_SOC_DAPM_SUPPLY("PDM_CLK", LPASS_CDC_CLK_PDM_CTL, 0, 0, NULL, 0),
	/* Connectivity Clock */
	SND_SOC_DAPM_SUPPLY_S("CDC_CONN", -2, LPASS_CDC_CLK_OTHR_CTL, 2, 0,
			      NULL, 0),
	SND_SOC_DAPM_MIC("Digital Mic1", NULL),
	SND_SOC_DAPM_MIC("Digital Mic2", NULL),

	/* Sidetone */
	SND_SOC_DAPM_MUX("IIR1 INP1 MUX", SND_SOC_NOPM, 0, 0, &iir1_inp1_mux),
	SND_SOC_DAPM_PGA_E("IIR1", LPASS_CDC_CLK_SD_CTL, 0, 0, NULL, 0,
		msm8x16_wcd_codec_set_iir_gain, SND_SOC_DAPM_POST_PMU),

	SND_SOC_DAPM_MUX("IIR2 INP1 MUX", SND_SOC_NOPM, 0, 0, &iir2_inp1_mux),
	SND_SOC_DAPM_PGA_E("IIR2", LPASS_CDC_CLK_SD_CTL, 1, 0, NULL, 0,
		msm8x16_wcd_codec_set_iir_gain, SND_SOC_DAPM_POST_PMU),

};

static int msm8916_wcd_digital_get_clks(struct platform_device *pdev,
					struct msm8916_wcd_digital_priv	*priv)
{
	struct device *dev = &pdev->dev;

	priv->ahbclk = devm_clk_get(dev, "ahbix-clk");
	if (IS_ERR(priv->ahbclk)) {
		dev_err(dev, "failed to get ahbix clk\n");
		return PTR_ERR(priv->ahbclk);
	}

	priv->mclk = devm_clk_get(dev, "mclk");
	if (IS_ERR(priv->mclk)) {
		dev_err(dev, "failed to get mclk\n");
		return PTR_ERR(priv->mclk);
	}

	return 0;
}

static int msm8916_wcd_digital_component_probe(struct snd_soc_component *component)
{
	struct msm8916_wcd_digital_priv *priv = dev_get_drvdata(component->dev);

	snd_soc_component_set_drvdata(component, priv);

	return 0;
}

static int msm8916_wcd_digital_component_set_sysclk(struct snd_soc_component *component,
						int clk_id, int source,
						unsigned int freq, int dir)
{
	struct msm8916_wcd_digital_priv *p = dev_get_drvdata(component->dev);

	return clk_set_rate(p->mclk, freq);
}

static int msm8916_wcd_digital_hw_params(struct snd_pcm_substream *substream,
					 struct snd_pcm_hw_params *params,
					 struct snd_soc_dai *dai)
{
	u8 tx_fs_rate;
	u8 rx_fs_rate;

	switch (params_rate(params)) {
	case 8000:
		tx_fs_rate = TX_I2S_CTL_TX_I2S_FS_RATE_F_8_KHZ;
		rx_fs_rate = RX_I2S_CTL_RX_I2S_FS_RATE_F_8_KHZ;
		break;
	case 16000:
		tx_fs_rate = TX_I2S_CTL_TX_I2S_FS_RATE_F_16_KHZ;
		rx_fs_rate = RX_I2S_CTL_RX_I2S_FS_RATE_F_16_KHZ;
		break;
	case 32000:
		tx_fs_rate = TX_I2S_CTL_TX_I2S_FS_RATE_F_32_KHZ;
		rx_fs_rate = RX_I2S_CTL_RX_I2S_FS_RATE_F_32_KHZ;
		break;
	case 48000:
		tx_fs_rate = TX_I2S_CTL_TX_I2S_FS_RATE_F_48_KHZ;
		rx_fs_rate = RX_I2S_CTL_RX_I2S_FS_RATE_F_48_KHZ;
		break;
	default:
		dev_err(dai->component->dev, "Invalid sampling rate %d\n",
			params_rate(params));
		return -EINVAL;
	}

	switch (substream->stream) {
	case SNDRV_PCM_STREAM_CAPTURE:
		snd_soc_component_update_bits(dai->component, LPASS_CDC_CLK_TX_I2S_CTL,
				    TX_I2S_CTL_TX_I2S_FS_RATE_MASK, tx_fs_rate);
		break;
	case SNDRV_PCM_STREAM_PLAYBACK:
		snd_soc_component_update_bits(dai->component, LPASS_CDC_CLK_RX_I2S_CTL,
				    RX_I2S_CTL_RX_I2S_FS_RATE_MASK, rx_fs_rate);
		break;
	default:
		return -EINVAL;
	}

	switch (params_format(params)) {
	case SNDRV_PCM_FORMAT_S16_LE:
		snd_soc_component_update_bits(dai->component, LPASS_CDC_CLK_TX_I2S_CTL,
				    TX_I2S_CTL_TX_I2S_MODE_MASK,
				    TX_I2S_CTL_TX_I2S_MODE_16);
		snd_soc_component_update_bits(dai->component, LPASS_CDC_CLK_RX_I2S_CTL,
				    RX_I2S_CTL_RX_I2S_MODE_MASK,
				    RX_I2S_CTL_RX_I2S_MODE_16);
		break;

	case SNDRV_PCM_FORMAT_S32_LE:
		snd_soc_component_update_bits(dai->component, LPASS_CDC_CLK_TX_I2S_CTL,
				    TX_I2S_CTL_TX_I2S_MODE_MASK,
				    TX_I2S_CTL_TX_I2S_MODE_32);
		snd_soc_component_update_bits(dai->component, LPASS_CDC_CLK_RX_I2S_CTL,
				    RX_I2S_CTL_RX_I2S_MODE_MASK,
				    RX_I2S_CTL_RX_I2S_MODE_32);
		break;
	default:
		dev_err(dai->dev, "%s: wrong format selected\n", __func__);
		return -EINVAL;
	}

	return 0;
}

static const struct snd_soc_dapm_route msm8916_wcd_digital_audio_map[] = {

	{"I2S RX1",  NULL, "AIF1 Playback"},
	{"I2S RX2",  NULL, "AIF1 Playback"},
	{"I2S RX3",  NULL, "AIF1 Playback"},

	{"AIF1 Capture", NULL, "I2S TX1"},
	{"AIF1 Capture", NULL, "I2S TX2"},
	{"AIF1 Capture", NULL, "I2S TX3"},

	{"CIC1 MUX", "DMIC", "DEC1 MUX"},
	{"CIC1 MUX", "AMIC", "DEC1 MUX"},
	{"CIC2 MUX", "DMIC", "DEC2 MUX"},
	{"CIC2 MUX", "AMIC", "DEC2 MUX"},

	/* Decimator Inputs */
	{"DEC1 MUX", "DMIC1", "DMIC1"},
	{"DEC1 MUX", "DMIC2", "DMIC2"},
	{"DEC1 MUX", "ADC1", "ADC1"},
	{"DEC1 MUX", "ADC2", "ADC2"},
	{"DEC1 MUX", "ADC3", "ADC3"},
	{"DEC1 MUX", NULL, "CDC_CONN"},

	{"DEC2 MUX", "DMIC1", "DMIC1"},
	{"DEC2 MUX", "DMIC2", "DMIC2"},
	{"DEC2 MUX", "ADC1", "ADC1"},
	{"DEC2 MUX", "ADC2", "ADC2"},
	{"DEC2 MUX", "ADC3", "ADC3"},
	{"DEC2 MUX", NULL, "CDC_CONN"},

	{"DMIC1", NULL, "DMIC_CLK"},
	{"DMIC2", NULL, "DMIC_CLK"},

	{"I2S TX1", NULL, "CIC1 MUX"},
	{"I2S TX2", NULL, "CIC2 MUX"},

	{"I2S TX1", NULL, "TX_I2S_CLK"},
	{"I2S TX2", NULL, "TX_I2S_CLK"},

	{"TX_I2S_CLK", NULL, "MCLK"},
	{"TX_I2S_CLK", NULL, "PDM_CLK"},

	{"ADC1", NULL, "LPASS_PDM_TX"},
	{"ADC2", NULL, "LPASS_PDM_TX"},
	{"ADC3", NULL, "LPASS_PDM_TX"},

	{"I2S RX1", NULL, "RX_I2S_CLK"},
	{"I2S RX2", NULL, "RX_I2S_CLK"},
	{"I2S RX3", NULL, "RX_I2S_CLK"},

	{"RX_I2S_CLK", NULL, "PDM_CLK"},
	{"RX_I2S_CLK", NULL, "MCLK"},
	{"RX_I2S_CLK", NULL, "CDC_CONN"},

	/* RX1 PATH.. */
	{"PDM_RX1", NULL, "RX1 INT"},
	{"RX1 INT", NULL, "RX1 MIX1"},

	{"RX1 MIX1", NULL, "RX1 MIX1 INP1"},
	{"RX1 MIX1", NULL, "RX1 MIX1 INP2"},
	{"RX1 MIX1", NULL, "RX1 MIX1 INP3"},

	{"RX1 MIX1 INP1", "RX1", "I2S RX1"},
	{"RX1 MIX1 INP1", "RX2", "I2S RX2"},
	{"RX1 MIX1 INP1", "RX3", "I2S RX3"},
	{"RX1 MIX1 INP1", "IIR1", "IIR1"},
	{"RX1 MIX1 INP1", "IIR2", "IIR2"},

	{"RX1 MIX1 INP2", "RX1", "I2S RX1"},
	{"RX1 MIX1 INP2", "RX2", "I2S RX2"},
	{"RX1 MIX1 INP2", "RX3", "I2S RX3"},
	{"RX1 MIX1 INP2", "IIR1", "IIR1"},
	{"RX1 MIX1 INP2", "IIR2", "IIR2"},

	{"RX1 MIX1 INP3", "RX1", "I2S RX1"},
	{"RX1 MIX1 INP3", "RX2", "I2S RX2"},
	{"RX1 MIX1 INP3", "RX3", "I2S RX3"},

	/* RX2 PATH */
	{"PDM_RX2", NULL, "RX2 INT"},
	{"RX2 INT", NULL, "RX2 MIX1"},

	{"RX2 MIX1", NULL, "RX2 MIX1 INP1"},
	{"RX2 MIX1", NULL, "RX2 MIX1 INP2"},
	{"RX2 MIX1", NULL, "RX2 MIX1 INP3"},

	{"RX2 MIX1 INP1", "RX1", "I2S RX1"},
	{"RX2 MIX1 INP1", "RX2", "I2S RX2"},
	{"RX2 MIX1 INP1", "RX3", "I2S RX3"},
	{"RX2 MIX1 INP1", "IIR1", "IIR1"},
	{"RX2 MIX1 INP1", "IIR2", "IIR2"},

	{"RX2 MIX1 INP2", "RX1", "I2S RX1"},
	{"RX2 MIX1 INP2", "RX2", "I2S RX2"},
	{"RX2 MIX1 INP2", "RX3", "I2S RX3"},
	{"RX2 MIX1 INP1", "IIR1", "IIR1"},
	{"RX2 MIX1 INP1", "IIR2", "IIR2"},

	{"RX2 MIX1 INP3", "RX1", "I2S RX1"},
	{"RX2 MIX1 INP3", "RX2", "I2S RX2"},
	{"RX2 MIX1 INP3", "RX3", "I2S RX3"},

	/* RX3 PATH */
	{"PDM_RX3", NULL, "RX3 INT"},
	{"RX3 INT", NULL, "RX3 MIX1"},

	{"RX3 MIX1", NULL, "RX3 MIX1 INP1"},
	{"RX3 MIX1", NULL, "RX3 MIX1 INP2"},
	{"RX3 MIX1", NULL, "RX3 MIX1 INP3"},

	{"RX3 MIX1 INP1", "RX1", "I2S RX1"},
	{"RX3 MIX1 INP1", "RX2", "I2S RX2"},
	{"RX3 MIX1 INP1", "RX3", "I2S RX3"},
	{"RX3 MIX1 INP1", "IIR1", "IIR1"},
	{"RX3 MIX1 INP1", "IIR2", "IIR2"},

	{"RX3 MIX1 INP2", "RX1", "I2S RX1"},
	{"RX3 MIX1 INP2", "RX2", "I2S RX2"},
	{"RX3 MIX1 INP2", "RX3", "I2S RX3"},
	{"RX3 MIX1 INP2", "IIR1", "IIR1"},
	{"RX3 MIX1 INP2", "IIR2", "IIR2"},

	{"RX1 MIX2 INP1", "IIR1", "IIR1"},
	{"RX2 MIX2 INP1", "IIR1", "IIR1"},
	{"RX1 MIX2 INP1", "IIR2", "IIR2"},
	{"RX2 MIX2 INP1", "IIR2", "IIR2"},

	{"IIR1", NULL, "IIR1 INP1 MUX"},
	{"IIR1 INP1 MUX", "DEC1", "DEC1 MUX"},
	{"IIR1 INP1 MUX", "DEC2", "DEC2 MUX"},

	{"IIR2", NULL, "IIR2 INP1 MUX"},
	{"IIR2 INP1 MUX", "DEC1", "DEC1 MUX"},
	{"IIR2 INP1 MUX", "DEC2", "DEC2 MUX"},

	{"RX3 MIX1 INP3", "RX1", "I2S RX1"},
	{"RX3 MIX1 INP3", "RX2", "I2S RX2"},
	{"RX3 MIX1 INP3", "RX3", "I2S RX3"},

};

static int msm8916_wcd_digital_startup(struct snd_pcm_substream *substream,
				       struct snd_soc_dai *dai)
{
	struct snd_soc_component *component = dai->component;
	struct msm8916_wcd_digital_priv *msm8916_wcd;
	unsigned long mclk_rate;

	msm8916_wcd = snd_soc_component_get_drvdata(component);
	snd_soc_component_update_bits(component, LPASS_CDC_CLK_MCLK_CTL,
			    MCLK_CTL_MCLK_EN_MASK,
			    MCLK_CTL_MCLK_EN_ENABLE);
	snd_soc_component_update_bits(component, LPASS_CDC_CLK_PDM_CTL,
			    LPASS_CDC_CLK_PDM_CTL_PDM_CLK_SEL_MASK,
			    LPASS_CDC_CLK_PDM_CTL_PDM_CLK_SEL_FB);

	mclk_rate = clk_get_rate(msm8916_wcd->mclk);
	switch (mclk_rate) {
	case 12288000:
		snd_soc_component_update_bits(component, LPASS_CDC_TOP_CTL,
				    TOP_CTL_DIG_MCLK_FREQ_MASK,
				    TOP_CTL_DIG_MCLK_FREQ_F_12_288MHZ);
		break;
	case 9600000:
		snd_soc_component_update_bits(component, LPASS_CDC_TOP_CTL,
				    TOP_CTL_DIG_MCLK_FREQ_MASK,
				    TOP_CTL_DIG_MCLK_FREQ_F_9_6MHZ);
		break;
	default:
		dev_err(component->dev, "Invalid mclk rate %ld\n", mclk_rate);
		break;
	}
	return 0;
}

static void msm8916_wcd_digital_shutdown(struct snd_pcm_substream *substream,
					 struct snd_soc_dai *dai)
{
	snd_soc_component_update_bits(dai->component, LPASS_CDC_CLK_PDM_CTL,
			    LPASS_CDC_CLK_PDM_CTL_PDM_CLK_SEL_MASK, 0);
}

static const struct snd_soc_dai_ops msm8916_wcd_digital_dai_ops = {
	.startup = msm8916_wcd_digital_startup,
	.shutdown = msm8916_wcd_digital_shutdown,
	.hw_params = msm8916_wcd_digital_hw_params,
};

static struct snd_soc_dai_driver msm8916_wcd_digital_dai[] = {
	[0] = {
	       .name = "msm8916_wcd_digital_i2s_rx1",
	       .id = 0,
	       .playback = {
			    .stream_name = "AIF1 Playback",
			    .rates = MSM8916_WCD_DIGITAL_RATES,
			    .formats = MSM8916_WCD_DIGITAL_FORMATS,
			    .channels_min = 1,
			    .channels_max = 3,
			    },
	       .ops = &msm8916_wcd_digital_dai_ops,
	       },
	[1] = {
	       .name = "msm8916_wcd_digital_i2s_tx1",
	       .id = 1,
	       .capture = {
			   .stream_name = "AIF1 Capture",
			   .rates = MSM8916_WCD_DIGITAL_RATES,
			   .formats = MSM8916_WCD_DIGITAL_FORMATS,
			   .channels_min = 1,
			   .channels_max = 4,
			   },
	       .ops = &msm8916_wcd_digital_dai_ops,
	       },
};

static const struct snd_soc_component_driver msm8916_wcd_digital = {
	.probe			= msm8916_wcd_digital_component_probe,
	.set_sysclk		= msm8916_wcd_digital_component_set_sysclk,
	.controls		= msm8916_wcd_digital_snd_controls,
	.num_controls		= ARRAY_SIZE(msm8916_wcd_digital_snd_controls),
	.dapm_widgets		= msm8916_wcd_digital_dapm_widgets,
	.num_dapm_widgets	= ARRAY_SIZE(msm8916_wcd_digital_dapm_widgets),
	.dapm_routes		= msm8916_wcd_digital_audio_map,
	.num_dapm_routes	= ARRAY_SIZE(msm8916_wcd_digital_audio_map),
	.idle_bias_on		= 1,
	.use_pmdown_time	= 1,
	.endianness		= 1,
	.non_legacy_dai_naming	= 1,
};

static const struct regmap_config msm8916_codec_regmap_config = {
	.reg_bits = 32,
	.reg_stride = 4,
	.val_bits = 32,
	.max_register = LPASS_CDC_TX2_DMIC_CTL,
	.cache_type = REGCACHE_FLAT,
};

static int msm8916_wcd_digital_probe(struct platform_device *pdev)
{
	struct msm8916_wcd_digital_priv *priv;
	struct device *dev = &pdev->dev;
	void __iomem *base;
	struct regmap *digital_map;
	int ret;

	priv = devm_kzalloc(dev, sizeof(*priv), GFP_KERNEL);
	if (!priv)
		return -ENOMEM;

	base = devm_platform_ioremap_resource(pdev, 0);
	if (IS_ERR(base))
		return PTR_ERR(base);

	digital_map =
	    devm_regmap_init_mmio(&pdev->dev, base,
				  &msm8916_codec_regmap_config);
	if (IS_ERR(digital_map))
		return PTR_ERR(digital_map);

	ret = msm8916_wcd_digital_get_clks(pdev, priv);
	if (ret < 0)
		return ret;

	ret = clk_prepare_enable(priv->ahbclk);
	if (ret < 0) {
		dev_err(dev, "failed to enable ahbclk %d\n", ret);
		return ret;
	}

	ret = clk_prepare_enable(priv->mclk);
	if (ret < 0) {
		dev_err(dev, "failed to enable mclk %d\n", ret);
		return ret;
	}

	dev_set_drvdata(dev, priv);

	return devm_snd_soc_register_component(dev, &msm8916_wcd_digital,
				      msm8916_wcd_digital_dai,
				      ARRAY_SIZE(msm8916_wcd_digital_dai));
}

static int msm8916_wcd_digital_remove(struct platform_device *pdev)
{
	struct msm8916_wcd_digital_priv *priv = dev_get_drvdata(&pdev->dev);

	clk_disable_unprepare(priv->mclk);
	clk_disable_unprepare(priv->ahbclk);

	return 0;
}

static const struct of_device_id msm8916_wcd_digital_match_table[] = {
	{ .compatible = "qcom,msm8916-wcd-digital-codec" },
	{ }
};

MODULE_DEVICE_TABLE(of, msm8916_wcd_digital_match_table);

static struct platform_driver msm8916_wcd_digital_driver = {
	.driver = {
		   .name = "msm8916-wcd-digital-codec",
		   .of_match_table = msm8916_wcd_digital_match_table,
	},
	.probe = msm8916_wcd_digital_probe,
	.remove = msm8916_wcd_digital_remove,
};

module_platform_driver(msm8916_wcd_digital_driver);

MODULE_AUTHOR("Srinivas Kandagatla <srinivas.kandagatla@linaro.org>");
MODULE_DESCRIPTION("MSM8916 WCD Digital Codec driver");
MODULE_LICENSE("GPL v2");
