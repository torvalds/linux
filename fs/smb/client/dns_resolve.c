// SPDX-License-Identifier: LGPL-2.1
/*
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

#include <linux/inet.h>
#include <linux/slab.h>
#include <linux/dns_resolver.h>
#include "dns_resolve.h"
#include "cifsglob.h"
#include "cifsproto.h"
#include "cifs_debug.h"

static int resolve_name(const char *name, size_t namelen, struct sockaddr *addr)
{
	char *ip;
	int rc;

	rc = dns_query(current->nsproxy->net_ns, NULL, name,
		       namelen, NULL, &ip, NULL, false);
	if (rc < 0) {
		cifs_dbg(FYI, "%s: unable to resolve: %*.*s\n",
			 __func__, (int)namelen, (int)namelen, name);
	} else {
		cifs_dbg(FYI, "%s: resolved: %*.*s to %s\n",
			 __func__, (int)namelen, (int)namelen, name, ip);

		rc = cifs_convert_address(addr, ip, strlen(ip));
		kfree(ip);
		if (!rc) {
			cifs_dbg(FYI, "%s: unable to determine ip address\n",
				 __func__);
			rc = -EHOSTUNREACH;
		} else {
			rc = 0;
		}
	}
	return rc;
}

/**
 * dns_resolve_name - Perform an upcall to resolve hostname to an ip address.
 * @dom: DNS domain name (or NULL)
 * @name: Name to look up
 * @namelen: Length of name
 * @ip_addr: Where to return the IP address
 *
 * Returns zero on success, -ve code otherwise.
 */
int dns_resolve_name(const char *dom, const char *name,
		     size_t namelen, struct sockaddr *ip_addr)
{
	size_t len;
	char *s;
	int rc;

	cifs_dbg(FYI, "%s: dom=%s name=%.*s\n", __func__, dom, (int)namelen, name);
	if (!ip_addr || !name || !*name || !namelen)
		return -EINVAL;

	cifs_dbg(FYI, "%s: hostname=%.*s\n", __func__, (int)namelen, name);
	/* Try to interpret hostname as an IPv4 or IPv6 address */
	rc = cifs_convert_address(ip_addr, name, namelen);
	if (rc > 0) {
		cifs_dbg(FYI, "%s: unc is IP, skipping dns upcall: %*.*s\n",
			 __func__, (int)namelen, (int)namelen, name);
		return 0;
	}

	/*
	 * If @name contains a NetBIOS name and @dom has been specified, then
	 * convert @name to an FQDN and try resolving it first.
	 */
	if (dom && *dom && cifs_netbios_name(name, namelen)) {
		len = strnlen(dom, CIFS_MAX_DOMAINNAME_LEN) + namelen + 2;
		s = kmalloc(len, GFP_KERNEL);
		if (!s)
			return -ENOMEM;

		scnprintf(s, len, "%.*s.%s", (int)namelen, name, dom);
		rc = resolve_name(s, len - 1, ip_addr);
		kfree(s);
		if (!rc)
			return 0;
	}
	return resolve_name(name, namelen, ip_addr);
}
