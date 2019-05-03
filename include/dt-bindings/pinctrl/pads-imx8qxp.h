/* SPDX-License-Identifier: GPL-2.0+ */
/*
 * Copyright (C) 2016 Freescale Semiconductor, Inc.
 * Copyright 2017~2018 NXP
 */

#ifndef _IMX8QXP_PADS_H
#define _IMX8QXP_PADS_H

/* pin id */
#define IMX8QXP_PCIE_CTRL0_PERST_B                  0
#define IMX8QXP_PCIE_CTRL0_CLKREQ_B                 1
#define IMX8QXP_PCIE_CTRL0_WAKE_B                   2
#define IMX8QXP_COMP_CTL_GPIO_1V8_3V3_PCIESEP       3
#define IMX8QXP_USB_SS3_TC0                         4
#define IMX8QXP_USB_SS3_TC1                         5
#define IMX8QXP_USB_SS3_TC2                         6
#define IMX8QXP_USB_SS3_TC3                         7
#define IMX8QXP_COMP_CTL_GPIO_3V3_USB3IO            8
#define IMX8QXP_EMMC0_CLK                           9
#define IMX8QXP_EMMC0_CMD                           10
#define IMX8QXP_EMMC0_DATA0                         11
#define IMX8QXP_EMMC0_DATA1                         12
#define IMX8QXP_EMMC0_DATA2                         13
#define IMX8QXP_EMMC0_DATA3                         14
#define IMX8QXP_COMP_CTL_GPIO_1V8_3V3_SD1FIX0       15
#define IMX8QXP_EMMC0_DATA4                         16
#define IMX8QXP_EMMC0_DATA5                         17
#define IMX8QXP_EMMC0_DATA6                         18
#define IMX8QXP_EMMC0_DATA7                         19
#define IMX8QXP_EMMC0_STROBE                        20
#define IMX8QXP_EMMC0_RESET_B                       21
#define IMX8QXP_COMP_CTL_GPIO_1V8_3V3_SD1FIX1       22
#define IMX8QXP_USDHC1_RESET_B                      23
#define IMX8QXP_USDHC1_VSELECT                      24
#define IMX8QXP_CTL_NAND_RE_P_N                     25
#define IMX8QXP_USDHC1_WP                           26
#define IMX8QXP_USDHC1_CD_B                         27
#define IMX8QXP_CTL_NAND_DQS_P_N                    28
#define IMX8QXP_COMP_CTL_GPIO_1V8_3V3_VSELSEP       29
#define IMX8QXP_USDHC1_CLK                          30
#define IMX8QXP_USDHC1_CMD                          31
#define IMX8QXP_USDHC1_DATA0                        32
#define IMX8QXP_USDHC1_DATA1                        33
#define IMX8QXP_USDHC1_DATA2                        34
#define IMX8QXP_USDHC1_DATA3                        35
#define IMX8QXP_COMP_CTL_GPIO_1V8_3V3_VSEL3         36
#define IMX8QXP_ENET0_RGMII_TXC                     37
#define IMX8QXP_ENET0_RGMII_TX_CTL                  38
#define IMX8QXP_ENET0_RGMII_TXD0                    39
#define IMX8QXP_ENET0_RGMII_TXD1                    40
#define IMX8QXP_ENET0_RGMII_TXD2                    41
#define IMX8QXP_ENET0_RGMII_TXD3                    42
#define IMX8QXP_COMP_CTL_GPIO_1V8_3V3_ENET_ENETB0   43
#define IMX8QXP_ENET0_RGMII_RXC                     44
#define IMX8QXP_ENET0_RGMII_RX_CTL                  45
#define IMX8QXP_ENET0_RGMII_RXD0                    46
#define IMX8QXP_ENET0_RGMII_RXD1                    47
#define IMX8QXP_ENET0_RGMII_RXD2                    48
#define IMX8QXP_ENET0_RGMII_RXD3                    49
#define IMX8QXP_COMP_CTL_GPIO_1V8_3V3_ENET_ENETB1   50
#define IMX8QXP_ENET0_REFCLK_125M_25M               51
#define IMX8QXP_ENET0_MDIO                          52
#define IMX8QXP_ENET0_MDC                           53
#define IMX8QXP_COMP_CTL_GPIO_1V8_3V3_GPIOCT        54
#define IMX8QXP_ESAI0_FSR                           55
#define IMX8QXP_ESAI0_FST                           56
#define IMX8QXP_ESAI0_SCKR                          57
#define IMX8QXP_ESAI0_SCKT                          58
#define IMX8QXP_ESAI0_TX0                           59
#define IMX8QXP_ESAI0_TX1                           60
#define IMX8QXP_ESAI0_TX2_RX3                       61
#define IMX8QXP_ESAI0_TX3_RX2                       62
#define IMX8QXP_ESAI0_TX4_RX1                       63
#define IMX8QXP_ESAI0_TX5_RX0                       64
#define IMX8QXP_SPDIF0_RX                           65
#define IMX8QXP_SPDIF0_TX                           66
#define IMX8QXP_SPDIF0_EXT_CLK                      67
#define IMX8QXP_COMP_CTL_GPIO_1V8_3V3_GPIORHB       68
#define IMX8QXP_SPI3_SCK                            69
#define IMX8QXP_SPI3_SDO                            70
#define IMX8QXP_SPI3_SDI                            71
#define IMX8QXP_SPI3_CS0                            72
#define IMX8QXP_SPI3_CS1                            73
#define IMX8QXP_MCLK_IN1                            74
#define IMX8QXP_MCLK_IN0                            75
#define IMX8QXP_MCLK_OUT0                           76
#define IMX8QXP_UART1_TX                            77
#define IMX8QXP_UART1_RX                            78
#define IMX8QXP_UART1_RTS_B                         79
#define IMX8QXP_UART1_CTS_B                         80
#define IMX8QXP_COMP_CTL_GPIO_1V8_3V3_GPIORHK       81
#define IMX8QXP_SAI0_TXD                            82
#define IMX8QXP_SAI0_TXC                            83
#define IMX8QXP_SAI0_RXD                            84
#define IMX8QXP_SAI0_TXFS                           85
#define IMX8QXP_SAI1_RXD                            86
#define IMX8QXP_SAI1_RXC                            87
#define IMX8QXP_SAI1_RXFS                           88
#define IMX8QXP_SPI2_CS0                            89
#define IMX8QXP_SPI2_SDO                            90
#define IMX8QXP_SPI2_SDI                            91
#define IMX8QXP_SPI2_SCK                            92
#define IMX8QXP_SPI0_SCK                            93
#define IMX8QXP_SPI0_SDI                            94
#define IMX8QXP_SPI0_SDO                            95
#define IMX8QXP_SPI0_CS1                            96
#define IMX8QXP_SPI0_CS0                            97
#define IMX8QXP_COMP_CTL_GPIO_1V8_3V3_GPIORHT       98
#define IMX8QXP_ADC_IN1                             99
#define IMX8QXP_ADC_IN0                             100
#define IMX8QXP_ADC_IN3                             101
#define IMX8QXP_ADC_IN2                             102
#define IMX8QXP_ADC_IN5                             103
#define IMX8QXP_ADC_IN4                             104
#define IMX8QXP_FLEXCAN0_RX                         105
#define IMX8QXP_FLEXCAN0_TX                         106
#define IMX8QXP_FLEXCAN1_RX                         107
#define IMX8QXP_FLEXCAN1_TX                         108
#define IMX8QXP_FLEXCAN2_RX                         109
#define IMX8QXP_FLEXCAN2_TX                         110
#define IMX8QXP_UART0_RX                            111
#define IMX8QXP_UART0_TX                            112
#define IMX8QXP_UART2_TX                            113
#define IMX8QXP_UART2_RX                            114
#define IMX8QXP_COMP_CTL_GPIO_1V8_3V3_GPIOLH        115
#define IMX8QXP_MIPI_DSI0_I2C0_SCL                  116
#define IMX8QXP_MIPI_DSI0_I2C0_SDA                  117
#define IMX8QXP_MIPI_DSI0_GPIO0_00                  118
#define IMX8QXP_MIPI_DSI0_GPIO0_01                  119
#define IMX8QXP_MIPI_DSI1_I2C0_SCL                  120
#define IMX8QXP_MIPI_DSI1_I2C0_SDA                  121
#define IMX8QXP_MIPI_DSI1_GPIO0_00                  122
#define IMX8QXP_MIPI_DSI1_GPIO0_01                  123
#define IMX8QXP_COMP_CTL_GPIO_1V8_3V3_MIPIDSIGPIO   124
#define IMX8QXP_JTAG_TRST_B                         125
#define IMX8QXP_PMIC_I2C_SCL                        126
#define IMX8QXP_PMIC_I2C_SDA                        127
#define IMX8QXP_PMIC_INT_B                          128
#define IMX8QXP_SCU_GPIO0_00                        129
#define IMX8QXP_SCU_GPIO0_01                        130
#define IMX8QXP_SCU_PMIC_STANDBY                    131
#define IMX8QXP_SCU_BOOT_MODE0                      132
#define IMX8QXP_SCU_BOOT_MODE1                      133
#define IMX8QXP_SCU_BOOT_MODE2                      134
#define IMX8QXP_SCU_BOOT_MODE3                      135
#define IMX8QXP_CSI_D00                             136
#define IMX8QXP_CSI_D01                             137
#define IMX8QXP_CSI_D02                             138
#define IMX8QXP_CSI_D03                             139
#define IMX8QXP_CSI_D04                             140
#define IMX8QXP_CSI_D05                             141
#define IMX8QXP_CSI_D06                             142
#define IMX8QXP_CSI_D07                             143
#define IMX8QXP_CSI_HSYNC                           144
#define IMX8QXP_CSI_VSYNC                           145
#define IMX8QXP_CSI_PCLK                            146
#define IMX8QXP_CSI_MCLK                            147
#define IMX8QXP_CSI_EN                              148
#define IMX8QXP_CSI_RESET                           149
#define IMX8QXP_COMP_CTL_GPIO_1V8_3V3_GPIORHD       150
#define IMX8QXP_MIPI_CSI0_MCLK_OUT                  151
#define IMX8QXP_MIPI_CSI0_I2C0_SCL                  152
#define IMX8QXP_MIPI_CSI0_I2C0_SDA                  153
#define IMX8QXP_MIPI_CSI0_GPIO0_01                  154
#define IMX8QXP_MIPI_CSI0_GPIO0_00                  155
#define IMX8QXP_QSPI0A_DATA0                        156
#define IMX8QXP_QSPI0A_DATA1                        157
#define IMX8QXP_QSPI0A_DATA2                        158
#define IMX8QXP_QSPI0A_DATA3                        159
#define IMX8QXP_QSPI0A_DQS                          160
#define IMX8QXP_QSPI0A_SS0_B                        161
#define IMX8QXP_QSPI0A_SS1_B                        162
#define IMX8QXP_QSPI0A_SCLK                         163
#define IMX8QXP_COMP_CTL_GPIO_1V8_3V3_QSPI0A        164
#define IMX8QXP_QSPI0B_SCLK                         165
#define IMX8QXP_QSPI0B_DATA0                        166
#define IMX8QXP_QSPI0B_DATA1                        167
#define IMX8QXP_QSPI0B_DATA2                        168
#define IMX8QXP_QSPI0B_DATA3                        169
#define IMX8QXP_QSPI0B_DQS                          170
#define IMX8QXP_QSPI0B_SS0_B                        171
#define IMX8QXP_QSPI0B_SS1_B                        172
#define IMX8QXP_COMP_CTL_GPIO_1V8_3V3_QSPI0B        173

/*
 * format: <pin_id mux_mode>
 */
#define IMX8QXP_PCIE_CTRL0_PERST_B_HSIO_PCIE0_PERST_B              IMX8QXP_PCIE_CTRL0_PERST_B            0
#define IMX8QXP_PCIE_CTRL0_PERST_B_LSIO_GPIO4_IO00                 IMX8QXP_PCIE_CTRL0_PERST_B            4
#define IMX8QXP_PCIE_CTRL0_CLKREQ_B_HSIO_PCIE0_CLKREQ_B            IMX8QXP_PCIE_CTRL0_CLKREQ_B           0
#define IMX8QXP_PCIE_CTRL0_CLKREQ_B_LSIO_GPIO4_IO01                IMX8QXP_PCIE_CTRL0_CLKREQ_B           4
#define IMX8QXP_PCIE_CTRL0_WAKE_B_HSIO_PCIE0_WAKE_B                IMX8QXP_PCIE_CTRL0_WAKE_B             0
#define IMX8QXP_PCIE_CTRL0_WAKE_B_LSIO_GPIO4_IO02                  IMX8QXP_PCIE_CTRL0_WAKE_B             4
#define IMX8QXP_USB_SS3_TC0_ADMA_I2C1_SCL                          IMX8QXP_USB_SS3_TC0                   0
#define IMX8QXP_USB_SS3_TC0_CONN_USB_OTG1_PWR                      IMX8QXP_USB_SS3_TC0                   1
#define IMX8QXP_USB_SS3_TC0_CONN_USB_OTG2_PWR                      IMX8QXP_USB_SS3_TC0                   2
#define IMX8QXP_USB_SS3_TC0_LSIO_GPIO4_IO03                        IMX8QXP_USB_SS3_TC0                   4
#define IMX8QXP_USB_SS3_TC1_ADMA_I2C1_SCL                          IMX8QXP_USB_SS3_TC1                   0
#define IMX8QXP_USB_SS3_TC1_CONN_USB_OTG2_PWR                      IMX8QXP_USB_SS3_TC1                   1
#define IMX8QXP_USB_SS3_TC1_LSIO_GPIO4_IO04                        IMX8QXP_USB_SS3_TC1                   4
#define IMX8QXP_USB_SS3_TC2_ADMA_I2C1_SDA                          IMX8QXP_USB_SS3_TC2                   0
#define IMX8QXP_USB_SS3_TC2_CONN_USB_OTG1_OC                       IMX8QXP_USB_SS3_TC2                   1
#define IMX8QXP_USB_SS3_TC2_CONN_USB_OTG2_OC                       IMX8QXP_USB_SS3_TC2                   2
#define IMX8QXP_USB_SS3_TC2_LSIO_GPIO4_IO05                        IMX8QXP_USB_SS3_TC2                   4
#define IMX8QXP_USB_SS3_TC3_ADMA_I2C1_SDA                          IMX8QXP_USB_SS3_TC3                   0
#define IMX8QXP_USB_SS3_TC3_CONN_USB_OTG2_OC                       IMX8QXP_USB_SS3_TC3                   1
#define IMX8QXP_USB_SS3_TC3_LSIO_GPIO4_IO06                        IMX8QXP_USB_SS3_TC3                   4
#define IMX8QXP_EMMC0_CLK_CONN_EMMC0_CLK                           IMX8QXP_EMMC0_CLK                     0
#define IMX8QXP_EMMC0_CLK_CONN_NAND_READY_B                        IMX8QXP_EMMC0_CLK                     1
#define IMX8QXP_EMMC0_CLK_LSIO_GPIO4_IO07                          IMX8QXP_EMMC0_CLK                     4
#define IMX8QXP_EMMC0_CMD_CONN_EMMC0_CMD                           IMX8QXP_EMMC0_CMD                     0
#define IMX8QXP_EMMC0_CMD_CONN_NAND_DQS                            IMX8QXP_EMMC0_CMD                     1
#define IMX8QXP_EMMC0_CMD_LSIO_GPIO4_IO08                          IMX8QXP_EMMC0_CMD                     4
#define IMX8QXP_EMMC0_DATA0_CONN_EMMC0_DATA0                       IMX8QXP_EMMC0_DATA0                   0
#define IMX8QXP_EMMC0_DATA0_CONN_NAND_DATA00                       IMX8QXP_EMMC0_DATA0                   1
#define IMX8QXP_EMMC0_DATA0_LSIO_GPIO4_IO09                        IMX8QXP_EMMC0_DATA0                   4
#define IMX8QXP_EMMC0_DATA1_CONN_EMMC0_DATA1                       IMX8QXP_EMMC0_DATA1                   0
#define IMX8QXP_EMMC0_DATA1_CONN_NAND_DATA01                       IMX8QXP_EMMC0_DATA1                   1
#define IMX8QXP_EMMC0_DATA1_LSIO_GPIO4_IO10                        IMX8QXP_EMMC0_DATA1                   4
#define IMX8QXP_EMMC0_DATA2_CONN_EMMC0_DATA2                       IMX8QXP_EMMC0_DATA2                   0
#define IMX8QXP_EMMC0_DATA2_CONN_NAND_DATA02                       IMX8QXP_EMMC0_DATA2                   1
#define IMX8QXP_EMMC0_DATA2_LSIO_GPIO4_IO11                        IMX8QXP_EMMC0_DATA2                   4
#define IMX8QXP_EMMC0_DATA3_CONN_EMMC0_DATA3                       IMX8QXP_EMMC0_DATA3                   0
#define IMX8QXP_EMMC0_DATA3_CONN_NAND_DATA03                       IMX8QXP_EMMC0_DATA3                   1
#define IMX8QXP_EMMC0_DATA3_LSIO_GPIO4_IO12                        IMX8QXP_EMMC0_DATA3                   4
#define IMX8QXP_EMMC0_DATA4_CONN_EMMC0_DATA4                       IMX8QXP_EMMC0_DATA4                   0
#define IMX8QXP_EMMC0_DATA4_CONN_NAND_DATA04                       IMX8QXP_EMMC0_DATA4                   1
#define IMX8QXP_EMMC0_DATA4_CONN_EMMC0_WP                          IMX8QXP_EMMC0_DATA4                   3
#define IMX8QXP_EMMC0_DATA4_LSIO_GPIO4_IO13                        IMX8QXP_EMMC0_DATA4                   4
#define IMX8QXP_EMMC0_DATA5_CONN_EMMC0_DATA5                       IMX8QXP_EMMC0_DATA5                   0
#define IMX8QXP_EMMC0_DATA5_CONN_NAND_DATA05                       IMX8QXP_EMMC0_DATA5                   1
#define IMX8QXP_EMMC0_DATA5_CONN_EMMC0_VSELECT                     IMX8QXP_EMMC0_DATA5                   3
#define IMX8QXP_EMMC0_DATA5_LSIO_GPIO4_IO14                        IMX8QXP_EMMC0_DATA5                   4
#define IMX8QXP_EMMC0_DATA6_CONN_EMMC0_DATA6                       IMX8QXP_EMMC0_DATA6                   0
#define IMX8QXP_EMMC0_DATA6_CONN_NAND_DATA06                       IMX8QXP_EMMC0_DATA6                   1
#define IMX8QXP_EMMC0_DATA6_CONN_MLB_CLK                           IMX8QXP_EMMC0_DATA6                   3
#define IMX8QXP_EMMC0_DATA6_LSIO_GPIO4_IO15                        IMX8QXP_EMMC0_DATA6                   4
#define IMX8QXP_EMMC0_DATA7_CONN_EMMC0_DATA7                       IMX8QXP_EMMC0_DATA7                   0
#define IMX8QXP_EMMC0_DATA7_CONN_NAND_DATA07                       IMX8QXP_EMMC0_DATA7                   1
#define IMX8QXP_EMMC0_DATA7_CONN_MLB_SIG                           IMX8QXP_EMMC0_DATA7                   3
#define IMX8QXP_EMMC0_DATA7_LSIO_GPIO4_IO16                        IMX8QXP_EMMC0_DATA7                   4
#define IMX8QXP_EMMC0_STROBE_CONN_EMMC0_STROBE                     IMX8QXP_EMMC0_STROBE                  0
#define IMX8QXP_EMMC0_STROBE_CONN_NAND_CLE                         IMX8QXP_EMMC0_STROBE                  1
#define IMX8QXP_EMMC0_STROBE_CONN_MLB_DATA                         IMX8QXP_EMMC0_STROBE                  3
#define IMX8QXP_EMMC0_STROBE_LSIO_GPIO4_IO17                       IMX8QXP_EMMC0_STROBE                  4
#define IMX8QXP_EMMC0_RESET_B_CONN_EMMC0_RESET_B                   IMX8QXP_EMMC0_RESET_B                 0
#define IMX8QXP_EMMC0_RESET_B_CONN_NAND_WP_B                       IMX8QXP_EMMC0_RESET_B                 1
#define IMX8QXP_EMMC0_RESET_B_LSIO_GPIO4_IO18                      IMX8QXP_EMMC0_RESET_B                 4
#define IMX8QXP_USDHC1_RESET_B_CONN_USDHC1_RESET_B                 IMX8QXP_USDHC1_RESET_B                0
#define IMX8QXP_USDHC1_RESET_B_CONN_NAND_RE_N                      IMX8QXP_USDHC1_RESET_B                1
#define IMX8QXP_USDHC1_RESET_B_ADMA_SPI2_SCK                       IMX8QXP_USDHC1_RESET_B                2
#define IMX8QXP_USDHC1_RESET_B_LSIO_GPIO4_IO19                     IMX8QXP_USDHC1_RESET_B                4
#define IMX8QXP_USDHC1_VSELECT_CONN_USDHC1_VSELECT                 IMX8QXP_USDHC1_VSELECT                0
#define IMX8QXP_USDHC1_VSELECT_CONN_NAND_RE_P                      IMX8QXP_USDHC1_VSELECT                1
#define IMX8QXP_USDHC1_VSELECT_ADMA_SPI2_SDO                       IMX8QXP_USDHC1_VSELECT                2
#define IMX8QXP_USDHC1_VSELECT_CONN_NAND_RE_B                      IMX8QXP_USDHC1_VSELECT                3
#define IMX8QXP_USDHC1_VSELECT_LSIO_GPIO4_IO20                     IMX8QXP_USDHC1_VSELECT                4
#define IMX8QXP_USDHC1_WP_CONN_USDHC1_WP                           IMX8QXP_USDHC1_WP                     0
#define IMX8QXP_USDHC1_WP_CONN_NAND_DQS_N                          IMX8QXP_USDHC1_WP                     1
#define IMX8QXP_USDHC1_WP_ADMA_SPI2_SDI                            IMX8QXP_USDHC1_WP                     2
#define IMX8QXP_USDHC1_WP_LSIO_GPIO4_IO21                          IMX8QXP_USDHC1_WP                     4
#define IMX8QXP_USDHC1_CD_B_CONN_USDHC1_CD_B                       IMX8QXP_USDHC1_CD_B                   0
#define IMX8QXP_USDHC1_CD_B_CONN_NAND_DQS_P                        IMX8QXP_USDHC1_CD_B                   1
#define IMX8QXP_USDHC1_CD_B_ADMA_SPI2_CS0                          IMX8QXP_USDHC1_CD_B                   2
#define IMX8QXP_USDHC1_CD_B_CONN_NAND_DQS                          IMX8QXP_USDHC1_CD_B                   3
#define IMX8QXP_USDHC1_CD_B_LSIO_GPIO4_IO22                        IMX8QXP_USDHC1_CD_B                   4
#define IMX8QXP_USDHC1_CLK_CONN_USDHC1_CLK                         IMX8QXP_USDHC1_CLK                    0
#define IMX8QXP_USDHC1_CLK_ADMA_UART3_RX                           IMX8QXP_USDHC1_CLK                    2
#define IMX8QXP_USDHC1_CLK_LSIO_GPIO4_IO23                         IMX8QXP_USDHC1_CLK                    4
#define IMX8QXP_USDHC1_CMD_CONN_USDHC1_CMD                         IMX8QXP_USDHC1_CMD                    0
#define IMX8QXP_USDHC1_CMD_CONN_NAND_CE0_B                         IMX8QXP_USDHC1_CMD                    1
#define IMX8QXP_USDHC1_CMD_ADMA_MQS_R                              IMX8QXP_USDHC1_CMD                    2
#define IMX8QXP_USDHC1_CMD_LSIO_GPIO4_IO24                         IMX8QXP_USDHC1_CMD                    4
#define IMX8QXP_USDHC1_DATA0_CONN_USDHC1_DATA0                     IMX8QXP_USDHC1_DATA0                  0
#define IMX8QXP_USDHC1_DATA0_CONN_NAND_CE1_B                       IMX8QXP_USDHC1_DATA0                  1
#define IMX8QXP_USDHC1_DATA0_ADMA_MQS_L                            IMX8QXP_USDHC1_DATA0                  2
#define IMX8QXP_USDHC1_DATA0_LSIO_GPIO4_IO25                       IMX8QXP_USDHC1_DATA0                  4
#define IMX8QXP_USDHC1_DATA1_CONN_USDHC1_DATA1                     IMX8QXP_USDHC1_DATA1                  0
#define IMX8QXP_USDHC1_DATA1_CONN_NAND_RE_B                        IMX8QXP_USDHC1_DATA1                  1
#define IMX8QXP_USDHC1_DATA1_ADMA_UART3_TX                         IMX8QXP_USDHC1_DATA1                  2
#define IMX8QXP_USDHC1_DATA1_LSIO_GPIO4_IO26                       IMX8QXP_USDHC1_DATA1                  4
#define IMX8QXP_USDHC1_DATA2_CONN_USDHC1_DATA2                     IMX8QXP_USDHC1_DATA2                  0
#define IMX8QXP_USDHC1_DATA2_CONN_NAND_WE_B                        IMX8QXP_USDHC1_DATA2                  1
#define IMX8QXP_USDHC1_DATA2_ADMA_UART3_CTS_B                      IMX8QXP_USDHC1_DATA2                  2
#define IMX8QXP_USDHC1_DATA2_LSIO_GPIO4_IO27                       IMX8QXP_USDHC1_DATA2                  4
#define IMX8QXP_USDHC1_DATA3_CONN_USDHC1_DATA3                     IMX8QXP_USDHC1_DATA3                  0
#define IMX8QXP_USDHC1_DATA3_CONN_NAND_ALE                         IMX8QXP_USDHC1_DATA3                  1
#define IMX8QXP_USDHC1_DATA3_ADMA_UART3_RTS_B                      IMX8QXP_USDHC1_DATA3                  2
#define IMX8QXP_USDHC1_DATA3_LSIO_GPIO4_IO28                       IMX8QXP_USDHC1_DATA3                  4
#define IMX8QXP_ENET0_RGMII_TXC_CONN_ENET0_RGMII_TXC               IMX8QXP_ENET0_RGMII_TXC               0
#define IMX8QXP_ENET0_RGMII_TXC_CONN_ENET0_RCLK50M_OUT             IMX8QXP_ENET0_RGMII_TXC               1
#define IMX8QXP_ENET0_RGMII_TXC_CONN_ENET0_RCLK50M_IN              IMX8QXP_ENET0_RGMII_TXC               2
#define IMX8QXP_ENET0_RGMII_TXC_CONN_NAND_CE1_B                    IMX8QXP_ENET0_RGMII_TXC               3
#define IMX8QXP_ENET0_RGMII_TXC_LSIO_GPIO4_IO29                    IMX8QXP_ENET0_RGMII_TXC               4
#define IMX8QXP_ENET0_RGMII_TX_CTL_CONN_ENET0_RGMII_TX_CTL         IMX8QXP_ENET0_RGMII_TX_CTL            0
#define IMX8QXP_ENET0_RGMII_TX_CTL_CONN_USDHC1_RESET_B             IMX8QXP_ENET0_RGMII_TX_CTL            3
#define IMX8QXP_ENET0_RGMII_TX_CTL_LSIO_GPIO4_IO30                 IMX8QXP_ENET0_RGMII_TX_CTL            4
#define IMX8QXP_ENET0_RGMII_TXD0_CONN_ENET0_RGMII_TXD0             IMX8QXP_ENET0_RGMII_TXD0              0
#define IMX8QXP_ENET0_RGMII_TXD0_CONN_USDHC1_VSELECT               IMX8QXP_ENET0_RGMII_TXD0              3
#define IMX8QXP_ENET0_RGMII_TXD0_LSIO_GPIO4_IO31                   IMX8QXP_ENET0_RGMII_TXD0              4
#define IMX8QXP_ENET0_RGMII_TXD1_CONN_ENET0_RGMII_TXD1             IMX8QXP_ENET0_RGMII_TXD1              0
#define IMX8QXP_ENET0_RGMII_TXD1_CONN_USDHC1_WP                    IMX8QXP_ENET0_RGMII_TXD1              3
#define IMX8QXP_ENET0_RGMII_TXD1_LSIO_GPIO5_IO00                   IMX8QXP_ENET0_RGMII_TXD1              4
#define IMX8QXP_ENET0_RGMII_TXD2_CONN_ENET0_RGMII_TXD2             IMX8QXP_ENET0_RGMII_TXD2              0
#define IMX8QXP_ENET0_RGMII_TXD2_CONN_MLB_CLK                      IMX8QXP_ENET0_RGMII_TXD2              1
#define IMX8QXP_ENET0_RGMII_TXD2_CONN_NAND_CE0_B                   IMX8QXP_ENET0_RGMII_TXD2              2
#define IMX8QXP_ENET0_RGMII_TXD2_CONN_USDHC1_CD_B                  IMX8QXP_ENET0_RGMII_TXD2              3
#define IMX8QXP_ENET0_RGMII_TXD2_LSIO_GPIO5_IO01                   IMX8QXP_ENET0_RGMII_TXD2              4
#define IMX8QXP_ENET0_RGMII_TXD3_CONN_ENET0_RGMII_TXD3             IMX8QXP_ENET0_RGMII_TXD3              0
#define IMX8QXP_ENET0_RGMII_TXD3_CONN_MLB_SIG                      IMX8QXP_ENET0_RGMII_TXD3              1
#define IMX8QXP_ENET0_RGMII_TXD3_CONN_NAND_RE_B                    IMX8QXP_ENET0_RGMII_TXD3              2
#define IMX8QXP_ENET0_RGMII_TXD3_LSIO_GPIO5_IO02                   IMX8QXP_ENET0_RGMII_TXD3              4
#define IMX8QXP_ENET0_RGMII_RXC_CONN_ENET0_RGMII_RXC               IMX8QXP_ENET0_RGMII_RXC               0
#define IMX8QXP_ENET0_RGMII_RXC_CONN_MLB_DATA                      IMX8QXP_ENET0_RGMII_RXC               1
#define IMX8QXP_ENET0_RGMII_RXC_CONN_NAND_WE_B                     IMX8QXP_ENET0_RGMII_RXC               2
#define IMX8QXP_ENET0_RGMII_RXC_CONN_USDHC1_CLK                    IMX8QXP_ENET0_RGMII_RXC               3
#define IMX8QXP_ENET0_RGMII_RXC_LSIO_GPIO5_IO03                    IMX8QXP_ENET0_RGMII_RXC               4
#define IMX8QXP_ENET0_RGMII_RX_CTL_CONN_ENET0_RGMII_RX_CTL         IMX8QXP_ENET0_RGMII_RX_CTL            0
#define IMX8QXP_ENET0_RGMII_RX_CTL_CONN_USDHC1_CMD                 IMX8QXP_ENET0_RGMII_RX_CTL            3
#define IMX8QXP_ENET0_RGMII_RX_CTL_LSIO_GPIO5_IO04                 IMX8QXP_ENET0_RGMII_RX_CTL            4
#define IMX8QXP_ENET0_RGMII_RXD0_CONN_ENET0_RGMII_RXD0             IMX8QXP_ENET0_RGMII_RXD0              0
#define IMX8QXP_ENET0_RGMII_RXD0_CONN_USDHC1_DATA0                 IMX8QXP_ENET0_RGMII_RXD0              3
#define IMX8QXP_ENET0_RGMII_RXD0_LSIO_GPIO5_IO05                   IMX8QXP_ENET0_RGMII_RXD0              4
#define IMX8QXP_ENET0_RGMII_RXD1_CONN_ENET0_RGMII_RXD1             IMX8QXP_ENET0_RGMII_RXD1              0
#define IMX8QXP_ENET0_RGMII_RXD1_CONN_USDHC1_DATA1                 IMX8QXP_ENET0_RGMII_RXD1              3
#define IMX8QXP_ENET0_RGMII_RXD1_LSIO_GPIO5_IO06                   IMX8QXP_ENET0_RGMII_RXD1              4
#define IMX8QXP_ENET0_RGMII_RXD2_CONN_ENET0_RGMII_RXD2             IMX8QXP_ENET0_RGMII_RXD2              0
#define IMX8QXP_ENET0_RGMII_RXD2_CONN_ENET0_RMII_RX_ER             IMX8QXP_ENET0_RGMII_RXD2              1
#define IMX8QXP_ENET0_RGMII_RXD2_CONN_USDHC1_DATA2                 IMX8QXP_ENET0_RGMII_RXD2              3
#define IMX8QXP_ENET0_RGMII_RXD2_LSIO_GPIO5_IO07                   IMX8QXP_ENET0_RGMII_RXD2              4
#define IMX8QXP_ENET0_RGMII_RXD3_CONN_ENET0_RGMII_RXD3             IMX8QXP_ENET0_RGMII_RXD3              0
#define IMX8QXP_ENET0_RGMII_RXD3_CONN_NAND_ALE                     IMX8QXP_ENET0_RGMII_RXD3              2
#define IMX8QXP_ENET0_RGMII_RXD3_CONN_USDHC1_DATA3                 IMX8QXP_ENET0_RGMII_RXD3              3
#define IMX8QXP_ENET0_RGMII_RXD3_LSIO_GPIO5_IO08                   IMX8QXP_ENET0_RGMII_RXD3              4
#define IMX8QXP_ENET0_REFCLK_125M_25M_CONN_ENET0_REFCLK_125M_25M   IMX8QXP_ENET0_REFCLK_125M_25M         0
#define IMX8QXP_ENET0_REFCLK_125M_25M_CONN_ENET0_PPS               IMX8QXP_ENET0_REFCLK_125M_25M         1
#define IMX8QXP_ENET0_REFCLK_125M_25M_CONN_ENET1_PPS               IMX8QXP_ENET0_REFCLK_125M_25M         2
#define IMX8QXP_ENET0_REFCLK_125M_25M_LSIO_GPIO5_IO09              IMX8QXP_ENET0_REFCLK_125M_25M         4
#define IMX8QXP_ENET0_MDIO_CONN_ENET0_MDIO                         IMX8QXP_ENET0_MDIO                    0
#define IMX8QXP_ENET0_MDIO_ADMA_I2C3_SDA                           IMX8QXP_ENET0_MDIO                    1
#define IMX8QXP_ENET0_MDIO_CONN_ENET1_MDIO                         IMX8QXP_ENET0_MDIO                    2
#define IMX8QXP_ENET0_MDIO_LSIO_GPIO5_IO10                         IMX8QXP_ENET0_MDIO                    4
#define IMX8QXP_ENET0_MDC_CONN_ENET0_MDC                           IMX8QXP_ENET0_MDC                     0
#define IMX8QXP_ENET0_MDC_ADMA_I2C3_SCL                            IMX8QXP_ENET0_MDC                     1
#define IMX8QXP_ENET0_MDC_CONN_ENET1_MDC                           IMX8QXP_ENET0_MDC                     2
#define IMX8QXP_ENET0_MDC_LSIO_GPIO5_IO11                          IMX8QXP_ENET0_MDC                     4
#define IMX8QXP_ESAI0_FSR_ADMA_ESAI0_FSR                           IMX8QXP_ESAI0_FSR                     0
#define IMX8QXP_ESAI0_FSR_CONN_ENET1_RCLK50M_OUT                   IMX8QXP_ESAI0_FSR                     1
#define IMX8QXP_ESAI0_FSR_ADMA_LCDIF_D00                           IMX8QXP_ESAI0_FSR                     2
#define IMX8QXP_ESAI0_FSR_CONN_ENET1_RGMII_TXC                     IMX8QXP_ESAI0_FSR                     3
#define IMX8QXP_ESAI0_FSR_CONN_ENET1_RCLK50M_IN                    IMX8QXP_ESAI0_FSR                     4
#define IMX8QXP_ESAI0_FST_ADMA_ESAI0_FST                           IMX8QXP_ESAI0_FST                     0
#define IMX8QXP_ESAI0_FST_CONN_MLB_CLK                             IMX8QXP_ESAI0_FST                     1
#define IMX8QXP_ESAI0_FST_ADMA_LCDIF_D01                           IMX8QXP_ESAI0_FST                     2
#define IMX8QXP_ESAI0_FST_CONN_ENET1_RGMII_TXD2                    IMX8QXP_ESAI0_FST                     3
#define IMX8QXP_ESAI0_FST_LSIO_GPIO0_IO01                          IMX8QXP_ESAI0_FST                     4
#define IMX8QXP_ESAI0_SCKR_ADMA_ESAI0_SCKR                         IMX8QXP_ESAI0_SCKR                    0
#define IMX8QXP_ESAI0_SCKR_ADMA_LCDIF_D02                          IMX8QXP_ESAI0_SCKR                    2
#define IMX8QXP_ESAI0_SCKR_CONN_ENET1_RGMII_TX_CTL                 IMX8QXP_ESAI0_SCKR                    3
#define IMX8QXP_ESAI0_SCKR_LSIO_GPIO0_IO02                         IMX8QXP_ESAI0_SCKR                    4
#define IMX8QXP_ESAI0_SCKT_ADMA_ESAI0_SCKT                         IMX8QXP_ESAI0_SCKT                    0
#define IMX8QXP_ESAI0_SCKT_CONN_MLB_SIG                            IMX8QXP_ESAI0_SCKT                    1
#define IMX8QXP_ESAI0_SCKT_ADMA_LCDIF_D03                          IMX8QXP_ESAI0_SCKT                    2
#define IMX8QXP_ESAI0_SCKT_CONN_ENET1_RGMII_TXD3                   IMX8QXP_ESAI0_SCKT                    3
#define IMX8QXP_ESAI0_SCKT_LSIO_GPIO0_IO03                         IMX8QXP_ESAI0_SCKT                    4
#define IMX8QXP_ESAI0_TX0_ADMA_ESAI0_TX0                           IMX8QXP_ESAI0_TX0                     0
#define IMX8QXP_ESAI0_TX0_CONN_MLB_DATA                            IMX8QXP_ESAI0_TX0                     1
#define IMX8QXP_ESAI0_TX0_ADMA_LCDIF_D04                           IMX8QXP_ESAI0_TX0                     2
#define IMX8QXP_ESAI0_TX0_CONN_ENET1_RGMII_RXC                     IMX8QXP_ESAI0_TX0                     3
#define IMX8QXP_ESAI0_TX0_LSIO_GPIO0_IO04                          IMX8QXP_ESAI0_TX0                     4
#define IMX8QXP_ESAI0_TX1_ADMA_ESAI0_TX1                           IMX8QXP_ESAI0_TX1                     0
#define IMX8QXP_ESAI0_TX1_ADMA_LCDIF_D05                           IMX8QXP_ESAI0_TX1                     2
#define IMX8QXP_ESAI0_TX1_CONN_ENET1_RGMII_RXD3                    IMX8QXP_ESAI0_TX1                     3
#define IMX8QXP_ESAI0_TX1_LSIO_GPIO0_IO05                          IMX8QXP_ESAI0_TX1                     4
#define IMX8QXP_ESAI0_TX2_RX3_ADMA_ESAI0_TX2_RX3                   IMX8QXP_ESAI0_TX2_RX3                 0
#define IMX8QXP_ESAI0_TX2_RX3_CONN_ENET1_RMII_RX_ER                IMX8QXP_ESAI0_TX2_RX3                 1
#define IMX8QXP_ESAI0_TX2_RX3_ADMA_LCDIF_D06                       IMX8QXP_ESAI0_TX2_RX3                 2
#define IMX8QXP_ESAI0_TX2_RX3_CONN_ENET1_RGMII_RXD2                IMX8QXP_ESAI0_TX2_RX3                 3
#define IMX8QXP_ESAI0_TX2_RX3_LSIO_GPIO0_IO06                      IMX8QXP_ESAI0_TX2_RX3                 4
#define IMX8QXP_ESAI0_TX3_RX2_ADMA_ESAI0_TX3_RX2                   IMX8QXP_ESAI0_TX3_RX2                 0
#define IMX8QXP_ESAI0_TX3_RX2_ADMA_LCDIF_D07                       IMX8QXP_ESAI0_TX3_RX2                 2
#define IMX8QXP_ESAI0_TX3_RX2_CONN_ENET1_RGMII_RXD1                IMX8QXP_ESAI0_TX3_RX2                 3
#define IMX8QXP_ESAI0_TX3_RX2_LSIO_GPIO0_IO07                      IMX8QXP_ESAI0_TX3_RX2                 4
#define IMX8QXP_ESAI0_TX4_RX1_ADMA_ESAI0_TX4_RX1                   IMX8QXP_ESAI0_TX4_RX1                 0
#define IMX8QXP_ESAI0_TX4_RX1_ADMA_LCDIF_D08                       IMX8QXP_ESAI0_TX4_RX1                 2
#define IMX8QXP_ESAI0_TX4_RX1_CONN_ENET1_RGMII_TXD0                IMX8QXP_ESAI0_TX4_RX1                 3
#define IMX8QXP_ESAI0_TX4_RX1_LSIO_GPIO0_IO08                      IMX8QXP_ESAI0_TX4_RX1                 4
#define IMX8QXP_ESAI0_TX5_RX0_ADMA_ESAI0_TX5_RX0                   IMX8QXP_ESAI0_TX5_RX0                 0
#define IMX8QXP_ESAI0_TX5_RX0_ADMA_LCDIF_D09                       IMX8QXP_ESAI0_TX5_RX0                 2
#define IMX8QXP_ESAI0_TX5_RX0_CONN_ENET1_RGMII_TXD1                IMX8QXP_ESAI0_TX5_RX0                 3
#define IMX8QXP_ESAI0_TX5_RX0_LSIO_GPIO0_IO09                      IMX8QXP_ESAI0_TX5_RX0                 4
#define IMX8QXP_SPDIF0_RX_ADMA_SPDIF0_RX                           IMX8QXP_SPDIF0_RX                     0
#define IMX8QXP_SPDIF0_RX_ADMA_MQS_R                               IMX8QXP_SPDIF0_RX                     1
#define IMX8QXP_SPDIF0_RX_ADMA_LCDIF_D10                           IMX8QXP_SPDIF0_RX                     2
#define IMX8QXP_SPDIF0_RX_CONN_ENET1_RGMII_RXD0                    IMX8QXP_SPDIF0_RX                     3
#define IMX8QXP_SPDIF0_RX_LSIO_GPIO0_IO10                          IMX8QXP_SPDIF0_RX                     4
#define IMX8QXP_SPDIF0_TX_ADMA_SPDIF0_TX                           IMX8QXP_SPDIF0_TX                     0
#define IMX8QXP_SPDIF0_TX_ADMA_MQS_L                               IMX8QXP_SPDIF0_TX                     1
#define IMX8QXP_SPDIF0_TX_ADMA_LCDIF_D11                           IMX8QXP_SPDIF0_TX                     2
#define IMX8QXP_SPDIF0_TX_CONN_ENET1_RGMII_RX_CTL                  IMX8QXP_SPDIF0_TX                     3
#define IMX8QXP_SPDIF0_TX_LSIO_GPIO0_IO11                          IMX8QXP_SPDIF0_TX                     4
#define IMX8QXP_SPDIF0_EXT_CLK_ADMA_SPDIF0_EXT_CLK                 IMX8QXP_SPDIF0_EXT_CLK                0
#define IMX8QXP_SPDIF0_EXT_CLK_ADMA_LCDIF_D12                      IMX8QXP_SPDIF0_EXT_CLK                2
#define IMX8QXP_SPDIF0_EXT_CLK_CONN_ENET1_REFCLK_125M_25M          IMX8QXP_SPDIF0_EXT_CLK                3
#define IMX8QXP_SPDIF0_EXT_CLK_LSIO_GPIO0_IO12                     IMX8QXP_SPDIF0_EXT_CLK                4
#define IMX8QXP_SPI3_SCK_ADMA_SPI3_SCK                             IMX8QXP_SPI3_SCK                      0
#define IMX8QXP_SPI3_SCK_ADMA_LCDIF_D13                            IMX8QXP_SPI3_SCK                      2
#define IMX8QXP_SPI3_SCK_LSIO_GPIO0_IO13                           IMX8QXP_SPI3_SCK                      4
#define IMX8QXP_SPI3_SDO_ADMA_SPI3_SDO                             IMX8QXP_SPI3_SDO                      0
#define IMX8QXP_SPI3_SDO_ADMA_LCDIF_D14                            IMX8QXP_SPI3_SDO                      2
#define IMX8QXP_SPI3_SDO_LSIO_GPIO0_IO14                           IMX8QXP_SPI3_SDO                      4
#define IMX8QXP_SPI3_SDI_ADMA_SPI3_SDI                             IMX8QXP_SPI3_SDI                      0
#define IMX8QXP_SPI3_SDI_ADMA_LCDIF_D15                            IMX8QXP_SPI3_SDI                      2
#define IMX8QXP_SPI3_SDI_LSIO_GPIO0_IO15                           IMX8QXP_SPI3_SDI                      4
#define IMX8QXP_SPI3_CS0_ADMA_SPI3_CS0                             IMX8QXP_SPI3_CS0                      0
#define IMX8QXP_SPI3_CS0_ADMA_ACM_MCLK_OUT1                        IMX8QXP_SPI3_CS0                      1
#define IMX8QXP_SPI3_CS0_ADMA_LCDIF_HSYNC                          IMX8QXP_SPI3_CS0                      2
#define IMX8QXP_SPI3_CS0_LSIO_GPIO0_IO16                           IMX8QXP_SPI3_CS0                      4
#define IMX8QXP_SPI3_CS1_ADMA_SPI3_CS1                             IMX8QXP_SPI3_CS1                      0
#define IMX8QXP_SPI3_CS1_ADMA_I2C3_SCL                             IMX8QXP_SPI3_CS1                      1
#define IMX8QXP_SPI3_CS1_ADMA_LCDIF_RESET                          IMX8QXP_SPI3_CS1                      2
#define IMX8QXP_SPI3_CS1_ADMA_SPI2_CS0                             IMX8QXP_SPI3_CS1                      3
#define IMX8QXP_SPI3_CS1_ADMA_LCDIF_D16                            IMX8QXP_SPI3_CS1                      4
#define IMX8QXP_MCLK_IN1_ADMA_ACM_MCLK_IN1                         IMX8QXP_MCLK_IN1                      0
#define IMX8QXP_MCLK_IN1_ADMA_I2C3_SDA                             IMX8QXP_MCLK_IN1                      1
#define IMX8QXP_MCLK_IN1_ADMA_LCDIF_EN                             IMX8QXP_MCLK_IN1                      2
#define IMX8QXP_MCLK_IN1_ADMA_SPI2_SCK                             IMX8QXP_MCLK_IN1                      3
#define IMX8QXP_MCLK_IN1_ADMA_LCDIF_D17                            IMX8QXP_MCLK_IN1                      4
#define IMX8QXP_MCLK_IN0_ADMA_ACM_MCLK_IN0                         IMX8QXP_MCLK_IN0                      0
#define IMX8QXP_MCLK_IN0_ADMA_ESAI0_RX_HF_CLK                      IMX8QXP_MCLK_IN0                      1
#define IMX8QXP_MCLK_IN0_ADMA_LCDIF_VSYNC                          IMX8QXP_MCLK_IN0                      2
#define IMX8QXP_MCLK_IN0_ADMA_SPI2_SDI                             IMX8QXP_MCLK_IN0                      3
#define IMX8QXP_MCLK_IN0_LSIO_GPIO0_IO19                           IMX8QXP_MCLK_IN0                      4
#define IMX8QXP_MCLK_OUT0_ADMA_ACM_MCLK_OUT0                       IMX8QXP_MCLK_OUT0                     0
#define IMX8QXP_MCLK_OUT0_ADMA_ESAI0_TX_HF_CLK                     IMX8QXP_MCLK_OUT0                     1
#define IMX8QXP_MCLK_OUT0_ADMA_LCDIF_CLK                           IMX8QXP_MCLK_OUT0                     2
#define IMX8QXP_MCLK_OUT0_ADMA_SPI2_SDO                            IMX8QXP_MCLK_OUT0                     3
#define IMX8QXP_MCLK_OUT0_LSIO_GPIO0_IO20                          IMX8QXP_MCLK_OUT0                     4
#define IMX8QXP_UART1_TX_ADMA_UART1_TX                             IMX8QXP_UART1_TX                      0
#define IMX8QXP_UART1_TX_LSIO_PWM0_OUT                             IMX8QXP_UART1_TX                      1
#define IMX8QXP_UART1_TX_LSIO_GPT0_CAPTURE                         IMX8QXP_UART1_TX                      2
#define IMX8QXP_UART1_TX_LSIO_GPIO0_IO21                           IMX8QXP_UART1_TX                      4
#define IMX8QXP_UART1_RX_ADMA_UART1_RX                             IMX8QXP_UART1_RX                      0
#define IMX8QXP_UART1_RX_LSIO_PWM1_OUT                             IMX8QXP_UART1_RX                      1
#define IMX8QXP_UART1_RX_LSIO_GPT0_COMPARE                         IMX8QXP_UART1_RX                      2
#define IMX8QXP_UART1_RX_LSIO_GPT1_CLK                             IMX8QXP_UART1_RX                      3
#define IMX8QXP_UART1_RX_LSIO_GPIO0_IO22                           IMX8QXP_UART1_RX                      4
#define IMX8QXP_UART1_RTS_B_ADMA_UART1_RTS_B                       IMX8QXP_UART1_RTS_B                   0
#define IMX8QXP_UART1_RTS_B_LSIO_PWM2_OUT                          IMX8QXP_UART1_RTS_B                   1
#define IMX8QXP_UART1_RTS_B_ADMA_LCDIF_D16                         IMX8QXP_UART1_RTS_B                   2
#define IMX8QXP_UART1_RTS_B_LSIO_GPT1_CAPTURE                      IMX8QXP_UART1_RTS_B                   3
#define IMX8QXP_UART1_RTS_B_LSIO_GPT0_CLK                          IMX8QXP_UART1_RTS_B                   4
#define IMX8QXP_UART1_CTS_B_ADMA_UART1_CTS_B                       IMX8QXP_UART1_CTS_B                   0
#define IMX8QXP_UART1_CTS_B_LSIO_PWM3_OUT                          IMX8QXP_UART1_CTS_B                   1
#define IMX8QXP_UART1_CTS_B_ADMA_LCDIF_D17                         IMX8QXP_UART1_CTS_B                   2
#define IMX8QXP_UART1_CTS_B_LSIO_GPT1_COMPARE                      IMX8QXP_UART1_CTS_B                   3
#define IMX8QXP_UART1_CTS_B_LSIO_GPIO0_IO24                        IMX8QXP_UART1_CTS_B                   4
#define IMX8QXP_SAI0_TXD_ADMA_SAI0_TXD                             IMX8QXP_SAI0_TXD                      0
#define IMX8QXP_SAI0_TXD_ADMA_SAI1_RXC                             IMX8QXP_SAI0_TXD                      1
#define IMX8QXP_SAI0_TXD_ADMA_SPI1_SDO                             IMX8QXP_SAI0_TXD                      2
#define IMX8QXP_SAI0_TXD_ADMA_LCDIF_D18                            IMX8QXP_SAI0_TXD                      3
#define IMX8QXP_SAI0_TXD_LSIO_GPIO0_IO25                           IMX8QXP_SAI0_TXD                      4
#define IMX8QXP_SAI0_TXC_ADMA_SAI0_TXC                             IMX8QXP_SAI0_TXC                      0
#define IMX8QXP_SAI0_TXC_ADMA_SAI1_TXD                             IMX8QXP_SAI0_TXC                      1
#define IMX8QXP_SAI0_TXC_ADMA_SPI1_SDI                             IMX8QXP_SAI0_TXC                      2
#define IMX8QXP_SAI0_TXC_ADMA_LCDIF_D19                            IMX8QXP_SAI0_TXC                      3
#define IMX8QXP_SAI0_TXC_LSIO_GPIO0_IO26                           IMX8QXP_SAI0_TXC                      4
#define IMX8QXP_SAI0_RXD_ADMA_SAI0_RXD                             IMX8QXP_SAI0_RXD                      0
#define IMX8QXP_SAI0_RXD_ADMA_SAI1_RXFS                            IMX8QXP_SAI0_RXD                      1
#define IMX8QXP_SAI0_RXD_ADMA_SPI1_CS0                             IMX8QXP_SAI0_RXD                      2
#define IMX8QXP_SAI0_RXD_ADMA_LCDIF_D20                            IMX8QXP_SAI0_RXD                      3
#define IMX8QXP_SAI0_RXD_LSIO_GPIO0_IO27                           IMX8QXP_SAI0_RXD                      4
#define IMX8QXP_SAI0_TXFS_ADMA_SAI0_TXFS                           IMX8QXP_SAI0_TXFS                     0
#define IMX8QXP_SAI0_TXFS_ADMA_SPI2_CS1                            IMX8QXP_SAI0_TXFS                     1
#define IMX8QXP_SAI0_TXFS_ADMA_SPI1_SCK                            IMX8QXP_SAI0_TXFS                     2
#define IMX8QXP_SAI0_TXFS_LSIO_GPIO0_IO28                          IMX8QXP_SAI0_TXFS                     4
#define IMX8QXP_SAI1_RXD_ADMA_SAI1_RXD                             IMX8QXP_SAI1_RXD                      0
#define IMX8QXP_SAI1_RXD_ADMA_SAI0_RXFS                            IMX8QXP_SAI1_RXD                      1
#define IMX8QXP_SAI1_RXD_ADMA_SPI1_CS1                             IMX8QXP_SAI1_RXD                      2
#define IMX8QXP_SAI1_RXD_ADMA_LCDIF_D21                            IMX8QXP_SAI1_RXD                      3
#define IMX8QXP_SAI1_RXD_LSIO_GPIO0_IO29                           IMX8QXP_SAI1_RXD                      4
#define IMX8QXP_SAI1_RXC_ADMA_SAI1_RXC                             IMX8QXP_SAI1_RXC                      0
#define IMX8QXP_SAI1_RXC_ADMA_SAI1_TXC                             IMX8QXP_SAI1_RXC                      1
#define IMX8QXP_SAI1_RXC_ADMA_LCDIF_D22                            IMX8QXP_SAI1_RXC                      3
#define IMX8QXP_SAI1_RXC_LSIO_GPIO0_IO30                           IMX8QXP_SAI1_RXC                      4
#define IMX8QXP_SAI1_RXFS_ADMA_SAI1_RXFS                           IMX8QXP_SAI1_RXFS                     0
#define IMX8QXP_SAI1_RXFS_ADMA_SAI1_TXFS                           IMX8QXP_SAI1_RXFS                     1
#define IMX8QXP_SAI1_RXFS_ADMA_LCDIF_D23                           IMX8QXP_SAI1_RXFS                     3
#define IMX8QXP_SAI1_RXFS_LSIO_GPIO0_IO31                          IMX8QXP_SAI1_RXFS                     4
#define IMX8QXP_SPI2_CS0_ADMA_SPI2_CS0                             IMX8QXP_SPI2_CS0                      0
#define IMX8QXP_SPI2_CS0_LSIO_GPIO1_IO00                           IMX8QXP_SPI2_CS0                      4
#define IMX8QXP_SPI2_SDO_ADMA_SPI2_SDO                             IMX8QXP_SPI2_SDO                      0
#define IMX8QXP_SPI2_SDO_LSIO_GPIO1_IO01                           IMX8QXP_SPI2_SDO                      4
#define IMX8QXP_SPI2_SDI_ADMA_SPI2_SDI                             IMX8QXP_SPI2_SDI                      0
#define IMX8QXP_SPI2_SDI_LSIO_GPIO1_IO02                           IMX8QXP_SPI2_SDI                      4
#define IMX8QXP_SPI2_SCK_ADMA_SPI2_SCK                             IMX8QXP_SPI2_SCK                      0
#define IMX8QXP_SPI2_SCK_LSIO_GPIO1_IO03                           IMX8QXP_SPI2_SCK                      4
#define IMX8QXP_SPI0_SCK_ADMA_SPI0_SCK                             IMX8QXP_SPI0_SCK                      0
#define IMX8QXP_SPI0_SCK_ADMA_SAI0_TXC                             IMX8QXP_SPI0_SCK                      1
#define IMX8QXP_SPI0_SCK_M40_I2C0_SCL                              IMX8QXP_SPI0_SCK                      2
#define IMX8QXP_SPI0_SCK_M40_GPIO0_IO00                            IMX8QXP_SPI0_SCK                      3
#define IMX8QXP_SPI0_SCK_LSIO_GPIO1_IO04                           IMX8QXP_SPI0_SCK                      4
#define IMX8QXP_SPI0_SDI_ADMA_SPI0_SDI                             IMX8QXP_SPI0_SDI                      0
#define IMX8QXP_SPI0_SDI_ADMA_SAI0_TXD                             IMX8QXP_SPI0_SDI                      1
#define IMX8QXP_SPI0_SDI_M40_TPM0_CH0                              IMX8QXP_SPI0_SDI                      2
#define IMX8QXP_SPI0_SDI_M40_GPIO0_IO02                            IMX8QXP_SPI0_SDI                      3
#define IMX8QXP_SPI0_SDI_LSIO_GPIO1_IO05                           IMX8QXP_SPI0_SDI                      4
#define IMX8QXP_SPI0_SDO_ADMA_SPI0_SDO                             IMX8QXP_SPI0_SDO                      0
#define IMX8QXP_SPI0_SDO_ADMA_SAI0_TXFS                            IMX8QXP_SPI0_SDO                      1
#define IMX8QXP_SPI0_SDO_M40_I2C0_SDA                              IMX8QXP_SPI0_SDO                      2
#define IMX8QXP_SPI0_SDO_M40_GPIO0_IO01                            IMX8QXP_SPI0_SDO                      3
#define IMX8QXP_SPI0_SDO_LSIO_GPIO1_IO06                           IMX8QXP_SPI0_SDO                      4
#define IMX8QXP_SPI0_CS1_ADMA_SPI0_CS1                             IMX8QXP_SPI0_CS1                      0
#define IMX8QXP_SPI0_CS1_ADMA_SAI0_RXC                             IMX8QXP_SPI0_CS1                      1
#define IMX8QXP_SPI0_CS1_ADMA_SAI1_TXD                             IMX8QXP_SPI0_CS1                      2
#define IMX8QXP_SPI0_CS1_ADMA_LCD_PWM0_OUT                         IMX8QXP_SPI0_CS1                      3
#define IMX8QXP_SPI0_CS1_LSIO_GPIO1_IO07                           IMX8QXP_SPI0_CS1                      4
#define IMX8QXP_SPI0_CS0_ADMA_SPI0_CS0                             IMX8QXP_SPI0_CS0                      0
#define IMX8QXP_SPI0_CS0_ADMA_SAI0_RXD                             IMX8QXP_SPI0_CS0                      1
#define IMX8QXP_SPI0_CS0_M40_TPM0_CH1                              IMX8QXP_SPI0_CS0                      2
#define IMX8QXP_SPI0_CS0_M40_GPIO0_IO03                            IMX8QXP_SPI0_CS0                      3
#define IMX8QXP_SPI0_CS0_LSIO_GPIO1_IO08                           IMX8QXP_SPI0_CS0                      4
#define IMX8QXP_ADC_IN1_ADMA_ADC_IN1                               IMX8QXP_ADC_IN1                       0
#define IMX8QXP_ADC_IN1_M40_I2C0_SDA                               IMX8QXP_ADC_IN1                       1
#define IMX8QXP_ADC_IN1_M40_GPIO0_IO01                             IMX8QXP_ADC_IN1                       2
#define IMX8QXP_ADC_IN1_LSIO_GPIO1_IO09                            IMX8QXP_ADC_IN1                       4
#define IMX8QXP_ADC_IN0_ADMA_ADC_IN0                               IMX8QXP_ADC_IN0                       0
#define IMX8QXP_ADC_IN0_M40_I2C0_SCL                               IMX8QXP_ADC_IN0                       1
#define IMX8QXP_ADC_IN0_M40_GPIO0_IO00                             IMX8QXP_ADC_IN0                       2
#define IMX8QXP_ADC_IN0_LSIO_GPIO1_IO10                            IMX8QXP_ADC_IN0                       4
#define IMX8QXP_ADC_IN3_ADMA_ADC_IN3                               IMX8QXP_ADC_IN3                       0
#define IMX8QXP_ADC_IN3_M40_UART0_TX                               IMX8QXP_ADC_IN3                       1
#define IMX8QXP_ADC_IN3_M40_GPIO0_IO03                             IMX8QXP_ADC_IN3                       2
#define IMX8QXP_ADC_IN3_ADMA_ACM_MCLK_OUT0                         IMX8QXP_ADC_IN3                       3
#define IMX8QXP_ADC_IN3_LSIO_GPIO1_IO11                            IMX8QXP_ADC_IN3                       4
#define IMX8QXP_ADC_IN2_ADMA_ADC_IN2                               IMX8QXP_ADC_IN2                       0
#define IMX8QXP_ADC_IN2_M40_UART0_RX                               IMX8QXP_ADC_IN2                       1
#define IMX8QXP_ADC_IN2_M40_GPIO0_IO02                             IMX8QXP_ADC_IN2                       2
#define IMX8QXP_ADC_IN2_ADMA_ACM_MCLK_IN0                          IMX8QXP_ADC_IN2                       3
#define IMX8QXP_ADC_IN2_LSIO_GPIO1_IO12                            IMX8QXP_ADC_IN2                       4
#define IMX8QXP_ADC_IN5_ADMA_ADC_IN5                               IMX8QXP_ADC_IN5                       0
#define IMX8QXP_ADC_IN5_M40_TPM0_CH1                               IMX8QXP_ADC_IN5                       1
#define IMX8QXP_ADC_IN5_M40_GPIO0_IO05                             IMX8QXP_ADC_IN5                       2
#define IMX8QXP_ADC_IN5_LSIO_GPIO1_IO13                            IMX8QXP_ADC_IN5                       4
#define IMX8QXP_ADC_IN4_ADMA_ADC_IN4                               IMX8QXP_ADC_IN4                       0
#define IMX8QXP_ADC_IN4_M40_TPM0_CH0                               IMX8QXP_ADC_IN4                       1
#define IMX8QXP_ADC_IN4_M40_GPIO0_IO04                             IMX8QXP_ADC_IN4                       2
#define IMX8QXP_ADC_IN4_LSIO_GPIO1_IO14                            IMX8QXP_ADC_IN4                       4
#define IMX8QXP_FLEXCAN0_RX_ADMA_FLEXCAN0_RX                       IMX8QXP_FLEXCAN0_RX                   0
#define IMX8QXP_FLEXCAN0_RX_ADMA_SAI2_RXC                          IMX8QXP_FLEXCAN0_RX                   1
#define IMX8QXP_FLEXCAN0_RX_ADMA_UART0_RTS_B                       IMX8QXP_FLEXCAN0_RX                   2
#define IMX8QXP_FLEXCAN0_RX_ADMA_SAI1_TXC                          IMX8QXP_FLEXCAN0_RX                   3
#define IMX8QXP_FLEXCAN0_RX_LSIO_GPIO1_IO15                        IMX8QXP_FLEXCAN0_RX                   4
#define IMX8QXP_FLEXCAN0_TX_ADMA_FLEXCAN0_TX                       IMX8QXP_FLEXCAN0_TX                   0
#define IMX8QXP_FLEXCAN0_TX_ADMA_SAI2_RXD                          IMX8QXP_FLEXCAN0_TX                   1
#define IMX8QXP_FLEXCAN0_TX_ADMA_UART0_CTS_B                       IMX8QXP_FLEXCAN0_TX                   2
#define IMX8QXP_FLEXCAN0_TX_ADMA_SAI1_TXFS                         IMX8QXP_FLEXCAN0_TX                   3
#define IMX8QXP_FLEXCAN0_TX_LSIO_GPIO1_IO16                        IMX8QXP_FLEXCAN0_TX                   4
#define IMX8QXP_FLEXCAN1_RX_ADMA_FLEXCAN1_RX                       IMX8QXP_FLEXCAN1_RX                   0
#define IMX8QXP_FLEXCAN1_RX_ADMA_SAI2_RXFS                         IMX8QXP_FLEXCAN1_RX                   1
#define IMX8QXP_FLEXCAN1_RX_ADMA_FTM_CH2                           IMX8QXP_FLEXCAN1_RX                   2
#define IMX8QXP_FLEXCAN1_RX_ADMA_SAI1_TXD                          IMX8QXP_FLEXCAN1_RX                   3
#define IMX8QXP_FLEXCAN1_RX_LSIO_GPIO1_IO17                        IMX8QXP_FLEXCAN1_RX                   4
#define IMX8QXP_FLEXCAN1_TX_ADMA_FLEXCAN1_TX                       IMX8QXP_FLEXCAN1_TX                   0
#define IMX8QXP_FLEXCAN1_TX_ADMA_SAI3_RXC                          IMX8QXP_FLEXCAN1_TX                   1
#define IMX8QXP_FLEXCAN1_TX_ADMA_DMA0_REQ_IN0                      IMX8QXP_FLEXCAN1_TX                   2
#define IMX8QXP_FLEXCAN1_TX_ADMA_SAI1_RXD                          IMX8QXP_FLEXCAN1_TX                   3
#define IMX8QXP_FLEXCAN1_TX_LSIO_GPIO1_IO18                        IMX8QXP_FLEXCAN1_TX                   4
#define IMX8QXP_FLEXCAN2_RX_ADMA_FLEXCAN2_RX                       IMX8QXP_FLEXCAN2_RX                   0
#define IMX8QXP_FLEXCAN2_RX_ADMA_SAI3_RXD                          IMX8QXP_FLEXCAN2_RX                   1
#define IMX8QXP_FLEXCAN2_RX_ADMA_UART3_RX                          IMX8QXP_FLEXCAN2_RX                   2
#define IMX8QXP_FLEXCAN2_RX_ADMA_SAI1_RXFS                         IMX8QXP_FLEXCAN2_RX                   3
#define IMX8QXP_FLEXCAN2_RX_LSIO_GPIO1_IO19                        IMX8QXP_FLEXCAN2_RX                   4
#define IMX8QXP_FLEXCAN2_TX_ADMA_FLEXCAN2_TX                       IMX8QXP_FLEXCAN2_TX                   0
#define IMX8QXP_FLEXCAN2_TX_ADMA_SAI3_RXFS                         IMX8QXP_FLEXCAN2_TX                   1
#define IMX8QXP_FLEXCAN2_TX_ADMA_UART3_TX                          IMX8QXP_FLEXCAN2_TX                   2
#define IMX8QXP_FLEXCAN2_TX_ADMA_SAI1_RXC                          IMX8QXP_FLEXCAN2_TX                   3
#define IMX8QXP_FLEXCAN2_TX_LSIO_GPIO1_IO20                        IMX8QXP_FLEXCAN2_TX                   4
#define IMX8QXP_UART0_RX_ADMA_UART0_RX                             IMX8QXP_UART0_RX                      0
#define IMX8QXP_UART0_RX_ADMA_MQS_R                                IMX8QXP_UART0_RX                      1
#define IMX8QXP_UART0_RX_ADMA_FLEXCAN0_RX                          IMX8QXP_UART0_RX                      2
#define IMX8QXP_UART0_RX_LSIO_GPIO1_IO21                           IMX8QXP_UART0_RX                      4
#define IMX8QXP_UART0_TX_ADMA_UART0_TX                             IMX8QXP_UART0_TX                      0
#define IMX8QXP_UART0_TX_ADMA_MQS_L                                IMX8QXP_UART0_TX                      1
#define IMX8QXP_UART0_TX_ADMA_FLEXCAN0_TX                          IMX8QXP_UART0_TX                      2
#define IMX8QXP_UART0_TX_LSIO_GPIO1_IO22                           IMX8QXP_UART0_TX                      4
#define IMX8QXP_UART2_TX_ADMA_UART2_TX                             IMX8QXP_UART2_TX                      0
#define IMX8QXP_UART2_TX_ADMA_FTM_CH1                              IMX8QXP_UART2_TX                      1
#define IMX8QXP_UART2_TX_ADMA_FLEXCAN1_TX                          IMX8QXP_UART2_TX                      2
#define IMX8QXP_UART2_TX_LSIO_GPIO1_IO23                           IMX8QXP_UART2_TX                      4
#define IMX8QXP_UART2_RX_ADMA_UART2_RX                             IMX8QXP_UART2_RX                      0
#define IMX8QXP_UART2_RX_ADMA_FTM_CH0                              IMX8QXP_UART2_RX                      1
#define IMX8QXP_UART2_RX_ADMA_FLEXCAN1_RX                          IMX8QXP_UART2_RX                      2
#define IMX8QXP_UART2_RX_LSIO_GPIO1_IO24                           IMX8QXP_UART2_RX                      4
#define IMX8QXP_MIPI_DSI0_I2C0_SCL_MIPI_DSI0_I2C0_SCL              IMX8QXP_MIPI_DSI0_I2C0_SCL            0
#define IMX8QXP_MIPI_DSI0_I2C0_SCL_MIPI_DSI1_GPIO0_IO02            IMX8QXP_MIPI_DSI0_I2C0_SCL            1
#define IMX8QXP_MIPI_DSI0_I2C0_SCL_LSIO_GPIO1_IO25                 IMX8QXP_MIPI_DSI0_I2C0_SCL            4
#define IMX8QXP_MIPI_DSI0_I2C0_SDA_MIPI_DSI0_I2C0_SDA              IMX8QXP_MIPI_DSI0_I2C0_SDA            0
#define IMX8QXP_MIPI_DSI0_I2C0_SDA_MIPI_DSI1_GPIO0_IO03            IMX8QXP_MIPI_DSI0_I2C0_SDA            1
#define IMX8QXP_MIPI_DSI0_I2C0_SDA_LSIO_GPIO1_IO26                 IMX8QXP_MIPI_DSI0_I2C0_SDA            4
#define IMX8QXP_MIPI_DSI0_GPIO0_00_MIPI_DSI0_GPIO0_IO00            IMX8QXP_MIPI_DSI0_GPIO0_00            0
#define IMX8QXP_MIPI_DSI0_GPIO0_00_ADMA_I2C1_SCL                   IMX8QXP_MIPI_DSI0_GPIO0_00            1
#define IMX8QXP_MIPI_DSI0_GPIO0_00_MIPI_DSI0_PWM0_OUT              IMX8QXP_MIPI_DSI0_GPIO0_00            2
#define IMX8QXP_MIPI_DSI0_GPIO0_00_LSIO_GPIO1_IO27                 IMX8QXP_MIPI_DSI0_GPIO0_00            4
#define IMX8QXP_MIPI_DSI0_GPIO0_01_MIPI_DSI0_GPIO0_IO01            IMX8QXP_MIPI_DSI0_GPIO0_01            0
#define IMX8QXP_MIPI_DSI0_GPIO0_01_ADMA_I2C1_SDA                   IMX8QXP_MIPI_DSI0_GPIO0_01            1
#define IMX8QXP_MIPI_DSI0_GPIO0_01_LSIO_GPIO1_IO28                 IMX8QXP_MIPI_DSI0_GPIO0_01            4
#define IMX8QXP_MIPI_DSI1_I2C0_SCL_MIPI_DSI1_I2C0_SCL              IMX8QXP_MIPI_DSI1_I2C0_SCL            0
#define IMX8QXP_MIPI_DSI1_I2C0_SCL_MIPI_DSI0_GPIO0_IO02            IMX8QXP_MIPI_DSI1_I2C0_SCL            1
#define IMX8QXP_MIPI_DSI1_I2C0_SCL_LSIO_GPIO1_IO29                 IMX8QXP_MIPI_DSI1_I2C0_SCL            4
#define IMX8QXP_MIPI_DSI1_I2C0_SDA_MIPI_DSI1_I2C0_SDA              IMX8QXP_MIPI_DSI1_I2C0_SDA            0
#define IMX8QXP_MIPI_DSI1_I2C0_SDA_MIPI_DSI0_GPIO0_IO03            IMX8QXP_MIPI_DSI1_I2C0_SDA            1
#define IMX8QXP_MIPI_DSI1_I2C0_SDA_LSIO_GPIO1_IO30                 IMX8QXP_MIPI_DSI1_I2C0_SDA            4
#define IMX8QXP_MIPI_DSI1_GPIO0_00_MIPI_DSI1_GPIO0_IO00            IMX8QXP_MIPI_DSI1_GPIO0_00            0
#define IMX8QXP_MIPI_DSI1_GPIO0_00_ADMA_I2C2_SCL                   IMX8QXP_MIPI_DSI1_GPIO0_00            1
#define IMX8QXP_MIPI_DSI1_GPIO0_00_MIPI_DSI1_PWM0_OUT              IMX8QXP_MIPI_DSI1_GPIO0_00            2
#define IMX8QXP_MIPI_DSI1_GPIO0_00_LSIO_GPIO1_IO31                 IMX8QXP_MIPI_DSI1_GPIO0_00            4
#define IMX8QXP_MIPI_DSI1_GPIO0_01_MIPI_DSI1_GPIO0_IO01            IMX8QXP_MIPI_DSI1_GPIO0_01            0
#define IMX8QXP_MIPI_DSI1_GPIO0_01_ADMA_I2C2_SDA                   IMX8QXP_MIPI_DSI1_GPIO0_01            1
#define IMX8QXP_MIPI_DSI1_GPIO0_01_LSIO_GPIO2_IO00                 IMX8QXP_MIPI_DSI1_GPIO0_01            4
#define IMX8QXP_JTAG_TRST_B_SCU_JTAG_TRST_B                        IMX8QXP_JTAG_TRST_B                   0
#define IMX8QXP_JTAG_TRST_B_SCU_WDOG0_WDOG_OUT                     IMX8QXP_JTAG_TRST_B                   1
#define IMX8QXP_PMIC_I2C_SCL_SCU_PMIC_I2C_SCL                      IMX8QXP_PMIC_I2C_SCL                  0
#define IMX8QXP_PMIC_I2C_SCL_SCU_GPIO0_IOXX_PMIC_A35_ON            IMX8QXP_PMIC_I2C_SCL                  1
#define IMX8QXP_PMIC_I2C_SCL_LSIO_GPIO2_IO01                       IMX8QXP_PMIC_I2C_SCL                  4
#define IMX8QXP_PMIC_I2C_SDA_SCU_PMIC_I2C_SDA                      IMX8QXP_PMIC_I2C_SDA                  0
#define IMX8QXP_PMIC_I2C_SDA_SCU_GPIO0_IOXX_PMIC_GPU_ON            IMX8QXP_PMIC_I2C_SDA                  1
#define IMX8QXP_PMIC_I2C_SDA_LSIO_GPIO2_IO02                       IMX8QXP_PMIC_I2C_SDA                  4
#define IMX8QXP_PMIC_INT_B_SCU_DIMX8QXPMIC_INT_B                      IMX8QXP_PMIC_INT_B                    0
#define IMX8QXP_SCU_GPIO0_00_SCU_GPIO0_IO00                        IMX8QXP_SCU_GPIO0_00                  0
#define IMX8QXP_SCU_GPIO0_00_SCU_UART0_RX                          IMX8QXP_SCU_GPIO0_00                  1
#define IMX8QXP_SCU_GPIO0_00_M40_UART0_RX                          IMX8QXP_SCU_GPIO0_00                  2
#define IMX8QXP_SCU_GPIO0_00_ADMA_UART3_RX                         IMX8QXP_SCU_GPIO0_00                  3
#define IMX8QXP_SCU_GPIO0_00_LSIO_GPIO2_IO03                       IMX8QXP_SCU_GPIO0_00                  4
#define IMX8QXP_SCU_GPIO0_01_SCU_GPIO0_IO01                        IMX8QXP_SCU_GPIO0_01                  0
#define IMX8QXP_SCU_GPIO0_01_SCU_UART0_TX                          IMX8QXP_SCU_GPIO0_01                  1
#define IMX8QXP_SCU_GPIO0_01_M40_UART0_TX                          IMX8QXP_SCU_GPIO0_01                  2
#define IMX8QXP_SCU_GPIO0_01_ADMA_UART3_TX                         IMX8QXP_SCU_GPIO0_01                  3
#define IMX8QXP_SCU_GPIO0_01_SCU_WDOG0_WDOG_OUT                    IMX8QXP_SCU_GPIO0_01                  4
#define IMX8QXP_SCU_PMIC_STANDBY_SCU_DIMX8QXPMIC_STANDBY              IMX8QXP_SCU_PMIC_STANDBY              0
#define IMX8QXP_SCU_BOOT_MODE0_SCU_DSC_BOOT_MODE0                  IMX8QXP_SCU_BOOT_MODE0                0
#define IMX8QXP_SCU_BOOT_MODE1_SCU_DSC_BOOT_MODE1                  IMX8QXP_SCU_BOOT_MODE1                0
#define IMX8QXP_SCU_BOOT_MODE2_SCU_DSC_BOOT_MODE2                  IMX8QXP_SCU_BOOT_MODE2                0
#define IMX8QXP_SCU_BOOT_MODE2_SCU_PMIC_I2C_SDA                    IMX8QXP_SCU_BOOT_MODE2                1
#define IMX8QXP_SCU_BOOT_MODE3_SCU_DSC_BOOT_MODE3                  IMX8QXP_SCU_BOOT_MODE3                0
#define IMX8QXP_SCU_BOOT_MODE3_SCU_PMIC_I2C_SCL                    IMX8QXP_SCU_BOOT_MODE3                1
#define IMX8QXP_SCU_BOOT_MODE3_SCU_DSC_RTC_CLOCK_OUTPUT_32K        IMX8QXP_SCU_BOOT_MODE3                3
#define IMX8QXP_CSI_D00_CI_PI_D02                                  IMX8QXP_CSI_D00                       0
#define IMX8QXP_CSI_D00_ADMA_SAI0_RXC                              IMX8QXP_CSI_D00                       2
#define IMX8QXP_CSI_D01_CI_PI_D03                                  IMX8QXP_CSI_D01                       0
#define IMX8QXP_CSI_D01_ADMA_SAI0_RXD                              IMX8QXP_CSI_D01                       2
#define IMX8QXP_CSI_D02_CI_PI_D04                                  IMX8QXP_CSI_D02                       0
#define IMX8QXP_CSI_D02_ADMA_SAI0_RXFS                             IMX8QXP_CSI_D02                       2
#define IMX8QXP_CSI_D03_CI_PI_D05                                  IMX8QXP_CSI_D03                       0
#define IMX8QXP_CSI_D03_ADMA_SAI2_RXC                              IMX8QXP_CSI_D03                       2
#define IMX8QXP_CSI_D04_CI_PI_D06                                  IMX8QXP_CSI_D04                       0
#define IMX8QXP_CSI_D04_ADMA_SAI2_RXD                              IMX8QXP_CSI_D04                       2
#define IMX8QXP_CSI_D05_CI_PI_D07                                  IMX8QXP_CSI_D05                       0
#define IMX8QXP_CSI_D05_ADMA_SAI2_RXFS                             IMX8QXP_CSI_D05                       2
#define IMX8QXP_CSI_D06_CI_PI_D08                                  IMX8QXP_CSI_D06                       0
#define IMX8QXP_CSI_D06_ADMA_SAI3_RXC                              IMX8QXP_CSI_D06                       2
#define IMX8QXP_CSI_D07_CI_PI_D09                                  IMX8QXP_CSI_D07                       0
#define IMX8QXP_CSI_D07_ADMA_SAI3_RXD                              IMX8QXP_CSI_D07                       2
#define IMX8QXP_CSI_HSYNC_CI_PI_HSYNC                              IMX8QXP_CSI_HSYNC                     0
#define IMX8QXP_CSI_HSYNC_CI_PI_D00                                IMX8QXP_CSI_HSYNC                     1
#define IMX8QXP_CSI_HSYNC_ADMA_SAI3_RXFS                           IMX8QXP_CSI_HSYNC                     2
#define IMX8QXP_CSI_VSYNC_CI_PI_VSYNC                              IMX8QXP_CSI_VSYNC                     0
#define IMX8QXP_CSI_VSYNC_CI_PI_D01                                IMX8QXP_CSI_VSYNC                     1
#define IMX8QXP_CSI_PCLK_CI_PI_PCLK                                IMX8QXP_CSI_PCLK                      0
#define IMX8QXP_CSI_PCLK_MIPI_CSI0_I2C0_SCL                        IMX8QXP_CSI_PCLK                      1
#define IMX8QXP_CSI_PCLK_ADMA_SPI1_SCK                             IMX8QXP_CSI_PCLK                      3
#define IMX8QXP_CSI_PCLK_LSIO_GPIO3_IO00                           IMX8QXP_CSI_PCLK                      4
#define IMX8QXP_CSI_MCLK_CI_PI_MCLK                                IMX8QXP_CSI_MCLK                      0
#define IMX8QXP_CSI_MCLK_MIPI_CSI0_I2C0_SDA                        IMX8QXP_CSI_MCLK                      1
#define IMX8QXP_CSI_MCLK_ADMA_SPI1_SDO                             IMX8QXP_CSI_MCLK                      3
#define IMX8QXP_CSI_MCLK_LSIO_GPIO3_IO01                           IMX8QXP_CSI_MCLK                      4
#define IMX8QXP_CSI_EN_CI_PI_EN                                    IMX8QXP_CSI_EN                        0
#define IMX8QXP_CSI_EN_CI_PI_I2C_SCL                               IMX8QXP_CSI_EN                        1
#define IMX8QXP_CSI_EN_ADMA_I2C3_SCL                               IMX8QXP_CSI_EN                        2
#define IMX8QXP_CSI_EN_ADMA_SPI1_SDI                               IMX8QXP_CSI_EN                        3
#define IMX8QXP_CSI_EN_LSIO_GPIO3_IO02                             IMX8QXP_CSI_EN                        4
#define IMX8QXP_CSI_RESET_CI_PI_RESET                              IMX8QXP_CSI_RESET                     0
#define IMX8QXP_CSI_RESET_CI_PI_I2C_SDA                            IMX8QXP_CSI_RESET                     1
#define IMX8QXP_CSI_RESET_ADMA_I2C3_SDA                            IMX8QXP_CSI_RESET                     2
#define IMX8QXP_CSI_RESET_ADMA_SPI1_CS0                            IMX8QXP_CSI_RESET                     3
#define IMX8QXP_CSI_RESET_LSIO_GPIO3_IO03                          IMX8QXP_CSI_RESET                     4
#define IMX8QXP_MIPI_CSI0_MCLK_OUT_MIPI_CSI0_ACM_MCLK_OUT          IMX8QXP_MIPI_CSI0_MCLK_OUT            0
#define IMX8QXP_MIPI_CSI0_MCLK_OUT_LSIO_GPIO3_IO04                 IMX8QXP_MIPI_CSI0_MCLK_OUT            4
#define IMX8QXP_MIPI_CSI0_I2C0_SCL_MIPI_CSI0_I2C0_SCL              IMX8QXP_MIPI_CSI0_I2C0_SCL            0
#define IMX8QXP_MIPI_CSI0_I2C0_SCL_MIPI_CSI0_GPIO0_IO02            IMX8QXP_MIPI_CSI0_I2C0_SCL            1
#define IMX8QXP_MIPI_CSI0_I2C0_SCL_LSIO_GPIO3_IO05                 IMX8QXP_MIPI_CSI0_I2C0_SCL            4
#define IMX8QXP_MIPI_CSI0_I2C0_SDA_MIPI_CSI0_I2C0_SDA              IMX8QXP_MIPI_CSI0_I2C0_SDA            0
#define IMX8QXP_MIPI_CSI0_I2C0_SDA_MIPI_CSI0_GPIO0_IO03            IMX8QXP_MIPI_CSI0_I2C0_SDA            1
#define IMX8QXP_MIPI_CSI0_I2C0_SDA_LSIO_GPIO3_IO06                 IMX8QXP_MIPI_CSI0_I2C0_SDA            4
#define IMX8QXP_MIPI_CSI0_GPIO0_01_MIPI_CSI0_GPIO0_IO01            IMX8QXP_MIPI_CSI0_GPIO0_01            0
#define IMX8QXP_MIPI_CSI0_GPIO0_01_ADMA_I2C0_SDA                   IMX8QXP_MIPI_CSI0_GPIO0_01            1
#define IMX8QXP_MIPI_CSI0_GPIO0_01_LSIO_GPIO3_IO07                 IMX8QXP_MIPI_CSI0_GPIO0_01            4
#define IMX8QXP_MIPI_CSI0_GPIO0_00_MIPI_CSI0_GPIO0_IO00            IMX8QXP_MIPI_CSI0_GPIO0_00            0
#define IMX8QXP_MIPI_CSI0_GPIO0_00_ADMA_I2C0_SCL                   IMX8QXP_MIPI_CSI0_GPIO0_00            1
#define IMX8QXP_MIPI_CSI0_GPIO0_00_LSIO_GPIO3_IO08                 IMX8QXP_MIPI_CSI0_GPIO0_00            4
#define IMX8QXP_QSPI0A_DATA0_LSIO_QSPI0A_DATA0                     IMX8QXP_QSPI0A_DATA0                  0
#define IMX8QXP_QSPI0A_DATA0_LSIO_GPIO3_IO09                       IMX8QXP_QSPI0A_DATA0                  4
#define IMX8QXP_QSPI0A_DATA1_LSIO_QSPI0A_DATA1                     IMX8QXP_QSPI0A_DATA1                  0
#define IMX8QXP_QSPI0A_DATA1_LSIO_GPIO3_IO10                       IMX8QXP_QSPI0A_DATA1                  4
#define IMX8QXP_QSPI0A_DATA2_LSIO_QSPI0A_DATA2                     IMX8QXP_QSPI0A_DATA2                  0
#define IMX8QXP_QSPI0A_DATA2_LSIO_GPIO3_IO11                       IMX8QXP_QSPI0A_DATA2                  4
#define IMX8QXP_QSPI0A_DATA3_LSIO_QSPI0A_DATA3                     IMX8QXP_QSPI0A_DATA3                  0
#define IMX8QXP_QSPI0A_DATA3_LSIO_GPIO3_IO12                       IMX8QXP_QSPI0A_DATA3                  4
#define IMX8QXP_QSPI0A_DQS_LSIO_QSPI0A_DQS                         IMX8QXP_QSPI0A_DQS                    0
#define IMX8QXP_QSPI0A_DQS_LSIO_GPIO3_IO13                         IMX8QXP_QSPI0A_DQS                    4
#define IMX8QXP_QSPI0A_SS0_B_LSIO_QSPI0A_SS0_B                     IMX8QXP_QSPI0A_SS0_B                  0
#define IMX8QXP_QSPI0A_SS0_B_LSIO_GPIO3_IO14                       IMX8QXP_QSPI0A_SS0_B                  4
#define IMX8QXP_QSPI0A_SS1_B_LSIO_QSPI0A_SS1_B                     IMX8QXP_QSPI0A_SS1_B                  0
#define IMX8QXP_QSPI0A_SS1_B_LSIO_GPIO3_IO15                       IMX8QXP_QSPI0A_SS1_B                  4
#define IMX8QXP_QSPI0A_SCLK_LSIO_QSPI0A_SCLK                       IMX8QXP_QSPI0A_SCLK                   0
#define IMX8QXP_QSPI0A_SCLK_LSIO_GPIO3_IO16                        IMX8QXP_QSPI0A_SCLK                   4
#define IMX8QXP_QSPI0B_SCLK_LSIO_QSPI0B_SCLK                       IMX8QXP_QSPI0B_SCLK                   0
#define IMX8QXP_QSPI0B_SCLK_LSIO_QSPI1A_SCLK                       IMX8QXP_QSPI0B_SCLK                   1
#define IMX8QXP_QSPI0B_SCLK_LSIO_KPP0_COL0                         IMX8QXP_QSPI0B_SCLK                   2
#define IMX8QXP_QSPI0B_SCLK_LSIO_GPIO3_IO17                        IMX8QXP_QSPI0B_SCLK                   4
#define IMX8QXP_QSPI0B_DATA0_LSIO_QSPI0B_DATA0                     IMX8QXP_QSPI0B_DATA0                  0
#define IMX8QXP_QSPI0B_DATA0_LSIO_QSPI1A_DATA0                     IMX8QXP_QSPI0B_DATA0                  1
#define IMX8QXP_QSPI0B_DATA0_LSIO_KPP0_COL1                        IMX8QXP_QSPI0B_DATA0                  2
#define IMX8QXP_QSPI0B_DATA0_LSIO_GPIO3_IO18                       IMX8QXP_QSPI0B_DATA0                  4
#define IMX8QXP_QSPI0B_DATA1_LSIO_QSPI0B_DATA1                     IMX8QXP_QSPI0B_DATA1                  0
#define IMX8QXP_QSPI0B_DATA1_LSIO_QSPI1A_DATA1                     IMX8QXP_QSPI0B_DATA1                  1
#define IMX8QXP_QSPI0B_DATA1_LSIO_KPP0_COL2                        IMX8QXP_QSPI0B_DATA1                  2
#define IMX8QXP_QSPI0B_DATA1_LSIO_GPIO3_IO19                       IMX8QXP_QSPI0B_DATA1                  4
#define IMX8QXP_QSPI0B_DATA2_LSIO_QSPI0B_DATA2                     IMX8QXP_QSPI0B_DATA2                  0
#define IMX8QXP_QSPI0B_DATA2_LSIO_QSPI1A_DATA2                     IMX8QXP_QSPI0B_DATA2                  1
#define IMX8QXP_QSPI0B_DATA2_LSIO_KPP0_COL3                        IMX8QXP_QSPI0B_DATA2                  2
#define IMX8QXP_QSPI0B_DATA2_LSIO_GPIO3_IO20                       IMX8QXP_QSPI0B_DATA2                  4
#define IMX8QXP_QSPI0B_DATA3_LSIO_QSPI0B_DATA3                     IMX8QXP_QSPI0B_DATA3                  0
#define IMX8QXP_QSPI0B_DATA3_LSIO_QSPI1A_DATA3                     IMX8QXP_QSPI0B_DATA3                  1
#define IMX8QXP_QSPI0B_DATA3_LSIO_KPP0_ROW0                        IMX8QXP_QSPI0B_DATA3                  2
#define IMX8QXP_QSPI0B_DATA3_LSIO_GPIO3_IO21                       IMX8QXP_QSPI0B_DATA3                  4
#define IMX8QXP_QSPI0B_DQS_LSIO_QSPI0B_DQS                         IMX8QXP_QSPI0B_DQS                    0
#define IMX8QXP_QSPI0B_DQS_LSIO_QSPI1A_DQS                         IMX8QXP_QSPI0B_DQS                    1
#define IMX8QXP_QSPI0B_DQS_LSIO_KPP0_ROW1                          IMX8QXP_QSPI0B_DQS                    2
#define IMX8QXP_QSPI0B_DQS_LSIO_GPIO3_IO22                         IMX8QXP_QSPI0B_DQS                    4
#define IMX8QXP_QSPI0B_SS0_B_LSIO_QSPI0B_SS0_B                     IMX8QXP_QSPI0B_SS0_B                  0
#define IMX8QXP_QSPI0B_SS0_B_LSIO_QSPI1A_SS0_B                     IMX8QXP_QSPI0B_SS0_B                  1
#define IMX8QXP_QSPI0B_SS0_B_LSIO_KPP0_ROW2                        IMX8QXP_QSPI0B_SS0_B                  2
#define IMX8QXP_QSPI0B_SS0_B_LSIO_GPIO3_IO23                       IMX8QXP_QSPI0B_SS0_B                  4
#define IMX8QXP_QSPI0B_SS1_B_LSIO_QSPI0B_SS1_B                     IMX8QXP_QSPI0B_SS1_B                  0
#define IMX8QXP_QSPI0B_SS1_B_LSIO_QSPI1A_SS1_B                     IMX8QXP_QSPI0B_SS1_B                  1
#define IMX8QXP_QSPI0B_SS1_B_LSIO_KPP0_ROW3                        IMX8QXP_QSPI0B_SS1_B                  2
#define IMX8QXP_QSPI0B_SS1_B_LSIO_GPIO3_IO24                       IMX8QXP_QSPI0B_SS1_B                  4

#endif /* _IMX8QXP_PADS_H */
