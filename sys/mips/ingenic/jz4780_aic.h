/*-
 * Copyright (c) 2016 Ruslan Bukin <br@bsdpad.com>
 * All rights reserved.
 *
 * This software was developed by SRI International and the University of
 * Cambridge Computer Laboratory under DARPA/AFRL contract FA8750-10-C-0237
 * ("CTSRD"), as part of the DARPA CRASH research programme.
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

#define	AICFR		0x00	/* AIC Configuration Register */
#define	 AICFR_TFTH_S	16	/* Transmit FIFO threshold for interrupt or DMA request. */
#define	 AICFR_TFTH_M	(0x1f << AICFR_TFTH_S)
#define	 AICFR_TFTH(x)	((x) << AICFR_TFTH_S)
#define	 AICFR_RFTH_S	24	/* Receive FIFO threshold for interrupt or DMA request. */
#define	 AICFR_RFTH_M	(0x0f << AICFR_RFTH_S)
#define	 AICFR_RFTH(x)	((x) << AICFR_RFTH_S)
#define	 AICFR_ICDC	(1 << 5) /* Internal CODEC used. */
#define	 AICFR_AUSEL	(1 << 4) /* Audio Unit Select */
#define	 AICFR_RST	(1 << 3) /* Reset AIC. */
#define	 AICFR_BCKD	(1 << 2) /* BIT_CLK Direction. */
#define	 AICFR_SYNCD	(1 << 1) /* SYNC is generated internally and driven out to the CODEC. */
#define	 AICFR_ENB	(1 << 0) /* Enable AIC Controller. */
#define	AICCR		0x04	/* AIC Common Control Register */
#define	 AICCR_TFLUSH		(1 << 8) /* Transmit FIFO Flush. */
#define	 AICCR_RFLUSH		(1 << 7) /* Receive FIFO Flush. */
#define	 AICCR_CHANNEL_S	24
#define	 AICCR_CHANNEL_M	(0x7 << AICCR_CHANNEL_S)
#define	 AICCR_CHANNEL_2	(0x1 << AICCR_CHANNEL_S) /* 2 channels, stereo */
#define	 AICCR_ISS_S		16	/* Input Sample Size. */
#define	 AICCR_ISS_M		(0x7 << AICCR_ISS_S)
#define	 AICCR_ISS_16		(0x1 << AICCR_ISS_S)
#define	 AICCR_OSS_S		19	/* Output Sample Size. */
#define	 AICCR_OSS_M		(0x7 << AICCR_OSS_S)
#define	 AICCR_OSS_16		(0x1 << AICCR_OSS_S)
#define	 AICCR_RDMS		(1 << 15) /* Receive DMA enable. */
#define	 AICCR_TDMS		(1 << 14) /* Transmit DMA enable. */
#define	 AICCR_ENLBF		(1 << 2) /* Enable AIC Loop Back Function. */
#define	 AICCR_ERPL		(1 << 1) /* Enable Playing Back function. */
#define	I2SCR		0x10	/* AIC I2S/MSB-justified Control */
#define	 I2SCR_ESCLK	(1 << 4) /* Enable SYSCLK output. */
#define	 I2SCR_AMSL	(1 << 0) /* Select MSB-Justified Operation Mode. */
#define	AICSR		0x14	/* AIC FIFO Status Register Register */
#define	I2SSR		0x1C	/* AIC I2S/MSB-justified Status Register */
#define	I2SDIV		0x30	/* AIC I2S/MSB-justified Clock Divider Register */
#define	AICDR		0x34	/* AIC FIFO Data Port Register */
#define	SPENA		0x80	/* SPDIF Enable Register */
#define	SPCTRL		0x84	/* SPDIF Control Register */
#define	SPSTATE		0x88	/* SPDIF Status Register */
#define	SPCFG1		0x8C	/* SPDIF Configure 1 Register */
#define	SPCFG2		0x90	/* SPDIF Configure 2 Register */
#define	SPFIFO		0x94	/* SPDIF FIFO Register */
