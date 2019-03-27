/******************************************************************************
 * features.h
 * 
 * Feature flags, reported by XENVER_get_features.
 * 
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to
 * deal in the Software without restriction, including without limitation the
 * rights to use, copy, modify, merge, publish, distribute, sublicense, and/or
 * sell copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
 * FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER
 * DEALINGS IN THE SOFTWARE.
 *
 * Copyright (c) 2006, Keir Fraser <keir@xensource.com>
 */

#ifndef __XEN_PUBLIC_FEATURES_H__
#define __XEN_PUBLIC_FEATURES_H__

/*
 * `incontents 200 elfnotes_features XEN_ELFNOTE_FEATURES
 *
 * The list of all the features the guest supports. They are set by
 * parsing the XEN_ELFNOTE_FEATURES and XEN_ELFNOTE_SUPPORTED_FEATURES
 * string. The format is the  feature names (as given here without the
 * "XENFEAT_" prefix) separated by '|' characters.
 * If a feature is required for the kernel to function then the feature name
 * must be preceded by a '!' character.
 *
 * Note that if XEN_ELFNOTE_SUPPORTED_FEATURES is used, then in the
 * XENFEAT_dom0 MUST be set if the guest is to be booted as dom0,
 */

/*
 * If set, the guest does not need to write-protect its pagetables, and can
 * update them via direct writes.
 */
#define XENFEAT_writable_page_tables       0

/*
 * If set, the guest does not need to write-protect its segment descriptor
 * tables, and can update them via direct writes.
 */
#define XENFEAT_writable_descriptor_tables 1

/*
 * If set, translation between the guest's 'pseudo-physical' address space
 * and the host's machine address space are handled by the hypervisor. In this
 * mode the guest does not need to perform phys-to/from-machine translations
 * when performing page table operations.
 */
#define XENFEAT_auto_translated_physmap    2

/* If set, the guest is running in supervisor mode (e.g., x86 ring 0). */
#define XENFEAT_supervisor_mode_kernel     3

/*
 * If set, the guest does not need to allocate x86 PAE page directories
 * below 4GB. This flag is usually implied by auto_translated_physmap.
 */
#define XENFEAT_pae_pgdir_above_4gb        4

/* x86: Does this Xen host support the MMU_PT_UPDATE_PRESERVE_AD hypercall? */
#define XENFEAT_mmu_pt_update_preserve_ad  5

/* x86: Does this Xen host support the MMU_{CLEAR,COPY}_PAGE hypercall? */
#define XENFEAT_highmem_assist             6

/*
 * If set, GNTTABOP_map_grant_ref honors flags to be placed into guest kernel
 * available pte bits.
 */
#define XENFEAT_gnttab_map_avail_bits      7

/* x86: Does this Xen host support the HVM callback vector type? */
#define XENFEAT_hvm_callback_vector        8

/* x86: pvclock algorithm is safe to use on HVM */
#define XENFEAT_hvm_safe_pvclock           9

/* x86: pirq can be used by HVM guests */
#define XENFEAT_hvm_pirqs                 10

/* operation as Dom0 is supported */
#define XENFEAT_dom0                      11

/* Xen also maps grant references at pfn = mfn.
 * This feature flag is deprecated and should not be used.
#define XENFEAT_grant_map_identity        12
 */

/* Guest can use XENMEMF_vnode to specify virtual node for memory op. */
#define XENFEAT_memory_op_vnode_supported 13

#define XENFEAT_NR_SUBMAPS 1

#endif /* __XEN_PUBLIC_FEATURES_H__ */

/*
 * Local variables:
 * mode: C
 * c-file-style: "BSD"
 * c-basic-offset: 4
 * tab-width: 4
 * indent-tabs-mode: nil
 * End:
 */
