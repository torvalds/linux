/*
 *  linux/include/asm-arm/io.h
 *
 *  Copyright (C) 1996-2000 Russell King
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 * Modifications:
 *  16-Sep-1996	RMK	Inlined the inx/outx functions & optimised for both
 *			constant addresses and variable addresses.
 *  04-Dec-1997	RMK	Moved a lot of this stuff to the new architecture
 *			specific IO header files.
 *  27-Mar-1999	PJB	Second parameter of memcpy_toio is const..
 *  04-Apr-1999	PJB	Added check_signature.
 *  12-Dec-1999	RMK	More cleanups
 *  18-Jun-2000 RMK	Removed virt_to_* and friends definitions
 */
#ifndef __ASM_ARM_IO_H
#define __ASM_ARM_IO_H

#ifdef __KERNEL__

#include <linux/types.h>
#include <asm/byteorder.h>
#include <asm/memory.h>
#include <asm/hardware.h>

/*
 * Generic IO read/write.  These perform native-endian accesses.  Note
 * that some architectures will want to re-define __raw_{read,write}w.
 */
extern void __raw_writesb(unsigned int addr, const void *data, int bytelen);
extern void __raw_writesw(unsigned int addr, const void *data, int wordlen);
extern void __raw_writesl(unsigned int addr, const void *data, int longlen);

extern void __raw_readsb(unsigned int addr, void *data, int bytelen);
extern void __raw_readsw(unsigned int addr, void *data, int wordlen);
extern void __raw_readsl(unsigned int addr, void *data, int longlen);

#define __raw_writeb(v,a)       (*(volatile unsigned char  *)(a) = (v))
#define __raw_writew(v,a)       (*(volatile unsigned short *)(a) = (v))
#define __raw_writel(v,a)       (*(volatile unsigned int   *)(a) = (v))

#define __raw_readb(a)          (*(volatile unsigned char  *)(a))
#define __raw_readw(a)          (*(volatile unsigned short *)(a))
#define __raw_readl(a)          (*(volatile unsigned int   *)(a))


/*
 * Bad read/write accesses...
 */
extern void __readwrite_bug(const char *fn);

/*
 * Now, pick up the machine-defined IO definitions
 */

#define IO_SPACE_LIMIT 0xffffffff

/*
 * GCC is totally crap at loading/storing data.  We try to persuade it
 * to do the right thing by using these whereever possible instead of
 * the above.
 */
#define __arch_base_getb(b,o)                   \
 ({                                             \
        unsigned int v, r = (b);                \
        __asm__ __volatile__(                   \
                "ldrb   %0, [%1, %2]"           \
                : "=r" (v)                      \
                : "r" (r), "Ir" (o));           \
        v;                                      \
 })

#define __arch_base_getl(b,o)                   \
 ({                                             \
        unsigned int v, r = (b);                \
        __asm__ __volatile__(                   \
                "ldr    %0, [%1, %2]"           \
                : "=r" (v)                      \
                : "r" (r), "Ir" (o));           \
        v;                                      \
 })

#define __arch_base_putb(v,b,o)                 \
 ({                                             \
        unsigned int r = (b);                   \
        __asm__ __volatile__(                   \
                "strb   %0, [%1, %2]"           \
                :                               \
                : "r" (v), "r" (r), "Ir" (o));  \
 })

#define __arch_base_putl(v,b,o)                 \
 ({                                             \
        unsigned int r = (b);                   \
        __asm__ __volatile__(                   \
                "str    %0, [%1, %2]"           \
                :                               \
                : "r" (v), "r" (r), "Ir" (o));  \
 })

/*
 * We use two different types of addressing - PC style addresses, and ARM
 * addresses.  PC style accesses the PC hardware with the normal PC IO
 * addresses, eg 0x3f8 for serial#1.  ARM addresses are 0x80000000+
 * and are translated to the start of IO.  Note that all addresses are
 * shifted left!
 */
#define __PORT_PCIO(x)  (!((x) & 0x80000000))

/*
 * Dynamic IO functions - let the compiler
 * optimize the expressions
 */
static inline void __outb (unsigned int value, unsigned int port)
{
        unsigned long temp;
        __asm__ __volatile__(
        "tst    %2, #0x80000000\n\t"
        "mov    %0, %4\n\t"
        "addeq  %0, %0, %3\n\t"
        "strb   %1, [%0, %2, lsl #2]    @ outb"
        : "=&r" (temp)
        : "r" (value), "r" (port), "Ir" (PCIO_BASE - IO_BASE), "Ir" (IO_BASE)
        : "cc");
}

static inline void __outw (unsigned int value, unsigned int port)
{
        unsigned long temp;
        __asm__ __volatile__(
        "tst    %2, #0x80000000\n\t"
        "mov    %0, %4\n\t"
        "addeq  %0, %0, %3\n\t"
        "str    %1, [%0, %2, lsl #2]    @ outw"
        : "=&r" (temp)
        : "r" (value|value<<16), "r" (port), "Ir" (PCIO_BASE - IO_BASE), "Ir" (IO_BASE)
        : "cc");
}

static inline void __outl (unsigned int value, unsigned int port)
{
        unsigned long temp;
        __asm__ __volatile__(
        "tst    %2, #0x80000000\n\t"
        "mov    %0, %4\n\t"
        "addeq  %0, %0, %3\n\t"
        "str    %1, [%0, %2, lsl #2]    @ outl"
        : "=&r" (temp)
        : "r" (value), "r" (port), "Ir" (PCIO_BASE - IO_BASE), "Ir" (IO_BASE)
        : "cc");
}

#define DECLARE_DYN_IN(sz,fnsuffix,instr)                                       \
static inline unsigned sz __in##fnsuffix (unsigned int port)            \
{                                                                               \
        unsigned long temp, value;                                              \
        __asm__ __volatile__(                                                   \
        "tst    %2, #0x80000000\n\t"                                            \
        "mov    %0, %4\n\t"                                                     \
        "addeq  %0, %0, %3\n\t"                                                 \
        "ldr" instr "   %1, [%0, %2, lsl #2]    @ in" #fnsuffix                 \
        : "=&r" (temp), "=r" (value)                                            \
        : "r" (port), "Ir" (PCIO_BASE - IO_BASE), "Ir" (IO_BASE)                \
        : "cc");                                                                \
        return (unsigned sz)value;                                              \
}

static inline unsigned int __ioaddr (unsigned int port)                 \
{                                                                               \
        if (__PORT_PCIO(port))                                                  \
                return (unsigned int)(PCIO_BASE + (port << 2));                 \
        else                                                                    \
                return (unsigned int)(IO_BASE + (port << 2));                   \
}

#define DECLARE_IO(sz,fnsuffix,instr)   \
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
#define __outbc(value,port)                                                     \
({                                                                              \
        if (__PORT_PCIO((port)))                                                \
                __asm__ __volatile__(                                           \
                "strb   %0, [%1, %2]    @ outbc"                                \
                : : "r" (value), "r" (PCIO_BASE), "Jr" ((port) << 2));          \
        else                                                                    \
                __asm__ __volatile__(                                           \
                "strb   %0, [%1, %2]    @ outbc"                                \
                : : "r" (value), "r" (IO_BASE), "r" ((port) << 2));             \
})

#define __inbc(port)                                                            \
({                                                                              \
        unsigned char result;                                                   \
        if (__PORT_PCIO((port)))                                                \
                __asm__ __volatile__(                                           \
                "ldrb   %0, [%1, %2]    @ inbc"                                 \
                : "=r" (result) : "r" (PCIO_BASE), "Jr" ((port) << 2));         \
        else                                                                    \
                __asm__ __volatile__(                                           \
                "ldrb   %0, [%1, %2]    @ inbc"                                 \
                : "=r" (result) : "r" (IO_BASE), "r" ((port) << 2));            \
        result;                                                                 \
})

#define __outwc(value,port)                                                     \
({                                                                              \
        unsigned long v = value;                                                \
        if (__PORT_PCIO((port)))                                                \
                __asm__ __volatile__(                                           \
                "str    %0, [%1, %2]    @ outwc"                                \
                : : "r" (v|v<<16), "r" (PCIO_BASE), "Jr" ((port) << 2));        \
        else                                                                    \
                __asm__ __volatile__(                                           \
                "str    %0, [%1, %2]    @ outwc"                                \
                : : "r" (v|v<<16), "r" (IO_BASE), "r" ((port) << 2));           \
})

#define __inwc(port)                                                            \
({                                                                              \
        unsigned short result;                                                  \
        if (__PORT_PCIO((port)))                                                \
                __asm__ __volatile__(                                           \
                "ldr    %0, [%1, %2]    @ inwc"                                 \
                : "=r" (result) : "r" (PCIO_BASE), "Jr" ((port) << 2));         \
        else                                                                    \
                __asm__ __volatile__(                                           \
                "ldr    %0, [%1, %2]    @ inwc"                                 \
                : "=r" (result) : "r" (IO_BASE), "r" ((port) << 2));            \
        result & 0xffff;                                                        \
})

#define __outlc(value,port)                                                     \
({                                                                              \
        unsigned long v = value;                                                \
        if (__PORT_PCIO((port)))                                                \
                __asm__ __volatile__(                                           \
                "str    %0, [%1, %2]    @ outlc"                                \
                : : "r" (v), "r" (PCIO_BASE), "Jr" ((port) << 2));              \
        else                                                                    \
                __asm__ __volatile__(                                           \
                "str    %0, [%1, %2]    @ outlc"                                \
                : : "r" (v), "r" (IO_BASE), "r" ((port) << 2));                 \
})

#define __inlc(port)                                                            \
({                                                                              \
        unsigned long result;                                                   \
        if (__PORT_PCIO((port)))                                                \
                __asm__ __volatile__(                                           \
                "ldr    %0, [%1, %2]    @ inlc"                                 \
                : "=r" (result) : "r" (PCIO_BASE), "Jr" ((port) << 2));         \
        else                                                                    \
                __asm__ __volatile__(                                           \
                "ldr    %0, [%1, %2]    @ inlc"                                 \
                : "=r" (result) : "r" (IO_BASE), "r" ((port) << 2));            \
        result;                                                                 \
})

#define __ioaddrc(port)                                                         \
({                                                                              \
        unsigned long addr;                                                     \
        if (__PORT_PCIO((port)))                                                \
                addr = PCIO_BASE + ((port) << 2);                               \
        else                                                                    \
                addr = IO_BASE + ((port) << 2);                                 \
        addr;                                                                   \
})

#define inb(p)          (__builtin_constant_p((p)) ? __inbc(p)    : __inb(p))
#define inw(p)          (__builtin_constant_p((p)) ? __inwc(p)    : __inw(p))
#define inl(p)          (__builtin_constant_p((p)) ? __inlc(p)    : __inl(p))
#define outb(v,p)       (__builtin_constant_p((p)) ? __outbc(v,p) : __outb(v,p))
#define outw(v,p)       (__builtin_constant_p((p)) ? __outwc(v,p) : __outw(v,p))
#define outl(v,p)       (__builtin_constant_p((p)) ? __outlc(v,p) : __outl(v,p))
#define __ioaddr(p)     (__builtin_constant_p((p)) ? __ioaddr(p)  : __ioaddrc(p))

/* JMA 18.02.03 added sb,sl from arm/io.h, changing io to ioaddr */

#define outsb(p,d,l)            __raw_writesb(__ioaddr(p),d,l)
#define outsw(p,d,l)            __raw_writesw(__ioaddr(p),d,l)
#define outsl(p,d,l)            __raw_writesl(__ioaddr(p),d,l)

#define insb(p,d,l)             __raw_readsb(__ioaddr(p),d,l)
#define insw(p,d,l)             __raw_readsw(__ioaddr(p),d,l)
#define insl(p,d,l)             __raw_readsl(__ioaddr(p),d,l)

#define insw(p,d,l)     __raw_readsw(__ioaddr(p),d,l)
#define outsw(p,d,l)    __raw_writesw(__ioaddr(p),d,l)

#define readb(c)                        (__readwrite_bug("readb"),0)
#define readw(c)                        (__readwrite_bug("readw"),0)
#define readl(c)                        (__readwrite_bug("readl"),0)
#define readb_relaxed(addr)		readb(addr)
#define readw_relaxed(addr)		readw(addr)
#define readl_relaxed(addr)		readl(addr)
#define writeb(v,c)                     __readwrite_bug("writeb")
#define writew(v,c)                     __readwrite_bug("writew")
#define writel(v,c)                     __readwrite_bug("writel")

#define readsw(p,d,l)                 (__readwrite_bug("readsw"),0)
#define readsl(p,d,l)                 (__readwrite_bug("readsl"),0)
#define writesw(p,d,l)                        __readwrite_bug("writesw")
#define writesl(p,d,l)                        __readwrite_bug("writesl")

#define mmiowb()

/* the following macro is deprecated */
#define ioaddr(port)                    __ioaddr((port))

/*
 * No ioremap support here.
 */
#define __arch_ioremap(c,s,f,a)   ((void *)(c))
#define __arch_iounmap(c)       do { }  while (0)


#if defined(__arch_putb) || defined(__arch_putw) || defined(__arch_putl) || \
    defined(__arch_getb) || defined(__arch_getw) || defined(__arch_getl)
#warning machine class uses old __arch_putw or __arch_getw
#endif

/*
 *  IO port access primitives
 *  -------------------------
 *
 * The ARM doesn't have special IO access instructions; all IO is memory
 * mapped.  Note that these are defined to perform little endian accesses
 * only.  Their primary purpose is to access PCI and ISA peripherals.
 *
 * Note that for a big endian machine, this implies that the following
 * big endian mode connectivity is in place, as described by numerious
 * ARM documents:
 *
 *    PCI:  D0-D7   D8-D15 D16-D23 D24-D31
 *    ARM: D24-D31 D16-D23  D8-D15  D0-D7
 *
 * The machine specific io.h include defines __io to translate an "IO"
 * address to a memory address.
 *
 * Note that we prevent GCC re-ordering or caching values in expressions
 * by introducing sequence points into the in*() definitions.  Note that
 * __raw_* do not guarantee this behaviour.
 */
/*
#define outsb(p,d,l)		__raw_writesb(__io(p),d,l)
#define outsw(p,d,l)		__raw_writesw(__io(p),d,l)

#define insb(p,d,l)		__raw_readsb(__io(p),d,l)
#define insw(p,d,l)		__raw_readsw(__io(p),d,l)
*/
#define outb_p(val,port)	outb((val),(port))
#define outw_p(val,port)	outw((val),(port))
#define inb_p(port)		inb((port))
#define inw_p(port)		inw((port))
#define inl_p(port)		inl((port))

#define outsb_p(port,from,len)	outsb(port,from,len)
#define outsw_p(port,from,len)	outsw(port,from,len)
#define insb_p(port,to,len)	insb(port,to,len)
#define insw_p(port,to,len)	insw(port,to,len)

/*
 * String version of IO memory access ops:
 */
extern void _memcpy_fromio(void *, unsigned long, size_t);
extern void _memcpy_toio(unsigned long, const void *, size_t);
extern void _memset_io(unsigned long, int, size_t);

/*
 * ioremap and friends.
 *
 * ioremap takes a PCI memory address, as specified in
 * Documentation/IO-mapping.txt.
 */
extern void * __ioremap(unsigned long, size_t, unsigned long, unsigned long);
extern void __iounmap(void *addr);

#ifndef __arch_ioremap
#define ioremap(cookie,size)		__ioremap(cookie,size,0,1)
#define ioremap_nocache(cookie,size)	__ioremap(cookie,size,0,1)
#define iounmap(cookie)			__iounmap(cookie)
#else
#define ioremap(cookie,size)		__arch_ioremap((cookie),(size),0,1)
#define ioremap_nocache(cookie,size)	__arch_ioremap((cookie),(size),0,1)
#define iounmap(cookie)			__arch_iounmap(cookie)
#endif

/*
 * DMA-consistent mapping functions.  These allocate/free a region of
 * uncached, unwrite-buffered mapped memory space for use with DMA
 * devices.  This is the "generic" version.  The PCI specific version
 * is in pci.h
 */
extern void *consistent_alloc(int gfp, size_t size, dma_addr_t *handle);
extern void consistent_free(void *vaddr, size_t size, dma_addr_t handle);
extern void consistent_sync(void *vaddr, size_t size, int rw);

/*
 * can the hardware map this into one segment or not, given no other
 * constraints.
 */
#define BIOVEC_MERGEABLE(vec1, vec2)	\
	((bvec_to_phys((vec1)) + (vec1)->bv_len) == bvec_to_phys((vec2)))

/*
 * Convert a physical pointer to a virtual kernel pointer for /dev/mem
 * access
 */
#define xlate_dev_mem_ptr(p)	__va(p)

/*
 * Convert a virtual cached pointer to an uncached pointer
 */
#define xlate_dev_kmem_ptr(p)	p

#endif	/* __KERNEL__ */
#endif	/* __ASM_ARM_IO_H */
