/* SPDX-License-Identifier: (GPL-2.0-only OR BSD-2-Clause) */
/*
 * Copyright (c) 2023, Qualcomm Innovation Center, Inc. All rights reserved.
 */

#ifndef __DT_BINDINGS_INTERCONNECT_QCOM_PITTI_H
#define __DT_BINDINGS_INTERCONNECT_QCOM_PITTI_H

/*
 * ID's used in RPM messages
 */
#define MASTER_AMPSS_M0				0
#define MASTER_SNOC_BIMC_RT				1
#define MASTER_SNOC_BIMC_NRT				2
#define SNOC_BIMC_MAS				3
#define MASTER_GRAPHICS_3D				4
#define MASTER_TCU_0				5
#define MASTER_QUP_CORE_0				6
#define MASTER_QUP_CORE_1				7
#define SNOC_CNOC_MAS				8
#define MASTER_QDSS_DAP				9
#define MASTER_CAMNOC_SF				10
#define MASTER_VIDEO_P0				11
#define MASTER_VIDEO_PROC				12
#define MASTER_CAMNOC_HF				13
#define MASTER_MDP_PORT0				14
#define MASTER_TIC				15
#define A0NOC_SNOC_MAS				16
#define BIMC_SNOC_MAS				17
#define MASTER_PIMEM				18
#define MASTER_QUP_0				19
#define MASTER_QUP_1				20
#define MASTER_CRYPTO_CORE0				21
#define MASTER_IPA				22
#define MASTER_QDSS_ETR				23
#define MASTER_SDCC_1				24
#define MASTER_SDCC_2				25
#define MASTER_UFS_MEM				26
#define MASTER_USB3				27
#define SLAVE_EBI_CH0				512
#define BIMC_SNOC_SLV				513
#define SLAVE_QUP_CORE_0				514
#define SLAVE_QUP_CORE_1				515
#define SLAVE_AHB2PHY_USB				516
#define SLAVE_BIMC_CFG				517
#define SLAVE_BOOT_ROM				518
#define SLAVE_CAMERA_NRT_THROTTLE_CFG				519
#define SLAVE_CAMERA_RT_THROTTLE_CFG				520
#define SLAVE_CAMERA_CFG				521
#define SLAVE_CLK_CTL				522
#define SLAVE_RBCPR_CX_CFG				523
#define SLAVE_RBCPR_MX_CFG				524
#define SLAVE_CRYPTO_0_CFG				525
#define SLAVE_DDR_PHY_CFG				526
#define SLAVE_DDR_SS_CFG				527
#define SLAVE_DISPLAY_CFG				528
#define SLAVE_DISPLAY_THROTTLE_CFG				529
#define SLAVE_GPU_CFG				530
#define SLAVE_IMEM_CFG				531
#define SLAVE_IPA_CFG				532
#define SLAVE_MAPSS				533
#define SLAVE_MDSP_MPU_CFG				534
#define SLAVE_MESSAGE_RAM				535
#define SLAVE_CNOC_MSS				536
#define SLAVE_PDM				537
#define SLAVE_PIMEM_CFG				538
#define SLAVE_PMIC_ARB				539
#define SLAVE_PRNG				540
#define SLAVE_QDSS_CFG				541
#define SLAVE_QM_CFG				542
#define SLAVE_QM_MPU_CFG				543
#define SLAVE_QUP_0				544
#define SLAVE_RPM				545
#define SLAVE_SDCC_1				546
#define SLAVE_SDCC_2				547
#define SLAVE_SECURITY				548
#define SLAVE_TCSR				549
#define SLAVE_UFS_MEM_CFG				550
#define SLAVE_USB3				551
#define SLAVE_VENUS_CFG				552
#define SLAVE_VENUS_THROTTLE_CFG				553
#define SLAVE_VSENSE_CTRL_CFG				554
#define SLAVE_MCDMA_MPU_CFG				555
#define SLAVE_LPASS				556
#define DDRSS_THROTTLE_CFG				557
#define SLAVE_SERVICE_CNOC				558
#define SLAVE_QUP_1				559
#define SLAVE_SNOC_BIMC_NRT				560
#define SLAVE_SNOC_BIMC_RT				561
#define SLAVE_APPSS				562
#define SNOC_CNOC_SLV				563
#define SLAVE_OCIMEM				564
#define SLAVE_PIMEM				565
#define SNOC_BIMC_SLV				566
#define SLAVE_QDSS_STM				567
#define SLAVE_TCU				568
#define A0NOC_SNOC_SLV				569

#endif

