/*-
 * Copyright (c) 2005 Michael Bushkov <bushman@rsu.ru>
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE AUTHOR OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 *
 * $FreeBSD$
 */

#ifndef __CACHELIB_HASHTABLE_H__
#define __CACHELIB_HASHTABLE_H__

#include <string.h>

#define HASHTABLE_INITIAL_ENTRIES_CAPACITY 8
typedef unsigned int hashtable_index_t;

/*
 * This file contains queue.h-like macro definitions for hash tables.
 * Hash table is organized as an array of the specified size of the user
 * defined (with HASTABLE_ENTRY_HEAD) structures. Each hash table
 * entry (user defined structure) stores its elements in the sorted array.
 * You can place elements into the hash table, retrieve elements with
 * specified key, traverse through all elements, and delete them.
 * New elements are placed into the hash table by using the compare and
 * hashing functions, provided by the user.
 */

/*
 * Defines the hash table entry structure, that uses specified type of
 * elements.
 */
#define HASHTABLE_ENTRY_HEAD(name, type) struct name {			\
	type	*values;						\
	size_t capacity;						\
	size_t size;							\
}

/*
 * Defines the hash table structure, which uses the specified type of entries.
 * The only restriction for entries is that is that they should have the field,
 * defined with HASHTABLE_ENTRY_HEAD macro.
 */
#define HASHTABLE_HEAD(name, entry) struct name {			\
	struct entry	*entries;					\
	size_t		entries_size;					\
}

#define HASHTABLE_ENTRIES_COUNT(table)					\
	((table)->entries_size)

/*
 * Unlike most of queue.h data types, hash tables can not be initialized
 * statically - so there is no HASHTABLE_HEAD_INITIALIZED macro.
 */
#define HASHTABLE_INIT(table, type, field, _entries_size)		\
	do {								\
		hashtable_index_t var;					\
		(table)->entries = calloc(_entries_size,		\
			sizeof(*(table)->entries));			\
		(table)->entries_size = (_entries_size);		\
		for (var = 0; var < HASHTABLE_ENTRIES_COUNT(table); ++var) {\
			(table)->entries[var].field.capacity = 		\
				HASHTABLE_INITIAL_ENTRIES_CAPACITY;	\
			(table)->entries[var].field.size = 0;		\
			(table)->entries[var].field.values = malloc(	\
				sizeof(type) *				\
				HASHTABLE_INITIAL_ENTRIES_CAPACITY);	\
			assert((table)->entries[var].field.values != NULL);\
		}							\
	} while (0)

/*
 * All initialized hashtables should be destroyed with this macro.
 */
#define HASHTABLE_DESTROY(table, field)					\
	do {								\
		hashtable_index_t var;					\
		for (var = 0; var < HASHTABLE_ENTRIES_COUNT(table); ++var) {\
			free((table)->entries[var].field.values);	\
		}							\
	} while (0)

#define HASHTABLE_GET_ENTRY(table, hash)				\
	(&((table)->entries[hash]))

/*
 * Traverses through all hash table entries
 */
#define HASHTABLE_FOREACH(table, var)					\
	for ((var) = &((table)->entries[0]);				\
		(var) < &((table)->entries[HASHTABLE_ENTRIES_COUNT(table)]);\
		++(var))

/*
 * Traverses through all elements of the specified hash table entry
 */
#define HASHTABLE_ENTRY_FOREACH(entry, field, var)			\
	for ((var) = &((entry)->field.values[0]);			\
		(var) < &((entry)->field.values[(entry)->field.size]);	\
		++(var))

#define HASHTABLE_ENTRY_CLEAR(entry, field)				\
	((entry)->field.size = 0)

#define HASHTABLE_ENTRY_SIZE(entry, field)				\
	((entry)->field.size)

#define HASHTABLE_ENTRY_CAPACITY(entry, field)				\
	((entry)->field.capacity)

#define HASHTABLE_ENTRY_CAPACITY_INCREASE(entry, field, type)		\
	do {								\
		(entry)->field.capacity *= 2;				\
		(entry)->field.values = realloc((entry)->field.values,	\
			 (entry)->field.capacity * sizeof(type));	\
	} while (0)

#define HASHTABLE_ENTRY_CAPACITY_DECREASE(entry, field, type)		\
	do {								\
		(entry)->field.capacity /= 2;				\
		(entry)->field.values = realloc((entry)->field.values,	\
			(entry)->field.capacity * sizeof(type));	\
	} while (0)

/*
 * Generates prototypes for the hash table functions
 */
#define HASHTABLE_PROTOTYPE(name, entry_, type)				\
hashtable_index_t name##_CALCULATE_HASH(struct name *, type *);		\
void name##_ENTRY_STORE(struct entry_*, type *);			\
type *name##_ENTRY_FIND(struct entry_*, type *);			\
type *name##_ENTRY_FIND_SPECIAL(struct entry_ *, type *,		\
	int (*) (const void *, const void *));				\
void name##_ENTRY_REMOVE(struct entry_*, type *);

/*
 * Generates implementations of the hash table functions
 */
#define HASHTABLE_GENERATE(name, entry_, type, field, HASH, CMP)	\
hashtable_index_t name##_CALCULATE_HASH(struct name *table, type *data)	\
{									\
									\
	return HASH(data, table->entries_size);				\
}									\
									\
void name##_ENTRY_STORE(struct entry_ *the_entry, type *data)		\
{									\
									\
	if (the_entry->field.size == the_entry->field.capacity)		\
		HASHTABLE_ENTRY_CAPACITY_INCREASE(the_entry, field, type);\
									\
	memcpy(&(the_entry->field.values[the_entry->field.size++]),	\
		data,							\
		sizeof(type));						\
	qsort(the_entry->field.values, the_entry->field.size, 		\
		sizeof(type), CMP);					\
}									\
									\
type *name##_ENTRY_FIND(struct entry_ *the_entry, type *key)		\
{									\
									\
	return ((type *)bsearch(key, the_entry->field.values,	 	\
		the_entry->field.size, sizeof(type), CMP));		\
}									\
									\
type *name##_ENTRY_FIND_SPECIAL(struct entry_ *the_entry, type *key,	\
	int (*compar) (const void *, const void *))			\
{									\
	return ((type *)bsearch(key, the_entry->field.values,	 	\
		the_entry->field.size, sizeof(type), compar));		\
}									\
									\
void name##_ENTRY_REMOVE(struct entry_ *the_entry, type *del_elm)	\
{									\
									\
	memmove(del_elm, del_elm + 1, 					\
		(&the_entry->field.values[--the_entry->field.size] - del_elm) *\
		sizeof(type));						\
}

/*
 * Macro definitions below wrap the functions, generaed with
 * HASHTABLE_GENERATE macro. You should use them and avoid using generated
 * functions directly.
 */
#define HASHTABLE_CALCULATE_HASH(name, table, data)			\
	(name##_CALCULATE_HASH((table), data))

#define HASHTABLE_ENTRY_STORE(name, entry, data)			\
	name##_ENTRY_STORE((entry), data)

#define HASHTABLE_ENTRY_FIND(name, entry, key)				\
	(name##_ENTRY_FIND((entry), (key)))

#define HASHTABLE_ENTRY_FIND_SPECIAL(name, entry, key, cmp)		\
	(name##_ENTRY_FIND_SPECIAL((entry), (key), (cmp)))

#define HASHTABLE_ENTRY_REMOVE(name, entry, del_elm)			\
	name##_ENTRY_REMOVE((entry), (del_elm))

#endif
