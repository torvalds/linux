#ifndef _ASM_GENERIC_BITOPS_EXT2_NON_ATOMIC_H_
#define _ASM_GENERIC_BITOPS_EXT2_NON_ATOMIC_H_

#include <asm-generic/bitops/le.h>

#define ext2_set_bit(nr,addr)	\
	__test_and_set_bit_le((nr), (unsigned long *)(addr))
#define ext2_clear_bit(nr,addr)	\
	__test_and_clear_bit_le((nr), (unsigned long *)(addr))

#define ext2_test_bit(nr,addr)	\
	test_bit_le((nr), (unsigned long *)(addr))
#define ext2_find_first_zero_bit(addr, size) \
	find_first_zero_bit_le((unsigned long *)(addr), (size))
#define ext2_find_next_zero_bit(addr, size, off) \
	find_next_zero_bit_le((unsigned long *)(addr), (size), (off))
#define ext2_find_next_bit(addr, size, off) \
	find_next_bit_le((unsigned long *)(addr), (size), (off))

#endif /* _ASM_GENERIC_BITOPS_EXT2_NON_ATOMIC_H_ */
