/* SPDX-License-Identifier: (GPL-2.0-only OR BSD-2-Clause) */
/*
 * Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries.
 */

#ifndef _DT_BINDINGS_CLK_QCOM_CAM_CC_KAANAPALI_H
#define _DT_BINDINGS_CLK_QCOM_CAM_CC_KAANAPALI_H

/* CAM_CC clocks */
#define CAM_CC_CAM_TOP_AHB_CLK					0
#define CAM_CC_CAM_TOP_FAST_AHB_CLK				1
#define CAM_CC_CAMNOC_DCD_XO_CLK				2
#define CAM_CC_CAMNOC_NRT_AXI_CLK				3
#define CAM_CC_CAMNOC_NRT_CRE_CLK				4
#define CAM_CC_CAMNOC_NRT_IPE_NPS_CLK				5
#define CAM_CC_CAMNOC_NRT_OFE_MAIN_CLK				6
#define CAM_CC_CAMNOC_RT_AXI_CLK				7
#define CAM_CC_CAMNOC_RT_AXI_CLK_SRC				8
#define CAM_CC_CAMNOC_RT_IFE_LITE_CLK				9
#define CAM_CC_CAMNOC_RT_TFE_0_MAIN_CLK				10
#define CAM_CC_CAMNOC_RT_TFE_1_MAIN_CLK				11
#define CAM_CC_CAMNOC_RT_TFE_2_MAIN_CLK				12
#define CAM_CC_CAMNOC_XO_CLK					13
#define CAM_CC_CCI_0_CLK					14
#define CAM_CC_CCI_0_CLK_SRC					15
#define CAM_CC_CCI_1_CLK					16
#define CAM_CC_CCI_1_CLK_SRC					17
#define CAM_CC_CCI_2_CLK					18
#define CAM_CC_CCI_2_CLK_SRC					19
#define CAM_CC_CORE_AHB_CLK					20
#define CAM_CC_CPHY_RX_CLK_SRC					21
#define CAM_CC_CRE_AHB_CLK					22
#define CAM_CC_CRE_CLK						23
#define CAM_CC_CRE_CLK_SRC					24
#define CAM_CC_CSI0PHYTIMER_CLK					25
#define CAM_CC_CSI0PHYTIMER_CLK_SRC				26
#define CAM_CC_CSI1PHYTIMER_CLK					27
#define CAM_CC_CSI1PHYTIMER_CLK_SRC				28
#define CAM_CC_CSI2PHYTIMER_CLK					29
#define CAM_CC_CSI2PHYTIMER_CLK_SRC				30
#define CAM_CC_CSI3PHYTIMER_CLK					31
#define CAM_CC_CSI3PHYTIMER_CLK_SRC				32
#define CAM_CC_CSI4PHYTIMER_CLK					33
#define CAM_CC_CSI4PHYTIMER_CLK_SRC				34
#define CAM_CC_CSI5PHYTIMER_CLK					35
#define CAM_CC_CSI5PHYTIMER_CLK_SRC				36
#define CAM_CC_CSID_CLK						37
#define CAM_CC_CSID_CLK_SRC					38
#define CAM_CC_CSID_CSIPHY_RX_CLK				39
#define CAM_CC_CSIPHY0_CLK					40
#define CAM_CC_CSIPHY1_CLK					41
#define CAM_CC_CSIPHY2_CLK					42
#define CAM_CC_CSIPHY3_CLK					43
#define CAM_CC_CSIPHY4_CLK					44
#define CAM_CC_CSIPHY5_CLK					45
#define CAM_CC_DRV_AHB_CLK					46
#define CAM_CC_DRV_XO_CLK					47
#define CAM_CC_FAST_AHB_CLK_SRC					48
#define CAM_CC_GDSC_CLK						49
#define CAM_CC_ICP_0_AHB_CLK					50
#define CAM_CC_ICP_0_CLK					51
#define CAM_CC_ICP_0_CLK_SRC					52
#define CAM_CC_ICP_1_AHB_CLK					53
#define CAM_CC_ICP_1_CLK					54
#define CAM_CC_ICP_1_CLK_SRC					55
#define CAM_CC_IFE_LITE_AHB_CLK					56
#define CAM_CC_IFE_LITE_CLK					57
#define CAM_CC_IFE_LITE_CLK_SRC					58
#define CAM_CC_IFE_LITE_CPHY_RX_CLK				59
#define CAM_CC_IFE_LITE_CSID_CLK				60
#define CAM_CC_IFE_LITE_CSID_CLK_SRC				61
#define CAM_CC_IPE_NPS_AHB_CLK					62
#define CAM_CC_IPE_NPS_CLK					63
#define CAM_CC_IPE_NPS_CLK_SRC					64
#define CAM_CC_IPE_NPS_FAST_AHB_CLK				65
#define CAM_CC_IPE_PPS_CLK					66
#define CAM_CC_IPE_PPS_FAST_AHB_CLK				67
#define CAM_CC_JPEG_CLK						68
#define CAM_CC_JPEG_CLK_SRC					69
#define CAM_CC_OFE_AHB_CLK					70
#define CAM_CC_OFE_ANCHOR_CLK					71
#define CAM_CC_OFE_ANCHOR_FAST_AHB_CLK				72
#define CAM_CC_OFE_CLK_SRC					73
#define CAM_CC_OFE_HDR_CLK					74
#define CAM_CC_OFE_HDR_FAST_AHB_CLK				75
#define CAM_CC_OFE_MAIN_CLK					76
#define CAM_CC_OFE_MAIN_FAST_AHB_CLK				77
#define CAM_CC_PLL0						78
#define CAM_CC_PLL0_OUT_EVEN					79
#define CAM_CC_PLL0_OUT_ODD					80
#define CAM_CC_PLL1						81
#define CAM_CC_PLL1_OUT_EVEN					82
#define CAM_CC_PLL2						83
#define CAM_CC_PLL2_OUT_EVEN					84
#define CAM_CC_PLL3						85
#define CAM_CC_PLL3_OUT_EVEN					86
#define CAM_CC_PLL4						87
#define CAM_CC_PLL4_OUT_EVEN					88
#define CAM_CC_PLL5						89
#define CAM_CC_PLL5_OUT_EVEN					90
#define CAM_CC_PLL6						91
#define CAM_CC_PLL6_OUT_EVEN					92
#define CAM_CC_PLL6_OUT_ODD					93
#define CAM_CC_PLL7						94
#define CAM_CC_PLL7_OUT_EVEN					95
#define CAM_CC_QDSS_DEBUG_CLK					96
#define CAM_CC_QDSS_DEBUG_CLK_SRC				97
#define CAM_CC_QDSS_DEBUG_XO_CLK				98
#define CAM_CC_SLEEP_CLK					99
#define CAM_CC_SLOW_AHB_CLK_SRC					100
#define CAM_CC_TFE_0_BAYER_CLK					101
#define CAM_CC_TFE_0_BAYER_FAST_AHB_CLK				102
#define CAM_CC_TFE_0_CLK_SRC					103
#define CAM_CC_TFE_0_MAIN_CLK					104
#define CAM_CC_TFE_0_MAIN_FAST_AHB_CLK				105
#define CAM_CC_TFE_1_BAYER_CLK					106
#define CAM_CC_TFE_1_BAYER_FAST_AHB_CLK				107
#define CAM_CC_TFE_1_CLK_SRC					108
#define CAM_CC_TFE_1_MAIN_CLK					109
#define CAM_CC_TFE_1_MAIN_FAST_AHB_CLK				110
#define CAM_CC_TFE_2_BAYER_CLK					111
#define CAM_CC_TFE_2_BAYER_FAST_AHB_CLK				112
#define CAM_CC_TFE_2_CLK_SRC					113
#define CAM_CC_TFE_2_MAIN_CLK					114
#define CAM_CC_TFE_2_MAIN_FAST_AHB_CLK				115
#define CAM_CC_TRACENOC_TPDM_1_CMB_CLK				116
#define CAM_CC_XO_CLK_SRC					117

/* CAM_CC power domains */
#define CAM_CC_IPE_0_GDSC					0
#define CAM_CC_OFE_GDSC						1
#define CAM_CC_TFE_0_GDSC					2
#define CAM_CC_TFE_1_GDSC					3
#define CAM_CC_TFE_2_GDSC					4
#define CAM_CC_TITAN_TOP_GDSC					5

/* CAM_CC resets */
#define CAM_CC_DRV_BCR						0
#define CAM_CC_ICP_BCR						1
#define CAM_CC_IPE_0_BCR					2
#define CAM_CC_OFE_BCR						3
#define CAM_CC_QDSS_DEBUG_BCR					4
#define CAM_CC_TFE_0_BCR					5
#define CAM_CC_TFE_1_BCR					6
#define CAM_CC_TFE_2_BCR					7

#endif
