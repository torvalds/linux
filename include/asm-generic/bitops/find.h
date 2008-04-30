#ifndef _ASM_GENERIC_BITOPS_FIND_H_
#define _ASM_GENERIC_BITOPS_FIND_H_

#ifndef CONFIG_GENERIC_FIND_NEXT_BIT
extern unsigned long find_next_bit(const unsigned long *addr, unsigned long
		size, unsigned long offset);

extern unsigned long find_next_zero_bit(const unsigned long *addr, unsigned
		long size, unsigned long offset);
#endif

#define find_first_bit(addr, size) find_next_bit((addr), (size), 0)
#define find_first_zero_bit(addr, size) find_next_zero_bit((addr), (size), 0)

#endif /*_ASM_GENERIC_BITOPS_FIND_H_ */
