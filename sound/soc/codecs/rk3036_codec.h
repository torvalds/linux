/*
 *
 * Copyright (C) 2014 rockchip
 *
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 */

#ifndef _RK3036_CODEC_H
#define _RK3036_CODEC_H

/* codec register */
#define RK3036_CODEC_RESET			(0x00 << 2)
#define RK3036_CODEC_REG03			(0x03 << 2)
#define RK3036_CODEC_REG04			(0x04 << 2)
#define RK3036_CODEC_REG05			(0x05 << 2)

#define RK3036_CODEC_REG22			(0x22 << 2)
#define RK3036_CODEC_REG23			(0x23 << 2)
#define RK3036_CODEC_REG24			(0x24 << 2)
#define RK3036_CODEC_REG25			(0x25 << 2)
#define RK3036_CODEC_REG26			(0x26 << 2)
#define RK3036_CODEC_REG27			(0x27 << 2)
#define RK3036_CODEC_REG28			(0x28 << 2)

/* RK3036_CODEC_RESET */
#define RK3036_CR00_DIGITAL_RESET           (0 << 1)
#define RK3036_CR00_DIGITAL_WORK            (1 << 1)
#define RK3036_CR00_SYSTEM_RESET            (0 << 0)
#define RK3036_CR00_SYSTEM_WORK             (1 << 0)

/*RK3036_CODEC_REG03*/
#define RK3036_CR03_DIRECTION_MASK          (1 << 5)
#define RK3036_CR03_DIRECTION_IN            (0 << 5)
#define RK3036_CR03_DIRECTION_IOUT          (1 << 5)
#define RK3036_CR03_I2SMODE_MASK            (1 << 4)
#define RK3036_CR03_I2SMODE_SLAVE           (0 << 4)
#define RK3036_CR03_I2SMODE_MASTER          (1 << 4)

/*RK3036_CODEC_REG04*/
#define RK3036_CR04_I2SLRC_MASK             (1 << 7)
#define RK3036_CR04_I2SLRC_NORMAL           (0 << 7)
#define RK3036_CR04_I2SLRC_REVERSAL         (1 << 7)
#define RK3036_CR04_HFVALID_MASK            (3 << 5)
#define RK3036_CR04_HFVALID_16BITS          (0 << 5)
#define RK3036_CR04_HFVALID_20BITS          (1 << 5)
#define RK3036_CR04_HFVALID_24BITS          (2 << 5)
#define RK3036_CR04_HFVALID_32BITS          (3 << 5)
#define RK3036_CR04_MODE_MASK               (3 << 3)
#define RK3036_CR04_MODE_RIGHT              (0 << 3)
#define RK3036_CR04_MODE_LEFT               (1 << 3)
#define RK3036_CR04_MODE_I2S                (2 << 3)
#define RK3036_CR04_MODE_PCM                (3 << 3)
#define RK3036_CR04_LR_SWAP_MASK            (1 << 2)
#define RK3036_CR04_LR_SWAP_DIS             (0 << 2)
#define RK3036_CR04_LR_SWAP_EN              (1 << 2)

/*RK3036_CODEC_REG05*/
#define RK3036_CR05_FRAMEH_MASK             (3 << 2)
#define RK3036_CR05_FRAMEH_16BITS           (0 << 2)
#define RK3036_CR05_FRAMEH_20BITS           (1 << 2)
#define RK3036_CR05_FRAMEH_24BITS           (2 << 2)
#define RK3036_CR05_FRAMEH_32BITS           (3 << 2)
#define RK3036_CR05_DAC_RESET_MASK          (1 << 1)
#define RK3036_CR05_DAC_RESET_EN            (0 << 1)
#define RK3036_CR05_DAC_RESET_DIS           (1 << 1)
#define RK3036_CR05_BCLKPOL_MASK            (1 << 0)
#define RK3036_CR05_BCLKPOL_NORMAL          (0 << 0)
#define RK3036_CR05_BCLKPOL_REVERSAL        (1 << 0)

/*RK3036_CODEC_REG22*/
#define RK3036_CR22_DACL_PATH_REFV_MASK     (1 << 5)
#define RK3036_CR22_DACL_PATH_REFV_STOP     (0 << 5)
#define RK3036_CR22_DACL_PATH_REFV_WORK     (1 << 5)
#define RK3036_CR22_DACR_PATH_REFV_MASK     (1 << 4)
#define RK3036_CR22_DACR_PATH_REFV_STOP     (0 << 4)
#define RK3036_CR22_DACR_PATH_REFV_WORK     (1 << 4)
#define RK3036_CR22_DACL_CLK_STOP           (0 << 3)
#define RK3036_CR22_DACL_CLK_WORK           (1 << 3)
#define RK3036_CR22_DACR_CLK_STOP           (0 << 2)
#define RK3036_CR22_DACR_CLK_WORK           (1 << 2)
#define RK3036_CR22_DACL_STOP               (0 << 1)
#define RK3036_CR22_DACL_WORK               (1 << 1)
#define RK3036_CR22_DACR_STOP               (0 << 0)
#define RK3036_CR22_DACR_WORK               (1 << 0)

/*RK3036_CODEC_REG23*/
#define RK3036_CR23_HPOUTL_INIT             (0 << 3)
#define RK3036_CR23_HPOUTL_WORK             (1 << 3)
#define RK3036_CR23_HPOUTR_INIT             (0 << 2)
#define RK3036_CR23_HPOUTR_WORK             (1 << 2)
#define RK3036_CR23_HPOUTL_EN_STOP          (0 << 1)
#define RK3036_CR23_HPOUTL_EN_WORK          (1 << 1)
#define RK3036_CR23_HPOUTR_EN_STOP          (0 << 0)
#define RK3036_CR23_HPOUTR_EN_WORK          (1 << 0)

/*RK3036_CODEC_REG24*/
#define RK3036_CR24_DAC_SOURCE_STOP         (0 << 5)
#define RK3036_CR24_DAC_SOURCE_WORK         (1 << 5)
#define RK3036_CR24_DAC_PRECHARGE           (0 << 4)
#define RK3036_CR24_DAC_DISCHARGE           (1 << 4)
#define RK3036_CR24_DACL_REFV_STOP          (0 << 3)
#define RK3036_CR24_DACL_REFV_WORK          (1 << 3)
#define RK3036_CR24_DACR_REFV_STOP          (0 << 2)
#define RK3036_CR24_DACR_REFV_WORK          (1 << 2)
#define RK3036_CR24_VOUTL_ZEROD_STOP        (0 << 1)
#define RK3036_CR24_VOUTL_ZEROD_WORK        (1 << 1)
#define RK3036_CR24_VOUTR_ZEROD_STOP        (0 << 0)
#define RK3036_CR24_VOUTR_ZEROD_WORK        (1 << 0)

/*RK3036_CODEC_REG27*/
#define RK3036_CR27_DACL_INIT               (0 << 7)
#define RK3036_CR27_DACL_WORK               (1 << 7)
#define RK3036_CR27_DACR_INIT               (0 << 6)
#define RK3036_CR27_DACR_WORK               (1 << 6)
#define RK3036_CR27_HPOUTL_G_MUTE           (0 << 5)
#define RK3036_CR27_HPOUTL_G_WORK           (1 << 5)
#define RK3036_CR27_HPOUTR_G_MUTE           (0 << 4)
#define RK3036_CR27_HPOUTR_G_WORK           (1 << 4)
#define RK3036_CR27_HPOUTL_POP_PRECHARGE    (1 << 2)
#define RK3036_CR27_HPOUTL_POP_WORK         (2 << 2)
#define RK3036_CR27_HPOUTR_POP_PRECHARGE    (1 << 0)
#define RK3036_CR27_HPOUTR_POP_WORK         (2 << 0)

/*RK3036_CODEC_REG28*/
#define RK3036_CR28_YES_027I                (0 << 5)
#define RK3036_CR28_NON_027I                (1 << 5)
#define RK3036_CR28_YES_050I                (0 << 4)
#define RK3036_CR28_NON_050I                (1 << 4)
#define RK3036_CR28_YES_100I                (0 << 3)
#define RK3036_CR28_NON_100I                (1 << 3)
#define RK3036_CR28_YES_130I                (0 << 2)
#define RK3036_CR28_NON_130I                (1 << 2)
#define RK3036_CR28_YES_260I                (0 << 1)
#define RK3036_CR28_NON_260I                (1 << 1)
#define RK3036_CR28_YES_400I                (0 << 0)
#define RK3036_CR28_NON_400I                (1 << 0)

enum {
	RK3036_HIFI,
	RK3036_VOICE,
};

struct rk3036_reg_val_typ {
	unsigned int reg;
	unsigned int value;
};

struct rk3036_init_bit_typ {
	unsigned int reg;
	unsigned int power_bit;
	unsigned int init2_bit;
	unsigned int init1_bit;
	unsigned int init0_bit;
};

#endif
