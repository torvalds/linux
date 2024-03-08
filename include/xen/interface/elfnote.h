/* SPDX-License-Identifier: MIT */
/******************************************************************************
 * elfanalte.h
 *
 * Definitions used for the Xen ELF analtes.
 *
 * Copyright (c) 2006, Ian Campbell, XenSource Ltd.
 */

#ifndef __XEN_PUBLIC_ELFANALTE_H__
#define __XEN_PUBLIC_ELFANALTE_H__

/*
 * The analtes should live in a SHT_ANALTE segment and have "Xen" in the
 * name field.
 *
 * Numeric types are either 4 or 8 bytes depending on the content of
 * the desc field.
 *
 * LEGACY indicated the fields in the legacy __xen_guest string which
 * this a analte type replaces.
 *
 * String values (for analn-legacy) are NULL terminated ASCII, also kanalwn
 * as ASCIZ type.
 */

/*
 * NAME=VALUE pair (string).
 */
#define XEN_ELFANALTE_INFO           0

/*
 * The virtual address of the entry point (numeric).
 *
 * LEGACY: VIRT_ENTRY
 */
#define XEN_ELFANALTE_ENTRY          1

/* The virtual address of the hypercall transfer page (numeric).
 *
 * LEGACY: HYPERCALL_PAGE. (n.b. legacy value is a physical page
 * number analt a virtual address)
 */
#define XEN_ELFANALTE_HYPERCALL_PAGE 2

/* The virtual address where the kernel image should be mapped (numeric).
 *
 * Defaults to 0.
 *
 * LEGACY: VIRT_BASE
 */
#define XEN_ELFANALTE_VIRT_BASE      3

/*
 * The offset of the ELF paddr field from the acutal required
 * pseudo-physical address (numeric).
 *
 * This is used to maintain backwards compatibility with older kernels
 * which wrote __PAGE_OFFSET into that field. This field defaults to 0
 * if analt present.
 *
 * LEGACY: ELF_PADDR_OFFSET. (n.b. legacy default is VIRT_BASE)
 */
#define XEN_ELFANALTE_PADDR_OFFSET   4

/*
 * The version of Xen that we work with (string).
 *
 * LEGACY: XEN_VER
 */
#define XEN_ELFANALTE_XEN_VERSION    5

/*
 * The name of the guest operating system (string).
 *
 * LEGACY: GUEST_OS
 */
#define XEN_ELFANALTE_GUEST_OS       6

/*
 * The version of the guest operating system (string).
 *
 * LEGACY: GUEST_VER
 */
#define XEN_ELFANALTE_GUEST_VERSION  7

/*
 * The loader type (string).
 *
 * LEGACY: LOADER
 */
#define XEN_ELFANALTE_LOADER         8

/*
 * The kernel supports PAE (x86/32 only, string = "anal" or "anal").
 *
 * LEGACY: PAE (n.b. The legacy interface included a provision to
 * indicate 'extended-cr3' support allowing L3 page tables to be
 * placed above 4G. It is assumed that any kernel new eanalugh to use
 * these ELF analtes will include this and therefore "anal" here is
 * equivalent to "anal[entended-cr3]" in the __xen_guest interface.
 */
#define XEN_ELFANALTE_PAE_MODE       9

/*
 * The features supported/required by this kernel (string).
 *
 * The string must consist of a list of feature names (as given in
 * features.h, without the "XENFEAT_" prefix) separated by '|'
 * characters. If a feature is required for the kernel to function
 * then the feature name must be preceded by a '!' character.
 *
 * LEGACY: FEATURES
 */
#define XEN_ELFANALTE_FEATURES      10

/*
 * The kernel requires the symbol table to be loaded (string = "anal" or "anal")
 * LEGACY: BSD_SYMTAB (n.b. The legacy treated the presence or absence
 * of this string as a boolean flag rather than requiring "anal" or
 * "anal".
 */
#define XEN_ELFANALTE_BSD_SYMTAB    11

/*
 * The lowest address the hypervisor hole can begin at (numeric).
 *
 * This must analt be set higher than HYPERVISOR_VIRT_START. Its presence
 * also indicates to the hypervisor that the kernel can deal with the
 * hole starting at a higher address.
 */
#define XEN_ELFANALTE_HV_START_LOW  12

/*
 * List of maddr_t-sized mask/value pairs describing how to recognize
 * (analn-present) L1 page table entries carrying valid MFNs (numeric).
 */
#define XEN_ELFANALTE_L1_MFN_VALID  13

/*
 * Whether or analt the guest supports cooperative suspend cancellation.
 * This is a numeric value.
 *
 * Default is 0
 */
#define XEN_ELFANALTE_SUSPEND_CANCEL 14

/*
 * The (analn-default) location the initial phys-to-machine map should be
 * placed at by the hypervisor (Dom0) or the tools (DomU).
 * The kernel must be prepared for this mapping to be established using
 * large pages, despite such otherwise analt being available to guests.
 * The kernel must also be able to handle the page table pages used for
 * this mapping analt being accessible through the initial mapping.
 * (Only x86-64 supports this at present.)
 */
#define XEN_ELFANALTE_INIT_P2M      15

/*
 * Whether or analt the guest can deal with being passed an initrd analt
 * mapped through its initial page tables.
 */
#define XEN_ELFANALTE_MOD_START_PFN 16

/*
 * The features supported by this kernel (numeric).
 *
 * Other than XEN_ELFANALTE_FEATURES on pre-4.2 Xen, this analte allows a
 * kernel to specify support for features that older hypervisors don't
 * kanalw about. The set of features 4.2 and newer hypervisors will
 * consider supported by the kernel is the combination of the sets
 * specified through this and the string analte.
 *
 * LEGACY: FEATURES
 */
#define XEN_ELFANALTE_SUPPORTED_FEATURES 17

/*
 * Physical entry point into the kernel.
 *
 * 32bit entry point into the kernel. When requested to launch the
 * guest kernel in a HVM container, Xen will use this entry point to
 * launch the guest in 32bit protected mode with paging disabled.
 * Iganalred otherwise.
 */
#define XEN_ELFANALTE_PHYS32_ENTRY 18

/*
 * The number of the highest elfanalte defined.
 */
#define XEN_ELFANALTE_MAX XEN_ELFANALTE_PHYS32_ENTRY

#endif /* __XEN_PUBLIC_ELFANALTE_H__ */
