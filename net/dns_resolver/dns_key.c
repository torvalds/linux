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
#include <linux/seq_file.h>
#include <keys/dns_resolver-type.h>
#include <keys/user-type.h>
#include "internal.h"

MODULE_DESCRIPTION("DNS Resolver");
MODULE_AUTHOR("Wang Lei");
MODULE_LICENSE("GPL");

unsigned int dns_resolver_debug;
module_param_named(debug, dns_resolver_debug, uint, S_IWUSR | S_IRUGO);
MODULE_PARM_DESC(debug, "DNS Resolver debugging mask");

const struct cred *dns_resolver_cache;

#define	DNS_ERRORNO_OPTION	"dnserror"

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
dns_resolver_instantiate(struct key *key, struct key_preparsed_payload *prep)
{
	struct user_key_payload *upayload;
	unsigned long derrno;
	int ret;
	size_t datalen = prep->datalen, result_len = 0;
	const char *data = prep->data, *end, *opt;

	kenter("%%%d,%s,'%*.*s',%zu",
	       key->serial, key->description,
	       (int)datalen, (int)datalen, data, datalen);

	if (datalen <= 1 || !data || data[datalen - 1] != '\0')
		return -EINVAL;
	datalen--;

	/* deal with any options embedded in the data */
	end = data + datalen;
	opt = memchr(data, '#', datalen);
	if (!opt) {
		/* no options: the entire data is the result */
		kdebug("no options");
		result_len = datalen;
	} else {
		const char *next_opt;

		result_len = opt - data;
		opt++;
		kdebug("options: '%s'", opt);
		do {
			const char *eq;
			int opt_len, opt_nlen, opt_vlen, tmp;

			next_opt = memchr(opt, '#', end - opt) ?: end;
			opt_len = next_opt - opt;
			if (!opt_len) {
				printk(KERN_WARNING
				       "Empty option to dns_resolver key %d\n",
				       key->serial);
				return -EINVAL;
			}

			eq = memchr(opt, '=', opt_len) ?: end;
			opt_nlen = eq - opt;
			eq++;
			opt_vlen = next_opt - eq; /* will be -1 if no value */

			tmp = opt_vlen >= 0 ? opt_vlen : 0;
			kdebug("option '%*.*s' val '%*.*s'",
			       opt_nlen, opt_nlen, opt, tmp, tmp, eq);

			/* see if it's an error number representing a DNS error
			 * that's to be recorded as the result in this key */
			if (opt_nlen == sizeof(DNS_ERRORNO_OPTION) - 1 &&
			    memcmp(opt, DNS_ERRORNO_OPTION, opt_nlen) == 0) {
				kdebug("dns error number option");
				if (opt_vlen <= 0)
					goto bad_option_value;

				ret = strict_strtoul(eq, 10, &derrno);
				if (ret < 0)
					goto bad_option_value;

				if (derrno < 1 || derrno > 511)
					goto bad_option_value;

				kdebug("dns error no. = %lu", derrno);
				key->type_data.x[0] = -derrno;
				continue;
			}

		bad_option_value:
			printk(KERN_WARNING
			       "Option '%*.*s' to dns_resolver key %d:"
			       " bad/missing value\n",
			       opt_nlen, opt_nlen, opt, key->serial);
			return -EINVAL;
		} while (opt = next_opt + 1, opt < end);
	}

	/* don't cache the result if we're caching an error saying there's no
	 * result */
	if (key->type_data.x[0]) {
		kleave(" = 0 [h_error %ld]", key->type_data.x[0]);
		return 0;
	}

	kdebug("store result");
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

/*
 * Describe a DNS key
 */
static void dns_resolver_describe(const struct key *key, struct seq_file *m)
{
	int err = key->type_data.x[0];

	seq_puts(m, key->description);
	if (key_is_instantiated(key)) {
		if (err)
			seq_printf(m, ": %d", err);
		else
			seq_printf(m, ": %u", key->datalen);
	}
}

/*
 * read the DNS data
 * - the key's semaphore is read-locked
 */
static long dns_resolver_read(const struct key *key,
			      char __user *buffer, size_t buflen)
{
	if (key->type_data.x[0])
		return key->type_data.x[0];

	return user_read(key, buffer, buflen);
}

struct key_type key_type_dns_resolver = {
	.name		= "dns_resolver",
	.instantiate	= dns_resolver_instantiate,
	.match		= dns_resolver_match,
	.revoke		= user_revoke,
	.destroy	= user_destroy,
	.describe	= dns_resolver_describe,
	.read		= dns_resolver_read,
};

static int __init init_dns_resolver(void)
{
	struct cred *cred;
	struct key *keyring;
	int ret;

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
	set_bit(KEY_FLAG_ROOT_CAN_CLEAR, &keyring->flags);
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
}

module_init(init_dns_resolver)
module_exit(exit_dns_resolver)
MODULE_LICENSE("GPL");
