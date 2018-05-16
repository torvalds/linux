// SPDX-License-Identifier: (GPL-2.0+ OR MIT)
//
// Spreadtrum SC9860 platform clocks
//
// Copyright (C) 2017, Spreadtrum Communications Inc.

#ifndef _DT_BINDINGS_CLK_SC9860_H_
#define _DT_BINDINGS_CLK_SC9860_H_

#define	CLK_FAC_4M		0
#define	CLK_FAC_2M		1
#define	CLK_FAC_1M		2
#define	CLK_FAC_250K		3
#define	CLK_FAC_RPLL0_26M	4
#define	CLK_FAC_RPLL1_26M	5
#define	CLK_FAC_RCO25M		6
#define	CLK_FAC_RCO4M		7
#define	CLK_FAC_RCO2M		8
#define	CLK_FAC_3K2		9
#define	CLK_FAC_1K		10
#define	CLK_MPLL0_GATE		11
#define	CLK_MPLL1_GATE		12
#define	CLK_DPLL0_GATE		13
#define	CLK_DPLL1_GATE		14
#define	CLK_LTEPLL0_GATE	15
#define	CLK_TWPLL_GATE		16
#define	CLK_LTEPLL1_GATE	17
#define	CLK_RPLL0_GATE		18
#define	CLK_RPLL1_GATE		19
#define	CLK_CPPLL_GATE		20
#define	CLK_GPLL_GATE		21
#define CLK_PMU_GATE_NUM	(CLK_GPLL_GATE + 1)

#define	CLK_MPLL0		0
#define	CLK_MPLL1		1
#define	CLK_DPLL0		2
#define	CLK_DPLL1		3
#define	CLK_RPLL0		4
#define	CLK_RPLL1		5
#define	CLK_TWPLL		6
#define	CLK_LTEPLL0		7
#define	CLK_LTEPLL1		8
#define	CLK_GPLL		9
#define	CLK_CPPLL		10
#define	CLK_GPLL_42M5		11
#define	CLK_TWPLL_768M		12
#define	CLK_TWPLL_384M		13
#define	CLK_TWPLL_192M		14
#define	CLK_TWPLL_96M		15
#define	CLK_TWPLL_48M		16
#define	CLK_TWPLL_24M		17
#define	CLK_TWPLL_12M		18
#define	CLK_TWPLL_512M		19
#define	CLK_TWPLL_256M		20
#define	CLK_TWPLL_128M		21
#define	CLK_TWPLL_64M		22
#define	CLK_TWPLL_307M2		23
#define	CLK_TWPLL_153M6		24
#define	CLK_TWPLL_76M8		25
#define	CLK_TWPLL_51M2		26
#define	CLK_TWPLL_38M4		27
#define	CLK_TWPLL_19M2		28
#define	CLK_L0_614M4		29
#define	CLK_L0_409M6		30
#define	CLK_L0_38M		31
#define	CLK_L1_38M		32
#define	CLK_RPLL0_192M		33
#define	CLK_RPLL0_96M		34
#define	CLK_RPLL0_48M		35
#define	CLK_RPLL1_468M		36
#define	CLK_RPLL1_192M		37
#define	CLK_RPLL1_96M		38
#define	CLK_RPLL1_64M		39
#define	CLK_RPLL1_48M		40
#define	CLK_DPLL0_50M		41
#define	CLK_DPLL1_50M		42
#define	CLK_CPPLL_50M		43
#define	CLK_M0_39M		44
#define	CLK_M1_63M		45
#define CLK_PLL_NUM		(CLK_M1_63M + 1)


#define	CLK_AP_APB		0
#define	CLK_AP_USB3		1
#define	CLK_UART0		2
#define	CLK_UART1		3
#define	CLK_UART2		4
#define	CLK_UART3		5
#define	CLK_UART4		6
#define	CLK_I2C0		7
#define	CLK_I2C1		8
#define	CLK_I2C2		9
#define	CLK_I2C3		10
#define	CLK_I2C4		11
#define	CLK_I2C5		12
#define	CLK_SPI0		13
#define	CLK_SPI1		14
#define	CLK_SPI2		15
#define	CLK_SPI3		16
#define	CLK_IIS0		17
#define	CLK_IIS1		18
#define	CLK_IIS2		19
#define	CLK_IIS3		20
#define CLK_AP_CLK_NUM		(CLK_IIS3 + 1)

#define	CLK_AON_APB		0
#define	CLK_AUX0		1
#define	CLK_AUX1		2
#define	CLK_AUX2		3
#define	CLK_PROBE		4
#define	CLK_SP_AHB		5
#define	CLK_CCI			6
#define	CLK_GIC			7
#define	CLK_CSSYS		8
#define	CLK_SDIO0_2X		9
#define	CLK_SDIO1_2X		10
#define	CLK_SDIO2_2X		11
#define	CLK_EMMC_2X		12
#define	CLK_SDIO0_1X		13
#define	CLK_SDIO1_1X		14
#define	CLK_SDIO2_1X		15
#define	CLK_EMMC_1X		16
#define	CLK_ADI			17
#define	CLK_PWM0		18
#define	CLK_PWM1		19
#define	CLK_PWM2		20
#define	CLK_PWM3		21
#define	CLK_EFUSE		22
#define	CLK_CM3_UART0		23
#define	CLK_CM3_UART1		24
#define	CLK_THM			25
#define	CLK_CM3_I2C0		26
#define	CLK_CM3_I2C1		27
#define	CLK_CM4_SPI		28
#define	CLK_AON_I2C		29
#define	CLK_AVS			30
#define	CLK_CA53_DAP		31
#define	CLK_CA53_TS		32
#define	CLK_DJTAG_TCK		33
#define	CLK_PMU			34
#define	CLK_PMU_26M		35
#define	CLK_DEBOUNCE		36
#define	CLK_OTG2_REF		37
#define	CLK_USB3_REF		38
#define	CLK_AP_AXI		39
#define CLK_AON_PREDIV_NUM	(CLK_AP_AXI + 1)

#define	CLK_USB3_EB		0
#define	CLK_USB3_SUSPEND_EB	1
#define	CLK_USB3_REF_EB		2
#define	CLK_DMA_EB		3
#define	CLK_SDIO0_EB		4
#define	CLK_SDIO1_EB		5
#define	CLK_SDIO2_EB		6
#define	CLK_EMMC_EB		7
#define	CLK_ROM_EB		8
#define	CLK_BUSMON_EB		9
#define	CLK_CC63S_EB		10
#define	CLK_CC63P_EB		11
#define	CLK_CE0_EB		12
#define	CLK_CE1_EB		13
#define CLK_APAHB_GATE_NUM	(CLK_CE1_EB + 1)

#define	CLK_AVS_LIT_EB		0
#define	CLK_AVS_BIG_EB		1
#define	CLK_AP_INTC5_EB		2
#define	CLK_GPIO_EB		3
#define	CLK_PWM0_EB		4
#define	CLK_PWM1_EB		5
#define	CLK_PWM2_EB		6
#define	CLK_PWM3_EB		7
#define	CLK_KPD_EB		8
#define	CLK_AON_SYS_EB		9
#define	CLK_AP_SYS_EB		10
#define	CLK_AON_TMR_EB		11
#define	CLK_AP_TMR0_EB		12
#define	CLK_EFUSE_EB		13
#define	CLK_EIC_EB		14
#define	CLK_PUB1_REG_EB		15
#define	CLK_ADI_EB		16
#define	CLK_AP_INTC0_EB		17
#define	CLK_AP_INTC1_EB		18
#define	CLK_AP_INTC2_EB		19
#define	CLK_AP_INTC3_EB		20
#define	CLK_AP_INTC4_EB		21
#define	CLK_SPLK_EB		22
#define	CLK_MSPI_EB		23
#define	CLK_PUB0_REG_EB		24
#define	CLK_PIN_EB		25
#define	CLK_AON_CKG_EB		26
#define	CLK_GPU_EB		27
#define	CLK_APCPU_TS0_EB	28
#define	CLK_APCPU_TS1_EB	29
#define	CLK_DAP_EB		30
#define	CLK_I2C_EB		31
#define	CLK_PMU_EB		32
#define	CLK_THM_EB		33
#define	CLK_AUX0_EB		34
#define	CLK_AUX1_EB		35
#define	CLK_AUX2_EB		36
#define	CLK_PROBE_EB		37
#define	CLK_GPU0_AVS_EB		38
#define	CLK_GPU1_AVS_EB		39
#define	CLK_APCPU_WDG_EB	40
#define	CLK_AP_TMR1_EB		41
#define	CLK_AP_TMR2_EB		42
#define	CLK_DISP_EMC_EB		43
#define	CLK_ZIP_EMC_EB		44
#define	CLK_GSP_EMC_EB		45
#define	CLK_OSC_AON_EB		46
#define	CLK_LVDS_TRX_EB		47
#define	CLK_LVDS_TCXO_EB	48
#define	CLK_MDAR_EB		49
#define	CLK_RTC4M0_CAL_EB	50
#define	CLK_RCT100M_CAL_EB	51
#define	CLK_DJTAG_EB		52
#define	CLK_MBOX_EB		53
#define	CLK_AON_DMA_EB		54
#define	CLK_DBG_EMC_EB		55
#define	CLK_LVDS_PLL_DIV_EN	56
#define	CLK_DEF_EB		57
#define	CLK_AON_APB_RSV0	58
#define	CLK_ORP_JTAG_EB		59
#define	CLK_VSP_EB		60
#define	CLK_CAM_EB		61
#define	CLK_DISP_EB		62
#define	CLK_DBG_AXI_IF_EB	63
#define	CLK_SDIO0_2X_EN		64
#define	CLK_SDIO1_2X_EN		65
#define	CLK_SDIO2_2X_EN		66
#define	CLK_EMMC_2X_EN		67
#define	CLK_ARCH_RTC_EB		68
#define	CLK_KPB_RTC_EB		69
#define	CLK_AON_SYST_RTC_EB	70
#define	CLK_AP_SYST_RTC_EB	71
#define	CLK_AON_TMR_RTC_EB	72
#define	CLK_AP_TMR0_RTC_EB	73
#define	CLK_EIC_RTC_EB		74
#define	CLK_EIC_RTCDV5_EB	75
#define	CLK_AP_WDG_RTC_EB	76
#define	CLK_AP_TMR1_RTC_EB	77
#define	CLK_AP_TMR2_RTC_EB	78
#define	CLK_DCXO_TMR_RTC_EB	79
#define	CLK_BB_CAL_RTC_EB	80
#define	CLK_AVS_BIG_RTC_EB	81
#define	CLK_AVS_LIT_RTC_EB	82
#define	CLK_AVS_GPU0_RTC_EB	83
#define	CLK_AVS_GPU1_RTC_EB	84
#define	CLK_GPU_TS_EB		85
#define	CLK_RTCDV10_EB		86
#define	CLK_AON_GATE_NUM	(CLK_RTCDV10_EB + 1)

#define	CLK_LIT_MCU		0
#define	CLK_BIG_MCU		1
#define CLK_AONSECURE_NUM	(CLK_BIG_MCU + 1)

#define	CLK_AGCP_IIS0_EB	0
#define	CLK_AGCP_IIS1_EB	1
#define	CLK_AGCP_IIS2_EB	2
#define	CLK_AGCP_IIS3_EB	3
#define	CLK_AGCP_UART_EB	4
#define	CLK_AGCP_DMACP_EB	5
#define	CLK_AGCP_DMAAP_EB	6
#define	CLK_AGCP_ARC48K_EB	7
#define	CLK_AGCP_SRC44P1K_EB	8
#define	CLK_AGCP_MCDT_EB	9
#define	CLK_AGCP_VBCIFD_EB	10
#define	CLK_AGCP_VBC_EB		11
#define	CLK_AGCP_SPINLOCK_EB	12
#define	CLK_AGCP_ICU_EB		13
#define	CLK_AGCP_AP_ASHB_EB	14
#define	CLK_AGCP_CP_ASHB_EB	15
#define	CLK_AGCP_AUD_EB		16
#define	CLK_AGCP_AUDIF_EB	17
#define CLK_AGCP_GATE_NUM	(CLK_AGCP_AUDIF_EB + 1)

#define	CLK_GPU			0
#define CLK_GPU_NUM		(CLK_GPU + 1)

#define	CLK_AHB_VSP		0
#define	CLK_VSP			1
#define	CLK_VSP_ENC		2
#define	CLK_VPP			3
#define	CLK_VSP_26M		4
#define CLK_VSP_NUM		(CLK_VSP_26M + 1)

#define	CLK_VSP_DEC_EB		0
#define	CLK_VSP_CKG_EB		1
#define	CLK_VSP_MMU_EB		2
#define	CLK_VSP_ENC_EB		3
#define	CLK_VPP_EB		4
#define	CLK_VSP_26M_EB		5
#define	CLK_VSP_AXI_GATE	6
#define	CLK_VSP_ENC_GATE	7
#define	CLK_VPP_AXI_GATE	8
#define	CLK_VSP_BM_GATE		9
#define	CLK_VSP_ENC_BM_GATE	10
#define	CLK_VPP_BM_GATE		11
#define CLK_VSP_GATE_NUM	(CLK_VPP_BM_GATE + 1)

#define	CLK_AHB_CAM		0
#define	CLK_SENSOR0		1
#define	CLK_SENSOR1		2
#define	CLK_SENSOR2		3
#define	CLK_MIPI_CSI0_EB	4
#define	CLK_MIPI_CSI1_EB	5
#define CLK_CAM_NUM		(CLK_MIPI_CSI1_EB + 1)

#define	CLK_DCAM0_EB		0
#define	CLK_DCAM1_EB		1
#define	CLK_ISP0_EB		2
#define	CLK_CSI0_EB		3
#define	CLK_CSI1_EB		4
#define	CLK_JPG0_EB		5
#define	CLK_JPG1_EB		6
#define	CLK_CAM_CKG_EB		7
#define	CLK_CAM_MMU_EB		8
#define	CLK_ISP1_EB		9
#define	CLK_CPP_EB		10
#define	CLK_MMU_PF_EB		11
#define	CLK_ISP2_EB		12
#define	CLK_DCAM2ISP_IF_EB	13
#define	CLK_ISP2DCAM_IF_EB	14
#define	CLK_ISP_LCLK_EB		15
#define	CLK_ISP_ICLK_EB		16
#define	CLK_ISP_MCLK_EB		17
#define	CLK_ISP_PCLK_EB		18
#define	CLK_ISP_ISP2DCAM_EB	19
#define	CLK_DCAM0_IF_EB		20
#define	CLK_CLK26M_IF_EB	21
#define	CLK_CPHY0_GATE		22
#define	CLK_MIPI_CSI0_GATE	23
#define	CLK_CPHY1_GATE		24
#define	CLK_MIPI_CSI1		25
#define	CLK_DCAM0_AXI_GATE	26
#define	CLK_DCAM1_AXI_GATE	27
#define	CLK_SENSOR0_GATE	28
#define	CLK_SENSOR1_GATE	29
#define	CLK_JPG0_AXI_GATE	30
#define	CLK_GPG1_AXI_GATE	31
#define	CLK_ISP0_AXI_GATE	32
#define	CLK_ISP1_AXI_GATE	33
#define	CLK_ISP2_AXI_GATE	34
#define	CLK_CPP_AXI_GATE	35
#define	CLK_D0_IF_AXI_GATE	36
#define	CLK_D2I_IF_AXI_GATE	37
#define	CLK_I2D_IF_AXI_GATE	38
#define	CLK_SPARE_AXI_GATE	39
#define	CLK_SENSOR2_GATE	40
#define	CLK_D0IF_IN_D_EN	41
#define	CLK_D1IF_IN_D_EN	42
#define	CLK_D0IF_IN_D2I_EN	43
#define	CLK_D1IF_IN_D2I_EN	44
#define	CLK_IA_IN_D2I_EN	45
#define	CLK_IB_IN_D2I_EN	46
#define	CLK_IC_IN_D2I_EN	47
#define	CLK_IA_IN_I_EN		48
#define	CLK_IB_IN_I_EN		49
#define	CLK_IC_IN_I_EN		50
#define CLK_CAM_GATE_NUM	(CLK_IC_IN_I_EN + 1)

#define	CLK_AHB_DISP		0
#define	CLK_DISPC0_DPI		1
#define	CLK_DISPC1_DPI		2
#define CLK_DISP_NUM		(CLK_DISPC1_DPI + 1)

#define	CLK_DISPC0_EB		0
#define	CLK_DISPC1_EB		1
#define	CLK_DISPC_MMU_EB	2
#define	CLK_GSP0_EB		3
#define	CLK_GSP1_EB		4
#define	CLK_GSP0_MMU_EB		5
#define	CLK_GSP1_MMU_EB		6
#define	CLK_DSI0_EB		7
#define	CLK_DSI1_EB		8
#define	CLK_DISP_CKG_EB		9
#define	CLK_DISP_GPU_EB		10
#define	CLK_GPU_MTX_EB		11
#define	CLK_GSP_MTX_EB		12
#define	CLK_TMC_MTX_EB		13
#define	CLK_DISPC_MTX_EB	14
#define	CLK_DPHY0_GATE		15
#define	CLK_DPHY1_GATE		16
#define	CLK_GSP0_A_GATE		17
#define	CLK_GSP1_A_GATE		18
#define	CLK_GSP0_F_GATE		19
#define	CLK_GSP1_F_GATE		20
#define	CLK_D_MTX_F_GATE	21
#define	CLK_D_MTX_A_GATE	22
#define	CLK_D_NOC_F_GATE	23
#define	CLK_D_NOC_A_GATE	24
#define	CLK_GSP_MTX_F_GATE	25
#define	CLK_GSP_MTX_A_GATE	26
#define	CLK_GSP_NOC_F_GATE	27
#define	CLK_GSP_NOC_A_GATE	28
#define	CLK_DISPM0IDLE_GATE	29
#define	CLK_GSPM0IDLE_GATE	30
#define CLK_DISP_GATE_NUM	(CLK_GSPM0IDLE_GATE + 1)

#define	CLK_SIM0_EB		0
#define	CLK_IIS0_EB		1
#define	CLK_IIS1_EB		2
#define	CLK_IIS2_EB		3
#define	CLK_IIS3_EB		4
#define	CLK_SPI0_EB		5
#define	CLK_SPI1_EB		6
#define	CLK_SPI2_EB		7
#define	CLK_I2C0_EB		8
#define	CLK_I2C1_EB		9
#define	CLK_I2C2_EB		10
#define	CLK_I2C3_EB		11
#define	CLK_I2C4_EB		12
#define	CLK_I2C5_EB		13
#define	CLK_UART0_EB		14
#define	CLK_UART1_EB		15
#define	CLK_UART2_EB		16
#define	CLK_UART3_EB		17
#define	CLK_UART4_EB		18
#define	CLK_AP_CKG_EB		19
#define	CLK_SPI3_EB		20
#define CLK_APAPB_GATE_NUM	(CLK_SPI3_EB + 1)

#endif /* _DT_BINDINGS_CLK_SC9860_H_ */
