/* SPDX-License-Identifier: GPL-2.0 */
#undef TRACE_SYSTEM
#define TRACE_SYSTEM fuse

#if !defined(_TRACE_FUSE_H) || defined(TRACE_HEADER_MULTI_READ)
#define _TRACE_FUSE_H

#include <linux/tracepoint.h>

#define OPCODES							\
	EM( FUSE_LOOKUP,		"FUSE_LOOKUP")		\
	EM( FUSE_FORGET,		"FUSE_FORGET")		\
	EM( FUSE_GETATTR,		"FUSE_GETATTR")		\
	EM( FUSE_SETATTR,		"FUSE_SETATTR")		\
	EM( FUSE_READLINK,		"FUSE_READLINK")	\
	EM( FUSE_SYMLINK,		"FUSE_SYMLINK")		\
	EM( FUSE_MKNOD,			"FUSE_MKNOD")		\
	EM( FUSE_MKDIR,			"FUSE_MKDIR")		\
	EM( FUSE_UNLINK,		"FUSE_UNLINK")		\
	EM( FUSE_RMDIR,			"FUSE_RMDIR")		\
	EM( FUSE_RENAME,		"FUSE_RENAME")		\
	EM( FUSE_LINK,			"FUSE_LINK")		\
	EM( FUSE_OPEN,			"FUSE_OPEN")		\
	EM( FUSE_READ,			"FUSE_READ")		\
	EM( FUSE_WRITE,			"FUSE_WRITE")		\
	EM( FUSE_STATFS,		"FUSE_STATFS")		\
	EM( FUSE_RELEASE,		"FUSE_RELEASE")		\
	EM( FUSE_FSYNC,			"FUSE_FSYNC")		\
	EM( FUSE_SETXATTR,		"FUSE_SETXATTR")	\
	EM( FUSE_GETXATTR,		"FUSE_GETXATTR")	\
	EM( FUSE_LISTXATTR,		"FUSE_LISTXATTR")	\
	EM( FUSE_REMOVEXATTR,		"FUSE_REMOVEXATTR")	\
	EM( FUSE_FLUSH,			"FUSE_FLUSH")		\
	EM( FUSE_INIT,			"FUSE_INIT")		\
	EM( FUSE_OPENDIR,		"FUSE_OPENDIR")		\
	EM( FUSE_READDIR,		"FUSE_READDIR")		\
	EM( FUSE_RELEASEDIR,		"FUSE_RELEASEDIR")	\
	EM( FUSE_FSYNCDIR,		"FUSE_FSYNCDIR")	\
	EM( FUSE_GETLK,			"FUSE_GETLK")		\
	EM( FUSE_SETLK,			"FUSE_SETLK")		\
	EM( FUSE_SETLKW,		"FUSE_SETLKW")		\
	EM( FUSE_ACCESS,		"FUSE_ACCESS")		\
	EM( FUSE_CREATE,		"FUSE_CREATE")		\
	EM( FUSE_INTERRUPT,		"FUSE_INTERRUPT")	\
	EM( FUSE_BMAP,			"FUSE_BMAP")		\
	EM( FUSE_DESTROY,		"FUSE_DESTROY")		\
	EM( FUSE_IOCTL,			"FUSE_IOCTL")		\
	EM( FUSE_POLL,			"FUSE_POLL")		\
	EM( FUSE_NOTIFY_REPLY,		"FUSE_NOTIFY_REPLY")	\
	EM( FUSE_BATCH_FORGET,		"FUSE_BATCH_FORGET")	\
	EM( FUSE_FALLOCATE,		"FUSE_FALLOCATE")	\
	EM( FUSE_READDIRPLUS,		"FUSE_READDIRPLUS")	\
	EM( FUSE_RENAME2,		"FUSE_RENAME2")		\
	EM( FUSE_LSEEK,			"FUSE_LSEEK")		\
	EM( FUSE_COPY_FILE_RANGE,	"FUSE_COPY_FILE_RANGE")	\
	EM( FUSE_SETUPMAPPING,		"FUSE_SETUPMAPPING")	\
	EM( FUSE_REMOVEMAPPING,		"FUSE_REMOVEMAPPING")	\
	EM( FUSE_SYNCFS,		"FUSE_SYNCFS")		\
	EM( FUSE_TMPFILE,		"FUSE_TMPFILE")		\
	EM( FUSE_STATX,			"FUSE_STATX")		\
	EMe(CUSE_INIT,			"CUSE_INIT")

/*
 * This will turn the above table into TRACE_DEFINE_ENUM() for each of the
 * entries.
 */
#undef EM
#undef EMe
#define EM(a, b)	TRACE_DEFINE_ENUM(a);
#define EMe(a, b)	TRACE_DEFINE_ENUM(a);

OPCODES

/* Now we redfine it with the table that __print_symbolic needs. */
#undef EM
#undef EMe
#define EM(a, b)	{a, b},
#define EMe(a, b)	{a, b}

TRACE_EVENT(fuse_request_send,
	TP_PROTO(const struct fuse_req *req),

	TP_ARGS(req),

	TP_STRUCT__entry(
		__field(dev_t,			connection)
		__field(uint64_t,		unique)
		__field(enum fuse_opcode,	opcode)
		__field(uint32_t,		len)
	),

	TP_fast_assign(
		__entry->connection	=	req->fm->fc->dev;
		__entry->unique		=	req->in.h.unique;
		__entry->opcode		=	req->in.h.opcode;
		__entry->len		=	req->in.h.len;
	),

	TP_printk("connection %u req %llu opcode %u (%s) len %u ",
		  __entry->connection, __entry->unique, __entry->opcode,
		  __print_symbolic(__entry->opcode, OPCODES), __entry->len)
);

TRACE_EVENT(fuse_request_end,
	TP_PROTO(const struct fuse_req *req),

	TP_ARGS(req),

	TP_STRUCT__entry(
		__field(dev_t,		connection)
		__field(uint64_t,	unique)
		__field(uint32_t,	len)
		__field(int32_t,	error)
	),

	TP_fast_assign(
		__entry->connection	=	req->fm->fc->dev;
		__entry->unique		=	req->in.h.unique;
		__entry->len		=	req->out.h.len;
		__entry->error		=	req->out.h.error;
	),

	TP_printk("connection %u req %llu len %u error %d", __entry->connection,
		  __entry->unique, __entry->len, __entry->error)
);

#endif /* _TRACE_FUSE_H */

#undef TRACE_INCLUDE_PATH
#define TRACE_INCLUDE_PATH .
#define TRACE_INCLUDE_FILE fuse_trace
#include <trace/define_trace.h>
