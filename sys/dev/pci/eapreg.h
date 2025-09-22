/*	$OpenBSD: eapreg.h,v 1.4 2012/03/30 08:18:19 ratchov Exp $ */
/*	$NetBSD: eapreg.h,v 1.10 2005/02/13 23:58:38 fredb Exp $	*/

/*
 * Copyright (c) 1998, 1999 The NetBSD Foundation, Inc.
 * All rights reserved.
 *
 * This code is derived from software contributed to The NetBSD Foundation
 * by Lennart Augustsson <augustss@netbsd.org> and Charles M. Hannum.
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
 * THIS SOFTWARE IS PROVIDED BY THE NETBSD FOUNDATION, INC. AND CONTRIBUTORS
 * ``AS IS'' AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED
 * TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR
 * PURPOSE ARE DISCLAIMED.  IN NO EVENT SHALL THE FOUNDATION OR CONTRIBUTORS
 * BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
 * CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 * SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
 * INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
 * CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 * ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
 * POSSIBILITY OF SUCH DAMAGE.
 */

/*
 * ES1370/ES1371/ES1373 registers
 */

#define EAP_ICSC		0x00    /* interrupt / chip select control */
#define  EAP_SERR_DISABLE	0x00000001
#define  EAP_CDC_EN		0x00000002
#define  EAP_JYSTK_EN		0x00000004
#define  EAP_UART_EN		0x00000008
#define  EAP_ADC_EN		0x00000010
#define  EAP_DAC2_EN		0x00000020
#define  EAP_DAC1_EN		0x00000040
#define  EAP_BREQ		0x00000080
#define  EAP_XTCL0		0x00000100
#define  EAP_M_CB		0x00000200
#define  EAP_CCB_INTRM		0x00000400
#define  EAP_DAC_SYNC		0x00000800
#define  EAP_WTSRSEL		0x00003000
#define   EAP_WTSRSEL_5		0x00000000
#define   EAP_WTSRSEL_11	0x00001000
#define   EAP_WTSRSEL_22	0x00002000
#define   EAP_WTSRSEL_44	0x00003000
#define  EAP_M_SBB		0x00004000
#define  E1371_SYNC_RES		0x00004000
#define  EAP_MSFMTSEL		0x00008000
#define  EAP_SET_PCLKDIV(n)	(((n)&0x1fff)<<16)
#define  EAP_GET_PCLKDIV(n)	(((n)>>16)&0x1fff)
#define  EAP_PCLKBITS		0x1fff0000
#define  EAP_XTCL1		0x40000000
#define  EAP_ADC_STOP		0x80000000

#define EAP_ICSS		0x04	/* interrupt / chip select status */
					/* on the 5880 control / status */
#define  EAP_I_ADC		0x00000001
#define  EAP_I_DAC2		0x00000002
#define  EAP_I_DAC1		0x00000004
#define  EAP_I_UART		0x00000008
#define  EAP_I_MCCB		0x00000010
#define  EAP_VC			0x00000060
#define  EAP_CWRIP		0x00000100
#define  EAP_CBUSY		0x00000200
#define  EAP_CSTAT		0x00000400
#define  EAP_CT5880_AC97_RESET	0x20000000
#define  EAP_INTR		0x80000000

#define EAP_UART_DATA		0x08
#define EAP_UART_STATUS		0x09
#define  EAP_US_RXRDY		0x01
#define  EAP_US_TXRDY		0x02
#define  EAP_US_TXINT		0x04
#define  EAP_US_RXINT		0x80
#define EAP_UART_CONTROL	0x09
#define  EAP_UC_CNTRL		0x03
#define  EAP_UC_TXINTEN		0x20
#define  EAP_UC_RXINTEN		0x80
#define EAP_MEMPAGE		0x0c
#define EAP_CODEC		0x10
#define  EAP_SET_CODEC(a,d)	(((a)<<8) | (d))

/*
 * ES1371/ES1373 registers
 */

#define E1371_CODEC		0x14
#define  E1371_CODEC_VALID      0x80000000
#define  E1371_CODEC_WIP	0x40000000
#define  E1371_CODEC_READ       0x00800000
#define  E1371_SET_CODEC(a,d)	(((a)<<16) | (d))

#define E1371_SRC		0x10
#define  E1371_SRC_RAMWE	0x01000000
#define  E1371_SRC_RBUSY	0x00800000
#define  E1371_SRC_DISABLE	0x00400000
#define  E1371_SRC_DISP1	0x00200000
#define  E1371_SRC_DISP2        0x00100000
#define  E1371_SRC_DISREC       0x00080000
#define  E1371_SRC_ADDR(a)	((a)<<25)
#define  E1371_SRC_DATA(d)	(d)
#define  E1371_SRC_DATAMASK	0x0000ffff
#define  E1371_SRC_CTLMASK	(E1371_SRC_DISABLE | E1371_SRC_DISP1 | \
				 E1371_SRC_DISP2 | E1371_SRC_DISREC)
#define  E1371_SRC_STATE_MASK   0x00870000
#define  E1371_SRC_STATE_OK     0x00010000

#define E1371_LEGACY		0x18

/*
 * ES1371/ES1373 sample rate converter registers
 */

#define ESRC_ADC		0x78
#define ESRC_DAC1		0x74
#define ESRC_DAC2		0x70
#define ESRC_ADC_VOLL		0x6c
#define ESRC_ADC_VOLR		0x6d
#define ESRC_DAC1_VOLL		0x7c
#define ESRC_DAC1_VOLR		0x7d
#define ESRC_DAC2_VOLL		0x7e
#define ESRC_DAC2_VOLR		0x7f
#define  ESRC_TRUNC_N		0x00
#define  ESRC_IREGS		0x01
#define  ESRC_ACF		0x02
#define  ESRC_VFF		0x03
#define ESRC_SET_TRUNC(n)	((n)<<9)
#define ESRC_SET_N(n)		((n)<<4)
#define ESRC_SMF		0x8000
#define ESRC_SET_VFI(n)		((n)<<10)
#define ESRC_SET_ACI(n)		(n)
#define ESRC_SET_ADC_VOL(n)	((n)<<8)
#define ESRC_SET_DAC_VOLI(n)	((n)<<12)
#define ESRC_SET_DAC_VOLF(n)	(n)
#define  SRC_MAGIC ((1<15)|(1<<13)|(1<<11)|(1<<9))

#define EAP_SIC			0x20
#define  EAP_P1_S_MB		0x00000001
#define  EAP_P1_S_EB		0x00000002
#define  EAP_P2_S_MB		0x00000004
#define  EAP_P2_S_EB		0x00000008
#define  EAP_R1_S_MB		0x00000010
#define  EAP_R1_S_EB		0x00000020
#define  EAP_P2_DAC_SEN		0x00000040
#define  EAP_P1_SCT_RLD		0x00000080
#define  EAP_P1_INTR_EN		0x00000100
#define  EAP_P2_INTR_EN		0x00000200
#define  EAP_R1_INTR_EN		0x00000400
#define  EAP_P1_PAUSE		0x00000800
#define  EAP_P2_PAUSE		0x00001000
#define  EAP_P1_LOOP_SEL	0x00002000
#define  EAP_P2_LOOP_SEL	0x00004000
#define  EAP_R1_LOOP_SEL	0x00008000
#define  EAP_SET_P2_ST_INC(i)	((i) << 16)
#define  EAP_SET_P2_END_INC(i)	((i) << 19)
#define  EAP_INC_BITS		0x003f0000

#define EAP_DAC1_CSR		0x24
#define EAP_DAC2_CSR		0x28
#define EAP_ADC_CSR		0x2c
#define  EAP_GET_CURRSAMP(r)	((r) >> 16)

#define EAP_DAC_PAGE		0xc
#define EAP_ADC_PAGE		0xd
#define EAP_UART_PAGE1		0xe
#define EAP_UART_PAGE2		0xf

#define EAP_DAC1_ADDR		0x30
#define EAP_DAC1_SIZE		0x34
#define EAP_DAC2_ADDR		0x38
#define EAP_DAC2_SIZE		0x3c
#define EAP_ADC_ADDR		0x30
#define EAP_ADC_SIZE		0x34
#define  EAP_SET_SIZE(c,s)	(((c)<<16) | (s))

#define EAP_READ_TIMEOUT	5000
#define EAP_WRITE_TIMEOUT	5000


#define EAP_XTAL_FREQ		1411200		/* 22.5792 / 16 MHz */

/* AK4531 registers */
#define AK_MASTER_L		0x00
#define AK_MASTER_R		0x01
#define AK_VOICE_L		0x02
#define AK_VOICE_R		0x03
#define AK_FM_L			0x04
#define AK_FM_R			0x05
#define AK_CD_L			0x06
#define AK_CD_R			0x07
#define AK_LINE_L		0x08
#define AK_LINE_R		0x09
#define AK_AUX_L		0x0a
#define AK_AUX_R		0x0b
#define AK_MONO1		0x0c
#define AK_MONO2		0x0d
#define AK_MIC			0x0e
#define AK_MONO			0x0f
#define AK_OUT_MIXER1		0x10
#define  AK_M_FM_L		0x40
#define  AK_M_FM_R		0x20
#define  AK_M_LINE_L		0x10
#define  AK_M_LINE_R		0x08
#define  AK_M_CD_L		0x04
#define  AK_M_CD_R		0x02
#define  AK_M_MIC		0x01
#define AK_OUT_MIXER2		0x11
#define  AK_M_AUX_L		0x20
#define  AK_M_AUX_R		0x10
#define  AK_M_VOICE_L		0x08
#define  AK_M_VOICE_R		0x04
#define  AK_M_MONO2		0x02
#define  AK_M_MONO1		0x01
#define AK_IN_MIXER1_L		0x12
#define AK_IN_MIXER1_R		0x13
#define AK_IN_MIXER2_L		0x14
#define AK_IN_MIXER2_R		0x15
#define  AK_M_TMIC		0x80
#define  AK_M_TMONO1		0x40
#define  AK_M_TMONO2		0x20
#define  AK_M2_AUX_L		0x10
#define  AK_M2_AUX_R		0x08
#define  AK_M_VOICE		0x04
#define  AK_M2_MONO2		0x02
#define  AK_M2_MONO1		0x01
#define AK_RESET		0x16
#define  AK_PD			0x02
#define  AK_NRST		0x01
#define AK_CS			0x17
#define AK_ADSEL		0x18
#define AK_MGAIN		0x19
#define AK_NPORTS               0x20

/* Not sensical for AC97? */
#define VOL_TO_ATT5(v) (0x1f - ((v) >> 3))
#define VOL_TO_GAIN5(v) VOL_TO_ATT5(v)
#define ATT5_TO_VOL(v) ((0x1f - (v)) << 3)
#define GAIN5_TO_VOL(v) ATT5_TO_VOL(v)
#define VOL_0DB 200

/* Futzable parms */
#define EAP_MASTER_VOL		0
#define EAP_VOICE_VOL		1
#define EAP_FM_VOL		2 
#define EAP_VIDEO_VOL		2	/* ES1371 */
#define EAP_CD_VOL		3
#define EAP_LINE_VOL		4
#define EAP_AUX_VOL		5
#define EAP_MIC_VOL		6
#define	EAP_RECORD_SOURCE 	7
#define EAP_INPUT_SOURCE	8
#define	EAP_MIC_PREAMP		9  
#define EAP_OUTPUT_CLASS	10
#define EAP_RECORD_CLASS	11
#define EAP_INPUT_CLASS		12

#define EAP_EV1938_A  0x00
#define EAP_ES1371_A  0x02
#define EAP_CT5880_C  0x02
#define EAP_CT5880_D  0x03
#define EAP_ES1373_A  0x04
#define EAP_ES1373_B  0x06
#define EAP_CT5880_A  0x07
#define EAP_ES1373_8  0x08
#define EAP_ES1371_B  0x09
