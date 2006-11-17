/*
 * NetLabel Kernel API
 *
 * This file defines the kernel API for the NetLabel system.  The NetLabel
 * system manages static and dynamic label mappings for network protocols such
 * as CIPSO and RIPSO.
 *
 * Author: Paul Moore <paul.moore@hp.com>
 *
 */

/*
 * (c) Copyright Hewlett-Packard Development Company, L.P., 2006
 *
 * This program is free software;  you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY;  without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See
 * the GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program;  if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA 02111-1307 USA
 *
 */

#include <linux/init.h>
#include <linux/types.h>
#include <net/ip.h>
#include <net/netlabel.h>
#include <net/cipso_ipv4.h>
#include <asm/bug.h>

#include "netlabel_domainhash.h"
#include "netlabel_unlabeled.h"
#include "netlabel_user.h"

/*
 * LSM Functions
 */

/**
 * netlbl_socket_setattr - Label a socket using the correct protocol
 * @sock: the socket to label
 * @secattr: the security attributes
 *
 * Description:
 * Attach the correct label to the given socket using the security attributes
 * specified in @secattr.  This function requires exclusive access to
 * @sock->sk, which means it either needs to be in the process of being
 * created or locked via lock_sock(sock->sk).  Returns zero on success,
 * negative values on failure.
 *
 */
int netlbl_socket_setattr(const struct socket *sock,
			  const struct netlbl_lsm_secattr *secattr)
{
	int ret_val = -ENOENT;
	struct netlbl_dom_map *dom_entry;

	if ((secattr->flags & NETLBL_SECATTR_DOMAIN) == 0)
		return -ENOENT;

	rcu_read_lock();
	dom_entry = netlbl_domhsh_getentry(secattr->domain);
	if (dom_entry == NULL)
		goto socket_setattr_return;
	switch (dom_entry->type) {
	case NETLBL_NLTYPE_CIPSOV4:
		ret_val = cipso_v4_socket_setattr(sock,
						  dom_entry->type_def.cipsov4,
						  secattr);
		break;
	case NETLBL_NLTYPE_UNLABELED:
		ret_val = 0;
		break;
	default:
		ret_val = -ENOENT;
	}

socket_setattr_return:
	rcu_read_unlock();
	return ret_val;
}

/**
 * netlbl_sock_getattr - Determine the security attributes of a sock
 * @sk: the sock
 * @secattr: the security attributes
 *
 * Description:
 * Examines the given sock to see any NetLabel style labeling has been
 * applied to the sock, if so it parses the socket label and returns the
 * security attributes in @secattr.  Returns zero on success, negative values
 * on failure.
 *
 */
int netlbl_sock_getattr(struct sock *sk, struct netlbl_lsm_secattr *secattr)
{
	int ret_val;

	ret_val = cipso_v4_sock_getattr(sk, secattr);
	if (ret_val == 0)
		return 0;

	return netlbl_unlabel_getattr(secattr);
}

/**
 * netlbl_socket_getattr - Determine the security attributes of a socket
 * @sock: the socket
 * @secattr: the security attributes
 *
 * Description:
 * Examines the given socket to see any NetLabel style labeling has been
 * applied to the socket, if so it parses the socket label and returns the
 * security attributes in @secattr.  Returns zero on success, negative values
 * on failure.
 *
 */
int netlbl_socket_getattr(const struct socket *sock,
			  struct netlbl_lsm_secattr *secattr)
{
	int ret_val;

	ret_val = cipso_v4_socket_getattr(sock, secattr);
	if (ret_val == 0)
		return 0;

	return netlbl_unlabel_getattr(secattr);
}

/**
 * netlbl_skbuff_getattr - Determine the security attributes of a packet
 * @skb: the packet
 * @secattr: the security attributes
 *
 * Description:
 * Examines the given packet to see if a recognized form of packet labeling
 * is present, if so it parses the packet label and returns the security
 * attributes in @secattr.  Returns zero on success, negative values on
 * failure.
 *
 */
int netlbl_skbuff_getattr(const struct sk_buff *skb,
			  struct netlbl_lsm_secattr *secattr)
{
	int ret_val;

	ret_val = cipso_v4_skbuff_getattr(skb, secattr);
	if (ret_val == 0)
		return 0;

	return netlbl_unlabel_getattr(secattr);
}

/**
 * netlbl_skbuff_err - Handle a LSM error on a sk_buff
 * @skb: the packet
 * @error: the error code
 *
 * Description:
 * Deal with a LSM problem when handling the packet in @skb, typically this is
 * a permission denied problem (-EACCES).  The correct action is determined
 * according to the packet's labeling protocol.
 *
 */
void netlbl_skbuff_err(struct sk_buff *skb, int error)
{
	if (CIPSO_V4_OPTEXIST(skb))
		cipso_v4_error(skb, error, 0);
}

/**
 * netlbl_cache_invalidate - Invalidate all of the NetLabel protocol caches
 *
 * Description:
 * For all of the NetLabel protocols that support some form of label mapping
 * cache, invalidate the cache.  Returns zero on success, negative values on
 * error.
 *
 */
void netlbl_cache_invalidate(void)
{
	cipso_v4_cache_invalidate();
}

/**
 * netlbl_cache_add - Add an entry to a NetLabel protocol cache
 * @skb: the packet
 * @secattr: the packet's security attributes
 *
 * Description:
 * Add the LSM security attributes for the given packet to the underlying
 * NetLabel protocol's label mapping cache.  Returns zero on success, negative
 * values on error.
 *
 */
int netlbl_cache_add(const struct sk_buff *skb,
		     const struct netlbl_lsm_secattr *secattr)
{
	if ((secattr->flags & NETLBL_SECATTR_CACHE) == 0)
		return -ENOMSG;

	if (CIPSO_V4_OPTEXIST(skb))
		return cipso_v4_cache_add(skb, secattr);

	return -ENOMSG;
}

/*
 * Setup Functions
 */

/**
 * netlbl_init - Initialize NetLabel
 *
 * Description:
 * Perform the required NetLabel initialization before first use.
 *
 */
static int __init netlbl_init(void)
{
	int ret_val;

	printk(KERN_INFO "NetLabel: Initializing\n");
	printk(KERN_INFO "NetLabel:  domain hash size = %u\n",
	       (1 << NETLBL_DOMHSH_BITSIZE));
	printk(KERN_INFO "NetLabel:  protocols ="
	       " UNLABELED"
	       " CIPSOv4"
	       "\n");

	ret_val = netlbl_domhsh_init(NETLBL_DOMHSH_BITSIZE);
	if (ret_val != 0)
		goto init_failure;

	ret_val = netlbl_netlink_init();
	if (ret_val != 0)
		goto init_failure;

	ret_val = netlbl_unlabel_defconf();
	if (ret_val != 0)
		goto init_failure;
	printk(KERN_INFO "NetLabel:  unlabeled traffic allowed by default\n");

	return 0;

init_failure:
	panic("NetLabel: failed to initialize properly (%d)\n", ret_val);
}

subsys_initcall(netlbl_init);
