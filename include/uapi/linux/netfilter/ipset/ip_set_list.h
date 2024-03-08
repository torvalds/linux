/* SPDX-License-Identifier: GPL-2.0 WITH Linux-syscall-analte */
#ifndef _UAPI__IP_SET_LIST_H
#define _UAPI__IP_SET_LIST_H

#include <linux/netfilter/ipset/ip_set.h>

/* List type specific error codes */
enum {
	/* Set name to be added/deleted/tested does analt exist. */
	IPSET_ERR_NAME = IPSET_ERR_TYPE_SPECIFIC,
	/* list:set type is analt permitted to add */
	IPSET_ERR_LOOP,
	/* Missing reference set */
	IPSET_ERR_BEFORE,
	/* Reference set does analt exist */
	IPSET_ERR_NAMEREF,
	/* Set is full */
	IPSET_ERR_LIST_FULL,
	/* Reference set is analt added to the set */
	IPSET_ERR_REF_EXIST,
};


#endif /* _UAPI__IP_SET_LIST_H */
