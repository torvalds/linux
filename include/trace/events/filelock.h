/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Events for filesystem locks
 *
 * Copyright 2013 Jeff Layton <jlayton@poochiereds.net>
 */
#undef TRACE_SYSTEM
#define TRACE_SYSTEM filelock

#if !defined(_TRACE_FILELOCK_H) || defined(TRACE_HEADER_MULTI_READ)
#define _TRACE_FILELOCK_H

#include <linux/tracepoint.h>
#include <linux/fs.h>
#include <linux/device.h>
#include <linux/kdev_t.h>

#define show_fl_flags(val)						\
	__print_flags(val, "|", 					\
		{ FL_POSIX,		"FL_POSIX" },			\
		{ FL_FLOCK,		"FL_FLOCK" },			\
		{ FL_DELEG,		"FL_DELEG" },			\
		{ FL_ACCESS,		"FL_ACCESS" },			\
		{ FL_EXISTS,		"FL_EXISTS" },			\
		{ FL_LEASE,		"FL_LEASE" },			\
		{ FL_CLOSE,		"FL_CLOSE" },			\
		{ FL_SLEEP,		"FL_SLEEP" },			\
		{ FL_DOWNGRADE_PENDING,	"FL_DOWNGRADE_PENDING" },	\
		{ FL_UNLOCK_PENDING,	"FL_UNLOCK_PENDING" },		\
		{ FL_OFDLCK,		"FL_OFDLCK" },			\
		{ FL_RECLAIM,		"FL_RECLAIM"})

#define show_fl_type(val)				\
	__print_symbolic(val,				\
			{ F_RDLCK, "F_RDLCK" },		\
			{ F_WRLCK, "F_WRLCK" },		\
			{ F_UNLCK, "F_UNLCK" })

TRACE_EVENT(locks_get_lock_context,
	TP_PROTO(struct inode *inode, int type, struct file_lock_context *ctx),

	TP_ARGS(inode, type, ctx),

	TP_STRUCT__entry(
		__field(unsigned long, i_ino)
		__field(dev_t, s_dev)
		__field(unsigned char, type)
		__field(struct file_lock_context *, ctx)
	),

	TP_fast_assign(
		__entry->s_dev = inode->i_sb->s_dev;
		__entry->i_ino = inode->i_ino;
		__entry->type = type;
		__entry->ctx = ctx;
	),

	TP_printk("dev=0x%x:0x%x ino=0x%lx type=%s ctx=%p",
		  MAJOR(__entry->s_dev), MINOR(__entry->s_dev),
		  __entry->i_ino, show_fl_type(__entry->type), __entry->ctx)
);

DECLARE_EVENT_CLASS(filelock_lock,
	TP_PROTO(struct inode *inode, struct file_lock *fl, int ret),

	TP_ARGS(inode, fl, ret),

	TP_STRUCT__entry(
		__field(struct file_lock *, fl)
		__field(unsigned long, i_ino)
		__field(dev_t, s_dev)
		__field(struct file_lock_core *, blocker)
		__field(fl_owner_t, owner)
		__field(unsigned int, pid)
		__field(unsigned int, flags)
		__field(unsigned char, type)
		__field(loff_t, fl_start)
		__field(loff_t, fl_end)
		__field(int, ret)
	),

	TP_fast_assign(
		__entry->fl = fl ? fl : NULL;
		__entry->s_dev = inode->i_sb->s_dev;
		__entry->i_ino = inode->i_ino;
		__entry->blocker = fl ? fl->c.flc_blocker : NULL;
		__entry->owner = fl ? fl->c.flc_owner : NULL;
		__entry->pid = fl ? fl->c.flc_pid : 0;
		__entry->flags = fl ? fl->c.flc_flags : 0;
		__entry->type = fl ? fl->c.flc_type : 0;
		__entry->fl_start = fl ? fl->fl_start : 0;
		__entry->fl_end = fl ? fl->fl_end : 0;
		__entry->ret = ret;
	),

	TP_printk("fl=%p dev=0x%x:0x%x ino=0x%lx fl_blocker=%p fl_owner=%p fl_pid=%u fl_flags=%s fl_type=%s fl_start=%lld fl_end=%lld ret=%d",
		__entry->fl, MAJOR(__entry->s_dev), MINOR(__entry->s_dev),
		__entry->i_ino, __entry->blocker, __entry->owner,
		__entry->pid, show_fl_flags(__entry->flags),
		show_fl_type(__entry->type),
		__entry->fl_start, __entry->fl_end, __entry->ret)
);

DEFINE_EVENT(filelock_lock, posix_lock_inode,
		TP_PROTO(struct inode *inode, struct file_lock *fl, int ret),
		TP_ARGS(inode, fl, ret));

DEFINE_EVENT(filelock_lock, fcntl_setlk,
		TP_PROTO(struct inode *inode, struct file_lock *fl, int ret),
		TP_ARGS(inode, fl, ret));

DEFINE_EVENT(filelock_lock, locks_remove_posix,
		TP_PROTO(struct inode *inode, struct file_lock *fl, int ret),
		TP_ARGS(inode, fl, ret));

DEFINE_EVENT(filelock_lock, flock_lock_inode,
		TP_PROTO(struct inode *inode, struct file_lock *fl, int ret),
		TP_ARGS(inode, fl, ret));

DECLARE_EVENT_CLASS(filelock_lease,
	TP_PROTO(struct inode *inode, struct file_lease *fl),

	TP_ARGS(inode, fl),

	TP_STRUCT__entry(
		__field(struct file_lease *, fl)
		__field(unsigned long, i_ino)
		__field(dev_t, s_dev)
		__field(struct file_lock_core *, blocker)
		__field(fl_owner_t, owner)
		__field(unsigned int, flags)
		__field(unsigned char, type)
		__field(unsigned long, break_time)
		__field(unsigned long, downgrade_time)
	),

	TP_fast_assign(
		__entry->fl = fl ? fl : NULL;
		__entry->s_dev = inode->i_sb->s_dev;
		__entry->i_ino = inode->i_ino;
		__entry->blocker = fl ? fl->c.flc_blocker : NULL;
		__entry->owner = fl ? fl->c.flc_owner : NULL;
		__entry->flags = fl ? fl->c.flc_flags : 0;
		__entry->type = fl ? fl->c.flc_type : 0;
		__entry->break_time = fl ? fl->fl_break_time : 0;
		__entry->downgrade_time = fl ? fl->fl_downgrade_time : 0;
	),

	TP_printk("fl=%p dev=0x%x:0x%x ino=0x%lx fl_blocker=%p fl_owner=%p fl_flags=%s fl_type=%s fl_break_time=%lu fl_downgrade_time=%lu",
		__entry->fl, MAJOR(__entry->s_dev), MINOR(__entry->s_dev),
		__entry->i_ino, __entry->blocker, __entry->owner,
		show_fl_flags(__entry->flags),
		show_fl_type(__entry->type),
		__entry->break_time, __entry->downgrade_time)
);

DEFINE_EVENT(filelock_lease, break_lease_noblock, TP_PROTO(struct inode *inode, struct file_lease *fl),
		TP_ARGS(inode, fl));

DEFINE_EVENT(filelock_lease, break_lease_block, TP_PROTO(struct inode *inode, struct file_lease *fl),
		TP_ARGS(inode, fl));

DEFINE_EVENT(filelock_lease, break_lease_unblock, TP_PROTO(struct inode *inode, struct file_lease *fl),
		TP_ARGS(inode, fl));

DEFINE_EVENT(filelock_lease, generic_delete_lease, TP_PROTO(struct inode *inode, struct file_lease *fl),
		TP_ARGS(inode, fl));

DEFINE_EVENT(filelock_lease, time_out_leases, TP_PROTO(struct inode *inode, struct file_lease *fl),
		TP_ARGS(inode, fl));

TRACE_EVENT(generic_add_lease,
	TP_PROTO(struct inode *inode, struct file_lease *fl),

	TP_ARGS(inode, fl),

	TP_STRUCT__entry(
		__field(unsigned long, i_ino)
		__field(int, wcount)
		__field(int, rcount)
		__field(int, icount)
		__field(dev_t, s_dev)
		__field(fl_owner_t, owner)
		__field(unsigned int, flags)
		__field(unsigned char, type)
	),

	TP_fast_assign(
		__entry->s_dev = inode->i_sb->s_dev;
		__entry->i_ino = inode->i_ino;
		__entry->wcount = atomic_read(&inode->i_writecount);
		__entry->rcount = atomic_read(&inode->i_readcount);
		__entry->icount = icount_read(inode);
		__entry->owner = fl->c.flc_owner;
		__entry->flags = fl->c.flc_flags;
		__entry->type = fl->c.flc_type;
	),

	TP_printk("dev=0x%x:0x%x ino=0x%lx wcount=%d rcount=%d icount=%d fl_owner=%p fl_flags=%s fl_type=%s",
		MAJOR(__entry->s_dev), MINOR(__entry->s_dev),
		__entry->i_ino, __entry->wcount, __entry->rcount,
		__entry->icount, __entry->owner,
		show_fl_flags(__entry->flags),
		show_fl_type(__entry->type))
);

TRACE_EVENT(leases_conflict,
	TP_PROTO(bool conflict, struct file_lease *lease, struct file_lease *breaker),

	TP_ARGS(conflict, lease, breaker),

	TP_STRUCT__entry(
		__field(void *, lease)
		__field(void *, breaker)
		__field(unsigned int, l_fl_flags)
		__field(unsigned int, b_fl_flags)
		__field(unsigned char, l_fl_type)
		__field(unsigned char, b_fl_type)
		__field(bool, conflict)
	),

	TP_fast_assign(
		__entry->lease = lease;
		__entry->l_fl_flags = lease->c.flc_flags;
		__entry->l_fl_type = lease->c.flc_type;
		__entry->breaker = breaker;
		__entry->b_fl_flags = breaker->c.flc_flags;
		__entry->b_fl_type = breaker->c.flc_type;
		__entry->conflict = conflict;
	),

	TP_printk("conflict %d: lease=%p fl_flags=%s fl_type=%s; breaker=%p fl_flags=%s fl_type=%s",
		__entry->conflict,
		__entry->lease,
		show_fl_flags(__entry->l_fl_flags),
		show_fl_type(__entry->l_fl_type),
		__entry->breaker,
		show_fl_flags(__entry->b_fl_flags),
		show_fl_type(__entry->b_fl_type))
);

#endif /* _TRACE_FILELOCK_H */

/* This part must be outside protection */
#include <trace/define_trace.h>
