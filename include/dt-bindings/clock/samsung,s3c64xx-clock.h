/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Copyright (c) 2013 Tomasz Figa <tomasz.figa at gmail.com>
 *
 * Device Tree binding constants for Samsung S3C64xx clock controller.
 */

#ifndef _DT_BINDINGS_CLOCK_SAMSUNG_S3C64XX_CLOCK_H
#define _DT_BINDINGS_CLOCK_SAMSUNG_S3C64XX_CLOCK_H

/*
 * Let each exported clock get a unique index, which is used on DT-enabled
 * platforms to lookup the clock from a clock specifier. These indices are
 * therefore considered an ABI and so must not be changed. This implies
 * that new clocks should be added either in free spaces between clock groups
 * or at the end.
 */

/* Core clocks. */
#define CLK27M			1
#define CLK48M			2
#define FOUT_APLL		3
#define FOUT_MPLL		4
#define FOUT_EPLL		5
#define ARMCLK			6
#define HCLKX2			7
#define HCLK			8
#define PCLK			9

/* HCLK bus clocks. */
#define HCLK_3DSE		16
#define HCLK_UHOST		17
#define HCLK_SECUR		18
#define HCLK_SDMA1		19
#define HCLK_SDMA0		20
#define HCLK_IROM		21
#define HCLK_DDR1		22
#define HCLK_MEM1		23
#define HCLK_MEM0		24
#define HCLK_USB		25
#define HCLK_HSMMC2		26
#define HCLK_HSMMC1		27
#define HCLK_HSMMC0		28
#define HCLK_MDP		29
#define HCLK_DHOST		30
#define HCLK_IHOST		31
#define HCLK_DMA1		32
#define HCLK_DMA0		33
#define HCLK_JPEG		34
#define HCLK_CAMIF		35
#define HCLK_SCALER		36
#define HCLK_2D			37
#define HCLK_TV			38
#define HCLK_POST0		39
#define HCLK_ROT		40
#define HCLK_LCD		41
#define HCLK_TZIC		42
#define HCLK_INTC		43
#define HCLK_MFC		44
#define HCLK_DDR0		45

/* PCLK bus clocks. */
#define PCLK_IIC1		48
#define PCLK_IIS2		49
#define PCLK_SKEY		50
#define PCLK_CHIPID		51
#define PCLK_SPI1		52
#define PCLK_SPI0		53
#define PCLK_HSIRX		54
#define PCLK_HSITX		55
#define PCLK_GPIO		56
#define PCLK_IIC0		57
#define PCLK_IIS1		58
#define PCLK_IIS0		59
#define PCLK_AC97		60
#define PCLK_TZPC		61
#define PCLK_TSADC		62
#define PCLK_KEYPAD		63
#define PCLK_IRDA		64
#define PCLK_PCM1		65
#define PCLK_PCM0		66
#define PCLK_PWM		67
#define PCLK_RTC		68
#define PCLK_WDT		69
#define PCLK_UART3		70
#define PCLK_UART2		71
#define PCLK_UART1		72
#define PCLK_UART0		73
#define PCLK_MFC		74

/* Special clocks. */
#define SCLK_UHOST		80
#define SCLK_MMC2_48		81
#define SCLK_MMC1_48		82
#define SCLK_MMC0_48		83
#define SCLK_MMC2		84
#define SCLK_MMC1		85
#define SCLK_MMC0		86
#define SCLK_SPI1_48		87
#define SCLK_SPI0_48		88
#define SCLK_SPI1		89
#define SCLK_SPI0		90
#define SCLK_DAC27		91
#define SCLK_TV27		92
#define SCLK_SCALER27		93
#define SCLK_SCALER		94
#define SCLK_LCD27		95
#define SCLK_LCD		96
#define SCLK_FIMC		97
#define SCLK_POST0_27		98
#define SCLK_AUDIO2		99
#define SCLK_POST0		100
#define SCLK_AUDIO1		101
#define SCLK_AUDIO0		102
#define SCLK_SECUR		103
#define SCLK_IRDA		104
#define SCLK_UART		105
#define SCLK_MFC		106
#define SCLK_CAM		107
#define SCLK_JPEG		108
#define SCLK_ONENAND		109

/* MEM0 bus clocks - S3C6410-specific. */
#define MEM0_CFCON		112
#define MEM0_ONENAND1		113
#define MEM0_ONENAND0		114
#define MEM0_NFCON		115
#define MEM0_SROM		116

/* Muxes. */
#define MOUT_APLL		128
#define MOUT_MPLL		129
#define MOUT_EPLL		130
#define MOUT_MFC		131
#define MOUT_AUDIO0		132
#define MOUT_AUDIO1		133
#define MOUT_UART		134
#define MOUT_SPI0		135
#define MOUT_SPI1		136
#define MOUT_MMC0		137
#define MOUT_MMC1		138
#define MOUT_MMC2		139
#define MOUT_UHOST		140
#define MOUT_IRDA		141
#define MOUT_LCD		142
#define MOUT_SCALER		143
#define MOUT_DAC27		144
#define MOUT_TV27		145
#define MOUT_AUDIO2		146

/* Dividers. */
#define DOUT_MPLL		160
#define DOUT_SECUR		161
#define DOUT_CAM		162
#define DOUT_JPEG		163
#define DOUT_MFC		164
#define DOUT_MMC0		165
#define DOUT_MMC1		166
#define DOUT_MMC2		167
#define DOUT_LCD		168
#define DOUT_SCALER		169
#define DOUT_UHOST		170
#define DOUT_SPI0		171
#define DOUT_SPI1		172
#define DOUT_AUDIO0		173
#define DOUT_AUDIO1		174
#define DOUT_UART		175
#define DOUT_IRDA		176
#define DOUT_FIMC		177
#define DOUT_AUDIO2		178

/* Total number of clocks. */
#define NR_CLKS			(DOUT_AUDIO2 + 1)

#endif /* _DT_BINDINGS_CLOCK_SAMSUNG_S3C64XX_CLOCK_H */
