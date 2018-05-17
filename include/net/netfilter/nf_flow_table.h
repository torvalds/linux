#ifndef _NF_FLOW_TABLE_H
#define _NF_FLOW_TABLE_H

#include <linux/in.h>
#include <linux/in6.h>
#include <linux/netdevice.h>
#include <linux/rhashtable.h>
#include <linux/rcupdate.h>
#include <net/dst.h>

struct nf_flowtable;

struct nf_flowtable_type {
	struct list_head		list;
	int				family;
	void				(*gc)(struct work_struct *work);
	void				(*free)(struct nf_flowtable *ft);
	const struct rhashtable_params	*params;
	nf_hookfn			*hook;
	struct module			*owner;
};

struct nf_flowtable {
	struct rhashtable		rhashtable;
	const struct nf_flowtable_type	*type;
	struct delayed_work		gc_work;
};

enum flow_offload_tuple_dir {
	FLOW_OFFLOAD_DIR_ORIGINAL,
	FLOW_OFFLOAD_DIR_REPLY,
	__FLOW_OFFLOAD_DIR_MAX		= FLOW_OFFLOAD_DIR_REPLY,
};
#define FLOW_OFFLOAD_DIR_MAX	(__FLOW_OFFLOAD_DIR_MAX + 1)

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

	int				oifidx;

	struct dst_entry		*dst_cache;
};

struct flow_offload_tuple_rhash {
	struct rhash_head		node;
	struct flow_offload_tuple	tuple;
};

#define FLOW_OFFLOAD_SNAT	0x1
#define FLOW_OFFLOAD_DNAT	0x2
#define FLOW_OFFLOAD_DYING	0x4

struct flow_offload {
	struct flow_offload_tuple_rhash		tuplehash[FLOW_OFFLOAD_DIR_MAX];
	u32					flags;
	union {
		/* Your private driver data here. */
		u32		timeout;
	};
};

#define NF_FLOW_TIMEOUT (30 * HZ)

struct nf_flow_route {
	struct {
		struct dst_entry	*dst;
		int			ifindex;
	} tuple[FLOW_OFFLOAD_DIR_MAX];
};

struct flow_offload *flow_offload_alloc(struct nf_conn *ct,
					struct nf_flow_route *route);
void flow_offload_free(struct flow_offload *flow);

int flow_offload_add(struct nf_flowtable *flow_table, struct flow_offload *flow);
struct flow_offload_tuple_rhash *flow_offload_lookup(struct nf_flowtable *flow_table,
						     struct flow_offload_tuple *tuple);
int nf_flow_table_iterate(struct nf_flowtable *flow_table,
			  void (*iter)(struct flow_offload *flow, void *data),
			  void *data);

void nf_flow_table_cleanup(struct net *net, struct net_device *dev);

void nf_flow_table_free(struct nf_flowtable *flow_table);
void nf_flow_offload_work_gc(struct work_struct *work);
extern const struct rhashtable_params nf_flow_offload_rhash_params;

void flow_offload_dead(struct flow_offload *flow);

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

#endif /* _FLOW_OFFLOAD_H */
