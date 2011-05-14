/******************************************************************************
 * elfnote.h
 *
 * Definitions used for the Xen ELF notes.
 *
 * Copyright (c) 2006, Ian Campbell, XenSource Ltd.
 */

#ifndef __XEN_PUBLIC_ELFNOTE_H__
#define __XEN_PUBLIC_ELFNOTE_H__

/*
 * The notes should live in a SHT_NOTE segment and have "Xen" in the
 * name field.
 *
 * Numeric types are either 4 or 8 bytes depending on the content of
 * the desc field.
 *
 * LEGACY indicated the fields in the legacy __xen_guest string which
 * this a note type replaces.
 */

/*
 * NAME=VALUE pair (string).
 *
 * LEGACY: FEATURES and PAE
 */
#define XEN_ELFNOTE_INFO           0

/*
 * The virtual address of the entry point (numeric).
 *
 * LEGACY: VIRT_ENTRY
 */
#define XEN_ELFNOTE_ENTRY          1

/* The virtual address of the hypercall transfer page (numeric).
 *
 * LEGACY: HYPERCALL_PAGE. (n.b. legacy value is a physical page
 * number not a virtual address)
 */
#define XEN_ELFNOTE_HYPERCALL_PAGE 2

/* The virtual address where the kernel image should be mapped (numeric).
 *
 * Defaults to 0.
 *
 * LEGACY: VIRT_BASE
 */
#define XEN_ELFNOTE_VIRT_BASE      3

/*
 * The offset of the ELF paddr field from the acutal required
 * pseudo-physical address (numeric).
 *
 * This is used to maintain backwards compatibility with older kernels
 * which wrote __PAGE_OFFSET into that field. This field defaults to 0
 * if not present.
 *
 * LEGACY: ELF_PADDR_OFFSET. (n.b. legacy default is VIRT_BASE)
 */
#define XEN_ELFNOTE_PADDR_OFFSET   4

/*
 * The version of Xen that we work with (string).
 *
 * LEGACY: XEN_VER
 */
#define XEN_ELFNOTE_XEN_VERSION    5

/*
 * The name of the guest operating system (string).
 *
 * LEGACY: GUEST_OS
 */
#define XEN_ELFNOTE_GUEST_OS       6

/*
 * The version of the guest operating system (string).
 *
 * LEGACY: GUEST_VER
 */
#define XEN_ELFNOTE_GUEST_VERSION  7

/*
 * The loader type (string).
 *
 * LEGACY: LOADER
 */
#define XEN_ELFNOTE_LOADER         8

/*
 * The kernel supports PAE (x86/32 only, string = "yes" or "no").
 *
 * LEGACY: PAE (n.b. The legacy interface included a provision to
 * indicate 'extended-cr3' support allowing L3 page tables to be
 * placed above 4G. It is assumed that any kernel new enough to use
 * these ELF notes will include this and therefore "yes" here is
 * equivalent to "yes[entended-cr3]" in the __xen_guest interface.
 */
#define XEN_ELFNOTE_PAE_MODE       9

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
#define XEN_ELFNOTE_FEATURES      10

/*
 * The kernel requires the symbol table to be loaded (string = "yes" or "no")
 * LEGACY: BSD_SYMTAB (n.b. The legacy treated the presence or absence
 * of this string as a boolean flag rather than requiring "yes" or
 * "no".
 */
#define XEN_ELFNOTE_BSD_SYMTAB    11

/*
 * The lowest address the hypervisor hole can begin at (numeric).
 *
 * This must not be set higher than HYPERVISOR_VIRT_START. Its presence
 * also indicates to the hypervisor that the kernel can deal with the
 * hole starting at a higher address.
 */
#define XEN_ELFNOTE_HV_START_LOW  12

/*
 * List of maddr_t-sized mask/value pairs describing how to recognize
 * (non-present) L1 page table entries carrying valid MFNs (numeric).
 */
#define XEN_ELFNOTE_L1_MFN_VALID  13

/*
 * Whether or not the guest supports cooperative suspend cancellation.
 */
#define XEN_ELFNOTE_SUSPEND_CANCEL 14

#endif /* __XEN_PUBLIC_ELFNOTE_H__ */

/*
 * Local variables:
 * mode: C
 * c-set-style: "BSD"
 * c-basic-offset: 4
 * tab-width: 4
 * indent-tabs-mode: nil
 * End:
 */
