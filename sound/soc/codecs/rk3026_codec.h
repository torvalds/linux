/*
 * rk3026.h  --  RK3026 CODEC ALSA SoC audio driver
 *
 * Copyright 2013 Rockship
 * Author: chenjq <chenjq@rock-chips.com>
 *
 */

#ifndef __RK3026_CODEC_H__
#define __RK3026_CODEC_H__



/* codec register */
#define RK3026_CODEC_BASE			(0x0)

#define RK3026_RESET				(RK3026_CODEC_BASE + 0x00)
#define RK3026_ADC_INT_CTL1			(RK3026_CODEC_BASE + 0x08)
#define RK3026_ADC_INT_CTL2			(RK3026_CODEC_BASE + 0x0c)
#define RK3026_DAC_INT_CTL1			(RK3026_CODEC_BASE + 0x10)
#define RK3026_DAC_INT_CTL2			(RK3026_CODEC_BASE + 0x14)
#define RK3026_DAC_INT_CTL3			(RK3026_CODEC_BASE + 0x18)
#define RK3026_ADC_MIC_CTL			(RK3026_CODEC_BASE + 0x88)
#define RK3026_BST_CTL				(RK3026_CODEC_BASE + 0x8c)
#define RK3026_ALC_MUNIN_CTL			(RK3026_CODEC_BASE + 0x90)
#define RK3026_BSTL_ALCL_CTL			(RK3026_CODEC_BASE + 0x94)
#define RK3026_ALCR_GAIN_CTL			(RK3026_CODEC_BASE + 0x98)
#define RK3026_ADC_ENABLE			(RK3026_CODEC_BASE + 0x9c)
#define RK3026_DAC_CTL				(RK3026_CODEC_BASE + 0xa0)
#define RK3026_DAC_ENABLE			(RK3026_CODEC_BASE + 0xa4)
#define RK3026_HPMIX_CTL			(RK3026_CODEC_BASE + 0xa8)
#define RK3026_HPMIX_S_SELECT			(RK3026_CODEC_BASE + 0xac)
#define RK3026_HPOUT_CTL			(RK3026_CODEC_BASE + 0xB0)
#define RK3026_HPOUTL_GAIN			(RK3026_CODEC_BASE + 0xB4)
#define RK3026_HPOUTR_GAIN			(RK3026_CODEC_BASE + 0xB8)
#define RK3026_SELECT_CURRENT			(RK3026_CODEC_BASE + 0xBC)
#define RK3026_PGAL_AGC_CTL1			(RK3026_CODEC_BASE + 0x100)
#define RK3026_PGAL_AGC_CTL2			(RK3026_CODEC_BASE + 0x104)
#define RK3026_PGAL_AGC_CTL3			(RK3026_CODEC_BASE + 0x108)
#define RK3026_PGAL_AGC_CTL4			(RK3026_CODEC_BASE + 0x10c)
#define RK3026_PGAL_ASR_CTL			(RK3026_CODEC_BASE + 0x110)
#define RK3026_PGAL_AGC_MAX_H			(RK3026_CODEC_BASE + 0x114)
#define RK3026_PGAL_AGC_MAX_L			(RK3026_CODEC_BASE + 0x118)
#define RK3026_PGAL_AGC_MIN_H			(RK3026_CODEC_BASE + 0x11c)
#define RK3026_PGAL_AGC_MIN_L			(RK3026_CODEC_BASE + 0x120)
#define RK3026_PGAL_AGC_CTL5			(RK3026_CODEC_BASE + 0x124)
#define RK3026_PGAR_AGC_CTL1			(RK3026_CODEC_BASE + 0x140)
#define RK3026_PGAR_AGC_CTL2			(RK3026_CODEC_BASE + 0x144)
#define RK3026_PGAR_AGC_CTL3			(RK3026_CODEC_BASE + 0x148)
#define RK3026_PGAR_AGC_CTL4			(RK3026_CODEC_BASE + 0x14c)
#define RK3026_PGAR_ASR_CTL			(RK3026_CODEC_BASE + 0x150)
#define RK3026_PGAR_AGC_MAX_H			(RK3026_CODEC_BASE + 0x154)
#define RK3026_PGAR_AGC_MAX_L			(RK3026_CODEC_BASE + 0x158)
#define RK3026_PGAR_AGC_MIN_H			(RK3026_CODEC_BASE + 0x15c)
#define RK3026_PGAR_AGC_MIN_L			(RK3026_CODEC_BASE + 0x160)
#define RK3026_PGAR_AGC_CTL5			(RK3026_CODEC_BASE + 0x164)

/* ADC Interface Control 1 (0x08) */
#define RK3026_ALRCK_POL_MASK			(0x1 << 7)
#define RK3026_ALRCK_POL_SFT			7
#define RK3026_ALRCK_POL_EN			(0x1 << 7)
#define RK3026_ALRCK_POL_DIS			(0x0 << 7)

#define RK3026_ADC_VWL_MASK			(0x3 << 5)
#define RK3026_ADC_VWL_SFT			5
#define RK3026_ADC_VWL_32			(0x3 << 5)
#define RK3026_ADC_VWL_24			(0x2 << 5)
#define RK3026_ADC_VWL_20			(0x1 << 5)
#define RK3026_ADC_VWL_16			(0x0 << 5)

#define RK3026_ADC_DF_MASK			(0x3 << 3)
#define RK3026_ADC_DF_SFT			3
#define RK3026_ADC_DF_PCM			(0x3 << 3)
#define RK3026_ADC_DF_I2S			(0x2 << 3)
#define RK3026_ADC_DF_LJ				(0x1 << 3)
#define RK3026_ADC_DF_RJ				(0x0 << 3)

#define RK3026_ADC_SWAP_MASK			(0x1 << 1)
#define RK3026_ADC_SWAP_SFT			1
#define RK3026_ADC_SWAP_EN			(0x1 << 1)
#define RK3026_ADC_SWAP_DIS			(0x0 << 1)

#define RK3026_ADC_TYPE_MASK			0x1
#define RK3026_ADC_TYPE_SFT			0
#define RK3026_ADC_TYPE_MONO			0x1
#define RK3026_ADC_TYPE_STEREO			0x0

/* ADC Interface Control 2 (0x0c) */
#define RK3026_I2S_MODE_MASK			(0x1 << 4)
#define RK3026_I2S_MODE_SFT			(4)
#define RK3026_I2S_MODE_MST			(0x1 << 4)
#define RK3026_I2S_MODE_SLV			(0x0 << 4)

#define RK3026_ADC_WL_MASK			(0x3 << 2)
#define RK3026_ADC_WL_SFT			(2)
#define RK3026_ADC_WL_32				(0x3 << 2)
#define RK3026_ADC_WL_24				(0x2 << 2)
#define RK3026_ADC_WL_20				(0x1 << 2)
#define RK3026_ADC_WL_16				(0x0 << 2)

#define RK3026_ADC_RST_MASK			(0x1 << 1)
#define RK3026_ADC_RST_SFT			91)
#define RK3026_ADC_RST_DIS			(0x1 << 1)
#define RK3026_ADC_RST_EN			(0x0 << 1)

#define RK3026_ABCLK_POL_MASK			0x1
#define RK3026_ABCLK_POL_SFT			0
#define RK3026_ABCLK_POL_EN			0x1
#define RK3026_ABCLK_POL_DIS			0x0

/* DAC Interface Control 1 (0x10) */
#define RK3026_DLRCK_POL_MASK			(0x1 << 7)
#define RK3026_DLRCK_POL_SFT			7
#define RK3026_DLRCK_POL_EN			(0x1 << 7)
#define RK3026_DLRCK_POL_DIS			(0x0 << 7)

#define RK3026_DAC_VWL_MASK			(0x3 << 5)
#define RK3026_DAC_VWL_SFT			5
#define RK3026_DAC_VWL_32			(0x3 << 5)
#define RK3026_DAC_VWL_24			(0x2 << 5)
#define RK3026_DAC_VWL_20			(0x1 << 5)
#define RK3026_DAC_VWL_16			(0x0 << 5)

#define RK3026_DAC_DF_MASK			(0x3 << 3)
#define RK3026_DAC_DF_SFT			3
#define RK3026_DAC_DF_PCM			(0x3 << 3)
#define RK3026_DAC_DF_I2S			(0x2 << 3)
#define RK3026_DAC_DF_LJ				(0x1 << 3)
#define RK3026_DAC_DF_RJ				(0x0 << 3)

#define RK3026_DAC_SWAP_MASK			(0x1 << 2)
#define RK3026_DAC_SWAP_SFT			2
#define RK3026_DAC_SWAP_EN			(0x1 << 2)
#define RK3026_DAC_SWAP_DIS			(0x0 << 2)

/* DAC Interface Control 2 (0x14) */
#define RK3026_DAC_WL_MASK			(0x3 << 2)
#define RK3026_DAC_WL_SFT			2
#define RK3026_DAC_WL_32				(0x3 << 2)
#define RK3026_DAC_WL_24				(0x2 << 2)
#define RK3026_DAC_WL_20				(0x1 << 2)
#define RK3026_DAC_WL_16				(0x0 << 2)

#define RK3026_DAC_RST_MASK			(0x1 << 1)
#define RK3026_DAC_RST_SFT			1
#define RK3026_DAC_RST_DIS			(0x1 << 1)
#define RK3026_DAC_RST_EN			(0x0 << 1)

#define RK3026_DBCLK_POL_MASK			0x1
#define RK3026_DBCLK_POL_SFT			0
#define RK3026_DBCLK_POL_EN			0x1
#define RK3026_DBCLK_POL_DIS			0x0

/* ADC & MICBIAS (0x88) */
#define  RK3026_ADC_CURRENT_ENABLE              (0x1 << 7)
#define  RK3026_ADC_CURRENT_DISABLE             (0x0 << 7)

#define  RK3026_MICBIAS_VOL_ENABLE              (6)

#define  RK3026_ADCL_ZERO_DET_EN_SFT                (5)
#define  RK3026_ADCL_ZERO_DET_EN                (0x1 << 5)
#define  RK3026_ADCL_ZERO_DET_DIS               (0x0 << 5)

#define  RK3026_ADCR_ZERO_DET_EN_SFT                (4)
#define  RK3026_ADCR_ZERO_DET_EN                (0x1 << 4)
#define  RK3026_ADCR_ZERO_DET_DIS               (0x0 << 4)

#define  RK3026_MICBIAS_VOL_SHT                  0
#define  RK3026_MICBIAS_VOL_MSK                  7
#define  RK3026_MICBIAS_VOL_MIN                  (0x0 << 0)  
#define  RK3026_MICBIAS_VOL_MAX                  (0x7 << 0)

/* BST_L  BST_R  CONTROL (0x8C)  */
#define  RK3026_BSTL_PWRD_SFT		    (6)
#define  RK3026_BSTL_EN                     (0x1 << 6)
#define  RK3026_BSTL_DIS                    (0x0 << 6)  
#define  RK3026_BSTL_GAIN_SHT               (5)
#define  RK3026_BSTL_GAIN_20                (0x1 << 5)
#define  RK3026_BSTL_GAIN_0                 (0x0 << 5)
#define  RK3026_BSTL_MUTE_SHT	             (4)

#define  RK3026_BSTR_PWRD_SFT		    (2)
#define  RK3026_BSTR_EN                     (0x1 << 2)
#define  RK3026_BSTR_DIS                    (0x0 << 2)
#define  RK3026_BSTR_GAIN_SHT               (1)
#define  RK3026_BSTR_GAIN_20                (0x1 << 1)
#define  RK3026_BSTR_GAIN_0                 (0x0 << 1)
#define  RK3026_BSTR_MUTE_SHT	               (0)


/* MUXINL ALCL MUXINR ALCR  (0x90)  */
#define  RK3026_MUXINL_F_SHT		   (6)
#define  RK3026_MUXINL_F_MSK		   (0x03 << 6)
#define  RK3026_MUXINL_F_INL                (0x02 << 6)
#define  RK3026_MUXINL_F_BSTL               (0x01 << 6)
#define  RK3026_ALCL_PWR_SHT                     (5)
#define  RK3026_ALCL_EN                     (0x1 << 5)
#define  RK3026_ALCL_DIS                    (0x0 << 5)
#define  RK3026_ALCL_MUTE_SHT                (4)
#define  RK3026_MUXINR_F_SHT		   (2)
#define  RK3026_MUXINR_F_MSK		   (0x03 << 2)
#define  RK3026_MUXINR_F_INR                (0x02 << 2)
#define  RK3026_MUXINR_F_BSTR               (0x01 << 2)
#define  RK3026_ALCR_PWR_SHT                     (1)
#define  RK3026_ALCR_EN                     (0x1 << 1)
#define  RK3026_ALCR_DIS                    (0x0 << 1)
#define  RK3026_ALCR_MUTE_SHT                (0)

/* BST_L MODE & ALC_L GAIN (0x94) */
#define  RK3026_BSTL_MODE_SFT          (5)
#define  RK3026_BSTL_MODE_SINGLE        (0x1 << 5)
#define  RK3026_BSTL_MODE_DIFF          (0x0 << 5)

#define  RK3026_ALCL_GAIN_SHT               (0)
#define  RK3026_ALCL_GAIN_MSK               (0x1f)

/* ALC_R GAIN (0x98) */
#define  RK3026_ALCR_GAIN_SHT               (0)
#define  RK3026_ALCR_GAIN_MSK               (0x1f)

/* ADC control (0x9C) */
#define RK3026_ADCL_REF_VOL_EN_SFT			(3)
#define RK3026_ADCL_REF_VOL_EN			(0x1 << 7)
#define RK3026_ADCL_REF_VOL_DIS			(0x0 << 7)

#define RK3026_ADCL_CLK_EN_SFT		       (6)
#define RK3026_ADCL_CLK_EN		       (0x1 << 6)
#define RK3026_ADCL_CLK_DIS		       (0x0 << 6)

#define RK3026_ADCL_AMP_EN_SFT			(5)
#define RK3026_ADCL_AMP_EN			(0x1 << 5)
#define RK3026_ADCL_AMP_DIS			(0x0 << 5)

#define  RK3026_ADCL_RST_EN                     (0x1 << 4)
#define  RK3026_ADCL_RST_DIS                     (0x0 << 4)

#define RK3026_ADCR_REF_VOL_EN_SFT			(3)
#define RK3026_ADCR_REF_VOL_EN			(0x1 << 3)
#define RK3026_ADCR_REF_VOL_DIS			(0x0 << 3)

#define RK3026_ADCR_CLK_EN_SFT		       (2)
#define RK3026_ADCR_CLK_EN		       (0x1 << 2)
#define RK3026_ADCR_CLK_DIS		       (0x0 << 2)

#define RK3026_ADCR_AMP_EN_SFT			(1)
#define RK3026_ADCR_AMP_EN			(0x1 << 1)
#define RK3026_ADCR_AMP_DIS			(0x0 << 1)

#define  RK3026_ADCR_RST_EN                     (0x1 << 0)
#define  RK3026_ADCR_RST_DIS                     (0x0 << 0)

/* DAC & VOUT Control (0xa0)  */
#define  RK3026_CURRENT_EN                  (0x1 << 6)
#define  RK3026_CURRENT_DIS                  (0x0 << 6)
#define  RK3026_REF_VOL_DACL_EN_SFT                  (5)
#define  RK3026_REF_VOL_DACL_EN                  (0x1 << 5)
#define  RK3026_REF_VOL_DACL_DIS                 (0x0 << 5)
#define  RK3026_ZO_DET_VOUTL_SFT                 (4)
#define  RK3026_ZO_DET_VOUTL_EN                 (0x1 << 4)
#define  RK3026_ZO_DET_VOUTL_DIS                  (0x0 << 4)
#define  RK3026_DET_ERAPHONE_DIS                  (0x0 << 3)
#define  RK3026_DET_ERAPHONE_EN                  (0x1 << 3)
#define  RK3026_REF_VOL_DACR_EN_SFT                  (1)
#define  RK3026_REF_VOL_DACR_EN                  (0x1 << 1)
#define  RK3026_REF_VOL_DACR_DIS                 (0x0 << 1)
#define  RK3026_ZO_DET_VOUTR_SFT                 (0)
#define  RK3026_ZO_DET_VOUTR_EN                 (0x1 << 0)
#define  RK3026_ZO_DET_VOUTR_DIS                  (0x0 << 0)

/* DAC control (0xa4) */
#define RK3026_DACL_REF_VOL_EN_SFT			(7)
#define RK3026_DACL_REF_VOL_EN			(0x1 << 7)
#define RK3026_DACL_REF_VOL_DIS			(0x0 << 7)

#define RK3026_DACL_CLK_EN		       (0x1 << 6)
#define RK3026_DACL_CLK_DIS		       (0x0 << 6)

#define RK3026_DACL_EN			(0x1 << 5)
#define RK3026_DACL_DIS			(0x0 << 5)

#define  RK3026_DACL_INIT                     (0x0 << 4)
#define  RK3026_DACL_WORK                    (0x1 << 4)

#define RK3026_DACR_REF_VOL_EN_SFT			(3)
#define RK3026_DACR_REF_VOL_EN			(0x1 << 3)
#define RK3026_DACR_REF_VOL_DIS			(0x0 << 3)

#define RK3026_DACR_CLK_EN		       (0x1 << 2)
#define RK3026_DACR_CLK_DIS		       (0x0 << 2)

#define RK3026_DACR_EN			(0x1 << 1)
#define RK3026_DACR_DIS			(0x0 << 1)

#define  RK3026_DACR_INIT                        (0x0 << 0)
#define  RK3026_DACR_WORK                    (0x1 << 0)

/* HPMIXL  HPMIXR Control (0xa8)  */
#define RK3026_HPMIXL_SFT                         (6)
#define RK3026_HPMIXL_EN                         (0x1 << 6)
#define RK3026_HPMIXL_DIS                      (0x0 << 6)
#define RK3026_HPMIXL_INIT1              (0x0 << 5)
#define RK3026_HPMIXL_WORK1               (0x1 << 5)
#define RK3026_HPMIXL_INIT2              (0x0 << 4)
#define RK3026_HPMIXL_WORK2                (0x1 << 4)
#define RK3026_HPMIXR_SFT                         (2)
#define RK3026_HPMIXR_EN                         (0x1 << 2)
#define RK3026_HPMIXR_DIS                      (0x0 << 2)
#define RK3026_HPMIXR_INIT1               (0x0 << 1)
#define RK3026_HPMIXR_WORK1               (0x1 << 1)
#define RK3026_HPMIXR_INIT2              (0x0 << 0)
#define RK3026_HPMIXR_WORK2                (0x1 << 0)

/* HPMIXL Control  (0xac) */
#define RK3026_HPMIXL_BYPASS_SFT             (7)
#define RK3026_HPMIXL_SEL_ALCL_SFT              (6)
#define RK3026_HPMIXL_SEL_ALCR_SFT              (5)
#define RK3026_HPMIXL_SEL_DACL_SFT             (4)
#define RK3026_HPMIXR_BYPASS_SFT             (3)
#define RK3026_HPMIXR_SEL_ALCL_SFT              (2)
#define RK3026_HPMIXR_SEL_ALCR_SFT              (1)
#define RK3026_HPMIXR_SEL_DACR_SFT             (0)

/* HPOUT Control  (0xb0) */
#define RK3026_HPOUTL_PWR_SHT			(7)
#define RK3026_HPOUTL_MSK                      (0x1 << 7)
#define RK3026_HPOUTL_EN                       (0x1 << 7)
#define RK3026_HPOUTL_DIS			(0x0 << 7)
#define RK3026_HPOUTL_INIT_MSK			(0x1 << 6)
#define RK3026_HPOUTL_INIT			(0x0 << 6)
#define RK3026_HPOUTL_WORK			(0x1 << 6)
#define RK3026_HPOUTL_MUTE_SHT			(5)
#define RK3026_HPOUTL_MUTE_MSK			(0x1 << 5)
#define RK3026_HPOUTL_MUTE_EN			(0x0 << 5)
#define RK3026_HPOUTL_MUTE_DIS			(0x1 << 5)
#define RK3026_HPOUTR_PWR_SHT			(4)
#define RK3026_HPOUTR_MSK                      (0x1 << 4)
#define RK3026_HPOUTR_EN         		(0x1 << 4)
#define RK3026_HPOUTR_DIS			(0x0 << 4)
#define RK3026_HPOUTR_INIT_MSK			(0x1 << 3)
#define RK3026_HPOUTR_WORK			(0x1 << 3)
#define RK3026_HPOUTR_INIT			(0x0 << 3)
#define RK3026_HPOUTR_MUTE_SHT			(2)
#define RK3026_HPOUTR_MUTE_MSK			(0x1 << 2)
#define RK3026_HPOUTR_MUTE_EN			(0x0 << 2)
#define RK3026_HPOUTR_MUTE_DIS			(0x1 << 2)

#define RK3026_HPVREF_PWR_SHT			(1)
#define RK3026_HPVREF_EN			(0x1 << 1)
#define RK3026_HPVREF_DIS			(0x0 << 1)
#define RK3026_HPVREF_WORK			(0x1 << 0)
#define RK3026_HPVREF_INIT			(0x0 << 0)

/* HPOUT GAIN (0xb4 0xb8) */
#define  RK3026_HPOUT_GAIN_SFT			(0)

/* SELECT CURR prechagrge/discharge (0xbc) */
#define RK3026_PRE_HPOUT			(0x1 << 5)
#define RK3026_DIS_HPOUT			(0x0 << 5)
#define RK3026_CUR_10UA_EN			(0x0 << 4)
#define RK3026_CUR_10UA_DIS			(0x1 << 4)
#define RK3026_CUR_I_EN				(0x0 << 3)
#define RK3026_CUR_I_DIS			(0x1 << 3)	
#define RK3026_CUR_2I_EN			(0x0 << 2)
#define RK3026_CUR_2I_DIS			(0x1 << 2)
#define RK3026_CUR_4I_EN			(0x0 << 0)
#define RK3026_CUR_4I_DIS			(0x3 << 0)

/* PGA AGC control 1 (0xc0 0x100) */
#define RK3026_PGA_AGC_WAY_MASK			(0x1 << 6)
#define RK3026_PGA_AGC_WAY_SFT			6
#define RK3026_PGA_AGC_WAY_JACK			(0x1 << 6)
#define RK3026_PGA_AGC_WAY_NOR			(0x0 << 6)

#define RK3026_PGA_AGC_BK_WAY_SFT			4
#define RK3026_PGA_AGC_BK_WAY_JACK1		(0x1 << 4)
#define RK3026_PGA_AGC_BK_WAY_NOR			(0x0 << 4)
#define RK3026_PGA_AGC_BK_WAY_JACK2		(0x2 << 4)
#define RK3026_PGA_AGC_BK_WAY_JACK3		(0x3 << 4)

#define RK3026_PGA_AGC_HOLD_T_MASK		0xf
#define RK3026_PGA_AGC_HOLD_T_SFT		0
#define RK3026_PGA_AGC_HOLD_T_1024		0xa
#define RK3026_PGA_AGC_HOLD_T_512		0x9
#define RK3026_PGA_AGC_HOLD_T_256		0x8
#define RK3026_PGA_AGC_HOLD_T_128		0x7
#define RK3026_PGA_AGC_HOLD_T_64			0x6
#define RK3026_PGA_AGC_HOLD_T_32			0x5
#define RK3026_PGA_AGC_HOLD_T_16			0x4
#define RK3026_PGA_AGC_HOLD_T_8			0x3
#define RK3026_PGA_AGC_HOLD_T_4			0x2
#define RK3026_PGA_AGC_HOLD_T_2			0x1
#define RK3026_PGA_AGC_HOLD_T_0			0x0

/* PGA AGC control 2 (0xc4 0x104) */
#define RK3026_PGA_AGC_GRU_T_MASK		(0xf << 4)
#define RK3026_PGA_AGC_GRU_T_SFT			4
#define RK3026_PGA_AGC_GRU_T_512			(0xa << 4)
#define RK3026_PGA_AGC_GRU_T_256			(0x9 << 4)
#define RK3026_PGA_AGC_GRU_T_128			(0x8 << 4)
#define RK3026_PGA_AGC_GRU_T_64			(0x7 << 4)
#define RK3026_PGA_AGC_GRU_T_32			(0x6 << 4)
#define RK3026_PGA_AGC_GRU_T_16			(0x5 << 4)
#define RK3026_PGA_AGC_GRU_T_8			(0x4 << 4)
#define RK3026_PGA_AGC_GRU_T_4			(0x3 << 4)
#define RK3026_PGA_AGC_GRU_T_2			(0x2 << 4)
#define RK3026_PGA_AGC_GRU_T_1			(0x1 << 4)
#define RK3026_PGA_AGC_GRU_T_0_5			(0x0 << 4)

#define RK3026_PGA_AGC_GRD_T_MASK		0xf
#define RK3026_PGA_AGC_GRD_T_SFT			0
#define RK3026_PGA_AGC_GRD_T_128_32		0xa
#define RK3026_PGA_AGC_GRD_T_64_16		0x9
#define RK3026_PGA_AGC_GRD_T_32_8		0x8
#define RK3026_PGA_AGC_GRD_T_16_4		0x7
#define RK3026_PGA_AGC_GRD_T_8_2			0x6
#define RK3026_PGA_AGC_GRD_T_4_1			0x5
#define RK3026_PGA_AGC_GRD_T_2_0_512		0x4
#define RK3026_PGA_AGC_GRD_T_1_0_256		0x3
#define RK3026_PGA_AGC_GRD_T_0_500_128		0x2
#define RK3026_PGA_AGC_GRD_T_0_250_64		0x1
#define RK3026_PGA_AGC_GRD_T_0_125_32		0x0

/* PGA AGC control 3 (0xc8 0x108) */
#define RK3026_PGA_AGC_MODE_MASK			(0x1 << 7)
#define RK3026_PGA_AGC_MODE_SFT			7
#define RK3026_PGA_AGC_MODE_LIMIT		(0x1 << 7)
#define RK3026_PGA_AGC_MODE_NOR			(0x0 << 7)

#define RK3026_PGA_AGC_ZO_MASK			(0x1 << 6)
#define RK3026_PGA_AGC_ZO_SFT			6
#define RK3026_PGA_AGC_ZO_EN			(0x1 << 6)
#define RK3026_PGA_AGC_ZO_DIS			(0x0 << 6)

#define RK3026_PGA_AGC_REC_MODE_MASK		(0x1 << 5)
#define RK3026_PGA_AGC_REC_MODE_SFT		5
#define RK3026_PGA_AGC_REC_MODE_AC		(0x1 << 5)
#define RK3026_PGA_AGC_REC_MODE_RN		(0x0 << 5)

#define RK3026_PGA_AGC_FAST_D_MASK		(0x1 << 4)
#define RK3026_PGA_AGC_FAST_D_SFT		4
#define RK3026_PGA_AGC_FAST_D_EN			(0x1 << 4)
#define RK3026_PGA_AGC_FAST_D_DIS		(0x0 << 4)

#define RK3026_PGA_AGC_NG_MASK			(0x1 << 3)
#define RK3026_PGA_AGC_NG_SFT			3
#define RK3026_PGA_AGC_NG_EN			(0x1 << 3)
#define RK3026_PGA_AGC_NG_DIS			(0x0 << 3)

#define RK3026_PGA_AGC_NG_THR_MASK		0x7
#define RK3026_PGA_AGC_NG_THR_SFT		0
#define RK3026_PGA_AGC_NG_THR_N81DB		0x7
#define RK3026_PGA_AGC_NG_THR_N75DB		0x6
#define RK3026_PGA_AGC_NG_THR_N69DB		0x5
#define RK3026_PGA_AGC_NG_THR_N63DB		0x4
#define RK3026_PGA_AGC_NG_THR_N57DB		0x3
#define RK3026_PGA_AGC_NG_THR_N51DB		0x2
#define RK3026_PGA_AGC_NG_THR_N45DB		0x1
#define RK3026_PGA_AGC_NG_THR_N39DB		0x0

/* PGA AGC Control 4 (0xcc 0x10c) */
#define RK3026_PGA_AGC_ZO_MODE_MASK		(0x1 << 5)
#define RK3026_PGA_AGC_ZO_MODE_SFT		5
#define RK3026_PGA_AGC_ZO_MODE_UWRC		(0x1 << 5)
#define RK3026_PGA_AGC_ZO_MODE_UARC		(0x0 << 5)

#define RK3026_PGA_AGC_VOL_MASK			0x1f
#define RK3026_PGA_AGC_VOL_SFT			0

/* PGA ASR Control (0xd0 0x110) */
#define RK3026_PGA_SLOW_CLK_MASK			(0x1 << 3)
#define RK3026_PGA_SLOW_CLK_SFT			3
#define RK3026_PGA_SLOW_CLK_EN			(0x1 << 3)
#define RK3026_PGA_SLOW_CLK_DIS			(0x0 << 3)

#define RK3026_PGA_ASR_MASK			0x7
#define RK3026_PGA_ASR_SFT			0
#define RK3026_PGA_ASR_8KHz			0x7
#define RK3026_PGA_ASR_12KHz			0x6
#define RK3026_PGA_ASR_16KHz			0x5
#define RK3026_PGA_ASR_24KHz			0x4
#define RK3026_PGA_ASR_32KHz			0x3
#define RK3026_PGA_ASR_441KHz			0x2
#define RK3026_PGA_ASR_48KHz			0x1
#define RK3026_PGA_ASR_96KHz			0x0

/* PGA AGC Control 5 (0xe4 0x124) */
#define RK3026_PGA_AGC_MASK			(0x1 << 6)
#define RK3026_PGA_AGC_SFT			6
#define RK3026_PGA_AGC_EN			(0x1 << 6)
#define RK3026_PGA_AGC_DIS			(0x0 << 6)

#define RK3026_PGA_AGC_MAX_G_MASK		(0x7 << 3)
#define RK3026_PGA_AGC_MAX_G_SFT			3
#define RK3026_PGA_AGC_MAX_G_28_5DB		(0x7 << 3)
#define RK3026_PGA_AGC_MAX_G_22_5DB		(0x6 << 3)
#define RK3026_PGA_AGC_MAX_G_16_5DB		(0x5 << 3)
#define RK3026_PGA_AGC_MAX_G_10_5DB		(0x4 << 3)
#define RK3026_PGA_AGC_MAX_G_4_5DB		(0x3 << 3)
#define RK3026_PGA_AGC_MAX_G_N1_5DB		(0x2 << 3)
#define RK3026_PGA_AGC_MAX_G_N7_5DB		(0x1 << 3)
#define RK3026_PGA_AGC_MAX_G_N13_5DB		(0x0 << 3)

#define RK3026_PGA_AGC_MIN_G_MASK		0x7
#define RK3026_PGA_AGC_MIN_G_SFT			0
#define RK3026_PGA_AGC_MIN_G_24DB		0x7
#define RK3026_PGA_AGC_MIN_G_18DB		0x6
#define RK3026_PGA_AGC_MIN_G_12DB		0x5
#define RK3026_PGA_AGC_MIN_G_6DB			0x4
#define RK3026_PGA_AGC_MIN_G_0DB			0x3
#define RK3026_PGA_AGC_MIN_G_N6DB		0x2
#define RK3026_PGA_AGC_MIN_G_N12DB		0x1
#define RK3026_PGA_AGC_MIN_G_N18DB		0x0

enum {
	RK3026_HIFI,
	RK3026_VOICE,
};

enum {
	RK3026_MONO = 1,
	RK3026_STEREO,
};

enum {
	OFF,
	RCV,
	SPK_PATH,
	HP_PATH,
	HP_NO_MIC,
	BT,
	SPK_HP,
	RING_SPK,
	RING_HP,
	RING_HP_NO_MIC,
	RING_SPK_HP,
};

enum {
	MIC_OFF,
	Main_Mic,
	Hands_Free_Mic,
	BT_Sco_Mic,
};

struct rk3026_reg_val_typ {
	unsigned int reg;
	unsigned int value;
};

struct rk3026_init_bit_typ {
	unsigned int reg;
	unsigned int power_bit;
	unsigned int init2_bit;
	unsigned int init1_bit;
	unsigned int init0_bit;	
};

bool get_hdmi_state(void);

struct rk3026_codec_pdata {
	int spk_ctl_gpio;
	int hp_ctl_gpio;	
	int delay_time;
};

#endif //__RK3026_CODEC_H__
