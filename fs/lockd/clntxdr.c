/*
 * linux/fs/lockd/clntxdr.c
 *
 * XDR functions to encode/decode NLM version 3 RPC arguments and results.
 * NLM version 3 is backwards compatible with NLM versions 1 and 2.
 *
 * NLM client-side only.
 *
 * Copyright (C) 2010, Oracle.  All rights reserved.
 */

#include <linux/types.h>
#include <linux/sunrpc/xdr.h>
#include <linux/sunrpc/clnt.h>
#include <linux/sunrpc/stats.h>
#include <linux/lockd/lockd.h>

#define NLMDBG_FACILITY		NLMDBG_XDR

#if (NLMCLNT_OHSIZE > XDR_MAX_NETOBJ)
#  error "NLM host name cannot be larger than XDR_MAX_NETOBJ!"
#endif

/*
 * Declare the space requirements for NLM arguments and replies as
 * number of 32bit-words
 */
#define NLM_cookie_sz		(1+(NLM_MAXCOOKIELEN>>2))
#define NLM_caller_sz		(1+(NLMCLNT_OHSIZE>>2))
#define NLM_owner_sz		(1+(NLMCLNT_OHSIZE>>2))
#define NLM_fhandle_sz		(1+(NFS2_FHSIZE>>2))
#define NLM_lock_sz		(3+NLM_caller_sz+NLM_owner_sz+NLM_fhandle_sz)
#define NLM_holder_sz		(4+NLM_owner_sz)

#define NLM_testargs_sz		(NLM_cookie_sz+1+NLM_lock_sz)
#define NLM_lockargs_sz		(NLM_cookie_sz+4+NLM_lock_sz)
#define NLM_cancargs_sz		(NLM_cookie_sz+2+NLM_lock_sz)
#define NLM_unlockargs_sz	(NLM_cookie_sz+NLM_lock_sz)

#define NLM_testres_sz		(NLM_cookie_sz+1+NLM_holder_sz)
#define NLM_res_sz		(NLM_cookie_sz+1)
#define NLM_norep_sz		(0)


static s32 loff_t_to_s32(loff_t offset)
{
	s32 res;

	if (offset >= NLM_OFFSET_MAX)
		res = NLM_OFFSET_MAX;
	else if (offset <= -NLM_OFFSET_MAX)
		res = -NLM_OFFSET_MAX;
	else
		res = offset;
	return res;
}

static void nlm_compute_offsets(const struct nlm_lock *lock,
				u32 *l_offset, u32 *l_len)
{
	const struct file_lock *fl = &lock->fl;

	*l_offset = loff_t_to_s32(fl->fl_start);
	if (fl->fl_end == OFFSET_MAX)
		*l_len = 0;
	else
		*l_len = loff_t_to_s32(fl->fl_end - fl->fl_start + 1);
}

/*
 * Handle decode buffer overflows out-of-line.
 */
static void print_overflow_msg(const char *func, const struct xdr_stream *xdr)
{
	dprintk("lockd: %s prematurely hit the end of our receive buffer. "
		"Remaining buffer length is %tu words.\n",
		func, xdr->end - xdr->p);
}


/*
 * Encode/decode NLMv3 basic data types
 *
 * Basic NLMv3 data types are not defined in an IETF standards
 * document.  X/Open has a description of these data types that
 * is useful.  See Chapter 10 of "Protocols for Interworking:
 * XNFS, Version 3W".
 *
 * Not all basic data types have their own encoding and decoding
 * functions.  For run-time efficiency, some data types are encoded
 * or decoded inline.
 */

static void encode_bool(struct xdr_stream *xdr, const int value)
{
	__be32 *p;

	p = xdr_reserve_space(xdr, 4);
	*p = value ? xdr_one : xdr_zero;
}

static void encode_int32(struct xdr_stream *xdr, const s32 value)
{
	__be32 *p;

	p = xdr_reserve_space(xdr, 4);
	*p = cpu_to_be32(value);
}

/*
 *	typedef opaque netobj<MAXNETOBJ_SZ>
 */
static void encode_netobj(struct xdr_stream *xdr,
			  const u8 *data, const unsigned int length)
{
	__be32 *p;

	p = xdr_reserve_space(xdr, 4 + length);
	xdr_encode_opaque(p, data, length);
}

static int decode_netobj(struct xdr_stream *xdr,
			 struct xdr_netobj *obj)
{
	u32 length;
	__be32 *p;

	p = xdr_inline_decode(xdr, 4);
	if (unlikely(p == NULL))
		goto out_overflow;
	length = be32_to_cpup(p++);
	if (unlikely(length > XDR_MAX_NETOBJ))
		goto out_size;
	obj->len = length;
	obj->data = (u8 *)p;
	return 0;
out_size:
	dprintk("NFS: returned netobj was too long: %u\n", length);
	return -EIO;
out_overflow:
	print_overflow_msg(__func__, xdr);
	return -EIO;
}

/*
 *	netobj cookie;
 */
static void encode_cookie(struct xdr_stream *xdr,
			  const struct nlm_cookie *cookie)
{
	encode_netobj(xdr, (u8 *)&cookie->data, cookie->len);
}

static int decode_cookie(struct xdr_stream *xdr,
			 struct nlm_cookie *cookie)
{
	u32 length;
	__be32 *p;

	p = xdr_inline_decode(xdr, 4);
	if (unlikely(p == NULL))
		goto out_overflow;
	length = be32_to_cpup(p++);
	/* apparently HPUX can return empty cookies */
	if (length == 0)
		goto out_hpux;
	if (length > NLM_MAXCOOKIELEN)
		goto out_size;
	p = xdr_inline_decode(xdr, length);
	if (unlikely(p == NULL))
		goto out_overflow;
	cookie->len = length;
	memcpy(cookie->data, p, length);
	return 0;
out_hpux:
	cookie->len = 4;
	memset(cookie->data, 0, 4);
	return 0;
out_size:
	dprintk("NFS: returned cookie was too long: %u\n", length);
	return -EIO;
out_overflow:
	print_overflow_msg(__func__, xdr);
	return -EIO;
}

/*
 *	netobj fh;
 */
static void encode_fh(struct xdr_stream *xdr, const struct nfs_fh *fh)
{
	encode_netobj(xdr, (u8 *)&fh->data, NFS2_FHSIZE);
}

/*
 *	enum nlm_stats {
 *		LCK_GRANTED = 0,
 *		LCK_DENIED = 1,
 *		LCK_DENIED_NOLOCKS = 2,
 *		LCK_BLOCKED = 3,
 *		LCK_DENIED_GRACE_PERIOD = 4
 *	};
 *
 *
 *	struct nlm_stat {
 *		nlm_stats stat;
 *	};
 *
 * NB: we don't swap bytes for the NLM status values.  The upper
 * layers deal directly with the status value in network byte
 * order.
 */

static void encode_nlm_stat(struct xdr_stream *xdr,
			    const __be32 stat)
{
	__be32 *p;

	WARN_ON_ONCE(be32_to_cpu(stat) > NLM_LCK_DENIED_GRACE_PERIOD);
	p = xdr_reserve_space(xdr, 4);
	*p = stat;
}

static int decode_nlm_stat(struct xdr_stream *xdr,
			   __be32 *stat)
{
	__be32 *p;

	p = xdr_inline_decode(xdr, 4);
	if (unlikely(p == NULL))
		goto out_overflow;
	if (unlikely(ntohl(*p) > ntohl(nlm_lck_denied_grace_period)))
		goto out_enum;
	*stat = *p;
	return 0;
out_enum:
	dprintk("%s: server returned invalid nlm_stats value: %u\n",
		__func__, be32_to_cpup(p));
	return -EIO;
out_overflow:
	print_overflow_msg(__func__, xdr);
	return -EIO;
}

/*
 *	struct nlm_holder {
 *		bool exclusive;
 *		int uppid;
 *		netobj oh;
 *		unsigned l_offset;
 *		unsigned l_len;
 *	};
 */
static void encode_nlm_holder(struct xdr_stream *xdr,
			      const struct nlm_res *result)
{
	const struct nlm_lock *lock = &result->lock;
	u32 l_offset, l_len;
	__be32 *p;

	encode_bool(xdr, lock->fl.fl_type == F_RDLCK);
	encode_int32(xdr, lock->svid);
	encode_netobj(xdr, lock->oh.data, lock->oh.len);

	p = xdr_reserve_space(xdr, 4 + 4);
	nlm_compute_offsets(lock, &l_offset, &l_len);
	*p++ = cpu_to_be32(l_offset);
	*p   = cpu_to_be32(l_len);
}

static int decode_nlm_holder(struct xdr_stream *xdr, struct nlm_res *result)
{
	struct nlm_lock *lock = &result->lock;
	struct file_lock *fl = &lock->fl;
	u32 exclusive, l_offset, l_len;
	int error;
	__be32 *p;
	s32 end;

	memset(lock, 0, sizeof(*lock));
	locks_init_lock(fl);

	p = xdr_inline_decode(xdr, 4 + 4);
	if (unlikely(p == NULL))
		goto out_overflow;
	exclusive = be32_to_cpup(p++);
	lock->svid = be32_to_cpup(p);
	fl->fl_pid = (pid_t)lock->svid;

	error = decode_netobj(xdr, &lock->oh);
	if (unlikely(error))
		goto out;

	p = xdr_inline_decode(xdr, 4 + 4);
	if (unlikely(p == NULL))
		goto out_overflow;

	fl->fl_flags = FL_POSIX;
	fl->fl_type  = exclusive != 0 ? F_WRLCK : F_RDLCK;
	l_offset = be32_to_cpup(p++);
	l_len = be32_to_cpup(p);
	end = l_offset + l_len - 1;

	fl->fl_start = (loff_t)l_offset;
	if (l_len == 0 || end < 0)
		fl->fl_end = OFFSET_MAX;
	else
		fl->fl_end = (loff_t)end;
	error = 0;
out:
	return error;
out_overflow:
	print_overflow_msg(__func__, xdr);
	return -EIO;
}

/*
 *	string caller_name<LM_MAXSTRLEN>;
 */
static void encode_caller_name(struct xdr_stream *xdr, const char *name)
{
	/* NB: client-side does not set lock->len */
	u32 length = strlen(name);
	__be32 *p;

	p = xdr_reserve_space(xdr, 4 + length);
	xdr_encode_opaque(p, name, length);
}

/*
 *	struct nlm_lock {
 *		string caller_name<LM_MAXSTRLEN>;
 *		netobj fh;
 *		netobj oh;
 *		int uppid;
 *		unsigned l_offset;
 *		unsigned l_len;
 *	};
 */
static void encode_nlm_lock(struct xdr_stream *xdr,
			    const struct nlm_lock *lock)
{
	u32 l_offset, l_len;
	__be32 *p;

	encode_caller_name(xdr, lock->caller);
	encode_fh(xdr, &lock->fh);
	encode_netobj(xdr, lock->oh.data, lock->oh.len);

	p = xdr_reserve_space(xdr, 4 + 4 + 4);
	*p++ = cpu_to_be32(lock->svid);

	nlm_compute_offsets(lock, &l_offset, &l_len);
	*p++ = cpu_to_be32(l_offset);
	*p   = cpu_to_be32(l_len);
}


/*
 * NLMv3 XDR encode functions
 *
 * NLMv3 argument types are defined in Chapter 10 of The Open Group's
 * "Protocols for Interworking: XNFS, Version 3W".
 */

/*
 *	struct nlm_testargs {
 *		netobj cookie;
 *		bool exclusive;
 *		struct nlm_lock alock;
 *	};
 */
static void nlm_xdr_enc_testargs(struct rpc_rqst *req,
				 struct xdr_stream *xdr,
				 const struct nlm_args *args)
{
	const struct nlm_lock *lock = &args->lock;

	encode_cookie(xdr, &args->cookie);
	encode_bool(xdr, lock->fl.fl_type == F_WRLCK);
	encode_nlm_lock(xdr, lock);
}

/*
 *	struct nlm_lockargs {
 *		netobj cookie;
 *		bool block;
 *		bool exclusive;
 *		struct nlm_lock alock;
 *		bool reclaim;
 *		int state;
 *	};
 */
static void nlm_xdr_enc_lockargs(struct rpc_rqst *req,
				 struct xdr_stream *xdr,
				 const struct nlm_args *args)
{
	const struct nlm_lock *lock = &args->lock;

	encode_cookie(xdr, &args->cookie);
	encode_bool(xdr, args->block);
	encode_bool(xdr, lock->fl.fl_type == F_WRLCK);
	encode_nlm_lock(xdr, lock);
	encode_bool(xdr, args->reclaim);
	encode_int32(xdr, args->state);
}

/*
 *	struct nlm_cancargs {
 *		netobj cookie;
 *		bool block;
 *		bool exclusive;
 *		struct nlm_lock alock;
 *	};
 */
static void nlm_xdr_enc_cancargs(struct rpc_rqst *req,
				 struct xdr_stream *xdr,
				 const struct nlm_args *args)
{
	const struct nlm_lock *lock = &args->lock;

	encode_cookie(xdr, &args->cookie);
	encode_bool(xdr, args->block);
	encode_bool(xdr, lock->fl.fl_type == F_WRLCK);
	encode_nlm_lock(xdr, lock);
}

/*
 *	struct nlm_unlockargs {
 *		netobj cookie;
 *		struct nlm_lock alock;
 *	};
 */
static void nlm_xdr_enc_unlockargs(struct rpc_rqst *req,
				   struct xdr_stream *xdr,
				   const struct nlm_args *args)
{
	const struct nlm_lock *lock = &args->lock;

	encode_cookie(xdr, &args->cookie);
	encode_nlm_lock(xdr, lock);
}

/*
 *	struct nlm_res {
 *		netobj cookie;
 *		nlm_stat stat;
 *	};
 */
static void nlm_xdr_enc_res(struct rpc_rqst *req,
			    struct xdr_stream *xdr,
			    const struct nlm_res *result)
{
	encode_cookie(xdr, &result->cookie);
	encode_nlm_stat(xdr, result->status);
}

/*
 *	union nlm_testrply switch (nlm_stats stat) {
 *	case LCK_DENIED:
 *		struct nlm_holder holder;
 *	default:
 *		void;
 *	};
 *
 *	struct nlm_testres {
 *		netobj cookie;
 *		nlm_testrply test_stat;
 *	};
 */
static void encode_nlm_testrply(struct xdr_stream *xdr,
				const struct nlm_res *result)
{
	if (result->status == nlm_lck_denied)
		encode_nlm_holder(xdr, result);
}

static void nlm_xdr_enc_testres(struct rpc_rqst *req,
				struct xdr_stream *xdr,
				const struct nlm_res *result)
{
	encode_cookie(xdr, &result->cookie);
	encode_nlm_stat(xdr, result->status);
	encode_nlm_testrply(xdr, result);
}


/*
 * NLMv3 XDR decode functions
 *
 * NLMv3 result types are defined in Chapter 10 of The Open Group's
 * "Protocols for Interworking: XNFS, Version 3W".
 */

/*
 *	union nlm_testrply switch (nlm_stats stat) {
 *	case LCK_DENIED:
 *		struct nlm_holder holder;
 *	default:
 *		void;
 *	};
 *
 *	struct nlm_testres {
 *		netobj cookie;
 *		nlm_testrply test_stat;
 *	};
 */
static int decode_nlm_testrply(struct xdr_stream *xdr,
			       struct nlm_res *result)
{
	int error;

	error = decode_nlm_stat(xdr, &result->status);
	if (unlikely(error))
		goto out;
	if (result->status == nlm_lck_denied)
		error = decode_nlm_holder(xdr, result);
out:
	return error;
}

static int nlm_xdr_dec_testres(struct rpc_rqst *req,
			       struct xdr_stream *xdr,
			       struct nlm_res *result)
{
	int error;

	error = decode_cookie(xdr, &result->cookie);
	if (unlikely(error))
		goto out;
	error = decode_nlm_testrply(xdr, result);
out:
	return error;
}

/*
 *	struct nlm_res {
 *		netobj cookie;
 *		nlm_stat stat;
 *	};
 */
static int nlm_xdr_dec_res(struct rpc_rqst *req,
			   struct xdr_stream *xdr,
			   struct nlm_res *result)
{
	int error;

	error = decode_cookie(xdr, &result->cookie);
	if (unlikely(error))
		goto out;
	error = decode_nlm_stat(xdr, &result->status);
out:
	return error;
}


/*
 * For NLM, a void procedure really returns nothing
 */
#define nlm_xdr_dec_norep	NULL

#define PROC(proc, argtype, restype)	\
[NLMPROC_##proc] = {							\
	.p_proc      = NLMPROC_##proc,					\
	.p_encode    = (kxdreproc_t)nlm_xdr_enc_##argtype,		\
	.p_decode    = (kxdrdproc_t)nlm_xdr_dec_##restype,		\
	.p_arglen    = NLM_##argtype##_sz,				\
	.p_replen    = NLM_##restype##_sz,				\
	.p_statidx   = NLMPROC_##proc,					\
	.p_name      = #proc,						\
	}

static struct rpc_procinfo	nlm_procedures[] = {
	PROC(TEST,		testargs,	testres),
	PROC(LOCK,		lockargs,	res),
	PROC(CANCEL,		cancargs,	res),
	PROC(UNLOCK,		unlockargs,	res),
	PROC(GRANTED,		testargs,	res),
	PROC(TEST_MSG,		testargs,	norep),
	PROC(LOCK_MSG,		lockargs,	norep),
	PROC(CANCEL_MSG,	cancargs,	norep),
	PROC(UNLOCK_MSG,	unlockargs,	norep),
	PROC(GRANTED_MSG,	testargs,	norep),
	PROC(TEST_RES,		testres,	norep),
	PROC(LOCK_RES,		res,		norep),
	PROC(CANCEL_RES,	res,		norep),
	PROC(UNLOCK_RES,	res,		norep),
	PROC(GRANTED_RES,	res,		norep),
};

static const struct rpc_version	nlm_version1 = {
		.number		= 1,
		.nrprocs	= ARRAY_SIZE(nlm_procedures),
		.procs		= nlm_procedures,
};

static const struct rpc_version	nlm_version3 = {
		.number		= 3,
		.nrprocs	= ARRAY_SIZE(nlm_procedures),
		.procs		= nlm_procedures,
};

static const struct rpc_version	*nlm_versions[] = {
	[1] = &nlm_version1,
	[3] = &nlm_version3,
#ifdef CONFIG_LOCKD_V4
	[4] = &nlm_version4,
#endif
};

static struct rpc_stat		nlm_rpc_stats;

const struct rpc_program	nlm_program = {
		.name		= "lockd",
		.number		= NLM_PROGRAM,
		.nrvers		= ARRAY_SIZE(nlm_versions),
		.version	= nlm_versions,
		.stats		= &nlm_rpc_stats,
};
