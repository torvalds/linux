/*	$OpenBSD: gscsioreg.h,v 1.3 2009/06/02 12:12:35 grange Exp $	*/
/*
 * Copyright (c) 2004 Alexander Yurchenko <grange@openbsd.org>
 *
 * Permission to use, copy, modify, and distribute this software for any
 * purpose with or without fee is hereby granted, provided that the above
 * copyright notice and this permission notice appear in all copies.
 *
 * THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
 * WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF
 * MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR
 * ANY SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
 * WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN
 * ACTION OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF
 * OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
 */

/*
 * National Semiconductor Geode SC1100 Super I/O register definitions.
 */

#define GSCSIO_IOSIZE	2	/* I/O space size */

/* Index-data register pair */
#define GSCSIO_IDX	0x0	/* index register */
#define GSCSIO_DAT	0x1	/* data register */

/* SIO control and configuration registers */
#define GSCSIO_LDN	0x07	/* logical device number */
#define GSCSIO_LDN_ACB1		0x05	/* ACCESS.bus 1 */
#define GSCSIO_LDN_ACB2		0x06	/* ACCESS.bus 2 */
#define GSCSIO_LDN_LAST		0x08	/* last logical device number */
#define GSCSIO_ID	0x20	/* SIO ID */
#define GSCSIO_ID_SC1100	0xf5	/* Geode SC1100 ID */
#define GSCSIO_CFG1	0x21	/* configuration 1 */
#define GSCSIO_CFG2	0x22	/* configuration 2 */
#define GSCSIO_REV	0x27	/* revision ID */

/* Logical device control and configuration registers */
#define GSCSIO_ACT	0x30	/* logical device activation control */
#define GSCSIO_ACT_EN		0x01	/* enabled */
#define GSCSIO_IO0_MSB	0x60	/* I/O space 0 base bits [15:8] */
#define GSCSIO_IO0_LSB	0x61	/* I/O space 0 base bits [7:0] */
#define GSCSIO_IO1_MSB	0x62	/* I/O space 1 base bits [15:8] */
#define GSCSIO_IO1_LSB	0x63	/* I/O space 1 base bits [7:0] */
#define GSCSIO_INUM	0x70	/* interrupt number */
#define GSCSIO_ITYPE	0x71	/* interrupt type */
#define GSCSIO_DMA0	0x74	/* DMA channel 0 */
#define GSCSIO_DMA1	0x75	/* DMA channel 1 */

/* ACB (ACCESS.bus) logical device registers */
#define GSCSIO_ACB_SDA	0x00	/* serial data */
#define GSCSIO_ACB_ST	0x01	/* status */
#define GSCSIO_ACB_ST_XMIT	(1 << 0)	/* transmit mode active */
#define GSCSIO_ACB_ST_MASTER	(1 << 1)	/* master mode active */
#define GSCSIO_ACB_ST_NMATCH	(1 << 2)	/* new match */
#define GSCSIO_ACB_ST_STASTR	(1 << 3)	/* stall after start */
#define GSCSIO_ACB_ST_NEGACK	(1 << 4)	/* negative ack */
#define GSCSIO_ACB_ST_BER	(1 << 5)	/* bus error */
#define GSCSIO_ACB_ST_SDAST	(1 << 6)	/* wait or hold data */
#define GSCSIO_ACB_ST_SLVSTP	(1 << 7)	/* slave stop */
#define GSCSIO_ACB_CST	0x02	/* control status */
#define GSCSIO_ACB_CST_BUSY	(1 << 0)	/* busy */
#define GSCSIO_ACB_CST_BB	(1 << 1)	/* bus busy */
#define GSCSIO_ACB_CST_MATCH	(1 << 2)	/* match address */
#define GSCSIO_ACB_CST_GCMTCH	(1 << 3)	/* global call match */
#define GSCSIO_ACB_CST_TSDA	(1 << 4)	/* test ABD line */
#define GSCSIO_ACB_CST_TGABC	(1 << 5)	/* toggle ABC line */
#define GSCSIO_ACB_CTL1	0x03	/* control 1 */
#define GSCSIO_ACB_CTL1_START	(1 << 0)	/* start condition */
#define GSCSIO_ACB_CTL1_STOP	(1 << 1)	/* stop condition */
#define GSCSIO_ACB_CTL1_INTEN	(1 << 2)	/* interrupt enabled */
#define GSCSIO_ACB_CTL1_ACK	(1 << 4)	/* acknowledge */
#define GSCSIO_ACB_CTL1_GCMEN	(1 << 5)	/* global call match enable */
#define GSCSIO_ACB_CTL1_NMINTE	(1 << 6)	/* new match intr enable */
#define GSCSIO_ACB_CTL1_STASTRE	(1 << 7)	/* stall after start enable */
#define GSCSIO_ACB_ADDR	0x04	/* own address */
#define GSCSIO_ACB_ADDR_MASK	0x7f		/* address mask */
#define GSCSIO_ACB_ADDR_SAEN	(1 << 7)	/* slave address enable */
#define GSCSIO_ACB_CTL2	0x05	/* control 2 */
#define GSCSIO_ACB_CTL2_EN	(1 << 0)	/* ACB enabled */
#define GSCSIO_ACB_CTL2_FREQ_SHIFT	1	/* ACB frequency shift */
#define GSCSIO_ACB_CTL2_FREQ_MASK	0x7f	/* ACB frequency mask */

#define GSCSIO_ACB_FREQ		0x3c	/* standard I2C frequency 100kHz */
