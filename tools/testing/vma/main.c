// SPDX-License-Identifier: GPL-2.0-or-later

#include "shared.h"
/*
 * Directly import the VMA implementation here. Our vma_internal.h wrapper
 * provides userland-equivalent functionality for everything vma.c uses.
 */
#include "../../../mm/vma_init.c"
#include "../../../mm/vma_exec.c"
#include "../../../mm/vma.c"

/* Tests are included directly so they can test static functions in mm/vma.c. */
#include "tests/merge.c"
#include "tests/mmap.c"
#include "tests/vma.c"

/* Helper functions which utilise static kernel functions. */

struct vm_area_struct *merge_existing(struct vma_merge_struct *vmg)
{
	struct vm_area_struct *vma;

	vma = vma_merge_existing_range(vmg);
	if (vma)
		vma_assert_attached(vma);
	return vma;
}

int attach_vma(struct mm_struct *mm, struct vm_area_struct *vma)
{
	int res;

	res = vma_link(mm, vma);
	if (!res)
		vma_assert_attached(vma);
	return res;
}

/* Main test running which invokes tests/ *.c runners. */
int main(void)
{
	int num_tests = 0, num_fail = 0;

	maple_tree_init();
	vma_state_init();

	run_merge_tests(&num_tests, &num_fail);
	run_mmap_tests(&num_tests, &num_fail);
	run_vma_tests(&num_tests, &num_fail);

	printf("%d tests run, %d passed, %d failed.\n",
	       num_tests, num_tests - num_fail, num_fail);

	return num_fail == 0 ? EXIT_SUCCESS : EXIT_FAILURE;
}
