#ifndef _NET_NF_TABLES_H
#define _NET_NF_TABLES_H

#include <linux/list.h>
#include <linux/netfilter.h>
#include <linux/netfilter/nf_tables.h>
#include <net/netlink.h>

struct nft_pktinfo {
	struct sk_buff			*skb;
	const struct net_device		*in;
	const struct net_device		*out;
	u8				hooknum;
	u8				nhoff;
	u8				thoff;
};

struct nft_data {
	union {
		u32				data[4];
		struct {
			u32			verdict;
			struct nft_chain	*chain;
		};
	};
} __attribute__((aligned(__alignof__(u64))));

static inline int nft_data_cmp(const struct nft_data *d1,
			       const struct nft_data *d2,
			       unsigned int len)
{
	return memcmp(d1->data, d2->data, len);
}

static inline void nft_data_copy(struct nft_data *dst,
				 const struct nft_data *src)
{
	BUILD_BUG_ON(__alignof__(*dst) != __alignof__(u64));
	*(u64 *)&dst->data[0] = *(u64 *)&src->data[0];
	*(u64 *)&dst->data[2] = *(u64 *)&src->data[2];
}

static inline void nft_data_debug(const struct nft_data *data)
{
	pr_debug("data[0]=%x data[1]=%x data[2]=%x data[3]=%x\n",
		 data->data[0], data->data[1],
		 data->data[2], data->data[3]);
}

/**
 *	struct nft_ctx - nf_tables rule context
 *
 * 	@afi: address family info
 * 	@table: the table the chain is contained in
 * 	@chain: the chain the rule is contained in
 */
struct nft_ctx {
	const struct nft_af_info	*afi;
	const struct nft_table		*table;
	const struct nft_chain		*chain;
};

enum nft_data_types {
	NFT_DATA_VALUE,
	NFT_DATA_VERDICT,
};

struct nft_data_desc {
	enum nft_data_types		type;
	unsigned int			len;
};

extern int nft_data_init(const struct nft_ctx *ctx, struct nft_data *data,
			 struct nft_data_desc *desc, const struct nlattr *nla);
extern void nft_data_uninit(const struct nft_data *data,
			    enum nft_data_types type);
extern int nft_data_dump(struct sk_buff *skb, int attr,
			 const struct nft_data *data,
			 enum nft_data_types type, unsigned int len);

static inline enum nft_data_types nft_dreg_to_type(enum nft_registers reg)
{
	return reg == NFT_REG_VERDICT ? NFT_DATA_VERDICT : NFT_DATA_VALUE;
}

extern int nft_validate_input_register(enum nft_registers reg);
extern int nft_validate_output_register(enum nft_registers reg);
extern int nft_validate_data_load(const struct nft_ctx *ctx,
				  enum nft_registers reg,
				  const struct nft_data *data,
				  enum nft_data_types type);

/**
 *	struct nft_expr_ops - nf_tables expression operations
 *
 *	@eval: Expression evaluation function
 *	@init: initialization function
 *	@destroy: destruction function
 *	@dump: function to dump parameters
 *	@list: used internally
 *	@name: Identifier
 *	@owner: module reference
 *	@policy: netlink attribute policy
 *	@maxattr: highest netlink attribute number
 *	@size: full expression size, including private data size
 */
struct nft_expr;
struct nft_expr_ops {
	void				(*eval)(const struct nft_expr *expr,
						struct nft_data data[NFT_REG_MAX + 1],
						const struct nft_pktinfo *pkt);
	int				(*init)(const struct nft_ctx *ctx,
						const struct nft_expr *expr,
						const struct nlattr * const tb[]);
	void				(*destroy)(const struct nft_expr *expr);
	int				(*dump)(struct sk_buff *skb,
						const struct nft_expr *expr);

	struct list_head		list;
	const char			*name;
	struct module			*owner;
	const struct nla_policy		*policy;
	unsigned int			maxattr;
	unsigned int			size;
};

#define NFT_EXPR_SIZE(size)		(sizeof(struct nft_expr) + \
					 ALIGN(size, __alignof__(struct nft_expr)))

/**
 *	struct nft_expr - nf_tables expression
 *
 *	@ops: expression ops
 *	@data: expression private data
 */
struct nft_expr {
	const struct nft_expr_ops	*ops;
	unsigned char			data[];
};

static inline void *nft_expr_priv(const struct nft_expr *expr)
{
	return (void *)expr->data;
}

/**
 *	struct nft_rule - nf_tables rule
 *
 *	@list: used internally
 *	@rcu_head: used internally for rcu
 *	@handle: rule handle
 *	@dlen: length of expression data
 *	@data: expression data
 */
struct nft_rule {
	struct list_head		list;
	struct rcu_head			rcu_head;
	u64				handle:48,
					dlen:16;
	unsigned char			data[]
		__attribute__((aligned(__alignof__(struct nft_expr))));
};

static inline struct nft_expr *nft_expr_first(const struct nft_rule *rule)
{
	return (struct nft_expr *)&rule->data[0];
}

static inline struct nft_expr *nft_expr_next(const struct nft_expr *expr)
{
	return ((void *)expr) + expr->ops->size;
}

static inline struct nft_expr *nft_expr_last(const struct nft_rule *rule)
{
	return (struct nft_expr *)&rule->data[rule->dlen];
}

/*
 * The last pointer isn't really necessary, but the compiler isn't able to
 * determine that the result of nft_expr_last() is always the same since it
 * can't assume that the dlen value wasn't changed within calls in the loop.
 */
#define nft_rule_for_each_expr(expr, last, rule) \
	for ((expr) = nft_expr_first(rule), (last) = nft_expr_last(rule); \
	     (expr) != (last); \
	     (expr) = nft_expr_next(expr))

enum nft_chain_flags {
	NFT_BASE_CHAIN			= 0x1,
	NFT_CHAIN_BUILTIN		= 0x2,
};

/**
 *	struct nft_chain - nf_tables chain
 *
 *	@rules: list of rules in the chain
 *	@list: used internally
 *	@rcu_head: used internally
 *	@handle: chain handle
 *	@flags: bitmask of enum nft_chain_flags
 *	@use: number of jump references to this chain
 *	@level: length of longest path to this chain
 *	@name: name of the chain
 */
struct nft_chain {
	struct list_head		rules;
	struct list_head		list;
	struct rcu_head			rcu_head;
	u64				handle;
	u8				flags;
	u16				use;
	u16				level;
	char				name[NFT_CHAIN_MAXNAMELEN];
};

/**
 *	struct nft_base_chain - nf_tables base chain
 *
 *	@ops: netfilter hook ops
 *	@chain: the chain
 */
struct nft_base_chain {
	struct nf_hook_ops		ops;
	struct nft_chain		chain;
};

static inline struct nft_base_chain *nft_base_chain(const struct nft_chain *chain)
{
	return container_of(chain, struct nft_base_chain, chain);
}

extern unsigned int nft_do_chain(const struct nf_hook_ops *ops,
				 struct sk_buff *skb,
				 const struct net_device *in,
				 const struct net_device *out,
				 int (*okfn)(struct sk_buff *));

enum nft_table_flags {
	NFT_TABLE_BUILTIN		= 0x1,
};

/**
 *	struct nft_table - nf_tables table
 *
 *	@list: used internally
 *	@chains: chains in the table
 *	@sets: sets in the table
 *	@hgenerator: handle generator state
 *	@use: number of chain references to this table
 *	@flags: table flag (see enum nft_table_flags)
 *	@name: name of the table
 */
struct nft_table {
	struct list_head		list;
	struct list_head		chains;
	struct list_head		sets;
	u64				hgenerator;
	u32				use;
	u16				flags;
	char				name[];
};

/**
 *	struct nft_af_info - nf_tables address family info
 *
 *	@list: used internally
 *	@family: address family
 *	@nhooks: number of hooks in this family
 *	@owner: module owner
 *	@tables: used internally
 *	@hooks: hookfn overrides for packet validation
 */
struct nft_af_info {
	struct list_head		list;
	int				family;
	unsigned int			nhooks;
	struct module			*owner;
	struct list_head		tables;
	nf_hookfn			*hooks[NF_MAX_HOOKS];
};

extern int nft_register_afinfo(struct nft_af_info *);
extern void nft_unregister_afinfo(struct nft_af_info *);

extern int nft_register_table(struct nft_table *, int family);
extern void nft_unregister_table(struct nft_table *, int family);

extern int nft_register_expr(struct nft_expr_ops *);
extern void nft_unregister_expr(struct nft_expr_ops *);

#define MODULE_ALIAS_NFT_FAMILY(family)	\
	MODULE_ALIAS("nft-afinfo-" __stringify(family))

#define MODULE_ALIAS_NFT_TABLE(family, name) \
	MODULE_ALIAS("nft-table-" __stringify(family) "-" name)

#define MODULE_ALIAS_NFT_EXPR(name) \
	MODULE_ALIAS("nft-expr-" name)

#endif /* _NET_NF_TABLES_H */
