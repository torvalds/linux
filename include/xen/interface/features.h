/* SPDX-License-Identifier: GPL-2.0 */
/******************************************************************************
 * features.h
 *
 * Feature flags, reported by XENVER_get_features.
 *
 * Copyright (c) 2006, Keir Fraser <keir@xensource.com>
 */

#ifndef __XEN_PUBLIC_FEATURES_H__
#define __XEN_PUBLIC_FEATURES_H__

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
#define XENFEAT_hvm_pirqs           10

/* operation as Dom0 is supported */
#define XENFEAT_dom0                      11

#define XENFEAT_NR_SUBMAPS 1

#endif /* __XEN_PUBLIC_FEATURES_H__ */
