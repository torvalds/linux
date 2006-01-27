/*
 * net/tipc/ref.h: Include file for TIPC object registry code
 * 
 * Copyright (c) 1991-2006, Ericsson AB
 * Copyright (c) 2005, Wind River Systems
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

#ifndef _TIPC_REF_H
#define _TIPC_REF_H

/**
 * struct reference - TIPC object reference entry
 * @object: pointer to object associated with reference entry
 * @lock: spinlock controlling access to object
 * @data: reference value associated with object (or link to next unused entry)
 */
 
struct reference {
	void *object;
	spinlock_t lock;
	union {
		u32 next_plus_upper;
		u32 reference;
	} data;
};

/**
 * struct tipc_ref_table - table of TIPC object reference entries
 * @entries: pointer to array of reference entries
 * @index_mask: bitmask for array index portion of reference values
 * @first_free: array index of first unused object reference entry
 * @last_free: array index of last unused object reference entry
 */

struct ref_table {
	struct reference *entries;
	u32 index_mask;
	u32 first_free;
	u32 last_free;
};

extern struct ref_table tipc_ref_table;

int tipc_ref_table_init(u32 requested_size, u32 start);
void tipc_ref_table_stop(void);

u32 tipc_ref_acquire(void *object, spinlock_t **lock);
void tipc_ref_discard(u32 ref);


/**
 * tipc_ref_lock - lock referenced object and return pointer to it
 */

static inline void *tipc_ref_lock(u32 ref)
{
	if (likely(tipc_ref_table.entries)) {
		struct reference *r =
			&tipc_ref_table.entries[ref & tipc_ref_table.index_mask];

		spin_lock_bh(&r->lock);
		if (likely(r->data.reference == ref))
			return r->object;
		spin_unlock_bh(&r->lock);
	}
	return 0;
}

/**
 * tipc_ref_unlock - unlock referenced object 
 */

static inline void tipc_ref_unlock(u32 ref)
{
	if (likely(tipc_ref_table.entries)) {
		struct reference *r =
			&tipc_ref_table.entries[ref & tipc_ref_table.index_mask];

		if (likely(r->data.reference == ref))
			spin_unlock_bh(&r->lock);
		else
			err("tipc_ref_unlock() invoked using obsolete reference\n");
	}
}

/**
 * tipc_ref_deref - return pointer referenced object (without locking it)
 */

static inline void *tipc_ref_deref(u32 ref)
{
	if (likely(tipc_ref_table.entries)) {
		struct reference *r = 
			&tipc_ref_table.entries[ref & tipc_ref_table.index_mask];

		if (likely(r->data.reference == ref))
			return r->object;
	}
	return 0;
}

#endif
