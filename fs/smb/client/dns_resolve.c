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

static int resolve_name(const char *name, size_t namelen,
			struct sockaddr *addr, time64_t *expiry)
{
	char *ip;
	int rc;

	rc = dns_query(current->nsproxy->net_ns, NULL, name,
		       namelen, NULL, &ip, expiry, false);
	if (rc < 0) {
		cifs_dbg(FYI, "%s: unable to resolve: %*.*s\n",
			 __func__, (int)namelen, (int)namelen, name);
	} else {
		cifs_dbg(FYI, "%s: resolved: %*.*s to %s expiry %llu\n",
			 __func__, (int)namelen, (int)namelen, name, ip,
			 expiry ? (*expiry) : 0);

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
 * dns_resolve_server_name_to_ip - Resolve UNC server name to ip address.
 * @dom: optional DNS domain name
 * @unc: UNC path specifying the server (with '/' as delimiter)
 * @ip_addr: Where to return the IP address.
 * @expiry: Where to return the expiry time for the dns record.
 *
 * Returns zero success, -ve on error.
 */
int dns_resolve_server_name_to_ip(const char *dom, const char *unc,
				  struct sockaddr *ip_addr, time64_t *expiry)
{
	const char *name;
	size_t namelen, len;
	char *s;
	int rc;

	if (!ip_addr || !unc)
		return -EINVAL;

	cifs_dbg(FYI, "%s: dom=%s unc=%s\n", __func__, dom, unc);
	if (strlen(unc) < 3)
		return -EINVAL;

	extract_unc_hostname(unc, &name, &namelen);
	if (!namelen)
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
		rc = resolve_name(s, len - 1, ip_addr, expiry);
		kfree(s);
		if (!rc)
			return 0;
	}
	return resolve_name(name, namelen, ip_addr, expiry);
}
