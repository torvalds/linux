// SPDX-License-Identifier: GPL-2.0-only

#ifndef _NFT_SET_PIPAPO_H

#include <linux/log2.h>
#include <net/ipv6.h>			/* For the maximum length of a field */

/* Count of concatenated fields depends on count of 32-bit nftables registers */
#define NFT_PIPAPO_MAX_FIELDS		NFT_REG32_COUNT

/* Restrict usage to multiple fields, make sure rbtree is used otherwise */
#define NFT_PIPAPO_MIN_FIELDS		2

/* Largest supported field size */
#define NFT_PIPAPO_MAX_BYTES		(sizeof(struct in6_addr))
#define NFT_PIPAPO_MAX_BITS		(NFT_PIPAPO_MAX_BYTES * BITS_PER_BYTE)

/* Bits to be grouped together in table buckets depending on set size */
#define NFT_PIPAPO_GROUP_BITS_INIT	NFT_PIPAPO_GROUP_BITS_SMALL_SET
#define NFT_PIPAPO_GROUP_BITS_SMALL_SET	8
#define NFT_PIPAPO_GROUP_BITS_LARGE_SET	4
#define NFT_PIPAPO_GROUP_BITS_ARE_8_OR_4				\
	BUILD_BUG_ON((NFT_PIPAPO_GROUP_BITS_SMALL_SET != 8) ||		\
		     (NFT_PIPAPO_GROUP_BITS_LARGE_SET != 4))
#define NFT_PIPAPO_GROUPS_PER_BYTE(f)	(BITS_PER_BYTE / (f)->bb)

/* If a lookup table gets bigger than NFT_PIPAPO_LT_SIZE_HIGH, switch to the
 * small group width, and switch to the big group width if the table gets
 * smaller than NFT_PIPAPO_LT_SIZE_LOW.
 *
 * Picking 2MiB as threshold (for a single table) avoids as much as possible
 * crossing page boundaries on most architectures (x86-64 and MIPS huge pages,
 * ARMv7 supersections, POWER "large" pages, SPARC Level 1 regions, etc.), which
 * keeps performance nice in case kvmalloc() gives us non-contiguous areas.
 */
#define NFT_PIPAPO_LT_SIZE_THRESHOLD	(1 << 21)
#define NFT_PIPAPO_LT_SIZE_HYSTERESIS	(1 << 16)
#define NFT_PIPAPO_LT_SIZE_HIGH		NFT_PIPAPO_LT_SIZE_THRESHOLD
#define NFT_PIPAPO_LT_SIZE_LOW		NFT_PIPAPO_LT_SIZE_THRESHOLD -	\
					NFT_PIPAPO_LT_SIZE_HYSTERESIS

/* Fields are padded to 32 bits in input registers */
#define NFT_PIPAPO_GROUPS_PADDED_SIZE(f)				\
	(round_up((f)->groups / NFT_PIPAPO_GROUPS_PER_BYTE(f), sizeof(u32)))
#define NFT_PIPAPO_GROUPS_PADDING(f)					\
	(NFT_PIPAPO_GROUPS_PADDED_SIZE(f) - (f)->groups /		\
					    NFT_PIPAPO_GROUPS_PER_BYTE(f))

/* Number of buckets given by 2 ^ n, with n bucket bits */
#define NFT_PIPAPO_BUCKETS(bb)		(1 << (bb))

/* Each n-bit range maps to up to n * 2 rules */
#define NFT_PIPAPO_MAP_NBITS		(const_ilog2(NFT_PIPAPO_MAX_BITS * 2))

/* Use the rest of mapping table buckets for rule indices, but it makes no sense
 * to exceed 32 bits
 */
#if BITS_PER_LONG == 64
#define NFT_PIPAPO_MAP_TOBITS		32
#else
#define NFT_PIPAPO_MAP_TOBITS		(BITS_PER_LONG - NFT_PIPAPO_MAP_NBITS)
#endif

/* ...which gives us the highest allowed index for a rule */
#define NFT_PIPAPO_RULE0_MAX		((1UL << (NFT_PIPAPO_MAP_TOBITS - 1)) \
					- (1UL << NFT_PIPAPO_MAP_NBITS))

/* Definitions for vectorised implementations */
#ifdef NFT_PIPAPO_ALIGN
#define NFT_PIPAPO_ALIGN_HEADROOM					\
	(NFT_PIPAPO_ALIGN - ARCH_KMALLOC_MINALIGN)
#define NFT_PIPAPO_LT_ALIGN(lt)		(PTR_ALIGN((lt), NFT_PIPAPO_ALIGN))
#define NFT_PIPAPO_LT_ASSIGN(field, x)					\
	do {								\
		(field)->lt_aligned = NFT_PIPAPO_LT_ALIGN(x);		\
		(field)->lt = (x);					\
	} while (0)
#else
#define NFT_PIPAPO_ALIGN_HEADROOM	0
#define NFT_PIPAPO_LT_ALIGN(lt)		(lt)
#define NFT_PIPAPO_LT_ASSIGN(field, x)	((field)->lt = (x))
#endif /* NFT_PIPAPO_ALIGN */

#define nft_pipapo_for_each_field(field, index, match)		\
	for ((field) = (match)->f, (index) = 0;			\
	     (index) < (match)->field_count;			\
	     (index)++, (field)++)

/**
 * union nft_pipapo_map_bucket - Bucket of mapping table
 * @to:		First rule number (in next field) this rule maps to
 * @n:		Number of rules (in next field) this rule maps to
 * @e:		If there's no next field, pointer to element this rule maps to
 */
union nft_pipapo_map_bucket {
	struct {
#if BITS_PER_LONG == 64
		static_assert(NFT_PIPAPO_MAP_TOBITS <= 32);
		u32 to;

		static_assert(NFT_PIPAPO_MAP_NBITS <= 32);
		u32 n;
#else
		unsigned long to:NFT_PIPAPO_MAP_TOBITS;
		unsigned long  n:NFT_PIPAPO_MAP_NBITS;
#endif
	};
	struct nft_pipapo_elem *e;
};

/**
 * struct nft_pipapo_field - Lookup, mapping tables and related data for a field
 * @groups:	Amount of bit groups
 * @rules:	Number of inserted rules
 * @bsize:	Size of each bucket in lookup table, in longs
 * @bb:		Number of bits grouped together in lookup table buckets
 * @lt:		Lookup table: 'groups' rows of buckets
 * @lt_aligned:	Version of @lt aligned to NFT_PIPAPO_ALIGN bytes
 * @mt:		Mapping table: one bucket per rule
 */
struct nft_pipapo_field {
	int groups;
	unsigned long rules;
	size_t bsize;
	int bb;
#ifdef NFT_PIPAPO_ALIGN
	unsigned long *lt_aligned;
#endif
	unsigned long *lt;
	union nft_pipapo_map_bucket *mt;
};

/**
 * struct nft_pipapo_match - Data used for lookup and matching
 * @field_count		Amount of fields in set
 * @scratch:		Preallocated per-CPU maps for partial matching results
 * @scratch_aligned:	Version of @scratch aligned to NFT_PIPAPO_ALIGN bytes
 * @bsize_max:		Maximum lookup table bucket size of all fields, in longs
 * @rcu			Matching data is swapped on commits
 * @f:			Fields, with lookup and mapping tables
 */
struct nft_pipapo_match {
	int field_count;
#ifdef NFT_PIPAPO_ALIGN
	unsigned long * __percpu *scratch_aligned;
#endif
	unsigned long * __percpu *scratch;
	size_t bsize_max;
	struct rcu_head rcu;
	struct nft_pipapo_field f[] __counted_by(field_count);
};

/**
 * struct nft_pipapo - Representation of a set
 * @match:	Currently in-use matching data
 * @clone:	Copy where pending insertions and deletions are kept
 * @width:	Total bytes to be matched for one packet, including padding
 * @dirty:	Working copy has pending insertions or deletions
 * @last_gc:	Timestamp of last garbage collection run, jiffies
 */
struct nft_pipapo {
	struct nft_pipapo_match __rcu *match;
	struct nft_pipapo_match *clone;
	int width;
	bool dirty;
	unsigned long last_gc;
};

struct nft_pipapo_elem;

/**
 * struct nft_pipapo_elem - API-facing representation of single set element
 * @priv:	element placeholder
 * @ext:	nftables API extensions
 */
struct nft_pipapo_elem {
	struct nft_elem_priv	priv;
	struct nft_set_ext	ext;
};

int pipapo_refill(unsigned long *map, int len, int rules, unsigned long *dst,
		  union nft_pipapo_map_bucket *mt, bool match_only);

/**
 * pipapo_and_field_buckets_4bit() - Intersect 4-bit buckets
 * @f:		Field including lookup table
 * @dst:	Area to store result
 * @data:	Input data selecting table buckets
 */
static inline void pipapo_and_field_buckets_4bit(struct nft_pipapo_field *f,
						 unsigned long *dst,
						 const u8 *data)
{
	unsigned long *lt = NFT_PIPAPO_LT_ALIGN(f->lt);
	int group;

	for (group = 0; group < f->groups; group += BITS_PER_BYTE / 4, data++) {
		u8 v;

		v = *data >> 4;
		__bitmap_and(dst, dst, lt + v * f->bsize,
			     f->bsize * BITS_PER_LONG);
		lt += f->bsize * NFT_PIPAPO_BUCKETS(4);

		v = *data & 0x0f;
		__bitmap_and(dst, dst, lt + v * f->bsize,
			     f->bsize * BITS_PER_LONG);
		lt += f->bsize * NFT_PIPAPO_BUCKETS(4);
	}
}

/**
 * pipapo_and_field_buckets_8bit() - Intersect 8-bit buckets
 * @f:		Field including lookup table
 * @dst:	Area to store result
 * @data:	Input data selecting table buckets
 */
static inline void pipapo_and_field_buckets_8bit(struct nft_pipapo_field *f,
						 unsigned long *dst,
						 const u8 *data)
{
	unsigned long *lt = NFT_PIPAPO_LT_ALIGN(f->lt);
	int group;

	for (group = 0; group < f->groups; group++, data++) {
		__bitmap_and(dst, dst, lt + *data * f->bsize,
			     f->bsize * BITS_PER_LONG);
		lt += f->bsize * NFT_PIPAPO_BUCKETS(8);
	}
}

/**
 * pipapo_estimate_size() - Estimate worst-case for set size
 * @desc:	Set description, element count and field description used here
 *
 * The size for this set type can vary dramatically, as it depends on the number
 * of rules (composing netmasks) the entries expand to. We compute the worst
 * case here.
 *
 * In general, for a non-ranged entry or a single composing netmask, we need
 * one bit in each of the sixteen NFT_PIPAPO_BUCKETS, for each 4-bit group (that
 * is, each input bit needs four bits of matching data), plus a bucket in the
 * mapping table for each field.
 *
 * Return: worst-case set size in bytes, 0 on any overflow
 */
static u64 pipapo_estimate_size(const struct nft_set_desc *desc)
{
	unsigned long entry_size;
	u64 size;
	int i;

	for (i = 0, entry_size = 0; i < desc->field_count; i++) {
		unsigned long rules;

		if (desc->field_len[i] > NFT_PIPAPO_MAX_BYTES)
			return 0;

		/* Worst-case ranges for each concatenated field: each n-bit
		 * field can expand to up to n * 2 rules in each bucket, and
		 * each rule also needs a mapping bucket.
		 */
		rules = ilog2(desc->field_len[i] * BITS_PER_BYTE) * 2;
		entry_size += rules *
			      NFT_PIPAPO_BUCKETS(NFT_PIPAPO_GROUP_BITS_INIT) /
			      BITS_PER_BYTE;
		entry_size += rules * sizeof(union nft_pipapo_map_bucket);
	}

	/* Rules in lookup and mapping tables are needed for each entry */
	size = desc->size * entry_size;
	if (size && div_u64(size, desc->size) != entry_size)
		return 0;

	size += sizeof(struct nft_pipapo) + sizeof(struct nft_pipapo_match) * 2;

	size += sizeof(struct nft_pipapo_field) * desc->field_count;

	return size;
}

#endif /* _NFT_SET_PIPAPO_H */
