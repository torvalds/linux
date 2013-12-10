/* Generic associative array implementation.
 *
 * See Documentation/assoc_array.txt for information.
 *
 * Copyright (C) 2013 Red Hat, Inc. All Rights Reserved.
 * Written by David Howells (dhowells@redhat.com)
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public Licence
 * as published by the Free Software Foundation; either version
 * 2 of the Licence, or (at your option) any later version.
 */

#ifndef _LINUX_ASSOC_ARRAY_H
#define _LINUX_ASSOC_ARRAY_H

#ifdef CONFIG_ASSOCIATIVE_ARRAY

#include <linux/types.h>

#define ASSOC_ARRAY_KEY_CHUNK_SIZE BITS_PER_LONG /* Key data retrieved in chunks of this size */

/*
 * Generic associative array.
 */
struct assoc_array {
	struct assoc_array_ptr	*root;		/* The node at the root of the tree */
	unsigned long		nr_leaves_on_tree;
};

/*
 * Operations on objects and index keys for use by array manipulation routines.
 */
struct assoc_array_ops {
	/* Method to get a chunk of an index key from caller-supplied data */
	unsigned long (*get_key_chunk)(const void *index_key, int level);

	/* Method to get a piece of an object's index key */
	unsigned long (*get_object_key_chunk)(const void *object, int level);

	/* Is this the object we're looking for? */
	bool (*compare_object)(const void *object, const void *index_key);

	/* How different are two objects, to a bit position in their keys? (or
	 * -1 if they're the same)
	 */
	int (*diff_objects)(const void *a, const void *b);

	/* Method to free an object. */
	void (*free_object)(void *object);
};

/*
 * Access and manipulation functions.
 */
struct assoc_array_edit;

static inline void assoc_array_init(struct assoc_array *array)
{
	array->root = NULL;
	array->nr_leaves_on_tree = 0;
}

extern int assoc_array_iterate(const struct assoc_array *array,
			       int (*iterator)(const void *object,
					       void *iterator_data),
			       void *iterator_data);
extern void *assoc_array_find(const struct assoc_array *array,
			      const struct assoc_array_ops *ops,
			      const void *index_key);
extern void assoc_array_destroy(struct assoc_array *array,
				const struct assoc_array_ops *ops);
extern struct assoc_array_edit *assoc_array_insert(struct assoc_array *array,
						   const struct assoc_array_ops *ops,
						   const void *index_key,
						   void *object);
extern void assoc_array_insert_set_object(struct assoc_array_edit *edit,
					  void *object);
extern struct assoc_array_edit *assoc_array_delete(struct assoc_array *array,
						   const struct assoc_array_ops *ops,
						   const void *index_key);
extern struct assoc_array_edit *assoc_array_clear(struct assoc_array *array,
						  const struct assoc_array_ops *ops);
extern void assoc_array_apply_edit(struct assoc_array_edit *edit);
extern void assoc_array_cancel_edit(struct assoc_array_edit *edit);
extern int assoc_array_gc(struct assoc_array *array,
			  const struct assoc_array_ops *ops,
			  bool (*iterator)(void *object, void *iterator_data),
			  void *iterator_data);

#endif /* CONFIG_ASSOCIATIVE_ARRAY */
#endif /* _LINUX_ASSOC_ARRAY_H */
