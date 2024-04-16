/* SPDX-License-Identifier: GPL-2.0 */
/*
 * This header provides constants for the PRCMU bindings.
 *
 */

#ifndef _DT_BINDINGS_MFD_PRCMU_H
#define _DT_BINDINGS_MFD_PRCMU_H

/*
 * Clock identifiers.
 */
#define ARMCLK			0
#define PRCMU_ACLK		1
#define PRCMU_SVAMMCSPCLK 	2
#define PRCMU_SDMMCHCLK 	2  /* DBx540 only. */
#define PRCMU_SIACLK 		3
#define PRCMU_SIAMMDSPCLK 	3  /* DBx540 only. */
#define PRCMU_SGACLK 		4
#define PRCMU_UARTCLK 		5
#define PRCMU_MSP02CLK 		6
#define PRCMU_MSP1CLK 		7
#define PRCMU_I2CCLK 		8
#define PRCMU_SDMMCCLK 		9
#define PRCMU_SLIMCLK 		10
#define PRCMU_CAMCLK 		10 /* DBx540 only. */
#define PRCMU_PER1CLK 		11
#define PRCMU_PER2CLK 		12
#define PRCMU_PER3CLK 		13
#define PRCMU_PER5CLK 		14
#define PRCMU_PER6CLK 		15
#define PRCMU_PER7CLK 		16
#define PRCMU_LCDCLK 		17
#define PRCMU_BMLCLK 		18
#define PRCMU_HSITXCLK 		19
#define PRCMU_HSIRXCLK 		20
#define PRCMU_HDMICLK		21
#define PRCMU_APEATCLK 		22
#define PRCMU_APETRACECLK 	23
#define PRCMU_MCDECLK  	 	24
#define PRCMU_IPI2CCLK  	25
#define PRCMU_DSIALTCLK  	26
#define PRCMU_DMACLK  	 	27
#define PRCMU_B2R2CLK  	 	28
#define PRCMU_TVCLK  	 	29
#define SPARE_UNIPROCLK  	30
#define PRCMU_SSPCLK  	 	31
#define PRCMU_RNGCLK  	 	32
#define PRCMU_UICCCLK  	 	33
#define PRCMU_G1CLK             34 /* DBx540 only. */
#define PRCMU_HVACLK            35 /* DBx540 only. */
#define PRCMU_SPARE1CLK	 	36
#define PRCMU_SPARE2CLK	 	37

#define PRCMU_NUM_REG_CLOCKS  	38

#define PRCMU_RTCCLK  	 	PRCMU_NUM_REG_CLOCKS
#define PRCMU_SYSCLK  	 	39
#define PRCMU_CDCLK  	 	40
#define PRCMU_TIMCLK  	 	41
#define PRCMU_PLLSOC0  	 	42
#define PRCMU_PLLSOC1  	 	43
#define PRCMU_ARMSS  	 	44
#define PRCMU_PLLDDR  	 	45

/* DSI Clocks */
#define PRCMU_PLLDSI  	 	46
#define PRCMU_DSI0CLK 	  	47
#define PRCMU_DSI1CLK  	 	48
#define PRCMU_DSI0ESCCLK  	49
#define PRCMU_DSI1ESCCLK  	50
#define PRCMU_DSI2ESCCLK  	51

/* LCD DSI PLL - Ux540 only */
#define PRCMU_PLLDSI_LCD        52
#define PRCMU_DSI0CLK_LCD       53
#define PRCMU_DSI1CLK_LCD       54
#define PRCMU_DSI0ESCCLK_LCD    55
#define PRCMU_DSI1ESCCLK_LCD    56
#define PRCMU_DSI2ESCCLK_LCD    57

#define PRCMU_NUM_CLKS  	58

#endif
