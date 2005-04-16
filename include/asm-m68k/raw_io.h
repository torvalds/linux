/*
 * linux/include/asm-m68k/raw_io.h
 *
 * 10/20/00 RZ: - created from bits of io.h and ide.h to cleanup namespace
 *
 */

#ifndef _RAW_IO_H
#define _RAW_IO_H

#ifdef __KERNEL__

#include <asm/types.h>


/* Values for nocacheflag and cmode */
#define IOMAP_FULL_CACHING		0
#define IOMAP_NOCACHE_SER		1
#define IOMAP_NOCACHE_NONSER		2
#define IOMAP_WRITETHROUGH		3

extern void iounmap(void *addr);

extern void *__ioremap(unsigned long physaddr, unsigned long size,
		       int cacheflag);
extern void __iounmap(void *addr, unsigned long size);


/* ++roman: The assignments to temp. vars avoid that gcc sometimes generates
 * two accesses to memory, which may be undesirable for some devices.
 */
#define in_8(addr) \
    ({ u8 __v = (*(volatile u8 *) (addr)); __v; })
#define in_be16(addr) \
    ({ u16 __v = (*(volatile u16 *) (addr)); __v; })
#define in_be32(addr) \
    ({ u32 __v = (*(volatile u32 *) (addr)); __v; })
#define in_le16(addr) \
    ({ u16 __v = le16_to_cpu(*(volatile u16 *) (addr)); __v; })
#define in_le32(addr) \
    ({ u32 __v = le32_to_cpu(*(volatile u32 *) (addr)); __v; })

#define out_8(addr,b) (void)((*(volatile u8 *) (addr)) = (b))
#define out_be16(addr,w) (void)((*(volatile u16 *) (addr)) = (w))
#define out_be32(addr,l) (void)((*(volatile u32 *) (addr)) = (l))
#define out_le16(addr,w) (void)((*(volatile u16 *) (addr)) = cpu_to_le16(w))
#define out_le32(addr,l) (void)((*(volatile u32 *) (addr)) = cpu_to_le32(l))

#define raw_inb in_8
#define raw_inw in_be16
#define raw_inl in_be32

#define raw_outb(val,port) out_8((port),(val))
#define raw_outw(val,port) out_be16((port),(val))
#define raw_outl(val,port) out_be32((port),(val))

static inline void raw_insb(volatile u8 *port, u8 *buf, unsigned int len)
{
	unsigned int i;

        for (i = 0; i < len; i++)
		*buf++ = in_8(port);
}

static inline void raw_outsb(volatile u8 *port, const u8 *buf,
			     unsigned int len)
{
	unsigned int i;

        for (i = 0; i < len; i++)
		out_8(port, *buf++);
}

static inline void raw_insw(volatile u16 *port, u16 *buf, unsigned int nr)
{
	unsigned int tmp;

	if (nr & 15) {
		tmp = (nr & 15) - 1;
		asm volatile (
			"1: movew %2@,%0@+; dbra %1,1b"
			: "=a" (buf), "=d" (tmp)
			: "a" (port), "0" (buf),
			  "1" (tmp));
	}
	if (nr >> 4) {
		tmp = (nr >> 4) - 1;
		asm volatile (
			"1: "
			"movew %2@,%0@+; "
			"movew %2@,%0@+; "
			"movew %2@,%0@+; "
			"movew %2@,%0@+; "
			"movew %2@,%0@+; "
			"movew %2@,%0@+; "
			"movew %2@,%0@+; "
			"movew %2@,%0@+; "
			"movew %2@,%0@+; "
			"movew %2@,%0@+; "
			"movew %2@,%0@+; "
			"movew %2@,%0@+; "
			"movew %2@,%0@+; "
			"movew %2@,%0@+; "
			"movew %2@,%0@+; "
			"movew %2@,%0@+; "
			"dbra %1,1b"
			: "=a" (buf), "=d" (tmp)
			: "a" (port), "0" (buf),
			  "1" (tmp));
	}
}

static inline void raw_outsw(volatile u16 *port, const u16 *buf,
			     unsigned int nr)
{
	unsigned int tmp;

	if (nr & 15) {
		tmp = (nr & 15) - 1;
		asm volatile (
			"1: movew %0@+,%2@; dbra %1,1b"
			: "=a" (buf), "=d" (tmp)
			: "a" (port), "0" (buf),
			  "1" (tmp));
	}
	if (nr >> 4) {
		tmp = (nr >> 4) - 1;
		asm volatile (
			"1: "
			"movew %0@+,%2@; "
			"movew %0@+,%2@; "
			"movew %0@+,%2@; "
			"movew %0@+,%2@; "
			"movew %0@+,%2@; "
			"movew %0@+,%2@; "
			"movew %0@+,%2@; "
			"movew %0@+,%2@; "
			"movew %0@+,%2@; "
			"movew %0@+,%2@; "
			"movew %0@+,%2@; "
			"movew %0@+,%2@; "
			"movew %0@+,%2@; "
			"movew %0@+,%2@; "
			"movew %0@+,%2@; "
			"movew %0@+,%2@; "
			"dbra %1,1b"
			: "=a" (buf), "=d" (tmp)
			: "a" (port), "0" (buf),
			  "1" (tmp));
	}
}

static inline void raw_insl(volatile u32 *port, u32 *buf, unsigned int nr)
{
	unsigned int tmp;

	if (nr & 15) {
		tmp = (nr & 15) - 1;
		asm volatile (
			"1: movel %2@,%0@+; dbra %1,1b"
			: "=a" (buf), "=d" (tmp)
			: "a" (port), "0" (buf),
			  "1" (tmp));
	}
	if (nr >> 4) {
		tmp = (nr >> 4) - 1;
		asm volatile (
			"1: "
			"movel %2@,%0@+; "
			"movel %2@,%0@+; "
			"movel %2@,%0@+; "
			"movel %2@,%0@+; "
			"movel %2@,%0@+; "
			"movel %2@,%0@+; "
			"movel %2@,%0@+; "
			"movel %2@,%0@+; "
			"movel %2@,%0@+; "
			"movel %2@,%0@+; "
			"movel %2@,%0@+; "
			"movel %2@,%0@+; "
			"movel %2@,%0@+; "
			"movel %2@,%0@+; "
			"movel %2@,%0@+; "
			"movel %2@,%0@+; "
			"dbra %1,1b"
			: "=a" (buf), "=d" (tmp)
			: "a" (port), "0" (buf),
			  "1" (tmp));
	}
}

static inline void raw_outsl(volatile u32 *port, const u32 *buf,
			     unsigned int nr)
{
	unsigned int tmp;

	if (nr & 15) {
		tmp = (nr & 15) - 1;
		asm volatile (
			"1: movel %0@+,%2@; dbra %1,1b"
			: "=a" (buf), "=d" (tmp)
			: "a" (port), "0" (buf),
			  "1" (tmp));
	}
	if (nr >> 4) {
		tmp = (nr >> 4) - 1;
		asm volatile (
			"1: "
			"movel %0@+,%2@; "
			"movel %0@+,%2@; "
			"movel %0@+,%2@; "
			"movel %0@+,%2@; "
			"movel %0@+,%2@; "
			"movel %0@+,%2@; "
			"movel %0@+,%2@; "
			"movel %0@+,%2@; "
			"movel %0@+,%2@; "
			"movel %0@+,%2@; "
			"movel %0@+,%2@; "
			"movel %0@+,%2@; "
			"movel %0@+,%2@; "
			"movel %0@+,%2@; "
			"movel %0@+,%2@; "
			"movel %0@+,%2@; "
			"dbra %1,1b"
			: "=a" (buf), "=d" (tmp)
			: "a" (port), "0" (buf),
			  "1" (tmp));
	}
}


static inline void raw_insw_swapw(volatile u16 *port, u16 *buf,
				  unsigned int nr)
{
    if ((nr) % 8)
	__asm__ __volatile__
	       ("\tmovel %0,%/a0\n\t"
		"movel %1,%/a1\n\t"
		"movel %2,%/d6\n\t"
		"subql #1,%/d6\n"
		"1:\tmovew %/a0@,%/d0\n\t"
		"rolw  #8,%/d0\n\t"
		"movew %/d0,%/a1@+\n\t"
		"dbra %/d6,1b"
		:
		: "g" (port), "g" (buf), "g" (nr)
		: "d0", "a0", "a1", "d6");
    else
	__asm__ __volatile__
	       ("movel %0,%/a0\n\t"
		"movel %1,%/a1\n\t"
		"movel %2,%/d6\n\t"
		"lsrl  #3,%/d6\n\t"
		"subql #1,%/d6\n"
		"1:\tmovew %/a0@,%/d0\n\t"
		"rolw  #8,%/d0\n\t"
		"movew %/d0,%/a1@+\n\t"
		"movew %/a0@,%/d0\n\t"
		"rolw  #8,%/d0\n\t"
		"movew %/d0,%/a1@+\n\t"
		"movew %/a0@,%/d0\n\t"
		"rolw  #8,%/d0\n\t"
		"movew %/d0,%/a1@+\n\t"
		"movew %/a0@,%/d0\n\t"
		"rolw  #8,%/d0\n\t"
		"movew %/d0,%/a1@+\n\t"
		"movew %/a0@,%/d0\n\t"
		"rolw  #8,%/d0\n\t"
		"movew %/d0,%/a1@+\n\t"
		"movew %/a0@,%/d0\n\t"
		"rolw  #8,%/d0\n\t"
		"movew %/d0,%/a1@+\n\t"
		"movew %/a0@,%/d0\n\t"
		"rolw  #8,%/d0\n\t"
		"movew %/d0,%/a1@+\n\t"
		"movew %/a0@,%/d0\n\t"
		"rolw  #8,%/d0\n\t"
		"movew %/d0,%/a1@+\n\t"
		"dbra %/d6,1b"
                :
		: "g" (port), "g" (buf), "g" (nr)
		: "d0", "a0", "a1", "d6");
}

static inline void raw_outsw_swapw(volatile u16 *port, const u16 *buf,
				   unsigned int nr)
{
    if ((nr) % 8)
	__asm__ __volatile__
	       ("movel %0,%/a0\n\t"
		"movel %1,%/a1\n\t"
		"movel %2,%/d6\n\t"
		"subql #1,%/d6\n"
		"1:\tmovew %/a1@+,%/d0\n\t"
		"rolw  #8,%/d0\n\t"
		"movew %/d0,%/a0@\n\t"
		"dbra %/d6,1b"
                :
		: "g" (port), "g" (buf), "g" (nr)
		: "d0", "a0", "a1", "d6");
    else
	__asm__ __volatile__
	       ("movel %0,%/a0\n\t"
		"movel %1,%/a1\n\t"
		"movel %2,%/d6\n\t"
		"lsrl  #3,%/d6\n\t"
		"subql #1,%/d6\n"
		"1:\tmovew %/a1@+,%/d0\n\t"
		"rolw  #8,%/d0\n\t"
		"movew %/d0,%/a0@\n\t"
		"movew %/a1@+,%/d0\n\t"
		"rolw  #8,%/d0\n\t"
		"movew %/d0,%/a0@\n\t"
		"movew %/a1@+,%/d0\n\t"
		"rolw  #8,%/d0\n\t"
		"movew %/d0,%/a0@\n\t"
		"movew %/a1@+,%/d0\n\t"
		"rolw  #8,%/d0\n\t"
		"movew %/d0,%/a0@\n\t"
		"movew %/a1@+,%/d0\n\t"
		"rolw  #8,%/d0\n\t"
		"movew %/d0,%/a0@\n\t"
		"movew %/a1@+,%/d0\n\t"
		"rolw  #8,%/d0\n\t"
		"movew %/d0,%/a0@\n\t"
		"movew %/a1@+,%/d0\n\t"
		"rolw  #8,%/d0\n\t"
		"movew %/d0,%/a0@\n\t"
		"movew %/a1@+,%/d0\n\t"
		"rolw  #8,%/d0\n\t"
		"movew %/d0,%/a0@\n\t"
		"dbra %/d6,1b"
                :
		: "g" (port), "g" (buf), "g" (nr)
		: "d0", "a0", "a1", "d6");
}


#endif /* __KERNEL__ */

#endif /* _RAW_IO_H */
