// SPDX-License-Identifier: GPL-2.0
/*
 * Neil Brown <neilb@cse.unsw.edu.au>
 * J. Bruce Fields <bfields@umich.edu>
 * Andy Adamson <andros@umich.edu>
 * Dug Song <dugsong@monkey.org>
 *
 * RPCSEC_GSS server authentication.
 * This implements RPCSEC_GSS as defined in rfc2203 (rpcsec_gss) and rfc2078
 * (gssapi)
 *
 * The RPCSEC_GSS involves three stages:
 *  1/ context creation
 *  2/ data exchange
 *  3/ context destruction
 *
 * Context creation is handled largely by upcalls to user-space.
 *  In particular, GSS_Accept_sec_context is handled by an upcall
 * Data exchange is handled entirely within the kernel
 *  In particular, GSS_GetMIC, GSS_VerifyMIC, GSS_Seal, GSS_Unseal are in-kernel.
 * Context destruction is handled in-kernel
 *  GSS_Delete_sec_context is in-kernel
 *
 * Context creation is initiated by a RPCSEC_GSS_INIT request arriving.
 * The context handle and gss_token are used as a key into the rpcsec_init cache.
 * The content of this cache includes some of the outputs of GSS_Accept_sec_context,
 * being major_status, minor_status, context_handle, reply_token.
 * These are sent back to the client.
 * Sequence window management is handled by the kernel.  The window size if currently
 * a compile time constant.
 *
 * When user-space is happy that a context is established, it places an entry
 * in the rpcsec_context cache. The key for this cache is the context_handle.
 * The content includes:
 *   uid/gidlist - for determining access rights
 *   mechanism type
 *   mechanism specific information, such as a key
 *
 */

#include <linux/slab.h>
#include <linux/types.h>
#include <linux/module.h>
#include <linux/pagemap.h>
#include <linux/user_namespace.h>

#include <linux/sunrpc/auth_gss.h>
#include <linux/sunrpc/gss_err.h>
#include <linux/sunrpc/svcauth.h>
#include <linux/sunrpc/svcauth_gss.h>
#include <linux/sunrpc/cache.h>

#include <trace/events/rpcgss.h>

#include "gss_rpc_upcall.h"


/* The rpcsec_init cache is used for mapping RPCSEC_GSS_{,CONT_}INIT requests
 * into replies.
 *
 * Key is context handle (\x if empty) and gss_token.
 * Content is major_status minor_status (integers) context_handle, reply_token.
 *
 */

static int netobj_equal(struct xdr_netobj *a, struct xdr_netobj *b)
{
	return a->len == b->len && 0 == memcmp(a->data, b->data, a->len);
}

#define	RSI_HASHBITS	6
#define	RSI_HASHMAX	(1<<RSI_HASHBITS)

struct rsi {
	struct cache_head	h;
	struct xdr_netobj	in_handle, in_token;
	struct xdr_netobj	out_handle, out_token;
	int			major_status, minor_status;
	struct rcu_head		rcu_head;
};

static struct rsi *rsi_update(struct cache_detail *cd, struct rsi *new, struct rsi *old);
static struct rsi *rsi_lookup(struct cache_detail *cd, struct rsi *item);

static void rsi_free(struct rsi *rsii)
{
	kfree(rsii->in_handle.data);
	kfree(rsii->in_token.data);
	kfree(rsii->out_handle.data);
	kfree(rsii->out_token.data);
}

static void rsi_free_rcu(struct rcu_head *head)
{
	struct rsi *rsii = container_of(head, struct rsi, rcu_head);

	rsi_free(rsii);
	kfree(rsii);
}

static void rsi_put(struct kref *ref)
{
	struct rsi *rsii = container_of(ref, struct rsi, h.ref);

	call_rcu(&rsii->rcu_head, rsi_free_rcu);
}

static inline int rsi_hash(struct rsi *item)
{
	return hash_mem(item->in_handle.data, item->in_handle.len, RSI_HASHBITS)
	     ^ hash_mem(item->in_token.data, item->in_token.len, RSI_HASHBITS);
}

static int rsi_match(struct cache_head *a, struct cache_head *b)
{
	struct rsi *item = container_of(a, struct rsi, h);
	struct rsi *tmp = container_of(b, struct rsi, h);
	return netobj_equal(&item->in_handle, &tmp->in_handle) &&
	       netobj_equal(&item->in_token, &tmp->in_token);
}

static int dup_to_netobj(struct xdr_netobj *dst, char *src, int len)
{
	dst->len = len;
	dst->data = (len ? kmemdup(src, len, GFP_KERNEL) : NULL);
	if (len && !dst->data)
		return -ENOMEM;
	return 0;
}

static inline int dup_netobj(struct xdr_netobj *dst, struct xdr_netobj *src)
{
	return dup_to_netobj(dst, src->data, src->len);
}

static void rsi_init(struct cache_head *cnew, struct cache_head *citem)
{
	struct rsi *new = container_of(cnew, struct rsi, h);
	struct rsi *item = container_of(citem, struct rsi, h);

	new->out_handle.data = NULL;
	new->out_handle.len = 0;
	new->out_token.data = NULL;
	new->out_token.len = 0;
	new->in_handle.len = item->in_handle.len;
	item->in_handle.len = 0;
	new->in_token.len = item->in_token.len;
	item->in_token.len = 0;
	new->in_handle.data = item->in_handle.data;
	item->in_handle.data = NULL;
	new->in_token.data = item->in_token.data;
	item->in_token.data = NULL;
}

static void update_rsi(struct cache_head *cnew, struct cache_head *citem)
{
	struct rsi *new = container_of(cnew, struct rsi, h);
	struct rsi *item = container_of(citem, struct rsi, h);

	BUG_ON(new->out_handle.data || new->out_token.data);
	new->out_handle.len = item->out_handle.len;
	item->out_handle.len = 0;
	new->out_token.len = item->out_token.len;
	item->out_token.len = 0;
	new->out_handle.data = item->out_handle.data;
	item->out_handle.data = NULL;
	new->out_token.data = item->out_token.data;
	item->out_token.data = NULL;

	new->major_status = item->major_status;
	new->minor_status = item->minor_status;
}

static struct cache_head *rsi_alloc(void)
{
	struct rsi *rsii = kmalloc(sizeof(*rsii), GFP_KERNEL);
	if (rsii)
		return &rsii->h;
	else
		return NULL;
}

static int rsi_upcall(struct cache_detail *cd, struct cache_head *h)
{
	return sunrpc_cache_pipe_upcall_timeout(cd, h);
}

static void rsi_request(struct cache_detail *cd,
		       struct cache_head *h,
		       char **bpp, int *blen)
{
	struct rsi *rsii = container_of(h, struct rsi, h);

	qword_addhex(bpp, blen, rsii->in_handle.data, rsii->in_handle.len);
	qword_addhex(bpp, blen, rsii->in_token.data, rsii->in_token.len);
	(*bpp)[-1] = '\n';
}

static int rsi_parse(struct cache_detail *cd,
		    char *mesg, int mlen)
{
	/* context token expiry major minor context token */
	char *buf = mesg;
	char *ep;
	int len;
	struct rsi rsii, *rsip = NULL;
	time64_t expiry;
	int status = -EINVAL;

	memset(&rsii, 0, sizeof(rsii));
	/* handle */
	len = qword_get(&mesg, buf, mlen);
	if (len < 0)
		goto out;
	status = -ENOMEM;
	if (dup_to_netobj(&rsii.in_handle, buf, len))
		goto out;

	/* token */
	len = qword_get(&mesg, buf, mlen);
	status = -EINVAL;
	if (len < 0)
		goto out;
	status = -ENOMEM;
	if (dup_to_netobj(&rsii.in_token, buf, len))
		goto out;

	rsip = rsi_lookup(cd, &rsii);
	if (!rsip)
		goto out;

	rsii.h.flags = 0;
	/* expiry */
	expiry = get_expiry(&mesg);
	status = -EINVAL;
	if (expiry == 0)
		goto out;

	/* major/minor */
	len = qword_get(&mesg, buf, mlen);
	if (len <= 0)
		goto out;
	rsii.major_status = simple_strtoul(buf, &ep, 10);
	if (*ep)
		goto out;
	len = qword_get(&mesg, buf, mlen);
	if (len <= 0)
		goto out;
	rsii.minor_status = simple_strtoul(buf, &ep, 10);
	if (*ep)
		goto out;

	/* out_handle */
	len = qword_get(&mesg, buf, mlen);
	if (len < 0)
		goto out;
	status = -ENOMEM;
	if (dup_to_netobj(&rsii.out_handle, buf, len))
		goto out;

	/* out_token */
	len = qword_get(&mesg, buf, mlen);
	status = -EINVAL;
	if (len < 0)
		goto out;
	status = -ENOMEM;
	if (dup_to_netobj(&rsii.out_token, buf, len))
		goto out;
	rsii.h.expiry_time = expiry;
	rsip = rsi_update(cd, &rsii, rsip);
	status = 0;
out:
	rsi_free(&rsii);
	if (rsip)
		cache_put(&rsip->h, cd);
	else
		status = -ENOMEM;
	return status;
}

static const struct cache_detail rsi_cache_template = {
	.owner		= THIS_MODULE,
	.hash_size	= RSI_HASHMAX,
	.name           = "auth.rpcsec.init",
	.cache_put      = rsi_put,
	.cache_upcall	= rsi_upcall,
	.cache_request  = rsi_request,
	.cache_parse    = rsi_parse,
	.match		= rsi_match,
	.init		= rsi_init,
	.update		= update_rsi,
	.alloc		= rsi_alloc,
};

static struct rsi *rsi_lookup(struct cache_detail *cd, struct rsi *item)
{
	struct cache_head *ch;
	int hash = rsi_hash(item);

	ch = sunrpc_cache_lookup_rcu(cd, &item->h, hash);
	if (ch)
		return container_of(ch, struct rsi, h);
	else
		return NULL;
}

static struct rsi *rsi_update(struct cache_detail *cd, struct rsi *new, struct rsi *old)
{
	struct cache_head *ch;
	int hash = rsi_hash(new);

	ch = sunrpc_cache_update(cd, &new->h,
				 &old->h, hash);
	if (ch)
		return container_of(ch, struct rsi, h);
	else
		return NULL;
}


/*
 * The rpcsec_context cache is used to store a context that is
 * used in data exchange.
 * The key is a context handle. The content is:
 *  uid, gidlist, mechanism, service-set, mech-specific-data
 */

#define	RSC_HASHBITS	10
#define	RSC_HASHMAX	(1<<RSC_HASHBITS)

#define GSS_SEQ_WIN	128

struct gss_svc_seq_data {
	/* highest seq number seen so far: */
	u32			sd_max;
	/* for i such that sd_max-GSS_SEQ_WIN < i <= sd_max, the i-th bit of
	 * sd_win is nonzero iff sequence number i has been seen already: */
	unsigned long		sd_win[GSS_SEQ_WIN/BITS_PER_LONG];
	spinlock_t		sd_lock;
};

struct rsc {
	struct cache_head	h;
	struct xdr_netobj	handle;
	struct svc_cred		cred;
	struct gss_svc_seq_data	seqdata;
	struct gss_ctx		*mechctx;
	struct rcu_head		rcu_head;
};

static struct rsc *rsc_update(struct cache_detail *cd, struct rsc *new, struct rsc *old);
static struct rsc *rsc_lookup(struct cache_detail *cd, struct rsc *item);

static void rsc_free(struct rsc *rsci)
{
	kfree(rsci->handle.data);
	if (rsci->mechctx)
		gss_delete_sec_context(&rsci->mechctx);
	free_svc_cred(&rsci->cred);
}

static void rsc_free_rcu(struct rcu_head *head)
{
	struct rsc *rsci = container_of(head, struct rsc, rcu_head);

	kfree(rsci->handle.data);
	kfree(rsci);
}

static void rsc_put(struct kref *ref)
{
	struct rsc *rsci = container_of(ref, struct rsc, h.ref);

	if (rsci->mechctx)
		gss_delete_sec_context(&rsci->mechctx);
	free_svc_cred(&rsci->cred);
	call_rcu(&rsci->rcu_head, rsc_free_rcu);
}

static inline int
rsc_hash(struct rsc *rsci)
{
	return hash_mem(rsci->handle.data, rsci->handle.len, RSC_HASHBITS);
}

static int
rsc_match(struct cache_head *a, struct cache_head *b)
{
	struct rsc *new = container_of(a, struct rsc, h);
	struct rsc *tmp = container_of(b, struct rsc, h);

	return netobj_equal(&new->handle, &tmp->handle);
}

static void
rsc_init(struct cache_head *cnew, struct cache_head *ctmp)
{
	struct rsc *new = container_of(cnew, struct rsc, h);
	struct rsc *tmp = container_of(ctmp, struct rsc, h);

	new->handle.len = tmp->handle.len;
	tmp->handle.len = 0;
	new->handle.data = tmp->handle.data;
	tmp->handle.data = NULL;
	new->mechctx = NULL;
	init_svc_cred(&new->cred);
}

static void
update_rsc(struct cache_head *cnew, struct cache_head *ctmp)
{
	struct rsc *new = container_of(cnew, struct rsc, h);
	struct rsc *tmp = container_of(ctmp, struct rsc, h);

	new->mechctx = tmp->mechctx;
	tmp->mechctx = NULL;
	memset(&new->seqdata, 0, sizeof(new->seqdata));
	spin_lock_init(&new->seqdata.sd_lock);
	new->cred = tmp->cred;
	init_svc_cred(&tmp->cred);
}

static struct cache_head *
rsc_alloc(void)
{
	struct rsc *rsci = kmalloc(sizeof(*rsci), GFP_KERNEL);
	if (rsci)
		return &rsci->h;
	else
		return NULL;
}

static int rsc_upcall(struct cache_detail *cd, struct cache_head *h)
{
	return -EINVAL;
}

static int rsc_parse(struct cache_detail *cd,
		     char *mesg, int mlen)
{
	/* contexthandle expiry [ uid gid N <n gids> mechname ...mechdata... ] */
	char *buf = mesg;
	int id;
	int len, rv;
	struct rsc rsci, *rscp = NULL;
	time64_t expiry;
	int status = -EINVAL;
	struct gss_api_mech *gm = NULL;

	memset(&rsci, 0, sizeof(rsci));
	/* context handle */
	len = qword_get(&mesg, buf, mlen);
	if (len < 0) goto out;
	status = -ENOMEM;
	if (dup_to_netobj(&rsci.handle, buf, len))
		goto out;

	rsci.h.flags = 0;
	/* expiry */
	expiry = get_expiry(&mesg);
	status = -EINVAL;
	if (expiry == 0)
		goto out;

	rscp = rsc_lookup(cd, &rsci);
	if (!rscp)
		goto out;

	/* uid, or NEGATIVE */
	rv = get_int(&mesg, &id);
	if (rv == -EINVAL)
		goto out;
	if (rv == -ENOENT)
		set_bit(CACHE_NEGATIVE, &rsci.h.flags);
	else {
		int N, i;

		/*
		 * NOTE: we skip uid_valid()/gid_valid() checks here:
		 * instead, * -1 id's are later mapped to the
		 * (export-specific) anonymous id by nfsd_setuser.
		 *
		 * (But supplementary gid's get no such special
		 * treatment so are checked for validity here.)
		 */
		/* uid */
		rsci.cred.cr_uid = make_kuid(current_user_ns(), id);

		/* gid */
		if (get_int(&mesg, &id))
			goto out;
		rsci.cred.cr_gid = make_kgid(current_user_ns(), id);

		/* number of additional gid's */
		if (get_int(&mesg, &N))
			goto out;
		if (N < 0 || N > NGROUPS_MAX)
			goto out;
		status = -ENOMEM;
		rsci.cred.cr_group_info = groups_alloc(N);
		if (rsci.cred.cr_group_info == NULL)
			goto out;

		/* gid's */
		status = -EINVAL;
		for (i=0; i<N; i++) {
			kgid_t kgid;
			if (get_int(&mesg, &id))
				goto out;
			kgid = make_kgid(current_user_ns(), id);
			if (!gid_valid(kgid))
				goto out;
			rsci.cred.cr_group_info->gid[i] = kgid;
		}
		groups_sort(rsci.cred.cr_group_info);

		/* mech name */
		len = qword_get(&mesg, buf, mlen);
		if (len < 0)
			goto out;
		gm = rsci.cred.cr_gss_mech = gss_mech_get_by_name(buf);
		status = -EOPNOTSUPP;
		if (!gm)
			goto out;

		status = -EINVAL;
		/* mech-specific data: */
		len = qword_get(&mesg, buf, mlen);
		if (len < 0)
			goto out;
		status = gss_import_sec_context(buf, len, gm, &rsci.mechctx,
						NULL, GFP_KERNEL);
		if (status)
			goto out;

		/* get client name */
		len = qword_get(&mesg, buf, mlen);
		if (len > 0) {
			rsci.cred.cr_principal = kstrdup(buf, GFP_KERNEL);
			if (!rsci.cred.cr_principal) {
				status = -ENOMEM;
				goto out;
			}
		}

	}
	rsci.h.expiry_time = expiry;
	rscp = rsc_update(cd, &rsci, rscp);
	status = 0;
out:
	rsc_free(&rsci);
	if (rscp)
		cache_put(&rscp->h, cd);
	else
		status = -ENOMEM;
	return status;
}

static const struct cache_detail rsc_cache_template = {
	.owner		= THIS_MODULE,
	.hash_size	= RSC_HASHMAX,
	.name		= "auth.rpcsec.context",
	.cache_put	= rsc_put,
	.cache_upcall	= rsc_upcall,
	.cache_parse	= rsc_parse,
	.match		= rsc_match,
	.init		= rsc_init,
	.update		= update_rsc,
	.alloc		= rsc_alloc,
};

static struct rsc *rsc_lookup(struct cache_detail *cd, struct rsc *item)
{
	struct cache_head *ch;
	int hash = rsc_hash(item);

	ch = sunrpc_cache_lookup_rcu(cd, &item->h, hash);
	if (ch)
		return container_of(ch, struct rsc, h);
	else
		return NULL;
}

static struct rsc *rsc_update(struct cache_detail *cd, struct rsc *new, struct rsc *old)
{
	struct cache_head *ch;
	int hash = rsc_hash(new);

	ch = sunrpc_cache_update(cd, &new->h,
				 &old->h, hash);
	if (ch)
		return container_of(ch, struct rsc, h);
	else
		return NULL;
}


static struct rsc *
gss_svc_searchbyctx(struct cache_detail *cd, struct xdr_netobj *handle)
{
	struct rsc rsci;
	struct rsc *found;

	memset(&rsci, 0, sizeof(rsci));
	if (dup_to_netobj(&rsci.handle, handle->data, handle->len))
		return NULL;
	found = rsc_lookup(cd, &rsci);
	rsc_free(&rsci);
	if (!found)
		return NULL;
	if (cache_check(cd, &found->h, NULL))
		return NULL;
	return found;
}

/**
 * gss_check_seq_num - GSS sequence number window check
 * @rqstp: RPC Call to use when reporting errors
 * @rsci: cached GSS context state (updated on return)
 * @seq_num: sequence number to check
 *
 * Implements sequence number algorithm as specified in
 * RFC 2203, Section 5.3.3.1. "Context Management".
 *
 * Return values:
 *   %true: @rqstp's GSS sequence number is inside the window
 *   %false: @rqstp's GSS sequence number is outside the window
 */
static bool gss_check_seq_num(const struct svc_rqst *rqstp, struct rsc *rsci,
			      u32 seq_num)
{
	struct gss_svc_seq_data *sd = &rsci->seqdata;
	bool result = false;

	spin_lock(&sd->sd_lock);
	if (seq_num > sd->sd_max) {
		if (seq_num >= sd->sd_max + GSS_SEQ_WIN) {
			memset(sd->sd_win, 0, sizeof(sd->sd_win));
			sd->sd_max = seq_num;
		} else while (sd->sd_max < seq_num) {
			sd->sd_max++;
			__clear_bit(sd->sd_max % GSS_SEQ_WIN, sd->sd_win);
		}
		__set_bit(seq_num % GSS_SEQ_WIN, sd->sd_win);
		goto ok;
	} else if (seq_num <= sd->sd_max - GSS_SEQ_WIN) {
		goto toolow;
	}
	if (__test_and_set_bit(seq_num % GSS_SEQ_WIN, sd->sd_win))
		goto alreadyseen;

ok:
	result = true;
out:
	spin_unlock(&sd->sd_lock);
	return result;

toolow:
	trace_rpcgss_svc_seqno_low(rqstp, seq_num,
				   sd->sd_max - GSS_SEQ_WIN,
				   sd->sd_max);
	goto out;
alreadyseen:
	trace_rpcgss_svc_seqno_seen(rqstp, seq_num);
	goto out;
}

static inline u32 round_up_to_quad(u32 i)
{
	return (i + 3 ) & ~3;
}

static inline int
svc_safe_getnetobj(struct kvec *argv, struct xdr_netobj *o)
{
	int l;

	if (argv->iov_len < 4)
		return -1;
	o->len = svc_getnl(argv);
	l = round_up_to_quad(o->len);
	if (argv->iov_len < l)
		return -1;
	o->data = argv->iov_base;
	argv->iov_base += l;
	argv->iov_len -= l;
	return 0;
}

static inline int
svc_safe_putnetobj(struct kvec *resv, struct xdr_netobj *o)
{
	u8 *p;

	if (resv->iov_len + 4 > PAGE_SIZE)
		return -1;
	svc_putnl(resv, o->len);
	p = resv->iov_base + resv->iov_len;
	resv->iov_len += round_up_to_quad(o->len);
	if (resv->iov_len > PAGE_SIZE)
		return -1;
	memcpy(p, o->data, o->len);
	memset(p + o->len, 0, round_up_to_quad(o->len) - o->len);
	return 0;
}

/*
 * Verify the checksum on the header and return SVC_OK on success.
 * Otherwise, return SVC_DROP (in the case of a bad sequence number)
 * or return SVC_DENIED and indicate error in authp.
 */
static int
gss_verify_header(struct svc_rqst *rqstp, struct rsc *rsci,
		  __be32 *rpcstart, struct rpc_gss_wire_cred *gc, __be32 *authp)
{
	struct gss_ctx		*ctx_id = rsci->mechctx;
	struct xdr_buf		rpchdr;
	struct xdr_netobj	checksum;
	u32			flavor = 0;
	struct kvec		*argv = &rqstp->rq_arg.head[0];
	struct kvec		iov;

	/* data to compute the checksum over: */
	iov.iov_base = rpcstart;
	iov.iov_len = (u8 *)argv->iov_base - (u8 *)rpcstart;
	xdr_buf_from_iov(&iov, &rpchdr);

	*authp = rpc_autherr_badverf;
	if (argv->iov_len < 4)
		return SVC_DENIED;
	flavor = svc_getnl(argv);
	if (flavor != RPC_AUTH_GSS)
		return SVC_DENIED;
	if (svc_safe_getnetobj(argv, &checksum))
		return SVC_DENIED;

	if (rqstp->rq_deferred) /* skip verification of revisited request */
		return SVC_OK;
	if (gss_verify_mic(ctx_id, &rpchdr, &checksum) != GSS_S_COMPLETE) {
		*authp = rpcsec_gsserr_credproblem;
		return SVC_DENIED;
	}

	if (gc->gc_seq > MAXSEQ) {
		trace_rpcgss_svc_seqno_large(rqstp, gc->gc_seq);
		*authp = rpcsec_gsserr_ctxproblem;
		return SVC_DENIED;
	}
	if (!gss_check_seq_num(rqstp, rsci, gc->gc_seq))
		return SVC_DROP;
	return SVC_OK;
}

static int
gss_write_null_verf(struct svc_rqst *rqstp)
{
	__be32     *p;

	svc_putnl(rqstp->rq_res.head, RPC_AUTH_NULL);
	p = rqstp->rq_res.head->iov_base + rqstp->rq_res.head->iov_len;
	/* don't really need to check if head->iov_len > PAGE_SIZE ... */
	*p++ = 0;
	if (!xdr_ressize_check(rqstp, p))
		return -1;
	return 0;
}

static int
gss_write_verf(struct svc_rqst *rqstp, struct gss_ctx *ctx_id, u32 seq)
{
	__be32			*xdr_seq;
	u32			maj_stat;
	struct xdr_buf		verf_data;
	struct xdr_netobj	mic;
	__be32			*p;
	struct kvec		iov;
	int err = -1;

	svc_putnl(rqstp->rq_res.head, RPC_AUTH_GSS);
	xdr_seq = kmalloc(4, GFP_KERNEL);
	if (!xdr_seq)
		return -1;
	*xdr_seq = htonl(seq);

	iov.iov_base = xdr_seq;
	iov.iov_len = 4;
	xdr_buf_from_iov(&iov, &verf_data);
	p = rqstp->rq_res.head->iov_base + rqstp->rq_res.head->iov_len;
	mic.data = (u8 *)(p + 1);
	maj_stat = gss_get_mic(ctx_id, &verf_data, &mic);
	if (maj_stat != GSS_S_COMPLETE)
		goto out;
	*p++ = htonl(mic.len);
	memset((u8 *)p + mic.len, 0, round_up_to_quad(mic.len) - mic.len);
	p += XDR_QUADLEN(mic.len);
	if (!xdr_ressize_check(rqstp, p))
		goto out;
	err = 0;
out:
	kfree(xdr_seq);
	return err;
}

struct gss_domain {
	struct auth_domain	h;
	u32			pseudoflavor;
};

static struct auth_domain *
find_gss_auth_domain(struct gss_ctx *ctx, u32 svc)
{
	char *name;

	name = gss_service_to_auth_domain_name(ctx->mech_type, svc);
	if (!name)
		return NULL;
	return auth_domain_find(name);
}

static struct auth_ops svcauthops_gss;

u32 svcauth_gss_flavor(struct auth_domain *dom)
{
	struct gss_domain *gd = container_of(dom, struct gss_domain, h);

	return gd->pseudoflavor;
}

EXPORT_SYMBOL_GPL(svcauth_gss_flavor);

struct auth_domain *
svcauth_gss_register_pseudoflavor(u32 pseudoflavor, char * name)
{
	struct gss_domain	*new;
	struct auth_domain	*test;
	int			stat = -ENOMEM;

	new = kmalloc(sizeof(*new), GFP_KERNEL);
	if (!new)
		goto out;
	kref_init(&new->h.ref);
	new->h.name = kstrdup(name, GFP_KERNEL);
	if (!new->h.name)
		goto out_free_dom;
	new->h.flavour = &svcauthops_gss;
	new->pseudoflavor = pseudoflavor;

	test = auth_domain_lookup(name, &new->h);
	if (test != &new->h) {
		pr_warn("svc: duplicate registration of gss pseudo flavour %s.\n",
			name);
		stat = -EADDRINUSE;
		auth_domain_put(test);
		goto out_free_name;
	}
	return test;

out_free_name:
	kfree(new->h.name);
out_free_dom:
	kfree(new);
out:
	return ERR_PTR(stat);
}
EXPORT_SYMBOL_GPL(svcauth_gss_register_pseudoflavor);

static inline int
read_u32_from_xdr_buf(struct xdr_buf *buf, int base, u32 *obj)
{
	__be32  raw;
	int     status;

	status = read_bytes_from_xdr_buf(buf, base, &raw, sizeof(*obj));
	if (status)
		return status;
	*obj = ntohl(raw);
	return 0;
}

/* It would be nice if this bit of code could be shared with the client.
 * Obstacles:
 *	The client shouldn't malloc(), would have to pass in own memory.
 *	The server uses base of head iovec as read pointer, while the
 *	client uses separate pointer. */
static int
unwrap_integ_data(struct svc_rqst *rqstp, struct xdr_buf *buf, u32 seq, struct gss_ctx *ctx)
{
	u32 integ_len, rseqno, maj_stat;
	int stat = -EINVAL;
	struct xdr_netobj mic;
	struct xdr_buf integ_buf;

	mic.data = NULL;

	/* NFS READ normally uses splice to send data in-place. However
	 * the data in cache can change after the reply's MIC is computed
	 * but before the RPC reply is sent. To prevent the client from
	 * rejecting the server-computed MIC in this somewhat rare case,
	 * do not use splice with the GSS integrity service.
	 */
	clear_bit(RQ_SPLICE_OK, &rqstp->rq_flags);

	/* Did we already verify the signature on the original pass through? */
	if (rqstp->rq_deferred)
		return 0;

	integ_len = svc_getnl(&buf->head[0]);
	if (integ_len & 3)
		goto unwrap_failed;
	if (integ_len > buf->len)
		goto unwrap_failed;
	if (xdr_buf_subsegment(buf, &integ_buf, 0, integ_len))
		goto unwrap_failed;

	/* copy out mic... */
	if (read_u32_from_xdr_buf(buf, integ_len, &mic.len))
		goto unwrap_failed;
	if (mic.len > RPC_MAX_AUTH_SIZE)
		goto unwrap_failed;
	mic.data = kmalloc(mic.len, GFP_KERNEL);
	if (!mic.data)
		goto unwrap_failed;
	if (read_bytes_from_xdr_buf(buf, integ_len + 4, mic.data, mic.len))
		goto unwrap_failed;
	maj_stat = gss_verify_mic(ctx, &integ_buf, &mic);
	if (maj_stat != GSS_S_COMPLETE)
		goto bad_mic;
	rseqno = svc_getnl(&buf->head[0]);
	if (rseqno != seq)
		goto bad_seqno;
	/* trim off the mic and padding at the end before returning */
	xdr_buf_trim(buf, round_up_to_quad(mic.len) + 4);
	stat = 0;
out:
	kfree(mic.data);
	return stat;

unwrap_failed:
	trace_rpcgss_svc_unwrap_failed(rqstp);
	goto out;
bad_seqno:
	trace_rpcgss_svc_seqno_bad(rqstp, seq, rseqno);
	goto out;
bad_mic:
	trace_rpcgss_svc_mic(rqstp, maj_stat);
	goto out;
}

static inline int
total_buf_len(struct xdr_buf *buf)
{
	return buf->head[0].iov_len + buf->page_len + buf->tail[0].iov_len;
}

static void
fix_priv_head(struct xdr_buf *buf, int pad)
{
	if (buf->page_len == 0) {
		/* We need to adjust head and buf->len in tandem in this
		 * case to make svc_defer() work--it finds the original
		 * buffer start using buf->len - buf->head[0].iov_len. */
		buf->head[0].iov_len -= pad;
	}
}

static int
unwrap_priv_data(struct svc_rqst *rqstp, struct xdr_buf *buf, u32 seq, struct gss_ctx *ctx)
{
	u32 priv_len, maj_stat;
	int pad, remaining_len, offset;
	u32 rseqno;

	clear_bit(RQ_SPLICE_OK, &rqstp->rq_flags);

	priv_len = svc_getnl(&buf->head[0]);
	if (rqstp->rq_deferred) {
		/* Already decrypted last time through! The sequence number
		 * check at out_seq is unnecessary but harmless: */
		goto out_seq;
	}
	/* buf->len is the number of bytes from the original start of the
	 * request to the end, where head[0].iov_len is just the bytes
	 * not yet read from the head, so these two values are different: */
	remaining_len = total_buf_len(buf);
	if (priv_len > remaining_len)
		goto unwrap_failed;
	pad = remaining_len - priv_len;
	buf->len -= pad;
	fix_priv_head(buf, pad);

	maj_stat = gss_unwrap(ctx, 0, priv_len, buf);
	pad = priv_len - buf->len;
	/* The upper layers assume the buffer is aligned on 4-byte boundaries.
	 * In the krb5p case, at least, the data ends up offset, so we need to
	 * move it around. */
	/* XXX: This is very inefficient.  It would be better to either do
	 * this while we encrypt, or maybe in the receive code, if we can peak
	 * ahead and work out the service and mechanism there. */
	offset = xdr_pad_size(buf->head[0].iov_len);
	if (offset) {
		buf->buflen = RPCSVC_MAXPAYLOAD;
		xdr_shift_buf(buf, offset);
		fix_priv_head(buf, pad);
	}
	if (maj_stat != GSS_S_COMPLETE)
		goto bad_unwrap;
out_seq:
	rseqno = svc_getnl(&buf->head[0]);
	if (rseqno != seq)
		goto bad_seqno;
	return 0;

unwrap_failed:
	trace_rpcgss_svc_unwrap_failed(rqstp);
	return -EINVAL;
bad_seqno:
	trace_rpcgss_svc_seqno_bad(rqstp, seq, rseqno);
	return -EINVAL;
bad_unwrap:
	trace_rpcgss_svc_unwrap(rqstp, maj_stat);
	return -EINVAL;
}

struct gss_svc_data {
	/* decoded gss client cred: */
	struct rpc_gss_wire_cred	clcred;
	/* save a pointer to the beginning of the encoded verifier,
	 * for use in encryption/checksumming in svcauth_gss_release: */
	__be32				*verf_start;
	struct rsc			*rsci;
};

static int
svcauth_gss_set_client(struct svc_rqst *rqstp)
{
	struct gss_svc_data *svcdata = rqstp->rq_auth_data;
	struct rsc *rsci = svcdata->rsci;
	struct rpc_gss_wire_cred *gc = &svcdata->clcred;
	int stat;

	/*
	 * A gss export can be specified either by:
	 * 	export	*(sec=krb5,rw)
	 * or by
	 * 	export gss/krb5(rw)
	 * The latter is deprecated; but for backwards compatibility reasons
	 * the nfsd code will still fall back on trying it if the former
	 * doesn't work; so we try to make both available to nfsd, below.
	 */
	rqstp->rq_gssclient = find_gss_auth_domain(rsci->mechctx, gc->gc_svc);
	if (rqstp->rq_gssclient == NULL)
		return SVC_DENIED;
	stat = svcauth_unix_set_client(rqstp);
	if (stat == SVC_DROP || stat == SVC_CLOSE)
		return stat;
	return SVC_OK;
}

static inline int
gss_write_init_verf(struct cache_detail *cd, struct svc_rqst *rqstp,
		struct xdr_netobj *out_handle, int *major_status)
{
	struct rsc *rsci;
	int        rc;

	if (*major_status != GSS_S_COMPLETE)
		return gss_write_null_verf(rqstp);
	rsci = gss_svc_searchbyctx(cd, out_handle);
	if (rsci == NULL) {
		*major_status = GSS_S_NO_CONTEXT;
		return gss_write_null_verf(rqstp);
	}
	rc = gss_write_verf(rqstp, rsci->mechctx, GSS_SEQ_WIN);
	cache_put(&rsci->h, cd);
	return rc;
}

static inline int
gss_read_common_verf(struct rpc_gss_wire_cred *gc,
		     struct kvec *argv, __be32 *authp,
		     struct xdr_netobj *in_handle)
{
	/* Read the verifier; should be NULL: */
	*authp = rpc_autherr_badverf;
	if (argv->iov_len < 2 * 4)
		return SVC_DENIED;
	if (svc_getnl(argv) != RPC_AUTH_NULL)
		return SVC_DENIED;
	if (svc_getnl(argv) != 0)
		return SVC_DENIED;
	/* Martial context handle and token for upcall: */
	*authp = rpc_autherr_badcred;
	if (gc->gc_proc == RPC_GSS_PROC_INIT && gc->gc_ctx.len != 0)
		return SVC_DENIED;
	if (dup_netobj(in_handle, &gc->gc_ctx))
		return SVC_CLOSE;
	*authp = rpc_autherr_badverf;

	return 0;
}

static inline int
gss_read_verf(struct rpc_gss_wire_cred *gc,
	      struct kvec *argv, __be32 *authp,
	      struct xdr_netobj *in_handle,
	      struct xdr_netobj *in_token)
{
	struct xdr_netobj tmpobj;
	int res;

	res = gss_read_common_verf(gc, argv, authp, in_handle);
	if (res)
		return res;

	if (svc_safe_getnetobj(argv, &tmpobj)) {
		kfree(in_handle->data);
		return SVC_DENIED;
	}
	if (dup_netobj(in_token, &tmpobj)) {
		kfree(in_handle->data);
		return SVC_CLOSE;
	}

	return 0;
}

static void gss_free_in_token_pages(struct gssp_in_token *in_token)
{
	u32 inlen;
	int i;

	i = 0;
	inlen = in_token->page_len;
	while (inlen) {
		if (in_token->pages[i])
			put_page(in_token->pages[i]);
		inlen -= inlen > PAGE_SIZE ? PAGE_SIZE : inlen;
	}

	kfree(in_token->pages);
	in_token->pages = NULL;
}

static int gss_read_proxy_verf(struct svc_rqst *rqstp,
			       struct rpc_gss_wire_cred *gc, __be32 *authp,
			       struct xdr_netobj *in_handle,
			       struct gssp_in_token *in_token)
{
	struct kvec *argv = &rqstp->rq_arg.head[0];
	unsigned int length, pgto_offs, pgfrom_offs;
	int pages, i, res, pgto, pgfrom;
	size_t inlen, to_offs, from_offs;

	res = gss_read_common_verf(gc, argv, authp, in_handle);
	if (res)
		return res;

	inlen = svc_getnl(argv);
	if (inlen > (argv->iov_len + rqstp->rq_arg.page_len))
		return SVC_DENIED;

	pages = DIV_ROUND_UP(inlen, PAGE_SIZE);
	in_token->pages = kcalloc(pages, sizeof(struct page *), GFP_KERNEL);
	if (!in_token->pages)
		return SVC_DENIED;
	in_token->page_base = 0;
	in_token->page_len = inlen;
	for (i = 0; i < pages; i++) {
		in_token->pages[i] = alloc_page(GFP_KERNEL);
		if (!in_token->pages[i]) {
			gss_free_in_token_pages(in_token);
			return SVC_DENIED;
		}
	}

	length = min_t(unsigned int, inlen, argv->iov_len);
	memcpy(page_address(in_token->pages[0]), argv->iov_base, length);
	inlen -= length;

	to_offs = length;
	from_offs = rqstp->rq_arg.page_base;
	while (inlen) {
		pgto = to_offs >> PAGE_SHIFT;
		pgfrom = from_offs >> PAGE_SHIFT;
		pgto_offs = to_offs & ~PAGE_MASK;
		pgfrom_offs = from_offs & ~PAGE_MASK;

		length = min_t(unsigned int, inlen,
			 min_t(unsigned int, PAGE_SIZE - pgto_offs,
			       PAGE_SIZE - pgfrom_offs));
		memcpy(page_address(in_token->pages[pgto]) + pgto_offs,
		       page_address(rqstp->rq_arg.pages[pgfrom]) + pgfrom_offs,
		       length);

		to_offs += length;
		from_offs += length;
		inlen -= length;
	}
	return 0;
}

static inline int
gss_write_resv(struct kvec *resv, size_t size_limit,
	       struct xdr_netobj *out_handle, struct xdr_netobj *out_token,
	       int major_status, int minor_status)
{
	if (resv->iov_len + 4 > size_limit)
		return -1;
	svc_putnl(resv, RPC_SUCCESS);
	if (svc_safe_putnetobj(resv, out_handle))
		return -1;
	if (resv->iov_len + 3 * 4 > size_limit)
		return -1;
	svc_putnl(resv, major_status);
	svc_putnl(resv, minor_status);
	svc_putnl(resv, GSS_SEQ_WIN);
	if (svc_safe_putnetobj(resv, out_token))
		return -1;
	return 0;
}

/*
 * Having read the cred already and found we're in the context
 * initiation case, read the verifier and initiate (or check the results
 * of) upcalls to userspace for help with context initiation.  If
 * the upcall results are available, write the verifier and result.
 * Otherwise, drop the request pending an answer to the upcall.
 */
static int svcauth_gss_legacy_init(struct svc_rqst *rqstp,
			struct rpc_gss_wire_cred *gc, __be32 *authp)
{
	struct kvec *argv = &rqstp->rq_arg.head[0];
	struct kvec *resv = &rqstp->rq_res.head[0];
	struct rsi *rsip, rsikey;
	int ret;
	struct sunrpc_net *sn = net_generic(SVC_NET(rqstp), sunrpc_net_id);

	memset(&rsikey, 0, sizeof(rsikey));
	ret = gss_read_verf(gc, argv, authp,
			    &rsikey.in_handle, &rsikey.in_token);
	if (ret)
		return ret;

	/* Perform upcall, or find upcall result: */
	rsip = rsi_lookup(sn->rsi_cache, &rsikey);
	rsi_free(&rsikey);
	if (!rsip)
		return SVC_CLOSE;
	if (cache_check(sn->rsi_cache, &rsip->h, &rqstp->rq_chandle) < 0)
		/* No upcall result: */
		return SVC_CLOSE;

	ret = SVC_CLOSE;
	/* Got an answer to the upcall; use it: */
	if (gss_write_init_verf(sn->rsc_cache, rqstp,
				&rsip->out_handle, &rsip->major_status))
		goto out;
	if (gss_write_resv(resv, PAGE_SIZE,
			   &rsip->out_handle, &rsip->out_token,
			   rsip->major_status, rsip->minor_status))
		goto out;

	ret = SVC_COMPLETE;
out:
	cache_put(&rsip->h, sn->rsi_cache);
	return ret;
}

static int gss_proxy_save_rsc(struct cache_detail *cd,
				struct gssp_upcall_data *ud,
				uint64_t *handle)
{
	struct rsc rsci, *rscp = NULL;
	static atomic64_t ctxhctr;
	long long ctxh;
	struct gss_api_mech *gm = NULL;
	time64_t expiry;
	int status = -EINVAL;

	memset(&rsci, 0, sizeof(rsci));
	/* context handle */
	status = -ENOMEM;
	/* the handle needs to be just a unique id,
	 * use a static counter */
	ctxh = atomic64_inc_return(&ctxhctr);

	/* make a copy for the caller */
	*handle = ctxh;

	/* make a copy for the rsc cache */
	if (dup_to_netobj(&rsci.handle, (char *)handle, sizeof(uint64_t)))
		goto out;
	rscp = rsc_lookup(cd, &rsci);
	if (!rscp)
		goto out;

	/* creds */
	if (!ud->found_creds) {
		/* userspace seem buggy, we should always get at least a
		 * mapping to nobody */
		goto out;
	} else {
		struct timespec64 boot;

		/* steal creds */
		rsci.cred = ud->creds;
		memset(&ud->creds, 0, sizeof(struct svc_cred));

		status = -EOPNOTSUPP;
		/* get mech handle from OID */
		gm = gss_mech_get_by_OID(&ud->mech_oid);
		if (!gm)
			goto out;
		rsci.cred.cr_gss_mech = gm;

		status = -EINVAL;
		/* mech-specific data: */
		status = gss_import_sec_context(ud->out_handle.data,
						ud->out_handle.len,
						gm, &rsci.mechctx,
						&expiry, GFP_KERNEL);
		if (status)
			goto out;

		getboottime64(&boot);
		expiry -= boot.tv_sec;
	}

	rsci.h.expiry_time = expiry;
	rscp = rsc_update(cd, &rsci, rscp);
	status = 0;
out:
	rsc_free(&rsci);
	if (rscp)
		cache_put(&rscp->h, cd);
	else
		status = -ENOMEM;
	return status;
}

static int svcauth_gss_proxy_init(struct svc_rqst *rqstp,
			struct rpc_gss_wire_cred *gc, __be32 *authp)
{
	struct kvec *resv = &rqstp->rq_res.head[0];
	struct xdr_netobj cli_handle;
	struct gssp_upcall_data ud;
	uint64_t handle;
	int status;
	int ret;
	struct net *net = SVC_NET(rqstp);
	struct sunrpc_net *sn = net_generic(net, sunrpc_net_id);

	memset(&ud, 0, sizeof(ud));
	ret = gss_read_proxy_verf(rqstp, gc, authp,
				  &ud.in_handle, &ud.in_token);
	if (ret)
		return ret;

	ret = SVC_CLOSE;

	/* Perform synchronous upcall to gss-proxy */
	status = gssp_accept_sec_context_upcall(net, &ud);
	if (status)
		goto out;

	trace_rpcgss_svc_accept_upcall(rqstp, ud.major_status, ud.minor_status);

	switch (ud.major_status) {
	case GSS_S_CONTINUE_NEEDED:
		cli_handle = ud.out_handle;
		break;
	case GSS_S_COMPLETE:
		status = gss_proxy_save_rsc(sn->rsc_cache, &ud, &handle);
		if (status)
			goto out;
		cli_handle.data = (u8 *)&handle;
		cli_handle.len = sizeof(handle);
		break;
	default:
		goto out;
	}

	/* Got an answer to the upcall; use it: */
	if (gss_write_init_verf(sn->rsc_cache, rqstp,
				&cli_handle, &ud.major_status))
		goto out;
	if (gss_write_resv(resv, PAGE_SIZE,
			   &cli_handle, &ud.out_token,
			   ud.major_status, ud.minor_status))
		goto out;

	ret = SVC_COMPLETE;
out:
	gss_free_in_token_pages(&ud.in_token);
	gssp_free_upcall_data(&ud);
	return ret;
}

/*
 * Try to set the sn->use_gss_proxy variable to a new value. We only allow
 * it to be changed if it's currently undefined (-1). If it's any other value
 * then return -EBUSY unless the type wouldn't have changed anyway.
 */
static int set_gss_proxy(struct net *net, int type)
{
	struct sunrpc_net *sn = net_generic(net, sunrpc_net_id);
	int ret;

	WARN_ON_ONCE(type != 0 && type != 1);
	ret = cmpxchg(&sn->use_gss_proxy, -1, type);
	if (ret != -1 && ret != type)
		return -EBUSY;
	return 0;
}

static bool use_gss_proxy(struct net *net)
{
	struct sunrpc_net *sn = net_generic(net, sunrpc_net_id);

	/* If use_gss_proxy is still undefined, then try to disable it */
	if (sn->use_gss_proxy == -1)
		set_gss_proxy(net, 0);
	return sn->use_gss_proxy;
}

#ifdef CONFIG_PROC_FS

static ssize_t write_gssp(struct file *file, const char __user *buf,
			 size_t count, loff_t *ppos)
{
	struct net *net = PDE_DATA(file_inode(file));
	char tbuf[20];
	unsigned long i;
	int res;

	if (*ppos || count > sizeof(tbuf)-1)
		return -EINVAL;
	if (copy_from_user(tbuf, buf, count))
		return -EFAULT;

	tbuf[count] = 0;
	res = kstrtoul(tbuf, 0, &i);
	if (res)
		return res;
	if (i != 1)
		return -EINVAL;
	res = set_gssp_clnt(net);
	if (res)
		return res;
	res = set_gss_proxy(net, 1);
	if (res)
		return res;
	return count;
}

static ssize_t read_gssp(struct file *file, char __user *buf,
			 size_t count, loff_t *ppos)
{
	struct net *net = PDE_DATA(file_inode(file));
	struct sunrpc_net *sn = net_generic(net, sunrpc_net_id);
	unsigned long p = *ppos;
	char tbuf[10];
	size_t len;

	snprintf(tbuf, sizeof(tbuf), "%d\n", sn->use_gss_proxy);
	len = strlen(tbuf);
	if (p >= len)
		return 0;
	len -= p;
	if (len > count)
		len = count;
	if (copy_to_user(buf, (void *)(tbuf+p), len))
		return -EFAULT;
	*ppos += len;
	return len;
}

static const struct proc_ops use_gss_proxy_proc_ops = {
	.proc_open	= nonseekable_open,
	.proc_write	= write_gssp,
	.proc_read	= read_gssp,
};

static int create_use_gss_proxy_proc_entry(struct net *net)
{
	struct sunrpc_net *sn = net_generic(net, sunrpc_net_id);
	struct proc_dir_entry **p = &sn->use_gssp_proc;

	sn->use_gss_proxy = -1;
	*p = proc_create_data("use-gss-proxy", S_IFREG | 0600,
			      sn->proc_net_rpc,
			      &use_gss_proxy_proc_ops, net);
	if (!*p)
		return -ENOMEM;
	init_gssp_clnt(sn);
	return 0;
}

static void destroy_use_gss_proxy_proc_entry(struct net *net)
{
	struct sunrpc_net *sn = net_generic(net, sunrpc_net_id);

	if (sn->use_gssp_proc) {
		remove_proc_entry("use-gss-proxy", sn->proc_net_rpc);
		clear_gssp_clnt(sn);
	}
}
#else /* CONFIG_PROC_FS */

static int create_use_gss_proxy_proc_entry(struct net *net)
{
	return 0;
}

static void destroy_use_gss_proxy_proc_entry(struct net *net) {}

#endif /* CONFIG_PROC_FS */

/*
 * Accept an rpcsec packet.
 * If context establishment, punt to user space
 * If data exchange, verify/decrypt
 * If context destruction, handle here
 * In the context establishment and destruction case we encode
 * response here and return SVC_COMPLETE.
 */
static int
svcauth_gss_accept(struct svc_rqst *rqstp, __be32 *authp)
{
	struct kvec	*argv = &rqstp->rq_arg.head[0];
	struct kvec	*resv = &rqstp->rq_res.head[0];
	u32		crlen;
	struct gss_svc_data *svcdata = rqstp->rq_auth_data;
	struct rpc_gss_wire_cred *gc;
	struct rsc	*rsci = NULL;
	__be32		*rpcstart;
	__be32		*reject_stat = resv->iov_base + resv->iov_len;
	int		ret;
	struct sunrpc_net *sn = net_generic(SVC_NET(rqstp), sunrpc_net_id);

	*authp = rpc_autherr_badcred;
	if (!svcdata)
		svcdata = kmalloc(sizeof(*svcdata), GFP_KERNEL);
	if (!svcdata)
		goto auth_err;
	rqstp->rq_auth_data = svcdata;
	svcdata->verf_start = NULL;
	svcdata->rsci = NULL;
	gc = &svcdata->clcred;

	/* start of rpc packet is 7 u32's back from here:
	 * xid direction rpcversion prog vers proc flavour
	 */
	rpcstart = argv->iov_base;
	rpcstart -= 7;

	/* credential is:
	 *   version(==1), proc(0,1,2,3), seq, service (1,2,3), handle
	 * at least 5 u32s, and is preceded by length, so that makes 6.
	 */

	if (argv->iov_len < 5 * 4)
		goto auth_err;
	crlen = svc_getnl(argv);
	if (svc_getnl(argv) != RPC_GSS_VERSION)
		goto auth_err;
	gc->gc_proc = svc_getnl(argv);
	gc->gc_seq = svc_getnl(argv);
	gc->gc_svc = svc_getnl(argv);
	if (svc_safe_getnetobj(argv, &gc->gc_ctx))
		goto auth_err;
	if (crlen != round_up_to_quad(gc->gc_ctx.len) + 5 * 4)
		goto auth_err;

	if ((gc->gc_proc != RPC_GSS_PROC_DATA) && (rqstp->rq_proc != 0))
		goto auth_err;

	*authp = rpc_autherr_badverf;
	switch (gc->gc_proc) {
	case RPC_GSS_PROC_INIT:
	case RPC_GSS_PROC_CONTINUE_INIT:
		if (use_gss_proxy(SVC_NET(rqstp)))
			return svcauth_gss_proxy_init(rqstp, gc, authp);
		else
			return svcauth_gss_legacy_init(rqstp, gc, authp);
	case RPC_GSS_PROC_DATA:
	case RPC_GSS_PROC_DESTROY:
		/* Look up the context, and check the verifier: */
		*authp = rpcsec_gsserr_credproblem;
		rsci = gss_svc_searchbyctx(sn->rsc_cache, &gc->gc_ctx);
		if (!rsci)
			goto auth_err;
		switch (gss_verify_header(rqstp, rsci, rpcstart, gc, authp)) {
		case SVC_OK:
			break;
		case SVC_DENIED:
			goto auth_err;
		case SVC_DROP:
			goto drop;
		}
		break;
	default:
		*authp = rpc_autherr_rejectedcred;
		goto auth_err;
	}

	/* now act upon the command: */
	switch (gc->gc_proc) {
	case RPC_GSS_PROC_DESTROY:
		if (gss_write_verf(rqstp, rsci->mechctx, gc->gc_seq))
			goto auth_err;
		/* Delete the entry from the cache_list and call cache_put */
		sunrpc_cache_unhash(sn->rsc_cache, &rsci->h);
		if (resv->iov_len + 4 > PAGE_SIZE)
			goto drop;
		svc_putnl(resv, RPC_SUCCESS);
		goto complete;
	case RPC_GSS_PROC_DATA:
		*authp = rpcsec_gsserr_ctxproblem;
		svcdata->verf_start = resv->iov_base + resv->iov_len;
		if (gss_write_verf(rqstp, rsci->mechctx, gc->gc_seq))
			goto auth_err;
		rqstp->rq_cred = rsci->cred;
		get_group_info(rsci->cred.cr_group_info);
		*authp = rpc_autherr_badcred;
		switch (gc->gc_svc) {
		case RPC_GSS_SVC_NONE:
			break;
		case RPC_GSS_SVC_INTEGRITY:
			/* placeholders for length and seq. number: */
			svc_putnl(resv, 0);
			svc_putnl(resv, 0);
			if (unwrap_integ_data(rqstp, &rqstp->rq_arg,
					gc->gc_seq, rsci->mechctx))
				goto garbage_args;
			rqstp->rq_auth_slack = RPC_MAX_AUTH_SIZE;
			break;
		case RPC_GSS_SVC_PRIVACY:
			/* placeholders for length and seq. number: */
			svc_putnl(resv, 0);
			svc_putnl(resv, 0);
			if (unwrap_priv_data(rqstp, &rqstp->rq_arg,
					gc->gc_seq, rsci->mechctx))
				goto garbage_args;
			rqstp->rq_auth_slack = RPC_MAX_AUTH_SIZE * 2;
			break;
		default:
			goto auth_err;
		}
		svcdata->rsci = rsci;
		cache_get(&rsci->h);
		rqstp->rq_cred.cr_flavor = gss_svc_to_pseudoflavor(
					rsci->mechctx->mech_type,
					GSS_C_QOP_DEFAULT,
					gc->gc_svc);
		ret = SVC_OK;
		trace_rpcgss_svc_authenticate(rqstp, gc);
		goto out;
	}
garbage_args:
	ret = SVC_GARBAGE;
	goto out;
auth_err:
	/* Restore write pointer to its original value: */
	xdr_ressize_check(rqstp, reject_stat);
	ret = SVC_DENIED;
	goto out;
complete:
	ret = SVC_COMPLETE;
	goto out;
drop:
	ret = SVC_CLOSE;
out:
	if (rsci)
		cache_put(&rsci->h, sn->rsc_cache);
	return ret;
}

static __be32 *
svcauth_gss_prepare_to_wrap(struct xdr_buf *resbuf, struct gss_svc_data *gsd)
{
	__be32 *p;
	u32 verf_len;

	p = gsd->verf_start;
	gsd->verf_start = NULL;

	/* If the reply stat is nonzero, don't wrap: */
	if (*(p-1) != rpc_success)
		return NULL;
	/* Skip the verifier: */
	p += 1;
	verf_len = ntohl(*p++);
	p += XDR_QUADLEN(verf_len);
	/* move accept_stat to right place: */
	memcpy(p, p + 2, 4);
	/* Also don't wrap if the accept stat is nonzero: */
	if (*p != rpc_success) {
		resbuf->head[0].iov_len -= 2 * 4;
		return NULL;
	}
	p++;
	return p;
}

static inline int
svcauth_gss_wrap_resp_integ(struct svc_rqst *rqstp)
{
	struct gss_svc_data *gsd = (struct gss_svc_data *)rqstp->rq_auth_data;
	struct rpc_gss_wire_cred *gc = &gsd->clcred;
	struct xdr_buf *resbuf = &rqstp->rq_res;
	struct xdr_buf integ_buf;
	struct xdr_netobj mic;
	struct kvec *resv;
	__be32 *p;
	int integ_offset, integ_len;
	int stat = -EINVAL;

	p = svcauth_gss_prepare_to_wrap(resbuf, gsd);
	if (p == NULL)
		goto out;
	integ_offset = (u8 *)(p + 1) - (u8 *)resbuf->head[0].iov_base;
	integ_len = resbuf->len - integ_offset;
	if (integ_len & 3)
		goto out;
	*p++ = htonl(integ_len);
	*p++ = htonl(gc->gc_seq);
	if (xdr_buf_subsegment(resbuf, &integ_buf, integ_offset, integ_len)) {
		WARN_ON_ONCE(1);
		goto out_err;
	}
	if (resbuf->tail[0].iov_base == NULL) {
		if (resbuf->head[0].iov_len + RPC_MAX_AUTH_SIZE > PAGE_SIZE)
			goto out_err;
		resbuf->tail[0].iov_base = resbuf->head[0].iov_base
						+ resbuf->head[0].iov_len;
		resbuf->tail[0].iov_len = 0;
	}
	resv = &resbuf->tail[0];
	mic.data = (u8 *)resv->iov_base + resv->iov_len + 4;
	if (gss_get_mic(gsd->rsci->mechctx, &integ_buf, &mic))
		goto out_err;
	svc_putnl(resv, mic.len);
	memset(mic.data + mic.len, 0,
			round_up_to_quad(mic.len) - mic.len);
	resv->iov_len += XDR_QUADLEN(mic.len) << 2;
	/* not strictly required: */
	resbuf->len += XDR_QUADLEN(mic.len) << 2;
	if (resv->iov_len > PAGE_SIZE)
		goto out_err;
out:
	stat = 0;
out_err:
	return stat;
}

static inline int
svcauth_gss_wrap_resp_priv(struct svc_rqst *rqstp)
{
	struct gss_svc_data *gsd = (struct gss_svc_data *)rqstp->rq_auth_data;
	struct rpc_gss_wire_cred *gc = &gsd->clcred;
	struct xdr_buf *resbuf = &rqstp->rq_res;
	struct page **inpages = NULL;
	__be32 *p, *len;
	int offset;
	int pad;

	p = svcauth_gss_prepare_to_wrap(resbuf, gsd);
	if (p == NULL)
		return 0;
	len = p++;
	offset = (u8 *)p - (u8 *)resbuf->head[0].iov_base;
	*p++ = htonl(gc->gc_seq);
	inpages = resbuf->pages;
	/* XXX: Would be better to write some xdr helper functions for
	 * nfs{2,3,4}xdr.c that place the data right, instead of copying: */

	/*
	 * If there is currently tail data, make sure there is
	 * room for the head, tail, and 2 * RPC_MAX_AUTH_SIZE in
	 * the page, and move the current tail data such that
	 * there is RPC_MAX_AUTH_SIZE slack space available in
	 * both the head and tail.
	 */
	if (resbuf->tail[0].iov_base) {
		if (resbuf->tail[0].iov_base >=
			resbuf->head[0].iov_base + PAGE_SIZE)
			return -EINVAL;
		if (resbuf->tail[0].iov_base < resbuf->head[0].iov_base)
			return -EINVAL;
		if (resbuf->tail[0].iov_len + resbuf->head[0].iov_len
				+ 2 * RPC_MAX_AUTH_SIZE > PAGE_SIZE)
			return -ENOMEM;
		memmove(resbuf->tail[0].iov_base + RPC_MAX_AUTH_SIZE,
			resbuf->tail[0].iov_base,
			resbuf->tail[0].iov_len);
		resbuf->tail[0].iov_base += RPC_MAX_AUTH_SIZE;
	}
	/*
	 * If there is no current tail data, make sure there is
	 * room for the head data, and 2 * RPC_MAX_AUTH_SIZE in the
	 * allotted page, and set up tail information such that there
	 * is RPC_MAX_AUTH_SIZE slack space available in both the
	 * head and tail.
	 */
	if (resbuf->tail[0].iov_base == NULL) {
		if (resbuf->head[0].iov_len + 2*RPC_MAX_AUTH_SIZE > PAGE_SIZE)
			return -ENOMEM;
		resbuf->tail[0].iov_base = resbuf->head[0].iov_base
			+ resbuf->head[0].iov_len + RPC_MAX_AUTH_SIZE;
		resbuf->tail[0].iov_len = 0;
	}
	if (gss_wrap(gsd->rsci->mechctx, offset, resbuf, inpages))
		return -ENOMEM;
	*len = htonl(resbuf->len - offset);
	pad = 3 - ((resbuf->len - offset - 1)&3);
	p = (__be32 *)(resbuf->tail[0].iov_base + resbuf->tail[0].iov_len);
	memset(p, 0, pad);
	resbuf->tail[0].iov_len += pad;
	resbuf->len += pad;
	return 0;
}

static int
svcauth_gss_release(struct svc_rqst *rqstp)
{
	struct gss_svc_data *gsd = (struct gss_svc_data *)rqstp->rq_auth_data;
	struct rpc_gss_wire_cred *gc = &gsd->clcred;
	struct xdr_buf *resbuf = &rqstp->rq_res;
	int stat = -EINVAL;
	struct sunrpc_net *sn = net_generic(SVC_NET(rqstp), sunrpc_net_id);

	if (gc->gc_proc != RPC_GSS_PROC_DATA)
		goto out;
	/* Release can be called twice, but we only wrap once. */
	if (gsd->verf_start == NULL)
		goto out;
	/* normally not set till svc_send, but we need it here: */
	/* XXX: what for?  Do we mess it up the moment we call svc_putu32
	 * or whatever? */
	resbuf->len = total_buf_len(resbuf);
	switch (gc->gc_svc) {
	case RPC_GSS_SVC_NONE:
		break;
	case RPC_GSS_SVC_INTEGRITY:
		stat = svcauth_gss_wrap_resp_integ(rqstp);
		if (stat)
			goto out_err;
		break;
	case RPC_GSS_SVC_PRIVACY:
		stat = svcauth_gss_wrap_resp_priv(rqstp);
		if (stat)
			goto out_err;
		break;
	/*
	 * For any other gc_svc value, svcauth_gss_accept() already set
	 * the auth_error appropriately; just fall through:
	 */
	}

out:
	stat = 0;
out_err:
	if (rqstp->rq_client)
		auth_domain_put(rqstp->rq_client);
	rqstp->rq_client = NULL;
	if (rqstp->rq_gssclient)
		auth_domain_put(rqstp->rq_gssclient);
	rqstp->rq_gssclient = NULL;
	if (rqstp->rq_cred.cr_group_info)
		put_group_info(rqstp->rq_cred.cr_group_info);
	rqstp->rq_cred.cr_group_info = NULL;
	if (gsd->rsci)
		cache_put(&gsd->rsci->h, sn->rsc_cache);
	gsd->rsci = NULL;

	return stat;
}

static void
svcauth_gss_domain_release_rcu(struct rcu_head *head)
{
	struct auth_domain *dom = container_of(head, struct auth_domain, rcu_head);
	struct gss_domain *gd = container_of(dom, struct gss_domain, h);

	kfree(dom->name);
	kfree(gd);
}

static void
svcauth_gss_domain_release(struct auth_domain *dom)
{
	call_rcu(&dom->rcu_head, svcauth_gss_domain_release_rcu);
}

static struct auth_ops svcauthops_gss = {
	.name		= "rpcsec_gss",
	.owner		= THIS_MODULE,
	.flavour	= RPC_AUTH_GSS,
	.accept		= svcauth_gss_accept,
	.release	= svcauth_gss_release,
	.domain_release = svcauth_gss_domain_release,
	.set_client	= svcauth_gss_set_client,
};

static int rsi_cache_create_net(struct net *net)
{
	struct sunrpc_net *sn = net_generic(net, sunrpc_net_id);
	struct cache_detail *cd;
	int err;

	cd = cache_create_net(&rsi_cache_template, net);
	if (IS_ERR(cd))
		return PTR_ERR(cd);
	err = cache_register_net(cd, net);
	if (err) {
		cache_destroy_net(cd, net);
		return err;
	}
	sn->rsi_cache = cd;
	return 0;
}

static void rsi_cache_destroy_net(struct net *net)
{
	struct sunrpc_net *sn = net_generic(net, sunrpc_net_id);
	struct cache_detail *cd = sn->rsi_cache;

	sn->rsi_cache = NULL;
	cache_purge(cd);
	cache_unregister_net(cd, net);
	cache_destroy_net(cd, net);
}

static int rsc_cache_create_net(struct net *net)
{
	struct sunrpc_net *sn = net_generic(net, sunrpc_net_id);
	struct cache_detail *cd;
	int err;

	cd = cache_create_net(&rsc_cache_template, net);
	if (IS_ERR(cd))
		return PTR_ERR(cd);
	err = cache_register_net(cd, net);
	if (err) {
		cache_destroy_net(cd, net);
		return err;
	}
	sn->rsc_cache = cd;
	return 0;
}

static void rsc_cache_destroy_net(struct net *net)
{
	struct sunrpc_net *sn = net_generic(net, sunrpc_net_id);
	struct cache_detail *cd = sn->rsc_cache;

	sn->rsc_cache = NULL;
	cache_purge(cd);
	cache_unregister_net(cd, net);
	cache_destroy_net(cd, net);
}

int
gss_svc_init_net(struct net *net)
{
	int rv;

	rv = rsc_cache_create_net(net);
	if (rv)
		return rv;
	rv = rsi_cache_create_net(net);
	if (rv)
		goto out1;
	rv = create_use_gss_proxy_proc_entry(net);
	if (rv)
		goto out2;
	return 0;
out2:
	destroy_use_gss_proxy_proc_entry(net);
out1:
	rsc_cache_destroy_net(net);
	return rv;
}

void
gss_svc_shutdown_net(struct net *net)
{
	destroy_use_gss_proxy_proc_entry(net);
	rsi_cache_destroy_net(net);
	rsc_cache_destroy_net(net);
}

int
gss_svc_init(void)
{
	return svc_auth_register(RPC_AUTH_GSS, &svcauthops_gss);
}

void
gss_svc_shutdown(void)
{
	svc_auth_unregister(RPC_AUTH_GSS);
}
