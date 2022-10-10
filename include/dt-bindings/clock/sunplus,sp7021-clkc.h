/* SPDX-License-Identifier: (GPL-2.0-only OR BSD-2-Clause) */
/*
 * Copyright (C) Sunplus Technology Co., Ltd.
 *       All rights reserved.
 */
#ifndef _DT_BINDINGS_CLOCK_SUNPLUS_SP7021_H
#define _DT_BINDINGS_CLOCK_SUNPLUS_SP7021_H

/* gates */
#define CLK_RTC         0
#define CLK_OTPRX       1
#define CLK_NOC         2
#define CLK_BR          3
#define CLK_SPIFL       4
#define CLK_PERI0       5
#define CLK_PERI1       6
#define CLK_STC0        7
#define CLK_STC_AV0     8
#define CLK_STC_AV1     9
#define CLK_STC_AV2     10
#define CLK_UA0         11
#define CLK_UA1         12
#define CLK_UA2         13
#define CLK_UA3         14
#define CLK_UA4         15
#define CLK_HWUA        16
#define CLK_DDC0        17
#define CLK_UADMA       18
#define CLK_CBDMA0      19
#define CLK_CBDMA1      20
#define CLK_SPI_COMBO_0 21
#define CLK_SPI_COMBO_1 22
#define CLK_SPI_COMBO_2 23
#define CLK_SPI_COMBO_3 24
#define CLK_AUD         25
#define CLK_USBC0       26
#define CLK_USBC1       27
#define CLK_UPHY0       28
#define CLK_UPHY1       29
#define CLK_I2CM0       30
#define CLK_I2CM1       31
#define CLK_I2CM2       32
#define CLK_I2CM3       33
#define CLK_PMC         34
#define CLK_CARD_CTL0   35
#define CLK_CARD_CTL1   36
#define CLK_CARD_CTL4   37
#define CLK_BCH         38
#define CLK_DDFCH       39
#define CLK_CSIIW0      40
#define CLK_CSIIW1      41
#define CLK_MIPICSI0    42
#define CLK_MIPICSI1    43
#define CLK_HDMI_TX     44
#define CLK_VPOST       45
#define CLK_TGEN        46
#define CLK_DMIX        47
#define CLK_TCON        48
#define CLK_GPIO        49
#define CLK_MAILBOX     50
#define CLK_SPIND       51
#define CLK_I2C2CBUS    52
#define CLK_SEC         53
#define CLK_DVE         54
#define CLK_GPOST0      55
#define CLK_OSD0        56
#define CLK_DISP_PWM    57
#define CLK_UADBG       58
#define CLK_FIO_CTL     59
#define CLK_FPGA        60
#define CLK_L2SW        61
#define CLK_ICM         62
#define CLK_AXI_GLOBAL  63

/* plls */
#define PLL_A           64
#define PLL_E           65
#define PLL_E_2P5       66
#define PLL_E_25        67
#define PLL_E_112P5     68
#define PLL_F           69
#define PLL_TV          70
#define PLL_TV_A        71
#define PLL_SYS         72

#define CLK_MAX         73

#endif
