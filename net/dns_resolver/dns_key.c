/* Key type used to cache DNS lookups made by the kernel
 *
 * See Documentation/networking/dns_resolver.txt
 *
 *   Copyright (c) 2007 Igor Mammedov
 *   Author(s): Igor Mammedov (niallain@gmail.com)
 *              Steve French (sfrench@us.ibm.com)
 *              Wang Lei (wang840925@gmail.com)
 *		David Howells (dhowells@redhat.com)
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
#include <linux/module.h>
#include <linux/moduleparam.h>
#include <linux/slab.h>
#include <linux/string.h>
#include <linux/kernel.h>
#include <linux/keyctl.h>
#include <linux/err.h>
#include <keys/dns_resolver-type.h>
#include <keys/user-type.h>
#include "internal.h"

MODULE_DESCRIPTION("DNS Resolver");
MODULE_AUTHOR("Wang Lei");
MODULE_LICENSE("GPL");

unsigned dns_resolver_debug;
module_param_named(debug, dns_resolver_debug, uint, S_IWUSR | S_IRUGO);
MODULE_PARM_DESC(debug, "DNS Resolver debugging mask");

const struct cred *dns_resolver_cache;

/*
 * Instantiate a user defined key for dns_resolver.
 *
 * The data must be a NUL-terminated string, with the NUL char accounted in
 * datalen.
 *
 * If the data contains a '#' characters, then we take the clause after each
 * one to be an option of the form 'key=value'.  The actual data of interest is
 * the string leading up to the first '#'.  For instance:
 *
 *        "ip1,ip2,...#foo=bar"
 */
static int
dns_resolver_instantiate(struct key *key, const void *_data, size_t datalen)
{
	struct user_key_payload *upayload;
	int ret;
	size_t result_len = 0;
	const char *data = _data, *opt;

	kenter("%%%d,%s,'%s',%zu",
	       key->serial, key->description, data, datalen);

	if (datalen <= 1 || !data || data[datalen - 1] != '\0')
		return -EINVAL;
	datalen--;

	/* deal with any options embedded in the data */
	opt = memchr(data, '#', datalen);
	if (!opt) {
		kdebug("no options currently supported");
		return -EINVAL;
	}

	result_len = datalen;
	ret = key_payload_reserve(key, result_len);
	if (ret < 0)
		return -EINVAL;

	upayload = kmalloc(sizeof(*upayload) + result_len + 1, GFP_KERNEL);
	if (!upayload) {
		kleave(" = -ENOMEM");
		return -ENOMEM;
	}

	upayload->datalen = result_len;
	memcpy(upayload->data, data, result_len);
	upayload->data[result_len] = '\0';
	rcu_assign_pointer(key->payload.data, upayload);

	kleave(" = 0");
	return 0;
}

/*
 * The description is of the form "[<type>:]<domain_name>"
 *
 * The domain name may be a simple name or an absolute domain name (which
 * should end with a period).  The domain name is case-independent.
 */
static int
dns_resolver_match(const struct key *key, const void *description)
{
	int slen, dlen, ret = 0;
	const char *src = key->description, *dsp = description;

	kenter("%s,%s", src, dsp);

	if (!src || !dsp)
		goto no_match;

	if (strcasecmp(src, dsp) == 0)
		goto matched;

	slen = strlen(src);
	dlen = strlen(dsp);
	if (slen <= 0 || dlen <= 0)
		goto no_match;
	if (src[slen - 1] == '.')
		slen--;
	if (dsp[dlen - 1] == '.')
		dlen--;
	if (slen != dlen || strncasecmp(src, dsp, slen) != 0)
		goto no_match;

matched:
	ret = 1;
no_match:
	kleave(" = %d", ret);
	return ret;
}

struct key_type key_type_dns_resolver = {
	.name		= "dns_resolver",
	.instantiate	= dns_resolver_instantiate,
	.match		= dns_resolver_match,
	.revoke		= user_revoke,
	.destroy	= user_destroy,
	.describe	= user_describe,
	.read		= user_read,
};

static int __init init_dns_resolver(void)
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

	kdebug("DNS resolver keyring: %d\n", key_serial(keyring));
	return 0;

failed_put_key:
	key_put(keyring);
failed_put_cred:
	put_cred(cred);
	return ret;
}

static void __exit exit_dns_resolver(void)
{
	key_revoke(dns_resolver_cache->thread_keyring);
	unregister_key_type(&key_type_dns_resolver);
	put_cred(dns_resolver_cache);
	printk(KERN_NOTICE "Unregistered %s key type\n",
	       key_type_dns_resolver.name);
}

module_init(init_dns_resolver)
module_exit(exit_dns_resolver)
MODULE_LICENSE("GPL");
