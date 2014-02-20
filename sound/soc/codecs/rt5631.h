#ifndef __RTCODEC5631_H__
#define __RTCODEC5631_H__


#define RT5631_RESET				0x00
#define RT5631_SPK_OUT_VOL			0x02
#define RT5631_HP_OUT_VOL			0x04
#define RT5631_MONO_AXO_1_2_VOL		0x06
#define RT5631_AUX_IN_VOL			0x0A
#define RT5631_STEREO_DAC_VOL_1		0x0C
#define RT5631_MIC_CTRL_1			0x0E
#define RT5631_STEREO_DAC_VOL_2		0x10
#define RT5631_ADC_CTRL_1			0x12
#define RT5631_ADC_REC_MIXER			0x14
#define RT5631_ADC_CTRL_2			0x16
#define RT5631_OUTMIXER_L_CTRL			0x1A
#define RT5631_OUTMIXER_R_CTRL			0x1C
#define RT5631_AXO1MIXER_CTRL			0x1E
#define RT5631_AXO2MIXER_CTRL			0x20
#define RT5631_MIC_CTRL_2			0x22
#define RT5631_DIG_MIC_CTRL			0x24
#define RT5631_MONO_INPUT_VOL			0x26
#define RT5631_SPK_MIXER_CTRL			0x28
#define RT5631_SPK_MONO_OUT_CTRL		0x2A
#define RT5631_SPK_MONO_HP_OUT_CTRL		0x2C
#define RT5631_SDP_CTRL				0x34
#define RT5631_STEREO_AD_DA_CLK_CTRL		0x38
#define RT5631_PWR_MANAG_ADD1		0x3A
#define RT5631_PWR_MANAG_ADD2		0x3B
#define RT5631_PWR_MANAG_ADD3		0x3C
#define RT5631_PWR_MANAG_ADD4		0x3E
#define RT5631_GEN_PUR_CTRL_REG		0x40
#define RT5631_GLOBAL_CLK_CTRL			0x42
#define RT5631_PLL_CTRL				0x44
#define RT5631_INT_ST_IRQ_CTRL_1		0x48
#define RT5631_INT_ST_IRQ_CTRL_2		0x4A
#define RT5631_GPIO_CTRL			0x4C
#define RT5631_MISC_CTRL			0x52
#define RT5631_DEPOP_FUN_CTRL_1		0x54
#define RT5631_DEPOP_FUN_CTRL_2		0x56
#define RT5631_JACK_DET_CTRL			0x5A
#define RT5631_SOFT_VOL_CTRL			0x5C
#define RT5631_ALC_CTRL_1			0x64
#define RT5631_ALC_CTRL_2			0x65
#define RT5631_ALC_CTRL_3			0x66
#define RT5631_PSEUDO_SPATL_CTRL		0x68
#define RT5631_INDEX_ADD			0x6A
#define RT5631_INDEX_DATA			0x6C
#define RT5631_EQ_CTRL				0x6E
#define RT5631_VENDOR_ID1			0x7C
#define RT5631_VENDOR_ID2			0x7E

/* Index of Codec Private Register definition */
#define RT5631_EQ_BW_LOP			0x00
#define RT5631_EQ_GAIN_LOP			0x01
#define RT5631_EQ_FC_BP1			0x02
#define RT5631_EQ_BW_BP1			0x03
#define RT5631_EQ_GAIN_BP1			0x04
#define RT5631_EQ_FC_BP2			0x05
#define RT5631_EQ_BW_BP2			0x06
#define RT5631_EQ_GAIN_BP2			0x07
#define RT5631_EQ_FC_BP3			0x08
#define RT5631_EQ_BW_BP3			0x09
#define RT5631_EQ_GAIN_BP3			0x0a
#define RT5631_EQ_BW_HIP			0x0b
#define RT5631_EQ_GAIN_HIP			0x0c
#define RT5631_EQ_HPF_A1			0x0d
#define RT5631_EQ_HPF_A2			0x0e
#define RT5631_EQ_HPF_GAIN			0x0f
#define RT5631_EQ_PRE_VOL_CTRL			0x11
#define RT5631_EQ_POST_VOL_CTRL		0x12
#define RT5631_TEST_MODE_CTRL			0x39
#define RT5631_CP_INTL_REG2			0x45
#define RT5631_ADDA_MIXER_INTL_REG3		0x52
#define RT5631_SPK_INTL_CTRL			0x56


/* global definition */
#define RT_L_MUTE				(0x1 << 15)
#define RT_R_MUTE				(0x1 << 7)

/* Speaker Output Control(0x02) */
#define SPK_L_VOL_SEL_MASK			(0x1 << 14)
#define SPK_L_VOL_SEL_VMID			(0x0 << 14)
#define SPK_L_VOL_SEL_SPKMIX_L			(0x1 << 14)
#define SPK_R_VOL_SEL_MASK			(0x1 << 6)
#define SPK_R_VOL_SEL_VMID			(0x0 << 6)
#define SPK_R_VOL_SEL_SPKMIX_R			(0x1 << 6)

/* Headphone Output Control(0x04) */
#define HP_L_VOL_SEL_MASK			(0x1 << 14)
#define HP_L_VOL_SEL_VMID			(0x0 << 14)
#define HP_L_VOL_SEL_OUTMIX_L			(0x1 << 14)
#define HP_R_VOL_SEL_MASK			(0x1 << 6)
#define HP_R_VOL_SEL_VMID			(0x0 << 6)
#define HP_R_VOL_SEL_OUTMIX_R			(0x1 << 6)

/* Output Control for AUXOUT/MONO(0x06) */
#define AUXOUT_1_VOL_SEL_MASK			(0x1 << 14)
#define AUXOUT_1_VOL_SEL_VMID			(0x0 << 14)
#define AUXOUT_1_VOL_SEL_OUTMIX_L		(0x1 << 14)
#define MUTE_MONO				(0x1 << 13)
#define AUXOUT_2_VOL_SEL_MASK			(0x1 << 6)
#define AUXOUT_2_VOL_SEL_VMID			(0x0 << 6)
#define AUXOUT_2_VOL_SEL_OUTMIX_R		(0x1 << 6)

/* Microphone Input Control 1(0x0E) */
#define MIC1_DIFF_INPUT_CTRL			(0x1 << 15)
#define MIC2_DIFF_INPUT_CTRL			(0x1 << 7)

/* ADC Recording Mixer Control(0x14) */
#define M_OUTMIXER_L_TO_RECMIXER_L		(0x1 << 15)
#define M_MIC1_TO_RECMIXER_L			(0x1 << 14)
#define M_AXIL_TO_RECMIXER_L			(0x1 << 13)
#define M_MONO_IN_TO_RECMIXER_L		(0x1 << 12)
#define M_OUTMIXER_R_TO_RECMIXER_R		(0x1 << 7)
#define M_MIC2_TO_RECMIXER_R			(0x1 << 6)
#define M_AXIR_TO_RECMIXER_R			(0x1 << 5)
#define M_MONO_IN_TO_RECMIXER_R		(0x1 << 4)

/* Left Output Mixer Control(0x1A) */
#define M_RECMIXER_L_TO_OUTMIXER_L		(0x1 << 15)
#define M_RECMIXER_R_TO_OUTMIXER_L		(0x1 << 14)
#define M_DAC_L_TO_OUTMIXER_L			(0x1 << 13)
#define M_MIC1_TO_OUTMIXER_L			(0x1 << 12)
#define M_MIC2_TO_OUTMIXER_L			(0x1 << 11)
#define M_MONO_IN_P_TO_OUTMIXER_L		(0x1 << 10)
#define M_AXIL_TO_OUTMIXER_L			(0x1 << 9)
#define M_AXIR_TO_OUTMIXER_L			(0x1 << 8)

/* Right Output Mixer Control(0x1C) */
#define M_RECMIXER_L_TO_OUTMIXER_R		(0x1 << 15)
#define M_RECMIXER_R_TO_OUTMIXER_R		(0x1 << 14)
#define M_DAC_R_TO_OUTMIXER_R			(0x1 << 13)
#define M_MIC1_TO_OUTMIXER_R			(0x1 << 12)
#define M_MIC2_TO_OUTMIXER_R			(0x1 << 11)
#define M_MONO_IN_N_TO_OUTMIXER_R		(0x1 << 10)
#define M_AXIL_TO_OUTMIXER_R			(0x1 << 9)
#define M_AXIR_TO_OUTMIXER_R			(0x1 << 8)

/* Lout Mixer Control(0x1E) */
#define M_MIC1_TO_AXO1MIXER			(0x1 << 15)
#define M_MIC2_TO_AXO1MIXER			(0x1 << 11)
#define M_OUTMIXER_L_TO_AXO1MIXER		(0x1 << 7)
#define M_OUTMIXER_R_TO_AXO1MIXER		(0x1 << 6)

/* Rout Mixer Control(0x20) */
#define M_MIC1_TO_AXO2MIXER			(0x1 << 15)
#define M_MIC2_TO_AXO2MIXER			(0x1 << 11)
#define M_OUTMIXER_L_TO_AXO2MIXER		(0x1 << 7)
#define M_OUTMIXER_R_TO_AXO2MIXER		(0x1 << 6)

/* Micphone Input Control 2(0x22) */
#define MIC_BIAS_90_PRECNET_AVDD 1
#define MIC_BIAS_75_PRECNET_AVDD 2

#define MIC1_BOOST_CTRL_MASK			(0xf << 12)
#define MIC1_BOOST_CTRL_BYPASS		(0x0 << 12)
#define MIC1_BOOST_CTRL_20DB			(0x1 << 12)
#define MIC1_BOOST_CTRL_24DB			(0x2 << 12)
#define MIC1_BOOST_CTRL_30DB			(0x3 << 12)
#define MIC1_BOOST_CTRL_35DB			(0x4 << 12)
#define MIC1_BOOST_CTRL_40DB			(0x5 << 12)
#define MIC1_BOOST_CTRL_34DB			(0x6 << 12)
#define MIC1_BOOST_CTRL_50DB			(0x7 << 12)
#define MIC1_BOOST_CTRL_52DB			(0x8 << 12)

#define MIC2_BOOST_CTRL_MASK			(0xf << 8)
#define MIC2_BOOST_CTRL_BYPASS		(0x0 << 8)
#define MIC2_BOOST_CTRL_20DB			(0x1 << 8)
#define MIC2_BOOST_CTRL_24DB			(0x2 << 8)
#define MIC2_BOOST_CTRL_30DB			(0x3 << 8)
#define MIC2_BOOST_CTRL_35DB			(0x4 << 8)
#define MIC2_BOOST_CTRL_40DB			(0x5 << 8)
#define MIC2_BOOST_CTRL_34DB			(0x6 << 8)
#define MIC2_BOOST_CTRL_50DB			(0x7 << 8)
#define MIC2_BOOST_CTRL_52DB			(0x8 << 8)

#define MICBIAS1_VOLT_CTRL_MASK		(0x1 << 7)
#define MICBIAS1_VOLT_CTRL_90P			(0x0 << 7)
#define MICBIAS1_VOLT_CTRL_75P			(0x1 << 7)

#define MICBIAS1_S_C_DET_MASK			(0x1 << 6)
#define MICBIAS1_S_C_DET_DIS			(0x0 << 6)
#define MICBIAS1_S_C_DET_ENA			(0x1 << 6)

#define MICBIAS1_SHORT_CURR_DET_MASK		(0x3 << 4)
#define MICBIAS1_SHORT_CURR_DET_600UA	(0x0 << 4)
#define MICBIAS1_SHORT_CURR_DET_1500UA	(0x1 << 4)
#define MICBIAS1_SHORT_CURR_DET_2000UA	(0x2 << 4)

#define MICBIAS2_VOLT_CTRL_MASK		(0x1 << 3)
#define MICBIAS2_VOLT_CTRL_90P			(0x0 << 3)
#define MICBIAS2_VOLT_CTRL_75P			(0x1 << 3)

#define MICBIAS2_S_C_DET_MASK			(0x1 << 2)
#define MICBIAS2_S_C_DET_DIS			(0x0 << 2)
#define MICBIAS2_S_C_DET_ENA			(0x1 << 2)

#define MICBIAS2_SHORT_CURR_DET_MASK		(0x3)
#define MICBIAS2_SHORT_CURR_DET_600UA	(0x0)
#define MICBIAS2_SHORT_CURR_DET_1500UA	(0x1)
#define MICBIAS2_SHORT_CURR_DET_2000UA	(0x2)


/* Digital Microphone Control(0x24) */
#define DMIC_ENA_MASK				(0x1 << 15)
/* DMIC_ENA: DMIC to ADC Digital filter */
#define DMIC_ENA				(0x1 << 15)
/* DMIC_DIS: ADC mixer to ADC Digital filter */
#define DMIC_DIS					(0x0 << 15)

#define DMIC_L_CH_MUTE_MASK			(0x1 << 13)
#define DMIC_L_CH_UNMUTE			(0x0 << 13)
#define DMIC_L_CH_MUTE				(0x1 << 13)

#define DMIC_R_CH_MUTE_MASK			(0x1 << 12)
#define DMIC_R_CH_UNMUTE			(0x0 << 12)
#define DMIC_R_CH_MUTE				(0x1 << 12)

#define DMIC_L_CH_LATCH_MASK			(0x1 << 9)
#define DMIC_L_CH_LATCH_RISING			(0x1 << 9)
#define DMIC_L_CH_LATCH_FALLING		(0x0 << 9)

#define DMIC_R_CH_LATCH_MASK			(0x1 << 8)
#define DMIC_R_CH_LATCH_RISING		(0x1 << 8)
#define DMIC_R_CH_LATCH_FALLING		(0x0 << 8)

#define DMIC_CLK_CTRL_MASK			(0x3 << 4)
#define DMIC_CLK_CTRL_TO_128FS			(0x0 << 4)
#define DMIC_CLK_CTRL_TO_64FS			(0x1 << 4)
#define DMIC_CLK_CTRL_TO_32FS			(0x2 << 4)

/* Speaker Mixer Control(0x28) */
#define M_RECMIXER_L_TO_SPKMIXER_L		(0x1 << 15)
#define M_MIC1_P_TO_SPKMIXER_L		(0x1 << 14)
#define M_DAC_L_TO_SPKMIXER_L			(0x1 << 13)
#define M_OUTMIXER_L_TO_SPKMIXER_L		(0x1 << 12)

#define M_RECMIXER_R_TO_SPKMIXER_R		(0x1 << 7)
#define M_MIC2_P_TO_SPKMIXER_R		(0x1 << 6)
#define M_DAC_R_TO_SPKMIXER_R			(0x1 << 5)
#define M_OUTMIXER_R_TO_SPKMIXER_R		(0x1 << 4)

/* Speaker/Mono Output Control(0x2A) */
#define M_SPKVOL_L_TO_SPOL_MIXER		(0x1 << 15)
#define M_SPKVOL_R_TO_SPOL_MIXER		(0x1 << 14)
#define M_SPKVOL_L_TO_SPOR_MIXER		(0x1 << 13)
#define M_SPKVOL_R_TO_SPOR_MIXER		(0x1 << 12)
#define M_OUTVOL_L_TO_MONOMIXER		(0x1 << 11)
#define M_OUTVOL_R_TO_MONOMIXER		(0x1 << 10)

/* Speaker/Mono/HP Output Control(0x2C) */
#define SPK_L_MUX_SEL_MASK			(0x3 << 14)
#define SPK_L_MUX_SEL_SPKMIXER_L		(0x0 << 14)
#define SPK_L_MUX_SEL_MONO_IN			(0x1 << 14)
#define SPK_L_MUX_SEL_DAC_L			(0x3 << 14)

#define SPK_R_MUX_SEL_MASK			(0x3 << 10)
#define SPK_R_MUX_SEL_SPKMIXER_R		(0x0 << 10)
#define SPK_R_MUX_SEL_MONO_IN			(0x1 << 10)
#define SPK_R_MUX_SEL_DAC_R			(0x3 << 10)

#define MONO_MUX_SEL_MASK			(0x3 << 6)
#define MONO_MUX_SEL_MONOMIXER		(0x0 << 6)
#define MONO_MUX_SEL_MONO_IN			(0x1 << 6)

#define HP_L_MUX_SEL_MASK			(0x1 << 3)
#define HP_L_MUX_SEL_HPVOL_L			(0x0 << 3)
#define HP_L_MUX_SEL_DAC_L			(0x1 << 3)

#define HP_R_MUX_SEL_MASK			(0x1 << 2)
#define HP_R_MUX_SEL_HPVOL_R			(0x0 << 2)
#define HP_R_MUX_SEL_DAC_R			(0x1 << 2)

/* Stereo I2S Serial Data Port Control(0x34) */
#define SDP_MODE_SEL_MASK			(0x1 << 15)
#define SDP_MODE_SEL_MASTER			(0x0 << 15)
#define SDP_MODE_SEL_SLAVE			(0x1 << 15)

#define SDP_ADC_CPS_SEL_MASK			(0x3 << 10)
#define SDP_ADC_CPS_SEL_OFF			(0x0 << 10)
#define SDP_ADC_CPS_SEL_U_LAW			(0x1 << 10)
#define SDP_ADC_CPS_SEL_A_LAW			(0x2 << 10)

#define SDP_DAC_CPS_SEL_MASK			(0x3 << 8)
#define SDP_DAC_CPS_SEL_OFF			(0x0 << 8)
#define SDP_DAC_CPS_SEL_U_LAW			(0x1 << 8)
#define SDP_DAC_CPS_SEL_A_LAW			(0x2 << 8)
/* 0:Normal 1:Invert */
#define SDP_I2S_BCLK_POL_CTRL			(0x1 << 7)
/* 0:Normal 1:Invert */
#define SDP_DAC_R_INV				(0x1 << 6)
/* 0:ADC data appear at left phase of LRCK
 * 1:ADC data appear at right phase of LRCK
 */
#define SDP_ADC_DATA_L_R_SWAP			(0x1 << 5)
/* 0:DAC data appear at left phase of LRCK
 * 1:DAC data appear at right phase of LRCK
 */
#define SDP_DAC_DATA_L_R_SWAP			(0x1 << 4)

/* Data Length Slection */
#define SDP_I2S_DL_MASK			(0x3 << 2)
#define SDP_I2S_DL_16				(0x0 << 2)
#define SDP_I2S_DL_20				(0x1 << 2)
#define SDP_I2S_DL_24				(0x2 << 2)
#define SDP_I2S_DL_8				(0x3 << 2)

/* PCM Data Format Selection */
#define SDP_I2S_DF_MASK			(0x3)
#define SDP_I2S_DF_I2S				(0x0)
#define SDP_I2S_DF_LEFT				(0x1)
#define SDP_I2S_DF_PCM_A			(0x2)
#define SDP_I2S_DF_PCM_B			(0x3)

/* Stereo AD/DA Clock Control(0x38h) */
#define I2S_PRE_DIV_MASK			(0x7 << 13)
#define I2S_PRE_DIV_1				(0x0 << 13)
#define I2S_PRE_DIV_2				(0x1 << 13)
#define I2S_PRE_DIV_4				(0x2 << 13)
#define I2S_PRE_DIV_8				(0x3 << 13)
#define I2S_PRE_DIV_16				(0x4 << 13)
#define I2S_PRE_DIV_32				(0x5 << 13)
/* CLOCK RELATIVE OF BCLK AND LCRK */
#define I2S_LRCK_SEL_N_BCLK_MASK		(0x1 << 12)
#define I2S_LRCK_SEL_64_BCLK			(0x0 << 12) /* 64FS */
#define I2S_LRCK_SEL_32_BCLK			(0x1 << 12) /* 32FS */

#define DAC_OSR_SEL_MASK			(0x3 << 10)
#define DAC_OSR_SEL_128FS			(0x3 << 10)
#define DAC_OSR_SEL_64FS			(0x3 << 10)
#define DAC_OSR_SEL_32FS			(0x3 << 10)
#define DAC_OSR_SEL_16FS			(0x3 << 10)

#define ADC_OSR_SEL_MASK			(0x3 << 8)
#define ADC_OSR_SEL_128FS			(0x3 << 8)
#define ADC_OSR_SEL_64FS			(0x3 << 8)
#define ADC_OSR_SEL_32FS			(0x3 << 8)
#define ADC_OSR_SEL_16FS			(0x3 << 8)

#define ADDA_FILTER_CLK_SEL_256FS		(0 << 7) /* 256FS */
#define ADDA_FILTER_CLK_SEL_384FS		(1 << 7) /* 384FS */

/* Power managment addition 1 (0x3A) */
#define PWR_MAIN_I2S_EN			(0x1 << 15)
#define PWR_CLASS_D				(0x1 << 12)
#define PWR_ADC_L_CLK				(0x1 << 11)
#define PWR_ADC_R_CLK				(0x1 << 10)
#define PWR_DAC_L_CLK				(0x1 << 9)
#define PWR_DAC_R_CLK				(0x1 << 8)
#define PWR_DAC_REF				(0x1 << 7)
#define PWR_DAC_L_TO_MIXER			(0x1 << 6)
#define PWR_DAC_R_TO_MIXER			(0x1 << 5)

/* Power managment addition 2 (0x3B) */
#define PWR_OUTMIXER_L				(0x1 << 15)
#define PWR_OUTMIXER_R				(0x1 << 14)
#define PWR_SPKMIXER_L				(0x1 << 13)
#define PWR_SPKMIXER_R				(0x1 << 12)
#define PWR_RECMIXER_L				(0x1 << 11)
#define PWR_RECMIXER_R				(0x1 << 10)
#define PWR_MIC1_BOOT_GAIN			(0x1 << 5)
#define PWR_MIC2_BOOT_GAIN			(0x1 << 4)
#define PWR_MICBIAS1_VOL			(0x1 << 3)
#define PWR_MICBIAS2_VOL			(0x1 << 2)
#define PWR_PLL					(0x1 << 1)

/* Power managment addition 3(0x3C) */
#define PWR_VREF				(0x1 << 15)
#define PWR_FAST_VREF_CTRL			(0x1 << 14)
#define PWR_MAIN_BIAS				(0x1 << 13)
#define PWR_AXO1MIXER				(0x1 << 11)
#define PWR_AXO2MIXER				(0x1 << 10)
#define PWR_MONOMIXER				(0x1 << 9)
#define PWR_MONO_DEPOP_DIS			(0x1 << 8)
#define PWR_MONO_AMP_EN			(0x1 << 7)
#define PWR_CHARGE_PUMP			(0x1 << 4)
#define PWR_HP_L_AMP				(0x1 << 3)
#define PWR_HP_R_AMP				(0x1 << 2)
#define PWR_HP_DEPOP_DIS			(0x1 << 1)
#define PWR_HP_AMP_DRIVING			(0x1)

/* Power managment addition 4(0x3E) */
#define PWR_SPK_L_VOL				(0x1 << 15)
#define PWR_SPK_R_VOL				(0x1 << 14)
#define PWR_LOUT_VOL				(0x1 << 13)
#define PWR_ROUT_VOL				(0x1 << 12)
#define PWR_HP_L_OUT_VOL			(0x1 << 11)
#define PWR_HP_R_OUT_VOL			(0x1 << 10)
#define PWR_AXIL_IN_VOL			(0x1 << 9)
#define PWR_AXIR_IN_VOL			(0x1 << 8)
#define PWR_MONO_IN_P_VOL			(0x1 << 7)
#define PWR_MONO_IN_N_VOL			(0x1 << 6)

/* General Purpose Control Register(0x40) */
#define SPK_AMP_AUTO_RATIO_EN			(0x1 << 15)

#define SPK_AMP_RATIO_CTRL_MASK		(0x7 << 12)
#define SPK_AMP_RATIO_CTRL_2_34		(0x0 << 12) /* 7.40DB */
#define SPK_AMP_RATIO_CTRL_1_99		(0x1 << 12) /* 5.99DB */
#define SPK_AMP_RATIO_CTRL_1_68		(0x2 << 12) /* 4.50DB */
#define SPK_AMP_RATIO_CTRL_1_56		(0x3 << 12) /* 3.86DB */
#define SPK_AMP_RATIO_CTRL_1_44		(0x4 << 12) /* 3.16DB */
#define SPK_AMP_RATIO_CTRL_1_27		(0x5 << 12) /* 2.10DB */
#define SPK_AMP_RATIO_CTRL_1_09		(0x6 << 12) /* 0.80DB */
#define SPK_AMP_RATIO_CTRL_1_00		(0x7 << 12) /* 0.00DB */

#define STEREO_DAC_HI_PASS_FILT_EN		(0x1 << 11)
#define STEREO_ADC_HI_PASS_FILT_EN		(0x1 << 10)
/* Select ADC Wind Filter Clock type */
#define ADC_WIND_FILT_MASK			(0x3 << 4)
#define ADC_WIND_FILT_8_16_32K			(0x0 << 4) /* 8/16/32k */
#define ADC_WIND_FILT_11_22_44K		(0x1 << 4) /* 11/22/44k */
#define ADC_WIND_FILT_12_24_48K		(0x2 << 4) /* 12/24/48k */
#define ADC_WIND_FILT_EN			(0x1 << 3)
/* SelectADC Wind Filter Corner Frequency */
#define ADC_WIND_CNR_FREQ_MASK		(0x7 << 0)
#define ADC_WIND_CNR_FREQ_82_113_122		(0x0 << 0) /* 82/113/122 Hz */
#define ADC_WIND_CNR_FREQ_102_141_153	(0x1 << 0) /* 102/141/153 Hz */
#define ADC_WIND_CNR_FREQ_131_180_156	(0x2 << 0) /* 131/180/156 Hz */
#define ADC_WIND_CNR_FREQ_163_225_245	(0x3 << 0) /* 163/225/245 Hz */
#define ADC_WIND_CNR_FREQ_204_281_306	(0x4 << 0) /* 204/281/306 Hz */
#define ADC_WIND_CNR_FREQ_261_360_392	(0x5 << 0) /* 261/360/392 Hz */
#define ADC_WIND_CNR_FREQ_327_450_490	(0x6 << 0) /* 327/450/490 Hz */
#define ADC_WIND_CNR_FREQ_408_563_612	(0x7 << 0) /* 408/563/612 Hz */

/* Global Clock Control Register(0x42) */
#define SYSCLK_SOUR_SEL_MASK			(0x1 << 14)
#define SYSCLK_SOUR_SEL_MCLK			(0x0 << 14)
#define SYSCLK_SOUR_SEL_PLL			(0x1 << 14)
#define SYSCLK_SOUR_SEL_PLL_TCK		(0x2 << 14)

#define PLLCLK_SOUR_SEL_MCLK			(0x0 << 12)
#define PLLCLK_SOUR_SEL_BITCLK			(0x1 << 12)

#define PLLCLK_PRE_DIV1				(0x0 << 11)
#define PLLCLK_PRE_DIV2				(0x1 << 11)

/* PLL Control(0x44) */
#define PLL_CTRL_M_VAL(m)			((m)&0xf)
#define PLL_CTRL_K_VAL(k)			(((k)&0x7) << 4)
#define PLL_CTRL_N_VAL(n)			(((n)&0xff) << 8)

/* GPIO Pin Configuration(0x4C) */
#define GPIO_PIN_FUN_SEL_MASK			(0x1 << 15)
#define GPIO_PIN_FUN_SEL_IRQ			(0x1 << 15)
#define GPIO_PIN_FUN_SEL_GPIO_DIMC		(0x0 << 15)

#define GPIO_DMIC_FUN_SEL_MASK		(0x1 << 3)
#define GPIO_DMIC_FUN_SEL_DIMC		(0x1 << 3) /* GPIO pin SELECT DMIC */
#define GPIO_DMIC_FUN_SEL_GPIO		(0x0 << 3) /* GPIO PIN SELECT GPIO */

#define GPIO_PIN_CON_MASK			(0x1 << 2)
#define GPIO_PIN_SET_INPUT			(0x0 << 2)
#define GPIO_PIN_SET_OUTPUT			(0x1 << 2)

/* De-POP function Control 1(0x54) */
#define POW_ON_SOFT_GEN			(0x1 << 15)
#define EN_MUTE_UNMUTE_DEPOP			(0x1 << 14)
#define EN_DEPOP2_FOR_HP			(0x1 << 7)
/* Power Down HPAMP_L Starts Up Signal */
#define PD_HPAMP_L_ST_UP			(0x1 << 5)
/* Power Down HPAMP_R Starts Up Signal */
#define PD_HPAMP_R_ST_UP			(0x1 << 4)
/* Enable left HP mute/unmute depop */
#define EN_HP_L_M_UN_MUTE_DEPOP		(0x1 << 1)
/* Enable right HP mute/unmute depop */
#define EN_HP_R_M_UN_MUTE_DEPOP		(0x1 << 0)

/* De-POP Fnction Control(0x56) */
#define EN_ONE_BIT_DEPOP			(0x1 << 15)
#define EN_CAP_FREE_DEPOP			(0x1 << 14)

/* Jack Detect Control Register(0x5A) */
#define JD_USE_MASK				(0x3 << 14)
#define JD_USE_JD2				(0x3 << 14)
#define JD_USE_JD1				(0x2 << 14)
#define JD_USE_GPIO				(0x1 << 14)
#define JD_OFF					(0x0 << 14)
/* JD trigger enable for HP */
#define JD_HP_EN				(0x1 << 11)
#define JD_HP_TRI_MASK				(0x1 << 10)
#define JD_HP_TRI_HI				(0x1 << 10)
#define JD_HP_TRI_LO				(0x1 << 10)
/* JD trigger enable for speaker LP/LN */
#define JD_SPK_L_EN				(0x1 << 9)
#define JD_SPK_L_TRI_MASK			(0x1 << 8)
#define JD_SPK_L_TRI_HI				(0x1 << 8)
#define JD_SPK_L_TRI_LO				(0x0 << 8)
/* JD trigger enable for speaker RP/RN */
#define JD_SPK_R_EN				(0x1 << 7)
#define JD_SPK_R_TRI_MASK			(0x1 << 6)
#define JD_SPK_R_TRI_HI				(0x1 << 6)
#define JD_SPK_R_TRI_LO				(0x0 << 6)
/* JD trigger enable for monoout */
#define JD_MONO_EN				(0x1 << 5)
#define JD_MONO_TRI_MASK			(0x1 << 4)
#define JD_MONO_TRI_HI				(0x1 << 4)
#define JD_MONO_TRI_LO				(0x0 << 4)
/* JD trigger enable for Lout */
#define JD_AUX_1_EN				(0x1 << 3)
#define JD_AUX_1_MASK				(0x1 << 2)
#define JD_AUX_1_TRI_HI				(0x1 << 2)
#define JD_AUX_1_TRI_LO				(0x0 << 2)
/* JD trigger enable for Rout */
#define JD_AUX_2_EN				(0x1 << 1)
#define JD_AUX_2_MASK				(0x1 << 0)
#define JD_AUX_2_TRI_HI				(0x1 << 0)
#define JD_AUX_2_TRI_LO				(0x0 << 0)

/* ALC CONTROL 1(0x64) */
#define ALC_ATTACK_RATE_MASK			(0x1F << 8)
#define ALC_RECOVERY_RATE_MASK		(0x1F << 0)

/* ALC CONTROL 2(0x65) */
/* select Compensation gain for Noise gate function */
#define ALC_COM_NOISE_GATE_MASK		(0xF << 0)

/* ALC CONTROL 3(0x66) */
#define ALC_FUN_MASK				(0x3 << 14)
#define ALC_FUN_DIS				(0x0 << 14)
#define ALC_ENA_DAC_PATH			(0x1 << 14)
#define ALC_ENA_ADC_PATH			(0x3 << 14)
#define ALC_PARA_UPDATE			(0x1 << 13)
#define ALC_LIMIT_LEVEL_MASK			(0x1F << 8)
#define ALC_NOISE_GATE_FUN_MASK		(0x1 << 7)
#define ALC_NOISE_GATE_FUN_DIS			(0x0 << 7)
#define ALC_NOISE_GATE_FUN_ENA		(0x1 << 7)
/* ALC noise gate hold data function */
#define ALC_NOISE_GATE_H_D_MASK		(0x1 << 6)
#define ALC_NOISE_GATE_H_D_DIS		(0x0 << 6)
#define ALC_NOISE_GATE_H_D_ENA		(0x1 << 6)

/* Psedueo Stereo & Spatial Effect Block Control(0x68) */
#define SPATIAL_CTRL_EN				(0x1 << 15)
#define ALL_PASS_FILTER_EN			(0x1 << 14)
#define PSEUDO_STEREO_EN			(0x1 << 13)
#define STEREO_EXPENSION_EN			(0x1 << 12)
/* 3D gain parameter */
#define GAIN_3D_PARA_MASK			(0x3 << 6)
#define GAIN_3D_PARA_1_00			(0x0 << 6) /* 3D gain 1.0 */
#define GAIN_3D_PARA_1_50			(0x1 << 6) /* 3D gain 1.5 */
#define GAIN_3D_PARA_2_00			(0x2 << 6) /* 3D gain 2.0 */
/* 3D ratio parameter */
#define RATIO_3D_MASK				(0x3 << 4)
#define RATIO_3D_0_0				(0x0 << 4) /* 3D ratio 0.0 */
#define RATIO_3D_0_66				(0x1 << 4) /* 3D ratio 0.66 */
#define RATIO_3D_1_0				(0x2 << 4) /* 3D ratio 1.0 */
/* select samplerate for all pass filter */
#define APF_FUN_SLE_MASK			(0x3 << 0)
#define APF_FUN_SEL_48K			(0x3 << 0)
#define APF_FUN_SEL_44_1K			(0x2 << 0)
#define APF_FUN_SEL_32K			(0x1 << 0)
#define APF_FUN_DIS				(0x0 << 0)

/* EQ CONTROL 1(0x6E) */
#define HW_EQ_PATH_SEL_MASK			(0x1 << 15)
#define HW_EQ_PATH_SEL_DAC			(0x0 << 15)
#define HW_EQ_PATH_SEL_ADC			(0x1 << 15)
#define HW_EQ_UPDATE_CTRL			(0x1 << 14)

#define EN_HW_EQ_HPF2				(0x1 << 5)
#define EN_HW_EQ_HPF1				(0x1 << 4)
#define EN_HW_EQ_BP3				(0x1 << 3)
#define EN_HW_EQ_BP2				(0x1 << 2)
#define EN_HW_EQ_BP1				(0x1 << 1)
#define EN_HW_EQ_LPF				(0x1 << 0)

#endif /* __RTCODEC5631_H__ */
