// SPDX-License-Identifier: GPL-2.0
/*
 * io_uring opcode handling table
 */
#include <linux/kernel.h>
#include <linux/errno.h>
#include <linux/fs.h>
#include <linux/file.h>
#include <linux/io_uring.h>

#include "io_uring.h"
#include "opdef.h"
#include "refs.h"
#include "tctx.h"
#include "sqpoll.h"
#include "fdinfo.h"
#include "kbuf.h"
#include "rsrc.h"

#include "xattr.h"
#include "nop.h"
#include "fs.h"
#include "splice.h"
#include "sync.h"
#include "advise.h"
#include "openclose.h"
#include "uring_cmd.h"
#include "epoll.h"
#include "statx.h"
#include "net.h"
#include "msg_ring.h"
#include "timeout.h"
#include "poll.h"
#include "cancel.h"
#include "rw.h"
#include "waitid.h"
#include "futex.h"
#include "truncate.h"

static int io_no_issue(struct io_kiocb *req, unsigned int issue_flags)
{
	WARN_ON_ONCE(1);
	return -ECANCELED;
}

static __maybe_unused int io_eopnotsupp_prep(struct io_kiocb *kiocb,
					     const struct io_uring_sqe *sqe)
{
	return -EOPNOTSUPP;
}

const struct io_issue_def io_issue_defs[] = {
	[IORING_OP_NOP] = {
		.audit_skip		= 1,
		.iopoll			= 1,
		.prep			= io_nop_prep,
		.issue			= io_nop,
	},
	[IORING_OP_READV] = {
		.needs_file		= 1,
		.unbound_nonreg_file	= 1,
		.pollin			= 1,
		.buffer_select		= 1,
		.plug			= 1,
		.audit_skip		= 1,
		.ioprio			= 1,
		.iopoll			= 1,
		.iopoll_queue		= 1,
		.vectored		= 1,
		.prep			= io_prep_rwv,
		.issue			= io_read,
	},
	[IORING_OP_WRITEV] = {
		.needs_file		= 1,
		.hash_reg_file		= 1,
		.unbound_nonreg_file	= 1,
		.pollout		= 1,
		.plug			= 1,
		.audit_skip		= 1,
		.ioprio			= 1,
		.iopoll			= 1,
		.iopoll_queue		= 1,
		.vectored		= 1,
		.prep			= io_prep_rwv,
		.issue			= io_write,
	},
	[IORING_OP_FSYNC] = {
		.needs_file		= 1,
		.audit_skip		= 1,
		.prep			= io_fsync_prep,
		.issue			= io_fsync,
	},
	[IORING_OP_READ_FIXED] = {
		.needs_file		= 1,
		.unbound_nonreg_file	= 1,
		.pollin			= 1,
		.plug			= 1,
		.audit_skip		= 1,
		.ioprio			= 1,
		.iopoll			= 1,
		.iopoll_queue		= 1,
		.prep			= io_prep_rw_fixed,
		.issue			= io_read,
	},
	[IORING_OP_WRITE_FIXED] = {
		.needs_file		= 1,
		.hash_reg_file		= 1,
		.unbound_nonreg_file	= 1,
		.pollout		= 1,
		.plug			= 1,
		.audit_skip		= 1,
		.ioprio			= 1,
		.iopoll			= 1,
		.iopoll_queue		= 1,
		.prep			= io_prep_rw_fixed,
		.issue			= io_write,
	},
	[IORING_OP_POLL_ADD] = {
		.needs_file		= 1,
		.unbound_nonreg_file	= 1,
		.audit_skip		= 1,
		.prep			= io_poll_add_prep,
		.issue			= io_poll_add,
	},
	[IORING_OP_POLL_REMOVE] = {
		.audit_skip		= 1,
		.prep			= io_poll_remove_prep,
		.issue			= io_poll_remove,
	},
	[IORING_OP_SYNC_FILE_RANGE] = {
		.needs_file		= 1,
		.audit_skip		= 1,
		.prep			= io_sfr_prep,
		.issue			= io_sync_file_range,
	},
	[IORING_OP_SENDMSG] = {
		.needs_file		= 1,
		.unbound_nonreg_file	= 1,
		.pollout		= 1,
		.ioprio			= 1,
		.manual_alloc		= 1,
#if defined(CONFIG_NET)
		.prep			= io_sendmsg_prep,
		.issue			= io_sendmsg,
#else
		.prep			= io_eopnotsupp_prep,
#endif
	},
	[IORING_OP_RECVMSG] = {
		.needs_file		= 1,
		.unbound_nonreg_file	= 1,
		.pollin			= 1,
		.buffer_select		= 1,
		.ioprio			= 1,
		.manual_alloc		= 1,
#if defined(CONFIG_NET)
		.prep			= io_recvmsg_prep,
		.issue			= io_recvmsg,
#else
		.prep			= io_eopnotsupp_prep,
#endif
	},
	[IORING_OP_TIMEOUT] = {
		.audit_skip		= 1,
		.prep			= io_timeout_prep,
		.issue			= io_timeout,
	},
	[IORING_OP_TIMEOUT_REMOVE] = {
		/* used by timeout updates' prep() */
		.audit_skip		= 1,
		.prep			= io_timeout_remove_prep,
		.issue			= io_timeout_remove,
	},
	[IORING_OP_ACCEPT] = {
		.needs_file		= 1,
		.unbound_nonreg_file	= 1,
		.pollin			= 1,
		.poll_exclusive		= 1,
		.ioprio			= 1,	/* used for flags */
#if defined(CONFIG_NET)
		.prep			= io_accept_prep,
		.issue			= io_accept,
#else
		.prep			= io_eopnotsupp_prep,
#endif
	},
	[IORING_OP_ASYNC_CANCEL] = {
		.audit_skip		= 1,
		.prep			= io_async_cancel_prep,
		.issue			= io_async_cancel,
	},
	[IORING_OP_LINK_TIMEOUT] = {
		.audit_skip		= 1,
		.prep			= io_link_timeout_prep,
		.issue			= io_no_issue,
	},
	[IORING_OP_CONNECT] = {
		.needs_file		= 1,
		.unbound_nonreg_file	= 1,
		.pollout		= 1,
#if defined(CONFIG_NET)
		.prep			= io_connect_prep,
		.issue			= io_connect,
#else
		.prep			= io_eopnotsupp_prep,
#endif
	},
	[IORING_OP_FALLOCATE] = {
		.needs_file		= 1,
		.prep			= io_fallocate_prep,
		.issue			= io_fallocate,
	},
	[IORING_OP_OPENAT] = {
		.prep			= io_openat_prep,
		.issue			= io_openat,
	},
	[IORING_OP_CLOSE] = {
		.prep			= io_close_prep,
		.issue			= io_close,
	},
	[IORING_OP_FILES_UPDATE] = {
		.audit_skip		= 1,
		.iopoll			= 1,
		.prep			= io_files_update_prep,
		.issue			= io_files_update,
	},
	[IORING_OP_STATX] = {
		.audit_skip		= 1,
		.prep			= io_statx_prep,
		.issue			= io_statx,
	},
	[IORING_OP_READ] = {
		.needs_file		= 1,
		.unbound_nonreg_file	= 1,
		.pollin			= 1,
		.buffer_select		= 1,
		.plug			= 1,
		.audit_skip		= 1,
		.ioprio			= 1,
		.iopoll			= 1,
		.iopoll_queue		= 1,
		.prep			= io_prep_rw,
		.issue			= io_read,
	},
	[IORING_OP_WRITE] = {
		.needs_file		= 1,
		.hash_reg_file		= 1,
		.unbound_nonreg_file	= 1,
		.pollout		= 1,
		.plug			= 1,
		.audit_skip		= 1,
		.ioprio			= 1,
		.iopoll			= 1,
		.iopoll_queue		= 1,
		.prep			= io_prep_rw,
		.issue			= io_write,
	},
	[IORING_OP_FADVISE] = {
		.needs_file		= 1,
		.audit_skip		= 1,
		.prep			= io_fadvise_prep,
		.issue			= io_fadvise,
	},
	[IORING_OP_MADVISE] = {
		.audit_skip		= 1,
		.prep			= io_madvise_prep,
		.issue			= io_madvise,
	},
	[IORING_OP_SEND] = {
		.needs_file		= 1,
		.unbound_nonreg_file	= 1,
		.pollout		= 1,
		.audit_skip		= 1,
		.ioprio			= 1,
		.manual_alloc		= 1,
#if defined(CONFIG_NET)
		.prep			= io_sendmsg_prep,
		.issue			= io_send,
#else
		.prep			= io_eopnotsupp_prep,
#endif
	},
	[IORING_OP_RECV] = {
		.needs_file		= 1,
		.unbound_nonreg_file	= 1,
		.pollin			= 1,
		.buffer_select		= 1,
		.audit_skip		= 1,
		.ioprio			= 1,
#if defined(CONFIG_NET)
		.prep			= io_recvmsg_prep,
		.issue			= io_recv,
#else
		.prep			= io_eopnotsupp_prep,
#endif
	},
	[IORING_OP_OPENAT2] = {
		.prep			= io_openat2_prep,
		.issue			= io_openat2,
	},
	[IORING_OP_EPOLL_CTL] = {
		.unbound_nonreg_file	= 1,
		.audit_skip		= 1,
#if defined(CONFIG_EPOLL)
		.prep			= io_epoll_ctl_prep,
		.issue			= io_epoll_ctl,
#else
		.prep			= io_eopnotsupp_prep,
#endif
	},
	[IORING_OP_SPLICE] = {
		.needs_file		= 1,
		.hash_reg_file		= 1,
		.unbound_nonreg_file	= 1,
		.audit_skip		= 1,
		.prep			= io_splice_prep,
		.issue			= io_splice,
	},
	[IORING_OP_PROVIDE_BUFFERS] = {
		.audit_skip		= 1,
		.iopoll			= 1,
		.prep			= io_provide_buffers_prep,
		.issue			= io_provide_buffers,
	},
	[IORING_OP_REMOVE_BUFFERS] = {
		.audit_skip		= 1,
		.iopoll			= 1,
		.prep			= io_remove_buffers_prep,
		.issue			= io_remove_buffers,
	},
	[IORING_OP_TEE] = {
		.needs_file		= 1,
		.hash_reg_file		= 1,
		.unbound_nonreg_file	= 1,
		.audit_skip		= 1,
		.prep			= io_tee_prep,
		.issue			= io_tee,
	},
	[IORING_OP_SHUTDOWN] = {
		.needs_file		= 1,
#if defined(CONFIG_NET)
		.prep			= io_shutdown_prep,
		.issue			= io_shutdown,
#else
		.prep			= io_eopnotsupp_prep,
#endif
	},
	[IORING_OP_RENAMEAT] = {
		.prep			= io_renameat_prep,
		.issue			= io_renameat,
	},
	[IORING_OP_UNLINKAT] = {
		.prep			= io_unlinkat_prep,
		.issue			= io_unlinkat,
	},
	[IORING_OP_MKDIRAT] = {
		.prep			= io_mkdirat_prep,
		.issue			= io_mkdirat,
	},
	[IORING_OP_SYMLINKAT] = {
		.prep			= io_symlinkat_prep,
		.issue			= io_symlinkat,
	},
	[IORING_OP_LINKAT] = {
		.prep			= io_linkat_prep,
		.issue			= io_linkat,
	},
	[IORING_OP_MSG_RING] = {
		.needs_file		= 1,
		.iopoll			= 1,
		.prep			= io_msg_ring_prep,
		.issue			= io_msg_ring,
	},
	[IORING_OP_FSETXATTR] = {
		.needs_file = 1,
		.prep			= io_fsetxattr_prep,
		.issue			= io_fsetxattr,
	},
	[IORING_OP_SETXATTR] = {
		.prep			= io_setxattr_prep,
		.issue			= io_setxattr,
	},
	[IORING_OP_FGETXATTR] = {
		.needs_file = 1,
		.prep			= io_fgetxattr_prep,
		.issue			= io_fgetxattr,
	},
	[IORING_OP_GETXATTR] = {
		.prep			= io_getxattr_prep,
		.issue			= io_getxattr,
	},
	[IORING_OP_SOCKET] = {
		.audit_skip		= 1,
#if defined(CONFIG_NET)
		.prep			= io_socket_prep,
		.issue			= io_socket,
#else
		.prep			= io_eopnotsupp_prep,
#endif
	},
	[IORING_OP_URING_CMD] = {
		.needs_file		= 1,
		.plug			= 1,
		.iopoll			= 1,
		.iopoll_queue		= 1,
		.prep			= io_uring_cmd_prep,
		.issue			= io_uring_cmd,
	},
	[IORING_OP_SEND_ZC] = {
		.needs_file		= 1,
		.unbound_nonreg_file	= 1,
		.pollout		= 1,
		.audit_skip		= 1,
		.ioprio			= 1,
		.manual_alloc		= 1,
#if defined(CONFIG_NET)
		.prep			= io_send_zc_prep,
		.issue			= io_send_zc,
#else
		.prep			= io_eopnotsupp_prep,
#endif
	},
	[IORING_OP_SENDMSG_ZC] = {
		.needs_file		= 1,
		.unbound_nonreg_file	= 1,
		.pollout		= 1,
		.ioprio			= 1,
		.manual_alloc		= 1,
#if defined(CONFIG_NET)
		.prep			= io_send_zc_prep,
		.issue			= io_sendmsg_zc,
#else
		.prep			= io_eopnotsupp_prep,
#endif
	},
	[IORING_OP_READ_MULTISHOT] = {
		.needs_file		= 1,
		.unbound_nonreg_file	= 1,
		.pollin			= 1,
		.buffer_select		= 1,
		.audit_skip		= 1,
		.prep			= io_read_mshot_prep,
		.issue			= io_read_mshot,
	},
	[IORING_OP_WAITID] = {
		.prep			= io_waitid_prep,
		.issue			= io_waitid,
	},
	[IORING_OP_FUTEX_WAIT] = {
#if defined(CONFIG_FUTEX)
		.prep			= io_futex_prep,
		.issue			= io_futex_wait,
#else
		.prep			= io_eopnotsupp_prep,
#endif
	},
	[IORING_OP_FUTEX_WAKE] = {
#if defined(CONFIG_FUTEX)
		.prep			= io_futex_prep,
		.issue			= io_futex_wake,
#else
		.prep			= io_eopnotsupp_prep,
#endif
	},
	[IORING_OP_FUTEX_WAITV] = {
#if defined(CONFIG_FUTEX)
		.prep			= io_futexv_prep,
		.issue			= io_futexv_wait,
#else
		.prep			= io_eopnotsupp_prep,
#endif
	},
	[IORING_OP_FIXED_FD_INSTALL] = {
		.needs_file		= 1,
		.prep			= io_install_fixed_fd_prep,
		.issue			= io_install_fixed_fd,
	},
	[IORING_OP_FTRUNCATE] = {
		.needs_file		= 1,
		.hash_reg_file		= 1,
		.prep			= io_ftruncate_prep,
		.issue			= io_ftruncate,
	},
};

const struct io_cold_def io_cold_defs[] = {
	[IORING_OP_NOP] = {
		.name			= "NOP",
	},
	[IORING_OP_READV] = {
		.async_size		= sizeof(struct io_async_rw),
		.name			= "READV",
		.prep_async		= io_readv_prep_async,
		.cleanup		= io_readv_writev_cleanup,
		.fail			= io_rw_fail,
	},
	[IORING_OP_WRITEV] = {
		.async_size		= sizeof(struct io_async_rw),
		.name			= "WRITEV",
		.prep_async		= io_writev_prep_async,
		.cleanup		= io_readv_writev_cleanup,
		.fail			= io_rw_fail,
	},
	[IORING_OP_FSYNC] = {
		.name			= "FSYNC",
	},
	[IORING_OP_READ_FIXED] = {
		.async_size		= sizeof(struct io_async_rw),
		.name			= "READ_FIXED",
		.fail			= io_rw_fail,
	},
	[IORING_OP_WRITE_FIXED] = {
		.async_size		= sizeof(struct io_async_rw),
		.name			= "WRITE_FIXED",
		.fail			= io_rw_fail,
	},
	[IORING_OP_POLL_ADD] = {
		.name			= "POLL_ADD",
	},
	[IORING_OP_POLL_REMOVE] = {
		.name			= "POLL_REMOVE",
	},
	[IORING_OP_SYNC_FILE_RANGE] = {
		.name			= "SYNC_FILE_RANGE",
	},
	[IORING_OP_SENDMSG] = {
		.name			= "SENDMSG",
#if defined(CONFIG_NET)
		.async_size		= sizeof(struct io_async_msghdr),
		.prep_async		= io_sendmsg_prep_async,
		.cleanup		= io_sendmsg_recvmsg_cleanup,
		.fail			= io_sendrecv_fail,
#endif
	},
	[IORING_OP_RECVMSG] = {
		.name			= "RECVMSG",
#if defined(CONFIG_NET)
		.async_size		= sizeof(struct io_async_msghdr),
		.prep_async		= io_recvmsg_prep_async,
		.cleanup		= io_sendmsg_recvmsg_cleanup,
		.fail			= io_sendrecv_fail,
#endif
	},
	[IORING_OP_TIMEOUT] = {
		.async_size		= sizeof(struct io_timeout_data),
		.name			= "TIMEOUT",
	},
	[IORING_OP_TIMEOUT_REMOVE] = {
		.name			= "TIMEOUT_REMOVE",
	},
	[IORING_OP_ACCEPT] = {
		.name			= "ACCEPT",
	},
	[IORING_OP_ASYNC_CANCEL] = {
		.name			= "ASYNC_CANCEL",
	},
	[IORING_OP_LINK_TIMEOUT] = {
		.async_size		= sizeof(struct io_timeout_data),
		.name			= "LINK_TIMEOUT",
	},
	[IORING_OP_CONNECT] = {
		.name			= "CONNECT",
#if defined(CONFIG_NET)
		.async_size		= sizeof(struct io_async_connect),
		.prep_async		= io_connect_prep_async,
#endif
	},
	[IORING_OP_FALLOCATE] = {
		.name			= "FALLOCATE",
	},
	[IORING_OP_OPENAT] = {
		.name			= "OPENAT",
		.cleanup		= io_open_cleanup,
	},
	[IORING_OP_CLOSE] = {
		.name			= "CLOSE",
	},
	[IORING_OP_FILES_UPDATE] = {
		.name			= "FILES_UPDATE",
	},
	[IORING_OP_STATX] = {
		.name			= "STATX",
		.cleanup		= io_statx_cleanup,
	},
	[IORING_OP_READ] = {
		.async_size		= sizeof(struct io_async_rw),
		.name			= "READ",
		.fail			= io_rw_fail,
	},
	[IORING_OP_WRITE] = {
		.async_size		= sizeof(struct io_async_rw),
		.name			= "WRITE",
		.fail			= io_rw_fail,
	},
	[IORING_OP_FADVISE] = {
		.name			= "FADVISE",
	},
	[IORING_OP_MADVISE] = {
		.name			= "MADVISE",
	},
	[IORING_OP_SEND] = {
		.name			= "SEND",
#if defined(CONFIG_NET)
		.async_size		= sizeof(struct io_async_msghdr),
		.fail			= io_sendrecv_fail,
		.prep_async		= io_send_prep_async,
#endif
	},
	[IORING_OP_RECV] = {
		.name			= "RECV",
#if defined(CONFIG_NET)
		.fail			= io_sendrecv_fail,
#endif
	},
	[IORING_OP_OPENAT2] = {
		.name			= "OPENAT2",
		.cleanup		= io_open_cleanup,
	},
	[IORING_OP_EPOLL_CTL] = {
		.name			= "EPOLL",
	},
	[IORING_OP_SPLICE] = {
		.name			= "SPLICE",
	},
	[IORING_OP_PROVIDE_BUFFERS] = {
		.name			= "PROVIDE_BUFFERS",
	},
	[IORING_OP_REMOVE_BUFFERS] = {
		.name			= "REMOVE_BUFFERS",
	},
	[IORING_OP_TEE] = {
		.name			= "TEE",
	},
	[IORING_OP_SHUTDOWN] = {
		.name			= "SHUTDOWN",
	},
	[IORING_OP_RENAMEAT] = {
		.name			= "RENAMEAT",
		.cleanup		= io_renameat_cleanup,
	},
	[IORING_OP_UNLINKAT] = {
		.name			= "UNLINKAT",
		.cleanup		= io_unlinkat_cleanup,
	},
	[IORING_OP_MKDIRAT] = {
		.name			= "MKDIRAT",
		.cleanup		= io_mkdirat_cleanup,
	},
	[IORING_OP_SYMLINKAT] = {
		.name			= "SYMLINKAT",
		.cleanup		= io_link_cleanup,
	},
	[IORING_OP_LINKAT] = {
		.name			= "LINKAT",
		.cleanup		= io_link_cleanup,
	},
	[IORING_OP_MSG_RING] = {
		.name			= "MSG_RING",
		.cleanup		= io_msg_ring_cleanup,
	},
	[IORING_OP_FSETXATTR] = {
		.name			= "FSETXATTR",
		.cleanup		= io_xattr_cleanup,
	},
	[IORING_OP_SETXATTR] = {
		.name			= "SETXATTR",
		.cleanup		= io_xattr_cleanup,
	},
	[IORING_OP_FGETXATTR] = {
		.name			= "FGETXATTR",
		.cleanup		= io_xattr_cleanup,
	},
	[IORING_OP_GETXATTR] = {
		.name			= "GETXATTR",
		.cleanup		= io_xattr_cleanup,
	},
	[IORING_OP_SOCKET] = {
		.name			= "SOCKET",
	},
	[IORING_OP_URING_CMD] = {
		.name			= "URING_CMD",
		.async_size		= 2 * sizeof(struct io_uring_sqe),
		.prep_async		= io_uring_cmd_prep_async,
	},
	[IORING_OP_SEND_ZC] = {
		.name			= "SEND_ZC",
#if defined(CONFIG_NET)
		.async_size		= sizeof(struct io_async_msghdr),
		.prep_async		= io_send_prep_async,
		.cleanup		= io_send_zc_cleanup,
		.fail			= io_sendrecv_fail,
#endif
	},
	[IORING_OP_SENDMSG_ZC] = {
		.name			= "SENDMSG_ZC",
#if defined(CONFIG_NET)
		.async_size		= sizeof(struct io_async_msghdr),
		.prep_async		= io_sendmsg_prep_async,
		.cleanup		= io_send_zc_cleanup,
		.fail			= io_sendrecv_fail,
#endif
	},
	[IORING_OP_READ_MULTISHOT] = {
		.name			= "READ_MULTISHOT",
	},
	[IORING_OP_WAITID] = {
		.name			= "WAITID",
		.async_size		= sizeof(struct io_waitid_async),
	},
	[IORING_OP_FUTEX_WAIT] = {
		.name			= "FUTEX_WAIT",
	},
	[IORING_OP_FUTEX_WAKE] = {
		.name			= "FUTEX_WAKE",
	},
	[IORING_OP_FUTEX_WAITV] = {
		.name			= "FUTEX_WAITV",
	},
	[IORING_OP_FIXED_FD_INSTALL] = {
		.name			= "FIXED_FD_INSTALL",
	},
	[IORING_OP_FTRUNCATE] = {
		.name			= "FTRUNCATE",
	},
};

const char *io_uring_get_opcode(u8 opcode)
{
	if (opcode < IORING_OP_LAST)
		return io_cold_defs[opcode].name;
	return "INVALID";
}

void __init io_uring_optable_init(void)
{
	int i;

	BUILD_BUG_ON(ARRAY_SIZE(io_cold_defs) != IORING_OP_LAST);
	BUILD_BUG_ON(ARRAY_SIZE(io_issue_defs) != IORING_OP_LAST);

	for (i = 0; i < ARRAY_SIZE(io_issue_defs); i++) {
		BUG_ON(!io_issue_defs[i].prep);
		if (io_issue_defs[i].prep != io_eopnotsupp_prep)
			BUG_ON(!io_issue_defs[i].issue);
		WARN_ON_ONCE(!io_cold_defs[i].name);
	}
}
