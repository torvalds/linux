/* SPDX-License-Identifier: GPL-2.0-or-later */
#ifndef __SOUND_CS46XX_H
#define __SOUND_CS46XX_H

/*
 *  Copyright (c) by Jaroslav Kysela <perex@perex.cz>,
 *		     Cirrus Logic, Inc.
 *  Definitions for Cirrus Logic CS46xx chips
 */

#include <sound/pcm.h>
#include <sound/pcm-indirect.h>
#include <sound/rawmidi.h>
#include <sound/ac97_codec.h>
#include "cs46xx_dsp_spos.h"

/*
 *  Direct registers
 */

/*
 *  The following define the offsets of the registers accessed via base address
 *  register zero on the CS46xx part.
 */
#define BA0_HISR				0x00000000
#define BA0_HSR0                                0x00000004
#define BA0_HICR                                0x00000008
#define BA0_DMSR                                0x00000100
#define BA0_HSAR                                0x00000110
#define BA0_HDAR                                0x00000114
#define BA0_HDMR                                0x00000118
#define BA0_HDCR                                0x0000011C
#define BA0_PFMC                                0x00000200
#define BA0_PFCV1                               0x00000204
#define BA0_PFCV2                               0x00000208
#define BA0_PCICFG00                            0x00000300
#define BA0_PCICFG04                            0x00000304
#define BA0_PCICFG08                            0x00000308
#define BA0_PCICFG0C                            0x0000030C
#define BA0_PCICFG10                            0x00000310
#define BA0_PCICFG14                            0x00000314
#define BA0_PCICFG18                            0x00000318
#define BA0_PCICFG1C                            0x0000031C
#define BA0_PCICFG20                            0x00000320
#define BA0_PCICFG24                            0x00000324
#define BA0_PCICFG28                            0x00000328
#define BA0_PCICFG2C                            0x0000032C
#define BA0_PCICFG30                            0x00000330
#define BA0_PCICFG34                            0x00000334
#define BA0_PCICFG38                            0x00000338
#define BA0_PCICFG3C                            0x0000033C
#define BA0_CLKCR1                              0x00000400
#define BA0_CLKCR2                              0x00000404
#define BA0_PLLM                                0x00000408
#define BA0_PLLCC                               0x0000040C
#define BA0_FRR                                 0x00000410 
#define BA0_CFL1                                0x00000414
#define BA0_CFL2                                0x00000418
#define BA0_SERMC1                              0x00000420
#define BA0_SERMC2                              0x00000424
#define BA0_SERC1                               0x00000428
#define BA0_SERC2                               0x0000042C
#define BA0_SERC3                               0x00000430
#define BA0_SERC4                               0x00000434
#define BA0_SERC5                               0x00000438
#define BA0_SERBSP                              0x0000043C
#define BA0_SERBST                              0x00000440
#define BA0_SERBCM                              0x00000444
#define BA0_SERBAD                              0x00000448
#define BA0_SERBCF                              0x0000044C
#define BA0_SERBWP                              0x00000450
#define BA0_SERBRP                              0x00000454
#ifndef NO_CS4612
#define BA0_ASER_FADDR                          0x00000458
#endif
#define BA0_ACCTL                               0x00000460
#define BA0_ACSTS                               0x00000464
#define BA0_ACOSV                               0x00000468
#define BA0_ACCAD                               0x0000046C
#define BA0_ACCDA                               0x00000470
#define BA0_ACISV                               0x00000474
#define BA0_ACSAD                               0x00000478
#define BA0_ACSDA                               0x0000047C
#define BA0_JSPT                                0x00000480
#define BA0_JSCTL                               0x00000484
#define BA0_JSC1                                0x00000488
#define BA0_JSC2                                0x0000048C
#define BA0_MIDCR                               0x00000490
#define BA0_MIDSR                               0x00000494
#define BA0_MIDWP                               0x00000498
#define BA0_MIDRP                               0x0000049C
#define BA0_JSIO                                0x000004A0
#ifndef NO_CS4612
#define BA0_ASER_MASTER                         0x000004A4
#endif
#define BA0_CFGI                                0x000004B0
#define BA0_SSVID                               0x000004B4
#define BA0_GPIOR                               0x000004B8
#ifndef NO_CS4612
#define BA0_EGPIODR                             0x000004BC
#define BA0_EGPIOPTR                            0x000004C0
#define BA0_EGPIOTR                             0x000004C4
#define BA0_EGPIOWR                             0x000004C8
#define BA0_EGPIOSR                             0x000004CC
#define BA0_SERC6                               0x000004D0
#define BA0_SERC7                               0x000004D4
#define BA0_SERACC                              0x000004D8
#define BA0_ACCTL2                              0x000004E0
#define BA0_ACSTS2                              0x000004E4
#define BA0_ACOSV2                              0x000004E8
#define BA0_ACCAD2                              0x000004EC
#define BA0_ACCDA2                              0x000004F0
#define BA0_ACISV2                              0x000004F4
#define BA0_ACSAD2                              0x000004F8
#define BA0_ACSDA2                              0x000004FC
#define BA0_IOTAC0                              0x00000500
#define BA0_IOTAC1                              0x00000504
#define BA0_IOTAC2                              0x00000508
#define BA0_IOTAC3                              0x0000050C
#define BA0_IOTAC4                              0x00000510
#define BA0_IOTAC5                              0x00000514
#define BA0_IOTAC6                              0x00000518
#define BA0_IOTAC7                              0x0000051C
#define BA0_IOTAC8                              0x00000520
#define BA0_IOTAC9                              0x00000524
#define BA0_IOTAC10                             0x00000528
#define BA0_IOTAC11                             0x0000052C
#define BA0_IOTFR0                              0x00000540
#define BA0_IOTFR1                              0x00000544
#define BA0_IOTFR2                              0x00000548
#define BA0_IOTFR3                              0x0000054C
#define BA0_IOTFR4                              0x00000550
#define BA0_IOTFR5                              0x00000554
#define BA0_IOTFR6                              0x00000558
#define BA0_IOTFR7                              0x0000055C
#define BA0_IOTFIFO                             0x00000580
#define BA0_IOTRRD                              0x00000584
#define BA0_IOTFP                               0x00000588
#define BA0_IOTCR                               0x0000058C
#define BA0_DPCID                               0x00000590
#define BA0_DPCIA                               0x00000594
#define BA0_DPCIC                               0x00000598
#define BA0_PCPCIR                              0x00000600
#define BA0_PCPCIG                              0x00000604
#define BA0_PCPCIEN                             0x00000608
#define BA0_EPCIPMC                             0x00000610
#endif

/*
 *  The following define the offsets of the registers and memories accessed via
 *  base address register one on the CS46xx part.
 */
#define BA1_SP_DMEM0                            0x00000000
#define BA1_SP_DMEM1                            0x00010000
#define BA1_SP_PMEM                             0x00020000
#define BA1_SP_REG				0x00030000
#define BA1_SPCR                                0x00030000
#define BA1_DREG                                0x00030004
#define BA1_DSRWP                               0x00030008
#define BA1_TWPR                                0x0003000C
#define BA1_SPWR                                0x00030010
#define BA1_SPIR                                0x00030014
#define BA1_FGR1                                0x00030020
#define BA1_SPCS                                0x00030028
#define BA1_SDSR                                0x0003002C
#define BA1_FRMT                                0x00030030
#define BA1_FRCC                                0x00030034
#define BA1_FRSC                                0x00030038
#define BA1_OMNI_MEM                            0x000E0000


/*
 *  The following defines are for the flags in the host interrupt status
 *  register.
 */
#define HISR_VC_MASK                            0x0000FFFF
#define HISR_VC0                                0x00000001
#define HISR_VC1                                0x00000002
#define HISR_VC2                                0x00000004
#define HISR_VC3                                0x00000008
#define HISR_VC4                                0x00000010
#define HISR_VC5                                0x00000020
#define HISR_VC6                                0x00000040
#define HISR_VC7                                0x00000080
#define HISR_VC8                                0x00000100
#define HISR_VC9                                0x00000200
#define HISR_VC10                               0x00000400
#define HISR_VC11                               0x00000800
#define HISR_VC12                               0x00001000
#define HISR_VC13                               0x00002000
#define HISR_VC14                               0x00004000
#define HISR_VC15                               0x00008000
#define HISR_INT0                               0x00010000
#define HISR_INT1                               0x00020000
#define HISR_DMAI                               0x00040000
#define HISR_FROVR                              0x00080000
#define HISR_MIDI                               0x00100000
#ifdef NO_CS4612
#define HISR_RESERVED                           0x0FE00000
#else
#define HISR_SBINT                              0x00200000
#define HISR_RESERVED                           0x0FC00000
#endif
#define HISR_H0P                                0x40000000
#define HISR_INTENA                             0x80000000

/*
 *  The following defines are for the flags in the host signal register 0.
 */
#define HSR0_VC_MASK                            0xFFFFFFFF
#define HSR0_VC16                               0x00000001
#define HSR0_VC17                               0x00000002
#define HSR0_VC18                               0x00000004
#define HSR0_VC19                               0x00000008
#define HSR0_VC20                               0x00000010
#define HSR0_VC21                               0x00000020
#define HSR0_VC22                               0x00000040
#define HSR0_VC23                               0x00000080
#define HSR0_VC24                               0x00000100
#define HSR0_VC25                               0x00000200
#define HSR0_VC26                               0x00000400
#define HSR0_VC27                               0x00000800
#define HSR0_VC28                               0x00001000
#define HSR0_VC29                               0x00002000
#define HSR0_VC30                               0x00004000
#define HSR0_VC31                               0x00008000
#define HSR0_VC32                               0x00010000
#define HSR0_VC33                               0x00020000
#define HSR0_VC34                               0x00040000
#define HSR0_VC35                               0x00080000
#define HSR0_VC36                               0x00100000
#define HSR0_VC37                               0x00200000
#define HSR0_VC38                               0x00400000
#define HSR0_VC39                               0x00800000
#define HSR0_VC40                               0x01000000
#define HSR0_VC41                               0x02000000
#define HSR0_VC42                               0x04000000
#define HSR0_VC43                               0x08000000
#define HSR0_VC44                               0x10000000
#define HSR0_VC45                               0x20000000
#define HSR0_VC46                               0x40000000
#define HSR0_VC47                               0x80000000

/*
 *  The following defines are for the flags in the host interrupt control
 *  register.
 */
#define HICR_IEV                                0x00000001
#define HICR_CHGM                               0x00000002

/*
 *  The following defines are for the flags in the DMA status register.
 */
#define DMSR_HP                                 0x00000001
#define DMSR_HR                                 0x00000002
#define DMSR_SP                                 0x00000004
#define DMSR_SR                                 0x00000008

/*
 *  The following defines are for the flags in the host DMA source address
 *  register.
 */
#define HSAR_HOST_ADDR_MASK                     0xFFFFFFFF
#define HSAR_DSP_ADDR_MASK                      0x0000FFFF
#define HSAR_MEMID_MASK                         0x000F0000
#define HSAR_MEMID_SP_DMEM0                     0x00000000
#define HSAR_MEMID_SP_DMEM1                     0x00010000
#define HSAR_MEMID_SP_PMEM                      0x00020000
#define HSAR_MEMID_SP_DEBUG                     0x00030000
#define HSAR_MEMID_OMNI_MEM                     0x000E0000
#define HSAR_END                                0x40000000
#define HSAR_ERR                                0x80000000

/*
 *  The following defines are for the flags in the host DMA destination address
 *  register.
 */
#define HDAR_HOST_ADDR_MASK                     0xFFFFFFFF
#define HDAR_DSP_ADDR_MASK                      0x0000FFFF
#define HDAR_MEMID_MASK                         0x000F0000
#define HDAR_MEMID_SP_DMEM0                     0x00000000
#define HDAR_MEMID_SP_DMEM1                     0x00010000
#define HDAR_MEMID_SP_PMEM                      0x00020000
#define HDAR_MEMID_SP_DEBUG                     0x00030000
#define HDAR_MEMID_OMNI_MEM                     0x000E0000
#define HDAR_END                                0x40000000
#define HDAR_ERR                                0x80000000

/*
 *  The following defines are for the flags in the host DMA control register.
 */
#define HDMR_AC_MASK                            0x0000F000
#define HDMR_AC_8_16                            0x00001000
#define HDMR_AC_M_S                             0x00002000
#define HDMR_AC_B_L                             0x00004000
#define HDMR_AC_S_U                             0x00008000

/*
 *  The following defines are for the flags in the host DMA control register.
 */
#define HDCR_COUNT_MASK                         0x000003FF
#define HDCR_DONE                               0x00004000
#define HDCR_OPT                                0x00008000
#define HDCR_WBD                                0x00400000
#define HDCR_WBS                                0x00800000
#define HDCR_DMS_MASK                           0x07000000
#define HDCR_DMS_LINEAR                         0x00000000
#define HDCR_DMS_16_DWORDS                      0x01000000
#define HDCR_DMS_32_DWORDS                      0x02000000
#define HDCR_DMS_64_DWORDS                      0x03000000
#define HDCR_DMS_128_DWORDS                     0x04000000
#define HDCR_DMS_256_DWORDS                     0x05000000
#define HDCR_DMS_512_DWORDS                     0x06000000
#define HDCR_DMS_1024_DWORDS                    0x07000000
#define HDCR_DH                                 0x08000000
#define HDCR_SMS_MASK                           0x70000000
#define HDCR_SMS_LINEAR                         0x00000000
#define HDCR_SMS_16_DWORDS                      0x10000000
#define HDCR_SMS_32_DWORDS                      0x20000000
#define HDCR_SMS_64_DWORDS                      0x30000000
#define HDCR_SMS_128_DWORDS                     0x40000000
#define HDCR_SMS_256_DWORDS                     0x50000000
#define HDCR_SMS_512_DWORDS                     0x60000000
#define HDCR_SMS_1024_DWORDS                    0x70000000
#define HDCR_SH                                 0x80000000
#define HDCR_COUNT_SHIFT                        0

/*
 *  The following defines are for the flags in the performance monitor control
 *  register.
 */
#define PFMC_C1SS_MASK                          0x0000001F
#define PFMC_C1EV                               0x00000020
#define PFMC_C1RS                               0x00008000
#define PFMC_C2SS_MASK                          0x001F0000
#define PFMC_C2EV                               0x00200000
#define PFMC_C2RS                               0x80000000
#define PFMC_C1SS_SHIFT                         0
#define PFMC_C2SS_SHIFT                         16
#define PFMC_BUS_GRANT                          0
#define PFMC_GRANT_AFTER_REQ                    1
#define PFMC_TRANSACTION                        2
#define PFMC_DWORD_TRANSFER                     3
#define PFMC_SLAVE_READ                         4
#define PFMC_SLAVE_WRITE                        5
#define PFMC_PREEMPTION                         6
#define PFMC_DISCONNECT_RETRY                   7
#define PFMC_INTERRUPT                          8
#define PFMC_BUS_OWNERSHIP                      9
#define PFMC_TRANSACTION_LAG                    10
#define PFMC_PCI_CLOCK                          11
#define PFMC_SERIAL_CLOCK                       12
#define PFMC_SP_CLOCK                           13

/*
 *  The following defines are for the flags in the performance counter value 1
 *  register.
 */
#define PFCV1_PC1V_MASK                         0xFFFFFFFF
#define PFCV1_PC1V_SHIFT                        0

/*
 *  The following defines are for the flags in the performance counter value 2
 *  register.
 */
#define PFCV2_PC2V_MASK                         0xFFFFFFFF
#define PFCV2_PC2V_SHIFT                        0

/*
 *  The following defines are for the flags in the clock control register 1.
 */
#define CLKCR1_OSCS                             0x00000001
#define CLKCR1_OSCP                             0x00000002
#define CLKCR1_PLLSS_MASK                       0x0000000C
#define CLKCR1_PLLSS_SERIAL                     0x00000000
#define CLKCR1_PLLSS_CRYSTAL                    0x00000004
#define CLKCR1_PLLSS_PCI                        0x00000008
#define CLKCR1_PLLSS_RESERVED                   0x0000000C
#define CLKCR1_PLLP                             0x00000010
#define CLKCR1_SWCE                             0x00000020
#define CLKCR1_PLLOS                            0x00000040

/*
 *  The following defines are for the flags in the clock control register 2.
 */
#define CLKCR2_PDIVS_MASK                       0x0000000F
#define CLKCR2_PDIVS_1                          0x00000001
#define CLKCR2_PDIVS_2                          0x00000002
#define CLKCR2_PDIVS_4                          0x00000004
#define CLKCR2_PDIVS_7                          0x00000007
#define CLKCR2_PDIVS_8                          0x00000008
#define CLKCR2_PDIVS_16                         0x00000000

/*
 *  The following defines are for the flags in the PLL multiplier register.
 */
#define PLLM_MASK                               0x000000FF
#define PLLM_SHIFT                              0

/*
 *  The following defines are for the flags in the PLL capacitor coefficient
 *  register.
 */
#define PLLCC_CDR_MASK                          0x00000007
#ifndef NO_CS4610
#define PLLCC_CDR_240_350_MHZ                   0x00000000
#define PLLCC_CDR_184_265_MHZ                   0x00000001
#define PLLCC_CDR_144_205_MHZ                   0x00000002
#define PLLCC_CDR_111_160_MHZ                   0x00000003
#define PLLCC_CDR_87_123_MHZ                    0x00000004
#define PLLCC_CDR_67_96_MHZ                     0x00000005
#define PLLCC_CDR_52_74_MHZ                     0x00000006
#define PLLCC_CDR_45_58_MHZ                     0x00000007
#endif
#ifndef NO_CS4612
#define PLLCC_CDR_271_398_MHZ                   0x00000000
#define PLLCC_CDR_227_330_MHZ                   0x00000001
#define PLLCC_CDR_167_239_MHZ                   0x00000002
#define PLLCC_CDR_150_215_MHZ                   0x00000003
#define PLLCC_CDR_107_154_MHZ                   0x00000004
#define PLLCC_CDR_98_140_MHZ                    0x00000005
#define PLLCC_CDR_73_104_MHZ                    0x00000006
#define PLLCC_CDR_63_90_MHZ                     0x00000007
#endif
#define PLLCC_LPF_MASK                          0x000000F8
#ifndef NO_CS4610
#define PLLCC_LPF_23850_60000_KHZ               0x00000000
#define PLLCC_LPF_7960_26290_KHZ                0x00000008
#define PLLCC_LPF_4160_10980_KHZ                0x00000018
#define PLLCC_LPF_1740_4580_KHZ                 0x00000038
#define PLLCC_LPF_724_1910_KHZ                  0x00000078
#define PLLCC_LPF_317_798_KHZ                   0x000000F8
#endif
#ifndef NO_CS4612
#define PLLCC_LPF_25580_64530_KHZ               0x00000000
#define PLLCC_LPF_14360_37270_KHZ               0x00000008
#define PLLCC_LPF_6100_16020_KHZ                0x00000018
#define PLLCC_LPF_2540_6690_KHZ                 0x00000038
#define PLLCC_LPF_1050_2780_KHZ                 0x00000078
#define PLLCC_LPF_450_1160_KHZ                  0x000000F8
#endif

/*
 *  The following defines are for the flags in the feature reporting register.
 */
#define FRR_FAB_MASK                            0x00000003
#define FRR_MASK_MASK                           0x0000001C
#ifdef NO_CS4612
#define FRR_CFOP_MASK                           0x000000E0
#else
#define FRR_CFOP_MASK                           0x00000FE0
#endif
#define FRR_CFOP_NOT_DVD                        0x00000020
#define FRR_CFOP_A3D                            0x00000040
#define FRR_CFOP_128_PIN                        0x00000080
#ifndef NO_CS4612
#define FRR_CFOP_CS4280                         0x00000800
#endif
#define FRR_FAB_SHIFT                           0
#define FRR_MASK_SHIFT                          2
#define FRR_CFOP_SHIFT                          5

/*
 *  The following defines are for the flags in the configuration load 1
 *  register.
 */
#define CFL1_CLOCK_SOURCE_MASK                  0x00000003
#define CFL1_CLOCK_SOURCE_CS423X                0x00000000
#define CFL1_CLOCK_SOURCE_AC97                  0x00000001
#define CFL1_CLOCK_SOURCE_CRYSTAL               0x00000002
#define CFL1_CLOCK_SOURCE_DUAL_AC97             0x00000003
#define CFL1_VALID_DATA_MASK                    0x000000FF

/*
 *  The following defines are for the flags in the configuration load 2
 *  register.
 */
#define CFL2_VALID_DATA_MASK                    0x000000FF

/*
 *  The following defines are for the flags in the serial port master control
 *  register 1.
 */
#define SERMC1_MSPE                             0x00000001
#define SERMC1_PTC_MASK                         0x0000000E
#define SERMC1_PTC_CS423X                       0x00000000
#define SERMC1_PTC_AC97                         0x00000002
#define SERMC1_PTC_DAC                          0x00000004
#define SERMC1_PLB                              0x00000010
#define SERMC1_XLB                              0x00000020

/*
 *  The following defines are for the flags in the serial port master control
 *  register 2.
 */
#define SERMC2_LROE                             0x00000001
#define SERMC2_MCOE                             0x00000002
#define SERMC2_MCDIV                            0x00000004

/*
 *  The following defines are for the flags in the serial port 1 configuration
 *  register.
 */
#define SERC1_SO1EN                             0x00000001
#define SERC1_SO1F_MASK                         0x0000000E
#define SERC1_SO1F_CS423X                       0x00000000
#define SERC1_SO1F_AC97                         0x00000002
#define SERC1_SO1F_DAC                          0x00000004
#define SERC1_SO1F_SPDIF                        0x00000006

/*
 *  The following defines are for the flags in the serial port 2 configuration
 *  register.
 */
#define SERC2_SI1EN                             0x00000001
#define SERC2_SI1F_MASK                         0x0000000E
#define SERC2_SI1F_CS423X                       0x00000000
#define SERC2_SI1F_AC97                         0x00000002
#define SERC2_SI1F_ADC                          0x00000004
#define SERC2_SI1F_SPDIF                        0x00000006

/*
 *  The following defines are for the flags in the serial port 3 configuration
 *  register.
 */
#define SERC3_SO2EN                             0x00000001
#define SERC3_SO2F_MASK                         0x00000006
#define SERC3_SO2F_DAC                          0x00000000
#define SERC3_SO2F_SPDIF                        0x00000002

/*
 *  The following defines are for the flags in the serial port 4 configuration
 *  register.
 */
#define SERC4_SO3EN                             0x00000001
#define SERC4_SO3F_MASK                         0x00000006
#define SERC4_SO3F_DAC                          0x00000000
#define SERC4_SO3F_SPDIF                        0x00000002

/*
 *  The following defines are for the flags in the serial port 5 configuration
 *  register.
 */
#define SERC5_SI2EN                             0x00000001
#define SERC5_SI2F_MASK                         0x00000006
#define SERC5_SI2F_ADC                          0x00000000
#define SERC5_SI2F_SPDIF                        0x00000002

/*
 *  The following defines are for the flags in the serial port backdoor sample
 *  pointer register.
 */
#define SERBSP_FSP_MASK                         0x0000000F
#define SERBSP_FSP_SHIFT                        0

/*
 *  The following defines are for the flags in the serial port backdoor status
 *  register.
 */
#define SERBST_RRDY                             0x00000001
#define SERBST_WBSY                             0x00000002

/*
 *  The following defines are for the flags in the serial port backdoor command
 *  register.
 */
#define SERBCM_RDC                              0x00000001
#define SERBCM_WRC                              0x00000002

/*
 *  The following defines are for the flags in the serial port backdoor address
 *  register.
 */
#ifdef NO_CS4612
#define SERBAD_FAD_MASK                         0x000000FF
#else
#define SERBAD_FAD_MASK                         0x000001FF
#endif
#define SERBAD_FAD_SHIFT                        0

/*
 *  The following defines are for the flags in the serial port backdoor
 *  configuration register.
 */
#define SERBCF_HBP                              0x00000001

/*
 *  The following defines are for the flags in the serial port backdoor write
 *  port register.
 */
#define SERBWP_FWD_MASK                         0x000FFFFF
#define SERBWP_FWD_SHIFT                        0

/*
 *  The following defines are for the flags in the serial port backdoor read
 *  port register.
 */
#define SERBRP_FRD_MASK                         0x000FFFFF
#define SERBRP_FRD_SHIFT                        0

/*
 *  The following defines are for the flags in the async FIFO address register.
 */
#ifndef NO_CS4612
#define ASER_FADDR_A1_MASK                      0x000001FF
#define ASER_FADDR_EN1                          0x00008000
#define ASER_FADDR_A2_MASK                      0x01FF0000
#define ASER_FADDR_EN2                          0x80000000
#define ASER_FADDR_A1_SHIFT                     0
#define ASER_FADDR_A2_SHIFT                     16
#endif

/*
 *  The following defines are for the flags in the AC97 control register.
 */
#define ACCTL_RSTN                              0x00000001
#define ACCTL_ESYN                              0x00000002
#define ACCTL_VFRM                              0x00000004
#define ACCTL_DCV                               0x00000008
#define ACCTL_CRW                               0x00000010
#define ACCTL_ASYN                              0x00000020
#ifndef NO_CS4612
#define ACCTL_TC                                0x00000040
#endif

/*
 *  The following defines are for the flags in the AC97 status register.
 */
#define ACSTS_CRDY                              0x00000001
#define ACSTS_VSTS                              0x00000002
#ifndef NO_CS4612
#define ACSTS_WKUP                              0x00000004
#endif

/*
 *  The following defines are for the flags in the AC97 output slot valid
 *  register.
 */
#define ACOSV_SLV3                              0x00000001
#define ACOSV_SLV4                              0x00000002
#define ACOSV_SLV5                              0x00000004
#define ACOSV_SLV6                              0x00000008
#define ACOSV_SLV7                              0x00000010
#define ACOSV_SLV8                              0x00000020
#define ACOSV_SLV9                              0x00000040
#define ACOSV_SLV10                             0x00000080
#define ACOSV_SLV11                             0x00000100
#define ACOSV_SLV12                             0x00000200

/*
 *  The following defines are for the flags in the AC97 command address
 *  register.
 */
#define ACCAD_CI_MASK                           0x0000007F
#define ACCAD_CI_SHIFT                          0

/*
 *  The following defines are for the flags in the AC97 command data register.
 */
#define ACCDA_CD_MASK                           0x0000FFFF
#define ACCDA_CD_SHIFT                          0

/*
 *  The following defines are for the flags in the AC97 input slot valid
 *  register.
 */
#define ACISV_ISV3                              0x00000001
#define ACISV_ISV4                              0x00000002
#define ACISV_ISV5                              0x00000004
#define ACISV_ISV6                              0x00000008
#define ACISV_ISV7                              0x00000010
#define ACISV_ISV8                              0x00000020
#define ACISV_ISV9                              0x00000040
#define ACISV_ISV10                             0x00000080
#define ACISV_ISV11                             0x00000100
#define ACISV_ISV12                             0x00000200

/*
 *  The following defines are for the flags in the AC97 status address
 *  register.
 */
#define ACSAD_SI_MASK                           0x0000007F
#define ACSAD_SI_SHIFT                          0

/*
 *  The following defines are for the flags in the AC97 status data register.
 */
#define ACSDA_SD_MASK                           0x0000FFFF
#define ACSDA_SD_SHIFT                          0

/*
 *  The following defines are for the flags in the joystick poll/trigger
 *  register.
 */
#define JSPT_CAX                                0x00000001
#define JSPT_CAY                                0x00000002
#define JSPT_CBX                                0x00000004
#define JSPT_CBY                                0x00000008
#define JSPT_BA1                                0x00000010
#define JSPT_BA2                                0x00000020
#define JSPT_BB1                                0x00000040
#define JSPT_BB2                                0x00000080

/*
 *  The following defines are for the flags in the joystick control register.
 */
#define JSCTL_SP_MASK                           0x00000003
#define JSCTL_SP_SLOW                           0x00000000
#define JSCTL_SP_MEDIUM_SLOW                    0x00000001
#define JSCTL_SP_MEDIUM_FAST                    0x00000002
#define JSCTL_SP_FAST                           0x00000003
#define JSCTL_ARE                               0x00000004

/*
 *  The following defines are for the flags in the joystick coordinate pair 1
 *  readback register.
 */
#define JSC1_Y1V_MASK                           0x0000FFFF
#define JSC1_X1V_MASK                           0xFFFF0000
#define JSC1_Y1V_SHIFT                          0
#define JSC1_X1V_SHIFT                          16

/*
 *  The following defines are for the flags in the joystick coordinate pair 2
 *  readback register.
 */
#define JSC2_Y2V_MASK                           0x0000FFFF
#define JSC2_X2V_MASK                           0xFFFF0000
#define JSC2_Y2V_SHIFT                          0
#define JSC2_X2V_SHIFT                          16

/*
 *  The following defines are for the flags in the MIDI control register.
 */
#define MIDCR_TXE                               0x00000001	/* Enable transmitting. */
#define MIDCR_RXE                               0x00000002	/* Enable receiving. */
#define MIDCR_RIE                               0x00000004	/* Interrupt upon tx ready. */
#define MIDCR_TIE                               0x00000008	/* Interrupt upon rx ready. */
#define MIDCR_MLB                               0x00000010	/* Enable midi loopback. */
#define MIDCR_MRST                              0x00000020	/* Reset interface. */

/*
 *  The following defines are for the flags in the MIDI status register.
 */
#define MIDSR_TBF                               0x00000001	/* Tx FIFO is full. */
#define MIDSR_RBE                               0x00000002	/* Rx FIFO is empty. */

/*
 *  The following defines are for the flags in the MIDI write port register.
 */
#define MIDWP_MWD_MASK                          0x000000FF
#define MIDWP_MWD_SHIFT                         0

/*
 *  The following defines are for the flags in the MIDI read port register.
 */
#define MIDRP_MRD_MASK                          0x000000FF
#define MIDRP_MRD_SHIFT                         0

/*
 *  The following defines are for the flags in the joystick GPIO register.
 */
#define JSIO_DAX                                0x00000001
#define JSIO_DAY                                0x00000002
#define JSIO_DBX                                0x00000004
#define JSIO_DBY                                0x00000008
#define JSIO_AXOE                               0x00000010
#define JSIO_AYOE                               0x00000020
#define JSIO_BXOE                               0x00000040
#define JSIO_BYOE                               0x00000080

/*
 *  The following defines are for the flags in the master async/sync serial
 *  port enable register.
 */
#ifndef NO_CS4612
#define ASER_MASTER_ME                          0x00000001
#endif

/*
 *  The following defines are for the flags in the configuration interface
 *  register.
 */
#define CFGI_CLK                                0x00000001
#define CFGI_DOUT                               0x00000002
#define CFGI_DIN_EEN                            0x00000004
#define CFGI_EELD                               0x00000008

/*
 *  The following defines are for the flags in the subsystem ID and vendor ID
 *  register.
 */
#define SSVID_VID_MASK                          0x0000FFFF
#define SSVID_SID_MASK                          0xFFFF0000
#define SSVID_VID_SHIFT                         0
#define SSVID_SID_SHIFT                         16

/*
 *  The following defines are for the flags in the GPIO pin interface register.
 */
#define GPIOR_VOLDN                             0x00000001
#define GPIOR_VOLUP                             0x00000002
#define GPIOR_SI2D                              0x00000004
#define GPIOR_SI2OE                             0x00000008

/*
 *  The following defines are for the flags in the extended GPIO pin direction
 *  register.
 */
#ifndef NO_CS4612
#define EGPIODR_GPOE0                           0x00000001
#define EGPIODR_GPOE1                           0x00000002
#define EGPIODR_GPOE2                           0x00000004
#define EGPIODR_GPOE3                           0x00000008
#define EGPIODR_GPOE4                           0x00000010
#define EGPIODR_GPOE5                           0x00000020
#define EGPIODR_GPOE6                           0x00000040
#define EGPIODR_GPOE7                           0x00000080
#define EGPIODR_GPOE8                           0x00000100
#endif

/*
 *  The following defines are for the flags in the extended GPIO pin polarity/
 *  type register.
 */
#ifndef NO_CS4612
#define EGPIOPTR_GPPT0                          0x00000001
#define EGPIOPTR_GPPT1                          0x00000002
#define EGPIOPTR_GPPT2                          0x00000004
#define EGPIOPTR_GPPT3                          0x00000008
#define EGPIOPTR_GPPT4                          0x00000010
#define EGPIOPTR_GPPT5                          0x00000020
#define EGPIOPTR_GPPT6                          0x00000040
#define EGPIOPTR_GPPT7                          0x00000080
#define EGPIOPTR_GPPT8                          0x00000100
#endif

/*
 *  The following defines are for the flags in the extended GPIO pin sticky
 *  register.
 */
#ifndef NO_CS4612
#define EGPIOTR_GPS0                            0x00000001
#define EGPIOTR_GPS1                            0x00000002
#define EGPIOTR_GPS2                            0x00000004
#define EGPIOTR_GPS3                            0x00000008
#define EGPIOTR_GPS4                            0x00000010
#define EGPIOTR_GPS5                            0x00000020
#define EGPIOTR_GPS6                            0x00000040
#define EGPIOTR_GPS7                            0x00000080
#define EGPIOTR_GPS8                            0x00000100
#endif

/*
 *  The following defines are for the flags in the extended GPIO ping wakeup
 *  register.
 */
#ifndef NO_CS4612
#define EGPIOWR_GPW0                            0x00000001
#define EGPIOWR_GPW1                            0x00000002
#define EGPIOWR_GPW2                            0x00000004
#define EGPIOWR_GPW3                            0x00000008
#define EGPIOWR_GPW4                            0x00000010
#define EGPIOWR_GPW5                            0x00000020
#define EGPIOWR_GPW6                            0x00000040
#define EGPIOWR_GPW7                            0x00000080
#define EGPIOWR_GPW8                            0x00000100
#endif

/*
 *  The following defines are for the flags in the extended GPIO pin status
 *  register.
 */
#ifndef NO_CS4612
#define EGPIOSR_GPS0                            0x00000001
#define EGPIOSR_GPS1                            0x00000002
#define EGPIOSR_GPS2                            0x00000004
#define EGPIOSR_GPS3                            0x00000008
#define EGPIOSR_GPS4                            0x00000010
#define EGPIOSR_GPS5                            0x00000020
#define EGPIOSR_GPS6                            0x00000040
#define EGPIOSR_GPS7                            0x00000080
#define EGPIOSR_GPS8                            0x00000100
#endif

/*
 *  The following defines are for the flags in the serial port 6 configuration
 *  register.
 */
#ifndef NO_CS4612
#define SERC6_ASDO2EN                           0x00000001
#endif

/*
 *  The following defines are for the flags in the serial port 7 configuration
 *  register.
 */
#ifndef NO_CS4612
#define SERC7_ASDI2EN                           0x00000001
#define SERC7_POSILB                            0x00000002
#define SERC7_SIPOLB                            0x00000004
#define SERC7_SOSILB                            0x00000008
#define SERC7_SISOLB                            0x00000010
#endif

/*
 *  The following defines are for the flags in the serial port AC link
 *  configuration register.
 */
#ifndef NO_CS4612
#define SERACC_CHIP_TYPE_MASK                  0x00000001
#define SERACC_CHIP_TYPE_1_03                  0x00000000
#define SERACC_CHIP_TYPE_2_0                   0x00000001
#define SERACC_TWO_CODECS                      0x00000002
#define SERACC_MDM                             0x00000004
#define SERACC_HSP                             0x00000008
#define SERACC_ODT                             0x00000010 /* only CS4630 */
#endif

/*
 *  The following defines are for the flags in the AC97 control register 2.
 */
#ifndef NO_CS4612
#define ACCTL2_RSTN                             0x00000001
#define ACCTL2_ESYN                             0x00000002
#define ACCTL2_VFRM                             0x00000004
#define ACCTL2_DCV                              0x00000008
#define ACCTL2_CRW                              0x00000010
#define ACCTL2_ASYN                             0x00000020
#endif

/*
 *  The following defines are for the flags in the AC97 status register 2.
 */
#ifndef NO_CS4612
#define ACSTS2_CRDY                             0x00000001
#define ACSTS2_VSTS                             0x00000002
#endif

/*
 *  The following defines are for the flags in the AC97 output slot valid
 *  register 2.
 */
#ifndef NO_CS4612
#define ACOSV2_SLV3                             0x00000001
#define ACOSV2_SLV4                             0x00000002
#define ACOSV2_SLV5                             0x00000004
#define ACOSV2_SLV6                             0x00000008
#define ACOSV2_SLV7                             0x00000010
#define ACOSV2_SLV8                             0x00000020
#define ACOSV2_SLV9                             0x00000040
#define ACOSV2_SLV10                            0x00000080
#define ACOSV2_SLV11                            0x00000100
#define ACOSV2_SLV12                            0x00000200
#endif

/*
 *  The following defines are for the flags in the AC97 command address
 *  register 2.
 */
#ifndef NO_CS4612
#define ACCAD2_CI_MASK                          0x0000007F
#define ACCAD2_CI_SHIFT                         0
#endif

/*
 *  The following defines are for the flags in the AC97 command data register
 *  2.
 */
#ifndef NO_CS4612
#define ACCDA2_CD_MASK                          0x0000FFFF
#define ACCDA2_CD_SHIFT                         0  
#endif

/*
 *  The following defines are for the flags in the AC97 input slot valid
 *  register 2.
 */
#ifndef NO_CS4612
#define ACISV2_ISV3                             0x00000001
#define ACISV2_ISV4                             0x00000002
#define ACISV2_ISV5                             0x00000004
#define ACISV2_ISV6                             0x00000008
#define ACISV2_ISV7                             0x00000010
#define ACISV2_ISV8                             0x00000020
#define ACISV2_ISV9                             0x00000040
#define ACISV2_ISV10                            0x00000080
#define ACISV2_ISV11                            0x00000100
#define ACISV2_ISV12                            0x00000200
#endif

/*
 *  The following defines are for the flags in the AC97 status address
 *  register 2.
 */
#ifndef NO_CS4612
#define ACSAD2_SI_MASK                          0x0000007F
#define ACSAD2_SI_SHIFT                         0
#endif

/*
 *  The following defines are for the flags in the AC97 status data register 2.
 */
#ifndef NO_CS4612
#define ACSDA2_SD_MASK                          0x0000FFFF
#define ACSDA2_SD_SHIFT                         0
#endif

/*
 *  The following defines are for the flags in the I/O trap address and control
 *  registers (all 12).
 */
#ifndef NO_CS4612
#define IOTAC_SA_MASK                           0x0000FFFF
#define IOTAC_MSK_MASK                          0x000F0000
#define IOTAC_IODC_MASK                         0x06000000
#define IOTAC_IODC_16_BIT                       0x00000000
#define IOTAC_IODC_10_BIT                       0x02000000
#define IOTAC_IODC_12_BIT                       0x04000000
#define IOTAC_WSPI                              0x08000000
#define IOTAC_RSPI                              0x10000000
#define IOTAC_WSE                               0x20000000
#define IOTAC_WE                                0x40000000
#define IOTAC_RE                                0x80000000
#define IOTAC_SA_SHIFT                          0
#define IOTAC_MSK_SHIFT                         16
#endif

/*
 *  The following defines are for the flags in the I/O trap fast read registers
 *  (all 8).
 */
#ifndef NO_CS4612
#define IOTFR_D_MASK                            0x0000FFFF
#define IOTFR_A_MASK                            0x000F0000
#define IOTFR_R_MASK                            0x0F000000
#define IOTFR_ALL                               0x40000000
#define IOTFR_VL                                0x80000000
#define IOTFR_D_SHIFT                           0
#define IOTFR_A_SHIFT                           16
#define IOTFR_R_SHIFT                           24
#endif

/*
 *  The following defines are for the flags in the I/O trap FIFO register.
 */
#ifndef NO_CS4612
#define IOTFIFO_BA_MASK                         0x00003FFF
#define IOTFIFO_S_MASK                          0x00FF0000
#define IOTFIFO_OF                              0x40000000
#define IOTFIFO_SPIOF                           0x80000000
#define IOTFIFO_BA_SHIFT                        0
#define IOTFIFO_S_SHIFT                         16
#endif

/*
 *  The following defines are for the flags in the I/O trap retry read data
 *  register.
 */
#ifndef NO_CS4612
#define IOTRRD_D_MASK                           0x0000FFFF
#define IOTRRD_RDV                              0x80000000
#define IOTRRD_D_SHIFT                          0
#endif

/*
 *  The following defines are for the flags in the I/O trap FIFO pointer
 *  register.
 */
#ifndef NO_CS4612
#define IOTFP_CA_MASK                           0x00003FFF
#define IOTFP_PA_MASK                           0x3FFF0000
#define IOTFP_CA_SHIFT                          0
#define IOTFP_PA_SHIFT                          16
#endif

/*
 *  The following defines are for the flags in the I/O trap control register.
 */
#ifndef NO_CS4612
#define IOTCR_ITD                               0x00000001
#define IOTCR_HRV                               0x00000002
#define IOTCR_SRV                               0x00000004
#define IOTCR_DTI                               0x00000008
#define IOTCR_DFI                               0x00000010
#define IOTCR_DDP                               0x00000020
#define IOTCR_JTE                               0x00000040
#define IOTCR_PPE                               0x00000080
#endif

/*
 *  The following defines are for the flags in the direct PCI data register.
 */
#ifndef NO_CS4612
#define DPCID_D_MASK                            0xFFFFFFFF
#define DPCID_D_SHIFT                           0
#endif

/*
 *  The following defines are for the flags in the direct PCI address register.
 */
#ifndef NO_CS4612
#define DPCIA_A_MASK                            0xFFFFFFFF
#define DPCIA_A_SHIFT                           0
#endif

/*
 *  The following defines are for the flags in the direct PCI command register.
 */
#ifndef NO_CS4612
#define DPCIC_C_MASK                            0x0000000F
#define DPCIC_C_IOREAD                          0x00000002
#define DPCIC_C_IOWRITE                         0x00000003
#define DPCIC_BE_MASK                           0x000000F0
#endif

/*
 *  The following defines are for the flags in the PC/PCI request register.
 */
#ifndef NO_CS4612
#define PCPCIR_RDC_MASK                         0x00000007
#define PCPCIR_C_MASK                           0x00007000
#define PCPCIR_REQ                              0x00008000
#define PCPCIR_RDC_SHIFT                        0
#define PCPCIR_C_SHIFT                          12
#endif

/*
 *  The following defines are for the flags in the PC/PCI grant register.
 */ 
#ifndef NO_CS4612
#define PCPCIG_GDC_MASK                         0x00000007
#define PCPCIG_VL                               0x00008000
#define PCPCIG_GDC_SHIFT                        0
#endif

/*
 *  The following defines are for the flags in the PC/PCI master enable
 *  register.
 */
#ifndef NO_CS4612
#define PCPCIEN_EN                              0x00000001
#endif

/*
 *  The following defines are for the flags in the extended PCI power
 *  management control register.
 */
#ifndef NO_CS4612
#define EPCIPMC_GWU                             0x00000001
#define EPCIPMC_FSPC                            0x00000002
#endif 

/*
 *  The following defines are for the flags in the SP control register.
 */
#define SPCR_RUN                                0x00000001
#define SPCR_STPFR                              0x00000002
#define SPCR_RUNFR                              0x00000004
#define SPCR_TICK                               0x00000008
#define SPCR_DRQEN                              0x00000020
#define SPCR_RSTSP                              0x00000040
#define SPCR_OREN                               0x00000080
#ifndef NO_CS4612
#define SPCR_PCIINT                             0x00000100
#define SPCR_OINTD                              0x00000200
#define SPCR_CRE                                0x00008000
#endif

/*
 *  The following defines are for the flags in the debug index register.
 */
#define DREG_REGID_MASK                         0x0000007F
#define DREG_DEBUG                              0x00000080
#define DREG_RGBK_MASK                          0x00000700
#define DREG_TRAP                               0x00000800
#if !defined(NO_CS4612)
#if !defined(NO_CS4615)
#define DREG_TRAPX                              0x00001000
#endif
#endif
#define DREG_REGID_SHIFT                        0
#define DREG_RGBK_SHIFT                         8
#define DREG_RGBK_REGID_MASK                    0x0000077F
#define DREG_REGID_R0                           0x00000010
#define DREG_REGID_R1                           0x00000011
#define DREG_REGID_R2                           0x00000012
#define DREG_REGID_R3                           0x00000013
#define DREG_REGID_R4                           0x00000014
#define DREG_REGID_R5                           0x00000015
#define DREG_REGID_R6                           0x00000016
#define DREG_REGID_R7                           0x00000017
#define DREG_REGID_R8                           0x00000018
#define DREG_REGID_R9                           0x00000019
#define DREG_REGID_RA                           0x0000001A
#define DREG_REGID_RB                           0x0000001B
#define DREG_REGID_RC                           0x0000001C
#define DREG_REGID_RD                           0x0000001D
#define DREG_REGID_RE                           0x0000001E
#define DREG_REGID_RF                           0x0000001F
#define DREG_REGID_RA_BUS_LOW                   0x00000020
#define DREG_REGID_RA_BUS_HIGH                  0x00000038
#define DREG_REGID_YBUS_LOW                     0x00000050
#define DREG_REGID_YBUS_HIGH                    0x00000058
#define DREG_REGID_TRAP_0                       0x00000100
#define DREG_REGID_TRAP_1                       0x00000101
#define DREG_REGID_TRAP_2                       0x00000102
#define DREG_REGID_TRAP_3                       0x00000103
#define DREG_REGID_TRAP_4                       0x00000104
#define DREG_REGID_TRAP_5                       0x00000105
#define DREG_REGID_TRAP_6                       0x00000106
#define DREG_REGID_TRAP_7                       0x00000107
#define DREG_REGID_INDIRECT_ADDRESS             0x0000010E
#define DREG_REGID_TOP_OF_STACK                 0x0000010F
#if !defined(NO_CS4612)
#if !defined(NO_CS4615)
#define DREG_REGID_TRAP_8                       0x00000110
#define DREG_REGID_TRAP_9                       0x00000111
#define DREG_REGID_TRAP_10                      0x00000112
#define DREG_REGID_TRAP_11                      0x00000113
#define DREG_REGID_TRAP_12                      0x00000114
#define DREG_REGID_TRAP_13                      0x00000115
#define DREG_REGID_TRAP_14                      0x00000116
#define DREG_REGID_TRAP_15                      0x00000117
#define DREG_REGID_TRAP_16                      0x00000118
#define DREG_REGID_TRAP_17                      0x00000119
#define DREG_REGID_TRAP_18                      0x0000011A
#define DREG_REGID_TRAP_19                      0x0000011B
#define DREG_REGID_TRAP_20                      0x0000011C
#define DREG_REGID_TRAP_21                      0x0000011D
#define DREG_REGID_TRAP_22                      0x0000011E
#define DREG_REGID_TRAP_23                      0x0000011F
#endif
#endif
#define DREG_REGID_RSA0_LOW                     0x00000200
#define DREG_REGID_RSA0_HIGH                    0x00000201
#define DREG_REGID_RSA1_LOW                     0x00000202
#define DREG_REGID_RSA1_HIGH                    0x00000203
#define DREG_REGID_RSA2                         0x00000204
#define DREG_REGID_RSA3                         0x00000205
#define DREG_REGID_RSI0_LOW                     0x00000206
#define DREG_REGID_RSI0_HIGH                    0x00000207
#define DREG_REGID_RSI1                         0x00000208
#define DREG_REGID_RSI2                         0x00000209
#define DREG_REGID_SAGUSTATUS                   0x0000020A
#define DREG_REGID_RSCONFIG01_LOW               0x0000020B
#define DREG_REGID_RSCONFIG01_HIGH              0x0000020C
#define DREG_REGID_RSCONFIG23_LOW               0x0000020D
#define DREG_REGID_RSCONFIG23_HIGH              0x0000020E
#define DREG_REGID_RSDMA01E                     0x0000020F
#define DREG_REGID_RSDMA23E                     0x00000210
#define DREG_REGID_RSD0_LOW                     0x00000211
#define DREG_REGID_RSD0_HIGH                    0x00000212
#define DREG_REGID_RSD1_LOW                     0x00000213
#define DREG_REGID_RSD1_HIGH                    0x00000214
#define DREG_REGID_RSD2_LOW                     0x00000215
#define DREG_REGID_RSD2_HIGH                    0x00000216
#define DREG_REGID_RSD3_LOW                     0x00000217
#define DREG_REGID_RSD3_HIGH                    0x00000218
#define DREG_REGID_SRAR_HIGH                    0x0000021A
#define DREG_REGID_SRAR_LOW                     0x0000021B
#define DREG_REGID_DMA_STATE                    0x0000021C
#define DREG_REGID_CURRENT_DMA_STREAM           0x0000021D
#define DREG_REGID_NEXT_DMA_STREAM              0x0000021E
#define DREG_REGID_CPU_STATUS                   0x00000300
#define DREG_REGID_MAC_MODE                     0x00000301
#define DREG_REGID_STACK_AND_REPEAT             0x00000302
#define DREG_REGID_INDEX0                       0x00000304
#define DREG_REGID_INDEX1                       0x00000305
#define DREG_REGID_DMA_STATE_0_3                0x00000400
#define DREG_REGID_DMA_STATE_4_7                0x00000404
#define DREG_REGID_DMA_STATE_8_11               0x00000408
#define DREG_REGID_DMA_STATE_12_15              0x0000040C
#define DREG_REGID_DMA_STATE_16_19              0x00000410
#define DREG_REGID_DMA_STATE_20_23              0x00000414
#define DREG_REGID_DMA_STATE_24_27              0x00000418
#define DREG_REGID_DMA_STATE_28_31              0x0000041C
#define DREG_REGID_DMA_STATE_32_35              0x00000420
#define DREG_REGID_DMA_STATE_36_39              0x00000424
#define DREG_REGID_DMA_STATE_40_43              0x00000428
#define DREG_REGID_DMA_STATE_44_47              0x0000042C
#define DREG_REGID_DMA_STATE_48_51              0x00000430
#define DREG_REGID_DMA_STATE_52_55              0x00000434
#define DREG_REGID_DMA_STATE_56_59              0x00000438
#define DREG_REGID_DMA_STATE_60_63              0x0000043C
#define DREG_REGID_DMA_STATE_64_67              0x00000440
#define DREG_REGID_DMA_STATE_68_71              0x00000444
#define DREG_REGID_DMA_STATE_72_75              0x00000448
#define DREG_REGID_DMA_STATE_76_79              0x0000044C
#define DREG_REGID_DMA_STATE_80_83              0x00000450
#define DREG_REGID_DMA_STATE_84_87              0x00000454
#define DREG_REGID_DMA_STATE_88_91              0x00000458
#define DREG_REGID_DMA_STATE_92_95              0x0000045C
#define DREG_REGID_TRAP_SELECT                  0x00000500
#define DREG_REGID_TRAP_WRITE_0                 0x00000500
#define DREG_REGID_TRAP_WRITE_1                 0x00000501
#define DREG_REGID_TRAP_WRITE_2                 0x00000502
#define DREG_REGID_TRAP_WRITE_3                 0x00000503
#define DREG_REGID_TRAP_WRITE_4                 0x00000504
#define DREG_REGID_TRAP_WRITE_5                 0x00000505
#define DREG_REGID_TRAP_WRITE_6                 0x00000506
#define DREG_REGID_TRAP_WRITE_7                 0x00000507
#if !defined(NO_CS4612)
#if !defined(NO_CS4615)
#define DREG_REGID_TRAP_WRITE_8                 0x00000510
#define DREG_REGID_TRAP_WRITE_9                 0x00000511
#define DREG_REGID_TRAP_WRITE_10                0x00000512
#define DREG_REGID_TRAP_WRITE_11                0x00000513
#define DREG_REGID_TRAP_WRITE_12                0x00000514
#define DREG_REGID_TRAP_WRITE_13                0x00000515
#define DREG_REGID_TRAP_WRITE_14                0x00000516
#define DREG_REGID_TRAP_WRITE_15                0x00000517
#define DREG_REGID_TRAP_WRITE_16                0x00000518
#define DREG_REGID_TRAP_WRITE_17                0x00000519
#define DREG_REGID_TRAP_WRITE_18                0x0000051A
#define DREG_REGID_TRAP_WRITE_19                0x0000051B
#define DREG_REGID_TRAP_WRITE_20                0x0000051C
#define DREG_REGID_TRAP_WRITE_21                0x0000051D
#define DREG_REGID_TRAP_WRITE_22                0x0000051E
#define DREG_REGID_TRAP_WRITE_23                0x0000051F
#endif
#endif
#define DREG_REGID_MAC0_ACC0_LOW                0x00000600
#define DREG_REGID_MAC0_ACC1_LOW                0x00000601
#define DREG_REGID_MAC0_ACC2_LOW                0x00000602
#define DREG_REGID_MAC0_ACC3_LOW                0x00000603
#define DREG_REGID_MAC1_ACC0_LOW                0x00000604
#define DREG_REGID_MAC1_ACC1_LOW                0x00000605
#define DREG_REGID_MAC1_ACC2_LOW                0x00000606
#define DREG_REGID_MAC1_ACC3_LOW                0x00000607
#define DREG_REGID_MAC0_ACC0_MID                0x00000608
#define DREG_REGID_MAC0_ACC1_MID                0x00000609
#define DREG_REGID_MAC0_ACC2_MID                0x0000060A
#define DREG_REGID_MAC0_ACC3_MID                0x0000060B
#define DREG_REGID_MAC1_ACC0_MID                0x0000060C
#define DREG_REGID_MAC1_ACC1_MID                0x0000060D
#define DREG_REGID_MAC1_ACC2_MID                0x0000060E
#define DREG_REGID_MAC1_ACC3_MID                0x0000060F
#define DREG_REGID_MAC0_ACC0_HIGH               0x00000610
#define DREG_REGID_MAC0_ACC1_HIGH               0x00000611
#define DREG_REGID_MAC0_ACC2_HIGH               0x00000612
#define DREG_REGID_MAC0_ACC3_HIGH               0x00000613
#define DREG_REGID_MAC1_ACC0_HIGH               0x00000614
#define DREG_REGID_MAC1_ACC1_HIGH               0x00000615
#define DREG_REGID_MAC1_ACC2_HIGH               0x00000616
#define DREG_REGID_MAC1_ACC3_HIGH               0x00000617
#define DREG_REGID_RSHOUT_LOW                   0x00000620
#define DREG_REGID_RSHOUT_MID                   0x00000628
#define DREG_REGID_RSHOUT_HIGH                  0x00000630

/*
 *  The following defines are for the flags in the DMA stream requestor write
 */
#define DSRWP_DSR_MASK                          0x0000000F
#define DSRWP_DSR_BG_RQ                         0x00000001
#define DSRWP_DSR_PRIORITY_MASK                 0x00000006
#define DSRWP_DSR_PRIORITY_0                    0x00000000
#define DSRWP_DSR_PRIORITY_1                    0x00000002
#define DSRWP_DSR_PRIORITY_2                    0x00000004
#define DSRWP_DSR_PRIORITY_3                    0x00000006
#define DSRWP_DSR_RQ_PENDING                    0x00000008

/*
 *  The following defines are for the flags in the trap write port register.
 */
#define TWPR_TW_MASK                            0x0000FFFF
#define TWPR_TW_SHIFT                           0

/*
 *  The following defines are for the flags in the stack pointer write
 *  register.
 */
#define SPWR_STKP_MASK                          0x0000000F
#define SPWR_STKP_SHIFT                         0

/*
 *  The following defines are for the flags in the SP interrupt register.
 */
#define SPIR_FRI                                0x00000001
#define SPIR_DOI                                0x00000002
#define SPIR_GPI2                               0x00000004
#define SPIR_GPI3                               0x00000008
#define SPIR_IP0                                0x00000010
#define SPIR_IP1                                0x00000020
#define SPIR_IP2                                0x00000040
#define SPIR_IP3                                0x00000080

/*
 *  The following defines are for the flags in the functional group 1 register.
 */
#define FGR1_F1S_MASK                           0x0000FFFF
#define FGR1_F1S_SHIFT                          0

/*
 *  The following defines are for the flags in the SP clock status register.
 */
#define SPCS_FRI                                0x00000001
#define SPCS_DOI                                0x00000002
#define SPCS_GPI2                               0x00000004
#define SPCS_GPI3                               0x00000008
#define SPCS_IP0                                0x00000010
#define SPCS_IP1                                0x00000020
#define SPCS_IP2                                0x00000040
#define SPCS_IP3                                0x00000080
#define SPCS_SPRUN                              0x00000100
#define SPCS_SLEEP                              0x00000200
#define SPCS_FG                                 0x00000400
#define SPCS_ORUN                               0x00000800
#define SPCS_IRQ                                0x00001000
#define SPCS_FGN_MASK                           0x0000E000
#define SPCS_FGN_SHIFT                          13

/*
 *  The following defines are for the flags in the SP DMA requestor status
 *  register.
 */
#define SDSR_DCS_MASK                           0x000000FF
#define SDSR_DCS_SHIFT                          0
#define SDSR_DCS_NONE                           0x00000007

/*
 *  The following defines are for the flags in the frame timer register.
 */
#define FRMT_FTV_MASK                           0x0000FFFF
#define FRMT_FTV_SHIFT                          0

/*
 *  The following defines are for the flags in the frame timer current count
 *  register.
 */
#define FRCC_FCC_MASK                           0x0000FFFF
#define FRCC_FCC_SHIFT                          0

/*
 *  The following defines are for the flags in the frame timer save count
 *  register.
 */
#define FRSC_FCS_MASK                           0x0000FFFF
#define FRSC_FCS_SHIFT                          0

/*
 *  The following define the various flags stored in the scatter/gather
 *  descriptors.
 */
#define DMA_SG_NEXT_ENTRY_MASK                  0x00000FF8
#define DMA_SG_SAMPLE_END_MASK                  0x0FFF0000
#define DMA_SG_SAMPLE_END_FLAG                  0x10000000
#define DMA_SG_LOOP_END_FLAG                    0x20000000
#define DMA_SG_SIGNAL_END_FLAG                  0x40000000
#define DMA_SG_SIGNAL_PAGE_FLAG                 0x80000000
#define DMA_SG_NEXT_ENTRY_SHIFT                 3
#define DMA_SG_SAMPLE_END_SHIFT                 16

/*
 *  The following define the offsets of the fields within the on-chip generic
 *  DMA requestor.
 */
#define DMA_RQ_CONTROL1                         0x00000000
#define DMA_RQ_CONTROL2                         0x00000004
#define DMA_RQ_SOURCE_ADDR                      0x00000008
#define DMA_RQ_DESTINATION_ADDR                 0x0000000C
#define DMA_RQ_NEXT_PAGE_ADDR                   0x00000010
#define DMA_RQ_NEXT_PAGE_SGDESC                 0x00000014
#define DMA_RQ_LOOP_START_ADDR                  0x00000018
#define DMA_RQ_POST_LOOP_ADDR                   0x0000001C
#define DMA_RQ_PAGE_MAP_ADDR                    0x00000020

/*
 *  The following defines are for the flags in the first control word of the
 *  on-chip generic DMA requestor.
 */
#define DMA_RQ_C1_COUNT_MASK                    0x000003FF
#define DMA_RQ_C1_DESTINATION_SCATTER           0x00001000
#define DMA_RQ_C1_SOURCE_GATHER                 0x00002000
#define DMA_RQ_C1_DONE_FLAG                     0x00004000
#define DMA_RQ_C1_OPTIMIZE_STATE                0x00008000
#define DMA_RQ_C1_SAMPLE_END_STATE_MASK         0x00030000
#define DMA_RQ_C1_FULL_PAGE                     0x00000000
#define DMA_RQ_C1_BEFORE_SAMPLE_END             0x00010000
#define DMA_RQ_C1_PAGE_MAP_ERROR                0x00020000
#define DMA_RQ_C1_AT_SAMPLE_END                 0x00030000
#define DMA_RQ_C1_LOOP_END_STATE_MASK           0x000C0000
#define DMA_RQ_C1_NOT_LOOP_END                  0x00000000
#define DMA_RQ_C1_BEFORE_LOOP_END               0x00040000
#define DMA_RQ_C1_2PAGE_LOOP_BEGIN              0x00080000
#define DMA_RQ_C1_LOOP_BEGIN                    0x000C0000
#define DMA_RQ_C1_PAGE_MAP_MASK                 0x00300000
#define DMA_RQ_C1_PM_NONE_PENDING               0x00000000
#define DMA_RQ_C1_PM_NEXT_PENDING               0x00100000
#define DMA_RQ_C1_PM_RESERVED                   0x00200000
#define DMA_RQ_C1_PM_LOOP_NEXT_PENDING          0x00300000
#define DMA_RQ_C1_WRITEBACK_DEST_FLAG           0x00400000
#define DMA_RQ_C1_WRITEBACK_SRC_FLAG            0x00800000
#define DMA_RQ_C1_DEST_SIZE_MASK                0x07000000
#define DMA_RQ_C1_DEST_LINEAR                   0x00000000
#define DMA_RQ_C1_DEST_MOD16                    0x01000000
#define DMA_RQ_C1_DEST_MOD32                    0x02000000
#define DMA_RQ_C1_DEST_MOD64                    0x03000000
#define DMA_RQ_C1_DEST_MOD128                   0x04000000
#define DMA_RQ_C1_DEST_MOD256                   0x05000000
#define DMA_RQ_C1_DEST_MOD512                   0x06000000
#define DMA_RQ_C1_DEST_MOD1024                  0x07000000
#define DMA_RQ_C1_DEST_ON_HOST                  0x08000000
#define DMA_RQ_C1_SOURCE_SIZE_MASK              0x70000000
#define DMA_RQ_C1_SOURCE_LINEAR                 0x00000000
#define DMA_RQ_C1_SOURCE_MOD16                  0x10000000
#define DMA_RQ_C1_SOURCE_MOD32                  0x20000000
#define DMA_RQ_C1_SOURCE_MOD64                  0x30000000
#define DMA_RQ_C1_SOURCE_MOD128                 0x40000000
#define DMA_RQ_C1_SOURCE_MOD256                 0x50000000
#define DMA_RQ_C1_SOURCE_MOD512                 0x60000000
#define DMA_RQ_C1_SOURCE_MOD1024                0x70000000
#define DMA_RQ_C1_SOURCE_ON_HOST                0x80000000
#define DMA_RQ_C1_COUNT_SHIFT                   0

/*
 *  The following defines are for the flags in the second control word of the
 *  on-chip generic DMA requestor.
 */
#define DMA_RQ_C2_VIRTUAL_CHANNEL_MASK          0x0000003F
#define DMA_RQ_C2_VIRTUAL_SIGNAL_MASK           0x00000300
#define DMA_RQ_C2_NO_VIRTUAL_SIGNAL             0x00000000
#define DMA_RQ_C2_SIGNAL_EVERY_DMA              0x00000100
#define DMA_RQ_C2_SIGNAL_SOURCE_PINGPONG        0x00000200
#define DMA_RQ_C2_SIGNAL_DEST_PINGPONG          0x00000300
#define DMA_RQ_C2_AUDIO_CONVERT_MASK            0x0000F000
#define DMA_RQ_C2_AC_NONE                       0x00000000
#define DMA_RQ_C2_AC_8_TO_16_BIT                0x00001000
#define DMA_RQ_C2_AC_MONO_TO_STEREO             0x00002000
#define DMA_RQ_C2_AC_ENDIAN_CONVERT             0x00004000
#define DMA_RQ_C2_AC_SIGNED_CONVERT             0x00008000
#define DMA_RQ_C2_LOOP_END_MASK                 0x0FFF0000
#define DMA_RQ_C2_LOOP_MASK                     0x30000000
#define DMA_RQ_C2_NO_LOOP                       0x00000000
#define DMA_RQ_C2_ONE_PAGE_LOOP                 0x10000000
#define DMA_RQ_C2_TWO_PAGE_LOOP                 0x20000000
#define DMA_RQ_C2_MULTI_PAGE_LOOP               0x30000000
#define DMA_RQ_C2_SIGNAL_LOOP_BACK              0x40000000
#define DMA_RQ_C2_SIGNAL_POST_BEGIN_PAGE        0x80000000
#define DMA_RQ_C2_VIRTUAL_CHANNEL_SHIFT         0
#define DMA_RQ_C2_LOOP_END_SHIFT                16

/*
 *  The following defines are for the flags in the source and destination words
 *  of the on-chip generic DMA requestor.
 */
#define DMA_RQ_SD_ADDRESS_MASK                  0x0000FFFF
#define DMA_RQ_SD_MEMORY_ID_MASK                0x000F0000
#define DMA_RQ_SD_SP_PARAM_ADDR                 0x00000000
#define DMA_RQ_SD_SP_SAMPLE_ADDR                0x00010000
#define DMA_RQ_SD_SP_PROGRAM_ADDR               0x00020000
#define DMA_RQ_SD_SP_DEBUG_ADDR                 0x00030000
#define DMA_RQ_SD_OMNIMEM_ADDR                  0x000E0000
#define DMA_RQ_SD_END_FLAG                      0x40000000
#define DMA_RQ_SD_ERROR_FLAG                    0x80000000
#define DMA_RQ_SD_ADDRESS_SHIFT                 0

/*
 *  The following defines are for the flags in the page map address word of the
 *  on-chip generic DMA requestor.
 */
#define DMA_RQ_PMA_LOOP_THIRD_PAGE_ENTRY_MASK   0x00000FF8
#define DMA_RQ_PMA_PAGE_TABLE_MASK              0xFFFFF000
#define DMA_RQ_PMA_LOOP_THIRD_PAGE_ENTRY_SHIFT  3
#define DMA_RQ_PMA_PAGE_TABLE_SHIFT             12

#define BA1_VARIDEC_BUF_1       0x000

#define BA1_PDTC                0x0c0    /* BA1_PLAY_DMA_TRANSACTION_COUNT_REG */
#define BA1_PFIE                0x0c4    /* BA1_PLAY_FORMAT_&_INTERRUPT_ENABLE_REG */
#define BA1_PBA                 0x0c8    /* BA1_PLAY_BUFFER_ADDRESS */
#define BA1_PVOL                0x0f8    /* BA1_PLAY_VOLUME_REG */
#define BA1_PSRC                0x288    /* BA1_PLAY_SAMPLE_RATE_CORRECTION_REG */
#define BA1_PCTL                0x2a4    /* BA1_PLAY_CONTROL_REG */
#define BA1_PPI                 0x2b4    /* BA1_PLAY_PHASE_INCREMENT_REG */

#define BA1_CCTL                0x064    /* BA1_CAPTURE_CONTROL_REG */
#define BA1_CIE                 0x104    /* BA1_CAPTURE_INTERRUPT_ENABLE_REG */
#define BA1_CBA                 0x10c    /* BA1_CAPTURE_BUFFER_ADDRESS */
#define BA1_CSRC                0x2c8    /* BA1_CAPTURE_SAMPLE_RATE_CORRECTION_REG */
#define BA1_CCI                 0x2d8    /* BA1_CAPTURE_COEFFICIENT_INCREMENT_REG */
#define BA1_CD                  0x2e0    /* BA1_CAPTURE_DELAY_REG */
#define BA1_CPI                 0x2f4    /* BA1_CAPTURE_PHASE_INCREMENT_REG */
#define BA1_CVOL                0x2f8    /* BA1_CAPTURE_VOLUME_REG */

#define BA1_CFG1                0x134    /* BA1_CAPTURE_FRAME_GROUP_1_REG */
#define BA1_CFG2                0x138    /* BA1_CAPTURE_FRAME_GROUP_2_REG */
#define BA1_CCST                0x13c    /* BA1_CAPTURE_CONSTANT_REG */
#define BA1_CSPB                0x340    /* BA1_CAPTURE_SPB_ADDRESS */

/*
 *
 */

#define CS46XX_MODE_OUTPUT	(1<<0)	 /* MIDI UART - output */ 
#define CS46XX_MODE_INPUT	(1<<1)	 /* MIDI UART - input */

/*
 *
 */

#define SAVE_REG_MAX             0x10
#define POWER_DOWN_ALL         0x7f0f

/* maxinum number of AC97 codecs connected, AC97 2.0 defined 4 */
#define MAX_NR_AC97				            4
#define CS46XX_PRIMARY_CODEC_INDEX          0
#define CS46XX_SECONDARY_CODEC_INDEX		1
#define CS46XX_SECONDARY_CODEC_OFFSET		0x80
#define CS46XX_DSP_CAPTURE_CHANNEL          1

/* capture */
#define CS46XX_DSP_CAPTURE_CHANNEL          1

/* mixer */
#define CS46XX_MIXER_SPDIF_INPUT_ELEMENT    1
#define CS46XX_MIXER_SPDIF_OUTPUT_ELEMENT   2


struct snd_cs46xx_pcm {
	struct snd_dma_buffer hw_buf;
  
	unsigned int ctl;
	unsigned int shift;	/* Shift count to trasform frames in bytes */
	struct snd_pcm_indirect pcm_rec;
	struct snd_pcm_substream *substream;

	struct dsp_pcm_channel_descriptor * pcm_channel;

	int pcm_channel_id;    /* Fron Rear, Center Lfe  ... */
};

struct snd_cs46xx_region {
	char name[24];
	unsigned long base;
	void __iomem *remap_addr;
	unsigned long size;
	struct resource *resource;
};

struct snd_cs46xx {
	int irq;
	unsigned long ba0_addr;
	unsigned long ba1_addr;
	union {
		struct {
			struct snd_cs46xx_region ba0;
			struct snd_cs46xx_region data0;
			struct snd_cs46xx_region data1;
			struct snd_cs46xx_region pmem;
			struct snd_cs46xx_region reg;
		} name;
		struct snd_cs46xx_region idx[5];
	} region;

	unsigned int mode;
	
	struct {
		struct snd_dma_buffer hw_buf;

		unsigned int ctl;
		unsigned int shift;	/* Shift count to trasform frames in bytes */
		struct snd_pcm_indirect pcm_rec;
		struct snd_pcm_substream *substream;
	} capt;


	int nr_ac97_codecs;
	struct snd_ac97_bus *ac97_bus;
	struct snd_ac97 *ac97[MAX_NR_AC97];

	struct pci_dev *pci;
	struct snd_card *card;
	struct snd_pcm *pcm;

	struct snd_rawmidi *rmidi;
	struct snd_rawmidi_substream *midi_input;
	struct snd_rawmidi_substream *midi_output;

	spinlock_t reg_lock;
	unsigned int midcr;
	unsigned int uartm;

	int amplifier;
	void (*amplifier_ctrl)(struct snd_cs46xx *, int);
	void (*active_ctrl)(struct snd_cs46xx *, int);
  	void (*mixer_init)(struct snd_cs46xx *);

	int acpi_port;
	struct snd_kcontrol *eapd_switch; /* for amplifier hack */
	int accept_valid;	/* accept mmap valid (for OSS) */
	int in_suspend;

	struct gameport *gameport;

#ifdef CONFIG_SND_CS46XX_NEW_DSP
	struct mutex spos_mutex;

	struct dsp_spos_instance * dsp_spos_instance;

	struct snd_pcm *pcm_rear;
	struct snd_pcm *pcm_center_lfe;
	struct snd_pcm *pcm_iec958;

#define CS46XX_DSP_MODULES	5
	struct dsp_module_desc *modules[CS46XX_DSP_MODULES];
#else /* for compatibility */
	struct snd_cs46xx_pcm *playback_pcm;
	unsigned int play_ctl;

	struct ba1_struct *ba1;
#endif

#ifdef CONFIG_PM_SLEEP
	u32 *saved_regs;
#endif
};

int snd_cs46xx_create(struct snd_card *card,
		      struct pci_dev *pci,
		      int external_amp, int thinkpad,
		      struct snd_cs46xx **rcodec);
extern const struct dev_pm_ops snd_cs46xx_pm;

int snd_cs46xx_pcm(struct snd_cs46xx *chip, int device);
int snd_cs46xx_pcm_rear(struct snd_cs46xx *chip, int device);
int snd_cs46xx_pcm_iec958(struct snd_cs46xx *chip, int device);
int snd_cs46xx_pcm_center_lfe(struct snd_cs46xx *chip, int device);
int snd_cs46xx_mixer(struct snd_cs46xx *chip, int spdif_device);
int snd_cs46xx_midi(struct snd_cs46xx *chip, int device);
int snd_cs46xx_start_dsp(struct snd_cs46xx *chip);
int snd_cs46xx_gameport(struct snd_cs46xx *chip);

#endif /* __SOUND_CS46XX_H */
