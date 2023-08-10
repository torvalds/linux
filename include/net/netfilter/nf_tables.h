/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _NET_NF_TABLES_H
#define _NET_NF_TABLES_H

#include <asm/unaligned.h>
#include <linux/list.h>
#include <linux/netfilter.h>
#include <linux/netfilter/nfnetlink.h>
#include <linux/netfilter/x_tables.h>
#include <linux/netfilter/nf_tables.h>
#include <linux/u64_stats_sync.h>
#include <linux/rhashtable.h>
#include <net/netfilter/nf_flow_table.h>
#include <net/netlink.h>
#include <net/flow_offload.h>
#include <net/netns/generic.h>

#define NFT_MAX_HOOKS	(NF_INET_INGRESS + 1)

struct module;

#define NFT_JUMP_STACK_SIZE	16

enum {
	NFT_PKTINFO_L4PROTO	= (1 << 0),
	NFT_PKTINFO_INNER	= (1 << 1),
};

struct nft_pktinfo {
	struct sk_buff			*skb;
	const struct nf_hook_state	*state;
	u8				flags;
	u8				tprot;
	u16				fragoff;
	unsigned int			thoff;
	unsigned int			inneroff;
};

static inline struct sock *nft_sk(const struct nft_pktinfo *pkt)
{
	return pkt->state->sk;
}

static inline unsigned int nft_thoff(const struct nft_pktinfo *pkt)
{
	return pkt->thoff;
}

static inline struct net *nft_net(const struct nft_pktinfo *pkt)
{
	return pkt->state->net;
}

static inline unsigned int nft_hook(const struct nft_pktinfo *pkt)
{
	return pkt->state->hook;
}

static inline u8 nft_pf(const struct nft_pktinfo *pkt)
{
	return pkt->state->pf;
}

static inline const struct net_device *nft_in(const struct nft_pktinfo *pkt)
{
	return pkt->state->in;
}

static inline const struct net_device *nft_out(const struct nft_pktinfo *pkt)
{
	return pkt->state->out;
}

static inline void nft_set_pktinfo(struct nft_pktinfo *pkt,
				   struct sk_buff *skb,
				   const struct nf_hook_state *state)
{
	pkt->skb = skb;
	pkt->state = state;
}

static inline void nft_set_pktinfo_unspec(struct nft_pktinfo *pkt)
{
	pkt->flags = 0;
	pkt->tprot = 0;
	pkt->thoff = 0;
	pkt->fragoff = 0;
}

/**
 * 	struct nft_verdict - nf_tables verdict
 *
 * 	@code: nf_tables/netfilter verdict code
 * 	@chain: destination chain for NFT_JUMP/NFT_GOTO
 */
struct nft_verdict {
	u32				code;
	struct nft_chain		*chain;
};

struct nft_data {
	union {
		u32			data[4];
		struct nft_verdict	verdict;
	};
} __attribute__((aligned(__alignof__(u64))));

#define NFT_REG32_NUM		20

/**
 *	struct nft_regs - nf_tables register set
 *
 *	@data: data registers
 *	@verdict: verdict register
 *
 *	The first four data registers alias to the verdict register.
 */
struct nft_regs {
	union {
		u32			data[NFT_REG32_NUM];
		struct nft_verdict	verdict;
	};
};

struct nft_regs_track {
	struct {
		const struct nft_expr		*selector;
		const struct nft_expr		*bitwise;
		u8				num_reg;
	} regs[NFT_REG32_NUM];

	const struct nft_expr			*cur;
	const struct nft_expr			*last;
};

/* Store/load an u8, u16 or u64 integer to/from the u32 data register.
 *
 * Note, when using concatenations, register allocation happens at 32-bit
 * level. So for store instruction, pad the rest part with zero to avoid
 * garbage values.
 */

static inline void nft_reg_store8(u32 *dreg, u8 val)
{
	*dreg = 0;
	*(u8 *)dreg = val;
}

static inline u8 nft_reg_load8(const u32 *sreg)
{
	return *(u8 *)sreg;
}

static inline void nft_reg_store16(u32 *dreg, u16 val)
{
	*dreg = 0;
	*(u16 *)dreg = val;
}

static inline void nft_reg_store_be16(u32 *dreg, __be16 val)
{
	nft_reg_store16(dreg, (__force __u16)val);
}

static inline u16 nft_reg_load16(const u32 *sreg)
{
	return *(u16 *)sreg;
}

static inline __be16 nft_reg_load_be16(const u32 *sreg)
{
	return (__force __be16)nft_reg_load16(sreg);
}

static inline __be32 nft_reg_load_be32(const u32 *sreg)
{
	return *(__force __be32 *)sreg;
}

static inline void nft_reg_store64(u32 *dreg, u64 val)
{
	put_unaligned(val, (u64 *)dreg);
}

static inline u64 nft_reg_load64(const u32 *sreg)
{
	return get_unaligned((u64 *)sreg);
}

static inline void nft_data_copy(u32 *dst, const struct nft_data *src,
				 unsigned int len)
{
	if (len % NFT_REG32_SIZE)
		dst[len / NFT_REG32_SIZE] = 0;
	memcpy(dst, src, len);
}

/**
 *	struct nft_ctx - nf_tables rule/set context
 *
 *	@net: net namespace
 * 	@table: the table the chain is contained in
 * 	@chain: the chain the rule is contained in
 *	@nla: netlink attributes
 *	@portid: netlink portID of the original message
 *	@seq: netlink sequence number
 *	@family: protocol family
 *	@level: depth of the chains
 *	@report: notify via unicast netlink message
 */
struct nft_ctx {
	struct net			*net;
	struct nft_table		*table;
	struct nft_chain		*chain;
	const struct nlattr * const 	*nla;
	u32				portid;
	u32				seq;
	u16				flags;
	u8				family;
	u8				level;
	bool				report;
};

enum nft_data_desc_flags {
	NFT_DATA_DESC_SETELEM	= (1 << 0),
};

struct nft_data_desc {
	enum nft_data_types		type;
	unsigned int			size;
	unsigned int			len;
	unsigned int			flags;
};

int nft_data_init(const struct nft_ctx *ctx, struct nft_data *data,
		  struct nft_data_desc *desc, const struct nlattr *nla);
void nft_data_hold(const struct nft_data *data, enum nft_data_types type);
void nft_data_release(const struct nft_data *data, enum nft_data_types type);
int nft_data_dump(struct sk_buff *skb, int attr, const struct nft_data *data,
		  enum nft_data_types type, unsigned int len);

static inline enum nft_data_types nft_dreg_to_type(enum nft_registers reg)
{
	return reg == NFT_REG_VERDICT ? NFT_DATA_VERDICT : NFT_DATA_VALUE;
}

static inline enum nft_registers nft_type_to_reg(enum nft_data_types type)
{
	return type == NFT_DATA_VERDICT ? NFT_REG_VERDICT : NFT_REG_1 * NFT_REG_SIZE / NFT_REG32_SIZE;
}

int nft_parse_u32_check(const struct nlattr *attr, int max, u32 *dest);
int nft_dump_register(struct sk_buff *skb, unsigned int attr, unsigned int reg);

int nft_parse_register_load(const struct nlattr *attr, u8 *sreg, u32 len);
int nft_parse_register_store(const struct nft_ctx *ctx,
			     const struct nlattr *attr, u8 *dreg,
			     const struct nft_data *data,
			     enum nft_data_types type, unsigned int len);

/**
 *	struct nft_userdata - user defined data associated with an object
 *
 *	@len: length of the data
 *	@data: content
 *
 *	The presence of user data is indicated in an object specific fashion,
 *	so a length of zero can't occur and the value "len" indicates data
 *	of length len + 1.
 */
struct nft_userdata {
	u8			len;
	unsigned char		data[];
};

/**
 *	struct nft_set_elem - generic representation of set elements
 *
 *	@key: element key
 *	@key_end: closing element key
 *	@priv: element private data and extensions
 */
struct nft_set_elem {
	union {
		u32		buf[NFT_DATA_VALUE_MAXLEN / sizeof(u32)];
		struct nft_data	val;
	} key;
	union {
		u32		buf[NFT_DATA_VALUE_MAXLEN / sizeof(u32)];
		struct nft_data	val;
	} key_end;
	union {
		u32		buf[NFT_DATA_VALUE_MAXLEN / sizeof(u32)];
		struct nft_data val;
	} data;
	void			*priv;
};

struct nft_set;
struct nft_set_iter {
	u8		genmask;
	unsigned int	count;
	unsigned int	skip;
	int		err;
	int		(*fn)(const struct nft_ctx *ctx,
			      struct nft_set *set,
			      const struct nft_set_iter *iter,
			      struct nft_set_elem *elem);
};

/**
 *	struct nft_set_desc - description of set elements
 *
 *	@ktype: key type
 *	@klen: key length
 *	@dtype: data type
 *	@dlen: data length
 *	@objtype: object type
 *	@flags: flags
 *	@size: number of set elements
 *	@policy: set policy
 *	@gc_int: garbage collector interval
 *	@field_len: length of each field in concatenation, bytes
 *	@field_count: number of concatenated fields in element
 *	@expr: set must support for expressions
 */
struct nft_set_desc {
	u32			ktype;
	unsigned int		klen;
	u32			dtype;
	unsigned int		dlen;
	u32			objtype;
	unsigned int		size;
	u32			policy;
	u32			gc_int;
	u64			timeout;
	u8			field_len[NFT_REG32_COUNT];
	u8			field_count;
	bool			expr;
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
 *	@lookup: lookup performance class
 *	@space: memory class
 */
struct nft_set_estimate {
	u64			size;
	enum nft_set_class	lookup;
	enum nft_set_class	space;
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
	unsigned char			data[]
		__attribute__((aligned(__alignof__(u64))));
};

static inline void *nft_expr_priv(const struct nft_expr *expr)
{
	return (void *)expr->data;
}

int nft_expr_clone(struct nft_expr *dst, struct nft_expr *src);
void nft_expr_destroy(const struct nft_ctx *ctx, struct nft_expr *expr);
int nft_expr_dump(struct sk_buff *skb, unsigned int attr,
		  const struct nft_expr *expr);
bool nft_expr_reduce_bitwise(struct nft_regs_track *track,
			     const struct nft_expr *expr);

struct nft_set_ext;

/**
 *	struct nft_set_ops - nf_tables set operations
 *
 *	@lookup: look up an element within the set
 *	@update: update an element if exists, add it if doesn't exist
 *	@delete: delete an element
 *	@insert: insert new element into set
 *	@activate: activate new element in the next generation
 *	@deactivate: lookup for element and deactivate it in the next generation
 *	@flush: deactivate element in the next generation
 *	@remove: remove element from set
 *	@walk: iterate over all set elements
 *	@get: get set elements
 *	@privsize: function to return size of set private data
 *	@init: initialize private data of new set instance
 *	@destroy: destroy private data of set instance
 *	@elemsize: element private size
 *
 *	Operations lookup, update and delete have simpler interfaces, are faster
 *	and currently only used in the packet path. All the rest are slower,
 *	control plane functions.
 */
struct nft_set_ops {
	bool				(*lookup)(const struct net *net,
						  const struct nft_set *set,
						  const u32 *key,
						  const struct nft_set_ext **ext);
	bool				(*update)(struct nft_set *set,
						  const u32 *key,
						  void *(*new)(struct nft_set *,
							       const struct nft_expr *,
							       struct nft_regs *),
						  const struct nft_expr *expr,
						  struct nft_regs *regs,
						  const struct nft_set_ext **ext);
	bool				(*delete)(const struct nft_set *set,
						  const u32 *key);

	int				(*insert)(const struct net *net,
						  const struct nft_set *set,
						  const struct nft_set_elem *elem,
						  struct nft_set_ext **ext);
	void				(*activate)(const struct net *net,
						    const struct nft_set *set,
						    const struct nft_set_elem *elem);
	void *				(*deactivate)(const struct net *net,
						      const struct nft_set *set,
						      const struct nft_set_elem *elem);
	bool				(*flush)(const struct net *net,
						 const struct nft_set *set,
						 void *priv);
	void				(*remove)(const struct net *net,
						  const struct nft_set *set,
						  const struct nft_set_elem *elem);
	void				(*walk)(const struct nft_ctx *ctx,
						struct nft_set *set,
						struct nft_set_iter *iter);
	void *				(*get)(const struct net *net,
					       const struct nft_set *set,
					       const struct nft_set_elem *elem,
					       unsigned int flags);
	void				(*commit)(const struct nft_set *set);
	void				(*abort)(const struct nft_set *set);
	u64				(*privsize)(const struct nlattr * const nla[],
						    const struct nft_set_desc *desc);
	bool				(*estimate)(const struct nft_set_desc *desc,
						    u32 features,
						    struct nft_set_estimate *est);
	int				(*init)(const struct nft_set *set,
						const struct nft_set_desc *desc,
						const struct nlattr * const nla[]);
	void				(*destroy)(const struct nft_ctx *ctx,
						   const struct nft_set *set);
	void				(*gc_init)(const struct nft_set *set);

	unsigned int			elemsize;
};

/**
 *      struct nft_set_type - nf_tables set type
 *
 *      @ops: set ops for this type
 *      @features: features supported by the implementation
 */
struct nft_set_type {
	const struct nft_set_ops	ops;
	u32				features;
};
#define to_set_type(o) container_of(o, struct nft_set_type, ops)

struct nft_set_elem_expr {
	u8				size;
	unsigned char			data[]
		__attribute__((aligned(__alignof__(struct nft_expr))));
};

#define nft_setelem_expr_at(__elem_expr, __offset)			\
	((struct nft_expr *)&__elem_expr->data[__offset])

#define nft_setelem_expr_foreach(__expr, __elem_expr, __size)		\
	for (__expr = nft_setelem_expr_at(__elem_expr, 0), __size = 0;	\
	     __size < (__elem_expr)->size;				\
	     __size += (__expr)->ops->size, __expr = ((void *)(__expr)) + (__expr)->ops->size)

#define NFT_SET_EXPR_MAX	2

/**
 * 	struct nft_set - nf_tables set instance
 *
 *	@list: table set list node
 *	@bindings: list of set bindings
 *	@refs: internal refcounting for async set destruction
 *	@table: table this set belongs to
 *	@net: netnamespace this set belongs to
 * 	@name: name of the set
 *	@handle: unique handle of the set
 * 	@ktype: key type (numeric type defined by userspace, not used in the kernel)
 * 	@dtype: data type (verdict or numeric type defined by userspace)
 * 	@objtype: object type (see NFT_OBJECT_* definitions)
 * 	@size: maximum set size
 *	@field_len: length of each field in concatenation, bytes
 *	@field_count: number of concatenated fields in element
 *	@use: number of rules references to this set
 * 	@nelems: number of elements
 * 	@ndeact: number of deactivated elements queued for removal
 *	@timeout: default timeout value in jiffies
 * 	@gc_int: garbage collection interval in msecs
 *	@policy: set parameterization (see enum nft_set_policies)
 *	@udlen: user data length
 *	@udata: user data
 *	@expr: stateful expression
 * 	@ops: set ops
 * 	@flags: set flags
 *	@dead: set will be freed, never cleared
 *	@genmask: generation mask
 * 	@klen: key length
 * 	@dlen: data length
 * 	@data: private set data
 */
struct nft_set {
	struct list_head		list;
	struct list_head		bindings;
	refcount_t			refs;
	struct nft_table		*table;
	possible_net_t			net;
	char				*name;
	u64				handle;
	u32				ktype;
	u32				dtype;
	u32				objtype;
	u32				size;
	u8				field_len[NFT_REG32_COUNT];
	u8				field_count;
	u32				use;
	atomic_t			nelems;
	u32				ndeact;
	u64				timeout;
	u32				gc_int;
	u16				policy;
	u16				udlen;
	unsigned char			*udata;
	struct list_head		pending_update;
	/* runtime data below here */
	const struct nft_set_ops	*ops ____cacheline_aligned;
	u16				flags:13,
					dead:1,
					genmask:2;
	u8				klen;
	u8				dlen;
	u8				num_exprs;
	struct nft_expr			*exprs[NFT_SET_EXPR_MAX];
	struct list_head		catchall_list;
	unsigned char			data[]
		__attribute__((aligned(__alignof__(u64))));
};

static inline bool nft_set_is_anonymous(const struct nft_set *set)
{
	return set->flags & NFT_SET_ANONYMOUS;
}

static inline void *nft_set_priv(const struct nft_set *set)
{
	return (void *)set->data;
}

static inline bool nft_set_gc_is_pending(const struct nft_set *s)
{
	return refcount_read(&s->refs) != 1;
}

static inline struct nft_set *nft_set_container_of(const void *priv)
{
	return (void *)priv - offsetof(struct nft_set, data);
}

struct nft_set *nft_set_lookup_global(const struct net *net,
				      const struct nft_table *table,
				      const struct nlattr *nla_set_name,
				      const struct nlattr *nla_set_id,
				      u8 genmask);

struct nft_set_ext *nft_set_catchall_lookup(const struct net *net,
					    const struct nft_set *set);

static inline unsigned long nft_set_gc_interval(const struct nft_set *set)
{
	u32 gc_int = READ_ONCE(set->gc_int);

	return gc_int ? msecs_to_jiffies(gc_int) : HZ;
}

/**
 *	struct nft_set_binding - nf_tables set binding
 *
 *	@list: set bindings list node
 *	@chain: chain containing the rule bound to the set
 *	@flags: set action flags
 *
 *	A set binding contains all information necessary for validation
 *	of new elements added to a bound set.
 */
struct nft_set_binding {
	struct list_head		list;
	const struct nft_chain		*chain;
	u32				flags;
};

enum nft_trans_phase;
void nf_tables_activate_set(const struct nft_ctx *ctx, struct nft_set *set);
void nf_tables_deactivate_set(const struct nft_ctx *ctx, struct nft_set *set,
			      struct nft_set_binding *binding,
			      enum nft_trans_phase phase);
int nf_tables_bind_set(const struct nft_ctx *ctx, struct nft_set *set,
		       struct nft_set_binding *binding);
void nf_tables_destroy_set(const struct nft_ctx *ctx, struct nft_set *set);

/**
 *	enum nft_set_extensions - set extension type IDs
 *
 *	@NFT_SET_EXT_KEY: element key
 *	@NFT_SET_EXT_KEY_END: upper bound element key, for ranges
 *	@NFT_SET_EXT_DATA: mapping data
 *	@NFT_SET_EXT_FLAGS: element flags
 *	@NFT_SET_EXT_TIMEOUT: element timeout
 *	@NFT_SET_EXT_EXPIRATION: element expiration time
 *	@NFT_SET_EXT_USERDATA: user data associated with the element
 *	@NFT_SET_EXT_EXPRESSIONS: expressions assiciated with the element
 *	@NFT_SET_EXT_OBJREF: stateful object reference associated with element
 *	@NFT_SET_EXT_NUM: number of extension types
 */
enum nft_set_extensions {
	NFT_SET_EXT_KEY,
	NFT_SET_EXT_KEY_END,
	NFT_SET_EXT_DATA,
	NFT_SET_EXT_FLAGS,
	NFT_SET_EXT_TIMEOUT,
	NFT_SET_EXT_EXPIRATION,
	NFT_SET_EXT_USERDATA,
	NFT_SET_EXT_EXPRESSIONS,
	NFT_SET_EXT_OBJREF,
	NFT_SET_EXT_NUM
};

/**
 *	struct nft_set_ext_type - set extension type
 *
 * 	@len: fixed part length of the extension
 * 	@align: alignment requirements of the extension
 */
struct nft_set_ext_type {
	u8	len;
	u8	align;
};

extern const struct nft_set_ext_type nft_set_ext_types[];

/**
 *	struct nft_set_ext_tmpl - set extension template
 *
 *	@len: length of extension area
 *	@offset: offsets of individual extension types
 */
struct nft_set_ext_tmpl {
	u16	len;
	u8	offset[NFT_SET_EXT_NUM];
	u8	ext_len[NFT_SET_EXT_NUM];
};

/**
 *	struct nft_set_ext - set extensions
 *
 *	@genmask: generation mask
 *	@offset: offsets of individual extension types
 *	@data: beginning of extension data
 */
struct nft_set_ext {
	u8	genmask;
	u8	offset[NFT_SET_EXT_NUM];
	char	data[];
};

static inline void nft_set_ext_prepare(struct nft_set_ext_tmpl *tmpl)
{
	memset(tmpl, 0, sizeof(*tmpl));
	tmpl->len = sizeof(struct nft_set_ext);
}

static inline int nft_set_ext_add_length(struct nft_set_ext_tmpl *tmpl, u8 id,
					 unsigned int len)
{
	tmpl->len	 = ALIGN(tmpl->len, nft_set_ext_types[id].align);
	if (tmpl->len > U8_MAX)
		return -EINVAL;

	tmpl->offset[id] = tmpl->len;
	tmpl->ext_len[id] = nft_set_ext_types[id].len + len;
	tmpl->len	+= tmpl->ext_len[id];

	return 0;
}

static inline int nft_set_ext_add(struct nft_set_ext_tmpl *tmpl, u8 id)
{
	return nft_set_ext_add_length(tmpl, id, 0);
}

static inline void nft_set_ext_init(struct nft_set_ext *ext,
				    const struct nft_set_ext_tmpl *tmpl)
{
	memcpy(ext->offset, tmpl->offset, sizeof(ext->offset));
}

static inline bool __nft_set_ext_exists(const struct nft_set_ext *ext, u8 id)
{
	return !!ext->offset[id];
}

static inline bool nft_set_ext_exists(const struct nft_set_ext *ext, u8 id)
{
	return ext && __nft_set_ext_exists(ext, id);
}

static inline void *nft_set_ext(const struct nft_set_ext *ext, u8 id)
{
	return (void *)ext + ext->offset[id];
}

static inline struct nft_data *nft_set_ext_key(const struct nft_set_ext *ext)
{
	return nft_set_ext(ext, NFT_SET_EXT_KEY);
}

static inline struct nft_data *nft_set_ext_key_end(const struct nft_set_ext *ext)
{
	return nft_set_ext(ext, NFT_SET_EXT_KEY_END);
}

static inline struct nft_data *nft_set_ext_data(const struct nft_set_ext *ext)
{
	return nft_set_ext(ext, NFT_SET_EXT_DATA);
}

static inline u8 *nft_set_ext_flags(const struct nft_set_ext *ext)
{
	return nft_set_ext(ext, NFT_SET_EXT_FLAGS);
}

static inline u64 *nft_set_ext_timeout(const struct nft_set_ext *ext)
{
	return nft_set_ext(ext, NFT_SET_EXT_TIMEOUT);
}

static inline u64 *nft_set_ext_expiration(const struct nft_set_ext *ext)
{
	return nft_set_ext(ext, NFT_SET_EXT_EXPIRATION);
}

static inline struct nft_userdata *nft_set_ext_userdata(const struct nft_set_ext *ext)
{
	return nft_set_ext(ext, NFT_SET_EXT_USERDATA);
}

static inline struct nft_set_elem_expr *nft_set_ext_expr(const struct nft_set_ext *ext)
{
	return nft_set_ext(ext, NFT_SET_EXT_EXPRESSIONS);
}

static inline bool nft_set_elem_expired(const struct nft_set_ext *ext)
{
	return nft_set_ext_exists(ext, NFT_SET_EXT_EXPIRATION) &&
	       time_is_before_eq_jiffies64(*nft_set_ext_expiration(ext));
}

static inline struct nft_set_ext *nft_set_elem_ext(const struct nft_set *set,
						   void *elem)
{
	return elem + set->ops->elemsize;
}

static inline struct nft_object **nft_set_ext_obj(const struct nft_set_ext *ext)
{
	return nft_set_ext(ext, NFT_SET_EXT_OBJREF);
}

struct nft_expr *nft_set_elem_expr_alloc(const struct nft_ctx *ctx,
					 const struct nft_set *set,
					 const struct nlattr *attr);

void *nft_set_elem_init(const struct nft_set *set,
			const struct nft_set_ext_tmpl *tmpl,
			const u32 *key, const u32 *key_end, const u32 *data,
			u64 timeout, u64 expiration, gfp_t gfp);
int nft_set_elem_expr_clone(const struct nft_ctx *ctx, struct nft_set *set,
			    struct nft_expr *expr_array[]);
void nft_set_elem_destroy(const struct nft_set *set, void *elem,
			  bool destroy_expr);
void nf_tables_set_elem_destroy(const struct nft_ctx *ctx,
				const struct nft_set *set, void *elem);

struct nft_expr_ops;
/**
 *	struct nft_expr_type - nf_tables expression type
 *
 *	@select_ops: function to select nft_expr_ops
 *	@release_ops: release nft_expr_ops
 *	@ops: default ops, used when no select_ops functions is present
 *	@list: used internally
 *	@name: Identifier
 *	@owner: module reference
 *	@policy: netlink attribute policy
 *	@maxattr: highest netlink attribute number
 *	@family: address family for AF-specific types
 *	@flags: expression type flags
 */
struct nft_expr_type {
	const struct nft_expr_ops	*(*select_ops)(const struct nft_ctx *,
						       const struct nlattr * const tb[]);
	void				(*release_ops)(const struct nft_expr_ops *ops);
	const struct nft_expr_ops	*ops;
	struct list_head		list;
	const char			*name;
	struct module			*owner;
	const struct nla_policy		*policy;
	unsigned int			maxattr;
	u8				family;
	u8				flags;
};

#define NFT_EXPR_STATEFUL		0x1
#define NFT_EXPR_GC			0x2

enum nft_trans_phase {
	NFT_TRANS_PREPARE,
	NFT_TRANS_PREPARE_ERROR,
	NFT_TRANS_ABORT,
	NFT_TRANS_COMMIT,
	NFT_TRANS_RELEASE
};

struct nft_flow_rule;
struct nft_offload_ctx;

/**
 *	struct nft_expr_ops - nf_tables expression operations
 *
 *	@eval: Expression evaluation function
 *	@size: full expression size, including private data size
 *	@init: initialization function
 *	@activate: activate expression in the next generation
 *	@deactivate: deactivate expression in next generation
 *	@destroy: destruction function, called after synchronize_rcu
 *	@dump: function to dump parameters
 *	@type: expression type
 *	@validate: validate expression, called during loop detection
 *	@data: extra data to attach to this expression operation
 */
struct nft_expr_ops {
	void				(*eval)(const struct nft_expr *expr,
						struct nft_regs *regs,
						const struct nft_pktinfo *pkt);
	int				(*clone)(struct nft_expr *dst,
						 const struct nft_expr *src);
	unsigned int			size;

	int				(*init)(const struct nft_ctx *ctx,
						const struct nft_expr *expr,
						const struct nlattr * const tb[]);
	void				(*activate)(const struct nft_ctx *ctx,
						    const struct nft_expr *expr);
	void				(*deactivate)(const struct nft_ctx *ctx,
						      const struct nft_expr *expr,
						      enum nft_trans_phase phase);
	void				(*destroy)(const struct nft_ctx *ctx,
						   const struct nft_expr *expr);
	void				(*destroy_clone)(const struct nft_ctx *ctx,
							 const struct nft_expr *expr);
	int				(*dump)(struct sk_buff *skb,
						const struct nft_expr *expr);
	int				(*validate)(const struct nft_ctx *ctx,
						    const struct nft_expr *expr,
						    const struct nft_data **data);
	bool				(*reduce)(struct nft_regs_track *track,
						  const struct nft_expr *expr);
	bool				(*gc)(struct net *net,
					      const struct nft_expr *expr);
	int				(*offload)(struct nft_offload_ctx *ctx,
						   struct nft_flow_rule *flow,
						   const struct nft_expr *expr);
	bool				(*offload_action)(const struct nft_expr *expr);
	void				(*offload_stats)(struct nft_expr *expr,
							 const struct flow_stats *stats);
	const struct nft_expr_type	*type;
	void				*data;
};

/**
 *	struct nft_rule - nf_tables rule
 *
 *	@list: used internally
 *	@handle: rule handle
 *	@genmask: generation mask
 *	@dlen: length of expression data
 *	@udata: user data is appended to the rule
 *	@data: expression data
 */
struct nft_rule {
	struct list_head		list;
	u64				handle:42,
					genmask:2,
					dlen:12,
					udata:1;
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

static inline bool nft_expr_more(const struct nft_rule *rule,
				 const struct nft_expr *expr)
{
	return expr != nft_expr_last(rule) && expr->ops;
}

static inline struct nft_userdata *nft_userdata(const struct nft_rule *rule)
{
	return (void *)&rule->data[rule->dlen];
}

void nft_rule_expr_activate(const struct nft_ctx *ctx, struct nft_rule *rule);
void nft_rule_expr_deactivate(const struct nft_ctx *ctx, struct nft_rule *rule,
			      enum nft_trans_phase phase);
void nf_tables_rule_destroy(const struct nft_ctx *ctx, struct nft_rule *rule);

static inline void nft_set_elem_update_expr(const struct nft_set_ext *ext,
					    struct nft_regs *regs,
					    const struct nft_pktinfo *pkt)
{
	struct nft_set_elem_expr *elem_expr;
	struct nft_expr *expr;
	u32 size;

	if (__nft_set_ext_exists(ext, NFT_SET_EXT_EXPRESSIONS)) {
		elem_expr = nft_set_ext_expr(ext);
		nft_setelem_expr_foreach(expr, elem_expr, size) {
			expr->ops->eval(expr, regs, pkt);
			if (regs->verdict.code == NFT_BREAK)
				return;
		}
	}
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

#define NFT_CHAIN_POLICY_UNSET		U8_MAX

struct nft_rule_dp {
	u64				is_last:1,
					dlen:12,
					handle:42;	/* for tracing */
	unsigned char			data[]
		__attribute__((aligned(__alignof__(struct nft_expr))));
};

struct nft_rule_blob {
	unsigned long			size;
	unsigned char			data[]
		__attribute__((aligned(__alignof__(struct nft_rule_dp))));
};

/**
 *	struct nft_chain - nf_tables chain
 *
 *	@rules: list of rules in the chain
 *	@list: used internally
 *	@rhlhead: used internally
 *	@table: table that this chain belongs to
 *	@handle: chain handle
 *	@use: number of jump references to this chain
 *	@flags: bitmask of enum nft_chain_flags
 *	@name: name of the chain
 */
struct nft_chain {
	struct nft_rule_blob		__rcu *blob_gen_0;
	struct nft_rule_blob		__rcu *blob_gen_1;
	struct list_head		rules;
	struct list_head		list;
	struct rhlist_head		rhlhead;
	struct nft_table		*table;
	u64				handle;
	u32				use;
	u8				flags:5,
					bound:1,
					genmask:2;
	char				*name;
	u16				udlen;
	u8				*udata;

	/* Only used during control plane commit phase: */
	struct nft_rule_blob		*blob_next;
};

int nft_chain_validate(const struct nft_ctx *ctx, const struct nft_chain *chain);
int nft_setelem_validate(const struct nft_ctx *ctx, struct nft_set *set,
			 const struct nft_set_iter *iter,
			 struct nft_set_elem *elem);
int nft_set_catchall_validate(const struct nft_ctx *ctx, struct nft_set *set);
int nf_tables_bind_chain(const struct nft_ctx *ctx, struct nft_chain *chain);
void nf_tables_unbind_chain(const struct nft_ctx *ctx, struct nft_chain *chain);

enum nft_chain_types {
	NFT_CHAIN_T_DEFAULT = 0,
	NFT_CHAIN_T_ROUTE,
	NFT_CHAIN_T_NAT,
	NFT_CHAIN_T_MAX
};

/**
 * 	struct nft_chain_type - nf_tables chain type info
 *
 * 	@name: name of the type
 * 	@type: numeric identifier
 * 	@family: address family
 * 	@owner: module owner
 * 	@hook_mask: mask of valid hooks
 * 	@hooks: array of hook functions
 *	@ops_register: base chain register function
 *	@ops_unregister: base chain unregister function
 */
struct nft_chain_type {
	const char			*name;
	enum nft_chain_types		type;
	int				family;
	struct module			*owner;
	unsigned int			hook_mask;
	nf_hookfn			*hooks[NFT_MAX_HOOKS];
	int				(*ops_register)(struct net *net, const struct nf_hook_ops *ops);
	void				(*ops_unregister)(struct net *net, const struct nf_hook_ops *ops);
};

int nft_chain_validate_dependency(const struct nft_chain *chain,
				  enum nft_chain_types type);
int nft_chain_validate_hooks(const struct nft_chain *chain,
                             unsigned int hook_flags);

static inline bool nft_chain_binding(const struct nft_chain *chain)
{
	return chain->flags & NFT_CHAIN_BINDING;
}

static inline bool nft_chain_is_bound(struct nft_chain *chain)
{
	return (chain->flags & NFT_CHAIN_BINDING) && chain->bound;
}

int nft_chain_add(struct nft_table *table, struct nft_chain *chain);
void nft_chain_del(struct nft_chain *chain);
void nf_tables_chain_destroy(struct nft_ctx *ctx);

struct nft_stats {
	u64			bytes;
	u64			pkts;
	struct u64_stats_sync	syncp;
};

struct nft_hook {
	struct list_head	list;
	struct nf_hook_ops	ops;
	struct rcu_head		rcu;
};

/**
 *	struct nft_base_chain - nf_tables base chain
 *
 *	@ops: netfilter hook ops
 *	@hook_list: list of netfilter hooks (for NFPROTO_NETDEV family)
 *	@type: chain type
 *	@policy: default policy
 *	@stats: per-cpu chain stats
 *	@chain: the chain
 *	@flow_block: flow block (for hardware offload)
 */
struct nft_base_chain {
	struct nf_hook_ops		ops;
	struct list_head		hook_list;
	const struct nft_chain_type	*type;
	u8				policy;
	u8				flags;
	struct nft_stats __percpu	*stats;
	struct nft_chain		chain;
	struct flow_block		flow_block;
};

static inline struct nft_base_chain *nft_base_chain(const struct nft_chain *chain)
{
	return container_of(chain, struct nft_base_chain, chain);
}

static inline bool nft_is_base_chain(const struct nft_chain *chain)
{
	return chain->flags & NFT_CHAIN_BASE;
}

int __nft_release_basechain(struct nft_ctx *ctx);

unsigned int nft_do_chain(struct nft_pktinfo *pkt, void *priv);

static inline bool nft_use_inc(u32 *use)
{
	if (*use == UINT_MAX)
		return false;

	(*use)++;

	return true;
}

static inline void nft_use_dec(u32 *use)
{
	WARN_ON_ONCE((*use)-- == 0);
}

/* For error and abort path: restore use counter to previous state. */
static inline void nft_use_inc_restore(u32 *use)
{
	WARN_ON_ONCE(!nft_use_inc(use));
}

#define nft_use_dec_restore	nft_use_dec

/**
 *	struct nft_table - nf_tables table
 *
 *	@list: used internally
 *	@chains_ht: chains in the table
 *	@chains: same, for stable walks
 *	@sets: sets in the table
 *	@objects: stateful objects in the table
 *	@flowtables: flow tables in the table
 *	@hgenerator: handle generator state
 *	@handle: table handle
 *	@use: number of chain references to this table
 *	@flags: table flag (see enum nft_table_flags)
 *	@genmask: generation mask
 *	@afinfo: address family info
 *	@name: name of the table
 */
struct nft_table {
	struct list_head		list;
	struct rhltable			chains_ht;
	struct list_head		chains;
	struct list_head		sets;
	struct list_head		objects;
	struct list_head		flowtables;
	u64				hgenerator;
	u64				handle;
	u32				use;
	u16				family:6,
					flags:8,
					genmask:2;
	u32				nlpid;
	char				*name;
	u16				udlen;
	u8				*udata;
};

static inline bool nft_table_has_owner(const struct nft_table *table)
{
	return table->flags & NFT_TABLE_F_OWNER;
}

static inline bool nft_base_chain_netdev(int family, u32 hooknum)
{
	return family == NFPROTO_NETDEV ||
	       (family == NFPROTO_INET && hooknum == NF_INET_INGRESS);
}

void nft_register_chain_type(const struct nft_chain_type *);
void nft_unregister_chain_type(const struct nft_chain_type *);

int nft_register_expr(struct nft_expr_type *);
void nft_unregister_expr(struct nft_expr_type *);

int nft_verdict_dump(struct sk_buff *skb, int type,
		     const struct nft_verdict *v);

/**
 *	struct nft_object_hash_key - key to lookup nft_object
 *
 *	@name: name of the stateful object to look up
 *	@table: table the object belongs to
 */
struct nft_object_hash_key {
	const char                      *name;
	const struct nft_table          *table;
};

/**
 *	struct nft_object - nf_tables stateful object
 *
 *	@list: table stateful object list node
 *	@key:  keys that identify this object
 *	@rhlhead: nft_objname_ht node
 *	@genmask: generation mask
 *	@use: number of references to this stateful object
 *	@handle: unique object handle
 *	@ops: object operations
 *	@data: object data, layout depends on type
 */
struct nft_object {
	struct list_head		list;
	struct rhlist_head		rhlhead;
	struct nft_object_hash_key	key;
	u32				genmask:2;
	u32				use;
	u64				handle;
	u16				udlen;
	u8				*udata;
	/* runtime data below here */
	const struct nft_object_ops	*ops ____cacheline_aligned;
	unsigned char			data[]
		__attribute__((aligned(__alignof__(u64))));
};

static inline void *nft_obj_data(const struct nft_object *obj)
{
	return (void *)obj->data;
}

#define nft_expr_obj(expr)	*((struct nft_object **)nft_expr_priv(expr))

struct nft_object *nft_obj_lookup(const struct net *net,
				  const struct nft_table *table,
				  const struct nlattr *nla, u32 objtype,
				  u8 genmask);

void nft_obj_notify(struct net *net, const struct nft_table *table,
		    struct nft_object *obj, u32 portid, u32 seq,
		    int event, u16 flags, int family, int report, gfp_t gfp);

/**
 *	struct nft_object_type - stateful object type
 *
 *	@select_ops: function to select nft_object_ops
 *	@ops: default ops, used when no select_ops functions is present
 *	@list: list node in list of object types
 *	@type: stateful object numeric type
 *	@owner: module owner
 *	@maxattr: maximum netlink attribute
 *	@policy: netlink attribute policy
 */
struct nft_object_type {
	const struct nft_object_ops	*(*select_ops)(const struct nft_ctx *,
						       const struct nlattr * const tb[]);
	const struct nft_object_ops	*ops;
	struct list_head		list;
	u32				type;
	unsigned int                    maxattr;
	struct module			*owner;
	const struct nla_policy		*policy;
};

/**
 *	struct nft_object_ops - stateful object operations
 *
 *	@eval: stateful object evaluation function
 *	@size: stateful object size
 *	@init: initialize object from netlink attributes
 *	@destroy: release existing stateful object
 *	@dump: netlink dump stateful object
 *	@update: update stateful object
 */
struct nft_object_ops {
	void				(*eval)(struct nft_object *obj,
						struct nft_regs *regs,
						const struct nft_pktinfo *pkt);
	unsigned int			size;
	int				(*init)(const struct nft_ctx *ctx,
						const struct nlattr *const tb[],
						struct nft_object *obj);
	void				(*destroy)(const struct nft_ctx *ctx,
						   struct nft_object *obj);
	int				(*dump)(struct sk_buff *skb,
						struct nft_object *obj,
						bool reset);
	void				(*update)(struct nft_object *obj,
						  struct nft_object *newobj);
	const struct nft_object_type	*type;
};

int nft_register_obj(struct nft_object_type *obj_type);
void nft_unregister_obj(struct nft_object_type *obj_type);

#define NFT_NETDEVICE_MAX	256

/**
 *	struct nft_flowtable - nf_tables flow table
 *
 *	@list: flow table list node in table list
 * 	@table: the table the flow table is contained in
 *	@name: name of this flow table
 *	@hooknum: hook number
 *	@ops_len: number of hooks in array
 *	@genmask: generation mask
 *	@use: number of references to this flow table
 * 	@handle: unique object handle
 *	@dev_name: array of device names
 *	@data: rhashtable and garbage collector
 * 	@ops: array of hooks
 */
struct nft_flowtable {
	struct list_head		list;
	struct nft_table		*table;
	char				*name;
	int				hooknum;
	int				ops_len;
	u32				genmask:2;
	u32				use;
	u64				handle;
	/* runtime data below here */
	struct list_head		hook_list ____cacheline_aligned;
	struct nf_flowtable		data;
};

struct nft_flowtable *nft_flowtable_lookup(const struct nft_table *table,
					   const struct nlattr *nla,
					   u8 genmask);

void nf_tables_deactivate_flowtable(const struct nft_ctx *ctx,
				    struct nft_flowtable *flowtable,
				    enum nft_trans_phase phase);

void nft_register_flowtable_type(struct nf_flowtable_type *type);
void nft_unregister_flowtable_type(struct nf_flowtable_type *type);

/**
 *	struct nft_traceinfo - nft tracing information and state
 *
 *	@trace: other struct members are initialised
 *	@nf_trace: copy of skb->nf_trace before rule evaluation
 *	@type: event type (enum nft_trace_types)
 *	@skbid: hash of skb to be used as trace id
 *	@packet_dumped: packet headers sent in a previous traceinfo message
 *	@pkt: pktinfo currently processed
 *	@basechain: base chain currently processed
 *	@chain: chain currently processed
 *	@rule:  rule that was evaluated
 *	@verdict: verdict given by rule
 */
struct nft_traceinfo {
	bool				trace;
	bool				nf_trace;
	bool				packet_dumped;
	enum nft_trace_types		type:8;
	u32				skbid;
	const struct nft_pktinfo	*pkt;
	const struct nft_base_chain	*basechain;
	const struct nft_chain		*chain;
	const struct nft_rule_dp	*rule;
	const struct nft_verdict	*verdict;
};

void nft_trace_init(struct nft_traceinfo *info, const struct nft_pktinfo *pkt,
		    const struct nft_verdict *verdict,
		    const struct nft_chain *basechain);

void nft_trace_notify(struct nft_traceinfo *info);

#define MODULE_ALIAS_NFT_CHAIN(family, name) \
	MODULE_ALIAS("nft-chain-" __stringify(family) "-" name)

#define MODULE_ALIAS_NFT_AF_EXPR(family, name) \
	MODULE_ALIAS("nft-expr-" __stringify(family) "-" name)

#define MODULE_ALIAS_NFT_EXPR(name) \
	MODULE_ALIAS("nft-expr-" name)

#define MODULE_ALIAS_NFT_OBJ(type) \
	MODULE_ALIAS("nft-obj-" __stringify(type))

#if IS_ENABLED(CONFIG_NF_TABLES)

/*
 * The gencursor defines two generations, the currently active and the
 * next one. Objects contain a bitmask of 2 bits specifying the generations
 * they're active in. A set bit means they're inactive in the generation
 * represented by that bit.
 *
 * New objects start out as inactive in the current and active in the
 * next generation. When committing the ruleset the bitmask is cleared,
 * meaning they're active in all generations. When removing an object,
 * it is set inactive in the next generation. After committing the ruleset,
 * the objects are removed.
 */
static inline unsigned int nft_gencursor_next(const struct net *net)
{
	return net->nft.gencursor + 1 == 1 ? 1 : 0;
}

static inline u8 nft_genmask_next(const struct net *net)
{
	return 1 << nft_gencursor_next(net);
}

static inline u8 nft_genmask_cur(const struct net *net)
{
	/* Use READ_ONCE() to prevent refetching the value for atomicity */
	return 1 << READ_ONCE(net->nft.gencursor);
}

#define NFT_GENMASK_ANY		((1 << 0) | (1 << 1))

/*
 * Generic transaction helpers
 */

/* Check if this object is currently active. */
#define nft_is_active(__net, __obj)				\
	(((__obj)->genmask & nft_genmask_cur(__net)) == 0)

/* Check if this object is active in the next generation. */
#define nft_is_active_next(__net, __obj)			\
	(((__obj)->genmask & nft_genmask_next(__net)) == 0)

/* This object becomes active in the next generation. */
#define nft_activate_next(__net, __obj)				\
	(__obj)->genmask = nft_genmask_cur(__net)

/* This object becomes inactive in the next generation. */
#define nft_deactivate_next(__net, __obj)			\
        (__obj)->genmask = nft_genmask_next(__net)

/* After committing the ruleset, clear the stale generation bit. */
#define nft_clear(__net, __obj)					\
	(__obj)->genmask &= ~nft_genmask_next(__net)
#define nft_active_genmask(__obj, __genmask)			\
	!((__obj)->genmask & __genmask)

/*
 * Set element transaction helpers
 */

static inline bool nft_set_elem_active(const struct nft_set_ext *ext,
				       u8 genmask)
{
	return !(ext->genmask & genmask);
}

static inline void nft_set_elem_change_active(const struct net *net,
					      const struct nft_set *set,
					      struct nft_set_ext *ext)
{
	ext->genmask ^= nft_genmask_next(net);
}

#endif /* IS_ENABLED(CONFIG_NF_TABLES) */

#define NFT_SET_ELEM_DEAD_MASK	(1 << 2)

#if defined(__LITTLE_ENDIAN_BITFIELD)
#define NFT_SET_ELEM_DEAD_BIT	2
#elif defined(__BIG_ENDIAN_BITFIELD)
#define NFT_SET_ELEM_DEAD_BIT	(BITS_PER_LONG - BITS_PER_BYTE + 2)
#else
#error
#endif

static inline void nft_set_elem_dead(struct nft_set_ext *ext)
{
	unsigned long *word = (unsigned long *)ext;

	BUILD_BUG_ON(offsetof(struct nft_set_ext, genmask) != 0);
	set_bit(NFT_SET_ELEM_DEAD_BIT, word);
}

static inline int nft_set_elem_is_dead(const struct nft_set_ext *ext)
{
	unsigned long *word = (unsigned long *)ext;

	BUILD_BUG_ON(offsetof(struct nft_set_ext, genmask) != 0);
	return test_bit(NFT_SET_ELEM_DEAD_BIT, word);
}

/**
 *	struct nft_trans - nf_tables object update in transaction
 *
 *	@list: used internally
 *	@binding_list: list of objects with possible bindings
 *	@msg_type: message type
 *	@put_net: ctx->net needs to be put
 *	@ctx: transaction context
 *	@data: internal information related to the transaction
 */
struct nft_trans {
	struct list_head		list;
	struct list_head		binding_list;
	int				msg_type;
	bool				put_net;
	struct nft_ctx			ctx;
	char				data[];
};

struct nft_trans_rule {
	struct nft_rule			*rule;
	struct nft_flow_rule		*flow;
	u32				rule_id;
	bool				bound;
};

#define nft_trans_rule(trans)	\
	(((struct nft_trans_rule *)trans->data)->rule)
#define nft_trans_flow_rule(trans)	\
	(((struct nft_trans_rule *)trans->data)->flow)
#define nft_trans_rule_id(trans)	\
	(((struct nft_trans_rule *)trans->data)->rule_id)
#define nft_trans_rule_bound(trans)	\
	(((struct nft_trans_rule *)trans->data)->bound)

struct nft_trans_set {
	struct nft_set			*set;
	u32				set_id;
	u32				gc_int;
	u64				timeout;
	bool				update;
	bool				bound;
};

#define nft_trans_set(trans)	\
	(((struct nft_trans_set *)trans->data)->set)
#define nft_trans_set_id(trans)	\
	(((struct nft_trans_set *)trans->data)->set_id)
#define nft_trans_set_bound(trans)	\
	(((struct nft_trans_set *)trans->data)->bound)
#define nft_trans_set_update(trans)	\
	(((struct nft_trans_set *)trans->data)->update)
#define nft_trans_set_timeout(trans)	\
	(((struct nft_trans_set *)trans->data)->timeout)
#define nft_trans_set_gc_int(trans)	\
	(((struct nft_trans_set *)trans->data)->gc_int)

struct nft_trans_chain {
	struct nft_chain		*chain;
	bool				update;
	char				*name;
	struct nft_stats __percpu	*stats;
	u8				policy;
	bool				bound;
	u32				chain_id;
};

#define nft_trans_chain(trans)	\
	(((struct nft_trans_chain *)trans->data)->chain)
#define nft_trans_chain_update(trans)	\
	(((struct nft_trans_chain *)trans->data)->update)
#define nft_trans_chain_name(trans)	\
	(((struct nft_trans_chain *)trans->data)->name)
#define nft_trans_chain_stats(trans)	\
	(((struct nft_trans_chain *)trans->data)->stats)
#define nft_trans_chain_policy(trans)	\
	(((struct nft_trans_chain *)trans->data)->policy)
#define nft_trans_chain_bound(trans)	\
	(((struct nft_trans_chain *)trans->data)->bound)
#define nft_trans_chain_id(trans)	\
	(((struct nft_trans_chain *)trans->data)->chain_id)

struct nft_trans_table {
	bool				update;
};

#define nft_trans_table_update(trans)	\
	(((struct nft_trans_table *)trans->data)->update)

struct nft_trans_elem {
	struct nft_set			*set;
	struct nft_set_elem		elem;
	bool				bound;
};

#define nft_trans_elem_set(trans)	\
	(((struct nft_trans_elem *)trans->data)->set)
#define nft_trans_elem(trans)	\
	(((struct nft_trans_elem *)trans->data)->elem)
#define nft_trans_elem_set_bound(trans)	\
	(((struct nft_trans_elem *)trans->data)->bound)

struct nft_trans_obj {
	struct nft_object		*obj;
	struct nft_object		*newobj;
	bool				update;
};

#define nft_trans_obj(trans)	\
	(((struct nft_trans_obj *)trans->data)->obj)
#define nft_trans_obj_newobj(trans) \
	(((struct nft_trans_obj *)trans->data)->newobj)
#define nft_trans_obj_update(trans)	\
	(((struct nft_trans_obj *)trans->data)->update)

struct nft_trans_flowtable {
	struct nft_flowtable		*flowtable;
	bool				update;
	struct list_head		hook_list;
	u32				flags;
};

#define nft_trans_flowtable(trans)	\
	(((struct nft_trans_flowtable *)trans->data)->flowtable)
#define nft_trans_flowtable_update(trans)	\
	(((struct nft_trans_flowtable *)trans->data)->update)
#define nft_trans_flowtable_hooks(trans)	\
	(((struct nft_trans_flowtable *)trans->data)->hook_list)
#define nft_trans_flowtable_flags(trans)	\
	(((struct nft_trans_flowtable *)trans->data)->flags)

#define NFT_TRANS_GC_BATCHCOUNT	256

struct nft_trans_gc {
	struct list_head	list;
	struct net		*net;
	struct nft_set		*set;
	u32			seq;
	u16			count;
	void			*priv[NFT_TRANS_GC_BATCHCOUNT];
	struct rcu_head		rcu;
};

struct nft_trans_gc *nft_trans_gc_alloc(struct nft_set *set,
					unsigned int gc_seq, gfp_t gfp);
void nft_trans_gc_destroy(struct nft_trans_gc *trans);

struct nft_trans_gc *nft_trans_gc_queue_async(struct nft_trans_gc *gc,
					      unsigned int gc_seq, gfp_t gfp);
void nft_trans_gc_queue_async_done(struct nft_trans_gc *gc);

struct nft_trans_gc *nft_trans_gc_queue_sync(struct nft_trans_gc *gc, gfp_t gfp);
void nft_trans_gc_queue_sync_done(struct nft_trans_gc *trans);

void nft_trans_gc_elem_add(struct nft_trans_gc *gc, void *priv);

struct nft_trans_gc *nft_trans_gc_catchall_async(struct nft_trans_gc *gc,
						 unsigned int gc_seq);
struct nft_trans_gc *nft_trans_gc_catchall_sync(struct nft_trans_gc *gc);

void nft_setelem_data_deactivate(const struct net *net,
				 const struct nft_set *set,
				 struct nft_set_elem *elem);

int __init nft_chain_filter_init(void);
void nft_chain_filter_fini(void);

void __init nft_chain_route_init(void);
void nft_chain_route_fini(void);

void nf_tables_trans_destroy_flush_work(void);

int nf_msecs_to_jiffies64(const struct nlattr *nla, u64 *result);
__be64 nf_jiffies64_to_msecs(u64 input);

#ifdef CONFIG_MODULES
__printf(2, 3) int nft_request_module(struct net *net, const char *fmt, ...);
#else
static inline int nft_request_module(struct net *net, const char *fmt, ...) { return -ENOENT; }
#endif

struct nftables_pernet {
	struct list_head	tables;
	struct list_head	commit_list;
	struct list_head	binding_list;
	struct list_head	module_list;
	struct list_head	notify_list;
	struct mutex		commit_mutex;
	u64			table_handle;
	unsigned int		base_seq;
	u8			validate_state;
	unsigned int		gc_seq;
};

extern unsigned int nf_tables_net_id;

static inline struct nftables_pernet *nft_pernet(const struct net *net)
{
	return net_generic(net, nf_tables_net_id);
}

#define __NFT_REDUCE_READONLY	1UL
#define NFT_REDUCE_READONLY	(void *)__NFT_REDUCE_READONLY

static inline bool nft_reduce_is_readonly(const struct nft_expr *expr)
{
	return expr->ops->reduce == NFT_REDUCE_READONLY;
}

void nft_reg_track_update(struct nft_regs_track *track,
			  const struct nft_expr *expr, u8 dreg, u8 len);
void nft_reg_track_cancel(struct nft_regs_track *track, u8 dreg, u8 len);
void __nft_reg_track_cancel(struct nft_regs_track *track, u8 dreg);

static inline bool nft_reg_track_cmp(struct nft_regs_track *track,
				     const struct nft_expr *expr, u8 dreg)
{
	return track->regs[dreg].selector &&
	       track->regs[dreg].selector->ops == expr->ops &&
	       track->regs[dreg].num_reg == 0;
}

#endif /* _NET_NF_TABLES_H */
