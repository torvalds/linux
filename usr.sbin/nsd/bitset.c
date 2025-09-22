/*
 * bitset.h -- Dynamic bitset.
 *
 * Copyright (c) 2001-2020, NLnet Labs. All rights reserved.
 *
 * See LICENSE for the license.
 *
 */
#include "config.h"
#include "bitset.h"

#include <assert.h>
#include <limits.h>
#include <string.h>

size_t nsd_bitset_size(size_t bits)
{
	if(bits == 0)
		bits++;

	return (bits / CHAR_BIT) + ((bits % CHAR_BIT) != 0) + sizeof(size_t);
}

void nsd_bitset_zero(struct nsd_bitset *bset)
{
	size_t sz;

	assert(bset != NULL);

	sz = nsd_bitset_size(bset->size) - sizeof(bset->size);
	assert(sz > 0);
	memset(bset->bits, 0, sz);
}

void nsd_bitset_init(struct nsd_bitset *bset, size_t bits)
{
	assert(bset != NULL);
	if (bits == 0)
		bits++;

	bset->size = bits;
	nsd_bitset_zero(bset);
}

int nsd_bitset_isset(struct nsd_bitset *bset, size_t bit)
{
	assert(bset != NULL);
	if(bit >= bset->size)
		return 0;

	return (bset->bits[ (bit / CHAR_BIT) ] & (1 << (bit % CHAR_BIT))) != 0;
}

void nsd_bitset_set(struct nsd_bitset *bset, size_t bit)
{
	assert(bset != NULL);
	assert(bset->size > bit);
	bset->bits[ (bit / CHAR_BIT) ] |= (1 << (bit % CHAR_BIT));
}

void nsd_bitset_unset(struct nsd_bitset *bset, size_t bit)
{
	assert(bset != NULL);
	assert(bset->size > bit);
	bset->bits[ (bit / CHAR_BIT) ] &= ~(1 << (bit % CHAR_BIT));
}

void nsd_bitset_or(
	struct nsd_bitset *destset,
	struct nsd_bitset *srcset1,
	struct nsd_bitset *srcset2)
{
	size_t i, n, size, bytes;
	unsigned char bits;
	unsigned int mask;

	assert(destset != NULL);
	assert(srcset1 != NULL);
	assert(srcset2 != NULL);

	size = destset->size;
	bytes = (size / CHAR_BIT) + ((size % CHAR_BIT) != 0);

	for(i = 0; i < bytes; i++) {
		bits = 0;

		n = (srcset1->size / CHAR_BIT);
		if (n > i) {
			bits |= srcset1->bits[i];
		} else {
			n += ((srcset1->size % CHAR_BIT) != 0);
			mask = (1 << ((srcset1->size % CHAR_BIT) + 1)) - 1;
			if (n > i) {
				bits |= (srcset1->bits[i] & mask);
			}
		}
		n = (srcset2->size / CHAR_BIT);
		if (n > i) {
			bits |= srcset2->bits[i];
		} else {
			n += ((srcset2->size % CHAR_BIT) != 0);
			mask = (1 << ((srcset2->size % CHAR_BIT) + 1)) - 1;
			if (n > i) {
				bits |= (srcset2->bits[i] & mask);
			}
		}
		destset->bits[i] = bits;
	}
}
