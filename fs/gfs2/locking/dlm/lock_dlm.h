/*
 * Copyright (C) Sistina Software, Inc.  1997-2003 All rights reserved.
 * Copyright (C) 2004-2005 Red Hat, Inc.  All rights reserved.
 *
 * This copyrighted material is made available to anyone wishing to use,
 * modify, copy, or redistribute it subject to the terms and conditions
 * of the GNU General Public License version 2.
 */

#ifndef LOCK_DLM_DOT_H
#define LOCK_DLM_DOT_H

#include <linux/module.h>
#include <linux/slab.h>
#include <linux/spinlock.h>
#include <linux/types.h>
#include <linux/string.h>
#include <linux/list.h>
#include <linux/socket.h>
#include <linux/delay.h>
#include <linux/kthread.h>
#include <linux/kobject.h>
#include <linux/fcntl.h>
#include <linux/wait.h>
#include <net/sock.h>

#include <linux/dlm.h>
#include <linux/dlm_plock.h>
#include <linux/lm_interface.h>

/*
 * Internally, we prefix things with gdlm_ and GDLM_ (for gfs-dlm) since a
 * prefix of lock_dlm_ gets awkward.  Externally, GFS refers to this module
 * as "lock_dlm".
 */

#define GDLM_STRNAME_BYTES	24
#define GDLM_LVB_SIZE		32
#define GDLM_DROP_COUNT		0
#define GDLM_DROP_PERIOD	60
#define GDLM_NAME_LEN		128

/* GFS uses 12 bytes to identify a resource (32 bit type + 64 bit number).
   We sprintf these numbers into a 24 byte string of hex values to make them
   human-readable (to make debugging simpler.) */

struct gdlm_strname {
	unsigned char		name[GDLM_STRNAME_BYTES];
	unsigned short		namelen;
};

enum {
	DFL_BLOCK_LOCKS		= 0,
	DFL_SPECTATOR		= 1,
	DFL_WITHDRAW		= 2,
};

struct gdlm_ls {
	u32		id;
	int			jid;
	int			first;
	int			first_done;
	unsigned long		flags;
	struct kobject		kobj;
	char			clustername[GDLM_NAME_LEN];
	char			fsname[GDLM_NAME_LEN];
	int			fsflags;
	dlm_lockspace_t		*dlm_lockspace;
	lm_callback_t		fscb;
	struct gfs2_sbd		*sdp;
	int			recover_jid;
	int			recover_jid_done;
	int			recover_jid_status;
	spinlock_t		async_lock;
	struct list_head	complete;
	struct list_head	blocking;
	struct list_head	delayed;
	struct list_head	submit;
	struct list_head	all_locks;
	u32		all_locks_count;
	wait_queue_head_t	wait_control;
	struct task_struct	*thread1;
	struct task_struct	*thread2;
	wait_queue_head_t	thread_wait;
	unsigned long		drop_time;
	int			drop_locks_count;
	int			drop_locks_period;
};

enum {
	LFL_NOBLOCK		= 0,
	LFL_NOCACHE		= 1,
	LFL_DLM_UNLOCK		= 2,
	LFL_DLM_CANCEL		= 3,
	LFL_SYNC_LVB		= 4,
	LFL_FORCE_PROMOTE	= 5,
	LFL_REREQUEST		= 6,
	LFL_ACTIVE		= 7,
	LFL_INLOCK		= 8,
	LFL_CANCEL		= 9,
	LFL_NOBAST		= 10,
	LFL_HEADQUE		= 11,
	LFL_UNLOCK_DELETE	= 12,
	LFL_AST_WAIT		= 13,
};

struct gdlm_lock {
	struct gdlm_ls		*ls;
	struct lm_lockname	lockname;
	struct gdlm_strname	strname;
	char			*lvb;
	struct dlm_lksb		lksb;

	s16			cur;
	s16			req;
	s16			prev_req;
	u32			lkf;		/* dlm flags DLM_LKF_ */
	unsigned long		flags;		/* lock_dlm flags LFL_ */

	int			bast_mode;	/* protected by async_lock */

	struct list_head	clist;		/* complete */
	struct list_head	blist;		/* blocking */
	struct list_head	delay_list;	/* delayed */
	struct list_head	all_list;	/* all locks for the fs */
	struct gdlm_lock	*hold_null;	/* NL lock for hold_lvb */
};

#define gdlm_assert(assertion, fmt, args...)                                  \
do {                                                                          \
	if (unlikely(!(assertion))) {                                         \
		printk(KERN_EMERG "lock_dlm: fatal assertion failed \"%s\"\n" \
				  "lock_dlm:  " fmt "\n",                     \
				  #assertion, ##args);                        \
		BUG();                                                        \
	}                                                                     \
} while (0)

#define log_print(lev, fmt, arg...) printk(lev "lock_dlm: " fmt "\n" , ## arg)
#define log_info(fmt, arg...)  log_print(KERN_INFO , fmt , ## arg)
#define log_error(fmt, arg...) log_print(KERN_ERR , fmt , ## arg)
#ifdef LOCK_DLM_LOG_DEBUG
#define log_debug(fmt, arg...) log_print(KERN_DEBUG , fmt , ## arg)
#else
#define log_debug(fmt, arg...)
#endif

/* sysfs.c */

int gdlm_sysfs_init(void);
void gdlm_sysfs_exit(void);
int gdlm_kobject_setup(struct gdlm_ls *, struct kobject *);
void gdlm_kobject_release(struct gdlm_ls *);

/* thread.c */

int gdlm_init_threads(struct gdlm_ls *);
void gdlm_release_threads(struct gdlm_ls *);

/* lock.c */

s16 gdlm_make_lmstate(s16);
void gdlm_queue_delayed(struct gdlm_lock *);
void gdlm_submit_delayed(struct gdlm_ls *);
int gdlm_release_all_locks(struct gdlm_ls *);
void gdlm_delete_lp(struct gdlm_lock *);
unsigned int gdlm_do_lock(struct gdlm_lock *);

int gdlm_get_lock(void *, struct lm_lockname *, void **);
void gdlm_put_lock(void *);
unsigned int gdlm_lock(void *, unsigned int, unsigned int, unsigned int);
unsigned int gdlm_unlock(void *, unsigned int);
void gdlm_cancel(void *);
int gdlm_hold_lvb(void *, char **);
void gdlm_unhold_lvb(void *, char *);

/* mount.c */

extern const struct lm_lockops gdlm_ops;

#endif

