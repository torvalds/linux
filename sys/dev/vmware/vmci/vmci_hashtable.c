/*-
 * Copyright (c) 2018 VMware, Inc.
 *
 * SPDX-License-Identifier: (BSD-2-Clause OR GPL-2.0)
 */

/* Implementation of the VMCI Hashtable. */

#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");

#include "vmci.h"
#include "vmci_driver.h"
#include "vmci_hashtable.h"
#include "vmci_kernel_defs.h"
#include "vmci_utils.h"

#define LGPFX	"vmci_hashtable: "

#define VMCI_HASHTABLE_HASH(_h, _sz)					\
	vmci_hash_id(VMCI_HANDLE_TO_RESOURCE_ID(_h), (_sz))

static int	hashtable_unlink_entry(struct vmci_hashtable *table,
		    struct vmci_hash_entry *entry);
static bool	vmci_hashtable_entry_exists_locked(struct vmci_hashtable *table,
		    struct vmci_handle handle);

/*
 *------------------------------------------------------------------------------
 *
 * vmci_hashtable_create --
 *
 *     Creates a hashtable.
 *
 * Result:
 *     None.
 *
 * Side effects:
 *     None.
 *
 *------------------------------------------------------------------------------
 */

struct vmci_hashtable *
vmci_hashtable_create(int size)
{
	struct vmci_hashtable *table;

	table = vmci_alloc_kernel_mem(sizeof(*table),
	    VMCI_MEMORY_NORMAL);
	if (table == NULL)
		return (NULL);
	memset(table, 0, sizeof(*table));

	table->entries = vmci_alloc_kernel_mem(sizeof(*table->entries) * size,
	    VMCI_MEMORY_NORMAL);
	if (table->entries == NULL) {
		vmci_free_kernel_mem(table, sizeof(*table));
		return (NULL);
	}
	memset(table->entries, 0, sizeof(*table->entries) * size);
	table->size = size;
	if (vmci_init_lock(&table->lock, "VMCI Hashtable lock") <
	    VMCI_SUCCESS) {
		vmci_free_kernel_mem(table->entries, sizeof(*table->entries) * size);
		vmci_free_kernel_mem(table, sizeof(*table));
		return (NULL);
	}

	return (table);
}

/*
 *------------------------------------------------------------------------------
 *
 * vmci_hashtable_destroy --
 *
 *     This function should be called at module exit time. We rely on the
 *     module ref count to insure that no one is accessing any hash table
 *     entries at this point in time. Hence we should be able to just remove
 *     all entries from the hash table.
 *
 * Result:
 *     None.
 *
 * Side effects:
 *     None.
 *
 *------------------------------------------------------------------------------
 */

void
vmci_hashtable_destroy(struct vmci_hashtable *table)
{

	ASSERT(table);

	vmci_grab_lock_bh(&table->lock);
	vmci_free_kernel_mem(table->entries, sizeof(*table->entries) *
	    table->size);
	table->entries = NULL;
	vmci_release_lock_bh(&table->lock);
	vmci_cleanup_lock(&table->lock);
	vmci_free_kernel_mem(table, sizeof(*table));
}

/*
 *------------------------------------------------------------------------------
 *
 * vmci_hashtable_init_entry --
 *
 *     Initializes a hash entry.
 *
 * Result:
 *     None.
 *
 * Side effects:
 *     None.
 *
 *------------------------------------------------------------------------------
 */
void
vmci_hashtable_init_entry(struct vmci_hash_entry *entry,
    struct vmci_handle handle)
{

	ASSERT(entry);
	entry->handle = handle;
	entry->ref_count = 0;
}

/*
 *------------------------------------------------------------------------------
 *
 * vmci_hashtable_add_entry --
 *
 *     Adds an entry to the hashtable.
 *
 * Result:
 *     None.
 *
 * Side effects:
 *     None.
 *
 *------------------------------------------------------------------------------
 */

int
vmci_hashtable_add_entry(struct vmci_hashtable *table,
    struct vmci_hash_entry *entry)
{
	int idx;

	ASSERT(entry);
	ASSERT(table);

	vmci_grab_lock_bh(&table->lock);

	if (vmci_hashtable_entry_exists_locked(table, entry->handle)) {
		VMCI_LOG_DEBUG(LGPFX"Entry (handle=0x%x:0x%x) already "
		    "exists.\n", entry->handle.context,
		    entry->handle.resource);
		vmci_release_lock_bh(&table->lock);
		return (VMCI_ERROR_DUPLICATE_ENTRY);
	}

	idx = VMCI_HASHTABLE_HASH(entry->handle, table->size);
	ASSERT(idx < table->size);

	/* New entry is added to top/front of hash bucket. */
	entry->ref_count++;
	entry->next = table->entries[idx];
	table->entries[idx] = entry;
	vmci_release_lock_bh(&table->lock);

	return (VMCI_SUCCESS);
}

/*
 *------------------------------------------------------------------------------
 *
 * vmci_hashtable_remove_entry --
 *
 *     Removes an entry from the hashtable.
 *
 * Result:
 *     None.
 *
 * Side effects:
 *     None.
 *
 *------------------------------------------------------------------------------
 */

int
vmci_hashtable_remove_entry(struct vmci_hashtable *table,
    struct vmci_hash_entry *entry)
{
	int result;

	ASSERT(table);
	ASSERT(entry);

	vmci_grab_lock_bh(&table->lock);

	/* First unlink the entry. */
	result = hashtable_unlink_entry(table, entry);
	if (result != VMCI_SUCCESS) {
		/* We failed to find the entry. */
		goto done;
	}

	/* Decrement refcount and check if this is last reference. */
	entry->ref_count--;
	if (entry->ref_count == 0) {
		result = VMCI_SUCCESS_ENTRY_DEAD;
		goto done;
	}

done:
	vmci_release_lock_bh(&table->lock);

	return (result);
}

/*
 *------------------------------------------------------------------------------
 *
 * vmci_hashtable_get_entry_locked --
 *
 *     Looks up an entry in the hash table, that is already locked.
 *
 * Result:
 *     If the element is found, a pointer to the element is returned.
 *     Otherwise NULL is returned.
 *
 * Side effects:
 *     The reference count of the returned element is increased.
 *
 *------------------------------------------------------------------------------
 */

static struct vmci_hash_entry *
vmci_hashtable_get_entry_locked(struct vmci_hashtable *table,
    struct vmci_handle handle)
{
	struct vmci_hash_entry *cur = NULL;
	int idx;

	ASSERT(!VMCI_HANDLE_EQUAL(handle, VMCI_INVALID_HANDLE));
	ASSERT(table);

	idx = VMCI_HASHTABLE_HASH(handle, table->size);

	cur = table->entries[idx];
	while (true) {
		if (cur == NULL)
			break;

		if (VMCI_HANDLE_TO_RESOURCE_ID(cur->handle) ==
		    VMCI_HANDLE_TO_RESOURCE_ID(handle)) {
			if ((VMCI_HANDLE_TO_CONTEXT_ID(cur->handle) ==
			    VMCI_HANDLE_TO_CONTEXT_ID(handle)) ||
			    (VMCI_INVALID_ID == VMCI_HANDLE_TO_CONTEXT_ID(cur->handle))) {
				cur->ref_count++;
				break;
			}
		}
		cur = cur->next;
	}

	return (cur);
}

/*
 *------------------------------------------------------------------------------
 *
 * vmci_hashtable_get_entry --
 *
 *     Gets an entry from the hashtable.
 *
 * Result:
 *     None.
 *
 * Side effects:
 *     None.
 *
 *------------------------------------------------------------------------------
 */

struct vmci_hash_entry *
vmci_hashtable_get_entry(struct vmci_hashtable *table,
    struct vmci_handle handle)
{
	struct vmci_hash_entry *entry;

	if (VMCI_HANDLE_EQUAL(handle, VMCI_INVALID_HANDLE))
		return (NULL);

	ASSERT(table);

	vmci_grab_lock_bh(&table->lock);
	entry = vmci_hashtable_get_entry_locked(table, handle);
	vmci_release_lock_bh(&table->lock);

	return (entry);
}

/*
 *------------------------------------------------------------------------------
 *
 * vmci_hashtable_hold_entry --
 *
 *     Hold the given entry. This will increment the entry's reference count.
 *     This is like a GetEntry() but without having to lookup the entry by
 *     handle.
 *
 * Result:
 *     None.
 *
 * Side effects:
 *     None.
 *
 *------------------------------------------------------------------------------
 */

void
vmci_hashtable_hold_entry(struct vmci_hashtable *table,
    struct vmci_hash_entry *entry)
{

	ASSERT(table);
	ASSERT(entry);

	vmci_grab_lock_bh(&table->lock);
	entry->ref_count++;
	vmci_release_lock_bh(&table->lock);
}

/*
 *------------------------------------------------------------------------------
 *
 * vmci_hashtable_release_entry_locked --
 *
 *     Releases an element previously obtained with
 *     vmci_hashtable_get_entry_locked.
 *
 * Result:
 *     If the entry is removed from the hash table, VMCI_SUCCESS_ENTRY_DEAD
 *     is returned. Otherwise, VMCI_SUCCESS is returned.
 *
 * Side effects:
 *     The reference count of the entry is decreased and the entry is removed
 *     from the hash table on 0.
 *
 *------------------------------------------------------------------------------
 */

static int
vmci_hashtable_release_entry_locked(struct vmci_hashtable *table,
    struct vmci_hash_entry *entry)
{
	int result = VMCI_SUCCESS;

	ASSERT(table);
	ASSERT(entry);

	entry->ref_count--;
	/* Check if this is last reference and report if so. */
	if (entry->ref_count == 0) {

		/*
		 * Remove entry from hash table if not already removed. This
		 * could have happened already because VMCIHashTable_RemoveEntry
		 * was called to unlink it. We ignore if it is not found.
		 * Datagram handles will often have RemoveEntry called, whereas
		 * SharedMemory regions rely on ReleaseEntry to unlink the entry
		 * , since the creator does not call RemoveEntry when it
		 * detaches.
		 */

		hashtable_unlink_entry(table, entry);
		result = VMCI_SUCCESS_ENTRY_DEAD;
	}

	return (result);
}

/*
 *------------------------------------------------------------------------------
 *
 * vmci_hashtable_release_entry --
 *
 *     Releases an entry from the hashtable.
 *
 * Result:
 *     None.
 *
 * Side effects:
 *     None.
 *
 *------------------------------------------------------------------------------
 */

int
vmci_hashtable_release_entry(struct vmci_hashtable *table,
    struct vmci_hash_entry *entry)
{
	int result;

	ASSERT(table);
	vmci_grab_lock_bh(&table->lock);
	result = vmci_hashtable_release_entry_locked(table, entry);
	vmci_release_lock_bh(&table->lock);

	return (result);
}

/*
 *------------------------------------------------------------------------------
 *
 * vmci_hashtable_entry_exists --
 *
 *     Returns whether an entry exists in the hashtable
 *
 * Result:
 *     true if handle already in hashtable. false otherwise.
 *
 * Side effects:
 *     None.
 *
 *------------------------------------------------------------------------------
 */

bool
vmci_hashtable_entry_exists(struct vmci_hashtable *table,
    struct vmci_handle handle)
{
	bool exists;

	ASSERT(table);

	vmci_grab_lock_bh(&table->lock);
	exists = vmci_hashtable_entry_exists_locked(table, handle);
	vmci_release_lock_bh(&table->lock);

	return (exists);
}

/*
 *------------------------------------------------------------------------------
 *
 * vmci_hashtable_entry_exists_locked --
 *
 *     Unlocked version of vmci_hashtable_entry_exists.
 *
 * Result:
 *     true if handle already in hashtable. false otherwise.
 *
 * Side effects:
 *     None.
 *
 *------------------------------------------------------------------------------
 */

static bool
vmci_hashtable_entry_exists_locked(struct vmci_hashtable *table,
    struct vmci_handle handle)

{
	struct vmci_hash_entry *entry;
	int idx;

	ASSERT(table);

	idx = VMCI_HASHTABLE_HASH(handle, table->size);

	entry = table->entries[idx];
	while (entry) {
		if (VMCI_HANDLE_TO_RESOURCE_ID(entry->handle) ==
		    VMCI_HANDLE_TO_RESOURCE_ID(handle))
			if ((VMCI_HANDLE_TO_CONTEXT_ID(entry->handle) ==
			    VMCI_HANDLE_TO_CONTEXT_ID(handle)) ||
			    (VMCI_INVALID_ID == VMCI_HANDLE_TO_CONTEXT_ID(handle)) ||
			    (VMCI_INVALID_ID == VMCI_HANDLE_TO_CONTEXT_ID(entry->handle)))
				return (true);
		entry = entry->next;
	}

	return (false);
}

/*
 *------------------------------------------------------------------------------
 *
 * hashtable_unlink_entry --
 *
 *     Assumes caller holds table lock.
 *
 * Result:
 *     None.
 *
 * Side effects:
 *     None.
 *
 *------------------------------------------------------------------------------
 */

static int
hashtable_unlink_entry(struct vmci_hashtable *table,
    struct vmci_hash_entry *entry)
{
	int result;
	struct vmci_hash_entry *prev, *cur;
	int idx;

	idx = VMCI_HASHTABLE_HASH(entry->handle, table->size);

	prev = NULL;
	cur = table->entries[idx];
	while (true) {
		if (cur == NULL) {
			result = VMCI_ERROR_NOT_FOUND;
			break;
		}
		if (VMCI_HANDLE_EQUAL(cur->handle, entry->handle)) {
			ASSERT(cur == entry);

			/* Remove entry and break. */
			if (prev)
				prev->next = cur->next;
			else
				table->entries[idx] = cur->next;
			cur->next = NULL;
			result = VMCI_SUCCESS;
			break;
		}
		prev = cur;
		cur = cur->next;
	}
	return (result);
}

/*
 *------------------------------------------------------------------------------
 *
 * vmci_hashtable_sync --
 *
 *     Use this as a synchronization point when setting globals, for example,
 *     during device shutdown.
 *
 * Results:
 *     None.
 *
 * Side effects:
 *     None.
 *
 *------------------------------------------------------------------------------
 */

void
vmci_hashtable_sync(struct vmci_hashtable *table)
{

	ASSERT(table);
	vmci_grab_lock_bh(&table->lock);
	vmci_release_lock_bh(&table->lock);
}
