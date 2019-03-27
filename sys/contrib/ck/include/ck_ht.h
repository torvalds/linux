/*
 * Copyright 2012-2015 Samy Al Bahra.
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
 */

#ifndef CK_HT_H
#define CK_HT_H

#include <ck_pr.h>

#define CK_F_HT
#if defined(CK_F_PR_LOAD_64) && defined(CK_F_PR_STORE_64)
#define CK_HT_TYPE uint64_t
#define CK_HT_TYPE_LOAD		ck_pr_load_64
#define CK_HT_TYPE_STORE 	ck_pr_store_64
#define CK_HT_TYPE_MAX		UINT64_MAX
#else
#define CK_HT_TYPE uint32_t
#define CK_HT_TYPE_LOAD		ck_pr_load_32
#define CK_HT_TYPE_STORE	ck_pr_store_32
#define CK_HT_TYPE_MAX		UINT32_MAX
#endif


#include <ck_cc.h>
#include <ck_malloc.h>
#include <ck_md.h>
#include <ck_stdint.h>
#include <ck_stdbool.h>
#include <ck_stddef.h>

struct ck_ht_hash {
	uint64_t value;
};
typedef struct ck_ht_hash ck_ht_hash_t;

#define CK_HT_MODE_DIRECT	1U
#define CK_HT_MODE_BYTESTRING	2U
#define CK_HT_WORKLOAD_DELETE	4U

#if defined(CK_MD_POINTER_PACK_ENABLE) && defined(CK_MD_VMA_BITS)
#define CK_HT_PP
#define CK_HT_KEY_LENGTH ((sizeof(void *) * 8) - CK_MD_VMA_BITS)
#define CK_HT_KEY_MASK   ((1U << CK_HT_KEY_LENGTH) - 1)
#else
#define CK_HT_KEY_LENGTH 65535U
#endif

struct ck_ht_entry {
#ifdef CK_HT_PP
	uintptr_t key;
	uintptr_t value CK_CC_PACKED;
} CK_CC_ALIGN(16);
#else
	uintptr_t key;
	uintptr_t value;
	CK_HT_TYPE key_length;
	CK_HT_TYPE hash;
} CK_CC_ALIGN(32);
#endif
typedef struct ck_ht_entry ck_ht_entry_t;

/*
 * The user is free to define their own stub values.
 */
#ifndef CK_HT_KEY_EMPTY
#define CK_HT_KEY_EMPTY		((uintptr_t)0)
#endif

#ifndef CK_HT_KEY_TOMBSTONE
#define CK_HT_KEY_TOMBSTONE	(~CK_HT_KEY_EMPTY)
#endif

/*
 * Hash callback function. First argument is updated to contain a hash value,
 * second argument is the key, third argument is key length and final argument
 * is the hash table seed value.
 */
typedef void ck_ht_hash_cb_t(ck_ht_hash_t *, const void *, size_t, uint64_t);

struct ck_ht_map;
struct ck_ht {
	struct ck_malloc *m;
	struct ck_ht_map *map;
	unsigned int mode;
	uint64_t seed;
	ck_ht_hash_cb_t *h;
};
typedef struct ck_ht ck_ht_t;

struct ck_ht_stat {
	uint64_t probe_maximum;
	uint64_t n_entries;
};

struct ck_ht_iterator {
	struct ck_ht_entry *current;
	uint64_t offset;
};
typedef struct ck_ht_iterator ck_ht_iterator_t;

#define CK_HT_ITERATOR_INITIALIZER { NULL, 0 }

CK_CC_INLINE static void
ck_ht_iterator_init(struct ck_ht_iterator *iterator)
{

	iterator->current = NULL;
	iterator->offset = 0;
	return;
}

CK_CC_INLINE static bool
ck_ht_entry_empty(ck_ht_entry_t *entry)
{

	return entry->key == CK_HT_KEY_EMPTY;
}

CK_CC_INLINE static void
ck_ht_entry_key_set_direct(ck_ht_entry_t *entry, uintptr_t key)
{

	entry->key = key;
	return;
}

CK_CC_INLINE static void
ck_ht_entry_key_set(ck_ht_entry_t *entry, const void *key, uint16_t key_length)
{

#ifdef CK_HT_PP
	entry->key = (uintptr_t)key | ((uintptr_t)key_length << CK_MD_VMA_BITS);
#else
	entry->key = (uintptr_t)key;
	entry->key_length = key_length;
#endif

	return;
}

CK_CC_INLINE static void *
ck_ht_entry_key(ck_ht_entry_t *entry)
{

#ifdef CK_HT_PP
	return (void *)(entry->key & (((uintptr_t)1 << CK_MD_VMA_BITS) - 1));
#else
	return (void *)entry->key;
#endif
}

CK_CC_INLINE static uint16_t
ck_ht_entry_key_length(ck_ht_entry_t *entry)
{

#ifdef CK_HT_PP
	return entry->key >> CK_MD_VMA_BITS;
#else
	return entry->key_length;
#endif
}

CK_CC_INLINE static void *
ck_ht_entry_value(ck_ht_entry_t *entry)
{

#ifdef CK_HT_PP
	return (void *)(entry->value & (((uintptr_t)1 << CK_MD_VMA_BITS) - 1));
#else
	return (void *)entry->value;
#endif
}

CK_CC_INLINE static void
ck_ht_entry_set(struct ck_ht_entry *entry,
		ck_ht_hash_t h,
		const void *key,
		uint16_t key_length,
		const void *value)
{

#ifdef CK_HT_PP
	entry->key = (uintptr_t)key | ((uintptr_t)key_length << CK_MD_VMA_BITS);
	entry->value = (uintptr_t)value | ((uintptr_t)(h.value >> 32) << CK_MD_VMA_BITS);
#else
	entry->key = (uintptr_t)key;
	entry->value = (uintptr_t)value;
	entry->key_length = key_length;
	entry->hash = h.value;
#endif

	return;
}

CK_CC_INLINE static void
ck_ht_entry_set_direct(struct ck_ht_entry *entry,
		       ck_ht_hash_t h,
		       uintptr_t key,
		       uintptr_t value)
{

	entry->key = key;
	entry->value = value;

#ifndef CK_HT_PP
	entry->hash = h.value;
#else
	(void)h;
#endif
	return;
}

CK_CC_INLINE static uintptr_t
ck_ht_entry_key_direct(ck_ht_entry_t *entry)
{

	return entry->key;
}

CK_CC_INLINE static uintptr_t
ck_ht_entry_value_direct(ck_ht_entry_t *entry)
{

	return entry->value;
}

/*
 * Iteration must occur without any concurrent mutations on
 * the hash table.
 */
bool ck_ht_next(ck_ht_t *, ck_ht_iterator_t *, ck_ht_entry_t **entry);

void ck_ht_stat(ck_ht_t *, struct ck_ht_stat *);
void ck_ht_hash(ck_ht_hash_t *, ck_ht_t *, const void *, uint16_t);
void ck_ht_hash_direct(ck_ht_hash_t *, ck_ht_t *, uintptr_t);
bool ck_ht_init(ck_ht_t *, unsigned int, ck_ht_hash_cb_t *,
    struct ck_malloc *, CK_HT_TYPE, uint64_t);
void ck_ht_destroy(ck_ht_t *);
bool ck_ht_set_spmc(ck_ht_t *, ck_ht_hash_t, ck_ht_entry_t *);
bool ck_ht_put_spmc(ck_ht_t *, ck_ht_hash_t, ck_ht_entry_t *);
bool ck_ht_get_spmc(ck_ht_t *, ck_ht_hash_t, ck_ht_entry_t *);
bool ck_ht_gc(struct ck_ht *, unsigned long, unsigned long);
bool ck_ht_grow_spmc(ck_ht_t *, CK_HT_TYPE);
bool ck_ht_remove_spmc(ck_ht_t *, ck_ht_hash_t, ck_ht_entry_t *);
bool ck_ht_reset_spmc(ck_ht_t *);
bool ck_ht_reset_size_spmc(ck_ht_t *, CK_HT_TYPE);
CK_HT_TYPE ck_ht_count(ck_ht_t *);

#endif /* CK_HT_H */
