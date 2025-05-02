/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _LINUX_EXECMEM_ALLOC_H
#define _LINUX_EXECMEM_ALLOC_H

#include <linux/types.h>
#include <linux/moduleloader.h>

#if (defined(CONFIG_KASAN_GENERIC) || defined(CONFIG_KASAN_SW_TAGS)) && \
		!defined(CONFIG_KASAN_VMALLOC)
#include <linux/kasan.h>
#define MODULE_ALIGN (PAGE_SIZE << KASAN_SHADOW_SCALE_SHIFT)
#else
#define MODULE_ALIGN PAGE_SIZE
#endif

/**
 * enum execmem_type - types of executable memory ranges
 *
 * There are several subsystems that allocate executable memory.
 * Architectures define different restrictions on placement,
 * permissions, alignment and other parameters for memory that can be used
 * by these subsystems.
 * Types in this enum identify subsystems that allocate executable memory
 * and let architectures define parameters for ranges suitable for
 * allocations by each subsystem.
 *
 * @EXECMEM_DEFAULT: default parameters that would be used for types that
 * are not explicitly defined.
 * @EXECMEM_MODULE_TEXT: parameters for module text sections
 * @EXECMEM_KPROBES: parameters for kprobes
 * @EXECMEM_FTRACE: parameters for ftrace
 * @EXECMEM_BPF: parameters for BPF
 * @EXECMEM_MODULE_DATA: parameters for module data sections
 * @EXECMEM_TYPE_MAX:
 */
enum execmem_type {
	EXECMEM_DEFAULT,
	EXECMEM_MODULE_TEXT = EXECMEM_DEFAULT,
	EXECMEM_KPROBES,
	EXECMEM_FTRACE,
	EXECMEM_BPF,
	EXECMEM_MODULE_DATA,
	EXECMEM_TYPE_MAX,
};

/**
 * enum execmem_range_flags - options for executable memory allocations
 * @EXECMEM_KASAN_SHADOW:	allocate kasan shadow
 * @EXECMEM_ROX_CACHE:		allocations should use ROX cache of huge pages
 */
enum execmem_range_flags {
	EXECMEM_KASAN_SHADOW	= (1 << 0),
	EXECMEM_ROX_CACHE	= (1 << 1),
};

#ifdef CONFIG_ARCH_HAS_EXECMEM_ROX
/**
 * execmem_fill_trapping_insns - set memory to contain instructions that
 *				 will trap
 * @ptr:	pointer to memory to fill
 * @size:	size of the range to fill
 * @writable:	is the memory poited by @ptr is writable or ROX
 *
 * A hook for architecures to fill execmem ranges with invalid instructions.
 * Architectures that use EXECMEM_ROX_CACHE must implement this.
 */
void execmem_fill_trapping_insns(void *ptr, size_t size, bool writable);

/**
 * execmem_make_temp_rw - temporarily remap region with read-write
 *			  permissions
 * @ptr:	address of the region to remap
 * @size:	size of the region to remap
 *
 * Remaps a part of the cached large page in the ROX cache in the range
 * [@ptr, @ptr + @size) as writable and not executable. The caller must
 * have exclusive ownership of this range and ensure nothing will try to
 * execute code in this range.
 *
 * Return: 0 on success or negative error code on failure.
 */
int execmem_make_temp_rw(void *ptr, size_t size);

/**
 * execmem_restore_rox - restore read-only-execute permissions
 * @ptr:	address of the region to remap
 * @size:	size of the region to remap
 *
 * Restores read-only-execute permissions on a range [@ptr, @ptr + @size)
 * after it was temporarily remapped as writable. Relies on architecture
 * implementation of set_memory_rox() to restore mapping using large pages.
 *
 * Return: 0 on success or negative error code on failure.
 */
int execmem_restore_rox(void *ptr, size_t size);
#else
static inline int execmem_make_temp_rw(void *ptr, size_t size) { return 0; }
static inline int execmem_restore_rox(void *ptr, size_t size) { return 0; }
#endif

/**
 * struct execmem_range - definition of an address space suitable for code and
 *			  related data allocations
 * @start:	address space start
 * @end:	address space end (inclusive)
 * @fallback_start: start of the secondary address space range for fallback
 *                  allocations on architectures that require it
 * @fallback_end:   start of the secondary address space (inclusive)
 * @pgprot:	permissions for memory in this address space
 * @alignment:	alignment required for text allocations
 * @flags:	options for memory allocations for this range
 */
struct execmem_range {
	unsigned long   start;
	unsigned long   end;
	unsigned long   fallback_start;
	unsigned long   fallback_end;
	pgprot_t        pgprot;
	unsigned int	alignment;
	enum execmem_range_flags flags;
};

/**
 * struct execmem_info - architecture parameters for code allocations
 * @ranges: array of parameter sets defining architecture specific
 * parameters for executable memory allocations. The ranges that are not
 * explicitly initialized by an architecture use parameters defined for
 * @EXECMEM_DEFAULT.
 */
struct execmem_info {
	struct execmem_range	ranges[EXECMEM_TYPE_MAX];
};

/**
 * execmem_arch_setup - define parameters for allocations of executable memory
 *
 * A hook for architectures to define parameters for allocations of
 * executable memory. These parameters should be filled into the
 * @execmem_info structure.
 *
 * For architectures that do not implement this method a default set of
 * parameters will be used
 *
 * Return: a structure defining architecture parameters and restrictions
 * for allocations of executable memory
 */
struct execmem_info *execmem_arch_setup(void);

/**
 * execmem_alloc - allocate executable memory
 * @type: type of the allocation
 * @size: how many bytes of memory are required
 *
 * Allocates memory that will contain executable code, either generated or
 * loaded from kernel modules.
 *
 * Allocates memory that will contain data coupled with executable code,
 * like data sections in kernel modules.
 *
 * The memory will have protections defined by architecture for executable
 * region of the @type.
 *
 * Return: a pointer to the allocated memory or %NULL
 */
void *execmem_alloc(enum execmem_type type, size_t size);

/**
 * execmem_free - free executable memory
 * @ptr: pointer to the memory that should be freed
 */
void execmem_free(void *ptr);

#ifdef CONFIG_MMU
/**
 * execmem_vmap - create virtual mapping for EXECMEM_MODULE_DATA memory
 * @size: size of the virtual mapping in bytes
 *
 * Maps virtually contiguous area in the range suitable for EXECMEM_MODULE_DATA.
 *
 * Return: the area descriptor on success or %NULL on failure.
 */
struct vm_struct *execmem_vmap(size_t size);
#endif

/**
 * execmem_update_copy - copy an update to executable memory
 * @dst:  destination address to update
 * @src:  source address containing the data
 * @size: how many bytes of memory shold be copied
 *
 * Copy @size bytes from @src to @dst using text poking if the memory at
 * @dst is read-only.
 *
 * Return: a pointer to @dst or NULL on error
 */
void *execmem_update_copy(void *dst, const void *src, size_t size);

/**
 * execmem_is_rox - check if execmem is read-only
 * @type - the execmem type to check
 *
 * Return: %true if the @type is read-only, %false if it's writable
 */
bool execmem_is_rox(enum execmem_type type);

#if defined(CONFIG_EXECMEM) && !defined(CONFIG_ARCH_WANTS_EXECMEM_LATE)
void execmem_init(void);
#else
static inline void execmem_init(void) {}
#endif

#endif /* _LINUX_EXECMEM_ALLOC_H */
