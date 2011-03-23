#ifndef _ASM_GENERIC_BITOPS_LE_H_
#define _ASM_GENERIC_BITOPS_LE_H_

#include <asm/types.h>
#include <asm/byteorder.h>

#if defined(__LITTLE_ENDIAN)

#define BITOP_LE_SWIZZLE	0

#define find_next_zero_bit_le(addr, size, offset) \
	find_next_zero_bit(addr, size, offset)
#define find_next_bit_le(addr, size, offset) \
	find_next_bit(addr, size, offset)
#define find_first_zero_bit_le(addr, size) \
	find_first_zero_bit(addr, size)

#elif defined(__BIG_ENDIAN)

#define BITOP_LE_SWIZZLE	((BITS_PER_LONG-1) & ~0x7)

extern unsigned long find_next_zero_bit_le(const unsigned long *addr,
		unsigned long size, unsigned long offset);
extern unsigned long find_next_bit_le(const unsigned long *addr,
		unsigned long size, unsigned long offset);

#define find_first_zero_bit_le(addr, size) \
	find_next_zero_bit_le((addr), (size), 0)

#else
#error "Please fix <asm/byteorder.h>"
#endif

#define test_bit_le(nr, addr) \
	test_bit((nr) ^ BITOP_LE_SWIZZLE, (addr))
#define __set_bit_le(nr, addr) \
	__set_bit((nr) ^ BITOP_LE_SWIZZLE, (addr))
#define __clear_bit_le(nr, addr) \
	__clear_bit((nr) ^ BITOP_LE_SWIZZLE, (addr))

#define test_and_set_bit_le(nr, addr) \
	test_and_set_bit((nr) ^ BITOP_LE_SWIZZLE, (addr))
#define test_and_clear_bit_le(nr, addr) \
	test_and_clear_bit((nr) ^ BITOP_LE_SWIZZLE, (addr))

#define __test_and_set_bit_le(nr, addr) \
	__test_and_set_bit((nr) ^ BITOP_LE_SWIZZLE, (addr))
#define __test_and_clear_bit_le(nr, addr) \
	__test_and_clear_bit((nr) ^ BITOP_LE_SWIZZLE, (addr))

#endif /* _ASM_GENERIC_BITOPS_LE_H_ */
