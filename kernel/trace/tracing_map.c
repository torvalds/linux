// SPDX-License-Identifier: GPL-2.0
/*
 * tracing_map - lock-free map for tracing
 *
 * Copyright (C) 2015 Tom Zanussi <tom.zanussi@linux.intel.com>
 *
 * tracing_map implementation inspired by lock-free map algorithms
 * originated by Dr. Cliff Click:
 *
 * http://www.azulsystems.com/blog/cliff/2007-03-26-non-blocking-hashtable
 * http://www.azulsystems.com/events/javaone_2007/2007_LockFreeHash.pdf
 */

#include <linux/vmalloc.h>
#include <linux/jhash.h>
#include <linux/slab.h>
#include <linux/sort.h>
#include <linux/kmemleak.h>

#include "tracing_map.h"
#include "trace.h"

/*
 * NOTE: For a detailed description of the data structures used by
 * these functions (such as tracing_map_elt) please see the overview
 * of tracing_map data structures at the beginning of tracing_map.h.
 */

/**
 * tracing_map_update_sum - Add a value to a tracing_map_elt's sum field
 * @elt: The tracing_map_elt
 * @i: The index of the given sum associated with the tracing_map_elt
 * @n: The value to add to the sum
 *
 * Add n to sum i associated with the specified tracing_map_elt
 * instance.  The index i is the index returned by the call to
 * tracing_map_add_sum_field() when the tracing map was set up.
 */
void tracing_map_update_sum(struct tracing_map_elt *elt, unsigned int i, u64 n)
{
	atomic64_add(n, &elt->fields[i].sum);
}

/**
 * tracing_map_read_sum - Return the value of a tracing_map_elt's sum field
 * @elt: The tracing_map_elt
 * @i: The index of the given sum associated with the tracing_map_elt
 *
 * Retrieve the value of the sum i associated with the specified
 * tracing_map_elt instance.  The index i is the index returned by the
 * call to tracing_map_add_sum_field() when the tracing map was set
 * up.
 *
 * Return: The sum associated with field i for elt.
 */
u64 tracing_map_read_sum(struct tracing_map_elt *elt, unsigned int i)
{
	return (u64)atomic64_read(&elt->fields[i].sum);
}

/**
 * tracing_map_set_var - Assign a tracing_map_elt's variable field
 * @elt: The tracing_map_elt
 * @i: The index of the given variable associated with the tracing_map_elt
 * @n: The value to assign
 *
 * Assign n to variable i associated with the specified tracing_map_elt
 * instance.  The index i is the index returned by the call to
 * tracing_map_add_var() when the tracing map was set up.
 */
void tracing_map_set_var(struct tracing_map_elt *elt, unsigned int i, u64 n)
{
	atomic64_set(&elt->vars[i], n);
	elt->var_set[i] = true;
}

/**
 * tracing_map_var_set - Return whether or not a variable has been set
 * @elt: The tracing_map_elt
 * @i: The index of the given variable associated with the tracing_map_elt
 *
 * Return true if the variable has been set, false otherwise.  The
 * index i is the index returned by the call to tracing_map_add_var()
 * when the tracing map was set up.
 */
bool tracing_map_var_set(struct tracing_map_elt *elt, unsigned int i)
{
	return elt->var_set[i];
}

/**
 * tracing_map_read_var - Return the value of a tracing_map_elt's variable field
 * @elt: The tracing_map_elt
 * @i: The index of the given variable associated with the tracing_map_elt
 *
 * Retrieve the value of the variable i associated with the specified
 * tracing_map_elt instance.  The index i is the index returned by the
 * call to tracing_map_add_var() when the tracing map was set
 * up.
 *
 * Return: The variable value associated with field i for elt.
 */
u64 tracing_map_read_var(struct tracing_map_elt *elt, unsigned int i)
{
	return (u64)atomic64_read(&elt->vars[i]);
}

/**
 * tracing_map_read_var_once - Return and reset a tracing_map_elt's variable field
 * @elt: The tracing_map_elt
 * @i: The index of the given variable associated with the tracing_map_elt
 *
 * Retrieve the value of the variable i associated with the specified
 * tracing_map_elt instance, and reset the variable to the 'not set'
 * state.  The index i is the index returned by the call to
 * tracing_map_add_var() when the tracing map was set up.  The reset
 * essentially makes the variable a read-once variable if it's only
 * accessed using this function.
 *
 * Return: The variable value associated with field i for elt.
 */
u64 tracing_map_read_var_once(struct tracing_map_elt *elt, unsigned int i)
{
	elt->var_set[i] = false;
	return (u64)atomic64_read(&elt->vars[i]);
}

int tracing_map_cmp_string(void *val_a, void *val_b)
{
	char *a = val_a;
	char *b = val_b;

	return strcmp(a, b);
}

int tracing_map_cmp_none(void *val_a, void *val_b)
{
	return 0;
}

static int tracing_map_cmp_atomic64(void *val_a, void *val_b)
{
	u64 a = atomic64_read((atomic64_t *)val_a);
	u64 b = atomic64_read((atomic64_t *)val_b);

	return (a > b) ? 1 : ((a < b) ? -1 : 0);
}

#define DEFINE_TRACING_MAP_CMP_FN(type)					\
static int tracing_map_cmp_##type(void *val_a, void *val_b)		\
{									\
	type a = (type)(*(u64 *)val_a);					\
	type b = (type)(*(u64 *)val_b);					\
									\
	return (a > b) ? 1 : ((a < b) ? -1 : 0);			\
}

DEFINE_TRACING_MAP_CMP_FN(s64);
DEFINE_TRACING_MAP_CMP_FN(u64);
DEFINE_TRACING_MAP_CMP_FN(s32);
DEFINE_TRACING_MAP_CMP_FN(u32);
DEFINE_TRACING_MAP_CMP_FN(s16);
DEFINE_TRACING_MAP_CMP_FN(u16);
DEFINE_TRACING_MAP_CMP_FN(s8);
DEFINE_TRACING_MAP_CMP_FN(u8);

tracing_map_cmp_fn_t tracing_map_cmp_num(int field_size,
					 int field_is_signed)
{
	tracing_map_cmp_fn_t fn = tracing_map_cmp_none;

	switch (field_size) {
	case 8:
		if (field_is_signed)
			fn = tracing_map_cmp_s64;
		else
			fn = tracing_map_cmp_u64;
		break;
	case 4:
		if (field_is_signed)
			fn = tracing_map_cmp_s32;
		else
			fn = tracing_map_cmp_u32;
		break;
	case 2:
		if (field_is_signed)
			fn = tracing_map_cmp_s16;
		else
			fn = tracing_map_cmp_u16;
		break;
	case 1:
		if (field_is_signed)
			fn = tracing_map_cmp_s8;
		else
			fn = tracing_map_cmp_u8;
		break;
	}

	return fn;
}

static int tracing_map_add_field(struct tracing_map *map,
				 tracing_map_cmp_fn_t cmp_fn)
{
	int ret = -EINVAL;

	if (map->n_fields < TRACING_MAP_FIELDS_MAX) {
		ret = map->n_fields;
		map->fields[map->n_fields++].cmp_fn = cmp_fn;
	}

	return ret;
}

/**
 * tracing_map_add_sum_field - Add a field describing a tracing_map sum
 * @map: The tracing_map
 *
 * Add a sum field to the key and return the index identifying it in
 * the map and associated tracing_map_elts.  This is the index used
 * for instance to update a sum for a particular tracing_map_elt using
 * tracing_map_update_sum() or reading it via tracing_map_read_sum().
 *
 * Return: The index identifying the field in the map and associated
 * tracing_map_elts, or -EINVAL on error.
 */
int tracing_map_add_sum_field(struct tracing_map *map)
{
	return tracing_map_add_field(map, tracing_map_cmp_atomic64);
}

/**
 * tracing_map_add_var - Add a field describing a tracing_map var
 * @map: The tracing_map
 *
 * Add a var to the map and return the index identifying it in the map
 * and associated tracing_map_elts.  This is the index used for
 * instance to update a var for a particular tracing_map_elt using
 * tracing_map_update_var() or reading it via tracing_map_read_var().
 *
 * Return: The index identifying the var in the map and associated
 * tracing_map_elts, or -EINVAL on error.
 */
int tracing_map_add_var(struct tracing_map *map)
{
	int ret = -EINVAL;

	if (map->n_vars < TRACING_MAP_VARS_MAX)
		ret = map->n_vars++;

	return ret;
}

/**
 * tracing_map_add_key_field - Add a field describing a tracing_map key
 * @map: The tracing_map
 * @offset: The offset within the key
 * @cmp_fn: The comparison function that will be used to sort on the key
 *
 * Let the map know there is a key and that if it's used as a sort key
 * to use cmp_fn.
 *
 * A key can be a subset of a compound key; for that purpose, the
 * offset param is used to describe where within the compound key
 * the key referenced by this key field resides.
 *
 * Return: The index identifying the field in the map and associated
 * tracing_map_elts, or -EINVAL on error.
 */
int tracing_map_add_key_field(struct tracing_map *map,
			      unsigned int offset,
			      tracing_map_cmp_fn_t cmp_fn)

{
	int idx = tracing_map_add_field(map, cmp_fn);

	if (idx < 0)
		return idx;

	map->fields[idx].offset = offset;

	map->key_idx[map->n_keys++] = idx;

	return idx;
}

static void tracing_map_array_clear(struct tracing_map_array *a)
{
	unsigned int i;

	if (!a->pages)
		return;

	for (i = 0; i < a->n_pages; i++)
		memset(a->pages[i], 0, PAGE_SIZE);
}

static void tracing_map_array_free(struct tracing_map_array *a)
{
	unsigned int i;

	if (!a)
		return;

	if (!a->pages)
		goto free;

	for (i = 0; i < a->n_pages; i++) {
		if (!a->pages[i])
			break;
		kmemleak_free(a->pages[i]);
		free_page((unsigned long)a->pages[i]);
	}

	kfree(a->pages);

 free:
	kfree(a);
}

static struct tracing_map_array *tracing_map_array_alloc(unsigned int n_elts,
						  unsigned int entry_size)
{
	struct tracing_map_array *a;
	unsigned int i;

	a = kzalloc(sizeof(*a), GFP_KERNEL);
	if (!a)
		return NULL;

	a->entry_size_shift = fls(roundup_pow_of_two(entry_size) - 1);
	a->entries_per_page = PAGE_SIZE / (1 << a->entry_size_shift);
	a->n_pages = n_elts / a->entries_per_page;
	if (!a->n_pages)
		a->n_pages = 1;
	a->entry_shift = fls(a->entries_per_page) - 1;
	a->entry_mask = (1 << a->entry_shift) - 1;

	a->pages = kcalloc(a->n_pages, sizeof(void *), GFP_KERNEL);
	if (!a->pages)
		goto free;

	for (i = 0; i < a->n_pages; i++) {
		a->pages[i] = (void *)get_zeroed_page(GFP_KERNEL);
		if (!a->pages[i])
			goto free;
		kmemleak_alloc(a->pages[i], PAGE_SIZE, 1, GFP_KERNEL);
	}
 out:
	return a;
 free:
	tracing_map_array_free(a);
	a = NULL;

	goto out;
}

static void tracing_map_elt_clear(struct tracing_map_elt *elt)
{
	unsigned i;

	for (i = 0; i < elt->map->n_fields; i++)
		if (elt->fields[i].cmp_fn == tracing_map_cmp_atomic64)
			atomic64_set(&elt->fields[i].sum, 0);

	for (i = 0; i < elt->map->n_vars; i++) {
		atomic64_set(&elt->vars[i], 0);
		elt->var_set[i] = false;
	}

	if (elt->map->ops && elt->map->ops->elt_clear)
		elt->map->ops->elt_clear(elt);
}

static void tracing_map_elt_init_fields(struct tracing_map_elt *elt)
{
	unsigned int i;

	tracing_map_elt_clear(elt);

	for (i = 0; i < elt->map->n_fields; i++) {
		elt->fields[i].cmp_fn = elt->map->fields[i].cmp_fn;

		if (elt->fields[i].cmp_fn != tracing_map_cmp_atomic64)
			elt->fields[i].offset = elt->map->fields[i].offset;
	}
}

static void tracing_map_elt_free(struct tracing_map_elt *elt)
{
	if (!elt)
		return;

	if (elt->map->ops && elt->map->ops->elt_free)
		elt->map->ops->elt_free(elt);
	kfree(elt->fields);
	kfree(elt->vars);
	kfree(elt->var_set);
	kfree(elt->key);
	kfree(elt);
}

static struct tracing_map_elt *tracing_map_elt_alloc(struct tracing_map *map)
{
	struct tracing_map_elt *elt;
	int err = 0;

	elt = kzalloc(sizeof(*elt), GFP_KERNEL);
	if (!elt)
		return ERR_PTR(-ENOMEM);

	elt->map = map;

	elt->key = kzalloc(map->key_size, GFP_KERNEL);
	if (!elt->key) {
		err = -ENOMEM;
		goto free;
	}

	elt->fields = kcalloc(map->n_fields, sizeof(*elt->fields), GFP_KERNEL);
	if (!elt->fields) {
		err = -ENOMEM;
		goto free;
	}

	elt->vars = kcalloc(map->n_vars, sizeof(*elt->vars), GFP_KERNEL);
	if (!elt->vars) {
		err = -ENOMEM;
		goto free;
	}

	elt->var_set = kcalloc(map->n_vars, sizeof(*elt->var_set), GFP_KERNEL);
	if (!elt->var_set) {
		err = -ENOMEM;
		goto free;
	}

	tracing_map_elt_init_fields(elt);

	if (map->ops && map->ops->elt_alloc) {
		err = map->ops->elt_alloc(elt);
		if (err)
			goto free;
	}
	return elt;
 free:
	tracing_map_elt_free(elt);

	return ERR_PTR(err);
}

static struct tracing_map_elt *get_free_elt(struct tracing_map *map)
{
	struct tracing_map_elt *elt = NULL;
	int idx;

	idx = atomic_fetch_add_unless(&map->next_elt, 1, map->max_elts);
	if (idx < map->max_elts) {
		elt = *(TRACING_MAP_ELT(map->elts, idx));
		if (map->ops && map->ops->elt_init)
			map->ops->elt_init(elt);
	}

	return elt;
}

static void tracing_map_free_elts(struct tracing_map *map)
{
	unsigned int i;

	if (!map->elts)
		return;

	for (i = 0; i < map->max_elts; i++) {
		tracing_map_elt_free(*(TRACING_MAP_ELT(map->elts, i)));
		*(TRACING_MAP_ELT(map->elts, i)) = NULL;
	}

	tracing_map_array_free(map->elts);
	map->elts = NULL;
}

static int tracing_map_alloc_elts(struct tracing_map *map)
{
	unsigned int i;

	map->elts = tracing_map_array_alloc(map->max_elts,
					    sizeof(struct tracing_map_elt *));
	if (!map->elts)
		return -ENOMEM;

	for (i = 0; i < map->max_elts; i++) {
		*(TRACING_MAP_ELT(map->elts, i)) = tracing_map_elt_alloc(map);
		if (IS_ERR(*(TRACING_MAP_ELT(map->elts, i)))) {
			*(TRACING_MAP_ELT(map->elts, i)) = NULL;
			tracing_map_free_elts(map);

			return -ENOMEM;
		}
	}

	return 0;
}

static inline bool keys_match(void *key, void *test_key, unsigned key_size)
{
	bool match = true;

	if (memcmp(key, test_key, key_size))
		match = false;

	return match;
}

static inline struct tracing_map_elt *
__tracing_map_insert(struct tracing_map *map, void *key, bool lookup_only)
{
	u32 idx, key_hash, test_key;
	int dup_try = 0;
	struct tracing_map_entry *entry;
	struct tracing_map_elt *val;

	key_hash = jhash(key, map->key_size, 0);
	if (key_hash == 0)
		key_hash = 1;
	idx = key_hash >> (32 - (map->map_bits + 1));

	while (1) {
		idx &= (map->map_size - 1);
		entry = TRACING_MAP_ENTRY(map->map, idx);
		test_key = entry->key;

		if (test_key && test_key == key_hash) {
			val = READ_ONCE(entry->val);
			if (val &&
			    keys_match(key, val->key, map->key_size)) {
				if (!lookup_only)
					atomic64_inc(&map->hits);
				return val;
			} else if (unlikely(!val)) {
				/*
				 * The key is present. But, val (pointer to elt
				 * struct) is still NULL. which means some other
				 * thread is in the process of inserting an
				 * element.
				 *
				 * On top of that, it's key_hash is same as the
				 * one being inserted right now. So, it's
				 * possible that the element has the same
				 * key as well.
				 */

				dup_try++;
				if (dup_try > map->map_size) {
					atomic64_inc(&map->drops);
					break;
				}
				continue;
			}
		}

		if (!test_key) {
			if (lookup_only)
				break;

			if (!cmpxchg(&entry->key, 0, key_hash)) {
				struct tracing_map_elt *elt;

				elt = get_free_elt(map);
				if (!elt) {
					atomic64_inc(&map->drops);
					entry->key = 0;
					break;
				}

				memcpy(elt->key, key, map->key_size);
				/*
				 * Ensure the initialization is visible and
				 * publish the elt.
				 */
				smp_wmb();
				WRITE_ONCE(entry->val, elt);
				atomic64_inc(&map->hits);

				return entry->val;
			} else {
				/*
				 * cmpxchg() failed. Loop around once
				 * more to check what key was inserted.
				 */
				dup_try++;
				continue;
			}
		}

		idx++;
	}

	return NULL;
}

/**
 * tracing_map_insert - Insert key and/or retrieve val from a tracing_map
 * @map: The tracing_map to insert into
 * @key: The key to insert
 *
 * Inserts a key into a tracing_map and creates and returns a new
 * tracing_map_elt for it, or if the key has already been inserted by
 * a previous call, returns the tracing_map_elt already associated
 * with it.  When the map was created, the number of elements to be
 * allocated for the map was specified (internally maintained as
 * 'max_elts' in struct tracing_map), and that number of
 * tracing_map_elts was created by tracing_map_init().  This is the
 * pre-allocated pool of tracing_map_elts that tracing_map_insert()
 * will allocate from when adding new keys.  Once that pool is
 * exhausted, tracing_map_insert() is useless and will return NULL to
 * signal that state.  There are two user-visible tracing_map
 * variables, 'hits' and 'drops', which are updated by this function.
 * Every time an element is either successfully inserted or retrieved,
 * the 'hits' value is incremented.  Every time an element insertion
 * fails, the 'drops' value is incremented.
 *
 * This is a lock-free tracing map insertion function implementing a
 * modified form of Cliff Click's basic insertion algorithm.  It
 * requires the table size be a power of two.  To prevent any
 * possibility of an infinite loop we always make the internal table
 * size double the size of the requested table size (max_elts * 2).
 * Likewise, we never reuse a slot or resize or delete elements - when
 * we've reached max_elts entries, we simply return NULL once we've
 * run out of entries.  Readers can at any point in time traverse the
 * tracing map and safely access the key/val pairs.
 *
 * Return: the tracing_map_elt pointer val associated with the key.
 * If this was a newly inserted key, the val will be a newly allocated
 * and associated tracing_map_elt pointer val.  If the key wasn't
 * found and the pool of tracing_map_elts has been exhausted, NULL is
 * returned and no further insertions will succeed.
 */
struct tracing_map_elt *tracing_map_insert(struct tracing_map *map, void *key)
{
	return __tracing_map_insert(map, key, false);
}

/**
 * tracing_map_lookup - Retrieve val from a tracing_map
 * @map: The tracing_map to perform the lookup on
 * @key: The key to look up
 *
 * Looks up key in tracing_map and if found returns the matching
 * tracing_map_elt.  This is a lock-free lookup; see
 * tracing_map_insert() for details on tracing_map and how it works.
 * Every time an element is retrieved, the 'hits' value is
 * incremented.  There is one user-visible tracing_map variable,
 * 'hits', which is updated by this function.  Every time an element
 * is successfully retrieved, the 'hits' value is incremented.  The
 * 'drops' value is never updated by this function.
 *
 * Return: the tracing_map_elt pointer val associated with the key.
 * If the key wasn't found, NULL is returned.
 */
struct tracing_map_elt *tracing_map_lookup(struct tracing_map *map, void *key)
{
	return __tracing_map_insert(map, key, true);
}

/**
 * tracing_map_destroy - Destroy a tracing_map
 * @map: The tracing_map to destroy
 *
 * Frees a tracing_map along with its associated array of
 * tracing_map_elts.
 *
 * Callers should make sure there are no readers or writers actively
 * reading or inserting into the map before calling this.
 */
void tracing_map_destroy(struct tracing_map *map)
{
	if (!map)
		return;

	tracing_map_free_elts(map);

	tracing_map_array_free(map->map);
	kfree(map);
}

/**
 * tracing_map_clear - Clear a tracing_map
 * @map: The tracing_map to clear
 *
 * Resets the tracing map to a cleared or initial state.  The
 * tracing_map_elts are all cleared, and the array of struct
 * tracing_map_entry is reset to an initialized state.
 *
 * Callers should make sure there are no writers actively inserting
 * into the map before calling this.
 */
void tracing_map_clear(struct tracing_map *map)
{
	unsigned int i;

	atomic_set(&map->next_elt, 0);
	atomic64_set(&map->hits, 0);
	atomic64_set(&map->drops, 0);

	tracing_map_array_clear(map->map);

	for (i = 0; i < map->max_elts; i++)
		tracing_map_elt_clear(*(TRACING_MAP_ELT(map->elts, i)));
}

static void set_sort_key(struct tracing_map *map,
			 struct tracing_map_sort_key *sort_key)
{
	map->sort_key = *sort_key;
}

/**
 * tracing_map_create - Create a lock-free map and element pool
 * @map_bits: The size of the map (2 ** map_bits)
 * @key_size: The size of the key for the map in bytes
 * @ops: Optional client-defined tracing_map_ops instance
 * @private_data: Client data associated with the map
 *
 * Creates and sets up a map to contain 2 ** map_bits number of
 * elements (internally maintained as 'max_elts' in struct
 * tracing_map).  Before using, map fields should be added to the map
 * with tracing_map_add_sum_field() and tracing_map_add_key_field().
 * tracing_map_init() should then be called to allocate the array of
 * tracing_map_elts, in order to avoid allocating anything in the map
 * insertion path.  The user-specified map size reflects the maximum
 * number of elements that can be contained in the table requested by
 * the user - internally we double that in order to keep the table
 * sparse and keep collisions manageable.
 *
 * A tracing_map is a special-purpose map designed to aggregate or
 * 'sum' one or more values associated with a specific object of type
 * tracing_map_elt, which is attached by the map to a given key.
 *
 * tracing_map_create() sets up the map itself, and provides
 * operations for inserting tracing_map_elts, but doesn't allocate the
 * tracing_map_elts themselves, or provide a means for describing the
 * keys or sums associated with the tracing_map_elts.  All
 * tracing_map_elts for a given map have the same set of sums and
 * keys, which are defined by the client using the functions
 * tracing_map_add_key_field() and tracing_map_add_sum_field().  Once
 * the fields are defined, the pool of elements allocated for the map
 * can be created, which occurs when the client code calls
 * tracing_map_init().
 *
 * When tracing_map_init() returns, tracing_map_elt elements can be
 * inserted into the map using tracing_map_insert().  When called,
 * tracing_map_insert() grabs a free tracing_map_elt from the pool, or
 * finds an existing match in the map and in either case returns it.
 * The client can then use tracing_map_update_sum() and
 * tracing_map_read_sum() to update or read a given sum field for the
 * tracing_map_elt.
 *
 * The client can at any point retrieve and traverse the current set
 * of inserted tracing_map_elts in a tracing_map, via
 * tracing_map_sort_entries().  Sorting can be done on any field,
 * including keys.
 *
 * See tracing_map.h for a description of tracing_map_ops.
 *
 * Return: the tracing_map pointer if successful, ERR_PTR if not.
 */
struct tracing_map *tracing_map_create(unsigned int map_bits,
				       unsigned int key_size,
				       const struct tracing_map_ops *ops,
				       void *private_data)
{
	struct tracing_map *map;
	unsigned int i;

	if (map_bits < TRACING_MAP_BITS_MIN ||
	    map_bits > TRACING_MAP_BITS_MAX)
		return ERR_PTR(-EINVAL);

	map = kzalloc(sizeof(*map), GFP_KERNEL);
	if (!map)
		return ERR_PTR(-ENOMEM);

	map->map_bits = map_bits;
	map->max_elts = (1 << map_bits);
	atomic_set(&map->next_elt, 0);

	map->map_size = (1 << (map_bits + 1));
	map->ops = ops;

	map->private_data = private_data;

	map->map = tracing_map_array_alloc(map->map_size,
					   sizeof(struct tracing_map_entry));
	if (!map->map)
		goto free;

	map->key_size = key_size;
	for (i = 0; i < TRACING_MAP_KEYS_MAX; i++)
		map->key_idx[i] = -1;
 out:
	return map;
 free:
	tracing_map_destroy(map);
	map = ERR_PTR(-ENOMEM);

	goto out;
}

/**
 * tracing_map_init - Allocate and clear a map's tracing_map_elts
 * @map: The tracing_map to initialize
 *
 * Allocates a clears a pool of tracing_map_elts equal to the
 * user-specified size of 2 ** map_bits (internally maintained as
 * 'max_elts' in struct tracing_map).  Before using, the map fields
 * should be added to the map with tracing_map_add_sum_field() and
 * tracing_map_add_key_field().  tracing_map_init() should then be
 * called to allocate the array of tracing_map_elts, in order to avoid
 * allocating anything in the map insertion path.  The user-specified
 * map size reflects the max number of elements requested by the user
 * - internally we double that in order to keep the table sparse and
 * keep collisions manageable.
 *
 * See tracing_map.h for a description of tracing_map_ops.
 *
 * Return: the tracing_map pointer if successful, ERR_PTR if not.
 */
int tracing_map_init(struct tracing_map *map)
{
	int err;

	if (map->n_fields < 2)
		return -EINVAL; /* need at least 1 key and 1 val */

	err = tracing_map_alloc_elts(map);
	if (err)
		return err;

	tracing_map_clear(map);

	return err;
}

static int cmp_entries_dup(const void *A, const void *B)
{
	const struct tracing_map_sort_entry *a, *b;

	a = *(const struct tracing_map_sort_entry **)A;
	b = *(const struct tracing_map_sort_entry **)B;

	return memcmp(a->key, b->key, a->elt->map->key_size);
}

static int cmp_entries_sum(const void *A, const void *B)
{
	const struct tracing_map_elt *elt_a, *elt_b;
	const struct tracing_map_sort_entry *a, *b;
	struct tracing_map_sort_key *sort_key;
	struct tracing_map_field *field;
	tracing_map_cmp_fn_t cmp_fn;
	void *val_a, *val_b;
	int ret = 0;

	a = *(const struct tracing_map_sort_entry **)A;
	b = *(const struct tracing_map_sort_entry **)B;

	elt_a = a->elt;
	elt_b = b->elt;

	sort_key = &elt_a->map->sort_key;

	field = &elt_a->fields[sort_key->field_idx];
	cmp_fn = field->cmp_fn;

	val_a = &elt_a->fields[sort_key->field_idx].sum;
	val_b = &elt_b->fields[sort_key->field_idx].sum;

	ret = cmp_fn(val_a, val_b);
	if (sort_key->descending)
		ret = -ret;

	return ret;
}

static int cmp_entries_key(const void *A, const void *B)
{
	const struct tracing_map_elt *elt_a, *elt_b;
	const struct tracing_map_sort_entry *a, *b;
	struct tracing_map_sort_key *sort_key;
	struct tracing_map_field *field;
	tracing_map_cmp_fn_t cmp_fn;
	void *val_a, *val_b;
	int ret = 0;

	a = *(const struct tracing_map_sort_entry **)A;
	b = *(const struct tracing_map_sort_entry **)B;

	elt_a = a->elt;
	elt_b = b->elt;

	sort_key = &elt_a->map->sort_key;

	field = &elt_a->fields[sort_key->field_idx];

	cmp_fn = field->cmp_fn;

	val_a = elt_a->key + field->offset;
	val_b = elt_b->key + field->offset;

	ret = cmp_fn(val_a, val_b);
	if (sort_key->descending)
		ret = -ret;

	return ret;
}

static void destroy_sort_entry(struct tracing_map_sort_entry *entry)
{
	if (!entry)
		return;

	if (entry->elt_copied)
		tracing_map_elt_free(entry->elt);

	kfree(entry);
}

/**
 * tracing_map_destroy_sort_entries - Destroy an array of sort entries
 * @entries: The entries to destroy
 * @n_entries: The number of entries in the array
 *
 * Destroy the elements returned by a tracing_map_sort_entries() call.
 */
void tracing_map_destroy_sort_entries(struct tracing_map_sort_entry **entries,
				      unsigned int n_entries)
{
	unsigned int i;

	for (i = 0; i < n_entries; i++)
		destroy_sort_entry(entries[i]);

	vfree(entries);
}

static struct tracing_map_sort_entry *
create_sort_entry(void *key, struct tracing_map_elt *elt)
{
	struct tracing_map_sort_entry *sort_entry;

	sort_entry = kzalloc(sizeof(*sort_entry), GFP_KERNEL);
	if (!sort_entry)
		return NULL;

	sort_entry->key = key;
	sort_entry->elt = elt;

	return sort_entry;
}

static void detect_dups(struct tracing_map_sort_entry **sort_entries,
		      int n_entries, unsigned int key_size)
{
	unsigned int total_dups = 0;
	int i;
	void *key;

	if (n_entries < 2)
		return;

	sort(sort_entries, n_entries, sizeof(struct tracing_map_sort_entry *),
	     (int (*)(const void *, const void *))cmp_entries_dup, NULL);

	key = sort_entries[0]->key;
	for (i = 1; i < n_entries; i++) {
		if (!memcmp(sort_entries[i]->key, key, key_size)) {
			total_dups++;
			continue;
		}
		key = sort_entries[i]->key;
	}

	WARN_ONCE(total_dups > 0,
		  "Duplicates detected: %d\n", total_dups);
}

static bool is_key(struct tracing_map *map, unsigned int field_idx)
{
	unsigned int i;

	for (i = 0; i < map->n_keys; i++)
		if (map->key_idx[i] == field_idx)
			return true;
	return false;
}

static void sort_secondary(struct tracing_map *map,
			   const struct tracing_map_sort_entry **entries,
			   unsigned int n_entries,
			   struct tracing_map_sort_key *primary_key,
			   struct tracing_map_sort_key *secondary_key)
{
	int (*primary_fn)(const void *, const void *);
	int (*secondary_fn)(const void *, const void *);
	unsigned i, start = 0, n_sub = 1;

	if (is_key(map, primary_key->field_idx))
		primary_fn = cmp_entries_key;
	else
		primary_fn = cmp_entries_sum;

	if (is_key(map, secondary_key->field_idx))
		secondary_fn = cmp_entries_key;
	else
		secondary_fn = cmp_entries_sum;

	for (i = 0; i < n_entries - 1; i++) {
		const struct tracing_map_sort_entry **a = &entries[i];
		const struct tracing_map_sort_entry **b = &entries[i + 1];

		if (primary_fn(a, b) == 0) {
			n_sub++;
			if (i < n_entries - 2)
				continue;
		}

		if (n_sub < 2) {
			start = i + 1;
			n_sub = 1;
			continue;
		}

		set_sort_key(map, secondary_key);
		sort(&entries[start], n_sub,
		     sizeof(struct tracing_map_sort_entry *),
		     (int (*)(const void *, const void *))secondary_fn, NULL);
		set_sort_key(map, primary_key);

		start = i + 1;
		n_sub = 1;
	}
}

/**
 * tracing_map_sort_entries - Sort the current set of tracing_map_elts in a map
 * @map: The tracing_map
 * @sort_keys: The sort key to use for sorting
 * @n_sort_keys: hitcount, always have at least one
 * @sort_entries: outval: pointer to allocated and sorted array of entries
 *
 * tracing_map_sort_entries() sorts the current set of entries in the
 * map and returns the list of tracing_map_sort_entries containing
 * them to the client in the sort_entries param.  The client can
 * access the struct tracing_map_elt element of interest directly as
 * the 'elt' field of a returned struct tracing_map_sort_entry object.
 *
 * The sort_key has only two fields: idx and descending.  'idx' refers
 * to the index of the field added via tracing_map_add_sum_field() or
 * tracing_map_add_key_field() when the tracing_map was initialized.
 * 'descending' is a flag that if set reverses the sort order, which
 * by default is ascending.
 *
 * The client should not hold on to the returned array but should use
 * it and call tracing_map_destroy_sort_entries() when done.
 *
 * Return: the number of sort_entries in the struct tracing_map_sort_entry
 * array, negative on error
 */
int tracing_map_sort_entries(struct tracing_map *map,
			     struct tracing_map_sort_key *sort_keys,
			     unsigned int n_sort_keys,
			     struct tracing_map_sort_entry ***sort_entries)
{
	int (*cmp_entries_fn)(const void *, const void *);
	struct tracing_map_sort_entry *sort_entry, **entries;
	int i, n_entries, ret;

	entries = vmalloc_array(map->max_elts, sizeof(sort_entry));
	if (!entries)
		return -ENOMEM;

	for (i = 0, n_entries = 0; i < map->map_size; i++) {
		struct tracing_map_entry *entry;

		entry = TRACING_MAP_ENTRY(map->map, i);

		if (!entry->key || !entry->val)
			continue;

		entries[n_entries] = create_sort_entry(entry->val->key,
						       entry->val);
		if (!entries[n_entries++]) {
			ret = -ENOMEM;
			goto free;
		}
	}

	if (n_entries == 0) {
		ret = 0;
		goto free;
	}

	if (n_entries == 1) {
		*sort_entries = entries;
		return 1;
	}

	detect_dups(entries, n_entries, map->key_size);

	if (is_key(map, sort_keys[0].field_idx))
		cmp_entries_fn = cmp_entries_key;
	else
		cmp_entries_fn = cmp_entries_sum;

	set_sort_key(map, &sort_keys[0]);

	sort(entries, n_entries, sizeof(struct tracing_map_sort_entry *),
	     (int (*)(const void *, const void *))cmp_entries_fn, NULL);

	if (n_sort_keys > 1)
		sort_secondary(map,
			       (const struct tracing_map_sort_entry **)entries,
			       n_entries,
			       &sort_keys[0],
			       &sort_keys[1]);

	*sort_entries = entries;

	return n_entries;
 free:
	tracing_map_destroy_sort_entries(entries, n_entries);

	return ret;
}
