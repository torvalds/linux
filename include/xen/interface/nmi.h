/******************************************************************************
 * nmi.h
 *
 * NMI callback registration and reason codes.
 *
 * Copyright (c) 2005, Keir Fraser <keir@xensource.com>
 */

#ifndef __XEN_PUBLIC_NMI_H__
#define __XEN_PUBLIC_NMI_H__

#include <xen/interface/xen.h>

/*
 * NMI reason codes:
 * Currently these are x86-specific, stored in arch_shared_info.nmi_reason.
 */
 /* I/O-check error reported via ISA port 0x61, bit 6. */
#define _XEN_NMIREASON_io_error     0
#define XEN_NMIREASON_io_error      (1UL << _XEN_NMIREASON_io_error)
 /* PCI SERR reported via ISA port 0x61, bit 7. */
#define _XEN_NMIREASON_pci_serr     1
#define XEN_NMIREASON_pci_serr      (1UL << _XEN_NMIREASON_pci_serr)
 /* Unknown hardware-generated NMI. */
#define _XEN_NMIREASON_unknown      2
#define XEN_NMIREASON_unknown       (1UL << _XEN_NMIREASON_unknown)

/*
 * long nmi_op(unsigned int cmd, void *arg)
 * NB. All ops return zero on success, else a negative error code.
 */

/*
 * Register NMI callback for this (calling) VCPU. Currently this only makes
 * sense for domain 0, vcpu 0. All other callers will be returned EINVAL.
 * arg == pointer to xennmi_callback structure.
 */
#define XENNMI_register_callback   0
struct xennmi_callback {
    unsigned long handler_address;
    unsigned long pad;
};
DEFINE_GUEST_HANDLE_STRUCT(xennmi_callback);

/*
 * Deregister NMI callback for this (calling) VCPU.
 * arg == NULL.
 */
#define XENNMI_unregister_callback 1

#endif /* __XEN_PUBLIC_NMI_H__ */
