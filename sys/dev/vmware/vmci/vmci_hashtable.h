/*-
 * Copyright (c) 2018 VMware, Inc.
 *
 * SPDX-License-Identifier: (BSD-2-Clause OR GPL-2.0)
 *
 * $FreeBSD$
 */

/* Hash table for use in the APIs. */

#ifndef _VMCI_HASHTABLE_H_
#define _VMCI_HASHTABLE_H_

#include "vmci_defs.h"
#include "vmci_kernel_if.h"

struct vmci_hash_entry {
	struct vmci_handle	handle;
	int			ref_count;
	struct vmci_hash_entry	*next;
};

struct vmci_hashtable {
	struct vmci_hash_entry	**entries;
	/* Number of buckets in above array. */
	int			size;
	vmci_lock		lock;
};

struct	vmci_hashtable *vmci_hashtable_create(int size);
void	vmci_hashtable_destroy(struct vmci_hashtable *table);
void	vmci_hashtable_init_entry(struct vmci_hash_entry *entry,
	    struct vmci_handle handle);
int	vmci_hashtable_add_entry(struct vmci_hashtable *table,
	    struct vmci_hash_entry *entry);
int	vmci_hashtable_remove_entry(struct vmci_hashtable *table,
	    struct vmci_hash_entry *entry);
struct	vmci_hash_entry *vmci_hashtable_get_entry(struct vmci_hashtable *table,
	    struct vmci_handle handle);
void	vmci_hashtable_hold_entry(struct vmci_hashtable *table,
	    struct vmci_hash_entry *entry);
int	vmci_hashtable_release_entry(struct vmci_hashtable *table,
	    struct vmci_hash_entry *entry);
bool	vmci_hashtable_entry_exists(struct vmci_hashtable *table,
	    struct vmci_handle handle);
void	vmci_hashtable_sync(struct vmci_hashtable *table);

#endif /* !_VMCI_HASHTABLE_H_ */
