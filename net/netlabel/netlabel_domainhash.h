/*
 * NetLabel Domain Hash Table
 *
 * This file manages the domain hash table that NetLabel uses to determine
 * which network labeling protocol to use for a given domain.  The NetLabel
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

#ifndef _NETLABEL_DOMAINHASH_H
#define _NETLABEL_DOMAINHASH_H

#include <linux/types.h>
#include <linux/rcupdate.h>
#include <linux/list.h>

/* Domain hash table size */
/* XXX - currently this number is an uneducated guess */
#define NETLBL_DOMHSH_BITSIZE       7

/* Domain mapping definition struct */
struct netlbl_dom_map {
	char *domain;
	u32 type;
	union {
		struct cipso_v4_doi *cipsov4;
	} type_def;

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
int netlbl_domhsh_remove(const char *domain, struct netlbl_audit *audit_info);
int netlbl_domhsh_remove_default(struct netlbl_audit *audit_info);
struct netlbl_dom_map *netlbl_domhsh_getentry(const char *domain);
int netlbl_domhsh_walk(u32 *skip_bkt,
		     u32 *skip_chain,
		     int (*callback) (struct netlbl_dom_map *entry, void *arg),
		     void *cb_arg);

#endif
