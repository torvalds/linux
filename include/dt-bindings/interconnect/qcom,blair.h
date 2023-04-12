/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (c) 2023, Qualcomm Innovation Center, Inc. All rights reserved.
 */

#ifndef __DT_BINDINGS_INTERCONNECT_QCOM_BLAIR_H
#define __DT_BINDINGS_INTERCONNECT_QCOM_BLAIR_H

#define MASTER_AMPSS_M0				0
#define MASTER_SNOC_BIMC_RT				1
#define MASTER_SNOC_BIMC_NRT				2
#define SNOC_BIMC_MAS				3
#define MASTER_GRAPHICS_3D				4
#define MASTER_CDSP_PROC				5
#define MASTER_TCU_0				6
#define MASTER_QUP_CORE_0				7
#define MASTER_QUP_CORE_1				8
#define MASTER_CRYPTO_CORE0				9
#define SNOC_CNOC_MAS				10
#define MASTER_QDSS_DAP				11
#define MASTER_CAMNOC_SF				12
#define MASTER_CAMNOC_HF				13
#define MASTER_MDP_PORT0				14
#define MASTER_VIDEO_P0				15
#define MASTER_VIDEO_PROC				16
#define MASTER_SNOC_CFG				17
#define MASTER_TIC				18
#define A1NOC_SNOC_MAS				19
#define A2NOC_SNOC_MAS				20
#define BIMC_SNOC_MAS				21
#define MASTER_PIMEM				22
#define MASTER_QUP_0				23
#define MASTER_QUP_1				24
#define MASTER_EMMC				25
#define MASTER_SDCC_2				26
#define MASTER_UFS_MEM				27
#define MASTER_QDSS_BAM				28
#define MASTER_IPA				29
#define MASTER_QDSS_ETR				30
#define MASTER_USB3_0				31
#define MASTER_CAMNOC_SF_SNOC				32
#define MASTER_CAMNOC_HF_SNOC				33
#define MASTER_MDP_PORT0_SNOC				34
#define MASTER_VIDEO_P0_SNOC				35
#define MASTER_VIDEO_PROC_SNOC				36
#define MASTER_SNOC_RT				37
#define MASTER_SNOC_NRT				38

#define SLAVE_EBI				512
#define BIMC_SNOC_SLV				513
#define SLAVE_PKA_CORE				514
#define SLAVE_QUP_CORE_0				515
#define SLAVE_QUP_CORE_1				516
#define SLAVE_BIMC_CFG				517
#define SLAVE_APPSS				518
#define SLAVE_CAMERA_NRT_THROTTLE_CFG				519
#define SLAVE_CAMERA_RT_THROTTLE_CFG				520
#define SLAVE_CAMERA_CFG				521
#define SLAVE_CLK_CTL				522
#define SLAVE_DSP_CFG				523
#define SLAVE_RBCPR_CX_CFG				524
#define SLAVE_RBCPR_MX_CFG				525
#define SLAVE_CRYPTO_0_CFG				526
#define SLAVE_DCC_CFG				527
#define SLAVE_DDR_PHY_CFG				528
#define SLAVE_DDR_SS_CFG				529
#define SLAVE_DISPLAY_CFG				530
#define SLAVE_DISPLAY_THROTTLE_CFG				531
#define SLAVE_EMMC_CFG				532
#define SLAVE_GRAPHICS_3D_CFG				533
#define SLAVE_HWKM				534
#define SLAVE_IMEM_CFG				535
#define SLAVE_IPA_CFG				536
#define SLAVE_LPASS				537
#define SLAVE_MAPSS				538
#define SLAVE_MESSAGE_RAM				539
#define SLAVE_PDM				540
#define SLAVE_PIMEM_CFG				541
#define SLAVE_PMIC_ARB				542
#define SLAVE_QDSS_CFG				543
#define SLAVE_QM_CFG				544
#define SLAVE_QM_MPU_CFG				545
#define SLAVE_QUP_0				546
#define SLAVE_QUP_1				547
#define SLAVE_RPM				548
#define SLAVE_SDCC_2				549
#define SLAVE_SECURITY				550
#define SLAVE_SNOC_CFG				551
#define SLAVE_TCSR				552
#define SLAVE_TLMM				553
#define SLAVE_UFS_MEM_CFG				554
#define SLAVE_USB3				555
#define SLAVE_VENUS_CFG				556
#define SLAVE_VENUS_THROTTLE_CFG				557
#define SLAVE_VSENSE_CTRL_CFG				558
#define SLAVE_SNOC_BIMC_NRT				559
#define SLAVE_SNOC_BIMC_RT				560
#define SNOC_CNOC_SLV				561
#define SLAVE_OCIMEM				562
#define SLAVE_PIMEM				563
#define SNOC_BIMC_SLV				564
#define SLAVE_SERVICE_SNOC				565
#define SLAVE_QDSS_STM				566
#define A1NOC_SNOC_SLV				567
#define A2NOC_SNOC_SLV				568
#define SLAVE_CAMNOC_HF_SNOC				569
#define SLAVE_MDP_PORT0_SNOC				570
#define SLAVE_CAMNOC_SF_SNOC				571
#define SLAVE_VIDEO_P0_SNOC				572
#define SLAVE_VIDEO_PROC_SNOC				573
#define SLAVE_SNOC_RT				574
#define SLAVE_SNOC_NRT				575
#define SLAVE_TCU				576

#endif
