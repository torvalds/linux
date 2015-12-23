/*
 * Driver of Inno Codec for rk3036 by Rockchip Inc.
 *
 * Author: Zheng ShunQian<zhengsq@rock-chips.com>
 */

#ifndef _INNO_RK3036_CODEC_H
#define _INNO_RK3036_CODEC_H

/* codec registers */
#define INNO_R00	0x00
#define INNO_R01	0x0c
#define INNO_R02	0x10
#define INNO_R03	0x14
#define INNO_R04	0x88
#define INNO_R05	0x8c
#define INNO_R06	0x90
#define INNO_R07	0x94
#define INNO_R08	0x98
#define INNO_R09	0x9c
#define INNO_R10	0xa0

/* register bit filed */
#define INNO_R00_CSR_RESET		(0x0 << 0) /*codec system reset*/
#define INNO_R00_CSR_WORK		(0x1 << 0)
#define INNO_R00_CDCR_RESET		(0x0 << 1) /*codec digital core reset*/
#define INNO_R00_CDCR_WORK		(0x1 << 1)
#define INNO_R00_PRB_DISABLE		(0x0 << 6) /*power reset bypass*/
#define INNO_R00_PRB_ENABLE		(0x1 << 6)

#define INNO_R01_I2SMODE_MSK		(0x1 << 4)
#define INNO_R01_I2SMODE_SLAVE		(0x0 << 4)
#define INNO_R01_I2SMODE_MASTER		(0x1 << 4)
#define INNO_R01_PINDIR_MSK		(0x1 << 5)
#define INNO_R01_PINDIR_IN_SLAVE	(0x0 << 5) /*direction of pin*/
#define INNO_R01_PINDIR_OUT_MASTER	(0x1 << 5)

#define INNO_R02_LRS_MSK		(0x1 << 2)
#define INNO_R02_LRS_NORMAL		(0x0 << 2) /*DAC Left Right Swap*/
#define INNO_R02_LRS_SWAP		(0x1 << 2)
#define INNO_R02_DACM_MSK		(0x3 << 3)
#define INNO_R02_DACM_PCM		(0x3 << 3) /*DAC Mode*/
#define INNO_R02_DACM_I2S		(0x2 << 3)
#define INNO_R02_DACM_LJM		(0x1 << 3)
#define INNO_R02_DACM_RJM		(0x0 << 3)
#define INNO_R02_VWL_MSK		(0x3 << 5)
#define INNO_R02_VWL_32BIT		(0x3 << 5) /*1/2Frame Valid Word Len*/
#define INNO_R02_VWL_24BIT		(0x2 << 5)
#define INNO_R02_VWL_20BIT		(0x1 << 5)
#define INNO_R02_VWL_16BIT		(0x0 << 5)
#define INNO_R02_LRCP_MSK		(0x1 << 7)
#define INNO_R02_LRCP_NORMAL		(0x0 << 7) /*Left Right Polarity*/
#define INNO_R02_LRCP_REVERSAL		(0x1 << 7)

#define INNO_R03_BCP_MSK		(0x1 << 0)
#define INNO_R03_BCP_NORMAL		(0x0 << 0) /*DAC bit clock polarity*/
#define INNO_R03_BCP_REVERSAL		(0x1 << 0)
#define INNO_R03_DACR_MSK		(0x1 << 1)
#define INNO_R03_DACR_RESET		(0x0 << 1) /*DAC Reset*/
#define INNO_R03_DACR_WORK		(0x1 << 1)
#define INNO_R03_FWL_MSK		(0x3 << 2)
#define INNO_R03_FWL_32BIT		(0x3 << 2) /*1/2Frame Word Length*/
#define INNO_R03_FWL_24BIT		(0x2 << 2)
#define INNO_R03_FWL_20BIT		(0x1 << 2)
#define INNO_R03_FWL_16BIT		(0x0 << 2)

#define INNO_R04_DACR_SW_SHIFT		0
#define INNO_R04_DACL_SW_SHIFT		1
#define INNO_R04_DACR_CLK_SHIFT		2
#define INNO_R04_DACL_CLK_SHIFT		3
#define INNO_R04_DACR_VREF_SHIFT	4
#define INNO_R04_DACL_VREF_SHIFT	5

#define INNO_R05_HPR_EN_SHIFT		0
#define INNO_R05_HPL_EN_SHIFT		1
#define INNO_R05_HPR_WORK_SHIFT		2
#define INNO_R05_HPL_WORK_SHIFT		3

#define INNO_R06_VOUTR_CZ_SHIFT		0
#define INNO_R06_VOUTL_CZ_SHIFT		1
#define INNO_R06_DACR_HILO_VREF_SHIFT	2
#define INNO_R06_DACL_HILO_VREF_SHIFT	3
#define INNO_R06_DAC_EN_SHIFT		5

#define INNO_R06_DAC_PRECHARGE		(0x0 << 4) /*PreCharge control for DAC*/
#define INNO_R06_DAC_DISCHARGE		(0x1 << 4)

#define INNO_HP_GAIN_SHIFT		0
/* Gain of output, 1.5db step: -39db(0x0) ~ 0db(0x1a) ~ 6db(0x1f) */
#define INNO_HP_GAIN_0DB		0x1a
#define INNO_HP_GAIN_N39DB		0x0

#define INNO_R09_HP_ANTIPOP_MSK		0x3
#define INNO_R09_HP_ANTIPOP_OFF		0x1
#define INNO_R09_HP_ANTIPOP_ON		0x2
#define INNO_R09_HPR_ANITPOP_SHIFT	0
#define INNO_R09_HPL_ANITPOP_SHIFT	2
#define INNO_R09_HPR_MUTE_SHIFT		4
#define INNO_R09_HPL_MUTE_SHIFT		5
#define INNO_R09_DACR_SWITCH_SHIFT	6
#define INNO_R09_DACL_SWITCH_SHIFT	7

#define INNO_R10_CHARGE_SEL_CUR_400I_YES	(0x0 << 0)
#define INNO_R10_CHARGE_SEL_CUR_400I_NO		(0x1 << 0)
#define INNO_R10_CHARGE_SEL_CUR_260I_YES	(0x0 << 1)
#define INNO_R10_CHARGE_SEL_CUR_260I_NO		(0x1 << 1)
#define INNO_R10_CHARGE_SEL_CUR_130I_YES	(0x0 << 2)
#define INNO_R10_CHARGE_SEL_CUR_130I_NO		(0x1 << 2)
#define INNO_R10_CHARGE_SEL_CUR_100I_YES	(0x0 << 3)
#define INNO_R10_CHARGE_SEL_CUR_100I_NO		(0x1 << 3)
#define INNO_R10_CHARGE_SEL_CUR_050I_YES	(0x0 << 4)
#define INNO_R10_CHARGE_SEL_CUR_050I_NO		(0x1 << 4)
#define INNO_R10_CHARGE_SEL_CUR_027I_YES	(0x0 << 5)
#define INNO_R10_CHARGE_SEL_CUR_027I_NO		(0x1 << 5)

#define INNO_R10_MAX_CUR (INNO_R10_CHARGE_SEL_CUR_400I_YES | \
			  INNO_R10_CHARGE_SEL_CUR_260I_YES | \
			  INNO_R10_CHARGE_SEL_CUR_130I_YES | \
			  INNO_R10_CHARGE_SEL_CUR_100I_YES | \
			  INNO_R10_CHARGE_SEL_CUR_050I_YES | \
			  INNO_R10_CHARGE_SEL_CUR_027I_YES)

#endif
