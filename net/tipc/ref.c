/*
 * net/tipc/ref.c: TIPC socket registry code
 *
 * Copyright (c) 1991-2006, 2014, Ericsson AB
 * Copyright (c) 2004-2007, Wind River Systems
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

/**
 * struct reference - TIPC socket reference entry
 * @tsk: pointer to socket associated with reference entry
 * @ref: reference value for socket (combines instance & array index info)
 */
struct reference {
	struct tipc_sock *tsk;
	u32 ref;
};

/**
 * struct tipc_ref_table - table of TIPC socket reference entries
 * @entries: pointer to array of reference entries
 * @capacity: array index of first unusable entry
 * @init_point: array index of first uninitialized entry
 * @first_free: array index of first unused socket reference entry
 * @last_free: array index of last unused socket reference entry
 * @index_mask: bitmask for array index portion of reference values
 * @start_mask: initial value for instance value portion of reference values
 */
struct ref_table {
	struct reference *entries;
	u32 capacity;
	u32 init_point;
	u32 first_free;
	u32 last_free;
	u32 index_mask;
	u32 start_mask;
};

/*
 * Socket reference table consists of 2**N entries.
 *
 * State	Socket ptr	Reference
 * -----        ----------      ---------
 * In use        non-NULL       XXXX|own index
 *				(XXXX changes each time entry is acquired)
 * Free            NULL         YYYY|next free index
 *				(YYYY is one more than last used XXXX)
 * Uninitialized   NULL         0
 *
 * Entry 0 is not used; this allows index 0 to denote the end of the free list.
 *
 * Note that a reference value of 0 does not necessarily indicate that an
 * entry is uninitialized, since the last entry in the free list could also
 * have a reference value of 0 (although this is unlikely).
 */

static struct ref_table tipc_ref_table;

static DEFINE_RWLOCK(ref_table_lock);

/**
 * tipc_ref_table_init - create reference table for sockets
 */
int tipc_ref_table_init(u32 requested_size, u32 start)
{
	struct reference *table;
	u32 actual_size;

	/* account for unused entry, then round up size to a power of 2 */

	requested_size++;
	for (actual_size = 16; actual_size < requested_size; actual_size <<= 1)
		/* do nothing */ ;

	/* allocate table & mark all entries as uninitialized */
	table = vzalloc(actual_size * sizeof(struct reference));
	if (table == NULL)
		return -ENOMEM;

	tipc_ref_table.entries = table;
	tipc_ref_table.capacity = requested_size;
	tipc_ref_table.init_point = 1;
	tipc_ref_table.first_free = 0;
	tipc_ref_table.last_free = 0;
	tipc_ref_table.index_mask = actual_size - 1;
	tipc_ref_table.start_mask = start & ~tipc_ref_table.index_mask;

	return 0;
}

/**
 * tipc_ref_table_stop - destroy reference table for sockets
 */
void tipc_ref_table_stop(void)
{
	if (!tipc_ref_table.entries)
		return;
	vfree(tipc_ref_table.entries);
	tipc_ref_table.entries = NULL;
}

/* tipc_ref_acquire - create reference to a socket
 *
 * Register an socket pointer in the reference table.
 * Returns a unique reference value that is used from then on to retrieve the
 * socket pointer, or to determine if the socket has been deregistered.
 */
u32 tipc_ref_acquire(struct tipc_sock *tsk)
{
	u32 index;
	u32 index_mask;
	u32 next_plus_upper;
	u32 ref = 0;
	struct reference *entry;

	if (unlikely(!tsk)) {
		pr_err("Attempt to acquire ref. to non-existent obj\n");
		return 0;
	}
	if (unlikely(!tipc_ref_table.entries)) {
		pr_err("Ref. table not found in acquisition attempt\n");
		return 0;
	}

	/* Take a free entry, if available; otherwise initialize a new one */
	write_lock_bh(&ref_table_lock);
	index = tipc_ref_table.first_free;
	entry = &tipc_ref_table.entries[index];

	if (likely(index)) {
		index = tipc_ref_table.first_free;
		entry = &(tipc_ref_table.entries[index]);
		index_mask = tipc_ref_table.index_mask;
		next_plus_upper = entry->ref;
		tipc_ref_table.first_free = next_plus_upper & index_mask;
		ref = (next_plus_upper & ~index_mask) + index;
		entry->tsk = tsk;
	} else if (tipc_ref_table.init_point < tipc_ref_table.capacity) {
		index = tipc_ref_table.init_point++;
		entry = &(tipc_ref_table.entries[index]);
		ref = tipc_ref_table.start_mask + index;
	}

	if (ref) {
		entry->ref = ref;
		entry->tsk = tsk;
	}
	write_unlock_bh(&ref_table_lock);
	return ref;
}

/* tipc_ref_discard - invalidate reference to an socket
 *
 * Disallow future references to an socket and free up the entry for re-use.
 */
void tipc_ref_discard(u32 ref)
{
	struct reference *entry;
	u32 index;
	u32 index_mask;

	if (unlikely(!tipc_ref_table.entries)) {
		pr_err("Ref. table not found during discard attempt\n");
		return;
	}

	index_mask = tipc_ref_table.index_mask;
	index = ref & index_mask;
	entry = &(tipc_ref_table.entries[index]);

	write_lock_bh(&ref_table_lock);

	if (unlikely(!entry->tsk)) {
		pr_err("Attempt to discard ref. to non-existent socket\n");
		goto exit;
	}
	if (unlikely(entry->ref != ref)) {
		pr_err("Attempt to discard non-existent reference\n");
		goto exit;
	}

	/*
	 * Mark entry as unused; increment instance part of entry's reference
	 * to invalidate any subsequent references
	 */
	entry->tsk = NULL;
	entry->ref = (ref & ~index_mask) + (index_mask + 1);

	/* Append entry to free entry list */
	if (unlikely(tipc_ref_table.first_free == 0))
		tipc_ref_table.first_free = index;
	else
		tipc_ref_table.entries[tipc_ref_table.last_free].ref |= index;
	tipc_ref_table.last_free = index;
exit:
	write_unlock_bh(&ref_table_lock);
}

/* tipc_sk_get - find referenced socket and return pointer to it
 */
struct tipc_sock *tipc_sk_get(u32 ref)
{
	struct reference *entry;
	struct tipc_sock *tsk;

	if (unlikely(!tipc_ref_table.entries))
		return NULL;
	read_lock_bh(&ref_table_lock);
	entry = &tipc_ref_table.entries[ref & tipc_ref_table.index_mask];
	tsk = entry->tsk;
	if (likely(tsk && (entry->ref == ref)))
		sock_hold(&tsk->sk);
	else
		tsk = NULL;
	read_unlock_bh(&ref_table_lock);
	return tsk;
}

/* tipc_sk_get_next - lock & return next socket after referenced one
*/
struct tipc_sock *tipc_sk_get_next(u32 *ref)
{
	struct reference *entry;
	struct tipc_sock *tsk = NULL;
	uint index = *ref & tipc_ref_table.index_mask;

	read_lock_bh(&ref_table_lock);
	while (++index < tipc_ref_table.capacity) {
		entry = &tipc_ref_table.entries[index];
		if (!entry->tsk)
			continue;
		tsk = entry->tsk;
		sock_hold(&tsk->sk);
		*ref = entry->ref;
		break;
	}
	read_unlock_bh(&ref_table_lock);
	return tsk;
}
