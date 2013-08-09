/*
 * NetLabel Domain Hash Table
 *
 * This file manages the domain hash table that NetLabel uses to determine
 * which network labeling protocol to use for a given domain.  The NetLabel
 * system manages static and dynamic label mappings for network protocols such
 * as CIPSO and RIPSO.
 *
 * Author: Paul Moore <paul@paul-moore.com>
 *
 */

/*
 * (c) Copyright Hewlett-Packard Development Company, L.P., 2006, 2008
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

#ifndef _NETLABEL_DOMAINHASH_H
#define _NETLABEL_DOMAINHASH_H

#include <linux/types.h>
#include <linux/rcupdate.h>
#include <linux/list.h>

#include "netlabel_addrlist.h"

/* Domain hash table size */
/* XXX - currently this number is an uneducated guess */
#define NETLBL_DOMHSH_BITSIZE       7

/* Domain mapping definition structures */
struct netlbl_domaddr_map {
	struct list_head list4;
	struct list_head list6;
};
struct netlbl_dommap_def {
	u32 type;
	union {
		struct netlbl_domaddr_map *addrsel;
		struct cipso_v4_doi *cipso;
	};
};
#define netlbl_domhsh_addr4_entry(iter) \
	container_of(iter, struct netlbl_domaddr4_map, list)
struct netlbl_domaddr4_map {
	struct netlbl_dommap_def def;

	struct netlbl_af4list list;
};
#define netlbl_domhsh_addr6_entry(iter) \
	container_of(iter, struct netlbl_domaddr6_map, list)
struct netlbl_domaddr6_map {
	struct netlbl_dommap_def def;

	struct netlbl_af6list list;
};

struct netlbl_dom_map {
	char *domain;
	struct netlbl_dommap_def def;

	u32 valid;
	struct list_head list;
	struct rcu_head rcu;
};

/* init function */
int netlbl_domhsh_init(u32 size);

/* Manipulate the domain hash table */
int netlbl_domhsh_add(struct netlbl_dom_map *entry,
		      struct netlbl_audit *audit_info);
int netlbl_domhsh_add_default(struct netlbl_dom_map *entry,
			      struct netlbl_audit *audit_info);
int netlbl_domhsh_remove_entry(struct netlbl_dom_map *entry,
			       struct netlbl_audit *audit_info);
int netlbl_domhsh_remove_af4(const char *domain,
			     const struct in_addr *addr,
			     const struct in_addr *mask,
			     struct netlbl_audit *audit_info);
int netlbl_domhsh_remove(const char *domain, struct netlbl_audit *audit_info);
int netlbl_domhsh_remove_default(struct netlbl_audit *audit_info);
struct netlbl_dom_map *netlbl_domhsh_getentry(const char *domain);
struct netlbl_dommap_def *netlbl_domhsh_getentry_af4(const char *domain,
						     __be32 addr);
#if IS_ENABLED(CONFIG_IPV6)
struct netlbl_dommap_def *netlbl_domhsh_getentry_af6(const char *domain,
						   const struct in6_addr *addr);
#endif /* IPv6 */

int netlbl_domhsh_walk(u32 *skip_bkt,
		     u32 *skip_chain,
		     int (*callback) (struct netlbl_dom_map *entry, void *arg),
		     void *cb_arg);

#endif
