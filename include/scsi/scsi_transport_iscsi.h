/*
 * iSCSI transport class definitions
 *
 * Copyright (C) IBM Corporation, 2004
 * Copyright (C) Mike Christie, 2004 - 2006
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
struct iscsi_transport;
struct Scsi_Host;
struct mempool_zone;
struct iscsi_cls_conn;
struct iscsi_conn;
struct iscsi_cmd_task;
struct iscsi_mgmt_task;
struct sockaddr;

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
 * @set_param:		set iSCSI parameter. Return 0 on success, -ENODATA
 *			when param is not supported, and a -Exx value on other
 *			error.
 * @get_param		get iSCSI parameter. Must return number of bytes
 *			copied to buffer on success, -ENODATA when param
 *			is not supported, and a -Exx value on other error
 * @start_conn:		set connection to be operational
 * @stop_conn:		suspend/recover/terminate connection
 * @send_pdu:		send iSCSI PDU, Login, Logout, NOP-Out, Reject, Text.
 * @session_recovery_timedout: notify LLD a block during recovery timed out
 * @init_cmd_task:	Initialize a iscsi_cmd_task and any internal structs.
 *			Called from queuecommand with session lock held.
 * @init_mgmt_task:	Initialize a iscsi_mgmt_task and any internal structs.
 *			Called from iscsi_conn_send_generic with xmitmutex.
 * @xmit_cmd_task:	Requests LLD to transfer cmd task. Returns 0 or the
 *			the number of bytes transferred on success, and -Exyz
 *			value on error.
 * @xmit_mgmt_task:	Requests LLD to transfer mgmt task. Returns 0 or the
 *			the number of bytes transferred on success, and -Exyz
 *			value on error.
 * @cleanup_cmd_task:	requests LLD to fail cmd task. Called with xmitmutex
 *			and session->lock after the connection has been
 *			suspended and terminated during recovery. If called
 *			from abort task then connection is not suspended
 *			or terminated but sk_callback_lock is held
 *
 * Template API provided by iSCSI Transport
 */
struct iscsi_transport {
	struct module *owner;
	char *name;
	unsigned int caps;
	/* LLD sets this to indicate what values it can export to sysfs */
	unsigned int param_mask;
	struct scsi_host_template *host_template;
	/* LLD connection data size */
	int conndata_size;
	/* LLD session data size */
	int sessiondata_size;
	int max_lun;
	unsigned int max_conn;
	unsigned int max_cmd_len;
	struct iscsi_cls_session *(*create_session) (struct iscsi_transport *it,
		struct scsi_transport_template *t, uint32_t sn, uint32_t *hn);
	void (*destroy_session) (struct iscsi_cls_session *session);
	struct iscsi_cls_conn *(*create_conn) (struct iscsi_cls_session *sess,
				uint32_t cid);
	int (*bind_conn) (struct iscsi_cls_session *session,
			  struct iscsi_cls_conn *cls_conn,
			  uint64_t transport_eph, int is_leading);
	int (*start_conn) (struct iscsi_cls_conn *conn);
	void (*stop_conn) (struct iscsi_cls_conn *conn, int flag);
	void (*destroy_conn) (struct iscsi_cls_conn *conn);
	int (*set_param) (struct iscsi_cls_conn *conn, enum iscsi_param param,
			  char *buf, int buflen);
	int (*get_conn_param) (struct iscsi_cls_conn *conn,
			       enum iscsi_param param, char *buf);
	int (*get_session_param) (struct iscsi_cls_session *session,
				  enum iscsi_param param, char *buf);
	int (*send_pdu) (struct iscsi_cls_conn *conn, struct iscsi_hdr *hdr,
			 char *data, uint32_t data_size);
	void (*get_stats) (struct iscsi_cls_conn *conn,
			   struct iscsi_stats *stats);
	void (*init_cmd_task) (struct iscsi_cmd_task *ctask);
	void (*init_mgmt_task) (struct iscsi_conn *conn,
				struct iscsi_mgmt_task *mtask,
				char *data, uint32_t data_size);
	int (*xmit_cmd_task) (struct iscsi_conn *conn,
			      struct iscsi_cmd_task *ctask);
	void (*cleanup_cmd_task) (struct iscsi_conn *conn,
				  struct iscsi_cmd_task *ctask);
	int (*xmit_mgmt_task) (struct iscsi_conn *conn,
			       struct iscsi_mgmt_task *mtask);
	void (*session_recovery_timedout) (struct iscsi_cls_session *session);
	int (*ep_connect) (struct sockaddr *dst_addr, int non_blocking,
			   uint64_t *ep_handle);
	int (*ep_poll) (uint64_t ep_handle, int timeout_ms);
	void (*ep_disconnect) (uint64_t ep_handle);
	int (*tgt_dscvr) (enum iscsi_tgt_dscvr type, uint32_t host_no,
			  uint32_t enable, struct sockaddr *dst_addr);
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


/* Connection's states */
#define ISCSI_CONN_INITIAL_STAGE	0
#define ISCSI_CONN_STARTED		1
#define ISCSI_CONN_STOPPED		2
#define ISCSI_CONN_CLEANUP_WAIT		3

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

/* Session's states */
#define ISCSI_STATE_FREE		1
#define ISCSI_STATE_LOGGED_IN		2
#define ISCSI_STATE_FAILED		3
#define ISCSI_STATE_TERMINATE		4
#define ISCSI_STATE_IN_RECOVERY		5
#define ISCSI_STATE_RECOVERY_FAILED	6

struct iscsi_cls_session {
	struct list_head sess_list;		/* item in session_list */
	struct list_head host_list;
	struct iscsi_transport *transport;

	/* recovery fields */
	int recovery_tmo;
	struct work_struct recovery_work;

	int target_id;

	int sid;				/* session id */
	void *dd_data;				/* LLD private data */
	struct device dev;	/* sysfs transport/container device */
};

#define iscsi_dev_to_session(_dev) \
	container_of(_dev, struct iscsi_cls_session, dev)

#define iscsi_session_to_shost(_session) \
	dev_to_shost(_session->dev.parent)

#define starget_to_session(_stgt) \
	iscsi_dev_to_session(_stgt->dev.parent)

struct iscsi_host {
	struct list_head sessions;
	struct mutex mutex;
};

/*
 * session and connection functions that can be used by HW iSCSI LLDs
 */
extern struct iscsi_cls_session *iscsi_alloc_session(struct Scsi_Host *shost,
					struct iscsi_transport *transport);
extern int iscsi_add_session(struct iscsi_cls_session *session,
			     unsigned int target_id);
extern int iscsi_if_create_session_done(struct iscsi_cls_conn *conn);
extern int iscsi_if_destroy_session_done(struct iscsi_cls_conn *conn);
extern struct iscsi_cls_session *iscsi_create_session(struct Scsi_Host *shost,
						struct iscsi_transport *t,
						unsigned int target_id);
extern void iscsi_remove_session(struct iscsi_cls_session *session);
extern void iscsi_free_session(struct iscsi_cls_session *session);
extern int iscsi_destroy_session(struct iscsi_cls_session *session);
extern struct iscsi_cls_conn *iscsi_create_conn(struct iscsi_cls_session *sess,
					    uint32_t cid);
extern int iscsi_destroy_conn(struct iscsi_cls_conn *conn);
extern void iscsi_unblock_session(struct iscsi_cls_session *session);
extern void iscsi_block_session(struct iscsi_cls_session *session);


#endif
