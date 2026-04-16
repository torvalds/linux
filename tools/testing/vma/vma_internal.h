/* SPDX-License-Identifier: GPL-2.0+ */
/*
 * vma_internal.h
 *
 * Header providing userland wrappers and shims for the functionality provided
 * by mm/vma_internal.h.
 *
 * We make the header guard the same as mm/vma_internal.h, so if this shim
 * header is included, it precludes the inclusion of the kernel one.
 */

#ifndef __MM_VMA_INTERNAL_H
#define __MM_VMA_INTERNAL_H

#include <stdlib.h>

#define CONFIG_MMU
#define CONFIG_PER_VMA_LOCK

#ifdef __CONCAT
#undef __CONCAT
#endif

#include <linux/args.h>
#include <linux/atomic.h>
#include <linux/bitmap.h>
#include <linux/list.h>
#include <linux/maple_tree.h>
#include <linux/mm.h>
#include <linux/rbtree.h>
#include <linux/refcount.h>
#include <linux/slab.h>

/*
 * DUPLICATE typedef definitions from kernel source that have to be declared
 * ahead of all other headers.
 */
#define __private
/* NUM_MM_FLAG_BITS defined by test code. */
typedef struct {
	__private DECLARE_BITMAP(__mm_flags, NUM_MM_FLAG_BITS);
} mm_flags_t;
/* NUM_VMA_FLAG_BITS defined by test code. */
typedef struct {
	DECLARE_BITMAP(__vma_flags, NUM_VMA_FLAG_BITS);
} __private vma_flags_t;

typedef unsigned long vm_flags_t;
#define pgoff_t unsigned long
typedef unsigned long	pgprotval_t;
typedef struct pgprot { pgprotval_t pgprot; } pgprot_t;
typedef __bitwise unsigned int vm_fault_t;

#include "include/stubs.h"
#include "include/dup.h"
#include "include/custom.h"

#endif	/* __MM_VMA_INTERNAL_H */
