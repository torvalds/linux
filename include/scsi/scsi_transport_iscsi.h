/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
 * iSCSI transport class definitions
 *
 * Copyright (C) IBM Corporation, 2004
 * Copyright (C) Mike Christie, 2004 - 2006
 * Copyright (C) Dmitry Yusupov, 2004 - 2005
 * Copyright (C) Alex Aizman, 2004 - 2005
 */
#ifndef SCSI_TRANSPORT_ISCSI_H
#define SCSI_TRANSPORT_ISCSI_H

#include <linux/device.h>
#include <linux/list.h>
#include <linux/mutex.h>
#include <scsi/iscsi_if.h>

struct scsi_transport_template;
struct iscsi_transport;
struct iscsi_endpoint;
struct Scsi_Host;
struct scsi_cmnd;
struct iscsi_cls_conn;
struct iscsi_conn;
struct iscsi_task;
struct sockaddr;
struct iscsi_iface;
struct bsg_job;
struct iscsi_bus_flash_session;
struct iscsi_bus_flash_conn;

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
 * @init_task:		Initialize a iscsi_task and any internal structs.
 *			When offloading the data path, this is called from
 *			queuecommand with the session lock, or from the
 *			iscsi_conn_send_pdu context with the session lock.
 *			When not offloading the data path, this is called
 *			from the scsi work queue without the session lock.
 * @xmit_task		Requests LLD to transfer cmd task. Returns 0 or the
 *			number of bytes transferred on success, and -Exyz
 *			value on error. When offloading the data path, this
 *			is called from queuecommand with the session lock, or
 *			from the iscsi_conn_send_pdu context with the session
 *			lock. When not offloading the data path, this is called
 *			from the scsi work queue without the session lock.
 * @cleanup_task:	requests LLD to fail task. Called with session lock
 *			and after the connection has been suspended and
 *			terminated during recovery. If called
 *			from abort task then connection is not suspended
 *			or terminated but sk_callback_lock is held
 *
 * Template API provided by iSCSI Transport
 */
struct iscsi_transport {
	struct module *owner;
	char *name;
	unsigned int caps;

	struct iscsi_cls_session *(*create_session) (struct iscsi_endpoint *ep,
					uint16_t cmds_max, uint16_t qdepth,
					uint32_t sn);
	void (*destroy_session) (struct iscsi_cls_session *session);
	struct iscsi_cls_conn *(*create_conn) (struct iscsi_cls_session *sess,
				uint32_t cid);
	void (*unbind_conn) (struct iscsi_cls_conn *conn, bool is_active);
	int (*bind_conn) (struct iscsi_cls_session *session,
			  struct iscsi_cls_conn *cls_conn,
			  uint64_t transport_eph, int is_leading);
	int (*start_conn) (struct iscsi_cls_conn *conn);
	void (*stop_conn) (struct iscsi_cls_conn *conn, int flag);
	void (*destroy_conn) (struct iscsi_cls_conn *conn);
	int (*set_param) (struct iscsi_cls_conn *conn, enum iscsi_param param,
			  char *buf, int buflen);
	int (*get_ep_param) (struct iscsi_endpoint *ep, enum iscsi_param param,
			     char *buf);
	int (*get_conn_param) (struct iscsi_cls_conn *conn,
			       enum iscsi_param param, char *buf);
	int (*get_session_param) (struct iscsi_cls_session *session,
				  enum iscsi_param param, char *buf);
	int (*get_host_param) (struct Scsi_Host *shost,
				enum iscsi_host_param param, char *buf);
	int (*set_host_param) (struct Scsi_Host *shost,
			       enum iscsi_host_param param, char *buf,
			       int buflen);
	int (*send_pdu) (struct iscsi_cls_conn *conn, struct iscsi_hdr *hdr,
			 char *data, uint32_t data_size);
	void (*get_stats) (struct iscsi_cls_conn *conn,
			   struct iscsi_stats *stats);

	int (*init_task) (struct iscsi_task *task);
	int (*xmit_task) (struct iscsi_task *task);
	void (*cleanup_task) (struct iscsi_task *task);

	int (*alloc_pdu) (struct iscsi_task *task, uint8_t opcode);
	int (*xmit_pdu) (struct iscsi_task *task);
	int (*init_pdu) (struct iscsi_task *task, unsigned int offset,
			 unsigned int count);
	void (*parse_pdu_itt) (struct iscsi_conn *conn, itt_t itt,
			       int *index, int *age);

	void (*session_recovery_timedout) (struct iscsi_cls_session *session);
	struct iscsi_endpoint *(*ep_connect) (struct Scsi_Host *shost,
					      struct sockaddr *dst_addr,
					      int non_blocking);
	int (*ep_poll) (struct iscsi_endpoint *ep, int timeout_ms);
	void (*ep_disconnect) (struct iscsi_endpoint *ep);
	int (*tgt_dscvr) (struct Scsi_Host *shost, enum iscsi_tgt_dscvr type,
			  uint32_t enable, struct sockaddr *dst_addr);
	int (*set_path) (struct Scsi_Host *shost, struct iscsi_path *params);
	int (*set_iface_param) (struct Scsi_Host *shost, void *data,
				uint32_t len);
	int (*get_iface_param) (struct iscsi_iface *iface,
				enum iscsi_param_type param_type,
				int param, char *buf);
	umode_t (*attr_is_visible)(int param_type, int param);
	int (*bsg_request)(struct bsg_job *job);
	int (*send_ping) (struct Scsi_Host *shost, uint32_t iface_num,
			  uint32_t iface_type, uint32_t payload_size,
			  uint32_t pid, struct sockaddr *dst_addr);
	int (*get_chap) (struct Scsi_Host *shost, uint16_t chap_tbl_idx,
			 uint32_t *num_entries, char *buf);
	int (*delete_chap) (struct Scsi_Host *shost, uint16_t chap_tbl_idx);
	int (*set_chap) (struct Scsi_Host *shost, void *data, int len);
	int (*get_flashnode_param) (struct iscsi_bus_flash_session *fnode_sess,
				    int param, char *buf);
	int (*set_flashnode_param) (struct iscsi_bus_flash_session *fnode_sess,
				    struct iscsi_bus_flash_conn *fnode_conn,
				    void *data, int len);
	int (*new_flashnode) (struct Scsi_Host *shost, const char *buf,
			      int len);
	int (*del_flashnode) (struct iscsi_bus_flash_session *fnode_sess);
	int (*login_flashnode) (struct iscsi_bus_flash_session *fnode_sess,
				struct iscsi_bus_flash_conn *fnode_conn);
	int (*logout_flashnode) (struct iscsi_bus_flash_session *fnode_sess,
				 struct iscsi_bus_flash_conn *fnode_conn);
	int (*logout_flashnode_sid) (struct iscsi_cls_session *cls_sess);
	int (*get_host_stats) (struct Scsi_Host *shost, char *buf, int len);
	u8 (*check_protection)(struct iscsi_task *task, sector_t *sector);
};

/*
 * transport registration upcalls
 */
extern struct scsi_transport_template *iscsi_register_transport(struct iscsi_transport *tt);
extern void iscsi_unregister_transport(struct iscsi_transport *tt);

/*
 * control plane upcalls
 */
extern void iscsi_conn_error_event(struct iscsi_cls_conn *conn,
				   enum iscsi_err error);
extern void iscsi_conn_login_event(struct iscsi_cls_conn *conn,
				   enum iscsi_conn_state state);
extern int iscsi_recv_pdu(struct iscsi_cls_conn *conn, struct iscsi_hdr *hdr,
			  char *data, uint32_t data_size);

extern int iscsi_offload_mesg(struct Scsi_Host *shost,
			      struct iscsi_transport *transport, uint32_t type,
			      char *data, uint16_t data_size);

extern void iscsi_post_host_event(uint32_t host_no,
				  struct iscsi_transport *transport,
				  enum iscsi_host_event_code code,
				  uint32_t data_size,
				  uint8_t *data);

extern void iscsi_ping_comp_event(uint32_t host_no,
				  struct iscsi_transport *transport,
				  uint32_t status, uint32_t pid,
				  uint32_t data_size, uint8_t *data);

/* iscsi class connection state */
enum iscsi_connection_state {
	ISCSI_CONN_UP = 0,
	ISCSI_CONN_DOWN,
	ISCSI_CONN_FAILED,
	ISCSI_CONN_BOUND,
};

#define ISCSI_CLS_CONN_BIT_CLEANUP	1

struct iscsi_cls_conn {
	struct list_head conn_list;	/* item in connlist */
	void *dd_data;			/* LLD private data */
	struct iscsi_transport *transport;
	uint32_t cid;			/* connection id */
	/*
	 * This protects the conn startup and binding/unbinding of the ep to
	 * the conn. Unbinding includes ep_disconnect and stop_conn.
	 */
	struct mutex ep_mutex;
	struct iscsi_endpoint *ep;

	/* Used when accessing flags and queueing work. */
	spinlock_t lock;
	unsigned long flags;
	struct work_struct cleanup_work;

	struct device dev;		/* sysfs transport/container device */
	enum iscsi_connection_state state;
};

#define iscsi_dev_to_conn(_dev) \
	container_of(_dev, struct iscsi_cls_conn, dev)

#define transport_class_to_conn(_cdev) \
	iscsi_dev_to_conn(_cdev->parent)

#define iscsi_conn_to_session(_conn) \
	iscsi_dev_to_session(_conn->dev.parent)

/* iscsi class session state */
enum {
	ISCSI_SESSION_LOGGED_IN,
	ISCSI_SESSION_FAILED,
	ISCSI_SESSION_FREE,
};

#define ISCSI_MAX_TARGET -1

struct iscsi_cls_session {
	struct list_head sess_list;		/* item in session_list */
	struct iscsi_transport *transport;
	spinlock_t lock;
	struct work_struct block_work;
	struct work_struct unblock_work;
	struct work_struct scan_work;
	struct work_struct unbind_work;
	struct work_struct destroy_work;

	/* recovery fields */
	int recovery_tmo;
	bool recovery_tmo_sysfs_override;
	struct delayed_work recovery_work;

	struct workqueue_struct *workq;

	unsigned int target_id;
	bool ida_used;

	/*
	 * pid of userspace process that created session or -1 if
	 * created by the kernel.
	 */
	pid_t creator;
	int state;
	int sid;				/* session id */
	void *dd_data;				/* LLD private data */
	struct device dev;	/* sysfs transport/container device */
};

#define iscsi_dev_to_session(_dev) \
	container_of(_dev, struct iscsi_cls_session, dev)

#define transport_class_to_session(_cdev) \
	iscsi_dev_to_session(_cdev->parent)

#define iscsi_session_to_shost(_session) \
	dev_to_shost(_session->dev.parent)

#define starget_to_session(_stgt) \
	iscsi_dev_to_session(_stgt->dev.parent)

struct iscsi_cls_host {
	struct mutex mutex;
	struct request_queue *bsg_q;
	uint32_t port_speed;
	uint32_t port_state;
};

#define iscsi_job_to_shost(_job) \
        dev_to_shost(_job->dev)

extern void iscsi_host_for_each_session(struct Scsi_Host *shost,
				void (*fn)(struct iscsi_cls_session *));

struct iscsi_endpoint {
	void *dd_data;			/* LLD private data */
	struct device dev;
	int id;
	struct iscsi_cls_conn *conn;
};

struct iscsi_iface {
	struct device dev;
	struct iscsi_transport *transport;
	uint32_t iface_type;	/* IPv4 or IPv6 */
	uint32_t iface_num;	/* iface number, 0 - n */
	void *dd_data;		/* LLD private data */
};

#define iscsi_dev_to_iface(_dev) \
	container_of(_dev, struct iscsi_iface, dev)

#define iscsi_iface_to_shost(_iface) \
	dev_to_shost(_iface->dev.parent)


struct iscsi_bus_flash_conn {
	struct list_head conn_list;	/* item in connlist */
	void *dd_data;			/* LLD private data */
	struct iscsi_transport *transport;
	struct device dev;		/* sysfs transport/container device */
	/* iscsi connection parameters */
	uint32_t		exp_statsn;
	uint32_t		statsn;
	unsigned		max_recv_dlength; /* initiator_max_recv_dsl*/
	unsigned		max_xmit_dlength; /* target_max_recv_dsl */
	unsigned		max_segment_size;
	unsigned		tcp_xmit_wsf;
	unsigned		tcp_recv_wsf;
	int			hdrdgst_en;
	int			datadgst_en;
	int			port;
	char			*ipaddress;
	char			*link_local_ipv6_addr;
	char			*redirect_ipaddr;
	uint16_t		keepalive_timeout;
	uint16_t		local_port;
	uint8_t			snack_req_en;
	/* tcp timestamp negotiation status */
	uint8_t			tcp_timestamp_stat;
	uint8_t			tcp_nagle_disable;
	/* tcp window scale factor */
	uint8_t			tcp_wsf_disable;
	uint8_t			tcp_timer_scale;
	uint8_t			tcp_timestamp_en;
	uint8_t			ipv4_tos;
	uint8_t			ipv6_traffic_class;
	uint8_t			ipv6_flow_label;
	uint8_t			fragment_disable;
	/* Link local IPv6 address is assigned by firmware or driver */
	uint8_t			is_fw_assigned_ipv6;
};

#define iscsi_dev_to_flash_conn(_dev) \
	container_of(_dev, struct iscsi_bus_flash_conn, dev)

#define iscsi_flash_conn_to_flash_session(_conn) \
	iscsi_dev_to_flash_session(_conn->dev.parent)

#define ISID_SIZE 6

struct iscsi_bus_flash_session {
	struct list_head sess_list;		/* item in session_list */
	struct iscsi_transport *transport;
	unsigned int target_id;
	int flash_state;	/* persistent or non-persistent */
	void *dd_data;				/* LLD private data */
	struct device dev;	/* sysfs transport/container device */
	/* iscsi session parameters */
	unsigned		first_burst;
	unsigned		max_burst;
	unsigned short		max_r2t;
	int			default_taskmgmt_timeout;
	int			initial_r2t_en;
	int			imm_data_en;
	int			time2wait;
	int			time2retain;
	int			pdu_inorder_en;
	int			dataseq_inorder_en;
	int			erl;
	int			tpgt;
	char			*username;
	char			*username_in;
	char			*password;
	char			*password_in;
	char			*targetname;
	char			*targetalias;
	char			*portal_type;
	uint16_t		tsid;
	uint16_t		chap_in_idx;
	uint16_t		chap_out_idx;
	/* index of iSCSI discovery session if the entry is
	 * discovered by iSCSI discovery session
	 */
	uint16_t		discovery_parent_idx;
	/* indicates if discovery was done through iSNS discovery service
	 * or through sendTarget */
	uint16_t		discovery_parent_type;
	/* Firmware auto sendtarget discovery disable */
	uint8_t			auto_snd_tgt_disable;
	uint8_t			discovery_sess;
	/* indicates if this flashnode entry is enabled or disabled */
	uint8_t			entry_state;
	uint8_t			chap_auth_en;
	/* enables firmware to auto logout the discovery session on discovery
	 * completion
	 */
	uint8_t			discovery_logout_en;
	uint8_t			bidi_chap_en;
	/* makes authentication for discovery session optional */
	uint8_t			discovery_auth_optional;
	uint8_t			isid[ISID_SIZE];
	uint8_t			is_boot_target;
};

#define iscsi_dev_to_flash_session(_dev) \
	container_of(_dev, struct iscsi_bus_flash_session, dev)

#define iscsi_flash_session_to_shost(_session) \
	dev_to_shost(_session->dev.parent)

/*
 * session and connection functions that can be used by HW iSCSI LLDs
 */
#define iscsi_cls_session_printk(prefix, _cls_session, fmt, a...) \
	dev_printk(prefix, &(_cls_session)->dev, fmt, ##a)

#define iscsi_cls_conn_printk(prefix, _cls_conn, fmt, a...) \
	dev_printk(prefix, &(_cls_conn)->dev, fmt, ##a)

extern int iscsi_session_chkready(struct iscsi_cls_session *session);
extern int iscsi_is_session_online(struct iscsi_cls_session *session);
extern struct iscsi_cls_session *iscsi_alloc_session(struct Scsi_Host *shost,
				struct iscsi_transport *transport, int dd_size);
extern int iscsi_add_session(struct iscsi_cls_session *session,
			     unsigned int target_id);
extern int iscsi_session_event(struct iscsi_cls_session *session,
			       enum iscsi_uevent_e event);
extern struct iscsi_cls_session *iscsi_create_session(struct Scsi_Host *shost,
						struct iscsi_transport *t,
						int dd_size,
						unsigned int target_id);
extern void iscsi_force_destroy_session(struct iscsi_cls_session *session);
extern void iscsi_remove_session(struct iscsi_cls_session *session);
extern void iscsi_free_session(struct iscsi_cls_session *session);
extern struct iscsi_cls_conn *iscsi_alloc_conn(struct iscsi_cls_session *sess,
						int dd_size, uint32_t cid);
extern int iscsi_add_conn(struct iscsi_cls_conn *conn);
extern void iscsi_remove_conn(struct iscsi_cls_conn *conn);
extern void iscsi_put_conn(struct iscsi_cls_conn *conn);
extern void iscsi_get_conn(struct iscsi_cls_conn *conn);
extern void iscsi_unblock_session(struct iscsi_cls_session *session);
extern void iscsi_block_session(struct iscsi_cls_session *session);
extern struct iscsi_endpoint *iscsi_create_endpoint(int dd_size);
extern void iscsi_destroy_endpoint(struct iscsi_endpoint *ep);
extern struct iscsi_endpoint *iscsi_lookup_endpoint(u64 handle);
extern void iscsi_put_endpoint(struct iscsi_endpoint *ep);
extern int iscsi_block_scsi_eh(struct scsi_cmnd *cmd);
extern struct iscsi_iface *iscsi_create_iface(struct Scsi_Host *shost,
					      struct iscsi_transport *t,
					      uint32_t iface_type,
					      uint32_t iface_num, int dd_size);
extern void iscsi_destroy_iface(struct iscsi_iface *iface);
extern struct iscsi_iface *iscsi_lookup_iface(int handle);
extern char *iscsi_get_port_speed_name(struct Scsi_Host *shost);
extern char *iscsi_get_port_state_name(struct Scsi_Host *shost);
extern int iscsi_is_session_dev(const struct device *dev);

extern char *iscsi_get_discovery_parent_name(int parent_type);
extern struct device *
iscsi_find_flashnode(struct Scsi_Host *shost, void *data,
		     int (*fn)(struct device *dev, void *data));

extern struct iscsi_bus_flash_session *
iscsi_create_flashnode_sess(struct Scsi_Host *shost, int index,
			    struct iscsi_transport *transport, int dd_size);

extern struct iscsi_bus_flash_conn *
iscsi_create_flashnode_conn(struct Scsi_Host *shost,
			    struct iscsi_bus_flash_session *fnode_sess,
			    struct iscsi_transport *transport, int dd_size);

extern void
iscsi_destroy_flashnode_sess(struct iscsi_bus_flash_session *fnode_sess);

extern void iscsi_destroy_all_flashnode(struct Scsi_Host *shost);
extern int iscsi_flashnode_bus_match(struct device *dev,
				     struct device_driver *drv);
extern struct device *
iscsi_find_flashnode_sess(struct Scsi_Host *shost, void *data,
			  int (*fn)(struct device *dev, void *data));
extern struct device *
iscsi_find_flashnode_conn(struct iscsi_bus_flash_session *fnode_sess);

extern char *
iscsi_get_ipaddress_state_name(enum iscsi_ipaddress_state port_state);
extern char *iscsi_get_router_state_name(enum iscsi_router_state router_state);
#endif
