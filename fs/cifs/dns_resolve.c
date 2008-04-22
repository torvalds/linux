/*
 *  fs/cifs/dns_resolve.c
 *
 *   Copyright (c) 2007 Igor Mammedov
 *   Author(s): Igor Mammedov (niallain@gmail.com)
 *              Steve French (sfrench@us.ibm.com)
 *
 *   Contains the CIFS DFS upcall routines used for hostname to
 *   IP address translation.
 *
 *   This library is free software; you can redistribute it and/or modify
 *   it under the terms of the GNU Lesser General Public License as published
 *   by the Free Software Foundation; either version 2.1 of the License, or
 *   (at your option) any later version.
 *
 *   This library is distributed in the hope that it will be useful,
 *   but WITHOUT ANY WARRANTY; without even the implied warranty of
 *   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See
 *   the GNU Lesser General Public License for more details.
 *
 *   You should have received a copy of the GNU Lesser General Public License
 *   along with this library; if not, write to the Free Software
 *   Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA 02111-1307 USA
 */

#include <keys/user-type.h>
#include "dns_resolve.h"
#include "cifsglob.h"
#include "cifsproto.h"
#include "cifs_debug.h"

static int dns_resolver_instantiate(struct key *key, const void *data,
		size_t datalen)
{
	int rc = 0;
	char *ip;

	ip = kmalloc(datalen+1, GFP_KERNEL);
	if (!ip)
		return -ENOMEM;

	memcpy(ip, data, datalen);
	ip[datalen] = '\0';

	rcu_assign_pointer(key->payload.data, ip);

	return rc;
}

struct key_type key_type_dns_resolver = {
	.name        = "dns_resolver",
	.def_datalen = sizeof(struct in_addr),
	.describe    = user_describe,
	.instantiate = dns_resolver_instantiate,
	.match       = user_match,
};


/* Resolves server name to ip address.
 * input:
 * 	unc - server UNC
 * output:
 * 	*ip_addr - pointer to server ip, caller responcible for freeing it.
 * return 0 on success
 */
int
dns_resolve_server_name_to_ip(const char *unc, char **ip_addr)
{
	int rc = -EAGAIN;
	struct key *rkey;
	char *name;
	int len;

	if (!ip_addr || !unc)
		return -EINVAL;

	/* search for server name delimiter */
	len = strlen(unc);
	if (len < 3) {
		cFYI(1, ("%s: unc is too short: %s", __func__, unc));
		return -EINVAL;
	}
	len -= 2;
	name = memchr(unc+2, '\\', len);
	if (!name) {
		cFYI(1, ("%s: probably server name is whole unc: %s",
					__func__, unc));
	} else {
		len = (name - unc) - 2/* leading // */;
	}

	name = kmalloc(len+1, GFP_KERNEL);
	if (!name) {
		rc = -ENOMEM;
		return rc;
	}
	memcpy(name, unc+2, len);
	name[len] = 0;

	rkey = request_key(&key_type_dns_resolver, name, "");
	if (!IS_ERR(rkey)) {
		len = strlen(rkey->payload.data);
		*ip_addr = kmalloc(len+1, GFP_KERNEL);
		if (*ip_addr) {
			memcpy(*ip_addr, rkey->payload.data, len);
			(*ip_addr)[len] = '\0';
			cFYI(1, ("%s: resolved: %s to %s", __func__,
					rkey->description,
					*ip_addr
				));
			rc = 0;
		} else {
			rc = -ENOMEM;
		}
		key_put(rkey);
	} else {
		cERROR(1, ("%s: unable to resolve: %s", __func__, name));
	}

	kfree(name);
	return rc;
}


