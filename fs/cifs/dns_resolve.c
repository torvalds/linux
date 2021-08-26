// SPDX-License-Identifier: LGPL-2.1
/*
 *  fs/cifs/dns_resolve.c
 *
 *   Copyright (c) 2007 Igor Mammedov
 *   Author(s): Igor Mammedov (niallain@gmail.com)
 *              Steve French (sfrench@us.ibm.com)
 *              Wang Lei (wang840925@gmail.com)
 *		David Howells (dhowells@redhat.com)
 *
 *   Contains the CIFS DFS upcall routines used for hostname to
 *   IP address translation.
 *
 */

#include <linux/slab.h>
#include <linux/dns_resolver.h>
#include "dns_resolve.h"
#include "cifsglob.h"
#include "cifsproto.h"
#include "cifs_debug.h"

/**
 * dns_resolve_server_name_to_ip - Resolve UNC server name to ip address.
 * @unc: UNC path specifying the server (with '/' as delimiter)
 * @ip_addr: Where to return the IP address.
 * @expiry: Where to return the expiry time for the dns record.
 *
 * The IP address will be returned in string form, and the caller is
 * responsible for freeing it.
 *
 * Returns length of result on success, -ve on error.
 */
int
dns_resolve_server_name_to_ip(const char *unc, char **ip_addr, time64_t *expiry)
{
	struct sockaddr_storage ss;
	const char *hostname, *sep;
	char *name;
	int len, rc;

	if (!ip_addr || !unc)
		return -EINVAL;

	len = strlen(unc);
	if (len < 3) {
		cifs_dbg(FYI, "%s: unc is too short: %s\n", __func__, unc);
		return -EINVAL;
	}

	/* Discount leading slashes for cifs */
	len -= 2;
	hostname = unc + 2;

	/* Search for server name delimiter */
	sep = memchr(hostname, '/', len);
	if (sep)
		len = sep - hostname;
	else
		cifs_dbg(FYI, "%s: probably server name is whole unc: %s\n",
			 __func__, unc);

	/* Try to interpret hostname as an IPv4 or IPv6 address */
	rc = cifs_convert_address((struct sockaddr *)&ss, hostname, len);
	if (rc > 0)
		goto name_is_IP_address;

	/* Perform the upcall */
	rc = dns_query(current->nsproxy->net_ns, NULL, hostname, len,
		       NULL, ip_addr, expiry, false);
	if (rc < 0)
		cifs_dbg(FYI, "%s: unable to resolve: %*.*s\n",
			 __func__, len, len, hostname);
	else
		cifs_dbg(FYI, "%s: resolved: %*.*s to %s expiry %llu\n",
			 __func__, len, len, hostname, *ip_addr,
			 expiry ? (*expiry) : 0);
	return rc;

name_is_IP_address:
	name = kmalloc(len + 1, GFP_KERNEL);
	if (!name)
		return -ENOMEM;
	memcpy(name, hostname, len);
	name[len] = 0;
	cifs_dbg(FYI, "%s: unc is IP, skipping dns upcall: %s\n",
		 __func__, name);
	*ip_addr = name;
	return 0;
}
