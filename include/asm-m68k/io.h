/*
 * linux/include/asm-m68k/io.h
 *
 * 4/1/00 RZ: - rewritten to avoid clashes between ISA/PCI and other
 *              IO access
 *            - added Q40 support
 *            - added skeleton for GG-II and Amiga PCMCIA
 * 2/3/01 RZ: - moved a few more defs into raw_io.h
 *
 * inX/outX/readX/writeX should not be used by any driver unless it does
 * ISA or PCI access. Other drivers should use function defined in raw_io.h
 * or define its own macros on top of these.
 *
 *    inX(),outX()              are for PCI and ISA I/O
 *    readX(),writeX()          are for PCI memory
 *    isa_readX(),isa_writeX()  are for ISA memory
 *
 * moved mem{cpy,set}_*io inside CONFIG_PCI
 */

#ifndef _IO_H
#define _IO_H

#ifdef __KERNEL__

#include <linux/config.h>
#include <linux/compiler.h>
#include <asm/raw_io.h>
#include <asm/virtconvert.h>


#ifdef CONFIG_ATARI
#include <asm/atarihw.h>
#endif


/*
 * IO/MEM definitions for various ISA bridges
 */


#ifdef CONFIG_Q40

#define q40_isa_io_base  0xff400000
#define q40_isa_mem_base 0xff800000

#define Q40_ISA_IO_B(ioaddr) (q40_isa_io_base+1+4*((unsigned long)(ioaddr)))
#define Q40_ISA_IO_W(ioaddr) (q40_isa_io_base+  4*((unsigned long)(ioaddr)))
#define Q40_ISA_MEM_B(madr)  (q40_isa_mem_base+1+4*((unsigned long)(madr)))
#define Q40_ISA_MEM_W(madr)  (q40_isa_mem_base+  4*((unsigned long)(madr)))

#define MULTI_ISA 0
#endif /* Q40 */

/* GG-II Zorro to ISA bridge */
#ifdef CONFIG_GG2

extern unsigned long gg2_isa_base;
#define GG2_ISA_IO_B(ioaddr) (gg2_isa_base+1+((unsigned long)(ioaddr)*4))
#define GG2_ISA_IO_W(ioaddr) (gg2_isa_base+  ((unsigned long)(ioaddr)*4))
#define GG2_ISA_MEM_B(madr)  (gg2_isa_base+1+(((unsigned long)(madr)*4) & 0xfffff))
#define GG2_ISA_MEM_W(madr)  (gg2_isa_base+  (((unsigned long)(madr)*4) & 0xfffff))

#ifndef MULTI_ISA
#define MULTI_ISA 0
#else
#undef MULTI_ISA
#define MULTI_ISA 1
#endif
#endif /* GG2 */

#ifdef CONFIG_AMIGA_PCMCIA
#include <asm/amigayle.h>

#define AG_ISA_IO_B(ioaddr) ( GAYLE_IO+(ioaddr)+(((ioaddr)&1)*GAYLE_ODD) )
#define AG_ISA_IO_W(ioaddr) ( GAYLE_IO+(ioaddr) )

#ifndef MULTI_ISA
#define MULTI_ISA 0
#else
#undef MULTI_ISA
#define MULTI_ISA 1
#endif
#endif /* AMIGA_PCMCIA */



#ifdef CONFIG_ISA

#if MULTI_ISA == 0
#undef MULTI_ISA
#endif

#define Q40_ISA (1)
#define GG2_ISA (2)
#define AG_ISA  (3)

#if defined(CONFIG_Q40) && !defined(MULTI_ISA)
#define ISA_TYPE Q40_ISA
#define ISA_SEX  0
#endif
#if defined(CONFIG_AMIGA_PCMCIA) && !defined(MULTI_ISA)
#define ISA_TYPE AG_ISA
#define ISA_SEX  1
#endif
#if defined(CONFIG_GG2) && !defined(MULTI_ISA)
#define ISA_TYPE GG2_ISA
#define ISA_SEX  0
#endif

#ifdef MULTI_ISA
extern int isa_type;
extern int isa_sex;

#define ISA_TYPE isa_type
#define ISA_SEX  isa_sex
#endif

/*
 * define inline addr translation functions. Normally only one variant will
 * be compiled in so the case statement will be optimised away
 */

static inline u8 __iomem *isa_itb(unsigned long addr)
{
  switch(ISA_TYPE)
    {
#ifdef CONFIG_Q40
    case Q40_ISA: return (u8 __iomem *)Q40_ISA_IO_B(addr);
#endif
#ifdef CONFIG_GG2
    case GG2_ISA: return (u8 __iomem *)GG2_ISA_IO_B(addr);
#endif
#ifdef CONFIG_AMIGA_PCMCIA
    case AG_ISA: return (u8 __iomem *)AG_ISA_IO_B(addr);
#endif
    default: return NULL; /* avoid warnings, just in case */
    }
}
static inline u16 __iomem *isa_itw(unsigned long addr)
{
  switch(ISA_TYPE)
    {
#ifdef CONFIG_Q40
    case Q40_ISA: return (u16 __iomem *)Q40_ISA_IO_W(addr);
#endif
#ifdef CONFIG_GG2
    case GG2_ISA: return (u16 __iomem *)GG2_ISA_IO_W(addr);
#endif
#ifdef CONFIG_AMIGA_PCMCIA
    case AG_ISA: return (u16 __iomem *)AG_ISA_IO_W(addr);
#endif
    default: return NULL; /* avoid warnings, just in case */
    }
}
static inline u8 __iomem *isa_mtb(unsigned long addr)
{
  switch(ISA_TYPE)
    {
#ifdef CONFIG_Q40
    case Q40_ISA: return (u8 __iomem *)Q40_ISA_MEM_B(addr);
#endif
#ifdef CONFIG_GG2
    case GG2_ISA: return (u8 __iomem *)GG2_ISA_MEM_B(addr);
#endif
#ifdef CONFIG_AMIGA_PCMCIA
    case AG_ISA: return (u8 __iomem *)addr;
#endif
    default: return NULL; /* avoid warnings, just in case */
    }
}
static inline u16 __iomem *isa_mtw(unsigned long addr)
{
  switch(ISA_TYPE)
    {
#ifdef CONFIG_Q40
    case Q40_ISA: return (u16 __iomem *)Q40_ISA_MEM_W(addr);
#endif
#ifdef CONFIG_GG2
    case GG2_ISA: return (u16 __iomem *)GG2_ISA_MEM_W(addr);
#endif
#ifdef CONFIG_AMIGA_PCMCIA
    case AG_ISA: return (u16 __iomem *)addr;
#endif
    default: return NULL; /* avoid warnings, just in case */
    }
}


#define isa_inb(port)      in_8(isa_itb(port))
#define isa_inw(port)      (ISA_SEX ? in_be16(isa_itw(port)) : in_le16(isa_itw(port)))
#define isa_outb(val,port) out_8(isa_itb(port),(val))
#define isa_outw(val,port) (ISA_SEX ? out_be16(isa_itw(port),(val)) : out_le16(isa_itw(port),(val)))

#define isa_readb(p)       in_8(isa_mtb((unsigned long)(p)))
#define isa_readw(p)       \
	(ISA_SEX ? in_be16(isa_mtw((unsigned long)(p)))	\
		 : in_le16(isa_mtw((unsigned long)(p))))
#define isa_writeb(val,p)  out_8(isa_mtb((unsigned long)(p)),(val))
#define isa_writew(val,p)  \
	(ISA_SEX ? out_be16(isa_mtw((unsigned long)(p)),(val))	\
		 : out_le16(isa_mtw((unsigned long)(p)),(val)))

static inline void isa_delay(void)
{
  switch(ISA_TYPE)
    {
#ifdef CONFIG_Q40
    case Q40_ISA: isa_outb(0,0x80); break;
#endif
#ifdef CONFIG_GG2
    case GG2_ISA: break;
#endif
#ifdef CONFIG_AMIGA_PCMCIA
    case AG_ISA: break;
#endif
    default: break; /* avoid warnings */
    }
}

#define isa_inb_p(p)      ({u8 v=isa_inb(p);isa_delay();v;})
#define isa_outb_p(v,p)   ({isa_outb((v),(p));isa_delay();})
#define isa_inw_p(p)      ({u16 v=isa_inw(p);isa_delay();v;})
#define isa_outw_p(v,p)   ({isa_outw((v),(p));isa_delay();})
#define isa_inl_p(p)      ({u32 v=isa_inl(p);isa_delay();v;})
#define isa_outl_p(v,p)   ({isa_outl((v),(p));isa_delay();})

#define isa_insb(port, buf, nr) raw_insb(isa_itb(port), (u8 *)(buf), (nr))
#define isa_outsb(port, buf, nr) raw_outsb(isa_itb(port), (u8 *)(buf), (nr))

#define isa_insw(port, buf, nr)     \
       (ISA_SEX ? raw_insw(isa_itw(port), (u16 *)(buf), (nr)) :    \
                  raw_insw_swapw(isa_itw(port), (u16 *)(buf), (nr)))

#define isa_outsw(port, buf, nr)    \
       (ISA_SEX ? raw_outsw(isa_itw(port), (u16 *)(buf), (nr)) :  \
                  raw_outsw_swapw(isa_itw(port), (u16 *)(buf), (nr)))
#endif  /* CONFIG_ISA */


#if defined(CONFIG_ISA) && !defined(CONFIG_PCI)
#define inb     isa_inb
#define inb_p   isa_inb_p
#define outb    isa_outb
#define outb_p  isa_outb_p
#define inw     isa_inw
#define inw_p   isa_inw_p
#define outw    isa_outw
#define outw_p  isa_outw_p
#define inl     isa_inw
#define inl_p   isa_inw_p
#define outl    isa_outw
#define outl_p  isa_outw_p
#define insb    isa_insb
#define insw    isa_insw
#define outsb   isa_outsb
#define outsw   isa_outsw
#define readb   isa_readb
#define readw   isa_readw
#define writeb  isa_writeb
#define writew  isa_writew
#endif /* CONFIG_ISA */

#if defined(CONFIG_PCI)

#define inl(port)        in_le32(port)
#define outl(val,port)   out_le32((port),(val))
#define readl(addr)      in_le32(addr)
#define writel(val,addr) out_le32((addr),(val))

/* those can be defined for both ISA and PCI - it won't work though */
#define readb(addr)       in_8(addr)
#define readw(addr)       in_le16(addr)
#define writeb(val,addr)  out_8((addr),(val))
#define writew(val,addr)  out_le16((addr),(val))

#define readb_relaxed(addr) readb(addr)
#define readw_relaxed(addr) readw(addr)
#define readl_relaxed(addr) readl(addr)

#ifndef CONFIG_ISA
#define inb(port)      in_8(port)
#define outb(val,port) out_8((port),(val))
#define inw(port)      in_le16(port)
#define outw(val,port) out_le16((port),(val))

#else
/*
 * kernel with both ISA and PCI compiled in, those have
 * conflicting defs for in/out. Simply consider port < 1024
 * ISA and everything else PCI. read,write not defined
 * in this case
 */
#define inb(port) ((port)<1024 ? isa_inb(port) : in_8(port))
#define inb_p(port) ((port)<1024 ? isa_inb_p(port) : in_8(port))
#define inw(port) ((port)<1024 ? isa_inw(port) : in_le16(port))
#define inw_p(port) ((port)<1024 ? isa_inw_p(port) : in_le16(port))
#define inl(port) ((port)<1024 ? isa_inl(port) : in_le32(port))
#define inl_p(port) ((port)<1024 ? isa_inl_p(port) : in_le32(port))

#define outb(val,port) ((port)<1024 ? isa_outb((val),(port)) : out_8((port),(val)))
#define outb_p(val,port) ((port)<1024 ? isa_outb_p((val),(port)) : out_8((port),(val)))
#define outw(val,port) ((port)<1024 ? isa_outw((val),(port)) : out_le16((port),(val)))
#define outw_p(val,port) ((port)<1024 ? isa_outw_p((val),(port)) : out_le16((port),(val)))
#define outl(val,port) ((port)<1024 ? isa_outl((val),(port)) : out_le32((port),(val)))
#define outl_p(val,port) ((port)<1024 ? isa_outl_p((val),(port)) : out_le32((port),(val)))
#endif
#endif /* CONFIG_PCI */

#if !defined(CONFIG_ISA) && !defined(CONFIG_PCI) && defined(CONFIG_HP300)
/*
 * We need to define dummy functions otherwise drivers/serial/8250.c doesn't link
 */
#define inb(port)        0xff
#define inb_p(port)      0xff
#define outb(val,port)   do { } while (0)
#define outb_p(val,port) do { } while (0)

/*
 * These should be valid on any ioremap()ed region
 */
#define readb(addr)      in_8(addr)
#define writeb(val,addr) out_8((addr),(val))
#define readl(addr)      in_le32(addr)
#define writel(val,addr) out_le32((addr),(val))
#endif

#define mmiowb()

static inline void __iomem *ioremap(unsigned long physaddr, unsigned long size)
{
	return __ioremap(physaddr, size, IOMAP_NOCACHE_SER);
}
static inline void __iomem *ioremap_nocache(unsigned long physaddr, unsigned long size)
{
	return __ioremap(physaddr, size, IOMAP_NOCACHE_SER);
}
static inline void __iomem *ioremap_writethrough(unsigned long physaddr,
					 unsigned long size)
{
	return __ioremap(physaddr, size, IOMAP_WRITETHROUGH);
}
static inline void __iomem *ioremap_fullcache(unsigned long physaddr,
				      unsigned long size)
{
	return __ioremap(physaddr, size, IOMAP_FULL_CACHING);
}


/* m68k caches aren't DMA coherent */
extern void dma_cache_wback_inv(unsigned long start, unsigned long size);
extern void dma_cache_wback(unsigned long start, unsigned long size);
extern void dma_cache_inv(unsigned long start, unsigned long size);


#ifndef CONFIG_SUN3
#define IO_SPACE_LIMIT 0xffff
#else
#define IO_SPACE_LIMIT 0x0fffffff
#endif

#endif /* __KERNEL__ */

#define __ARCH_HAS_NO_PAGE_ZERO_MAPPED		1

/*
 * Convert a physical pointer to a virtual kernel pointer for /dev/mem
 * access
 */
#define xlate_dev_mem_ptr(p)	__va(p)

/*
 * Convert a virtual cached pointer to an uncached pointer
 */
#define xlate_dev_kmem_ptr(p)	p

#endif /* _IO_H */
