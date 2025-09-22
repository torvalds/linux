/*
 * bitset.h -- Dynamic bitset.
 *
 * Copyright (c) 2001-2020, NLnet Labs. All rights reserved.
 *
 * See LICENSE for the license.
 *
 */
#ifndef BITSET_H
#define BITSET_H

#include <assert.h>
#include <limits.h>
#include <string.h>

typedef struct nsd_bitset nsd_bitset_type;

struct nsd_bitset {
	size_t size; /** Number of available bits in the set */
	unsigned char bits[];
};

size_t nsd_bitset_size(size_t bits);

void nsd_bitset_zero(struct nsd_bitset *bset);

void nsd_bitset_init(struct nsd_bitset *bset, size_t bits);

int nsd_bitset_isset(struct nsd_bitset *bset, size_t bit);

void nsd_bitset_set(struct nsd_bitset *bset, size_t bit);

void nsd_bitset_unset(struct nsd_bitset *bset, size_t bit);

void nsd_bitset_or(
	struct nsd_bitset *destset,
	struct nsd_bitset *srcset1,
	struct nsd_bitset *srcset2);

#endif /* BITSET_H */
