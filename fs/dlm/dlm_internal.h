/* SPDX-License-Identifier: GPL-2.0-only */
/******************************************************************************
*******************************************************************************
**
**  Copyright (C) Sistina Software, Inc.  1997-2003  All rights reserved.
**  Copyright (C) 2004-2011 Red Hat, Inc.  All rights reserved.
**
**
*******************************************************************************
******************************************************************************/

#ifndef __DLM_INTERNAL_DOT_H__
#define __DLM_INTERNAL_DOT_H__

/*
 * This is the main header file to be included in each DLM source file.
 */

#include <linux/slab.h>
#include <linux/sched.h>
#include <linux/types.h>
#include <linux/ctype.h>
#include <linux/spinlock.h>
#include <linux/vmalloc.h>
#include <linux/list.h>
#include <linux/errno.h>
#include <linux/random.h>
#include <linux/delay.h>
#include <linux/socket.h>
#include <linux/kthread.h>
#include <linux/kobject.h>
#include <linux/kref.h>
#include <linux/kernel.h>
#include <linux/jhash.h>
#include <linux/miscdevice.h>
#include <linux/mutex.h>
#include <linux/idr.h>
#include <linux/ratelimit.h>
#include <linux/uaccess.h>

#include <linux/dlm.h>
#include "config.h"

/* Size of the temp buffer midcomms allocates on the stack.
   We try to make this large enough so most messages fit.
   FIXME: should sctp make this unnecessary? */

#define DLM_INBUF_LEN		148

struct dlm_ls;
struct dlm_lkb;
struct dlm_rsb;
struct dlm_member;
struct dlm_rsbtable;
struct dlm_recover;
struct dlm_header;
struct dlm_message;
struct dlm_rcom;
struct dlm_mhandle;

#define log_print(fmt, args...) \
	printk(KERN_ERR "dlm: "fmt"\n" , ##args)
#define log_error(ls, fmt, args...) \
	printk(KERN_ERR "dlm: %s: " fmt "\n", (ls)->ls_name , ##args)

#define log_rinfo(ls, fmt, args...) \
do { \
	if (dlm_config.ci_log_info) \
		printk(KERN_INFO "dlm: %s: " fmt "\n", \
			(ls)->ls_name, ##args); \
	else if (dlm_config.ci_log_debug) \
		printk(KERN_DEBUG "dlm: %s: " fmt "\n", \
		       (ls)->ls_name , ##args); \
} while (0)

#define log_debug(ls, fmt, args...) \
do { \
	if (dlm_config.ci_log_debug) \
		printk(KERN_DEBUG "dlm: %s: " fmt "\n", \
		       (ls)->ls_name , ##args); \
} while (0)

#define log_limit(ls, fmt, args...) \
do { \
	if (dlm_config.ci_log_debug) \
		printk_ratelimited(KERN_DEBUG "dlm: %s: " fmt "\n", \
			(ls)->ls_name , ##args); \
} while (0)

#define DLM_ASSERT(x, do) \
{ \
  if (!(x)) \
  { \
    printk(KERN_ERR "\nDLM:  Assertion failed on line %d of file %s\n" \
               "DLM:  assertion:  \"%s\"\n" \
               "DLM:  time = %lu\n", \
               __LINE__, __FILE__, #x, jiffies); \
    {do} \
    printk("\n"); \
    BUG(); \
    panic("DLM:  Record message above and reboot.\n"); \
  } \
}


#define DLM_RTF_SHRINK		0x00000001

struct dlm_rsbtable {
	struct rb_root		keep;
	struct rb_root		toss;
	spinlock_t		lock;
	uint32_t		flags;
};


/*
 * Lockspace member (per node in a ls)
 */

struct dlm_member {
	struct list_head	list;
	int			nodeid;
	int			weight;
	int			slot;
	int			slot_prev;
	int			comm_seq;
	uint32_t		generation;
};

/*
 * Save and manage recovery state for a lockspace.
 */

struct dlm_recover {
	struct list_head	list;
	struct dlm_config_node	*nodes;
	int			nodes_count;
	uint64_t		seq;
};

/*
 * Pass input args to second stage locking function.
 */

struct dlm_args {
	uint32_t		flags;
	void			(*astfn) (void *astparam);
	void			*astparam;
	void			(*bastfn) (void *astparam, int mode);
	int			mode;
	struct dlm_lksb		*lksb;
	unsigned long		timeout;
};


/*
 * Lock block
 *
 * A lock can be one of three types:
 *
 * local copy      lock is mastered locally
 *                 (lkb_nodeid is zero and DLM_LKF_MSTCPY is not set)
 * process copy    lock is mastered on a remote node
 *                 (lkb_nodeid is non-zero and DLM_LKF_MSTCPY is not set)
 * master copy     master node's copy of a lock owned by remote node
 *                 (lkb_nodeid is non-zero and DLM_LKF_MSTCPY is set)
 *
 * lkb_exflags: a copy of the most recent flags arg provided to dlm_lock or
 * dlm_unlock.  The dlm does not modify these or use any private flags in
 * this field; it only contains DLM_LKF_ flags from dlm.h.  These flags
 * are sent as-is to the remote master when the lock is remote.
 *
 * lkb_flags: internal dlm flags (DLM_IFL_ prefix) from dlm_internal.h.
 * Some internal flags are shared between the master and process nodes;
 * these shared flags are kept in the lower two bytes.  One of these
 * flags set on the master copy will be propagated to the process copy
 * and v.v.  Other internal flags are private to the master or process
 * node (e.g. DLM_IFL_MSTCPY).  These are kept in the high two bytes.
 *
 * lkb_sbflags: status block flags.  These flags are copied directly into
 * the caller's lksb.sb_flags prior to the dlm_lock/dlm_unlock completion
 * ast.  All defined in dlm.h with DLM_SBF_ prefix.
 *
 * lkb_status: the lock status indicates which rsb queue the lock is
 * on, grant, convert, or wait.  DLM_LKSTS_ WAITING/GRANTED/CONVERT
 *
 * lkb_wait_type: the dlm message type (DLM_MSG_ prefix) for which a
 * reply is needed.  Only set when the lkb is on the lockspace waiters
 * list awaiting a reply from a remote node.
 *
 * lkb_nodeid: when the lkb is a local copy, nodeid is 0; when the lkb
 * is a master copy, nodeid specifies the remote lock holder, when the
 * lkb is a process copy, the nodeid specifies the lock master.
 */

/* lkb_status */

#define DLM_LKSTS_WAITING	1
#define DLM_LKSTS_GRANTED	2
#define DLM_LKSTS_CONVERT	3

/* lkb_flags */

#define DLM_IFL_MSTCPY		0x00010000
#define DLM_IFL_RESEND		0x00020000
#define DLM_IFL_DEAD		0x00040000
#define DLM_IFL_OVERLAP_UNLOCK  0x00080000
#define DLM_IFL_OVERLAP_CANCEL  0x00100000
#define DLM_IFL_ENDOFLIFE	0x00200000
#define DLM_IFL_WATCH_TIMEWARN	0x00400000
#define DLM_IFL_TIMEOUT_CANCEL	0x00800000
#define DLM_IFL_DEADLOCK_CANCEL	0x01000000
#define DLM_IFL_STUB_MS		0x02000000 /* magic number for m_flags */
#define DLM_IFL_USER		0x00000001
#define DLM_IFL_ORPHAN		0x00000002

#define DLM_CALLBACKS_SIZE	6

#define DLM_CB_CAST		0x00000001
#define DLM_CB_BAST		0x00000002
#define DLM_CB_SKIP		0x00000004

struct dlm_callback {
	uint64_t		seq;
	uint32_t		flags;		/* DLM_CBF_ */
	int			sb_status;	/* copy to lksb status */
	uint8_t			sb_flags;	/* copy to lksb flags */
	int8_t			mode; /* rq mode of bast, gr mode of cast */
};

struct dlm_lkb {
	struct dlm_rsb		*lkb_resource;	/* the rsb */
	struct kref		lkb_ref;
	int			lkb_nodeid;	/* copied from rsb */
	int			lkb_ownpid;	/* pid of lock owner */
	uint32_t		lkb_id;		/* our lock ID */
	uint32_t		lkb_remid;	/* lock ID on remote partner */
	uint32_t		lkb_exflags;	/* external flags from caller */
	uint32_t		lkb_sbflags;	/* lksb flags */
	uint32_t		lkb_flags;	/* internal flags */
	uint32_t		lkb_lvbseq;	/* lvb sequence number */

	int8_t			lkb_status;     /* granted, waiting, convert */
	int8_t			lkb_rqmode;	/* requested lock mode */
	int8_t			lkb_grmode;	/* granted lock mode */
	int8_t			lkb_highbast;	/* highest mode bast sent for */

	int8_t			lkb_wait_type;	/* type of reply waiting for */
	int8_t			lkb_wait_count;
	int			lkb_wait_nodeid; /* for debugging */

	struct list_head	lkb_statequeue;	/* rsb g/c/w list */
	struct list_head	lkb_rsb_lookup;	/* waiting for rsb lookup */
	struct list_head	lkb_wait_reply;	/* waiting for remote reply */
	struct list_head	lkb_ownqueue;	/* list of locks for a process */
	struct list_head	lkb_time_list;
	ktime_t			lkb_timestamp;
	ktime_t			lkb_wait_time;
	unsigned long		lkb_timeout_cs;

	struct mutex		lkb_cb_mutex;
	struct work_struct	lkb_cb_work;
	struct list_head	lkb_cb_list; /* for ls_cb_delay or proc->asts */
	struct dlm_callback	lkb_callbacks[DLM_CALLBACKS_SIZE];
	struct dlm_callback	lkb_last_cast;
	struct dlm_callback	lkb_last_bast;
	ktime_t			lkb_last_cast_time;	/* for debugging */
	ktime_t			lkb_last_bast_time;	/* for debugging */

	uint64_t		lkb_recover_seq; /* from ls_recover_seq */

	char			*lkb_lvbptr;
	struct dlm_lksb		*lkb_lksb;      /* caller's status block */
	void			(*lkb_astfn) (void *astparam);
	void			(*lkb_bastfn) (void *astparam, int mode);
	union {
		void			*lkb_astparam;	/* caller's ast arg */
		struct dlm_user_args	*lkb_ua;
	};
};

/*
 * res_master_nodeid is "normal": 0 is unset/invalid, non-zero is the real
 * nodeid, even when nodeid is our_nodeid.
 *
 * res_nodeid is "odd": -1 is unset/invalid, zero means our_nodeid,
 * greater than zero when another nodeid.
 *
 * (TODO: remove res_nodeid and only use res_master_nodeid)
 */

struct dlm_rsb {
	struct dlm_ls		*res_ls;	/* the lockspace */
	struct kref		res_ref;
	struct mutex		res_mutex;
	unsigned long		res_flags;
	int			res_length;	/* length of rsb name */
	int			res_nodeid;
	int			res_master_nodeid;
	int			res_dir_nodeid;
	int			res_id;		/* for ls_recover_idr */
	uint32_t                res_lvbseq;
	uint32_t		res_hash;
	uint32_t		res_bucket;	/* rsbtbl */
	unsigned long		res_toss_time;
	uint32_t		res_first_lkid;
	struct list_head	res_lookup;	/* lkbs waiting on first */
	union {
		struct list_head	res_hashchain;
		struct rb_node		res_hashnode;	/* rsbtbl */
	};
	struct list_head	res_grantqueue;
	struct list_head	res_convertqueue;
	struct list_head	res_waitqueue;

	struct list_head	res_root_list;	    /* used for recovery */
	struct list_head	res_recover_list;   /* used for recovery */
	int			res_recover_locks_count;

	char			*res_lvbptr;
	char			res_name[DLM_RESNAME_MAXLEN+1];
};

/* dlm_master_lookup() flags */

#define DLM_LU_RECOVER_DIR	1
#define DLM_LU_RECOVER_MASTER	2

/* dlm_master_lookup() results */

#define DLM_LU_MATCH		1
#define DLM_LU_ADD		2

/* find_rsb() flags */

#define R_REQUEST		0x00000001
#define R_RECEIVE_REQUEST	0x00000002
#define R_RECEIVE_RECOVER	0x00000004

/* rsb_flags */

enum rsb_flags {
	RSB_MASTER_UNCERTAIN,
	RSB_VALNOTVALID,
	RSB_VALNOTVALID_PREV,
	RSB_NEW_MASTER,
	RSB_NEW_MASTER2,
	RSB_RECOVER_CONVERT,
	RSB_RECOVER_GRANT,
	RSB_RECOVER_LVB_INVAL,
};

static inline void rsb_set_flag(struct dlm_rsb *r, enum rsb_flags flag)
{
	__set_bit(flag, &r->res_flags);
}

static inline void rsb_clear_flag(struct dlm_rsb *r, enum rsb_flags flag)
{
	__clear_bit(flag, &r->res_flags);
}

static inline int rsb_flag(struct dlm_rsb *r, enum rsb_flags flag)
{
	return test_bit(flag, &r->res_flags);
}


/* dlm_header is first element of all structs sent between nodes */

#define DLM_HEADER_MAJOR	0x00030000
#define DLM_HEADER_MINOR	0x00000001

#define DLM_HEADER_SLOTS	0x00000001

#define DLM_MSG			1
#define DLM_RCOM		2

struct dlm_header {
	uint32_t		h_version;
	uint32_t		h_lockspace;
	uint32_t		h_nodeid;	/* nodeid of sender */
	uint16_t		h_length;
	uint8_t			h_cmd;		/* DLM_MSG, DLM_RCOM */
	uint8_t			h_pad;
};


#define DLM_MSG_REQUEST		1
#define DLM_MSG_CONVERT		2
#define DLM_MSG_UNLOCK		3
#define DLM_MSG_CANCEL		4
#define DLM_MSG_REQUEST_REPLY	5
#define DLM_MSG_CONVERT_REPLY	6
#define DLM_MSG_UNLOCK_REPLY	7
#define DLM_MSG_CANCEL_REPLY	8
#define DLM_MSG_GRANT		9
#define DLM_MSG_BAST		10
#define DLM_MSG_LOOKUP		11
#define DLM_MSG_REMOVE		12
#define DLM_MSG_LOOKUP_REPLY	13
#define DLM_MSG_PURGE		14

struct dlm_message {
	struct dlm_header	m_header;
	uint32_t		m_type;		/* DLM_MSG_ */
	uint32_t		m_nodeid;
	uint32_t		m_pid;
	uint32_t		m_lkid;		/* lkid on sender */
	uint32_t		m_remid;	/* lkid on receiver */
	uint32_t		m_parent_lkid;
	uint32_t		m_parent_remid;
	uint32_t		m_exflags;
	uint32_t		m_sbflags;
	uint32_t		m_flags;
	uint32_t		m_lvbseq;
	uint32_t		m_hash;
	int			m_status;
	int			m_grmode;
	int			m_rqmode;
	int			m_bastmode;
	int			m_asts;
	int			m_result;	/* 0 or -EXXX */
	char			m_extra[0];	/* name or lvb */
};


#define DLM_RS_NODES		0x00000001
#define DLM_RS_NODES_ALL	0x00000002
#define DLM_RS_DIR		0x00000004
#define DLM_RS_DIR_ALL		0x00000008
#define DLM_RS_LOCKS		0x00000010
#define DLM_RS_LOCKS_ALL	0x00000020
#define DLM_RS_DONE		0x00000040
#define DLM_RS_DONE_ALL		0x00000080

#define DLM_RCOM_STATUS		1
#define DLM_RCOM_NAMES		2
#define DLM_RCOM_LOOKUP		3
#define DLM_RCOM_LOCK		4
#define DLM_RCOM_STATUS_REPLY	5
#define DLM_RCOM_NAMES_REPLY	6
#define DLM_RCOM_LOOKUP_REPLY	7
#define DLM_RCOM_LOCK_REPLY	8

struct dlm_rcom {
	struct dlm_header	rc_header;
	uint32_t		rc_type;	/* DLM_RCOM_ */
	int			rc_result;	/* multi-purpose */
	uint64_t		rc_id;		/* match reply with request */
	uint64_t		rc_seq;		/* sender's ls_recover_seq */
	uint64_t		rc_seq_reply;	/* remote ls_recover_seq */
	char			rc_buf[0];
};

union dlm_packet {
	struct dlm_header	header;		/* common to other two */
	struct dlm_message	message;
	struct dlm_rcom		rcom;
};

#define DLM_RSF_NEED_SLOTS	0x00000001

/* RCOM_STATUS data */
struct rcom_status {
	__le32			rs_flags;
	__le32			rs_unused1;
	__le64			rs_unused2;
};

/* RCOM_STATUS_REPLY data */
struct rcom_config {
	__le32			rf_lvblen;
	__le32			rf_lsflags;

	/* DLM_HEADER_SLOTS adds: */
	__le32			rf_flags;
	__le16			rf_our_slot;
	__le16			rf_num_slots;
	__le32			rf_generation;
	__le32			rf_unused1;
	__le64			rf_unused2;
};

struct rcom_slot {
	__le32			ro_nodeid;
	__le16			ro_slot;
	__le16			ro_unused1;
	__le64			ro_unused2;
};

struct rcom_lock {
	__le32			rl_ownpid;
	__le32			rl_lkid;
	__le32			rl_remid;
	__le32			rl_parent_lkid;
	__le32			rl_parent_remid;
	__le32			rl_exflags;
	__le32			rl_flags;
	__le32			rl_lvbseq;
	__le32			rl_result;
	int8_t			rl_rqmode;
	int8_t			rl_grmode;
	int8_t			rl_status;
	int8_t			rl_asts;
	__le16			rl_wait_type;
	__le16			rl_namelen;
	char			rl_name[DLM_RESNAME_MAXLEN];
	char			rl_lvb[0];
};

/*
 * The max number of resources per rsbtbl bucket that shrink will attempt
 * to remove in each iteration.
 */

#define DLM_REMOVE_NAMES_MAX 8

struct dlm_ls {
	struct list_head	ls_list;	/* list of lockspaces */
	dlm_lockspace_t		*ls_local_handle;
	uint32_t		ls_global_id;	/* global unique lockspace ID */
	uint32_t		ls_generation;
	uint32_t		ls_exflags;
	int			ls_lvblen;
	int			ls_count;	/* refcount of processes in
						   the dlm using this ls */
	int			ls_create_count; /* create/release refcount */
	unsigned long		ls_flags;	/* LSFL_ */
	unsigned long		ls_scan_time;
	struct kobject		ls_kobj;

	struct idr		ls_lkbidr;
	spinlock_t		ls_lkbidr_spin;

	struct dlm_rsbtable	*ls_rsbtbl;
	uint32_t		ls_rsbtbl_size;

	struct mutex		ls_waiters_mutex;
	struct list_head	ls_waiters;	/* lkbs needing a reply */

	struct mutex		ls_orphans_mutex;
	struct list_head	ls_orphans;

	struct mutex		ls_timeout_mutex;
	struct list_head	ls_timeout;

	spinlock_t		ls_new_rsb_spin;
	int			ls_new_rsb_count;
	struct list_head	ls_new_rsb;	/* new rsb structs */

	spinlock_t		ls_remove_spin;
	char			ls_remove_name[DLM_RESNAME_MAXLEN+1];
	char			*ls_remove_names[DLM_REMOVE_NAMES_MAX];
	int			ls_remove_len;
	int			ls_remove_lens[DLM_REMOVE_NAMES_MAX];

	struct list_head	ls_nodes;	/* current nodes in ls */
	struct list_head	ls_nodes_gone;	/* dead node list, recovery */
	int			ls_num_nodes;	/* number of nodes in ls */
	int			ls_low_nodeid;
	int			ls_total_weight;
	int			*ls_node_array;

	int			ls_slot;
	int			ls_num_slots;
	int			ls_slots_size;
	struct dlm_slot		*ls_slots;

	struct dlm_rsb		ls_stub_rsb;	/* for returning errors */
	struct dlm_lkb		ls_stub_lkb;	/* for returning errors */
	struct dlm_message	ls_stub_ms;	/* for faking a reply */

	struct dentry		*ls_debug_rsb_dentry; /* debugfs */
	struct dentry		*ls_debug_waiters_dentry; /* debugfs */
	struct dentry		*ls_debug_locks_dentry; /* debugfs */
	struct dentry		*ls_debug_all_dentry; /* debugfs */
	struct dentry		*ls_debug_toss_dentry; /* debugfs */

	wait_queue_head_t	ls_uevent_wait;	/* user part of join/leave */
	int			ls_uevent_result;
	struct completion	ls_members_done;
	int			ls_members_result;

	struct miscdevice       ls_device;

	struct workqueue_struct	*ls_callback_wq;

	/* recovery related */

	struct mutex		ls_cb_mutex;
	struct list_head	ls_cb_delay; /* save for queue_work later */
	struct timer_list	ls_timer;
	struct task_struct	*ls_recoverd_task;
	struct mutex		ls_recoverd_active;
	spinlock_t		ls_recover_lock;
	unsigned long		ls_recover_begin; /* jiffies timestamp */
	uint32_t		ls_recover_status; /* DLM_RS_ */
	uint64_t		ls_recover_seq;
	struct dlm_recover	*ls_recover_args;
	struct rw_semaphore	ls_in_recovery;	/* block local requests */
	struct rw_semaphore	ls_recv_active;	/* block dlm_recv */
	struct list_head	ls_requestqueue;/* queue remote requests */
	struct mutex		ls_requestqueue_mutex;
	struct dlm_rcom		*ls_recover_buf;
	int			ls_recover_nodeid; /* for debugging */
	unsigned int		ls_recover_dir_sent_res; /* for log info */
	unsigned int		ls_recover_dir_sent_msg; /* for log info */
	unsigned int		ls_recover_locks_in; /* for log info */
	uint64_t		ls_rcom_seq;
	spinlock_t		ls_rcom_spin;
	struct list_head	ls_recover_list;
	spinlock_t		ls_recover_list_lock;
	int			ls_recover_list_count;
	struct idr		ls_recover_idr;
	spinlock_t		ls_recover_idr_lock;
	wait_queue_head_t	ls_wait_general;
	wait_queue_head_t	ls_recover_lock_wait;
	struct mutex		ls_clear_proc_locks;

	struct list_head	ls_root_list;	/* root resources */
	struct rw_semaphore	ls_root_sem;	/* protect root_list */

	const struct dlm_lockspace_ops *ls_ops;
	void			*ls_ops_arg;

	int			ls_namelen;
	char			ls_name[1];
};

/*
 * LSFL_RECOVER_STOP - dlm_ls_stop() sets this to tell dlm recovery routines
 * that they should abort what they're doing so new recovery can be started.
 *
 * LSFL_RECOVER_DOWN - dlm_ls_stop() sets this to tell dlm_recoverd that it
 * should do down_write() on the in_recovery rw_semaphore. (doing down_write
 * within dlm_ls_stop causes complaints about the lock acquired/released
 * in different contexts.)
 *
 * LSFL_RECOVER_LOCK - dlm_recoverd holds the in_recovery rw_semaphore.
 * It sets this after it is done with down_write() on the in_recovery
 * rw_semaphore and clears it after it has released the rw_semaphore.
 *
 * LSFL_RECOVER_WORK - dlm_ls_start() sets this to tell dlm_recoverd that it
 * should begin recovery of the lockspace.
 *
 * LSFL_RUNNING - set when normal locking activity is enabled.
 * dlm_ls_stop() clears this to tell dlm locking routines that they should
 * quit what they are doing so recovery can run.  dlm_recoverd sets
 * this after recovery is finished.
 */

#define LSFL_RECOVER_STOP	0
#define LSFL_RECOVER_DOWN	1
#define LSFL_RECOVER_LOCK	2
#define LSFL_RECOVER_WORK	3
#define LSFL_RUNNING		4

#define LSFL_RCOM_READY		5
#define LSFL_RCOM_WAIT		6
#define LSFL_UEVENT_WAIT	7
#define LSFL_TIMEWARN		8
#define LSFL_CB_DELAY		9
#define LSFL_NODIR		10

/* much of this is just saving user space pointers associated with the
   lock that we pass back to the user lib with an ast */

struct dlm_user_args {
	struct dlm_user_proc	*proc; /* each process that opens the lockspace
					  device has private data
					  (dlm_user_proc) on the struct file,
					  the process's locks point back to it*/
	struct dlm_lksb		lksb;
	struct dlm_lksb __user	*user_lksb;
	void __user		*castparam;
	void __user		*castaddr;
	void __user		*bastparam;
	void __user		*bastaddr;
	uint64_t		xid;
};

#define DLM_PROC_FLAGS_CLOSING 1
#define DLM_PROC_FLAGS_COMPAT  2

/* locks list is kept so we can remove all a process's locks when it
   exits (or orphan those that are persistent) */

struct dlm_user_proc {
	dlm_lockspace_t		*lockspace;
	unsigned long		flags; /* DLM_PROC_FLAGS */
	struct list_head	asts;
	spinlock_t		asts_spin;
	struct list_head	locks;
	spinlock_t		locks_spin;
	struct list_head	unlocking;
	wait_queue_head_t	wait;
};

static inline int dlm_locking_stopped(struct dlm_ls *ls)
{
	return !test_bit(LSFL_RUNNING, &ls->ls_flags);
}

static inline int dlm_recovery_stopped(struct dlm_ls *ls)
{
	return test_bit(LSFL_RECOVER_STOP, &ls->ls_flags);
}

static inline int dlm_no_directory(struct dlm_ls *ls)
{
	return test_bit(LSFL_NODIR, &ls->ls_flags);
}

int dlm_netlink_init(void);
void dlm_netlink_exit(void);
void dlm_timeout_warn(struct dlm_lkb *lkb);
int dlm_plock_init(void);
void dlm_plock_exit(void);

#ifdef CONFIG_DLM_DEBUG
void dlm_register_debugfs(void);
void dlm_unregister_debugfs(void);
void dlm_create_debug_file(struct dlm_ls *ls);
void dlm_delete_debug_file(struct dlm_ls *ls);
#else
static inline void dlm_register_debugfs(void) { }
static inline void dlm_unregister_debugfs(void) { }
static inline void dlm_create_debug_file(struct dlm_ls *ls) { }
static inline void dlm_delete_debug_file(struct dlm_ls *ls) { }
#endif

#endif				/* __DLM_INTERNAL_DOT_H__ */

