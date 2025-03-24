/* SPDX-License-Identifier: (GPL-2.0-only OR BSD-2-Clause) */
/*
 * Qualcomm MSM8976 interconnect IDs
 */

#ifndef __DT_BINDINGS_INTERCONNECT_QCOM_MSM8976_H
#define __DT_BINDINGS_INTERCONNECT_QCOM_MSM8976_H

/* BIMC fabric */
#define MAS_APPS_PROC		0
#define MAS_SMMNOC_BIMC	        1
#define MAS_SNOC_BIMC		2
#define MAS_TCU_0		3
#define SLV_EBI		        4
#define SLV_BIMC_SNOC		5

/* PCNOC fabric */
#define MAS_USB_HS2		0
#define MAS_BLSP_1		1
#define MAS_USB_HS1		2
#define MAS_BLSP_2		3
#define MAS_CRYPTO		4
#define MAS_SDCC_1		5
#define MAS_SDCC_2		6
#define MAS_SDCC_3		7
#define MAS_SNOC_PCNOC		8
#define MAS_LPASS_AHB		9
#define MAS_SPDM		10
#define MAS_DEHR		11
#define MAS_XM_USB_HS1		12
#define PCNOC_M_0		13
#define PCNOC_M_1		14
#define PCNOC_INT_0		15
#define PCNOC_INT_1		16
#define PCNOC_INT_2		17
#define PCNOC_S_1		18
#define PCNOC_S_2		19
#define PCNOC_S_3		20
#define PCNOC_S_4		21
#define PCNOC_S_8		22
#define PCNOC_S_9		23
#define SLV_TCSR		24
#define SLV_TLMM		25
#define SLV_CRYPTO_0_CFG	26
#define SLV_MESSAGE_RAM	        27
#define SLV_PDM		        28
#define SLV_PRNG		29
#define SLV_PMIC_ARB		30
#define SLV_SNOC_CFG		31
#define SLV_DCC_CFG		32
#define SLV_CAMERA_SS_CFG	33
#define SLV_DISP_SS_CFG	        34
#define SLV_VENUS_CFG		35
#define SLV_SDCC_1		36
#define SLV_BLSP_1		37
#define SLV_USB_HS		38
#define SLV_SDCC_3		39
#define SLV_SDCC_2		40
#define SLV_GPU_CFG		41
#define SLV_USB_HS2		42
#define SLV_BLSP_2		43
#define SLV_PCNOC_SNOC		44

/* SNOC fabric */
#define MAS_QDSS_BAM		0
#define MAS_BIMC_SNOC		1
#define MAS_PCNOC_SNOC		2
#define MAS_QDSS_ETR		3
#define MAS_LPASS_PROC		4
#define MAS_IPA		        5
#define QDSS_INT		6
#define SNOC_INT_0		7
#define SNOC_INT_1		8
#define SNOC_INT_2		9
#define SLV_KPSS_AHB		10
#define SLV_SNOC_BIMC		11
#define SLV_IMEM		12
#define SLV_SNOC_PCNOC		13
#define SLV_QDSS_STM		14
#define SLV_CATS_0		15
#define SLV_CATS_1		16
#define SLV_LPASS		17

/* SNOC-MM fabric */
#define MAS_JPEG		0
#define MAS_OXILI		1
#define MAS_MDP0		2
#define MAS_MDP1		3
#define MAS_VENUS_0		4
#define MAS_VENUS_1		5
#define MAS_VFE_0		6
#define MAS_VFE_1		7
#define MAS_CPP		        8
#define MM_INT_0		9
#define SLV_SMMNOC_BIMC		10

#endif /* __DT_BINDINGS_INTERCONNECT_QCOM_MSM8976_H */
