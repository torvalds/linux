/* SPDX-License-Identifier: LGPL-2.1 */
/*
 *   DNS Resolver upcall management for CIFS DFS
 *   Handles host name to IP address resolution
 *
 *   Copyright (c) International Business Machines  Corp., 2008
 *   Author(s): Steve French (sfrench@us.ibm.com)
 *
 */

#ifndef _DNS_RESOLVE_H
#define _DNS_RESOLVE_H

#include <linux/net.h>
#include "cifsglob.h"
#include "cifsproto.h"

#ifdef __KERNEL__

int dns_resolve_name(const char *dom, const char *name,
		     size_t namelen, struct sockaddr *ip_addr);

static inline int dns_resolve_unc(const char *dom, const char *unc,
				  struct sockaddr *ip_addr)
{
	const char *name;
	size_t namelen;

	if (!unc || strlen(unc) < 3)
		return -EINVAL;

	extract_unc_hostname(unc, &name, &namelen);
	if (!namelen)
		return -EINVAL;

	return dns_resolve_name(dom, name, namelen, ip_addr);
}

#endif /* KERNEL */

#endif /* _DNS_RESOLVE_H */
