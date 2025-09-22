/*	$OpenBSD: ixgbe_type.h,v 1.40 2024/10/27 04:44:41 yasuoka Exp $	*/

/******************************************************************************
  SPDX-License-Identifier: BSD-3-Clause

  Copyright (c) 2001-2017, Intel Corporation
  All rights reserved.

  Redistribution and use in source and binary forms, with or without
  modification, are permitted provided that the following conditions are met:

   1. Redistributions of source code must retain the above copyright notice,
      this list of conditions and the following disclaimer.

   2. Redistributions in binary form must reproduce the above copyright
      notice, this list of conditions and the following disclaimer in the
      documentation and/or other materials provided with the distribution.

   3. Neither the name of the Intel Corporation nor the names of its
      contributors may be used to endorse or promote products derived from
      this software without specific prior written permission.

  THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
  AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
  IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
  ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT OWNER OR CONTRIBUTORS BE
  LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
  CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
  SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
  INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
  CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
  ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
  POSSIBILITY OF SUCH DAMAGE.

******************************************************************************/
/*$FreeBSD: head/sys/dev/ixgbe/ixgbe_type.h 331224 2018-03-19 20:55:05Z erj $*/
/*$FreeBSD: head/sys/dev/ixgbe/ixgbe_phy.h 326022 2017-11-20 19:36:21Z pfg $*/
/*$FreeBSD: head/sys/dev/ixgbe/ixgbe_mbx.h 326022 2017-11-20 19:36:21Z pfg $*/

#ifndef _IXGBE_TYPE_H_
#define _IXGBE_TYPE_H_

/*
 * The following is a brief description of the error categories used by the
 * ERROR_REPORT* macros.
 *
 * - IXGBE_ERROR_INVALID_STATE
 * This category is for errors which represent a serious failure state that is
 * unexpected, and could be potentially harmful to device operation. It should
 * not be used for errors relating to issues that can be worked around or
 * ignored.
 *
 * - IXGBE_ERROR_POLLING
 * This category is for errors related to polling/timeout issues and should be
 * used in any case where the timeout occurred, or a failure to obtain a lock, or
 * failure to receive data within the time limit.
 *
 * - IXGBE_ERROR_CAUTION
 * This category should be used for reporting issues that may be the cause of
 * other errors, such as temperature warnings. It should indicate an event which
 * could be serious, but hasn't necessarily caused problems yet.
 *
 * - IXGBE_ERROR_SOFTWARE
 * This category is intended for errors due to software state preventing
 * something. The category is not intended for errors due to bad arguments, or
 * due to unsupported features. It should be used when a state occurs which
 * prevents action but is not a serious issue.
 *
 * - IXGBE_ERROR_ARGUMENT
 * This category is for when a bad or invalid argument is passed. It should be
 * used whenever a function is called and error checking has detected the
 * argument is wrong or incorrect.
 *
 * - IXGBE_ERROR_UNSUPPORTED
 * This category is for errors which are due to unsupported circumstances or
 * configuration issues. It should not be used when the issue is due to an
 * invalid argument, but for when something has occurred that is unsupported
 * (Ex: Flow control autonegotiation or an unsupported SFP+ module.)
 */

/* Vendor ID */
#define IXGBE_INTEL_VENDOR_ID			0x8086

/* Device IDs */
#define IXGBE_DEV_ID_82598			0x10B6
#define IXGBE_DEV_ID_82598_BX			0x1508
#define IXGBE_DEV_ID_82598AF_DUAL_PORT		0x10C6
#define IXGBE_DEV_ID_82598AF_SINGLE_PORT	0x10C7
#define IXGBE_DEV_ID_82598AT			0x10C8
#define IXGBE_DEV_ID_82598AT2			0x150B
#define IXGBE_DEV_ID_82598AT_DUAL_PORT		0x10D7
#define IXGBE_DEV_ID_82598EB_SFP_LOM		0x10DB
#define IXGBE_DEV_ID_82598EB_CX4		0x10DD
#define IXGBE_DEV_ID_82598_CX4_DUAL_PORT	0x10EC
#define IXGBE_DEV_ID_82598_DA_DUAL_PORT		0x10F1
#define IXGBE_DEV_ID_82598_SR_DUAL_PORT_EM	0x10E1
#define IXGBE_DEV_ID_82598EB_XF_LR		0x10F4
#define IXGBE_DEV_ID_82599_KX4			0x10F7
#define IXGBE_DEV_ID_82599_KX4_MEZZ		0x1514
#define IXGBE_DEV_ID_82599_KR			0x1517
#define IXGBE_DEV_ID_82599_COMBO_BACKPLANE	0x10F8
#define IXGBE_SUBDEV_ID_82599_KX4_KR_MEZZ	0x000C
#define IXGBE_DEV_ID_82599_CX4			0x10F9
#define IXGBE_DEV_ID_82599_SFP			0x10FB
#define IXGBE_SUBDEV_ID_82599_SFP		0x11A9
#define IXGBE_SUBDEV_ID_82599_SFP_WOL0		0x1071
#define IXGBE_SUBDEV_ID_82599_RNDC		0x1F72
#define IXGBE_SUBDEV_ID_82599_560FLR		0x17D0
#define IXGBE_SUBDEV_ID_82599_ECNA_DP		0x0470
#define IXGBE_SUBDEV_ID_82599_SP_560FLR		0x211B
#define IXGBE_SUBDEV_ID_82599_LOM_SNAP6		0x2159
#define IXGBE_SUBDEV_ID_82599_SFP_1OCP		0x000D
#define IXGBE_SUBDEV_ID_82599_SFP_2OCP		0x0008
#define IXGBE_SUBDEV_ID_82599_SFP_LOM_OEM1	0x8976
#define IXGBE_SUBDEV_ID_82599_SFP_LOM_OEM2	0x06EE
#define IXGBE_DEV_ID_82599_BACKPLANE_FCOE	0x152A
#define IXGBE_DEV_ID_82599_SFP_FCOE		0x1529
#define IXGBE_DEV_ID_82599_SFP_EM		0x1507
#define IXGBE_DEV_ID_82599_SFP_SF2		0x154D
#define IXGBE_DEV_ID_82599_SFP_SF_QP		0x154A
#define IXGBE_DEV_ID_82599_QSFP_SF_QP		0x1558
#define IXGBE_DEV_ID_82599EN_SFP		0x1557
#define IXGBE_SUBDEV_ID_82599EN_SFP_OCP1	0x0001
#define IXGBE_DEV_ID_82599_XAUI_LOM		0x10FC
#define IXGBE_DEV_ID_82599_T3_LOM		0x151C
#define IXGBE_DEV_ID_82599_VF			0x10ED
#define IXGBE_DEV_ID_82599_VF_HV		0x152E
#define IXGBE_DEV_ID_82599_BYPASS		0x155D
#define IXGBE_DEV_ID_X540T			0x1528
#define IXGBE_DEV_ID_X540_VF			0x1515
#define IXGBE_DEV_ID_X540_VF_HV			0x1530
#define IXGBE_DEV_ID_X540_BYPASS		0x155C
#define IXGBE_DEV_ID_X540T1			0x1560
#define IXGBE_DEV_ID_X550T			0x1563
#define IXGBE_DEV_ID_X550T1			0x15D1
#define IXGBE_DEV_ID_X550EM_A_KR		0x15C2
#define IXGBE_DEV_ID_X550EM_A_KR_L		0x15C3
#define IXGBE_DEV_ID_X550EM_A_SFP_N		0x15C4
#define IXGBE_DEV_ID_X550EM_A_SGMII		0x15C6
#define IXGBE_DEV_ID_X550EM_A_SGMII_L		0x15C7
#define IXGBE_DEV_ID_X550EM_A_10G_T		0x15C8
#define IXGBE_DEV_ID_X550EM_A_QSFP		0x15CA
#define IXGBE_DEV_ID_X550EM_A_QSFP_N		0x15CC
#define IXGBE_DEV_ID_X550EM_A_SFP		0x15CE
#define IXGBE_DEV_ID_X550EM_A_1G_T		0x15E4
#define IXGBE_DEV_ID_X550EM_A_1G_T_L		0x15E5
#define IXGBE_DEV_ID_X550EM_X_KX4		0x15AA
#define IXGBE_DEV_ID_X550EM_X_KR		0x15AB
#define IXGBE_DEV_ID_X550EM_X_SFP		0x15AC
#define IXGBE_DEV_ID_X550EM_X_10G_T		0x15AD
#define IXGBE_DEV_ID_X550EM_X_1G_T		0x15AE
#define IXGBE_DEV_ID_X550EM_X_XFI		0x15B0
#define IXGBE_DEV_ID_X550_VF_HV			0x1564
#define IXGBE_DEV_ID_X550_VF			0x1565
#define IXGBE_DEV_ID_X550EM_A_VF		0x15C5
#define IXGBE_DEV_ID_X550EM_A_VF_HV		0x15B4
#define IXGBE_DEV_ID_X550EM_X_VF		0x15A8
#define IXGBE_DEV_ID_X550EM_X_VF_HV		0x15A9

#define IXGBE_CAT(r,m) IXGBE_##r##m

#define IXGBE_BY_MAC(_hw, r) ((_hw)->mvals[IXGBE_CAT(r, _IDX)])

/* General Registers */
#define IXGBE_CTRL		0x00000
#define IXGBE_STATUS		0x00008
#define IXGBE_CTRL_EXT		0x00018
#define IXGBE_ESDP		0x00020
#define IXGBE_EODSDP		0x00028
#define IXGBE_I2CCTL_82599	0x00028
#define IXGBE_I2CCTL		IXGBE_I2CCTL_82599
#define IXGBE_I2CCTL_X540	IXGBE_I2CCTL_82599
#define IXGBE_I2CCTL_X550	0x15F5C
#define IXGBE_I2CCTL_X550EM_x	IXGBE_I2CCTL_X550
#define IXGBE_I2CCTL_X550EM_a	IXGBE_I2CCTL_X550
#define IXGBE_I2CCTL_BY_MAC(_hw) IXGBE_BY_MAC((_hw), I2CCTL)
#define IXGBE_PHY_GPIO		0x00028
#define IXGBE_MAC_GPIO		0x00030
#define IXGBE_PHYINT_STATUS0	0x00100
#define IXGBE_PHYINT_STATUS1	0x00104
#define IXGBE_PHYINT_STATUS2	0x00108
#define IXGBE_LEDCTL		0x00200
#define IXGBE_FRTIMER		0x00048
#define IXGBE_TCPTIMER		0x0004C
#define IXGBE_CORESPARE		0x00600
#define IXGBE_EXVET		0x05078

/* NVM Registers */
#define IXGBE_EEC		0x10010
#define IXGBE_EEC_X540		IXGBE_EEC
#define IXGBE_EEC_X550		IXGBE_EEC
#define IXGBE_EEC_X550EM_x	IXGBE_EEC
#define IXGBE_EEC_X550EM_a	0x15FF8
#define IXGBE_EEC_BY_MAC(_hw)	IXGBE_BY_MAC((_hw), EEC)

#define IXGBE_EERD		0x10014
#define IXGBE_EEWR		0x10018

#define IXGBE_FLA		0x1001C
#define IXGBE_FLA_X540		IXGBE_FLA
#define IXGBE_FLA_X550		IXGBE_FLA
#define IXGBE_FLA_X550EM_x	IXGBE_FLA
#define IXGBE_FLA_X550EM_a	0x15F68
#define IXGBE_FLA_BY_MAC(_hw)	IXGBE_BY_MAC((_hw), FLA)

#define IXGBE_EEMNGCTL	0x10110
#define IXGBE_EEMNGDATA	0x10114
#define IXGBE_FLMNGCTL	0x10118
#define IXGBE_FLMNGDATA	0x1011C
#define IXGBE_FLMNGCNT	0x10120
#define IXGBE_FLOP	0x1013C

#define IXGBE_GRC		0x10200
#define IXGBE_GRC_X540		IXGBE_GRC
#define IXGBE_GRC_X550		IXGBE_GRC
#define IXGBE_GRC_X550EM_x	IXGBE_GRC
#define IXGBE_GRC_X550EM_a	0x15F64
#define IXGBE_GRC_BY_MAC(_hw)	IXGBE_BY_MAC((_hw), GRC)

#define IXGBE_SRAMREL		0x10210
#define IXGBE_SRAMREL_X540	IXGBE_SRAMREL
#define IXGBE_SRAMREL_X550	IXGBE_SRAMREL
#define IXGBE_SRAMREL_X550EM_x	IXGBE_SRAMREL
#define IXGBE_SRAMREL_X550EM_a	0x15F6C
#define IXGBE_SRAMREL_BY_MAC(_hw)	IXGBE_BY_MAC((_hw), SRAMREL)

#define IXGBE_PHYDBG	0x10218

/* General Receive Control */
#define IXGBE_GRC_MNG	0x00000001 /* Manageability Enable */
#define IXGBE_GRC_APME	0x00000002 /* APM enabled in EEPROM */

#define IXGBE_VPDDIAG0	0x10204
#define IXGBE_VPDDIAG1	0x10208

/* I2CCTL Bit Masks */
#define IXGBE_I2C_CLK_IN		0x00000001
#define IXGBE_I2C_CLK_IN_X540		IXGBE_I2C_CLK_IN
#define IXGBE_I2C_CLK_IN_X550		0x00004000
#define IXGBE_I2C_CLK_IN_X550EM_x	IXGBE_I2C_CLK_IN_X550
#define IXGBE_I2C_CLK_IN_X550EM_a	IXGBE_I2C_CLK_IN_X550
#define IXGBE_I2C_CLK_IN_BY_MAC(_hw)	IXGBE_BY_MAC((_hw), I2C_CLK_IN)

#define IXGBE_I2C_CLK_OUT		0x00000002
#define IXGBE_I2C_CLK_OUT_X540		IXGBE_I2C_CLK_OUT
#define IXGBE_I2C_CLK_OUT_X550		0x00000200
#define IXGBE_I2C_CLK_OUT_X550EM_x	IXGBE_I2C_CLK_OUT_X550
#define IXGBE_I2C_CLK_OUT_X550EM_a	IXGBE_I2C_CLK_OUT_X550
#define IXGBE_I2C_CLK_OUT_BY_MAC(_hw)	IXGBE_BY_MAC((_hw), I2C_CLK_OUT)

#define IXGBE_I2C_DATA_IN		0x00000004
#define IXGBE_I2C_DATA_IN_X540		IXGBE_I2C_DATA_IN
#define IXGBE_I2C_DATA_IN_X550		0x00001000
#define IXGBE_I2C_DATA_IN_X550EM_x	IXGBE_I2C_DATA_IN_X550
#define IXGBE_I2C_DATA_IN_X550EM_a	IXGBE_I2C_DATA_IN_X550
#define IXGBE_I2C_DATA_IN_BY_MAC(_hw)	IXGBE_BY_MAC((_hw), I2C_DATA_IN)

#define IXGBE_I2C_DATA_OUT		0x00000008
#define IXGBE_I2C_DATA_OUT_X540		IXGBE_I2C_DATA_OUT
#define IXGBE_I2C_DATA_OUT_X550		0x00000400
#define IXGBE_I2C_DATA_OUT_X550EM_x	IXGBE_I2C_DATA_OUT_X550
#define IXGBE_I2C_DATA_OUT_X550EM_a	IXGBE_I2C_DATA_OUT_X550
#define IXGBE_I2C_DATA_OUT_BY_MAC(_hw)	IXGBE_BY_MAC((_hw), I2C_DATA_OUT)

#define IXGBE_I2C_DATA_OE_N_EN		0
#define IXGBE_I2C_DATA_OE_N_EN_X540	IXGBE_I2C_DATA_OE_N_EN
#define IXGBE_I2C_DATA_OE_N_EN_X550	0x00000800
#define IXGBE_I2C_DATA_OE_N_EN_X550EM_x	IXGBE_I2C_DATA_OE_N_EN_X550
#define IXGBE_I2C_DATA_OE_N_EN_X550EM_a	IXGBE_I2C_DATA_OE_N_EN_X550
#define IXGBE_I2C_DATA_OE_N_EN_BY_MAC(_hw) IXGBE_BY_MAC((_hw), I2C_DATA_OE_N_EN)

#define IXGBE_I2C_BB_EN			0
#define IXGBE_I2C_BB_EN_X540		IXGBE_I2C_BB_EN
#define IXGBE_I2C_BB_EN_X550		0x00000100
#define IXGBE_I2C_BB_EN_X550EM_x	IXGBE_I2C_BB_EN_X550
#define IXGBE_I2C_BB_EN_X550EM_a	IXGBE_I2C_BB_EN_X550
#define IXGBE_I2C_BB_EN_BY_MAC(_hw)	IXGBE_BY_MAC((_hw), I2C_BB_EN)

#define IXGBE_I2C_CLK_OE_N_EN		0
#define IXGBE_I2C_CLK_OE_N_EN_X540	IXGBE_I2C_CLK_OE_N_EN
#define IXGBE_I2C_CLK_OE_N_EN_X550	0x00002000
#define IXGBE_I2C_CLK_OE_N_EN_X550EM_x	IXGBE_I2C_CLK_OE_N_EN_X550
#define IXGBE_I2C_CLK_OE_N_EN_X550EM_a	IXGBE_I2C_CLK_OE_N_EN_X550
#define IXGBE_I2C_CLK_OE_N_EN_BY_MAC(_hw) IXGBE_BY_MAC((_hw), I2C_CLK_OE_N_EN)
#define IXGBE_I2C_CLOCK_STRETCHING_TIMEOUT	500



#define NVM_OROM_OFFSET		0x17
#define NVM_OROM_BLK_LOW	0x83
#define NVM_OROM_BLK_HI		0x84
#define NVM_OROM_PATCH_MASK	0xFF
#define NVM_OROM_SHIFT		8

#define NVM_VER_MASK		0x00FF /* version mask */
#define NVM_VER_SHIFT		8     /* version bit shift */
#define NVM_OEM_PROD_VER_PTR	0x1B  /* OEM Product version block pointer */
#define NVM_OEM_PROD_VER_CAP_OFF 0x1  /* OEM Product version format offset */
#define NVM_OEM_PROD_VER_OFF_L	0x2   /* OEM Product version offset low */
#define NVM_OEM_PROD_VER_OFF_H	0x3   /* OEM Product version offset high */
#define NVM_OEM_PROD_VER_CAP_MASK 0xF /* OEM Product version cap mask */
#define NVM_OEM_PROD_VER_MOD_LEN 0x3  /* OEM Product version module length */
#define NVM_ETK_OFF_LOW		0x2D  /* version low order word */
#define NVM_ETK_OFF_HI		0x2E  /* version high order word */
#define NVM_ETK_SHIFT		16    /* high version word shift */
#define NVM_VER_INVALID		0xFFFF
#define NVM_ETK_VALID		0x8000
#define NVM_INVALID_PTR		0xFFFF
#define NVM_VER_SIZE		32    /* version string size */

struct ixgbe_nvm_version {
	uint32_t etk_id;
	uint8_t  nvm_major;
	uint16_t nvm_minor;
	uint8_t  nvm_id;

	bool oem_valid;
	uint8_t   oem_major;
	uint8_t   oem_minor;
	uint16_t  oem_release;

	bool or_valid;
	uint8_t  or_major;
	uint16_t or_build;
	uint8_t  or_patch;

};

/* Interrupt Registers */
#define IXGBE_EICR		0x00800
#define IXGBE_EICS		0x00808
#define IXGBE_EIMS		0x00880
#define IXGBE_EIMC		0x00888
#define IXGBE_EIAC		0x00810
#define IXGBE_EIAM		0x00890
#define IXGBE_EICS_EX(_i)	(0x00A90 + (_i) * 4)
#define IXGBE_EIMS_EX(_i)	(0x00AA0 + (_i) * 4)
#define IXGBE_EIMC_EX(_i)	(0x00AB0 + (_i) * 4)
#define IXGBE_EIAM_EX(_i)	(0x00AD0 + (_i) * 4)
/* 82599 EITR is only 12 bits, with the lower 3 always zero */
/*
 * 82598 EITR is 16 bits but set the limits based on the max
 * supported by all ixgbe hardware
 */
#define IXGBE_MAX_INT_RATE	488281
#define IXGBE_MIN_INT_RATE	956
#define IXGBE_MAX_EITR		0x00000FF8
#define IXGBE_MIN_EITR		8
#define IXGBE_EITR(_i)		(((_i) <= 23) ? (0x00820 + ((_i) * 4)) : \
				 (0x012300 + (((_i) - 24) * 4)))
#define IXGBE_EITR_ITR_INT_MASK	0x00000FF8
#define IXGBE_EITR_LLI_MOD	0x00008000
#define IXGBE_EITR_CNT_WDIS	0x80000000
#define IXGBE_IVAR(_i)		(0x00900 + ((_i) * 4)) /* 24 at 0x900-0x960 */
#define IXGBE_IVAR_MISC		0x00A00 /* misc MSI-X interrupt causes */
#define IXGBE_EITRSEL		0x00894
#define IXGBE_MSIXT		0x00000 /* MSI-X Table. 0x0000 - 0x01C */
#define IXGBE_MSIXPBA		0x02000 /* MSI-X Pending bit array */
#define IXGBE_PBACL(_i)	(((_i) == 0) ? (0x11068) : (0x110C0 + ((_i) * 4)))
#define IXGBE_GPIE		0x00898

/* Flow Control Registers */
#define IXGBE_FCADBUL		0x03210
#define IXGBE_FCADBUH		0x03214
#define IXGBE_FCAMACL		0x04328
#define IXGBE_FCAMACH		0x0432C
#define IXGBE_FCRTH_82599(_i)	(0x03260 + ((_i) * 4)) /* 8 of these (0-7) */
#define IXGBE_FCRTL_82599(_i)	(0x03220 + ((_i) * 4)) /* 8 of these (0-7) */
#define IXGBE_PFCTOP		0x03008
#define IXGBE_FCTTV(_i)		(0x03200 + ((_i) * 4)) /* 4 of these (0-3) */
#define IXGBE_FCRTL(_i)		(0x03220 + ((_i) * 8)) /* 8 of these (0-7) */
#define IXGBE_FCRTH(_i)		(0x03260 + ((_i) * 8)) /* 8 of these (0-7) */
#define IXGBE_FCRTV		0x032A0
#define IXGBE_FCCFG		0x03D00
#define IXGBE_TFCS		0x0CE00

/* Receive DMA Registers */
#define IXGBE_RDBAL(_i)	(((_i) < 64) ? (0x01000 + ((_i) * 0x40)) : \
			 (0x0D000 + (((_i) - 64) * 0x40)))
#define IXGBE_RDBAH(_i)	(((_i) < 64) ? (0x01004 + ((_i) * 0x40)) : \
			 (0x0D004 + (((_i) - 64) * 0x40)))
#define IXGBE_RDLEN(_i)	(((_i) < 64) ? (0x01008 + ((_i) * 0x40)) : \
			 (0x0D008 + (((_i) - 64) * 0x40)))
#define IXGBE_RDH(_i)	(((_i) < 64) ? (0x01010 + ((_i) * 0x40)) : \
			 (0x0D010 + (((_i) - 64) * 0x40)))
#define IXGBE_RDT(_i)	(((_i) < 64) ? (0x01018 + ((_i) * 0x40)) : \
			 (0x0D018 + (((_i) - 64) * 0x40)))
#define IXGBE_RXDCTL(_i)	(((_i) < 64) ? (0x01028 + ((_i) * 0x40)) : \
				 (0x0D028 + (((_i) - 64) * 0x40)))
#define IXGBE_RSCCTL(_i)	(((_i) < 64) ? (0x0102C + ((_i) * 0x40)) : \
				 (0x0D02C + (((_i) - 64) * 0x40)))
#define IXGBE_RSCDBU	0x03028
#define IXGBE_RDDCC	0x02F20
#define IXGBE_RXMEMWRAP	0x03190
#define IXGBE_STARCTRL	0x03024
/*
 * Split and Replication Receive Control Registers
 * 00-15 : 0x02100 + n*4
 * 16-64 : 0x01014 + n*0x40
 * 64-127: 0x0D014 + (n-64)*0x40
 */
#define IXGBE_SRRCTL(_i)	(((_i) <= 15) ? (0x02100 + ((_i) * 4)) : \
				 (((_i) < 64) ? (0x01014 + ((_i) * 0x40)) : \
				 (0x0D014 + (((_i) - 64) * 0x40))))
/*
 * Rx DCA Control Register:
 * 00-15 : 0x02200 + n*4
 * 16-64 : 0x0100C + n*0x40
 * 64-127: 0x0D00C + (n-64)*0x40
 */
#define IXGBE_DCA_RXCTRL(_i)	(((_i) <= 15) ? (0x02200 + ((_i) * 4)) : \
				 (((_i) < 64) ? (0x0100C + ((_i) * 0x40)) : \
				 (0x0D00C + (((_i) - 64) * 0x40))))
#define IXGBE_RDRXCTL		0x02F00
/* 8 of these 0x03C00 - 0x03C1C */
#define IXGBE_RXPBSIZE(_i)	(0x03C00 + ((_i) * 4))
#define IXGBE_RXCTRL		0x03000
#define IXGBE_DROPEN		0x03D04
#define IXGBE_RXPBSIZE_SHIFT	10
#define IXGBE_RXPBSIZE_MASK	0x000FFC00

/* Receive Registers */
#define IXGBE_RXCSUM		0x05000
#define IXGBE_RFCTL		0x05008
#define IXGBE_DRECCCTL		0x02F08
#define IXGBE_DRECCCTL_DISABLE	0
#define IXGBE_DRECCCTL2		0x02F8C

/* Multicast Table Array - 128 entries */
#define IXGBE_MTA(_i)		(0x05200 + ((_i) * 4))
#define IXGBE_RAL(_i)		(((_i) <= 15) ? (0x05400 + ((_i) * 8)) : \
				 (0x0A200 + ((_i) * 8)))
#define IXGBE_RAH(_i)		(((_i) <= 15) ? (0x05404 + ((_i) * 8)) : \
				 (0x0A204 + ((_i) * 8)))
#define IXGBE_MPSAR_LO(_i)	(0x0A600 + ((_i) * 8))
#define IXGBE_MPSAR_HI(_i)	(0x0A604 + ((_i) * 8))
/* Packet split receive type */
#define IXGBE_PSRTYPE(_i)	(((_i) <= 15) ? (0x05480 + ((_i) * 4)) : \
				 (0x0EA00 + ((_i) * 4)))
/* array of 4096 1-bit vlan filters */
#define IXGBE_VFTA(_i)		(0x0A000 + ((_i) * 4))
/*array of 4096 4-bit vlan vmdq indices */
#define IXGBE_VFTAVIND(_j, _i)	(0x0A200 + ((_j) * 0x200) + ((_i) * 4))
#define IXGBE_FCTRL		0x05080
#define IXGBE_VLNCTRL		0x05088
#define IXGBE_MCSTCTRL		0x05090
#define IXGBE_MRQC		0x05818
#define IXGBE_SAQF(_i)	(0x0E000 + ((_i) * 4)) /* Source Address Queue Filter */
#define IXGBE_DAQF(_i)	(0x0E200 + ((_i) * 4)) /* Dest. Address Queue Filter */
#define IXGBE_SDPQF(_i)	(0x0E400 + ((_i) * 4)) /* Src Dest. Addr Queue Filter */
#define IXGBE_FTQF(_i)	(0x0E600 + ((_i) * 4)) /* Five Tuple Queue Filter */
#define IXGBE_ETQF(_i)	(0x05128 + ((_i) * 4)) /* EType Queue Filter */
#define IXGBE_ETQS(_i)	(0x0EC00 + ((_i) * 4)) /* EType Queue Select */
#define IXGBE_SYNQF	0x0EC30 /* SYN Packet Queue Filter */
#define IXGBE_RQTC	0x0EC70
#define IXGBE_MTQC	0x08120
#define IXGBE_VLVF(_i)	(0x0F100 + ((_i) * 4))  /* 64 of these (0-63) */
#define IXGBE_VLVFB(_i)	(0x0F200 + ((_i) * 4))  /* 128 of these (0-127) */
#define IXGBE_VMVIR(_i)	(0x08000 + ((_i) * 4))  /* 64 of these (0-63) */
#define IXGBE_PFFLPL		0x050B0
#define IXGBE_PFFLPH		0x050B4
#define IXGBE_VT_CTL		0x051B0
#define IXGBE_PFMAILBOX(_i)	(0x04B00 + (4 * (_i))) /* 64 total */
/* 64 Mailboxes, 16 DW each */
#define IXGBE_PFMBMEM(_i)	(0x13000 + (64 * (_i)))
#define IXGBE_PFMBICR_INDEX(_i)	((_i) >> 4)
#define IXGBE_PFMBICR_SHIFT(_i)	((_i) % 16)
#define IXGBE_PFMBICR(_i)	(0x00710 + (4 * (_i))) /* 4 total */
#define IXGBE_PFMBIMR(_i)	(0x00720 + (4 * (_i))) /* 4 total */
#define IXGBE_PFVFLRE(_i)	((((_i) & 1) ? 0x001C0 : 0x00600))
#define IXGBE_PFVFLREC(_i)	(0x00700 + ((_i) * 4))
#define IXGBE_PFVFLRE_INDEX(_i)	((_i) >> 5)
#define IXGBE_PFVFLRE_SHIFT(_i)	((_i) % 32)
#define IXGBE_VFRE(_i)		(0x051E0 + ((_i) * 4))
#define IXGBE_VFTE(_i)		(0x08110 + ((_i) * 4))
#define IXGBE_VMECM(_i)		(0x08790 + ((_i) * 4))
#define IXGBE_QDE		0x2F04
#define IXGBE_VMTXSW(_i)	(0x05180 + ((_i) * 4)) /* 2 total */
#define IXGBE_VMOLR(_i)		(0x0F000 + ((_i) * 4)) /* 64 total */
#define IXGBE_UTA(_i)		(0x0F400 + ((_i) * 4))
#define IXGBE_MRCTL(_i)		(0x0F600 + ((_i) * 4))
#define IXGBE_VMRVLAN(_i)	(0x0F610 + ((_i) * 4))
#define IXGBE_VMRVM(_i)		(0x0F630 + ((_i) * 4))
#define IXGBE_LVMMC_RX		0x2FA8
#define IXGBE_LVMMC_TX		0x8108
#define IXGBE_LMVM_RX		0x2FA4
#define IXGBE_LMVM_TX		0x8124
#define IXGBE_WQBR_RX(_i)	(0x2FB0 + ((_i) * 4)) /* 4 total */
#define IXGBE_WQBR_TX(_i)	(0x8130 + ((_i) * 4)) /* 4 total */
#define IXGBE_L34T_IMIR(_i)	(0x0E800 + ((_i) * 4)) /*128 of these (0-127)*/
#define IXGBE_RXFECCERR0	0x051B8
#define IXGBE_LLITHRESH		0x0EC90
#define IXGBE_IMIR(_i)		(0x05A80 + ((_i) * 4))  /* 8 of these (0-7) */
#define IXGBE_IMIREXT(_i)	(0x05AA0 + ((_i) * 4))  /* 8 of these (0-7) */
#define IXGBE_IMIRVP		0x05AC0
#define IXGBE_VMD_CTL		0x0581C
#define IXGBE_RETA(_i)		(0x05C00 + ((_i) * 4))  /* 32 of these (0-31) */
#define IXGBE_ERETA(_i)		(0x0EE80 + ((_i) * 4))  /* 96 of these (0-95) */
#define IXGBE_RSSRK(_i)		(0x05C80 + ((_i) * 4))  /* 10 of these (0-9) */

/* Registers for setting up RSS on X550 with SRIOV
 * _p - pool number (0..63)
 * _i - index (0..10 for PFVFRSSRK, 0..15 for PFVFRETA)
 */
#define IXGBE_PFVFMRQC(_p)	(0x03400 + ((_p) * 4))
#define IXGBE_PFVFRSSRK(_i, _p)	(0x018000 + ((_i) * 4) + ((_p) * 0x40))
#define IXGBE_PFVFRETA(_i, _p)	(0x019000 + ((_i) * 4) + ((_p) * 0x40))

/* Flow Director registers */
#define IXGBE_FDIRCTRL	0x0EE00
#define IXGBE_FDIRHKEY	0x0EE68
#define IXGBE_FDIRSKEY	0x0EE6C
#define IXGBE_FDIRDIP4M	0x0EE3C
#define IXGBE_FDIRSIP4M	0x0EE40
#define IXGBE_FDIRTCPM	0x0EE44
#define IXGBE_FDIRUDPM	0x0EE48
#define IXGBE_FDIRSCTPM	0x0EE78
#define IXGBE_FDIRIP6M	0x0EE74
#define IXGBE_FDIRM	0x0EE70

/* Flow Director Stats registers */
#define IXGBE_FDIRFREE	0x0EE38
#define IXGBE_FDIRLEN	0x0EE4C
#define IXGBE_FDIRUSTAT	0x0EE50
#define IXGBE_FDIRFSTAT	0x0EE54
#define IXGBE_FDIRMATCH	0x0EE58
#define IXGBE_FDIRMISS	0x0EE5C

/* Flow Director Programming registers */
#define IXGBE_FDIRSIPv6(_i) (0x0EE0C + ((_i) * 4)) /* 3 of these (0-2) */
#define IXGBE_FDIRIPSA	0x0EE18
#define IXGBE_FDIRIPDA	0x0EE1C
#define IXGBE_FDIRPORT	0x0EE20
#define IXGBE_FDIRVLAN	0x0EE24
#define IXGBE_FDIRHASH	0x0EE28
#define IXGBE_FDIRCMD	0x0EE2C

/* Transmit DMA registers */
#define IXGBE_TDBAL(_i)		(0x06000 + ((_i) * 0x40)) /* 32 of them (0-31)*/
#define IXGBE_TDBAH(_i)		(0x06004 + ((_i) * 0x40))
#define IXGBE_TDLEN(_i)		(0x06008 + ((_i) * 0x40))
#define IXGBE_TDH(_i)		(0x06010 + ((_i) * 0x40))
#define IXGBE_TDT(_i)		(0x06018 + ((_i) * 0x40))
#define IXGBE_TXDCTL(_i)	(0x06028 + ((_i) * 0x40))
#define IXGBE_TDWBAL(_i)	(0x06038 + ((_i) * 0x40))
#define IXGBE_TDWBAH(_i)	(0x0603C + ((_i) * 0x40))
#define IXGBE_DTXCTL		0x07E00

#define IXGBE_DMATXCTL		0x04A80
#define IXGBE_PFVFSPOOF(_i)	(0x08200 + ((_i) * 4)) /* 8 of these 0 - 7 */
#define IXGBE_PFDTXGSWC		0x08220
#define IXGBE_DTXMXSZRQ		0x08100
#define IXGBE_DTXTCPFLGL	0x04A88
#define IXGBE_DTXTCPFLGH	0x04A8C
#define IXGBE_LBDRPEN		0x0CA00
#define IXGBE_TXPBTHRESH(_i)	(0x04950 + ((_i) * 4)) /* 8 of these 0 - 7 */

#define IXGBE_DMATXCTL_TE	0x1 /* Transmit Enable */
#define IXGBE_DMATXCTL_NS	0x2 /* No Snoop LSO hdr buffer */
#define IXGBE_DMATXCTL_GDV	0x8 /* Global Double VLAN */
#define IXGBE_DMATXCTL_MDP_EN	0x20 /* Bit 5 */
#define IXGBE_DMATXCTL_MBINTEN	0x40 /* Bit 6 */
#define IXGBE_DMATXCTL_VT_SHIFT	16  /* VLAN EtherType */

#define IXGBE_PFDTXGSWC_VT_LBEN	0x1 /* Local L2 VT switch enable */

/* Anti-spoofing defines */
#define IXGBE_SPOOF_MACAS_MASK		0xFF
#define IXGBE_SPOOF_VLANAS_MASK		0xFF00
#define IXGBE_SPOOF_VLANAS_SHIFT	8
#define IXGBE_SPOOF_ETHERTYPEAS		0xFF000000
#define IXGBE_SPOOF_ETHERTYPEAS_SHIFT	16
#define IXGBE_PFVFSPOOF_REG_COUNT	8
/* 16 of these (0-15) */
#define IXGBE_DCA_TXCTRL(_i)		(0x07200 + ((_i) * 4))
/* Tx DCA Control register : 128 of these (0-127) */
#define IXGBE_DCA_TXCTRL_82599(_i)	(0x0600C + ((_i) * 0x40))
#define IXGBE_TIPG			0x0CB00
#define IXGBE_TXPBSIZE(_i)		(0x0CC00 + ((_i) * 4)) /* 8 of these */
#define IXGBE_MNGTXMAP			0x0CD10
#define IXGBE_TIPG_FIBER_DEFAULT	3
#define IXGBE_TXPBSIZE_SHIFT		10

/* Wake up registers */
#define IXGBE_WUC	0x05800
#define IXGBE_WUFC	0x05808
#define IXGBE_WUS	0x05810
#define IXGBE_IPAV	0x05838
#define IXGBE_IP4AT	0x05840 /* IPv4 table 0x5840-0x5858 */
#define IXGBE_IP6AT	0x05880 /* IPv6 table 0x5880-0x588F */

#define IXGBE_WUPL	0x05900
#define IXGBE_WUPM	0x05A00 /* wake up pkt memory 0x5A00-0x5A7C */
#define IXGBE_PROXYS	0x05F60 /* Proxying Status Register */
#define IXGBE_PROXYFC	0x05F64 /* Proxying Filter Control Register */
#define IXGBE_VXLANCTRL	0x0000507C /* Rx filter VXLAN UDPPORT Register */

/* masks for accessing VXLAN and GENEVE UDP ports */
#define IXGBE_VXLANCTRL_VXLAN_UDPPORT_MASK	0x0000ffff /* VXLAN port */
#define IXGBE_VXLANCTRL_GENEVE_UDPPORT_MASK	0xffff0000 /* GENEVE port */
#define IXGBE_VXLANCTRL_ALL_UDPPORT_MASK	0xffffffff /* GENEVE/VXLAN */
#define IXGBE_VXLANCTRL_GENEVE_UDPPORT_SHIFT	16

#define IXGBE_FHFT(_n)	(0x09000 + ((_n) * 0x100)) /* Flex host filter table */
/* Ext Flexible Host Filter Table */
#define IXGBE_FHFT_EXT(_n)	(0x09800 + ((_n) * 0x100))
#define IXGBE_FHFT_EXT_X550(_n)	(0x09600 + ((_n) * 0x100))

/* Four Flexible Filters are supported */
#define IXGBE_FLEXIBLE_FILTER_COUNT_MAX		4
/* Six Flexible Filters are supported */
#define IXGBE_FLEXIBLE_FILTER_COUNT_MAX_6	6
/* Eight Flexible Filters are supported */
#define IXGBE_FLEXIBLE_FILTER_COUNT_MAX_8	8
#define IXGBE_EXT_FLEXIBLE_FILTER_COUNT_MAX	2

/* Each Flexible Filter is at most 128 (0x80) bytes in length */
#define IXGBE_FLEXIBLE_FILTER_SIZE_MAX		128
#define IXGBE_FHFT_LENGTH_OFFSET		0xFC  /* Length byte in FHFT */
#define IXGBE_FHFT_LENGTH_MASK			0x0FF /* Length in lower byte */

/* Definitions for power management and wakeup registers */
/* Wake Up Control */
#define IXGBE_WUC_PME_EN	0x00000002 /* PME Enable */
#define IXGBE_WUC_PME_STATUS	0x00000004 /* PME Status */
#define IXGBE_WUC_WKEN		0x00000010 /* Enable PE_WAKE_N pin assertion  */

/* Wake Up Filter Control */
#define IXGBE_WUFC_LNKC	0x00000001 /* Link Status Change Wakeup Enable */
#define IXGBE_WUFC_MAG	0x00000002 /* Magic Packet Wakeup Enable */
#define IXGBE_WUFC_EX	0x00000004 /* Directed Exact Wakeup Enable */
#define IXGBE_WUFC_MC	0x00000008 /* Directed Multicast Wakeup Enable */
#define IXGBE_WUFC_BC	0x00000010 /* Broadcast Wakeup Enable */
#define IXGBE_WUFC_ARP	0x00000020 /* ARP Request Packet Wakeup Enable */
#define IXGBE_WUFC_IPV4	0x00000040 /* Directed IPv4 Packet Wakeup Enable */
#define IXGBE_WUFC_IPV6	0x00000080 /* Directed IPv6 Packet Wakeup Enable */
#define IXGBE_WUFC_MNG	0x00000100 /* Directed Mgmt Packet Wakeup Enable */

#define IXGBE_WUFC_IGNORE_TCO	0x00008000 /* Ignore WakeOn TCO packets */
#define IXGBE_WUFC_FLX0	0x00010000 /* Flexible Filter 0 Enable */
#define IXGBE_WUFC_FLX1	0x00020000 /* Flexible Filter 1 Enable */
#define IXGBE_WUFC_FLX2	0x00040000 /* Flexible Filter 2 Enable */
#define IXGBE_WUFC_FLX3	0x00080000 /* Flexible Filter 3 Enable */
#define IXGBE_WUFC_FLX4	0x00100000 /* Flexible Filter 4 Enable */
#define IXGBE_WUFC_FLX5	0x00200000 /* Flexible Filter 5 Enable */
#define IXGBE_WUFC_FLX_FILTERS		0x000F0000 /* Mask for 4 flex filters */
#define IXGBE_WUFC_FLX_FILTERS_6	0x003F0000 /* Mask for 6 flex filters */
#define IXGBE_WUFC_FLX_FILTERS_8	0x00FF0000 /* Mask for 8 flex filters */
#define IXGBE_WUFC_FW_RST_WK	0x80000000 /* Ena wake on FW reset assertion */
/* Mask for Ext. flex filters */
#define IXGBE_WUFC_EXT_FLX_FILTERS	0x00300000
#define IXGBE_WUFC_ALL_FILTERS		0x000F00FF /* Mask all 4 flex filters */
#define IXGBE_WUFC_ALL_FILTERS_6	0x003F00FF /* Mask all 6 flex filters */
#define IXGBE_WUFC_ALL_FILTERS_8	0x00FF00FF /* Mask all 8 flex filters */
#define IXGBE_WUFC_FLX_OFFSET	16 /* Offset to the Flexible Filters bits */

/* Wake Up Status */
#define IXGBE_WUS_LNKC		IXGBE_WUFC_LNKC
#define IXGBE_WUS_MAG		IXGBE_WUFC_MAG
#define IXGBE_WUS_EX		IXGBE_WUFC_EX
#define IXGBE_WUS_MC		IXGBE_WUFC_MC
#define IXGBE_WUS_BC		IXGBE_WUFC_BC
#define IXGBE_WUS_ARP		IXGBE_WUFC_ARP
#define IXGBE_WUS_IPV4		IXGBE_WUFC_IPV4
#define IXGBE_WUS_IPV6		IXGBE_WUFC_IPV6
#define IXGBE_WUS_MNG		IXGBE_WUFC_MNG
#define IXGBE_WUS_FLX0		IXGBE_WUFC_FLX0
#define IXGBE_WUS_FLX1		IXGBE_WUFC_FLX1
#define IXGBE_WUS_FLX2		IXGBE_WUFC_FLX2
#define IXGBE_WUS_FLX3		IXGBE_WUFC_FLX3
#define IXGBE_WUS_FLX4		IXGBE_WUFC_FLX4
#define IXGBE_WUS_FLX5		IXGBE_WUFC_FLX5
#define IXGBE_WUS_FLX_FILTERS	IXGBE_WUFC_FLX_FILTERS
#define IXGBE_WUS_FW_RST_WK	IXGBE_WUFC_FW_RST_WK
/* Proxy Status */
#define IXGBE_PROXYS_EX		0x00000004 /* Exact packet received */
#define IXGBE_PROXYS_ARP_DIR	0x00000020 /* ARP w/filter match received */
#define IXGBE_PROXYS_NS		0x00000200 /* IPV6 NS received */
#define IXGBE_PROXYS_NS_DIR	0x00000400 /* IPV6 NS w/DA match received */
#define IXGBE_PROXYS_ARP	0x00000800 /* ARP request packet received */
#define IXGBE_PROXYS_MLD	0x00001000 /* IPv6 MLD packet received */

/* Proxying Filter Control */
#define IXGBE_PROXYFC_ENABLE	0x00000001 /* Port Proxying Enable */
#define IXGBE_PROXYFC_EX	0x00000004 /* Directed Exact Proxy Enable */
#define IXGBE_PROXYFC_ARP_DIR	0x00000020 /* Directed ARP Proxy Enable */
#define IXGBE_PROXYFC_NS	0x00000200 /* IPv6 Neighbor Solicitation */
#define IXGBE_PROXYFC_ARP	0x00000800 /* ARP Request Proxy Enable */
#define IXGBE_PROXYFC_MLD	0x00000800 /* IPv6 MLD Proxy Enable */
#define IXGBE_PROXYFC_NO_TCO	0x00008000 /* Ignore TCO packets */

#define IXGBE_WUPL_LENGTH_MASK	0xFFFF

/* DCB registers */
#define IXGBE_DCB_MAX_TRAFFIC_CLASS	8
#define IXGBE_RMCS		0x03D00
#define IXGBE_DPMCS		0x07F40
#define IXGBE_PDPMCS		0x0CD00
#define IXGBE_RUPPBMR		0x050A0
#define IXGBE_RT2CR(_i)		(0x03C20 + ((_i) * 4)) /* 8 of these (0-7) */
#define IXGBE_RT2SR(_i)		(0x03C40 + ((_i) * 4)) /* 8 of these (0-7) */
#define IXGBE_TDTQ2TCCR(_i)	(0x0602C + ((_i) * 0x40)) /* 8 of these (0-7) */
#define IXGBE_TDTQ2TCSR(_i)	(0x0622C + ((_i) * 0x40)) /* 8 of these (0-7) */
#define IXGBE_TDPT2TCCR(_i)	(0x0CD20 + ((_i) * 4)) /* 8 of these (0-7) */
#define IXGBE_TDPT2TCSR(_i)	(0x0CD40 + ((_i) * 4)) /* 8 of these (0-7) */

/* Power Management */
/* DMA Coalescing configuration */
struct ixgbe_dmac_config {
	uint16_t	watchdog_timer; /* usec units */
	uint32_t	link_speed;
	uint8_t	fcoe_tc;
	uint8_t	num_tcs;
};

/*
 * DMA Coalescing threshold Rx PB TC[n] value in Kilobyte by link speed.
 * DMACRXT = 10Gbps = 10,000 bits / usec = 1250 bytes / usec 70 * 1250 ==
 * 87500 bytes [85KB]
 */
#define IXGBE_DMACRXT_10G		0x55
#define IXGBE_DMACRXT_1G		0x09
#define IXGBE_DMACRXT_100M		0x01

/* DMA Coalescing registers */
#define IXGBE_DMCMNGTH			0x15F20 /* Management Threshold */
#define IXGBE_DMACR			0x02400 /* Control register */
#define IXGBE_DMCTH(_i)			(0x03300 + ((_i) * 4)) /* 8 of these */
#define IXGBE_DMCTLX			0x02404 /* Time to Lx request */
/* DMA Coalescing register fields */
#define IXGBE_DMCMNGTH_DMCMNGTH_MASK	0x000FFFF0 /* Mng Threshold mask */
#define IXGBE_DMCMNGTH_DMCMNGTH_SHIFT	4 /* Management Threshold shift */
#define IXGBE_DMACR_DMACWT_MASK		0x0000FFFF /* Watchdog Timer mask */
#define IXGBE_DMACR_HIGH_PRI_TC_MASK	0x00FF0000
#define IXGBE_DMACR_HIGH_PRI_TC_SHIFT	16
#define IXGBE_DMACR_EN_MNG_IND		0x10000000 /* Enable Mng Indications */
#define IXGBE_DMACR_LX_COAL_IND		0x40000000 /* Lx Coalescing indicate */
#define IXGBE_DMACR_DMAC_EN		0x80000000 /* DMA Coalescing Enable */
#define IXGBE_DMCTH_DMACRXT_MASK	0x000001FF /* Receive Threshold mask */
#define IXGBE_DMCTLX_TTLX_MASK		0x00000FFF /* Time to Lx request mask */

/* EEE registers */
#define IXGBE_EEER			0x043A0 /* EEE register */
#define IXGBE_EEE_STAT			0x04398 /* EEE Status */
#define IXGBE_EEE_SU			0x04380 /* EEE Set up */
#define IXGBE_EEE_SU_TEEE_DLY_SHIFT	26
#define IXGBE_TLPIC			0x041F4 /* EEE Tx LPI count */
#define IXGBE_RLPIC			0x041F8 /* EEE Rx LPI count */

/* EEE register fields */
#define IXGBE_EEER_TX_LPI_EN		0x00010000 /* Enable EEE LPI TX path */
#define IXGBE_EEER_RX_LPI_EN		0x00020000 /* Enable EEE LPI RX path */
#define IXGBE_EEE_STAT_NEG		0x20000000 /* EEE support neg on link */
#define IXGBE_EEE_RX_LPI_STATUS		0x40000000 /* RX Link in LPI status */
#define IXGBE_EEE_TX_LPI_STATUS		0x80000000 /* TX Link in LPI status */

/* Security Control Registers */
#define IXGBE_SECTXCTRL		0x08800
#define IXGBE_SECTXSTAT		0x08804
#define IXGBE_SECTXBUFFAF	0x08808
#define IXGBE_SECTXMINIFG	0x08810
#define IXGBE_SECRXCTRL		0x08D00
#define IXGBE_SECRXSTAT		0x08D04

/* Security Bit Fields and Masks */
#define IXGBE_SECTXCTRL_SECTX_DIS	0x00000001
#define IXGBE_SECTXCTRL_TX_DIS		0x00000002
#define IXGBE_SECTXCTRL_STORE_FORWARD	0x00000004

#define IXGBE_SECTXSTAT_SECTX_RDY	0x00000001
#define IXGBE_SECTXSTAT_ECC_TXERR	0x00000002

#define IXGBE_SECRXCTRL_SECRX_DIS	0x00000001
#define IXGBE_SECRXCTRL_RX_DIS		0x00000002

#define IXGBE_SECRXSTAT_SECRX_RDY	0x00000001
#define IXGBE_SECRXSTAT_ECC_RXERR	0x00000002

/* LinkSec (MacSec) Registers */
#define IXGBE_LSECTXCAP		0x08A00
#define IXGBE_LSECRXCAP		0x08F00
#define IXGBE_LSECTXCTRL	0x08A04
#define IXGBE_LSECTXSCL		0x08A08 /* SCI Low */
#define IXGBE_LSECTXSCH		0x08A0C /* SCI High */
#define IXGBE_LSECTXSA		0x08A10
#define IXGBE_LSECTXPN0		0x08A14
#define IXGBE_LSECTXPN1		0x08A18
#define IXGBE_LSECTXKEY0(_n)	(0x08A1C + (4 * (_n))) /* 4 of these (0-3) */
#define IXGBE_LSECTXKEY1(_n)	(0x08A2C + (4 * (_n))) /* 4 of these (0-3) */
#define IXGBE_LSECRXCTRL	0x08F04
#define IXGBE_LSECRXSCL		0x08F08
#define IXGBE_LSECRXSCH		0x08F0C
#define IXGBE_LSECRXSA(_i)	(0x08F10 + (4 * (_i))) /* 2 of these (0-1) */
#define IXGBE_LSECRXPN(_i)	(0x08F18 + (4 * (_i))) /* 2 of these (0-1) */
#define IXGBE_LSECRXKEY(_n, _m)	(0x08F20 + ((0x10 * (_n)) + (4 * (_m))))
#define IXGBE_LSECTXUT		0x08A3C /* OutPktsUntagged */
#define IXGBE_LSECTXPKTE	0x08A40 /* OutPktsEncrypted */
#define IXGBE_LSECTXPKTP	0x08A44 /* OutPktsProtected */
#define IXGBE_LSECTXOCTE	0x08A48 /* OutOctetsEncrypted */
#define IXGBE_LSECTXOCTP	0x08A4C /* OutOctetsProtected */
#define IXGBE_LSECRXUT		0x08F40 /* InPktsUntagged/InPktsNoTag */
#define IXGBE_LSECRXOCTD	0x08F44 /* InOctetsDecrypted */
#define IXGBE_LSECRXOCTV	0x08F48 /* InOctetsValidated */
#define IXGBE_LSECRXBAD		0x08F4C /* InPktsBadTag */
#define IXGBE_LSECRXNOSCI	0x08F50 /* InPktsNoSci */
#define IXGBE_LSECRXUNSCI	0x08F54 /* InPktsUnknownSci */
#define IXGBE_LSECRXUNCH	0x08F58 /* InPktsUnchecked */
#define IXGBE_LSECRXDELAY	0x08F5C /* InPktsDelayed */
#define IXGBE_LSECRXLATE	0x08F60 /* InPktsLate */
#define IXGBE_LSECRXOK(_n)	(0x08F64 + (0x04 * (_n))) /* InPktsOk */
#define IXGBE_LSECRXINV(_n)	(0x08F6C + (0x04 * (_n))) /* InPktsInvalid */
#define IXGBE_LSECRXNV(_n)	(0x08F74 + (0x04 * (_n))) /* InPktsNotValid */
#define IXGBE_LSECRXUNSA	0x08F7C /* InPktsUnusedSa */
#define IXGBE_LSECRXNUSA	0x08F80 /* InPktsNotUsingSa */

/* LinkSec (MacSec) Bit Fields and Masks */
#define IXGBE_LSECTXCAP_SUM_MASK	0x00FF0000
#define IXGBE_LSECTXCAP_SUM_SHIFT	16
#define IXGBE_LSECRXCAP_SUM_MASK	0x00FF0000
#define IXGBE_LSECRXCAP_SUM_SHIFT	16

#define IXGBE_LSECTXCTRL_EN_MASK	0x00000003
#define IXGBE_LSECTXCTRL_DISABLE	0x0
#define IXGBE_LSECTXCTRL_AUTH		0x1
#define IXGBE_LSECTXCTRL_AUTH_ENCRYPT	0x2
#define IXGBE_LSECTXCTRL_AISCI		0x00000020
#define IXGBE_LSECTXCTRL_PNTHRSH_MASK	0xFFFFFF00
#define IXGBE_LSECTXCTRL_RSV_MASK	0x000000D8

#define IXGBE_LSECRXCTRL_EN_MASK	0x0000000C
#define IXGBE_LSECRXCTRL_EN_SHIFT	2
#define IXGBE_LSECRXCTRL_DISABLE	0x0
#define IXGBE_LSECRXCTRL_CHECK		0x1
#define IXGBE_LSECRXCTRL_STRICT		0x2
#define IXGBE_LSECRXCTRL_DROP		0x3
#define IXGBE_LSECRXCTRL_PLSH		0x00000040
#define IXGBE_LSECRXCTRL_RP		0x00000080
#define IXGBE_LSECRXCTRL_RSV_MASK	0xFFFFFF33

/* IpSec Registers */
#define IXGBE_IPSTXIDX		0x08900
#define IXGBE_IPSTXSALT		0x08904
#define IXGBE_IPSTXKEY(_i)	(0x08908 + (4 * (_i))) /* 4 of these (0-3) */
#define IXGBE_IPSRXIDX		0x08E00
#define IXGBE_IPSRXIPADDR(_i)	(0x08E04 + (4 * (_i))) /* 4 of these (0-3) */
#define IXGBE_IPSRXSPI		0x08E14
#define IXGBE_IPSRXIPIDX	0x08E18
#define IXGBE_IPSRXKEY(_i)	(0x08E1C + (4 * (_i))) /* 4 of these (0-3) */
#define IXGBE_IPSRXSALT		0x08E2C
#define IXGBE_IPSRXMOD		0x08E30

#define IXGBE_SECTXCTRL_STORE_FORWARD_ENABLE	0x4

/* DCB registers */
#define IXGBE_RTRPCS		0x02430
#define IXGBE_RTTDCS		0x04900
#define IXGBE_RTTDCS_ARBDIS	0x00000040 /* DCB arbiter disable */
#define IXGBE_RTTPCS		0x0CD00
#define IXGBE_RTRUP2TC		0x03020
#define IXGBE_RTTUP2TC		0x0C800
#define IXGBE_RTRPT4C(_i)	(0x02140 + ((_i) * 4)) /* 8 of these (0-7) */
#define IXGBE_TXLLQ(_i)		(0x082E0 + ((_i) * 4)) /* 4 of these (0-3) */
#define IXGBE_RTRPT4S(_i)	(0x02160 + ((_i) * 4)) /* 8 of these (0-7) */
#define IXGBE_RTTDT2C(_i)	(0x04910 + ((_i) * 4)) /* 8 of these (0-7) */
#define IXGBE_RTTDT2S(_i)	(0x04930 + ((_i) * 4)) /* 8 of these (0-7) */
#define IXGBE_RTTPT2C(_i)	(0x0CD20 + ((_i) * 4)) /* 8 of these (0-7) */
#define IXGBE_RTTPT2S(_i)	(0x0CD40 + ((_i) * 4)) /* 8 of these (0-7) */
#define IXGBE_RTTDQSEL		0x04904
#define IXGBE_RTTDT1C		0x04908
#define IXGBE_RTTDT1S		0x0490C
#define IXGBE_RTTDTECC		0x04990
#define IXGBE_RTTDTECC_NO_BCN	0x00000100

#define IXGBE_RTTBCNRC			0x04984
#define IXGBE_RTTBCNRC_RS_ENA		0x80000000
#define IXGBE_RTTBCNRC_RF_DEC_MASK	0x00003FFF
#define IXGBE_RTTBCNRC_RF_INT_SHIFT	14
#define IXGBE_RTTBCNRC_RF_INT_MASK \
	(IXGBE_RTTBCNRC_RF_DEC_MASK << IXGBE_RTTBCNRC_RF_INT_SHIFT)
#define IXGBE_RTTBCNRM	0x04980

/* BCN (for DCB) Registers */
#define IXGBE_RTTBCNRS	0x04988
#define IXGBE_RTTBCNCR	0x08B00
#define IXGBE_RTTBCNACH	0x08B04
#define IXGBE_RTTBCNACL	0x08B08
#define IXGBE_RTTBCNTG	0x04A90
#define IXGBE_RTTBCNIDX	0x08B0C
#define IXGBE_RTTBCNCP	0x08B10
#define IXGBE_RTFRTIMER	0x08B14
#define IXGBE_RTTBCNRTT	0x05150
#define IXGBE_RTTBCNRD	0x0498C

/* FCoE DMA Context Registers */
/* FCoE Direct DMA Context */
#define IXGBE_FCDDC(_i, _j)	(0x20000 + ((_i) * 0x4) + ((_j) * 0x10))
#define IXGBE_FCPTRL		0x02410 /* FC User Desc. PTR Low */
#define IXGBE_FCPTRH		0x02414 /* FC USer Desc. PTR High */
#define IXGBE_FCBUFF		0x02418 /* FC Buffer Control */
#define IXGBE_FCDMARW		0x02420 /* FC Receive DMA RW */
#define IXGBE_FCBUFF_VALID	(1 << 0)   /* DMA Context Valid */
#define IXGBE_FCBUFF_BUFFSIZE	(3 << 3)   /* User Buffer Size */
#define IXGBE_FCBUFF_WRCONTX	(1 << 7)   /* 0: Initiator, 1: Target */
#define IXGBE_FCBUFF_BUFFCNT	0x0000ff00 /* Number of User Buffers */
#define IXGBE_FCBUFF_OFFSET	0xffff0000 /* User Buffer Offset */
#define IXGBE_FCBUFF_BUFFSIZE_SHIFT	3
#define IXGBE_FCBUFF_BUFFCNT_SHIFT	8
#define IXGBE_FCBUFF_OFFSET_SHIFT	16
#define IXGBE_FCDMARW_WE		(1 << 14)   /* Write enable */
#define IXGBE_FCDMARW_RE		(1 << 15)   /* Read enable */
#define IXGBE_FCDMARW_FCOESEL		0x000001ff  /* FC X_ID: 11 bits */
#define IXGBE_FCDMARW_LASTSIZE		0xffff0000  /* Last User Buffer Size */
#define IXGBE_FCDMARW_LASTSIZE_SHIFT	16
/* FCoE SOF/EOF */
#define IXGBE_TEOFF		0x04A94 /* Tx FC EOF */
#define IXGBE_TSOFF		0x04A98 /* Tx FC SOF */
#define IXGBE_REOFF		0x05158 /* Rx FC EOF */
#define IXGBE_RSOFF		0x051F8 /* Rx FC SOF */
/* FCoE Filter Context Registers */
#define IXGBE_FCD_ID		0x05114 /* FCoE D_ID */
#define IXGBE_FCSMAC		0x0510C /* FCoE Source MAC */
#define IXGBE_FCFLTRW_SMAC_HIGH_SHIFT	16
/* FCoE Direct Filter Context */
#define IXGBE_FCDFC(_i, _j)	(0x28000 + ((_i) * 0x4) + ((_j) * 0x10))
#define IXGBE_FCDFCD(_i)	(0x30000 + ((_i) * 0x4))
#define IXGBE_FCFLT		0x05108 /* FC FLT Context */
#define IXGBE_FCFLTRW		0x05110 /* FC Filter RW Control */
#define IXGBE_FCPARAM		0x051d8 /* FC Offset Parameter */
#define IXGBE_FCFLT_VALID	(1 << 0)   /* Filter Context Valid */
#define IXGBE_FCFLT_FIRST	(1 << 1)   /* Filter First */
#define IXGBE_FCFLT_SEQID	0x00ff0000 /* Sequence ID */
#define IXGBE_FCFLT_SEQCNT	0xff000000 /* Sequence Count */
#define IXGBE_FCFLTRW_RVALDT	(1 << 13)  /* Fast Re-Validation */
#define IXGBE_FCFLTRW_WE	(1 << 14)  /* Write Enable */
#define IXGBE_FCFLTRW_RE	(1 << 15)  /* Read Enable */
/* FCoE Receive Control */
#define IXGBE_FCRXCTRL		0x05100 /* FC Receive Control */
#define IXGBE_FCRXCTRL_FCOELLI	(1 << 0)   /* Low latency interrupt */
#define IXGBE_FCRXCTRL_SAVBAD	(1 << 1)   /* Save Bad Frames */
#define IXGBE_FCRXCTRL_FRSTRDH	(1 << 2)   /* EN 1st Read Header */
#define IXGBE_FCRXCTRL_LASTSEQH	(1 << 3)   /* EN Last Header in Seq */
#define IXGBE_FCRXCTRL_ALLH	(1 << 4)   /* EN All Headers */
#define IXGBE_FCRXCTRL_FRSTSEQH	(1 << 5)   /* EN 1st Seq. Header */
#define IXGBE_FCRXCTRL_ICRC	(1 << 6)   /* Ignore Bad FC CRC */
#define IXGBE_FCRXCTRL_FCCRCBO	(1 << 7)   /* FC CRC Byte Ordering */
#define IXGBE_FCRXCTRL_FCOEVER	0x00000f00 /* FCoE Version: 4 bits */
#define IXGBE_FCRXCTRL_FCOEVER_SHIFT	8
/* FCoE Redirection */
#define IXGBE_FCRECTL		0x0ED00 /* FC Redirection Control */
#define IXGBE_FCRETA0		0x0ED10 /* FC Redirection Table 0 */
#define IXGBE_FCRETA(_i)	(IXGBE_FCRETA0 + ((_i) * 4)) /* FCoE Redir */
#define IXGBE_FCRECTL_ENA	0x1 /* FCoE Redir Table Enable */
#define IXGBE_FCRETASEL_ENA	0x2 /* FCoE FCRETASEL bit */
#define IXGBE_FCRETA_SIZE	8 /* Max entries in FCRETA */
#define IXGBE_FCRETA_ENTRY_MASK	0x0000007f /* 7 bits for the queue index */
#define IXGBE_FCRETA_SIZE_X550	32 /* Max entries in FCRETA */
/* Higher 7 bits for the queue index */
#define IXGBE_FCRETA_ENTRY_HIGH_MASK	0x007F0000
#define IXGBE_FCRETA_ENTRY_HIGH_SHIFT	16

/* Stats registers */
#define IXGBE_CRCERRS	0x04000
#define IXGBE_ILLERRC	0x04004
#define IXGBE_ERRBC	0x04008
#define IXGBE_MSPDC	0x04010
#define IXGBE_MPC(_i)	(0x03FA0 + ((_i) * 4)) /* 8 of these 3FA0-3FBC*/
#define IXGBE_MLFC	0x04034
#define IXGBE_MRFC	0x04038
#define IXGBE_RLEC	0x04040
#define IXGBE_LXONTXC	0x03F60
#define IXGBE_LXONRXC	0x0CF60
#define IXGBE_LXOFFTXC	0x03F68
#define IXGBE_LXOFFRXC	0x0CF68
#define IXGBE_LXONRXCNT		0x041A4
#define IXGBE_LXOFFRXCNT	0x041A8
#define IXGBE_PXONRXCNT(_i)	(0x04140 + ((_i) * 4)) /* 8 of these */
#define IXGBE_PXOFFRXCNT(_i)	(0x04160 + ((_i) * 4)) /* 8 of these */
#define IXGBE_PXON2OFFCNT(_i)	(0x03240 + ((_i) * 4)) /* 8 of these */
#define IXGBE_PXONTXC(_i)	(0x03F00 + ((_i) * 4)) /* 8 of these 3F00-3F1C*/
#define IXGBE_PXONRXC(_i)	(0x0CF00 + ((_i) * 4)) /* 8 of these CF00-CF1C*/
#define IXGBE_PXOFFTXC(_i)	(0x03F20 + ((_i) * 4)) /* 8 of these 3F20-3F3C*/
#define IXGBE_PXOFFRXC(_i)	(0x0CF20 + ((_i) * 4)) /* 8 of these CF20-CF3C*/
#define IXGBE_PRC64		0x0405C
#define IXGBE_PRC127		0x04060
#define IXGBE_PRC255		0x04064
#define IXGBE_PRC511		0x04068
#define IXGBE_PRC1023		0x0406C
#define IXGBE_PRC1522		0x04070
#define IXGBE_GPRC		0x04074
#define IXGBE_BPRC		0x04078
#define IXGBE_MPRC		0x0407C
#define IXGBE_GPTC		0x04080
#define IXGBE_GORCL		0x04088
#define IXGBE_GORCH		0x0408C
#define IXGBE_GOTCL		0x04090
#define IXGBE_GOTCH		0x04094
#define IXGBE_RNBC(_i)		(0x03FC0 + ((_i) * 4)) /* 8 of these 3FC0-3FDC*/
#define IXGBE_RUC		0x040A4
#define IXGBE_RFC		0x040A8
#define IXGBE_ROC		0x040AC
#define IXGBE_RJC		0x040B0
#define IXGBE_MNGPRC		0x040B4
#define IXGBE_MNGPDC		0x040B8
#define IXGBE_MNGPTC		0x0CF90
#define IXGBE_TORL		0x040C0
#define IXGBE_TORH		0x040C4
#define IXGBE_TPR		0x040D0
#define IXGBE_TPT		0x040D4
#define IXGBE_PTC64		0x040D8
#define IXGBE_PTC127		0x040DC
#define IXGBE_PTC255		0x040E0
#define IXGBE_PTC511		0x040E4
#define IXGBE_PTC1023		0x040E8
#define IXGBE_PTC1522		0x040EC
#define IXGBE_MPTC		0x040F0
#define IXGBE_BPTC		0x040F4
#define IXGBE_XEC		0x04120
#define IXGBE_SSVPC		0x08780

#define IXGBE_RQSMR(_i)	(0x02300 + ((_i) * 4))
#define IXGBE_TQSMR(_i)	(((_i) <= 7) ? (0x07300 + ((_i) * 4)) : \
			 (0x08600 + ((_i) * 4)))
#define IXGBE_TQSM(_i)	(0x08600 + ((_i) * 4))

#define IXGBE_QPRC(_i)	(0x01030 + ((_i) * 0x40)) /* 16 of these */
#define IXGBE_QPTC(_i)	(0x06030 + ((_i) * 0x40)) /* 16 of these */
#define IXGBE_QBRC(_i)	(0x01034 + ((_i) * 0x40)) /* 16 of these */
#define IXGBE_QBTC(_i)	(0x06034 + ((_i) * 0x40)) /* 16 of these */
#define IXGBE_QBRC_L(_i)	(0x01034 + ((_i) * 0x40)) /* 16 of these */
#define IXGBE_QBRC_H(_i)	(0x01038 + ((_i) * 0x40)) /* 16 of these */
#define IXGBE_QPRDC(_i)		(0x01430 + ((_i) * 0x40)) /* 16 of these */
#define IXGBE_QBTC_L(_i)	(0x08700 + ((_i) * 0x8)) /* 16 of these */
#define IXGBE_QBTC_H(_i)	(0x08704 + ((_i) * 0x8)) /* 16 of these */
#define IXGBE_FCCRC		0x05118 /* Num of Good Eth CRC w/ Bad FC CRC */
#define IXGBE_FCOERPDC		0x0241C /* FCoE Rx Packets Dropped Count */
#define IXGBE_FCLAST		0x02424 /* FCoE Last Error Count */
#define IXGBE_FCOEPRC		0x02428 /* Number of FCoE Packets Received */
#define IXGBE_FCOEDWRC		0x0242C /* Number of FCoE DWords Received */
#define IXGBE_FCOEPTC		0x08784 /* Number of FCoE Packets Transmitted */
#define IXGBE_FCOEDWTC		0x08788 /* Number of FCoE DWords Transmitted */
#define IXGBE_FCCRC_CNT_MASK	0x0000FFFF /* CRC_CNT: bit 0 - 15 */
#define IXGBE_FCLAST_CNT_MASK	0x0000FFFF /* Last_CNT: bit 0 - 15 */
#define IXGBE_O2BGPTC		0x041C4
#define IXGBE_O2BSPC		0x087B0
#define IXGBE_B2OSPC		0x041C0
#define IXGBE_B2OGPRC		0x02F90
#define IXGBE_BUPRC		0x04180
#define IXGBE_BMPRC		0x04184
#define IXGBE_BBPRC		0x04188
#define IXGBE_BUPTC		0x0418C
#define IXGBE_BMPTC		0x04190
#define IXGBE_BBPTC		0x04194
#define IXGBE_BCRCERRS		0x04198
#define IXGBE_BXONRXC		0x0419C
#define IXGBE_BXOFFRXC		0x041E0
#define IXGBE_BXONTXC		0x041E4
#define IXGBE_BXOFFTXC		0x041E8

/* Management */
#define IXGBE_MAVTV(_i)		(0x05010 + ((_i) * 4)) /* 8 of these (0-7) */
#define IXGBE_MFUTP(_i)		(0x05030 + ((_i) * 4)) /* 8 of these (0-7) */
#define IXGBE_MANC		0x05820
#define IXGBE_MFVAL		0x05824
#define IXGBE_MANC2H		0x05860
#define IXGBE_MDEF(_i)		(0x05890 + ((_i) * 4)) /* 8 of these (0-7) */
#define IXGBE_MIPAF		0x058B0
#define IXGBE_MMAL(_i)		(0x05910 + ((_i) * 8)) /* 4 of these (0-3) */
#define IXGBE_MMAH(_i)		(0x05914 + ((_i) * 8)) /* 4 of these (0-3) */
#define IXGBE_FTFT		0x09400 /* 0x9400-0x97FC */
#define IXGBE_METF(_i)		(0x05190 + ((_i) * 4)) /* 4 of these (0-3) */
#define IXGBE_MDEF_EXT(_i)	(0x05160 + ((_i) * 4)) /* 8 of these (0-7) */
#define IXGBE_LSWFW		0x15F14
#define IXGBE_BMCIP(_i)		(0x05050 + ((_i) * 4)) /* 0x5050-0x505C */
#define IXGBE_BMCIPVAL		0x05060
#define IXGBE_BMCIP_IPADDR_TYPE	0x00000001
#define IXGBE_BMCIP_IPADDR_VALID	0x00000002

/* Management Bit Fields and Masks */
#define IXGBE_MANC_MPROXYE	0x40000000 /* Management Proxy Enable */
#define IXGBE_MANC_RCV_TCO_EN	0x00020000 /* Rcv TCO packet enable */
#define IXGBE_MANC_EN_BMC2OS	0x10000000 /* Ena BMC2OS and OS2BMC traffic */
#define IXGBE_MANC_EN_BMC2OS_SHIFT	28

/* Firmware Semaphore Register */
#define IXGBE_FWSM_MODE_MASK	0xE
#define IXGBE_FWSM_TS_ENABLED	0x1
#define IXGBE_FWSM_FW_MODE_PT	0x4

/* ARC Subsystem registers */
#define IXGBE_HICR		0x15F00
#define IXGBE_FWSTS		0x15F0C
#define IXGBE_HSMC0R		0x15F04
#define IXGBE_HSMC1R		0x15F08
#define IXGBE_SWSR		0x15F10
#define IXGBE_HFDR		0x15FE8
#define IXGBE_FLEX_MNG		0x15800 /* 0x15800 - 0x15EFC */

#define IXGBE_HICR_EN		0x01  /* Enable bit - RO */
/* Driver sets this bit when done to put command in RAM */
#define IXGBE_HICR_C		0x02
#define IXGBE_HICR_SV		0x04  /* Status Validity */
#define IXGBE_HICR_FW_RESET_ENABLE	0x40
#define IXGBE_HICR_FW_RESET	0x80

/* PCI-E registers */
#define IXGBE_GCR		0x11000
#define IXGBE_GTV		0x11004
#define IXGBE_FUNCTAG		0x11008
#define IXGBE_GLT		0x1100C
#define IXGBE_PCIEPIPEADR	0x11004
#define IXGBE_PCIEPIPEDAT	0x11008
#define IXGBE_GSCL_1		0x11010
#define IXGBE_GSCL_2		0x11014
#define IXGBE_GSCL_1_X540	IXGBE_GSCL_1
#define IXGBE_GSCL_2_X540	IXGBE_GSCL_2
#define IXGBE_GSCL_3		0x11018
#define IXGBE_GSCL_4		0x1101C
#define IXGBE_GSCN_0		0x11020
#define IXGBE_GSCN_1		0x11024
#define IXGBE_GSCN_2		0x11028
#define IXGBE_GSCN_3		0x1102C
#define IXGBE_GSCN_0_X540	IXGBE_GSCN_0
#define IXGBE_GSCN_1_X540	IXGBE_GSCN_1
#define IXGBE_GSCN_2_X540	IXGBE_GSCN_2
#define IXGBE_GSCN_3_X540	IXGBE_GSCN_3
#define IXGBE_FACTPS		0x10150
#define IXGBE_FACTPS_X540	IXGBE_FACTPS
#define IXGBE_GSCL_1_X550	0x11800
#define IXGBE_GSCL_2_X550	0x11804
#define IXGBE_GSCL_1_X550EM_x	IXGBE_GSCL_1_X550
#define IXGBE_GSCL_2_X550EM_x	IXGBE_GSCL_2_X550
#define IXGBE_GSCN_0_X550	0x11820
#define IXGBE_GSCN_1_X550	0x11824
#define IXGBE_GSCN_2_X550	0x11828
#define IXGBE_GSCN_3_X550	0x1182C
#define IXGBE_GSCN_0_X550EM_x	IXGBE_GSCN_0_X550
#define IXGBE_GSCN_1_X550EM_x	IXGBE_GSCN_1_X550
#define IXGBE_GSCN_2_X550EM_x	IXGBE_GSCN_2_X550
#define IXGBE_GSCN_3_X550EM_x	IXGBE_GSCN_3_X550
#define IXGBE_FACTPS_X550	IXGBE_FACTPS
#define IXGBE_FACTPS_X550EM_x	IXGBE_FACTPS
#define IXGBE_GSCL_1_X550EM_a	IXGBE_GSCL_1_X550
#define IXGBE_GSCL_2_X550EM_a	IXGBE_GSCL_2_X550
#define IXGBE_GSCN_0_X550EM_a	IXGBE_GSCN_0_X550
#define IXGBE_GSCN_1_X550EM_a	IXGBE_GSCN_1_X550
#define IXGBE_GSCN_2_X550EM_a	IXGBE_GSCN_2_X550
#define IXGBE_GSCN_3_X550EM_a	IXGBE_GSCN_3_X550
#define IXGBE_FACTPS_X550EM_a	0x15FEC
#define IXGBE_FACTPS_BY_MAC(_hw)	IXGBE_BY_MAC((_hw), FACTPS)

#define IXGBE_PCIEANACTL	0x11040
#define IXGBE_SWSM		0x10140
#define IXGBE_SWSM_X540		IXGBE_SWSM
#define IXGBE_SWSM_X550		IXGBE_SWSM
#define IXGBE_SWSM_X550EM_x	IXGBE_SWSM
#define IXGBE_SWSM_X550EM_a	0x15F70
#define IXGBE_SWSM_BY_MAC(_hw)	IXGBE_BY_MAC((_hw), SWSM)

#define IXGBE_FWSM		0x10148
#define IXGBE_FWSM_X540		IXGBE_FWSM
#define IXGBE_FWSM_X550		IXGBE_FWSM
#define IXGBE_FWSM_X550EM_x	IXGBE_FWSM
#define IXGBE_FWSM_X550EM_a	0x15F74
#define IXGBE_FWSM_BY_MAC(_hw)	IXGBE_BY_MAC((_hw), FWSM)

#define IXGBE_SWFW_SYNC		IXGBE_GSSR
#define IXGBE_SWFW_SYNC_X540	IXGBE_SWFW_SYNC
#define IXGBE_SWFW_SYNC_X550	IXGBE_SWFW_SYNC
#define IXGBE_SWFW_SYNC_X550EM_x	IXGBE_SWFW_SYNC
#define IXGBE_SWFW_SYNC_X550EM_a	0x15F78
#define IXGBE_SWFW_SYNC_BY_MAC(_hw)	IXGBE_BY_MAC((_hw), SWFW_SYNC)

#define IXGBE_GSSR		0x10160
#define IXGBE_MREVID		0x11064
#define IXGBE_DCA_ID		0x11070
#define IXGBE_DCA_CTRL		0x11074

/* PCI-E registers 82599-Specific */
#define IXGBE_GCR_EXT		0x11050
#define IXGBE_GSCL_5_82599	0x11030
#define IXGBE_GSCL_6_82599	0x11034
#define IXGBE_GSCL_7_82599	0x11038
#define IXGBE_GSCL_8_82599	0x1103C
#define IXGBE_GSCL_5_X540	IXGBE_GSCL_5_82599
#define IXGBE_GSCL_6_X540	IXGBE_GSCL_6_82599
#define IXGBE_GSCL_7_X540	IXGBE_GSCL_7_82599
#define IXGBE_GSCL_8_X540	IXGBE_GSCL_8_82599
#define IXGBE_PHYADR_82599	0x11040
#define IXGBE_PHYDAT_82599	0x11044
#define IXGBE_PHYCTL_82599	0x11048
#define IXGBE_PBACLR_82599	0x11068
#define IXGBE_CIAA		0x11088
#define IXGBE_CIAD		0x1108C
#define IXGBE_CIAA_82599	IXGBE_CIAA
#define IXGBE_CIAD_82599	IXGBE_CIAD
#define IXGBE_CIAA_X540		IXGBE_CIAA
#define IXGBE_CIAD_X540		IXGBE_CIAD
#define IXGBE_GSCL_5_X550	0x11810
#define IXGBE_GSCL_6_X550	0x11814
#define IXGBE_GSCL_7_X550	0x11818
#define IXGBE_GSCL_8_X550	0x1181C
#define IXGBE_GSCL_5_X550EM_x	IXGBE_GSCL_5_X550
#define IXGBE_GSCL_6_X550EM_x	IXGBE_GSCL_6_X550
#define IXGBE_GSCL_7_X550EM_x	IXGBE_GSCL_7_X550
#define IXGBE_GSCL_8_X550EM_x	IXGBE_GSCL_8_X550
#define IXGBE_CIAA_X550		0x11508
#define IXGBE_CIAD_X550		0x11510
#define IXGBE_CIAA_X550EM_x	IXGBE_CIAA_X550
#define IXGBE_CIAD_X550EM_x	IXGBE_CIAD_X550
#define IXGBE_GSCL_5_X550EM_a	IXGBE_GSCL_5_X550
#define IXGBE_GSCL_6_X550EM_a	IXGBE_GSCL_6_X550
#define IXGBE_GSCL_7_X550EM_a	IXGBE_GSCL_7_X550
#define IXGBE_GSCL_8_X550EM_a	IXGBE_GSCL_8_X550
#define IXGBE_CIAA_X550EM_a	IXGBE_CIAA_X550
#define IXGBE_CIAD_X550EM_a	IXGBE_CIAD_X550
#define IXGBE_CIAA_BY_MAC(_hw)	IXGBE_BY_MAC((_hw), CIAA)
#define IXGBE_CIAD_BY_MAC(_hw)	IXGBE_BY_MAC((_hw), CIAD)
#define IXGBE_PICAUSE		0x110B0
#define IXGBE_PIENA		0x110B8
#define IXGBE_CDQ_MBR_82599	0x110B4
#define IXGBE_PCIESPARE		0x110BC
#define IXGBE_MISC_REG_82599	0x110F0
#define IXGBE_ECC_CTRL_0_82599	0x11100
#define IXGBE_ECC_CTRL_1_82599	0x11104
#define IXGBE_ECC_STATUS_82599	0x110E0
#define IXGBE_BAR_CTRL_82599	0x110F4

/* PCI Express Control */
#define IXGBE_GCR_CMPL_TMOUT_MASK	0x0000F000
#define IXGBE_GCR_CMPL_TMOUT_10ms	0x00001000
#define IXGBE_GCR_CMPL_TMOUT_RESEND	0x00010000
#define IXGBE_GCR_CAP_VER2		0x00040000

#define IXGBE_GCR_EXT_MSIX_EN		0x80000000
#define IXGBE_GCR_EXT_BUFFERS_CLEAR	0x40000000
#define IXGBE_GCR_EXT_VT_MODE_16	0x00000001
#define IXGBE_GCR_EXT_VT_MODE_32	0x00000002
#define IXGBE_GCR_EXT_VT_MODE_64	0x00000003
#define IXGBE_GCR_EXT_SRIOV		(IXGBE_GCR_EXT_MSIX_EN | \
					 IXGBE_GCR_EXT_VT_MODE_64)
#define IXGBE_GCR_EXT_VT_MODE_MASK	0x00000003
/* Time Sync Registers */
#define IXGBE_TSYNCRXCTL	0x05188 /* Rx Time Sync Control register - RW */
#define IXGBE_TSYNCTXCTL	0x08C00 /* Tx Time Sync Control register - RW */
#define IXGBE_RXSTMPL	0x051E8 /* Rx timestamp Low - RO */
#define IXGBE_RXSTMPH	0x051A4 /* Rx timestamp High - RO */
#define IXGBE_RXSATRL	0x051A0 /* Rx timestamp attribute low - RO */
#define IXGBE_RXSATRH	0x051A8 /* Rx timestamp attribute high - RO */
#define IXGBE_RXMTRL	0x05120 /* RX message type register low - RW */
#define IXGBE_TXSTMPL	0x08C04 /* Tx timestamp value Low - RO */
#define IXGBE_TXSTMPH	0x08C08 /* Tx timestamp value High - RO */
#define IXGBE_SYSTIML	0x08C0C /* System time register Low - RO */
#define IXGBE_SYSTIMH	0x08C10 /* System time register High - RO */
#define IXGBE_SYSTIMR	0x08C58 /* System time register Residue - RO */
#define IXGBE_TIMINCA	0x08C14 /* Increment attributes register - RW */
#define IXGBE_TIMADJL	0x08C18 /* Time Adjustment Offset register Low - RW */
#define IXGBE_TIMADJH	0x08C1C /* Time Adjustment Offset register High - RW */
#define IXGBE_TSAUXC	0x08C20 /* TimeSync Auxiliary Control register - RW */
#define IXGBE_TRGTTIML0	0x08C24 /* Target Time Register 0 Low - RW */
#define IXGBE_TRGTTIMH0	0x08C28 /* Target Time Register 0 High - RW */
#define IXGBE_TRGTTIML1	0x08C2C /* Target Time Register 1 Low - RW */
#define IXGBE_TRGTTIMH1	0x08C30 /* Target Time Register 1 High - RW */
#define IXGBE_CLKTIML	0x08C34 /* Clock Out Time Register Low - RW */
#define IXGBE_CLKTIMH	0x08C38 /* Clock Out Time Register High - RW */
#define IXGBE_FREQOUT0	0x08C34 /* Frequency Out 0 Control register - RW */
#define IXGBE_FREQOUT1	0x08C38 /* Frequency Out 1 Control register - RW */
#define IXGBE_AUXSTMPL0	0x08C3C /* Auxiliary Time Stamp 0 register Low - RO */
#define IXGBE_AUXSTMPH0	0x08C40 /* Auxiliary Time Stamp 0 register High - RO */
#define IXGBE_AUXSTMPL1	0x08C44 /* Auxiliary Time Stamp 1 register Low - RO */
#define IXGBE_AUXSTMPH1	0x08C48 /* Auxiliary Time Stamp 1 register High - RO */
#define IXGBE_TSIM	0x08C68 /* TimeSync Interrupt Mask Register - RW */
#define IXGBE_TSICR	0x08C60 /* TimeSync Interrupt Cause Register - WO */
#define IXGBE_TSSDP	0x0003C /* TimeSync SDP Configuration Register - RW */

/* Diagnostic Registers */
#define IXGBE_RDSTATCTL		0x02C20
#define IXGBE_RDSTAT(_i)	(0x02C00 + ((_i) * 4)) /* 0x02C00-0x02C1C */
#define IXGBE_RDHMPN		0x02F08
#define IXGBE_RIC_DW(_i)	(0x02F10 + ((_i) * 4))
#define IXGBE_RDPROBE		0x02F20
#define IXGBE_RDMAM		0x02F30
#define IXGBE_RDMAD		0x02F34
#define IXGBE_TDHMPN		0x07F08
#define IXGBE_TDHMPN2		0x082FC
#define IXGBE_TXDESCIC		0x082CC
#define IXGBE_TIC_DW(_i)	(0x07F10 + ((_i) * 4))
#define IXGBE_TIC_DW2(_i)	(0x082B0 + ((_i) * 4))
#define IXGBE_TDPROBE		0x07F20
#define IXGBE_TXBUFCTRL		0x0C600
#define IXGBE_TXBUFDATA0	0x0C610
#define IXGBE_TXBUFDATA1	0x0C614
#define IXGBE_TXBUFDATA2	0x0C618
#define IXGBE_TXBUFDATA3	0x0C61C
#define IXGBE_RXBUFCTRL		0x03600
#define IXGBE_RXBUFDATA0	0x03610
#define IXGBE_RXBUFDATA1	0x03614
#define IXGBE_RXBUFDATA2	0x03618
#define IXGBE_RXBUFDATA3	0x0361C
#define IXGBE_PCIE_DIAG(_i)	(0x11090 + ((_i) * 4)) /* 8 of these */
#define IXGBE_RFVAL		0x050A4
#define IXGBE_MDFTC1		0x042B8
#define IXGBE_MDFTC2		0x042C0
#define IXGBE_MDFTFIFO1		0x042C4
#define IXGBE_MDFTFIFO2		0x042C8
#define IXGBE_MDFTS		0x042CC
#define IXGBE_RXDATAWRPTR(_i)	(0x03700 + ((_i) * 4)) /* 8 of these 3700-370C*/
#define IXGBE_RXDESCWRPTR(_i)	(0x03710 + ((_i) * 4)) /* 8 of these 3710-371C*/
#define IXGBE_RXDATARDPTR(_i)	(0x03720 + ((_i) * 4)) /* 8 of these 3720-372C*/
#define IXGBE_RXDESCRDPTR(_i)	(0x03730 + ((_i) * 4)) /* 8 of these 3730-373C*/
#define IXGBE_TXDATAWRPTR(_i)	(0x0C700 + ((_i) * 4)) /* 8 of these C700-C70C*/
#define IXGBE_TXDESCWRPTR(_i)	(0x0C710 + ((_i) * 4)) /* 8 of these C710-C71C*/
#define IXGBE_TXDATARDPTR(_i)	(0x0C720 + ((_i) * 4)) /* 8 of these C720-C72C*/
#define IXGBE_TXDESCRDPTR(_i)	(0x0C730 + ((_i) * 4)) /* 8 of these C730-C73C*/
#define IXGBE_PCIEECCCTL	0x1106C
#define IXGBE_RXWRPTR(_i)	(0x03100 + ((_i) * 4)) /* 8 of these 3100-310C*/
#define IXGBE_RXUSED(_i)	(0x03120 + ((_i) * 4)) /* 8 of these 3120-312C*/
#define IXGBE_RXRDPTR(_i)	(0x03140 + ((_i) * 4)) /* 8 of these 3140-314C*/
#define IXGBE_RXRDWRPTR(_i)	(0x03160 + ((_i) * 4)) /* 8 of these 3160-310C*/
#define IXGBE_TXWRPTR(_i)	(0x0C100 + ((_i) * 4)) /* 8 of these C100-C10C*/
#define IXGBE_TXUSED(_i)	(0x0C120 + ((_i) * 4)) /* 8 of these C120-C12C*/
#define IXGBE_TXRDPTR(_i)	(0x0C140 + ((_i) * 4)) /* 8 of these C140-C14C*/
#define IXGBE_TXRDWRPTR(_i)	(0x0C160 + ((_i) * 4)) /* 8 of these C160-C10C*/
#define IXGBE_PCIEECCCTL0	0x11100
#define IXGBE_PCIEECCCTL1	0x11104
#define IXGBE_RXDBUECC		0x03F70
#define IXGBE_TXDBUECC		0x0CF70
#define IXGBE_RXDBUEST		0x03F74
#define IXGBE_TXDBUEST		0x0CF74
#define IXGBE_PBTXECC		0x0C300
#define IXGBE_PBRXECC		0x03300
#define IXGBE_GHECCR		0x110B0

/* MAC Registers */
#define IXGBE_PCS1GCFIG		0x04200
#define IXGBE_PCS1GLCTL		0x04208
#define IXGBE_PCS1GLSTA		0x0420C
#define IXGBE_PCS1GDBG0		0x04210
#define IXGBE_PCS1GDBG1		0x04214
#define IXGBE_PCS1GANA		0x04218
#define IXGBE_PCS1GANLP		0x0421C
#define IXGBE_PCS1GANNP		0x04220
#define IXGBE_PCS1GANLPNP	0x04224
#define IXGBE_HLREG0		0x04240
#define IXGBE_HLREG1		0x04244
#define IXGBE_PAP		0x04248
#define IXGBE_MACA		0x0424C
#define IXGBE_APAE		0x04250
#define IXGBE_ARD		0x04254
#define IXGBE_AIS		0x04258
#define IXGBE_MSCA		0x0425C
#define IXGBE_MSRWD		0x04260
#define IXGBE_MLADD		0x04264
#define IXGBE_MHADD		0x04268
#define IXGBE_MAXFRS		0x04268
#define IXGBE_TREG		0x0426C
#define IXGBE_PCSS1		0x04288
#define IXGBE_PCSS2		0x0428C
#define IXGBE_XPCSS		0x04290
#define IXGBE_MFLCN		0x04294
#define IXGBE_SERDESC		0x04298
#define IXGBE_MAC_SGMII_BUSY	0x04298
#define IXGBE_MACS		0x0429C
#define IXGBE_AUTOC		0x042A0
#define IXGBE_LINKS		0x042A4
#define IXGBE_LINKS2		0x04324
#define IXGBE_AUTOC2		0x042A8
#define IXGBE_AUTOC3		0x042AC
#define IXGBE_ANLP1		0x042B0
#define IXGBE_ANLP2		0x042B4
#define IXGBE_MACC		0x04330
#define IXGBE_ATLASCTL		0x04800
#define IXGBE_MMNGC		0x042D0
#define IXGBE_ANLPNP1		0x042D4
#define IXGBE_ANLPNP2		0x042D8
#define IXGBE_KRPCSFC		0x042E0
#define IXGBE_KRPCSS		0x042E4
#define IXGBE_FECS1		0x042E8
#define IXGBE_FECS2		0x042EC
#define IXGBE_SMADARCTL		0x14F10
#define IXGBE_MPVC		0x04318
#define IXGBE_SGMIIC		0x04314

/* Statistics Registers */
#define IXGBE_RXNFGPC		0x041B0
#define IXGBE_RXNFGBCL		0x041B4
#define IXGBE_RXNFGBCH		0x041B8
#define IXGBE_RXDGPC		0x02F50
#define IXGBE_RXDGBCL		0x02F54
#define IXGBE_RXDGBCH		0x02F58
#define IXGBE_RXDDGPC		0x02F5C
#define IXGBE_RXDDGBCL		0x02F60
#define IXGBE_RXDDGBCH		0x02F64
#define IXGBE_RXLPBKGPC		0x02F68
#define IXGBE_RXLPBKGBCL	0x02F6C
#define IXGBE_RXLPBKGBCH	0x02F70
#define IXGBE_RXDLPBKGPC	0x02F74
#define IXGBE_RXDLPBKGBCL	0x02F78
#define IXGBE_RXDLPBKGBCH	0x02F7C
#define IXGBE_TXDGPC		0x087A0
#define IXGBE_TXDGBCL		0x087A4
#define IXGBE_TXDGBCH		0x087A8

#define IXGBE_RXDSTATCTRL	0x02F40

/* Copper Pond 2 link timeout */
#define IXGBE_VALIDATE_LINK_READY_TIMEOUT 50

/* Omer CORECTL */
#define IXGBE_CORECTL			0x014F00
/* BARCTRL */
#define IXGBE_BARCTRL			0x110F4
#define IXGBE_BARCTRL_FLSIZE		0x0700
#define IXGBE_BARCTRL_FLSIZE_SHIFT	8
#define IXGBE_BARCTRL_CSRSIZE		0x2000

/* RSCCTL Bit Masks */
#define IXGBE_RSCCTL_RSCEN	0x01
#define IXGBE_RSCCTL_MAXDESC_1	0x00
#define IXGBE_RSCCTL_MAXDESC_4	0x04
#define IXGBE_RSCCTL_MAXDESC_8	0x08
#define IXGBE_RSCCTL_MAXDESC_16	0x0C
#define IXGBE_RSCCTL_TS_DIS	0x02

/* RSCDBU Bit Masks */
#define IXGBE_RSCDBU_RSCSMALDIS_MASK	0x0000007F
#define IXGBE_RSCDBU_RSCACKDIS		0x00000080

/* RDRXCTL Bit Masks */
#define IXGBE_RDRXCTL_RDMTS_1_2		0x00000000 /* Rx Desc Min THLD Size */
#define IXGBE_RDRXCTL_CRCSTRIP		0x00000002 /* CRC Strip */
#define IXGBE_RDRXCTL_PSP		0x00000004 /* Pad Small Packet */
#define IXGBE_RDRXCTL_MVMEN		0x00000020
#define IXGBE_RDRXCTL_RSC_PUSH_DIS	0x00000020
#define IXGBE_RDRXCTL_DMAIDONE		0x00000008 /* DMA init cycle done */
#define IXGBE_RDRXCTL_RSC_PUSH		0x00000080
#define IXGBE_RDRXCTL_AGGDIS		0x00010000 /* Aggregation disable */
#define IXGBE_RDRXCTL_RSCFRSTSIZE	0x003E0000 /* RSC First packet size */
#define IXGBE_RDRXCTL_RSCLLIDIS		0x00800000 /* Disable RSC compl on LLI*/
#define IXGBE_RDRXCTL_RSCACKC		0x02000000 /* must set 1 when RSC ena */
#define IXGBE_RDRXCTL_FCOE_WRFIX	0x04000000 /* must set 1 when RSC ena */
#define IXGBE_RDRXCTL_MBINTEN		0x10000000
#define IXGBE_RDRXCTL_MDP_EN		0x20000000

/* RQTC Bit Masks and Shifts */
#define IXGBE_RQTC_SHIFT_TC(_i)	((_i) * 4)
#define IXGBE_RQTC_TC0_MASK	(0x7 << 0)
#define IXGBE_RQTC_TC1_MASK	(0x7 << 4)
#define IXGBE_RQTC_TC2_MASK	(0x7 << 8)
#define IXGBE_RQTC_TC3_MASK	(0x7 << 12)
#define IXGBE_RQTC_TC4_MASK	(0x7 << 16)
#define IXGBE_RQTC_TC5_MASK	(0x7 << 20)
#define IXGBE_RQTC_TC6_MASK	(0x7 << 24)
#define IXGBE_RQTC_TC7_MASK	(0x7 << 28)

/* PSRTYPE.RQPL Bit masks and shift */
#define IXGBE_PSRTYPE_RQPL_MASK		0x7
#define IXGBE_PSRTYPE_RQPL_SHIFT	29

/* CTRL Bit Masks */
#define IXGBE_CTRL_GIO_DIS	0x00000004 /* Global IO Master Disable bit */
#define IXGBE_CTRL_LNK_RST	0x00000008 /* Link Reset. Resets everything. */
#define IXGBE_CTRL_RST		0x04000000 /* Reset (SW) */
#define IXGBE_CTRL_RST_MASK	(IXGBE_CTRL_LNK_RST | IXGBE_CTRL_RST)

/* FACTPS */
#define IXGBE_FACTPS_MNGCG	0x20000000 /* Manageability Clock Gated */
#define IXGBE_FACTPS_LFS	0x40000000 /* LAN Function Select */

/* MHADD Bit Masks */
#define IXGBE_MHADD_MFS_MASK	0xFFFF0000
#define IXGBE_MHADD_MFS_SHIFT	16

/* Extended Device Control */
#define IXGBE_CTRL_EXT_PFRSTD	0x00004000 /* Physical Function Reset Done */
#define IXGBE_CTRL_EXT_NS_DIS	0x00010000 /* No Snoop disable */
#define IXGBE_CTRL_EXT_RO_DIS	0x00020000 /* Relaxed Ordering disable */
#define IXGBE_CTRL_EXT_DRV_LOAD	0x10000000 /* Driver loaded bit for FW */

/* Direct Cache Access (DCA) definitions */
#define IXGBE_DCA_CTRL_DCA_ENABLE	0x00000000 /* DCA Enable */
#define IXGBE_DCA_CTRL_DCA_DISABLE	0x00000001 /* DCA Disable */

#define IXGBE_DCA_CTRL_DCA_MODE_CB1	0x00 /* DCA Mode CB1 */
#define IXGBE_DCA_CTRL_DCA_MODE_CB2	0x02 /* DCA Mode CB2 */

#define IXGBE_DCA_RXCTRL_CPUID_MASK	0x0000001F /* Rx CPUID Mask */
#define IXGBE_DCA_RXCTRL_CPUID_MASK_82599	0xFF000000 /* Rx CPUID Mask */
#define IXGBE_DCA_RXCTRL_CPUID_SHIFT_82599	24 /* Rx CPUID Shift */
#define IXGBE_DCA_RXCTRL_DESC_DCA_EN	(1 << 5) /* Rx Desc enable */
#define IXGBE_DCA_RXCTRL_HEAD_DCA_EN	(1 << 6) /* Rx Desc header ena */
#define IXGBE_DCA_RXCTRL_DATA_DCA_EN	(1 << 7) /* Rx Desc payload ena */
#define IXGBE_DCA_RXCTRL_DESC_RRO_EN	(1 << 9) /* Rx rd Desc Relax Order */
#define IXGBE_DCA_RXCTRL_DATA_WRO_EN	(1 << 13) /* Rx wr data Relax Order */
#define IXGBE_DCA_RXCTRL_HEAD_WRO_EN	(1 << 15) /* Rx wr header RO */

#define IXGBE_DCA_TXCTRL_CPUID_MASK	0x0000001F /* Tx CPUID Mask */
#define IXGBE_DCA_TXCTRL_CPUID_MASK_82599	0xFF000000 /* Tx CPUID Mask */
#define IXGBE_DCA_TXCTRL_CPUID_SHIFT_82599	24 /* Tx CPUID Shift */
#define IXGBE_DCA_TXCTRL_DESC_DCA_EN	(1 << 5) /* DCA Tx Desc enable */
#define IXGBE_DCA_TXCTRL_DESC_RRO_EN	(1 << 9) /* Tx rd Desc Relax Order */
#define IXGBE_DCA_TXCTRL_DESC_WRO_EN	(1 << 11) /* Tx Desc writeback RO bit */
#define IXGBE_DCA_TXCTRL_DATA_RRO_EN	(1 << 13) /* Tx rd data Relax Order */
#define IXGBE_DCA_MAX_QUEUES_82598	16 /* DCA regs only on 16 queues */

/* MSCA Bit Masks */
#define IXGBE_MSCA_NP_ADDR_MASK		0x0000FFFF /* MDI Addr (new prot) */
#define IXGBE_MSCA_NP_ADDR_SHIFT	0
#define IXGBE_MSCA_DEV_TYPE_MASK	0x001F0000 /* Dev Type (new prot) */
#define IXGBE_MSCA_DEV_TYPE_SHIFT	16 /* Register Address (old prot */
#define IXGBE_MSCA_PHY_ADDR_MASK	0x03E00000 /* PHY Address mask */
#define IXGBE_MSCA_PHY_ADDR_SHIFT	21 /* PHY Address shift*/
#define IXGBE_MSCA_OP_CODE_MASK		0x0C000000 /* OP CODE mask */
#define IXGBE_MSCA_OP_CODE_SHIFT	26 /* OP CODE shift */
#define IXGBE_MSCA_ADDR_CYCLE		0x00000000 /* OP CODE 00 (addr cycle) */
#define IXGBE_MSCA_WRITE		0x04000000 /* OP CODE 01 (wr) */
#define IXGBE_MSCA_READ			0x0C000000 /* OP CODE 11 (rd) */
#define IXGBE_MSCA_READ_AUTOINC		0x08000000 /* OP CODE 10 (rd auto inc)*/
#define IXGBE_MSCA_ST_CODE_MASK		0x30000000 /* ST Code mask */
#define IXGBE_MSCA_ST_CODE_SHIFT	28 /* ST Code shift */
#define IXGBE_MSCA_NEW_PROTOCOL		0x00000000 /* ST CODE 00 (new prot) */
#define IXGBE_MSCA_OLD_PROTOCOL		0x10000000 /* ST CODE 01 (old prot) */
#define IXGBE_MSCA_MDI_COMMAND		0x40000000 /* Initiate MDI command */
#define IXGBE_MSCA_MDI_IN_PROG_EN	0x80000000 /* MDI in progress ena */

/* MSRWD bit masks */
#define IXGBE_MSRWD_WRITE_DATA_MASK	0x0000FFFF
#define IXGBE_MSRWD_WRITE_DATA_SHIFT	0
#define IXGBE_MSRWD_READ_DATA_MASK	0xFFFF0000
#define IXGBE_MSRWD_READ_DATA_SHIFT	16

/* Atlas registers */
#define IXGBE_ATLAS_PDN_LPBK		0x24
#define IXGBE_ATLAS_PDN_10G		0xB
#define IXGBE_ATLAS_PDN_1G		0xC
#define IXGBE_ATLAS_PDN_AN		0xD

/* Atlas bit masks */
#define IXGBE_ATLASCTL_WRITE_CMD	0x00010000
#define IXGBE_ATLAS_PDN_TX_REG_EN	0x10
#define IXGBE_ATLAS_PDN_TX_10G_QL_ALL	0xF0
#define IXGBE_ATLAS_PDN_TX_1G_QL_ALL	0xF0
#define IXGBE_ATLAS_PDN_TX_AN_QL_ALL	0xF0

/* Omer bit masks */
#define IXGBE_CORECTL_WRITE_CMD		0x00010000

/* Device Type definitions for new protocol MDIO commands */
#define IXGBE_MDIO_ZERO_DEV_TYPE		0x0
#define IXGBE_MDIO_PMA_PMD_DEV_TYPE		0x1
#define IXGBE_MDIO_PCS_DEV_TYPE			0x3
#define IXGBE_MDIO_PHY_XS_DEV_TYPE		0x4
#define IXGBE_MDIO_AUTO_NEG_DEV_TYPE		0x7
#define IXGBE_MDIO_VENDOR_SPECIFIC_1_DEV_TYPE	0x1E   /* Device 30 */
#define IXGBE_TWINAX_DEV			1

#define IXGBE_MDIO_COMMAND_TIMEOUT	100 /* PHY Timeout for 1 GB mode */

#define IXGBE_MDIO_VENDOR_SPECIFIC_1_CONTROL		0x0 /* VS1 Ctrl Reg */
#define IXGBE_MDIO_VENDOR_SPECIFIC_1_STATUS		0x1 /* VS1 Status Reg */
#define IXGBE_MDIO_VENDOR_SPECIFIC_1_LINK_STATUS	0x0008 /* 1 = Link Up */
#define IXGBE_MDIO_VENDOR_SPECIFIC_1_SPEED_STATUS	0x0010 /* 0-10G, 1-1G */
#define IXGBE_MDIO_VENDOR_SPECIFIC_1_10G_SPEED		0x0018
#define IXGBE_MDIO_VENDOR_SPECIFIC_1_1G_SPEED		0x0010

#define IXGBE_MDIO_AUTO_NEG_CONTROL	0x0 /* AUTO_NEG Control Reg */
#define IXGBE_MDIO_AUTO_NEG_STATUS	0x1 /* AUTO_NEG Status Reg */
#define IXGBE_MDIO_AUTO_NEG_VENDOR_STAT	0xC800 /* AUTO_NEG Vendor Status Reg */
#define IXGBE_MDIO_AUTO_NEG_VENDOR_TX_ALARM 0xCC00 /* AUTO_NEG Vendor TX Reg */
#define IXGBE_MDIO_AUTO_NEG_VENDOR_TX_ALARM2 0xCC01 /* AUTO_NEG Vendor Tx Reg */
#define IXGBE_MDIO_AUTO_NEG_VEN_LSC	0x1 /* AUTO_NEG Vendor Tx LSC */
#define IXGBE_MDIO_AUTO_NEG_ADVT	0x10 /* AUTO_NEG Advt Reg */
#define IXGBE_MDIO_AUTO_NEG_LP		0x13 /* AUTO_NEG LP Status Reg */
#define IXGBE_MDIO_AUTO_NEG_EEE_ADVT	0x3C /* AUTO_NEG EEE Advt Reg */
#define IXGBE_AUTO_NEG_10GBASE_EEE_ADVT	0x8  /* AUTO NEG EEE 10GBaseT Advt */
#define IXGBE_AUTO_NEG_1000BASE_EEE_ADVT 0x4  /* AUTO NEG EEE 1000BaseT Advt */
#define IXGBE_AUTO_NEG_100BASE_EEE_ADVT	0x2  /* AUTO NEG EEE 100BaseT Advt */
#define IXGBE_MDIO_PHY_XS_CONTROL	0x0 /* PHY_XS Control Reg */
#define IXGBE_MDIO_PHY_XS_RESET		0x8000 /* PHY_XS Reset */
#define IXGBE_MDIO_PHY_ID_HIGH		0x2 /* PHY ID High Reg*/
#define IXGBE_MDIO_PHY_ID_LOW		0x3 /* PHY ID Low Reg*/
#define IXGBE_MDIO_PHY_SPEED_ABILITY	0x4 /* Speed Ability Reg */
#define IXGBE_MDIO_PHY_SPEED_10G	0x0001 /* 10G capable */
#define IXGBE_MDIO_PHY_SPEED_1G		0x0010 /* 1G capable */
#define IXGBE_MDIO_PHY_SPEED_100M	0x0020 /* 100M capable */
#define IXGBE_MDIO_PHY_EXT_ABILITY	0xB /* Ext Ability Reg */
#define IXGBE_MDIO_PHY_10GBASET_ABILITY		0x0004 /* 10GBaseT capable */
#define IXGBE_MDIO_PHY_1000BASET_ABILITY	0x0020 /* 1000BaseT capable */
#define IXGBE_MDIO_PHY_100BASETX_ABILITY	0x0080 /* 100BaseTX capable */
#define IXGBE_MDIO_PHY_SET_LOW_POWER_MODE	0x0800 /* Set low power mode */
#define IXGBE_AUTO_NEG_LP_STATUS	0xE820 /* AUTO NEG Rx LP Status Reg */
#define IXGBE_AUTO_NEG_LP_1000BASE_CAP	0x8000 /* AUTO NEG Rx LP 1000BaseT Cap */
#define IXGBE_AUTO_NEG_LP_10GBASE_CAP	0x0800 /* AUTO NEG Rx LP 10GBaseT Cap */
#define IXGBE_AUTO_NEG_10GBASET_STAT	0x0021 /* AUTO NEG 10G BaseT Stat */

#define IXGBE_MDIO_TX_VENDOR_ALARMS_3		0xCC02 /* Vendor Alarms 3 Reg */
#define IXGBE_MDIO_TX_VENDOR_ALARMS_3_RST_MASK	0x3 /* PHY Reset Complete Mask */
#define IXGBE_MDIO_GLOBAL_RES_PR_10 0xC479 /* Global Resv Provisioning 10 Reg */
#define IXGBE_MDIO_POWER_UP_STALL		0x8000 /* Power Up Stall */
#define IXGBE_MDIO_GLOBAL_INT_CHIP_STD_MASK	0xFF00 /* int std mask */
#define IXGBE_MDIO_GLOBAL_CHIP_STD_INT_FLAG	0xFC00 /* chip std int flag */
#define IXGBE_MDIO_GLOBAL_INT_CHIP_VEN_MASK	0xFF01 /* int chip-wide mask */
#define IXGBE_MDIO_GLOBAL_INT_CHIP_VEN_FLAG	0xFC01 /* int chip-wide mask */
#define IXGBE_MDIO_GLOBAL_ALARM_1		0xCC00 /* Global alarm 1 */
#define IXGBE_MDIO_GLOBAL_ALM_1_DEV_FAULT	0x0010 /* device fault */
#define IXGBE_MDIO_GLOBAL_ALM_1_HI_TMP_FAIL	0x4000 /* high temp failure */
#define IXGBE_MDIO_GLOBAL_FAULT_MSG		0xC850 /* Global Fault Message */
#define IXGBE_MDIO_GLOBAL_FAULT_MSG_HI_TMP	0x8007 /* high temp failure */
#define IXGBE_MDIO_GLOBAL_INT_MASK		0xD400 /* Global int mask */
#define IXGBE_MDIO_GLOBAL_AN_VEN_ALM_INT_EN	0x1000 /* autoneg vendor alarm int enable */
#define IXGBE_MDIO_GLOBAL_ALARM_1_INT		0x4 /* int in Global alarm 1 */
#define IXGBE_MDIO_GLOBAL_VEN_ALM_INT_EN	0x1 /* vendor alarm int enable */
#define IXGBE_MDIO_GLOBAL_STD_ALM2_INT		0x200 /* vendor alarm2 int mask */
#define IXGBE_MDIO_GLOBAL_INT_HI_TEMP_EN	0x4000 /* int high temp enable */
#define IXGBE_MDIO_GLOBAL_INT_DEV_FAULT_EN 0x0010 /* int dev fault enable */
#define IXGBE_MDIO_PMA_PMD_CONTROL_ADDR	0x0000 /* PMA/PMD Control Reg */
#define IXGBE_MDIO_PMA_PMD_SDA_SCL_ADDR	0xC30A /* PHY_XS SDA/SCL Addr Reg */
#define IXGBE_MDIO_PMA_PMD_SDA_SCL_DATA	0xC30B /* PHY_XS SDA/SCL Data Reg */
#define IXGBE_MDIO_PMA_PMD_SDA_SCL_STAT	0xC30C /* PHY_XS SDA/SCL Status Reg */
#define IXGBE_MDIO_PMA_TX_VEN_LASI_INT_MASK 0xD401 /* PHY TX Vendor LASI */
#define IXGBE_MDIO_PMA_TX_VEN_LASI_INT_EN   0x1 /* PHY TX Vendor LASI enable */
#define IXGBE_MDIO_PMD_STD_TX_DISABLE_CNTR 0x9 /* Standard Transmit Dis Reg */
#define IXGBE_MDIO_PMD_GLOBAL_TX_DISABLE 0x0001 /* PMD Global Transmit Dis */

#define IXGBE_PCRC8ECL		0x0E810 /* PCR CRC-8 Error Count Lo */
#define IXGBE_PCRC8ECH		0x0E811 /* PCR CRC-8 Error Count Hi */
#define IXGBE_PCRC8ECH_MASK	0x1F
#define IXGBE_LDPCECL		0x0E820 /* PCR Uncorrected Error Count Lo */
#define IXGBE_LDPCECH		0x0E821 /* PCR Uncorrected Error Count Hi */

/* MII clause 22/28 definitions */
#define IXGBE_MDIO_PHY_LOW_POWER_MODE	0x0800

#define IXGBE_MDIO_XENPAK_LASI_STATUS		0x9005 /* XENPAK LASI Status register*/
#define IXGBE_XENPAK_LASI_LINK_STATUS_ALARM	0x1 /* Link Status Alarm change */

#define IXGBE_MDIO_AUTO_NEG_LINK_STATUS		0x4 /* Indicates if link is up */

#define IXGBE_MDIO_AUTO_NEG_VENDOR_STATUS_MASK	0x7 /* Speed/Duplex Mask */
#define IXGBE_MDIO_AUTO_NEG_VEN_STAT_SPEED_MASK		0x6 /* Speed Mask */
#define IXGBE_MDIO_AUTO_NEG_VENDOR_STATUS_10M_HALF	0x0 /* 10Mb/s Half Duplex */
#define IXGBE_MDIO_AUTO_NEG_VENDOR_STATUS_10M_FULL	0x1 /* 10Mb/s Full Duplex */
#define IXGBE_MDIO_AUTO_NEG_VENDOR_STATUS_100M_HALF	0x2 /* 100Mb/s Half Duplex */
#define IXGBE_MDIO_AUTO_NEG_VENDOR_STATUS_100M_FULL	0x3 /* 100Mb/s Full Duplex */
#define IXGBE_MDIO_AUTO_NEG_VENDOR_STATUS_1GB_HALF	0x4 /* 1Gb/s Half Duplex */
#define IXGBE_MDIO_AUTO_NEG_VENDOR_STATUS_1GB_FULL	0x5 /* 1Gb/s Full Duplex */
#define IXGBE_MDIO_AUTO_NEG_VENDOR_STATUS_10GB_HALF	0x6 /* 10Gb/s Half Duplex */
#define IXGBE_MDIO_AUTO_NEG_VENDOR_STATUS_10GB_FULL	0x7 /* 10Gb/s Full Duplex */
#define IXGBE_MDIO_AUTO_NEG_VENDOR_STATUS_1GB		0x4 /* 1Gb/s */
#define IXGBE_MDIO_AUTO_NEG_VENDOR_STATUS_10GB		0x6 /* 10Gb/s */

#define IXGBE_MII_10GBASE_T_AUTONEG_CTRL_REG	0x20   /* 10G Control Reg */
#define IXGBE_MII_AUTONEG_VENDOR_PROVISION_1_REG 0xC400 /* 1G Provisioning 1 */
#define IXGBE_MII_AUTONEG_XNP_TX_REG		0x17   /* 1G XNP Transmit */
#define IXGBE_MII_AUTONEG_ADVERTISE_REG		0x10   /* 100M Advertisement */
#define IXGBE_MII_10GBASE_T_ADVERTISE		0x1000 /* full duplex, bit:12*/
#define IXGBE_MII_1GBASE_T_ADVERTISE_XNP_TX	0x4000 /* full duplex, bit:14*/
#define IXGBE_MII_1GBASE_T_ADVERTISE		0x8000 /* full duplex, bit:15*/
#define IXGBE_MII_2_5GBASE_T_ADVERTISE		0x0400
#define IXGBE_MII_5GBASE_T_ADVERTISE		0x0800
#define IXGBE_MII_100BASE_T_ADVERTISE		0x0100 /* full duplex, bit:8 */
#define IXGBE_MII_100BASE_T_ADVERTISE_HALF	0x0080 /* half duplex, bit:7 */
#define IXGBE_MII_RESTART			0x200
#define IXGBE_MII_AUTONEG_COMPLETE		0x20
#define IXGBE_MII_AUTONEG_LINK_UP		0x04
#define IXGBE_MII_AUTONEG_REG			0x0

#define IXGBE_PHY_REVISION_MASK		0xFFFFFFF0
#define IXGBE_MAX_PHY_ADDR		32

/* PHY IDs*/
#define TN1010_PHY_ID	0x00A19410
#define TNX_FW_REV	0xB
#define X540_PHY_ID	0x01540200
#define X550_PHY_ID2	0x01540223
#define X550_PHY_ID3	0x01540221
#define X557_PHY_ID	0x01540240
#define X557_PHY_ID2	0x01540250
#define AQ_FW_REV	0x20
#define QT2022_PHY_ID	0x0043A400
#define ATH_PHY_ID	0x03429050

/* PHY Types */
#define IXGBE_M88E1500_E_PHY_ID	0x01410DD0
#define IXGBE_M88E1543_E_PHY_ID	0x01410EA0

/* Special PHY Init Routine */
#define IXGBE_PHY_INIT_OFFSET_NL	0x002B
#define IXGBE_PHY_INIT_END_NL		0xFFFF
#define IXGBE_CONTROL_MASK_NL		0xF000
#define IXGBE_DATA_MASK_NL		0x0FFF
#define IXGBE_CONTROL_SHIFT_NL		12
#define IXGBE_DELAY_NL			0
#define IXGBE_DATA_NL			1
#define IXGBE_CONTROL_NL		0x000F
#define IXGBE_CONTROL_EOL_NL		0x0FFF
#define IXGBE_CONTROL_SOL_NL		0x0000

/* General purpose Interrupt Enable */
#define IXGBE_SDP0_GPIEN	0x00000001 /* SDP0 */
#define IXGBE_SDP1_GPIEN	0x00000002 /* SDP1 */
#define IXGBE_SDP2_GPIEN	0x00000004 /* SDP2 */
#define IXGBE_SDP0_GPIEN_X540	0x00000002 /* SDP0 on X540 and X550 */
#define IXGBE_SDP1_GPIEN_X540	0x00000004 /* SDP1 on X540 and X550 */
#define IXGBE_SDP2_GPIEN_X540	0x00000008 /* SDP2 on X540 and X550 */
#define IXGBE_SDP0_GPIEN_X550	IXGBE_SDP0_GPIEN_X540
#define IXGBE_SDP1_GPIEN_X550	IXGBE_SDP1_GPIEN_X540
#define IXGBE_SDP2_GPIEN_X550	IXGBE_SDP2_GPIEN_X540
#define IXGBE_SDP0_GPIEN_X550EM_x	IXGBE_SDP0_GPIEN_X540
#define IXGBE_SDP1_GPIEN_X550EM_x	IXGBE_SDP1_GPIEN_X540
#define IXGBE_SDP2_GPIEN_X550EM_x	IXGBE_SDP2_GPIEN_X540
#define IXGBE_SDP0_GPIEN_X550EM_a	IXGBE_SDP0_GPIEN_X540
#define IXGBE_SDP1_GPIEN_X550EM_a	IXGBE_SDP1_GPIEN_X540
#define IXGBE_SDP2_GPIEN_X550EM_a	IXGBE_SDP2_GPIEN_X540
#define IXGBE_SDP0_GPIEN_BY_MAC(_hw)	IXGBE_BY_MAC((_hw), SDP0_GPIEN)
#define IXGBE_SDP1_GPIEN_BY_MAC(_hw)	IXGBE_BY_MAC((_hw), SDP1_GPIEN)
#define IXGBE_SDP2_GPIEN_BY_MAC(_hw)	IXGBE_BY_MAC((_hw), SDP2_GPIEN)

#define IXGBE_GPIE_MSIX_MODE	0x00000010 /* MSI-X mode */
#define IXGBE_GPIE_OCD		0x00000020 /* Other Clear Disable */
#define IXGBE_GPIE_EIMEN	0x00000040 /* Immediate Interrupt Enable */
#define IXGBE_GPIE_EIAME	0x40000000
#define IXGBE_GPIE_PBA_SUPPORT	0x80000000
#define IXGBE_GPIE_LLI_DELAY_SHIFT  7
#define IXGBE_GPIE_RSC_DELAY_SHIFT	11
#define IXGBE_GPIE_VTMODE_MASK	0x0000C000 /* VT Mode Mask */
#define IXGBE_GPIE_VTMODE_16	0x00004000 /* 16 VFs 8 queues per VF */
#define IXGBE_GPIE_VTMODE_32	0x00008000 /* 32 VFs 4 queues per VF */
#define IXGBE_GPIE_VTMODE_64	0x0000C000 /* 64 VFs 2 queues per VF */

/* Packet Buffer Initialization */
#define IXGBE_MAX_PACKET_BUFFERS	8

#define IXGBE_TXPBSIZE_20KB	0x00005000 /* 20KB Packet Buffer */
#define IXGBE_TXPBSIZE_40KB	0x0000A000 /* 40KB Packet Buffer */
#define IXGBE_RXPBSIZE_48KB	0x0000C000 /* 48KB Packet Buffer */
#define IXGBE_RXPBSIZE_64KB	0x00010000 /* 64KB Packet Buffer */
#define IXGBE_RXPBSIZE_80KB	0x00014000 /* 80KB Packet Buffer */
#define IXGBE_RXPBSIZE_128KB	0x00020000 /* 128KB Packet Buffer */
#define IXGBE_RXPBSIZE_MAX	0x00080000 /* 512KB Packet Buffer */
#define IXGBE_TXPBSIZE_MAX	0x00028000 /* 160KB Packet Buffer */

#define IXGBE_TXPKT_SIZE_MAX	0xA /* Max Tx Packet size */
#define IXGBE_MAX_PB		8

/* Packet buffer allocation strategies */
enum {
	PBA_STRATEGY_EQUAL	= 0, /* Distribute PB space equally */
#define PBA_STRATEGY_EQUAL	PBA_STRATEGY_EQUAL
	PBA_STRATEGY_WEIGHTED	= 1, /* Weight front half of TCs */
#define PBA_STRATEGY_WEIGHTED	PBA_STRATEGY_WEIGHTED
};

/* Transmit Flow Control status */
#define IXGBE_TFCS_TXON  0x00000001
#define IXGBE_TFCS_TXOFF	0x00000001
#define IXGBE_TFCS_TXOFF0	0x00000100
#define IXGBE_TFCS_TXOFF1	0x00000200
#define IXGBE_TFCS_TXOFF2	0x00000400
#define IXGBE_TFCS_TXOFF3	0x00000800
#define IXGBE_TFCS_TXOFF4	0x00001000
#define IXGBE_TFCS_TXOFF5	0x00002000
#define IXGBE_TFCS_TXOFF6	0x00004000
#define IXGBE_TFCS_TXOFF7	0x00008000

/* TCP Timer */
#define IXGBE_TCPTIMER_KS		0x00000100
#define IXGBE_TCPTIMER_COUNT_ENABLE	0x00000200
#define IXGBE_TCPTIMER_COUNT_FINISH	0x00000400
#define IXGBE_TCPTIMER_LOOP		0x00000800
#define IXGBE_TCPTIMER_DURATION_MASK	0x000000FF

/* HLREG0 Bit Masks */
#define IXGBE_HLREG0_TXCRCEN		0x00000001 /* bit  0 */
#define IXGBE_HLREG0_RXCRCSTRP		0x00000002 /* bit  1 */
#define IXGBE_HLREG0_JUMBOEN		0x00000004 /* bit  2 */
#define IXGBE_HLREG0_TXPADEN		0x00000400 /* bit 10 */
#define IXGBE_HLREG0_TXPAUSEEN		0x00001000 /* bit 12 */
#define IXGBE_HLREG0_RXPAUSEEN		0x00004000 /* bit 14 */
#define IXGBE_HLREG0_LPBK		0x00008000 /* bit 15 */
#define IXGBE_HLREG0_MDCSPD		0x00010000 /* bit 16 */
#define IXGBE_HLREG0_CONTMDC		0x00020000 /* bit 17 */
#define IXGBE_HLREG0_CTRLFLTR		0x00040000 /* bit 18 */
#define IXGBE_HLREG0_PREPEND		0x00F00000 /* bits 20-23 */
#define IXGBE_HLREG0_PRIPAUSEEN		0x01000000 /* bit 24 */
#define IXGBE_HLREG0_RXPAUSERECDA	0x06000000 /* bits 25-26 */
#define IXGBE_HLREG0_RXLNGTHERREN	0x08000000 /* bit 27 */
#define IXGBE_HLREG0_RXPADSTRIPEN	0x10000000 /* bit 28 */

/* VMD_CTL bitmasks */
#define IXGBE_VMD_CTL_VMDQ_EN		0x00000001
#define IXGBE_VMD_CTL_VMDQ_FILTER	0x00000002

/* VT_CTL bitmasks */
#define IXGBE_VT_CTL_DIS_DEFPL		0x20000000 /* disable default pool */
#define IXGBE_VT_CTL_REPLEN		0x40000000 /* replication enabled */
#define IXGBE_VT_CTL_VT_ENABLE		0x00000001  /* Enable VT Mode */
#define IXGBE_VT_CTL_POOL_SHIFT		7
#define IXGBE_VT_CTL_POOL_MASK		(0x3F << IXGBE_VT_CTL_POOL_SHIFT)

/* VMOLR bitmasks */
#define IXGBE_VMOLR_UPE		0x00400000 /* unicast promiscuous */
#define IXGBE_VMOLR_VPE		0x00800000 /* VLAN promiscuous */
#define IXGBE_VMOLR_AUPE	0x01000000 /* accept untagged packets */
#define IXGBE_VMOLR_ROMPE	0x02000000 /* accept packets in MTA tbl */
#define IXGBE_VMOLR_ROPE	0x04000000 /* accept packets in UC tbl */
#define IXGBE_VMOLR_BAM		0x08000000 /* accept broadcast packets */
#define IXGBE_VMOLR_MPE		0x10000000 /* multicast promiscuous */

/* VFRE bitmask */
#define IXGBE_VFRE_ENABLE_ALL	0xFFFFFFFF

#define IXGBE_VF_INIT_TIMEOUT	200 /* Number of retries to clear RSTI */

/* RDHMPN and TDHMPN bitmasks */
#define IXGBE_RDHMPN_RDICADDR		0x007FF800
#define IXGBE_RDHMPN_RDICRDREQ		0x00800000
#define IXGBE_RDHMPN_RDICADDR_SHIFT	11
#define IXGBE_TDHMPN_TDICADDR		0x003FF800
#define IXGBE_TDHMPN_TDICRDREQ		0x00800000
#define IXGBE_TDHMPN_TDICADDR_SHIFT	11

#define IXGBE_RDMAM_MEM_SEL_SHIFT		13
#define IXGBE_RDMAM_DWORD_SHIFT			9
#define IXGBE_RDMAM_DESC_COMP_FIFO		1
#define IXGBE_RDMAM_DFC_CMD_FIFO		2
#define IXGBE_RDMAM_RSC_HEADER_ADDR		3
#define IXGBE_RDMAM_TCN_STATUS_RAM		4
#define IXGBE_RDMAM_WB_COLL_FIFO		5
#define IXGBE_RDMAM_QSC_CNT_RAM			6
#define IXGBE_RDMAM_QSC_FCOE_RAM		7
#define IXGBE_RDMAM_QSC_QUEUE_CNT		8
#define IXGBE_RDMAM_QSC_QUEUE_RAM		0xA
#define IXGBE_RDMAM_QSC_RSC_RAM			0xB
#define IXGBE_RDMAM_DESC_COM_FIFO_RANGE		135
#define IXGBE_RDMAM_DESC_COM_FIFO_COUNT		4
#define IXGBE_RDMAM_DFC_CMD_FIFO_RANGE		48
#define IXGBE_RDMAM_DFC_CMD_FIFO_COUNT		7
#define IXGBE_RDMAM_RSC_HEADER_ADDR_RANGE	32
#define IXGBE_RDMAM_RSC_HEADER_ADDR_COUNT	4
#define IXGBE_RDMAM_TCN_STATUS_RAM_RANGE	256
#define IXGBE_RDMAM_TCN_STATUS_RAM_COUNT	9
#define IXGBE_RDMAM_WB_COLL_FIFO_RANGE		8
#define IXGBE_RDMAM_WB_COLL_FIFO_COUNT		4
#define IXGBE_RDMAM_QSC_CNT_RAM_RANGE		64
#define IXGBE_RDMAM_QSC_CNT_RAM_COUNT		4
#define IXGBE_RDMAM_QSC_FCOE_RAM_RANGE		512
#define IXGBE_RDMAM_QSC_FCOE_RAM_COUNT		5
#define IXGBE_RDMAM_QSC_QUEUE_CNT_RANGE		32
#define IXGBE_RDMAM_QSC_QUEUE_CNT_COUNT		4
#define IXGBE_RDMAM_QSC_QUEUE_RAM_RANGE		128
#define IXGBE_RDMAM_QSC_QUEUE_RAM_COUNT		8
#define IXGBE_RDMAM_QSC_RSC_RAM_RANGE		32
#define IXGBE_RDMAM_QSC_RSC_RAM_COUNT		8

#define IXGBE_TXDESCIC_READY	0x80000000

/* Receive Checksum Control */
#define IXGBE_RXCSUM_IPPCSE	0x00001000 /* IP payload checksum enable */
#define IXGBE_RXCSUM_PCSD	0x00002000 /* packet checksum disabled */

/* FCRTL Bit Masks */
#define IXGBE_FCRTL_XONE	0x80000000 /* XON enable */
#define IXGBE_FCRTH_FCEN	0x80000000 /* Packet buffer fc enable */

/* PAP bit masks*/
#define IXGBE_PAP_TXPAUSECNT_MASK	0x0000FFFF /* Pause counter mask */

/* RMCS Bit Masks */
#define IXGBE_RMCS_RRM			0x00000002 /* Rx Recycle Mode enable */
/* Receive Arbitration Control: 0 Round Robin, 1 DFP */
#define IXGBE_RMCS_RAC			0x00000004
/* Deficit Fixed Prio ena */
#define IXGBE_RMCS_DFP			IXGBE_RMCS_RAC
#define IXGBE_RMCS_TFCE_802_3X		0x00000008 /* Tx Priority FC ena */
#define IXGBE_RMCS_TFCE_PRIORITY	0x00000010 /* Tx Priority FC ena */
#define IXGBE_RMCS_ARBDIS		0x00000040 /* Arbitration disable bit */

/* FCCFG Bit Masks */
#define IXGBE_FCCFG_TFCE_802_3X		0x00000008 /* Tx link FC enable */
#define IXGBE_FCCFG_TFCE_PRIORITY	0x00000010 /* Tx priority FC enable */

/* Interrupt register bitmasks */

/* Extended Interrupt Cause Read */
#define IXGBE_EICR_RTX_QUEUE	0x0000FFFF /* RTx Queue Interrupt */
#define IXGBE_EICR_FLOW_DIR	0x00010000 /* FDir Exception */
#define IXGBE_EICR_RX_MISS	0x00020000 /* Packet Buffer Overrun */
#define IXGBE_EICR_PCI		0x00040000 /* PCI Exception */
#define IXGBE_EICR_MAILBOX	0x00080000 /* VF to PF Mailbox Interrupt */
#define IXGBE_EICR_LSC		0x00100000 /* Link Status Change */
#define IXGBE_EICR_LINKSEC	0x00200000 /* PN Threshold */
#define IXGBE_EICR_MNG		0x00400000 /* Manageability Event Interrupt */
#define IXGBE_EICR_TS		0x00800000 /* Thermal Sensor Event */
#define IXGBE_EICR_TIMESYNC	0x01000000 /* Timesync Event */
#define IXGBE_EICR_GPI_SDP0	0x01000000 /* Gen Purpose Interrupt on SDP0 */
#define IXGBE_EICR_GPI_SDP1	0x02000000 /* Gen Purpose Interrupt on SDP1 */
#define IXGBE_EICR_GPI_SDP2	0x04000000 /* Gen Purpose Interrupt on SDP2 */
#define IXGBE_EICR_ECC		0x10000000 /* ECC Error */
#define IXGBE_EICR_GPI_SDP0_X540 0x02000000 /* Gen Purpose Interrupt on SDP0 */
#define IXGBE_EICR_GPI_SDP1_X540 0x04000000 /* Gen Purpose Interrupt on SDP1 */
#define IXGBE_EICR_GPI_SDP2_X540 0x08000000 /* Gen Purpose Interrupt on SDP2 */
#define IXGBE_EIMS_GPI_SDP0_X540 IXGBE_EICR_GPI_SDP0_X540 /* deprecated */
#define IXGBE_EIMS_GPI_SDP1_X540 IXGBE_EICR_GPI_SDP1_X540 /* deprecated */
#define IXGBE_EIMS_GPI_SDP2_X540 IXGBE_EICR_GPI_SDP2_X540 /* deprecated */
#define IXGBE_EICR_GPI_SDP0_X550	IXGBE_EICR_GPI_SDP0_X540
#define IXGBE_EICR_GPI_SDP1_X550	IXGBE_EICR_GPI_SDP1_X540
#define IXGBE_EICR_GPI_SDP2_X550	IXGBE_EICR_GPI_SDP2_X540
#define IXGBE_EICR_GPI_SDP0_X550EM_x	IXGBE_EICR_GPI_SDP0_X540
#define IXGBE_EICR_GPI_SDP1_X550EM_x	IXGBE_EICR_GPI_SDP1_X540
#define IXGBE_EICR_GPI_SDP2_X550EM_x	IXGBE_EICR_GPI_SDP2_X540
#define IXGBE_EICR_GPI_SDP0_X550EM_a	IXGBE_EICR_GPI_SDP0_X540
#define IXGBE_EICR_GPI_SDP1_X550EM_a	IXGBE_EICR_GPI_SDP1_X540
#define IXGBE_EICR_GPI_SDP2_X550EM_a	IXGBE_EICR_GPI_SDP2_X540
#define IXGBE_EICR_GPI_SDP0_BY_MAC(_hw)	IXGBE_BY_MAC((_hw), EICR_GPI_SDP0)
#define IXGBE_EICR_GPI_SDP1_BY_MAC(_hw)	IXGBE_BY_MAC((_hw), EICR_GPI_SDP1)
#define IXGBE_EICR_GPI_SDP2_BY_MAC(_hw)	IXGBE_BY_MAC((_hw), EICR_GPI_SDP2)

#define IXGBE_EICR_PBUR		0x10000000 /* Packet Buffer Handler Error */
#define IXGBE_EICR_DHER		0x20000000 /* Descriptor Handler Error */
#define IXGBE_EICR_TCP_TIMER	0x40000000 /* TCP Timer */
#define IXGBE_EICR_OTHER	0x80000000 /* Interrupt Cause Active */

/* Extended Interrupt Cause Set */
#define IXGBE_EICS_RTX_QUEUE	IXGBE_EICR_RTX_QUEUE /* RTx Queue Interrupt */
#define IXGBE_EICS_FLOW_DIR	IXGBE_EICR_FLOW_DIR  /* FDir Exception */
#define IXGBE_EICS_RX_MISS	IXGBE_EICR_RX_MISS   /* Pkt Buffer Overrun */
#define IXGBE_EICS_PCI		IXGBE_EICR_PCI /* PCI Exception */
#define IXGBE_EICS_MAILBOX	IXGBE_EICR_MAILBOX   /* VF to PF Mailbox Int */
#define IXGBE_EICS_LSC		IXGBE_EICR_LSC /* Link Status Change */
#define IXGBE_EICS_MNG		IXGBE_EICR_MNG /* MNG Event Interrupt */
#define IXGBE_EICS_TIMESYNC	IXGBE_EICR_TIMESYNC /* Timesync Event */
#define IXGBE_EICS_GPI_SDP0	IXGBE_EICR_GPI_SDP0 /* SDP0 Gen Purpose Int */
#define IXGBE_EICS_GPI_SDP1	IXGBE_EICR_GPI_SDP1 /* SDP1 Gen Purpose Int */
#define IXGBE_EICS_GPI_SDP2	IXGBE_EICR_GPI_SDP2 /* SDP2 Gen Purpose Int */
#define IXGBE_EICS_ECC		IXGBE_EICR_ECC /* ECC Error */
#define IXGBE_EICS_GPI_SDP0_BY_MAC(_hw)	IXGBE_EICR_GPI_SDP0_BY_MAC(_hw)
#define IXGBE_EICS_GPI_SDP1_BY_MAC(_hw)	IXGBE_EICR_GPI_SDP1_BY_MAC(_hw)
#define IXGBE_EICS_GPI_SDP2_BY_MAC(_hw)	IXGBE_EICR_GPI_SDP2_BY_MAC(_hw)
#define IXGBE_EICS_PBUR		IXGBE_EICR_PBUR /* Pkt Buf Handler Err */
#define IXGBE_EICS_DHER		IXGBE_EICR_DHER /* Desc Handler Error */
#define IXGBE_EICS_TCP_TIMER	IXGBE_EICR_TCP_TIMER /* TCP Timer */
#define IXGBE_EICS_OTHER	IXGBE_EICR_OTHER /* INT Cause Active */

/* Extended Interrupt Mask Set */
#define IXGBE_EIMS_RTX_QUEUE	IXGBE_EICR_RTX_QUEUE /* RTx Queue Interrupt */
#define IXGBE_EIMS_FLOW_DIR	IXGBE_EICR_FLOW_DIR /* FDir Exception */
#define IXGBE_EIMS_RX_MISS	IXGBE_EICR_RX_MISS /* Packet Buffer Overrun */
#define IXGBE_EIMS_PCI		IXGBE_EICR_PCI /* PCI Exception */
#define IXGBE_EIMS_MAILBOX	IXGBE_EICR_MAILBOX   /* VF to PF Mailbox Int */
#define IXGBE_EIMS_LSC		IXGBE_EICR_LSC /* Link Status Change */
#define IXGBE_EIMS_MNG		IXGBE_EICR_MNG /* MNG Event Interrupt */
#define IXGBE_EIMS_TS		IXGBE_EICR_TS /* Thermal Sensor Event */
#define IXGBE_EIMS_TIMESYNC	IXGBE_EICR_TIMESYNC /* Timesync Event */
#define IXGBE_EIMS_GPI_SDP0	IXGBE_EICR_GPI_SDP0 /* SDP0 Gen Purpose Int */
#define IXGBE_EIMS_GPI_SDP1	IXGBE_EICR_GPI_SDP1 /* SDP1 Gen Purpose Int */
#define IXGBE_EIMS_GPI_SDP2	IXGBE_EICR_GPI_SDP2 /* SDP2 Gen Purpose Int */
#define IXGBE_EIMS_ECC		IXGBE_EICR_ECC /* ECC Error */
#define IXGBE_EIMS_GPI_SDP0_BY_MAC(_hw)	IXGBE_EICR_GPI_SDP0_BY_MAC(_hw)
#define IXGBE_EIMS_GPI_SDP1_BY_MAC(_hw)	IXGBE_EICR_GPI_SDP1_BY_MAC(_hw)
#define IXGBE_EIMS_GPI_SDP2_BY_MAC(_hw)	IXGBE_EICR_GPI_SDP2_BY_MAC(_hw)
#define IXGBE_EIMS_PBUR		IXGBE_EICR_PBUR /* Pkt Buf Handler Err */
#define IXGBE_EIMS_DHER		IXGBE_EICR_DHER /* Descr Handler Error */
#define IXGBE_EIMS_TCP_TIMER	IXGBE_EICR_TCP_TIMER /* TCP Timer */
#define IXGBE_EIMS_OTHER	IXGBE_EICR_OTHER /* INT Cause Active */

/* Extended Interrupt Mask Clear */
#define IXGBE_EIMC_RTX_QUEUE	IXGBE_EICR_RTX_QUEUE /* RTx Queue Interrupt */
#define IXGBE_EIMC_FLOW_DIR	IXGBE_EICR_FLOW_DIR /* FDir Exception */
#define IXGBE_EIMC_RX_MISS	IXGBE_EICR_RX_MISS /* Packet Buffer Overrun */
#define IXGBE_EIMC_PCI		IXGBE_EICR_PCI /* PCI Exception */
#define IXGBE_EIMC_MAILBOX	IXGBE_EICR_MAILBOX /* VF to PF Mailbox Int */
#define IXGBE_EIMC_LSC		IXGBE_EICR_LSC /* Link Status Change */
#define IXGBE_EIMC_MNG		IXGBE_EICR_MNG /* MNG Event Interrupt */
#define IXGBE_EIMC_TIMESYNC	IXGBE_EICR_TIMESYNC /* Timesync Event */
#define IXGBE_EIMC_GPI_SDP0	IXGBE_EICR_GPI_SDP0 /* SDP0 Gen Purpose Int */
#define IXGBE_EIMC_GPI_SDP1	IXGBE_EICR_GPI_SDP1 /* SDP1 Gen Purpose Int */
#define IXGBE_EIMC_GPI_SDP2	IXGBE_EICR_GPI_SDP2  /* SDP2 Gen Purpose Int */
#define IXGBE_EIMC_ECC		IXGBE_EICR_ECC /* ECC Error */
#define IXGBE_EIMC_GPI_SDP0_BY_MAC(_hw)	IXGBE_EICR_GPI_SDP0_BY_MAC(_hw)
#define IXGBE_EIMC_GPI_SDP1_BY_MAC(_hw)	IXGBE_EICR_GPI_SDP1_BY_MAC(_hw)
#define IXGBE_EIMC_GPI_SDP2_BY_MAC(_hw)	IXGBE_EICR_GPI_SDP2_BY_MAC(_hw)
#define IXGBE_EIMC_PBUR		IXGBE_EICR_PBUR /* Pkt Buf Handler Err */
#define IXGBE_EIMC_DHER		IXGBE_EICR_DHER /* Desc Handler Err */
#define IXGBE_EIMC_TCP_TIMER	IXGBE_EICR_TCP_TIMER /* TCP Timer */
#define IXGBE_EIMC_OTHER	IXGBE_EICR_OTHER /* INT Cause Active */

#define IXGBE_EIMS_ENABLE_MASK ( \
				IXGBE_EIMS_RTX_QUEUE	| \
				IXGBE_EIMS_LSC		| \
				IXGBE_EIMS_TCP_TIMER	| \
				IXGBE_EIMS_OTHER)

/* Immediate Interrupt Rx (A.K.A. Low Latency Interrupt) */
#define IXGBE_IMIR_PORT_IM_EN	0x00010000  /* TCP port enable */
#define IXGBE_IMIR_PORT_BP	0x00020000  /* TCP port check bypass */
#define IXGBE_IMIREXT_SIZE_BP	0x00001000  /* Packet size bypass */
#define IXGBE_IMIREXT_CTRL_URG	0x00002000  /* Check URG bit in header */
#define IXGBE_IMIREXT_CTRL_ACK	0x00004000  /* Check ACK bit in header */
#define IXGBE_IMIREXT_CTRL_PSH	0x00008000  /* Check PSH bit in header */
#define IXGBE_IMIREXT_CTRL_RST	0x00010000  /* Check RST bit in header */
#define IXGBE_IMIREXT_CTRL_SYN	0x00020000  /* Check SYN bit in header */
#define IXGBE_IMIREXT_CTRL_FIN	0x00040000  /* Check FIN bit in header */
#define IXGBE_IMIREXT_CTRL_BP	0x00080000  /* Bypass check of control bits */
#define IXGBE_IMIR_SIZE_BP_82599	0x00001000 /* Packet size bypass */
#define IXGBE_IMIR_CTRL_URG_82599	0x00002000 /* Check URG bit in header */
#define IXGBE_IMIR_CTRL_ACK_82599	0x00004000 /* Check ACK bit in header */
#define IXGBE_IMIR_CTRL_PSH_82599	0x00008000 /* Check PSH bit in header */
#define IXGBE_IMIR_CTRL_RST_82599	0x00010000 /* Check RST bit in header */
#define IXGBE_IMIR_CTRL_SYN_82599	0x00020000 /* Check SYN bit in header */
#define IXGBE_IMIR_CTRL_FIN_82599	0x00040000 /* Check FIN bit in header */
#define IXGBE_IMIR_CTRL_BP_82599	0x00080000 /* Bypass chk of ctrl bits */
#define IXGBE_IMIR_LLI_EN_82599		0x00100000 /* Enables low latency Int */
#define IXGBE_IMIR_RX_QUEUE_MASK_82599	0x0000007F /* Rx Queue Mask */
#define IXGBE_IMIR_RX_QUEUE_SHIFT_82599	21 /* Rx Queue Shift */
#define IXGBE_IMIRVP_PRIORITY_MASK	0x00000007 /* VLAN priority mask */
#define IXGBE_IMIRVP_PRIORITY_EN	0x00000008 /* VLAN priority enable */

#define IXGBE_MAX_FTQF_FILTERS		128
#define IXGBE_FTQF_PROTOCOL_MASK	0x00000003
#define IXGBE_FTQF_PROTOCOL_TCP		0x00000000
#define IXGBE_FTQF_PROTOCOL_UDP		0x00000001
#define IXGBE_FTQF_PROTOCOL_SCTP	2
#define IXGBE_FTQF_PRIORITY_MASK	0x00000007
#define IXGBE_FTQF_PRIORITY_SHIFT	2
#define IXGBE_FTQF_POOL_MASK		0x0000003F
#define IXGBE_FTQF_POOL_SHIFT		8
#define IXGBE_FTQF_5TUPLE_MASK_MASK	0x0000001F
#define IXGBE_FTQF_5TUPLE_MASK_SHIFT	25
#define IXGBE_FTQF_SOURCE_ADDR_MASK	0x1E
#define IXGBE_FTQF_DEST_ADDR_MASK	0x1D
#define IXGBE_FTQF_SOURCE_PORT_MASK	0x1B
#define IXGBE_FTQF_DEST_PORT_MASK	0x17
#define IXGBE_FTQF_PROTOCOL_COMP_MASK	0x0F
#define IXGBE_FTQF_POOL_MASK_EN		0x40000000
#define IXGBE_FTQF_QUEUE_ENABLE		0x80000000

/* Interrupt clear mask */
#define IXGBE_IRQ_CLEAR_MASK	0xFFFFFFFF

/* Interrupt Vector Allocation Registers */
#define IXGBE_IVAR_REG_NUM		25
#define IXGBE_IVAR_REG_NUM_82599	64
#define IXGBE_IVAR_TXRX_ENTRY		96
#define IXGBE_IVAR_RX_ENTRY		64
#define IXGBE_IVAR_RX_QUEUE(_i)		(0 + (_i))
#define IXGBE_IVAR_TX_QUEUE(_i)		(64 + (_i))
#define IXGBE_IVAR_TX_ENTRY		32

#define IXGBE_IVAR_TCP_TIMER_INDEX	96 /* 0 based index */
#define IXGBE_IVAR_OTHER_CAUSES_INDEX	97 /* 0 based index */

#define IXGBE_MSIX_VECTOR(_i)		(0 + (_i))

#define IXGBE_IVAR_ALLOC_VAL		0x80 /* Interrupt Allocation valid */

/* ETYPE Queue Filter/Select Bit Masks */
#define IXGBE_MAX_ETQF_FILTERS		8
#define IXGBE_ETQF_FCOE			0x08000000 /* bit 27 */
#define IXGBE_ETQF_BCN			0x10000000 /* bit 28 */
#define IXGBE_ETQF_TX_ANTISPOOF		0x20000000 /* bit 29 */
#define IXGBE_ETQF_1588			0x40000000 /* bit 30 */
#define IXGBE_ETQF_FILTER_EN		0x80000000 /* bit 31 */
#define IXGBE_ETQF_POOL_ENABLE		(1 << 26) /* bit 26 */
#define IXGBE_ETQF_POOL_SHIFT		20

#define IXGBE_ETQS_RX_QUEUE		0x007F0000 /* bits 22:16 */
#define IXGBE_ETQS_RX_QUEUE_SHIFT	16
#define IXGBE_ETQS_LLI			0x20000000 /* bit 29 */
#define IXGBE_ETQS_QUEUE_EN		0x80000000 /* bit 31 */

/*
 * ETQF filter list: one static filter per filter consumer. This is
 *		   to avoid filter collisions later. Add new filters
 *		   here!!
 *
 * Current filters:
 *	EAPOL 802.1x (0x888e): Filter 0
 *	FCoE (0x8906):	 Filter 2
 *	1588 (0x88f7):	 Filter 3
 *	FIP  (0x8914):	 Filter 4
 *	LLDP (0x88CC):	 Filter 5
 *	LACP (0x8809):	 Filter 6
 *	FC   (0x8808):	 Filter 7
 */
#define IXGBE_ETQF_FILTER_EAPOL		0
#define IXGBE_ETQF_FILTER_FCOE		2
#define IXGBE_ETQF_FILTER_1588		3
#define IXGBE_ETQF_FILTER_FIP		4
#define IXGBE_ETQF_FILTER_LLDP		5
#define IXGBE_ETQF_FILTER_LACP		6
#define IXGBE_ETQF_FILTER_FC		7
/* VLAN Control Bit Masks */
#define IXGBE_VLNCTRL_VET		0x0000FFFF  /* bits 0-15 */
#define IXGBE_VLNCTRL_CFI		0x10000000  /* bit 28 */
#define IXGBE_VLNCTRL_CFIEN		0x20000000  /* bit 29 */
#define IXGBE_VLNCTRL_VFE		0x40000000  /* bit 30 */
#define IXGBE_VLNCTRL_VME		0x80000000  /* bit 31 */

/* VLAN pool filtering masks */
#define IXGBE_VLVF_VIEN			0x80000000  /* filter is valid */
#define IXGBE_VLVF_ENTRIES		64
#define IXGBE_VLVF_VLANID_MASK		0x00000FFF
/* Per VF Port VLAN insertion rules */
#define IXGBE_VMVIR_VLANA_DEFAULT	0x40000000 /* Always use default VLAN */
#define IXGBE_VMVIR_VLANA_NEVER		0x80000000 /* Never insert VLAN tag */

#define IXGBE_ETHERNET_IEEE_VLAN_TYPE	0x8100  /* 802.1q protocol */

/* STATUS Bit Masks */
#define IXGBE_STATUS_LAN_ID		0x0000000C /* LAN ID */
#define IXGBE_STATUS_LAN_ID_SHIFT	2 /* LAN ID Shift*/
#define IXGBE_STATUS_GIO		0x00080000 /* GIO Master Ena Status */

#define IXGBE_STATUS_LAN_ID_0	0x00000000 /* LAN ID 0 */
#define IXGBE_STATUS_LAN_ID_1	0x00000004 /* LAN ID 1 */

/* ESDP Bit Masks */
#define IXGBE_ESDP_SDP0		0x00000001 /* SDP0 Data Value */
#define IXGBE_ESDP_SDP1		0x00000002 /* SDP1 Data Value */
#define IXGBE_ESDP_SDP2		0x00000004 /* SDP2 Data Value */
#define IXGBE_ESDP_SDP3		0x00000008 /* SDP3 Data Value */
#define IXGBE_ESDP_SDP4		0x00000010 /* SDP4 Data Value */
#define IXGBE_ESDP_SDP5		0x00000020 /* SDP5 Data Value */
#define IXGBE_ESDP_SDP6		0x00000040 /* SDP6 Data Value */
#define IXGBE_ESDP_SDP7		0x00000080 /* SDP7 Data Value */
#define IXGBE_ESDP_SDP0_DIR	0x00000100 /* SDP0 IO direction */
#define IXGBE_ESDP_SDP1_DIR	0x00000200 /* SDP1 IO direction */
#define IXGBE_ESDP_SDP2_DIR	0x00000400 /* SDP1 IO direction */
#define IXGBE_ESDP_SDP3_DIR	0x00000800 /* SDP3 IO direction */
#define IXGBE_ESDP_SDP4_DIR	0x00001000 /* SDP4 IO direction */
#define IXGBE_ESDP_SDP5_DIR	0x00002000 /* SDP5 IO direction */
#define IXGBE_ESDP_SDP6_DIR	0x00004000 /* SDP6 IO direction */
#define IXGBE_ESDP_SDP7_DIR	0x00008000 /* SDP7 IO direction */
#define IXGBE_ESDP_SDP0_NATIVE	0x00010000 /* SDP0 IO mode */
#define IXGBE_ESDP_SDP1_NATIVE	0x00020000 /* SDP1 IO mode */


/* LEDCTL Bit Masks */
#define IXGBE_LED_IVRT_BASE		0x00000040
#define IXGBE_LED_BLINK_BASE		0x00000080
#define IXGBE_LED_MODE_MASK_BASE	0x0000000F
#define IXGBE_LED_OFFSET(_base, _i)	(_base << (8 * (_i)))
#define IXGBE_LED_MODE_SHIFT(_i)	(8*(_i))
#define IXGBE_LED_IVRT(_i)	IXGBE_LED_OFFSET(IXGBE_LED_IVRT_BASE, _i)
#define IXGBE_LED_BLINK(_i)	IXGBE_LED_OFFSET(IXGBE_LED_BLINK_BASE, _i)
#define IXGBE_LED_MODE_MASK(_i)	IXGBE_LED_OFFSET(IXGBE_LED_MODE_MASK_BASE, _i)
#define IXGBE_X557_LED_MANUAL_SET_MASK	(1 << 8)
#define IXGBE_X557_MAX_LED_INDEX	3
#define IXGBE_X557_LED_PROVISIONING	0xC430

/* LED modes */
#define IXGBE_LED_LINK_UP	0x0
#define IXGBE_LED_LINK_10G	0x1
#define IXGBE_LED_MAC		0x2
#define IXGBE_LED_FILTER	0x3
#define IXGBE_LED_LINK_ACTIVE	0x4
#define IXGBE_LED_LINK_1G	0x5
#define IXGBE_LED_ON		0xE
#define IXGBE_LED_OFF		0xF

/* AUTOC Bit Masks */
#define IXGBE_AUTOC_KX4_KX_SUPP_MASK 0xC0000000
#define IXGBE_AUTOC_KX4_SUPP	0x80000000
#define IXGBE_AUTOC_KX_SUPP	0x40000000
#define IXGBE_AUTOC_PAUSE	0x30000000
#define IXGBE_AUTOC_ASM_PAUSE	0x20000000
#define IXGBE_AUTOC_SYM_PAUSE	0x10000000
#define IXGBE_AUTOC_RF		0x08000000
#define IXGBE_AUTOC_PD_TMR	0x06000000
#define IXGBE_AUTOC_AN_RX_LOOSE	0x01000000
#define IXGBE_AUTOC_AN_RX_DRIFT	0x00800000
#define IXGBE_AUTOC_AN_RX_ALIGN	0x007C0000
#define IXGBE_AUTOC_FECA	0x00040000
#define IXGBE_AUTOC_FECR	0x00020000
#define IXGBE_AUTOC_KR_SUPP	0x00010000
#define IXGBE_AUTOC_AN_RESTART	0x00001000
#define IXGBE_AUTOC_FLU		0x00000001
#define IXGBE_AUTOC_LMS_SHIFT	13
#define IXGBE_AUTOC_LMS_10G_SERIAL	(0x3 << IXGBE_AUTOC_LMS_SHIFT)
#define IXGBE_AUTOC_LMS_KX4_KX_KR	(0x4 << IXGBE_AUTOC_LMS_SHIFT)
#define IXGBE_AUTOC_LMS_SGMII_1G_100M	(0x5 << IXGBE_AUTOC_LMS_SHIFT)
#define IXGBE_AUTOC_LMS_KX4_KX_KR_1G_AN	(0x6 << IXGBE_AUTOC_LMS_SHIFT)
#define IXGBE_AUTOC_LMS_KX4_KX_KR_SGMII	(0x7 << IXGBE_AUTOC_LMS_SHIFT)
#define IXGBE_AUTOC_LMS_MASK		(0x7 << IXGBE_AUTOC_LMS_SHIFT)
#define IXGBE_AUTOC_LMS_1G_LINK_NO_AN	(0x0 << IXGBE_AUTOC_LMS_SHIFT)
#define IXGBE_AUTOC_LMS_10G_LINK_NO_AN	(0x1 << IXGBE_AUTOC_LMS_SHIFT)
#define IXGBE_AUTOC_LMS_1G_AN		(0x2 << IXGBE_AUTOC_LMS_SHIFT)
#define IXGBE_AUTOC_LMS_KX4_AN		(0x4 << IXGBE_AUTOC_LMS_SHIFT)
#define IXGBE_AUTOC_LMS_KX4_AN_1G_AN	(0x6 << IXGBE_AUTOC_LMS_SHIFT)
#define IXGBE_AUTOC_LMS_ATTACH_TYPE	(0x7 << IXGBE_AUTOC_10G_PMA_PMD_SHIFT)

#define IXGBE_AUTOC_1G_PMA_PMD_MASK	0x00000200
#define IXGBE_AUTOC_1G_PMA_PMD_SHIFT	9
#define IXGBE_AUTOC_10G_PMA_PMD_MASK	0x00000180
#define IXGBE_AUTOC_10G_PMA_PMD_SHIFT	7
#define IXGBE_AUTOC_10G_XAUI	(0x0 << IXGBE_AUTOC_10G_PMA_PMD_SHIFT)
#define IXGBE_AUTOC_10G_KX4	(0x1 << IXGBE_AUTOC_10G_PMA_PMD_SHIFT)
#define IXGBE_AUTOC_10G_CX4	(0x2 << IXGBE_AUTOC_10G_PMA_PMD_SHIFT)
#define IXGBE_AUTOC_1G_BX	(0x0 << IXGBE_AUTOC_1G_PMA_PMD_SHIFT)
#define IXGBE_AUTOC_1G_KX	(0x1 << IXGBE_AUTOC_1G_PMA_PMD_SHIFT)
#define IXGBE_AUTOC_1G_SFI	(0x0 << IXGBE_AUTOC_1G_PMA_PMD_SHIFT)
#define IXGBE_AUTOC_1G_KX_BX	(0x1 << IXGBE_AUTOC_1G_PMA_PMD_SHIFT)

#define IXGBE_AUTOC2_UPPER_MASK	0xFFFF0000
#define IXGBE_AUTOC2_10G_SERIAL_PMA_PMD_MASK	0x00030000
#define IXGBE_AUTOC2_10G_SERIAL_PMA_PMD_SHIFT	16
#define IXGBE_AUTOC2_10G_KR	(0x0 << IXGBE_AUTOC2_10G_SERIAL_PMA_PMD_SHIFT)
#define IXGBE_AUTOC2_10G_XFI	(0x1 << IXGBE_AUTOC2_10G_SERIAL_PMA_PMD_SHIFT)
#define IXGBE_AUTOC2_10G_SFI	(0x2 << IXGBE_AUTOC2_10G_SERIAL_PMA_PMD_SHIFT)
#define IXGBE_AUTOC2_LINK_DISABLE_ON_D3_MASK	0x50000000
#define IXGBE_AUTOC2_LINK_DISABLE_MASK		0x70000000

#define IXGBE_MACC_FLU		0x00000001
#define IXGBE_MACC_FSV_10G	0x00030000
#define IXGBE_MACC_FS		0x00040000
#define IXGBE_MAC_RX2TX_LPBK	0x00000002

/* Veto Bit definition */
#define IXGBE_MMNGC_MNG_VETO	0x00000001

/* LINKS Bit Masks */
#define IXGBE_LINKS_KX_AN_COMP	0x80000000
#define IXGBE_LINKS_UP		0x40000000
#define IXGBE_LINKS_SPEED	0x20000000
#define IXGBE_LINKS_MODE	0x18000000
#define IXGBE_LINKS_RX_MODE	0x06000000
#define IXGBE_LINKS_TX_MODE	0x01800000
#define IXGBE_LINKS_XGXS_EN	0x00400000
#define IXGBE_LINKS_SGMII_EN	0x02000000
#define IXGBE_LINKS_PCS_1G_EN	0x00200000
#define IXGBE_LINKS_1G_AN_EN	0x00100000
#define IXGBE_LINKS_KX_AN_IDLE	0x00080000
#define IXGBE_LINKS_1G_SYNC	0x00040000
#define IXGBE_LINKS_10G_ALIGN	0x00020000
#define IXGBE_LINKS_10G_LANE_SYNC	0x00017000
#define IXGBE_LINKS_TL_FAULT		0x00001000
#define IXGBE_LINKS_SIGNAL		0x00000F00

#define IXGBE_LINKS_SPEED_NON_STD	0x08000000
#define IXGBE_LINKS_SPEED_82599		0x30000000
#define IXGBE_LINKS_SPEED_10G_82599	0x30000000
#define IXGBE_LINKS_SPEED_1G_82599	0x20000000
#define IXGBE_LINKS_SPEED_100_82599	0x10000000
#define IXGBE_LINKS_SPEED_10_X550EM_A	0x00000000
#define IXGBE_LINK_UP_TIME		90 /* 9.0 Seconds */
#define IXGBE_AUTO_NEG_TIME		45 /* 4.5 Seconds */

#define IXGBE_LINKS2_AN_SUPPORTED	0x00000040

/* PCS1GLSTA Bit Masks */
#define IXGBE_PCS1GLSTA_LINK_OK		1
#define IXGBE_PCS1GLSTA_SYNK_OK		0x10
#define IXGBE_PCS1GLSTA_AN_COMPLETE	0x10000
#define IXGBE_PCS1GLSTA_AN_PAGE_RX	0x20000
#define IXGBE_PCS1GLSTA_AN_TIMED_OUT	0x40000
#define IXGBE_PCS1GLSTA_AN_REMOTE_FAULT	0x80000
#define IXGBE_PCS1GLSTA_AN_ERROR_RWS	0x100000

#define IXGBE_PCS1GANA_SYM_PAUSE	0x80
#define IXGBE_PCS1GANA_ASM_PAUSE	0x100

/* PCS1GLCTL Bit Masks */
#define IXGBE_PCS1GLCTL_AN_1G_TIMEOUT_EN 0x00040000 /* PCS 1G autoneg to en */
#define IXGBE_PCS1GLCTL_FLV_LINK_UP	1
#define IXGBE_PCS1GLCTL_FORCE_LINK	0x20
#define IXGBE_PCS1GLCTL_LOW_LINK_LATCH	0x40
#define IXGBE_PCS1GLCTL_AN_ENABLE	0x10000
#define IXGBE_PCS1GLCTL_AN_RESTART	0x20000

/* ANLP1 Bit Masks */
#define IXGBE_ANLP1_PAUSE		0x0C00
#define IXGBE_ANLP1_SYM_PAUSE		0x0400
#define IXGBE_ANLP1_ASM_PAUSE		0x0800
#define IXGBE_ANLP1_AN_STATE_MASK	0x000f0000

/* SW Semaphore Register bitmasks */
#define IXGBE_SWSM_SMBI		0x00000001 /* Driver Semaphore bit */
#define IXGBE_SWSM_SWESMBI	0x00000002 /* FW Semaphore bit */
#define IXGBE_SWSM_WMNG		0x00000004 /* Wake MNG Clock */
#define IXGBE_SWFW_REGSMP	0x80000000 /* Register Semaphore bit 31 */

/* SW_FW_SYNC/GSSR definitions */
#define IXGBE_GSSR_EEP_SM		0x0001
#define IXGBE_GSSR_PHY0_SM		0x0002
#define IXGBE_GSSR_PHY1_SM		0x0004
#define IXGBE_GSSR_MAC_CSR_SM		0x0008
#define IXGBE_GSSR_FLASH_SM		0x0010
#define IXGBE_GSSR_NVM_UPDATE_SM	0x0200
#define IXGBE_GSSR_SW_MNG_SM		0x0400
#define IXGBE_GSSR_TOKEN_SM	0x40000000 /* SW bit for shared access */
#define IXGBE_GSSR_SHARED_I2C_SM 0x1806 /* Wait for both phys and both I2Cs */
#define IXGBE_GSSR_I2C_MASK	0x1800
#define IXGBE_GSSR_NVM_PHY_MASK	0xF

/* FW Status register bitmask */
#define IXGBE_FWSTS_FWRI	0x00000200 /* Firmware Reset Indication */

/* EEC Register */
#define IXGBE_EEC_SK		0x00000001 /* EEPROM Clock */
#define IXGBE_EEC_CS		0x00000002 /* EEPROM Chip Select */
#define IXGBE_EEC_DI		0x00000004 /* EEPROM Data In */
#define IXGBE_EEC_DO		0x00000008 /* EEPROM Data Out */
#define IXGBE_EEC_FWE_MASK	0x00000030 /* FLASH Write Enable */
#define IXGBE_EEC_FWE_DIS	0x00000010 /* Disable FLASH writes */
#define IXGBE_EEC_FWE_EN	0x00000020 /* Enable FLASH writes */
#define IXGBE_EEC_FWE_SHIFT	4
#define IXGBE_EEC_REQ		0x00000040 /* EEPROM Access Request */
#define IXGBE_EEC_GNT		0x00000080 /* EEPROM Access Grant */
#define IXGBE_EEC_PRES		0x00000100 /* EEPROM Present */
#define IXGBE_EEC_ARD		0x00000200 /* EEPROM Auto Read Done */
#define IXGBE_EEC_FLUP		0x00800000 /* Flash update command */
#define IXGBE_EEC_SEC1VAL	0x02000000 /* Sector 1 Valid */
#define IXGBE_EEC_FLUDONE	0x04000000 /* Flash update done */
/* EEPROM Addressing bits based on type (0-small, 1-large) */
#define IXGBE_EEC_ADDR_SIZE	0x00000400
#define IXGBE_EEC_SIZE		0x00007800 /* EEPROM Size */
#define IXGBE_EERD_MAX_ADDR	0x00003FFF /* EERD allows 14 bits for addr. */

#define IXGBE_EEC_SIZE_SHIFT		11
#define IXGBE_EEPROM_WORD_SIZE_SHIFT	6
#define IXGBE_EEPROM_OPCODE_BITS	8

/* FLA Register */
#define IXGBE_FLA_LOCKED	0x00000040

/* Part Number String Length */
#define IXGBE_PBANUM_LENGTH	11

/* Checksum and EEPROM pointers */
#define IXGBE_PBANUM_PTR_GUARD		0xFAFA
#define IXGBE_EEPROM_CHECKSUM		0x3F
#define IXGBE_EEPROM_SUM		0xBABA
#define IXGBE_EEPROM_CTRL_4		0x45
#define IXGBE_EE_CTRL_4_INST_ID		0x10
#define IXGBE_EE_CTRL_4_INST_ID_SHIFT	4
#define IXGBE_PCIE_ANALOG_PTR		0x03
#define IXGBE_ATLAS0_CONFIG_PTR		0x04
#define IXGBE_PHY_PTR			0x04
#define IXGBE_ATLAS1_CONFIG_PTR		0x05
#define IXGBE_OPTION_ROM_PTR		0x05
#define IXGBE_PCIE_GENERAL_PTR		0x06
#define IXGBE_PCIE_CONFIG0_PTR		0x07
#define IXGBE_PCIE_CONFIG1_PTR		0x08
#define IXGBE_CORE0_PTR			0x09
#define IXGBE_CORE1_PTR			0x0A
#define IXGBE_MAC0_PTR			0x0B
#define IXGBE_MAC1_PTR			0x0C
#define IXGBE_CSR0_CONFIG_PTR		0x0D
#define IXGBE_CSR1_CONFIG_PTR		0x0E
#define IXGBE_PCIE_ANALOG_PTR_X550	0x02
#define IXGBE_SHADOW_RAM_SIZE_X550	0x4000
#define IXGBE_IXGBE_PCIE_GENERAL_SIZE	0x24
#define IXGBE_PCIE_CONFIG_SIZE		0x08
#define IXGBE_EEPROM_LAST_WORD		0x41
#define IXGBE_FW_PTR			0x0F
#define IXGBE_PBANUM0_PTR		0x15
#define IXGBE_PBANUM1_PTR		0x16
#define IXGBE_ALT_MAC_ADDR_PTR		0x37
#define IXGBE_FREE_SPACE_PTR		0X3E

#define IXGBE_SAN_MAC_ADDR_PTR		0x28
#define IXGBE_DEVICE_CAPS		0x2C
#define IXGBE_82599_SERIAL_NUMBER_MAC_ADDR	0x11
#define IXGBE_X550_SERIAL_NUMBER_MAC_ADDR	0x04

#define IXGBE_PCIE_MSIX_82599_CAPS	0x72
#define IXGBE_MAX_MSIX_VECTORS_82599	0x40
#define IXGBE_PCIE_MSIX_82598_CAPS	0x62
#define IXGBE_MAX_MSIX_VECTORS_82598	0x13

/* MSI-X capability fields masks */
#define IXGBE_PCIE_MSIX_TBL_SZ_MASK	0x7FF

/* Legacy EEPROM word offsets */
#define IXGBE_ISCSI_BOOT_CAPS		0x0033
#define IXGBE_ISCSI_SETUP_PORT_0	0x0030
#define IXGBE_ISCSI_SETUP_PORT_1	0x0034

/* EEPROM Commands - SPI */
#define IXGBE_EEPROM_MAX_RETRY_SPI	5000 /* Max wait 5ms for RDY signal */
#define IXGBE_EEPROM_STATUS_RDY_SPI	0x01
#define IXGBE_EEPROM_READ_OPCODE_SPI	0x03  /* EEPROM read opcode */
#define IXGBE_EEPROM_WRITE_OPCODE_SPI	0x02  /* EEPROM write opcode */
#define IXGBE_EEPROM_A8_OPCODE_SPI	0x08  /* opcode bit-3 = addr bit-8 */
#define IXGBE_EEPROM_WREN_OPCODE_SPI	0x06  /* EEPROM set Write Ena latch */
/* EEPROM reset Write Enable latch */
#define IXGBE_EEPROM_WRDI_OPCODE_SPI	0x04
#define IXGBE_EEPROM_RDSR_OPCODE_SPI	0x05  /* EEPROM read Status reg */
#define IXGBE_EEPROM_WRSR_OPCODE_SPI	0x01  /* EEPROM write Status reg */
#define IXGBE_EEPROM_ERASE4K_OPCODE_SPI	0x20  /* EEPROM ERASE 4KB */
#define IXGBE_EEPROM_ERASE64K_OPCODE_SPI	0xD8  /* EEPROM ERASE 64KB */
#define IXGBE_EEPROM_ERASE256_OPCODE_SPI	0xDB  /* EEPROM ERASE 256B */

/* EEPROM Read Register */
#define IXGBE_EEPROM_RW_REG_DATA	16 /* data offset in EEPROM read reg */
#define IXGBE_EEPROM_RW_REG_DONE	2 /* Offset to READ done bit */
#define IXGBE_EEPROM_RW_REG_START	1 /* First bit to start operation */
#define IXGBE_EEPROM_RW_ADDR_SHIFT	2 /* Shift to the address bits */
#define IXGBE_NVM_POLL_WRITE		1 /* Flag for polling for wr complete */
#define IXGBE_NVM_POLL_READ		0 /* Flag for polling for rd complete */

#define NVM_INIT_CTRL_3		0x38
#define NVM_INIT_CTRL_3_LPLU	0x8
#define NVM_INIT_CTRL_3_D10GMP_PORT0 0x40
#define NVM_INIT_CTRL_3_D10GMP_PORT1 0x100

#define IXGBE_ETH_LENGTH_OF_ADDRESS	6

#define IXGBE_EEPROM_PAGE_SIZE_MAX	128
#define IXGBE_EEPROM_RD_BUFFER_MAX_COUNT	256 /* words rd in burst */
#define IXGBE_EEPROM_WR_BUFFER_MAX_COUNT	256 /* words wr in burst */
#define IXGBE_EEPROM_CTRL_2		1 /* EEPROM CTRL word 2 */
#define IXGBE_EEPROM_CCD_BIT		2

#ifndef IXGBE_EEPROM_GRANT_ATTEMPTS
#define IXGBE_EEPROM_GRANT_ATTEMPTS	1000 /* EEPROM attempts to gain grant */
#endif

/* Number of 5 microseconds we wait for EERD read and
 * EERW write to complete */
#define IXGBE_EERD_EEWR_ATTEMPTS	100000

/* # attempts we wait for flush update to complete */
#define IXGBE_FLUDONE_ATTEMPTS		20000

#define IXGBE_PCIE_CTRL2		0x5   /* PCIe Control 2 Offset */
#define IXGBE_PCIE_CTRL2_DUMMY_ENABLE	0x8   /* Dummy Function Enable */
#define IXGBE_PCIE_CTRL2_LAN_DISABLE	0x2   /* LAN PCI Disable */
#define IXGBE_PCIE_CTRL2_DISABLE_SELECT	0x1   /* LAN Disable Select */

#define IXGBE_SAN_MAC_ADDR_PORT0_OFFSET		0x0
#define IXGBE_SAN_MAC_ADDR_PORT1_OFFSET		0x3
#define IXGBE_DEVICE_CAPS_ALLOW_ANY_SFP		0x1
#define IXGBE_DEVICE_CAPS_FCOE_OFFLOADS		0x2
#define IXGBE_DEVICE_CAPS_NO_CROSSTALK_WR	(1 << 7)
#define IXGBE_FW_LESM_PARAMETERS_PTR		0x2
#define IXGBE_FW_LESM_STATE_1			0x1
#define IXGBE_FW_LESM_STATE_ENABLED		0x8000 /* LESM Enable bit */
#define IXGBE_FW_PASSTHROUGH_PATCH_CONFIG_PTR	0x4
#define IXGBE_FW_PATCH_VERSION_4		0x7
#define IXGBE_FCOE_IBA_CAPS_BLK_PTR		0x33 /* iSCSI/FCOE block */
#define IXGBE_FCOE_IBA_CAPS_FCOE		0x20 /* FCOE flags */
#define IXGBE_ISCSI_FCOE_BLK_PTR		0x17 /* iSCSI/FCOE block */
#define IXGBE_ISCSI_FCOE_FLAGS_OFFSET		0x0 /* FCOE flags */
#define IXGBE_ISCSI_FCOE_FLAGS_ENABLE		0x1 /* FCOE flags enable bit */
#define IXGBE_ALT_SAN_MAC_ADDR_BLK_PTR		0x27 /* Alt. SAN MAC block */
#define IXGBE_ALT_SAN_MAC_ADDR_CAPS_OFFSET	0x0 /* Alt SAN MAC capability */
#define IXGBE_ALT_SAN_MAC_ADDR_PORT0_OFFSET	0x1 /* Alt SAN MAC 0 offset */
#define IXGBE_ALT_SAN_MAC_ADDR_PORT1_OFFSET	0x4 /* Alt SAN MAC 1 offset */
#define IXGBE_ALT_SAN_MAC_ADDR_WWNN_OFFSET	0x7 /* Alt WWNN prefix offset */
#define IXGBE_ALT_SAN_MAC_ADDR_WWPN_OFFSET	0x8 /* Alt WWPN prefix offset */
#define IXGBE_ALT_SAN_MAC_ADDR_CAPS_SANMAC	0x0 /* Alt SAN MAC exists */
#define IXGBE_ALT_SAN_MAC_ADDR_CAPS_ALTWWN	0x1 /* Alt WWN base exists */

/* FW header offset */
#define IXGBE_X540_FW_PASSTHROUGH_PATCH_CONFIG_PTR	0x4
#define IXGBE_X540_FW_MODULE_MASK			0x7FFF
/* 4KB multiplier */
#define IXGBE_X540_FW_MODULE_LENGTH			0x1000
/* version word 2 (month & day) */
#define IXGBE_X540_FW_PATCH_VERSION_2		0x5
/* version word 3 (silicon compatibility & year) */
#define IXGBE_X540_FW_PATCH_VERSION_3		0x6
/* version word 4 (major & minor numbers) */
#define IXGBE_X540_FW_PATCH_VERSION_4		0x7

#define IXGBE_DEVICE_CAPS_WOL_PORT0_1	0x4 /* WoL supported on ports 0 & 1 */
#define IXGBE_DEVICE_CAPS_WOL_PORT0	0x8 /* WoL supported on port 0 */
#define IXGBE_DEVICE_CAPS_WOL_MASK	0xC /* Mask for WoL capabilities */

/* PCI Bus Info */
#define IXGBE_PCI_DEVICE_STATUS		0xAA
#define IXGBE_PCI_DEVICE_STATUS_TRANSACTION_PENDING	0x0020
#define IXGBE_PCI_LINK_STATUS		0xB2
#define IXGBE_PCI_DEVICE_CONTROL2	0xC8
#define IXGBE_PCI_LINK_WIDTH		0x3F0
#define IXGBE_PCI_LINK_WIDTH_1		0x10
#define IXGBE_PCI_LINK_WIDTH_2		0x20
#define IXGBE_PCI_LINK_WIDTH_4		0x40
#define IXGBE_PCI_LINK_WIDTH_8		0x80
#define IXGBE_PCI_LINK_SPEED		0xF
#define IXGBE_PCI_LINK_SPEED_2500	0x1
#define IXGBE_PCI_LINK_SPEED_5000	0x2
#define IXGBE_PCI_LINK_SPEED_8000	0x3
#define IXGBE_PCI_HEADER_TYPE_REGISTER	0x0E
#define IXGBE_PCI_HEADER_TYPE_MULTIFUNC	0x80
#define IXGBE_PCI_DEVICE_CONTROL2_16ms	0x0005

#define IXGBE_PCIDEVCTRL2_TIMEO_MASK	0xf
#define IXGBE_PCIDEVCTRL2_16_32ms_def	0x0
#define IXGBE_PCIDEVCTRL2_50_100us	0x1
#define IXGBE_PCIDEVCTRL2_1_2ms		0x2
#define IXGBE_PCIDEVCTRL2_16_32ms	0x5
#define IXGBE_PCIDEVCTRL2_65_130ms	0x6
#define IXGBE_PCIDEVCTRL2_260_520ms	0x9
#define IXGBE_PCIDEVCTRL2_1_2s		0xa
#define IXGBE_PCIDEVCTRL2_4_8s		0xd
#define IXGBE_PCIDEVCTRL2_17_34s	0xe

/* Number of 100 microseconds we wait for PCI Express master disable */
#define IXGBE_PCI_MASTER_DISABLE_TIMEOUT	800

/* Check whether address is multicast. This is little-endian specific check.*/
#define IXGBE_IS_MULTICAST(Address) \
		(bool)(((uint8_t *)(Address))[0] & ((uint8_t)0x01))

/* Check whether an address is broadcast. */
#define IXGBE_IS_BROADCAST(Address) \
		((((uint8_t *)(Address))[0] == ((uint8_t)0xff)) && \
		(((uint8_t *)(Address))[1] == ((uint8_t)0xff)))

/* RAH */
#define IXGBE_RAH_VIND_MASK	0x003C0000
#define IXGBE_RAH_VIND_SHIFT	18
#define IXGBE_RAH_AV		0x80000000
#define IXGBE_CLEAR_VMDQ_ALL	0xFFFFFFFF

/* Header split receive */
#define IXGBE_RFCTL_ISCSI_DIS		0x00000001
#define IXGBE_RFCTL_ISCSI_DWC_MASK	0x0000003E
#define IXGBE_RFCTL_ISCSI_DWC_SHIFT	1
#define IXGBE_RFCTL_RSC_DIS		0x00000020
#define IXGBE_RFCTL_NFSW_DIS		0x00000040
#define IXGBE_RFCTL_NFSR_DIS		0x00000080
#define IXGBE_RFCTL_NFS_VER_MASK	0x00000300
#define IXGBE_RFCTL_NFS_VER_SHIFT	8
#define IXGBE_RFCTL_NFS_VER_2		0
#define IXGBE_RFCTL_NFS_VER_3		1
#define IXGBE_RFCTL_NFS_VER_4		2
#define IXGBE_RFCTL_IPV6_DIS		0x00000400
#define IXGBE_RFCTL_IPV6_XSUM_DIS	0x00000800
#define IXGBE_RFCTL_IPFRSP_DIS		0x00004000
#define IXGBE_RFCTL_IPV6_EX_DIS		0x00010000
#define IXGBE_RFCTL_NEW_IPV6_EXT_DIS	0x00020000

/* Transmit Config masks */
#define IXGBE_TXDCTL_ENABLE		0x02000000 /* Ena specific Tx Queue */
#define IXGBE_TXDCTL_SWFLSH		0x04000000 /* Tx Desc. wr-bk flushing */
#define IXGBE_TXDCTL_WTHRESH_SHIFT	16 /* shift to WTHRESH bits */
/* Enable short packet padding to 64 bytes */
#define IXGBE_TX_PAD_ENABLE		0x00000400
#define IXGBE_JUMBO_FRAME_ENABLE	0x00000004  /* Allow jumbo frames */
/* This allows for 16K packets + 4k for vlan */
#define IXGBE_MAX_FRAME_SZ		0x40040000

#define IXGBE_TDWBAL_HEAD_WB_ENABLE	0x1 /* Tx head write-back enable */
#define IXGBE_TDWBAL_SEQNUM_WB_ENABLE	0x2 /* Tx seq# write-back enable */

/* Receive Config masks */
#define IXGBE_RXCTRL_RXEN		0x00000001 /* Enable Receiver */
#define IXGBE_RXCTRL_DMBYPS		0x00000002 /* Desc Monitor Bypass */
#define IXGBE_RXDCTL_ENABLE		0x02000000 /* Ena specific Rx Queue */
#define IXGBE_RXDCTL_SWFLSH		0x04000000 /* Rx Desc wr-bk flushing */
#define IXGBE_RXDCTL_RLPMLMASK		0x00003FFF /* X540 supported only */
#define IXGBE_RXDCTL_RLPML_EN		0x00008000
#define IXGBE_RXDCTL_VME		0x40000000 /* VLAN mode enable */

#define IXGBE_TSAUXC_EN_CLK		0x00000004
#define IXGBE_TSAUXC_SYNCLK		0x00000008
#define IXGBE_TSAUXC_SDP0_INT		0x00000040
#define IXGBE_TSAUXC_EN_TT0		0x00000001
#define IXGBE_TSAUXC_EN_TT1		0x00000002
#define IXGBE_TSAUXC_ST0		0x00000010
#define IXGBE_TSAUXC_DISABLE_SYSTIME	0x80000000

#define IXGBE_TSSDP_TS_SDP0_SEL_MASK	0x000000C0
#define IXGBE_TSSDP_TS_SDP0_CLK0	0x00000080
#define IXGBE_TSSDP_TS_SDP0_EN		0x00000100

#define IXGBE_TSYNCTXCTL_VALID		0x00000001 /* Tx timestamp valid */
#define IXGBE_TSYNCTXCTL_ENABLED	0x00000010 /* Tx timestamping enabled */

#define IXGBE_TSYNCRXCTL_VALID		0x00000001 /* Rx timestamp valid */
#define IXGBE_TSYNCRXCTL_TYPE_MASK	0x0000000E /* Rx type mask */
#define IXGBE_TSYNCRXCTL_TYPE_L2_V2	0x00
#define IXGBE_TSYNCRXCTL_TYPE_L4_V1	0x02
#define IXGBE_TSYNCRXCTL_TYPE_L2_L4_V2	0x04
#define IXGBE_TSYNCRXCTL_TYPE_ALL	0x08
#define IXGBE_TSYNCRXCTL_TYPE_EVENT_V2	0x0A
#define IXGBE_TSYNCRXCTL_ENABLED	0x00000010 /* Rx Timestamping enabled */
#define IXGBE_TSYNCRXCTL_TSIP_UT_EN	0x00800000 /* Rx Timestamp in Packet */
#define IXGBE_TSYNCRXCTL_TSIP_UP_MASK	0xFF000000 /* Rx Timestamp UP Mask */

#define IXGBE_TSIM_SYS_WRAP		0x00000001
#define IXGBE_TSIM_TXTS			0x00000002
#define IXGBE_TSIM_TADJ			0x00000080

#define IXGBE_TSICR_SYS_WRAP		IXGBE_TSIM_SYS_WRAP
#define IXGBE_TSICR_TXTS		IXGBE_TSIM_TXTS
#define IXGBE_TSICR_TADJ		IXGBE_TSIM_TADJ

#define IXGBE_RXMTRL_V1_CTRLT_MASK	0x000000FF
#define IXGBE_RXMTRL_V1_SYNC_MSG	0x00
#define IXGBE_RXMTRL_V1_DELAY_REQ_MSG	0x01
#define IXGBE_RXMTRL_V1_FOLLOWUP_MSG	0x02
#define IXGBE_RXMTRL_V1_DELAY_RESP_MSG	0x03
#define IXGBE_RXMTRL_V1_MGMT_MSG	0x04

#define IXGBE_RXMTRL_V2_MSGID_MASK	0x0000FF00
#define IXGBE_RXMTRL_V2_SYNC_MSG	0x0000
#define IXGBE_RXMTRL_V2_DELAY_REQ_MSG	0x0100
#define IXGBE_RXMTRL_V2_PDELAY_REQ_MSG	0x0200
#define IXGBE_RXMTRL_V2_PDELAY_RESP_MSG	0x0300
#define IXGBE_RXMTRL_V2_FOLLOWUP_MSG	0x0800
#define IXGBE_RXMTRL_V2_DELAY_RESP_MSG	0x0900
#define IXGBE_RXMTRL_V2_PDELAY_FOLLOWUP_MSG 0x0A00
#define IXGBE_RXMTRL_V2_ANNOUNCE_MSG	0x0B00
#define IXGBE_RXMTRL_V2_SIGNALLING_MSG	0x0C00
#define IXGBE_RXMTRL_V2_MGMT_MSG	0x0D00

#define IXGBE_FCTRL_SBP		0x00000002 /* Store Bad Packet */
#define IXGBE_FCTRL_MPE		0x00000100 /* Multicast Promiscuous Ena*/
#define IXGBE_FCTRL_UPE		0x00000200 /* Unicast Promiscuous Ena */
#define IXGBE_FCTRL_BAM		0x00000400 /* Broadcast Accept Mode */
#define IXGBE_FCTRL_PMCF	0x00001000 /* Pass MAC Control Frames */
#define IXGBE_FCTRL_DPF		0x00002000 /* Discard Pause Frame */
/* Receive Priority Flow Control Enable */
#define IXGBE_FCTRL_RPFCE	0x00004000
#define IXGBE_FCTRL_RFCE	0x00008000 /* Receive Flow Control Ena */
#define IXGBE_MFLCN_PMCF	0x00000001 /* Pass MAC Control Frames */
#define IXGBE_MFLCN_DPF		0x00000002 /* Discard Pause Frame */
#define IXGBE_MFLCN_RPFCE	0x00000004 /* Receive Priority FC Enable */
#define IXGBE_MFLCN_RFCE	0x00000008 /* Receive FC Enable */
#define IXGBE_MFLCN_RPFCE_MASK	0x00000FF4 /* Rx Priority FC bitmap mask */
#define IXGBE_MFLCN_RPFCE_SHIFT	4 /* Rx Priority FC bitmap shift */

/* Multiple Receive Queue Control */
#define IXGBE_MRQC_RSSEN	0x00000001  /* RSS Enable */
#define IXGBE_MRQC_MRQE_MASK	0xF /* Bits 3:0 */
#define IXGBE_MRQC_RT8TCEN	0x00000002 /* 8 TC no RSS */
#define IXGBE_MRQC_RT4TCEN	0x00000003 /* 4 TC no RSS */
#define IXGBE_MRQC_RTRSS8TCEN	0x00000004 /* 8 TC w/ RSS */
#define IXGBE_MRQC_RTRSS4TCEN	0x00000005 /* 4 TC w/ RSS */
#define IXGBE_MRQC_VMDQEN	0x00000008 /* VMDq2 64 pools no RSS */
#define IXGBE_MRQC_VMDQRSS32EN	0x0000000A /* VMDq2 32 pools w/ RSS */
#define IXGBE_MRQC_VMDQRSS64EN	0x0000000B /* VMDq2 64 pools w/ RSS */
#define IXGBE_MRQC_VMDQRT8TCEN	0x0000000C /* VMDq2/RT 16 pool 8 TC */
#define IXGBE_MRQC_VMDQRT4TCEN	0x0000000D /* VMDq2/RT 32 pool 4 TC */
#define IXGBE_MRQC_L3L4TXSWEN	0x00008000 /* Enable L3/L4 Tx switch */
#define IXGBE_MRQC_RSS_FIELD_MASK	0xFFFF0000
#define IXGBE_MRQC_RSS_FIELD_IPV4_TCP	0x00010000
#define IXGBE_MRQC_RSS_FIELD_IPV4	0x00020000
#define IXGBE_MRQC_RSS_FIELD_IPV6_EX_TCP 0x00040000
#define IXGBE_MRQC_RSS_FIELD_IPV6_EX	0x00080000
#define IXGBE_MRQC_RSS_FIELD_IPV6	0x00100000
#define IXGBE_MRQC_RSS_FIELD_IPV6_TCP	0x00200000
#define IXGBE_MRQC_RSS_FIELD_IPV4_UDP	0x00400000
#define IXGBE_MRQC_RSS_FIELD_IPV6_UDP	0x00800000
#define IXGBE_MRQC_RSS_FIELD_IPV6_EX_UDP 0x01000000
#define IXGBE_MRQC_MULTIPLE_RSS		0x00002000
#define IXGBE_MRQC_L3L4TXSWEN		0x00008000

/* Queue Drop Enable */
#define IXGBE_QDE_ENABLE	0x00000001
#define IXGBE_QDE_HIDE_VLAN	0x00000002
#define IXGBE_QDE_IDX_MASK	0x00007F00
#define IXGBE_QDE_IDX_SHIFT	8
#define IXGBE_QDE_WRITE		0x00010000
#define IXGBE_QDE_READ		0x00020000

#define IXGBE_TXD_POPTS_IXSM	0x01 /* Insert IP checksum */
#define IXGBE_TXD_POPTS_TXSM	0x02 /* Insert TCP/UDP checksum */
#define IXGBE_TXD_CMD_EOP	0x01000000 /* End of Packet */
#define IXGBE_TXD_CMD_IFCS	0x02000000 /* Insert FCS (Ethernet CRC) */
#define IXGBE_TXD_CMD_IC	0x04000000 /* Insert Checksum */
#define IXGBE_TXD_CMD_RS	0x08000000 /* Report Status */
#define IXGBE_TXD_CMD_DEXT	0x20000000 /* Desc extension (0 = legacy) */
#define IXGBE_TXD_CMD_VLE	0x40000000 /* Add VLAN tag */
#define IXGBE_TXD_STAT_DD	0x00000001 /* Descriptor Done */

#define IXGBE_RXDADV_IPSEC_STATUS_SECP		0x00020000
#define IXGBE_RXDADV_IPSEC_ERROR_INVALID_PROTOCOL 0x08000000
#define IXGBE_RXDADV_IPSEC_ERROR_INVALID_LENGTH	0x10000000
#define IXGBE_RXDADV_IPSEC_ERROR_AUTH_FAILED	0x18000000
#define IXGBE_RXDADV_IPSEC_ERROR_BIT_MASK	0x18000000
/* Multiple Transmit Queue Command Register */
#define IXGBE_MTQC_RT_ENA	0x1 /* DCB Enable */
#define IXGBE_MTQC_VT_ENA	0x2 /* VMDQ2 Enable */
#define IXGBE_MTQC_64Q_1PB	0x0 /* 64 queues 1 pack buffer */
#define IXGBE_MTQC_32VF		0x8 /* 4 TX Queues per pool w/32VF's */
#define IXGBE_MTQC_64VF		0x4 /* 2 TX Queues per pool w/64VF's */
#define IXGBE_MTQC_4TC_4TQ	0x8 /* 4 TC if RT_ENA and VT_ENA */
#define IXGBE_MTQC_8TC_8TQ	0xC /* 8 TC if RT_ENA or 8 TQ if VT_ENA */

/* Receive Descriptor bit definitions */
#define IXGBE_RXD_STAT_DD	0x01 /* Descriptor Done */
#define IXGBE_RXD_STAT_EOP	0x02 /* End of Packet */
#define IXGBE_RXD_STAT_FLM	0x04 /* FDir Match */
#define IXGBE_RXD_STAT_VP	0x08 /* IEEE VLAN Packet */
#define IXGBE_RXDADV_NEXTP_MASK	0x000FFFF0 /* Next Descriptor Index */
#define IXGBE_RXDADV_NEXTP_SHIFT	0x00000004
#define IXGBE_RXD_STAT_UDPCS	0x10 /* UDP xsum calculated */
#define IXGBE_RXD_STAT_L4CS	0x20 /* L4 xsum calculated */
#define IXGBE_RXD_STAT_IPCS	0x40 /* IP xsum calculated */
#define IXGBE_RXD_STAT_PIF	0x80 /* passed in-exact filter */
#define IXGBE_RXD_STAT_CRCV	0x100 /* Speculative CRC Valid */
#define IXGBE_RXD_STAT_OUTERIPCS	0x100 /* Cloud IP xsum calculated */
#define IXGBE_RXD_STAT_VEXT	0x200 /* 1st VLAN found */
#define IXGBE_RXD_STAT_UDPV	0x400 /* Valid UDP checksum */
#define IXGBE_RXD_STAT_DYNINT	0x800 /* Pkt caused INT via DYNINT */
#define IXGBE_RXD_STAT_LLINT	0x800 /* Pkt caused Low Latency Interrupt */
#define IXGBE_RXD_STAT_TSIP	0x08000 /* Time Stamp in packet buffer */
#define IXGBE_RXD_STAT_TS	0x10000 /* Time Stamp */
#define IXGBE_RXD_STAT_SECP	0x20000 /* Security Processing */
#define IXGBE_RXD_STAT_LB	0x40000 /* Loopback Status */
#define IXGBE_RXD_STAT_ACK	0x8000 /* ACK Packet indication */
#define IXGBE_RXD_ERR_CE	0x01 /* CRC Error */
#define IXGBE_RXD_ERR_LE	0x02 /* Length Error */
#define IXGBE_RXD_ERR_PE	0x08 /* Packet Error */
#define IXGBE_RXD_ERR_OSE	0x10 /* Oversize Error */
#define IXGBE_RXD_ERR_USE	0x20 /* Undersize Error */
#define IXGBE_RXD_ERR_TCPE	0x40 /* TCP/UDP Checksum Error */
#define IXGBE_RXD_ERR_IPE	0x80 /* IP Checksum Error */
#define IXGBE_RXDADV_ERR_MASK		0xfff00000 /* RDESC.ERRORS mask */
#define IXGBE_RXDADV_ERR_SHIFT		20 /* RDESC.ERRORS shift */
#define IXGBE_RXDADV_ERR_OUTERIPER	0x04000000 /* CRC IP Header error */
#define IXGBE_RXDADV_ERR_RXE		0x20000000 /* Any MAC Error */
#define IXGBE_RXDADV_ERR_FCEOFE		0x80000000 /* FCEOFe/IPE */
#define IXGBE_RXDADV_ERR_FCERR		0x00700000 /* FCERR/FDIRERR */
#define IXGBE_RXDADV_ERR_FDIR_LEN	0x00100000 /* FDIR Length error */
#define IXGBE_RXDADV_ERR_FDIR_DROP	0x00200000 /* FDIR Drop error */
#define IXGBE_RXDADV_ERR_FDIR_COLL	0x00400000 /* FDIR Collision error */
#define IXGBE_RXDADV_ERR_HBO	0x00800000 /*Header Buffer Overflow */
#define IXGBE_RXDADV_ERR_CE	0x01000000 /* CRC Error */
#define IXGBE_RXDADV_ERR_LE	0x02000000 /* Length Error */
#define IXGBE_RXDADV_ERR_PE	0x08000000 /* Packet Error */
#define IXGBE_RXDADV_ERR_OSE	0x10000000 /* Oversize Error */
#define IXGBE_RXDADV_ERR_USE	0x20000000 /* Undersize Error */
#define IXGBE_RXDADV_ERR_TCPE	0x40000000 /* TCP/UDP Checksum Error */
#define IXGBE_RXDADV_ERR_IPE	0x80000000 /* IP Checksum Error */
#define IXGBE_RXD_VLAN_ID_MASK	0x0FFF  /* VLAN ID is in lower 12 bits */
#define IXGBE_RXD_PRI_MASK	0xE000  /* Priority is in upper 3 bits */
#define IXGBE_RXD_PRI_SHIFT	13
#define IXGBE_RXD_CFI_MASK	0x1000  /* CFI is bit 12 */
#define IXGBE_RXD_CFI_SHIFT	12

#define IXGBE_RXDADV_STAT_DD		IXGBE_RXD_STAT_DD  /* Done */
#define IXGBE_RXDADV_STAT_EOP		IXGBE_RXD_STAT_EOP /* End of Packet */
#define IXGBE_RXDADV_STAT_FLM		IXGBE_RXD_STAT_FLM /* FDir Match */
#define IXGBE_RXDADV_STAT_VP		IXGBE_RXD_STAT_VP  /* IEEE VLAN Pkt */
#define IXGBE_RXDADV_STAT_MASK		0x000fffff /* Stat/NEXTP: bit 0-19 */
#define IXGBE_RXDADV_STAT_FCEOFS	0x00000040 /* FCoE EOF/SOF Stat */
#define IXGBE_RXDADV_STAT_FCSTAT	0x00000030 /* FCoE Pkt Stat */
#define IXGBE_RXDADV_STAT_FCSTAT_NOMTCH	0x00000000 /* 00: No Ctxt Match */
#define IXGBE_RXDADV_STAT_FCSTAT_NODDP	0x00000010 /* 01: Ctxt w/o DDP */
#define IXGBE_RXDADV_STAT_FCSTAT_FCPRSP	0x00000020 /* 10: Recv. FCP_RSP */
#define IXGBE_RXDADV_STAT_FCSTAT_DDP	0x00000030 /* 11: Ctxt w/ DDP */
#define IXGBE_RXDADV_STAT_TS		0x00010000 /* IEEE1588 Time Stamp */
#define IXGBE_RXDADV_STAT_TSIP		0x00008000 /* Time Stamp in packet buffer */

/* PSRTYPE bit definitions */
#define IXGBE_PSRTYPE_TCPHDR	0x00000010
#define IXGBE_PSRTYPE_UDPHDR	0x00000020
#define IXGBE_PSRTYPE_IPV4HDR	0x00000100
#define IXGBE_PSRTYPE_IPV6HDR	0x00000200
#define IXGBE_PSRTYPE_L2HDR	0x00001000

/* SRRCTL bit definitions */
#define IXGBE_SRRCTL_BSIZEPKT_SHIFT	10 /* so many KBs */
#define IXGBE_SRRCTL_BSIZEHDRSIZE_SHIFT	2 /* 64byte resolution (>> 6)
					   * + at bit 8 offset (<< 8)
					   *  = (<< 2)
					   */
#define IXGBE_SRRCTL_RDMTS_SHIFT	22
#define IXGBE_SRRCTL_RDMTS_MASK		0x01C00000
#define IXGBE_SRRCTL_DROP_EN		0x10000000
#define IXGBE_SRRCTL_BSIZEPKT_MASK	0x0000007F
#define IXGBE_SRRCTL_BSIZEHDR_MASK	0x00003F00
#define IXGBE_SRRCTL_DESCTYPE_LEGACY	0x00000000
#define IXGBE_SRRCTL_DESCTYPE_ADV_ONEBUF 0x02000000
#define IXGBE_SRRCTL_DESCTYPE_HDR_SPLIT	0x04000000
#define IXGBE_SRRCTL_DESCTYPE_HDR_REPLICATION_LARGE_PKT 0x08000000
#define IXGBE_SRRCTL_DESCTYPE_HDR_SPLIT_ALWAYS 0x0A000000
#define IXGBE_SRRCTL_DESCTYPE_MASK	0x0E000000

#define IXGBE_RXDPS_HDRSTAT_HDRSP	0x00008000
#define IXGBE_RXDPS_HDRSTAT_HDRLEN_MASK	0x000003FF

#define IXGBE_RXDADV_RSSTYPE_MASK	0x0000000F
#define IXGBE_RXDADV_PKTTYPE_MASK	0x0000FFF0
#define IXGBE_RXDADV_PKTTYPE_MASK_EX	0x0001FFF0
#define IXGBE_RXDADV_HDRBUFLEN_MASK	0x00007FE0
#define IXGBE_RXDADV_RSCCNT_MASK	0x001E0000
#define IXGBE_RXDADV_RSCCNT_SHIFT	17
#define IXGBE_RXDADV_HDRBUFLEN_SHIFT	5
#define IXGBE_RXDADV_SPLITHEADER_EN	0x00001000
#define IXGBE_RXDADV_SPH		0x8000

/* RSS Hash results */
#define IXGBE_RXDADV_RSSTYPE_NONE	0x00000000
#define IXGBE_RXDADV_RSSTYPE_IPV4_TCP	0x00000001
#define IXGBE_RXDADV_RSSTYPE_IPV4	0x00000002
#define IXGBE_RXDADV_RSSTYPE_IPV6_TCP	0x00000003
#define IXGBE_RXDADV_RSSTYPE_IPV6_EX	0x00000004
#define IXGBE_RXDADV_RSSTYPE_IPV6	0x00000005
#define IXGBE_RXDADV_RSSTYPE_IPV6_TCP_EX 0x00000006
#define IXGBE_RXDADV_RSSTYPE_IPV4_UDP	0x00000007
#define IXGBE_RXDADV_RSSTYPE_IPV6_UDP	0x00000008
#define IXGBE_RXDADV_RSSTYPE_IPV6_UDP_EX 0x00000009

/* RSS Packet Types as indicated in the receive descriptor. */
#define IXGBE_RXDADV_PKTTYPE_NONE	0x00000000
#define IXGBE_RXDADV_PKTTYPE_IPV4	0x00000010 /* IPv4 hdr present */
#define IXGBE_RXDADV_PKTTYPE_IPV4_EX	0x00000020 /* IPv4 hdr + extensions */
#define IXGBE_RXDADV_PKTTYPE_IPV6	0x00000040 /* IPv6 hdr present */
#define IXGBE_RXDADV_PKTTYPE_IPV6_EX	0x00000080 /* IPv6 hdr + extensions */
#define IXGBE_RXDADV_PKTTYPE_TCP	0x00000100 /* TCP hdr present */
#define IXGBE_RXDADV_PKTTYPE_UDP	0x00000200 /* UDP hdr present */
#define IXGBE_RXDADV_PKTTYPE_SCTP	0x00000400 /* SCTP hdr present */
#define IXGBE_RXDADV_PKTTYPE_NFS	0x00000800 /* NFS hdr present */
#define IXGBE_RXDADV_PKTTYPE_GENEVE	0x00000800 /* GENEVE hdr present */
#define IXGBE_RXDADV_PKTTYPE_VXLAN	0x00000800 /* VXLAN hdr present */
#define IXGBE_RXDADV_PKTTYPE_TUNNEL	0x00010000 /* Tunnel type */
#define IXGBE_RXDADV_PKTTYPE_IPSEC_ESP	0x00001000 /* IPSec ESP */
#define IXGBE_RXDADV_PKTTYPE_IPSEC_AH	0x00002000 /* IPSec AH */
#define IXGBE_RXDADV_PKTTYPE_LINKSEC	0x00004000 /* LinkSec Encap */
#define IXGBE_RXDADV_PKTTYPE_ETQF	0x00008000 /* PKTTYPE is ETQF index */
#define IXGBE_RXDADV_PKTTYPE_ETQF_MASK	0x00000070 /* ETQF has 8 indices */
#define IXGBE_RXDADV_PKTTYPE_ETQF_SHIFT	4 /* Right-shift 4 bits */

/* Security Processing bit Indication */
#define IXGBE_RXDADV_LNKSEC_STATUS_SECP		0x00020000
#define IXGBE_RXDADV_LNKSEC_ERROR_NO_SA_MATCH	0x08000000
#define IXGBE_RXDADV_LNKSEC_ERROR_REPLAY_ERROR	0x10000000
#define IXGBE_RXDADV_LNKSEC_ERROR_BIT_MASK	0x18000000
#define IXGBE_RXDADV_LNKSEC_ERROR_BAD_SIG	0x18000000

/* Masks to determine if packets should be dropped due to frame errors */
#define IXGBE_RXD_ERR_FRAME_ERR_MASK ( \
				IXGBE_RXD_ERR_CE | \
				IXGBE_RXD_ERR_LE | \
				IXGBE_RXD_ERR_PE | \
				IXGBE_RXD_ERR_OSE | \
				IXGBE_RXD_ERR_USE)

#define IXGBE_RXDADV_ERR_FRAME_ERR_MASK ( \
				IXGBE_RXDADV_ERR_CE | \
				IXGBE_RXDADV_ERR_LE | \
				IXGBE_RXDADV_ERR_PE | \
				IXGBE_RXDADV_ERR_OSE | \
				IXGBE_RXDADV_ERR_USE)

#define IXGBE_RXDADV_ERR_FRAME_ERR_MASK_82599	IXGBE_RXDADV_ERR_RXE

/* Multicast bit mask */
#define IXGBE_MCSTCTRL_MFE	0x4

/* Number of Transmit and Receive Descriptors must be a multiple of 8 */
#define IXGBE_REQ_TX_DESCRIPTOR_MULTIPLE	8
#define IXGBE_REQ_RX_DESCRIPTOR_MULTIPLE	8
#define IXGBE_REQ_TX_BUFFER_GRANULARITY		1024

/* Vlan-specific macros */
#define IXGBE_RX_DESC_SPECIAL_VLAN_MASK	0x0FFF /* VLAN ID in lower 12 bits */
#define IXGBE_RX_DESC_SPECIAL_PRI_MASK	0xE000 /* Priority in upper 3 bits */
#define IXGBE_RX_DESC_SPECIAL_PRI_SHIFT	0x000D /* Priority in upper 3 of 16 */
#define IXGBE_TX_DESC_SPECIAL_PRI_SHIFT	IXGBE_RX_DESC_SPECIAL_PRI_SHIFT

/* SR-IOV specific macros */
#define IXGBE_MBVFICR_INDEX(vf_number)	(vf_number >> 4)
#define IXGBE_MBVFICR(_i)		(0x00710 + ((_i) * 4))
#define IXGBE_VFLRE(_i)			(((_i & 1) ? 0x001C0 : 0x00600))
#define IXGBE_VFLREC(_i)		 (0x00700 + ((_i) * 4))
/* Translated register #defines */
#define IXGBE_PVFCTRL(P)	(0x00300 + (4 * (P)))
#define IXGBE_PVFSTATUS(P)	(0x00008 + (0 * (P)))
#define IXGBE_PVFLINKS(P)	(0x042A4 + (0 * (P)))
#define IXGBE_PVFRTIMER(P)	(0x00048 + (0 * (P)))
#define IXGBE_PVFMAILBOX(P)	(0x04C00 + (4 * (P)))
#define IXGBE_PVFRXMEMWRAP(P)	(0x03190 + (0 * (P)))
#define IXGBE_PVTEICR(P)	(0x00B00 + (4 * (P)))
#define IXGBE_PVTEICS(P)	(0x00C00 + (4 * (P)))
#define IXGBE_PVTEIMS(P)	(0x00D00 + (4 * (P)))
#define IXGBE_PVTEIMC(P)	(0x00E00 + (4 * (P)))
#define IXGBE_PVTEIAC(P)	(0x00F00 + (4 * (P)))
#define IXGBE_PVTEIAM(P)	(0x04D00 + (4 * (P)))
#define IXGBE_PVTEITR(P)	(((P) < 24) ? (0x00820 + ((P) * 4)) : \
				 (0x012300 + (((P) - 24) * 4)))
#define IXGBE_PVTIVAR(P)	(0x12500 + (4 * (P)))
#define IXGBE_PVTIVAR_MISC(P)	(0x04E00 + (4 * (P)))
#define IXGBE_PVTRSCINT(P)	(0x12000 + (4 * (P)))
#define IXGBE_VFPBACL(P)	(0x110C8 + (4 * (P)))
#define IXGBE_PVFRDBAL(P)	((P < 64) ? (0x01000 + (0x40 * (P))) \
				 : (0x0D000 + (0x40 * ((P) - 64))))
#define IXGBE_PVFRDBAH(P)	((P < 64) ? (0x01004 + (0x40 * (P))) \
				 : (0x0D004 + (0x40 * ((P) - 64))))
#define IXGBE_PVFRDLEN(P)	((P < 64) ? (0x01008 + (0x40 * (P))) \
				 : (0x0D008 + (0x40 * ((P) - 64))))
#define IXGBE_PVFRDH(P)		((P < 64) ? (0x01010 + (0x40 * (P))) \
				 : (0x0D010 + (0x40 * ((P) - 64))))
#define IXGBE_PVFRDT(P)		((P < 64) ? (0x01018 + (0x40 * (P))) \
				 : (0x0D018 + (0x40 * ((P) - 64))))
#define IXGBE_PVFRXDCTL(P)	((P < 64) ? (0x01028 + (0x40 * (P))) \
				 : (0x0D028 + (0x40 * ((P) - 64))))
#define IXGBE_PVFSRRCTL(P)	((P < 64) ? (0x01014 + (0x40 * (P))) \
				 : (0x0D014 + (0x40 * ((P) - 64))))
#define IXGBE_PVFPSRTYPE(P)	(0x0EA00 + (4 * (P)))
#define IXGBE_PVFTDBAL(P)	(0x06000 + (0x40 * (P)))
#define IXGBE_PVFTDBAH(P)	(0x06004 + (0x40 * (P)))
#define IXGBE_PVFTDLEN(P)	(0x06008 + (0x40 * (P)))
#define IXGBE_PVFTDH(P)		(0x06010 + (0x40 * (P)))
#define IXGBE_PVFTDT(P)		(0x06018 + (0x40 * (P)))
#define IXGBE_PVFTXDCTL(P)	(0x06028 + (0x40 * (P)))
#define IXGBE_PVFTDWBAL(P)	(0x06038 + (0x40 * (P)))
#define IXGBE_PVFTDWBAH(P)	(0x0603C + (0x40 * (P)))
#define IXGBE_PVFDCA_RXCTRL(P)	(((P) < 64) ? (0x0100C + (0x40 * (P))) \
				 : (0x0D00C + (0x40 * ((P) - 64))))
#define IXGBE_PVFDCA_TXCTRL(P)	(0x0600C + (0x40 * (P)))
#define IXGBE_PVFGPRC(x)	(0x0101C + (0x40 * (x)))
#define IXGBE_PVFGPTC(x)	(0x08300 + (0x04 * (x)))
#define IXGBE_PVFGORC_LSB(x)	(0x01020 + (0x40 * (x)))
#define IXGBE_PVFGORC_MSB(x)	(0x0D020 + (0x40 * (x)))
#define IXGBE_PVFGOTC_LSB(x)	(0x08400 + (0x08 * (x)))
#define IXGBE_PVFGOTC_MSB(x)	(0x08404 + (0x08 * (x)))
#define IXGBE_PVFMPRC(x)	(0x0D01C + (0x40 * (x)))

#define IXGBE_PVFTDWBALn(q_per_pool, vf_number, vf_q_index) \
		(IXGBE_PVFTDWBAL((q_per_pool)*(vf_number) + (vf_q_index)))
#define IXGBE_PVFTDWBAHn(q_per_pool, vf_number, vf_q_index) \
		(IXGBE_PVFTDWBAH((q_per_pool)*(vf_number) + (vf_q_index)))

#define IXGBE_PVFTDHn(q_per_pool, vf_number, vf_q_index) \
		(IXGBE_PVFTDH((q_per_pool)*(vf_number) + (vf_q_index)))
#define IXGBE_PVFTDTn(q_per_pool, vf_number, vf_q_index) \
		(IXGBE_PVFTDT((q_per_pool)*(vf_number) + (vf_q_index)))

/* Little Endian defines */
#ifndef __le16
#define __le16  uint16_t
#endif
#ifndef __le32
#define __le32  uint32_t
#endif
#ifndef __le64
#define __le64  uint64_t

#endif
#ifndef __be16
/* Big Endian defines */
#define __be16  uint16_t
#define __be32  uint32_t
#define __be64  uint64_t

#endif
enum ixgbe_fdir_pballoc_type {
	IXGBE_FDIR_PBALLOC_NONE = 0,
	IXGBE_FDIR_PBALLOC_64K  = 1,
	IXGBE_FDIR_PBALLOC_128K = 2,
	IXGBE_FDIR_PBALLOC_256K = 3,
};

/* Flow Director register values */
#define IXGBE_FDIRCTRL_PBALLOC_64K		0x00000001
#define IXGBE_FDIRCTRL_PBALLOC_128K		0x00000002
#define IXGBE_FDIRCTRL_PBALLOC_256K		0x00000003
#define IXGBE_FDIRCTRL_INIT_DONE		0x00000008
#define IXGBE_FDIRCTRL_PERFECT_MATCH		0x00000010
#define IXGBE_FDIRCTRL_REPORT_STATUS		0x00000020
#define IXGBE_FDIRCTRL_REPORT_STATUS_ALWAYS	0x00000080
#define IXGBE_FDIRCTRL_DROP_Q_SHIFT		8
#define IXGBE_FDIRCTRL_DROP_Q_MASK		0x00007F00
#define IXGBE_FDIRCTRL_FLEX_SHIFT		16
#define IXGBE_FDIRCTRL_DROP_NO_MATCH		0x00008000
#define IXGBE_FDIRCTRL_FILTERMODE_SHIFT		21
#define IXGBE_FDIRCTRL_FILTERMODE_MACVLAN	0x0001 /* bit 23:21, 001b */
#define IXGBE_FDIRCTRL_FILTERMODE_CLOUD		0x0002 /* bit 23:21, 010b */
#define IXGBE_FDIRCTRL_SEARCHLIM		0x00800000
#define IXGBE_FDIRCTRL_FILTERMODE_MASK		0x00E00000
#define IXGBE_FDIRCTRL_MAX_LENGTH_SHIFT		24
#define IXGBE_FDIRCTRL_FULL_THRESH_MASK		0xF0000000
#define IXGBE_FDIRCTRL_FULL_THRESH_SHIFT	28

#define IXGBE_FDIRTCPM_DPORTM_SHIFT		16
#define IXGBE_FDIRUDPM_DPORTM_SHIFT		16
#define IXGBE_FDIRIP6M_DIPM_SHIFT		16
#define IXGBE_FDIRM_VLANID			0x00000001
#define IXGBE_FDIRM_VLANP			0x00000002
#define IXGBE_FDIRM_POOL			0x00000004
#define IXGBE_FDIRM_L4P				0x00000008
#define IXGBE_FDIRM_FLEX			0x00000010
#define IXGBE_FDIRM_DIPv6			0x00000020
#define IXGBE_FDIRM_L3P				0x00000040

#define IXGBE_FDIRIP6M_INNER_MAC	0x03F0 /* bit 9:4 */
#define IXGBE_FDIRIP6M_TUNNEL_TYPE	0x0800 /* bit 11 */
#define IXGBE_FDIRIP6M_TNI_VNI		0xF000 /* bit 15:12 */
#define IXGBE_FDIRIP6M_TNI_VNI_24	0x1000 /* bit 12 */
#define IXGBE_FDIRIP6M_ALWAYS_MASK	0x040F /* bit 10, 3:0 */

#define IXGBE_FDIRFREE_FREE_MASK		0xFFFF
#define IXGBE_FDIRFREE_FREE_SHIFT		0
#define IXGBE_FDIRFREE_COLL_MASK		0x7FFF0000
#define IXGBE_FDIRFREE_COLL_SHIFT		16
#define IXGBE_FDIRLEN_MAXLEN_MASK		0x3F
#define IXGBE_FDIRLEN_MAXLEN_SHIFT		0
#define IXGBE_FDIRLEN_MAXHASH_MASK		0x7FFF0000
#define IXGBE_FDIRLEN_MAXHASH_SHIFT		16
#define IXGBE_FDIRUSTAT_ADD_MASK		0xFFFF
#define IXGBE_FDIRUSTAT_ADD_SHIFT		0
#define IXGBE_FDIRUSTAT_REMOVE_MASK		0xFFFF0000
#define IXGBE_FDIRUSTAT_REMOVE_SHIFT		16
#define IXGBE_FDIRFSTAT_FADD_MASK		0x00FF
#define IXGBE_FDIRFSTAT_FADD_SHIFT		0
#define IXGBE_FDIRFSTAT_FREMOVE_MASK		0xFF00
#define IXGBE_FDIRFSTAT_FREMOVE_SHIFT		8
#define IXGBE_FDIRPORT_DESTINATION_SHIFT	16
#define IXGBE_FDIRVLAN_FLEX_SHIFT		16
#define IXGBE_FDIRHASH_BUCKET_VALID_SHIFT	15
#define IXGBE_FDIRHASH_SIG_SW_INDEX_SHIFT	16

#define IXGBE_FDIRCMD_CMD_MASK			0x00000003
#define IXGBE_FDIRCMD_CMD_ADD_FLOW		0x00000001
#define IXGBE_FDIRCMD_CMD_REMOVE_FLOW		0x00000002
#define IXGBE_FDIRCMD_CMD_QUERY_REM_FILT	0x00000003
#define IXGBE_FDIRCMD_FILTER_VALID		0x00000004
#define IXGBE_FDIRCMD_FILTER_UPDATE		0x00000008
#define IXGBE_FDIRCMD_IPv6DMATCH		0x00000010
#define IXGBE_FDIRCMD_L4TYPE_UDP		0x00000020
#define IXGBE_FDIRCMD_L4TYPE_TCP		0x00000040
#define IXGBE_FDIRCMD_L4TYPE_SCTP		0x00000060
#define IXGBE_FDIRCMD_IPV6			0x00000080
#define IXGBE_FDIRCMD_CLEARHT			0x00000100
#define IXGBE_FDIRCMD_DROP			0x00000200
#define IXGBE_FDIRCMD_INT			0x00000400
#define IXGBE_FDIRCMD_LAST			0x00000800
#define IXGBE_FDIRCMD_COLLISION			0x00001000
#define IXGBE_FDIRCMD_QUEUE_EN			0x00008000
#define IXGBE_FDIRCMD_FLOW_TYPE_SHIFT		5
#define IXGBE_FDIRCMD_RX_QUEUE_SHIFT		16
#define IXGBE_FDIRCMD_TUNNEL_FILTER_SHIFT	23
#define IXGBE_FDIRCMD_VT_POOL_SHIFT		24
#define IXGBE_FDIR_INIT_DONE_POLL		10
#define IXGBE_FDIRCMD_CMD_POLL			10
#define IXGBE_FDIRCMD_TUNNEL_FILTER		0x00800000
#define IXGBE_FDIR_DROP_QUEUE			127


/* Manageability Host Interface defines */
#define IXGBE_HI_MAX_BLOCK_BYTE_LENGTH	1792 /* Num of bytes in range */
#define IXGBE_HI_MAX_BLOCK_DWORD_LENGTH	448 /* Num of dwords in range */
#define IXGBE_HI_COMMAND_TIMEOUT	500 /* Process HI command limit */
#define IXGBE_HI_FLASH_ERASE_TIMEOUT	1000 /* Process Erase command limit */
#define IXGBE_HI_FLASH_UPDATE_TIMEOUT	5000 /* Process Update command limit */
#define IXGBE_HI_FLASH_APPLY_TIMEOUT	0 /* Process Apply command limit */
#define IXGBE_HI_PHY_MGMT_REQ_TIMEOUT	2000 /* Wait up to 2 seconds */

/* CEM Support */
#define FW_CEM_HDR_LEN			0x4
#define FW_CEM_CMD_DRIVER_INFO		0xDD
#define FW_CEM_CMD_DRIVER_INFO_LEN	0x5
#define FW_CEM_CMD_RESERVED		0X0
#define FW_CEM_UNUSED_VER		0x0
#define FW_CEM_MAX_RETRIES		3
#define FW_CEM_RESP_STATUS_SUCCESS	0x1
#define FW_CEM_DRIVER_VERSION_SIZE	39 /* +9 would send 48 bytes to fw */
#define FW_READ_SHADOW_RAM_CMD		0x31
#define FW_READ_SHADOW_RAM_LEN		0x6
#define FW_WRITE_SHADOW_RAM_CMD		0x33
#define FW_WRITE_SHADOW_RAM_LEN		0xA /* 8 plus 1 WORD to write */
#define FW_SHADOW_RAM_DUMP_CMD		0x36
#define FW_SHADOW_RAM_DUMP_LEN		0
#define FW_DEFAULT_CHECKSUM		0xFF /* checksum always 0xFF */
#define FW_NVM_DATA_OFFSET		3
#define FW_MAX_READ_BUFFER_SIZE		1024
#define FW_DISABLE_RXEN_CMD		0xDE
#define FW_DISABLE_RXEN_LEN		0x1
#define FW_PHY_MGMT_REQ_CMD		0x20
#define FW_PHY_TOKEN_REQ_CMD		0xA
#define FW_PHY_TOKEN_REQ_LEN		2
#define FW_PHY_TOKEN_REQ		0
#define FW_PHY_TOKEN_REL		1
#define FW_PHY_TOKEN_OK			1
#define FW_PHY_TOKEN_RETRY		0x80
#define FW_PHY_TOKEN_DELAY		5	/* milliseconds */
#define FW_PHY_TOKEN_WAIT		5	/* seconds */
#define FW_PHY_TOKEN_RETRIES ((FW_PHY_TOKEN_WAIT * 1000) / FW_PHY_TOKEN_DELAY)
#define FW_INT_PHY_REQ_CMD		0xB
#define FW_INT_PHY_REQ_LEN		10
#define FW_INT_PHY_REQ_READ		0
#define FW_INT_PHY_REQ_WRITE		1
#define FW_PHY_ACT_REQ_CMD		5
#define FW_PHY_ACT_DATA_COUNT		4
#define FW_PHY_ACT_REQ_LEN		(4 + 4 * FW_PHY_ACT_DATA_COUNT)
#define FW_PHY_ACT_INIT_PHY		1
#define FW_PHY_ACT_SETUP_LINK		2
#define FW_PHY_ACT_LINK_SPEED_10	(1u << 0)
#define FW_PHY_ACT_LINK_SPEED_100	(1u << 1)
#define FW_PHY_ACT_LINK_SPEED_1G	(1u << 2)
#define FW_PHY_ACT_LINK_SPEED_2_5G	(1u << 3)
#define FW_PHY_ACT_LINK_SPEED_5G	(1u << 4)
#define FW_PHY_ACT_LINK_SPEED_10G	(1u << 5)
#define FW_PHY_ACT_LINK_SPEED_20G	(1u << 6)
#define FW_PHY_ACT_LINK_SPEED_25G	(1u << 7)
#define FW_PHY_ACT_LINK_SPEED_40G	(1u << 8)
#define FW_PHY_ACT_LINK_SPEED_50G	(1u << 9)
#define FW_PHY_ACT_LINK_SPEED_100G	(1u << 10)
#define FW_PHY_ACT_SETUP_LINK_PAUSE_SHIFT 16
#define FW_PHY_ACT_SETUP_LINK_PAUSE_MASK (3u << \
					  FW_PHY_ACT_SETUP_LINK_PAUSE_SHIFT)
#define FW_PHY_ACT_SETUP_LINK_PAUSE_NONE 0u
#define FW_PHY_ACT_SETUP_LINK_PAUSE_TX	1u
#define FW_PHY_ACT_SETUP_LINK_PAUSE_RX	2u
#define FW_PHY_ACT_SETUP_LINK_PAUSE_RXTX 3u
#define FW_PHY_ACT_SETUP_LINK_LP	(1u << 18)
#define FW_PHY_ACT_SETUP_LINK_HP	(1u << 19)
#define FW_PHY_ACT_SETUP_LINK_EEE	(1u << 20)
#define FW_PHY_ACT_SETUP_LINK_AN	(1u << 22)
#define FW_PHY_ACT_SETUP_LINK_RSP_DOWN	(1u << 0)
#define FW_PHY_ACT_GET_LINK_INFO	3
#define FW_PHY_ACT_GET_LINK_INFO_EEE	(1u << 19)
#define FW_PHY_ACT_GET_LINK_INFO_FC_TX	(1u << 20)
#define FW_PHY_ACT_GET_LINK_INFO_FC_RX	(1u << 21)
#define FW_PHY_ACT_GET_LINK_INFO_POWER	(1u << 22)
#define FW_PHY_ACT_GET_LINK_INFO_AN_COMPLETE	(1u << 24)
#define FW_PHY_ACT_GET_LINK_INFO_TEMP	(1u << 25)
#define FW_PHY_ACT_GET_LINK_INFO_LP_FC_TX	(1u << 28)
#define FW_PHY_ACT_GET_LINK_INFO_LP_FC_RX	(1u << 29)
#define FW_PHY_ACT_FORCE_LINK_DOWN	4
#define FW_PHY_ACT_FORCE_LINK_DOWN_OFF	(1u << 0)
#define FW_PHY_ACT_PHY_SW_RESET		5
#define FW_PHY_ACT_PHY_HW_RESET		6
#define FW_PHY_ACT_GET_PHY_INFO		7
#define FW_PHY_ACT_UD_2			0x1002
#define FW_PHY_ACT_UD_2_10G_KR_EEE	(1u << 6)
#define FW_PHY_ACT_UD_2_10G_KX4_EEE	(1u << 5)
#define FW_PHY_ACT_UD_2_1G_KX_EEE	(1u << 4)
#define FW_PHY_ACT_UD_2_10G_T_EEE	(1u << 3)
#define FW_PHY_ACT_UD_2_1G_T_EEE	(1u << 2)
#define FW_PHY_ACT_UD_2_100M_TX_EEE	(1u << 1)
#define FW_PHY_ACT_RETRIES		50
#define FW_PHY_INFO_SPEED_MASK		0xFFFu
#define FW_PHY_INFO_ID_HI_MASK		0xFFFF0000u
#define FW_PHY_INFO_ID_LO_MASK		0x0000FFFFu

/* Host Interface Command Structures */

struct ixgbe_hic_hdr {
	uint8_t cmd;
	uint8_t buf_len;
	union {
		uint8_t cmd_resv;
		uint8_t ret_status;
	} cmd_or_resp;
	uint8_t checksum;
} __packed __aligned(4);

struct ixgbe_hic_hdr2_req {
	uint8_t cmd;
	uint8_t buf_lenh;
	uint8_t buf_lenl;
	uint8_t checksum;
} __packed __aligned(4);

struct ixgbe_hic_hdr2_rsp {
	uint8_t cmd;
	uint8_t buf_lenl;
	uint8_t buf_lenh_status;	/* 7-5: high bits of buf_len, 4-0: status */
	uint8_t checksum;
} __packed __aligned(4);

union ixgbe_hic_hdr2 {
	struct ixgbe_hic_hdr2_req req;
	struct ixgbe_hic_hdr2_rsp rsp;
} __packed __aligned(4);

struct ixgbe_hic_drv_info {
	struct ixgbe_hic_hdr hdr;
	uint8_t port_num;
	uint8_t ver_sub;
	uint8_t ver_build;
	uint8_t ver_min;
	uint8_t ver_maj;
	uint8_t pad; /* end spacing to ensure length is mult. of dword */
	uint16_t pad2; /* end spacing to ensure length is mult. of dword2 */
} __packed __aligned(4);

struct ixgbe_hic_drv_info2 {
	struct ixgbe_hic_hdr hdr;
	uint8_t port_num;
	uint8_t ver_sub;
	uint8_t ver_build;
	uint8_t ver_min;
	uint8_t ver_maj;
	char driver_string[FW_CEM_DRIVER_VERSION_SIZE];
} __packed __aligned(4);

/* These need to be dword aligned */
struct ixgbe_hic_read_shadow_ram {
	union ixgbe_hic_hdr2 hdr;
	uint32_t address;
	uint16_t length;
	uint16_t pad2;
	uint16_t data;
	uint16_t pad3;
} __packed __aligned(4);

struct ixgbe_hic_write_shadow_ram {
	union ixgbe_hic_hdr2 hdr;
	uint32_t address;
	uint16_t length;
	uint16_t pad2;
	uint16_t data;
	uint16_t pad3;
} __packed __aligned(4);

struct ixgbe_hic_disable_rxen {
	struct ixgbe_hic_hdr hdr;
	uint8_t  port_number;
	uint8_t  pad2;
	uint16_t pad3;
} __packed __aligned(4);

struct ixgbe_hic_phy_token_req {
	struct ixgbe_hic_hdr hdr;
	uint8_t port_number;
	uint8_t command_type;
	uint16_t pad;
} __packed __aligned(4);

struct ixgbe_hic_internal_phy_req {
	struct ixgbe_hic_hdr hdr;
	uint8_t port_number;
	uint8_t command_type;
	__be16 address;
	uint16_t rsv1;
	__be32 write_data;
	uint16_t pad;
} __packed __aligned(4);

struct ixgbe_hic_internal_phy_resp {
	struct ixgbe_hic_hdr hdr;
	__be32 read_data;
} __packed __aligned(4);

struct ixgbe_hic_phy_activity_req {
	struct ixgbe_hic_hdr hdr;
	uint8_t port_number;
	uint8_t pad;
	__le16 activity_id;
	__be32 data[FW_PHY_ACT_DATA_COUNT];
} __packed __aligned(4);

struct ixgbe_hic_phy_activity_resp {
	struct ixgbe_hic_hdr hdr;
	__be32 data[FW_PHY_ACT_DATA_COUNT];
} __packed __aligned(4);

/* Transmit Descriptor - Legacy */
struct ixgbe_legacy_tx_desc {
	uint64_t buffer_addr; /* Address of the descriptor's data buffer */
	union {
		__le32 data;
		struct {
			__le16 length; /* Data buffer length */
			uint8_t cso; /* Checksum offset */
			uint8_t cmd; /* Descriptor control */
		} flags;
	} lower;
	union {
		__le32 data;
		struct {
			uint8_t status; /* Descriptor status */
			uint8_t css; /* Checksum start */
			__le16 vlan;
		} fields;
	} upper;
};

/* Transmit Descriptor - Advanced */
union ixgbe_adv_tx_desc {
	struct {
		__le64 buffer_addr; /* Address of descriptor's data buf */
		__le32 cmd_type_len;
		__le32 olinfo_status;
	} read;
	struct {
		__le64 rsvd; /* Reserved */
		__le32 nxtseq_seed;
		__le32 status;
	} wb;
};

/* Receive Descriptor - Legacy */
struct ixgbe_legacy_rx_desc {
	__le64 buffer_addr; /* Address of the descriptor's data buffer */
	__le16 length; /* Length of data DMAed into data buffer */
	__le16 csum; /* Packet checksum */
	uint8_t status;   /* Descriptor status */
	uint8_t errors;   /* Descriptor Errors */
	__le16 vlan;
};

/* Receive Descriptor - Advanced */
union ixgbe_adv_rx_desc {
	struct {
		__le64 pkt_addr; /* Packet buffer address */
		__le64 hdr_addr; /* Header buffer address */
	} read;
	struct {
		struct {
			union {
				__le32 data;
				struct {
					__le16 pkt_info; /* RSS, Pkt type */
					__le16 hdr_info; /* Splithdr, hdrlen */
				} hs_rss;
			} lo_dword;
			union {
				__le32 rss; /* RSS Hash */
				struct {
					__le16 ip_id; /* IP id */
					__le16 csum; /* Packet Checksum */
				} csum_ip;
			} hi_dword;
		} lower;
		struct {
			__le32 status_error; /* ext status/error */
			__le16 length; /* Packet length */
			__le16 vlan; /* VLAN tag */
		} upper;
	} wb;  /* writeback */
};

/* Context descriptors */
struct ixgbe_adv_tx_context_desc {
	__le32 vlan_macip_lens;
	__le32 seqnum_seed;
	__le32 type_tucmd_mlhl;
	__le32 mss_l4len_idx;
};

/* Adv Transmit Descriptor Config Masks */
#define IXGBE_ADVTXD_DTALEN_MASK	0x0000FFFF /* Data buf length(bytes) */
#define IXGBE_ADVTXD_MAC_LINKSEC	0x00040000 /* Insert LinkSec */
#define IXGBE_ADVTXD_MAC_TSTAMP		0x00080000 /* IEEE1588 time stamp */
#define IXGBE_ADVTXD_IPSEC_SA_INDEX_MASK 0x000003FF /* IPSec SA index */
#define IXGBE_ADVTXD_IPSEC_ESP_LEN_MASK	0x000001FF /* IPSec ESP length */
#define IXGBE_ADVTXD_DTYP_MASK		0x00F00000 /* DTYP mask */
#define IXGBE_ADVTXD_DTYP_CTXT		0x00200000 /* Adv Context Desc */
#define IXGBE_ADVTXD_DTYP_DATA		0x00300000 /* Adv Data Descriptor */
#define IXGBE_ADVTXD_DCMD_EOP		IXGBE_TXD_CMD_EOP  /* End of Packet */
#define IXGBE_ADVTXD_DCMD_IFCS		IXGBE_TXD_CMD_IFCS /* Insert FCS */
#define IXGBE_ADVTXD_DCMD_RS		IXGBE_TXD_CMD_RS /* Report Status */
#define IXGBE_ADVTXD_DCMD_DDTYP_ISCSI	0x10000000 /* DDP hdr type or iSCSI */
#define IXGBE_ADVTXD_DCMD_DEXT		IXGBE_TXD_CMD_DEXT /* Desc ext 1=Adv */
#define IXGBE_ADVTXD_DCMD_VLE		IXGBE_TXD_CMD_VLE  /* VLAN pkt enable */
#define IXGBE_ADVTXD_DCMD_TSE		0x80000000 /* TCP Seg enable */
#define IXGBE_ADVTXD_STAT_DD		IXGBE_TXD_STAT_DD  /* Descriptor Done */
#define IXGBE_ADVTXD_STAT_SN_CRC	0x00000002 /* NXTSEQ/SEED pres in WB */
#define IXGBE_ADVTXD_STAT_RSV		0x0000000C /* STA Reserved */
#define IXGBE_ADVTXD_IDX_SHIFT		4 /* Adv desc Index shift */
#define IXGBE_ADVTXD_CC			0x00000080 /* Check Context */
#define IXGBE_ADVTXD_POPTS_SHIFT	8  /* Adv desc POPTS shift */
#define IXGBE_ADVTXD_POPTS_IXSM		(IXGBE_TXD_POPTS_IXSM << \
					 IXGBE_ADVTXD_POPTS_SHIFT)
#define IXGBE_ADVTXD_POPTS_TXSM		(IXGBE_TXD_POPTS_TXSM << \
					 IXGBE_ADVTXD_POPTS_SHIFT)
#define IXGBE_ADVTXD_POPTS_ISCO_1ST	0x00000000 /* 1st TSO of iSCSI PDU */
#define IXGBE_ADVTXD_POPTS_ISCO_MDL	0x00000800 /* Middle TSO of iSCSI PDU */
#define IXGBE_ADVTXD_POPTS_ISCO_LAST	0x00001000 /* Last TSO of iSCSI PDU */
/* 1st&Last TSO-full iSCSI PDU */
#define IXGBE_ADVTXD_POPTS_ISCO_FULL	0x00001800
#define IXGBE_ADVTXD_POPTS_RSV		0x00002000 /* POPTS Reserved */
#define IXGBE_ADVTXD_PAYLEN_MASK	0x0003FFFF /* Adv desc PAYLEN */
#define IXGBE_ADVTXD_PAYLEN_SHIFT	14 /* Adv desc PAYLEN shift */
#define IXGBE_ADVTXD_MACLEN_SHIFT	9  /* Adv ctxt desc mac len shift */
#define IXGBE_ADVTXD_VLAN_SHIFT		16  /* Adv ctxt vlan tag shift */
#define IXGBE_ADVTXD_TUCMD_IPV4		0x00000400 /* IP Packet Type: 1=IPv4 */
#define IXGBE_ADVTXD_TUCMD_IPV6		0x00000000 /* IP Packet Type: 0=IPv6 */
#define IXGBE_ADVTXD_TUCMD_L4T_UDP	0x00000000 /* L4 Packet TYPE of UDP */
#define IXGBE_ADVTXD_TUCMD_L4T_TCP	0x00000800 /* L4 Packet TYPE of TCP */
#define IXGBE_ADVTXD_TUCMD_L4T_SCTP	0x00001000 /* L4 Packet TYPE of SCTP */
#define IXGBE_ADVTXD_TUCMD_L4T_RSV	0x00001800 /* RSV L4 Packet TYPE */
#define IXGBE_ADVTXD_TUCMD_MKRREQ	0x00002000 /* req Markers and CRC */
#define IXGBE_ADVTXD_POPTS_IPSEC	0x00000400 /* IPSec offload request */
#define IXGBE_ADVTXD_TUCMD_IPSEC_TYPE_ESP 0x00002000 /* IPSec Type ESP */
#define IXGBE_ADVTXD_TUCMD_IPSEC_ENCRYPT_EN 0x00004000/* ESP Encrypt Enable */
#define IXGBE_ADVTXT_TUCMD_FCOE		0x00008000 /* FCoE Frame Type */
#define IXGBE_ADVTXD_FCOEF_EOF_MASK	(0x3 << 10) /* FC EOF index */
#define IXGBE_ADVTXD_FCOEF_SOF		((1 << 2) << 10) /* FC SOF index */
#define IXGBE_ADVTXD_FCOEF_PARINC	((1 << 3) << 10) /* Rel_Off in F_CTL */
#define IXGBE_ADVTXD_FCOEF_ORIE		((1 << 4) << 10) /* Orientation End */
#define IXGBE_ADVTXD_FCOEF_ORIS		((1 << 5) << 10) /* Orientation Start */
#define IXGBE_ADVTXD_FCOEF_EOF_N	(0x0 << 10) /* 00: EOFn */
#define IXGBE_ADVTXD_FCOEF_EOF_T	(0x1 << 10) /* 01: EOFt */
#define IXGBE_ADVTXD_FCOEF_EOF_NI	(0x2 << 10) /* 10: EOFni */
#define IXGBE_ADVTXD_FCOEF_EOF_A	(0x3 << 10) /* 11: EOFa */
#define IXGBE_ADVTXD_L4LEN_SHIFT	8  /* Adv ctxt L4LEN shift */
#define IXGBE_ADVTXD_MSS_SHIFT		16  /* Adv ctxt MSS shift */

#define IXGBE_ADVTXD_OUTER_IPLEN	16 /* Adv ctxt OUTERIPLEN shift */
#define IXGBE_ADVTXD_TUNNEL_LEN 	24 /* Adv ctxt TUNNELLEN shift */
#define IXGBE_ADVTXD_TUNNEL_TYPE_SHIFT	16 /* Adv Tx Desc Tunnel Type shift */
#define IXGBE_ADVTXD_OUTERIPCS_SHIFT	17 /* Adv Tx Desc OUTERIPCS Shift */
#define IXGBE_ADVTXD_TUNNEL_TYPE_NVGRE	1  /* Adv Tx Desc Tunnel Type NVGRE */
/* Adv Tx Desc OUTERIPCS Shift for X550EM_a */
#define IXGBE_ADVTXD_OUTERIPCS_SHIFT_X550EM_a	26
/* Autonegotiation advertised speeds */
typedef uint32_t ixgbe_autoneg_advertised;
/* Link speed */
typedef uint32_t ixgbe_link_speed;
#define IXGBE_LINK_SPEED_UNKNOWN	0
#define IXGBE_LINK_SPEED_10_FULL	0x0002
#define IXGBE_LINK_SPEED_100_FULL	0x0008
#define IXGBE_LINK_SPEED_1GB_FULL	0x0020
#define IXGBE_LINK_SPEED_2_5GB_FULL	0x0400
#define IXGBE_LINK_SPEED_5GB_FULL	0x0800
#define IXGBE_LINK_SPEED_10GB_FULL	0x0080
#define IXGBE_LINK_SPEED_82598_AUTONEG	(IXGBE_LINK_SPEED_1GB_FULL | \
					 IXGBE_LINK_SPEED_10GB_FULL)
#define IXGBE_LINK_SPEED_82599_AUTONEG	(IXGBE_LINK_SPEED_100_FULL | \
					 IXGBE_LINK_SPEED_1GB_FULL | \
					 IXGBE_LINK_SPEED_10GB_FULL)

/* Physical layer type */
typedef uint64_t ixgbe_physical_layer;
#define IXGBE_PHYSICAL_LAYER_UNKNOWN		0
#define IXGBE_PHYSICAL_LAYER_10GBASE_T		0x00001
#define IXGBE_PHYSICAL_LAYER_1000BASE_T		0x00002
#define IXGBE_PHYSICAL_LAYER_100BASE_TX		0x00004
#define IXGBE_PHYSICAL_LAYER_SFP_PLUS_CU	0x00008
#define IXGBE_PHYSICAL_LAYER_10GBASE_LR		0x00010
#define IXGBE_PHYSICAL_LAYER_10GBASE_LRM	0x00020
#define IXGBE_PHYSICAL_LAYER_10GBASE_SR		0x00040
#define IXGBE_PHYSICAL_LAYER_10GBASE_KX4	0x00080
#define IXGBE_PHYSICAL_LAYER_10GBASE_CX4	0x00100
#define IXGBE_PHYSICAL_LAYER_1000BASE_KX	0x00200
#define IXGBE_PHYSICAL_LAYER_1000BASE_BX	0x00400
#define IXGBE_PHYSICAL_LAYER_10GBASE_KR		0x00800
#define IXGBE_PHYSICAL_LAYER_10GBASE_XAUI	0x01000
#define IXGBE_PHYSICAL_LAYER_SFP_ACTIVE_DA	0x02000
#define IXGBE_PHYSICAL_LAYER_1000BASE_SX	0x04000
#define IXGBE_PHYSICAL_LAYER_10BASE_T		0x08000
#define IXGBE_PHYSICAL_LAYER_2500BASE_KX	0x10000
#define IXGBE_PHYSICAL_LAYER_1000BASE_LX	0x20000

/* Flow Control Data Sheet defined values
 * Calculation and defines taken from 802.1bb Annex O
 */

/* BitTimes (BT) conversion */
#define IXGBE_BT2KB(BT)		((BT + (8 * 1024 - 1)) / (8 * 1024))
#define IXGBE_B2BT(BT)		(BT * 8)

/* Calculate Delay to respond to PFC */
#define IXGBE_PFC_D	672

/* Calculate Cable Delay */
#define IXGBE_CABLE_DC	5556 /* Delay Copper */
#define IXGBE_CABLE_DO	5000 /* Delay Optical */

/* Calculate Interface Delay X540 */
#define IXGBE_PHY_DC	25600 /* Delay 10G BASET */
#define IXGBE_MAC_DC	8192  /* Delay Copper XAUI interface */
#define IXGBE_XAUI_DC	(2 * 2048) /* Delay Copper Phy */

#define IXGBE_ID_X540	(IXGBE_MAC_DC + IXGBE_XAUI_DC + IXGBE_PHY_DC)

/* Calculate Interface Delay 82598, 82599 */
#define IXGBE_PHY_D	12800
#define IXGBE_MAC_D	4096
#define IXGBE_XAUI_D	(2 * 1024)

#define IXGBE_ID	(IXGBE_MAC_D + IXGBE_XAUI_D + IXGBE_PHY_D)

/* Calculate Delay incurred from higher layer */
#define IXGBE_HD	6144

/* Calculate PCI Bus delay for low thresholds */
#define IXGBE_PCI_DELAY	10000

/* Calculate X540 delay value in bit times */
#define IXGBE_DV_X540(_max_frame_link, _max_frame_tc) \
			((36 * \
			  (IXGBE_B2BT(_max_frame_link) + \
			   IXGBE_PFC_D + \
			   (2 * IXGBE_CABLE_DC) + \
			   (2 * IXGBE_ID_X540) + \
			   IXGBE_HD) / 25 + 1) + \
			 2 * IXGBE_B2BT(_max_frame_tc))

/* Calculate 82599, 82598 delay value in bit times */
#define IXGBE_DV(_max_frame_link, _max_frame_tc) \
			((36 * \
			  (IXGBE_B2BT(_max_frame_link) + \
			   IXGBE_PFC_D + \
			   (2 * IXGBE_CABLE_DC) + \
			   (2 * IXGBE_ID) + \
			   IXGBE_HD) / 25 + 1) + \
			 2 * IXGBE_B2BT(_max_frame_tc))

/* Calculate low threshold delay values */
#define IXGBE_LOW_DV_X540(_max_frame_tc) \
			(2 * IXGBE_B2BT(_max_frame_tc) + \
			(36 * IXGBE_PCI_DELAY / 25) + 1)
#define IXGBE_LOW_DV(_max_frame_tc) \
			(2 * IXGBE_LOW_DV_X540(_max_frame_tc))

/* Software ATR hash keys */
#define IXGBE_ATR_BUCKET_HASH_KEY	0x3DAD14E2
#define IXGBE_ATR_SIGNATURE_HASH_KEY	0x174D3614

/* Software ATR input stream values and masks */
#define IXGBE_ATR_HASH_MASK		0x7fff
#define IXGBE_ATR_L4TYPE_MASK		0x3
#define IXGBE_ATR_L4TYPE_UDP		0x1
#define IXGBE_ATR_L4TYPE_TCP		0x2
#define IXGBE_ATR_L4TYPE_SCTP		0x3
#define IXGBE_ATR_L4TYPE_IPV6_MASK	0x4
#define IXGBE_ATR_L4TYPE_TUNNEL_MASK	0x10
enum ixgbe_atr_flow_type {
	IXGBE_ATR_FLOW_TYPE_IPV4	= 0x0,
	IXGBE_ATR_FLOW_TYPE_UDPV4	= 0x1,
	IXGBE_ATR_FLOW_TYPE_TCPV4	= 0x2,
	IXGBE_ATR_FLOW_TYPE_SCTPV4	= 0x3,
	IXGBE_ATR_FLOW_TYPE_IPV6	= 0x4,
	IXGBE_ATR_FLOW_TYPE_UDPV6	= 0x5,
	IXGBE_ATR_FLOW_TYPE_TCPV6	= 0x6,
	IXGBE_ATR_FLOW_TYPE_SCTPV6	= 0x7,
	IXGBE_ATR_FLOW_TYPE_TUNNELED_IPV4	= 0x10,
	IXGBE_ATR_FLOW_TYPE_TUNNELED_UDPV4	= 0x11,
	IXGBE_ATR_FLOW_TYPE_TUNNELED_TCPV4	= 0x12,
	IXGBE_ATR_FLOW_TYPE_TUNNELED_SCTPV4	= 0x13,
	IXGBE_ATR_FLOW_TYPE_TUNNELED_IPV6	= 0x14,
	IXGBE_ATR_FLOW_TYPE_TUNNELED_UDPV6	= 0x15,
	IXGBE_ATR_FLOW_TYPE_TUNNELED_TCPV6	= 0x16,
	IXGBE_ATR_FLOW_TYPE_TUNNELED_SCTPV6	= 0x17,
};

/* Flow Director ATR input struct. */
union ixgbe_atr_input {
	/*
	 * Byte layout in order, all values with MSB first:
	 *
	 * vm_pool	- 1 byte
	 * flow_type	- 1 byte
	 * vlan_id	- 2 bytes
	 * src_ip	- 16 bytes
	 * inner_mac	- 6 bytes
	 * cloud_mode	- 2 bytes
	 * tni_vni	- 4 bytes
	 * dst_ip	- 16 bytes
	 * src_port	- 2 bytes
	 * dst_port	- 2 bytes
	 * flex_bytes	- 2 bytes
	 * bkt_hash	- 2 bytes
	 */
	struct {
		uint8_t vm_pool;
		uint8_t flow_type;
		__be16 vlan_id;
		__be32 dst_ip[4];
		__be32 src_ip[4];
		uint8_t inner_mac[6];
		__be16 tunnel_type;
		__be32 tni_vni;
		__be16 src_port;
		__be16 dst_port;
		__be16 flex_bytes;
		__be16 bkt_hash;
	} formatted;
	__be32 dword_stream[14];
};

/* Flow Director compressed ATR hash input struct */
union ixgbe_atr_hash_dword {
	struct {
		uint8_t vm_pool;
		uint8_t flow_type;
		__be16 vlan_id;
	} formatted;
	__be32 ip;
	struct {
		__be16 src;
		__be16 dst;
	} port;
	__be16 flex_bytes;
	__be32 dword;
};


#define IXGBE_MVALS_INIT(m)	\
	IXGBE_CAT(EEC, m),		\
	IXGBE_CAT(FLA, m),		\
	IXGBE_CAT(GRC, m),		\
	IXGBE_CAT(SRAMREL, m),		\
	IXGBE_CAT(FACTPS, m),		\
	IXGBE_CAT(SWSM, m),		\
	IXGBE_CAT(SWFW_SYNC, m),	\
	IXGBE_CAT(FWSM, m),		\
	IXGBE_CAT(SDP0_GPIEN, m),	\
	IXGBE_CAT(SDP1_GPIEN, m),	\
	IXGBE_CAT(SDP2_GPIEN, m),	\
	IXGBE_CAT(EICR_GPI_SDP0, m),	\
	IXGBE_CAT(EICR_GPI_SDP1, m),	\
	IXGBE_CAT(EICR_GPI_SDP2, m),	\
	IXGBE_CAT(CIAA, m),		\
	IXGBE_CAT(CIAD, m),		\
	IXGBE_CAT(I2C_CLK_IN, m),	\
	IXGBE_CAT(I2C_CLK_OUT, m),	\
	IXGBE_CAT(I2C_DATA_IN, m),	\
	IXGBE_CAT(I2C_DATA_OUT, m),	\
	IXGBE_CAT(I2C_DATA_OE_N_EN, m),	\
	IXGBE_CAT(I2C_BB_EN, m),	\
	IXGBE_CAT(I2C_CLK_OE_N_EN, m),	\
	IXGBE_CAT(I2CCTL, m)

enum ixgbe_mvals {
	IXGBE_MVALS_INIT(_IDX),
	IXGBE_MVALS_IDX_LIMIT
};

/*
 * Unavailable: The FCoE Boot Option ROM is not present in the flash.
 * Disabled: Present; boot order is not set for any targets on the port.
 * Enabled: Present; boot order is set for at least one target on the port.
 */
enum ixgbe_fcoe_boot_status {
	ixgbe_fcoe_bootstatus_disabled = 0,
	ixgbe_fcoe_bootstatus_enabled = 1,
	ixgbe_fcoe_bootstatus_unavailable = 0xFFFF
};

enum ixgbe_eeprom_type {
	ixgbe_eeprom_uninitialized = 0,
	ixgbe_eeprom_spi,
	ixgbe_flash,
	ixgbe_eeprom_none /* No NVM support */
};

enum ixgbe_mac_type {
	ixgbe_mac_unknown = 0,
	ixgbe_mac_82598EB,
	ixgbe_mac_82599EB,
	ixgbe_mac_82599_vf,
	ixgbe_mac_X540,
	ixgbe_mac_X540_vf,
	ixgbe_mac_X550,
	ixgbe_mac_X550EM_x,
	ixgbe_mac_X550EM_a,
	ixgbe_mac_X550_vf,
	ixgbe_mac_X550EM_x_vf,
	ixgbe_mac_X550EM_a_vf,
	ixgbe_num_macs
};

enum ixgbe_phy_type {
	ixgbe_phy_unknown = 0,
	ixgbe_phy_none,
	ixgbe_phy_tn,
	ixgbe_phy_aq,
	ixgbe_phy_x550em_kr,
	ixgbe_phy_x550em_kx4,
	ixgbe_phy_x550em_xfi,
	ixgbe_phy_x550em_ext_t,
	ixgbe_phy_ext_1g_t,
	ixgbe_phy_cu_unknown,
	ixgbe_phy_qt,
	ixgbe_phy_xaui,
	ixgbe_phy_nl,
	ixgbe_phy_sfp_passive_tyco,
	ixgbe_phy_sfp_passive_unknown,
	ixgbe_phy_sfp_active_unknown,
	ixgbe_phy_sfp_avago,
	ixgbe_phy_sfp_ftl,
	ixgbe_phy_sfp_ftl_active,
	ixgbe_phy_sfp_unknown,
	ixgbe_phy_sfp_intel,
	ixgbe_phy_qsfp_passive_unknown,
	ixgbe_phy_qsfp_active_unknown,
	ixgbe_phy_qsfp_intel,
	ixgbe_phy_qsfp_unknown,
	ixgbe_phy_sfp_unsupported, /*Enforce bit set with unsupported module*/
	ixgbe_phy_sgmii,
	ixgbe_phy_fw,
	ixgbe_phy_generic
};

/*
 * SFP+ module type IDs:
 *
 * ID	Module Type
 * =============
 * 0	SFP_DA_CU
 * 1	SFP_SR
 * 2	SFP_LR
 * 3	SFP_DA_CU_CORE0 - 82599-specific
 * 4	SFP_DA_CU_CORE1 - 82599-specific
 * 5	SFP_SR/LR_CORE0 - 82599-specific
 * 6	SFP_SR/LR_CORE1 - 82599-specific
 */
enum ixgbe_sfp_type {
	ixgbe_sfp_type_da_cu = 0,
	ixgbe_sfp_type_sr = 1,
	ixgbe_sfp_type_lr = 2,
	ixgbe_sfp_type_da_cu_core0 = 3,
	ixgbe_sfp_type_da_cu_core1 = 4,
	ixgbe_sfp_type_srlr_core0 = 5,
	ixgbe_sfp_type_srlr_core1 = 6,
	ixgbe_sfp_type_da_act_lmt_core0 = 7,
	ixgbe_sfp_type_da_act_lmt_core1 = 8,
	ixgbe_sfp_type_1g_cu_core0 = 9,
	ixgbe_sfp_type_1g_cu_core1 = 10,
	ixgbe_sfp_type_1g_sx_core0 = 11,
	ixgbe_sfp_type_1g_sx_core1 = 12,
	ixgbe_sfp_type_1g_lx_core0 = 13,
	ixgbe_sfp_type_1g_lx_core1 = 14,
	ixgbe_sfp_type_not_present = 0xFFFE,
	ixgbe_sfp_type_unknown = 0xFFFF
};

enum ixgbe_media_type {
	ixgbe_media_type_unknown = 0,
	ixgbe_media_type_fiber,
	ixgbe_media_type_fiber_fixed,
	ixgbe_media_type_fiber_qsfp,
	ixgbe_media_type_copper,
	ixgbe_media_type_backplane,
	ixgbe_media_type_cx4,
	ixgbe_media_type_virtual
};

/* Flow Control Settings */
enum ixgbe_fc_mode {
	ixgbe_fc_none = 0,
	ixgbe_fc_rx_pause,
	ixgbe_fc_tx_pause,
	ixgbe_fc_full,
	ixgbe_fc_default
};

/* Smart Speed Settings */
#define IXGBE_SMARTSPEED_MAX_RETRIES	3
enum ixgbe_smart_speed {
	ixgbe_smart_speed_auto = 0,
	ixgbe_smart_speed_on,
	ixgbe_smart_speed_off
};

/* PCI bus types */
enum ixgbe_bus_type {
	ixgbe_bus_type_unknown = 0,
	ixgbe_bus_type_pci,
	ixgbe_bus_type_pcix,
	ixgbe_bus_type_pci_express,
	ixgbe_bus_type_internal,
	ixgbe_bus_type_reserved
};

/* PCI bus speeds */
enum ixgbe_bus_speed {
	ixgbe_bus_speed_unknown	= 0,
	ixgbe_bus_speed_33	= 33,
	ixgbe_bus_speed_66	= 66,
	ixgbe_bus_speed_100	= 100,
	ixgbe_bus_speed_120	= 120,
	ixgbe_bus_speed_133	= 133,
	ixgbe_bus_speed_2500	= 2500,
	ixgbe_bus_speed_5000	= 5000,
	ixgbe_bus_speed_8000	= 8000,
	ixgbe_bus_speed_reserved
};

/* PCI bus widths */
enum ixgbe_bus_width {
	ixgbe_bus_width_unknown	= 0,
	ixgbe_bus_width_pcie_x1	= 1,
	ixgbe_bus_width_pcie_x2	= 2,
	ixgbe_bus_width_pcie_x4	= 4,
	ixgbe_bus_width_pcie_x8	= 8,
	ixgbe_bus_width_32	= 32,
	ixgbe_bus_width_64	= 64,
	ixgbe_bus_width_reserved
};

struct ixgbe_addr_filter_info {
	uint32_t num_mc_addrs;
	uint32_t rar_used_count;
	uint32_t mta_in_use;
	uint32_t overflow_promisc;
	bool user_set_promisc;
};

/* Bus parameters */
struct ixgbe_bus_info {
	enum ixgbe_bus_speed speed;
	enum ixgbe_bus_width width;
	enum ixgbe_bus_type type;

	uint16_t func;
	uint8_t lan_id;
	uint16_t instance_id;
};

/* Flow control parameters */
struct ixgbe_fc_info {
	uint32_t high_water[IXGBE_DCB_MAX_TRAFFIC_CLASS]; /* Flow Ctrl High-water */
	uint32_t low_water[IXGBE_DCB_MAX_TRAFFIC_CLASS]; /* Flow Ctrl Low-water */
	uint16_t pause_time; /* Flow Control Pause timer */
	bool send_xon; /* Flow control send XON */
	bool strict_ieee; /* Strict IEEE mode */
	bool disable_fc_autoneg; /* Do not autonegotiate FC */
	bool fc_was_autonegged; /* Is current_mode the result of autonegging? */
	enum ixgbe_fc_mode current_mode; /* FC mode in effect */
	enum ixgbe_fc_mode requested_mode; /* FC mode requested by caller */
};

/* Statistics counters collected by the MAC */
struct ixgbe_hw_stats {
	uint64_t crcerrs;
	uint64_t illerrc;
	uint64_t errbc;
	uint64_t mspdc;
	uint64_t mpctotal;
	uint64_t mpc[8];
	uint64_t mlfc;
	uint64_t mrfc;
	uint64_t rlec;
	uint64_t lxontxc;
	uint64_t lxonrxc;
	uint64_t lxofftxc;
	uint64_t lxoffrxc;
	uint64_t pxontxc[8];
	uint64_t pxonrxc[8];
	uint64_t pxofftxc[8];
	uint64_t pxoffrxc[8];
	uint64_t prc64;
	uint64_t prc127;
	uint64_t prc255;
	uint64_t prc511;
	uint64_t prc1023;
	uint64_t prc1522;
	uint64_t gprc;
	uint64_t bprc;
	uint64_t mprc;
	uint64_t gptc;
	uint64_t gorc;
	uint64_t gotc;
	uint64_t rnbc[8];
	uint64_t ruc;
	uint64_t rfc;
	uint64_t roc;
	uint64_t rjc;
	uint64_t mngprc;
	uint64_t mngpdc;
	uint64_t mngptc;
	uint64_t tor;
	uint64_t tpr;
	uint64_t tpt;
	uint64_t ptc64;
	uint64_t ptc127;
	uint64_t ptc255;
	uint64_t ptc511;
	uint64_t ptc1023;
	uint64_t ptc1522;
	uint64_t mptc;
	uint64_t bptc;
	uint64_t xec;
	uint64_t qprc[16];
	uint64_t qptc[16];
	uint64_t qbrc[16];
	uint64_t qbtc[16];
	uint64_t qprdc[16];
	uint64_t pxon2offc[8];
	uint64_t fdirustat_add;
	uint64_t fdirustat_remove;
	uint64_t fdirfstat_fadd;
	uint64_t fdirfstat_fremove;
	uint64_t fdirmatch;
	uint64_t fdirmiss;
	uint64_t fccrc;
	uint64_t fclast;
	uint64_t fcoerpdc;
	uint64_t fcoeprc;
	uint64_t fcoeptc;
	uint64_t fcoedwrc;
	uint64_t fcoedwtc;
	uint64_t fcoe_noddp;
	uint64_t fcoe_noddp_ext_buff;
	uint64_t ldpcec;
	uint64_t pcrc8ec;
	uint64_t b2ospc;
	uint64_t b2ogprc;
	uint64_t o2bgptc;
	uint64_t o2bspc;
};

/* forward declaration */
struct ixgbe_hw;

/* iterator type for walking multicast address lists */
typedef uint8_t* (*ixgbe_mc_addr_itr) (struct ixgbe_hw *hw, uint8_t **mc_addr_ptr,
				  uint32_t *vmdq);

/* Function pointer table */
struct ixgbe_eeprom_operations {
	int32_t (*init_params)(struct ixgbe_hw *);
	int32_t (*read)(struct ixgbe_hw *, uint16_t, uint16_t *);
	int32_t (*write)(struct ixgbe_hw *, uint16_t, uint16_t);
	int32_t (*validate_checksum)(struct ixgbe_hw *, uint16_t *);
	int32_t (*update_checksum)(struct ixgbe_hw *);
	int32_t (*calc_checksum)(struct ixgbe_hw *);
};

struct ixgbe_mac_operations {
	int32_t (*init_hw)(struct ixgbe_hw *);
	int32_t (*reset_hw)(struct ixgbe_hw *);
	int32_t (*start_hw)(struct ixgbe_hw *);
	int32_t (*clear_hw_cntrs)(struct ixgbe_hw *);
	enum ixgbe_media_type (*get_media_type)(struct ixgbe_hw *);
	uint64_t (*get_supported_physical_layer)(struct ixgbe_hw *);
	int32_t (*get_mac_addr)(struct ixgbe_hw *, uint8_t *);
	int32_t (*get_device_caps)(struct ixgbe_hw *, uint16_t *);
	int32_t (*stop_adapter)(struct ixgbe_hw *);
	int32_t (*get_bus_info)(struct ixgbe_hw *);
	int32_t (*negotiate_api_version)(struct ixgbe_hw *, int);
	void (*set_lan_id)(struct ixgbe_hw *);
	int32_t (*read_analog_reg8)(struct ixgbe_hw*, uint32_t, uint8_t*);
	int32_t (*write_analog_reg8)(struct ixgbe_hw*, uint32_t, uint8_t);
	int32_t (*setup_sfp)(struct ixgbe_hw *);
	int32_t (*enable_rx_dma)(struct ixgbe_hw *, uint32_t);
	int32_t (*disable_sec_rx_path)(struct ixgbe_hw *);
	int32_t (*enable_sec_rx_path)(struct ixgbe_hw *);
	int32_t (*acquire_swfw_sync)(struct ixgbe_hw *, uint32_t);
	void (*release_swfw_sync)(struct ixgbe_hw *, uint32_t);
	void (*init_swfw_sync)(struct ixgbe_hw *);
	int32_t (*prot_autoc_read)(struct ixgbe_hw *, bool *, uint32_t *);
	int32_t (*prot_autoc_write)(struct ixgbe_hw *, uint32_t, bool);

	/* Link */
	void (*disable_tx_laser)(struct ixgbe_hw *);
	void (*enable_tx_laser)(struct ixgbe_hw *);
	void (*flap_tx_laser)(struct ixgbe_hw *);
	int32_t (*setup_link)(struct ixgbe_hw *, ixgbe_link_speed, bool);
	int32_t (*setup_mac_link)(struct ixgbe_hw *, ixgbe_link_speed, bool);
	int32_t (*check_link)(struct ixgbe_hw *, ixgbe_link_speed *, bool *, bool);
	int32_t (*get_link_capabilities)(struct ixgbe_hw *, ixgbe_link_speed *,
				     bool *);
	void (*set_rate_select_speed)(struct ixgbe_hw *, ixgbe_link_speed);

	/* LED */
	int32_t (*led_on)(struct ixgbe_hw *, uint32_t);
	int32_t (*led_off)(struct ixgbe_hw *, uint32_t);
	int32_t (*blink_led_start)(struct ixgbe_hw *, uint32_t);
	int32_t (*blink_led_stop)(struct ixgbe_hw *, uint32_t);

	/* RAR, Multicast, VLAN */
	int32_t (*set_rar)(struct ixgbe_hw *, uint32_t, uint8_t *, uint32_t, uint32_t);
	int32_t (*set_uc_addr)(struct ixgbe_hw *, uint32_t, uint8_t *);
	int32_t (*clear_rar)(struct ixgbe_hw *, uint32_t);
	int32_t (*insert_mac_addr)(struct ixgbe_hw *, uint8_t *, uint32_t);
	int32_t (*set_vmdq)(struct ixgbe_hw *, uint32_t, uint32_t);
	int32_t (*clear_vmdq)(struct ixgbe_hw *, uint32_t, uint32_t);
	int32_t (*init_rx_addrs)(struct ixgbe_hw *);
	int32_t (*update_mc_addr_list)(struct ixgbe_hw *, uint8_t *, uint32_t,
				   ixgbe_mc_addr_itr, bool clear);
	int32_t (*update_xcast_mode)(struct ixgbe_hw *, int);
	int32_t (*get_link_state)(struct ixgbe_hw *hw, bool *link_state);
	int32_t (*enable_mc)(struct ixgbe_hw *);
	int32_t (*disable_mc)(struct ixgbe_hw *);
	int32_t (*clear_vfta)(struct ixgbe_hw *);
	int32_t (*set_vfta)(struct ixgbe_hw *, uint32_t, uint32_t, bool, bool);
	int32_t (*set_vlvf)(struct ixgbe_hw *, uint32_t, uint32_t, bool, uint32_t *, uint32_t,
			bool);
	int32_t (*set_rlpml)(struct ixgbe_hw *, uint16_t);
	int32_t (*init_uta_tables)(struct ixgbe_hw *);
	void (*set_mac_anti_spoofing)(struct ixgbe_hw *, bool, int);
	void (*set_vlan_anti_spoofing)(struct ixgbe_hw *, bool, int);

	/* Flow Control */
	int32_t (*fc_enable)(struct ixgbe_hw *);
	int32_t (*setup_fc)(struct ixgbe_hw *);
	void (*fc_autoneg)(struct ixgbe_hw *);

	/* Manageability interface */
	void (*disable_rx)(struct ixgbe_hw *hw);
	void (*enable_rx)(struct ixgbe_hw *hw);
	void (*stop_mac_link_on_d3)(struct ixgbe_hw *);
	void (*set_source_address_pruning)(struct ixgbe_hw *, bool,
					   unsigned int);
	int32_t (*dmac_update_tcs)(struct ixgbe_hw *hw);
	int32_t (*dmac_config_tcs)(struct ixgbe_hw *hw);
	int32_t (*dmac_config)(struct ixgbe_hw *hw);
	int32_t (*setup_eee)(struct ixgbe_hw *hw, bool enable_eee);
	int32_t (*read_iosf_sb_reg)(struct ixgbe_hw *, uint32_t, uint32_t, uint32_t *);
	int32_t (*write_iosf_sb_reg)(struct ixgbe_hw *, uint32_t, uint32_t, uint32_t);
};

struct ixgbe_phy_operations {
	int32_t (*identify)(struct ixgbe_hw *);
	int32_t (*identify_sfp)(struct ixgbe_hw *);
	int32_t (*init)(struct ixgbe_hw *);
	int32_t (*reset)(struct ixgbe_hw *);
	int32_t (*read_reg)(struct ixgbe_hw *, uint32_t, uint32_t, uint16_t *);
	int32_t (*write_reg)(struct ixgbe_hw *, uint32_t, uint32_t, uint16_t);
	int32_t (*read_reg_mdi)(struct ixgbe_hw *, uint32_t, uint32_t, uint16_t *);
	int32_t (*write_reg_mdi)(struct ixgbe_hw *, uint32_t, uint32_t, uint16_t);
	int32_t (*setup_link)(struct ixgbe_hw *);
	int32_t (*setup_internal_link)(struct ixgbe_hw *);
	int32_t (*setup_link_speed)(struct ixgbe_hw *, ixgbe_link_speed, bool);
	int32_t (*check_link)(struct ixgbe_hw *, ixgbe_link_speed *, bool *);
	int32_t (*get_firmware_version)(struct ixgbe_hw *, uint16_t *);
	int32_t (*read_i2c_byte)(struct ixgbe_hw *, uint8_t, uint8_t, uint8_t *);
	int32_t (*write_i2c_byte)(struct ixgbe_hw *, uint8_t, uint8_t, uint8_t);
	int32_t (*read_i2c_eeprom)(struct ixgbe_hw *, uint8_t , uint8_t *);
	int32_t (*write_i2c_eeprom)(struct ixgbe_hw *, uint8_t, uint8_t);
	void (*i2c_bus_clear)(struct ixgbe_hw *);
	/*depreatced*/
	int32_t (*read_i2c_combined)(struct ixgbe_hw *, uint8_t addr, uint16_t reg, uint16_t *val); 
	int32_t (*write_i2c_combined)(struct ixgbe_hw *, uint8_t addr, uint16_t reg, uint16_t val);
	int32_t (*read_i2c_combined_unlocked)(struct ixgbe_hw *, uint8_t addr, uint16_t reg,
					      uint16_t *value);
	int32_t (*write_i2c_combined_unlocked)(struct ixgbe_hw *, uint8_t addr, uint16_t reg,
					       uint16_t value);
	/**/
	int32_t (*check_overtemp)(struct ixgbe_hw *);
	int32_t (*set_phy_power)(struct ixgbe_hw *, bool on);
	int32_t (*enter_lplu)(struct ixgbe_hw *);
	int32_t (*handle_lasi)(struct ixgbe_hw *hw);
	int32_t (*read_i2c_byte_unlocked)(struct ixgbe_hw *, uint8_t offset, uint8_t addr,
				      uint8_t *value);
	int32_t (*write_i2c_byte_unlocked)(struct ixgbe_hw *, uint8_t offset, uint8_t addr,
				       uint8_t value);
};

struct ixgbe_link_operations {
	int32_t (*read_link)(struct ixgbe_hw *, uint8_t addr, uint16_t reg, uint16_t *val);
	int32_t (*read_link_unlocked)(struct ixgbe_hw *, uint8_t addr, uint16_t reg,
				  uint16_t *val);
	int32_t (*write_link)(struct ixgbe_hw *, uint8_t addr, uint16_t reg, uint16_t val);
	int32_t (*write_link_unlocked)(struct ixgbe_hw *, uint8_t addr, uint16_t reg,
				   uint16_t val);
};

struct ixgbe_link_info {
	struct ixgbe_link_operations ops;
	uint8_t addr;
};

struct ixgbe_eeprom_info {
	struct ixgbe_eeprom_operations ops;
	enum ixgbe_eeprom_type type;
	uint32_t semaphore_delay;
	uint16_t word_size;
	uint16_t address_bits;
	uint16_t word_page_size;
	uint16_t ctrl_word_3;
};

#define IXGBE_FLAGS_DOUBLE_RESET_REQUIRED	0x01
struct ixgbe_mac_info {
	struct ixgbe_mac_operations ops;
	enum ixgbe_mac_type type;
	uint8_t addr[IXGBE_ETH_LENGTH_OF_ADDRESS];
	uint8_t perm_addr[IXGBE_ETH_LENGTH_OF_ADDRESS];
#define IXGBE_MAX_MTA			128
	uint32_t mta_shadow[IXGBE_MAX_MTA];
	int32_t mc_filter_type;
	uint32_t mcft_size;
	uint32_t vft_size;
	uint32_t num_rar_entries;
	uint32_t rar_highwater;
	uint32_t rx_pb_size;
	uint32_t max_tx_queues;
	uint32_t max_rx_queues;
	uint32_t orig_autoc;
	bool get_link_status;
	uint32_t orig_autoc2;
	uint16_t max_msix_vectors;
	bool arc_subsystem_valid;
	bool orig_link_settings_stored;
	bool autotry_restart;
	uint8_t flags;
	struct ixgbe_dmac_config dmac_config;
	bool set_lben;
	uint32_t  max_link_up_time;
};

struct ixgbe_phy_info {
	struct ixgbe_phy_operations ops;
	enum ixgbe_phy_type type;
	uint32_t addr;
	uint32_t id;
	enum ixgbe_sfp_type sfp_type;
	bool sfp_setup_needed;
	uint32_t revision;
	enum ixgbe_media_type media_type;
	uint32_t phy_semaphore_mask;
	bool reset_disable;
	ixgbe_autoneg_advertised autoneg_advertised;
	ixgbe_link_speed speeds_supported;
	ixgbe_link_speed eee_speeds_supported;
	ixgbe_link_speed eee_speeds_advertised;
	enum ixgbe_smart_speed smart_speed;
	bool smart_speed_active;
	bool multispeed_fiber;
	bool reset_if_overtemp;
	bool qsfp_shared_i2c_bus;
	uint32_t nw_mng_if_sel;
};

#define IXGBE_VFMAILBOX_SIZE	16 /* 16 32 bit words - 64 bytes */
#define IXGBE_MAX_MULTICAST_ADDRESSES_VF  30

#define IXGBE_VFMAILBOX		0x002FC
#define IXGBE_VFMBMEM		0x00200

/* Define mailbox register bits */
#define IXGBE_VFMAILBOX_REQ	0x00000001 /* Request for PF Ready bit */
#define IXGBE_VFMAILBOX_ACK	0x00000002 /* Ack PF message received */
#define IXGBE_VFMAILBOX_VFU	0x00000004 /* VF owns the mailbox buffer */
#define IXGBE_VFMAILBOX_PFU	0x00000008 /* PF owns the mailbox buffer */
#define IXGBE_VFMAILBOX_PFSTS	0x00000010 /* PF wrote a message in the MB */
#define IXGBE_VFMAILBOX_PFACK	0x00000020 /* PF ack the previous VF msg */
#define IXGBE_VFMAILBOX_RSTI	0x00000040 /* PF has reset indication */
#define IXGBE_VFMAILBOX_RSTD	0x00000080 /* PF has indicated reset done */
#define IXGBE_VFMAILBOX_R2C_BITS	0x000000B0 /* All read to clear bits */

#define IXGBE_PFMAILBOX_STS	0x00000001 /* Initiate message send to VF */
#define IXGBE_PFMAILBOX_ACK	0x00000002 /* Ack message recv'd from VF */
#define IXGBE_PFMAILBOX_VFU	0x00000004 /* VF owns the mailbox buffer */
#define IXGBE_PFMAILBOX_PFU	0x00000008 /* PF owns the mailbox buffer */
#define IXGBE_PFMAILBOX_RVFU	0x00000010 /* Reset VFU - used when VF stuck */

#define IXGBE_PFMBICR_VFREQ_MASK	0x0000FFFF /* bits for VF messages */
#define IXGBE_PFMBICR_VFREQ_VF1		0x00000001 /* bit for VF 1 message */
#define IXGBE_PFMBICR_VFACK_MASK	0xFFFF0000 /* bits for VF acks */
#define IXGBE_PFMBICR_VFACK_VF1		0x00010000 /* bit for VF 1 ack */


/* If it's a IXGBE_VF_* msg then it originates in the VF and is sent to the
 * PF.  The reverse is TRUE if it is IXGBE_PF_*.
 * Message results are the value or'd with 0xF0000000
 */
#define IXGBE_VT_MSGTYPE_SUCCESS	0x80000000 /* Messages or'd with this
						    * have succeeded
						    */
#define IXGBE_VT_MSGTYPE_FAILURE	0x40000000 /* Messages or'd with this
						    * have failed
						    */
#define IXGBE_VT_MSGTYPE_CTS		0x20000000 /* Indicates that VF is still
						    * clear to send requests
						    */
#define IXGBE_VT_MSGINFO_SHIFT	16
/* bits 23:16 are used for extra info for certain messages */
#define IXGBE_VT_MSGINFO_MASK	(0xFF << IXGBE_VT_MSGINFO_SHIFT)

/* definitions to support mailbox API version negotiation */

/*
 * each element denotes a version of the API; existing numbers may not
 * change; any additions must go at the end
 */
enum ixgbe_pfvf_api_rev {
	ixgbe_mbox_api_10,	/* API version 1.0, linux/freebsd VF driver */
	ixgbe_mbox_api_20,	/* API version 2.0, solaris Phase1 VF driver */
	ixgbe_mbox_api_11,	/* API version 1.1, linux/freebsd VF driver */
	ixgbe_mbox_api_12,	/* API version 1.2, linux/freebsd VF driver */
	ixgbe_mbox_api_13,	/* API version 1.3, linux/freebsd VF driver */
	/* API 1.4 is being used in the upstream for IPsec */
	ixgbe_mbox_api_14,	/* API version 1.4, linux/freebsd VF driver */
	ixgbe_mbox_api_15,	/* API version 1.5, linux/freebsd VF driver */
	/* This value should always be last */
	ixgbe_mbox_api_unknown,	/* indicates that API version is not known */
};

/* mailbox API, legacy requests */
#define IXGBE_VF_RESET		0x01 /* VF requests reset */
#define IXGBE_VF_SET_MAC_ADDR	0x02 /* VF requests PF to set MAC addr */
#define IXGBE_VF_SET_MULTICAST	0x03 /* VF requests PF to set MC addr */
#define IXGBE_VF_SET_VLAN	0x04 /* VF requests PF to set VLAN */

/* mailbox API, version 1.0 VF requests */
#define IXGBE_VF_SET_LPE	0x05 /* VF requests PF to set VMOLR.LPE */
#define IXGBE_VF_SET_MACVLAN	0x06 /* VF requests PF for unicast filter */
#define IXGBE_VF_API_NEGOTIATE	0x08 /* negotiate API version */

/* mailbox API, version 1.1 VF requests */
#define IXGBE_VF_GET_QUEUES	0x09 /* get queue configuration */

/* mailbox API, version 1.2 VF requests */
#define IXGBE_VF_GET_RETA      0x0a    /* VF request for RETA */
#define IXGBE_VF_GET_RSS_KEY	0x0b    /* get RSS key */
#define IXGBE_VF_UPDATE_XCAST_MODE	0x0c
#define IXGBE_VF_GET_LINK_STATE 0x10 /* get vf link state */

/* mode choices for IXGBE_VF_UPDATE_XCAST_MODE */
enum ixgbevf_xcast_modes {
	IXGBEVF_XCAST_MODE_NONE = 0,
	IXGBEVF_XCAST_MODE_MULTI,
	IXGBEVF_XCAST_MODE_ALLMULTI,
	IXGBEVF_XCAST_MODE_PROMISC,
};

/* GET_QUEUES return data indices within the mailbox */
#define IXGBE_VF_TX_QUEUES	1	/* number of Tx queues supported */
#define IXGBE_VF_RX_QUEUES	2	/* number of Rx queues supported */
#define IXGBE_VF_TRANS_VLAN	3	/* Indication of port vlan */
#define IXGBE_VF_DEF_QUEUE	4	/* Default queue offset */

/* length of permanent address message returned from PF */
#define IXGBE_VF_PERMADDR_MSG_LEN	4
/* word in permanent address message with the current multicast type */
#define IXGBE_VF_MC_TYPE_WORD		3

#define IXGBE_PF_CONTROL_MSG		0x0100 /* PF control message */

/* mailbox API, version 2.0 VF requests */
#define IXGBE_VF_API_NEGOTIATE		0x08 /* negotiate API version */
#define IXGBE_VF_GET_QUEUES		0x09 /* get queue configuration */
#define IXGBE_VF_ENABLE_MACADDR		0x0A /* enable MAC address */
#define IXGBE_VF_DISABLE_MACADDR	0x0B /* disable MAC address */
#define IXGBE_VF_GET_MACADDRS		0x0C /* get all configured MAC addrs */
#define IXGBE_VF_SET_MCAST_PROMISC	0x0D /* enable multicast promiscuous */
#define IXGBE_VF_GET_MTU		0x0E /* get bounds on MTU */
#define IXGBE_VF_SET_MTU		0x0F /* set a specific MTU */

/* mailbox API, version 2.0 PF requests */
#define IXGBE_PF_TRANSPARENT_VLAN	0x0101 /* enable transparent vlan */

#define IXGBE_VF_MBX_INIT_TIMEOUT	2000 /* number of retries on mailbox */
#define IXGBE_VF_MBX_INIT_DELAY		500  /* microseconds between retries */

#define IXGBE_VF_IRQ_CLEAR_MASK	7
#define IXGBE_VF_MAX_TX_QUEUES	8
#define IXGBE_VF_MAX_RX_QUEUES	8
/* DCB define */
#define IXGBE_VF_MAX_TRAFFIC_CLASS	8

#define IXGBE_VFCTRL		0x00000
#define IXGBE_VFSTATUS		0x00008
#define IXGBE_VFLINKS		0x00010
#define IXGBE_VFFRTIMER		0x00048
#define IXGBE_VFRXMEMWRAP	0x03190
#define IXGBE_VTEICR		0x00100
#define IXGBE_VTEICS		0x00104
#define IXGBE_VTEIMS		0x00108
#define IXGBE_VTEIMC		0x0010C
#define IXGBE_VTEIAC		0x00110
#define IXGBE_VTEIAM		0x00114
#define IXGBE_VTEITR(x)		(0x00820 + (4 * (x)))
#define IXGBE_VTIVAR(x)		(0x00120 + (4 * (x)))
#define IXGBE_VTIVAR_MISC	0x00140
#define IXGBE_VTRSCINT(x)	(0x00180 + (4 * (x)))
/* define IXGBE_VFPBACL  still says TBD in EAS */
#define IXGBE_VFRDBAL(x)	(0x01000 + (0x40 * (x)))
#define IXGBE_VFRDBAH(x)	(0x01004 + (0x40 * (x)))
#define IXGBE_VFRDLEN(x)	(0x01008 + (0x40 * (x)))
#define IXGBE_VFRDH(x)		(0x01010 + (0x40 * (x)))
#define IXGBE_VFRDT(x)		(0x01018 + (0x40 * (x)))
#define IXGBE_VFRXDCTL(x)	(0x01028 + (0x40 * (x)))
#define IXGBE_VFSRRCTL(x)	(0x01014 + (0x40 * (x)))
#define IXGBE_VFRSCCTL(x)	(0x0102C + (0x40 * (x)))
#define IXGBE_VFPSRTYPE		0x00300
#define IXGBE_VFTDBAL(x)	(0x02000 + (0x40 * (x)))
#define IXGBE_VFTDBAH(x)	(0x02004 + (0x40 * (x)))
#define IXGBE_VFTDLEN(x)	(0x02008 + (0x40 * (x)))
#define IXGBE_VFTDH(x)		(0x02010 + (0x40 * (x)))
#define IXGBE_VFTDT(x)		(0x02018 + (0x40 * (x)))
#define IXGBE_VFTXDCTL(x)	(0x02028 + (0x40 * (x)))
#define IXGBE_VFTDWBAL(x)	(0x02038 + (0x40 * (x)))
#define IXGBE_VFTDWBAH(x)	(0x0203C + (0x40 * (x)))
#define IXGBE_VFDCA_RXCTRL(x)	(0x0100C + (0x40 * (x)))
#define IXGBE_VFDCA_TXCTRL(x)	(0x0200c + (0x40 * (x)))
#define IXGBE_VFGPRC		0x0101C
#define IXGBE_VFGPTC		0x0201C
#define IXGBE_VFGORC_LSB	0x01020
#define IXGBE_VFGORC_MSB	0x01024
#define IXGBE_VFGOTC_LSB	0x02020
#define IXGBE_VFGOTC_MSB	0x02024
#define IXGBE_VFMPRC		0x01034
#define IXGBE_VFMRQC		0x3000
#define IXGBE_VFRSSRK(x)	(0x3100 + ((x) * 4))
#define IXGBE_VFRETA(x)		(0x3200 + ((x) * 4))

struct ixgbe_mbx_operations {
	void (*init_params)(struct ixgbe_hw *hw);
	void (*release)(struct ixgbe_hw *, uint16_t);
	int32_t  (*read)(struct ixgbe_hw *, uint32_t *, uint16_t,  uint16_t);
	int32_t  (*write)(struct ixgbe_hw *, uint32_t *, uint16_t, uint16_t);
	int32_t  (*read_posted)(struct ixgbe_hw *, uint32_t *, uint16_t,  uint16_t);
	int32_t  (*write_posted)(struct ixgbe_hw *, uint32_t *, uint16_t, uint16_t);
	int32_t  (*check_for_msg)(struct ixgbe_hw *, uint16_t);
	int32_t  (*check_for_ack)(struct ixgbe_hw *, uint16_t);
	int32_t  (*check_for_rst)(struct ixgbe_hw *, uint16_t);
	int32_t  (*clear)(struct ixgbe_hw *, uint16_t);
};

struct ixgbe_mbx_stats {
	uint32_t msgs_tx;
	uint32_t msgs_rx;

	uint32_t acks;
	uint32_t reqs;
	uint32_t rsts;
};

struct ixgbe_mbx_info {
	struct ixgbe_mbx_operations ops;
	struct ixgbe_mbx_stats stats;
	uint32_t timeout;
	uint32_t usec_delay;
	uint32_t vf_mailbox;
	uint16_t size;
};

struct ixgbe_hw {
	uint8_t *hw_addr;
	void *back;
	struct ixgbe_mac_info mac;
	struct ixgbe_addr_filter_info addr_ctrl;
	struct ixgbe_fc_info fc;
	struct ixgbe_phy_info phy;
	struct ixgbe_link_info link;
	struct ixgbe_eeprom_info eeprom;
	struct ixgbe_bus_info bus;
	struct ixgbe_mbx_info mbx;
	const uint32_t *mvals;
	uint16_t device_id;
	uint16_t vendor_id;
	uint16_t subsystem_device_id;
	uint16_t subsystem_vendor_id;
	uint8_t revision_id;
	bool adapter_stopped;
	int api_version;
	bool force_full_reset;
	bool allow_unsupported_sfp;
	bool wol_enabled;
	bool need_crosstalk_fix;
};

#define ixgbe_call_func(hw, func, params, error) \
		(func != NULL) ? func params : error


/* Error Codes */
#define IXGBE_SUCCESS				0
#define IXGBE_ERR_EEPROM			-1
#define IXGBE_ERR_EEPROM_CHECKSUM		-2
#define IXGBE_ERR_PHY				-3
#define IXGBE_ERR_CONFIG			-4
#define IXGBE_ERR_PARAM				-5
#define IXGBE_ERR_MAC_TYPE			-6
#define IXGBE_ERR_UNKNOWN_PHY			-7
#define IXGBE_ERR_LINK_SETUP			-8
#define IXGBE_ERR_ADAPTER_STOPPED		-9
#define IXGBE_ERR_INVALID_MAC_ADDR		-10
#define IXGBE_ERR_DEVICE_NOT_SUPPORTED		-11
#define IXGBE_ERR_MASTER_REQUESTS_PENDING	-12
#define IXGBE_ERR_INVALID_LINK_SETTINGS		-13
#define IXGBE_ERR_AUTONEG_NOT_COMPLETE		-14
#define IXGBE_ERR_RESET_FAILED			-15
#define IXGBE_ERR_SWFW_SYNC			-16
#define IXGBE_ERR_PHY_ADDR_INVALID		-17
#define IXGBE_ERR_I2C				-18
#define IXGBE_ERR_SFP_NOT_SUPPORTED		-19
#define IXGBE_ERR_SFP_NOT_PRESENT		-20
#define IXGBE_ERR_SFP_NO_INIT_SEQ_PRESENT	-21
#define IXGBE_ERR_NO_SAN_ADDR_PTR		-22
#define IXGBE_ERR_FDIR_REINIT_FAILED		-23
#define IXGBE_ERR_EEPROM_VERSION		-24
#define IXGBE_ERR_NO_SPACE			-25
#define IXGBE_ERR_OVERTEMP			-26
#define IXGBE_ERR_FC_NOT_NEGOTIATED		-27
#define IXGBE_ERR_FC_NOT_SUPPORTED		-28
#define IXGBE_ERR_SFP_SETUP_NOT_COMPLETE	-30
#define IXGBE_ERR_PBA_SECTION			-31
#define IXGBE_ERR_INVALID_ARGUMENT		-32
#define IXGBE_ERR_HOST_INTERFACE_COMMAND	-33
#define IXGBE_ERR_OUT_OF_MEM			-34
#define IXGBE_BYPASS_FW_WRITE_FAILURE		-35
#define IXGBE_ERR_FEATURE_NOT_SUPPORTED		-36
#define IXGBE_ERR_EEPROM_PROTECTED_REGION	-37
#define IXGBE_ERR_FDIR_CMD_INCOMPLETE		-38
#define IXGBE_ERR_FW_RESP_INVALID		-39
#define IXGBE_ERR_TOKEN_RETRY			-40
#define IXGBE_ERR_MBX				-41
#define IXGBE_ERR_MBX_NOMSG			-42
#define IXGBE_ERR_TIMEOUT			-43

#define IXGBE_NOT_IMPLEMENTED			0x7FFFFFFF


#define BYPASS_PAGE_CTL0	0x00000000
#define BYPASS_PAGE_CTL1	0x40000000
#define BYPASS_PAGE_CTL2	0x80000000
#define BYPASS_PAGE_M		0xc0000000
#define BYPASS_WE		0x20000000

#define BYPASS_AUTO	0x0
#define BYPASS_NOP	0x0
#define BYPASS_NORM	0x1
#define BYPASS_BYPASS	0x2
#define BYPASS_ISOLATE	0x3

#define BYPASS_EVENT_MAIN_ON	0x1
#define BYPASS_EVENT_AUX_ON	0x2
#define BYPASS_EVENT_MAIN_OFF	0x3
#define BYPASS_EVENT_AUX_OFF	0x4
#define BYPASS_EVENT_WDT_TO	0x5
#define BYPASS_EVENT_USR	0x6

#define BYPASS_MODE_OFF_M	0x00000003
#define BYPASS_STATUS_OFF_M	0x0000000c
#define BYPASS_AUX_ON_M		0x00000030
#define BYPASS_MAIN_ON_M	0x000000c0
#define BYPASS_MAIN_OFF_M	0x00000300
#define BYPASS_AUX_OFF_M	0x00000c00
#define BYPASS_WDTIMEOUT_M	0x00003000
#define BYPASS_WDT_ENABLE_M	0x00004000
#define BYPASS_WDT_VALUE_M	0x00070000

#define BYPASS_MODE_OFF_SHIFT	0
#define BYPASS_STATUS_OFF_SHIFT	2
#define BYPASS_AUX_ON_SHIFT	4
#define BYPASS_MAIN_ON_SHIFT	6
#define BYPASS_MAIN_OFF_SHIFT	8
#define BYPASS_AUX_OFF_SHIFT	10
#define BYPASS_WDTIMEOUT_SHIFT	12
#define BYPASS_WDT_ENABLE_SHIFT	14
#define BYPASS_WDT_TIME_SHIFT	16

#define BYPASS_WDT_1	0x0
#define BYPASS_WDT_1_5	0x1
#define BYPASS_WDT_2	0x2
#define BYPASS_WDT_3	0x3
#define BYPASS_WDT_4	0x4
#define BYPASS_WDT_8	0x5
#define BYPASS_WDT_16	0x6
#define BYPASS_WDT_32	0x7
#define BYPASS_WDT_OFF	0xffff

#define BYPASS_CTL1_TIME_M	0x01ffffff
#define BYPASS_CTL1_VALID_M	0x02000000
#define BYPASS_CTL1_OFFTRST_M	0x04000000
#define BYPASS_CTL1_WDT_PET_M	0x08000000

#define BYPASS_CTL1_VALID	0x02000000
#define BYPASS_CTL1_OFFTRST	0x04000000
#define BYPASS_CTL1_WDT_PET	0x08000000

#define BYPASS_CTL2_DATA_M	0x000000ff
#define BYPASS_CTL2_OFFSET_M	0x0000ff00
#define BYPASS_CTL2_RW_M	0x00010000
#define BYPASS_CTL2_HEAD_M	0x0ff00000

#define BYPASS_CTL2_OFFSET_SHIFT	8
#define BYPASS_CTL2_HEAD_SHIFT		20

#define BYPASS_CTL2_RW		0x00010000

struct ixgbe_bypass_eeprom {
	uint32_t logs;
	uint32_t clear_off;
	uint8_t actions;
};

#define BYPASS_MAX_LOGS		43
#define BYPASS_LOG_SIZE		5
#define BYPASS_LOG_LINE_SIZE	37

#define BYPASS_EEPROM_VER_ADD	0x02

#define BYPASS_LOG_TIME_M	0x01ffffff
#define BYPASS_LOG_TIME_VALID_M	0x02000000
#define BYPASS_LOG_HEAD_M	0x04000000
#define BYPASS_LOG_CLEAR_M	0x08000000
#define BYPASS_LOG_EVENT_M	0xf0000000
#define BYPASS_LOG_ACTION_M	0x03

#define BYPASS_LOG_EVENT_SHIFT	28
#define BYPASS_LOG_CLEAR_SHIFT	24 /* bit offset */

#define IXGBE_FUSES0_GROUP(_i)		(0x11158 + ((_i) * 4))
#define IXGBE_FUSES0_300MHZ		(1 << 5)
#define IXGBE_FUSES0_REV_MASK		(3 << 6)

#define IXGBE_KRM_PORT_CAR_GEN_CTRL(P)	((P) ? 0x8010 : 0x4010)
#define IXGBE_KRM_LINK_S1(P)		((P) ? 0x8200 : 0x4200)
#define IXGBE_KRM_LINK_CTRL_1(P)	((P) ? 0x820C : 0x420C)
#define IXGBE_KRM_AN_CNTL_1(P)		((P) ? 0x822C : 0x422C)
#define IXGBE_KRM_AN_CNTL_4(P)		((P) ? 0x8238 : 0x4238)
#define IXGBE_KRM_AN_CNTL_8(P)		((P) ? 0x8248 : 0x4248)
#define IXGBE_KRM_PCS_KX_AN(P)		((P) ? 0x9918 : 0x5918)
#define IXGBE_KRM_PCS_KX_AN_LP(P)	((P) ? 0x991C : 0x591C)
#define IXGBE_KRM_SGMII_CTRL(P)		((P) ? 0x82A0 : 0x42A0)
#define IXGBE_KRM_LP_BASE_PAGE_HIGH(P)	((P) ? 0x836C : 0x436C)
#define IXGBE_KRM_DSP_TXFFE_STATE_4(P)	((P) ? 0x8634 : 0x4634)
#define IXGBE_KRM_DSP_TXFFE_STATE_5(P)	((P) ? 0x8638 : 0x4638)
#define IXGBE_KRM_RX_TRN_LINKUP_CTRL(P)	((P) ? 0x8B00 : 0x4B00)
#define IXGBE_KRM_PMD_DFX_BURNIN(P)	((P) ? 0x8E00 : 0x4E00)
#define IXGBE_KRM_PMD_FLX_MASK_ST20(P)	((P) ? 0x9054 : 0x5054)
#define IXGBE_KRM_TX_COEFF_CTRL_1(P)	((P) ? 0x9520 : 0x5520)
#define IXGBE_KRM_RX_ANA_CTL(P)		((P) ? 0x9A00 : 0x5A00)

#define IXGBE_KRM_PMD_FLX_MASK_ST20_SFI_10G_DA		~(0x3 << 20)
#define IXGBE_KRM_PMD_FLX_MASK_ST20_SFI_10G_SR		(1u << 20)
#define IXGBE_KRM_PMD_FLX_MASK_ST20_SFI_10G_LR		(0x2 << 20)
#define IXGBE_KRM_PMD_FLX_MASK_ST20_SGMII_EN		(1u << 25)
#define IXGBE_KRM_PMD_FLX_MASK_ST20_AN37_EN		(1u << 26)
#define IXGBE_KRM_PMD_FLX_MASK_ST20_AN_EN		(1u << 27)
#define IXGBE_KRM_PMD_FLX_MASK_ST20_SPEED_10M		~(0x7 << 28)
#define IXGBE_KRM_PMD_FLX_MASK_ST20_SPEED_100M		(1u << 28)
#define IXGBE_KRM_PMD_FLX_MASK_ST20_SPEED_1G		(0x2 << 28)
#define IXGBE_KRM_PMD_FLX_MASK_ST20_SPEED_10G		(0x3 << 28)
#define IXGBE_KRM_PMD_FLX_MASK_ST20_SPEED_AN		(0x4 << 28)
#define IXGBE_KRM_PMD_FLX_MASK_ST20_SPEED_2_5G		(0x7 << 28)
#define IXGBE_KRM_PMD_FLX_MASK_ST20_SPEED_MASK		(0x7 << 28)
#define IXGBE_KRM_PMD_FLX_MASK_ST20_FW_AN_RESTART	(1u << 31)

#define IXGBE_KRM_PORT_CAR_GEN_CTRL_NELB_32B		(1 << 9)
#define IXGBE_KRM_PORT_CAR_GEN_CTRL_NELB_KRPCS		(1 << 11)

#define IXGBE_KRM_LINK_CTRL_1_TETH_FORCE_SPEED_MASK	(0x7 << 8)
#define IXGBE_KRM_LINK_CTRL_1_TETH_FORCE_SPEED_1G	(2 << 8)
#define IXGBE_KRM_LINK_CTRL_1_TETH_FORCE_SPEED_10G	(4 << 8)
#define IXGBE_KRM_LINK_CTRL_1_TETH_AN_SGMII_EN		(1 << 12)
#define IXGBE_KRM_LINK_CTRL_1_TETH_AN_CLAUSE_37_EN	(1 << 13)
#define IXGBE_KRM_LINK_CTRL_1_TETH_AN_FEC_REQ		(1 << 14)
#define IXGBE_KRM_LINK_CTRL_1_TETH_AN_CAP_FEC		(1 << 15)
#define IXGBE_KRM_LINK_CTRL_1_TETH_AN_CAP_KX		(1 << 16)
#define IXGBE_KRM_LINK_CTRL_1_TETH_AN_CAP_KR		(1 << 18)
#define IXGBE_KRM_LINK_CTRL_1_TETH_EEE_CAP_KX		(1 << 24)
#define IXGBE_KRM_LINK_CTRL_1_TETH_EEE_CAP_KR		(1 << 26)
#define IXGBE_KRM_LINK_S1_MAC_AN_COMPLETE		(1 << 28)
#define IXGBE_KRM_LINK_CTRL_1_TETH_AN_ENABLE		(1 << 29)
#define IXGBE_KRM_LINK_CTRL_1_TETH_AN_RESTART		(1U << 31)

#define IXGBE_KRM_AN_CNTL_1_SYM_PAUSE			(1 << 28)
#define IXGBE_KRM_AN_CNTL_1_ASM_PAUSE			(1 << 29)
#define IXGBE_KRM_PCS_KX_AN_SYM_PAUSE			(1 << 1)
#define IXGBE_KRM_PCS_KX_AN_ASM_PAUSE			(1 << 2)
#define IXGBE_KRM_PCS_KX_AN_LP_SYM_PAUSE		(1 << 2)
#define IXGBE_KRM_PCS_KX_AN_LP_ASM_PAUSE		(1 << 3)
#define IXGBE_KRM_AN_CNTL_4_ECSR_AN37_OVER_73		(1 << 29)
#define IXGBE_KRM_AN_CNTL_8_LINEAR			(1 << 0)
#define IXGBE_KRM_AN_CNTL_8_LIMITING			(1 << 1)

#define IXGBE_KRM_LP_BASE_PAGE_HIGH_SYM_PAUSE		(1 << 10)
#define IXGBE_KRM_LP_BASE_PAGE_HIGH_ASM_PAUSE		(1 << 11)

#define IXGBE_KRM_SGMII_CTRL_MAC_TAR_FORCE_100_D	(1 << 12)
#define IXGBE_KRM_SGMII_CTRL_MAC_TAR_FORCE_10_D		(1 << 19)

#define IXGBE_KRM_DSP_TXFFE_STATE_C0_EN			(1 << 6)
#define IXGBE_KRM_DSP_TXFFE_STATE_CP1_CN1_EN		(1 << 15)
#define IXGBE_KRM_DSP_TXFFE_STATE_CO_ADAPT_EN		(1 << 16)

#define IXGBE_KRM_RX_TRN_LINKUP_CTRL_CONV_WO_PROTOCOL	(1 << 4)
#define IXGBE_KRM_RX_TRN_LINKUP_CTRL_PROTOCOL_BYPASS	(1 << 2)

#define IXGBE_KRM_PMD_DFX_BURNIN_TX_RX_KR_LB_MASK	(0x3 << 16)

#define IXGBE_KRM_TX_COEFF_CTRL_1_CMINUS1_OVRRD_EN	(1 << 1)
#define IXGBE_KRM_TX_COEFF_CTRL_1_CPLUS1_OVRRD_EN	(1 << 2)
#define IXGBE_KRM_TX_COEFF_CTRL_1_CZERO_EN		(1 << 3)
#define IXGBE_KRM_TX_COEFF_CTRL_1_OVRRD_EN		(1U << 31)

#define IXGBE_SB_IOSF_INDIRECT_CTRL	0x00011144
#define IXGBE_SB_IOSF_INDIRECT_DATA	0x00011148

#define IXGBE_SB_IOSF_CTRL_ADDR_SHIFT		0
#define IXGBE_SB_IOSF_CTRL_ADDR_MASK		0xFF
#define IXGBE_SB_IOSF_CTRL_RESP_STAT_SHIFT	18
#define IXGBE_SB_IOSF_CTRL_RESP_STAT_MASK	\
				(0x3 << IXGBE_SB_IOSF_CTRL_RESP_STAT_SHIFT)
#define IXGBE_SB_IOSF_CTRL_CMPL_ERR_SHIFT	20
#define IXGBE_SB_IOSF_CTRL_CMPL_ERR_MASK	\
				(0xFF << IXGBE_SB_IOSF_CTRL_CMPL_ERR_SHIFT)
#define IXGBE_SB_IOSF_CTRL_TARGET_SELECT_SHIFT	28
#define IXGBE_SB_IOSF_CTRL_TARGET_SELECT_MASK	0x7
#define IXGBE_SB_IOSF_CTRL_BUSY_SHIFT		31
#define IXGBE_SB_IOSF_CTRL_BUSY		(1 << IXGBE_SB_IOSF_CTRL_BUSY_SHIFT)
#define IXGBE_SB_IOSF_TARGET_KR_PHY	0

#define IXGBE_NW_MNG_IF_SEL		0x00011178
#define IXGBE_NW_MNG_IF_SEL_MDIO_ACT	(1u << 1)
#define IXGBE_NW_MNG_IF_SEL_MDIO_IF_MODE	(1u << 2)
#define IXGBE_NW_MNG_IF_SEL_EN_SHARED_MDIO	(1u << 13)
#define IXGBE_NW_MNG_IF_SEL_PHY_SPEED_10M	(1u << 17)
#define IXGBE_NW_MNG_IF_SEL_PHY_SPEED_100M	(1u << 18)
#define IXGBE_NW_MNG_IF_SEL_PHY_SPEED_1G	(1u << 19)
#define IXGBE_NW_MNG_IF_SEL_PHY_SPEED_2_5G	(1u << 20)
#define IXGBE_NW_MNG_IF_SEL_PHY_SPEED_10G	(1u << 21)
#define IXGBE_NW_MNG_IF_SEL_SGMII_ENABLE	(1u << 25)
#define IXGBE_NW_MNG_IF_SEL_INT_PHY_MODE (1 << 24) /* X552 reg field only */
#define IXGBE_NW_MNG_IF_SEL_MDIO_PHY_ADD_SHIFT 3
#define IXGBE_NW_MNG_IF_SEL_MDIO_PHY_ADD	\
				(0x1F << IXGBE_NW_MNG_IF_SEL_MDIO_PHY_ADD_SHIFT)

/* PHY */
#define IXGBE_I2C_EEPROM_DEV_ADDR	0xA0
#define IXGBE_I2C_EEPROM_DEV_ADDR2	0xA2
#define IXGBE_I2C_EEPROM_BANK_LEN	0xFF

/* EEPROM byte offsets */
#define IXGBE_SFF_IDENTIFIER		0x0
#define IXGBE_SFF_IDENTIFIER_SFP	0x3
#define IXGBE_SFF_VENDOR_OUI_BYTE0	0x25
#define IXGBE_SFF_VENDOR_OUI_BYTE1	0x26
#define IXGBE_SFF_VENDOR_OUI_BYTE2	0x27
#define IXGBE_SFF_1GBE_COMP_CODES	0x6
#define IXGBE_SFF_10GBE_COMP_CODES	0x3
#define IXGBE_SFF_CABLE_TECHNOLOGY	0x8
#define IXGBE_SFF_CABLE_SPEC_COMP	0x3C
#define IXGBE_SFF_SFF_8472_SWAP		0x5C
#define IXGBE_SFF_SFF_8472_COMP		0x5E
#define IXGBE_SFF_SFF_8472_OSCB		0x6E
#define IXGBE_SFF_SFF_8472_ESCB		0x76
#define IXGBE_SFF_IDENTIFIER_QSFP_PLUS	0xD
#define IXGBE_SFF_QSFP_VENDOR_OUI_BYTE0	0xA5
#define IXGBE_SFF_QSFP_VENDOR_OUI_BYTE1	0xA6
#define IXGBE_SFF_QSFP_VENDOR_OUI_BYTE2	0xA7
#define IXGBE_SFF_QSFP_CONNECTOR	0x82
#define IXGBE_SFF_QSFP_10GBE_COMP	0x83
#define IXGBE_SFF_QSFP_1GBE_COMP	0x86
#define IXGBE_SFF_QSFP_CABLE_LENGTH	0x92
#define IXGBE_SFF_QSFP_DEVICE_TECH	0x93

/* Bitmasks */
#define IXGBE_SFF_DA_PASSIVE_CABLE	0x4
#define IXGBE_SFF_DA_ACTIVE_CABLE	0x8
#define IXGBE_SFF_DA_SPEC_ACTIVE_LIMITING	0x4
#define IXGBE_SFF_1GBASESX_CAPABLE	0x1
#define IXGBE_SFF_1GBASELX_CAPABLE	0x2
#define IXGBE_SFF_1GBASET_CAPABLE	0x8
#define IXGBE_SFF_10GBASESR_CAPABLE	0x10
#define IXGBE_SFF_10GBASELR_CAPABLE	0x20
#define IXGBE_SFF_DA_BAD_HP_CABLE	0x80
#define IXGBE_SFF_SOFT_RS_SELECT_MASK	0x8
#define IXGBE_SFF_SOFT_RS_SELECT_10G	0x8
#define IXGBE_SFF_SOFT_RS_SELECT_1G	0x0
#define IXGBE_SFF_ADDRESSING_MODE	0x4
#define IXGBE_SFF_QSFP_DA_ACTIVE_CABLE	0x1
#define IXGBE_SFF_QSFP_DA_PASSIVE_CABLE	0x8
#define IXGBE_SFF_QSFP_CONNECTOR_NOT_SEPARABLE	0x23
#define IXGBE_SFF_QSFP_TRANSMITER_850NM_VCSEL	0x0
#define IXGBE_I2C_EEPROM_READ_MASK	0x100
#define IXGBE_I2C_EEPROM_STATUS_MASK	0x3
#define IXGBE_I2C_EEPROM_STATUS_NO_OPERATION	0x0
#define IXGBE_I2C_EEPROM_STATUS_PASS	0x1
#define IXGBE_I2C_EEPROM_STATUS_FAIL	0x2
#define IXGBE_I2C_EEPROM_STATUS_IN_PROGRESS	0x3

#define IXGBE_CS4227			0xBE	/* CS4227 address */
#define IXGBE_CS4227_GLOBAL_ID_LSB	0
#define IXGBE_CS4227_GLOBAL_ID_MSB	1
#define IXGBE_CS4227_SCRATCH		2
#define IXGBE_CS4227_GLOBAL_ID_VALUE	0x03E5
#define IXGBE_CS4227_EFUSE_PDF_SKU	0x19F
#define IXGBE_CS4223_SKU_ID		0x0010	/* Quad port */
#define IXGBE_CS4227_SKU_ID		0x0014	/* Dual port */
#define IXGBE_CS4227_RESET_PENDING	0x1357
#define IXGBE_CS4227_RESET_COMPLETE	0x5AA5
#define IXGBE_CS4227_RETRIES		15
#define IXGBE_CS4227_EFUSE_STATUS	0x0181
#define IXGBE_CS4227_LINE_SPARE22_MSB	0x12AD	/* Reg to program speed */
#define IXGBE_CS4227_LINE_SPARE24_LSB	0x12B0	/* Reg to program EDC */
#define IXGBE_CS4227_HOST_SPARE22_MSB	0x1AAD	/* Reg to program speed */
#define IXGBE_CS4227_HOST_SPARE24_LSB	0x1AB0	/* Reg to program EDC */
#define IXGBE_CS4227_EEPROM_STATUS	0x5001
#define IXGBE_CS4227_EEPROM_LOAD_OK	0x0001
#define IXGBE_CS4227_SPEED_1G		0x8000
#define IXGBE_CS4227_SPEED_10G		0
#define IXGBE_CS4227_EDC_MODE_CX1	0x0002
#define IXGBE_CS4227_EDC_MODE_SR	0x0004
#define IXGBE_CS4227_EDC_MODE_DIAG	0x0008
#define IXGBE_CS4227_RESET_HOLD		500	/* microseconds */
#define IXGBE_CS4227_RESET_DELAY	450	/* milliseconds */
#define IXGBE_CS4227_CHECK_DELAY	30	/* milliseconds */
#define IXGBE_PE			0xE0	/* Port expander address */
#define IXGBE_PE_OUTPUT			1	/* Output register offset */
#define IXGBE_PE_CONFIG			3	/* Config register offset */
#define IXGBE_PE_BIT1			(1 << 1)

/* Flow control defines */
#define IXGBE_TAF_SYM_PAUSE		0x400
#define IXGBE_TAF_ASM_PAUSE		0x800

/* Bit-shift macros */
#define IXGBE_SFF_VENDOR_OUI_BYTE0_SHIFT	24
#define IXGBE_SFF_VENDOR_OUI_BYTE1_SHIFT	16
#define IXGBE_SFF_VENDOR_OUI_BYTE2_SHIFT	8

/* Vendor OUIs: format of OUI is 0x[byte0][byte1][byte2][00] */
#define IXGBE_SFF_VENDOR_OUI_TYCO	0x00407600
#define IXGBE_SFF_VENDOR_OUI_FTL	0x00906500
#define IXGBE_SFF_VENDOR_OUI_AVAGO	0x00176A00
#define IXGBE_SFF_VENDOR_OUI_INTEL	0x001B2100

/* I2C SDA and SCL timing parameters for standard mode */
#define IXGBE_I2C_T_HD_STA	4
#define IXGBE_I2C_T_LOW		5
#define IXGBE_I2C_T_HIGH	4
#define IXGBE_I2C_T_SU_STA	5
#define IXGBE_I2C_T_HD_DATA	5
#define IXGBE_I2C_T_SU_DATA	1
#define IXGBE_I2C_T_RISE	1
#define IXGBE_I2C_T_FALL	1
#define IXGBE_I2C_T_SU_STO	4
#define IXGBE_I2C_T_BUF		5

#ifndef IXGBE_SFP_DETECT_RETRIES
#define IXGBE_SFP_DETECT_RETRIES	10

#endif /* IXGBE_SFP_DETECT_RETRIES */
#define IXGBE_TN_LASI_STATUS_REG	0x9005
#define IXGBE_TN_LASI_STATUS_TEMP_ALARM	0x0008

/* SFP+ SFF-8472 Compliance */
#define IXGBE_SFF_SFF_8472_UNSUP	0x00

/* end PHY */

#endif /* _IXGBE_TYPE_H_ */
