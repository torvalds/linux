#ifndef _NF_FLOW_TABLE_H
#define _NF_FLOW_TABLE_H

#include <linux/in.h>
#include <linux/in6.h>
#include <linux/netdevice.h>
#include <linux/rhashtable-types.h>
#include <linux/rcupdate.h>
#include <linux/netfilter.h>
#include <linux/netfilter/nf_conntrack_tuple_common.h>
#include <net/flow_offload.h>
#include <net/dst.h>

struct nf_flowtable;
struct nf_flow_rule;
struct flow_offload;
enum flow_offload_tuple_dir;

struct nf_flow_key {
	struct flow_dissector_key_meta			meta;
	struct flow_dissector_key_control		control;
	struct flow_dissector_key_control		enc_control;
	struct flow_dissector_key_basic			basic;
	union {
		struct flow_dissector_key_ipv4_addrs	ipv4;
		struct flow_dissector_key_ipv6_addrs	ipv6;
	};
	struct flow_dissector_key_keyid			enc_key_id;
	union {
		struct flow_dissector_key_ipv4_addrs	enc_ipv4;
		struct flow_dissector_key_ipv6_addrs	enc_ipv6;
	};
	struct flow_dissector_key_tcp			tcp;
	struct flow_dissector_key_ports			tp;
} __aligned(BITS_PER_LONG / 8); /* Ensure that we can do comparisons as longs. */

struct nf_flow_match {
	struct flow_dissector	dissector;
	struct nf_flow_key	key;
	struct nf_flow_key	mask;
};

struct nf_flow_rule {
	struct nf_flow_match	match;
	struct flow_rule	*rule;
};

struct nf_flowtable_type {
	struct list_head		list;
	int				family;
	int				(*init)(struct nf_flowtable *ft);
	int				(*setup)(struct nf_flowtable *ft,
						 struct net_device *dev,
						 enum flow_block_command cmd);
	int				(*action)(struct net *net,
						  const struct flow_offload *flow,
						  enum flow_offload_tuple_dir dir,
						  struct nf_flow_rule *flow_rule);
	void				(*free)(struct nf_flowtable *ft);
	nf_hookfn			*hook;
	struct module			*owner;
};

enum nf_flowtable_flags {
	NF_FLOWTABLE_HW_OFFLOAD		= 0x1,	/* NFT_FLOWTABLE_HW_OFFLOAD */
	NF_FLOWTABLE_COUNTER		= 0x2,	/* NFT_FLOWTABLE_COUNTER */
};

struct nf_flowtable {
	struct list_head		list;
	struct rhashtable		rhashtable;
	int				priority;
	const struct nf_flowtable_type	*type;
	struct delayed_work		gc_work;
	unsigned int			flags;
	struct flow_block		flow_block;
	struct rw_semaphore		flow_block_lock; /* Guards flow_block */
	possible_net_t			net;
};

static inline bool nf_flowtable_hw_offload(struct nf_flowtable *flowtable)
{
	return flowtable->flags & NF_FLOWTABLE_HW_OFFLOAD;
}

enum flow_offload_tuple_dir {
	FLOW_OFFLOAD_DIR_ORIGINAL = IP_CT_DIR_ORIGINAL,
	FLOW_OFFLOAD_DIR_REPLY = IP_CT_DIR_REPLY,
	FLOW_OFFLOAD_DIR_MAX = IP_CT_DIR_MAX
};

struct flow_offload_tuple {
	union {
		struct in_addr		src_v4;
		struct in6_addr		src_v6;
	};
	union {
		struct in_addr		dst_v4;
		struct in6_addr		dst_v6;
	};
	struct {
		__be16			src_port;
		__be16			dst_port;
	};

	int				iifidx;

	u8				l3proto;
	u8				l4proto;
	u8				dir;

	u16				mtu;

	struct dst_entry		*dst_cache;
};

struct flow_offload_tuple_rhash {
	struct rhash_head		node;
	struct flow_offload_tuple	tuple;
};

enum nf_flow_flags {
	NF_FLOW_SNAT,
	NF_FLOW_DNAT,
	NF_FLOW_TEARDOWN,
	NF_FLOW_HW,
	NF_FLOW_HW_DYING,
	NF_FLOW_HW_DEAD,
	NF_FLOW_HW_REFRESH,
	NF_FLOW_HW_PENDING,
};

enum flow_offload_type {
	NF_FLOW_OFFLOAD_UNSPEC	= 0,
	NF_FLOW_OFFLOAD_ROUTE,
};

struct flow_offload {
	struct flow_offload_tuple_rhash		tuplehash[FLOW_OFFLOAD_DIR_MAX];
	struct nf_conn				*ct;
	unsigned long				flags;
	u16					type;
	u32					timeout;
	struct rcu_head				rcu_head;
};

#define NF_FLOW_TIMEOUT (30 * HZ)
#define nf_flowtable_time_stamp	(u32)jiffies

static inline __s32 nf_flow_timeout_delta(unsigned int timeout)
{
	return (__s32)(timeout - nf_flowtable_time_stamp);
}

struct nf_flow_route {
	struct {
		struct dst_entry	*dst;
	} tuple[FLOW_OFFLOAD_DIR_MAX];
};

struct flow_offload *flow_offload_alloc(struct nf_conn *ct);
void flow_offload_free(struct flow_offload *flow);

int nf_flow_table_offload_add_cb(struct nf_flowtable *flow_table,
				 flow_setup_cb_t *cb, void *cb_priv);
void nf_flow_table_offload_del_cb(struct nf_flowtable *flow_table,
				  flow_setup_cb_t *cb, void *cb_priv);

int flow_offload_route_init(struct flow_offload *flow,
			    const struct nf_flow_route *route);

int flow_offload_add(struct nf_flowtable *flow_table, struct flow_offload *flow);
void flow_offload_refresh(struct nf_flowtable *flow_table,
			  struct flow_offload *flow);

struct flow_offload_tuple_rhash *flow_offload_lookup(struct nf_flowtable *flow_table,
						     struct flow_offload_tuple *tuple);
void nf_flow_table_cleanup(struct net_device *dev);

int nf_flow_table_init(struct nf_flowtable *flow_table);
void nf_flow_table_free(struct nf_flowtable *flow_table);

void flow_offload_teardown(struct flow_offload *flow);

int nf_flow_snat_port(const struct flow_offload *flow,
		      struct sk_buff *skb, unsigned int thoff,
		      u8 protocol, enum flow_offload_tuple_dir dir);
int nf_flow_dnat_port(const struct flow_offload *flow,
		      struct sk_buff *skb, unsigned int thoff,
		      u8 protocol, enum flow_offload_tuple_dir dir);

struct flow_ports {
	__be16 source, dest;
};

unsigned int nf_flow_offload_ip_hook(void *priv, struct sk_buff *skb,
				     const struct nf_hook_state *state);
unsigned int nf_flow_offload_ipv6_hook(void *priv, struct sk_buff *skb,
				       const struct nf_hook_state *state);

#define MODULE_ALIAS_NF_FLOWTABLE(family)	\
	MODULE_ALIAS("nf-flowtable-" __stringify(family))

void nf_flow_offload_add(struct nf_flowtable *flowtable,
			 struct flow_offload *flow);
void nf_flow_offload_del(struct nf_flowtable *flowtable,
			 struct flow_offload *flow);
void nf_flow_offload_stats(struct nf_flowtable *flowtable,
			   struct flow_offload *flow);

void nf_flow_table_offload_flush(struct nf_flowtable *flowtable);
int nf_flow_table_offload_setup(struct nf_flowtable *flowtable,
				struct net_device *dev,
				enum flow_block_command cmd);
int nf_flow_rule_route_ipv4(struct net *net, const struct flow_offload *flow,
			    enum flow_offload_tuple_dir dir,
			    struct nf_flow_rule *flow_rule);
int nf_flow_rule_route_ipv6(struct net *net, const struct flow_offload *flow,
			    enum flow_offload_tuple_dir dir,
			    struct nf_flow_rule *flow_rule);

int nf_flow_table_offload_init(void);
void nf_flow_table_offload_exit(void);

#endif /* _NF_FLOW_TABLE_H */
