#ifndef INCLUDE_XEN_OPS_H
#define INCLUDE_XEN_OPS_H

#include <linux/percpu.h>

DECLARE_PER_CPU(struct vcpu_info *, xen_vcpu);

#endif /* INCLUDE_XEN_OPS_H */
