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
#include <linux/sunrpc/gss_krb5.h>

#include <trace/events/rpcgss.h>

#include "gss_rpc_upcall.h"

/*
 * Unfortunately there isn't a maximum checksum size exported via the
 * GSS API. Manufacture one based on GSS mechanisms supported by this
 * implementation.
 */
#define GSS_MAX_CKSUMSIZE (GSS_KRB5_TOK_HDR_LEN + GSS_KRB5_MAX_CKSUM_LEN)

/*
 * This value may be increased in the future to accommodate other
 * usage of the scratch buffer.
 */
#define GSS_SCRATCH_SIZE GSS_MAX_CKSUMSIZE

struct gss_svc_data {
	/* decoded gss client cred: */
	struct rpc_gss_wire_cred	clcred;
	u32				gsd_databody_offset;
	struct rsc			*rsci;

	/* for temporary results */
	__be32				gsd_seq_num;
	u8				gsd_scratch[GSS_SCRATCH_SIZE];
};

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
	WARN_ONCE(*blen < 0,
		  "RPCSEC/GSS credential too large - please use gssproxy\n");
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
	status = get_expiry(&mesg, &expiry);
	if (status)
		goto out;

	status = -EINVAL;
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
	status = get_expiry(&mesg, &expiry);
	if (status)
		goto out;

	status = -EINVAL;
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
	} else if (seq_num + GSS_SEQ_WIN <= sd->sd_max) {
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

/*
 * Decode and verify a Call's verifier field. For RPC_AUTH_GSS Calls,
 * the body of this field contains a variable length checksum.
 *
 * GSS-specific auth_stat values are mandated by RFC 2203 Section
 * 5.3.3.3.
 */
static int
svcauth_gss_verify_header(struct svc_rqst *rqstp, struct rsc *rsci,
			  __be32 *rpcstart, struct rpc_gss_wire_cred *gc)
{
	struct xdr_stream	*xdr = &rqstp->rq_arg_stream;
	struct gss_ctx		*ctx_id = rsci->mechctx;
	u32			flavor, maj_stat;
	struct xdr_buf		rpchdr;
	struct xdr_netobj	checksum;
	struct kvec		iov;

	/*
	 * Compute the checksum of the incoming Call from the
	 * XID field to credential field:
	 */
	iov.iov_base = rpcstart;
	iov.iov_len = (u8 *)xdr->p - (u8 *)rpcstart;
	xdr_buf_from_iov(&iov, &rpchdr);

	/* Call's verf field: */
	if (xdr_stream_decode_opaque_auth(xdr, &flavor,
					  (void **)&checksum.data,
					  &checksum.len) < 0) {
		rqstp->rq_auth_stat = rpc_autherr_badverf;
		return SVC_DENIED;
	}
	if (flavor != RPC_AUTH_GSS) {
		rqstp->rq_auth_stat = rpc_autherr_badverf;
		return SVC_DENIED;
	}

	if (rqstp->rq_deferred)
		return SVC_OK;
	maj_stat = gss_verify_mic(ctx_id, &rpchdr, &checksum);
	if (maj_stat != GSS_S_COMPLETE) {
		trace_rpcgss_svc_mic(rqstp, maj_stat);
		rqstp->rq_auth_stat = rpcsec_gsserr_credproblem;
		return SVC_DENIED;
	}

	if (gc->gc_seq > MAXSEQ) {
		trace_rpcgss_svc_seqno_large(rqstp, gc->gc_seq);
		rqstp->rq_auth_stat = rpcsec_gsserr_ctxproblem;
		return SVC_DENIED;
	}
	if (!gss_check_seq_num(rqstp, rsci, gc->gc_seq))
		return SVC_DROP;
	return SVC_OK;
}

/*
 * Construct and encode a Reply's verifier field. The verifier's body
 * field contains a variable-length checksum of the GSS sequence
 * number.
 */
static bool
svcauth_gss_encode_verf(struct svc_rqst *rqstp, struct gss_ctx *ctx_id, u32 seq)
{
	struct gss_svc_data	*gsd = rqstp->rq_auth_data;
	u32			maj_stat;
	struct xdr_buf		verf_data;
	struct xdr_netobj	checksum;
	struct kvec		iov;

	gsd->gsd_seq_num = cpu_to_be32(seq);
	iov.iov_base = &gsd->gsd_seq_num;
	iov.iov_len = XDR_UNIT;
	xdr_buf_from_iov(&iov, &verf_data);

	checksum.data = gsd->gsd_scratch;
	maj_stat = gss_get_mic(ctx_id, &verf_data, &checksum);
	if (maj_stat != GSS_S_COMPLETE)
		goto bad_mic;

	return xdr_stream_encode_opaque_auth(&rqstp->rq_res_stream, RPC_AUTH_GSS,
					     checksum.data, checksum.len) > 0;

bad_mic:
	trace_rpcgss_svc_get_mic(rqstp, maj_stat);
	return false;
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

/*
 * RFC 2203, Section 5.3.2.2
 *
 *	struct rpc_gss_integ_data {
 *		opaque databody_integ<>;
 *		opaque checksum<>;
 *	};
 *
 *	struct rpc_gss_data_t {
 *		unsigned int seq_num;
 *		proc_req_arg_t arg;
 *	};
 */
static noinline_for_stack int
svcauth_gss_unwrap_integ(struct svc_rqst *rqstp, u32 seq, struct gss_ctx *ctx)
{
	struct gss_svc_data *gsd = rqstp->rq_auth_data;
	struct xdr_stream *xdr = &rqstp->rq_arg_stream;
	u32 len, offset, seq_num, maj_stat;
	struct xdr_buf *buf = xdr->buf;
	struct xdr_buf databody_integ;
	struct xdr_netobj checksum;

	/* Did we already verify the signature on the original pass through? */
	if (rqstp->rq_deferred)
		return 0;

	if (xdr_stream_decode_u32(xdr, &len) < 0)
		goto unwrap_failed;
	if (len & 3)
		goto unwrap_failed;
	offset = xdr_stream_pos(xdr);
	if (xdr_buf_subsegment(buf, &databody_integ, offset, len))
		goto unwrap_failed;

	/*
	 * The xdr_stream now points to the @seq_num field. The next
	 * XDR data item is the @arg field, which contains the clear
	 * text RPC program payload. The checksum, which follows the
	 * @arg field, is located and decoded without updating the
	 * xdr_stream.
	 */

	offset += len;
	if (xdr_decode_word(buf, offset, &checksum.len))
		goto unwrap_failed;
	if (checksum.len > sizeof(gsd->gsd_scratch))
		goto unwrap_failed;
	checksum.data = gsd->gsd_scratch;
	if (read_bytes_from_xdr_buf(buf, offset + XDR_UNIT, checksum.data,
				    checksum.len))
		goto unwrap_failed;

	maj_stat = gss_verify_mic(ctx, &databody_integ, &checksum);
	if (maj_stat != GSS_S_COMPLETE)
		goto bad_mic;

	/* The received seqno is protected by the checksum. */
	if (xdr_stream_decode_u32(xdr, &seq_num) < 0)
		goto unwrap_failed;
	if (seq_num != seq)
		goto bad_seqno;

	xdr_truncate_decode(xdr, XDR_UNIT + checksum.len);
	return 0;

unwrap_failed:
	trace_rpcgss_svc_unwrap_failed(rqstp);
	return -EINVAL;
bad_seqno:
	trace_rpcgss_svc_seqno_bad(rqstp, seq, seq_num);
	return -EINVAL;
bad_mic:
	trace_rpcgss_svc_mic(rqstp, maj_stat);
	return -EINVAL;
}

/*
 * RFC 2203, Section 5.3.2.3
 *
 *	struct rpc_gss_priv_data {
 *		opaque databody_priv<>
 *	};
 *
 *	struct rpc_gss_data_t {
 *		unsigned int seq_num;
 *		proc_req_arg_t arg;
 *	};
 */
static noinline_for_stack int
svcauth_gss_unwrap_priv(struct svc_rqst *rqstp, u32 seq, struct gss_ctx *ctx)
{
	struct xdr_stream *xdr = &rqstp->rq_arg_stream;
	u32 len, maj_stat, seq_num, offset;
	struct xdr_buf *buf = xdr->buf;
	unsigned int saved_len;

	if (xdr_stream_decode_u32(xdr, &len) < 0)
		goto unwrap_failed;
	if (rqstp->rq_deferred) {
		/* Already decrypted last time through! The sequence number
		 * check at out_seq is unnecessary but harmless: */
		goto out_seq;
	}
	if (len > xdr_stream_remaining(xdr))
		goto unwrap_failed;
	offset = xdr_stream_pos(xdr);

	saved_len = buf->len;
	maj_stat = gss_unwrap(ctx, offset, offset + len, buf);
	if (maj_stat != GSS_S_COMPLETE)
		goto bad_unwrap;
	xdr->nwords -= XDR_QUADLEN(saved_len - buf->len);

out_seq:
	/* gss_unwrap() decrypted the sequence number. */
	if (xdr_stream_decode_u32(xdr, &seq_num) < 0)
		goto unwrap_failed;
	if (seq_num != seq)
		goto bad_seqno;
	return 0;

unwrap_failed:
	trace_rpcgss_svc_unwrap_failed(rqstp);
	return -EINVAL;
bad_seqno:
	trace_rpcgss_svc_seqno_bad(rqstp, seq, seq_num);
	return -EINVAL;
bad_unwrap:
	trace_rpcgss_svc_unwrap(rqstp, maj_stat);
	return -EINVAL;
}

static enum svc_auth_status
svcauth_gss_set_client(struct svc_rqst *rqstp)
{
	struct gss_svc_data *svcdata = rqstp->rq_auth_data;
	struct rsc *rsci = svcdata->rsci;
	struct rpc_gss_wire_cred *gc = &svcdata->clcred;
	int stat;

	rqstp->rq_auth_stat = rpc_autherr_badcred;

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

	rqstp->rq_auth_stat = rpc_auth_ok;
	return SVC_OK;
}

static bool
svcauth_gss_proc_init_verf(struct cache_detail *cd, struct svc_rqst *rqstp,
			   struct xdr_netobj *out_handle, int *major_status,
			   u32 seq_num)
{
	struct xdr_stream *xdr = &rqstp->rq_res_stream;
	struct rsc *rsci;
	bool rc;

	if (*major_status != GSS_S_COMPLETE)
		goto null_verifier;
	rsci = gss_svc_searchbyctx(cd, out_handle);
	if (rsci == NULL) {
		*major_status = GSS_S_NO_CONTEXT;
		goto null_verifier;
	}

	rc = svcauth_gss_encode_verf(rqstp, rsci->mechctx, seq_num);
	cache_put(&rsci->h, cd);
	return rc;

null_verifier:
	return xdr_stream_encode_opaque_auth(xdr, RPC_AUTH_NULL, NULL, 0) > 0;
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
			       struct rpc_gss_wire_cred *gc,
			       struct xdr_netobj *in_handle,
			       struct gssp_in_token *in_token)
{
	struct xdr_stream *xdr = &rqstp->rq_arg_stream;
	unsigned int length, pgto_offs, pgfrom_offs;
	int pages, i, pgto, pgfrom;
	size_t to_offs, from_offs;
	u32 inlen;

	if (dup_netobj(in_handle, &gc->gc_ctx))
		return SVC_CLOSE;

	/*
	 *  RFC 2203 Section 5.2.2
	 *
	 *	struct rpc_gss_init_arg {
	 *		opaque gss_token<>;
	 *	};
	 */
	if (xdr_stream_decode_u32(xdr, &inlen) < 0)
		goto out_denied_free;
	if (inlen > xdr_stream_remaining(xdr))
		goto out_denied_free;

	pages = DIV_ROUND_UP(inlen, PAGE_SIZE);
	in_token->pages = kcalloc(pages, sizeof(struct page *), GFP_KERNEL);
	if (!in_token->pages)
		goto out_denied_free;
	in_token->page_base = 0;
	in_token->page_len = inlen;
	for (i = 0; i < pages; i++) {
		in_token->pages[i] = alloc_page(GFP_KERNEL);
		if (!in_token->pages[i]) {
			gss_free_in_token_pages(in_token);
			goto out_denied_free;
		}
	}

	length = min_t(unsigned int, inlen, (char *)xdr->end - (char *)xdr->p);
	memcpy(page_address(in_token->pages[0]), xdr->p, length);
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

out_denied_free:
	kfree(in_handle->data);
	return SVC_DENIED;
}

/*
 * RFC 2203, Section 5.2.3.1.
 *
 *	struct rpc_gss_init_res {
 *		opaque handle<>;
 *		unsigned int gss_major;
 *		unsigned int gss_minor;
 *		unsigned int seq_window;
 *		opaque gss_token<>;
 *	};
 */
static bool
svcxdr_encode_gss_init_res(struct xdr_stream *xdr,
			   struct xdr_netobj *handle,
			   struct xdr_netobj *gss_token,
			   unsigned int major_status,
			   unsigned int minor_status, u32 seq_num)
{
	if (xdr_stream_encode_opaque(xdr, handle->data, handle->len) < 0)
		return false;
	if (xdr_stream_encode_u32(xdr, major_status) < 0)
		return false;
	if (xdr_stream_encode_u32(xdr, minor_status) < 0)
		return false;
	if (xdr_stream_encode_u32(xdr, seq_num) < 0)
		return false;
	if (xdr_stream_encode_opaque(xdr, gss_token->data, gss_token->len) < 0)
		return false;
	return true;
}

/*
 * Having read the cred already and found we're in the context
 * initiation case, read the verifier and initiate (or check the results
 * of) upcalls to userspace for help with context initiation.  If
 * the upcall results are available, write the verifier and result.
 * Otherwise, drop the request pending an answer to the upcall.
 */
static int
svcauth_gss_legacy_init(struct svc_rqst *rqstp,
			struct rpc_gss_wire_cred *gc)
{
	struct xdr_stream *xdr = &rqstp->rq_arg_stream;
	struct rsi *rsip, rsikey;
	__be32 *p;
	u32 len;
	int ret;
	struct sunrpc_net *sn = net_generic(SVC_NET(rqstp), sunrpc_net_id);

	memset(&rsikey, 0, sizeof(rsikey));
	if (dup_netobj(&rsikey.in_handle, &gc->gc_ctx))
		return SVC_CLOSE;

	/*
	 *  RFC 2203 Section 5.2.2
	 *
	 *	struct rpc_gss_init_arg {
	 *		opaque gss_token<>;
	 *	};
	 */
	if (xdr_stream_decode_u32(xdr, &len) < 0) {
		kfree(rsikey.in_handle.data);
		return SVC_DENIED;
	}
	p = xdr_inline_decode(xdr, len);
	if (!p) {
		kfree(rsikey.in_handle.data);
		return SVC_DENIED;
	}
	rsikey.in_token.data = kmalloc(len, GFP_KERNEL);
	if (ZERO_OR_NULL_PTR(rsikey.in_token.data)) {
		kfree(rsikey.in_handle.data);
		return SVC_CLOSE;
	}
	memcpy(rsikey.in_token.data, p, len);
	rsikey.in_token.len = len;

	/* Perform upcall, or find upcall result: */
	rsip = rsi_lookup(sn->rsi_cache, &rsikey);
	rsi_free(&rsikey);
	if (!rsip)
		return SVC_CLOSE;
	if (cache_check(sn->rsi_cache, &rsip->h, &rqstp->rq_chandle) < 0)
		/* No upcall result: */
		return SVC_CLOSE;

	ret = SVC_CLOSE;
	if (!svcauth_gss_proc_init_verf(sn->rsc_cache, rqstp, &rsip->out_handle,
					&rsip->major_status, GSS_SEQ_WIN))
		goto out;
	if (!svcxdr_set_accept_stat(rqstp))
		goto out;
	if (!svcxdr_encode_gss_init_res(&rqstp->rq_res_stream, &rsip->out_handle,
					&rsip->out_token, rsip->major_status,
					rsip->minor_status, GSS_SEQ_WIN))
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
	int status;

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
				  struct rpc_gss_wire_cred *gc)
{
	struct xdr_netobj cli_handle;
	struct gssp_upcall_data ud;
	uint64_t handle;
	int status;
	int ret;
	struct net *net = SVC_NET(rqstp);
	struct sunrpc_net *sn = net_generic(net, sunrpc_net_id);

	memset(&ud, 0, sizeof(ud));
	ret = gss_read_proxy_verf(rqstp, gc, &ud.in_handle, &ud.in_token);
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

	if (!svcauth_gss_proc_init_verf(sn->rsc_cache, rqstp, &cli_handle,
					&ud.major_status, GSS_SEQ_WIN))
		goto out;
	if (!svcxdr_set_accept_stat(rqstp))
		goto out;
	if (!svcxdr_encode_gss_init_res(&rqstp->rq_res_stream, &cli_handle,
					&ud.out_token, ud.major_status,
					ud.minor_status, GSS_SEQ_WIN))
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

static noinline_for_stack int
svcauth_gss_proc_init(struct svc_rqst *rqstp, struct rpc_gss_wire_cred *gc)
{
	struct xdr_stream *xdr = &rqstp->rq_arg_stream;
	u32 flavor, len;
	void *body;

	/* Call's verf field: */
	if (xdr_stream_decode_opaque_auth(xdr, &flavor, &body, &len) < 0)
		return SVC_GARBAGE;
	if (flavor != RPC_AUTH_NULL || len != 0) {
		rqstp->rq_auth_stat = rpc_autherr_badverf;
		return SVC_DENIED;
	}

	if (gc->gc_proc == RPC_GSS_PROC_INIT && gc->gc_ctx.len != 0) {
		rqstp->rq_auth_stat = rpc_autherr_badcred;
		return SVC_DENIED;
	}

	if (!use_gss_proxy(SVC_NET(rqstp)))
		return svcauth_gss_legacy_init(rqstp, gc);
	return svcauth_gss_proxy_init(rqstp, gc);
}

#ifdef CONFIG_PROC_FS

static ssize_t write_gssp(struct file *file, const char __user *buf,
			 size_t count, loff_t *ppos)
{
	struct net *net = pde_data(file_inode(file));
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
	struct net *net = pde_data(file_inode(file));
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

static ssize_t read_gss_krb5_enctypes(struct file *file, char __user *buf,
				      size_t count, loff_t *ppos)
{
	struct rpcsec_gss_oid oid = {
		.len	= 9,
		.data	= "\x2a\x86\x48\x86\xf7\x12\x01\x02\x02",
	};
	struct gss_api_mech *mech;
	ssize_t ret;

	mech = gss_mech_get_by_OID(&oid);
	if (!mech)
		return 0;
	if (!mech->gm_upcall_enctypes) {
		gss_mech_put(mech);
		return 0;
	}

	ret = simple_read_from_buffer(buf, count, ppos,
				      mech->gm_upcall_enctypes,
				      strlen(mech->gm_upcall_enctypes));
	gss_mech_put(mech);
	return ret;
}

static const struct proc_ops gss_krb5_enctypes_proc_ops = {
	.proc_open	= nonseekable_open,
	.proc_read	= read_gss_krb5_enctypes,
};

static int create_krb5_enctypes_proc_entry(struct net *net)
{
	struct sunrpc_net *sn = net_generic(net, sunrpc_net_id);

	sn->gss_krb5_enctypes =
		proc_create_data("gss_krb5_enctypes", S_IFREG | 0444,
				 sn->proc_net_rpc, &gss_krb5_enctypes_proc_ops,
				 net);
	return sn->gss_krb5_enctypes ? 0 : -ENOMEM;
}

static void destroy_krb5_enctypes_proc_entry(struct net *net)
{
	struct sunrpc_net *sn = net_generic(net, sunrpc_net_id);

	if (sn->gss_krb5_enctypes)
		remove_proc_entry("gss_krb5_enctypes", sn->proc_net_rpc);
}

#else /* CONFIG_PROC_FS */

static int create_use_gss_proxy_proc_entry(struct net *net)
{
	return 0;
}

static void destroy_use_gss_proxy_proc_entry(struct net *net) {}

static int create_krb5_enctypes_proc_entry(struct net *net)
{
	return 0;
}

static void destroy_krb5_enctypes_proc_entry(struct net *net) {}

#endif /* CONFIG_PROC_FS */

/*
 * The Call's credential body should contain a struct rpc_gss_cred_t.
 *
 * RFC 2203 Section 5
 *
 *	struct rpc_gss_cred_t {
 *		union switch (unsigned int version) {
 *		case RPCSEC_GSS_VERS_1:
 *			struct {
 *				rpc_gss_proc_t gss_proc;
 *				unsigned int seq_num;
 *				rpc_gss_service_t service;
 *				opaque handle<>;
 *			} rpc_gss_cred_vers_1_t;
 *		}
 *	};
 */
static bool
svcauth_gss_decode_credbody(struct xdr_stream *xdr,
			    struct rpc_gss_wire_cred *gc,
			    __be32 **rpcstart)
{
	ssize_t handle_len;
	u32 body_len;
	__be32 *p;

	p = xdr_inline_decode(xdr, XDR_UNIT);
	if (!p)
		return false;
	/*
	 * start of rpc packet is 7 u32's back from here:
	 * xid direction rpcversion prog vers proc flavour
	 */
	*rpcstart = p - 7;
	body_len = be32_to_cpup(p);
	if (body_len > RPC_MAX_AUTH_SIZE)
		return false;

	/* struct rpc_gss_cred_t */
	if (xdr_stream_decode_u32(xdr, &gc->gc_v) < 0)
		return false;
	if (xdr_stream_decode_u32(xdr, &gc->gc_proc) < 0)
		return false;
	if (xdr_stream_decode_u32(xdr, &gc->gc_seq) < 0)
		return false;
	if (xdr_stream_decode_u32(xdr, &gc->gc_svc) < 0)
		return false;
	handle_len = xdr_stream_decode_opaque_inline(xdr,
						     (void **)&gc->gc_ctx.data,
						     body_len);
	if (handle_len < 0)
		return false;
	if (body_len != XDR_UNIT * 5 + xdr_align_size(handle_len))
		return false;

	gc->gc_ctx.len = handle_len;
	return true;
}

/**
 * svcauth_gss_accept - Decode and validate incoming RPC_AUTH_GSS credential
 * @rqstp: RPC transaction
 *
 * Return values:
 *   %SVC_OK: Success
 *   %SVC_COMPLETE: GSS context lifetime event
 *   %SVC_DENIED: Credential or verifier is not valid
 *   %SVC_GARBAGE: Failed to decode credential or verifier
 *   %SVC_CLOSE: Temporary failure
 *
 * The rqstp->rq_auth_stat field is also set (see RFCs 2203 and 5531).
 */
static enum svc_auth_status
svcauth_gss_accept(struct svc_rqst *rqstp)
{
	struct gss_svc_data *svcdata = rqstp->rq_auth_data;
	__be32		*rpcstart;
	struct rpc_gss_wire_cred *gc;
	struct rsc	*rsci = NULL;
	int		ret;
	struct sunrpc_net *sn = net_generic(SVC_NET(rqstp), sunrpc_net_id);

	rqstp->rq_auth_stat = rpc_autherr_badcred;
	if (!svcdata)
		svcdata = kmalloc(sizeof(*svcdata), GFP_KERNEL);
	if (!svcdata)
		goto auth_err;
	rqstp->rq_auth_data = svcdata;
	svcdata->gsd_databody_offset = 0;
	svcdata->rsci = NULL;
	gc = &svcdata->clcred;

	if (!svcauth_gss_decode_credbody(&rqstp->rq_arg_stream, gc, &rpcstart))
		goto auth_err;
	if (gc->gc_v != RPC_GSS_VERSION)
		goto auth_err;

	switch (gc->gc_proc) {
	case RPC_GSS_PROC_INIT:
	case RPC_GSS_PROC_CONTINUE_INIT:
		if (rqstp->rq_proc != 0)
			goto auth_err;
		return svcauth_gss_proc_init(rqstp, gc);
	case RPC_GSS_PROC_DESTROY:
		if (rqstp->rq_proc != 0)
			goto auth_err;
		fallthrough;
	case RPC_GSS_PROC_DATA:
		rqstp->rq_auth_stat = rpcsec_gsserr_credproblem;
		rsci = gss_svc_searchbyctx(sn->rsc_cache, &gc->gc_ctx);
		if (!rsci)
			goto auth_err;
		switch (svcauth_gss_verify_header(rqstp, rsci, rpcstart, gc)) {
		case SVC_OK:
			break;
		case SVC_DENIED:
			goto auth_err;
		case SVC_DROP:
			goto drop;
		}
		break;
	default:
		if (rqstp->rq_proc != 0)
			goto auth_err;
		rqstp->rq_auth_stat = rpc_autherr_rejectedcred;
		goto auth_err;
	}

	/* now act upon the command: */
	switch (gc->gc_proc) {
	case RPC_GSS_PROC_DESTROY:
		if (!svcauth_gss_encode_verf(rqstp, rsci->mechctx, gc->gc_seq))
			goto auth_err;
		if (!svcxdr_set_accept_stat(rqstp))
			goto auth_err;
		/* Delete the entry from the cache_list and call cache_put */
		sunrpc_cache_unhash(sn->rsc_cache, &rsci->h);
		goto complete;
	case RPC_GSS_PROC_DATA:
		rqstp->rq_auth_stat = rpcsec_gsserr_ctxproblem;
		if (!svcauth_gss_encode_verf(rqstp, rsci->mechctx, gc->gc_seq))
			goto auth_err;
		if (!svcxdr_set_accept_stat(rqstp))
			goto auth_err;
		svcdata->gsd_databody_offset = xdr_stream_pos(&rqstp->rq_res_stream);
		rqstp->rq_cred = rsci->cred;
		get_group_info(rsci->cred.cr_group_info);
		rqstp->rq_auth_stat = rpc_autherr_badcred;
		switch (gc->gc_svc) {
		case RPC_GSS_SVC_NONE:
			break;
		case RPC_GSS_SVC_INTEGRITY:
			/* placeholders for body length and seq. number: */
			xdr_reserve_space(&rqstp->rq_res_stream, XDR_UNIT * 2);
			if (svcauth_gss_unwrap_integ(rqstp, gc->gc_seq,
						     rsci->mechctx))
				goto garbage_args;
			svcxdr_set_auth_slack(rqstp, RPC_MAX_AUTH_SIZE);
			break;
		case RPC_GSS_SVC_PRIVACY:
			/* placeholders for body length and seq. number: */
			xdr_reserve_space(&rqstp->rq_res_stream, XDR_UNIT * 2);
			if (svcauth_gss_unwrap_priv(rqstp, gc->gc_seq,
						    rsci->mechctx))
				goto garbage_args;
			svcxdr_set_auth_slack(rqstp, RPC_MAX_AUTH_SIZE * 2);
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
	xdr_truncate_encode(&rqstp->rq_res_stream, XDR_UNIT * 2);
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

static u32
svcauth_gss_prepare_to_wrap(struct svc_rqst *rqstp, struct gss_svc_data *gsd)
{
	u32 offset;

	/* Release can be called twice, but we only wrap once. */
	offset = gsd->gsd_databody_offset;
	gsd->gsd_databody_offset = 0;

	/* AUTH_ERROR replies are not wrapped. */
	if (rqstp->rq_auth_stat != rpc_auth_ok)
		return 0;

	/* Also don't wrap if the accept_stat is nonzero: */
	if (*rqstp->rq_accept_statp != rpc_success)
		return 0;

	return offset;
}

/*
 * RFC 2203, Section 5.3.2.2
 *
 *	struct rpc_gss_integ_data {
 *		opaque databody_integ<>;
 *		opaque checksum<>;
 *	};
 *
 *	struct rpc_gss_data_t {
 *		unsigned int seq_num;
 *		proc_req_arg_t arg;
 *	};
 *
 * The RPC Reply message has already been XDR-encoded. rq_res_stream
 * is now positioned so that the checksum can be written just past
 * the RPC Reply message.
 */
static int svcauth_gss_wrap_integ(struct svc_rqst *rqstp)
{
	struct gss_svc_data *gsd = rqstp->rq_auth_data;
	struct xdr_stream *xdr = &rqstp->rq_res_stream;
	struct rpc_gss_wire_cred *gc = &gsd->clcred;
	struct xdr_buf *buf = xdr->buf;
	struct xdr_buf databody_integ;
	struct xdr_netobj checksum;
	u32 offset, maj_stat;

	offset = svcauth_gss_prepare_to_wrap(rqstp, gsd);
	if (!offset)
		goto out;

	if (xdr_buf_subsegment(buf, &databody_integ, offset + XDR_UNIT,
			       buf->len - offset - XDR_UNIT))
		goto wrap_failed;
	/* Buffer space for these has already been reserved in
	 * svcauth_gss_accept(). */
	if (xdr_encode_word(buf, offset, databody_integ.len))
		goto wrap_failed;
	if (xdr_encode_word(buf, offset + XDR_UNIT, gc->gc_seq))
		goto wrap_failed;

	checksum.data = gsd->gsd_scratch;
	maj_stat = gss_get_mic(gsd->rsci->mechctx, &databody_integ, &checksum);
	if (maj_stat != GSS_S_COMPLETE)
		goto bad_mic;

	if (xdr_stream_encode_opaque(xdr, checksum.data, checksum.len) < 0)
		goto wrap_failed;
	xdr_commit_encode(xdr);

out:
	return 0;

bad_mic:
	trace_rpcgss_svc_get_mic(rqstp, maj_stat);
	return -EINVAL;
wrap_failed:
	trace_rpcgss_svc_wrap_failed(rqstp);
	return -EINVAL;
}

/*
 * RFC 2203, Section 5.3.2.3
 *
 *	struct rpc_gss_priv_data {
 *		opaque databody_priv<>
 *	};
 *
 *	struct rpc_gss_data_t {
 *		unsigned int seq_num;
 *		proc_req_arg_t arg;
 *	};
 *
 * gss_wrap() expands the size of the RPC message payload in the
 * response buffer. The main purpose of svcauth_gss_wrap_priv()
 * is to ensure there is adequate space in the response buffer to
 * avoid overflow during the wrap.
 */
static int svcauth_gss_wrap_priv(struct svc_rqst *rqstp)
{
	struct gss_svc_data *gsd = rqstp->rq_auth_data;
	struct rpc_gss_wire_cred *gc = &gsd->clcred;
	struct xdr_buf *buf = &rqstp->rq_res;
	struct kvec *head = buf->head;
	struct kvec *tail = buf->tail;
	u32 offset, pad, maj_stat;
	__be32 *p;

	offset = svcauth_gss_prepare_to_wrap(rqstp, gsd);
	if (!offset)
		return 0;

	/*
	 * Buffer space for this field has already been reserved
	 * in svcauth_gss_accept(). Note that the GSS sequence
	 * number is encrypted along with the RPC reply payload.
	 */
	if (xdr_encode_word(buf, offset + XDR_UNIT, gc->gc_seq))
		goto wrap_failed;

	/*
	 * If there is currently tail data, make sure there is
	 * room for the head, tail, and 2 * RPC_MAX_AUTH_SIZE in
	 * the page, and move the current tail data such that
	 * there is RPC_MAX_AUTH_SIZE slack space available in
	 * both the head and tail.
	 */
	if (tail->iov_base) {
		if (tail->iov_base >= head->iov_base + PAGE_SIZE)
			goto wrap_failed;
		if (tail->iov_base < head->iov_base)
			goto wrap_failed;
		if (tail->iov_len + head->iov_len
				+ 2 * RPC_MAX_AUTH_SIZE > PAGE_SIZE)
			goto wrap_failed;
		memmove(tail->iov_base + RPC_MAX_AUTH_SIZE, tail->iov_base,
			tail->iov_len);
		tail->iov_base += RPC_MAX_AUTH_SIZE;
	}
	/*
	 * If there is no current tail data, make sure there is
	 * room for the head data, and 2 * RPC_MAX_AUTH_SIZE in the
	 * allotted page, and set up tail information such that there
	 * is RPC_MAX_AUTH_SIZE slack space available in both the
	 * head and tail.
	 */
	if (!tail->iov_base) {
		if (head->iov_len + 2 * RPC_MAX_AUTH_SIZE > PAGE_SIZE)
			goto wrap_failed;
		tail->iov_base = head->iov_base
			+ head->iov_len + RPC_MAX_AUTH_SIZE;
		tail->iov_len = 0;
	}

	maj_stat = gss_wrap(gsd->rsci->mechctx, offset + XDR_UNIT, buf,
			    buf->pages);
	if (maj_stat != GSS_S_COMPLETE)
		goto bad_wrap;

	/* Wrapping can change the size of databody_priv. */
	if (xdr_encode_word(buf, offset, buf->len - offset - XDR_UNIT))
		goto wrap_failed;
	pad = xdr_pad_size(buf->len - offset - XDR_UNIT);
	p = (__be32 *)(tail->iov_base + tail->iov_len);
	memset(p, 0, pad);
	tail->iov_len += pad;
	buf->len += pad;

	return 0;
wrap_failed:
	trace_rpcgss_svc_wrap_failed(rqstp);
	return -EINVAL;
bad_wrap:
	trace_rpcgss_svc_wrap(rqstp, maj_stat);
	return -ENOMEM;
}

/**
 * svcauth_gss_release - Wrap payload and release resources
 * @rqstp: RPC transaction context
 *
 * Return values:
 *    %0: the Reply is ready to be sent
 *    %-ENOMEM: failed to allocate memory
 *    %-EINVAL: encoding error
 */
static int
svcauth_gss_release(struct svc_rqst *rqstp)
{
	struct sunrpc_net *sn = net_generic(SVC_NET(rqstp), sunrpc_net_id);
	struct gss_svc_data *gsd = rqstp->rq_auth_data;
	struct rpc_gss_wire_cred *gc;
	int stat;

	if (!gsd)
		goto out;
	gc = &gsd->clcred;
	if (gc->gc_proc != RPC_GSS_PROC_DATA)
		goto out;

	switch (gc->gc_svc) {
	case RPC_GSS_SVC_NONE:
		break;
	case RPC_GSS_SVC_INTEGRITY:
		stat = svcauth_gss_wrap_integ(rqstp);
		if (stat)
			goto out_err;
		break;
	case RPC_GSS_SVC_PRIVACY:
		stat = svcauth_gss_wrap_priv(rqstp);
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
	if (gsd && gsd->rsci) {
		cache_put(&gsd->rsci->h, sn->rsc_cache);
		gsd->rsci = NULL;
	}
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

static rpc_authflavor_t svcauth_gss_pseudoflavor(struct svc_rqst *rqstp)
{
	return svcauth_gss_flavor(rqstp->rq_gssclient);
}

static struct auth_ops svcauthops_gss = {
	.name		= "rpcsec_gss",
	.owner		= THIS_MODULE,
	.flavour	= RPC_AUTH_GSS,
	.accept		= svcauth_gss_accept,
	.release	= svcauth_gss_release,
	.domain_release = svcauth_gss_domain_release,
	.set_client	= svcauth_gss_set_client,
	.pseudoflavor	= svcauth_gss_pseudoflavor,
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

	rv = create_krb5_enctypes_proc_entry(net);
	if (rv)
		goto out3;

	return 0;

out3:
	destroy_use_gss_proxy_proc_entry(net);
out2:
	rsi_cache_destroy_net(net);
out1:
	rsc_cache_destroy_net(net);
	return rv;
}

void
gss_svc_shutdown_net(struct net *net)
{
	destroy_krb5_enctypes_proc_entry(net);
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
