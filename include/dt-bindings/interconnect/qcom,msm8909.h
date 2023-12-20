/* SPDX-License-Identifier: (GPL-2.0-only OR BSD-2-Clause) */
/*
 * Qualcomm MSM8909 interconnect IDs
 */

#ifndef __DT_BINDINGS_INTERCONNECT_QCOM_MSM8909_H
#define __DT_BINDINGS_INTERCONNECT_QCOM_MSM8909_H

/* BIMC fabric */
#define MAS_APPS_PROC			0
#define MAS_OXILI			1
#define MAS_SNOC_BIMC_0			2
#define MAS_SNOC_BIMC_1			3
#define MAS_TCU_0			4
#define MAS_TCU_1			5
#define SLV_EBI				6
#define SLV_BIMC_SNOC			7

/* PCNOC fabric */
#define MAS_AUDIO			0
#define MAS_SPDM			1
#define MAS_DEHR			2
#define MAS_QPIC			3
#define MAS_BLSP_1			4
#define MAS_USB_HS			5
#define MAS_CRYPTO			6
#define MAS_SDCC_1			7
#define MAS_SDCC_2			8
#define MAS_SNOC_PCNOC			9
#define PCNOC_M_0			10
#define PCNOC_M_1			11
#define PCNOC_INT_0			12
#define PCNOC_INT_1			13
#define PCNOC_S_0			14
#define PCNOC_S_1			15
#define PCNOC_S_2			16
#define PCNOC_S_3			17
#define PCNOC_S_4			18
#define PCNOC_S_5			19
#define PCNOC_S_7			20
#define SLV_TCSR			21
#define SLV_SDCC_1			22
#define SLV_BLSP_1			23
#define SLV_CRYPTO_0_CFG		24
#define SLV_MESSAGE_RAM			25
#define SLV_PDM				26
#define SLV_PRNG			27
#define SLV_USB_HS			28
#define SLV_QPIC			29
#define SLV_SPDM			30
#define SLV_SDCC_2			31
#define SLV_AUDIO			32
#define SLV_DEHR_CFG			33
#define SLV_SNOC_CFG			34
#define SLV_QDSS_CFG			35
#define SLV_USB_PHY			36
#define SLV_CAMERA_SS_CFG		37
#define SLV_DISP_SS_CFG			38
#define SLV_VENUS_CFG			39
#define SLV_TLMM			40
#define SLV_GPU_CFG			41
#define SLV_IMEM_CFG			42
#define SLV_BIMC_CFG			43
#define SLV_PMIC_ARB			44
#define SLV_TCU				45
#define SLV_PCNOC_SNOC			46

/* SNOC fabric */
#define MAS_QDSS_BAM			0
#define MAS_BIMC_SNOC			1
#define MAS_MDP				2
#define MAS_PCNOC_SNOC			3
#define MAS_VENUS			4
#define MAS_VFE				5
#define MAS_QDSS_ETR			6
#define MM_INT_0			7
#define MM_INT_1			8
#define MM_INT_2			9
#define MM_INT_BIMC			10
#define QDSS_INT			11
#define SNOC_INT_0			12
#define SNOC_INT_1			13
#define SNOC_INT_BIMC			14
#define SLV_KPSS_AHB			15
#define SLV_SNOC_BIMC_0			16
#define SLV_SNOC_BIMC_1			17
#define SLV_IMEM			18
#define SLV_SNOC_PCNOC			19
#define SLV_QDSS_STM			20
#define SLV_CATS_0			21
#define SLV_CATS_1			22

#endif /* __DT_BINDINGS_INTERCONNECT_QCOM_MSM8909_H */
