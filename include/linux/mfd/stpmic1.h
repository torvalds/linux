/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Copyright (C) STMicroelectronics 2018 - All Rights Reserved
 * Author: Philippe Peurichard <philippe.peurichard@st.com>,
 * Pascal Paillet <p.paillet@st.com> for STMicroelectronics.
 */

#ifndef __LINUX_MFD_STPMIC1_H
#define __LINUX_MFD_STPMIC1_H

#define TURN_ON_SR		0x1
#define TURN_OFF_SR		0x2
#define ICC_LDO_TURN_OFF_SR	0x3
#define ICC_BUCK_TURN_OFF_SR	0x4
#define RREQ_STATE_SR		0x5
#define VERSION_SR		0x6

#define SWOFF_PWRCTRL_CR	0x10
#define PADS_PULL_CR		0x11
#define BUCKS_PD_CR		0x12
#define LDO14_PD_CR		0x13
#define LDO56_VREF_PD_CR	0x14
#define VBUS_DET_VIN_CR		0x15
#define PKEY_TURNOFF_CR		0x16
#define BUCKS_MASK_RANK_CR	0x17
#define BUCKS_MASK_RESET_CR	0x18
#define LDOS_MASK_RANK_CR	0x19
#define LDOS_MASK_RESET_CR	0x1A
#define WCHDG_CR		0x1B
#define WCHDG_TIMER_CR		0x1C
#define BUCKS_ICCTO_CR		0x1D
#define LDOS_ICCTO_CR		0x1E

#define BUCK1_ACTIVE_CR		0x20
#define BUCK2_ACTIVE_CR		0x21
#define BUCK3_ACTIVE_CR		0x22
#define BUCK4_ACTIVE_CR		0x23
#define VREF_DDR_ACTIVE_CR	0x24
#define LDO1_ACTIVE_CR		0x25
#define LDO2_ACTIVE_CR		0x26
#define LDO3_ACTIVE_CR		0x27
#define LDO4_ACTIVE_CR		0x28
#define LDO5_ACTIVE_CR		0x29
#define LDO6_ACTIVE_CR		0x2A

#define BUCK1_STDBY_CR		0x30
#define BUCK2_STDBY_CR		0x31
#define BUCK3_STDBY_CR		0x32
#define BUCK4_STDBY_CR		0x33
#define VREF_DDR_STDBY_CR	0x34
#define LDO1_STDBY_CR		0x35
#define LDO2_STDBY_CR		0x36
#define LDO3_STDBY_CR		0x37
#define LDO4_STDBY_CR		0x38
#define LDO5_STDBY_CR		0x39
#define LDO6_STDBY_CR		0x3A

#define BST_SW_CR		0x40

#define INT_PENDING_R1		0x50
#define INT_PENDING_R2		0x51
#define INT_PENDING_R3		0x52
#define INT_PENDING_R4		0x53

#define INT_DBG_LATCH_R1	0x60
#define INT_DBG_LATCH_R2	0x61
#define INT_DBG_LATCH_R3	0x62
#define INT_DBG_LATCH_R4	0x63

#define INT_CLEAR_R1		0x70
#define INT_CLEAR_R2		0x71
#define INT_CLEAR_R3		0x72
#define INT_CLEAR_R4		0x73

#define INT_MASK_R1		0x80
#define INT_MASK_R2		0x81
#define INT_MASK_R3		0x82
#define INT_MASK_R4		0x83

#define INT_SET_MASK_R1		0x90
#define INT_SET_MASK_R2		0x91
#define INT_SET_MASK_R3		0x92
#define INT_SET_MASK_R4		0x93

#define INT_CLEAR_MASK_R1	0xA0
#define INT_CLEAR_MASK_R2	0xA1
#define INT_CLEAR_MASK_R3	0xA2
#define INT_CLEAR_MASK_R4	0xA3

#define INT_SRC_R1		0xB0
#define INT_SRC_R2		0xB1
#define INT_SRC_R3		0xB2
#define INT_SRC_R4		0xB3

#define PMIC_MAX_REGISTER_ADDRESS INT_SRC_R4

#define STPMIC1_PMIC_NUM_IRQ_REGS 4

#define TURN_OFF_SR_ICC_EVENT	0x08

#define LDO_VOLTAGE_MASK		GENMASK(6, 2)
#define BUCK_VOLTAGE_MASK		GENMASK(7, 2)
#define LDO_BUCK_VOLTAGE_SHIFT		2

#define LDO_ENABLE_MASK			BIT(0)
#define BUCK_ENABLE_MASK		BIT(0)

#define BUCK_HPLP_ENABLE_MASK		BIT(1)
#define BUCK_HPLP_SHIFT			1

#define STDBY_ENABLE_MASK  BIT(0)

#define BUCKS_PD_CR_REG_MASK	GENMASK(7, 0)
#define BUCK_MASK_RANK_REGISTER_MASK	GENMASK(3, 0)
#define BUCK_MASK_RESET_REGISTER_MASK	GENMASK(3, 0)
#define LDO1234_PULL_DOWN_REGISTER_MASK	GENMASK(7, 0)
#define LDO56_VREF_PD_CR_REG_MASK	GENMASK(5, 0)
#define LDO_MASK_RANK_REGISTER_MASK	GENMASK(5, 0)
#define LDO_MASK_RESET_REGISTER_MASK	GENMASK(5, 0)

#define BUCK1_PULL_DOWN_REG		BUCKS_PD_CR
#define BUCK1_PULL_DOWN_MASK		BIT(0)
#define BUCK2_PULL_DOWN_REG		BUCKS_PD_CR
#define BUCK2_PULL_DOWN_MASK		BIT(2)
#define BUCK3_PULL_DOWN_REG		BUCKS_PD_CR
#define BUCK3_PULL_DOWN_MASK		BIT(4)
#define BUCK4_PULL_DOWN_REG		BUCKS_PD_CR
#define BUCK4_PULL_DOWN_MASK		BIT(6)

#define LDO1_PULL_DOWN_REG		LDO14_PD_CR
#define LDO1_PULL_DOWN_MASK		BIT(0)
#define LDO2_PULL_DOWN_REG		LDO14_PD_CR
#define LDO2_PULL_DOWN_MASK		BIT(2)
#define LDO3_PULL_DOWN_REG		LDO14_PD_CR
#define LDO3_PULL_DOWN_MASK		BIT(4)
#define LDO4_PULL_DOWN_REG		LDO14_PD_CR
#define LDO4_PULL_DOWN_MASK		BIT(6)
#define LDO5_PULL_DOWN_REG		LDO56_VREF_PD_CR
#define LDO5_PULL_DOWN_MASK		BIT(0)
#define LDO6_PULL_DOWN_REG		LDO56_VREF_PD_CR
#define LDO6_PULL_DOWN_MASK		BIT(2)
#define VREF_DDR_PULL_DOWN_REG		LDO56_VREF_PD_CR
#define VREF_DDR_PULL_DOWN_MASK		BIT(4)

#define BUCKS_ICCTO_CR_REG_MASK	GENMASK(6, 0)
#define LDOS_ICCTO_CR_REG_MASK	GENMASK(5, 0)

#define LDO_BYPASS_MASK			BIT(7)

/* Main PMIC Control Register
 * SWOFF_PWRCTRL_CR
 * Address : 0x10
 */
#define ICC_EVENT_ENABLED		BIT(4)
#define PWRCTRL_POLARITY_HIGH		BIT(3)
#define PWRCTRL_PIN_VALID		BIT(2)
#define RESTART_REQUEST_ENABLED		BIT(1)
#define SOFTWARE_SWITCH_OFF_ENABLED	BIT(0)

/* Main PMIC PADS Control Register
 * PADS_PULL_CR
 * Address : 0x11
 */
#define WAKEUP_DETECTOR_DISABLED	BIT(4)
#define PWRCTRL_PD_ACTIVE		BIT(3)
#define PWRCTRL_PU_ACTIVE		BIT(2)
#define WAKEUP_PD_ACTIVE		BIT(1)
#define PONKEY_PU_INACTIVE		BIT(0)

/* Main PMIC VINLOW Control Register
 * VBUS_DET_VIN_CRC DMSC
 * Address : 0x15
 */
#define SWIN_DETECTOR_ENABLED		BIT(7)
#define SWOUT_DETECTOR_ENABLED		BIT(6)
#define VINLOW_ENABLED			BIT(0)
#define VINLOW_CTRL_REG_MASK		GENMASK(7, 0)

/* USB Control Register
 * Address : 0x40
 */
#define BOOST_OVP_DISABLED		BIT(7)
#define VBUS_OTG_DETECTION_DISABLED	BIT(6)
#define SW_OUT_DISCHARGE		BIT(5)
#define VBUS_OTG_DISCHARGE		BIT(4)
#define OCP_LIMIT_HIGH			BIT(3)
#define SWIN_SWOUT_ENABLED		BIT(2)
#define USBSW_OTG_SWITCH_ENABLED	BIT(1)
#define BOOST_ENABLED			BIT(0)

/* PKEY_TURNOFF_CR
 * Address : 0x16
 */
#define PONKEY_PWR_OFF			BIT(7)
#define PONKEY_CC_FLAG_CLEAR		BIT(6)
#define PONKEY_TURNOFF_TIMER_MASK	GENMASK(3, 0)
#define PONKEY_TURNOFF_MASK		GENMASK(7, 0)

/*
 * struct stpmic1 - stpmic1 master device for sub-drivers
 * @dev: master device of the chip (can be used to access platform data)
 * @irq: main IRQ number
 * @regmap_irq_chip_data: irq chip data
 */
struct stpmic1 {
	struct device *dev;
	struct regmap *regmap;
	int irq;
	struct regmap_irq_chip_data *irq_data;
};

#endif /*  __LINUX_MFD_STPMIC1_H */
