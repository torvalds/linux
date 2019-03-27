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

#ifndef ISCSI_H
#define	ISCSI_H

struct iscsi_softc;
struct icl_conn;

#define	ISCSI_NAME_LEN		224	/* 223 bytes, by RFC 3720, + '\0' */
#define	ISCSI_ADDR_LEN		47	/* INET6_ADDRSTRLEN + '\0' */
#define	ISCSI_SECRET_LEN	17	/* 16 + '\0' */

struct iscsi_outstanding {
	TAILQ_ENTRY(iscsi_outstanding)	io_next;
	union ccb			*io_ccb;
	size_t				io_received;
	uint32_t			io_initiator_task_tag;
	uint32_t			io_datasn;
	void				*io_icl_prv;
};

struct iscsi_session {
	TAILQ_ENTRY(iscsi_session)	is_next;

	struct icl_conn			*is_conn;
	struct mtx			is_lock;

	uint32_t			is_statsn;
	uint32_t			is_cmdsn;
	uint32_t			is_expcmdsn;
	uint32_t			is_maxcmdsn;
	uint32_t			is_initiator_task_tag;
	int				is_header_digest;
	int				is_data_digest;
	int				is_initial_r2t;
	int				is_max_burst_length;
	int				is_first_burst_length;
	uint8_t				is_isid[6];
	uint16_t			is_tsih;
	bool				is_immediate_data;
	int				is_max_recv_data_segment_length;
	int				is_max_send_data_segment_length;
	char				is_target_alias[ISCSI_ALIAS_LEN];

	TAILQ_HEAD(, iscsi_outstanding)	is_outstanding;
	STAILQ_HEAD(, icl_pdu)		is_postponed;

	struct callout			is_callout;
	unsigned int			is_timeout;

	/*
	 * XXX: This could be rewritten using a single variable,
	 * 	but somehow it results in uglier code. 
	 */
	/*
	 * We're waiting for iscsid(8); after iscsid_timeout
	 * expires, kernel will wake up an iscsid(8) to handle
	 * the session.
	 */
	bool				is_waiting_for_iscsid;

	/*
	 * Some iscsid(8) instance is handling the session;
	 * after login_timeout expires, kernel will wake up
	 * another iscsid(8) to handle the session.
	 */
	bool				is_login_phase;

	/*
	 * We're in the process of removing the iSCSI session.
	 */
	bool				is_terminating;

	/*
	 * We're waiting for the maintenance thread to do some
	 * reconnection tasks.
	 */
	bool				is_reconnecting;

	bool				is_connected;

	struct cam_devq			*is_devq;
	struct cam_sim			*is_sim;
	struct cam_path			*is_path;
	struct cv			is_maintenance_cv;
	struct iscsi_softc		*is_softc;
	unsigned int			is_id;
	struct iscsi_session_conf	is_conf;
	bool				is_simq_frozen;

	char				is_reason[ISCSI_REASON_LEN];

#ifdef ICL_KERNEL_PROXY
	struct cv			is_login_cv;
	struct icl_pdu			*is_login_pdu;
#endif
};

struct iscsi_softc {
	device_t			sc_dev;
	struct sx			sc_lock;
	struct cdev			*sc_cdev;
	TAILQ_HEAD(, iscsi_session)	sc_sessions;
	struct cv			sc_cv;
	unsigned int			sc_last_session_id;
	eventhandler_tag		sc_shutdown_pre_eh;
	eventhandler_tag		sc_shutdown_post_eh;
};

#endif /* !ISCSI_H */
