/* SPDX-License-Identifier: GPL-2.0-only */
#ifndef HASHTABLE_H
#define HASHTABLE_H

#include "array_size.h"
#include "list.h"

#define HASH_SIZE(name) (ARRAY_SIZE(name))

#define HASHTABLE_DECLARE(name, size)		struct hlist_head name[size]

#define HASHTABLE_DEFINE(name, size)						\
	HASHTABLE_DECLARE(name, size) =						\
			{ [0 ... ((size) - 1)] = HLIST_HEAD_INIT }

#define hash_head(table, key)		(&(table)[(key) % HASH_SIZE(table)])

/**
 * hash_add - add an object to a hashtable
 * @table: hashtable to add to
 * @node: the &struct hlist_node of the object to be added
 * @key: the key of the object to be added
 */
#define hash_add(table, node, key)						\
	hlist_add_head(node, hash_head(table, key))

/**
 * hash_for_each - iterate over a hashtable
 * @table: hashtable to iterate
 * @obj: the type * to use as a loop cursor for each entry
 * @member: the name of the hlist_node within the struct
 */
#define hash_for_each(table, obj, member)				\
	for (int _bkt = 0; _bkt < HASH_SIZE(table); _bkt++)		\
		hlist_for_each_entry(obj, &table[_bkt], member)

/**
 * hash_for_each_possible - iterate over all possible objects hashing to the
 * same bucket
 * @table: hashtable to iterate
 * @obj: the type * to use as a loop cursor for each entry
 * @member: the name of the hlist_node within the struct
 * @key: the key of the objects to iterate over
 */
#define hash_for_each_possible(table, obj, member, key)			\
	hlist_for_each_entry(obj, hash_head(table, key), member)

#endif /* HASHTABLE_H */
