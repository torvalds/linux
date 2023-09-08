/* SPDX-License-Identifier: GPL-2.0 OR MIT */
/*
 * Copyright (C) 2022 Emil Renner Berthing <kernel@esmil.dk>
 * Copyright (C) 2022 StarFive Technology Co., Ltd.
 */

#ifndef __DT_BINDINGS_PINCTRL_STARFIVE_JH7110_H__
#define __DT_BINDINGS_PINCTRL_STARFIVE_JH7110_H__

/* sys_iomux pins */
#define	PAD_GPIO0		0
#define	PAD_GPIO1		1
#define	PAD_GPIO2		2
#define	PAD_GPIO3		3
#define	PAD_GPIO4		4
#define	PAD_GPIO5		5
#define	PAD_GPIO6		6
#define	PAD_GPIO7		7
#define	PAD_GPIO8		8
#define	PAD_GPIO9		9
#define	PAD_GPIO10		10
#define	PAD_GPIO11		11
#define	PAD_GPIO12		12
#define	PAD_GPIO13		13
#define	PAD_GPIO14		14
#define	PAD_GPIO15		15
#define	PAD_GPIO16		16
#define	PAD_GPIO17		17
#define	PAD_GPIO18		18
#define	PAD_GPIO19		19
#define	PAD_GPIO20		20
#define	PAD_GPIO21		21
#define	PAD_GPIO22		22
#define	PAD_GPIO23		23
#define	PAD_GPIO24		24
#define	PAD_GPIO25		25
#define	PAD_GPIO26		26
#define	PAD_GPIO27		27
#define	PAD_GPIO28		28
#define	PAD_GPIO29		29
#define	PAD_GPIO30		30
#define	PAD_GPIO31		31
#define	PAD_GPIO32		32
#define	PAD_GPIO33		33
#define	PAD_GPIO34		34
#define	PAD_GPIO35		35
#define	PAD_GPIO36		36
#define	PAD_GPIO37		37
#define	PAD_GPIO38		38
#define	PAD_GPIO39		39
#define	PAD_GPIO40		40
#define	PAD_GPIO41		41
#define	PAD_GPIO42		42
#define	PAD_GPIO43		43
#define	PAD_GPIO44		44
#define	PAD_GPIO45		45
#define	PAD_GPIO46		46
#define	PAD_GPIO47		47
#define	PAD_GPIO48		48
#define	PAD_GPIO49		49
#define	PAD_GPIO50		50
#define	PAD_GPIO51		51
#define	PAD_GPIO52		52
#define	PAD_GPIO53		53
#define	PAD_GPIO54		54
#define	PAD_GPIO55		55
#define	PAD_GPIO56		56
#define	PAD_GPIO57		57
#define	PAD_GPIO58		58
#define	PAD_GPIO59		59
#define	PAD_GPIO60		60
#define	PAD_GPIO61		61
#define	PAD_GPIO62		62
#define	PAD_GPIO63		63
#define	PAD_SD0_CLK		64
#define	PAD_SD0_CMD		65
#define	PAD_SD0_DATA0		66
#define	PAD_SD0_DATA1		67
#define	PAD_SD0_DATA2		68
#define	PAD_SD0_DATA3		69
#define	PAD_SD0_DATA4		70
#define	PAD_SD0_DATA5		71
#define	PAD_SD0_DATA6		72
#define	PAD_SD0_DATA7		73
#define	PAD_SD0_STRB		74
#define	PAD_GMAC1_MDC		75
#define	PAD_GMAC1_MDIO		76
#define	PAD_GMAC1_RXD0		77
#define	PAD_GMAC1_RXD1		78
#define	PAD_GMAC1_RXD2		79
#define	PAD_GMAC1_RXD3		80
#define	PAD_GMAC1_RXDV		81
#define	PAD_GMAC1_RXC		82
#define	PAD_GMAC1_TXD0		83
#define	PAD_GMAC1_TXD1		84
#define	PAD_GMAC1_TXD2		85
#define	PAD_GMAC1_TXD3		86
#define	PAD_GMAC1_TXEN		87
#define	PAD_GMAC1_TXC		88
#define	PAD_QSPI_SCLK		89
#define	PAD_QSPI_CS0		90
#define	PAD_QSPI_DATA0		91
#define	PAD_QSPI_DATA1		92
#define	PAD_QSPI_DATA2		93
#define	PAD_QSPI_DATA3		94

/* aon_iomux pins */
#define	PAD_TESTEN		0
#define	PAD_RGPIO0		1
#define	PAD_RGPIO1		2
#define	PAD_RGPIO2		3
#define	PAD_RGPIO3		4
#define	PAD_RSTN		5
#define	PAD_GMAC0_MDC		6
#define	PAD_GMAC0_MDIO		7
#define	PAD_GMAC0_RXD0		8
#define	PAD_GMAC0_RXD1		9
#define	PAD_GMAC0_RXD2		10
#define	PAD_GMAC0_RXD3		11
#define	PAD_GMAC0_RXDV		12
#define	PAD_GMAC0_RXC		13
#define	PAD_GMAC0_TXD0		14
#define	PAD_GMAC0_TXD1		15
#define	PAD_GMAC0_TXD2		16
#define	PAD_GMAC0_TXD3		17
#define	PAD_GMAC0_TXEN		18
#define	PAD_GMAC0_TXC		19

#define GPOUT_LOW		0
#define GPOUT_HIGH		1

#define GPOEN_ENABLE		0
#define GPOEN_DISABLE		1

#define GPI_NONE		255

#endif
