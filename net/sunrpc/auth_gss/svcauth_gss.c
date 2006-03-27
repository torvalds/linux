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

#include <linux/types.h>
#include <linux/module.h>
#include <linux/pagemap.h>

#include <linux/sunrpc/auth_gss.h>
#include <linux/sunrpc/svcauth.h>
#include <linux/sunrpc/gss_err.h>
#include <linux/sunrpc/svcauth.h>
#include <linux/sunrpc/svcauth_gss.h>
#include <linux/sunrpc/cache.h>

#ifdef RPC_DEBUG
# define RPCDBG_FACILITY	RPCDBG_AUTH
#endif

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
#define	RSI_HASHMASK	(RSI_HASHMAX-1)

struct rsi {
	struct cache_head	h;
	struct xdr_netobj	in_handle, in_token;
	struct xdr_netobj	out_handle, out_token;
	int			major_status, minor_status;
};

static struct cache_head *rsi_table[RSI_HASHMAX];
static struct cache_detail rsi_cache;
static struct rsi *rsi_update(struct rsi *new, struct rsi *old);
static struct rsi *rsi_lookup(struct rsi *item);

static void rsi_free(struct rsi *rsii)
{
	kfree(rsii->in_handle.data);
	kfree(rsii->in_token.data);
	kfree(rsii->out_handle.data);
	kfree(rsii->out_token.data);
}

static void rsi_put(struct kref *ref)
{
	struct rsi *rsii = container_of(ref, struct rsi, h.ref);
	rsi_free(rsii);
	kfree(rsii);
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
	return netobj_equal(&item->in_handle, &tmp->in_handle)
		&& netobj_equal(&item->in_token, &tmp->in_token);
}

static int dup_to_netobj(struct xdr_netobj *dst, char *src, int len)
{
	dst->len = len;
	dst->data = (len ? kmalloc(len, GFP_KERNEL) : NULL);
	if (dst->data)
		memcpy(dst->data, src, len);
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
	time_t expiry;
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

	rsip = rsi_lookup(&rsii);
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
	if (len < 0)
		goto out;
	if (len == 0) {
		goto out;
	} else {
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
	}
	rsii.h.expiry_time = expiry;
	rsip = rsi_update(&rsii, rsip);
	status = 0;
out:
	rsi_free(&rsii);
	if (rsip)
		cache_put(&rsip->h, &rsi_cache);
	else
		status = -ENOMEM;
	return status;
}

static struct cache_detail rsi_cache = {
	.owner		= THIS_MODULE,
	.hash_size	= RSI_HASHMAX,
	.hash_table     = rsi_table,
	.name           = "auth.rpcsec.init",
	.cache_put      = rsi_put,
	.cache_request  = rsi_request,
	.cache_parse    = rsi_parse,
	.match		= rsi_match,
	.init		= rsi_init,
	.update		= update_rsi,
	.alloc		= rsi_alloc,
};

static struct rsi *rsi_lookup(struct rsi *item)
{
	struct cache_head *ch;
	int hash = rsi_hash(item);

	ch = sunrpc_cache_lookup(&rsi_cache, &item->h, hash);
	if (ch)
		return container_of(ch, struct rsi, h);
	else
		return NULL;
}

static struct rsi *rsi_update(struct rsi *new, struct rsi *old)
{
	struct cache_head *ch;
	int hash = rsi_hash(new);

	ch = sunrpc_cache_update(&rsi_cache, &new->h,
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
#define	RSC_HASHMASK	(RSC_HASHMAX-1)

#define GSS_SEQ_WIN	128

struct gss_svc_seq_data {
	/* highest seq number seen so far: */
	int			sd_max;
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
};

static struct cache_head *rsc_table[RSC_HASHMAX];
static struct cache_detail rsc_cache;
static struct rsc *rsc_update(struct rsc *new, struct rsc *old);
static struct rsc *rsc_lookup(struct rsc *item);

static void rsc_free(struct rsc *rsci)
{
	kfree(rsci->handle.data);
	if (rsci->mechctx)
		gss_delete_sec_context(&rsci->mechctx);
	if (rsci->cred.cr_group_info)
		put_group_info(rsci->cred.cr_group_info);
}

static void rsc_put(struct kref *ref)
{
	struct rsc *rsci = container_of(ref, struct rsc, h.ref);

	rsc_free(rsci);
	kfree(rsci);
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
	new->cred.cr_group_info = NULL;
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
	tmp->cred.cr_group_info = NULL;
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

static int rsc_parse(struct cache_detail *cd,
		     char *mesg, int mlen)
{
	/* contexthandle expiry [ uid gid N <n gids> mechname ...mechdata... ] */
	char *buf = mesg;
	int len, rv;
	struct rsc rsci, *rscp = NULL;
	time_t expiry;
	int status = -EINVAL;

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

	rscp = rsc_lookup(&rsci);
	if (!rscp)
		goto out;

	/* uid, or NEGATIVE */
	rv = get_int(&mesg, &rsci.cred.cr_uid);
	if (rv == -EINVAL)
		goto out;
	if (rv == -ENOENT)
		set_bit(CACHE_NEGATIVE, &rsci.h.flags);
	else {
		int N, i;
		struct gss_api_mech *gm;

		/* gid */
		if (get_int(&mesg, &rsci.cred.cr_gid))
			goto out;

		/* number of additional gid's */
		if (get_int(&mesg, &N))
			goto out;
		status = -ENOMEM;
		rsci.cred.cr_group_info = groups_alloc(N);
		if (rsci.cred.cr_group_info == NULL)
			goto out;

		/* gid's */
		status = -EINVAL;
		for (i=0; i<N; i++) {
			gid_t gid;
			if (get_int(&mesg, &gid))
				goto out;
			GROUP_AT(rsci.cred.cr_group_info, i) = gid;
		}

		/* mech name */
		len = qword_get(&mesg, buf, mlen);
		if (len < 0)
			goto out;
		gm = gss_mech_get_by_name(buf);
		status = -EOPNOTSUPP;
		if (!gm)
			goto out;

		status = -EINVAL;
		/* mech-specific data: */
		len = qword_get(&mesg, buf, mlen);
		if (len < 0) {
			gss_mech_put(gm);
			goto out;
		}
		status = gss_import_sec_context(buf, len, gm, &rsci.mechctx);
		if (status) {
			gss_mech_put(gm);
			goto out;
		}
		gss_mech_put(gm);
	}
	rsci.h.expiry_time = expiry;
	rscp = rsc_update(&rsci, rscp);
	status = 0;
out:
	rsc_free(&rsci);
	if (rscp)
		cache_put(&rscp->h, &rsc_cache);
	else
		status = -ENOMEM;
	return status;
}

static struct cache_detail rsc_cache = {
	.owner		= THIS_MODULE,
	.hash_size	= RSC_HASHMAX,
	.hash_table	= rsc_table,
	.name		= "auth.rpcsec.context",
	.cache_put	= rsc_put,
	.cache_parse	= rsc_parse,
	.match		= rsc_match,
	.init		= rsc_init,
	.update		= update_rsc,
	.alloc		= rsc_alloc,
};

static struct rsc *rsc_lookup(struct rsc *item)
{
	struct cache_head *ch;
	int hash = rsc_hash(item);

	ch = sunrpc_cache_lookup(&rsc_cache, &item->h, hash);
	if (ch)
		return container_of(ch, struct rsc, h);
	else
		return NULL;
}

static struct rsc *rsc_update(struct rsc *new, struct rsc *old)
{
	struct cache_head *ch;
	int hash = rsc_hash(new);

	ch = sunrpc_cache_update(&rsc_cache, &new->h,
				 &old->h, hash);
	if (ch)
		return container_of(ch, struct rsc, h);
	else
		return NULL;
}


static struct rsc *
gss_svc_searchbyctx(struct xdr_netobj *handle)
{
	struct rsc rsci;
	struct rsc *found;

	memset(&rsci, 0, sizeof(rsci));
	if (dup_to_netobj(&rsci.handle, handle->data, handle->len))
		return NULL;
	found = rsc_lookup(&rsci);
	rsc_free(&rsci);
	if (!found)
		return NULL;
	if (cache_check(&rsc_cache, &found->h, NULL))
		return NULL;
	return found;
}

/* Implements sequence number algorithm as specified in RFC 2203. */
static int
gss_check_seq_num(struct rsc *rsci, int seq_num)
{
	struct gss_svc_seq_data *sd = &rsci->seqdata;

	spin_lock(&sd->sd_lock);
	if (seq_num > sd->sd_max) {
		if (seq_num >= sd->sd_max + GSS_SEQ_WIN) {
			memset(sd->sd_win,0,sizeof(sd->sd_win));
			sd->sd_max = seq_num;
		} else while (sd->sd_max < seq_num) {
			sd->sd_max++;
			__clear_bit(sd->sd_max % GSS_SEQ_WIN, sd->sd_win);
		}
		__set_bit(seq_num % GSS_SEQ_WIN, sd->sd_win);
		goto ok;
	} else if (seq_num <= sd->sd_max - GSS_SEQ_WIN) {
		goto drop;
	}
	/* sd_max - GSS_SEQ_WIN < seq_num <= sd_max */
	if (__test_and_set_bit(seq_num % GSS_SEQ_WIN, sd->sd_win))
		goto drop;
ok:
	spin_unlock(&sd->sd_lock);
	return 1;
drop:
	spin_unlock(&sd->sd_lock);
	return 0;
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
	o->len = ntohl(svc_getu32(argv));
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
	u32 *p;

	if (resv->iov_len + 4 > PAGE_SIZE)
		return -1;
	svc_putu32(resv, htonl(o->len));
	p = resv->iov_base + resv->iov_len;
	resv->iov_len += round_up_to_quad(o->len);
	if (resv->iov_len > PAGE_SIZE)
		return -1;
	memcpy(p, o->data, o->len);
	memset((u8 *)p + o->len, 0, round_up_to_quad(o->len) - o->len);
	return 0;
}

/* Verify the checksum on the header and return SVC_OK on success.
 * Otherwise, return SVC_DROP (in the case of a bad sequence number)
 * or return SVC_DENIED and indicate error in authp.
 */
static int
gss_verify_header(struct svc_rqst *rqstp, struct rsc *rsci,
		  u32 *rpcstart, struct rpc_gss_wire_cred *gc, u32 *authp)
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
	flavor = ntohl(svc_getu32(argv));
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
		dprintk("RPC:      svcauth_gss: discarding request with large sequence number %d\n",
				gc->gc_seq);
		*authp = rpcsec_gsserr_ctxproblem;
		return SVC_DENIED;
	}
	if (!gss_check_seq_num(rsci, gc->gc_seq)) {
		dprintk("RPC:      svcauth_gss: discarding request with old sequence number %d\n",
				gc->gc_seq);
		return SVC_DROP;
	}
	return SVC_OK;
}

static int
gss_write_null_verf(struct svc_rqst *rqstp)
{
	u32     *p;

	svc_putu32(rqstp->rq_res.head, htonl(RPC_AUTH_NULL));
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
	u32			xdr_seq;
	u32			maj_stat;
	struct xdr_buf		verf_data;
	struct xdr_netobj	mic;
	u32			*p;
	struct kvec		iov;

	svc_putu32(rqstp->rq_res.head, htonl(RPC_AUTH_GSS));
	xdr_seq = htonl(seq);

	iov.iov_base = &xdr_seq;
	iov.iov_len = sizeof(xdr_seq);
	xdr_buf_from_iov(&iov, &verf_data);
	p = rqstp->rq_res.head->iov_base + rqstp->rq_res.head->iov_len;
	mic.data = (u8 *)(p + 1);
	maj_stat = gss_get_mic(ctx_id, &verf_data, &mic);
	if (maj_stat != GSS_S_COMPLETE)
		return -1;
	*p++ = htonl(mic.len);
	memset((u8 *)p + mic.len, 0, round_up_to_quad(mic.len) - mic.len);
	p += XDR_QUADLEN(mic.len);
	if (!xdr_ressize_check(rqstp, p))
		return -1;
	return 0;
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

int
svcauth_gss_register_pseudoflavor(u32 pseudoflavor, char * name)
{
	struct gss_domain	*new;
	struct auth_domain	*test;
	int			stat = -ENOMEM;

	new = kmalloc(sizeof(*new), GFP_KERNEL);
	if (!new)
		goto out;
	kref_init(&new->h.ref);
	new->h.name = kmalloc(strlen(name) + 1, GFP_KERNEL);
	if (!new->h.name)
		goto out_free_dom;
	strcpy(new->h.name, name);
	new->h.flavour = &svcauthops_gss;
	new->pseudoflavor = pseudoflavor;

	test = auth_domain_lookup(name, &new->h);
	if (test != &new->h) { /* XXX Duplicate registration? */
		auth_domain_put(&new->h);
		/* dangling ref-count... */
		goto out;
	}
	return 0;

out_free_dom:
	kfree(new);
out:
	return stat;
}

EXPORT_SYMBOL(svcauth_gss_register_pseudoflavor);

static inline int
read_u32_from_xdr_buf(struct xdr_buf *buf, int base, u32 *obj)
{
	u32     raw;
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
unwrap_integ_data(struct xdr_buf *buf, u32 seq, struct gss_ctx *ctx)
{
	int stat = -EINVAL;
	u32 integ_len, maj_stat;
	struct xdr_netobj mic;
	struct xdr_buf integ_buf;

	integ_len = ntohl(svc_getu32(&buf->head[0]));
	if (integ_len & 3)
		goto out;
	if (integ_len > buf->len)
		goto out;
	if (xdr_buf_subsegment(buf, &integ_buf, 0, integ_len))
		BUG();
	/* copy out mic... */
	if (read_u32_from_xdr_buf(buf, integ_len, &mic.len))
		BUG();
	if (mic.len > RPC_MAX_AUTH_SIZE)
		goto out;
	mic.data = kmalloc(mic.len, GFP_KERNEL);
	if (!mic.data)
		goto out;
	if (read_bytes_from_xdr_buf(buf, integ_len + 4, mic.data, mic.len))
		goto out;
	maj_stat = gss_verify_mic(ctx, &integ_buf, &mic);
	if (maj_stat != GSS_S_COMPLETE)
		goto out;
	if (ntohl(svc_getu32(&buf->head[0])) != seq)
		goto out;
	stat = 0;
out:
	return stat;
}

struct gss_svc_data {
	/* decoded gss client cred: */
	struct rpc_gss_wire_cred	clcred;
	/* pointer to the beginning of the procedure-specific results,
	 * which may be encrypted/checksummed in svcauth_gss_release: */
	u32				*body_start;
	struct rsc			*rsci;
};

static int
svcauth_gss_set_client(struct svc_rqst *rqstp)
{
	struct gss_svc_data *svcdata = rqstp->rq_auth_data;
	struct rsc *rsci = svcdata->rsci;
	struct rpc_gss_wire_cred *gc = &svcdata->clcred;

	rqstp->rq_client = find_gss_auth_domain(rsci->mechctx, gc->gc_svc);
	if (rqstp->rq_client == NULL)
		return SVC_DENIED;
	return SVC_OK;
}

static inline int
gss_write_init_verf(struct svc_rqst *rqstp, struct rsi *rsip)
{
	struct rsc *rsci;

	if (rsip->major_status != GSS_S_COMPLETE)
		return gss_write_null_verf(rqstp);
	rsci = gss_svc_searchbyctx(&rsip->out_handle);
	if (rsci == NULL) {
		rsip->major_status = GSS_S_NO_CONTEXT;
		return gss_write_null_verf(rqstp);
	}
	return gss_write_verf(rqstp, rsci->mechctx, GSS_SEQ_WIN);
}

/*
 * Accept an rpcsec packet.
 * If context establishment, punt to user space
 * If data exchange, verify/decrypt
 * If context destruction, handle here
 * In the context establishment and destruction case we encode
 * response here and return SVC_COMPLETE.
 */
static int
svcauth_gss_accept(struct svc_rqst *rqstp, u32 *authp)
{
	struct kvec	*argv = &rqstp->rq_arg.head[0];
	struct kvec	*resv = &rqstp->rq_res.head[0];
	u32		crlen;
	struct xdr_netobj tmpobj;
	struct gss_svc_data *svcdata = rqstp->rq_auth_data;
	struct rpc_gss_wire_cred *gc;
	struct rsc	*rsci = NULL;
	struct rsi	*rsip, rsikey;
	u32		*rpcstart;
	u32		*reject_stat = resv->iov_base + resv->iov_len;
	int		ret;

	dprintk("RPC:      svcauth_gss: argv->iov_len = %zd\n",argv->iov_len);

	*authp = rpc_autherr_badcred;
	if (!svcdata)
		svcdata = kmalloc(sizeof(*svcdata), GFP_KERNEL);
	if (!svcdata)
		goto auth_err;
	rqstp->rq_auth_data = svcdata;
	svcdata->body_start = NULL;
	svcdata->rsci = NULL;
	gc = &svcdata->clcred;

	/* start of rpc packet is 7 u32's back from here:
	 * xid direction rpcversion prog vers proc flavour
	 */
	rpcstart = argv->iov_base;
	rpcstart -= 7;

	/* credential is:
	 *   version(==1), proc(0,1,2,3), seq, service (1,2,3), handle
	 * at least 5 u32s, and is preceeded by length, so that makes 6.
	 */

	if (argv->iov_len < 5 * 4)
		goto auth_err;
	crlen = ntohl(svc_getu32(argv));
	if (ntohl(svc_getu32(argv)) != RPC_GSS_VERSION)
		goto auth_err;
	gc->gc_proc = ntohl(svc_getu32(argv));
	gc->gc_seq = ntohl(svc_getu32(argv));
	gc->gc_svc = ntohl(svc_getu32(argv));
	if (svc_safe_getnetobj(argv, &gc->gc_ctx))
		goto auth_err;
	if (crlen != round_up_to_quad(gc->gc_ctx.len) + 5 * 4)
		goto auth_err;

	if ((gc->gc_proc != RPC_GSS_PROC_DATA) && (rqstp->rq_proc != 0))
		goto auth_err;

	/*
	 * We've successfully parsed the credential. Let's check out the
	 * verifier.  An AUTH_NULL verifier is allowed (and required) for
	 * INIT and CONTINUE_INIT requests. AUTH_RPCSEC_GSS is required for
	 * PROC_DATA and PROC_DESTROY.
	 *
	 * AUTH_NULL verifier is 0 (AUTH_NULL), 0 (length).
	 * AUTH_RPCSEC_GSS verifier is:
	 *   6 (AUTH_RPCSEC_GSS), length, checksum.
	 * checksum is calculated over rpcheader from xid up to here.
	 */
	*authp = rpc_autherr_badverf;
	switch (gc->gc_proc) {
	case RPC_GSS_PROC_INIT:
	case RPC_GSS_PROC_CONTINUE_INIT:
		if (argv->iov_len < 2 * 4)
			goto auth_err;
		if (ntohl(svc_getu32(argv)) != RPC_AUTH_NULL)
			goto auth_err;
		if (ntohl(svc_getu32(argv)) != 0)
			goto auth_err;
		break;
	case RPC_GSS_PROC_DATA:
	case RPC_GSS_PROC_DESTROY:
		*authp = rpcsec_gsserr_credproblem;
		rsci = gss_svc_searchbyctx(&gc->gc_ctx);
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
	case RPC_GSS_PROC_INIT:
	case RPC_GSS_PROC_CONTINUE_INIT:
		*authp = rpc_autherr_badcred;
		if (gc->gc_proc == RPC_GSS_PROC_INIT && gc->gc_ctx.len != 0)
			goto auth_err;
		memset(&rsikey, 0, sizeof(rsikey));
		if (dup_netobj(&rsikey.in_handle, &gc->gc_ctx))
			goto drop;
		*authp = rpc_autherr_badverf;
		if (svc_safe_getnetobj(argv, &tmpobj)) {
			kfree(rsikey.in_handle.data);
			goto auth_err;
		}
		if (dup_netobj(&rsikey.in_token, &tmpobj)) {
			kfree(rsikey.in_handle.data);
			goto drop;
		}

		rsip = rsi_lookup(&rsikey);
		rsi_free(&rsikey);
		if (!rsip) {
			goto drop;
		}
		switch(cache_check(&rsi_cache, &rsip->h, &rqstp->rq_chandle)) {
		case -EAGAIN:
			goto drop;
		case -ENOENT:
			goto drop;
		case 0:
			if (gss_write_init_verf(rqstp, rsip))
				goto drop;
			if (resv->iov_len + 4 > PAGE_SIZE)
				goto drop;
			svc_putu32(resv, rpc_success);
			if (svc_safe_putnetobj(resv, &rsip->out_handle))
				goto drop;
			if (resv->iov_len + 3 * 4 > PAGE_SIZE)
				goto drop;
			svc_putu32(resv, htonl(rsip->major_status));
			svc_putu32(resv, htonl(rsip->minor_status));
			svc_putu32(resv, htonl(GSS_SEQ_WIN));
			if (svc_safe_putnetobj(resv, &rsip->out_token))
				goto drop;
			rqstp->rq_client = NULL;
		}
		goto complete;
	case RPC_GSS_PROC_DESTROY:
		set_bit(CACHE_NEGATIVE, &rsci->h.flags);
		if (resv->iov_len + 4 > PAGE_SIZE)
			goto drop;
		svc_putu32(resv, rpc_success);
		goto complete;
	case RPC_GSS_PROC_DATA:
		*authp = rpcsec_gsserr_ctxproblem;
		if (gss_write_verf(rqstp, rsci->mechctx, gc->gc_seq))
			goto auth_err;
		rqstp->rq_cred = rsci->cred;
		get_group_info(rsci->cred.cr_group_info);
		*authp = rpc_autherr_badcred;
		switch (gc->gc_svc) {
		case RPC_GSS_SVC_NONE:
			break;
		case RPC_GSS_SVC_INTEGRITY:
			if (unwrap_integ_data(&rqstp->rq_arg,
					gc->gc_seq, rsci->mechctx))
				goto auth_err;
			/* placeholders for length and seq. number: */
			svcdata->body_start = resv->iov_base + resv->iov_len;
			svc_putu32(resv, 0);
			svc_putu32(resv, 0);
			break;
		case RPC_GSS_SVC_PRIVACY:
			/* currently unsupported */
		default:
			goto auth_err;
		}
		svcdata->rsci = rsci;
		cache_get(&rsci->h);
		ret = SVC_OK;
		goto out;
	}
auth_err:
	/* Restore write pointer to original value: */
	xdr_ressize_check(rqstp, reject_stat);
	ret = SVC_DENIED;
	goto out;
complete:
	ret = SVC_COMPLETE;
	goto out;
drop:
	ret = SVC_DROP;
out:
	if (rsci)
		cache_put(&rsci->h, &rsc_cache);
	return ret;
}

static int
svcauth_gss_release(struct svc_rqst *rqstp)
{
	struct gss_svc_data *gsd = (struct gss_svc_data *)rqstp->rq_auth_data;
	struct rpc_gss_wire_cred *gc = &gsd->clcred;
	struct xdr_buf *resbuf = &rqstp->rq_res;
	struct xdr_buf integ_buf;
	struct xdr_netobj mic;
	struct kvec *resv;
	u32 *p;
	int integ_offset, integ_len;
	int stat = -EINVAL;

	if (gc->gc_proc != RPC_GSS_PROC_DATA)
		goto out;
	/* Release can be called twice, but we only wrap once. */
	if (gsd->body_start == NULL)
		goto out;
	/* normally not set till svc_send, but we need it here: */
	resbuf->len = resbuf->head[0].iov_len
		+ resbuf->page_len + resbuf->tail[0].iov_len;
	switch (gc->gc_svc) {
	case RPC_GSS_SVC_NONE:
		break;
	case RPC_GSS_SVC_INTEGRITY:
		p = gsd->body_start;
		gsd->body_start = NULL;
		/* move accept_stat to right place: */
		memcpy(p, p + 2, 4);
		/* don't wrap in failure case: */
		/* Note: counting on not getting here if call was not even
		 * accepted! */
		if (*p != rpc_success) {
			resbuf->head[0].iov_len -= 2 * 4;
			goto out;
		}
		p++;
		integ_offset = (u8 *)(p + 1) - (u8 *)resbuf->head[0].iov_base;
		integ_len = resbuf->len - integ_offset;
		BUG_ON(integ_len % 4);
		*p++ = htonl(integ_len);
		*p++ = htonl(gc->gc_seq);
		if (xdr_buf_subsegment(resbuf, &integ_buf, integ_offset,
					integ_len))
			BUG();
		if (resbuf->page_len == 0
			&& resbuf->tail[0].iov_len + RPC_MAX_AUTH_SIZE
				< PAGE_SIZE) {
			BUG_ON(resbuf->tail[0].iov_len);
			/* Use head for everything */
			resv = &resbuf->head[0];
		} else if (resbuf->tail[0].iov_base == NULL) {
			/* copied from nfsd4_encode_read */
			svc_take_page(rqstp);
			resbuf->tail[0].iov_base = page_address(rqstp
					->rq_respages[rqstp->rq_resused-1]);
			rqstp->rq_restailpage = rqstp->rq_resused-1;
			resbuf->tail[0].iov_len = 0;
			resv = &resbuf->tail[0];
		} else {
			resv = &resbuf->tail[0];
		}
		mic.data = (u8 *)resv->iov_base + resv->iov_len + 4;
		if (gss_get_mic(gsd->rsci->mechctx, &integ_buf, &mic))
			goto out_err;
		svc_putu32(resv, htonl(mic.len));
		memset(mic.data + mic.len, 0,
				round_up_to_quad(mic.len) - mic.len);
		resv->iov_len += XDR_QUADLEN(mic.len) << 2;
		/* not strictly required: */
		resbuf->len += XDR_QUADLEN(mic.len) << 2;
		BUG_ON(resv->iov_len > PAGE_SIZE);
		break;
	case RPC_GSS_SVC_PRIVACY:
	default:
		goto out_err;
	}

out:
	stat = 0;
out_err:
	if (rqstp->rq_client)
		auth_domain_put(rqstp->rq_client);
	rqstp->rq_client = NULL;
	if (rqstp->rq_cred.cr_group_info)
		put_group_info(rqstp->rq_cred.cr_group_info);
	rqstp->rq_cred.cr_group_info = NULL;
	if (gsd->rsci)
		cache_put(&gsd->rsci->h, &rsc_cache);
	gsd->rsci = NULL;

	return stat;
}

static void
svcauth_gss_domain_release(struct auth_domain *dom)
{
	struct gss_domain *gd = container_of(dom, struct gss_domain, h);

	kfree(dom->name);
	kfree(gd);
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

int
gss_svc_init(void)
{
	int rv = svc_auth_register(RPC_AUTH_GSS, &svcauthops_gss);
	if (rv == 0) {
		cache_register(&rsc_cache);
		cache_register(&rsi_cache);
	}
	return rv;
}

void
gss_svc_shutdown(void)
{
	if (cache_unregister(&rsc_cache))
		printk(KERN_ERR "auth_rpcgss: failed to unregister rsc cache\n");
	if (cache_unregister(&rsi_cache))
		printk(KERN_ERR "auth_rpcgss: failed to unregister rsi cache\n");
	svc_auth_unregister(RPC_AUTH_GSS);
}
