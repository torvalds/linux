/* SPDX-License-Identifier: GPL-2.0 */
#ifndef INCLUDE_XEN_OPS_H
#define INCLUDE_XEN_OPS_H

#include <linux/percpu.h>
#include <linux/notifier.h>
#include <linux/efi.h>
#include <xen/features.h>
#include <asm/xen/interface.h>
#include <xen/interface/vcpu.h>

DECLARE_PER_CPU(struct vcpu_info *, xen_vcpu);

DECLARE_PER_CPU(uint32_t, xen_vcpu_id);
static inline uint32_t xen_vcpu_nr(int cpu)
{
	return per_cpu(xen_vcpu_id, cpu);
}

#define XEN_VCPU_ID_INVALID U32_MAX

void xen_arch_pre_suspend(void);
void xen_arch_post_suspend(int suspend_cancelled);

void xen_timer_resume(void);
void xen_arch_resume(void);
void xen_arch_suspend(void);

void xen_reboot(int reason);

void xen_resume_notifier_register(struct notifier_block *nb);
void xen_resume_notifier_unregister(struct notifier_block *nb);

bool xen_vcpu_stolen(int vcpu);
void xen_setup_runstate_info(int cpu);
void xen_time_setup_guest(void);
void xen_manage_runstate_time(int action);
void xen_get_runstate_snapshot(struct vcpu_runstate_info *res);
u64 xen_steal_clock(int cpu);

int xen_setup_shutdown_event(void);

extern unsigned long *xen_contiguous_bitmap;

#if defined(CONFIG_XEN_PV) || defined(CONFIG_ARM) || defined(CONFIG_ARM64)
int xen_create_contiguous_region(phys_addr_t pstart, unsigned int order,
				unsigned int address_bits,
				dma_addr_t *dma_handle);

void xen_destroy_contiguous_region(phys_addr_t pstart, unsigned int order);
#else
static inline int xen_create_contiguous_region(phys_addr_t pstart,
					       unsigned int order,
					       unsigned int address_bits,
					       dma_addr_t *dma_handle)
{
	return 0;
}

static inline void xen_destroy_contiguous_region(phys_addr_t pstart,
						 unsigned int order) { }
#endif

#if defined(CONFIG_XEN_PV)
int xen_remap_pfn(struct vm_area_struct *vma, unsigned long addr,
		  xen_pfn_t *pfn, int nr, int *err_ptr, pgprot_t prot,
		  unsigned int domid, bool no_translate, struct page **pages);
#else
static inline int xen_remap_pfn(struct vm_area_struct *vma, unsigned long addr,
				xen_pfn_t *pfn, int nr, int *err_ptr,
				pgprot_t prot,  unsigned int domid,
				bool no_translate, struct page **pages)
{
	BUG();
	return 0;
}
#endif

struct vm_area_struct;

#ifdef CONFIG_XEN_AUTO_XLATE
int xen_xlate_remap_gfn_array(struct vm_area_struct *vma,
			      unsigned long addr,
			      xen_pfn_t *gfn, int nr,
			      int *err_ptr, pgprot_t prot,
			      unsigned int domid,
			      struct page **pages);
int xen_xlate_unmap_gfn_range(struct vm_area_struct *vma,
			      int nr, struct page **pages);
#else
/*
 * These two functions are called from arch/x86/xen/mmu.c and so stubs
 * are needed for a configuration not specifying CONFIG_XEN_AUTO_XLATE.
 */
static inline int xen_xlate_remap_gfn_array(struct vm_area_struct *vma,
					    unsigned long addr,
					    xen_pfn_t *gfn, int nr,
					    int *err_ptr, pgprot_t prot,
					    unsigned int domid,
					    struct page **pages)
{
	return -EOPNOTSUPP;
}

static inline int xen_xlate_unmap_gfn_range(struct vm_area_struct *vma,
					    int nr, struct page **pages)
{
	return -EOPNOTSUPP;
}
#endif

/*
 * xen_remap_domain_gfn_array() - map an array of foreign frames by gfn
 * @vma:     VMA to map the pages into
 * @addr:    Address at which to map the pages
 * @gfn:     Array of GFNs to map
 * @nr:      Number entries in the GFN array
 * @err_ptr: Returns per-GFN error status.
 * @prot:    page protection mask
 * @domid:   Domain owning the pages
 * @pages:   Array of pages if this domain has an auto-translated physmap
 *
 * @gfn and @err_ptr may point to the same buffer, the GFNs will be
 * overwritten by the error codes after they are mapped.
 *
 * Returns the number of successfully mapped frames, or a -ve error
 * code.
 */
static inline int xen_remap_domain_gfn_array(struct vm_area_struct *vma,
					     unsigned long addr,
					     xen_pfn_t *gfn, int nr,
					     int *err_ptr, pgprot_t prot,
					     unsigned int domid,
					     struct page **pages)
{
	if (xen_feature(XENFEAT_auto_translated_physmap))
		return xen_xlate_remap_gfn_array(vma, addr, gfn, nr, err_ptr,
						 prot, domid, pages);

	/* We BUG_ON because it's a programmer error to pass a NULL err_ptr,
	 * and the consequences later is quite hard to detect what the actual
	 * cause of "wrong memory was mapped in".
	 */
	BUG_ON(err_ptr == NULL);
	return xen_remap_pfn(vma, addr, gfn, nr, err_ptr, prot, domid,
			     false, pages);
}

/*
 * xen_remap_domain_mfn_array() - map an array of foreign frames by mfn
 * @vma:     VMA to map the pages into
 * @addr:    Address at which to map the pages
 * @mfn:     Array of MFNs to map
 * @nr:      Number entries in the MFN array
 * @err_ptr: Returns per-MFN error status.
 * @prot:    page protection mask
 * @domid:   Domain owning the pages
 * @pages:   Array of pages if this domain has an auto-translated physmap
 *
 * @mfn and @err_ptr may point to the same buffer, the MFNs will be
 * overwritten by the error codes after they are mapped.
 *
 * Returns the number of successfully mapped frames, or a -ve error
 * code.
 */
static inline int xen_remap_domain_mfn_array(struct vm_area_struct *vma,
					     unsigned long addr, xen_pfn_t *mfn,
					     int nr, int *err_ptr,
					     pgprot_t prot, unsigned int domid,
					     struct page **pages)
{
	if (xen_feature(XENFEAT_auto_translated_physmap))
		return -EOPNOTSUPP;

	return xen_remap_pfn(vma, addr, mfn, nr, err_ptr, prot, domid,
			     true, pages);
}

/* xen_remap_domain_gfn_range() - map a range of foreign frames
 * @vma:     VMA to map the pages into
 * @addr:    Address at which to map the pages
 * @gfn:     First GFN to map.
 * @nr:      Number frames to map
 * @prot:    page protection mask
 * @domid:   Domain owning the pages
 * @pages:   Array of pages if this domain has an auto-translated physmap
 *
 * Returns the number of successfully mapped frames, or a -ve error
 * code.
 */
static inline int xen_remap_domain_gfn_range(struct vm_area_struct *vma,
					     unsigned long addr,
					     xen_pfn_t gfn, int nr,
					     pgprot_t prot, unsigned int domid,
					     struct page **pages)
{
	if (xen_feature(XENFEAT_auto_translated_physmap))
		return -EOPNOTSUPP;

	return xen_remap_pfn(vma, addr, &gfn, nr, NULL, prot, domid, false,
			     pages);
}

int xen_unmap_domain_gfn_range(struct vm_area_struct *vma,
			       int numpgs, struct page **pages);

int xen_xlate_map_ballooned_pages(xen_pfn_t **pfns, void **vaddr,
				  unsigned long nr_grant_frames);

bool xen_running_on_version_or_later(unsigned int major, unsigned int minor);

efi_status_t xen_efi_get_time(efi_time_t *tm, efi_time_cap_t *tc);
efi_status_t xen_efi_set_time(efi_time_t *tm);
efi_status_t xen_efi_get_wakeup_time(efi_bool_t *enabled, efi_bool_t *pending,
				     efi_time_t *tm);
efi_status_t xen_efi_set_wakeup_time(efi_bool_t enabled, efi_time_t *tm);
efi_status_t xen_efi_get_variable(efi_char16_t *name, efi_guid_t *vendor,
				  u32 *attr, unsigned long *data_size,
				  void *data);
efi_status_t xen_efi_get_next_variable(unsigned long *name_size,
				       efi_char16_t *name, efi_guid_t *vendor);
efi_status_t xen_efi_set_variable(efi_char16_t *name, efi_guid_t *vendor,
				  u32 attr, unsigned long data_size,
				  void *data);
efi_status_t xen_efi_query_variable_info(u32 attr, u64 *storage_space,
					 u64 *remaining_space,
					 u64 *max_variable_size);
efi_status_t xen_efi_get_next_high_mono_count(u32 *count);
efi_status_t xen_efi_update_capsule(efi_capsule_header_t **capsules,
				    unsigned long count, unsigned long sg_list);
efi_status_t xen_efi_query_capsule_caps(efi_capsule_header_t **capsules,
					unsigned long count, u64 *max_size,
					int *reset_type);
void xen_efi_reset_system(int reset_type, efi_status_t status,
			  unsigned long data_size, efi_char16_t *data);


#ifdef CONFIG_PREEMPT

static inline void xen_preemptible_hcall_begin(void)
{
}

static inline void xen_preemptible_hcall_end(void)
{
}

#else

DECLARE_PER_CPU(bool, xen_in_preemptible_hcall);

static inline void xen_preemptible_hcall_begin(void)
{
	__this_cpu_write(xen_in_preemptible_hcall, true);
}

static inline void xen_preemptible_hcall_end(void)
{
	__this_cpu_write(xen_in_preemptible_hcall, false);
}

#endif /* CONFIG_PREEMPT */

#endif /* INCLUDE_XEN_OPS_H */
