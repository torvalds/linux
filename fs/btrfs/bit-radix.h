#ifndef __BIT_RADIX__
#define __BIT_RADIX__
#include <linux/radix-tree.h>

int set_radix_bit(struct radix_tree_root *radix, unsigned long bit);
int test_radix_bit(struct radix_tree_root *radix, unsigned long bit);
int clear_radix_bit(struct radix_tree_root *radix, unsigned long bit);
int find_first_radix_bit(struct radix_tree_root *radix, unsigned long *retbits,
			 int nr);

static inline void init_bit_radix(struct radix_tree_root *radix)
{
	INIT_RADIX_TREE(radix, GFP_NOFS);
}
#endif
