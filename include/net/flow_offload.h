#ifndef _NET_FLOW_OFFLOAD_H
#define _NET_FLOW_OFFLOAD_H

#include <linux/kernel.h>
#include <linux/list.h>
#include <net/flow_dissector.h>

struct flow_match {
	struct flow_dissector	*dissector;
	void			*mask;
	void			*key;
};

struct flow_match_meta {
	struct flow_dissector_key_meta *key, *mask;
};

struct flow_match_basic {
	struct flow_dissector_key_basic *key, *mask;
};

struct flow_match_control {
	struct flow_dissector_key_control *key, *mask;
};

struct flow_match_eth_addrs {
	struct flow_dissector_key_eth_addrs *key, *mask;
};

struct flow_match_vlan {
	struct flow_dissector_key_vlan *key, *mask;
};

struct flow_match_ipv4_addrs {
	struct flow_dissector_key_ipv4_addrs *key, *mask;
};

struct flow_match_ipv6_addrs {
	struct flow_dissector_key_ipv6_addrs *key, *mask;
};

struct flow_match_ip {
	struct flow_dissector_key_ip *key, *mask;
};

struct flow_match_ports {
	struct flow_dissector_key_ports *key, *mask;
};

struct flow_match_icmp {
	struct flow_dissector_key_icmp *key, *mask;
};

struct flow_match_tcp {
	struct flow_dissector_key_tcp *key, *mask;
};

struct flow_match_mpls {
	struct flow_dissector_key_mpls *key, *mask;
};

struct flow_match_enc_keyid {
	struct flow_dissector_key_keyid *key, *mask;
};

struct flow_match_enc_opts {
	struct flow_dissector_key_enc_opts *key, *mask;
};

struct flow_rule;

void flow_rule_match_meta(const struct flow_rule *rule,
			  struct flow_match_meta *out);
void flow_rule_match_basic(const struct flow_rule *rule,
			   struct flow_match_basic *out);
void flow_rule_match_control(const struct flow_rule *rule,
			     struct flow_match_control *out);
void flow_rule_match_eth_addrs(const struct flow_rule *rule,
			       struct flow_match_eth_addrs *out);
void flow_rule_match_vlan(const struct flow_rule *rule,
			  struct flow_match_vlan *out);
void flow_rule_match_cvlan(const struct flow_rule *rule,
			   struct flow_match_vlan *out);
void flow_rule_match_ipv4_addrs(const struct flow_rule *rule,
				struct flow_match_ipv4_addrs *out);
void flow_rule_match_ipv6_addrs(const struct flow_rule *rule,
				struct flow_match_ipv6_addrs *out);
void flow_rule_match_ip(const struct flow_rule *rule,
			struct flow_match_ip *out);
void flow_rule_match_ports(const struct flow_rule *rule,
			   struct flow_match_ports *out);
void flow_rule_match_tcp(const struct flow_rule *rule,
			 struct flow_match_tcp *out);
void flow_rule_match_icmp(const struct flow_rule *rule,
			  struct flow_match_icmp *out);
void flow_rule_match_mpls(const struct flow_rule *rule,
			  struct flow_match_mpls *out);
void flow_rule_match_enc_control(const struct flow_rule *rule,
				 struct flow_match_control *out);
void flow_rule_match_enc_ipv4_addrs(const struct flow_rule *rule,
				    struct flow_match_ipv4_addrs *out);
void flow_rule_match_enc_ipv6_addrs(const struct flow_rule *rule,
				    struct flow_match_ipv6_addrs *out);
void flow_rule_match_enc_ip(const struct flow_rule *rule,
			    struct flow_match_ip *out);
void flow_rule_match_enc_ports(const struct flow_rule *rule,
			       struct flow_match_ports *out);
void flow_rule_match_enc_keyid(const struct flow_rule *rule,
			       struct flow_match_enc_keyid *out);
void flow_rule_match_enc_opts(const struct flow_rule *rule,
			      struct flow_match_enc_opts *out);

enum flow_action_id {
	FLOW_ACTION_ACCEPT		= 0,
	FLOW_ACTION_DROP,
	FLOW_ACTION_TRAP,
	FLOW_ACTION_GOTO,
	FLOW_ACTION_REDIRECT,
	FLOW_ACTION_MIRRED,
	FLOW_ACTION_VLAN_PUSH,
	FLOW_ACTION_VLAN_POP,
	FLOW_ACTION_VLAN_MANGLE,
	FLOW_ACTION_TUNNEL_ENCAP,
	FLOW_ACTION_TUNNEL_DECAP,
	FLOW_ACTION_MANGLE,
	FLOW_ACTION_ADD,
	FLOW_ACTION_CSUM,
	FLOW_ACTION_MARK,
	FLOW_ACTION_WAKE,
	FLOW_ACTION_QUEUE,
	FLOW_ACTION_SAMPLE,
	FLOW_ACTION_POLICE,
	FLOW_ACTION_CT,
};

/* This is mirroring enum pedit_header_type definition for easy mapping between
 * tc pedit action. Legacy TCA_PEDIT_KEY_EX_HDR_TYPE_NETWORK is mapped to
 * FLOW_ACT_MANGLE_UNSPEC, which is supported by no driver.
 */
enum flow_action_mangle_base {
	FLOW_ACT_MANGLE_UNSPEC		= 0,
	FLOW_ACT_MANGLE_HDR_TYPE_ETH,
	FLOW_ACT_MANGLE_HDR_TYPE_IP4,
	FLOW_ACT_MANGLE_HDR_TYPE_IP6,
	FLOW_ACT_MANGLE_HDR_TYPE_TCP,
	FLOW_ACT_MANGLE_HDR_TYPE_UDP,
};

struct flow_action_entry {
	enum flow_action_id		id;
	union {
		u32			chain_index;	/* FLOW_ACTION_GOTO */
		struct net_device	*dev;		/* FLOW_ACTION_REDIRECT */
		struct {				/* FLOW_ACTION_VLAN */
			u16		vid;
			__be16		proto;
			u8		prio;
		} vlan;
		struct {				/* FLOW_ACTION_PACKET_EDIT */
			enum flow_action_mangle_base htype;
			u32		offset;
			u32		mask;
			u32		val;
		} mangle;
		const struct ip_tunnel_info *tunnel;	/* FLOW_ACTION_TUNNEL_ENCAP */
		u32			csum_flags;	/* FLOW_ACTION_CSUM */
		u32			mark;		/* FLOW_ACTION_MARK */
		struct {				/* FLOW_ACTION_QUEUE */
			u32		ctx;
			u32		index;
			u8		vf;
		} queue;
		struct {				/* FLOW_ACTION_SAMPLE */
			struct psample_group	*psample_group;
			u32			rate;
			u32			trunc_size;
			bool			truncate;
		} sample;
		struct {				/* FLOW_ACTION_POLICE */
			s64			burst;
			u64			rate_bytes_ps;
		} police;
		struct {				/* FLOW_ACTION_CT */
			int action;
			u16 zone;
		} ct;
	};
};

struct flow_action {
	unsigned int			num_entries;
	struct flow_action_entry 	entries[0];
};

static inline bool flow_action_has_entries(const struct flow_action *action)
{
	return action->num_entries;
}

/**
 * flow_action_has_one_action() - check if exactly one action is present
 * @action: tc filter flow offload action
 *
 * Returns true if exactly one action is present.
 */
static inline bool flow_offload_has_one_action(const struct flow_action *action)
{
	return action->num_entries == 1;
}

#define flow_action_for_each(__i, __act, __actions)			\
        for (__i = 0, __act = &(__actions)->entries[0]; __i < (__actions)->num_entries; __act = &(__actions)->entries[++__i])

struct flow_rule {
	struct flow_match	match;
	struct flow_action	action;
};

struct flow_rule *flow_rule_alloc(unsigned int num_actions);

static inline bool flow_rule_match_key(const struct flow_rule *rule,
				       enum flow_dissector_key_id key)
{
	return dissector_uses_key(rule->match.dissector, key);
}

struct flow_stats {
	u64	pkts;
	u64	bytes;
	u64	lastused;
};

static inline void flow_stats_update(struct flow_stats *flow_stats,
				     u64 bytes, u64 pkts, u64 lastused)
{
	flow_stats->pkts	+= pkts;
	flow_stats->bytes	+= bytes;
	flow_stats->lastused	= max_t(u64, flow_stats->lastused, lastused);
}

enum flow_block_command {
	FLOW_BLOCK_BIND,
	FLOW_BLOCK_UNBIND,
};

enum flow_block_binder_type {
	FLOW_BLOCK_BINDER_TYPE_UNSPEC,
	FLOW_BLOCK_BINDER_TYPE_CLSACT_INGRESS,
	FLOW_BLOCK_BINDER_TYPE_CLSACT_EGRESS,
};

struct flow_block {
	struct list_head cb_list;
};

struct netlink_ext_ack;

struct flow_block_offload {
	enum flow_block_command command;
	enum flow_block_binder_type binder_type;
	bool block_shared;
	struct net *net;
	struct flow_block *block;
	struct list_head cb_list;
	struct list_head *driver_block_list;
	struct netlink_ext_ack *extack;
};

enum tc_setup_type;
typedef int flow_setup_cb_t(enum tc_setup_type type, void *type_data,
			    void *cb_priv);

struct flow_block_cb {
	struct list_head	driver_list;
	struct list_head	list;
	flow_setup_cb_t		*cb;
	void			*cb_ident;
	void			*cb_priv;
	void			(*release)(void *cb_priv);
	unsigned int		refcnt;
};

struct flow_block_cb *flow_block_cb_alloc(flow_setup_cb_t *cb,
					  void *cb_ident, void *cb_priv,
					  void (*release)(void *cb_priv));
void flow_block_cb_free(struct flow_block_cb *block_cb);

struct flow_block_cb *flow_block_cb_lookup(struct flow_block *block,
					   flow_setup_cb_t *cb, void *cb_ident);

void *flow_block_cb_priv(struct flow_block_cb *block_cb);
void flow_block_cb_incref(struct flow_block_cb *block_cb);
unsigned int flow_block_cb_decref(struct flow_block_cb *block_cb);

static inline void flow_block_cb_add(struct flow_block_cb *block_cb,
				     struct flow_block_offload *offload)
{
	list_add_tail(&block_cb->list, &offload->cb_list);
}

static inline void flow_block_cb_remove(struct flow_block_cb *block_cb,
					struct flow_block_offload *offload)
{
	list_move(&block_cb->list, &offload->cb_list);
}

bool flow_block_cb_is_busy(flow_setup_cb_t *cb, void *cb_ident,
			   struct list_head *driver_block_list);

int flow_block_cb_setup_simple(struct flow_block_offload *f,
			       struct list_head *driver_list,
			       flow_setup_cb_t *cb,
			       void *cb_ident, void *cb_priv, bool ingress_only);

enum flow_cls_command {
	FLOW_CLS_REPLACE,
	FLOW_CLS_DESTROY,
	FLOW_CLS_STATS,
	FLOW_CLS_TMPLT_CREATE,
	FLOW_CLS_TMPLT_DESTROY,
};

struct flow_cls_common_offload {
	u32 chain_index;
	__be16 protocol;
	u32 prio;
	struct netlink_ext_ack *extack;
};

struct flow_cls_offload {
	struct flow_cls_common_offload common;
	enum flow_cls_command command;
	unsigned long cookie;
	struct flow_rule *rule;
	struct flow_stats stats;
	u32 classid;
};

static inline struct flow_rule *
flow_cls_offload_flow_rule(struct flow_cls_offload *flow_cmd)
{
	return flow_cmd->rule;
}

static inline void flow_block_init(struct flow_block *flow_block)
{
	INIT_LIST_HEAD(&flow_block->cb_list);
}

#endif /* _NET_FLOW_OFFLOAD_H */
