#ifndef __LINKMODE_H
#define __LINKMODE_H

#include <linux/bitmap.h>
#include <linux/ethtool.h>
#include <uapi/linux/ethtool.h>

static inline void linkmode_zero(unsigned long *dst)
{
	bitmap_zero(dst, __ETHTOOL_LINK_MODE_MASK_NBITS);
}

static inline void linkmode_copy(unsigned long *dst, const unsigned long *src)
{
	bitmap_copy(dst, src, __ETHTOOL_LINK_MODE_MASK_NBITS);
}

static inline void linkmode_and(unsigned long *dst, const unsigned long *a,
				const unsigned long *b)
{
	bitmap_and(dst, a, b, __ETHTOOL_LINK_MODE_MASK_NBITS);
}

static inline void linkmode_or(unsigned long *dst, const unsigned long *a,
				const unsigned long *b)
{
	bitmap_or(dst, a, b, __ETHTOOL_LINK_MODE_MASK_NBITS);
}

static inline bool linkmode_empty(const unsigned long *src)
{
	return bitmap_empty(src, __ETHTOOL_LINK_MODE_MASK_NBITS);
}

static inline int linkmode_andnot(unsigned long *dst, const unsigned long *src1,
				  const unsigned long *src2)
{
	return bitmap_andnot(dst, src1, src2,  __ETHTOOL_LINK_MODE_MASK_NBITS);
}

static inline void linkmode_set_bit(int nr, volatile unsigned long *addr)
{
	__set_bit(nr, addr);
}

static inline void linkmode_clear_bit(int nr, volatile unsigned long *addr)
{
	__clear_bit(nr, addr);
}

static inline void linkmode_mod_bit(int nr, volatile unsigned long *addr,
				    int set)
{
	if (set)
		linkmode_set_bit(nr, addr);
	else
		linkmode_clear_bit(nr, addr);
}

static inline int linkmode_test_bit(int nr, const volatile unsigned long *addr)
{
	return test_bit(nr, addr);
}

static inline void linkmode_set_bit_array(const int *array, int array_size,
					  unsigned long *addr)
{
	int i;

	for (i = 0; i < array_size; i++)
		linkmode_set_bit(array[i], addr);
}

static inline int linkmode_equal(const unsigned long *src1,
				 const unsigned long *src2)
{
	return bitmap_equal(src1, src2, __ETHTOOL_LINK_MODE_MASK_NBITS);
}

static inline int linkmode_intersects(const unsigned long *src1,
				      const unsigned long *src2)
{
	return bitmap_intersects(src1, src2, __ETHTOOL_LINK_MODE_MASK_NBITS);
}

static inline int linkmode_subset(const unsigned long *src1,
				  const unsigned long *src2)
{
	return bitmap_subset(src1, src2, __ETHTOOL_LINK_MODE_MASK_NBITS);
}

void linkmode_resolve_pause(const unsigned long *local_adv,
			    const unsigned long *partner_adv,
			    bool *tx_pause, bool *rx_pause);

void linkmode_set_pause(unsigned long *advertisement, bool tx, bool rx);

#endif /* __LINKMODE_H */
