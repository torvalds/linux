/* SPDX-License-Identifier: GPL-2.0 */
/*
 *   Copyright (C) 2018, Microsoft Corporation.
 *
 *   Author(s): Steve French <stfrench@microsoft.com>
 */
#undef TRACE_SYSTEM
#define TRACE_SYSTEM cifs

#if !defined(_CIFS_TRACE_H) || defined(TRACE_HEADER_MULTI_READ)
#define _CIFS_TRACE_H

#include <linux/tracepoint.h>
#include <linux/net.h>
#include <linux/inet.h>

/*
 * Please use this 3-part article as a reference for writing new tracepoints:
 * https://lwn.net/Articles/379903/
 */

/* For logging errors in read or write */
DECLARE_EVENT_CLASS(smb3_rw_err_class,
	TP_PROTO(unsigned int xid,
		__u64	fid,
		__u32	tid,
		__u64	sesid,
		__u64	offset,
		__u32	len,
		int	rc),
	TP_ARGS(xid, fid, tid, sesid, offset, len, rc),
	TP_STRUCT__entry(
		__field(unsigned int, xid)
		__field(__u64, fid)
		__field(__u32, tid)
		__field(__u64, sesid)
		__field(__u64, offset)
		__field(__u32, len)
		__field(int, rc)
	),
	TP_fast_assign(
		__entry->xid = xid;
		__entry->fid = fid;
		__entry->tid = tid;
		__entry->sesid = sesid;
		__entry->offset = offset;
		__entry->len = len;
		__entry->rc = rc;
	),
	TP_printk("\txid=%u sid=0x%llx tid=0x%x fid=0x%llx offset=0x%llx len=0x%x rc=%d",
		__entry->xid, __entry->sesid, __entry->tid, __entry->fid,
		__entry->offset, __entry->len, __entry->rc)
)

#define DEFINE_SMB3_RW_ERR_EVENT(name)          \
DEFINE_EVENT(smb3_rw_err_class, smb3_##name,    \
	TP_PROTO(unsigned int xid,		\
		__u64	fid,			\
		__u32	tid,			\
		__u64	sesid,			\
		__u64	offset,			\
		__u32	len,			\
		int	rc),			\
	TP_ARGS(xid, fid, tid, sesid, offset, len, rc))

DEFINE_SMB3_RW_ERR_EVENT(write_err);
DEFINE_SMB3_RW_ERR_EVENT(read_err);
DEFINE_SMB3_RW_ERR_EVENT(query_dir_err);
DEFINE_SMB3_RW_ERR_EVENT(zero_err);
DEFINE_SMB3_RW_ERR_EVENT(falloc_err);


/* For logging successful read or write */
DECLARE_EVENT_CLASS(smb3_rw_done_class,
	TP_PROTO(unsigned int xid,
		__u64	fid,
		__u32	tid,
		__u64	sesid,
		__u64	offset,
		__u32	len),
	TP_ARGS(xid, fid, tid, sesid, offset, len),
	TP_STRUCT__entry(
		__field(unsigned int, xid)
		__field(__u64, fid)
		__field(__u32, tid)
		__field(__u64, sesid)
		__field(__u64, offset)
		__field(__u32, len)
	),
	TP_fast_assign(
		__entry->xid = xid;
		__entry->fid = fid;
		__entry->tid = tid;
		__entry->sesid = sesid;
		__entry->offset = offset;
		__entry->len = len;
	),
	TP_printk("xid=%u sid=0x%llx tid=0x%x fid=0x%llx offset=0x%llx len=0x%x",
		__entry->xid, __entry->sesid, __entry->tid, __entry->fid,
		__entry->offset, __entry->len)
)

#define DEFINE_SMB3_RW_DONE_EVENT(name)         \
DEFINE_EVENT(smb3_rw_done_class, smb3_##name,   \
	TP_PROTO(unsigned int xid,		\
		__u64	fid,			\
		__u32	tid,			\
		__u64	sesid,			\
		__u64	offset,			\
		__u32	len),			\
	TP_ARGS(xid, fid, tid, sesid, offset, len))

DEFINE_SMB3_RW_DONE_EVENT(write_enter);
DEFINE_SMB3_RW_DONE_EVENT(read_enter);
DEFINE_SMB3_RW_DONE_EVENT(query_dir_enter);
DEFINE_SMB3_RW_DONE_EVENT(zero_enter);
DEFINE_SMB3_RW_DONE_EVENT(falloc_enter);
DEFINE_SMB3_RW_DONE_EVENT(write_done);
DEFINE_SMB3_RW_DONE_EVENT(read_done);
DEFINE_SMB3_RW_DONE_EVENT(query_dir_done);
DEFINE_SMB3_RW_DONE_EVENT(zero_done);
DEFINE_SMB3_RW_DONE_EVENT(falloc_done);

/* For logging successful set EOF (truncate) */
DECLARE_EVENT_CLASS(smb3_eof_class,
	TP_PROTO(unsigned int xid,
		__u64	fid,
		__u32	tid,
		__u64	sesid,
		__u64	offset),
	TP_ARGS(xid, fid, tid, sesid, offset),
	TP_STRUCT__entry(
		__field(unsigned int, xid)
		__field(__u64, fid)
		__field(__u32, tid)
		__field(__u64, sesid)
		__field(__u64, offset)
	),
	TP_fast_assign(
		__entry->xid = xid;
		__entry->fid = fid;
		__entry->tid = tid;
		__entry->sesid = sesid;
		__entry->offset = offset;
	),
	TP_printk("xid=%u sid=0x%llx tid=0x%x fid=0x%llx offset=0x%llx",
		__entry->xid, __entry->sesid, __entry->tid, __entry->fid,
		__entry->offset)
)

#define DEFINE_SMB3_EOF_EVENT(name)         \
DEFINE_EVENT(smb3_eof_class, smb3_##name,   \
	TP_PROTO(unsigned int xid,		\
		__u64	fid,			\
		__u32	tid,			\
		__u64	sesid,			\
		__u64	offset),		\
	TP_ARGS(xid, fid, tid, sesid, offset))

DEFINE_SMB3_EOF_EVENT(set_eof);

/*
 * For handle based calls other than read and write, and get/set info
 */
DECLARE_EVENT_CLASS(smb3_fd_class,
	TP_PROTO(unsigned int xid,
		__u64	fid,
		__u32	tid,
		__u64	sesid),
	TP_ARGS(xid, fid, tid, sesid),
	TP_STRUCT__entry(
		__field(unsigned int, xid)
		__field(__u64, fid)
		__field(__u32, tid)
		__field(__u64, sesid)
	),
	TP_fast_assign(
		__entry->xid = xid;
		__entry->fid = fid;
		__entry->tid = tid;
		__entry->sesid = sesid;
	),
	TP_printk("\txid=%u sid=0x%llx tid=0x%x fid=0x%llx",
		__entry->xid, __entry->sesid, __entry->tid, __entry->fid)
)

#define DEFINE_SMB3_FD_EVENT(name)          \
DEFINE_EVENT(smb3_fd_class, smb3_##name,    \
	TP_PROTO(unsigned int xid,		\
		__u64	fid,			\
		__u32	tid,			\
		__u64	sesid),			\
	TP_ARGS(xid, fid, tid, sesid))

DEFINE_SMB3_FD_EVENT(flush_enter);
DEFINE_SMB3_FD_EVENT(flush_done);
DEFINE_SMB3_FD_EVENT(close_enter);
DEFINE_SMB3_FD_EVENT(close_done);
DEFINE_SMB3_FD_EVENT(oplock_not_found);

DECLARE_EVENT_CLASS(smb3_fd_err_class,
	TP_PROTO(unsigned int xid,
		__u64	fid,
		__u32	tid,
		__u64	sesid,
		int	rc),
	TP_ARGS(xid, fid, tid, sesid, rc),
	TP_STRUCT__entry(
		__field(unsigned int, xid)
		__field(__u64, fid)
		__field(__u32, tid)
		__field(__u64, sesid)
		__field(int, rc)
	),
	TP_fast_assign(
		__entry->xid = xid;
		__entry->fid = fid;
		__entry->tid = tid;
		__entry->sesid = sesid;
		__entry->rc = rc;
	),
	TP_printk("\txid=%u sid=0x%llx tid=0x%x fid=0x%llx rc=%d",
		__entry->xid, __entry->sesid, __entry->tid, __entry->fid,
		__entry->rc)
)

#define DEFINE_SMB3_FD_ERR_EVENT(name)          \
DEFINE_EVENT(smb3_fd_err_class, smb3_##name,    \
	TP_PROTO(unsigned int xid,		\
		__u64	fid,			\
		__u32	tid,			\
		__u64	sesid,			\
		int	rc),			\
	TP_ARGS(xid, fid, tid, sesid, rc))

DEFINE_SMB3_FD_ERR_EVENT(flush_err);
DEFINE_SMB3_FD_ERR_EVENT(lock_err);
DEFINE_SMB3_FD_ERR_EVENT(close_err);

/*
 * For handle based query/set info calls
 */
DECLARE_EVENT_CLASS(smb3_inf_enter_class,
	TP_PROTO(unsigned int xid,
		__u64	fid,
		__u32	tid,
		__u64	sesid,
		__u8	infclass,
		__u32	type),
	TP_ARGS(xid, fid, tid, sesid, infclass, type),
	TP_STRUCT__entry(
		__field(unsigned int, xid)
		__field(__u64, fid)
		__field(__u32, tid)
		__field(__u64, sesid)
		__field(__u8, infclass)
		__field(__u32, type)
	),
	TP_fast_assign(
		__entry->xid = xid;
		__entry->fid = fid;
		__entry->tid = tid;
		__entry->sesid = sesid;
		__entry->infclass = infclass;
		__entry->type = type;
	),
	TP_printk("xid=%u sid=0x%llx tid=0x%x fid=0x%llx class=%u type=0x%x",
		__entry->xid, __entry->sesid, __entry->tid, __entry->fid,
		__entry->infclass, __entry->type)
)

#define DEFINE_SMB3_INF_ENTER_EVENT(name)          \
DEFINE_EVENT(smb3_inf_enter_class, smb3_##name,    \
	TP_PROTO(unsigned int xid,		\
		__u64	fid,			\
		__u32	tid,			\
		__u64	sesid,			\
		__u8	infclass,		\
		__u32	type),			\
	TP_ARGS(xid, fid, tid, sesid, infclass, type))

DEFINE_SMB3_INF_ENTER_EVENT(query_info_enter);
DEFINE_SMB3_INF_ENTER_EVENT(query_info_done);
DEFINE_SMB3_INF_ENTER_EVENT(notify_enter);
DEFINE_SMB3_INF_ENTER_EVENT(notify_done);

DECLARE_EVENT_CLASS(smb3_inf_err_class,
	TP_PROTO(unsigned int xid,
		__u64	fid,
		__u32	tid,
		__u64	sesid,
		__u8	infclass,
		__u32	type,
		int	rc),
	TP_ARGS(xid, fid, tid, sesid, infclass, type, rc),
	TP_STRUCT__entry(
		__field(unsigned int, xid)
		__field(__u64, fid)
		__field(__u32, tid)
		__field(__u64, sesid)
		__field(__u8, infclass)
		__field(__u32, type)
		__field(int, rc)
	),
	TP_fast_assign(
		__entry->xid = xid;
		__entry->fid = fid;
		__entry->tid = tid;
		__entry->sesid = sesid;
		__entry->infclass = infclass;
		__entry->type = type;
		__entry->rc = rc;
	),
	TP_printk("xid=%u sid=0x%llx tid=0x%x fid=0x%llx class=%u type=0x%x rc=%d",
		__entry->xid, __entry->sesid, __entry->tid, __entry->fid,
		__entry->infclass, __entry->type, __entry->rc)
)

#define DEFINE_SMB3_INF_ERR_EVENT(name)          \
DEFINE_EVENT(smb3_inf_err_class, smb3_##name,    \
	TP_PROTO(unsigned int xid,		\
		__u64	fid,			\
		__u32	tid,			\
		__u64	sesid,			\
		__u8	infclass,		\
		__u32	type,			\
		int	rc),			\
	TP_ARGS(xid, fid, tid, sesid, infclass, type, rc))

DEFINE_SMB3_INF_ERR_EVENT(query_info_err);
DEFINE_SMB3_INF_ERR_EVENT(set_info_err);
DEFINE_SMB3_INF_ERR_EVENT(notify_err);
DEFINE_SMB3_INF_ERR_EVENT(fsctl_err);

DECLARE_EVENT_CLASS(smb3_inf_compound_enter_class,
	TP_PROTO(unsigned int xid,
		__u32	tid,
		__u64	sesid,
		const char *full_path),
	TP_ARGS(xid, tid, sesid, full_path),
	TP_STRUCT__entry(
		__field(unsigned int, xid)
		__field(__u32, tid)
		__field(__u64, sesid)
		__string(path, full_path)
	),
	TP_fast_assign(
		__entry->xid = xid;
		__entry->tid = tid;
		__entry->sesid = sesid;
		__assign_str(path, full_path);
	),
	TP_printk("xid=%u sid=0x%llx tid=0x%x path=%s",
		__entry->xid, __entry->sesid, __entry->tid,
		__get_str(path))
)

#define DEFINE_SMB3_INF_COMPOUND_ENTER_EVENT(name)     \
DEFINE_EVENT(smb3_inf_compound_enter_class, smb3_##name,    \
	TP_PROTO(unsigned int xid,		\
		__u32	tid,			\
		__u64	sesid,			\
		const char *full_path),		\
	TP_ARGS(xid, tid, sesid, full_path))

DEFINE_SMB3_INF_COMPOUND_ENTER_EVENT(query_info_compound_enter);
DEFINE_SMB3_INF_COMPOUND_ENTER_EVENT(posix_query_info_compound_enter);
DEFINE_SMB3_INF_COMPOUND_ENTER_EVENT(hardlink_enter);
DEFINE_SMB3_INF_COMPOUND_ENTER_EVENT(rename_enter);
DEFINE_SMB3_INF_COMPOUND_ENTER_EVENT(rmdir_enter);
DEFINE_SMB3_INF_COMPOUND_ENTER_EVENT(set_eof_enter);
DEFINE_SMB3_INF_COMPOUND_ENTER_EVENT(set_info_compound_enter);
DEFINE_SMB3_INF_COMPOUND_ENTER_EVENT(set_reparse_compound_enter);
DEFINE_SMB3_INF_COMPOUND_ENTER_EVENT(get_reparse_compound_enter);
DEFINE_SMB3_INF_COMPOUND_ENTER_EVENT(delete_enter);
DEFINE_SMB3_INF_COMPOUND_ENTER_EVENT(mkdir_enter);
DEFINE_SMB3_INF_COMPOUND_ENTER_EVENT(tdis_enter);
DEFINE_SMB3_INF_COMPOUND_ENTER_EVENT(mknod_enter);

DECLARE_EVENT_CLASS(smb3_inf_compound_done_class,
	TP_PROTO(unsigned int xid,
		__u32	tid,
		__u64	sesid),
	TP_ARGS(xid, tid, sesid),
	TP_STRUCT__entry(
		__field(unsigned int, xid)
		__field(__u32, tid)
		__field(__u64, sesid)
	),
	TP_fast_assign(
		__entry->xid = xid;
		__entry->tid = tid;
		__entry->sesid = sesid;
	),
	TP_printk("xid=%u sid=0x%llx tid=0x%x",
		__entry->xid, __entry->sesid, __entry->tid)
)

#define DEFINE_SMB3_INF_COMPOUND_DONE_EVENT(name)     \
DEFINE_EVENT(smb3_inf_compound_done_class, smb3_##name,    \
	TP_PROTO(unsigned int xid,		\
		__u32	tid,			\
		__u64	sesid),			\
	TP_ARGS(xid, tid, sesid))

DEFINE_SMB3_INF_COMPOUND_DONE_EVENT(query_info_compound_done);
DEFINE_SMB3_INF_COMPOUND_DONE_EVENT(posix_query_info_compound_done);
DEFINE_SMB3_INF_COMPOUND_DONE_EVENT(hardlink_done);
DEFINE_SMB3_INF_COMPOUND_DONE_EVENT(rename_done);
DEFINE_SMB3_INF_COMPOUND_DONE_EVENT(rmdir_done);
DEFINE_SMB3_INF_COMPOUND_DONE_EVENT(set_eof_done);
DEFINE_SMB3_INF_COMPOUND_DONE_EVENT(set_info_compound_done);
DEFINE_SMB3_INF_COMPOUND_DONE_EVENT(set_reparse_compound_done);
DEFINE_SMB3_INF_COMPOUND_DONE_EVENT(get_reparse_compound_done);
DEFINE_SMB3_INF_COMPOUND_DONE_EVENT(query_wsl_ea_compound_done);
DEFINE_SMB3_INF_COMPOUND_DONE_EVENT(delete_done);
DEFINE_SMB3_INF_COMPOUND_DONE_EVENT(mkdir_done);
DEFINE_SMB3_INF_COMPOUND_DONE_EVENT(tdis_done);
DEFINE_SMB3_INF_COMPOUND_DONE_EVENT(mknod_done);

DECLARE_EVENT_CLASS(smb3_inf_compound_err_class,
	TP_PROTO(unsigned int xid,
		__u32	tid,
		__u64	sesid,
		int	rc),
	TP_ARGS(xid, tid, sesid, rc),
	TP_STRUCT__entry(
		__field(unsigned int, xid)
		__field(__u32, tid)
		__field(__u64, sesid)
		__field(int, rc)
	),
	TP_fast_assign(
		__entry->xid = xid;
		__entry->tid = tid;
		__entry->sesid = sesid;
		__entry->rc = rc;
	),
	TP_printk("xid=%u sid=0x%llx tid=0x%x rc=%d",
		__entry->xid, __entry->sesid, __entry->tid,
		__entry->rc)
)

#define DEFINE_SMB3_INF_COMPOUND_ERR_EVENT(name)     \
DEFINE_EVENT(smb3_inf_compound_err_class, smb3_##name,    \
	TP_PROTO(unsigned int xid,		\
		__u32	tid,			\
		__u64	sesid,			\
		int rc),			\
	TP_ARGS(xid, tid, sesid, rc))

DEFINE_SMB3_INF_COMPOUND_ERR_EVENT(query_info_compound_err);
DEFINE_SMB3_INF_COMPOUND_ERR_EVENT(posix_query_info_compound_err);
DEFINE_SMB3_INF_COMPOUND_ERR_EVENT(hardlink_err);
DEFINE_SMB3_INF_COMPOUND_ERR_EVENT(rename_err);
DEFINE_SMB3_INF_COMPOUND_ERR_EVENT(rmdir_err);
DEFINE_SMB3_INF_COMPOUND_ERR_EVENT(set_eof_err);
DEFINE_SMB3_INF_COMPOUND_ERR_EVENT(set_info_compound_err);
DEFINE_SMB3_INF_COMPOUND_ERR_EVENT(set_reparse_compound_err);
DEFINE_SMB3_INF_COMPOUND_ERR_EVENT(get_reparse_compound_err);
DEFINE_SMB3_INF_COMPOUND_ERR_EVENT(query_wsl_ea_compound_err);
DEFINE_SMB3_INF_COMPOUND_ERR_EVENT(mkdir_err);
DEFINE_SMB3_INF_COMPOUND_ERR_EVENT(delete_err);
DEFINE_SMB3_INF_COMPOUND_ERR_EVENT(tdis_err);
DEFINE_SMB3_INF_COMPOUND_ERR_EVENT(mknod_err);

/*
 * For logging SMB3 Status code and Command for responses which return errors
 */
DECLARE_EVENT_CLASS(smb3_cmd_err_class,
	TP_PROTO(__u32	tid,
		__u64	sesid,
		__u16	cmd,
		__u64	mid,
		__u32	status,
		int	rc),
	TP_ARGS(tid, sesid, cmd, mid, status, rc),
	TP_STRUCT__entry(
		__field(__u32, tid)
		__field(__u64, sesid)
		__field(__u16, cmd)
		__field(__u64, mid)
		__field(__u32, status)
		__field(int, rc)
	),
	TP_fast_assign(
		__entry->tid = tid;
		__entry->sesid = sesid;
		__entry->cmd = cmd;
		__entry->mid = mid;
		__entry->status = status;
		__entry->rc = rc;
	),
	TP_printk("\tsid=0x%llx tid=0x%x cmd=%u mid=%llu status=0x%x rc=%d",
		__entry->sesid, __entry->tid, __entry->cmd, __entry->mid,
		__entry->status, __entry->rc)
)

#define DEFINE_SMB3_CMD_ERR_EVENT(name)          \
DEFINE_EVENT(smb3_cmd_err_class, smb3_##name,    \
	TP_PROTO(__u32	tid,			\
		__u64	sesid,			\
		__u16	cmd,			\
		__u64	mid,			\
		__u32	status,			\
		int	rc),			\
	TP_ARGS(tid, sesid, cmd, mid, status, rc))

DEFINE_SMB3_CMD_ERR_EVENT(cmd_err);

DECLARE_EVENT_CLASS(smb3_cmd_done_class,
	TP_PROTO(__u32	tid,
		__u64	sesid,
		__u16	cmd,
		__u64	mid),
	TP_ARGS(tid, sesid, cmd, mid),
	TP_STRUCT__entry(
		__field(__u32, tid)
		__field(__u64, sesid)
		__field(__u16, cmd)
		__field(__u64, mid)
	),
	TP_fast_assign(
		__entry->tid = tid;
		__entry->sesid = sesid;
		__entry->cmd = cmd;
		__entry->mid = mid;
	),
	TP_printk("\tsid=0x%llx tid=0x%x cmd=%u mid=%llu",
		__entry->sesid, __entry->tid,
		__entry->cmd, __entry->mid)
)

#define DEFINE_SMB3_CMD_DONE_EVENT(name)          \
DEFINE_EVENT(smb3_cmd_done_class, smb3_##name,    \
	TP_PROTO(__u32	tid,			\
		__u64	sesid,			\
		__u16	cmd,			\
		__u64	mid),			\
	TP_ARGS(tid, sesid, cmd, mid))

DEFINE_SMB3_CMD_DONE_EVENT(cmd_enter);
DEFINE_SMB3_CMD_DONE_EVENT(cmd_done);
DEFINE_SMB3_CMD_DONE_EVENT(ses_expired);

DECLARE_EVENT_CLASS(smb3_mid_class,
	TP_PROTO(__u16	cmd,
		__u64	mid,
		__u32	pid,
		unsigned long when_sent,
		unsigned long when_received),
	TP_ARGS(cmd, mid, pid, when_sent, when_received),
	TP_STRUCT__entry(
		__field(__u16, cmd)
		__field(__u64, mid)
		__field(__u32, pid)
		__field(unsigned long, when_sent)
		__field(unsigned long, when_received)
	),
	TP_fast_assign(
		__entry->cmd = cmd;
		__entry->mid = mid;
		__entry->pid = pid;
		__entry->when_sent = when_sent;
		__entry->when_received = when_received;
	),
	TP_printk("\tcmd=%u mid=%llu pid=%u, when_sent=%lu when_rcv=%lu",
		__entry->cmd, __entry->mid, __entry->pid, __entry->when_sent,
		__entry->when_received)
)

#define DEFINE_SMB3_MID_EVENT(name)          \
DEFINE_EVENT(smb3_mid_class, smb3_##name,    \
	TP_PROTO(__u16	cmd,			\
		__u64	mid,			\
		__u32	pid,			\
		unsigned long when_sent,	\
		unsigned long when_received),	\
	TP_ARGS(cmd, mid, pid, when_sent, when_received))

DEFINE_SMB3_MID_EVENT(slow_rsp);

DECLARE_EVENT_CLASS(smb3_exit_err_class,
	TP_PROTO(unsigned int xid,
		const char *func_name,
		int	rc),
	TP_ARGS(xid, func_name, rc),
	TP_STRUCT__entry(
		__field(unsigned int, xid)
		__string(func_name, func_name)
		__field(int, rc)
	),
	TP_fast_assign(
		__entry->xid = xid;
		__assign_str(func_name, func_name);
		__entry->rc = rc;
	),
	TP_printk("\t%s: xid=%u rc=%d",
		__get_str(func_name), __entry->xid, __entry->rc)
)

#define DEFINE_SMB3_EXIT_ERR_EVENT(name)          \
DEFINE_EVENT(smb3_exit_err_class, smb3_##name,    \
	TP_PROTO(unsigned int xid,		\
		const char *func_name,		\
		int	rc),			\
	TP_ARGS(xid, func_name, rc))

DEFINE_SMB3_EXIT_ERR_EVENT(exit_err);


DECLARE_EVENT_CLASS(smb3_sync_err_class,
	TP_PROTO(unsigned long ino,
		int	rc),
	TP_ARGS(ino, rc),
	TP_STRUCT__entry(
		__field(unsigned long, ino)
		__field(int, rc)
	),
	TP_fast_assign(
		__entry->ino = ino;
		__entry->rc = rc;
	),
	TP_printk("\tino=%lu rc=%d",
		__entry->ino, __entry->rc)
)

#define DEFINE_SMB3_SYNC_ERR_EVENT(name)          \
DEFINE_EVENT(smb3_sync_err_class, cifs_##name,    \
	TP_PROTO(unsigned long ino,		\
		int	rc),			\
	TP_ARGS(ino, rc))

DEFINE_SMB3_SYNC_ERR_EVENT(fsync_err);
DEFINE_SMB3_SYNC_ERR_EVENT(flush_err);


DECLARE_EVENT_CLASS(smb3_enter_exit_class,
	TP_PROTO(unsigned int xid,
		const char *func_name),
	TP_ARGS(xid, func_name),
	TP_STRUCT__entry(
		__field(unsigned int, xid)
		__string(func_name, func_name)
	),
	TP_fast_assign(
		__entry->xid = xid;
		__assign_str(func_name, func_name);
	),
	TP_printk("\t%s: xid=%u",
		__get_str(func_name), __entry->xid)
)

#define DEFINE_SMB3_ENTER_EXIT_EVENT(name)        \
DEFINE_EVENT(smb3_enter_exit_class, smb3_##name,  \
	TP_PROTO(unsigned int xid,		\
		const char *func_name),		\
	TP_ARGS(xid, func_name))

DEFINE_SMB3_ENTER_EXIT_EVENT(enter);
DEFINE_SMB3_ENTER_EXIT_EVENT(exit_done);

/*
 * For SMB2/SMB3 tree connect
 */

DECLARE_EVENT_CLASS(smb3_tcon_class,
	TP_PROTO(unsigned int xid,
		__u32	tid,
		__u64	sesid,
		const char *unc_name,
		int	rc),
	TP_ARGS(xid, tid, sesid, unc_name, rc),
	TP_STRUCT__entry(
		__field(unsigned int, xid)
		__field(__u32, tid)
		__field(__u64, sesid)
		__string(name, unc_name)
		__field(int, rc)
	),
	TP_fast_assign(
		__entry->xid = xid;
		__entry->tid = tid;
		__entry->sesid = sesid;
		__assign_str(name, unc_name);
		__entry->rc = rc;
	),
	TP_printk("xid=%u sid=0x%llx tid=0x%x unc_name=%s rc=%d",
		__entry->xid, __entry->sesid, __entry->tid,
		__get_str(name), __entry->rc)
)

#define DEFINE_SMB3_TCON_EVENT(name)          \
DEFINE_EVENT(smb3_tcon_class, smb3_##name,    \
	TP_PROTO(unsigned int xid,		\
		__u32	tid,			\
		__u64	sesid,			\
		const char *unc_name,		\
		int	rc),			\
	TP_ARGS(xid, tid, sesid, unc_name, rc))

DEFINE_SMB3_TCON_EVENT(tcon);
DEFINE_SMB3_TCON_EVENT(qfs_done);

/*
 * For smb2/smb3 open (including create and mkdir) calls
 */

DECLARE_EVENT_CLASS(smb3_open_enter_class,
	TP_PROTO(unsigned int xid,
		__u32	tid,
		__u64	sesid,
		const char *full_path,
		int	create_options,
		int	desired_access),
	TP_ARGS(xid, tid, sesid, full_path, create_options, desired_access),
	TP_STRUCT__entry(
		__field(unsigned int, xid)
		__field(__u32, tid)
		__field(__u64, sesid)
		__string(path, full_path)
		__field(int, create_options)
		__field(int, desired_access)
	),
	TP_fast_assign(
		__entry->xid = xid;
		__entry->tid = tid;
		__entry->sesid = sesid;
		__assign_str(path, full_path);
		__entry->create_options = create_options;
		__entry->desired_access = desired_access;
	),
	TP_printk("xid=%u sid=0x%llx tid=0x%x path=%s cr_opts=0x%x des_access=0x%x",
		__entry->xid, __entry->sesid, __entry->tid, __get_str(path),
		__entry->create_options, __entry->desired_access)
)

#define DEFINE_SMB3_OPEN_ENTER_EVENT(name)        \
DEFINE_EVENT(smb3_open_enter_class, smb3_##name,  \
	TP_PROTO(unsigned int xid,		\
		__u32	tid,			\
		__u64	sesid,			\
		const char *full_path,		\
		int	create_options,		\
		int	desired_access),	\
	TP_ARGS(xid, tid, sesid, full_path, create_options, desired_access))

DEFINE_SMB3_OPEN_ENTER_EVENT(open_enter);
DEFINE_SMB3_OPEN_ENTER_EVENT(posix_mkdir_enter);

DECLARE_EVENT_CLASS(smb3_open_err_class,
	TP_PROTO(unsigned int xid,
		__u32	tid,
		__u64	sesid,
		int	create_options,
		int	desired_access,
		int	rc),
	TP_ARGS(xid, tid, sesid, create_options, desired_access, rc),
	TP_STRUCT__entry(
		__field(unsigned int, xid)
		__field(__u32, tid)
		__field(__u64, sesid)
		__field(int,   create_options)
		__field(int, desired_access)
		__field(int, rc)
	),
	TP_fast_assign(
		__entry->xid = xid;
		__entry->tid = tid;
		__entry->sesid = sesid;
		__entry->create_options = create_options;
		__entry->desired_access = desired_access;
		__entry->rc = rc;
	),
	TP_printk("xid=%u sid=0x%llx tid=0x%x cr_opts=0x%x des_access=0x%x rc=%d",
		__entry->xid, __entry->sesid, __entry->tid,
		__entry->create_options, __entry->desired_access, __entry->rc)
)

#define DEFINE_SMB3_OPEN_ERR_EVENT(name)          \
DEFINE_EVENT(smb3_open_err_class, smb3_##name,    \
	TP_PROTO(unsigned int xid,		\
		__u32	tid,			\
		__u64	sesid,			\
		int	create_options,		\
		int	desired_access,		\
		int	rc),			\
	TP_ARGS(xid, tid, sesid, create_options, desired_access, rc))

DEFINE_SMB3_OPEN_ERR_EVENT(open_err);
DEFINE_SMB3_OPEN_ERR_EVENT(posix_mkdir_err);

DECLARE_EVENT_CLASS(smb3_open_done_class,
	TP_PROTO(unsigned int xid,
		__u64	fid,
		__u32	tid,
		__u64	sesid,
		int	create_options,
		int	desired_access),
	TP_ARGS(xid, fid, tid, sesid, create_options, desired_access),
	TP_STRUCT__entry(
		__field(unsigned int, xid)
		__field(__u64, fid)
		__field(__u32, tid)
		__field(__u64, sesid)
		__field(int, create_options)
		__field(int, desired_access)
	),
	TP_fast_assign(
		__entry->xid = xid;
		__entry->fid = fid;
		__entry->tid = tid;
		__entry->sesid = sesid;
		__entry->create_options = create_options;
		__entry->desired_access = desired_access;
	),
	TP_printk("xid=%u sid=0x%llx tid=0x%x fid=0x%llx cr_opts=0x%x des_access=0x%x",
		__entry->xid, __entry->sesid, __entry->tid, __entry->fid,
		__entry->create_options, __entry->desired_access)
)

#define DEFINE_SMB3_OPEN_DONE_EVENT(name)        \
DEFINE_EVENT(smb3_open_done_class, smb3_##name,  \
	TP_PROTO(unsigned int xid,		\
		__u64	fid,			\
		__u32	tid,			\
		__u64	sesid,			\
		int	create_options,		\
		int	desired_access),	\
	TP_ARGS(xid, fid, tid, sesid, create_options, desired_access))

DEFINE_SMB3_OPEN_DONE_EVENT(open_done);
DEFINE_SMB3_OPEN_DONE_EVENT(posix_mkdir_done);


DECLARE_EVENT_CLASS(smb3_lease_done_class,
	TP_PROTO(__u32	lease_state,
		__u32	tid,
		__u64	sesid,
		__u64	lease_key_low,
		__u64	lease_key_high),
	TP_ARGS(lease_state, tid, sesid, lease_key_low, lease_key_high),
	TP_STRUCT__entry(
		__field(__u32, lease_state)
		__field(__u32, tid)
		__field(__u64, sesid)
		__field(__u64, lease_key_low)
		__field(__u64, lease_key_high)
	),
	TP_fast_assign(
		__entry->lease_state = lease_state;
		__entry->tid = tid;
		__entry->sesid = sesid;
		__entry->lease_key_low = lease_key_low;
		__entry->lease_key_high = lease_key_high;
	),
	TP_printk("sid=0x%llx tid=0x%x lease_key=0x%llx%llx lease_state=0x%x",
		__entry->sesid, __entry->tid, __entry->lease_key_high,
		__entry->lease_key_low, __entry->lease_state)
)

#define DEFINE_SMB3_LEASE_DONE_EVENT(name)        \
DEFINE_EVENT(smb3_lease_done_class, smb3_##name,  \
	TP_PROTO(__u32	lease_state,		\
		__u32	tid,			\
		__u64	sesid,			\
		__u64	lease_key_low,		\
		__u64	lease_key_high),	\
	TP_ARGS(lease_state, tid, sesid, lease_key_low, lease_key_high))

DEFINE_SMB3_LEASE_DONE_EVENT(lease_done);
DEFINE_SMB3_LEASE_DONE_EVENT(lease_not_found);

DECLARE_EVENT_CLASS(smb3_lease_err_class,
	TP_PROTO(__u32	lease_state,
		__u32	tid,
		__u64	sesid,
		__u64	lease_key_low,
		__u64	lease_key_high,
		int	rc),
	TP_ARGS(lease_state, tid, sesid, lease_key_low, lease_key_high, rc),
	TP_STRUCT__entry(
		__field(__u32, lease_state)
		__field(__u32, tid)
		__field(__u64, sesid)
		__field(__u64, lease_key_low)
		__field(__u64, lease_key_high)
		__field(int, rc)
	),
	TP_fast_assign(
		__entry->lease_state = lease_state;
		__entry->tid = tid;
		__entry->sesid = sesid;
		__entry->lease_key_low = lease_key_low;
		__entry->lease_key_high = lease_key_high;
		__entry->rc = rc;
	),
	TP_printk("sid=0x%llx tid=0x%x lease_key=0x%llx%llx lease_state=0x%x rc=%d",
		__entry->sesid, __entry->tid, __entry->lease_key_high,
		__entry->lease_key_low, __entry->lease_state, __entry->rc)
)

#define DEFINE_SMB3_LEASE_ERR_EVENT(name)        \
DEFINE_EVENT(smb3_lease_err_class, smb3_##name,  \
	TP_PROTO(__u32	lease_state,		\
		__u32	tid,			\
		__u64	sesid,			\
		__u64	lease_key_low,		\
		__u64	lease_key_high,		\
		int	rc),			\
	TP_ARGS(lease_state, tid, sesid, lease_key_low, lease_key_high, rc))

DEFINE_SMB3_LEASE_ERR_EVENT(lease_err);

DECLARE_EVENT_CLASS(smb3_connect_class,
	TP_PROTO(char *hostname,
		__u64 conn_id,
		const struct __kernel_sockaddr_storage *dst_addr),
	TP_ARGS(hostname, conn_id, dst_addr),
	TP_STRUCT__entry(
		__string(hostname, hostname)
		__field(__u64, conn_id)
		__array(__u8, dst_addr, sizeof(struct sockaddr_storage))
	),
	TP_fast_assign(
		struct sockaddr_storage *pss = NULL;

		__entry->conn_id = conn_id;
		pss = (struct sockaddr_storage *)__entry->dst_addr;
		*pss = *dst_addr;
		__assign_str(hostname, hostname);
	),
	TP_printk("conn_id=0x%llx server=%s addr=%pISpsfc",
		__entry->conn_id,
		__get_str(hostname),
		__entry->dst_addr)
)

#define DEFINE_SMB3_CONNECT_EVENT(name)        \
DEFINE_EVENT(smb3_connect_class, smb3_##name,  \
	TP_PROTO(char *hostname,		\
		__u64 conn_id,			\
		const struct __kernel_sockaddr_storage *addr),	\
	TP_ARGS(hostname, conn_id, addr))

DEFINE_SMB3_CONNECT_EVENT(connect_done);
DEFINE_SMB3_CONNECT_EVENT(smbd_connect_done);
DEFINE_SMB3_CONNECT_EVENT(smbd_connect_err);

DECLARE_EVENT_CLASS(smb3_connect_err_class,
	TP_PROTO(char *hostname, __u64 conn_id,
		const struct __kernel_sockaddr_storage *dst_addr, int rc),
	TP_ARGS(hostname, conn_id, dst_addr, rc),
	TP_STRUCT__entry(
		__string(hostname, hostname)
		__field(__u64, conn_id)
		__array(__u8, dst_addr, sizeof(struct sockaddr_storage))
		__field(int, rc)
	),
	TP_fast_assign(
		struct sockaddr_storage *pss = NULL;

		__entry->conn_id = conn_id;
		__entry->rc = rc;
		pss = (struct sockaddr_storage *)__entry->dst_addr;
		*pss = *dst_addr;
		__assign_str(hostname, hostname);
	),
	TP_printk("rc=%d conn_id=0x%llx server=%s addr=%pISpsfc",
		__entry->rc,
		__entry->conn_id,
		__get_str(hostname),
		__entry->dst_addr)
)

#define DEFINE_SMB3_CONNECT_ERR_EVENT(name)        \
DEFINE_EVENT(smb3_connect_err_class, smb3_##name,  \
	TP_PROTO(char *hostname,		\
		__u64 conn_id,			\
		const struct __kernel_sockaddr_storage *addr,	\
		int rc),			\
	TP_ARGS(hostname, conn_id, addr, rc))

DEFINE_SMB3_CONNECT_ERR_EVENT(connect_err);

DECLARE_EVENT_CLASS(smb3_reconnect_class,
	TP_PROTO(__u64	currmid,
		__u64 conn_id,
		char *hostname),
	TP_ARGS(currmid, conn_id, hostname),
	TP_STRUCT__entry(
		__field(__u64, currmid)
		__field(__u64, conn_id)
		__string(hostname, hostname)
	),
	TP_fast_assign(
		__entry->currmid = currmid;
		__entry->conn_id = conn_id;
		__assign_str(hostname, hostname);
	),
	TP_printk("conn_id=0x%llx server=%s current_mid=%llu",
		__entry->conn_id,
		__get_str(hostname),
		__entry->currmid)
)

#define DEFINE_SMB3_RECONNECT_EVENT(name)        \
DEFINE_EVENT(smb3_reconnect_class, smb3_##name,  \
	TP_PROTO(__u64	currmid,		\
		__u64 conn_id,			\
		char *hostname),				\
	TP_ARGS(currmid, conn_id, hostname))

DEFINE_SMB3_RECONNECT_EVENT(reconnect);
DEFINE_SMB3_RECONNECT_EVENT(partial_send_reconnect);

DECLARE_EVENT_CLASS(smb3_ses_class,
	TP_PROTO(__u64	sesid),
	TP_ARGS(sesid),
	TP_STRUCT__entry(
		__field(__u64, sesid)
	),
	TP_fast_assign(
		__entry->sesid = sesid;
	),
	TP_printk("sid=0x%llx",
		__entry->sesid)
)

#define DEFINE_SMB3_SES_EVENT(name)        \
DEFINE_EVENT(smb3_ses_class, smb3_##name,  \
	TP_PROTO(__u64	sesid),				\
	TP_ARGS(sesid))

DEFINE_SMB3_SES_EVENT(ses_not_found);

DECLARE_EVENT_CLASS(smb3_ioctl_class,
	TP_PROTO(unsigned int xid,
		__u64	fid,
		unsigned int command),
	TP_ARGS(xid, fid, command),
	TP_STRUCT__entry(
		__field(unsigned int, xid)
		__field(__u64, fid)
		__field(unsigned int, command)
	),
	TP_fast_assign(
		__entry->xid = xid;
		__entry->fid = fid;
		__entry->command = command;
	),
	TP_printk("xid=%u fid=0x%llx ioctl cmd=0x%x",
		__entry->xid, __entry->fid, __entry->command)
)

#define DEFINE_SMB3_IOCTL_EVENT(name)        \
DEFINE_EVENT(smb3_ioctl_class, smb3_##name,  \
	TP_PROTO(unsigned int xid,	     \
		__u64 fid,		     \
		unsigned int command),	     \
	TP_ARGS(xid, fid, command))

DEFINE_SMB3_IOCTL_EVENT(ioctl);





DECLARE_EVENT_CLASS(smb3_credit_class,
	TP_PROTO(__u64	currmid,
		__u64 conn_id,
		char *hostname,
		int credits,
		int credits_to_add,
		int in_flight),
	TP_ARGS(currmid, conn_id, hostname, credits, credits_to_add, in_flight),
	TP_STRUCT__entry(
		__field(__u64, currmid)
		__field(__u64, conn_id)
		__string(hostname, hostname)
		__field(int, credits)
		__field(int, credits_to_add)
		__field(int, in_flight)
	),
	TP_fast_assign(
		__entry->currmid = currmid;
		__entry->conn_id = conn_id;
		__assign_str(hostname, hostname);
		__entry->credits = credits;
		__entry->credits_to_add = credits_to_add;
		__entry->in_flight = in_flight;
	),
	TP_printk("conn_id=0x%llx server=%s current_mid=%llu "
			"credits=%d credit_change=%d in_flight=%d",
		__entry->conn_id,
		__get_str(hostname),
		__entry->currmid,
		__entry->credits,
		__entry->credits_to_add,
		__entry->in_flight)
)

#define DEFINE_SMB3_CREDIT_EVENT(name)        \
DEFINE_EVENT(smb3_credit_class, smb3_##name,  \
	TP_PROTO(__u64	currmid,		\
		__u64 conn_id,			\
		char *hostname,			\
		int  credits,			\
		int  credits_to_add,	\
		int in_flight),			\
	TP_ARGS(currmid, conn_id, hostname, credits, credits_to_add, in_flight))

DEFINE_SMB3_CREDIT_EVENT(reconnect_with_invalid_credits);
DEFINE_SMB3_CREDIT_EVENT(reconnect_detected);
DEFINE_SMB3_CREDIT_EVENT(credit_timeout);
DEFINE_SMB3_CREDIT_EVENT(insufficient_credits);
DEFINE_SMB3_CREDIT_EVENT(too_many_credits);
DEFINE_SMB3_CREDIT_EVENT(add_credits);
DEFINE_SMB3_CREDIT_EVENT(adj_credits);
DEFINE_SMB3_CREDIT_EVENT(hdr_credits);
DEFINE_SMB3_CREDIT_EVENT(nblk_credits);
DEFINE_SMB3_CREDIT_EVENT(pend_credits);
DEFINE_SMB3_CREDIT_EVENT(wait_credits);
DEFINE_SMB3_CREDIT_EVENT(waitff_credits);
DEFINE_SMB3_CREDIT_EVENT(overflow_credits);
DEFINE_SMB3_CREDIT_EVENT(set_credits);

#endif /* _CIFS_TRACE_H */

#undef TRACE_INCLUDE_PATH
#define TRACE_INCLUDE_PATH .
#define TRACE_INCLUDE_FILE trace
#include <trace/define_trace.h>
