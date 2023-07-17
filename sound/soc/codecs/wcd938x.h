/* SPDX-License-Identifier: GPL-2.0 */
#ifndef __WCD938X_H__
#define __WCD938X_H__
#include <linux/soundwire/sdw.h>
#include <linux/soundwire/sdw_type.h>

#define WCD938X_BASE_ADDRESS			(0x3000)
#define WCD938X_ANA_PAGE_REGISTER               (0x3000)
#define WCD938X_ANA_BIAS                        (0x3001)
#define WCD938X_ANA_RX_SUPPLIES                 (0x3008)
#define WCD938X_RX_BIAS_EN_MASK			BIT(0)
#define WCD938X_REGULATOR_MODE_MASK		BIT(1)
#define WCD938X_REGULATOR_MODE_CLASS_AB		1
#define WCD938X_VNEG_EN_MASK			BIT(6)
#define WCD938X_VPOS_EN_MASK			BIT(7)
#define WCD938X_ANA_HPH                         (0x3009)
#define WCD938X_HPHR_REF_EN_MASK		BIT(4)
#define WCD938X_HPHL_REF_EN_MASK		BIT(5)
#define WCD938X_HPHR_EN_MASK			BIT(6)
#define WCD938X_HPHL_EN_MASK			BIT(7)
#define WCD938X_ANA_EAR                         (0x300A)
#define WCD938X_ANA_EAR_COMPANDER_CTL           (0x300B)
#define WCD938X_GAIN_OVRD_REG_MASK		BIT(7)
#define WCD938X_EAR_GAIN_MASK			GENMASK(6, 2)
#define WCD938X_ANA_TX_CH1                      (0x300E)
#define WCD938X_ANA_TX_CH2                      (0x300F)
#define WCD938X_HPF1_INIT_MASK			BIT(6)
#define WCD938X_HPF2_INIT_MASK			BIT(5)
#define WCD938X_ANA_TX_CH3                      (0x3010)
#define WCD938X_ANA_TX_CH4                      (0x3011)
#define WCD938X_HPF3_INIT_MASK			BIT(6)
#define WCD938X_HPF4_INIT_MASK			BIT(5)
#define WCD938X_ANA_MICB1_MICB2_DSP_EN_LOGIC    (0x3012)
#define WCD938X_ANA_MICB3_DSP_EN_LOGIC          (0x3013)
#define WCD938X_ANA_MBHC_MECH                   (0x3014)
#define WCD938X_MBHC_L_DET_EN_MASK		BIT(7)
#define WCD938X_MBHC_L_DET_EN			BIT(7)
#define WCD938X_MBHC_GND_DET_EN_MASK		BIT(6)
#define WCD938X_MBHC_MECH_DETECT_TYPE_MASK	BIT(5)
#define WCD938X_MBHC_MECH_DETECT_TYPE_INS	1
#define WCD938X_MBHC_HPHL_PLUG_TYPE_MASK	BIT(4)
#define WCD938X_MBHC_HPHL_PLUG_TYPE_NO		1
#define WCD938X_MBHC_GND_PLUG_TYPE_MASK		BIT(3)
#define WCD938X_MBHC_GND_PLUG_TYPE_NO		1
#define WCD938X_MBHC_HSL_PULLUP_COMP_EN		BIT(2)
#define WCD938X_MBHC_HSG_PULLUP_COMP_EN		BIT(1)
#define WCD938X_MBHC_HPHL_100K_TO_GND_EN	BIT(0)

#define WCD938X_ANA_MBHC_ELECT                  (0x3015)
#define WCD938X_ANA_MBHC_BD_ISRC_CTL_MASK	GENMASK(6, 4)
#define WCD938X_ANA_MBHC_BD_ISRC_100UA		GENMASK(5, 4)
#define WCD938X_ANA_MBHC_BD_ISRC_OFF		0
#define WCD938X_ANA_MBHC_BIAS_EN_MASK		BIT(0)
#define WCD938X_ANA_MBHC_BIAS_EN		BIT(0)
#define WCD938X_ANA_MBHC_ZDET                   (0x3016)
#define WCD938X_ANA_MBHC_RESULT_1               (0x3017)
#define WCD938X_ANA_MBHC_RESULT_2               (0x3018)
#define WCD938X_ANA_MBHC_RESULT_3               (0x3019)
#define WCD938X_MBHC_BTN_RESULT_MASK		GENMASK(2, 0)
#define WCD938X_ANA_MBHC_BTN0                   (0x301A)
#define WCD938X_MBHC_BTN_VTH_MASK		GENMASK(7, 2)
#define WCD938X_ANA_MBHC_BTN1                   (0x301B)
#define WCD938X_ANA_MBHC_BTN2                   (0x301C)
#define WCD938X_ANA_MBHC_BTN3                   (0x301D)
#define WCD938X_ANA_MBHC_BTN4                   (0x301E)
#define WCD938X_ANA_MBHC_BTN5                   (0x301F)
#define WCD938X_VTH_MASK			GENMASK(7, 2)
#define WCD938X_ANA_MBHC_BTN6                   (0x3020)
#define WCD938X_ANA_MBHC_BTN7                   (0x3021)
#define WCD938X_ANA_MICB1                       (0x3022)
#define WCD938X_MICB_VOUT_MASK			GENMASK(5, 0)
#define WCD938X_MICB_EN_MASK			GENMASK(7, 6)
#define WCD938X_MICB_DISABLE			0
#define WCD938X_MICB_ENABLE			1
#define WCD938X_MICB_PULL_UP			2
#define WCD938X_MICB_PULL_DOWN			3
#define WCD938X_ANA_MICB2                       (0x3023)
#define WCD938X_ANA_MICB2_ENABLE		BIT(6)
#define WCD938X_ANA_MICB2_ENABLE_MASK		GENMASK(7, 6)
#define WCD938X_ANA_MICB2_VOUT_MASK		GENMASK(5, 0)
#define WCD938X_ANA_MICB2_RAMP                  (0x3024)
#define WCD938X_RAMP_EN_MASK			BIT(7)
#define WCD938X_RAMP_SHIFT_CTRL_MASK		GENMASK(4, 2)
#define WCD938X_ANA_MICB3                       (0x3025)
#define WCD938X_ANA_MICB4                       (0x3026)
#define WCD938X_BIAS_CTL                        (0x3028)
#define WCD938X_BIAS_VBG_FINE_ADJ               (0x3029)
#define WCD938X_LDOL_VDDCX_ADJUST               (0x3040)
#define WCD938X_LDOL_DISABLE_LDOL               (0x3041)
#define WCD938X_MBHC_CTL_CLK                    (0x3056)
#define WCD938X_MBHC_CTL_ANA                    (0x3057)
#define WCD938X_MBHC_CTL_SPARE_1                (0x3058)
#define WCD938X_MBHC_CTL_SPARE_2                (0x3059)
#define WCD938X_MBHC_CTL_BCS                    (0x305A)
#define WCD938X_MBHC_MOISTURE_DET_FSM_STATUS    (0x305B)
#define WCD938X_MBHC_TEST_CTL                   (0x305C)
#define WCD938X_LDOH_MODE                       (0x3067)
#define WCD938X_LDOH_EN_MASK			BIT(7)
#define WCD938X_LDOH_BIAS                       (0x3068)
#define WCD938X_LDOH_STB_LOADS                  (0x3069)
#define WCD938X_LDOH_SLOWRAMP                   (0x306A)
#define WCD938X_MICB1_TEST_CTL_1                (0x306B)
#define WCD938X_MICB1_TEST_CTL_2                (0x306C)
#define WCD938X_MICB1_TEST_CTL_3                (0x306D)
#define WCD938X_MICB2_TEST_CTL_1                (0x306E)
#define WCD938X_MICB2_TEST_CTL_2                (0x306F)
#define WCD938X_MICB2_TEST_CTL_3                (0x3070)
#define WCD938X_MICB3_TEST_CTL_1                (0x3071)
#define WCD938X_MICB3_TEST_CTL_2                (0x3072)
#define WCD938X_MICB3_TEST_CTL_3                (0x3073)
#define WCD938X_MICB4_TEST_CTL_1                (0x3074)
#define WCD938X_MICB4_TEST_CTL_2                (0x3075)
#define WCD938X_MICB4_TEST_CTL_3                (0x3076)
#define WCD938X_TX_COM_ADC_VCM                  (0x3077)
#define WCD938X_TX_COM_BIAS_ATEST               (0x3078)
#define WCD938X_TX_COM_SPARE1                   (0x3079)
#define WCD938X_TX_COM_SPARE2                   (0x307A)
#define WCD938X_TX_COM_TXFE_DIV_CTL             (0x307B)
#define WCD938X_TX_COM_TXFE_DIV_START           (0x307C)
#define WCD938X_TX_COM_SPARE3                   (0x307D)
#define WCD938X_TX_COM_SPARE4                   (0x307E)
#define WCD938X_TX_1_2_TEST_EN                  (0x307F)
#define WCD938X_TX_1_2_ADC_IB                   (0x3080)
#define WCD938X_TX_1_2_ATEST_REFCTL             (0x3081)
#define WCD938X_TX_1_2_TEST_CTL                 (0x3082)
#define WCD938X_TX_1_2_TEST_BLK_EN1             (0x3083)
#define WCD938X_TX_1_2_TXFE1_CLKDIV             (0x3084)
#define WCD938X_TX_1_2_SAR2_ERR                 (0x3085)
#define WCD938X_TX_1_2_SAR1_ERR                 (0x3086)
#define WCD938X_TX_3_4_TEST_EN                  (0x3087)
#define WCD938X_TX_3_4_ADC_IB                   (0x3088)
#define WCD938X_TX_3_4_ATEST_REFCTL             (0x3089)
#define WCD938X_TX_3_4_TEST_CTL                 (0x308A)
#define WCD938X_TX_3_4_TEST_BLK_EN3             (0x308B)
#define WCD938X_TX_3_4_TXFE3_CLKDIV             (0x308C)
#define WCD938X_TX_3_4_SAR4_ERR                 (0x308D)
#define WCD938X_TX_3_4_SAR3_ERR                 (0x308E)
#define WCD938X_TX_3_4_TEST_BLK_EN2             (0x308F)
#define WCD938X_TX_3_4_TXFE2_CLKDIV             (0x3090)
#define WCD938X_TX_3_4_SPARE1                   (0x3091)
#define WCD938X_TX_3_4_TEST_BLK_EN4             (0x3092)
#define WCD938X_TX_3_4_TXFE4_CLKDIV             (0x3093)
#define WCD938X_TX_3_4_SPARE2                   (0x3094)
#define WCD938X_CLASSH_MODE_1                   (0x3097)
#define WCD938X_CLASSH_MODE_2                   (0x3098)
#define WCD938X_CLASSH_MODE_3                   (0x3099)
#define WCD938X_CLASSH_CTRL_VCL_1               (0x309A)
#define WCD938X_CLASSH_CTRL_VCL_2               (0x309B)
#define WCD938X_CLASSH_CTRL_CCL_1               (0x309C)
#define WCD938X_CLASSH_CTRL_CCL_2               (0x309D)
#define WCD938X_CLASSH_CTRL_CCL_3               (0x309E)
#define WCD938X_CLASSH_CTRL_CCL_4               (0x309F)
#define WCD938X_CLASSH_CTRL_CCL_5               (0x30A0)
#define WCD938X_CLASSH_BUCK_TMUX_A_D            (0x30A1)
#define WCD938X_CLASSH_BUCK_SW_DRV_CNTL         (0x30A2)
#define WCD938X_CLASSH_SPARE                    (0x30A3)
#define WCD938X_FLYBACK_EN                      (0x30A4)
#define WCD938X_EN_CUR_DET_MASK			BIT(2)
#define WCD938X_FLYBACK_VNEG_CTRL_1             (0x30A5)
#define WCD938X_FLYBACK_VNEG_CTRL_2             (0x30A6)
#define WCD938X_FLYBACK_VNEG_CTRL_3             (0x30A7)
#define WCD938X_FLYBACK_VNEG_CTRL_4             (0x30A8)
#define WCD938X_FLYBACK_VNEG_CTRL_5             (0x30A9)
#define WCD938X_FLYBACK_VNEG_CTRL_6             (0x30AA)
#define WCD938X_FLYBACK_VNEG_CTRL_7             (0x30AB)
#define WCD938X_FLYBACK_VNEG_CTRL_8             (0x30AC)
#define WCD938X_FLYBACK_VNEG_CTRL_9             (0x30AD)
#define WCD938X_FLYBACK_VNEGDAC_CTRL_1          (0x30AE)
#define WCD938X_FLYBACK_VNEGDAC_CTRL_2          (0x30AF)
#define WCD938X_FLYBACK_VNEGDAC_CTRL_3          (0x30B0)
#define WCD938X_FLYBACK_CTRL_1                  (0x30B1)
#define WCD938X_FLYBACK_TEST_CTL                (0x30B2)
#define WCD938X_RX_AUX_SW_CTL                   (0x30B3)
#define WCD938X_RX_PA_AUX_IN_CONN               (0x30B4)
#define WCD938X_RX_TIMER_DIV                    (0x30B5)
#define WCD938X_RX_OCP_CTL                      (0x30B6)
#define WCD938X_RX_OCP_COUNT                    (0x30B7)
#define WCD938X_RX_BIAS_EAR_DAC                 (0x30B8)
#define WCD938X_RX_BIAS_EAR_AMP                 (0x30B9)
#define WCD938X_RX_BIAS_HPH_LDO                 (0x30BA)
#define WCD938X_RX_BIAS_HPH_PA                  (0x30BB)
#define WCD938X_RX_BIAS_HPH_RDACBUFF_CNP2       (0x30BC)
#define WCD938X_RX_BIAS_HPH_RDAC_LDO            (0x30BD)
#define WCD938X_RX_BIAS_HPH_CNP1                (0x30BE)
#define WCD938X_RX_BIAS_HPH_LOWPOWER            (0x30BF)
#define WCD938X_RX_BIAS_AUX_DAC                 (0x30C0)
#define WCD938X_RX_BIAS_AUX_AMP                 (0x30C1)
#define WCD938X_RX_BIAS_VNEGDAC_BLEEDER         (0x30C2)
#define WCD938X_RX_BIAS_MISC                    (0x30C3)
#define WCD938X_RX_BIAS_BUCK_RST                (0x30C4)
#define WCD938X_RX_BIAS_BUCK_VREF_ERRAMP        (0x30C5)
#define WCD938X_RX_BIAS_FLYB_ERRAMP             (0x30C6)
#define WCD938X_RX_BIAS_FLYB_BUFF               (0x30C7)
#define WCD938X_RX_BIAS_FLYB_MID_RST            (0x30C8)
#define WCD938X_HPH_L_STATUS                    (0x30C9)
#define WCD938X_HPH_R_STATUS                    (0x30CA)
#define WCD938X_HPH_CNP_EN                      (0x30CB)
#define WCD938X_HPH_CNP_WG_CTL                  (0x30CC)
#define WCD938X_HPH_CNP_WG_TIME                 (0x30CD)
#define WCD938X_HPH_OCP_CTL                     (0x30CE)
#define WCD938X_HPH_AUTO_CHOP                   (0x30CF)
#define WCD938X_HPH_CHOP_CTL                    (0x30D0)
#define WCD938X_HPH_PA_CTL1                     (0x30D1)
#define WCD938X_HPH_PA_CTL2                     (0x30D2)
#define WCD938X_HPHPA_GND_R_MASK		BIT(6)
#define WCD938X_HPHPA_GND_L_MASK		BIT(4)
#define WCD938X_HPH_L_EN                        (0x30D3)
#define WCD938X_HPH_L_TEST                      (0x30D4)
#define WCD938X_HPH_L_ATEST                     (0x30D5)
#define WCD938X_HPH_R_EN                        (0x30D6)
#define WCD938X_GAIN_SRC_SEL_MASK		BIT(5)
#define WCD938X_GAIN_SRC_SEL_REGISTER		1
#define WCD938X_HPH_R_TEST                      (0x30D7)
#define WCD938X_HPH_R_ATEST                     (0x30D8)
#define WCD938X_HPHPA_GND_OVR_MASK		BIT(1)
#define WCD938X_HPH_RDAC_CLK_CTL1               (0x30D9)
#define WCD938X_CHOP_CLK_EN_MASK		BIT(7)
#define WCD938X_HPH_RDAC_CLK_CTL2               (0x30DA)
#define WCD938X_HPH_RDAC_LDO_CTL                (0x30DB)
#define WCD938X_HPH_RDAC_CHOP_CLK_LP_CTL        (0x30DC)
#define WCD938X_HPH_REFBUFF_UHQA_CTL            (0x30DD)
#define WCD938X_HPH_REFBUFF_LP_CTL              (0x30DE)
#define WCD938X_PREREF_FLIT_BYPASS_MASK		BIT(0)
#define WCD938X_HPH_L_DAC_CTL                   (0x30DF)
#define WCD938X_HPH_R_DAC_CTL                   (0x30E0)
#define WCD938X_HPH_SURGE_HPHLR_SURGE_COMP_SEL  (0x30E1)
#define WCD938X_HPH_SURGE_HPHLR_SURGE_EN        (0x30E2)
#define WCD938X_HPH_SURGE_HPHLR_SURGE_MISC1     (0x30E3)
#define WCD938X_HPH_SURGE_HPHLR_SURGE_STATUS    (0x30E4)
#define WCD938X_EAR_EAR_EN_REG                  (0x30E9)
#define WCD938X_EAR_EAR_PA_CON                  (0x30EA)
#define WCD938X_EAR_EAR_SP_CON                  (0x30EB)
#define WCD938X_EAR_EAR_DAC_CON                 (0x30EC)
#define WCD938X_DAC_SAMPLE_EDGE_SEL_MASK	BIT(7)
#define WCD938X_EAR_EAR_CNP_FSM_CON             (0x30ED)
#define WCD938X_EAR_TEST_CTL                    (0x30EE)
#define WCD938X_EAR_STATUS_REG_1                (0x30EF)
#define WCD938X_EAR_STATUS_REG_2                (0x30F0)
#define WCD938X_ANA_NEW_PAGE_REGISTER           (0x3100)
#define WCD938X_HPH_NEW_ANA_HPH2                (0x3101)
#define WCD938X_HPH_NEW_ANA_HPH3                (0x3102)
#define WCD938X_SLEEP_CTL                       (0x3103)
#define WCD938X_SLEEP_WATCHDOG_CTL              (0x3104)
#define WCD938X_MBHC_NEW_ELECT_REM_CLAMP_CTL    (0x311F)
#define WCD938X_MBHC_NEW_CTL_1                  (0x3120)
#define WCD938X_MBHC_CTL_RCO_EN_MASK		BIT(7)
#define WCD938X_MBHC_CTL_RCO_EN			BIT(7)
#define WCD938X_MBHC_BTN_DBNC_MASK		GENMASK(1, 0)
#define WCD938X_MBHC_BTN_DBNC_T_16_MS		0x2
#define WCD938X_MBHC_NEW_CTL_2                  (0x3121)
#define WCD938X_M_RTH_CTL_MASK			GENMASK(3, 2)
#define WCD938X_MBHC_HS_VREF_CTL_MASK		GENMASK(1, 0)
#define WCD938X_MBHC_HS_VREF_1P5_V		0x1
#define WCD938X_MBHC_NEW_PLUG_DETECT_CTL        (0x3122)
#define WCD938X_MBHC_DBNC_TIMER_INSREM_DBNC_T_96_MS	0x6

#define WCD938X_MBHC_NEW_ZDET_ANA_CTL           (0x3123)
#define WCD938X_ZDET_RANGE_CTL_MASK		GENMASK(3, 0)
#define WCD938X_ZDET_MAXV_CTL_MASK		GENMASK(6, 4)
#define WCD938X_MBHC_NEW_ZDET_RAMP_CTL          (0x3124)
#define WCD938X_MBHC_NEW_FSM_STATUS             (0x3125)
#define WCD938X_MBHC_NEW_ADC_RESULT             (0x3126)
#define WCD938X_TX_NEW_AMIC_MUX_CFG             (0x3127)
#define WCD938X_AUX_AUXPA                       (0x3128)
#define WCD938X_AUXPA_CLK_EN_MASK		BIT(4)
#define WCD938X_LDORXTX_MODE                    (0x3129)
#define WCD938X_LDORXTX_CONFIG                  (0x312A)
#define WCD938X_DIE_CRACK_DIE_CRK_DET_EN        (0x312C)
#define WCD938X_DIE_CRACK_DIE_CRK_DET_OUT       (0x312D)
#define WCD938X_HPH_NEW_INT_RDAC_GAIN_CTL       (0x3132)
#define WCD938X_HPH_NEW_INT_RDAC_HD2_CTL_L      (0x3133)
#define WCD938X_HPH_NEW_INT_RDAC_VREF_CTL       (0x3134)
#define WCD938X_HPH_NEW_INT_RDAC_OVERRIDE_CTL   (0x3135)
#define WCD938X_HPH_NEW_INT_RDAC_HD2_CTL_R      (0x3136)
#define WCD938X_HPH_RES_DIV_MASK		GENMASK(4, 0)
#define WCD938X_HPH_NEW_INT_PA_MISC1            (0x3137)
#define WCD938X_HPH_NEW_INT_PA_MISC2            (0x3138)
#define WCD938X_HPH_NEW_INT_PA_RDAC_MISC        (0x3139)
#define WCD938X_HPH_NEW_INT_HPH_TIMER1          (0x313A)
#define WCD938X_AUTOCHOP_TIMER_EN		BIT(1)
#define WCD938X_HPH_NEW_INT_HPH_TIMER2          (0x313B)
#define WCD938X_HPH_NEW_INT_HPH_TIMER3          (0x313C)
#define WCD938X_HPH_NEW_INT_HPH_TIMER4          (0x313D)
#define WCD938X_HPH_NEW_INT_PA_RDAC_MISC2       (0x313E)
#define WCD938X_HPH_NEW_INT_PA_RDAC_MISC3       (0x313F)
#define WCD938X_HPH_NEW_INT_RDAC_HD2_CTL_L_NEW  (0x3140)
#define WCD938X_HPH_NEW_INT_RDAC_HD2_CTL_R_NEW  (0x3141)
#define WCD938X_RX_NEW_INT_HPH_RDAC_BIAS_LOHIFI (0x3145)
#define WCD938X_RX_NEW_INT_HPH_RDAC_BIAS_ULP    (0x3146)
#define WCD938X_RX_NEW_INT_HPH_RDAC_LDO_LP      (0x3147)
#define WCD938X_MBHC_NEW_INT_MOISTURE_DET_DC_CTRL  (0x31AF)
#define WCD938X_MBHC_NEW_INT_MOISTURE_DET_POLLING_CTRL (0x31B0)
#define WCD938X_MOISTURE_EN_POLLING_MASK	BIT(2)
#define WCD938X_MBHC_NEW_INT_MECH_DET_CURRENT   (0x31B1)
#define WCD938X_HSDET_PULLUP_C_MASK		GENMASK(4, 0)
#define WCD938X_MBHC_NEW_INT_SPARE_2            (0x31B2)
#define WCD938X_EAR_INT_NEW_EAR_CHOPPER_CON     (0x31B7)
#define WCD938X_EAR_INT_NEW_CNP_VCM_CON1        (0x31B8)
#define WCD938X_EAR_INT_NEW_CNP_VCM_CON2        (0x31B9)
#define WCD938X_EAR_INT_NEW_EAR_DYNAMIC_BIAS    (0x31BA)
#define WCD938X_AUX_INT_EN_REG                  (0x31BD)
#define WCD938X_AUX_INT_PA_CTRL                 (0x31BE)
#define WCD938X_AUX_INT_SP_CTRL                 (0x31BF)
#define WCD938X_AUX_INT_DAC_CTRL                (0x31C0)
#define WCD938X_AUX_INT_CLK_CTRL                (0x31C1)
#define WCD938X_AUX_INT_TEST_CTRL               (0x31C2)
#define WCD938X_AUX_INT_STATUS_REG              (0x31C3)
#define WCD938X_AUX_INT_MISC                    (0x31C4)
#define WCD938X_LDORXTX_INT_BIAS                (0x31C5)
#define WCD938X_LDORXTX_INT_STB_LOADS_DTEST     (0x31C6)
#define WCD938X_LDORXTX_INT_TEST0               (0x31C7)
#define WCD938X_LDORXTX_INT_STARTUP_TIMER       (0x31C8)
#define WCD938X_LDORXTX_INT_TEST1               (0x31C9)
#define WCD938X_LDORXTX_INT_STATUS              (0x31CA)
#define WCD938X_SLEEP_INT_WATCHDOG_CTL_1        (0x31D0)
#define WCD938X_SLEEP_INT_WATCHDOG_CTL_2        (0x31D1)
#define WCD938X_DIE_CRACK_INT_DIE_CRK_DET_INT1  (0x31D3)
#define WCD938X_DIE_CRACK_INT_DIE_CRK_DET_INT2  (0x31D4)
#define WCD938X_TX_COM_NEW_INT_TXFE_DIVSTOP_L2  (0x31D5)
#define WCD938X_TX_COM_NEW_INT_TXFE_DIVSTOP_L1  (0x31D6)
#define WCD938X_TX_COM_NEW_INT_TXFE_DIVSTOP_L0  (0x31D7)
#define WCD938X_TX_COM_NEW_INT_TXFE_DIVSTOP_ULP1P2M	(0x31D8)
#define WCD938X_TX_COM_NEW_INT_TXFE_DIVSTOP_ULP0P6M	(0x31D9)
#define WCD938X_TX_COM_NEW_INT_TXFE_ICTRL_STG1_L2L1	(0x31DA)
#define WCD938X_TX_COM_NEW_INT_TXFE_ICTRL_STG1_L0       (0x31DB)
#define WCD938X_TX_COM_NEW_INT_TXFE_ICTRL_STG1_ULP      (0x31DC)
#define WCD938X_TX_COM_NEW_INT_TXFE_ICTRL_STG2MAIN_L2L1 (0x31DD)
#define WCD938X_TX_COM_NEW_INT_TXFE_ICTRL_STG2MAIN_L0   (0x31DE)
#define WCD938X_TX_COM_NEW_INT_TXFE_ICTRL_STG2MAIN_ULP  (0x31DF)
#define WCD938X_TX_COM_NEW_INT_TXFE_ICTRL_STG2CASC_L2L1L0 (0x31E0)
#define WCD938X_TX_COM_NEW_INT_TXFE_ICTRL_STG2CASC_ULP	(0x31E1)
#define WCD938X_TX_COM_NEW_INT_TXADC_SCBIAS_L2L1	(0x31E2)
#define WCD938X_TX_COM_NEW_INT_TXADC_SCBIAS_L0ULP	(0x31E3)
#define WCD938X_TX_COM_NEW_INT_TXADC_INT_L2     (0x31E4)
#define WCD938X_TX_COM_NEW_INT_TXADC_INT_L1     (0x31E5)
#define WCD938X_TX_COM_NEW_INT_TXADC_INT_L0     (0x31E6)
#define WCD938X_TX_COM_NEW_INT_TXADC_INT_ULP    (0x31E7)
#define WCD938X_DIGITAL_PAGE_REGISTER           (0x3400)
#define WCD938X_DIGITAL_CHIP_ID0                (0x3401)
#define WCD938X_DIGITAL_CHIP_ID1                (0x3402)
#define WCD938X_DIGITAL_CHIP_ID2                (0x3403)
#define WCD938X_DIGITAL_CHIP_ID3                (0x3404)
#define WCD938X_DIGITAL_SWR_TX_CLK_RATE         (0x3405)
#define WCD938X_DIGITAL_CDC_RST_CTL             (0x3406)
#define WCD938X_DIGITAL_TOP_CLK_CFG             (0x3407)
#define WCD938X_DIGITAL_CDC_ANA_CLK_CTL         (0x3408)
#define WCD938X_ANA_RX_CLK_EN_MASK		BIT(0)
#define WCD938X_ANA_RX_DIV2_CLK_EN_MASK		BIT(1)
#define WCD938X_ANA_RX_DIV4_CLK_EN_MASK		BIT(2)
#define WCD938X_ANA_TX_CLK_EN_MASK		BIT(3)
#define WCD938X_ANA_TX_DIV2_CLK_EN_MASK		BIT(4)
#define WCD938X_ANA_TX_DIV4_CLK_EN_MASK		BIT(5)
#define WCD938X_DIGITAL_CDC_DIG_CLK_CTL         (0x3409)
#define WCD938X_TXD3_CLK_EN_MASK		BIT(7)
#define WCD938X_TXD2_CLK_EN_MASK		BIT(6)
#define WCD938X_TXD1_CLK_EN_MASK		BIT(5)
#define WCD938X_TXD0_CLK_EN_MASK		BIT(4)
#define WCD938X_TX_CLK_EN_MASK			GENMASK(7, 4)
#define WCD938X_RXD2_CLK_EN_MASK		BIT(2)
#define WCD938X_RXD1_CLK_EN_MASK		BIT(1)
#define WCD938X_RXD0_CLK_EN_MASK		BIT(0)
#define WCD938X_DIGITAL_SWR_RST_EN              (0x340A)
#define WCD938X_DIGITAL_CDC_PATH_MODE           (0x340B)
#define WCD938X_DIGITAL_CDC_RX_RST              (0x340C)
#define WCD938X_DIGITAL_CDC_RX0_CTL             (0x340D)
#define WCD938X_DEM_DITHER_ENABLE_MASK		BIT(6)
#define WCD938X_DIGITAL_CDC_RX1_CTL             (0x340E)
#define WCD938X_DIGITAL_CDC_RX2_CTL             (0x340F)
#define WCD938X_DIGITAL_CDC_TX_ANA_MODE_0_1     (0x3410)
#define WCD938X_TXD0_MODE_MASK			GENMASK(3, 0)
#define WCD938X_TXD1_MODE_MASK			GENMASK(7, 4)
#define WCD938X_DIGITAL_CDC_TX_ANA_MODE_2_3     (0x3411)
#define WCD938X_TXD2_MODE_MASK			GENMASK(3, 0)
#define WCD938X_TXD3_MODE_MASK			GENMASK(7, 4)
#define WCD938X_DIGITAL_CDC_COMP_CTL_0          (0x3414)
#define WCD938X_HPHR_COMP_EN_MASK		BIT(0)
#define WCD938X_HPHL_COMP_EN_MASK		BIT(1)
#define WCD938X_DIGITAL_CDC_ANA_TX_CLK_CTL      (0x3417)
#define WCD938X_TX_SC_CLK_EN_MASK		BIT(0)
#define WCD938X_DIGITAL_CDC_HPH_DSM_A1_0        (0x3418)
#define WCD938X_DIGITAL_CDC_HPH_DSM_A1_1        (0x3419)
#define WCD938X_DIGITAL_CDC_HPH_DSM_A2_0        (0x341A)
#define WCD938X_DIGITAL_CDC_HPH_DSM_A2_1        (0x341B)
#define WCD938X_DIGITAL_CDC_HPH_DSM_A3_0        (0x341C)
#define WCD938X_DIGITAL_CDC_HPH_DSM_A3_1        (0x341D)
#define WCD938X_DIGITAL_CDC_HPH_DSM_A4_0        (0x341E)
#define WCD938X_DIGITAL_CDC_HPH_DSM_A4_1        (0x341F)
#define WCD938X_DIGITAL_CDC_HPH_DSM_A5_0        (0x3420)
#define WCD938X_DIGITAL_CDC_HPH_DSM_A5_1        (0x3421)
#define WCD938X_DIGITAL_CDC_HPH_DSM_A6_0        (0x3422)
#define WCD938X_DIGITAL_CDC_HPH_DSM_A7_0        (0x3423)
#define WCD938X_DIGITAL_CDC_HPH_DSM_C_0         (0x3424)
#define WCD938X_DIGITAL_CDC_HPH_DSM_C_1         (0x3425)
#define WCD938X_DIGITAL_CDC_HPH_DSM_C_2         (0x3426)
#define WCD938X_DIGITAL_CDC_HPH_DSM_C_3         (0x3427)
#define WCD938X_DIGITAL_CDC_HPH_DSM_R1          (0x3428)
#define WCD938X_DIGITAL_CDC_HPH_DSM_R2          (0x3429)
#define WCD938X_DIGITAL_CDC_HPH_DSM_R3          (0x342A)
#define WCD938X_DIGITAL_CDC_HPH_DSM_R4          (0x342B)
#define WCD938X_DIGITAL_CDC_HPH_DSM_R5          (0x342C)
#define WCD938X_DIGITAL_CDC_HPH_DSM_R6          (0x342D)
#define WCD938X_DIGITAL_CDC_HPH_DSM_R7          (0x342E)
#define WCD938X_DIGITAL_CDC_AUX_DSM_A1_0        (0x342F)
#define WCD938X_DIGITAL_CDC_AUX_DSM_A1_1        (0x3430)
#define WCD938X_DIGITAL_CDC_AUX_DSM_A2_0        (0x3431)
#define WCD938X_DIGITAL_CDC_AUX_DSM_A2_1        (0x3432)
#define WCD938X_DIGITAL_CDC_AUX_DSM_A3_0        (0x3433)
#define WCD938X_DIGITAL_CDC_AUX_DSM_A3_1        (0x3434)
#define WCD938X_DIGITAL_CDC_AUX_DSM_A4_0        (0x3435)
#define WCD938X_DIGITAL_CDC_AUX_DSM_A4_1        (0x3436)
#define WCD938X_DIGITAL_CDC_AUX_DSM_A5_0        (0x3437)
#define WCD938X_DIGITAL_CDC_AUX_DSM_A5_1        (0x3438)
#define WCD938X_DIGITAL_CDC_AUX_DSM_A6_0        (0x3439)
#define WCD938X_DIGITAL_CDC_AUX_DSM_A7_0        (0x343A)
#define WCD938X_DIGITAL_CDC_AUX_DSM_C_0         (0x343B)
#define WCD938X_DIGITAL_CDC_AUX_DSM_C_1         (0x343C)
#define WCD938X_DIGITAL_CDC_AUX_DSM_C_2         (0x343D)
#define WCD938X_DIGITAL_CDC_AUX_DSM_C_3         (0x343E)
#define WCD938X_DIGITAL_CDC_AUX_DSM_R1          (0x343F)
#define WCD938X_DIGITAL_CDC_AUX_DSM_R2          (0x3440)
#define WCD938X_DIGITAL_CDC_AUX_DSM_R3          (0x3441)
#define WCD938X_DIGITAL_CDC_AUX_DSM_R4          (0x3442)
#define WCD938X_DIGITAL_CDC_AUX_DSM_R5          (0x3443)
#define WCD938X_DIGITAL_CDC_AUX_DSM_R6          (0x3444)
#define WCD938X_DIGITAL_CDC_AUX_DSM_R7          (0x3445)
#define WCD938X_DIGITAL_CDC_HPH_GAIN_RX_0       (0x3446)
#define WCD938X_DIGITAL_CDC_HPH_GAIN_RX_1       (0x3447)
#define WCD938X_DIGITAL_CDC_HPH_GAIN_DSD_0      (0x3448)
#define WCD938X_DIGITAL_CDC_HPH_GAIN_DSD_1      (0x3449)
#define WCD938X_DIGITAL_CDC_HPH_GAIN_DSD_2      (0x344A)
#define WCD938X_DIGITAL_CDC_AUX_GAIN_DSD_0      (0x344B)
#define WCD938X_DIGITAL_CDC_AUX_GAIN_DSD_1      (0x344C)
#define WCD938X_DIGITAL_CDC_AUX_GAIN_DSD_2      (0x344D)
#define WCD938X_DIGITAL_CDC_HPH_GAIN_CTL        (0x344E)
#define WCD938X_HPHL_RX_EN_MASK			BIT(2)
#define WCD938X_HPHR_RX_EN_MASK			BIT(3)
#define WCD938X_DIGITAL_CDC_AUX_GAIN_CTL        (0x344F)
#define WCD938X_AUX_EN_MASK			BIT(0)
#define WCD938X_DIGITAL_CDC_EAR_PATH_CTL        (0x3450)
#define WCD938X_DIGITAL_CDC_SWR_CLH             (0x3451)
#define WCD938X_DIGITAL_SWR_CLH_BYP             (0x3452)
#define WCD938X_DIGITAL_CDC_TX0_CTL             (0x3453)
#define WCD938X_DIGITAL_CDC_TX1_CTL             (0x3454)
#define WCD938X_DIGITAL_CDC_TX2_CTL             (0x3455)
#define WCD938X_DIGITAL_CDC_TX_RST              (0x3456)
#define WCD938X_DIGITAL_CDC_REQ_CTL             (0x3457)
#define WCD938X_FS_RATE_4P8_MASK		BIT(1)
#define WCD938X_NO_NOTCH_MASK			BIT(0)
#define WCD938X_DIGITAL_CDC_RST                 (0x3458)
#define WCD938X_DIGITAL_CDC_AMIC_CTL            (0x345A)
#define WCD938X_AMIC1_IN_SEL_DMIC		0
#define WCD938X_AMIC1_IN_SEL_AMIC		0
#define WCD938X_AMIC1_IN_SEL_MASK		BIT(0)
#define WCD938X_AMIC3_IN_SEL_MASK		BIT(1)
#define WCD938X_AMIC4_IN_SEL_MASK		BIT(2)
#define WCD938X_AMIC5_IN_SEL_MASK		BIT(3)
#define WCD938X_DIGITAL_CDC_DMIC_CTL            (0x345B)
#define WCD938X_DMIC_CLK_SCALING_EN_MASK	GENMASK(2, 1)
#define WCD938X_DIGITAL_CDC_DMIC1_CTL           (0x345C)
#define WCD938X_DMIC_CLK_EN_MASK		BIT(3)
#define WCD938X_DIGITAL_CDC_DMIC2_CTL           (0x345D)
#define WCD938X_DIGITAL_CDC_DMIC3_CTL           (0x345E)
#define WCD938X_DIGITAL_CDC_DMIC4_CTL           (0x345F)
#define WCD938X_DIGITAL_EFUSE_PRG_CTL           (0x3460)
#define WCD938X_DIGITAL_EFUSE_CTL               (0x3461)
#define WCD938X_DIGITAL_CDC_DMIC_RATE_1_2       (0x3462)
#define WCD938X_DIGITAL_CDC_DMIC_RATE_3_4       (0x3463)
#define WCD938X_DMIC1_RATE_MASK			GENMASK(3, 0)
#define WCD938X_DMIC2_RATE_MASK			GENMASK(7, 4)
#define WCD938X_DMIC3_RATE_MASK			GENMASK(3, 0)
#define WCD938X_DMIC4_RATE_MASK			GENMASK(7, 4)
#define WCD938X_DMIC4_RATE_2P4MHZ		3

#define WCD938X_DIGITAL_PDM_WD_CTL0             (0x3465)
#define WCD938X_PDM_WD_EN_MASK			GENMASK(2, 0)
#define WCD938X_DIGITAL_PDM_WD_CTL1             (0x3466)
#define WCD938X_DIGITAL_PDM_WD_CTL2             (0x3467)
#define WCD938X_AUX_PDM_WD_EN_MASK			GENMASK(2, 0)
#define WCD938X_DIGITAL_INTR_MODE               (0x346A)
#define WCD938X_DIGITAL_INTR_MASK_0             (0x346B)
#define WCD938X_DIGITAL_INTR_MASK_1             (0x346C)
#define WCD938X_DIGITAL_INTR_MASK_2             (0x346D)
#define WCD938X_DIGITAL_INTR_STATUS_0           (0x346E)
#define WCD938X_DIGITAL_INTR_STATUS_1           (0x346F)
#define WCD938X_DIGITAL_INTR_STATUS_2           (0x3470)
#define WCD938X_DIGITAL_INTR_CLEAR_0            (0x3471)
#define WCD938X_DIGITAL_INTR_CLEAR_1            (0x3472)
#define WCD938X_DIGITAL_INTR_CLEAR_2            (0x3473)
#define WCD938X_DIGITAL_INTR_LEVEL_0            (0x3474)
#define WCD938X_DIGITAL_INTR_LEVEL_1            (0x3475)
#define WCD938X_DIGITAL_INTR_LEVEL_2            (0x3476)
#define WCD938X_DIGITAL_INTR_SET_0              (0x3477)
#define WCD938X_DIGITAL_INTR_SET_1              (0x3478)
#define WCD938X_DIGITAL_INTR_SET_2              (0x3479)
#define WCD938X_DIGITAL_INTR_TEST_0             (0x347A)
#define WCD938X_DIGITAL_INTR_TEST_1             (0x347B)
#define WCD938X_DIGITAL_INTR_TEST_2             (0x347C)
#define WCD938X_DIGITAL_TX_MODE_DBG_EN          (0x347F)
#define WCD938X_DIGITAL_TX_MODE_DBG_0_1         (0x3480)
#define WCD938X_DIGITAL_TX_MODE_DBG_2_3         (0x3481)
#define WCD938X_DIGITAL_LB_IN_SEL_CTL           (0x3482)
#define WCD938X_DIGITAL_LOOP_BACK_MODE          (0x3483)
#define WCD938X_DIGITAL_SWR_DAC_TEST            (0x3484)
#define WCD938X_DIGITAL_SWR_HM_TEST_RX_0        (0x3485)
#define WCD938X_DIGITAL_SWR_HM_TEST_TX_0        (0x3486)
#define WCD938X_DIGITAL_SWR_HM_TEST_RX_1        (0x3487)
#define WCD938X_DIGITAL_SWR_HM_TEST_TX_1        (0x3488)
#define WCD938X_DIGITAL_SWR_HM_TEST_TX_2        (0x3489)
#define WCD938X_DIGITAL_SWR_HM_TEST_0           (0x348A)
#define WCD938X_DIGITAL_SWR_HM_TEST_1           (0x348B)
#define WCD938X_DIGITAL_PAD_CTL_SWR_0           (0x348C)
#define WCD938X_DIGITAL_PAD_CTL_SWR_1           (0x348D)
#define WCD938X_DIGITAL_I2C_CTL                 (0x348E)
#define WCD938X_DIGITAL_CDC_TX_TANGGU_SW_MODE   (0x348F)
#define WCD938X_DIGITAL_EFUSE_TEST_CTL_0        (0x3490)
#define WCD938X_DIGITAL_EFUSE_TEST_CTL_1        (0x3491)
#define WCD938X_DIGITAL_EFUSE_T_DATA_0          (0x3492)
#define WCD938X_DIGITAL_EFUSE_T_DATA_1          (0x3493)
#define WCD938X_DIGITAL_PAD_CTL_PDM_RX0         (0x3494)
#define WCD938X_DIGITAL_PAD_CTL_PDM_RX1         (0x3495)
#define WCD938X_DIGITAL_PAD_CTL_PDM_TX0         (0x3496)
#define WCD938X_DIGITAL_PAD_CTL_PDM_TX1         (0x3497)
#define WCD938X_DIGITAL_PAD_CTL_PDM_TX2         (0x3498)
#define WCD938X_DIGITAL_PAD_INP_DIS_0           (0x3499)
#define WCD938X_DIGITAL_PAD_INP_DIS_1           (0x349A)
#define WCD938X_DIGITAL_DRIVE_STRENGTH_0        (0x349B)
#define WCD938X_DIGITAL_DRIVE_STRENGTH_1        (0x349C)
#define WCD938X_DIGITAL_DRIVE_STRENGTH_2        (0x349D)
#define WCD938X_DIGITAL_RX_DATA_EDGE_CTL        (0x349E)
#define WCD938X_DIGITAL_TX_DATA_EDGE_CTL        (0x349F)
#define WCD938X_DIGITAL_GPIO_MODE               (0x34A0)
#define WCD938X_DIGITAL_PIN_CTL_OE              (0x34A1)
#define WCD938X_DIGITAL_PIN_CTL_DATA_0          (0x34A2)
#define WCD938X_DIGITAL_PIN_CTL_DATA_1          (0x34A3)
#define WCD938X_DIGITAL_PIN_STATUS_0            (0x34A4)
#define WCD938X_DIGITAL_PIN_STATUS_1            (0x34A5)
#define WCD938X_DIGITAL_DIG_DEBUG_CTL           (0x34A6)
#define WCD938X_DIGITAL_DIG_DEBUG_EN            (0x34A7)
#define WCD938X_DIGITAL_ANA_CSR_DBG_ADD         (0x34A8)
#define WCD938X_DIGITAL_ANA_CSR_DBG_CTL         (0x34A9)
#define WCD938X_DIGITAL_SSP_DBG                 (0x34AA)
#define WCD938X_DIGITAL_MODE_STATUS_0           (0x34AB)
#define WCD938X_DIGITAL_MODE_STATUS_1           (0x34AC)
#define WCD938X_DIGITAL_SPARE_0                 (0x34AD)
#define WCD938X_DIGITAL_SPARE_1                 (0x34AE)
#define WCD938X_DIGITAL_SPARE_2                 (0x34AF)
#define WCD938X_DIGITAL_EFUSE_REG_0             (0x34B0)
#define WCD938X_ID_MASK				GENMASK(4, 1)
#define WCD938X_DIGITAL_EFUSE_REG_1             (0x34B1)
#define WCD938X_DIGITAL_EFUSE_REG_2             (0x34B2)
#define WCD938X_DIGITAL_EFUSE_REG_3             (0x34B3)
#define WCD938X_DIGITAL_EFUSE_REG_4             (0x34B4)
#define WCD938X_DIGITAL_EFUSE_REG_5             (0x34B5)
#define WCD938X_DIGITAL_EFUSE_REG_6             (0x34B6)
#define WCD938X_DIGITAL_EFUSE_REG_7             (0x34B7)
#define WCD938X_DIGITAL_EFUSE_REG_8             (0x34B8)
#define WCD938X_DIGITAL_EFUSE_REG_9             (0x34B9)
#define WCD938X_DIGITAL_EFUSE_REG_10            (0x34BA)
#define WCD938X_DIGITAL_EFUSE_REG_11            (0x34BB)
#define WCD938X_DIGITAL_EFUSE_REG_12            (0x34BC)
#define WCD938X_DIGITAL_EFUSE_REG_13            (0x34BD)
#define WCD938X_DIGITAL_EFUSE_REG_14            (0x34BE)
#define WCD938X_DIGITAL_EFUSE_REG_15            (0x34BF)
#define WCD938X_DIGITAL_EFUSE_REG_16            (0x34C0)
#define WCD938X_DIGITAL_EFUSE_REG_17            (0x34C1)
#define WCD938X_DIGITAL_EFUSE_REG_18            (0x34C2)
#define WCD938X_DIGITAL_EFUSE_REG_19            (0x34C3)
#define WCD938X_DIGITAL_EFUSE_REG_20            (0x34C4)
#define WCD938X_DIGITAL_EFUSE_REG_21            (0x34C5)
#define WCD938X_DIGITAL_EFUSE_REG_22            (0x34C6)
#define WCD938X_DIGITAL_EFUSE_REG_23            (0x34C7)
#define WCD938X_DIGITAL_EFUSE_REG_24            (0x34C8)
#define WCD938X_DIGITAL_EFUSE_REG_25            (0x34C9)
#define WCD938X_DIGITAL_EFUSE_REG_26            (0x34CA)
#define WCD938X_DIGITAL_EFUSE_REG_27            (0x34CB)
#define WCD938X_DIGITAL_EFUSE_REG_28            (0x34CC)
#define WCD938X_DIGITAL_EFUSE_REG_29            (0x34CD)
#define WCD938X_DIGITAL_EFUSE_REG_30            (0x34CE)
#define WCD938X_DIGITAL_EFUSE_REG_31            (0x34CF)
#define WCD938X_DIGITAL_TX_REQ_FB_CTL_0         (0x34D0)
#define WCD938X_DIGITAL_TX_REQ_FB_CTL_1         (0x34D1)
#define WCD938X_DIGITAL_TX_REQ_FB_CTL_2         (0x34D2)
#define WCD938X_DIGITAL_TX_REQ_FB_CTL_3         (0x34D3)
#define WCD938X_DIGITAL_TX_REQ_FB_CTL_4         (0x34D4)
#define WCD938X_DIGITAL_DEM_BYPASS_DATA0        (0x34D5)
#define WCD938X_DIGITAL_DEM_BYPASS_DATA1        (0x34D6)
#define WCD938X_DIGITAL_DEM_BYPASS_DATA2        (0x34D7)
#define WCD938X_DIGITAL_DEM_BYPASS_DATA3        (0x34D8)
#define WCD938X_MAX_REGISTER			(WCD938X_DIGITAL_DEM_BYPASS_DATA3)

#define WCD938X_MAX_SWR_PORTS	5
#define WCD938X_MAX_TX_SWR_PORTS 4
#define WCD938X_MAX_SWR_CH_IDS	15

struct wcd938x_sdw_ch_info {
	int port_num;
	unsigned int ch_mask;
};

#define WCD_SDW_CH(id, pn, cmask)	\
	[id] = {			\
		.port_num = pn,		\
		.ch_mask = cmask,	\
	}

enum wcd938x_tx_sdw_ports {
	WCD938X_ADC_1_2_PORT = 1,
	WCD938X_ADC_3_4_PORT,
	/* DMIC0_0, DMIC0_1, DMIC1_0, DMIC1_1 */
	WCD938X_DMIC_0_3_MBHC_PORT,
	WCD938X_DMIC_4_7_PORT,
};

enum wcd938x_tx_sdw_channels {
	WCD938X_ADC1,
	WCD938X_ADC2,
	WCD938X_ADC3,
	WCD938X_ADC4,
	WCD938X_DMIC0,
	WCD938X_DMIC1,
	WCD938X_MBHC,
	WCD938X_DMIC2,
	WCD938X_DMIC3,
	WCD938X_DMIC4,
	WCD938X_DMIC5,
	WCD938X_DMIC6,
	WCD938X_DMIC7,
};

enum wcd938x_rx_sdw_ports {
	WCD938X_HPH_PORT = 1,
	WCD938X_CLSH_PORT,
	WCD938X_COMP_PORT,
	WCD938X_LO_PORT,
	WCD938X_DSD_PORT,
};

enum wcd938x_rx_sdw_channels {
	WCD938X_HPH_L,
	WCD938X_HPH_R,
	WCD938X_CLSH,
	WCD938X_COMP_L,
	WCD938X_COMP_R,
	WCD938X_LO,
	WCD938X_DSD_R,
	WCD938X_DSD_L,
};
enum {
	WCD938X_SDW_DIR_RX,
	WCD938X_SDW_DIR_TX,
};

struct wcd938x_priv;
struct wcd938x_sdw_priv {
	struct sdw_slave *sdev;
	struct sdw_stream_config sconfig;
	struct sdw_stream_runtime *sruntime;
	struct sdw_port_config port_config[WCD938X_MAX_SWR_PORTS];
	struct wcd938x_sdw_ch_info *ch_info;
	bool port_enable[WCD938X_MAX_SWR_CH_IDS];
	int active_ports;
	int num_ports;
	bool is_tx;
	struct wcd938x_priv *wcd938x;
	struct irq_domain *slave_irq;
	struct regmap *regmap;
};

#if IS_ENABLED(CONFIG_SND_SOC_WCD938X_SDW)
int wcd938x_sdw_free(struct wcd938x_sdw_priv *wcd,
		     struct snd_pcm_substream *substream,
		     struct snd_soc_dai *dai);
int wcd938x_sdw_set_sdw_stream(struct wcd938x_sdw_priv *wcd,
			       struct snd_soc_dai *dai,
			       void *stream, int direction);
int wcd938x_sdw_hw_params(struct wcd938x_sdw_priv *wcd,
			  struct snd_pcm_substream *substream,
			  struct snd_pcm_hw_params *params,
			  struct snd_soc_dai *dai);

struct device *wcd938x_sdw_device_get(struct device_node *np);
int wcd938x_swr_get_current_bank(struct sdw_slave *sdev);

#else

static inline int wcd938x_sdw_free(struct wcd938x_sdw_priv *wcd,
		     struct snd_pcm_substream *substream,
		     struct snd_soc_dai *dai)
{
	return -EOPNOTSUPP;
}

static inline int wcd938x_sdw_set_sdw_stream(struct wcd938x_sdw_priv *wcd,
			       struct snd_soc_dai *dai,
			       void *stream, int direction)
{
	return -EOPNOTSUPP;
}

static inline int wcd938x_sdw_hw_params(struct wcd938x_sdw_priv *wcd,
			  struct snd_pcm_substream *substream,
			  struct snd_pcm_hw_params *params,
			  struct snd_soc_dai *dai)
{
	return -EOPNOTSUPP;
}

static inline struct device *wcd938x_sdw_device_get(struct device_node *np)
{
	return NULL;
}

static inline int wcd938x_swr_get_current_bank(struct sdw_slave *sdev)
{
	return 0;
}
#endif /* CONFIG_SND_SOC_WCD938X_SDW */
#endif /* __WCD938X_H__ */
