/*-
 * Copyright (c) 2012, 2015 Chelsio Communications, Inc.
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
 *
 */

#ifndef __CXGBEI_OFLD_H__
#define __CXGBEI_OFLD_H__

#include <dev/iscsi/icl.h>

enum {
	CWT_SLEEPING	= 1,
	CWT_RUNNING	= 2,
	CWT_STOP	= 3,
	CWT_STOPPED	= 4,
};

struct cxgbei_worker_thread_softc {
	struct mtx	cwt_lock;
	struct cv	cwt_cv;
	volatile int	cwt_state;

	TAILQ_HEAD(, icl_cxgbei_conn) rx_head;
} __aligned(CACHE_LINE_SIZE);

#define CXGBEI_CONN_SIGNATURE 0x56788765

enum {
	RXF_ACTIVE	= 1 << 0,	/* In the worker thread's queue */
};

struct icl_cxgbei_conn {
	struct icl_conn ic;

	/* cxgbei specific stuff goes here. */
	uint32_t icc_signature;
	int ulp_submode;
	struct adapter *sc;
	struct toepcb *toep;

	/* Receive related. */
	u_int rx_flags;				/* protected by so_rcv lock */
	u_int cwt;
	STAILQ_HEAD(, icl_pdu) rcvd_pdus;	/* protected by so_rcv lock */
	TAILQ_ENTRY(icl_cxgbei_conn) rx_link;	/* protected by cwt lock */
};

static inline struct icl_cxgbei_conn *
ic_to_icc(struct icl_conn *ic)
{

	return (__containerof(ic, struct icl_cxgbei_conn, ic));
}

/* PDU flags and signature. */
enum {
	ICPF_RX_HDR	= 1 << 0, /* PDU header received. */
	ICPF_RX_FLBUF	= 1 << 1, /* PDU payload received in a freelist. */
	ICPF_RX_DDP	= 1 << 2, /* PDU payload DDP'd. */
	ICPF_RX_STATUS	= 1 << 3, /* Rx status received. */
	ICPF_HCRC_ERR	= 1 << 4, /* Header digest error. */
	ICPF_DCRC_ERR	= 1 << 5, /* Data digest error. */
	ICPF_PAD_ERR	= 1 << 6, /* Padding error. */

	CXGBEI_PDU_SIGNATURE = 0x12344321
};

struct icl_cxgbei_pdu {
	struct icl_pdu ip;

	/* cxgbei specific stuff goes here. */
	uint32_t icp_signature;
	uint32_t icp_seq;	/* For debug only */
	u_int icp_flags;
};

static inline struct icl_cxgbei_pdu *
ip_to_icp(struct icl_pdu *ip)
{

	return (__containerof(ip, struct icl_cxgbei_pdu, ip));
}

struct cxgbei_data {
	u_int max_tx_pdu_len;
	u_int max_rx_pdu_len;

	u_int ddp_threshold;
	struct ppod_region pr;

	struct sysctl_ctx_list ctx;	/* from uld_activate to deactivate */
	counter_u64_t ddp_setup_ok;
	counter_u64_t ddp_setup_error;
	counter_u64_t ddp_bytes;
	counter_u64_t ddp_pdus;
	counter_u64_t fl_bytes;
	counter_u64_t fl_pdus;
};

/* cxgbei.c */
u_int cxgbei_select_worker_thread(struct icl_cxgbei_conn *);

/* icl_cxgbei.c */
int icl_cxgbei_mod_load(void);
int icl_cxgbei_mod_unload(void);
#endif
