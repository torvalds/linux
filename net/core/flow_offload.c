/* SPDX-License-Identifier: GPL-2.0 */
#include <linux/kernel.h>
#include <linux/slab.h>
#include <net/flow_offload.h>
#include <net/pkt_cls.h>

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

struct flow_block_cb *flow_block_cb_alloc(struct net *net, tc_setup_cb_t *cb,
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

int flow_block_cb_setup_simple(struct flow_block_offload *f,
			       struct list_head *driver_block_list,
			       tc_setup_cb_t *cb, void *cb_ident, void *cb_priv,
			       bool ingress_only)
{
	if (ingress_only &&
	    f->binder_type != FLOW_BLOCK_BINDER_TYPE_CLSACT_INGRESS)
		return -EOPNOTSUPP;

	f->driver_block_list = driver_block_list;

	switch (f->command) {
	case FLOW_BLOCK_BIND:
		return tcf_block_cb_register(f->block, cb, cb_ident, cb_priv,
					     f->extack);
	case FLOW_BLOCK_UNBIND:
		tcf_block_cb_unregister(f->block, cb, cb_ident);
		return 0;
	default:
		return -EOPNOTSUPP;
	}
}
EXPORT_SYMBOL(flow_block_cb_setup_simple);
