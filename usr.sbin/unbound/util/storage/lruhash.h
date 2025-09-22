/*
 * util/storage/lruhash.h - hashtable, hash function, LRU keeping.
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
 * This file contains a hashtable with LRU keeping of entries.
 *
 * The hash table keeps a maximum memory size. Old entries are removed
 * to make space for new entries.
 *
 * The locking strategy is as follows:
 * 	o since (almost) every read also implies a LRU update, the
 *	  hashtable lock is a spinlock, not rwlock.
 *	o the idea is to move every thread through the hash lock quickly,
 *	  so that the next thread can access the lookup table.
 *	o User performs hash function.
 *
 * For read:
 *	o lock hashtable.
 *		o lookup hash bin.
 *		o lock hash bin.
 *			o find entry (if failed, unlock hash, unl bin, exit).
 *			o swizzle pointers for LRU update.
 *		o unlock hashtable.
 *		o lock entry (rwlock).
 *		o unlock hash bin.
 *		o work on entry.
 *	o unlock entry.
 *
 * To update an entry, gain writelock and change the entry.
 * (the entry must keep the same hashvalue, so a data update.)
 * (you cannot upgrade a readlock to a writelock, because the item may
 *  be deleted, it would cause race conditions. So instead, unlock and
 *  relookup it in the hashtable.)
 *
 * To delete an entry:
 *	o unlock the entry if you hold the lock already.
 *	o lock hashtable.
 *		o lookup hash bin.
 *		o lock hash bin.
 *			o find entry (if failed, unlock hash, unl bin, exit).
 *			o remove entry from hashtable bin overflow chain.
 *		o unlock hashtable.
 *		o lock entry (writelock).
 *		o unlock hash bin.
 *	o unlock entry (nobody else should be waiting for this lock,
 *	  since you removed it from hashtable, and you got writelock while
 *	  holding the hashbinlock so you are the only one.)
 * 	  Note you are only allowed to obtain a lock while holding hashbinlock.
 *	o delete entry.
 *
 * The above sequence is:
 *	o race free, works with read, write and delete.
 *	o but has a queue, imagine someone needing a writelock on an item.
 *	  but there are still readlocks. The writelocker waits, but holds
 *	  the hashbinlock. The next thread that comes in and needs the same
 * 	  hashbin will wait for the lock while holding the hashtable lock.
 *	  thus halting the entire system on hashtable.
 *	  This is because of the delete protection. 
 *	  Readlocks will be easier on the rwlock on entries.
 *	  While the writer is holding writelock, similar problems happen with
 *	  a reader or writer needing the same item.
 *	  the scenario requires more than three threads.
 * 	o so the queue length is 3 threads in a bad situation. The fourth is
 *	  unable to use the hashtable.
 *
 * If you need to acquire locks on multiple items from the hashtable.
 *	o you MUST release all locks on items from the hashtable before
 *	  doing the next lookup/insert/delete/whatever.
 *	o To acquire multiple items you should use a special routine that
 *	  obtains the locks on those multiple items in one go.
 */

#ifndef UTIL_STORAGE_LRUHASH_H
#define UTIL_STORAGE_LRUHASH_H
#include "util/locks.h"
struct lruhash_bin;
struct lruhash_entry;

/** default start size for hash arrays */
#define HASH_DEFAULT_STARTARRAY		1024 /* entries in array */
/** default max memory for hash arrays */
#define HASH_DEFAULT_MAXMEM		4*1024*1024 /* bytes */

/** the type of a hash value */
typedef uint32_t hashvalue_type;

/** 
 * Type of function that calculates the size of an entry.
 * Result must include the size of struct lruhash_entry. 
 * Keys that are identical must also calculate to the same size.
 * size = func(key, data).
 */
typedef size_t (*lruhash_sizefunc_type)(void*, void*);

/** type of function that compares two keys. return 0 if equal. */
typedef int (*lruhash_compfunc_type)(void*, void*);

/** old keys are deleted. 
 * The RRset type has to revoke its ID number, markdel() is used first.
 * This function is called: func(key, userarg) */
typedef void (*lruhash_delkeyfunc_type)(void*, void*);

/** old data is deleted. This function is called: func(data, userarg). */
typedef void (*lruhash_deldatafunc_type)(void*, void*);

/** mark a key as pending to be deleted (and not to be used by anyone). 
 * called: func(key) */
typedef void (*lruhash_markdelfunc_type)(void*);

/**
 * Hash table that keeps LRU list of entries.
 */
struct lruhash {
	/** lock for exclusive access, to the lookup array */
	lock_quick_type lock;
	/** the size function for entries in this table */
	lruhash_sizefunc_type sizefunc;
	/** the compare function for entries in this table. */
	lruhash_compfunc_type compfunc;
	/** how to delete keys. */
	lruhash_delkeyfunc_type delkeyfunc;
	/** how to delete data. */
	lruhash_deldatafunc_type deldatafunc;
	/** how to mark a key pending deletion */
	lruhash_markdelfunc_type markdelfunc;
	/** user argument for user functions */
	void* cb_arg;

	/** the size of the lookup array */
	size_t size;
	/** size bitmask - since size is a power of 2 */
	int size_mask;
	/** lookup array of bins */
	struct lruhash_bin* array;

	/** the lru list, start and end, noncyclical double linked list. */
	struct lruhash_entry* lru_start;
	/** lru list end item (least recently used) */
	struct lruhash_entry* lru_end;

	/** the number of entries in the hash table. */
	size_t num;
	/** the amount of space used, roughly the number of bytes in use. */
	size_t space_used;
	/** the amount of space the hash table is maximally allowed to use. */
	size_t space_max;
	/** the maximum collisions were detected during the lruhash_insert operations. */
	size_t max_collisions;
};

/**
 * A single bin with a linked list of entries in it.
 */
struct lruhash_bin {
	/** 
	 * Lock for exclusive access to the linked list
	 * This lock makes deletion of items safe in this overflow list.
	 */
	lock_quick_type lock;
	/** linked list of overflow entries */
	struct lruhash_entry* overflow_list;
};

/**
 * An entry into the hash table.
 * To change overflow_next you need to hold the bin lock.
 * To change the lru items you need to hold the hashtable lock.
 * This structure is designed as part of key struct. And key pointer helps
 * to get the surrounding structure. Data should be allocated on its own.
 */
struct lruhash_entry {
	/** 
	 * rwlock for access to the contents of the entry
	 * Note that it does _not_ cover the lru_ and overflow_ ptrs.
	 * Even with a writelock, you cannot change hash and key.
	 * You need to delete it to change hash or key.
	 */
	lock_rw_type lock;
	/** next entry in overflow chain. Covered by hashlock and binlock. */
	struct lruhash_entry* overflow_next;
	/** next entry in lru chain. covered by hashlock. */
	struct lruhash_entry* lru_next;
	/** prev entry in lru chain. covered by hashlock. */
	struct lruhash_entry* lru_prev;
	/** hash value of the key. It may not change, until entry deleted. */
	hashvalue_type hash;
	/** key */
	void* key;
	/** data */
	void* data;
};

/**
 * Create new hash table.
 * @param start_size: size of hashtable array at start, must be power of 2.
 * @param maxmem: maximum amount of memory this table is allowed to use.
 * @param sizefunc: calculates memory usage of entries.
 * @param compfunc: compares entries, 0 on equality.
 * @param delkeyfunc: deletes key.
 *   Calling both delkey and deldata will also free the struct lruhash_entry.
 *   Make it part of the key structure and delete it in delkeyfunc.
 * @param deldatafunc: deletes data. 
 * @param arg: user argument that is passed to user function calls.
 * @return: new hash table or NULL on malloc failure.
 */
struct lruhash* lruhash_create(size_t start_size, size_t maxmem,
	lruhash_sizefunc_type sizefunc, lruhash_compfunc_type compfunc,
	lruhash_delkeyfunc_type delkeyfunc,
	lruhash_deldatafunc_type deldatafunc, void* arg);

/**
 * Delete hash table. Entries are all deleted.
 * @param table: to delete.
 */
void lruhash_delete(struct lruhash* table);

/**
 * Clear hash table. Entries are all deleted, while locking them before 
 * doing so. At end the table is empty.
 * @param table: to make empty.
 */
void lruhash_clear(struct lruhash* table);

/**
 * Insert a new element into the hashtable. 
 * If key is already present data pointer in that entry is updated.
 * The space calculation function is called with the key, data.
 * If necessary the least recently used entries are deleted to make space.
 * If necessary the hash array is grown up.
 *
 * @param table: hash table.
 * @param hash: hash value. User calculates the hash.
 * @param entry: identifies the entry.
 * 	If key already present, this entry->key is deleted immediately.
 *	But entry->data is set to NULL before deletion, and put into
 * 	the existing entry. The data is then freed.
 * @param data: the data.
 * @param cb_override: if not null overrides the cb_arg for the deletefunc.
 */
void lruhash_insert(struct lruhash* table, hashvalue_type hash, 
	struct lruhash_entry* entry, void* data, void* cb_override);

/**
 * Lookup an entry in the hashtable.
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
struct lruhash_entry* lruhash_lookup(struct lruhash* table,
	hashvalue_type hash, void* key, int wr);

/**
 * Touch entry, so it becomes the most recently used in the LRU list.
 * Caller must hold hash table lock. The entry must be inserted already.
 * @param table: hash table.
 * @param entry: entry to make first in LRU.
 */
void lru_touch(struct lruhash* table, struct lruhash_entry* entry);

/**
 * Set the markdelfunction (or NULL)
 */
void lruhash_setmarkdel(struct lruhash* table, lruhash_markdelfunc_type md);

/**
 * Update the size of an element in the hashtable.
 *
 * @param table: hash table.
 * @param cb_override: if not NULL overrides the cb_arg for deletefunc.
 * @param diff_size: difference in size to the hash table storage.
 * 	This is newsize - oldsize, a positive number uses more space.
 */
void lruhash_update_space_used(struct lruhash* table, void* cb_override,
	int diff_size);

/**
 * Update the max space for the hashtable.
 *
 * @param table: hash table.
 * @param cb_override: if not NULL overrides the cb_arg for deletefunc.
 * @param max: the new max.
 */
void lruhash_update_space_max(struct lruhash* table, void* cb_override,
	size_t max);

/************************* getdns functions ************************/
/*** these are used by getdns only and not by unbound. ***/

/**
 * Demote entry, so it becomes the least recently used in the LRU list.
 * Caller must hold hash table lock. The entry must be inserted already.
 * @param table: hash table.
 * @param entry: entry to make last in LRU.
 */
void lru_demote(struct lruhash* table, struct lruhash_entry* entry);

/**
 * Insert a new element into the hashtable, or retrieve the corresponding
 * element of it exits.
 *
 * If key is already present data pointer in that entry is kept.
 * If it is not present, a new entry is created. In that case, 
 * the space calculation function is called with the key, data.
 * If necessary the least recently used entries are deleted to make space.
 * If necessary the hash array is grown up.
 *
 * @param table: hash table.
 * @param hash: hash value. User calculates the hash.
 * @param entry: identifies the entry.
 * @param data: the data.
 * @param cb_arg: if not null overrides the cb_arg for the deletefunc.
 * @return: pointer to the existing entry if the key was already present,
 *     or to the entry argument if it was not.
 */
struct lruhash_entry* lruhash_insert_or_retrieve(struct lruhash* table, hashvalue_type hash,
        struct lruhash_entry* entry, void* data, void* cb_arg);

/************************* Internal functions ************************/
/*** these are only exposed for unit tests. ***/

/**
 * Remove entry from hashtable. Does nothing if not found in hashtable.
 * Delfunc is called for the entry.
 * @param table: hash table.
 * @param hash: hash of key.
 * @param key: what to look for. 
 */
void lruhash_remove(struct lruhash* table, hashvalue_type hash, void* key);

/** init the hash bins for the table */
void bin_init(struct lruhash_bin* array, size_t size);

/** delete the hash bin and entries inside it */
void bin_delete(struct lruhash* table, struct lruhash_bin* bin);

/** 
 * Find entry in hash bin. You must have locked the bin.
 * @param table: hash table with function pointers.
 * @param bin: hash bin to look into.
 * @param hash: hash value to look for.
 * @param key: key to look for.
 * @param collisions: how many collisions were found during the search.
 * @return: the entry or NULL if not found.
 */
struct lruhash_entry* bin_find_entry(struct lruhash* table, 
	struct lruhash_bin* bin, hashvalue_type hash, void* key, size_t* collisions);

/**
 * Remove entry from bin overflow chain.
 * You must have locked the bin.
 * @param bin: hash bin to look into.
 * @param entry: entry ptr that needs removal.
 */
void bin_overflow_remove(struct lruhash_bin* bin, 
	struct lruhash_entry* entry);

/**
 * Split hash bin into two new ones. Based on increased size_mask.
 * Caller must hold hash table lock.
 * At the end the routine acquires all hashbin locks (in the old array).
 * This makes it wait for other threads to finish with the bins.
 * So the bins are ready to be deleted after this function.
 * @param table: hash table with function pointers.
 * @param newa: new increased array.
 * @param newmask: new lookup mask.
 */
void bin_split(struct lruhash* table, struct lruhash_bin* newa, 
	int newmask);

/** 
 * Try to make space available by deleting old entries.
 * Assumes that the lock on the hashtable is being held by caller.
 * Caller must not hold bin locks.
 * @param table: hash table.
 * @param list: list of entries that are to be deleted later.
 *	Entries have been removed from the hash table and writelock is held.
 */
void reclaim_space(struct lruhash* table, struct lruhash_entry** list);

/**
 * Grow the table lookup array. Becomes twice as large.
 * Caller must hold the hash table lock. Must not hold any bin locks.
 * Tries to grow, on malloc failure, nothing happened.
 * @param table: hash table.
 */
void table_grow(struct lruhash* table);

/**
 * Put entry at front of lru. entry must be unlinked from lru.
 * Caller must hold hash table lock.
 * @param table: hash table with lru head and tail.
 * @param entry: entry to make most recently used.
 */
void lru_front(struct lruhash* table, struct lruhash_entry* entry);

/**
 * Remove entry from lru list.
 * Caller must hold hash table lock.
 * @param table: hash table with lru head and tail.
 * @param entry: entry to remove from lru.
 */
void lru_remove(struct lruhash* table, struct lruhash_entry* entry);

/**
 * Output debug info to the log as to state of the hash table.
 * @param table: hash table.
 * @param id: string printed with table to identify the hash table.
 * @param extended: set to true to print statistics on overflow bin lengths.
 */
void lruhash_status(struct lruhash* table, const char* id, int extended);

/**
 * Get memory in use now by the lruhash table.
 * @param table: hash table. Will be locked before use. And unlocked after.
 * @return size in bytes.
 */
size_t lruhash_get_mem(struct lruhash* table);

/**
 * Traverse a lruhash. Call back for every element in the table.
 * @param h: hash table.  Locked before use.
 * @param wr: if true writelock is obtained on element, otherwise readlock.
 * @param func: function for every element. Do not lock or unlock elements.
 * @param arg: user argument to func.
 */
void lruhash_traverse(struct lruhash* h, int wr,
        void (*func)(struct lruhash_entry*, void*), void* arg);

#endif /* UTIL_STORAGE_LRUHASH_H */
