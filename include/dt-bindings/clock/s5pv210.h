/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (c) 2013 Samsung Electronics Co., Ltd.
 * Author: Mateusz Krawczuk <m.krawczuk@partner.samsung.com>
 *
 * Device Tree binding constants for Samsung S5PV210 clock controller.
 */

#ifndef _DT_BINDINGS_CLOCK_S5PV210_H
#define _DT_BINDINGS_CLOCK_S5PV210_H

/* Core clocks. */
#define FIN_PLL			1
#define FOUT_APLL		2
#define FOUT_MPLL		3
#define FOUT_EPLL		4
#define FOUT_VPLL		5

/* Muxes. */
#define MOUT_FLASH		6
#define MOUT_PSYS		7
#define MOUT_DSYS		8
#define MOUT_MSYS		9
#define MOUT_VPLL		10
#define MOUT_EPLL		11
#define MOUT_MPLL		12
#define MOUT_APLL		13
#define MOUT_VPLLSRC		14
#define MOUT_CSIS		15
#define MOUT_FIMD		16
#define MOUT_CAM1		17
#define MOUT_CAM0		18
#define MOUT_DAC		19
#define MOUT_MIXER		20
#define MOUT_HDMI		21
#define MOUT_G2D		22
#define MOUT_MFC		23
#define MOUT_G3D		24
#define MOUT_FIMC2		25
#define MOUT_FIMC1		26
#define MOUT_FIMC0		27
#define MOUT_UART3		28
#define MOUT_UART2		29
#define MOUT_UART1		30
#define MOUT_UART0		31
#define MOUT_MMC3		32
#define MOUT_MMC2		33
#define MOUT_MMC1		34
#define MOUT_MMC0		35
#define MOUT_PWM		36
#define MOUT_SPI0		37
#define MOUT_SPI1		38
#define MOUT_DMC0		39
#define MOUT_PWI		40
#define MOUT_HPM		41
#define MOUT_SPDIF		42
#define MOUT_AUDIO2		43
#define MOUT_AUDIO1		44
#define MOUT_AUDIO0		45

/* Dividers. */
#define DOUT_PCLKP		46
#define DOUT_HCLKP		47
#define DOUT_PCLKD		48
#define DOUT_HCLKD		49
#define DOUT_PCLKM		50
#define DOUT_HCLKM		51
#define DOUT_A2M		52
#define DOUT_APLL		53
#define DOUT_CSIS		54
#define DOUT_FIMD		55
#define DOUT_CAM1		56
#define DOUT_CAM0		57
#define DOUT_TBLK		58
#define DOUT_G2D		59
#define DOUT_MFC		60
#define DOUT_G3D		61
#define DOUT_FIMC2		62
#define DOUT_FIMC1		63
#define DOUT_FIMC0		64
#define DOUT_UART3		65
#define DOUT_UART2		66
#define DOUT_UART1		67
#define DOUT_UART0		68
#define DOUT_MMC3		69
#define DOUT_MMC2		70
#define DOUT_MMC1		71
#define DOUT_MMC0		72
#define DOUT_PWM		73
#define DOUT_SPI1		74
#define DOUT_SPI0		75
#define DOUT_DMC0		76
#define DOUT_PWI		77
#define DOUT_HPM		78
#define DOUT_COPY		79
#define DOUT_FLASH		80
#define DOUT_AUDIO2		81
#define DOUT_AUDIO1		82
#define DOUT_AUDIO0		83
#define DOUT_DPM		84
#define DOUT_DVSEM		85

/* Gates */
#define SCLK_FIMC		86
#define CLK_CSIS		87
#define CLK_ROTATOR		88
#define CLK_FIMC2		89
#define CLK_FIMC1		90
#define CLK_FIMC0		91
#define CLK_MFC			92
#define CLK_G2D			93
#define CLK_G3D			94
#define CLK_IMEM		95
#define CLK_PDMA1		96
#define CLK_PDMA0		97
#define CLK_MDMA		98
#define CLK_DMC1		99
#define CLK_DMC0		100
#define CLK_NFCON		101
#define CLK_SROMC		102
#define CLK_CFCON		103
#define CLK_NANDXL		104
#define CLK_USB_HOST		105
#define CLK_USB_OTG		106
#define CLK_HDMI		107
#define CLK_TVENC		108
#define CLK_MIXER		109
#define CLK_VP			110
#define CLK_DSIM		111
#define CLK_FIMD		112
#define CLK_TZIC3		113
#define CLK_TZIC2		114
#define CLK_TZIC1		115
#define CLK_TZIC0		116
#define CLK_VIC3		117
#define CLK_VIC2		118
#define CLK_VIC1		119
#define CLK_VIC0		120
#define CLK_TSI			121
#define CLK_HSMMC3		122
#define CLK_HSMMC2		123
#define CLK_HSMMC1		124
#define CLK_HSMMC0		125
#define CLK_JTAG		126
#define CLK_MODEMIF		127
#define CLK_CORESIGHT		128
#define CLK_SDM			129
#define CLK_SECSS		130
#define CLK_PCM2		131
#define CLK_PCM1		132
#define CLK_PCM0		133
#define CLK_SYSCON		134
#define CLK_GPIO		135
#define CLK_TSADC		136
#define CLK_PWM			137
#define CLK_WDT			138
#define CLK_KEYIF		139
#define CLK_UART3		140
#define CLK_UART2		141
#define CLK_UART1		142
#define CLK_UART0		143
#define CLK_SYSTIMER		144
#define CLK_RTC			145
#define CLK_SPI1		146
#define CLK_SPI0		147
#define CLK_I2C_HDMI_PHY	148
#define CLK_I2C1		149
#define CLK_I2C2		150
#define CLK_I2C0		151
#define CLK_I2S1		152
#define CLK_I2S2		153
#define CLK_I2S0		154
#define CLK_AC97		155
#define CLK_SPDIF		156
#define CLK_TZPC3		157
#define CLK_TZPC2		158
#define CLK_TZPC1		159
#define CLK_TZPC0		160
#define CLK_SECKEY		161
#define CLK_IEM_APC		162
#define CLK_IEM_IEC		163
#define CLK_CHIPID		164
#define CLK_JPEG		163

/* Special clocks*/
#define SCLK_PWI		164
#define SCLK_SPDIF		165
#define SCLK_AUDIO2		166
#define SCLK_AUDIO1		167
#define SCLK_AUDIO0		168
#define SCLK_PWM		169
#define SCLK_SPI1		170
#define SCLK_SPI0		171
#define SCLK_UART3		172
#define SCLK_UART2		173
#define SCLK_UART1		174
#define SCLK_UART0		175
#define SCLK_MMC3		176
#define SCLK_MMC2		177
#define SCLK_MMC1		178
#define SCLK_MMC0		179
#define SCLK_FINVPLL		180
#define SCLK_CSIS		181
#define SCLK_FIMD		182
#define SCLK_CAM1		183
#define SCLK_CAM0		184
#define SCLK_DAC		185
#define SCLK_MIXER		186
#define SCLK_HDMI		187
#define SCLK_FIMC2		188
#define SCLK_FIMC1		189
#define SCLK_FIMC0		190
#define SCLK_HDMI27M		191
#define SCLK_HDMIPHY		192
#define SCLK_USBPHY0		193
#define SCLK_USBPHY1		194

/* S5P6442-specific clocks */
#define MOUT_D0SYNC		195
#define MOUT_D1SYNC		196
#define DOUT_MIXER		197
#define CLK_ETB			198
#define CLK_ETM			199

/* CLKOUT */
#define FOUT_APLL_CLKOUT	200
#define FOUT_MPLL_CLKOUT	201
#define DOUT_APLL_CLKOUT	202
#define MOUT_CLKSEL		203
#define DOUT_CLKOUT		204
#define MOUT_CLKOUT		205

/* Total number of clocks. */
#define NR_CLKS			206

#endif /* _DT_BINDINGS_CLOCK_S5PV210_H */
