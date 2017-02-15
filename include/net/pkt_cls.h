#ifndef __NET_PKT_CLS_H
#define __NET_PKT_CLS_H

#include <linux/pkt_cls.h>
#include <net/sch_generic.h>
#include <net/act_api.h>

/* Basic packet classifier frontend definitions. */

struct tcf_walker {
	int	stop;
	int	skip;
	int	count;
	int	(*fn)(struct tcf_proto *, unsigned long node, struct tcf_walker *);
};

int register_tcf_proto_ops(struct tcf_proto_ops *ops);
int unregister_tcf_proto_ops(struct tcf_proto_ops *ops);

#ifdef CONFIG_NET_CLS
void tcf_destroy_chain(struct tcf_proto __rcu **fl);
#else
static inline void tcf_destroy_chain(struct tcf_proto __rcu **fl)
{
}
#endif

static inline unsigned long
__cls_set_class(unsigned long *clp, unsigned long cl)
{
	return xchg(clp, cl);
}

static inline unsigned long
cls_set_class(struct tcf_proto *tp, unsigned long *clp, 
	unsigned long cl)
{
	unsigned long old_cl;
	
	tcf_tree_lock(tp);
	old_cl = __cls_set_class(clp, cl);
	tcf_tree_unlock(tp);
 
	return old_cl;
}

static inline void
tcf_bind_filter(struct tcf_proto *tp, struct tcf_result *r, unsigned long base)
{
	unsigned long cl;

	cl = tp->q->ops->cl_ops->bind_tcf(tp->q, base, r->classid);
	cl = cls_set_class(tp, &r->class, cl);
	if (cl)
		tp->q->ops->cl_ops->unbind_tcf(tp->q, cl);
}

static inline void
tcf_unbind_filter(struct tcf_proto *tp, struct tcf_result *r)
{
	unsigned long cl;

	if ((cl = __cls_set_class(&r->class, 0)) != 0)
		tp->q->ops->cl_ops->unbind_tcf(tp->q, cl);
}

struct tcf_exts {
#ifdef CONFIG_NET_CLS_ACT
	__u32	type; /* for backward compat(TCA_OLD_COMPAT) */
	int nr_actions;
	struct tc_action **actions;
#endif
	/* Map to export classifier specific extension TLV types to the
	 * generic extensions API. Unsupported extensions must be set to 0.
	 */
	int action;
	int police;
};

static inline int tcf_exts_init(struct tcf_exts *exts, int action, int police)
{
#ifdef CONFIG_NET_CLS_ACT
	exts->type = 0;
	exts->nr_actions = 0;
	exts->actions = kcalloc(TCA_ACT_MAX_PRIO, sizeof(struct tc_action *),
				GFP_KERNEL);
	if (!exts->actions)
		return -ENOMEM;
#endif
	exts->action = action;
	exts->police = police;
	return 0;
}

/**
 * tcf_exts_is_predicative - check if a predicative extension is present
 * @exts: tc filter extensions handle
 *
 * Returns 1 if a predicative extension is present, i.e. an extension which
 * might cause further actions and thus overrule the regular tcf_result.
 */
static inline int
tcf_exts_is_predicative(struct tcf_exts *exts)
{
#ifdef CONFIG_NET_CLS_ACT
	return exts->nr_actions;
#else
	return 0;
#endif
}

/**
 * tcf_exts_is_available - check if at least one extension is present
 * @exts: tc filter extensions handle
 *
 * Returns 1 if at least one extension is present.
 */
static inline int
tcf_exts_is_available(struct tcf_exts *exts)
{
	/* All non-predicative extensions must be added here. */
	return tcf_exts_is_predicative(exts);
}

static inline void tcf_exts_to_list(const struct tcf_exts *exts,
				    struct list_head *actions)
{
#ifdef CONFIG_NET_CLS_ACT
	int i;

	for (i = 0; i < exts->nr_actions; i++) {
		struct tc_action *a = exts->actions[i];

		list_add_tail(&a->list, actions);
	}
#endif
}

/**
 * tcf_exts_exec - execute tc filter extensions
 * @skb: socket buffer
 * @exts: tc filter extensions handle
 * @res: desired result
 *
 * Executes all configured extensions. Returns 0 on a normal execution,
 * a negative number if the filter must be considered unmatched or
 * a positive action code (TC_ACT_*) which must be returned to the
 * underlying layer.
 */
static inline int
tcf_exts_exec(struct sk_buff *skb, struct tcf_exts *exts,
	       struct tcf_result *res)
{
#ifdef CONFIG_NET_CLS_ACT
	if (exts->nr_actions)
		return tcf_action_exec(skb, exts->actions, exts->nr_actions,
				       res);
#endif
	return 0;
}

#ifdef CONFIG_NET_CLS_ACT

#define tc_no_actions(_exts)  ((_exts)->nr_actions == 0)
#define tc_single_action(_exts) ((_exts)->nr_actions == 1)

#else /* CONFIG_NET_CLS_ACT */

#define tc_no_actions(_exts) true
#define tc_single_action(_exts) false

#endif /* CONFIG_NET_CLS_ACT */

int tcf_exts_validate(struct net *net, struct tcf_proto *tp,
		      struct nlattr **tb, struct nlattr *rate_tlv,
		      struct tcf_exts *exts, bool ovr);
void tcf_exts_destroy(struct tcf_exts *exts);
void tcf_exts_change(struct tcf_proto *tp, struct tcf_exts *dst,
		     struct tcf_exts *src);
int tcf_exts_dump(struct sk_buff *skb, struct tcf_exts *exts);
int tcf_exts_dump_stats(struct sk_buff *skb, struct tcf_exts *exts);
int tcf_exts_get_dev(struct net_device *dev, struct tcf_exts *exts,
		     struct net_device **hw_dev);

/**
 * struct tcf_pkt_info - packet information
 */
struct tcf_pkt_info {
	unsigned char *		ptr;
	int			nexthdr;
};

#ifdef CONFIG_NET_EMATCH

struct tcf_ematch_ops;

/**
 * struct tcf_ematch - extended match (ematch)
 * 
 * @matchid: identifier to allow userspace to reidentify a match
 * @flags: flags specifying attributes and the relation to other matches
 * @ops: the operations lookup table of the corresponding ematch module
 * @datalen: length of the ematch specific configuration data
 * @data: ematch specific data
 */
struct tcf_ematch {
	struct tcf_ematch_ops * ops;
	unsigned long		data;
	unsigned int		datalen;
	u16			matchid;
	u16			flags;
	struct net		*net;
};

static inline int tcf_em_is_container(struct tcf_ematch *em)
{
	return !em->ops;
}

static inline int tcf_em_is_simple(struct tcf_ematch *em)
{
	return em->flags & TCF_EM_SIMPLE;
}

static inline int tcf_em_is_inverted(struct tcf_ematch *em)
{
	return em->flags & TCF_EM_INVERT;
}

static inline int tcf_em_last_match(struct tcf_ematch *em)
{
	return (em->flags & TCF_EM_REL_MASK) == TCF_EM_REL_END;
}

static inline int tcf_em_early_end(struct tcf_ematch *em, int result)
{
	if (tcf_em_last_match(em))
		return 1;

	if (result == 0 && em->flags & TCF_EM_REL_AND)
		return 1;

	if (result != 0 && em->flags & TCF_EM_REL_OR)
		return 1;

	return 0;
}
	
/**
 * struct tcf_ematch_tree - ematch tree handle
 *
 * @hdr: ematch tree header supplied by userspace
 * @matches: array of ematches
 */
struct tcf_ematch_tree {
	struct tcf_ematch_tree_hdr hdr;
	struct tcf_ematch *	matches;
	
};

/**
 * struct tcf_ematch_ops - ematch module operations
 * 
 * @kind: identifier (kind) of this ematch module
 * @datalen: length of expected configuration data (optional)
 * @change: called during validation (optional)
 * @match: called during ematch tree evaluation, must return 1/0
 * @destroy: called during destroyage (optional)
 * @dump: called during dumping process (optional)
 * @owner: owner, must be set to THIS_MODULE
 * @link: link to previous/next ematch module (internal use)
 */
struct tcf_ematch_ops {
	int			kind;
	int			datalen;
	int			(*change)(struct net *net, void *,
					  int, struct tcf_ematch *);
	int			(*match)(struct sk_buff *, struct tcf_ematch *,
					 struct tcf_pkt_info *);
	void			(*destroy)(struct tcf_ematch *);
	int			(*dump)(struct sk_buff *, struct tcf_ematch *);
	struct module		*owner;
	struct list_head	link;
};

int tcf_em_register(struct tcf_ematch_ops *);
void tcf_em_unregister(struct tcf_ematch_ops *);
int tcf_em_tree_validate(struct tcf_proto *, struct nlattr *,
			 struct tcf_ematch_tree *);
void tcf_em_tree_destroy(struct tcf_ematch_tree *);
int tcf_em_tree_dump(struct sk_buff *, struct tcf_ematch_tree *, int);
int __tcf_em_tree_match(struct sk_buff *, struct tcf_ematch_tree *,
			struct tcf_pkt_info *);

/**
 * tcf_em_tree_change - replace ematch tree of a running classifier
 *
 * @tp: classifier kind handle
 * @dst: destination ematch tree variable
 * @src: source ematch tree (temporary tree from tcf_em_tree_validate)
 *
 * This functions replaces the ematch tree in @dst with the ematch
 * tree in @src. The classifier in charge of the ematch tree may be
 * running.
 */
static inline void tcf_em_tree_change(struct tcf_proto *tp,
				      struct tcf_ematch_tree *dst,
				      struct tcf_ematch_tree *src)
{
	tcf_tree_lock(tp);
	memcpy(dst, src, sizeof(*dst));
	tcf_tree_unlock(tp);
}

/**
 * tcf_em_tree_match - evaulate an ematch tree
 *
 * @skb: socket buffer of the packet in question
 * @tree: ematch tree to be used for evaluation
 * @info: packet information examined by classifier
 *
 * This function matches @skb against the ematch tree in @tree by going
 * through all ematches respecting their logic relations returning
 * as soon as the result is obvious.
 *
 * Returns 1 if the ematch tree as-one matches, no ematches are configured
 * or ematch is not enabled in the kernel, otherwise 0 is returned.
 */
static inline int tcf_em_tree_match(struct sk_buff *skb,
				    struct tcf_ematch_tree *tree,
				    struct tcf_pkt_info *info)
{
	if (tree->hdr.nmatches)
		return __tcf_em_tree_match(skb, tree, info);
	else
		return 1;
}

#define MODULE_ALIAS_TCF_EMATCH(kind)	MODULE_ALIAS("ematch-kind-" __stringify(kind))

#else /* CONFIG_NET_EMATCH */

struct tcf_ematch_tree {
};

#define tcf_em_tree_validate(tp, tb, t) ((void)(t), 0)
#define tcf_em_tree_destroy(t) do { (void)(t); } while(0)
#define tcf_em_tree_dump(skb, t, tlv) (0)
#define tcf_em_tree_change(tp, dst, src) do { } while(0)
#define tcf_em_tree_match(skb, t, info) ((void)(info), 1)

#endif /* CONFIG_NET_EMATCH */

static inline unsigned char * tcf_get_base_ptr(struct sk_buff *skb, int layer)
{
	switch (layer) {
		case TCF_LAYER_LINK:
			return skb->data;
		case TCF_LAYER_NETWORK:
			return skb_network_header(skb);
		case TCF_LAYER_TRANSPORT:
			return skb_transport_header(skb);
	}

	return NULL;
}

static inline int tcf_valid_offset(const struct sk_buff *skb,
				   const unsigned char *ptr, const int len)
{
	return likely((ptr + len) <= skb_tail_pointer(skb) &&
		      ptr >= skb->head &&
		      (ptr <= (ptr + len)));
}

#ifdef CONFIG_NET_CLS_IND
#include <net/net_namespace.h>

static inline int
tcf_change_indev(struct net *net, struct nlattr *indev_tlv)
{
	char indev[IFNAMSIZ];
	struct net_device *dev;

	if (nla_strlcpy(indev, indev_tlv, IFNAMSIZ) >= IFNAMSIZ)
		return -EINVAL;
	dev = __dev_get_by_name(net, indev);
	if (!dev)
		return -ENODEV;
	return dev->ifindex;
}

static inline bool
tcf_match_indev(struct sk_buff *skb, int ifindex)
{
	if (!ifindex)
		return true;
	if  (!skb->skb_iif)
		return false;
	return ifindex == skb->skb_iif;
}
#endif /* CONFIG_NET_CLS_IND */

struct tc_cls_u32_knode {
	struct tcf_exts *exts;
	struct tc_u32_sel *sel;
	u32 handle;
	u32 val;
	u32 mask;
	u32 link_handle;
	u8 fshift;
};

struct tc_cls_u32_hnode {
	u32 handle;
	u32 prio;
	unsigned int divisor;
};

enum tc_clsu32_command {
	TC_CLSU32_NEW_KNODE,
	TC_CLSU32_REPLACE_KNODE,
	TC_CLSU32_DELETE_KNODE,
	TC_CLSU32_NEW_HNODE,
	TC_CLSU32_REPLACE_HNODE,
	TC_CLSU32_DELETE_HNODE,
};

struct tc_cls_u32_offload {
	/* knode values */
	enum tc_clsu32_command command;
	union {
		struct tc_cls_u32_knode knode;
		struct tc_cls_u32_hnode hnode;
	};
};

static inline bool tc_can_offload(const struct net_device *dev,
				  const struct tcf_proto *tp)
{
	const struct Qdisc *sch = tp->q;
	const struct Qdisc_class_ops *cops = sch->ops->cl_ops;

	if (!(dev->features & NETIF_F_HW_TC))
		return false;
	if (!dev->netdev_ops->ndo_setup_tc)
		return false;
	if (cops && cops->tcf_cl_offload)
		return cops->tcf_cl_offload(tp->classid);

	return true;
}

static inline bool tc_skip_hw(u32 flags)
{
	return (flags & TCA_CLS_FLAGS_SKIP_HW) ? true : false;
}

static inline bool tc_should_offload(const struct net_device *dev,
				     const struct tcf_proto *tp, u32 flags)
{
	if (tc_skip_hw(flags))
		return false;
	return tc_can_offload(dev, tp);
}

static inline bool tc_skip_sw(u32 flags)
{
	return (flags & TCA_CLS_FLAGS_SKIP_SW) ? true : false;
}

/* SKIP_HW and SKIP_SW are mutually exclusive flags. */
static inline bool tc_flags_valid(u32 flags)
{
	if (flags & ~(TCA_CLS_FLAGS_SKIP_HW | TCA_CLS_FLAGS_SKIP_SW))
		return false;

	if (!(flags ^ (TCA_CLS_FLAGS_SKIP_HW | TCA_CLS_FLAGS_SKIP_SW)))
		return false;

	return true;
}

enum tc_fl_command {
	TC_CLSFLOWER_REPLACE,
	TC_CLSFLOWER_DESTROY,
	TC_CLSFLOWER_STATS,
};

struct tc_cls_flower_offload {
	enum tc_fl_command command;
	u32 prio;
	unsigned long cookie;
	struct flow_dissector *dissector;
	struct fl_flow_key *mask;
	struct fl_flow_key *key;
	struct tcf_exts *exts;
};

enum tc_matchall_command {
	TC_CLSMATCHALL_REPLACE,
	TC_CLSMATCHALL_DESTROY,
};

struct tc_cls_matchall_offload {
	enum tc_matchall_command command;
	struct tcf_exts *exts;
	unsigned long cookie;
};

enum tc_clsbpf_command {
	TC_CLSBPF_ADD,
	TC_CLSBPF_REPLACE,
	TC_CLSBPF_DESTROY,
	TC_CLSBPF_STATS,
};

struct tc_cls_bpf_offload {
	enum tc_clsbpf_command command;
	struct tcf_exts *exts;
	struct bpf_prog *prog;
	const char *name;
	bool exts_integrated;
	u32 gen_flags;
};


/* This structure holds cookie structure that is passed from user
 * to the kernel for actions and classifiers
 */
struct tc_cookie {
	u8  *data;
	u32 len;
};
#endif
