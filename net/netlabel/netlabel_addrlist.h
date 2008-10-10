/*
 * NetLabel Network Address Lists
 *
 * This file contains network address list functions used to manage ordered
 * lists of network addresses for use by the NetLabel subsystem.  The NetLabel
 * system manages static and dynamic label mappings for network protocols such
 * as CIPSO and RIPSO.
 *
 * Author: Paul Moore <paul.moore@hp.com>
 *
 */

/*
 * (c) Copyright Hewlett-Packard Development Company, L.P., 2008
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

#ifndef _NETLABEL_ADDRLIST_H
#define _NETLABEL_ADDRLIST_H

#include <linux/types.h>
#include <linux/rcupdate.h>
#include <linux/list.h>
#include <linux/in6.h>

/**
 * struct netlbl_af4list - NetLabel IPv4 address list
 * @addr: IPv4 address
 * @mask: IPv4 address mask
 * @valid: valid flag
 * @list: list structure, used internally
 */
struct netlbl_af4list {
	__be32 addr;
	__be32 mask;

	u32 valid;
	struct list_head list;
};

/**
 * struct netlbl_af6list - NetLabel IPv6 address list
 * @addr: IPv6 address
 * @mask: IPv6 address mask
 * @valid: valid flag
 * @list: list structure, used internally
 */
struct netlbl_af6list {
	struct in6_addr addr;
	struct in6_addr mask;

	u32 valid;
	struct list_head list;
};

#define __af4list_entry(ptr) container_of(ptr, struct netlbl_af4list, list)

static inline struct netlbl_af4list *__af4list_valid(struct list_head *s,
						     struct list_head *h)
{
	struct list_head *i = s;
	struct netlbl_af4list *n = __af4list_entry(s);
	while (i != h && !n->valid) {
		i = i->next;
		n = __af4list_entry(i);
	}
	return n;
}

static inline struct netlbl_af4list *__af4list_valid_rcu(struct list_head *s,
							 struct list_head *h)
{
	struct list_head *i = s;
	struct netlbl_af4list *n = __af4list_entry(s);
	while (i != h && !n->valid) {
		i = rcu_dereference(i->next);
		n = __af4list_entry(i);
	}
	return n;
}

#define netlbl_af4list_foreach(iter, head)				\
	for (iter = __af4list_valid((head)->next, head);		\
	     prefetch(iter->list.next), &iter->list != (head);		\
	     iter = __af4list_valid(iter->list.next, head))

#define netlbl_af4list_foreach_rcu(iter, head)				\
	for (iter = __af4list_valid_rcu((head)->next, head);		\
	     prefetch(iter->list.next),	&iter->list != (head);		\
	     iter = __af4list_valid_rcu(iter->list.next, head))

#define netlbl_af4list_foreach_safe(iter, tmp, head)			\
	for (iter = __af4list_valid((head)->next, head),		\
		     tmp = __af4list_valid(iter->list.next, head);	\
	     &iter->list != (head);					\
	     iter = tmp, tmp = __af4list_valid(iter->list.next, head))

int netlbl_af4list_add(struct netlbl_af4list *entry,
		       struct list_head *head);
struct netlbl_af4list *netlbl_af4list_remove(__be32 addr, __be32 mask,
					     struct list_head *head);
void netlbl_af4list_remove_entry(struct netlbl_af4list *entry);
struct netlbl_af4list *netlbl_af4list_search(__be32 addr,
					     struct list_head *head);

#if defined(CONFIG_IPV6) || defined(CONFIG_IPV6_MODULE)

#define __af6list_entry(ptr) container_of(ptr, struct netlbl_af6list, list)

static inline struct netlbl_af6list *__af6list_valid(struct list_head *s,
						     struct list_head *h)
{
	struct list_head *i = s;
	struct netlbl_af6list *n = __af6list_entry(s);
	while (i != h && !n->valid) {
		i = i->next;
		n = __af6list_entry(i);
	}
	return n;
}

static inline struct netlbl_af6list *__af6list_valid_rcu(struct list_head *s,
							 struct list_head *h)
{
	struct list_head *i = s;
	struct netlbl_af6list *n = __af6list_entry(s);
	while (i != h && !n->valid) {
		i = rcu_dereference(i->next);
		n = __af6list_entry(i);
	}
	return n;
}

#define netlbl_af6list_foreach(iter, head)				\
	for (iter = __af6list_valid((head)->next, head);		\
	     prefetch(iter->list.next),	&iter->list != (head);		\
	     iter = __af6list_valid(iter->list.next, head))

#define netlbl_af6list_foreach_rcu(iter, head)				\
	for (iter = __af6list_valid_rcu((head)->next, head);		\
	     prefetch(iter->list.next),	&iter->list != (head);		\
	     iter = __af6list_valid_rcu(iter->list.next, head))

#define netlbl_af6list_foreach_safe(iter, tmp, head)			\
	for (iter = __af6list_valid((head)->next, head),		\
		     tmp = __af6list_valid(iter->list.next, head);	\
	     &iter->list != (head);					\
	     iter = tmp, tmp = __af6list_valid(iter->list.next, head))

int netlbl_af6list_add(struct netlbl_af6list *entry,
		       struct list_head *head);
struct netlbl_af6list *netlbl_af6list_remove(const struct in6_addr *addr,
					     const struct in6_addr *mask,
					     struct list_head *head);
void netlbl_af6list_remove_entry(struct netlbl_af6list *entry);
struct netlbl_af6list *netlbl_af6list_search(const struct in6_addr *addr,
					     struct list_head *head);
#endif /* IPV6 */

#endif
