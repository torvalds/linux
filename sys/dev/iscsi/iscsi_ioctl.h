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

#ifndef ISCSI_IOCTL_H
#define	ISCSI_IOCTL_H

#ifdef ICL_KERNEL_PROXY
#include <sys/socket.h>
#endif

#define	ISCSI_PATH		"/dev/iscsi"
#define	ISCSI_MAX_DATA_SEGMENT_LENGTH	(128 * 1024)

#define	ISCSI_NAME_LEN		224	/* 223 bytes, by RFC 3720, + '\0' */
#define	ISCSI_ADDR_LEN		47	/* INET6_ADDRSTRLEN + '\0' */
#define	ISCSI_ALIAS_LEN		256	/* XXX: Where did it come from? */
#define	ISCSI_SECRET_LEN	17	/* 16 + '\0' */
#define	ISCSI_OFFLOAD_LEN	8
#define	ISCSI_REASON_LEN	64

#define	ISCSI_DIGEST_NONE	0
#define	ISCSI_DIGEST_CRC32C	1

/*
 * Session configuration, set when adding the session.
 */
struct iscsi_session_conf {
	char 		isc_initiator[ISCSI_NAME_LEN];
	char		isc_initiator_addr[ISCSI_ADDR_LEN];
	char		isc_initiator_alias[ISCSI_ALIAS_LEN];
	char 		isc_target[ISCSI_NAME_LEN];
	char		isc_target_addr[ISCSI_ADDR_LEN];
	char		isc_user[ISCSI_NAME_LEN];
	char		isc_secret[ISCSI_SECRET_LEN];
	char		isc_mutual_user[ISCSI_NAME_LEN];
	char		isc_mutual_secret[ISCSI_SECRET_LEN];
	int		isc_discovery;
	int		isc_header_digest;
	int		isc_data_digest;
	int		isc_iser;
	char		isc_offload[ISCSI_OFFLOAD_LEN];
	int		isc_enable;
	int		isc_spare[4];
};

/*
 * Additional constraints imposed by chosen ICL offload module;
 * iscsid(8) must obey those when negotiating operational parameters.
 */
struct iscsi_session_limits {
	size_t		isl_spare0;
	int		isl_max_recv_data_segment_length;
	int		isl_max_send_data_segment_length;
	int		isl_max_burst_length;
	int		isl_first_burst_length;
	int		isl_spare[4];
};

/*
 * Session state, negotiated by iscsid(8) and queried by iscsictl(8).
 */
struct iscsi_session_state {
	struct iscsi_session_conf	iss_conf;
	unsigned int	iss_id;
	char		iss_target_alias[ISCSI_ALIAS_LEN];
	int		iss_header_digest;
	int		iss_data_digest;
	int		iss_max_recv_data_segment_length;
	int		iss_max_burst_length;
	int		iss_first_burst_length;
	int		iss_immediate_data;
	int		iss_connected;
	char		iss_reason[ISCSI_REASON_LEN];
	char		iss_offload[ISCSI_OFFLOAD_LEN];
	int		iss_max_send_data_segment_length;
	int		iss_spare[3];
};

/*
 * The following ioctls are used by iscsid(8).
 */
struct iscsi_daemon_request {
	unsigned int			idr_session_id;
	struct iscsi_session_conf	idr_conf;
	uint8_t				idr_isid[6];
	uint16_t			idr_tsih;
	uint16_t			idr_spare_cid;
	struct iscsi_session_limits	idr_limits;
	int				idr_spare[4];
};

struct iscsi_daemon_handoff {
	unsigned int			idh_session_id;
	int				idh_socket;
	char				idh_target_alias[ISCSI_ALIAS_LEN];
	uint8_t				idh_spare_isid[6];
	uint16_t			idh_tsih;
	uint16_t			idh_spare_cid;
	uint32_t			idh_statsn;
	int				idh_header_digest;
	int				idh_data_digest;
	size_t				spare[3];
	int				idh_immediate_data;
	int				idh_initial_r2t;
	int				idh_max_recv_data_segment_length;
	int				idh_max_send_data_segment_length;
	int				idh_max_burst_length;
	int				idh_first_burst_length;
};

struct iscsi_daemon_fail {
	unsigned int			idf_session_id;
	char				idf_reason[ISCSI_REASON_LEN];
	int				idf_spare[4];
};

#define	ISCSIDWAIT	_IOR('I', 0x01, struct iscsi_daemon_request)
#define	ISCSIDHANDOFF	_IOW('I', 0x02, struct iscsi_daemon_handoff)
#define	ISCSIDFAIL	_IOW('I', 0x03, struct iscsi_daemon_fail)

#ifdef ICL_KERNEL_PROXY

/*
 * When ICL_KERNEL_PROXY is not defined, the iscsid(8) is responsible
 * for creating the socket, connecting, and performing Login Phase using
 * the socket in the usual userspace way, and then passing the socket
 * file descriptor to the kernel part using ISCSIDHANDOFF.
 *
 * When ICL_KERNEL_PROXY is defined, the iscsid(8) creates the session
 * using ISCSICONNECT, performs Login Phase using ISCSISEND/ISCSIRECEIVE
 * instead of read(2)/write(2), and then calls ISCSIDHANDOFF with
 * idh_socket set to 0.
 *
 * The purpose of ICL_KERNEL_PROXY is to workaround the fact that,
 * at this time, it's not possible to do iWARP (RDMA) in userspace.
 */

struct iscsi_daemon_connect {
	unsigned int			idc_session_id;
	int				idc_iser;
	int				idc_domain;
	int				idc_socktype;
	int				idc_protocol;
	struct sockaddr			*idc_from_addr;
	socklen_t			idc_from_addrlen;
	struct sockaddr			*idc_to_addr;
	socklen_t			idc_to_addrlen;
	int				idc_spare[4];
};

struct iscsi_daemon_send {
	unsigned int			ids_session_id;
	void				*ids_bhs;
	size_t				ids_spare;
	void				*ids_spare2;
	size_t				ids_data_segment_len;
	void				*ids_data_segment;
	int				ids_spare3[4];
};

struct iscsi_daemon_receive {
	unsigned int			idr_session_id;
	void				*idr_bhs;
	size_t				idr_spare;
	void				*idr_spare2;
	size_t				idr_data_segment_len;
	void				*idr_data_segment;
	int				idr_spare3[4];
};

#define	ISCSIDCONNECT	_IOWR('I', 0x04, struct iscsi_daemon_connect)
#define	ISCSIDSEND	_IOWR('I', 0x05, struct iscsi_daemon_send)
#define	ISCSIDRECEIVE	_IOWR('I', 0x06, struct iscsi_daemon_receive)

#endif /* ICL_KERNEL_PROXY */

/*
 * The following ioctls are used by iscsictl(8).
 */
struct iscsi_session_add {
	struct iscsi_session_conf	isa_conf;
	int				isa_spare[4];
};

struct iscsi_session_remove {
	unsigned int			isr_session_id;
	struct iscsi_session_conf	isr_conf;
	int				isr_spare[4];
};

struct iscsi_session_list {
	unsigned int			isl_nentries;
	struct iscsi_session_state	*isl_pstates;
	int				isl_spare[4];
};

struct iscsi_session_modify {
	unsigned int			ism_session_id;
	struct iscsi_session_conf	ism_conf;
	int				ism_spare[4];
};

#define	ISCSISADD	_IOW('I', 0x11, struct iscsi_session_add)
#define	ISCSISREMOVE	_IOW('I', 0x12, struct iscsi_session_remove)
#define	ISCSISLIST	_IOWR('I', 0x13, struct iscsi_session_list)
#define	ISCSISMODIFY	_IOWR('I', 0x14, struct iscsi_session_modify)

#endif /* !ISCSI_IOCTL_H */
