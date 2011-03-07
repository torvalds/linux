#ifndef GENL_MAGIC_FUNC_H
#define GENL_MAGIC_FUNC_H

#include <linux/genl_magic_struct.h>

/*
 * Extension of genl attribute validation policies			{{{1
 *									{{{2
 */

/**
 * nla_is_required - return true if this attribute is required
 * @nla: netlink attribute
 */
static inline int nla_is_required(const struct nlattr *nla)
{
        return nla->nla_type & GENLA_F_REQUIRED;
}

/**
 * nla_is_mandatory - return true if understanding this attribute is mandatory
 * @nla: netlink attribute
 * Note: REQUIRED attributes are implicitly MANDATORY as well
 */
static inline int nla_is_mandatory(const struct nlattr *nla)
{
        return nla->nla_type & (GENLA_F_MANDATORY | GENLA_F_REQUIRED);
}

/* Functionality to be integrated into nla_parse(), and validate_nla(),
 * respectively.
 *
 * Enforcing the "mandatory" bit is done here,
 * by rejecting unknown mandatory attributes.
 *
 * Part of enforcing the "required" flag would mean to embed it into
 * nla_policy.type, and extending validate_nla(), which currently does
 * BUG_ON(pt->type > NLA_TYPE_MAX); we have to work on existing kernels,
 * so we cannot do that.  Thats why enforcing "required" is done in the
 * generated assignment functions below. */
static int nla_check_unknown(int maxtype, struct nlattr *head, int len)
{
	struct nlattr *nla;
	int rem;
        nla_for_each_attr(nla, head, len, rem) {
		__u16 type = nla_type(nla);
		if (type > maxtype && nla_is_mandatory(nla))
			return -EOPNOTSUPP;
	}
	return 0;
}

/*
 * Magic: declare tla policy						{{{1
 * Magic: declare nested policies
 *									{{{2
 */
#undef GENL_mc_group
#define GENL_mc_group(group)

#undef GENL_notification
#define GENL_notification(op_name, op_num, mcast_group, tla_list)

#undef GENL_op
#define GENL_op(op_name, op_num, handler, tla_list)

#undef GENL_struct
#define GENL_struct(tag_name, tag_number, s_name, s_fields)		\
	[tag_name] = { .type = NLA_NESTED },

static struct nla_policy CONCAT_(GENL_MAGIC_FAMILY, _tla_nl_policy)[] = {
#include GENL_MAGIC_INCLUDE_FILE
};

#undef GENL_struct
#define GENL_struct(tag_name, tag_number, s_name, s_fields)		\
static struct nla_policy s_name ## _nl_policy[] __read_mostly =		\
{ s_fields };

#undef __field
#define __field(attr_nr, attr_flag, name, nla_type, _type, __get, __put) \
	[__nla_type(attr_nr)] = { .type = nla_type },

#undef __array
#define __array(attr_nr, attr_flag, name, nla_type, _type, maxlen,	\
		__get, __put)						\
	[__nla_type(attr_nr)] = { .type = nla_type,			\
		.len = maxlen - (nla_type == NLA_NUL_STRING) },

#include GENL_MAGIC_INCLUDE_FILE

#ifndef __KERNEL__
#ifndef pr_info
#define pr_info(args...)	fprintf(stderr, args);
#endif
#endif

#if 1
static void dprint_field(const char *dir, int nla_type,
		const char *name, void *valp)
{
	__u64 val = valp ? *(__u32 *)valp : 1;
	switch (nla_type) {
	case NLA_U8:  val = (__u8)val;
	case NLA_U16: val = (__u16)val;
	case NLA_U32: val = (__u32)val;
		pr_info("%s attr %s: %d 0x%08x\n", dir,
			name, (int)val, (unsigned)val);
		break;
	case NLA_U64:
		val = *(__u64*)valp;
		pr_info("%s attr %s: %lld 0x%08llx\n", dir,
			name, (long long)val, (unsigned long long)val);
		break;
	case NLA_FLAG:
		if (val)
			pr_info("%s attr %s: set\n", dir, name);
		break;
	}
}

static void dprint_array(const char *dir, int nla_type,
		const char *name, const char *val, unsigned len)
{
	switch (nla_type) {
	case NLA_NUL_STRING:
		if (len && val[len-1] == '\0')
			len--;
		pr_info("%s attr %s: [len:%u] '%s'\n", dir, name, len, val);
		break;
	default:
		/* we can always show 4 byte,
		 * thats what nlattr are aligned to. */
		pr_info("%s attr %s: [len:%u] %02x%02x%02x%02x ...\n",
			dir, name, len, val[0], val[1], val[2], val[3]);
	}
}

#define DPRINT_TLA(a, op, b) pr_info("%s %s %s\n", a, op, b);

/* Name is a member field name of the struct s.
 * If s is NULL (only parsing, no copy requested in *_from_attrs()),
 * nla is supposed to point to the attribute containing the information
 * corresponding to that struct member. */
#define DPRINT_FIELD(dir, nla_type, name, s, nla)			\
	do {								\
		if (s)							\
			dprint_field(dir, nla_type, #name, &s->name);	\
		else if (nla)						\
			dprint_field(dir, nla_type, #name,		\
				(nla_type == NLA_FLAG) ? NULL		\
						: nla_data(nla));	\
	} while (0)

#define	DPRINT_ARRAY(dir, nla_type, name, s, nla)			\
	do {								\
		if (s)							\
			dprint_array(dir, nla_type, #name,		\
					s->name, s->name ## _len);	\
		else if (nla)						\
			dprint_array(dir, nla_type, #name,		\
					nla_data(nla), nla_len(nla));	\
	} while (0)
#else
#define DPRINT_TLA(a, op, b) do {} while (0)
#define DPRINT_FIELD(dir, nla_type, name, s, nla) do {} while (0)
#define	DPRINT_ARRAY(dir, nla_type, name, s, nla) do {} while (0)
#endif

/*
 * Magic: provide conversion functions					{{{1
 * populate struct from attribute table:
 *									{{{2
 */

/* processing of generic netlink messages is serialized.
 * use one static buffer for parsing of nested attributes */
static struct nlattr *nested_attr_tb[128];

#ifndef BUILD_BUG_ON
/* Force a compilation error if condition is true */
#define BUILD_BUG_ON(condition) ((void)BUILD_BUG_ON_ZERO(condition))
/* Force a compilation error if condition is true, but also produce a
   result (of value 0 and type size_t), so the expression can be used
   e.g. in a structure initializer (or where-ever else comma expressions
   aren't permitted). */
#define BUILD_BUG_ON_ZERO(e) (sizeof(struct { int:-!!(e); }))
#define BUILD_BUG_ON_NULL(e) ((void *)sizeof(struct { int:-!!(e); }))
#endif

#undef GENL_struct
#define GENL_struct(tag_name, tag_number, s_name, s_fields)		\
	/* static, potentially unused */				\
int s_name ## _from_attrs(struct s_name *s, struct nlattr *tb[])	\
{									\
	const int maxtype = ARRAY_SIZE(s_name ## _nl_policy)-1;		\
	struct nlattr *tla = tb[tag_number];				\
	struct nlattr **ntb = nested_attr_tb;				\
	struct nlattr *nla;						\
	int err;							\
	BUILD_BUG_ON(ARRAY_SIZE(s_name ## _nl_policy) > ARRAY_SIZE(nested_attr_tb));	\
	if (!tla)							\
		return -ENOMSG;						\
	DPRINT_TLA(#s_name, "<=-", #tag_name);				\
	err = nla_parse_nested(ntb, maxtype, tla, s_name ## _nl_policy); \
	if (err)							\
		return err;						\
	err = nla_check_unknown(maxtype, nla_data(tla), nla_len(tla));	\
	if (err)							\
	      return err;						\
									\
	s_fields							\
	return 0;							\
}

#undef __field
#define __field(attr_nr, attr_flag, name, nla_type, type, __get, __put)	\
		nla = ntb[__nla_type(attr_nr)];				\
		if (nla) {						\
			if (s)						\
				s->name = __get(nla);			\
			DPRINT_FIELD("<<", nla_type, name, s, nla);	\
		} else if ((attr_flag) & GENLA_F_REQUIRED) {		\
			pr_info("<< missing attr: %s\n", #name);	\
			return -ENOMSG;					\
		}

/* validate_nla() already checked nla_len <= maxlen appropriately. */
#undef __array
#define __array(attr_nr, attr_flag, name, nla_type, type, maxlen, __get, __put) \
		nla = ntb[__nla_type(attr_nr)];				\
		if (nla) {						\
			if (s)						\
				s->name ## _len =			\
					__get(s->name, nla, maxlen);	\
			DPRINT_ARRAY("<<", nla_type, name, s, nla);	\
		} else if ((attr_flag) & GENLA_F_REQUIRED) {		\
			pr_info("<< missing attr: %s\n", #name);	\
			return -ENOMSG;					\
		}							\

#include GENL_MAGIC_INCLUDE_FILE

#undef GENL_struct
#define GENL_struct(tag_name, tag_number, s_name, s_fields)

/*
 * Magic: define op number to op name mapping				{{{1
 *									{{{2
 */
const char *CONCAT_(GENL_MAGIC_FAMILY, _genl_cmd_to_str)(__u8 cmd)
{
	switch (cmd) {
#undef GENL_op
#define GENL_op(op_name, op_num, handler, tla_list)		\
	case op_num: return #op_name;
#include GENL_MAGIC_INCLUDE_FILE
	default:
		     return "unknown";
	}
}

#ifdef __KERNEL__
#include <linux/stringify.h>
/*
 * Magic: define genl_ops						{{{1
 *									{{{2
 */

#undef GENL_op
#define GENL_op(op_name, op_num, handler, tla_list)		\
{								\
	handler							\
	.cmd = op_name,						\
	.policy	= CONCAT_(GENL_MAGIC_FAMILY, _tla_nl_policy),	\
},

#define ZZZ_genl_ops		CONCAT_(GENL_MAGIC_FAMILY, _genl_ops)
static struct genl_ops ZZZ_genl_ops[] __read_mostly = {
#include GENL_MAGIC_INCLUDE_FILE
};

#undef GENL_op
#define GENL_op(op_name, op_num, handler, tla_list)

/*
 * Define the genl_family, multicast groups,				{{{1
 * and provide register/unregister functions.
 *									{{{2
 */
#define ZZZ_genl_family		CONCAT_(GENL_MAGIC_FAMILY, _genl_family)
static struct genl_family ZZZ_genl_family __read_mostly = {
	.id = GENL_ID_GENERATE,
	.name = __stringify(GENL_MAGIC_FAMILY),
	.version = GENL_MAGIC_VERSION,
#ifdef GENL_MAGIC_FAMILY_HDRSZ
	.hdrsize = NLA_ALIGN(GENL_MAGIC_FAMILY_HDRSZ),
#endif
	.maxattr = ARRAY_SIZE(drbd_tla_nl_policy)-1,
};

/*
 * Magic: define multicast groups
 * Magic: define multicast group registration helper
 */
#undef GENL_mc_group
#define GENL_mc_group(group)						\
static struct genl_multicast_group					\
CONCAT_(GENL_MAGIC_FAMILY, _mcg_ ## group) __read_mostly = {		\
	.name = #group,							\
};									\
static int CONCAT_(GENL_MAGIC_FAMILY, _genl_multicast_ ## group)(	\
	struct sk_buff *skb, gfp_t flags)				\
{									\
	unsigned int group_id =						\
		CONCAT_(GENL_MAGIC_FAMILY, _mcg_ ## group).id;	\
	if (!group_id)							\
		return -EINVAL;						\
	return genlmsg_multicast(skb, 0, group_id, flags);		\
}

#include GENL_MAGIC_INCLUDE_FILE

int CONCAT_(GENL_MAGIC_FAMILY, _genl_register)(void)
{
	int err = genl_register_family_with_ops(&ZZZ_genl_family,
		ZZZ_genl_ops, ARRAY_SIZE(ZZZ_genl_ops));
	if (err)
		return err;
#undef GENL_mc_group
#define GENL_mc_group(group)						\
	err = genl_register_mc_group(&ZZZ_genl_family,			\
		&CONCAT_(GENL_MAGIC_FAMILY, _mcg_ ## group));		\
	if (err)							\
		goto fail;						\
	else								\
		pr_info("%s: mcg %s: %u\n", #group,			\
			__stringify(GENL_MAGIC_FAMILY),			\
			CONCAT_(GENL_MAGIC_FAMILY, _mcg_ ## group).id);

#include GENL_MAGIC_INCLUDE_FILE

#undef GENL_mc_group
#define GENL_mc_group(group)
	return 0;
fail:
	genl_unregister_family(&ZZZ_genl_family);
	return err;
}

void CONCAT_(GENL_MAGIC_FAMILY, _genl_unregister)(void)
{
	genl_unregister_family(&ZZZ_genl_family);
}

/*
 * Magic: provide conversion functions					{{{1
 * populate skb from struct.
 *									{{{2
 */

#undef GENL_op
#define GENL_op(op_name, op_num, handler, tla_list)

#undef GENL_struct
#define GENL_struct(tag_name, tag_number, s_name, s_fields)		\
static int s_name ## _to_skb(struct sk_buff *skb, struct s_name *s,	\
		const bool exclude_sensitive)				\
{									\
	struct nlattr *tla = nla_nest_start(skb, tag_number);		\
	if (!tla)							\
		goto nla_put_failure;					\
	DPRINT_TLA(#s_name, "-=>", #tag_name);				\
	s_fields							\
	nla_nest_end(skb, tla);						\
	return 0;							\
									\
nla_put_failure:							\
	if (tla)							\
		nla_nest_cancel(skb, tla);				\
        return -EMSGSIZE;						\
}									\
static inline int s_name ## _to_priv_skb(struct sk_buff *skb,		\
		struct s_name *s)					\
{									\
	return s_name ## _to_skb(skb, s, 0);				\
}									\
static inline int s_name ## _to_unpriv_skb(struct sk_buff *skb,		\
		struct s_name *s)					\
{									\
	return s_name ## _to_skb(skb, s, 1);				\
}


#undef __field
#define __field(attr_nr, attr_flag, name, nla_type, type, __get, __put)	\
	if (!exclude_sensitive || !((attr_flag) & GENLA_F_SENSITIVE)) {	\
		DPRINT_FIELD(">>", nla_type, name, s, NULL);		\
		__put(skb, attr_nr, s->name);				\
	}

#undef __array
#define __array(attr_nr, attr_flag, name, nla_type, type, maxlen, __get, __put) \
	if (!exclude_sensitive || !((attr_flag) & GENLA_F_SENSITIVE)) {	\
		DPRINT_ARRAY(">>",nla_type, name, s, NULL);		\
		__put(skb, attr_nr, min_t(int, maxlen,			\
			s->name ## _len + (nla_type == NLA_NUL_STRING)),\
						s->name);		\
	}

#include GENL_MAGIC_INCLUDE_FILE

#endif /* __KERNEL__ */

/* }}}1 */
#endif /* GENL_MAGIC_FUNC_H */
/* vim: set foldmethod=marker foldlevel=1 nofoldenable : */
