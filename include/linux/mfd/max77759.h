/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright 2020 Google Inc.
 * Copyright 2025 Linaro Ltd.
 *
 * Maxim MAX77759 core driver
 */

#ifndef __LINUX_MFD_MAX77759_H
#define __LINUX_MFD_MAX77759_H

#include <linux/completion.h>
#include <linux/mutex.h>
#include <linux/regmap.h>

#define MAX77759_PMIC_REG_PMIC_ID               0x00
#define MAX77759_PMIC_REG_PMIC_REVISION         0x01
#define MAX77759_PMIC_REG_OTP_REVISION          0x02
#define MAX77759_PMIC_REG_INTSRC                0x22
#define MAX77759_PMIC_REG_INTSRCMASK            0x23
#define   MAX77759_PMIC_REG_INTSRC_MAXQ         BIT(3)
#define   MAX77759_PMIC_REG_INTSRC_TOPSYS       BIT(1)
#define   MAX77759_PMIC_REG_INTSRC_CHGR         BIT(0)
#define MAX77759_PMIC_REG_TOPSYS_INT            0x24
#define MAX77759_PMIC_REG_TOPSYS_INT_MASK       0x26
#define   MAX77759_PMIC_REG_TOPSYS_INT_TSHDN    BIT(6)
#define   MAX77759_PMIC_REG_TOPSYS_INT_SYSOVLO  BIT(5)
#define   MAX77759_PMIC_REG_TOPSYS_INT_SYSUVLO  BIT(4)
#define   MAX77759_PMIC_REG_TOPSYS_INT_FSHIP    BIT(0)
#define MAX77759_PMIC_REG_I2C_CNFG              0x40
#define MAX77759_PMIC_REG_SWRESET               0x50
#define MAX77759_PMIC_REG_CONTROL_FG            0x51

#define MAX77759_MAXQ_REG_UIC_INT1              0x64
#define   MAX77759_MAXQ_REG_UIC_INT1_APCMDRESI  BIT(7)
#define   MAX77759_MAXQ_REG_UIC_INT1_SYSMSGI    BIT(6)
#define   MAX77759_MAXQ_REG_UIC_INT1_GPIO6I     BIT(1)
#define   MAX77759_MAXQ_REG_UIC_INT1_GPIO5I     BIT(0)
#define   MAX77759_MAXQ_REG_UIC_INT1_GPIOxI(offs, en)  (((en) & 1) << (offs))
#define   MAX77759_MAXQ_REG_UIC_INT1_GPIOxI_MASK(offs) \
				MAX77759_MAXQ_REG_UIC_INT1_GPIOxI(offs, ~0)
#define MAX77759_MAXQ_REG_UIC_INT2              0x65
#define MAX77759_MAXQ_REG_UIC_INT3              0x66
#define MAX77759_MAXQ_REG_UIC_INT4              0x67
#define MAX77759_MAXQ_REG_UIC_UIC_STATUS1       0x68
#define MAX77759_MAXQ_REG_UIC_UIC_STATUS2       0x69
#define MAX77759_MAXQ_REG_UIC_UIC_STATUS3       0x6a
#define MAX77759_MAXQ_REG_UIC_UIC_STATUS4       0x6b
#define MAX77759_MAXQ_REG_UIC_UIC_STATUS5       0x6c
#define MAX77759_MAXQ_REG_UIC_UIC_STATUS6       0x6d
#define MAX77759_MAXQ_REG_UIC_UIC_STATUS7       0x6f
#define MAX77759_MAXQ_REG_UIC_UIC_STATUS8       0x6f
#define MAX77759_MAXQ_REG_UIC_INT1_M            0x70
#define MAX77759_MAXQ_REG_UIC_INT2_M            0x71
#define MAX77759_MAXQ_REG_UIC_INT3_M            0x72
#define MAX77759_MAXQ_REG_UIC_INT4_M            0x73
#define MAX77759_MAXQ_REG_AP_DATAOUT0           0x81
#define MAX77759_MAXQ_REG_AP_DATAOUT32          0xa1
#define MAX77759_MAXQ_REG_AP_DATAIN0            0xb1
#define MAX77759_MAXQ_REG_UIC_SWRST             0xe0

#define MAX77759_CHGR_REG_CHG_INT               0xb0
#define MAX77759_CHGR_REG_CHG_INT2              0xb1
#define MAX77759_CHGR_REG_CHG_INT_MASK          0xb2
#define MAX77759_CHGR_REG_CHG_INT2_MASK         0xb3
#define MAX77759_CHGR_REG_CHG_INT_OK            0xb4
#define MAX77759_CHGR_REG_CHG_DETAILS_00        0xb5
#define MAX77759_CHGR_REG_CHG_DETAILS_01        0xb6
#define MAX77759_CHGR_REG_CHG_DETAILS_02        0xb7
#define MAX77759_CHGR_REG_CHG_DETAILS_03        0xb8
#define MAX77759_CHGR_REG_CHG_CNFG_00           0xb9
#define MAX77759_CHGR_REG_CHG_CNFG_01           0xba
#define MAX77759_CHGR_REG_CHG_CNFG_02           0xbb
#define MAX77759_CHGR_REG_CHG_CNFG_03           0xbc
#define MAX77759_CHGR_REG_CHG_CNFG_04           0xbd
#define MAX77759_CHGR_REG_CHG_CNFG_05           0xbe
#define MAX77759_CHGR_REG_CHG_CNFG_06           0xbf
#define MAX77759_CHGR_REG_CHG_CNFG_07           0xc0
#define MAX77759_CHGR_REG_CHG_CNFG_08           0xc1
#define MAX77759_CHGR_REG_CHG_CNFG_09           0xc2
#define MAX77759_CHGR_REG_CHG_CNFG_10           0xc3
#define MAX77759_CHGR_REG_CHG_CNFG_11           0xc4
#define MAX77759_CHGR_REG_CHG_CNFG_12           0xc5
#define MAX77759_CHGR_REG_CHG_CNFG_13           0xc6
#define MAX77759_CHGR_REG_CHG_CNFG_14           0xc7
#define MAX77759_CHGR_REG_CHG_CNFG_15           0xc8
#define MAX77759_CHGR_REG_CHG_CNFG_16           0xc9
#define MAX77759_CHGR_REG_CHG_CNFG_17           0xca
#define MAX77759_CHGR_REG_CHG_CNFG_18           0xcb
#define MAX77759_CHGR_REG_CHG_CNFG_19           0xcc

/* MaxQ opcodes for max77759_maxq_command() */
#define MAX77759_MAXQ_OPCODE_MAXLENGTH (MAX77759_MAXQ_REG_AP_DATAOUT32 - \
					MAX77759_MAXQ_REG_AP_DATAOUT0 + \
					1)

#define MAX77759_MAXQ_OPCODE_GPIO_TRIGGER_READ   0x21
#define MAX77759_MAXQ_OPCODE_GPIO_TRIGGER_WRITE  0x22
#define MAX77759_MAXQ_OPCODE_GPIO_CONTROL_READ   0x23
#define MAX77759_MAXQ_OPCODE_GPIO_CONTROL_WRITE  0x24
#define MAX77759_MAXQ_OPCODE_USER_SPACE_READ     0x81
#define MAX77759_MAXQ_OPCODE_USER_SPACE_WRITE    0x82

/**
 * struct max77759 - core max77759 internal data structure
 *
 * @regmap_top: Regmap for accessing TOP registers
 * @maxq_lock: Lock for serializing access to MaxQ
 * @regmap_maxq: Regmap for accessing MaxQ registers
 * @cmd_done: Used to signal completion of a MaxQ command
 * @regmap_charger: Regmap for accessing charger registers
 *
 * The MAX77759 comprises several sub-blocks, namely TOP, MaxQ, Charger,
 * Fuel Gauge, and TCPCI.
 */
struct max77759 {
	struct regmap *regmap_top;

	/* This protects MaxQ commands - only one can be active */
	struct mutex maxq_lock;
	struct regmap *regmap_maxq;
	struct completion cmd_done;

	struct regmap *regmap_charger;
};

/**
 * struct max77759_maxq_command - structure containing the MaxQ command to
 * send
 *
 * @length: The number of bytes to send.
 * @cmd: The data to send.
 */
struct max77759_maxq_command {
	u8 length;
	u8 cmd[] __counted_by(length);
};

/**
 * struct max77759_maxq_response - structure containing the MaxQ response
 *
 * @length: The number of bytes to receive.
 * @rsp: The data received. Must have at least @length bytes space.
 */
struct max77759_maxq_response {
	u8 length;
	u8 rsp[] __counted_by(length);
};

/**
 * max77759_maxq_command() - issue a MaxQ command and wait for the response
 * and associated data
 *
 * @max77759: The core max77759 device handle.
 * @cmd: The command to be sent.
 * @rsp: Any response data associated with the command will be copied here;
 *     can be %NULL if the command has no response (other than ACK).
 *
 * Return: 0 on success, a negative error number otherwise.
 */
int max77759_maxq_command(struct max77759 *max77759,
			  const struct max77759_maxq_command *cmd,
			  struct max77759_maxq_response *rsp);

#endif /* __LINUX_MFD_MAX77759_H */
