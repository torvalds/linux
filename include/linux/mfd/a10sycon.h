/*
 *  Copyright (C) 2014 Altera Corporation
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
 * Declarations for Altera MAX5 Arria10 System Control
 * Adapted from DA9052
 */

#ifndef __MFD_A10SYCON_H
#define __MFD_A10SYCON_H

#include <linux/interrupt.h>
#include <linux/regmap.h>
#include <linux/slab.h>
#include <linux/completion.h>
#include <linux/list.h>
#include <linux/mfd/core.h>

/* Write registers are always on even addresses */
#define  WRITE_REG_MASK              0xFE
/* Odd registers are always on odd addresses */
#define  READ_REG_MASK               0x01

#define A10SYCON_BITS_PER_REGISTER   8
/*
 * To find the correct register, we divide the input GPIO by
 * the number of GPIO in each register. We then need to multiply
 * by 2 because the reads are at odd addresses.
 */
#define A10SYCON_REG_OFFSET(X)     (((X) / A10SYCON_BITS_PER_REGISTER) << 1)
#define A10SYCON_REG_BIT(X)        ((X) % A10SYCON_BITS_PER_REGISTER)
#define A10SYCON_REG_BIT_CHG(X, Y) ((X) << A10SYCON_REG_BIT(Y))
#define A10SYCON_REG_BIT_MASK(X)   (1 << A10SYCON_REG_BIT(X))

/* Arria10 System Controller Register Defines */
#define A10SYCON_NOP               0x00    /* No Change */
#define AA10SYCON_DUMMY_READ       0x01    /* Dummy Read */

#define A10SYCON_LED_WR_REG        0x02    /* LED - Upper 4 bits */
#define A10SYCON_LED_RD_REG        0x03    /* LED - Upper 4 bits */
/* LED register Bit Definitions */
#define A10SC_LED_MASK             0xF0    /* LED - Mask Upper 4 bits */
#define A10SC_LED_VALID_SHIFT      4       /* LED - Upper 4 bits valid */
#define A10SC_LED_VALID_NUM        4       /* LED - # valid LEDs */
#define A10SC_OUT_VALID_RANGE_LO   A10SC_LED_VALID_SHIFT
#define A10SC_OUT_VALID_RANGE_HI   7

#define A10SYCON_PBDSW_RD_REG      0x05    /* PB & DIP SW - Input only */
#define A10SYCON_PBDSW_CLR_REG     0x06    /* PB & DIP SW Flag Clear */
#define A10SYCON_PBDSW_IRQ_RD_REG  0x07    /* PB & DIP SW Flag Read */
/* Pushbutton & DIP Switch Bit Definitions */
#define A10SC_PB_DWS_PB_MASK       0xF0    /* PB - Upper 4 bits */
#define A10SC_PB_DWS_DWS_MASK      0x0F    /* DWS - Lower 4 bits */
#define A10SC_IRQ_PB_3_SHIFT       7       /* Pushbutton 4 */
#define A10SC_IRQ_PB_2_SHIFT       6       /* Pushbutton 3 */
#define A10SC_IRQ_PB_1_SHIFT       5       /* Pushbutton 2 */
#define A10SC_IRQ_PB_0_SHIFT       4       /* Pushbutton 1 */
#define A10SC_IRQ_DSW_3_SHIFT      3       /* DIP SW 3 */
#define A10SC_IRQ_DSW_2_SHIFT      2       /* DIP SW 2 */
#define A10SC_IRQ_DSW_1_SHIFT      1       /* DIP SW 1 */
#define A10SC_IRQ_DSW_O_SHIFT      0       /* DIP SW 0 */
#define A10SC_IN_VALID_RANGE_LO    8
#define A10SC_IN_VALID_RANGE_HI    15

#define A10SYCON_PWR_GOOD1_RD_REG  0x09	   /* Power Good1 Read */
/* Power Good #1 Register Bit Definitions */
#define A10SC_PG1_OP_FLAG_SHIFT    7       /* Power On Complete */
#define A10SC_PG1_1V8_SHIFT        6       /* 1.8V Power Good */
#define A10SC_PG1_2V5_SHIFT        5       /* 2.5V Power Good */
#define A10SC_PG1_3V3_SHIFT        4       /* 3.3V Power Good */
#define A10SC_PG1_5V0_SHIFT        3       /* 5.0V Power Good */
#define A10SC_PG1_0V9_SHIFT        2       /* 0.9V Power Good */
#define A10SC_PG1_0V95_SHIFT       1       /* 0.95V Power Good */
#define A10SC_PG1_1V0_SHIFT        0       /* 1.0V Power Good */

#define A10SYCON_PWR_GOOD2_RD_REG  0x0B	   /* Power Good2 Read */
/* Power Good #2 Register Bit Definitions */
#define A10SC_PG2_HPS_SHIFT        7       /* HPS Power Good */
#define A10SC_PG2_HL_HPS_SHIFT     6       /* HILOHPS_VDD Power Good */
#define A10SC_PG2_HL_VDD_SHIFT     5       /* HILO VDD Power Good */
#define A10SC_PG2_HL_VDDQ_SHIFT    4       /* HILO VDDQ Power Good */
#define A10SC_PG2_FMCAVADJ_SHIFT   3       /* FMCA VADJ Power Good */
#define A10SC_PG2_FMCBVADJ_SHIFT   2       /* FMCB VADJ Power Good */
#define A10SC_PG2_FAC2MP_SHIFT     1       /* FAC2MP Power Good */
#define A10SC_PG2_FBC2MP_SHIFT     0       /* FBC2MP Power Good */

#define A10SYCON_PWR_GOOD3_RD_REG  0x0D	   /* Power Good3 Read */
/* Power Good #3 Register Bit Definitions */
#define A10SC_PG3_FAM2C_SHIFT      7       /* FAM2C Power Good */
#define A10SC_PG3_10V_FAIL_SHIFT   6       /* 10V Fail n */
#define A10SC_PG3_BF_PR_SHIFT      5       /* BF Present n */
#define A10SC_PG3_FILE_PR_SHIFT    4       /* File Present n */
#define A10SC_PG3_FMCA_PR_SHIFT    3       /* FMCA Present n */
#define A10SC_PG3_FMCB_PR_SHIFT    2       /* FMCB Present n */
#define A10SC_PG3_PCIE_PR_SHIFT    1       /* PCIE Present n */
#define A10SC_PG3_PCIE_WAKE_SHIFT  0       /* PCIe Wake N */

#define A10SYCON_FMCAB_WR_REG	   0x0E    /* FMCA/B & PCIe Pwr Enable */
#define A10SYCON_FMCAB_RD_REG	   0x0F	   /* FMCA/B & PCIe Pwr Enable */
/* FMCA/B & PCIe Power Bit Definitions */
#define A10SC_PCIE_EN_SHIFT        7       /* PCIe Pwr Enable */
#define A10SC_PCIE_AUXEN_SHIFT     6       /* PCIe Aux Pwr Enable */
#define A10SC_FMCA_EN_SHIFT        5       /* FMCA Pwr Enable */
#define A10SC_FMCA_AUXEN_SHIFT     4       /* FMCA Aux Pwr Enable */
#define A10SC_FMCB_EN_SHIFT        3       /* FMCB Pwr Enable */
#define A10SC_FMCB_AUXEN_SHIFT     2       /* FMCB Aux Pwr Enable */

#define A10SYCON_HPS_RST_WR_REG	   0x10	   /* HPS Reset */
#define A10SYCON_HPS_RST_RD_REG	   0x11	   /* HPS Reset */
/* HPS Reset Bit Definitions */
#define A10SC_HPS_UARTA_RST_N      BIT(7)  /* UARTA Reset n */
#define A10SC_HPS_WARM_RST_N       BIT(6)  /* WARM Reset n */
#define A10SC_HPS_WARM_RST1_N      BIT(5)  /* WARM Reset1 n */
#define A10SC_HPS_COLD_RST_N       BIT(4)  /* COLD Reset n */
#define A10SC_HPS_NPOR             BIT(3)  /* N Power On Reset */
#define A10SC_HPS_NRST             BIT(2)  /* N Reset */
#define A10SC_ENET_HPS_RST_N       BIT(1)  /* Ethernet Reset n */
#define A10SC_ENET_HPS_INT_N       BIT(0)  /* Ethernet IRQ n */

#define A10SYCON_USB_QSPI_WR_REG   0x12	   /* USB, BQSPI, FILE Reset */
#define A10SYCON_USB_QSPI_RD_REG   0x13	   /* USB, BQSPI, FILE Reset */
/* USB/QSPI/FILE Reset Bit Definitions */
#define A10SC_USB_RST              BIT(7)  /* USB Reset */
#define A10SC_BQSPI_RST_N          BIT(6)  /* BQSPI Reset n */
#define A10SC_FILE_RST_N           BIT(5)  /* FILE Reset n */
#define A10SC_PCIE_PERST_N         BIT(4)  /* PCIe PE Reset n */

#define A10SYCON_SFPA_WR_REG       0x14	   /* SFPA Control Reg */
#define A10SYCON_SFPA_RD_REG       0x15	   /* SFPA Control Reg */
/* SFPA Bit Definitions */
#define A10SC_SFPA_TXDISABLE       BIT(7)  /* SFPA TX Disable */
#define A10SC_SFPA_RATESEL10       0x60    /* SFPA_Rate Select [1:0] */
#define A10SC_SFPA_LOS             BIT(4)  /* SFPA LOS */
#define A10SC_SFPA_FAULT           BIT(3)  /* SFPA Fault */

#define A10SYCON_SFPB_WR_REG       0x16    /* SFPA Control Reg */
#define A10SYCON_SFPB_RD_REG       0x17	   /* SFPA Control Reg */
/* SFPB Bit Definitions */
#define A10SC_SFPB_TXDISABLE       BIT(7)  /* SFPB TX Disable */
#define A10SC_SFPB_RATESEL10       0x60    /* SFPB_Rate Select [1:0] */
#define A10SC_SFPB_LOS             BIT(4)  /* SFPB LOS */
#define A10SC_SFPB_FAULT           BIT(3)  /* SFPB Fault */

#define A10SYCON_I2C_M_RD_REG      0x19	   /* I2C Master Select */

#define A10SYCON_WARM_RST_WR_REG   0x1A	   /* HPS Warm Reset */
#define A10SYCON_WARM_RST_RD_REG   0x1B	   /* HPS Warm Reset */

#define A10SYCON_WR_KEY_WR_REG     0x1C	   /* HPS Warm Reset Key */
#define A10SYCON_WR_KEY_RD_REG     0x1D	   /* HPS Warm Reset Key */

struct a10sycon_pdata;

struct a10sycon {
	struct device *dev;
	struct regmap *regmap;

	struct completion done;

	int irq_base;
	struct regmap_irq_chip_data *irq_data;

	int chip_irq;
};

/* Device I/O API */
static inline int a10sycon_reg_read(struct a10sycon *a10sycon,
				    unsigned char reg)
{
	int val, ret;

	ret = regmap_read(a10sycon->regmap, (reg | READ_REG_MASK), &val);
	if (ret < 0)
		return ret;

	return val;
}

static inline int a10sycon_reg_write(struct a10sycon *a10sycon,
				     unsigned char reg, unsigned char val)
{
	int ret;

	ret = regmap_write(a10sycon->regmap, (reg & WRITE_REG_MASK), val);
	if (ret < 0)
		return ret;

	return ret;
}

static inline int a10sycon_group_read(struct a10sycon *a10sycon,
				      unsigned char reg, unsigned reg_cnt,
				      unsigned char *val)
{
	return regmap_bulk_read(a10sycon->regmap, reg, val, reg_cnt);
}

static inline int a10sycon_group_write(struct a10sycon *a10sycon,
				       unsigned char reg, unsigned reg_cnt,
				       unsigned char *val)
{
	return regmap_bulk_write(a10sycon->regmap, reg, val, reg_cnt);
}

static inline int a10sycon_reg_update(struct a10sycon *a10sycon,
				      unsigned char reg,
				      unsigned char bit_mask,
				      unsigned char reg_val)
{
	int rval, ret;

	/*
	 * We can't use the standard regmap_update_bits function because
	 * the read register has a different address than the write register.
	 * Therefore, just do a read, modify, write operation here.
	 */
	ret = regmap_read(a10sycon->regmap, (reg | READ_REG_MASK), &rval);
	if (ret < 0)
		return ret;

	rval = ((rval & ~bit_mask) | (reg_val & bit_mask));

	ret = regmap_write(a10sycon->regmap, (reg & WRITE_REG_MASK), rval);
	if (ret < 0)
		return ret;

	return ret;
}

int a10sycon_device_init(struct a10sycon *a10sycon);
void a10sycon_device_exit(struct a10sycon *a10sycon);

extern struct regmap_config a10sycon_regmap_config;

int a10sycon_irq_init(struct a10sycon *a10sycon);
int a10sycon_irq_exit(struct a10sycon *a10sycon);
int a10sycon_map_irq(struct a10sycon *a10sycon, int irq);
int a10sycon_request_irq(struct a10sycon *a10sycon, int irq, const char *name,
			 irq_handler_t handler, void *data);
void a10sycon_free_irq(struct a10sycon *a10sycon, int irq, void *data);

int a10sycon_enable_irq(struct a10sycon *a10sycon, int irq);
int a10sycon_disable_irq(struct a10sycon *a10sycon, int irq);
int a10sycon_disable_irq_nosync(struct a10sycon *a10sycon, int irq);

#endif /* __MFD_A10SYCON_H */
