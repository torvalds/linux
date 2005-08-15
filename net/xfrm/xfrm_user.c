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

#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/types.h>
#include <linux/slab.h>
#include <linux/socket.h>
#include <linux/string.h>
#include <linux/net.h>
#include <linux/skbuff.h>
#include <linux/netlink.h>
#include <linux/rtnetlink.h>
#include <linux/pfkeyv2.h>
#include <linux/ipsec.h>
#include <linux/init.h>
#include <linux/security.h>
#include <net/sock.h>
#include <net/xfrm.h>
#include <asm/uaccess.h>

static struct sock *xfrm_nl;

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

	err = -EINVAL;
	switch (p->mode) {
	case 0:
	case 1:
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
	p = kmalloc(len, GFP_KERNEL);
	if (!p)
		return -ENOMEM;

	memcpy(p, ualg, len);
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
	p = kmalloc(sizeof(*p), GFP_KERNEL);
	if (!p)
		return -ENOMEM;

	memcpy(p, uencap, sizeof(*p));
	*encapp = p;
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
	x->props.saddr = p->saddr;
	x->props.flags = p->flags;
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

	err = xfrm_init_state(x);
	if (err)
		goto error;

	x->km.seq = p->seq;

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

	err = verify_newsa_info(p, (struct rtattr **) xfrma);
	if (err)
		return err;

	x = xfrm_state_construct(p, (struct rtattr **) xfrma, &err);
	if (!x)
		return err;

	xfrm_state_hold(x);
	if (nlh->nlmsg_type == XFRM_MSG_NEWSA)
		err = xfrm_state_add(x);
	else
		err = xfrm_state_update(x);

	if (err < 0) {
		x->km.state = XFRM_STATE_DEAD;
		xfrm_state_put(x);
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

static int xfrm_del_sa(struct sk_buff *skb, struct nlmsghdr *nlh, void **xfrma)
{
	struct xfrm_state *x;
	int err;
	struct km_event c;
	struct xfrm_usersa_id *p = NLMSG_DATA(nlh);

	x = xfrm_state_lookup(&p->daddr, p->spi, p->proto, p->family);
	if (x == NULL)
		return -ESRCH;

	if (xfrm_state_kern(x)) {
		xfrm_state_put(x);
		return -EPERM;
	}

	err = xfrm_state_delete(x);
	if (err < 0) {
		xfrm_state_put(x);
		return err;
	}

	c.seq = nlh->nlmsg_seq;
	c.pid = nlh->nlmsg_pid;
	c.event = nlh->nlmsg_type;
	km_state_notify(x, &c);
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
	p->saddr = x->props.saddr;
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
	(void) xfrm_state_walk(IPSEC_PROTO_ANY, dump_one_state, &info);
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

	NETLINK_CB(skb).dst_pid = NETLINK_CB(in_skb).pid;
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
	int err;

	x = xfrm_state_lookup(&p->daddr, p->spi, p->proto, p->family);
	err = -ESRCH;
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

static int verify_policy_dir(__u8 dir)
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
	err = copy_from_user_tmpl(xp, xfrma);
	if (err) {
		*errp = err;
		kfree(xp);
		xp = NULL;
	}

	return xp;
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

	xp = xfrm_policy_construct(p, (struct rtattr **) xfrma, &err);
	if (!xp)
		return err;

	/* shouldnt excl be based on nlh flags??
	 * Aha! this is anti-netlink really i.e  more pfkey derived
	 * in netlink excl is a flag and you wouldnt need
	 * a type XFRM_MSG_UPDPOLICY - JHS */
	excl = nlh->nlmsg_type == XFRM_MSG_NEWPOLICY;
	err = xfrm_policy_insert(p->dir, xp, excl);
	if (err) {
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
		up->family = xp->family;
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
	(void) xfrm_policy_walk(dump_one_policy, &info);
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

	NETLINK_CB(skb).dst_pid = NETLINK_CB(in_skb).pid;
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
	int err;
	struct km_event c;
	int delete;

	p = NLMSG_DATA(nlh);
	delete = nlh->nlmsg_type == XFRM_MSG_DELPOLICY;

	err = verify_policy_dir(p->dir);
	if (err)
		return err;

	if (p->index)
		xp = xfrm_policy_byid(p->dir, p->index, delete);
	else
		xp = xfrm_policy_bysel(p->dir, &p->sel, delete);
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
		c.data.byid = p->index;
		c.event = nlh->nlmsg_type;
		c.seq = nlh->nlmsg_seq;
		c.pid = nlh->nlmsg_pid;
		km_policy_notify(xp, p->dir, &c);
	}

	xfrm_pol_put(xp);

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

static int xfrm_flush_policy(struct sk_buff *skb, struct nlmsghdr *nlh, void **xfrma)
{
	struct km_event c;

	xfrm_policy_flush();
	c.event = nlh->nlmsg_type;
	c.seq = nlh->nlmsg_seq;
	c.pid = nlh->nlmsg_pid;
	km_policy_notify(NULL, 0, &c);
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
	[XFRM_MSG_UPDPOLICY   - XFRM_MSG_BASE] = { .doit = xfrm_add_policy    },
	[XFRM_MSG_UPDSA       - XFRM_MSG_BASE] = { .doit = xfrm_add_sa        },
	[XFRM_MSG_FLUSHSA     - XFRM_MSG_BASE] = { .doit = xfrm_flush_sa      },
	[XFRM_MSG_FLUSHPOLICY - XFRM_MSG_BASE] = { .doit = xfrm_flush_policy  },
};

static int xfrm_done(struct netlink_callback *cb)
{
	return 0;
}

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
	if (security_netlink_recv(skb)) {
		*errp = -EPERM;
		return -1;
	}

	if ((type == (XFRM_MSG_GETSA - XFRM_MSG_BASE) ||
	     type == (XFRM_MSG_GETPOLICY - XFRM_MSG_BASE)) &&
	    (nlh->nlmsg_flags & NLM_F_DUMP)) {
		u32 rlen;

		if (link->dump == NULL)
			goto err_einval;

		if ((*errp = netlink_dump_start(xfrm_nl, skb, nlh,
						link->dump,
						xfrm_done)) != 0) {
			return -1;
		}
		rlen = NLMSG_ALIGN(nlh->nlmsg_len);
		if (rlen > skb->len)
			rlen = skb->len;
		skb_pull(skb, rlen);
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

static int xfrm_user_rcv_skb(struct sk_buff *skb)
{
	int err;
	struct nlmsghdr *nlh;

	while (skb->len >= NLMSG_SPACE(0)) {
		u32 rlen;

		nlh = (struct nlmsghdr *) skb->data;
		if (nlh->nlmsg_len < sizeof(*nlh) ||
		    skb->len < nlh->nlmsg_len)
			return 0;
		rlen = NLMSG_ALIGN(nlh->nlmsg_len);
		if (rlen > skb->len)
			rlen = skb->len;
		if (xfrm_user_rcv_msg(skb, nlh, &err) < 0) {
			if (err == 0)
				return -1;
			netlink_ack(skb, nlh, err);
		} else if (nlh->nlmsg_flags & NLM_F_ACK)
			netlink_ack(skb, nlh, 0);
		skb_pull(skb, rlen);
	}

	return 0;
}

static void xfrm_netlink_rcv(struct sock *sk, int len)
{
	unsigned int qlen = skb_queue_len(&sk->sk_receive_queue);

	do {
		struct sk_buff *skb;

		down(&xfrm_cfg_sem);

		if (qlen > skb_queue_len(&sk->sk_receive_queue))
			qlen = skb_queue_len(&sk->sk_receive_queue);

		for (; qlen; qlen--) {
			skb = skb_dequeue(&sk->sk_receive_queue);
			if (xfrm_user_rcv_skb(skb)) {
				if (skb->len)
					skb_queue_head(&sk->sk_receive_queue,
						       skb);
				else {
					kfree_skb(skb);
					qlen--;
				}
				break;
			}
			kfree_skb(skb);
		}

		up(&xfrm_cfg_sem);

	} while (qlen);
}

static int build_expire(struct sk_buff *skb, struct xfrm_state *x, int hard)
{
	struct xfrm_user_expire *ue;
	struct nlmsghdr *nlh;
	unsigned char *b = skb->tail;

	nlh = NLMSG_PUT(skb, 0, 0, XFRM_MSG_EXPIRE,
			sizeof(*ue));
	ue = NLMSG_DATA(nlh);
	nlh->nlmsg_flags = 0;

	copy_to_user_state(x, &ue->state);
	ue->hard = (hard != 0) ? 1 : 0;

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

	if (build_expire(skb, x, c->data.hard) < 0)
		BUG();

	NETLINK_CB(skb).dst_group = XFRMNLGRP_EXPIRE;
	return netlink_broadcast(xfrm_nl, skb, 0, XFRMNLGRP_EXPIRE, GFP_ATOMIC);
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
static struct xfrm_policy *xfrm_compile_policy(u16 family, int opt,
					       u8 *data, int len, int *dir)
{
	struct xfrm_userpolicy_info *p = (struct xfrm_userpolicy_info *)data;
	struct xfrm_user_tmpl *ut = (struct xfrm_user_tmpl *) (p + 1);
	struct xfrm_policy *xp;
	int nr;

	switch (family) {
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
	copy_templates(xp, ut, nr);

	*dir = p->dir;

	return xp;
}

static int build_polexpire(struct sk_buff *skb, struct xfrm_policy *xp,
			   int dir, int hard)
{
	struct xfrm_user_polexpire *upe;
	struct nlmsghdr *nlh;
	unsigned char *b = skb->tail;

	nlh = NLMSG_PUT(skb, 0, 0, XFRM_MSG_POLEXPIRE, sizeof(*upe));
	upe = NLMSG_DATA(nlh);
	nlh->nlmsg_flags = 0;

	copy_to_user_policy(xp, &upe->pol, dir);
	if (copy_to_user_tmpl(xp, skb) < 0)
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
	skb = alloc_skb(len, GFP_ATOMIC);
	if (skb == NULL)
		return -ENOMEM;

	if (build_polexpire(skb, xp, dir, c->data.hard) < 0)
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
	int len = NLMSG_LENGTH(0);

	skb = alloc_skb(len, GFP_ATOMIC);
	if (skb == NULL)
		return -ENOMEM;
	b = skb->tail;


	nlh = NLMSG_PUT(skb, c->pid, c->seq, XFRM_MSG_FLUSHPOLICY, 0);

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

static struct xfrm_mgr netlink_mgr = {
	.id		= "netlink",
	.notify		= xfrm_send_state_notify,
	.acquire	= xfrm_send_acquire,
	.compile_policy	= xfrm_compile_policy,
	.notify_policy	= xfrm_send_policy_notify,
};

static int __init xfrm_user_init(void)
{
	printk(KERN_INFO "Initializing IPsec netlink socket\n");

	xfrm_nl = netlink_kernel_create(NETLINK_XFRM, XFRMNLGRP_MAX,
	                                xfrm_netlink_rcv, THIS_MODULE);
	if (xfrm_nl == NULL)
		return -ENOMEM;

	xfrm_register_km(&netlink_mgr);

	return 0;
}

static void __exit xfrm_user_exit(void)
{
	xfrm_unregister_km(&netlink_mgr);
	sock_release(xfrm_nl->sk_socket);
}

module_init(xfrm_user_init);
module_exit(xfrm_user_exit);
MODULE_LICENSE("GPL");
MODULE_ALIAS_NET_PF_PROTO(PF_NETLINK, NETLINK_XFRM);
