/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _LINUX_EXECMEM_ALLOC_H
#define _LINUX_EXECMEM_ALLOC_H

#include <linux/types.h>
#include <linux/moduleloader.h>

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
 * @EXECMEM_TYPE_MAX:
 */
enum execmem_type {
	EXECMEM_DEFAULT,
	EXECMEM_MODULE_TEXT = EXECMEM_DEFAULT,
	EXECMEM_KPROBES,
	EXECMEM_FTRACE,
	EXECMEM_BPF,
	EXECMEM_TYPE_MAX,
};

/**
 * struct execmem_range - definition of an address space suitable for code and
 *			  related data allocations
 * @start:	address space start
 * @end:	address space end (inclusive)
 * @pgprot:	permissions for memory in this address space
 * @alignment:	alignment required for text allocations
 */
struct execmem_range {
	unsigned long   start;
	unsigned long   end;
	pgprot_t        pgprot;
	unsigned int	alignment;
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

#ifdef CONFIG_EXECMEM
void execmem_init(void);
#else
static inline void execmem_init(void) {}
#endif

#endif /* _LINUX_EXECMEM_ALLOC_H */
