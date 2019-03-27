/*-
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD
 *
 * Copyright (c) 2007-2016 Solarflare Communications Inc.
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are met:
 *
 * 1. Redistributions of source code must retain the above copyright notice,
 *    this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright notice,
 *    this list of conditions and the following disclaimer in the documentation
 *    and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
 * AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO,
 * THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR
 * PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT OWNER OR
 * CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL,
 * EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO,
 * PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS;
 * OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY,
 * WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR
 * OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE,
 * EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 *
 * The views and conclusions contained in the software and documentation are
 * those of the authors and should not be interpreted as representing official
 * policies, either expressed or implied, of the FreeBSD Project.
 *
 * $FreeBSD$
 */

#ifndef	_SYS_EFX_REGS_PCI_H
#define	_SYS_EFX_REGS_PCI_H

#ifdef	__cplusplus
extern "C" {
#endif

/*
 * PC_VEND_ID_REG(16bit):
 * Vendor ID register
 */

#define	PCR_AZ_VEND_ID_REG 0x00000000
/* falcona0,falconb0,sienaa0,hunta0=pci_f0_config */

#define	PCRF_AZ_VEND_ID_LBN 0
#define	PCRF_AZ_VEND_ID_WIDTH 16


/*
 * PC_DEV_ID_REG(16bit):
 * Device ID register
 */

#define	PCR_AZ_DEV_ID_REG 0x00000002
/* falcona0,falconb0,sienaa0,hunta0=pci_f0_config */

#define	PCRF_AZ_DEV_ID_LBN 0
#define	PCRF_AZ_DEV_ID_WIDTH 16


/*
 * PC_CMD_REG(16bit):
 * Command register
 */

#define	PCR_AZ_CMD_REG 0x00000004
/* falcona0,falconb0,sienaa0,hunta0=pci_f0_config */

#define	PCRF_AZ_INTX_DIS_LBN 10
#define	PCRF_AZ_INTX_DIS_WIDTH 1
#define	PCRF_AZ_FB2B_EN_LBN 9
#define	PCRF_AZ_FB2B_EN_WIDTH 1
#define	PCRF_AZ_SERR_EN_LBN 8
#define	PCRF_AZ_SERR_EN_WIDTH 1
#define	PCRF_AZ_IDSEL_CTL_LBN 7
#define	PCRF_AZ_IDSEL_CTL_WIDTH 1
#define	PCRF_AZ_PERR_EN_LBN 6
#define	PCRF_AZ_PERR_EN_WIDTH 1
#define	PCRF_AZ_VGA_PAL_SNP_LBN 5
#define	PCRF_AZ_VGA_PAL_SNP_WIDTH 1
#define	PCRF_AZ_MWI_EN_LBN 4
#define	PCRF_AZ_MWI_EN_WIDTH 1
#define	PCRF_AZ_SPEC_CYC_LBN 3
#define	PCRF_AZ_SPEC_CYC_WIDTH 1
#define	PCRF_AZ_MST_EN_LBN 2
#define	PCRF_AZ_MST_EN_WIDTH 1
#define	PCRF_AZ_MEM_EN_LBN 1
#define	PCRF_AZ_MEM_EN_WIDTH 1
#define	PCRF_AZ_IO_EN_LBN 0
#define	PCRF_AZ_IO_EN_WIDTH 1


/*
 * PC_STAT_REG(16bit):
 * Status register
 */

#define	PCR_AZ_STAT_REG 0x00000006
/* falcona0,falconb0,sienaa0,hunta0=pci_f0_config */

#define	PCRF_AZ_DET_PERR_LBN 15
#define	PCRF_AZ_DET_PERR_WIDTH 1
#define	PCRF_AZ_SIG_SERR_LBN 14
#define	PCRF_AZ_SIG_SERR_WIDTH 1
#define	PCRF_AZ_GOT_MABRT_LBN 13
#define	PCRF_AZ_GOT_MABRT_WIDTH 1
#define	PCRF_AZ_GOT_TABRT_LBN 12
#define	PCRF_AZ_GOT_TABRT_WIDTH 1
#define	PCRF_AZ_SIG_TABRT_LBN 11
#define	PCRF_AZ_SIG_TABRT_WIDTH 1
#define	PCRF_AZ_DEVSEL_TIM_LBN 9
#define	PCRF_AZ_DEVSEL_TIM_WIDTH 2
#define	PCRF_AZ_MDAT_PERR_LBN 8
#define	PCRF_AZ_MDAT_PERR_WIDTH 1
#define	PCRF_AZ_FB2B_CAP_LBN 7
#define	PCRF_AZ_FB2B_CAP_WIDTH 1
#define	PCRF_AZ_66MHZ_CAP_LBN 5
#define	PCRF_AZ_66MHZ_CAP_WIDTH 1
#define	PCRF_AZ_CAP_LIST_LBN 4
#define	PCRF_AZ_CAP_LIST_WIDTH 1
#define	PCRF_AZ_INTX_STAT_LBN 3
#define	PCRF_AZ_INTX_STAT_WIDTH 1


/*
 * PC_REV_ID_REG(8bit):
 * Class code & revision ID register
 */

#define	PCR_AZ_REV_ID_REG 0x00000008
/* falcona0,falconb0,sienaa0,hunta0=pci_f0_config */

#define	PCRF_AZ_REV_ID_LBN 0
#define	PCRF_AZ_REV_ID_WIDTH 8


/*
 * PC_CC_REG(24bit):
 * Class code register
 */

#define	PCR_AZ_CC_REG 0x00000009
/* falcona0,falconb0,sienaa0,hunta0=pci_f0_config */

#define	PCRF_AZ_BASE_CC_LBN 16
#define	PCRF_AZ_BASE_CC_WIDTH 8
#define	PCRF_AZ_SUB_CC_LBN 8
#define	PCRF_AZ_SUB_CC_WIDTH 8
#define	PCRF_AZ_PROG_IF_LBN 0
#define	PCRF_AZ_PROG_IF_WIDTH 8


/*
 * PC_CACHE_LSIZE_REG(8bit):
 * Cache line size
 */

#define	PCR_AZ_CACHE_LSIZE_REG 0x0000000c
/* falcona0,falconb0,sienaa0,hunta0=pci_f0_config */

#define	PCRF_AZ_CACHE_LSIZE_LBN 0
#define	PCRF_AZ_CACHE_LSIZE_WIDTH 8


/*
 * PC_MST_LAT_REG(8bit):
 * Master latency timer register
 */

#define	PCR_AZ_MST_LAT_REG 0x0000000d
/* falcona0,falconb0,sienaa0,hunta0=pci_f0_config */

#define	PCRF_AZ_MST_LAT_LBN 0
#define	PCRF_AZ_MST_LAT_WIDTH 8


/*
 * PC_HDR_TYPE_REG(8bit):
 * Header type register
 */

#define	PCR_AZ_HDR_TYPE_REG 0x0000000e
/* falcona0,falconb0,sienaa0,hunta0=pci_f0_config */

#define	PCRF_AZ_MULT_FUNC_LBN 7
#define	PCRF_AZ_MULT_FUNC_WIDTH 1
#define	PCRF_AZ_TYPE_LBN 0
#define	PCRF_AZ_TYPE_WIDTH 7


/*
 * PC_BIST_REG(8bit):
 * BIST register
 */

#define	PCR_AZ_BIST_REG 0x0000000f
/* falcona0,falconb0,sienaa0,hunta0=pci_f0_config */

#define	PCRF_AZ_BIST_LBN 0
#define	PCRF_AZ_BIST_WIDTH 8


/*
 * PC_BAR0_REG(32bit):
 * Primary function base address register 0
 */

#define	PCR_AZ_BAR0_REG 0x00000010
/* falcona0,falconb0,sienaa0,hunta0=pci_f0_config */

#define	PCRF_AZ_BAR0_LBN 4
#define	PCRF_AZ_BAR0_WIDTH 28
#define	PCRF_AZ_BAR0_PREF_LBN 3
#define	PCRF_AZ_BAR0_PREF_WIDTH 1
#define	PCRF_AZ_BAR0_TYPE_LBN 1
#define	PCRF_AZ_BAR0_TYPE_WIDTH 2
#define	PCRF_AZ_BAR0_IOM_LBN 0
#define	PCRF_AZ_BAR0_IOM_WIDTH 1


/*
 * PC_BAR1_REG(32bit):
 * Primary function base address register 1, BAR1 is not implemented so read only.
 */

#define	PCR_DZ_BAR1_REG 0x00000014
/* hunta0=pci_f0_config */

#define	PCRF_DZ_BAR1_LBN 0
#define	PCRF_DZ_BAR1_WIDTH 32


/*
 * PC_BAR2_LO_REG(32bit):
 * Primary function base address register 2 low bits
 */

#define	PCR_AZ_BAR2_LO_REG 0x00000018
/* falcona0,falconb0,sienaa0,hunta0=pci_f0_config */

#define	PCRF_AZ_BAR2_LO_LBN 4
#define	PCRF_AZ_BAR2_LO_WIDTH 28
#define	PCRF_AZ_BAR2_PREF_LBN 3
#define	PCRF_AZ_BAR2_PREF_WIDTH 1
#define	PCRF_AZ_BAR2_TYPE_LBN 1
#define	PCRF_AZ_BAR2_TYPE_WIDTH 2
#define	PCRF_AZ_BAR2_IOM_LBN 0
#define	PCRF_AZ_BAR2_IOM_WIDTH 1


/*
 * PC_BAR2_HI_REG(32bit):
 * Primary function base address register 2 high bits
 */

#define	PCR_AZ_BAR2_HI_REG 0x0000001c
/* falcona0,falconb0,sienaa0,hunta0=pci_f0_config */

#define	PCRF_AZ_BAR2_HI_LBN 0
#define	PCRF_AZ_BAR2_HI_WIDTH 32


/*
 * PC_BAR4_LO_REG(32bit):
 * Primary function base address register 2 low bits
 */

#define	PCR_CZ_BAR4_LO_REG 0x00000020
/* sienaa0,hunta0=pci_f0_config */

#define	PCRF_CZ_BAR4_LO_LBN 4
#define	PCRF_CZ_BAR4_LO_WIDTH 28
#define	PCRF_CZ_BAR4_PREF_LBN 3
#define	PCRF_CZ_BAR4_PREF_WIDTH 1
#define	PCRF_CZ_BAR4_TYPE_LBN 1
#define	PCRF_CZ_BAR4_TYPE_WIDTH 2
#define	PCRF_CZ_BAR4_IOM_LBN 0
#define	PCRF_CZ_BAR4_IOM_WIDTH 1


/*
 * PC_BAR4_HI_REG(32bit):
 * Primary function base address register 2 high bits
 */

#define	PCR_CZ_BAR4_HI_REG 0x00000024
/* sienaa0,hunta0=pci_f0_config */

#define	PCRF_CZ_BAR4_HI_LBN 0
#define	PCRF_CZ_BAR4_HI_WIDTH 32


/*
 * PC_SS_VEND_ID_REG(16bit):
 * Sub-system vendor ID register
 */

#define	PCR_AZ_SS_VEND_ID_REG 0x0000002c
/* falcona0,falconb0,sienaa0,hunta0=pci_f0_config */

#define	PCRF_AZ_SS_VEND_ID_LBN 0
#define	PCRF_AZ_SS_VEND_ID_WIDTH 16


/*
 * PC_SS_ID_REG(16bit):
 * Sub-system ID register
 */

#define	PCR_AZ_SS_ID_REG 0x0000002e
/* falcona0,falconb0,sienaa0,hunta0=pci_f0_config */

#define	PCRF_AZ_SS_ID_LBN 0
#define	PCRF_AZ_SS_ID_WIDTH 16


/*
 * PC_EXPROM_BAR_REG(32bit):
 * Expansion ROM base address register
 */

#define	PCR_AZ_EXPROM_BAR_REG 0x00000030
/* falcona0,falconb0,sienaa0,hunta0=pci_f0_config */

#define	PCRF_AZ_EXPROM_BAR_LBN 11
#define	PCRF_AZ_EXPROM_BAR_WIDTH 21
#define	PCRF_AB_EXPROM_MIN_SIZE_LBN 2
#define	PCRF_AB_EXPROM_MIN_SIZE_WIDTH 9
#define	PCRF_CZ_EXPROM_MIN_SIZE_LBN 1
#define	PCRF_CZ_EXPROM_MIN_SIZE_WIDTH 10
#define	PCRF_AB_EXPROM_FEATURE_ENABLE_LBN 1
#define	PCRF_AB_EXPROM_FEATURE_ENABLE_WIDTH 1
#define	PCRF_AZ_EXPROM_EN_LBN 0
#define	PCRF_AZ_EXPROM_EN_WIDTH 1


/*
 * PC_CAP_PTR_REG(8bit):
 * Capability pointer register
 */

#define	PCR_AZ_CAP_PTR_REG 0x00000034
/* falcona0,falconb0,sienaa0,hunta0=pci_f0_config */

#define	PCRF_AZ_CAP_PTR_LBN 0
#define	PCRF_AZ_CAP_PTR_WIDTH 8


/*
 * PC_INT_LINE_REG(8bit):
 * Interrupt line register
 */

#define	PCR_AZ_INT_LINE_REG 0x0000003c
/* falcona0,falconb0,sienaa0,hunta0=pci_f0_config */

#define	PCRF_AZ_INT_LINE_LBN 0
#define	PCRF_AZ_INT_LINE_WIDTH 8


/*
 * PC_INT_PIN_REG(8bit):
 * Interrupt pin register
 */

#define	PCR_AZ_INT_PIN_REG 0x0000003d
/* falcona0,falconb0,sienaa0,hunta0=pci_f0_config */

#define	PCRF_AZ_INT_PIN_LBN 0
#define	PCRF_AZ_INT_PIN_WIDTH 8
#define	PCFE_DZ_INTPIN_INTD 4
#define	PCFE_DZ_INTPIN_INTC 3
#define	PCFE_DZ_INTPIN_INTB 2
#define	PCFE_DZ_INTPIN_INTA 1


/*
 * PC_PM_CAP_ID_REG(8bit):
 * Power management capability ID
 */

#define	PCR_AZ_PM_CAP_ID_REG 0x00000040
/* falcona0,falconb0,sienaa0,hunta0=pci_f0_config */

#define	PCRF_AZ_PM_CAP_ID_LBN 0
#define	PCRF_AZ_PM_CAP_ID_WIDTH 8


/*
 * PC_PM_NXT_PTR_REG(8bit):
 * Power management next item pointer
 */

#define	PCR_AZ_PM_NXT_PTR_REG 0x00000041
/* falcona0,falconb0,sienaa0,hunta0=pci_f0_config */

#define	PCRF_AZ_PM_NXT_PTR_LBN 0
#define	PCRF_AZ_PM_NXT_PTR_WIDTH 8


/*
 * PC_PM_CAP_REG(16bit):
 * Power management capabilities register
 */

#define	PCR_AZ_PM_CAP_REG 0x00000042
/* falcona0,falconb0,sienaa0,hunta0=pci_f0_config */

#define	PCRF_AZ_PM_PME_SUPT_LBN 11
#define	PCRF_AZ_PM_PME_SUPT_WIDTH 5
#define	PCRF_AZ_PM_D2_SUPT_LBN 10
#define	PCRF_AZ_PM_D2_SUPT_WIDTH 1
#define	PCRF_AZ_PM_D1_SUPT_LBN 9
#define	PCRF_AZ_PM_D1_SUPT_WIDTH 1
#define	PCRF_AZ_PM_AUX_CURR_LBN 6
#define	PCRF_AZ_PM_AUX_CURR_WIDTH 3
#define	PCRF_AZ_PM_DSI_LBN 5
#define	PCRF_AZ_PM_DSI_WIDTH 1
#define	PCRF_AZ_PM_PME_CLK_LBN 3
#define	PCRF_AZ_PM_PME_CLK_WIDTH 1
#define	PCRF_AZ_PM_PME_VER_LBN 0
#define	PCRF_AZ_PM_PME_VER_WIDTH 3


/*
 * PC_PM_CS_REG(16bit):
 * Power management control & status register
 */

#define	PCR_AZ_PM_CS_REG 0x00000044
/* falcona0,falconb0,sienaa0,hunta0=pci_f0_config */

#define	PCRF_AZ_PM_PME_STAT_LBN 15
#define	PCRF_AZ_PM_PME_STAT_WIDTH 1
#define	PCRF_AZ_PM_DAT_SCALE_LBN 13
#define	PCRF_AZ_PM_DAT_SCALE_WIDTH 2
#define	PCRF_AZ_PM_DAT_SEL_LBN 9
#define	PCRF_AZ_PM_DAT_SEL_WIDTH 4
#define	PCRF_AZ_PM_PME_EN_LBN 8
#define	PCRF_AZ_PM_PME_EN_WIDTH 1
#define	PCRF_CZ_NO_SOFT_RESET_LBN 3
#define	PCRF_CZ_NO_SOFT_RESET_WIDTH 1
#define	PCRF_AZ_PM_PWR_ST_LBN 0
#define	PCRF_AZ_PM_PWR_ST_WIDTH 2


/*
 * PC_MSI_CAP_ID_REG(8bit):
 * MSI capability ID
 */

#define	PCR_AZ_MSI_CAP_ID_REG 0x00000050
/* falcona0,falconb0,sienaa0,hunta0=pci_f0_config */

#define	PCRF_AZ_MSI_CAP_ID_LBN 0
#define	PCRF_AZ_MSI_CAP_ID_WIDTH 8


/*
 * PC_MSI_NXT_PTR_REG(8bit):
 * MSI next item pointer
 */

#define	PCR_AZ_MSI_NXT_PTR_REG 0x00000051
/* falcona0,falconb0,sienaa0,hunta0=pci_f0_config */

#define	PCRF_AZ_MSI_NXT_PTR_LBN 0
#define	PCRF_AZ_MSI_NXT_PTR_WIDTH 8


/*
 * PC_MSI_CTL_REG(16bit):
 * MSI control register
 */

#define	PCR_AZ_MSI_CTL_REG 0x00000052
/* falcona0,falconb0,sienaa0,hunta0=pci_f0_config */

#define	PCRF_AZ_MSI_64_EN_LBN 7
#define	PCRF_AZ_MSI_64_EN_WIDTH 1
#define	PCRF_AZ_MSI_MULT_MSG_EN_LBN 4
#define	PCRF_AZ_MSI_MULT_MSG_EN_WIDTH 3
#define	PCRF_AZ_MSI_MULT_MSG_CAP_LBN 1
#define	PCRF_AZ_MSI_MULT_MSG_CAP_WIDTH 3
#define	PCRF_AZ_MSI_EN_LBN 0
#define	PCRF_AZ_MSI_EN_WIDTH 1


/*
 * PC_MSI_ADR_LO_REG(32bit):
 * MSI low 32 bits address register
 */

#define	PCR_AZ_MSI_ADR_LO_REG 0x00000054
/* falcona0,falconb0,sienaa0,hunta0=pci_f0_config */

#define	PCRF_AZ_MSI_ADR_LO_LBN 2
#define	PCRF_AZ_MSI_ADR_LO_WIDTH 30


/*
 * PC_MSI_ADR_HI_REG(32bit):
 * MSI high 32 bits address register
 */

#define	PCR_AZ_MSI_ADR_HI_REG 0x00000058
/* falcona0,falconb0,sienaa0,hunta0=pci_f0_config */

#define	PCRF_AZ_MSI_ADR_HI_LBN 0
#define	PCRF_AZ_MSI_ADR_HI_WIDTH 32


/*
 * PC_MSI_DAT_REG(16bit):
 * MSI data register
 */

#define	PCR_AZ_MSI_DAT_REG 0x0000005c
/* falcona0,falconb0,sienaa0,hunta0=pci_f0_config */

#define	PCRF_AZ_MSI_DAT_LBN 0
#define	PCRF_AZ_MSI_DAT_WIDTH 16


/*
 * PC_PCIE_CAP_LIST_REG(16bit):
 * PCIe capability list register
 */

#define	PCR_AB_PCIE_CAP_LIST_REG 0x00000060
/* falcona0,falconb0=pci_f0_config */

#define	PCR_CZ_PCIE_CAP_LIST_REG 0x00000070
/* sienaa0,hunta0=pci_f0_config */

#define	PCRF_AZ_PCIE_NXT_PTR_LBN 8
#define	PCRF_AZ_PCIE_NXT_PTR_WIDTH 8
#define	PCRF_AZ_PCIE_CAP_ID_LBN 0
#define	PCRF_AZ_PCIE_CAP_ID_WIDTH 8


/*
 * PC_PCIE_CAP_REG(16bit):
 * PCIe capability register
 */

#define	PCR_AB_PCIE_CAP_REG 0x00000062
/* falcona0,falconb0=pci_f0_config */

#define	PCR_CZ_PCIE_CAP_REG 0x00000072
/* sienaa0,hunta0=pci_f0_config */

#define	PCRF_AZ_PCIE_INT_MSG_NUM_LBN 9
#define	PCRF_AZ_PCIE_INT_MSG_NUM_WIDTH 5
#define	PCRF_AZ_PCIE_SLOT_IMP_LBN 8
#define	PCRF_AZ_PCIE_SLOT_IMP_WIDTH 1
#define	PCRF_AZ_PCIE_DEV_PORT_TYPE_LBN 4
#define	PCRF_AZ_PCIE_DEV_PORT_TYPE_WIDTH 4
#define	PCRF_AZ_PCIE_CAP_VER_LBN 0
#define	PCRF_AZ_PCIE_CAP_VER_WIDTH 4


/*
 * PC_DEV_CAP_REG(32bit):
 * PCIe device capabilities register
 */

#define	PCR_AB_DEV_CAP_REG 0x00000064
/* falcona0,falconb0=pci_f0_config */

#define	PCR_CZ_DEV_CAP_REG 0x00000074
/* sienaa0=pci_f0_config,hunta0=pci_f0_config */

#define	PCRF_CZ_CAP_FN_LEVEL_RESET_LBN 28
#define	PCRF_CZ_CAP_FN_LEVEL_RESET_WIDTH 1
#define	PCRF_AZ_CAP_SLOT_PWR_SCL_LBN 26
#define	PCRF_AZ_CAP_SLOT_PWR_SCL_WIDTH 2
#define	PCRF_AZ_CAP_SLOT_PWR_VAL_LBN 18
#define	PCRF_AZ_CAP_SLOT_PWR_VAL_WIDTH 8
#define	PCRF_CZ_ROLE_BASE_ERR_REPORTING_LBN 15
#define	PCRF_CZ_ROLE_BASE_ERR_REPORTING_WIDTH 1
#define	PCRF_AB_PWR_IND_LBN 14
#define	PCRF_AB_PWR_IND_WIDTH 1
#define	PCRF_AB_ATTN_IND_LBN 13
#define	PCRF_AB_ATTN_IND_WIDTH 1
#define	PCRF_AB_ATTN_BUTTON_LBN 12
#define	PCRF_AB_ATTN_BUTTON_WIDTH 1
#define	PCRF_AZ_ENDPT_L1_LAT_LBN 9
#define	PCRF_AZ_ENDPT_L1_LAT_WIDTH 3
#define	PCRF_AZ_ENDPT_L0_LAT_LBN 6
#define	PCRF_AZ_ENDPT_L0_LAT_WIDTH 3
#define	PCRF_AZ_TAG_FIELD_LBN 5
#define	PCRF_AZ_TAG_FIELD_WIDTH 1
#define	PCRF_AZ_PHAN_FUNC_LBN 3
#define	PCRF_AZ_PHAN_FUNC_WIDTH 2
#define	PCRF_AZ_MAX_PAYL_SIZE_SUPT_LBN 0
#define	PCRF_AZ_MAX_PAYL_SIZE_SUPT_WIDTH 3


/*
 * PC_DEV_CTL_REG(16bit):
 * PCIe device control register
 */

#define	PCR_AB_DEV_CTL_REG 0x00000068
/* falcona0,falconb0=pci_f0_config */

#define	PCR_CZ_DEV_CTL_REG 0x00000078
/* sienaa0,hunta0=pci_f0_config */

#define	PCRF_CZ_FN_LEVEL_RESET_LBN 15
#define	PCRF_CZ_FN_LEVEL_RESET_WIDTH 1
#define	PCRF_AZ_MAX_RD_REQ_SIZE_LBN 12
#define	PCRF_AZ_MAX_RD_REQ_SIZE_WIDTH 3
#define	PCFE_AZ_MAX_RD_REQ_SIZE_4096 5
#define	PCFE_AZ_MAX_RD_REQ_SIZE_2048 4
#define	PCFE_AZ_MAX_RD_REQ_SIZE_1024 3
#define	PCFE_AZ_MAX_RD_REQ_SIZE_512 2
#define	PCFE_AZ_MAX_RD_REQ_SIZE_256 1
#define	PCFE_AZ_MAX_RD_REQ_SIZE_128 0
#define	PCRF_AZ_EN_NO_SNOOP_LBN 11
#define	PCRF_AZ_EN_NO_SNOOP_WIDTH 1
#define	PCRF_AZ_AUX_PWR_PM_EN_LBN 10
#define	PCRF_AZ_AUX_PWR_PM_EN_WIDTH 1
#define	PCRF_AZ_PHAN_FUNC_EN_LBN 9
#define	PCRF_AZ_PHAN_FUNC_EN_WIDTH 1
#define	PCRF_AB_DEV_CAP_REG_RSVD0_LBN 8
#define	PCRF_AB_DEV_CAP_REG_RSVD0_WIDTH 1
#define	PCRF_CZ_EXTENDED_TAG_EN_LBN 8
#define	PCRF_CZ_EXTENDED_TAG_EN_WIDTH 1
#define	PCRF_AZ_MAX_PAYL_SIZE_LBN 5
#define	PCRF_AZ_MAX_PAYL_SIZE_WIDTH 3
#define	PCFE_AZ_MAX_PAYL_SIZE_4096 5
#define	PCFE_AZ_MAX_PAYL_SIZE_2048 4
#define	PCFE_AZ_MAX_PAYL_SIZE_1024 3
#define	PCFE_AZ_MAX_PAYL_SIZE_512 2
#define	PCFE_AZ_MAX_PAYL_SIZE_256 1
#define	PCFE_AZ_MAX_PAYL_SIZE_128 0
#define	PCRF_AZ_EN_RELAX_ORDER_LBN 4
#define	PCRF_AZ_EN_RELAX_ORDER_WIDTH 1
#define	PCRF_AZ_UNSUP_REQ_RPT_EN_LBN 3
#define	PCRF_AZ_UNSUP_REQ_RPT_EN_WIDTH 1
#define	PCRF_AZ_FATAL_ERR_RPT_EN_LBN 2
#define	PCRF_AZ_FATAL_ERR_RPT_EN_WIDTH 1
#define	PCRF_AZ_NONFATAL_ERR_RPT_EN_LBN 1
#define	PCRF_AZ_NONFATAL_ERR_RPT_EN_WIDTH 1
#define	PCRF_AZ_CORR_ERR_RPT_EN_LBN 0
#define	PCRF_AZ_CORR_ERR_RPT_EN_WIDTH 1


/*
 * PC_DEV_STAT_REG(16bit):
 * PCIe device status register
 */

#define	PCR_AB_DEV_STAT_REG 0x0000006a
/* falcona0,falconb0=pci_f0_config */

#define	PCR_CZ_DEV_STAT_REG 0x0000007a
/* sienaa0,hunta0=pci_f0_config */

#define	PCRF_AZ_TRNS_PEND_LBN 5
#define	PCRF_AZ_TRNS_PEND_WIDTH 1
#define	PCRF_AZ_AUX_PWR_DET_LBN 4
#define	PCRF_AZ_AUX_PWR_DET_WIDTH 1
#define	PCRF_AZ_UNSUP_REQ_DET_LBN 3
#define	PCRF_AZ_UNSUP_REQ_DET_WIDTH 1
#define	PCRF_AZ_FATAL_ERR_DET_LBN 2
#define	PCRF_AZ_FATAL_ERR_DET_WIDTH 1
#define	PCRF_AZ_NONFATAL_ERR_DET_LBN 1
#define	PCRF_AZ_NONFATAL_ERR_DET_WIDTH 1
#define	PCRF_AZ_CORR_ERR_DET_LBN 0
#define	PCRF_AZ_CORR_ERR_DET_WIDTH 1


/*
 * PC_LNK_CAP_REG(32bit):
 * PCIe link capabilities register
 */

#define	PCR_AB_LNK_CAP_REG 0x0000006c
/* falcona0,falconb0=pci_f0_config */

#define	PCR_CZ_LNK_CAP_REG 0x0000007c
/* sienaa0,hunta0=pci_f0_config */

#define	PCRF_AZ_PORT_NUM_LBN 24
#define	PCRF_AZ_PORT_NUM_WIDTH 8
#define	PCRF_DZ_ASPM_OPTIONALITY_CAP_LBN 22
#define	PCRF_DZ_ASPM_OPTIONALITY_CAP_WIDTH 1
#define	PCRF_CZ_LINK_BWDITH_NOTIF_CAP_LBN 21
#define	PCRF_CZ_LINK_BWDITH_NOTIF_CAP_WIDTH 1
#define	PCRF_CZ_DATA_LINK_ACTIVE_RPT_CAP_LBN 20
#define	PCRF_CZ_DATA_LINK_ACTIVE_RPT_CAP_WIDTH 1
#define	PCRF_CZ_SURPISE_DOWN_RPT_CAP_LBN 19
#define	PCRF_CZ_SURPISE_DOWN_RPT_CAP_WIDTH 1
#define	PCRF_CZ_CLOCK_PWR_MNGMNT_CAP_LBN 18
#define	PCRF_CZ_CLOCK_PWR_MNGMNT_CAP_WIDTH 1
#define	PCRF_AZ_DEF_L1_EXIT_LAT_LBN 15
#define	PCRF_AZ_DEF_L1_EXIT_LAT_WIDTH 3
#define	PCRF_AZ_DEF_L0_EXIT_LATPORT_NUM_LBN 12
#define	PCRF_AZ_DEF_L0_EXIT_LATPORT_NUM_WIDTH 3
#define	PCRF_AZ_AS_LNK_PM_SUPT_LBN 10
#define	PCRF_AZ_AS_LNK_PM_SUPT_WIDTH 2
#define	PCRF_AZ_MAX_LNK_WIDTH_LBN 4
#define	PCRF_AZ_MAX_LNK_WIDTH_WIDTH 6
#define	PCRF_AZ_MAX_LNK_SP_LBN 0
#define	PCRF_AZ_MAX_LNK_SP_WIDTH 4


/*
 * PC_LNK_CTL_REG(16bit):
 * PCIe link control register
 */

#define	PCR_AB_LNK_CTL_REG 0x00000070
/* falcona0,falconb0=pci_f0_config */

#define	PCR_CZ_LNK_CTL_REG 0x00000080
/* sienaa0,hunta0=pci_f0_config */

#define	PCRF_AZ_EXT_SYNC_LBN 7
#define	PCRF_AZ_EXT_SYNC_WIDTH 1
#define	PCRF_AZ_COMM_CLK_CFG_LBN 6
#define	PCRF_AZ_COMM_CLK_CFG_WIDTH 1
#define	PCRF_AB_LNK_CTL_REG_RSVD0_LBN 5
#define	PCRF_AB_LNK_CTL_REG_RSVD0_WIDTH 1
#define	PCRF_CZ_LNK_RETRAIN_LBN 5
#define	PCRF_CZ_LNK_RETRAIN_WIDTH 1
#define	PCRF_AZ_LNK_DIS_LBN 4
#define	PCRF_AZ_LNK_DIS_WIDTH 1
#define	PCRF_AZ_RD_COM_BDRY_LBN 3
#define	PCRF_AZ_RD_COM_BDRY_WIDTH 1
#define	PCRF_AZ_ACT_ST_LNK_PM_CTL_LBN 0
#define	PCRF_AZ_ACT_ST_LNK_PM_CTL_WIDTH 2


/*
 * PC_LNK_STAT_REG(16bit):
 * PCIe link status register
 */

#define	PCR_AB_LNK_STAT_REG 0x00000072
/* falcona0,falconb0=pci_f0_config */

#define	PCR_CZ_LNK_STAT_REG 0x00000082
/* sienaa0,hunta0=pci_f0_config */

#define	PCRF_AZ_SLOT_CLK_CFG_LBN 12
#define	PCRF_AZ_SLOT_CLK_CFG_WIDTH 1
#define	PCRF_AZ_LNK_TRAIN_LBN 11
#define	PCRF_AZ_LNK_TRAIN_WIDTH 1
#define	PCRF_AB_TRAIN_ERR_LBN 10
#define	PCRF_AB_TRAIN_ERR_WIDTH 1
#define	PCRF_AZ_LNK_WIDTH_LBN 4
#define	PCRF_AZ_LNK_WIDTH_WIDTH 6
#define	PCRF_AZ_LNK_SP_LBN 0
#define	PCRF_AZ_LNK_SP_WIDTH 4


/*
 * PC_SLOT_CAP_REG(32bit):
 * PCIe slot capabilities register
 */

#define	PCR_AB_SLOT_CAP_REG 0x00000074
/* falcona0,falconb0=pci_f0_config */

#define	PCRF_AB_SLOT_NUM_LBN 19
#define	PCRF_AB_SLOT_NUM_WIDTH 13
#define	PCRF_AB_SLOT_PWR_LIM_SCL_LBN 15
#define	PCRF_AB_SLOT_PWR_LIM_SCL_WIDTH 2
#define	PCRF_AB_SLOT_PWR_LIM_VAL_LBN 7
#define	PCRF_AB_SLOT_PWR_LIM_VAL_WIDTH 8
#define	PCRF_AB_SLOT_HP_CAP_LBN 6
#define	PCRF_AB_SLOT_HP_CAP_WIDTH 1
#define	PCRF_AB_SLOT_HP_SURP_LBN 5
#define	PCRF_AB_SLOT_HP_SURP_WIDTH 1
#define	PCRF_AB_SLOT_PWR_IND_PRST_LBN 4
#define	PCRF_AB_SLOT_PWR_IND_PRST_WIDTH 1
#define	PCRF_AB_SLOT_ATTN_IND_PRST_LBN 3
#define	PCRF_AB_SLOT_ATTN_IND_PRST_WIDTH 1
#define	PCRF_AB_SLOT_MRL_SENS_PRST_LBN 2
#define	PCRF_AB_SLOT_MRL_SENS_PRST_WIDTH 1
#define	PCRF_AB_SLOT_PWR_CTL_PRST_LBN 1
#define	PCRF_AB_SLOT_PWR_CTL_PRST_WIDTH 1
#define	PCRF_AB_SLOT_ATTN_BUT_PRST_LBN 0
#define	PCRF_AB_SLOT_ATTN_BUT_PRST_WIDTH 1


/*
 * PC_SLOT_CTL_REG(16bit):
 * PCIe slot control register
 */

#define	PCR_AB_SLOT_CTL_REG 0x00000078
/* falcona0,falconb0=pci_f0_config */

#define	PCRF_AB_SLOT_PWR_CTLR_CTL_LBN 10
#define	PCRF_AB_SLOT_PWR_CTLR_CTL_WIDTH 1
#define	PCRF_AB_SLOT_PWR_IND_CTL_LBN 8
#define	PCRF_AB_SLOT_PWR_IND_CTL_WIDTH 2
#define	PCRF_AB_SLOT_ATT_IND_CTL_LBN 6
#define	PCRF_AB_SLOT_ATT_IND_CTL_WIDTH 2
#define	PCRF_AB_SLOT_HP_INT_EN_LBN 5
#define	PCRF_AB_SLOT_HP_INT_EN_WIDTH 1
#define	PCRF_AB_SLOT_CMD_COMP_INT_EN_LBN 4
#define	PCRF_AB_SLOT_CMD_COMP_INT_EN_WIDTH 1
#define	PCRF_AB_SLOT_PRES_DET_CHG_EN_LBN 3
#define	PCRF_AB_SLOT_PRES_DET_CHG_EN_WIDTH 1
#define	PCRF_AB_SLOT_MRL_SENS_CHG_EN_LBN 2
#define	PCRF_AB_SLOT_MRL_SENS_CHG_EN_WIDTH 1
#define	PCRF_AB_SLOT_PWR_FLTDET_EN_LBN 1
#define	PCRF_AB_SLOT_PWR_FLTDET_EN_WIDTH 1
#define	PCRF_AB_SLOT_ATTN_BUT_EN_LBN 0
#define	PCRF_AB_SLOT_ATTN_BUT_EN_WIDTH 1


/*
 * PC_SLOT_STAT_REG(16bit):
 * PCIe slot status register
 */

#define	PCR_AB_SLOT_STAT_REG 0x0000007a
/* falcona0,falconb0=pci_f0_config */

#define	PCRF_AB_PRES_DET_ST_LBN 6
#define	PCRF_AB_PRES_DET_ST_WIDTH 1
#define	PCRF_AB_MRL_SENS_ST_LBN 5
#define	PCRF_AB_MRL_SENS_ST_WIDTH 1
#define	PCRF_AB_SLOT_PWR_IND_LBN 4
#define	PCRF_AB_SLOT_PWR_IND_WIDTH 1
#define	PCRF_AB_SLOT_ATTN_IND_LBN 3
#define	PCRF_AB_SLOT_ATTN_IND_WIDTH 1
#define	PCRF_AB_SLOT_MRL_SENS_LBN 2
#define	PCRF_AB_SLOT_MRL_SENS_WIDTH 1
#define	PCRF_AB_PWR_FLTDET_LBN 1
#define	PCRF_AB_PWR_FLTDET_WIDTH 1
#define	PCRF_AB_ATTN_BUTDET_LBN 0
#define	PCRF_AB_ATTN_BUTDET_WIDTH 1


/*
 * PC_MSIX_CAP_ID_REG(8bit):
 * MSIX Capability ID
 */

#define	PCR_BB_MSIX_CAP_ID_REG 0x00000090
/* falconb0=pci_f0_config */

#define	PCR_CZ_MSIX_CAP_ID_REG 0x000000b0
/* sienaa0,hunta0=pci_f0_config */

#define	PCRF_BZ_MSIX_CAP_ID_LBN 0
#define	PCRF_BZ_MSIX_CAP_ID_WIDTH 8


/*
 * PC_MSIX_NXT_PTR_REG(8bit):
 * MSIX Capability Next Capability Ptr
 */

#define	PCR_BB_MSIX_NXT_PTR_REG 0x00000091
/* falconb0=pci_f0_config */

#define	PCR_CZ_MSIX_NXT_PTR_REG 0x000000b1
/* sienaa0,hunta0=pci_f0_config */

#define	PCRF_BZ_MSIX_NXT_PTR_LBN 0
#define	PCRF_BZ_MSIX_NXT_PTR_WIDTH 8


/*
 * PC_MSIX_CTL_REG(16bit):
 * MSIX control register
 */

#define	PCR_BB_MSIX_CTL_REG 0x00000092
/* falconb0=pci_f0_config */

#define	PCR_CZ_MSIX_CTL_REG 0x000000b2
/* sienaa0,hunta0=pci_f0_config */

#define	PCRF_BZ_MSIX_EN_LBN 15
#define	PCRF_BZ_MSIX_EN_WIDTH 1
#define	PCRF_BZ_MSIX_FUNC_MASK_LBN 14
#define	PCRF_BZ_MSIX_FUNC_MASK_WIDTH 1
#define	PCRF_BZ_MSIX_TBL_SIZE_LBN 0
#define	PCRF_BZ_MSIX_TBL_SIZE_WIDTH 11


/*
 * PC_MSIX_TBL_BASE_REG(32bit):
 * MSIX Capability Vector Table Base
 */

#define	PCR_BB_MSIX_TBL_BASE_REG 0x00000094
/* falconb0=pci_f0_config */

#define	PCR_CZ_MSIX_TBL_BASE_REG 0x000000b4
/* sienaa0,hunta0=pci_f0_config */

#define	PCRF_BZ_MSIX_TBL_OFF_LBN 3
#define	PCRF_BZ_MSIX_TBL_OFF_WIDTH 29
#define	PCRF_BZ_MSIX_TBL_BIR_LBN 0
#define	PCRF_BZ_MSIX_TBL_BIR_WIDTH 3


/*
 * PC_DEV_CAP2_REG(32bit):
 * PCIe Device Capabilities 2
 */

#define	PCR_CZ_DEV_CAP2_REG 0x00000094
/* sienaa0=pci_f0_config,hunta0=pci_f0_config */

#define	PCRF_DZ_OBFF_SUPPORTED_LBN 18
#define	PCRF_DZ_OBFF_SUPPORTED_WIDTH 2
#define	PCRF_DZ_TPH_CMPL_SUPPORTED_LBN 12
#define	PCRF_DZ_TPH_CMPL_SUPPORTED_WIDTH 2
#define	PCRF_DZ_LTR_M_SUPPORTED_LBN 11
#define	PCRF_DZ_LTR_M_SUPPORTED_WIDTH 1
#define	PCRF_CC_CMPL_TIMEOUT_DIS_LBN 4
#define	PCRF_CC_CMPL_TIMEOUT_DIS_WIDTH 1
#define	PCRF_DZ_CMPL_TIMEOUT_DIS_SUPPORTED_LBN 4
#define	PCRF_DZ_CMPL_TIMEOUT_DIS_SUPPORTED_WIDTH 1
#define	PCRF_CZ_CMPL_TIMEOUT_LBN 0
#define	PCRF_CZ_CMPL_TIMEOUT_WIDTH 4
#define	PCFE_CZ_CMPL_TIMEOUT_17000_TO_6400MS 14
#define	PCFE_CZ_CMPL_TIMEOUT_4000_TO_1300MS 13
#define	PCFE_CZ_CMPL_TIMEOUT_1000_TO_3500MS 10
#define	PCFE_CZ_CMPL_TIMEOUT_260_TO_900MS 9
#define	PCFE_CZ_CMPL_TIMEOUT_65_TO_210MS 6
#define	PCFE_CZ_CMPL_TIMEOUT_16_TO_55MS 5
#define	PCFE_CZ_CMPL_TIMEOUT_1_TO_10MS 2
#define	PCFE_CZ_CMPL_TIMEOUT_50_TO_100US 1
#define	PCFE_CZ_CMPL_TIMEOUT_DEFAULT 0


/*
 * PC_DEV_CTL2_REG(16bit):
 * PCIe Device Control 2
 */

#define	PCR_CZ_DEV_CTL2_REG 0x00000098
/* sienaa0,hunta0=pci_f0_config */

#define	PCRF_DZ_OBFF_ENABLE_LBN 13
#define	PCRF_DZ_OBFF_ENABLE_WIDTH 2
#define	PCRF_DZ_LTR_ENABLE_LBN 10
#define	PCRF_DZ_LTR_ENABLE_WIDTH 1
#define	PCRF_DZ_IDO_COMPLETION_ENABLE_LBN 9
#define	PCRF_DZ_IDO_COMPLETION_ENABLE_WIDTH 1
#define	PCRF_DZ_IDO_REQUEST_ENABLE_LBN 8
#define	PCRF_DZ_IDO_REQUEST_ENABLE_WIDTH 1
#define	PCRF_CZ_CMPL_TIMEOUT_DIS_CTL_LBN 4
#define	PCRF_CZ_CMPL_TIMEOUT_DIS_CTL_WIDTH 1
#define	PCRF_CZ_CMPL_TIMEOUT_CTL_LBN 0
#define	PCRF_CZ_CMPL_TIMEOUT_CTL_WIDTH 4


/*
 * PC_MSIX_PBA_BASE_REG(32bit):
 * MSIX Capability PBA Base
 */

#define	PCR_BB_MSIX_PBA_BASE_REG 0x00000098
/* falconb0=pci_f0_config */

#define	PCR_CZ_MSIX_PBA_BASE_REG 0x000000b8
/* sienaa0,hunta0=pci_f0_config */

#define	PCRF_BZ_MSIX_PBA_OFF_LBN 3
#define	PCRF_BZ_MSIX_PBA_OFF_WIDTH 29
#define	PCRF_BZ_MSIX_PBA_BIR_LBN 0
#define	PCRF_BZ_MSIX_PBA_BIR_WIDTH 3


/*
 * PC_LNK_CAP2_REG(32bit):
 * PCIe Link Capability 2
 */

#define	PCR_DZ_LNK_CAP2_REG 0x0000009c
/* hunta0=pci_f0_config */

#define	PCRF_DZ_LNK_SPEED_SUP_LBN 1
#define	PCRF_DZ_LNK_SPEED_SUP_WIDTH 7


/*
 * PC_LNK_CTL2_REG(16bit):
 * PCIe Link Control 2
 */

#define	PCR_CZ_LNK_CTL2_REG 0x000000a0
/* sienaa0,hunta0=pci_f0_config */

#define	PCRF_CZ_POLLING_DEEMPH_LVL_LBN 12
#define	PCRF_CZ_POLLING_DEEMPH_LVL_WIDTH 1
#define	PCRF_CZ_COMPLIANCE_SOS_CTL_LBN 11
#define	PCRF_CZ_COMPLIANCE_SOS_CTL_WIDTH 1
#define	PCRF_CZ_ENTER_MODIFIED_COMPLIANCE_CTL_LBN 10
#define	PCRF_CZ_ENTER_MODIFIED_COMPLIANCE_CTL_WIDTH 1
#define	PCRF_CZ_TRANSMIT_MARGIN_LBN 7
#define	PCRF_CZ_TRANSMIT_MARGIN_WIDTH 3
#define	PCRF_CZ_SELECT_DEEMPH_LBN 6
#define	PCRF_CZ_SELECT_DEEMPH_WIDTH 1
#define	PCRF_CZ_HW_AUTONOMOUS_SPEED_DIS_LBN 5
#define	PCRF_CZ_HW_AUTONOMOUS_SPEED_DIS_WIDTH 1
#define	PCRF_CZ_ENTER_COMPLIANCE_CTL_LBN 4
#define	PCRF_CZ_ENTER_COMPLIANCE_CTL_WIDTH 1
#define	PCRF_CZ_TGT_LNK_SPEED_CTL_LBN 0
#define	PCRF_CZ_TGT_LNK_SPEED_CTL_WIDTH 4
#define	PCFE_DZ_LCTL2_TGT_SPEED_GEN3 3
#define	PCFE_DZ_LCTL2_TGT_SPEED_GEN2 2
#define	PCFE_DZ_LCTL2_TGT_SPEED_GEN1 1


/*
 * PC_LNK_STAT2_REG(16bit):
 * PCIe Link Status 2
 */

#define	PCR_CZ_LNK_STAT2_REG 0x000000a2
/* sienaa0,hunta0=pci_f0_config */

#define	PCRF_CZ_CURRENT_DEEMPH_LBN 0
#define	PCRF_CZ_CURRENT_DEEMPH_WIDTH 1


/*
 * PC_VPD_CAP_ID_REG(8bit):
 * VPD data register
 */

#define	PCR_AB_VPD_CAP_ID_REG 0x000000b0
/* falcona0,falconb0=pci_f0_config */

#define	PCRF_AB_VPD_CAP_ID_LBN 0
#define	PCRF_AB_VPD_CAP_ID_WIDTH 8


/*
 * PC_VPD_NXT_PTR_REG(8bit):
 * VPD next item pointer
 */

#define	PCR_AB_VPD_NXT_PTR_REG 0x000000b1
/* falcona0,falconb0=pci_f0_config */

#define	PCRF_AB_VPD_NXT_PTR_LBN 0
#define	PCRF_AB_VPD_NXT_PTR_WIDTH 8


/*
 * PC_VPD_ADDR_REG(16bit):
 * VPD address register
 */

#define	PCR_AB_VPD_ADDR_REG 0x000000b2
/* falcona0,falconb0=pci_f0_config */

#define	PCRF_AB_VPD_FLAG_LBN 15
#define	PCRF_AB_VPD_FLAG_WIDTH 1
#define	PCRF_AB_VPD_ADDR_LBN 0
#define	PCRF_AB_VPD_ADDR_WIDTH 15


/*
 * PC_VPD_CAP_DATA_REG(32bit):
 * documentation to be written for sum_PC_VPD_CAP_DATA_REG
 */

#define	PCR_AB_VPD_CAP_DATA_REG 0x000000b4
/* falcona0,falconb0=pci_f0_config */

#define	PCR_CZ_VPD_CAP_DATA_REG 0x000000d4
/* sienaa0,hunta0=pci_f0_config */

#define	PCRF_AZ_VPD_DATA_LBN 0
#define	PCRF_AZ_VPD_DATA_WIDTH 32


/*
 * PC_VPD_CAP_CTL_REG(8bit):
 * VPD control and capabilities register
 */

#define	PCR_CZ_VPD_CAP_CTL_REG 0x000000d0
/* sienaa0,hunta0=pci_f0_config */

#define	PCRF_CZ_VPD_FLAG_LBN 31
#define	PCRF_CZ_VPD_FLAG_WIDTH 1
#define	PCRF_CZ_VPD_ADDR_LBN 16
#define	PCRF_CZ_VPD_ADDR_WIDTH 15
#define	PCRF_CZ_VPD_NXT_PTR_LBN 8
#define	PCRF_CZ_VPD_NXT_PTR_WIDTH 8
#define	PCRF_CZ_VPD_CAP_ID_LBN 0
#define	PCRF_CZ_VPD_CAP_ID_WIDTH 8


/*
 * PC_AER_CAP_HDR_REG(32bit):
 * AER capability header register
 */

#define	PCR_AZ_AER_CAP_HDR_REG 0x00000100
/* falcona0,falconb0,sienaa0,hunta0=pci_f0_config */

#define	PCRF_AZ_AERCAPHDR_NXT_PTR_LBN 20
#define	PCRF_AZ_AERCAPHDR_NXT_PTR_WIDTH 12
#define	PCRF_AZ_AERCAPHDR_VER_LBN 16
#define	PCRF_AZ_AERCAPHDR_VER_WIDTH 4
#define	PCRF_AZ_AERCAPHDR_ID_LBN 0
#define	PCRF_AZ_AERCAPHDR_ID_WIDTH 16


/*
 * PC_AER_UNCORR_ERR_STAT_REG(32bit):
 * AER Uncorrectable error status register
 */

#define	PCR_AZ_AER_UNCORR_ERR_STAT_REG 0x00000104
/* falcona0,falconb0,sienaa0,hunta0=pci_f0_config */

#define	PCRF_AZ_UNSUPT_REQ_ERR_STAT_LBN 20
#define	PCRF_AZ_UNSUPT_REQ_ERR_STAT_WIDTH 1
#define	PCRF_AZ_ECRC_ERR_STAT_LBN 19
#define	PCRF_AZ_ECRC_ERR_STAT_WIDTH 1
#define	PCRF_AZ_MALF_TLP_STAT_LBN 18
#define	PCRF_AZ_MALF_TLP_STAT_WIDTH 1
#define	PCRF_AZ_RX_OVF_STAT_LBN 17
#define	PCRF_AZ_RX_OVF_STAT_WIDTH 1
#define	PCRF_AZ_UNEXP_COMP_STAT_LBN 16
#define	PCRF_AZ_UNEXP_COMP_STAT_WIDTH 1
#define	PCRF_AZ_COMP_ABRT_STAT_LBN 15
#define	PCRF_AZ_COMP_ABRT_STAT_WIDTH 1
#define	PCRF_AZ_COMP_TIMEOUT_STAT_LBN 14
#define	PCRF_AZ_COMP_TIMEOUT_STAT_WIDTH 1
#define	PCRF_AZ_FC_PROTO_ERR_STAT_LBN 13
#define	PCRF_AZ_FC_PROTO_ERR_STAT_WIDTH 1
#define	PCRF_AZ_PSON_TLP_STAT_LBN 12
#define	PCRF_AZ_PSON_TLP_STAT_WIDTH 1
#define	PCRF_AZ_DL_PROTO_ERR_STAT_LBN 4
#define	PCRF_AZ_DL_PROTO_ERR_STAT_WIDTH 1
#define	PCRF_AB_TRAIN_ERR_STAT_LBN 0
#define	PCRF_AB_TRAIN_ERR_STAT_WIDTH 1


/*
 * PC_AER_UNCORR_ERR_MASK_REG(32bit):
 * AER Uncorrectable error mask register
 */

#define	PCR_AZ_AER_UNCORR_ERR_MASK_REG 0x00000108
/* falcona0,falconb0,sienaa0,hunta0=pci_f0_config */

#define	PCRF_DZ_ATOMIC_OP_EGR_BLOCKED_MASK_LBN 24
#define	PCRF_DZ_ATOMIC_OP_EGR_BLOCKED_MASK_WIDTH 1
#define	PCRF_DZ_UNCORR_INT_ERR_MASK_LBN 22
#define	PCRF_DZ_UNCORR_INT_ERR_MASK_WIDTH 1
#define	PCRF_AZ_UNSUPT_REQ_ERR_MASK_LBN 20
#define	PCRF_AZ_UNSUPT_REQ_ERR_MASK_WIDTH 1
#define	PCRF_AZ_ECRC_ERR_MASK_LBN 19
#define	PCRF_AZ_ECRC_ERR_MASK_WIDTH 1
#define	PCRF_AZ_MALF_TLP_MASK_LBN 18
#define	PCRF_AZ_MALF_TLP_MASK_WIDTH 1
#define	PCRF_AZ_RX_OVF_MASK_LBN 17
#define	PCRF_AZ_RX_OVF_MASK_WIDTH 1
#define	PCRF_AZ_UNEXP_COMP_MASK_LBN 16
#define	PCRF_AZ_UNEXP_COMP_MASK_WIDTH 1
#define	PCRF_AZ_COMP_ABRT_MASK_LBN 15
#define	PCRF_AZ_COMP_ABRT_MASK_WIDTH 1
#define	PCRF_AZ_COMP_TIMEOUT_MASK_LBN 14
#define	PCRF_AZ_COMP_TIMEOUT_MASK_WIDTH 1
#define	PCRF_AZ_FC_PROTO_ERR_MASK_LBN 13
#define	PCRF_AZ_FC_PROTO_ERR_MASK_WIDTH 1
#define	PCRF_AZ_PSON_TLP_MASK_LBN 12
#define	PCRF_AZ_PSON_TLP_MASK_WIDTH 1
#define	PCRF_AZ_DL_PROTO_ERR_MASK_LBN 4
#define	PCRF_AZ_DL_PROTO_ERR_MASK_WIDTH 1
#define	PCRF_AB_TRAIN_ERR_MASK_LBN 0
#define	PCRF_AB_TRAIN_ERR_MASK_WIDTH 1


/*
 * PC_AER_UNCORR_ERR_SEV_REG(32bit):
 * AER Uncorrectable error severity register
 */

#define	PCR_AZ_AER_UNCORR_ERR_SEV_REG 0x0000010c
/* falcona0,falconb0,sienaa0,hunta0=pci_f0_config */

#define	PCRF_AZ_UNSUPT_REQ_ERR_SEV_LBN 20
#define	PCRF_AZ_UNSUPT_REQ_ERR_SEV_WIDTH 1
#define	PCRF_AZ_ECRC_ERR_SEV_LBN 19
#define	PCRF_AZ_ECRC_ERR_SEV_WIDTH 1
#define	PCRF_AZ_MALF_TLP_SEV_LBN 18
#define	PCRF_AZ_MALF_TLP_SEV_WIDTH 1
#define	PCRF_AZ_RX_OVF_SEV_LBN 17
#define	PCRF_AZ_RX_OVF_SEV_WIDTH 1
#define	PCRF_AZ_UNEXP_COMP_SEV_LBN 16
#define	PCRF_AZ_UNEXP_COMP_SEV_WIDTH 1
#define	PCRF_AZ_COMP_ABRT_SEV_LBN 15
#define	PCRF_AZ_COMP_ABRT_SEV_WIDTH 1
#define	PCRF_AZ_COMP_TIMEOUT_SEV_LBN 14
#define	PCRF_AZ_COMP_TIMEOUT_SEV_WIDTH 1
#define	PCRF_AZ_FC_PROTO_ERR_SEV_LBN 13
#define	PCRF_AZ_FC_PROTO_ERR_SEV_WIDTH 1
#define	PCRF_AZ_PSON_TLP_SEV_LBN 12
#define	PCRF_AZ_PSON_TLP_SEV_WIDTH 1
#define	PCRF_AZ_DL_PROTO_ERR_SEV_LBN 4
#define	PCRF_AZ_DL_PROTO_ERR_SEV_WIDTH 1
#define	PCRF_AB_TRAIN_ERR_SEV_LBN 0
#define	PCRF_AB_TRAIN_ERR_SEV_WIDTH 1


/*
 * PC_AER_CORR_ERR_STAT_REG(32bit):
 * AER Correctable error status register
 */

#define	PCR_AZ_AER_CORR_ERR_STAT_REG 0x00000110
/* falcona0,falconb0,sienaa0,hunta0=pci_f0_config */

#define	PCRF_CZ_ADVSY_NON_FATAL_STAT_LBN 13
#define	PCRF_CZ_ADVSY_NON_FATAL_STAT_WIDTH 1
#define	PCRF_AZ_RPLY_TMR_TOUT_STAT_LBN 12
#define	PCRF_AZ_RPLY_TMR_TOUT_STAT_WIDTH 1
#define	PCRF_AZ_RPLAY_NUM_RO_STAT_LBN 8
#define	PCRF_AZ_RPLAY_NUM_RO_STAT_WIDTH 1
#define	PCRF_AZ_BAD_DLLP_STAT_LBN 7
#define	PCRF_AZ_BAD_DLLP_STAT_WIDTH 1
#define	PCRF_AZ_BAD_TLP_STAT_LBN 6
#define	PCRF_AZ_BAD_TLP_STAT_WIDTH 1
#define	PCRF_AZ_RX_ERR_STAT_LBN 0
#define	PCRF_AZ_RX_ERR_STAT_WIDTH 1


/*
 * PC_AER_CORR_ERR_MASK_REG(32bit):
 * AER Correctable error status register
 */

#define	PCR_AZ_AER_CORR_ERR_MASK_REG 0x00000114
/* falcona0,falconb0,sienaa0,hunta0=pci_f0_config */

#define	PCRF_CZ_ADVSY_NON_FATAL_MASK_LBN 13
#define	PCRF_CZ_ADVSY_NON_FATAL_MASK_WIDTH 1
#define	PCRF_AZ_RPLY_TMR_TOUT_MASK_LBN 12
#define	PCRF_AZ_RPLY_TMR_TOUT_MASK_WIDTH 1
#define	PCRF_AZ_RPLAY_NUM_RO_MASK_LBN 8
#define	PCRF_AZ_RPLAY_NUM_RO_MASK_WIDTH 1
#define	PCRF_AZ_BAD_DLLP_MASK_LBN 7
#define	PCRF_AZ_BAD_DLLP_MASK_WIDTH 1
#define	PCRF_AZ_BAD_TLP_MASK_LBN 6
#define	PCRF_AZ_BAD_TLP_MASK_WIDTH 1
#define	PCRF_AZ_RX_ERR_MASK_LBN 0
#define	PCRF_AZ_RX_ERR_MASK_WIDTH 1


/*
 * PC_AER_CAP_CTL_REG(32bit):
 * AER capability and control register
 */

#define	PCR_AZ_AER_CAP_CTL_REG 0x00000118
/* falcona0,falconb0,sienaa0,hunta0=pci_f0_config */

#define	PCRF_AZ_ECRC_CHK_EN_LBN 8
#define	PCRF_AZ_ECRC_CHK_EN_WIDTH 1
#define	PCRF_AZ_ECRC_CHK_CAP_LBN 7
#define	PCRF_AZ_ECRC_CHK_CAP_WIDTH 1
#define	PCRF_AZ_ECRC_GEN_EN_LBN 6
#define	PCRF_AZ_ECRC_GEN_EN_WIDTH 1
#define	PCRF_AZ_ECRC_GEN_CAP_LBN 5
#define	PCRF_AZ_ECRC_GEN_CAP_WIDTH 1
#define	PCRF_AZ_1ST_ERR_PTR_LBN 0
#define	PCRF_AZ_1ST_ERR_PTR_WIDTH 5


/*
 * PC_AER_HDR_LOG_REG(128bit):
 * AER Header log register
 */

#define	PCR_AZ_AER_HDR_LOG_REG 0x0000011c
/* falcona0,falconb0,sienaa0,hunta0=pci_f0_config */

#define	PCRF_AZ_HDR_LOG_LBN 0
#define	PCRF_AZ_HDR_LOG_WIDTH 128


/*
 * PC_DEVSN_CAP_HDR_REG(32bit):
 * Device serial number capability header register
 */

#define	PCR_CZ_DEVSN_CAP_HDR_REG 0x00000140
/* sienaa0,hunta0=pci_f0_config */

#define	PCRF_CZ_DEVSNCAPHDR_NXT_PTR_LBN 20
#define	PCRF_CZ_DEVSNCAPHDR_NXT_PTR_WIDTH 12
#define	PCRF_CZ_DEVSNCAPHDR_VER_LBN 16
#define	PCRF_CZ_DEVSNCAPHDR_VER_WIDTH 4
#define	PCRF_CZ_DEVSNCAPHDR_ID_LBN 0
#define	PCRF_CZ_DEVSNCAPHDR_ID_WIDTH 16


/*
 * PC_DEVSN_DWORD0_REG(32bit):
 * Device serial number DWORD0
 */

#define	PCR_CZ_DEVSN_DWORD0_REG 0x00000144
/* sienaa0,hunta0=pci_f0_config */

#define	PCRF_CZ_DEVSN_DWORD0_LBN 0
#define	PCRF_CZ_DEVSN_DWORD0_WIDTH 32


/*
 * PC_DEVSN_DWORD1_REG(32bit):
 * Device serial number DWORD0
 */

#define	PCR_CZ_DEVSN_DWORD1_REG 0x00000148
/* sienaa0,hunta0=pci_f0_config */

#define	PCRF_CZ_DEVSN_DWORD1_LBN 0
#define	PCRF_CZ_DEVSN_DWORD1_WIDTH 32


/*
 * PC_ARI_CAP_HDR_REG(32bit):
 * ARI capability header register
 */

#define	PCR_CZ_ARI_CAP_HDR_REG 0x00000150
/* sienaa0,hunta0=pci_f0_config */

#define	PCRF_CZ_ARICAPHDR_NXT_PTR_LBN 20
#define	PCRF_CZ_ARICAPHDR_NXT_PTR_WIDTH 12
#define	PCRF_CZ_ARICAPHDR_VER_LBN 16
#define	PCRF_CZ_ARICAPHDR_VER_WIDTH 4
#define	PCRF_CZ_ARICAPHDR_ID_LBN 0
#define	PCRF_CZ_ARICAPHDR_ID_WIDTH 16


/*
 * PC_ARI_CAP_REG(16bit):
 * ARI Capabilities
 */

#define	PCR_CZ_ARI_CAP_REG 0x00000154
/* sienaa0,hunta0=pci_f0_config */

#define	PCRF_CZ_ARI_NXT_FN_NUM_LBN 8
#define	PCRF_CZ_ARI_NXT_FN_NUM_WIDTH 8
#define	PCRF_CZ_ARI_ACS_FNGRP_CAP_LBN 1
#define	PCRF_CZ_ARI_ACS_FNGRP_CAP_WIDTH 1
#define	PCRF_CZ_ARI_MFVC_FNGRP_CAP_LBN 0
#define	PCRF_CZ_ARI_MFVC_FNGRP_CAP_WIDTH 1


/*
 * PC_ARI_CTL_REG(16bit):
 * ARI Control
 */

#define	PCR_CZ_ARI_CTL_REG 0x00000156
/* sienaa0,hunta0=pci_f0_config */

#define	PCRF_CZ_ARI_FN_GRP_LBN 4
#define	PCRF_CZ_ARI_FN_GRP_WIDTH 3
#define	PCRF_CZ_ARI_ACS_FNGRP_EN_LBN 1
#define	PCRF_CZ_ARI_ACS_FNGRP_EN_WIDTH 1
#define	PCRF_CZ_ARI_MFVC_FNGRP_EN_LBN 0
#define	PCRF_CZ_ARI_MFVC_FNGRP_EN_WIDTH 1


/*
 * PC_SEC_PCIE_CAP_REG(32bit):
 * Secondary PCIE Capability Register
 */

#define	PCR_DZ_SEC_PCIE_CAP_REG 0x00000160
/* hunta0=pci_f0_config */

#define	PCRF_DZ_SEC_NXT_PTR_LBN 20
#define	PCRF_DZ_SEC_NXT_PTR_WIDTH 12
#define	PCRF_DZ_SEC_VERSION_LBN 16
#define	PCRF_DZ_SEC_VERSION_WIDTH 4
#define	PCRF_DZ_SEC_EXT_CAP_ID_LBN 0
#define	PCRF_DZ_SEC_EXT_CAP_ID_WIDTH 16


/*
 * PC_SRIOV_CAP_HDR_REG(32bit):
 * SRIOV capability header register
 */

#define	PCR_CC_SRIOV_CAP_HDR_REG 0x00000160
/* sienaa0=pci_f0_config */

#define	PCR_DZ_SRIOV_CAP_HDR_REG 0x00000180
/* hunta0=pci_f0_config */

#define	PCRF_CZ_SRIOVCAPHDR_NXT_PTR_LBN 20
#define	PCRF_CZ_SRIOVCAPHDR_NXT_PTR_WIDTH 12
#define	PCRF_CZ_SRIOVCAPHDR_VER_LBN 16
#define	PCRF_CZ_SRIOVCAPHDR_VER_WIDTH 4
#define	PCRF_CZ_SRIOVCAPHDR_ID_LBN 0
#define	PCRF_CZ_SRIOVCAPHDR_ID_WIDTH 16


/*
 * PC_SRIOV_CAP_REG(32bit):
 * SRIOV Capabilities
 */

#define	PCR_CC_SRIOV_CAP_REG 0x00000164
/* sienaa0=pci_f0_config */

#define	PCR_DZ_SRIOV_CAP_REG 0x00000184
/* hunta0=pci_f0_config */

#define	PCRF_CZ_VF_MIGR_INT_MSG_NUM_LBN 21
#define	PCRF_CZ_VF_MIGR_INT_MSG_NUM_WIDTH 11
#define	PCRF_DZ_VF_ARI_CAP_PRESV_LBN 1
#define	PCRF_DZ_VF_ARI_CAP_PRESV_WIDTH 1
#define	PCRF_CZ_VF_MIGR_CAP_LBN 0
#define	PCRF_CZ_VF_MIGR_CAP_WIDTH 1


/*
 * PC_LINK_CONTROL3_REG(32bit):
 * Link Control 3.
 */

#define	PCR_DZ_LINK_CONTROL3_REG 0x00000164
/* hunta0=pci_f0_config */

#define	PCRF_DZ_LINK_EQ_INT_EN_LBN 1
#define	PCRF_DZ_LINK_EQ_INT_EN_WIDTH 1
#define	PCRF_DZ_PERFORM_EQL_LBN 0
#define	PCRF_DZ_PERFORM_EQL_WIDTH 1


/*
 * PC_LANE_ERROR_STAT_REG(32bit):
 * Lane Error Status Register.
 */

#define	PCR_DZ_LANE_ERROR_STAT_REG 0x00000168
/* hunta0=pci_f0_config */

#define	PCRF_DZ_LANE_STATUS_LBN 0
#define	PCRF_DZ_LANE_STATUS_WIDTH 8


/*
 * PC_SRIOV_CTL_REG(16bit):
 * SRIOV Control
 */

#define	PCR_CC_SRIOV_CTL_REG 0x00000168
/* sienaa0=pci_f0_config */

#define	PCR_DZ_SRIOV_CTL_REG 0x00000188
/* hunta0=pci_f0_config */

#define	PCRF_CZ_VF_ARI_CAP_HRCHY_LBN 4
#define	PCRF_CZ_VF_ARI_CAP_HRCHY_WIDTH 1
#define	PCRF_CZ_VF_MSE_LBN 3
#define	PCRF_CZ_VF_MSE_WIDTH 1
#define	PCRF_CZ_VF_MIGR_INT_EN_LBN 2
#define	PCRF_CZ_VF_MIGR_INT_EN_WIDTH 1
#define	PCRF_CZ_VF_MIGR_EN_LBN 1
#define	PCRF_CZ_VF_MIGR_EN_WIDTH 1
#define	PCRF_CZ_VF_EN_LBN 0
#define	PCRF_CZ_VF_EN_WIDTH 1


/*
 * PC_SRIOV_STAT_REG(16bit):
 * SRIOV Status
 */

#define	PCR_CC_SRIOV_STAT_REG 0x0000016a
/* sienaa0=pci_f0_config */

#define	PCR_DZ_SRIOV_STAT_REG 0x0000018a
/* hunta0=pci_f0_config */

#define	PCRF_CZ_VF_MIGR_STAT_LBN 0
#define	PCRF_CZ_VF_MIGR_STAT_WIDTH 1


/*
 * PC_LANE01_EQU_CONTROL_REG(32bit):
 * Lanes 0,1 Equalization Control Register.
 */

#define	PCR_DZ_LANE01_EQU_CONTROL_REG 0x0000016c
/* hunta0=pci_f0_config */

#define	PCRF_DZ_LANE1_EQ_CTRL_LBN 16
#define	PCRF_DZ_LANE1_EQ_CTRL_WIDTH 16
#define	PCRF_DZ_LANE0_EQ_CTRL_LBN 0
#define	PCRF_DZ_LANE0_EQ_CTRL_WIDTH 16


/*
 * PC_SRIOV_INITIALVFS_REG(16bit):
 * SRIOV Initial VFs
 */

#define	PCR_CC_SRIOV_INITIALVFS_REG 0x0000016c
/* sienaa0=pci_f0_config */

#define	PCR_DZ_SRIOV_INITIALVFS_REG 0x0000018c
/* hunta0=pci_f0_config */

#define	PCRF_CZ_VF_INITIALVFS_LBN 0
#define	PCRF_CZ_VF_INITIALVFS_WIDTH 16


/*
 * PC_SRIOV_TOTALVFS_REG(10bit):
 * SRIOV Total VFs
 */

#define	PCR_CC_SRIOV_TOTALVFS_REG 0x0000016e
/* sienaa0=pci_f0_config */

#define	PCR_DZ_SRIOV_TOTALVFS_REG 0x0000018e
/* hunta0=pci_f0_config */

#define	PCRF_CZ_VF_TOTALVFS_LBN 0
#define	PCRF_CZ_VF_TOTALVFS_WIDTH 16


/*
 * PC_SRIOV_NUMVFS_REG(16bit):
 * SRIOV Number of VFs
 */

#define	PCR_CC_SRIOV_NUMVFS_REG 0x00000170
/* sienaa0=pci_f0_config */

#define	PCR_DZ_SRIOV_NUMVFS_REG 0x00000190
/* hunta0=pci_f0_config */

#define	PCRF_CZ_VF_NUMVFS_LBN 0
#define	PCRF_CZ_VF_NUMVFS_WIDTH 16


/*
 * PC_LANE23_EQU_CONTROL_REG(32bit):
 * Lanes 2,3 Equalization Control Register.
 */

#define	PCR_DZ_LANE23_EQU_CONTROL_REG 0x00000170
/* hunta0=pci_f0_config */

#define	PCRF_DZ_LANE3_EQ_CTRL_LBN 16
#define	PCRF_DZ_LANE3_EQ_CTRL_WIDTH 16
#define	PCRF_DZ_LANE2_EQ_CTRL_LBN 0
#define	PCRF_DZ_LANE2_EQ_CTRL_WIDTH 16


/*
 * PC_SRIOV_FN_DPND_LNK_REG(16bit):
 * SRIOV Function dependency link
 */

#define	PCR_CC_SRIOV_FN_DPND_LNK_REG 0x00000172
/* sienaa0=pci_f0_config */

#define	PCR_DZ_SRIOV_FN_DPND_LNK_REG 0x00000192
/* hunta0=pci_f0_config */

#define	PCRF_CZ_SRIOV_FN_DPND_LNK_LBN 0
#define	PCRF_CZ_SRIOV_FN_DPND_LNK_WIDTH 8


/*
 * PC_SRIOV_1STVF_OFFSET_REG(16bit):
 * SRIOV First VF Offset
 */

#define	PCR_CC_SRIOV_1STVF_OFFSET_REG 0x00000174
/* sienaa0=pci_f0_config */

#define	PCR_DZ_SRIOV_1STVF_OFFSET_REG 0x00000194
/* hunta0=pci_f0_config */

#define	PCRF_CZ_VF_1STVF_OFFSET_LBN 0
#define	PCRF_CZ_VF_1STVF_OFFSET_WIDTH 16


/*
 * PC_LANE45_EQU_CONTROL_REG(32bit):
 * Lanes 4,5 Equalization Control Register.
 */

#define	PCR_DZ_LANE45_EQU_CONTROL_REG 0x00000174
/* hunta0=pci_f0_config */

#define	PCRF_DZ_LANE5_EQ_CTRL_LBN 16
#define	PCRF_DZ_LANE5_EQ_CTRL_WIDTH 16
#define	PCRF_DZ_LANE4_EQ_CTRL_LBN 0
#define	PCRF_DZ_LANE4_EQ_CTRL_WIDTH 16


/*
 * PC_SRIOV_VFSTRIDE_REG(16bit):
 * SRIOV VF Stride
 */

#define	PCR_CC_SRIOV_VFSTRIDE_REG 0x00000176
/* sienaa0=pci_f0_config */

#define	PCR_DZ_SRIOV_VFSTRIDE_REG 0x00000196
/* hunta0=pci_f0_config */

#define	PCRF_CZ_VF_VFSTRIDE_LBN 0
#define	PCRF_CZ_VF_VFSTRIDE_WIDTH 16


/*
 * PC_LANE67_EQU_CONTROL_REG(32bit):
 * Lanes 6,7 Equalization Control Register.
 */

#define	PCR_DZ_LANE67_EQU_CONTROL_REG 0x00000178
/* hunta0=pci_f0_config */

#define	PCRF_DZ_LANE7_EQ_CTRL_LBN 16
#define	PCRF_DZ_LANE7_EQ_CTRL_WIDTH 16
#define	PCRF_DZ_LANE6_EQ_CTRL_LBN 0
#define	PCRF_DZ_LANE6_EQ_CTRL_WIDTH 16


/*
 * PC_SRIOV_DEVID_REG(16bit):
 * SRIOV VF Device ID
 */

#define	PCR_CC_SRIOV_DEVID_REG 0x0000017a
/* sienaa0=pci_f0_config */

#define	PCR_DZ_SRIOV_DEVID_REG 0x0000019a
/* hunta0=pci_f0_config */

#define	PCRF_CZ_VF_DEVID_LBN 0
#define	PCRF_CZ_VF_DEVID_WIDTH 16


/*
 * PC_SRIOV_SUP_PAGESZ_REG(16bit):
 * SRIOV Supported Page Sizes
 */

#define	PCR_CC_SRIOV_SUP_PAGESZ_REG 0x0000017c
/* sienaa0=pci_f0_config */

#define	PCR_DZ_SRIOV_SUP_PAGESZ_REG 0x0000019c
/* hunta0=pci_f0_config */

#define	PCRF_CZ_VF_SUP_PAGESZ_LBN 0
#define	PCRF_CZ_VF_SUP_PAGESZ_WIDTH 16


/*
 * PC_SRIOV_SYS_PAGESZ_REG(32bit):
 * SRIOV System Page Size
 */

#define	PCR_CC_SRIOV_SYS_PAGESZ_REG 0x00000180
/* sienaa0=pci_f0_config */

#define	PCR_DZ_SRIOV_SYS_PAGESZ_REG 0x000001a0
/* hunta0=pci_f0_config */

#define	PCRF_CZ_VF_SYS_PAGESZ_LBN 0
#define	PCRF_CZ_VF_SYS_PAGESZ_WIDTH 16


/*
 * PC_SRIOV_BAR0_REG(32bit):
 * SRIOV VF Bar0
 */

#define	PCR_CC_SRIOV_BAR0_REG 0x00000184
/* sienaa0=pci_f0_config */

#define	PCR_DZ_SRIOV_BAR0_REG 0x000001a4
/* hunta0=pci_f0_config */

#define	PCRF_CC_VF_BAR_ADDRESS_LBN 0
#define	PCRF_CC_VF_BAR_ADDRESS_WIDTH 32
#define	PCRF_DZ_VF_BAR0_ADDRESS_LBN 4
#define	PCRF_DZ_VF_BAR0_ADDRESS_WIDTH 28
#define	PCRF_DZ_VF_BAR0_PREF_LBN 3
#define	PCRF_DZ_VF_BAR0_PREF_WIDTH 1
#define	PCRF_DZ_VF_BAR0_TYPE_LBN 1
#define	PCRF_DZ_VF_BAR0_TYPE_WIDTH 2
#define	PCRF_DZ_VF_BAR0_IOM_LBN 0
#define	PCRF_DZ_VF_BAR0_IOM_WIDTH 1


/*
 * PC_SRIOV_BAR1_REG(32bit):
 * SRIOV Bar1
 */

#define	PCR_CC_SRIOV_BAR1_REG 0x00000188
/* sienaa0=pci_f0_config */

#define	PCR_DZ_SRIOV_BAR1_REG 0x000001a8
/* hunta0=pci_f0_config */

/* defined as PCRF_CC_VF_BAR_ADDRESS_LBN 0; */
/* defined as PCRF_CC_VF_BAR_ADDRESS_WIDTH 32 */
#define	PCRF_DZ_VF_BAR1_ADDRESS_LBN 0
#define	PCRF_DZ_VF_BAR1_ADDRESS_WIDTH 32


/*
 * PC_SRIOV_BAR2_REG(32bit):
 * SRIOV Bar2
 */

#define	PCR_CC_SRIOV_BAR2_REG 0x0000018c
/* sienaa0=pci_f0_config */

#define	PCR_DZ_SRIOV_BAR2_REG 0x000001ac
/* hunta0=pci_f0_config */

/* defined as PCRF_CC_VF_BAR_ADDRESS_LBN 0; */
/* defined as PCRF_CC_VF_BAR_ADDRESS_WIDTH 32 */
#define	PCRF_DZ_VF_BAR2_ADDRESS_LBN 4
#define	PCRF_DZ_VF_BAR2_ADDRESS_WIDTH 28
#define	PCRF_DZ_VF_BAR2_PREF_LBN 3
#define	PCRF_DZ_VF_BAR2_PREF_WIDTH 1
#define	PCRF_DZ_VF_BAR2_TYPE_LBN 1
#define	PCRF_DZ_VF_BAR2_TYPE_WIDTH 2
#define	PCRF_DZ_VF_BAR2_IOM_LBN 0
#define	PCRF_DZ_VF_BAR2_IOM_WIDTH 1


/*
 * PC_SRIOV_BAR3_REG(32bit):
 * SRIOV Bar3
 */

#define	PCR_CC_SRIOV_BAR3_REG 0x00000190
/* sienaa0=pci_f0_config */

#define	PCR_DZ_SRIOV_BAR3_REG 0x000001b0
/* hunta0=pci_f0_config */

/* defined as PCRF_CC_VF_BAR_ADDRESS_LBN 0; */
/* defined as PCRF_CC_VF_BAR_ADDRESS_WIDTH 32 */
#define	PCRF_DZ_VF_BAR3_ADDRESS_LBN 0
#define	PCRF_DZ_VF_BAR3_ADDRESS_WIDTH 32


/*
 * PC_SRIOV_BAR4_REG(32bit):
 * SRIOV Bar4
 */

#define	PCR_CC_SRIOV_BAR4_REG 0x00000194
/* sienaa0=pci_f0_config */

#define	PCR_DZ_SRIOV_BAR4_REG 0x000001b4
/* hunta0=pci_f0_config */

/* defined as PCRF_CC_VF_BAR_ADDRESS_LBN 0; */
/* defined as PCRF_CC_VF_BAR_ADDRESS_WIDTH 32 */
#define	PCRF_DZ_VF_BAR4_ADDRESS_LBN 0
#define	PCRF_DZ_VF_BAR4_ADDRESS_WIDTH 32


/*
 * PC_SRIOV_BAR5_REG(32bit):
 * SRIOV Bar5
 */

#define	PCR_CC_SRIOV_BAR5_REG 0x00000198
/* sienaa0=pci_f0_config */

#define	PCR_DZ_SRIOV_BAR5_REG 0x000001b8
/* hunta0=pci_f0_config */

/* defined as PCRF_CC_VF_BAR_ADDRESS_LBN 0; */
/* defined as PCRF_CC_VF_BAR_ADDRESS_WIDTH 32 */
#define	PCRF_DZ_VF_BAR5_ADDRESS_LBN 0
#define	PCRF_DZ_VF_BAR5_ADDRESS_WIDTH 32


/*
 * PC_SRIOV_RSVD_REG(16bit):
 * Reserved register
 */

#define	PCR_DZ_SRIOV_RSVD_REG 0x00000198
/* hunta0=pci_f0_config */

#define	PCRF_DZ_VF_RSVD_LBN 0
#define	PCRF_DZ_VF_RSVD_WIDTH 16


/*
 * PC_SRIOV_MIBR_SARRAY_OFFSET_REG(32bit):
 * SRIOV VF Migration State Array Offset
 */

#define	PCR_CC_SRIOV_MIBR_SARRAY_OFFSET_REG 0x0000019c
/* sienaa0=pci_f0_config */

#define	PCR_DZ_SRIOV_MIBR_SARRAY_OFFSET_REG 0x000001bc
/* hunta0=pci_f0_config */

#define	PCRF_CZ_VF_MIGR_OFFSET_LBN 3
#define	PCRF_CZ_VF_MIGR_OFFSET_WIDTH 29
#define	PCRF_CZ_VF_MIGR_BIR_LBN 0
#define	PCRF_CZ_VF_MIGR_BIR_WIDTH 3


/*
 * PC_TPH_CAP_HDR_REG(32bit):
 * TPH Capability Header Register
 */

#define	PCR_DZ_TPH_CAP_HDR_REG 0x000001c0
/* hunta0=pci_f0_config */

#define	PCRF_DZ_TPH_NXT_PTR_LBN 20
#define	PCRF_DZ_TPH_NXT_PTR_WIDTH 12
#define	PCRF_DZ_TPH_VERSION_LBN 16
#define	PCRF_DZ_TPH_VERSION_WIDTH 4
#define	PCRF_DZ_TPH_EXT_CAP_ID_LBN 0
#define	PCRF_DZ_TPH_EXT_CAP_ID_WIDTH 16


/*
 * PC_TPH_REQ_CAP_REG(32bit):
 * TPH Requester Capability Register
 */

#define	PCR_DZ_TPH_REQ_CAP_REG 0x000001c4
/* hunta0=pci_f0_config */

#define	PCRF_DZ_ST_TBLE_SIZE_LBN 16
#define	PCRF_DZ_ST_TBLE_SIZE_WIDTH 11
#define	PCRF_DZ_ST_TBLE_LOC_LBN 9
#define	PCRF_DZ_ST_TBLE_LOC_WIDTH 2
#define	PCRF_DZ_EXT_TPH_MODE_SUP_LBN 8
#define	PCRF_DZ_EXT_TPH_MODE_SUP_WIDTH 1
#define	PCRF_DZ_TPH_DEV_MODE_SUP_LBN 2
#define	PCRF_DZ_TPH_DEV_MODE_SUP_WIDTH 1
#define	PCRF_DZ_TPH_INT_MODE_SUP_LBN 1
#define	PCRF_DZ_TPH_INT_MODE_SUP_WIDTH 1
#define	PCRF_DZ_TPH_NOST_MODE_SUP_LBN 0
#define	PCRF_DZ_TPH_NOST_MODE_SUP_WIDTH 1


/*
 * PC_TPH_REQ_CTL_REG(32bit):
 * TPH Requester Control Register
 */

#define	PCR_DZ_TPH_REQ_CTL_REG 0x000001c8
/* hunta0=pci_f0_config */

#define	PCRF_DZ_TPH_REQ_ENABLE_LBN 8
#define	PCRF_DZ_TPH_REQ_ENABLE_WIDTH 2
#define	PCRF_DZ_TPH_ST_MODE_LBN 0
#define	PCRF_DZ_TPH_ST_MODE_WIDTH 3


/*
 * PC_LTR_CAP_HDR_REG(32bit):
 * Latency Tolerance Reporting Cap Header Reg
 */

#define	PCR_DZ_LTR_CAP_HDR_REG 0x00000290
/* hunta0=pci_f0_config */

#define	PCRF_DZ_LTR_NXT_PTR_LBN 20
#define	PCRF_DZ_LTR_NXT_PTR_WIDTH 12
#define	PCRF_DZ_LTR_VERSION_LBN 16
#define	PCRF_DZ_LTR_VERSION_WIDTH 4
#define	PCRF_DZ_LTR_EXT_CAP_ID_LBN 0
#define	PCRF_DZ_LTR_EXT_CAP_ID_WIDTH 16


/*
 * PC_LTR_MAX_SNOOP_REG(32bit):
 * LTR Maximum Snoop/No Snoop Register
 */

#define	PCR_DZ_LTR_MAX_SNOOP_REG 0x00000294
/* hunta0=pci_f0_config */

#define	PCRF_DZ_LTR_MAX_NOSNOOP_SCALE_LBN 26
#define	PCRF_DZ_LTR_MAX_NOSNOOP_SCALE_WIDTH 3
#define	PCRF_DZ_LTR_MAX_NOSNOOP_LAT_LBN 16
#define	PCRF_DZ_LTR_MAX_NOSNOOP_LAT_WIDTH 10
#define	PCRF_DZ_LTR_MAX_SNOOP_SCALE_LBN 10
#define	PCRF_DZ_LTR_MAX_SNOOP_SCALE_WIDTH 3
#define	PCRF_DZ_LTR_MAX_SNOOP_LAT_LBN 0
#define	PCRF_DZ_LTR_MAX_SNOOP_LAT_WIDTH 10


/*
 * PC_ACK_LAT_TMR_REG(32bit):
 * ACK latency timer & replay timer register
 */

#define	PCR_AC_ACK_LAT_TMR_REG 0x00000700
/* falcona0,falconb0,sienaa0=pci_f0_config */

#define	PCRF_AC_RT_LBN 16
#define	PCRF_AC_RT_WIDTH 16
#define	PCRF_AC_ALT_LBN 0
#define	PCRF_AC_ALT_WIDTH 16


/*
 * PC_OTHER_MSG_REG(32bit):
 * Other message register
 */

#define	PCR_AC_OTHER_MSG_REG 0x00000704
/* falcona0,falconb0,sienaa0=pci_f0_config */

#define	PCRF_AC_OM_CRPT3_LBN 24
#define	PCRF_AC_OM_CRPT3_WIDTH 8
#define	PCRF_AC_OM_CRPT2_LBN 16
#define	PCRF_AC_OM_CRPT2_WIDTH 8
#define	PCRF_AC_OM_CRPT1_LBN 8
#define	PCRF_AC_OM_CRPT1_WIDTH 8
#define	PCRF_AC_OM_CRPT0_LBN 0
#define	PCRF_AC_OM_CRPT0_WIDTH 8


/*
 * PC_FORCE_LNK_REG(24bit):
 * Port force link register
 */

#define	PCR_AC_FORCE_LNK_REG 0x00000708
/* falcona0,falconb0,sienaa0=pci_f0_config */

#define	PCRF_AC_LFS_LBN 16
#define	PCRF_AC_LFS_WIDTH 6
#define	PCRF_AC_FL_LBN 15
#define	PCRF_AC_FL_WIDTH 1
#define	PCRF_AC_LN_LBN 0
#define	PCRF_AC_LN_WIDTH 8


/*
 * PC_ACK_FREQ_REG(32bit):
 * ACK frequency register
 */

#define	PCR_AC_ACK_FREQ_REG 0x0000070c
/* falcona0,falconb0,sienaa0=pci_f0_config */

#define	PCRF_CC_ALLOW_L1_WITHOUT_L0S_LBN 30
#define	PCRF_CC_ALLOW_L1_WITHOUT_L0S_WIDTH 1
#define	PCRF_AC_L1_ENTR_LAT_LBN 27
#define	PCRF_AC_L1_ENTR_LAT_WIDTH 3
#define	PCRF_AC_L0_ENTR_LAT_LBN 24
#define	PCRF_AC_L0_ENTR_LAT_WIDTH 3
#define	PCRF_CC_COMM_NFTS_LBN 16
#define	PCRF_CC_COMM_NFTS_WIDTH 8
#define	PCRF_AB_ACK_FREQ_REG_RSVD0_LBN 16
#define	PCRF_AB_ACK_FREQ_REG_RSVD0_WIDTH 3
#define	PCRF_AC_MAX_FTS_LBN 8
#define	PCRF_AC_MAX_FTS_WIDTH 8
#define	PCRF_AC_ACK_FREQ_LBN 0
#define	PCRF_AC_ACK_FREQ_WIDTH 8


/*
 * PC_PORT_LNK_CTL_REG(32bit):
 * Port link control register
 */

#define	PCR_AC_PORT_LNK_CTL_REG 0x00000710
/* falcona0,falconb0,sienaa0=pci_f0_config */

#define	PCRF_AB_LRE_LBN 27
#define	PCRF_AB_LRE_WIDTH 1
#define	PCRF_AB_ESYNC_LBN 26
#define	PCRF_AB_ESYNC_WIDTH 1
#define	PCRF_AB_CRPT_LBN 25
#define	PCRF_AB_CRPT_WIDTH 1
#define	PCRF_AB_XB_LBN 24
#define	PCRF_AB_XB_WIDTH 1
#define	PCRF_AC_LC_LBN 16
#define	PCRF_AC_LC_WIDTH 6
#define	PCRF_AC_LDR_LBN 8
#define	PCRF_AC_LDR_WIDTH 4
#define	PCRF_AC_FLM_LBN 7
#define	PCRF_AC_FLM_WIDTH 1
#define	PCRF_AC_LKD_LBN 6
#define	PCRF_AC_LKD_WIDTH 1
#define	PCRF_AC_DLE_LBN 5
#define	PCRF_AC_DLE_WIDTH 1
#define	PCRF_AB_PORT_LNK_CTL_REG_RSVD0_LBN 4
#define	PCRF_AB_PORT_LNK_CTL_REG_RSVD0_WIDTH 1
#define	PCRF_AC_RA_LBN 3
#define	PCRF_AC_RA_WIDTH 1
#define	PCRF_AC_LE_LBN 2
#define	PCRF_AC_LE_WIDTH 1
#define	PCRF_AC_SD_LBN 1
#define	PCRF_AC_SD_WIDTH 1
#define	PCRF_AC_OMR_LBN 0
#define	PCRF_AC_OMR_WIDTH 1


/*
 * PC_LN_SKEW_REG(32bit):
 * Lane skew register
 */

#define	PCR_AC_LN_SKEW_REG 0x00000714
/* falcona0,falconb0,sienaa0=pci_f0_config */

#define	PCRF_AC_DIS_LBN 31
#define	PCRF_AC_DIS_WIDTH 1
#define	PCRF_AB_RST_LBN 30
#define	PCRF_AB_RST_WIDTH 1
#define	PCRF_AC_AD_LBN 25
#define	PCRF_AC_AD_WIDTH 1
#define	PCRF_AC_FCD_LBN 24
#define	PCRF_AC_FCD_WIDTH 1
#define	PCRF_AC_LS2_LBN 16
#define	PCRF_AC_LS2_WIDTH 8
#define	PCRF_AC_LS1_LBN 8
#define	PCRF_AC_LS1_WIDTH 8
#define	PCRF_AC_LS0_LBN 0
#define	PCRF_AC_LS0_WIDTH 8


/*
 * PC_SYM_NUM_REG(16bit):
 * Symbol number register
 */

#define	PCR_AC_SYM_NUM_REG 0x00000718
/* falcona0,falconb0,sienaa0=pci_f0_config */

#define	PCRF_CC_MAX_FUNCTIONS_LBN 29
#define	PCRF_CC_MAX_FUNCTIONS_WIDTH 3
#define	PCRF_CC_FC_WATCHDOG_TMR_LBN 24
#define	PCRF_CC_FC_WATCHDOG_TMR_WIDTH 5
#define	PCRF_CC_ACK_NAK_TMR_MOD_LBN 19
#define	PCRF_CC_ACK_NAK_TMR_MOD_WIDTH 5
#define	PCRF_CC_REPLAY_TMR_MOD_LBN 14
#define	PCRF_CC_REPLAY_TMR_MOD_WIDTH 5
#define	PCRF_AB_ES_LBN 12
#define	PCRF_AB_ES_WIDTH 3
#define	PCRF_AB_SYM_NUM_REG_RSVD0_LBN 11
#define	PCRF_AB_SYM_NUM_REG_RSVD0_WIDTH 1
#define	PCRF_CC_NUM_SKP_SYMS_LBN 8
#define	PCRF_CC_NUM_SKP_SYMS_WIDTH 3
#define	PCRF_AB_TS2_LBN 4
#define	PCRF_AB_TS2_WIDTH 4
#define	PCRF_AC_TS1_LBN 0
#define	PCRF_AC_TS1_WIDTH 4


/*
 * PC_SYM_TMR_FLT_MSK_REG(16bit):
 * Symbol timer and Filter Mask Register
 */

#define	PCR_CC_SYM_TMR_FLT_MSK_REG 0x0000071c
/* sienaa0=pci_f0_config */

#define	PCRF_CC_DEFAULT_FLT_MSK1_LBN 16
#define	PCRF_CC_DEFAULT_FLT_MSK1_WIDTH 16
#define	PCRF_CC_FC_WDOG_TMR_DIS_LBN 15
#define	PCRF_CC_FC_WDOG_TMR_DIS_WIDTH 1
#define	PCRF_CC_SI1_LBN 8
#define	PCRF_CC_SI1_WIDTH 3
#define	PCRF_CC_SKIP_INT_VAL_LBN 0
#define	PCRF_CC_SKIP_INT_VAL_WIDTH 11
#define	PCRF_CC_SI0_LBN 0
#define	PCRF_CC_SI0_WIDTH 8


/*
 * PC_SYM_TMR_REG(16bit):
 * Symbol timer register
 */

#define	PCR_AB_SYM_TMR_REG 0x0000071c
/* falcona0,falconb0=pci_f0_config */

#define	PCRF_AB_ET_LBN 11
#define	PCRF_AB_ET_WIDTH 4
#define	PCRF_AB_SI1_LBN 8
#define	PCRF_AB_SI1_WIDTH 3
#define	PCRF_AB_SI0_LBN 0
#define	PCRF_AB_SI0_WIDTH 8


/*
 * PC_FLT_MSK_REG(32bit):
 * Filter Mask Register 2
 */

#define	PCR_CC_FLT_MSK_REG 0x00000720
/* sienaa0=pci_f0_config */

#define	PCRF_CC_DEFAULT_FLT_MSK2_LBN 0
#define	PCRF_CC_DEFAULT_FLT_MSK2_WIDTH 32


/*
 * PC_PHY_STAT_REG(32bit):
 * PHY status register
 */

#define	PCR_AB_PHY_STAT_REG 0x00000720
/* falcona0,falconb0=pci_f0_config */

#define	PCR_CC_PHY_STAT_REG 0x00000810
/* sienaa0=pci_f0_config */

#define	PCRF_AC_SSL_LBN 3
#define	PCRF_AC_SSL_WIDTH 1
#define	PCRF_AC_SSR_LBN 2
#define	PCRF_AC_SSR_WIDTH 1
#define	PCRF_AC_SSCL_LBN 1
#define	PCRF_AC_SSCL_WIDTH 1
#define	PCRF_AC_SSCD_LBN 0
#define	PCRF_AC_SSCD_WIDTH 1


/*
 * PC_PHY_CTL_REG(32bit):
 * PHY control register
 */

#define	PCR_AB_PHY_CTL_REG 0x00000724
/* falcona0,falconb0=pci_f0_config */

#define	PCR_CC_PHY_CTL_REG 0x00000814
/* sienaa0=pci_f0_config */

#define	PCRF_AC_BD_LBN 31
#define	PCRF_AC_BD_WIDTH 1
#define	PCRF_AC_CDS_LBN 30
#define	PCRF_AC_CDS_WIDTH 1
#define	PCRF_AC_DWRAP_LB_LBN 29
#define	PCRF_AC_DWRAP_LB_WIDTH 1
#define	PCRF_AC_EBD_LBN 28
#define	PCRF_AC_EBD_WIDTH 1
#define	PCRF_AC_SNR_LBN 27
#define	PCRF_AC_SNR_WIDTH 1
#define	PCRF_AC_RX_NOT_DET_LBN 2
#define	PCRF_AC_RX_NOT_DET_WIDTH 1
#define	PCRF_AC_FORCE_LOS_VAL_LBN 1
#define	PCRF_AC_FORCE_LOS_VAL_WIDTH 1
#define	PCRF_AC_FORCE_LOS_EN_LBN 0
#define	PCRF_AC_FORCE_LOS_EN_WIDTH 1


/*
 * PC_DEBUG0_REG(32bit):
 * Debug register 0
 */

#define	PCR_AC_DEBUG0_REG 0x00000728
/* falcona0,falconb0,sienaa0=pci_f0_config */

#define	PCRF_AC_CDI03_LBN 24
#define	PCRF_AC_CDI03_WIDTH 8
#define	PCRF_AC_CDI0_LBN 0
#define	PCRF_AC_CDI0_WIDTH 32
#define	PCRF_AC_CDI02_LBN 16
#define	PCRF_AC_CDI02_WIDTH 8
#define	PCRF_AC_CDI01_LBN 8
#define	PCRF_AC_CDI01_WIDTH 8
#define	PCRF_AC_CDI00_LBN 0
#define	PCRF_AC_CDI00_WIDTH 8


/*
 * PC_DEBUG1_REG(32bit):
 * Debug register 1
 */

#define	PCR_AC_DEBUG1_REG 0x0000072c
/* falcona0,falconb0,sienaa0=pci_f0_config */

#define	PCRF_AC_CDI13_LBN 24
#define	PCRF_AC_CDI13_WIDTH 8
#define	PCRF_AC_CDI1_LBN 0
#define	PCRF_AC_CDI1_WIDTH 32
#define	PCRF_AC_CDI12_LBN 16
#define	PCRF_AC_CDI12_WIDTH 8
#define	PCRF_AC_CDI11_LBN 8
#define	PCRF_AC_CDI11_WIDTH 8
#define	PCRF_AC_CDI10_LBN 0
#define	PCRF_AC_CDI10_WIDTH 8


/*
 * PC_XPFCC_STAT_REG(24bit):
 * documentation to be written for sum_PC_XPFCC_STAT_REG
 */

#define	PCR_AC_XPFCC_STAT_REG 0x00000730
/* falcona0,falconb0,sienaa0=pci_f0_config */

#define	PCRF_AC_XPDC_LBN 12
#define	PCRF_AC_XPDC_WIDTH 8
#define	PCRF_AC_XPHC_LBN 0
#define	PCRF_AC_XPHC_WIDTH 12


/*
 * PC_XNPFCC_STAT_REG(24bit):
 * documentation to be written for sum_PC_XNPFCC_STAT_REG
 */

#define	PCR_AC_XNPFCC_STAT_REG 0x00000734
/* falcona0,falconb0,sienaa0=pci_f0_config */

#define	PCRF_AC_XNPDC_LBN 12
#define	PCRF_AC_XNPDC_WIDTH 8
#define	PCRF_AC_XNPHC_LBN 0
#define	PCRF_AC_XNPHC_WIDTH 12


/*
 * PC_XCFCC_STAT_REG(24bit):
 * documentation to be written for sum_PC_XCFCC_STAT_REG
 */

#define	PCR_AC_XCFCC_STAT_REG 0x00000738
/* falcona0,falconb0,sienaa0=pci_f0_config */

#define	PCRF_AC_XCDC_LBN 12
#define	PCRF_AC_XCDC_WIDTH 8
#define	PCRF_AC_XCHC_LBN 0
#define	PCRF_AC_XCHC_WIDTH 12


/*
 * PC_Q_STAT_REG(8bit):
 * documentation to be written for sum_PC_Q_STAT_REG
 */

#define	PCR_AC_Q_STAT_REG 0x0000073c
/* falcona0,falconb0,sienaa0=pci_f0_config */

#define	PCRF_AC_RQNE_LBN 2
#define	PCRF_AC_RQNE_WIDTH 1
#define	PCRF_AC_XRNE_LBN 1
#define	PCRF_AC_XRNE_WIDTH 1
#define	PCRF_AC_RCNR_LBN 0
#define	PCRF_AC_RCNR_WIDTH 1


/*
 * PC_VC_XMIT_ARB1_REG(32bit):
 * VC Transmit Arbitration Register 1
 */

#define	PCR_CC_VC_XMIT_ARB1_REG 0x00000740
/* sienaa0=pci_f0_config */



/*
 * PC_VC_XMIT_ARB2_REG(32bit):
 * VC Transmit Arbitration Register 2
 */

#define	PCR_CC_VC_XMIT_ARB2_REG 0x00000744
/* sienaa0=pci_f0_config */



/*
 * PC_VC0_P_RQ_CTL_REG(32bit):
 * VC0 Posted Receive Queue Control
 */

#define	PCR_CC_VC0_P_RQ_CTL_REG 0x00000748
/* sienaa0=pci_f0_config */



/*
 * PC_VC0_NP_RQ_CTL_REG(32bit):
 * VC0 Non-Posted Receive Queue Control
 */

#define	PCR_CC_VC0_NP_RQ_CTL_REG 0x0000074c
/* sienaa0=pci_f0_config */



/*
 * PC_VC0_C_RQ_CTL_REG(32bit):
 * VC0 Completion Receive Queue Control
 */

#define	PCR_CC_VC0_C_RQ_CTL_REG 0x00000750
/* sienaa0=pci_f0_config */



/*
 * PC_GEN2_REG(32bit):
 * Gen2 Register
 */

#define	PCR_CC_GEN2_REG 0x0000080c
/* sienaa0=pci_f0_config */

#define	PCRF_CC_SET_DE_EMPHASIS_LBN 20
#define	PCRF_CC_SET_DE_EMPHASIS_WIDTH 1
#define	PCRF_CC_CFG_TX_COMPLIANCE_LBN 19
#define	PCRF_CC_CFG_TX_COMPLIANCE_WIDTH 1
#define	PCRF_CC_CFG_TX_SWING_LBN 18
#define	PCRF_CC_CFG_TX_SWING_WIDTH 1
#define	PCRF_CC_DIR_SPEED_CHANGE_LBN 17
#define	PCRF_CC_DIR_SPEED_CHANGE_WIDTH 1
#define	PCRF_CC_LANE_ENABLE_LBN 8
#define	PCRF_CC_LANE_ENABLE_WIDTH 9
#define	PCRF_CC_NUM_FTS_LBN 0
#define	PCRF_CC_NUM_FTS_WIDTH 8


#ifdef	__cplusplus
}
#endif

#endif /* _SYS_EFX_REGS_PCI_H */
