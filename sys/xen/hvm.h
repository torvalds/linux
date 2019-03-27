/*
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
 * $FreeBSD$
 */

#ifndef	__XEN_HVM_H__
#define	__XEN_HVM_H__

#include <xen/xen-os.h>
#include <xen/hypervisor.h>

#include <xen/interface/hvm/params.h>

/**
 * \brief Wrapper function to obtain a HVM parameter value.
 *
 * \param index	HVM parameter index; see <xen/interface/hvm/params.h>.
 * 
 * \returns	0 on failure; the value of the parameter otherwise.
 */
static inline unsigned long
hvm_get_parameter(int index)
{
	struct xen_hvm_param xhv;
	int error;

	xhv.domid = DOMID_SELF;
	xhv.index = index;
	error = HYPERVISOR_hvm_op(HVMOP_get_param, &xhv);
	if (error) {
		printf("%s: error %d trying to get %d\n", __func__,
		    error, index);
		return (0);
	}
	return (xhv.value);
}

/** The callback method types for Hypervisor event delivery to our domain. */
enum {
	HVM_CB_TYPE_GSI,
	HVM_CB_TYPE_PCI_INTX,
	HVM_CB_TYPE_VECTOR,
	HVM_CB_TYPE_MASK  = 0xFF,
	HVM_CB_TYPE_SHIFT = 56
};

/** Format for specifying a GSI type callback. */
enum {
	HVM_CB_GSI_GSI_MASK  = 0xFFFFFFFF,
	HVM_CB_GSI_GSI_SHIFT = 0
};
#define HVM_CALLBACK_GSI(gsi) \
    (((uint64_t)HVM_CB_TYPE_GSI << HVM_CB_TYPE_SHIFT) \
   | ((gsi) & HVM_CB_GSI_GSI_MASK) << HVM_CB_GSI_GSI_SHIFT)

/** Format for specifying a virtual PCI interrupt line GSI style callback. */
enum {
	HVM_CB_PCI_INTX_INTPIN_MASK  = 0x3,
	HVM_CB_PCI_INTX_INTPIN_SHIFT = 0,
	HVM_CB_PCI_INTX_SLOT_MASK    = 0x1F,
	HVM_CB_PCI_INTX_SLOT_SHIFT   = 11,
};
#define HVM_CALLBACK_PCI_INTX(slot, pin) \
    (((uint64_t)HVM_CB_TYPE_PCI_INTX << HVM_CB_TYPE_SHIFT) \
   | (((slot) & HVM_CB_PCI_INTX_SLOT_MASK) << HVM_CB_PCI_INTX_SLOT_SHIFT) \
   | (((pin) & HVM_CB_PCI_INTX_INTPIN_MASK) << HVM_CB_PCI_INTX_INTPIN_SHIFT))

/** Format for specifying a direct IDT vector injection style callback. */
enum {
	HVM_CB_VECTOR_VECTOR_MASK  = 0xFFFFFFFF,
	HVM_CB_VECTOR_VECTOR_SHIFT = 0
};
#define HVM_CALLBACK_VECTOR(vector) \
    (((uint64_t)HVM_CB_TYPE_VECTOR << HVM_CB_TYPE_SHIFT) \
   | (((vector) & HVM_CB_GSI_GSI_MASK) << HVM_CB_GSI_GSI_SHIFT))

enum xen_hvm_init_type {
	XEN_HVM_INIT_EARLY,
	XEN_HVM_INIT_LATE,
	XEN_HVM_INIT_CANCELLED_SUSPEND,
	XEN_HVM_INIT_RESUME,
};

int xen_hvm_init_hypercall_stubs(enum xen_hvm_init_type);
void xen_hvm_set_callback(device_t);
void xen_hvm_suspend(void);
void xen_hvm_resume(bool suspend_cancelled);

extern uint32_t hvm_start_flags;
extern bool xen_evtchn_needs_ack;

#endif	/* __XEN_HVM_H__ */
