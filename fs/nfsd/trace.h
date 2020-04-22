/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Copyright (c) 2014 Christoph Hellwig.
 */
#undef TRACE_SYSTEM
#define TRACE_SYSTEM nfsd

#if !defined(_NFSD_TRACE_H) || defined(TRACE_HEADER_MULTI_READ)
#define _NFSD_TRACE_H

#include <linux/tracepoint.h>
#include "export.h"
#include "nfsfh.h"

TRACE_EVENT(nfsd_compound,
	TP_PROTO(const struct svc_rqst *rqst,
		 u32 args_opcnt),
	TP_ARGS(rqst, args_opcnt),
	TP_STRUCT__entry(
		__field(u32, xid)
		__field(u32, args_opcnt)
	),
	TP_fast_assign(
		__entry->xid = be32_to_cpu(rqst->rq_xid);
		__entry->args_opcnt = args_opcnt;
	),
	TP_printk("xid=0x%08x opcnt=%u",
		__entry->xid, __entry->args_opcnt)
)

TRACE_EVENT(nfsd_compound_status,
	TP_PROTO(u32 args_opcnt,
		 u32 resp_opcnt,
		 __be32 status,
		 const char *name),
	TP_ARGS(args_opcnt, resp_opcnt, status, name),
	TP_STRUCT__entry(
		__field(u32, args_opcnt)
		__field(u32, resp_opcnt)
		__field(int, status)
		__string(name, name)
	),
	TP_fast_assign(
		__entry->args_opcnt = args_opcnt;
		__entry->resp_opcnt = resp_opcnt;
		__entry->status = be32_to_cpu(status);
		__assign_str(name, name);
	),
	TP_printk("op=%u/%u %s status=%d",
		__entry->resp_opcnt, __entry->args_opcnt,
		__get_str(name), __entry->status)
)

DECLARE_EVENT_CLASS(nfsd_fh_err_class,
	TP_PROTO(struct svc_rqst *rqstp,
		 struct svc_fh	*fhp,
		 int		status),
	TP_ARGS(rqstp, fhp, status),
	TP_STRUCT__entry(
		__field(u32, xid)
		__field(u32, fh_hash)
		__field(int, status)
	),
	TP_fast_assign(
		__entry->xid = be32_to_cpu(rqstp->rq_xid);
		__entry->fh_hash = knfsd_fh_hash(&fhp->fh_handle);
		__entry->status = status;
	),
	TP_printk("xid=0x%08x fh_hash=0x%08x status=%d",
		  __entry->xid, __entry->fh_hash,
		  __entry->status)
)

#define DEFINE_NFSD_FH_ERR_EVENT(name)		\
DEFINE_EVENT(nfsd_fh_err_class, nfsd_##name,	\
	TP_PROTO(struct svc_rqst *rqstp,	\
		 struct svc_fh	*fhp,		\
		 int		status),	\
	TP_ARGS(rqstp, fhp, status))

DEFINE_NFSD_FH_ERR_EVENT(set_fh_dentry_badexport);
DEFINE_NFSD_FH_ERR_EVENT(set_fh_dentry_badhandle);

TRACE_EVENT(nfsd_exp_find_key,
	TP_PROTO(const struct svc_expkey *key,
		 int status),
	TP_ARGS(key, status),
	TP_STRUCT__entry(
		__field(int, fsidtype)
		__array(u32, fsid, 6)
		__string(auth_domain, key->ek_client->name)
		__field(int, status)
	),
	TP_fast_assign(
		__entry->fsidtype = key->ek_fsidtype;
		memcpy(__entry->fsid, key->ek_fsid, 4*6);
		__assign_str(auth_domain, key->ek_client->name);
		__entry->status = status;
	),
	TP_printk("fsid=%x::%s domain=%s status=%d",
		__entry->fsidtype,
		__print_array(__entry->fsid, 6, 4),
		__get_str(auth_domain),
		__entry->status
	)
);

TRACE_EVENT(nfsd_expkey_update,
	TP_PROTO(const struct svc_expkey *key, const char *exp_path),
	TP_ARGS(key, exp_path),
	TP_STRUCT__entry(
		__field(int, fsidtype)
		__array(u32, fsid, 6)
		__string(auth_domain, key->ek_client->name)
		__string(path, exp_path)
		__field(bool, cache)
	),
	TP_fast_assign(
		__entry->fsidtype = key->ek_fsidtype;
		memcpy(__entry->fsid, key->ek_fsid, 4*6);
		__assign_str(auth_domain, key->ek_client->name);
		__assign_str(path, exp_path);
		__entry->cache = !test_bit(CACHE_NEGATIVE, &key->h.flags);
	),
	TP_printk("fsid=%x::%s domain=%s path=%s cache=%s",
		__entry->fsidtype,
		__print_array(__entry->fsid, 6, 4),
		__get_str(auth_domain),
		__get_str(path),
		__entry->cache ? "pos" : "neg"
	)
);

TRACE_EVENT(nfsd_exp_get_by_name,
	TP_PROTO(const struct svc_export *key,
		 int status),
	TP_ARGS(key, status),
	TP_STRUCT__entry(
		__string(path, key->ex_path.dentry->d_name.name)
		__string(auth_domain, key->ex_client->name)
		__field(int, status)
	),
	TP_fast_assign(
		__assign_str(path, key->ex_path.dentry->d_name.name);
		__assign_str(auth_domain, key->ex_client->name);
		__entry->status = status;
	),
	TP_printk("path=%s domain=%s status=%d",
		__get_str(path),
		__get_str(auth_domain),
		__entry->status
	)
);

TRACE_EVENT(nfsd_export_update,
	TP_PROTO(const struct svc_export *key),
	TP_ARGS(key),
	TP_STRUCT__entry(
		__string(path, key->ex_path.dentry->d_name.name)
		__string(auth_domain, key->ex_client->name)
		__field(bool, cache)
	),
	TP_fast_assign(
		__assign_str(path, key->ex_path.dentry->d_name.name);
		__assign_str(auth_domain, key->ex_client->name);
		__entry->cache = !test_bit(CACHE_NEGATIVE, &key->h.flags);
	),
	TP_printk("path=%s domain=%s cache=%s",
		__get_str(path),
		__get_str(auth_domain),
		__entry->cache ? "pos" : "neg"
	)
);

DECLARE_EVENT_CLASS(nfsd_io_class,
	TP_PROTO(struct svc_rqst *rqstp,
		 struct svc_fh	*fhp,
		 loff_t		offset,
		 unsigned long	len),
	TP_ARGS(rqstp, fhp, offset, len),
	TP_STRUCT__entry(
		__field(u32, xid)
		__field(u32, fh_hash)
		__field(loff_t, offset)
		__field(unsigned long, len)
	),
	TP_fast_assign(
		__entry->xid = be32_to_cpu(rqstp->rq_xid);
		__entry->fh_hash = knfsd_fh_hash(&fhp->fh_handle);
		__entry->offset = offset;
		__entry->len = len;
	),
	TP_printk("xid=0x%08x fh_hash=0x%08x offset=%lld len=%lu",
		  __entry->xid, __entry->fh_hash,
		  __entry->offset, __entry->len)
)

#define DEFINE_NFSD_IO_EVENT(name)		\
DEFINE_EVENT(nfsd_io_class, nfsd_##name,	\
	TP_PROTO(struct svc_rqst *rqstp,	\
		 struct svc_fh	*fhp,		\
		 loff_t		offset,		\
		 unsigned long	len),		\
	TP_ARGS(rqstp, fhp, offset, len))

DEFINE_NFSD_IO_EVENT(read_start);
DEFINE_NFSD_IO_EVENT(read_splice);
DEFINE_NFSD_IO_EVENT(read_vector);
DEFINE_NFSD_IO_EVENT(read_io_done);
DEFINE_NFSD_IO_EVENT(read_done);
DEFINE_NFSD_IO_EVENT(write_start);
DEFINE_NFSD_IO_EVENT(write_opened);
DEFINE_NFSD_IO_EVENT(write_io_done);
DEFINE_NFSD_IO_EVENT(write_done);

DECLARE_EVENT_CLASS(nfsd_err_class,
	TP_PROTO(struct svc_rqst *rqstp,
		 struct svc_fh	*fhp,
		 loff_t		offset,
		 int		status),
	TP_ARGS(rqstp, fhp, offset, status),
	TP_STRUCT__entry(
		__field(u32, xid)
		__field(u32, fh_hash)
		__field(loff_t, offset)
		__field(int, status)
	),
	TP_fast_assign(
		__entry->xid = be32_to_cpu(rqstp->rq_xid);
		__entry->fh_hash = knfsd_fh_hash(&fhp->fh_handle);
		__entry->offset = offset;
		__entry->status = status;
	),
	TP_printk("xid=0x%08x fh_hash=0x%08x offset=%lld status=%d",
		  __entry->xid, __entry->fh_hash,
		  __entry->offset, __entry->status)
)

#define DEFINE_NFSD_ERR_EVENT(name)		\
DEFINE_EVENT(nfsd_err_class, nfsd_##name,	\
	TP_PROTO(struct svc_rqst *rqstp,	\
		 struct svc_fh	*fhp,		\
		 loff_t		offset,		\
		 int		len),		\
	TP_ARGS(rqstp, fhp, offset, len))

DEFINE_NFSD_ERR_EVENT(read_err);
DEFINE_NFSD_ERR_EVENT(write_err);

#include "state.h"
#include "filecache.h"
#include "vfs.h"

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
DEFINE_EVENT(nfsd_stateid_class, nfsd_##name, \
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

TRACE_DEFINE_ENUM(NFSD_FILE_HASHED);
TRACE_DEFINE_ENUM(NFSD_FILE_PENDING);
TRACE_DEFINE_ENUM(NFSD_FILE_BREAK_READ);
TRACE_DEFINE_ENUM(NFSD_FILE_BREAK_WRITE);
TRACE_DEFINE_ENUM(NFSD_FILE_REFERENCED);

#define show_nf_flags(val)						\
	__print_flags(val, "|",						\
		{ 1 << NFSD_FILE_HASHED,	"HASHED" },		\
		{ 1 << NFSD_FILE_PENDING,	"PENDING" },		\
		{ 1 << NFSD_FILE_BREAK_READ,	"BREAK_READ" },		\
		{ 1 << NFSD_FILE_BREAK_WRITE,	"BREAK_WRITE" },	\
		{ 1 << NFSD_FILE_REFERENCED,	"REFERENCED"})

/* FIXME: This should probably be fleshed out in the future. */
#define show_nf_may(val)						\
	__print_flags(val, "|",						\
		{ NFSD_MAY_READ,		"READ" },		\
		{ NFSD_MAY_WRITE,		"WRITE" },		\
		{ NFSD_MAY_NOT_BREAK_LEASE,	"NOT_BREAK_LEASE" })

DECLARE_EVENT_CLASS(nfsd_file_class,
	TP_PROTO(struct nfsd_file *nf),
	TP_ARGS(nf),
	TP_STRUCT__entry(
		__field(unsigned int, nf_hashval)
		__field(void *, nf_inode)
		__field(int, nf_ref)
		__field(unsigned long, nf_flags)
		__field(unsigned char, nf_may)
		__field(struct file *, nf_file)
	),
	TP_fast_assign(
		__entry->nf_hashval = nf->nf_hashval;
		__entry->nf_inode = nf->nf_inode;
		__entry->nf_ref = refcount_read(&nf->nf_ref);
		__entry->nf_flags = nf->nf_flags;
		__entry->nf_may = nf->nf_may;
		__entry->nf_file = nf->nf_file;
	),
	TP_printk("hash=0x%x inode=0x%p ref=%d flags=%s may=%s file=%p",
		__entry->nf_hashval,
		__entry->nf_inode,
		__entry->nf_ref,
		show_nf_flags(__entry->nf_flags),
		show_nf_may(__entry->nf_may),
		__entry->nf_file)
)

#define DEFINE_NFSD_FILE_EVENT(name) \
DEFINE_EVENT(nfsd_file_class, name, \
	TP_PROTO(struct nfsd_file *nf), \
	TP_ARGS(nf))

DEFINE_NFSD_FILE_EVENT(nfsd_file_alloc);
DEFINE_NFSD_FILE_EVENT(nfsd_file_put_final);
DEFINE_NFSD_FILE_EVENT(nfsd_file_unhash);
DEFINE_NFSD_FILE_EVENT(nfsd_file_put);
DEFINE_NFSD_FILE_EVENT(nfsd_file_unhash_and_release_locked);

TRACE_EVENT(nfsd_file_acquire,
	TP_PROTO(struct svc_rqst *rqstp, unsigned int hash,
		 struct inode *inode, unsigned int may_flags,
		 struct nfsd_file *nf, __be32 status),

	TP_ARGS(rqstp, hash, inode, may_flags, nf, status),

	TP_STRUCT__entry(
		__field(u32, xid)
		__field(unsigned int, hash)
		__field(void *, inode)
		__field(unsigned int, may_flags)
		__field(int, nf_ref)
		__field(unsigned long, nf_flags)
		__field(unsigned char, nf_may)
		__field(struct file *, nf_file)
		__field(u32, status)
	),

	TP_fast_assign(
		__entry->xid = be32_to_cpu(rqstp->rq_xid);
		__entry->hash = hash;
		__entry->inode = inode;
		__entry->may_flags = may_flags;
		__entry->nf_ref = nf ? refcount_read(&nf->nf_ref) : 0;
		__entry->nf_flags = nf ? nf->nf_flags : 0;
		__entry->nf_may = nf ? nf->nf_may : 0;
		__entry->nf_file = nf ? nf->nf_file : NULL;
		__entry->status = be32_to_cpu(status);
	),

	TP_printk("xid=0x%x hash=0x%x inode=0x%p may_flags=%s ref=%d nf_flags=%s nf_may=%s nf_file=0x%p status=%u",
			__entry->xid, __entry->hash, __entry->inode,
			show_nf_may(__entry->may_flags), __entry->nf_ref,
			show_nf_flags(__entry->nf_flags),
			show_nf_may(__entry->nf_may), __entry->nf_file,
			__entry->status)
);

DECLARE_EVENT_CLASS(nfsd_file_search_class,
	TP_PROTO(struct inode *inode, unsigned int hash, int found),
	TP_ARGS(inode, hash, found),
	TP_STRUCT__entry(
		__field(struct inode *, inode)
		__field(unsigned int, hash)
		__field(int, found)
	),
	TP_fast_assign(
		__entry->inode = inode;
		__entry->hash = hash;
		__entry->found = found;
	),
	TP_printk("hash=0x%x inode=0x%p found=%d", __entry->hash,
			__entry->inode, __entry->found)
);

#define DEFINE_NFSD_FILE_SEARCH_EVENT(name)				\
DEFINE_EVENT(nfsd_file_search_class, name,				\
	TP_PROTO(struct inode *inode, unsigned int hash, int found),	\
	TP_ARGS(inode, hash, found))

DEFINE_NFSD_FILE_SEARCH_EVENT(nfsd_file_close_inode_sync);
DEFINE_NFSD_FILE_SEARCH_EVENT(nfsd_file_close_inode);
DEFINE_NFSD_FILE_SEARCH_EVENT(nfsd_file_is_cached);

TRACE_EVENT(nfsd_file_fsnotify_handle_event,
	TP_PROTO(struct inode *inode, u32 mask),
	TP_ARGS(inode, mask),
	TP_STRUCT__entry(
		__field(struct inode *, inode)
		__field(unsigned int, nlink)
		__field(umode_t, mode)
		__field(u32, mask)
	),
	TP_fast_assign(
		__entry->inode = inode;
		__entry->nlink = inode->i_nlink;
		__entry->mode = inode->i_mode;
		__entry->mask = mask;
	),
	TP_printk("inode=0x%p nlink=%u mode=0%ho mask=0x%x", __entry->inode,
			__entry->nlink, __entry->mode, __entry->mask)
);

#endif /* _NFSD_TRACE_H */

#undef TRACE_INCLUDE_PATH
#define TRACE_INCLUDE_PATH .
#define TRACE_INCLUDE_FILE trace
#include <trace/define_trace.h>
