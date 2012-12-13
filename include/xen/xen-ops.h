#ifndef INCLUDE_XEN_OPS_H
#define INCLUDE_XEN_OPS_H

#include <linux/percpu.h>
#include <asm/xen/interface.h>

DECLARE_PER_CPU(struct vcpu_info *, xen_vcpu);

void xen_arch_pre_suspend(void);
void xen_arch_post_suspend(int suspend_cancelled);
void xen_arch_hvm_post_suspend(int suspend_cancelled);

void xen_mm_pin_all(void);
void xen_mm_unpin_all(void);

void xen_timer_resume(void);
void xen_arch_resume(void);

int xen_setup_shutdown_event(void);

extern unsigned long *xen_contiguous_bitmap;
int xen_create_contiguous_region(unsigned long vstart, unsigned int order,
				unsigned int address_bits);

void xen_destroy_contiguous_region(unsigned long vstart, unsigned int order);

struct vm_area_struct;
int xen_remap_domain_mfn_range(struct vm_area_struct *vma,
			       unsigned long addr,
			       xen_pfn_t mfn, int nr,
			       pgprot_t prot, unsigned domid,
			       struct page **pages);
int xen_unmap_domain_mfn_range(struct vm_area_struct *vma,
			       int numpgs, struct page **pages);

bool xen_running_on_version_or_later(unsigned int major, unsigned int minor);
#endif /* INCLUDE_XEN_OPS_H */
