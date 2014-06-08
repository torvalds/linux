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
		__entry->fl = fl;
		__entry->s_dev = inode->i_sb->s_dev;
		__entry->i_ino = inode->i_ino;
		__entry->fl_next = fl->fl_next;
		__entry->fl_owner = fl->fl_owner;
		__entry->fl_flags = fl->fl_flags;
		__entry->fl_type = fl->fl_type;
		__entry->fl_break_time = fl->fl_break_time;
		__entry->fl_downgrade_time = fl->fl_downgrade_time;
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

DEFINE_EVENT(filelock_lease, generic_add_lease, TP_PROTO(struct inode *inode, struct file_lock *fl),
		TP_ARGS(inode, fl));

DEFINE_EVENT(filelock_lease, generic_delete_lease, TP_PROTO(struct inode *inode, struct file_lock *fl),
		TP_ARGS(inode, fl));

DEFINE_EVENT(filelock_lease, time_out_leases, TP_PROTO(struct inode *inode, struct file_lock *fl),
		TP_ARGS(inode, fl));

#endif /* _TRACE_FILELOCK_H */

/* This part must be outside protection */
#include <trace/define_trace.h>
