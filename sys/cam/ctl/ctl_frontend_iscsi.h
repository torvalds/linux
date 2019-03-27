/*-
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD
 *
 * Copyright (c) 2012 The FreeBSD Foundation
 * All rights reserved.
 *
 * This software was developed by Edward Tomasz Napierala under sponsorship
 * from the FreeBSD Foundation.
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

#ifndef CTL_FRONTEND_ISCSI_H
#define	CTL_FRONTEND_ISCSI_H

#define CFISCSI_TARGET_STATE_INVALID	0
#define CFISCSI_TARGET_STATE_ACTIVE	1
#define CFISCSI_TARGET_STATE_DYING	2

struct cfiscsi_target {
	TAILQ_ENTRY(cfiscsi_target)	ct_next;
	struct cfiscsi_softc		*ct_softc;
	volatile u_int			ct_refcount;
	char				ct_name[CTL_ISCSI_NAME_LEN];
	char				ct_alias[CTL_ISCSI_ALIAS_LEN];
	uint16_t			ct_tag;
	int				ct_state;
	int				ct_online;
	int				ct_target_id;
	struct ctl_port			ct_port;
};

struct cfiscsi_data_wait {
	TAILQ_ENTRY(cfiscsi_data_wait)	cdw_next;
	union ctl_io			*cdw_ctl_io;
	uint32_t			cdw_target_transfer_tag;
	uint32_t			cdw_initiator_task_tag;
	int				cdw_sg_index;
	char				*cdw_sg_addr;
	size_t				cdw_sg_len;
	uint32_t			cdw_r2t_end;
	uint32_t			cdw_datasn;
	void				*cdw_icl_prv;
};

#define CFISCSI_SESSION_STATE_INVALID		0
#define CFISCSI_SESSION_STATE_BHS		1
#define CFISCSI_SESSION_STATE_AHS		2
#define CFISCSI_SESSION_STATE_HEADER_DIGEST	3
#define CFISCSI_SESSION_STATE_DATA		4
#define CFISCSI_SESSION_STATE_DATA_DIGEST	5

struct cfiscsi_session {
	TAILQ_ENTRY(cfiscsi_session)	cs_next;
	struct mtx			cs_lock;
	struct icl_conn			*cs_conn;
	uint32_t			cs_cmdsn;
	uint32_t			cs_statsn;
	uint32_t			cs_target_transfer_tag;
	volatile u_int			cs_outstanding_ctl_pdus;
	TAILQ_HEAD(, cfiscsi_data_wait)	cs_waiting_for_data_out;
	struct cfiscsi_target		*cs_target;
	struct callout			cs_callout;
	int				cs_timeout;
	struct cv			cs_maintenance_cv;
	bool				cs_terminating;
	bool				cs_handoff_in_progress;
	bool				cs_tasks_aborted;
	int				cs_max_recv_data_segment_length;
	int				cs_max_send_data_segment_length;
	int				cs_max_burst_length;
	int				cs_first_burst_length;
	bool				cs_immediate_data;
	char				cs_initiator_name[CTL_ISCSI_NAME_LEN];
	char				cs_initiator_addr[CTL_ISCSI_ADDR_LEN];
	char				cs_initiator_alias[CTL_ISCSI_ALIAS_LEN];
	char				cs_initiator_isid[6];
	char				cs_initiator_id[CTL_ISCSI_NAME_LEN + 5 + 6 + 1];
	unsigned int			cs_id;
	int				cs_ctl_initid;
#ifdef ICL_KERNEL_PROXY
	struct sockaddr			*cs_initiator_sa;
	int				cs_portal_id;
	bool				cs_login_phase;
	bool				cs_waiting_for_ctld;
	struct cv			cs_login_cv;
	struct icl_pdu			*cs_login_pdu;
#endif
};

#ifdef ICL_KERNEL_PROXY
struct icl_listen;
#endif

struct cfiscsi_softc {
	struct mtx			lock;
	char				port_name[32];
	int				online;
	int				last_target_id;
	unsigned int			last_session_id;
	TAILQ_HEAD(, cfiscsi_target)	targets;
	TAILQ_HEAD(, cfiscsi_session)	sessions;
	struct cv			sessions_cv;
#ifdef ICL_KERNEL_PROXY
	struct icl_listen		*listener;
	struct cv			accept_cv;
#endif
};

#endif /* !CTL_FRONTEND_ISCSI_H */
