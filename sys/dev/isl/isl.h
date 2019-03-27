/*-
 * Copyright (c) 2015 Michael Gmelin <freebsd@grem.de>
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

#ifndef _ISL_H_
#define _ISL_H_

/* Command register 1  (bits 7-5) */
#define REG_CMD1		0x00
#define CMD1_MASK_POWER_DOWN	0x00      /* 00000000 */
#define CMD1_MASK_ALS_ONCE	0x01 << 5 /* 00100000 */
#define CMD1_MASK_IR_ONCE	0x02 << 5 /* 01000000 */
#define CMD1_MASK_PROX_ONCE	0x03 << 5 /* 01100000 */
/* RESERVED */                            /* 10000000 */
#define CMD1_MASK_ALS_CONT	0x05 << 5 /* 10100000 */
#define CMD1_MASK_IR_CONT	0x06 << 5 /* 11000000 */
#define CMD1_MASK_PROX_CONT	0x07 << 5 /* 11100000 */

/* Command register 2 (bits) */
#define REG_CMD2		0x01

/* data registers */
#define REG_DATA1		0x02
#define REG_DATA2		0x03
#define CMD2_SHIFT_RANGE	0x00
#define CMD2_MASK_RANGE		(0x03 << CMD2_SHIFT_RANGE)
#define CMD2_SHIFT_RESOLUTION	0x02
#define CMD2_MASK_RESOLUTION	(0x03 << CMD2_SHIFT_RESOLUTION)

/* Interrupt registers */
#define REG_INT_LO_LSB		0x04
#define REG_INT_LO_MSB		0x05
#define REG_INT_HI_LSB		0x06
#define REG_INT_HI_MSB		0x07

/* Test register (should hold 0x00 at all times */
#define REG_TEST		0x08

#endif
