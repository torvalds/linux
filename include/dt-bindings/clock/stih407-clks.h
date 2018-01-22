/* SPDX-License-Identifier: GPL-2.0 */
/*
 * This header provides constants clk index STMicroelectronics
 * STiH407 SoC.
 */
#ifndef _DT_BINDINGS_CLK_STIH407
#define _DT_BINDINGS_CLK_STIH407

/* CLOCKGEN A0 */
#define CLK_IC_LMI0		0
#define CLK_IC_LMI1		1

/* CLOCKGEN C0 */
#define CLK_ICN_GPU		0
#define CLK_FDMA		1
#define CLK_NAND		2
#define CLK_HVA			3
#define CLK_PROC_STFE		4
#define CLK_PROC_TP		5
#define CLK_RX_ICN_DMU		6
#define CLK_RX_ICN_DISP_0	6
#define CLK_RX_ICN_DISP_1	6
#define CLK_RX_ICN_HVA		7
#define CLK_RX_ICN_TS		7
#define CLK_ICN_CPU		8
#define CLK_TX_ICN_DMU		9
#define CLK_TX_ICN_HVA		9
#define CLK_TX_ICN_TS		9
#define CLK_ICN_COMPO		9
#define CLK_MMC_0		10
#define CLK_MMC_1		11
#define CLK_JPEGDEC		12
#define CLK_ICN_REG		13
#define CLK_TRACE_A9		13
#define CLK_PTI_STM		13
#define CLK_EXT2F_A9		13
#define CLK_IC_BDISP_0		14
#define CLK_IC_BDISP_1		15
#define CLK_PP_DMU		16
#define CLK_VID_DMU		17
#define CLK_DSS_LPC		18
#define CLK_ST231_AUD_0		19
#define CLK_ST231_GP_0		19
#define CLK_ST231_GP_1		20
#define CLK_ST231_DMU		21
#define CLK_ICN_LMI		22
#define CLK_TX_ICN_DISP_0	23
#define CLK_TX_ICN_DISP_1	23
#define CLK_ICN_SBC		24
#define CLK_STFE_FRC2		25
#define CLK_ETH_PHY		26
#define CLK_ETH_REF_PHYCLK	27
#define CLK_FLASH_PROMIP	28
#define CLK_MAIN_DISP		29
#define CLK_AUX_DISP		30
#define CLK_COMPO_DVP		31

/* CLOCKGEN D0 */
#define CLK_PCM_0		0
#define CLK_PCM_1		1
#define CLK_PCM_2		2
#define CLK_SPDIFF		3

/* CLOCKGEN D2 */
#define CLK_PIX_MAIN_DISP	0
#define CLK_PIX_PIP		1
#define CLK_PIX_GDP1		2
#define CLK_PIX_GDP2		3
#define CLK_PIX_GDP3		4
#define CLK_PIX_GDP4		5
#define CLK_PIX_AUX_DISP	6
#define CLK_DENC		7
#define CLK_PIX_HDDAC		8
#define CLK_HDDAC		9
#define CLK_SDDAC		10
#define CLK_PIX_DVO		11
#define CLK_DVO			12
#define CLK_PIX_HDMI		13
#define CLK_TMDS_HDMI		14
#define CLK_REF_HDMIPHY		15

/* CLOCKGEN D3 */
#define CLK_STFE_FRC1		0
#define CLK_TSOUT_0		1
#define CLK_TSOUT_1		2
#define CLK_MCHI		3
#define CLK_VSENS_COMPO		4
#define CLK_FRC1_REMOTE		5
#define CLK_LPC_0		6
#define CLK_LPC_1		7
#endif
