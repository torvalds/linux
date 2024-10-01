/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
 * wm8400 private definitions.
 *
 * Copyright 2008 Wolfson Microelectronics plc
 */

#ifndef __LINUX_MFD_WM8400_PRIV_H
#define __LINUX_MFD_WM8400_PRIV_H

#include <linux/mfd/wm8400.h>
#include <linux/mutex.h>
#include <linux/platform_device.h>
#include <linux/regmap.h>

#define WM8400_REGISTER_COUNT 0x55

struct wm8400 {
	struct device *dev;
	struct regmap *regmap;

	struct platform_device regulators[6];
};

/*
 * Register values.
 */
#define WM8400_RESET_ID                         0x00
#define WM8400_ID                               0x01
#define WM8400_POWER_MANAGEMENT_1               0x02
#define WM8400_POWER_MANAGEMENT_2               0x03
#define WM8400_POWER_MANAGEMENT_3               0x04
#define WM8400_AUDIO_INTERFACE_1                0x05
#define WM8400_AUDIO_INTERFACE_2                0x06
#define WM8400_CLOCKING_1                       0x07
#define WM8400_CLOCKING_2                       0x08
#define WM8400_AUDIO_INTERFACE_3                0x09
#define WM8400_AUDIO_INTERFACE_4                0x0A
#define WM8400_DAC_CTRL                         0x0B
#define WM8400_LEFT_DAC_DIGITAL_VOLUME          0x0C
#define WM8400_RIGHT_DAC_DIGITAL_VOLUME         0x0D
#define WM8400_DIGITAL_SIDE_TONE                0x0E
#define WM8400_ADC_CTRL                         0x0F
#define WM8400_LEFT_ADC_DIGITAL_VOLUME          0x10
#define WM8400_RIGHT_ADC_DIGITAL_VOLUME         0x11
#define WM8400_GPIO_CTRL_1                      0x12
#define WM8400_GPIO1_GPIO2                      0x13
#define WM8400_GPIO3_GPIO4                      0x14
#define WM8400_GPIO5_GPIO6                      0x15
#define WM8400_GPIOCTRL_2                       0x16
#define WM8400_GPIO_POL                         0x17
#define WM8400_LEFT_LINE_INPUT_1_2_VOLUME       0x18
#define WM8400_LEFT_LINE_INPUT_3_4_VOLUME       0x19
#define WM8400_RIGHT_LINE_INPUT_1_2_VOLUME      0x1A
#define WM8400_RIGHT_LINE_INPUT_3_4_VOLUME      0x1B
#define WM8400_LEFT_OUTPUT_VOLUME               0x1C
#define WM8400_RIGHT_OUTPUT_VOLUME              0x1D
#define WM8400_LINE_OUTPUTS_VOLUME              0x1E
#define WM8400_OUT3_4_VOLUME                    0x1F
#define WM8400_LEFT_OPGA_VOLUME                 0x20
#define WM8400_RIGHT_OPGA_VOLUME                0x21
#define WM8400_SPEAKER_VOLUME                   0x22
#define WM8400_CLASSD1                          0x23
#define WM8400_CLASSD3                          0x25
#define WM8400_INPUT_MIXER1                     0x27
#define WM8400_INPUT_MIXER2                     0x28
#define WM8400_INPUT_MIXER3                     0x29
#define WM8400_INPUT_MIXER4                     0x2A
#define WM8400_INPUT_MIXER5                     0x2B
#define WM8400_INPUT_MIXER6                     0x2C
#define WM8400_OUTPUT_MIXER1                    0x2D
#define WM8400_OUTPUT_MIXER2                    0x2E
#define WM8400_OUTPUT_MIXER3                    0x2F
#define WM8400_OUTPUT_MIXER4                    0x30
#define WM8400_OUTPUT_MIXER5                    0x31
#define WM8400_OUTPUT_MIXER6                    0x32
#define WM8400_OUT3_4_MIXER                     0x33
#define WM8400_LINE_MIXER1                      0x34
#define WM8400_LINE_MIXER2                      0x35
#define WM8400_SPEAKER_MIXER                    0x36
#define WM8400_ADDITIONAL_CONTROL               0x37
#define WM8400_ANTIPOP1                         0x38
#define WM8400_ANTIPOP2                         0x39
#define WM8400_MICBIAS                          0x3A
#define WM8400_FLL_CONTROL_1                    0x3C
#define WM8400_FLL_CONTROL_2                    0x3D
#define WM8400_FLL_CONTROL_3                    0x3E
#define WM8400_FLL_CONTROL_4                    0x3F
#define WM8400_LDO1_CONTROL                     0x41
#define WM8400_LDO2_CONTROL                     0x42
#define WM8400_LDO3_CONTROL                     0x43
#define WM8400_LDO4_CONTROL                     0x44
#define WM8400_DCDC1_CONTROL_1                  0x46
#define WM8400_DCDC1_CONTROL_2                  0x47
#define WM8400_DCDC2_CONTROL_1                  0x48
#define WM8400_DCDC2_CONTROL_2                  0x49
#define WM8400_INTERFACE                        0x4B
#define WM8400_PM_GENERAL                       0x4C
#define WM8400_PM_SHUTDOWN_CONTROL              0x4E
#define WM8400_INTERRUPT_STATUS_1               0x4F
#define WM8400_INTERRUPT_STATUS_1_MASK          0x50
#define WM8400_INTERRUPT_LEVELS                 0x51
#define WM8400_SHUTDOWN_REASON                  0x52
#define WM8400_LINE_CIRCUITS                    0x54

/*
 * Field Definitions.
 */

/*
 * R0 (0x00) - Reset/ID
 */
#define WM8400_SW_RESET_CHIP_ID_MASK            0xFFFF  /* SW_RESET/CHIP_ID - [15:0] */
#define WM8400_SW_RESET_CHIP_ID_SHIFT                0  /* SW_RESET/CHIP_ID - [15:0] */
#define WM8400_SW_RESET_CHIP_ID_WIDTH               16  /* SW_RESET/CHIP_ID - [15:0] */

/*
 * R1 (0x01) - ID
 */
#define WM8400_CHIP_REV_MASK                    0x7000  /* CHIP_REV - [14:12] */
#define WM8400_CHIP_REV_SHIFT                       12  /* CHIP_REV - [14:12] */
#define WM8400_CHIP_REV_WIDTH                        3  /* CHIP_REV - [14:12] */

/*
 * R18 (0x12) - GPIO CTRL 1
 */
#define WM8400_IRQ                              0x1000  /* IRQ */
#define WM8400_IRQ_MASK                         0x1000  /* IRQ */
#define WM8400_IRQ_SHIFT                            12  /* IRQ */
#define WM8400_IRQ_WIDTH                             1  /* IRQ */
#define WM8400_TEMPOK                           0x0800  /* TEMPOK */
#define WM8400_TEMPOK_MASK                      0x0800  /* TEMPOK */
#define WM8400_TEMPOK_SHIFT                         11  /* TEMPOK */
#define WM8400_TEMPOK_WIDTH                          1  /* TEMPOK */
#define WM8400_MIC1SHRT                         0x0400  /* MIC1SHRT */
#define WM8400_MIC1SHRT_MASK                    0x0400  /* MIC1SHRT */
#define WM8400_MIC1SHRT_SHIFT                       10  /* MIC1SHRT */
#define WM8400_MIC1SHRT_WIDTH                        1  /* MIC1SHRT */
#define WM8400_MIC1DET                          0x0200  /* MIC1DET */
#define WM8400_MIC1DET_MASK                     0x0200  /* MIC1DET */
#define WM8400_MIC1DET_SHIFT                         9  /* MIC1DET */
#define WM8400_MIC1DET_WIDTH                         1  /* MIC1DET */
#define WM8400_FLL_LCK                          0x0100  /* FLL_LCK */
#define WM8400_FLL_LCK_MASK                     0x0100  /* FLL_LCK */
#define WM8400_FLL_LCK_SHIFT                         8  /* FLL_LCK */
#define WM8400_FLL_LCK_WIDTH                         1  /* FLL_LCK */
#define WM8400_GPIO_STATUS_MASK                 0x00FF  /* GPIO_STATUS - [7:0] */
#define WM8400_GPIO_STATUS_SHIFT                     0  /* GPIO_STATUS - [7:0] */
#define WM8400_GPIO_STATUS_WIDTH                     8  /* GPIO_STATUS - [7:0] */

/*
 * R19 (0x13) - GPIO1 & GPIO2
 */
#define WM8400_GPIO2_DEB_ENA                    0x8000  /* GPIO2_DEB_ENA */
#define WM8400_GPIO2_DEB_ENA_MASK               0x8000  /* GPIO2_DEB_ENA */
#define WM8400_GPIO2_DEB_ENA_SHIFT                  15  /* GPIO2_DEB_ENA */
#define WM8400_GPIO2_DEB_ENA_WIDTH                   1  /* GPIO2_DEB_ENA */
#define WM8400_GPIO2_IRQ_ENA                    0x4000  /* GPIO2_IRQ_ENA */
#define WM8400_GPIO2_IRQ_ENA_MASK               0x4000  /* GPIO2_IRQ_ENA */
#define WM8400_GPIO2_IRQ_ENA_SHIFT                  14  /* GPIO2_IRQ_ENA */
#define WM8400_GPIO2_IRQ_ENA_WIDTH                   1  /* GPIO2_IRQ_ENA */
#define WM8400_GPIO2_PU                         0x2000  /* GPIO2_PU */
#define WM8400_GPIO2_PU_MASK                    0x2000  /* GPIO2_PU */
#define WM8400_GPIO2_PU_SHIFT                       13  /* GPIO2_PU */
#define WM8400_GPIO2_PU_WIDTH                        1  /* GPIO2_PU */
#define WM8400_GPIO2_PD                         0x1000  /* GPIO2_PD */
#define WM8400_GPIO2_PD_MASK                    0x1000  /* GPIO2_PD */
#define WM8400_GPIO2_PD_SHIFT                       12  /* GPIO2_PD */
#define WM8400_GPIO2_PD_WIDTH                        1  /* GPIO2_PD */
#define WM8400_GPIO2_SEL_MASK                   0x0F00  /* GPIO2_SEL - [11:8] */
#define WM8400_GPIO2_SEL_SHIFT                       8  /* GPIO2_SEL - [11:8] */
#define WM8400_GPIO2_SEL_WIDTH                       4  /* GPIO2_SEL - [11:8] */
#define WM8400_GPIO1_DEB_ENA                    0x0080  /* GPIO1_DEB_ENA */
#define WM8400_GPIO1_DEB_ENA_MASK               0x0080  /* GPIO1_DEB_ENA */
#define WM8400_GPIO1_DEB_ENA_SHIFT                   7  /* GPIO1_DEB_ENA */
#define WM8400_GPIO1_DEB_ENA_WIDTH                   1  /* GPIO1_DEB_ENA */
#define WM8400_GPIO1_IRQ_ENA                    0x0040  /* GPIO1_IRQ_ENA */
#define WM8400_GPIO1_IRQ_ENA_MASK               0x0040  /* GPIO1_IRQ_ENA */
#define WM8400_GPIO1_IRQ_ENA_SHIFT                   6  /* GPIO1_IRQ_ENA */
#define WM8400_GPIO1_IRQ_ENA_WIDTH                   1  /* GPIO1_IRQ_ENA */
#define WM8400_GPIO1_PU                         0x0020  /* GPIO1_PU */
#define WM8400_GPIO1_PU_MASK                    0x0020  /* GPIO1_PU */
#define WM8400_GPIO1_PU_SHIFT                        5  /* GPIO1_PU */
#define WM8400_GPIO1_PU_WIDTH                        1  /* GPIO1_PU */
#define WM8400_GPIO1_PD                         0x0010  /* GPIO1_PD */
#define WM8400_GPIO1_PD_MASK                    0x0010  /* GPIO1_PD */
#define WM8400_GPIO1_PD_SHIFT                        4  /* GPIO1_PD */
#define WM8400_GPIO1_PD_WIDTH                        1  /* GPIO1_PD */
#define WM8400_GPIO1_SEL_MASK                   0x000F  /* GPIO1_SEL - [3:0] */
#define WM8400_GPIO1_SEL_SHIFT                       0  /* GPIO1_SEL - [3:0] */
#define WM8400_GPIO1_SEL_WIDTH                       4  /* GPIO1_SEL - [3:0] */

/*
 * R20 (0x14) - GPIO3 & GPIO4
 */
#define WM8400_GPIO4_DEB_ENA                    0x8000  /* GPIO4_DEB_ENA */
#define WM8400_GPIO4_DEB_ENA_MASK               0x8000  /* GPIO4_DEB_ENA */
#define WM8400_GPIO4_DEB_ENA_SHIFT                  15  /* GPIO4_DEB_ENA */
#define WM8400_GPIO4_DEB_ENA_WIDTH                   1  /* GPIO4_DEB_ENA */
#define WM8400_GPIO4_IRQ_ENA                    0x4000  /* GPIO4_IRQ_ENA */
#define WM8400_GPIO4_IRQ_ENA_MASK               0x4000  /* GPIO4_IRQ_ENA */
#define WM8400_GPIO4_IRQ_ENA_SHIFT                  14  /* GPIO4_IRQ_ENA */
#define WM8400_GPIO4_IRQ_ENA_WIDTH                   1  /* GPIO4_IRQ_ENA */
#define WM8400_GPIO4_PU                         0x2000  /* GPIO4_PU */
#define WM8400_GPIO4_PU_MASK                    0x2000  /* GPIO4_PU */
#define WM8400_GPIO4_PU_SHIFT                       13  /* GPIO4_PU */
#define WM8400_GPIO4_PU_WIDTH                        1  /* GPIO4_PU */
#define WM8400_GPIO4_PD                         0x1000  /* GPIO4_PD */
#define WM8400_GPIO4_PD_MASK                    0x1000  /* GPIO4_PD */
#define WM8400_GPIO4_PD_SHIFT                       12  /* GPIO4_PD */
#define WM8400_GPIO4_PD_WIDTH                        1  /* GPIO4_PD */
#define WM8400_GPIO4_SEL_MASK                   0x0F00  /* GPIO4_SEL - [11:8] */
#define WM8400_GPIO4_SEL_SHIFT                       8  /* GPIO4_SEL - [11:8] */
#define WM8400_GPIO4_SEL_WIDTH                       4  /* GPIO4_SEL - [11:8] */
#define WM8400_GPIO3_DEB_ENA                    0x0080  /* GPIO3_DEB_ENA */
#define WM8400_GPIO3_DEB_ENA_MASK               0x0080  /* GPIO3_DEB_ENA */
#define WM8400_GPIO3_DEB_ENA_SHIFT                   7  /* GPIO3_DEB_ENA */
#define WM8400_GPIO3_DEB_ENA_WIDTH                   1  /* GPIO3_DEB_ENA */
#define WM8400_GPIO3_IRQ_ENA                    0x0040  /* GPIO3_IRQ_ENA */
#define WM8400_GPIO3_IRQ_ENA_MASK               0x0040  /* GPIO3_IRQ_ENA */
#define WM8400_GPIO3_IRQ_ENA_SHIFT                   6  /* GPIO3_IRQ_ENA */
#define WM8400_GPIO3_IRQ_ENA_WIDTH                   1  /* GPIO3_IRQ_ENA */
#define WM8400_GPIO3_PU                         0x0020  /* GPIO3_PU */
#define WM8400_GPIO3_PU_MASK                    0x0020  /* GPIO3_PU */
#define WM8400_GPIO3_PU_SHIFT                        5  /* GPIO3_PU */
#define WM8400_GPIO3_PU_WIDTH                        1  /* GPIO3_PU */
#define WM8400_GPIO3_PD                         0x0010  /* GPIO3_PD */
#define WM8400_GPIO3_PD_MASK                    0x0010  /* GPIO3_PD */
#define WM8400_GPIO3_PD_SHIFT                        4  /* GPIO3_PD */
#define WM8400_GPIO3_PD_WIDTH                        1  /* GPIO3_PD */
#define WM8400_GPIO3_SEL_MASK                   0x000F  /* GPIO3_SEL - [3:0] */
#define WM8400_GPIO3_SEL_SHIFT                       0  /* GPIO3_SEL - [3:0] */
#define WM8400_GPIO3_SEL_WIDTH                       4  /* GPIO3_SEL - [3:0] */

/*
 * R21 (0x15) - GPIO5 & GPIO6
 */
#define WM8400_GPIO6_DEB_ENA                    0x8000  /* GPIO6_DEB_ENA */
#define WM8400_GPIO6_DEB_ENA_MASK               0x8000  /* GPIO6_DEB_ENA */
#define WM8400_GPIO6_DEB_ENA_SHIFT                  15  /* GPIO6_DEB_ENA */
#define WM8400_GPIO6_DEB_ENA_WIDTH                   1  /* GPIO6_DEB_ENA */
#define WM8400_GPIO6_IRQ_ENA                    0x4000  /* GPIO6_IRQ_ENA */
#define WM8400_GPIO6_IRQ_ENA_MASK               0x4000  /* GPIO6_IRQ_ENA */
#define WM8400_GPIO6_IRQ_ENA_SHIFT                  14  /* GPIO6_IRQ_ENA */
#define WM8400_GPIO6_IRQ_ENA_WIDTH                   1  /* GPIO6_IRQ_ENA */
#define WM8400_GPIO6_PU                         0x2000  /* GPIO6_PU */
#define WM8400_GPIO6_PU_MASK                    0x2000  /* GPIO6_PU */
#define WM8400_GPIO6_PU_SHIFT                       13  /* GPIO6_PU */
#define WM8400_GPIO6_PU_WIDTH                        1  /* GPIO6_PU */
#define WM8400_GPIO6_PD                         0x1000  /* GPIO6_PD */
#define WM8400_GPIO6_PD_MASK                    0x1000  /* GPIO6_PD */
#define WM8400_GPIO6_PD_SHIFT                       12  /* GPIO6_PD */
#define WM8400_GPIO6_PD_WIDTH                        1  /* GPIO6_PD */
#define WM8400_GPIO6_SEL_MASK                   0x0F00  /* GPIO6_SEL - [11:8] */
#define WM8400_GPIO6_SEL_SHIFT                       8  /* GPIO6_SEL - [11:8] */
#define WM8400_GPIO6_SEL_WIDTH                       4  /* GPIO6_SEL - [11:8] */
#define WM8400_GPIO5_DEB_ENA                    0x0080  /* GPIO5_DEB_ENA */
#define WM8400_GPIO5_DEB_ENA_MASK               0x0080  /* GPIO5_DEB_ENA */
#define WM8400_GPIO5_DEB_ENA_SHIFT                   7  /* GPIO5_DEB_ENA */
#define WM8400_GPIO5_DEB_ENA_WIDTH                   1  /* GPIO5_DEB_ENA */
#define WM8400_GPIO5_IRQ_ENA                    0x0040  /* GPIO5_IRQ_ENA */
#define WM8400_GPIO5_IRQ_ENA_MASK               0x0040  /* GPIO5_IRQ_ENA */
#define WM8400_GPIO5_IRQ_ENA_SHIFT                   6  /* GPIO5_IRQ_ENA */
#define WM8400_GPIO5_IRQ_ENA_WIDTH                   1  /* GPIO5_IRQ_ENA */
#define WM8400_GPIO5_PU                         0x0020  /* GPIO5_PU */
#define WM8400_GPIO5_PU_MASK                    0x0020  /* GPIO5_PU */
#define WM8400_GPIO5_PU_SHIFT                        5  /* GPIO5_PU */
#define WM8400_GPIO5_PU_WIDTH                        1  /* GPIO5_PU */
#define WM8400_GPIO5_PD                         0x0010  /* GPIO5_PD */
#define WM8400_GPIO5_PD_MASK                    0x0010  /* GPIO5_PD */
#define WM8400_GPIO5_PD_SHIFT                        4  /* GPIO5_PD */
#define WM8400_GPIO5_PD_WIDTH                        1  /* GPIO5_PD */
#define WM8400_GPIO5_SEL_MASK                   0x000F  /* GPIO5_SEL - [3:0] */
#define WM8400_GPIO5_SEL_SHIFT                       0  /* GPIO5_SEL - [3:0] */
#define WM8400_GPIO5_SEL_WIDTH                       4  /* GPIO5_SEL - [3:0] */

/*
 * R22 (0x16) - GPIOCTRL 2
 */
#define WM8400_TEMPOK_IRQ_ENA                   0x0800  /* TEMPOK_IRQ_ENA */
#define WM8400_TEMPOK_IRQ_ENA_MASK              0x0800  /* TEMPOK_IRQ_ENA */
#define WM8400_TEMPOK_IRQ_ENA_SHIFT                 11  /* TEMPOK_IRQ_ENA */
#define WM8400_TEMPOK_IRQ_ENA_WIDTH                  1  /* TEMPOK_IRQ_ENA */
#define WM8400_MIC1SHRT_IRQ_ENA                 0x0400  /* MIC1SHRT_IRQ_ENA */
#define WM8400_MIC1SHRT_IRQ_ENA_MASK            0x0400  /* MIC1SHRT_IRQ_ENA */
#define WM8400_MIC1SHRT_IRQ_ENA_SHIFT               10  /* MIC1SHRT_IRQ_ENA */
#define WM8400_MIC1SHRT_IRQ_ENA_WIDTH                1  /* MIC1SHRT_IRQ_ENA */
#define WM8400_MIC1DET_IRQ_ENA                  0x0200  /* MIC1DET_IRQ_ENA */
#define WM8400_MIC1DET_IRQ_ENA_MASK             0x0200  /* MIC1DET_IRQ_ENA */
#define WM8400_MIC1DET_IRQ_ENA_SHIFT                 9  /* MIC1DET_IRQ_ENA */
#define WM8400_MIC1DET_IRQ_ENA_WIDTH                 1  /* MIC1DET_IRQ_ENA */
#define WM8400_FLL_LCK_IRQ_ENA                  0x0100  /* FLL_LCK_IRQ_ENA */
#define WM8400_FLL_LCK_IRQ_ENA_MASK             0x0100  /* FLL_LCK_IRQ_ENA */
#define WM8400_FLL_LCK_IRQ_ENA_SHIFT                 8  /* FLL_LCK_IRQ_ENA */
#define WM8400_FLL_LCK_IRQ_ENA_WIDTH                 1  /* FLL_LCK_IRQ_ENA */
#define WM8400_GPI8_DEB_ENA                     0x0080  /* GPI8_DEB_ENA */
#define WM8400_GPI8_DEB_ENA_MASK                0x0080  /* GPI8_DEB_ENA */
#define WM8400_GPI8_DEB_ENA_SHIFT                    7  /* GPI8_DEB_ENA */
#define WM8400_GPI8_DEB_ENA_WIDTH                    1  /* GPI8_DEB_ENA */
#define WM8400_GPI8_IRQ_ENA                     0x0040  /* GPI8_IRQ_ENA */
#define WM8400_GPI8_IRQ_ENA_MASK                0x0040  /* GPI8_IRQ_ENA */
#define WM8400_GPI8_IRQ_ENA_SHIFT                    6  /* GPI8_IRQ_ENA */
#define WM8400_GPI8_IRQ_ENA_WIDTH                    1  /* GPI8_IRQ_ENA */
#define WM8400_GPI8_ENA                         0x0010  /* GPI8_ENA */
#define WM8400_GPI8_ENA_MASK                    0x0010  /* GPI8_ENA */
#define WM8400_GPI8_ENA_SHIFT                        4  /* GPI8_ENA */
#define WM8400_GPI8_ENA_WIDTH                        1  /* GPI8_ENA */
#define WM8400_GPI7_DEB_ENA                     0x0008  /* GPI7_DEB_ENA */
#define WM8400_GPI7_DEB_ENA_MASK                0x0008  /* GPI7_DEB_ENA */
#define WM8400_GPI7_DEB_ENA_SHIFT                    3  /* GPI7_DEB_ENA */
#define WM8400_GPI7_DEB_ENA_WIDTH                    1  /* GPI7_DEB_ENA */
#define WM8400_GPI7_IRQ_ENA                     0x0004  /* GPI7_IRQ_ENA */
#define WM8400_GPI7_IRQ_ENA_MASK                0x0004  /* GPI7_IRQ_ENA */
#define WM8400_GPI7_IRQ_ENA_SHIFT                    2  /* GPI7_IRQ_ENA */
#define WM8400_GPI7_IRQ_ENA_WIDTH                    1  /* GPI7_IRQ_ENA */
#define WM8400_GPI7_ENA                         0x0001  /* GPI7_ENA */
#define WM8400_GPI7_ENA_MASK                    0x0001  /* GPI7_ENA */
#define WM8400_GPI7_ENA_SHIFT                        0  /* GPI7_ENA */
#define WM8400_GPI7_ENA_WIDTH                        1  /* GPI7_ENA */

/*
 * R23 (0x17) - GPIO_POL
 */
#define WM8400_IRQ_INV                          0x1000  /* IRQ_INV */
#define WM8400_IRQ_INV_MASK                     0x1000  /* IRQ_INV */
#define WM8400_IRQ_INV_SHIFT                        12  /* IRQ_INV */
#define WM8400_IRQ_INV_WIDTH                         1  /* IRQ_INV */
#define WM8400_TEMPOK_POL                       0x0800  /* TEMPOK_POL */
#define WM8400_TEMPOK_POL_MASK                  0x0800  /* TEMPOK_POL */
#define WM8400_TEMPOK_POL_SHIFT                     11  /* TEMPOK_POL */
#define WM8400_TEMPOK_POL_WIDTH                      1  /* TEMPOK_POL */
#define WM8400_MIC1SHRT_POL                     0x0400  /* MIC1SHRT_POL */
#define WM8400_MIC1SHRT_POL_MASK                0x0400  /* MIC1SHRT_POL */
#define WM8400_MIC1SHRT_POL_SHIFT                   10  /* MIC1SHRT_POL */
#define WM8400_MIC1SHRT_POL_WIDTH                    1  /* MIC1SHRT_POL */
#define WM8400_MIC1DET_POL                      0x0200  /* MIC1DET_POL */
#define WM8400_MIC1DET_POL_MASK                 0x0200  /* MIC1DET_POL */
#define WM8400_MIC1DET_POL_SHIFT                     9  /* MIC1DET_POL */
#define WM8400_MIC1DET_POL_WIDTH                     1  /* MIC1DET_POL */
#define WM8400_FLL_LCK_POL                      0x0100  /* FLL_LCK_POL */
#define WM8400_FLL_LCK_POL_MASK                 0x0100  /* FLL_LCK_POL */
#define WM8400_FLL_LCK_POL_SHIFT                     8  /* FLL_LCK_POL */
#define WM8400_FLL_LCK_POL_WIDTH                     1  /* FLL_LCK_POL */
#define WM8400_GPIO_POL_MASK                    0x00FF  /* GPIO_POL - [7:0] */
#define WM8400_GPIO_POL_SHIFT                        0  /* GPIO_POL - [7:0] */
#define WM8400_GPIO_POL_WIDTH                        8  /* GPIO_POL - [7:0] */

/*
 * R65 (0x41) - LDO 1 Control
 */
#define WM8400_LDO1_ENA                         0x8000  /* LDO1_ENA */
#define WM8400_LDO1_ENA_MASK                    0x8000  /* LDO1_ENA */
#define WM8400_LDO1_ENA_SHIFT                       15  /* LDO1_ENA */
#define WM8400_LDO1_ENA_WIDTH                        1  /* LDO1_ENA */
#define WM8400_LDO1_SWI                         0x4000  /* LDO1_SWI */
#define WM8400_LDO1_SWI_MASK                    0x4000  /* LDO1_SWI */
#define WM8400_LDO1_SWI_SHIFT                       14  /* LDO1_SWI */
#define WM8400_LDO1_SWI_WIDTH                        1  /* LDO1_SWI */
#define WM8400_LDO1_OPFLT                       0x1000  /* LDO1_OPFLT */
#define WM8400_LDO1_OPFLT_MASK                  0x1000  /* LDO1_OPFLT */
#define WM8400_LDO1_OPFLT_SHIFT                     12  /* LDO1_OPFLT */
#define WM8400_LDO1_OPFLT_WIDTH                      1  /* LDO1_OPFLT */
#define WM8400_LDO1_ERRACT                      0x0800  /* LDO1_ERRACT */
#define WM8400_LDO1_ERRACT_MASK                 0x0800  /* LDO1_ERRACT */
#define WM8400_LDO1_ERRACT_SHIFT                    11  /* LDO1_ERRACT */
#define WM8400_LDO1_ERRACT_WIDTH                     1  /* LDO1_ERRACT */
#define WM8400_LDO1_HIB_MODE                    0x0400  /* LDO1_HIB_MODE */
#define WM8400_LDO1_HIB_MODE_MASK               0x0400  /* LDO1_HIB_MODE */
#define WM8400_LDO1_HIB_MODE_SHIFT                  10  /* LDO1_HIB_MODE */
#define WM8400_LDO1_HIB_MODE_WIDTH                   1  /* LDO1_HIB_MODE */
#define WM8400_LDO1_VIMG_MASK                   0x03E0  /* LDO1_VIMG - [9:5] */
#define WM8400_LDO1_VIMG_SHIFT                       5  /* LDO1_VIMG - [9:5] */
#define WM8400_LDO1_VIMG_WIDTH                       5  /* LDO1_VIMG - [9:5] */
#define WM8400_LDO1_VSEL_MASK                   0x001F  /* LDO1_VSEL - [4:0] */
#define WM8400_LDO1_VSEL_SHIFT                       0  /* LDO1_VSEL - [4:0] */
#define WM8400_LDO1_VSEL_WIDTH                       5  /* LDO1_VSEL - [4:0] */

/*
 * R66 (0x42) - LDO 2 Control
 */
#define WM8400_LDO2_ENA                         0x8000  /* LDO2_ENA */
#define WM8400_LDO2_ENA_MASK                    0x8000  /* LDO2_ENA */
#define WM8400_LDO2_ENA_SHIFT                       15  /* LDO2_ENA */
#define WM8400_LDO2_ENA_WIDTH                        1  /* LDO2_ENA */
#define WM8400_LDO2_SWI                         0x4000  /* LDO2_SWI */
#define WM8400_LDO2_SWI_MASK                    0x4000  /* LDO2_SWI */
#define WM8400_LDO2_SWI_SHIFT                       14  /* LDO2_SWI */
#define WM8400_LDO2_SWI_WIDTH                        1  /* LDO2_SWI */
#define WM8400_LDO2_OPFLT                       0x1000  /* LDO2_OPFLT */
#define WM8400_LDO2_OPFLT_MASK                  0x1000  /* LDO2_OPFLT */
#define WM8400_LDO2_OPFLT_SHIFT                     12  /* LDO2_OPFLT */
#define WM8400_LDO2_OPFLT_WIDTH                      1  /* LDO2_OPFLT */
#define WM8400_LDO2_ERRACT                      0x0800  /* LDO2_ERRACT */
#define WM8400_LDO2_ERRACT_MASK                 0x0800  /* LDO2_ERRACT */
#define WM8400_LDO2_ERRACT_SHIFT                    11  /* LDO2_ERRACT */
#define WM8400_LDO2_ERRACT_WIDTH                     1  /* LDO2_ERRACT */
#define WM8400_LDO2_HIB_MODE                    0x0400  /* LDO2_HIB_MODE */
#define WM8400_LDO2_HIB_MODE_MASK               0x0400  /* LDO2_HIB_MODE */
#define WM8400_LDO2_HIB_MODE_SHIFT                  10  /* LDO2_HIB_MODE */
#define WM8400_LDO2_HIB_MODE_WIDTH                   1  /* LDO2_HIB_MODE */
#define WM8400_LDO2_VIMG_MASK                   0x03E0  /* LDO2_VIMG - [9:5] */
#define WM8400_LDO2_VIMG_SHIFT                       5  /* LDO2_VIMG - [9:5] */
#define WM8400_LDO2_VIMG_WIDTH                       5  /* LDO2_VIMG - [9:5] */
#define WM8400_LDO2_VSEL_MASK                   0x001F  /* LDO2_VSEL - [4:0] */
#define WM8400_LDO2_VSEL_SHIFT                       0  /* LDO2_VSEL - [4:0] */
#define WM8400_LDO2_VSEL_WIDTH                       5  /* LDO2_VSEL - [4:0] */

/*
 * R67 (0x43) - LDO 3 Control
 */
#define WM8400_LDO3_ENA                         0x8000  /* LDO3_ENA */
#define WM8400_LDO3_ENA_MASK                    0x8000  /* LDO3_ENA */
#define WM8400_LDO3_ENA_SHIFT                       15  /* LDO3_ENA */
#define WM8400_LDO3_ENA_WIDTH                        1  /* LDO3_ENA */
#define WM8400_LDO3_SWI                         0x4000  /* LDO3_SWI */
#define WM8400_LDO3_SWI_MASK                    0x4000  /* LDO3_SWI */
#define WM8400_LDO3_SWI_SHIFT                       14  /* LDO3_SWI */
#define WM8400_LDO3_SWI_WIDTH                        1  /* LDO3_SWI */
#define WM8400_LDO3_OPFLT                       0x1000  /* LDO3_OPFLT */
#define WM8400_LDO3_OPFLT_MASK                  0x1000  /* LDO3_OPFLT */
#define WM8400_LDO3_OPFLT_SHIFT                     12  /* LDO3_OPFLT */
#define WM8400_LDO3_OPFLT_WIDTH                      1  /* LDO3_OPFLT */
#define WM8400_LDO3_ERRACT                      0x0800  /* LDO3_ERRACT */
#define WM8400_LDO3_ERRACT_MASK                 0x0800  /* LDO3_ERRACT */
#define WM8400_LDO3_ERRACT_SHIFT                    11  /* LDO3_ERRACT */
#define WM8400_LDO3_ERRACT_WIDTH                     1  /* LDO3_ERRACT */
#define WM8400_LDO3_HIB_MODE                    0x0400  /* LDO3_HIB_MODE */
#define WM8400_LDO3_HIB_MODE_MASK               0x0400  /* LDO3_HIB_MODE */
#define WM8400_LDO3_HIB_MODE_SHIFT                  10  /* LDO3_HIB_MODE */
#define WM8400_LDO3_HIB_MODE_WIDTH                   1  /* LDO3_HIB_MODE */
#define WM8400_LDO3_VIMG_MASK                   0x03E0  /* LDO3_VIMG - [9:5] */
#define WM8400_LDO3_VIMG_SHIFT                       5  /* LDO3_VIMG - [9:5] */
#define WM8400_LDO3_VIMG_WIDTH                       5  /* LDO3_VIMG - [9:5] */
#define WM8400_LDO3_VSEL_MASK                   0x001F  /* LDO3_VSEL - [4:0] */
#define WM8400_LDO3_VSEL_SHIFT                       0  /* LDO3_VSEL - [4:0] */
#define WM8400_LDO3_VSEL_WIDTH                       5  /* LDO3_VSEL - [4:0] */

/*
 * R68 (0x44) - LDO 4 Control
 */
#define WM8400_LDO4_ENA                         0x8000  /* LDO4_ENA */
#define WM8400_LDO4_ENA_MASK                    0x8000  /* LDO4_ENA */
#define WM8400_LDO4_ENA_SHIFT                       15  /* LDO4_ENA */
#define WM8400_LDO4_ENA_WIDTH                        1  /* LDO4_ENA */
#define WM8400_LDO4_SWI                         0x4000  /* LDO4_SWI */
#define WM8400_LDO4_SWI_MASK                    0x4000  /* LDO4_SWI */
#define WM8400_LDO4_SWI_SHIFT                       14  /* LDO4_SWI */
#define WM8400_LDO4_SWI_WIDTH                        1  /* LDO4_SWI */
#define WM8400_LDO4_OPFLT                       0x1000  /* LDO4_OPFLT */
#define WM8400_LDO4_OPFLT_MASK                  0x1000  /* LDO4_OPFLT */
#define WM8400_LDO4_OPFLT_SHIFT                     12  /* LDO4_OPFLT */
#define WM8400_LDO4_OPFLT_WIDTH                      1  /* LDO4_OPFLT */
#define WM8400_LDO4_ERRACT                      0x0800  /* LDO4_ERRACT */
#define WM8400_LDO4_ERRACT_MASK                 0x0800  /* LDO4_ERRACT */
#define WM8400_LDO4_ERRACT_SHIFT                    11  /* LDO4_ERRACT */
#define WM8400_LDO4_ERRACT_WIDTH                     1  /* LDO4_ERRACT */
#define WM8400_LDO4_HIB_MODE                    0x0400  /* LDO4_HIB_MODE */
#define WM8400_LDO4_HIB_MODE_MASK               0x0400  /* LDO4_HIB_MODE */
#define WM8400_LDO4_HIB_MODE_SHIFT                  10  /* LDO4_HIB_MODE */
#define WM8400_LDO4_HIB_MODE_WIDTH                   1  /* LDO4_HIB_MODE */
#define WM8400_LDO4_VIMG_MASK                   0x03E0  /* LDO4_VIMG - [9:5] */
#define WM8400_LDO4_VIMG_SHIFT                       5  /* LDO4_VIMG - [9:5] */
#define WM8400_LDO4_VIMG_WIDTH                       5  /* LDO4_VIMG - [9:5] */
#define WM8400_LDO4_VSEL_MASK                   0x001F  /* LDO4_VSEL - [4:0] */
#define WM8400_LDO4_VSEL_SHIFT                       0  /* LDO4_VSEL - [4:0] */
#define WM8400_LDO4_VSEL_WIDTH                       5  /* LDO4_VSEL - [4:0] */

/*
 * R70 (0x46) - DCDC1 Control 1
 */
#define WM8400_DC1_ENA                          0x8000  /* DC1_ENA */
#define WM8400_DC1_ENA_MASK                     0x8000  /* DC1_ENA */
#define WM8400_DC1_ENA_SHIFT                        15  /* DC1_ENA */
#define WM8400_DC1_ENA_WIDTH                         1  /* DC1_ENA */
#define WM8400_DC1_ACTIVE                       0x4000  /* DC1_ACTIVE */
#define WM8400_DC1_ACTIVE_MASK                  0x4000  /* DC1_ACTIVE */
#define WM8400_DC1_ACTIVE_SHIFT                     14  /* DC1_ACTIVE */
#define WM8400_DC1_ACTIVE_WIDTH                      1  /* DC1_ACTIVE */
#define WM8400_DC1_SLEEP                        0x2000  /* DC1_SLEEP */
#define WM8400_DC1_SLEEP_MASK                   0x2000  /* DC1_SLEEP */
#define WM8400_DC1_SLEEP_SHIFT                      13  /* DC1_SLEEP */
#define WM8400_DC1_SLEEP_WIDTH                       1  /* DC1_SLEEP */
#define WM8400_DC1_OPFLT                        0x1000  /* DC1_OPFLT */
#define WM8400_DC1_OPFLT_MASK                   0x1000  /* DC1_OPFLT */
#define WM8400_DC1_OPFLT_SHIFT                      12  /* DC1_OPFLT */
#define WM8400_DC1_OPFLT_WIDTH                       1  /* DC1_OPFLT */
#define WM8400_DC1_ERRACT                       0x0800  /* DC1_ERRACT */
#define WM8400_DC1_ERRACT_MASK                  0x0800  /* DC1_ERRACT */
#define WM8400_DC1_ERRACT_SHIFT                     11  /* DC1_ERRACT */
#define WM8400_DC1_ERRACT_WIDTH                      1  /* DC1_ERRACT */
#define WM8400_DC1_HIB_MODE                     0x0400  /* DC1_HIB_MODE */
#define WM8400_DC1_HIB_MODE_MASK                0x0400  /* DC1_HIB_MODE */
#define WM8400_DC1_HIB_MODE_SHIFT                   10  /* DC1_HIB_MODE */
#define WM8400_DC1_HIB_MODE_WIDTH                    1  /* DC1_HIB_MODE */
#define WM8400_DC1_SOFTST_MASK                  0x0300  /* DC1_SOFTST - [9:8] */
#define WM8400_DC1_SOFTST_SHIFT                      8  /* DC1_SOFTST - [9:8] */
#define WM8400_DC1_SOFTST_WIDTH                      2  /* DC1_SOFTST - [9:8] */
#define WM8400_DC1_OV_PROT                      0x0080  /* DC1_OV_PROT */
#define WM8400_DC1_OV_PROT_MASK                 0x0080  /* DC1_OV_PROT */
#define WM8400_DC1_OV_PROT_SHIFT                     7  /* DC1_OV_PROT */
#define WM8400_DC1_OV_PROT_WIDTH                     1  /* DC1_OV_PROT */
#define WM8400_DC1_VSEL_MASK                    0x007F  /* DC1_VSEL - [6:0] */
#define WM8400_DC1_VSEL_SHIFT                        0  /* DC1_VSEL - [6:0] */
#define WM8400_DC1_VSEL_WIDTH                        7  /* DC1_VSEL - [6:0] */

/*
 * R71 (0x47) - DCDC1 Control 2
 */
#define WM8400_DC1_FRC_PWM                      0x2000  /* DC1_FRC_PWM */
#define WM8400_DC1_FRC_PWM_MASK                 0x2000  /* DC1_FRC_PWM */
#define WM8400_DC1_FRC_PWM_SHIFT                    13  /* DC1_FRC_PWM */
#define WM8400_DC1_FRC_PWM_WIDTH                     1  /* DC1_FRC_PWM */
#define WM8400_DC1_STBY_LIM_MASK                0x0300  /* DC1_STBY_LIM - [9:8] */
#define WM8400_DC1_STBY_LIM_SHIFT                    8  /* DC1_STBY_LIM - [9:8] */
#define WM8400_DC1_STBY_LIM_WIDTH                    2  /* DC1_STBY_LIM - [9:8] */
#define WM8400_DC1_ACT_LIM                      0x0080  /* DC1_ACT_LIM */
#define WM8400_DC1_ACT_LIM_MASK                 0x0080  /* DC1_ACT_LIM */
#define WM8400_DC1_ACT_LIM_SHIFT                     7  /* DC1_ACT_LIM */
#define WM8400_DC1_ACT_LIM_WIDTH                     1  /* DC1_ACT_LIM */
#define WM8400_DC1_VIMG_MASK                    0x007F  /* DC1_VIMG - [6:0] */
#define WM8400_DC1_VIMG_SHIFT                        0  /* DC1_VIMG - [6:0] */
#define WM8400_DC1_VIMG_WIDTH                        7  /* DC1_VIMG - [6:0] */

/*
 * R72 (0x48) - DCDC2 Control 1
 */
#define WM8400_DC2_ENA                          0x8000  /* DC2_ENA */
#define WM8400_DC2_ENA_MASK                     0x8000  /* DC2_ENA */
#define WM8400_DC2_ENA_SHIFT                        15  /* DC2_ENA */
#define WM8400_DC2_ENA_WIDTH                         1  /* DC2_ENA */
#define WM8400_DC2_ACTIVE                       0x4000  /* DC2_ACTIVE */
#define WM8400_DC2_ACTIVE_MASK                  0x4000  /* DC2_ACTIVE */
#define WM8400_DC2_ACTIVE_SHIFT                     14  /* DC2_ACTIVE */
#define WM8400_DC2_ACTIVE_WIDTH                      1  /* DC2_ACTIVE */
#define WM8400_DC2_SLEEP                        0x2000  /* DC2_SLEEP */
#define WM8400_DC2_SLEEP_MASK                   0x2000  /* DC2_SLEEP */
#define WM8400_DC2_SLEEP_SHIFT                      13  /* DC2_SLEEP */
#define WM8400_DC2_SLEEP_WIDTH                       1  /* DC2_SLEEP */
#define WM8400_DC2_OPFLT                        0x1000  /* DC2_OPFLT */
#define WM8400_DC2_OPFLT_MASK                   0x1000  /* DC2_OPFLT */
#define WM8400_DC2_OPFLT_SHIFT                      12  /* DC2_OPFLT */
#define WM8400_DC2_OPFLT_WIDTH                       1  /* DC2_OPFLT */
#define WM8400_DC2_ERRACT                       0x0800  /* DC2_ERRACT */
#define WM8400_DC2_ERRACT_MASK                  0x0800  /* DC2_ERRACT */
#define WM8400_DC2_ERRACT_SHIFT                     11  /* DC2_ERRACT */
#define WM8400_DC2_ERRACT_WIDTH                      1  /* DC2_ERRACT */
#define WM8400_DC2_HIB_MODE                     0x0400  /* DC2_HIB_MODE */
#define WM8400_DC2_HIB_MODE_MASK                0x0400  /* DC2_HIB_MODE */
#define WM8400_DC2_HIB_MODE_SHIFT                   10  /* DC2_HIB_MODE */
#define WM8400_DC2_HIB_MODE_WIDTH                    1  /* DC2_HIB_MODE */
#define WM8400_DC2_SOFTST_MASK                  0x0300  /* DC2_SOFTST - [9:8] */
#define WM8400_DC2_SOFTST_SHIFT                      8  /* DC2_SOFTST - [9:8] */
#define WM8400_DC2_SOFTST_WIDTH                      2  /* DC2_SOFTST - [9:8] */
#define WM8400_DC2_OV_PROT                      0x0080  /* DC2_OV_PROT */
#define WM8400_DC2_OV_PROT_MASK                 0x0080  /* DC2_OV_PROT */
#define WM8400_DC2_OV_PROT_SHIFT                     7  /* DC2_OV_PROT */
#define WM8400_DC2_OV_PROT_WIDTH                     1  /* DC2_OV_PROT */
#define WM8400_DC2_VSEL_MASK                    0x007F  /* DC2_VSEL - [6:0] */
#define WM8400_DC2_VSEL_SHIFT                        0  /* DC2_VSEL - [6:0] */
#define WM8400_DC2_VSEL_WIDTH                        7  /* DC2_VSEL - [6:0] */

/*
 * R73 (0x49) - DCDC2 Control 2
 */
#define WM8400_DC2_FRC_PWM                      0x2000  /* DC2_FRC_PWM */
#define WM8400_DC2_FRC_PWM_MASK                 0x2000  /* DC2_FRC_PWM */
#define WM8400_DC2_FRC_PWM_SHIFT                    13  /* DC2_FRC_PWM */
#define WM8400_DC2_FRC_PWM_WIDTH                     1  /* DC2_FRC_PWM */
#define WM8400_DC2_STBY_LIM_MASK                0x0300  /* DC2_STBY_LIM - [9:8] */
#define WM8400_DC2_STBY_LIM_SHIFT                    8  /* DC2_STBY_LIM - [9:8] */
#define WM8400_DC2_STBY_LIM_WIDTH                    2  /* DC2_STBY_LIM - [9:8] */
#define WM8400_DC2_ACT_LIM                      0x0080  /* DC2_ACT_LIM */
#define WM8400_DC2_ACT_LIM_MASK                 0x0080  /* DC2_ACT_LIM */
#define WM8400_DC2_ACT_LIM_SHIFT                     7  /* DC2_ACT_LIM */
#define WM8400_DC2_ACT_LIM_WIDTH                     1  /* DC2_ACT_LIM */
#define WM8400_DC2_VIMG_MASK                    0x007F  /* DC2_VIMG - [6:0] */
#define WM8400_DC2_VIMG_SHIFT                        0  /* DC2_VIMG - [6:0] */
#define WM8400_DC2_VIMG_WIDTH                        7  /* DC2_VIMG - [6:0] */

/*
 * R75 (0x4B) - Interface
 */
#define WM8400_AUTOINC                          0x0008  /* AUTOINC */
#define WM8400_AUTOINC_MASK                     0x0008  /* AUTOINC */
#define WM8400_AUTOINC_SHIFT                         3  /* AUTOINC */
#define WM8400_AUTOINC_WIDTH                         1  /* AUTOINC */
#define WM8400_ARA_ENA                          0x0004  /* ARA_ENA */
#define WM8400_ARA_ENA_MASK                     0x0004  /* ARA_ENA */
#define WM8400_ARA_ENA_SHIFT                         2  /* ARA_ENA */
#define WM8400_ARA_ENA_WIDTH                         1  /* ARA_ENA */
#define WM8400_SPI_CFG                          0x0002  /* SPI_CFG */
#define WM8400_SPI_CFG_MASK                     0x0002  /* SPI_CFG */
#define WM8400_SPI_CFG_SHIFT                         1  /* SPI_CFG */
#define WM8400_SPI_CFG_WIDTH                         1  /* SPI_CFG */

/*
 * R76 (0x4C) - PM GENERAL
 */
#define WM8400_CODEC_SOFTST                     0x8000  /* CODEC_SOFTST */
#define WM8400_CODEC_SOFTST_MASK                0x8000  /* CODEC_SOFTST */
#define WM8400_CODEC_SOFTST_SHIFT                   15  /* CODEC_SOFTST */
#define WM8400_CODEC_SOFTST_WIDTH                    1  /* CODEC_SOFTST */
#define WM8400_CODEC_SOFTSD                     0x4000  /* CODEC_SOFTSD */
#define WM8400_CODEC_SOFTSD_MASK                0x4000  /* CODEC_SOFTSD */
#define WM8400_CODEC_SOFTSD_SHIFT                   14  /* CODEC_SOFTSD */
#define WM8400_CODEC_SOFTSD_WIDTH                    1  /* CODEC_SOFTSD */
#define WM8400_CHIP_SOFTSD                      0x2000  /* CHIP_SOFTSD */
#define WM8400_CHIP_SOFTSD_MASK                 0x2000  /* CHIP_SOFTSD */
#define WM8400_CHIP_SOFTSD_SHIFT                    13  /* CHIP_SOFTSD */
#define WM8400_CHIP_SOFTSD_WIDTH                     1  /* CHIP_SOFTSD */
#define WM8400_DSLEEP1_POL                      0x0008  /* DSLEEP1_POL */
#define WM8400_DSLEEP1_POL_MASK                 0x0008  /* DSLEEP1_POL */
#define WM8400_DSLEEP1_POL_SHIFT                     3  /* DSLEEP1_POL */
#define WM8400_DSLEEP1_POL_WIDTH                     1  /* DSLEEP1_POL */
#define WM8400_DSLEEP2_POL                      0x0004  /* DSLEEP2_POL */
#define WM8400_DSLEEP2_POL_MASK                 0x0004  /* DSLEEP2_POL */
#define WM8400_DSLEEP2_POL_SHIFT                     2  /* DSLEEP2_POL */
#define WM8400_DSLEEP2_POL_WIDTH                     1  /* DSLEEP2_POL */
#define WM8400_PWR_STATE_MASK                   0x0003  /* PWR_STATE - [1:0] */
#define WM8400_PWR_STATE_SHIFT                       0  /* PWR_STATE - [1:0] */
#define WM8400_PWR_STATE_WIDTH                       2  /* PWR_STATE - [1:0] */

/*
 * R78 (0x4E) - PM Shutdown Control
 */
#define WM8400_CHIP_GT150_ERRACT                0x0200  /* CHIP_GT150_ERRACT */
#define WM8400_CHIP_GT150_ERRACT_MASK           0x0200  /* CHIP_GT150_ERRACT */
#define WM8400_CHIP_GT150_ERRACT_SHIFT               9  /* CHIP_GT150_ERRACT */
#define WM8400_CHIP_GT150_ERRACT_WIDTH               1  /* CHIP_GT150_ERRACT */
#define WM8400_CHIP_GT115_ERRACT                0x0100  /* CHIP_GT115_ERRACT */
#define WM8400_CHIP_GT115_ERRACT_MASK           0x0100  /* CHIP_GT115_ERRACT */
#define WM8400_CHIP_GT115_ERRACT_SHIFT               8  /* CHIP_GT115_ERRACT */
#define WM8400_CHIP_GT115_ERRACT_WIDTH               1  /* CHIP_GT115_ERRACT */
#define WM8400_LINE_CMP_ERRACT                  0x0080  /* LINE_CMP_ERRACT */
#define WM8400_LINE_CMP_ERRACT_MASK             0x0080  /* LINE_CMP_ERRACT */
#define WM8400_LINE_CMP_ERRACT_SHIFT                 7  /* LINE_CMP_ERRACT */
#define WM8400_LINE_CMP_ERRACT_WIDTH                 1  /* LINE_CMP_ERRACT */
#define WM8400_UVLO_ERRACT                      0x0040  /* UVLO_ERRACT */
#define WM8400_UVLO_ERRACT_MASK                 0x0040  /* UVLO_ERRACT */
#define WM8400_UVLO_ERRACT_SHIFT                     6  /* UVLO_ERRACT */
#define WM8400_UVLO_ERRACT_WIDTH                     1  /* UVLO_ERRACT */

/*
 * R79 (0x4F) - Interrupt Status 1
 */
#define WM8400_MICD_CINT                        0x8000  /* MICD_CINT */
#define WM8400_MICD_CINT_MASK                   0x8000  /* MICD_CINT */
#define WM8400_MICD_CINT_SHIFT                      15  /* MICD_CINT */
#define WM8400_MICD_CINT_WIDTH                       1  /* MICD_CINT */
#define WM8400_MICSCD_CINT                      0x4000  /* MICSCD_CINT */
#define WM8400_MICSCD_CINT_MASK                 0x4000  /* MICSCD_CINT */
#define WM8400_MICSCD_CINT_SHIFT                    14  /* MICSCD_CINT */
#define WM8400_MICSCD_CINT_WIDTH                     1  /* MICSCD_CINT */
#define WM8400_JDL_CINT                         0x2000  /* JDL_CINT */
#define WM8400_JDL_CINT_MASK                    0x2000  /* JDL_CINT */
#define WM8400_JDL_CINT_SHIFT                       13  /* JDL_CINT */
#define WM8400_JDL_CINT_WIDTH                        1  /* JDL_CINT */
#define WM8400_JDR_CINT                         0x1000  /* JDR_CINT */
#define WM8400_JDR_CINT_MASK                    0x1000  /* JDR_CINT */
#define WM8400_JDR_CINT_SHIFT                       12  /* JDR_CINT */
#define WM8400_JDR_CINT_WIDTH                        1  /* JDR_CINT */
#define WM8400_CODEC_SEQ_END_EINT               0x0800  /* CODEC_SEQ_END_EINT */
#define WM8400_CODEC_SEQ_END_EINT_MASK          0x0800  /* CODEC_SEQ_END_EINT */
#define WM8400_CODEC_SEQ_END_EINT_SHIFT             11  /* CODEC_SEQ_END_EINT */
#define WM8400_CODEC_SEQ_END_EINT_WIDTH              1  /* CODEC_SEQ_END_EINT */
#define WM8400_CDEL_TO_EINT                     0x0400  /* CDEL_TO_EINT */
#define WM8400_CDEL_TO_EINT_MASK                0x0400  /* CDEL_TO_EINT */
#define WM8400_CDEL_TO_EINT_SHIFT                   10  /* CDEL_TO_EINT */
#define WM8400_CDEL_TO_EINT_WIDTH                    1  /* CDEL_TO_EINT */
#define WM8400_CHIP_GT150_EINT                  0x0200  /* CHIP_GT150_EINT */
#define WM8400_CHIP_GT150_EINT_MASK             0x0200  /* CHIP_GT150_EINT */
#define WM8400_CHIP_GT150_EINT_SHIFT                 9  /* CHIP_GT150_EINT */
#define WM8400_CHIP_GT150_EINT_WIDTH                 1  /* CHIP_GT150_EINT */
#define WM8400_CHIP_GT115_EINT                  0x0100  /* CHIP_GT115_EINT */
#define WM8400_CHIP_GT115_EINT_MASK             0x0100  /* CHIP_GT115_EINT */
#define WM8400_CHIP_GT115_EINT_SHIFT                 8  /* CHIP_GT115_EINT */
#define WM8400_CHIP_GT115_EINT_WIDTH                 1  /* CHIP_GT115_EINT */
#define WM8400_LINE_CMP_EINT                    0x0080  /* LINE_CMP_EINT */
#define WM8400_LINE_CMP_EINT_MASK               0x0080  /* LINE_CMP_EINT */
#define WM8400_LINE_CMP_EINT_SHIFT                   7  /* LINE_CMP_EINT */
#define WM8400_LINE_CMP_EINT_WIDTH                   1  /* LINE_CMP_EINT */
#define WM8400_UVLO_EINT                        0x0040  /* UVLO_EINT */
#define WM8400_UVLO_EINT_MASK                   0x0040  /* UVLO_EINT */
#define WM8400_UVLO_EINT_SHIFT                       6  /* UVLO_EINT */
#define WM8400_UVLO_EINT_WIDTH                       1  /* UVLO_EINT */
#define WM8400_DC2_UV_EINT                      0x0020  /* DC2_UV_EINT */
#define WM8400_DC2_UV_EINT_MASK                 0x0020  /* DC2_UV_EINT */
#define WM8400_DC2_UV_EINT_SHIFT                     5  /* DC2_UV_EINT */
#define WM8400_DC2_UV_EINT_WIDTH                     1  /* DC2_UV_EINT */
#define WM8400_DC1_UV_EINT                      0x0010  /* DC1_UV_EINT */
#define WM8400_DC1_UV_EINT_MASK                 0x0010  /* DC1_UV_EINT */
#define WM8400_DC1_UV_EINT_SHIFT                     4  /* DC1_UV_EINT */
#define WM8400_DC1_UV_EINT_WIDTH                     1  /* DC1_UV_EINT */
#define WM8400_LDO4_UV_EINT                     0x0008  /* LDO4_UV_EINT */
#define WM8400_LDO4_UV_EINT_MASK                0x0008  /* LDO4_UV_EINT */
#define WM8400_LDO4_UV_EINT_SHIFT                    3  /* LDO4_UV_EINT */
#define WM8400_LDO4_UV_EINT_WIDTH                    1  /* LDO4_UV_EINT */
#define WM8400_LDO3_UV_EINT                     0x0004  /* LDO3_UV_EINT */
#define WM8400_LDO3_UV_EINT_MASK                0x0004  /* LDO3_UV_EINT */
#define WM8400_LDO3_UV_EINT_SHIFT                    2  /* LDO3_UV_EINT */
#define WM8400_LDO3_UV_EINT_WIDTH                    1  /* LDO3_UV_EINT */
#define WM8400_LDO2_UV_EINT                     0x0002  /* LDO2_UV_EINT */
#define WM8400_LDO2_UV_EINT_MASK                0x0002  /* LDO2_UV_EINT */
#define WM8400_LDO2_UV_EINT_SHIFT                    1  /* LDO2_UV_EINT */
#define WM8400_LDO2_UV_EINT_WIDTH                    1  /* LDO2_UV_EINT */
#define WM8400_LDO1_UV_EINT                     0x0001  /* LDO1_UV_EINT */
#define WM8400_LDO1_UV_EINT_MASK                0x0001  /* LDO1_UV_EINT */
#define WM8400_LDO1_UV_EINT_SHIFT                    0  /* LDO1_UV_EINT */
#define WM8400_LDO1_UV_EINT_WIDTH                    1  /* LDO1_UV_EINT */

/*
 * R80 (0x50) - Interrupt Status 1 Mask
 */
#define WM8400_IM_MICD_CINT                     0x8000  /* IM_MICD_CINT */
#define WM8400_IM_MICD_CINT_MASK                0x8000  /* IM_MICD_CINT */
#define WM8400_IM_MICD_CINT_SHIFT                   15  /* IM_MICD_CINT */
#define WM8400_IM_MICD_CINT_WIDTH                    1  /* IM_MICD_CINT */
#define WM8400_IM_MICSCD_CINT                   0x4000  /* IM_MICSCD_CINT */
#define WM8400_IM_MICSCD_CINT_MASK              0x4000  /* IM_MICSCD_CINT */
#define WM8400_IM_MICSCD_CINT_SHIFT                 14  /* IM_MICSCD_CINT */
#define WM8400_IM_MICSCD_CINT_WIDTH                  1  /* IM_MICSCD_CINT */
#define WM8400_IM_JDL_CINT                      0x2000  /* IM_JDL_CINT */
#define WM8400_IM_JDL_CINT_MASK                 0x2000  /* IM_JDL_CINT */
#define WM8400_IM_JDL_CINT_SHIFT                    13  /* IM_JDL_CINT */
#define WM8400_IM_JDL_CINT_WIDTH                     1  /* IM_JDL_CINT */
#define WM8400_IM_JDR_CINT                      0x1000  /* IM_JDR_CINT */
#define WM8400_IM_JDR_CINT_MASK                 0x1000  /* IM_JDR_CINT */
#define WM8400_IM_JDR_CINT_SHIFT                    12  /* IM_JDR_CINT */
#define WM8400_IM_JDR_CINT_WIDTH                     1  /* IM_JDR_CINT */
#define WM8400_IM_CODEC_SEQ_END_EINT            0x0800  /* IM_CODEC_SEQ_END_EINT */
#define WM8400_IM_CODEC_SEQ_END_EINT_MASK       0x0800  /* IM_CODEC_SEQ_END_EINT */
#define WM8400_IM_CODEC_SEQ_END_EINT_SHIFT          11  /* IM_CODEC_SEQ_END_EINT */
#define WM8400_IM_CODEC_SEQ_END_EINT_WIDTH           1  /* IM_CODEC_SEQ_END_EINT */
#define WM8400_IM_CDEL_TO_EINT                  0x0400  /* IM_CDEL_TO_EINT */
#define WM8400_IM_CDEL_TO_EINT_MASK             0x0400  /* IM_CDEL_TO_EINT */
#define WM8400_IM_CDEL_TO_EINT_SHIFT                10  /* IM_CDEL_TO_EINT */
#define WM8400_IM_CDEL_TO_EINT_WIDTH                 1  /* IM_CDEL_TO_EINT */
#define WM8400_IM_CHIP_GT150_EINT               0x0200  /* IM_CHIP_GT150_EINT */
#define WM8400_IM_CHIP_GT150_EINT_MASK          0x0200  /* IM_CHIP_GT150_EINT */
#define WM8400_IM_CHIP_GT150_EINT_SHIFT              9  /* IM_CHIP_GT150_EINT */
#define WM8400_IM_CHIP_GT150_EINT_WIDTH              1  /* IM_CHIP_GT150_EINT */
#define WM8400_IM_CHIP_GT115_EINT               0x0100  /* IM_CHIP_GT115_EINT */
#define WM8400_IM_CHIP_GT115_EINT_MASK          0x0100  /* IM_CHIP_GT115_EINT */
#define WM8400_IM_CHIP_GT115_EINT_SHIFT              8  /* IM_CHIP_GT115_EINT */
#define WM8400_IM_CHIP_GT115_EINT_WIDTH              1  /* IM_CHIP_GT115_EINT */
#define WM8400_IM_LINE_CMP_EINT                 0x0080  /* IM_LINE_CMP_EINT */
#define WM8400_IM_LINE_CMP_EINT_MASK            0x0080  /* IM_LINE_CMP_EINT */
#define WM8400_IM_LINE_CMP_EINT_SHIFT                7  /* IM_LINE_CMP_EINT */
#define WM8400_IM_LINE_CMP_EINT_WIDTH                1  /* IM_LINE_CMP_EINT */
#define WM8400_IM_UVLO_EINT                     0x0040  /* IM_UVLO_EINT */
#define WM8400_IM_UVLO_EINT_MASK                0x0040  /* IM_UVLO_EINT */
#define WM8400_IM_UVLO_EINT_SHIFT                    6  /* IM_UVLO_EINT */
#define WM8400_IM_UVLO_EINT_WIDTH                    1  /* IM_UVLO_EINT */
#define WM8400_IM_DC2_UV_EINT                   0x0020  /* IM_DC2_UV_EINT */
#define WM8400_IM_DC2_UV_EINT_MASK              0x0020  /* IM_DC2_UV_EINT */
#define WM8400_IM_DC2_UV_EINT_SHIFT                  5  /* IM_DC2_UV_EINT */
#define WM8400_IM_DC2_UV_EINT_WIDTH                  1  /* IM_DC2_UV_EINT */
#define WM8400_IM_DC1_UV_EINT                   0x0010  /* IM_DC1_UV_EINT */
#define WM8400_IM_DC1_UV_EINT_MASK              0x0010  /* IM_DC1_UV_EINT */
#define WM8400_IM_DC1_UV_EINT_SHIFT                  4  /* IM_DC1_UV_EINT */
#define WM8400_IM_DC1_UV_EINT_WIDTH                  1  /* IM_DC1_UV_EINT */
#define WM8400_IM_LDO4_UV_EINT                  0x0008  /* IM_LDO4_UV_EINT */
#define WM8400_IM_LDO4_UV_EINT_MASK             0x0008  /* IM_LDO4_UV_EINT */
#define WM8400_IM_LDO4_UV_EINT_SHIFT                 3  /* IM_LDO4_UV_EINT */
#define WM8400_IM_LDO4_UV_EINT_WIDTH                 1  /* IM_LDO4_UV_EINT */
#define WM8400_IM_LDO3_UV_EINT                  0x0004  /* IM_LDO3_UV_EINT */
#define WM8400_IM_LDO3_UV_EINT_MASK             0x0004  /* IM_LDO3_UV_EINT */
#define WM8400_IM_LDO3_UV_EINT_SHIFT                 2  /* IM_LDO3_UV_EINT */
#define WM8400_IM_LDO3_UV_EINT_WIDTH                 1  /* IM_LDO3_UV_EINT */
#define WM8400_IM_LDO2_UV_EINT                  0x0002  /* IM_LDO2_UV_EINT */
#define WM8400_IM_LDO2_UV_EINT_MASK             0x0002  /* IM_LDO2_UV_EINT */
#define WM8400_IM_LDO2_UV_EINT_SHIFT                 1  /* IM_LDO2_UV_EINT */
#define WM8400_IM_LDO2_UV_EINT_WIDTH                 1  /* IM_LDO2_UV_EINT */
#define WM8400_IM_LDO1_UV_EINT                  0x0001  /* IM_LDO1_UV_EINT */
#define WM8400_IM_LDO1_UV_EINT_MASK             0x0001  /* IM_LDO1_UV_EINT */
#define WM8400_IM_LDO1_UV_EINT_SHIFT                 0  /* IM_LDO1_UV_EINT */
#define WM8400_IM_LDO1_UV_EINT_WIDTH                 1  /* IM_LDO1_UV_EINT */

/*
 * R81 (0x51) - Interrupt Levels
 */
#define WM8400_MICD_LVL                         0x8000  /* MICD_LVL */
#define WM8400_MICD_LVL_MASK                    0x8000  /* MICD_LVL */
#define WM8400_MICD_LVL_SHIFT                       15  /* MICD_LVL */
#define WM8400_MICD_LVL_WIDTH                        1  /* MICD_LVL */
#define WM8400_MICSCD_LVL                       0x4000  /* MICSCD_LVL */
#define WM8400_MICSCD_LVL_MASK                  0x4000  /* MICSCD_LVL */
#define WM8400_MICSCD_LVL_SHIFT                     14  /* MICSCD_LVL */
#define WM8400_MICSCD_LVL_WIDTH                      1  /* MICSCD_LVL */
#define WM8400_JDL_LVL                          0x2000  /* JDL_LVL */
#define WM8400_JDL_LVL_MASK                     0x2000  /* JDL_LVL */
#define WM8400_JDL_LVL_SHIFT                        13  /* JDL_LVL */
#define WM8400_JDL_LVL_WIDTH                         1  /* JDL_LVL */
#define WM8400_JDR_LVL                          0x1000  /* JDR_LVL */
#define WM8400_JDR_LVL_MASK                     0x1000  /* JDR_LVL */
#define WM8400_JDR_LVL_SHIFT                        12  /* JDR_LVL */
#define WM8400_JDR_LVL_WIDTH                         1  /* JDR_LVL */
#define WM8400_CODEC_SEQ_END_LVL                0x0800  /* CODEC_SEQ_END_LVL */
#define WM8400_CODEC_SEQ_END_LVL_MASK           0x0800  /* CODEC_SEQ_END_LVL */
#define WM8400_CODEC_SEQ_END_LVL_SHIFT              11  /* CODEC_SEQ_END_LVL */
#define WM8400_CODEC_SEQ_END_LVL_WIDTH               1  /* CODEC_SEQ_END_LVL */
#define WM8400_CDEL_TO_LVL                      0x0400  /* CDEL_TO_LVL */
#define WM8400_CDEL_TO_LVL_MASK                 0x0400  /* CDEL_TO_LVL */
#define WM8400_CDEL_TO_LVL_SHIFT                    10  /* CDEL_TO_LVL */
#define WM8400_CDEL_TO_LVL_WIDTH                     1  /* CDEL_TO_LVL */
#define WM8400_CHIP_GT150_LVL                   0x0200  /* CHIP_GT150_LVL */
#define WM8400_CHIP_GT150_LVL_MASK              0x0200  /* CHIP_GT150_LVL */
#define WM8400_CHIP_GT150_LVL_SHIFT                  9  /* CHIP_GT150_LVL */
#define WM8400_CHIP_GT150_LVL_WIDTH                  1  /* CHIP_GT150_LVL */
#define WM8400_CHIP_GT115_LVL                   0x0100  /* CHIP_GT115_LVL */
#define WM8400_CHIP_GT115_LVL_MASK              0x0100  /* CHIP_GT115_LVL */
#define WM8400_CHIP_GT115_LVL_SHIFT                  8  /* CHIP_GT115_LVL */
#define WM8400_CHIP_GT115_LVL_WIDTH                  1  /* CHIP_GT115_LVL */
#define WM8400_LINE_CMP_LVL                     0x0080  /* LINE_CMP_LVL */
#define WM8400_LINE_CMP_LVL_MASK                0x0080  /* LINE_CMP_LVL */
#define WM8400_LINE_CMP_LVL_SHIFT                    7  /* LINE_CMP_LVL */
#define WM8400_LINE_CMP_LVL_WIDTH                    1  /* LINE_CMP_LVL */
#define WM8400_UVLO_LVL                         0x0040  /* UVLO_LVL */
#define WM8400_UVLO_LVL_MASK                    0x0040  /* UVLO_LVL */
#define WM8400_UVLO_LVL_SHIFT                        6  /* UVLO_LVL */
#define WM8400_UVLO_LVL_WIDTH                        1  /* UVLO_LVL */
#define WM8400_DC2_UV_LVL                       0x0020  /* DC2_UV_LVL */
#define WM8400_DC2_UV_LVL_MASK                  0x0020  /* DC2_UV_LVL */
#define WM8400_DC2_UV_LVL_SHIFT                      5  /* DC2_UV_LVL */
#define WM8400_DC2_UV_LVL_WIDTH                      1  /* DC2_UV_LVL */
#define WM8400_DC1_UV_LVL                       0x0010  /* DC1_UV_LVL */
#define WM8400_DC1_UV_LVL_MASK                  0x0010  /* DC1_UV_LVL */
#define WM8400_DC1_UV_LVL_SHIFT                      4  /* DC1_UV_LVL */
#define WM8400_DC1_UV_LVL_WIDTH                      1  /* DC1_UV_LVL */
#define WM8400_LDO4_UV_LVL                      0x0008  /* LDO4_UV_LVL */
#define WM8400_LDO4_UV_LVL_MASK                 0x0008  /* LDO4_UV_LVL */
#define WM8400_LDO4_UV_LVL_SHIFT                     3  /* LDO4_UV_LVL */
#define WM8400_LDO4_UV_LVL_WIDTH                     1  /* LDO4_UV_LVL */
#define WM8400_LDO3_UV_LVL                      0x0004  /* LDO3_UV_LVL */
#define WM8400_LDO3_UV_LVL_MASK                 0x0004  /* LDO3_UV_LVL */
#define WM8400_LDO3_UV_LVL_SHIFT                     2  /* LDO3_UV_LVL */
#define WM8400_LDO3_UV_LVL_WIDTH                     1  /* LDO3_UV_LVL */
#define WM8400_LDO2_UV_LVL                      0x0002  /* LDO2_UV_LVL */
#define WM8400_LDO2_UV_LVL_MASK                 0x0002  /* LDO2_UV_LVL */
#define WM8400_LDO2_UV_LVL_SHIFT                     1  /* LDO2_UV_LVL */
#define WM8400_LDO2_UV_LVL_WIDTH                     1  /* LDO2_UV_LVL */
#define WM8400_LDO1_UV_LVL                      0x0001  /* LDO1_UV_LVL */
#define WM8400_LDO1_UV_LVL_MASK                 0x0001  /* LDO1_UV_LVL */
#define WM8400_LDO1_UV_LVL_SHIFT                     0  /* LDO1_UV_LVL */
#define WM8400_LDO1_UV_LVL_WIDTH                     1  /* LDO1_UV_LVL */

/*
 * R82 (0x52) - Shutdown Reason
 */
#define WM8400_SDR_CHIP_SOFTSD                  0x2000  /* SDR_CHIP_SOFTSD */
#define WM8400_SDR_CHIP_SOFTSD_MASK             0x2000  /* SDR_CHIP_SOFTSD */
#define WM8400_SDR_CHIP_SOFTSD_SHIFT                13  /* SDR_CHIP_SOFTSD */
#define WM8400_SDR_CHIP_SOFTSD_WIDTH                 1  /* SDR_CHIP_SOFTSD */
#define WM8400_SDR_NPDN                         0x0800  /* SDR_NPDN */
#define WM8400_SDR_NPDN_MASK                    0x0800  /* SDR_NPDN */
#define WM8400_SDR_NPDN_SHIFT                       11  /* SDR_NPDN */
#define WM8400_SDR_NPDN_WIDTH                        1  /* SDR_NPDN */
#define WM8400_SDR_CHIP_GT150                   0x0200  /* SDR_CHIP_GT150 */
#define WM8400_SDR_CHIP_GT150_MASK              0x0200  /* SDR_CHIP_GT150 */
#define WM8400_SDR_CHIP_GT150_SHIFT                  9  /* SDR_CHIP_GT150 */
#define WM8400_SDR_CHIP_GT150_WIDTH                  1  /* SDR_CHIP_GT150 */
#define WM8400_SDR_CHIP_GT115                   0x0100  /* SDR_CHIP_GT115 */
#define WM8400_SDR_CHIP_GT115_MASK              0x0100  /* SDR_CHIP_GT115 */
#define WM8400_SDR_CHIP_GT115_SHIFT                  8  /* SDR_CHIP_GT115 */
#define WM8400_SDR_CHIP_GT115_WIDTH                  1  /* SDR_CHIP_GT115 */
#define WM8400_SDR_LINE_CMP                     0x0080  /* SDR_LINE_CMP */
#define WM8400_SDR_LINE_CMP_MASK                0x0080  /* SDR_LINE_CMP */
#define WM8400_SDR_LINE_CMP_SHIFT                    7  /* SDR_LINE_CMP */
#define WM8400_SDR_LINE_CMP_WIDTH                    1  /* SDR_LINE_CMP */
#define WM8400_SDR_UVLO                         0x0040  /* SDR_UVLO */
#define WM8400_SDR_UVLO_MASK                    0x0040  /* SDR_UVLO */
#define WM8400_SDR_UVLO_SHIFT                        6  /* SDR_UVLO */
#define WM8400_SDR_UVLO_WIDTH                        1  /* SDR_UVLO */
#define WM8400_SDR_DC2_UV                       0x0020  /* SDR_DC2_UV */
#define WM8400_SDR_DC2_UV_MASK                  0x0020  /* SDR_DC2_UV */
#define WM8400_SDR_DC2_UV_SHIFT                      5  /* SDR_DC2_UV */
#define WM8400_SDR_DC2_UV_WIDTH                      1  /* SDR_DC2_UV */
#define WM8400_SDR_DC1_UV                       0x0010  /* SDR_DC1_UV */
#define WM8400_SDR_DC1_UV_MASK                  0x0010  /* SDR_DC1_UV */
#define WM8400_SDR_DC1_UV_SHIFT                      4  /* SDR_DC1_UV */
#define WM8400_SDR_DC1_UV_WIDTH                      1  /* SDR_DC1_UV */
#define WM8400_SDR_LDO4_UV                      0x0008  /* SDR_LDO4_UV */
#define WM8400_SDR_LDO4_UV_MASK                 0x0008  /* SDR_LDO4_UV */
#define WM8400_SDR_LDO4_UV_SHIFT                     3  /* SDR_LDO4_UV */
#define WM8400_SDR_LDO4_UV_WIDTH                     1  /* SDR_LDO4_UV */
#define WM8400_SDR_LDO3_UV                      0x0004  /* SDR_LDO3_UV */
#define WM8400_SDR_LDO3_UV_MASK                 0x0004  /* SDR_LDO3_UV */
#define WM8400_SDR_LDO3_UV_SHIFT                     2  /* SDR_LDO3_UV */
#define WM8400_SDR_LDO3_UV_WIDTH                     1  /* SDR_LDO3_UV */
#define WM8400_SDR_LDO2_UV                      0x0002  /* SDR_LDO2_UV */
#define WM8400_SDR_LDO2_UV_MASK                 0x0002  /* SDR_LDO2_UV */
#define WM8400_SDR_LDO2_UV_SHIFT                     1  /* SDR_LDO2_UV */
#define WM8400_SDR_LDO2_UV_WIDTH                     1  /* SDR_LDO2_UV */
#define WM8400_SDR_LDO1_UV                      0x0001  /* SDR_LDO1_UV */
#define WM8400_SDR_LDO1_UV_MASK                 0x0001  /* SDR_LDO1_UV */
#define WM8400_SDR_LDO1_UV_SHIFT                     0  /* SDR_LDO1_UV */
#define WM8400_SDR_LDO1_UV_WIDTH                     1  /* SDR_LDO1_UV */

/*
 * R84 (0x54) - Line Circuits
 */
#define WM8400_BG_LINE_COMP                     0x8000  /* BG_LINE_COMP */
#define WM8400_BG_LINE_COMP_MASK                0x8000  /* BG_LINE_COMP */
#define WM8400_BG_LINE_COMP_SHIFT                   15  /* BG_LINE_COMP */
#define WM8400_BG_LINE_COMP_WIDTH                    1  /* BG_LINE_COMP */
#define WM8400_LINE_CMP_VTHI_MASK               0x00F0  /* LINE_CMP_VTHI - [7:4] */
#define WM8400_LINE_CMP_VTHI_SHIFT                   4  /* LINE_CMP_VTHI - [7:4] */
#define WM8400_LINE_CMP_VTHI_WIDTH                   4  /* LINE_CMP_VTHI - [7:4] */
#define WM8400_LINE_CMP_VTHD_MASK               0x000F  /* LINE_CMP_VTHD - [3:0] */
#define WM8400_LINE_CMP_VTHD_SHIFT                   0  /* LINE_CMP_VTHD - [3:0] */
#define WM8400_LINE_CMP_VTHD_WIDTH                   4  /* LINE_CMP_VTHD - [3:0] */

#endif
