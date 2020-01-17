// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * net/sched/em_meta.c	Metadata ematch
 *
 * Authors:	Thomas Graf <tgraf@suug.ch>
 *
 * ==========================================================================
 *
 * 	The metadata ematch compares two meta objects where each object
 * 	represents either a meta value stored in the kernel or a static
 * 	value provided by userspace. The objects are not provided by
 * 	userspace itself but rather a definition providing the information
 * 	to build them. Every object is of a certain type which must be
 * 	equal to the object it is being compared to.
 *
 * 	The definition of a objects conists of the type (meta type), a
 * 	identifier (meta id) and additional type specific information.
 * 	The meta id is either TCF_META_TYPE_VALUE for values provided by
 * 	userspace or a index to the meta operations table consisting of
 * 	function pointers to type specific meta data collectors returning
 * 	the value of the requested meta value.
 *
 * 	         lvalue                                   rvalue
 * 	      +-----------+                           +-----------+
 * 	      | type: INT |                           | type: INT |
 * 	 def  | id: DEV   |                           | id: VALUE |
 * 	      | data:     |                           | data: 3   |
 * 	      +-----------+                           +-----------+
 * 	            |                                       |
 * 	            ---> meta_ops[INT][DEV](...)            |
 *	                      |                             |
 * 	            -----------                             |
 * 	            V                                       V
 * 	      +-----------+                           +-----------+
 * 	      | type: INT |                           | type: INT |
 * 	 obj  | id: DEV |                             | id: VALUE |
 * 	      | data: 2   |<--data got filled out     | data: 3   |
 * 	      +-----------+                           +-----------+
 * 	            |                                         |
 * 	            --------------> 2  equals 3 <--------------
 *
 * 	This is a simplified schema, the complexity varies depending
 * 	on the meta type. Obviously, the length of the data must also
 * 	be provided for non-numeric types.
 *
 * 	Additionally, type dependent modifiers such as shift operators
 * 	or mask may be applied to extend the functionaliy. As of now,
 * 	the variable length type supports shifting the byte string to
 * 	the right, eating up any number of octets and thus supporting
 * 	wildcard interface name comparisons such as "ppp%" matching
 * 	ppp0..9.
 *
 * 	NOTE: Certain meta values depend on other subsystems and are
 * 	      only available if that subsystem is enabled in the kernel.
 */

#include <linux/slab.h>
#include <linux/module.h>
#include <linux/types.h>
#include <linux/kernel.h>
#include <linux/sched.h>
#include <linux/sched/loadavg.h>
#include <linux/string.h>
#include <linux/skbuff.h>
#include <linux/random.h>
#include <linux/if_vlan.h>
#include <linux/tc_ematch/tc_em_meta.h>
#include <net/dst.h>
#include <net/route.h>
#include <net/pkt_cls.h>
#include <net/sock.h>

struct meta_obj {
	unsigned long		value;
	unsigned int		len;
};

struct meta_value {
	struct tcf_meta_val	hdr;
	unsigned long		val;
	unsigned int		len;
};

struct meta_match {
	struct meta_value	lvalue;
	struct meta_value	rvalue;
};

static inline int meta_id(struct meta_value *v)
{
	return TCF_META_ID(v->hdr.kind);
}

static inline int meta_type(struct meta_value *v)
{
	return TCF_META_TYPE(v->hdr.kind);
}

#define META_COLLECTOR(FUNC) static void meta_##FUNC(struct sk_buff *skb, \
	struct tcf_pkt_info *info, struct meta_value *v, \
	struct meta_obj *dst, int *err)

/**************************************************************************
 * System status & misc
 **************************************************************************/

META_COLLECTOR(int_random)
{
	get_random_bytes(&dst->value, sizeof(dst->value));
}

static inline unsigned long fixed_loadavg(int load)
{
	int rnd_load = load + (FIXED_1/200);
	int rnd_frac = ((rnd_load & (FIXED_1-1)) * 100) >> FSHIFT;

	return ((rnd_load >> FSHIFT) * 100) + rnd_frac;
}

META_COLLECTOR(int_loadavg_0)
{
	dst->value = fixed_loadavg(avenrun[0]);
}

META_COLLECTOR(int_loadavg_1)
{
	dst->value = fixed_loadavg(avenrun[1]);
}

META_COLLECTOR(int_loadavg_2)
{
	dst->value = fixed_loadavg(avenrun[2]);
}

/**************************************************************************
 * Device names & indices
 **************************************************************************/

static inline int int_dev(struct net_device *dev, struct meta_obj *dst)
{
	if (unlikely(dev == NULL))
		return -1;

	dst->value = dev->ifindex;
	return 0;
}

static inline int var_dev(struct net_device *dev, struct meta_obj *dst)
{
	if (unlikely(dev == NULL))
		return -1;

	dst->value = (unsigned long) dev->name;
	dst->len = strlen(dev->name);
	return 0;
}

META_COLLECTOR(int_dev)
{
	*err = int_dev(skb->dev, dst);
}

META_COLLECTOR(var_dev)
{
	*err = var_dev(skb->dev, dst);
}

/**************************************************************************
 * vlan tag
 **************************************************************************/

META_COLLECTOR(int_vlan_tag)
{
	unsigned short tag;

	if (skb_vlan_tag_present(skb))
		dst->value = skb_vlan_tag_get(skb);
	else if (!__vlan_get_tag(skb, &tag))
		dst->value = tag;
	else
		*err = -1;
}



/**************************************************************************
 * skb attributes
 **************************************************************************/

META_COLLECTOR(int_priority)
{
	dst->value = skb->priority;
}

META_COLLECTOR(int_protocol)
{
	/* Let userspace take care of the byte ordering */
	dst->value = tc_skb_protocol(skb);
}

META_COLLECTOR(int_pkttype)
{
	dst->value = skb->pkt_type;
}

META_COLLECTOR(int_pktlen)
{
	dst->value = skb->len;
}

META_COLLECTOR(int_datalen)
{
	dst->value = skb->data_len;
}

META_COLLECTOR(int_maclen)
{
	dst->value = skb->mac_len;
}

META_COLLECTOR(int_rxhash)
{
	dst->value = skb_get_hash(skb);
}

/**************************************************************************
 * Netfilter
 **************************************************************************/

META_COLLECTOR(int_mark)
{
	dst->value = skb->mark;
}

/**************************************************************************
 * Traffic Control
 **************************************************************************/

META_COLLECTOR(int_tcindex)
{
	dst->value = skb->tc_index;
}

/**************************************************************************
 * Routing
 **************************************************************************/

META_COLLECTOR(int_rtclassid)
{
	if (unlikely(skb_dst(skb) == NULL))
		*err = -1;
	else
#ifdef CONFIG_IP_ROUTE_CLASSID
		dst->value = skb_dst(skb)->tclassid;
#else
		dst->value = 0;
#endif
}

META_COLLECTOR(int_rtiif)
{
	if (unlikely(skb_rtable(skb) == NULL))
		*err = -1;
	else
		dst->value = inet_iif(skb);
}

/**************************************************************************
 * Socket Attributes
 **************************************************************************/

#define skip_nonlocal(skb) \
	(unlikely(skb->sk == NULL))

META_COLLECTOR(int_sk_family)
{
	if (skip_nonlocal(skb)) {
		*err = -1;
		return;
	}
	dst->value = skb->sk->sk_family;
}

META_COLLECTOR(int_sk_state)
{
	if (skip_nonlocal(skb)) {
		*err = -1;
		return;
	}
	dst->value = skb->sk->sk_state;
}

META_COLLECTOR(int_sk_reuse)
{
	if (skip_nonlocal(skb)) {
		*err = -1;
		return;
	}
	dst->value = skb->sk->sk_reuse;
}

META_COLLECTOR(int_sk_bound_if)
{
	if (skip_nonlocal(skb)) {
		*err = -1;
		return;
	}
	/* No error if bound_dev_if is 0, legal userspace check */
	dst->value = skb->sk->sk_bound_dev_if;
}

META_COLLECTOR(var_sk_bound_if)
{
	if (skip_nonlocal(skb)) {
		*err = -1;
		return;
	}

	if (skb->sk->sk_bound_dev_if == 0) {
		dst->value = (unsigned long) "any";
		dst->len = 3;
	} else {
		struct net_device *dev;

		rcu_read_lock();
		dev = dev_get_by_index_rcu(sock_net(skb->sk),
					   skb->sk->sk_bound_dev_if);
		*err = var_dev(dev, dst);
		rcu_read_unlock();
	}
}

META_COLLECTOR(int_sk_refcnt)
{
	if (skip_nonlocal(skb)) {
		*err = -1;
		return;
	}
	dst->value = refcount_read(&skb->sk->sk_refcnt);
}

META_COLLECTOR(int_sk_rcvbuf)
{
	const struct sock *sk = skb_to_full_sk(skb);

	if (!sk) {
		*err = -1;
		return;
	}
	dst->value = sk->sk_rcvbuf;
}

META_COLLECTOR(int_sk_shutdown)
{
	const struct sock *sk = skb_to_full_sk(skb);

	if (!sk) {
		*err = -1;
		return;
	}
	dst->value = sk->sk_shutdown;
}

META_COLLECTOR(int_sk_proto)
{
	const struct sock *sk = skb_to_full_sk(skb);

	if (!sk) {
		*err = -1;
		return;
	}
	dst->value = sk->sk_protocol;
}

META_COLLECTOR(int_sk_type)
{
	const struct sock *sk = skb_to_full_sk(skb);

	if (!sk) {
		*err = -1;
		return;
	}
	dst->value = sk->sk_type;
}

META_COLLECTOR(int_sk_rmem_alloc)
{
	const struct sock *sk = skb_to_full_sk(skb);

	if (!sk) {
		*err = -1;
		return;
	}
	dst->value = sk_rmem_alloc_get(sk);
}

META_COLLECTOR(int_sk_wmem_alloc)
{
	const struct sock *sk = skb_to_full_sk(skb);

	if (!sk) {
		*err = -1;
		return;
	}
	dst->value = sk_wmem_alloc_get(sk);
}

META_COLLECTOR(int_sk_omem_alloc)
{
	const struct sock *sk = skb_to_full_sk(skb);

	if (!sk) {
		*err = -1;
		return;
	}
	dst->value = atomic_read(&sk->sk_omem_alloc);
}

META_COLLECTOR(int_sk_rcv_qlen)
{
	const struct sock *sk = skb_to_full_sk(skb);

	if (!sk) {
		*err = -1;
		return;
	}
	dst->value = sk->sk_receive_queue.qlen;
}

META_COLLECTOR(int_sk_snd_qlen)
{
	const struct sock *sk = skb_to_full_sk(skb);

	if (!sk) {
		*err = -1;
		return;
	}
	dst->value = sk->sk_write_queue.qlen;
}

META_COLLECTOR(int_sk_wmem_queued)
{
	const struct sock *sk = skb_to_full_sk(skb);

	if (!sk) {
		*err = -1;
		return;
	}
	dst->value = READ_ONCE(sk->sk_wmem_queued);
}

META_COLLECTOR(int_sk_fwd_alloc)
{
	const struct sock *sk = skb_to_full_sk(skb);

	if (!sk) {
		*err = -1;
		return;
	}
	dst->value = sk->sk_forward_alloc;
}

META_COLLECTOR(int_sk_sndbuf)
{
	const struct sock *sk = skb_to_full_sk(skb);

	if (!sk) {
		*err = -1;
		return;
	}
	dst->value = sk->sk_sndbuf;
}

META_COLLECTOR(int_sk_alloc)
{
	const struct sock *sk = skb_to_full_sk(skb);

	if (!sk) {
		*err = -1;
		return;
	}
	dst->value = (__force int) sk->sk_allocation;
}

META_COLLECTOR(int_sk_hash)
{
	if (skip_nonlocal(skb)) {
		*err = -1;
		return;
	}
	dst->value = skb->sk->sk_hash;
}

META_COLLECTOR(int_sk_lingertime)
{
	const struct sock *sk = skb_to_full_sk(skb);

	if (!sk) {
		*err = -1;
		return;
	}
	dst->value = sk->sk_lingertime / HZ;
}

META_COLLECTOR(int_sk_err_qlen)
{
	const struct sock *sk = skb_to_full_sk(skb);

	if (!sk) {
		*err = -1;
		return;
	}
	dst->value = sk->sk_error_queue.qlen;
}

META_COLLECTOR(int_sk_ack_bl)
{
	const struct sock *sk = skb_to_full_sk(skb);

	if (!sk) {
		*err = -1;
		return;
	}
	dst->value = sk->sk_ack_backlog;
}

META_COLLECTOR(int_sk_max_ack_bl)
{
	const struct sock *sk = skb_to_full_sk(skb);

	if (!sk) {
		*err = -1;
		return;
	}
	dst->value = sk->sk_max_ack_backlog;
}

META_COLLECTOR(int_sk_prio)
{
	const struct sock *sk = skb_to_full_sk(skb);

	if (!sk) {
		*err = -1;
		return;
	}
	dst->value = sk->sk_priority;
}

META_COLLECTOR(int_sk_rcvlowat)
{
	const struct sock *sk = skb_to_full_sk(skb);

	if (!sk) {
		*err = -1;
		return;
	}
	dst->value = READ_ONCE(sk->sk_rcvlowat);
}

META_COLLECTOR(int_sk_rcvtimeo)
{
	const struct sock *sk = skb_to_full_sk(skb);

	if (!sk) {
		*err = -1;
		return;
	}
	dst->value = sk->sk_rcvtimeo / HZ;
}

META_COLLECTOR(int_sk_sndtimeo)
{
	const struct sock *sk = skb_to_full_sk(skb);

	if (!sk) {
		*err = -1;
		return;
	}
	dst->value = sk->sk_sndtimeo / HZ;
}

META_COLLECTOR(int_sk_sendmsg_off)
{
	const struct sock *sk = skb_to_full_sk(skb);

	if (!sk) {
		*err = -1;
		return;
	}
	dst->value = sk->sk_frag.offset;
}

META_COLLECTOR(int_sk_write_pend)
{
	const struct sock *sk = skb_to_full_sk(skb);

	if (!sk) {
		*err = -1;
		return;
	}
	dst->value = sk->sk_write_pending;
}

/**************************************************************************
 * Meta value collectors assignment table
 **************************************************************************/

struct meta_ops {
	void		(*get)(struct sk_buff *, struct tcf_pkt_info *,
			       struct meta_value *, struct meta_obj *, int *);
};

#define META_ID(name) TCF_META_ID_##name
#define META_FUNC(name) { .get = meta_##name }

/* Meta value operations table listing all meta value collectors and
 * assigns them to a type and meta id. */
static struct meta_ops __meta_ops[TCF_META_TYPE_MAX + 1][TCF_META_ID_MAX + 1] = {
	[TCF_META_TYPE_VAR] = {
		[META_ID(DEV)]			= META_FUNC(var_dev),
		[META_ID(SK_BOUND_IF)] 		= META_FUNC(var_sk_bound_if),
	},
	[TCF_META_TYPE_INT] = {
		[META_ID(RANDOM)]		= META_FUNC(int_random),
		[META_ID(LOADAVG_0)]		= META_FUNC(int_loadavg_0),
		[META_ID(LOADAVG_1)]		= META_FUNC(int_loadavg_1),
		[META_ID(LOADAVG_2)]		= META_FUNC(int_loadavg_2),
		[META_ID(DEV)]			= META_FUNC(int_dev),
		[META_ID(PRIORITY)]		= META_FUNC(int_priority),
		[META_ID(PROTOCOL)]		= META_FUNC(int_protocol),
		[META_ID(PKTTYPE)]		= META_FUNC(int_pkttype),
		[META_ID(PKTLEN)]		= META_FUNC(int_pktlen),
		[META_ID(DATALEN)]		= META_FUNC(int_datalen),
		[META_ID(MACLEN)]		= META_FUNC(int_maclen),
		[META_ID(NFMARK)]		= META_FUNC(int_mark),
		[META_ID(TCINDEX)]		= META_FUNC(int_tcindex),
		[META_ID(RTCLASSID)]		= META_FUNC(int_rtclassid),
		[META_ID(RTIIF)]		= META_FUNC(int_rtiif),
		[META_ID(SK_FAMILY)]		= META_FUNC(int_sk_family),
		[META_ID(SK_STATE)]		= META_FUNC(int_sk_state),
		[META_ID(SK_REUSE)]		= META_FUNC(int_sk_reuse),
		[META_ID(SK_BOUND_IF)]		= META_FUNC(int_sk_bound_if),
		[META_ID(SK_REFCNT)]		= META_FUNC(int_sk_refcnt),
		[META_ID(SK_RCVBUF)]		= META_FUNC(int_sk_rcvbuf),
		[META_ID(SK_SNDBUF)]		= META_FUNC(int_sk_sndbuf),
		[META_ID(SK_SHUTDOWN)]		= META_FUNC(int_sk_shutdown),
		[META_ID(SK_PROTO)]		= META_FUNC(int_sk_proto),
		[META_ID(SK_TYPE)]		= META_FUNC(int_sk_type),
		[META_ID(SK_RMEM_ALLOC)]	= META_FUNC(int_sk_rmem_alloc),
		[META_ID(SK_WMEM_ALLOC)]	= META_FUNC(int_sk_wmem_alloc),
		[META_ID(SK_OMEM_ALLOC)]	= META_FUNC(int_sk_omem_alloc),
		[META_ID(SK_WMEM_QUEUED)]	= META_FUNC(int_sk_wmem_queued),
		[META_ID(SK_RCV_QLEN)]		= META_FUNC(int_sk_rcv_qlen),
		[META_ID(SK_SND_QLEN)]		= META_FUNC(int_sk_snd_qlen),
		[META_ID(SK_ERR_QLEN)]		= META_FUNC(int_sk_err_qlen),
		[META_ID(SK_FORWARD_ALLOCS)]	= META_FUNC(int_sk_fwd_alloc),
		[META_ID(SK_ALLOCS)]		= META_FUNC(int_sk_alloc),
		[META_ID(SK_HASH)]		= META_FUNC(int_sk_hash),
		[META_ID(SK_LINGERTIME)]	= META_FUNC(int_sk_lingertime),
		[META_ID(SK_ACK_BACKLOG)]	= META_FUNC(int_sk_ack_bl),
		[META_ID(SK_MAX_ACK_BACKLOG)]	= META_FUNC(int_sk_max_ack_bl),
		[META_ID(SK_PRIO)]		= META_FUNC(int_sk_prio),
		[META_ID(SK_RCVLOWAT)]		= META_FUNC(int_sk_rcvlowat),
		[META_ID(SK_RCVTIMEO)]		= META_FUNC(int_sk_rcvtimeo),
		[META_ID(SK_SNDTIMEO)]		= META_FUNC(int_sk_sndtimeo),
		[META_ID(SK_SENDMSG_OFF)]	= META_FUNC(int_sk_sendmsg_off),
		[META_ID(SK_WRITE_PENDING)]	= META_FUNC(int_sk_write_pend),
		[META_ID(VLAN_TAG)]		= META_FUNC(int_vlan_tag),
		[META_ID(RXHASH)]		= META_FUNC(int_rxhash),
	}
};

static inline struct meta_ops *meta_ops(struct meta_value *val)
{
	return &__meta_ops[meta_type(val)][meta_id(val)];
}

/**************************************************************************
 * Type specific operations for TCF_META_TYPE_VAR
 **************************************************************************/

static int meta_var_compare(struct meta_obj *a, struct meta_obj *b)
{
	int r = a->len - b->len;

	if (r == 0)
		r = memcmp((void *) a->value, (void *) b->value, a->len);

	return r;
}

static int meta_var_change(struct meta_value *dst, struct nlattr *nla)
{
	int len = nla_len(nla);

	dst->val = (unsigned long)kmemdup(nla_data(nla), len, GFP_KERNEL);
	if (dst->val == 0UL)
		return -ENOMEM;
	dst->len = len;
	return 0;
}

static void meta_var_destroy(struct meta_value *v)
{
	kfree((void *) v->val);
}

static void meta_var_apply_extras(struct meta_value *v,
				  struct meta_obj *dst)
{
	int shift = v->hdr.shift;

	if (shift && shift < dst->len)
		dst->len -= shift;
}

static int meta_var_dump(struct sk_buff *skb, struct meta_value *v, int tlv)
{
	if (v->val && v->len &&
	    nla_put(skb, tlv, v->len, (void *) v->val))
		goto nla_put_failure;
	return 0;

nla_put_failure:
	return -1;
}

/**************************************************************************
 * Type specific operations for TCF_META_TYPE_INT
 **************************************************************************/

static int meta_int_compare(struct meta_obj *a, struct meta_obj *b)
{
	/* Let gcc optimize it, the unlikely is not really based on
	 * some numbers but jump free code for mismatches seems
	 * more logical. */
	if (unlikely(a->value == b->value))
		return 0;
	else if (a->value < b->value)
		return -1;
	else
		return 1;
}

static int meta_int_change(struct meta_value *dst, struct nlattr *nla)
{
	if (nla_len(nla) >= sizeof(unsigned long)) {
		dst->val = *(unsigned long *) nla_data(nla);
		dst->len = sizeof(unsigned long);
	} else if (nla_len(nla) == sizeof(u32)) {
		dst->val = nla_get_u32(nla);
		dst->len = sizeof(u32);
	} else
		return -EINVAL;

	return 0;
}

static void meta_int_apply_extras(struct meta_value *v,
				  struct meta_obj *dst)
{
	if (v->hdr.shift)
		dst->value >>= v->hdr.shift;

	if (v->val)
		dst->value &= v->val;
}

static int meta_int_dump(struct sk_buff *skb, struct meta_value *v, int tlv)
{
	if (v->len == sizeof(unsigned long)) {
		if (nla_put(skb, tlv, sizeof(unsigned long), &v->val))
			goto nla_put_failure;
	} else if (v->len == sizeof(u32)) {
		if (nla_put_u32(skb, tlv, v->val))
			goto nla_put_failure;
	}

	return 0;

nla_put_failure:
	return -1;
}

/**************************************************************************
 * Type specific operations table
 **************************************************************************/

struct meta_type_ops {
	void	(*destroy)(struct meta_value *);
	int	(*compare)(struct meta_obj *, struct meta_obj *);
	int	(*change)(struct meta_value *, struct nlattr *);
	void	(*apply_extras)(struct meta_value *, struct meta_obj *);
	int	(*dump)(struct sk_buff *, struct meta_value *, int);
};

static const struct meta_type_ops __meta_type_ops[TCF_META_TYPE_MAX + 1] = {
	[TCF_META_TYPE_VAR] = {
		.destroy = meta_var_destroy,
		.compare = meta_var_compare,
		.change = meta_var_change,
		.apply_extras = meta_var_apply_extras,
		.dump = meta_var_dump
	},
	[TCF_META_TYPE_INT] = {
		.compare = meta_int_compare,
		.change = meta_int_change,
		.apply_extras = meta_int_apply_extras,
		.dump = meta_int_dump
	}
};

static inline const struct meta_type_ops *meta_type_ops(struct meta_value *v)
{
	return &__meta_type_ops[meta_type(v)];
}

/**************************************************************************
 * Core
 **************************************************************************/

static int meta_get(struct sk_buff *skb, struct tcf_pkt_info *info,
		    struct meta_value *v, struct meta_obj *dst)
{
	int err = 0;

	if (meta_id(v) == TCF_META_ID_VALUE) {
		dst->value = v->val;
		dst->len = v->len;
		return 0;
	}

	meta_ops(v)->get(skb, info, v, dst, &err);
	if (err < 0)
		return err;

	if (meta_type_ops(v)->apply_extras)
		meta_type_ops(v)->apply_extras(v, dst);

	return 0;
}

static int em_meta_match(struct sk_buff *skb, struct tcf_ematch *m,
			 struct tcf_pkt_info *info)
{
	int r;
	struct meta_match *meta = (struct meta_match *) m->data;
	struct meta_obj l_value, r_value;

	if (meta_get(skb, info, &meta->lvalue, &l_value) < 0 ||
	    meta_get(skb, info, &meta->rvalue, &r_value) < 0)
		return 0;

	r = meta_type_ops(&meta->lvalue)->compare(&l_value, &r_value);

	switch (meta->lvalue.hdr.op) {
	case TCF_EM_OPND_EQ:
		return !r;
	case TCF_EM_OPND_LT:
		return r < 0;
	case TCF_EM_OPND_GT:
		return r > 0;
	}

	return 0;
}

static void meta_delete(struct meta_match *meta)
{
	if (meta) {
		const struct meta_type_ops *ops = meta_type_ops(&meta->lvalue);

		if (ops && ops->destroy) {
			ops->destroy(&meta->lvalue);
			ops->destroy(&meta->rvalue);
		}
	}

	kfree(meta);
}

static inline int meta_change_data(struct meta_value *dst, struct nlattr *nla)
{
	if (nla) {
		if (nla_len(nla) == 0)
			return -EINVAL;

		return meta_type_ops(dst)->change(dst, nla);
	}

	return 0;
}

static inline int meta_is_supported(struct meta_value *val)
{
	return !meta_id(val) || meta_ops(val)->get;
}

static const struct nla_policy meta_policy[TCA_EM_META_MAX + 1] = {
	[TCA_EM_META_HDR]	= { .len = sizeof(struct tcf_meta_hdr) },
};

static int em_meta_change(struct net *net, void *data, int len,
			  struct tcf_ematch *m)
{
	int err;
	struct nlattr *tb[TCA_EM_META_MAX + 1];
	struct tcf_meta_hdr *hdr;
	struct meta_match *meta = NULL;

	err = nla_parse_deprecated(tb, TCA_EM_META_MAX, data, len,
				   meta_policy, NULL);
	if (err < 0)
		goto errout;

	err = -EINVAL;
	if (tb[TCA_EM_META_HDR] == NULL)
		goto errout;
	hdr = nla_data(tb[TCA_EM_META_HDR]);

	if (TCF_META_TYPE(hdr->left.kind) != TCF_META_TYPE(hdr->right.kind) ||
	    TCF_META_TYPE(hdr->left.kind) > TCF_META_TYPE_MAX ||
	    TCF_META_ID(hdr->left.kind) > TCF_META_ID_MAX ||
	    TCF_META_ID(hdr->right.kind) > TCF_META_ID_MAX)
		goto errout;

	meta = kzalloc(sizeof(*meta), GFP_KERNEL);
	if (meta == NULL) {
		err = -ENOMEM;
		goto errout;
	}

	memcpy(&meta->lvalue.hdr, &hdr->left, sizeof(hdr->left));
	memcpy(&meta->rvalue.hdr, &hdr->right, sizeof(hdr->right));

	if (!meta_is_supported(&meta->lvalue) ||
	    !meta_is_supported(&meta->rvalue)) {
		err = -EOPNOTSUPP;
		goto errout;
	}

	if (meta_change_data(&meta->lvalue, tb[TCA_EM_META_LVALUE]) < 0 ||
	    meta_change_data(&meta->rvalue, tb[TCA_EM_META_RVALUE]) < 0)
		goto errout;

	m->datalen = sizeof(*meta);
	m->data = (unsigned long) meta;

	err = 0;
errout:
	if (err && meta)
		meta_delete(meta);
	return err;
}

static void em_meta_destroy(struct tcf_ematch *m)
{
	if (m)
		meta_delete((struct meta_match *) m->data);
}

static int em_meta_dump(struct sk_buff *skb, struct tcf_ematch *em)
{
	struct meta_match *meta = (struct meta_match *) em->data;
	struct tcf_meta_hdr hdr;
	const struct meta_type_ops *ops;

	memset(&hdr, 0, sizeof(hdr));
	memcpy(&hdr.left, &meta->lvalue.hdr, sizeof(hdr.left));
	memcpy(&hdr.right, &meta->rvalue.hdr, sizeof(hdr.right));

	if (nla_put(skb, TCA_EM_META_HDR, sizeof(hdr), &hdr))
		goto nla_put_failure;

	ops = meta_type_ops(&meta->lvalue);
	if (ops->dump(skb, &meta->lvalue, TCA_EM_META_LVALUE) < 0 ||
	    ops->dump(skb, &meta->rvalue, TCA_EM_META_RVALUE) < 0)
		goto nla_put_failure;

	return 0;

nla_put_failure:
	return -1;
}

static struct tcf_ematch_ops em_meta_ops = {
	.kind	  = TCF_EM_META,
	.change	  = em_meta_change,
	.match	  = em_meta_match,
	.destroy  = em_meta_destroy,
	.dump	  = em_meta_dump,
	.owner	  = THIS_MODULE,
	.link	  = LIST_HEAD_INIT(em_meta_ops.link)
};

static int __init init_em_meta(void)
{
	return tcf_em_register(&em_meta_ops);
}

static void __exit exit_em_meta(void)
{
	tcf_em_unregister(&em_meta_ops);
}

MODULE_LICENSE("GPL");

module_init(init_em_meta);
module_exit(exit_em_meta);

MODULE_ALIAS_TCF_EMATCH(TCF_EM_META);
