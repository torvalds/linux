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

#ifndef ICL_H
#define	ICL_H

/*
 * iSCSI Common Layer.  It's used by both the initiator and target to send
 * and receive iSCSI PDUs.
 */

#include <sys/types.h>
#include <sys/kobj.h>
#include <sys/condvar.h>
#include <sys/sysctl.h>

SYSCTL_DECL(_kern_icl);

extern int icl_debug;

#define	ICL_DEBUG(X, ...)						\
	do {								\
		if (icl_debug > 1)					\
			printf("%s: " X "\n", __func__, ## __VA_ARGS__);\
	} while (0)

#define	ICL_WARN(X, ...)						\
	do {								\
		if (icl_debug > 0) {					\
			printf("WARNING: %s: " X "\n",			\
			    __func__, ## __VA_ARGS__);			\
		}							\
	} while (0)

struct icl_conn;
struct ccb_scsiio;
union ctl_io;

struct icl_pdu {
	STAILQ_ENTRY(icl_pdu)	ip_next;
	struct icl_conn		*ip_conn;
	struct iscsi_bhs	*ip_bhs;
	struct mbuf		*ip_bhs_mbuf;
	size_t			ip_ahs_len;
	struct mbuf		*ip_ahs_mbuf;
	size_t			ip_data_len;
	struct mbuf		*ip_data_mbuf;

	/*
	 * User (initiator or provider) private fields.
	 */
	uint32_t		ip_prv0;
	uint32_t		ip_prv1;
	uint32_t		ip_prv2;
};

#define ICL_CONN_STATE_INVALID		0
#define ICL_CONN_STATE_BHS		1
#define ICL_CONN_STATE_AHS		2
#define ICL_CONN_STATE_HEADER_DIGEST	3
#define ICL_CONN_STATE_DATA		4
#define ICL_CONN_STATE_DATA_DIGEST	5

#define	ICL_MAX_DATA_SEGMENT_LENGTH	(128 * 1024)

struct icl_conn {
	KOBJ_FIELDS;
	struct mtx		*ic_lock;
	struct socket		*ic_socket;
#ifdef DIAGNOSTIC
	volatile u_int		ic_outstanding_pdus;
#endif
	STAILQ_HEAD(, icl_pdu)	ic_to_send;
	bool			ic_check_send_space;
	size_t			ic_receive_len;
	int			ic_receive_state;
	struct icl_pdu		*ic_receive_pdu;
	struct cv		ic_send_cv;
	struct cv		ic_receive_cv;
	bool			ic_header_crc32c;
	bool			ic_data_crc32c;
	bool			ic_send_running;
	bool			ic_receive_running;
	size_t			ic_max_data_segment_length;
	size_t			ic_maxtags;
	bool			ic_disconnecting;
	bool			ic_iser;
	bool			ic_unmapped;
	const char		*ic_name;
	const char		*ic_offload;

	void			(*ic_receive)(struct icl_pdu *);
	void			(*ic_error)(struct icl_conn *);

	/*
	 * User (initiator or provider) private fields.
	 */
	void			*ic_prv0;
};

struct icl_drv_limits {
	int idl_max_recv_data_segment_length;
	int idl_max_send_data_segment_length;
	int idl_max_burst_length;
	int idl_first_burst_length;
	int spare[4];
};

struct icl_conn	*icl_new_conn(const char *offload, bool iser, const char *name,
		    struct mtx *lock);
int		icl_limits(const char *offload, bool iser,
		    struct icl_drv_limits *idl);
int		icl_register(const char *offload, bool iser, int priority,
		    int (*limits)(struct icl_drv_limits *),
		    struct icl_conn *(*new_conn)(const char *, struct mtx *));
int		icl_unregister(const char *offload, bool rdma);

#ifdef ICL_KERNEL_PROXY

struct sockaddr;
struct icl_listen;

/*
 * Target part.
 */
struct icl_listen	*icl_listen_new(void (*accept_cb)(struct socket *,
			    struct sockaddr *, int));
void			icl_listen_free(struct icl_listen *il);
int			icl_listen_add(struct icl_listen *il, bool rdma,
			    int domain, int socktype, int protocol,
			    struct sockaddr *sa, int portal_id);
int			icl_listen_remove(struct icl_listen *il, struct sockaddr *sa);

/*
 * Those two are not a public API; only to be used between icl_soft.c
 * and icl_soft_proxy.c.
 */
int			icl_soft_handoff_sock(struct icl_conn *ic, struct socket *so);
int			icl_soft_proxy_connect(struct icl_conn *ic, int domain,
			    int socktype, int protocol, struct sockaddr *from_sa,
			    struct sockaddr *to_sa);
#endif /* ICL_KERNEL_PROXY */
#endif /* !ICL_H */
