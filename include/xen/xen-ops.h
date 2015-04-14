#ifndef INCLUDE_XEN_OPS_H
#define INCLUDE_XEN_OPS_H

#include <linux/percpu.h>
#include <linux/notifier.h>
#include <linux/efi.h>
#include <asm/xen/interface.h>

DECLARE_PER_CPU(struct vcpu_info *, xen_vcpu);

void xen_arch_pre_suspend(void);
void xen_arch_post_suspend(int suspend_cancelled);

void xen_timer_resume(void);
void xen_arch_resume(void);

void xen_resume_notifier_register(struct notifier_block *nb);
void xen_resume_notifier_unregister(struct notifier_block *nb);

int xen_setup_shutdown_event(void);

extern unsigned long *xen_contiguous_bitmap;
int xen_create_contiguous_region(phys_addr_t pstart, unsigned int order,
				unsigned int address_bits,
				dma_addr_t *dma_handle);

void xen_destroy_contiguous_region(phys_addr_t pstart, unsigned int order);

struct vm_area_struct;
int xen_remap_domain_mfn_range(struct vm_area_struct *vma,
			       unsigned long addr,
			       xen_pfn_t mfn, int nr,
			       pgprot_t prot, unsigned domid,
			       struct page **pages);
int xen_unmap_domain_mfn_range(struct vm_area_struct *vma,
			       int numpgs, struct page **pages);

bool xen_running_on_version_or_later(unsigned int major, unsigned int minor);

#ifdef CONFIG_XEN_EFI
extern efi_system_table_t *xen_efi_probe(void);
#else
static inline efi_system_table_t __init *xen_efi_probe(void)
{
	return NULL;
}
#endif

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
