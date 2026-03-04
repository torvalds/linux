// SPDX-License-Identifier: GPL-2.0-only

#include <linux/gfp.h>
#include <linux/init.h>
#include <linux/mm.h>
#include <linux/time_namespace.h>
#include <linux/types.h>
#include <linux/vdso_datastore.h>
#include <vdso/datapage.h>

static u8 vdso_initdata[VDSO_NR_PAGES * PAGE_SIZE] __aligned(PAGE_SIZE) __initdata = {};

#ifdef CONFIG_GENERIC_GETTIMEOFDAY
struct vdso_time_data *vdso_k_time_data __refdata =
	(void *)&vdso_initdata[VDSO_TIME_PAGE_OFFSET * PAGE_SIZE];

static_assert(sizeof(struct vdso_time_data) <= PAGE_SIZE);
#endif /* CONFIG_GENERIC_GETTIMEOFDAY */

#ifdef CONFIG_VDSO_GETRANDOM
struct vdso_rng_data *vdso_k_rng_data __refdata =
	(void *)&vdso_initdata[VDSO_RNG_PAGE_OFFSET * PAGE_SIZE];

static_assert(sizeof(struct vdso_rng_data) <= PAGE_SIZE);
#endif /* CONFIG_VDSO_GETRANDOM */

#ifdef CONFIG_ARCH_HAS_VDSO_ARCH_DATA
struct vdso_arch_data *vdso_k_arch_data __refdata =
	(void *)&vdso_initdata[VDSO_ARCH_PAGES_START * PAGE_SIZE];
#endif /* CONFIG_ARCH_HAS_VDSO_ARCH_DATA */

void __init vdso_setup_data_pages(void)
{
	unsigned int order = get_order(VDSO_NR_PAGES * PAGE_SIZE);
	struct page *pages;

	/*
	 * Allocate the data pages dynamically. SPARC does not support mapping
	 * static pages to be mapped into userspace.
	 * It is also a requirement for mlockall() support.
	 *
	 * Do not use folios. In time namespaces the pages are mapped in a different order
	 * to userspace, which is not handled by the folio optimizations in finish_fault().
	 */
	pages = alloc_pages(GFP_KERNEL, order);
	if (!pages)
		panic("Unable to allocate VDSO storage pages");

	/* The pages are mapped one-by-one into userspace and each one needs to be refcounted. */
	split_page(pages, order);

	/* Move the data already written by other subsystems to the new pages */
	memcpy(page_address(pages), vdso_initdata, VDSO_NR_PAGES * PAGE_SIZE);

	if (IS_ENABLED(CONFIG_GENERIC_GETTIMEOFDAY))
		vdso_k_time_data = page_address(pages + VDSO_TIME_PAGE_OFFSET);

	if (IS_ENABLED(CONFIG_VDSO_GETRANDOM))
		vdso_k_rng_data = page_address(pages + VDSO_RNG_PAGE_OFFSET);

	if (IS_ENABLED(CONFIG_ARCH_HAS_VDSO_ARCH_DATA))
		vdso_k_arch_data = page_address(pages + VDSO_ARCH_PAGES_START);
}

static vm_fault_t vvar_fault(const struct vm_special_mapping *sm,
			     struct vm_area_struct *vma, struct vm_fault *vmf)
{
	struct page *page, *timens_page;

	timens_page = find_timens_vvar_page(vma);

	switch (vmf->pgoff) {
	case VDSO_TIME_PAGE_OFFSET:
		if (!IS_ENABLED(CONFIG_GENERIC_GETTIMEOFDAY))
			return VM_FAULT_SIGBUS;
		page = virt_to_page(vdso_k_time_data);
		if (timens_page) {
			/*
			 * Fault in VVAR page too, since it will be accessed
			 * to get clock data anyway.
			 */
			unsigned long addr;
			vm_fault_t err;

			addr = vmf->address + VDSO_TIMENS_PAGE_OFFSET * PAGE_SIZE;
			err = vmf_insert_page(vma, addr, page);
			if (unlikely(err & VM_FAULT_ERROR))
				return err;
			page = timens_page;
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
		page = virt_to_page(vdso_k_time_data);
		break;
	case VDSO_RNG_PAGE_OFFSET:
		if (!IS_ENABLED(CONFIG_VDSO_GETRANDOM))
			return VM_FAULT_SIGBUS;
		page = virt_to_page(vdso_k_rng_data);
		break;
	case VDSO_ARCH_PAGES_START ... VDSO_ARCH_PAGES_END:
		if (!IS_ENABLED(CONFIG_ARCH_HAS_VDSO_ARCH_DATA))
			return VM_FAULT_SIGBUS;
		page = virt_to_page(vdso_k_arch_data) + vmf->pgoff - VDSO_ARCH_PAGES_START;
		break;
	default:
		return VM_FAULT_SIGBUS;
	}

	get_page(page);
	vmf->page = page;
	return 0;
}

const struct vm_special_mapping vdso_vvar_mapping = {
	.name	= "[vvar]",
	.fault	= vvar_fault,
};

struct vm_area_struct *vdso_install_vvar_mapping(struct mm_struct *mm, unsigned long addr)
{
	return _install_special_mapping(mm, addr, VDSO_NR_PAGES * PAGE_SIZE,
					VM_READ | VM_MAYREAD | VM_IO | VM_DONTDUMP |
					VM_MIXEDMAP | VM_SEALED_SYSMAP,
					&vdso_vvar_mapping);
}

#ifdef CONFIG_TIME_NS
/*
 * The vvar page layout depends on whether a task belongs to the root or
 * non-root time namespace. Whenever a task changes its namespace, the VVAR
 * page tables are cleared and then they will be re-faulted with a
 * corresponding layout.
 * See also the comment near timens_setup_vdso_clock_data() for details.
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
#endif
