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
#include <linux/keyctl.h>
#include <linux/key-type.h>
#include <keys/user-type.h>
#include "dns_resolve.h"
#include "cifsglob.h"
#include "cifsproto.h"
#include "cifs_debug.h"

static const struct cred *dns_resolver_cache;

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
	const struct cred *saved_cred;
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
		cFYI(1, "%s: unc is too short: %s", __func__, unc);
		return -EINVAL;
	}
	len -= 2;
	name = memchr(unc+2, '\\', len);
	if (!name) {
		cFYI(1, "%s: probably server name is whole unc: %s",
					__func__, unc);
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
		cFYI(1, "%s: it is IP, skipping dns upcall: %s",
					__func__, name);
		data = name;
		goto skip_upcall;
	}

	saved_cred = override_creds(dns_resolver_cache);
	rkey = request_key(&key_type_dns_resolver, name, "");
	revert_creds(saved_cred);
	if (!IS_ERR(rkey)) {
		if (!(rkey->perm & KEY_USR_VIEW)) {
			down_read(&rkey->sem);
			rkey->perm |= KEY_USR_VIEW;
			up_read(&rkey->sem);
		}
		len = rkey->type_data.x[0];
		data = rkey->payload.data;
	} else {
		cERROR(1, "%s: unable to resolve: %s", __func__, name);
		goto out;
	}

skip_upcall:
	if (data) {
		*ip_addr = kmalloc(len + 1, GFP_KERNEL);
		if (*ip_addr) {
			memcpy(*ip_addr, data, len + 1);
			if (!IS_ERR(rkey))
				cFYI(1, "%s: resolved: %s to %s", __func__,
							name,
							*ip_addr
					);
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

int __init cifs_init_dns_resolver(void)
{
	struct cred *cred;
	struct key *keyring;
	int ret;

	printk(KERN_NOTICE "Registering the %s key type\n",
	       key_type_dns_resolver.name);

	/* create an override credential set with a special thread keyring in
	 * which DNS requests are cached
	 *
	 * this is used to prevent malicious redirections from being installed
	 * with add_key().
	 */
	cred = prepare_kernel_cred(NULL);
	if (!cred)
		return -ENOMEM;

	keyring = key_alloc(&key_type_keyring, ".dns_resolver", 0, 0, cred,
			    (KEY_POS_ALL & ~KEY_POS_SETATTR) |
			    KEY_USR_VIEW | KEY_USR_READ,
			    KEY_ALLOC_NOT_IN_QUOTA);
	if (IS_ERR(keyring)) {
		ret = PTR_ERR(keyring);
		goto failed_put_cred;
	}

	ret = key_instantiate_and_link(keyring, NULL, 0, NULL, NULL);
	if (ret < 0)
		goto failed_put_key;

	ret = register_key_type(&key_type_dns_resolver);
	if (ret < 0)
		goto failed_put_key;

	/* instruct request_key() to use this special keyring as a cache for
	 * the results it looks up */
	cred->thread_keyring = keyring;
	cred->jit_keyring = KEY_REQKEY_DEFL_THREAD_KEYRING;
	dns_resolver_cache = cred;
	return 0;

failed_put_key:
	key_put(keyring);
failed_put_cred:
	put_cred(cred);
	return ret;
}

void cifs_exit_dns_resolver(void)
{
	key_revoke(dns_resolver_cache->thread_keyring);
	unregister_key_type(&key_type_dns_resolver);
	put_cred(dns_resolver_cache);
	printk(KERN_NOTICE "Unregistered %s key type\n",
	       key_type_dns_resolver.name);
}
