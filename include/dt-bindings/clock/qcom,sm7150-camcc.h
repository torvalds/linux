/* SPDX-License-Identifier: (GPL-2.0-only OR BSD-2-Clause) */
/*
 * Copyright (c) 2018, The Linux Foundation. All rights reserved.
 * Copyright (c) 2024, Danila Tikhonov <danila@jiaxyga.com>
 */

#ifndef _DT_BINDINGS_CLK_QCOM_CAMCC_SM7150_H
#define _DT_BINDINGS_CLK_QCOM_CAMCC_SM7150_H

/* Hardware clocks */
#define CAMCC_PLL0_OUT_EVEN					0
#define CAMCC_PLL0_OUT_ODD					1
#define CAMCC_PLL1_OUT_EVEN					2
#define CAMCC_PLL2_OUT_EARLY					3
#define CAMCC_PLL3_OUT_EVEN					4
#define CAMCC_PLL4_OUT_EVEN					5

/* CAMCC clock registers */
#define CAMCC_PLL0						6
#define CAMCC_PLL1						7
#define CAMCC_PLL2						8
#define CAMCC_PLL2_OUT_AUX					9
#define CAMCC_PLL2_OUT_MAIN					10
#define CAMCC_PLL3						11
#define CAMCC_PLL4						12
#define CAMCC_BPS_AHB_CLK					13
#define CAMCC_BPS_AREG_CLK					14
#define CAMCC_BPS_AXI_CLK					15
#define CAMCC_BPS_CLK						16
#define CAMCC_BPS_CLK_SRC					17
#define CAMCC_CAMNOC_AXI_CLK					18
#define CAMCC_CAMNOC_AXI_CLK_SRC				19
#define CAMCC_CAMNOC_DCD_XO_CLK					20
#define CAMCC_CCI_0_CLK						21
#define CAMCC_CCI_0_CLK_SRC					22
#define CAMCC_CCI_1_CLK						23
#define CAMCC_CCI_1_CLK_SRC					24
#define CAMCC_CORE_AHB_CLK					25
#define CAMCC_CPAS_AHB_CLK					26
#define CAMCC_CPHY_RX_CLK_SRC					27
#define CAMCC_CSI0PHYTIMER_CLK					28
#define CAMCC_CSI0PHYTIMER_CLK_SRC				29
#define CAMCC_CSI1PHYTIMER_CLK					30
#define CAMCC_CSI1PHYTIMER_CLK_SRC				31
#define CAMCC_CSI2PHYTIMER_CLK					32
#define CAMCC_CSI2PHYTIMER_CLK_SRC				33
#define CAMCC_CSI3PHYTIMER_CLK					34
#define CAMCC_CSI3PHYTIMER_CLK_SRC				35
#define CAMCC_CSIPHY0_CLK					36
#define CAMCC_CSIPHY1_CLK					37
#define CAMCC_CSIPHY2_CLK					38
#define CAMCC_CSIPHY3_CLK					39
#define CAMCC_FAST_AHB_CLK_SRC					40
#define CAMCC_FD_CORE_CLK					41
#define CAMCC_FD_CORE_CLK_SRC					42
#define CAMCC_FD_CORE_UAR_CLK					43
#define CAMCC_ICP_AHB_CLK					44
#define CAMCC_ICP_CLK						45
#define CAMCC_ICP_CLK_SRC					46
#define CAMCC_IFE_0_AXI_CLK					47
#define CAMCC_IFE_0_CLK						48
#define CAMCC_IFE_0_CLK_SRC					49
#define CAMCC_IFE_0_CPHY_RX_CLK					50
#define CAMCC_IFE_0_CSID_CLK					51
#define CAMCC_IFE_0_CSID_CLK_SRC				52
#define CAMCC_IFE_0_DSP_CLK					53
#define CAMCC_IFE_1_AXI_CLK					54
#define CAMCC_IFE_1_CLK						55
#define CAMCC_IFE_1_CLK_SRC					56
#define CAMCC_IFE_1_CPHY_RX_CLK					57
#define CAMCC_IFE_1_CSID_CLK					58
#define CAMCC_IFE_1_CSID_CLK_SRC				59
#define CAMCC_IFE_1_DSP_CLK					60
#define CAMCC_IFE_LITE_CLK					61
#define CAMCC_IFE_LITE_CLK_SRC					62
#define CAMCC_IFE_LITE_CPHY_RX_CLK				63
#define CAMCC_IFE_LITE_CSID_CLK					64
#define CAMCC_IFE_LITE_CSID_CLK_SRC				65
#define CAMCC_IPE_0_AHB_CLK					66
#define CAMCC_IPE_0_AREG_CLK					67
#define CAMCC_IPE_0_AXI_CLK					68
#define CAMCC_IPE_0_CLK						69
#define CAMCC_IPE_0_CLK_SRC					70
#define CAMCC_IPE_1_AHB_CLK					71
#define CAMCC_IPE_1_AREG_CLK					72
#define CAMCC_IPE_1_AXI_CLK					73
#define CAMCC_IPE_1_CLK						74
#define CAMCC_JPEG_CLK						75
#define CAMCC_JPEG_CLK_SRC					76
#define CAMCC_LRME_CLK						77
#define CAMCC_LRME_CLK_SRC					78
#define CAMCC_MCLK0_CLK						79
#define CAMCC_MCLK0_CLK_SRC					80
#define CAMCC_MCLK1_CLK						81
#define CAMCC_MCLK1_CLK_SRC					82
#define CAMCC_MCLK2_CLK						83
#define CAMCC_MCLK2_CLK_SRC					84
#define CAMCC_MCLK3_CLK						85
#define CAMCC_MCLK3_CLK_SRC					86
#define CAMCC_SLEEP_CLK						87
#define CAMCC_SLEEP_CLK_SRC					88
#define CAMCC_SLOW_AHB_CLK_SRC					89
#define CAMCC_XO_CLK_SRC					90

/* CAMCC GDSCRs */
#define BPS_GDSC						0
#define IFE_0_GDSC						1
#define IFE_1_GDSC						2
#define IPE_0_GDSC						3
#define IPE_1_GDSC						4
#define TITAN_TOP_GDSC						5

#endif
