// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>

#include "generated/bit-length.h"
#include "maple-shared.h"
#include "vma_internal.h"
#include "../../../mm/vma.h"

/* Simple test runner. Assumes local num_[fail, tests] counters. */
#define TEST(name)							\
	do {								\
		(*num_tests)++;						\
		if (!test_##name()) {					\
			(*num_fail)++;					\
			fprintf(stderr, "Test " #name " FAILED\n");	\
		}							\
	} while (0)

#define ASSERT_TRUE(_expr)						\
	do {								\
		if (!(_expr)) {						\
			fprintf(stderr,					\
				"Assert FAILED at %s:%d:%s(): %s is FALSE.\n", \
				__FILE__, __LINE__, __FUNCTION__, #_expr); \
			return false;					\
		}							\
	} while (0)

#define ASSERT_FALSE(_expr) ASSERT_TRUE(!(_expr))
#define ASSERT_EQ(_val1, _val2) ASSERT_TRUE((_val1) == (_val2))
#define ASSERT_NE(_val1, _val2) ASSERT_TRUE((_val1) != (_val2))

#define IS_SET(_val, _flags) ((_val & _flags) == _flags)

extern bool fail_prealloc;

/* Override vma_iter_prealloc() so we can choose to fail it. */
#define vma_iter_prealloc(vmi, vma)					\
	(fail_prealloc ? -ENOMEM : mas_preallocate(&(vmi)->mas, (vma), GFP_KERNEL))

#define CONFIG_DEFAULT_MMAP_MIN_ADDR 65536

extern unsigned long mmap_min_addr;
extern unsigned long dac_mmap_min_addr;
extern unsigned long stack_guard_gap;

extern const struct vm_operations_struct vma_dummy_vm_ops;
extern struct anon_vma dummy_anon_vma;
extern struct task_struct __current;

/*
 * Helper function which provides a wrapper around a merge existing VMA
 * operation.
 *
 * Declared in main.c as uses static VMA function.
 */
struct vm_area_struct *merge_existing(struct vma_merge_struct *vmg);

/*
 * Helper function to allocate a VMA and link it to the tree.
 *
 * Declared in main.c as uses static VMA function.
 */
int attach_vma(struct mm_struct *mm, struct vm_area_struct *vma);

/* Helper function providing a dummy vm_ops->close() method.*/
static inline void dummy_close(struct vm_area_struct *)
{
}

/* Helper function to simply allocate a VMA. */
struct vm_area_struct *alloc_vma(struct mm_struct *mm,
		unsigned long start, unsigned long end,
		pgoff_t pgoff, vm_flags_t vm_flags);

/* Helper function to detach and free a VMA. */
void detach_free_vma(struct vm_area_struct *vma);

/* Helper function to allocate a VMA and link it to the tree. */
struct vm_area_struct *alloc_and_link_vma(struct mm_struct *mm,
		unsigned long start, unsigned long end,
		pgoff_t pgoff, vm_flags_t vm_flags);

/*
 * Helper function to reset the dummy anon_vma to indicate it has not been
 * duplicated.
 */
void reset_dummy_anon_vma(void);

/*
 * Helper function to remove all VMAs and destroy the maple tree associated with
 * a virtual address space. Returns a count of VMAs in the tree.
 */
int cleanup_mm(struct mm_struct *mm, struct vma_iterator *vmi);

/* Helper function to determine if VMA has had vma_start_write() performed. */
bool vma_write_started(struct vm_area_struct *vma);

void __vma_set_dummy_anon_vma(struct vm_area_struct *vma,
		struct anon_vma_chain *avc, struct anon_vma *anon_vma);

/* Provide a simple dummy VMA/anon_vma dummy setup for testing. */
void vma_set_dummy_anon_vma(struct vm_area_struct *vma,
			    struct anon_vma_chain *avc);

/* Helper function to specify a VMA's range. */
void vma_set_range(struct vm_area_struct *vma,
		   unsigned long start, unsigned long end,
		   pgoff_t pgoff);
