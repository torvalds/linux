/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _LINUX_ERR_H
#define _LINUX_ERR_H

#include <linux/compiler.h>
#include <linux/types.h>

#include <asm/errno.h>

/*
 * Kernel pointers have redundant information, so we can use a
 * scheme where we can return either an error code or a normal
 * pointer with the same return value.
 *
 * This should be a per-architecture thing, to allow different
 * error and pointer decisions.
 */
#define MAX_ERRNO	4095

#ifndef __ASSEMBLY__

/**
 * IS_ERR_VALUE - Detect an error pointer.
 * @x: The pointer to check.
 *
 * Like IS_ERR(), but does not generate a compiler warning if result is unused.
 */
#define IS_ERR_VALUE(x) unlikely((unsigned long)(void *)(x) >= (unsigned long)-MAX_ERRNO)

/**
 * ERR_PTR - Create an error pointer.
 * @error: A negative error code.
 *
 * Encodes @error into a pointer value. Users should consider the result
 * opaque and not assume anything about how the error is encoded.
 *
 * Return: A pointer with @error encoded within its value.
 */
static inline void * __must_check ERR_PTR(long error)
{
	return (void *) error;
}

/* Return the pointer in the percpu address space. */
#define ERR_PTR_PCPU(error) ((void __percpu *)(unsigned long)ERR_PTR(error))

/**
 * PTR_ERR - Extract the error code from an error pointer.
 * @ptr: An error pointer.
 * Return: The error code within @ptr.
 */
static inline long __must_check PTR_ERR(__force const void *ptr)
{
	return (long) ptr;
}

/* Read an error pointer from the percpu address space. */
#define PTR_ERR_PCPU(ptr) (PTR_ERR((const void *)(__force const unsigned long)(ptr)))

/**
 * IS_ERR - Detect an error pointer.
 * @ptr: The pointer to check.
 * Return: true if @ptr is an error pointer, false otherwise.
 */
static inline bool __must_check IS_ERR(__force const void *ptr)
{
	return IS_ERR_VALUE((unsigned long)ptr);
}

/* Read an error pointer from the percpu address space. */
#define IS_ERR_PCPU(ptr) (IS_ERR((const void *)(__force const unsigned long)(ptr)))

/**
 * IS_ERR_OR_NULL - Detect an error pointer or a null pointer.
 * @ptr: The pointer to check.
 *
 * Like IS_ERR(), but also returns true for a null pointer.
 */
static inline bool __must_check IS_ERR_OR_NULL(__force const void *ptr)
{
	return unlikely(!ptr) || IS_ERR_VALUE((unsigned long)ptr);
}

/**
 * ERR_CAST - Explicitly cast an error-valued pointer to another pointer type
 * @ptr: The pointer to cast.
 *
 * Explicitly cast an error-valued pointer to another pointer type in such a
 * way as to make it clear that's what's going on.
 */
static inline void * __must_check ERR_CAST(__force const void *ptr)
{
	/* cast away the const */
	return (void *) ptr;
}

/**
 * PTR_ERR_OR_ZERO - Extract the error code from a pointer if it has one.
 * @ptr: A potential error pointer.
 *
 * Convenience function that can be used inside a function that returns
 * an error code to propagate errors received as error pointers.
 * For example, ``return PTR_ERR_OR_ZERO(ptr);`` replaces:
 *
 * .. code-block:: c
 *
 *	if (IS_ERR(ptr))
 *		return PTR_ERR(ptr);
 *	else
 *		return 0;
 *
 * Return: The error code within @ptr if it is an error pointer; 0 otherwise.
 */
static inline int __must_check PTR_ERR_OR_ZERO(__force const void *ptr)
{
	if (IS_ERR(ptr))
		return PTR_ERR(ptr);
	else
		return 0;
}

#endif

#endif /* _LINUX_ERR_H */
