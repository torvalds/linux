// SPDX-License-Identifier: GPL-2.0 OR MIT
/*
 * Copyright (C) 2022 StarFive Technology Co., Ltd.
 */

#ifndef _DT_BINDINGS_PINCTRL_STARFIVE_H
#define _DT_BINDINGS_PINCTRL_STARFIVE_H

/************************aon_iomux***************************/
//aon_iomux pin
#define	PAD_TESTEN      0
#define	PAD_RGPIO0      1
#define	PAD_RGPIO1      2
#define	PAD_RGPIO2      3
#define	PAD_RGPIO3      4
#define	PAD_RSTN        5
#define	PAD_GMAC0_MDC	6
#define	PAD_GMAC0_MDIO	7
#define	PAD_GMAC0_RXD0	8
#define	PAD_GMAC0_RXD1	9
#define	PAD_GMAC0_RXD2	10
#define	PAD_GMAC0_RXD3	11
#define	PAD_GMAC0_RXDV	12
#define	PAD_GMAC0_RXC	13
#define	PAD_GMAC0_TXD0	14
#define	PAD_GMAC0_TXD1	15
#define	PAD_GMAC0_TXD2	16
#define	PAD_GMAC0_TXD3	17
#define	PAD_GMAC0_TXEN	18
#define	PAD_GMAC0_TXC	19

//<fmux_idx.h>
//aon_iomux dout
#define GPO_AON_IOMUX_U0_AON_CRG_CLK_32K_OUT    2
#define GPO_AON_IOMUX_U0_PWM_8CH_PTC_PWM_4      3
#define GPO_AON_IOMUX_U0_PWM_8CH_PTC_PWM_5      4
#define GPO_AON_IOMUX_U0_PWM_8CH_PTC_PWM_6      5
#define GPO_AON_IOMUX_U0_PWM_8CH_PTC_PWM_7      6
#define GPO_AON_IOMUX_U0_SYS_CRG_CLK_GCLK0      7
#define GPO_AON_IOMUX_U0_SYS_CRG_CLK_GCLK1      8
#define GPO_AON_IOMUX_U0_SYS_CRG_CLK_GCLK2      9
//aon_iomux doen
#define GPEN_AON_IOMUX_U0_PWM_8CH_PTC_OE_N_4    2
#define GPEN_AON_IOMUX_U0_PWM_8CH_PTC_OE_N_5    3
#define GPEN_AON_IOMUX_U0_PWM_8CH_PTC_OE_N_6    4
#define GPEN_AON_IOMUX_U0_PWM_8CH_PTC_OE_N_7    5
//aon_iomux gin
#define GPI_AON_IOMUX_U0_PMU_IO_EVENT_STUB_GPIO_WAKEUP_0    0
#define GPI_AON_IOMUX_U0_PMU_IO_EVENT_STUB_GPIO_WAKEUP_1    1
#define GPI_AON_IOMUX_U0_PMU_IO_EVENT_STUB_GPIO_WAKEUP_2    2
#define GPI_AON_IOMUX_U0_PMU_IO_EVENT_STUB_GPIO_WAKEUP_3    3


//===============================GPIO_OUT_SELECT=======================================
// gpio_out config:
// every define below is a couple of signal and signal idx
// use macros in corresponding syscfg_macro.h and idx defined below to config gpio_out
//e.g. SET_AON_IOMUX_GPO[gpio_num]_DOUT_CFG([signal])
//e.g. SET_AON_IOMUX_GPO0_DOUT_CFG(__LOW)
//=====================================================================================
#define __LOW                                        0
#define __HIGH                                       1
#define U0_AON_CRG_CLK_32K_OUT                       2
#define U0_PWM_8CH_PTC_PWM_4                         3
#define U0_PWM_8CH_PTC_PWM_5                         4
#define U0_PWM_8CH_PTC_PWM_6                         5
#define U0_PWM_8CH_PTC_PWM_7                         6
#define U0_SYS_CRG_CLK_GCLK0                         7
#define U0_SYS_CRG_CLK_GCLK1                         8
#define U0_SYS_CRG_CLK_GCLK2                         9

//===============================GPIO_OEN_SELECT=======================================
// gpio_oen config:
// every define below is a couple of signal and signal idx
// use macros in corresponding syscfg_macro.h and idx defined below to config gpio_oen
//e.g. SET_AON_IOMUX_GPO[gpio_num]_DOEN_CFG([signal])
//e.g. SET_AON_IOMUX_GPO0_DOEN_CFG(__LOW)
//=====================================================================================
#define __LOW                                        0
#define __HIGH                                       1
#define U0_PWM_8CH_PTC_OE_N_4                        2
#define U0_PWM_8CH_PTC_OE_N_5                        3
#define U0_PWM_8CH_PTC_OE_N_6                        4
#define U0_PWM_8CH_PTC_OE_N_7                        5


//aon_iomux gmac0 syscon
#define AON_IOMUX_CFG__SAIF__SYSCFG_88_ADDR                (0x58U)
#define AON_IOMUX_CFG__SAIF__SYSCFG_92_ADDR                (0x5cU)
#define AON_IOMUX_CFG__SAIF__SYSCFG_96_ADDR                (0x60U)
#define AON_IOMUX_CFG__SAIF__SYSCFG_100_ADDR               (0x64U)
#define AON_IOMUX_CFG__SAIF__SYSCFG_104_ADDR               (0x68U)
#define AON_IOMUX_CFG__SAIF__SYSCFG_108_ADDR               (0x6cU)
#define AON_IOMUX_CFG__SAIF__SYSCFG_112_ADDR               (0x70U)
#define AON_IOMUX_CFG__SAIF__SYSCFG_116_ADDR               (0x74U)
#define AON_IOMUX_CFG__SAIF__SYSCFG_120_ADDR               (0x78U)
#define AON_IOMUX_CFG__SAIF__SYSCFG_124_ADDR               (0x7cU)
#define AON_IOMUX_CFG__SAIF__SYSCFG_128_ADDR               (0x80U)
#define AON_IOMUX_CFG__SAIF__SYSCFG_132_ADDR               (0x84U)
#define AON_IOMUX_CFG__SAIF__SYSCFG_136_ADDR               (0x88U)
#define AON_IOMUX_CFG__SAIF__SYSCFG_140_ADDR               (0x8cU)

#define PADCFG_PAD_GMAC0_MDC_SYSCON     AON_IOMUX_CFG__SAIF__SYSCFG_88_ADDR
#define PADCFG_PAD_GMAC0_MDIO_SYSCON    AON_IOMUX_CFG__SAIF__SYSCFG_92_ADDR
#define PADCFG_PAD_GMAC0_RXD0_SYSCON    AON_IOMUX_CFG__SAIF__SYSCFG_96_ADDR
#define PADCFG_PAD_GMAC0_RXD1_SYSCON    AON_IOMUX_CFG__SAIF__SYSCFG_100_ADDR
#define PADCFG_PAD_GMAC0_RXD2_SYSCON    AON_IOMUX_CFG__SAIF__SYSCFG_104_ADDR
#define PADCFG_PAD_GMAC0_RXD3_SYSCON    AON_IOMUX_CFG__SAIF__SYSCFG_108_ADDR
#define PADCFG_PAD_GMAC0_RXDV_SYSCON    AON_IOMUX_CFG__SAIF__SYSCFG_112_ADDR
#define PADCFG_PAD_GMAC0_RXC_SYSCON     AON_IOMUX_CFG__SAIF__SYSCFG_116_ADDR
#define PADCFG_PAD_GMAC0_TXD0_SYSCON    AON_IOMUX_CFG__SAIF__SYSCFG_120_ADDR
#define PADCFG_PAD_GMAC0_TXD1_SYSCON    AON_IOMUX_CFG__SAIF__SYSCFG_124_ADDR
#define PADCFG_PAD_GMAC0_TXD2_SYSCON    AON_IOMUX_CFG__SAIF__SYSCFG_128_ADDR
#define PADCFG_PAD_GMAC0_TXD3_SYSCON    AON_IOMUX_CFG__SAIF__SYSCFG_132_ADDR
#define PADCFG_PAD_GMAC0_TXEN_SYSCON    AON_IOMUX_CFG__SAIF__SYSCFG_136_ADDR
#define PADCFG_PAD_GMAC0_TXC_SYSCON     AON_IOMUX_CFG__SAIF__SYSCFG_140_ADDR

//aon_iomux func sel
#define AON_IOMUX_CFGSAIF__SYSCFG_144_ADDR                 (0x90U)
#define PAD_GMAC0_RXC_FUNC_SEL_SHIFT                       0x0U
#define PAD_GMAC0_RXC_FUNC_SEL_MASK                        0x3U

#define PAD_GMAC0_RXC_FUNC_SEL              \
        AON_IOMUX_CFGSAIF__SYSCFG_144_ADDR  \
        PAD_GMAC0_RXC_FUNC_SEL_SHIFT        \
        PAD_GMAC0_RXC_FUNC_SEL_MASK
/************************aon_iomux***************************/

/************************sys_iomux***************************/
//sys_iomux pin
#define	PAD_GPIO0       0
#define	PAD_GPIO1       1
#define	PAD_GPIO2       2
#define	PAD_GPIO3       3
#define	PAD_GPIO4       4
#define	PAD_GPIO5       5
#define	PAD_GPIO6       6
#define	PAD_GPIO7       7
#define	PAD_GPIO8       8
#define	PAD_GPIO9       9
#define	PAD_GPIO10      10
#define	PAD_GPIO11      11
#define	PAD_GPIO12      12
#define	PAD_GPIO13      13
#define	PAD_GPIO14      14
#define	PAD_GPIO15      15
#define	PAD_GPIO16      16
#define	PAD_GPIO17      17
#define	PAD_GPIO18      18
#define	PAD_GPIO19      19
#define	PAD_GPIO20      20
#define	PAD_GPIO21      21
#define	PAD_GPIO22      22
#define	PAD_GPIO23      23
#define	PAD_GPIO24      24
#define	PAD_GPIO25      25
#define	PAD_GPIO26      26
#define	PAD_GPIO27      27
#define	PAD_GPIO28      28
#define	PAD_GPIO29      29
#define	PAD_GPIO30      30
#define	PAD_GPIO31      31
#define	PAD_GPIO32      32
#define	PAD_GPIO33      33
#define	PAD_GPIO34      34
#define	PAD_GPIO35      35
#define	PAD_GPIO36      36
#define	PAD_GPIO37      37
#define	PAD_GPIO38      38
#define	PAD_GPIO39      39
#define	PAD_GPIO40      40
#define	PAD_GPIO41      41
#define	PAD_GPIO42      42
#define	PAD_GPIO43      43
#define	PAD_GPIO44      44
#define	PAD_GPIO45      45
#define	PAD_GPIO46      46
#define	PAD_GPIO47      47
#define	PAD_GPIO48      48
#define	PAD_GPIO49      49
#define	PAD_GPIO50      50
#define	PAD_GPIO51      51
#define	PAD_GPIO52      52
#define	PAD_GPIO53      53
#define	PAD_GPIO54      54
#define	PAD_GPIO55      55
#define	PAD_GPIO56      56
#define	PAD_GPIO57      57
#define	PAD_GPIO58      58
#define	PAD_GPIO59      59
#define	PAD_GPIO60      60
#define	PAD_GPIO61      61
#define	PAD_GPIO62      62
#define	PAD_GPIO63      63
#define	PAD_SD0_CLK     64
#define	PAD_SD0_CMD     65
#define	PAD_SD0_DATA0   66
#define	PAD_SD0_DATA1   67
#define	PAD_SD0_DATA2   68
#define	PAD_SD0_DATA3   69
#define	PAD_SD0_DATA4   70
#define	PAD_SD0_DATA5   71
#define	PAD_SD0_DATA6   72
#define	PAD_SD0_DATA7   73
#define	PAD_SD0_STRB    74
#define	PAD_GMAC1_MDC   75
#define	PAD_GMAC1_MDIO  76
#define	PAD_GMAC1_RXD0  77
#define	PAD_GMAC1_RXD1  78
#define	PAD_GMAC1_RXD2  79
#define	PAD_GMAC1_RXD3  80
#define	PAD_GMAC1_RXDV  81
#define	PAD_GMAC1_RXC   82
#define	PAD_GMAC1_TXD0  83
#define	PAD_GMAC1_TXD1  84
#define	PAD_GMAC1_TXD2  85
#define	PAD_GMAC1_TXD3  86
#define	PAD_GMAC1_TXEN  87
#define	PAD_GMAC1_TXC   88
#define	PAD_QSPI_SCLK   89
#define	PAD_QSPI_CSn0   90
#define	PAD_QSPI_DATA0  91
#define	PAD_QSPI_DATA1  92
#define	PAD_QSPI_DATA2  93
#define	PAD_QSPI_DATA3  94


//<fmux_idx.h>
//oen and out idx is for chosen, in idx is for reg offset
#define GPO_SYS_IOMUX_U0_WAVE511_O_UART_TXSOUT                      2
#define GPO_SYS_IOMUX_U0_CAN_CTRL_STBY                              3
#define GPO_SYS_IOMUX_U0_CAN_CTRL_TST_NEXT_BIT                      4
#define GPO_SYS_IOMUX_U0_CAN_CTRL_TST_SAMPLE_POINT                  5
#define GPO_SYS_IOMUX_U0_CAN_CTRL_TXD                               6
#define GPO_SYS_IOMUX_U0_CDN_USB_DRIVE_VBUS_IO                      7
#define GPO_SYS_IOMUX_U0_CDNS_QSPI_CSN1                             8
#define GPO_SYS_IOMUX_U0_CDNS_SPDIF_SPDIFO                          9
#define GPO_SYS_IOMUX_U0_DOM_VOUT_TOP_U0_HDMI_TX_PIN_CEC_SDA_OUT    10
#define GPO_SYS_IOMUX_U0_DOM_VOUT_TOP_U0_HDMI_TX_PIN_DDC_SCL_OUT    11
#define GPO_SYS_IOMUX_U0_DOM_VOUT_TOP_U0_HDMI_TX_PIN_DDC_SDA_OUT    12
#define GPO_SYS_IOMUX_U0_DSKIT_WDT_WDOGRES                          13
#define GPO_SYS_IOMUX_U0_DW_I2C_IC_CLK_OUT_A                        14
#define GPO_SYS_IOMUX_U0_DW_I2C_IC_DATA_OUT_A                       15
#define GPO_SYS_IOMUX_U0_DW_SDIO_BACK_END_POWER                     16
#define GPO_SYS_IOMUX_U0_DW_SDIO_CARD_POWER_EN                      17
#define GPO_SYS_IOMUX_U0_DW_SDIO_CCMD_OD_PULLUP_EN_N                18
#define GPO_SYS_IOMUX_U0_DW_SDIO_RST_N                              19
#define GPO_SYS_IOMUX_U0_DW_UART_SOUT                               20
#define GPO_SYS_IOMUX_U0_HIFI4_JTDO                                 21
#define GPO_SYS_IOMUX_U0_JTAG_CERTIFICATION_TDO                     22
#define GPO_SYS_IOMUX_U0_PDM_4MIC_DMIC_MCLK                         23
#define GPO_SYS_IOMUX_U0_PWM_8CH_PTC_PWM_0                          24
#define GPO_SYS_IOMUX_U0_PWM_8CH_PTC_PWM_1                          25
#define GPO_SYS_IOMUX_U0_PWM_8CH_PTC_PWM_2                          26
#define GPO_SYS_IOMUX_U0_PWM_8CH_PTC_PWM_3                          27
#define GPO_SYS_IOMUX_U0_PWMDAC_PWMDAC_LEFT_OUTPUT                  28
#define GPO_SYS_IOMUX_U0_PWMDAC_PWMDAC_RIGHT_OUTPUT                 29
#define GPO_SYS_IOMUX_U0_SSP_SPI_SSPCLKOUT                          30
#define GPO_SYS_IOMUX_U0_SSP_SPI_SSPFSSOUT                          31
#define GPO_SYS_IOMUX_U0_SSP_SPI_SSPTXD                             32
#define GPO_SYS_IOMUX_U0_SYS_CRG_CLK_GMAC_PHY                       33
#define GPO_SYS_IOMUX_U0_SYS_CRG_I2SRX_BCLK_MST                     34
#define GPO_SYS_IOMUX_U0_SYS_CRG_I2SRX_LRCK_MST                     35
#define GPO_SYS_IOMUX_U0_SYS_CRG_I2STX_BCLK_MST                     36
#define GPO_SYS_IOMUX_U0_SYS_CRG_I2STX_LRCK_MST                     37
#define GPO_SYS_IOMUX_U0_SYS_CRG_MCLK_OUT                           38
#define GPO_SYS_IOMUX_U0_SYS_CRG_TDM_CLK_MST                        39
#define GPO_SYS_IOMUX_U0_TDM16SLOT_PCM_SYNCOUT                      40
#define GPO_SYS_IOMUX_U0_TDM16SLOT_PCM_TXD                          41
#define GPO_SYS_IOMUX_U0_U7MC_SFT7110_TRACE_COM_PIB_TDATA_0         42
#define GPO_SYS_IOMUX_U0_U7MC_SFT7110_TRACE_COM_PIB_TDATA_1         43
#define GPO_SYS_IOMUX_U0_U7MC_SFT7110_TRACE_COM_PIB_TDATA_2         44
#define GPO_SYS_IOMUX_U0_U7MC_SFT7110_TRACE_COM_PIB_TDATA_3         45
#define GPO_SYS_IOMUX_U0_U7MC_SFT7110_TRACE_COM_PIB_TREF            46
#define GPO_SYS_IOMUX_U1_CAN_CTRL_STBY                              47
#define GPO_SYS_IOMUX_U1_CAN_CTRL_TST_NEXT_BIT                      48
#define GPO_SYS_IOMUX_U1_CAN_CTRL_TST_SAMPLE_POINT                  49
#define GPO_SYS_IOMUX_U1_CAN_CTRL_TXD                               50
#define GPO_SYS_IOMUX_U1_DW_I2C_IC_CLK_OUT_A                        51
#define GPO_SYS_IOMUX_U1_DW_I2C_IC_DATA_OUT_A                       52
#define GPO_SYS_IOMUX_U1_DW_SDIO_BACK_END_POWER                     53
#define GPO_SYS_IOMUX_U1_DW_SDIO_CARD_POWER_EN                      54
#define GPO_SYS_IOMUX_U1_DW_SDIO_CCLK_OUT                           55
#define GPO_SYS_IOMUX_U1_DW_SDIO_CCMD_OD_PULLUP_EN_N                56
#define GPO_SYS_IOMUX_U1_DW_SDIO_CCMD_OUT                           57
#define GPO_SYS_IOMUX_U1_DW_SDIO_CDATA_OUT_0                        58
#define GPO_SYS_IOMUX_U1_DW_SDIO_CDATA_OUT_1                        59
#define GPO_SYS_IOMUX_U1_DW_SDIO_CDATA_OUT_2                        60
#define GPO_SYS_IOMUX_U1_DW_SDIO_CDATA_OUT_3                        61
#define GPO_SYS_IOMUX_U1_DW_SDIO_CDATA_OUT_4                        62
#define GPO_SYS_IOMUX_U1_DW_SDIO_CDATA_OUT_5                        63
#define GPO_SYS_IOMUX_U1_DW_SDIO_CDATA_OUT_6                        64
#define GPO_SYS_IOMUX_U1_DW_SDIO_CDATA_OUT_7                        65
#define GPO_SYS_IOMUX_U1_DW_SDIO_RST_N                              66
#define GPO_SYS_IOMUX_U1_DW_UART_RTS_N                              67
#define GPO_SYS_IOMUX_U1_DW_UART_SOUT                               68
#define GPO_SYS_IOMUX_U1_I2STX_4CH_SDO0                             69
#define GPO_SYS_IOMUX_U1_I2STX_4CH_SDO1                             70
#define GPO_SYS_IOMUX_U1_I2STX_4CH_SDO2                             71
#define GPO_SYS_IOMUX_U1_I2STX_4CH_SDO3                             72
#define GPO_SYS_IOMUX_U1_SSP_SPI_SSPCLKOUT                          73
#define GPO_SYS_IOMUX_U1_SSP_SPI_SSPFSSOUT                          74
#define GPO_SYS_IOMUX_U1_SSP_SPI_SSPTXD                             75
#define GPO_SYS_IOMUX_U2_DW_I2C_IC_CLK_OUT_A                        76
#define GPO_SYS_IOMUX_U2_DW_I2C_IC_DATA_OUT_A                       77
#define GPO_SYS_IOMUX_U2_DW_UART_RTS_N                              78
#define GPO_SYS_IOMUX_U2_DW_UART_SOUT                               79
#define GPO_SYS_IOMUX_U2_SSP_SPI_SSPCLKOUT                          80
#define GPO_SYS_IOMUX_U2_SSP_SPI_SSPFSSOUT                          81
#define GPO_SYS_IOMUX_U2_SSP_SPI_SSPTXD                             82
#define GPO_SYS_IOMUX_U3_DW_I2C_IC_CLK_OUT_A                        83
#define GPO_SYS_IOMUX_U3_DW_I2C_IC_DATA_OUT_A                       84
#define GPO_SYS_IOMUX_U3_DW_UART_SOUT                               85
#define GPO_SYS_IOMUX_U3_SSP_SPI_SSPCLKOUT                          86
#define GPO_SYS_IOMUX_U3_SSP_SPI_SSPFSSOUT                          87
#define GPO_SYS_IOMUX_U3_SSP_SPI_SSPTXD                             88
#define GPO_SYS_IOMUX_U4_DW_I2C_IC_CLK_OUT_A                        89
#define GPO_SYS_IOMUX_U4_DW_I2C_IC_DATA_OUT_A                       90
#define GPO_SYS_IOMUX_U4_DW_UART_RTS_N                              91
#define GPO_SYS_IOMUX_U4_DW_UART_SOUT                               92
#define GPO_SYS_IOMUX_U4_SSP_SPI_SSPCLKOUT                          93
#define GPO_SYS_IOMUX_U4_SSP_SPI_SSPFSSOUT                          94
#define GPO_SYS_IOMUX_U4_SSP_SPI_SSPTXD                             95
#define GPO_SYS_IOMUX_U5_DW_I2C_IC_CLK_OUT_A                        96
#define GPO_SYS_IOMUX_U5_DW_I2C_IC_DATA_OUT_A                       97
#define GPO_SYS_IOMUX_U5_DW_UART_RTS_N                              98
#define GPO_SYS_IOMUX_U5_DW_UART_SOUT                               99
#define GPO_SYS_IOMUX_U5_SSP_SPI_SSPCLKOUT                          100
#define GPO_SYS_IOMUX_U5_SSP_SPI_SSPFSSOUT                          101
#define GPO_SYS_IOMUX_U5_SSP_SPI_SSPTXD                             102
#define GPO_SYS_IOMUX_U6_DW_I2C_IC_CLK_OUT_A                        103
#define GPO_SYS_IOMUX_U6_DW_I2C_IC_DATA_OUT_A                       104
#define GPO_SYS_IOMUX_U6_SSP_SPI_SSPCLKOUT                          105
#define GPO_SYS_IOMUX_U6_SSP_SPI_SSPFSSOUT                          106
#define GPO_SYS_IOMUX_U6_SSP_SPI_SSPTXD                             107

#define GPEN_SYS_IOMUX_U0_DOM_VOUT_TOP_U0_HDMI_TX_PIN_CEC_SDA_OEN   2
#define GPEN_SYS_IOMUX_U0_DOM_VOUT_TOP_U0_HDMI_TX_PIN_DDC_SCL_OEN   3
#define GPEN_SYS_IOMUX_U0_DOM_VOUT_TOP_U0_HDMI_TX_PIN_DDC_SDA_OEN   4
#define GPEN_SYS_IOMUX_U0_DW_I2C_IC_CLK_OE                          5
#define GPEN_SYS_IOMUX_U0_DW_I2C_IC_DATA_OE                         6
#define GPEN_SYS_IOMUX_U0_HIFI4_JTDOEN                              7
#define GPEN_SYS_IOMUX_U0_JTAG_CERTIFICATION_TDO_OE                 8
#define GPEN_SYS_IOMUX_U0_PWM_8CH_PTC_OE_N_0                        9
#define GPEN_SYS_IOMUX_U0_PWM_8CH_PTC_OE_N_1                        10
#define GPEN_SYS_IOMUX_U0_PWM_8CH_PTC_OE_N_2                        11
#define GPEN_SYS_IOMUX_U0_PWM_8CH_PTC_OE_N_3                        12
#define GPEN_SYS_IOMUX_U0_SSP_SPI_NSSPCTLOE                         13
#define GPEN_SYS_IOMUX_U0_SSP_SPI_NSSPOE                            14
#define GPEN_SYS_IOMUX_U0_TDM16SLOT_NPCM_SYNCOE                     15
#define GPEN_SYS_IOMUX_U0_TDM16SLOT_NPCM_TXDOE                      16
#define GPEN_SYS_IOMUX_U1_DW_I2C_IC_CLK_OE                          17
#define GPEN_SYS_IOMUX_U1_DW_I2C_IC_DATA_OE                         18
#define GPEN_SYS_IOMUX_U1_DW_SDIO_CCMD_OUT_EN                       19
#define GPEN_SYS_IOMUX_U1_DW_SDIO_CDATA_OUT_EN_0                    20
#define GPEN_SYS_IOMUX_U1_DW_SDIO_CDATA_OUT_EN_1                    21
#define GPEN_SYS_IOMUX_U1_DW_SDIO_CDATA_OUT_EN_2                    22
#define GPEN_SYS_IOMUX_U1_DW_SDIO_CDATA_OUT_EN_3                    23
#define GPEN_SYS_IOMUX_U1_DW_SDIO_CDATA_OUT_EN_4                    24
#define GPEN_SYS_IOMUX_U1_DW_SDIO_CDATA_OUT_EN_5                    25
#define GPEN_SYS_IOMUX_U1_DW_SDIO_CDATA_OUT_EN_6                    26
#define GPEN_SYS_IOMUX_U1_DW_SDIO_CDATA_OUT_EN_7                    27
#define GPEN_SYS_IOMUX_U1_SSP_SPI_NSSPCTLOE                         28
#define GPEN_SYS_IOMUX_U1_SSP_SPI_NSSPOE                            29
#define GPEN_SYS_IOMUX_U2_DW_I2C_IC_CLK_OE                          30
#define GPEN_SYS_IOMUX_U2_DW_I2C_IC_DATA_OE                         31
#define GPEN_SYS_IOMUX_U2_SSP_SPI_NSSPCTLOE                         32
#define GPEN_SYS_IOMUX_U2_SSP_SPI_NSSPOE                            33
#define GPEN_SYS_IOMUX_U3_DW_I2C_IC_CLK_OE                          34
#define GPEN_SYS_IOMUX_U3_DW_I2C_IC_DATA_OE                         35
#define GPEN_SYS_IOMUX_U3_SSP_SPI_NSSPCTLOE                         36
#define GPEN_SYS_IOMUX_U3_SSP_SPI_NSSPOE                            37
#define GPEN_SYS_IOMUX_U4_DW_I2C_IC_CLK_OE                          38
#define GPEN_SYS_IOMUX_U4_DW_I2C_IC_DATA_OE                         39
#define GPEN_SYS_IOMUX_U4_SSP_SPI_NSSPCTLOE                         40
#define GPEN_SYS_IOMUX_U4_SSP_SPI_NSSPOE                            41
#define GPEN_SYS_IOMUX_U5_DW_I2C_IC_CLK_OE                          42
#define GPEN_SYS_IOMUX_U5_DW_I2C_IC_DATA_OE                         43
#define GPEN_SYS_IOMUX_U5_SSP_SPI_NSSPCTLOE                         44
#define GPEN_SYS_IOMUX_U5_SSP_SPI_NSSPOE                            45
#define GPEN_SYS_IOMUX_U6_DW_I2C_IC_CLK_OE                          46
#define GPEN_SYS_IOMUX_U6_DW_I2C_IC_DATA_OE                         47
#define GPEN_SYS_IOMUX_U6_SSP_SPI_NSSPCTLOE                         48
#define GPEN_SYS_IOMUX_U6_SSP_SPI_NSSPOE                            49

#define GPI_SYS_IOMUX_U0_WAVE511_I_UART_RXSIN                       0
#define GPI_SYS_IOMUX_U0_CAN_CTRL_RXD                               1
#define GPI_SYS_IOMUX_U0_CDN_USB_OVERCURRENT_N_IO                   2
#define GPI_SYS_IOMUX_U0_CDNS_SPDIF_SPDIFI                          3
#define GPI_SYS_IOMUX_U0_CLKRST_SRC_BYPASS_JTAG_TRSTN               4
#define GPI_SYS_IOMUX_U0_DOM_VOUT_TOP_U0_HDMI_TX_PIN_CEC_SDA_IN     5
#define GPI_SYS_IOMUX_U0_DOM_VOUT_TOP_U0_HDMI_TX_PIN_DDC_SCL_IN     6
#define GPI_SYS_IOMUX_U0_DOM_VOUT_TOP_U0_HDMI_TX_PIN_DDC_SDA_IN     7
#define GPI_SYS_IOMUX_U0_DOM_VOUT_TOP_U0_HDMI_TX_PIN_HPD            8
#define GPI_SYS_IOMUX_U0_DW_I2C_IC_CLK_IN_A                         9
#define GPI_SYS_IOMUX_U0_DW_I2C_IC_DATA_IN_A                        10
#define GPI_SYS_IOMUX_U0_DW_SDIO_CARD_DETECT_N                      11
#define GPI_SYS_IOMUX_U0_DW_SDIO_CARD_INT_N                         12
#define GPI_SYS_IOMUX_U0_DW_SDIO_CARD_WRITE_PRT                     13
#define GPI_SYS_IOMUX_U0_DW_UART_SIN                                14
#define GPI_SYS_IOMUX_U0_HIFI4_JTCK                                 15
#define GPI_SYS_IOMUX_U0_HIFI4_JTDI                                 16
#define GPI_SYS_IOMUX_U0_HIFI4_JTMS                                 17
#define GPI_SYS_IOMUX_U0_HIFI4_JTRSTN                               18
#define GPI_SYS_IOMUX_U0_JTAG_CERTIFICATION_TDI                     19
#define GPI_SYS_IOMUX_U0_JTAG_CERTIFICATION_TMS                     20
#define GPI_SYS_IOMUX_U0_PDM_4MIC_DMIC0_DIN                         21
#define GPI_SYS_IOMUX_U0_PDM_4MIC_DMIC1_DIN                         22
#define GPI_SYS_IOMUX_U0_SAIF_AUDIO_SDIN_MUX_I2SRX_EXT_SDIN0        23
#define GPI_SYS_IOMUX_U0_SAIF_AUDIO_SDIN_MUX_I2SRX_EXT_SDIN1        24
#define GPI_SYS_IOMUX_U0_SAIF_AUDIO_SDIN_MUX_I2SRX_EXT_SDIN2        25
#define GPI_SYS_IOMUX_U0_SSP_SPI_SSPCLKIN                           26
#define GPI_SYS_IOMUX_U0_SSP_SPI_SSPFSSIN                           27
#define GPI_SYS_IOMUX_U0_SSP_SPI_SSPRXD                             28
#define GPI_SYS_IOMUX_U0_SYS_CRG_CLK_JTAG_TCK                       29
#define GPI_SYS_IOMUX_U0_SYS_CRG_EXT_MCLK                           30
#define GPI_SYS_IOMUX_U0_SYS_CRG_I2SRX_BCLK_SLV                     31
#define GPI_SYS_IOMUX_U0_SYS_CRG_I2SRX_LRCK_SLV                     32
#define GPI_SYS_IOMUX_U0_SYS_CRG_I2STX_BCLK_SLV                     33
#define GPI_SYS_IOMUX_U0_SYS_CRG_I2STX_LRCK_SLV                     34
#define GPI_SYS_IOMUX_U0_SYS_CRG_TDM_CLK_SLV                        35
#define GPI_SYS_IOMUX_U0_TDM16SLOT_PCM_RXD                          36
#define GPI_SYS_IOMUX_U0_TDM16SLOT_PCM_SYNCIN                       37
#define GPI_SYS_IOMUX_U1_CAN_CTRL_RXD                               38
#define GPI_SYS_IOMUX_U1_DW_I2C_IC_CLK_IN_A                         39
#define GPI_SYS_IOMUX_U1_DW_I2C_IC_DATA_IN_A                        40
#define GPI_SYS_IOMUX_U1_DW_SDIO_CARD_DETECT_N                      41
#define GPI_SYS_IOMUX_U1_DW_SDIO_CARD_INT_N                         42
#define GPI_SYS_IOMUX_U1_DW_SDIO_CARD_WRITE_PRT                     43
#define GPI_SYS_IOMUX_U1_DW_SDIO_CCMD_IN                            44
#define GPI_SYS_IOMUX_U1_DW_SDIO_CDATA_IN_0                         45
#define GPI_SYS_IOMUX_U1_DW_SDIO_CDATA_IN_1                         46
#define GPI_SYS_IOMUX_U1_DW_SDIO_CDATA_IN_2                         47
#define GPI_SYS_IOMUX_U1_DW_SDIO_CDATA_IN_3                         48
#define GPI_SYS_IOMUX_U1_DW_SDIO_CDATA_IN_4                         49
#define GPI_SYS_IOMUX_U1_DW_SDIO_CDATA_IN_5                         50
#define GPI_SYS_IOMUX_U1_DW_SDIO_CDATA_IN_6                         51
#define GPI_SYS_IOMUX_U1_DW_SDIO_CDATA_IN_7                         52
#define GPI_SYS_IOMUX_U1_DW_SDIO_DATA_STROBE                        53
#define GPI_SYS_IOMUX_U1_DW_UART_CTS_N                              54
#define GPI_SYS_IOMUX_U1_DW_UART_SIN                                55
#define GPI_SYS_IOMUX_U1_SSP_SPI_SSPCLKIN                           56
#define GPI_SYS_IOMUX_U1_SSP_SPI_SSPFSSIN                           57
#define GPI_SYS_IOMUX_U1_SSP_SPI_SSPRXD                             58
#define GPI_SYS_IOMUX_U2_DW_I2C_IC_CLK_IN_A                         59
#define GPI_SYS_IOMUX_U2_DW_I2C_IC_DATA_IN_A                        60
#define GPI_SYS_IOMUX_U2_DW_UART_CTS_N                              61
#define GPI_SYS_IOMUX_U2_DW_UART_SIN                                62
#define GPI_SYS_IOMUX_U2_SSP_SPI_SSPCLKIN                           63
#define GPI_SYS_IOMUX_U2_SSP_SPI_SSPFSSIN                           64
#define GPI_SYS_IOMUX_U2_SSP_SPI_SSPRXD                             65
#define GPI_SYS_IOMUX_U3_DW_I2C_IC_CLK_IN_A                         66
#define GPI_SYS_IOMUX_U3_DW_I2C_IC_DATA_IN_A                        67
#define GPI_SYS_IOMUX_U3_DW_UART_SIN                                68
#define GPI_SYS_IOMUX_U3_SSP_SPI_SSPCLKIN                           69
#define GPI_SYS_IOMUX_U3_SSP_SPI_SSPFSSIN                           70
#define GPI_SYS_IOMUX_U3_SSP_SPI_SSPRXD                             71
#define GPI_SYS_IOMUX_U4_DW_I2C_IC_CLK_IN_A                         72
#define GPI_SYS_IOMUX_U4_DW_I2C_IC_DATA_IN_A                        73
#define GPI_SYS_IOMUX_U4_DW_UART_CTS_N                              74
#define GPI_SYS_IOMUX_U4_DW_UART_SIN                                75
#define GPI_SYS_IOMUX_U4_SSP_SPI_SSPCLKIN                           76
#define GPI_SYS_IOMUX_U4_SSP_SPI_SSPFSSIN                           77
#define GPI_SYS_IOMUX_U4_SSP_SPI_SSPRXD                             78
#define GPI_SYS_IOMUX_U5_DW_I2C_IC_CLK_IN_A                         79
#define GPI_SYS_IOMUX_U5_DW_I2C_IC_DATA_IN_A                        80
#define GPI_SYS_IOMUX_U5_DW_UART_CTS_N                              81
#define GPI_SYS_IOMUX_U5_DW_UART_SIN                                82
#define GPI_SYS_IOMUX_U5_SSP_SPI_SSPCLKIN                           83
#define GPI_SYS_IOMUX_U5_SSP_SPI_SSPFSSIN                           84
#define GPI_SYS_IOMUX_U5_SSP_SPI_SSPRXD                             85
#define GPI_SYS_IOMUX_U6_DW_I2C_IC_CLK_IN_A                         86
#define GPI_SYS_IOMUX_U6_DW_I2C_IC_DATA_IN_A                        87
#define GPI_SYS_IOMUX_U6_SSP_SPI_SSPCLKIN                           88
#define GPI_SYS_IOMUX_U6_SSP_SPI_SSPFSSIN                           89
#define GPI_SYS_IOMUX_U6_SSP_SPI_SSPRXD                             90



//gpo(n)_dout signal pool
#define GPO_LOW 	0
#define GPO_HIGH 	1
#define GPO_CAN0_CTRL_STBY              \
        GPO_SYS_IOMUX_U0_CAN_CTRL_STBY
#define GPO_CAN0_CTRL_TST_NEXT_BIT      \
        GPO_SYS_IOMUX_U0_CAN_CTRL_TST_NEXT_BIT
#define GPO_CAN0_CTRL_TST_SAMPLE_POINT  \
        GPO_SYS_IOMUX_U0_CAN_CTRL_TST_SAMPLE_POINT
#define GPO_CAN0_CTRL_TXD               \
        GPO_SYS_IOMUX_U0_CAN_CTRL_TXD
#define GPO_CAN1_CTRL_STBY              \
        GPO_SYS_IOMUX_U1_CAN_CTRL_STBY
#define GPO_CAN1_CTRL_TST_NEXT_BIT      \
        GPO_SYS_IOMUX_U1_CAN_CTRL_TST_NEXT_BIT
#define GPO_CAN1_CTRL_TST_SAMPLE_POINT  \
        GPO_SYS_IOMUX_U1_CAN_CTRL_TST_SAMPLE_POINT
#define GPO_CAN1_CTRL_TXD               \
        GPO_SYS_IOMUX_U1_CAN_CTRL_TXD
#define GPO_CRG0_MCLK_OUT               \
        GPO_SYS_IOMUX_U0_SYS_CRG_MCLK_OUT
#define GPO_GMAC0_CLK_PHY               \
        GPO_SYS_IOMUX_U0_SYS_CRG_CLK_GMAC_PHY
#define GPO_HDMI0_CEC_SDA_OUT           \
        GPO_SYS_IOMUX_U0_DOM_VOUT_TOP_U0_HDMI_TX_PIN_CEC_SDA_OUT
#define GPO_HDMI0_DDC_SCL_OUT           \
        GPO_SYS_IOMUX_U0_DOM_VOUT_TOP_U0_HDMI_TX_PIN_DDC_SCL_OUT
#define GPO_HDMI0_DDC_SDA_OUT           \
        GPO_SYS_IOMUX_U0_DOM_VOUT_TOP_U0_HDMI_TX_PIN_DDC_SDA_OUT
#define GPO_I2C0_IC_CLK_OUT_A           \
        GPO_SYS_IOMUX_U0_DW_I2C_IC_CLK_OUT_A
#define GPO_I2C0_IC_DATA_OUT_A          \
        GPO_SYS_IOMUX_U0_DW_I2C_IC_DATA_OUT_A
#define GPO_I2C1_IC_CLK_OUT_A           \
        GPO_SYS_IOMUX_U1_DW_I2C_IC_CLK_OUT_A
#define GPO_I2C1_IC_DATA_OUT_A          \
        GPO_SYS_IOMUX_U1_DW_I2C_IC_DATA_OUT_A
#define GPO_I2C2_IC_CLK_OUT_A           \
        GPO_SYS_IOMUX_U2_DW_I2C_IC_CLK_OUT_A
#define GPO_I2C2_IC_DATA_OUT_A          \
        GPO_SYS_IOMUX_U2_DW_I2C_IC_DATA_OUT_A
#define GPO_I2C3_IC_CLK_OUT_A           \
        GPO_SYS_IOMUX_U3_DW_I2C_IC_CLK_OUT_A
#define GPO_I2C3_IC_DATA_OUT_A          \
        GPO_SYS_IOMUX_U3_DW_I2C_IC_DATA_OUT_A
#define GPO_I2C4_IC_CLK_OUT_A           \
        GPO_SYS_IOMUX_U4_DW_I2C_IC_CLK_OUT_A
#define GPO_I2C4_IC_DATA_OUT_A          \
        GPO_SYS_IOMUX_U4_DW_I2C_IC_DATA_OUT_A
#define GPO_I2C5_IC_CLK_OUT_A           \
        GPO_SYS_IOMUX_U5_DW_I2C_IC_CLK_OUT_A
#define GPO_I2C5_IC_DATA_OUT_A          \
        GPO_SYS_IOMUX_U5_DW_I2C_IC_DATA_OUT_A
#define GPO_I2C6_IC_CLK_OUT_A           \
        GPO_SYS_IOMUX_U6_DW_I2C_IC_CLK_OUT_A
#define GPO_I2C6_IC_DATA_OUT_A          \
        GPO_SYS_IOMUX_U6_DW_I2C_IC_DATA_OUT_A
#define GPO_I2SRX0_BCLK_MST             \
        GPO_SYS_IOMUX_U0_SYS_CRG_I2SRX_BCLK_MST
#define GPO_I2SRX0_LRCK_MST             \
        GPO_SYS_IOMUX_U0_SYS_CRG_I2SRX_LRCK_MST
#define GPO_I2STX_4CH1_SDO0             \
        GPO_SYS_IOMUX_U1_I2STX_4CH_SDO0
#define GPO_I2STX_4CH1_SDO1             \
        GPO_SYS_IOMUX_U1_I2STX_4CH_SDO1
#define GPO_I2STX_4CH1_SDO2             \
        GPO_SYS_IOMUX_U1_I2STX_4CH_SDO2
#define GPO_I2STX_4CH1_SDO3             \
        GPO_SYS_IOMUX_U1_I2STX_4CH_SDO3
#define GPO_I2STX0_BCLK_MST             \
        GPO_SYS_IOMUX_U0_SYS_CRG_I2STX_BCLK_MST
#define GPO_I2STX0_LRCK_MST             \
        GPO_SYS_IOMUX_U0_SYS_CRG_I2STX_LRCK_MST
#define GPO_JTAG_CPU_CERTIFICATION_TDO  \
        GPO_SYS_IOMUX_U0_JTAG_CERTIFICATION_TDO
#define GPO_JTAG_DSP_TDO                \
        GPO_SYS_IOMUX_U0_HIFI4_JTDO
#define GPO_PDM_4MIC0_DMIC_MCLK         \
        GPO_SYS_IOMUX_U0_PDM_4MIC_DMIC_MCLK
#define GPO_PTC0_PWM_0                  \
        GPO_SYS_IOMUX_U0_PWM_8CH_PTC_PWM_0
#define GPO_PTC0_PWM_1                  \
        GPO_SYS_IOMUX_U0_PWM_8CH_PTC_PWM_1
#define GPO_PTC0_PWM_2                  \
        GPO_SYS_IOMUX_U0_PWM_8CH_PTC_PWM_2
#define GPO_PTC0_PWM_3                  \
        GPO_SYS_IOMUX_U0_PWM_8CH_PTC_PWM_3
#define GPO_PWMDAC0_LEFT_OUTPUT         \
        GPO_SYS_IOMUX_U0_PWMDAC_PWMDAC_LEFT_OUTPUT
#define GPO_PWMDAC0_RIGHT_OUTPUT        \
        GPO_SYS_IOMUX_U0_PWMDAC_PWMDAC_RIGHT_OUTPUT
#define GPO_QSPI0_CSN1                  \
        GPO_SYS_IOMUX_U0_CDNS_QSPI_CSN1
#define GPO_SDIO0_BACK_END_POWER        \
        GPO_SYS_IOMUX_U0_DW_SDIO_BACK_END_POWER
#define GPO_SDIO0_CARD_POWER_EN         \
        GPO_SYS_IOMUX_U0_DW_SDIO_CARD_POWER_EN
#define GPO_SDIO0_CCMD_OD_PULLUP_EN_N   \
        GPO_SYS_IOMUX_U0_DW_SDIO_CCMD_OD_PULLUP_EN_N
#define GPO_SDIO0_RST_N                 \
        GPO_SYS_IOMUX_U0_DW_SDIO_RST_N
#define GPO_SDIO1_BACK_END_POWER        \
        GPO_SYS_IOMUX_U1_DW_SDIO_BACK_END_POWER
#define GPO_SDIO1_CARD_POWER_EN         \
        GPO_SYS_IOMUX_U1_DW_SDIO_CARD_POWER_EN
#define GPO_SDIO1_CCLK_OUT              \
        GPO_SYS_IOMUX_U1_DW_SDIO_CCLK_OUT
#define GPO_SDIO1_CCMD_OD_PULLUP_EN_N   \
        GPO_SYS_IOMUX_U1_DW_SDIO_CCMD_OD_PULLUP_EN_N
#define GPO_SDIO1_CCMD_OUT              \
        GPO_SYS_IOMUX_U1_DW_SDIO_CCMD_OUT
#define GPO_SDIO1_CDATA_OUT_0           \
        GPO_SYS_IOMUX_U1_DW_SDIO_CDATA_OUT_0
#define GPO_SDIO1_CDATA_OUT_1           \
        GPO_SYS_IOMUX_U1_DW_SDIO_CDATA_OUT_1
#define GPO_SDIO1_CDATA_OUT_2           \
        GPO_SYS_IOMUX_U1_DW_SDIO_CDATA_OUT_2
#define GPO_SDIO1_CDATA_OUT_3           \
        GPO_SYS_IOMUX_U1_DW_SDIO_CDATA_OUT_3
#define GPO_SDIO1_CDATA_OUT_4           \
        GPO_SYS_IOMUX_U1_DW_SDIO_CDATA_OUT_4
#define GPO_SDIO1_CDATA_OUT_5           \
        GPO_SYS_IOMUX_U1_DW_SDIO_CDATA_OUT_5
#define GPO_SDIO1_CDATA_OUT_6           \
        GPO_SYS_IOMUX_U1_DW_SDIO_CDATA_OUT_6
#define GPO_SDIO1_CDATA_OUT_7           \
        GPO_SYS_IOMUX_U1_DW_SDIO_CDATA_OUT_7
#define GPO_SDIO1_RST_N                 \
        GPO_SYS_IOMUX_U1_DW_SDIO_RST_N
#define GPO_SPDIF0_SPDIFO               \
        GPO_SYS_IOMUX_U0_CDNS_SPDIF_SPDIFO
#define GPO_SPI0_SSPCLKOUT              \
        GPO_SYS_IOMUX_U0_SSP_SPI_SSPCLKOUT
#define GPO_SPI0_SSPFSSOUT              \
        GPO_SYS_IOMUX_U0_SSP_SPI_SSPFSSOUT
#define GPO_SPI0_SSPTXD                 \
        GPO_SYS_IOMUX_U0_SSP_SPI_SSPTXD
#define GPO_SPI1_SSPCLKOUT              \
        GPO_SYS_IOMUX_U1_SSP_SPI_SSPCLKOUT
#define GPO_SPI1_SSPFSSOUT              \
        GPO_SYS_IOMUX_U1_SSP_SPI_SSPFSSOUT
#define GPO_SPI1_SSPTXD                 \
        GPO_SYS_IOMUX_U1_SSP_SPI_SSPTXD
#define GPO_SPI2_SSPCLKOUT              \
        GPO_SYS_IOMUX_U2_SSP_SPI_SSPCLKOUT
#define GPO_SPI2_SSPFSSOUT              \
        GPO_SYS_IOMUX_U2_SSP_SPI_SSPFSSOUT
#define GPO_SPI2_SSPTXD                 \
        GPO_SYS_IOMUX_U2_SSP_SPI_SSPTXD
#define GPO_SPI3_SSPCLKOUT              \
        GPO_SYS_IOMUX_U3_SSP_SPI_SSPCLKOUT
#define GPO_SPI3_SSPFSSOUT              \
        GPO_SYS_IOMUX_U3_SSP_SPI_SSPFSSOUT
#define GPO_SPI3_SSPTXD                 \
        GPO_SYS_IOMUX_U3_SSP_SPI_SSPTXD
#define GPO_SPI4_SSPCLKOUT              \
        GPO_SYS_IOMUX_U4_SSP_SPI_SSPCLKOUT
#define GPO_SPI4_SSPFSSOUT              \
        GPO_SYS_IOMUX_U4_SSP_SPI_SSPFSSOUT
#define GPO_SPI4_SSPTXD                 \
        GPO_SYS_IOMUX_U4_SSP_SPI_SSPTXD
#define GPO_SPI5_SSPCLKOUT              \
        GPO_SYS_IOMUX_U5_SSP_SPI_SSPCLKOUT
#define GPO_SPI5_SSPFSSOUT              \
        GPO_SYS_IOMUX_U5_SSP_SPI_SSPFSSOUT
#define GPO_SPI5_SSPTXD                 \
        GPO_SYS_IOMUX_U5_SSP_SPI_SSPTXD
#define GPO_SPI6_SSPCLKOUT              \
        GPO_SYS_IOMUX_U6_SSP_SPI_SSPCLKOUT
#define GPO_SPI6_SSPFSSOUT              \
        GPO_SYS_IOMUX_U6_SSP_SPI_SSPFSSOUT
#define GPO_SPI6_SSPTXD                 \
        GPO_SYS_IOMUX_U6_SSP_SPI_SSPTXD
#define GPO_TDM0_CLK_MST                \
        GPO_SYS_IOMUX_U0_SYS_CRG_TDM_CLK_MST
#define GPO_TDM0_PCM_SYNCOUT            \
        GPO_SYS_IOMUX_U0_TDM16SLOT_PCM_SYNCOUT
#define GPO_TDM0_PCM_TXD                \
        GPO_SYS_IOMUX_U0_TDM16SLOT_PCM_TXD
#define GPO_U7MC_TRACE0_TDATA_0         \
        GPO_SYS_IOMUX_U0_U7MC_SFT7110_TRACE_COM_PIB_TDATA_0
#define GPO_U7MC_TRACE0_TDATA_1         \
        GPO_SYS_IOMUX_U0_U7MC_SFT7110_TRACE_COM_PIB_TDATA_1
#define GPO_U7MC_TRACE0_TDATA_2         \
        GPO_SYS_IOMUX_U0_U7MC_SFT7110_TRACE_COM_PIB_TDATA_2
#define GPO_U7MC_TRACE0_TDATA_3         \
        GPO_SYS_IOMUX_U0_U7MC_SFT7110_TRACE_COM_PIB_TDATA_3
#define GPO_U7MC_TRACE0_TREF            \
        GPO_SYS_IOMUX_U0_U7MC_SFT7110_TRACE_COM_PIB_TREF
#define GPO_UART0_SOUT                  \
        GPO_SYS_IOMUX_U0_DW_UART_SOUT
#define GPO_UART1_RTS_N                 \
        GPO_SYS_IOMUX_U1_DW_UART_RTS_N
#define GPO_UART1_SOUT                  \
        GPO_SYS_IOMUX_U1_DW_UART_SOUT
#define GPO_UART2_RTS_N                 \
        GPO_SYS_IOMUX_U2_DW_UART_RTS_N
#define GPO_UART2_SOUT                  \
        GPO_SYS_IOMUX_U2_DW_UART_SOUT
#define GPO_UART3_SOUT                  \
        GPO_SYS_IOMUX_U3_DW_UART_SOUT
#define GPO_UART4_RTS_N                 \
        GPO_SYS_IOMUX_U4_DW_UART_RTS_N
#define GPO_UART4_SOUT                  \
        GPO_SYS_IOMUX_U4_DW_UART_SOUT
#define GPO_UART5_RTS_N                 \
        GPO_SYS_IOMUX_U5_DW_UART_RTS_N
#define GPO_UART5_SOUT                  \
        GPO_SYS_IOMUX_U5_DW_UART_SOUT
#define GPO_USB0_DRIVE_VBUS_IO          \
        GPO_SYS_IOMUX_U0_CDN_USB_DRIVE_VBUS_IO
#define GPO_WAVE511_0_O_UART_TXSOUT     \
        GPO_SYS_IOMUX_U0_WAVE511_O_UART_TXSOUT
#define GPO_WDT0_WDOGRES                \
        GPO_SYS_IOMUX_U0_DSKIT_WDT_WDOGRES
#define GPO_NONE                        \
        GPO_SYS_IOMUX_U6_SSP_SPI_SSPTXD + 1


//gpo(n)_doen signal pool
#define OEN_LOW 	0
#define OEN_HIGH 	1
#define OEN_HDMI0_CEC_SDA_OEN               \
        GPEN_SYS_IOMUX_U0_DOM_VOUT_TOP_U0_HDMI_TX_PIN_CEC_SDA_OEN
#define OEN_HDMI0_DDC_SCL_OEN               \
        GPEN_SYS_IOMUX_U0_DOM_VOUT_TOP_U0_HDMI_TX_PIN_DDC_SCL_OEN
#define OEN_HDMI0_DDC_SDA_OEN               \
        GPEN_SYS_IOMUX_U0_DOM_VOUT_TOP_U0_HDMI_TX_PIN_DDC_SDA_OEN
#define OEN_I2C0_IC_CLK_OE                  \
        GPEN_SYS_IOMUX_U0_DW_I2C_IC_CLK_OE
#define OEN_I2C0_IC_DATA_OE                 \
        GPEN_SYS_IOMUX_U0_DW_I2C_IC_DATA_OE
#define OEN_I2C1_IC_CLK_OE                  \
        GPEN_SYS_IOMUX_U1_DW_I2C_IC_CLK_OE
#define OEN_I2C1_IC_DATA_OE                 \
        GPEN_SYS_IOMUX_U1_DW_I2C_IC_DATA_OE
#define OEN_I2C2_IC_CLK_OE                  \
        GPEN_SYS_IOMUX_U2_DW_I2C_IC_CLK_OE
#define OEN_I2C2_IC_DATA_OE                 \
        GPEN_SYS_IOMUX_U2_DW_I2C_IC_DATA_OE
#define OEN_I2C3_IC_CLK_OE                  \
        GPEN_SYS_IOMUX_U3_DW_I2C_IC_CLK_OE
#define OEN_I2C3_IC_DATA_OE                 \
        GPEN_SYS_IOMUX_U3_DW_I2C_IC_DATA_OE
#define OEN_I2C4_IC_CLK_OE                  \
        GPEN_SYS_IOMUX_U4_DW_I2C_IC_CLK_OE
#define OEN_I2C4_IC_DATA_OE                 \
        GPEN_SYS_IOMUX_U4_DW_I2C_IC_DATA_OE
#define OEN_I2C5_IC_CLK_OE                  \
        GPEN_SYS_IOMUX_U5_DW_I2C_IC_CLK_OE
#define OEN_I2C5_IC_DATA_OE                 \
        GPEN_SYS_IOMUX_U5_DW_I2C_IC_DATA_OE
#define OEN_I2C6_IC_CLK_OE                  \
        GPEN_SYS_IOMUX_U6_DW_I2C_IC_CLK_OE
#define OEN_I2C6_IC_DATA_OE                 \
        GPEN_SYS_IOMUX_U6_DW_I2C_IC_DATA_OE
#define OEN_JTAG_CPU_CERTIFICATION_TDO_OE   \
        GPEN_SYS_IOMUX_U0_JTAG_CERTIFICATION_TDO_OE
#define OEN_JTAG_DSP_TDO_OEN                \
        GPEN_SYS_IOMUX_U0_HIFI4_JTDOEN
#define OEN_PTC0_PWM_0_OE_N                 \
        GPEN_SYS_IOMUX_U0_PWM_8CH_PTC_OE_N_0
#define OEN_PTC0_PWM_1_OE_N                 \
        GPEN_SYS_IOMUX_U0_PWM_8CH_PTC_OE_N_1
#define OEN_PTC0_PWM_2_OE_N                 \
        GPEN_SYS_IOMUX_U0_PWM_8CH_PTC_OE_N_2
#define OEN_PTC0_PWM_3_OE_N                 \
        GPEN_SYS_IOMUX_U0_PWM_8CH_PTC_OE_N_3
#define OEN_SDIO1_CCMD_OUT_EN               \
        GPEN_SYS_IOMUX_U1_DW_SDIO_CCMD_OUT_EN
#define OEN_SDIO1_CDATA_OUT_EN_0            \
        GPEN_SYS_IOMUX_U1_DW_SDIO_CDATA_OUT_EN_0
#define OEN_SDIO1_CDATA_OUT_EN_1            \
        GPEN_SYS_IOMUX_U1_DW_SDIO_CDATA_OUT_EN_1
#define OEN_SDIO1_CDATA_OUT_EN_2            \
        GPEN_SYS_IOMUX_U1_DW_SDIO_CDATA_OUT_EN_2
#define OEN_SDIO1_CDATA_OUT_EN_3            \
        GPEN_SYS_IOMUX_U1_DW_SDIO_CDATA_OUT_EN_3
#define OEN_SDIO1_CDATA_OUT_EN_4            \
        GPEN_SYS_IOMUX_U1_DW_SDIO_CDATA_OUT_EN_4
#define OEN_SDIO1_CDATA_OUT_EN_5            \
        GPEN_SYS_IOMUX_U1_DW_SDIO_CDATA_OUT_EN_5
#define OEN_SDIO1_CDATA_OUT_EN_6            \
        GPEN_SYS_IOMUX_U1_DW_SDIO_CDATA_OUT_EN_6
#define OEN_SDIO1_CDATA_OUT_EN_7            \
        GPEN_SYS_IOMUX_U1_DW_SDIO_CDATA_OUT_EN_7
#define OEN_SPI0_NSSPCTLOE                  \
        GPEN_SYS_IOMUX_U0_SSP_SPI_NSSPCTLOE
#define OEN_SPI0_NSSPOE                     \
        GPEN_SYS_IOMUX_U0_SSP_SPI_NSSPOE
#define OEN_SPI1_NSSPCTLOE                  \
        GPEN_SYS_IOMUX_U1_SSP_SPI_NSSPCTLOE
#define OEN_SPI1_NSSPOE                     \
        GPEN_SYS_IOMUX_U1_SSP_SPI_NSSPOE
#define OEN_SPI2_NSSPCTLOE                  \
        GPEN_SYS_IOMUX_U2_SSP_SPI_NSSPCTLOE
#define OEN_SPI2_NSSPOE                     \
        GPEN_SYS_IOMUX_U2_SSP_SPI_NSSPOE
#define OEN_SPI3_NSSPCTLOE                  \
        GPEN_SYS_IOMUX_U3_SSP_SPI_NSSPCTLOE
#define OEN_SPI3_NSSPOE                     \
        GPEN_SYS_IOMUX_U3_SSP_SPI_NSSPOE
#define OEN_SPI4_NSSPCTLOE                  \
        GPEN_SYS_IOMUX_U4_SSP_SPI_NSSPCTLOE
#define OEN_SPI4_NSSPOE                     \
        GPEN_SYS_IOMUX_U4_SSP_SPI_NSSPOE
#define OEN_SPI5_NSSPCTLOE                  \
        GPEN_SYS_IOMUX_U5_SSP_SPI_NSSPCTLOE
#define OEN_SPI5_NSSPOE                     \
        GPEN_SYS_IOMUX_U5_SSP_SPI_NSSPOE
#define OEN_SPI6_NSSPCTLOE                  \
        GPEN_SYS_IOMUX_U6_SSP_SPI_NSSPCTLOE
#define OEN_SPI6_NSSPOE                     \
        GPEN_SYS_IOMUX_U6_SSP_SPI_NSSPOE
#define OEN_TDM0_NPCM_SYNCOE                \
        GPEN_SYS_IOMUX_U0_TDM16SLOT_NPCM_SYNCOE
#define OEN_TDM0_NPCM_TXDOE                 \
        GPEN_SYS_IOMUX_U0_TDM16SLOT_NPCM_TXDOE
#define OEN_NONE                            \
        GPEN_SYS_IOMUX_U6_SSP_SPI_NSSPOE + 1

//sys_iomux gpi din
#define	GPI_CAN0_CTRL_RXD                       \
        GPI_SYS_IOMUX_U0_CAN_CTRL_RXD
#define	GPI_CAN1_CTRL_RXD                       \
        GPI_SYS_IOMUX_U1_CAN_CTRL_RXD
#define	GPI_CRG0_EXT_MCLK                       \
        GPI_SYS_IOMUX_U0_SYS_CRG_EXT_MCLK
#define	GPI_HDMI0_CEC_SDA_IN                    \
        GPI_SYS_IOMUX_U0_DOM_VOUT_TOP_U0_HDMI_TX_PIN_CEC_SDA_IN
#define	GPI_HDMI0_DDC_SCL_IN                    \
        GPI_SYS_IOMUX_U0_DOM_VOUT_TOP_U0_HDMI_TX_PIN_DDC_SCL_IN
#define	GPI_HDMI0_DDC_SDA_IN                    \
        GPI_SYS_IOMUX_U0_DOM_VOUT_TOP_U0_HDMI_TX_PIN_DDC_SDA_IN
#define	GPI_HDMI0_HPD                           \
        GPI_SYS_IOMUX_U0_DOM_VOUT_TOP_U0_HDMI_TX_PIN_HPD
#define	GPI_I2C0_IC_CLK_IN_A                    \
        GPI_SYS_IOMUX_U0_DW_I2C_IC_CLK_IN_A
#define	GPI_I2C0_IC_DATA_IN_A                   \
        GPI_SYS_IOMUX_U0_DW_I2C_IC_DATA_IN_A
#define	GPI_I2C1_IC_CLK_IN_A                    \
        GPI_SYS_IOMUX_U1_DW_I2C_IC_CLK_IN_A
#define	GPI_I2C1_IC_DATA_IN_A                   \
        GPI_SYS_IOMUX_U1_DW_I2C_IC_DATA_IN_A
#define	GPI_I2C2_IC_CLK_IN_A                    \
        GPI_SYS_IOMUX_U2_DW_I2C_IC_CLK_IN_A
#define	GPI_I2C2_IC_DATA_IN_A                   \
        GPI_SYS_IOMUX_U2_DW_I2C_IC_DATA_IN_A
#define	GPI_I2C3_IC_CLK_IN_A                    \
        GPI_SYS_IOMUX_U3_DW_I2C_IC_CLK_IN_A
#define	GPI_I2C3_IC_DATA_IN_A                   \
        GPI_SYS_IOMUX_U3_DW_I2C_IC_DATA_IN_A
#define	GPI_I2C4_IC_CLK_IN_A                    \
        GPI_SYS_IOMUX_U4_DW_I2C_IC_CLK_IN_A
#define	GPI_I2C4_IC_DATA_IN_A                   \
        GPI_SYS_IOMUX_U4_DW_I2C_IC_DATA_IN_A
#define	GPI_I2C5_IC_CLK_IN_A                    \
        GPI_SYS_IOMUX_U5_DW_I2C_IC_CLK_IN_A
#define	GPI_I2C5_IC_DATA_IN_A                   \
        GPI_SYS_IOMUX_U5_DW_I2C_IC_DATA_IN_A
#define	GPI_I2C6_IC_CLK_IN_A                    \
        GPI_SYS_IOMUX_U6_DW_I2C_IC_CLK_IN_A
#define	GPI_I2C6_IC_DATA_IN_A                   \
        GPI_SYS_IOMUX_U6_DW_I2C_IC_DATA_IN_A
#define	GPI_I2SRX0_BCLK_SLV                     \
        GPI_SYS_IOMUX_U0_SYS_CRG_I2SRX_BCLK_SLV
#define	GPI_I2SRX0_EXT_SDIN0                    \
        GPI_SYS_IOMUX_U0_SAIF_AUDIO_SDIN_MUX_I2SRX_EXT_SDIN0
#define	GPI_I2SRX0_EXT_SDIN1                    \
        GPI_SYS_IOMUX_U0_SAIF_AUDIO_SDIN_MUX_I2SRX_EXT_SDIN1
#define	GPI_I2SRX0_EXT_SDIN2                    \
        GPI_SYS_IOMUX_U0_SAIF_AUDIO_SDIN_MUX_I2SRX_EXT_SDIN2
#define	GPI_I2SRX0_LRCK_SLV                     \
        GPI_SYS_IOMUX_U0_SYS_CRG_I2SRX_LRCK_SLV
#define	GPI_I2STX0_BCLK_SLV                     \
        GPI_SYS_IOMUX_U0_SYS_CRG_I2STX_BCLK_SLV
#define	GPI_I2STX0_LRCK_SLV                     \
        GPI_SYS_IOMUX_U0_SYS_CRG_I2STX_LRCK_SLV
#define	GPI_JTAG_CPU_CERTIFICATION_BYPASS_TRSTN \
        GPI_SYS_IOMUX_U0_CLKRST_SRC_BYPASS_JTAG_TRSTN
#define	GPI_JTAG_CPU_CERTIFICATION_TCK          \
        GPI_SYS_IOMUX_U0_SYS_CRG_CLK_JTAG_TCK
#define	GPI_JTAG_CPU_CERTIFICATION_TDI          \
        GPI_SYS_IOMUX_U0_JTAG_CERTIFICATION_TDI
#define	GPI_JTAG_CPU_CERTIFICATION_TMS          \
        GPI_SYS_IOMUX_U0_JTAG_CERTIFICATION_TMS
#define	GPI_JTAG_DSP_TCK                        \
        GPI_SYS_IOMUX_U0_HIFI4_JTCK
#define	GPI_JTAG_DSP_TDI                        \
        GPI_SYS_IOMUX_U0_HIFI4_JTDI
#define	GPI_JTAG_DSP_TMS                        \
        GPI_SYS_IOMUX_U0_HIFI4_JTMS
#define	GPI_JTAG_DSP_TRST_N                     \
        GPI_SYS_IOMUX_U0_HIFI4_JTRSTN
#define	GPI_PDM_4MIC0_DMIC0_DIN                 \
        GPI_SYS_IOMUX_U0_PDM_4MIC_DMIC0_DIN
#define	GPI_PDM_4MIC0_DMIC1_DIN                 \
        GPI_SYS_IOMUX_U0_PDM_4MIC_DMIC1_DIN
#define	GPI_SDIO0_CARD_DETECT_N                 \
        GPI_SYS_IOMUX_U0_DW_SDIO_CARD_DETECT_N
#define	GPI_SDIO0_CARD_INT_N                    \
        GPI_SYS_IOMUX_U0_DW_SDIO_CARD_INT_N
#define	GPI_SDIO0_CARD_WRITE_PRT                \
        GPI_SYS_IOMUX_U0_DW_SDIO_CARD_WRITE_PRT
#define	GPI_SDIO1_CARD_DETECT_N                 \
        GPI_SYS_IOMUX_U1_DW_SDIO_CARD_DETECT_N
#define	GPI_SDIO1_CARD_INT_N                    \
        GPI_SYS_IOMUX_U1_DW_SDIO_CARD_INT_N
#define	GPI_SDIO1_CARD_WRITE_PRT                \
        GPI_SYS_IOMUX_U1_DW_SDIO_CARD_WRITE_PRT
#define	GPI_SDIO1_CCMD_IN                       \
        GPI_SYS_IOMUX_U1_DW_SDIO_CCMD_IN
#define	GPI_SDIO1_CDATA_IN_0                    \
        GPI_SYS_IOMUX_U1_DW_SDIO_CDATA_IN_0
#define	GPI_SDIO1_CDATA_IN_1                    \
        GPI_SYS_IOMUX_U1_DW_SDIO_CDATA_IN_1
#define	GPI_SDIO1_CDATA_IN_2                    \
        GPI_SYS_IOMUX_U1_DW_SDIO_CDATA_IN_2
#define	GPI_SDIO1_CDATA_IN_3                    \
        GPI_SYS_IOMUX_U1_DW_SDIO_CDATA_IN_3
#define	GPI_SDIO1_CDATA_IN_4                    \
        GPI_SYS_IOMUX_U1_DW_SDIO_CDATA_IN_4
#define	GPI_SDIO1_CDATA_IN_5                    \
        GPI_SYS_IOMUX_U1_DW_SDIO_CDATA_IN_5
#define	GPI_SDIO1_CDATA_IN_6                    \
        GPI_SYS_IOMUX_U1_DW_SDIO_CDATA_IN_6
#define	GPI_SDIO1_CDATA_IN_7                    \
        GPI_SYS_IOMUX_U1_DW_SDIO_CDATA_IN_7
#define	GPI_SDIO1_DATA_STROBE                   \
        GPI_SYS_IOMUX_U1_DW_SDIO_DATA_STROBE
#define	GPI_SPDIF0_SPDIFI                       \
        GPI_SYS_IOMUX_U0_CDNS_SPDIF_SPDIFI
#define	GPI_SPI0_SSPCLKIN                       \
        GPI_SYS_IOMUX_U0_SSP_SPI_SSPCLKIN
#define	GPI_SPI0_SSPFSSIN                       \
        GPI_SYS_IOMUX_U0_SSP_SPI_SSPFSSIN
#define	GPI_SPI0_SSPRXD                         \
        GPI_SYS_IOMUX_U0_SSP_SPI_SSPRXD
#define	GPI_SPI1_SSPCLKIN                       \
        GPI_SYS_IOMUX_U1_SSP_SPI_SSPCLKIN
#define	GPI_SPI1_SSPFSSIN                       \
        GPI_SYS_IOMUX_U1_SSP_SPI_SSPFSSIN
#define	GPI_SPI1_SSPRXD                         \
        GPI_SYS_IOMUX_U1_SSP_SPI_SSPRXD
#define	GPI_SPI2_SSPCLKIN                       \
        GPI_SYS_IOMUX_U2_SSP_SPI_SSPCLKIN
#define	GPI_SPI2_SSPFSSIN                       \
        GPI_SYS_IOMUX_U2_SSP_SPI_SSPFSSIN
#define	GPI_SPI2_SSPRXD                         \
        GPI_SYS_IOMUX_U2_SSP_SPI_SSPRXD
#define	GPI_SPI3_SSPCLKIN                       \
        GPI_SYS_IOMUX_U3_SSP_SPI_SSPCLKIN
#define	GPI_SPI3_SSPFSSIN                       \
        GPI_SYS_IOMUX_U3_SSP_SPI_SSPFSSIN
#define	GPI_SPI3_SSPRXD                         \
        GPI_SYS_IOMUX_U3_SSP_SPI_SSPRXD
#define	GPI_SPI4_SSPCLKIN                       \
        GPI_SYS_IOMUX_U4_SSP_SPI_SSPCLKIN
#define	GPI_SPI4_SSPFSSIN                       \
        GPI_SYS_IOMUX_U4_SSP_SPI_SSPFSSIN
#define	GPI_SPI4_SSPRXD                         \
        GPI_SYS_IOMUX_U4_SSP_SPI_SSPRXD
#define	GPI_SPI5_SSPCLKIN                       \
        GPI_SYS_IOMUX_U5_SSP_SPI_SSPCLKIN
#define	GPI_SPI5_SSPFSSIN                       \
        GPI_SYS_IOMUX_U5_SSP_SPI_SSPFSSIN
#define	GPI_SPI5_SSPRXD                         \
        GPI_SYS_IOMUX_U5_SSP_SPI_SSPRXD
#define	GPI_SPI6_SSPCLKIN                       \
        GPI_SYS_IOMUX_U6_SSP_SPI_SSPCLKIN
#define	GPI_SPI6_SSPFSSIN                       \
        GPI_SYS_IOMUX_U6_SSP_SPI_SSPFSSIN
#define	GPI_SPI6_SSPRXD                         \
        GPI_SYS_IOMUX_U6_SSP_SPI_SSPRXD
#define	GPI_TDM0_CLK_SLV                        \
        GPI_SYS_IOMUX_U0_SYS_CRG_TDM_CLK_SLV
#define	GPI_TDM0_PCM_RXD                        \
        GPI_SYS_IOMUX_U0_TDM16SLOT_PCM_RXD
#define	GPI_TDM0_PCM_SYNCIN                     \
        GPI_SYS_IOMUX_U0_TDM16SLOT_PCM_SYNCIN
#define	GPI_UART0_SIN                           \
        GPI_SYS_IOMUX_U0_DW_UART_SIN
#define	GPI_UART1_CTS_N                         \
        GPI_SYS_IOMUX_U1_DW_UART_CTS_N
#define	GPI_UART1_SIN                           \
        GPI_SYS_IOMUX_U1_DW_UART_SIN
#define	GPI_UART2_CTS_N                         \
        GPI_SYS_IOMUX_U2_DW_UART_CTS_N
#define	GPI_UART2_SIN                           \
        GPI_SYS_IOMUX_U2_DW_UART_SIN
#define	GPI_UART3_SIN                           \
        GPI_SYS_IOMUX_U3_DW_UART_SIN
#define	GPI_UART4_CTS_N                         \
        GPI_SYS_IOMUX_U4_DW_UART_CTS_N
#define	GPI_UART4_SIN                           \
        GPI_SYS_IOMUX_U4_DW_UART_SIN
#define	GPI_UART5_CTS_N                         \
        GPI_SYS_IOMUX_U5_DW_UART_CTS_N
#define	GPI_UART5_SIN                           \
        GPI_SYS_IOMUX_U5_DW_UART_SIN
#define	GPI_USB0_OVERCURRENT_N_IO               \
        GPI_SYS_IOMUX_U0_CDN_USB_OVERCURRENT_N_IO
#define	GPI_WAVE511_0_I_UART_RXSIN              \
        GPI_SYS_IOMUX_U0_WAVE511_I_UART_RXSIN
#define	GPI_NONE  					GPI_SYS_IOMUX_U6_SSP_SPI_SSPRXD

//sys_iomux syscon
#define SYS_IOMUX_CFG__SAIF__SYSCFG_588_ADDR               (0x24cU)
#define SYS_IOMUX_CFG__SAIF__SYSCFG_592_ADDR               (0x250U)
#define SYS_IOMUX_CFG__SAIF__SYSCFG_596_ADDR               (0x254U)
#define SYS_IOMUX_CFG__SAIF__SYSCFG_600_ADDR               (0x258U)
#define SYS_IOMUX_CFG__SAIF__SYSCFG_604_ADDR               (0x25cU)
#define SYS_IOMUX_CFG__SAIF__SYSCFG_608_ADDR               (0x260U)
#define SYS_IOMUX_CFG__SAIF__SYSCFG_612_ADDR               (0x264U)
#define SYS_IOMUX_CFG__SAIF__SYSCFG_616_ADDR               (0x268U)
#define SYS_IOMUX_CFG__SAIF__SYSCFG_620_ADDR               (0x26cU)
#define SYS_IOMUX_CFG__SAIF__SYSCFG_624_ADDR               (0x270U)
#define SYS_IOMUX_CFG__SAIF__SYSCFG_628_ADDR               (0x274U)
#define SYS_IOMUX_CFG__SAIF__SYSCFG_632_ADDR               (0x278U)
#define SYS_IOMUX_CFG__SAIF__SYSCFG_636_ADDR               (0x27cU)
#define SYS_IOMUX_CFG__SAIF__SYSCFG_640_ADDR               (0x280U)

#define PADCFG_PAD_GMAC1_MDC_SYSCON     SYS_IOMUX_CFG__SAIF__SYSCFG_588_ADDR
#define PADCFG_PAD_GMAC1_MDIO_SYSCON    SYS_IOMUX_CFG__SAIF__SYSCFG_592_ADDR
#define PADCFG_PAD_GMAC1_RXD0_SYSCON    SYS_IOMUX_CFG__SAIF__SYSCFG_596_ADDR
#define PADCFG_PAD_GMAC1_RXD1_SYSCON    SYS_IOMUX_CFG__SAIF__SYSCFG_600_ADDR
#define PADCFG_PAD_GMAC1_RXD2_SYSCON    SYS_IOMUX_CFG__SAIF__SYSCFG_604_ADDR
#define PADCFG_PAD_GMAC1_RXD3_SYSCON    SYS_IOMUX_CFG__SAIF__SYSCFG_608_ADDR
#define PADCFG_PAD_GMAC1_RXDV_SYSCON    SYS_IOMUX_CFG__SAIF__SYSCFG_612_ADDR
#define PADCFG_PAD_GMAC1_RXC_SYSCON     SYS_IOMUX_CFG__SAIF__SYSCFG_616_ADDR
#define PADCFG_PAD_GMAC1_TXD0_SYSCON    SYS_IOMUX_CFG__SAIF__SYSCFG_620_ADDR
#define PADCFG_PAD_GMAC1_TXD1_SYSCON    SYS_IOMUX_CFG__SAIF__SYSCFG_624_ADDR
#define PADCFG_PAD_GMAC1_TXD2_SYSCON    SYS_IOMUX_CFG__SAIF__SYSCFG_628_ADDR
#define PADCFG_PAD_GMAC1_TXD3_SYSCON    SYS_IOMUX_CFG__SAIF__SYSCFG_632_ADDR
#define PADCFG_PAD_GMAC1_TXEN_SYSCON    SYS_IOMUX_CFG__SAIF__SYSCFG_636_ADDR
#define PADCFG_PAD_GMAC1_TXC_SYSCON     SYS_IOMUX_CFG__SAIF__SYSCFG_640_ADDR


//sys_iomux func sel setting
#define SYS_IOMUX_CFGSAIF__SYSCFG_668_ADDR                 (0x29cU)
#define PAD_GMAC1_RXC_FUNC_SEL_WIDTH                       0x2U
#define PAD_GMAC1_RXC_FUNC_SEL_SHIFT                       0x0U
#define PAD_GMAC1_RXC_FUNC_SEL_MASK                        0x3U
#define PAD_GPIO10_FUNC_SEL_WIDTH                          0x3U
#define PAD_GPIO10_FUNC_SEL_SHIFT                          0x2U
#define PAD_GPIO10_FUNC_SEL_MASK                           0x1CU
#define PAD_GPIO11_FUNC_SEL_WIDTH                          0x3U
#define PAD_GPIO11_FUNC_SEL_SHIFT                          0x5U
#define PAD_GPIO11_FUNC_SEL_MASK                           0xE0U
#define PAD_GPIO12_FUNC_SEL_WIDTH                          0x3U
#define PAD_GPIO12_FUNC_SEL_SHIFT                          0x8U
#define PAD_GPIO12_FUNC_SEL_MASK                           0x700U
#define PAD_GPIO13_FUNC_SEL_WIDTH                          0x3U
#define PAD_GPIO13_FUNC_SEL_SHIFT                          0xBU
#define PAD_GPIO13_FUNC_SEL_MASK                           0x3800U
#define PAD_GPIO14_FUNC_SEL_WIDTH                          0x3U
#define PAD_GPIO14_FUNC_SEL_SHIFT                          0xEU
#define PAD_GPIO14_FUNC_SEL_MASK                           0x1C000U
#define PAD_GPIO15_FUNC_SEL_WIDTH                          0x3U
#define PAD_GPIO15_FUNC_SEL_SHIFT                          0x11U
#define PAD_GPIO15_FUNC_SEL_MASK                           0xE0000U
#define PAD_GPIO16_FUNC_SEL_WIDTH                          0x3U
#define PAD_GPIO16_FUNC_SEL_SHIFT                          0x14U
#define PAD_GPIO16_FUNC_SEL_MASK                           0x700000U
#define PAD_GPIO17_FUNC_SEL_WIDTH                          0x3U
#define PAD_GPIO17_FUNC_SEL_SHIFT                          0x17U
#define PAD_GPIO17_FUNC_SEL_MASK                           0x3800000U
#define PAD_GPIO18_FUNC_SEL_WIDTH                          0x3U
#define PAD_GPIO18_FUNC_SEL_SHIFT                          0x1AU
#define PAD_GPIO18_FUNC_SEL_MASK                           0x1C000000U
#define PAD_GPIO19_FUNC_SEL_WIDTH                          0x3U
#define PAD_GPIO19_FUNC_SEL_SHIFT                          0x1DU
#define PAD_GPIO19_FUNC_SEL_MASK                           0xE0000000U
#define SYS_IOMUX_CFGSAIF__SYSCFG_672_ADDR                 (0x2a0U)
#define PAD_GPIO20_FUNC_SEL_WIDTH                          0x3U
#define PAD_GPIO20_FUNC_SEL_SHIFT                          0x0U
#define PAD_GPIO20_FUNC_SEL_MASK                           0x7U
#define PAD_GPIO21_FUNC_SEL_WIDTH                          0x3U
#define PAD_GPIO21_FUNC_SEL_SHIFT                          0x3U
#define PAD_GPIO21_FUNC_SEL_MASK                           0x38U
#define PAD_GPIO22_FUNC_SEL_WIDTH                          0x3U
#define PAD_GPIO22_FUNC_SEL_SHIFT                          0x6U
#define PAD_GPIO22_FUNC_SEL_MASK                           0x1C0U
#define PAD_GPIO23_FUNC_SEL_WIDTH                          0x3U
#define PAD_GPIO23_FUNC_SEL_SHIFT                          0x9U
#define PAD_GPIO23_FUNC_SEL_MASK                           0xE00U
#define PAD_GPIO24_FUNC_SEL_WIDTH                          0x3U
#define PAD_GPIO24_FUNC_SEL_SHIFT                          0xCU
#define PAD_GPIO24_FUNC_SEL_MASK                           0x7000U
#define PAD_GPIO25_FUNC_SEL_WIDTH                          0x3U
#define PAD_GPIO25_FUNC_SEL_SHIFT                          0xFU
#define PAD_GPIO25_FUNC_SEL_MASK                           0x38000U
#define PAD_GPIO26_FUNC_SEL_WIDTH                          0x3U
#define PAD_GPIO26_FUNC_SEL_SHIFT                          0x12U
#define PAD_GPIO26_FUNC_SEL_MASK                           0x1C0000U
#define PAD_GPIO27_FUNC_SEL_WIDTH                          0x3U
#define PAD_GPIO27_FUNC_SEL_SHIFT                          0x15U
#define PAD_GPIO27_FUNC_SEL_MASK                           0xE00000U
#define PAD_GPIO28_FUNC_SEL_WIDTH                          0x3U
#define PAD_GPIO28_FUNC_SEL_SHIFT                          0x18U
#define PAD_GPIO28_FUNC_SEL_MASK                           0x7000000U
#define PAD_GPIO29_FUNC_SEL_WIDTH                          0x3U
#define PAD_GPIO29_FUNC_SEL_SHIFT                          0x1BU
#define PAD_GPIO29_FUNC_SEL_MASK                           0x38000000U
#define SYS_IOMUX_CFGSAIF__SYSCFG_676_ADDR                 (0x2a4U)
#define PAD_GPIO30_FUNC_SEL_WIDTH                          0x3U
#define PAD_GPIO30_FUNC_SEL_SHIFT                          0x0U
#define PAD_GPIO30_FUNC_SEL_MASK                           0x7U
#define PAD_GPIO31_FUNC_SEL_WIDTH                          0x3U
#define PAD_GPIO31_FUNC_SEL_SHIFT                          0x3U
#define PAD_GPIO31_FUNC_SEL_MASK                           0x38U
#define PAD_GPIO32_FUNC_SEL_WIDTH                          0x3U
#define PAD_GPIO32_FUNC_SEL_SHIFT                          0x6U
#define PAD_GPIO32_FUNC_SEL_MASK                           0x1C0U
#define PAD_GPIO33_FUNC_SEL_WIDTH                          0x3U
#define PAD_GPIO33_FUNC_SEL_SHIFT                          0x9U
#define PAD_GPIO33_FUNC_SEL_MASK                           0xE00U
#define PAD_GPIO34_FUNC_SEL_WIDTH                          0x3U
#define PAD_GPIO34_FUNC_SEL_SHIFT                          0xCU
#define PAD_GPIO34_FUNC_SEL_MASK                           0x7000U
#define PAD_GPIO35_FUNC_SEL_WIDTH                          0x2U
#define PAD_GPIO35_FUNC_SEL_SHIFT                          0xFU
#define PAD_GPIO35_FUNC_SEL_MASK                           0x18000U
#define PAD_GPIO36_FUNC_SEL_WIDTH                          0x3U
#define PAD_GPIO36_FUNC_SEL_SHIFT                          0x11U
#define PAD_GPIO36_FUNC_SEL_MASK                           0xE0000U
#define PAD_GPIO37_FUNC_SEL_WIDTH                          0x3U
#define PAD_GPIO37_FUNC_SEL_SHIFT                          0x14U
#define PAD_GPIO37_FUNC_SEL_MASK                           0x700000U
#define PAD_GPIO38_FUNC_SEL_WIDTH                          0x3U
#define PAD_GPIO38_FUNC_SEL_SHIFT                          0x17U
#define PAD_GPIO38_FUNC_SEL_MASK                           0x3800000U
#define PAD_GPIO39_FUNC_SEL_WIDTH                          0x3U
#define PAD_GPIO39_FUNC_SEL_SHIFT                          0x1AU
#define PAD_GPIO39_FUNC_SEL_MASK                           0x1C000000U
#define PAD_GPIO40_FUNC_SEL_WIDTH                          0x3U
#define PAD_GPIO40_FUNC_SEL_SHIFT                          0x1DU
#define PAD_GPIO40_FUNC_SEL_MASK                           0xE0000000U
#define SYS_IOMUX_CFGSAIF__SYSCFG_680_ADDR                 (0x2a8U)
#define PAD_GPIO41_FUNC_SEL_WIDTH                          0x3U
#define PAD_GPIO41_FUNC_SEL_SHIFT                          0x0U
#define PAD_GPIO41_FUNC_SEL_MASK                           0x7U
#define PAD_GPIO42_FUNC_SEL_WIDTH                          0x3U
#define PAD_GPIO42_FUNC_SEL_SHIFT                          0x3U
#define PAD_GPIO42_FUNC_SEL_MASK                           0x38U
#define PAD_GPIO43_FUNC_SEL_WIDTH                          0x3U
#define PAD_GPIO43_FUNC_SEL_SHIFT                          0x6U
#define PAD_GPIO43_FUNC_SEL_MASK                           0x1C0U
#define PAD_GPIO44_FUNC_SEL_WIDTH                          0x3U
#define PAD_GPIO44_FUNC_SEL_SHIFT                          0x9U
#define PAD_GPIO44_FUNC_SEL_MASK                           0xE00U
#define PAD_GPIO45_FUNC_SEL_WIDTH                          0x3U
#define PAD_GPIO45_FUNC_SEL_SHIFT                          0xCU
#define PAD_GPIO45_FUNC_SEL_MASK                           0x7000U
#define PAD_GPIO46_FUNC_SEL_WIDTH                          0x3U
#define PAD_GPIO46_FUNC_SEL_SHIFT                          0xFU
#define PAD_GPIO46_FUNC_SEL_MASK                           0x38000U
#define PAD_GPIO47_FUNC_SEL_WIDTH                          0x3U
#define PAD_GPIO47_FUNC_SEL_SHIFT                          0x12U
#define PAD_GPIO47_FUNC_SEL_MASK                           0x1C0000U
#define PAD_GPIO48_FUNC_SEL_WIDTH                          0x3U
#define PAD_GPIO48_FUNC_SEL_SHIFT                          0x15U
#define PAD_GPIO48_FUNC_SEL_MASK                           0xE00000U
#define PAD_GPIO49_FUNC_SEL_WIDTH                          0x3U
#define PAD_GPIO49_FUNC_SEL_SHIFT                          0x18U
#define PAD_GPIO49_FUNC_SEL_MASK                           0x7000000U
#define PAD_GPIO50_FUNC_SEL_WIDTH                          0x3U
#define PAD_GPIO50_FUNC_SEL_SHIFT                          0x1BU
#define PAD_GPIO50_FUNC_SEL_MASK                           0x38000000U
#define PAD_GPIO51_FUNC_SEL_WIDTH                          0x2U
#define PAD_GPIO51_FUNC_SEL_SHIFT                          0x1EU
#define PAD_GPIO51_FUNC_SEL_MASK                           0xC0000000U
#define SYS_IOMUX_CFGSAIF__SYSCFG_684_ADDR                 (0x2acU)
#define PAD_GPIO52_FUNC_SEL_WIDTH                          0x2U
#define PAD_GPIO52_FUNC_SEL_SHIFT                          0x0U
#define PAD_GPIO52_FUNC_SEL_MASK                           0x3U
#define PAD_GPIO53_FUNC_SEL_WIDTH                          0x2U
#define PAD_GPIO53_FUNC_SEL_SHIFT                          0x2U
#define PAD_GPIO53_FUNC_SEL_MASK                           0xCU
#define PAD_GPIO54_FUNC_SEL_WIDTH                          0x2U
#define PAD_GPIO54_FUNC_SEL_SHIFT                          0x4U
#define PAD_GPIO54_FUNC_SEL_MASK                           0x30U
#define PAD_GPIO55_FUNC_SEL_WIDTH                          0x3U
#define PAD_GPIO55_FUNC_SEL_SHIFT                          0x6U
#define PAD_GPIO55_FUNC_SEL_MASK                           0x1C0U
#define PAD_GPIO56_FUNC_SEL_WIDTH                          0x3U
#define PAD_GPIO56_FUNC_SEL_SHIFT                          0x9U
#define PAD_GPIO56_FUNC_SEL_MASK                           0xE00U
#define PAD_GPIO57_FUNC_SEL_WIDTH                          0x3U
#define PAD_GPIO57_FUNC_SEL_SHIFT                          0xCU
#define PAD_GPIO57_FUNC_SEL_MASK                           0x7000U
#define PAD_GPIO58_FUNC_SEL_WIDTH                          0x3U
#define PAD_GPIO58_FUNC_SEL_SHIFT                          0xFU
#define PAD_GPIO58_FUNC_SEL_MASK                           0x38000U
#define PAD_GPIO59_FUNC_SEL_WIDTH                          0x3U
#define PAD_GPIO59_FUNC_SEL_SHIFT                          0x12U
#define PAD_GPIO59_FUNC_SEL_MASK                           0x1C0000U
#define PAD_GPIO60_FUNC_SEL_WIDTH                          0x3U
#define PAD_GPIO60_FUNC_SEL_SHIFT                          0x15U
#define PAD_GPIO60_FUNC_SEL_MASK                           0xE00000U
#define PAD_GPIO61_FUNC_SEL_WIDTH                          0x3U
#define PAD_GPIO61_FUNC_SEL_SHIFT                          0x18U
#define PAD_GPIO61_FUNC_SEL_MASK                           0x7000000U
#define PAD_GPIO62_FUNC_SEL_WIDTH                          0x3U
#define PAD_GPIO62_FUNC_SEL_SHIFT                          0x1BU
#define PAD_GPIO62_FUNC_SEL_MASK                           0x38000000U
#define PAD_GPIO63_FUNC_SEL_WIDTH                          0x2U
#define PAD_GPIO63_FUNC_SEL_SHIFT                          0x1EU
#define PAD_GPIO63_FUNC_SEL_MASK                           0xC0000000U
#define SYS_IOMUX_CFGSAIF__SYSCFG_688_ADDR                 (0x2b0U)
#define PAD_GPIO6_FUNC_SEL_WIDTH                           0x2U
#define PAD_GPIO6_FUNC_SEL_SHIFT                           0x0U
#define PAD_GPIO6_FUNC_SEL_MASK                            0x3U
#define PAD_GPIO7_FUNC_SEL_WIDTH                           0x3U
#define PAD_GPIO7_FUNC_SEL_SHIFT                           0x2U
#define PAD_GPIO7_FUNC_SEL_MASK                            0x1CU
#define PAD_GPIO8_FUNC_SEL_WIDTH                           0x3U
#define PAD_GPIO8_FUNC_SEL_SHIFT                           0x5U
#define PAD_GPIO8_FUNC_SEL_MASK                            0xE0U
#define PAD_GPIO9_FUNC_SEL_WIDTH                           0x3U
#define PAD_GPIO9_FUNC_SEL_SHIFT                           0x8U
#define PAD_GPIO9_FUNC_SEL_MASK                            0x700U
#define U0_DOM_ISP_TOP_U0_VIN_DVP_DATA_C0_FUNC_SEL_WIDTH   0x3U
#define U0_DOM_ISP_TOP_U0_VIN_DVP_DATA_C0_FUNC_SEL_SHIFT   0xBU
#define U0_DOM_ISP_TOP_U0_VIN_DVP_DATA_C0_FUNC_SEL_MASK    0x3800U
#define U0_DOM_ISP_TOP_U0_VIN_DVP_DATA_C10_FUNC_SEL_WIDTH  0x3U
#define U0_DOM_ISP_TOP_U0_VIN_DVP_DATA_C10_FUNC_SEL_SHIFT  0xEU
#define U0_DOM_ISP_TOP_U0_VIN_DVP_DATA_C10_FUNC_SEL_MASK   0x1C000U
#define U0_DOM_ISP_TOP_U0_VIN_DVP_DATA_C11_FUNC_SEL_WIDTH  0x3U
#define U0_DOM_ISP_TOP_U0_VIN_DVP_DATA_C11_FUNC_SEL_SHIFT  0x11U
#define U0_DOM_ISP_TOP_U0_VIN_DVP_DATA_C11_FUNC_SEL_MASK   0xE0000U
#define U0_DOM_ISP_TOP_U0_VIN_DVP_DATA_C1_FUNC_SEL_WIDTH   0x3U
#define U0_DOM_ISP_TOP_U0_VIN_DVP_DATA_C1_FUNC_SEL_SHIFT   0x14U
#define U0_DOM_ISP_TOP_U0_VIN_DVP_DATA_C1_FUNC_SEL_MASK    0x700000U
#define U0_DOM_ISP_TOP_U0_VIN_DVP_DATA_C2_FUNC_SEL_WIDTH   0x3U
#define U0_DOM_ISP_TOP_U0_VIN_DVP_DATA_C2_FUNC_SEL_SHIFT   0x17U
#define U0_DOM_ISP_TOP_U0_VIN_DVP_DATA_C2_FUNC_SEL_MASK    0x3800000U
#define U0_DOM_ISP_TOP_U0_VIN_DVP_DATA_C3_FUNC_SEL_WIDTH   0x3U
#define U0_DOM_ISP_TOP_U0_VIN_DVP_DATA_C3_FUNC_SEL_SHIFT   0x1AU
#define U0_DOM_ISP_TOP_U0_VIN_DVP_DATA_C3_FUNC_SEL_MASK    0x1C000000U
#define U0_DOM_ISP_TOP_U0_VIN_DVP_DATA_C4_FUNC_SEL_WIDTH   0x3U
#define U0_DOM_ISP_TOP_U0_VIN_DVP_DATA_C4_FUNC_SEL_SHIFT   0x1DU
#define U0_DOM_ISP_TOP_U0_VIN_DVP_DATA_C4_FUNC_SEL_MASK    0xE0000000U
#define SYS_IOMUX_CFGSAIF__SYSCFG_692_ADDR                 (0x2b4U)
#define U0_DOM_ISP_TOP_U0_VIN_DVP_DATA_C5_FUNC_SEL_WIDTH   0x3U
#define U0_DOM_ISP_TOP_U0_VIN_DVP_DATA_C5_FUNC_SEL_SHIFT   0x0U
#define U0_DOM_ISP_TOP_U0_VIN_DVP_DATA_C5_FUNC_SEL_MASK    0x7U
#define U0_DOM_ISP_TOP_U0_VIN_DVP_DATA_C6_FUNC_SEL_WIDTH   0x3U
#define U0_DOM_ISP_TOP_U0_VIN_DVP_DATA_C6_FUNC_SEL_SHIFT   0x3U
#define U0_DOM_ISP_TOP_U0_VIN_DVP_DATA_C6_FUNC_SEL_MASK    0x38U
#define U0_DOM_ISP_TOP_U0_VIN_DVP_DATA_C7_FUNC_SEL_WIDTH   0x3U
#define U0_DOM_ISP_TOP_U0_VIN_DVP_DATA_C7_FUNC_SEL_SHIFT   0x6U
#define U0_DOM_ISP_TOP_U0_VIN_DVP_DATA_C7_FUNC_SEL_MASK    0x1C0U
#define U0_DOM_ISP_TOP_U0_VIN_DVP_DATA_C8_FUNC_SEL_WIDTH   0x3U
#define U0_DOM_ISP_TOP_U0_VIN_DVP_DATA_C8_FUNC_SEL_SHIFT   0x9U
#define U0_DOM_ISP_TOP_U0_VIN_DVP_DATA_C8_FUNC_SEL_MASK    0xE00U
#define U0_DOM_ISP_TOP_U0_VIN_DVP_DATA_C9_FUNC_SEL_WIDTH   0x3U
#define U0_DOM_ISP_TOP_U0_VIN_DVP_DATA_C9_FUNC_SEL_SHIFT   0xCU
#define U0_DOM_ISP_TOP_U0_VIN_DVP_DATA_C9_FUNC_SEL_MASK    0x7000U
#define U0_DOM_ISP_TOP_U0_VIN_DVP_HVALID_C_FUNC_SEL_WIDTH  0x3U
#define U0_DOM_ISP_TOP_U0_VIN_DVP_HVALID_C_FUNC_SEL_SHIFT  0xFU
#define U0_DOM_ISP_TOP_U0_VIN_DVP_HVALID_C_FUNC_SEL_MASK   0x38000U
#define U0_DOM_ISP_TOP_U0_VIN_DVP_VVALID_C_FUNC_SEL_WIDTH  0x3U
#define U0_DOM_ISP_TOP_U0_VIN_DVP_VVALID_C_FUNC_SEL_SHIFT  0x12U
#define U0_DOM_ISP_TOP_U0_VIN_DVP_VVALID_C_FUNC_SEL_MASK   0x1C0000U
#define U0_SYS_CRG_DVP_CLK_FUNC_SEL_WIDTH                  0x3U
#define U0_SYS_CRG_DVP_CLK_FUNC_SEL_SHIFT                  0x15U
#define U0_SYS_CRG_DVP_CLK_FUNC_SEL_MASK                   0xE00000U

#define PAD_GMAC1_RXC_FUNC_SEL                              \
        SYS_IOMUX_CFGSAIF__SYSCFG_668_ADDR                  \
        PAD_GMAC1_RXC_FUNC_SEL_SHIFT                        \
        PAD_GMAC1_RXC_FUNC_SEL_MASK
#define PAD_GPIO10_FUNC_SEL                                 \
        SYS_IOMUX_CFGSAIF__SYSCFG_668_ADDR                  \
        PAD_GPIO10_FUNC_SEL_SHIFT                           \
        PAD_GPIO10_FUNC_SEL_MASK
#define PAD_GPIO11_FUNC_SEL                                 \
        SYS_IOMUX_CFGSAIF__SYSCFG_668_ADDR                  \
        PAD_GPIO11_FUNC_SEL_SHIFT                           \
        PAD_GPIO11_FUNC_SEL_MASK
#define PAD_GPIO12_FUNC_SEL                                 \
        SYS_IOMUX_CFGSAIF__SYSCFG_668_ADDR                  \
        PAD_GPIO12_FUNC_SEL_SHIFT                           \
        PAD_GPIO12_FUNC_SEL_MASK
#define PAD_GPIO13_FUNC_SEL                                 \
        SYS_IOMUX_CFGSAIF__SYSCFG_668_ADDR                  \
        PAD_GPIO13_FUNC_SEL_SHIFT                           \
        PAD_GPIO13_FUNC_SEL_MASK
#define PAD_GPIO14_FUNC_SEL                                 \
        SYS_IOMUX_CFGSAIF__SYSCFG_668_ADDR                  \
        PAD_GPIO14_FUNC_SEL_SHIFT                           \
        PAD_GPIO14_FUNC_SEL_MASK
#define PAD_GPIO15_FUNC_SEL                                 \
        SYS_IOMUX_CFGSAIF__SYSCFG_668_ADDR                  \
        PAD_GPIO15_FUNC_SEL_SHIFT                           \
        PAD_GPIO15_FUNC_SEL_MASK
#define PAD_GPIO16_FUNC_SEL                                 \
        SYS_IOMUX_CFGSAIF__SYSCFG_668_ADDR                  \
        PAD_GPIO16_FUNC_SEL_SHIFT                           \
        PAD_GPIO16_FUNC_SEL_MASK
#define PAD_GPIO17_FUNC_SEL                                 \
        SYS_IOMUX_CFGSAIF__SYSCFG_668_ADDR                  \
        PAD_GPIO17_FUNC_SEL_SHIFT                           \
        PAD_GPIO17_FUNC_SEL_MASK
#define PAD_GPIO18_FUNC_SEL                                 \
        SYS_IOMUX_CFGSAIF__SYSCFG_668_ADDR                  \
        PAD_GPIO18_FUNC_SEL_SHIFT                           \
        PAD_GPIO18_FUNC_SEL_MASK
#define PAD_GPIO19_FUNC_SEL                                 \
        SYS_IOMUX_CFGSAIF__SYSCFG_668_ADDR                  \
        PAD_GPIO19_FUNC_SEL_SHIFT                           \
        PAD_GPIO19_FUNC_SEL_MASK
#define PAD_GPIO20_FUNC_SEL                                 \
        SYS_IOMUX_CFGSAIF__SYSCFG_672_ADDR                  \
        PAD_GPIO20_FUNC_SEL_SHIFT                           \
        PAD_GPIO20_FUNC_SEL_MASK
#define PAD_GPIO21_FUNC_SEL                                 \
        SYS_IOMUX_CFGSAIF__SYSCFG_672_ADDR                  \
        PAD_GPIO21_FUNC_SEL_SHIFT                           \
        PAD_GPIO21_FUNC_SEL_MASK
#define PAD_GPIO22_FUNC_SEL                                 \
        SYS_IOMUX_CFGSAIF__SYSCFG_672_ADDR                  \
        PAD_GPIO22_FUNC_SEL_SHIFT                           \
        PAD_GPIO22_FUNC_SEL_MASK
#define PAD_GPIO23_FUNC_SEL                                 \
        SYS_IOMUX_CFGSAIF__SYSCFG_672_ADDR                  \
        PAD_GPIO23_FUNC_SEL_SHIFT                           \
        PAD_GPIO23_FUNC_SEL_MASK
#define PAD_GPIO24_FUNC_SEL                                 \
        SYS_IOMUX_CFGSAIF__SYSCFG_672_ADDR                  \
        PAD_GPIO24_FUNC_SEL_SHIFT                           \
        PAD_GPIO24_FUNC_SEL_MASK
#define PAD_GPIO25_FUNC_SEL                                 \
        SYS_IOMUX_CFGSAIF__SYSCFG_672_ADDR                  \
        PAD_GPIO25_FUNC_SEL_SHIFT                           \
        PAD_GPIO25_FUNC_SEL_MASK
#define PAD_GPIO26_FUNC_SEL                                 \
        SYS_IOMUX_CFGSAIF__SYSCFG_672_ADDR                  \
        PAD_GPIO26_FUNC_SEL_SHIFT                           \
        PAD_GPIO26_FUNC_SEL_MASK
#define PAD_GPIO27_FUNC_SEL                                 \
        SYS_IOMUX_CFGSAIF__SYSCFG_672_ADDR                  \
        PAD_GPIO27_FUNC_SEL_SHIFT                           \
        PAD_GPIO27_FUNC_SEL_MASK
#define PAD_GPIO28_FUNC_SEL                                 \
        SYS_IOMUX_CFGSAIF__SYSCFG_672_ADDR                  \
        PAD_GPIO28_FUNC_SEL_SHIFT                           \
        PAD_GPIO28_FUNC_SEL_MASK
#define PAD_GPIO29_FUNC_SEL                                 \
        SYS_IOMUX_CFGSAIF__SYSCFG_672_ADDR                  \
        PAD_GPIO29_FUNC_SEL_SHIFT                           \
        PAD_GPIO29_FUNC_SEL_MASK
#define PAD_GPIO30_FUNC_SEL                                 \
        SYS_IOMUX_CFGSAIF__SYSCFG_676_ADDR                  \
        PAD_GPIO30_FUNC_SEL_SHIFT                           \
        PAD_GPIO30_FUNC_SEL_MASK
#define PAD_GPIO31_FUNC_SEL                                 \
        SYS_IOMUX_CFGSAIF__SYSCFG_676_ADDR                  \
        PAD_GPIO31_FUNC_SEL_SHIFT                           \
        PAD_GPIO31_FUNC_SEL_MASK
#define PAD_GPIO32_FUNC_SEL                                 \
        SYS_IOMUX_CFGSAIF__SYSCFG_676_ADDR                  \
        PAD_GPIO32_FUNC_SEL_SHIFT                           \
        PAD_GPIO32_FUNC_SEL_MASK
#define PAD_GPIO33_FUNC_SEL                                 \
        SYS_IOMUX_CFGSAIF__SYSCFG_676_ADDR                  \
        PAD_GPIO33_FUNC_SEL_SHIFT                           \
        PAD_GPIO33_FUNC_SEL_MASK
#define PAD_GPIO34_FUNC_SEL                                 \
        SYS_IOMUX_CFGSAIF__SYSCFG_676_ADDR                  \
        PAD_GPIO34_FUNC_SEL_SHIFT                           \
        PAD_GPIO34_FUNC_SEL_MASK
#define PAD_GPIO35_FUNC_SEL                                 \
        SYS_IOMUX_CFGSAIF__SYSCFG_676_ADDR                  \
        PAD_GPIO35_FUNC_SEL_SHIFT                           \
        PAD_GPIO35_FUNC_SEL_MASK
#define PAD_GPIO36_FUNC_SEL                                 \
        SYS_IOMUX_CFGSAIF__SYSCFG_676_ADDR                  \
        PAD_GPIO36_FUNC_SEL_SHIFT                           \
        PAD_GPIO36_FUNC_SEL_MASK
#define PAD_GPIO37_FUNC_SEL                                 \
        SYS_IOMUX_CFGSAIF__SYSCFG_676_ADDR                  \
        PAD_GPIO37_FUNC_SEL_SHIFT                           \
        PAD_GPIO37_FUNC_SEL_MASK
#define PAD_GPIO38_FUNC_SEL                                 \
        SYS_IOMUX_CFGSAIF__SYSCFG_676_ADDR                  \
        PAD_GPIO38_FUNC_SEL_SHIFT                           \
        PAD_GPIO38_FUNC_SEL_MASK
#define PAD_GPIO39_FUNC_SEL                                 \
        SYS_IOMUX_CFGSAIF__SYSCFG_676_ADDR                  \
        PAD_GPIO39_FUNC_SEL_SHIFT                           \
        PAD_GPIO39_FUNC_SEL_MASK
#define PAD_GPIO40_FUNC_SEL                                 \
        SYS_IOMUX_CFGSAIF__SYSCFG_676_ADDR                  \
        PAD_GPIO40_FUNC_SEL_SHIFT                           \
        PAD_GPIO40_FUNC_SEL_MASK
#define PAD_GPIO41_FUNC_SEL                                 \
        SYS_IOMUX_CFGSAIF__SYSCFG_680_ADDR                  \
        PAD_GPIO41_FUNC_SEL_SHIFT                           \
        PAD_GPIO41_FUNC_SEL_MASK
#define PAD_GPIO42_FUNC_SEL                                 \
        SYS_IOMUX_CFGSAIF__SYSCFG_680_ADDR                  \
        PAD_GPIO42_FUNC_SEL_SHIFT                           \
        PAD_GPIO42_FUNC_SEL_MASK
#define PAD_GPIO43_FUNC_SEL                                 \
        SYS_IOMUX_CFGSAIF__SYSCFG_680_ADDR                  \
        PAD_GPIO43_FUNC_SEL_SHIFT                           \
        PAD_GPIO43_FUNC_SEL_MASK
#define PAD_GPIO44_FUNC_SEL                                 \
        SYS_IOMUX_CFGSAIF__SYSCFG_680_ADDR                  \
        PAD_GPIO44_FUNC_SEL_SHIFT                           \
        PAD_GPIO44_FUNC_SEL_MASK
#define PAD_GPIO45_FUNC_SEL                                 \
        SYS_IOMUX_CFGSAIF__SYSCFG_680_ADDR                  \
        PAD_GPIO45_FUNC_SEL_SHIFT                           \
        PAD_GPIO45_FUNC_SEL_MASK
#define PAD_GPIO46_FUNC_SEL                                 \
        SYS_IOMUX_CFGSAIF__SYSCFG_680_ADDR                  \
        PAD_GPIO46_FUNC_SEL_SHIFT                           \
        PAD_GPIO46_FUNC_SEL_MASK
#define PAD_GPIO47_FUNC_SEL                                 \
        SYS_IOMUX_CFGSAIF__SYSCFG_680_ADDR                  \
        PAD_GPIO47_FUNC_SEL_SHIFT                           \
        PAD_GPIO47_FUNC_SEL_MASK
#define PAD_GPIO48_FUNC_SEL                                 \
        SYS_IOMUX_CFGSAIF__SYSCFG_680_ADDR                  \
        PAD_GPIO48_FUNC_SEL_SHIFT                           \
        PAD_GPIO48_FUNC_SEL_MASK
#define PAD_GPIO49_FUNC_SEL                                 \
        SYS_IOMUX_CFGSAIF__SYSCFG_680_ADDR                  \
        PAD_GPIO49_FUNC_SEL_SHIFT                           \
        PAD_GPIO49_FUNC_SEL_MASK
#define PAD_GPIO50_FUNC_SEL                                 \
        SYS_IOMUX_CFGSAIF__SYSCFG_680_ADDR                  \
        PAD_GPIO50_FUNC_SEL_SHIFT                           \
        PAD_GPIO50_FUNC_SEL_MASK
#define PAD_GPIO51_FUNC_SEL                                 \
        SYS_IOMUX_CFGSAIF__SYSCFG_680_ADDR                  \
        PAD_GPIO51_FUNC_SEL_SHIFT                           \
        PAD_GPIO51_FUNC_SEL_MASK
#define PAD_GPIO52_FUNC_SEL                                 \
        SYS_IOMUX_CFGSAIF__SYSCFG_684_ADDR                  \
        PAD_GPIO52_FUNC_SEL_SHIFT                           \
        PAD_GPIO52_FUNC_SEL_MASK
#define PAD_GPIO53_FUNC_SEL                                 \
        SYS_IOMUX_CFGSAIF__SYSCFG_684_ADDR                  \
        PAD_GPIO53_FUNC_SEL_SHIFT                           \
        PAD_GPIO53_FUNC_SEL_MASK
#define PAD_GPIO54_FUNC_SEL                                 \
        SYS_IOMUX_CFGSAIF__SYSCFG_684_ADDR                  \
        PAD_GPIO54_FUNC_SEL_SHIFT                           \
        PAD_GPIO54_FUNC_SEL_MASK
#define PAD_GPIO55_FUNC_SEL                                 \
        SYS_IOMUX_CFGSAIF__SYSCFG_684_ADDR                  \
        PAD_GPIO55_FUNC_SEL_SHIFT                           \
        PAD_GPIO55_FUNC_SEL_MASK
#define PAD_GPIO56_FUNC_SEL                                 \
        SYS_IOMUX_CFGSAIF__SYSCFG_684_ADDR                  \
        PAD_GPIO56_FUNC_SEL_SHIFT                           \
        PAD_GPIO56_FUNC_SEL_MASK
#define PAD_GPIO57_FUNC_SEL                                 \
        SYS_IOMUX_CFGSAIF__SYSCFG_684_ADDR                  \
        PAD_GPIO57_FUNC_SEL_SHIFT                           \
        PAD_GPIO57_FUNC_SEL_MASK
#define PAD_GPIO58_FUNC_SEL                                 \
        SYS_IOMUX_CFGSAIF__SYSCFG_684_ADDR                  \
        PAD_GPIO58_FUNC_SEL_SHIFT                           \
        PAD_GPIO58_FUNC_SEL_MASK
#define PAD_GPIO59_FUNC_SEL                                 \
        SYS_IOMUX_CFGSAIF__SYSCFG_684_ADDR                  \
        PAD_GPIO59_FUNC_SEL_SHIFT                           \
        PAD_GPIO59_FUNC_SEL_MASK
#define PAD_GPIO60_FUNC_SEL                                 \
        SYS_IOMUX_CFGSAIF__SYSCFG_684_ADDR                  \
        PAD_GPIO60_FUNC_SEL_SHIFT                           \
        PAD_GPIO60_FUNC_SEL_MASK
#define PAD_GPIO61_FUNC_SEL                                 \
        SYS_IOMUX_CFGSAIF__SYSCFG_684_ADDR                  \
        PAD_GPIO61_FUNC_SEL_SHIFT                           \
        PAD_GPIO61_FUNC_SEL_MASK
#define PAD_GPIO62_FUNC_SEL                                 \
        SYS_IOMUX_CFGSAIF__SYSCFG_684_ADDR                  \
        PAD_GPIO62_FUNC_SEL_SHIFT                           \
        PAD_GPIO62_FUNC_SEL_MASK
#define PAD_GPIO63_FUNC_SEL                                 \
        SYS_IOMUX_CFGSAIF__SYSCFG_684_ADDR                  \
        PAD_GPIO63_FUNC_SEL_SHIFT                           \
        PAD_GPIO63_FUNC_SEL_MASK
#define PAD_GPIO6_FUNC_SEL                                  \
        SYS_IOMUX_CFGSAIF__SYSCFG_688_ADDR                  \
        PAD_GPIO6_FUNC_SEL_SHIFT                            \
        PAD_GPIO6_FUNC_SEL_MASK
#define PAD_GPIO7_FUNC_SEL                                  \
        SYS_IOMUX_CFGSAIF__SYSCFG_688_ADDR                  \
        PAD_GPIO7_FUNC_SEL_SHIFT                            \
        PAD_GPIO7_FUNC_SEL_MASK
#define PAD_GPIO8_FUNC_SEL                                  \
        SYS_IOMUX_CFGSAIF__SYSCFG_688_ADDR                  \
        PAD_GPIO8_FUNC_SEL_SHIFT                            \
        PAD_GPIO8_FUNC_SEL_MASK
#define PAD_GPIO9_FUNC_SEL                                  \
        SYS_IOMUX_CFGSAIF__SYSCFG_688_ADDR                  \
        PAD_GPIO9_FUNC_SEL_SHIFT                            \
        PAD_GPIO9_FUNC_SEL_MASK
#define U0_DOM_ISP_TOP_U0_VIN_DVP_DATA_C0_FUNC_SEL          \
        SYS_IOMUX_CFGSAIF__SYSCFG_688_ADDR                  \
        U0_DOM_ISP_TOP_U0_VIN_DVP_DATA_C0_FUNC_SEL_SHIFT    \
        U0_DOM_ISP_TOP_U0_VIN_DVP_DATA_C0_FUNC_SEL_MASK
#define U0_DOM_ISP_TOP_U0_VIN_DVP_DATA_C10_FUNC_SEL         \
        SYS_IOMUX_CFGSAIF__SYSCFG_688_ADDR                  \
        U0_DOM_ISP_TOP_U0_VIN_DVP_DATA_C10_FUNC_SEL_SHIFT   \
        U0_DOM_ISP_TOP_U0_VIN_DVP_DATA_C10_FUNC_SEL_MASK
#define U0_DOM_ISP_TOP_U0_VIN_DVP_DATA_C11_FUNC_SEL         \
        SYS_IOMUX_CFGSAIF__SYSCFG_688_ADDR                  \
        U0_DOM_ISP_TOP_U0_VIN_DVP_DATA_C11_FUNC_SEL_SHIFT   \
        U0_DOM_ISP_TOP_U0_VIN_DVP_DATA_C11_FUNC_SEL_MASK
#define U0_DOM_ISP_TOP_U0_VIN_DVP_DATA_C1_FUNC_SEL          \
        SYS_IOMUX_CFGSAIF__SYSCFG_688_ADDR                  \
        U0_DOM_ISP_TOP_U0_VIN_DVP_DATA_C1_FUNC_SEL_SHIFT    \
        U0_DOM_ISP_TOP_U0_VIN_DVP_DATA_C1_FUNC_SEL_MASK
#define U0_DOM_ISP_TOP_U0_VIN_DVP_DATA_C2_FUNC_SEL          \
        SYS_IOMUX_CFGSAIF__SYSCFG_688_ADDR                  \
        U0_DOM_ISP_TOP_U0_VIN_DVP_DATA_C2_FUNC_SEL_SHIFT    \
        U0_DOM_ISP_TOP_U0_VIN_DVP_DATA_C2_FUNC_SEL_MASK
#define U0_DOM_ISP_TOP_U0_VIN_DVP_DATA_C3_FUNC_SEL          \
        SYS_IOMUX_CFGSAIF__SYSCFG_688_ADDR                  \
        U0_DOM_ISP_TOP_U0_VIN_DVP_DATA_C3_FUNC_SEL_SHIFT    \
        U0_DOM_ISP_TOP_U0_VIN_DVP_DATA_C3_FUNC_SEL_MASK
#define U0_DOM_ISP_TOP_U0_VIN_DVP_DATA_C4_FUNC_SEL          \
        SYS_IOMUX_CFGSAIF__SYSCFG_688_ADDR                  \
        U0_DOM_ISP_TOP_U0_VIN_DVP_DATA_C4_FUNC_SEL_SHIFT    \
        U0_DOM_ISP_TOP_U0_VIN_DVP_DATA_C4_FUNC_SEL_MASK
#define U0_DOM_ISP_TOP_U0_VIN_DVP_DATA_C5_FUNC_SEL          \
        SYS_IOMUX_CFGSAIF__SYSCFG_692_ADDR                  \
        U0_DOM_ISP_TOP_U0_VIN_DVP_DATA_C5_FUNC_SEL_SHIFT    \
        U0_DOM_ISP_TOP_U0_VIN_DVP_DATA_C5_FUNC_SEL_MASK
#define U0_DOM_ISP_TOP_U0_VIN_DVP_DATA_C6_FUNC_SEL          \
        SYS_IOMUX_CFGSAIF__SYSCFG_692_ADDR                  \
        U0_DOM_ISP_TOP_U0_VIN_DVP_DATA_C6_FUNC_SEL_SHIFT    \
        U0_DOM_ISP_TOP_U0_VIN_DVP_DATA_C6_FUNC_SEL_MASK
#define U0_DOM_ISP_TOP_U0_VIN_DVP_DATA_C7_FUNC_SEL          \
        SYS_IOMUX_CFGSAIF__SYSCFG_692_ADDR                  \
        U0_DOM_ISP_TOP_U0_VIN_DVP_DATA_C7_FUNC_SEL_SHIFT    \
        U0_DOM_ISP_TOP_U0_VIN_DVP_DATA_C7_FUNC_SEL_MASK
#define U0_DOM_ISP_TOP_U0_VIN_DVP_DATA_C8_FUNC_SEL          \
        SYS_IOMUX_CFGSAIF__SYSCFG_692_ADDR                  \
        U0_DOM_ISP_TOP_U0_VIN_DVP_DATA_C8_FUNC_SEL_SHIFT    \
        U0_DOM_ISP_TOP_U0_VIN_DVP_DATA_C8_FUNC_SEL_MASK
#define U0_DOM_ISP_TOP_U0_VIN_DVP_DATA_C9_FUNC_SEL          \
        SYS_IOMUX_CFGSAIF__SYSCFG_692_ADDR                  \
        U0_DOM_ISP_TOP_U0_VIN_DVP_DATA_C9_FUNC_SEL_SHIFT    \
        U0_DOM_ISP_TOP_U0_VIN_DVP_DATA_C9_FUNC_SEL_MASK
#define U0_DOM_ISP_TOP_U0_VIN_DVP_HVALID_C_FUNC_SEL         \
        SYS_IOMUX_CFGSAIF__SYSCFG_692_ADDR                  \
        U0_DOM_ISP_TOP_U0_VIN_DVP_HVALID_C_FUNC_SEL_SHIFT   \
        U0_DOM_ISP_TOP_U0_VIN_DVP_HVALID_C_FUNC_SEL_MASK
#define U0_DOM_ISP_TOP_U0_VIN_DVP_VVALID_C_FUNC_SEL         \
        SYS_IOMUX_CFGSAIF__SYSCFG_692_ADDR                  \
        U0_DOM_ISP_TOP_U0_VIN_DVP_VVALID_C_FUNC_SEL_SHIFT   \
        U0_DOM_ISP_TOP_U0_VIN_DVP_VVALID_C_FUNC_SEL_MASK
#define U0_SYS_CRG_DVP_CLK_FUNC_SEL                         \
        SYS_IOMUX_CFGSAIF__SYSCFG_692_ADDR                  \
        U0_SYS_CRG_DVP_CLK_FUNC_SEL_SHIFT                   \
        U0_SYS_CRG_DVP_CLK_FUNC_SEL_MASK
/************************sys_iomux***************************/
//aon ioconfig

// POS[0]
#define TESTEN_POS(data)        ((data << 0x0U) & 0x1U)

// SMT[0] POS[1]
#define RSTN_SMT(data)          ((data << 0x0U) & 0x1U)
#define RSTN_POS(data)          ((data << 0x1U) & 0x2U)

// DS[1:0]
#define OSC_DS(data)            ((data << 0x0U) & 0x3U)

//sys ioconfig
// IE[0] DS[2:1] PU[3] PD[4] SLEW[5] SMT[6] POS[7]
#define GPIO_IE(data)           ((data << 0x0U) & 0x1U)
#define GPIO_DS(data)           ((data << 0x1U) & 0x6U)
#define GPIO_PU(data)           ((data << 0x3U) & 0x8U)
#define GPIO_PD(data)           ((data << 0x4U) & 0x10U)
#define GPIO_SLEW(data)         ((data << 0x5U) & 0x20U)
#define GPIO_SMT(data)          ((data << 0x6U) & 0x40U)
#define GPIO_POS(data)          ((data << 0x7U) & 0x80U)

#define IO(config)              ((config) & 0xFF)
#define DOUT(dout)              ((dout) & 0xFF)
#define DOEN(doen)              ((doen) & 0xFF)
#define DIN(din_reg)            ((din_reg) & 0xFF)

//syscon value
#define IO_3_3V                 0 /*00:3.3v*/
#define IO_2_5V                 1 /*01:2.5v*/
#define IO_1_8V                 2 /*10:1.8v*/

#endif
