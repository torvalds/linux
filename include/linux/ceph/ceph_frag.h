#ifndef FS_CEPH_FRAG_H
#define FS_CEPH_FRAG_H

/*
 * "Frags" are a way to describe a subset of a 32-bit number space,
 * using a mask and a value to match against that mask.  Any given frag
 * (subset of the number space) can be partitioned into 2^n sub-frags.
 *
 * Frags are encoded into a 32-bit word:
 *   8 upper bits = "bits"
 *  24 lower bits = "value"
 * (We could go to 5+27 bits, but who cares.)
 *
 * We use the _most_ significant bits of the 24 bit value.  This makes
 * values logically sort.
 *
 * Unfortunately, because the "bits" field is still in the high bits, we
 * can't sort encoded frags numerically.  However, it does allow you
 * to feed encoded frags as values into frag_contains_value.
 */
static inline __u32 ceph_frag_make(__u32 b, __u32 v)
{
	return (b << 24) |
		(v & (0xffffffu << (24-b)) & 0xffffffu);
}
static inline __u32 ceph_frag_bits(__u32 f)
{
	return f >> 24;
}
static inline __u32 ceph_frag_value(__u32 f)
{
	return f & 0xffffffu;
}
static inline __u32 ceph_frag_mask(__u32 f)
{
	return (0xffffffu << (24-ceph_frag_bits(f))) & 0xffffffu;
}
static inline __u32 ceph_frag_mask_shift(__u32 f)
{
	return 24 - ceph_frag_bits(f);
}

static inline bool ceph_frag_contains_value(__u32 f, __u32 v)
{
	return (v & ceph_frag_mask(f)) == ceph_frag_value(f);
}

static inline __u32 ceph_frag_make_child(__u32 f, int by, int i)
{
	int newbits = ceph_frag_bits(f) + by;
	return ceph_frag_make(newbits,
			 ceph_frag_value(f) | (i << (24 - newbits)));
}
static inline bool ceph_frag_is_leftmost(__u32 f)
{
	return ceph_frag_value(f) == 0;
}
static inline bool ceph_frag_is_rightmost(__u32 f)
{
	return ceph_frag_value(f) == ceph_frag_mask(f);
}
static inline __u32 ceph_frag_next(__u32 f)
{
	return ceph_frag_make(ceph_frag_bits(f),
			 ceph_frag_value(f) + (0x1000000 >> ceph_frag_bits(f)));
}

/*
 * comparator to sort frags logically, as when traversing the
 * number space in ascending order...
 */
int ceph_frag_compare(__u32 a, __u32 b);

#endif
