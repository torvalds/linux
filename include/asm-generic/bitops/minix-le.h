#ifndef _ASM_GENERIC_BITOPS_MINIX_LE_H_
#define _ASM_GENERIC_BITOPS_MINIX_LE_H_

#define minix_test_and_set_bit(nr,addr)	\
	__test_and_set_bit_le((nr), (unsigned long *)(addr))
#define minix_set_bit(nr,addr)		\
	__set_bit_le((nr), (unsigned long *)(addr))
#define minix_test_and_clear_bit(nr,addr) \
	__test_and_clear_bit_le((nr), (unsigned long *)(addr))
#define minix_test_bit(nr,addr)		\
	test_bit_le((nr), (unsigned long *)(addr))
#define minix_find_first_zero_bit(addr,size) \
	find_first_zero_bit_le((unsigned long *)(addr), (size))

#endif /* _ASM_GENERIC_BITOPS_MINIX_LE_H_ */
