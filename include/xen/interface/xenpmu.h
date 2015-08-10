#ifndef __XEN_PUBLIC_XENPMU_H__
#define __XEN_PUBLIC_XENPMU_H__

#include "xen.h"

#define XENPMU_VER_MAJ    0
#define XENPMU_VER_MIN    1

/*
 * ` enum neg_errnoval
 * ` HYPERVISOR_xenpmu_op(enum xenpmu_op cmd, struct xenpmu_params *args);
 *
 * @cmd  == XENPMU_* (PMU operation)
 * @args == struct xenpmu_params
 */
/* ` enum xenpmu_op { */
#define XENPMU_mode_get        0 /* Also used for getting PMU version */
#define XENPMU_mode_set        1
#define XENPMU_feature_get     2
#define XENPMU_feature_set     3
#define XENPMU_init            4
#define XENPMU_finish          5
#define XENPMU_lvtpc_set       6
#define XENPMU_flush           7

/* ` } */

/* Parameters structure for HYPERVISOR_xenpmu_op call */
struct xen_pmu_params {
	/* IN/OUT parameters */
	struct {
		uint32_t maj;
		uint32_t min;
	} version;
	uint64_t val;

	/* IN parameters */
	uint32_t vcpu;
	uint32_t pad;
};

/* PMU modes:
 * - XENPMU_MODE_OFF:   No PMU virtualization
 * - XENPMU_MODE_SELF:  Guests can profile themselves
 * - XENPMU_MODE_HV:    Guests can profile themselves, dom0 profiles
 *                      itself and Xen
 * - XENPMU_MODE_ALL:   Only dom0 has access to VPMU and it profiles
 *                      everyone: itself, the hypervisor and the guests.
 */
#define XENPMU_MODE_OFF           0
#define XENPMU_MODE_SELF          (1<<0)
#define XENPMU_MODE_HV            (1<<1)
#define XENPMU_MODE_ALL           (1<<2)

/*
 * PMU features:
 * - XENPMU_FEATURE_INTEL_BTS: Intel BTS support (ignored on AMD)
 */
#define XENPMU_FEATURE_INTEL_BTS  1

/*
 * Shared PMU data between hypervisor and PV(H) domains.
 *
 * The hypervisor fills out this structure during PMU interrupt and sends an
 * interrupt to appropriate VCPU.
 * Architecture-independent fields of xen_pmu_data are WO for the hypervisor
 * and RO for the guest but some fields in xen_pmu_arch can be writable
 * by both the hypervisor and the guest (see arch-$arch/pmu.h).
 */
struct xen_pmu_data {
	/* Interrupted VCPU */
	uint32_t vcpu_id;

	/*
	 * Physical processor on which the interrupt occurred. On non-privileged
	 * guests set to vcpu_id;
	 */
	uint32_t pcpu_id;

	/*
	 * Domain that was interrupted. On non-privileged guests set to
	 * DOMID_SELF.
	 * On privileged guests can be DOMID_SELF, DOMID_XEN, or, when in
	 * XENPMU_MODE_ALL mode, domain ID of another domain.
	 */
	domid_t  domain_id;

	uint8_t pad[6];

	/* Architecture-specific information */
	struct xen_pmu_arch pmu;
};

#endif /* __XEN_PUBLIC_XENPMU_H__ */
