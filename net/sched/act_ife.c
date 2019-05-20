/*
 * net/sched/ife.c	Inter-FE action based on ForCES WG InterFE LFB
 *
 *		Refer to:
 *		draft-ietf-forces-interfelfb-03
 *		and
 *		netdev01 paper:
 *		"Distributing Linux Traffic Control Classifier-Action
 *		Subsystem"
 *		Authors: Jamal Hadi Salim and Damascene M. Joachimpillai
 *
 *		This program is free software; you can redistribute it and/or
 *		modify it under the terms of the GNU General Public License
 *		as published by the Free Software Foundation; either version
 *		2 of the License, or (at your option) any later version.
 *
 * copyright Jamal Hadi Salim (2015)
 *
*/

#include <linux/types.h>
#include <linux/kernel.h>
#include <linux/string.h>
#include <linux/errno.h>
#include <linux/skbuff.h>
#include <linux/rtnetlink.h>
#include <linux/module.h>
#include <linux/init.h>
#include <net/net_namespace.h>
#include <net/netlink.h>
#include <net/pkt_sched.h>
#include <net/pkt_cls.h>
#include <uapi/linux/tc_act/tc_ife.h>
#include <net/tc_act/tc_ife.h>
#include <linux/etherdevice.h>
#include <net/ife.h>

static unsigned int ife_net_id;
static int max_metacnt = IFE_META_MAX + 1;
static struct tc_action_ops act_ife_ops;

static const struct nla_policy ife_policy[TCA_IFE_MAX + 1] = {
	[TCA_IFE_PARMS] = { .len = sizeof(struct tc_ife)},
	[TCA_IFE_DMAC] = { .len = ETH_ALEN},
	[TCA_IFE_SMAC] = { .len = ETH_ALEN},
	[TCA_IFE_TYPE] = { .type = NLA_U16},
};

int ife_encode_meta_u16(u16 metaval, void *skbdata, struct tcf_meta_info *mi)
{
	u16 edata = 0;

	if (mi->metaval)
		edata = *(u16 *)mi->metaval;
	else if (metaval)
		edata = metaval;

	if (!edata) /* will not encode */
		return 0;

	edata = htons(edata);
	return ife_tlv_meta_encode(skbdata, mi->metaid, 2, &edata);
}
EXPORT_SYMBOL_GPL(ife_encode_meta_u16);

int ife_get_meta_u32(struct sk_buff *skb, struct tcf_meta_info *mi)
{
	if (mi->metaval)
		return nla_put_u32(skb, mi->metaid, *(u32 *)mi->metaval);
	else
		return nla_put(skb, mi->metaid, 0, NULL);
}
EXPORT_SYMBOL_GPL(ife_get_meta_u32);

int ife_check_meta_u32(u32 metaval, struct tcf_meta_info *mi)
{
	if (metaval || mi->metaval)
		return 8; /* T+L+V == 2+2+4 */

	return 0;
}
EXPORT_SYMBOL_GPL(ife_check_meta_u32);

int ife_check_meta_u16(u16 metaval, struct tcf_meta_info *mi)
{
	if (metaval || mi->metaval)
		return 8; /* T+L+(V) == 2+2+(2+2bytepad) */

	return 0;
}
EXPORT_SYMBOL_GPL(ife_check_meta_u16);

int ife_encode_meta_u32(u32 metaval, void *skbdata, struct tcf_meta_info *mi)
{
	u32 edata = metaval;

	if (mi->metaval)
		edata = *(u32 *)mi->metaval;
	else if (metaval)
		edata = metaval;

	if (!edata) /* will not encode */
		return 0;

	edata = htonl(edata);
	return ife_tlv_meta_encode(skbdata, mi->metaid, 4, &edata);
}
EXPORT_SYMBOL_GPL(ife_encode_meta_u32);

int ife_get_meta_u16(struct sk_buff *skb, struct tcf_meta_info *mi)
{
	if (mi->metaval)
		return nla_put_u16(skb, mi->metaid, *(u16 *)mi->metaval);
	else
		return nla_put(skb, mi->metaid, 0, NULL);
}
EXPORT_SYMBOL_GPL(ife_get_meta_u16);

int ife_alloc_meta_u32(struct tcf_meta_info *mi, void *metaval, gfp_t gfp)
{
	mi->metaval = kmemdup(metaval, sizeof(u32), gfp);
	if (!mi->metaval)
		return -ENOMEM;

	return 0;
}
EXPORT_SYMBOL_GPL(ife_alloc_meta_u32);

int ife_alloc_meta_u16(struct tcf_meta_info *mi, void *metaval, gfp_t gfp)
{
	mi->metaval = kmemdup(metaval, sizeof(u16), gfp);
	if (!mi->metaval)
		return -ENOMEM;

	return 0;
}
EXPORT_SYMBOL_GPL(ife_alloc_meta_u16);

void ife_release_meta_gen(struct tcf_meta_info *mi)
{
	kfree(mi->metaval);
}
EXPORT_SYMBOL_GPL(ife_release_meta_gen);

int ife_validate_meta_u32(void *val, int len)
{
	if (len == sizeof(u32))
		return 0;

	return -EINVAL;
}
EXPORT_SYMBOL_GPL(ife_validate_meta_u32);

int ife_validate_meta_u16(void *val, int len)
{
	/* length will not include padding */
	if (len == sizeof(u16))
		return 0;

	return -EINVAL;
}
EXPORT_SYMBOL_GPL(ife_validate_meta_u16);

static LIST_HEAD(ifeoplist);
static DEFINE_RWLOCK(ife_mod_lock);

static struct tcf_meta_ops *find_ife_oplist(u16 metaid)
{
	struct tcf_meta_ops *o;

	read_lock(&ife_mod_lock);
	list_for_each_entry(o, &ifeoplist, list) {
		if (o->metaid == metaid) {
			if (!try_module_get(o->owner))
				o = NULL;
			read_unlock(&ife_mod_lock);
			return o;
		}
	}
	read_unlock(&ife_mod_lock);

	return NULL;
}

int register_ife_op(struct tcf_meta_ops *mops)
{
	struct tcf_meta_ops *m;

	if (!mops->metaid || !mops->metatype || !mops->name ||
	    !mops->check_presence || !mops->encode || !mops->decode ||
	    !mops->get || !mops->alloc)
		return -EINVAL;

	write_lock(&ife_mod_lock);

	list_for_each_entry(m, &ifeoplist, list) {
		if (m->metaid == mops->metaid ||
		    (strcmp(mops->name, m->name) == 0)) {
			write_unlock(&ife_mod_lock);
			return -EEXIST;
		}
	}

	if (!mops->release)
		mops->release = ife_release_meta_gen;

	list_add_tail(&mops->list, &ifeoplist);
	write_unlock(&ife_mod_lock);
	return 0;
}
EXPORT_SYMBOL_GPL(unregister_ife_op);

int unregister_ife_op(struct tcf_meta_ops *mops)
{
	struct tcf_meta_ops *m;
	int err = -ENOENT;

	write_lock(&ife_mod_lock);
	list_for_each_entry(m, &ifeoplist, list) {
		if (m->metaid == mops->metaid) {
			list_del(&mops->list);
			err = 0;
			break;
		}
	}
	write_unlock(&ife_mod_lock);

	return err;
}
EXPORT_SYMBOL_GPL(register_ife_op);

static int ife_validate_metatype(struct tcf_meta_ops *ops, void *val, int len)
{
	int ret = 0;
	/* XXX: unfortunately cant use nla_policy at this point
	* because a length of 0 is valid in the case of
	* "allow". "use" semantics do enforce for proper
	* length and i couldve use nla_policy but it makes it hard
	* to use it just for that..
	*/
	if (ops->validate)
		return ops->validate(val, len);

	if (ops->metatype == NLA_U32)
		ret = ife_validate_meta_u32(val, len);
	else if (ops->metatype == NLA_U16)
		ret = ife_validate_meta_u16(val, len);

	return ret;
}

#ifdef CONFIG_MODULES
static const char *ife_meta_id2name(u32 metaid)
{
	switch (metaid) {
	case IFE_META_SKBMARK:
		return "skbmark";
	case IFE_META_PRIO:
		return "skbprio";
	case IFE_META_TCINDEX:
		return "tcindex";
	default:
		return "unknown";
	}
}
#endif

/* called when adding new meta information
*/
static int load_metaops_and_vet(u32 metaid, void *val, int len, bool rtnl_held)
{
	struct tcf_meta_ops *ops = find_ife_oplist(metaid);
	int ret = 0;

	if (!ops) {
		ret = -ENOENT;
#ifdef CONFIG_MODULES
		if (rtnl_held)
			rtnl_unlock();
		request_module("ife-meta-%s", ife_meta_id2name(metaid));
		if (rtnl_held)
			rtnl_lock();
		ops = find_ife_oplist(metaid);
#endif
	}

	if (ops) {
		ret = 0;
		if (len)
			ret = ife_validate_metatype(ops, val, len);

		module_put(ops->owner);
	}

	return ret;
}

/* called when adding new meta information
*/
static int __add_metainfo(const struct tcf_meta_ops *ops,
			  struct tcf_ife_info *ife, u32 metaid, void *metaval,
			  int len, bool atomic, bool exists)
{
	struct tcf_meta_info *mi = NULL;
	int ret = 0;

	mi = kzalloc(sizeof(*mi), atomic ? GFP_ATOMIC : GFP_KERNEL);
	if (!mi)
		return -ENOMEM;

	mi->metaid = metaid;
	mi->ops = ops;
	if (len > 0) {
		ret = ops->alloc(mi, metaval, atomic ? GFP_ATOMIC : GFP_KERNEL);
		if (ret != 0) {
			kfree(mi);
			return ret;
		}
	}

	if (exists)
		spin_lock_bh(&ife->tcf_lock);
	list_add_tail(&mi->metalist, &ife->metalist);
	if (exists)
		spin_unlock_bh(&ife->tcf_lock);

	return ret;
}

static int add_metainfo_and_get_ops(const struct tcf_meta_ops *ops,
				    struct tcf_ife_info *ife, u32 metaid,
				    bool exists)
{
	int ret;

	if (!try_module_get(ops->owner))
		return -ENOENT;
	ret = __add_metainfo(ops, ife, metaid, NULL, 0, true, exists);
	if (ret)
		module_put(ops->owner);
	return ret;
}

static int add_metainfo(struct tcf_ife_info *ife, u32 metaid, void *metaval,
			int len, bool exists)
{
	const struct tcf_meta_ops *ops = find_ife_oplist(metaid);
	int ret;

	if (!ops)
		return -ENOENT;
	ret = __add_metainfo(ops, ife, metaid, metaval, len, false, exists);
	if (ret)
		/*put back what find_ife_oplist took */
		module_put(ops->owner);
	return ret;
}

static int use_all_metadata(struct tcf_ife_info *ife, bool exists)
{
	struct tcf_meta_ops *o;
	int rc = 0;
	int installed = 0;

	read_lock(&ife_mod_lock);
	list_for_each_entry(o, &ifeoplist, list) {
		rc = add_metainfo_and_get_ops(o, ife, o->metaid, exists);
		if (rc == 0)
			installed += 1;
	}
	read_unlock(&ife_mod_lock);

	if (installed)
		return 0;
	else
		return -EINVAL;
}

static int dump_metalist(struct sk_buff *skb, struct tcf_ife_info *ife)
{
	struct tcf_meta_info *e;
	struct nlattr *nest;
	unsigned char *b = skb_tail_pointer(skb);
	int total_encoded = 0;

	/*can only happen on decode */
	if (list_empty(&ife->metalist))
		return 0;

	nest = nla_nest_start_noflag(skb, TCA_IFE_METALST);
	if (!nest)
		goto out_nlmsg_trim;

	list_for_each_entry(e, &ife->metalist, metalist) {
		if (!e->ops->get(skb, e))
			total_encoded += 1;
	}

	if (!total_encoded)
		goto out_nlmsg_trim;

	nla_nest_end(skb, nest);

	return 0;

out_nlmsg_trim:
	nlmsg_trim(skb, b);
	return -1;
}

/* under ife->tcf_lock */
static void _tcf_ife_cleanup(struct tc_action *a)
{
	struct tcf_ife_info *ife = to_ife(a);
	struct tcf_meta_info *e, *n;

	list_for_each_entry_safe(e, n, &ife->metalist, metalist) {
		list_del(&e->metalist);
		if (e->metaval) {
			if (e->ops->release)
				e->ops->release(e);
			else
				kfree(e->metaval);
		}
		module_put(e->ops->owner);
		kfree(e);
	}
}

static void tcf_ife_cleanup(struct tc_action *a)
{
	struct tcf_ife_info *ife = to_ife(a);
	struct tcf_ife_params *p;

	spin_lock_bh(&ife->tcf_lock);
	_tcf_ife_cleanup(a);
	spin_unlock_bh(&ife->tcf_lock);

	p = rcu_dereference_protected(ife->params, 1);
	if (p)
		kfree_rcu(p, rcu);
}

static int populate_metalist(struct tcf_ife_info *ife, struct nlattr **tb,
			     bool exists, bool rtnl_held)
{
	int len = 0;
	int rc = 0;
	int i = 0;
	void *val;

	for (i = 1; i < max_metacnt; i++) {
		if (tb[i]) {
			val = nla_data(tb[i]);
			len = nla_len(tb[i]);

			rc = load_metaops_and_vet(i, val, len, rtnl_held);
			if (rc != 0)
				return rc;

			rc = add_metainfo(ife, i, val, len, exists);
			if (rc)
				return rc;
		}
	}

	return rc;
}

static int tcf_ife_init(struct net *net, struct nlattr *nla,
			struct nlattr *est, struct tc_action **a,
			int ovr, int bind, bool rtnl_held,
			struct tcf_proto *tp, struct netlink_ext_ack *extack)
{
	struct tc_action_net *tn = net_generic(net, ife_net_id);
	struct nlattr *tb[TCA_IFE_MAX + 1];
	struct nlattr *tb2[IFE_META_MAX + 1];
	struct tcf_chain *goto_ch = NULL;
	struct tcf_ife_params *p;
	struct tcf_ife_info *ife;
	u16 ife_type = ETH_P_IFE;
	struct tc_ife *parm;
	u8 *daddr = NULL;
	u8 *saddr = NULL;
	bool exists = false;
	int ret = 0;
	int err;

	err = nla_parse_nested_deprecated(tb, TCA_IFE_MAX, nla, ife_policy,
					  NULL);
	if (err < 0)
		return err;

	if (!tb[TCA_IFE_PARMS])
		return -EINVAL;

	parm = nla_data(tb[TCA_IFE_PARMS]);

	/* IFE_DECODE is 0 and indicates the opposite of IFE_ENCODE because
	 * they cannot run as the same time. Check on all other values which
	 * are not supported right now.
	 */
	if (parm->flags & ~IFE_ENCODE)
		return -EINVAL;

	p = kzalloc(sizeof(*p), GFP_KERNEL);
	if (!p)
		return -ENOMEM;

	err = tcf_idr_check_alloc(tn, &parm->index, a, bind);
	if (err < 0) {
		kfree(p);
		return err;
	}
	exists = err;
	if (exists && bind) {
		kfree(p);
		return 0;
	}

	if (!exists) {
		ret = tcf_idr_create(tn, parm->index, est, a, &act_ife_ops,
				     bind, true);
		if (ret) {
			tcf_idr_cleanup(tn, parm->index);
			kfree(p);
			return ret;
		}
		ret = ACT_P_CREATED;
	} else if (!ovr) {
		tcf_idr_release(*a, bind);
		kfree(p);
		return -EEXIST;
	}

	ife = to_ife(*a);
	err = tcf_action_check_ctrlact(parm->action, tp, &goto_ch, extack);
	if (err < 0)
		goto release_idr;

	p->flags = parm->flags;

	if (parm->flags & IFE_ENCODE) {
		if (tb[TCA_IFE_TYPE])
			ife_type = nla_get_u16(tb[TCA_IFE_TYPE]);
		if (tb[TCA_IFE_DMAC])
			daddr = nla_data(tb[TCA_IFE_DMAC]);
		if (tb[TCA_IFE_SMAC])
			saddr = nla_data(tb[TCA_IFE_SMAC]);
	}

	if (parm->flags & IFE_ENCODE) {
		if (daddr)
			ether_addr_copy(p->eth_dst, daddr);
		else
			eth_zero_addr(p->eth_dst);

		if (saddr)
			ether_addr_copy(p->eth_src, saddr);
		else
			eth_zero_addr(p->eth_src);

		p->eth_type = ife_type;
	}


	if (ret == ACT_P_CREATED)
		INIT_LIST_HEAD(&ife->metalist);

	if (tb[TCA_IFE_METALST]) {
		err = nla_parse_nested_deprecated(tb2, IFE_META_MAX,
						  tb[TCA_IFE_METALST], NULL,
						  NULL);
		if (err)
			goto metadata_parse_err;
		err = populate_metalist(ife, tb2, exists, rtnl_held);
		if (err)
			goto metadata_parse_err;

	} else {
		/* if no passed metadata allow list or passed allow-all
		 * then here we process by adding as many supported metadatum
		 * as we can. You better have at least one else we are
		 * going to bail out
		 */
		err = use_all_metadata(ife, exists);
		if (err)
			goto metadata_parse_err;
	}

	if (exists)
		spin_lock_bh(&ife->tcf_lock);
	/* protected by tcf_lock when modifying existing action */
	goto_ch = tcf_action_set_ctrlact(*a, parm->action, goto_ch);
	rcu_swap_protected(ife->params, p, 1);

	if (exists)
		spin_unlock_bh(&ife->tcf_lock);
	if (goto_ch)
		tcf_chain_put_by_act(goto_ch);
	if (p)
		kfree_rcu(p, rcu);

	if (ret == ACT_P_CREATED)
		tcf_idr_insert(tn, *a);

	return ret;
metadata_parse_err:
	if (goto_ch)
		tcf_chain_put_by_act(goto_ch);
release_idr:
	kfree(p);
	tcf_idr_release(*a, bind);
	return err;
}

static int tcf_ife_dump(struct sk_buff *skb, struct tc_action *a, int bind,
			int ref)
{
	unsigned char *b = skb_tail_pointer(skb);
	struct tcf_ife_info *ife = to_ife(a);
	struct tcf_ife_params *p;
	struct tc_ife opt = {
		.index = ife->tcf_index,
		.refcnt = refcount_read(&ife->tcf_refcnt) - ref,
		.bindcnt = atomic_read(&ife->tcf_bindcnt) - bind,
	};
	struct tcf_t t;

	spin_lock_bh(&ife->tcf_lock);
	opt.action = ife->tcf_action;
	p = rcu_dereference_protected(ife->params,
				      lockdep_is_held(&ife->tcf_lock));
	opt.flags = p->flags;

	if (nla_put(skb, TCA_IFE_PARMS, sizeof(opt), &opt))
		goto nla_put_failure;

	tcf_tm_dump(&t, &ife->tcf_tm);
	if (nla_put_64bit(skb, TCA_IFE_TM, sizeof(t), &t, TCA_IFE_PAD))
		goto nla_put_failure;

	if (!is_zero_ether_addr(p->eth_dst)) {
		if (nla_put(skb, TCA_IFE_DMAC, ETH_ALEN, p->eth_dst))
			goto nla_put_failure;
	}

	if (!is_zero_ether_addr(p->eth_src)) {
		if (nla_put(skb, TCA_IFE_SMAC, ETH_ALEN, p->eth_src))
			goto nla_put_failure;
	}

	if (nla_put(skb, TCA_IFE_TYPE, 2, &p->eth_type))
		goto nla_put_failure;

	if (dump_metalist(skb, ife)) {
		/*ignore failure to dump metalist */
		pr_info("Failed to dump metalist\n");
	}

	spin_unlock_bh(&ife->tcf_lock);
	return skb->len;

nla_put_failure:
	spin_unlock_bh(&ife->tcf_lock);
	nlmsg_trim(skb, b);
	return -1;
}

static int find_decode_metaid(struct sk_buff *skb, struct tcf_ife_info *ife,
			      u16 metaid, u16 mlen, void *mdata)
{
	struct tcf_meta_info *e;

	/* XXX: use hash to speed up */
	list_for_each_entry(e, &ife->metalist, metalist) {
		if (metaid == e->metaid) {
			if (e->ops) {
				/* We check for decode presence already */
				return e->ops->decode(skb, mdata, mlen);
			}
		}
	}

	return -ENOENT;
}

static int tcf_ife_decode(struct sk_buff *skb, const struct tc_action *a,
			  struct tcf_result *res)
{
	struct tcf_ife_info *ife = to_ife(a);
	int action = ife->tcf_action;
	u8 *ifehdr_end;
	u8 *tlv_data;
	u16 metalen;

	bstats_cpu_update(this_cpu_ptr(ife->common.cpu_bstats), skb);
	tcf_lastuse_update(&ife->tcf_tm);

	if (skb_at_tc_ingress(skb))
		skb_push(skb, skb->dev->hard_header_len);

	tlv_data = ife_decode(skb, &metalen);
	if (unlikely(!tlv_data)) {
		qstats_drop_inc(this_cpu_ptr(ife->common.cpu_qstats));
		return TC_ACT_SHOT;
	}

	ifehdr_end = tlv_data + metalen;
	for (; tlv_data < ifehdr_end; tlv_data = ife_tlv_meta_next(tlv_data)) {
		u8 *curr_data;
		u16 mtype;
		u16 dlen;

		curr_data = ife_tlv_meta_decode(tlv_data, ifehdr_end, &mtype,
						&dlen, NULL);
		if (!curr_data) {
			qstats_drop_inc(this_cpu_ptr(ife->common.cpu_qstats));
			return TC_ACT_SHOT;
		}

		if (find_decode_metaid(skb, ife, mtype, dlen, curr_data)) {
			/* abuse overlimits to count when we receive metadata
			 * but dont have an ops for it
			 */
			pr_info_ratelimited("Unknown metaid %d dlen %d\n",
					    mtype, dlen);
			qstats_overlimit_inc(this_cpu_ptr(ife->common.cpu_qstats));
		}
	}

	if (WARN_ON(tlv_data != ifehdr_end)) {
		qstats_drop_inc(this_cpu_ptr(ife->common.cpu_qstats));
		return TC_ACT_SHOT;
	}

	skb->protocol = eth_type_trans(skb, skb->dev);
	skb_reset_network_header(skb);

	return action;
}

/*XXX: check if we can do this at install time instead of current
 * send data path
**/
static int ife_get_sz(struct sk_buff *skb, struct tcf_ife_info *ife)
{
	struct tcf_meta_info *e, *n;
	int tot_run_sz = 0, run_sz = 0;

	list_for_each_entry_safe(e, n, &ife->metalist, metalist) {
		if (e->ops->check_presence) {
			run_sz = e->ops->check_presence(skb, e);
			tot_run_sz += run_sz;
		}
	}

	return tot_run_sz;
}

static int tcf_ife_encode(struct sk_buff *skb, const struct tc_action *a,
			  struct tcf_result *res, struct tcf_ife_params *p)
{
	struct tcf_ife_info *ife = to_ife(a);
	int action = ife->tcf_action;
	struct ethhdr *oethh;	/* outer ether header */
	struct tcf_meta_info *e;
	/*
	   OUTERHDR:TOTMETALEN:{TLVHDR:Metadatum:TLVHDR..}:ORIGDATA
	   where ORIGDATA = original ethernet header ...
	 */
	u16 metalen = ife_get_sz(skb, ife);
	int hdrm = metalen + skb->dev->hard_header_len + IFE_METAHDRLEN;
	unsigned int skboff = 0;
	int new_len = skb->len + hdrm;
	bool exceed_mtu = false;
	void *ife_meta;
	int err = 0;

	if (!skb_at_tc_ingress(skb)) {
		if (new_len > skb->dev->mtu)
			exceed_mtu = true;
	}

	bstats_cpu_update(this_cpu_ptr(ife->common.cpu_bstats), skb);
	tcf_lastuse_update(&ife->tcf_tm);

	if (!metalen) {		/* no metadata to send */
		/* abuse overlimits to count when we allow packet
		 * with no metadata
		 */
		qstats_overlimit_inc(this_cpu_ptr(ife->common.cpu_qstats));
		return action;
	}
	/* could be stupid policy setup or mtu config
	 * so lets be conservative.. */
	if ((action == TC_ACT_SHOT) || exceed_mtu) {
		qstats_drop_inc(this_cpu_ptr(ife->common.cpu_qstats));
		return TC_ACT_SHOT;
	}

	if (skb_at_tc_ingress(skb))
		skb_push(skb, skb->dev->hard_header_len);

	ife_meta = ife_encode(skb, metalen);

	spin_lock(&ife->tcf_lock);

	/* XXX: we dont have a clever way of telling encode to
	 * not repeat some of the computations that are done by
	 * ops->presence_check...
	 */
	list_for_each_entry(e, &ife->metalist, metalist) {
		if (e->ops->encode) {
			err = e->ops->encode(skb, (void *)(ife_meta + skboff),
					     e);
		}
		if (err < 0) {
			/* too corrupt to keep around if overwritten */
			spin_unlock(&ife->tcf_lock);
			qstats_drop_inc(this_cpu_ptr(ife->common.cpu_qstats));
			return TC_ACT_SHOT;
		}
		skboff += err;
	}
	spin_unlock(&ife->tcf_lock);
	oethh = (struct ethhdr *)skb->data;

	if (!is_zero_ether_addr(p->eth_src))
		ether_addr_copy(oethh->h_source, p->eth_src);
	if (!is_zero_ether_addr(p->eth_dst))
		ether_addr_copy(oethh->h_dest, p->eth_dst);
	oethh->h_proto = htons(p->eth_type);

	if (skb_at_tc_ingress(skb))
		skb_pull(skb, skb->dev->hard_header_len);

	return action;
}

static int tcf_ife_act(struct sk_buff *skb, const struct tc_action *a,
		       struct tcf_result *res)
{
	struct tcf_ife_info *ife = to_ife(a);
	struct tcf_ife_params *p;
	int ret;

	p = rcu_dereference_bh(ife->params);
	if (p->flags & IFE_ENCODE) {
		ret = tcf_ife_encode(skb, a, res, p);
		return ret;
	}

	return tcf_ife_decode(skb, a, res);
}

static int tcf_ife_walker(struct net *net, struct sk_buff *skb,
			  struct netlink_callback *cb, int type,
			  const struct tc_action_ops *ops,
			  struct netlink_ext_ack *extack)
{
	struct tc_action_net *tn = net_generic(net, ife_net_id);

	return tcf_generic_walker(tn, skb, cb, type, ops, extack);
}

static int tcf_ife_search(struct net *net, struct tc_action **a, u32 index)
{
	struct tc_action_net *tn = net_generic(net, ife_net_id);

	return tcf_idr_search(tn, a, index);
}

static struct tc_action_ops act_ife_ops = {
	.kind = "ife",
	.id = TCA_ID_IFE,
	.owner = THIS_MODULE,
	.act = tcf_ife_act,
	.dump = tcf_ife_dump,
	.cleanup = tcf_ife_cleanup,
	.init = tcf_ife_init,
	.walk = tcf_ife_walker,
	.lookup = tcf_ife_search,
	.size =	sizeof(struct tcf_ife_info),
};

static __net_init int ife_init_net(struct net *net)
{
	struct tc_action_net *tn = net_generic(net, ife_net_id);

	return tc_action_net_init(tn, &act_ife_ops);
}

static void __net_exit ife_exit_net(struct list_head *net_list)
{
	tc_action_net_exit(net_list, ife_net_id);
}

static struct pernet_operations ife_net_ops = {
	.init = ife_init_net,
	.exit_batch = ife_exit_net,
	.id   = &ife_net_id,
	.size = sizeof(struct tc_action_net),
};

static int __init ife_init_module(void)
{
	return tcf_register_action(&act_ife_ops, &ife_net_ops);
}

static void __exit ife_cleanup_module(void)
{
	tcf_unregister_action(&act_ife_ops, &ife_net_ops);
}

module_init(ife_init_module);
module_exit(ife_cleanup_module);

MODULE_AUTHOR("Jamal Hadi Salim(2015)");
MODULE_DESCRIPTION("Inter-FE LFB action");
MODULE_LICENSE("GPL");
