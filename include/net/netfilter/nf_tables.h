#ifndef _NET_NF_TABLES_H
#define _NET_NF_TABLES_H

#include <linux/list.h>
#include <linux/netfilter.h>
#include <linux/netfilter/nfnetlink.h>
#include <linux/netfilter/x_tables.h>
#include <linux/netfilter/nf_tables.h>
#include <linux/u64_stats_sync.h>
#include <net/netlink.h>

#define NFT_JUMP_STACK_SIZE	16

struct nft_pktinfo {
	struct sk_buff			*skb;
	const struct net_device		*in;
	const struct net_device		*out;
	const struct nf_hook_ops	*ops;
	u8				nhoff;
	u8				thoff;
	u8				tprot;
	/* for x_tables compatibility */
	struct xt_action_param		xt;
};

static inline void nft_set_pktinfo(struct nft_pktinfo *pkt,
				   const struct nf_hook_ops *ops,
				   struct sk_buff *skb,
				   const struct net_device *in,
				   const struct net_device *out)
{
	pkt->skb = skb;
	pkt->in = pkt->xt.in = in;
	pkt->out = pkt->xt.out = out;
	pkt->ops = ops;
	pkt->xt.hooknum = ops->hooknum;
	pkt->xt.family = ops->pf;
}

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
 *	struct nft_ctx - nf_tables rule/set context
 *
 *	@net: net namespace
 * 	@afi: address family info
 * 	@table: the table the chain is contained in
 * 	@chain: the chain the rule is contained in
 *	@nla: netlink attributes
 *	@portid: netlink portID of the original message
 *	@seq: netlink sequence number
 *	@report: notify via unicast netlink message
 */
struct nft_ctx {
	struct net			*net;
	struct nft_af_info		*afi;
	struct nft_table		*table;
	struct nft_chain		*chain;
	const struct nlattr * const 	*nla;
	u32				portid;
	u32				seq;
	bool				report;
};

struct nft_data_desc {
	enum nft_data_types		type;
	unsigned int			len;
};

int nft_data_init(const struct nft_ctx *ctx, struct nft_data *data,
		  struct nft_data_desc *desc, const struct nlattr *nla);
void nft_data_uninit(const struct nft_data *data, enum nft_data_types type);
int nft_data_dump(struct sk_buff *skb, int attr, const struct nft_data *data,
		  enum nft_data_types type, unsigned int len);

static inline enum nft_data_types nft_dreg_to_type(enum nft_registers reg)
{
	return reg == NFT_REG_VERDICT ? NFT_DATA_VERDICT : NFT_DATA_VALUE;
}

static inline enum nft_registers nft_type_to_reg(enum nft_data_types type)
{
	return type == NFT_DATA_VERDICT ? NFT_REG_VERDICT : NFT_REG_1;
}

int nft_validate_input_register(enum nft_registers reg);
int nft_validate_output_register(enum nft_registers reg);
int nft_validate_data_load(const struct nft_ctx *ctx, enum nft_registers reg,
			   const struct nft_data *data,
			   enum nft_data_types type);

/**
 *	struct nft_set_elem - generic representation of set elements
 *
 *	@cookie: implementation specific element cookie
 *	@key: element key
 *	@data: element data (maps only)
 *	@flags: element flags (end of interval)
 *
 *	The cookie can be used to store a handle to the element for subsequent
 *	removal.
 */
struct nft_set_elem {
	void			*cookie;
	struct nft_data		key;
	struct nft_data		data;
	u32			flags;
};

struct nft_set;
struct nft_set_iter {
	unsigned int	count;
	unsigned int	skip;
	int		err;
	int		(*fn)(const struct nft_ctx *ctx,
			      const struct nft_set *set,
			      const struct nft_set_iter *iter,
			      const struct nft_set_elem *elem);
};

/**
 *	struct nft_set_desc - description of set elements
 *
 *	@klen: key length
 *	@dlen: data length
 *	@size: number of set elements
 */
struct nft_set_desc {
	unsigned int		klen;
	unsigned int		dlen;
	unsigned int		size;
};

/**
 *	enum nft_set_class - performance class
 *
 *	@NFT_LOOKUP_O_1: constant, O(1)
 *	@NFT_LOOKUP_O_LOG_N: logarithmic, O(log N)
 *	@NFT_LOOKUP_O_N: linear, O(N)
 */
enum nft_set_class {
	NFT_SET_CLASS_O_1,
	NFT_SET_CLASS_O_LOG_N,
	NFT_SET_CLASS_O_N,
};

/**
 *	struct nft_set_estimate - estimation of memory and performance
 *				  characteristics
 *
 *	@size: required memory
 *	@class: lookup performance class
 */
struct nft_set_estimate {
	unsigned int		size;
	enum nft_set_class	class;
};

/**
 *	struct nft_set_ops - nf_tables set operations
 *
 *	@lookup: look up an element within the set
 *	@insert: insert new element into set
 *	@remove: remove element from set
 *	@walk: iterate over all set elemeennts
 *	@privsize: function to return size of set private data
 *	@init: initialize private data of new set instance
 *	@destroy: destroy private data of set instance
 *	@list: nf_tables_set_ops list node
 *	@owner: module reference
 *	@features: features supported by the implementation
 */
struct nft_set_ops {
	bool				(*lookup)(const struct nft_set *set,
						  const struct nft_data *key,
						  struct nft_data *data);
	int				(*get)(const struct nft_set *set,
					       struct nft_set_elem *elem);
	int				(*insert)(const struct nft_set *set,
						  const struct nft_set_elem *elem);
	void				(*remove)(const struct nft_set *set,
						  const struct nft_set_elem *elem);
	void				(*walk)(const struct nft_ctx *ctx,
						const struct nft_set *set,
						struct nft_set_iter *iter);

	unsigned int			(*privsize)(const struct nlattr * const nla[]);
	bool				(*estimate)(const struct nft_set_desc *desc,
						    u32 features,
						    struct nft_set_estimate *est);
	int				(*init)(const struct nft_set *set,
						const struct nft_set_desc *desc,
						const struct nlattr * const nla[]);
	void				(*destroy)(const struct nft_set *set);

	struct list_head		list;
	struct module			*owner;
	u32				features;
};

int nft_register_set(struct nft_set_ops *ops);
void nft_unregister_set(struct nft_set_ops *ops);

/**
 * 	struct nft_set - nf_tables set instance
 *
 *	@list: table set list node
 *	@bindings: list of set bindings
 * 	@name: name of the set
 * 	@ktype: key type (numeric type defined by userspace, not used in the kernel)
 * 	@dtype: data type (verdict or numeric type defined by userspace)
 * 	@size: maximum set size
 * 	@nelems: number of elements
 *	@policy: set parameterization (see enum nft_set_policies)
 * 	@ops: set ops
 * 	@flags: set flags
 * 	@klen: key length
 * 	@dlen: data length
 * 	@data: private set data
 */
struct nft_set {
	struct list_head		list;
	struct list_head		bindings;
	char				name[IFNAMSIZ];
	u32				ktype;
	u32				dtype;
	u32				size;
	u32				nelems;
	u16				policy;
	/* runtime data below here */
	const struct nft_set_ops	*ops ____cacheline_aligned;
	u16				flags;
	u8				klen;
	u8				dlen;
	unsigned char			data[]
		__attribute__((aligned(__alignof__(u64))));
};

static inline void *nft_set_priv(const struct nft_set *set)
{
	return (void *)set->data;
}

struct nft_set *nf_tables_set_lookup(const struct nft_table *table,
				     const struct nlattr *nla);
struct nft_set *nf_tables_set_lookup_byid(const struct net *net,
					  const struct nlattr *nla);

/**
 *	struct nft_set_binding - nf_tables set binding
 *
 *	@list: set bindings list node
 *	@chain: chain containing the rule bound to the set
 *
 *	A set binding contains all information necessary for validation
 *	of new elements added to a bound set.
 */
struct nft_set_binding {
	struct list_head		list;
	const struct nft_chain		*chain;
};

int nf_tables_bind_set(const struct nft_ctx *ctx, struct nft_set *set,
		       struct nft_set_binding *binding);
void nf_tables_unbind_set(const struct nft_ctx *ctx, struct nft_set *set,
			  struct nft_set_binding *binding);


/**
 *	struct nft_expr_type - nf_tables expression type
 *
 *	@select_ops: function to select nft_expr_ops
 *	@ops: default ops, used when no select_ops functions is present
 *	@list: used internally
 *	@name: Identifier
 *	@owner: module reference
 *	@policy: netlink attribute policy
 *	@maxattr: highest netlink attribute number
 *	@family: address family for AF-specific types
 */
struct nft_expr_type {
	const struct nft_expr_ops	*(*select_ops)(const struct nft_ctx *,
						       const struct nlattr * const tb[]);
	const struct nft_expr_ops	*ops;
	struct list_head		list;
	const char			*name;
	struct module			*owner;
	const struct nla_policy		*policy;
	unsigned int			maxattr;
	u8				family;
};

/**
 *	struct nft_expr_ops - nf_tables expression operations
 *
 *	@eval: Expression evaluation function
 *	@size: full expression size, including private data size
 *	@init: initialization function
 *	@destroy: destruction function
 *	@dump: function to dump parameters
 *	@type: expression type
 *	@validate: validate expression, called during loop detection
 *	@data: extra data to attach to this expression operation
 */
struct nft_expr;
struct nft_expr_ops {
	void				(*eval)(const struct nft_expr *expr,
						struct nft_data data[NFT_REG_MAX + 1],
						const struct nft_pktinfo *pkt);
	unsigned int			size;

	int				(*init)(const struct nft_ctx *ctx,
						const struct nft_expr *expr,
						const struct nlattr * const tb[]);
	void				(*destroy)(const struct nft_ctx *ctx,
						   const struct nft_expr *expr);
	int				(*dump)(struct sk_buff *skb,
						const struct nft_expr *expr);
	int				(*validate)(const struct nft_ctx *ctx,
						    const struct nft_expr *expr,
						    const struct nft_data **data);
	const struct nft_expr_type	*type;
	void				*data;
};

#define NFT_EXPR_MAXATTR		16
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
 *	@handle: rule handle
 *	@genmask: generation mask
 *	@dlen: length of expression data
 *	@ulen: length of user data (used for comments)
 *	@data: expression data
 */
struct nft_rule {
	struct list_head		list;
	u64				handle:42,
					genmask:2,
					dlen:12,
					ulen:8;
	unsigned char			data[]
		__attribute__((aligned(__alignof__(struct nft_expr))));
};

/**
 *	struct nft_trans - nf_tables object update in transaction
 *
 *	@rcu_head: rcu head to defer release of transaction data
 *	@list: used internally
 *	@msg_type: message type
 *	@ctx: transaction context
 *	@data: internal information related to the transaction
 */
struct nft_trans {
	struct rcu_head			rcu_head;
	struct list_head		list;
	int				msg_type;
	struct nft_ctx			ctx;
	char				data[0];
};

struct nft_trans_rule {
	struct nft_rule			*rule;
};

#define nft_trans_rule(trans)	\
	(((struct nft_trans_rule *)trans->data)->rule)

struct nft_trans_set {
	struct nft_set	*set;
	u32		set_id;
};

#define nft_trans_set(trans)	\
	(((struct nft_trans_set *)trans->data)->set)
#define nft_trans_set_id(trans)	\
	(((struct nft_trans_set *)trans->data)->set_id)

struct nft_trans_chain {
	bool		update;
	char		name[NFT_CHAIN_MAXNAMELEN];
	struct nft_stats __percpu *stats;
	u8		policy;
};

#define nft_trans_chain_update(trans)	\
	(((struct nft_trans_chain *)trans->data)->update)
#define nft_trans_chain_name(trans)	\
	(((struct nft_trans_chain *)trans->data)->name)
#define nft_trans_chain_stats(trans)	\
	(((struct nft_trans_chain *)trans->data)->stats)
#define nft_trans_chain_policy(trans)	\
	(((struct nft_trans_chain *)trans->data)->policy)

struct nft_trans_table {
	bool		update;
	bool		enable;
};

#define nft_trans_table_update(trans)	\
	(((struct nft_trans_table *)trans->data)->update)
#define nft_trans_table_enable(trans)	\
	(((struct nft_trans_table *)trans->data)->enable)

struct nft_trans_elem {
	struct nft_set		*set;
	struct nft_set_elem	elem;
};

#define nft_trans_elem_set(trans)	\
	(((struct nft_trans_elem *)trans->data)->set)
#define nft_trans_elem(trans)	\
	(((struct nft_trans_elem *)trans->data)->elem)

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

static inline void *nft_userdata(const struct nft_rule *rule)
{
	return (void *)&rule->data[rule->dlen];
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
	NFT_CHAIN_INACTIVE		= 0x2,
};

/**
 *	struct nft_chain - nf_tables chain
 *
 *	@rules: list of rules in the chain
 *	@list: used internally
 *	@net: net namespace that this chain belongs to
 *	@table: table that this chain belongs to
 *	@handle: chain handle
 *	@use: number of jump references to this chain
 *	@level: length of longest path to this chain
 *	@flags: bitmask of enum nft_chain_flags
 *	@name: name of the chain
 */
struct nft_chain {
	struct list_head		rules;
	struct list_head		list;
	struct net			*net;
	struct nft_table		*table;
	u64				handle;
	u32				use;
	u16				level;
	u8				flags;
	char				name[NFT_CHAIN_MAXNAMELEN];
};

enum nft_chain_type {
	NFT_CHAIN_T_DEFAULT = 0,
	NFT_CHAIN_T_ROUTE,
	NFT_CHAIN_T_NAT,
	NFT_CHAIN_T_MAX
};

struct nft_stats {
	u64			bytes;
	u64			pkts;
	struct u64_stats_sync	syncp;
};

#define NFT_HOOK_OPS_MAX		2

/**
 *	struct nft_base_chain - nf_tables base chain
 *
 *	@ops: netfilter hook ops
 *	@type: chain type
 *	@policy: default policy
 *	@stats: per-cpu chain stats
 *	@chain: the chain
 */
struct nft_base_chain {
	struct nf_hook_ops		ops[NFT_HOOK_OPS_MAX];
	const struct nf_chain_type	*type;
	u8				policy;
	struct nft_stats __percpu	*stats;
	struct nft_chain		chain;
};

static inline struct nft_base_chain *nft_base_chain(const struct nft_chain *chain)
{
	return container_of(chain, struct nft_base_chain, chain);
}

unsigned int nft_do_chain(struct nft_pktinfo *pkt,
			  const struct nf_hook_ops *ops);

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
 *	@nops: number of hook ops in this family
 *	@hook_ops_init: initialization function for chain hook ops
 *	@hooks: hookfn overrides for packet validation
 */
struct nft_af_info {
	struct list_head		list;
	int				family;
	unsigned int			nhooks;
	struct module			*owner;
	struct list_head		tables;
	unsigned int			nops;
	void				(*hook_ops_init)(struct nf_hook_ops *,
							 unsigned int);
	nf_hookfn			*hooks[NF_MAX_HOOKS];
};

int nft_register_afinfo(struct net *, struct nft_af_info *);
void nft_unregister_afinfo(struct nft_af_info *);

/**
 * 	struct nf_chain_type - nf_tables chain type info
 *
 * 	@name: name of the type
 * 	@type: numeric identifier
 * 	@family: address family
 * 	@owner: module owner
 * 	@hook_mask: mask of valid hooks
 * 	@hooks: hookfn overrides
 */
struct nf_chain_type {
	const char			*name;
	enum nft_chain_type		type;
	int				family;
	struct module			*owner;
	unsigned int			hook_mask;
	nf_hookfn			*hooks[NF_MAX_HOOKS];
};

int nft_register_chain_type(const struct nf_chain_type *);
void nft_unregister_chain_type(const struct nf_chain_type *);

int nft_register_expr(struct nft_expr_type *);
void nft_unregister_expr(struct nft_expr_type *);

#define nft_dereference(p)					\
	nfnl_dereference(p, NFNL_SUBSYS_NFTABLES)

#define MODULE_ALIAS_NFT_FAMILY(family)	\
	MODULE_ALIAS("nft-afinfo-" __stringify(family))

#define MODULE_ALIAS_NFT_CHAIN(family, name) \
	MODULE_ALIAS("nft-chain-" __stringify(family) "-" name)

#define MODULE_ALIAS_NFT_AF_EXPR(family, name) \
	MODULE_ALIAS("nft-expr-" __stringify(family) "-" name)

#define MODULE_ALIAS_NFT_EXPR(name) \
	MODULE_ALIAS("nft-expr-" name)

#define MODULE_ALIAS_NFT_SET() \
	MODULE_ALIAS("nft-set")

#endif /* _NET_NF_TABLES_H */
