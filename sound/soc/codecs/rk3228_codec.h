/*
 * rk3228_codec.h  --  rk3228 ALSA Soc Audio driver
 *
 * Copyright (c) 2018, Fuzhou Rockchip Electronics Co., Ltd All rights reserved.
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms and conditions of the GNU General Public License,
 * version 2, as published by the Free Software Foundation.
 *
 * This program is distributed in the hope it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License for
 * more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 *
 */

#ifndef _RK3228_CODEC_H
#define _RK3228_CODEC_H

/* codec register */
#define CODEC_RESET			(0x00 << 2)
#define DAC_INIT_CTRL1			(0x03 << 2)
#define DAC_INIT_CTRL2			(0x04 << 2)
#define DAC_INIT_CTRL3			(0x05 << 2)
#define DAC_PRECHARGE_CTRL		(0x22 << 2)
#define DAC_PWR_CTRL			(0x23 << 2)
#define DAC_CLK_CTRL			(0x24 << 2)
#define HPMIX_CTRL			(0x25 << 2)
#define DAC_SELECT			(0x26 << 2)
#define HPOUT_CTRL			(0x27 << 2)
#define HPOUTL_GAIN_CTRL		(0x28 << 2)
#define HPOUTR_GAIN_CTRL		(0x29 << 2)
#define HPOUT_POP_CTRL			(0x2a << 2)

/* REG00: CODEC_RESET */
#define PWR_RST_BYPASS_DIS		BIT(6)
#define PWR_RST_BYPASS_EN		BIT(6)
#define DIG_CORE_RST			(0 << 1)
#define DIG_CORE_WORK			BIT(1)
#define SYS_RST				(0)
#define SYS_WORK			BIT(0)

/* REG03: DAC_INIT_CTRL1 */
#define PIN_DIRECTION_MASK		BIT(5)
#define PIN_DIRECTION_IN		(0 << 5)
#define PIN_DIRECTION_OUT		BIT(5)
#define DAC_I2S_MODE_MASK		BIT(4)
#define DAC_I2S_MODE_SLAVE		(0 << 4)
#define DAC_I2S_MODE_MASTER		BIT(4)

/* REG04: DAC_INIT_CTRL2 */
#define DAC_I2S_LRP_MASK		BIT(7)
#define DAC_I2S_LRP_NORMAL		(0 << 7)
#define DAC_I2S_LRP_REVERSAL		BIT(7)
#define DAC_VDL_MASK			(3 << 5)
#define DAC_VDL_16BITS			(0 << 5)
#define DAC_VDL_20BITS			BIT(5)
#define DAC_VDL_24BITS			(2 << 5)
#define DAC_VDL_32BITS			(3 << 5)
#define DAC_MODE_MASK			(3 << 3)
#define DAC_MODE_RJM			(0 << 3)
#define DAC_MODE_LJM			BIT(3)
#define DAC_MODE_I2S			(2 << 3)
#define DAC_MODE_PCM			(3 << 3)
#define DAC_LR_SWAP_MASK		BIT(2)
#define DAC_LR_SWAP_DIS			(0 << 2)
#define DAC_LR_SWAP_EN			BIT(2)

/* REG05: DAC_INIT_CTRL3 */
#define DAC_WL_MASK			(3 << 2)
#define DAC_WL_16BITS			(0 << 2)
#define DAC_WL_20BITS			BIT(2)
#define DAC_WL_24BITS			(2 << 2)
#define DAC_WL_32BITS			(3 << 2)
#define DAC_RST_MASK			BIT(1)
#define DAC_RST_EN			(0 << 1)
#define DAC_RST_DIS			BIT(1)
#define DAC_BCP_MASK			BIT(0)
#define DAC_BCP_NORMAL			(0 << 0)
#define DAC_BCP_REVERSAL		BIT(0)

/* REG22: DAC_PRECHARGE_CTRL */
#define DAC_CHARGE_PRECHARGE		BIT(7)
#define DAC_CHARGE_DISCHARGE		(0 << 7)
#define DAC_CHARGE_XCHARGE_MASK		BIT(7)
#define DAC_CHARGE_CURRENT_64I		BIT(6)
#define DAC_CHARGE_CURRENT_64I_MASK	BIT(6)
#define DAC_CHARGE_CURRENT_32I		BIT(5)
#define DAC_CHARGE_CURRENT_32I_MASK	BIT(5)
#define DAC_CHARGE_CURRENT_16I		BIT(4)
#define DAC_CHARGE_CURRENT_16I_MASK	BIT(4)
#define DAC_CHARGE_CURRENT_08I		BIT(3)
#define DAC_CHARGE_CURRENT_08I_MASK	BIT(3)
#define DAC_CHARGE_CURRENT_04I		BIT(2)
#define DAC_CHARGE_CURRENT_04I_MASK	BIT(2)
#define DAC_CHARGE_CURRENT_02I		BIT(1)
#define DAC_CHARGE_CURRENT_02I_MASK	BIT(1)
#define DAC_CHARGE_CURRENT_I		BIT(0)
#define DAC_CHARGE_CURRENT_I_MASK	BIT(0)
#define DAC_CHARGE_CURRENT_ALL_MASK	(0x7f)
#define DAC_CHARGE_CURRENT_ALL_OFF	(0x0)
#define DAC_CHARGE_CURRENT_ALL_ON	(0x7f)

/* REG23: DAC_PWR_CTRL */
#define DAC_PWR_OFF			(0 << 6)
#define DAC_PWR_ON			BIT(6)
#define DAC_PWR_MASK			BIT(6)
#define DACL_PATH_REFV_OFF		(0 << 5)
#define DACL_PATH_REFV_ON		BIT(5)
#define DACL_PATH_REFV_MASK		BIT(5)
#define HPOUTL_ZERO_CROSSING_OFF	(0 << 4)
#define HPOUTL_ZERO_CROSSING_ON		BIT(4)
#define DACR_PATH_REFV_OFF		(0 << 1)
#define DACR_PATH_REFV_ON		BIT(1)
#define DACR_PATH_REFV_MASK		BIT(1)
#define HPOUTR_ZERO_CROSSING_OFF	(0 << 0)
#define HPOUTR_ZERO_CROSSING_ON		BIT(0)

/* REG24: DAC_CLK_CTRL */
#define DACL_REFV_OFF			(0 << 7)
#define DACL_REFV_ON			BIT(7)
#define DACL_REFV_MASK			BIT(7)
#define DACL_CLK_OFF			(0 << 6)
#define DACL_CLK_ON			BIT(6)
#define DACL_CLK_MASK			BIT(6)
#define DACL_OFF			(0 << 5)
#define DACL_ON				BIT(5)
#define DACL_MASK			BIT(5)
#define DACL_INIT_OFF			(0 << 4)
#define DACL_INIT_ON			BIT(4)
#define DACL_INIT_MASK			BIT(4)
#define DACR_REFV_OFF			(0 << 3)
#define DACR_REFV_ON			BIT(3)
#define DACR_REFV_MASK			BIT(3)
#define DACR_CLK_OFF			(0 << 2)
#define DACR_CLK_ON			BIT(2)
#define DACR_CLK_MASK			BIT(2)
#define DACR_OFF			(0 << 1)
#define DACR_ON				BIT(1)
#define DACR_MASK			BIT(1)
#define DACR_INIT_OFF			(0 << 0)
#define DACR_INIT_ON			BIT(0)
#define DACR_INIT_MASK			BIT(0)

/* REG25: HPMIX_CTRL*/
#define HPMIXL_DIS			(0 << 6)
#define HPMIXL_EN			BIT(6)
#define HPMIXL_MASK			BIT(6)
#define HPMIXL_INIT_DIS			(0 << 5)
#define HPMIXL_INIT_EN			BIT(5)
#define HPMIXL_INIT_MASK		BIT(5)
#define HPMIXL_INIT2_DIS		(0 << 4)
#define HPMIXL_INIT2_EN			BIT(4)
#define HPMIXL_INIT2_MASK		BIT(4)
#define HPMIXR_DIS			(0 << 2)
#define HPMIXR_EN			BIT(2)
#define HPMIXR_MASK			BIT(2)
#define HPMIXR_INIT_DIS			(0 << 1)
#define HPMIXR_INIT_EN			BIT(1)
#define HPMIXR_INIT_MASK		BIT(1)
#define HPMIXR_INIT2_DIS		(0 << 0)
#define HPMIXR_INIT2_EN			BIT(0)
#define HPMIXR_INIT2_MASK		BIT(0)

/* REG26: DAC_SELECT */
#define DACL_SELECT			BIT(4)
#define DACL_SELECT_MASK		BIT(4)
#define DACL_DESELECT			(0 << 4)
#define DACR_SELECT			BIT(0)
#define DACR_SELECT_MASK		BIT(0)
#define DACR_DESELECT			(0 << 0)

/* REG27: HPOUT_CTRL */
#define HPOUTL_DIS			(0 << 7)
#define HPOUTL_EN			BIT(7)
#define HPOUTL_MASK			BIT(7)
#define HPOUTL_INIT_DIS			(0 << 6)
#define HPOUTL_INIT_EN			BIT(6)
#define HPOUTL_INIT_MASK		BIT(6)
#define HPOUTL_MUTE			(0 << 5)
#define HPOUTL_UNMUTE			BIT(5)
#define HPOUTL_MUTE_MASK		BIT(5)
#define HPOUTR_DIS			(0 << 4)
#define HPOUTR_EN			BIT(4)
#define HPOUTR_MASK			BIT(4)
#define HPOUTR_INIT_DIS			(0 << 3)
#define HPOUTR_INIT_EN			BIT(3)
#define HPOUTR_INIT_MASK		BIT(3)
#define HPOUTR_MUTE			(0 << 2)
#define HPOUTR_UNMUTE			BIT(2)
#define HPOUTR_MUTE_MASK		BIT(2)

/* REG28: HPOUTL_GAIN_CTRL */
#define HPOUTL_GAIN_MASK		(0X1f << 0)

/* REG29: HPOUTR_GAIN_CTRL */
#define HPOUTR_GAIN_MASK		(0X1f << 0)

/* REG2a: HPOUT_POP_CTRL */
#define HPOUTR_POP_XCHARGE		BIT(4)
#define HPOUTR_POP_WORK			(2 << 4)
#define HPOUTR_POP_MASK			(3 << 4)
#define HPOUTL_POP_XCHARGE		BIT(0)
#define HPOUTL_POP_WORK			(2 << 0)
#define HPOUTL_POP_MASK			(3 << 0)

#define RK3228_HIFI			(0)

struct rk3228_reg_msk_val {
	unsigned int reg;
	unsigned int msk;
	unsigned int val;
};

#endif
