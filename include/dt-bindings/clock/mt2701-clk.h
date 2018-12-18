/*
 * Copyright (c) 2014 MediaTek Inc.
 * Author: Shunli Wang <shunli.wang@mediatek.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 */

#ifndef _DT_BINDINGS_CLK_MT2701_H
#define _DT_BINDINGS_CLK_MT2701_H

/* TOPCKGEN */
#define CLK_TOP_SYSPLL				1
#define CLK_TOP_SYSPLL_D2			2
#define CLK_TOP_SYSPLL_D3			3
#define CLK_TOP_SYSPLL_D5			4
#define CLK_TOP_SYSPLL_D7			5
#define CLK_TOP_SYSPLL1_D2			6
#define CLK_TOP_SYSPLL1_D4			7
#define CLK_TOP_SYSPLL1_D8			8
#define CLK_TOP_SYSPLL1_D16			9
#define CLK_TOP_SYSPLL2_D2			10
#define CLK_TOP_SYSPLL2_D4			11
#define CLK_TOP_SYSPLL2_D8			12
#define CLK_TOP_SYSPLL3_D2			13
#define CLK_TOP_SYSPLL3_D4			14
#define CLK_TOP_SYSPLL4_D2			15
#define CLK_TOP_SYSPLL4_D4			16
#define CLK_TOP_UNIVPLL				17
#define CLK_TOP_UNIVPLL_D2			18
#define CLK_TOP_UNIVPLL_D3			19
#define CLK_TOP_UNIVPLL_D5			20
#define CLK_TOP_UNIVPLL_D7			21
#define CLK_TOP_UNIVPLL_D26			22
#define CLK_TOP_UNIVPLL_D52			23
#define CLK_TOP_UNIVPLL_D108			24
#define CLK_TOP_USB_PHY48M			25
#define CLK_TOP_UNIVPLL1_D2			26
#define CLK_TOP_UNIVPLL1_D4			27
#define CLK_TOP_UNIVPLL1_D8			28
#define CLK_TOP_UNIVPLL2_D2			29
#define CLK_TOP_UNIVPLL2_D4			30
#define CLK_TOP_UNIVPLL2_D8			31
#define CLK_TOP_UNIVPLL2_D16			32
#define CLK_TOP_UNIVPLL2_D32			33
#define CLK_TOP_UNIVPLL3_D2			34
#define CLK_TOP_UNIVPLL3_D4			35
#define CLK_TOP_UNIVPLL3_D8			36
#define CLK_TOP_MSDCPLL				37
#define CLK_TOP_MSDCPLL_D2			38
#define CLK_TOP_MSDCPLL_D4			39
#define CLK_TOP_MSDCPLL_D8			40
#define CLK_TOP_MMPLL				41
#define CLK_TOP_MMPLL_D2			42
#define CLK_TOP_DMPLL				43
#define CLK_TOP_DMPLL_D2			44
#define CLK_TOP_DMPLL_D4			45
#define CLK_TOP_DMPLL_X2			46
#define CLK_TOP_TVDPLL				47
#define CLK_TOP_TVDPLL_D2			48
#define CLK_TOP_TVDPLL_D4			49
#define CLK_TOP_TVD2PLL				50
#define CLK_TOP_TVD2PLL_D2			51
#define CLK_TOP_HADDS2PLL_98M			52
#define CLK_TOP_HADDS2PLL_294M			53
#define CLK_TOP_HADDS2_FB			54
#define CLK_TOP_MIPIPLL_D2			55
#define CLK_TOP_MIPIPLL_D4			56
#define CLK_TOP_HDMIPLL				57
#define CLK_TOP_HDMIPLL_D2			58
#define CLK_TOP_HDMIPLL_D3			59
#define CLK_TOP_HDMI_SCL_RX			60
#define CLK_TOP_HDMI_0_PIX340M			61
#define CLK_TOP_HDMI_0_DEEP340M			62
#define CLK_TOP_HDMI_0_PLL340M			63
#define CLK_TOP_AUD1PLL_98M			64
#define CLK_TOP_AUD2PLL_90M			65
#define CLK_TOP_AUDPLL				66
#define CLK_TOP_AUDPLL_D4			67
#define CLK_TOP_AUDPLL_D8			68
#define CLK_TOP_AUDPLL_D16			69
#define CLK_TOP_AUDPLL_D24			70
#define CLK_TOP_ETHPLL_500M			71
#define CLK_TOP_VDECPLL				72
#define CLK_TOP_VENCPLL				73
#define CLK_TOP_MIPIPLL				74
#define CLK_TOP_ARMPLL_1P3G			75

#define CLK_TOP_MM_SEL				76
#define CLK_TOP_DDRPHYCFG_SEL			77
#define CLK_TOP_MEM_SEL				78
#define CLK_TOP_AXI_SEL				79
#define CLK_TOP_CAMTG_SEL			80
#define CLK_TOP_MFG_SEL				81
#define CLK_TOP_VDEC_SEL			82
#define CLK_TOP_PWM_SEL				83
#define CLK_TOP_MSDC30_0_SEL			84
#define CLK_TOP_USB20_SEL			85
#define CLK_TOP_SPI0_SEL			86
#define CLK_TOP_UART_SEL			87
#define CLK_TOP_AUDINTBUS_SEL			88
#define CLK_TOP_AUDIO_SEL			89
#define CLK_TOP_MSDC30_2_SEL			90
#define CLK_TOP_MSDC30_1_SEL			91
#define CLK_TOP_DPI1_SEL			92
#define CLK_TOP_DPI0_SEL			93
#define CLK_TOP_SCP_SEL				94
#define CLK_TOP_PMICSPI_SEL			95
#define CLK_TOP_APLL_SEL			96
#define CLK_TOP_HDMI_SEL			97
#define CLK_TOP_TVE_SEL				98
#define CLK_TOP_EMMC_HCLK_SEL			99
#define CLK_TOP_NFI2X_SEL			100
#define CLK_TOP_RTC_SEL				101
#define CLK_TOP_OSD_SEL				102
#define CLK_TOP_NR_SEL				103
#define CLK_TOP_DI_SEL				104
#define CLK_TOP_FLASH_SEL			105
#define CLK_TOP_ASM_M_SEL			106
#define CLK_TOP_ASM_I_SEL			107
#define CLK_TOP_INTDIR_SEL			108
#define CLK_TOP_HDMIRX_BIST_SEL			109
#define CLK_TOP_ETHIF_SEL			110
#define CLK_TOP_MS_CARD_SEL			111
#define CLK_TOP_ASM_H_SEL			112
#define CLK_TOP_SPI1_SEL			113
#define CLK_TOP_CMSYS_SEL			114
#define CLK_TOP_MSDC30_3_SEL			115
#define CLK_TOP_HDMIRX26_24_SEL			116
#define CLK_TOP_AUD2DVD_SEL			117
#define CLK_TOP_8BDAC_SEL			118
#define CLK_TOP_SPI2_SEL			119
#define CLK_TOP_AUD_MUX1_SEL			120
#define CLK_TOP_AUD_MUX2_SEL			121
#define CLK_TOP_AUDPLL_MUX_SEL			122
#define CLK_TOP_AUD_K1_SRC_SEL			123
#define CLK_TOP_AUD_K2_SRC_SEL			124
#define CLK_TOP_AUD_K3_SRC_SEL			125
#define CLK_TOP_AUD_K4_SRC_SEL			126
#define CLK_TOP_AUD_K5_SRC_SEL			127
#define CLK_TOP_AUD_K6_SRC_SEL			128
#define CLK_TOP_PADMCLK_SEL			129
#define CLK_TOP_AUD_EXTCK1_DIV			130
#define CLK_TOP_AUD_EXTCK2_DIV			131
#define CLK_TOP_AUD_MUX1_DIV			132
#define CLK_TOP_AUD_MUX2_DIV			133
#define CLK_TOP_AUD_K1_SRC_DIV			134
#define CLK_TOP_AUD_K2_SRC_DIV			135
#define CLK_TOP_AUD_K3_SRC_DIV			136
#define CLK_TOP_AUD_K4_SRC_DIV			137
#define CLK_TOP_AUD_K5_SRC_DIV			138
#define CLK_TOP_AUD_K6_SRC_DIV			139
#define CLK_TOP_AUD_I2S1_MCLK			140
#define CLK_TOP_AUD_I2S2_MCLK			141
#define CLK_TOP_AUD_I2S3_MCLK			142
#define CLK_TOP_AUD_I2S4_MCLK			143
#define CLK_TOP_AUD_I2S5_MCLK			144
#define CLK_TOP_AUD_I2S6_MCLK			145
#define CLK_TOP_AUD_48K_TIMING			146
#define CLK_TOP_AUD_44K_TIMING			147

#define CLK_TOP_32K_INTERNAL			148
#define CLK_TOP_32K_EXTERNAL			149
#define CLK_TOP_CLK26M_D8			150
#define CLK_TOP_8BDAC				151
#define CLK_TOP_WBG_DIG_416M			152
#define CLK_TOP_DPI				153
#define CLK_TOP_DSI0_LNTC_DSI			154
#define CLK_TOP_AUD_EXT1			155
#define CLK_TOP_AUD_EXT2			156
#define CLK_TOP_NFI1X_PAD			157
#define CLK_TOP_AXISEL_D4			158
#define CLK_TOP_NR				159

/* APMIXEDSYS */

#define CLK_APMIXED_ARMPLL			1
#define CLK_APMIXED_MAINPLL			2
#define CLK_APMIXED_UNIVPLL			3
#define CLK_APMIXED_MMPLL			4
#define CLK_APMIXED_MSDCPLL			5
#define CLK_APMIXED_TVDPLL			6
#define CLK_APMIXED_AUD1PLL			7
#define CLK_APMIXED_TRGPLL			8
#define CLK_APMIXED_ETHPLL			9
#define CLK_APMIXED_VDECPLL			10
#define CLK_APMIXED_HADDS2PLL			11
#define CLK_APMIXED_AUD2PLL			12
#define CLK_APMIXED_TVD2PLL			13
#define CLK_APMIXED_HDMI_REF			14
#define CLK_APMIXED_NR				15

/* DDRPHY */

#define CLK_DDRPHY_VENCPLL			1
#define CLK_DDRPHY_NR				2

/* INFRACFG */

#define CLK_INFRA_DBG				1
#define CLK_INFRA_SMI				2
#define CLK_INFRA_QAXI_CM4			3
#define CLK_INFRA_AUD_SPLIN_B			4
#define CLK_INFRA_AUDIO				5
#define CLK_INFRA_EFUSE				6
#define CLK_INFRA_L2C_SRAM			7
#define CLK_INFRA_M4U				8
#define CLK_INFRA_CONNMCU			9
#define CLK_INFRA_TRNG				10
#define CLK_INFRA_RAMBUFIF			11
#define CLK_INFRA_CPUM				12
#define CLK_INFRA_KP				13
#define CLK_INFRA_CEC				14
#define CLK_INFRA_IRRX				15
#define CLK_INFRA_PMICSPI			16
#define CLK_INFRA_PMICWRAP			17
#define CLK_INFRA_DDCCI				18
#define CLK_INFRA_CLK_13M			19
#define CLK_INFRA_CPUSEL                        20
#define CLK_INFRA_NR				21

/* PERICFG */

#define CLK_PERI_NFI				1
#define CLK_PERI_THERM				2
#define CLK_PERI_PWM1				3
#define CLK_PERI_PWM2				4
#define CLK_PERI_PWM3				5
#define CLK_PERI_PWM4				6
#define CLK_PERI_PWM5				7
#define CLK_PERI_PWM6				8
#define CLK_PERI_PWM7				9
#define CLK_PERI_PWM				10
#define CLK_PERI_USB0				11
#define CLK_PERI_USB1				12
#define CLK_PERI_AP_DMA				13
#define CLK_PERI_MSDC30_0			14
#define CLK_PERI_MSDC30_1			15
#define CLK_PERI_MSDC30_2			16
#define CLK_PERI_MSDC30_3			17
#define CLK_PERI_MSDC50_3			18
#define CLK_PERI_NLI				19
#define CLK_PERI_UART0				20
#define CLK_PERI_UART1				21
#define CLK_PERI_UART2				22
#define CLK_PERI_UART3				23
#define CLK_PERI_BTIF				24
#define CLK_PERI_I2C0				25
#define CLK_PERI_I2C1				26
#define CLK_PERI_I2C2				27
#define CLK_PERI_I2C3				28
#define CLK_PERI_AUXADC				29
#define CLK_PERI_SPI0				30
#define CLK_PERI_ETH				31
#define CLK_PERI_USB0_MCU			32

#define CLK_PERI_USB1_MCU			33
#define CLK_PERI_USB_SLV			34
#define CLK_PERI_GCPU				35
#define CLK_PERI_NFI_ECC			36
#define CLK_PERI_NFI_PAD			37
#define CLK_PERI_FLASH				38
#define CLK_PERI_HOST89_INT			39
#define CLK_PERI_HOST89_SPI			40
#define CLK_PERI_HOST89_DVD			41
#define CLK_PERI_SPI1				42
#define CLK_PERI_SPI2				43
#define CLK_PERI_FCI				44

#define CLK_PERI_UART0_SEL			45
#define CLK_PERI_UART1_SEL			46
#define CLK_PERI_UART2_SEL			47
#define CLK_PERI_UART3_SEL			48
#define CLK_PERI_NR				49

/* AUDIO */

#define CLK_AUD_AFE				1
#define CLK_AUD_LRCK_DETECT			2
#define CLK_AUD_I2S				3
#define CLK_AUD_APLL_TUNER			4
#define CLK_AUD_HDMI				5
#define CLK_AUD_SPDF				6
#define CLK_AUD_SPDF2				7
#define CLK_AUD_APLL				8
#define CLK_AUD_TML				9
#define CLK_AUD_AHB_IDLE_EXT			10
#define CLK_AUD_AHB_IDLE_INT			11

#define CLK_AUD_I2SIN1				12
#define CLK_AUD_I2SIN2				13
#define CLK_AUD_I2SIN3				14
#define CLK_AUD_I2SIN4				15
#define CLK_AUD_I2SIN5				16
#define CLK_AUD_I2SIN6				17
#define CLK_AUD_I2SO1				18
#define CLK_AUD_I2SO2				19
#define CLK_AUD_I2SO3				20
#define CLK_AUD_I2SO4				21
#define CLK_AUD_I2SO5				22
#define CLK_AUD_I2SO6				23
#define CLK_AUD_ASRCI1				24
#define CLK_AUD_ASRCI2				25
#define CLK_AUD_ASRCO1				26
#define CLK_AUD_ASRCO2				27
#define CLK_AUD_ASRC11				28
#define CLK_AUD_ASRC12				29
#define CLK_AUD_HDMIRX				30
#define CLK_AUD_INTDIR				31
#define CLK_AUD_A1SYS				32
#define CLK_AUD_A2SYS				33
#define CLK_AUD_AFE_CONN			34
#define CLK_AUD_AFE_PCMIF			35
#define CLK_AUD_AFE_MRGIF			36

#define CLK_AUD_MMIF_UL1			37
#define CLK_AUD_MMIF_UL2			38
#define CLK_AUD_MMIF_UL3			39
#define CLK_AUD_MMIF_UL4			40
#define CLK_AUD_MMIF_UL5			41
#define CLK_AUD_MMIF_UL6			42
#define CLK_AUD_MMIF_DL1			43
#define CLK_AUD_MMIF_DL2			44
#define CLK_AUD_MMIF_DL3			45
#define CLK_AUD_MMIF_DL4			46
#define CLK_AUD_MMIF_DL5			47
#define CLK_AUD_MMIF_DL6			48
#define CLK_AUD_MMIF_DLMCH			49
#define CLK_AUD_MMIF_ARB1			50
#define CLK_AUD_MMIF_AWB1			51
#define CLK_AUD_MMIF_AWB2			52
#define CLK_AUD_MMIF_DAI			53

#define CLK_AUD_DMIC1				54
#define CLK_AUD_DMIC2				55
#define CLK_AUD_ASRCI3				56
#define CLK_AUD_ASRCI4				57
#define CLK_AUD_ASRCI5				58
#define CLK_AUD_ASRCI6				59
#define CLK_AUD_ASRCO3				60
#define CLK_AUD_ASRCO4				61
#define CLK_AUD_ASRCO5				62
#define CLK_AUD_ASRCO6				63
#define CLK_AUD_MEM_ASRC1			64
#define CLK_AUD_MEM_ASRC2			65
#define CLK_AUD_MEM_ASRC3			66
#define CLK_AUD_MEM_ASRC4			67
#define CLK_AUD_MEM_ASRC5			68
#define CLK_AUD_DSD_ENC				69
#define CLK_AUD_ASRC_BRG			70
#define CLK_AUD_NR				71

/* MMSYS */

#define CLK_MM_SMI_COMMON			1
#define CLK_MM_SMI_LARB0			2
#define CLK_MM_CMDQ				3
#define CLK_MM_MUTEX				4
#define CLK_MM_DISP_COLOR			5
#define CLK_MM_DISP_BLS				6
#define CLK_MM_DISP_WDMA			7
#define CLK_MM_DISP_RDMA			8
#define CLK_MM_DISP_OVL				9
#define CLK_MM_MDP_TDSHP			10
#define CLK_MM_MDP_WROT				11
#define CLK_MM_MDP_WDMA				12
#define CLK_MM_MDP_RSZ1				13
#define CLK_MM_MDP_RSZ0				14
#define CLK_MM_MDP_RDMA				15
#define CLK_MM_MDP_BLS_26M			16
#define CLK_MM_CAM_MDP				17
#define CLK_MM_FAKE_ENG				18
#define CLK_MM_MUTEX_32K			19
#define CLK_MM_DISP_RDMA1			20
#define CLK_MM_DISP_UFOE			21

#define CLK_MM_DSI_ENGINE			22
#define CLK_MM_DSI_DIG				23
#define CLK_MM_DPI_DIGL				24
#define CLK_MM_DPI_ENGINE			25
#define CLK_MM_DPI1_DIGL			26
#define CLK_MM_DPI1_ENGINE			27
#define CLK_MM_TVE_OUTPUT			28
#define CLK_MM_TVE_INPUT			29
#define CLK_MM_HDMI_PIXEL			30
#define CLK_MM_HDMI_PLL				31
#define CLK_MM_HDMI_AUDIO			32
#define CLK_MM_HDMI_SPDIF			33
#define CLK_MM_TVE_FMM				34
#define CLK_MM_NR				35

/* IMGSYS */

#define CLK_IMG_SMI_COMM			1
#define CLK_IMG_RESZ				2
#define CLK_IMG_JPGDEC_SMI			3
#define CLK_IMG_JPGDEC				4
#define CLK_IMG_VENC_LT				5
#define CLK_IMG_VENC				6
#define CLK_IMG_NR				7

/* VDEC */

#define CLK_VDEC_CKGEN				1
#define CLK_VDEC_LARB				2
#define CLK_VDEC_NR				3

/* HIFSYS */

#define CLK_HIFSYS_USB0PHY			1
#define CLK_HIFSYS_USB1PHY			2
#define CLK_HIFSYS_PCIE0			3
#define CLK_HIFSYS_PCIE1			4
#define CLK_HIFSYS_PCIE2			5
#define CLK_HIFSYS_NR				6

/* ETHSYS */
#define CLK_ETHSYS_HSDMA			1
#define CLK_ETHSYS_ESW				2
#define CLK_ETHSYS_GP2				3
#define CLK_ETHSYS_GP1				4
#define CLK_ETHSYS_PCM				5
#define CLK_ETHSYS_GDMA				6
#define CLK_ETHSYS_I2S				7
#define CLK_ETHSYS_CRYPTO			8
#define CLK_ETHSYS_NR				9

/* G3DSYS */
#define CLK_G3DSYS_CORE				1
#define CLK_G3DSYS_NR				2

/* BDP */

#define CLK_BDP_BRG_BA				1
#define CLK_BDP_BRG_DRAM			2
#define CLK_BDP_LARB_DRAM			3
#define CLK_BDP_WR_VDI_PXL			4
#define CLK_BDP_WR_VDI_DRAM			5
#define CLK_BDP_WR_B				6
#define CLK_BDP_DGI_IN				7
#define CLK_BDP_DGI_OUT				8
#define CLK_BDP_FMT_MAST_27			9
#define CLK_BDP_FMT_B				10
#define CLK_BDP_OSD_B				11
#define CLK_BDP_OSD_DRAM			12
#define CLK_BDP_OSD_AGENT			13
#define CLK_BDP_OSD_PXL				14
#define CLK_BDP_RLE_B				15
#define CLK_BDP_RLE_AGENT			16
#define CLK_BDP_RLE_DRAM			17
#define CLK_BDP_F27M				18
#define CLK_BDP_F27M_VDOUT			19
#define CLK_BDP_F27_74_74			20
#define CLK_BDP_F2FS				21
#define CLK_BDP_F2FS74_148			22
#define CLK_BDP_FB				23
#define CLK_BDP_VDO_DRAM			24
#define CLK_BDP_VDO_2FS				25
#define CLK_BDP_VDO_B				26
#define CLK_BDP_WR_DI_PXL			27
#define CLK_BDP_WR_DI_DRAM			28
#define CLK_BDP_WR_DI_B				29
#define CLK_BDP_NR_PXL				30
#define CLK_BDP_NR_DRAM				31
#define CLK_BDP_NR_B				32

#define CLK_BDP_RX_F				33
#define CLK_BDP_RX_X				34
#define CLK_BDP_RXPDT				35
#define CLK_BDP_RX_CSCL_N			36
#define CLK_BDP_RX_CSCL				37
#define CLK_BDP_RX_DDCSCL_N			38
#define CLK_BDP_RX_DDCSCL			39
#define CLK_BDP_RX_VCO				40
#define CLK_BDP_RX_DP				41
#define CLK_BDP_RX_P				42
#define CLK_BDP_RX_M				43
#define CLK_BDP_RX_PLL				44
#define CLK_BDP_BRG_RT_B			45
#define CLK_BDP_BRG_RT_DRAM			46
#define CLK_BDP_LARBRT_DRAM			47
#define CLK_BDP_TMDS_SYN			48
#define CLK_BDP_HDMI_MON			49
#define CLK_BDP_NR				50

#endif /* _DT_BINDINGS_CLK_MT2701_H */
