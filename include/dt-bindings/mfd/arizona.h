/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Device Tree defines for Arizona devices
 *
 * Copyright 2015 Cirrus Logic Inc.
 *
 * Author: Charles Keepax <ckeepax@opensource.wolfsonmicro.com>
 */

#ifndef _DT_BINDINGS_MFD_ARIZONA_H
#define _DT_BINDINGS_MFD_ARIZONA_H

/* GPIO Function Definitions */
#define ARIZONA_GP_FN_TXLRCLK                    0x00
#define ARIZONA_GP_FN_GPIO                       0x01
#define ARIZONA_GP_FN_IRQ1                       0x02
#define ARIZONA_GP_FN_IRQ2                       0x03
#define ARIZONA_GP_FN_OPCLK                      0x04
#define ARIZONA_GP_FN_FLL1_OUT                   0x05
#define ARIZONA_GP_FN_FLL2_OUT                   0x06
#define ARIZONA_GP_FN_PWM1                       0x08
#define ARIZONA_GP_FN_PWM2                       0x09
#define ARIZONA_GP_FN_SYSCLK_UNDERCLOCKED        0x0A
#define ARIZONA_GP_FN_ASYNCCLK_UNDERCLOCKED      0x0B
#define ARIZONA_GP_FN_FLL1_LOCK                  0x0C
#define ARIZONA_GP_FN_FLL2_LOCK                  0x0D
#define ARIZONA_GP_FN_FLL1_CLOCK_OK              0x0F
#define ARIZONA_GP_FN_FLL2_CLOCK_OK              0x10
#define ARIZONA_GP_FN_HEADPHONE_DET              0x12
#define ARIZONA_GP_FN_MIC_DET                    0x13
#define ARIZONA_GP_FN_WSEQ_STATUS                0x15
#define ARIZONA_GP_FN_CIF_ADDRESS_ERROR          0x16
#define ARIZONA_GP_FN_ASRC1_LOCK                 0x1A
#define ARIZONA_GP_FN_ASRC2_LOCK                 0x1B
#define ARIZONA_GP_FN_ASRC_CONFIG_ERROR          0x1C
#define ARIZONA_GP_FN_DRC1_SIGNAL_DETECT         0x1D
#define ARIZONA_GP_FN_DRC1_ANTICLIP              0x1E
#define ARIZONA_GP_FN_DRC1_DECAY                 0x1F
#define ARIZONA_GP_FN_DRC1_NOISE                 0x20
#define ARIZONA_GP_FN_DRC1_QUICK_RELEASE         0x21
#define ARIZONA_GP_FN_DRC2_SIGNAL_DETECT         0x22
#define ARIZONA_GP_FN_DRC2_ANTICLIP              0x23
#define ARIZONA_GP_FN_DRC2_DECAY                 0x24
#define ARIZONA_GP_FN_DRC2_NOISE                 0x25
#define ARIZONA_GP_FN_DRC2_QUICK_RELEASE         0x26
#define ARIZONA_GP_FN_MIXER_DROPPED_SAMPLE       0x27
#define ARIZONA_GP_FN_AIF1_CONFIG_ERROR          0x28
#define ARIZONA_GP_FN_AIF2_CONFIG_ERROR          0x29
#define ARIZONA_GP_FN_AIF3_CONFIG_ERROR          0x2A
#define ARIZONA_GP_FN_SPK_TEMP_SHUTDOWN          0x2B
#define ARIZONA_GP_FN_SPK_TEMP_WARNING           0x2C
#define ARIZONA_GP_FN_UNDERCLOCKED               0x2D
#define ARIZONA_GP_FN_OVERCLOCKED                0x2E
#define ARIZONA_GP_FN_DSP_IRQ1                   0x35
#define ARIZONA_GP_FN_DSP_IRQ2                   0x36
#define ARIZONA_GP_FN_ASYNC_OPCLK                0x3D
#define ARIZONA_GP_FN_BOOT_DONE                  0x44
#define ARIZONA_GP_FN_DSP1_RAM_READY             0x45
#define ARIZONA_GP_FN_SYSCLK_ENA_STATUS          0x4B
#define ARIZONA_GP_FN_ASYNCCLK_ENA_STATUS        0x4C

/* GPIO Configuration Bits */
#define ARIZONA_GPN_DIR                          0x8000
#define ARIZONA_GPN_PU                           0x4000
#define ARIZONA_GPN_PD                           0x2000
#define ARIZONA_GPN_LVL                          0x0800
#define ARIZONA_GPN_POL                          0x0400
#define ARIZONA_GPN_OP_CFG                       0x0200
#define ARIZONA_GPN_DB                           0x0100

/* Provide some defines for the most common configs */
#define ARIZONA_GP_DEFAULT             0xffffffff
#define ARIZONA_GP_OUTPUT              (ARIZONA_GP_FN_GPIO)
#define ARIZONA_GP_INPUT               (ARIZONA_GP_FN_GPIO | \
					ARIZONA_GPN_DIR)

#define ARIZONA_32KZ_MCLK1 1
#define ARIZONA_32KZ_MCLK2 2
#define ARIZONA_32KZ_NONE  3

#define ARIZONA_DMIC_MICVDD   0
#define ARIZONA_DMIC_MICBIAS1 1
#define ARIZONA_DMIC_MICBIAS2 2
#define ARIZONA_DMIC_MICBIAS3 3

#define ARIZONA_INMODE_DIFF 0
#define ARIZONA_INMODE_SE   1
#define ARIZONA_INMODE_DMIC 2

#define ARIZONA_MICD_TIME_CONTINUOUS                 0
#define ARIZONA_MICD_TIME_250US                      1
#define ARIZONA_MICD_TIME_500US                      2
#define ARIZONA_MICD_TIME_1MS                        3
#define ARIZONA_MICD_TIME_2MS                        4
#define ARIZONA_MICD_TIME_4MS                        5
#define ARIZONA_MICD_TIME_8MS                        6
#define ARIZONA_MICD_TIME_16MS                       7
#define ARIZONA_MICD_TIME_32MS                       8
#define ARIZONA_MICD_TIME_64MS                       9
#define ARIZONA_MICD_TIME_128MS                      10
#define ARIZONA_MICD_TIME_256MS                      11
#define ARIZONA_MICD_TIME_512MS                      12

#define ARIZONA_ACCDET_MODE_MIC 0
#define ARIZONA_ACCDET_MODE_HPL 1
#define ARIZONA_ACCDET_MODE_HPR 2
#define ARIZONA_ACCDET_MODE_HPM 4
#define ARIZONA_ACCDET_MODE_ADC 7

#define ARIZONA_GPSW_OPEN           0
#define ARIZONA_GPSW_CLOSED         1
#define ARIZONA_GPSW_CLAMP_ENABLED  2
#define ARIZONA_GPSW_CLAMP_DISABLED 3

#endif
