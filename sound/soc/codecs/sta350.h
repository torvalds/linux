/*
 * Codec driver for ST STA350 2.1-channel high-efficiency digital audio system
 *
 * Copyright: 2011 Raumfeld GmbH
 * Author: Sven Brandau <info@brandau.biz>
 *
 * based on code from:
 *      Raumfeld GmbH
 *        Johannes Stezenbach <js@sig21.net>
 *	Wolfson Microelectronics PLC.
 *	  Mark Brown <broonie@opensource.wolfsonmicro.com>
 *
 * This program is free software; you can redistribute  it and/or modify it
 * under  the terms of  the GNU General  Public License as published by the
 * Free Software Foundation;  either version 2 of the  License, or (at your
 * option) any later version.
 */
#ifndef _ASOC_STA_350_H
#define _ASOC_STA_350_H

/* STA50 register addresses */

#define STA350_REGISTER_COUNT	0x4D
#define STA350_COEF_COUNT 62

#define STA350_CONFA	0x00
#define STA350_CONFB    0x01
#define STA350_CONFC    0x02
#define STA350_CONFD    0x03
#define STA350_CONFE    0x04
#define STA350_CONFF    0x05
#define STA350_MMUTE    0x06
#define STA350_MVOL     0x07
#define STA350_C1VOL    0x08
#define STA350_C2VOL    0x09
#define STA350_C3VOL    0x0a
#define STA350_AUTO1    0x0b
#define STA350_AUTO2    0x0c
#define STA350_AUTO3    0x0d
#define STA350_C1CFG    0x0e
#define STA350_C2CFG    0x0f
#define STA350_C3CFG    0x10
#define STA350_TONE     0x11
#define STA350_L1AR     0x12
#define STA350_L1ATRT   0x13
#define STA350_L2AR     0x14
#define STA350_L2ATRT   0x15
#define STA350_CFADDR2  0x16
#define STA350_B1CF1    0x17
#define STA350_B1CF2    0x18
#define STA350_B1CF3    0x19
#define STA350_B2CF1    0x1a
#define STA350_B2CF2    0x1b
#define STA350_B2CF3    0x1c
#define STA350_A1CF1    0x1d
#define STA350_A1CF2    0x1e
#define STA350_A1CF3    0x1f
#define STA350_A2CF1    0x20
#define STA350_A2CF2    0x21
#define STA350_A2CF3    0x22
#define STA350_B0CF1    0x23
#define STA350_B0CF2    0x24
#define STA350_B0CF3    0x25
#define STA350_CFUD     0x26
#define STA350_MPCC1    0x27
#define STA350_MPCC2    0x28
#define STA350_DCC1     0x29
#define STA350_DCC2     0x2a
#define STA350_FDRC1    0x2b
#define STA350_FDRC2    0x2c
#define STA350_STATUS   0x2d
/* reserved: 0x2d - 0x30 */
#define STA350_EQCFG    0x31
#define STA350_EATH1    0x32
#define STA350_ERTH1    0x33
#define STA350_EATH2    0x34
#define STA350_ERTH2    0x35
#define STA350_CONFX    0x36
#define STA350_SVCA     0x37
#define STA350_SVCB     0x38
#define STA350_RMS0A    0x39
#define STA350_RMS0B    0x3a
#define STA350_RMS0C    0x3b
#define STA350_RMS1A    0x3c
#define STA350_RMS1B    0x3d
#define STA350_RMS1C    0x3e
#define STA350_EVOLRES  0x3f
/* reserved: 0x40 - 0x47 */
#define STA350_NSHAPE   0x48
#define STA350_CTXB4B1  0x49
#define STA350_CTXB7B5  0x4a
#define STA350_MISC1    0x4b
#define STA350_MISC2    0x4c

/* 0x00 CONFA */
#define STA350_CONFA_MCS_MASK	0x03
#define STA350_CONFA_MCS_SHIFT	0
#define STA350_CONFA_IR_MASK	0x18
#define STA350_CONFA_IR_SHIFT	3
#define STA350_CONFA_TWRB	BIT(5)
#define STA350_CONFA_TWAB	BIT(6)
#define STA350_CONFA_FDRB	BIT(7)

/* 0x01 CONFB */
#define STA350_CONFB_SAI_MASK	0x0f
#define STA350_CONFB_SAI_SHIFT	0
#define STA350_CONFB_SAIFB	BIT(4)
#define STA350_CONFB_DSCKE	BIT(5)
#define STA350_CONFB_C1IM	BIT(6)
#define STA350_CONFB_C2IM	BIT(7)

/* 0x02 CONFC */
#define STA350_CONFC_OM_MASK	0x03
#define STA350_CONFC_OM_SHIFT	0
#define STA350_CONFC_CSZ_MASK	0x3c
#define STA350_CONFC_CSZ_SHIFT	2
#define STA350_CONFC_OCRB	BIT(7)

/* 0x03 CONFD */
#define STA350_CONFD_HPB_SHIFT	0
#define STA350_CONFD_DEMP_SHIFT	1
#define STA350_CONFD_DSPB_SHIFT	2
#define STA350_CONFD_PSL_SHIFT	3
#define STA350_CONFD_BQL_SHIFT	4
#define STA350_CONFD_DRC_SHIFT	5
#define STA350_CONFD_ZDE_SHIFT	6
#define STA350_CONFD_SME_SHIFT	7

/* 0x04 CONFE */
#define STA350_CONFE_MPCV	BIT(0)
#define STA350_CONFE_MPCV_SHIFT	0
#define STA350_CONFE_MPC	BIT(1)
#define STA350_CONFE_MPC_SHIFT	1
#define STA350_CONFE_NSBW	BIT(2)
#define STA350_CONFE_NSBW_SHIFT	2
#define STA350_CONFE_AME	BIT(3)
#define STA350_CONFE_AME_SHIFT	3
#define STA350_CONFE_PWMS	BIT(4)
#define STA350_CONFE_PWMS_SHIFT	4
#define STA350_CONFE_DCCV	BIT(5)
#define STA350_CONFE_DCCV_SHIFT	5
#define STA350_CONFE_ZCE	BIT(6)
#define STA350_CONFE_ZCE_SHIFT	6
#define STA350_CONFE_SVE	BIT(7)
#define STA350_CONFE_SVE_SHIFT	7

/* 0x05 CONFF */
#define STA350_CONFF_OCFG_MASK	0x03
#define STA350_CONFF_OCFG_SHIFT	0
#define STA350_CONFF_IDE	BIT(2)
#define STA350_CONFF_BCLE	BIT(3)
#define STA350_CONFF_LDTE	BIT(4)
#define STA350_CONFF_ECLE	BIT(5)
#define STA350_CONFF_PWDN	BIT(6)
#define STA350_CONFF_EAPD	BIT(7)

/* 0x06 MMUTE */
#define STA350_MMUTE_MMUTE		0x01
#define STA350_MMUTE_MMUTE_SHIFT	0
#define STA350_MMUTE_C1M		0x02
#define STA350_MMUTE_C1M_SHIFT		1
#define STA350_MMUTE_C2M		0x04
#define STA350_MMUTE_C2M_SHIFT		2
#define STA350_MMUTE_C3M		0x08
#define STA350_MMUTE_C3M_SHIFT		3
#define STA350_MMUTE_LOC_MASK		0xC0
#define STA350_MMUTE_LOC_SHIFT		6

/* 0x0b AUTO1 */
#define STA350_AUTO1_AMGC_MASK	0x30
#define STA350_AUTO1_AMGC_SHIFT	4

/* 0x0c AUTO2 */
#define STA350_AUTO2_AMAME	0x01
#define STA350_AUTO2_AMAM_MASK	0x0e
#define STA350_AUTO2_AMAM_SHIFT	1
#define STA350_AUTO2_XO_MASK	0xf0
#define STA350_AUTO2_XO_SHIFT	4

/* 0x0d AUTO3 */
#define STA350_AUTO3_PEQ_MASK	0x1f
#define STA350_AUTO3_PEQ_SHIFT	0

/* 0x0e 0x0f 0x10 CxCFG */
#define STA350_CxCFG_TCB_SHIFT	0
#define STA350_CxCFG_EQBP_SHIFT	1
#define STA350_CxCFG_VBP_SHIFT	2
#define STA350_CxCFG_BO_SHIFT	3
#define STA350_CxCFG_LS_SHIFT	4
#define STA350_CxCFG_OM_MASK	0xc0
#define STA350_CxCFG_OM_SHIFT	6

/* 0x11 TONE */
#define STA350_TONE_BTC_SHIFT	0
#define STA350_TONE_TTC_SHIFT	4

/* 0x12 0x13 0x14 0x15 limiter attack/release */
#define STA350_LxA_SHIFT	0
#define STA350_LxR_SHIFT	4

/* 0x26 CFUD */
#define STA350_CFUD_W1		0x01
#define STA350_CFUD_WA		0x02
#define STA350_CFUD_R1		0x04
#define STA350_CFUD_RA		0x08


/* biquad filter coefficient table offsets */
#define STA350_C1_BQ_BASE	0
#define STA350_C2_BQ_BASE	20
#define STA350_CH_BQ_NUM	4
#define STA350_BQ_NUM_COEF	5
#define STA350_XO_HP_BQ_BASE	40
#define STA350_XO_LP_BQ_BASE	45
#define STA350_C1_PRESCALE	50
#define STA350_C2_PRESCALE	51
#define STA350_C1_POSTSCALE	52
#define STA350_C2_POSTSCALE	53
#define STA350_C3_POSTSCALE	54
#define STA350_TW_POSTSCALE	55
#define STA350_C1_MIX1		56
#define STA350_C1_MIX2		57
#define STA350_C2_MIX1		58
#define STA350_C2_MIX2		59
#define STA350_C3_MIX1		60
#define STA350_C3_MIX2		61

/* miscellaneous register 1 */
#define STA350_MISC1_CPWMEN	BIT(2)
#define STA350_MISC1_BRIDGOFF	BIT(5)
#define STA350_MISC1_NSHHPEN	BIT(6)
#define STA350_MISC1_RPDNEN	BIT(7)

/* miscellaneous register 2 */
#define STA350_MISC2_PNDLSL_MASK	0x1c
#define STA350_MISC2_PNDLSL_SHIFT	2

#endif /* _ASOC_STA_350_H */
