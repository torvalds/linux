#include <linux/module.h>
#include <linux/err.h>
#include <linux/kernel.h>
#include <linux/delay.h>
#include <linux/regulator/consumer.h>
#include <linux/types.h>
#include <linux/clk.h>
#include <linux/of.h>
#include <linux/platform_device.h>
#include <linux/regmap.h>
#include <sound/soc.h>
#include <sound/pcm.h>
#include <sound/pcm_params.h>
#include <sound/tlv.h>
#include <sound/jack.h>

#define CDC_D_REVISION1			(0xf000)
#define CDC_D_PERPH_SUBTYPE		(0xf005)
#define CDC_D_INT_EN_SET		(0x015)
#define CDC_D_INT_EN_CLR		(0x016)
#define MBHC_SWITCH_INT			BIT(7)
#define MBHC_MIC_ELECTRICAL_INS_REM_DET	BIT(6)
#define MBHC_BUTTON_PRESS_DET		BIT(5)
#define MBHC_BUTTON_RELEASE_DET		BIT(4)
#define CDC_D_CDC_RST_CTL		(0xf046)
#define RST_CTL_DIG_SW_RST_N_MASK	BIT(7)
#define RST_CTL_DIG_SW_RST_N_RESET	0
#define RST_CTL_DIG_SW_RST_N_REMOVE_RESET BIT(7)

#define CDC_D_CDC_TOP_CLK_CTL		(0xf048)
#define TOP_CLK_CTL_A_MCLK_MCLK2_EN_MASK (BIT(2) | BIT(3))
#define TOP_CLK_CTL_A_MCLK_EN_ENABLE	 BIT(2)
#define TOP_CLK_CTL_A_MCLK2_EN_ENABLE	BIT(3)

#define CDC_D_CDC_ANA_CLK_CTL		(0xf049)
#define ANA_CLK_CTL_EAR_HPHR_CLK_EN_MASK BIT(0)
#define ANA_CLK_CTL_EAR_HPHR_CLK_EN	BIT(0)
#define ANA_CLK_CTL_EAR_HPHL_CLK_EN	BIT(1)
#define ANA_CLK_CTL_SPKR_CLK_EN_MASK	BIT(4)
#define ANA_CLK_CTL_SPKR_CLK_EN	BIT(4)
#define ANA_CLK_CTL_TXA_CLK25_EN	BIT(5)

#define CDC_D_CDC_DIG_CLK_CTL		(0xf04A)
#define DIG_CLK_CTL_RXD1_CLK_EN		BIT(0)
#define DIG_CLK_CTL_RXD2_CLK_EN		BIT(1)
#define DIG_CLK_CTL_RXD3_CLK_EN		BIT(2)
#define DIG_CLK_CTL_D_MBHC_CLK_EN_MASK	BIT(3)
#define DIG_CLK_CTL_D_MBHC_CLK_EN	BIT(3)
#define DIG_CLK_CTL_TXD_CLK_EN		BIT(4)
#define DIG_CLK_CTL_NCP_CLK_EN_MASK	BIT(6)
#define DIG_CLK_CTL_NCP_CLK_EN		BIT(6)
#define DIG_CLK_CTL_RXD_PDM_CLK_EN_MASK	BIT(7)
#define DIG_CLK_CTL_RXD_PDM_CLK_EN	BIT(7)

#define CDC_D_CDC_CONN_TX1_CTL		(0xf050)
#define CONN_TX1_SERIAL_TX1_MUX		GENMASK(1, 0)
#define CONN_TX1_SERIAL_TX1_ADC_1	0x0
#define CONN_TX1_SERIAL_TX1_RX_PDM_LB	0x1
#define CONN_TX1_SERIAL_TX1_ZERO	0x2

#define CDC_D_CDC_CONN_TX2_CTL		(0xf051)
#define CONN_TX2_SERIAL_TX2_MUX		GENMASK(1, 0)
#define CONN_TX2_SERIAL_TX2_ADC_2	0x0
#define CONN_TX2_SERIAL_TX2_RX_PDM_LB	0x1
#define CONN_TX2_SERIAL_TX2_ZERO	0x2
#define CDC_D_CDC_CONN_HPHR_DAC_CTL	(0xf052)
#define CDC_D_CDC_CONN_RX1_CTL		(0xf053)
#define CDC_D_CDC_CONN_RX2_CTL		(0xf054)
#define CDC_D_CDC_CONN_RX3_CTL		(0xf055)
#define CDC_D_CDC_CONN_RX_LB_CTL	(0xf056)
#define CDC_D_SEC_ACCESS		(0xf0D0)
#define CDC_D_PERPH_RESET_CTL3		(0xf0DA)
#define CDC_D_PERPH_RESET_CTL4		(0xf0DB)
#define CDC_A_REVISION1			(0xf100)
#define CDC_A_REVISION2			(0xf101)
#define CDC_A_REVISION3			(0xf102)
#define CDC_A_REVISION4			(0xf103)
#define CDC_A_PERPH_TYPE		(0xf104)
#define CDC_A_PERPH_SUBTYPE		(0xf105)
#define CDC_A_INT_RT_STS		(0xf110)
#define CDC_A_INT_SET_TYPE		(0xf111)
#define CDC_A_INT_POLARITY_HIGH		(0xf112)
#define CDC_A_INT_POLARITY_LOW		(0xf113)
#define CDC_A_INT_LATCHED_CLR		(0xf114)
#define CDC_A_INT_EN_SET		(0xf115)
#define CDC_A_INT_EN_CLR		(0xf116)
#define CDC_A_INT_LATCHED_STS		(0xf118)
#define CDC_A_INT_PENDING_STS		(0xf119)
#define CDC_A_INT_MID_SEL		(0xf11A)
#define CDC_A_INT_PRIORITY		(0xf11B)
#define CDC_A_MICB_1_EN			(0xf140)
#define MICB_1_EN_MICB_ENABLE		BIT(7)
#define MICB_1_EN_BYP_CAP_MASK		BIT(6)
#define MICB_1_EN_NO_EXT_BYP_CAP	BIT(6)
#define MICB_1_EN_EXT_BYP_CAP		0
#define MICB_1_EN_PULL_DOWN_EN_MASK	BIT(5)
#define MICB_1_EN_PULL_DOWN_EN_ENABLE	BIT(5)
#define MICB_1_EN_OPA_STG2_TAIL_CURR_MASK GENMASK(3, 1)
#define MICB_1_EN_OPA_STG2_TAIL_CURR_1_60UA	(0x4)
#define MICB_1_EN_PULL_UP_EN_MASK	BIT(4)
#define MICB_1_EN_TX3_GND_SEL_MASK	BIT(0)
#define MICB_1_EN_TX3_GND_SEL_TX_GND	0

#define CDC_A_MICB_1_VAL		(0xf141)
#define MICB_MIN_VAL 1600
#define MICB_STEP_SIZE 50
#define MICB_VOLTAGE_REGVAL(v)		(((v - MICB_MIN_VAL)/MICB_STEP_SIZE) << 3)
#define MICB_1_VAL_MICB_OUT_VAL_MASK	GENMASK(7, 3)
#define MICB_1_VAL_MICB_OUT_VAL_V2P70V	((0x16)  << 3)
#define MICB_1_VAL_MICB_OUT_VAL_V1P80V	((0x4)  << 3)
#define CDC_A_MICB_1_CTL		(0xf142)

#define MICB_1_CTL_CFILT_REF_SEL_MASK		BIT(1)
#define MICB_1_CTL_CFILT_REF_SEL_HPF_REF	BIT(1)
#define MICB_1_CTL_EXT_PRECHARG_EN_MASK		BIT(5)
#define MICB_1_CTL_EXT_PRECHARG_EN_ENABLE	BIT(5)
#define MICB_1_CTL_INT_PRECHARG_BYP_MASK	BIT(6)
#define MICB_1_CTL_INT_PRECHARG_BYP_EXT_PRECHRG_SEL	BIT(6)

#define CDC_A_MICB_1_INT_RBIAS			(0xf143)
#define MICB_1_INT_TX1_INT_RBIAS_EN_MASK	BIT(7)
#define MICB_1_INT_TX1_INT_RBIAS_EN_ENABLE	BIT(7)
#define MICB_1_INT_TX1_INT_RBIAS_EN_DISABLE	0

#define MICB_1_INT_TX1_INT_PULLUP_EN_MASK	BIT(6)
#define MICB_1_INT_TX1_INT_PULLUP_EN_TX1N_TO_MICBIAS BIT(6)
#define MICB_1_INT_TX1_INT_PULLUP_EN_TX1N_TO_GND	0

#define MICB_1_INT_TX2_INT_RBIAS_EN_MASK	BIT(4)
#define MICB_1_INT_TX2_INT_RBIAS_EN_ENABLE	BIT(4)
#define MICB_1_INT_TX2_INT_RBIAS_EN_DISABLE	0
#define MICB_1_INT_TX2_INT_PULLUP_EN_MASK	BIT(3)
#define MICB_1_INT_TX2_INT_PULLUP_EN_TX1N_TO_MICBIAS BIT(3)
#define MICB_1_INT_TX2_INT_PULLUP_EN_TX1N_TO_GND	0

#define MICB_1_INT_TX3_INT_RBIAS_EN_MASK	BIT(1)
#define MICB_1_INT_TX3_INT_RBIAS_EN_ENABLE	BIT(1)
#define MICB_1_INT_TX3_INT_RBIAS_EN_DISABLE	0
#define MICB_1_INT_TX3_INT_PULLUP_EN_MASK	BIT(0)
#define MICB_1_INT_TX3_INT_PULLUP_EN_TX1N_TO_MICBIAS BIT(0)
#define MICB_1_INT_TX3_INT_PULLUP_EN_TX1N_TO_GND	0

#define CDC_A_MICB_2_EN			(0xf144)
#define CDC_A_MICB_2_EN_ENABLE		BIT(7)
#define CDC_A_MICB_2_PULL_DOWN_EN_MASK	BIT(5)
#define CDC_A_MICB_2_PULL_DOWN_EN	BIT(5)
#define CDC_A_TX_1_2_ATEST_CTL_2	(0xf145)
#define CDC_A_MASTER_BIAS_CTL		(0xf146)
#define CDC_A_MBHC_DET_CTL_1		(0xf147)
#define CDC_A_MBHC_DET_CTL_L_DET_EN			BIT(7)
#define CDC_A_MBHC_DET_CTL_GND_DET_EN			BIT(6)
#define CDC_A_MBHC_DET_CTL_MECH_DET_TYPE_INSERTION	BIT(5)
#define CDC_A_MBHC_DET_CTL_MECH_DET_TYPE_REMOVAL	(0)
#define CDC_A_MBHC_DET_CTL_MECH_DET_TYPE_MASK		BIT(5)
#define CDC_A_MBHC_DET_CTL_MECH_DET_TYPE_SHIFT		(5)
#define CDC_A_MBHC_DET_CTL_MIC_CLAMP_CTL_AUTO		BIT(4)
#define CDC_A_MBHC_DET_CTL_MIC_CLAMP_CTL_MANUAL		BIT(3)
#define CDC_A_MBHC_DET_CTL_MIC_CLAMP_CTL_MASK		GENMASK(4, 3)
#define CDC_A_MBHC_DET_CTL_MBHC_BIAS_EN			BIT(2)
#define CDC_A_MBHC_DET_CTL_2		(0xf150)
#define CDC_A_MBHC_DET_CTL_HS_L_DET_PULL_UP_CTRL_I_3P0	(BIT(7) | BIT(6))
#define CDC_A_MBHC_DET_CTL_HS_L_DET_COMPA_CTRL_V0P9_VDD	BIT(5)
#define CDC_A_PLUG_TYPE_MASK				GENMASK(4, 3)
#define CDC_A_HPHL_PLUG_TYPE_NO				BIT(4)
#define CDC_A_GND_PLUG_TYPE_NO				BIT(3)
#define CDC_A_MBHC_DET_CTL_HPHL_100K_TO_GND_EN_MASK	BIT(0)
#define CDC_A_MBHC_DET_CTL_HPHL_100K_TO_GND_EN		BIT(0)
#define CDC_A_MBHC_FSM_CTL		(0xf151)
#define CDC_A_MBHC_FSM_CTL_MBHC_FSM_EN			BIT(7)
#define CDC_A_MBHC_FSM_CTL_MBHC_FSM_EN_MASK		BIT(7)
#define CDC_A_MBHC_FSM_CTL_BTN_ISRC_CTRL_I_100UA	(0x3 << 4)
#define CDC_A_MBHC_FSM_CTL_BTN_ISRC_CTRL_MASK		GENMASK(6, 4)
#define CDC_A_MBHC_DBNC_TIMER		(0xf152)
#define CDC_A_MBHC_DBNC_TIMER_BTN_DBNC_T_16MS		BIT(3)
#define CDC_A_MBHC_DBNC_TIMER_INSREM_DBNC_T_256_MS	(0x9 << 4)
#define CDC_A_MBHC_BTN0_ZDET_CTL_0	(0xf153)
#define CDC_A_MBHC_BTN1_ZDET_CTL_1	(0xf154)
#define CDC_A_MBHC_BTN2_ZDET_CTL_2	(0xf155)
#define CDC_A_MBHC_BTN3_CTL		(0xf156)
#define CDC_A_MBHC_BTN4_CTL		(0xf157)
#define CDC_A_MBHC_BTN_VREF_FINE_SHIFT	(2)
#define CDC_A_MBHC_BTN_VREF_FINE_MASK	GENMASK(4, 2)
#define CDC_A_MBHC_BTN_VREF_COARSE_MASK	GENMASK(7, 5)
#define CDC_A_MBHC_BTN_VREF_COARSE_SHIFT (5)
#define CDC_A_MBHC_BTN_VREF_MASK	(CDC_A_MBHC_BTN_VREF_COARSE_MASK | \
					CDC_A_MBHC_BTN_VREF_FINE_MASK)
#define CDC_A_MBHC_RESULT_1		(0xf158)
#define CDC_A_MBHC_RESULT_1_BTN_RESULT_MASK	GENMASK(4, 0)
#define CDC_A_TX_1_EN			(0xf160)
#define CDC_A_TX_2_EN			(0xf161)
#define CDC_A_TX_1_2_TEST_CTL_1		(0xf162)
#define CDC_A_TX_1_2_TEST_CTL_2		(0xf163)
#define CDC_A_TX_1_2_ATEST_CTL		(0xf164)
#define CDC_A_TX_1_2_OPAMP_BIAS		(0xf165)
#define CDC_A_TX_3_EN			(0xf167)
#define CDC_A_NCP_EN			(0xf180)
#define CDC_A_NCP_CLK			(0xf181)
#define CDC_A_NCP_FBCTRL		(0xf183)
#define CDC_A_NCP_FBCTRL_FB_CLK_INV_MASK	BIT(5)
#define CDC_A_NCP_FBCTRL_FB_CLK_INV		BIT(5)
#define CDC_A_NCP_BIAS			(0xf184)
#define CDC_A_NCP_VCTRL			(0xf185)
#define CDC_A_NCP_TEST			(0xf186)
#define CDC_A_NCP_CLIM_ADDR		(0xf187)
#define CDC_A_RX_CLOCK_DIVIDER		(0xf190)
#define CDC_A_RX_COM_OCP_CTL		(0xf191)
#define CDC_A_RX_COM_OCP_COUNT		(0xf192)
#define CDC_A_RX_COM_BIAS_DAC		(0xf193)
#define RX_COM_BIAS_DAC_RX_BIAS_EN_MASK		BIT(7)
#define RX_COM_BIAS_DAC_RX_BIAS_EN_ENABLE	BIT(7)
#define RX_COM_BIAS_DAC_DAC_REF_EN_MASK		BIT(0)
#define RX_COM_BIAS_DAC_DAC_REF_EN_ENABLE	BIT(0)

#define CDC_A_RX_HPH_BIAS_PA		(0xf194)
#define CDC_A_RX_HPH_BIAS_LDO_OCP	(0xf195)
#define CDC_A_RX_HPH_BIAS_CNP		(0xf196)
#define CDC_A_RX_HPH_CNP_EN		(0xf197)
#define CDC_A_RX_HPH_L_PA_DAC_CTL	(0xf19B)
#define RX_HPA_L_PA_DAC_CTL_DATA_RESET_MASK	BIT(1)
#define RX_HPA_L_PA_DAC_CTL_DATA_RESET_RESET	BIT(1)
#define CDC_A_RX_HPH_R_PA_DAC_CTL	(0xf19D)
#define RX_HPH_R_PA_DAC_CTL_DATA_RESET	BIT(1)
#define RX_HPH_R_PA_DAC_CTL_DATA_RESET_MASK BIT(1)

#define CDC_A_RX_EAR_CTL			(0xf19E)
#define RX_EAR_CTL_SPK_VBAT_LDO_EN_MASK		BIT(0)
#define RX_EAR_CTL_SPK_VBAT_LDO_EN_ENABLE	BIT(0)

#define CDC_A_SPKR_DAC_CTL		(0xf1B0)
#define SPKR_DAC_CTL_DAC_RESET_MASK	BIT(4)
#define SPKR_DAC_CTL_DAC_RESET_NORMAL	0

#define CDC_A_SPKR_DRV_CTL		(0xf1B2)
#define SPKR_DRV_CTL_DEF_MASK		0xEF
#define SPKR_DRV_CLASSD_PA_EN_MASK	BIT(7)
#define SPKR_DRV_CLASSD_PA_EN_ENABLE	BIT(7)
#define SPKR_DRV_CAL_EN			BIT(6)
#define SPKR_DRV_SETTLE_EN		BIT(5)
#define SPKR_DRV_FW_EN			BIT(3)
#define SPKR_DRV_BOOST_SET		BIT(2)
#define SPKR_DRV_CMFB_SET		BIT(1)
#define SPKR_DRV_GAIN_SET		BIT(0)
#define SPKR_DRV_CTL_DEF_VAL (SPKR_DRV_CLASSD_PA_EN_ENABLE | \
		SPKR_DRV_CAL_EN | SPKR_DRV_SETTLE_EN | \
		SPKR_DRV_FW_EN | SPKR_DRV_BOOST_SET | \
		SPKR_DRV_CMFB_SET | SPKR_DRV_GAIN_SET)
#define CDC_A_SPKR_OCP_CTL		(0xf1B4)
#define CDC_A_SPKR_PWRSTG_CTL		(0xf1B5)
#define SPKR_PWRSTG_CTL_DAC_EN_MASK	BIT(0)
#define SPKR_PWRSTG_CTL_DAC_EN		BIT(0)
#define SPKR_PWRSTG_CTL_MASK		0xE0
#define SPKR_PWRSTG_CTL_BBM_MASK	BIT(7)
#define SPKR_PWRSTG_CTL_BBM_EN		BIT(7)
#define SPKR_PWRSTG_CTL_HBRDGE_EN_MASK	BIT(6)
#define SPKR_PWRSTG_CTL_HBRDGE_EN	BIT(6)
#define SPKR_PWRSTG_CTL_CLAMP_EN_MASK	BIT(5)
#define SPKR_PWRSTG_CTL_CLAMP_EN	BIT(5)

#define CDC_A_SPKR_DRV_DBG		(0xf1B7)
#define CDC_A_CURRENT_LIMIT		(0xf1C0)
#define CDC_A_BOOST_EN_CTL		(0xf1C3)
#define CDC_A_SLOPE_COMP_IP_ZERO	(0xf1C4)
#define CDC_A_SEC_ACCESS		(0xf1D0)
#define CDC_A_PERPH_RESET_CTL3		(0xf1DA)
#define CDC_A_PERPH_RESET_CTL4		(0xf1DB)

#define MSM8916_WCD_ANALOG_RATES (SNDRV_PCM_RATE_8000 | SNDRV_PCM_RATE_16000 |\
			SNDRV_PCM_RATE_32000 | SNDRV_PCM_RATE_48000)
#define MSM8916_WCD_ANALOG_FORMATS (SNDRV_PCM_FMTBIT_S16_LE |\
				    SNDRV_PCM_FMTBIT_S32_LE)

static int btn_mask = SND_JACK_BTN_0 | SND_JACK_BTN_1 |
	       SND_JACK_BTN_2 | SND_JACK_BTN_3 | SND_JACK_BTN_4;
static int hs_jack_mask = SND_JACK_HEADPHONE | SND_JACK_HEADSET;

static const char * const supply_names[] = {
	"vdd-cdc-io",
	"vdd-cdc-tx-rx-cx",
};

#define MBHC_MAX_BUTTONS	(5)

struct pm8916_wcd_analog_priv {
	u16 pmic_rev;
	u16 codec_version;
	bool	mbhc_btn_enabled;
	/* special event to detect accessory type */
	int	mbhc_btn0_released;
	bool	detect_accessory_type;
	struct clk *mclk;
	struct snd_soc_codec *codec;
	struct regulator_bulk_data supplies[ARRAY_SIZE(supply_names)];
	struct snd_soc_jack *jack;
	bool hphl_jack_type_normally_open;
	bool gnd_jack_type_normally_open;
	/* Voltage threshold when internal current source of 100uA is used */
	u32 vref_btn_cs[MBHC_MAX_BUTTONS];
	/* Voltage threshold when microphone bias is ON */
	u32 vref_btn_micb[MBHC_MAX_BUTTONS];
	unsigned int micbias1_cap_mode;
	unsigned int micbias2_cap_mode;
	unsigned int micbias_mv;
};

static const char *const adc2_mux_text[] = { "ZERO", "INP2", "INP3" };
static const char *const rdac2_mux_text[] = { "ZERO", "RX2", "RX1" };
static const char *const hph_text[] = { "ZERO", "Switch", };

static const struct soc_enum hph_enum = SOC_ENUM_SINGLE_VIRT(
					ARRAY_SIZE(hph_text), hph_text);

static const struct snd_kcontrol_new hphl_mux = SOC_DAPM_ENUM("HPHL", hph_enum);
static const struct snd_kcontrol_new hphr_mux = SOC_DAPM_ENUM("HPHR", hph_enum);

/* ADC2 MUX */
static const struct soc_enum adc2_enum = SOC_ENUM_SINGLE_VIRT(
			ARRAY_SIZE(adc2_mux_text), adc2_mux_text);

/* RDAC2 MUX */
static const struct soc_enum rdac2_mux_enum = SOC_ENUM_SINGLE(
			CDC_D_CDC_CONN_HPHR_DAC_CTL, 0, 3, rdac2_mux_text);

static const struct snd_kcontrol_new spkr_switch[] = {
	SOC_DAPM_SINGLE("Switch", CDC_A_SPKR_DAC_CTL, 7, 1, 0)
};

static const struct snd_kcontrol_new rdac2_mux = SOC_DAPM_ENUM(
					"RDAC2 MUX Mux", rdac2_mux_enum);
static const struct snd_kcontrol_new tx_adc2_mux = SOC_DAPM_ENUM(
					"ADC2 MUX Mux", adc2_enum);

/* Analog Gain control 0 dB to +24 dB in 6 dB steps */
static const DECLARE_TLV_DB_SCALE(analog_gain, 0, 600, 0);

static const struct snd_kcontrol_new pm8916_wcd_analog_snd_controls[] = {
	SOC_SINGLE_TLV("ADC1 Volume", CDC_A_TX_1_EN, 3, 8, 0, analog_gain),
	SOC_SINGLE_TLV("ADC2 Volume", CDC_A_TX_2_EN, 3, 8, 0, analog_gain),
	SOC_SINGLE_TLV("ADC3 Volume", CDC_A_TX_3_EN, 3, 8, 0, analog_gain),
};

static void pm8916_wcd_analog_micbias_enable(struct snd_soc_codec *codec)
{
	struct pm8916_wcd_analog_priv *wcd = snd_soc_codec_get_drvdata(codec);

	snd_soc_update_bits(codec, CDC_A_MICB_1_CTL,
			    MICB_1_CTL_EXT_PRECHARG_EN_MASK |
			    MICB_1_CTL_INT_PRECHARG_BYP_MASK,
			    MICB_1_CTL_INT_PRECHARG_BYP_EXT_PRECHRG_SEL
			    | MICB_1_CTL_EXT_PRECHARG_EN_ENABLE);

	if (wcd->micbias_mv) {
		snd_soc_update_bits(codec, CDC_A_MICB_1_VAL,
				    MICB_1_VAL_MICB_OUT_VAL_MASK,
				    MICB_VOLTAGE_REGVAL(wcd->micbias_mv));
		/*
		 * Special headset needs MICBIAS as 2.7V so wait for
		 * 50 msec for the MICBIAS to reach 2.7 volts.
		 */
		if (wcd->micbias_mv >= 2700)
			msleep(50);
	}

	snd_soc_update_bits(codec, CDC_A_MICB_1_CTL,
			    MICB_1_CTL_EXT_PRECHARG_EN_MASK |
			    MICB_1_CTL_INT_PRECHARG_BYP_MASK, 0);

}

static int pm8916_wcd_analog_enable_micbias_ext(struct snd_soc_codec
						 *codec, int event,
						 int reg, unsigned int cap_mode)
{
	switch (event) {
	case SND_SOC_DAPM_POST_PMU:
		pm8916_wcd_analog_micbias_enable(codec);
		snd_soc_update_bits(codec, CDC_A_MICB_1_EN,
				    MICB_1_EN_BYP_CAP_MASK, cap_mode);
		break;
	}

	return 0;
}

static int pm8916_wcd_analog_enable_micbias_int(struct snd_soc_codec
						 *codec, int event,
						 int reg, u32 cap_mode)
{

	switch (event) {
	case SND_SOC_DAPM_PRE_PMU:
		snd_soc_update_bits(codec, CDC_A_MICB_1_INT_RBIAS,
				    MICB_1_INT_TX2_INT_RBIAS_EN_MASK,
				    MICB_1_INT_TX2_INT_RBIAS_EN_ENABLE);
		snd_soc_update_bits(codec, reg, MICB_1_EN_PULL_DOWN_EN_MASK, 0);
		snd_soc_update_bits(codec, CDC_A_MICB_1_EN,
				    MICB_1_EN_OPA_STG2_TAIL_CURR_MASK,
				    MICB_1_EN_OPA_STG2_TAIL_CURR_1_60UA);

		break;
	case SND_SOC_DAPM_POST_PMU:
		pm8916_wcd_analog_micbias_enable(codec);
		snd_soc_update_bits(codec, CDC_A_MICB_1_EN,
				    MICB_1_EN_BYP_CAP_MASK, cap_mode);
		break;
	}

	return 0;
}

static int pm8916_wcd_analog_enable_micbias_ext1(struct
						  snd_soc_dapm_widget
						  *w, struct snd_kcontrol
						  *kcontrol, int event)
{
	struct snd_soc_codec *codec = snd_soc_dapm_to_codec(w->dapm);
	struct pm8916_wcd_analog_priv *wcd = snd_soc_codec_get_drvdata(codec);

	return pm8916_wcd_analog_enable_micbias_ext(codec, event, w->reg,
						     wcd->micbias1_cap_mode);
}

static int pm8916_wcd_analog_enable_micbias_ext2(struct
						  snd_soc_dapm_widget
						  *w, struct snd_kcontrol
						  *kcontrol, int event)
{
	struct snd_soc_codec *codec = snd_soc_dapm_to_codec(w->dapm);
	struct pm8916_wcd_analog_priv *wcd = snd_soc_codec_get_drvdata(codec);

	return pm8916_wcd_analog_enable_micbias_ext(codec, event, w->reg,
						     wcd->micbias2_cap_mode);

}

static int pm8916_wcd_analog_enable_micbias_int1(struct
						  snd_soc_dapm_widget
						  *w, struct snd_kcontrol
						  *kcontrol, int event)
{
	struct snd_soc_codec *codec = snd_soc_dapm_to_codec(w->dapm);
	struct pm8916_wcd_analog_priv *wcd = snd_soc_codec_get_drvdata(codec);

	return pm8916_wcd_analog_enable_micbias_int(codec, event, w->reg,
						     wcd->micbias1_cap_mode);
}

static int pm8916_mbhc_configure_bias(struct pm8916_wcd_analog_priv *priv,
				      bool micbias2_enabled)
{
	struct snd_soc_codec *codec = priv->codec;
	u32 coarse, fine, reg_val, reg_addr;
	int *vrefs, i;

	if (!micbias2_enabled) { /* use internal 100uA Current source */
		/* Enable internal 2.2k Internal Rbias Resistor */
		snd_soc_update_bits(codec, CDC_A_MICB_1_INT_RBIAS,
				    MICB_1_INT_TX2_INT_RBIAS_EN_MASK,
				    MICB_1_INT_TX2_INT_RBIAS_EN_ENABLE);
		/* Remove pull down on MIC BIAS2 */
		snd_soc_update_bits(codec, CDC_A_MICB_2_EN,
				   CDC_A_MICB_2_PULL_DOWN_EN_MASK,
				   0);
		/* enable 100uA internal current source */
		snd_soc_update_bits(codec, CDC_A_MBHC_FSM_CTL,
				    CDC_A_MBHC_FSM_CTL_BTN_ISRC_CTRL_MASK,
				    CDC_A_MBHC_FSM_CTL_BTN_ISRC_CTRL_I_100UA);
	}
	snd_soc_update_bits(codec, CDC_A_MBHC_FSM_CTL,
			CDC_A_MBHC_FSM_CTL_MBHC_FSM_EN_MASK,
			CDC_A_MBHC_FSM_CTL_MBHC_FSM_EN);

	if (micbias2_enabled)
		vrefs = &priv->vref_btn_micb[0];
	else
		vrefs = &priv->vref_btn_cs[0];

	/* program vref ranges for all the buttons */
	reg_addr = CDC_A_MBHC_BTN0_ZDET_CTL_0;
	for (i = 0; i <  MBHC_MAX_BUTTONS; i++) {
		/* split mv in to coarse parts of 100mv & fine parts of 12mv */
		coarse = (vrefs[i] / 100);
		fine = ((vrefs[i] % 100) / 12);
		reg_val = (coarse << CDC_A_MBHC_BTN_VREF_COARSE_SHIFT) |
			 (fine << CDC_A_MBHC_BTN_VREF_FINE_SHIFT);
		snd_soc_update_bits(codec, reg_addr,
			       CDC_A_MBHC_BTN_VREF_MASK,
			       reg_val);
		reg_addr++;
	}

	return 0;
}

static void pm8916_wcd_setup_mbhc(struct pm8916_wcd_analog_priv *wcd)
{
	struct snd_soc_codec *codec = wcd->codec;
	bool micbias_enabled = false;
	u32 plug_type = 0;
	u32 int_en_mask;

	snd_soc_write(codec, CDC_A_MBHC_DET_CTL_1,
		      CDC_A_MBHC_DET_CTL_L_DET_EN |
		      CDC_A_MBHC_DET_CTL_MECH_DET_TYPE_INSERTION |
		      CDC_A_MBHC_DET_CTL_MIC_CLAMP_CTL_AUTO |
		      CDC_A_MBHC_DET_CTL_MBHC_BIAS_EN);

	if (wcd->hphl_jack_type_normally_open)
		plug_type |= CDC_A_HPHL_PLUG_TYPE_NO;

	if (wcd->gnd_jack_type_normally_open)
		plug_type |= CDC_A_GND_PLUG_TYPE_NO;

	snd_soc_write(codec, CDC_A_MBHC_DET_CTL_2,
		      CDC_A_MBHC_DET_CTL_HS_L_DET_PULL_UP_CTRL_I_3P0 |
		      CDC_A_MBHC_DET_CTL_HS_L_DET_COMPA_CTRL_V0P9_VDD |
		      plug_type |
		      CDC_A_MBHC_DET_CTL_HPHL_100K_TO_GND_EN);


	snd_soc_write(codec, CDC_A_MBHC_DBNC_TIMER,
		      CDC_A_MBHC_DBNC_TIMER_INSREM_DBNC_T_256_MS |
		      CDC_A_MBHC_DBNC_TIMER_BTN_DBNC_T_16MS);

	/* enable MBHC clock */
	snd_soc_update_bits(codec, CDC_D_CDC_DIG_CLK_CTL,
			    DIG_CLK_CTL_D_MBHC_CLK_EN_MASK,
			    DIG_CLK_CTL_D_MBHC_CLK_EN);

	if (snd_soc_read(codec, CDC_A_MICB_2_EN) & CDC_A_MICB_2_EN_ENABLE)
		micbias_enabled = true;

	pm8916_mbhc_configure_bias(wcd, micbias_enabled);

	int_en_mask = MBHC_SWITCH_INT;
	if (wcd->mbhc_btn_enabled)
		int_en_mask |= MBHC_BUTTON_PRESS_DET | MBHC_BUTTON_RELEASE_DET;

	snd_soc_update_bits(codec, CDC_D_INT_EN_CLR, int_en_mask, 0);
	snd_soc_update_bits(codec, CDC_D_INT_EN_SET, int_en_mask, int_en_mask);
	wcd->mbhc_btn0_released = false;
	wcd->detect_accessory_type = true;
}

static int pm8916_wcd_analog_enable_micbias_int2(struct
						  snd_soc_dapm_widget
						  *w, struct snd_kcontrol
						  *kcontrol, int event)
{
	struct snd_soc_codec *codec = snd_soc_dapm_to_codec(w->dapm);
	struct pm8916_wcd_analog_priv *wcd = snd_soc_codec_get_drvdata(codec);

	switch (event) {
	case SND_SOC_DAPM_POST_PMU:
		pm8916_mbhc_configure_bias(wcd, true);
		break;
	case SND_SOC_DAPM_POST_PMD:
		pm8916_mbhc_configure_bias(wcd, false);
		break;
	}

	return pm8916_wcd_analog_enable_micbias_int(codec, event, w->reg,
						     wcd->micbias2_cap_mode);
}

static int pm8916_wcd_analog_enable_adc(struct snd_soc_dapm_widget *w,
					 struct snd_kcontrol *kcontrol,
					 int event)
{
	struct snd_soc_codec *codec = snd_soc_dapm_to_codec(w->dapm);
	u16 adc_reg = CDC_A_TX_1_2_TEST_CTL_2;
	u8 init_bit_shift;

	if (w->reg == CDC_A_TX_1_EN)
		init_bit_shift = 5;
	else
		init_bit_shift = 4;

	switch (event) {
	case SND_SOC_DAPM_PRE_PMU:
		if (w->reg == CDC_A_TX_2_EN)
			snd_soc_update_bits(codec, CDC_A_MICB_1_CTL,
					    MICB_1_CTL_CFILT_REF_SEL_MASK,
					    MICB_1_CTL_CFILT_REF_SEL_HPF_REF);
		/*
		 * Add delay of 10 ms to give sufficient time for the voltage
		 * to shoot up and settle so that the txfe init does not
		 * happen when the input voltage is changing too much.
		 */
		usleep_range(10000, 10010);
		snd_soc_update_bits(codec, adc_reg, 1 << init_bit_shift,
				    1 << init_bit_shift);
		switch (w->reg) {
		case CDC_A_TX_1_EN:
			snd_soc_update_bits(codec, CDC_D_CDC_CONN_TX1_CTL,
					    CONN_TX1_SERIAL_TX1_MUX,
					    CONN_TX1_SERIAL_TX1_ADC_1);
			break;
		case CDC_A_TX_2_EN:
		case CDC_A_TX_3_EN:
			snd_soc_update_bits(codec, CDC_D_CDC_CONN_TX2_CTL,
					    CONN_TX2_SERIAL_TX2_MUX,
					    CONN_TX2_SERIAL_TX2_ADC_2);
			break;
		}
		break;
	case SND_SOC_DAPM_POST_PMU:
		/*
		 * Add delay of 12 ms before deasserting the init
		 * to reduce the tx pop
		 */
		usleep_range(12000, 12010);
		snd_soc_update_bits(codec, adc_reg, 1 << init_bit_shift, 0x00);
		break;
	case SND_SOC_DAPM_POST_PMD:
		switch (w->reg) {
		case CDC_A_TX_1_EN:
			snd_soc_update_bits(codec, CDC_D_CDC_CONN_TX1_CTL,
					    CONN_TX1_SERIAL_TX1_MUX,
					    CONN_TX1_SERIAL_TX1_ZERO);
			break;
		case CDC_A_TX_2_EN:
			snd_soc_update_bits(codec, CDC_A_MICB_1_CTL,
					    MICB_1_CTL_CFILT_REF_SEL_MASK, 0);
			/* fall through */
		case CDC_A_TX_3_EN:
			snd_soc_update_bits(codec, CDC_D_CDC_CONN_TX2_CTL,
					    CONN_TX2_SERIAL_TX2_MUX,
					    CONN_TX2_SERIAL_TX2_ZERO);
			break;
		}


		break;
	}
	return 0;
}

static int pm8916_wcd_analog_enable_spk_pa(struct snd_soc_dapm_widget *w,
					    struct snd_kcontrol *kcontrol,
					    int event)
{
	struct snd_soc_codec *codec = snd_soc_dapm_to_codec(w->dapm);

	switch (event) {
	case SND_SOC_DAPM_PRE_PMU:
		snd_soc_update_bits(codec, CDC_A_SPKR_PWRSTG_CTL,
				    SPKR_PWRSTG_CTL_DAC_EN_MASK |
				    SPKR_PWRSTG_CTL_BBM_MASK |
				    SPKR_PWRSTG_CTL_HBRDGE_EN_MASK |
				    SPKR_PWRSTG_CTL_CLAMP_EN_MASK,
				    SPKR_PWRSTG_CTL_DAC_EN|
				    SPKR_PWRSTG_CTL_BBM_EN |
				    SPKR_PWRSTG_CTL_HBRDGE_EN |
				    SPKR_PWRSTG_CTL_CLAMP_EN);

		snd_soc_update_bits(codec, CDC_A_RX_EAR_CTL,
				    RX_EAR_CTL_SPK_VBAT_LDO_EN_MASK,
				    RX_EAR_CTL_SPK_VBAT_LDO_EN_ENABLE);
		break;
	case SND_SOC_DAPM_POST_PMU:
		snd_soc_update_bits(codec, CDC_A_SPKR_DRV_CTL,
				    SPKR_DRV_CTL_DEF_MASK,
				    SPKR_DRV_CTL_DEF_VAL);
		snd_soc_update_bits(codec, w->reg,
				    SPKR_DRV_CLASSD_PA_EN_MASK,
				    SPKR_DRV_CLASSD_PA_EN_ENABLE);
		break;
	case SND_SOC_DAPM_POST_PMD:
		snd_soc_update_bits(codec, CDC_A_SPKR_PWRSTG_CTL,
				    SPKR_PWRSTG_CTL_DAC_EN_MASK|
				    SPKR_PWRSTG_CTL_BBM_MASK |
				    SPKR_PWRSTG_CTL_HBRDGE_EN_MASK |
				    SPKR_PWRSTG_CTL_CLAMP_EN_MASK, 0);

		snd_soc_update_bits(codec, CDC_A_SPKR_DAC_CTL,
				    SPKR_DAC_CTL_DAC_RESET_MASK,
				    SPKR_DAC_CTL_DAC_RESET_NORMAL);
		snd_soc_update_bits(codec, CDC_A_RX_EAR_CTL,
				    RX_EAR_CTL_SPK_VBAT_LDO_EN_MASK, 0);
		break;
	}
	return 0;
}

static const struct reg_default wcd_reg_defaults_2_0[] = {
	{CDC_A_RX_COM_OCP_CTL, 0xD1},
	{CDC_A_RX_COM_OCP_COUNT, 0xFF},
	{CDC_D_SEC_ACCESS, 0xA5},
	{CDC_D_PERPH_RESET_CTL3, 0x0F},
	{CDC_A_TX_1_2_OPAMP_BIAS, 0x4F},
	{CDC_A_NCP_FBCTRL, 0x28},
	{CDC_A_SPKR_DRV_CTL, 0x69},
	{CDC_A_SPKR_DRV_DBG, 0x01},
	{CDC_A_BOOST_EN_CTL, 0x5F},
	{CDC_A_SLOPE_COMP_IP_ZERO, 0x88},
	{CDC_A_SEC_ACCESS, 0xA5},
	{CDC_A_PERPH_RESET_CTL3, 0x0F},
	{CDC_A_CURRENT_LIMIT, 0x82},
	{CDC_A_SPKR_DAC_CTL, 0x03},
	{CDC_A_SPKR_OCP_CTL, 0xE1},
	{CDC_A_MASTER_BIAS_CTL, 0x30},
};

static int pm8916_wcd_analog_probe(struct snd_soc_codec *codec)
{
	struct pm8916_wcd_analog_priv *priv = dev_get_drvdata(codec->dev);
	int err, reg;

	err = regulator_bulk_enable(ARRAY_SIZE(priv->supplies), priv->supplies);
	if (err != 0) {
		dev_err(codec->dev, "failed to enable regulators (%d)\n", err);
		return err;
	}

	snd_soc_codec_init_regmap(codec,
				  dev_get_regmap(codec->dev->parent, NULL));
	snd_soc_codec_set_drvdata(codec, priv);
	priv->pmic_rev = snd_soc_read(codec, CDC_D_REVISION1);
	priv->codec_version = snd_soc_read(codec, CDC_D_PERPH_SUBTYPE);

	dev_info(codec->dev, "PMIC REV: %d\t CODEC Version: %d\n",
		 priv->pmic_rev, priv->codec_version);

	snd_soc_write(codec, CDC_D_PERPH_RESET_CTL4, 0x01);
	snd_soc_write(codec, CDC_A_PERPH_RESET_CTL4, 0x01);

	for (reg = 0; reg < ARRAY_SIZE(wcd_reg_defaults_2_0); reg++)
		snd_soc_write(codec, wcd_reg_defaults_2_0[reg].reg,
			      wcd_reg_defaults_2_0[reg].def);

	priv->codec = codec;

	snd_soc_update_bits(codec, CDC_D_CDC_RST_CTL,
			    RST_CTL_DIG_SW_RST_N_MASK,
			    RST_CTL_DIG_SW_RST_N_REMOVE_RESET);

	pm8916_wcd_setup_mbhc(priv);

	return 0;
}

static int pm8916_wcd_analog_remove(struct snd_soc_codec *codec)
{
	struct pm8916_wcd_analog_priv *priv = dev_get_drvdata(codec->dev);

	snd_soc_update_bits(codec, CDC_D_CDC_RST_CTL,
			    RST_CTL_DIG_SW_RST_N_MASK, 0);

	return regulator_bulk_disable(ARRAY_SIZE(priv->supplies),
				      priv->supplies);
}

static const struct snd_soc_dapm_route pm8916_wcd_analog_audio_map[] = {

	{"PDM_RX1", NULL, "PDM Playback"},
	{"PDM_RX2", NULL, "PDM Playback"},
	{"PDM_RX3", NULL, "PDM Playback"},
	{"PDM Capture", NULL, "PDM_TX"},

	/* ADC Connections */
	{"PDM_TX", NULL, "ADC2"},
	{"PDM_TX", NULL, "ADC3"},
	{"ADC2", NULL, "ADC2 MUX"},
	{"ADC3", NULL, "ADC2 MUX"},
	{"ADC2 MUX", "INP2", "ADC2_INP2"},
	{"ADC2 MUX", "INP3", "ADC2_INP3"},

	{"PDM_TX", NULL, "ADC1"},
	{"ADC1", NULL, "AMIC1"},
	{"ADC2_INP2", NULL, "AMIC2"},
	{"ADC2_INP3", NULL, "AMIC3"},

	/* RDAC Connections */
	{"HPHR DAC", NULL, "RDAC2 MUX"},
	{"RDAC2 MUX", "RX1", "PDM_RX1"},
	{"RDAC2 MUX", "RX2", "PDM_RX2"},
	{"HPHL DAC", NULL, "PDM_RX1"},
	{"PDM_RX1", NULL, "RXD1_CLK"},
	{"PDM_RX2", NULL, "RXD2_CLK"},
	{"PDM_RX3", NULL, "RXD3_CLK"},

	{"PDM_RX1", NULL, "RXD_PDM_CLK"},
	{"PDM_RX2", NULL, "RXD_PDM_CLK"},
	{"PDM_RX3", NULL, "RXD_PDM_CLK"},

	{"ADC1", NULL, "TXD_CLK"},
	{"ADC2", NULL, "TXD_CLK"},
	{"ADC3", NULL, "TXD_CLK"},

	{"ADC1", NULL, "TXA_CLK25"},
	{"ADC2", NULL, "TXA_CLK25"},
	{"ADC3", NULL, "TXA_CLK25"},

	{"PDM_RX1", NULL, "A_MCLK2"},
	{"PDM_RX2", NULL, "A_MCLK2"},
	{"PDM_RX3", NULL, "A_MCLK2"},

	{"PDM_TX", NULL, "A_MCLK2"},
	{"A_MCLK2", NULL, "A_MCLK"},

	/* Headset (RX MIX1 and RX MIX2) */
	{"HEADPHONE", NULL, "HPHL PA"},
	{"HEADPHONE", NULL, "HPHR PA"},

	{"HPHL PA", NULL, "EAR_HPHL_CLK"},
	{"HPHR PA", NULL, "EAR_HPHR_CLK"},

	{"CP", NULL, "NCP_CLK"},

	{"HPHL PA", NULL, "HPHL"},
	{"HPHR PA", NULL, "HPHR"},
	{"HPHL PA", NULL, "CP"},
	{"HPHL PA", NULL, "RX_BIAS"},
	{"HPHR PA", NULL, "CP"},
	{"HPHR PA", NULL, "RX_BIAS"},
	{"HPHL", "Switch", "HPHL DAC"},
	{"HPHR", "Switch", "HPHR DAC"},

	{"RX_BIAS", NULL, "DAC_REF"},

	{"SPK_OUT", NULL, "SPK PA"},
	{"SPK PA", NULL, "RX_BIAS"},
	{"SPK PA", NULL, "SPKR_CLK"},
	{"SPK PA", NULL, "SPK DAC"},
	{"SPK DAC", "Switch", "PDM_RX3"},

	{"MIC BIAS Internal1", NULL, "INT_LDO_H"},
	{"MIC BIAS Internal2", NULL, "INT_LDO_H"},
	{"MIC BIAS External1", NULL, "INT_LDO_H"},
	{"MIC BIAS External2", NULL, "INT_LDO_H"},
	{"MIC BIAS Internal1", NULL, "vdd-micbias"},
	{"MIC BIAS Internal2", NULL, "vdd-micbias"},
	{"MIC BIAS External1", NULL, "vdd-micbias"},
	{"MIC BIAS External2", NULL, "vdd-micbias"},
};

static const struct snd_soc_dapm_widget pm8916_wcd_analog_dapm_widgets[] = {

	SND_SOC_DAPM_AIF_IN("PDM_RX1", NULL, 0, SND_SOC_NOPM, 0, 0),
	SND_SOC_DAPM_AIF_IN("PDM_RX2", NULL, 0, SND_SOC_NOPM, 0, 0),
	SND_SOC_DAPM_AIF_IN("PDM_RX3", NULL, 0, SND_SOC_NOPM, 0, 0),
	SND_SOC_DAPM_AIF_OUT("PDM_TX", NULL, 0, SND_SOC_NOPM, 0, 0),

	SND_SOC_DAPM_INPUT("AMIC1"),
	SND_SOC_DAPM_INPUT("AMIC3"),
	SND_SOC_DAPM_INPUT("AMIC2"),
	SND_SOC_DAPM_OUTPUT("HEADPHONE"),

	/* RX stuff */
	SND_SOC_DAPM_SUPPLY("INT_LDO_H", SND_SOC_NOPM, 1, 0, NULL, 0),

	SND_SOC_DAPM_PGA("HPHL PA", CDC_A_RX_HPH_CNP_EN, 5, 0, NULL, 0),
	SND_SOC_DAPM_MUX("HPHL", SND_SOC_NOPM, 0, 0, &hphl_mux),
	SND_SOC_DAPM_MIXER("HPHL DAC", CDC_A_RX_HPH_L_PA_DAC_CTL, 3, 0, NULL,
			   0),
	SND_SOC_DAPM_PGA("HPHR PA", CDC_A_RX_HPH_CNP_EN, 4, 0, NULL, 0),
	SND_SOC_DAPM_MUX("HPHR", SND_SOC_NOPM, 0, 0, &hphr_mux),
	SND_SOC_DAPM_MIXER("HPHR DAC", CDC_A_RX_HPH_R_PA_DAC_CTL, 3, 0, NULL,
			   0),
	SND_SOC_DAPM_MIXER("SPK DAC", SND_SOC_NOPM, 0, 0,
			   spkr_switch, ARRAY_SIZE(spkr_switch)),

	/* Speaker */
	SND_SOC_DAPM_OUTPUT("SPK_OUT"),
	SND_SOC_DAPM_PGA_E("SPK PA", CDC_A_SPKR_DRV_CTL,
			   6, 0, NULL, 0,
			   pm8916_wcd_analog_enable_spk_pa,
			   SND_SOC_DAPM_PRE_PMU | SND_SOC_DAPM_POST_PMU |
			   SND_SOC_DAPM_PRE_PMD | SND_SOC_DAPM_POST_PMD),
	SND_SOC_DAPM_REGULATOR_SUPPLY("vdd-micbias", 0, 0),
	SND_SOC_DAPM_SUPPLY("CP", CDC_A_NCP_EN, 0, 0, NULL, 0),

	SND_SOC_DAPM_SUPPLY("DAC_REF", CDC_A_RX_COM_BIAS_DAC, 0, 0, NULL, 0),
	SND_SOC_DAPM_SUPPLY("RX_BIAS", CDC_A_RX_COM_BIAS_DAC, 7, 0, NULL, 0),

	/* TX */
	SND_SOC_DAPM_SUPPLY("MIC BIAS Internal1", CDC_A_MICB_1_EN, 7, 0,
			    pm8916_wcd_analog_enable_micbias_int1,
			    SND_SOC_DAPM_PRE_PMU | SND_SOC_DAPM_POST_PMU |
			    SND_SOC_DAPM_POST_PMD),
	SND_SOC_DAPM_SUPPLY("MIC BIAS Internal2", CDC_A_MICB_2_EN, 7, 0,
			    pm8916_wcd_analog_enable_micbias_int2,
			    SND_SOC_DAPM_PRE_PMU | SND_SOC_DAPM_POST_PMU |
			    SND_SOC_DAPM_POST_PMD),

	SND_SOC_DAPM_SUPPLY("MIC BIAS External1", CDC_A_MICB_1_EN, 7, 0,
			    pm8916_wcd_analog_enable_micbias_ext1,
			    SND_SOC_DAPM_PRE_PMU | SND_SOC_DAPM_POST_PMD),
	SND_SOC_DAPM_SUPPLY("MIC BIAS External2", CDC_A_MICB_2_EN, 7, 0,
			    pm8916_wcd_analog_enable_micbias_ext2,
			    SND_SOC_DAPM_POST_PMU | SND_SOC_DAPM_POST_PMD),

	SND_SOC_DAPM_ADC_E("ADC1", NULL, CDC_A_TX_1_EN, 7, 0,
			   pm8916_wcd_analog_enable_adc,
			   SND_SOC_DAPM_PRE_PMU | SND_SOC_DAPM_POST_PMU |
			   SND_SOC_DAPM_POST_PMD),
	SND_SOC_DAPM_ADC_E("ADC2_INP2", NULL, CDC_A_TX_2_EN, 7, 0,
			   pm8916_wcd_analog_enable_adc,
			   SND_SOC_DAPM_PRE_PMU | SND_SOC_DAPM_POST_PMU |
			   SND_SOC_DAPM_POST_PMD),
	SND_SOC_DAPM_ADC_E("ADC2_INP3", NULL, CDC_A_TX_3_EN, 7, 0,
			   pm8916_wcd_analog_enable_adc,
			   SND_SOC_DAPM_PRE_PMU | SND_SOC_DAPM_POST_PMU |
			   SND_SOC_DAPM_POST_PMD),

	SND_SOC_DAPM_MIXER("ADC2", SND_SOC_NOPM, 0, 0, NULL, 0),
	SND_SOC_DAPM_MIXER("ADC3", SND_SOC_NOPM, 0, 0, NULL, 0),

	SND_SOC_DAPM_MUX("ADC2 MUX", SND_SOC_NOPM, 0, 0, &tx_adc2_mux),
	SND_SOC_DAPM_MUX("RDAC2 MUX", SND_SOC_NOPM, 0, 0, &rdac2_mux),

	/* Analog path clocks */
	SND_SOC_DAPM_SUPPLY("EAR_HPHR_CLK", CDC_D_CDC_ANA_CLK_CTL, 0, 0, NULL,
			    0),
	SND_SOC_DAPM_SUPPLY("EAR_HPHL_CLK", CDC_D_CDC_ANA_CLK_CTL, 1, 0, NULL,
			    0),
	SND_SOC_DAPM_SUPPLY("SPKR_CLK", CDC_D_CDC_ANA_CLK_CTL, 4, 0, NULL, 0),
	SND_SOC_DAPM_SUPPLY("TXA_CLK25", CDC_D_CDC_ANA_CLK_CTL, 5, 0, NULL, 0),

	/* Digital path clocks */

	SND_SOC_DAPM_SUPPLY("RXD1_CLK", CDC_D_CDC_DIG_CLK_CTL, 0, 0, NULL, 0),
	SND_SOC_DAPM_SUPPLY("RXD2_CLK", CDC_D_CDC_DIG_CLK_CTL, 1, 0, NULL, 0),
	SND_SOC_DAPM_SUPPLY("RXD3_CLK", CDC_D_CDC_DIG_CLK_CTL, 2, 0, NULL, 0),

	SND_SOC_DAPM_SUPPLY("TXD_CLK", CDC_D_CDC_DIG_CLK_CTL, 4, 0, NULL, 0),
	SND_SOC_DAPM_SUPPLY("NCP_CLK", CDC_D_CDC_DIG_CLK_CTL, 6, 0, NULL, 0),
	SND_SOC_DAPM_SUPPLY("RXD_PDM_CLK", CDC_D_CDC_DIG_CLK_CTL, 7, 0, NULL,
			    0),

	/* System Clock source */
	SND_SOC_DAPM_SUPPLY("A_MCLK", CDC_D_CDC_TOP_CLK_CTL, 2, 0, NULL, 0),
	/* TX ADC and RX DAC Clock source. */
	SND_SOC_DAPM_SUPPLY("A_MCLK2", CDC_D_CDC_TOP_CLK_CTL, 3, 0, NULL, 0),
};

static int pm8916_wcd_analog_set_jack(struct snd_soc_codec *codec,
				      struct snd_soc_jack *jack,
				      void *data)
{
	struct pm8916_wcd_analog_priv *wcd = snd_soc_codec_get_drvdata(codec);

	wcd->jack = jack;

	return 0;
}

static irqreturn_t mbhc_btn_release_irq_handler(int irq, void *arg)
{
	struct pm8916_wcd_analog_priv *priv = arg;

	if (priv->detect_accessory_type) {
		struct snd_soc_codec *codec = priv->codec;
		u32 val = snd_soc_read(codec, CDC_A_MBHC_RESULT_1);

		/* check if its BTN0 thats released */
		if ((val != -1) && !(val & CDC_A_MBHC_RESULT_1_BTN_RESULT_MASK))
			priv->mbhc_btn0_released = true;

	} else {
		snd_soc_jack_report(priv->jack, 0, btn_mask);
	}

	return IRQ_HANDLED;
}

static irqreturn_t mbhc_btn_press_irq_handler(int irq, void *arg)
{
	struct pm8916_wcd_analog_priv *priv = arg;
	struct snd_soc_codec *codec = priv->codec;
	u32 btn_result;

	btn_result = snd_soc_read(codec, CDC_A_MBHC_RESULT_1) &
				  CDC_A_MBHC_RESULT_1_BTN_RESULT_MASK;

	switch (btn_result) {
	case 0xf:
		snd_soc_jack_report(priv->jack, SND_JACK_BTN_4, btn_mask);
		break;
	case 0x7:
		snd_soc_jack_report(priv->jack, SND_JACK_BTN_3, btn_mask);
		break;
	case 0x3:
		snd_soc_jack_report(priv->jack, SND_JACK_BTN_2, btn_mask);
		break;
	case 0x1:
		snd_soc_jack_report(priv->jack, SND_JACK_BTN_1, btn_mask);
		break;
	case 0x0:
		/* handle BTN_0 specially for type detection */
		if (!priv->detect_accessory_type)
			snd_soc_jack_report(priv->jack,
					    SND_JACK_BTN_0, btn_mask);
		break;
	default:
		dev_err(codec->dev,
			"Unexpected button press result (%x)", btn_result);
		break;
	}

	return IRQ_HANDLED;
}

static irqreturn_t pm8916_mbhc_switch_irq_handler(int irq, void *arg)
{
	struct pm8916_wcd_analog_priv *priv = arg;
	struct snd_soc_codec *codec = priv->codec;
	bool ins = false;

	if (snd_soc_read(codec, CDC_A_MBHC_DET_CTL_1) &
				CDC_A_MBHC_DET_CTL_MECH_DET_TYPE_MASK)
		ins = true;

	/* Set the detection type appropriately */
	snd_soc_update_bits(codec, CDC_A_MBHC_DET_CTL_1,
			    CDC_A_MBHC_DET_CTL_MECH_DET_TYPE_MASK,
			    (!ins << CDC_A_MBHC_DET_CTL_MECH_DET_TYPE_SHIFT));


	if (ins) { /* hs insertion */
		bool micbias_enabled = false;

		if (snd_soc_read(codec, CDC_A_MICB_2_EN) &
				CDC_A_MICB_2_EN_ENABLE)
			micbias_enabled = true;

		pm8916_mbhc_configure_bias(priv, micbias_enabled);

		/*
		 * if only a btn0 press event is receive just before
		 * insert event then its a 3 pole headphone else if
		 * both press and release event received then its
		 * a headset.
		 */
		if (priv->mbhc_btn0_released)
			snd_soc_jack_report(priv->jack,
					    SND_JACK_HEADSET, hs_jack_mask);
		else
			snd_soc_jack_report(priv->jack,
					    SND_JACK_HEADPHONE, hs_jack_mask);

		priv->detect_accessory_type = false;

	} else { /* removal */
		snd_soc_jack_report(priv->jack, 0, hs_jack_mask);
		priv->detect_accessory_type = true;
		priv->mbhc_btn0_released = false;
	}

	return IRQ_HANDLED;
}

static struct snd_soc_dai_driver pm8916_wcd_analog_dai[] = {
	[0] = {
	       .name = "pm8916_wcd_analog_pdm_rx",
	       .id = 0,
	       .playback = {
			    .stream_name = "PDM Playback",
			    .rates = MSM8916_WCD_ANALOG_RATES,
			    .formats = MSM8916_WCD_ANALOG_FORMATS,
			    .channels_min = 1,
			    .channels_max = 3,
			    },
	       },
	[1] = {
	       .name = "pm8916_wcd_analog_pdm_tx",
	       .id = 1,
	       .capture = {
			   .stream_name = "PDM Capture",
			   .rates = MSM8916_WCD_ANALOG_RATES,
			   .formats = MSM8916_WCD_ANALOG_FORMATS,
			   .channels_min = 1,
			   .channels_max = 4,
			   },
	       },
};

static const struct snd_soc_codec_driver pm8916_wcd_analog = {
	.probe = pm8916_wcd_analog_probe,
	.remove = pm8916_wcd_analog_remove,
	.set_jack = pm8916_wcd_analog_set_jack,
	.component_driver = {
		.controls = pm8916_wcd_analog_snd_controls,
		.num_controls = ARRAY_SIZE(pm8916_wcd_analog_snd_controls),
		.dapm_widgets = pm8916_wcd_analog_dapm_widgets,
		.num_dapm_widgets = ARRAY_SIZE(pm8916_wcd_analog_dapm_widgets),
		.dapm_routes = pm8916_wcd_analog_audio_map,
		.num_dapm_routes = ARRAY_SIZE(pm8916_wcd_analog_audio_map),
	},
};

static int pm8916_wcd_analog_parse_dt(struct device *dev,
				       struct pm8916_wcd_analog_priv *priv)
{
	int rval;

	if (of_property_read_bool(dev->of_node, "qcom,micbias1-ext-cap"))
		priv->micbias1_cap_mode = MICB_1_EN_EXT_BYP_CAP;
	else
		priv->micbias1_cap_mode = MICB_1_EN_NO_EXT_BYP_CAP;

	if (of_property_read_bool(dev->of_node, "qcom,micbias2-ext-cap"))
		priv->micbias2_cap_mode = MICB_1_EN_EXT_BYP_CAP;
	else
		priv->micbias2_cap_mode = MICB_1_EN_NO_EXT_BYP_CAP;

	of_property_read_u32(dev->of_node, "qcom,micbias-lvl",
			     &priv->micbias_mv);

	if (of_property_read_bool(dev->of_node,
				  "qcom,hphl-jack-type-normally-open"))
		priv->hphl_jack_type_normally_open = true;
	else
		priv->hphl_jack_type_normally_open = false;

	if (of_property_read_bool(dev->of_node,
				  "qcom,gnd-jack-type-normally-open"))
		priv->gnd_jack_type_normally_open = true;
	else
		priv->gnd_jack_type_normally_open = false;

	priv->mbhc_btn_enabled = true;
	rval = of_property_read_u32_array(dev->of_node,
					  "qcom,mbhc-vthreshold-low",
					  &priv->vref_btn_cs[0],
					  MBHC_MAX_BUTTONS);
	if (rval < 0) {
		priv->mbhc_btn_enabled = false;
	} else {
		rval = of_property_read_u32_array(dev->of_node,
						  "qcom,mbhc-vthreshold-high",
						  &priv->vref_btn_micb[0],
						  MBHC_MAX_BUTTONS);
		if (rval < 0)
			priv->mbhc_btn_enabled = false;
	}

	if (!priv->mbhc_btn_enabled)
		dev_err(dev,
			"DT property missing, MBHC btn detection disabled\n");


	return 0;
}

static int pm8916_wcd_analog_spmi_probe(struct platform_device *pdev)
{
	struct pm8916_wcd_analog_priv *priv;
	struct device *dev = &pdev->dev;
	int ret, i, irq;

	priv = devm_kzalloc(dev, sizeof(*priv), GFP_KERNEL);
	if (!priv)
		return -ENOMEM;

	ret = pm8916_wcd_analog_parse_dt(dev, priv);
	if (ret < 0)
		return ret;

	priv->mclk = devm_clk_get(dev, "mclk");
	if (IS_ERR(priv->mclk)) {
		dev_err(dev, "failed to get mclk\n");
		return PTR_ERR(priv->mclk);
	}

	for (i = 0; i < ARRAY_SIZE(supply_names); i++)
		priv->supplies[i].supply = supply_names[i];

	ret = devm_regulator_bulk_get(dev, ARRAY_SIZE(priv->supplies),
				    priv->supplies);
	if (ret) {
		dev_err(dev, "Failed to get regulator supplies %d\n", ret);
		return ret;
	}

	ret = clk_prepare_enable(priv->mclk);
	if (ret < 0) {
		dev_err(dev, "failed to enable mclk %d\n", ret);
		return ret;
	}

	irq = platform_get_irq_byname(pdev, "mbhc_switch_int");
	if (irq < 0) {
		dev_err(dev, "failed to get mbhc switch irq\n");
		return irq;
	}

	ret = devm_request_irq(dev, irq, pm8916_mbhc_switch_irq_handler,
			       IRQF_TRIGGER_RISING | IRQF_TRIGGER_FALLING |
			       IRQF_ONESHOT,
			       "mbhc switch irq", priv);
	if (ret)
		dev_err(dev, "cannot request mbhc switch irq\n");

	if (priv->mbhc_btn_enabled) {
		irq = platform_get_irq_byname(pdev, "mbhc_but_press_det");
		if (irq < 0) {
			dev_err(dev, "failed to get button press irq\n");
			return irq;
		}

		ret = devm_request_irq(dev, irq, mbhc_btn_press_irq_handler,
				       IRQF_TRIGGER_RISING |
				       IRQF_TRIGGER_FALLING | IRQF_ONESHOT,
				       "mbhc btn press irq", priv);
		if (ret)
			dev_err(dev, "cannot request mbhc button press irq\n");

		irq = platform_get_irq_byname(pdev, "mbhc_but_rel_det");
		if (irq < 0) {
			dev_err(dev, "failed to get button release irq\n");
			return irq;
		}

		ret = devm_request_irq(dev, irq, mbhc_btn_release_irq_handler,
				       IRQF_TRIGGER_RISING |
				       IRQF_TRIGGER_FALLING | IRQF_ONESHOT,
				       "mbhc btn release irq", priv);
		if (ret)
			dev_err(dev, "cannot request mbhc button release irq\n");

	}

	dev_set_drvdata(dev, priv);

	return snd_soc_register_codec(dev, &pm8916_wcd_analog,
				      pm8916_wcd_analog_dai,
				      ARRAY_SIZE(pm8916_wcd_analog_dai));
}

static int pm8916_wcd_analog_spmi_remove(struct platform_device *pdev)
{
	struct pm8916_wcd_analog_priv *priv = dev_get_drvdata(&pdev->dev);

	snd_soc_unregister_codec(&pdev->dev);
	clk_disable_unprepare(priv->mclk);

	return 0;
}

static const struct of_device_id pm8916_wcd_analog_spmi_match_table[] = {
	{ .compatible = "qcom,pm8916-wcd-analog-codec", },
	{ }
};

MODULE_DEVICE_TABLE(of, pm8916_wcd_analog_spmi_match_table);

static struct platform_driver pm8916_wcd_analog_spmi_driver = {
	.driver = {
		   .name = "qcom,pm8916-wcd-spmi-codec",
		   .of_match_table = pm8916_wcd_analog_spmi_match_table,
	},
	.probe = pm8916_wcd_analog_spmi_probe,
	.remove = pm8916_wcd_analog_spmi_remove,
};

module_platform_driver(pm8916_wcd_analog_spmi_driver);

MODULE_AUTHOR("Srinivas Kandagatla <srinivas.kandagatla@linaro.org>");
MODULE_DESCRIPTION("PMIC PM8916 WCD Analog Codec driver");
MODULE_LICENSE("GPL v2");
