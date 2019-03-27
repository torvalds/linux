/*-
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD
 *
 * Copyright (c) 2006-2007 Daniel Roethlisberger <daniel@roe.ch>
 * Copyright (c) 2000-2004 OMNIKEY GmbH (www.omnikey.com)
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

/* I/O port registers */
#define REG_OFFSET_DTR			0	/* data transfer register */
#define REG_OFFSET_BSR			1	/* buffer status register */
#define REG_OFFSET_SCR			2	/* sync control register  */

/* buffer status register flags */
#define BSR_BULK_OUT_FULL		0x01
#define BSR_BULK_IN_FULL		0x02

/* sync control register flags */
#define SCR_POWER_DOWN			0x01
#define SCR_PULSE_INTERRUPT		0x02
#define SCR_HOST_TO_READER_DONE		0x04
#define SCR_READER_TO_HOST_DONE		0x08
#define SCR_ACK_NOTIFY			0x10
#define SCR_EN_NOTIFY			0x20
#define SCR_ABORT			0x40
#define SCR_HOST_TO_READER_START	0x80

/* CCID commands */
#define CMD_PC_TO_RDR_SETPARAMETERS	0x61
#define CMD_PC_TO_RDR_ICCPOWERON	0x62
#define CMD_PC_TO_RDR_ICCPOWEROFF	0x63
#define CMD_PC_TO_RDR_GETSLOTSTATUS	0x65
#define CMD_PC_TO_RDR_SECURE		0x69
#define CMD_PC_TO_RDR_ESCAPE		0x6B
#define CMD_PC_TO_RDR_GETPARAMETERS	0x6C
#define CMD_PC_TO_RDR_RESETPARAMETERS	0x6D
#define CMD_PC_TO_RDR_ICCCLOCK		0x6E
#define CMD_PC_TO_RDR_XFRBLOCK		0x6F
#define CMD_PC_TO_RDR_TEST_SECURE	0x74
#define CMD_PC_TO_RDR_OK_SECURE		0x89
#define CMD_RDR_TO_PC_DATABLOCK		0x80
#define CMD_RDR_TO_PC_SLOTSTATUS	0x81
#define CMD_RDR_TO_PC_PARAMETERS	0x82
#define CMD_RDR_TO_PC_ESCAPE		0x83
#define CMD_RDR_TO_PC_OK_SECURE		0x89
