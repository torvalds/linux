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
DEFINE_SMB3_INF_COMPOUND_ENTER_EVENT(delete_enter);
DEFINE_SMB3_INF_COMPOUND_ENTER_EVENT(mkdir_enter);


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
DEFINE_SMB3_INF_COMPOUND_DONE_EVENT(delete_done);
DEFINE_SMB3_INF_COMPOUND_DONE_EVENT(mkdir_done);


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
DEFINE_SMB3_INF_COMPOUND_ERR_EVENT(mkdir_err);
DEFINE_SMB3_INF_COMPOUND_ERR_EVENT(delete_err);

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
		__field(const char *, func_name)
		__field(int, rc)
	),
	TP_fast_assign(
		__entry->xid = xid;
		__entry->func_name = func_name;
		__entry->rc = rc;
	),
	TP_printk("\t%s: xid=%u rc=%d",
		__entry->func_name, __entry->xid, __entry->rc)
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
		__field(const char *, func_name)
	),
	TP_fast_assign(
		__entry->xid = xid;
		__entry->func_name = func_name;
	),
	TP_printk("\t%s: xid=%u",
		__entry->func_name, __entry->xid)
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


/*
 * For smb2/smb3 open (including create and mkdir) calls
 */

DECLARE_EVENT_CLASS(smb3_open_enter_class,
	TP_PROTO(unsigned int xid,
		__u32	tid,
		__u64	sesid,
		int	create_options,
		int	desired_access),
	TP_ARGS(xid, tid, sesid, create_options, desired_access),
	TP_STRUCT__entry(
		__field(unsigned int, xid)
		__field(__u32, tid)
		__field(__u64, sesid)
		__field(int, create_options)
		__field(int, desired_access)
	),
	TP_fast_assign(
		__entry->xid = xid;
		__entry->tid = tid;
		__entry->sesid = sesid;
		__entry->create_options = create_options;
		__entry->desired_access = desired_access;
	),
	TP_printk("xid=%u sid=0x%llx tid=0x%x cr_opts=0x%x des_access=0x%x",
		__entry->xid, __entry->sesid, __entry->tid,
		__entry->create_options, __entry->desired_access)
)

#define DEFINE_SMB3_OPEN_ENTER_EVENT(name)        \
DEFINE_EVENT(smb3_open_enter_class, smb3_##name,  \
	TP_PROTO(unsigned int xid,		\
		__u32	tid,			\
		__u64	sesid,			\
		int	create_options,		\
		int	desired_access),	\
	TP_ARGS(xid, tid, sesid, create_options, desired_access))

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

DECLARE_EVENT_CLASS(smb3_reconnect_class,
	TP_PROTO(__u64	currmid,
		char *hostname),
	TP_ARGS(currmid, hostname),
	TP_STRUCT__entry(
		__field(__u64, currmid)
		__field(char *, hostname)
	),
	TP_fast_assign(
		__entry->currmid = currmid;
		__entry->hostname = hostname;
	),
	TP_printk("server=%s current_mid=0x%llx",
		__entry->hostname,
		__entry->currmid)
)

#define DEFINE_SMB3_RECONNECT_EVENT(name)        \
DEFINE_EVENT(smb3_reconnect_class, smb3_##name,  \
	TP_PROTO(__u64	currmid,		\
		char *hostname),		\
	TP_ARGS(currmid, hostname))

DEFINE_SMB3_RECONNECT_EVENT(reconnect);
DEFINE_SMB3_RECONNECT_EVENT(partial_send_reconnect);

DECLARE_EVENT_CLASS(smb3_credit_class,
	TP_PROTO(__u64	currmid,
		char *hostname,
		int credits,
		int credits_to_add),
	TP_ARGS(currmid, hostname, credits, credits_to_add),
	TP_STRUCT__entry(
		__field(__u64, currmid)
		__field(char *, hostname)
		__field(int, credits)
		__field(int, credits_to_add)
	),
	TP_fast_assign(
		__entry->currmid = currmid;
		__entry->hostname = hostname;
		__entry->credits = credits;
		__entry->credits_to_add = credits_to_add;
	),
	TP_printk("server=%s current_mid=0x%llx credits=%d credits_to_add=%d",
		__entry->hostname,
		__entry->currmid,
		__entry->credits,
		__entry->credits_to_add)
)

#define DEFINE_SMB3_CREDIT_EVENT(name)        \
DEFINE_EVENT(smb3_credit_class, smb3_##name,  \
	TP_PROTO(__u64	currmid,		\
		char *hostname,			\
		int  credits,			\
		int  credits_to_add),		\
	TP_ARGS(currmid, hostname, credits, credits_to_add))

DEFINE_SMB3_CREDIT_EVENT(reconnect_with_invalid_credits);
DEFINE_SMB3_CREDIT_EVENT(credit_timeout);
DEFINE_SMB3_CREDIT_EVENT(add_credits);

#endif /* _CIFS_TRACE_H */

#undef TRACE_INCLUDE_PATH
#define TRACE_INCLUDE_PATH .
#define TRACE_INCLUDE_FILE trace
#include <trace/define_trace.h>
