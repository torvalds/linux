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

#include <linux/types.h>
#include <linux/rcupdate.h>
#include <linux/list.h>
#include <linux/skbuff.h>
#include <linux/spinlock.h>
#include <linux/string.h>
#include <linux/audit.h>
#include <net/netlabel.h>
#include <net/cipso_ipv4.h>
#include <asm/bug.h>

#include "netlabel_mgmt.h"
#include "netlabel_domainhash.h"
#include "netlabel_user.h"

struct netlbl_domhsh_tbl {
	struct list_head *tbl;
	u32 size;
};

/* Domain hash table */
/* XXX - updates should be so rare that having one spinlock for the entire
 * hash table should be okay */
static DEFINE_SPINLOCK(netlbl_domhsh_lock);
static struct netlbl_domhsh_tbl *netlbl_domhsh = NULL;

/* Default domain mapping */
static DEFINE_SPINLOCK(netlbl_domhsh_def_lock);
static struct netlbl_dom_map *netlbl_domhsh_def = NULL;

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

	ptr = container_of(entry, struct netlbl_dom_map, rcu);
	kfree(ptr->domain);
	kfree(ptr);
}

/**
 * netlbl_domhsh_hash - Hashing function for the domain hash table
 * @domain: the domain name to hash
 *
 * Description:
 * This is the hashing function for the domain hash table, it returns the
 * correct bucket number for the domain.  The caller is responsibile for
 * calling the rcu_read_[un]lock() functions.
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
	return val & (rcu_dereference(netlbl_domhsh)->size - 1);
}

/**
 * netlbl_domhsh_search - Search for a domain entry
 * @domain: the domain
 * @def: return default if no match is found
 *
 * Description:
 * Searches the domain hash table and returns a pointer to the hash table
 * entry if found, otherwise NULL is returned.  If @def is non-zero and a
 * match is not found in the domain hash table the default mapping is returned
 * if it exists.  The caller is responsibile for the rcu hash table locks
 * (i.e. the caller much call rcu_read_[un]lock()).
 *
 */
static struct netlbl_dom_map *netlbl_domhsh_search(const char *domain, u32 def)
{
	u32 bkt;
	struct netlbl_dom_map *iter;

	if (domain != NULL) {
		bkt = netlbl_domhsh_hash(domain);
		list_for_each_entry_rcu(iter,
				     &rcu_dereference(netlbl_domhsh)->tbl[bkt],
				     list)
			if (iter->valid && strcmp(iter->domain, domain) == 0)
				return iter;
	}

	if (def != 0) {
		iter = rcu_dereference(netlbl_domhsh_def);
		if (iter != NULL && iter->valid)
			return iter;
	}

	return NULL;
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
int netlbl_domhsh_init(u32 size)
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
	int ret_val;
	u32 bkt;
	struct audit_buffer *audit_buf;

	switch (entry->type) {
	case NETLBL_NLTYPE_UNLABELED:
		ret_val = 0;
		break;
	case NETLBL_NLTYPE_CIPSOV4:
		ret_val = cipso_v4_doi_domhsh_add(entry->type_def.cipsov4,
						  entry->domain);
		break;
	default:
		return -EINVAL;
	}
	if (ret_val != 0)
		return ret_val;

	entry->valid = 1;
	INIT_RCU_HEAD(&entry->rcu);

	rcu_read_lock();
	if (entry->domain != NULL) {
		bkt = netlbl_domhsh_hash(entry->domain);
		spin_lock(&netlbl_domhsh_lock);
		if (netlbl_domhsh_search(entry->domain, 0) == NULL)
			list_add_tail_rcu(&entry->list,
				    &rcu_dereference(netlbl_domhsh)->tbl[bkt]);
		else
			ret_val = -EEXIST;
		spin_unlock(&netlbl_domhsh_lock);
	} else {
		INIT_LIST_HEAD(&entry->list);
		spin_lock(&netlbl_domhsh_def_lock);
		if (rcu_dereference(netlbl_domhsh_def) == NULL)
			rcu_assign_pointer(netlbl_domhsh_def, entry);
		else
			ret_val = -EEXIST;
		spin_unlock(&netlbl_domhsh_def_lock);
	}
	audit_buf = netlbl_audit_start_common(AUDIT_MAC_MAP_ADD, audit_info);
	if (audit_buf != NULL) {
		audit_log_format(audit_buf,
				 " nlbl_domain=%s",
				 entry->domain ? entry->domain : "(default)");
		switch (entry->type) {
		case NETLBL_NLTYPE_UNLABELED:
			audit_log_format(audit_buf, " nlbl_protocol=unlbl");
			break;
		case NETLBL_NLTYPE_CIPSOV4:
			audit_log_format(audit_buf,
					 " nlbl_protocol=cipsov4 cipso_doi=%u",
					 entry->type_def.cipsov4->doi);
			break;
		}
		audit_log_format(audit_buf, " res=%u", ret_val == 0 ? 1 : 0);
		audit_log_end(audit_buf);
	}
	rcu_read_unlock();

	if (ret_val != 0) {
		switch (entry->type) {
		case NETLBL_NLTYPE_CIPSOV4:
			if (cipso_v4_doi_domhsh_remove(entry->type_def.cipsov4,
						       entry->domain) != 0)
				BUG();
			break;
		}
	}

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
	int ret_val = -ENOENT;
	struct netlbl_dom_map *entry;
	struct audit_buffer *audit_buf;

	rcu_read_lock();
	entry = netlbl_domhsh_search(domain, (domain != NULL ? 0 : 1));
	if (entry == NULL)
		goto remove_return;
	switch (entry->type) {
	case NETLBL_NLTYPE_CIPSOV4:
		cipso_v4_doi_domhsh_remove(entry->type_def.cipsov4,
					   entry->domain);
		break;
	}
	if (entry != rcu_dereference(netlbl_domhsh_def)) {
		spin_lock(&netlbl_domhsh_lock);
		if (entry->valid) {
			entry->valid = 0;
			list_del_rcu(&entry->list);
			ret_val = 0;
		}
		spin_unlock(&netlbl_domhsh_lock);
	} else {
		spin_lock(&netlbl_domhsh_def_lock);
		if (entry->valid) {
			entry->valid = 0;
			rcu_assign_pointer(netlbl_domhsh_def, NULL);
			ret_val = 0;
		}
		spin_unlock(&netlbl_domhsh_def_lock);
	}

	audit_buf = netlbl_audit_start_common(AUDIT_MAC_MAP_DEL, audit_info);
	if (audit_buf != NULL) {
		audit_log_format(audit_buf,
				 " nlbl_domain=%s res=%u",
				 entry->domain ? entry->domain : "(default)",
				 ret_val == 0 ? 1 : 0);
		audit_log_end(audit_buf);
	}

remove_return:
	rcu_read_unlock();
	if (ret_val == 0)
		call_rcu(&entry->rcu, netlbl_domhsh_free_entry);
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
 * return a pointer to a copy of the entry or NULL.  The caller is responsibile
 * for ensuring that rcu_read_[un]lock() is called.
 *
 */
struct netlbl_dom_map *netlbl_domhsh_getentry(const char *domain)
{
	return netlbl_domhsh_search(domain, 1);
}

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
 * return.  Returns zero on succcess, negative values on failure.
 *
 */
int netlbl_domhsh_walk(u32 *skip_bkt,
		     u32 *skip_chain,
		     int (*callback) (struct netlbl_dom_map *entry, void *arg),
		     void *cb_arg)
{
	int ret_val = -ENOENT;
	u32 iter_bkt;
	struct netlbl_dom_map *iter_entry;
	u32 chain_cnt = 0;

	rcu_read_lock();
	for (iter_bkt = *skip_bkt;
	     iter_bkt < rcu_dereference(netlbl_domhsh)->size;
	     iter_bkt++, chain_cnt = 0) {
		list_for_each_entry_rcu(iter_entry,
				&rcu_dereference(netlbl_domhsh)->tbl[iter_bkt],
				list)
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
