/*
 * CALIPSO - Common Architecture Label IPv6 Security Option
 *
 * This is an implementation of the CALIPSO protocol as specified in
 * RFC 5570.
 *
 * Authors: Paul Moore <paul.moore@hp.com>
 *          Huw Davies <huw@codeweavers.com>
 *
 */

/* (c) Copyright Hewlett-Packard Development Company, L.P., 2006, 2008
 * (c) Copyright Huw Davies <huw@codeweavers.com>, 2015
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

#include <linux/init.h>
#include <linux/types.h>
#include <linux/rcupdate.h>
#include <linux/list.h>
#include <linux/spinlock.h>
#include <linux/string.h>
#include <linux/jhash.h>
#include <linux/audit.h>
#include <linux/slab.h>
#include <net/ip.h>
#include <net/icmp.h>
#include <net/tcp.h>
#include <net/netlabel.h>
#include <net/calipso.h>
#include <linux/atomic.h>
#include <linux/bug.h>
#include <asm/unaligned.h>

/* List of available DOI definitions */
static DEFINE_SPINLOCK(calipso_doi_list_lock);
static LIST_HEAD(calipso_doi_list);

/* DOI List Functions
 */

/**
 * calipso_doi_search - Searches for a DOI definition
 * @doi: the DOI to search for
 *
 * Description:
 * Search the DOI definition list for a DOI definition with a DOI value that
 * matches @doi.  The caller is responsible for calling rcu_read_[un]lock().
 * Returns a pointer to the DOI definition on success and NULL on failure.
 */
static struct calipso_doi *calipso_doi_search(u32 doi)
{
	struct calipso_doi *iter;

	list_for_each_entry_rcu(iter, &calipso_doi_list, list)
		if (iter->doi == doi && atomic_read(&iter->refcount))
			return iter;
	return NULL;
}

/**
 * calipso_doi_add - Add a new DOI to the CALIPSO protocol engine
 * @doi_def: the DOI structure
 * @audit_info: NetLabel audit information
 *
 * Description:
 * The caller defines a new DOI for use by the CALIPSO engine and calls this
 * function to add it to the list of acceptable domains.  The caller must
 * ensure that the mapping table specified in @doi_def->map meets all of the
 * requirements of the mapping type (see calipso.h for details).  Returns
 * zero on success and non-zero on failure.
 *
 */
static int calipso_doi_add(struct calipso_doi *doi_def,
			   struct netlbl_audit *audit_info)
{
	int ret_val = -EINVAL;
	u32 doi;
	u32 doi_type;
	struct audit_buffer *audit_buf;

	doi = doi_def->doi;
	doi_type = doi_def->type;

	if (doi_def->doi == CALIPSO_DOI_UNKNOWN)
		goto doi_add_return;

	atomic_set(&doi_def->refcount, 1);

	spin_lock(&calipso_doi_list_lock);
	if (calipso_doi_search(doi_def->doi)) {
		spin_unlock(&calipso_doi_list_lock);
		ret_val = -EEXIST;
		goto doi_add_return;
	}
	list_add_tail_rcu(&doi_def->list, &calipso_doi_list);
	spin_unlock(&calipso_doi_list_lock);
	ret_val = 0;

doi_add_return:
	audit_buf = netlbl_audit_start(AUDIT_MAC_CALIPSO_ADD, audit_info);
	if (audit_buf) {
		const char *type_str;

		switch (doi_type) {
		case CALIPSO_MAP_PASS:
			type_str = "pass";
			break;
		default:
			type_str = "(unknown)";
		}
		audit_log_format(audit_buf,
				 " calipso_doi=%u calipso_type=%s res=%u",
				 doi, type_str, ret_val == 0 ? 1 : 0);
		audit_log_end(audit_buf);
	}

	return ret_val;
}

/**
 * calipso_doi_free - Frees a DOI definition
 * @doi_def: the DOI definition
 *
 * Description:
 * This function frees all of the memory associated with a DOI definition.
 *
 */
static void calipso_doi_free(struct calipso_doi *doi_def)
{
	kfree(doi_def);
}

static const struct netlbl_calipso_ops ops = {
	.doi_add          = calipso_doi_add,
	.doi_free         = calipso_doi_free,
};

/**
 * calipso_init - Initialize the CALIPSO module
 *
 * Description:
 * Initialize the CALIPSO module and prepare it for use.  Returns zero on
 * success and negative values on failure.
 *
 */
int __init calipso_init(void)
{
	netlbl_calipso_ops_register(&ops);
	return 0;
}

void calipso_exit(void)
{
	netlbl_calipso_ops_register(NULL);
}
