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

#include <linux/slab.h>
#include <keys/user-type.h>
#include "dns_resolve.h"
#include "cifsglob.h"
#include "cifsproto.h"
#include "cifs_debug.h"

/* Checks if supplied name is IP address
 * returns:
 * 		1 - name is IP
 * 		0 - name is not IP
 */
static int
is_ip(char *name)
{
	struct sockaddr_storage ss;

	return cifs_convert_address(name, &ss);
}

static int
dns_resolver_instantiate(struct key *key, const void *data,
		size_t datalen)
{
	int rc = 0;
	char *ip;

	ip = kmalloc(datalen + 1, GFP_KERNEL);
	if (!ip)
		return -ENOMEM;

	memcpy(ip, data, datalen);
	ip[datalen] = '\0';

	/* make sure this looks like an address */
	if (!is_ip(ip)) {
		kfree(ip);
		return -EINVAL;
	}

	key->type_data.x[0] = datalen;
	key->payload.data = ip;

	return rc;
}

static void
dns_resolver_destroy(struct key *key)
{
	kfree(key->payload.data);
}

struct key_type key_type_dns_resolver = {
	.name        = "dns_resolver",
	.def_datalen = sizeof(struct in_addr),
	.describe    = user_describe,
	.instantiate = dns_resolver_instantiate,
	.destroy     = dns_resolver_destroy,
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
	struct key *rkey = ERR_PTR(-EAGAIN);
	char *name;
	char *data = NULL;
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

	if (is_ip(name)) {
		cFYI(1, ("%s: it is IP, skipping dns upcall: %s",
					__func__, name));
		data = name;
		goto skip_upcall;
	}

	rkey = request_key(&key_type_dns_resolver, name, "");
	if (!IS_ERR(rkey)) {
		len = rkey->type_data.x[0];
		data = rkey->payload.data;
	} else {
		cERROR(1, ("%s: unable to resolve: %s", __func__, name));
		goto out;
	}

skip_upcall:
	if (data) {
		*ip_addr = kmalloc(len + 1, GFP_KERNEL);
		if (*ip_addr) {
			memcpy(*ip_addr, data, len + 1);
			if (!IS_ERR(rkey))
				cFYI(1, ("%s: resolved: %s to %s", __func__,
							name,
							*ip_addr
					));
			rc = 0;
		} else {
			rc = -ENOMEM;
		}
		if (!IS_ERR(rkey))
			key_put(rkey);
	}

out:
	kfree(name);
	return rc;
}


