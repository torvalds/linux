/*
 * iSCSI transport class definitions
 *
 * Copyright (C) IBM Corporation, 2004
 * Copyright (C) Mike Christie, 2004 - 2005
 * Copyright (C) Dmitry Yusupov, 2004 - 2005
 * Copyright (C) Alex Aizman, 2004 - 2005
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA 02111-1307, USA.
 */
#ifndef SCSI_TRANSPORT_ISCSI_H
#define SCSI_TRANSPORT_ISCSI_H

#include <scsi/iscsi_if.h>

/**
 * struct iscsi_transport - iSCSI Transport template
 *
 * @name:		transport name
 * @caps:		iSCSI Data-Path capabilities
 * @create_session:	create new iSCSI session object
 * @destroy_session:	destroy existing iSCSI session object
 * @create_conn:	create new iSCSI connection
 * @bind_conn:		associate this connection with existing iSCSI session
 *			and specified transport descriptor
 * @destroy_conn:	destroy inactive iSCSI connection
 * @set_param:		set iSCSI Data-Path operational parameter
 * @start_conn:		set connection to be operational
 * @stop_conn:		suspend/recover/terminate connection
 * @send_pdu:		send iSCSI PDU, Login, Logout, NOP-Out, Reject, Text.
 *
 * Template API provided by iSCSI Transport
 */
struct iscsi_transport {
	struct module *owner;
	char *name;
	unsigned int caps;
	struct scsi_host_template *host_template;
	int hostdata_size;
	int max_lun;
	unsigned int max_conn;
	unsigned int max_cmd_len;
	iscsi_sessionh_t (*create_session) (uint32_t initial_cmdsn,
					    struct Scsi_Host *shost);
	void (*destroy_session) (iscsi_sessionh_t session);
	iscsi_connh_t (*create_conn) (iscsi_sessionh_t session, uint32_t cid);
	int (*bind_conn) (iscsi_sessionh_t session, iscsi_connh_t conn,
			  uint32_t transport_fd, int is_leading);
	int (*start_conn) (iscsi_connh_t conn);
	void (*stop_conn) (iscsi_connh_t conn, int flag);
	void (*destroy_conn) (iscsi_connh_t conn);
	int (*set_param) (iscsi_connh_t conn, enum iscsi_param param,
			  uint32_t value);
	int (*get_param) (iscsi_connh_t conn, enum iscsi_param param,
			  uint32_t *value);
	int (*send_pdu) (iscsi_connh_t conn, struct iscsi_hdr *hdr,
			 char *data, uint32_t data_size);
	void (*get_stats) (iscsi_connh_t conn, struct iscsi_stats *stats);
};

/*
 * transport registration upcalls
 */
extern int iscsi_register_transport(struct iscsi_transport *tt);
extern int iscsi_unregister_transport(struct iscsi_transport *tt);

/*
 * control plane upcalls
 */
extern void iscsi_conn_error(iscsi_connh_t conn, enum iscsi_err error);
extern int iscsi_recv_pdu(iscsi_connh_t conn, struct iscsi_hdr *hdr,
			  char *data, uint32_t data_size);

#endif
