/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Lochnagar2 register definitions
 *
 * Copyright (c) 2017-2018 Cirrus Logic, Inc. and
 *                         Cirrus Logic International Semiconductor Ltd.
 *
 * Author: Charles Keepax <ckeepax@opensource.cirrus.com>
 */

#ifndef LOCHNAGAR2_REGISTERS_H
#define LOCHNAGAR2_REGISTERS_H

/* Register Addresses */
#define LOCHNAGAR2_CDC_AIF1_CTRL                      0x000D
#define LOCHNAGAR2_CDC_AIF2_CTRL                      0x000E
#define LOCHNAGAR2_CDC_AIF3_CTRL                      0x000F
#define LOCHNAGAR2_DSP_AIF1_CTRL                      0x0010
#define LOCHNAGAR2_DSP_AIF2_CTRL                      0x0011
#define LOCHNAGAR2_PSIA1_CTRL                         0x0012
#define LOCHNAGAR2_PSIA2_CTRL                         0x0013
#define LOCHNAGAR2_GF_AIF3_CTRL                       0x0014
#define LOCHNAGAR2_GF_AIF4_CTRL                       0x0015
#define LOCHNAGAR2_GF_AIF1_CTRL                       0x0016
#define LOCHNAGAR2_GF_AIF2_CTRL                       0x0017
#define LOCHNAGAR2_SPDIF_AIF_CTRL                     0x0018
#define LOCHNAGAR2_USB_AIF1_CTRL                      0x0019
#define LOCHNAGAR2_USB_AIF2_CTRL                      0x001A
#define LOCHNAGAR2_ADAT_AIF_CTRL                      0x001B
#define LOCHNAGAR2_CDC_MCLK1_CTRL                     0x001E
#define LOCHNAGAR2_CDC_MCLK2_CTRL                     0x001F
#define LOCHNAGAR2_DSP_CLKIN_CTRL                     0x0020
#define LOCHNAGAR2_PSIA1_MCLK_CTRL                    0x0021
#define LOCHNAGAR2_PSIA2_MCLK_CTRL                    0x0022
#define LOCHNAGAR2_SPDIF_MCLK_CTRL                    0x0023
#define LOCHNAGAR2_GF_CLKOUT1_CTRL                    0x0024
#define LOCHNAGAR2_GF_CLKOUT2_CTRL                    0x0025
#define LOCHNAGAR2_ADAT_MCLK_CTRL                     0x0026
#define LOCHNAGAR2_SOUNDCARD_MCLK_CTRL                0x0027
#define LOCHNAGAR2_GPIO_FPGA_GPIO1                    0x0031
#define LOCHNAGAR2_GPIO_FPGA_GPIO2                    0x0032
#define LOCHNAGAR2_GPIO_FPGA_GPIO3                    0x0033
#define LOCHNAGAR2_GPIO_FPGA_GPIO4                    0x0034
#define LOCHNAGAR2_GPIO_FPGA_GPIO5                    0x0035
#define LOCHNAGAR2_GPIO_FPGA_GPIO6                    0x0036
#define LOCHNAGAR2_GPIO_CDC_GPIO1                     0x0037
#define LOCHNAGAR2_GPIO_CDC_GPIO2                     0x0038
#define LOCHNAGAR2_GPIO_CDC_GPIO3                     0x0039
#define LOCHNAGAR2_GPIO_CDC_GPIO4                     0x003A
#define LOCHNAGAR2_GPIO_CDC_GPIO5                     0x003B
#define LOCHNAGAR2_GPIO_CDC_GPIO6                     0x003C
#define LOCHNAGAR2_GPIO_CDC_GPIO7                     0x003D
#define LOCHNAGAR2_GPIO_CDC_GPIO8                     0x003E
#define LOCHNAGAR2_GPIO_DSP_GPIO1                     0x003F
#define LOCHNAGAR2_GPIO_DSP_GPIO2                     0x0040
#define LOCHNAGAR2_GPIO_DSP_GPIO3                     0x0041
#define LOCHNAGAR2_GPIO_DSP_GPIO4                     0x0042
#define LOCHNAGAR2_GPIO_DSP_GPIO5                     0x0043
#define LOCHNAGAR2_GPIO_DSP_GPIO6                     0x0044
#define LOCHNAGAR2_GPIO_GF_GPIO2                      0x0045
#define LOCHNAGAR2_GPIO_GF_GPIO3                      0x0046
#define LOCHNAGAR2_GPIO_GF_GPIO7                      0x0047
#define LOCHNAGAR2_GPIO_CDC_AIF1_BCLK                 0x0048
#define LOCHNAGAR2_GPIO_CDC_AIF1_RXDAT                0x0049
#define LOCHNAGAR2_GPIO_CDC_AIF1_LRCLK                0x004A
#define LOCHNAGAR2_GPIO_CDC_AIF1_TXDAT                0x004B
#define LOCHNAGAR2_GPIO_CDC_AIF2_BCLK                 0x004C
#define LOCHNAGAR2_GPIO_CDC_AIF2_RXDAT                0x004D
#define LOCHNAGAR2_GPIO_CDC_AIF2_LRCLK                0x004E
#define LOCHNAGAR2_GPIO_CDC_AIF2_TXDAT                0x004F
#define LOCHNAGAR2_GPIO_CDC_AIF3_BCLK                 0x0050
#define LOCHNAGAR2_GPIO_CDC_AIF3_RXDAT                0x0051
#define LOCHNAGAR2_GPIO_CDC_AIF3_LRCLK                0x0052
#define LOCHNAGAR2_GPIO_CDC_AIF3_TXDAT                0x0053
#define LOCHNAGAR2_GPIO_DSP_AIF1_BCLK                 0x0054
#define LOCHNAGAR2_GPIO_DSP_AIF1_RXDAT                0x0055
#define LOCHNAGAR2_GPIO_DSP_AIF1_LRCLK                0x0056
#define LOCHNAGAR2_GPIO_DSP_AIF1_TXDAT                0x0057
#define LOCHNAGAR2_GPIO_DSP_AIF2_BCLK                 0x0058
#define LOCHNAGAR2_GPIO_DSP_AIF2_RXDAT                0x0059
#define LOCHNAGAR2_GPIO_DSP_AIF2_LRCLK                0x005A
#define LOCHNAGAR2_GPIO_DSP_AIF2_TXDAT                0x005B
#define LOCHNAGAR2_GPIO_PSIA1_BCLK                    0x005C
#define LOCHNAGAR2_GPIO_PSIA1_RXDAT                   0x005D
#define LOCHNAGAR2_GPIO_PSIA1_LRCLK                   0x005E
#define LOCHNAGAR2_GPIO_PSIA1_TXDAT                   0x005F
#define LOCHNAGAR2_GPIO_PSIA2_BCLK                    0x0060
#define LOCHNAGAR2_GPIO_PSIA2_RXDAT                   0x0061
#define LOCHNAGAR2_GPIO_PSIA2_LRCLK                   0x0062
#define LOCHNAGAR2_GPIO_PSIA2_TXDAT                   0x0063
#define LOCHNAGAR2_GPIO_GF_AIF3_BCLK                  0x0064
#define LOCHNAGAR2_GPIO_GF_AIF3_RXDAT                 0x0065
#define LOCHNAGAR2_GPIO_GF_AIF3_LRCLK                 0x0066
#define LOCHNAGAR2_GPIO_GF_AIF3_TXDAT                 0x0067
#define LOCHNAGAR2_GPIO_GF_AIF4_BCLK                  0x0068
#define LOCHNAGAR2_GPIO_GF_AIF4_RXDAT                 0x0069
#define LOCHNAGAR2_GPIO_GF_AIF4_LRCLK                 0x006A
#define LOCHNAGAR2_GPIO_GF_AIF4_TXDAT                 0x006B
#define LOCHNAGAR2_GPIO_GF_AIF1_BCLK                  0x006C
#define LOCHNAGAR2_GPIO_GF_AIF1_RXDAT                 0x006D
#define LOCHNAGAR2_GPIO_GF_AIF1_LRCLK                 0x006E
#define LOCHNAGAR2_GPIO_GF_AIF1_TXDAT                 0x006F
#define LOCHNAGAR2_GPIO_GF_AIF2_BCLK                  0x0070
#define LOCHNAGAR2_GPIO_GF_AIF2_RXDAT                 0x0071
#define LOCHNAGAR2_GPIO_GF_AIF2_LRCLK                 0x0072
#define LOCHNAGAR2_GPIO_GF_AIF2_TXDAT                 0x0073
#define LOCHNAGAR2_GPIO_DSP_UART1_RX                  0x0074
#define LOCHNAGAR2_GPIO_DSP_UART1_TX                  0x0075
#define LOCHNAGAR2_GPIO_DSP_UART2_RX                  0x0076
#define LOCHNAGAR2_GPIO_DSP_UART2_TX                  0x0077
#define LOCHNAGAR2_GPIO_GF_UART2_RX                   0x0078
#define LOCHNAGAR2_GPIO_GF_UART2_TX                   0x0079
#define LOCHNAGAR2_GPIO_USB_UART_RX                   0x007A
#define LOCHNAGAR2_GPIO_CDC_PDMCLK1                   0x007C
#define LOCHNAGAR2_GPIO_CDC_PDMDAT1                   0x007D
#define LOCHNAGAR2_GPIO_CDC_PDMCLK2                   0x007E
#define LOCHNAGAR2_GPIO_CDC_PDMDAT2                   0x007F
#define LOCHNAGAR2_GPIO_CDC_DMICCLK1                  0x0080
#define LOCHNAGAR2_GPIO_CDC_DMICDAT1                  0x0081
#define LOCHNAGAR2_GPIO_CDC_DMICCLK2                  0x0082
#define LOCHNAGAR2_GPIO_CDC_DMICDAT2                  0x0083
#define LOCHNAGAR2_GPIO_CDC_DMICCLK3                  0x0084
#define LOCHNAGAR2_GPIO_CDC_DMICDAT3                  0x0085
#define LOCHNAGAR2_GPIO_CDC_DMICCLK4                  0x0086
#define LOCHNAGAR2_GPIO_CDC_DMICDAT4                  0x0087
#define LOCHNAGAR2_GPIO_DSP_DMICCLK1                  0x0088
#define LOCHNAGAR2_GPIO_DSP_DMICDAT1                  0x0089
#define LOCHNAGAR2_GPIO_DSP_DMICCLK2                  0x008A
#define LOCHNAGAR2_GPIO_DSP_DMICDAT2                  0x008B
#define LOCHNAGAR2_GPIO_I2C2_SCL                      0x008C
#define LOCHNAGAR2_GPIO_I2C2_SDA                      0x008D
#define LOCHNAGAR2_GPIO_I2C3_SCL                      0x008E
#define LOCHNAGAR2_GPIO_I2C3_SDA                      0x008F
#define LOCHNAGAR2_GPIO_I2C4_SCL                      0x0090
#define LOCHNAGAR2_GPIO_I2C4_SDA                      0x0091
#define LOCHNAGAR2_GPIO_DSP_STANDBY                   0x0092
#define LOCHNAGAR2_GPIO_CDC_MCLK1                     0x0093
#define LOCHNAGAR2_GPIO_CDC_MCLK2                     0x0094
#define LOCHNAGAR2_GPIO_DSP_CLKIN                     0x0095
#define LOCHNAGAR2_GPIO_PSIA1_MCLK                    0x0096
#define LOCHNAGAR2_GPIO_PSIA2_MCLK                    0x0097
#define LOCHNAGAR2_GPIO_GF_GPIO1                      0x0098
#define LOCHNAGAR2_GPIO_GF_GPIO5                      0x0099
#define LOCHNAGAR2_GPIO_DSP_GPIO20                    0x009A
#define LOCHNAGAR2_GPIO_CHANNEL1                      0x00B9
#define LOCHNAGAR2_GPIO_CHANNEL2                      0x00BA
#define LOCHNAGAR2_GPIO_CHANNEL3                      0x00BB
#define LOCHNAGAR2_GPIO_CHANNEL4                      0x00BC
#define LOCHNAGAR2_GPIO_CHANNEL5                      0x00BD
#define LOCHNAGAR2_GPIO_CHANNEL6                      0x00BE
#define LOCHNAGAR2_GPIO_CHANNEL7                      0x00BF
#define LOCHNAGAR2_GPIO_CHANNEL8                      0x00C0
#define LOCHNAGAR2_GPIO_CHANNEL9                      0x00C1
#define LOCHNAGAR2_GPIO_CHANNEL10                     0x00C2
#define LOCHNAGAR2_GPIO_CHANNEL11                     0x00C3
#define LOCHNAGAR2_GPIO_CHANNEL12                     0x00C4
#define LOCHNAGAR2_GPIO_CHANNEL13                     0x00C5
#define LOCHNAGAR2_GPIO_CHANNEL14                     0x00C6
#define LOCHNAGAR2_GPIO_CHANNEL15                     0x00C7
#define LOCHNAGAR2_GPIO_CHANNEL16                     0x00C8
#define LOCHNAGAR2_MINICARD_RESETS                    0x00DF
#define LOCHNAGAR2_ANALOGUE_PATH_CTRL1                0x00E3
#define LOCHNAGAR2_ANALOGUE_PATH_CTRL2                0x00E4
#define LOCHNAGAR2_COMMS_CTRL4                        0x00F0
#define LOCHNAGAR2_SPDIF_CTRL                         0x00FE
#define LOCHNAGAR2_IMON_CTRL1                         0x0108
#define LOCHNAGAR2_IMON_CTRL2                         0x0109
#define LOCHNAGAR2_IMON_CTRL3                         0x010A
#define LOCHNAGAR2_IMON_CTRL4                         0x010B
#define LOCHNAGAR2_IMON_DATA1                         0x010C
#define LOCHNAGAR2_IMON_DATA2                         0x010D
#define LOCHNAGAR2_POWER_CTRL                         0x0116
#define LOCHNAGAR2_MICVDD_CTRL1                       0x0119
#define LOCHNAGAR2_MICVDD_CTRL2                       0x011B
#define LOCHNAGAR2_VDDCORE_CDC_CTRL1                  0x011E
#define LOCHNAGAR2_VDDCORE_CDC_CTRL2                  0x0120
#define LOCHNAGAR2_SOUNDCARD_AIF_CTRL                 0x0180

/* (0x000D-0x001B, 0x0180)  CDC_AIF1_CTRL - SOUNCARD_AIF_CTRL */
#define LOCHNAGAR2_AIF_ENA_MASK                       0x8000
#define LOCHNAGAR2_AIF_ENA_SHIFT                          15
#define LOCHNAGAR2_AIF_LRCLK_DIR_MASK                 0x4000
#define LOCHNAGAR2_AIF_LRCLK_DIR_SHIFT                    14
#define LOCHNAGAR2_AIF_BCLK_DIR_MASK                  0x2000
#define LOCHNAGAR2_AIF_BCLK_DIR_SHIFT                     13
#define LOCHNAGAR2_AIF_SRC_MASK                       0x00FF
#define LOCHNAGAR2_AIF_SRC_SHIFT                           0

/* (0x001E - 0x0027)  CDC_MCLK1_CTRL - SOUNDCARD_MCLK_CTRL */
#define LOCHNAGAR2_CLK_ENA_MASK                       0x8000
#define LOCHNAGAR2_CLK_ENA_SHIFT                          15
#define LOCHNAGAR2_CLK_SRC_MASK                       0x00FF
#define LOCHNAGAR2_CLK_SRC_SHIFT                           0

/* (0x0031 - 0x009A)  GPIO_FPGA_GPIO1 - GPIO_DSP_GPIO20 */
#define LOCHNAGAR2_GPIO_SRC_MASK                      0x00FF
#define LOCHNAGAR2_GPIO_SRC_SHIFT                          0

/* (0x00B9 - 0x00C8)  GPIO_CHANNEL1 - GPIO_CHANNEL16 */
#define LOCHNAGAR2_GPIO_CHANNEL_STS_MASK              0x8000
#define LOCHNAGAR2_GPIO_CHANNEL_STS_SHIFT                 15
#define LOCHNAGAR2_GPIO_CHANNEL_SRC_MASK              0x00FF
#define LOCHNAGAR2_GPIO_CHANNEL_SRC_SHIFT                  0

/* (0x00DF)  MINICARD_RESETS */
#define LOCHNAGAR2_DSP_RESET_MASK                     0x0002
#define LOCHNAGAR2_DSP_RESET_SHIFT                         1
#define LOCHNAGAR2_CDC_RESET_MASK                     0x0001
#define LOCHNAGAR2_CDC_RESET_SHIFT                         0

/* (0x00E3)  ANALOGUE_PATH_CTRL1 */
#define LOCHNAGAR2_ANALOGUE_PATH_UPDATE_MASK          0x8000
#define LOCHNAGAR2_ANALOGUE_PATH_UPDATE_SHIFT             15
#define LOCHNAGAR2_ANALOGUE_PATH_UPDATE_STS_MASK      0x4000
#define LOCHNAGAR2_ANALOGUE_PATH_UPDATE_STS_SHIFT         14

/* (0x00E4)  ANALOGUE_PATH_CTRL2 */
#define LOCHNAGAR2_P2_INPUT_BIAS_ENA_MASK             0x0080
#define LOCHNAGAR2_P2_INPUT_BIAS_ENA_SHIFT                 7
#define LOCHNAGAR2_P1_INPUT_BIAS_ENA_MASK             0x0040
#define LOCHNAGAR2_P1_INPUT_BIAS_ENA_SHIFT                 6
#define LOCHNAGAR2_P2_MICBIAS_SRC_MASK                0x0038
#define LOCHNAGAR2_P2_MICBIAS_SRC_SHIFT                    3
#define LOCHNAGAR2_P1_MICBIAS_SRC_MASK                0x0007
#define LOCHNAGAR2_P1_MICBIAS_SRC_SHIFT                    0

/* (0x00F0)  COMMS_CTRL4 */
#define LOCHNAGAR2_CDC_CIF1MODE_MASK                  0x0001
#define LOCHNAGAR2_CDC_CIF1MODE_SHIFT                      0

/* (0x00FE)  SPDIF_CTRL */
#define LOCHNAGAR2_SPDIF_HWMODE_MASK                  0x0008
#define LOCHNAGAR2_SPDIF_HWMODE_SHIFT                      3
#define LOCHNAGAR2_SPDIF_RESET_MASK                   0x0001
#define LOCHNAGAR2_SPDIF_RESET_SHIFT                       0

/* (0x0108)  IMON_CTRL1 */
#define LOCHNAGAR2_IMON_ENA_MASK                      0x8000
#define LOCHNAGAR2_IMON_ENA_SHIFT                         15
#define LOCHNAGAR2_IMON_MEASURED_CHANNELS_MASK        0x03FC
#define LOCHNAGAR2_IMON_MEASURED_CHANNELS_SHIFT            2
#define LOCHNAGAR2_IMON_MODE_SEL_MASK                 0x0003
#define LOCHNAGAR2_IMON_MODE_SEL_SHIFT                     0

/* (0x0109)  IMON_CTRL2 */
#define LOCHNAGAR2_IMON_FSR_MASK                      0x03FF
#define LOCHNAGAR2_IMON_FSR_SHIFT                          0

/* (0x010A)  IMON_CTRL3 */
#define LOCHNAGAR2_IMON_DONE_MASK                     0x0004
#define LOCHNAGAR2_IMON_DONE_SHIFT                         2
#define LOCHNAGAR2_IMON_CONFIGURE_MASK                0x0002
#define LOCHNAGAR2_IMON_CONFIGURE_SHIFT                    1
#define LOCHNAGAR2_IMON_MEASURE_MASK                  0x0001
#define LOCHNAGAR2_IMON_MEASURE_SHIFT                      0

/* (0x010B)  IMON_CTRL4 */
#define LOCHNAGAR2_IMON_DATA_REQ_MASK                 0x0080
#define LOCHNAGAR2_IMON_DATA_REQ_SHIFT                     7
#define LOCHNAGAR2_IMON_CH_SEL_MASK                   0x0070
#define LOCHNAGAR2_IMON_CH_SEL_SHIFT                       4
#define LOCHNAGAR2_IMON_DATA_RDY_MASK                 0x0008
#define LOCHNAGAR2_IMON_DATA_RDY_SHIFT                     3
#define LOCHNAGAR2_IMON_CH_SRC_MASK                   0x0007
#define LOCHNAGAR2_IMON_CH_SRC_SHIFT                       0

/* (0x010C, 0x010D)  IMON_DATA1, IMON_DATA2 */
#define LOCHNAGAR2_IMON_DATA_MASK                     0xFFFF
#define LOCHNAGAR2_IMON_DATA_SHIFT                         0

/* (0x0116)  POWER_CTRL */
#define LOCHNAGAR2_PWR_ENA_MASK                       0x0001
#define LOCHNAGAR2_PWR_ENA_SHIFT                           0

/* (0x0119)  MICVDD_CTRL1 */
#define LOCHNAGAR2_MICVDD_REG_ENA_MASK                0x8000
#define LOCHNAGAR2_MICVDD_REG_ENA_SHIFT                   15

/* (0x011B)  MICVDD_CTRL2 */
#define LOCHNAGAR2_MICVDD_VSEL_MASK                   0x001F
#define LOCHNAGAR2_MICVDD_VSEL_SHIFT                       0

/* (0x011E)  VDDCORE_CDC_CTRL1 */
#define LOCHNAGAR2_VDDCORE_CDC_REG_ENA_MASK           0x8000
#define LOCHNAGAR2_VDDCORE_CDC_REG_ENA_SHIFT              15

/* (0x0120)  VDDCORE_CDC_CTRL2 */
#define LOCHNAGAR2_VDDCORE_CDC_VSEL_MASK              0x007F
#define LOCHNAGAR2_VDDCORE_CDC_VSEL_SHIFT                  0

#endif
