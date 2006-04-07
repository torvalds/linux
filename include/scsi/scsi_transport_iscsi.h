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

#include <linux/device.h>
#include <scsi/iscsi_if.h>

struct scsi_transport_template;
struct Scsi_Host;
struct mempool_zone;
struct iscsi_cls_conn;

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
	/* LLD session/scsi_host data size */
	int hostdata_size;
	/* LLD iscsi_host data size */
	int ihostdata_size;
	/* LLD connection data size */
	int conndata_size;
	/* LLD session data size */
	int sessiondata_size;
	int max_lun;
	unsigned int max_conn;
	unsigned int max_cmd_len;
	struct iscsi_cls_session *(*create_session)
		(struct scsi_transport_template *t, uint32_t sn, uint32_t *hn);
	void (*destroy_session) (struct iscsi_cls_session *session);
	struct iscsi_cls_conn *(*create_conn) (struct iscsi_cls_session *sess,
				uint32_t cid);
	int (*bind_conn) (struct iscsi_cls_session *session,
			  struct iscsi_cls_conn *cls_conn,
			  uint32_t transport_fd, int is_leading);
	int (*start_conn) (struct iscsi_cls_conn *conn);
	void (*stop_conn) (struct iscsi_cls_conn *conn, int flag);
	void (*destroy_conn) (struct iscsi_cls_conn *conn);
	int (*set_param) (struct iscsi_cls_conn *conn, enum iscsi_param param,
			  uint32_t value);
	int (*get_conn_param) (struct iscsi_cls_conn *conn,
			       enum iscsi_param param,
			       uint32_t *value);
	int (*get_session_param) (struct iscsi_cls_session *session,
				  enum iscsi_param param, uint32_t *value);
	int (*send_pdu) (struct iscsi_cls_conn *conn, struct iscsi_hdr *hdr,
			 char *data, uint32_t data_size);
	void (*get_stats) (struct iscsi_cls_conn *conn,
			   struct iscsi_stats *stats);
};

/*
 * transport registration upcalls
 */
extern struct scsi_transport_template *iscsi_register_transport(struct iscsi_transport *tt);
extern int iscsi_unregister_transport(struct iscsi_transport *tt);

/*
 * control plane upcalls
 */
extern void iscsi_conn_error(struct iscsi_cls_conn *conn, enum iscsi_err error);
extern int iscsi_recv_pdu(struct iscsi_cls_conn *conn, struct iscsi_hdr *hdr,
			  char *data, uint32_t data_size);

struct iscsi_cls_conn {
	struct list_head conn_list;	/* item in connlist */
	void *dd_data;			/* LLD private data */
	struct iscsi_transport *transport;
	uint32_t cid;			/* connection id */
	int active;			/* must be accessed with the connlock */
	struct device dev;		/* sysfs transport/container device */
	struct mempool_zone *z_error;
	struct mempool_zone *z_pdu;
	struct list_head freequeue;
};

#define iscsi_dev_to_conn(_dev) \
	container_of(_dev, struct iscsi_cls_conn, dev)

struct iscsi_cls_session {
	struct list_head sess_list;		/* item in session_list */
	struct iscsi_transport *transport;
	int sid;				/* session id */
	void *dd_data;				/* LLD private data */
	struct device dev;	/* sysfs transport/container device */
};

#define iscsi_dev_to_session(_dev) \
	container_of(_dev, struct iscsi_cls_session, dev)

#define iscsi_session_to_shost(_session) \
	dev_to_shost(_session->dev.parent)

/*
 * session and connection functions that can be used by HW iSCSI LLDs
 */
extern struct iscsi_cls_session *iscsi_create_session(struct Scsi_Host *shost,
				struct iscsi_transport *t);
extern int iscsi_destroy_session(struct iscsi_cls_session *session);
extern struct iscsi_cls_conn *iscsi_create_conn(struct iscsi_cls_session *sess,
					    uint32_t cid);
extern int iscsi_destroy_conn(struct iscsi_cls_conn *conn);

/*
 * session functions used by software iscsi
 */
extern struct Scsi_Host *
iscsi_transport_create_session(struct scsi_transport_template *scsit,
                               struct iscsi_transport *transport);
extern int iscsi_transport_destroy_session(struct Scsi_Host *shost);

#endif
