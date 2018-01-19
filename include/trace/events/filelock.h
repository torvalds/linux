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
		{ FL_OFDLCK,		"FL_OFDLCK" })

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
		__field(struct file_lock *, fl_next)
		__field(fl_owner_t, fl_owner)
		__field(unsigned int, fl_pid)
		__field(unsigned int, fl_flags)
		__field(unsigned char, fl_type)
		__field(loff_t, fl_start)
		__field(loff_t, fl_end)
		__field(int, ret)
	),

	TP_fast_assign(
		__entry->fl = fl ? fl : NULL;
		__entry->s_dev = inode->i_sb->s_dev;
		__entry->i_ino = inode->i_ino;
		__entry->fl_next = fl ? fl->fl_next : NULL;
		__entry->fl_owner = fl ? fl->fl_owner : NULL;
		__entry->fl_pid = fl ? fl->fl_pid : 0;
		__entry->fl_flags = fl ? fl->fl_flags : 0;
		__entry->fl_type = fl ? fl->fl_type : 0;
		__entry->fl_start = fl ? fl->fl_start : 0;
		__entry->fl_end = fl ? fl->fl_end : 0;
		__entry->ret = ret;
	),

	TP_printk("fl=0x%p dev=0x%x:0x%x ino=0x%lx fl_next=0x%p fl_owner=0x%p fl_pid=%u fl_flags=%s fl_type=%s fl_start=%lld fl_end=%lld ret=%d",
		__entry->fl, MAJOR(__entry->s_dev), MINOR(__entry->s_dev),
		__entry->i_ino, __entry->fl_next, __entry->fl_owner,
		__entry->fl_pid, show_fl_flags(__entry->fl_flags),
		show_fl_type(__entry->fl_type),
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

DECLARE_EVENT_CLASS(filelock_lease,

	TP_PROTO(struct inode *inode, struct file_lock *fl),

	TP_ARGS(inode, fl),

	TP_STRUCT__entry(
		__field(struct file_lock *, fl)
		__field(unsigned long, i_ino)
		__field(dev_t, s_dev)
		__field(struct file_lock *, fl_next)
		__field(fl_owner_t, fl_owner)
		__field(unsigned int, fl_flags)
		__field(unsigned char, fl_type)
		__field(unsigned long, fl_break_time)
		__field(unsigned long, fl_downgrade_time)
	),

	TP_fast_assign(
		__entry->fl = fl ? fl : NULL;
		__entry->s_dev = inode->i_sb->s_dev;
		__entry->i_ino = inode->i_ino;
		__entry->fl_next = fl ? fl->fl_next : NULL;
		__entry->fl_owner = fl ? fl->fl_owner : NULL;
		__entry->fl_flags = fl ? fl->fl_flags : 0;
		__entry->fl_type = fl ? fl->fl_type : 0;
		__entry->fl_break_time = fl ? fl->fl_break_time : 0;
		__entry->fl_downgrade_time = fl ? fl->fl_downgrade_time : 0;
	),

	TP_printk("fl=0x%p dev=0x%x:0x%x ino=0x%lx fl_next=0x%p fl_owner=0x%p fl_flags=%s fl_type=%s fl_break_time=%lu fl_downgrade_time=%lu",
		__entry->fl, MAJOR(__entry->s_dev), MINOR(__entry->s_dev),
		__entry->i_ino, __entry->fl_next, __entry->fl_owner,
		show_fl_flags(__entry->fl_flags),
		show_fl_type(__entry->fl_type),
		__entry->fl_break_time, __entry->fl_downgrade_time)
);

DEFINE_EVENT(filelock_lease, break_lease_noblock, TP_PROTO(struct inode *inode, struct file_lock *fl),
		TP_ARGS(inode, fl));

DEFINE_EVENT(filelock_lease, break_lease_block, TP_PROTO(struct inode *inode, struct file_lock *fl),
		TP_ARGS(inode, fl));

DEFINE_EVENT(filelock_lease, break_lease_unblock, TP_PROTO(struct inode *inode, struct file_lock *fl),
		TP_ARGS(inode, fl));

DEFINE_EVENT(filelock_lease, generic_delete_lease, TP_PROTO(struct inode *inode, struct file_lock *fl),
		TP_ARGS(inode, fl));

DEFINE_EVENT(filelock_lease, time_out_leases, TP_PROTO(struct inode *inode, struct file_lock *fl),
		TP_ARGS(inode, fl));

TRACE_EVENT(generic_add_lease,
	TP_PROTO(struct inode *inode, struct file_lock *fl),

	TP_ARGS(inode, fl),

	TP_STRUCT__entry(
		__field(unsigned long, i_ino)
		__field(int, wcount)
		__field(int, dcount)
		__field(int, icount)
		__field(dev_t, s_dev)
		__field(fl_owner_t, fl_owner)
		__field(unsigned int, fl_flags)
		__field(unsigned char, fl_type)
	),

	TP_fast_assign(
		__entry->s_dev = inode->i_sb->s_dev;
		__entry->i_ino = inode->i_ino;
		__entry->wcount = atomic_read(&inode->i_writecount);
		__entry->dcount = d_count(fl->fl_file->f_path.dentry);
		__entry->icount = atomic_read(&inode->i_count);
		__entry->fl_owner = fl ? fl->fl_owner : NULL;
		__entry->fl_flags = fl ? fl->fl_flags : 0;
		__entry->fl_type = fl ? fl->fl_type : 0;
	),

	TP_printk("dev=0x%x:0x%x ino=0x%lx wcount=%d dcount=%d icount=%d fl_owner=0x%p fl_flags=%s fl_type=%s",
		MAJOR(__entry->s_dev), MINOR(__entry->s_dev),
		__entry->i_ino, __entry->wcount, __entry->dcount,
		__entry->icount, __entry->fl_owner,
		show_fl_flags(__entry->fl_flags),
		show_fl_type(__entry->fl_type))
);

#endif /* _TRACE_FILELOCK_H */

/* This part must be outside protection */
#include <trace/define_trace.h>
