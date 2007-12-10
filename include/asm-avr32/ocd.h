/*
 * AVR32 OCD Interface and register definitions
 *
 * Copyright (C) 2004-2007 Atmel Corporation
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */
#ifndef __ASM_AVR32_OCD_H
#define __ASM_AVR32_OCD_H

/* OCD Register offsets. Abbreviations used below:
 *
 *      BP      Breakpoint
 *      Comm    Communication
 *      DT      Data Trace
 *      PC      Program Counter
 *      PID     Process ID
 *      R/W     Read/Write
 *      WP      Watchpoint
 */
#define OCD_DID				0x0000  /* Device ID */
#define OCD_DC				0x0008  /* Development Control */
#define OCD_DS				0x0010  /* Development Status */
#define OCD_RWCS			0x001c  /* R/W Access Control */
#define OCD_RWA				0x0024  /* R/W Access Address */
#define OCD_RWD				0x0028  /* R/W Access Data */
#define OCD_WT				0x002c  /* Watchpoint Trigger */
#define OCD_DTC				0x0034  /* Data Trace Control */
#define OCD_DTSA0			0x0038  /* DT Start Addr Channel 0 */
#define OCD_DTSA1			0x003c  /* DT Start Addr Channel 1 */
#define OCD_DTEA0			0x0048  /* DT End Addr Channel 0 */
#define OCD_DTEA1			0x004c  /* DT End Addr Channel 1 */
#define OCD_BWC0A			0x0058  /* PC BP/WP Control 0A */
#define OCD_BWC0B			0x005c  /* PC BP/WP Control 0B */
#define OCD_BWC1A			0x0060  /* PC BP/WP Control 1A */
#define OCD_BWC1B			0x0064  /* PC BP/WP Control 1B */
#define OCD_BWC2A			0x0068  /* PC BP/WP Control 2A */
#define OCD_BWC2B			0x006c  /* PC BP/WP Control 2B */
#define OCD_BWC3A			0x0070  /* Data BP/WP Control 3A */
#define OCD_BWC3B			0x0074  /* Data BP/WP Control 3B */
#define OCD_BWA0A			0x0078  /* PC BP/WP Address 0A */
#define OCD_BWA0B			0x007c  /* PC BP/WP Address 0B */
#define OCD_BWA1A			0x0080  /* PC BP/WP Address 1A */
#define OCD_BWA1B			0x0084  /* PC BP/WP Address 1B */
#define OCD_BWA2A			0x0088  /* PC BP/WP Address 2A */
#define OCD_BWA2B			0x008c  /* PC BP/WP Address 2B */
#define OCD_BWA3A			0x0090  /* Data BP/WP Address 3A */
#define OCD_BWA3B			0x0094  /* Data BP/WP Address 3B */
#define OCD_NXCFG			0x0100  /* Nexus Configuration */
#define OCD_DINST			0x0104  /* Debug Instruction */
#define OCD_DPC				0x0108  /* Debug Program Counter */
#define OCD_CPUCM			0x010c  /* CPU Control Mask */
#define OCD_DCCPU			0x0110  /* Debug Comm CPU */
#define OCD_DCEMU			0x0114  /* Debug Comm Emulator */
#define OCD_DCSR			0x0118  /* Debug Comm Status */
#define OCD_PID				0x011c  /* Ownership Trace PID */
#define OCD_EPC0			0x0120  /* Event Pair Control 0 */
#define OCD_EPC1			0x0124  /* Event Pair Control 1 */
#define OCD_EPC2			0x0128  /* Event Pair Control 2 */
#define OCD_EPC3			0x012c  /* Event Pair Control 3 */
#define OCD_AXC				0x0130  /* AUX port Control */

/* Bits in DID */
#define OCD_DID_MID_START		1
#define OCD_DID_MID_SIZE		11
#define OCD_DID_PN_START		12
#define OCD_DID_PN_SIZE			16
#define OCD_DID_RN_START		28
#define OCD_DID_RN_SIZE			4

/* Bits in DC */
#define OCD_DC_TM_START			0
#define OCD_DC_TM_SIZE			2
#define OCD_DC_EIC_START		3
#define OCD_DC_EIC_SIZE			2
#define OCD_DC_OVC_START		5
#define OCD_DC_OVC_SIZE			3
#define OCD_DC_SS_BIT			8
#define OCD_DC_DBR_BIT			12
#define OCD_DC_DBE_BIT			13
#define OCD_DC_EOS_START		20
#define OCD_DC_EOS_SIZE			2
#define OCD_DC_SQA_BIT			22
#define OCD_DC_IRP_BIT			23
#define OCD_DC_IFM_BIT			24
#define OCD_DC_TOZ_BIT			25
#define OCD_DC_TSR_BIT			26
#define OCD_DC_RID_BIT			27
#define OCD_DC_ORP_BIT			28
#define OCD_DC_MM_BIT			29
#define OCD_DC_RES_BIT			30
#define OCD_DC_ABORT_BIT		31

/* Bits in DS */
#define OCD_DS_SSS_BIT			0
#define OCD_DS_SWB_BIT			1
#define OCD_DS_HWB_BIT			2
#define OCD_DS_HWE_BIT			3
#define OCD_DS_STP_BIT			4
#define OCD_DS_DBS_BIT			5
#define OCD_DS_BP_START			8
#define OCD_DS_BP_SIZE			8
#define OCD_DS_INC_BIT			24
#define OCD_DS_BOZ_BIT			25
#define OCD_DS_DBA_BIT			26
#define OCD_DS_EXB_BIT			27
#define OCD_DS_NTBF_BIT			28

/* Bits in RWCS */
#define OCD_RWCS_DV_BIT			0
#define OCD_RWCS_ERR_BIT		1
#define OCD_RWCS_CNT_START		2
#define OCD_RWCS_CNT_SIZE		14
#define OCD_RWCS_CRC_BIT		19
#define OCD_RWCS_NTBC_START		20
#define OCD_RWCS_NTBC_SIZE		2
#define OCD_RWCS_NTE_BIT		22
#define OCD_RWCS_NTAP_BIT		23
#define OCD_RWCS_WRAPPED_BIT		24
#define OCD_RWCS_CCTRL_START		25
#define OCD_RWCS_CCTRL_SIZE		2
#define OCD_RWCS_SZ_START		27
#define OCD_RWCS_SZ_SIZE		3
#define OCD_RWCS_RW_BIT			30
#define OCD_RWCS_AC_BIT			31

/* Bits in RWA */
#define OCD_RWA_RWA_START		0
#define OCD_RWA_RWA_SIZE		32

/* Bits in RWD */
#define OCD_RWD_RWD_START		0
#define OCD_RWD_RWD_SIZE		32

/* Bits in WT */
#define OCD_WT_DTE_START		20
#define OCD_WT_DTE_SIZE			3
#define OCD_WT_DTS_START		23
#define OCD_WT_DTS_SIZE			3
#define OCD_WT_PTE_START		26
#define OCD_WT_PTE_SIZE			3
#define OCD_WT_PTS_START		29
#define OCD_WT_PTS_SIZE			3

/* Bits in DTC */
#define OCD_DTC_T0WP_BIT		0
#define OCD_DTC_T1WP_BIT		1
#define OCD_DTC_ASID0EN_BIT		2
#define OCD_DTC_ASID0_START		3
#define OCD_DTC_ASID0_SIZE		8
#define OCD_DTC_ASID1EN_BIT		11
#define OCD_DTC_ASID1_START		12
#define OCD_DTC_ASID1_SIZE		8
#define OCD_DTC_RWT1_START		28
#define OCD_DTC_RWT1_SIZE		2
#define OCD_DTC_RWT0_START		30
#define OCD_DTC_RWT0_SIZE		2

/* Bits in DTSA0 */
#define OCD_DTSA0_DTSA_START		0
#define OCD_DTSA0_DTSA_SIZE		32

/* Bits in DTSA1 */
#define OCD_DTSA1_DTSA_START		0
#define OCD_DTSA1_DTSA_SIZE		32

/* Bits in DTEA0 */
#define OCD_DTEA0_DTEA_START		0
#define OCD_DTEA0_DTEA_SIZE		32

/* Bits in DTEA1 */
#define OCD_DTEA1_DTEA_START		0
#define OCD_DTEA1_DTEA_SIZE		32

/* Bits in BWC0A */
#define OCD_BWC0A_ASIDEN_BIT		0
#define OCD_BWC0A_ASID_START		1
#define OCD_BWC0A_ASID_SIZE		8
#define OCD_BWC0A_EOC_BIT		14
#define OCD_BWC0A_AME_BIT		25
#define OCD_BWC0A_BWE_START		30
#define OCD_BWC0A_BWE_SIZE		2

/* Bits in BWC0B */
#define OCD_BWC0B_ASIDEN_BIT		0
#define OCD_BWC0B_ASID_START		1
#define OCD_BWC0B_ASID_SIZE		8
#define OCD_BWC0B_EOC_BIT		14
#define OCD_BWC0B_AME_BIT		25
#define OCD_BWC0B_BWE_START		30
#define OCD_BWC0B_BWE_SIZE		2

/* Bits in BWC1A */
#define OCD_BWC1A_ASIDEN_BIT		0
#define OCD_BWC1A_ASID_START		1
#define OCD_BWC1A_ASID_SIZE		8
#define OCD_BWC1A_EOC_BIT		14
#define OCD_BWC1A_AME_BIT		25
#define OCD_BWC1A_BWE_START		30
#define OCD_BWC1A_BWE_SIZE		2

/* Bits in BWC1B */
#define OCD_BWC1B_ASIDEN_BIT		0
#define OCD_BWC1B_ASID_START		1
#define OCD_BWC1B_ASID_SIZE		8
#define OCD_BWC1B_EOC_BIT		14
#define OCD_BWC1B_AME_BIT		25
#define OCD_BWC1B_BWE_START		30
#define OCD_BWC1B_BWE_SIZE		2

/* Bits in BWC2A */
#define OCD_BWC2A_ASIDEN_BIT		0
#define OCD_BWC2A_ASID_START		1
#define OCD_BWC2A_ASID_SIZE		8
#define OCD_BWC2A_EOC_BIT		14
#define OCD_BWC2A_AMB_START		20
#define OCD_BWC2A_AMB_SIZE		5
#define OCD_BWC2A_AME_BIT		25
#define OCD_BWC2A_BWE_START		30
#define OCD_BWC2A_BWE_SIZE		2

/* Bits in BWC2B */
#define OCD_BWC2B_ASIDEN_BIT		0
#define OCD_BWC2B_ASID_START		1
#define OCD_BWC2B_ASID_SIZE		8
#define OCD_BWC2B_EOC_BIT		14
#define OCD_BWC2B_AME_BIT		25
#define OCD_BWC2B_BWE_START		30
#define OCD_BWC2B_BWE_SIZE		2

/* Bits in BWC3A */
#define OCD_BWC3A_ASIDEN_BIT		0
#define OCD_BWC3A_ASID_START		1
#define OCD_BWC3A_ASID_SIZE		8
#define OCD_BWC3A_SIZE_START		9
#define OCD_BWC3A_SIZE_SIZE		3
#define OCD_BWC3A_EOC_BIT		14
#define OCD_BWC3A_BWO_START		16
#define OCD_BWC3A_BWO_SIZE		2
#define OCD_BWC3A_BME_START		20
#define OCD_BWC3A_BME_SIZE		4
#define OCD_BWC3A_BRW_START		28
#define OCD_BWC3A_BRW_SIZE		2
#define OCD_BWC3A_BWE_START		30
#define OCD_BWC3A_BWE_SIZE		2

/* Bits in BWC3B */
#define OCD_BWC3B_ASIDEN_BIT		0
#define OCD_BWC3B_ASID_START		1
#define OCD_BWC3B_ASID_SIZE		8
#define OCD_BWC3B_SIZE_START		9
#define OCD_BWC3B_SIZE_SIZE		3
#define OCD_BWC3B_EOC_BIT		14
#define OCD_BWC3B_BWO_START		16
#define OCD_BWC3B_BWO_SIZE		2
#define OCD_BWC3B_BME_START		20
#define OCD_BWC3B_BME_SIZE		4
#define OCD_BWC3B_BRW_START		28
#define OCD_BWC3B_BRW_SIZE		2
#define OCD_BWC3B_BWE_START		30
#define OCD_BWC3B_BWE_SIZE		2

/* Bits in BWA0A */
#define OCD_BWA0A_BWA_START		0
#define OCD_BWA0A_BWA_SIZE		32

/* Bits in BWA0B */
#define OCD_BWA0B_BWA_START		0
#define OCD_BWA0B_BWA_SIZE		32

/* Bits in BWA1A */
#define OCD_BWA1A_BWA_START		0
#define OCD_BWA1A_BWA_SIZE		32

/* Bits in BWA1B */
#define OCD_BWA1B_BWA_START		0
#define OCD_BWA1B_BWA_SIZE		32

/* Bits in BWA2A */
#define OCD_BWA2A_BWA_START		0
#define OCD_BWA2A_BWA_SIZE		32

/* Bits in BWA2B */
#define OCD_BWA2B_BWA_START		0
#define OCD_BWA2B_BWA_SIZE		32

/* Bits in BWA3A */
#define OCD_BWA3A_BWA_START		0
#define OCD_BWA3A_BWA_SIZE		32

/* Bits in BWA3B */
#define OCD_BWA3B_BWA_START		0
#define OCD_BWA3B_BWA_SIZE		32

/* Bits in NXCFG */
#define OCD_NXCFG_NXARCH_START		0
#define OCD_NXCFG_NXARCH_SIZE		4
#define OCD_NXCFG_NXOCD_START		4
#define OCD_NXCFG_NXOCD_SIZE		4
#define OCD_NXCFG_NXPCB_START		8
#define OCD_NXCFG_NXPCB_SIZE		4
#define OCD_NXCFG_NXDB_START		12
#define OCD_NXCFG_NXDB_SIZE		4
#define OCD_NXCFG_MXMSEO_BIT		16
#define OCD_NXCFG_NXMDO_START		17
#define OCD_NXCFG_NXMDO_SIZE		4
#define OCD_NXCFG_NXPT_BIT		21
#define OCD_NXCFG_NXOT_BIT		22
#define OCD_NXCFG_NXDWT_BIT		23
#define OCD_NXCFG_NXDRT_BIT		24
#define OCD_NXCFG_NXDTC_START		25
#define OCD_NXCFG_NXDTC_SIZE		3
#define OCD_NXCFG_NXDMA_BIT		28

/* Bits in DINST */
#define OCD_DINST_DINST_START		0
#define OCD_DINST_DINST_SIZE		32

/* Bits in CPUCM */
#define OCD_CPUCM_BEM_BIT		1
#define OCD_CPUCM_FEM_BIT		2
#define OCD_CPUCM_REM_BIT		3
#define OCD_CPUCM_IBEM_BIT		4
#define OCD_CPUCM_IEEM_BIT		5

/* Bits in DCCPU */
#define OCD_DCCPU_DATA_START		0
#define OCD_DCCPU_DATA_SIZE		32

/* Bits in DCEMU */
#define OCD_DCEMU_DATA_START		0
#define OCD_DCEMU_DATA_SIZE		32

/* Bits in DCSR */
#define OCD_DCSR_CPUD_BIT		0
#define OCD_DCSR_EMUD_BIT		1

/* Bits in PID */
#define OCD_PID_PROCESS_START		0
#define OCD_PID_PROCESS_SIZE		32

/* Bits in EPC0 */
#define OCD_EPC0_RNG_START		0
#define OCD_EPC0_RNG_SIZE		2
#define OCD_EPC0_CE_BIT			4
#define OCD_EPC0_ECNT_START		16
#define OCD_EPC0_ECNT_SIZE		16

/* Bits in EPC1 */
#define OCD_EPC1_RNG_START		0
#define OCD_EPC1_RNG_SIZE		2
#define OCD_EPC1_ATB_BIT		5
#define OCD_EPC1_AM_BIT			6

/* Bits in EPC2 */
#define OCD_EPC2_RNG_START		0
#define OCD_EPC2_RNG_SIZE		2
#define OCD_EPC2_DB_START		2
#define OCD_EPC2_DB_SIZE		2

/* Bits in EPC3 */
#define OCD_EPC3_RNG_START		0
#define OCD_EPC3_RNG_SIZE		2
#define OCD_EPC3_DWE_BIT		2

/* Bits in AXC */
#define OCD_AXC_DIV_START		0
#define OCD_AXC_DIV_SIZE		4
#define OCD_AXC_AXE_BIT			8
#define OCD_AXC_AXS_BIT			9
#define OCD_AXC_DDR_BIT			10
#define OCD_AXC_LS_BIT			11
#define OCD_AXC_REX_BIT			12
#define OCD_AXC_REXTEN_BIT		13

/* Constants for DC:EIC */
#define OCD_EIC_PROGRAM_AND_DATA_TRACE	0
#define OCD_EIC_BREAKPOINT		1
#define OCD_EIC_NOP			2

/* Constants for DC:OVC */
#define OCD_OVC_OVERRUN			0
#define OCD_OVC_DELAY_CPU_BTM		1
#define OCD_OVC_DELAY_CPU_DTM		2
#define OCD_OVC_DELAY_CPU_BTM_DTM	3

/* Constants for DC:EOS */
#define OCD_EOS_NOP			0
#define OCD_EOS_DEBUG_MODE		1
#define OCD_EOS_BREAKPOINT_WATCHPOINT	2
#define OCD_EOS_THQ			3

/* Constants for RWCS:NTBC */
#define OCD_NTBC_OVERWRITE		0
#define OCD_NTBC_DISABLE		1
#define OCD_NTBC_BREAKPOINT		2

/* Constants for RWCS:CCTRL */
#define OCD_CCTRL_AUTO			0
#define OCD_CCTRL_CACHED		1
#define OCD_CCTRL_UNCACHED		2

/* Constants for RWCS:SZ */
#define OCD_SZ_BYTE			0
#define OCD_SZ_HALFWORD			1
#define OCD_SZ_WORD			2

/* Constants for WT:PTS */
#define OCD_PTS_DISABLED		0
#define OCD_PTS_PROGRAM_0B		1
#define OCD_PTS_PROGRAM_1A		2
#define OCD_PTS_PROGRAM_1B		3
#define OCD_PTS_PROGRAM_2A		4
#define OCD_PTS_PROGRAM_2B		5
#define OCD_PTS_DATA_3A			6
#define OCD_PTS_DATA_3B			7

/* Constants for DTC:RWT1 */
#define OCD_RWT1_NO_TRACE		0
#define OCD_RWT1_DATA_READ		1
#define OCD_RWT1_DATA_WRITE		2
#define OCD_RWT1_DATA_READ_WRITE	3

/* Constants for DTC:RWT0 */
#define OCD_RWT0_NO_TRACE		0
#define OCD_RWT0_DATA_READ		1
#define OCD_RWT0_DATA_WRITE		2
#define OCD_RWT0_DATA_READ_WRITE	3

/* Constants for BWC0A:BWE */
#define OCD_BWE_DISABLED		0
#define OCD_BWE_BREAKPOINT_ENABLED	1
#define OCD_BWE_WATCHPOINT_ENABLED	3

/* Constants for BWC0B:BWE */
#define OCD_BWE_DISABLED		0
#define OCD_BWE_BREAKPOINT_ENABLED	1
#define OCD_BWE_WATCHPOINT_ENABLED	3

/* Constants for BWC1A:BWE */
#define OCD_BWE_DISABLED		0
#define OCD_BWE_BREAKPOINT_ENABLED	1
#define OCD_BWE_WATCHPOINT_ENABLED	3

/* Constants for BWC1B:BWE */
#define OCD_BWE_DISABLED		0
#define OCD_BWE_BREAKPOINT_ENABLED	1
#define OCD_BWE_WATCHPOINT_ENABLED	3

/* Constants for BWC2A:BWE */
#define OCD_BWE_DISABLED		0
#define OCD_BWE_BREAKPOINT_ENABLED	1
#define OCD_BWE_WATCHPOINT_ENABLED	3

/* Constants for BWC2B:BWE */
#define OCD_BWE_DISABLED		0
#define OCD_BWE_BREAKPOINT_ENABLED	1
#define OCD_BWE_WATCHPOINT_ENABLED	3

/* Constants for BWC3A:SIZE */
#define OCD_SIZE_BYTE_ACCESS		4
#define OCD_SIZE_HALFWORD_ACCESS	5
#define OCD_SIZE_WORD_ACCESS		6
#define OCD_SIZE_DOUBLE_WORD_ACCESS	7

/* Constants for BWC3A:BRW */
#define OCD_BRW_READ_BREAK		0
#define OCD_BRW_WRITE_BREAK		1
#define OCD_BRW_ANY_ACCES_BREAK		2

/* Constants for BWC3A:BWE */
#define OCD_BWE_DISABLED		0
#define OCD_BWE_BREAKPOINT_ENABLED	1
#define OCD_BWE_WATCHPOINT_ENABLED	3

/* Constants for BWC3B:SIZE */
#define OCD_SIZE_BYTE_ACCESS		4
#define OCD_SIZE_HALFWORD_ACCESS	5
#define OCD_SIZE_WORD_ACCESS		6
#define OCD_SIZE_DOUBLE_WORD_ACCESS	7

/* Constants for BWC3B:BRW */
#define OCD_BRW_READ_BREAK		0
#define OCD_BRW_WRITE_BREAK		1
#define OCD_BRW_ANY_ACCES_BREAK		2

/* Constants for BWC3B:BWE */
#define OCD_BWE_DISABLED		0
#define OCD_BWE_BREAKPOINT_ENABLED	1
#define OCD_BWE_WATCHPOINT_ENABLED	3

/* Constants for EPC0:RNG */
#define OCD_RNG_DISABLED		0
#define OCD_RNG_EXCLUSIVE		1
#define OCD_RNG_INCLUSIVE		2

/* Constants for EPC1:RNG */
#define OCD_RNG_DISABLED		0
#define OCD_RNG_EXCLUSIVE		1
#define OCD_RNG_INCLUSIVE		2

/* Constants for EPC2:RNG */
#define OCD_RNG_DISABLED		0
#define OCD_RNG_EXCLUSIVE		1
#define OCD_RNG_INCLUSIVE		2

/* Constants for EPC2:DB */
#define OCD_DB_DISABLED			0
#define OCD_DB_CHAINED_B		1
#define OCD_DB_CHAINED_A		2
#define OCD_DB_AHAINED_A_AND_B		3

/* Constants for EPC3:RNG */
#define OCD_RNG_DISABLED		0
#define OCD_RNG_EXCLUSIVE		1
#define OCD_RNG_INCLUSIVE		2

#ifndef __ASSEMBLER__

/* Register access macros */
static inline unsigned long __ocd_read(unsigned int reg)
{
	return __builtin_mfdr(reg);
}

static inline void __ocd_write(unsigned int reg, unsigned long value)
{
	__builtin_mtdr(reg, value);
}

#define ocd_read(reg)			__ocd_read(OCD_##reg)
#define ocd_write(reg, value)		__ocd_write(OCD_##reg, value)

#endif /* !__ASSEMBLER__ */

#endif /* __ASM_AVR32_OCD_H */
