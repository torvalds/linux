/*
 * GPIO configuration for Arizona devices
 *
 * Copyright 2013 Wolfson Microelectronics. PLC.
 *
 * Author: Charles Keepax <ckeepax@opensource.wolfsonmicro.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#ifndef _ARIZONA_GPIO_H
#define _ARIZONA_GPIO_H

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

#define ARIZONA_GPN_DIR                          0x8000  /* GPN_DIR */
#define ARIZONA_GPN_DIR_MASK                     0x8000  /* GPN_DIR */
#define ARIZONA_GPN_DIR_SHIFT                        15  /* GPN_DIR */
#define ARIZONA_GPN_DIR_WIDTH                         1  /* GPN_DIR */
#define ARIZONA_GPN_PU                           0x4000  /* GPN_PU */
#define ARIZONA_GPN_PU_MASK                      0x4000  /* GPN_PU */
#define ARIZONA_GPN_PU_SHIFT                         14  /* GPN_PU */
#define ARIZONA_GPN_PU_WIDTH                          1  /* GPN_PU */
#define ARIZONA_GPN_PD                           0x2000  /* GPN_PD */
#define ARIZONA_GPN_PD_MASK                      0x2000  /* GPN_PD */
#define ARIZONA_GPN_PD_SHIFT                         13  /* GPN_PD */
#define ARIZONA_GPN_PD_WIDTH                          1  /* GPN_PD */
#define ARIZONA_GPN_LVL                          0x0800  /* GPN_LVL */
#define ARIZONA_GPN_LVL_MASK                     0x0800  /* GPN_LVL */
#define ARIZONA_GPN_LVL_SHIFT                        11  /* GPN_LVL */
#define ARIZONA_GPN_LVL_WIDTH                         1  /* GPN_LVL */
#define ARIZONA_GPN_POL                          0x0400  /* GPN_POL */
#define ARIZONA_GPN_POL_MASK                     0x0400  /* GPN_POL */
#define ARIZONA_GPN_POL_SHIFT                        10  /* GPN_POL */
#define ARIZONA_GPN_POL_WIDTH                         1  /* GPN_POL */
#define ARIZONA_GPN_OP_CFG                       0x0200  /* GPN_OP_CFG */
#define ARIZONA_GPN_OP_CFG_MASK                  0x0200  /* GPN_OP_CFG */
#define ARIZONA_GPN_OP_CFG_SHIFT                      9  /* GPN_OP_CFG */
#define ARIZONA_GPN_OP_CFG_WIDTH                      1  /* GPN_OP_CFG */
#define ARIZONA_GPN_DB                           0x0100  /* GPN_DB */
#define ARIZONA_GPN_DB_MASK                      0x0100  /* GPN_DB */
#define ARIZONA_GPN_DB_SHIFT                          8  /* GPN_DB */
#define ARIZONA_GPN_DB_WIDTH                          1  /* GPN_DB */
#define ARIZONA_GPN_FN_MASK                      0x007F  /* GPN_DB */
#define ARIZONA_GPN_FN_SHIFT                          0  /* GPN_DB */
#define ARIZONA_GPN_FN_WIDTH                          7  /* GPN_DB */

#endif
