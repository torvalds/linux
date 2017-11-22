/*
 * CDDL HEADER START
 *
 * The contents of this file are subject to the terms of the
 * Common Development and Distribution License (the "License").
 * You may not use this file except in compliance with the License.
 *
 * You can obtain a copy of the license at usr/src/OPENSOLARIS.LICENSE
 * or http://www.opensolaris.org/os/licensing.
 * See the License for the specific language governing permissions
 * and limitations under the License.
 *
 * When distributing Covered Code, include this CDDL HEADER in each
 * file and include the License file at usr/src/OPENSOLARIS.LICENSE.
 * If applicable, add the following below this CDDL HEADER, with the
 * fields enclosed by brackets "[]" replaced with your own identifying
 * information: Portions Copyright [yyyy] [name of copyright owner]
 *
 * CDDL HEADER END
 */
/*
 * Copyright 2008 Sun Microsystems, Inc.  All rights reserved.
 * Use is subject to license terms.
 */

/*
 * mod_hash: flexible hash table implementation.
 *
 * This is a reasonably fast, reasonably flexible hash table implementation
 * which features pluggable hash algorithms to support storing arbitrary keys
 * and values.  It is designed to handle small (< 100,000 items) amounts of
 * data.  The hash uses chaining to resolve collisions, and does not feature a
 * mechanism to grow the hash.  Care must be taken to pick nchains to be large
 * enough for the application at hand, or lots of time will be wasted searching
 * hash chains.
 *
 * The client of the hash is required to supply a number of items to support
 * the various hash functions:
 *
 * 	- Destructor functions for the key and value being hashed.
 *	  A destructor is responsible for freeing an object when the hash
 *	  table is no longer storing it.  Since keys and values can be of
 *	  arbitrary type, separate destructors for keys & values are used.
 *	  These may be mod_hash_null_keydtor and mod_hash_null_valdtor if no
 *	  destructor is needed for either a key or value.
 *
 *	- A hashing algorithm which returns a uint_t representing a hash index
 *	  The number returned need _not_ be between 0 and nchains.  The mod_hash
 *	  code will take care of doing that.  The second argument (after the
 *	  key) to the hashing function is a void * that represents
 *	  hash_alg_data-- this is provided so that the hashing algrorithm can
 *	  maintain some state across calls, or keep algorithm-specific
 *	  constants associated with the hash table.
 *
 *	  A pointer-hashing and a string-hashing algorithm are supplied in
 *	  this file.
 *
 *	- A key comparator (a la qsort).
 *	  This is used when searching the hash chain.  The key comparator
 *	  determines if two keys match.  It should follow the return value
 *	  semantics of strcmp.
 *
 *	  string and pointer comparators are supplied in this file.
 *
 * mod_hash_create_strhash() and mod_hash_create_ptrhash() provide good
 * examples of how to create a customized hash table.
 *
 * Basic hash operations:
 *
 *   mod_hash_create_strhash(name, nchains, dtor),
 *	create a hash using strings as keys.
 *	NOTE: This create a hash which automatically cleans up the string
 *	      values it is given for keys.
 *
 *   mod_hash_create_ptrhash(name, nchains, dtor, key_elem_size):
 *	create a hash using pointers as keys.
 *
 *   mod_hash_create_extended(name, nchains, kdtor, vdtor,
 *			      hash_alg, hash_alg_data,
 *			      keycmp, sleep)
 *	create a customized hash table.
 *
 *   mod_hash_destroy_hash(hash):
 *	destroy the given hash table, calling the key and value destructors
 *	on each key-value pair stored in the hash.
 *
 *   mod_hash_insert(hash, key, val):
 *	place a key, value pair into the given hash.
 *	duplicate keys are rejected.
 *
 *   mod_hash_insert_reserve(hash, key, val, handle):
 *	place a key, value pair into the given hash, using handle to indicate
 *	the reserved storage for the pair.  (no memory allocation is needed
 *	during a mod_hash_insert_reserve.)  duplicate keys are rejected.
 *
 *   mod_hash_reserve(hash, *handle):
 *      reserve storage for a key-value pair using the memory allocation
 *      policy of 'hash', returning the storage handle in 'handle'.
 *
 *   mod_hash_reserve_nosleep(hash, *handle): reserve storage for a key-value
 *	pair ignoring the memory allocation policy of 'hash' and always without
 *	sleep, returning the storage handle in 'handle'.
 *
 *   mod_hash_remove(hash, key, *val):
 *	remove a key-value pair with key 'key' from 'hash', destroying the
 *	stored key, and returning the value in val.
 *
 *   mod_hash_replace(hash, key, val)
 * 	atomically remove an existing key-value pair from a hash, and replace
 * 	the key and value with the ones supplied.  The removed key and value
 * 	(if any) are destroyed.
 *
 *   mod_hash_destroy(hash, key):
 *	remove a key-value pair with key 'key' from 'hash', destroying both
 *	stored key and stored value.
 *
 *   mod_hash_find(hash, key, val):
 *	find a value in the hash table corresponding to the given key.
 *
 *   mod_hash_find_cb(hash, key, val, found_callback)
 *	find a value in the hash table corresponding to the given key.
 *	If a value is found, call specified callback passing key and val to it.
 *      The callback is called with the hash lock held.
 *	It is intended to be used in situations where the act of locating the
 *	data must also modify it - such as in reference counting schemes.
 *
 *   mod_hash_walk(hash, callback(key, elem, arg), arg)
 * 	walks all the elements in the hashtable and invokes the callback
 * 	function with the key/value pair for each element.  the hashtable
 * 	is locked for readers so the callback function should not attempt
 * 	to do any updates to the hashable.  the callback function should
 * 	return MH_WALK_CONTINUE to continue walking the hashtable or
 * 	MH_WALK_TERMINATE to abort the walk of the hashtable.
 *
 *   mod_hash_clear(hash):
 *	clears the given hash table of entries, calling the key and value
 *	destructors for every element in the hash.
 */

#include <sys/zfs_context.h>
#include <sys/bitmap.h>
#include <sys/modhash_impl.h>
#include <sys/sysmacros.h>

/*
 * MH_KEY_DESTROY()
 * 	Invoke the key destructor.
 */
#define	MH_KEY_DESTROY(hash, key) ((hash->mh_kdtor)(key))

/*
 * MH_VAL_DESTROY()
 * 	Invoke the value destructor.
 */
#define	MH_VAL_DESTROY(hash, val) ((hash->mh_vdtor)(val))

/*
 * MH_KEYCMP()
 * 	Call the key comparator for the given hash keys.
 */
#define	MH_KEYCMP(hash, key1, key2) ((hash->mh_keycmp)(key1, key2))

/*
 * Cache for struct mod_hash_entry
 */
kmem_cache_t *mh_e_cache = NULL;
mod_hash_t *mh_head = NULL;
kmutex_t mh_head_lock;

/*
 * mod_hash_null_keydtor()
 * mod_hash_null_valdtor()
 * 	no-op key and value destructors.
 */
/*ARGSUSED*/
void
mod_hash_null_keydtor(mod_hash_key_t key)
{
}

/*ARGSUSED*/
void
mod_hash_null_valdtor(mod_hash_val_t val)
{
}

/*
 * mod_hash_bystr()
 * mod_hash_strkey_cmp()
 * mod_hash_strkey_dtor()
 * mod_hash_strval_dtor()
 *	Hash and key comparison routines for hashes with string keys.
 *
 * mod_hash_create_strhash()
 * 	Create a hash using strings as keys
 *
 *	The string hashing algorithm is from the "Dragon Book" --
 *	"Compilers: Principles, Tools & Techniques", by Aho, Sethi, Ullman
 */

/*ARGSUSED*/
uint_t
mod_hash_bystr(void *hash_data, mod_hash_key_t key)
{
	uint_t hash = 0;
	uint_t g;
	char *p, *k = (char *)key;

	ASSERT(k);
	for (p = k; *p != '\0'; p++) {
		hash = (hash << 4) + *p;
		if ((g = (hash & 0xf0000000)) != 0) {
			hash ^= (g >> 24);
			hash ^= g;
		}
	}
	return (hash);
}

int
mod_hash_strkey_cmp(mod_hash_key_t key1, mod_hash_key_t key2)
{
	return (strcmp((char *)key1, (char *)key2));
}

void
mod_hash_strkey_dtor(mod_hash_key_t key)
{
	char *c = (char *)key;
	kmem_free(c, strlen(c) + 1);
}

void
mod_hash_strval_dtor(mod_hash_val_t val)
{
	char *c = (char *)val;
	kmem_free(c, strlen(c) + 1);
}

mod_hash_t *
mod_hash_create_strhash_nodtr(char *name, size_t nchains,
    void (*val_dtor)(mod_hash_val_t))
{
	return mod_hash_create_extended(name, nchains, mod_hash_null_keydtor,
	    val_dtor, mod_hash_bystr, NULL, mod_hash_strkey_cmp, KM_SLEEP);
}

mod_hash_t *
mod_hash_create_strhash(char *name, size_t nchains,
    void (*val_dtor)(mod_hash_val_t))
{
	return mod_hash_create_extended(name, nchains, mod_hash_strkey_dtor,
	    val_dtor, mod_hash_bystr, NULL, mod_hash_strkey_cmp, KM_SLEEP);
}

void
mod_hash_destroy_strhash(mod_hash_t *strhash)
{
	ASSERT(strhash);
	mod_hash_destroy_hash(strhash);
}


/*
 * mod_hash_byptr()
 * mod_hash_ptrkey_cmp()
 *	Hash and key comparison routines for hashes with pointer keys.
 *
 * mod_hash_create_ptrhash()
 * mod_hash_destroy_ptrhash()
 * 	Create a hash that uses pointers as keys.  This hash algorithm
 * 	picks an appropriate set of middle bits in the address to hash on
 * 	based on the size of the hash table and a hint about the size of
 * 	the items pointed at.
 */
uint_t
mod_hash_byptr(void *hash_data, mod_hash_key_t key)
{
	uintptr_t k = (uintptr_t)key;
	k >>= (int)(uintptr_t)hash_data;

	return ((uint_t)k);
}

int
mod_hash_ptrkey_cmp(mod_hash_key_t key1, mod_hash_key_t key2)
{
	uintptr_t k1 = (uintptr_t)key1;
	uintptr_t k2 = (uintptr_t)key2;
	if (k1 > k2)
		return (-1);
	else if (k1 < k2)
		return (1);
	else
		return (0);
}

mod_hash_t *
mod_hash_create_ptrhash(char *name, size_t nchains,
    void (*val_dtor)(mod_hash_val_t), size_t key_elem_size)
{
	size_t rshift;

	/*
	 * We want to hash on the bits in the middle of the address word
	 * Bits far to the right in the word have little significance, and
	 * are likely to all look the same (for example, an array of
	 * 256-byte structures will have the bottom 8 bits of address
	 * words the same).  So we want to right-shift each address to
	 * ignore the bottom bits.
	 *
	 * The high bits, which are also unused, will get taken out when
	 * mod_hash takes hashkey % nchains.
	 */
	rshift = highbit(key_elem_size);

	return mod_hash_create_extended(name, nchains, mod_hash_null_keydtor,
	    val_dtor, mod_hash_byptr, (void *)rshift, mod_hash_ptrkey_cmp,
	    KM_SLEEP);
}

void
mod_hash_destroy_ptrhash(mod_hash_t *hash)
{
	ASSERT(hash);
	mod_hash_destroy_hash(hash);
}

/*
 * mod_hash_byid()
 * mod_hash_idkey_cmp()
 *	Hash and key comparison routines for hashes with 32-bit unsigned keys.
 *
 * mod_hash_create_idhash()
 * mod_hash_destroy_idhash()
 * mod_hash_iddata_gen()
 * 	Create a hash that uses numeric keys.
 *
 *	The hash algorithm is documented in "Introduction to Algorithms"
 *	(Cormen, Leiserson, Rivest);  when the hash table is created, it
 *	attempts to find the next largest prime above the number of hash
 *	slots.  The hash index is then this number times the key modulo
 *	the hash size, or (key * prime) % nchains.
 */
uint_t
mod_hash_byid(void *hash_data, mod_hash_key_t key)
{
	uint_t kval = (uint_t)(uintptr_t)hash_data;
	return ((uint_t)(uintptr_t)key * (uint_t)kval);
}

int
mod_hash_idkey_cmp(mod_hash_key_t key1, mod_hash_key_t key2)
{
	return ((uint_t)(uintptr_t)key1 - (uint_t)(uintptr_t)key2);
}

/*
 * Generate the next largest prime number greater than nchains; this value
 * is intended to be later passed in to mod_hash_create_extended() as the
 * hash_data.
 */
uint_t
mod_hash_iddata_gen(size_t nchains)
{
	uint_t kval, i, prime;

	/*
	 * Pick the first (odd) prime greater than nchains.  Make sure kval is
	 * odd (so start with nchains +1 or +2 as appropriate).
	 */
	kval = (nchains % 2 == 0) ? nchains + 1 : nchains + 2;

	for (;;) {
		prime = 1;
		for (i = 3; i * i <= kval; i += 2) {
			if (kval % i == 0)
				prime = 0;
		}
		if (prime == 1)
			break;
		kval += 2;
	}
	return (kval);
}

mod_hash_t *
mod_hash_create_idhash(char *name, size_t nchains,
    void (*val_dtor)(mod_hash_val_t))
{
	uint_t kval = mod_hash_iddata_gen(nchains);

	return (mod_hash_create_extended(name, nchains, mod_hash_null_keydtor,
	    val_dtor, mod_hash_byid, (void *)(uintptr_t)kval,
	    mod_hash_idkey_cmp, KM_SLEEP));
}

void
mod_hash_destroy_idhash(mod_hash_t *hash)
{
	ASSERT(hash);
	mod_hash_destroy_hash(hash);
}

void
mod_hash_fini(void)
{
	mutex_destroy(&mh_head_lock);

	if (mh_e_cache) {
		kmem_cache_destroy(mh_e_cache);
		mh_e_cache = NULL;
	}
}

/*
 * mod_hash_init()
 * 	sets up globals, etc for mod_hash_*
 */
void
mod_hash_init(void)
{
	ASSERT(mh_e_cache == NULL);
	mh_e_cache = kmem_cache_create("mod_hash_entries",
	    sizeof (struct mod_hash_entry), 0, NULL, NULL, NULL, NULL,
	    NULL, 0);

	mutex_init(&mh_head_lock, NULL, MUTEX_DEFAULT, NULL);
}

/*
 * mod_hash_create_extended()
 * 	The full-blown hash creation function.
 *
 * notes:
 * 	nchains		- how many hash slots to create.  More hash slots will
 *			  result in shorter hash chains, but will consume
 *			  slightly more memory up front.
 *	sleep		- should be KM_SLEEP or KM_NOSLEEP, to indicate whether
 *			  to sleep for memory, or fail in low-memory conditions.
 *
 * 	Fails only if KM_NOSLEEP was specified, and no memory was available.
 */
mod_hash_t *
mod_hash_create_extended(
    char *hname,			/* descriptive name for hash */
    size_t nchains,			/* number of hash slots */
    void (*kdtor)(mod_hash_key_t),	/* key destructor */
    void (*vdtor)(mod_hash_val_t),	/* value destructor */
    uint_t (*hash_alg)(void *, mod_hash_key_t), /* hash algorithm */
    void *hash_alg_data,		/* pass-thru arg for hash_alg */
    int (*keycmp)(mod_hash_key_t, mod_hash_key_t), /* key comparator */
    int sleep)				/* whether to sleep for mem */
{
	mod_hash_t *mod_hash;
	ASSERT(hname && keycmp && hash_alg && vdtor && kdtor);

	if ((mod_hash = kmem_zalloc(MH_SIZE(nchains), sleep)) == NULL)
		return (NULL);

	mod_hash->mh_name = kmem_alloc(strlen(hname) + 1, sleep);
	if (mod_hash->mh_name == NULL) {
		kmem_free(mod_hash, MH_SIZE(nchains));
		return (NULL);
	}
	(void) strcpy(mod_hash->mh_name, hname);

	rw_init(&mod_hash->mh_contents, NULL, RW_DEFAULT, NULL);
	mod_hash->mh_sleep = sleep;
	mod_hash->mh_nchains = nchains;
	mod_hash->mh_kdtor = kdtor;
	mod_hash->mh_vdtor = vdtor;
	mod_hash->mh_hashalg = hash_alg;
	mod_hash->mh_hashalg_data = hash_alg_data;
	mod_hash->mh_keycmp = keycmp;

	/*
	 * Link the hash up on the list of hashes
	 */
	mutex_enter(&mh_head_lock);
	mod_hash->mh_next = mh_head;
	mh_head = mod_hash;
	mutex_exit(&mh_head_lock);

	return (mod_hash);
}

/*
 * mod_hash_destroy_hash()
 * 	destroy a hash table, destroying all of its stored keys and values
 * 	as well.
 */
void
mod_hash_destroy_hash(mod_hash_t *hash)
{
	mod_hash_t *mhp, *mhpp;

	mutex_enter(&mh_head_lock);
	/*
	 * Remove the hash from the hash list
	 */
	if (hash == mh_head) {		/* removing 1st list elem */
		mh_head = mh_head->mh_next;
	} else {
		/*
		 * mhpp can start out NULL since we know the 1st elem isn't the
		 * droid we're looking for.
		 */
		mhpp = NULL;
		for (mhp = mh_head; mhp != NULL; mhp = mhp->mh_next) {
			if (mhp == hash) {
				mhpp->mh_next = mhp->mh_next;
				break;
			}
			mhpp = mhp;
		}
	}
	mutex_exit(&mh_head_lock);

	/*
	 * Clean out keys and values.
	 */
	mod_hash_clear(hash);

	rw_destroy(&hash->mh_contents);
	kmem_free(hash->mh_name, strlen(hash->mh_name) + 1);
	kmem_free(hash, MH_SIZE(hash->mh_nchains));
}

/*
 * i_mod_hash()
 * 	Call the hashing algorithm for this hash table, with the given key.
 */
uint_t
i_mod_hash(mod_hash_t *hash, mod_hash_key_t key)
{
	uint_t h;
	/*
	 * Prevent div by 0 problems;
	 * Also a nice shortcut when using a hash as a list
	 */
	if (hash->mh_nchains == 1)
		return (0);

	h = (hash->mh_hashalg)(hash->mh_hashalg_data, key);
	return (h % (hash->mh_nchains - 1));
}

/*
 * i_mod_hash_insert_nosync()
 * mod_hash_insert()
 * mod_hash_insert_reserve()
 * 	insert 'val' into the hash table, using 'key' as its key.  If 'key' is
 * 	already a key in the hash, an error will be returned, and the key-val
 * 	pair will not be inserted.  i_mod_hash_insert_nosync() supports a simple
 * 	handle abstraction, allowing hash entry allocation to be separated from
 * 	the hash insertion.  this abstraction allows simple use of the mod_hash
 * 	structure in situations where mod_hash_insert() with a KM_SLEEP
 * 	allocation policy would otherwise be unsafe.
 */
int
i_mod_hash_insert_nosync(mod_hash_t *hash, mod_hash_key_t key,
    mod_hash_val_t val, mod_hash_hndl_t handle)
{
	uint_t hashidx;
	struct mod_hash_entry *entry;

	ASSERT(hash);

	/*
	 * If we've not been given reserved storage, allocate storage directly,
	 * using the hash's allocation policy.
	 */
	if (handle == (mod_hash_hndl_t)0) {
		entry = kmem_cache_alloc(mh_e_cache, hash->mh_sleep);
		if (entry == NULL) {
			hash->mh_stat.mhs_nomem++;
			return (MH_ERR_NOMEM);
		}
	} else {
		entry = (struct mod_hash_entry *)handle;
	}

	hashidx = i_mod_hash(hash, key);
	entry->mhe_key = key;
	entry->mhe_val = val;
	entry->mhe_next = hash->mh_entries[hashidx];

	hash->mh_entries[hashidx] = entry;
	hash->mh_stat.mhs_nelems++;

	return (0);
}

int
mod_hash_insert(mod_hash_t *hash, mod_hash_key_t key, mod_hash_val_t val)
{
	int res;
	mod_hash_val_t v;

	rw_enter(&hash->mh_contents, RW_WRITER);

	/*
	 * Disallow duplicate keys in the hash
	 */
	if (i_mod_hash_find_nosync(hash, key, &v) == 0) {
		rw_exit(&hash->mh_contents);
		hash->mh_stat.mhs_coll++;
		return (MH_ERR_DUPLICATE);
	}

	res = i_mod_hash_insert_nosync(hash, key, val, (mod_hash_hndl_t)0);
	rw_exit(&hash->mh_contents);

	return (res);
}

int
mod_hash_insert_reserve(mod_hash_t *hash, mod_hash_key_t key,
    mod_hash_val_t val, mod_hash_hndl_t handle)
{
	int res;
	mod_hash_val_t v;

	rw_enter(&hash->mh_contents, RW_WRITER);

	/*
	 * Disallow duplicate keys in the hash
	 */
	if (i_mod_hash_find_nosync(hash, key, &v) == 0) {
		rw_exit(&hash->mh_contents);
		hash->mh_stat.mhs_coll++;
		return (MH_ERR_DUPLICATE);
	}
	res = i_mod_hash_insert_nosync(hash, key, val, handle);
	rw_exit(&hash->mh_contents);

	return (res);
}

/*
 * mod_hash_reserve()
 * mod_hash_reserve_nosleep()
 * mod_hash_cancel()
 *   Make or cancel a mod_hash_entry_t reservation.  Reservations are used in
 *   mod_hash_insert_reserve() above.
 */
int
mod_hash_reserve(mod_hash_t *hash, mod_hash_hndl_t *handlep)
{
	*handlep = kmem_cache_alloc(mh_e_cache, hash->mh_sleep);
	if (*handlep == NULL) {
		hash->mh_stat.mhs_nomem++;
		return (MH_ERR_NOMEM);
	}

	return (0);
}

int
mod_hash_reserve_nosleep(mod_hash_t *hash, mod_hash_hndl_t *handlep)
{
	*handlep = kmem_cache_alloc(mh_e_cache, KM_NOSLEEP);
	if (*handlep == NULL) {
		hash->mh_stat.mhs_nomem++;
		return (MH_ERR_NOMEM);
	}

	return (0);

}

/*ARGSUSED*/
void
mod_hash_cancel(mod_hash_t *hash, mod_hash_hndl_t *handlep)
{
	kmem_cache_free(mh_e_cache, *handlep);
	*handlep = (mod_hash_hndl_t)0;
}

/*
 * i_mod_hash_remove_nosync()
 * mod_hash_remove()
 * 	Remove an element from the hash table.
 */
int
i_mod_hash_remove_nosync(mod_hash_t *hash, mod_hash_key_t key,
    mod_hash_val_t *val)
{
	int hashidx;
	struct mod_hash_entry *e, *ep;

	hashidx = i_mod_hash(hash, key);
	ep = NULL; /* e's parent */

	for (e = hash->mh_entries[hashidx]; e != NULL; e = e->mhe_next) {
		if (MH_KEYCMP(hash, e->mhe_key, key) == 0)
			break;
		ep = e;
	}

	if (e == NULL) {	/* not found */
		return (MH_ERR_NOTFOUND);
	}

	if (ep == NULL) 	/* special case 1st element in bucket */
		hash->mh_entries[hashidx] = e->mhe_next;
	else
		ep->mhe_next = e->mhe_next;

	/*
	 * Clean up resources used by the node's key.
	 */
	MH_KEY_DESTROY(hash, e->mhe_key);

	*val = e->mhe_val;
	kmem_cache_free(mh_e_cache, e);
	hash->mh_stat.mhs_nelems--;

	return (0);
}

int
mod_hash_remove(mod_hash_t *hash, mod_hash_key_t key, mod_hash_val_t *val)
{
	int res;

	rw_enter(&hash->mh_contents, RW_WRITER);
	res = i_mod_hash_remove_nosync(hash, key, val);
	rw_exit(&hash->mh_contents);

	return (res);
}

/*
 * mod_hash_replace()
 * 	atomically remove an existing key-value pair from a hash, and replace
 * 	the key and value with the ones supplied.  The removed key and value
 * 	(if any) are destroyed.
 */
int
mod_hash_replace(mod_hash_t *hash, mod_hash_key_t key, mod_hash_val_t val)
{
	int res;
	mod_hash_val_t v;

	rw_enter(&hash->mh_contents, RW_WRITER);

	if (i_mod_hash_remove_nosync(hash, key, &v) == 0) {
		/*
		 * mod_hash_remove() takes care of freeing up the key resources.
		 */
		MH_VAL_DESTROY(hash, v);
	}
	res = i_mod_hash_insert_nosync(hash, key, val, (mod_hash_hndl_t)0);

	rw_exit(&hash->mh_contents);

	return (res);
}

/*
 * mod_hash_destroy()
 * 	Remove an element from the hash table matching 'key', and destroy it.
 */
int
mod_hash_destroy(mod_hash_t *hash, mod_hash_key_t key)
{
	mod_hash_val_t val;
	int rv;

	rw_enter(&hash->mh_contents, RW_WRITER);

	if ((rv = i_mod_hash_remove_nosync(hash, key, &val)) == 0) {
		/*
		 * mod_hash_remove() takes care of freeing up the key resources.
		 */
		MH_VAL_DESTROY(hash, val);
	}

	rw_exit(&hash->mh_contents);
	return (rv);
}

/*
 * i_mod_hash_find_nosync()
 * mod_hash_find()
 * 	Find a value in the hash table corresponding to the given key.
 */
int
i_mod_hash_find_nosync(mod_hash_t *hash, mod_hash_key_t key,
    mod_hash_val_t *val)
{
	uint_t hashidx;
	struct mod_hash_entry *e;

	hashidx = i_mod_hash(hash, key);

	for (e = hash->mh_entries[hashidx]; e != NULL; e = e->mhe_next) {
		if (MH_KEYCMP(hash, e->mhe_key, key) == 0) {
			*val = e->mhe_val;
			hash->mh_stat.mhs_hit++;
			return (0);
		}
	}
	hash->mh_stat.mhs_miss++;
	return (MH_ERR_NOTFOUND);
}

int
mod_hash_find(mod_hash_t *hash, mod_hash_key_t key, mod_hash_val_t *val)
{
	int res;

	rw_enter(&hash->mh_contents, RW_READER);
	res = i_mod_hash_find_nosync(hash, key, val);
	rw_exit(&hash->mh_contents);

	return (res);
}

int
mod_hash_find_cb(mod_hash_t *hash, mod_hash_key_t key, mod_hash_val_t *val,
    void (*find_cb)(mod_hash_key_t, mod_hash_val_t))
{
	int res;

	rw_enter(&hash->mh_contents, RW_READER);
	res = i_mod_hash_find_nosync(hash, key, val);
	if (res == 0) {
		find_cb(key, *val);
	}
	rw_exit(&hash->mh_contents);

	return (res);
}

int
mod_hash_find_cb_rval(mod_hash_t *hash, mod_hash_key_t key, mod_hash_val_t *val,
    int (*find_cb)(mod_hash_key_t, mod_hash_val_t), int *cb_rval)
{
	int res;

	rw_enter(&hash->mh_contents, RW_READER);
	res = i_mod_hash_find_nosync(hash, key, val);
	if (res == 0) {
		*cb_rval = find_cb(key, *val);
	}
	rw_exit(&hash->mh_contents);

	return (res);
}

void
i_mod_hash_walk_nosync(mod_hash_t *hash,
    uint_t (*callback)(mod_hash_key_t, mod_hash_val_t *, void *), void *arg)
{
	struct mod_hash_entry	*e;
	uint_t			hashidx;
	int			res = MH_WALK_CONTINUE;

	for (hashidx = 0;
	    (hashidx < (hash->mh_nchains - 1)) && (res == MH_WALK_CONTINUE);
	    hashidx++) {
		e = hash->mh_entries[hashidx];
		while ((e != NULL) && (res == MH_WALK_CONTINUE)) {
			res = callback(e->mhe_key, e->mhe_val, arg);
			e = e->mhe_next;
		}
	}
}

/*
 * mod_hash_walk()
 * 	Walks all the elements in the hashtable and invokes the callback
 * 	function with the key/value pair for each element.  The hashtable
 * 	is locked for readers so the callback function should not attempt
 * 	to do any updates to the hashable.  The callback function should
 * 	return MH_WALK_CONTINUE to continue walking the hashtable or
 * 	MH_WALK_TERMINATE to abort the walk of the hashtable.
 */
void
mod_hash_walk(mod_hash_t *hash,
    uint_t (*callback)(mod_hash_key_t, mod_hash_val_t *, void *), void *arg)
{
	rw_enter(&hash->mh_contents, RW_READER);
	i_mod_hash_walk_nosync(hash, callback, arg);
	rw_exit(&hash->mh_contents);
}


/*
 * i_mod_hash_clear_nosync()
 * mod_hash_clear()
 *	Clears the given hash table by calling the destructor of every hash
 *	element and freeing up all mod_hash_entry's.
 */
void
i_mod_hash_clear_nosync(mod_hash_t *hash)
{
	int i;
	struct mod_hash_entry *e, *old_e;

	for (i = 0; i < hash->mh_nchains; i++) {
		e = hash->mh_entries[i];
		while (e != NULL) {
			MH_KEY_DESTROY(hash, e->mhe_key);
			MH_VAL_DESTROY(hash, e->mhe_val);
			old_e = e;
			e = e->mhe_next;
			kmem_cache_free(mh_e_cache, old_e);
		}
		hash->mh_entries[i] = NULL;
	}
	hash->mh_stat.mhs_nelems = 0;
}

void
mod_hash_clear(mod_hash_t *hash)
{
	ASSERT(hash);
	rw_enter(&hash->mh_contents, RW_WRITER);
	i_mod_hash_clear_nosync(hash);
	rw_exit(&hash->mh_contents);
}
