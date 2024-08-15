/* SPDX-License-Identifier: (GPL-2.0-only OR BSD-2-Clause) */
/*
 * Sunplus SP7021 dt-bindings Pinctrl header file
 * Copyright (C) Sunplus Tech/Tibbo Tech.
 * Author: Dvorkin Dmitry <dvorkin@tibbo.com>
 */

#ifndef	__DT_BINDINGS_PINCTRL_SPPCTL_SP7021_H__
#define	__DT_BINDINGS_PINCTRL_SPPCTL_SP7021_H__

#include <dt-bindings/pinctrl/sppctl.h>

/*
 * Please don't change the order of the following defines.
 * They are based on order of 'hardware' control register
 * defined in MOON2 ~ MOON3 registers.
 */
#define MUXF_GPIO                       0
#define MUXF_IOP                        1
#define MUXF_L2SW_CLK_OUT               2
#define MUXF_L2SW_MAC_SMI_MDC           3
#define MUXF_L2SW_LED_FLASH0            4
#define MUXF_L2SW_LED_FLASH1            5
#define MUXF_L2SW_LED_ON0               6
#define MUXF_L2SW_LED_ON1               7
#define MUXF_L2SW_MAC_SMI_MDIO          8
#define MUXF_L2SW_P0_MAC_RMII_TXEN      9
#define MUXF_L2SW_P0_MAC_RMII_TXD0      10
#define MUXF_L2SW_P0_MAC_RMII_TXD1      11
#define MUXF_L2SW_P0_MAC_RMII_CRSDV     12
#define MUXF_L2SW_P0_MAC_RMII_RXD0      13
#define MUXF_L2SW_P0_MAC_RMII_RXD1      14
#define MUXF_L2SW_P0_MAC_RMII_RXER      15
#define MUXF_L2SW_P1_MAC_RMII_TXEN      16
#define MUXF_L2SW_P1_MAC_RMII_TXD0      17
#define MUXF_L2SW_P1_MAC_RMII_TXD1      18
#define MUXF_L2SW_P1_MAC_RMII_CRSDV     19
#define MUXF_L2SW_P1_MAC_RMII_RXD0      20
#define MUXF_L2SW_P1_MAC_RMII_RXD1      21
#define MUXF_L2SW_P1_MAC_RMII_RXER      22
#define MUXF_DAISY_MODE                 23
#define MUXF_SDIO_CLK                   24
#define MUXF_SDIO_CMD                   25
#define MUXF_SDIO_D0                    26
#define MUXF_SDIO_D1                    27
#define MUXF_SDIO_D2                    28
#define MUXF_SDIO_D3                    29
#define MUXF_PWM0                       30
#define MUXF_PWM1                       31
#define MUXF_PWM2                       32
#define MUXF_PWM3                       33
#define MUXF_PWM4                       34
#define MUXF_PWM5                       35
#define MUXF_PWM6                       36
#define MUXF_PWM7                       37
#define MUXF_ICM0_D                     38
#define MUXF_ICM1_D                     39
#define MUXF_ICM2_D                     40
#define MUXF_ICM3_D                     41
#define MUXF_ICM0_CLK                   42
#define MUXF_ICM1_CLK                   43
#define MUXF_ICM2_CLK                   44
#define MUXF_ICM3_CLK                   45
#define MUXF_SPIM0_INT                  46
#define MUXF_SPIM0_CLK                  47
#define MUXF_SPIM0_EN                   48
#define MUXF_SPIM0_DO                   49
#define MUXF_SPIM0_DI                   50
#define MUXF_SPIM1_INT                  51
#define MUXF_SPIM1_CLK                  52
#define MUXF_SPIM1_EN                   53
#define MUXF_SPIM1_DO                   54
#define MUXF_SPIM1_DI                   55
#define MUXF_SPIM2_INT                  56
#define MUXF_SPIM2_CLK                  57
#define MUXF_SPIM2_EN                   58
#define MUXF_SPIM2_DO                   59
#define MUXF_SPIM2_DI                   60
#define MUXF_SPIM3_INT                  61
#define MUXF_SPIM3_CLK                  62
#define MUXF_SPIM3_EN                   63
#define MUXF_SPIM3_DO                   64
#define MUXF_SPIM3_DI                   65
#define MUXF_SPI0S_INT                  66
#define MUXF_SPI0S_CLK                  67
#define MUXF_SPI0S_EN                   68
#define MUXF_SPI0S_DO                   69
#define MUXF_SPI0S_DI                   70
#define MUXF_SPI1S_INT                  71
#define MUXF_SPI1S_CLK                  72
#define MUXF_SPI1S_EN                   73
#define MUXF_SPI1S_DO                   74
#define MUXF_SPI1S_DI                   75
#define MUXF_SPI2S_INT                  76
#define MUXF_SPI2S_CLK                  77
#define MUXF_SPI2S_EN                   78
#define MUXF_SPI2S_DO                   79
#define MUXF_SPI2S_DI                   80
#define MUXF_SPI3S_INT                  81
#define MUXF_SPI3S_CLK                  82
#define MUXF_SPI3S_EN                   83
#define MUXF_SPI3S_DO                   84
#define MUXF_SPI3S_DI                   85
#define MUXF_I2CM0_CLK                  86
#define MUXF_I2CM0_DAT                  87
#define MUXF_I2CM1_CLK                  88
#define MUXF_I2CM1_DAT                  89
#define MUXF_I2CM2_CLK                  90
#define MUXF_I2CM2_DAT                  91
#define MUXF_I2CM3_CLK                  92
#define MUXF_I2CM3_DAT                  93
#define MUXF_UA1_TX                     94
#define MUXF_UA1_RX                     95
#define MUXF_UA1_CTS                    96
#define MUXF_UA1_RTS                    97
#define MUXF_UA2_TX                     98
#define MUXF_UA2_RX                     99
#define MUXF_UA2_CTS                    100
#define MUXF_UA2_RTS                    101
#define MUXF_UA3_TX                     102
#define MUXF_UA3_RX                     103
#define MUXF_UA3_CTS                    104
#define MUXF_UA3_RTS                    105
#define MUXF_UA4_TX                     106
#define MUXF_UA4_RX                     107
#define MUXF_UA4_CTS                    108
#define MUXF_UA4_RTS                    109
#define MUXF_TIMER0_INT                 110
#define MUXF_TIMER1_INT                 111
#define MUXF_TIMER2_INT                 112
#define MUXF_TIMER3_INT                 113
#define MUXF_GPIO_INT0                  114
#define MUXF_GPIO_INT1                  115
#define MUXF_GPIO_INT2                  116
#define MUXF_GPIO_INT3                  117
#define MUXF_GPIO_INT4                  118
#define MUXF_GPIO_INT5                  119
#define MUXF_GPIO_INT6                  120
#define MUXF_GPIO_INT7                  121

/*
 * Please don't change the order of the following defines.
 * They are based on order of items in array 'sppctl_list_funcs'
 * in Sunplus pinctrl driver.
 */
#define GROP_SPI_FLASH                  122
#define GROP_SPI_FLASH_4BIT             123
#define GROP_SPI_NAND                   124
#define GROP_CARD0_EMMC                 125
#define GROP_SD_CARD                    126
#define GROP_UA0                        127
#define GROP_ACHIP_DEBUG                128
#define GROP_ACHIP_UA2AXI               129
#define GROP_FPGA_IFX                   130
#define GROP_HDMI_TX                    131
#define GROP_AUD_EXT_ADC_IFX0           132
#define GROP_AUD_EXT_DAC_IFX0           133
#define GROP_SPDIF_RX                   134
#define GROP_SPDIF_TX                   135
#define GROP_TDMTX_IFX0                 136
#define GROP_TDMRX_IFX0                 137
#define GROP_PDMRX_IFX0                 138
#define GROP_PCM_IEC_TX                 139
#define GROP_LCDIF                      140
#define GROP_DVD_DSP_DEBUG              141
#define GROP_I2C_DEBUG                  142
#define GROP_I2C_SLAVE                  143
#define GROP_WAKEUP                     144
#define GROP_UART2AXI                   145
#define GROP_USB0_I2C                   146
#define GROP_USB1_I2C                   147
#define GROP_USB0_OTG                   148
#define GROP_USB1_OTG                   149
#define GROP_UPHY0_DEBUG                150
#define GROP_UPHY1_DEBUG                151
#define GROP_UPHY0_EXT                  152
#define GROP_PROBE_PORT                 153

#endif
