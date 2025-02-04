// SPDX-License-Identifier: GPL-2.0-only

#include <linux/linkage.h>
#include <linux/mmap_lock.h>
#include <linux/mm.h>
#include <linux/time_namespace.h>
#include <linux/types.h>
#include <linux/vdso_datastore.h>
#include <vdso/datapage.h>

/*
 * The vDSO data page.
 */
#ifdef CONFIG_HAVE_GENERIC_VDSO
static union vdso_data_store vdso_time_data_store __page_aligned_data;
struct vdso_time_data *vdso_k_time_data = vdso_time_data_store.data;
static_assert(sizeof(vdso_time_data_store) == PAGE_SIZE);
#endif /* CONFIG_HAVE_GENERIC_VDSO */

#ifdef CONFIG_VDSO_GETRANDOM
static union {
	struct vdso_rng_data	data;
	u8			page[PAGE_SIZE];
} vdso_rng_data_store __page_aligned_data;
struct vdso_rng_data *vdso_k_rng_data = &vdso_rng_data_store.data;
static_assert(sizeof(vdso_rng_data_store) == PAGE_SIZE);
#endif /* CONFIG_VDSO_GETRANDOM */

#ifdef CONFIG_ARCH_HAS_VDSO_ARCH_DATA
static union {
	struct vdso_arch_data	data;
	u8			page[VDSO_ARCH_DATA_SIZE];
} vdso_arch_data_store __page_aligned_data;
struct vdso_arch_data *vdso_k_arch_data = &vdso_arch_data_store.data;
#endif /* CONFIG_ARCH_HAS_VDSO_ARCH_DATA */

static vm_fault_t vvar_fault(const struct vm_special_mapping *sm,
			     struct vm_area_struct *vma, struct vm_fault *vmf)
{
	struct page *timens_page = find_timens_vvar_page(vma);
	unsigned long addr, pfn;
	vm_fault_t err;

	switch (vmf->pgoff) {
	case VDSO_TIME_PAGE_OFFSET:
		if (!IS_ENABLED(CONFIG_HAVE_GENERIC_VDSO))
			return VM_FAULT_SIGBUS;
		pfn = __phys_to_pfn(__pa_symbol(vdso_k_time_data));
		if (timens_page) {
			/*
			 * Fault in VVAR page too, since it will be accessed
			 * to get clock data anyway.
			 */
			addr = vmf->address + VDSO_TIMENS_PAGE_OFFSET * PAGE_SIZE;
			err = vmf_insert_pfn(vma, addr, pfn);
			if (unlikely(err & VM_FAULT_ERROR))
				return err;
			pfn = page_to_pfn(timens_page);
		}
		break;
	case VDSO_TIMENS_PAGE_OFFSET:
		/*
		 * If a task belongs to a time namespace then a namespace
		 * specific VVAR is mapped with the VVAR_DATA_PAGE_OFFSET and
		 * the real VVAR page is mapped with the VVAR_TIMENS_PAGE_OFFSET
		 * offset.
		 * See also the comment near timens_setup_vdso_data().
		 */
		if (!IS_ENABLED(CONFIG_TIME_NS) || !timens_page)
			return VM_FAULT_SIGBUS;
		pfn = __phys_to_pfn(__pa_symbol(vdso_k_time_data));
		break;
	case VDSO_RNG_PAGE_OFFSET:
		if (!IS_ENABLED(CONFIG_VDSO_GETRANDOM))
			return VM_FAULT_SIGBUS;
		pfn = __phys_to_pfn(__pa_symbol(vdso_k_rng_data));
		break;
	case VDSO_ARCH_PAGES_START ... VDSO_ARCH_PAGES_END:
		if (!IS_ENABLED(CONFIG_ARCH_HAS_VDSO_ARCH_DATA))
			return VM_FAULT_SIGBUS;
		pfn = __phys_to_pfn(__pa_symbol(vdso_k_arch_data)) +
			vmf->pgoff - VDSO_ARCH_PAGES_START;
		break;
	default:
		return VM_FAULT_SIGBUS;
	}

	return vmf_insert_pfn(vma, vmf->address, pfn);
}

const struct vm_special_mapping vdso_vvar_mapping = {
	.name	= "[vvar]",
	.fault	= vvar_fault,
};

struct vm_area_struct *vdso_install_vvar_mapping(struct mm_struct *mm, unsigned long addr)
{
	return _install_special_mapping(mm, addr, VDSO_NR_PAGES * PAGE_SIZE,
					VM_READ | VM_MAYREAD | VM_IO | VM_DONTDUMP | VM_PFNMAP,
					&vdso_vvar_mapping);
}

#ifdef CONFIG_TIME_NS
/*
 * The vvar page layout depends on whether a task belongs to the root or
 * non-root time namespace. Whenever a task changes its namespace, the VVAR
 * page tables are cleared and then they will be re-faulted with a
 * corresponding layout.
 * See also the comment near timens_setup_vdso_data() for details.
 */
int vdso_join_timens(struct task_struct *task, struct time_namespace *ns)
{
	struct mm_struct *mm = task->mm;
	struct vm_area_struct *vma;
	VMA_ITERATOR(vmi, mm, 0);

	mmap_read_lock(mm);
	for_each_vma(vmi, vma) {
		if (vma_is_special_mapping(vma, &vdso_vvar_mapping))
			zap_vma_pages(vma);
	}
	mmap_read_unlock(mm);

	return 0;
}

struct vdso_time_data *arch_get_vdso_data(void *vvar_page)
{
	return (struct vdso_time_data *)vvar_page;
}
#endif
