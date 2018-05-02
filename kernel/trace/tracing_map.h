/* SPDX-License-Identifier: GPL-2.0 */
#ifndef __TRACING_MAP_H
#define __TRACING_MAP_H

#define TRACING_MAP_BITS_DEFAULT	11
#define TRACING_MAP_BITS_MAX		17
#define TRACING_MAP_BITS_MIN		7

#define TRACING_MAP_KEYS_MAX		3
#define TRACING_MAP_VALS_MAX		3
#define TRACING_MAP_FIELDS_MAX		(TRACING_MAP_KEYS_MAX + \
					 TRACING_MAP_VALS_MAX)
#define TRACING_MAP_VARS_MAX		16
#define TRACING_MAP_SORT_KEYS_MAX	2

typedef int (*tracing_map_cmp_fn_t) (void *val_a, void *val_b);

/*
 * This is an overview of the tracing_map data structures and how they
 * relate to the tracing_map API.  The details of the algorithms
 * aren't discussed here - this is just a general overview of the data
 * structures and how they interact with the API.
 *
 * The central data structure of the tracing_map is an initially
 * zeroed array of struct tracing_map_entry (stored in the map field
 * of struct tracing_map).  tracing_map_entry is a very simple data
 * structure containing only two fields: a 32-bit unsigned 'key'
 * variable and a pointer named 'val'.  This array of struct
 * tracing_map_entry is essentially a hash table which will be
 * modified by a single function, tracing_map_insert(), but which can
 * be traversed and read by a user at any time (though the user does
 * this indirectly via an array of tracing_map_sort_entry - see the
 * explanation of that data structure in the discussion of the
 * sorting-related data structures below).
 *
 * The central function of the tracing_map API is
 * tracing_map_insert().  tracing_map_insert() hashes the
 * arbitrarily-sized key passed into it into a 32-bit unsigned key.
 * It then uses this key, truncated to the array size, as an index
 * into the array of tracing_map_entries.  If the value of the 'key'
 * field of the tracing_map_entry found at that location is 0, then
 * that entry is considered to be free and can be claimed, by
 * replacing the 0 in the 'key' field of the tracing_map_entry with
 * the new 32-bit hashed key.  Once claimed, that tracing_map_entry's
 * 'val' field is then used to store a unique element which will be
 * forever associated with that 32-bit hashed key in the
 * tracing_map_entry.
 *
 * That unique element now in the tracing_map_entry's 'val' field is
 * an instance of tracing_map_elt, where 'elt' in the latter part of
 * that variable name is short for 'element'.  The purpose of a
 * tracing_map_elt is to hold values specific to the particular
 * 32-bit hashed key it's assocated with.  Things such as the unique
 * set of aggregated sums associated with the 32-bit hashed key, along
 * with a copy of the full key associated with the entry, and which
 * was used to produce the 32-bit hashed key.
 *
 * When tracing_map_create() is called to create the tracing map, the
 * user specifies (indirectly via the map_bits param, the details are
 * unimportant for this discussion) the maximum number of elements
 * that the map can hold (stored in the max_elts field of struct
 * tracing_map).  This is the maximum possible number of
 * tracing_map_entries in the tracing_map_entry array which can be
 * 'claimed' as described in the above discussion, and therefore is
 * also the maximum number of tracing_map_elts that can be associated
 * with the tracing_map_entry array in the tracing_map.  Because of
 * the way the insertion algorithm works, the size of the allocated
 * tracing_map_entry array is always twice the maximum number of
 * elements (2 * max_elts).  This value is stored in the map_size
 * field of struct tracing_map.
 *
 * Because tracing_map_insert() needs to work from any context,
 * including from within the memory allocation functions themselves,
 * both the tracing_map_entry array and a pool of max_elts
 * tracing_map_elts are pre-allocated before any call is made to
 * tracing_map_insert().
 *
 * The tracing_map_entry array is allocated as a single block by
 * tracing_map_create().
 *
 * Because the tracing_map_elts are much larger objects and can't
 * generally be allocated together as a single large array without
 * failure, they're allocated individually, by tracing_map_init().
 *
 * The pool of tracing_map_elts are allocated by tracing_map_init()
 * rather than by tracing_map_create() because at the time
 * tracing_map_create() is called, there isn't enough information to
 * create the tracing_map_elts.  Specifically,the user first needs to
 * tell the tracing_map implementation how many fields the
 * tracing_map_elts contain, and which types of fields they are (key
 * or sum).  The user does this via the tracing_map_add_sum_field()
 * and tracing_map_add_key_field() functions, following which the user
 * calls tracing_map_init() to finish up the tracing map setup.  The
 * array holding the pointers which make up the pre-allocated pool of
 * tracing_map_elts is allocated as a single block and is stored in
 * the elts field of struct tracing_map.
 *
 * There is also a set of structures used for sorting that might
 * benefit from some minimal explanation.
 *
 * struct tracing_map_sort_key is used to drive the sort at any given
 * time.  By 'any given time' we mean that a different
 * tracing_map_sort_key will be used at different times depending on
 * whether the sort currently being performed is a primary or a
 * secondary sort.
 *
 * The sort key is very simple, consisting of the field index of the
 * tracing_map_elt field to sort on (which the user saved when adding
 * the field), and whether the sort should be done in an ascending or
 * descending order.
 *
 * For the convenience of the sorting code, a tracing_map_sort_entry
 * is created for each tracing_map_elt, again individually allocated
 * to avoid failures that might be expected if allocated as a single
 * large array of struct tracing_map_sort_entry.
 * tracing_map_sort_entry instances are the objects expected by the
 * various internal sorting functions, and are also what the user
 * ultimately receives after calling tracing_map_sort_entries().
 * Because it doesn't make sense for users to access an unordered and
 * sparsely populated tracing_map directly, the
 * tracing_map_sort_entries() function is provided so that users can
 * retrieve a sorted list of all existing elements.  In addition to
 * the associated tracing_map_elt 'elt' field contained within the
 * tracing_map_sort_entry, which is the object of interest to the
 * user, tracing_map_sort_entry objects contain a number of additional
 * fields which are used for caching and internal purposes and can
 * safely be ignored.
*/

struct tracing_map_field {
	tracing_map_cmp_fn_t		cmp_fn;
	union {
		atomic64_t			sum;
		unsigned int			offset;
	};
};

struct tracing_map_elt {
	struct tracing_map		*map;
	struct tracing_map_field	*fields;
	atomic64_t			*vars;
	bool				*var_set;
	void				*key;
	void				*private_data;
};

struct tracing_map_entry {
	u32				key;
	struct tracing_map_elt		*val;
};

struct tracing_map_sort_key {
	unsigned int			field_idx;
	bool				descending;
};

struct tracing_map_sort_entry {
	void				*key;
	struct tracing_map_elt		*elt;
	bool				elt_copied;
	bool				dup;
};

struct tracing_map_array {
	unsigned int entries_per_page;
	unsigned int entry_size_shift;
	unsigned int entry_shift;
	unsigned int entry_mask;
	unsigned int n_pages;
	void **pages;
};

#define TRACING_MAP_ARRAY_ELT(array, idx)				\
	(array->pages[idx >> array->entry_shift] +			\
	 ((idx & array->entry_mask) << array->entry_size_shift))

#define TRACING_MAP_ENTRY(array, idx)					\
	((struct tracing_map_entry *)TRACING_MAP_ARRAY_ELT(array, idx))

#define TRACING_MAP_ELT(array, idx)					\
	((struct tracing_map_elt **)TRACING_MAP_ARRAY_ELT(array, idx))

struct tracing_map {
	unsigned int			key_size;
	unsigned int			map_bits;
	unsigned int			map_size;
	unsigned int			max_elts;
	atomic_t			next_elt;
	struct tracing_map_array	*elts;
	struct tracing_map_array	*map;
	const struct tracing_map_ops	*ops;
	void				*private_data;
	struct tracing_map_field	fields[TRACING_MAP_FIELDS_MAX];
	unsigned int			n_fields;
	int				key_idx[TRACING_MAP_KEYS_MAX];
	unsigned int			n_keys;
	struct tracing_map_sort_key	sort_key;
	unsigned int			n_vars;
	atomic64_t			hits;
	atomic64_t			drops;
};

/**
 * struct tracing_map_ops - callbacks for tracing_map
 *
 * The methods in this structure define callback functions for various
 * operations on a tracing_map or objects related to a tracing_map.
 *
 * For a detailed description of tracing_map_elt objects please see
 * the overview of tracing_map data structures at the beginning of
 * this file.
 *
 * All the methods below are optional.
 *
 * @elt_alloc: When a tracing_map_elt is allocated, this function, if
 *	defined, will be called and gives clients the opportunity to
 *	allocate additional data and attach it to the element
 *	(tracing_map_elt->private_data is meant for that purpose).
 *	Element allocation occurs before tracing begins, when the
 *	tracing_map_init() call is made by client code.
 *
 * @elt_free: When a tracing_map_elt is freed, this function is called
 *	and allows client-allocated per-element data to be freed.
 *
 * @elt_clear: This callback allows per-element client-defined data to
 *	be cleared, if applicable.
 *
 * @elt_init: This callback allows per-element client-defined data to
 *	be initialized when used i.e. when the element is actually
 *	claimed by tracing_map_insert() in the context of the map
 *	insertion.
 */
struct tracing_map_ops {
	int			(*elt_alloc)(struct tracing_map_elt *elt);
	void			(*elt_free)(struct tracing_map_elt *elt);
	void			(*elt_clear)(struct tracing_map_elt *elt);
	void			(*elt_init)(struct tracing_map_elt *elt);
};

extern struct tracing_map *
tracing_map_create(unsigned int map_bits,
		   unsigned int key_size,
		   const struct tracing_map_ops *ops,
		   void *private_data);
extern int tracing_map_init(struct tracing_map *map);

extern int tracing_map_add_sum_field(struct tracing_map *map);
extern int tracing_map_add_var(struct tracing_map *map);
extern int tracing_map_add_key_field(struct tracing_map *map,
				     unsigned int offset,
				     tracing_map_cmp_fn_t cmp_fn);

extern void tracing_map_destroy(struct tracing_map *map);
extern void tracing_map_clear(struct tracing_map *map);

extern struct tracing_map_elt *
tracing_map_insert(struct tracing_map *map, void *key);
extern struct tracing_map_elt *
tracing_map_lookup(struct tracing_map *map, void *key);

extern tracing_map_cmp_fn_t tracing_map_cmp_num(int field_size,
						int field_is_signed);
extern int tracing_map_cmp_string(void *val_a, void *val_b);
extern int tracing_map_cmp_none(void *val_a, void *val_b);

extern void tracing_map_update_sum(struct tracing_map_elt *elt,
				   unsigned int i, u64 n);
extern void tracing_map_set_var(struct tracing_map_elt *elt,
				unsigned int i, u64 n);
extern bool tracing_map_var_set(struct tracing_map_elt *elt, unsigned int i);
extern u64 tracing_map_read_sum(struct tracing_map_elt *elt, unsigned int i);
extern u64 tracing_map_read_var(struct tracing_map_elt *elt, unsigned int i);
extern u64 tracing_map_read_var_once(struct tracing_map_elt *elt, unsigned int i);

extern void tracing_map_set_field_descr(struct tracing_map *map,
					unsigned int i,
					unsigned int key_offset,
					tracing_map_cmp_fn_t cmp_fn);
extern int
tracing_map_sort_entries(struct tracing_map *map,
			 struct tracing_map_sort_key *sort_keys,
			 unsigned int n_sort_keys,
			 struct tracing_map_sort_entry ***sort_entries);

extern void
tracing_map_destroy_sort_entries(struct tracing_map_sort_entry **entries,
				 unsigned int n_entries);
#endif /* __TRACING_MAP_H */
