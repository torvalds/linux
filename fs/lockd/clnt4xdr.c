/*
 * linux/fs/lockd/clnt4xdr.c
 *
 * XDR functions to encode/decode NLM version 4 RPC arguments and results.
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

#include <uapi/linux/nfs3.h>

#define NLMDBG_FACILITY		NLMDBG_XDR

#if (NLMCLNT_OHSIZE > XDR_MAX_NETOBJ)
#  error "NLM host name cannot be larger than XDR_MAX_NETOBJ!"
#endif

#if (NLMCLNT_OHSIZE > NLM_MAXSTRLEN)
#  error "NLM host name cannot be larger than NLM's maximum string length!"
#endif

/*
 * Declare the space requirements for NLM arguments and replies as
 * number of 32bit-words
 */
#define NLM4_void_sz		(0)
#define NLM4_cookie_sz		(1+(NLM_MAXCOOKIELEN>>2))
#define NLM4_caller_sz		(1+(NLMCLNT_OHSIZE>>2))
#define NLM4_owner_sz		(1+(NLMCLNT_OHSIZE>>2))
#define NLM4_fhandle_sz		(1+(NFS3_FHSIZE>>2))
#define NLM4_lock_sz		(5+NLM4_caller_sz+NLM4_owner_sz+NLM4_fhandle_sz)
#define NLM4_holder_sz		(6+NLM4_owner_sz)

#define NLM4_testargs_sz	(NLM4_cookie_sz+1+NLM4_lock_sz)
#define NLM4_lockargs_sz	(NLM4_cookie_sz+4+NLM4_lock_sz)
#define NLM4_cancargs_sz	(NLM4_cookie_sz+2+NLM4_lock_sz)
#define NLM4_unlockargs_sz	(NLM4_cookie_sz+NLM4_lock_sz)

#define NLM4_testres_sz		(NLM4_cookie_sz+1+NLM4_holder_sz)
#define NLM4_res_sz		(NLM4_cookie_sz+1)
#define NLM4_norep_sz		(0)


static s64 loff_t_to_s64(loff_t offset)
{
	s64 res;

	if (offset >= NLM4_OFFSET_MAX)
		res = NLM4_OFFSET_MAX;
	else if (offset <= -NLM4_OFFSET_MAX)
		res = -NLM4_OFFSET_MAX;
	else
		res = offset;
	return res;
}

static void nlm4_compute_offsets(const struct nlm_lock *lock,
				 u64 *l_offset, u64 *l_len)
{
	const struct file_lock *fl = &lock->fl;

	*l_offset = loff_t_to_s64(fl->fl_start);
	if (fl->fl_end == OFFSET_MAX)
		*l_len = 0;
	else
		*l_len = loff_t_to_s64(fl->fl_end - fl->fl_start + 1);
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
 * Encode/decode NLMv4 basic data types
 *
 * Basic NLMv4 data types are defined in Appendix II, section 6.1.4
 * of RFC 1813: "NFS Version 3 Protocol Specification" and in Chapter
 * 10 of X/Open's "Protocols for Interworking: XNFS, Version 3W".
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
	encode_netobj(xdr, (u8 *)&fh->data, fh->size);
}

/*
 *	enum nlm4_stats {
 *		NLM4_GRANTED = 0,
 *		NLM4_DENIED = 1,
 *		NLM4_DENIED_NOLOCKS = 2,
 *		NLM4_BLOCKED = 3,
 *		NLM4_DENIED_GRACE_PERIOD = 4,
 *		NLM4_DEADLCK = 5,
 *		NLM4_ROFS = 6,
 *		NLM4_STALE_FH = 7,
 *		NLM4_FBIG = 8,
 *		NLM4_FAILED = 9
 *	};
 *
 *	struct nlm4_stat {
 *		nlm4_stats stat;
 *	};
 *
 * NB: we don't swap bytes for the NLM status values.  The upper
 * layers deal directly with the status value in network byte
 * order.
 */
static void encode_nlm4_stat(struct xdr_stream *xdr,
			     const __be32 stat)
{
	__be32 *p;

	BUG_ON(be32_to_cpu(stat) > NLM_FAILED);
	p = xdr_reserve_space(xdr, 4);
	*p = stat;
}

static int decode_nlm4_stat(struct xdr_stream *xdr, __be32 *stat)
{
	__be32 *p;

	p = xdr_inline_decode(xdr, 4);
	if (unlikely(p == NULL))
		goto out_overflow;
	if (unlikely(ntohl(*p) > ntohl(nlm4_failed)))
		goto out_bad_xdr;
	*stat = *p;
	return 0;
out_bad_xdr:
	dprintk("%s: server returned invalid nlm4_stats value: %u\n",
			__func__, be32_to_cpup(p));
	return -EIO;
out_overflow:
	print_overflow_msg(__func__, xdr);
	return -EIO;
}

/*
 *	struct nlm4_holder {
 *		bool	exclusive;
 *		int32	svid;
 *		netobj	oh;
 *		uint64	l_offset;
 *		uint64	l_len;
 *	};
 */
static void encode_nlm4_holder(struct xdr_stream *xdr,
			       const struct nlm_res *result)
{
	const struct nlm_lock *lock = &result->lock;
	u64 l_offset, l_len;
	__be32 *p;

	encode_bool(xdr, lock->fl.fl_type == F_RDLCK);
	encode_int32(xdr, lock->svid);
	encode_netobj(xdr, lock->oh.data, lock->oh.len);

	p = xdr_reserve_space(xdr, 4 + 4);
	nlm4_compute_offsets(lock, &l_offset, &l_len);
	p = xdr_encode_hyper(p, l_offset);
	xdr_encode_hyper(p, l_len);
}

static int decode_nlm4_holder(struct xdr_stream *xdr, struct nlm_res *result)
{
	struct nlm_lock *lock = &result->lock;
	struct file_lock *fl = &lock->fl;
	u64 l_offset, l_len;
	u32 exclusive;
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

	p = xdr_inline_decode(xdr, 8 + 8);
	if (unlikely(p == NULL))
		goto out_overflow;

	fl->fl_flags = FL_POSIX;
	fl->fl_type  = exclusive != 0 ? F_WRLCK : F_RDLCK;
	p = xdr_decode_hyper(p, &l_offset);
	xdr_decode_hyper(p, &l_len);
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
 *	struct nlm4_lock {
 *		string	caller_name<LM_MAXSTRLEN>;
 *		netobj	fh;
 *		netobj	oh;
 *		int32	svid;
 *		uint64	l_offset;
 *		uint64	l_len;
 *	};
 */
static void encode_nlm4_lock(struct xdr_stream *xdr,
			     const struct nlm_lock *lock)
{
	u64 l_offset, l_len;
	__be32 *p;

	encode_caller_name(xdr, lock->caller);
	encode_fh(xdr, &lock->fh);
	encode_netobj(xdr, lock->oh.data, lock->oh.len);

	p = xdr_reserve_space(xdr, 4 + 8 + 8);
	*p++ = cpu_to_be32(lock->svid);

	nlm4_compute_offsets(lock, &l_offset, &l_len);
	p = xdr_encode_hyper(p, l_offset);
	xdr_encode_hyper(p, l_len);
}


/*
 * NLMv4 XDR encode functions
 *
 * NLMv4 argument types are defined in Appendix II of RFC 1813:
 * "NFS Version 3 Protocol Specification" and Chapter 10 of X/Open's
 * "Protocols for Interworking: XNFS, Version 3W".
 */

/*
 *	struct nlm4_testargs {
 *		netobj cookie;
 *		bool exclusive;
 *		struct nlm4_lock alock;
 *	};
 */
static void nlm4_xdr_enc_testargs(struct rpc_rqst *req,
				  struct xdr_stream *xdr,
				  const void *data)
{
	const struct nlm_args *args = data;
	const struct nlm_lock *lock = &args->lock;

	encode_cookie(xdr, &args->cookie);
	encode_bool(xdr, lock->fl.fl_type == F_WRLCK);
	encode_nlm4_lock(xdr, lock);
}

/*
 *	struct nlm4_lockargs {
 *		netobj cookie;
 *		bool block;
 *		bool exclusive;
 *		struct nlm4_lock alock;
 *		bool reclaim;
 *		int state;
 *	};
 */
static void nlm4_xdr_enc_lockargs(struct rpc_rqst *req,
				  struct xdr_stream *xdr,
				  const void *data)
{
	const struct nlm_args *args = data;
	const struct nlm_lock *lock = &args->lock;

	encode_cookie(xdr, &args->cookie);
	encode_bool(xdr, args->block);
	encode_bool(xdr, lock->fl.fl_type == F_WRLCK);
	encode_nlm4_lock(xdr, lock);
	encode_bool(xdr, args->reclaim);
	encode_int32(xdr, args->state);
}

/*
 *	struct nlm4_cancargs {
 *		netobj cookie;
 *		bool block;
 *		bool exclusive;
 *		struct nlm4_lock alock;
 *	};
 */
static void nlm4_xdr_enc_cancargs(struct rpc_rqst *req,
				  struct xdr_stream *xdr,
				  const void *data)
{
	const struct nlm_args *args = data;
	const struct nlm_lock *lock = &args->lock;

	encode_cookie(xdr, &args->cookie);
	encode_bool(xdr, args->block);
	encode_bool(xdr, lock->fl.fl_type == F_WRLCK);
	encode_nlm4_lock(xdr, lock);
}

/*
 *	struct nlm4_unlockargs {
 *		netobj cookie;
 *		struct nlm4_lock alock;
 *	};
 */
static void nlm4_xdr_enc_unlockargs(struct rpc_rqst *req,
				    struct xdr_stream *xdr,
				    const void *data)
{
	const struct nlm_args *args = data;
	const struct nlm_lock *lock = &args->lock;

	encode_cookie(xdr, &args->cookie);
	encode_nlm4_lock(xdr, lock);
}

/*
 *	struct nlm4_res {
 *		netobj cookie;
 *		nlm4_stat stat;
 *	};
 */
static void nlm4_xdr_enc_res(struct rpc_rqst *req,
			     struct xdr_stream *xdr,
			     const void *data)
{
	const struct nlm_res *result = data;

	encode_cookie(xdr, &result->cookie);
	encode_nlm4_stat(xdr, result->status);
}

/*
 *	union nlm4_testrply switch (nlm4_stats stat) {
 *	case NLM4_DENIED:
 *		struct nlm4_holder holder;
 *	default:
 *		void;
 *	};
 *
 *	struct nlm4_testres {
 *		netobj cookie;
 *		nlm4_testrply test_stat;
 *	};
 */
static void nlm4_xdr_enc_testres(struct rpc_rqst *req,
				 struct xdr_stream *xdr,
				 const void *data)
{
	const struct nlm_res *result = data;

	encode_cookie(xdr, &result->cookie);
	encode_nlm4_stat(xdr, result->status);
	if (result->status == nlm_lck_denied)
		encode_nlm4_holder(xdr, result);
}


/*
 * NLMv4 XDR decode functions
 *
 * NLMv4 argument types are defined in Appendix II of RFC 1813:
 * "NFS Version 3 Protocol Specification" and Chapter 10 of X/Open's
 * "Protocols for Interworking: XNFS, Version 3W".
 */

/*
 *	union nlm4_testrply switch (nlm4_stats stat) {
 *	case NLM4_DENIED:
 *		struct nlm4_holder holder;
 *	default:
 *		void;
 *	};
 *
 *	struct nlm4_testres {
 *		netobj cookie;
 *		nlm4_testrply test_stat;
 *	};
 */
static int decode_nlm4_testrply(struct xdr_stream *xdr,
				struct nlm_res *result)
{
	int error;

	error = decode_nlm4_stat(xdr, &result->status);
	if (unlikely(error))
		goto out;
	if (result->status == nlm_lck_denied)
		error = decode_nlm4_holder(xdr, result);
out:
	return error;
}

static int nlm4_xdr_dec_testres(struct rpc_rqst *req,
				struct xdr_stream *xdr,
				void *data)
{
	struct nlm_res *result = data;
	int error;

	error = decode_cookie(xdr, &result->cookie);
	if (unlikely(error))
		goto out;
	error = decode_nlm4_testrply(xdr, result);
out:
	return error;
}

/*
 *	struct nlm4_res {
 *		netobj cookie;
 *		nlm4_stat stat;
 *	};
 */
static int nlm4_xdr_dec_res(struct rpc_rqst *req,
			    struct xdr_stream *xdr,
			    void *data)
{
	struct nlm_res *result = data;
	int error;

	error = decode_cookie(xdr, &result->cookie);
	if (unlikely(error))
		goto out;
	error = decode_nlm4_stat(xdr, &result->status);
out:
	return error;
}


/*
 * For NLM, a void procedure really returns nothing
 */
#define nlm4_xdr_dec_norep	NULL

#define PROC(proc, argtype, restype)					\
[NLMPROC_##proc] = {							\
	.p_proc      = NLMPROC_##proc,					\
	.p_encode    = nlm4_xdr_enc_##argtype,				\
	.p_decode    = nlm4_xdr_dec_##restype,				\
	.p_arglen    = NLM4_##argtype##_sz,				\
	.p_replen    = NLM4_##restype##_sz,				\
	.p_statidx   = NLMPROC_##proc,					\
	.p_name      = #proc,						\
	}

static struct rpc_procinfo	nlm4_procedures[] = {
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

const struct rpc_version nlm_version4 = {
	.number		= 4,
	.nrprocs	= ARRAY_SIZE(nlm4_procedures),
	.procs		= nlm4_procedures,
};
