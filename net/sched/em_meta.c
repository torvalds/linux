/*
 * net/sched/em_meta.c	Metadata ematch
 *
 *		This program is free software; you can redistribute it and/or
 *		modify it under the terms of the GNU General Public License
 *		as published by the Free Software Foundation; either version
 *		2 of the License, or (at your option) any later version.
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
 * 	 def  | id: INDEV |                           | id: VALUE |
 * 	      | data:     |                           | data: 3   |
 * 	      +-----------+                           +-----------+
 * 	            |                                       |
 * 	            ---> meta_ops[INT][INDEV](...)          |
 *                            |                            |
 * 	            -----------                             |
 * 	            V                                       V
 * 	      +-----------+                           +-----------+
 * 	      | type: INT |                           | type: INT |
 * 	 obj  | id: INDEV |                           | id: VALUE |
 * 	      | data: 2   |<--data got filled out     | data: 3   |
 * 	      +-----------+                           +-----------+
 * 	            |                                         |
 * 	            --------------> 2  equals 3 <--------------
 *
 * 	This is a simplified schema, the complexity varies depending
 * 	on the meta type. Obviously, the length of the data must also
 * 	be provided for non-numeric types.
 *
 * 	Additionaly, type dependant modifiers such as shift operators
 * 	or mask may be applied to extend the functionaliy. As of now,
 * 	the variable length type supports shifting the byte string to
 * 	the right, eating up any number of octets and thus supporting
 * 	wildcard interface name comparisons such as "ppp%" matching
 * 	ppp0..9.
 *
 * 	NOTE: Certain meta values depend on other subsystems and are
 * 	      only available if that subsytem is enabled in the kernel.
 */

#include <linux/config.h>
#include <linux/module.h>
#include <linux/types.h>
#include <linux/kernel.h>
#include <linux/sched.h>
#include <linux/string.h>
#include <linux/skbuff.h>
#include <linux/random.h>
#include <linux/tc_ematch/tc_em_meta.h>
#include <net/dst.h>
#include <net/route.h>
#include <net/pkt_cls.h>

struct meta_obj
{
	unsigned long		value;
	unsigned int		len;
};

struct meta_value
{
	struct tcf_meta_val	hdr;
	unsigned long		val;
	unsigned int		len;
};

struct meta_match
{
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

META_COLLECTOR(int_indev)
{
	*err = int_dev(skb->input_dev, dst);
}

META_COLLECTOR(var_indev)
{
	*err = var_dev(skb->input_dev, dst);
}

META_COLLECTOR(int_realdev)
{
	*err = int_dev(skb->real_dev, dst);
}

META_COLLECTOR(var_realdev)
{
	*err = var_dev(skb->real_dev, dst);
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
	dst->value = skb->protocol;
}

META_COLLECTOR(int_security)
{
	dst->value = skb->security;
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

/**************************************************************************
 * Netfilter
 **************************************************************************/

#ifdef CONFIG_NETFILTER
META_COLLECTOR(int_nfmark)
{
	dst->value = skb->nfmark;
}
#endif

/**************************************************************************
 * Traffic Control
 **************************************************************************/

META_COLLECTOR(int_tcindex)
{
	dst->value = skb->tc_index;
}

#ifdef CONFIG_NET_CLS_ACT
META_COLLECTOR(int_tcverd)
{
	dst->value = skb->tc_verd;
}

META_COLLECTOR(int_tcclassid)
{
	dst->value = skb->tc_classid;
}
#endif

/**************************************************************************
 * Routing
 **************************************************************************/

#ifdef CONFIG_NET_CLS_ROUTE
META_COLLECTOR(int_rtclassid)
{
	if (unlikely(skb->dst == NULL))
		*err = -1;
	else
		dst->value = skb->dst->tclassid;
}
#endif

META_COLLECTOR(int_rtiif)
{
	if (unlikely(skb->dst == NULL))
		*err = -1;
	else
		dst->value = ((struct rtable*) skb->dst)->fl.iif;
}

/**************************************************************************
 * Meta value collectors assignment table
 **************************************************************************/

struct meta_ops
{
	void		(*get)(struct sk_buff *, struct tcf_pkt_info *,
			       struct meta_value *, struct meta_obj *, int *);
};

/* Meta value operations table listing all meta value collectors and
 * assigns them to a type and meta id. */
static struct meta_ops __meta_ops[TCF_META_TYPE_MAX+1][TCF_META_ID_MAX+1] = {
	[TCF_META_TYPE_VAR] = {
		[TCF_META_ID_DEV]	= { .get = meta_var_dev },
		[TCF_META_ID_INDEV]	= { .get = meta_var_indev },
		[TCF_META_ID_REALDEV]	= { .get = meta_var_realdev }
	},
	[TCF_META_TYPE_INT] = {
		[TCF_META_ID_RANDOM]	= { .get = meta_int_random },
		[TCF_META_ID_LOADAVG_0]	= { .get = meta_int_loadavg_0 },
		[TCF_META_ID_LOADAVG_1]	= { .get = meta_int_loadavg_1 },
		[TCF_META_ID_LOADAVG_2]	= { .get = meta_int_loadavg_2 },
		[TCF_META_ID_DEV]	= { .get = meta_int_dev },
		[TCF_META_ID_INDEV]	= { .get = meta_int_indev },
		[TCF_META_ID_REALDEV]	= { .get = meta_int_realdev },
		[TCF_META_ID_PRIORITY]	= { .get = meta_int_priority },
		[TCF_META_ID_PROTOCOL]	= { .get = meta_int_protocol },
		[TCF_META_ID_SECURITY]	= { .get = meta_int_security },
		[TCF_META_ID_PKTTYPE]	= { .get = meta_int_pkttype },
		[TCF_META_ID_PKTLEN]	= { .get = meta_int_pktlen },
		[TCF_META_ID_DATALEN]	= { .get = meta_int_datalen },
		[TCF_META_ID_MACLEN]	= { .get = meta_int_maclen },
#ifdef CONFIG_NETFILTER
		[TCF_META_ID_NFMARK]	= { .get = meta_int_nfmark },
#endif
		[TCF_META_ID_TCINDEX]	= { .get = meta_int_tcindex },
#ifdef CONFIG_NET_CLS_ACT
		[TCF_META_ID_TCVERDICT]	= { .get = meta_int_tcverd },
		[TCF_META_ID_TCCLASSID]	= { .get = meta_int_tcclassid },
#endif
#ifdef CONFIG_NET_CLS_ROUTE
		[TCF_META_ID_RTCLASSID]	= { .get = meta_int_rtclassid },
#endif
		[TCF_META_ID_RTIIF]	= { .get = meta_int_rtiif }
	}
};

static inline struct meta_ops * meta_ops(struct meta_value *val)
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

static int meta_var_change(struct meta_value *dst, struct rtattr *rta)
{
	int len = RTA_PAYLOAD(rta);

	dst->val = (unsigned long) kmalloc(len, GFP_KERNEL);
	if (dst->val == 0UL)
		return -ENOMEM;
	memcpy((void *) dst->val, RTA_DATA(rta), len);
	dst->len = len;
	return 0;
}

static void meta_var_destroy(struct meta_value *v)
{
	if (v->val)
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
	if (v->val && v->len)
		RTA_PUT(skb, tlv, v->len, (void *) v->val);
	return 0;

rtattr_failure:
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
	if (unlikely(a == b))
		return 0;
	else if (a < b)
		return -1;
	else
		return 1;
}

static int meta_int_change(struct meta_value *dst, struct rtattr *rta)
{
	if (RTA_PAYLOAD(rta) >= sizeof(unsigned long)) {
		dst->val = *(unsigned long *) RTA_DATA(rta);
		dst->len = sizeof(unsigned long);
	} else if (RTA_PAYLOAD(rta) == sizeof(u32)) {
		dst->val = *(u32 *) RTA_DATA(rta);
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
	if (v->len == sizeof(unsigned long))
		RTA_PUT(skb, tlv, sizeof(unsigned long), &v->val);
	else if (v->len == sizeof(u32)) {
		u32 d = v->val;
		RTA_PUT(skb, tlv, sizeof(d), &d);
	}

	return 0;

rtattr_failure:
	return -1;
}

/**************************************************************************
 * Type specific operations table
 **************************************************************************/

struct meta_type_ops
{
	void	(*destroy)(struct meta_value *);
	int	(*compare)(struct meta_obj *, struct meta_obj *);
	int	(*change)(struct meta_value *, struct rtattr *);
	void	(*apply_extras)(struct meta_value *, struct meta_obj *);
	int	(*dump)(struct sk_buff *, struct meta_value *, int);
};

static struct meta_type_ops __meta_type_ops[TCF_META_TYPE_MAX+1] = {
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

static inline struct meta_type_ops * meta_type_ops(struct meta_value *v)
{
	return &__meta_type_ops[meta_type(v)];
}

/**************************************************************************
 * Core
 **************************************************************************/

static inline int meta_get(struct sk_buff *skb, struct tcf_pkt_info *info, 
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

static inline void meta_delete(struct meta_match *meta)
{
	struct meta_type_ops *ops = meta_type_ops(&meta->lvalue);

	if (ops && ops->destroy) {
		ops->destroy(&meta->lvalue);
		ops->destroy(&meta->rvalue);
	}

	kfree(meta);
}

static inline int meta_change_data(struct meta_value *dst, struct rtattr *rta)
{
	if (rta) {
		if (RTA_PAYLOAD(rta) == 0)
			return -EINVAL;

		return meta_type_ops(dst)->change(dst, rta);
	}

	return 0;
}

static inline int meta_is_supported(struct meta_value *val)
{
	return (!meta_id(val) || meta_ops(val)->get);
}

static int em_meta_change(struct tcf_proto *tp, void *data, int len,
			  struct tcf_ematch *m)
{
	int err = -EINVAL;
	struct rtattr *tb[TCA_EM_META_MAX];
	struct tcf_meta_hdr *hdr;
	struct meta_match *meta = NULL;
	
	if (rtattr_parse(tb, TCA_EM_META_MAX, data, len) < 0)
		goto errout;

	if (tb[TCA_EM_META_HDR-1] == NULL ||
	    RTA_PAYLOAD(tb[TCA_EM_META_HDR-1]) < sizeof(*hdr))
		goto errout;
	hdr = RTA_DATA(tb[TCA_EM_META_HDR-1]);

	if (TCF_META_TYPE(hdr->left.kind) != TCF_META_TYPE(hdr->right.kind) ||
	    TCF_META_TYPE(hdr->left.kind) > TCF_META_TYPE_MAX ||
	    TCF_META_ID(hdr->left.kind) > TCF_META_ID_MAX ||
	    TCF_META_ID(hdr->right.kind) > TCF_META_ID_MAX)
		goto errout;

	meta = kmalloc(sizeof(*meta), GFP_KERNEL);
	if (meta == NULL)
		goto errout;
	memset(meta, 0, sizeof(*meta));

	memcpy(&meta->lvalue.hdr, &hdr->left, sizeof(hdr->left));
	memcpy(&meta->rvalue.hdr, &hdr->right, sizeof(hdr->right));

	if (!meta_is_supported(&meta->lvalue) ||
	    !meta_is_supported(&meta->rvalue)) {
		err = -EOPNOTSUPP;
		goto errout;
	}

	if (meta_change_data(&meta->lvalue, tb[TCA_EM_META_LVALUE-1]) < 0 ||
	    meta_change_data(&meta->rvalue, tb[TCA_EM_META_RVALUE-1]) < 0)
		goto errout;

	m->datalen = sizeof(*meta);
	m->data = (unsigned long) meta;

	err = 0;
errout:
	if (err && meta)
		meta_delete(meta);
	return err;
}

static void em_meta_destroy(struct tcf_proto *tp, struct tcf_ematch *m)
{
	if (m)
		meta_delete((struct meta_match *) m->data);
}

static int em_meta_dump(struct sk_buff *skb, struct tcf_ematch *em)
{
	struct meta_match *meta = (struct meta_match *) em->data;
	struct tcf_meta_hdr hdr;
	struct meta_type_ops *ops;

	memset(&hdr, 0, sizeof(hdr));
	memcpy(&hdr.left, &meta->lvalue.hdr, sizeof(hdr.left));
	memcpy(&hdr.right, &meta->rvalue.hdr, sizeof(hdr.right));

	RTA_PUT(skb, TCA_EM_META_HDR, sizeof(hdr), &hdr);

	ops = meta_type_ops(&meta->lvalue);
	if (ops->dump(skb, &meta->lvalue, TCA_EM_META_LVALUE) < 0 ||
	    ops->dump(skb, &meta->rvalue, TCA_EM_META_RVALUE) < 0)
		goto rtattr_failure;

	return 0;

rtattr_failure:
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
