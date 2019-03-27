/*-
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD
 *
 * Copyright (c) 1998 Joachim Kuebart <joachim.kuebart@gmx.net>
 * All rights reserved.
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

/* This supports the ENSONIQ AudioPCI board based on the ES1370. */

#ifndef _ES1370_REG_H
#define _ES1370_REG_H

#define ES1370_REG_CONTROL		0x00
#define ES1370_REG_STATUS		0x04
#define ES1370_REG_UART_DATA		0x08
#define ES1370_REG_UART_STATUS		0x09
#define ES1370_REG_UART_CONTROL		0x09
#define ES1370_REG_UART_TEST		0x0a
#define ES1370_REG_MEMPAGE		0x0c
#define ES1370_REG_CODEC		0x10
#define CODEC_INDEX_SHIFT		8
#define ES1370_REG_SERIAL_CONTROL	0x20
#define ES1370_REG_DAC1_SCOUNT		0x24
#define ES1370_REG_DAC2_SCOUNT		0x28
#define ES1370_REG_ADC_SCOUNT		0x2c

#define ES1370_REG_DAC1_FRAMEADR	0xc30
#define ES1370_REG_DAC1_FRAMECNT	0xc34
#define ES1370_REG_DAC2_FRAMEADR	0xc38
#define ES1370_REG_DAC2_FRAMECNT	0xc3c
#define ES1370_REG_ADC_FRAMEADR		0xd30
#define ES1370_REG_ADC_FRAMECNT		0xd34

#define DAC2_SRTODIV(x)	(((1411200 + (x) / 2) / (x) - 2) & 0x1fff)
#define DAC2_DIVTOSR(x)	(1411200 / ((x) + 2))

#define CTRL_ADC_STOP   0x80000000	/* 1 = ADC stopped */
#define CTRL_XCTL1      0x40000000	/* SERR pin if enabled */
#define CTRL_OPEN       0x20000000	/* no function, can be read and
					 * written */
#define CTRL_PCLKDIV    0x1fff0000	/* ADC/DAC2 clock divider */
#define CTRL_SH_PCLKDIV 16
#define CTRL_MSFMTSEL   0x00008000	/* MPEG serial data fmt: 0 = Sony, 1
					 * = I2S */
#define CTRL_M_SBB      0x00004000	/* DAC2 clock: 0 = PCLKDIV, 1 = MPEG */
#define CTRL_WTSRSEL    0x00003000	/* DAC1 clock freq: 0=5512, 1=11025,
					 * 2=22050, 3=44100 */
#define CTRL_SH_WTSRSEL 12
#define CTRL_DAC_SYNC   0x00000800	/* 1 = DAC2 runs off DAC1 clock */
#define CTRL_CCB_INTRM  0x00000400	/* 1 = CCB "voice" ints enabled */
#define CTRL_M_CB       0x00000200	/* recording source: 0 = ADC, 1 =
					 * MPEG */
#define CTRL_XCTL0      0x00000100	/* 0 = Line in, 1 = Line out */
#define CTRL_BREQ       0x00000080	/* 1 = test mode (internal mem test) */
#define CTRL_DAC1_EN    0x00000040	/* enable DAC1 */
#define CTRL_DAC2_EN    0x00000020	/* enable DAC2 */
#define CTRL_ADC_EN     0x00000010	/* enable ADC */
#define CTRL_UART_EN    0x00000008	/* enable MIDI uart */
#define CTRL_JYSTK_EN   0x00000004	/* enable Joystick port (presumably
					 * at address 0x200) */
#define CTRL_CDC_EN     0x00000002	/* enable serial (CODEC) interface */
#define CTRL_SERR_DIS   0x00000001	/* 1 = disable PCI SERR signal */

#define SCTRL_P2ENDINC    0x00380000	/* */
#define SCTRL_SH_P2ENDINC 19
#define SCTRL_P2STINC     0x00070000	/* */
#define SCTRL_SH_P2STINC  16
#define SCTRL_R1LOOPSEL   0x00008000	/* 0 = loop mode */
#define SCTRL_P2LOOPSEL   0x00004000	/* 0 = loop mode */
#define SCTRL_P1LOOPSEL   0x00002000	/* 0 = loop mode */
#define SCTRL_P2PAUSE     0x00001000	/* 1 = pause mode */
#define SCTRL_P1PAUSE     0x00000800	/* 1 = pause mode */
#define SCTRL_R1INTEN     0x00000400	/* enable interrupt */
#define SCTRL_P2INTEN     0x00000200	/* enable interrupt */
#define SCTRL_P1INTEN     0x00000100	/* enable interrupt */
#define SCTRL_P1SCTRLD    0x00000080	/* reload sample count register for
					 * DAC1 */
#define SCTRL_P2DACSEN    0x00000040	/* 1 = DAC2 play back last sample
					 * when disabled */
#define SCTRL_R1SEB       0x00000020	/* 1 = 16bit */
#define SCTRL_R1SMB       0x00000010	/* 1 = stereo */
#define SCTRL_R1FMT       0x00000030	/* format mask */
#define SCTRL_SH_R1FMT    4
#define SCTRL_P2SEB       0x00000008	/* 1 = 16bit */
#define SCTRL_P2SMB       0x00000004	/* 1 = stereo */
#define SCTRL_P2FMT       0x0000000c	/* format mask */
#define SCTRL_SH_P2FMT    2
#define SCTRL_P1SEB       0x00000002	/* 1 = 16bit */
#define SCTRL_P1SMB       0x00000001	/* 1 = stereo */
#define SCTRL_P1FMT       0x00000003	/* format mask */
#define SCTRL_SH_P1FMT    0

#define STAT_INTR       0x80000000	/* wired or of all interrupt bits */
#define STAT_CSTAT      0x00000400	/* 1 = codec busy or codec write in
					 * progress */
#define STAT_CBUSY      0x00000200	/* 1 = codec busy */
#define STAT_CWRIP      0x00000100	/* 1 = codec write in progress */
#define STAT_VC         0x00000060	/* CCB int source, 0=DAC1, 1=DAC2,
					 * 2=ADC, 3=undef */
#define STAT_SH_VC      5
#define STAT_MCCB       0x00000010	/* CCB int pending */
#define STAT_UART       0x00000008	/* UART int pending */
#define STAT_DAC1       0x00000004	/* DAC1 int pending */
#define STAT_DAC2       0x00000002	/* DAC2 int pending */
#define STAT_ADC        0x00000001	/* ADC int pending */

#define CODEC_OMIX1	0x10
#define CODEC_OMIX2	0x11
#define CODEC_LIMIX1	0x12
#define CODEC_RIMIX1	0x13
#define CODEC_LIMIX2	0x14
#define CODEC_RIMIX2	0x15
#define CODEC_RES_PD	0x16
#define CODEC_CSEL	0x17
#define CODEC_ADSEL	0x18
#define CODEC_MGAIN	0x19

/* ES1371 specific */

#define CODEC_ID_SESHIFT	10
#define CODEC_ID_SEMASK		0x1f

#define CODEC_PIRD		0x00800000  /* 0 = write AC97 register */
#define CODEC_PIADD_MASK	0x007f0000
#define CODEC_PIADD_SHIFT	16
#define CODEC_PIDAT_MASK	0x0000ffff
#define CODEC_PIDAT_SHIFT	0

#define CODEC_PORD		0x00800000  /* 0 = write AC97 register */
#define CODEC_POADD_MASK	0x007f0000
#define CODEC_POADD_SHIFT	16
#define CODEC_PODAT_MASK	0x0000ffff
#define CODEC_PODAT_SHIFT	0

#define CODEC_RDY		0x80000000  /* AC97 read data valid */
#define CODEC_WIP		0x40000000  /* AC97 write in progress */

#define ES1370_REG_CONTROL	0x00
#define ES1370_REG_SERIAL_CONTROL	0x20
#define ES1371_REG_CODEC	0x14
#define ES1371_REG_LEGACY	0x18	     /* W/R: Legacy control/status register */
#define ES1371_REG_SMPRATE	0x10	     /* W/R: Codec rate converter interface register */

#define ES1371_SYNC_RES		(1<<14)	 /* Warm AC97 reset */
#define ES1371_DIS_R1		(1<<19)	 /* record channel accumulator update disable */
#define ES1371_DIS_P2		(1<<20)	 /* playback channel 2 accumulator update disable */
#define ES1371_DIS_P1		(1<<21)	 /* playback channel 1 accumulator update disable */
#define ES1371_DIS_SRC		(1<<22)	 /* sample rate converter disable */
#define ES1371_SRC_RAM_BUSY	(1<<23)	 /* R/O: sample rate memory is busy */
#define ES1371_SRC_RAM_WE	(1<<24)	 /* R/W: read/write control for sample rate converter */
#define ES1371_SRC_RAM_ADDRO(o) (((o)&0x7f)<<25)	/* address of the sample rate converter */
#define ES1371_SRC_RAM_DATAO(o) (((o)&0xffff)<<0)	/* current value of the sample rate converter */
#define ES1371_SRC_RAM_DATAI(i) (((i)>>0)&0xffff)	/* current value of the sample rate converter */

/*
 * S/PDIF specific
 */

/* Use ES1370_REG_CONTROL */
#define RECEN_B			0x08000000	/* Used to control mixing of analog with digital data */
#define SPDIFEN_B		0x04000000	/* Reset to switch digital output mux to "THRU" mode */
/* Use ES1370_REG_STATUS */
#define ENABLE_SPDIF		0x00040000	/* Used to enable the S/PDIF circuitry */
#define TEST_SPDIF		0x00020000	/* Used to put the S/PDIF module in "test mode" */

/*
 *  Sample rate converter addresses
 */

#define ES_SMPREG_DAC1		0x70
#define ES_SMPREG_DAC2		0x74
#define ES_SMPREG_ADC		0x78
#define ES_SMPREG_TRUNC_N	0x00
#define ES_SMPREG_INT_REGS	0x01
#define ES_SMPREG_VFREQ_FRAC	0x03
#define ES_SMPREG_VOL_ADC	0x6c
#define ES_SMPREG_VOL_DAC1	0x7c
#define ES_SMPREG_VOL_DAC2	0x7e

#endif
