/*
 * util/storage/slabhash.h - hashtable consisting of several smaller tables.
 *
 * Copyright (c) 2007, NLnet Labs. All rights reserved.
 *
 * This software is open source.
 * 
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 
 * Redistributions of source code must retain the above copyright notice,
 * this list of conditions and the following disclaimer.
 * 
 * Redistributions in binary form must reproduce the above copyright notice,
 * this list of conditions and the following disclaimer in the documentation
 * and/or other materials provided with the distribution.
 * 
 * Neither the name of the NLNET LABS nor the names of its contributors may
 * be used to endorse or promote products derived from this software without
 * specific prior written permission.
 * 
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 * "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 * LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
 * A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
 * HOLDER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
 * SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED
 * TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR
 * PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF
 * LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING
 * NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS
 * SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

/**
 * \file
 *
 * Hash table that consists of smaller hash tables.
 * It cannot grow, but that gives it the ability to have multiple
 * locks. Also this means there are multiple LRU lists.
 */

#ifndef UTIL_STORAGE_SLABHASH_H
#define UTIL_STORAGE_SLABHASH_H
#include "util/storage/lruhash.h"

/** default number of slabs */
#define HASH_DEFAULT_SLABS 4

/**
 * Hash table formed from several smaller ones. 
 * This results in a partitioned lruhash table, a 'slashtable'.
 * None of the data inside the slabhash may be altered.
 * Therefore, no locks are needed to access the structure.
 */
struct slabhash {
	/** the size of the array - must be power of 2 */
	size_t size;
	/** size bitmask - uses high bits. */
	uint32_t mask;
	/** shift right this many bits to get index into array. */
	unsigned int shift;
	/** lookup array of hash tables */
	struct lruhash** array;
};

/**
 * Create new slabbed hash table.
 * @param numtables: number of hash tables to use, other parameters used to
 *	initialize these smaller hashtables.
 * @param start_size: size of hashtable array at start, must be power of 2.
 * @param maxmem: maximum amount of memory this table is allowed to use.
 *	so every table gets maxmem/numtables to use for itself.
 * @param sizefunc: calculates memory usage of entries.
 * @param compfunc: compares entries, 0 on equality.
 * @param delkeyfunc: deletes key.
 * @param deldatafunc: deletes data. 
 * @param arg: user argument that is passed to user function calls.
 * @return: new hash table or NULL on malloc failure.
 */
struct slabhash* slabhash_create(size_t numtables, size_t start_size, 
	size_t maxmem, lruhash_sizefunc_type sizefunc, 
	lruhash_compfunc_type compfunc, lruhash_delkeyfunc_type delkeyfunc, 
	lruhash_deldatafunc_type deldatafunc, void* arg);

/**
 * Delete hash table. Entries are all deleted.
 * @param table: to delete.
 */
void slabhash_delete(struct slabhash* table);

/**
 * Clear hash table. Entries are all deleted.
 * @param table: to make empty.
 */
void slabhash_clear(struct slabhash* table);

/**
 * Insert a new element into the hashtable, uses lruhash_insert. 
 * If key is already present data pointer in that entry is updated.
 *
 * @param table: hash table.
 * @param hash: hash value. User calculates the hash.
 * @param entry: identifies the entry.
 * 	If key already present, this entry->key is deleted immediately.
 *	But entry->data is set to NULL before deletion, and put into
 * 	the existing entry. The data is then freed.
 * @param data: the data.
 * @param cb_override: if not NULL overrides the cb_arg for deletefunc.
 */
void slabhash_insert(struct slabhash* table, hashvalue_type hash, 
	struct lruhash_entry* entry, void* data, void* cb_override);

/**
 * Lookup an entry in the hashtable. Uses lruhash_lookup.
 * At the end of the function you hold a (read/write)lock on the entry.
 * The LRU is updated for the entry (if found).
 * @param table: hash table.
 * @param hash: hash of key.
 * @param key: what to look for, compared against entries in overflow chain.
 *    the hash value must be set, and must work with compare function.
 * @param wr: set to true if you desire a writelock on the entry.
 *    with a writelock you can update the data part.
 * @return: pointer to the entry or NULL. The entry is locked.
 *    The user must unlock the entry when done.
 */
struct lruhash_entry* slabhash_lookup(struct slabhash* table, 
	hashvalue_type hash, void* key, int wr);

/**
 * Remove entry from hashtable. Does nothing if not found in hashtable.
 * Delfunc is called for the entry. Uses lruhash_remove.
 * @param table: hash table.
 * @param hash: hash of key.
 * @param key: what to look for. 
 */
void slabhash_remove(struct slabhash* table, hashvalue_type hash, void* key);

/**
 * Output debug info to the log as to state of the hash table.
 * @param table: hash table.
 * @param id: string printed with table to identify the hash table.
 * @param extended: set to true to print statistics on overflow bin lengths.
 */
void slabhash_status(struct slabhash* table, const char* id, int extended);

/**
 * Retrieve slab hash total size.
 * @param table: hash table.
 * @return size configured as max.
 */
size_t slabhash_get_size(struct slabhash* table);

/**
 * See if slabhash is of given (size, slabs) configuration.
 * @param table: hash table
 * @param size: max size to test for
 * @param slabs: slab count to test for.
 * @return true if equal
 */
int slabhash_is_size(struct slabhash* table, size_t size, size_t slabs);

/**
 * Update the size of an element in the hashtable, uses
 * lruhash_update_space_used.
 *
 * @param table: hash table.
 * @param hash: hash value. User calculates the hash.
 * @param cb_override: if not NULL overrides the cb_arg for deletefunc.
 * @param diff_size: difference in size to the hash table storage.
 */
void slabhash_update_space_used(struct slabhash* table, hashvalue_type hash,
	void* cb_override, int diff_size);

/**
 * Retrieve slab hash current memory use.
 * @param table: hash table.
 * @return memory in use.
 */
size_t slabhash_get_mem(struct slabhash* table);

/**
 * Get lruhash table for a given hash value
 * @param table: slabbed hash table.
 * @param hash: hash value.
 * @return the lru hash table.
 */
struct lruhash* slabhash_gettable(struct slabhash* table, hashvalue_type hash);

/**
 * Set markdel function
 * @param table: slabbed hash table.
 * @param md: markdel function ptr.
 */
void slabhash_setmarkdel(struct slabhash* table, lruhash_markdelfunc_type md);

/**
 * Traverse a slabhash.
 * @param table: slabbed hash table.
 * @param wr: if true, writelock is obtained, otherwise readlock.
 * @param func: function to call for every element.
 * @param arg: user argument to function.
 */
void slabhash_traverse(struct slabhash* table, int wr,
        void (*func)(struct lruhash_entry*, void*), void* arg);

/*
 * Count entries in slabhash.
 * @param table: slabbed hash table;
 * @return the number of items
 */
size_t count_slabhash_entries(struct slabhash* table);

/**
 * Retrieves number of items in slabhash and the current max collision level
 * @param table: slabbed hash table.
 * @param entries_count: where to save the current number of elements.
 * @param max_collisions: where to save the current max collisions level.
 */
void get_slabhash_stats(struct slabhash* table,
	long long* entries_count, long long* max_collisions);

/**
 * Adjust size of slabhash memory max
 * @param table: slabbed hash table
 * @param max: new max memory
 */
void slabhash_adjust_size(struct slabhash* table, size_t max);

/* --- test representation --- */
/** test structure contains test key */
struct slabhash_testkey {
	/** the key id */
	int id;
	/** the entry */
	struct lruhash_entry entry;
};
/** test structure contains test data */
struct slabhash_testdata {
	/** data value */
	int data;
};

/** test sizefunc for lruhash */
size_t test_slabhash_sizefunc(void*, void*);
/** test comparefunc for lruhash */
int test_slabhash_compfunc(void*, void*);
/** test delkey for lruhash */
void test_slabhash_delkey(void*, void*);
/** test deldata for lruhash */
void test_slabhash_deldata(void*, void*);
/* --- end test representation --- */

#endif /* UTIL_STORAGE_SLABHASH_H */
