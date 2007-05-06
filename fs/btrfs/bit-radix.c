#include <linux/module.h>
#include "bit-radix.h"

#define BIT_ARRAY_BYTES 256
#define BIT_RADIX_BITS_PER_ARRAY ((BIT_ARRAY_BYTES - sizeof(unsigned long)) * 8)

extern struct kmem_cache *btrfs_bit_radix_cachep;
int set_radix_bit(struct radix_tree_root *radix, unsigned long bit)
{
	unsigned long *bits;
	unsigned long slot;
	int bit_slot;
	int ret;

	slot = bit / BIT_RADIX_BITS_PER_ARRAY;
	bit_slot = bit % BIT_RADIX_BITS_PER_ARRAY;

	bits = radix_tree_lookup(radix, slot);
	if (!bits) {
		bits = kmem_cache_alloc(btrfs_bit_radix_cachep, GFP_NOFS);
		if (!bits)
			return -ENOMEM;
		memset(bits + 1, 0, BIT_ARRAY_BYTES - sizeof(unsigned long));
		bits[0] = slot;
		ret = radix_tree_insert(radix, slot, bits);
		if (ret)
			return ret;
	}
	ret = test_and_set_bit(bit_slot, bits + 1);
	if (ret < 0)
		ret = 1;
	return ret;
}

int test_radix_bit(struct radix_tree_root *radix, unsigned long bit)
{
	unsigned long *bits;
	unsigned long slot;
	int bit_slot;

	slot = bit / BIT_RADIX_BITS_PER_ARRAY;
	bit_slot = bit % BIT_RADIX_BITS_PER_ARRAY;

	bits = radix_tree_lookup(radix, slot);
	if (!bits)
		return 0;
	return test_bit(bit_slot, bits + 1);
}

int clear_radix_bit(struct radix_tree_root *radix, unsigned long bit)
{
	unsigned long *bits;
	unsigned long slot;
	int bit_slot;
	int i;
	int empty = 1;

	slot = bit / BIT_RADIX_BITS_PER_ARRAY;
	bit_slot = bit % BIT_RADIX_BITS_PER_ARRAY;

	bits = radix_tree_lookup(radix, slot);
	if (!bits)
		return 0;
	clear_bit(bit_slot, bits + 1);
	for (i = 1; i < BIT_ARRAY_BYTES / sizeof(unsigned long); i++) {
		if (bits[i]) {
			empty = 0;
			break;
		}
	}
	if (empty) {
		bits = radix_tree_delete(radix, slot);
		BUG_ON(!bits);
		kmem_cache_free(btrfs_bit_radix_cachep, bits);
	}
	return 0;
}

int find_first_radix_bit(struct radix_tree_root *radix, unsigned long *retbits,
			 int nr)
{
	unsigned long *bits;
	unsigned long *gang[4];
	int found;
	int ret;
	int i;
	int total_found = 0;

	ret = radix_tree_gang_lookup(radix, (void **)gang, 0, ARRAY_SIZE(gang));
	for (i = 0; i < ret && nr > 0; i++) {
		found = 0;
		bits = gang[i];
		while(nr > 0) {
			found = find_next_bit(bits + 1,
					      BIT_RADIX_BITS_PER_ARRAY,
					      found);
			if (found < BIT_RADIX_BITS_PER_ARRAY) {
				*retbits = bits[0] *
					BIT_RADIX_BITS_PER_ARRAY + found;
				retbits++;
				nr--;
				total_found++;
				found++;
			} else
				break;
		}
	}
	return total_found;
}
