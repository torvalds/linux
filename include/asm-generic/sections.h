/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _ASM_GENERIC_SECTIONS_H_
#define _ASM_GENERIC_SECTIONS_H_

/* References to section boundaries */

#include <linux/compiler.h>
#include <linux/types.h>

/*
 * Usage guidelines:
 * _text, _data: architecture specific, don't use them in arch-independent code
 * [_stext, _etext]: contains .text.* sections, may also contain .rodata.*
 *                   and/or .init.* sections
 * [_sdata, _edata]: contains .data.* sections, may also contain .rodata.*
 *                   and/or .init.* sections.
 * [__start_rodata, __end_rodata]: contains .rodata.* sections
 * [__start_ro_after_init, __end_ro_after_init]:
 *		     contains .data..ro_after_init section
 * [__init_begin, __init_end]: contains .init.* sections, but .init.text.*
 *                   may be out of this range on some architectures.
 * [_sinittext, _einittext]: contains .init.text.* sections
 * [__bss_start, __bss_stop]: contains BSS sections
 *
 * Following global variables are optional and may be unavailable on some
 * architectures and/or kernel configurations.
 *	_text, _data
 *	__kprobes_text_start, __kprobes_text_end
 *	__entry_text_start, __entry_text_end
 *	__ctors_start, __ctors_end
 *	__irqentry_text_start, __irqentry_text_end
 *	__softirqentry_text_start, __softirqentry_text_end
 *	__start_opd, __end_opd
 */
extern char _text[], _stext[], _etext[];
extern char _data[], _sdata[], _edata[];
extern char __bss_start[], __bss_stop[];
extern char __init_begin[], __init_end[];
extern char _sinittext[], _einittext[];
extern char __start_ro_after_init[], __end_ro_after_init[];
extern char _end[];
extern char __per_cpu_load[], __per_cpu_start[], __per_cpu_end[];
extern char __kprobes_text_start[], __kprobes_text_end[];
extern char __entry_text_start[], __entry_text_end[];
extern char __start_rodata[], __end_rodata[];
extern char __irqentry_text_start[], __irqentry_text_end[];
extern char __softirqentry_text_start[], __softirqentry_text_end[];
extern char __start_once[], __end_once[];

/* Start and end of .ctors section - used for constructor calls. */
extern char __ctors_start[], __ctors_end[];

/* Start and end of .opd section - used for function descriptors. */
extern char __start_opd[], __end_opd[];

/* Start and end of instrumentation protected text section */
extern char __noinstr_text_start[], __noinstr_text_end[];

extern __visible const void __nosave_begin, __nosave_end;

/* Function descriptor handling (if any).  Override in asm/sections.h */
#ifdef CONFIG_HAVE_FUNCTION_DESCRIPTORS
void *dereference_function_descriptor(void *ptr);
void *dereference_kernel_function_descriptor(void *ptr);
#else
#define dereference_function_descriptor(p) ((void *)(p))
#define dereference_kernel_function_descriptor(p) ((void *)(p))

/* An address is simply the address of the function. */
typedef struct {
	unsigned long addr;
} func_desc_t;
#endif

static inline bool have_function_descriptors(void)
{
	return IS_ENABLED(CONFIG_HAVE_FUNCTION_DESCRIPTORS);
}

/**
 * memory_contains - checks if an object is contained within a memory region
 * @begin: virtual address of the beginning of the memory region
 * @end: virtual address of the end of the memory region
 * @virt: virtual address of the memory object
 * @size: size of the memory object
 *
 * Returns: true if the object specified by @virt and @size is entirely
 * contained within the memory region defined by @begin and @end, false
 * otherwise.
 */
static inline bool memory_contains(void *begin, void *end, void *virt,
				   size_t size)
{
	return virt >= begin && virt + size <= end;
}

/**
 * memory_intersects - checks if the region occupied by an object intersects
 *                     with another memory region
 * @begin: virtual address of the beginning of the memory region
 * @end: virtual address of the end of the memory region
 * @virt: virtual address of the memory object
 * @size: size of the memory object
 *
 * Returns: true if an object's memory region, specified by @virt and @size,
 * intersects with the region specified by @begin and @end, false otherwise.
 */
static inline bool memory_intersects(void *begin, void *end, void *virt,
				     size_t size)
{
	void *vend = virt + size;

	if (virt < end && vend > begin)
		return true;

	return false;
}

/**
 * init_section_contains - checks if an object is contained within the init
 *                         section
 * @virt: virtual address of the memory object
 * @size: size of the memory object
 *
 * Returns: true if the object specified by @virt and @size is entirely
 * contained within the init section, false otherwise.
 */
static inline bool init_section_contains(void *virt, size_t size)
{
	return memory_contains(__init_begin, __init_end, virt, size);
}

/**
 * init_section_intersects - checks if the region occupied by an object
 *                           intersects with the init section
 * @virt: virtual address of the memory object
 * @size: size of the memory object
 *
 * Returns: true if an object's memory region, specified by @virt and @size,
 * intersects with the init section, false otherwise.
 */
static inline bool init_section_intersects(void *virt, size_t size)
{
	return memory_intersects(__init_begin, __init_end, virt, size);
}

/**
 * is_kernel_core_data - checks if the pointer address is located in the
 *			 .data or .bss section
 *
 * @addr: address to check
 *
 * Returns: true if the address is located in .data or .bss, false otherwise.
 * Note: On some archs it may return true for core RODATA, and false
 *       for others. But will always be true for core RW data.
 */
static inline bool is_kernel_core_data(unsigned long addr)
{
	if (addr >= (unsigned long)_sdata && addr < (unsigned long)_edata)
		return true;

	if (addr >= (unsigned long)__bss_start &&
	    addr < (unsigned long)__bss_stop)
		return true;

	return false;
}

/**
 * is_kernel_rodata - checks if the pointer address is located in the
 *                    .rodata section
 *
 * @addr: address to check
 *
 * Returns: true if the address is located in .rodata, false otherwise.
 */
static inline bool is_kernel_rodata(unsigned long addr)
{
	return addr >= (unsigned long)__start_rodata &&
	       addr < (unsigned long)__end_rodata;
}

/**
 * is_kernel_inittext - checks if the pointer address is located in the
 *                      .init.text section
 *
 * @addr: address to check
 *
 * Returns: true if the address is located in .init.text, false otherwise.
 */
static inline bool is_kernel_inittext(unsigned long addr)
{
	return addr >= (unsigned long)_sinittext &&
	       addr < (unsigned long)_einittext;
}

/**
 * __is_kernel_text - checks if the pointer address is located in the
 *                    .text section
 *
 * @addr: address to check
 *
 * Returns: true if the address is located in .text, false otherwise.
 * Note: an internal helper, only check the range of _stext to _etext.
 */
static inline bool __is_kernel_text(unsigned long addr)
{
	return addr >= (unsigned long)_stext &&
	       addr < (unsigned long)_etext;
}

/**
 * __is_kernel - checks if the pointer address is located in the kernel range
 *
 * @addr: address to check
 *
 * Returns: true if the address is located in the kernel range, false otherwise.
 * Note: an internal helper, check the range of _stext to _end,
 *       and range from __init_begin to __init_end, which can be outside
 *       of the _stext to _end range.
 */
static inline bool __is_kernel(unsigned long addr)
{
	return ((addr >= (unsigned long)_stext &&
	         addr < (unsigned long)_end) ||
		(addr >= (unsigned long)__init_begin &&
		 addr < (unsigned long)__init_end));
}

#endif /* _ASM_GENERIC_SECTIONS_H_ */
