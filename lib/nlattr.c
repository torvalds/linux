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

static int validate_nla_bitfield32(const struct nlattr *nla,
				   u32 *valid_flags_allowed)
{
	const struct nla_bitfield32 *bf = nla_data(nla);
	u32 *valid_flags_mask = valid_flags_allowed;

	if (!valid_flags_allowed)
		return -EINVAL;

	/*disallow invalid bit selector */
	if (bf->selector & ~*valid_flags_mask)
		return -EINVAL;

	/*disallow invalid bit values */
	if (bf->value & ~*valid_flags_mask)
		return -EINVAL;

	/*disallow valid bit values that are not selected*/
	if (bf->value & ~bf->selector)
		return -EINVAL;

	return 0;
}

static int validate_nla(const struct nlattr *nla, int maxtype,
			const struct nla_policy *policy)
{
	const struct nla_policy *pt;
	int minlen = 0, attrlen = nla_len(nla), type = nla_type(nla);

	if (type <= 0 || type > maxtype)
		return 0;

	pt = &policy[type];

	BUG_ON(pt->type > NLA_TYPE_MAX);

	switch (pt->type) {
	case NLA_FLAG:
		if (attrlen > 0)
			return -ERANGE;
		break;

	case NLA_BITFIELD32:
		if (attrlen != sizeof(struct nla_bitfield32))
			return -ERANGE;

		return validate_nla_bitfield32(nla, pt->validation_data);

	case NLA_NUL_STRING:
		if (pt->len)
			minlen = min_t(int, attrlen, pt->len + 1);
		else
			minlen = attrlen;

		if (!minlen || memchr(nla_data(nla), '\0', minlen) == NULL)
			return -EINVAL;
		/* fall through */

	case NLA_STRING:
		if (attrlen < 1)
			return -ERANGE;

		if (pt->len) {
			char *buf = nla_data(nla);

			if (buf[attrlen - 1] == '\0')
				attrlen--;

			if (attrlen > pt->len)
				return -ERANGE;
		}
		break;

	case NLA_BINARY:
		if (pt->len && attrlen > pt->len)
			return -ERANGE;
		break;

	case NLA_NESTED_COMPAT:
		if (attrlen < pt->len)
			return -ERANGE;
		if (attrlen < NLA_ALIGN(pt->len))
			break;
		if (attrlen < NLA_ALIGN(pt->len) + NLA_HDRLEN)
			return -ERANGE;
		nla = nla_data(nla) + NLA_ALIGN(pt->len);
		if (attrlen < NLA_ALIGN(pt->len) + NLA_HDRLEN + nla_len(nla))
			return -ERANGE;
		break;
	case NLA_NESTED:
		/* a nested attributes is allowed to be empty; if its not,
		 * it must have a size of at least NLA_HDRLEN.
		 */
		if (attrlen == 0)
			break;
	default:
		if (pt->len)
			minlen = pt->len;
		else if (pt->type != NLA_UNSPEC)
			minlen = nla_attr_minlen[pt->type];

		if (attrlen < minlen)
			return -ERANGE;
	}

	return 0;
}

/**
 * nla_validate - Validate a stream of attributes
 * @head: head of attribute stream
 * @len: length of attribute stream
 * @maxtype: maximum attribute type to be expected
 * @policy: validation policy
 * @extack: extended ACK report struct
 *
 * Validates all attributes in the specified attribute stream against the
 * specified policy. Attributes with a type exceeding maxtype will be
 * ignored. See documenation of struct nla_policy for more details.
 *
 * Returns 0 on success or a negative error code.
 */
int nla_validate(const struct nlattr *head, int len, int maxtype,
		 const struct nla_policy *policy,
		 struct netlink_ext_ack *extack)
{
	const struct nlattr *nla;
	int rem;

	nla_for_each_attr(nla, head, len, rem) {
		int err = validate_nla(nla, maxtype, policy);

		if (err < 0) {
			if (extack)
				extack->bad_attr = nla;
			return err;
		}
	}

	return 0;
}
EXPORT_SYMBOL(nla_validate);

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
		else if (nla_attr_minlen[p->type])
			len += nla_total_size(nla_attr_minlen[p->type]);
	}

	return len;
}
EXPORT_SYMBOL(nla_policy_len);

/**
 * nla_parse - Parse a stream of attributes into a tb buffer
 * @tb: destination array with maxtype+1 elements
 * @maxtype: maximum attribute type to be expected
 * @head: head of attribute stream
 * @len: length of attribute stream
 * @policy: validation policy
 *
 * Parses a stream of attributes and stores a pointer to each attribute in
 * the tb array accessible via the attribute type. Attributes with a type
 * exceeding maxtype will be silently ignored for backwards compatibility
 * reasons. policy may be set to NULL if no validation is required.
 *
 * Returns 0 on success or a negative error code.
 */
int nla_parse(struct nlattr **tb, int maxtype, const struct nlattr *head,
	      int len, const struct nla_policy *policy,
	      struct netlink_ext_ack *extack)
{
	const struct nlattr *nla;
	int rem, err;

	memset(tb, 0, sizeof(struct nlattr *) * (maxtype + 1));

	nla_for_each_attr(nla, head, len, rem) {
		u16 type = nla_type(nla);

		if (type > 0 && type <= maxtype) {
			if (policy) {
				err = validate_nla(nla, maxtype, policy);
				if (err < 0) {
					if (extack)
						extack->bad_attr = nla;
					goto errout;
				}
			}

			tb[type] = (struct nlattr *)nla;
		}
	}

	if (unlikely(rem > 0))
		pr_warn_ratelimited("netlink: %d bytes leftover after parsing attributes in process `%s'.\n",
				    rem, current->comm);

	err = 0;
errout:
	return err;
}
EXPORT_SYMBOL(nla_parse);

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
 * nla_strlcpy - Copy string attribute payload into a sized buffer
 * @dst: where to copy the string to
 * @nla: attribute to copy the string from
 * @dstsize: size of destination buffer
 *
 * Copies at most dstsize - 1 bytes into the destination buffer.
 * The result is always a valid NUL-terminated string. Unlike
 * strlcpy the destination buffer is always padded out.
 *
 * Returns the length of the source buffer.
 */
size_t nla_strlcpy(char *dst, const struct nlattr *nla, size_t dstsize)
{
	size_t srclen = nla_len(nla);
	char *src = nla_data(nla);

	if (srclen > 0 && src[srclen - 1] == '\0')
		srclen--;

	if (dstsize > 0) {
		size_t len = (srclen >= dstsize) ? dstsize - 1 : srclen;

		memset(dst, 0, dstsize);
		memcpy(dst, src, len);
	}

	return srclen;
}
EXPORT_SYMBOL(nla_strlcpy);

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
	if (nla_need_padding_for_64bit(skb))
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
