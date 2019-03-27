/******************************************************************************
 * nmi.h
 * 
 * NMI callback registration and reason codes.
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
 * Copyright (c) 2005, Keir Fraser <keir@xensource.com>
 */

#ifndef __XEN_PUBLIC_NMI_H__
#define __XEN_PUBLIC_NMI_H__

#include "xen.h"

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
#if __XEN_INTERFACE_VERSION__ < 0x00040300 /* legacy alias of the above */
 /* Parity error reported via ISA port 0x61, bit 7. */
#define _XEN_NMIREASON_parity_error 1
#define XEN_NMIREASON_parity_error  (1UL << _XEN_NMIREASON_parity_error)
#endif
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
typedef struct xennmi_callback xennmi_callback_t;
DEFINE_XEN_GUEST_HANDLE(xennmi_callback_t);

/*
 * Deregister NMI callback for this (calling) VCPU.
 * arg == NULL.
 */
#define XENNMI_unregister_callback 1

#endif /* __XEN_PUBLIC_NMI_H__ */

/*
 * Local variables:
 * mode: C
 * c-file-style: "BSD"
 * c-basic-offset: 4
 * tab-width: 4
 * indent-tabs-mode: nil
 * End:
 */
