/*
 * linux/include/asm-arm/arch-shark/io.h
 *
 * by Alexander Schulz
 *
 * derived from:
 * linux/include/asm-arm/arch-ebsa110/io.h
 * Copyright (C) 1997,1998 Russell King
 */

#ifndef __ASM_ARM_ARCH_IO_H
#define __ASM_ARM_ARCH_IO_H

#define IO_SPACE_LIMIT 0xffffffff

/*
 * We use two different types of addressing - PC style addresses, and ARM
 * addresses.  PC style accesses the PC hardware with the normal PC IO
 * addresses, eg 0x3f8 for serial#1.  ARM addresses are 0x80000000+
 * and are translated to the start of IO.
 */
#define __PORT_PCIO(x)	(!((x) & 0x80000000))

/*
 * Dynamic IO functions - let the compiler
 * optimize the expressions
 */
#define DECLARE_DYN_OUT(fnsuffix,instr)						\
static inline void __out##fnsuffix (unsigned int value, unsigned int port)	\
{										\
	unsigned long temp;							\
	__asm__ __volatile__(							\
	"tst	%2, #0x80000000\n\t"						\
	"mov	%0, %4\n\t"							\
	"addeq	%0, %0, %3\n\t"							\
	"str" instr "	%1, [%0, %2]	@ out" #fnsuffix			\
	: "=&r" (temp)								\
	: "r" (value), "r" (port), "Ir" (PCIO_BASE - IO_BASE), "Ir" (IO_BASE)	\
	: "cc");								\
}

#define DECLARE_DYN_IN(sz,fnsuffix,instr)					\
static inline unsigned sz __in##fnsuffix (unsigned int port)		\
{										\
	unsigned long temp, value;						\
	__asm__ __volatile__(							\
	"tst	%2, #0x80000000\n\t"						\
	"mov	%0, %4\n\t"							\
	"addeq	%0, %0, %3\n\t"							\
	"ldr" instr "	%1, [%0, %2]	@ in" #fnsuffix				\
	: "=&r" (temp), "=r" (value)						\
	: "r" (port), "Ir" (PCIO_BASE - IO_BASE), "Ir" (IO_BASE)		\
	: "cc");								\
	return (unsigned sz)value;						\
}

static inline unsigned int __ioaddr (unsigned int port)			\
{										\
	if (__PORT_PCIO(port))							\
		return (unsigned int)(PCIO_BASE + (port));			\
	else									\
		return (unsigned int)(IO_BASE + (port));			\
}

#define DECLARE_IO(sz,fnsuffix,instr)	\
	DECLARE_DYN_OUT(fnsuffix,instr)	\
	DECLARE_DYN_IN(sz,fnsuffix,instr)

DECLARE_IO(char,b,"b")
DECLARE_IO(short,w,"h")
DECLARE_IO(long,l,"")

#undef DECLARE_IO
#undef DECLARE_DYN_OUT
#undef DECLARE_DYN_IN

/*
 * Constant address IO functions
 *
 * These have to be macros for the 'J' constraint to work -
 * +/-4096 immediate operand.
 */
#define __outbc(value,port)							\
({										\
	if (__PORT_PCIO((port)))						\
		__asm__ __volatile__(						\
		"strb	%0, [%1, %2]		@ outbc"			\
		: : "r" (value), "r" (PCIO_BASE), "Jr" (port));		\
	else									\
		__asm__ __volatile__(						\
		"strb	%0, [%1, %2]		@ outbc"			\
		: : "r" (value), "r" (IO_BASE), "r" (port));		\
})

#define __inbc(port)								\
({										\
	unsigned char result;                                                   \
	if (__PORT_PCIO((port)))						\
		__asm__ __volatile__(						\
		"ldrb	%0, [%1, %2]		@ inbc"				\
		: "=r" (result) : "r" (PCIO_BASE), "Jr" (port));		\
	else									\
		__asm__ __volatile__(						\
		"ldrb	%0, [%1, %2]		@ inbc"				\
		: "=r" (result) : "r" (IO_BASE), "r" (port));		\
	result;									\
})

#define __outwc(value,port)							\
({										\
	unsigned long v = value;						\
	if (__PORT_PCIO((port)))						\
		__asm__ __volatile__(						\
		"strh	%0, [%1, %2]		@ outwc"			\
		: : "r" (v|v<<16), "r" (PCIO_BASE), "Jr" (port));	\
	else									\
		__asm__ __volatile__(						\
		"strh	%0, [%1, %2]		@ outwc"			\
		: : "r" (v|v<<16), "r" (IO_BASE), "r" (port));		\
})

#define __inwc(port)								\
({										\
	unsigned short result;							\
	if (__PORT_PCIO((port)))						\
		__asm__ __volatile__(						\
		"ldrh	%0, [%1, %2]		@ inwc"				\
		: "=r" (result) : "r" (PCIO_BASE), "Jr" (port));		\
	else									\
		__asm__ __volatile__(						\
		"ldrh	%0, [%1, %2]		@ inwc"				\
		: "=r" (result) : "r" (IO_BASE), "r" (port));		\
	result & 0xffff;							\
})

#define __outlc(value,port)								\
({										\
	unsigned long v = value;						\
	if (__PORT_PCIO((port)))						\
		__asm__ __volatile__(						\
		"str	%0, [%1, %2]		@ outlc"			\
		: : "r" (v), "r" (PCIO_BASE), "Jr" (port));		\
	else									\
		__asm__ __volatile__(						\
		"str	%0, [%1, %2]		@ outlc"			\
		: : "r" (v), "r" (IO_BASE), "r" (port));			\
})

#define __inlc(port)								\
({										\
	unsigned long result;							\
	if (__PORT_PCIO((port)))						\
		__asm__ __volatile__(						\
		"ldr	%0, [%1, %2]		@ inlc"				\
		: "=r" (result) : "r" (PCIO_BASE), "Jr" (port));		\
	else									\
		__asm__ __volatile__(						\
		"ldr	%0, [%1, %2]		@ inlc"				\
		: "=r" (result) : "r" (IO_BASE), "r" (port));		\
	result;									\
})

#define __ioaddrc(port)								\
({										\
	unsigned long addr;							\
	if (__PORT_PCIO((port)))						\
		addr = PCIO_BASE + (port);				\
	else									\
		addr = IO_BASE + (port);					\
	addr;									\
})

#define __mem_pci(addr) (addr)

#define inb(p)	 	(__builtin_constant_p((p)) ? __inbc(p)    : __inb(p))
#define inw(p)	 	(__builtin_constant_p((p)) ? __inwc(p)    : __inw(p))
#define inl(p)	 	(__builtin_constant_p((p)) ? __inlc(p)    : __inl(p))
#define outb(v,p)	(__builtin_constant_p((p)) ? __outbc(v,p) : __outb(v,p))
#define outw(v,p)	(__builtin_constant_p((p)) ? __outwc(v,p) : __outw(v,p))
#define outl(v,p)	(__builtin_constant_p((p)) ? __outlc(v,p) : __outl(v,p))

/*
 * Translated address IO functions
 *
 * IO address has already been translated to a virtual address
 */
#define outb_t(v,p)								\
	(*(volatile unsigned char *)(p) = (v))

#define inb_t(p)								\
	(*(volatile unsigned char *)(p))

#define outl_t(v,p)								\
	(*(volatile unsigned long *)(p) = (v))

#define inl_t(p)								\
	(*(volatile unsigned long *)(p))

#endif
