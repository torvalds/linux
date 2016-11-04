/*
 * Copyright Intel Corporation (C) 2014-2016. All Rights Reserved
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
 * You should have received a copy of the GNU General Public License along with
 * this program.  If not, see <http://www.gnu.org/licenses/>.
 *
 * Declarations for Altera Arria10 MAX5 System Resource Chip
 *
 * Adapted from DA9052
 */

#ifndef __MFD_ALTERA_A10SR_H
#define __MFD_ALTERA_A10SR_H

#include <linux/completion.h>
#include <linux/list.h>
#include <linux/mfd/core.h>
#include <linux/regmap.h>
#include <linux/slab.h>

/* Write registers are always on even addresses */
#define  WRITE_REG_MASK              0xFE
/* Odd registers are always on odd addresses */
#define  READ_REG_MASK               0x01

#define ALTR_A10SR_BITS_PER_REGISTER  8
/*
 * To find the correct register, we divide the input GPIO by
 * the number of GPIO in each register. We then need to multiply
 * by 2 because the reads are at odd addresses.
 */
#define ALTR_A10SR_REG_OFFSET(X)     (((X) / ALTR_A10SR_BITS_PER_REGISTER) << 1)
#define ALTR_A10SR_REG_BIT(X)        ((X) % ALTR_A10SR_BITS_PER_REGISTER)
#define ALTR_A10SR_REG_BIT_CHG(X, Y) ((X) << ALTR_A10SR_REG_BIT(Y))
#define ALTR_A10SR_REG_BIT_MASK(X)   (1 << ALTR_A10SR_REG_BIT(X))

/* Arria10 System Controller Register Defines */
#define ALTR_A10SR_NOP                0x00    /* No Change */
#define ALTR_A10SR_VERSION_READ       0x00    /* MAX5 Version Read */

#define ALTR_A10SR_LED_REG            0x02    /* LED - Upper 4 bits */
/* LED register Bit Definitions */
#define ALTR_A10SR_LED_VALID_SHIFT        4       /* LED - Upper 4 bits valid */
#define ALTR_A10SR_OUT_VALID_RANGE_LO     ALTR_A10SR_LED_VALID_SHIFT
#define ALTR_A10SR_OUT_VALID_RANGE_HI     7

#define ALTR_A10SR_PBDSW_REG          0x04    /* PB & DIP SW - Input only */
#define ALTR_A10SR_PBDSW_IRQ_REG      0x06    /* PB & DIP SW Flag Clear */
/* Pushbutton & DIP Switch Bit Definitions */
#define ALTR_A10SR_IN_VALID_RANGE_LO      8
#define ALTR_A10SR_IN_VALID_RANGE_HI      15

#define ALTR_A10SR_PWR_GOOD1_REG      0x08    /* Power Good1 Read */
#define ALTR_A10SR_PWR_GOOD2_REG      0x0A    /* Power Good2 Read */
#define ALTR_A10SR_PWR_GOOD3_REG      0x0C    /* Power Good3 Read */
#define ALTR_A10SR_FMCAB_REG          0x0E    /* FMCA/B & PCIe Pwr Enable */
#define ALTR_A10SR_HPS_RST_REG        0x10    /* HPS Reset */
#define ALTR_A10SR_USB_QSPI_REG       0x12    /* USB, BQSPI, FILE Reset */
#define ALTR_A10SR_SFPA_REG           0x14    /* SFPA Control Reg */
#define ALTR_A10SR_SFPB_REG           0x16    /* SFPB Control Reg */
#define ALTR_A10SR_I2C_M_REG          0x18    /* I2C Master Select */
#define ALTR_A10SR_WARM_RST_REG       0x1A    /* HPS Warm Reset */
#define ALTR_A10SR_WR_KEY_REG         0x1C    /* HPS Warm Reset Key */
#define ALTR_A10SR_PMBUS_REG          0x1E    /* HPS PM Bus */

/**
 * struct altr_a10sr - Altera Max5 MFD device private data structure
 * @dev:  : this device
 * @regmap: the regmap assigned to the parent device.
 */
struct altr_a10sr {
	struct device *dev;
	struct regmap *regmap;
};

#endif /* __MFD_ALTERA_A10SR_H */
