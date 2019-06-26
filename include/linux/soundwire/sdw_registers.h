/* SPDX-License-Identifier: (GPL-2.0 OR BSD-3-Clause) */
/* Copyright(c) 2015-17 Intel Corporation. */

#ifndef __SDW_REGISTERS_H
#define __SDW_REGISTERS_H

/*
 * typically we define register and shifts but if one observes carefully,
 * the shift can be generated from MASKS using few bit primitaives like ffs
 * etc, so we use that and avoid defining shifts
 */
#define SDW_REG_SHIFT(n)			(ffs(n) - 1)

/*
 * SDW registers as defined by MIPI 1.1 Spec
 */
#define SDW_REGADDR				GENMASK(14, 0)
#define SDW_SCP_ADDRPAGE2_MASK			GENMASK(22, 15)
#define SDW_SCP_ADDRPAGE1_MASK			GENMASK(30, 23)

#define SDW_REG_NO_PAGE				0x00008000
#define SDW_REG_OPTIONAL_PAGE			0x00010000
#define SDW_REG_MAX				0x80000000

#define SDW_DPN_SIZE				0x100
#define SDW_BANK1_OFFSET			0x10

/*
 * DP0 Interrupt register & bits
 *
 * Spec treats Status (RO) and Clear (WC) as separate but they are same
 * address, so treat as same register with WC.
 */

/* both INT and STATUS register are same */
#define SDW_DP0_INT				0x0
#define SDW_DP0_INTMASK				0x1
#define SDW_DP0_PORTCTRL			0x2
#define SDW_DP0_BLOCKCTRL1			0x3
#define SDW_DP0_PREPARESTATUS			0x4
#define SDW_DP0_PREPARECTRL			0x5

#define SDW_DP0_INT_TEST_FAIL			BIT(0)
#define SDW_DP0_INT_PORT_READY			BIT(1)
#define SDW_DP0_INT_BRA_FAILURE			BIT(2)
#define SDW_DP0_INT_IMPDEF1			BIT(5)
#define SDW_DP0_INT_IMPDEF2			BIT(6)
#define SDW_DP0_INT_IMPDEF3			BIT(7)

#define SDW_DP0_PORTCTRL_DATAMODE		GENMASK(3, 2)
#define SDW_DP0_PORTCTRL_NXTINVBANK		BIT(4)
#define SDW_DP0_PORTCTRL_BPT_PAYLD		GENMASK(7, 6)

#define SDW_DP0_CHANNELEN			0x20
#define SDW_DP0_SAMPLECTRL1			0x22
#define SDW_DP0_SAMPLECTRL2			0x23
#define SDW_DP0_OFFSETCTRL1			0x24
#define SDW_DP0_OFFSETCTRL2			0x25
#define SDW_DP0_HCTRL				0x26
#define SDW_DP0_LANECTRL			0x28

/* Both INT and STATUS register are same */
#define SDW_SCP_INT1				0x40
#define SDW_SCP_INTMASK1			0x41

#define SDW_SCP_INT1_PARITY			BIT(0)
#define SDW_SCP_INT1_BUS_CLASH			BIT(1)
#define SDW_SCP_INT1_IMPL_DEF			BIT(2)
#define SDW_SCP_INT1_SCP2_CASCADE		BIT(7)
#define SDW_SCP_INT1_PORT0_3			GENMASK(6, 3)

#define SDW_SCP_INTSTAT2			0x42
#define SDW_SCP_INTSTAT2_SCP3_CASCADE		BIT(7)
#define SDW_SCP_INTSTAT2_PORT4_10		GENMASK(6, 0)

#define SDW_SCP_INTSTAT3			0x43
#define SDW_SCP_INTSTAT3_PORT11_14		GENMASK(3, 0)

/* Number of interrupt status registers */
#define SDW_NUM_INT_STAT_REGISTERS		3

/* Number of interrupt clear registers */
#define SDW_NUM_INT_CLEAR_REGISTERS		1

#define SDW_SCP_CTRL				0x44
#define SDW_SCP_CTRL_CLK_STP_NOW		BIT(1)
#define SDW_SCP_CTRL_FORCE_RESET		BIT(7)

#define SDW_SCP_STAT				0x44
#define SDW_SCP_STAT_CLK_STP_NF			BIT(0)
#define SDW_SCP_STAT_HPHY_NOK			BIT(5)
#define SDW_SCP_STAT_CURR_BANK			BIT(6)

#define SDW_SCP_SYSTEMCTRL			0x45
#define SDW_SCP_SYSTEMCTRL_CLK_STP_PREP		BIT(0)
#define SDW_SCP_SYSTEMCTRL_CLK_STP_MODE		BIT(2)
#define SDW_SCP_SYSTEMCTRL_WAKE_UP_EN		BIT(3)
#define SDW_SCP_SYSTEMCTRL_HIGH_PHY		BIT(4)

#define SDW_SCP_SYSTEMCTRL_CLK_STP_MODE0	0
#define SDW_SCP_SYSTEMCTRL_CLK_STP_MODE1	BIT(2)

#define SDW_SCP_DEVNUMBER			0x46
#define SDW_SCP_HIGH_PHY_CHECK			0x47
#define SDW_SCP_ADDRPAGE1			0x48
#define SDW_SCP_ADDRPAGE2			0x49
#define SDW_SCP_KEEPEREN			0x4A
#define SDW_SCP_BANKDELAY			0x4B
#define SDW_SCP_TESTMODE			0x4F
#define SDW_SCP_DEVID_0				0x50
#define SDW_SCP_DEVID_1				0x51
#define SDW_SCP_DEVID_2				0x52
#define SDW_SCP_DEVID_3				0x53
#define SDW_SCP_DEVID_4				0x54
#define SDW_SCP_DEVID_5				0x55

/* Banked Registers */
#define SDW_SCP_FRAMECTRL_B0			0x60
#define SDW_SCP_FRAMECTRL_B1			(0x60 + SDW_BANK1_OFFSET)
#define SDW_SCP_NEXTFRAME_B0			0x61
#define SDW_SCP_NEXTFRAME_B1			(0x61 + SDW_BANK1_OFFSET)

/* Both INT and STATUS register is same */
#define SDW_DPN_INT(n)				(0x0 + SDW_DPN_SIZE * (n))
#define SDW_DPN_INTMASK(n)			(0x1 + SDW_DPN_SIZE * (n))
#define SDW_DPN_PORTCTRL(n)			(0x2 + SDW_DPN_SIZE * (n))
#define SDW_DPN_BLOCKCTRL1(n)			(0x3 + SDW_DPN_SIZE * (n))
#define SDW_DPN_PREPARESTATUS(n)		(0x4 + SDW_DPN_SIZE * (n))
#define SDW_DPN_PREPARECTRL(n)			(0x5 + SDW_DPN_SIZE * (n))

#define SDW_DPN_INT_TEST_FAIL			BIT(0)
#define SDW_DPN_INT_PORT_READY			BIT(1)
#define SDW_DPN_INT_IMPDEF1			BIT(5)
#define SDW_DPN_INT_IMPDEF2			BIT(6)
#define SDW_DPN_INT_IMPDEF3			BIT(7)

#define SDW_DPN_PORTCTRL_FLOWMODE		GENMASK(1, 0)
#define SDW_DPN_PORTCTRL_DATAMODE		GENMASK(3, 2)
#define SDW_DPN_PORTCTRL_NXTINVBANK		BIT(4)

#define SDW_DPN_BLOCKCTRL1_WDLEN		GENMASK(5, 0)

#define SDW_DPN_PREPARECTRL_CH_PREP		GENMASK(7, 0)

#define SDW_DPN_CHANNELEN_B0(n)			(0x20 + SDW_DPN_SIZE * (n))
#define SDW_DPN_CHANNELEN_B1(n)			(0x30 + SDW_DPN_SIZE * (n))

#define SDW_DPN_BLOCKCTRL2_B0(n)		(0x21 + SDW_DPN_SIZE * (n))
#define SDW_DPN_BLOCKCTRL2_B1(n)		(0x31 + SDW_DPN_SIZE * (n))

#define SDW_DPN_SAMPLECTRL1_B0(n)		(0x22 + SDW_DPN_SIZE * (n))
#define SDW_DPN_SAMPLECTRL1_B1(n)		(0x32 + SDW_DPN_SIZE * (n))

#define SDW_DPN_SAMPLECTRL2_B0(n)		(0x23 + SDW_DPN_SIZE * (n))
#define SDW_DPN_SAMPLECTRL2_B1(n)		(0x33 + SDW_DPN_SIZE * (n))

#define SDW_DPN_OFFSETCTRL1_B0(n)		(0x24 + SDW_DPN_SIZE * (n))
#define SDW_DPN_OFFSETCTRL1_B1(n)		(0x34 + SDW_DPN_SIZE * (n))

#define SDW_DPN_OFFSETCTRL2_B0(n)		(0x25 + SDW_DPN_SIZE * (n))
#define SDW_DPN_OFFSETCTRL2_B1(n)		(0x35 + SDW_DPN_SIZE * (n))

#define SDW_DPN_HCTRL_B0(n)			(0x26 + SDW_DPN_SIZE * (n))
#define SDW_DPN_HCTRL_B1(n)			(0x36 + SDW_DPN_SIZE * (n))

#define SDW_DPN_BLOCKCTRL3_B0(n)		(0x27 + SDW_DPN_SIZE * (n))
#define SDW_DPN_BLOCKCTRL3_B1(n)		(0x37 + SDW_DPN_SIZE * (n))

#define SDW_DPN_LANECTRL_B0(n)			(0x28 + SDW_DPN_SIZE * (n))
#define SDW_DPN_LANECTRL_B1(n)			(0x38 + SDW_DPN_SIZE * (n))

#define SDW_DPN_SAMPLECTRL_LOW			GENMASK(7, 0)
#define SDW_DPN_SAMPLECTRL_HIGH			GENMASK(15, 8)

#define SDW_DPN_HCTRL_HSTART			GENMASK(7, 4)
#define SDW_DPN_HCTRL_HSTOP			GENMASK(3, 0)

#define SDW_NUM_CASC_PORT_INTSTAT1		4
#define SDW_CASC_PORT_START_INTSTAT1		0
#define SDW_CASC_PORT_MASK_INTSTAT1		0x8
#define SDW_CASC_PORT_REG_OFFSET_INTSTAT1	0x0

#define SDW_NUM_CASC_PORT_INTSTAT2		7
#define SDW_CASC_PORT_START_INTSTAT2		4
#define SDW_CASC_PORT_MASK_INTSTAT2		1
#define SDW_CASC_PORT_REG_OFFSET_INTSTAT2	1

#define SDW_NUM_CASC_PORT_INTSTAT3		4
#define SDW_CASC_PORT_START_INTSTAT3		11
#define SDW_CASC_PORT_MASK_INTSTAT3		1
#define SDW_CASC_PORT_REG_OFFSET_INTSTAT3	2

#endif /* __SDW_REGISTERS_H */
