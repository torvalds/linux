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

#endif /* _LINUX_EXECMEM_ALLOC_H */
