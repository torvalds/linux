// SPDX-License-Identifier: LGPL-2.1

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

#include <errno.h>
#include "nlattr.h"
#include <linux/rtnetlink.h>
#include <string.h>
#include <stdio.h>

static uint16_t nla_attr_minlen[NLA_TYPE_MAX+1] = {
	[NLA_U8]	= sizeof(uint8_t),
	[NLA_U16]	= sizeof(uint16_t),
	[NLA_U32]	= sizeof(uint32_t),
	[NLA_U64]	= sizeof(uint64_t),
	[NLA_STRING]	= 1,
	[NLA_FLAG]	= 0,
};

static int nla_len(const struct nlattr *nla)
{
	return nla->nla_len - NLA_HDRLEN;
}

static struct nlattr *nla_next(const struct nlattr *nla, int *remaining)
{
	int totlen = NLA_ALIGN(nla->nla_len);

	*remaining -= totlen;
	return (struct nlattr *) ((char *) nla + totlen);
}

static int nla_ok(const struct nlattr *nla, int remaining)
{
	return remaining >= sizeof(*nla) &&
	       nla->nla_len >= sizeof(*nla) &&
	       nla->nla_len <= remaining;
}

static void *nla_data(const struct nlattr *nla)
{
	return (char *) nla + NLA_HDRLEN;
}

static int nla_type(const struct nlattr *nla)
{
	return nla->nla_type & NLA_TYPE_MASK;
}

static int validate_nla(struct nlattr *nla, int maxtype,
			struct nla_policy *policy)
{
	struct nla_policy *pt;
	unsigned int minlen = 0;
	int type = nla_type(nla);

	if (type < 0 || type > maxtype)
		return 0;

	pt = &policy[type];

	if (pt->type > NLA_TYPE_MAX)
		return 0;

	if (pt->minlen)
		minlen = pt->minlen;
	else if (pt->type != NLA_UNSPEC)
		minlen = nla_attr_minlen[pt->type];

	if (nla_len(nla) < minlen)
		return -1;

	if (pt->maxlen && nla_len(nla) > pt->maxlen)
		return -1;

	if (pt->type == NLA_STRING) {
		char *data = nla_data(nla);
		if (data[nla_len(nla) - 1] != '\0')
			return -1;
	}

	return 0;
}

static inline int nlmsg_len(const struct nlmsghdr *nlh)
{
	return nlh->nlmsg_len - NLMSG_HDRLEN;
}

/**
 * Create attribute index based on a stream of attributes.
 * @arg tb		Index array to be filled (maxtype+1 elements).
 * @arg maxtype		Maximum attribute type expected and accepted.
 * @arg head		Head of attribute stream.
 * @arg len		Length of attribute stream.
 * @arg policy		Attribute validation policy.
 *
 * Iterates over the stream of attributes and stores a pointer to each
 * attribute in the index array using the attribute type as index to
 * the array. Attribute with a type greater than the maximum type
 * specified will be silently ignored in order to maintain backwards
 * compatibility. If \a policy is not NULL, the attribute will be
 * validated using the specified policy.
 *
 * @see nla_validate
 * @return 0 on success or a negative error code.
 */
static int nla_parse(struct nlattr *tb[], int maxtype, struct nlattr *head, int len,
		     struct nla_policy *policy)
{
	struct nlattr *nla;
	int rem, err;

	memset(tb, 0, sizeof(struct nlattr *) * (maxtype + 1));

	nla_for_each_attr(nla, head, len, rem) {
		int type = nla_type(nla);

		if (type > maxtype)
			continue;

		if (policy) {
			err = validate_nla(nla, maxtype, policy);
			if (err < 0)
				goto errout;
		}

		if (tb[type])
			fprintf(stderr, "Attribute of type %#x found multiple times in message, "
				  "previous attribute is being ignored.\n", type);

		tb[type] = nla;
	}

	err = 0;
errout:
	return err;
}

/* dump netlink extended ack error message */
int nla_dump_errormsg(struct nlmsghdr *nlh)
{
	struct nla_policy extack_policy[NLMSGERR_ATTR_MAX + 1] = {
		[NLMSGERR_ATTR_MSG]	= { .type = NLA_STRING },
		[NLMSGERR_ATTR_OFFS]	= { .type = NLA_U32 },
	};
	struct nlattr *tb[NLMSGERR_ATTR_MAX + 1], *attr;
	struct nlmsgerr *err;
	char *errmsg = NULL;
	int hlen, alen;

	/* no TLVs, nothing to do here */
	if (!(nlh->nlmsg_flags & NLM_F_ACK_TLVS))
		return 0;

	err = (struct nlmsgerr *)NLMSG_DATA(nlh);
	hlen = sizeof(*err);

	/* if NLM_F_CAPPED is set then the inner err msg was capped */
	if (!(nlh->nlmsg_flags & NLM_F_CAPPED))
		hlen += nlmsg_len(&err->msg);

	attr = (struct nlattr *) ((void *) err + hlen);
	alen = nlh->nlmsg_len - hlen;

	if (nla_parse(tb, NLMSGERR_ATTR_MAX, attr, alen, extack_policy) != 0) {
		fprintf(stderr,
			"Failed to parse extended error attributes\n");
		return 0;
	}

	if (tb[NLMSGERR_ATTR_MSG])
		errmsg = (char *) nla_data(tb[NLMSGERR_ATTR_MSG]);

	fprintf(stderr, "Kernel error message: %s\n", errmsg);

	return 0;
}
