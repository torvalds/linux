/*	$OpenBSD: cs4281reg.h,v 1.2 2001/02/09 21:15:23 aaron Exp $ */
/*	$Tera: cs4281reg.h,v 1.9 2000/12/31 10:52:25 tacha Exp $	*/

/*
 * Copyright (c) 2000 Tatoku Ogaito.  All rights reserved.
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


#define CS4281_BA0_SIZE		
#define CS4281_BA1_SIZE	 	0x10000
#define CS4281_BUFFER_SIZE      0x10000

/* Base Address 0 */

/* Interrupt Reporting Registers */
#define CS4281_HISR    0x000 /* Host Interrupt Status Register*/
#define  HISR_INTENA       0x80000000
#define  HISR_MIDI         0x00400000
#define  HISR_FIFOI        0x00100000
#define  HISR_DMAI         0x00040000
#define  HISR_FIFO3        0x00008000
#define  HISR_FIFO2        0x00004000
#define  HISR_FIFO1        0x00002000
#define  HISR_FIFO0        0x00001000
#define  HISR_DMA3         0x00000800
#define  HISR_DMA2         0x00000400
#define  HISR_DMA1         0x00000200
#define  HISR_DMA0         0x00000100
#define  HISR_GPPI         0x00000020
#define  HISR_GPSI         0x00000010
#define  HISR_GP3I         0x00000008
#define  HISR_GP1I         0x00000004
#define  HISR_VUPI         0x00000002
#define  HISR_VDNI         0x00000001

#define CS4281_HICR    0x008 /* Host Interrupt Control Register */
#define  HICR_CHGM         0x00000002
#define  HICR_IEV          0x00000001

#define CS4281_HIMR    0x00C /* Host Interrupt Mask Register */
#define  HIMR_MIDIM        0x00400000
#define  HIMR_FIFOIM       0x00100000
#define  HIMR_DMAIM        0x00040000
#define  HIMR_F3IM         0x00008000
#define  HIMR_F2IM         0x00004000
#define  HIMR_F1IM         0x00002000
#define  HIMR_F0IM         0x00001000
#define  HIMR_D3IM         0x00000800
#define  HIMR_D2IM         0x00000400
#define  HIMR_D1IM         0x00000200
#define  HIMR_D0IM         0x00000100
#define  HIMR_GPPIM        0x00000020
#define  HIMR_GPSIM        0x00000010
#define  HIMR_GP3IM        0x00000008
#define  HIMR_GP1IM        0x00000004
#define  HIMR_VUPIM        0x00000002
#define  HIMR_VDNIM        0x00000001

#define CS4281_IIER    0x010 /* ISA Interrupt Enable Register */

#define CS4281_HDSR0   0x0F0 /* Host DMA Engine 0 Status Register */
#define CS4281_HDSR1   0x0F4 /* Host DMA Engine 1 Status Register */
#define CS4281_HDSR2   0x0F8 /* Host DMA Engine 2 Status Register */
#define CS4281_HDSR3   0x0FC /* Host DMA Engine 3 Status Register */
#define CS4281_DCA0    0x110 /* DMA Engine 0 Current Address Register */
#define CS4281_DCC0    0x114 /* DMA Engine 0 Current Count Register */
#define CS4281_DBA0    0x118 /* DMA Engine 0 Base Address Register */
#define CS4281_DBC0    0x11C /* DMA Engine 0 Base Count Register */
#define CS4281_DCA1    0x120 /* DMA Engine 1 Current Address Register */
#define CS4281_DCC1    0x124 /* DMA Engine 1 Current Count Register */
#define CS4281_DBA1    0x128 /* DMA Engine 1 Base Address Register */
#define CS4281_DBC1    0x12C /* DMA Engine 1 Base Count Register */
#define CS4281_DCA2    0x130 /* DMA Engine 2 Current Address Register */
#define CS4281_DCC2    0x134 /* DMA Engine 2 Current Count Register */
#define CS4281_DBA2    0x138 /* DMA Engine 2 Base Address Register */
#define CS4281_DBC2    0x13C /* DMA Engine 2 Base Count Register */
#define CS4281_DCA3    0x140 /* DMA Engine 3 Current Address Register */
#define CS4281_DCC3    0x144 /* DMA Engine 3 Current Count Register */
#define CS4281_DBA3    0x148 /* DMA Engine 3 Base Address Register */
#define CS4281_DBC3    0x14C /* DMA Engine 3 Base Count Register */
#define CS4281_DMR0    0x150 /* DMA Engine 0 Mode Register */
#define CS4281_DCR0    0x154 /* DMA Engine 0 Command Register */
#define CS4281_DMR1    0x158 /* DMA Engine 1 Mode Register */
#define CS4281_DCR1    0x15C /* DMA Engine 1 Command Register */
#define CS4281_DMR2    0x160 /* DMA Engine 2 Mode Register */
#define CS4281_DCR2    0x164 /* DMA Engine 2 Command Register */
#define CS4281_DMR3    0x168 /* DMA Engine 3 Mode Register */
#define CS4281_DCR3    0x16C /* DMA Engine 3 Command Register */
/* DMRn common bit description*/
#define  DMRn_DMA      0x20000000
#define  DMRn_POLL     0x10000000
#define  DMRn_TBC      0x02000000
#define  DMRn_CBC      0x01000000
#define  DMRn_SWAPC    0x00400000
#define  DMRn_SIZE20   0x00100000
#define  DMRn_USIGN    0x00080000
#define  DMRn_BEND     0x00040000
#define  DMRn_MONO     0x00020000
#define  DMRn_SIZE8    0x00010000
#define  DMRn_FMTMSK   ( DMRn_SWAPC | DMRn_SIZE20 | DMRn_USIGN | DMRn_BEND | DMRn_MONO | DMRn_SIZE8 )
#define  DMRn_TYPE1    0x00000080
#define  DMRn_TYPE0    0x00000040
#define  DMRn_DEC      0x00000020
#define  DMRn_AUTO     0x00000010
#define  DMRn_TR_MASK  0x0000000c
#define  DMRn_TR_READ  0x00000008
#define  DMRn_TR_WRITE 0x00000004
/* DCRn common bit description*/
#define  DCRn_HTCIE    0x00020000 /* Half Terminal Count Interrupt Enable */
#define  DCRn_TCIE     0x00010000 /* Terminal Count Interrupt Enable */
#define  DCRn_MSK      0x00000001

#define CS4281_FCR0    0x180 /* FIFO Control Register 0 */
#define CS4281_FCR1    0x184 /* FIFO Control Register 1 */
#define CS4281_FCR2    0x188 /* FIFO Control Register 2 */
#define CS4281_FCR3    0x18C /* FIFO Control Register 3 */
#define  FCRn_FEN      0x80000000
#define  FCRn_DACZ     0x40000000
#define  FCRn_PSH      0x20000000

#define CS4281_FPDR0   0x190 /* FIFO Polled Data Register 0 */
#define CS4281_FPDR1   0x194 /* FIFO Polled Data Register 1 */
#define CS4281_FPDR2   0x198 /* FIFO Polled Data Register 2 */
#define CS4281_FPDR3   0x19C /* FIFO Polled Data Register 3 */
#define CS4281_FCHS    0x20C /* FIFO Channel Status */
#define CS4281_FSIC0   0x210 /* FIFO Status and Interrupt Control Register 0 */
#define CS4281_FSIC1   0x214 /* FIFO Status and Interrupt Control Register 1 */
#define CS4281_FSIC2   0x218 /* FIFO Status and Interrupt Control Register 2 */
#define CS4281_FSIC3   0x21C /* FIFO Status and Interrupt Control Register 3 */

#if 0
300h - 340h /*    PCI Configuration Space Echo, offsets 00h - 42h, RO */
#endif

#define CS4281_PMCS    0x344 /* Power Management Control/Status */
#define CS4281_CWPR    0x3E0 /* Configuration Write Protect Register */
#define CS4281_EPPMC   0x3E4 /* Extended PCI Power Management Control */
#define EPPMC_FPDN     (0x1 << 14)
#define CS4281_GPIOR   0x3E8 /* GPIO Pin Interface Register */
#define CS4281_SPMC    0x3EC /* Serial Port Power Management Control (& ASDIN2 enable) */
#define  SPMC_RSTN     0x00000001
#define  SPMC_ASYN     0x00000002
#define  SPMC_WUP1     0x00000004
#define  SPMC_WUP2     0x00000008
#define  SPMC_ASDO     0x00000080
#define  SPMC_ASDI2E   0x00000100
#define  SPMC_EESPD    0x00000200
#define  SPMC_GISPEN   0x00000400
#define  SPMC_GIPPEN   0x00008000
#define CS4281_CFLR    0x3F0 /* Configuration Load Register (EEPROM or BIOS) */
#define CS4281_IISR    0x3F4 /* ISA Interrupt Select Register */
#define CS4281_TMS     0x3F8 /* Test Register - Reserved */

#define CS4281_SSVID   0x3FC /* Subsystem ID register (read-only version at 32Ch) */

#define CS4281_CLKCR1  0x400 /* Clock Control Register 1 */
#define  CLKCR1_DLLSS0 0x00000004
#define  CLKCR1_DLLSS1 0x00000008
#define  CLKCR1_DLLP   0x00000010
#define  CLKCR1_SWCE   0x00000020
#define  CLKCR1_DLLOS  0x00000040
#define  CLKCR1_CKRA   0x00010000
#define  CLKCR1_CKRN   0x00020000
#define  CLKCR1_DLLRDY 0x01000000
#define  CLKCR1_CLKON  0x02000000

#define CS4281_FRR     0x410 /* Feature Reporting Register */
#define CS4281_SLT12O  0x41C /* Slot 12 GPIO Output Register for AC Link */

#define CS4281_SERMC   0x420 /* Serial Port Master Control Register */
#define  SERMC_MSPE    0x00000001
#define  SERMC_PTCMASK 0x0000000E
#define  SERMC_PTCAC97 0x00000002
#define  SERMC_PLB     0x00000100
#define  SERMC_PXLB    0x00000200
#define  SERMC_TCID0   0x00010000
#define  SERMC_TICD1   0x00020000
#define  SERMC_LOVF    0x00080000
#define  SERMC_SLB     0x00100000
#define  SERMC_SXLB    0x00200000
#define  SERMC_ODSEN1  0x01000000
#define  SERMC_ODSEN2  0x02000000
#define  SERMC_FCRN    0x08000000
#define CS4281_SERC1   0x428 /* Serial Port Configuration Register 1 - RO */
#define CS4281_SERC2   0x42C /* Serial Port Configuration Register 2 - RO */

#define CS4281_SLT12M  0x45C /* Slot 12 Monitor Register for Primary AC Link */

/*
 * AC97 Registers are moved to cs428xreg.h since
 * they are common for CS4280 and CS4281
 */

#define CS4281_JSPT    0x480 /* Joystick Poll/Trigger Register */
#define CS4281_JSCTL   0x484 /* Joystick Control Register */
#define CS4281_MIDCR   0x490 /* MIDI Control Register */
#define CS4281_MIDCMD  0x494 /* MIDI Command Register - WO */
#define CS4281_MIDSR   0x494 /* MIDI Status Register - RO */
#define CS4281_MIDWP   0x498 /* MIDI Write Port */
#define CS4281_MIDRP   0x49C /* MIDI Read Port - RO */
#define CS4281_AODSD1  0x4A8 /* AC `97 On-Demand Slot Disable for primary link - RO */
#define CS4281_AODSD2  0x4AC /* AC `97 On-Demand Slot Disable for secondary link - RO */
#define CS4281_CFGI    0x4B0 /* Configuration Interface Register (EEPROM interface) */
#define CS4281_SLT12M2 0x4DC /* Slot 12 Monitor Register 2 for Secondary AC Link */
#define CS4281_ACSTS2  0x4E4 /* AC 97 Status Register 2 */
#define  ACSTS2_CRDY2  0x00000001
#define  ACSTS2_BSYS2  0x00000002
#define CS4281_ACISV2  0x4F4 /* AC 97 Input Slot Valid Register 2 */
#define CS4281_ACSAD2  0x4F8 /* AC 97 Status Address Register 2 */
#define CS4281_ACSDA2  0x4FC /* AC 97 Status Data Register 2 */
#define CS4281_FMSR    0x730 /* FM Synthesis Status Register - RO */
#define CS4281_B0AP    0x730 /* FM Bank 0 Address Port - WO */
#define CS4281_FMDP    0x734 /* FM Data Port */
#define CS4281_B1AP    0x738 /* FM Bank 1 Address Port */
#define CS4281_B1DP    0x73C /* FM Bank 1 Data Port */
#define CS4281_SSPM    0x740 /* Sound System Power Management */
#define  SSPM_ALL      0x0000007E
#define  SSPM_MIXEN    0x00000040   /* p167 */
#define  SSPM_CSRCEN   0x00000020
#define  SSPM_PSRCEN   0x00000010
#define  SSPM_JSEN     0x00000008
#define  SSPM_ACLEN    0x00000004
#define  SSPM_FMEN     0x00000002

#define CS4281_DACSR   0x744 /* DAC Sample Rate - Playback SRC */
#define CS4281_ADCSR   0x748 /* ADC Sample Rate - Capture SRC */
#define CS4281_SSCR    0x74C /* Sound System Control Register */
#define  SSCR_HVS1     0x00800000  /* Hardware Volume step by 1 */
#define  SSCR_MVCS     0x00080000  /* Master Volume Codec Select */
#define  SSCR_MVLD     0x00040000  /* Master Volume Line Out Disable */
#define  SSCR_MVAD     0x00020000  /* Master Volume Alternate Out Disable */
#define  SSCR_MVMD     0x00010000  /* Master Volume Mono Out Disable */
#define  SSCR_XLPSRC   0x00000100  /* External SRC loopback mode */
#define  SSCR_LPSRC    0x00000080  /* SRC loopback mode */
#define  SSCR_CDTX     0x00000020  /* CD Transfer Data */
#define  SSCR_HVC      0x00000008  /* Hardware Volume Control Enable */
#define CS4281_FMLVC   0x754 /* FM Synthesis Left Volume Control */
#define CS4281_FMRVC   0x758 /* FM Synthesis Right Volume Control */
#define CS4281_SRCSA   0x75C /* SRC Slot Assignments */
#define CS4281_PPLVC   0x760 /* PCM Playback Left Volume Control */
#define CS4281_PPRVC   0x764 /* PCM Playback Right Volume Control */

/* Base Address 1 Direct Memory Map */

#if 0
0000h - 03FFh    FIFO RAM    Audio Sample RAM Memory Block - FIFOs
                             Logical Size: 256 x 32 bits (1 kbytes stereo double words)
0400h - D51Fh    Reserved    Reserved internal memory
D600h - FFFFh    Reserved    Reserved for future use
#endif

#define CS4281_ACCTL   0x460    /* AC97 Control Register */
#define  ACCTL_RSTN        0x00000001 /* Only for CS4280 */
#define  ACCTL_ESYN        0x00000002
#define  ACCTL_VFRM        0x00000004
#define  ACCTL_DCV         0x00000008
#define  ACCTL_CRW         0x00000010
#define  ACCTL_ASYN        0x00000020 /* Only for CS4280 */
#define  ACCTL_TC          0x00000040

#define CS4281_ACSTS   0x464    /* AC97 Status Register */
#define  ACSTS_CRDY        0x00000001
#define  ACSTS_VSTS        0x00000002

#define CS4281_ACOSV   0x468	/* AC97 Output Slot Valid Register */
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

#define CS4281_ACCAD   0x46c	/* AC97 Command Address Register */
#define CS4281_ACCDA   0x470	/* AC97 Command Data Register */

#define CS4281_ACISV   0x474	/* AC97 Input Slot Valid Register */
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
#define CS4281_ACSAD   0x478	/* AC97 Status Address Register */
#define CS4281_ACSDA   0x47c	/* AC97 Status Data Register */

/* AC97 Registers */
#define CS4281_SAVE_REG_MAX	0x10

/* for AC97_REG_POWER */
#define CS4281_POWER_DOWN_ALL	0x7f0f
