/*
 * Copyright 2013 Texas Instruments, Inc.
 *      Russ Dill <russ.dill@ti.com>
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms and conditions of the GNU General Public License,
 * version 2, as published by the Free Software Foundation.
 */

#ifndef _LINUX_PIE_H
#define _LINUX_PIE_H

#include <linux/kernel.h>
#include <linux/err.h>

#include <asm/fncpy.h>
#include <asm/bug.h>

struct gen_pool;
struct pie_chunk;

/**
 * pie_arch_fixup - arch specific fixups of copied PIE code
 * @chunk:	identifier to be used with kern_to_pie/pie_to_phys
 * @base:	virtual address of start of copied PIE section
 * @tail:	virtual address of tail data in copied PIE
 * @offset:	offset to apply to relocation entries.
 *
 * When this code is done executing, it should be possible to jump to code
 * so long as it is located at the given offset.
 */
extern int pie_arch_fixup(struct pie_chunk *chunk, void *base, void *tail,
							unsigned long offset);

/**
 * pie_arch_fill_tail - arch specific tail information for copied PIE
 * @tail:		virtual address of tail data in copied PIE to be filled
 * @common_start:	virtual address of common code within kernel data
 * @common_end:		virtual end address of common code within kernel data
 * @overlay_start:	virtual address of first overlay within kernel data
 * @code_start:		virtual address of this overlay within kernel data
 * @code_end:		virtual end address of this overlay within kernel data
 *
 * Fill tail data with data necessary to for pie_arch_fixup to perform
 * relocations. If tail is NULL, do not update data, but still calculate
 * the number of bytes required.
 *
 * Returns number of bytes required/used for tail on success, -EERROR otherwise.
 */
extern int pie_arch_fill_tail(void *tail, void *common_start, void *common_end,
			void *overlay_start, void *code_start, void *code_end,
			void *rel_start, void *rel_end);

#ifdef CONFIG_PIE

/**
 * __pie_load_data - load and fixup PIE code from kernel data
 * @pool:	pool to allocate memory from and copy code into
 * @start:	virtual start address in kernel of chunk specific code
 * @end:	virtual end address in kernel of chunk specific code
 * @phys:	%true to fixup to physical address of destination, %false to
 *		fixup to virtual address of destination
 *
 * Returns 0 on success, -EERROR otherwise
 */
extern struct pie_chunk *__pie_load_data(struct gen_pool *pool, bool phys,
				void *start, void *end,
				void *rel_start, void *rel_end);

/**
 * pie_to_phys - translate a virtual PIE address into a physical one
 * @chunk:	identifier returned by pie_load_sections
 * @addr:	virtual address within pie chunk
 *
 * Returns physical address on success, -1 otherwise
 */
extern phys_addr_t pie_to_phys(struct pie_chunk *chunk, unsigned long addr);

extern void __iomem *__kern_to_pie(struct pie_chunk *chunk, void *ptr);

/**
 * pie_free - free the pool space used by an pie chunk
 * @chunk:	identifier returned by pie_load_sections
 */
extern void pie_free(struct pie_chunk *chunk);

#define __pie_load_sections(pool, name, phys) ({			\
	extern char __pie_##name##_start[];				\
	extern char __pie_##name##_end[];				\
	extern char __pie_rel_##name##_start[];				\
	extern char __pie_rel_##name##_end[];				\
									\
	__pie_load_data(pool, phys,					\
		__pie_##name##_start, __pie_##name##_end,		\
		__pie_rel_##name##_start, __pie_rel_##name##_end);	\
})

/*
 * Required for any symbol within an PIE section that is referenced by the
 * kernel
 */
#define EXPORT_PIE_SYMBOL(sym)		extern typeof(sym) sym __weak

/* For marking data and functions that should be part of a PIE */
#define __pie(name)	__attribute__ ((__section__(".pie." #name ".text")))
#define __pie_data(name) __attribute__ ((__section__(".pie." #name ".data")))

#else

static inline struct pie_chunk *__pie_load_data(struct gen_pool *pool,
					void *start, void *end, bool phys)
{
	return ERR_PTR(-EINVAL);
}

static inline phys_addr_t pie_to_phys(struct pie_chunk *chunk,
						unsigned long addr)
{
	return -1;
}

static inline void __iomem *__kern_to_pie(struct pie_chunk *chunk, void *ptr)
{
	return NULL;
}

static inline void pie_free(struct pie_chunk *chunk)
{
}

#define __pie_load_sections(pool, name, phys) ({ ERR_PTR(-EINVAL); })

#define EXPORT_PIE_SYMBOL(sym)

#define __pie(name)
#define __pie_data(name)

#endif

/**
 * pie_load_sections - load and fixup sections associated with the given name
 * @pool:	pool to allocate memory from and copy code into
 *		fixup to virtual address of destination
 * @name:	the name given to __pie() and __pie_data() when marking
 *		data and code
 *
 * Returns 0 on success, -EERROR otherwise
 */
#define pie_load_sections(pool, name) ({				\
	__pie_load_sections(pool, name, false);				\
})

/**
 * pie_load_sections_phys - load and fixup sections associated with the given
 * name for execution with the MMU off
 *
 * @pool:	pool to allocate memory from and copy code into
 *		fixup to virtual address of destination
 * @name:	the name given to __pie() and __pie_data() when marking
 *		data and code
 *
 * Returns 0 on success, -EERROR otherwise
 */
#define pie_load_sections_phys(pool, name) ({				\
	__pie_load_sections(pool, name, true);				\
})

/**
 * kern_to_pie - convert a kernel symbol to the virtual address of where
 * that symbol is loaded into the given PIE chunk.
 *
 * @chunk:	identifier returned by pie_load_sections
 * @p:		symbol to convert
 *
 * Return type is the same as type passed
 */
#define kern_to_pie(chunk, p) ({					\
	void *__ptr = (void *) (p);					\
	typeof(p) __result = (typeof(p)) __kern_to_pie(chunk, __ptr);	\
	__result;							\
})

/**
 * kern_to_fn - convert a kernel function symbol to the virtual address of where
 * that symbol is loaded into the given PIE chunk
 *
 * @chunk:	identifier returned by pie_load_sections
 * @p:		function to convert
 *
 * Return type is the same as type passed
 */
#define fn_to_pie(chunk, funcp) ({					\
	uintptr_t __kern_addr, __pie_addr;				\
									\
	__kern_addr = fnptr_to_addr(funcp);				\
	__pie_addr = kern_to_pie(chunk, __kern_addr);			\
									\
	fnptr_translate(funcp, __pie_addr);				\
})

#endif
