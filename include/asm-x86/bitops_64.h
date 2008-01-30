#ifndef _X86_64_BITOPS_H
#define _X86_64_BITOPS_H

/*
 * Copyright 1992, Linus Torvalds.
 */

extern long find_first_zero_bit(const unsigned long *addr, unsigned long size);
extern long find_next_zero_bit(const unsigned long *addr, long size, long offset);
extern long find_first_bit(const unsigned long *addr, unsigned long size);
extern long find_next_bit(const unsigned long *addr, long size, long offset);

/* return index of first bet set in val or max when no bit is set */
static inline long __scanbit(unsigned long val, unsigned long max)
{
	asm("bsfq %1,%0 ; cmovz %2,%0" : "=&r" (val) : "r" (val), "r" (max));
	return val;
}

#define find_first_bit(addr,size) \
((__builtin_constant_p(size) && (size) <= BITS_PER_LONG ? \
  (__scanbit(*(unsigned long *)addr,(size))) : \
  find_first_bit(addr,size)))

#define find_next_bit(addr,size,off) \
((__builtin_constant_p(size) && (size) <= BITS_PER_LONG ? 	  \
  ((off) + (__scanbit((*(unsigned long *)addr) >> (off),(size)-(off)))) : \
	find_next_bit(addr,size,off)))

#define find_first_zero_bit(addr,size) \
((__builtin_constant_p(size) && (size) <= BITS_PER_LONG ? \
  (__scanbit(~*(unsigned long *)addr,(size))) : \
  	find_first_zero_bit(addr,size)))
	
#define find_next_zero_bit(addr,size,off) \
((__builtin_constant_p(size) && (size) <= BITS_PER_LONG ? 	  \
  ((off)+(__scanbit(~(((*(unsigned long *)addr)) >> (off)),(size)-(off)))) : \
	find_next_zero_bit(addr,size,off)))

/* 
 * Find string of zero bits in a bitmap. -1 when not found.
 */ 
extern unsigned long 
find_next_zero_string(unsigned long *bitmap, long start, long nbits, int len);

static inline void set_bit_string(unsigned long *bitmap, unsigned long i, 
				  int len) 
{ 
	unsigned long end = i + len; 
	while (i < end) {
		__set_bit(i, bitmap); 
		i++;
	}
} 

static inline void __clear_bit_string(unsigned long *bitmap, unsigned long i, 
				    int len) 
{ 
	unsigned long end = i + len; 
	while (i < end) {
		__clear_bit(i, bitmap); 
		i++;
	}
} 

/**
 * ffz - find first zero in word.
 * @word: The word to search
 *
 * Undefined if no zero exists, so code should check against ~0UL first.
 */
static inline unsigned long ffz(unsigned long word)
{
	__asm__("bsfq %1,%0"
		:"=r" (word)
		:"r" (~word));
	return word;
}

/**
 * __ffs - find first bit in word.
 * @word: The word to search
 *
 * Undefined if no bit exists, so code should check against 0 first.
 */
static inline unsigned long __ffs(unsigned long word)
{
	__asm__("bsfq %1,%0"
		:"=r" (word)
		:"rm" (word));
	return word;
}

/*
 * __fls: find last bit set.
 * @word: The word to search
 *
 * Undefined if no zero exists, so code should check against ~0UL first.
 */
static inline unsigned long __fls(unsigned long word)
{
	__asm__("bsrq %1,%0"
		:"=r" (word)
		:"rm" (word));
	return word;
}

#ifdef __KERNEL__

#include <asm-generic/bitops/sched.h>

/**
 * ffs - find first bit set
 * @x: the word to search
 *
 * This is defined the same way as
 * the libc and compiler builtin ffs routines, therefore
 * differs in spirit from the above ffz (man ffs).
 */
static inline int ffs(int x)
{
	int r;

	__asm__("bsfl %1,%0\n\t"
		"cmovzl %2,%0" 
		: "=r" (r) : "rm" (x), "r" (-1));
	return r+1;
}

/**
 * fls64 - find last bit set in 64 bit word
 * @x: the word to search
 *
 * This is defined the same way as fls.
 */
static inline int fls64(__u64 x)
{
	if (x == 0)
		return 0;
	return __fls(x) + 1;
}

/**
 * fls - find last bit set
 * @x: the word to search
 *
 * This is defined the same way as ffs.
 */
static inline int fls(int x)
{
	int r;

	__asm__("bsrl %1,%0\n\t"
		"cmovzl %2,%0"
		: "=&r" (r) : "rm" (x), "rm" (-1));
	return r+1;
}

#define ARCH_HAS_FAST_MULTIPLIER 1

#include <asm-generic/bitops/hweight.h>

#endif /* __KERNEL__ */

#ifdef __KERNEL__

#include <asm-generic/bitops/ext2-non-atomic.h>

#define ext2_set_bit_atomic(lock,nr,addr) \
	        test_and_set_bit((nr),(unsigned long*)addr)
#define ext2_clear_bit_atomic(lock,nr,addr) \
	        test_and_clear_bit((nr),(unsigned long*)addr)

#include <asm-generic/bitops/minix.h>

#endif /* __KERNEL__ */

#endif /* _X86_64_BITOPS_H */
