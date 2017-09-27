/*
 * Copyright (c) 2014 Christoph Hellwig.
 */
#undef TRACE_SYSTEM
#define TRACE_SYSTEM nfsd

#if !defined(_NFSD_TRACE_H) || defined(TRACE_HEADER_MULTI_READ)
#define _NFSD_TRACE_H

#include <linux/tracepoint.h>
#include "nfsfh.h"

DECLARE_EVENT_CLASS(nfsd_io_class,
	TP_PROTO(struct svc_rqst *rqstp,
		 struct svc_fh	*fhp,
		 loff_t		offset,
		 int		len),
	TP_ARGS(rqstp, fhp, offset, len),
	TP_STRUCT__entry(
		__field(__be32, xid)
		__field_struct(struct knfsd_fh, fh)
		__field(loff_t, offset)
		__field(int, len)
	),
	TP_fast_assign(
		__entry->xid = rqstp->rq_xid,
		fh_copy_shallow(&__entry->fh, &fhp->fh_handle);
		__entry->offset = offset;
		__entry->len = len;
	),
	TP_printk("xid=0x%x fh=0x%x offset=%lld len=%d",
		  __be32_to_cpu(__entry->xid), knfsd_fh_hash(&__entry->fh),
		  __entry->offset, __entry->len)
)

#define DEFINE_NFSD_IO_EVENT(name)		\
DEFINE_EVENT(nfsd_io_class, name,		\
	TP_PROTO(struct svc_rqst *rqstp,	\
		 struct svc_fh	*fhp,		\
		 loff_t		offset,		\
		 int		len),		\
	TP_ARGS(rqstp, fhp, offset, len))

DEFINE_NFSD_IO_EVENT(read_start);
DEFINE_NFSD_IO_EVENT(read_opened);
DEFINE_NFSD_IO_EVENT(read_io_done);
DEFINE_NFSD_IO_EVENT(read_done);
DEFINE_NFSD_IO_EVENT(write_start);
DEFINE_NFSD_IO_EVENT(write_opened);
DEFINE_NFSD_IO_EVENT(write_io_done);
DEFINE_NFSD_IO_EVENT(write_done);

#include "state.h"

DECLARE_EVENT_CLASS(nfsd_stateid_class,
	TP_PROTO(stateid_t *stp),
	TP_ARGS(stp),
	TP_STRUCT__entry(
		__field(u32, cl_boot)
		__field(u32, cl_id)
		__field(u32, si_id)
		__field(u32, si_generation)
	),
	TP_fast_assign(
		__entry->cl_boot = stp->si_opaque.so_clid.cl_boot;
		__entry->cl_id = stp->si_opaque.so_clid.cl_id;
		__entry->si_id = stp->si_opaque.so_id;
		__entry->si_generation = stp->si_generation;
	),
	TP_printk("client %08x:%08x stateid %08x:%08x",
		__entry->cl_boot,
		__entry->cl_id,
		__entry->si_id,
		__entry->si_generation)
)

#define DEFINE_STATEID_EVENT(name) \
DEFINE_EVENT(nfsd_stateid_class, name, \
	TP_PROTO(stateid_t *stp), \
	TP_ARGS(stp))
DEFINE_STATEID_EVENT(layoutstate_alloc);
DEFINE_STATEID_EVENT(layoutstate_unhash);
DEFINE_STATEID_EVENT(layoutstate_free);
DEFINE_STATEID_EVENT(layout_get_lookup_fail);
DEFINE_STATEID_EVENT(layout_commit_lookup_fail);
DEFINE_STATEID_EVENT(layout_return_lookup_fail);
DEFINE_STATEID_EVENT(layout_recall);
DEFINE_STATEID_EVENT(layout_recall_done);
DEFINE_STATEID_EVENT(layout_recall_fail);
DEFINE_STATEID_EVENT(layout_recall_release);

#endif /* _NFSD_TRACE_H */

#undef TRACE_INCLUDE_PATH
#define TRACE_INCLUDE_PATH .
#define TRACE_INCLUDE_FILE trace
#include <trace/define_trace.h>
