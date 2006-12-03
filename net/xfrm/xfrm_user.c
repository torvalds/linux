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
#include <linux/rtnetlink.h>
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

static int verify_one_alg(struct rtattr **xfrma, enum xfrm_attr_type_t type)
{
	struct rtattr *rt = xfrma[type - 1];
	struct xfrm_algo *algp;
	int len;

	if (!rt)
		return 0;

	len = (rt->rta_len - sizeof(*rt)) - sizeof(*algp);
	if (len < 0)
		return -EINVAL;

	algp = RTA_DATA(rt);

	len -= (algp->alg_key_len + 7U) / 8; 
	if (len < 0)
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
	};

	algp->alg_name[CRYPTO_MAX_ALG_NAME - 1] = '\0';
	return 0;
}

static int verify_encap_tmpl(struct rtattr **xfrma)
{
	struct rtattr *rt = xfrma[XFRMA_ENCAP - 1];
	struct xfrm_encap_tmpl *encap;

	if (!rt)
		return 0;

	if ((rt->rta_len - sizeof(*rt)) < sizeof(*encap))
		return -EINVAL;

	return 0;
}

static int verify_one_addr(struct rtattr **xfrma, enum xfrm_attr_type_t type,
			   xfrm_address_t **addrp)
{
	struct rtattr *rt = xfrma[type - 1];

	if (!rt)
		return 0;

	if ((rt->rta_len - sizeof(*rt)) < sizeof(**addrp))
		return -EINVAL;

	if (addrp)
		*addrp = RTA_DATA(rt);

	return 0;
}

static inline int verify_sec_ctx_len(struct rtattr **xfrma)
{
	struct rtattr *rt = xfrma[XFRMA_SEC_CTX - 1];
	struct xfrm_user_sec_ctx *uctx;
	int len = 0;

	if (!rt)
		return 0;

	if (rt->rta_len < sizeof(*uctx))
		return -EINVAL;

	uctx = RTA_DATA(rt);

	len += sizeof(struct xfrm_user_sec_ctx);
	len += uctx->ctx_len;

	if (uctx->len != len)
		return -EINVAL;

	return 0;
}


static int verify_newsa_info(struct xfrm_usersa_info *p,
			     struct rtattr **xfrma)
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
	};

	err = -EINVAL;
	switch (p->id.proto) {
	case IPPROTO_AH:
		if (!xfrma[XFRMA_ALG_AUTH-1]	||
		    xfrma[XFRMA_ALG_CRYPT-1]	||
		    xfrma[XFRMA_ALG_COMP-1])
			goto out;
		break;

	case IPPROTO_ESP:
		if ((!xfrma[XFRMA_ALG_AUTH-1] &&
		     !xfrma[XFRMA_ALG_CRYPT-1])	||
		    xfrma[XFRMA_ALG_COMP-1])
			goto out;
		break;

	case IPPROTO_COMP:
		if (!xfrma[XFRMA_ALG_COMP-1]	||
		    xfrma[XFRMA_ALG_AUTH-1]	||
		    xfrma[XFRMA_ALG_CRYPT-1])
			goto out;
		break;

#if defined(CONFIG_IPV6) || defined(CONFIG_IPV6_MODULE)
	case IPPROTO_DSTOPTS:
	case IPPROTO_ROUTING:
		if (xfrma[XFRMA_ALG_COMP-1]	||
		    xfrma[XFRMA_ALG_AUTH-1]	||
		    xfrma[XFRMA_ALG_CRYPT-1]	||
		    xfrma[XFRMA_ENCAP-1]	||
		    xfrma[XFRMA_SEC_CTX-1]	||
		    !xfrma[XFRMA_COADDR-1])
			goto out;
		break;
#endif

	default:
		goto out;
	};

	if ((err = verify_one_alg(xfrma, XFRMA_ALG_AUTH)))
		goto out;
	if ((err = verify_one_alg(xfrma, XFRMA_ALG_CRYPT)))
		goto out;
	if ((err = verify_one_alg(xfrma, XFRMA_ALG_COMP)))
		goto out;
	if ((err = verify_encap_tmpl(xfrma)))
		goto out;
	if ((err = verify_sec_ctx_len(xfrma)))
		goto out;
	if ((err = verify_one_addr(xfrma, XFRMA_COADDR, NULL)))
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
	};

	err = 0;

out:
	return err;
}

static int attach_one_algo(struct xfrm_algo **algpp, u8 *props,
			   struct xfrm_algo_desc *(*get_byname)(char *, int),
			   struct rtattr *u_arg)
{
	struct rtattr *rta = u_arg;
	struct xfrm_algo *p, *ualg;
	struct xfrm_algo_desc *algo;
	int len;

	if (!rta)
		return 0;

	ualg = RTA_DATA(rta);

	algo = get_byname(ualg->alg_name, 1);
	if (!algo)
		return -ENOSYS;
	*props = algo->desc.sadb_alg_id;

	len = sizeof(*ualg) + (ualg->alg_key_len + 7U) / 8;
	p = kmemdup(ualg, len, GFP_KERNEL);
	if (!p)
		return -ENOMEM;

	strcpy(p->alg_name, algo->name);
	*algpp = p;
	return 0;
}

static int attach_encap_tmpl(struct xfrm_encap_tmpl **encapp, struct rtattr *u_arg)
{
	struct rtattr *rta = u_arg;
	struct xfrm_encap_tmpl *p, *uencap;

	if (!rta)
		return 0;

	uencap = RTA_DATA(rta);
	p = kmemdup(uencap, sizeof(*p), GFP_KERNEL);
	if (!p)
		return -ENOMEM;

	*encapp = p;
	return 0;
}


static inline int xfrm_user_sec_ctx_size(struct xfrm_policy *xp)
{
	struct xfrm_sec_ctx *xfrm_ctx = xp->security;
	int len = 0;

	if (xfrm_ctx) {
		len += sizeof(struct xfrm_user_sec_ctx);
		len += xfrm_ctx->ctx_len;
	}
	return len;
}

static int attach_sec_ctx(struct xfrm_state *x, struct rtattr *u_arg)
{
	struct xfrm_user_sec_ctx *uctx;

	if (!u_arg)
		return 0;

	uctx = RTA_DATA(u_arg);
	return security_xfrm_state_alloc(x, uctx);
}

static int attach_one_addr(xfrm_address_t **addrpp, struct rtattr *u_arg)
{
	struct rtattr *rta = u_arg;
	xfrm_address_t *p, *uaddrp;

	if (!rta)
		return 0;

	uaddrp = RTA_DATA(rta);
	p = kmemdup(uaddrp, sizeof(*p), GFP_KERNEL);
	if (!p)
		return -ENOMEM;

	*addrpp = p;
	return 0;
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
}

/*
 * someday when pfkey also has support, we could have the code
 * somehow made shareable and move it to xfrm_state.c - JHS
 *
*/
static int xfrm_update_ae_params(struct xfrm_state *x, struct rtattr **xfrma)
{
	int err = - EINVAL;
	struct rtattr *rp = xfrma[XFRMA_REPLAY_VAL-1];
	struct rtattr *lt = xfrma[XFRMA_LTIME_VAL-1];
	struct rtattr *et = xfrma[XFRMA_ETIMER_THRESH-1];
	struct rtattr *rt = xfrma[XFRMA_REPLAY_THRESH-1];

	if (rp) {
		struct xfrm_replay_state *replay;
		if (RTA_PAYLOAD(rp) < sizeof(*replay))
			goto error;
		replay = RTA_DATA(rp);
		memcpy(&x->replay, replay, sizeof(*replay));
		memcpy(&x->preplay, replay, sizeof(*replay));
	}

	if (lt) {
		struct xfrm_lifetime_cur *ltime;
		if (RTA_PAYLOAD(lt) < sizeof(*ltime))
			goto error;
		ltime = RTA_DATA(lt);
		x->curlft.bytes = ltime->bytes;
		x->curlft.packets = ltime->packets;
		x->curlft.add_time = ltime->add_time;
		x->curlft.use_time = ltime->use_time;
	}

	if (et) {
		if (RTA_PAYLOAD(et) < sizeof(u32))
			goto error;
		x->replay_maxage = *(u32*)RTA_DATA(et);
	}

	if (rt) {
		if (RTA_PAYLOAD(rt) < sizeof(u32))
			goto error;
		x->replay_maxdiff = *(u32*)RTA_DATA(rt);
	}

	return 0;
error:
	return err;
}

static struct xfrm_state *xfrm_state_construct(struct xfrm_usersa_info *p,
					       struct rtattr **xfrma,
					       int *errp)
{
	struct xfrm_state *x = xfrm_state_alloc();
	int err = -ENOMEM;

	if (!x)
		goto error_no_put;

	copy_from_user_state(x, p);

	if ((err = attach_one_algo(&x->aalg, &x->props.aalgo,
				   xfrm_aalg_get_byname,
				   xfrma[XFRMA_ALG_AUTH-1])))
		goto error;
	if ((err = attach_one_algo(&x->ealg, &x->props.ealgo,
				   xfrm_ealg_get_byname,
				   xfrma[XFRMA_ALG_CRYPT-1])))
		goto error;
	if ((err = attach_one_algo(&x->calg, &x->props.calgo,
				   xfrm_calg_get_byname,
				   xfrma[XFRMA_ALG_COMP-1])))
		goto error;
	if ((err = attach_encap_tmpl(&x->encap, xfrma[XFRMA_ENCAP-1])))
		goto error;
	if ((err = attach_one_addr(&x->coaddr, xfrma[XFRMA_COADDR-1])))
		goto error;
	err = xfrm_init_state(x);
	if (err)
		goto error;

	if ((err = attach_sec_ctx(x, xfrma[XFRMA_SEC_CTX-1])))
		goto error;

	x->km.seq = p->seq;
	x->replay_maxdiff = sysctl_xfrm_aevent_rseqth;
	/* sysctl_xfrm_aevent_etime is in 100ms units */
	x->replay_maxage = (sysctl_xfrm_aevent_etime*HZ)/XFRM_AE_ETH_M;
	x->preplay.bitmap = 0;
	x->preplay.seq = x->replay.seq+x->replay_maxdiff;
	x->preplay.oseq = x->replay.oseq +x->replay_maxdiff;

	/* override default values from above */

	err = xfrm_update_ae_params(x, (struct rtattr **)xfrma);
	if (err	< 0)
		goto error;

	return x;

error:
	x->km.state = XFRM_STATE_DEAD;
	xfrm_state_put(x);
error_no_put:
	*errp = err;
	return NULL;
}

static int xfrm_add_sa(struct sk_buff *skb, struct nlmsghdr *nlh, void **xfrma)
{
	struct xfrm_usersa_info *p = NLMSG_DATA(nlh);
	struct xfrm_state *x;
	int err;
	struct km_event c;

	err = verify_newsa_info(p, (struct rtattr **)xfrma);
	if (err)
		return err;

	x = xfrm_state_construct(p, (struct rtattr **)xfrma, &err);
	if (!x)
		return err;

	xfrm_state_hold(x);
	if (nlh->nlmsg_type == XFRM_MSG_NEWSA)
		err = xfrm_state_add(x);
	else
		err = xfrm_state_update(x);

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
						 struct rtattr **xfrma,
						 int *errp)
{
	struct xfrm_state *x = NULL;
	int err;

	if (xfrm_id_proto_match(p->proto, IPSEC_PROTO_ANY)) {
		err = -ESRCH;
		x = xfrm_state_lookup(&p->daddr, p->spi, p->proto, p->family);
	} else {
		xfrm_address_t *saddr = NULL;

		err = verify_one_addr(xfrma, XFRMA_SRCADDR, &saddr);
		if (err)
			goto out;

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

static int xfrm_del_sa(struct sk_buff *skb, struct nlmsghdr *nlh, void **xfrma)
{
	struct xfrm_state *x;
	int err = -ESRCH;
	struct km_event c;
	struct xfrm_usersa_id *p = NLMSG_DATA(nlh);

	x = xfrm_user_state_lookup(p, (struct rtattr **)xfrma, &err);
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

static int dump_one_state(struct xfrm_state *x, int count, void *ptr)
{
	struct xfrm_dump_info *sp = ptr;
	struct sk_buff *in_skb = sp->in_skb;
	struct sk_buff *skb = sp->out_skb;
	struct xfrm_usersa_info *p;
	struct nlmsghdr *nlh;
	unsigned char *b = skb->tail;

	if (sp->this_idx < sp->start_idx)
		goto out;

	nlh = NLMSG_PUT(skb, NETLINK_CB(in_skb).pid,
			sp->nlmsg_seq,
			XFRM_MSG_NEWSA, sizeof(*p));
	nlh->nlmsg_flags = sp->nlmsg_flags;

	p = NLMSG_DATA(nlh);
	copy_to_user_state(x, p);

	if (x->aalg)
		RTA_PUT(skb, XFRMA_ALG_AUTH,
			sizeof(*(x->aalg))+(x->aalg->alg_key_len+7)/8, x->aalg);
	if (x->ealg)
		RTA_PUT(skb, XFRMA_ALG_CRYPT,
			sizeof(*(x->ealg))+(x->ealg->alg_key_len+7)/8, x->ealg);
	if (x->calg)
		RTA_PUT(skb, XFRMA_ALG_COMP, sizeof(*(x->calg)), x->calg);

	if (x->encap)
		RTA_PUT(skb, XFRMA_ENCAP, sizeof(*x->encap), x->encap);

	if (x->security) {
		int ctx_size = sizeof(struct xfrm_sec_ctx) +
				x->security->ctx_len;
		struct rtattr *rt = __RTA_PUT(skb, XFRMA_SEC_CTX, ctx_size);
		struct xfrm_user_sec_ctx *uctx = RTA_DATA(rt);

		uctx->exttype = XFRMA_SEC_CTX;
		uctx->len = ctx_size;
		uctx->ctx_doi = x->security->ctx_doi;
		uctx->ctx_alg = x->security->ctx_alg;
		uctx->ctx_len = x->security->ctx_len;
		memcpy(uctx + 1, x->security->ctx_str, x->security->ctx_len);
	}

	if (x->coaddr)
		RTA_PUT(skb, XFRMA_COADDR, sizeof(*x->coaddr), x->coaddr);

	if (x->lastused)
		RTA_PUT(skb, XFRMA_LASTUSED, sizeof(x->lastused), &x->lastused);

	nlh->nlmsg_len = skb->tail - b;
out:
	sp->this_idx++;
	return 0;

nlmsg_failure:
rtattr_failure:
	skb_trim(skb, b - skb->data);
	return -1;
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

	skb = alloc_skb(NLMSG_GOODSIZE, GFP_ATOMIC);
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

static int xfrm_get_sa(struct sk_buff *skb, struct nlmsghdr *nlh, void **xfrma)
{
	struct xfrm_usersa_id *p = NLMSG_DATA(nlh);
	struct xfrm_state *x;
	struct sk_buff *resp_skb;
	int err = -ESRCH;

	x = xfrm_user_state_lookup(p, (struct rtattr **)xfrma, &err);
	if (x == NULL)
		goto out_noput;

	resp_skb = xfrm_state_netlink(skb, x, nlh->nlmsg_seq);
	if (IS_ERR(resp_skb)) {
		err = PTR_ERR(resp_skb);
	} else {
		err = netlink_unicast(xfrm_nl, resp_skb,
				      NETLINK_CB(skb).pid, MSG_DONTWAIT);
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
	};

	if (p->min > p->max)
		return -EINVAL;

	return 0;
}

static int xfrm_alloc_userspi(struct sk_buff *skb, struct nlmsghdr *nlh, void **xfrma)
{
	struct xfrm_state *x;
	struct xfrm_userspi_info *p;
	struct sk_buff *resp_skb;
	xfrm_address_t *daddr;
	int family;
	int err;

	p = NLMSG_DATA(nlh);
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

	resp_skb = ERR_PTR(-ENOENT);

	spin_lock_bh(&x->lock);
	if (x->km.state != XFRM_STATE_DEAD) {
		xfrm_alloc_spi(x, htonl(p->min), htonl(p->max));
		if (x->id.spi)
			resp_skb = xfrm_state_netlink(skb, x, nlh->nlmsg_seq);
	}
	spin_unlock_bh(&x->lock);

	if (IS_ERR(resp_skb)) {
		err = PTR_ERR(resp_skb);
		goto out;
	}

	err = netlink_unicast(xfrm_nl, resp_skb,
			      NETLINK_CB(skb).pid, MSG_DONTWAIT);

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
	};

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
	};

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
	};

	switch (p->action) {
	case XFRM_POLICY_ALLOW:
	case XFRM_POLICY_BLOCK:
		break;

	default:
		return -EINVAL;
	};

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
	};

	return verify_policy_dir(p->dir);
}

static int copy_from_user_sec_ctx(struct xfrm_policy *pol, struct rtattr **xfrma)
{
	struct rtattr *rt = xfrma[XFRMA_SEC_CTX-1];
	struct xfrm_user_sec_ctx *uctx;

	if (!rt)
		return 0;

	uctx = RTA_DATA(rt);
	return security_xfrm_policy_alloc(pol, uctx);
}

static void copy_templates(struct xfrm_policy *xp, struct xfrm_user_tmpl *ut,
			   int nr)
{
	int i;

	xp->xfrm_nr = nr;
	xp->family = ut->family;
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

static int copy_from_user_tmpl(struct xfrm_policy *pol, struct rtattr **xfrma)
{
	struct rtattr *rt = xfrma[XFRMA_TMPL-1];
	struct xfrm_user_tmpl *utmpl;
	int nr;

	if (!rt) {
		pol->xfrm_nr = 0;
	} else {
		nr = (rt->rta_len - sizeof(*rt)) / sizeof(*utmpl);

		if (nr > XFRM_MAX_DEPTH)
			return -EINVAL;

		copy_templates(pol, RTA_DATA(rt), nr);
	}
	return 0;
}

static int copy_from_user_policy_type(u8 *tp, struct rtattr **xfrma)
{
	struct rtattr *rt = xfrma[XFRMA_POLICY_TYPE-1];
	struct xfrm_userpolicy_type *upt;
	u8 type = XFRM_POLICY_TYPE_MAIN;
	int err;

	if (rt) {
		if (rt->rta_len < sizeof(*upt))
			return -EINVAL;

		upt = RTA_DATA(rt);
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

static struct xfrm_policy *xfrm_policy_construct(struct xfrm_userpolicy_info *p, struct rtattr **xfrma, int *errp)
{
	struct xfrm_policy *xp = xfrm_policy_alloc(GFP_KERNEL);
	int err;

	if (!xp) {
		*errp = -ENOMEM;
		return NULL;
	}

	copy_from_user_policy(xp, p);

	err = copy_from_user_policy_type(&xp->type, xfrma);
	if (err)
		goto error;

	if (!(err = copy_from_user_tmpl(xp, xfrma)))
		err = copy_from_user_sec_ctx(xp, xfrma);
	if (err)
		goto error;

	return xp;
 error:
	*errp = err;
	kfree(xp);
	return NULL;
}

static int xfrm_add_policy(struct sk_buff *skb, struct nlmsghdr *nlh, void **xfrma)
{
	struct xfrm_userpolicy_info *p = NLMSG_DATA(nlh);
	struct xfrm_policy *xp;
	struct km_event c;
	int err;
	int excl;

	err = verify_newpolicy_info(p);
	if (err)
		return err;
	err = verify_sec_ctx_len((struct rtattr **)xfrma);
	if (err)
		return err;

	xp = xfrm_policy_construct(p, (struct rtattr **)xfrma, &err);
	if (!xp)
		return err;

	/* shouldnt excl be based on nlh flags??
	 * Aha! this is anti-netlink really i.e  more pfkey derived
	 * in netlink excl is a flag and you wouldnt need
	 * a type XFRM_MSG_UPDPOLICY - JHS */
	excl = nlh->nlmsg_type == XFRM_MSG_NEWPOLICY;
	err = xfrm_policy_insert(p->dir, xp, excl);
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
	RTA_PUT(skb, XFRMA_TMPL,
		(sizeof(struct xfrm_user_tmpl) * xp->xfrm_nr),
		vec);

	return 0;

rtattr_failure:
	return -1;
}

static int copy_sec_ctx(struct xfrm_sec_ctx *s, struct sk_buff *skb)
{
	int ctx_size = sizeof(struct xfrm_sec_ctx) + s->ctx_len;
	struct rtattr *rt = __RTA_PUT(skb, XFRMA_SEC_CTX, ctx_size);
	struct xfrm_user_sec_ctx *uctx = RTA_DATA(rt);

	uctx->exttype = XFRMA_SEC_CTX;
	uctx->len = ctx_size;
	uctx->ctx_doi = s->ctx_doi;
	uctx->ctx_alg = s->ctx_alg;
	uctx->ctx_len = s->ctx_len;
	memcpy(uctx + 1, s->ctx_str, s->ctx_len);
 	return 0;

 rtattr_failure:
	return -1;
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

#ifdef CONFIG_XFRM_SUB_POLICY
static int copy_to_user_policy_type(u8 type, struct sk_buff *skb)
{
	struct xfrm_userpolicy_type upt;

	memset(&upt, 0, sizeof(upt));
	upt.type = type;

	RTA_PUT(skb, XFRMA_POLICY_TYPE, sizeof(upt), &upt);

	return 0;

rtattr_failure:
	return -1;
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
	unsigned char *b = skb->tail;

	if (sp->this_idx < sp->start_idx)
		goto out;

	nlh = NLMSG_PUT(skb, NETLINK_CB(in_skb).pid,
			sp->nlmsg_seq,
			XFRM_MSG_NEWPOLICY, sizeof(*p));
	p = NLMSG_DATA(nlh);
	nlh->nlmsg_flags = sp->nlmsg_flags;

	copy_to_user_policy(xp, p, dir);
	if (copy_to_user_tmpl(xp, skb) < 0)
		goto nlmsg_failure;
	if (copy_to_user_sec_ctx(xp, skb))
		goto nlmsg_failure;
	if (copy_to_user_policy_type(xp->type, skb) < 0)
		goto nlmsg_failure;

	nlh->nlmsg_len = skb->tail - b;
out:
	sp->this_idx++;
	return 0;

nlmsg_failure:
	skb_trim(skb, b - skb->data);
	return -1;
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

	skb = alloc_skb(NLMSG_GOODSIZE, GFP_KERNEL);
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

static int xfrm_get_policy(struct sk_buff *skb, struct nlmsghdr *nlh, void **xfrma)
{
	struct xfrm_policy *xp;
	struct xfrm_userpolicy_id *p;
	u8 type = XFRM_POLICY_TYPE_MAIN;
	int err;
	struct km_event c;
	int delete;

	p = NLMSG_DATA(nlh);
	delete = nlh->nlmsg_type == XFRM_MSG_DELPOLICY;

	err = copy_from_user_policy_type(&type, (struct rtattr **)xfrma);
	if (err)
		return err;

	err = verify_policy_dir(p->dir);
	if (err)
		return err;

	if (p->index)
		xp = xfrm_policy_byid(type, p->dir, p->index, delete);
	else {
		struct rtattr **rtattrs = (struct rtattr **)xfrma;
		struct rtattr *rt = rtattrs[XFRMA_SEC_CTX-1];
		struct xfrm_policy tmp;

		err = verify_sec_ctx_len(rtattrs);
		if (err)
			return err;

		memset(&tmp, 0, sizeof(struct xfrm_policy));
		if (rt) {
			struct xfrm_user_sec_ctx *uctx = RTA_DATA(rt);

			if ((err = security_xfrm_policy_alloc(&tmp, uctx)))
				return err;
		}
		xp = xfrm_policy_bysel_ctx(type, p->dir, &p->sel, tmp.security, delete);
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
			err = netlink_unicast(xfrm_nl, resp_skb,
					      NETLINK_CB(skb).pid,
					      MSG_DONTWAIT);
		}
	} else {
		if ((err = security_xfrm_policy_delete(xp)) != 0)
			goto out;
		c.data.byid = p->index;
		c.event = nlh->nlmsg_type;
		c.seq = nlh->nlmsg_seq;
		c.pid = nlh->nlmsg_pid;
		km_policy_notify(xp, p->dir, &c);
	}

	xfrm_pol_put(xp);

out:
	return err;
}

static int xfrm_flush_sa(struct sk_buff *skb, struct nlmsghdr *nlh, void **xfrma)
{
	struct km_event c;
	struct xfrm_usersa_flush *p = NLMSG_DATA(nlh);

	xfrm_state_flush(p->proto);
	c.data.proto = p->proto;
	c.event = nlh->nlmsg_type;
	c.seq = nlh->nlmsg_seq;
	c.pid = nlh->nlmsg_pid;
	km_state_notify(NULL, &c);

	return 0;
}


static int build_aevent(struct sk_buff *skb, struct xfrm_state *x, struct km_event *c)
{
	struct xfrm_aevent_id *id;
	struct nlmsghdr *nlh;
	struct xfrm_lifetime_cur ltime;
	unsigned char *b = skb->tail;

	nlh = NLMSG_PUT(skb, c->pid, c->seq, XFRM_MSG_NEWAE, sizeof(*id));
	id = NLMSG_DATA(nlh);
	nlh->nlmsg_flags = 0;

	memcpy(&id->sa_id.daddr, &x->id.daddr,sizeof(x->id.daddr));
	id->sa_id.spi = x->id.spi;
	id->sa_id.family = x->props.family;
	id->sa_id.proto = x->id.proto;
	memcpy(&id->saddr, &x->props.saddr,sizeof(x->props.saddr));
	id->reqid = x->props.reqid;
	id->flags = c->data.aevent;

	RTA_PUT(skb, XFRMA_REPLAY_VAL, sizeof(x->replay), &x->replay);

	ltime.bytes = x->curlft.bytes;
	ltime.packets = x->curlft.packets;
	ltime.add_time = x->curlft.add_time;
	ltime.use_time = x->curlft.use_time;

	RTA_PUT(skb, XFRMA_LTIME_VAL, sizeof(struct xfrm_lifetime_cur), &ltime);

	if (id->flags&XFRM_AE_RTHR) {
		RTA_PUT(skb,XFRMA_REPLAY_THRESH,sizeof(u32),&x->replay_maxdiff);
	}

	if (id->flags&XFRM_AE_ETHR) {
		u32 etimer = x->replay_maxage*10/HZ;
		RTA_PUT(skb,XFRMA_ETIMER_THRESH,sizeof(u32),&etimer);
	}

	nlh->nlmsg_len = skb->tail - b;
	return skb->len;

rtattr_failure:
nlmsg_failure:
	skb_trim(skb, b - skb->data);
	return -1;
}

static int xfrm_get_ae(struct sk_buff *skb, struct nlmsghdr *nlh, void **xfrma)
{
	struct xfrm_state *x;
	struct sk_buff *r_skb;
	int err;
	struct km_event c;
	struct xfrm_aevent_id *p = NLMSG_DATA(nlh);
	int len = NLMSG_LENGTH(sizeof(struct xfrm_aevent_id));
	struct xfrm_usersa_id *id = &p->sa_id;

	len += RTA_SPACE(sizeof(struct xfrm_replay_state));
	len += RTA_SPACE(sizeof(struct xfrm_lifetime_cur));

	if (p->flags&XFRM_AE_RTHR)
		len+=RTA_SPACE(sizeof(u32));

	if (p->flags&XFRM_AE_ETHR)
		len+=RTA_SPACE(sizeof(u32));

	r_skb = alloc_skb(len, GFP_ATOMIC);
	if (r_skb == NULL)
		return -ENOMEM;

	x = xfrm_state_lookup(&id->daddr, id->spi, id->proto, id->family);
	if (x == NULL) {
		kfree(r_skb);
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
	err = netlink_unicast(xfrm_nl, r_skb,
			      NETLINK_CB(skb).pid, MSG_DONTWAIT);
	spin_unlock_bh(&x->lock);
	xfrm_state_put(x);
	return err;
}

static int xfrm_new_ae(struct sk_buff *skb, struct nlmsghdr *nlh, void **xfrma)
{
	struct xfrm_state *x;
	struct km_event c;
	int err = - EINVAL;
	struct xfrm_aevent_id *p = NLMSG_DATA(nlh);
	struct rtattr *rp = xfrma[XFRMA_REPLAY_VAL-1];
	struct rtattr *lt = xfrma[XFRMA_LTIME_VAL-1];

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
	err = xfrm_update_ae_params(x,(struct rtattr **)xfrma);
	spin_unlock_bh(&x->lock);
	if (err	< 0)
		goto out;

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

static int xfrm_flush_policy(struct sk_buff *skb, struct nlmsghdr *nlh, void **xfrma)
{
	struct km_event c;
	u8 type = XFRM_POLICY_TYPE_MAIN;
	int err;

	err = copy_from_user_policy_type(&type, (struct rtattr **)xfrma);
	if (err)
		return err;

	xfrm_policy_flush(type);
	c.data.type = type;
	c.event = nlh->nlmsg_type;
	c.seq = nlh->nlmsg_seq;
	c.pid = nlh->nlmsg_pid;
	km_policy_notify(NULL, 0, &c);
	return 0;
}

static int xfrm_add_pol_expire(struct sk_buff *skb, struct nlmsghdr *nlh, void **xfrma)
{
	struct xfrm_policy *xp;
	struct xfrm_user_polexpire *up = NLMSG_DATA(nlh);
	struct xfrm_userpolicy_info *p = &up->pol;
	u8 type = XFRM_POLICY_TYPE_MAIN;
	int err = -ENOENT;

	err = copy_from_user_policy_type(&type, (struct rtattr **)xfrma);
	if (err)
		return err;

	if (p->index)
		xp = xfrm_policy_byid(type, p->dir, p->index, 0);
	else {
		struct rtattr **rtattrs = (struct rtattr **)xfrma;
		struct rtattr *rt = rtattrs[XFRMA_SEC_CTX-1];
		struct xfrm_policy tmp;

		err = verify_sec_ctx_len(rtattrs);
		if (err)
			return err;

		memset(&tmp, 0, sizeof(struct xfrm_policy));
		if (rt) {
			struct xfrm_user_sec_ctx *uctx = RTA_DATA(rt);

			if ((err = security_xfrm_policy_alloc(&tmp, uctx)))
				return err;
		}
		xp = xfrm_policy_bysel_ctx(type, p->dir, &p->sel, tmp.security, 0);
		security_xfrm_policy_free(&tmp);
	}

	if (xp == NULL)
		return err;
											read_lock(&xp->lock);
	if (xp->dead) {
		read_unlock(&xp->lock);
		goto out;
	}

	read_unlock(&xp->lock);
	err = 0;
	if (up->hard) {
		xfrm_policy_delete(xp, p->dir);
	} else {
		// reset the timers here?
		printk("Dont know what to do with soft policy expire\n");
	}
	km_policy_expired(xp, p->dir, up->hard, current->pid);

out:
	xfrm_pol_put(xp);
	return err;
}

static int xfrm_add_sa_expire(struct sk_buff *skb, struct nlmsghdr *nlh, void **xfrma)
{
	struct xfrm_state *x;
	int err;
	struct xfrm_user_expire *ue = NLMSG_DATA(nlh);
	struct xfrm_usersa_info *p = &ue->state;

	x = xfrm_state_lookup(&p->id.daddr, p->id.spi, p->id.proto, p->family);
		err = -ENOENT;

	if (x == NULL)
		return err;

	err = -EINVAL;

	spin_lock_bh(&x->lock);
	if (x->km.state != XFRM_STATE_VALID)
		goto out;
	km_state_expired(x, ue->hard, current->pid);

	if (ue->hard)
		__xfrm_state_delete(x);
out:
	spin_unlock_bh(&x->lock);
	xfrm_state_put(x);
	return err;
}

static int xfrm_add_acquire(struct sk_buff *skb, struct nlmsghdr *nlh, void **xfrma)
{
	struct xfrm_policy *xp;
	struct xfrm_user_tmpl *ut;
	int i;
	struct rtattr *rt = xfrma[XFRMA_TMPL-1];

	struct xfrm_user_acquire *ua = NLMSG_DATA(nlh);
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
	xp = xfrm_policy_construct(&ua->policy, (struct rtattr **) xfrma, &err);        if (!xp) {
		kfree(x);
		return err;
	}

	memcpy(&x->id, &ua->id, sizeof(ua->id));
	memcpy(&x->props.saddr, &ua->saddr, sizeof(ua->saddr));
	memcpy(&x->sel, &ua->sel, sizeof(ua->sel));

	ut = RTA_DATA(rt);
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


#define XMSGSIZE(type) NLMSG_LENGTH(sizeof(struct type))

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
	[XFRM_MSG_FLUSHPOLICY - XFRM_MSG_BASE] = NLMSG_LENGTH(0),
	[XFRM_MSG_NEWAE       - XFRM_MSG_BASE] = XMSGSIZE(xfrm_aevent_id),
	[XFRM_MSG_GETAE       - XFRM_MSG_BASE] = XMSGSIZE(xfrm_aevent_id),
	[XFRM_MSG_REPORT      - XFRM_MSG_BASE] = XMSGSIZE(xfrm_user_report),
};

#undef XMSGSIZE

static struct xfrm_link {
	int (*doit)(struct sk_buff *, struct nlmsghdr *, void **);
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
};

static int xfrm_user_rcv_msg(struct sk_buff *skb, struct nlmsghdr *nlh, int *errp)
{
	struct rtattr *xfrma[XFRMA_MAX];
	struct xfrm_link *link;
	int type, min_len;

	if (!(nlh->nlmsg_flags & NLM_F_REQUEST))
		return 0;

	type = nlh->nlmsg_type;

	/* A control message: ignore them */
	if (type < XFRM_MSG_BASE)
		return 0;

	/* Unknown message: reply with EINVAL */
	if (type > XFRM_MSG_MAX)
		goto err_einval;

	type -= XFRM_MSG_BASE;
	link = &xfrm_dispatch[type];

	/* All operations require privileges, even GET */
	if (security_netlink_recv(skb, CAP_NET_ADMIN)) {
		*errp = -EPERM;
		return -1;
	}

	if ((type == (XFRM_MSG_GETSA - XFRM_MSG_BASE) ||
	     type == (XFRM_MSG_GETPOLICY - XFRM_MSG_BASE)) &&
	    (nlh->nlmsg_flags & NLM_F_DUMP)) {
		if (link->dump == NULL)
			goto err_einval;

		if ((*errp = netlink_dump_start(xfrm_nl, skb, nlh,
						link->dump, NULL)) != 0) {
			return -1;
		}

		netlink_queue_skip(nlh, skb);
		return -1;
	}

	memset(xfrma, 0, sizeof(xfrma));

	if (nlh->nlmsg_len < (min_len = xfrm_msg_min[type]))
		goto err_einval;

	if (nlh->nlmsg_len > min_len) {
		int attrlen = nlh->nlmsg_len - NLMSG_ALIGN(min_len);
		struct rtattr *attr = (void *) nlh + NLMSG_ALIGN(min_len);

		while (RTA_OK(attr, attrlen)) {
			unsigned short flavor = attr->rta_type;
			if (flavor) {
				if (flavor > XFRMA_MAX)
					goto err_einval;
				xfrma[flavor - 1] = attr;
			}
			attr = RTA_NEXT(attr, attrlen);
		}
	}

	if (link->doit == NULL)
		goto err_einval;
	*errp = link->doit(skb, nlh, (void **) &xfrma);

	return *errp;

err_einval:
	*errp = -EINVAL;
	return -1;
}

static void xfrm_netlink_rcv(struct sock *sk, int len)
{
	unsigned int qlen = 0;

	do {
		mutex_lock(&xfrm_cfg_mutex);
		netlink_run_queue(sk, &qlen, &xfrm_user_rcv_msg);
		mutex_unlock(&xfrm_cfg_mutex);

	} while (qlen);
}

static int build_expire(struct sk_buff *skb, struct xfrm_state *x, struct km_event *c)
{
	struct xfrm_user_expire *ue;
	struct nlmsghdr *nlh;
	unsigned char *b = skb->tail;

	nlh = NLMSG_PUT(skb, c->pid, 0, XFRM_MSG_EXPIRE,
			sizeof(*ue));
	ue = NLMSG_DATA(nlh);
	nlh->nlmsg_flags = 0;

	copy_to_user_state(x, &ue->state);
	ue->hard = (c->data.hard != 0) ? 1 : 0;

	nlh->nlmsg_len = skb->tail - b;
	return skb->len;

nlmsg_failure:
	skb_trim(skb, b - skb->data);
	return -1;
}

static int xfrm_exp_state_notify(struct xfrm_state *x, struct km_event *c)
{
	struct sk_buff *skb;
	int len = NLMSG_LENGTH(sizeof(struct xfrm_user_expire));

	skb = alloc_skb(len, GFP_ATOMIC);
	if (skb == NULL)
		return -ENOMEM;

	if (build_expire(skb, x, c) < 0)
		BUG();

	NETLINK_CB(skb).dst_group = XFRMNLGRP_EXPIRE;
	return netlink_broadcast(xfrm_nl, skb, 0, XFRMNLGRP_EXPIRE, GFP_ATOMIC);
}

static int xfrm_aevent_state_notify(struct xfrm_state *x, struct km_event *c)
{
	struct sk_buff *skb;
	int len = NLMSG_LENGTH(sizeof(struct xfrm_aevent_id));

	len += RTA_SPACE(sizeof(struct xfrm_replay_state));
	len += RTA_SPACE(sizeof(struct xfrm_lifetime_cur));
	skb = alloc_skb(len, GFP_ATOMIC);
	if (skb == NULL)
		return -ENOMEM;

	if (build_aevent(skb, x, c) < 0)
		BUG();

	NETLINK_CB(skb).dst_group = XFRMNLGRP_AEVENTS;
	return netlink_broadcast(xfrm_nl, skb, 0, XFRMNLGRP_AEVENTS, GFP_ATOMIC);
}

static int xfrm_notify_sa_flush(struct km_event *c)
{
	struct xfrm_usersa_flush *p;
	struct nlmsghdr *nlh;
	struct sk_buff *skb;
	unsigned char *b;
	int len = NLMSG_LENGTH(sizeof(struct xfrm_usersa_flush));

	skb = alloc_skb(len, GFP_ATOMIC);
	if (skb == NULL)
		return -ENOMEM;
	b = skb->tail;

	nlh = NLMSG_PUT(skb, c->pid, c->seq,
			XFRM_MSG_FLUSHSA, sizeof(*p));
	nlh->nlmsg_flags = 0;

	p = NLMSG_DATA(nlh);
	p->proto = c->data.proto;

	nlh->nlmsg_len = skb->tail - b;

	NETLINK_CB(skb).dst_group = XFRMNLGRP_SA;
	return netlink_broadcast(xfrm_nl, skb, 0, XFRMNLGRP_SA, GFP_ATOMIC);

nlmsg_failure:
	kfree_skb(skb);
	return -1;
}

static int inline xfrm_sa_len(struct xfrm_state *x)
{
	int l = 0;
	if (x->aalg)
		l += RTA_SPACE(sizeof(*x->aalg) + (x->aalg->alg_key_len+7)/8);
	if (x->ealg)
		l += RTA_SPACE(sizeof(*x->ealg) + (x->ealg->alg_key_len+7)/8);
	if (x->calg)
		l += RTA_SPACE(sizeof(*x->calg));
	if (x->encap)
		l += RTA_SPACE(sizeof(*x->encap));

	return l;
}

static int xfrm_notify_sa(struct xfrm_state *x, struct km_event *c)
{
	struct xfrm_usersa_info *p;
	struct xfrm_usersa_id *id;
	struct nlmsghdr *nlh;
	struct sk_buff *skb;
	unsigned char *b;
	int len = xfrm_sa_len(x);
	int headlen;

	headlen = sizeof(*p);
	if (c->event == XFRM_MSG_DELSA) {
		len += RTA_SPACE(headlen);
		headlen = sizeof(*id);
	}
	len += NLMSG_SPACE(headlen);

	skb = alloc_skb(len, GFP_ATOMIC);
	if (skb == NULL)
		return -ENOMEM;
	b = skb->tail;

	nlh = NLMSG_PUT(skb, c->pid, c->seq, c->event, headlen);
	nlh->nlmsg_flags = 0;

	p = NLMSG_DATA(nlh);
	if (c->event == XFRM_MSG_DELSA) {
		id = NLMSG_DATA(nlh);
		memcpy(&id->daddr, &x->id.daddr, sizeof(id->daddr));
		id->spi = x->id.spi;
		id->family = x->props.family;
		id->proto = x->id.proto;

		p = RTA_DATA(__RTA_PUT(skb, XFRMA_SA, sizeof(*p)));
	}

	copy_to_user_state(x, p);

	if (x->aalg)
		RTA_PUT(skb, XFRMA_ALG_AUTH,
			sizeof(*(x->aalg))+(x->aalg->alg_key_len+7)/8, x->aalg);
	if (x->ealg)
		RTA_PUT(skb, XFRMA_ALG_CRYPT,
			sizeof(*(x->ealg))+(x->ealg->alg_key_len+7)/8, x->ealg);
	if (x->calg)
		RTA_PUT(skb, XFRMA_ALG_COMP, sizeof(*(x->calg)), x->calg);

	if (x->encap)
		RTA_PUT(skb, XFRMA_ENCAP, sizeof(*x->encap), x->encap);

	nlh->nlmsg_len = skb->tail - b;

	NETLINK_CB(skb).dst_group = XFRMNLGRP_SA;
	return netlink_broadcast(xfrm_nl, skb, 0, XFRMNLGRP_SA, GFP_ATOMIC);

nlmsg_failure:
rtattr_failure:
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

static int build_acquire(struct sk_buff *skb, struct xfrm_state *x,
			 struct xfrm_tmpl *xt, struct xfrm_policy *xp,
			 int dir)
{
	struct xfrm_user_acquire *ua;
	struct nlmsghdr *nlh;
	unsigned char *b = skb->tail;
	__u32 seq = xfrm_get_acqseq();

	nlh = NLMSG_PUT(skb, 0, 0, XFRM_MSG_ACQUIRE,
			sizeof(*ua));
	ua = NLMSG_DATA(nlh);
	nlh->nlmsg_flags = 0;

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

	nlh->nlmsg_len = skb->tail - b;
	return skb->len;

nlmsg_failure:
	skb_trim(skb, b - skb->data);
	return -1;
}

static int xfrm_send_acquire(struct xfrm_state *x, struct xfrm_tmpl *xt,
			     struct xfrm_policy *xp, int dir)
{
	struct sk_buff *skb;
	size_t len;

	len = RTA_SPACE(sizeof(struct xfrm_user_tmpl) * xp->xfrm_nr);
	len += NLMSG_SPACE(sizeof(struct xfrm_user_acquire));
	len += RTA_SPACE(xfrm_user_sec_ctx_size(xp));
#ifdef CONFIG_XFRM_SUB_POLICY
	len += RTA_SPACE(sizeof(struct xfrm_userpolicy_type));
#endif
	skb = alloc_skb(len, GFP_ATOMIC);
	if (skb == NULL)
		return -ENOMEM;

	if (build_acquire(skb, x, xt, xp, dir) < 0)
		BUG();

	NETLINK_CB(skb).dst_group = XFRMNLGRP_ACQUIRE;
	return netlink_broadcast(xfrm_nl, skb, 0, XFRMNLGRP_ACQUIRE, GFP_ATOMIC);
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
	if (nr > XFRM_MAX_DEPTH)
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

static int build_polexpire(struct sk_buff *skb, struct xfrm_policy *xp,
			   int dir, struct km_event *c)
{
	struct xfrm_user_polexpire *upe;
	struct nlmsghdr *nlh;
	int hard = c->data.hard;
	unsigned char *b = skb->tail;

	nlh = NLMSG_PUT(skb, c->pid, 0, XFRM_MSG_POLEXPIRE, sizeof(*upe));
	upe = NLMSG_DATA(nlh);
	nlh->nlmsg_flags = 0;

	copy_to_user_policy(xp, &upe->pol, dir);
	if (copy_to_user_tmpl(xp, skb) < 0)
		goto nlmsg_failure;
	if (copy_to_user_sec_ctx(xp, skb))
		goto nlmsg_failure;
	if (copy_to_user_policy_type(xp->type, skb) < 0)
		goto nlmsg_failure;
	upe->hard = !!hard;

	nlh->nlmsg_len = skb->tail - b;
	return skb->len;

nlmsg_failure:
	skb_trim(skb, b - skb->data);
	return -1;
}

static int xfrm_exp_policy_notify(struct xfrm_policy *xp, int dir, struct km_event *c)
{
	struct sk_buff *skb;
	size_t len;

	len = RTA_SPACE(sizeof(struct xfrm_user_tmpl) * xp->xfrm_nr);
	len += NLMSG_SPACE(sizeof(struct xfrm_user_polexpire));
	len += RTA_SPACE(xfrm_user_sec_ctx_size(xp));
#ifdef CONFIG_XFRM_SUB_POLICY
	len += RTA_SPACE(sizeof(struct xfrm_userpolicy_type));
#endif
	skb = alloc_skb(len, GFP_ATOMIC);
	if (skb == NULL)
		return -ENOMEM;

	if (build_polexpire(skb, xp, dir, c) < 0)
		BUG();

	NETLINK_CB(skb).dst_group = XFRMNLGRP_EXPIRE;
	return netlink_broadcast(xfrm_nl, skb, 0, XFRMNLGRP_EXPIRE, GFP_ATOMIC);
}

static int xfrm_notify_policy(struct xfrm_policy *xp, int dir, struct km_event *c)
{
	struct xfrm_userpolicy_info *p;
	struct xfrm_userpolicy_id *id;
	struct nlmsghdr *nlh;
	struct sk_buff *skb;
	unsigned char *b;
	int len = RTA_SPACE(sizeof(struct xfrm_user_tmpl) * xp->xfrm_nr);
	int headlen;

	headlen = sizeof(*p);
	if (c->event == XFRM_MSG_DELPOLICY) {
		len += RTA_SPACE(headlen);
		headlen = sizeof(*id);
	}
#ifdef CONFIG_XFRM_SUB_POLICY
	len += RTA_SPACE(sizeof(struct xfrm_userpolicy_type));
#endif
	len += NLMSG_SPACE(headlen);

	skb = alloc_skb(len, GFP_ATOMIC);
	if (skb == NULL)
		return -ENOMEM;
	b = skb->tail;

	nlh = NLMSG_PUT(skb, c->pid, c->seq, c->event, headlen);

	p = NLMSG_DATA(nlh);
	if (c->event == XFRM_MSG_DELPOLICY) {
		id = NLMSG_DATA(nlh);
		memset(id, 0, sizeof(*id));
		id->dir = dir;
		if (c->data.byid)
			id->index = xp->index;
		else
			memcpy(&id->sel, &xp->selector, sizeof(id->sel));

		p = RTA_DATA(__RTA_PUT(skb, XFRMA_POLICY, sizeof(*p)));
	}

	nlh->nlmsg_flags = 0;

	copy_to_user_policy(xp, p, dir);
	if (copy_to_user_tmpl(xp, skb) < 0)
		goto nlmsg_failure;
	if (copy_to_user_policy_type(xp->type, skb) < 0)
		goto nlmsg_failure;

	nlh->nlmsg_len = skb->tail - b;

	NETLINK_CB(skb).dst_group = XFRMNLGRP_POLICY;
	return netlink_broadcast(xfrm_nl, skb, 0, XFRMNLGRP_POLICY, GFP_ATOMIC);

nlmsg_failure:
rtattr_failure:
	kfree_skb(skb);
	return -1;
}

static int xfrm_notify_policy_flush(struct km_event *c)
{
	struct nlmsghdr *nlh;
	struct sk_buff *skb;
	unsigned char *b;
	int len = 0;
#ifdef CONFIG_XFRM_SUB_POLICY
	len += RTA_SPACE(sizeof(struct xfrm_userpolicy_type));
#endif
	len += NLMSG_LENGTH(0);

	skb = alloc_skb(len, GFP_ATOMIC);
	if (skb == NULL)
		return -ENOMEM;
	b = skb->tail;


	nlh = NLMSG_PUT(skb, c->pid, c->seq, XFRM_MSG_FLUSHPOLICY, 0);
	nlh->nlmsg_flags = 0;
	if (copy_to_user_policy_type(c->data.type, skb) < 0)
		goto nlmsg_failure;

	nlh->nlmsg_len = skb->tail - b;

	NETLINK_CB(skb).dst_group = XFRMNLGRP_POLICY;
	return netlink_broadcast(xfrm_nl, skb, 0, XFRMNLGRP_POLICY, GFP_ATOMIC);

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

static int build_report(struct sk_buff *skb, u8 proto,
			struct xfrm_selector *sel, xfrm_address_t *addr)
{
	struct xfrm_user_report *ur;
	struct nlmsghdr *nlh;
	unsigned char *b = skb->tail;

	nlh = NLMSG_PUT(skb, 0, 0, XFRM_MSG_REPORT, sizeof(*ur));
	ur = NLMSG_DATA(nlh);
	nlh->nlmsg_flags = 0;

	ur->proto = proto;
	memcpy(&ur->sel, sel, sizeof(ur->sel));

	if (addr)
		RTA_PUT(skb, XFRMA_COADDR, sizeof(*addr), addr);

	nlh->nlmsg_len = skb->tail - b;
	return skb->len;

nlmsg_failure:
rtattr_failure:
	skb_trim(skb, b - skb->data);
	return -1;
}

static int xfrm_send_report(u8 proto, struct xfrm_selector *sel,
			    xfrm_address_t *addr)
{
	struct sk_buff *skb;
	size_t len;

	len = NLMSG_ALIGN(NLMSG_LENGTH(sizeof(struct xfrm_user_report)));
	skb = alloc_skb(len, GFP_ATOMIC);
	if (skb == NULL)
		return -ENOMEM;

	if (build_report(skb, proto, sel, addr) < 0)
		BUG();

	NETLINK_CB(skb).dst_group = XFRMNLGRP_REPORT;
	return netlink_broadcast(xfrm_nl, skb, 0, XFRMNLGRP_REPORT, GFP_ATOMIC);
}

static struct xfrm_mgr netlink_mgr = {
	.id		= "netlink",
	.notify		= xfrm_send_state_notify,
	.acquire	= xfrm_send_acquire,
	.compile_policy	= xfrm_compile_policy,
	.notify_policy	= xfrm_send_policy_notify,
	.report		= xfrm_send_report,
};

static int __init xfrm_user_init(void)
{
	struct sock *nlsk;

	printk(KERN_INFO "Initializing XFRM netlink socket\n");

	nlsk = netlink_kernel_create(NETLINK_XFRM, XFRMNLGRP_MAX,
	                             xfrm_netlink_rcv, THIS_MODULE);
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
	sock_release(nlsk->sk_socket);
}

module_init(xfrm_user_init);
module_exit(xfrm_user_exit);
MODULE_LICENSE("GPL");
MODULE_ALIAS_NET_PF_PROTO(PF_NETLINK, NETLINK_XFRM);

