/*
 * NetLabel Unlabeled Support
 *
 * This file defines functions for dealing with unlabeled packets for the
 * NetLabel system.  The NetLabel system manages static and dynamic label
 * mappings for network protocols such as CIPSO and RIPSO.
 *
 * Author: Paul Moore <paul@paul-moore.com>
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

#ifndef _NETLABEL_UNLABELED_H
#define _NETLABEL_UNLABELED_H

#include <net/netlabel.h>

/*
 * The following NetLabel payloads are supported by the Unlabeled subsystem.
 *
 * o STATICADD
 *   This message is sent from an application to add a new static label for
 *   incoming unlabeled connections.
 *
 *   Required attributes:
 *
 *     NLBL_UNLABEL_A_IFACE
 *     NLBL_UNLABEL_A_SECCTX
 *
 *   If IPv4 is specified the following attributes are required:
 *
 *     NLBL_UNLABEL_A_IPV4ADDR
 *     NLBL_UNLABEL_A_IPV4MASK
 *
 *   If IPv6 is specified the following attributes are required:
 *
 *     NLBL_UNLABEL_A_IPV6ADDR
 *     NLBL_UNLABEL_A_IPV6MASK
 *
 * o STATICREMOVE
 *   This message is sent from an application to remove an existing static
 *   label for incoming unlabeled connections.
 *
 *   Required attributes:
 *
 *     NLBL_UNLABEL_A_IFACE
 *
 *   If IPv4 is specified the following attributes are required:
 *
 *     NLBL_UNLABEL_A_IPV4ADDR
 *     NLBL_UNLABEL_A_IPV4MASK
 *
 *   If IPv6 is specified the following attributes are required:
 *
 *     NLBL_UNLABEL_A_IPV6ADDR
 *     NLBL_UNLABEL_A_IPV6MASK
 *
 * o STATICLIST
 *   This message can be sent either from an application or by the kernel in
 *   response to an application generated STATICLIST message.  When sent by an
 *   application there is no payload and the NLM_F_DUMP flag should be set.
 *   The kernel should response with a series of the following messages.
 *
 *   Required attributes:
 *
 *     NLBL_UNLABEL_A_IFACE
 *     NLBL_UNLABEL_A_SECCTX
 *
 *   If IPv4 is specified the following attributes are required:
 *
 *     NLBL_UNLABEL_A_IPV4ADDR
 *     NLBL_UNLABEL_A_IPV4MASK
 *
 *   If IPv6 is specified the following attributes are required:
 *
 *     NLBL_UNLABEL_A_IPV6ADDR
 *     NLBL_UNLABEL_A_IPV6MASK
 *
 * o STATICADDDEF
 *   This message is sent from an application to set the default static
 *   label for incoming unlabeled connections.
 *
 *   Required attribute:
 *
 *     NLBL_UNLABEL_A_SECCTX
 *
 *   If IPv4 is specified the following attributes are required:
 *
 *     NLBL_UNLABEL_A_IPV4ADDR
 *     NLBL_UNLABEL_A_IPV4MASK
 *
 *   If IPv6 is specified the following attributes are required:
 *
 *     NLBL_UNLABEL_A_IPV6ADDR
 *     NLBL_UNLABEL_A_IPV6MASK
 *
 * o STATICREMOVEDEF
 *   This message is sent from an application to remove the existing default
 *   static label for incoming unlabeled connections.
 *
 *   If IPv4 is specified the following attributes are required:
 *
 *     NLBL_UNLABEL_A_IPV4ADDR
 *     NLBL_UNLABEL_A_IPV4MASK
 *
 *   If IPv6 is specified the following attributes are required:
 *
 *     NLBL_UNLABEL_A_IPV6ADDR
 *     NLBL_UNLABEL_A_IPV6MASK
 *
 * o STATICLISTDEF
 *   This message can be sent either from an application or by the kernel in
 *   response to an application generated STATICLISTDEF message.  When sent by
 *   an application there is no payload and the NLM_F_DUMP flag should be set.
 *   The kernel should response with the following message.
 *
 *   Required attribute:
 *
 *     NLBL_UNLABEL_A_SECCTX
 *
 *   If IPv4 is specified the following attributes are required:
 *
 *     NLBL_UNLABEL_A_IPV4ADDR
 *     NLBL_UNLABEL_A_IPV4MASK
 *
 *   If IPv6 is specified the following attributes are required:
 *
 *     NLBL_UNLABEL_A_IPV6ADDR
 *     NLBL_UNLABEL_A_IPV6MASK
 *
 * o ACCEPT
 *   This message is sent from an application to specify if the kernel should
 *   allow unlabled packets to pass if they do not match any of the static
 *   mappings defined in the unlabeled module.
 *
 *   Required attributes:
 *
 *     NLBL_UNLABEL_A_ACPTFLG
 *
 * o LIST
 *   This message can be sent either from an application or by the kernel in
 *   response to an application generated LIST message.  When sent by an
 *   application there is no payload.  The kernel should respond to a LIST
 *   message with a LIST message on success.
 *
 *   Required attributes:
 *
 *     NLBL_UNLABEL_A_ACPTFLG
 *
 */

/* NetLabel Unlabeled commands */
enum {
	NLBL_UNLABEL_C_UNSPEC,
	NLBL_UNLABEL_C_ACCEPT,
	NLBL_UNLABEL_C_LIST,
	NLBL_UNLABEL_C_STATICADD,
	NLBL_UNLABEL_C_STATICREMOVE,
	NLBL_UNLABEL_C_STATICLIST,
	NLBL_UNLABEL_C_STATICADDDEF,
	NLBL_UNLABEL_C_STATICREMOVEDEF,
	NLBL_UNLABEL_C_STATICLISTDEF,
	__NLBL_UNLABEL_C_MAX,
};

/* NetLabel Unlabeled attributes */
enum {
	NLBL_UNLABEL_A_UNSPEC,
	NLBL_UNLABEL_A_ACPTFLG,
	/* (NLA_U8)
	 * if true then unlabeled packets are allowed to pass, else unlabeled
	 * packets are rejected */
	NLBL_UNLABEL_A_IPV6ADDR,
	/* (NLA_BINARY, struct in6_addr)
	 * an IPv6 address */
	NLBL_UNLABEL_A_IPV6MASK,
	/* (NLA_BINARY, struct in6_addr)
	 * an IPv6 address mask */
	NLBL_UNLABEL_A_IPV4ADDR,
	/* (NLA_BINARY, struct in_addr)
	 * an IPv4 address */
	NLBL_UNLABEL_A_IPV4MASK,
	/* (NLA_BINARY, struct in_addr)
	 * and IPv4 address mask */
	NLBL_UNLABEL_A_IFACE,
	/* (NLA_NULL_STRING)
	 * network interface */
	NLBL_UNLABEL_A_SECCTX,
	/* (NLA_BINARY)
	 * a LSM specific security context */
	__NLBL_UNLABEL_A_MAX,
};
#define NLBL_UNLABEL_A_MAX (__NLBL_UNLABEL_A_MAX - 1)

/* NetLabel protocol functions */
int netlbl_unlabel_genl_init(void);

/* Unlabeled connection hash table size */
/* XXX - currently this number is an uneducated guess */
#define NETLBL_UNLHSH_BITSIZE       7

/* General Unlabeled init function */
int netlbl_unlabel_init(u32 size);

/* Static/Fallback label management functions */
int netlbl_unlhsh_add(struct net *net,
		      const char *dev_name,
		      const void *addr,
		      const void *mask,
		      u32 addr_len,
		      u32 secid,
		      struct netlbl_audit *audit_info);
int netlbl_unlhsh_remove(struct net *net,
			 const char *dev_name,
			 const void *addr,
			 const void *mask,
			 u32 addr_len,
			 struct netlbl_audit *audit_info);

/* Process Unlabeled incoming network packets */
int netlbl_unlabel_getattr(const struct sk_buff *skb,
			   u16 family,
			   struct netlbl_lsm_secattr *secattr);

/* Set the default configuration to allow Unlabeled packets */
int netlbl_unlabel_defconf(void);

#endif
