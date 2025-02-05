/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Copyright (c) 2024 Trond Myklebust <trond.myklebust@hammerspace.com>
 * Copyright (C) 2024 Mike Snitzer <snitzer@hammerspace.com>
 */
#undef TRACE_SYSTEM
#define TRACE_SYSTEM nfs_localio

#if !defined(_TRACE_NFS_COMMON_LOCALIO_H) || defined(TRACE_HEADER_MULTI_READ)
#define _TRACE_NFS_COMMON_LOCALIO_H

#include <linux/tracepoint.h>

#include <trace/misc/fs.h>
#include <trace/misc/nfs.h>
#include <trace/misc/sunrpc.h>

DECLARE_EVENT_CLASS(nfs_local_client_event,
		TP_PROTO(
			const struct nfs_client *clp
		),

		TP_ARGS(clp),

		TP_STRUCT__entry(
			__field(unsigned int, protocol)
			__string(server, clp->cl_hostname)
		),

		TP_fast_assign(
			__entry->protocol = clp->rpc_ops->version;
			__assign_str(server);
		),

		TP_printk(
			"server=%s NFSv%u", __get_str(server), __entry->protocol
		)
);

#define DEFINE_NFS_LOCAL_CLIENT_EVENT(name) \
	DEFINE_EVENT(nfs_local_client_event, name, \
			TP_PROTO( \
				const struct nfs_client *clp \
			), \
			TP_ARGS(clp))

DEFINE_NFS_LOCAL_CLIENT_EVENT(nfs_localio_enable_client);
DEFINE_NFS_LOCAL_CLIENT_EVENT(nfs_localio_disable_client);

#endif /* _TRACE_NFS_COMMON_LOCALIO_H */

#undef TRACE_INCLUDE_PATH
#define TRACE_INCLUDE_PATH .
#define TRACE_INCLUDE_FILE localio_trace
/* This part must be outside protection */
#include <trace/define_trace.h>
