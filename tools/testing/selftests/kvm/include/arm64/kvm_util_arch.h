/* SPDX-License-Identifier: GPL-2.0-only */
#ifndef SELFTEST_KVM_UTIL_ARCH_H
#define SELFTEST_KVM_UTIL_ARCH_H

struct kvm_vm_arch {
	bool	has_gic;
	int	gic_fd;
};

#endif  // SELFTEST_KVM_UTIL_ARCH_H
