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
#include <net/ah.h>
#include <linux/uaccess.h>
#if IS_ENABLED(CONFIG_IPV6)
#include <linux/in6.h>
#endif
#include <asm/unaligned.h>

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
	case XFRMA_ALG_CRYPT:
	case XFRMA_ALG_COMP:
		break;

	default:
		return -EINVAL;
	}

	algp->alg_name[sizeof(algp->alg_name) - 1] = '\0';
	return 0;
}

static int verify_auth_trunc(struct nlattr **attrs)
{
	struct nlattr *rt = attrs[XFRMA_ALG_AUTH_TRUNC];
	struct xfrm_algo_auth *algp;

	if (!rt)
		return 0;

	algp = nla_data(rt);
	if (nla_len(rt) < xfrm_alg_auth_len(algp))
		return -EINVAL;

	algp->alg_name[sizeof(algp->alg_name) - 1] = '\0';
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

	algp->alg_name[sizeof(algp->alg_name) - 1] = '\0';
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

static inline int verify_replay(struct xfrm_usersa_info *p,
				struct nlattr **attrs)
{
	struct nlattr *rt = attrs[XFRMA_REPLAY_ESN_VAL];
	struct xfrm_replay_state_esn *rs;

	if (p->flags & XFRM_STATE_ESN) {
		if (!rt)
			return -EINVAL;

		rs = nla_data(rt);

		if (rs->bmp_len > XFRMA_REPLAY_ESN_MAX / sizeof(rs->bmp[0]) / 8)
			return -EINVAL;

		if (nla_len(rt) < xfrm_replay_state_esn_len(rs) &&
		    nla_len(rt) != sizeof(*rs))
			return -EINVAL;
	}

	if (!rt)
		return 0;

	/* As only ESP and AH support ESN feature. */
	if ((p->id.proto != IPPROTO_ESP) && (p->id.proto != IPPROTO_AH))
		return -EINVAL;

	if (p->replay_window != 0)
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
#if IS_ENABLED(CONFIG_IPV6)
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
		if ((!attrs[XFRMA_ALG_AUTH]	&&
		     !attrs[XFRMA_ALG_AUTH_TRUNC]) ||
		    attrs[XFRMA_ALG_AEAD]	||
		    attrs[XFRMA_ALG_CRYPT]	||
		    attrs[XFRMA_ALG_COMP]	||
		    attrs[XFRMA_TFCPAD])
			goto out;
		break;

	case IPPROTO_ESP:
		if (attrs[XFRMA_ALG_COMP])
			goto out;
		if (!attrs[XFRMA_ALG_AUTH] &&
		    !attrs[XFRMA_ALG_AUTH_TRUNC] &&
		    !attrs[XFRMA_ALG_CRYPT] &&
		    !attrs[XFRMA_ALG_AEAD])
			goto out;
		if ((attrs[XFRMA_ALG_AUTH] ||
		     attrs[XFRMA_ALG_AUTH_TRUNC] ||
		     attrs[XFRMA_ALG_CRYPT]) &&
		    attrs[XFRMA_ALG_AEAD])
			goto out;
		if (attrs[XFRMA_TFCPAD] &&
		    p->mode != XFRM_MODE_TUNNEL)
			goto out;
		break;

	case IPPROTO_COMP:
		if (!attrs[XFRMA_ALG_COMP]	||
		    attrs[XFRMA_ALG_AEAD]	||
		    attrs[XFRMA_ALG_AUTH]	||
		    attrs[XFRMA_ALG_AUTH_TRUNC]	||
		    attrs[XFRMA_ALG_CRYPT]	||
		    attrs[XFRMA_TFCPAD]		||
		    (ntohl(p->id.spi) >= 0x10000))
			goto out;
		break;

#if IS_ENABLED(CONFIG_IPV6)
	case IPPROTO_DSTOPTS:
	case IPPROTO_ROUTING:
		if (attrs[XFRMA_ALG_COMP]	||
		    attrs[XFRMA_ALG_AUTH]	||
		    attrs[XFRMA_ALG_AUTH_TRUNC]	||
		    attrs[XFRMA_ALG_AEAD]	||
		    attrs[XFRMA_ALG_CRYPT]	||
		    attrs[XFRMA_ENCAP]		||
		    attrs[XFRMA_SEC_CTX]	||
		    attrs[XFRMA_TFCPAD]		||
		    !attrs[XFRMA_COADDR])
			goto out;
		break;
#endif

	default:
		goto out;
	}

	if ((err = verify_aead(attrs)))
		goto out;
	if ((err = verify_auth_trunc(attrs)))
		goto out;
	if ((err = verify_one_alg(attrs, XFRMA_ALG_AUTH)))
		goto out;
	if ((err = verify_one_alg(attrs, XFRMA_ALG_CRYPT)))
		goto out;
	if ((err = verify_one_alg(attrs, XFRMA_ALG_COMP)))
		goto out;
	if ((err = verify_sec_ctx_len(attrs)))
		goto out;
	if ((err = verify_replay(p, attrs)))
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
			   struct xfrm_algo_desc *(*get_byname)(const char *, int),
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

static int attach_crypt(struct xfrm_state *x, struct nlattr *rta)
{
	struct xfrm_algo *p, *ualg;
	struct xfrm_algo_desc *algo;

	if (!rta)
		return 0;

	ualg = nla_data(rta);

	algo = xfrm_ealg_get_byname(ualg->alg_name, 1);
	if (!algo)
		return -ENOSYS;
	x->props.ealgo = algo->desc.sadb_alg_id;

	p = kmemdup(ualg, xfrm_alg_len(ualg), GFP_KERNEL);
	if (!p)
		return -ENOMEM;

	strcpy(p->alg_name, algo->name);
	x->ealg = p;
	x->geniv = algo->uinfo.encr.geniv;
	return 0;
}

static int attach_auth(struct xfrm_algo_auth **algpp, u8 *props,
		       struct nlattr *rta)
{
	struct xfrm_algo *ualg;
	struct xfrm_algo_auth *p;
	struct xfrm_algo_desc *algo;

	if (!rta)
		return 0;

	ualg = nla_data(rta);

	algo = xfrm_aalg_get_byname(ualg->alg_name, 1);
	if (!algo)
		return -ENOSYS;
	*props = algo->desc.sadb_alg_id;

	p = kmalloc(sizeof(*p) + (ualg->alg_key_len + 7) / 8, GFP_KERNEL);
	if (!p)
		return -ENOMEM;

	strcpy(p->alg_name, algo->name);
	p->alg_key_len = ualg->alg_key_len;
	p->alg_trunc_len = algo->uinfo.auth.icv_truncbits;
	memcpy(p->alg_key, ualg->alg_key, (ualg->alg_key_len + 7) / 8);

	*algpp = p;
	return 0;
}

static int attach_auth_trunc(struct xfrm_algo_auth **algpp, u8 *props,
			     struct nlattr *rta)
{
	struct xfrm_algo_auth *p, *ualg;
	struct xfrm_algo_desc *algo;

	if (!rta)
		return 0;

	ualg = nla_data(rta);

	algo = xfrm_aalg_get_byname(ualg->alg_name, 1);
	if (!algo)
		return -ENOSYS;
	if (ualg->alg_trunc_len > algo->uinfo.auth.icv_fullbits)
		return -EINVAL;
	*props = algo->desc.sadb_alg_id;

	p = kmemdup(ualg, xfrm_alg_auth_len(ualg), GFP_KERNEL);
	if (!p)
		return -ENOMEM;

	strcpy(p->alg_name, algo->name);
	if (!p->alg_trunc_len)
		p->alg_trunc_len = algo->uinfo.auth.icv_truncbits;

	*algpp = p;
	return 0;
}

static int attach_aead(struct xfrm_state *x, struct nlattr *rta)
{
	struct xfrm_algo_aead *p, *ualg;
	struct xfrm_algo_desc *algo;

	if (!rta)
		return 0;

	ualg = nla_data(rta);

	algo = xfrm_aead_get_byname(ualg->alg_name, ualg->alg_icv_len, 1);
	if (!algo)
		return -ENOSYS;
	x->props.ealgo = algo->desc.sadb_alg_id;

	p = kmemdup(ualg, aead_len(ualg), GFP_KERNEL);
	if (!p)
		return -ENOMEM;

	strcpy(p->alg_name, algo->name);
	x->aead = p;
	x->geniv = algo->uinfo.aead.geniv;
	return 0;
}

static inline int xfrm_replay_verify_len(struct xfrm_replay_state_esn *replay_esn,
					 struct nlattr *rp)
{
	struct xfrm_replay_state_esn *up;
	int ulen;

	if (!replay_esn || !rp)
		return 0;

	up = nla_data(rp);
	ulen = xfrm_replay_state_esn_len(up);

	/* Check the overall length and the internal bitmap length to avoid
	 * potential overflow. */
	if (nla_len(rp) < ulen ||
	    xfrm_replay_state_esn_len(replay_esn) != ulen ||
	    replay_esn->bmp_len != up->bmp_len)
		return -EINVAL;

	if (up->replay_window > up->bmp_len * sizeof(__u32) * 8)
		return -EINVAL;

	return 0;
}

static int xfrm_alloc_replay_state_esn(struct xfrm_replay_state_esn **replay_esn,
				       struct xfrm_replay_state_esn **preplay_esn,
				       struct nlattr *rta)
{
	struct xfrm_replay_state_esn *p, *pp, *up;
	int klen, ulen;

	if (!rta)
		return 0;

	up = nla_data(rta);
	klen = xfrm_replay_state_esn_len(up);
	ulen = nla_len(rta) >= klen ? klen : sizeof(*up);

	p = kzalloc(klen, GFP_KERNEL);
	if (!p)
		return -ENOMEM;

	pp = kzalloc(klen, GFP_KERNEL);
	if (!pp) {
		kfree(p);
		return -ENOMEM;
	}

	memcpy(p, up, ulen);
	memcpy(pp, up, ulen);

	*replay_esn = p;
	*preplay_esn = pp;

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
	x->props.replay_window = min_t(unsigned int, p->replay_window,
					sizeof(x->replay.bitmap) * 8);
	x->props.reqid = p->reqid;
	x->props.family = p->family;
	memcpy(&x->props.saddr, &p->saddr, sizeof(x->props.saddr));
	x->props.flags = p->flags;

	if (!x->sel.family && !(p->flags & XFRM_STATE_AF_UNSPEC))
		x->sel.family = p->family;
}

/*
 * someday when pfkey also has support, we could have the code
 * somehow made shareable and move it to xfrm_state.c - JHS
 *
*/
static void xfrm_update_ae_params(struct xfrm_state *x, struct nlattr **attrs,
				  int update_esn)
{
	struct nlattr *rp = attrs[XFRMA_REPLAY_VAL];
	struct nlattr *re = update_esn ? attrs[XFRMA_REPLAY_ESN_VAL] : NULL;
	struct nlattr *lt = attrs[XFRMA_LTIME_VAL];
	struct nlattr *et = attrs[XFRMA_ETIMER_THRESH];
	struct nlattr *rt = attrs[XFRMA_REPLAY_THRESH];

	if (re) {
		struct xfrm_replay_state_esn *replay_esn;
		replay_esn = nla_data(re);
		memcpy(x->replay_esn, replay_esn,
		       xfrm_replay_state_esn_len(replay_esn));
		memcpy(x->preplay_esn, replay_esn,
		       xfrm_replay_state_esn_len(replay_esn));
	}

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

static struct xfrm_state *xfrm_state_construct(struct net *net,
					       struct xfrm_usersa_info *p,
					       struct nlattr **attrs,
					       int *errp)
{
	struct xfrm_state *x = xfrm_state_alloc(net);
	int err = -ENOMEM;

	if (!x)
		goto error_no_put;

	copy_from_user_state(x, p);

	if (attrs[XFRMA_SA_EXTRA_FLAGS])
		x->props.extra_flags = nla_get_u32(attrs[XFRMA_SA_EXTRA_FLAGS]);

	if ((err = attach_aead(x, attrs[XFRMA_ALG_AEAD])))
		goto error;
	if ((err = attach_auth_trunc(&x->aalg, &x->props.aalgo,
				     attrs[XFRMA_ALG_AUTH_TRUNC])))
		goto error;
	if (!x->props.aalgo) {
		if ((err = attach_auth(&x->aalg, &x->props.aalgo,
				       attrs[XFRMA_ALG_AUTH])))
			goto error;
	}
	if ((err = attach_crypt(x, attrs[XFRMA_ALG_CRYPT])))
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

	if (attrs[XFRMA_TFCPAD])
		x->tfcpad = nla_get_u32(attrs[XFRMA_TFCPAD]);

	if (attrs[XFRMA_COADDR]) {
		x->coaddr = kmemdup(nla_data(attrs[XFRMA_COADDR]),
				    sizeof(*x->coaddr), GFP_KERNEL);
		if (x->coaddr == NULL)
			goto error;
	}

	xfrm_mark_get(attrs, &x->mark);

	if (attrs[XFRMA_OUTPUT_MARK])
		x->props.output_mark = nla_get_u32(attrs[XFRMA_OUTPUT_MARK]);

	err = __xfrm_init_state(x, false, attrs[XFRMA_OFFLOAD_DEV]);
	if (err)
		goto error;

	if (attrs[XFRMA_SEC_CTX]) {
		err = security_xfrm_state_alloc(x,
						nla_data(attrs[XFRMA_SEC_CTX]));
		if (err)
			goto error;
	}

	if (attrs[XFRMA_OFFLOAD_DEV]) {
		err = xfrm_dev_state_add(net, x,
					 nla_data(attrs[XFRMA_OFFLOAD_DEV]));
		if (err)
			goto error;
	}

	if ((err = xfrm_alloc_replay_state_esn(&x->replay_esn, &x->preplay_esn,
					       attrs[XFRMA_REPLAY_ESN_VAL])))
		goto error;

	x->km.seq = p->seq;
	x->replay_maxdiff = net->xfrm.sysctl_aevent_rseqth;
	/* sysctl_xfrm_aevent_etime is in 100ms units */
	x->replay_maxage = (net->xfrm.sysctl_aevent_etime*HZ)/XFRM_AE_ETH_M;

	if ((err = xfrm_init_replay(x)))
		goto error;

	/* override default values from above */
	xfrm_update_ae_params(x, attrs, 0);

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
	struct net *net = sock_net(skb->sk);
	struct xfrm_usersa_info *p = nlmsg_data(nlh);
	struct xfrm_state *x;
	int err;
	struct km_event c;

	err = verify_newsa_info(p, attrs);
	if (err)
		return err;

	x = xfrm_state_construct(net, p, attrs, &err);
	if (!x)
		return err;

	xfrm_state_hold(x);
	if (nlh->nlmsg_type == XFRM_MSG_NEWSA)
		err = xfrm_state_add(x);
	else
		err = xfrm_state_update(x);

	xfrm_audit_state_add(x, err ? 0 : 1, true);

	if (err < 0) {
		x->km.state = XFRM_STATE_DEAD;
		xfrm_dev_state_delete(x);
		__xfrm_state_put(x);
		goto out;
	}

	c.seq = nlh->nlmsg_seq;
	c.portid = nlh->nlmsg_pid;
	c.event = nlh->nlmsg_type;

	km_state_notify(x, &c);
out:
	xfrm_state_put(x);
	return err;
}

static struct xfrm_state *xfrm_user_state_lookup(struct net *net,
						 struct xfrm_usersa_id *p,
						 struct nlattr **attrs,
						 int *errp)
{
	struct xfrm_state *x = NULL;
	struct xfrm_mark m;
	int err;
	u32 mark = xfrm_mark_get(attrs, &m);

	if (xfrm_id_proto_match(p->proto, IPSEC_PROTO_ANY)) {
		err = -ESRCH;
		x = xfrm_state_lookup(net, mark, &p->daddr, p->spi, p->proto, p->family);
	} else {
		xfrm_address_t *saddr = NULL;

		verify_one_addr(attrs, XFRMA_SRCADDR, &saddr);
		if (!saddr) {
			err = -EINVAL;
			goto out;
		}

		err = -ESRCH;
		x = xfrm_state_lookup_byaddr(net, mark,
					     &p->daddr, saddr,
					     p->proto, p->family);
	}

 out:
	if (!x && errp)
		*errp = err;
	return x;
}

static int xfrm_del_sa(struct sk_buff *skb, struct nlmsghdr *nlh,
		struct nlattr **attrs)
{
	struct net *net = sock_net(skb->sk);
	struct xfrm_state *x;
	int err = -ESRCH;
	struct km_event c;
	struct xfrm_usersa_id *p = nlmsg_data(nlh);

	x = xfrm_user_state_lookup(net, p, attrs, &err);
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
	c.portid = nlh->nlmsg_pid;
	c.event = nlh->nlmsg_type;
	km_state_notify(x, &c);

out:
	xfrm_audit_state_delete(x, err ? 0 : 1, true);
	xfrm_state_put(x);
	return err;
}

static void copy_to_user_state(struct xfrm_state *x, struct xfrm_usersa_info *p)
{
	memset(p, 0, sizeof(*p));
	memcpy(&p->id, &x->id, sizeof(p->id));
	memcpy(&p->sel, &x->sel, sizeof(p->sel));
	memcpy(&p->lft, &x->lft, sizeof(p->lft));
	memcpy(&p->curlft, &x->curlft, sizeof(p->curlft));
	put_unaligned(x->stats.replay_window, &p->stats.replay_window);
	put_unaligned(x->stats.replay, &p->stats.replay);
	put_unaligned(x->stats.integrity_failed, &p->stats.integrity_failed);
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

static int copy_user_offload(struct xfrm_state_offload *xso, struct sk_buff *skb)
{
	struct xfrm_user_offload *xuo;
	struct nlattr *attr;

	attr = nla_reserve(skb, XFRMA_OFFLOAD_DEV, sizeof(*xuo));
	if (attr == NULL)
		return -EMSGSIZE;

	xuo = nla_data(attr);
	memset(xuo, 0, sizeof(*xuo));
	xuo->ifindex = xso->dev->ifindex;
	xuo->flags = xso->flags;

	return 0;
}

static int copy_to_user_auth(struct xfrm_algo_auth *auth, struct sk_buff *skb)
{
	struct xfrm_algo *algo;
	struct nlattr *nla;

	nla = nla_reserve(skb, XFRMA_ALG_AUTH,
			  sizeof(*algo) + (auth->alg_key_len + 7) / 8);
	if (!nla)
		return -EMSGSIZE;

	algo = nla_data(nla);
	strncpy(algo->alg_name, auth->alg_name, sizeof(algo->alg_name));
	memcpy(algo->alg_key, auth->alg_key, (auth->alg_key_len + 7) / 8);
	algo->alg_key_len = auth->alg_key_len;

	return 0;
}

/* Don't change this without updating xfrm_sa_len! */
static int copy_to_user_state_extra(struct xfrm_state *x,
				    struct xfrm_usersa_info *p,
				    struct sk_buff *skb)
{
	int ret = 0;

	copy_to_user_state(x, p);

	if (x->props.extra_flags) {
		ret = nla_put_u32(skb, XFRMA_SA_EXTRA_FLAGS,
				  x->props.extra_flags);
		if (ret)
			goto out;
	}

	if (x->coaddr) {
		ret = nla_put(skb, XFRMA_COADDR, sizeof(*x->coaddr), x->coaddr);
		if (ret)
			goto out;
	}
	if (x->lastused) {
		ret = nla_put_u64_64bit(skb, XFRMA_LASTUSED, x->lastused,
					XFRMA_PAD);
		if (ret)
			goto out;
	}
	if (x->aead) {
		ret = nla_put(skb, XFRMA_ALG_AEAD, aead_len(x->aead), x->aead);
		if (ret)
			goto out;
	}
	if (x->aalg) {
		ret = copy_to_user_auth(x->aalg, skb);
		if (!ret)
			ret = nla_put(skb, XFRMA_ALG_AUTH_TRUNC,
				      xfrm_alg_auth_len(x->aalg), x->aalg);
		if (ret)
			goto out;
	}
	if (x->ealg) {
		ret = nla_put(skb, XFRMA_ALG_CRYPT, xfrm_alg_len(x->ealg), x->ealg);
		if (ret)
			goto out;
	}
	if (x->calg) {
		ret = nla_put(skb, XFRMA_ALG_COMP, sizeof(*(x->calg)), x->calg);
		if (ret)
			goto out;
	}
	if (x->encap) {
		ret = nla_put(skb, XFRMA_ENCAP, sizeof(*x->encap), x->encap);
		if (ret)
			goto out;
	}
	if (x->tfcpad) {
		ret = nla_put_u32(skb, XFRMA_TFCPAD, x->tfcpad);
		if (ret)
			goto out;
	}
	ret = xfrm_mark_put(skb, &x->mark);
	if (ret)
		goto out;
	if (x->replay_esn)
		ret = nla_put(skb, XFRMA_REPLAY_ESN_VAL,
			      xfrm_replay_state_esn_len(x->replay_esn),
			      x->replay_esn);
	else
		ret = nla_put(skb, XFRMA_REPLAY_VAL, sizeof(x->replay),
			      &x->replay);
	if (ret)
		goto out;
	if(x->xso.dev)
		ret = copy_user_offload(&x->xso, skb);
	if (ret)
		goto out;
	if (x->props.output_mark) {
		ret = nla_put_u32(skb, XFRMA_OUTPUT_MARK, x->props.output_mark);
		if (ret)
			goto out;
	}
	if (x->security)
		ret = copy_sec_ctx(x->security, skb);
out:
	return ret;
}

static int dump_one_state(struct xfrm_state *x, int count, void *ptr)
{
	struct xfrm_dump_info *sp = ptr;
	struct sk_buff *in_skb = sp->in_skb;
	struct sk_buff *skb = sp->out_skb;
	struct xfrm_usersa_info *p;
	struct nlmsghdr *nlh;
	int err;

	nlh = nlmsg_put(skb, NETLINK_CB(in_skb).portid, sp->nlmsg_seq,
			XFRM_MSG_NEWSA, sizeof(*p), sp->nlmsg_flags);
	if (nlh == NULL)
		return -EMSGSIZE;

	p = nlmsg_data(nlh);

	err = copy_to_user_state_extra(x, p, skb);
	if (err) {
		nlmsg_cancel(skb, nlh);
		return err;
	}
	nlmsg_end(skb, nlh);
	return 0;
}

static int xfrm_dump_sa_done(struct netlink_callback *cb)
{
	struct xfrm_state_walk *walk = (struct xfrm_state_walk *) &cb->args[1];
	struct sock *sk = cb->skb->sk;
	struct net *net = sock_net(sk);

	if (cb->args[0])
		xfrm_state_walk_done(walk, net);
	return 0;
}

static const struct nla_policy xfrma_policy[XFRMA_MAX+1];
static int xfrm_dump_sa(struct sk_buff *skb, struct netlink_callback *cb)
{
	struct net *net = sock_net(skb->sk);
	struct xfrm_state_walk *walk = (struct xfrm_state_walk *) &cb->args[1];
	struct xfrm_dump_info info;

	BUILD_BUG_ON(sizeof(struct xfrm_state_walk) >
		     sizeof(cb->args) - sizeof(cb->args[0]));

	info.in_skb = cb->skb;
	info.out_skb = skb;
	info.nlmsg_seq = cb->nlh->nlmsg_seq;
	info.nlmsg_flags = NLM_F_MULTI;

	if (!cb->args[0]) {
		struct nlattr *attrs[XFRMA_MAX+1];
		struct xfrm_address_filter *filter = NULL;
		u8 proto = 0;
		int err;

		err = nlmsg_parse(cb->nlh, 0, attrs, XFRMA_MAX, xfrma_policy,
				  NULL);
		if (err < 0)
			return err;

		if (attrs[XFRMA_ADDRESS_FILTER]) {
			filter = kmemdup(nla_data(attrs[XFRMA_ADDRESS_FILTER]),
					 sizeof(*filter), GFP_KERNEL);
			if (filter == NULL)
				return -ENOMEM;
		}

		if (attrs[XFRMA_PROTO])
			proto = nla_get_u8(attrs[XFRMA_PROTO]);

		xfrm_state_walk_init(walk, proto, filter);
		cb->args[0] = 1;
	}

	(void) xfrm_state_walk(net, walk, dump_one_state, &info);

	return skb->len;
}

static struct sk_buff *xfrm_state_netlink(struct sk_buff *in_skb,
					  struct xfrm_state *x, u32 seq)
{
	struct xfrm_dump_info info;
	struct sk_buff *skb;
	int err;

	skb = nlmsg_new(NLMSG_DEFAULT_SIZE, GFP_ATOMIC);
	if (!skb)
		return ERR_PTR(-ENOMEM);

	info.in_skb = in_skb;
	info.out_skb = skb;
	info.nlmsg_seq = seq;
	info.nlmsg_flags = 0;

	err = dump_one_state(x, 0, &info);
	if (err) {
		kfree_skb(skb);
		return ERR_PTR(err);
	}

	return skb;
}

/* A wrapper for nlmsg_multicast() checking that nlsk is still available.
 * Must be called with RCU read lock.
 */
static inline int xfrm_nlmsg_multicast(struct net *net, struct sk_buff *skb,
				       u32 pid, unsigned int group)
{
	struct sock *nlsk = rcu_dereference(net->xfrm.nlsk);

	if (nlsk)
		return nlmsg_multicast(nlsk, skb, pid, group, GFP_ATOMIC);
	else
		return -1;
}

static inline size_t xfrm_spdinfo_msgsize(void)
{
	return NLMSG_ALIGN(4)
	       + nla_total_size(sizeof(struct xfrmu_spdinfo))
	       + nla_total_size(sizeof(struct xfrmu_spdhinfo))
	       + nla_total_size(sizeof(struct xfrmu_spdhthresh))
	       + nla_total_size(sizeof(struct xfrmu_spdhthresh));
}

static int build_spdinfo(struct sk_buff *skb, struct net *net,
			 u32 portid, u32 seq, u32 flags)
{
	struct xfrmk_spdinfo si;
	struct xfrmu_spdinfo spc;
	struct xfrmu_spdhinfo sph;
	struct xfrmu_spdhthresh spt4, spt6;
	struct nlmsghdr *nlh;
	int err;
	u32 *f;
	unsigned lseq;

	nlh = nlmsg_put(skb, portid, seq, XFRM_MSG_NEWSPDINFO, sizeof(u32), 0);
	if (nlh == NULL) /* shouldn't really happen ... */
		return -EMSGSIZE;

	f = nlmsg_data(nlh);
	*f = flags;
	xfrm_spd_getinfo(net, &si);
	spc.incnt = si.incnt;
	spc.outcnt = si.outcnt;
	spc.fwdcnt = si.fwdcnt;
	spc.inscnt = si.inscnt;
	spc.outscnt = si.outscnt;
	spc.fwdscnt = si.fwdscnt;
	sph.spdhcnt = si.spdhcnt;
	sph.spdhmcnt = si.spdhmcnt;

	do {
		lseq = read_seqbegin(&net->xfrm.policy_hthresh.lock);

		spt4.lbits = net->xfrm.policy_hthresh.lbits4;
		spt4.rbits = net->xfrm.policy_hthresh.rbits4;
		spt6.lbits = net->xfrm.policy_hthresh.lbits6;
		spt6.rbits = net->xfrm.policy_hthresh.rbits6;
	} while (read_seqretry(&net->xfrm.policy_hthresh.lock, lseq));

	err = nla_put(skb, XFRMA_SPD_INFO, sizeof(spc), &spc);
	if (!err)
		err = nla_put(skb, XFRMA_SPD_HINFO, sizeof(sph), &sph);
	if (!err)
		err = nla_put(skb, XFRMA_SPD_IPV4_HTHRESH, sizeof(spt4), &spt4);
	if (!err)
		err = nla_put(skb, XFRMA_SPD_IPV6_HTHRESH, sizeof(spt6), &spt6);
	if (err) {
		nlmsg_cancel(skb, nlh);
		return err;
	}

	nlmsg_end(skb, nlh);
	return 0;
}

static int xfrm_set_spdinfo(struct sk_buff *skb, struct nlmsghdr *nlh,
			    struct nlattr **attrs)
{
	struct net *net = sock_net(skb->sk);
	struct xfrmu_spdhthresh *thresh4 = NULL;
	struct xfrmu_spdhthresh *thresh6 = NULL;

	/* selector prefixlen thresholds to hash policies */
	if (attrs[XFRMA_SPD_IPV4_HTHRESH]) {
		struct nlattr *rta = attrs[XFRMA_SPD_IPV4_HTHRESH];

		if (nla_len(rta) < sizeof(*thresh4))
			return -EINVAL;
		thresh4 = nla_data(rta);
		if (thresh4->lbits > 32 || thresh4->rbits > 32)
			return -EINVAL;
	}
	if (attrs[XFRMA_SPD_IPV6_HTHRESH]) {
		struct nlattr *rta = attrs[XFRMA_SPD_IPV6_HTHRESH];

		if (nla_len(rta) < sizeof(*thresh6))
			return -EINVAL;
		thresh6 = nla_data(rta);
		if (thresh6->lbits > 128 || thresh6->rbits > 128)
			return -EINVAL;
	}

	if (thresh4 || thresh6) {
		write_seqlock(&net->xfrm.policy_hthresh.lock);
		if (thresh4) {
			net->xfrm.policy_hthresh.lbits4 = thresh4->lbits;
			net->xfrm.policy_hthresh.rbits4 = thresh4->rbits;
		}
		if (thresh6) {
			net->xfrm.policy_hthresh.lbits6 = thresh6->lbits;
			net->xfrm.policy_hthresh.rbits6 = thresh6->rbits;
		}
		write_sequnlock(&net->xfrm.policy_hthresh.lock);

		xfrm_policy_hash_rebuild(net);
	}

	return 0;
}

static int xfrm_get_spdinfo(struct sk_buff *skb, struct nlmsghdr *nlh,
		struct nlattr **attrs)
{
	struct net *net = sock_net(skb->sk);
	struct sk_buff *r_skb;
	u32 *flags = nlmsg_data(nlh);
	u32 sportid = NETLINK_CB(skb).portid;
	u32 seq = nlh->nlmsg_seq;

	r_skb = nlmsg_new(xfrm_spdinfo_msgsize(), GFP_ATOMIC);
	if (r_skb == NULL)
		return -ENOMEM;

	if (build_spdinfo(r_skb, net, sportid, seq, *flags) < 0)
		BUG();

	return nlmsg_unicast(net->xfrm.nlsk, r_skb, sportid);
}

static inline size_t xfrm_sadinfo_msgsize(void)
{
	return NLMSG_ALIGN(4)
	       + nla_total_size(sizeof(struct xfrmu_sadhinfo))
	       + nla_total_size(4); /* XFRMA_SAD_CNT */
}

static int build_sadinfo(struct sk_buff *skb, struct net *net,
			 u32 portid, u32 seq, u32 flags)
{
	struct xfrmk_sadinfo si;
	struct xfrmu_sadhinfo sh;
	struct nlmsghdr *nlh;
	int err;
	u32 *f;

	nlh = nlmsg_put(skb, portid, seq, XFRM_MSG_NEWSADINFO, sizeof(u32), 0);
	if (nlh == NULL) /* shouldn't really happen ... */
		return -EMSGSIZE;

	f = nlmsg_data(nlh);
	*f = flags;
	xfrm_sad_getinfo(net, &si);

	sh.sadhmcnt = si.sadhmcnt;
	sh.sadhcnt = si.sadhcnt;

	err = nla_put_u32(skb, XFRMA_SAD_CNT, si.sadcnt);
	if (!err)
		err = nla_put(skb, XFRMA_SAD_HINFO, sizeof(sh), &sh);
	if (err) {
		nlmsg_cancel(skb, nlh);
		return err;
	}

	nlmsg_end(skb, nlh);
	return 0;
}

static int xfrm_get_sadinfo(struct sk_buff *skb, struct nlmsghdr *nlh,
		struct nlattr **attrs)
{
	struct net *net = sock_net(skb->sk);
	struct sk_buff *r_skb;
	u32 *flags = nlmsg_data(nlh);
	u32 sportid = NETLINK_CB(skb).portid;
	u32 seq = nlh->nlmsg_seq;

	r_skb = nlmsg_new(xfrm_sadinfo_msgsize(), GFP_ATOMIC);
	if (r_skb == NULL)
		return -ENOMEM;

	if (build_sadinfo(r_skb, net, sportid, seq, *flags) < 0)
		BUG();

	return nlmsg_unicast(net->xfrm.nlsk, r_skb, sportid);
}

static int xfrm_get_sa(struct sk_buff *skb, struct nlmsghdr *nlh,
		struct nlattr **attrs)
{
	struct net *net = sock_net(skb->sk);
	struct xfrm_usersa_id *p = nlmsg_data(nlh);
	struct xfrm_state *x;
	struct sk_buff *resp_skb;
	int err = -ESRCH;

	x = xfrm_user_state_lookup(net, p, attrs, &err);
	if (x == NULL)
		goto out_noput;

	resp_skb = xfrm_state_netlink(skb, x, nlh->nlmsg_seq);
	if (IS_ERR(resp_skb)) {
		err = PTR_ERR(resp_skb);
	} else {
		err = nlmsg_unicast(net->xfrm.nlsk, resp_skb, NETLINK_CB(skb).portid);
	}
	xfrm_state_put(x);
out_noput:
	return err;
}

static int xfrm_alloc_userspi(struct sk_buff *skb, struct nlmsghdr *nlh,
		struct nlattr **attrs)
{
	struct net *net = sock_net(skb->sk);
	struct xfrm_state *x;
	struct xfrm_userspi_info *p;
	struct sk_buff *resp_skb;
	xfrm_address_t *daddr;
	int family;
	int err;
	u32 mark;
	struct xfrm_mark m;

	p = nlmsg_data(nlh);
	err = verify_spi_info(p->info.id.proto, p->min, p->max);
	if (err)
		goto out_noput;

	family = p->info.family;
	daddr = &p->info.id.daddr;

	x = NULL;

	mark = xfrm_mark_get(attrs, &m);
	if (p->info.seq) {
		x = xfrm_find_acq_byseq(net, mark, p->info.seq);
		if (x && !xfrm_addr_equal(&x->id.daddr, daddr, family)) {
			xfrm_state_put(x);
			x = NULL;
		}
	}

	if (!x)
		x = xfrm_find_acq(net, &m, p->info.mode, p->info.reqid,
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

	err = nlmsg_unicast(net->xfrm.nlsk, resp_skb, NETLINK_CB(skb).portid);

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
	int ret;

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
#if IS_ENABLED(CONFIG_IPV6)
		break;
#else
		return  -EAFNOSUPPORT;
#endif

	default:
		return -EINVAL;
	}

	ret = verify_policy_dir(p->dir);
	if (ret)
		return ret;
	if (p->index && ((p->index & XFRM_POLICY_MAX) != p->dir))
		return -EINVAL;

	return 0;
}

static int copy_from_user_sec_ctx(struct xfrm_policy *pol, struct nlattr **attrs)
{
	struct nlattr *rt = attrs[XFRMA_SEC_CTX];
	struct xfrm_user_sec_ctx *uctx;

	if (!rt)
		return 0;

	uctx = nla_data(rt);
	return security_xfrm_policy_alloc(&pol->security, uctx, GFP_KERNEL);
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
		/* If all masks are ~0, then we allow all algorithms. */
		t->allalgs = !~(t->aalgos & t->ealgos & t->calgos);
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
#if IS_ENABLED(CONFIG_IPV6)
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
	memset(p, 0, sizeof(*p));
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

static struct xfrm_policy *xfrm_policy_construct(struct net *net, struct xfrm_userpolicy_info *p, struct nlattr **attrs, int *errp)
{
	struct xfrm_policy *xp = xfrm_policy_alloc(net, GFP_KERNEL);
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

	xfrm_mark_get(attrs, &xp->mark);

	return xp;
 error:
	*errp = err;
	xp->walk.dead = 1;
	xfrm_policy_destroy(xp);
	return NULL;
}

static int xfrm_add_policy(struct sk_buff *skb, struct nlmsghdr *nlh,
		struct nlattr **attrs)
{
	struct net *net = sock_net(skb->sk);
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

	xp = xfrm_policy_construct(net, p, attrs, &err);
	if (!xp)
		return err;

	/* shouldn't excl be based on nlh flags??
	 * Aha! this is anti-netlink really i.e  more pfkey derived
	 * in netlink excl is a flag and you wouldnt need
	 * a type XFRM_MSG_UPDPOLICY - JHS */
	excl = nlh->nlmsg_type == XFRM_MSG_NEWPOLICY;
	err = xfrm_policy_insert(p->dir, xp, excl);
	xfrm_audit_policy_add(xp, err ? 0 : 1, true);

	if (err) {
		security_xfrm_policy_free(xp->security);
		kfree(xp);
		return err;
	}

	c.event = nlh->nlmsg_type;
	c.seq = nlh->nlmsg_seq;
	c.portid = nlh->nlmsg_pid;
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

		memset(up, 0, sizeof(*up));
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
	if (xp->security)
		return copy_sec_ctx(xp->security, skb);
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
	int err;

	nlh = nlmsg_put(skb, NETLINK_CB(in_skb).portid, sp->nlmsg_seq,
			XFRM_MSG_NEWPOLICY, sizeof(*p), sp->nlmsg_flags);
	if (nlh == NULL)
		return -EMSGSIZE;

	p = nlmsg_data(nlh);
	copy_to_user_policy(xp, p, dir);
	err = copy_to_user_tmpl(xp, skb);
	if (!err)
		err = copy_to_user_sec_ctx(xp, skb);
	if (!err)
		err = copy_to_user_policy_type(xp->type, skb);
	if (!err)
		err = xfrm_mark_put(skb, &xp->mark);
	if (err) {
		nlmsg_cancel(skb, nlh);
		return err;
	}
	nlmsg_end(skb, nlh);
	return 0;
}

static int xfrm_dump_policy_done(struct netlink_callback *cb)
{
	struct xfrm_policy_walk *walk = (struct xfrm_policy_walk *) &cb->args[1];
	struct net *net = sock_net(cb->skb->sk);

	xfrm_policy_walk_done(walk, net);
	return 0;
}

static int xfrm_dump_policy(struct sk_buff *skb, struct netlink_callback *cb)
{
	struct net *net = sock_net(skb->sk);
	struct xfrm_policy_walk *walk = (struct xfrm_policy_walk *) &cb->args[1];
	struct xfrm_dump_info info;

	BUILD_BUG_ON(sizeof(struct xfrm_policy_walk) >
		     sizeof(cb->args) - sizeof(cb->args[0]));

	info.in_skb = cb->skb;
	info.out_skb = skb;
	info.nlmsg_seq = cb->nlh->nlmsg_seq;
	info.nlmsg_flags = NLM_F_MULTI;

	if (!cb->args[0]) {
		cb->args[0] = 1;
		xfrm_policy_walk_init(walk, XFRM_POLICY_TYPE_ANY);
	}

	(void) xfrm_policy_walk(net, walk, dump_one_policy, &info);

	return skb->len;
}

static struct sk_buff *xfrm_policy_netlink(struct sk_buff *in_skb,
					  struct xfrm_policy *xp,
					  int dir, u32 seq)
{
	struct xfrm_dump_info info;
	struct sk_buff *skb;
	int err;

	skb = nlmsg_new(NLMSG_DEFAULT_SIZE, GFP_KERNEL);
	if (!skb)
		return ERR_PTR(-ENOMEM);

	info.in_skb = in_skb;
	info.out_skb = skb;
	info.nlmsg_seq = seq;
	info.nlmsg_flags = 0;

	err = dump_one_policy(xp, dir, 0, &info);
	if (err) {
		kfree_skb(skb);
		return ERR_PTR(err);
	}

	return skb;
}

static int xfrm_get_policy(struct sk_buff *skb, struct nlmsghdr *nlh,
		struct nlattr **attrs)
{
	struct net *net = sock_net(skb->sk);
	struct xfrm_policy *xp;
	struct xfrm_userpolicy_id *p;
	u8 type = XFRM_POLICY_TYPE_MAIN;
	int err;
	struct km_event c;
	int delete;
	struct xfrm_mark m;
	u32 mark = xfrm_mark_get(attrs, &m);

	p = nlmsg_data(nlh);
	delete = nlh->nlmsg_type == XFRM_MSG_DELPOLICY;

	err = copy_from_user_policy_type(&type, attrs);
	if (err)
		return err;

	err = verify_policy_dir(p->dir);
	if (err)
		return err;

	if (p->index)
		xp = xfrm_policy_byid(net, mark, type, p->dir, p->index, delete, &err);
	else {
		struct nlattr *rt = attrs[XFRMA_SEC_CTX];
		struct xfrm_sec_ctx *ctx;

		err = verify_sec_ctx_len(attrs);
		if (err)
			return err;

		ctx = NULL;
		if (rt) {
			struct xfrm_user_sec_ctx *uctx = nla_data(rt);

			err = security_xfrm_policy_alloc(&ctx, uctx, GFP_KERNEL);
			if (err)
				return err;
		}
		xp = xfrm_policy_bysel_ctx(net, mark, type, p->dir, &p->sel,
					   ctx, delete, &err);
		security_xfrm_policy_free(ctx);
	}
	if (xp == NULL)
		return -ENOENT;

	if (!delete) {
		struct sk_buff *resp_skb;

		resp_skb = xfrm_policy_netlink(skb, xp, p->dir, nlh->nlmsg_seq);
		if (IS_ERR(resp_skb)) {
			err = PTR_ERR(resp_skb);
		} else {
			err = nlmsg_unicast(net->xfrm.nlsk, resp_skb,
					    NETLINK_CB(skb).portid);
		}
	} else {
		xfrm_audit_policy_delete(xp, err ? 0 : 1, true);

		if (err != 0)
			goto out;

		c.data.byid = p->index;
		c.event = nlh->nlmsg_type;
		c.seq = nlh->nlmsg_seq;
		c.portid = nlh->nlmsg_pid;
		km_policy_notify(xp, p->dir, &c);
	}

out:
	xfrm_pol_put(xp);
	return err;
}

static int xfrm_flush_sa(struct sk_buff *skb, struct nlmsghdr *nlh,
		struct nlattr **attrs)
{
	struct net *net = sock_net(skb->sk);
	struct km_event c;
	struct xfrm_usersa_flush *p = nlmsg_data(nlh);
	int err;

	err = xfrm_state_flush(net, p->proto, true);
	if (err) {
		if (err == -ESRCH) /* empty table */
			return 0;
		return err;
	}
	c.data.proto = p->proto;
	c.event = nlh->nlmsg_type;
	c.seq = nlh->nlmsg_seq;
	c.portid = nlh->nlmsg_pid;
	c.net = net;
	km_state_notify(NULL, &c);

	return 0;
}

static inline size_t xfrm_aevent_msgsize(struct xfrm_state *x)
{
	size_t replay_size = x->replay_esn ?
			      xfrm_replay_state_esn_len(x->replay_esn) :
			      sizeof(struct xfrm_replay_state);

	return NLMSG_ALIGN(sizeof(struct xfrm_aevent_id))
	       + nla_total_size(replay_size)
	       + nla_total_size_64bit(sizeof(struct xfrm_lifetime_cur))
	       + nla_total_size(sizeof(struct xfrm_mark))
	       + nla_total_size(4) /* XFRM_AE_RTHR */
	       + nla_total_size(4); /* XFRM_AE_ETHR */
}

static int build_aevent(struct sk_buff *skb, struct xfrm_state *x, const struct km_event *c)
{
	struct xfrm_aevent_id *id;
	struct nlmsghdr *nlh;
	int err;

	nlh = nlmsg_put(skb, c->portid, c->seq, XFRM_MSG_NEWAE, sizeof(*id), 0);
	if (nlh == NULL)
		return -EMSGSIZE;

	id = nlmsg_data(nlh);
	memset(&id->sa_id, 0, sizeof(id->sa_id));
	memcpy(&id->sa_id.daddr, &x->id.daddr, sizeof(x->id.daddr));
	id->sa_id.spi = x->id.spi;
	id->sa_id.family = x->props.family;
	id->sa_id.proto = x->id.proto;
	memcpy(&id->saddr, &x->props.saddr, sizeof(x->props.saddr));
	id->reqid = x->props.reqid;
	id->flags = c->data.aevent;

	if (x->replay_esn) {
		err = nla_put(skb, XFRMA_REPLAY_ESN_VAL,
			      xfrm_replay_state_esn_len(x->replay_esn),
			      x->replay_esn);
	} else {
		err = nla_put(skb, XFRMA_REPLAY_VAL, sizeof(x->replay),
			      &x->replay);
	}
	if (err)
		goto out_cancel;
	err = nla_put_64bit(skb, XFRMA_LTIME_VAL, sizeof(x->curlft), &x->curlft,
			    XFRMA_PAD);
	if (err)
		goto out_cancel;

	if (id->flags & XFRM_AE_RTHR) {
		err = nla_put_u32(skb, XFRMA_REPLAY_THRESH, x->replay_maxdiff);
		if (err)
			goto out_cancel;
	}
	if (id->flags & XFRM_AE_ETHR) {
		err = nla_put_u32(skb, XFRMA_ETIMER_THRESH,
				  x->replay_maxage * 10 / HZ);
		if (err)
			goto out_cancel;
	}
	err = xfrm_mark_put(skb, &x->mark);
	if (err)
		goto out_cancel;

	nlmsg_end(skb, nlh);
	return 0;

out_cancel:
	nlmsg_cancel(skb, nlh);
	return err;
}

static int xfrm_get_ae(struct sk_buff *skb, struct nlmsghdr *nlh,
		struct nlattr **attrs)
{
	struct net *net = sock_net(skb->sk);
	struct xfrm_state *x;
	struct sk_buff *r_skb;
	int err;
	struct km_event c;
	u32 mark;
	struct xfrm_mark m;
	struct xfrm_aevent_id *p = nlmsg_data(nlh);
	struct xfrm_usersa_id *id = &p->sa_id;

	mark = xfrm_mark_get(attrs, &m);

	x = xfrm_state_lookup(net, mark, &id->daddr, id->spi, id->proto, id->family);
	if (x == NULL)
		return -ESRCH;

	r_skb = nlmsg_new(xfrm_aevent_msgsize(x), GFP_ATOMIC);
	if (r_skb == NULL) {
		xfrm_state_put(x);
		return -ENOMEM;
	}

	/*
	 * XXX: is this lock really needed - none of the other
	 * gets lock (the concern is things getting updated
	 * while we are still reading) - jhs
	*/
	spin_lock_bh(&x->lock);
	c.data.aevent = p->flags;
	c.seq = nlh->nlmsg_seq;
	c.portid = nlh->nlmsg_pid;

	if (build_aevent(r_skb, x, &c) < 0)
		BUG();
	err = nlmsg_unicast(net->xfrm.nlsk, r_skb, NETLINK_CB(skb).portid);
	spin_unlock_bh(&x->lock);
	xfrm_state_put(x);
	return err;
}

static int xfrm_new_ae(struct sk_buff *skb, struct nlmsghdr *nlh,
		struct nlattr **attrs)
{
	struct net *net = sock_net(skb->sk);
	struct xfrm_state *x;
	struct km_event c;
	int err = -EINVAL;
	u32 mark = 0;
	struct xfrm_mark m;
	struct xfrm_aevent_id *p = nlmsg_data(nlh);
	struct nlattr *rp = attrs[XFRMA_REPLAY_VAL];
	struct nlattr *re = attrs[XFRMA_REPLAY_ESN_VAL];
	struct nlattr *lt = attrs[XFRMA_LTIME_VAL];
	struct nlattr *et = attrs[XFRMA_ETIMER_THRESH];
	struct nlattr *rt = attrs[XFRMA_REPLAY_THRESH];

	if (!lt && !rp && !re && !et && !rt)
		return err;

	/* pedantic mode - thou shalt sayeth replaceth */
	if (!(nlh->nlmsg_flags&NLM_F_REPLACE))
		return err;

	mark = xfrm_mark_get(attrs, &m);

	x = xfrm_state_lookup(net, mark, &p->sa_id.daddr, p->sa_id.spi, p->sa_id.proto, p->sa_id.family);
	if (x == NULL)
		return -ESRCH;

	if (x->km.state != XFRM_STATE_VALID)
		goto out;

	err = xfrm_replay_verify_len(x->replay_esn, re);
	if (err)
		goto out;

	spin_lock_bh(&x->lock);
	xfrm_update_ae_params(x, attrs, 1);
	spin_unlock_bh(&x->lock);

	c.event = nlh->nlmsg_type;
	c.seq = nlh->nlmsg_seq;
	c.portid = nlh->nlmsg_pid;
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
	struct net *net = sock_net(skb->sk);
	struct km_event c;
	u8 type = XFRM_POLICY_TYPE_MAIN;
	int err;

	err = copy_from_user_policy_type(&type, attrs);
	if (err)
		return err;

	err = xfrm_policy_flush(net, type, true);
	if (err) {
		if (err == -ESRCH) /* empty table */
			return 0;
		return err;
	}

	c.data.type = type;
	c.event = nlh->nlmsg_type;
	c.seq = nlh->nlmsg_seq;
	c.portid = nlh->nlmsg_pid;
	c.net = net;
	km_policy_notify(NULL, 0, &c);
	return 0;
}

static int xfrm_add_pol_expire(struct sk_buff *skb, struct nlmsghdr *nlh,
		struct nlattr **attrs)
{
	struct net *net = sock_net(skb->sk);
	struct xfrm_policy *xp;
	struct xfrm_user_polexpire *up = nlmsg_data(nlh);
	struct xfrm_userpolicy_info *p = &up->pol;
	u8 type = XFRM_POLICY_TYPE_MAIN;
	int err = -ENOENT;
	struct xfrm_mark m;
	u32 mark = xfrm_mark_get(attrs, &m);

	err = copy_from_user_policy_type(&type, attrs);
	if (err)
		return err;

	err = verify_policy_dir(p->dir);
	if (err)
		return err;

	if (p->index)
		xp = xfrm_policy_byid(net, mark, type, p->dir, p->index, 0, &err);
	else {
		struct nlattr *rt = attrs[XFRMA_SEC_CTX];
		struct xfrm_sec_ctx *ctx;

		err = verify_sec_ctx_len(attrs);
		if (err)
			return err;

		ctx = NULL;
		if (rt) {
			struct xfrm_user_sec_ctx *uctx = nla_data(rt);

			err = security_xfrm_policy_alloc(&ctx, uctx, GFP_KERNEL);
			if (err)
				return err;
		}
		xp = xfrm_policy_bysel_ctx(net, mark, type, p->dir,
					   &p->sel, ctx, 0, &err);
		security_xfrm_policy_free(ctx);
	}
	if (xp == NULL)
		return -ENOENT;

	if (unlikely(xp->walk.dead))
		goto out;

	err = 0;
	if (up->hard) {
		xfrm_policy_delete(xp, p->dir);
		xfrm_audit_policy_delete(xp, 1, true);
	}
	km_policy_expired(xp, p->dir, up->hard, nlh->nlmsg_pid);

out:
	xfrm_pol_put(xp);
	return err;
}

static int xfrm_add_sa_expire(struct sk_buff *skb, struct nlmsghdr *nlh,
		struct nlattr **attrs)
{
	struct net *net = sock_net(skb->sk);
	struct xfrm_state *x;
	int err;
	struct xfrm_user_expire *ue = nlmsg_data(nlh);
	struct xfrm_usersa_info *p = &ue->state;
	struct xfrm_mark m;
	u32 mark = xfrm_mark_get(attrs, &m);

	x = xfrm_state_lookup(net, mark, &p->id.daddr, p->id.spi, p->id.proto, p->family);

	err = -ENOENT;
	if (x == NULL)
		return err;

	spin_lock_bh(&x->lock);
	err = -EINVAL;
	if (x->km.state != XFRM_STATE_VALID)
		goto out;
	km_state_expired(x, ue->hard, nlh->nlmsg_pid);

	if (ue->hard) {
		__xfrm_state_delete(x);
		xfrm_audit_state_delete(x, 1, true);
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
	struct net *net = sock_net(skb->sk);
	struct xfrm_policy *xp;
	struct xfrm_user_tmpl *ut;
	int i;
	struct nlattr *rt = attrs[XFRMA_TMPL];
	struct xfrm_mark mark;

	struct xfrm_user_acquire *ua = nlmsg_data(nlh);
	struct xfrm_state *x = xfrm_state_alloc(net);
	int err = -ENOMEM;

	if (!x)
		goto nomem;

	xfrm_mark_get(attrs, &mark);

	err = verify_newpolicy_info(&ua->policy);
	if (err)
		goto free_state;

	/*   build an XP */
	xp = xfrm_policy_construct(net, &ua->policy, attrs, &err);
	if (!xp)
		goto free_state;

	memcpy(&x->id, &ua->id, sizeof(ua->id));
	memcpy(&x->props.saddr, &ua->saddr, sizeof(ua->saddr));
	memcpy(&x->sel, &ua->sel, sizeof(ua->sel));
	xp->mark.m = x->mark.m = mark.m;
	xp->mark.v = x->mark.v = mark.v;
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

free_state:
	kfree(x);
nomem:
	return err;
}

#ifdef CONFIG_XFRM_MIGRATE
static int copy_from_user_migrate(struct xfrm_migrate *ma,
				  struct xfrm_kmaddress *k,
				  struct nlattr **attrs, int *num)
{
	struct nlattr *rt = attrs[XFRMA_MIGRATE];
	struct xfrm_user_migrate *um;
	int i, num_migrate;

	if (k != NULL) {
		struct xfrm_user_kmaddress *uk;

		uk = nla_data(attrs[XFRMA_KMADDRESS]);
		memcpy(&k->local, &uk->local, sizeof(k->local));
		memcpy(&k->remote, &uk->remote, sizeof(k->remote));
		k->family = uk->family;
		k->reserved = uk->reserved;
	}

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
	struct xfrm_kmaddress km, *kmp;
	u8 type;
	int err;
	int n = 0;
	struct net *net = sock_net(skb->sk);
	struct xfrm_encap_tmpl  *encap = NULL;

	if (attrs[XFRMA_MIGRATE] == NULL)
		return -EINVAL;

	kmp = attrs[XFRMA_KMADDRESS] ? &km : NULL;

	err = copy_from_user_policy_type(&type, attrs);
	if (err)
		return err;

	err = copy_from_user_migrate((struct xfrm_migrate *)m, kmp, attrs, &n);
	if (err)
		return err;

	if (!n)
		return 0;

	if (attrs[XFRMA_ENCAP]) {
		encap = kmemdup(nla_data(attrs[XFRMA_ENCAP]),
				sizeof(*encap), GFP_KERNEL);
		if (!encap)
			return 0;
	}

	err = xfrm_migrate(&pi->sel, pi->dir, type, m, n, kmp, net, encap);

	kfree(encap);

	return err;
}
#else
static int xfrm_do_migrate(struct sk_buff *skb, struct nlmsghdr *nlh,
			   struct nlattr **attrs)
{
	return -ENOPROTOOPT;
}
#endif

#ifdef CONFIG_XFRM_MIGRATE
static int copy_to_user_migrate(const struct xfrm_migrate *m, struct sk_buff *skb)
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

static int copy_to_user_kmaddress(const struct xfrm_kmaddress *k, struct sk_buff *skb)
{
	struct xfrm_user_kmaddress uk;

	memset(&uk, 0, sizeof(uk));
	uk.family = k->family;
	uk.reserved = k->reserved;
	memcpy(&uk.local, &k->local, sizeof(uk.local));
	memcpy(&uk.remote, &k->remote, sizeof(uk.remote));

	return nla_put(skb, XFRMA_KMADDRESS, sizeof(uk), &uk);
}

static inline size_t xfrm_migrate_msgsize(int num_migrate, int with_kma,
					  int with_encp)
{
	return NLMSG_ALIGN(sizeof(struct xfrm_userpolicy_id))
	      + (with_kma ? nla_total_size(sizeof(struct xfrm_kmaddress)) : 0)
	      + (with_encp ? nla_total_size(sizeof(struct xfrm_encap_tmpl)) : 0)
	      + nla_total_size(sizeof(struct xfrm_user_migrate) * num_migrate)
	      + userpolicy_type_attrsize();
}

static int build_migrate(struct sk_buff *skb, const struct xfrm_migrate *m,
			 int num_migrate, const struct xfrm_kmaddress *k,
			 const struct xfrm_selector *sel,
			 const struct xfrm_encap_tmpl *encap, u8 dir, u8 type)
{
	const struct xfrm_migrate *mp;
	struct xfrm_userpolicy_id *pol_id;
	struct nlmsghdr *nlh;
	int i, err;

	nlh = nlmsg_put(skb, 0, 0, XFRM_MSG_MIGRATE, sizeof(*pol_id), 0);
	if (nlh == NULL)
		return -EMSGSIZE;

	pol_id = nlmsg_data(nlh);
	/* copy data from selector, dir, and type to the pol_id */
	memset(pol_id, 0, sizeof(*pol_id));
	memcpy(&pol_id->sel, sel, sizeof(pol_id->sel));
	pol_id->dir = dir;

	if (k != NULL) {
		err = copy_to_user_kmaddress(k, skb);
		if (err)
			goto out_cancel;
	}
	if (encap) {
		err = nla_put(skb, XFRMA_ENCAP, sizeof(*encap), encap);
		if (err)
			goto out_cancel;
	}
	err = copy_to_user_policy_type(type, skb);
	if (err)
		goto out_cancel;
	for (i = 0, mp = m ; i < num_migrate; i++, mp++) {
		err = copy_to_user_migrate(mp, skb);
		if (err)
			goto out_cancel;
	}

	nlmsg_end(skb, nlh);
	return 0;

out_cancel:
	nlmsg_cancel(skb, nlh);
	return err;
}

static int xfrm_send_migrate(const struct xfrm_selector *sel, u8 dir, u8 type,
			     const struct xfrm_migrate *m, int num_migrate,
			     const struct xfrm_kmaddress *k,
			     const struct xfrm_encap_tmpl *encap)
{
	struct net *net = &init_net;
	struct sk_buff *skb;

	skb = nlmsg_new(xfrm_migrate_msgsize(num_migrate, !!k, !!encap),
			GFP_ATOMIC);
	if (skb == NULL)
		return -ENOMEM;

	/* build migrate */
	if (build_migrate(skb, m, num_migrate, k, sel, encap, dir, type) < 0)
		BUG();

	return xfrm_nlmsg_multicast(net, skb, 0, XFRMNLGRP_MIGRATE);
}
#else
static int xfrm_send_migrate(const struct xfrm_selector *sel, u8 dir, u8 type,
			     const struct xfrm_migrate *m, int num_migrate,
			     const struct xfrm_kmaddress *k,
			     const struct xfrm_encap_tmpl *encap)
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
	[XFRM_MSG_NEWSPDINFO  - XFRM_MSG_BASE] = sizeof(u32),
	[XFRM_MSG_GETSPDINFO  - XFRM_MSG_BASE] = sizeof(u32),
};

#undef XMSGSIZE

static const struct nla_policy xfrma_policy[XFRMA_MAX+1] = {
	[XFRMA_SA]		= { .len = sizeof(struct xfrm_usersa_info)},
	[XFRMA_POLICY]		= { .len = sizeof(struct xfrm_userpolicy_info)},
	[XFRMA_LASTUSED]	= { .type = NLA_U64},
	[XFRMA_ALG_AUTH_TRUNC]	= { .len = sizeof(struct xfrm_algo_auth)},
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
	[XFRMA_KMADDRESS]	= { .len = sizeof(struct xfrm_user_kmaddress) },
	[XFRMA_MARK]		= { .len = sizeof(struct xfrm_mark) },
	[XFRMA_TFCPAD]		= { .type = NLA_U32 },
	[XFRMA_REPLAY_ESN_VAL]	= { .len = sizeof(struct xfrm_replay_state_esn) },
	[XFRMA_SA_EXTRA_FLAGS]	= { .type = NLA_U32 },
	[XFRMA_PROTO]		= { .type = NLA_U8 },
	[XFRMA_ADDRESS_FILTER]	= { .len = sizeof(struct xfrm_address_filter) },
	[XFRMA_OFFLOAD_DEV]	= { .len = sizeof(struct xfrm_user_offload) },
	[XFRMA_OUTPUT_MARK]	= { .len = NLA_U32 },
};

static const struct nla_policy xfrma_spd_policy[XFRMA_SPD_MAX+1] = {
	[XFRMA_SPD_IPV4_HTHRESH] = { .len = sizeof(struct xfrmu_spdhthresh) },
	[XFRMA_SPD_IPV6_HTHRESH] = { .len = sizeof(struct xfrmu_spdhthresh) },
};

static const struct xfrm_link {
	int (*doit)(struct sk_buff *, struct nlmsghdr *, struct nlattr **);
	int (*dump)(struct sk_buff *, struct netlink_callback *);
	int (*done)(struct netlink_callback *);
	const struct nla_policy *nla_pol;
	int nla_max;
} xfrm_dispatch[XFRM_NR_MSGTYPES] = {
	[XFRM_MSG_NEWSA       - XFRM_MSG_BASE] = { .doit = xfrm_add_sa        },
	[XFRM_MSG_DELSA       - XFRM_MSG_BASE] = { .doit = xfrm_del_sa        },
	[XFRM_MSG_GETSA       - XFRM_MSG_BASE] = { .doit = xfrm_get_sa,
						   .dump = xfrm_dump_sa,
						   .done = xfrm_dump_sa_done  },
	[XFRM_MSG_NEWPOLICY   - XFRM_MSG_BASE] = { .doit = xfrm_add_policy    },
	[XFRM_MSG_DELPOLICY   - XFRM_MSG_BASE] = { .doit = xfrm_get_policy    },
	[XFRM_MSG_GETPOLICY   - XFRM_MSG_BASE] = { .doit = xfrm_get_policy,
						   .dump = xfrm_dump_policy,
						   .done = xfrm_dump_policy_done },
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
	[XFRM_MSG_NEWSPDINFO  - XFRM_MSG_BASE] = { .doit = xfrm_set_spdinfo,
						   .nla_pol = xfrma_spd_policy,
						   .nla_max = XFRMA_SPD_MAX },
	[XFRM_MSG_GETSPDINFO  - XFRM_MSG_BASE] = { .doit = xfrm_get_spdinfo   },
};

static int xfrm_user_rcv_msg(struct sk_buff *skb, struct nlmsghdr *nlh,
			     struct netlink_ext_ack *extack)
{
	struct net *net = sock_net(skb->sk);
	struct nlattr *attrs[XFRMA_MAX+1];
	const struct xfrm_link *link;
	int type, err;

#ifdef CONFIG_COMPAT
	if (in_compat_syscall())
		return -EOPNOTSUPP;
#endif

	type = nlh->nlmsg_type;
	if (type > XFRM_MSG_MAX)
		return -EINVAL;

	type -= XFRM_MSG_BASE;
	link = &xfrm_dispatch[type];

	/* All operations require privileges, even GET */
	if (!netlink_net_capable(skb, CAP_NET_ADMIN))
		return -EPERM;

	if ((type == (XFRM_MSG_GETSA - XFRM_MSG_BASE) ||
	     type == (XFRM_MSG_GETPOLICY - XFRM_MSG_BASE)) &&
	    (nlh->nlmsg_flags & NLM_F_DUMP)) {
		if (link->dump == NULL)
			return -EINVAL;

		{
			struct netlink_dump_control c = {
				.dump = link->dump,
				.done = link->done,
			};
			return netlink_dump_start(net->xfrm.nlsk, skb, nlh, &c);
		}
	}

	err = nlmsg_parse(nlh, xfrm_msg_min[type], attrs,
			  link->nla_max ? : XFRMA_MAX,
			  link->nla_pol ? : xfrma_policy, extack);
	if (err < 0)
		return err;

	if (link->doit == NULL)
		return -EINVAL;

	return link->doit(skb, nlh, attrs);
}

static void xfrm_netlink_rcv(struct sk_buff *skb)
{
	struct net *net = sock_net(skb->sk);

	mutex_lock(&net->xfrm.xfrm_cfg_mutex);
	netlink_rcv_skb(skb, &xfrm_user_rcv_msg);
	mutex_unlock(&net->xfrm.xfrm_cfg_mutex);
}

static inline size_t xfrm_expire_msgsize(void)
{
	return NLMSG_ALIGN(sizeof(struct xfrm_user_expire))
	       + nla_total_size(sizeof(struct xfrm_mark));
}

static int build_expire(struct sk_buff *skb, struct xfrm_state *x, const struct km_event *c)
{
	struct xfrm_user_expire *ue;
	struct nlmsghdr *nlh;
	int err;

	nlh = nlmsg_put(skb, c->portid, 0, XFRM_MSG_EXPIRE, sizeof(*ue), 0);
	if (nlh == NULL)
		return -EMSGSIZE;

	ue = nlmsg_data(nlh);
	copy_to_user_state(x, &ue->state);
	ue->hard = (c->data.hard != 0) ? 1 : 0;
	/* clear the padding bytes */
	memset(&ue->hard + 1, 0, sizeof(*ue) - offsetofend(typeof(*ue), hard));

	err = xfrm_mark_put(skb, &x->mark);
	if (err)
		return err;

	nlmsg_end(skb, nlh);
	return 0;
}

static int xfrm_exp_state_notify(struct xfrm_state *x, const struct km_event *c)
{
	struct net *net = xs_net(x);
	struct sk_buff *skb;

	skb = nlmsg_new(xfrm_expire_msgsize(), GFP_ATOMIC);
	if (skb == NULL)
		return -ENOMEM;

	if (build_expire(skb, x, c) < 0) {
		kfree_skb(skb);
		return -EMSGSIZE;
	}

	return xfrm_nlmsg_multicast(net, skb, 0, XFRMNLGRP_EXPIRE);
}

static int xfrm_aevent_state_notify(struct xfrm_state *x, const struct km_event *c)
{
	struct net *net = xs_net(x);
	struct sk_buff *skb;

	skb = nlmsg_new(xfrm_aevent_msgsize(x), GFP_ATOMIC);
	if (skb == NULL)
		return -ENOMEM;

	if (build_aevent(skb, x, c) < 0)
		BUG();

	return xfrm_nlmsg_multicast(net, skb, 0, XFRMNLGRP_AEVENTS);
}

static int xfrm_notify_sa_flush(const struct km_event *c)
{
	struct net *net = c->net;
	struct xfrm_usersa_flush *p;
	struct nlmsghdr *nlh;
	struct sk_buff *skb;
	int len = NLMSG_ALIGN(sizeof(struct xfrm_usersa_flush));

	skb = nlmsg_new(len, GFP_ATOMIC);
	if (skb == NULL)
		return -ENOMEM;

	nlh = nlmsg_put(skb, c->portid, c->seq, XFRM_MSG_FLUSHSA, sizeof(*p), 0);
	if (nlh == NULL) {
		kfree_skb(skb);
		return -EMSGSIZE;
	}

	p = nlmsg_data(nlh);
	p->proto = c->data.proto;

	nlmsg_end(skb, nlh);

	return xfrm_nlmsg_multicast(net, skb, 0, XFRMNLGRP_SA);
}

static inline size_t xfrm_sa_len(struct xfrm_state *x)
{
	size_t l = 0;
	if (x->aead)
		l += nla_total_size(aead_len(x->aead));
	if (x->aalg) {
		l += nla_total_size(sizeof(struct xfrm_algo) +
				    (x->aalg->alg_key_len + 7) / 8);
		l += nla_total_size(xfrm_alg_auth_len(x->aalg));
	}
	if (x->ealg)
		l += nla_total_size(xfrm_alg_len(x->ealg));
	if (x->calg)
		l += nla_total_size(sizeof(*x->calg));
	if (x->encap)
		l += nla_total_size(sizeof(*x->encap));
	if (x->tfcpad)
		l += nla_total_size(sizeof(x->tfcpad));
	if (x->replay_esn)
		l += nla_total_size(xfrm_replay_state_esn_len(x->replay_esn));
	else
		l += nla_total_size(sizeof(struct xfrm_replay_state));
	if (x->security)
		l += nla_total_size(sizeof(struct xfrm_user_sec_ctx) +
				    x->security->ctx_len);
	if (x->coaddr)
		l += nla_total_size(sizeof(*x->coaddr));
	if (x->props.extra_flags)
		l += nla_total_size(sizeof(x->props.extra_flags));
	if (x->xso.dev)
		 l += nla_total_size(sizeof(x->xso));
	if (x->props.output_mark)
		l += nla_total_size(sizeof(x->props.output_mark));

	/* Must count x->lastused as it may become non-zero behind our back. */
	l += nla_total_size_64bit(sizeof(u64));

	return l;
}

static int xfrm_notify_sa(struct xfrm_state *x, const struct km_event *c)
{
	struct net *net = xs_net(x);
	struct xfrm_usersa_info *p;
	struct xfrm_usersa_id *id;
	struct nlmsghdr *nlh;
	struct sk_buff *skb;
	int len = xfrm_sa_len(x);
	int headlen, err;

	headlen = sizeof(*p);
	if (c->event == XFRM_MSG_DELSA) {
		len += nla_total_size(headlen);
		headlen = sizeof(*id);
		len += nla_total_size(sizeof(struct xfrm_mark));
	}
	len += NLMSG_ALIGN(headlen);

	skb = nlmsg_new(len, GFP_ATOMIC);
	if (skb == NULL)
		return -ENOMEM;

	nlh = nlmsg_put(skb, c->portid, c->seq, c->event, headlen, 0);
	err = -EMSGSIZE;
	if (nlh == NULL)
		goto out_free_skb;

	p = nlmsg_data(nlh);
	if (c->event == XFRM_MSG_DELSA) {
		struct nlattr *attr;

		id = nlmsg_data(nlh);
		memset(id, 0, sizeof(*id));
		memcpy(&id->daddr, &x->id.daddr, sizeof(id->daddr));
		id->spi = x->id.spi;
		id->family = x->props.family;
		id->proto = x->id.proto;

		attr = nla_reserve(skb, XFRMA_SA, sizeof(*p));
		err = -EMSGSIZE;
		if (attr == NULL)
			goto out_free_skb;

		p = nla_data(attr);
	}
	err = copy_to_user_state_extra(x, p, skb);
	if (err)
		goto out_free_skb;

	nlmsg_end(skb, nlh);

	return xfrm_nlmsg_multicast(net, skb, 0, XFRMNLGRP_SA);

out_free_skb:
	kfree_skb(skb);
	return err;
}

static int xfrm_send_state_notify(struct xfrm_state *x, const struct km_event *c)
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
		printk(KERN_NOTICE "xfrm_user: Unknown SA event %d\n",
		       c->event);
		break;
	}

	return 0;

}

static inline size_t xfrm_acquire_msgsize(struct xfrm_state *x,
					  struct xfrm_policy *xp)
{
	return NLMSG_ALIGN(sizeof(struct xfrm_user_acquire))
	       + nla_total_size(sizeof(struct xfrm_user_tmpl) * xp->xfrm_nr)
	       + nla_total_size(sizeof(struct xfrm_mark))
	       + nla_total_size(xfrm_user_sec_ctx_size(x->security))
	       + userpolicy_type_attrsize();
}

static int build_acquire(struct sk_buff *skb, struct xfrm_state *x,
			 struct xfrm_tmpl *xt, struct xfrm_policy *xp)
{
	__u32 seq = xfrm_get_acqseq();
	struct xfrm_user_acquire *ua;
	struct nlmsghdr *nlh;
	int err;

	nlh = nlmsg_put(skb, 0, 0, XFRM_MSG_ACQUIRE, sizeof(*ua), 0);
	if (nlh == NULL)
		return -EMSGSIZE;

	ua = nlmsg_data(nlh);
	memcpy(&ua->id, &x->id, sizeof(ua->id));
	memcpy(&ua->saddr, &x->props.saddr, sizeof(ua->saddr));
	memcpy(&ua->sel, &x->sel, sizeof(ua->sel));
	copy_to_user_policy(xp, &ua->policy, XFRM_POLICY_OUT);
	ua->aalgos = xt->aalgos;
	ua->ealgos = xt->ealgos;
	ua->calgos = xt->calgos;
	ua->seq = x->km.seq = seq;

	err = copy_to_user_tmpl(xp, skb);
	if (!err)
		err = copy_to_user_state_sec_ctx(x, skb);
	if (!err)
		err = copy_to_user_policy_type(xp->type, skb);
	if (!err)
		err = xfrm_mark_put(skb, &xp->mark);
	if (err) {
		nlmsg_cancel(skb, nlh);
		return err;
	}

	nlmsg_end(skb, nlh);
	return 0;
}

static int xfrm_send_acquire(struct xfrm_state *x, struct xfrm_tmpl *xt,
			     struct xfrm_policy *xp)
{
	struct net *net = xs_net(x);
	struct sk_buff *skb;

	skb = nlmsg_new(xfrm_acquire_msgsize(x, xp), GFP_ATOMIC);
	if (skb == NULL)
		return -ENOMEM;

	if (build_acquire(skb, x, xt, xp) < 0)
		BUG();

	return xfrm_nlmsg_multicast(net, skb, 0, XFRMNLGRP_ACQUIRE);
}

/* User gives us xfrm_user_policy_info followed by an array of 0
 * or more templates.
 */
static struct xfrm_policy *xfrm_compile_policy(struct sock *sk, int opt,
					       u8 *data, int len, int *dir)
{
	struct net *net = sock_net(sk);
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
#if IS_ENABLED(CONFIG_IPV6)
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

	xp = xfrm_policy_alloc(net, GFP_ATOMIC);
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
	       + nla_total_size(sizeof(struct xfrm_mark))
	       + userpolicy_type_attrsize();
}

static int build_polexpire(struct sk_buff *skb, struct xfrm_policy *xp,
			   int dir, const struct km_event *c)
{
	struct xfrm_user_polexpire *upe;
	int hard = c->data.hard;
	struct nlmsghdr *nlh;
	int err;

	nlh = nlmsg_put(skb, c->portid, 0, XFRM_MSG_POLEXPIRE, sizeof(*upe), 0);
	if (nlh == NULL)
		return -EMSGSIZE;

	upe = nlmsg_data(nlh);
	copy_to_user_policy(xp, &upe->pol, dir);
	err = copy_to_user_tmpl(xp, skb);
	if (!err)
		err = copy_to_user_sec_ctx(xp, skb);
	if (!err)
		err = copy_to_user_policy_type(xp->type, skb);
	if (!err)
		err = xfrm_mark_put(skb, &xp->mark);
	if (err) {
		nlmsg_cancel(skb, nlh);
		return err;
	}
	upe->hard = !!hard;

	nlmsg_end(skb, nlh);
	return 0;
}

static int xfrm_exp_policy_notify(struct xfrm_policy *xp, int dir, const struct km_event *c)
{
	struct net *net = xp_net(xp);
	struct sk_buff *skb;

	skb = nlmsg_new(xfrm_polexpire_msgsize(xp), GFP_ATOMIC);
	if (skb == NULL)
		return -ENOMEM;

	if (build_polexpire(skb, xp, dir, c) < 0)
		BUG();

	return xfrm_nlmsg_multicast(net, skb, 0, XFRMNLGRP_EXPIRE);
}

static int xfrm_notify_policy(struct xfrm_policy *xp, int dir, const struct km_event *c)
{
	int len = nla_total_size(sizeof(struct xfrm_user_tmpl) * xp->xfrm_nr);
	struct net *net = xp_net(xp);
	struct xfrm_userpolicy_info *p;
	struct xfrm_userpolicy_id *id;
	struct nlmsghdr *nlh;
	struct sk_buff *skb;
	int headlen, err;

	headlen = sizeof(*p);
	if (c->event == XFRM_MSG_DELPOLICY) {
		len += nla_total_size(headlen);
		headlen = sizeof(*id);
	}
	len += userpolicy_type_attrsize();
	len += nla_total_size(sizeof(struct xfrm_mark));
	len += NLMSG_ALIGN(headlen);

	skb = nlmsg_new(len, GFP_ATOMIC);
	if (skb == NULL)
		return -ENOMEM;

	nlh = nlmsg_put(skb, c->portid, c->seq, c->event, headlen, 0);
	err = -EMSGSIZE;
	if (nlh == NULL)
		goto out_free_skb;

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
		err = -EMSGSIZE;
		if (attr == NULL)
			goto out_free_skb;

		p = nla_data(attr);
	}

	copy_to_user_policy(xp, p, dir);
	err = copy_to_user_tmpl(xp, skb);
	if (!err)
		err = copy_to_user_policy_type(xp->type, skb);
	if (!err)
		err = xfrm_mark_put(skb, &xp->mark);
	if (err)
		goto out_free_skb;

	nlmsg_end(skb, nlh);

	return xfrm_nlmsg_multicast(net, skb, 0, XFRMNLGRP_POLICY);

out_free_skb:
	kfree_skb(skb);
	return err;
}

static int xfrm_notify_policy_flush(const struct km_event *c)
{
	struct net *net = c->net;
	struct nlmsghdr *nlh;
	struct sk_buff *skb;
	int err;

	skb = nlmsg_new(userpolicy_type_attrsize(), GFP_ATOMIC);
	if (skb == NULL)
		return -ENOMEM;

	nlh = nlmsg_put(skb, c->portid, c->seq, XFRM_MSG_FLUSHPOLICY, 0, 0);
	err = -EMSGSIZE;
	if (nlh == NULL)
		goto out_free_skb;
	err = copy_to_user_policy_type(c->data.type, skb);
	if (err)
		goto out_free_skb;

	nlmsg_end(skb, nlh);

	return xfrm_nlmsg_multicast(net, skb, 0, XFRMNLGRP_POLICY);

out_free_skb:
	kfree_skb(skb);
	return err;
}

static int xfrm_send_policy_notify(struct xfrm_policy *xp, int dir, const struct km_event *c)
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
		printk(KERN_NOTICE "xfrm_user: Unknown Policy event %d\n",
		       c->event);
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

	if (addr) {
		int err = nla_put(skb, XFRMA_COADDR, sizeof(*addr), addr);
		if (err) {
			nlmsg_cancel(skb, nlh);
			return err;
		}
	}
	nlmsg_end(skb, nlh);
	return 0;
}

static int xfrm_send_report(struct net *net, u8 proto,
			    struct xfrm_selector *sel, xfrm_address_t *addr)
{
	struct sk_buff *skb;

	skb = nlmsg_new(xfrm_report_msgsize(), GFP_ATOMIC);
	if (skb == NULL)
		return -ENOMEM;

	if (build_report(skb, proto, sel, addr) < 0)
		BUG();

	return xfrm_nlmsg_multicast(net, skb, 0, XFRMNLGRP_REPORT);
}

static inline size_t xfrm_mapping_msgsize(void)
{
	return NLMSG_ALIGN(sizeof(struct xfrm_user_mapping));
}

static int build_mapping(struct sk_buff *skb, struct xfrm_state *x,
			 xfrm_address_t *new_saddr, __be16 new_sport)
{
	struct xfrm_user_mapping *um;
	struct nlmsghdr *nlh;

	nlh = nlmsg_put(skb, 0, 0, XFRM_MSG_MAPPING, sizeof(*um), 0);
	if (nlh == NULL)
		return -EMSGSIZE;

	um = nlmsg_data(nlh);

	memcpy(&um->id.daddr, &x->id.daddr, sizeof(um->id.daddr));
	um->id.spi = x->id.spi;
	um->id.family = x->props.family;
	um->id.proto = x->id.proto;
	memcpy(&um->new_saddr, new_saddr, sizeof(um->new_saddr));
	memcpy(&um->old_saddr, &x->props.saddr, sizeof(um->old_saddr));
	um->new_sport = new_sport;
	um->old_sport = x->encap->encap_sport;
	um->reqid = x->props.reqid;

	nlmsg_end(skb, nlh);
	return 0;
}

static int xfrm_send_mapping(struct xfrm_state *x, xfrm_address_t *ipaddr,
			     __be16 sport)
{
	struct net *net = xs_net(x);
	struct sk_buff *skb;

	if (x->id.proto != IPPROTO_ESP)
		return -EINVAL;

	if (!x->encap)
		return -EINVAL;

	skb = nlmsg_new(xfrm_mapping_msgsize(), GFP_ATOMIC);
	if (skb == NULL)
		return -ENOMEM;

	if (build_mapping(skb, x, ipaddr, sport) < 0)
		BUG();

	return xfrm_nlmsg_multicast(net, skb, 0, XFRMNLGRP_MAPPING);
}

static bool xfrm_is_alive(const struct km_event *c)
{
	return (bool)xfrm_acquire_is_on(c->net);
}

static struct xfrm_mgr netlink_mgr = {
	.notify		= xfrm_send_state_notify,
	.acquire	= xfrm_send_acquire,
	.compile_policy	= xfrm_compile_policy,
	.notify_policy	= xfrm_send_policy_notify,
	.report		= xfrm_send_report,
	.migrate	= xfrm_send_migrate,
	.new_mapping	= xfrm_send_mapping,
	.is_alive	= xfrm_is_alive,
};

static int __net_init xfrm_user_net_init(struct net *net)
{
	struct sock *nlsk;
	struct netlink_kernel_cfg cfg = {
		.groups	= XFRMNLGRP_MAX,
		.input	= xfrm_netlink_rcv,
	};

	nlsk = netlink_kernel_create(net, NETLINK_XFRM, &cfg);
	if (nlsk == NULL)
		return -ENOMEM;
	net->xfrm.nlsk_stash = nlsk; /* Don't set to NULL */
	rcu_assign_pointer(net->xfrm.nlsk, nlsk);
	return 0;
}

static void __net_exit xfrm_user_net_exit(struct list_head *net_exit_list)
{
	struct net *net;
	list_for_each_entry(net, net_exit_list, exit_list)
		RCU_INIT_POINTER(net->xfrm.nlsk, NULL);
	synchronize_net();
	list_for_each_entry(net, net_exit_list, exit_list)
		netlink_kernel_release(net->xfrm.nlsk_stash);
}

static struct pernet_operations xfrm_user_net_ops = {
	.init	    = xfrm_user_net_init,
	.exit_batch = xfrm_user_net_exit,
};

static int __init xfrm_user_init(void)
{
	int rv;

	printk(KERN_INFO "Initializing XFRM netlink socket\n");

	rv = register_pernet_subsys(&xfrm_user_net_ops);
	if (rv < 0)
		return rv;
	rv = xfrm_register_km(&netlink_mgr);
	if (rv < 0)
		unregister_pernet_subsys(&xfrm_user_net_ops);
	return rv;
}

static void __exit xfrm_user_exit(void)
{
	xfrm_unregister_km(&netlink_mgr);
	unregister_pernet_subsys(&xfrm_user_net_ops);
}

module_init(xfrm_user_init);
module_exit(xfrm_user_exit);
MODULE_LICENSE("GPL");
MODULE_ALIAS_NET_PF_PROTO(PF_NETLINK, NETLINK_XFRM);

