/* SPDX-License-Identifier: GPL-2.0-or-later */
/* Copyright (C) 2021 ROHM Semiconductors */

#ifndef __LINUX_MFD_BD957X_H__
#define __LINUX_MFD_BD957X_H__

enum {
	BD957X_VD50,
	BD957X_VD18,
	BD957X_VDDDR,
	BD957X_VD10,
	BD957X_VOUTL1,
	BD957X_VOUTS1,
};

/*
 * The BD9576 has own IRQ 'blocks' for:
 *  - I2C/thermal,
 *  - Over voltage protection
 *  - Short-circuit protection
 *  - Over current protection
 *  - Over voltage detection
 *  - Under voltage detection
 *  - Under voltage protection
 *  - 'system interrupt'.
 *
 * Each of the blocks have a status register giving more accurate IRQ source
 * information - for example which of the regulators have over-voltage.
 *
 * On top of this, there is "main IRQ" status register where each bit indicates
 * which of sub-blocks have active IRQs. Fine. That would fit regmap-irq main
 * status handling. Except that:
 *  - Only some sub-IRQs can be masked.
 *  - The IRQ informs us about fault-condition, not when fault state changes.
 *    The IRQ line it is kept asserted until the detected condition is acked
 *    AND cleared in HW. This is annoying for IRQs like the one informing high
 *    temperature because if IRQ is not disabled it keeps the CPU in IRQ
 *    handling loop.
 *
 * For now we do just use the main-IRQ register as source for our IRQ
 * information and bind the regmap-irq to this. We leave fine-grained sub-IRQ
 * register handling to handlers in sub-devices. The regulator driver shall
 * read which regulators are source for problem - or if the detected error is
 * regulator temperature error. The sub-drivers do also handle masking of "sub-
 * IRQs" if this is supported/needed.
 *
 * To overcome the problem with HW keeping IRQ asserted we do call
 * disable_irq_nosync() from sub-device handler and add a delayed work to
 * re-enable IRQ roughly 1 second later. This should keep our CPU out of
 * busy-loop.
 */
#define IRQS_SILENT_MS			1000

enum {
	BD9576_INT_THERM,
	BD9576_INT_OVP,
	BD9576_INT_SCP,
	BD9576_INT_OCP,
	BD9576_INT_OVD,
	BD9576_INT_UVD,
	BD9576_INT_UVP,
	BD9576_INT_SYS,
};

#define BD957X_REG_SMRB_ASSERT		0x15
#define BD957X_REG_PMIC_INTERNAL_STAT	0x20
#define BD957X_REG_INT_THERM_STAT	0x23
#define BD957X_REG_INT_THERM_MASK	0x24
#define BD957X_REG_INT_OVP_STAT		0x25
#define BD957X_REG_INT_SCP_STAT		0x26
#define BD957X_REG_INT_OCP_STAT		0x27
#define BD957X_REG_INT_OVD_STAT		0x28
#define BD957X_REG_INT_UVD_STAT		0x29
#define BD957X_REG_INT_UVP_STAT		0x2a
#define BD957X_REG_INT_SYS_STAT		0x2b
#define BD957X_REG_INT_SYS_MASK		0x2c
#define BD957X_REG_INT_MAIN_STAT	0x30
#define BD957X_REG_INT_MAIN_MASK	0x31

#define UVD_IRQ_VALID_MASK		0x6F
#define OVD_IRQ_VALID_MASK		0x2F

#define BD957X_MASK_INT_MAIN_THERM	BIT(0)
#define BD957X_MASK_INT_MAIN_OVP	BIT(1)
#define BD957X_MASK_INT_MAIN_SCP	BIT(2)
#define BD957X_MASK_INT_MAIN_OCP	BIT(3)
#define BD957X_MASK_INT_MAIN_OVD	BIT(4)
#define BD957X_MASK_INT_MAIN_UVD	BIT(5)
#define BD957X_MASK_INT_MAIN_UVP	BIT(6)
#define BD957X_MASK_INT_MAIN_SYS	BIT(7)
#define BD957X_MASK_INT_ALL		0xff

#define BD957X_REG_WDT_CONF		0x16

#define BD957X_REG_POW_TRIGGER1		0x41
#define BD957X_REG_POW_TRIGGER2		0x42
#define BD957X_REG_POW_TRIGGER3		0x43
#define BD957X_REG_POW_TRIGGER4		0x44
#define BD957X_REG_POW_TRIGGERL1	0x45
#define BD957X_REG_POW_TRIGGERS1	0x46

#define BD957X_REGULATOR_EN_MASK	0xff
#define BD957X_REGULATOR_DIS_VAL	0xff

#define BD957X_VSEL_REG_MASK		0xff

#define BD957X_MASK_VOUT1_TUNE		0x87
#define BD957X_MASK_VOUT2_TUNE		0x87
#define BD957X_MASK_VOUT3_TUNE		0x1f
#define BD957X_MASK_VOUT4_TUNE		0x1f
#define BD957X_MASK_VOUTL1_TUNE		0x87

#define BD957X_REG_VOUT1_TUNE		0x50
#define BD957X_REG_VOUT2_TUNE		0x53
#define BD957X_REG_VOUT3_TUNE		0x56
#define BD957X_REG_VOUT4_TUNE		0x59
#define BD957X_REG_VOUTL1_TUNE		0x5c

#define BD9576_REG_VOUT1_OVD		0x51
#define BD9576_REG_VOUT1_UVD		0x52
#define BD9576_REG_VOUT2_OVD		0x54
#define BD9576_REG_VOUT2_UVD		0x55
#define BD9576_REG_VOUT3_OVD		0x57
#define BD9576_REG_VOUT3_UVD		0x58
#define BD9576_REG_VOUT4_OVD		0x5a
#define BD9576_REG_VOUT4_UVD		0x5b
#define BD9576_REG_VOUTL1_OVD		0x5d
#define BD9576_REG_VOUTL1_UVD		0x5e

#define BD9576_MASK_XVD			0x7f

#define BD9576_REG_VOUT1S_OCW		0x5f
#define BD9576_REG_VOUT1S_OCP		0x60

#define BD9576_MASK_VOUT1S_OCW		0x3f
#define BD9576_MASK_VOUT1S_OCP		0x3f

#define BD957X_MAX_REGISTER		0x61

#endif
