// SPDX-License-Identifier: GPL-2.0
/*
 * linux/fs/lockd/xdr.c
 *
 * XDR support for lockd and the lock client.
 *
 * Copyright (C) 1995, 1996 Olaf Kirch <okir@monad.swb.de>
 */

#include <linux/types.h>
#include <linux/sched.h>
#include <linux/nfs.h>

#include <linux/sunrpc/xdr.h>
#include <linux/sunrpc/clnt.h>
#include <linux/sunrpc/svc.h>
#include <linux/sunrpc/stats.h>
#include <linux/lockd/lockd.h>

#include <uapi/linux/nfs2.h>

#include "svcxdr.h"


static inline loff_t
s32_to_loff_t(__s32 offset)
{
	return (loff_t)offset;
}

static inline __s32
loff_t_to_s32(loff_t offset)
{
	__s32 res;
	if (offset >= NLM_OFFSET_MAX)
		res = NLM_OFFSET_MAX;
	else if (offset <= -NLM_OFFSET_MAX)
		res = -NLM_OFFSET_MAX;
	else
		res = offset;
	return res;
}

/*
 * NLM file handles are defined by specification to be a variable-length
 * XDR opaque no longer than 1024 bytes. However, this implementation
 * constrains their length to exactly the length of an NFSv2 file
 * handle.
 */
static bool
svcxdr_decode_fhandle(struct xdr_stream *xdr, struct nfs_fh *fh)
{
	__be32 *p;
	u32 len;

	if (xdr_stream_decode_u32(xdr, &len) < 0)
		return false;
	if (len != NFS2_FHSIZE)
		return false;

	p = xdr_inline_decode(xdr, len);
	if (!p)
		return false;
	fh->size = NFS2_FHSIZE;
	memcpy(fh->data, p, len);
	memset(fh->data + NFS2_FHSIZE, 0, sizeof(fh->data) - NFS2_FHSIZE);

	return true;
}

static bool
svcxdr_decode_lock(struct xdr_stream *xdr, struct nlm_lock *lock)
{
	struct file_lock *fl = &lock->fl;
	s32 start, len, end;

	if (!svcxdr_decode_string(xdr, &lock->caller, &lock->len))
		return false;
	if (!svcxdr_decode_fhandle(xdr, &lock->fh))
		return false;
	if (!svcxdr_decode_owner(xdr, &lock->oh))
		return false;
	if (xdr_stream_decode_u32(xdr, &lock->svid) < 0)
		return false;
	if (xdr_stream_decode_u32(xdr, &start) < 0)
		return false;
	if (xdr_stream_decode_u32(xdr, &len) < 0)
		return false;

	locks_init_lock(fl);
	fl->fl_flags = FL_POSIX;
	fl->fl_type  = F_RDLCK;
	end = start + len - 1;
	fl->fl_start = s32_to_loff_t(start);
	if (len == 0 || end < 0)
		fl->fl_end = OFFSET_MAX;
	else
		fl->fl_end = s32_to_loff_t(end);

	return true;
}

static bool
svcxdr_encode_holder(struct xdr_stream *xdr, const struct nlm_lock *lock)
{
	const struct file_lock *fl = &lock->fl;
	s32 start, len;

	/* exclusive */
	if (xdr_stream_encode_bool(xdr, fl->fl_type != F_RDLCK) < 0)
		return false;
	if (xdr_stream_encode_u32(xdr, lock->svid) < 0)
		return false;
	if (!svcxdr_encode_owner(xdr, &lock->oh))
		return false;
	start = loff_t_to_s32(fl->fl_start);
	if (fl->fl_end == OFFSET_MAX)
		len = 0;
	else
		len = loff_t_to_s32(fl->fl_end - fl->fl_start + 1);
	if (xdr_stream_encode_u32(xdr, start) < 0)
		return false;
	if (xdr_stream_encode_u32(xdr, len) < 0)
		return false;

	return true;
}

static bool
svcxdr_encode_testrply(struct xdr_stream *xdr, const struct nlm_res *resp)
{
	if (!svcxdr_encode_stats(xdr, resp->status))
		return false;
	switch (resp->status) {
	case nlm_lck_denied:
		if (!svcxdr_encode_holder(xdr, &resp->lock))
			return false;
	}

	return true;
}


/*
 * Decode Call arguments
 */

bool
nlmsvc_decode_void(struct svc_rqst *rqstp, struct xdr_stream *xdr)
{
	return true;
}

bool
nlmsvc_decode_testargs(struct svc_rqst *rqstp, struct xdr_stream *xdr)
{
	struct nlm_args *argp = rqstp->rq_argp;
	u32 exclusive;

	if (!svcxdr_decode_cookie(xdr, &argp->cookie))
		return false;
	if (xdr_stream_decode_bool(xdr, &exclusive) < 0)
		return false;
	if (!svcxdr_decode_lock(xdr, &argp->lock))
		return false;
	if (exclusive)
		argp->lock.fl.fl_type = F_WRLCK;

	return true;
}

bool
nlmsvc_decode_lockargs(struct svc_rqst *rqstp, struct xdr_stream *xdr)
{
	struct nlm_args *argp = rqstp->rq_argp;
	u32 exclusive;

	if (!svcxdr_decode_cookie(xdr, &argp->cookie))
		return false;
	if (xdr_stream_decode_bool(xdr, &argp->block) < 0)
		return false;
	if (xdr_stream_decode_bool(xdr, &exclusive) < 0)
		return false;
	if (!svcxdr_decode_lock(xdr, &argp->lock))
		return false;
	if (exclusive)
		argp->lock.fl.fl_type = F_WRLCK;
	if (xdr_stream_decode_bool(xdr, &argp->reclaim) < 0)
		return false;
	if (xdr_stream_decode_u32(xdr, &argp->state) < 0)
		return false;
	argp->monitor = 1;		/* monitor client by default */

	return true;
}

bool
nlmsvc_decode_cancargs(struct svc_rqst *rqstp, struct xdr_stream *xdr)
{
	struct nlm_args *argp = rqstp->rq_argp;
	u32 exclusive;

	if (!svcxdr_decode_cookie(xdr, &argp->cookie))
		return false;
	if (xdr_stream_decode_bool(xdr, &argp->block) < 0)
		return false;
	if (xdr_stream_decode_bool(xdr, &exclusive) < 0)
		return false;
	if (!svcxdr_decode_lock(xdr, &argp->lock))
		return false;
	if (exclusive)
		argp->lock.fl.fl_type = F_WRLCK;

	return true;
}

bool
nlmsvc_decode_unlockargs(struct svc_rqst *rqstp, struct xdr_stream *xdr)
{
	struct nlm_args *argp = rqstp->rq_argp;

	if (!svcxdr_decode_cookie(xdr, &argp->cookie))
		return false;
	if (!svcxdr_decode_lock(xdr, &argp->lock))
		return false;
	argp->lock.fl.fl_type = F_UNLCK;

	return true;
}

bool
nlmsvc_decode_res(struct svc_rqst *rqstp, struct xdr_stream *xdr)
{
	struct nlm_res *resp = rqstp->rq_argp;

	if (!svcxdr_decode_cookie(xdr, &resp->cookie))
		return false;
	if (!svcxdr_decode_stats(xdr, &resp->status))
		return false;

	return true;
}

bool
nlmsvc_decode_reboot(struct svc_rqst *rqstp, struct xdr_stream *xdr)
{
	struct nlm_reboot *argp = rqstp->rq_argp;
	__be32 *p;
	u32 len;

	if (xdr_stream_decode_u32(xdr, &len) < 0)
		return false;
	if (len > SM_MAXSTRLEN)
		return false;
	p = xdr_inline_decode(xdr, len);
	if (!p)
		return false;
	argp->len = len;
	argp->mon = (char *)p;
	if (xdr_stream_decode_u32(xdr, &argp->state) < 0)
		return false;
	p = xdr_inline_decode(xdr, SM_PRIV_SIZE);
	if (!p)
		return false;
	memcpy(&argp->priv.data, p, sizeof(argp->priv.data));

	return true;
}

bool
nlmsvc_decode_shareargs(struct svc_rqst *rqstp, struct xdr_stream *xdr)
{
	struct nlm_args *argp = rqstp->rq_argp;
	struct nlm_lock	*lock = &argp->lock;

	memset(lock, 0, sizeof(*lock));
	locks_init_lock(&lock->fl);
	lock->svid = ~(u32)0;

	if (!svcxdr_decode_cookie(xdr, &argp->cookie))
		return false;
	if (!svcxdr_decode_string(xdr, &lock->caller, &lock->len))
		return false;
	if (!svcxdr_decode_fhandle(xdr, &lock->fh))
		return false;
	if (!svcxdr_decode_owner(xdr, &lock->oh))
		return false;
	/* XXX: Range checks are missing in the original code */
	if (xdr_stream_decode_u32(xdr, &argp->fsm_mode) < 0)
		return false;
	if (xdr_stream_decode_u32(xdr, &argp->fsm_access) < 0)
		return false;

	return true;
}

bool
nlmsvc_decode_notify(struct svc_rqst *rqstp, struct xdr_stream *xdr)
{
	struct nlm_args *argp = rqstp->rq_argp;
	struct nlm_lock	*lock = &argp->lock;

	if (!svcxdr_decode_string(xdr, &lock->caller, &lock->len))
		return false;
	if (xdr_stream_decode_u32(xdr, &argp->state) < 0)
		return false;

	return true;
}


/*
 * Encode Reply results
 */

bool
nlmsvc_encode_void(struct svc_rqst *rqstp, struct xdr_stream *xdr)
{
	return true;
}

bool
nlmsvc_encode_testres(struct svc_rqst *rqstp, struct xdr_stream *xdr)
{
	struct nlm_res *resp = rqstp->rq_resp;

	return svcxdr_encode_cookie(xdr, &resp->cookie) &&
		svcxdr_encode_testrply(xdr, resp);
}

bool
nlmsvc_encode_res(struct svc_rqst *rqstp, struct xdr_stream *xdr)
{
	struct nlm_res *resp = rqstp->rq_resp;

	return svcxdr_encode_cookie(xdr, &resp->cookie) &&
		svcxdr_encode_stats(xdr, resp->status);
}

bool
nlmsvc_encode_shareres(struct svc_rqst *rqstp, struct xdr_stream *xdr)
{
	struct nlm_res *resp = rqstp->rq_resp;

	if (!svcxdr_encode_cookie(xdr, &resp->cookie))
		return false;
	if (!svcxdr_encode_stats(xdr, resp->status))
		return false;
	/* sequence */
	if (xdr_stream_encode_u32(xdr, 0) < 0)
		return false;

	return true;
}
