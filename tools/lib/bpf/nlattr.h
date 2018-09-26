/* SPDX-License-Identifier: LGPL-2.1 */

/*
 * NETLINK      Netlink attributes
 *
 *	This library is free software; you can redistribute it and/or
 *	modify it under the terms of the GNU Lesser General Public
 *	License as published by the Free Software Foundation version 2.1
 *	of the License.
 *
 * Copyright (c) 2003-2013 Thomas Graf <tgraf@suug.ch>
 */

#ifndef __NLATTR_H
#define __NLATTR_H

#include <stdint.h>
#include <linux/netlink.h>
/* avoid multiple definition of netlink features */
#define __LINUX_NETLINK_H

/**
 * Standard attribute types to specify validation policy
 */
enum {
	NLA_UNSPEC,	/**< Unspecified type, binary data chunk */
	NLA_U8,		/**< 8 bit integer */
	NLA_U16,	/**< 16 bit integer */
	NLA_U32,	/**< 32 bit integer */
	NLA_U64,	/**< 64 bit integer */
	NLA_STRING,	/**< NUL terminated character string */
	NLA_FLAG,	/**< Flag */
	NLA_MSECS,	/**< Micro seconds (64bit) */
	NLA_NESTED,	/**< Nested attributes */
	__NLA_TYPE_MAX,
};

#define NLA_TYPE_MAX (__NLA_TYPE_MAX - 1)

/**
 * @ingroup attr
 * Attribute validation policy.
 *
 * See section @core_doc{core_attr_parse,Attribute Parsing} for more details.
 */
struct nla_policy {
	/** Type of attribute or NLA_UNSPEC */
	uint16_t	type;

	/** Minimal length of payload required */
	uint16_t	minlen;

	/** Maximal length of payload allowed */
	uint16_t	maxlen;
};

/**
 * @ingroup attr
 * Iterate over a stream of attributes
 * @arg pos	loop counter, set to current attribute
 * @arg head	head of attribute stream
 * @arg len	length of attribute stream
 * @arg rem	initialized to len, holds bytes currently remaining in stream
 */
#define nla_for_each_attr(pos, head, len, rem) \
	for (pos = head, rem = len; \
	     nla_ok(pos, rem); \
	     pos = nla_next(pos, &(rem)))

/**
 * nla_data - head of payload
 * @nla: netlink attribute
 */
static inline void *nla_data(const struct nlattr *nla)
{
	return (char *) nla + NLA_HDRLEN;
}

static inline uint8_t nla_getattr_u8(const struct nlattr *nla)
{
	return *(uint8_t *)nla_data(nla);
}

static inline uint32_t nla_getattr_u32(const struct nlattr *nla)
{
	return *(uint32_t *)nla_data(nla);
}

static inline const char *nla_getattr_str(const struct nlattr *nla)
{
	return (const char *)nla_data(nla);
}

/**
 * nla_len - length of payload
 * @nla: netlink attribute
 */
static inline int nla_len(const struct nlattr *nla)
{
	return nla->nla_len - NLA_HDRLEN;
}

int nla_parse(struct nlattr *tb[], int maxtype, struct nlattr *head, int len,
	      struct nla_policy *policy);
int nla_parse_nested(struct nlattr *tb[], int maxtype, struct nlattr *nla,
		     struct nla_policy *policy);

int nla_dump_errormsg(struct nlmsghdr *nlh);

#endif /* __NLATTR_H */
