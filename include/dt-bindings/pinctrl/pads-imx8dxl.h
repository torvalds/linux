/* SPDX-License-Identifier: GPL-2.0+ */
/*
 * Copyright 2019~2020 NXP
 */

#ifndef _IMX8DXL_PADS_H
#define _IMX8DXL_PADS_H

/* pin id */
#define IMX8DXL_PCIE_CTRL0_PERST_B                  0
#define IMX8DXL_PCIE_CTRL0_CLKREQ_B                 1
#define IMX8DXL_PCIE_CTRL0_WAKE_B                   2
#define IMX8DXL_COMP_CTL_GPIO_1V8_3V3_PCIESEP       3
#define IMX8DXL_USB_SS3_TC0                         4
#define IMX8DXL_USB_SS3_TC1                         5
#define IMX8DXL_USB_SS3_TC2                         6
#define IMX8DXL_USB_SS3_TC3                         7
#define IMX8DXL_COMP_CTL_GPIO_3V3_USB3IO            8
#define IMX8DXL_EMMC0_CLK                           9
#define IMX8DXL_EMMC0_CMD                           10
#define IMX8DXL_EMMC0_DATA0                         11
#define IMX8DXL_EMMC0_DATA1                         12
#define IMX8DXL_EMMC0_DATA2                         13
#define IMX8DXL_EMMC0_DATA3                         14
#define IMX8DXL_EMMC0_DATA4                         15
#define IMX8DXL_EMMC0_DATA5                         16
#define IMX8DXL_EMMC0_DATA6                         17
#define IMX8DXL_EMMC0_DATA7                         18
#define IMX8DXL_EMMC0_STROBE                        19
#define IMX8DXL_EMMC0_RESET_B                       20
#define IMX8DXL_COMP_CTL_GPIO_1V8_3V3_SD1FIX0       21
#define IMX8DXL_USDHC1_RESET_B                      22
#define IMX8DXL_USDHC1_VSELECT                      23
#define IMX8DXL_CTL_NAND_RE_P_N                     24
#define IMX8DXL_USDHC1_WP                           25
#define IMX8DXL_USDHC1_CD_B                         26
#define IMX8DXL_CTL_NAND_DQS_P_N                    27
#define IMX8DXL_COMP_CTL_GPIO_1V8_3V3_VSELSEP       28
#define IMX8DXL_ENET0_RGMII_TXC                     29
#define IMX8DXL_ENET0_RGMII_TX_CTL                  30
#define IMX8DXL_ENET0_RGMII_TXD0                    31
#define IMX8DXL_ENET0_RGMII_TXD1                    32
#define IMX8DXL_ENET0_RGMII_TXD2                    33
#define IMX8DXL_ENET0_RGMII_TXD3                    34
#define IMX8DXL_COMP_CTL_GPIO_1V8_3V3_ENET_ENETB0   35
#define IMX8DXL_ENET0_RGMII_RXC                     36
#define IMX8DXL_ENET0_RGMII_RX_CTL                  37
#define IMX8DXL_ENET0_RGMII_RXD0                    38
#define IMX8DXL_ENET0_RGMII_RXD1                    39
#define IMX8DXL_ENET0_RGMII_RXD2                    40
#define IMX8DXL_ENET0_RGMII_RXD3                    41
#define IMX8DXL_COMP_CTL_GPIO_1V8_3V3_ENET_ENETB1   42
#define IMX8DXL_ENET0_REFCLK_125M_25M               43
#define IMX8DXL_ENET0_MDIO                          44
#define IMX8DXL_ENET0_MDC                           45
#define IMX8DXL_COMP_CTL_GPIO_1V8_3V3_GPIOCT        46
#define IMX8DXL_ENET1_RGMII_TXC                     47
#define IMX8DXL_ENET1_RGMII_TXD2                    48
#define IMX8DXL_ENET1_RGMII_TX_CTL                  49
#define IMX8DXL_ENET1_RGMII_TXD3                    50
#define IMX8DXL_ENET1_RGMII_RXC                     51
#define IMX8DXL_ENET1_RGMII_RXD3                    52
#define IMX8DXL_ENET1_RGMII_RXD2                    53
#define IMX8DXL_ENET1_RGMII_RXD1                    54
#define IMX8DXL_ENET1_RGMII_TXD0                    55
#define IMX8DXL_ENET1_RGMII_TXD1                    56
#define IMX8DXL_ENET1_RGMII_RXD0                    57
#define IMX8DXL_ENET1_RGMII_RX_CTL                  58
#define IMX8DXL_ENET1_REFCLK_125M_25M               59
#define IMX8DXL_COMP_CTL_GPIO_1V8_3V3_GPIORHB       60
#define IMX8DXL_SPI3_SCK                            61
#define IMX8DXL_SPI3_SDO                            62
#define IMX8DXL_SPI3_SDI                            63
#define IMX8DXL_SPI3_CS0                            64
#define IMX8DXL_SPI3_CS1                            65
#define IMX8DXL_MCLK_IN1                            66
#define IMX8DXL_MCLK_IN0                            67
#define IMX8DXL_MCLK_OUT0                           68
#define IMX8DXL_UART1_TX                            69
#define IMX8DXL_UART1_RX                            70
#define IMX8DXL_UART1_RTS_B                         71
#define IMX8DXL_UART1_CTS_B                         72
#define IMX8DXL_COMP_CTL_GPIO_1V8_3V3_GPIORHK       73
#define IMX8DXL_SPI0_SCK                            74
#define IMX8DXL_SPI0_SDI                            75
#define IMX8DXL_SPI0_SDO                            76
#define IMX8DXL_SPI0_CS1                            77
#define IMX8DXL_SPI0_CS0                            78
#define IMX8DXL_COMP_CTL_GPIO_1V8_3V3_GPIORHT       79
#define IMX8DXL_ADC_IN1                             80
#define IMX8DXL_ADC_IN0                             81
#define IMX8DXL_ADC_IN3                             82
#define IMX8DXL_ADC_IN2                             83
#define IMX8DXL_ADC_IN5                             84
#define IMX8DXL_ADC_IN4                             85
#define IMX8DXL_FLEXCAN0_RX                         86
#define IMX8DXL_FLEXCAN0_TX                         87
#define IMX8DXL_FLEXCAN1_RX                         88
#define IMX8DXL_FLEXCAN1_TX                         89
#define IMX8DXL_FLEXCAN2_RX                         90
#define IMX8DXL_FLEXCAN2_TX                         91
#define IMX8DXL_UART0_RX                            92
#define IMX8DXL_UART0_TX                            93
#define IMX8DXL_UART2_TX                            94
#define IMX8DXL_UART2_RX                            95
#define IMX8DXL_COMP_CTL_GPIO_1V8_3V3_GPIOLH        96
#define IMX8DXL_JTAG_TRST_B                         97
#define IMX8DXL_PMIC_I2C_SCL                        98
#define IMX8DXL_PMIC_I2C_SDA                        99
#define IMX8DXL_PMIC_INT_B                          100
#define IMX8DXL_SCU_GPIO0_00                        101
#define IMX8DXL_SCU_GPIO0_01                        102
#define IMX8DXL_SCU_PMIC_STANDBY                    103
#define IMX8DXL_SCU_BOOT_MODE1                      104
#define IMX8DXL_SCU_BOOT_MODE0                      105
#define IMX8DXL_SCU_BOOT_MODE2                      106
#define IMX8DXL_SNVS_TAMPER_OUT1                    107
#define IMX8DXL_SNVS_TAMPER_OUT2                    108
#define IMX8DXL_SNVS_TAMPER_OUT3                    109
#define IMX8DXL_SNVS_TAMPER_OUT4                    110
#define IMX8DXL_SNVS_TAMPER_IN0                     111
#define IMX8DXL_SNVS_TAMPER_IN1                     112
#define IMX8DXL_SNVS_TAMPER_IN2                     113
#define IMX8DXL_SNVS_TAMPER_IN3                     114
#define IMX8DXL_SPI1_SCK                            115
#define IMX8DXL_SPI1_SDO                            116
#define IMX8DXL_SPI1_SDI                            117
#define IMX8DXL_SPI1_CS0                            118
#define IMX8DXL_COMP_CTL_GPIO_1V8_3V3_GPIORHD       119
#define IMX8DXL_QSPI0A_DATA1                        120
#define IMX8DXL_QSPI0A_DATA0                        121
#define IMX8DXL_QSPI0A_DATA3                        122
#define IMX8DXL_QSPI0A_DATA2                        123
#define IMX8DXL_QSPI0A_SS0_B                        124
#define IMX8DXL_QSPI0A_DQS                          125
#define IMX8DXL_QSPI0A_SCLK                         126
#define IMX8DXL_COMP_CTL_GPIO_1V8_3V3_QSPI0A        127
#define IMX8DXL_QSPI0B_SCLK                         128
#define IMX8DXL_QSPI0B_DQS                          129
#define IMX8DXL_QSPI0B_DATA1                        130
#define IMX8DXL_QSPI0B_DATA0                        131
#define IMX8DXL_QSPI0B_DATA3                        132
#define IMX8DXL_QSPI0B_DATA2                        133
#define IMX8DXL_QSPI0B_SS0_B                        134
#define IMX8DXL_COMP_CTL_GPIO_1V8_3V3_QSPI0B        135

/* format: <pin_id mux_mode> */
#define IMX8DXL_PCIE_CTRL0_PERST_B_HSIO_PCIE0_PERST_B              IMX8DXL_PCIE_CTRL0_PERST_B            0
#define IMX8DXL_PCIE_CTRL0_PERST_B_LSIO_GPIO4_IO00                 IMX8DXL_PCIE_CTRL0_PERST_B            4
#define IMX8DXL_PCIE_CTRL0_PERST_B_LSIO_GPIO7_IO00                 IMX8DXL_PCIE_CTRL0_PERST_B            5
#define IMX8DXL_PCIE_CTRL0_CLKREQ_B_HSIO_PCIE0_CLKREQ_B            IMX8DXL_PCIE_CTRL0_CLKREQ_B           0
#define IMX8DXL_PCIE_CTRL0_CLKREQ_B_LSIO_GPIO4_IO01                IMX8DXL_PCIE_CTRL0_CLKREQ_B           4
#define IMX8DXL_PCIE_CTRL0_CLKREQ_B_LSIO_GPIO7_IO01                IMX8DXL_PCIE_CTRL0_CLKREQ_B           5
#define IMX8DXL_PCIE_CTRL0_WAKE_B_HSIO_PCIE0_WAKE_B                IMX8DXL_PCIE_CTRL0_WAKE_B             0
#define IMX8DXL_PCIE_CTRL0_WAKE_B_LSIO_GPIO4_IO02                  IMX8DXL_PCIE_CTRL0_WAKE_B             4
#define IMX8DXL_PCIE_CTRL0_WAKE_B_LSIO_GPIO7_IO02                  IMX8DXL_PCIE_CTRL0_WAKE_B             5
#define IMX8DXL_USB_SS3_TC0_ADMA_I2C1_SCL                          IMX8DXL_USB_SS3_TC0                   0
#define IMX8DXL_USB_SS3_TC0_CONN_USB_OTG1_PWR                      IMX8DXL_USB_SS3_TC0                   1
#define IMX8DXL_USB_SS3_TC0_CONN_USB_OTG2_PWR                      IMX8DXL_USB_SS3_TC0                   2
#define IMX8DXL_USB_SS3_TC0_LSIO_GPIO4_IO03                        IMX8DXL_USB_SS3_TC0                   4
#define IMX8DXL_USB_SS3_TC0_LSIO_GPIO7_IO03                        IMX8DXL_USB_SS3_TC0                   5
#define IMX8DXL_USB_SS3_TC1_ADMA_I2C1_SCL                          IMX8DXL_USB_SS3_TC1                   0
#define IMX8DXL_USB_SS3_TC1_CONN_USB_OTG2_PWR                      IMX8DXL_USB_SS3_TC1                   1
#define IMX8DXL_USB_SS3_TC1_LSIO_GPIO4_IO04                        IMX8DXL_USB_SS3_TC1                   4
#define IMX8DXL_USB_SS3_TC1_LSIO_GPIO7_IO04                        IMX8DXL_USB_SS3_TC1                   5
#define IMX8DXL_USB_SS3_TC2_ADMA_I2C1_SDA                          IMX8DXL_USB_SS3_TC2                   0
#define IMX8DXL_USB_SS3_TC2_CONN_USB_OTG1_OC                       IMX8DXL_USB_SS3_TC2                   1
#define IMX8DXL_USB_SS3_TC2_CONN_USB_OTG2_OC                       IMX8DXL_USB_SS3_TC2                   2
#define IMX8DXL_USB_SS3_TC2_LSIO_GPIO4_IO05                        IMX8DXL_USB_SS3_TC2                   4
#define IMX8DXL_USB_SS3_TC2_LSIO_GPIO7_IO05                        IMX8DXL_USB_SS3_TC2                   5
#define IMX8DXL_USB_SS3_TC3_ADMA_I2C1_SDA                          IMX8DXL_USB_SS3_TC3                   0
#define IMX8DXL_USB_SS3_TC3_CONN_USB_OTG2_OC                       IMX8DXL_USB_SS3_TC3                   1
#define IMX8DXL_USB_SS3_TC3_LSIO_GPIO4_IO06                        IMX8DXL_USB_SS3_TC3                   4
#define IMX8DXL_USB_SS3_TC3_LSIO_GPIO7_IO06                        IMX8DXL_USB_SS3_TC3                   5
#define IMX8DXL_EMMC0_CLK_CONN_EMMC0_CLK                           IMX8DXL_EMMC0_CLK                     0
#define IMX8DXL_EMMC0_CLK_CONN_NAND_READY_B                        IMX8DXL_EMMC0_CLK                     1
#define IMX8DXL_EMMC0_CLK_LSIO_GPIO4_IO07                          IMX8DXL_EMMC0_CLK                     4
#define IMX8DXL_EMMC0_CMD_CONN_EMMC0_CMD                           IMX8DXL_EMMC0_CMD                     0
#define IMX8DXL_EMMC0_CMD_CONN_NAND_DQS                            IMX8DXL_EMMC0_CMD                     1
#define IMX8DXL_EMMC0_CMD_LSIO_GPIO4_IO08                          IMX8DXL_EMMC0_CMD                     4
#define IMX8DXL_EMMC0_DATA0_CONN_EMMC0_DATA0                       IMX8DXL_EMMC0_DATA0                   0
#define IMX8DXL_EMMC0_DATA0_CONN_NAND_DATA00                       IMX8DXL_EMMC0_DATA0                   1
#define IMX8DXL_EMMC0_DATA0_LSIO_GPIO4_IO09                        IMX8DXL_EMMC0_DATA0                   4
#define IMX8DXL_EMMC0_DATA1_CONN_EMMC0_DATA1                       IMX8DXL_EMMC0_DATA1                   0
#define IMX8DXL_EMMC0_DATA1_CONN_NAND_DATA01                       IMX8DXL_EMMC0_DATA1                   1
#define IMX8DXL_EMMC0_DATA1_LSIO_GPIO4_IO10                        IMX8DXL_EMMC0_DATA1                   4
#define IMX8DXL_EMMC0_DATA2_CONN_EMMC0_DATA2                       IMX8DXL_EMMC0_DATA2                   0
#define IMX8DXL_EMMC0_DATA2_CONN_NAND_DATA02                       IMX8DXL_EMMC0_DATA2                   1
#define IMX8DXL_EMMC0_DATA2_LSIO_GPIO4_IO11                        IMX8DXL_EMMC0_DATA2                   4
#define IMX8DXL_EMMC0_DATA3_CONN_EMMC0_DATA3                       IMX8DXL_EMMC0_DATA3                   0
#define IMX8DXL_EMMC0_DATA3_CONN_NAND_DATA03                       IMX8DXL_EMMC0_DATA3                   1
#define IMX8DXL_EMMC0_DATA3_LSIO_GPIO4_IO12                        IMX8DXL_EMMC0_DATA3                   4
#define IMX8DXL_EMMC0_DATA4_CONN_EMMC0_DATA4                       IMX8DXL_EMMC0_DATA4                   0
#define IMX8DXL_EMMC0_DATA4_CONN_NAND_DATA04                       IMX8DXL_EMMC0_DATA4                   1
#define IMX8DXL_EMMC0_DATA4_LSIO_GPIO4_IO13                        IMX8DXL_EMMC0_DATA4                   4
#define IMX8DXL_EMMC0_DATA5_CONN_EMMC0_DATA5                       IMX8DXL_EMMC0_DATA5                   0
#define IMX8DXL_EMMC0_DATA5_CONN_NAND_DATA05                       IMX8DXL_EMMC0_DATA5                   1
#define IMX8DXL_EMMC0_DATA5_LSIO_GPIO4_IO14                        IMX8DXL_EMMC0_DATA5                   4
#define IMX8DXL_EMMC0_DATA6_CONN_EMMC0_DATA6                       IMX8DXL_EMMC0_DATA6                   0
#define IMX8DXL_EMMC0_DATA6_CONN_NAND_DATA06                       IMX8DXL_EMMC0_DATA6                   1
#define IMX8DXL_EMMC0_DATA6_LSIO_GPIO4_IO15                        IMX8DXL_EMMC0_DATA6                   4
#define IMX8DXL_EMMC0_DATA7_CONN_EMMC0_DATA7                       IMX8DXL_EMMC0_DATA7                   0
#define IMX8DXL_EMMC0_DATA7_CONN_NAND_DATA07                       IMX8DXL_EMMC0_DATA7                   1
#define IMX8DXL_EMMC0_DATA7_LSIO_GPIO4_IO16                        IMX8DXL_EMMC0_DATA7                   4
#define IMX8DXL_EMMC0_STROBE_CONN_EMMC0_STROBE                     IMX8DXL_EMMC0_STROBE                  0
#define IMX8DXL_EMMC0_STROBE_CONN_NAND_CLE                         IMX8DXL_EMMC0_STROBE                  1
#define IMX8DXL_EMMC0_STROBE_LSIO_GPIO4_IO17                       IMX8DXL_EMMC0_STROBE                  4
#define IMX8DXL_EMMC0_RESET_B_CONN_EMMC0_RESET_B                   IMX8DXL_EMMC0_RESET_B                 0
#define IMX8DXL_EMMC0_RESET_B_CONN_NAND_WP_B                       IMX8DXL_EMMC0_RESET_B                 1
#define IMX8DXL_EMMC0_RESET_B_LSIO_GPIO4_IO18                      IMX8DXL_EMMC0_RESET_B                 4
#define IMX8DXL_USDHC1_RESET_B_CONN_USDHC1_RESET_B                 IMX8DXL_USDHC1_RESET_B                0
#define IMX8DXL_USDHC1_RESET_B_CONN_NAND_RE_N                      IMX8DXL_USDHC1_RESET_B                1
#define IMX8DXL_USDHC1_RESET_B_ADMA_SPI2_SCK                       IMX8DXL_USDHC1_RESET_B                2
#define IMX8DXL_USDHC1_RESET_B_CONN_NAND_WE_B                      IMX8DXL_USDHC1_RESET_B                3
#define IMX8DXL_USDHC1_RESET_B_LSIO_GPIO4_IO19                     IMX8DXL_USDHC1_RESET_B                4
#define IMX8DXL_USDHC1_RESET_B_LSIO_GPIO7_IO08                     IMX8DXL_USDHC1_RESET_B                5
#define IMX8DXL_USDHC1_VSELECT_CONN_USDHC1_VSELECT                 IMX8DXL_USDHC1_VSELECT                0
#define IMX8DXL_USDHC1_VSELECT_CONN_NAND_RE_P                      IMX8DXL_USDHC1_VSELECT                1
#define IMX8DXL_USDHC1_VSELECT_ADMA_SPI2_SDO                       IMX8DXL_USDHC1_VSELECT                2
#define IMX8DXL_USDHC1_VSELECT_CONN_NAND_RE_B                      IMX8DXL_USDHC1_VSELECT                3
#define IMX8DXL_USDHC1_VSELECT_LSIO_GPIO4_IO20                     IMX8DXL_USDHC1_VSELECT                4
#define IMX8DXL_USDHC1_VSELECT_LSIO_GPIO7_IO09                     IMX8DXL_USDHC1_VSELECT                5
#define IMX8DXL_USDHC1_WP_CONN_USDHC1_WP                           IMX8DXL_USDHC1_WP                     0
#define IMX8DXL_USDHC1_WP_CONN_NAND_DQS_N                          IMX8DXL_USDHC1_WP                     1
#define IMX8DXL_USDHC1_WP_ADMA_SPI2_SDI                            IMX8DXL_USDHC1_WP                     2
#define IMX8DXL_USDHC1_WP_CONN_NAND_ALE                            IMX8DXL_USDHC1_WP                     3
#define IMX8DXL_USDHC1_WP_LSIO_GPIO4_IO21                          IMX8DXL_USDHC1_WP                     4
#define IMX8DXL_USDHC1_WP_LSIO_GPIO7_IO10                          IMX8DXL_USDHC1_WP                     5
#define IMX8DXL_USDHC1_CD_B_CONN_USDHC1_CD_B                       IMX8DXL_USDHC1_CD_B                   0
#define IMX8DXL_USDHC1_CD_B_CONN_NAND_DQS_P                        IMX8DXL_USDHC1_CD_B                   1
#define IMX8DXL_USDHC1_CD_B_ADMA_SPI2_CS0                          IMX8DXL_USDHC1_CD_B                   2
#define IMX8DXL_USDHC1_CD_B_CONN_NAND_DQS                          IMX8DXL_USDHC1_CD_B                   3
#define IMX8DXL_USDHC1_CD_B_LSIO_GPIO4_IO22                        IMX8DXL_USDHC1_CD_B                   4
#define IMX8DXL_USDHC1_CD_B_LSIO_GPIO7_IO11                        IMX8DXL_USDHC1_CD_B                   5
#define IMX8DXL_ENET0_RGMII_TXC_CONN_ENET0_RGMII_TXC               IMX8DXL_ENET0_RGMII_TXC               0
#define IMX8DXL_ENET0_RGMII_TXC_CONN_ENET0_RCLK50M_OUT             IMX8DXL_ENET0_RGMII_TXC               1
#define IMX8DXL_ENET0_RGMII_TXC_CONN_ENET0_RCLK50M_IN              IMX8DXL_ENET0_RGMII_TXC               2
#define IMX8DXL_ENET0_RGMII_TXC_CONN_NAND_CE1_B                    IMX8DXL_ENET0_RGMII_TXC               3
#define IMX8DXL_ENET0_RGMII_TXC_LSIO_GPIO4_IO29                    IMX8DXL_ENET0_RGMII_TXC               4
#define IMX8DXL_ENET0_RGMII_TXC_CONN_USDHC2_CLK                    IMX8DXL_ENET0_RGMII_TXC               5
#define IMX8DXL_ENET0_RGMII_TX_CTL_CONN_ENET0_RGMII_TX_CTL         IMX8DXL_ENET0_RGMII_TX_CTL            0
#define IMX8DXL_ENET0_RGMII_TX_CTL_CONN_USDHC1_RESET_B             IMX8DXL_ENET0_RGMII_TX_CTL            3
#define IMX8DXL_ENET0_RGMII_TX_CTL_LSIO_GPIO4_IO30                 IMX8DXL_ENET0_RGMII_TX_CTL            4
#define IMX8DXL_ENET0_RGMII_TX_CTL_CONN_USDHC2_CMD                 IMX8DXL_ENET0_RGMII_TX_CTL            5
#define IMX8DXL_ENET0_RGMII_TXD0_CONN_ENET0_RGMII_TXD0             IMX8DXL_ENET0_RGMII_TXD0              0
#define IMX8DXL_ENET0_RGMII_TXD0_CONN_USDHC1_VSELECT               IMX8DXL_ENET0_RGMII_TXD0              3
#define IMX8DXL_ENET0_RGMII_TXD0_LSIO_GPIO4_IO31                   IMX8DXL_ENET0_RGMII_TXD0              4
#define IMX8DXL_ENET0_RGMII_TXD0_CONN_USDHC2_DATA0                 IMX8DXL_ENET0_RGMII_TXD0              5
#define IMX8DXL_ENET0_RGMII_TXD1_CONN_ENET0_RGMII_TXD1             IMX8DXL_ENET0_RGMII_TXD1              0
#define IMX8DXL_ENET0_RGMII_TXD1_CONN_USDHC1_WP                    IMX8DXL_ENET0_RGMII_TXD1              3
#define IMX8DXL_ENET0_RGMII_TXD1_LSIO_GPIO5_IO00                   IMX8DXL_ENET0_RGMII_TXD1              4
#define IMX8DXL_ENET0_RGMII_TXD1_CONN_USDHC2_DATA1                 IMX8DXL_ENET0_RGMII_TXD1              5
#define IMX8DXL_ENET0_RGMII_TXD2_CONN_ENET0_RGMII_TXD2             IMX8DXL_ENET0_RGMII_TXD2              0
#define IMX8DXL_ENET0_RGMII_TXD2_CONN_NAND_CE0_B                   IMX8DXL_ENET0_RGMII_TXD2              2
#define IMX8DXL_ENET0_RGMII_TXD2_CONN_USDHC1_CD_B                  IMX8DXL_ENET0_RGMII_TXD2              3
#define IMX8DXL_ENET0_RGMII_TXD2_LSIO_GPIO5_IO01                   IMX8DXL_ENET0_RGMII_TXD2              4
#define IMX8DXL_ENET0_RGMII_TXD2_CONN_USDHC2_DATA2                 IMX8DXL_ENET0_RGMII_TXD2              5
#define IMX8DXL_ENET0_RGMII_TXD3_CONN_ENET0_RGMII_TXD3             IMX8DXL_ENET0_RGMII_TXD3              0
#define IMX8DXL_ENET0_RGMII_TXD3_CONN_NAND_RE_B                    IMX8DXL_ENET0_RGMII_TXD3              2
#define IMX8DXL_ENET0_RGMII_TXD3_LSIO_GPIO5_IO02                   IMX8DXL_ENET0_RGMII_TXD3              4
#define IMX8DXL_ENET0_RGMII_TXD3_CONN_USDHC2_DATA3                 IMX8DXL_ENET0_RGMII_TXD3              5
#define IMX8DXL_ENET0_RGMII_RXC_CONN_ENET0_RGMII_RXC               IMX8DXL_ENET0_RGMII_RXC               0
#define IMX8DXL_ENET0_RGMII_RXC_CONN_NAND_WE_B                     IMX8DXL_ENET0_RGMII_RXC               2
#define IMX8DXL_ENET0_RGMII_RXC_CONN_USDHC1_CLK                    IMX8DXL_ENET0_RGMII_RXC               3
#define IMX8DXL_ENET0_RGMII_RXC_LSIO_GPIO5_IO03                    IMX8DXL_ENET0_RGMII_RXC               4
#define IMX8DXL_ENET0_RGMII_RX_CTL_CONN_ENET0_RGMII_RX_CTL         IMX8DXL_ENET0_RGMII_RX_CTL            0
#define IMX8DXL_ENET0_RGMII_RX_CTL_CONN_USDHC1_CMD                 IMX8DXL_ENET0_RGMII_RX_CTL            3
#define IMX8DXL_ENET0_RGMII_RX_CTL_LSIO_GPIO5_IO04                 IMX8DXL_ENET0_RGMII_RX_CTL            4
#define IMX8DXL_ENET0_RGMII_RXD0_CONN_ENET0_RGMII_RXD0             IMX8DXL_ENET0_RGMII_RXD0              0
#define IMX8DXL_ENET0_RGMII_RXD0_CONN_USDHC1_DATA0                 IMX8DXL_ENET0_RGMII_RXD0              3
#define IMX8DXL_ENET0_RGMII_RXD0_LSIO_GPIO5_IO05                   IMX8DXL_ENET0_RGMII_RXD0              4
#define IMX8DXL_ENET0_RGMII_RXD1_CONN_ENET0_RGMII_RXD1             IMX8DXL_ENET0_RGMII_RXD1              0
#define IMX8DXL_ENET0_RGMII_RXD1_CONN_USDHC1_DATA1                 IMX8DXL_ENET0_RGMII_RXD1              3
#define IMX8DXL_ENET0_RGMII_RXD1_LSIO_GPIO5_IO06                   IMX8DXL_ENET0_RGMII_RXD1              4
#define IMX8DXL_ENET0_RGMII_RXD2_CONN_ENET0_RGMII_RXD2             IMX8DXL_ENET0_RGMII_RXD2              0
#define IMX8DXL_ENET0_RGMII_RXD2_CONN_ENET0_RMII_RX_ER             IMX8DXL_ENET0_RGMII_RXD2              1
#define IMX8DXL_ENET0_RGMII_RXD2_CONN_USDHC1_DATA2                 IMX8DXL_ENET0_RGMII_RXD2              3
#define IMX8DXL_ENET0_RGMII_RXD2_LSIO_GPIO5_IO07                   IMX8DXL_ENET0_RGMII_RXD2              4
#define IMX8DXL_ENET0_RGMII_RXD3_CONN_ENET0_RGMII_RXD3             IMX8DXL_ENET0_RGMII_RXD3              0
#define IMX8DXL_ENET0_RGMII_RXD3_CONN_NAND_ALE                     IMX8DXL_ENET0_RGMII_RXD3              2
#define IMX8DXL_ENET0_RGMII_RXD3_CONN_USDHC1_DATA3                 IMX8DXL_ENET0_RGMII_RXD3              3
#define IMX8DXL_ENET0_RGMII_RXD3_LSIO_GPIO5_IO08                   IMX8DXL_ENET0_RGMII_RXD3              4
#define IMX8DXL_ENET0_REFCLK_125M_25M_CONN_ENET0_REFCLK_125M_25M   IMX8DXL_ENET0_REFCLK_125M_25M         0
#define IMX8DXL_ENET0_REFCLK_125M_25M_CONN_ENET0_PPS               IMX8DXL_ENET0_REFCLK_125M_25M         1
#define IMX8DXL_ENET0_REFCLK_125M_25M_CONN_EQOS_PPS_IN             IMX8DXL_ENET0_REFCLK_125M_25M         2
#define IMX8DXL_ENET0_REFCLK_125M_25M_CONN_EQOS_PPS_OUT            IMX8DXL_ENET0_REFCLK_125M_25M         3
#define IMX8DXL_ENET0_REFCLK_125M_25M_LSIO_GPIO5_IO09              IMX8DXL_ENET0_REFCLK_125M_25M         4
#define IMX8DXL_ENET0_MDIO_CONN_ENET0_MDIO                         IMX8DXL_ENET0_MDIO                    0
#define IMX8DXL_ENET0_MDIO_ADMA_I2C3_SDA                           IMX8DXL_ENET0_MDIO                    1
#define IMX8DXL_ENET0_MDIO_CONN_EQOS_MDIO                          IMX8DXL_ENET0_MDIO                    2
#define IMX8DXL_ENET0_MDIO_LSIO_GPIO5_IO10                         IMX8DXL_ENET0_MDIO                    4
#define IMX8DXL_ENET0_MDIO_LSIO_GPIO7_IO16                         IMX8DXL_ENET0_MDIO                    5
#define IMX8DXL_ENET0_MDC_CONN_ENET0_MDC                           IMX8DXL_ENET0_MDC                     0
#define IMX8DXL_ENET0_MDC_ADMA_I2C3_SCL                            IMX8DXL_ENET0_MDC                     1
#define IMX8DXL_ENET0_MDC_CONN_EQOS_MDC                            IMX8DXL_ENET0_MDC                     2
#define IMX8DXL_ENET0_MDC_LSIO_GPIO5_IO11                          IMX8DXL_ENET0_MDC                     4
#define IMX8DXL_ENET0_MDC_LSIO_GPIO7_IO17                          IMX8DXL_ENET0_MDC                     5
#define IMX8DXL_ENET1_RGMII_TXC_LSIO_GPIO0_IO00                    IMX8DXL_ENET1_RGMII_TXC               0
#define IMX8DXL_ENET1_RGMII_TXC_CONN_EQOS_RCLK50M_OUT              IMX8DXL_ENET1_RGMII_TXC               1
#define IMX8DXL_ENET1_RGMII_TXC_ADMA_LCDIF_D00                     IMX8DXL_ENET1_RGMII_TXC               2
#define IMX8DXL_ENET1_RGMII_TXC_CONN_EQOS_RGMII_TXC                IMX8DXL_ENET1_RGMII_TXC               3
#define IMX8DXL_ENET1_RGMII_TXC_CONN_EQOS_RCLK50M_IN               IMX8DXL_ENET1_RGMII_TXC               4
#define IMX8DXL_ENET1_RGMII_TXD2_ADMA_LCDIF_D01                    IMX8DXL_ENET1_RGMII_TXD2              2
#define IMX8DXL_ENET1_RGMII_TXD2_CONN_EQOS_RGMII_TXD2              IMX8DXL_ENET1_RGMII_TXD2              3
#define IMX8DXL_ENET1_RGMII_TXD2_LSIO_GPIO0_IO01                   IMX8DXL_ENET1_RGMII_TXD2              4
#define IMX8DXL_ENET1_RGMII_TX_CTL_ADMA_LCDIF_D02                  IMX8DXL_ENET1_RGMII_TX_CTL            2
#define IMX8DXL_ENET1_RGMII_TX_CTL_CONN_EQOS_RGMII_TX_CTL          IMX8DXL_ENET1_RGMII_TX_CTL            3
#define IMX8DXL_ENET1_RGMII_TX_CTL_LSIO_GPIO0_IO02                 IMX8DXL_ENET1_RGMII_TX_CTL            4
#define IMX8DXL_ENET1_RGMII_TXD3_ADMA_LCDIF_D03                    IMX8DXL_ENET1_RGMII_TXD3              2
#define IMX8DXL_ENET1_RGMII_TXD3_CONN_EQOS_RGMII_TXD3              IMX8DXL_ENET1_RGMII_TXD3              3
#define IMX8DXL_ENET1_RGMII_TXD3_LSIO_GPIO0_IO03                   IMX8DXL_ENET1_RGMII_TXD3              4
#define IMX8DXL_ENET1_RGMII_RXC_ADMA_LCDIF_D04                     IMX8DXL_ENET1_RGMII_RXC               2
#define IMX8DXL_ENET1_RGMII_RXC_CONN_EQOS_RGMII_RXC                IMX8DXL_ENET1_RGMII_RXC               3
#define IMX8DXL_ENET1_RGMII_RXC_LSIO_GPIO0_IO04                    IMX8DXL_ENET1_RGMII_RXC               4
#define IMX8DXL_ENET1_RGMII_RXD3_ADMA_LCDIF_D05                    IMX8DXL_ENET1_RGMII_RXD3              2
#define IMX8DXL_ENET1_RGMII_RXD3_CONN_EQOS_RGMII_RXD3              IMX8DXL_ENET1_RGMII_RXD3              3
#define IMX8DXL_ENET1_RGMII_RXD3_LSIO_GPIO0_IO05                   IMX8DXL_ENET1_RGMII_RXD3              4
#define IMX8DXL_ENET1_RGMII_RXD2_ADMA_LCDIF_D06                    IMX8DXL_ENET1_RGMII_RXD2              2
#define IMX8DXL_ENET1_RGMII_RXD2_CONN_EQOS_RGMII_RXD2              IMX8DXL_ENET1_RGMII_RXD2              3
#define IMX8DXL_ENET1_RGMII_RXD2_LSIO_GPIO0_IO06                   IMX8DXL_ENET1_RGMII_RXD2              4
#define IMX8DXL_ENET1_RGMII_RXD2_LSIO_GPIO6_IO00                   IMX8DXL_ENET1_RGMII_RXD2              5
#define IMX8DXL_ENET1_RGMII_RXD1_ADMA_LCDIF_D07                    IMX8DXL_ENET1_RGMII_RXD1              2
#define IMX8DXL_ENET1_RGMII_RXD1_CONN_EQOS_RGMII_RXD1              IMX8DXL_ENET1_RGMII_RXD1              3
#define IMX8DXL_ENET1_RGMII_RXD1_LSIO_GPIO0_IO07                   IMX8DXL_ENET1_RGMII_RXD1              4
#define IMX8DXL_ENET1_RGMII_RXD1_LSIO_GPIO6_IO01                   IMX8DXL_ENET1_RGMII_RXD1              5
#define IMX8DXL_ENET1_RGMII_TXD0_ADMA_LCDIF_D08                    IMX8DXL_ENET1_RGMII_TXD0              2
#define IMX8DXL_ENET1_RGMII_TXD0_CONN_EQOS_RGMII_TXD0              IMX8DXL_ENET1_RGMII_TXD0              3
#define IMX8DXL_ENET1_RGMII_TXD0_LSIO_GPIO0_IO08                   IMX8DXL_ENET1_RGMII_TXD0              4
#define IMX8DXL_ENET1_RGMII_TXD0_LSIO_GPIO6_IO02                   IMX8DXL_ENET1_RGMII_TXD0              5
#define IMX8DXL_ENET1_RGMII_TXD1_ADMA_LCDIF_D09                    IMX8DXL_ENET1_RGMII_TXD1              2
#define IMX8DXL_ENET1_RGMII_TXD1_CONN_EQOS_RGMII_TXD1              IMX8DXL_ENET1_RGMII_TXD1              3
#define IMX8DXL_ENET1_RGMII_TXD1_LSIO_GPIO0_IO09                   IMX8DXL_ENET1_RGMII_TXD1              4
#define IMX8DXL_ENET1_RGMII_TXD1_LSIO_GPIO6_IO03                   IMX8DXL_ENET1_RGMII_TXD1              5
#define IMX8DXL_ENET1_RGMII_RXD0_ADMA_SPDIF0_RX                    IMX8DXL_ENET1_RGMII_RXD0              0
#define IMX8DXL_ENET1_RGMII_RXD0_ADMA_MQS_R                        IMX8DXL_ENET1_RGMII_RXD0              1
#define IMX8DXL_ENET1_RGMII_RXD0_ADMA_LCDIF_D10                    IMX8DXL_ENET1_RGMII_RXD0              2
#define IMX8DXL_ENET1_RGMII_RXD0_CONN_EQOS_RGMII_RXD0              IMX8DXL_ENET1_RGMII_RXD0              3
#define IMX8DXL_ENET1_RGMII_RXD0_LSIO_GPIO0_IO10                   IMX8DXL_ENET1_RGMII_RXD0              4
#define IMX8DXL_ENET1_RGMII_RXD0_LSIO_GPIO6_IO04                   IMX8DXL_ENET1_RGMII_RXD0              5
#define IMX8DXL_ENET1_RGMII_RX_CTL_ADMA_SPDIF0_TX                  IMX8DXL_ENET1_RGMII_RX_CTL            0
#define IMX8DXL_ENET1_RGMII_RX_CTL_ADMA_MQS_L                      IMX8DXL_ENET1_RGMII_RX_CTL            1
#define IMX8DXL_ENET1_RGMII_RX_CTL_ADMA_LCDIF_D11                  IMX8DXL_ENET1_RGMII_RX_CTL            2
#define IMX8DXL_ENET1_RGMII_RX_CTL_CONN_EQOS_RGMII_RX_CTL          IMX8DXL_ENET1_RGMII_RX_CTL            3
#define IMX8DXL_ENET1_RGMII_RX_CTL_LSIO_GPIO0_IO11                 IMX8DXL_ENET1_RGMII_RX_CTL            4
#define IMX8DXL_ENET1_RGMII_RX_CTL_LSIO_GPIO6_IO05                 IMX8DXL_ENET1_RGMII_RX_CTL            5
#define IMX8DXL_ENET1_REFCLK_125M_25M_ADMA_SPDIF0_EXT_CLK          IMX8DXL_ENET1_REFCLK_125M_25M         0
#define IMX8DXL_ENET1_REFCLK_125M_25M_ADMA_LCDIF_D12               IMX8DXL_ENET1_REFCLK_125M_25M         2
#define IMX8DXL_ENET1_REFCLK_125M_25M_CONN_EQOS_REFCLK_125M_25M    IMX8DXL_ENET1_REFCLK_125M_25M         3
#define IMX8DXL_ENET1_REFCLK_125M_25M_LSIO_GPIO0_IO12              IMX8DXL_ENET1_REFCLK_125M_25M         4
#define IMX8DXL_ENET1_REFCLK_125M_25M_LSIO_GPIO6_IO06              IMX8DXL_ENET1_REFCLK_125M_25M         5
#define IMX8DXL_SPI3_SCK_ADMA_SPI3_SCK                             IMX8DXL_SPI3_SCK                      0
#define IMX8DXL_SPI3_SCK_ADMA_LCDIF_D13                            IMX8DXL_SPI3_SCK                      2
#define IMX8DXL_SPI3_SCK_LSIO_GPIO0_IO13                           IMX8DXL_SPI3_SCK                      4
#define IMX8DXL_SPI3_SCK_ADMA_LCDIF_D00                            IMX8DXL_SPI3_SCK                      5
#define IMX8DXL_SPI3_SDO_ADMA_SPI3_SDO                             IMX8DXL_SPI3_SDO                      0
#define IMX8DXL_SPI3_SDO_ADMA_LCDIF_D14                            IMX8DXL_SPI3_SDO                      2
#define IMX8DXL_SPI3_SDO_LSIO_GPIO0_IO14                           IMX8DXL_SPI3_SDO                      4
#define IMX8DXL_SPI3_SDO_ADMA_LCDIF_D01                            IMX8DXL_SPI3_SDO                      5
#define IMX8DXL_SPI3_SDI_ADMA_SPI3_SDI                             IMX8DXL_SPI3_SDI                      0
#define IMX8DXL_SPI3_SDI_ADMA_LCDIF_D15                            IMX8DXL_SPI3_SDI                      2
#define IMX8DXL_SPI3_SDI_LSIO_GPIO0_IO15                           IMX8DXL_SPI3_SDI                      4
#define IMX8DXL_SPI3_SDI_ADMA_LCDIF_D02                            IMX8DXL_SPI3_SDI                      5
#define IMX8DXL_SPI3_CS0_ADMA_SPI3_CS0                             IMX8DXL_SPI3_CS0                      0
#define IMX8DXL_SPI3_CS0_ADMA_ACM_MCLK_OUT1                        IMX8DXL_SPI3_CS0                      1
#define IMX8DXL_SPI3_CS0_ADMA_LCDIF_HSYNC                          IMX8DXL_SPI3_CS0                      2
#define IMX8DXL_SPI3_CS0_LSIO_GPIO0_IO16                           IMX8DXL_SPI3_CS0                      4
#define IMX8DXL_SPI3_CS0_ADMA_LCDIF_CS                             IMX8DXL_SPI3_CS0                      5
#define IMX8DXL_SPI3_CS1_ADMA_SPI3_CS1                             IMX8DXL_SPI3_CS1                      0
#define IMX8DXL_SPI3_CS1_ADMA_I2C3_SCL                             IMX8DXL_SPI3_CS1                      1
#define IMX8DXL_SPI3_CS1_ADMA_LCDIF_RESET                          IMX8DXL_SPI3_CS1                      2
#define IMX8DXL_SPI3_CS1_ADMA_SPI2_CS0                             IMX8DXL_SPI3_CS1                      3
#define IMX8DXL_SPI3_CS1_ADMA_LCDIF_D16                            IMX8DXL_SPI3_CS1                      4
#define IMX8DXL_SPI3_CS1_ADMA_LCDIF_RD_E                           IMX8DXL_SPI3_CS1                      5
#define IMX8DXL_MCLK_IN1_ADMA_ACM_MCLK_IN1                         IMX8DXL_MCLK_IN1                      0
#define IMX8DXL_MCLK_IN1_ADMA_I2C3_SDA                             IMX8DXL_MCLK_IN1                      1
#define IMX8DXL_MCLK_IN1_ADMA_LCDIF_EN                             IMX8DXL_MCLK_IN1                      2
#define IMX8DXL_MCLK_IN1_ADMA_SPI2_SCK                             IMX8DXL_MCLK_IN1                      3
#define IMX8DXL_MCLK_IN1_ADMA_LCDIF_D17                            IMX8DXL_MCLK_IN1                      4
#define IMX8DXL_MCLK_IN1_ADMA_LCDIF_D03                            IMX8DXL_MCLK_IN1                      5
#define IMX8DXL_MCLK_IN0_ADMA_ACM_MCLK_IN0                         IMX8DXL_MCLK_IN0                      0
#define IMX8DXL_MCLK_IN0_ADMA_LCDIF_VSYNC                          IMX8DXL_MCLK_IN0                      2
#define IMX8DXL_MCLK_IN0_ADMA_SPI2_SDI                             IMX8DXL_MCLK_IN0                      3
#define IMX8DXL_MCLK_IN0_LSIO_GPIO0_IO19                           IMX8DXL_MCLK_IN0                      4
#define IMX8DXL_MCLK_IN0_ADMA_LCDIF_RS                             IMX8DXL_MCLK_IN0                      5
#define IMX8DXL_MCLK_OUT0_ADMA_ACM_MCLK_OUT0                       IMX8DXL_MCLK_OUT0                     0
#define IMX8DXL_MCLK_OUT0_ADMA_LCDIF_CLK                           IMX8DXL_MCLK_OUT0                     2
#define IMX8DXL_MCLK_OUT0_ADMA_SPI2_SDO                            IMX8DXL_MCLK_OUT0                     3
#define IMX8DXL_MCLK_OUT0_LSIO_GPIO0_IO20                          IMX8DXL_MCLK_OUT0                     4
#define IMX8DXL_MCLK_OUT0_ADMA_LCDIF_WR_RWN                        IMX8DXL_MCLK_OUT0                     5
#define IMX8DXL_UART1_TX_ADMA_UART1_TX                             IMX8DXL_UART1_TX                      0
#define IMX8DXL_UART1_TX_LSIO_PWM0_OUT                             IMX8DXL_UART1_TX                      1
#define IMX8DXL_UART1_TX_LSIO_GPT0_CAPTURE                         IMX8DXL_UART1_TX                      2
#define IMX8DXL_UART1_TX_LSIO_GPIO0_IO21                           IMX8DXL_UART1_TX                      4
#define IMX8DXL_UART1_TX_ADMA_LCDIF_D04                            IMX8DXL_UART1_TX                      5
#define IMX8DXL_UART1_RX_ADMA_UART1_RX                             IMX8DXL_UART1_RX                      0
#define IMX8DXL_UART1_RX_LSIO_PWM1_OUT                             IMX8DXL_UART1_RX                      1
#define IMX8DXL_UART1_RX_LSIO_GPT0_COMPARE                         IMX8DXL_UART1_RX                      2
#define IMX8DXL_UART1_RX_LSIO_GPT1_CLK                             IMX8DXL_UART1_RX                      3
#define IMX8DXL_UART1_RX_LSIO_GPIO0_IO22                           IMX8DXL_UART1_RX                      4
#define IMX8DXL_UART1_RX_ADMA_LCDIF_D05                            IMX8DXL_UART1_RX                      5
#define IMX8DXL_UART1_RTS_B_ADMA_UART1_RTS_B                       IMX8DXL_UART1_RTS_B                   0
#define IMX8DXL_UART1_RTS_B_LSIO_PWM2_OUT                          IMX8DXL_UART1_RTS_B                   1
#define IMX8DXL_UART1_RTS_B_ADMA_LCDIF_D16                         IMX8DXL_UART1_RTS_B                   2
#define IMX8DXL_UART1_RTS_B_LSIO_GPT1_CAPTURE                      IMX8DXL_UART1_RTS_B                   3
#define IMX8DXL_UART1_RTS_B_LSIO_GPT0_CLK                          IMX8DXL_UART1_RTS_B                   4
#define IMX8DXL_UART1_RTS_B_ADMA_LCDIF_D06                         IMX8DXL_UART1_RTS_B                   5
#define IMX8DXL_UART1_CTS_B_ADMA_UART1_CTS_B                       IMX8DXL_UART1_CTS_B                   0
#define IMX8DXL_UART1_CTS_B_LSIO_PWM3_OUT                          IMX8DXL_UART1_CTS_B                   1
#define IMX8DXL_UART1_CTS_B_ADMA_LCDIF_D17                         IMX8DXL_UART1_CTS_B                   2
#define IMX8DXL_UART1_CTS_B_LSIO_GPT1_COMPARE                      IMX8DXL_UART1_CTS_B                   3
#define IMX8DXL_UART1_CTS_B_LSIO_GPIO0_IO24                        IMX8DXL_UART1_CTS_B                   4
#define IMX8DXL_UART1_CTS_B_ADMA_LCDIF_D07                         IMX8DXL_UART1_CTS_B                   5
#define IMX8DXL_SPI0_SCK_ADMA_SPI0_SCK                             IMX8DXL_SPI0_SCK                      0
#define IMX8DXL_SPI0_SCK_ADMA_SAI0_TXC                             IMX8DXL_SPI0_SCK                      1
#define IMX8DXL_SPI0_SCK_M40_I2C0_SCL                              IMX8DXL_SPI0_SCK                      2
#define IMX8DXL_SPI0_SCK_M40_GPIO0_IO00                            IMX8DXL_SPI0_SCK                      3
#define IMX8DXL_SPI0_SCK_LSIO_GPIO1_IO04                           IMX8DXL_SPI0_SCK                      4
#define IMX8DXL_SPI0_SCK_ADMA_LCDIF_D08                            IMX8DXL_SPI0_SCK                      5
#define IMX8DXL_SPI0_SDI_ADMA_SPI0_SDI                             IMX8DXL_SPI0_SDI                      0
#define IMX8DXL_SPI0_SDI_ADMA_SAI0_TXD                             IMX8DXL_SPI0_SDI                      1
#define IMX8DXL_SPI0_SDI_M40_TPM0_CH0                              IMX8DXL_SPI0_SDI                      2
#define IMX8DXL_SPI0_SDI_M40_GPIO0_IO02                            IMX8DXL_SPI0_SDI                      3
#define IMX8DXL_SPI0_SDI_LSIO_GPIO1_IO05                           IMX8DXL_SPI0_SDI                      4
#define IMX8DXL_SPI0_SDI_ADMA_LCDIF_D09                            IMX8DXL_SPI0_SDI                      5
#define IMX8DXL_SPI0_SDO_ADMA_SPI0_SDO                             IMX8DXL_SPI0_SDO                      0
#define IMX8DXL_SPI0_SDO_ADMA_SAI0_TXFS                            IMX8DXL_SPI0_SDO                      1
#define IMX8DXL_SPI0_SDO_M40_I2C0_SDA                              IMX8DXL_SPI0_SDO                      2
#define IMX8DXL_SPI0_SDO_M40_GPIO0_IO01                            IMX8DXL_SPI0_SDO                      3
#define IMX8DXL_SPI0_SDO_LSIO_GPIO1_IO06                           IMX8DXL_SPI0_SDO                      4
#define IMX8DXL_SPI0_SDO_ADMA_LCDIF_D10                            IMX8DXL_SPI0_SDO                      5
#define IMX8DXL_SPI0_CS1_ADMA_SPI0_CS1                             IMX8DXL_SPI0_CS1                      0
#define IMX8DXL_SPI0_CS1_ADMA_SAI0_RXC                             IMX8DXL_SPI0_CS1                      1
#define IMX8DXL_SPI0_CS1_ADMA_SAI1_TXD                             IMX8DXL_SPI0_CS1                      2
#define IMX8DXL_SPI0_CS1_ADMA_LCD_PWM0_OUT                         IMX8DXL_SPI0_CS1                      3
#define IMX8DXL_SPI0_CS1_LSIO_GPIO1_IO07                           IMX8DXL_SPI0_CS1                      4
#define IMX8DXL_SPI0_CS1_ADMA_LCDIF_D11                            IMX8DXL_SPI0_CS1                      5
#define IMX8DXL_SPI0_CS0_ADMA_SPI0_CS0                             IMX8DXL_SPI0_CS0                      0
#define IMX8DXL_SPI0_CS0_ADMA_SAI0_RXD                             IMX8DXL_SPI0_CS0                      1
#define IMX8DXL_SPI0_CS0_M40_TPM0_CH1                              IMX8DXL_SPI0_CS0                      2
#define IMX8DXL_SPI0_CS0_M40_GPIO0_IO03                            IMX8DXL_SPI0_CS0                      3
#define IMX8DXL_SPI0_CS0_LSIO_GPIO1_IO08                           IMX8DXL_SPI0_CS0                      4
#define IMX8DXL_SPI0_CS0_ADMA_LCDIF_D12                            IMX8DXL_SPI0_CS0                      5
#define IMX8DXL_ADC_IN1_ADMA_ADC_IN1                               IMX8DXL_ADC_IN1                       0
#define IMX8DXL_ADC_IN1_M40_I2C0_SDA                               IMX8DXL_ADC_IN1                       1
#define IMX8DXL_ADC_IN1_M40_GPIO0_IO01                             IMX8DXL_ADC_IN1                       2
#define IMX8DXL_ADC_IN1_ADMA_I2C0_SDA                              IMX8DXL_ADC_IN1                       3
#define IMX8DXL_ADC_IN1_LSIO_GPIO1_IO09                            IMX8DXL_ADC_IN1                       4
#define IMX8DXL_ADC_IN1_ADMA_LCDIF_D13                             IMX8DXL_ADC_IN1                       5
#define IMX8DXL_ADC_IN0_ADMA_ADC_IN0                               IMX8DXL_ADC_IN0                       0
#define IMX8DXL_ADC_IN0_M40_I2C0_SCL                               IMX8DXL_ADC_IN0                       1
#define IMX8DXL_ADC_IN0_M40_GPIO0_IO00                             IMX8DXL_ADC_IN0                       2
#define IMX8DXL_ADC_IN0_ADMA_I2C0_SCL                              IMX8DXL_ADC_IN0                       3
#define IMX8DXL_ADC_IN0_LSIO_GPIO1_IO10                            IMX8DXL_ADC_IN0                       4
#define IMX8DXL_ADC_IN0_ADMA_LCDIF_D14                             IMX8DXL_ADC_IN0                       5
#define IMX8DXL_ADC_IN3_ADMA_ADC_IN3                               IMX8DXL_ADC_IN3                       0
#define IMX8DXL_ADC_IN3_M40_UART0_TX                               IMX8DXL_ADC_IN3                       1
#define IMX8DXL_ADC_IN3_M40_GPIO0_IO03                             IMX8DXL_ADC_IN3                       2
#define IMX8DXL_ADC_IN3_ADMA_ACM_MCLK_OUT0                         IMX8DXL_ADC_IN3                       3
#define IMX8DXL_ADC_IN3_LSIO_GPIO1_IO11                            IMX8DXL_ADC_IN3                       4
#define IMX8DXL_ADC_IN3_ADMA_LCDIF_D15                             IMX8DXL_ADC_IN3                       5
#define IMX8DXL_ADC_IN2_ADMA_ADC_IN2                               IMX8DXL_ADC_IN2                       0
#define IMX8DXL_ADC_IN2_M40_UART0_RX                               IMX8DXL_ADC_IN2                       1
#define IMX8DXL_ADC_IN2_M40_GPIO0_IO02                             IMX8DXL_ADC_IN2                       2
#define IMX8DXL_ADC_IN2_ADMA_ACM_MCLK_IN0                          IMX8DXL_ADC_IN2                       3
#define IMX8DXL_ADC_IN2_LSIO_GPIO1_IO12                            IMX8DXL_ADC_IN2                       4
#define IMX8DXL_ADC_IN2_ADMA_LCDIF_D16                             IMX8DXL_ADC_IN2                       5
#define IMX8DXL_ADC_IN5_ADMA_ADC_IN5                               IMX8DXL_ADC_IN5                       0
#define IMX8DXL_ADC_IN5_M40_TPM0_CH1                               IMX8DXL_ADC_IN5                       1
#define IMX8DXL_ADC_IN5_M40_GPIO0_IO05                             IMX8DXL_ADC_IN5                       2
#define IMX8DXL_ADC_IN5_ADMA_LCDIF_LCDBUSY                         IMX8DXL_ADC_IN5                       3
#define IMX8DXL_ADC_IN5_LSIO_GPIO1_IO13                            IMX8DXL_ADC_IN5                       4
#define IMX8DXL_ADC_IN5_ADMA_LCDIF_D17                             IMX8DXL_ADC_IN5                       5
#define IMX8DXL_ADC_IN4_ADMA_ADC_IN4                               IMX8DXL_ADC_IN4                       0
#define IMX8DXL_ADC_IN4_M40_TPM0_CH0                               IMX8DXL_ADC_IN4                       1
#define IMX8DXL_ADC_IN4_M40_GPIO0_IO04                             IMX8DXL_ADC_IN4                       2
#define IMX8DXL_ADC_IN4_ADMA_LCDIF_LCDRESET                        IMX8DXL_ADC_IN4                       3
#define IMX8DXL_ADC_IN4_LSIO_GPIO1_IO14                            IMX8DXL_ADC_IN4                       4
#define IMX8DXL_FLEXCAN0_RX_ADMA_FLEXCAN0_RX                       IMX8DXL_FLEXCAN0_RX                   0
#define IMX8DXL_FLEXCAN0_RX_ADMA_SAI2_RXC                          IMX8DXL_FLEXCAN0_RX                   1
#define IMX8DXL_FLEXCAN0_RX_ADMA_UART0_RTS_B                       IMX8DXL_FLEXCAN0_RX                   2
#define IMX8DXL_FLEXCAN0_RX_ADMA_SAI1_TXC                          IMX8DXL_FLEXCAN0_RX                   3
#define IMX8DXL_FLEXCAN0_RX_LSIO_GPIO1_IO15                        IMX8DXL_FLEXCAN0_RX                   4
#define IMX8DXL_FLEXCAN0_RX_LSIO_GPIO6_IO08                        IMX8DXL_FLEXCAN0_RX                   5
#define IMX8DXL_FLEXCAN0_TX_ADMA_FLEXCAN0_TX                       IMX8DXL_FLEXCAN0_TX                   0
#define IMX8DXL_FLEXCAN0_TX_ADMA_SAI2_RXD                          IMX8DXL_FLEXCAN0_TX                   1
#define IMX8DXL_FLEXCAN0_TX_ADMA_UART0_CTS_B                       IMX8DXL_FLEXCAN0_TX                   2
#define IMX8DXL_FLEXCAN0_TX_ADMA_SAI1_TXFS                         IMX8DXL_FLEXCAN0_TX                   3
#define IMX8DXL_FLEXCAN0_TX_LSIO_GPIO1_IO16                        IMX8DXL_FLEXCAN0_TX                   4
#define IMX8DXL_FLEXCAN0_TX_LSIO_GPIO6_IO09                        IMX8DXL_FLEXCAN0_TX                   5
#define IMX8DXL_FLEXCAN1_RX_ADMA_FLEXCAN1_RX                       IMX8DXL_FLEXCAN1_RX                   0
#define IMX8DXL_FLEXCAN1_RX_ADMA_SAI2_RXFS                         IMX8DXL_FLEXCAN1_RX                   1
#define IMX8DXL_FLEXCAN1_RX_ADMA_FTM_CH2                           IMX8DXL_FLEXCAN1_RX                   2
#define IMX8DXL_FLEXCAN1_RX_ADMA_SAI1_TXD                          IMX8DXL_FLEXCAN1_RX                   3
#define IMX8DXL_FLEXCAN1_RX_LSIO_GPIO1_IO17                        IMX8DXL_FLEXCAN1_RX                   4
#define IMX8DXL_FLEXCAN1_RX_LSIO_GPIO6_IO10                        IMX8DXL_FLEXCAN1_RX                   5
#define IMX8DXL_FLEXCAN1_TX_ADMA_FLEXCAN1_TX                       IMX8DXL_FLEXCAN1_TX                   0
#define IMX8DXL_FLEXCAN1_TX_ADMA_SAI3_RXC                          IMX8DXL_FLEXCAN1_TX                   1
#define IMX8DXL_FLEXCAN1_TX_ADMA_DMA0_REQ_IN0                      IMX8DXL_FLEXCAN1_TX                   2
#define IMX8DXL_FLEXCAN1_TX_ADMA_SAI1_RXD                          IMX8DXL_FLEXCAN1_TX                   3
#define IMX8DXL_FLEXCAN1_TX_LSIO_GPIO1_IO18                        IMX8DXL_FLEXCAN1_TX                   4
#define IMX8DXL_FLEXCAN1_TX_LSIO_GPIO6_IO11                        IMX8DXL_FLEXCAN1_TX                   5
#define IMX8DXL_FLEXCAN2_RX_ADMA_FLEXCAN2_RX                       IMX8DXL_FLEXCAN2_RX                   0
#define IMX8DXL_FLEXCAN2_RX_ADMA_SAI3_RXD                          IMX8DXL_FLEXCAN2_RX                   1
#define IMX8DXL_FLEXCAN2_RX_ADMA_UART3_RX                          IMX8DXL_FLEXCAN2_RX                   2
#define IMX8DXL_FLEXCAN2_RX_ADMA_SAI1_RXFS                         IMX8DXL_FLEXCAN2_RX                   3
#define IMX8DXL_FLEXCAN2_RX_LSIO_GPIO1_IO19                        IMX8DXL_FLEXCAN2_RX                   4
#define IMX8DXL_FLEXCAN2_RX_LSIO_GPIO6_IO12                        IMX8DXL_FLEXCAN2_RX                   5
#define IMX8DXL_FLEXCAN2_TX_ADMA_FLEXCAN2_TX                       IMX8DXL_FLEXCAN2_TX                   0
#define IMX8DXL_FLEXCAN2_TX_ADMA_SAI3_RXFS                         IMX8DXL_FLEXCAN2_TX                   1
#define IMX8DXL_FLEXCAN2_TX_ADMA_UART3_TX                          IMX8DXL_FLEXCAN2_TX                   2
#define IMX8DXL_FLEXCAN2_TX_ADMA_SAI1_RXC                          IMX8DXL_FLEXCAN2_TX                   3
#define IMX8DXL_FLEXCAN2_TX_LSIO_GPIO1_IO20                        IMX8DXL_FLEXCAN2_TX                   4
#define IMX8DXL_FLEXCAN2_TX_LSIO_GPIO6_IO13                        IMX8DXL_FLEXCAN2_TX                   5
#define IMX8DXL_UART0_RX_ADMA_UART0_RX                             IMX8DXL_UART0_RX                      0
#define IMX8DXL_UART0_RX_ADMA_MQS_R                                IMX8DXL_UART0_RX                      1
#define IMX8DXL_UART0_RX_ADMA_FLEXCAN0_RX                          IMX8DXL_UART0_RX                      2
#define IMX8DXL_UART0_RX_SCU_UART0_RX                              IMX8DXL_UART0_RX                      3
#define IMX8DXL_UART0_RX_LSIO_GPIO1_IO21                           IMX8DXL_UART0_RX                      4
#define IMX8DXL_UART0_RX_LSIO_GPIO6_IO14                           IMX8DXL_UART0_RX                      5
#define IMX8DXL_UART0_TX_ADMA_UART0_TX                             IMX8DXL_UART0_TX                      0
#define IMX8DXL_UART0_TX_ADMA_MQS_L                                IMX8DXL_UART0_TX                      1
#define IMX8DXL_UART0_TX_ADMA_FLEXCAN0_TX                          IMX8DXL_UART0_TX                      2
#define IMX8DXL_UART0_TX_SCU_UART0_TX                              IMX8DXL_UART0_TX                      3
#define IMX8DXL_UART0_TX_LSIO_GPIO1_IO22                           IMX8DXL_UART0_TX                      4
#define IMX8DXL_UART0_TX_LSIO_GPIO6_IO15                           IMX8DXL_UART0_TX                      5
#define IMX8DXL_UART2_TX_ADMA_UART2_TX                             IMX8DXL_UART2_TX                      0
#define IMX8DXL_UART2_TX_ADMA_FTM_CH1                              IMX8DXL_UART2_TX                      1
#define IMX8DXL_UART2_TX_ADMA_FLEXCAN1_TX                          IMX8DXL_UART2_TX                      2
#define IMX8DXL_UART2_TX_LSIO_GPIO1_IO23                           IMX8DXL_UART2_TX                      4
#define IMX8DXL_UART2_TX_LSIO_GPIO6_IO16                           IMX8DXL_UART2_TX                      5
#define IMX8DXL_UART2_RX_ADMA_UART2_RX                             IMX8DXL_UART2_RX                      0
#define IMX8DXL_UART2_RX_ADMA_FTM_CH0                              IMX8DXL_UART2_RX                      1
#define IMX8DXL_UART2_RX_ADMA_FLEXCAN1_RX                          IMX8DXL_UART2_RX                      2
#define IMX8DXL_UART2_RX_LSIO_GPIO1_IO24                           IMX8DXL_UART2_RX                      4
#define IMX8DXL_UART2_RX_LSIO_GPIO6_IO17                           IMX8DXL_UART2_RX                      5
#define IMX8DXL_JTAG_TRST_B_SCU_JTAG_TRST_B                        IMX8DXL_JTAG_TRST_B                   0
#define IMX8DXL_JTAG_TRST_B_SCU_WDOG0_WDOG_OUT                     IMX8DXL_JTAG_TRST_B                   1
#define IMX8DXL_PMIC_I2C_SCL_SCU_PMIC_I2C_SCL                      IMX8DXL_PMIC_I2C_SCL                  0
#define IMX8DXL_PMIC_I2C_SCL_SCU_GPIO0_IOXX_PMIC_A35_ON            IMX8DXL_PMIC_I2C_SCL                  1
#define IMX8DXL_PMIC_I2C_SCL_LSIO_GPIO2_IO01                       IMX8DXL_PMIC_I2C_SCL                  4
#define IMX8DXL_PMIC_I2C_SDA_SCU_PMIC_I2C_SDA                      IMX8DXL_PMIC_I2C_SDA                  0
#define IMX8DXL_PMIC_I2C_SDA_SCU_GPIO0_IOXX_PMIC_GPU_ON            IMX8DXL_PMIC_I2C_SDA                  1
#define IMX8DXL_PMIC_I2C_SDA_LSIO_GPIO2_IO02                       IMX8DXL_PMIC_I2C_SDA                  4
#define IMX8DXL_PMIC_INT_B_SCU_DSC_PMIC_INT_B                      IMX8DXL_PMIC_INT_B                    0
#define IMX8DXL_SCU_GPIO0_00_SCU_GPIO0_IO00                        IMX8DXL_SCU_GPIO0_00                  0
#define IMX8DXL_SCU_GPIO0_00_SCU_UART0_RX                          IMX8DXL_SCU_GPIO0_00                  1
#define IMX8DXL_SCU_GPIO0_00_M40_UART0_RX                          IMX8DXL_SCU_GPIO0_00                  2
#define IMX8DXL_SCU_GPIO0_00_ADMA_UART3_RX                         IMX8DXL_SCU_GPIO0_00                  3
#define IMX8DXL_SCU_GPIO0_00_LSIO_GPIO2_IO03                       IMX8DXL_SCU_GPIO0_00                  4
#define IMX8DXL_SCU_GPIO0_01_SCU_GPIO0_IO01                        IMX8DXL_SCU_GPIO0_01                  0
#define IMX8DXL_SCU_GPIO0_01_SCU_UART0_TX                          IMX8DXL_SCU_GPIO0_01                  1
#define IMX8DXL_SCU_GPIO0_01_M40_UART0_TX                          IMX8DXL_SCU_GPIO0_01                  2
#define IMX8DXL_SCU_GPIO0_01_ADMA_UART3_TX                         IMX8DXL_SCU_GPIO0_01                  3
#define IMX8DXL_SCU_GPIO0_01_SCU_WDOG0_WDOG_OUT                    IMX8DXL_SCU_GPIO0_01                  4
#define IMX8DXL_SCU_PMIC_STANDBY_SCU_DSC_PMIC_STANDBY              IMX8DXL_SCU_PMIC_STANDBY              0
#define IMX8DXL_SCU_BOOT_MODE1_SCU_DSC_BOOT_MODE1                  IMX8DXL_SCU_BOOT_MODE1                0
#define IMX8DXL_SCU_BOOT_MODE0_SCU_DSC_BOOT_MODE0                  IMX8DXL_SCU_BOOT_MODE0                0
#define IMX8DXL_SCU_BOOT_MODE2_SCU_DSC_BOOT_MODE2                  IMX8DXL_SCU_BOOT_MODE2                0
#define IMX8DXL_SCU_BOOT_MODE2_SCU_DSC_RTC_CLOCK_OUTPUT_32K        IMX8DXL_SCU_BOOT_MODE2                1
#define IMX8DXL_SNVS_TAMPER_OUT1_LSIO_GPIO2_IO05_IN                IMX8DXL_SNVS_TAMPER_OUT1              4
#define IMX8DXL_SNVS_TAMPER_OUT1_LSIO_GPIO6_IO19_IN                IMX8DXL_SNVS_TAMPER_OUT1              5
#define IMX8DXL_SNVS_TAMPER_OUT2_LSIO_GPIO2_IO06_IN                IMX8DXL_SNVS_TAMPER_OUT2              4
#define IMX8DXL_SNVS_TAMPER_OUT2_LSIO_GPIO6_IO20_IN                IMX8DXL_SNVS_TAMPER_OUT2              5
#define IMX8DXL_SNVS_TAMPER_OUT3_ADMA_SAI2_RXC                     IMX8DXL_SNVS_TAMPER_OUT3              2
#define IMX8DXL_SNVS_TAMPER_OUT3_LSIO_GPIO2_IO07_IN                IMX8DXL_SNVS_TAMPER_OUT3              4
#define IMX8DXL_SNVS_TAMPER_OUT3_LSIO_GPIO6_IO21_IN                IMX8DXL_SNVS_TAMPER_OUT3              5
#define IMX8DXL_SNVS_TAMPER_OUT4_ADMA_SAI2_RXD                     IMX8DXL_SNVS_TAMPER_OUT4              2
#define IMX8DXL_SNVS_TAMPER_OUT4_LSIO_GPIO2_IO08_IN                IMX8DXL_SNVS_TAMPER_OUT4              4
#define IMX8DXL_SNVS_TAMPER_OUT4_LSIO_GPIO6_IO22_IN                IMX8DXL_SNVS_TAMPER_OUT4              5
#define IMX8DXL_SNVS_TAMPER_IN0_ADMA_SAI2_RXFS                     IMX8DXL_SNVS_TAMPER_IN0               2
#define IMX8DXL_SNVS_TAMPER_IN0_LSIO_GPIO2_IO09_IN                 IMX8DXL_SNVS_TAMPER_IN0               4
#define IMX8DXL_SNVS_TAMPER_IN0_LSIO_GPIO6_IO23_IN                 IMX8DXL_SNVS_TAMPER_IN0               5
#define IMX8DXL_SNVS_TAMPER_IN1_ADMA_SAI3_RXC                      IMX8DXL_SNVS_TAMPER_IN1               2
#define IMX8DXL_SNVS_TAMPER_IN1_LSIO_GPIO2_IO10_IN                 IMX8DXL_SNVS_TAMPER_IN1               4
#define IMX8DXL_SNVS_TAMPER_IN1_LSIO_GPIO6_IO24_IN                 IMX8DXL_SNVS_TAMPER_IN1               5
#define IMX8DXL_SNVS_TAMPER_IN2_ADMA_SAI3_RXD                      IMX8DXL_SNVS_TAMPER_IN2               2
#define IMX8DXL_SNVS_TAMPER_IN2_LSIO_GPIO2_IO11_IN                 IMX8DXL_SNVS_TAMPER_IN2               4
#define IMX8DXL_SNVS_TAMPER_IN2_LSIO_GPIO6_IO25_IN                 IMX8DXL_SNVS_TAMPER_IN2               5
#define IMX8DXL_SNVS_TAMPER_IN3_ADMA_SAI3_RXFS                     IMX8DXL_SNVS_TAMPER_IN3               2
#define IMX8DXL_SNVS_TAMPER_IN3_LSIO_GPIO2_IO12_IN                 IMX8DXL_SNVS_TAMPER_IN3               4
#define IMX8DXL_SNVS_TAMPER_IN3_LSIO_GPIO6_IO26_IN                 IMX8DXL_SNVS_TAMPER_IN3               5
#define IMX8DXL_SPI1_SCK_ADMA_I2C2_SDA                             IMX8DXL_SPI1_SCK                      2
#define IMX8DXL_SPI1_SCK_ADMA_SPI1_SCK                             IMX8DXL_SPI1_SCK                      3
#define IMX8DXL_SPI1_SCK_LSIO_GPIO3_IO00                           IMX8DXL_SPI1_SCK                      4
#define IMX8DXL_SPI1_SDO_ADMA_I2C2_SCL                             IMX8DXL_SPI1_SDO                      2
#define IMX8DXL_SPI1_SDO_ADMA_SPI1_SDO                             IMX8DXL_SPI1_SDO                      3
#define IMX8DXL_SPI1_SDO_LSIO_GPIO3_IO01                           IMX8DXL_SPI1_SDO                      4
#define IMX8DXL_SPI1_SDI_ADMA_I2C3_SCL                             IMX8DXL_SPI1_SDI                      2
#define IMX8DXL_SPI1_SDI_ADMA_SPI1_SDI                             IMX8DXL_SPI1_SDI                      3
#define IMX8DXL_SPI1_SDI_LSIO_GPIO3_IO02                           IMX8DXL_SPI1_SDI                      4
#define IMX8DXL_SPI1_CS0_ADMA_I2C3_SDA                             IMX8DXL_SPI1_CS0                      2
#define IMX8DXL_SPI1_CS0_ADMA_SPI1_CS0                             IMX8DXL_SPI1_CS0                      3
#define IMX8DXL_SPI1_CS0_LSIO_GPIO3_IO03                           IMX8DXL_SPI1_CS0                      4
#define IMX8DXL_QSPI0A_DATA1_LSIO_QSPI0A_DATA1                     IMX8DXL_QSPI0A_DATA1                  0
#define IMX8DXL_QSPI0A_DATA1_LSIO_GPIO3_IO10                       IMX8DXL_QSPI0A_DATA1                  4
#define IMX8DXL_QSPI0A_DATA0_LSIO_QSPI0A_DATA0                     IMX8DXL_QSPI0A_DATA0                  0
#define IMX8DXL_QSPI0A_DATA0_LSIO_GPIO3_IO09                       IMX8DXL_QSPI0A_DATA0                  4
#define IMX8DXL_QSPI0A_DATA3_LSIO_QSPI0A_DATA3                     IMX8DXL_QSPI0A_DATA3                  0
#define IMX8DXL_QSPI0A_DATA3_LSIO_GPIO3_IO12                       IMX8DXL_QSPI0A_DATA3                  4
#define IMX8DXL_QSPI0A_DATA2_LSIO_QSPI0A_DATA2                     IMX8DXL_QSPI0A_DATA2                  0
#define IMX8DXL_QSPI0A_DATA2_LSIO_GPIO3_IO11                       IMX8DXL_QSPI0A_DATA2                  4
#define IMX8DXL_QSPI0A_SS0_B_LSIO_QSPI0A_SS0_B                     IMX8DXL_QSPI0A_SS0_B                  0
#define IMX8DXL_QSPI0A_SS0_B_LSIO_GPIO3_IO14                       IMX8DXL_QSPI0A_SS0_B                  4
#define IMX8DXL_QSPI0A_DQS_LSIO_QSPI0A_DQS                         IMX8DXL_QSPI0A_DQS                    0
#define IMX8DXL_QSPI0A_DQS_LSIO_GPIO3_IO13                         IMX8DXL_QSPI0A_DQS                    4
#define IMX8DXL_QSPI0A_SCLK_LSIO_QSPI0A_SCLK                       IMX8DXL_QSPI0A_SCLK                   0
#define IMX8DXL_QSPI0A_SCLK_LSIO_GPIO3_IO16                        IMX8DXL_QSPI0A_SCLK                   4
#define IMX8DXL_QSPI0B_SCLK_LSIO_QSPI0B_SCLK                       IMX8DXL_QSPI0B_SCLK                   0
#define IMX8DXL_QSPI0B_SCLK_LSIO_GPIO3_IO17                        IMX8DXL_QSPI0B_SCLK                   4
#define IMX8DXL_QSPI0B_DQS_LSIO_QSPI0B_DQS                         IMX8DXL_QSPI0B_DQS                    0
#define IMX8DXL_QSPI0B_DQS_LSIO_GPIO3_IO22                         IMX8DXL_QSPI0B_DQS                    4
#define IMX8DXL_QSPI0B_DATA1_LSIO_QSPI0B_DATA1                     IMX8DXL_QSPI0B_DATA1                  0
#define IMX8DXL_QSPI0B_DATA1_LSIO_GPIO3_IO19                       IMX8DXL_QSPI0B_DATA1                  4
#define IMX8DXL_QSPI0B_DATA0_LSIO_QSPI0B_DATA0                     IMX8DXL_QSPI0B_DATA0                  0
#define IMX8DXL_QSPI0B_DATA0_LSIO_GPIO3_IO18                       IMX8DXL_QSPI0B_DATA0                  4
#define IMX8DXL_QSPI0B_DATA3_LSIO_QSPI0B_DATA3                     IMX8DXL_QSPI0B_DATA3                  0
#define IMX8DXL_QSPI0B_DATA3_LSIO_GPIO3_IO21                       IMX8DXL_QSPI0B_DATA3                  4
#define IMX8DXL_QSPI0B_DATA2_LSIO_QSPI0B_DATA2                     IMX8DXL_QSPI0B_DATA2                  0
#define IMX8DXL_QSPI0B_DATA2_LSIO_GPIO3_IO20                       IMX8DXL_QSPI0B_DATA2                  4
#define IMX8DXL_QSPI0B_SS0_B_LSIO_QSPI0B_SS0_B                     IMX8DXL_QSPI0B_SS0_B                  0
#define IMX8DXL_QSPI0B_SS0_B_LSIO_GPIO3_IO23                       IMX8DXL_QSPI0B_SS0_B                  4
#define IMX8DXL_QSPI0B_SS0_B_LSIO_QSPI0A_SS1_B                     IMX8DXL_QSPI0B_SS0_B                  5

#define IMX8DXL_COMP_CTL_GPIO_1V8_3V3_PCIESEP_PAD          IMX8DXL_COMP_CTL_GPIO_1V8_3V3_PCIESEP         0
#define IMX8DXL_COMP_CTL_GPIO_3V3_USB3IO_PAD               IMX8DXL_COMP_CTL_GPIO_3V3_USB3IO              0
#define IMX8DXL_COMP_CTL_GPIO_1V8_3V3_SD1FIX0_PAD          IMX8DXL_COMP_CTL_GPIO_1V8_3V3_SD1FIX0         0
#define IMX8DXL_COMP_CTL_GPIO_1V8_3V3_VSELSEP_PAD          IMX8DXL_COMP_CTL_GPIO_1V8_3V3_VSELSEP         0
#define IMX8DXL_COMP_CTL_GPIO_1V8_3V3_ENET_ENETB0_PAD      IMX8DXL_COMP_CTL_GPIO_1V8_3V3_ENET_ENETB0     0
#define IMX8DXL_COMP_CTL_GPIO_1V8_3V3_ENET_ENETB1_PAD      IMX8DXL_COMP_CTL_GPIO_1V8_3V3_ENET_ENETB1     0
#define IMX8DXL_COMP_CTL_GPIO_1V8_3V3_GPIOCT_PAD           IMX8DXL_COMP_CTL_GPIO_1V8_3V3_GPIOCT          0
#define IMX8DXL_COMP_CTL_GPIO_1V8_3V3_GPIORHB_PAD          IMX8DXL_COMP_CTL_GPIO_1V8_3V3_GPIORHB         0
#define IMX8DXL_COMP_CTL_GPIO_1V8_3V3_GPIORHK_PAD          IMX8DXL_COMP_CTL_GPIO_1V8_3V3_GPIORHK         0
#define IMX8DXL_COMP_CTL_GPIO_1V8_3V3_GPIORHT_PAD          IMX8DXL_COMP_CTL_GPIO_1V8_3V3_GPIORHT         0
#define IMX8DXL_COMP_CTL_GPIO_1V8_3V3_GPIOLH_PAD           IMX8DXL_COMP_CTL_GPIO_1V8_3V3_GPIOLH          0
#define IMX8DXL_COMP_CTL_GPIO_1V8_3V3_GPIORHD_PAD          IMX8DXL_COMP_CTL_GPIO_1V8_3V3_GPIORHD         0
#define IMX8DXL_COMP_CTL_GPIO_1V8_3V3_QSPI0A_PAD           IMX8DXL_COMP_CTL_GPIO_1V8_3V3_QSPI0A          0
#define IMX8DXL_COMP_CTL_GPIO_1V8_3V3_QSPI0B_PAD           IMX8DXL_COMP_CTL_GPIO_1V8_3V3_QSPI0B          0

#endif
