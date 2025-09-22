/*	$OpenBSD: harmonyreg.h,v 1.5 2003/06/02 19:54:29 jason Exp $	*/

/*
 * Copyright (c) 2003 Jason L. Wright (jason@thought.net)
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
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR ``AS IS'' AND ANY EXPRESS OR
 * IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED
 * WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
 * DISCLAIMED.  IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR ANY DIRECT,
 * INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES
 * (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR
 * SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT,
 * STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN
 * ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
 * POSSIBILITY OF SUCH DAMAGE.
 */

/*
 * Harmony CS4215/AD1849 register definitions based on:
 *  "712 I/O Subsystem ERS", Revision 1.1, 12 February 1993
 */

/* harmony always uses a 4K buffer */
#define	HARMONY_BUFSIZE			4096

#define	HARMONY_NREGS	0x40

#define	HARMONY_ID		0x00		/* identification */
#define	HARMONY_RESET		0x04		/* reset */
#define	HARMONY_CNTL		0x08		/* control */
#define	HARMONY_GAINCTL		0x0c		/* gain control */
#define	HARMONY_PNXTADD		0x10		/* play next address */
#define	HARMONY_PCURADD		0x14		/* play current address */
#define	HARMONY_RNXTADD		0x18		/* record next address */
#define	HARMONY_RCURADD		0x1c		/* record current address */
#define	HARMONY_DSTATUS		0x20		/* device status */
#define	HARMONY_OV		0x24		/* overrange input */
#define	HARMONY_PIO		0x28		/* general purpose i/o */
#define	HARMONY_DIAG		0x3c		/* chi diagnostic */

/* HARMONY_ID */
#define	ID_REV_MASK		0x00ff0000	/* revision mask: */
#define	ID_REV_SHIFT		16
#define	ID_REV_TS		0x00150000	/*  teleshare installed */
#define	ID_REV_NOTS		0x00140000	/*  teleshare not installed */
#define	ID_CHIID		0x0000f000	/* CHI identification */
#define	ID_CHIID_SHIFT		12

/* HARMONY_RESET */
#define	RESET_RST		0x00000001	/* reset codec */

/* HARMONY_CNTL */
#define	CNTL_C			0x80000000	/* control mode */
#define	CNTL_CODEC_REV_MASK	0x0ff00000	/* codec revision */
#define	CNTL_CODEC_REV_SHIFT	20
#define	CNTL_EXP_3		0x00020000	/* expansion bit 3 */
#define	CNTL_EXP_2		0x00010000	/* expansion bit 2 */
#define	CNTL_EXP_1		0x00008000	/* expansion bit 1 */
#define	CNTL_EXP_0		0x00004000	/* expansion bit 0 */
#define	CNTL_AC			0x00002000	/* autocalibration ad1849 */
#define	CNTL_AD			0x00001000	/* ad1849 compat? */
#define	CNTL_OLB		0x00000800	/* output level */
#define	CNTL_ITS		0x00000400	/* codec immediate tristate */
#define	CNTL_LS_MASK		0x00000300	/* loopback select: */
#define	CNTL_LS_NONE		0x00000000	/*  none */
#define	CNTL_LS_INTERNAL	0x00000100	/*  internal */
#define	CNTL_LS_DIGITAL		0x00000200	/*  digital */
#define	CNTL_LS_ANALOG		0x00000300	/*  analog */
#define	CNTL_FORMAT_MASK	0x000000c0	/* encoding format: */
#define	CNTL_FORMAT_SLINEAR16BE	0x00000000	/*  16 bit signed linear be */
#define	CNTL_FORMAT_ULAW	0x00000040	/*  8 bit ulaw */
#define	CNTL_FORMAT_ALAW	0x00000080	/*  8 bit alaw */
#define	CNTL_FORMAT_ULINEAR8	0x000000c0	/*  8 bit unsigned linear */
#define	CNTL_CHANS_MASK		0x00000020	/* number of channels: */
#define	CNTL_CHANS_MONO		0x00000000	/*  mono */
#define	CNTL_CHANS_STEREO	0x00000020	/*  stereo */
#define	CNTL_RATE_MASK		0x0000001f	/* sample rate (kHz): */
#define	CNTL_RATE_5125		0x00000010	/*  5.5125 */
#define	CNTL_RATE_6615		0x00000017	/*  6.615 */
#define	CNTL_RATE_8000		0x00000008	/*  8 */
#define	CNTL_RATE_9600		0x0000000f	/*  9.6 */
#define	CNTL_RATE_11025		0x00000011	/*  11.025 */
#define	CNTL_RATE_16000		0x00000009	/*  16 */
#define	CNTL_RATE_18900		0x00000012	/*  18.9 */
#define	CNTL_RATE_22050		0x00000013	/*  22.05 */
#define	CNTL_RATE_27428		0x0000000a	/*  27.42857 */
#define	CNTL_RATE_32000		0x0000000b	/*  32 */
#define	CNTL_RATE_33075		0x00000016	/*  33.075 */
#define	CNTL_RATE_37800		0x00000014	/*  37.8 */
#define	CNTL_RATE_44100		0x00000015	/*  44.1 */
#define	CNTL_RATE_48000		0x0000000e	/*  48 */

/* HARMONY_GAINCTL */
#define	GAINCTL_HE		0x08000000	/* headphones enable */
#define	GAINCTL_LE		0x04000000	/* line output enable */
#define	GAINCTL_SE		0x02000000	/* speaker enable */
#define	GAINCTL_IS_MASK		0x01000000	/* input select: */
#define	GAINCTL_IS_LINE		0x00000000	/*  line input */
#define	GAINCTL_IS_MICROPHONE	0x01000000	/*  microphone */
#define	GAINCTL_INPUT_LEFT_M	0x0000f000	/* left input gain */
#define	GAINCTL_INPUT_LEFT_S	12
#define	GAINCTL_INPUT_RIGHT_M	0x000f0000	/* left input gain */
#define	GAINCTL_INPUT_RIGHT_S	16
#define	GAINCTL_INPUT_BITS	4
#define	GAINCTL_MONITOR_M	0x00f00000	/* monitor gain (inverted) */
#define	GAINCTL_MONITOR_S	20
#define	GAINCTL_MONITOR_BITS	4
#define	GAINCTL_OUTPUT_LEFT_M	0x00000fc0	/* left out gain (inverted) */
#define	GAINCTL_OUTPUT_LEFT_S	6
#define	GAINCTL_OUTPUT_RIGHT_M	0x0000003f	/* right out gain (inverted) */
#define	GAINCTL_OUTPUT_RIGHT_S	0
#define	GAINCTL_OUTPUT_BITS	6

/* HARMONY_PCURADD */
#define	PCURADD_BUFMASK		(~(HARMONY_BUFSIZE - 1))

/* HARMONY_RCURADD */
#define	PCURADD_BUFMASK		(~(HARMONY_BUFSIZE - 1))

/* HARMONY_DSTATUS */
#define	DSTATUS_IE		0x80000000	/* interrupt enable */
#define	DSTATUS_PN		0x00000200	/* playback next empty */
#define	DSTATUS_PC		0x00000100	/* playback dma active */
#define	DSTATUS_RN		0x00000002	/* record next empty */
#define	DSTATUS_RC		0x00000001	/* record dma active */

/* HARMONY_OV */
#define	OV_OV			0x00000001	/* input over range */

/* HARMONY_PIO */
#define	PIO_PO			0x00000002	/* parallel output */
#define	PIO_PI			0x00000001	/* parallel input */

/* HARMONY_DIAG */
#define	DIAG_CO			0x00000001	/* sclk from codec */

/* CS4215_REV */
#define	CS4215_REV_VER		0x0f
#define	CS4215_REV_VER_C	0x00		/* CS4215 rev C */
#define	CS4215_REV_VER_D	0x01		/* CS4215 rev D */
#define	CS4215_REV_VER_E	0x02		/* CS4215 rev E/AD1849K */
