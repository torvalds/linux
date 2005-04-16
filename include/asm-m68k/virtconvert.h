#ifndef __VIRT_CONVERT__
#define __VIRT_CONVERT__

/*
 * Macros used for converting between virtual and physical mappings.
 */

#ifdef __KERNEL__

#include <linux/config.h>
#include <linux/compiler.h>
#include <asm/setup.h>
#include <asm/page.h>

#ifdef CONFIG_AMIGA
#include <asm/amigahw.h>
#endif

/*
 * Change virtual addresses to physical addresses and vv.
 */
#ifndef CONFIG_SUN3
extern unsigned long mm_vtop(unsigned long addr) __attribute_const__;
extern unsigned long mm_ptov(unsigned long addr) __attribute_const__;
#else
static inline unsigned long mm_vtop(unsigned long vaddr)
{
	return __pa(vaddr);
}

static inline unsigned long mm_ptov(unsigned long paddr)
{
	return (unsigned long)__va(paddr);
}
#endif

#ifdef CONFIG_SINGLE_MEMORY_CHUNK
static inline unsigned long virt_to_phys(void *vaddr)
{
	return (unsigned long)vaddr - PAGE_OFFSET + m68k_memory[0].addr;
}

static inline void * phys_to_virt(unsigned long paddr)
{
	return (void *)(paddr - m68k_memory[0].addr + PAGE_OFFSET);
}
#else
static inline unsigned long virt_to_phys(void *address)
{
	return mm_vtop((unsigned long)address);
}

static inline void *phys_to_virt(unsigned long address)
{
	return (void *) mm_ptov(address);
}
#endif

/* Permanent address of a page. */
#define __page_address(page)	(PAGE_OFFSET + (((page) - mem_map) << PAGE_SHIFT))
#define page_to_phys(page)	virt_to_phys((void *)__page_address(page))

/*
 * IO bus memory addresses are 1:1 with the physical address,
 * except on the PCI bus of the Hades.
 */
#ifdef CONFIG_HADES
#define virt_to_bus(a) (virt_to_phys(a) + (MACH_IS_HADES ? 0x80000000 : 0))
#define bus_to_virt(a) (phys_to_virt((a) - (MACH_IS_HADES ? 0x80000000 : 0)))
#else
#define virt_to_bus virt_to_phys
#define bus_to_virt phys_to_virt
#endif

#endif
#endif
