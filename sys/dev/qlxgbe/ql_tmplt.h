/*-
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD
 *
 * Copyright (c) 2013-2016 Qlogic Corporation
 * All rights reserved.
 *
 *  Redistribution and use in source and binary forms, with or without
 *  modification, are permitted provided that the following conditions
 *  are met:
 *
 *  1. Redistributions of source code must retain the above copyright
 *     notice, this list of conditions and the following disclaimer.
 *  2. Redistributions in binary form must reproduce the above copyright
 *     notice, this list of conditions and the following disclaimer in the
 *     documentation and/or other materials provided with the distribution.
 *
 *  THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
 *  AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 *  IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 *  ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT OWNER OR CONTRIBUTORS BE
 *  LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
 *  CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 *  SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
 *  INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
 *  CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 *  ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
 *  POSSIBILITY OF SUCH DAMAGE.
 *
 * $FreeBSD$
 */
/*
 * File: ql_tmplt.h
 * Author : David C Somayajulu, Qlogic Corporation, Aliso Viejo, CA 92656.
 */
#ifndef _QL_TMPLT_H_
#define _QL_TMPLT_H_


typedef struct _q8_tmplt_hdr {
	uint16_t	version;
	uint16_t	signature;
	uint16_t	size;
	uint16_t	nentries;
	uint16_t	stop_seq_off;
	uint16_t	csum;
	uint16_t	init_seq_off;
	uint16_t	start_seq_off;
} __packed q8_tmplt_hdr_t;


typedef struct _q8_ce_hdr {
	uint16_t	opcode;
	uint16_t	size;
	uint16_t	opcount;
	uint16_t	delay_to;
} __packed q8_ce_hdr_t;

/*
 * Values for opcode field in q8_ce_hdr_t
 */
#define Q8_CE_OPCODE_NOP		0x000
#define Q8_CE_OPCODE_WRITE_LIST		0x001
#define Q8_CE_OPCODE_READ_WRITE_LIST	0x002
#define Q8_CE_OPCODE_POLL_LIST		0x004
#define Q8_CE_OPCODE_POLL_WRITE_LIST	0x008
#define Q8_CE_OPCODE_READ_MODIFY_WRITE	0x010
#define Q8_CE_OPCODE_SEQ_PAUSE		0x020
#define Q8_CE_OPCODE_SEQ_END		0x040
#define Q8_CE_OPCODE_TMPLT_END		0x080
#define Q8_CE_OPCODE_POLL_RD_LIST	0x100

/*
 * structure for Q8_CE_OPCODE_WRITE_LIST
 */
typedef struct _q8_wrl_e {
	uint32_t	addr;
	uint32_t	value;
} __packed q8_wrl_e_t;

/*
 * structure for Q8_CE_OPCODE_READ_WRITE_LIST
 */
typedef struct _q8_rdwrl_e {
	uint32_t	rd_addr;
	uint32_t	wr_addr;
} __packed q8_rdwrl_e_t;

/*
 * common for
 *	Q8_CE_OPCODE_POLL_LIST
 *	Q8_CE_OPCODE_POLL_WRITE_LIST
 *	Q8_CE_OPCODE_POLL_RD_LIST
 */
typedef struct _q8_poll_hdr {
	uint32_t	tmask;
	uint32_t	tvalue;
} q8_poll_hdr_t;

/*
 * structure for Q8_CE_OPCODE_POLL_LIST
 */
typedef struct _q8_poll_e {
	uint32_t	addr;
	uint32_t	to_addr;
} q8_poll_e_t;

/*
 * structure for Q8_CE_OPCODE_POLL_WRITE_LIST
 */
typedef struct _q8_poll_wr_e {
	uint32_t	dr_addr;
	uint32_t	dr_value;
	uint32_t	ar_addr;
	uint32_t	ar_value;
} q8_poll_wr_e_t;

/*
 * structure for Q8_CE_OPCODE_POLL_RD_LIST
 */
typedef struct _q8_poll_rd_e {
	uint32_t	ar_addr;
	uint32_t	ar_value;
	uint32_t	dr_addr;
	uint32_t	rsrvd;
} q8_poll_rd_e_t;

/*
 * structure for Q8_CE_OPCODE_READ_MODIFY_WRITE
 */
typedef struct _q8_rdmwr_hdr {
	uint32_t	and_value;
	uint32_t	xor_value;
	uint32_t	or_value;
	uint8_t		shl;
	uint8_t		shr;
	uint8_t		index_a;
	uint8_t		rsrvd;
} q8_rdmwr_hdr_t;

typedef struct _q8_rdmwr_e {
	uint32_t	rd_addr;
	uint32_t	wr_addr;
} q8_rdmwr_e_t;

#endif /* #ifndef _QL_TMPLT_H_ */
