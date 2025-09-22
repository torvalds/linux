/* $OpenBSD: i2sreg.h,v 1.3 2011/06/07 16:29:51 mpi Exp $ */
/*-
 * Copyright (c) 2002 Tsubai Masanari.  All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 * 3. The name of the author may not be used to endorse or promote products
 *    derived from this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR ``AS IS'' AND ANY EXPRESS OR
 * IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES
 * OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED.
 * IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR ANY DIRECT, INDIRECT,
 * INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT
 * NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 * DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
 * THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
 * OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

/* I2S registers */
#define I2S_INT		0x00
#define I2S_FORMAT	0x10
#define I2S_FRAMECOUNT	0x40
#define I2S_FRAMEMATCH	0x50
#define I2S_WORDSIZE	0x60

/* I2S_INT register definitions */
#define I2SClockOffset		0x3c
#define I2S_INT_CLKSTOPPEND	0x01000000

#define I2S_SELECT_SPEAKER	1 << 0
#define I2S_SELECT_HEADPHONE	1 << 1
#define I2S_SELECT_LINEOUT	1 << 2

/* FCR(0x3c) bits */
#define I2S0CLKEN	0x1000
#define I2S0EN		0x2000
#define I2S1CLKEN	0x080000
#define I2S1EN		0x100000


#define CLKSRC_49MHz	0x80000000	/* Use 49152000Hz Osc. */
#define CLKSRC_45MHz	0x40000000	/* Use 45158400Hz Osc. */
#define CLKSRC_18MHz	0x00000000	/* Use 18432000Hz Osc. */
#define CLKSRC_VS	0x01fa0000	/* Magic value of xserve vu-meter */
#define MCLK_DIV	0x1f000000	/* MCLK = SRC / DIV */
#define  MCLK_DIV1	0x14000000	/*  MCLK = SRC */
#define  MCLK_DIV3	0x13000000	/*  MCLK = SRC / 3 */
#define  MCLK_DIV5	0x12000000	/*  MCLK = SRC / 5 */
#define SCLK_DIV	0x00f00000	/* SCLK = MCLK / DIV */
#define  SCLK_DIV1	0x00800000
#define  SCLK_DIV3	0x00900000
#define SCLK_MASTER	0x00080000	/* Master mode */
#define SCLK_SLAVE	0x00000000	/* Slave mode */
#define SERIAL_FORMAT	0x00070000
#define  SERIAL_SONY	0x00000000
#define  SERIAL_64x	0x00010000
#define  SERIAL_32x	0x00020000
#define  SERIAL_DAV	0x00040000
#define  SERIAL_SILICON	0x00050000
