/* Upcall routine, designed to work as a key type and working through
 * /sbin/request-key to contact userspace when handling DNS queries.
 *
 * See Documentation/networking/dns_resolver.txt
 *
 *   Copyright (c) 2007 Igor Mammedov
 *   Author(s): Igor Mammedov (niallain@gmail.com)
 *              Steve French (sfrench@us.ibm.com)
 *              Wang Lei (wang840925@gmail.com)
 *		David Howells (dhowells@redhat.com)
 *
 *   The upcall wrapper used to make an arbitrary DNS query.
 *
 *   This function requires the appropriate userspace tool dns.upcall to be
 *   installed and something like the following lines should be added to the
 *   /etc/request-key.conf file:
 *
 *	create dns_resolver * * /sbin/dns.upcall %k
 *
 *   For example to use this module to query AFSDB RR:
 *
 *	create dns_resolver afsdb:* * /sbin/dns.afsdb %k
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
 *   along with this library; if not, see <http://www.gnu.org/licenses/>.
 */

#include <linux/module.h>
#include <linux/slab.h>
#include <linux/dns_resolver.h>
#include <linux/err.h>
#include <keys/dns_resolver-type.h>
#include <keys/user-type.h>

#include "internal.h"

/**
 * dns_query - Query the DNS
 * @type: Query type (or NULL for straight host->IP lookup)
 * @name: Name to look up
 * @namelen: Length of name
 * @options: Request options (or NULL if no options)
 * @_result: Where to place the returned data.
 * @_expiry: Where to store the result expiry time (or NULL)
 *
 * The data will be returned in the pointer at *result, and the caller is
 * responsible for freeing it.
 *
 * The description should be of the form "[<query_type>:]<domain_name>", and
 * the options need to be appropriate for the query type requested.  If no
 * query_type is given, then the query is a straight hostname to IP address
 * lookup.
 *
 * The DNS resolution lookup is performed by upcalling to userspace by way of
 * requesting a key of type dns_resolver.
 *
 * Returns the size of the result on success, -ve error code otherwise.
 */
int dns_query(const char *type, const char *name, size_t namelen,
	      const char *options, char **_result, time_t *_expiry)
{
	struct key *rkey;
	struct user_key_payload *upayload;
	const struct cred *saved_cred;
	size_t typelen, desclen;
	char *desc, *cp;
	int ret, len;

	kenter("%s,%*.*s,%zu,%s",
	       type, (int)namelen, (int)namelen, name, namelen, options);

	if (!name || namelen == 0 || !_result)
		return -EINVAL;

	/* construct the query key description as "[<type>:]<name>" */
	typelen = 0;
	desclen = 0;
	if (type) {
		typelen = strlen(type);
		if (typelen < 1)
			return -EINVAL;
		desclen += typelen + 1;
	}

	if (!namelen)
		namelen = strnlen(name, 256);
	if (namelen < 3 || namelen > 255)
		return -EINVAL;
	desclen += namelen + 1;

	desc = kmalloc(desclen, GFP_KERNEL);
	if (!desc)
		return -ENOMEM;

	cp = desc;
	if (type) {
		memcpy(cp, type, typelen);
		cp += typelen;
		*cp++ = ':';
	}
	memcpy(cp, name, namelen);
	cp += namelen;
	*cp = '\0';

	if (!options)
		options = "";
	kdebug("call request_key(,%s,%s)", desc, options);

	/* make the upcall, using special credentials to prevent the use of
	 * add_key() to preinstall malicious redirections
	 */
	saved_cred = override_creds(dns_resolver_cache);
	rkey = request_key(&key_type_dns_resolver, desc, options);
	revert_creds(saved_cred);
	kfree(desc);
	if (IS_ERR(rkey)) {
		ret = PTR_ERR(rkey);
		goto out;
	}

	down_read(&rkey->sem);
	set_bit(KEY_FLAG_ROOT_CAN_INVAL, &rkey->flags);
	rkey->perm |= KEY_USR_VIEW;

	ret = key_validate(rkey);
	if (ret < 0)
		goto put;

	/* If the DNS server gave an error, return that to the caller */
	ret = rkey->type_data.x[0];
	if (ret)
		goto put;

	upayload = rcu_dereference_protected(rkey->payload.data,
					     lockdep_is_held(&rkey->sem));
	len = upayload->datalen;

	ret = -ENOMEM;
	*_result = kmalloc(len + 1, GFP_KERNEL);
	if (!*_result)
		goto put;

	memcpy(*_result, upayload->data, len);
	*_result[len] = '\0';

	if (_expiry)
		*_expiry = rkey->expiry;

	ret = len;
put:
	up_read(&rkey->sem);
	key_put(rkey);
out:
	kleave(" = %d", ret);
	return ret;
}
EXPORT_SYMBOL(dns_query);
