/* SPDX-License-Identifier: GPL-2.0 */
#include <linux/kernel.h>
#include <linux/slab.h>
#include <net/flow_offload.h>
#include <linux/rtnetlink.h>
#include <linux/mutex.h>

struct flow_rule *flow_rule_alloc(unsigned int num_actions)
{
	struct flow_rule *rule;

	rule = kzalloc(struct_size(rule, action.entries, num_actions),
		       GFP_KERNEL);
	if (!rule)
		return NULL;

	rule->action.num_entries = num_actions;

	return rule;
}
EXPORT_SYMBOL(flow_rule_alloc);

#define FLOW_DISSECTOR_MATCH(__rule, __type, __out)				\
	const struct flow_match *__m = &(__rule)->match;			\
	struct flow_dissector *__d = (__m)->dissector;				\
										\
	(__out)->key = skb_flow_dissector_target(__d, __type, (__m)->key);	\
	(__out)->mask = skb_flow_dissector_target(__d, __type, (__m)->mask);	\

void flow_rule_match_meta(const struct flow_rule *rule,
			  struct flow_match_meta *out)
{
	FLOW_DISSECTOR_MATCH(rule, FLOW_DISSECTOR_KEY_META, out);
}
EXPORT_SYMBOL(flow_rule_match_meta);

void flow_rule_match_basic(const struct flow_rule *rule,
			   struct flow_match_basic *out)
{
	FLOW_DISSECTOR_MATCH(rule, FLOW_DISSECTOR_KEY_BASIC, out);
}
EXPORT_SYMBOL(flow_rule_match_basic);

void flow_rule_match_control(const struct flow_rule *rule,
			     struct flow_match_control *out)
{
	FLOW_DISSECTOR_MATCH(rule, FLOW_DISSECTOR_KEY_CONTROL, out);
}
EXPORT_SYMBOL(flow_rule_match_control);

void flow_rule_match_eth_addrs(const struct flow_rule *rule,
			       struct flow_match_eth_addrs *out)
{
	FLOW_DISSECTOR_MATCH(rule, FLOW_DISSECTOR_KEY_ETH_ADDRS, out);
}
EXPORT_SYMBOL(flow_rule_match_eth_addrs);

void flow_rule_match_vlan(const struct flow_rule *rule,
			  struct flow_match_vlan *out)
{
	FLOW_DISSECTOR_MATCH(rule, FLOW_DISSECTOR_KEY_VLAN, out);
}
EXPORT_SYMBOL(flow_rule_match_vlan);

void flow_rule_match_cvlan(const struct flow_rule *rule,
			   struct flow_match_vlan *out)
{
	FLOW_DISSECTOR_MATCH(rule, FLOW_DISSECTOR_KEY_CVLAN, out);
}
EXPORT_SYMBOL(flow_rule_match_cvlan);

void flow_rule_match_ipv4_addrs(const struct flow_rule *rule,
				struct flow_match_ipv4_addrs *out)
{
	FLOW_DISSECTOR_MATCH(rule, FLOW_DISSECTOR_KEY_IPV4_ADDRS, out);
}
EXPORT_SYMBOL(flow_rule_match_ipv4_addrs);

void flow_rule_match_ipv6_addrs(const struct flow_rule *rule,
				struct flow_match_ipv6_addrs *out)
{
	FLOW_DISSECTOR_MATCH(rule, FLOW_DISSECTOR_KEY_IPV6_ADDRS, out);
}
EXPORT_SYMBOL(flow_rule_match_ipv6_addrs);

void flow_rule_match_ip(const struct flow_rule *rule,
			struct flow_match_ip *out)
{
	FLOW_DISSECTOR_MATCH(rule, FLOW_DISSECTOR_KEY_IP, out);
}
EXPORT_SYMBOL(flow_rule_match_ip);

void flow_rule_match_ports(const struct flow_rule *rule,
			   struct flow_match_ports *out)
{
	FLOW_DISSECTOR_MATCH(rule, FLOW_DISSECTOR_KEY_PORTS, out);
}
EXPORT_SYMBOL(flow_rule_match_ports);

void flow_rule_match_tcp(const struct flow_rule *rule,
			 struct flow_match_tcp *out)
{
	FLOW_DISSECTOR_MATCH(rule, FLOW_DISSECTOR_KEY_TCP, out);
}
EXPORT_SYMBOL(flow_rule_match_tcp);

void flow_rule_match_icmp(const struct flow_rule *rule,
			  struct flow_match_icmp *out)
{
	FLOW_DISSECTOR_MATCH(rule, FLOW_DISSECTOR_KEY_ICMP, out);
}
EXPORT_SYMBOL(flow_rule_match_icmp);

void flow_rule_match_mpls(const struct flow_rule *rule,
			  struct flow_match_mpls *out)
{
	FLOW_DISSECTOR_MATCH(rule, FLOW_DISSECTOR_KEY_MPLS, out);
}
EXPORT_SYMBOL(flow_rule_match_mpls);

void flow_rule_match_enc_control(const struct flow_rule *rule,
				 struct flow_match_control *out)
{
	FLOW_DISSECTOR_MATCH(rule, FLOW_DISSECTOR_KEY_ENC_CONTROL, out);
}
EXPORT_SYMBOL(flow_rule_match_enc_control);

void flow_rule_match_enc_ipv4_addrs(const struct flow_rule *rule,
				    struct flow_match_ipv4_addrs *out)
{
	FLOW_DISSECTOR_MATCH(rule, FLOW_DISSECTOR_KEY_ENC_IPV4_ADDRS, out);
}
EXPORT_SYMBOL(flow_rule_match_enc_ipv4_addrs);

void flow_rule_match_enc_ipv6_addrs(const struct flow_rule *rule,
				    struct flow_match_ipv6_addrs *out)
{
	FLOW_DISSECTOR_MATCH(rule, FLOW_DISSECTOR_KEY_ENC_IPV6_ADDRS, out);
}
EXPORT_SYMBOL(flow_rule_match_enc_ipv6_addrs);

void flow_rule_match_enc_ip(const struct flow_rule *rule,
			    struct flow_match_ip *out)
{
	FLOW_DISSECTOR_MATCH(rule, FLOW_DISSECTOR_KEY_ENC_IP, out);
}
EXPORT_SYMBOL(flow_rule_match_enc_ip);

void flow_rule_match_enc_ports(const struct flow_rule *rule,
			       struct flow_match_ports *out)
{
	FLOW_DISSECTOR_MATCH(rule, FLOW_DISSECTOR_KEY_ENC_PORTS, out);
}
EXPORT_SYMBOL(flow_rule_match_enc_ports);

void flow_rule_match_enc_keyid(const struct flow_rule *rule,
			       struct flow_match_enc_keyid *out)
{
	FLOW_DISSECTOR_MATCH(rule, FLOW_DISSECTOR_KEY_ENC_KEYID, out);
}
EXPORT_SYMBOL(flow_rule_match_enc_keyid);

void flow_rule_match_enc_opts(const struct flow_rule *rule,
			      struct flow_match_enc_opts *out)
{
	FLOW_DISSECTOR_MATCH(rule, FLOW_DISSECTOR_KEY_ENC_OPTS, out);
}
EXPORT_SYMBOL(flow_rule_match_enc_opts);

struct flow_block_cb *flow_block_cb_alloc(flow_setup_cb_t *cb,
					  void *cb_ident, void *cb_priv,
					  void (*release)(void *cb_priv))
{
	struct flow_block_cb *block_cb;

	block_cb = kzalloc(sizeof(*block_cb), GFP_KERNEL);
	if (!block_cb)
		return ERR_PTR(-ENOMEM);

	block_cb->cb = cb;
	block_cb->cb_ident = cb_ident;
	block_cb->cb_priv = cb_priv;
	block_cb->release = release;

	return block_cb;
}
EXPORT_SYMBOL(flow_block_cb_alloc);

void flow_block_cb_free(struct flow_block_cb *block_cb)
{
	if (block_cb->release)
		block_cb->release(block_cb->cb_priv);

	kfree(block_cb);
}
EXPORT_SYMBOL(flow_block_cb_free);

struct flow_block_cb *flow_block_cb_lookup(struct flow_block *block,
					   flow_setup_cb_t *cb, void *cb_ident)
{
	struct flow_block_cb *block_cb;

	list_for_each_entry(block_cb, &block->cb_list, list) {
		if (block_cb->cb == cb &&
		    block_cb->cb_ident == cb_ident)
			return block_cb;
	}

	return NULL;
}
EXPORT_SYMBOL(flow_block_cb_lookup);

void *flow_block_cb_priv(struct flow_block_cb *block_cb)
{
	return block_cb->cb_priv;
}
EXPORT_SYMBOL(flow_block_cb_priv);

void flow_block_cb_incref(struct flow_block_cb *block_cb)
{
	block_cb->refcnt++;
}
EXPORT_SYMBOL(flow_block_cb_incref);

unsigned int flow_block_cb_decref(struct flow_block_cb *block_cb)
{
	return --block_cb->refcnt;
}
EXPORT_SYMBOL(flow_block_cb_decref);

bool flow_block_cb_is_busy(flow_setup_cb_t *cb, void *cb_ident,
			   struct list_head *driver_block_list)
{
	struct flow_block_cb *block_cb;

	list_for_each_entry(block_cb, driver_block_list, driver_list) {
		if (block_cb->cb == cb &&
		    block_cb->cb_ident == cb_ident)
			return true;
	}

	return false;
}
EXPORT_SYMBOL(flow_block_cb_is_busy);

int flow_block_cb_setup_simple(struct flow_block_offload *f,
			       struct list_head *driver_block_list,
			       flow_setup_cb_t *cb,
			       void *cb_ident, void *cb_priv,
			       bool ingress_only)
{
	struct flow_block_cb *block_cb;

	if (ingress_only &&
	    f->binder_type != FLOW_BLOCK_BINDER_TYPE_CLSACT_INGRESS)
		return -EOPNOTSUPP;

	f->driver_block_list = driver_block_list;

	switch (f->command) {
	case FLOW_BLOCK_BIND:
		if (flow_block_cb_is_busy(cb, cb_ident, driver_block_list))
			return -EBUSY;

		block_cb = flow_block_cb_alloc(cb, cb_ident, cb_priv, NULL);
		if (IS_ERR(block_cb))
			return PTR_ERR(block_cb);

		flow_block_cb_add(block_cb, f);
		list_add_tail(&block_cb->driver_list, driver_block_list);
		return 0;
	case FLOW_BLOCK_UNBIND:
		block_cb = flow_block_cb_lookup(f->block, cb, cb_ident);
		if (!block_cb)
			return -ENOENT;

		flow_block_cb_remove(block_cb, f);
		list_del(&block_cb->driver_list);
		return 0;
	default:
		return -EOPNOTSUPP;
	}
}
EXPORT_SYMBOL(flow_block_cb_setup_simple);

static LIST_HEAD(block_cb_list);

static struct rhashtable indr_setup_block_ht;

struct flow_indr_block_cb {
	struct list_head list;
	void *cb_priv;
	flow_indr_block_bind_cb_t *cb;
	void *cb_ident;
};

struct flow_indr_block_dev {
	struct rhash_head ht_node;
	struct net_device *dev;
	unsigned int refcnt;
	struct list_head cb_list;
};

static const struct rhashtable_params flow_indr_setup_block_ht_params = {
	.key_offset	= offsetof(struct flow_indr_block_dev, dev),
	.head_offset	= offsetof(struct flow_indr_block_dev, ht_node),
	.key_len	= sizeof(struct net_device *),
};

static struct flow_indr_block_dev *
flow_indr_block_dev_lookup(struct net_device *dev)
{
	return rhashtable_lookup_fast(&indr_setup_block_ht, &dev,
				      flow_indr_setup_block_ht_params);
}

static struct flow_indr_block_dev *
flow_indr_block_dev_get(struct net_device *dev)
{
	struct flow_indr_block_dev *indr_dev;

	indr_dev = flow_indr_block_dev_lookup(dev);
	if (indr_dev)
		goto inc_ref;

	indr_dev = kzalloc(sizeof(*indr_dev), GFP_KERNEL);
	if (!indr_dev)
		return NULL;

	INIT_LIST_HEAD(&indr_dev->cb_list);
	indr_dev->dev = dev;
	if (rhashtable_insert_fast(&indr_setup_block_ht, &indr_dev->ht_node,
				   flow_indr_setup_block_ht_params)) {
		kfree(indr_dev);
		return NULL;
	}

inc_ref:
	indr_dev->refcnt++;
	return indr_dev;
}

static void flow_indr_block_dev_put(struct flow_indr_block_dev *indr_dev)
{
	if (--indr_dev->refcnt)
		return;

	rhashtable_remove_fast(&indr_setup_block_ht, &indr_dev->ht_node,
			       flow_indr_setup_block_ht_params);
	kfree(indr_dev);
}

static struct flow_indr_block_cb *
flow_indr_block_cb_lookup(struct flow_indr_block_dev *indr_dev,
			  flow_indr_block_bind_cb_t *cb, void *cb_ident)
{
	struct flow_indr_block_cb *indr_block_cb;

	list_for_each_entry(indr_block_cb, &indr_dev->cb_list, list)
		if (indr_block_cb->cb == cb &&
		    indr_block_cb->cb_ident == cb_ident)
			return indr_block_cb;
	return NULL;
}

static struct flow_indr_block_cb *
flow_indr_block_cb_add(struct flow_indr_block_dev *indr_dev, void *cb_priv,
		       flow_indr_block_bind_cb_t *cb, void *cb_ident)
{
	struct flow_indr_block_cb *indr_block_cb;

	indr_block_cb = flow_indr_block_cb_lookup(indr_dev, cb, cb_ident);
	if (indr_block_cb)
		return ERR_PTR(-EEXIST);

	indr_block_cb = kzalloc(sizeof(*indr_block_cb), GFP_KERNEL);
	if (!indr_block_cb)
		return ERR_PTR(-ENOMEM);

	indr_block_cb->cb_priv = cb_priv;
	indr_block_cb->cb = cb;
	indr_block_cb->cb_ident = cb_ident;
	list_add(&indr_block_cb->list, &indr_dev->cb_list);

	return indr_block_cb;
}

static void flow_indr_block_cb_del(struct flow_indr_block_cb *indr_block_cb)
{
	list_del(&indr_block_cb->list);
	kfree(indr_block_cb);
}

static DEFINE_MUTEX(flow_indr_block_cb_lock);

static void flow_block_cmd(struct net_device *dev,
			   flow_indr_block_bind_cb_t *cb, void *cb_priv,
			   enum flow_block_command command)
{
	struct flow_indr_block_entry *entry;

	mutex_lock(&flow_indr_block_cb_lock);
	list_for_each_entry(entry, &block_cb_list, list) {
		entry->cb(dev, cb, cb_priv, command);
	}
	mutex_unlock(&flow_indr_block_cb_lock);
}

int __flow_indr_block_cb_register(struct net_device *dev, void *cb_priv,
				  flow_indr_block_bind_cb_t *cb,
				  void *cb_ident)
{
	struct flow_indr_block_cb *indr_block_cb;
	struct flow_indr_block_dev *indr_dev;
	int err;

	indr_dev = flow_indr_block_dev_get(dev);
	if (!indr_dev)
		return -ENOMEM;

	indr_block_cb = flow_indr_block_cb_add(indr_dev, cb_priv, cb, cb_ident);
	err = PTR_ERR_OR_ZERO(indr_block_cb);
	if (err)
		goto err_dev_put;

	flow_block_cmd(dev, indr_block_cb->cb, indr_block_cb->cb_priv,
		       FLOW_BLOCK_BIND);

	return 0;

err_dev_put:
	flow_indr_block_dev_put(indr_dev);
	return err;
}
EXPORT_SYMBOL_GPL(__flow_indr_block_cb_register);

int flow_indr_block_cb_register(struct net_device *dev, void *cb_priv,
				flow_indr_block_bind_cb_t *cb,
				void *cb_ident)
{
	int err;

	rtnl_lock();
	err = __flow_indr_block_cb_register(dev, cb_priv, cb, cb_ident);
	rtnl_unlock();

	return err;
}
EXPORT_SYMBOL_GPL(flow_indr_block_cb_register);

void __flow_indr_block_cb_unregister(struct net_device *dev,
				     flow_indr_block_bind_cb_t *cb,
				     void *cb_ident)
{
	struct flow_indr_block_cb *indr_block_cb;
	struct flow_indr_block_dev *indr_dev;

	indr_dev = flow_indr_block_dev_lookup(dev);
	if (!indr_dev)
		return;

	indr_block_cb = flow_indr_block_cb_lookup(indr_dev, cb, cb_ident);
	if (!indr_block_cb)
		return;

	flow_block_cmd(dev, indr_block_cb->cb, indr_block_cb->cb_priv,
		       FLOW_BLOCK_UNBIND);

	flow_indr_block_cb_del(indr_block_cb);
	flow_indr_block_dev_put(indr_dev);
}
EXPORT_SYMBOL_GPL(__flow_indr_block_cb_unregister);

void flow_indr_block_cb_unregister(struct net_device *dev,
				   flow_indr_block_bind_cb_t *cb,
				   void *cb_ident)
{
	rtnl_lock();
	__flow_indr_block_cb_unregister(dev, cb, cb_ident);
	rtnl_unlock();
}
EXPORT_SYMBOL_GPL(flow_indr_block_cb_unregister);

void flow_indr_block_call(struct net_device *dev,
			  struct flow_block_offload *bo,
			  enum flow_block_command command)
{
	struct flow_indr_block_cb *indr_block_cb;
	struct flow_indr_block_dev *indr_dev;

	indr_dev = flow_indr_block_dev_lookup(dev);
	if (!indr_dev)
		return;

	list_for_each_entry(indr_block_cb, &indr_dev->cb_list, list)
		indr_block_cb->cb(dev, indr_block_cb->cb_priv, TC_SETUP_BLOCK,
				  bo);
}
EXPORT_SYMBOL_GPL(flow_indr_block_call);

void flow_indr_add_block_cb(struct flow_indr_block_entry *entry)
{
	mutex_lock(&flow_indr_block_cb_lock);
	list_add_tail(&entry->list, &block_cb_list);
	mutex_unlock(&flow_indr_block_cb_lock);
}
EXPORT_SYMBOL_GPL(flow_indr_add_block_cb);

void flow_indr_del_block_cb(struct flow_indr_block_entry *entry)
{
	mutex_lock(&flow_indr_block_cb_lock);
	list_del(&entry->list);
	mutex_unlock(&flow_indr_block_cb_lock);
}
EXPORT_SYMBOL_GPL(flow_indr_del_block_cb);

static int __init init_flow_indr_rhashtable(void)
{
	return rhashtable_init(&indr_setup_block_ht,
			       &flow_indr_setup_block_ht_params);
}
subsys_initcall(init_flow_indr_rhashtable);
