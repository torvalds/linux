#ifndef _H8300_BITOPS_H
#define _H8300_BITOPS_H

/*
 * Copyright 1992, Linus Torvalds.
 * Copyright 2002, Yoshinori Sato
 */

#include <linux/config.h>
#include <linux/compiler.h>
#include <asm/byteorder.h>	/* swab32 */
#include <asm/system.h>

#ifdef __KERNEL__
/*
 * Function prototypes to keep gcc -Wall happy
 */

/*
 * ffz = Find First Zero in word. Undefined if no zero exists,
 * so code should check against ~0UL first..
 */
static __inline__ unsigned long ffz(unsigned long word)
{
	unsigned long result;

	result = -1;
	__asm__("1:\n\t"
		"shlr.l %2\n\t"
		"adds #1,%0\n\t"
		"bcs 1b"
		: "=r" (result)
		: "0"  (result),"r" (word));
	return result;
}

#define H8300_GEN_BITOP_CONST(OP,BIT)			    \
	case BIT:					    \
	__asm__(OP " #" #BIT ",@%0"::"r"(b_addr):"memory"); \
	break;

#define H8300_GEN_BITOP(FNAME,OP)				      \
static __inline__ void FNAME(int nr, volatile unsigned long* addr)    \
{								      \
	volatile unsigned char *b_addr;				      \
	b_addr = (volatile unsigned char *)addr + ((nr >> 3) ^ 3);    \
	if (__builtin_constant_p(nr)) {				      \
		switch(nr & 7) {				      \
			H8300_GEN_BITOP_CONST(OP,0)		      \
			H8300_GEN_BITOP_CONST(OP,1)		      \
			H8300_GEN_BITOP_CONST(OP,2)		      \
			H8300_GEN_BITOP_CONST(OP,3)		      \
			H8300_GEN_BITOP_CONST(OP,4)		      \
			H8300_GEN_BITOP_CONST(OP,5)		      \
			H8300_GEN_BITOP_CONST(OP,6)		      \
			H8300_GEN_BITOP_CONST(OP,7)		      \
		}						      \
	} else {						      \
		__asm__(OP " %w0,@%1"::"r"(nr),"r"(b_addr):"memory"); \
	}							      \
}

/*
 * clear_bit() doesn't provide any barrier for the compiler.
 */
#define smp_mb__before_clear_bit()	barrier()
#define smp_mb__after_clear_bit()	barrier()

H8300_GEN_BITOP(set_bit	  ,"bset")
H8300_GEN_BITOP(clear_bit ,"bclr")
H8300_GEN_BITOP(change_bit,"bnot")
#define __set_bit(nr,addr)    set_bit((nr),(addr))
#define __clear_bit(nr,addr)  clear_bit((nr),(addr))
#define __change_bit(nr,addr) change_bit((nr),(addr))

#undef H8300_GEN_BITOP
#undef H8300_GEN_BITOP_CONST

static __inline__ int test_bit(int nr, const unsigned long* addr)
{
	return (*((volatile unsigned char *)addr + 
               ((nr >> 3) ^ 3)) & (1UL << (nr & 7))) != 0;
}

#define __test_bit(nr, addr) test_bit(nr, addr)

#define H8300_GEN_TEST_BITOP_CONST_INT(OP,BIT)			     \
	case BIT:						     \
	__asm__("stc ccr,%w1\n\t"				     \
		"orc #0x80,ccr\n\t"				     \
		"bld #" #BIT ",@%4\n\t"				     \
		OP " #" #BIT ",@%4\n\t"				     \
		"rotxl.l %0\n\t"				     \
		"ldc %w1,ccr"					     \
		: "=r"(retval),"=&r"(ccrsave),"=m"(*b_addr)	     \
		: "0" (retval),"r" (b_addr)			     \
		: "memory");                                         \
        break;

#define H8300_GEN_TEST_BITOP_CONST(OP,BIT)			     \
	case BIT:						     \
	__asm__("bld #" #BIT ",@%3\n\t"				     \
		OP " #" #BIT ",@%3\n\t"				     \
		"rotxl.l %0\n\t"				     \
		: "=r"(retval),"=m"(*b_addr)			     \
		: "0" (retval),"r" (b_addr)			     \
		: "memory");                                         \
        break;

#define H8300_GEN_TEST_BITOP(FNNAME,OP)				     \
static __inline__ int FNNAME(int nr, volatile void * addr)	     \
{								     \
	int retval = 0;						     \
	char ccrsave;						     \
	volatile unsigned char *b_addr;				     \
	b_addr = (volatile unsigned char *)addr + ((nr >> 3) ^ 3);   \
	if (__builtin_constant_p(nr)) {				     \
		switch(nr & 7) {				     \
			H8300_GEN_TEST_BITOP_CONST_INT(OP,0)	     \
			H8300_GEN_TEST_BITOP_CONST_INT(OP,1)	     \
			H8300_GEN_TEST_BITOP_CONST_INT(OP,2)	     \
			H8300_GEN_TEST_BITOP_CONST_INT(OP,3)	     \
			H8300_GEN_TEST_BITOP_CONST_INT(OP,4)	     \
			H8300_GEN_TEST_BITOP_CONST_INT(OP,5)	     \
			H8300_GEN_TEST_BITOP_CONST_INT(OP,6)	     \
			H8300_GEN_TEST_BITOP_CONST_INT(OP,7)	     \
		}						     \
	} else {						     \
		__asm__("stc ccr,%w1\n\t"			     \
			"orc #0x80,ccr\n\t"			     \
			"btst %w5,@%4\n\t"			     \
			OP " %w5,@%4\n\t"			     \
			"beq 1f\n\t"				     \
			"inc.l #1,%0\n"				     \
			"1:\n\t"				     \
			"ldc %w1,ccr"				     \
			: "=r"(retval),"=&r"(ccrsave),"=m"(*b_addr)  \
			: "0" (retval),"r" (b_addr),"r"(nr)	     \
			: "memory");				     \
	}							     \
	return retval;						     \
}								     \
								     \
static __inline__ int __ ## FNNAME(int nr, volatile void * addr)     \
{								     \
	int retval = 0;						     \
	volatile unsigned char *b_addr;				     \
	b_addr = (volatile unsigned char *)addr + ((nr >> 3) ^ 3);   \
	if (__builtin_constant_p(nr)) {				     \
		switch(nr & 7) {				     \
			H8300_GEN_TEST_BITOP_CONST(OP,0) 	     \
			H8300_GEN_TEST_BITOP_CONST(OP,1) 	     \
			H8300_GEN_TEST_BITOP_CONST(OP,2) 	     \
			H8300_GEN_TEST_BITOP_CONST(OP,3) 	     \
			H8300_GEN_TEST_BITOP_CONST(OP,4) 	     \
			H8300_GEN_TEST_BITOP_CONST(OP,5) 	     \
			H8300_GEN_TEST_BITOP_CONST(OP,6) 	     \
			H8300_GEN_TEST_BITOP_CONST(OP,7) 	     \
		}						     \
	} else {						     \
		__asm__("btst %w4,@%3\n\t"			     \
			OP " %w4,@%3\n\t"			     \
			"beq 1f\n\t"				     \
			"inc.l #1,%0\n"				     \
			"1:"					     \
			: "=r"(retval),"=m"(*b_addr)		     \
			: "0" (retval),"r" (b_addr),"r"(nr)	     \
			: "memory");				     \
	}							     \
	return retval;						     \
}

H8300_GEN_TEST_BITOP(test_and_set_bit,	 "bset")
H8300_GEN_TEST_BITOP(test_and_clear_bit, "bclr")
H8300_GEN_TEST_BITOP(test_and_change_bit,"bnot")
#undef H8300_GEN_TEST_BITOP_CONST
#undef H8300_GEN_TEST_BITOP_CONST_INT
#undef H8300_GEN_TEST_BITOP

#define find_first_zero_bit(addr, size) \
	find_next_zero_bit((addr), (size), 0)

#define ffs(x) generic_ffs(x)

static __inline__ unsigned long __ffs(unsigned long word)
{
	unsigned long result;

	result = -1;
	__asm__("1:\n\t"
		"shlr.l %2\n\t"
		"adds #1,%0\n\t"
		"bcc 1b"
		: "=r" (result)
		: "0"(result),"r"(word));
	return result;
}

static __inline__ int find_next_zero_bit (const unsigned long * addr, int size, int offset)
{
	unsigned long *p = (unsigned long *)(((unsigned long)addr + (offset >> 3)) & ~3);
	unsigned long result = offset & ~31UL;
	unsigned long tmp;

	if (offset >= size)
		return size;
	size -= result;
	offset &= 31UL;
	if (offset) {
		tmp = *(p++);
		tmp |= ~0UL >> (32-offset);
		if (size < 32)
			goto found_first;
		if (~tmp)
			goto found_middle;
		size -= 32;
		result += 32;
	}
	while (size & ~31UL) {
		if (~(tmp = *(p++)))
			goto found_middle;
		result += 32;
		size -= 32;
	}
	if (!size)
		return result;
	tmp = *p;

found_first:
	tmp |= ~0UL >> size;
found_middle:
	return result + ffz(tmp);
}

static __inline__ unsigned long find_next_bit(const unsigned long *addr,
	unsigned long size, unsigned long offset)
{
	unsigned long *p = (unsigned long *)(((unsigned long)addr + (offset >> 3)) & ~3);
	unsigned int result = offset & ~31UL;
	unsigned int tmp;

	if (offset >= size)
		return size;
	size -= result;
	offset &= 31UL;
	if (offset) {
		tmp = *(p++);
		tmp &= ~0UL << offset;
		if (size < 32)
			goto found_first;
		if (tmp)
			goto found_middle;
		size -= 32;
		result += 32;
	}
	while (size >= 32) {
		if ((tmp = *p++) != 0)
			goto found_middle;
		result += 32;
		size -= 32;
	}
	if (!size)
		return result;
	tmp = *p;

found_first:
	tmp &= ~0UL >> (32 - size);
	if (tmp == 0UL)
		return result + size;
found_middle:
	return result + __ffs(tmp);
}

#define find_first_bit(addr, size) find_next_bit(addr, size, 0)

/*
 * Every architecture must define this function. It's the fastest
 * way of searching a 140-bit bitmap where the first 100 bits are
 * unlikely to be set. It's guaranteed that at least one of the 140
 * bits is cleared.
 */
static inline int sched_find_first_bit(unsigned long *b)
{
	if (unlikely(b[0]))
		return __ffs(b[0]);
	if (unlikely(b[1]))
		return __ffs(b[1]) + 32;
	if (unlikely(b[2]))
		return __ffs(b[2]) + 64;
	if (b[3])
		return __ffs(b[3]) + 96;
	return __ffs(b[4]) + 128;
}

/*
 * hweightN: returns the hamming weight (i.e. the number
 * of bits set) of a N-bit word
 */

#define hweight32(x) generic_hweight32(x)
#define hweight16(x) generic_hweight16(x)
#define hweight8(x) generic_hweight8(x)

static __inline__ int ext2_set_bit(int nr, volatile void * addr)
{
	int		mask, retval;
	unsigned long	flags;
	volatile unsigned char	*ADDR = (unsigned char *) addr;

	ADDR += nr >> 3;
	mask = 1 << (nr & 0x07);
	local_irq_save(flags);
	retval = (mask & *ADDR) != 0;
	*ADDR |= mask;
	local_irq_restore(flags);
	return retval;
}
#define ext2_set_bit_atomic(lock, nr, addr) ext2_set_bit(nr, addr)

static __inline__ int ext2_clear_bit(int nr, volatile void * addr)
{
	int		mask, retval;
	unsigned long	flags;
	volatile unsigned char	*ADDR = (unsigned char *) addr;

	ADDR += nr >> 3;
	mask = 1 << (nr & 0x07);
	local_irq_save(flags);
	retval = (mask & *ADDR) != 0;
	*ADDR &= ~mask;
	local_irq_restore(flags);
	return retval;
}
#define ext2_clear_bit_atomic(lock, nr, addr) ext2_set_bit(nr, addr)

static __inline__ int ext2_test_bit(int nr, const volatile void * addr)
{
	int			mask;
	const volatile unsigned char	*ADDR = (const unsigned char *) addr;

	ADDR += nr >> 3;
	mask = 1 << (nr & 0x07);
	return ((mask & *ADDR) != 0);
}

#define ext2_find_first_zero_bit(addr, size) \
	ext2_find_next_zero_bit((addr), (size), 0)

static __inline__ unsigned long ext2_find_next_zero_bit(void *addr, unsigned long size, unsigned long offset)
{
	unsigned long *p = ((unsigned long *) addr) + (offset >> 5);
	unsigned long result = offset & ~31UL;
	unsigned long tmp;

	if (offset >= size)
		return size;
	size -= result;
	offset &= 31UL;
	if(offset) {
		/* We hold the little endian value in tmp, but then the
		 * shift is illegal. So we could keep a big endian value
		 * in tmp, like this:
		 *
		 * tmp = __swab32(*(p++));
		 * tmp |= ~0UL >> (32-offset);
		 *
		 * but this would decrease performance, so we change the
		 * shift:
		 */
		tmp = *(p++);
		tmp |= __swab32(~0UL >> (32-offset));
		if(size < 32)
			goto found_first;
		if(~tmp)
			goto found_middle;
		size -= 32;
		result += 32;
	}
	while(size & ~31UL) {
		if(~(tmp = *(p++)))
			goto found_middle;
		result += 32;
		size -= 32;
	}
	if(!size)
		return result;
	tmp = *p;

found_first:
	/* tmp is little endian, so we would have to swab the shift,
	 * see above. But then we have to swab tmp below for ffz, so
	 * we might as well do this here.
	 */
	return result + ffz(__swab32(tmp) | (~0UL << size));
found_middle:
	return result + ffz(__swab32(tmp));
}

/* Bitmap functions for the minix filesystem.  */
#define minix_test_and_set_bit(nr,addr) test_and_set_bit(nr,addr)
#define minix_set_bit(nr,addr) set_bit(nr,addr)
#define minix_test_and_clear_bit(nr,addr) test_and_clear_bit(nr,addr)
#define minix_test_bit(nr,addr) test_bit(nr,addr)
#define minix_find_first_zero_bit(addr,size) find_first_zero_bit(addr,size)

#endif /* __KERNEL__ */

#define fls(x) generic_fls(x)
#define fls64(x)   generic_fls64(x)

#endif /* _H8300_BITOPS_H */
