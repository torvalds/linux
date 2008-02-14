/* xfrm_user.c: User interface to configure xfrm engine.
 *
 * Copyright (C) 2002 David S. Miller (davem@redhat.com)
 *
 * Changes:
 *	Mitsuru KANDA @USAGI
 * 	Kazunori MIYAZAWA @USAGI
 * 	Kunihiro Ishiguro <kunihiro@ipinfusion.com>
 * 		IPv6 support
 *
 */

#include <linux/crypto.h>
#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/types.h>
#include <linux/slab.h>
#include <linux/socket.h>
#include <linux/string.h>
#include <linux/net.h>
#include <linux/skbuff.h>
#include <linux/pfkeyv2.h>
#include <linux/ipsec.h>
#include <linux/init.h>
#include <linux/security.h>
#include <net/sock.h>
#include <net/xfrm.h>
#include <net/netlink.h>
#include <asm/uaccess.h>
#if defined(CONFIG_IPV6) || defined(CONFIG_IPV6_MODULE)
#include <linux/in6.h>
#endif

static inline int aead_len(struct xfrm_algo_aead *alg)
{
	return sizeof(*alg) + ((alg->alg_key_len + 7) / 8);
}

static int verify_one_alg(struct nlattr **attrs, enum xfrm_attr_type_t type)
{
	struct nlattr *rt = attrs[type];
	struct xfrm_algo *algp;

	if (!rt)
		return 0;

	algp = nla_data(rt);
	if (nla_len(rt) < xfrm_alg_len(algp))
		return -EINVAL;

	switch (type) {
	case XFRMA_ALG_AUTH:
		if (!algp->alg_key_len &&
		    strcmp(algp->alg_name, "digest_null") != 0)
			return -EINVAL;
		break;

	case XFRMA_ALG_CRYPT:
		if (!algp->alg_key_len &&
		    strcmp(algp->alg_name, "cipher_null") != 0)
			return -EINVAL;
		break;

	case XFRMA_ALG_COMP:
		/* Zero length keys are legal.  */
		break;

	default:
		return -EINVAL;
	}

	algp->alg_name[CRYPTO_MAX_ALG_NAME - 1] = '\0';
	return 0;
}

static int verify_aead(struct nlattr **attrs)
{
	struct nlattr *rt = attrs[XFRMA_ALG_AEAD];
	struct xfrm_algo_aead *algp;

	if (!rt)
		return 0;

	algp = nla_data(rt);
	if (nla_len(rt) < aead_len(algp))
		return -EINVAL;

	algp->alg_name[CRYPTO_MAX_ALG_NAME - 1] = '\0';
	return 0;
}

static void verify_one_addr(struct nlattr **attrs, enum xfrm_attr_type_t type,
			   xfrm_address_t **addrp)
{
	struct nlattr *rt = attrs[type];

	if (rt && addrp)
		*addrp = nla_data(rt);
}

static inline int verify_sec_ctx_len(struct nlattr **attrs)
{
	struct nlattr *rt = attrs[XFRMA_SEC_CTX];
	struct xfrm_user_sec_ctx *uctx;

	if (!rt)
		return 0;

	uctx = nla_data(rt);
	if (uctx->len != (sizeof(struct xfrm_user_sec_ctx) + uctx->ctx_len))
		return -EINVAL;

	return 0;
}


static int verify_newsa_info(struct xfrm_usersa_info *p,
			     struct nlattr **attrs)
{
	int err;

	err = -EINVAL;
	switch (p->family) {
	case AF_INET:
		break;

	case AF_INET6:
#if defined(CONFIG_IPV6) || defined(CONFIG_IPV6_MODULE)
		break;
#else
		err = -EAFNOSUPPORT;
		goto out;
#endif

	default:
		goto out;
	}

	err = -EINVAL;
	switch (p->id.proto) {
	case IPPROTO_AH:
		if (!attrs[XFRMA_ALG_AUTH]	||
		    attrs[XFRMA_ALG_AEAD]	||
		    attrs[XFRMA_ALG_CRYPT]	||
		    attrs[XFRMA_ALG_COMP])
			goto out;
		break;

	case IPPROTO_ESP:
		if (attrs[XFRMA_ALG_COMP])
			goto out;
		if (!attrs[XFRMA_ALG_AUTH] &&
		    !attrs[XFRMA_ALG_CRYPT] &&
		    !attrs[XFRMA_ALG_AEAD])
			goto out;
		if ((attrs[XFRMA_ALG_AUTH] ||
		     attrs[XFRMA_ALG_CRYPT]) &&
		    attrs[XFRMA_ALG_AEAD])
			goto out;
		break;

	case IPPROTO_COMP:
		if (!attrs[XFRMA_ALG_COMP]	||
		    attrs[XFRMA_ALG_AEAD]	||
		    attrs[XFRMA_ALG_AUTH]	||
		    attrs[XFRMA_ALG_CRYPT])
			goto out;
		break;

#if defined(CONFIG_IPV6) || defined(CONFIG_IPV6_MODULE)
	case IPPROTO_DSTOPTS:
	case IPPROTO_ROUTING:
		if (attrs[XFRMA_ALG_COMP]	||
		    attrs[XFRMA_ALG_AUTH]	||
		    attrs[XFRMA_ALG_AEAD]	||
		    attrs[XFRMA_ALG_CRYPT]	||
		    attrs[XFRMA_ENCAP]		||
		    attrs[XFRMA_SEC_CTX]	||
		    !attrs[XFRMA_COADDR])
			goto out;
		break;
#endif

	default:
		goto out;
	}

	if ((err = verify_aead(attrs)))
		goto out;
	if ((err = verify_one_alg(attrs, XFRMA_ALG_AUTH)))
		goto out;
	if ((err = verify_one_alg(attrs, XFRMA_ALG_CRYPT)))
		goto out;
	if ((err = verify_one_alg(attrs, XFRMA_ALG_COMP)))
		goto out;
	if ((err = verify_sec_ctx_len(attrs)))
		goto out;

	err = -EINVAL;
	switch (p->mode) {
	case XFRM_MODE_TRANSPORT:
	case XFRM_MODE_TUNNEL:
	case XFRM_MODE_ROUTEOPTIMIZATION:
	case XFRM_MODE_BEET:
		break;

	default:
		goto out;
	}

	err = 0;

out:
	return err;
}

static int attach_one_algo(struct xfrm_algo **algpp, u8 *props,
			   struct xfrm_algo_desc *(*get_byname)(char *, int),
			   struct nlattr *rta)
{
	struct xfrm_algo *p, *ualg;
	struct xfrm_algo_desc *algo;

	if (!rta)
		return 0;

	ualg = nla_data(rta);

	algo = get_byname(ualg->alg_name, 1);
	if (!algo)
		return -ENOSYS;
	*props = algo->desc.sadb_alg_id;

	p = kmemdup(ualg, xfrm_alg_len(ualg), GFP_KERNEL);
	if (!p)
		return -ENOMEM;

	strcpy(p->alg_name, algo->name);
	*algpp = p;
	return 0;
}

static int attach_aead(struct xfrm_algo_aead **algpp, u8 *props,
		       struct nlattr *rta)
{
	struct xfrm_algo_aead *p, *ualg;
	struct xfrm_algo_desc *algo;

	if (!rta)
		return 0;

	ualg = nla_data(rta);

	algo = xfrm_aead_get_byname(ualg->alg_name, ualg->alg_icv_len, 1);
	if (!algo)
		return -ENOSYS;
	*props = algo->desc.sadb_alg_id;

	p = kmemdup(ualg, aead_len(ualg), GFP_KERNEL);
	if (!p)
		return -ENOMEM;

	strcpy(p->alg_name, algo->name);
	*algpp = p;
	return 0;
}

static inline int xfrm_user_sec_ctx_size(struct xfrm_sec_ctx *xfrm_ctx)
{
	int len = 0;

	if (xfrm_ctx) {
		len += sizeof(struct xfrm_user_sec_ctx);
		len += xfrm_ctx->ctx_len;
	}
	return len;
}

static void copy_from_user_state(struct xfrm_state *x, struct xfrm_usersa_info *p)
{
	memcpy(&x->id, &p->id, sizeof(x->id));
	memcpy(&x->sel, &p->sel, sizeof(x->sel));
	memcpy(&x->lft, &p->lft, sizeof(x->lft));
	x->props.mode = p->mode;
	x->props.replay_window = p->replay_window;
	x->props.reqid = p->reqid;
	x->props.family = p->family;
	memcpy(&x->props.saddr, &p->saddr, sizeof(x->props.saddr));
	x->props.flags = p->flags;

	/*
	 * Set inner address family if the KM left it as zero.
	 * See comment in validate_tmpl.
	 */
	if (!x->sel.family)
		x->sel.family = p->family;
}

/*
 * someday when pfkey also has support, we could have the code
 * somehow made shareable and move it to xfrm_state.c - JHS
 *
*/
static void xfrm_update_ae_params(struct xfrm_state *x, struct nlattr **attrs)
{
	struct nlattr *rp = attrs[XFRMA_REPLAY_VAL];
	struct nlattr *lt = attrs[XFRMA_LTIME_VAL];
	struct nlattr *et = attrs[XFRMA_ETIMER_THRESH];
	struct nlattr *rt = attrs[XFRMA_REPLAY_THRESH];

	if (rp) {
		struct xfrm_replay_state *replay;
		replay = nla_data(rp);
		memcpy(&x->replay, replay, sizeof(*replay));
		memcpy(&x->preplay, replay, sizeof(*replay));
	}

	if (lt) {
		struct xfrm_lifetime_cur *ltime;
		ltime = nla_data(lt);
		x->curlft.bytes = ltime->bytes;
		x->curlft.packets = ltime->packets;
		x->curlft.add_time = ltime->add_time;
		x->curlft.use_time = ltime->use_time;
	}

	if (et)
		x->replay_maxage = nla_get_u32(et);

	if (rt)
		x->replay_maxdiff = nla_get_u32(rt);
}

static struct xfrm_state *xfrm_state_construct(struct xfrm_usersa_info *p,
					       struct nlattr **attrs,
					       int *errp)
{
	struct xfrm_state *x = xfrm_state_alloc();
	int err = -ENOMEM;

	if (!x)
		goto error_no_put;

	copy_from_user_state(x, p);

	if ((err = attach_aead(&x->aead, &x->props.ealgo,
			       attrs[XFRMA_ALG_AEAD])))
		goto error;
	if ((err = attach_one_algo(&x->aalg, &x->props.aalgo,
				   xfrm_aalg_get_byname,
				   attrs[XFRMA_ALG_AUTH])))
		goto error;
	if ((err = attach_one_algo(&x->ealg, &x->props.ealgo,
				   xfrm_ealg_get_byname,
				   attrs[XFRMA_ALG_CRYPT])))
		goto error;
	if ((err = attach_one_algo(&x->calg, &x->props.calgo,
				   xfrm_calg_get_byname,
				   attrs[XFRMA_ALG_COMP])))
		goto error;

	if (attrs[XFRMA_ENCAP]) {
		x->encap = kmemdup(nla_data(attrs[XFRMA_ENCAP]),
				   sizeof(*x->encap), GFP_KERNEL);
		if (x->encap == NULL)
			goto error;
	}

	if (attrs[XFRMA_COADDR]) {
		x->coaddr = kmemdup(nla_data(attrs[XFRMA_COADDR]),
				    sizeof(*x->coaddr), GFP_KERNEL);
		if (x->coaddr == NULL)
			goto error;
	}

	err = xfrm_init_state(x);
	if (err)
		goto error;

	if (attrs[XFRMA_SEC_CTX] &&
	    security_xfrm_state_alloc(x, nla_data(attrs[XFRMA_SEC_CTX])))
		goto error;

	x->km.seq = p->seq;
	x->replay_maxdiff = sysctl_xfrm_aevent_rseqth;
	/* sysctl_xfrm_aevent_etime is in 100ms units */
	x->replay_maxage = (sysctl_xfrm_aevent_etime*HZ)/XFRM_AE_ETH_M;
	x->preplay.bitmap = 0;
	x->preplay.seq = x->replay.seq+x->replay_maxdiff;
	x->preplay.oseq = x->replay.oseq +x->replay_maxdiff;

	/* override default values from above */

	xfrm_update_ae_params(x, attrs);

	return x;

error:
	x->km.state = XFRM_STATE_DEAD;
	xfrm_state_put(x);
error_no_put:
	*errp = err;
	return NULL;
}

static int xfrm_add_sa(struct sk_buff *skb, struct nlmsghdr *nlh,
		struct nlattr **attrs)
{
	struct xfrm_usersa_info *p = nlmsg_data(nlh);
	struct xfrm_state *x;
	int err;
	struct km_event c;

	err = verify_newsa_info(p, attrs);
	if (err)
		return err;

	x = xfrm_state_construct(p, attrs, &err);
	if (!x)
		return err;

	xfrm_state_hold(x);
	if (nlh->nlmsg_type == XFRM_MSG_NEWSA)
		err = xfrm_state_add(x);
	else
		err = xfrm_state_update(x);

	xfrm_audit_state_add(x, err ? 0 : 1, NETLINK_CB(skb).loginuid,
			     NETLINK_CB(skb).sid);

	if (err < 0) {
		x->km.state = XFRM_STATE_DEAD;
		__xfrm_state_put(x);
		goto out;
	}

	c.seq = nlh->nlmsg_seq;
	c.pid = nlh->nlmsg_pid;
	c.event = nlh->nlmsg_type;

	km_state_notify(x, &c);
out:
	xfrm_state_put(x);
	return err;
}

static struct xfrm_state *xfrm_user_state_lookup(struct xfrm_usersa_id *p,
						 struct nlattr **attrs,
						 int *errp)
{
	struct xfrm_state *x = NULL;
	int err;

	if (xfrm_id_proto_match(p->proto, IPSEC_PROTO_ANY)) {
		err = -ESRCH;
		x = xfrm_state_lookup(&p->daddr, p->spi, p->proto, p->family);
	} else {
		xfrm_address_t *saddr = NULL;

		verify_one_addr(attrs, XFRMA_SRCADDR, &saddr);
		if (!saddr) {
			err = -EINVAL;
			goto out;
		}

		err = -ESRCH;
		x = xfrm_state_lookup_byaddr(&p->daddr, saddr, p->proto,
					     p->family);
	}

 out:
	if (!x && errp)
		*errp = err;
	return x;
}

static int xfrm_del_sa(struct sk_buff *skb, struct nlmsghdr *nlh,
		struct nlattr **attrs)
{
	struct xfrm_state *x;
	int err = -ESRCH;
	struct km_event c;
	struct xfrm_usersa_id *p = nlmsg_data(nlh);

	x = xfrm_user_state_lookup(p, attrs, &err);
	if (x == NULL)
		return err;

	if ((err = security_xfrm_state_delete(x)) != 0)
		goto out;

	if (xfrm_state_kern(x)) {
		err = -EPERM;
		goto out;
	}

	err = xfrm_state_delete(x);

	if (err < 0)
		goto out;

	c.seq = nlh->nlmsg_seq;
	c.pid = nlh->nlmsg_pid;
	c.event = nlh->nlmsg_type;
	km_state_notify(x, &c);

out:
	xfrm_audit_state_delete(x, err ? 0 : 1, NETLINK_CB(skb).loginuid,
				NETLINK_CB(skb).sid);
	xfrm_state_put(x);
	return err;
}

static void copy_to_user_state(struct xfrm_state *x, struct xfrm_usersa_info *p)
{
	memcpy(&p->id, &x->id, sizeof(p->id));
	memcpy(&p->sel, &x->sel, sizeof(p->sel));
	memcpy(&p->lft, &x->lft, sizeof(p->lft));
	memcpy(&p->curlft, &x->curlft, sizeof(p->curlft));
	memcpy(&p->stats, &x->stats, sizeof(p->stats));
	memcpy(&p->saddr, &x->props.saddr, sizeof(p->saddr));
	p->mode = x->props.mode;
	p->replay_window = x->props.replay_window;
	p->reqid = x->props.reqid;
	p->family = x->props.family;
	p->flags = x->props.flags;
	p->seq = x->km.seq;
}

struct xfrm_dump_info {
	struct sk_buff *in_skb;
	struct sk_buff *out_skb;
	u32 nlmsg_seq;
	u16 nlmsg_flags;
	int start_idx;
	int this_idx;
};

static int copy_sec_ctx(struct xfrm_sec_ctx *s, struct sk_buff *skb)
{
	struct xfrm_user_sec_ctx *uctx;
	struct nlattr *attr;
	int ctx_size = sizeof(*uctx) + s->ctx_len;

	attr = nla_reserve(skb, XFRMA_SEC_CTX, ctx_size);
	if (attr == NULL)
		return -EMSGSIZE;

	uctx = nla_data(attr);
	uctx->exttype = XFRMA_SEC_CTX;
	uctx->len = ctx_size;
	uctx->ctx_doi = s->ctx_doi;
	uctx->ctx_alg = s->ctx_alg;
	uctx->ctx_len = s->ctx_len;
	memcpy(uctx + 1, s->ctx_str, s->ctx_len);

	return 0;
}

/* Don't change this without updating xfrm_sa_len! */
static int copy_to_user_state_extra(struct xfrm_state *x,
				    struct xfrm_usersa_info *p,
				    struct sk_buff *skb)
{
	copy_to_user_state(x, p);

	if (x->coaddr)
		NLA_PUT(skb, XFRMA_COADDR, sizeof(*x->coaddr), x->coaddr);

	if (x->lastused)
		NLA_PUT_U64(skb, XFRMA_LASTUSED, x->lastused);

	if (x->aead)
		NLA_PUT(skb, XFRMA_ALG_AEAD, aead_len(x->aead), x->aead);
	if (x->aalg)
		NLA_PUT(skb, XFRMA_ALG_AUTH, xfrm_alg_len(x->aalg), x->aalg);
	if (x->ealg)
		NLA_PUT(skb, XFRMA_ALG_CRYPT, xfrm_alg_len(x->ealg), x->ealg);
	if (x->calg)
		NLA_PUT(skb, XFRMA_ALG_COMP, sizeof(*(x->calg)), x->calg);

	if (x->encap)
		NLA_PUT(skb, XFRMA_ENCAP, sizeof(*x->encap), x->encap);

	if (x->security && copy_sec_ctx(x->security, skb) < 0)
		goto nla_put_failure;

	return 0;

nla_put_failure:
	return -EMSGSIZE;
}

static int dump_one_state(struct xfrm_state *x, int count, void *ptr)
{
	struct xfrm_dump_info *sp = ptr;
	struct sk_buff *in_skb = sp->in_skb;
	struct sk_buff *skb = sp->out_skb;
	struct xfrm_usersa_info *p;
	struct nlmsghdr *nlh;
	int err;

	if (sp->this_idx < sp->start_idx)
		goto out;

	nlh = nlmsg_put(skb, NETLINK_CB(in_skb).pid, sp->nlmsg_seq,
			XFRM_MSG_NEWSA, sizeof(*p), sp->nlmsg_flags);
	if (nlh == NULL)
		return -EMSGSIZE;

	p = nlmsg_data(nlh);

	err = copy_to_user_state_extra(x, p, skb);
	if (err)
		goto nla_put_failure;

	nlmsg_end(skb, nlh);
out:
	sp->this_idx++;
	return 0;

nla_put_failure:
	nlmsg_cancel(skb, nlh);
	return err;
}

static int xfrm_dump_sa(struct sk_buff *skb, struct netlink_callback *cb)
{
	struct xfrm_dump_info info;

	info.in_skb = cb->skb;
	info.out_skb = skb;
	info.nlmsg_seq = cb->nlh->nlmsg_seq;
	info.nlmsg_flags = NLM_F_MULTI;
	info.this_idx = 0;
	info.start_idx = cb->args[0];
	(void) xfrm_state_walk(0, dump_one_state, &info);
	cb->args[0] = info.this_idx;

	return skb->len;
}

static struct sk_buff *xfrm_state_netlink(struct sk_buff *in_skb,
					  struct xfrm_state *x, u32 seq)
{
	struct xfrm_dump_info info;
	struct sk_buff *skb;

	skb = nlmsg_new(NLMSG_DEFAULT_SIZE, GFP_ATOMIC);
	if (!skb)
		return ERR_PTR(-ENOMEM);

	info.in_skb = in_skb;
	info.out_skb = skb;
	info.nlmsg_seq = seq;
	info.nlmsg_flags = 0;
	info.this_idx = info.start_idx = 0;

	if (dump_one_state(x, 0, &info)) {
		kfree_skb(skb);
		return NULL;
	}

	return skb;
}

static inline size_t xfrm_spdinfo_msgsize(void)
{
	return NLMSG_ALIGN(4)
	       + nla_total_size(sizeof(struct xfrmu_spdinfo))
	       + nla_total_size(sizeof(struct xfrmu_spdhinfo));
}

static int build_spdinfo(struct sk_buff *skb, u32 pid, u32 seq, u32 flags)
{
	struct xfrmk_spdinfo si;
	struct xfrmu_spdinfo spc;
	struct xfrmu_spdhinfo sph;
	struct nlmsghdr *nlh;
	u32 *f;

	nlh = nlmsg_put(skb, pid, seq, XFRM_MSG_NEWSPDINFO, sizeof(u32), 0);
	if (nlh == NULL) /* shouldnt really happen ... */
		return -EMSGSIZE;

	f = nlmsg_data(nlh);
	*f = flags;
	xfrm_spd_getinfo(&si);
	spc.incnt = si.incnt;
	spc.outcnt = si.outcnt;
	spc.fwdcnt = si.fwdcnt;
	spc.inscnt = si.inscnt;
	spc.outscnt = si.outscnt;
	spc.fwdscnt = si.fwdscnt;
	sph.spdhcnt = si.spdhcnt;
	sph.spdhmcnt = si.spdhmcnt;

	NLA_PUT(skb, XFRMA_SPD_INFO, sizeof(spc), &spc);
	NLA_PUT(skb, XFRMA_SPD_HINFO, sizeof(sph), &sph);

	return nlmsg_end(skb, nlh);

nla_put_failure:
	nlmsg_cancel(skb, nlh);
	return -EMSGSIZE;
}

static int xfrm_get_spdinfo(struct sk_buff *skb, struct nlmsghdr *nlh,
		struct nlattr **attrs)
{
	struct sk_buff *r_skb;
	u32 *flags = nlmsg_data(nlh);
	u32 spid = NETLINK_CB(skb).pid;
	u32 seq = nlh->nlmsg_seq;

	r_skb = nlmsg_new(xfrm_spdinfo_msgsize(), GFP_ATOMIC);
	if (r_skb == NULL)
		return -ENOMEM;

	if (build_spdinfo(r_skb, spid, seq, *flags) < 0)
		BUG();

	return nlmsg_unicast(xfrm_nl, r_skb, spid);
}

static inline size_t xfrm_sadinfo_msgsize(void)
{
	return NLMSG_ALIGN(4)
	       + nla_total_size(sizeof(struct xfrmu_sadhinfo))
	       + nla_total_size(4); /* XFRMA_SAD_CNT */
}

static int build_sadinfo(struct sk_buff *skb, u32 pid, u32 seq, u32 flags)
{
	struct xfrmk_sadinfo si;
	struct xfrmu_sadhinfo sh;
	struct nlmsghdr *nlh;
	u32 *f;

	nlh = nlmsg_put(skb, pid, seq, XFRM_MSG_NEWSADINFO, sizeof(u32), 0);
	if (nlh == NULL) /* shouldnt really happen ... */
		return -EMSGSIZE;

	f = nlmsg_data(nlh);
	*f = flags;
	xfrm_sad_getinfo(&si);

	sh.sadhmcnt = si.sadhmcnt;
	sh.sadhcnt = si.sadhcnt;

	NLA_PUT_U32(skb, XFRMA_SAD_CNT, si.sadcnt);
	NLA_PUT(skb, XFRMA_SAD_HINFO, sizeof(sh), &sh);

	return nlmsg_end(skb, nlh);

nla_put_failure:
	nlmsg_cancel(skb, nlh);
	return -EMSGSIZE;
}

static int xfrm_get_sadinfo(struct sk_buff *skb, struct nlmsghdr *nlh,
		struct nlattr **attrs)
{
	struct sk_buff *r_skb;
	u32 *flags = nlmsg_data(nlh);
	u32 spid = NETLINK_CB(skb).pid;
	u32 seq = nlh->nlmsg_seq;

	r_skb = nlmsg_new(xfrm_sadinfo_msgsize(), GFP_ATOMIC);
	if (r_skb == NULL)
		return -ENOMEM;

	if (build_sadinfo(r_skb, spid, seq, *flags) < 0)
		BUG();

	return nlmsg_unicast(xfrm_nl, r_skb, spid);
}

static int xfrm_get_sa(struct sk_buff *skb, struct nlmsghdr *nlh,
		struct nlattr **attrs)
{
	struct xfrm_usersa_id *p = nlmsg_data(nlh);
	struct xfrm_state *x;
	struct sk_buff *resp_skb;
	int err = -ESRCH;

	x = xfrm_user_state_lookup(p, attrs, &err);
	if (x == NULL)
		goto out_noput;

	resp_skb = xfrm_state_netlink(skb, x, nlh->nlmsg_seq);
	if (IS_ERR(resp_skb)) {
		err = PTR_ERR(resp_skb);
	} else {
		err = nlmsg_unicast(xfrm_nl, resp_skb, NETLINK_CB(skb).pid);
	}
	xfrm_state_put(x);
out_noput:
	return err;
}

static int verify_userspi_info(struct xfrm_userspi_info *p)
{
	switch (p->info.id.proto) {
	case IPPROTO_AH:
	case IPPROTO_ESP:
		break;

	case IPPROTO_COMP:
		/* IPCOMP spi is 16-bits. */
		if (p->max >= 0x10000)
			return -EINVAL;
		break;

	default:
		return -EINVAL;
	}

	if (p->min > p->max)
		return -EINVAL;

	return 0;
}

static int xfrm_alloc_userspi(struct sk_buff *skb, struct nlmsghdr *nlh,
		struct nlattr **attrs)
{
	struct xfrm_state *x;
	struct xfrm_userspi_info *p;
	struct sk_buff *resp_skb;
	xfrm_address_t *daddr;
	int family;
	int err;

	p = nlmsg_data(nlh);
	err = verify_userspi_info(p);
	if (err)
		goto out_noput;

	family = p->info.family;
	daddr = &p->info.id.daddr;

	x = NULL;
	if (p->info.seq) {
		x = xfrm_find_acq_byseq(p->info.seq);
		if (x && xfrm_addr_cmp(&x->id.daddr, daddr, family)) {
			xfrm_state_put(x);
			x = NULL;
		}
	}

	if (!x)
		x = xfrm_find_acq(p->info.mode, p->info.reqid,
				  p->info.id.proto, daddr,
				  &p->info.saddr, 1,
				  family);
	err = -ENOENT;
	if (x == NULL)
		goto out_noput;

	err = xfrm_alloc_spi(x, p->min, p->max);
	if (err)
		goto out;

	resp_skb = xfrm_state_netlink(skb, x, nlh->nlmsg_seq);
	if (IS_ERR(resp_skb)) {
		err = PTR_ERR(resp_skb);
		goto out;
	}

	err = nlmsg_unicast(xfrm_nl, resp_skb, NETLINK_CB(skb).pid);

out:
	xfrm_state_put(x);
out_noput:
	return err;
}

static int verify_policy_dir(u8 dir)
{
	switch (dir) {
	case XFRM_POLICY_IN:
	case XFRM_POLICY_OUT:
	case XFRM_POLICY_FWD:
		break;

	default:
		return -EINVAL;
	}

	return 0;
}

static int verify_policy_type(u8 type)
{
	switch (type) {
	case XFRM_POLICY_TYPE_MAIN:
#ifdef CONFIG_XFRM_SUB_POLICY
	case XFRM_POLICY_TYPE_SUB:
#endif
		break;

	default:
		return -EINVAL;
	}

	return 0;
}

static int verify_newpolicy_info(struct xfrm_userpolicy_info *p)
{
	switch (p->share) {
	case XFRM_SHARE_ANY:
	case XFRM_SHARE_SESSION:
	case XFRM_SHARE_USER:
	case XFRM_SHARE_UNIQUE:
		break;

	default:
		return -EINVAL;
	}

	switch (p->action) {
	case XFRM_POLICY_ALLOW:
	case XFRM_POLICY_BLOCK:
		break;

	default:
		return -EINVAL;
	}

	switch (p->sel.family) {
	case AF_INET:
		break;

	case AF_INET6:
#if defined(CONFIG_IPV6) || defined(CONFIG_IPV6_MODULE)
		break;
#else
		return  -EAFNOSUPPORT;
#endif

	default:
		return -EINVAL;
	}

	return verify_policy_dir(p->dir);
}

static int copy_from_user_sec_ctx(struct xfrm_policy *pol, struct nlattr **attrs)
{
	struct nlattr *rt = attrs[XFRMA_SEC_CTX];
	struct xfrm_user_sec_ctx *uctx;

	if (!rt)
		return 0;

	uctx = nla_data(rt);
	return security_xfrm_policy_alloc(pol, uctx);
}

static void copy_templates(struct xfrm_policy *xp, struct xfrm_user_tmpl *ut,
			   int nr)
{
	int i;

	xp->xfrm_nr = nr;
	for (i = 0; i < nr; i++, ut++) {
		struct xfrm_tmpl *t = &xp->xfrm_vec[i];

		memcpy(&t->id, &ut->id, sizeof(struct xfrm_id));
		memcpy(&t->saddr, &ut->saddr,
		       sizeof(xfrm_address_t));
		t->reqid = ut->reqid;
		t->mode = ut->mode;
		t->share = ut->share;
		t->optional = ut->optional;
		t->aalgos = ut->aalgos;
		t->ealgos = ut->ealgos;
		t->calgos = ut->calgos;
		t->encap_family = ut->family;
	}
}

static int validate_tmpl(int nr, struct xfrm_user_tmpl *ut, u16 family)
{
	int i;

	if (nr > XFRM_MAX_DEPTH)
		return -EINVAL;

	for (i = 0; i < nr; i++) {
		/* We never validated the ut->family value, so many
		 * applications simply leave it at zero.  The check was
		 * never made and ut->family was ignored because all
		 * templates could be assumed to have the same family as
		 * the policy itself.  Now that we will have ipv4-in-ipv6
		 * and ipv6-in-ipv4 tunnels, this is no longer true.
		 */
		if (!ut[i].family)
			ut[i].family = family;

		switch (ut[i].family) {
		case AF_INET:
			break;
#if defined(CONFIG_IPV6) || defined(CONFIG_IPV6_MODULE)
		case AF_INET6:
			break;
#endif
		default:
			return -EINVAL;
		}
	}

	return 0;
}

static int copy_from_user_tmpl(struct xfrm_policy *pol, struct nlattr **attrs)
{
	struct nlattr *rt = attrs[XFRMA_TMPL];

	if (!rt) {
		pol->xfrm_nr = 0;
	} else {
		struct xfrm_user_tmpl *utmpl = nla_data(rt);
		int nr = nla_len(rt) / sizeof(*utmpl);
		int err;

		err = validate_tmpl(nr, utmpl, pol->family);
		if (err)
			return err;

		copy_templates(pol, utmpl, nr);
	}
	return 0;
}

static int copy_from_user_policy_type(u8 *tp, struct nlattr **attrs)
{
	struct nlattr *rt = attrs[XFRMA_POLICY_TYPE];
	struct xfrm_userpolicy_type *upt;
	u8 type = XFRM_POLICY_TYPE_MAIN;
	int err;

	if (rt) {
		upt = nla_data(rt);
		type = upt->type;
	}

	err = verify_policy_type(type);
	if (err)
		return err;

	*tp = type;
	return 0;
}

static void copy_from_user_policy(struct xfrm_policy *xp, struct xfrm_userpolicy_info *p)
{
	xp->priority = p->priority;
	xp->index = p->index;
	memcpy(&xp->selector, &p->sel, sizeof(xp->selector));
	memcpy(&xp->lft, &p->lft, sizeof(xp->lft));
	xp->action = p->action;
	xp->flags = p->flags;
	xp->family = p->sel.family;
	/* XXX xp->share = p->share; */
}

static void copy_to_user_policy(struct xfrm_policy *xp, struct xfrm_userpolicy_info *p, int dir)
{
	memcpy(&p->sel, &xp->selector, sizeof(p->sel));
	memcpy(&p->lft, &xp->lft, sizeof(p->lft));
	memcpy(&p->curlft, &xp->curlft, sizeof(p->curlft));
	p->priority = xp->priority;
	p->index = xp->index;
	p->sel.family = xp->family;
	p->dir = dir;
	p->action = xp->action;
	p->flags = xp->flags;
	p->share = XFRM_SHARE_ANY; /* XXX xp->share */
}

static struct xfrm_policy *xfrm_policy_construct(struct xfrm_userpolicy_info *p, struct nlattr **attrs, int *errp)
{
	struct xfrm_policy *xp = xfrm_policy_alloc(GFP_KERNEL);
	int err;

	if (!xp) {
		*errp = -ENOMEM;
		return NULL;
	}

	copy_from_user_policy(xp, p);

	err = copy_from_user_policy_type(&xp->type, attrs);
	if (err)
		goto error;

	if (!(err = copy_from_user_tmpl(xp, attrs)))
		err = copy_from_user_sec_ctx(xp, attrs);
	if (err)
		goto error;

	return xp;
 error:
	*errp = err;
	xp->dead = 1;
	xfrm_policy_destroy(xp);
	return NULL;
}

static int xfrm_add_policy(struct sk_buff *skb, struct nlmsghdr *nlh,
		struct nlattr **attrs)
{
	struct xfrm_userpolicy_info *p = nlmsg_data(nlh);
	struct xfrm_policy *xp;
	struct km_event c;
	int err;
	int excl;

	err = verify_newpolicy_info(p);
	if (err)
		return err;
	err = verify_sec_ctx_len(attrs);
	if (err)
		return err;

	xp = xfrm_policy_construct(p, attrs, &err);
	if (!xp)
		return err;

	/* shouldnt excl be based on nlh flags??
	 * Aha! this is anti-netlink really i.e  more pfkey derived
	 * in netlink excl is a flag and you wouldnt need
	 * a type XFRM_MSG_UPDPOLICY - JHS */
	excl = nlh->nlmsg_type == XFRM_MSG_NEWPOLICY;
	err = xfrm_policy_insert(p->dir, xp, excl);
	xfrm_audit_policy_add(xp, err ? 0 : 1, NETLINK_CB(skb).loginuid,
			      NETLINK_CB(skb).sid);

	if (err) {
		security_xfrm_policy_free(xp);
		kfree(xp);
		return err;
	}

	c.event = nlh->nlmsg_type;
	c.seq = nlh->nlmsg_seq;
	c.pid = nlh->nlmsg_pid;
	km_policy_notify(xp, p->dir, &c);

	xfrm_pol_put(xp);

	return 0;
}

static int copy_to_user_tmpl(struct xfrm_policy *xp, struct sk_buff *skb)
{
	struct xfrm_user_tmpl vec[XFRM_MAX_DEPTH];
	int i;

	if (xp->xfrm_nr == 0)
		return 0;

	for (i = 0; i < xp->xfrm_nr; i++) {
		struct xfrm_user_tmpl *up = &vec[i];
		struct xfrm_tmpl *kp = &xp->xfrm_vec[i];

		memcpy(&up->id, &kp->id, sizeof(up->id));
		up->family = kp->encap_family;
		memcpy(&up->saddr, &kp->saddr, sizeof(up->saddr));
		up->reqid = kp->reqid;
		up->mode = kp->mode;
		up->share = kp->share;
		up->optional = kp->optional;
		up->aalgos = kp->aalgos;
		up->ealgos = kp->ealgos;
		up->calgos = kp->calgos;
	}

	return nla_put(skb, XFRMA_TMPL,
		       sizeof(struct xfrm_user_tmpl) * xp->xfrm_nr, vec);
}

static inline int copy_to_user_state_sec_ctx(struct xfrm_state *x, struct sk_buff *skb)
{
	if (x->security) {
		return copy_sec_ctx(x->security, skb);
	}
	return 0;
}

static inline int copy_to_user_sec_ctx(struct xfrm_policy *xp, struct sk_buff *skb)
{
	if (xp->security) {
		return copy_sec_ctx(xp->security, skb);
	}
	return 0;
}
static inline size_t userpolicy_type_attrsize(void)
{
#ifdef CONFIG_XFRM_SUB_POLICY
	return nla_total_size(sizeof(struct xfrm_userpolicy_type));
#else
	return 0;
#endif
}

#ifdef CONFIG_XFRM_SUB_POLICY
static int copy_to_user_policy_type(u8 type, struct sk_buff *skb)
{
	struct xfrm_userpolicy_type upt = {
		.type = type,
	};

	return nla_put(skb, XFRMA_POLICY_TYPE, sizeof(upt), &upt);
}

#else
static inline int copy_to_user_policy_type(u8 type, struct sk_buff *skb)
{
	return 0;
}
#endif

static int dump_one_policy(struct xfrm_policy *xp, int dir, int count, void *ptr)
{
	struct xfrm_dump_info *sp = ptr;
	struct xfrm_userpolicy_info *p;
	struct sk_buff *in_skb = sp->in_skb;
	struct sk_buff *skb = sp->out_skb;
	struct nlmsghdr *nlh;

	if (sp->this_idx < sp->start_idx)
		goto out;

	nlh = nlmsg_put(skb, NETLINK_CB(in_skb).pid, sp->nlmsg_seq,
			XFRM_MSG_NEWPOLICY, sizeof(*p), sp->nlmsg_flags);
	if (nlh == NULL)
		return -EMSGSIZE;

	p = nlmsg_data(nlh);
	copy_to_user_policy(xp, p, dir);
	if (copy_to_user_tmpl(xp, skb) < 0)
		goto nlmsg_failure;
	if (copy_to_user_sec_ctx(xp, skb))
		goto nlmsg_failure;
	if (copy_to_user_policy_type(xp->type, skb) < 0)
		goto nlmsg_failure;

	nlmsg_end(skb, nlh);
out:
	sp->this_idx++;
	return 0;

nlmsg_failure:
	nlmsg_cancel(skb, nlh);
	return -EMSGSIZE;
}

static int xfrm_dump_policy(struct sk_buff *skb, struct netlink_callback *cb)
{
	struct xfrm_dump_info info;

	info.in_skb = cb->skb;
	info.out_skb = skb;
	info.nlmsg_seq = cb->nlh->nlmsg_seq;
	info.nlmsg_flags = NLM_F_MULTI;
	info.this_idx = 0;
	info.start_idx = cb->args[0];
	(void) xfrm_policy_walk(XFRM_POLICY_TYPE_MAIN, dump_one_policy, &info);
#ifdef CONFIG_XFRM_SUB_POLICY
	(void) xfrm_policy_walk(XFRM_POLICY_TYPE_SUB, dump_one_policy, &info);
#endif
	cb->args[0] = info.this_idx;

	return skb->len;
}

static struct sk_buff *xfrm_policy_netlink(struct sk_buff *in_skb,
					  struct xfrm_policy *xp,
					  int dir, u32 seq)
{
	struct xfrm_dump_info info;
	struct sk_buff *skb;

	skb = nlmsg_new(NLMSG_DEFAULT_SIZE, GFP_KERNEL);
	if (!skb)
		return ERR_PTR(-ENOMEM);

	info.in_skb = in_skb;
	info.out_skb = skb;
	info.nlmsg_seq = seq;
	info.nlmsg_flags = 0;
	info.this_idx = info.start_idx = 0;

	if (dump_one_policy(xp, dir, 0, &info) < 0) {
		kfree_skb(skb);
		return NULL;
	}

	return skb;
}

static int xfrm_get_policy(struct sk_buff *skb, struct nlmsghdr *nlh,
		struct nlattr **attrs)
{
	struct xfrm_policy *xp;
	struct xfrm_userpolicy_id *p;
	u8 type = XFRM_POLICY_TYPE_MAIN;
	int err;
	struct km_event c;
	int delete;

	p = nlmsg_data(nlh);
	delete = nlh->nlmsg_type == XFRM_MSG_DELPOLICY;

	err = copy_from_user_policy_type(&type, attrs);
	if (err)
		return err;

	err = verify_policy_dir(p->dir);
	if (err)
		return err;

	if (p->index)
		xp = xfrm_policy_byid(type, p->dir, p->index, delete, &err);
	else {
		struct nlattr *rt = attrs[XFRMA_SEC_CTX];
		struct xfrm_policy tmp;

		err = verify_sec_ctx_len(attrs);
		if (err)
			return err;

		memset(&tmp, 0, sizeof(struct xfrm_policy));
		if (rt) {
			struct xfrm_user_sec_ctx *uctx = nla_data(rt);

			if ((err = security_xfrm_policy_alloc(&tmp, uctx)))
				return err;
		}
		xp = xfrm_policy_bysel_ctx(type, p->dir, &p->sel, tmp.security,
					   delete, &err);
		security_xfrm_policy_free(&tmp);
	}
	if (xp == NULL)
		return -ENOENT;

	if (!delete) {
		struct sk_buff *resp_skb;

		resp_skb = xfrm_policy_netlink(skb, xp, p->dir, nlh->nlmsg_seq);
		if (IS_ERR(resp_skb)) {
			err = PTR_ERR(resp_skb);
		} else {
			err = nlmsg_unicast(xfrm_nl, resp_skb,
					    NETLINK_CB(skb).pid);
		}
	} else {
		xfrm_audit_policy_delete(xp, err ? 0 : 1,
					 NETLINK_CB(skb).loginuid,
					 NETLINK_CB(skb).sid);

		if (err != 0)
			goto out;

		c.data.byid = p->index;
		c.event = nlh->nlmsg_type;
		c.seq = nlh->nlmsg_seq;
		c.pid = nlh->nlmsg_pid;
		km_policy_notify(xp, p->dir, &c);
	}

out:
	xfrm_pol_put(xp);
	return err;
}

static int xfrm_flush_sa(struct sk_buff *skb, struct nlmsghdr *nlh,
		struct nlattr **attrs)
{
	struct km_event c;
	struct xfrm_usersa_flush *p = nlmsg_data(nlh);
	struct xfrm_audit audit_info;
	int err;

	audit_info.loginuid = NETLINK_CB(skb).loginuid;
	audit_info.secid = NETLINK_CB(skb).sid;
	err = xfrm_state_flush(p->proto, &audit_info);
	if (err)
		return err;
	c.data.proto = p->proto;
	c.event = nlh->nlmsg_type;
	c.seq = nlh->nlmsg_seq;
	c.pid = nlh->nlmsg_pid;
	km_state_notify(NULL, &c);

	return 0;
}

static inline size_t xfrm_aevent_msgsize(void)
{
	return NLMSG_ALIGN(sizeof(struct xfrm_aevent_id))
	       + nla_total_size(sizeof(struct xfrm_replay_state))
	       + nla_total_size(sizeof(struct xfrm_lifetime_cur))
	       + nla_total_size(4) /* XFRM_AE_RTHR */
	       + nla_total_size(4); /* XFRM_AE_ETHR */
}

static int build_aevent(struct sk_buff *skb, struct xfrm_state *x, struct km_event *c)
{
	struct xfrm_aevent_id *id;
	struct nlmsghdr *nlh;

	nlh = nlmsg_put(skb, c->pid, c->seq, XFRM_MSG_NEWAE, sizeof(*id), 0);
	if (nlh == NULL)
		return -EMSGSIZE;

	id = nlmsg_data(nlh);
	memcpy(&id->sa_id.daddr, &x->id.daddr,sizeof(x->id.daddr));
	id->sa_id.spi = x->id.spi;
	id->sa_id.family = x->props.family;
	id->sa_id.proto = x->id.proto;
	memcpy(&id->saddr, &x->props.saddr,sizeof(x->props.saddr));
	id->reqid = x->props.reqid;
	id->flags = c->data.aevent;

	NLA_PUT(skb, XFRMA_REPLAY_VAL, sizeof(x->replay), &x->replay);
	NLA_PUT(skb, XFRMA_LTIME_VAL, sizeof(x->curlft), &x->curlft);

	if (id->flags & XFRM_AE_RTHR)
		NLA_PUT_U32(skb, XFRMA_REPLAY_THRESH, x->replay_maxdiff);

	if (id->flags & XFRM_AE_ETHR)
		NLA_PUT_U32(skb, XFRMA_ETIMER_THRESH,
			    x->replay_maxage * 10 / HZ);

	return nlmsg_end(skb, nlh);

nla_put_failure:
	nlmsg_cancel(skb, nlh);
	return -EMSGSIZE;
}

static int xfrm_get_ae(struct sk_buff *skb, struct nlmsghdr *nlh,
		struct nlattr **attrs)
{
	struct xfrm_state *x;
	struct sk_buff *r_skb;
	int err;
	struct km_event c;
	struct xfrm_aevent_id *p = nlmsg_data(nlh);
	struct xfrm_usersa_id *id = &p->sa_id;

	r_skb = nlmsg_new(xfrm_aevent_msgsize(), GFP_ATOMIC);
	if (r_skb == NULL)
		return -ENOMEM;

	x = xfrm_state_lookup(&id->daddr, id->spi, id->proto, id->family);
	if (x == NULL) {
		kfree_skb(r_skb);
		return -ESRCH;
	}

	/*
	 * XXX: is this lock really needed - none of the other
	 * gets lock (the concern is things getting updated
	 * while we are still reading) - jhs
	*/
	spin_lock_bh(&x->lock);
	c.data.aevent = p->flags;
	c.seq = nlh->nlmsg_seq;
	c.pid = nlh->nlmsg_pid;

	if (build_aevent(r_skb, x, &c) < 0)
		BUG();
	err = nlmsg_unicast(xfrm_nl, r_skb, NETLINK_CB(skb).pid);
	spin_unlock_bh(&x->lock);
	xfrm_state_put(x);
	return err;
}

static int xfrm_new_ae(struct sk_buff *skb, struct nlmsghdr *nlh,
		struct nlattr **attrs)
{
	struct xfrm_state *x;
	struct km_event c;
	int err = - EINVAL;
	struct xfrm_aevent_id *p = nlmsg_data(nlh);
	struct nlattr *rp = attrs[XFRMA_REPLAY_VAL];
	struct nlattr *lt = attrs[XFRMA_LTIME_VAL];

	if (!lt && !rp)
		return err;

	/* pedantic mode - thou shalt sayeth replaceth */
	if (!(nlh->nlmsg_flags&NLM_F_REPLACE))
		return err;

	x = xfrm_state_lookup(&p->sa_id.daddr, p->sa_id.spi, p->sa_id.proto, p->sa_id.family);
	if (x == NULL)
		return -ESRCH;

	if (x->km.state != XFRM_STATE_VALID)
		goto out;

	spin_lock_bh(&x->lock);
	xfrm_update_ae_params(x, attrs);
	spin_unlock_bh(&x->lock);

	c.event = nlh->nlmsg_type;
	c.seq = nlh->nlmsg_seq;
	c.pid = nlh->nlmsg_pid;
	c.data.aevent = XFRM_AE_CU;
	km_state_notify(x, &c);
	err = 0;
out:
	xfrm_state_put(x);
	return err;
}

static int xfrm_flush_policy(struct sk_buff *skb, struct nlmsghdr *nlh,
		struct nlattr **attrs)
{
	struct km_event c;
	u8 type = XFRM_POLICY_TYPE_MAIN;
	int err;
	struct xfrm_audit audit_info;

	err = copy_from_user_policy_type(&type, attrs);
	if (err)
		return err;

	audit_info.loginuid = NETLINK_CB(skb).loginuid;
	audit_info.secid = NETLINK_CB(skb).sid;
	err = xfrm_policy_flush(type, &audit_info);
	if (err)
		return err;
	c.data.type = type;
	c.event = nlh->nlmsg_type;
	c.seq = nlh->nlmsg_seq;
	c.pid = nlh->nlmsg_pid;
	km_policy_notify(NULL, 0, &c);
	return 0;
}

static int xfrm_add_pol_expire(struct sk_buff *skb, struct nlmsghdr *nlh,
		struct nlattr **attrs)
{
	struct xfrm_policy *xp;
	struct xfrm_user_polexpire *up = nlmsg_data(nlh);
	struct xfrm_userpolicy_info *p = &up->pol;
	u8 type = XFRM_POLICY_TYPE_MAIN;
	int err = -ENOENT;

	err = copy_from_user_policy_type(&type, attrs);
	if (err)
		return err;

	if (p->index)
		xp = xfrm_policy_byid(type, p->dir, p->index, 0, &err);
	else {
		struct nlattr *rt = attrs[XFRMA_SEC_CTX];
		struct xfrm_policy tmp;

		err = verify_sec_ctx_len(attrs);
		if (err)
			return err;

		memset(&tmp, 0, sizeof(struct xfrm_policy));
		if (rt) {
			struct xfrm_user_sec_ctx *uctx = nla_data(rt);

			if ((err = security_xfrm_policy_alloc(&tmp, uctx)))
				return err;
		}
		xp = xfrm_policy_bysel_ctx(type, p->dir, &p->sel, tmp.security,
					   0, &err);
		security_xfrm_policy_free(&tmp);
	}

	if (xp == NULL)
		return -ENOENT;
	read_lock(&xp->lock);
	if (xp->dead) {
		read_unlock(&xp->lock);
		goto out;
	}

	read_unlock(&xp->lock);
	err = 0;
	if (up->hard) {
		xfrm_policy_delete(xp, p->dir);
		xfrm_audit_policy_delete(xp, 1, NETLINK_CB(skb).loginuid,
					 NETLINK_CB(skb).sid);

	} else {
		// reset the timers here?
		printk("Dont know what to do with soft policy expire\n");
	}
	km_policy_expired(xp, p->dir, up->hard, current->pid);

out:
	xfrm_pol_put(xp);
	return err;
}

static int xfrm_add_sa_expire(struct sk_buff *skb, struct nlmsghdr *nlh,
		struct nlattr **attrs)
{
	struct xfrm_state *x;
	int err;
	struct xfrm_user_expire *ue = nlmsg_data(nlh);
	struct xfrm_usersa_info *p = &ue->state;

	x = xfrm_state_lookup(&p->id.daddr, p->id.spi, p->id.proto, p->family);

	err = -ENOENT;
	if (x == NULL)
		return err;

	spin_lock_bh(&x->lock);
	err = -EINVAL;
	if (x->km.state != XFRM_STATE_VALID)
		goto out;
	km_state_expired(x, ue->hard, current->pid);

	if (ue->hard) {
		__xfrm_state_delete(x);
		xfrm_audit_state_delete(x, 1, NETLINK_CB(skb).loginuid,
					NETLINK_CB(skb).sid);
	}
	err = 0;
out:
	spin_unlock_bh(&x->lock);
	xfrm_state_put(x);
	return err;
}

static int xfrm_add_acquire(struct sk_buff *skb, struct nlmsghdr *nlh,
		struct nlattr **attrs)
{
	struct xfrm_policy *xp;
	struct xfrm_user_tmpl *ut;
	int i;
	struct nlattr *rt = attrs[XFRMA_TMPL];

	struct xfrm_user_acquire *ua = nlmsg_data(nlh);
	struct xfrm_state *x = xfrm_state_alloc();
	int err = -ENOMEM;

	if (!x)
		return err;

	err = verify_newpolicy_info(&ua->policy);
	if (err) {
		printk("BAD policy passed\n");
		kfree(x);
		return err;
	}

	/*   build an XP */
	xp = xfrm_policy_construct(&ua->policy, attrs, &err);
	if (!xp) {
		kfree(x);
		return err;
	}

	memcpy(&x->id, &ua->id, sizeof(ua->id));
	memcpy(&x->props.saddr, &ua->saddr, sizeof(ua->saddr));
	memcpy(&x->sel, &ua->sel, sizeof(ua->sel));

	ut = nla_data(rt);
	/* extract the templates and for each call km_key */
	for (i = 0; i < xp->xfrm_nr; i++, ut++) {
		struct xfrm_tmpl *t = &xp->xfrm_vec[i];
		memcpy(&x->id, &t->id, sizeof(x->id));
		x->props.mode = t->mode;
		x->props.reqid = t->reqid;
		x->props.family = ut->family;
		t->aalgos = ua->aalgos;
		t->ealgos = ua->ealgos;
		t->calgos = ua->calgos;
		err = km_query(x, t, xp);

	}

	kfree(x);
	kfree(xp);

	return 0;
}

#ifdef CONFIG_XFRM_MIGRATE
static int copy_from_user_migrate(struct xfrm_migrate *ma,
				  struct nlattr **attrs, int *num)
{
	struct nlattr *rt = attrs[XFRMA_MIGRATE];
	struct xfrm_user_migrate *um;
	int i, num_migrate;

	um = nla_data(rt);
	num_migrate = nla_len(rt) / sizeof(*um);

	if (num_migrate <= 0 || num_migrate > XFRM_MAX_DEPTH)
		return -EINVAL;

	for (i = 0; i < num_migrate; i++, um++, ma++) {
		memcpy(&ma->old_daddr, &um->old_daddr, sizeof(ma->old_daddr));
		memcpy(&ma->old_saddr, &um->old_saddr, sizeof(ma->old_saddr));
		memcpy(&ma->new_daddr, &um->new_daddr, sizeof(ma->new_daddr));
		memcpy(&ma->new_saddr, &um->new_saddr, sizeof(ma->new_saddr));

		ma->proto = um->proto;
		ma->mode = um->mode;
		ma->reqid = um->reqid;

		ma->old_family = um->old_family;
		ma->new_family = um->new_family;
	}

	*num = i;
	return 0;
}

static int xfrm_do_migrate(struct sk_buff *skb, struct nlmsghdr *nlh,
			   struct nlattr **attrs)
{
	struct xfrm_userpolicy_id *pi = nlmsg_data(nlh);
	struct xfrm_migrate m[XFRM_MAX_DEPTH];
	u8 type;
	int err;
	int n = 0;

	if (attrs[XFRMA_MIGRATE] == NULL)
		return -EINVAL;

	err = copy_from_user_policy_type(&type, attrs);
	if (err)
		return err;

	err = copy_from_user_migrate((struct xfrm_migrate *)m,
				     attrs, &n);
	if (err)
		return err;

	if (!n)
		return 0;

	xfrm_migrate(&pi->sel, pi->dir, type, m, n);

	return 0;
}
#else
static int xfrm_do_migrate(struct sk_buff *skb, struct nlmsghdr *nlh,
			   struct nlattr **attrs)
{
	return -ENOPROTOOPT;
}
#endif

#ifdef CONFIG_XFRM_MIGRATE
static int copy_to_user_migrate(struct xfrm_migrate *m, struct sk_buff *skb)
{
	struct xfrm_user_migrate um;

	memset(&um, 0, sizeof(um));
	um.proto = m->proto;
	um.mode = m->mode;
	um.reqid = m->reqid;
	um.old_family = m->old_family;
	memcpy(&um.old_daddr, &m->old_daddr, sizeof(um.old_daddr));
	memcpy(&um.old_saddr, &m->old_saddr, sizeof(um.old_saddr));
	um.new_family = m->new_family;
	memcpy(&um.new_daddr, &m->new_daddr, sizeof(um.new_daddr));
	memcpy(&um.new_saddr, &m->new_saddr, sizeof(um.new_saddr));

	return nla_put(skb, XFRMA_MIGRATE, sizeof(um), &um);
}

static inline size_t xfrm_migrate_msgsize(int num_migrate)
{
	return NLMSG_ALIGN(sizeof(struct xfrm_userpolicy_id))
	       + nla_total_size(sizeof(struct xfrm_user_migrate) * num_migrate)
	       + userpolicy_type_attrsize();
}

static int build_migrate(struct sk_buff *skb, struct xfrm_migrate *m,
			 int num_migrate, struct xfrm_selector *sel,
			 u8 dir, u8 type)
{
	struct xfrm_migrate *mp;
	struct xfrm_userpolicy_id *pol_id;
	struct nlmsghdr *nlh;
	int i;

	nlh = nlmsg_put(skb, 0, 0, XFRM_MSG_MIGRATE, sizeof(*pol_id), 0);
	if (nlh == NULL)
		return -EMSGSIZE;

	pol_id = nlmsg_data(nlh);
	/* copy data from selector, dir, and type to the pol_id */
	memset(pol_id, 0, sizeof(*pol_id));
	memcpy(&pol_id->sel, sel, sizeof(pol_id->sel));
	pol_id->dir = dir;

	if (copy_to_user_policy_type(type, skb) < 0)
		goto nlmsg_failure;

	for (i = 0, mp = m ; i < num_migrate; i++, mp++) {
		if (copy_to_user_migrate(mp, skb) < 0)
			goto nlmsg_failure;
	}

	return nlmsg_end(skb, nlh);
nlmsg_failure:
	nlmsg_cancel(skb, nlh);
	return -EMSGSIZE;
}

static int xfrm_send_migrate(struct xfrm_selector *sel, u8 dir, u8 type,
			     struct xfrm_migrate *m, int num_migrate)
{
	struct sk_buff *skb;

	skb = nlmsg_new(xfrm_migrate_msgsize(num_migrate), GFP_ATOMIC);
	if (skb == NULL)
		return -ENOMEM;

	/* build migrate */
	if (build_migrate(skb, m, num_migrate, sel, dir, type) < 0)
		BUG();

	return nlmsg_multicast(xfrm_nl, skb, 0, XFRMNLGRP_MIGRATE, GFP_ATOMIC);
}
#else
static int xfrm_send_migrate(struct xfrm_selector *sel, u8 dir, u8 type,
			     struct xfrm_migrate *m, int num_migrate)
{
	return -ENOPROTOOPT;
}
#endif

#define XMSGSIZE(type) sizeof(struct type)

static const int xfrm_msg_min[XFRM_NR_MSGTYPES] = {
	[XFRM_MSG_NEWSA       - XFRM_MSG_BASE] = XMSGSIZE(xfrm_usersa_info),
	[XFRM_MSG_DELSA       - XFRM_MSG_BASE] = XMSGSIZE(xfrm_usersa_id),
	[XFRM_MSG_GETSA       - XFRM_MSG_BASE] = XMSGSIZE(xfrm_usersa_id),
	[XFRM_MSG_NEWPOLICY   - XFRM_MSG_BASE] = XMSGSIZE(xfrm_userpolicy_info),
	[XFRM_MSG_DELPOLICY   - XFRM_MSG_BASE] = XMSGSIZE(xfrm_userpolicy_id),
	[XFRM_MSG_GETPOLICY   - XFRM_MSG_BASE] = XMSGSIZE(xfrm_userpolicy_id),
	[XFRM_MSG_ALLOCSPI    - XFRM_MSG_BASE] = XMSGSIZE(xfrm_userspi_info),
	[XFRM_MSG_ACQUIRE     - XFRM_MSG_BASE] = XMSGSIZE(xfrm_user_acquire),
	[XFRM_MSG_EXPIRE      - XFRM_MSG_BASE] = XMSGSIZE(xfrm_user_expire),
	[XFRM_MSG_UPDPOLICY   - XFRM_MSG_BASE] = XMSGSIZE(xfrm_userpolicy_info),
	[XFRM_MSG_UPDSA       - XFRM_MSG_BASE] = XMSGSIZE(xfrm_usersa_info),
	[XFRM_MSG_POLEXPIRE   - XFRM_MSG_BASE] = XMSGSIZE(xfrm_user_polexpire),
	[XFRM_MSG_FLUSHSA     - XFRM_MSG_BASE] = XMSGSIZE(xfrm_usersa_flush),
	[XFRM_MSG_FLUSHPOLICY - XFRM_MSG_BASE] = 0,
	[XFRM_MSG_NEWAE       - XFRM_MSG_BASE] = XMSGSIZE(xfrm_aevent_id),
	[XFRM_MSG_GETAE       - XFRM_MSG_BASE] = XMSGSIZE(xfrm_aevent_id),
	[XFRM_MSG_REPORT      - XFRM_MSG_BASE] = XMSGSIZE(xfrm_user_report),
	[XFRM_MSG_MIGRATE     - XFRM_MSG_BASE] = XMSGSIZE(xfrm_userpolicy_id),
	[XFRM_MSG_GETSADINFO  - XFRM_MSG_BASE] = sizeof(u32),
	[XFRM_MSG_GETSPDINFO  - XFRM_MSG_BASE] = sizeof(u32),
};

#undef XMSGSIZE

static const struct nla_policy xfrma_policy[XFRMA_MAX+1] = {
	[XFRMA_ALG_AEAD]	= { .len = sizeof(struct xfrm_algo_aead) },
	[XFRMA_ALG_AUTH]	= { .len = sizeof(struct xfrm_algo) },
	[XFRMA_ALG_CRYPT]	= { .len = sizeof(struct xfrm_algo) },
	[XFRMA_ALG_COMP]	= { .len = sizeof(struct xfrm_algo) },
	[XFRMA_ENCAP]		= { .len = sizeof(struct xfrm_encap_tmpl) },
	[XFRMA_TMPL]		= { .len = sizeof(struct xfrm_user_tmpl) },
	[XFRMA_SEC_CTX]		= { .len = sizeof(struct xfrm_sec_ctx) },
	[XFRMA_LTIME_VAL]	= { .len = sizeof(struct xfrm_lifetime_cur) },
	[XFRMA_REPLAY_VAL]	= { .len = sizeof(struct xfrm_replay_state) },
	[XFRMA_REPLAY_THRESH]	= { .type = NLA_U32 },
	[XFRMA_ETIMER_THRESH]	= { .type = NLA_U32 },
	[XFRMA_SRCADDR]		= { .len = sizeof(xfrm_address_t) },
	[XFRMA_COADDR]		= { .len = sizeof(xfrm_address_t) },
	[XFRMA_POLICY_TYPE]	= { .len = sizeof(struct xfrm_userpolicy_type)},
	[XFRMA_MIGRATE]		= { .len = sizeof(struct xfrm_user_migrate) },
};

static struct xfrm_link {
	int (*doit)(struct sk_buff *, struct nlmsghdr *, struct nlattr **);
	int (*dump)(struct sk_buff *, struct netlink_callback *);
} xfrm_dispatch[XFRM_NR_MSGTYPES] = {
	[XFRM_MSG_NEWSA       - XFRM_MSG_BASE] = { .doit = xfrm_add_sa        },
	[XFRM_MSG_DELSA       - XFRM_MSG_BASE] = { .doit = xfrm_del_sa        },
	[XFRM_MSG_GETSA       - XFRM_MSG_BASE] = { .doit = xfrm_get_sa,
						   .dump = xfrm_dump_sa       },
	[XFRM_MSG_NEWPOLICY   - XFRM_MSG_BASE] = { .doit = xfrm_add_policy    },
	[XFRM_MSG_DELPOLICY   - XFRM_MSG_BASE] = { .doit = xfrm_get_policy    },
	[XFRM_MSG_GETPOLICY   - XFRM_MSG_BASE] = { .doit = xfrm_get_policy,
						   .dump = xfrm_dump_policy   },
	[XFRM_MSG_ALLOCSPI    - XFRM_MSG_BASE] = { .doit = xfrm_alloc_userspi },
	[XFRM_MSG_ACQUIRE     - XFRM_MSG_BASE] = { .doit = xfrm_add_acquire   },
	[XFRM_MSG_EXPIRE      - XFRM_MSG_BASE] = { .doit = xfrm_add_sa_expire },
	[XFRM_MSG_UPDPOLICY   - XFRM_MSG_BASE] = { .doit = xfrm_add_policy    },
	[XFRM_MSG_UPDSA       - XFRM_MSG_BASE] = { .doit = xfrm_add_sa        },
	[XFRM_MSG_POLEXPIRE   - XFRM_MSG_BASE] = { .doit = xfrm_add_pol_expire},
	[XFRM_MSG_FLUSHSA     - XFRM_MSG_BASE] = { .doit = xfrm_flush_sa      },
	[XFRM_MSG_FLUSHPOLICY - XFRM_MSG_BASE] = { .doit = xfrm_flush_policy  },
	[XFRM_MSG_NEWAE       - XFRM_MSG_BASE] = { .doit = xfrm_new_ae  },
	[XFRM_MSG_GETAE       - XFRM_MSG_BASE] = { .doit = xfrm_get_ae  },
	[XFRM_MSG_MIGRATE     - XFRM_MSG_BASE] = { .doit = xfrm_do_migrate    },
	[XFRM_MSG_GETSADINFO  - XFRM_MSG_BASE] = { .doit = xfrm_get_sadinfo   },
	[XFRM_MSG_GETSPDINFO  - XFRM_MSG_BASE] = { .doit = xfrm_get_spdinfo   },
};

static int xfrm_user_rcv_msg(struct sk_buff *skb, struct nlmsghdr *nlh)
{
	struct nlattr *attrs[XFRMA_MAX+1];
	struct xfrm_link *link;
	int type, err;

	type = nlh->nlmsg_type;
	if (type > XFRM_MSG_MAX)
		return -EINVAL;

	type -= XFRM_MSG_BASE;
	link = &xfrm_dispatch[type];

	/* All operations require privileges, even GET */
	if (security_netlink_recv(skb, CAP_NET_ADMIN))
		return -EPERM;

	if ((type == (XFRM_MSG_GETSA - XFRM_MSG_BASE) ||
	     type == (XFRM_MSG_GETPOLICY - XFRM_MSG_BASE)) &&
	    (nlh->nlmsg_flags & NLM_F_DUMP)) {
		if (link->dump == NULL)
			return -EINVAL;

		return netlink_dump_start(xfrm_nl, skb, nlh, link->dump, NULL);
	}

	err = nlmsg_parse(nlh, xfrm_msg_min[type], attrs, XFRMA_MAX,
			  xfrma_policy);
	if (err < 0)
		return err;

	if (link->doit == NULL)
		return -EINVAL;

	return link->doit(skb, nlh, attrs);
}

static void xfrm_netlink_rcv(struct sk_buff *skb)
{
	mutex_lock(&xfrm_cfg_mutex);
	netlink_rcv_skb(skb, &xfrm_user_rcv_msg);
	mutex_unlock(&xfrm_cfg_mutex);
}

static inline size_t xfrm_expire_msgsize(void)
{
	return NLMSG_ALIGN(sizeof(struct xfrm_user_expire));
}

static int build_expire(struct sk_buff *skb, struct xfrm_state *x, struct km_event *c)
{
	struct xfrm_user_expire *ue;
	struct nlmsghdr *nlh;

	nlh = nlmsg_put(skb, c->pid, 0, XFRM_MSG_EXPIRE, sizeof(*ue), 0);
	if (nlh == NULL)
		return -EMSGSIZE;

	ue = nlmsg_data(nlh);
	copy_to_user_state(x, &ue->state);
	ue->hard = (c->data.hard != 0) ? 1 : 0;

	return nlmsg_end(skb, nlh);
}

static int xfrm_exp_state_notify(struct xfrm_state *x, struct km_event *c)
{
	struct sk_buff *skb;

	skb = nlmsg_new(xfrm_expire_msgsize(), GFP_ATOMIC);
	if (skb == NULL)
		return -ENOMEM;

	if (build_expire(skb, x, c) < 0)
		BUG();

	return nlmsg_multicast(xfrm_nl, skb, 0, XFRMNLGRP_EXPIRE, GFP_ATOMIC);
}

static int xfrm_aevent_state_notify(struct xfrm_state *x, struct km_event *c)
{
	struct sk_buff *skb;

	skb = nlmsg_new(xfrm_aevent_msgsize(), GFP_ATOMIC);
	if (skb == NULL)
		return -ENOMEM;

	if (build_aevent(skb, x, c) < 0)
		BUG();

	return nlmsg_multicast(xfrm_nl, skb, 0, XFRMNLGRP_AEVENTS, GFP_ATOMIC);
}

static int xfrm_notify_sa_flush(struct km_event *c)
{
	struct xfrm_usersa_flush *p;
	struct nlmsghdr *nlh;
	struct sk_buff *skb;
	int len = NLMSG_ALIGN(sizeof(struct xfrm_usersa_flush));

	skb = nlmsg_new(len, GFP_ATOMIC);
	if (skb == NULL)
		return -ENOMEM;

	nlh = nlmsg_put(skb, c->pid, c->seq, XFRM_MSG_FLUSHSA, sizeof(*p), 0);
	if (nlh == NULL) {
		kfree_skb(skb);
		return -EMSGSIZE;
	}

	p = nlmsg_data(nlh);
	p->proto = c->data.proto;

	nlmsg_end(skb, nlh);

	return nlmsg_multicast(xfrm_nl, skb, 0, XFRMNLGRP_SA, GFP_ATOMIC);
}

static inline size_t xfrm_sa_len(struct xfrm_state *x)
{
	size_t l = 0;
	if (x->aead)
		l += nla_total_size(aead_len(x->aead));
	if (x->aalg)
		l += nla_total_size(xfrm_alg_len(x->aalg));
	if (x->ealg)
		l += nla_total_size(xfrm_alg_len(x->ealg));
	if (x->calg)
		l += nla_total_size(sizeof(*x->calg));
	if (x->encap)
		l += nla_total_size(sizeof(*x->encap));
	if (x->security)
		l += nla_total_size(sizeof(struct xfrm_user_sec_ctx) +
				    x->security->ctx_len);
	if (x->coaddr)
		l += nla_total_size(sizeof(*x->coaddr));

	/* Must count x->lastused as it may become non-zero behind our back. */
	l += nla_total_size(sizeof(u64));

	return l;
}

static int xfrm_notify_sa(struct xfrm_state *x, struct km_event *c)
{
	struct xfrm_usersa_info *p;
	struct xfrm_usersa_id *id;
	struct nlmsghdr *nlh;
	struct sk_buff *skb;
	int len = xfrm_sa_len(x);
	int headlen;

	headlen = sizeof(*p);
	if (c->event == XFRM_MSG_DELSA) {
		len += nla_total_size(headlen);
		headlen = sizeof(*id);
	}
	len += NLMSG_ALIGN(headlen);

	skb = nlmsg_new(len, GFP_ATOMIC);
	if (skb == NULL)
		return -ENOMEM;

	nlh = nlmsg_put(skb, c->pid, c->seq, c->event, headlen, 0);
	if (nlh == NULL)
		goto nla_put_failure;

	p = nlmsg_data(nlh);
	if (c->event == XFRM_MSG_DELSA) {
		struct nlattr *attr;

		id = nlmsg_data(nlh);
		memcpy(&id->daddr, &x->id.daddr, sizeof(id->daddr));
		id->spi = x->id.spi;
		id->family = x->props.family;
		id->proto = x->id.proto;

		attr = nla_reserve(skb, XFRMA_SA, sizeof(*p));
		if (attr == NULL)
			goto nla_put_failure;

		p = nla_data(attr);
	}

	if (copy_to_user_state_extra(x, p, skb))
		goto nla_put_failure;

	nlmsg_end(skb, nlh);

	return nlmsg_multicast(xfrm_nl, skb, 0, XFRMNLGRP_SA, GFP_ATOMIC);

nla_put_failure:
	/* Somebody screwed up with xfrm_sa_len! */
	WARN_ON(1);
	kfree_skb(skb);
	return -1;
}

static int xfrm_send_state_notify(struct xfrm_state *x, struct km_event *c)
{

	switch (c->event) {
	case XFRM_MSG_EXPIRE:
		return xfrm_exp_state_notify(x, c);
	case XFRM_MSG_NEWAE:
		return xfrm_aevent_state_notify(x, c);
	case XFRM_MSG_DELSA:
	case XFRM_MSG_UPDSA:
	case XFRM_MSG_NEWSA:
		return xfrm_notify_sa(x, c);
	case XFRM_MSG_FLUSHSA:
		return xfrm_notify_sa_flush(c);
	default:
		 printk("xfrm_user: Unknown SA event %d\n", c->event);
		 break;
	}

	return 0;

}

static inline size_t xfrm_acquire_msgsize(struct xfrm_state *x,
					  struct xfrm_policy *xp)
{
	return NLMSG_ALIGN(sizeof(struct xfrm_user_acquire))
	       + nla_total_size(sizeof(struct xfrm_user_tmpl) * xp->xfrm_nr)
	       + nla_total_size(xfrm_user_sec_ctx_size(x->security))
	       + userpolicy_type_attrsize();
}

static int build_acquire(struct sk_buff *skb, struct xfrm_state *x,
			 struct xfrm_tmpl *xt, struct xfrm_policy *xp,
			 int dir)
{
	struct xfrm_user_acquire *ua;
	struct nlmsghdr *nlh;
	__u32 seq = xfrm_get_acqseq();

	nlh = nlmsg_put(skb, 0, 0, XFRM_MSG_ACQUIRE, sizeof(*ua), 0);
	if (nlh == NULL)
		return -EMSGSIZE;

	ua = nlmsg_data(nlh);
	memcpy(&ua->id, &x->id, sizeof(ua->id));
	memcpy(&ua->saddr, &x->props.saddr, sizeof(ua->saddr));
	memcpy(&ua->sel, &x->sel, sizeof(ua->sel));
	copy_to_user_policy(xp, &ua->policy, dir);
	ua->aalgos = xt->aalgos;
	ua->ealgos = xt->ealgos;
	ua->calgos = xt->calgos;
	ua->seq = x->km.seq = seq;

	if (copy_to_user_tmpl(xp, skb) < 0)
		goto nlmsg_failure;
	if (copy_to_user_state_sec_ctx(x, skb))
		goto nlmsg_failure;
	if (copy_to_user_policy_type(xp->type, skb) < 0)
		goto nlmsg_failure;

	return nlmsg_end(skb, nlh);

nlmsg_failure:
	nlmsg_cancel(skb, nlh);
	return -EMSGSIZE;
}

static int xfrm_send_acquire(struct xfrm_state *x, struct xfrm_tmpl *xt,
			     struct xfrm_policy *xp, int dir)
{
	struct sk_buff *skb;

	skb = nlmsg_new(xfrm_acquire_msgsize(x, xp), GFP_ATOMIC);
	if (skb == NULL)
		return -ENOMEM;

	if (build_acquire(skb, x, xt, xp, dir) < 0)
		BUG();

	return nlmsg_multicast(xfrm_nl, skb, 0, XFRMNLGRP_ACQUIRE, GFP_ATOMIC);
}

/* User gives us xfrm_user_policy_info followed by an array of 0
 * or more templates.
 */
static struct xfrm_policy *xfrm_compile_policy(struct sock *sk, int opt,
					       u8 *data, int len, int *dir)
{
	struct xfrm_userpolicy_info *p = (struct xfrm_userpolicy_info *)data;
	struct xfrm_user_tmpl *ut = (struct xfrm_user_tmpl *) (p + 1);
	struct xfrm_policy *xp;
	int nr;

	switch (sk->sk_family) {
	case AF_INET:
		if (opt != IP_XFRM_POLICY) {
			*dir = -EOPNOTSUPP;
			return NULL;
		}
		break;
#if defined(CONFIG_IPV6) || defined(CONFIG_IPV6_MODULE)
	case AF_INET6:
		if (opt != IPV6_XFRM_POLICY) {
			*dir = -EOPNOTSUPP;
			return NULL;
		}
		break;
#endif
	default:
		*dir = -EINVAL;
		return NULL;
	}

	*dir = -EINVAL;

	if (len < sizeof(*p) ||
	    verify_newpolicy_info(p))
		return NULL;

	nr = ((len - sizeof(*p)) / sizeof(*ut));
	if (validate_tmpl(nr, ut, p->sel.family))
		return NULL;

	if (p->dir > XFRM_POLICY_OUT)
		return NULL;

	xp = xfrm_policy_alloc(GFP_KERNEL);
	if (xp == NULL) {
		*dir = -ENOBUFS;
		return NULL;
	}

	copy_from_user_policy(xp, p);
	xp->type = XFRM_POLICY_TYPE_MAIN;
	copy_templates(xp, ut, nr);

	*dir = p->dir;

	return xp;
}

static inline size_t xfrm_polexpire_msgsize(struct xfrm_policy *xp)
{
	return NLMSG_ALIGN(sizeof(struct xfrm_user_polexpire))
	       + nla_total_size(sizeof(struct xfrm_user_tmpl) * xp->xfrm_nr)
	       + nla_total_size(xfrm_user_sec_ctx_size(xp->security))
	       + userpolicy_type_attrsize();
}

static int build_polexpire(struct sk_buff *skb, struct xfrm_policy *xp,
			   int dir, struct km_event *c)
{
	struct xfrm_user_polexpire *upe;
	struct nlmsghdr *nlh;
	int hard = c->data.hard;

	nlh = nlmsg_put(skb, c->pid, 0, XFRM_MSG_POLEXPIRE, sizeof(*upe), 0);
	if (nlh == NULL)
		return -EMSGSIZE;

	upe = nlmsg_data(nlh);
	copy_to_user_policy(xp, &upe->pol, dir);
	if (copy_to_user_tmpl(xp, skb) < 0)
		goto nlmsg_failure;
	if (copy_to_user_sec_ctx(xp, skb))
		goto nlmsg_failure;
	if (copy_to_user_policy_type(xp->type, skb) < 0)
		goto nlmsg_failure;
	upe->hard = !!hard;

	return nlmsg_end(skb, nlh);

nlmsg_failure:
	nlmsg_cancel(skb, nlh);
	return -EMSGSIZE;
}

static int xfrm_exp_policy_notify(struct xfrm_policy *xp, int dir, struct km_event *c)
{
	struct sk_buff *skb;

	skb = nlmsg_new(xfrm_polexpire_msgsize(xp), GFP_ATOMIC);
	if (skb == NULL)
		return -ENOMEM;

	if (build_polexpire(skb, xp, dir, c) < 0)
		BUG();

	return nlmsg_multicast(xfrm_nl, skb, 0, XFRMNLGRP_EXPIRE, GFP_ATOMIC);
}

static int xfrm_notify_policy(struct xfrm_policy *xp, int dir, struct km_event *c)
{
	struct xfrm_userpolicy_info *p;
	struct xfrm_userpolicy_id *id;
	struct nlmsghdr *nlh;
	struct sk_buff *skb;
	int len = nla_total_size(sizeof(struct xfrm_user_tmpl) * xp->xfrm_nr);
	int headlen;

	headlen = sizeof(*p);
	if (c->event == XFRM_MSG_DELPOLICY) {
		len += nla_total_size(headlen);
		headlen = sizeof(*id);
	}
	len += userpolicy_type_attrsize();
	len += NLMSG_ALIGN(headlen);

	skb = nlmsg_new(len, GFP_ATOMIC);
	if (skb == NULL)
		return -ENOMEM;

	nlh = nlmsg_put(skb, c->pid, c->seq, c->event, headlen, 0);
	if (nlh == NULL)
		goto nlmsg_failure;

	p = nlmsg_data(nlh);
	if (c->event == XFRM_MSG_DELPOLICY) {
		struct nlattr *attr;

		id = nlmsg_data(nlh);
		memset(id, 0, sizeof(*id));
		id->dir = dir;
		if (c->data.byid)
			id->index = xp->index;
		else
			memcpy(&id->sel, &xp->selector, sizeof(id->sel));

		attr = nla_reserve(skb, XFRMA_POLICY, sizeof(*p));
		if (attr == NULL)
			goto nlmsg_failure;

		p = nla_data(attr);
	}

	copy_to_user_policy(xp, p, dir);
	if (copy_to_user_tmpl(xp, skb) < 0)
		goto nlmsg_failure;
	if (copy_to_user_policy_type(xp->type, skb) < 0)
		goto nlmsg_failure;

	nlmsg_end(skb, nlh);

	return nlmsg_multicast(xfrm_nl, skb, 0, XFRMNLGRP_POLICY, GFP_ATOMIC);

nlmsg_failure:
	kfree_skb(skb);
	return -1;
}

static int xfrm_notify_policy_flush(struct km_event *c)
{
	struct nlmsghdr *nlh;
	struct sk_buff *skb;

	skb = nlmsg_new(userpolicy_type_attrsize(), GFP_ATOMIC);
	if (skb == NULL)
		return -ENOMEM;

	nlh = nlmsg_put(skb, c->pid, c->seq, XFRM_MSG_FLUSHPOLICY, 0, 0);
	if (nlh == NULL)
		goto nlmsg_failure;
	if (copy_to_user_policy_type(c->data.type, skb) < 0)
		goto nlmsg_failure;

	nlmsg_end(skb, nlh);

	return nlmsg_multicast(xfrm_nl, skb, 0, XFRMNLGRP_POLICY, GFP_ATOMIC);

nlmsg_failure:
	kfree_skb(skb);
	return -1;
}

static int xfrm_send_policy_notify(struct xfrm_policy *xp, int dir, struct km_event *c)
{

	switch (c->event) {
	case XFRM_MSG_NEWPOLICY:
	case XFRM_MSG_UPDPOLICY:
	case XFRM_MSG_DELPOLICY:
		return xfrm_notify_policy(xp, dir, c);
	case XFRM_MSG_FLUSHPOLICY:
		return xfrm_notify_policy_flush(c);
	case XFRM_MSG_POLEXPIRE:
		return xfrm_exp_policy_notify(xp, dir, c);
	default:
		printk("xfrm_user: Unknown Policy event %d\n", c->event);
	}

	return 0;

}

static inline size_t xfrm_report_msgsize(void)
{
	return NLMSG_ALIGN(sizeof(struct xfrm_user_report));
}

static int build_report(struct sk_buff *skb, u8 proto,
			struct xfrm_selector *sel, xfrm_address_t *addr)
{
	struct xfrm_user_report *ur;
	struct nlmsghdr *nlh;

	nlh = nlmsg_put(skb, 0, 0, XFRM_MSG_REPORT, sizeof(*ur), 0);
	if (nlh == NULL)
		return -EMSGSIZE;

	ur = nlmsg_data(nlh);
	ur->proto = proto;
	memcpy(&ur->sel, sel, sizeof(ur->sel));

	if (addr)
		NLA_PUT(skb, XFRMA_COADDR, sizeof(*addr), addr);

	return nlmsg_end(skb, nlh);

nla_put_failure:
	nlmsg_cancel(skb, nlh);
	return -EMSGSIZE;
}

static int xfrm_send_report(u8 proto, struct xfrm_selector *sel,
			    xfrm_address_t *addr)
{
	struct sk_buff *skb;

	skb = nlmsg_new(xfrm_report_msgsize(), GFP_ATOMIC);
	if (skb == NULL)
		return -ENOMEM;

	if (build_report(skb, proto, sel, addr) < 0)
		BUG();

	return nlmsg_multicast(xfrm_nl, skb, 0, XFRMNLGRP_REPORT, GFP_ATOMIC);
}

static struct xfrm_mgr netlink_mgr = {
	.id		= "netlink",
	.notify		= xfrm_send_state_notify,
	.acquire	= xfrm_send_acquire,
	.compile_policy	= xfrm_compile_policy,
	.notify_policy	= xfrm_send_policy_notify,
	.report		= xfrm_send_report,
	.migrate	= xfrm_send_migrate,
};

static int __init xfrm_user_init(void)
{
	struct sock *nlsk;

	printk(KERN_INFO "Initializing XFRM netlink socket\n");

	nlsk = netlink_kernel_create(&init_net, NETLINK_XFRM, XFRMNLGRP_MAX,
				     xfrm_netlink_rcv, NULL, THIS_MODULE);
	if (nlsk == NULL)
		return -ENOMEM;
	rcu_assign_pointer(xfrm_nl, nlsk);

	xfrm_register_km(&netlink_mgr);

	return 0;
}

static void __exit xfrm_user_exit(void)
{
	struct sock *nlsk = xfrm_nl;

	xfrm_unregister_km(&netlink_mgr);
	rcu_assign_pointer(xfrm_nl, NULL);
	synchronize_rcu();
	netlink_kernel_release(nlsk);
}

module_init(xfrm_user_init);
module_exit(xfrm_user_exit);
MODULE_LICENSE("GPL");
MODULE_ALIAS_NET_PF_PROTO(PF_NETLINK, NETLINK_XFRM);

