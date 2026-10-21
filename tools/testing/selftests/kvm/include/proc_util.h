/* SPDX-License-Identifier: GPL-2.0-only */
#ifndef SELFTEST_KVM_PROC_UTIL_H
#define SELFTEST_KVM_PROC_UTIL_H

#include <stdint.h>

unsigned int vfio_msix_to_host_irq(const char *vfio_device_bdf, int msix);

void proc_irq_set_smp_affinity(unsigned int irq, int cpu);

#endif /* SELFTEST_KVM_PROC_UTIL_H */
