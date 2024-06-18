/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _BCACHEFS_BKEY_CMP_H
#define _BCACHEFS_BKEY_CMP_H

#include "bkey.h"

#ifdef CONFIG_X86_64
static inline int __bkey_cmp_bits(const u64 *l, const u64 *r,
				  unsigned nr_key_bits)
{
	long d0, d1, d2, d3;
	int cmp;

	/* we shouldn't need asm for this, but gcc is being retarded: */

	asm(".intel_syntax noprefix;"
	    "xor eax, eax;"
	    "xor edx, edx;"
	    "1:;"
	    "mov r8, [rdi];"
	    "mov r9, [rsi];"
	    "sub ecx, 64;"
	    "jl 2f;"

	    "cmp r8, r9;"
	    "jnz 3f;"

	    "lea rdi, [rdi - 8];"
	    "lea rsi, [rsi - 8];"
	    "jmp 1b;"

	    "2:;"
	    "not ecx;"
	    "shr r8, 1;"
	    "shr r9, 1;"
	    "shr r8, cl;"
	    "shr r9, cl;"
	    "cmp r8, r9;"

	    "3:\n"
	    "seta al;"
	    "setb dl;"
	    "sub eax, edx;"
	    ".att_syntax prefix;"
	    : "=&D" (d0), "=&S" (d1), "=&d" (d2), "=&c" (d3), "=&a" (cmp)
	    : "0" (l), "1" (r), "3" (nr_key_bits)
	    : "r8", "r9", "cc", "memory");

	return cmp;
}
#else
static inline int __bkey_cmp_bits(const u64 *l, const u64 *r,
				  unsigned nr_key_bits)
{
	u64 l_v, r_v;

	if (!nr_key_bits)
		return 0;

	/* for big endian, skip past header */
	nr_key_bits += high_bit_offset;
	l_v = *l & (~0ULL >> high_bit_offset);
	r_v = *r & (~0ULL >> high_bit_offset);

	while (1) {
		if (nr_key_bits < 64) {
			l_v >>= 64 - nr_key_bits;
			r_v >>= 64 - nr_key_bits;
			nr_key_bits = 0;
		} else {
			nr_key_bits -= 64;
		}

		if (!nr_key_bits || l_v != r_v)
			break;

		l = next_word(l);
		r = next_word(r);

		l_v = *l;
		r_v = *r;
	}

	return cmp_int(l_v, r_v);
}
#endif

static inline __pure __flatten
int __bch2_bkey_cmp_packed_format_checked_inlined(const struct bkey_packed *l,
					  const struct bkey_packed *r,
					  const struct btree *b)
{
	const struct bkey_format *f = &b->format;
	int ret;

	EBUG_ON(!bkey_packed(l) || !bkey_packed(r));
	EBUG_ON(b->nr_key_bits != bkey_format_key_bits(f));

	ret = __bkey_cmp_bits(high_word(f, l),
			      high_word(f, r),
			      b->nr_key_bits);

	EBUG_ON(ret != bpos_cmp(bkey_unpack_pos(b, l),
				bkey_unpack_pos(b, r)));
	return ret;
}

static inline __pure __flatten
int bch2_bkey_cmp_packed_inlined(const struct btree *b,
			 const struct bkey_packed *l,
			 const struct bkey_packed *r)
{
	struct bkey unpacked;

	if (likely(bkey_packed(l) && bkey_packed(r)))
		return __bch2_bkey_cmp_packed_format_checked_inlined(l, r, b);

	if (bkey_packed(l)) {
		__bkey_unpack_key_format_checked(b, &unpacked, l);
		l = (void *) &unpacked;
	} else if (bkey_packed(r)) {
		__bkey_unpack_key_format_checked(b, &unpacked, r);
		r = (void *) &unpacked;
	}

	return bpos_cmp(((struct bkey *) l)->p, ((struct bkey *) r)->p);
}

#endif /* _BCACHEFS_BKEY_CMP_H */
