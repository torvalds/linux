/*	$OpenBSD: cs4280reg.h,v 1.2 2022/01/09 05:42:45 jsg Exp $	*/
/*	$NetBSD: cs4280reg.h,v 1.3 2000/05/15 01:35:29 thorpej Exp $	*/

/*
 * Copyright (c) 1999, 2000 Tatoku Ogaito.  All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 * 3. All advertising materials mentioning features or use of this software
 *    must display the following acknowledgement:
 *      This product includes software developed by Tatoku Ogaito
 *	for the NetBSD Project.
 * 4. The name of the author may not be used to endorse or promote products
 *    derived from this software without specific prior written permission
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR ``AS IS'' AND ANY EXPRESS OR
 * IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES
 * OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED.
 * IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR ANY DIRECT, INDIRECT,
 * INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT
 * NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 * DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
 * THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF
 * THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */


#define CS4280_BA0_SIZE   0x2000
#define CS4280_BA1_SIZE   0x40000

/* BA0 */

/* Interrupt Reporting Registers */
#define CS4280_HISR        0x000	/* Host Interrupt Status Register */
#define  HISR_INTENA       0x80000000
#define  HISR_MIDI         0x00100000
#define  HISR_CINT         0x00000002
#define  HISR_PINT         0x00000001
#define CS4280_HICR        0x008	/* Host Interrupt Control Register */
#define  HICR_CHGM         0x00000002
#define  HICR_IEV          0x00000001

/* Clock Control Registers */
#define CS4280_CLKCR1      0x400	/* Clock Control Register 1 */
#define  CLKCR1_PLLSS_SPBC 0x00000000
#define  CLKCR1_PLLSS_RSV  0x00000004
#define  CLKCR1_PLLSS_PCI  0x00000008
#define  CLKCR1_PLLSS_RSV2 0x0000000c
#define  CLKCR1_PLLP       0x00000010
#define  CLKCR1_SWCE       0x00000020

#define CS4280_CLKCR2      0x404	/* Clock Control Register 2 */
#define  CLKCR2_PDIVS_RSV  0x00000002
#define  CLKCR2_PDIVS_8    0x00000008
#define  CLKCR2_PDIVS_16   0x00000000

#define CS4280_PLLM        0x408	/* PLL Multiplier Register */
#define  PLLM_STATE        0x0000003a

#define CS4280_PLLCC       0x40c	/* PLL Capacitor Coefficient Register */
#define  PLLCC_CDR_STATE   0x00000006
#define  PLLCC_LPF_STATE   0x00000078

/* General Configuration Registers */
#define CS4280_SERMC1      0x420	/* Serial Port Master Control Register 1 */
#define  SERMC1_MSPE       0x00000001
#define  SERMC1_PTC_MASK   0x0000000e
#define  SERMC1_PTC_CS423X 0x00000000
#define  SERMC1_PTC_AC97   0x00000002
#define  SERMC1_PLB_EN     0x00000010
#define  SERMC1_XLB_EN     0x00000020
#define CS4280_SERC1       0x428	/* Serial Port Configuration Register 1 */
#define  SERC1_SO1EN       0x00000001
#define  SERC1_SO1F_MASK   0x0000000e
#define  SERC1_SO1F_CS423X 0x00000000
#define  SERC1_SO1F_AC97   0x00000002
#define  SERC1_SO1F_DAC    0x00000004
#define  SERC1_SO1F_SPDIF  0x00000006
#define CS4280_SERC2       0x42c	/* Serial Port Configuration Register 2 */
#define  SERC2_SI1EN       0x00000001
#define  SERC2_SI1F_MASK   0x0000000e
#define  SERC2_SI1F_CS423X 0x00000000
#define  SERC2_SI1F_AC97   0x00000002
#define  SERC2_SI1F_ADC    0x00000004
#define  SERC2_SI1F_SPDIF  0x00000006

#define CS4280_SERBSP      0x43c
#define  SERBSP_FSP_MASK   0x0000000f

#define CS4280_SERBST      0x440
#define  SERBST_RRDY       0x00000001
#define  SERBST_WBSY       0x00000002
#define CS4280_SERBCM      0x444
#define  SERBCM_RDC        0x000000001
#define  SERBCM_WRC        0x000000002
#define CS4280_SERBAD      0x448
#define CS4280_SERBWP      0x450
/* AC97 Registers */
#define CS4280_ACCTL       0x460	/* AC97 Control Register */
#define  ACCTL_RSTN        0x00000001
#define  ACCTL_ESYN        0x00000002
#define  ACCTL_VFRM        0x00000004
#define  ACCTL_DCV         0x00000008
#define  ACCTL_CRW         0x00000010
#define  ACCTL_ASYN        0x00000020
#define  ACCTL_TC          0x00000040
#define CS4280_ACSTS       0x464	/* AC97 Status Register */
#define  ACSTS_CRDY        0x00000001
#define  ACSTS_VSTS        0x00000002
#define  ACSTS_WKUP        0x00000004
#define CS4280_ACOSV       0x468	/* AC97 Output Slot Valid Register */
#define  ACOSV_SLV3        0x00000001
#define  ACOSV_SLV4        0x00000002
#define  ACOSV_SLV5        0x00000004
#define  ACOSV_SLV6        0x00000008
#define  ACOSV_SLV7        0x00000010
#define  ACOSV_SLV8        0x00000020
#define  ACOSV_SLV9        0x00000040
#define  ACOSV_SLV10       0x00000080
#define  ACOSV_SLV11       0x00000100
#define  ACOSV_SLV12       0x00000200

#define CS4280_ACCAD       0x46c	/* AC97 Command Address Register */
#define CS4280_ACCDA       0x470	/* AC97 Command Data Register */
#define CS4280_ACISV       0x474	/* AC97 Input Slot Valid Register */
#define  ACISV_ISV3        0x00000001
#define  ACISV_ISV4        0x00000002
#define  ACISV_ISV5        0x00000004
#define  ACISV_ISV6        0x00000008
#define  ACISV_ISV7        0x00000010
#define  ACISV_ISV8        0x00000020
#define  ACISV_ISV9        0x00000040
#define  ACISV_ISV10       0x00000080
#define  ACISV_ISV11       0x00000100
#define  ACISV_ISV12       0x00000200
#define CS4280_ACSAD       0x478	/* AC97 Status Address Register */
#define CS4280_ACSDA       0x47c	/* AC97 Status Data Register */

/* Host Access Methods */
#define CS4280_GPIOR	   0x4b8	/* General Purpose I/O Register */
#define CS4280_EGPIODR	   0x4bc	/* Extended GPIO Direction Register */
#define CS4280_EGPIOPTR    0x4c0        /* Extended GPIO Polarity/Type Register */
#define CS4280_EGPIOTR     0x4c4        /* Extended GPIO Sticky Register */
#define CS4280_EGPIOWR     0x4c8        /* Extended GPIO Wakeup Register */
#define CS4280_EGPIOSR     0x4cc        /* Extended GPIO Status Register */

/* Control Register */
#define CS4280_CFGI	   0x4b0        /* Configuration Interface Register */

#define CS4280_SERACC      0x4d8
#define  SERACC_CTYPE_MASK 0x00000001
#define  SERACC_CTYPE_1_03 0x00000000
#define  SERACC_CTYPE_2_0  0x00000001
#define  SERACC_TWO_CODECS 0x00000002
#define  SERACC_MDM        0x00000004
#define  SERACC_HSP        0x00000008

/* Midi Port */
#define CS4280_MIDCR       0x490        /* MIDI Control Register */
#define  MIDCR_TXE         0x00000001   /* MIDI Transmit Enable */
#define  MIDCR_RXE         0x00000002   /* MIDI Receive Enable */
#define  MIDCR_RIE         0x00000004   /* MIDI Receive Interrupt Enable */
#define  MIDCR_TIE         0x00000008   /* MIDI Transmit Interrupt Enable */
#define  MIDCR_MLB         0x00000010   /* MIDI Loop Back Enable */
#define  MIDCR_MRST        0x00000020   /* MIDI Reset */
#define  MIDCR_MASK        0x0000003f
#define CS4280_MIDSR       0x494        /* Host MIDI Status Register */
#define  MIDSR_TBF         0x00000001   /* Transmit Buffer Full */
#define  MIDSR_RBE         0x00000002   /* Receive Buffer Empty */
#define CS4280_MIDWP       0x498        /* MIDI Write Port */
#define  MIDWP_MASK        0x000000ff
#define CS4280_MIDRP       0x49c        /* MIDI Read Port */
#define  MIDRP_MASK        0x000000ff

/* Joy Stick Port */
#define CS4280_JSPT        0x480        /* Joystick Poll/Trigger Register */
#define CS4280_JSCTL       0x484        /* Joystick Control Register */
#define CS4280_JSC1        0x488        /* Joystick Coordinate Register 1 */
#define CS4280_JSC2        0x48c        /* Joystick Coordinate Register 2 */


/* BA1 */

/* Playback Parameters */
#define CS4280_PDTC       0x00c0	/* Playback DMA Transaction Count */
#define  PDTC_MASK        0x000003ff
#define  CS4280_MK_PDTC(x) ((x)/2 - 1)
#define CS4280_PFIE       0x00c4	/* Playback Format and Interrupt Enable */
#define  PFIE_UNSIGNED    0x00008000    /* Playback Format is unsigned */
#define  PFIE_SWAPPED     0x00004000    /* Playback Format is need swapped */
#define  PFIE_MONO        0x00002000    /* Playback Format is monoral */
#define  PFIE_8BIT        0x00001000    /* Playback Format is 8bit */
#define  PFIE_PI_ENABLE   0x00000000    /* Playback Interrupt Enabled */
#define  PFIE_PI_DISABLE  0x00000010    /* Playback Interrupt Disabled */
#define  PFIE_PI_MASK     0x0000003f
#define  PFIE_MASK        0x0000f03f
#define CS4280_PBA        0x00c8	/* Playback Buffer Address */
#define CS4280_PVOL       0x00f8	/* Playback Volume */
#define CS4280_PSRC       0x0288	/* Playback Sample Rate Correction */
#define  PSRC_MASK        0xffff0000
#define  CS4280_MK_PSRC(psrc, py) ((((psrc) << 16) & 0xffff0000) | ((py) & 0xffff))
#define CS4280_PCTL       0x02a4	/* Playback Control */
#define  PCTL_MASK        0xffff0000
#define CS4280_PPI        0x02b4	/* Playback Phase Increment */

/* Capture Parameters */
#define CS4280_CCTL       0x0064	/* Capture Control */
#define  CCTL_MASK        0x0000ffff
#define CS4280_CDTC       0x0100	/* Capture DMA Transaction Count */
#define CS4280_CIE        0x0104	/* Capture Interrupt Enable */
#define  CIE_CI_ENABLE    0x00000001    /* Capture Interrupt enabled */
#define  CIE_CI_DISABLE   0x00000011    /* Capture Interrupt disabled */
#define  CIE_CI_MASK      0x0000003f
#define CS4280_CBA        0x010c	/* Capture Buffer Address */
#define CS4280_CSRC       0x02c8	/* Capture Sample Rate Correction */
#define  CSRC_MASK        0xffff0000
#define  CS4280_MK_CSRC(csrc, cy) ((((csrc) << 16) & 0xffff0000) | ((cy) & 0xffff))
#define CS4280_CCI        0x02d8	/* Capture Coefficient Increment */
#define  CCI_MASK         0xffff0000
#define CS4280_CD         0x02e0	/* Capture Delay */
#define  CD_MASK          0xfffc000
#define CS4280_CPI        0x02f4	/* Capture Phase Increment */
#define CS4280_CGL        0x0134	/* Capture Group Length */
#define  CGL_MASK         0x0000ffff
#define CS4280_CNT        0x0340	/* Capture Number of Triplets */
#define CS4280_CGC        0x0138	/* Capture Group Count */
#define  CGC_MASK         0x0000ffff
#define CS4280_CVOL       0x02f8	/* Capture Volume */

/* Processor Registers */
#define CS4280_SPCR       0x30000	/* Processor Control Register */
#define  SPCR_RUN         0x00000001
#define  SPCR_STPFR       0x00000002
#define  SPCR_RUNFR       0x00000004
#define  SPCR_DRQEN       0x00000020
#define  SPCR_RSTSP       0x00000040
#define CS4280_DREG       0x30004
#define CS4280_DSRWP      0x30008
#define CS4280_TWPR       0x3000c	/* Trap Write Port Register */
#define CS4280_SPWR       0x30010
#define CS4280_SPCS       0x30028	/* Processor Clock Status Register */
#define  SPCS_SPRUN       0x00000100
#define CS4280_FRMT       0x30030	/* Frame Timer Register */
#define  FRMT_FTV         0x00000adf


#define CF_MONO           0x01
#define CF_8BIT           0x02

#define CF_16BIT_STEREO   0x00
#define CF_16BIT_MONO     0x01
#define CF_8BIT_STEREO    0x02
#define CF_8BIT_MONO      0x03

#define MIDI_BUSY_WAIT		100
#define MIDI_BUSY_DELAY		100	/* Delay when UART is busy */

/* 3*1024 parameter, 3.5*1024 sample, 2*3.5*1024 code */
#define BA1_DWORD_SIZE		(13 * 1024 + 512)
#define BA1_MEMORY_COUNT	3

struct BA1struct {
	struct {
		u_int32_t offset;
		u_int32_t size;
	} memory[BA1_MEMORY_COUNT];
	u_int32_t map[BA1_DWORD_SIZE];
};

#define CS4280_ICHUNK	2048	/* Bytes between interrupts */
#define CS4280_DCHUNK	4096	/* Bytes of DMA memory */
#define CS4280_DALIGN	4096	/* Alignment of DMA memory */

/* AC97 Registers */
#define CS4280_SAVE_REG_MAX  0x10

/* for AC97_REG_POWER */
#define   CS4280_POWER_DOWN_ALL       0x7f0f
