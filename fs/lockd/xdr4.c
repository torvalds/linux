/*
 * linux/fs/lockd/xdr4.c
 *
 * XDR support for lockd and the lock client.
 *
 * Copyright (C) 1995, 1996 Olaf Kirch <okir@monad.swb.de>
 * Copyright (C) 1999, Trond Myklebust <trond.myklebust@fys.uio.no>
 */

#include <linux/types.h>
#include <linux/sched.h>
#include <linux/utsname.h>
#include <linux/nfs.h>

#include <linux/sunrpc/xdr.h>
#include <linux/sunrpc/clnt.h>
#include <linux/sunrpc/svc.h>
#include <linux/sunrpc/stats.h>
#include <linux/lockd/lockd.h>
#include <linux/lockd/sm_inter.h>

#define NLMDBG_FACILITY		NLMDBG_XDR

static inline loff_t
s64_to_loff_t(__s64 offset)
{
	return (loff_t)offset;
}


static inline s64
loff_t_to_s64(loff_t offset)
{
	s64 res;
	if (offset > NLM4_OFFSET_MAX)
		res = NLM4_OFFSET_MAX;
	else if (offset < -NLM4_OFFSET_MAX)
		res = -NLM4_OFFSET_MAX;
	else
		res = offset;
	return res;
}

/*
 * XDR functions for basic NLM types
 */
static __be32 *
nlm4_decode_cookie(__be32 *p, struct nlm_cookie *c)
{
	unsigned int	len;

	len = ntohl(*p++);
	
	if(len==0)
	{
		c->len=4;
		memset(c->data, 0, 4);	/* hockeypux brain damage */
	}
	else if(len<=NLM_MAXCOOKIELEN)
	{
		c->len=len;
		memcpy(c->data, p, len);
		p+=XDR_QUADLEN(len);
	}
	else 
	{
		printk(KERN_NOTICE
			"lockd: bad cookie size %d (only cookies under %d bytes are supported.)\n", len, NLM_MAXCOOKIELEN);
		return NULL;
	}
	return p;
}

static __be32 *
nlm4_encode_cookie(__be32 *p, struct nlm_cookie *c)
{
	*p++ = htonl(c->len);
	memcpy(p, c->data, c->len);
	p+=XDR_QUADLEN(c->len);
	return p;
}

static __be32 *
nlm4_decode_fh(__be32 *p, struct nfs_fh *f)
{
	memset(f->data, 0, sizeof(f->data));
	f->size = ntohl(*p++);
	if (f->size > NFS_MAXFHSIZE) {
		printk(KERN_NOTICE
			"lockd: bad fhandle size %d (should be <=%d)\n",
			f->size, NFS_MAXFHSIZE);
		return NULL;
	}
      	memcpy(f->data, p, f->size);
	return p + XDR_QUADLEN(f->size);
}

static __be32 *
nlm4_encode_fh(__be32 *p, struct nfs_fh *f)
{
	*p++ = htonl(f->size);
	if (f->size) p[XDR_QUADLEN(f->size)-1] = 0; /* don't leak anything */
	memcpy(p, f->data, f->size);
	return p + XDR_QUADLEN(f->size);
}

/*
 * Encode and decode owner handle
 */
static __be32 *
nlm4_decode_oh(__be32 *p, struct xdr_netobj *oh)
{
	return xdr_decode_netobj(p, oh);
}

static __be32 *
nlm4_encode_oh(__be32 *p, struct xdr_netobj *oh)
{
	return xdr_encode_netobj(p, oh);
}

static __be32 *
nlm4_decode_lock(__be32 *p, struct nlm_lock *lock)
{
	struct file_lock	*fl = &lock->fl;
	__s64			len, start, end;

	if (!(p = xdr_decode_string_inplace(p, &lock->caller,
					    &lock->len, NLM_MAXSTRLEN))
	 || !(p = nlm4_decode_fh(p, &lock->fh))
	 || !(p = nlm4_decode_oh(p, &lock->oh)))
		return NULL;
	lock->svid  = ntohl(*p++);

	locks_init_lock(fl);
	fl->fl_owner = current->files;
	fl->fl_pid   = (pid_t)lock->svid;
	fl->fl_flags = FL_POSIX;
	fl->fl_type  = F_RDLCK;		/* as good as anything else */
	p = xdr_decode_hyper(p, &start);
	p = xdr_decode_hyper(p, &len);
	end = start + len - 1;

	fl->fl_start = s64_to_loff_t(start);

	if (len == 0 || end < 0)
		fl->fl_end = OFFSET_MAX;
	else
		fl->fl_end = s64_to_loff_t(end);
	return p;
}

/*
 * Encode a lock as part of an NLM call
 */
static __be32 *
nlm4_encode_lock(__be32 *p, struct nlm_lock *lock)
{
	struct file_lock	*fl = &lock->fl;
	__s64			start, len;

	if (!(p = xdr_encode_string(p, lock->caller))
	 || !(p = nlm4_encode_fh(p, &lock->fh))
	 || !(p = nlm4_encode_oh(p, &lock->oh)))
		return NULL;

	if (fl->fl_start > NLM4_OFFSET_MAX
	 || (fl->fl_end > NLM4_OFFSET_MAX && fl->fl_end != OFFSET_MAX))
		return NULL;

	*p++ = htonl(lock->svid);

	start = loff_t_to_s64(fl->fl_start);
	if (fl->fl_end == OFFSET_MAX)
		len = 0;
	else
		len = loff_t_to_s64(fl->fl_end - fl->fl_start + 1);

	p = xdr_encode_hyper(p, start);
	p = xdr_encode_hyper(p, len);

	return p;
}

/*
 * Encode result of a TEST/TEST_MSG call
 */
static __be32 *
nlm4_encode_testres(__be32 *p, struct nlm_res *resp)
{
	s64		start, len;

	dprintk("xdr: before encode_testres (p %p resp %p)\n", p, resp);
	if (!(p = nlm4_encode_cookie(p, &resp->cookie)))
		return NULL;
	*p++ = resp->status;

	if (resp->status == nlm_lck_denied) {
		struct file_lock	*fl = &resp->lock.fl;

		*p++ = (fl->fl_type == F_RDLCK)? xdr_zero : xdr_one;
		*p++ = htonl(resp->lock.svid);

		/* Encode owner handle. */
		if (!(p = xdr_encode_netobj(p, &resp->lock.oh)))
			return NULL;

		start = loff_t_to_s64(fl->fl_start);
		if (fl->fl_end == OFFSET_MAX)
			len = 0;
		else
			len = loff_t_to_s64(fl->fl_end - fl->fl_start + 1);
		
		p = xdr_encode_hyper(p, start);
		p = xdr_encode_hyper(p, len);
		dprintk("xdr: encode_testres (status %u pid %d type %d start %Ld end %Ld)\n",
			resp->status, (int)resp->lock.svid, fl->fl_type,
			(long long)fl->fl_start,  (long long)fl->fl_end);
	}

	dprintk("xdr: after encode_testres (p %p resp %p)\n", p, resp);
	return p;
}


/*
 * First, the server side XDR functions
 */
int
nlm4svc_decode_testargs(struct svc_rqst *rqstp, __be32 *p, nlm_args *argp)
{
	u32	exclusive;

	if (!(p = nlm4_decode_cookie(p, &argp->cookie)))
		return 0;

	exclusive = ntohl(*p++);
	if (!(p = nlm4_decode_lock(p, &argp->lock)))
		return 0;
	if (exclusive)
		argp->lock.fl.fl_type = F_WRLCK;

	return xdr_argsize_check(rqstp, p);
}

int
nlm4svc_encode_testres(struct svc_rqst *rqstp, __be32 *p, struct nlm_res *resp)
{
	if (!(p = nlm4_encode_testres(p, resp)))
		return 0;
	return xdr_ressize_check(rqstp, p);
}

int
nlm4svc_decode_lockargs(struct svc_rqst *rqstp, __be32 *p, nlm_args *argp)
{
	u32	exclusive;

	if (!(p = nlm4_decode_cookie(p, &argp->cookie)))
		return 0;
	argp->block  = ntohl(*p++);
	exclusive    = ntohl(*p++);
	if (!(p = nlm4_decode_lock(p, &argp->lock)))
		return 0;
	if (exclusive)
		argp->lock.fl.fl_type = F_WRLCK;
	argp->reclaim = ntohl(*p++);
	argp->state   = ntohl(*p++);
	argp->monitor = 1;		/* monitor client by default */

	return xdr_argsize_check(rqstp, p);
}

int
nlm4svc_decode_cancargs(struct svc_rqst *rqstp, __be32 *p, nlm_args *argp)
{
	u32	exclusive;

	if (!(p = nlm4_decode_cookie(p, &argp->cookie)))
		return 0;
	argp->block = ntohl(*p++);
	exclusive = ntohl(*p++);
	if (!(p = nlm4_decode_lock(p, &argp->lock)))
		return 0;
	if (exclusive)
		argp->lock.fl.fl_type = F_WRLCK;
	return xdr_argsize_check(rqstp, p);
}

int
nlm4svc_decode_unlockargs(struct svc_rqst *rqstp, __be32 *p, nlm_args *argp)
{
	if (!(p = nlm4_decode_cookie(p, &argp->cookie))
	 || !(p = nlm4_decode_lock(p, &argp->lock)))
		return 0;
	argp->lock.fl.fl_type = F_UNLCK;
	return xdr_argsize_check(rqstp, p);
}

int
nlm4svc_decode_shareargs(struct svc_rqst *rqstp, __be32 *p, nlm_args *argp)
{
	struct nlm_lock	*lock = &argp->lock;

	memset(lock, 0, sizeof(*lock));
	locks_init_lock(&lock->fl);
	lock->svid = ~(u32) 0;
	lock->fl.fl_pid = (pid_t)lock->svid;

	if (!(p = nlm4_decode_cookie(p, &argp->cookie))
	 || !(p = xdr_decode_string_inplace(p, &lock->caller,
					    &lock->len, NLM_MAXSTRLEN))
	 || !(p = nlm4_decode_fh(p, &lock->fh))
	 || !(p = nlm4_decode_oh(p, &lock->oh)))
		return 0;
	argp->fsm_mode = ntohl(*p++);
	argp->fsm_access = ntohl(*p++);
	return xdr_argsize_check(rqstp, p);
}

int
nlm4svc_encode_shareres(struct svc_rqst *rqstp, __be32 *p, struct nlm_res *resp)
{
	if (!(p = nlm4_encode_cookie(p, &resp->cookie)))
		return 0;
	*p++ = resp->status;
	*p++ = xdr_zero;		/* sequence argument */
	return xdr_ressize_check(rqstp, p);
}

int
nlm4svc_encode_res(struct svc_rqst *rqstp, __be32 *p, struct nlm_res *resp)
{
	if (!(p = nlm4_encode_cookie(p, &resp->cookie)))
		return 0;
	*p++ = resp->status;
	return xdr_ressize_check(rqstp, p);
}

int
nlm4svc_decode_notify(struct svc_rqst *rqstp, __be32 *p, struct nlm_args *argp)
{
	struct nlm_lock	*lock = &argp->lock;

	if (!(p = xdr_decode_string_inplace(p, &lock->caller,
					    &lock->len, NLM_MAXSTRLEN)))
		return 0;
	argp->state = ntohl(*p++);
	return xdr_argsize_check(rqstp, p);
}

int
nlm4svc_decode_reboot(struct svc_rqst *rqstp, __be32 *p, struct nlm_reboot *argp)
{
	if (!(p = xdr_decode_string_inplace(p, &argp->mon, &argp->len, SM_MAXSTRLEN)))
		return 0;
	argp->state = ntohl(*p++);
	/* Preserve the address in network byte order */
	argp->addr  = *p++;
	argp->vers  = *p++;
	argp->proto = *p++;
	return xdr_argsize_check(rqstp, p);
}

int
nlm4svc_decode_res(struct svc_rqst *rqstp, __be32 *p, struct nlm_res *resp)
{
	if (!(p = nlm4_decode_cookie(p, &resp->cookie)))
		return 0;
	resp->status = *p++;
	return xdr_argsize_check(rqstp, p);
}

int
nlm4svc_decode_void(struct svc_rqst *rqstp, __be32 *p, void *dummy)
{
	return xdr_argsize_check(rqstp, p);
}

int
nlm4svc_encode_void(struct svc_rqst *rqstp, __be32 *p, void *dummy)
{
	return xdr_ressize_check(rqstp, p);
}

/*
 * Now, the client side XDR functions
 */
#ifdef NLMCLNT_SUPPORT_SHARES
static int
nlm4clt_decode_void(struct rpc_rqst *req, __be32 *p, void *ptr)
{
	return 0;
}
#endif

static int
nlm4clt_encode_testargs(struct rpc_rqst *req, __be32 *p, nlm_args *argp)
{
	struct nlm_lock	*lock = &argp->lock;

	if (!(p = nlm4_encode_cookie(p, &argp->cookie)))
		return -EIO;
	*p++ = (lock->fl.fl_type == F_WRLCK)? xdr_one : xdr_zero;
	if (!(p = nlm4_encode_lock(p, lock)))
		return -EIO;
	req->rq_slen = xdr_adjust_iovec(req->rq_svec, p);
	return 0;
}

static int
nlm4clt_decode_testres(struct rpc_rqst *req, __be32 *p, struct nlm_res *resp)
{
	if (!(p = nlm4_decode_cookie(p, &resp->cookie)))
		return -EIO;
	resp->status = *p++;
	if (resp->status == nlm_lck_denied) {
		struct file_lock	*fl = &resp->lock.fl;
		u32			excl;
		s64			start, end, len;

		memset(&resp->lock, 0, sizeof(resp->lock));
		locks_init_lock(fl);
		excl = ntohl(*p++);
		resp->lock.svid = ntohl(*p++);
		fl->fl_pid = (pid_t)resp->lock.svid;
		if (!(p = nlm4_decode_oh(p, &resp->lock.oh)))
			return -EIO;

		fl->fl_flags = FL_POSIX;
		fl->fl_type  = excl? F_WRLCK : F_RDLCK;
		p = xdr_decode_hyper(p, &start);
		p = xdr_decode_hyper(p, &len);
		end = start + len - 1;

		fl->fl_start = s64_to_loff_t(start);
		if (len == 0 || end < 0)
			fl->fl_end = OFFSET_MAX;
		else
			fl->fl_end = s64_to_loff_t(end);
	}
	return 0;
}


static int
nlm4clt_encode_lockargs(struct rpc_rqst *req, __be32 *p, nlm_args *argp)
{
	struct nlm_lock	*lock = &argp->lock;

	if (!(p = nlm4_encode_cookie(p, &argp->cookie)))
		return -EIO;
	*p++ = argp->block? xdr_one : xdr_zero;
	*p++ = (lock->fl.fl_type == F_WRLCK)? xdr_one : xdr_zero;
	if (!(p = nlm4_encode_lock(p, lock)))
		return -EIO;
	*p++ = argp->reclaim? xdr_one : xdr_zero;
	*p++ = htonl(argp->state);
	req->rq_slen = xdr_adjust_iovec(req->rq_svec, p);
	return 0;
}

static int
nlm4clt_encode_cancargs(struct rpc_rqst *req, __be32 *p, nlm_args *argp)
{
	struct nlm_lock	*lock = &argp->lock;

	if (!(p = nlm4_encode_cookie(p, &argp->cookie)))
		return -EIO;
	*p++ = argp->block? xdr_one : xdr_zero;
	*p++ = (lock->fl.fl_type == F_WRLCK)? xdr_one : xdr_zero;
	if (!(p = nlm4_encode_lock(p, lock)))
		return -EIO;
	req->rq_slen = xdr_adjust_iovec(req->rq_svec, p);
	return 0;
}

static int
nlm4clt_encode_unlockargs(struct rpc_rqst *req, __be32 *p, nlm_args *argp)
{
	struct nlm_lock	*lock = &argp->lock;

	if (!(p = nlm4_encode_cookie(p, &argp->cookie)))
		return -EIO;
	if (!(p = nlm4_encode_lock(p, lock)))
		return -EIO;
	req->rq_slen = xdr_adjust_iovec(req->rq_svec, p);
	return 0;
}

static int
nlm4clt_encode_res(struct rpc_rqst *req, __be32 *p, struct nlm_res *resp)
{
	if (!(p = nlm4_encode_cookie(p, &resp->cookie)))
		return -EIO;
	*p++ = resp->status;
	req->rq_slen = xdr_adjust_iovec(req->rq_svec, p);
	return 0;
}

static int
nlm4clt_encode_testres(struct rpc_rqst *req, __be32 *p, struct nlm_res *resp)
{
	if (!(p = nlm4_encode_testres(p, resp)))
		return -EIO;
	req->rq_slen = xdr_adjust_iovec(req->rq_svec, p);
	return 0;
}

static int
nlm4clt_decode_res(struct rpc_rqst *req, __be32 *p, struct nlm_res *resp)
{
	if (!(p = nlm4_decode_cookie(p, &resp->cookie)))
		return -EIO;
	resp->status = *p++;
	return 0;
}

#if (NLMCLNT_OHSIZE > XDR_MAX_NETOBJ)
#  error "NLM host name cannot be larger than XDR_MAX_NETOBJ!"
#endif

#if (NLMCLNT_OHSIZE > NLM_MAXSTRLEN)
#  error "NLM host name cannot be larger than NLM's maximum string length!"
#endif

/*
 * Buffer requirements for NLM
 */
#define NLM4_void_sz		0
#define NLM4_cookie_sz		1+XDR_QUADLEN(NLM_MAXCOOKIELEN)
#define NLM4_caller_sz		1+XDR_QUADLEN(NLMCLNT_OHSIZE)
#define NLM4_owner_sz		1+XDR_QUADLEN(NLMCLNT_OHSIZE)
#define NLM4_fhandle_sz		1+XDR_QUADLEN(NFS3_FHSIZE)
#define NLM4_lock_sz		5+NLM4_caller_sz+NLM4_owner_sz+NLM4_fhandle_sz
#define NLM4_holder_sz		6+NLM4_owner_sz

#define NLM4_testargs_sz	NLM4_cookie_sz+1+NLM4_lock_sz
#define NLM4_lockargs_sz	NLM4_cookie_sz+4+NLM4_lock_sz
#define NLM4_cancargs_sz	NLM4_cookie_sz+2+NLM4_lock_sz
#define NLM4_unlockargs_sz	NLM4_cookie_sz+NLM4_lock_sz

#define NLM4_testres_sz		NLM4_cookie_sz+1+NLM4_holder_sz
#define NLM4_res_sz		NLM4_cookie_sz+1
#define NLM4_norep_sz		0

/*
 * For NLM, a void procedure really returns nothing
 */
#define nlm4clt_decode_norep	NULL

#define PROC(proc, argtype, restype)					\
[NLMPROC_##proc] = {							\
	.p_proc      = NLMPROC_##proc,					\
	.p_encode    = (kxdrproc_t) nlm4clt_encode_##argtype,		\
	.p_decode    = (kxdrproc_t) nlm4clt_decode_##restype,		\
	.p_arglen    = NLM4_##argtype##_sz,				\
	.p_replen    = NLM4_##restype##_sz,				\
	.p_statidx   = NLMPROC_##proc,					\
	.p_name      = #proc,						\
	}

static struct rpc_procinfo	nlm4_procedures[] = {
    PROC(TEST,		testargs,	testres),
    PROC(LOCK,		lockargs,	res),
    PROC(CANCEL,	cancargs,	res),
    PROC(UNLOCK,	unlockargs,	res),
    PROC(GRANTED,	testargs,	res),
    PROC(TEST_MSG,	testargs,	norep),
    PROC(LOCK_MSG,	lockargs,	norep),
    PROC(CANCEL_MSG,	cancargs,	norep),
    PROC(UNLOCK_MSG,	unlockargs,	norep),
    PROC(GRANTED_MSG,	testargs,	norep),
    PROC(TEST_RES,	testres,	norep),
    PROC(LOCK_RES,	res,		norep),
    PROC(CANCEL_RES,	res,		norep),
    PROC(UNLOCK_RES,	res,		norep),
    PROC(GRANTED_RES,	res,		norep),
#ifdef NLMCLNT_SUPPORT_SHARES
    PROC(SHARE,		shareargs,	shareres),
    PROC(UNSHARE,	shareargs,	shareres),
    PROC(NM_LOCK,	lockargs,	res),
    PROC(FREE_ALL,	notify,		void),
#endif
};

struct rpc_version	nlm_version4 = {
	.number		= 4,
	.nrprocs	= 24,
	.procs		= nlm4_procedures,
};
