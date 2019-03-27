/*-
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD
 *
 * Copyright (c) 1999 Seigo Tanimura
 * All rights reserved.
 *
 * Portions of this source are based on hwdefs.h in cwcealdr1.zip, the
 * sample source by Crystal Semiconductor.
 * Copyright (c) 1996-1998 Crystal Semiconductor Corp.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE AUTHOR OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 *
 * $FreeBSD$
 */

#ifndef _CSA_REG_H
#define _CSA_REG_H

/*
 * The following constats are orginally in the sample by Crystal Semiconductor.
 * Copyright (c) 1996-1998 Crystal Semiconductor Corp.
 */

/*****************************************************************************
 *
 * The following define the offsets of the registers accessed via base address
 * register zero on the CS461x part.
 *
 *****************************************************************************/
#define BA0_HISR                                0x00000000L
#define BA0_HSR0                                0x00000004L
#define BA0_HICR                                0x00000008L
#define BA0_DMSR                                0x00000100L
#define BA0_HSAR                                0x00000110L
#define BA0_HDAR                                0x00000114L
#define BA0_HDMR                                0x00000118L
#define BA0_HDCR                                0x0000011CL
#define BA0_PFMC                                0x00000200L
#define BA0_PFCV1                               0x00000204L
#define BA0_PFCV2                               0x00000208L
#define BA0_PCICFG00                            0x00000300L
#define BA0_PCICFG04                            0x00000304L
#define BA0_PCICFG08                            0x00000308L
#define BA0_PCICFG0C                            0x0000030CL
#define BA0_PCICFG10                            0x00000310L
#define BA0_PCICFG14                            0x00000314L
#define BA0_PCICFG18                            0x00000318L
#define BA0_PCICFG1C                            0x0000031CL
#define BA0_PCICFG20                            0x00000320L
#define BA0_PCICFG24                            0x00000324L
#define BA0_PCICFG28                            0x00000328L
#define BA0_PCICFG2C                            0x0000032CL
#define BA0_PCICFG30                            0x00000330L
#define BA0_PCICFG34                            0x00000334L
#define BA0_PCICFG38                            0x00000338L
#define BA0_PCICFG3C                            0x0000033CL
#define BA0_CLKCR1                              0x00000400L
#define BA0_CLKCR2                              0x00000404L
#define BA0_PLLM                                0x00000408L
#define BA0_PLLCC                               0x0000040CL
#define BA0_FRR                                 0x00000410L
#define BA0_CFL1                                0x00000414L
#define BA0_CFL2                                0x00000418L
#define BA0_SERMC1                              0x00000420L
#define BA0_SERMC2                              0x00000424L
#define BA0_SERC1                               0x00000428L
#define BA0_SERC2                               0x0000042CL
#define BA0_SERC3                               0x00000430L
#define BA0_SERC4                               0x00000434L
#define BA0_SERC5                               0x00000438L
#define BA0_SERBSP                              0x0000043CL
#define BA0_SERBST                              0x00000440L
#define BA0_SERBCM                              0x00000444L
#define BA0_SERBAD                              0x00000448L
#define BA0_SERBCF                              0x0000044CL
#define BA0_SERBWP                              0x00000450L
#define BA0_SERBRP                              0x00000454L
#ifndef NO_CS4612
#define BA0_ASER_FADDR                          0x00000458L
#endif
#define BA0_ACCTL                               0x00000460L
#define BA0_ACSTS                               0x00000464L
#define BA0_ACOSV                               0x00000468L
#define BA0_ACCAD                               0x0000046CL
#define BA0_ACCDA                               0x00000470L
#define BA0_ACISV                               0x00000474L
#define BA0_ACSAD                               0x00000478L
#define BA0_ACSDA                               0x0000047CL
#define BA0_JSPT                                0x00000480L
#define BA0_JSCTL                               0x00000484L
#define BA0_JSC1                                0x00000488L
#define BA0_JSC2                                0x0000048CL
#define BA0_MIDCR                               0x00000490L
#define BA0_MIDSR                               0x00000494L
#define BA0_MIDWP                               0x00000498L
#define BA0_MIDRP                               0x0000049CL
#define BA0_JSIO                                0x000004A0L
#ifndef NO_CS4612
#define BA0_ASER_MASTER                         0x000004A4L
#endif
#define BA0_CFGI                                0x000004B0L
#define BA0_SSVID                               0x000004B4L
#define BA0_GPIOR                               0x000004B8L
#ifndef NO_CS4612
#define BA0_EGPIODR                             0x000004BCL
#define BA0_EGPIOPTR                            0x000004C0L
#define BA0_EGPIOTR                             0x000004C4L
#define BA0_EGPIOWR                             0x000004C8L
#define BA0_EGPIOSR                             0x000004CCL
#define BA0_SERC6                               0x000004D0L
#define BA0_SERC7                               0x000004D4L
#define BA0_SERACC                              0x000004D8L
#define BA0_ACCTL2                              0x000004E0L
#define BA0_ACSTS2                              0x000004E4L
#define BA0_ACOSV2                              0x000004E8L
#define BA0_ACCAD2                              0x000004ECL
#define BA0_ACCDA2                              0x000004F0L
#define BA0_ACISV2                              0x000004F4L
#define BA0_ACSAD2                              0x000004F8L
#define BA0_ACSDA2                              0x000004FCL
#define BA0_IOTAC0                              0x00000500L
#define BA0_IOTAC1                              0x00000504L
#define BA0_IOTAC2                              0x00000508L
#define BA0_IOTAC3                              0x0000050CL
#define BA0_IOTAC4                              0x00000510L
#define BA0_IOTAC5                              0x00000514L
#define BA0_IOTAC6                              0x00000518L
#define BA0_IOTAC7                              0x0000051CL
#define BA0_IOTAC8                              0x00000520L
#define BA0_IOTAC9                              0x00000524L
#define BA0_IOTAC10                             0x00000528L
#define BA0_IOTAC11                             0x0000052CL
#define BA0_IOTFR0                              0x00000540L
#define BA0_IOTFR1                              0x00000544L
#define BA0_IOTFR2                              0x00000548L
#define BA0_IOTFR3                              0x0000054CL
#define BA0_IOTFR4                              0x00000550L
#define BA0_IOTFR5                              0x00000554L
#define BA0_IOTFR6                              0x00000558L
#define BA0_IOTFR7                              0x0000055CL
#define BA0_IOTFIFO                             0x00000580L
#define BA0_IOTRRD                              0x00000584L
#define BA0_IOTFP                               0x00000588L
#define BA0_IOTCR                               0x0000058CL
#define BA0_DPCID                               0x00000590L
#define BA0_DPCIA                               0x00000594L
#define BA0_DPCIC                               0x00000598L
#define BA0_PCPCIR                              0x00000600L
#define BA0_PCPCIG                              0x00000604L
#define BA0_PCPCIEN                             0x00000608L
#define BA0_EPCIPMC                             0x00000610L
#endif

/*****************************************************************************
 *
 * The following define the offsets of the AC97 shadow registers, which appear
 * as a virtual extension to the base address register zero memory range.
 *
 *****************************************************************************/
#define BA0_AC97_RESET                          0x00001000L
#define BA0_AC97_MASTER_VOLUME                  0x00001002L
#define BA0_AC97_HEADPHONE_VOLUME               0x00001004L
#define BA0_AC97_MASTER_VOLUME_MONO             0x00001006L
#define BA0_AC97_MASTER_TONE                    0x00001008L
#define BA0_AC97_PC_BEEP_VOLUME                 0x0000100AL
#define BA0_AC97_PHONE_VOLUME                   0x0000100CL
#define BA0_AC97_MIC_VOLUME                     0x0000100EL
#define BA0_AC97_LINE_IN_VOLUME                 0x00001010L
#define BA0_AC97_CD_VOLUME                      0x00001012L
#define BA0_AC97_VIDEO_VOLUME                   0x00001014L
#define BA0_AC97_AUX_VOLUME                     0x00001016L
#define BA0_AC97_PCM_OUT_VOLUME                 0x00001018L
#define BA0_AC97_RECORD_SELECT                  0x0000101AL
#define BA0_AC97_RECORD_GAIN                    0x0000101CL
#define BA0_AC97_RECORD_GAIN_MIC                0x0000101EL
#define BA0_AC97_GENERAL_PURPOSE                0x00001020L
#define BA0_AC97_3D_CONTROL                     0x00001022L
#define BA0_AC97_MODEM_RATE                     0x00001024L
#define BA0_AC97_POWERDOWN                      0x00001026L
#define BA0_AC97_RESERVED_28                    0x00001028L
#define BA0_AC97_RESERVED_2A                    0x0000102AL
#define BA0_AC97_RESERVED_2C                    0x0000102CL
#define BA0_AC97_RESERVED_2E                    0x0000102EL
#define BA0_AC97_RESERVED_30                    0x00001030L
#define BA0_AC97_RESERVED_32                    0x00001032L
#define BA0_AC97_RESERVED_34                    0x00001034L
#define BA0_AC97_RESERVED_36                    0x00001036L
#define BA0_AC97_RESERVED_38                    0x00001038L
#define BA0_AC97_RESERVED_3A                    0x0000103AL
#define BA0_AC97_RESERVED_3C                    0x0000103CL
#define BA0_AC97_RESERVED_3E                    0x0000103EL
#define BA0_AC97_RESERVED_40                    0x00001040L
#define BA0_AC97_RESERVED_42                    0x00001042L
#define BA0_AC97_RESERVED_44                    0x00001044L
#define BA0_AC97_RESERVED_46                    0x00001046L
#define BA0_AC97_RESERVED_48                    0x00001048L
#define BA0_AC97_RESERVED_4A                    0x0000104AL
#define BA0_AC97_RESERVED_4C                    0x0000104CL
#define BA0_AC97_RESERVED_4E                    0x0000104EL
#define BA0_AC97_RESERVED_50                    0x00001050L
#define BA0_AC97_RESERVED_52                    0x00001052L
#define BA0_AC97_RESERVED_54                    0x00001054L
#define BA0_AC97_RESERVED_56                    0x00001056L
#define BA0_AC97_RESERVED_58                    0x00001058L
#define BA0_AC97_VENDOR_RESERVED_5A             0x0000105AL
#define BA0_AC97_VENDOR_RESERVED_5C             0x0000105CL
#define BA0_AC97_VENDOR_RESERVED_5E             0x0000105EL
#define BA0_AC97_VENDOR_RESERVED_60             0x00001060L
#define BA0_AC97_VENDOR_RESERVED_62             0x00001062L
#define BA0_AC97_VENDOR_RESERVED_64             0x00001064L
#define BA0_AC97_VENDOR_RESERVED_66             0x00001066L
#define BA0_AC97_VENDOR_RESERVED_68             0x00001068L
#define BA0_AC97_VENDOR_RESERVED_6A             0x0000106AL
#define BA0_AC97_VENDOR_RESERVED_6C             0x0000106CL
#define BA0_AC97_VENDOR_RESERVED_6E             0x0000106EL
#define BA0_AC97_VENDOR_RESERVED_70             0x00001070L
#define BA0_AC97_VENDOR_RESERVED_72             0x00001072L
#define BA0_AC97_VENDOR_RESERVED_74             0x00001074L
#define BA0_AC97_VENDOR_RESERVED_76             0x00001076L
#define BA0_AC97_VENDOR_RESERVED_78             0x00001078L
#define BA0_AC97_VENDOR_RESERVED_7A             0x0000107AL
#define BA0_AC97_VENDOR_ID1                     0x0000107CL
#define BA0_AC97_VENDOR_ID2                     0x0000107EL

/*****************************************************************************
 *
 * The following define the offsets of the registers and memories accessed via
 * base address register one on the CS461x part.
 *
 *****************************************************************************/
#define BA1_SP_DMEM0                            0x00000000L
#define BA1_SP_DMEM1                            0x00010000L
#define BA1_SP_PMEM                             0x00020000L
#define BA1_SPCR                                0x00030000L
#define BA1_DREG                                0x00030004L
#define BA1_DSRWP                               0x00030008L
#define BA1_TWPR                                0x0003000CL
#define BA1_SPWR                                0x00030010L
#define BA1_SPIR                                0x00030014L
#define BA1_FGR1                                0x00030020L
#define BA1_SPCS                                0x00030028L
#define BA1_SDSR                                0x0003002CL
#define BA1_FRMT                                0x00030030L
#define BA1_FRCC                                0x00030034L
#define BA1_FRSC                                0x00030038L
#define BA1_OMNI_MEM                            0x000E0000L

/*****************************************************************************
 *
 * The following defines are for the flags in the PCI interrupt register.
 *
 *****************************************************************************/
#define PI_LINE_MASK                            0x000000FFL
#define PI_PIN_MASK                             0x0000FF00L
#define PI_MIN_GRANT_MASK                       0x00FF0000L
#define PI_MAX_LATENCY_MASK                     0xFF000000L
#define PI_LINE_SHIFT                           0L
#define PI_PIN_SHIFT                            8L
#define PI_MIN_GRANT_SHIFT                      16L
#define PI_MAX_LATENCY_SHIFT                    24L

/*****************************************************************************
 *
 * The following defines are for the flags in the host interrupt status
 * register.
 *
 *****************************************************************************/
#define HISR_VC_MASK                            0x0000FFFFL
#define HISR_VC0                                0x00000001L
#define HISR_VC1                                0x00000002L
#define HISR_VC2                                0x00000004L
#define HISR_VC3                                0x00000008L
#define HISR_VC4                                0x00000010L
#define HISR_VC5                                0x00000020L
#define HISR_VC6                                0x00000040L
#define HISR_VC7                                0x00000080L
#define HISR_VC8                                0x00000100L
#define HISR_VC9                                0x00000200L
#define HISR_VC10                               0x00000400L
#define HISR_VC11                               0x00000800L
#define HISR_VC12                               0x00001000L
#define HISR_VC13                               0x00002000L
#define HISR_VC14                               0x00004000L
#define HISR_VC15                               0x00008000L
#define HISR_INT0                               0x00010000L
#define HISR_INT1                               0x00020000L
#define HISR_DMAI                               0x00040000L
#define HISR_FROVR                              0x00080000L
#define HISR_MIDI                               0x00100000L
#ifdef NO_CS4612
#define HISR_RESERVED                           0x0FE00000L
#else
#define HISR_SBINT                              0x00200000L
#define HISR_RESERVED                           0x0FC00000L
#endif
#define HISR_H0P                                0x40000000L
#define HISR_INTENA                             0x80000000L

/*****************************************************************************
 *
 * The following defines are for the flags in the host signal register 0.
 *
 *****************************************************************************/
#define HSR0_VC_MASK                            0xFFFFFFFFL
#define HSR0_VC16                               0x00000001L
#define HSR0_VC17                               0x00000002L
#define HSR0_VC18                               0x00000004L
#define HSR0_VC19                               0x00000008L
#define HSR0_VC20                               0x00000010L
#define HSR0_VC21                               0x00000020L
#define HSR0_VC22                               0x00000040L
#define HSR0_VC23                               0x00000080L
#define HSR0_VC24                               0x00000100L
#define HSR0_VC25                               0x00000200L
#define HSR0_VC26                               0x00000400L
#define HSR0_VC27                               0x00000800L
#define HSR0_VC28                               0x00001000L
#define HSR0_VC29                               0x00002000L
#define HSR0_VC30                               0x00004000L
#define HSR0_VC31                               0x00008000L
#define HSR0_VC32                               0x00010000L
#define HSR0_VC33                               0x00020000L
#define HSR0_VC34                               0x00040000L
#define HSR0_VC35                               0x00080000L
#define HSR0_VC36                               0x00100000L
#define HSR0_VC37                               0x00200000L
#define HSR0_VC38                               0x00400000L
#define HSR0_VC39                               0x00800000L
#define HSR0_VC40                               0x01000000L
#define HSR0_VC41                               0x02000000L
#define HSR0_VC42                               0x04000000L
#define HSR0_VC43                               0x08000000L
#define HSR0_VC44                               0x10000000L
#define HSR0_VC45                               0x20000000L
#define HSR0_VC46                               0x40000000L
#define HSR0_VC47                               0x80000000L

/*****************************************************************************
 *
 * The following defines are for the flags in the host interrupt control
 * register.
 *
 *****************************************************************************/
#define HICR_IEV                                0x00000001L
#define HICR_CHGM                               0x00000002L

/*****************************************************************************
 *
 * The following defines are for the flags in the DMA status register.
 *
 *****************************************************************************/
#define DMSR_HP                                 0x00000001L
#define DMSR_HR                                 0x00000002L
#define DMSR_SP                                 0x00000004L
#define DMSR_SR                                 0x00000008L

/*****************************************************************************
 *
 * The following defines are for the flags in the host DMA source address
 * register.
 *
 *****************************************************************************/
#define HSAR_HOST_ADDR_MASK                     0xFFFFFFFFL
#define HSAR_DSP_ADDR_MASK                      0x0000FFFFL
#define HSAR_MEMID_MASK                         0x000F0000L
#define HSAR_MEMID_SP_DMEM0                     0x00000000L
#define HSAR_MEMID_SP_DMEM1                     0x00010000L
#define HSAR_MEMID_SP_PMEM                      0x00020000L
#define HSAR_MEMID_SP_DEBUG                     0x00030000L
#define HSAR_MEMID_OMNI_MEM                     0x000E0000L
#define HSAR_END                                0x40000000L
#define HSAR_ERR                                0x80000000L

/*****************************************************************************
 *
 * The following defines are for the flags in the host DMA destination address
 * register.
 *
 *****************************************************************************/
#define HDAR_HOST_ADDR_MASK                     0xFFFFFFFFL
#define HDAR_DSP_ADDR_MASK                      0x0000FFFFL
#define HDAR_MEMID_MASK                         0x000F0000L
#define HDAR_MEMID_SP_DMEM0                     0x00000000L
#define HDAR_MEMID_SP_DMEM1                     0x00010000L
#define HDAR_MEMID_SP_PMEM                      0x00020000L
#define HDAR_MEMID_SP_DEBUG                     0x00030000L
#define HDAR_MEMID_OMNI_MEM                     0x000E0000L
#define HDAR_END                                0x40000000L
#define HDAR_ERR                                0x80000000L

/*****************************************************************************
 *
 * The following defines are for the flags in the host DMA control register.
 *
 *****************************************************************************/
#define HDMR_AC_MASK                            0x0000F000L
#define HDMR_AC_8_16                            0x00001000L
#define HDMR_AC_M_S                             0x00002000L
#define HDMR_AC_B_L                             0x00004000L
#define HDMR_AC_S_U                             0x00008000L

/*****************************************************************************
 *
 * The following defines are for the flags in the host DMA control register.
 *
 *****************************************************************************/
#define HDCR_COUNT_MASK                         0x000003FFL
#define HDCR_DONE                               0x00004000L
#define HDCR_OPT                                0x00008000L
#define HDCR_WBD                                0x00400000L
#define HDCR_WBS                                0x00800000L
#define HDCR_DMS_MASK                           0x07000000L
#define HDCR_DMS_LINEAR                         0x00000000L
#define HDCR_DMS_16_DWORDS                      0x01000000L
#define HDCR_DMS_32_DWORDS                      0x02000000L
#define HDCR_DMS_64_DWORDS                      0x03000000L
#define HDCR_DMS_128_DWORDS                     0x04000000L
#define HDCR_DMS_256_DWORDS                     0x05000000L
#define HDCR_DMS_512_DWORDS                     0x06000000L
#define HDCR_DMS_1024_DWORDS                    0x07000000L
#define HDCR_DH                                 0x08000000L
#define HDCR_SMS_MASK                           0x70000000L
#define HDCR_SMS_LINEAR                         0x00000000L
#define HDCR_SMS_16_DWORDS                      0x10000000L
#define HDCR_SMS_32_DWORDS                      0x20000000L
#define HDCR_SMS_64_DWORDS                      0x30000000L
#define HDCR_SMS_128_DWORDS                     0x40000000L
#define HDCR_SMS_256_DWORDS                     0x50000000L
#define HDCR_SMS_512_DWORDS                     0x60000000L
#define HDCR_SMS_1024_DWORDS                    0x70000000L
#define HDCR_SH                                 0x80000000L
#define HDCR_COUNT_SHIFT                        0L

/*****************************************************************************
 *
 * The following defines are for the flags in the performance monitor control
 * register.
 *
 *****************************************************************************/
#define PFMC_C1SS_MASK                          0x0000001FL
#define PFMC_C1EV                               0x00000020L
#define PFMC_C1RS                               0x00008000L
#define PFMC_C2SS_MASK                          0x001F0000L
#define PFMC_C2EV                               0x00200000L
#define PFMC_C2RS                               0x80000000L
#define PFMC_C1SS_SHIFT                         0L
#define PFMC_C2SS_SHIFT                         16L
#define PFMC_BUS_GRANT                          0L
#define PFMC_GRANT_AFTER_REQ                    1L
#define PFMC_TRANSACTION                        2L
#define PFMC_DWORD_TRANSFER                     3L
#define PFMC_SLAVE_READ                         4L
#define PFMC_SLAVE_WRITE                        5L
#define PFMC_PREEMPTION                         6L
#define PFMC_DISCONNECT_RETRY                   7L
#define PFMC_INTERRUPT                          8L
#define PFMC_BUS_OWNERSHIP                      9L
#define PFMC_TRANSACTION_LAG                    10L
#define PFMC_PCI_CLOCK                          11L
#define PFMC_SERIAL_CLOCK                       12L
#define PFMC_SP_CLOCK                           13L

/*****************************************************************************
 *
 * The following defines are for the flags in the performance counter value 1
 * register.
 *
 *****************************************************************************/
#define PFCV1_PC1V_MASK                         0xFFFFFFFFL
#define PFCV1_PC1V_SHIFT                        0L

/*****************************************************************************
 *
 * The following defines are for the flags in the performance counter value 2
 * register.
 *
 *****************************************************************************/
#define PFCV2_PC2V_MASK                         0xFFFFFFFFL
#define PFCV2_PC2V_SHIFT                        0L

/*****************************************************************************
 *
 * The following defines are for the flags in the clock control register 1.
 *
 *****************************************************************************/
#define CLKCR1_OSCS                             0x00000001L
#define CLKCR1_OSCP                             0x00000002L
#define CLKCR1_PLLSS_MASK                       0x0000000CL
#define CLKCR1_PLLSS_SERIAL                     0x00000000L
#define CLKCR1_PLLSS_CRYSTAL                    0x00000004L
#define CLKCR1_PLLSS_PCI                        0x00000008L
#define CLKCR1_PLLSS_RESERVED                   0x0000000CL
#define CLKCR1_PLLP                             0x00000010L
#define CLKCR1_SWCE                             0x00000020L
#define CLKCR1_PLLOS                            0x00000040L

/*****************************************************************************
 *
 * The following defines are for the flags in the clock control register 2.
 *
 *****************************************************************************/
#define CLKCR2_PDIVS_MASK                       0x0000000FL
#define CLKCR2_PDIVS_1                          0x00000001L
#define CLKCR2_PDIVS_2                          0x00000002L
#define CLKCR2_PDIVS_4                          0x00000004L
#define CLKCR2_PDIVS_7                          0x00000007L
#define CLKCR2_PDIVS_8                          0x00000008L
#define CLKCR2_PDIVS_16                         0x00000000L

/*****************************************************************************
 *
 * The following defines are for the flags in the PLL multiplier register.
 *
 *****************************************************************************/
#define PLLM_MASK                               0x000000FFL
#define PLLM_SHIFT                              0L

/*****************************************************************************
 *
 * The following defines are for the flags in the PLL capacitor coefficient
 * register.
 *
 *****************************************************************************/
#define PLLCC_CDR_MASK                          0x00000007L
#ifndef NO_CS4610
#define PLLCC_CDR_240_350_MHZ                   0x00000000L
#define PLLCC_CDR_184_265_MHZ                   0x00000001L
#define PLLCC_CDR_144_205_MHZ                   0x00000002L
#define PLLCC_CDR_111_160_MHZ                   0x00000003L
#define PLLCC_CDR_87_123_MHZ                    0x00000004L
#define PLLCC_CDR_67_96_MHZ                     0x00000005L
#define PLLCC_CDR_52_74_MHZ                     0x00000006L
#define PLLCC_CDR_45_58_MHZ                     0x00000007L
#endif
#ifndef NO_CS4612
#define PLLCC_CDR_271_398_MHZ                   0x00000000L
#define PLLCC_CDR_227_330_MHZ                   0x00000001L
#define PLLCC_CDR_167_239_MHZ                   0x00000002L
#define PLLCC_CDR_150_215_MHZ                   0x00000003L
#define PLLCC_CDR_107_154_MHZ                   0x00000004L
#define PLLCC_CDR_98_140_MHZ                    0x00000005L
#define PLLCC_CDR_73_104_MHZ                    0x00000006L
#define PLLCC_CDR_63_90_MHZ                     0x00000007L
#endif
#define PLLCC_LPF_MASK                          0x000000F8L
#ifndef NO_CS4610
#define PLLCC_LPF_23850_60000_KHZ               0x00000000L
#define PLLCC_LPF_7960_26290_KHZ                0x00000008L
#define PLLCC_LPF_4160_10980_KHZ                0x00000018L
#define PLLCC_LPF_1740_4580_KHZ                 0x00000038L
#define PLLCC_LPF_724_1910_KHZ                  0x00000078L
#define PLLCC_LPF_317_798_KHZ                   0x000000F8L
#endif
#ifndef NO_CS4612
#define PLLCC_LPF_25580_64530_KHZ               0x00000000L
#define PLLCC_LPF_14360_37270_KHZ               0x00000008L
#define PLLCC_LPF_6100_16020_KHZ                0x00000018L
#define PLLCC_LPF_2540_6690_KHZ                 0x00000038L
#define PLLCC_LPF_1050_2780_KHZ                 0x00000078L
#define PLLCC_LPF_450_1160_KHZ                  0x000000F8L
#endif

/*****************************************************************************
 *
 * The following defines are for the flags in the feature reporting register.
 *
 *****************************************************************************/
#define FRR_FAB_MASK                            0x00000003L
#define FRR_MASK_MASK                           0x0000001CL
#ifdef NO_CS4612
#define FRR_CFOP_MASK                           0x000000E0L
#else
#define FRR_CFOP_MASK                           0x00000FE0L
#endif
#define FRR_CFOP_NOT_DVD                        0x00000020L
#define FRR_CFOP_A3D                            0x00000040L
#define FRR_CFOP_128_PIN                        0x00000080L
#ifndef NO_CS4612
#define FRR_CFOP_CS4280                         0x00000800L
#endif
#define FRR_FAB_SHIFT                           0L
#define FRR_MASK_SHIFT                          2L
#define FRR_CFOP_SHIFT                          5L

/*****************************************************************************
 *
 * The following defines are for the flags in the configuration load 1
 * register.
 *
 *****************************************************************************/
#define CFL1_CLOCK_SOURCE_MASK                  0x00000003L
#define CFL1_CLOCK_SOURCE_CS423X                0x00000000L
#define CFL1_CLOCK_SOURCE_AC97                  0x00000001L
#define CFL1_CLOCK_SOURCE_CRYSTAL               0x00000002L
#define CFL1_CLOCK_SOURCE_DUAL_AC97             0x00000003L
#define CFL1_VALID_DATA_MASK                    0x000000FFL

/*****************************************************************************
 *
 * The following defines are for the flags in the configuration load 2
 * register.
 *
 *****************************************************************************/
#define CFL2_VALID_DATA_MASK                    0x000000FFL

/*****************************************************************************
 *
 * The following defines are for the flags in the serial port master control
 * register 1.
 *
 *****************************************************************************/
#define SERMC1_MSPE                             0x00000001L
#define SERMC1_PTC_MASK                         0x0000000EL
#define SERMC1_PTC_CS423X                       0x00000000L
#define SERMC1_PTC_AC97                         0x00000002L
#define SERMC1_PTC_DAC                          0x00000004L
#define SERMC1_PLB                              0x00000010L
#define SERMC1_XLB                              0x00000020L

/*****************************************************************************
 *
 * The following defines are for the flags in the serial port master control
 * register 2.
 *
 *****************************************************************************/
#define SERMC2_LROE                             0x00000001L
#define SERMC2_MCOE                             0x00000002L
#define SERMC2_MCDIV                            0x00000004L

/*****************************************************************************
 *
 * The following defines are for the flags in the serial port 1 configuration
 * register.
 *
 *****************************************************************************/
#define SERC1_SO1EN                             0x00000001L
#define SERC1_SO1F_MASK                         0x0000000EL
#define SERC1_SO1F_CS423X                       0x00000000L
#define SERC1_SO1F_AC97                         0x00000002L
#define SERC1_SO1F_DAC                          0x00000004L
#define SERC1_SO1F_SPDIF                        0x00000006L

/*****************************************************************************
 *
 * The following defines are for the flags in the serial port 2 configuration
 * register.
 *
 *****************************************************************************/
#define SERC2_SI1EN                             0x00000001L
#define SERC2_SI1F_MASK                         0x0000000EL
#define SERC2_SI1F_CS423X                       0x00000000L
#define SERC2_SI1F_AC97                         0x00000002L
#define SERC2_SI1F_ADC                          0x00000004L
#define SERC2_SI1F_SPDIF                        0x00000006L

/*****************************************************************************
 *
 * The following defines are for the flags in the serial port 3 configuration
 * register.
 *
 *****************************************************************************/
#define SERC3_SO2EN                             0x00000001L
#define SERC3_SO2F_MASK                         0x00000006L
#define SERC3_SO2F_DAC                          0x00000000L
#define SERC3_SO2F_SPDIF                        0x00000002L

/*****************************************************************************
 *
 * The following defines are for the flags in the serial port 4 configuration
 * register.
 *
 *****************************************************************************/
#define SERC4_SO3EN                             0x00000001L
#define SERC4_SO3F_MASK                         0x00000006L
#define SERC4_SO3F_DAC                          0x00000000L
#define SERC4_SO3F_SPDIF                        0x00000002L

/*****************************************************************************
 *
 * The following defines are for the flags in the serial port 5 configuration
 * register.
 *
 *****************************************************************************/
#define SERC5_SI2EN                             0x00000001L
#define SERC5_SI2F_MASK                         0x00000006L
#define SERC5_SI2F_ADC                          0x00000000L
#define SERC5_SI2F_SPDIF                        0x00000002L

/*****************************************************************************
 *
 * The following defines are for the flags in the serial port backdoor sample
 * pointer register.
 *
 *****************************************************************************/
#define SERBSP_FSP_MASK                         0x0000000FL
#define SERBSP_FSP_SHIFT                        0L

/*****************************************************************************
 *
 * The following defines are for the flags in the serial port backdoor status
 * register.
 *
 *****************************************************************************/
#define SERBST_RRDY                             0x00000001L
#define SERBST_WBSY                             0x00000002L

/*****************************************************************************
 *
 * The following defines are for the flags in the serial port backdoor command
 * register.
 *
 *****************************************************************************/
#define SERBCM_RDC                              0x00000001L
#define SERBCM_WRC                              0x00000002L

/*****************************************************************************
 *
 * The following defines are for the flags in the serial port backdoor address
 * register.
 *
 *****************************************************************************/
#ifdef NO_CS4612
#define SERBAD_FAD_MASK                         0x000000FFL
#else
#define SERBAD_FAD_MASK                         0x000001FFL
#endif
#define SERBAD_FAD_SHIFT                        0L

/*****************************************************************************
 *
 * The following defines are for the flags in the serial port backdoor
 * configuration register.
 *
 *****************************************************************************/
#define SERBCF_HBP                              0x00000001L

/*****************************************************************************
 *
 * The following defines are for the flags in the serial port backdoor write
 * port register.
 *
 *****************************************************************************/
#define SERBWP_FWD_MASK                         0x000FFFFFL
#define SERBWP_FWD_SHIFT                        0L

/*****************************************************************************
 *
 * The following defines are for the flags in the serial port backdoor read
 * port register.
 *
 *****************************************************************************/
#define SERBRP_FRD_MASK                         0x000FFFFFL
#define SERBRP_FRD_SHIFT                        0L

/*****************************************************************************
 *
 * The following defines are for the flags in the async FIFO address register.
 *
 *****************************************************************************/
#ifndef NO_CS4612
#define ASER_FADDR_A1_MASK                      0x000001FFL
#define ASER_FADDR_EN1                          0x00008000L
#define ASER_FADDR_A2_MASK                      0x01FF0000L
#define ASER_FADDR_EN2                          0x80000000L
#define ASER_FADDR_A1_SHIFT                     0L
#define ASER_FADDR_A2_SHIFT                     16L
#endif

/*****************************************************************************
 *
 * The following defines are for the flags in the AC97 control register.
 *
 *****************************************************************************/
#define ACCTL_RSTN                              0x00000001L
#define ACCTL_ESYN                              0x00000002L
#define ACCTL_VFRM                              0x00000004L
#define ACCTL_DCV                               0x00000008L
#define ACCTL_CRW                               0x00000010L
#define ACCTL_ASYN                              0x00000020L
#ifndef NO_CS4612
#define ACCTL_TC                                0x00000040L
#endif

/*****************************************************************************
 *
 * The following defines are for the flags in the AC97 status register.
 *
 *****************************************************************************/
#define ACSTS_CRDY                              0x00000001L
#define ACSTS_VSTS                              0x00000002L
#ifndef NO_CS4612
#define ACSTS_WKUP                              0x00000004L
#endif

/*****************************************************************************
 *
 * The following defines are for the flags in the AC97 output slot valid
 * register.
 *
 *****************************************************************************/
#define ACOSV_SLV3                              0x00000001L
#define ACOSV_SLV4                              0x00000002L
#define ACOSV_SLV5                              0x00000004L
#define ACOSV_SLV6                              0x00000008L
#define ACOSV_SLV7                              0x00000010L
#define ACOSV_SLV8                              0x00000020L
#define ACOSV_SLV9                              0x00000040L
#define ACOSV_SLV10                             0x00000080L
#define ACOSV_SLV11                             0x00000100L
#define ACOSV_SLV12                             0x00000200L

/*****************************************************************************
 *
 * The following defines are for the flags in the AC97 command address
 * register.
 *
 *****************************************************************************/
#define ACCAD_CI_MASK                           0x0000007FL
#define ACCAD_CI_SHIFT                          0L

/*****************************************************************************
 *
 * The following defines are for the flags in the AC97 command data register.
 *
 *****************************************************************************/
#define ACCDA_CD_MASK                           0x0000FFFFL
#define ACCDA_CD_SHIFT                          0L

/*****************************************************************************
 *
 * The following defines are for the flags in the AC97 input slot valid
 * register.
 *
 *****************************************************************************/
#define ACISV_ISV3                              0x00000001L
#define ACISV_ISV4                              0x00000002L
#define ACISV_ISV5                              0x00000004L
#define ACISV_ISV6                              0x00000008L
#define ACISV_ISV7                              0x00000010L
#define ACISV_ISV8                              0x00000020L
#define ACISV_ISV9                              0x00000040L
#define ACISV_ISV10                             0x00000080L
#define ACISV_ISV11                             0x00000100L
#define ACISV_ISV12                             0x00000200L

/*****************************************************************************
 *
 * The following defines are for the flags in the AC97 status address
 * register.
 *
 *****************************************************************************/
#define ACSAD_SI_MASK                           0x0000007FL
#define ACSAD_SI_SHIFT                          0L

/*****************************************************************************
 *
 * The following defines are for the flags in the AC97 status data register.
 *
 *****************************************************************************/
#define ACSDA_SD_MASK                           0x0000FFFFL
#define ACSDA_SD_SHIFT                          0L

/*****************************************************************************
 *
 * The following defines are for the flags in the joystick poll/trigger
 * register.
 *
 *****************************************************************************/
#define JSPT_CAX                                0x00000001L
#define JSPT_CAY                                0x00000002L
#define JSPT_CBX                                0x00000004L
#define JSPT_CBY                                0x00000008L
#define JSPT_BA1                                0x00000010L
#define JSPT_BA2                                0x00000020L
#define JSPT_BB1                                0x00000040L
#define JSPT_BB2                                0x00000080L

/*****************************************************************************
 *
 * The following defines are for the flags in the joystick control register.
 *
 *****************************************************************************/
#define JSCTL_SP_MASK                           0x00000003L
#define JSCTL_SP_SLOW                           0x00000000L
#define JSCTL_SP_MEDIUM_SLOW                    0x00000001L
#define JSCTL_SP_MEDIUM_FAST                    0x00000002L
#define JSCTL_SP_FAST                           0x00000003L
#define JSCTL_ARE                               0x00000004L

/*****************************************************************************
 *
 * The following defines are for the flags in the joystick coordinate pair 1
 * readback register.
 *
 *****************************************************************************/
#define JSC1_Y1V_MASK                           0x0000FFFFL
#define JSC1_X1V_MASK                           0xFFFF0000L
#define JSC1_Y1V_SHIFT                          0L
#define JSC1_X1V_SHIFT                          16L

/*****************************************************************************
 *
 * The following defines are for the flags in the joystick coordinate pair 2
 * readback register.
 *
 *****************************************************************************/
#define JSC2_Y2V_MASK                           0x0000FFFFL
#define JSC2_X2V_MASK                           0xFFFF0000L
#define JSC2_Y2V_SHIFT                          0L
#define JSC2_X2V_SHIFT                          16L

/*****************************************************************************
 *
 * The following defines are for the flags in the MIDI control register.
 *
 *****************************************************************************/
#define MIDCR_TXE                               0x00000001L
#define MIDCR_RXE                               0x00000002L
#define MIDCR_RIE                               0x00000004L
#define MIDCR_TIE                               0x00000008L
#define MIDCR_MLB                               0x00000010L
#define MIDCR_MRST                              0x00000020L

/*****************************************************************************
 *
 * The following defines are for the flags in the MIDI status register.
 *
 *****************************************************************************/
#define MIDSR_TBF                               0x00000001L
#define MIDSR_RBE                               0x00000002L

/*****************************************************************************
 *
 * The following defines are for the flags in the MIDI write port register.
 *
 *****************************************************************************/
#define MIDWP_MWD_MASK                          0x000000FFL
#define MIDWP_MWD_SHIFT                         0L

/*****************************************************************************
 *
 * The following defines are for the flags in the MIDI read port register.
 *
 *****************************************************************************/
#define MIDRP_MRD_MASK                          0x000000FFL
#define MIDRP_MRD_SHIFT                         0L

/*****************************************************************************
 *
 * The following defines are for the flags in the joystick GPIO register.
 *
 *****************************************************************************/
#define JSIO_DAX                                0x00000001L
#define JSIO_DAY                                0x00000002L
#define JSIO_DBX                                0x00000004L
#define JSIO_DBY                                0x00000008L
#define JSIO_AXOE                               0x00000010L
#define JSIO_AYOE                               0x00000020L
#define JSIO_BXOE                               0x00000040L
#define JSIO_BYOE                               0x00000080L

/*****************************************************************************
 *
 * The following defines are for the flags in the master async/sync serial
 * port enable register.
 *
 *****************************************************************************/
#ifndef NO_CS4612
#define ASER_MASTER_ME                          0x00000001L
#endif

/*****************************************************************************
 *
 * The following defines are for the flags in the configuration interface
 * register.
 *
 *****************************************************************************/
#define CFGI_CLK                                0x00000001L
#define CFGI_DOUT                               0x00000002L
#define CFGI_DIN_EEN                            0x00000004L
#define CFGI_EELD                               0x00000008L

/*****************************************************************************
 *
 * The following defines are for the flags in the subsystem ID and vendor ID
 * register.
 *
 *****************************************************************************/
#define SSVID_VID_MASK                          0x0000FFFFL
#define SSVID_SID_MASK                          0xFFFF0000L
#define SSVID_VID_SHIFT                         0L
#define SSVID_SID_SHIFT                         16L

/*****************************************************************************
 *
 * The following defines are for the flags in the GPIO pin interface register.
 *
 *****************************************************************************/
#define GPIOR_VOLDN                             0x00000001L
#define GPIOR_VOLUP                             0x00000002L
#define GPIOR_SI2D                              0x00000004L
#define GPIOR_SI2OE                             0x00000008L

/*****************************************************************************
 *
 * The following defines are for the flags in the extended GPIO pin direction
 * register.
 *
 *****************************************************************************/
#ifndef NO_CS4612
#define EGPIODR_GPOE0                           0x00000001L
#define EGPIODR_GPOE1                           0x00000002L
#define EGPIODR_GPOE2                           0x00000004L
#define EGPIODR_GPOE3                           0x00000008L
#define EGPIODR_GPOE4                           0x00000010L
#define EGPIODR_GPOE5                           0x00000020L
#define EGPIODR_GPOE6                           0x00000040L
#define EGPIODR_GPOE7                           0x00000080L
#define EGPIODR_GPOE8                           0x00000100L
#endif

/*****************************************************************************
 *
 * The following defines are for the flags in the extended GPIO pin polarity/
 * type register.
 *
 *****************************************************************************/
#ifndef NO_CS4612
#define EGPIOPTR_GPPT0                          0x00000001L
#define EGPIOPTR_GPPT1                          0x00000002L
#define EGPIOPTR_GPPT2                          0x00000004L
#define EGPIOPTR_GPPT3                          0x00000008L
#define EGPIOPTR_GPPT4                          0x00000010L
#define EGPIOPTR_GPPT5                          0x00000020L
#define EGPIOPTR_GPPT6                          0x00000040L
#define EGPIOPTR_GPPT7                          0x00000080L
#define EGPIOPTR_GPPT8                          0x00000100L
#endif

/*****************************************************************************
 *
 * The following defines are for the flags in the extended GPIO pin sticky
 * register.
 *
 *****************************************************************************/
#ifndef NO_CS4612
#define EGPIOTR_GPS0                            0x00000001L
#define EGPIOTR_GPS1                            0x00000002L
#define EGPIOTR_GPS2                            0x00000004L
#define EGPIOTR_GPS3                            0x00000008L
#define EGPIOTR_GPS4                            0x00000010L
#define EGPIOTR_GPS5                            0x00000020L
#define EGPIOTR_GPS6                            0x00000040L
#define EGPIOTR_GPS7                            0x00000080L
#define EGPIOTR_GPS8                            0x00000100L
#endif

/*****************************************************************************
 *
 * The following defines are for the flags in the extended GPIO ping wakeup
 * register.
 *
 *****************************************************************************/
#ifndef NO_CS4612
#define EGPIOWR_GPW0                            0x00000001L
#define EGPIOWR_GPW1                            0x00000002L
#define EGPIOWR_GPW2                            0x00000004L
#define EGPIOWR_GPW3                            0x00000008L
#define EGPIOWR_GPW4                            0x00000010L
#define EGPIOWR_GPW5                            0x00000020L
#define EGPIOWR_GPW6                            0x00000040L
#define EGPIOWR_GPW7                            0x00000080L
#define EGPIOWR_GPW8                            0x00000100L
#endif

/*****************************************************************************
 *
 * The following defines are for the flags in the extended GPIO pin status
 * register.
 *
 *****************************************************************************/
#ifndef NO_CS4612
#define EGPIOSR_GPS0                            0x00000001L
#define EGPIOSR_GPS1                            0x00000002L
#define EGPIOSR_GPS2                            0x00000004L
#define EGPIOSR_GPS3                            0x00000008L
#define EGPIOSR_GPS4                            0x00000010L
#define EGPIOSR_GPS5                            0x00000020L
#define EGPIOSR_GPS6                            0x00000040L
#define EGPIOSR_GPS7                            0x00000080L
#define EGPIOSR_GPS8                            0x00000100L
#endif

/*****************************************************************************
 *
 * The following defines are for the flags in the serial port 6 configuration
 * register.
 *
 *****************************************************************************/
#ifndef NO_CS4612
#define SERC6_ASDO2EN                           0x00000001L
#endif

/*****************************************************************************
 *
 * The following defines are for the flags in the serial port 7 configuration
 * register.
 *
 *****************************************************************************/
#ifndef NO_CS4612
#define SERC7_ASDI2EN                           0x00000001L
#define SERC7_POSILB                            0x00000002L
#define SERC7_SIPOLB                            0x00000004L
#define SERC7_SOSILB                            0x00000008L
#define SERC7_SISOLB                            0x00000010L
#endif

/*****************************************************************************
 *
 * The following defines are for the flags in the serial port AC link
 * configuration register.
 *
 *****************************************************************************/
#ifndef NO_CS4612
#define SERACC_CODEC_TYPE_MASK                  0x00000001L
#define SERACC_CODEC_TYPE_1_03                  0x00000000L
#define SERACC_CODEC_TYPE_2_0                   0x00000001L
#define SERACC_TWO_CODECS                       0x00000002L
#define SERACC_MDM                              0x00000004L
#define SERACC_HSP                              0x00000008L
#endif

/*****************************************************************************
 *
 * The following defines are for the flags in the AC97 control register 2.
 *
 *****************************************************************************/
#ifndef NO_CS4612
#define ACCTL2_RSTN                             0x00000001L
#define ACCTL2_ESYN                             0x00000002L
#define ACCTL2_VFRM                             0x00000004L
#define ACCTL2_DCV                              0x00000008L
#define ACCTL2_CRW                              0x00000010L
#define ACCTL2_ASYN                             0x00000020L
#endif

/*****************************************************************************
 *
 * The following defines are for the flags in the AC97 status register 2.
 *
 *****************************************************************************/
#ifndef NO_CS4612
#define ACSTS2_CRDY                             0x00000001L
#define ACSTS2_VSTS                             0x00000002L
#endif

/*****************************************************************************
 *
 * The following defines are for the flags in the AC97 output slot valid
 * register 2.
 *
 *****************************************************************************/
#ifndef NO_CS4612
#define ACOSV2_SLV3                             0x00000001L
#define ACOSV2_SLV4                             0x00000002L
#define ACOSV2_SLV5                             0x00000004L
#define ACOSV2_SLV6                             0x00000008L
#define ACOSV2_SLV7                             0x00000010L
#define ACOSV2_SLV8                             0x00000020L
#define ACOSV2_SLV9                             0x00000040L
#define ACOSV2_SLV10                            0x00000080L
#define ACOSV2_SLV11                            0x00000100L
#define ACOSV2_SLV12                            0x00000200L
#endif

/*****************************************************************************
 *
 * The following defines are for the flags in the AC97 command address
 * register 2.
 *
 *****************************************************************************/
#ifndef NO_CS4612
#define ACCAD2_CI_MASK                          0x0000007FL
#define ACCAD2_CI_SHIFT                         0L
#endif

/*****************************************************************************
 *
 * The following defines are for the flags in the AC97 command data register
 * 2.
 *
 *****************************************************************************/
#ifndef NO_CS4612
#define ACCDA2_CD_MASK                          0x0000FFFFL
#define ACCDA2_CD_SHIFT                         0L
#endif

/*****************************************************************************
 *
 * The following defines are for the flags in the AC97 input slot valid
 * register 2.
 *
 *****************************************************************************/
#ifndef NO_CS4612
#define ACISV2_ISV3                             0x00000001L
#define ACISV2_ISV4                             0x00000002L
#define ACISV2_ISV5                             0x00000004L
#define ACISV2_ISV6                             0x00000008L
#define ACISV2_ISV7                             0x00000010L
#define ACISV2_ISV8                             0x00000020L
#define ACISV2_ISV9                             0x00000040L
#define ACISV2_ISV10                            0x00000080L
#define ACISV2_ISV11                            0x00000100L
#define ACISV2_ISV12                            0x00000200L
#endif

/*****************************************************************************
 *
 * The following defines are for the flags in the AC97 status address
 * register 2.
 *
 *****************************************************************************/
#ifndef NO_CS4612
#define ACSAD2_SI_MASK                          0x0000007FL
#define ACSAD2_SI_SHIFT                         0L
#endif

/*****************************************************************************
 *
 * The following defines are for the flags in the AC97 status data register 2.
 *
 *****************************************************************************/
#ifndef NO_CS4612
#define ACSDA2_SD_MASK                          0x0000FFFFL
#define ACSDA2_SD_SHIFT                         0L
#endif

/*****************************************************************************
 *
 * The following defines are for the flags in the I/O trap address and control
 * registers (all 12).
 *
 *****************************************************************************/
#ifndef NO_CS4612
#define IOTAC_SA_MASK                           0x0000FFFFL
#define IOTAC_MSK_MASK                          0x000F0000L
#define IOTAC_IODC_MASK                         0x06000000L
#define IOTAC_IODC_16_BIT                       0x00000000L
#define IOTAC_IODC_10_BIT                       0x02000000L
#define IOTAC_IODC_12_BIT                       0x04000000L
#define IOTAC_WSPI                              0x08000000L
#define IOTAC_RSPI                              0x10000000L
#define IOTAC_WSE                               0x20000000L
#define IOTAC_WE                                0x40000000L
#define IOTAC_RE                                0x80000000L
#define IOTAC_SA_SHIFT                          0L
#define IOTAC_MSK_SHIFT                         16L
#endif

/*****************************************************************************
 *
 * The following defines are for the flags in the I/O trap fast read registers
 * (all 8).
 *
 *****************************************************************************/
#ifndef NO_CS4612
#define IOTFR_D_MASK                            0x0000FFFFL
#define IOTFR_A_MASK                            0x000F0000L
#define IOTFR_R_MASK                            0x0F000000L
#define IOTFR_ALL                               0x40000000L
#define IOTFR_VL                                0x80000000L
#define IOTFR_D_SHIFT                           0L
#define IOTFR_A_SHIFT                           16L
#define IOTFR_R_SHIFT                           24L
#endif

/*****************************************************************************
 *
 * The following defines are for the flags in the I/O trap FIFO register.
 *
 *****************************************************************************/
#ifndef NO_CS4612
#define IOTFIFO_BA_MASK                         0x00003FFFL
#define IOTFIFO_S_MASK                          0x00FF0000L
#define IOTFIFO_OF                              0x40000000L
#define IOTFIFO_SPIOF                           0x80000000L
#define IOTFIFO_BA_SHIFT                        0L
#define IOTFIFO_S_SHIFT                         16L
#endif

/*****************************************************************************
 *
 * The following defines are for the flags in the I/O trap retry read data
 * register.
 *
 *****************************************************************************/
#ifndef NO_CS4612
#define IOTRRD_D_MASK                           0x0000FFFFL
#define IOTRRD_RDV                              0x80000000L
#define IOTRRD_D_SHIFT                          0L
#endif

/*****************************************************************************
 *
 * The following defines are for the flags in the I/O trap FIFO pointer
 * register.
 *
 *****************************************************************************/
#ifndef NO_CS4612
#define IOTFP_CA_MASK                           0x00003FFFL
#define IOTFP_PA_MASK                           0x3FFF0000L
#define IOTFP_CA_SHIFT                          0L
#define IOTFP_PA_SHIFT                          16L
#endif

/*****************************************************************************
 *
 * The following defines are for the flags in the I/O trap control register.
 *
 *****************************************************************************/
#ifndef NO_CS4612
#define IOTCR_ITD                               0x00000001L
#define IOTCR_HRV                               0x00000002L
#define IOTCR_SRV                               0x00000004L
#define IOTCR_DTI                               0x00000008L
#define IOTCR_DFI                               0x00000010L
#define IOTCR_DDP                               0x00000020L
#define IOTCR_JTE                               0x00000040L
#define IOTCR_PPE                               0x00000080L
#endif

/*****************************************************************************
 *
 * The following defines are for the flags in the direct PCI data register.
 *
 *****************************************************************************/
#ifndef NO_CS4612
#define DPCID_D_MASK                            0xFFFFFFFFL
#define DPCID_D_SHIFT                           0L
#endif

/*****************************************************************************
 *
 * The following defines are for the flags in the direct PCI address register.
 *
 *****************************************************************************/
#ifndef NO_CS4612
#define DPCIA_A_MASK                            0xFFFFFFFFL
#define DPCIA_A_SHIFT                           0L
#endif

/*****************************************************************************
 *
 * The following defines are for the flags in the direct PCI command register.
 *
 *****************************************************************************/
#ifndef NO_CS4612
#define DPCIC_C_MASK                            0x0000000FL
#define DPCIC_C_IOREAD                          0x00000002L
#define DPCIC_C_IOWRITE                         0x00000003L
#define DPCIC_BE_MASK                           0x000000F0L
#endif

/*****************************************************************************
 *
 * The following defines are for the flags in the PC/PCI request register.
 *
 *****************************************************************************/
#ifndef NO_CS4612
#define PCPCIR_RDC_MASK                         0x00000007L
#define PCPCIR_C_MASK                           0x00007000L
#define PCPCIR_REQ                              0x00008000L
#define PCPCIR_RDC_SHIFT                        0L
#define PCPCIR_C_SHIFT                          12L
#endif

/*****************************************************************************
 *
 * The following defines are for the flags in the PC/PCI grant register.
 *
 *****************************************************************************/
#ifndef NO_CS4612
#define PCPCIG_GDC_MASK                         0x00000007L
#define PCPCIG_VL                               0x00008000L
#define PCPCIG_GDC_SHIFT                        0L
#endif

/*****************************************************************************
 *
 * The following defines are for the flags in the PC/PCI master enable
 * register.
 *
 *****************************************************************************/
#ifndef NO_CS4612
#define PCPCIEN_EN                              0x00000001L
#endif

/*****************************************************************************
 *
 * The following defines are for the flags in the extended PCI power
 * management control register.
 *
 *****************************************************************************/
#ifndef NO_CS4612
#define EPCIPMC_GWU                             0x00000001L
#define EPCIPMC_FSPC                            0x00000002L
#endif

/*****************************************************************************
 *
 * The following defines are for the flags in the SP control register.
 *
 *****************************************************************************/
#define SPCR_RUN                                0x00000001L
#define SPCR_STPFR                              0x00000002L
#define SPCR_RUNFR                              0x00000004L
#define SPCR_TICK                               0x00000008L
#define SPCR_DRQEN                              0x00000020L
#define SPCR_RSTSP                              0x00000040L
#define SPCR_OREN                               0x00000080L
#ifndef NO_CS4612
#define SPCR_PCIINT                             0x00000100L
#define SPCR_OINTD                              0x00000200L
#define SPCR_CRE                                0x00008000L
#endif

/*****************************************************************************
 *
 * The following defines are for the flags in the debug index register.
 *
 *****************************************************************************/
#define DREG_REGID_MASK                         0x0000007FL
#define DREG_DEBUG                              0x00000080L
#define DREG_RGBK_MASK                          0x00000700L
#define DREG_TRAP                               0x00000800L
#if !defined(NO_CS4612)
#if !defined(NO_CS4615)
#define DREG_TRAPX                              0x00001000L
#endif
#endif
#define DREG_REGID_SHIFT                        0L
#define DREG_RGBK_SHIFT                         8L
#define DREG_RGBK_REGID_MASK                    0x0000077FL
#define DREG_REGID_R0                           0x00000010L
#define DREG_REGID_R1                           0x00000011L
#define DREG_REGID_R2                           0x00000012L
#define DREG_REGID_R3                           0x00000013L
#define DREG_REGID_R4                           0x00000014L
#define DREG_REGID_R5                           0x00000015L
#define DREG_REGID_R6                           0x00000016L
#define DREG_REGID_R7                           0x00000017L
#define DREG_REGID_R8                           0x00000018L
#define DREG_REGID_R9                           0x00000019L
#define DREG_REGID_RA                           0x0000001AL
#define DREG_REGID_RB                           0x0000001BL
#define DREG_REGID_RC                           0x0000001CL
#define DREG_REGID_RD                           0x0000001DL
#define DREG_REGID_RE                           0x0000001EL
#define DREG_REGID_RF                           0x0000001FL
#define DREG_REGID_RA_BUS_LOW                   0x00000020L
#define DREG_REGID_RA_BUS_HIGH                  0x00000038L
#define DREG_REGID_YBUS_LOW                     0x00000050L
#define DREG_REGID_YBUS_HIGH                    0x00000058L
#define DREG_REGID_TRAP_0                       0x00000100L
#define DREG_REGID_TRAP_1                       0x00000101L
#define DREG_REGID_TRAP_2                       0x00000102L
#define DREG_REGID_TRAP_3                       0x00000103L
#define DREG_REGID_TRAP_4                       0x00000104L
#define DREG_REGID_TRAP_5                       0x00000105L
#define DREG_REGID_TRAP_6                       0x00000106L
#define DREG_REGID_TRAP_7                       0x00000107L
#define DREG_REGID_INDIRECT_ADDRESS             0x0000010EL
#define DREG_REGID_TOP_OF_STACK                 0x0000010FL
#if !defined(NO_CS4612)
#if !defined(NO_CS4615)
#define DREG_REGID_TRAP_8                       0x00000110L
#define DREG_REGID_TRAP_9                       0x00000111L
#define DREG_REGID_TRAP_10                      0x00000112L
#define DREG_REGID_TRAP_11                      0x00000113L
#define DREG_REGID_TRAP_12                      0x00000114L
#define DREG_REGID_TRAP_13                      0x00000115L
#define DREG_REGID_TRAP_14                      0x00000116L
#define DREG_REGID_TRAP_15                      0x00000117L
#define DREG_REGID_TRAP_16                      0x00000118L
#define DREG_REGID_TRAP_17                      0x00000119L
#define DREG_REGID_TRAP_18                      0x0000011AL
#define DREG_REGID_TRAP_19                      0x0000011BL
#define DREG_REGID_TRAP_20                      0x0000011CL
#define DREG_REGID_TRAP_21                      0x0000011DL
#define DREG_REGID_TRAP_22                      0x0000011EL
#define DREG_REGID_TRAP_23                      0x0000011FL
#endif
#endif
#define DREG_REGID_RSA0_LOW                     0x00000200L
#define DREG_REGID_RSA0_HIGH                    0x00000201L
#define DREG_REGID_RSA1_LOW                     0x00000202L
#define DREG_REGID_RSA1_HIGH                    0x00000203L
#define DREG_REGID_RSA2                         0x00000204L
#define DREG_REGID_RSA3                         0x00000205L
#define DREG_REGID_RSI0_LOW                     0x00000206L
#define DREG_REGID_RSI0_HIGH                    0x00000207L
#define DREG_REGID_RSI1                         0x00000208L
#define DREG_REGID_RSI2                         0x00000209L
#define DREG_REGID_SAGUSTATUS                   0x0000020AL
#define DREG_REGID_RSCONFIG01_LOW               0x0000020BL
#define DREG_REGID_RSCONFIG01_HIGH              0x0000020CL
#define DREG_REGID_RSCONFIG23_LOW               0x0000020DL
#define DREG_REGID_RSCONFIG23_HIGH              0x0000020EL
#define DREG_REGID_RSDMA01E                     0x0000020FL
#define DREG_REGID_RSDMA23E                     0x00000210L
#define DREG_REGID_RSD0_LOW                     0x00000211L
#define DREG_REGID_RSD0_HIGH                    0x00000212L
#define DREG_REGID_RSD1_LOW                     0x00000213L
#define DREG_REGID_RSD1_HIGH                    0x00000214L
#define DREG_REGID_RSD2_LOW                     0x00000215L
#define DREG_REGID_RSD2_HIGH                    0x00000216L
#define DREG_REGID_RSD3_LOW                     0x00000217L
#define DREG_REGID_RSD3_HIGH                    0x00000218L
#define DREG_REGID_SRAR_HIGH                    0x0000021AL
#define DREG_REGID_SRAR_LOW                     0x0000021BL
#define DREG_REGID_DMA_STATE                    0x0000021CL
#define DREG_REGID_CURRENT_DMA_STREAM           0x0000021DL
#define DREG_REGID_NEXT_DMA_STREAM              0x0000021EL
#define DREG_REGID_CPU_STATUS                   0x00000300L
#define DREG_REGID_MAC_MODE                     0x00000301L
#define DREG_REGID_STACK_AND_REPEAT             0x00000302L
#define DREG_REGID_INDEX0                       0x00000304L
#define DREG_REGID_INDEX1                       0x00000305L
#define DREG_REGID_DMA_STATE_0_3                0x00000400L
#define DREG_REGID_DMA_STATE_4_7                0x00000404L
#define DREG_REGID_DMA_STATE_8_11               0x00000408L
#define DREG_REGID_DMA_STATE_12_15              0x0000040CL
#define DREG_REGID_DMA_STATE_16_19              0x00000410L
#define DREG_REGID_DMA_STATE_20_23              0x00000414L
#define DREG_REGID_DMA_STATE_24_27              0x00000418L
#define DREG_REGID_DMA_STATE_28_31              0x0000041CL
#define DREG_REGID_DMA_STATE_32_35              0x00000420L
#define DREG_REGID_DMA_STATE_36_39              0x00000424L
#define DREG_REGID_DMA_STATE_40_43              0x00000428L
#define DREG_REGID_DMA_STATE_44_47              0x0000042CL
#define DREG_REGID_DMA_STATE_48_51              0x00000430L
#define DREG_REGID_DMA_STATE_52_55              0x00000434L
#define DREG_REGID_DMA_STATE_56_59              0x00000438L
#define DREG_REGID_DMA_STATE_60_63              0x0000043CL
#define DREG_REGID_DMA_STATE_64_67              0x00000440L
#define DREG_REGID_DMA_STATE_68_71              0x00000444L
#define DREG_REGID_DMA_STATE_72_75              0x00000448L
#define DREG_REGID_DMA_STATE_76_79              0x0000044CL
#define DREG_REGID_DMA_STATE_80_83              0x00000450L
#define DREG_REGID_DMA_STATE_84_87              0x00000454L
#define DREG_REGID_DMA_STATE_88_91              0x00000458L
#define DREG_REGID_DMA_STATE_92_95              0x0000045CL
#define DREG_REGID_TRAP_SELECT                  0x00000500L
#define DREG_REGID_TRAP_WRITE_0                 0x00000500L
#define DREG_REGID_TRAP_WRITE_1                 0x00000501L
#define DREG_REGID_TRAP_WRITE_2                 0x00000502L
#define DREG_REGID_TRAP_WRITE_3                 0x00000503L
#define DREG_REGID_TRAP_WRITE_4                 0x00000504L
#define DREG_REGID_TRAP_WRITE_5                 0x00000505L
#define DREG_REGID_TRAP_WRITE_6                 0x00000506L
#define DREG_REGID_TRAP_WRITE_7                 0x00000507L
#if !defined(NO_CS4612)
#if !defined(NO_CS4615)
#define DREG_REGID_TRAP_WRITE_8                 0x00000510L
#define DREG_REGID_TRAP_WRITE_9                 0x00000511L
#define DREG_REGID_TRAP_WRITE_10                0x00000512L
#define DREG_REGID_TRAP_WRITE_11                0x00000513L
#define DREG_REGID_TRAP_WRITE_12                0x00000514L
#define DREG_REGID_TRAP_WRITE_13                0x00000515L
#define DREG_REGID_TRAP_WRITE_14                0x00000516L
#define DREG_REGID_TRAP_WRITE_15                0x00000517L
#define DREG_REGID_TRAP_WRITE_16                0x00000518L
#define DREG_REGID_TRAP_WRITE_17                0x00000519L
#define DREG_REGID_TRAP_WRITE_18                0x0000051AL
#define DREG_REGID_TRAP_WRITE_19                0x0000051BL
#define DREG_REGID_TRAP_WRITE_20                0x0000051CL
#define DREG_REGID_TRAP_WRITE_21                0x0000051DL
#define DREG_REGID_TRAP_WRITE_22                0x0000051EL
#define DREG_REGID_TRAP_WRITE_23                0x0000051FL
#endif
#endif
#define DREG_REGID_MAC0_ACC0_LOW                0x00000600L
#define DREG_REGID_MAC0_ACC1_LOW                0x00000601L
#define DREG_REGID_MAC0_ACC2_LOW                0x00000602L
#define DREG_REGID_MAC0_ACC3_LOW                0x00000603L
#define DREG_REGID_MAC1_ACC0_LOW                0x00000604L
#define DREG_REGID_MAC1_ACC1_LOW                0x00000605L
#define DREG_REGID_MAC1_ACC2_LOW                0x00000606L
#define DREG_REGID_MAC1_ACC3_LOW                0x00000607L
#define DREG_REGID_MAC0_ACC0_MID                0x00000608L
#define DREG_REGID_MAC0_ACC1_MID                0x00000609L
#define DREG_REGID_MAC0_ACC2_MID                0x0000060AL
#define DREG_REGID_MAC0_ACC3_MID                0x0000060BL
#define DREG_REGID_MAC1_ACC0_MID                0x0000060CL
#define DREG_REGID_MAC1_ACC1_MID                0x0000060DL
#define DREG_REGID_MAC1_ACC2_MID                0x0000060EL
#define DREG_REGID_MAC1_ACC3_MID                0x0000060FL
#define DREG_REGID_MAC0_ACC0_HIGH               0x00000610L
#define DREG_REGID_MAC0_ACC1_HIGH               0x00000611L
#define DREG_REGID_MAC0_ACC2_HIGH               0x00000612L
#define DREG_REGID_MAC0_ACC3_HIGH               0x00000613L
#define DREG_REGID_MAC1_ACC0_HIGH               0x00000614L
#define DREG_REGID_MAC1_ACC1_HIGH               0x00000615L
#define DREG_REGID_MAC1_ACC2_HIGH               0x00000616L
#define DREG_REGID_MAC1_ACC3_HIGH               0x00000617L
#define DREG_REGID_RSHOUT_LOW                   0x00000620L
#define DREG_REGID_RSHOUT_MID                   0x00000628L
#define DREG_REGID_RSHOUT_HIGH                  0x00000630L

/*****************************************************************************
 *
 * The following defines are for the flags in the DMA stream requestor write
 * port register.
 *
 *****************************************************************************/
#define DSRWP_DSR_MASK                          0x0000000FL
#define DSRWP_DSR_BG_RQ                         0x00000001L
#define DSRWP_DSR_PRIORITY_MASK                 0x00000006L
#define DSRWP_DSR_PRIORITY_0                    0x00000000L
#define DSRWP_DSR_PRIORITY_1                    0x00000002L
#define DSRWP_DSR_PRIORITY_2                    0x00000004L
#define DSRWP_DSR_PRIORITY_3                    0x00000006L
#define DSRWP_DSR_RQ_PENDING                    0x00000008L

/*****************************************************************************
 *
 * The following defines are for the flags in the trap write port register.
 *
 *****************************************************************************/
#define TWPR_TW_MASK                            0x0000FFFFL
#define TWPR_TW_SHIFT                           0L

/*****************************************************************************
 *
 * The following defines are for the flags in the stack pointer write
 * register.
 *
 *****************************************************************************/
#define SPWR_STKP_MASK                          0x0000000FL
#define SPWR_STKP_SHIFT                         0L

/*****************************************************************************
 *
 * The following defines are for the flags in the SP interrupt register.
 *
 *****************************************************************************/
#define SPIR_FRI                                0x00000001L
#define SPIR_DOI                                0x00000002L
#define SPIR_GPI2                               0x00000004L
#define SPIR_GPI3                               0x00000008L
#define SPIR_IP0                                0x00000010L
#define SPIR_IP1                                0x00000020L
#define SPIR_IP2                                0x00000040L
#define SPIR_IP3                                0x00000080L

/*****************************************************************************
 *
 * The following defines are for the flags in the functional group 1 register.
 *
 *****************************************************************************/
#define FGR1_F1S_MASK                           0x0000FFFFL
#define FGR1_F1S_SHIFT                          0L

/*****************************************************************************
 *
 * The following defines are for the flags in the SP clock status register.
 *
 *****************************************************************************/
#define SPCS_FRI                                0x00000001L
#define SPCS_DOI                                0x00000002L
#define SPCS_GPI2                               0x00000004L
#define SPCS_GPI3                               0x00000008L
#define SPCS_IP0                                0x00000010L
#define SPCS_IP1                                0x00000020L
#define SPCS_IP2                                0x00000040L
#define SPCS_IP3                                0x00000080L
#define SPCS_SPRUN                              0x00000100L
#define SPCS_SLEEP                              0x00000200L
#define SPCS_FG                                 0x00000400L
#define SPCS_ORUN                               0x00000800L
#define SPCS_IRQ                                0x00001000L
#define SPCS_FGN_MASK                           0x0000E000L
#define SPCS_FGN_SHIFT                          13L

/*****************************************************************************
 *
 * The following defines are for the flags in the SP DMA requestor status
 * register.
 *
 *****************************************************************************/
#define SDSR_DCS_MASK                           0x000000FFL
#define SDSR_DCS_SHIFT                          0L
#define SDSR_DCS_NONE                           0x00000007L

/*****************************************************************************
 *
 * The following defines are for the flags in the frame timer register.
 *
 *****************************************************************************/
#define FRMT_FTV_MASK                           0x0000FFFFL
#define FRMT_FTV_SHIFT                          0L

/*****************************************************************************
 *
 * The following defines are for the flags in the frame timer current count
 * register.
 *
 *****************************************************************************/
#define FRCC_FCC_MASK                           0x0000FFFFL
#define FRCC_FCC_SHIFT                          0L

/*****************************************************************************
 *
 * The following defines are for the flags in the frame timer save count
 * register.
 *
 *****************************************************************************/
#define FRSC_FCS_MASK                           0x0000FFFFL
#define FRSC_FCS_SHIFT                          0L

/*****************************************************************************
 *
 * The following define the various flags stored in the scatter/gather
 * descriptors.
 *
 *****************************************************************************/
#define DMA_SG_NEXT_ENTRY_MASK                  0x00000FF8L
#define DMA_SG_SAMPLE_END_MASK                  0x0FFF0000L
#define DMA_SG_SAMPLE_END_FLAG                  0x10000000L
#define DMA_SG_LOOP_END_FLAG                    0x20000000L
#define DMA_SG_SIGNAL_END_FLAG                  0x40000000L
#define DMA_SG_SIGNAL_PAGE_FLAG                 0x80000000L
#define DMA_SG_NEXT_ENTRY_SHIFT                 3L
#define DMA_SG_SAMPLE_END_SHIFT                 16L

/*****************************************************************************
 *
 * The following define the offsets of the fields within the on-chip generic
 * DMA requestor.
 *
 *****************************************************************************/
#define DMA_RQ_CONTROL1                         0x00000000L
#define DMA_RQ_CONTROL2                         0x00000004L
#define DMA_RQ_SOURCE_ADDR                      0x00000008L
#define DMA_RQ_DESTINATION_ADDR                 0x0000000CL
#define DMA_RQ_NEXT_PAGE_ADDR                   0x00000010L
#define DMA_RQ_NEXT_PAGE_SGDESC                 0x00000014L
#define DMA_RQ_LOOP_START_ADDR                  0x00000018L
#define DMA_RQ_POST_LOOP_ADDR                   0x0000001CL
#define DMA_RQ_PAGE_MAP_ADDR                    0x00000020L

/*****************************************************************************
 *
 * The following defines are for the flags in the first control word of the
 * on-chip generic DMA requestor.
 *
 *****************************************************************************/
#define DMA_RQ_C1_COUNT_MASK                    0x000003FFL
#define DMA_RQ_C1_DESTINATION_SCATTER           0x00001000L
#define DMA_RQ_C1_SOURCE_GATHER                 0x00002000L
#define DMA_RQ_C1_DONE_FLAG                     0x00004000L
#define DMA_RQ_C1_OPTIMIZE_STATE                0x00008000L
#define DMA_RQ_C1_SAMPLE_END_STATE_MASK         0x00030000L
#define DMA_RQ_C1_FULL_PAGE                     0x00000000L
#define DMA_RQ_C1_BEFORE_SAMPLE_END             0x00010000L
#define DMA_RQ_C1_PAGE_MAP_ERROR                0x00020000L
#define DMA_RQ_C1_AT_SAMPLE_END                 0x00030000L
#define DMA_RQ_C1_LOOP_END_STATE_MASK           0x000C0000L
#define DMA_RQ_C1_NOT_LOOP_END                  0x00000000L
#define DMA_RQ_C1_BEFORE_LOOP_END               0x00040000L
#define DMA_RQ_C1_2PAGE_LOOP_BEGIN              0x00080000L
#define DMA_RQ_C1_LOOP_BEGIN                    0x000C0000L
#define DMA_RQ_C1_PAGE_MAP_MASK                 0x00300000L
#define DMA_RQ_C1_PM_NONE_PENDING               0x00000000L
#define DMA_RQ_C1_PM_NEXT_PENDING               0x00100000L
#define DMA_RQ_C1_PM_RESERVED                   0x00200000L
#define DMA_RQ_C1_PM_LOOP_NEXT_PENDING          0x00300000L
#define DMA_RQ_C1_WRITEBACK_DEST_FLAG           0x00400000L
#define DMA_RQ_C1_WRITEBACK_SRC_FLAG            0x00800000L
#define DMA_RQ_C1_DEST_SIZE_MASK                0x07000000L
#define DMA_RQ_C1_DEST_LINEAR                   0x00000000L
#define DMA_RQ_C1_DEST_MOD16                    0x01000000L
#define DMA_RQ_C1_DEST_MOD32                    0x02000000L
#define DMA_RQ_C1_DEST_MOD64                    0x03000000L
#define DMA_RQ_C1_DEST_MOD128                   0x04000000L
#define DMA_RQ_C1_DEST_MOD256                   0x05000000L
#define DMA_RQ_C1_DEST_MOD512                   0x06000000L
#define DMA_RQ_C1_DEST_MOD1024                  0x07000000L
#define DMA_RQ_C1_DEST_ON_HOST                  0x08000000L
#define DMA_RQ_C1_SOURCE_SIZE_MASK              0x70000000L
#define DMA_RQ_C1_SOURCE_LINEAR                 0x00000000L
#define DMA_RQ_C1_SOURCE_MOD16                  0x10000000L
#define DMA_RQ_C1_SOURCE_MOD32                  0x20000000L
#define DMA_RQ_C1_SOURCE_MOD64                  0x30000000L
#define DMA_RQ_C1_SOURCE_MOD128                 0x40000000L
#define DMA_RQ_C1_SOURCE_MOD256                 0x50000000L
#define DMA_RQ_C1_SOURCE_MOD512                 0x60000000L
#define DMA_RQ_C1_SOURCE_MOD1024                0x70000000L
#define DMA_RQ_C1_SOURCE_ON_HOST                0x80000000L
#define DMA_RQ_C1_COUNT_SHIFT                   0L

/*****************************************************************************
 *
 * The following defines are for the flags in the second control word of the
 * on-chip generic DMA requestor.
 *
 *****************************************************************************/
#define DMA_RQ_C2_VIRTUAL_CHANNEL_MASK          0x0000003FL
#define DMA_RQ_C2_VIRTUAL_SIGNAL_MASK           0x00000300L
#define DMA_RQ_C2_NO_VIRTUAL_SIGNAL             0x00000000L
#define DMA_RQ_C2_SIGNAL_EVERY_DMA              0x00000100L
#define DMA_RQ_C2_SIGNAL_SOURCE_PINGPONG        0x00000200L
#define DMA_RQ_C2_SIGNAL_DEST_PINGPONG          0x00000300L
#define DMA_RQ_C2_AUDIO_CONVERT_MASK            0x0000F000L
#define DMA_RQ_C2_AC_NONE                       0x00000000L
#define DMA_RQ_C2_AC_8_TO_16_BIT                0x00001000L
#define DMA_RQ_C2_AC_MONO_TO_STEREO             0x00002000L
#define DMA_RQ_C2_AC_ENDIAN_CONVERT             0x00004000L
#define DMA_RQ_C2_AC_SIGNED_CONVERT             0x00008000L
#define DMA_RQ_C2_LOOP_END_MASK                 0x0FFF0000L
#define DMA_RQ_C2_LOOP_MASK                     0x30000000L
#define DMA_RQ_C2_NO_LOOP                       0x00000000L
#define DMA_RQ_C2_ONE_PAGE_LOOP                 0x10000000L
#define DMA_RQ_C2_TWO_PAGE_LOOP                 0x20000000L
#define DMA_RQ_C2_MULTI_PAGE_LOOP               0x30000000L
#define DMA_RQ_C2_SIGNAL_LOOP_BACK              0x40000000L
#define DMA_RQ_C2_SIGNAL_POST_BEGIN_PAGE        0x80000000L
#define DMA_RQ_C2_VIRTUAL_CHANNEL_SHIFT         0L
#define DMA_RQ_C2_LOOP_END_SHIFT                16L

/*****************************************************************************
 *
 * The following defines are for the flags in the source and destination words
 * of the on-chip generic DMA requestor.
 *
 *****************************************************************************/
#define DMA_RQ_SD_ADDRESS_MASK                  0x0000FFFFL
#define DMA_RQ_SD_MEMORY_ID_MASK                0x000F0000L
#define DMA_RQ_SD_SP_PARAM_ADDR                 0x00000000L
#define DMA_RQ_SD_SP_SAMPLE_ADDR                0x00010000L
#define DMA_RQ_SD_SP_PROGRAM_ADDR               0x00020000L
#define DMA_RQ_SD_SP_DEBUG_ADDR                 0x00030000L
#define DMA_RQ_SD_OMNIMEM_ADDR                  0x000E0000L
#define DMA_RQ_SD_END_FLAG                      0x40000000L
#define DMA_RQ_SD_ERROR_FLAG                    0x80000000L
#define DMA_RQ_SD_ADDRESS_SHIFT                 0L

/*****************************************************************************
 *
 * The following defines are for the flags in the page map address word of the
 * on-chip generic DMA requestor.
 *
 *****************************************************************************/
#define DMA_RQ_PMA_LOOP_THIRD_PAGE_ENTRY_MASK   0x00000FF8L
#define DMA_RQ_PMA_PAGE_TABLE_MASK              0xFFFFF000L
#define DMA_RQ_PMA_LOOP_THIRD_PAGE_ENTRY_SHIFT  3L
#define DMA_RQ_PMA_PAGE_TABLE_SHIFT             12L

/*****************************************************************************
 *
 * The following defines are for the flags in the rsConfig01/23 registers of
 * the SP.
 *
 *****************************************************************************/
#define RSCONFIG_MODULO_SIZE_MASK               0x0000000FL
#define RSCONFIG_MODULO_16                      0x00000001L
#define RSCONFIG_MODULO_32                      0x00000002L
#define RSCONFIG_MODULO_64                      0x00000003L
#define RSCONFIG_MODULO_128                     0x00000004L
#define RSCONFIG_MODULO_256                     0x00000005L
#define RSCONFIG_MODULO_512                     0x00000006L
#define RSCONFIG_MODULO_1024                    0x00000007L
#define RSCONFIG_MODULO_4                       0x00000008L
#define RSCONFIG_MODULO_8                       0x00000009L
#define RSCONFIG_SAMPLE_SIZE_MASK               0x000000C0L
#define RSCONFIG_SAMPLE_8MONO                   0x00000000L
#define RSCONFIG_SAMPLE_8STEREO                 0x00000040L
#define RSCONFIG_SAMPLE_16MONO                  0x00000080L
#define RSCONFIG_SAMPLE_16STEREO                0x000000C0L
#define RSCONFIG_UNDERRUN_ZERO                  0x00004000L
#define RSCONFIG_DMA_TO_HOST                    0x00008000L
#define RSCONFIG_STREAM_NUM_MASK                0x00FF0000L
#define RSCONFIG_MAX_DMA_SIZE_MASK              0x1F000000L
#define RSCONFIG_DMA_ENABLE                     0x20000000L
#define RSCONFIG_PRIORITY_MASK                  0xC0000000L
#define RSCONFIG_PRIORITY_HIGH                  0x00000000L
#define RSCONFIG_PRIORITY_MEDIUM_HIGH           0x40000000L
#define RSCONFIG_PRIORITY_MEDIUM_LOW            0x80000000L
#define RSCONFIG_PRIORITY_LOW                   0xC0000000L
#define RSCONFIG_STREAM_NUM_SHIFT               16L
#define RSCONFIG_MAX_DMA_SIZE_SHIFT             24L

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

/* PM state definitions */
#define CS461x_AC97_HIGHESTREGTORESTORE	0x26
#define CS461x_AC97_NUMBER_RESTORE_REGS	(CS461x_AC97_HIGHESTREGTORESTORE/2-1)

#define CS_POWER_DAC			0x0001
#define CS_POWER_ADC			0x0002
#define CS_POWER_MIXVON			0x0004
#define CS_POWER_MIXVOFF		0x0008
#define CS_AC97_POWER_CONTROL_ON	0xf000	/* always on bits (inverted) */
#define CS_AC97_POWER_CONTROL_ADC	0x0100
#define CS_AC97_POWER_CONTROL_DAC	0x0200
#define CS_AC97_POWER_CONTROL_MIXVON	0x0400
#define CS_AC97_POWER_CONTROL_MIXVOFF	0x0800
#define CS_AC97_POWER_CONTROL_ADC_ON	0x0001
#define CS_AC97_POWER_CONTROL_DAC_ON	0x0002
#define CS_AC97_POWER_CONTROL_MIXVON_ON	0x0004
#define CS_AC97_POWER_CONTROL_MIXVOFF_ON 0x0008

/*  
 * this is 3*1024 for parameter, 3.5*1024 for sample and 2*3.5*1024 
 * for code since each instruction is 40 bits and takes two dwords
 */

/* The following struct holds the initialization array. */
#define INKY_BA1_DWORD_SIZE  (13*1024+512)
/* this is parameter, sample, and code */
#define INKY_MEMORY_COUNT     3

struct cs461x_firmware_struct
{
	struct
	{
		u_int32_t ulDestAddr, ulSourceSize;
	} MemoryStat[INKY_MEMORY_COUNT];

	u_int32_t BA1Array[INKY_BA1_DWORD_SIZE];
};


#endif /* _CSA_REG_H */
