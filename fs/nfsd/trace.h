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

#define NFSD_TRACE_PROC_RES_FIELDS \
		__field(unsigned int, netns_ino) \
		__field(u32, xid) \
		__field(unsigned long, status) \
		__array(unsigned char, server, sizeof(struct sockaddr_in6)) \
		__array(unsigned char, client, sizeof(struct sockaddr_in6))

#define NFSD_TRACE_PROC_RES_ASSIGNMENTS(error) \
		do { \
			__entry->netns_ino = SVC_NET(rqstp)->ns.inum; \
			__entry->xid = be32_to_cpu(rqstp->rq_xid); \
			__entry->status = be32_to_cpu(error); \
			memcpy(__entry->server, &rqstp->rq_xprt->xpt_local, \
			       rqstp->rq_xprt->xpt_locallen); \
			memcpy(__entry->client, &rqstp->rq_xprt->xpt_remote, \
			       rqstp->rq_xprt->xpt_remotelen); \
		} while (0);

DECLARE_EVENT_CLASS(nfsd_xdr_err_class,
	TP_PROTO(
		const struct svc_rqst *rqstp
	),
	TP_ARGS(rqstp),
	TP_STRUCT__entry(
		__field(unsigned int, netns_ino)
		__field(u32, xid)
		__field(u32, vers)
		__field(u32, proc)
		__sockaddr(server, rqstp->rq_xprt->xpt_locallen)
		__sockaddr(client, rqstp->rq_xprt->xpt_remotelen)
	),
	TP_fast_assign(
		const struct svc_xprt *xprt = rqstp->rq_xprt;

		__entry->netns_ino = xprt->xpt_net->ns.inum;
		__entry->xid = be32_to_cpu(rqstp->rq_xid);
		__entry->vers = rqstp->rq_vers;
		__entry->proc = rqstp->rq_proc;
		__assign_sockaddr(server, &xprt->xpt_local, xprt->xpt_locallen);
		__assign_sockaddr(client, &xprt->xpt_remote, xprt->xpt_remotelen);
	),
	TP_printk("xid=0x%08x vers=%u proc=%u",
		__entry->xid, __entry->vers, __entry->proc
	)
);

#define DEFINE_NFSD_XDR_ERR_EVENT(name) \
DEFINE_EVENT(nfsd_xdr_err_class, nfsd_##name##_err, \
	TP_PROTO(const struct svc_rqst *rqstp), \
	TP_ARGS(rqstp))

DEFINE_NFSD_XDR_ERR_EVENT(garbage_args);
DEFINE_NFSD_XDR_ERR_EVENT(cant_encode);

#define show_nfsd_may_flags(x)						\
	__print_flags(x, "|",						\
		{ NFSD_MAY_EXEC,		"EXEC" },		\
		{ NFSD_MAY_WRITE,		"WRITE" },		\
		{ NFSD_MAY_READ,		"READ" },		\
		{ NFSD_MAY_SATTR,		"SATTR" },		\
		{ NFSD_MAY_TRUNC,		"TRUNC" },		\
		{ NFSD_MAY_LOCK,		"LOCK" },		\
		{ NFSD_MAY_OWNER_OVERRIDE,	"OWNER_OVERRIDE" },	\
		{ NFSD_MAY_LOCAL_ACCESS,	"LOCAL_ACCESS" },	\
		{ NFSD_MAY_BYPASS_GSS_ON_ROOT,	"BYPASS_GSS_ON_ROOT" },	\
		{ NFSD_MAY_NOT_BREAK_LEASE,	"NOT_BREAK_LEASE" },	\
		{ NFSD_MAY_BYPASS_GSS,		"BYPASS_GSS" },		\
		{ NFSD_MAY_READ_IF_EXEC,	"READ_IF_EXEC" },	\
		{ NFSD_MAY_64BIT_COOKIE,	"64BIT_COOKIE" })

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

TRACE_EVENT(nfsd_compound_decode_err,
	TP_PROTO(
		const struct svc_rqst *rqstp,
		u32 args_opcnt,
		u32 resp_opcnt,
		u32 opnum,
		__be32 status
	),
	TP_ARGS(rqstp, args_opcnt, resp_opcnt, opnum, status),
	TP_STRUCT__entry(
		NFSD_TRACE_PROC_RES_FIELDS

		__field(u32, args_opcnt)
		__field(u32, resp_opcnt)
		__field(u32, opnum)
	),
	TP_fast_assign(
		NFSD_TRACE_PROC_RES_ASSIGNMENTS(status)

		__entry->args_opcnt = args_opcnt;
		__entry->resp_opcnt = resp_opcnt;
		__entry->opnum = opnum;
	),
	TP_printk("op=%u/%u opnum=%u status=%lu",
		__entry->resp_opcnt, __entry->args_opcnt,
		__entry->opnum, __entry->status)
);

TRACE_EVENT(nfsd_compound_encode_err,
	TP_PROTO(
		const struct svc_rqst *rqstp,
		u32 opnum,
		__be32 status
	),
	TP_ARGS(rqstp, opnum, status),
	TP_STRUCT__entry(
		NFSD_TRACE_PROC_RES_FIELDS

		__field(u32, opnum)
	),
	TP_fast_assign(
		NFSD_TRACE_PROC_RES_ASSIGNMENTS(status)

		__entry->opnum = opnum;
	),
	TP_printk("opnum=%u status=%lu",
		__entry->opnum, __entry->status)
);


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
		 u64		offset,
		 u32		len),
	TP_ARGS(rqstp, fhp, offset, len),
	TP_STRUCT__entry(
		__field(u32, xid)
		__field(u32, fh_hash)
		__field(u64, offset)
		__field(u32, len)
	),
	TP_fast_assign(
		__entry->xid = be32_to_cpu(rqstp->rq_xid);
		__entry->fh_hash = knfsd_fh_hash(&fhp->fh_handle);
		__entry->offset = offset;
		__entry->len = len;
	),
	TP_printk("xid=0x%08x fh_hash=0x%08x offset=%llu len=%u",
		  __entry->xid, __entry->fh_hash,
		  __entry->offset, __entry->len)
)

#define DEFINE_NFSD_IO_EVENT(name)		\
DEFINE_EVENT(nfsd_io_class, nfsd_##name,	\
	TP_PROTO(struct svc_rqst *rqstp,	\
		 struct svc_fh	*fhp,		\
		 u64		offset,		\
		 u32		len),		\
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

TRACE_EVENT(nfsd_dirent,
	TP_PROTO(struct svc_fh *fhp,
		 u64 ino,
		 const char *name,
		 int namlen),
	TP_ARGS(fhp, ino, name, namlen),
	TP_STRUCT__entry(
		__field(u32, fh_hash)
		__field(u64, ino)
		__string_len(name, name, namlen)
	),
	TP_fast_assign(
		__entry->fh_hash = fhp ? knfsd_fh_hash(&fhp->fh_handle) : 0;
		__entry->ino = ino;
		__assign_str_len(name, name, namlen)
	),
	TP_printk("fh_hash=0x%08x ino=%llu name=%s",
		__entry->fh_hash, __entry->ino, __get_str(name)
	)
)

DECLARE_EVENT_CLASS(nfsd_copy_err_class,
	TP_PROTO(struct svc_rqst *rqstp,
		 struct svc_fh	*src_fhp,
		 loff_t		src_offset,
		 struct svc_fh	*dst_fhp,
		 loff_t		dst_offset,
		 u64		count,
		 int		status),
	TP_ARGS(rqstp, src_fhp, src_offset, dst_fhp, dst_offset, count, status),
	TP_STRUCT__entry(
		__field(u32, xid)
		__field(u32, src_fh_hash)
		__field(loff_t, src_offset)
		__field(u32, dst_fh_hash)
		__field(loff_t, dst_offset)
		__field(u64, count)
		__field(int, status)
	),
	TP_fast_assign(
		__entry->xid = be32_to_cpu(rqstp->rq_xid);
		__entry->src_fh_hash = knfsd_fh_hash(&src_fhp->fh_handle);
		__entry->src_offset = src_offset;
		__entry->dst_fh_hash = knfsd_fh_hash(&dst_fhp->fh_handle);
		__entry->dst_offset = dst_offset;
		__entry->count = count;
		__entry->status = status;
	),
	TP_printk("xid=0x%08x src_fh_hash=0x%08x src_offset=%lld "
			"dst_fh_hash=0x%08x dst_offset=%lld "
			"count=%llu status=%d",
		  __entry->xid, __entry->src_fh_hash, __entry->src_offset,
		  __entry->dst_fh_hash, __entry->dst_offset,
		  (unsigned long long)__entry->count,
		  __entry->status)
)

#define DEFINE_NFSD_COPY_ERR_EVENT(name)		\
DEFINE_EVENT(nfsd_copy_err_class, nfsd_##name,		\
	TP_PROTO(struct svc_rqst	*rqstp,		\
		 struct svc_fh		*src_fhp,	\
		 loff_t			src_offset,	\
		 struct svc_fh		*dst_fhp,	\
		 loff_t			dst_offset,	\
		 u64			count,		\
		 int			status),	\
	TP_ARGS(rqstp, src_fhp, src_offset, dst_fhp, dst_offset, \
		count, status))

DEFINE_NFSD_COPY_ERR_EVENT(clone_file_range_err);

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

DEFINE_STATEID_EVENT(open);
DEFINE_STATEID_EVENT(deleg_read);
DEFINE_STATEID_EVENT(deleg_recall);

DECLARE_EVENT_CLASS(nfsd_stateseqid_class,
	TP_PROTO(u32 seqid, const stateid_t *stp),
	TP_ARGS(seqid, stp),
	TP_STRUCT__entry(
		__field(u32, seqid)
		__field(u32, cl_boot)
		__field(u32, cl_id)
		__field(u32, si_id)
		__field(u32, si_generation)
	),
	TP_fast_assign(
		__entry->seqid = seqid;
		__entry->cl_boot = stp->si_opaque.so_clid.cl_boot;
		__entry->cl_id = stp->si_opaque.so_clid.cl_id;
		__entry->si_id = stp->si_opaque.so_id;
		__entry->si_generation = stp->si_generation;
	),
	TP_printk("seqid=%u client %08x:%08x stateid %08x:%08x",
		__entry->seqid, __entry->cl_boot, __entry->cl_id,
		__entry->si_id, __entry->si_generation)
)

#define DEFINE_STATESEQID_EVENT(name) \
DEFINE_EVENT(nfsd_stateseqid_class, nfsd_##name, \
	TP_PROTO(u32 seqid, const stateid_t *stp), \
	TP_ARGS(seqid, stp))

DEFINE_STATESEQID_EVENT(preprocess);
DEFINE_STATESEQID_EVENT(open_confirm);

DECLARE_EVENT_CLASS(nfsd_clientid_class,
	TP_PROTO(const clientid_t *clid),
	TP_ARGS(clid),
	TP_STRUCT__entry(
		__field(u32, cl_boot)
		__field(u32, cl_id)
	),
	TP_fast_assign(
		__entry->cl_boot = clid->cl_boot;
		__entry->cl_id = clid->cl_id;
	),
	TP_printk("client %08x:%08x", __entry->cl_boot, __entry->cl_id)
)

#define DEFINE_CLIENTID_EVENT(name) \
DEFINE_EVENT(nfsd_clientid_class, nfsd_clid_##name, \
	TP_PROTO(const clientid_t *clid), \
	TP_ARGS(clid))

DEFINE_CLIENTID_EVENT(expire_unconf);
DEFINE_CLIENTID_EVENT(reclaim_complete);
DEFINE_CLIENTID_EVENT(confirmed);
DEFINE_CLIENTID_EVENT(destroyed);
DEFINE_CLIENTID_EVENT(admin_expired);
DEFINE_CLIENTID_EVENT(replaced);
DEFINE_CLIENTID_EVENT(purged);
DEFINE_CLIENTID_EVENT(renew);
DEFINE_CLIENTID_EVENT(stale);

DECLARE_EVENT_CLASS(nfsd_net_class,
	TP_PROTO(const struct nfsd_net *nn),
	TP_ARGS(nn),
	TP_STRUCT__entry(
		__field(unsigned long long, boot_time)
	),
	TP_fast_assign(
		__entry->boot_time = nn->boot_time;
	),
	TP_printk("boot_time=%16llx", __entry->boot_time)
)

#define DEFINE_NET_EVENT(name) \
DEFINE_EVENT(nfsd_net_class, nfsd_##name, \
	TP_PROTO(const struct nfsd_net *nn), \
	TP_ARGS(nn))

DEFINE_NET_EVENT(grace_start);
DEFINE_NET_EVENT(grace_complete);

TRACE_EVENT(nfsd_writeverf_reset,
	TP_PROTO(
		const struct nfsd_net *nn,
		const struct svc_rqst *rqstp,
		int error
	),
	TP_ARGS(nn, rqstp, error),
	TP_STRUCT__entry(
		__field(unsigned long long, boot_time)
		__field(u32, xid)
		__field(int, error)
		__array(unsigned char, verifier, NFS4_VERIFIER_SIZE)
	),
	TP_fast_assign(
		__entry->boot_time = nn->boot_time;
		__entry->xid = be32_to_cpu(rqstp->rq_xid);
		__entry->error = error;

		/* avoid seqlock inside TP_fast_assign */
		memcpy(__entry->verifier, nn->writeverf,
		       NFS4_VERIFIER_SIZE);
	),
	TP_printk("boot_time=%16llx xid=0x%08x error=%d new verifier=0x%s",
		__entry->boot_time, __entry->xid, __entry->error,
		__print_hex_str(__entry->verifier, NFS4_VERIFIER_SIZE)
	)
);

TRACE_EVENT(nfsd_clid_cred_mismatch,
	TP_PROTO(
		const struct nfs4_client *clp,
		const struct svc_rqst *rqstp
	),
	TP_ARGS(clp, rqstp),
	TP_STRUCT__entry(
		__field(u32, cl_boot)
		__field(u32, cl_id)
		__field(unsigned long, cl_flavor)
		__field(unsigned long, new_flavor)
		__sockaddr(addr, rqstp->rq_xprt->xpt_remotelen)
	),
	TP_fast_assign(
		__entry->cl_boot = clp->cl_clientid.cl_boot;
		__entry->cl_id = clp->cl_clientid.cl_id;
		__entry->cl_flavor = clp->cl_cred.cr_flavor;
		__entry->new_flavor = rqstp->rq_cred.cr_flavor;
		__assign_sockaddr(addr, &rqstp->rq_xprt->xpt_remote,
				  rqstp->rq_xprt->xpt_remotelen);
	),
	TP_printk("client %08x:%08x flavor=%s, conflict=%s from addr=%pISpc",
		__entry->cl_boot, __entry->cl_id,
		show_nfsd_authflavor(__entry->cl_flavor),
		show_nfsd_authflavor(__entry->new_flavor),
		__get_sockaddr(addr)
	)
)

TRACE_EVENT(nfsd_clid_verf_mismatch,
	TP_PROTO(
		const struct nfs4_client *clp,
		const struct svc_rqst *rqstp,
		const nfs4_verifier *verf
	),
	TP_ARGS(clp, rqstp, verf),
	TP_STRUCT__entry(
		__field(u32, cl_boot)
		__field(u32, cl_id)
		__array(unsigned char, cl_verifier, NFS4_VERIFIER_SIZE)
		__array(unsigned char, new_verifier, NFS4_VERIFIER_SIZE)
		__sockaddr(addr, rqstp->rq_xprt->xpt_remotelen)
	),
	TP_fast_assign(
		__entry->cl_boot = clp->cl_clientid.cl_boot;
		__entry->cl_id = clp->cl_clientid.cl_id;
		memcpy(__entry->cl_verifier, (void *)&clp->cl_verifier,
		       NFS4_VERIFIER_SIZE);
		memcpy(__entry->new_verifier, (void *)verf,
		       NFS4_VERIFIER_SIZE);
		__assign_sockaddr(addr, &rqstp->rq_xprt->xpt_remote,
				  rqstp->rq_xprt->xpt_remotelen);
	),
	TP_printk("client %08x:%08x verf=0x%s, updated=0x%s from addr=%pISpc",
		__entry->cl_boot, __entry->cl_id,
		__print_hex_str(__entry->cl_verifier, NFS4_VERIFIER_SIZE),
		__print_hex_str(__entry->new_verifier, NFS4_VERIFIER_SIZE),
		__get_sockaddr(addr)
	)
);

DECLARE_EVENT_CLASS(nfsd_clid_class,
	TP_PROTO(const struct nfs4_client *clp),
	TP_ARGS(clp),
	TP_STRUCT__entry(
		__field(u32, cl_boot)
		__field(u32, cl_id)
		__array(unsigned char, addr, sizeof(struct sockaddr_in6))
		__field(unsigned long, flavor)
		__array(unsigned char, verifier, NFS4_VERIFIER_SIZE)
		__string_len(name, name, clp->cl_name.len)
	),
	TP_fast_assign(
		__entry->cl_boot = clp->cl_clientid.cl_boot;
		__entry->cl_id = clp->cl_clientid.cl_id;
		memcpy(__entry->addr, &clp->cl_addr,
			sizeof(struct sockaddr_in6));
		__entry->flavor = clp->cl_cred.cr_flavor;
		memcpy(__entry->verifier, (void *)&clp->cl_verifier,
		       NFS4_VERIFIER_SIZE);
		__assign_str_len(name, clp->cl_name.data, clp->cl_name.len);
	),
	TP_printk("addr=%pISpc name='%s' verifier=0x%s flavor=%s client=%08x:%08x",
		__entry->addr, __get_str(name),
		__print_hex_str(__entry->verifier, NFS4_VERIFIER_SIZE),
		show_nfsd_authflavor(__entry->flavor),
		__entry->cl_boot, __entry->cl_id)
);

#define DEFINE_CLID_EVENT(name) \
DEFINE_EVENT(nfsd_clid_class, nfsd_clid_##name, \
	TP_PROTO(const struct nfs4_client *clp), \
	TP_ARGS(clp))

DEFINE_CLID_EVENT(fresh);
DEFINE_CLID_EVENT(confirmed_r);

/*
 * from fs/nfsd/filecache.h
 */
#define show_nf_flags(val)						\
	__print_flags(val, "|",						\
		{ 1 << NFSD_FILE_HASHED,	"HASHED" },		\
		{ 1 << NFSD_FILE_PENDING,	"PENDING" },		\
		{ 1 << NFSD_FILE_REFERENCED,	"REFERENCED"})

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
	TP_printk("hash=0x%x inode=%p ref=%d flags=%s may=%s file=%p",
		__entry->nf_hashval,
		__entry->nf_inode,
		__entry->nf_ref,
		show_nf_flags(__entry->nf_flags),
		show_nfsd_may_flags(__entry->nf_may),
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
		__field(unsigned long, may_flags)
		__field(int, nf_ref)
		__field(unsigned long, nf_flags)
		__field(unsigned long, nf_may)
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

	TP_printk("xid=0x%x hash=0x%x inode=%p may_flags=%s ref=%d nf_flags=%s nf_may=%s nf_file=%p status=%u",
			__entry->xid, __entry->hash, __entry->inode,
			show_nfsd_may_flags(__entry->may_flags),
			__entry->nf_ref, show_nf_flags(__entry->nf_flags),
			show_nfsd_may_flags(__entry->nf_may),
			__entry->nf_file, __entry->status)
);

TRACE_EVENT(nfsd_file_open,
	TP_PROTO(struct nfsd_file *nf, __be32 status),
	TP_ARGS(nf, status),
	TP_STRUCT__entry(
		__field(unsigned int, nf_hashval)
		__field(void *, nf_inode)	/* cannot be dereferenced */
		__field(int, nf_ref)
		__field(unsigned long, nf_flags)
		__field(unsigned long, nf_may)
		__field(void *, nf_file)	/* cannot be dereferenced */
	),
	TP_fast_assign(
		__entry->nf_hashval = nf->nf_hashval;
		__entry->nf_inode = nf->nf_inode;
		__entry->nf_ref = refcount_read(&nf->nf_ref);
		__entry->nf_flags = nf->nf_flags;
		__entry->nf_may = nf->nf_may;
		__entry->nf_file = nf->nf_file;
	),
	TP_printk("hash=0x%x inode=%p ref=%d flags=%s may=%s file=%p",
		__entry->nf_hashval,
		__entry->nf_inode,
		__entry->nf_ref,
		show_nf_flags(__entry->nf_flags),
		show_nfsd_may_flags(__entry->nf_may),
		__entry->nf_file)
)

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
	TP_printk("hash=0x%x inode=%p found=%d", __entry->hash,
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
	TP_printk("inode=%p nlink=%u mode=0%ho mask=0x%x", __entry->inode,
			__entry->nlink, __entry->mode, __entry->mask)
);

#include "cache.h"

TRACE_DEFINE_ENUM(RC_DROPIT);
TRACE_DEFINE_ENUM(RC_REPLY);
TRACE_DEFINE_ENUM(RC_DOIT);

#define show_drc_retval(x)						\
	__print_symbolic(x,						\
		{ RC_DROPIT, "DROPIT" },				\
		{ RC_REPLY, "REPLY" },					\
		{ RC_DOIT, "DOIT" })

TRACE_EVENT(nfsd_drc_found,
	TP_PROTO(
		const struct nfsd_net *nn,
		const struct svc_rqst *rqstp,
		int result
	),
	TP_ARGS(nn, rqstp, result),
	TP_STRUCT__entry(
		__field(unsigned long long, boot_time)
		__field(unsigned long, result)
		__field(u32, xid)
	),
	TP_fast_assign(
		__entry->boot_time = nn->boot_time;
		__entry->result = result;
		__entry->xid = be32_to_cpu(rqstp->rq_xid);
	),
	TP_printk("boot_time=%16llx xid=0x%08x result=%s",
		__entry->boot_time, __entry->xid,
		show_drc_retval(__entry->result))

);

TRACE_EVENT(nfsd_drc_mismatch,
	TP_PROTO(
		const struct nfsd_net *nn,
		const struct svc_cacherep *key,
		const struct svc_cacherep *rp
	),
	TP_ARGS(nn, key, rp),
	TP_STRUCT__entry(
		__field(unsigned long long, boot_time)
		__field(u32, xid)
		__field(u32, cached)
		__field(u32, ingress)
	),
	TP_fast_assign(
		__entry->boot_time = nn->boot_time;
		__entry->xid = be32_to_cpu(key->c_key.k_xid);
		__entry->cached = (__force u32)key->c_key.k_csum;
		__entry->ingress = (__force u32)rp->c_key.k_csum;
	),
	TP_printk("boot_time=%16llx xid=0x%08x cached-csum=0x%08x ingress-csum=0x%08x",
		__entry->boot_time, __entry->xid, __entry->cached,
		__entry->ingress)
);

TRACE_EVENT(nfsd_cb_args,
	TP_PROTO(
		const struct nfs4_client *clp,
		const struct nfs4_cb_conn *conn
	),
	TP_ARGS(clp, conn),
	TP_STRUCT__entry(
		__field(u32, cl_boot)
		__field(u32, cl_id)
		__field(u32, prog)
		__field(u32, ident)
		__sockaddr(addr, conn->cb_addrlen)
	),
	TP_fast_assign(
		__entry->cl_boot = clp->cl_clientid.cl_boot;
		__entry->cl_id = clp->cl_clientid.cl_id;
		__entry->prog = conn->cb_prog;
		__entry->ident = conn->cb_ident;
		__assign_sockaddr(addr, &conn->cb_addr, conn->cb_addrlen);
	),
	TP_printk("addr=%pISpc client %08x:%08x prog=%u ident=%u",
		__get_sockaddr(addr), __entry->cl_boot, __entry->cl_id,
		__entry->prog, __entry->ident)
);

TRACE_EVENT(nfsd_cb_nodelegs,
	TP_PROTO(const struct nfs4_client *clp),
	TP_ARGS(clp),
	TP_STRUCT__entry(
		__field(u32, cl_boot)
		__field(u32, cl_id)
	),
	TP_fast_assign(
		__entry->cl_boot = clp->cl_clientid.cl_boot;
		__entry->cl_id = clp->cl_clientid.cl_id;
	),
	TP_printk("client %08x:%08x", __entry->cl_boot, __entry->cl_id)
)

#define show_cb_state(val)						\
	__print_symbolic(val,						\
		{ NFSD4_CB_UP,		"UP" },				\
		{ NFSD4_CB_UNKNOWN,	"UNKNOWN" },			\
		{ NFSD4_CB_DOWN,	"DOWN" },			\
		{ NFSD4_CB_FAULT,	"FAULT"})

DECLARE_EVENT_CLASS(nfsd_cb_class,
	TP_PROTO(const struct nfs4_client *clp),
	TP_ARGS(clp),
	TP_STRUCT__entry(
		__field(unsigned long, state)
		__field(u32, cl_boot)
		__field(u32, cl_id)
		__sockaddr(addr, clp->cl_cb_conn.cb_addrlen)
	),
	TP_fast_assign(
		__entry->state = clp->cl_cb_state;
		__entry->cl_boot = clp->cl_clientid.cl_boot;
		__entry->cl_id = clp->cl_clientid.cl_id;
		__assign_sockaddr(addr, &clp->cl_cb_conn.cb_addr,
				  clp->cl_cb_conn.cb_addrlen)
	),
	TP_printk("addr=%pISpc client %08x:%08x state=%s",
		__get_sockaddr(addr), __entry->cl_boot, __entry->cl_id,
		show_cb_state(__entry->state))
);

#define DEFINE_NFSD_CB_EVENT(name)			\
DEFINE_EVENT(nfsd_cb_class, nfsd_cb_##name,		\
	TP_PROTO(const struct nfs4_client *clp),	\
	TP_ARGS(clp))

DEFINE_NFSD_CB_EVENT(state);
DEFINE_NFSD_CB_EVENT(probe);
DEFINE_NFSD_CB_EVENT(lost);
DEFINE_NFSD_CB_EVENT(shutdown);

TRACE_DEFINE_ENUM(RPC_AUTH_NULL);
TRACE_DEFINE_ENUM(RPC_AUTH_UNIX);
TRACE_DEFINE_ENUM(RPC_AUTH_GSS);
TRACE_DEFINE_ENUM(RPC_AUTH_GSS_KRB5);
TRACE_DEFINE_ENUM(RPC_AUTH_GSS_KRB5I);
TRACE_DEFINE_ENUM(RPC_AUTH_GSS_KRB5P);

#define show_nfsd_authflavor(val)					\
	__print_symbolic(val,						\
		{ RPC_AUTH_NULL,		"none" },		\
		{ RPC_AUTH_UNIX,		"sys" },		\
		{ RPC_AUTH_GSS,			"gss" },		\
		{ RPC_AUTH_GSS_KRB5,		"krb5" },		\
		{ RPC_AUTH_GSS_KRB5I,		"krb5i" },		\
		{ RPC_AUTH_GSS_KRB5P,		"krb5p" })

TRACE_EVENT(nfsd_cb_setup,
	TP_PROTO(const struct nfs4_client *clp,
		 const char *netid,
		 rpc_authflavor_t authflavor
	),
	TP_ARGS(clp, netid, authflavor),
	TP_STRUCT__entry(
		__field(u32, cl_boot)
		__field(u32, cl_id)
		__field(unsigned long, authflavor)
		__sockaddr(addr, clp->cl_cb_conn.cb_addrlen)
		__array(unsigned char, netid, 8)
	),
	TP_fast_assign(
		__entry->cl_boot = clp->cl_clientid.cl_boot;
		__entry->cl_id = clp->cl_clientid.cl_id;
		strlcpy(__entry->netid, netid, sizeof(__entry->netid));
		__entry->authflavor = authflavor;
		__assign_sockaddr(addr, &clp->cl_cb_conn.cb_addr,
				  clp->cl_cb_conn.cb_addrlen)
	),
	TP_printk("addr=%pISpc client %08x:%08x proto=%s flavor=%s",
		__get_sockaddr(addr), __entry->cl_boot, __entry->cl_id,
		__entry->netid, show_nfsd_authflavor(__entry->authflavor))
);

TRACE_EVENT(nfsd_cb_setup_err,
	TP_PROTO(
		const struct nfs4_client *clp,
		long error
	),
	TP_ARGS(clp, error),
	TP_STRUCT__entry(
		__field(long, error)
		__field(u32, cl_boot)
		__field(u32, cl_id)
		__sockaddr(addr, clp->cl_cb_conn.cb_addrlen)
	),
	TP_fast_assign(
		__entry->error = error;
		__entry->cl_boot = clp->cl_clientid.cl_boot;
		__entry->cl_id = clp->cl_clientid.cl_id;
		__assign_sockaddr(addr, &clp->cl_cb_conn.cb_addr,
				  clp->cl_cb_conn.cb_addrlen)
	),
	TP_printk("addr=%pISpc client %08x:%08x error=%ld",
		__get_sockaddr(addr), __entry->cl_boot, __entry->cl_id,
		__entry->error)
);

TRACE_EVENT_CONDITION(nfsd_cb_recall,
	TP_PROTO(
		const struct nfs4_stid *stid
	),
	TP_ARGS(stid),
	TP_CONDITION(stid->sc_client),
	TP_STRUCT__entry(
		__field(u32, cl_boot)
		__field(u32, cl_id)
		__field(u32, si_id)
		__field(u32, si_generation)
		__sockaddr(addr, stid->sc_client->cl_cb_conn.cb_addrlen)
	),
	TP_fast_assign(
		const stateid_t *stp = &stid->sc_stateid;
		const struct nfs4_client *clp = stid->sc_client;

		__entry->cl_boot = stp->si_opaque.so_clid.cl_boot;
		__entry->cl_id = stp->si_opaque.so_clid.cl_id;
		__entry->si_id = stp->si_opaque.so_id;
		__entry->si_generation = stp->si_generation;
		__assign_sockaddr(addr, &clp->cl_cb_conn.cb_addr,
				  clp->cl_cb_conn.cb_addrlen)
	),
	TP_printk("addr=%pISpc client %08x:%08x stateid %08x:%08x",
		__get_sockaddr(addr), __entry->cl_boot, __entry->cl_id,
		__entry->si_id, __entry->si_generation)
);

TRACE_EVENT(nfsd_cb_notify_lock,
	TP_PROTO(
		const struct nfs4_lockowner *lo,
		const struct nfsd4_blocked_lock *nbl
	),
	TP_ARGS(lo, nbl),
	TP_STRUCT__entry(
		__field(u32, cl_boot)
		__field(u32, cl_id)
		__field(u32, fh_hash)
		__sockaddr(addr, lo->lo_owner.so_client->cl_cb_conn.cb_addrlen)
	),
	TP_fast_assign(
		const struct nfs4_client *clp = lo->lo_owner.so_client;

		__entry->cl_boot = clp->cl_clientid.cl_boot;
		__entry->cl_id = clp->cl_clientid.cl_id;
		__entry->fh_hash = knfsd_fh_hash(&nbl->nbl_fh);
		__assign_sockaddr(addr, &clp->cl_cb_conn.cb_addr,
				  clp->cl_cb_conn.cb_addrlen)
	),
	TP_printk("addr=%pISpc client %08x:%08x fh_hash=0x%08x",
		__get_sockaddr(addr), __entry->cl_boot, __entry->cl_id,
		__entry->fh_hash)
);

TRACE_EVENT(nfsd_cb_offload,
	TP_PROTO(
		const struct nfs4_client *clp,
		const stateid_t *stp,
		const struct knfsd_fh *fh,
		u64 count,
		__be32 status
	),
	TP_ARGS(clp, stp, fh, count, status),
	TP_STRUCT__entry(
		__field(u32, cl_boot)
		__field(u32, cl_id)
		__field(u32, si_id)
		__field(u32, si_generation)
		__field(u32, fh_hash)
		__field(int, status)
		__field(u64, count)
		__sockaddr(addr, clp->cl_cb_conn.cb_addrlen)
	),
	TP_fast_assign(
		__entry->cl_boot = stp->si_opaque.so_clid.cl_boot;
		__entry->cl_id = stp->si_opaque.so_clid.cl_id;
		__entry->si_id = stp->si_opaque.so_id;
		__entry->si_generation = stp->si_generation;
		__entry->fh_hash = knfsd_fh_hash(fh);
		__entry->status = be32_to_cpu(status);
		__entry->count = count;
		__assign_sockaddr(addr, &clp->cl_cb_conn.cb_addr,
				  clp->cl_cb_conn.cb_addrlen)
	),
	TP_printk("addr=%pISpc client %08x:%08x stateid %08x:%08x fh_hash=0x%08x count=%llu status=%d",
		__get_sockaddr(addr), __entry->cl_boot, __entry->cl_id,
		__entry->si_id, __entry->si_generation,
		__entry->fh_hash, __entry->count, __entry->status)
);

#endif /* _NFSD_TRACE_H */

#undef TRACE_INCLUDE_PATH
#define TRACE_INCLUDE_PATH .
#define TRACE_INCLUDE_FILE trace
#include <trace/define_trace.h>
