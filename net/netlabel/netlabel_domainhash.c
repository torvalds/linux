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
 * along with this program;  if not, see <http://www.gnu.org/licenses/>.
 *
 */

#include <linux/types.h>
#include <linux/rculist.h>
#include <linux/skbuff.h>
#include <linux/spinlock.h>
#include <linux/string.h>
#include <linux/audit.h>
#include <linux/slab.h>
#include <net/netlabel.h>
#include <net/cipso_ipv4.h>
#include <asm/bug.h>

#include "netlabel_mgmt.h"
#include "netlabel_addrlist.h"
#include "netlabel_domainhash.h"
#include "netlabel_user.h"

struct netlbl_domhsh_tbl {
	struct list_head *tbl;
	u32 size;
};

/* Domain hash table */
/* updates should be so rare that having one spinlock for the entire hash table
 * should be okay */
static DEFINE_SPINLOCK(netlbl_domhsh_lock);
#define netlbl_domhsh_rcu_deref(p) \
	rcu_dereference_check(p, lockdep_is_held(&netlbl_domhsh_lock))
static struct netlbl_domhsh_tbl *netlbl_domhsh;
static struct netlbl_dom_map *netlbl_domhsh_def;

/*
 * Domain Hash Table Helper Functions
 */

/**
 * netlbl_domhsh_free_entry - Frees a domain hash table entry
 * @entry: the entry's RCU field
 *
 * Description:
 * This function is designed to be used as a callback to the call_rcu()
 * function so that the memory allocated to a hash table entry can be released
 * safely.
 *
 */
static void netlbl_domhsh_free_entry(struct rcu_head *entry)
{
	struct netlbl_dom_map *ptr;
	struct netlbl_af4list *iter4;
	struct netlbl_af4list *tmp4;
#if IS_ENABLED(CONFIG_IPV6)
	struct netlbl_af6list *iter6;
	struct netlbl_af6list *tmp6;
#endif /* IPv6 */

	ptr = container_of(entry, struct netlbl_dom_map, rcu);
	if (ptr->def.type == NETLBL_NLTYPE_ADDRSELECT) {
		netlbl_af4list_foreach_safe(iter4, tmp4,
					    &ptr->def.addrsel->list4) {
			netlbl_af4list_remove_entry(iter4);
			kfree(netlbl_domhsh_addr4_entry(iter4));
		}
#if IS_ENABLED(CONFIG_IPV6)
		netlbl_af6list_foreach_safe(iter6, tmp6,
					    &ptr->def.addrsel->list6) {
			netlbl_af6list_remove_entry(iter6);
			kfree(netlbl_domhsh_addr6_entry(iter6));
		}
#endif /* IPv6 */
	}
	kfree(ptr->domain);
	kfree(ptr);
}

/**
 * netlbl_domhsh_hash - Hashing function for the domain hash table
 * @domain: the domain name to hash
 *
 * Description:
 * This is the hashing function for the domain hash table, it returns the
 * correct bucket number for the domain.  The caller is responsible for
 * ensuring that the hash table is protected with either a RCU read lock or the
 * hash table lock.
 *
 */
static u32 netlbl_domhsh_hash(const char *key)
{
	u32 iter;
	u32 val;
	u32 len;

	/* This is taken (with slight modification) from
	 * security/selinux/ss/symtab.c:symhash() */

	for (iter = 0, val = 0, len = strlen(key); iter < len; iter++)
		val = (val << 4 | (val >> (8 * sizeof(u32) - 4))) ^ key[iter];
	return val & (netlbl_domhsh_rcu_deref(netlbl_domhsh)->size - 1);
}

/**
 * netlbl_domhsh_search - Search for a domain entry
 * @domain: the domain
 *
 * Description:
 * Searches the domain hash table and returns a pointer to the hash table
 * entry if found, otherwise NULL is returned.  The caller is responsible for
 * ensuring that the hash table is protected with either a RCU read lock or the
 * hash table lock.
 *
 */
static struct netlbl_dom_map *netlbl_domhsh_search(const char *domain)
{
	u32 bkt;
	struct list_head *bkt_list;
	struct netlbl_dom_map *iter;

	if (domain != NULL) {
		bkt = netlbl_domhsh_hash(domain);
		bkt_list = &netlbl_domhsh_rcu_deref(netlbl_domhsh)->tbl[bkt];
		list_for_each_entry_rcu(iter, bkt_list, list)
			if (iter->valid && strcmp(iter->domain, domain) == 0)
				return iter;
	}

	return NULL;
}

/**
 * netlbl_domhsh_search_def - Search for a domain entry
 * @domain: the domain
 * @def: return default if no match is found
 *
 * Description:
 * Searches the domain hash table and returns a pointer to the hash table
 * entry if an exact match is found, if an exact match is not present in the
 * hash table then the default entry is returned if valid otherwise NULL is
 * returned.  The caller is responsible ensuring that the hash table is
 * protected with either a RCU read lock or the hash table lock.
 *
 */
static struct netlbl_dom_map *netlbl_domhsh_search_def(const char *domain)
{
	struct netlbl_dom_map *entry;

	entry = netlbl_domhsh_search(domain);
	if (entry == NULL) {
		entry = netlbl_domhsh_rcu_deref(netlbl_domhsh_def);
		if (entry != NULL && !entry->valid)
			entry = NULL;
	}

	return entry;
}

/**
 * netlbl_domhsh_audit_add - Generate an audit entry for an add event
 * @entry: the entry being added
 * @addr4: the IPv4 address information
 * @addr6: the IPv6 address information
 * @result: the result code
 * @audit_info: NetLabel audit information
 *
 * Description:
 * Generate an audit record for adding a new NetLabel/LSM mapping entry with
 * the given information.  Caller is responsible for holding the necessary
 * locks.
 *
 */
static void netlbl_domhsh_audit_add(struct netlbl_dom_map *entry,
				    struct netlbl_af4list *addr4,
				    struct netlbl_af6list *addr6,
				    int result,
				    struct netlbl_audit *audit_info)
{
	struct audit_buffer *audit_buf;
	struct cipso_v4_doi *cipsov4 = NULL;
	u32 type;

	audit_buf = netlbl_audit_start_common(AUDIT_MAC_MAP_ADD, audit_info);
	if (audit_buf != NULL) {
		audit_log_format(audit_buf, " nlbl_domain=%s",
				 entry->domain ? entry->domain : "(default)");
		if (addr4 != NULL) {
			struct netlbl_domaddr4_map *map4;
			map4 = netlbl_domhsh_addr4_entry(addr4);
			type = map4->def.type;
			cipsov4 = map4->def.cipso;
			netlbl_af4list_audit_addr(audit_buf, 0, NULL,
						  addr4->addr, addr4->mask);
#if IS_ENABLED(CONFIG_IPV6)
		} else if (addr6 != NULL) {
			struct netlbl_domaddr6_map *map6;
			map6 = netlbl_domhsh_addr6_entry(addr6);
			type = map6->def.type;
			netlbl_af6list_audit_addr(audit_buf, 0, NULL,
						  &addr6->addr, &addr6->mask);
#endif /* IPv6 */
		} else {
			type = entry->def.type;
			cipsov4 = entry->def.cipso;
		}
		switch (type) {
		case NETLBL_NLTYPE_UNLABELED:
			audit_log_format(audit_buf, " nlbl_protocol=unlbl");
			break;
		case NETLBL_NLTYPE_CIPSOV4:
			BUG_ON(cipsov4 == NULL);
			audit_log_format(audit_buf,
					 " nlbl_protocol=cipsov4 cipso_doi=%u",
					 cipsov4->doi);
			break;
		}
		audit_log_format(audit_buf, " res=%u", result == 0 ? 1 : 0);
		audit_log_end(audit_buf);
	}
}

/**
 * netlbl_domhsh_validate - Validate a new domain mapping entry
 * @entry: the entry to validate
 *
 * This function validates the new domain mapping entry to ensure that it is
 * a valid entry.  Returns zero on success, negative values on failure.
 *
 */
static int netlbl_domhsh_validate(const struct netlbl_dom_map *entry)
{
	struct netlbl_af4list *iter4;
	struct netlbl_domaddr4_map *map4;
#if IS_ENABLED(CONFIG_IPV6)
	struct netlbl_af6list *iter6;
	struct netlbl_domaddr6_map *map6;
#endif /* IPv6 */

	if (entry == NULL)
		return -EINVAL;

	switch (entry->def.type) {
	case NETLBL_NLTYPE_UNLABELED:
		if (entry->def.cipso != NULL || entry->def.addrsel != NULL)
			return -EINVAL;
		break;
	case NETLBL_NLTYPE_CIPSOV4:
		if (entry->def.cipso == NULL)
			return -EINVAL;
		break;
	case NETLBL_NLTYPE_ADDRSELECT:
		netlbl_af4list_foreach(iter4, &entry->def.addrsel->list4) {
			map4 = netlbl_domhsh_addr4_entry(iter4);
			switch (map4->def.type) {
			case NETLBL_NLTYPE_UNLABELED:
				if (map4->def.cipso != NULL)
					return -EINVAL;
				break;
			case NETLBL_NLTYPE_CIPSOV4:
				if (map4->def.cipso == NULL)
					return -EINVAL;
				break;
			default:
				return -EINVAL;
			}
		}
#if IS_ENABLED(CONFIG_IPV6)
		netlbl_af6list_foreach(iter6, &entry->def.addrsel->list6) {
			map6 = netlbl_domhsh_addr6_entry(iter6);
			switch (map6->def.type) {
			case NETLBL_NLTYPE_UNLABELED:
				break;
			default:
				return -EINVAL;
			}
		}
#endif /* IPv6 */
		break;
	default:
		return -EINVAL;
	}

	return 0;
}

/*
 * Domain Hash Table Functions
 */

/**
 * netlbl_domhsh_init - Init for the domain hash
 * @size: the number of bits to use for the hash buckets
 *
 * Description:
 * Initializes the domain hash table, should be called only by
 * netlbl_user_init() during initialization.  Returns zero on success, non-zero
 * values on error.
 *
 */
int __init netlbl_domhsh_init(u32 size)
{
	u32 iter;
	struct netlbl_domhsh_tbl *hsh_tbl;

	if (size == 0)
		return -EINVAL;

	hsh_tbl = kmalloc(sizeof(*hsh_tbl), GFP_KERNEL);
	if (hsh_tbl == NULL)
		return -ENOMEM;
	hsh_tbl->size = 1 << size;
	hsh_tbl->tbl = kcalloc(hsh_tbl->size,
			       sizeof(struct list_head),
			       GFP_KERNEL);
	if (hsh_tbl->tbl == NULL) {
		kfree(hsh_tbl);
		return -ENOMEM;
	}
	for (iter = 0; iter < hsh_tbl->size; iter++)
		INIT_LIST_HEAD(&hsh_tbl->tbl[iter]);

	spin_lock(&netlbl_domhsh_lock);
	rcu_assign_pointer(netlbl_domhsh, hsh_tbl);
	spin_unlock(&netlbl_domhsh_lock);

	return 0;
}

/**
 * netlbl_domhsh_add - Adds a entry to the domain hash table
 * @entry: the entry to add
 * @audit_info: NetLabel audit information
 *
 * Description:
 * Adds a new entry to the domain hash table and handles any updates to the
 * lower level protocol handler (i.e. CIPSO).  Returns zero on success,
 * negative on failure.
 *
 */
int netlbl_domhsh_add(struct netlbl_dom_map *entry,
		      struct netlbl_audit *audit_info)
{
	int ret_val = 0;
	struct netlbl_dom_map *entry_old;
	struct netlbl_af4list *iter4;
	struct netlbl_af4list *tmp4;
#if IS_ENABLED(CONFIG_IPV6)
	struct netlbl_af6list *iter6;
	struct netlbl_af6list *tmp6;
#endif /* IPv6 */

	ret_val = netlbl_domhsh_validate(entry);
	if (ret_val != 0)
		return ret_val;

	/* XXX - we can remove this RCU read lock as the spinlock protects the
	 *       entire function, but before we do we need to fixup the
	 *       netlbl_af[4,6]list RCU functions to do "the right thing" with
	 *       respect to rcu_dereference() when only a spinlock is held. */
	rcu_read_lock();
	spin_lock(&netlbl_domhsh_lock);
	if (entry->domain != NULL)
		entry_old = netlbl_domhsh_search(entry->domain);
	else
		entry_old = netlbl_domhsh_search_def(entry->domain);
	if (entry_old == NULL) {
		entry->valid = 1;

		if (entry->domain != NULL) {
			u32 bkt = netlbl_domhsh_hash(entry->domain);
			list_add_tail_rcu(&entry->list,
				    &rcu_dereference(netlbl_domhsh)->tbl[bkt]);
		} else {
			INIT_LIST_HEAD(&entry->list);
			rcu_assign_pointer(netlbl_domhsh_def, entry);
		}

		if (entry->def.type == NETLBL_NLTYPE_ADDRSELECT) {
			netlbl_af4list_foreach_rcu(iter4,
						   &entry->def.addrsel->list4)
				netlbl_domhsh_audit_add(entry, iter4, NULL,
							ret_val, audit_info);
#if IS_ENABLED(CONFIG_IPV6)
			netlbl_af6list_foreach_rcu(iter6,
						   &entry->def.addrsel->list6)
				netlbl_domhsh_audit_add(entry, NULL, iter6,
							ret_val, audit_info);
#endif /* IPv6 */
		} else
			netlbl_domhsh_audit_add(entry, NULL, NULL,
						ret_val, audit_info);
	} else if (entry_old->def.type == NETLBL_NLTYPE_ADDRSELECT &&
		   entry->def.type == NETLBL_NLTYPE_ADDRSELECT) {
		struct list_head *old_list4;
		struct list_head *old_list6;

		old_list4 = &entry_old->def.addrsel->list4;
		old_list6 = &entry_old->def.addrsel->list6;

		/* we only allow the addition of address selectors if all of
		 * the selectors do not exist in the existing domain map */
		netlbl_af4list_foreach_rcu(iter4, &entry->def.addrsel->list4)
			if (netlbl_af4list_search_exact(iter4->addr,
							iter4->mask,
							old_list4)) {
				ret_val = -EEXIST;
				goto add_return;
			}
#if IS_ENABLED(CONFIG_IPV6)
		netlbl_af6list_foreach_rcu(iter6, &entry->def.addrsel->list6)
			if (netlbl_af6list_search_exact(&iter6->addr,
							&iter6->mask,
							old_list6)) {
				ret_val = -EEXIST;
				goto add_return;
			}
#endif /* IPv6 */

		netlbl_af4list_foreach_safe(iter4, tmp4,
					    &entry->def.addrsel->list4) {
			netlbl_af4list_remove_entry(iter4);
			iter4->valid = 1;
			ret_val = netlbl_af4list_add(iter4, old_list4);
			netlbl_domhsh_audit_add(entry_old, iter4, NULL,
						ret_val, audit_info);
			if (ret_val != 0)
				goto add_return;
		}
#if IS_ENABLED(CONFIG_IPV6)
		netlbl_af6list_foreach_safe(iter6, tmp6,
					    &entry->def.addrsel->list6) {
			netlbl_af6list_remove_entry(iter6);
			iter6->valid = 1;
			ret_val = netlbl_af6list_add(iter6, old_list6);
			netlbl_domhsh_audit_add(entry_old, NULL, iter6,
						ret_val, audit_info);
			if (ret_val != 0)
				goto add_return;
		}
#endif /* IPv6 */
	} else
		ret_val = -EINVAL;

add_return:
	spin_unlock(&netlbl_domhsh_lock);
	rcu_read_unlock();
	return ret_val;
}

/**
 * netlbl_domhsh_add_default - Adds the default entry to the domain hash table
 * @entry: the entry to add
 * @audit_info: NetLabel audit information
 *
 * Description:
 * Adds a new default entry to the domain hash table and handles any updates
 * to the lower level protocol handler (i.e. CIPSO).  Returns zero on success,
 * negative on failure.
 *
 */
int netlbl_domhsh_add_default(struct netlbl_dom_map *entry,
			      struct netlbl_audit *audit_info)
{
	return netlbl_domhsh_add(entry, audit_info);
}

/**
 * netlbl_domhsh_remove_entry - Removes a given entry from the domain table
 * @entry: the entry to remove
 * @audit_info: NetLabel audit information
 *
 * Description:
 * Removes an entry from the domain hash table and handles any updates to the
 * lower level protocol handler (i.e. CIPSO).  Caller is responsible for
 * ensuring that the RCU read lock is held.  Returns zero on success, negative
 * on failure.
 *
 */
int netlbl_domhsh_remove_entry(struct netlbl_dom_map *entry,
			       struct netlbl_audit *audit_info)
{
	int ret_val = 0;
	struct audit_buffer *audit_buf;

	if (entry == NULL)
		return -ENOENT;

	spin_lock(&netlbl_domhsh_lock);
	if (entry->valid) {
		entry->valid = 0;
		if (entry != rcu_dereference(netlbl_domhsh_def))
			list_del_rcu(&entry->list);
		else
			RCU_INIT_POINTER(netlbl_domhsh_def, NULL);
	} else
		ret_val = -ENOENT;
	spin_unlock(&netlbl_domhsh_lock);

	audit_buf = netlbl_audit_start_common(AUDIT_MAC_MAP_DEL, audit_info);
	if (audit_buf != NULL) {
		audit_log_format(audit_buf,
				 " nlbl_domain=%s res=%u",
				 entry->domain ? entry->domain : "(default)",
				 ret_val == 0 ? 1 : 0);
		audit_log_end(audit_buf);
	}

	if (ret_val == 0) {
		struct netlbl_af4list *iter4;
		struct netlbl_domaddr4_map *map4;

		switch (entry->def.type) {
		case NETLBL_NLTYPE_ADDRSELECT:
			netlbl_af4list_foreach_rcu(iter4,
					     &entry->def.addrsel->list4) {
				map4 = netlbl_domhsh_addr4_entry(iter4);
				cipso_v4_doi_putdef(map4->def.cipso);
			}
			/* no need to check the IPv6 list since we currently
			 * support only unlabeled protocols for IPv6 */
			break;
		case NETLBL_NLTYPE_CIPSOV4:
			cipso_v4_doi_putdef(entry->def.cipso);
			break;
		}
		call_rcu(&entry->rcu, netlbl_domhsh_free_entry);
	}

	return ret_val;
}

/**
 * netlbl_domhsh_remove_af4 - Removes an address selector entry
 * @domain: the domain
 * @addr: IPv4 address
 * @mask: IPv4 address mask
 * @audit_info: NetLabel audit information
 *
 * Description:
 * Removes an individual address selector from a domain mapping and potentially
 * the entire mapping if it is empty.  Returns zero on success, negative values
 * on failure.
 *
 */
int netlbl_domhsh_remove_af4(const char *domain,
			     const struct in_addr *addr,
			     const struct in_addr *mask,
			     struct netlbl_audit *audit_info)
{
	struct netlbl_dom_map *entry_map;
	struct netlbl_af4list *entry_addr;
	struct netlbl_af4list *iter4;
#if IS_ENABLED(CONFIG_IPV6)
	struct netlbl_af6list *iter6;
#endif /* IPv6 */
	struct netlbl_domaddr4_map *entry;

	rcu_read_lock();

	if (domain)
		entry_map = netlbl_domhsh_search(domain);
	else
		entry_map = netlbl_domhsh_search_def(domain);
	if (entry_map == NULL ||
	    entry_map->def.type != NETLBL_NLTYPE_ADDRSELECT)
		goto remove_af4_failure;

	spin_lock(&netlbl_domhsh_lock);
	entry_addr = netlbl_af4list_remove(addr->s_addr, mask->s_addr,
					   &entry_map->def.addrsel->list4);
	spin_unlock(&netlbl_domhsh_lock);

	if (entry_addr == NULL)
		goto remove_af4_failure;
	netlbl_af4list_foreach_rcu(iter4, &entry_map->def.addrsel->list4)
		goto remove_af4_single_addr;
#if IS_ENABLED(CONFIG_IPV6)
	netlbl_af6list_foreach_rcu(iter6, &entry_map->def.addrsel->list6)
		goto remove_af4_single_addr;
#endif /* IPv6 */
	/* the domain mapping is empty so remove it from the mapping table */
	netlbl_domhsh_remove_entry(entry_map, audit_info);

remove_af4_single_addr:
	rcu_read_unlock();
	/* yick, we can't use call_rcu here because we don't have a rcu head
	 * pointer but hopefully this should be a rare case so the pause
	 * shouldn't be a problem */
	synchronize_rcu();
	entry = netlbl_domhsh_addr4_entry(entry_addr);
	cipso_v4_doi_putdef(entry->def.cipso);
	kfree(entry);
	return 0;

remove_af4_failure:
	rcu_read_unlock();
	return -ENOENT;
}

/**
 * netlbl_domhsh_remove - Removes an entry from the domain hash table
 * @domain: the domain to remove
 * @audit_info: NetLabel audit information
 *
 * Description:
 * Removes an entry from the domain hash table and handles any updates to the
 * lower level protocol handler (i.e. CIPSO).  Returns zero on success,
 * negative on failure.
 *
 */
int netlbl_domhsh_remove(const char *domain, struct netlbl_audit *audit_info)
{
	int ret_val;
	struct netlbl_dom_map *entry;

	rcu_read_lock();
	if (domain)
		entry = netlbl_domhsh_search(domain);
	else
		entry = netlbl_domhsh_search_def(domain);
	ret_val = netlbl_domhsh_remove_entry(entry, audit_info);
	rcu_read_unlock();

	return ret_val;
}

/**
 * netlbl_domhsh_remove_default - Removes the default entry from the table
 * @audit_info: NetLabel audit information
 *
 * Description:
 * Removes/resets the default entry for the domain hash table and handles any
 * updates to the lower level protocol handler (i.e. CIPSO).  Returns zero on
 * success, non-zero on failure.
 *
 */
int netlbl_domhsh_remove_default(struct netlbl_audit *audit_info)
{
	return netlbl_domhsh_remove(NULL, audit_info);
}

/**
 * netlbl_domhsh_getentry - Get an entry from the domain hash table
 * @domain: the domain name to search for
 *
 * Description:
 * Look through the domain hash table searching for an entry to match @domain,
 * return a pointer to a copy of the entry or NULL.  The caller is responsible
 * for ensuring that rcu_read_[un]lock() is called.
 *
 */
struct netlbl_dom_map *netlbl_domhsh_getentry(const char *domain)
{
	return netlbl_domhsh_search_def(domain);
}

/**
 * netlbl_domhsh_getentry_af4 - Get an entry from the domain hash table
 * @domain: the domain name to search for
 * @addr: the IP address to search for
 *
 * Description:
 * Look through the domain hash table searching for an entry to match @domain
 * and @addr, return a pointer to a copy of the entry or NULL.  The caller is
 * responsible for ensuring that rcu_read_[un]lock() is called.
 *
 */
struct netlbl_dommap_def *netlbl_domhsh_getentry_af4(const char *domain,
						     __be32 addr)
{
	struct netlbl_dom_map *dom_iter;
	struct netlbl_af4list *addr_iter;

	dom_iter = netlbl_domhsh_search_def(domain);
	if (dom_iter == NULL)
		return NULL;

	if (dom_iter->def.type != NETLBL_NLTYPE_ADDRSELECT)
		return &dom_iter->def;
	addr_iter = netlbl_af4list_search(addr, &dom_iter->def.addrsel->list4);
	if (addr_iter == NULL)
		return NULL;
	return &(netlbl_domhsh_addr4_entry(addr_iter)->def);
}

#if IS_ENABLED(CONFIG_IPV6)
/**
 * netlbl_domhsh_getentry_af6 - Get an entry from the domain hash table
 * @domain: the domain name to search for
 * @addr: the IP address to search for
 *
 * Description:
 * Look through the domain hash table searching for an entry to match @domain
 * and @addr, return a pointer to a copy of the entry or NULL.  The caller is
 * responsible for ensuring that rcu_read_[un]lock() is called.
 *
 */
struct netlbl_dommap_def *netlbl_domhsh_getentry_af6(const char *domain,
						   const struct in6_addr *addr)
{
	struct netlbl_dom_map *dom_iter;
	struct netlbl_af6list *addr_iter;

	dom_iter = netlbl_domhsh_search_def(domain);
	if (dom_iter == NULL)
		return NULL;

	if (dom_iter->def.type != NETLBL_NLTYPE_ADDRSELECT)
		return &dom_iter->def;
	addr_iter = netlbl_af6list_search(addr, &dom_iter->def.addrsel->list6);
	if (addr_iter == NULL)
		return NULL;
	return &(netlbl_domhsh_addr6_entry(addr_iter)->def);
}
#endif /* IPv6 */

/**
 * netlbl_domhsh_walk - Iterate through the domain mapping hash table
 * @skip_bkt: the number of buckets to skip at the start
 * @skip_chain: the number of entries to skip in the first iterated bucket
 * @callback: callback for each entry
 * @cb_arg: argument for the callback function
 *
 * Description:
 * Interate over the domain mapping hash table, skipping the first @skip_bkt
 * buckets and @skip_chain entries.  For each entry in the table call
 * @callback, if @callback returns a negative value stop 'walking' through the
 * table and return.  Updates the values in @skip_bkt and @skip_chain on
 * return.  Returns zero on success, negative values on failure.
 *
 */
int netlbl_domhsh_walk(u32 *skip_bkt,
		     u32 *skip_chain,
		     int (*callback) (struct netlbl_dom_map *entry, void *arg),
		     void *cb_arg)
{
	int ret_val = -ENOENT;
	u32 iter_bkt;
	struct list_head *iter_list;
	struct netlbl_dom_map *iter_entry;
	u32 chain_cnt = 0;

	rcu_read_lock();
	for (iter_bkt = *skip_bkt;
	     iter_bkt < rcu_dereference(netlbl_domhsh)->size;
	     iter_bkt++, chain_cnt = 0) {
		iter_list = &rcu_dereference(netlbl_domhsh)->tbl[iter_bkt];
		list_for_each_entry_rcu(iter_entry, iter_list, list)
			if (iter_entry->valid) {
				if (chain_cnt++ < *skip_chain)
					continue;
				ret_val = callback(iter_entry, cb_arg);
				if (ret_val < 0) {
					chain_cnt--;
					goto walk_return;
				}
			}
	}

walk_return:
	rcu_read_unlock();
	*skip_bkt = iter_bkt;
	*skip_chain = chain_cnt;
	return ret_val;
}
