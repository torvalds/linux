/*
 * linux/include/asm-arm/arch-cl7500/io.h
 *  from linux/include/asm-arm/arch-rpc/io.h
 *
 * Copyright (C) 1997 Russell King
 *
 * Modifications:
 *  06-Dec-1997	RMK	Created.
 */
#ifndef __ASM_ARM_ARCH_IO_H
#define __ASM_ARM_ARCH_IO_H

#include <asm/hardware.h>

#define IO_SPACE_LIMIT 0xffffffff

/*
 * GCC is totally crap at loading/storing data.  We try to persuade it
 * to do the right thing by using these whereever possible instead of
 * the above.
 */
#define __arch_base_getb(b,o)			\
 ({						\
	unsigned int v, r = (b);		\
	__asm__ __volatile__(			\
		"ldrb	%0, [%1, %2]"		\
		: "=r" (v)			\
		: "r" (r), "Ir" (o));		\
	v;					\
 })

#define __arch_base_getl(b,o)			\
 ({						\
	unsigned int v, r = (b);		\
	__asm__ __volatile__(			\
		"ldr	%0, [%1, %2]"		\
		: "=r" (v)			\
		: "r" (r), "Ir" (o));		\
	v;					\
 })

#define __arch_base_putb(v,b,o)			\
 ({						\
	unsigned int r = (b);			\
	__asm__ __volatile__(			\
		"strb	%0, [%1, %2]"		\
		:				\
		: "r" (v), "r" (r), "Ir" (o));	\
 })

#define __arch_base_putl(v,b,o)			\
 ({						\
	unsigned int r = (b);			\
	__asm__ __volatile__(			\
		"str	%0, [%1, %2]"		\
		:				\
		: "r" (v), "r" (r), "Ir" (o));	\
 })

/*
 * We use two different types of addressing - PC style addresses, and ARM
 * addresses.  PC style accesses the PC hardware with the normal PC IO
 * addresses, eg 0x3f8 for serial#1.  ARM addresses are 0x80000000+
 * and are translated to the start of IO.  Note that all addresses are
 * shifted left!
 */
#define __PORT_PCIO(x)	(!((x) & 0x80000000))

/*
 * Dynamic IO functions - let the compiler
 * optimize the expressions
 */
static inline void __outb (unsigned int value, unsigned int port)
{
	unsigned long temp;
	__asm__ __volatile__(
	"tst	%2, #0x80000000\n\t"
	"mov	%0, %4\n\t"
	"addeq	%0, %0, %3\n\t"
	"strb	%1, [%0, %2, lsl #2]	@ outb"
	: "=&r" (temp)
	: "r" (value), "r" (port), "Ir" (PCIO_BASE - IO_BASE), "Ir" (IO_BASE)
	: "cc");
}

static inline void __outw (unsigned int value, unsigned int port)
{
	unsigned long temp;
	__asm__ __volatile__(
	"tst	%2, #0x80000000\n\t"
	"mov	%0, %4\n\t"
	"addeq	%0, %0, %3\n\t"
	"str	%1, [%0, %2, lsl #2]	@ outw"
	: "=&r" (temp)
	: "r" (value|value<<16), "r" (port), "Ir" (PCIO_BASE - IO_BASE), "Ir" (IO_BASE)
	: "cc");
}

static inline void __outl (unsigned int value, unsigned int port)
{
	unsigned long temp;
	__asm__ __volatile__(
	"tst	%2, #0x80000000\n\t"
	"mov	%0, %4\n\t"
	"addeq	%0, %0, %3\n\t"
	"str	%1, [%0, %2, lsl #2]	@ outl"
	: "=&r" (temp)
	: "r" (value), "r" (port), "Ir" (PCIO_BASE - IO_BASE), "Ir" (IO_BASE)
	: "cc");
}

#define DECLARE_DYN_IN(sz,fnsuffix,instr)					\
static inline unsigned sz __in##fnsuffix (unsigned int port)		\
{										\
	unsigned long temp, value;						\
	__asm__ __volatile__(							\
	"tst	%2, #0x80000000\n\t"						\
	"mov	%0, %4\n\t"							\
	"addeq	%0, %0, %3\n\t"							\
	"ldr" instr "	%1, [%0, %2, lsl #2]	@ in" #fnsuffix			\
	: "=&r" (temp), "=r" (value)						\
	: "r" (port), "Ir" (PCIO_BASE - IO_BASE), "Ir" (IO_BASE)		\
	: "cc");								\
	return (unsigned sz)value;						\
}

static inline unsigned int __ioaddr (unsigned int port)			\
{										\
	if (__PORT_PCIO(port))							\
		return (unsigned int)(PCIO_BASE + (port << 2));			\
	else									\
		return (unsigned int)(IO_BASE + (port << 2));			\
}

#define DECLARE_IO(sz,fnsuffix,instr)	\
	DECLARE_DYN_IN(sz,fnsuffix,instr)

DECLARE_IO(char,b,"b")
DECLARE_IO(short,w,"")
DECLARE_IO(int,l,"")

#undef DECLARE_IO
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
		"strb	%0, [%1, %2]	@ outbc"				\
		: : "r" (value), "r" (PCIO_BASE), "Jr" ((port) << 2));		\
	else									\
		__asm__ __volatile__(						\
		"strb	%0, [%1, %2]	@ outbc"				\
		: : "r" (value), "r" (IO_BASE), "r" ((port) << 2));		\
})

#define __inbc(port)								\
({										\
	unsigned char result;							\
	if (__PORT_PCIO((port)))						\
		__asm__ __volatile__(						\
		"ldrb	%0, [%1, %2]	@ inbc"					\
		: "=r" (result) : "r" (PCIO_BASE), "Jr" ((port) << 2));		\
	else									\
		__asm__ __volatile__(						\
		"ldrb	%0, [%1, %2]	@ inbc"					\
		: "=r" (result) : "r" (IO_BASE), "r" ((port) << 2));		\
	result;									\
})

#define __outwc(value,port)							\
({										\
	unsigned long v = value;						\
	if (__PORT_PCIO((port)))						\
		__asm__ __volatile__(						\
		"str	%0, [%1, %2]	@ outwc"				\
		: : "r" (v|v<<16), "r" (PCIO_BASE), "Jr" ((port) << 2));	\
	else									\
		__asm__ __volatile__(						\
		"str	%0, [%1, %2]	@ outwc"				\
		: : "r" (v|v<<16), "r" (IO_BASE), "r" ((port) << 2));		\
})

#define __inwc(port)								\
({										\
	unsigned short result;							\
	if (__PORT_PCIO((port)))						\
		__asm__ __volatile__(						\
		"ldr	%0, [%1, %2]	@ inwc"					\
		: "=r" (result) : "r" (PCIO_BASE), "Jr" ((port) << 2));		\
	else									\
		__asm__ __volatile__(						\
		"ldr	%0, [%1, %2]	@ inwc"					\
		: "=r" (result) : "r" (IO_BASE), "r" ((port) << 2));		\
	result & 0xffff;							\
})

#define __outlc(value,port)							\
({										\
	unsigned long v = value;						\
	if (__PORT_PCIO((port)))						\
		__asm__ __volatile__(						\
		"str	%0, [%1, %2]	@ outlc"				\
		: : "r" (v), "r" (PCIO_BASE), "Jr" ((port) << 2));		\
	else									\
		__asm__ __volatile__(						\
		"str	%0, [%1, %2]	@ outlc"				\
		: : "r" (v), "r" (IO_BASE), "r" ((port) << 2));			\
})

#define __inlc(port)								\
({										\
	unsigned long result;							\
	if (__PORT_PCIO((port)))						\
		__asm__ __volatile__(						\
		"ldr	%0, [%1, %2]	@ inlc"					\
		: "=r" (result) : "r" (PCIO_BASE), "Jr" ((port) << 2));		\
	else									\
		__asm__ __volatile__(						\
		"ldr	%0, [%1, %2]	@ inlc"					\
		: "=r" (result) : "r" (IO_BASE), "r" ((port) << 2));		\
	result;									\
})

#define __ioaddrc(port)								\
	(__PORT_PCIO((port)) ? PCIO_BASE + ((port) << 2) : IO_BASE + ((port) << 2))

#define inb(p)	 	(__builtin_constant_p((p)) ? __inbc(p)    : __inb(p))
#define inw(p)	 	(__builtin_constant_p((p)) ? __inwc(p)    : __inw(p))
#define inl(p)	 	(__builtin_constant_p((p)) ? __inlc(p)    : __inl(p))
#define outb(v,p)	(__builtin_constant_p((p)) ? __outbc(v,p) : __outb(v,p))
#define outw(v,p)	(__builtin_constant_p((p)) ? __outwc(v,p) : __outw(v,p))
#define outl(v,p)	(__builtin_constant_p((p)) ? __outlc(v,p) : __outl(v,p))
#define __ioaddr(p)	(__builtin_constant_p((p)) ? __ioaddr(p)  : __ioaddrc(p))
/* the following macro is deprecated */
#define ioaddr(port)			__ioaddr((port))

#define insb(p,d,l)	__raw_readsb(__ioaddr(p),d,l)
#define insw(p,d,l)	__raw_readsw(__ioaddr(p),d,l)

#define outsb(p,d,l)	__raw_writesb(__ioaddr(p),d,l)
#define outsw(p,d,l)	__raw_writesw(__ioaddr(p),d,l)

/*
 * 1:1 mapping for ioremapped regions.
 */
#define __mem_pci(x)	(x)

#endif
