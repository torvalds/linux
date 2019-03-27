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

#ifndef CK_RHS_H
#define CK_RHS_H

#include <ck_cc.h>
#include <ck_malloc.h>
#include <ck_md.h>
#include <ck_pr.h>
#include <ck_stdint.h>
#include <ck_stdbool.h>
#include <ck_stddef.h>

/*
 * Indicates a single-writer many-reader workload. Mutually
 * exclusive with CK_RHS_MODE_MPMC
 */
#define CK_RHS_MODE_SPMC		1

/*
 * Indicates that values to be stored are not pointers but
 * values. Allows for full precision. Mutually exclusive
 * with CK_RHS_MODE_OBJECT.
 */
#define CK_RHS_MODE_DIRECT	2

/*
 * Indicates that the values to be stored are pointers.
 * Allows for space optimizations in the presence of pointer
 * packing. Mutually exclusive with CK_RHS_MODE_DIRECT.
 */
#define CK_RHS_MODE_OBJECT	8

/*
 * Indicated that the load is read-mostly, so get should be optimized
 * over put and delete
 */
#define CK_RHS_MODE_READ_MOSTLY	16

/* Currently unsupported. */
#define CK_RHS_MODE_MPMC    (void)

/*
 * Hash callback function.
 */
typedef unsigned long ck_rhs_hash_cb_t(const void *, unsigned long);

/*
 * Returns pointer to object if objects are equivalent.
 */
typedef bool ck_rhs_compare_cb_t(const void *, const void *);

#if defined(CK_MD_POINTER_PACK_ENABLE) && defined(CK_MD_VMA_BITS)
#define CK_RHS_PP
#define CK_RHS_KEY_MASK ((1U << ((sizeof(void *) * 8) - CK_MD_VMA_BITS)) - 1)
#endif

struct ck_rhs_map;
struct ck_rhs {
	struct ck_malloc *m;
	struct ck_rhs_map *map;
	unsigned int mode;
	unsigned int load_factor;
	unsigned long seed;
	ck_rhs_hash_cb_t *hf;
	ck_rhs_compare_cb_t *compare;
};
typedef struct ck_rhs ck_rhs_t;

struct ck_rhs_stat {
	unsigned long n_entries;
	unsigned int probe_maximum;
};

struct ck_rhs_iterator {
	void **cursor;
	unsigned long offset;
};
typedef struct ck_rhs_iterator ck_rhs_iterator_t;

#define CK_RHS_ITERATOR_INITIALIZER { NULL, 0 }

/* Convenience wrapper to table hash function. */
#define CK_RHS_HASH(T, F, K) F((K), (T)->seed)

typedef void *ck_rhs_apply_fn_t(void *, void *);
bool ck_rhs_apply(ck_rhs_t *, unsigned long, const void *, ck_rhs_apply_fn_t *, void *);
void ck_rhs_iterator_init(ck_rhs_iterator_t *);
bool ck_rhs_next(ck_rhs_t *, ck_rhs_iterator_t *, void **);
bool ck_rhs_move(ck_rhs_t *, ck_rhs_t *, ck_rhs_hash_cb_t *,
    ck_rhs_compare_cb_t *, struct ck_malloc *);
bool ck_rhs_init(ck_rhs_t *, unsigned int, ck_rhs_hash_cb_t *,
    ck_rhs_compare_cb_t *, struct ck_malloc *, unsigned long, unsigned long);
void ck_rhs_destroy(ck_rhs_t *);
void *ck_rhs_get(ck_rhs_t *, unsigned long, const void *);
bool ck_rhs_put(ck_rhs_t *, unsigned long, const void *);
bool ck_rhs_put_unique(ck_rhs_t *, unsigned long, const void *);
bool ck_rhs_set(ck_rhs_t *, unsigned long, const void *, void **);
bool ck_rhs_fas(ck_rhs_t *, unsigned long, const void *, void **);
void *ck_rhs_remove(ck_rhs_t *, unsigned long, const void *);
bool ck_rhs_grow(ck_rhs_t *, unsigned long);
bool ck_rhs_rebuild(ck_rhs_t *);
bool ck_rhs_gc(ck_rhs_t *);
unsigned long ck_rhs_count(ck_rhs_t *);
bool ck_rhs_reset(ck_rhs_t *);
bool ck_rhs_reset_size(ck_rhs_t *, unsigned long);
void ck_rhs_stat(ck_rhs_t *, struct ck_rhs_stat *);
bool ck_rhs_set_load_factor(ck_rhs_t *, unsigned int);

#endif /* CK_RHS_H */
