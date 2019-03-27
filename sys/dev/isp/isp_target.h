/* $FreeBSD$ */
/*-
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD
 *
 *  Copyright (c) 1997-2009 by Matthew Jacob
 *  All rights reserved.
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
 *  THIS SOFTWARE IS PROVIDED BY AUTHOR AND CONTRIBUTORS ``AS IS'' AND
 *  ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 *  IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 *  ARE DISCLAIMED.  IN NO EVENT SHALL AUTHOR OR CONTRIBUTORS BE LIABLE
 *  FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 *  DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 *  OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 *  HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 *  LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 *  OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 *  SUCH DAMAGE.
 * 
 */
/*
 * Qlogic Target Mode Structure and Flag Definitions
 */
#ifndef	_ISP_TARGET_H
#define	_ISP_TARGET_H

/*
 * Notify structure- these are for asynchronous events that need to be sent
 * as notifications to the outer layer. It should be pretty self-explanatory.
 */
typedef enum {
	NT_UNKNOWN=0x999,
	NT_ABORT_TASK=0x1000,
	NT_ABORT_TASK_SET,
	NT_CLEAR_ACA,
	NT_CLEAR_TASK_SET,
	NT_LUN_RESET,
	NT_TARGET_RESET,
	NT_BUS_RESET,
	NT_LIP_RESET,
	NT_LINK_UP,
	NT_LINK_DOWN,
	NT_LOGOUT,
	NT_GLOBAL_LOGOUT,
	NT_CHANGED,
	NT_HBA_RESET,
	NT_QUERY_TASK_SET,
	NT_QUERY_ASYNC_EVENT,
	NT_SRR			/* Sequence Retransmission Request */
} isp_ncode_t;

typedef struct isp_notify {
	void *		nt_hba;		/* HBA tag */
	void *		nt_lreserved;	/* original IOCB pointer */
	uint64_t	nt_wwn;		/* source (wwn) */
	uint64_t	nt_tgt;		/* destination (wwn) */
	uint64_t	nt_tagval;	/* tag value */
	lun_id_t	nt_lun;		/* logical unit */
	uint32_t	nt_sid : 24;	/* source port id */
	uint32_t	nt_did : 24;	/* destination port id */
	uint16_t	nt_nphdl;	/* n-port handle */
	uint8_t		nt_channel;	/* channel id */
	uint8_t		nt_need_ack;	/* this notify needs an ACK */
	isp_ncode_t	nt_ncode;	/* action */
} isp_notify_t;

/*
 * Debug macros
 */

#define	ISP_TDQE(isp, msg, idx, arg)	\
    if (isp->isp_dblev & ISP_LOGTDEBUG2) isp_print_qentry(isp, msg, idx, arg)

/*
 * Special Constatns
 */
#define INI_ANY			((uint64_t) -1)
#define VALID_INI(ini)		(ini != INI_NONE && ini != INI_ANY)
#define LUN_ANY     		0xffff
#define TGT_ANY     		((uint64_t) -1)
#define TAG_ANY     		((uint64_t) 0)
#endif	/* _ISP_TARGET_H */
