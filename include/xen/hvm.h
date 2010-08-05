/* Simple wrappers around HVM functions */
#ifndef XEN_HVM_H__
#define XEN_HVM_H__

#include <xen/interface/hvm/params.h>
#include <asm/xen/hypercall.h>

static inline int hvm_get_parameter(int idx, uint64_t *value)
{
	struct xen_hvm_param xhv;
	int r;

	xhv.domid = DOMID_SELF;
	xhv.index = idx;
	r = HYPERVISOR_hvm_op(HVMOP_get_param, &xhv);
	if (r < 0) {
		printk(KERN_ERR "Cannot get hvm parameter %d: %d!\n",
			idx, r);
		return r;
	}
	*value = xhv.value;
	return r;
}

#define HVM_CALLBACK_VIA_TYPE_VECTOR 0x2
#define HVM_CALLBACK_VIA_TYPE_SHIFT 56
#define HVM_CALLBACK_VECTOR(x) (((uint64_t)HVM_CALLBACK_VIA_TYPE_VECTOR)<<\
		HVM_CALLBACK_VIA_TYPE_SHIFT | (x))

#endif /* XEN_HVM_H__ */
