/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Lochnagar1 register definitions
 *
 * Copyright (c) 2017-2018 Cirrus Logic, Inc. and
 *                         Cirrus Logic International Semiconductor Ltd.
 *
 * Author: Charles Keepax <ckeepax@opensource.cirrus.com>
 */

#ifndef LOCHNAGAR1_REGISTERS_H
#define LOCHNAGAR1_REGISTERS_H

/* Register Addresses */
#define LOCHNAGAR1_CDC_AIF1_SEL                       0x0008
#define LOCHNAGAR1_CDC_AIF2_SEL                       0x0009
#define LOCHNAGAR1_CDC_AIF3_SEL                       0x000A
#define LOCHNAGAR1_CDC_MCLK1_SEL                      0x000B
#define LOCHNAGAR1_CDC_MCLK2_SEL                      0x000C
#define LOCHNAGAR1_CDC_AIF_CTRL1                      0x000D
#define LOCHNAGAR1_CDC_AIF_CTRL2                      0x000E
#define LOCHNAGAR1_EXT_AIF_CTRL                       0x000F
#define LOCHNAGAR1_DSP_AIF1_SEL                       0x0010
#define LOCHNAGAR1_DSP_AIF2_SEL                       0x0011
#define LOCHNAGAR1_DSP_CLKIN_SEL                      0x0012
#define LOCHNAGAR1_DSP_AIF                            0x0013
#define LOCHNAGAR1_GF_AIF1                            0x0014
#define LOCHNAGAR1_GF_AIF2                            0x0015
#define LOCHNAGAR1_PSIA_AIF                           0x0016
#define LOCHNAGAR1_PSIA1_SEL                          0x0017
#define LOCHNAGAR1_PSIA2_SEL                          0x0018
#define LOCHNAGAR1_SPDIF_AIF_SEL                      0x0019
#define LOCHNAGAR1_GF_AIF3_SEL                        0x001C
#define LOCHNAGAR1_GF_AIF4_SEL                        0x001D
#define LOCHNAGAR1_GF_CLKOUT1_SEL                     0x001E
#define LOCHNAGAR1_GF_AIF1_SEL                        0x001F
#define LOCHNAGAR1_GF_AIF2_SEL                        0x0020
#define LOCHNAGAR1_GF_GPIO2                           0x0026
#define LOCHNAGAR1_GF_GPIO3                           0x0027
#define LOCHNAGAR1_GF_GPIO7                           0x0028
#define LOCHNAGAR1_RST                                0x0029
#define LOCHNAGAR1_LED1                               0x002A
#define LOCHNAGAR1_LED2                               0x002B
#define LOCHNAGAR1_I2C_CTRL                           0x0046

/*
 * (0x0008 - 0x000C, 0x0010 - 0x0012, 0x0017 - 0x0020)
 * CDC_AIF1_SEL - GF_AIF2_SEL
 */
#define LOCHNAGAR1_SRC_MASK                             0xFF
#define LOCHNAGAR1_SRC_SHIFT                               0

/* (0x000D)  CDC_AIF_CTRL1 */
#define LOCHNAGAR1_CDC_AIF2_LRCLK_DIR_MASK              0x40
#define LOCHNAGAR1_CDC_AIF2_LRCLK_DIR_SHIFT                6
#define LOCHNAGAR1_CDC_AIF2_BCLK_DIR_MASK               0x20
#define LOCHNAGAR1_CDC_AIF2_BCLK_DIR_SHIFT                 5
#define LOCHNAGAR1_CDC_AIF2_ENA_MASK                    0x10
#define LOCHNAGAR1_CDC_AIF2_ENA_SHIFT                      4
#define LOCHNAGAR1_CDC_AIF1_LRCLK_DIR_MASK              0x04
#define LOCHNAGAR1_CDC_AIF1_LRCLK_DIR_SHIFT                2
#define LOCHNAGAR1_CDC_AIF1_BCLK_DIR_MASK               0x02
#define LOCHNAGAR1_CDC_AIF1_BCLK_DIR_SHIFT                 1
#define LOCHNAGAR1_CDC_AIF1_ENA_MASK                    0x01
#define LOCHNAGAR1_CDC_AIF1_ENA_SHIFT                      0

/* (0x000E)  CDC_AIF_CTRL2 */
#define LOCHNAGAR1_CDC_AIF3_LRCLK_DIR_MASK              0x40
#define LOCHNAGAR1_CDC_AIF3_LRCLK_DIR_SHIFT                6
#define LOCHNAGAR1_CDC_AIF3_BCLK_DIR_MASK               0x20
#define LOCHNAGAR1_CDC_AIF3_BCLK_DIR_SHIFT                 5
#define LOCHNAGAR1_CDC_AIF3_ENA_MASK                    0x10
#define LOCHNAGAR1_CDC_AIF3_ENA_SHIFT                      4
#define LOCHNAGAR1_CDC_MCLK1_ENA_MASK                   0x02
#define LOCHNAGAR1_CDC_MCLK1_ENA_SHIFT                     1
#define LOCHNAGAR1_CDC_MCLK2_ENA_MASK                   0x01
#define LOCHNAGAR1_CDC_MCLK2_ENA_SHIFT                     0

/* (0x000F)  EXT_AIF_CTRL */
#define LOCHNAGAR1_SPDIF_AIF_LRCLK_DIR_MASK             0x20
#define LOCHNAGAR1_SPDIF_AIF_LRCLK_DIR_SHIFT               5
#define LOCHNAGAR1_SPDIF_AIF_BCLK_DIR_MASK              0x10
#define LOCHNAGAR1_SPDIF_AIF_BCLK_DIR_SHIFT                4
#define LOCHNAGAR1_SPDIF_AIF_ENA_MASK                   0x08
#define LOCHNAGAR1_SPDIF_AIF_ENA_SHIFT                     3

/* (0x0013)  DSP_AIF */
#define LOCHNAGAR1_DSP_AIF2_LRCLK_DIR_MASK              0x40
#define LOCHNAGAR1_DSP_AIF2_LRCLK_DIR_SHIFT                6
#define LOCHNAGAR1_DSP_AIF2_BCLK_DIR_MASK               0x20
#define LOCHNAGAR1_DSP_AIF2_BCLK_DIR_SHIFT                 5
#define LOCHNAGAR1_DSP_AIF2_ENA_MASK                    0x10
#define LOCHNAGAR1_DSP_AIF2_ENA_SHIFT                      4
#define LOCHNAGAR1_DSP_CLKIN_ENA_MASK                   0x08
#define LOCHNAGAR1_DSP_CLKIN_ENA_SHIFT                     3
#define LOCHNAGAR1_DSP_AIF1_LRCLK_DIR_MASK              0x04
#define LOCHNAGAR1_DSP_AIF1_LRCLK_DIR_SHIFT                2
#define LOCHNAGAR1_DSP_AIF1_BCLK_DIR_MASK               0x02
#define LOCHNAGAR1_DSP_AIF1_BCLK_DIR_SHIFT                 1
#define LOCHNAGAR1_DSP_AIF1_ENA_MASK                    0x01
#define LOCHNAGAR1_DSP_AIF1_ENA_SHIFT                      0

/* (0x0014)  GF_AIF1 */
#define LOCHNAGAR1_GF_CLKOUT1_ENA_MASK                  0x40
#define LOCHNAGAR1_GF_CLKOUT1_ENA_SHIFT                    6
#define LOCHNAGAR1_GF_AIF3_LRCLK_DIR_MASK               0x20
#define LOCHNAGAR1_GF_AIF3_LRCLK_DIR_SHIFT                 5
#define LOCHNAGAR1_GF_AIF3_BCLK_DIR_MASK                0x10
#define LOCHNAGAR1_GF_AIF3_BCLK_DIR_SHIFT                  4
#define LOCHNAGAR1_GF_AIF3_ENA_MASK                     0x08
#define LOCHNAGAR1_GF_AIF3_ENA_SHIFT                       3
#define LOCHNAGAR1_GF_AIF1_LRCLK_DIR_MASK               0x04
#define LOCHNAGAR1_GF_AIF1_LRCLK_DIR_SHIFT                 2
#define LOCHNAGAR1_GF_AIF1_BCLK_DIR_MASK                0x02
#define LOCHNAGAR1_GF_AIF1_BCLK_DIR_SHIFT                  1
#define LOCHNAGAR1_GF_AIF1_ENA_MASK                     0x01
#define LOCHNAGAR1_GF_AIF1_ENA_SHIFT                       0

/* (0x0015)  GF_AIF2 */
#define LOCHNAGAR1_GF_AIF4_LRCLK_DIR_MASK               0x20
#define LOCHNAGAR1_GF_AIF4_LRCLK_DIR_SHIFT                 5
#define LOCHNAGAR1_GF_AIF4_BCLK_DIR_MASK                0x10
#define LOCHNAGAR1_GF_AIF4_BCLK_DIR_SHIFT                  4
#define LOCHNAGAR1_GF_AIF4_ENA_MASK                     0x08
#define LOCHNAGAR1_GF_AIF4_ENA_SHIFT                       3
#define LOCHNAGAR1_GF_AIF2_LRCLK_DIR_MASK               0x04
#define LOCHNAGAR1_GF_AIF2_LRCLK_DIR_SHIFT                 2
#define LOCHNAGAR1_GF_AIF2_BCLK_DIR_MASK                0x02
#define LOCHNAGAR1_GF_AIF2_BCLK_DIR_SHIFT                  1
#define LOCHNAGAR1_GF_AIF2_ENA_MASK                     0x01
#define LOCHNAGAR1_GF_AIF2_ENA_SHIFT                       0

/* (0x0016)  PSIA_AIF */
#define LOCHNAGAR1_PSIA2_LRCLK_DIR_MASK                 0x40
#define LOCHNAGAR1_PSIA2_LRCLK_DIR_SHIFT                   6
#define LOCHNAGAR1_PSIA2_BCLK_DIR_MASK                  0x20
#define LOCHNAGAR1_PSIA2_BCLK_DIR_SHIFT                    5
#define LOCHNAGAR1_PSIA2_ENA_MASK                       0x10
#define LOCHNAGAR1_PSIA2_ENA_SHIFT                         4
#define LOCHNAGAR1_PSIA1_LRCLK_DIR_MASK                 0x04
#define LOCHNAGAR1_PSIA1_LRCLK_DIR_SHIFT                   2
#define LOCHNAGAR1_PSIA1_BCLK_DIR_MASK                  0x02
#define LOCHNAGAR1_PSIA1_BCLK_DIR_SHIFT                    1
#define LOCHNAGAR1_PSIA1_ENA_MASK                       0x01
#define LOCHNAGAR1_PSIA1_ENA_SHIFT                         0

/* (0x0029)  RST */
#define LOCHNAGAR1_DSP_RESET_MASK                       0x02
#define LOCHNAGAR1_DSP_RESET_SHIFT                         1
#define LOCHNAGAR1_CDC_RESET_MASK                       0x01
#define LOCHNAGAR1_CDC_RESET_SHIFT                         0

/* (0x0046)  I2C_CTRL */
#define LOCHNAGAR1_CDC_CIF_MODE_MASK                    0x01
#define LOCHNAGAR1_CDC_CIF_MODE_SHIFT                      0

#endif
