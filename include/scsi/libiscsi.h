/*
 * iSCSI lib definitions
 *
 * Copyright (C) 2006 Red Hat, Inc.  All rights reserved.
 * Copyright (C) 2004 - 2006 Mike Christie
 * Copyright (C) 2004 - 2005 Dmitry Yusupov
 * Copyright (C) 2004 - 2005 Alex Aizman
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
#ifndef LIBISCSI_H
#define LIBISCSI_H

#include <linux/types.h>
#include <linux/mutex.h>
#include <scsi/iscsi_proto.h>
#include <scsi/iscsi_if.h>

struct scsi_transport_template;
struct scsi_device;
struct Scsi_Host;
struct scsi_cmnd;
struct socket;
struct iscsi_transport;
struct iscsi_cls_session;
struct iscsi_cls_conn;
struct iscsi_session;
struct iscsi_nopin;

/* #define DEBUG_SCSI */
#ifdef DEBUG_SCSI
#define debug_scsi(fmt...) printk(KERN_INFO "iscsi: " fmt)
#else
#define debug_scsi(fmt...)
#endif

#define ISCSI_XMIT_CMDS_MAX	128	/* must be power of 2 */
#define ISCSI_MGMT_CMDS_MAX	32	/* must be power of 2 */
#define ISCSI_CONN_MAX			1

#define ISCSI_MGMT_ITT_OFFSET	0xa00

#define ISCSI_DEF_CMD_PER_LUN		32
#define ISCSI_MAX_CMD_PER_LUN		128

/* Task Mgmt states */
#define TMABORT_INITIAL			0x0
#define TMABORT_SUCCESS			0x1
#define TMABORT_FAILED			0x2
#define TMABORT_TIMEDOUT		0x3

/* Connection suspend "bit" */
#define ISCSI_SUSPEND_BIT		1

#define ISCSI_ITT_MASK			(0xfff)
#define ISCSI_CID_SHIFT			12
#define ISCSI_CID_MASK			(0xffff << ISCSI_CID_SHIFT)
#define ISCSI_AGE_SHIFT			28
#define ISCSI_AGE_MASK			(0xf << ISCSI_AGE_SHIFT)

struct iscsi_mgmt_task {
	/*
	 * Becuae LLDs allocate their hdr differently, this is a pointer to
	 * that storage. It must be setup at session creation time.
	 */
	struct iscsi_hdr	*hdr;
	char			*data;		/* mgmt payload */
	int			data_count;	/* counts data to be sent */
	uint32_t		itt;		/* this ITT */
	void			*dd_data;	/* driver/transport data */
	struct list_head	running;
};

struct iscsi_cmd_task {
	/*
	 * Becuae LLDs allocate their hdr differently, this is a pointer to
	 * that storage. It must be setup at session creation time.
	 */
	struct iscsi_cmd	*hdr;
	int			itt;		/* this ITT */
	int			datasn;		/* DataSN */

	uint32_t		unsol_datasn;
	int			imm_count;	/* imm-data (bytes)   */
	int			unsol_count;	/* unsolicited (bytes)*/
	int			data_count;	/* remaining Data-Out */
	struct scsi_cmnd	*sc;		/* associated SCSI cmd*/
	int			total_length;
	struct iscsi_conn	*conn;		/* used connection    */
	struct iscsi_mgmt_task	*mtask;		/* tmf mtask in progr */

	struct list_head	running;	/* running cmd list */
	void			*dd_data;	/* driver/transport data */
};

struct iscsi_conn {
	struct iscsi_cls_conn	*cls_conn;	/* ptr to class connection */
	void			*dd_data;	/* iscsi_transport data */
	struct iscsi_session	*session;	/* parent session */
	/*
	 * LLDs should set this lock. It protects the transport recv
	 * code
	 */
	rwlock_t		*recv_lock;
	/*
	 * conn_stop() flag: stop to recover, stop to terminate
	 */
        int			stop_stage;

	/* iSCSI connection-wide sequencing */
	uint32_t		exp_statsn;

	/* control data */
	int			id;		/* CID */
	struct list_head	item;		/* maintains list of conns */
	int			c_stage;	/* connection state */
	struct iscsi_mgmt_task	*login_mtask;	/* mtask used for login/text */
	struct iscsi_mgmt_task	*mtask;		/* xmit mtask in progress */
	struct iscsi_cmd_task	*ctask;		/* xmit ctask in progress */

	/* xmit */
	struct kfifo		*immqueue;	/* immediate xmit queue */
	struct kfifo		*mgmtqueue;	/* mgmt (control) xmit queue */
	struct list_head	mgmt_run_list;	/* list of control tasks */
	struct kfifo		*xmitqueue;	/* data-path cmd queue */
	struct list_head	run_list;	/* list of cmds in progress */
	struct work_struct	xmitwork;	/* per-conn. xmit workqueue */
	/*
	 * serializes connection xmit, access to kfifos:
	 * xmitqueue, immqueue, mgmtqueue
	 */
	struct mutex		xmitmutex;

	unsigned long		suspend_tx;	/* suspend Tx */
	unsigned long		suspend_rx;	/* suspend Rx */

	/* abort */
	wait_queue_head_t	ehwait;		/* used in eh_abort() */
	struct iscsi_tm		tmhdr;
	struct timer_list	tmabort_timer;
	int			tmabort_state;	/* see TMABORT_INITIAL, etc.*/

	/* negotiated params */
	int			max_recv_dlength; /* initiator_max_recv_dsl*/
	int			max_xmit_dlength; /* target_max_recv_dsl */
	int			hdrdgst_en;
	int			datadgst_en;

	/* MIB-statistics */
	uint64_t		txdata_octets;
	uint64_t		rxdata_octets;
	uint32_t		scsicmd_pdus_cnt;
	uint32_t		dataout_pdus_cnt;
	uint32_t		scsirsp_pdus_cnt;
	uint32_t		datain_pdus_cnt;
	uint32_t		r2t_pdus_cnt;
	uint32_t		tmfcmd_pdus_cnt;
	int32_t			tmfrsp_pdus_cnt;

	/* custom statistics */
	uint32_t		eh_abort_cnt;
};

struct iscsi_queue {
	struct kfifo		*queue;		/* FIFO Queue */
	void			**pool;		/* Pool of elements */
	int			max;		/* Max number of elements */
};

struct iscsi_session {
	/* iSCSI session-wide sequencing */
	uint32_t		cmdsn;
	uint32_t		exp_cmdsn;
	uint32_t		max_cmdsn;

	/* configuration */
	int			initial_r2t_en;
	int			max_r2t;
	int			imm_data_en;
	int			first_burst;
	int			max_burst;
	int			time2wait;
	int			time2retain;
	int			pdu_inorder_en;
	int			dataseq_inorder_en;
	int			erl;
	int			ifmarker_en;
	int			ofmarker_en;

	/* control data */
	struct iscsi_transport	*tt;
	struct Scsi_Host	*host;
	struct iscsi_conn	*leadconn;	/* leading connection */
	spinlock_t		lock;		/* protects session state, *
						 * sequence numbers,       *
						 * session resources:      *
						 * - cmdpool,		   *
						 * - mgmtpool,		   *
						 * - r2tpool		   */
	int			state;		/* session state           */
	struct list_head	item;
	int			conn_cnt;
	int			age;		/* counts session re-opens */

	struct list_head	connections;	/* list of connections */
	int			cmds_max;	/* size of cmds array */
	struct iscsi_cmd_task	**cmds;		/* Original Cmds arr */
	struct iscsi_queue	cmdpool;	/* PDU's pool */
	int			mgmtpool_max;	/* size of mgmt array */
	struct iscsi_mgmt_task	**mgmt_cmds;	/* Original mgmt arr */
	struct iscsi_queue	mgmtpool;	/* Mgmt PDU's pool */
};

/*
 * scsi host template
 */
extern int iscsi_change_queue_depth(struct scsi_device *sdev, int depth);
extern int iscsi_eh_abort(struct scsi_cmnd *sc);
extern int iscsi_eh_host_reset(struct scsi_cmnd *sc);
extern int iscsi_queuecommand(struct scsi_cmnd *sc,
			      void (*done)(struct scsi_cmnd *));

/*
 * session management
 */
extern struct iscsi_cls_session *
iscsi_session_setup(struct iscsi_transport *, struct scsi_transport_template *,
		    int, int, uint32_t, uint32_t *);
extern void iscsi_session_teardown(struct iscsi_cls_session *);
extern struct iscsi_session *class_to_transport_session(struct iscsi_cls_session *);
extern void iscsi_session_recovery_timedout(struct iscsi_cls_session *);

#define session_to_cls(_sess) \
	hostdata_session(_sess->host->hostdata)

/*
 * connection management
 */
extern struct iscsi_cls_conn *iscsi_conn_setup(struct iscsi_cls_session *,
					       uint32_t);
extern void iscsi_conn_teardown(struct iscsi_cls_conn *);
extern int iscsi_conn_start(struct iscsi_cls_conn *);
extern void iscsi_conn_stop(struct iscsi_cls_conn *, int);
extern int iscsi_conn_bind(struct iscsi_cls_session *, struct iscsi_cls_conn *,
			   int);
extern void iscsi_conn_failure(struct iscsi_conn *conn, enum iscsi_err err);

/*
 * pdu and task processing
 */
extern int iscsi_check_assign_cmdsn(struct iscsi_session *,
				    struct iscsi_nopin *);
extern void iscsi_prep_unsolicit_data_pdu(struct iscsi_cmd_task *,
					struct iscsi_data *hdr,
					int transport_data_cnt);
extern int iscsi_conn_send_pdu(struct iscsi_cls_conn *, struct iscsi_hdr *,
				char *, uint32_t);
extern int iscsi_complete_pdu(struct iscsi_conn *, struct iscsi_hdr *,
			      char *, int);
extern int __iscsi_complete_pdu(struct iscsi_conn *, struct iscsi_hdr *,
				char *, int);
extern int iscsi_verify_itt(struct iscsi_conn *, struct iscsi_hdr *,
			    uint32_t *);

/*
 * generic helpers
 */
extern void iscsi_pool_free(struct iscsi_queue *, void **);
extern int iscsi_pool_init(struct iscsi_queue *, int, void ***, int);

#endif
