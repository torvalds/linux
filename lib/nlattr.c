// SPDX-License-Identifier: GPL-2.0
/*
 * NETLINK      Netlink attributes
 *
 * 		Authors:	Thomas Graf <tgraf@suug.ch>
 * 				Alexey Kuznetsov <kuznet@ms2.inr.ac.ru>
 */

#include <linux/export.h>
#include <linux/kernel.h>
#include <linux/errno.h>
#include <linux/jiffies.h>
#include <linux/skbuff.h>
#include <linux/string.h>
#include <linux/types.h>
#include <net/netlink.h>

/* For these data types, attribute length should be exactly the given
 * size. However, to maintain compatibility with broken commands, if the
 * attribute length does not match the expected size a warning is emitted
 * to the user that the command is sending invalid data and needs to be fixed.
 */
static const u8 nla_attr_len[NLA_TYPE_MAX+1] = {
	[NLA_U8]	= sizeof(u8),
	[NLA_U16]	= sizeof(u16),
	[NLA_U32]	= sizeof(u32),
	[NLA_U64]	= sizeof(u64),
	[NLA_S8]	= sizeof(s8),
	[NLA_S16]	= sizeof(s16),
	[NLA_S32]	= sizeof(s32),
	[NLA_S64]	= sizeof(s64),
};

static const u8 nla_attr_minlen[NLA_TYPE_MAX+1] = {
	[NLA_U8]	= sizeof(u8),
	[NLA_U16]	= sizeof(u16),
	[NLA_U32]	= sizeof(u32),
	[NLA_U64]	= sizeof(u64),
	[NLA_MSECS]	= sizeof(u64),
	[NLA_NESTED]	= NLA_HDRLEN,
	[NLA_S8]	= sizeof(s8),
	[NLA_S16]	= sizeof(s16),
	[NLA_S32]	= sizeof(s32),
	[NLA_S64]	= sizeof(s64),
};

/*
 * Nested policies might refer back to the original
 * policy in some cases, and userspace could try to
 * abuse that and recurse by nesting in the right
 * ways. Limit recursion to avoid this problem.
 */
#define MAX_POLICY_RECURSION_DEPTH	10

static int __nla_validate_parse(const struct nlattr *head, int len, int maxtype,
				const struct nla_policy *policy,
				unsigned int validate,
				struct netlink_ext_ack *extack,
				struct nlattr **tb, unsigned int depth);

static int validate_nla_bitfield32(const struct nlattr *nla,
				   const u32 valid_flags_mask)
{
	const struct nla_bitfield32 *bf = nla_data(nla);

	if (!valid_flags_mask)
		return -EINVAL;

	/*disallow invalid bit selector */
	if (bf->selector & ~valid_flags_mask)
		return -EINVAL;

	/*disallow invalid bit values */
	if (bf->value & ~valid_flags_mask)
		return -EINVAL;

	/*disallow valid bit values that are not selected*/
	if (bf->value & ~bf->selector)
		return -EINVAL;

	return 0;
}

static int nla_validate_array(const struct nlattr *head, int len, int maxtype,
			      const struct nla_policy *policy,
			      struct netlink_ext_ack *extack,
			      unsigned int validate, unsigned int depth)
{
	const struct nlattr *entry;
	int rem;

	nla_for_each_attr(entry, head, len, rem) {
		int ret;

		if (nla_len(entry) == 0)
			continue;

		if (nla_len(entry) < NLA_HDRLEN) {
			NL_SET_ERR_MSG_ATTR_POL(extack, entry, policy,
						"Array element too short");
			return -ERANGE;
		}

		ret = __nla_validate_parse(nla_data(entry), nla_len(entry),
					   maxtype, policy, validate, extack,
					   NULL, depth + 1);
		if (ret < 0)
			return ret;
	}

	return 0;
}

void nla_get_range_unsigned(const struct nla_policy *pt,
			    struct netlink_range_validation *range)
{
	WARN_ON_ONCE(pt->validation_type != NLA_VALIDATE_RANGE_PTR &&
		     (pt->min < 0 || pt->max < 0));

	range->min = 0;

	switch (pt->type) {
	case NLA_U8:
		range->max = U8_MAX;
		break;
	case NLA_U16:
	case NLA_BINARY:
		range->max = U16_MAX;
		break;
	case NLA_U32:
		range->max = U32_MAX;
		break;
	case NLA_U64:
	case NLA_MSECS:
		range->max = U64_MAX;
		break;
	default:
		WARN_ON_ONCE(1);
		return;
	}

	switch (pt->validation_type) {
	case NLA_VALIDATE_RANGE:
	case NLA_VALIDATE_RANGE_WARN_TOO_LONG:
		range->min = pt->min;
		range->max = pt->max;
		break;
	case NLA_VALIDATE_RANGE_PTR:
		*range = *pt->range;
		break;
	case NLA_VALIDATE_MIN:
		range->min = pt->min;
		break;
	case NLA_VALIDATE_MAX:
		range->max = pt->max;
		break;
	default:
		break;
	}
}

static int nla_validate_range_unsigned(const struct nla_policy *pt,
				       const struct nlattr *nla,
				       struct netlink_ext_ack *extack,
				       unsigned int validate)
{
	struct netlink_range_validation range;
	u64 value;

	switch (pt->type) {
	case NLA_U8:
		value = nla_get_u8(nla);
		break;
	case NLA_U16:
		value = nla_get_u16(nla);
		break;
	case NLA_U32:
		value = nla_get_u32(nla);
		break;
	case NLA_U64:
	case NLA_MSECS:
		value = nla_get_u64(nla);
		break;
	case NLA_BINARY:
		value = nla_len(nla);
		break;
	default:
		return -EINVAL;
	}

	nla_get_range_unsigned(pt, &range);

	if (pt->validation_type == NLA_VALIDATE_RANGE_WARN_TOO_LONG &&
	    pt->type == NLA_BINARY && value > range.max) {
		pr_warn_ratelimited("netlink: '%s': attribute type %d has an invalid length.\n",
				    current->comm, pt->type);
		if (validate & NL_VALIDATE_STRICT_ATTRS) {
			NL_SET_ERR_MSG_ATTR_POL(extack, nla, pt,
						"invalid attribute length");
			return -EINVAL;
		}

		/* this assumes min <= max (don't validate against min) */
		return 0;
	}

	if (value < range.min || value > range.max) {
		bool binary = pt->type == NLA_BINARY;

		if (binary)
			NL_SET_ERR_MSG_ATTR_POL(extack, nla, pt,
						"binary attribute size out of range");
		else
			NL_SET_ERR_MSG_ATTR_POL(extack, nla, pt,
						"integer out of range");

		return -ERANGE;
	}

	return 0;
}

void nla_get_range_signed(const struct nla_policy *pt,
			  struct netlink_range_validation_signed *range)
{
	switch (pt->type) {
	case NLA_S8:
		range->min = S8_MIN;
		range->max = S8_MAX;
		break;
	case NLA_S16:
		range->min = S16_MIN;
		range->max = S16_MAX;
		break;
	case NLA_S32:
		range->min = S32_MIN;
		range->max = S32_MAX;
		break;
	case NLA_S64:
		range->min = S64_MIN;
		range->max = S64_MAX;
		break;
	default:
		WARN_ON_ONCE(1);
		return;
	}

	switch (pt->validation_type) {
	case NLA_VALIDATE_RANGE:
		range->min = pt->min;
		range->max = pt->max;
		break;
	case NLA_VALIDATE_RANGE_PTR:
		*range = *pt->range_signed;
		break;
	case NLA_VALIDATE_MIN:
		range->min = pt->min;
		break;
	case NLA_VALIDATE_MAX:
		range->max = pt->max;
		break;
	default:
		break;
	}
}

static int nla_validate_int_range_signed(const struct nla_policy *pt,
					 const struct nlattr *nla,
					 struct netlink_ext_ack *extack)
{
	struct netlink_range_validation_signed range;
	s64 value;

	switch (pt->type) {
	case NLA_S8:
		value = nla_get_s8(nla);
		break;
	case NLA_S16:
		value = nla_get_s16(nla);
		break;
	case NLA_S32:
		value = nla_get_s32(nla);
		break;
	case NLA_S64:
		value = nla_get_s64(nla);
		break;
	default:
		return -EINVAL;
	}

	nla_get_range_signed(pt, &range);

	if (value < range.min || value > range.max) {
		NL_SET_ERR_MSG_ATTR_POL(extack, nla, pt,
					"integer out of range");
		return -ERANGE;
	}

	return 0;
}

static int nla_validate_int_range(const struct nla_policy *pt,
				  const struct nlattr *nla,
				  struct netlink_ext_ack *extack,
				  unsigned int validate)
{
	switch (pt->type) {
	case NLA_U8:
	case NLA_U16:
	case NLA_U32:
	case NLA_U64:
	case NLA_MSECS:
	case NLA_BINARY:
		return nla_validate_range_unsigned(pt, nla, extack, validate);
	case NLA_S8:
	case NLA_S16:
	case NLA_S32:
	case NLA_S64:
		return nla_validate_int_range_signed(pt, nla, extack);
	default:
		WARN_ON(1);
		return -EINVAL;
	}
}

static int nla_validate_mask(const struct nla_policy *pt,
			     const struct nlattr *nla,
			     struct netlink_ext_ack *extack)
{
	u64 value;

	switch (pt->type) {
	case NLA_U8:
		value = nla_get_u8(nla);
		break;
	case NLA_U16:
		value = nla_get_u16(nla);
		break;
	case NLA_U32:
		value = nla_get_u32(nla);
		break;
	case NLA_U64:
		value = nla_get_u64(nla);
		break;
	default:
		return -EINVAL;
	}

	if (value & ~(u64)pt->mask) {
		NL_SET_ERR_MSG_ATTR(extack, nla, "reserved bit set");
		return -EINVAL;
	}

	return 0;
}

static int validate_nla(const struct nlattr *nla, int maxtype,
			const struct nla_policy *policy, unsigned int validate,
			struct netlink_ext_ack *extack, unsigned int depth)
{
	u16 strict_start_type = policy[0].strict_start_type;
	const struct nla_policy *pt;
	int minlen = 0, attrlen = nla_len(nla), type = nla_type(nla);
	int err = -ERANGE;

	if (strict_start_type && type >= strict_start_type)
		validate |= NL_VALIDATE_STRICT;

	if (type <= 0 || type > maxtype)
		return 0;

	pt = &policy[type];

	BUG_ON(pt->type > NLA_TYPE_MAX);

	if (nla_attr_len[pt->type] && attrlen != nla_attr_len[pt->type]) {
		pr_warn_ratelimited("netlink: '%s': attribute type %d has an invalid length.\n",
				    current->comm, type);
		if (validate & NL_VALIDATE_STRICT_ATTRS) {
			NL_SET_ERR_MSG_ATTR_POL(extack, nla, pt,
						"invalid attribute length");
			return -EINVAL;
		}
	}

	if (validate & NL_VALIDATE_NESTED) {
		if ((pt->type == NLA_NESTED || pt->type == NLA_NESTED_ARRAY) &&
		    !(nla->nla_type & NLA_F_NESTED)) {
			NL_SET_ERR_MSG_ATTR_POL(extack, nla, pt,
						"NLA_F_NESTED is missing");
			return -EINVAL;
		}
		if (pt->type != NLA_NESTED && pt->type != NLA_NESTED_ARRAY &&
		    pt->type != NLA_UNSPEC && (nla->nla_type & NLA_F_NESTED)) {
			NL_SET_ERR_MSG_ATTR_POL(extack, nla, pt,
						"NLA_F_NESTED not expected");
			return -EINVAL;
		}
	}

	switch (pt->type) {
	case NLA_REJECT:
		if (extack && pt->reject_message) {
			NL_SET_BAD_ATTR(extack, nla);
			extack->_msg = pt->reject_message;
			return -EINVAL;
		}
		err = -EINVAL;
		goto out_err;

	case NLA_FLAG:
		if (attrlen > 0)
			goto out_err;
		break;

	case NLA_BITFIELD32:
		if (attrlen != sizeof(struct nla_bitfield32))
			goto out_err;

		err = validate_nla_bitfield32(nla, pt->bitfield32_valid);
		if (err)
			goto out_err;
		break;

	case NLA_NUL_STRING:
		if (pt->len)
			minlen = min_t(int, attrlen, pt->len + 1);
		else
			minlen = attrlen;

		if (!minlen || memchr(nla_data(nla), '\0', minlen) == NULL) {
			err = -EINVAL;
			goto out_err;
		}
		/* fall through */

	case NLA_STRING:
		if (attrlen < 1)
			goto out_err;

		if (pt->len) {
			char *buf = nla_data(nla);

			if (buf[attrlen - 1] == '\0')
				attrlen--;

			if (attrlen > pt->len)
				goto out_err;
		}
		break;

	case NLA_BINARY:
		if (pt->len && attrlen > pt->len)
			goto out_err;
		break;

	case NLA_NESTED:
		/* a nested attributes is allowed to be empty; if its not,
		 * it must have a size of at least NLA_HDRLEN.
		 */
		if (attrlen == 0)
			break;
		if (attrlen < NLA_HDRLEN)
			goto out_err;
		if (pt->nested_policy) {
			err = __nla_validate_parse(nla_data(nla), nla_len(nla),
						   pt->len, pt->nested_policy,
						   validate, extack, NULL,
						   depth + 1);
			if (err < 0) {
				/*
				 * return directly to preserve the inner
				 * error message/attribute pointer
				 */
				return err;
			}
		}
		break;
	case NLA_NESTED_ARRAY:
		/* a nested array attribute is allowed to be empty; if its not,
		 * it must have a size of at least NLA_HDRLEN.
		 */
		if (attrlen == 0)
			break;
		if (attrlen < NLA_HDRLEN)
			goto out_err;
		if (pt->nested_policy) {
			int err;

			err = nla_validate_array(nla_data(nla), nla_len(nla),
						 pt->len, pt->nested_policy,
						 extack, validate, depth);
			if (err < 0) {
				/*
				 * return directly to preserve the inner
				 * error message/attribute pointer
				 */
				return err;
			}
		}
		break;

	case NLA_UNSPEC:
		if (validate & NL_VALIDATE_UNSPEC) {
			NL_SET_ERR_MSG_ATTR(extack, nla,
					    "Unsupported attribute");
			return -EINVAL;
		}
		if (attrlen < pt->len)
			goto out_err;
		break;

	default:
		if (pt->len)
			minlen = pt->len;
		else
			minlen = nla_attr_minlen[pt->type];

		if (attrlen < minlen)
			goto out_err;
	}

	/* further validation */
	switch (pt->validation_type) {
	case NLA_VALIDATE_NONE:
		/* nothing to do */
		break;
	case NLA_VALIDATE_RANGE_PTR:
	case NLA_VALIDATE_RANGE:
	case NLA_VALIDATE_RANGE_WARN_TOO_LONG:
	case NLA_VALIDATE_MIN:
	case NLA_VALIDATE_MAX:
		err = nla_validate_int_range(pt, nla, extack, validate);
		if (err)
			return err;
		break;
	case NLA_VALIDATE_MASK:
		err = nla_validate_mask(pt, nla, extack);
		if (err)
			return err;
		break;
	case NLA_VALIDATE_FUNCTION:
		if (pt->validate) {
			err = pt->validate(nla, extack);
			if (err)
				return err;
		}
		break;
	}

	return 0;
out_err:
	NL_SET_ERR_MSG_ATTR_POL(extack, nla, pt,
				"Attribute failed policy validation");
	return err;
}

static int __nla_validate_parse(const struct nlattr *head, int len, int maxtype,
				const struct nla_policy *policy,
				unsigned int validate,
				struct netlink_ext_ack *extack,
				struct nlattr **tb, unsigned int depth)
{
	const struct nlattr *nla;
	int rem;

	if (depth >= MAX_POLICY_RECURSION_DEPTH) {
		NL_SET_ERR_MSG(extack,
			       "allowed policy recursion depth exceeded");
		return -EINVAL;
	}

	if (tb)
		memset(tb, 0, sizeof(struct nlattr *) * (maxtype + 1));

	nla_for_each_attr(nla, head, len, rem) {
		u16 type = nla_type(nla);

		if (type == 0 || type > maxtype) {
			if (validate & NL_VALIDATE_MAXTYPE) {
				NL_SET_ERR_MSG_ATTR(extack, nla,
						    "Unknown attribute type");
				return -EINVAL;
			}
			continue;
		}
		if (policy) {
			int err = validate_nla(nla, maxtype, policy,
					       validate, extack, depth);

			if (err < 0)
				return err;
		}

		if (tb)
			tb[type] = (struct nlattr *)nla;
	}

	if (unlikely(rem > 0)) {
		pr_warn_ratelimited("netlink: %d bytes leftover after parsing attributes in process `%s'.\n",
				    rem, current->comm);
		NL_SET_ERR_MSG(extack, "bytes leftover after parsing attributes");
		if (validate & NL_VALIDATE_TRAILING)
			return -EINVAL;
	}

	return 0;
}

/**
 * __nla_validate - Validate a stream of attributes
 * @head: head of attribute stream
 * @len: length of attribute stream
 * @maxtype: maximum attribute type to be expected
 * @policy: validation policy
 * @validate: validation strictness
 * @extack: extended ACK report struct
 *
 * Validates all attributes in the specified attribute stream against the
 * specified policy. Validation depends on the validate flags passed, see
 * &enum netlink_validation for more details on that.
 * See documenation of struct nla_policy for more details.
 *
 * Returns 0 on success or a negative error code.
 */
int __nla_validate(const struct nlattr *head, int len, int maxtype,
		   const struct nla_policy *policy, unsigned int validate,
		   struct netlink_ext_ack *extack)
{
	return __nla_validate_parse(head, len, maxtype, policy, validate,
				    extack, NULL, 0);
}
EXPORT_SYMBOL(__nla_validate);

/**
 * nla_policy_len - Determin the max. length of a policy
 * @policy: policy to use
 * @n: number of policies
 *
 * Determines the max. length of the policy.  It is currently used
 * to allocated Netlink buffers roughly the size of the actual
 * message.
 *
 * Returns 0 on success or a negative error code.
 */
int
nla_policy_len(const struct nla_policy *p, int n)
{
	int i, len = 0;

	for (i = 0; i < n; i++, p++) {
		if (p->len)
			len += nla_total_size(p->len);
		else if (nla_attr_len[p->type])
			len += nla_total_size(nla_attr_len[p->type]);
		else if (nla_attr_minlen[p->type])
			len += nla_total_size(nla_attr_minlen[p->type]);
	}

	return len;
}
EXPORT_SYMBOL(nla_policy_len);

/**
 * __nla_parse - Parse a stream of attributes into a tb buffer
 * @tb: destination array with maxtype+1 elements
 * @maxtype: maximum attribute type to be expected
 * @head: head of attribute stream
 * @len: length of attribute stream
 * @policy: validation policy
 * @validate: validation strictness
 * @extack: extended ACK pointer
 *
 * Parses a stream of attributes and stores a pointer to each attribute in
 * the tb array accessible via the attribute type.
 * Validation is controlled by the @validate parameter.
 *
 * Returns 0 on success or a negative error code.
 */
int __nla_parse(struct nlattr **tb, int maxtype,
		const struct nlattr *head, int len,
		const struct nla_policy *policy, unsigned int validate,
		struct netlink_ext_ack *extack)
{
	return __nla_validate_parse(head, len, maxtype, policy, validate,
				    extack, tb, 0);
}
EXPORT_SYMBOL(__nla_parse);

/**
 * nla_find - Find a specific attribute in a stream of attributes
 * @head: head of attribute stream
 * @len: length of attribute stream
 * @attrtype: type of attribute to look for
 *
 * Returns the first attribute in the stream matching the specified type.
 */
struct nlattr *nla_find(const struct nlattr *head, int len, int attrtype)
{
	const struct nlattr *nla;
	int rem;

	nla_for_each_attr(nla, head, len, rem)
		if (nla_type(nla) == attrtype)
			return (struct nlattr *)nla;

	return NULL;
}
EXPORT_SYMBOL(nla_find);

/**
 * nla_strscpy - Copy string attribute payload into a sized buffer
 * @dst: Where to copy the string to.
 * @nla: Attribute to copy the string from.
 * @dstsize: Size of destination buffer.
 *
 * Copies at most dstsize - 1 bytes into the destination buffer.
 * Unlike strlcpy the destination buffer is always padded out.
 *
 * Return:
 * * srclen - Returns @nla length (not including the trailing %NUL).
 * * -E2BIG - If @dstsize is 0 or greater than U16_MAX or @nla length greater
 *            than @dstsize.
 */
ssize_t nla_strscpy(char *dst, const struct nlattr *nla, size_t dstsize)
{
	size_t srclen = nla_len(nla);
	char *src = nla_data(nla);
	ssize_t ret;
	size_t len;

	if (dstsize == 0 || WARN_ON_ONCE(dstsize > U16_MAX))
		return -E2BIG;

	if (srclen > 0 && src[srclen - 1] == '\0')
		srclen--;

	if (srclen >= dstsize) {
		len = dstsize - 1;
		ret = -E2BIG;
	} else {
		len = srclen;
		ret = len;
	}

	memcpy(dst, src, len);
	/* Zero pad end of dst. */
	memset(dst + len, 0, dstsize - len);

	return ret;
}
EXPORT_SYMBOL(nla_strscpy);

/**
 * nla_strdup - Copy string attribute payload into a newly allocated buffer
 * @nla: attribute to copy the string from
 * @flags: the type of memory to allocate (see kmalloc).
 *
 * Returns a pointer to the allocated buffer or NULL on error.
 */
char *nla_strdup(const struct nlattr *nla, gfp_t flags)
{
	size_t srclen = nla_len(nla);
	char *src = nla_data(nla), *dst;

	if (srclen > 0 && src[srclen - 1] == '\0')
		srclen--;

	dst = kmalloc(srclen + 1, flags);
	if (dst != NULL) {
		memcpy(dst, src, srclen);
		dst[srclen] = '\0';
	}
	return dst;
}
EXPORT_SYMBOL(nla_strdup);

/**
 * nla_memcpy - Copy a netlink attribute into another memory area
 * @dest: where to copy to memcpy
 * @src: netlink attribute to copy from
 * @count: size of the destination area
 *
 * Note: The number of bytes copied is limited by the length of
 *       attribute's payload. memcpy
 *
 * Returns the number of bytes copied.
 */
int nla_memcpy(void *dest, const struct nlattr *src, int count)
{
	int minlen = min_t(int, count, nla_len(src));

	memcpy(dest, nla_data(src), minlen);
	if (count > minlen)
		memset(dest + minlen, 0, count - minlen);

	return minlen;
}
EXPORT_SYMBOL(nla_memcpy);

/**
 * nla_memcmp - Compare an attribute with sized memory area
 * @nla: netlink attribute
 * @data: memory area
 * @size: size of memory area
 */
int nla_memcmp(const struct nlattr *nla, const void *data,
			     size_t size)
{
	int d = nla_len(nla) - size;

	if (d == 0)
		d = memcmp(nla_data(nla), data, size);

	return d;
}
EXPORT_SYMBOL(nla_memcmp);

/**
 * nla_strcmp - Compare a string attribute against a string
 * @nla: netlink string attribute
 * @str: another string
 */
int nla_strcmp(const struct nlattr *nla, const char *str)
{
	int len = strlen(str);
	char *buf = nla_data(nla);
	int attrlen = nla_len(nla);
	int d;

	if (attrlen > 0 && buf[attrlen - 1] == '\0')
		attrlen--;

	d = attrlen - len;
	if (d == 0)
		d = memcmp(nla_data(nla), str, len);

	return d;
}
EXPORT_SYMBOL(nla_strcmp);

#ifdef CONFIG_NET
/**
 * __nla_reserve - reserve room for attribute on the skb
 * @skb: socket buffer to reserve room on
 * @attrtype: attribute type
 * @attrlen: length of attribute payload
 *
 * Adds a netlink attribute header to a socket buffer and reserves
 * room for the payload but does not copy it.
 *
 * The caller is responsible to ensure that the skb provides enough
 * tailroom for the attribute header and payload.
 */
struct nlattr *__nla_reserve(struct sk_buff *skb, int attrtype, int attrlen)
{
	struct nlattr *nla;

	nla = skb_put(skb, nla_total_size(attrlen));
	nla->nla_type = attrtype;
	nla->nla_len = nla_attr_size(attrlen);

	memset((unsigned char *) nla + nla->nla_len, 0, nla_padlen(attrlen));

	return nla;
}
EXPORT_SYMBOL(__nla_reserve);

/**
 * __nla_reserve_64bit - reserve room for attribute on the skb and align it
 * @skb: socket buffer to reserve room on
 * @attrtype: attribute type
 * @attrlen: length of attribute payload
 * @padattr: attribute type for the padding
 *
 * Adds a netlink attribute header to a socket buffer and reserves
 * room for the payload but does not copy it. It also ensure that this
 * attribute will have a 64-bit aligned nla_data() area.
 *
 * The caller is responsible to ensure that the skb provides enough
 * tailroom for the attribute header and payload.
 */
struct nlattr *__nla_reserve_64bit(struct sk_buff *skb, int attrtype,
				   int attrlen, int padattr)
{
	nla_align_64bit(skb, padattr);

	return __nla_reserve(skb, attrtype, attrlen);
}
EXPORT_SYMBOL(__nla_reserve_64bit);

/**
 * __nla_reserve_nohdr - reserve room for attribute without header
 * @skb: socket buffer to reserve room on
 * @attrlen: length of attribute payload
 *
 * Reserves room for attribute payload without a header.
 *
 * The caller is responsible to ensure that the skb provides enough
 * tailroom for the payload.
 */
void *__nla_reserve_nohdr(struct sk_buff *skb, int attrlen)
{
	return skb_put_zero(skb, NLA_ALIGN(attrlen));
}
EXPORT_SYMBOL(__nla_reserve_nohdr);

/**
 * nla_reserve - reserve room for attribute on the skb
 * @skb: socket buffer to reserve room on
 * @attrtype: attribute type
 * @attrlen: length of attribute payload
 *
 * Adds a netlink attribute header to a socket buffer and reserves
 * room for the payload but does not copy it.
 *
 * Returns NULL if the tailroom of the skb is insufficient to store
 * the attribute header and payload.
 */
struct nlattr *nla_reserve(struct sk_buff *skb, int attrtype, int attrlen)
{
	if (unlikely(skb_tailroom(skb) < nla_total_size(attrlen)))
		return NULL;

	return __nla_reserve(skb, attrtype, attrlen);
}
EXPORT_SYMBOL(nla_reserve);

/**
 * nla_reserve_64bit - reserve room for attribute on the skb and align it
 * @skb: socket buffer to reserve room on
 * @attrtype: attribute type
 * @attrlen: length of attribute payload
 * @padattr: attribute type for the padding
 *
 * Adds a netlink attribute header to a socket buffer and reserves
 * room for the payload but does not copy it. It also ensure that this
 * attribute will have a 64-bit aligned nla_data() area.
 *
 * Returns NULL if the tailroom of the skb is insufficient to store
 * the attribute header and payload.
 */
struct nlattr *nla_reserve_64bit(struct sk_buff *skb, int attrtype, int attrlen,
				 int padattr)
{
	size_t len;

	if (nla_need_padding_for_64bit(skb))
		len = nla_total_size_64bit(attrlen);
	else
		len = nla_total_size(attrlen);
	if (unlikely(skb_tailroom(skb) < len))
		return NULL;

	return __nla_reserve_64bit(skb, attrtype, attrlen, padattr);
}
EXPORT_SYMBOL(nla_reserve_64bit);

/**
 * nla_reserve_nohdr - reserve room for attribute without header
 * @skb: socket buffer to reserve room on
 * @attrlen: length of attribute payload
 *
 * Reserves room for attribute payload without a header.
 *
 * Returns NULL if the tailroom of the skb is insufficient to store
 * the attribute payload.
 */
void *nla_reserve_nohdr(struct sk_buff *skb, int attrlen)
{
	if (unlikely(skb_tailroom(skb) < NLA_ALIGN(attrlen)))
		return NULL;

	return __nla_reserve_nohdr(skb, attrlen);
}
EXPORT_SYMBOL(nla_reserve_nohdr);

/**
 * __nla_put - Add a netlink attribute to a socket buffer
 * @skb: socket buffer to add attribute to
 * @attrtype: attribute type
 * @attrlen: length of attribute payload
 * @data: head of attribute payload
 *
 * The caller is responsible to ensure that the skb provides enough
 * tailroom for the attribute header and payload.
 */
void __nla_put(struct sk_buff *skb, int attrtype, int attrlen,
			     const void *data)
{
	struct nlattr *nla;

	nla = __nla_reserve(skb, attrtype, attrlen);
	memcpy(nla_data(nla), data, attrlen);
}
EXPORT_SYMBOL(__nla_put);

/**
 * __nla_put_64bit - Add a netlink attribute to a socket buffer and align it
 * @skb: socket buffer to add attribute to
 * @attrtype: attribute type
 * @attrlen: length of attribute payload
 * @data: head of attribute payload
 * @padattr: attribute type for the padding
 *
 * The caller is responsible to ensure that the skb provides enough
 * tailroom for the attribute header and payload.
 */
void __nla_put_64bit(struct sk_buff *skb, int attrtype, int attrlen,
		     const void *data, int padattr)
{
	struct nlattr *nla;

	nla = __nla_reserve_64bit(skb, attrtype, attrlen, padattr);
	memcpy(nla_data(nla), data, attrlen);
}
EXPORT_SYMBOL(__nla_put_64bit);

/**
 * __nla_put_nohdr - Add a netlink attribute without header
 * @skb: socket buffer to add attribute to
 * @attrlen: length of attribute payload
 * @data: head of attribute payload
 *
 * The caller is responsible to ensure that the skb provides enough
 * tailroom for the attribute payload.
 */
void __nla_put_nohdr(struct sk_buff *skb, int attrlen, const void *data)
{
	void *start;

	start = __nla_reserve_nohdr(skb, attrlen);
	memcpy(start, data, attrlen);
}
EXPORT_SYMBOL(__nla_put_nohdr);

/**
 * nla_put - Add a netlink attribute to a socket buffer
 * @skb: socket buffer to add attribute to
 * @attrtype: attribute type
 * @attrlen: length of attribute payload
 * @data: head of attribute payload
 *
 * Returns -EMSGSIZE if the tailroom of the skb is insufficient to store
 * the attribute header and payload.
 */
int nla_put(struct sk_buff *skb, int attrtype, int attrlen, const void *data)
{
	if (unlikely(skb_tailroom(skb) < nla_total_size(attrlen)))
		return -EMSGSIZE;

	__nla_put(skb, attrtype, attrlen, data);
	return 0;
}
EXPORT_SYMBOL(nla_put);

/**
 * nla_put_64bit - Add a netlink attribute to a socket buffer and align it
 * @skb: socket buffer to add attribute to
 * @attrtype: attribute type
 * @attrlen: length of attribute payload
 * @data: head of attribute payload
 * @padattr: attribute type for the padding
 *
 * Returns -EMSGSIZE if the tailroom of the skb is insufficient to store
 * the attribute header and payload.
 */
int nla_put_64bit(struct sk_buff *skb, int attrtype, int attrlen,
		  const void *data, int padattr)
{
	size_t len;

	if (nla_need_padding_for_64bit(skb))
		len = nla_total_size_64bit(attrlen);
	else
		len = nla_total_size(attrlen);
	if (unlikely(skb_tailroom(skb) < len))
		return -EMSGSIZE;

	__nla_put_64bit(skb, attrtype, attrlen, data, padattr);
	return 0;
}
EXPORT_SYMBOL(nla_put_64bit);

/**
 * nla_put_nohdr - Add a netlink attribute without header
 * @skb: socket buffer to add attribute to
 * @attrlen: length of attribute payload
 * @data: head of attribute payload
 *
 * Returns -EMSGSIZE if the tailroom of the skb is insufficient to store
 * the attribute payload.
 */
int nla_put_nohdr(struct sk_buff *skb, int attrlen, const void *data)
{
	if (unlikely(skb_tailroom(skb) < NLA_ALIGN(attrlen)))
		return -EMSGSIZE;

	__nla_put_nohdr(skb, attrlen, data);
	return 0;
}
EXPORT_SYMBOL(nla_put_nohdr);

/**
 * nla_append - Add a netlink attribute without header or padding
 * @skb: socket buffer to add attribute to
 * @attrlen: length of attribute payload
 * @data: head of attribute payload
 *
 * Returns -EMSGSIZE if the tailroom of the skb is insufficient to store
 * the attribute payload.
 */
int nla_append(struct sk_buff *skb, int attrlen, const void *data)
{
	if (unlikely(skb_tailroom(skb) < NLA_ALIGN(attrlen)))
		return -EMSGSIZE;

	skb_put_data(skb, data, attrlen);
	return 0;
}
EXPORT_SYMBOL(nla_append);
#endif
