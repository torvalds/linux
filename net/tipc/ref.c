/*
 * net/tipc/ref.c: TIPC object registry code
 * 
 * Copyright (c) 1991-2006, Ericsson AB
 * Copyright (c) 2004-2005, Wind River Systems
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are met:
 *
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 * 3. Neither the names of the copyright holders nor the names of its
 *    contributors may be used to endorse or promote products derived from
 *    this software without specific prior written permission.
 *
 * Alternatively, this software may be distributed under the terms of the
 * GNU General Public License ("GPL") version 2 as published by the Free
 * Software Foundation.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
 * AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT OWNER OR CONTRIBUTORS BE
 * LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
 * CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 * SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
 * INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
 * CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 * ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
 * POSSIBILITY OF SUCH DAMAGE.
 */

#include "core.h"
#include "ref.h"
#include "port.h"
#include "subscr.h"
#include "name_distr.h"
#include "name_table.h"
#include "config.h"
#include "discover.h"
#include "bearer.h"
#include "node.h"
#include "bcast.h"

/*
 * Object reference table consists of 2**N entries.
 *
 * A used entry has object ptr != 0, reference == XXXX|own index
 *				     (XXXX changes each time entry is acquired) 
 * A free entry has object ptr == 0, reference == YYYY|next free index
 *				     (YYYY is one more than last used XXXX)
 *
 * Free list is initially chained from entry (2**N)-1 to entry 1. 
 * Entry 0 is not used to allow index 0 to indicate the end of the free list.
 *
 * Note: Any accidental reference of the form XXXX|0--0 won't match entry 0
 * because entry 0's reference field has the form XXXX|1--1.
 */

struct ref_table ref_table = { 0 };

rwlock_t reftbl_lock = RW_LOCK_UNLOCKED;

/**
 * ref_table_init - create reference table for objects
 */

int ref_table_init(u32 requested_size, u32 start)
{
	struct reference *table;
	u32 sz = 1 << 4;
	u32 index_mask;
	int i;

	while (sz < requested_size) {
		sz <<= 1;
	}
	table = (struct reference *)vmalloc(sz * sizeof(struct reference));
	if (table == NULL)
		return -ENOMEM;

	write_lock_bh(&reftbl_lock);
	index_mask = sz - 1;
	for (i = sz - 1; i >= 0; i--) {
		table[i].object = 0;
		table[i].lock = SPIN_LOCK_UNLOCKED;
		table[i].data.next_plus_upper = (start & ~index_mask) + i - 1;
	}
	ref_table.entries = table;
	ref_table.index_mask = index_mask;
	ref_table.first_free = sz - 1;
	ref_table.last_free = 1;
	write_unlock_bh(&reftbl_lock);
	return TIPC_OK;
}

/**
 * ref_table_stop - destroy reference table for objects
 */

void ref_table_stop(void)
{
	if (!ref_table.entries)
		return;

	vfree(ref_table.entries);
	ref_table.entries = 0;
}

/**
 * ref_acquire - create reference to an object
 * 
 * Return a unique reference value which can be translated back to the pointer
 * 'object' at a later time.  Also, pass back a pointer to the lock protecting 
 * the object, but without locking it.
 */

u32 ref_acquire(void *object, spinlock_t **lock)
{
	struct reference *entry;
	u32 index;
	u32 index_mask;
	u32 next_plus_upper;
	u32 reference = 0;

	assert(ref_table.entries && object);

	write_lock_bh(&reftbl_lock);
	if (ref_table.first_free) {
		index = ref_table.first_free;
		entry = &(ref_table.entries[index]);
		index_mask = ref_table.index_mask;
		/* take lock in case a previous user of entry still holds it */ 
		spin_lock_bh(&entry->lock);  
		next_plus_upper = entry->data.next_plus_upper;
		ref_table.first_free = next_plus_upper & index_mask;
		reference = (next_plus_upper & ~index_mask) + index;
		entry->data.reference = reference;
		entry->object = object;
                if (lock != 0)
                        *lock = &entry->lock;
		spin_unlock_bh(&entry->lock);
	}
	write_unlock_bh(&reftbl_lock);
	return reference;
}

/**
 * ref_discard - invalidate references to an object
 * 
 * Disallow future references to an object and free up the entry for re-use.
 * Note: The entry's spin_lock may still be busy after discard
 */

void ref_discard(u32 ref)
{
	struct reference *entry;
	u32 index; 
	u32 index_mask;

	assert(ref_table.entries);
	assert(ref != 0);

	write_lock_bh(&reftbl_lock);
	index_mask = ref_table.index_mask;
	index = ref & index_mask;
	entry = &(ref_table.entries[index]);
	assert(entry->object != 0);
	assert(entry->data.reference == ref);

	/* mark entry as unused */
	entry->object = 0;
	if (ref_table.first_free == 0)
		ref_table.first_free = index;
	else
		/* next_plus_upper is always XXXX|0--0 for last free entry */
		ref_table.entries[ref_table.last_free].data.next_plus_upper 
			|= index;
	ref_table.last_free = index;

	/* increment upper bits of entry to invalidate subsequent references */
	entry->data.next_plus_upper = (ref & ~index_mask) + (index_mask + 1);
	write_unlock_bh(&reftbl_lock);
}

