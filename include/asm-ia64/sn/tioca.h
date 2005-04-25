#ifndef _ASM_IA64_SN_TIO_TIOCA_H
#define _ASM_IA64_SN_TIO_TIOCA_H

/*
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file "COPYING" in the main directory of this archive
 * for more details.
 *
 * Copyright (c) 2003-2005 Silicon Graphics, Inc. All rights reserved.
 */


#define TIOCA_PART_NUM	0xE020
#define TIOCA_MFGR_NUM	0x24
#define TIOCA_REV_A	0x1

/*
 * Register layout for TIO:CA.  See below for bitmasks for each register.
 */

struct tioca {
	uint64_t	ca_id;				/* 0x000000 */
	uint64_t	ca_control1;			/* 0x000008 */
	uint64_t	ca_control2;			/* 0x000010 */
	uint64_t	ca_status1;			/* 0x000018 */
	uint64_t	ca_status2;			/* 0x000020 */
	uint64_t	ca_gart_aperature;		/* 0x000028 */
	uint64_t	ca_gfx_detach;			/* 0x000030 */
	uint64_t	ca_inta_dest_addr;		/* 0x000038 */
	uint64_t	ca_intb_dest_addr;		/* 0x000040 */
	uint64_t	ca_err_int_dest_addr;		/* 0x000048 */
	uint64_t	ca_int_status;			/* 0x000050 */
	uint64_t	ca_int_status_alias;		/* 0x000058 */
	uint64_t	ca_mult_error;			/* 0x000060 */
	uint64_t	ca_mult_error_alias;		/* 0x000068 */
	uint64_t	ca_first_error;			/* 0x000070 */
	uint64_t	ca_int_mask;			/* 0x000078 */
	uint64_t	ca_crm_pkterr_type;		/* 0x000080 */
	uint64_t	ca_crm_pkterr_type_alias;	/* 0x000088 */
	uint64_t	ca_crm_ct_error_detail_1;	/* 0x000090 */
	uint64_t	ca_crm_ct_error_detail_2;	/* 0x000098 */
	uint64_t	ca_crm_tnumto;			/* 0x0000A0 */
	uint64_t	ca_gart_err;			/* 0x0000A8 */
	uint64_t	ca_pcierr_type;			/* 0x0000B0 */
	uint64_t	ca_pcierr_addr;			/* 0x0000B8 */

	uint64_t	ca_pad_0000C0[3];		/* 0x0000{C0..D0} */

	uint64_t	ca_pci_rd_buf_flush;		/* 0x0000D8 */
	uint64_t	ca_pci_dma_addr_extn;		/* 0x0000E0 */
	uint64_t	ca_agp_dma_addr_extn;		/* 0x0000E8 */
	uint64_t	ca_force_inta;			/* 0x0000F0 */
	uint64_t	ca_force_intb;			/* 0x0000F8 */
	uint64_t	ca_debug_vector_sel;		/* 0x000100 */
	uint64_t	ca_debug_mux_core_sel;		/* 0x000108 */
	uint64_t	ca_debug_mux_pci_sel;		/* 0x000110 */
	uint64_t	ca_debug_domain_sel;		/* 0x000118 */

	uint64_t	ca_pad_000120[28];		/* 0x0001{20..F8} */

	uint64_t	ca_gart_ptr_table;		/* 0x200 */
	uint64_t	ca_gart_tlb_addr[8];		/* 0x2{08..40} */
};

/*
 * Mask/shift definitions for TIO:CA registers.  The convention here is
 * to mainly use the names as they appear in the "TIO AEGIS Programmers'
 * Reference" with a CA_ prefix added.  Some exceptions were made to fix
 * duplicate field names or to generalize fields that are common to
 * different registers (ca_debug_mux_core_sel and ca_debug_mux_pci_sel for
 * example).
 *
 * Fields consisting of a single bit have a single #define have a single
 * macro declaration to mask the bit.  Fields consisting of multiple bits
 * have two declarations: one to mask the proper bits in a register, and 
 * a second with the suffix "_SHFT" to identify how far the mask needs to
 * be shifted right to get its base value.
 */

/* ==== ca_control1 */
#define CA_SYS_BIG_END			(1ull << 0)
#define CA_DMA_AGP_SWAP			(1ull << 1)
#define CA_DMA_PCI_SWAP			(1ull << 2)
#define CA_PIO_IO_SWAP			(1ull << 3)
#define CA_PIO_MEM_SWAP			(1ull << 4)
#define CA_GFX_WR_SWAP			(1ull << 5)
#define CA_AGP_FW_ENABLE		(1ull << 6)
#define CA_AGP_CAL_CYCLE		(0x7ull << 7)
#define CA_AGP_CAL_CYCLE_SHFT		7
#define CA_AGP_CAL_PRSCL_BYP		(1ull << 10)
#define CA_AGP_INIT_CAL_ENB		(1ull << 11)
#define CA_INJ_ADDR_PERR		(1ull << 12)
#define CA_INJ_DATA_PERR		(1ull << 13)
	/* bits 15:14 unused */
#define CA_PCIM_IO_NBE_AD		(0x7ull << 16)
#define CA_PCIM_IO_NBE_AD_SHFT		16
#define CA_PCIM_FAST_BTB_ENB		(1ull << 19)
	/* bits 23:20 unused */
#define CA_PIO_ADDR_OFFSET		(0xffull << 24)
#define CA_PIO_ADDR_OFFSET_SHFT		24
	/* bits 35:32 unused */
#define CA_AGPDMA_OP_COMBDELAY		(0x1full << 36)
#define CA_AGPDMA_OP_COMBDELAY_SHFT	36
	/* bit 41 unused */
#define CA_AGPDMA_OP_ENB_COMBDELAY	(1ull << 42)
#define	CA_PCI_INT_LPCNT		(0xffull << 44)
#define CA_PCI_INT_LPCNT_SHFT		44
	/* bits 63:52 unused */

/* ==== ca_control2 */
#define CA_AGP_LATENCY_TO		(0xffull << 0)
#define CA_AGP_LATENCY_TO_SHFT		0
#define CA_PCI_LATENCY_TO		(0xffull << 8)
#define CA_PCI_LATENCY_TO_SHFT		8
#define CA_PCI_MAX_RETRY		(0x3ffull << 16)
#define CA_PCI_MAX_RETRY_SHFT		16
	/* bits 27:26 unused */
#define CA_RT_INT_EN			(0x3ull << 28)
#define CA_RT_INT_EN_SHFT			28
#define CA_MSI_INT_ENB			(1ull << 30)
#define CA_PCI_ARB_ERR_ENB		(1ull << 31)
#define CA_GART_MEM_PARAM		(0x3ull << 32)
#define CA_GART_MEM_PARAM_SHFT		32
#define CA_GART_RD_PREFETCH_ENB		(1ull << 34)
#define CA_GART_WR_PREFETCH_ENB		(1ull << 35)
#define CA_GART_FLUSH_TLB		(1ull << 36)
	/* bits 39:37 unused */
#define CA_CRM_TNUMTO_PERIOD		(0x1fffull << 40)
#define CA_CRM_TNUMTO_PERIOD_SHFT	40
	/* bits 55:53 unused */
#define CA_CRM_TNUMTO_ENB		(1ull << 56)
#define CA_CRM_PRESCALER_BYP		(1ull << 57)
	/* bits 59:58 unused */
#define CA_CRM_MAX_CREDIT		(0x7ull << 60)
#define CA_CRM_MAX_CREDIT_SHFT		60
	/* bit 63 unused */

/* ==== ca_status1 */
#define CA_CORELET_ID			(0x3ull << 0)
#define CA_CORELET_ID_SHFT		0
#define CA_INTA_N			(1ull << 2)
#define CA_INTB_N			(1ull << 3)
#define CA_CRM_CREDIT_AVAIL		(0x7ull << 4)
#define CA_CRM_CREDIT_AVAIL_SHFT	4
	/* bit 7 unused */
#define CA_CRM_SPACE_AVAIL		(0x7full << 8)
#define CA_CRM_SPACE_AVAIL_SHFT		8
	/* bit 15 unused */
#define CA_GART_TLB_VAL			(0xffull << 16)
#define CA_GART_TLB_VAL_SHFT		16
	/* bits 63:24 unused */

/* ==== ca_status2 */
#define CA_GFX_CREDIT_AVAIL		(0xffull << 0)
#define CA_GFX_CREDIT_AVAIL_SHFT	0
#define CA_GFX_OPQ_AVAIL		(0xffull << 8)
#define CA_GFX_OPQ_AVAIL_SHFT		8
#define CA_GFX_WRBUFF_AVAIL		(0xffull << 16)
#define CA_GFX_WRBUFF_AVAIL_SHFT	16
#define CA_ADMA_OPQ_AVAIL		(0xffull << 24)
#define CA_ADMA_OPQ_AVAIL_SHFT		24
#define CA_ADMA_WRBUFF_AVAIL		(0xffull << 32)
#define CA_ADMA_WRBUFF_AVAIL_SHFT	32
#define CA_ADMA_RDBUFF_AVAIL		(0x7full << 40)
#define CA_ADMA_RDBUFF_AVAIL_SHFT	40
#define CA_PCI_PIO_OP_STAT		(1ull << 47)
#define CA_PDMA_OPQ_AVAIL		(0xfull << 48)
#define CA_PDMA_OPQ_AVAIL_SHFT		48
#define CA_PDMA_WRBUFF_AVAIL		(0xfull << 52)
#define CA_PDMA_WRBUFF_AVAIL_SHFT	52
#define CA_PDMA_RDBUFF_AVAIL		(0x3ull << 56)
#define CA_PDMA_RDBUFF_AVAIL_SHFT	56
	/* bits 63:58 unused */

/* ==== ca_gart_aperature */
#define CA_GART_AP_ENB_AGP		(1ull << 0)
#define CA_GART_PAGE_SIZE		(1ull << 1)
#define CA_GART_AP_ENB_PCI		(1ull << 2)
	/* bits 11:3 unused */
#define CA_GART_AP_SIZE			(0x3ffull << 12)
#define CA_GART_AP_SIZE_SHFT		12
#define CA_GART_AP_BASE			(0x3ffffffffffull << 22)
#define CA_GART_AP_BASE_SHFT		22

/* ==== ca_inta_dest_addr
   ==== ca_intb_dest_addr 
   ==== ca_err_int_dest_addr */
	/* bits 2:0 unused */
#define CA_INT_DEST_ADDR		(0x7ffffffffffffull << 3)
#define CA_INT_DEST_ADDR_SHFT		3
	/* bits 55:54 unused */
#define CA_INT_DEST_VECT		(0xffull << 56)
#define CA_INT_DEST_VECT_SHFT		56

/* ==== ca_int_status */
/* ==== ca_int_status_alias */
/* ==== ca_mult_error */
/* ==== ca_mult_error_alias */
/* ==== ca_first_error */
/* ==== ca_int_mask */
#define CA_PCI_ERR			(1ull << 0)
	/* bits 3:1 unused */
#define CA_GART_FETCH_ERR		(1ull << 4)
#define CA_GFX_WR_OVFLW			(1ull << 5)
#define CA_PIO_REQ_OVFLW		(1ull << 6)
#define CA_CRM_PKTERR			(1ull << 7)
#define CA_CRM_DVERR			(1ull << 8)
#define CA_TNUMTO			(1ull << 9)
#define CA_CXM_RSP_CRED_OVFLW		(1ull << 10)
#define CA_CXM_REQ_CRED_OVFLW		(1ull << 11)
#define CA_PIO_INVALID_ADDR		(1ull << 12)
#define CA_PCI_ARB_TO			(1ull << 13)
#define CA_AGP_REQ_OFLOW		(1ull << 14)
#define CA_SBA_TYPE1_ERR		(1ull << 15)
	/* bit 16 unused */
#define CA_INTA				(1ull << 17)
#define CA_INTB				(1ull << 18)
#define CA_MULT_INTA			(1ull << 19)
#define CA_MULT_INTB			(1ull << 20)
#define CA_GFX_CREDIT_OVFLW		(1ull << 21)
	/* bits 63:22 unused */

/* ==== ca_crm_pkterr_type */
/* ==== ca_crm_pkterr_type_alias */
#define CA_CRM_PKTERR_SBERR_HDR		(1ull << 0)
#define CA_CRM_PKTERR_DIDN		(1ull << 1)
#define CA_CRM_PKTERR_PACTYPE		(1ull << 2)
#define CA_CRM_PKTERR_INV_TNUM		(1ull << 3)
#define CA_CRM_PKTERR_ADDR_RNG		(1ull << 4)
#define CA_CRM_PKTERR_ADDR_ALGN		(1ull << 5)
#define CA_CRM_PKTERR_HDR_PARAM		(1ull << 6)
#define CA_CRM_PKTERR_CW_ERR		(1ull << 7)
#define CA_CRM_PKTERR_SBERR_NH		(1ull << 8)
#define CA_CRM_PKTERR_EARLY_TERM	(1ull << 9)
#define CA_CRM_PKTERR_EARLY_TAIL	(1ull << 10)
#define CA_CRM_PKTERR_MSSNG_TAIL	(1ull << 11)
#define CA_CRM_PKTERR_MSSNG_HDR		(1ull << 12)
	/* bits 15:13 unused */
#define CA_FIRST_CRM_PKTERR_SBERR_HDR	(1ull << 16)
#define CA_FIRST_CRM_PKTERR_DIDN	(1ull << 17)
#define CA_FIRST_CRM_PKTERR_PACTYPE	(1ull << 18)
#define CA_FIRST_CRM_PKTERR_INV_TNUM	(1ull << 19)
#define CA_FIRST_CRM_PKTERR_ADDR_RNG	(1ull << 20)
#define CA_FIRST_CRM_PKTERR_ADDR_ALGN	(1ull << 21)
#define CA_FIRST_CRM_PKTERR_HDR_PARAM	(1ull << 22)
#define CA_FIRST_CRM_PKTERR_CW_ERR	(1ull << 23)
#define CA_FIRST_CRM_PKTERR_SBERR_NH	(1ull << 24)
#define CA_FIRST_CRM_PKTERR_EARLY_TERM	(1ull << 25)
#define CA_FIRST_CRM_PKTERR_EARLY_TAIL	(1ull << 26)
#define CA_FIRST_CRM_PKTERR_MSSNG_TAIL	(1ull << 27)
#define CA_FIRST_CRM_PKTERR_MSSNG_HDR	(1ull << 28)
	/* bits 63:29 unused */

/* ==== ca_crm_ct_error_detail_1 */
#define CA_PKT_TYPE			(0xfull << 0)
#define CA_PKT_TYPE_SHFT		0
#define CA_SRC_ID			(0x3ull << 4)
#define CA_SRC_ID_SHFT			4
#define CA_DATA_SZ			(0x3ull << 6)
#define CA_DATA_SZ_SHFT			6
#define CA_TNUM				(0xffull << 8)
#define CA_TNUM_SHFT			8
#define CA_DW_DATA_EN			(0xffull << 16)
#define CA_DW_DATA_EN_SHFT		16
#define CA_GFX_CRED			(0xffull << 24)
#define CA_GFX_CRED_SHFT		24
#define CA_MEM_RD_PARAM			(0x3ull << 32)
#define CA_MEM_RD_PARAM_SHFT		32
#define CA_PIO_OP			(1ull << 34)
#define CA_CW_ERR			(1ull << 35)
	/* bits 62:36 unused */
#define CA_VALID			(1ull << 63)

/* ==== ca_crm_ct_error_detail_2 */
	/* bits 2:0 unused */
#define CA_PKT_ADDR			(0x1fffffffffffffull << 3)
#define CA_PKT_ADDR_SHFT		3
	/* bits 63:56 unused */

/* ==== ca_crm_tnumto */
#define CA_CRM_TNUMTO_VAL		(0xffull << 0)
#define CA_CRM_TNUMTO_VAL_SHFT		0
#define CA_CRM_TNUMTO_WR		(1ull << 8)
	/* bits 63:9 unused */

/* ==== ca_gart_err */
#define CA_GART_ERR_SOURCE		(0x3ull << 0)
#define CA_GART_ERR_SOURCE_SHFT		0
	/* bits 3:2 unused */
#define CA_GART_ERR_ADDR		(0xfffffffffull << 4)
#define CA_GART_ERR_ADDR_SHFT		4
	/* bits 63:40 unused */

/* ==== ca_pcierr_type */
#define CA_PCIERR_DATA			(0xffffffffull << 0)
#define CA_PCIERR_DATA_SHFT		0
#define CA_PCIERR_ENB			(0xfull << 32)
#define CA_PCIERR_ENB_SHFT		32
#define CA_PCIERR_CMD			(0xfull << 36)
#define CA_PCIERR_CMD_SHFT		36
#define CA_PCIERR_A64			(1ull << 40)
#define CA_PCIERR_SLV_SERR		(1ull << 41)
#define CA_PCIERR_SLV_WR_PERR		(1ull << 42)
#define CA_PCIERR_SLV_RD_PERR		(1ull << 43)
#define CA_PCIERR_MST_SERR		(1ull << 44)
#define CA_PCIERR_MST_WR_PERR		(1ull << 45)
#define CA_PCIERR_MST_RD_PERR		(1ull << 46)
#define CA_PCIERR_MST_MABT		(1ull << 47)
#define CA_PCIERR_MST_TABT		(1ull << 48)
#define CA_PCIERR_MST_RETRY_TOUT	(1ull << 49)

#define CA_PCIERR_TYPES \
	(CA_PCIERR_A64|CA_PCIERR_SLV_SERR| \
	 CA_PCIERR_SLV_WR_PERR|CA_PCIERR_SLV_RD_PERR| \
	 CA_PCIERR_MST_SERR|CA_PCIERR_MST_WR_PERR|CA_PCIERR_MST_RD_PERR| \
	 CA_PCIERR_MST_MABT|CA_PCIERR_MST_TABT|CA_PCIERR_MST_RETRY_TOUT)

	/* bits 63:50 unused */

/* ==== ca_pci_dma_addr_extn */
#define CA_UPPER_NODE_OFFSET		(0x3full << 0)
#define CA_UPPER_NODE_OFFSET_SHFT	0
	/* bits 7:6 unused */
#define CA_CHIPLET_ID			(0x3ull << 8)
#define CA_CHIPLET_ID_SHFT		8
	/* bits 11:10 unused */
#define CA_PCI_DMA_NODE_ID		(0xffffull << 12)
#define CA_PCI_DMA_NODE_ID_SHFT		12
	/* bits 27:26 unused */
#define CA_PCI_DMA_PIO_MEM_TYPE		(1ull << 28)
	/* bits 63:29 unused */


/* ==== ca_agp_dma_addr_extn */
	/* bits 19:0 unused */
#define CA_AGP_DMA_NODE_ID		(0xffffull << 20)
#define CA_AGP_DMA_NODE_ID_SHFT		20
	/* bits 27:26 unused */
#define CA_AGP_DMA_PIO_MEM_TYPE		(1ull << 28)
	/* bits 63:29 unused */

/* ==== ca_debug_vector_sel */
#define CA_DEBUG_MN_VSEL		(0xfull << 0)
#define CA_DEBUG_MN_VSEL_SHFT		0
#define CA_DEBUG_PP_VSEL		(0xfull << 4)
#define CA_DEBUG_PP_VSEL_SHFT		4
#define CA_DEBUG_GW_VSEL		(0xfull << 8)
#define CA_DEBUG_GW_VSEL_SHFT		8
#define CA_DEBUG_GT_VSEL		(0xfull << 12)
#define CA_DEBUG_GT_VSEL_SHFT		12
#define CA_DEBUG_PD_VSEL		(0xfull << 16)
#define CA_DEBUG_PD_VSEL_SHFT		16
#define CA_DEBUG_AD_VSEL		(0xfull << 20)
#define CA_DEBUG_AD_VSEL_SHFT		20
#define CA_DEBUG_CX_VSEL		(0xfull << 24)
#define CA_DEBUG_CX_VSEL_SHFT		24
#define CA_DEBUG_CR_VSEL		(0xfull << 28)
#define CA_DEBUG_CR_VSEL_SHFT		28
#define CA_DEBUG_BA_VSEL		(0xfull << 32)
#define CA_DEBUG_BA_VSEL_SHFT		32
#define CA_DEBUG_PE_VSEL		(0xfull << 36)
#define CA_DEBUG_PE_VSEL_SHFT		36
#define CA_DEBUG_BO_VSEL		(0xfull << 40)
#define CA_DEBUG_BO_VSEL_SHFT		40
#define CA_DEBUG_BI_VSEL		(0xfull << 44)
#define CA_DEBUG_BI_VSEL_SHFT		44
#define CA_DEBUG_AS_VSEL		(0xfull << 48)
#define CA_DEBUG_AS_VSEL_SHFT		48
#define CA_DEBUG_PS_VSEL		(0xfull << 52)
#define CA_DEBUG_PS_VSEL_SHFT		52
#define CA_DEBUG_PM_VSEL		(0xfull << 56)
#define CA_DEBUG_PM_VSEL_SHFT		56
	/* bits 63:60 unused */

/* ==== ca_debug_mux_core_sel */
/* ==== ca_debug_mux_pci_sel */
#define CA_DEBUG_MSEL0			(0x7ull << 0)
#define CA_DEBUG_MSEL0_SHFT		0
	/* bit 3 unused */
#define CA_DEBUG_NSEL0			(0x7ull << 4)
#define CA_DEBUG_NSEL0_SHFT		4
	/* bit 7 unused */
#define CA_DEBUG_MSEL1			(0x7ull << 8)
#define CA_DEBUG_MSEL1_SHFT		8
	/* bit 11 unused */
#define CA_DEBUG_NSEL1			(0x7ull << 12)
#define CA_DEBUG_NSEL1_SHFT		12
	/* bit 15 unused */
#define CA_DEBUG_MSEL2			(0x7ull << 16)
#define CA_DEBUG_MSEL2_SHFT		16
	/* bit 19 unused */
#define CA_DEBUG_NSEL2			(0x7ull << 20)
#define CA_DEBUG_NSEL2_SHFT		20
	/* bit 23 unused */
#define CA_DEBUG_MSEL3			(0x7ull << 24)
#define CA_DEBUG_MSEL3_SHFT		24
	/* bit 27 unused */
#define CA_DEBUG_NSEL3			(0x7ull << 28)
#define CA_DEBUG_NSEL3_SHFT		28
	/* bit 31 unused */
#define CA_DEBUG_MSEL4			(0x7ull << 32)
#define CA_DEBUG_MSEL4_SHFT		32
	/* bit 35 unused */
#define CA_DEBUG_NSEL4			(0x7ull << 36)
#define CA_DEBUG_NSEL4_SHFT		36
	/* bit 39 unused */
#define CA_DEBUG_MSEL5			(0x7ull << 40)
#define CA_DEBUG_MSEL5_SHFT		40
	/* bit 43 unused */
#define CA_DEBUG_NSEL5			(0x7ull << 44)
#define CA_DEBUG_NSEL5_SHFT		44
	/* bit 47 unused */
#define CA_DEBUG_MSEL6			(0x7ull << 48)
#define CA_DEBUG_MSEL6_SHFT		48
	/* bit 51 unused */
#define CA_DEBUG_NSEL6			(0x7ull << 52)
#define CA_DEBUG_NSEL6_SHFT		52
	/* bit 55 unused */
#define CA_DEBUG_MSEL7			(0x7ull << 56)
#define CA_DEBUG_MSEL7_SHFT		56
	/* bit 59 unused */
#define CA_DEBUG_NSEL7			(0x7ull << 60)
#define CA_DEBUG_NSEL7_SHFT		60
	/* bit 63 unused */


/* ==== ca_debug_domain_sel */
#define CA_DEBUG_DOMAIN_L		(1ull << 0)
#define CA_DEBUG_DOMAIN_H		(1ull << 1)
	/* bits 63:2 unused */

/* ==== ca_gart_ptr_table */
#define CA_GART_PTR_VAL			(1ull << 0)
	/* bits 11:1 unused */
#define CA_GART_PTR_ADDR		(0xfffffffffffull << 12)
#define CA_GART_PTR_ADDR_SHFT		12
	/* bits 63:56 unused */

/* ==== ca_gart_tlb_addr[0-7] */
#define CA_GART_TLB_ADDR		(0xffffffffffffffull << 0)
#define CA_GART_TLB_ADDR_SHFT		0
	/* bits 62:56 unused */
#define CA_GART_TLB_ENTRY_VAL		(1ull << 63)

/*
 * PIO address space ranges for TIO:CA
 */

/* CA internal registers */
#define CA_PIO_ADMIN			0x00000000
#define CA_PIO_ADMIN_LEN		0x00010000

/* GFX Write Buffer - Diagnostics */
#define CA_PIO_GFX			0x00010000
#define CA_PIO_GFX_LEN			0x00010000

/* AGP DMA Write Buffer - Diagnostics */
#define CA_PIO_AGP_DMAWRITE		0x00020000
#define CA_PIO_AGP_DMAWRITE_LEN		0x00010000

/* AGP DMA READ Buffer - Diagnostics */
#define CA_PIO_AGP_DMAREAD		0x00030000
#define CA_PIO_AGP_DMAREAD_LEN		0x00010000

/* PCI Config Type 0 */
#define CA_PIO_PCI_TYPE0_CONFIG		0x01000000
#define CA_PIO_PCI_TYPE0_CONFIG_LEN	0x01000000

/* PCI Config Type 1 */
#define CA_PIO_PCI_TYPE1_CONFIG		0x02000000
#define CA_PIO_PCI_TYPE1_CONFIG_LEN	0x01000000

/* PCI I/O Cycles - mapped to PCI Address 0x00000000-0x04ffffff */
#define CA_PIO_PCI_IO			0x03000000
#define CA_PIO_PCI_IO_LEN		0x05000000

/* PCI MEM Cycles - mapped to PCI with CA_PIO_ADDR_OFFSET of ca_control1 */
/*	use Fast Write if enabled and coretalk packet type is a GFX request */
#define CA_PIO_PCI_MEM_OFFSET		0x08000000
#define CA_PIO_PCI_MEM_OFFSET_LEN	0x08000000

/* PCI MEM Cycles - mapped to PCI Address 0x00000000-0xbfffffff */
/*	use Fast Write if enabled and coretalk packet type is a GFX request */
#define CA_PIO_PCI_MEM			0x40000000
#define CA_PIO_PCI_MEM_LEN		0xc0000000

/*
 * DMA space
 *
 * The CA aperature (ie. bus address range) mapped by the GART is segmented into
 * two parts.  The lower portion of the aperature is used for mapping 32 bit
 * PCI addresses which are managed by the dma interfaces in this file.  The
 * upper poprtion of the aperature is used for mapping 48 bit AGP addresses.
 * The AGP portion of the aperature is managed by the agpgart_be.c driver
 * in drivers/linux/agp.  There are ca-specific hooks in that driver to
 * manipulate the gart, but management of the AGP portion of the aperature
 * is the responsibility of that driver.
 *
 * CA allows three main types of DMA mapping:
 *
 * PCI 64-bit	Managed by this driver
 * PCI 32-bit 	Managed by this driver
 * AGP 48-bit	Managed by hooks in the /dev/agpgart driver
 *
 * All of the above can optionally be remapped through the GART.  The following
 * table lists the combinations of addressing types and GART remapping that
 * is currently supported by the driver (h/w supports all, s/w limits this):
 *
 *		PCI64		PCI32		AGP48
 * GART		no		yes		yes
 * Direct	yes		yes		no
 *
 * GART remapping of PCI64 is not done because there is no need to.  The
 * 64 bit PCI address holds all of the information necessary to target any
 * memory in the system.
 *
 * AGP48 is always mapped through the GART.  Management of the AGP48 portion
 * of the aperature is the responsibility of code in the agpgart_be driver.
 *
 * The non-64 bit bus address space will currently be partitioned like this:
 *
 *	0xffff_ffff_ffff	+--------
 *				| AGP48 direct
 *				| Space managed by this driver
 *	CA_AGP_DIRECT_BASE	+--------
 *				| AGP GART mapped (gfx aperature)
 *				| Space managed by /dev/agpgart driver
 *				| This range is exposed to the agpgart
 * 				| driver as the "graphics aperature"
 *	CA_AGP_MAPPED_BASE	+-----
 *				| PCI GART mapped
 *				| Space managed by this driver		
 *	CA_PCI32_MAPPED_BASE	+----
 *				| PCI32 direct
 *				| Space managed by this driver
 *	0xC000_0000		+--------
 *	(CA_PCI32_DIRECT_BASE)
 *
 * The bus address range CA_PCI32_MAPPED_BASE through CA_AGP_DIRECT_BASE
 * is what we call the CA aperature.  Addresses falling in this range will
 * be remapped using the GART.
 *
 * The bus address range CA_AGP_MAPPED_BASE through CA_AGP_DIRECT_BASE
 * is what we call the graphics aperature.  This is a subset of the CA
 * aperature and is under the control of the agpgart_be driver.
 *
 * CA_PCI32_MAPPED_BASE, CA_AGP_MAPPED_BASE, and CA_AGP_DIRECT_BASE are
 * somewhat arbitrary values.  The known constraints on choosing these is:
 *
 * 1)  CA_AGP_DIRECT_BASE-CA_PCI32_MAPPED_BASE+1 (the CA aperature size)
 *     must be one of the values supported by the ca_gart_aperature register.
 *     Currently valid values are: 4MB through 4096MB in powers of 2 increments
 *
 * 2)  CA_AGP_DIRECT_BASE-CA_AGP_MAPPED_BASE+1 (the gfx aperature size)
 *     must be in MB units since that's what the agpgart driver assumes.
 */

/*
 * Define Bus DMA ranges.  These are configurable (see constraints above)
 * and will probably need tuning based on experience.
 */


/*
 * 11/24/03
 * CA has an addressing glitch w.r.t. PCI direct 32 bit DMA that makes it
 * generally unusable.  The problem is that for PCI direct 32 
 * DMA's, all 32 bits of the bus address are used to form the lower 32 bits
 * of the coretalk address, and coretalk bits 38:32 come from a register.
 * Since only PCI bus addresses 0xC0000000-0xFFFFFFFF (1GB) are available
 * for DMA (the rest is allocated to PIO), host node addresses need to be
 * such that their lower 32 bits fall in the 0xC0000000-0xffffffff range
 * as well.  So there can be no PCI32 direct DMA below 3GB!!  For this
 * reason we set the CA_PCI32_DIRECT_SIZE to 0 which essentially makes
 * tioca_dma_direct32() a noop but preserves the code flow should this issue
 * be fixed in a respin.
 *
 * For now, all PCI32 DMA's must be mapped through the GART.
 */

#define CA_PCI32_DIRECT_BASE	0xC0000000UL	/* BASE not configurable */
#define CA_PCI32_DIRECT_SIZE	0x00000000UL	/* 0 MB */

#define CA_PCI32_MAPPED_BASE	0xC0000000UL
#define CA_PCI32_MAPPED_SIZE	0x40000000UL	/* 2GB */

#define CA_AGP_MAPPED_BASE	0x80000000UL
#define CA_AGP_MAPPED_SIZE	0x40000000UL	/* 2GB */

#define CA_AGP_DIRECT_BASE	0x40000000UL	/* 2GB */
#define CA_AGP_DIRECT_SIZE	0x40000000UL

#define CA_APERATURE_BASE	(CA_AGP_MAPPED_BASE)
#define CA_APERATURE_SIZE	(CA_AGP_MAPPED_SIZE+CA_PCI32_MAPPED_SIZE)

#endif  /* _ASM_IA64_SN_TIO_TIOCA_H */
