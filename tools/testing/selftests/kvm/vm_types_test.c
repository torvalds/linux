// SPDX-License-Identifier: GPL-2.0-only
/*
 * Verify that KVM_CREATE_VM accepts exactly the VM types enumerated by
 * KVM_CAP_VM_TYPES, and rejects every other type with -EINVAL.
 */
#include <errno.h>
#include <stdbool.h>
#include <unistd.h>

#include <linux/kvm.h>

#include "kvm_util.h"
#include "test_util.h"

int main(void)
{
	unsigned long type, supported_types;
	int kvm_fd;

	TEST_REQUIRE(kvm_has_cap(KVM_CAP_VM_TYPES));

	kvm_fd = open_kvm_dev_path_or_exit();
	supported_types = kvm_check_cap(KVM_CAP_VM_TYPES);
	pr_info("Supported VM types: 0x%lx\n", supported_types);

	/*
	 * For compatibility with 32-bit kernels, KVM_CHECK_EXTENSION restricts
	 * its return to 32-bit values, i.e. only types 0..31 can be advertised.
	 * Walk past that range as well to confirm that any out-of-range type is
	 * rejected rather than silently accepted (or truncated).
	 */
	for (type = 0; type < BITS_PER_TYPE(supported_types); type++) {
		int fd = __kvm_ioctl(kvm_fd, KVM_CREATE_VM, (void *)type);

		if (supported_types & BIT(type)) {
			TEST_ASSERT(fd >= 0,
				    "KVM_CREATE_VM(%lu) should succeed, supported types = 0x%lx",
				    type, supported_types);
			kvm_close(fd);
		} else {
			TEST_ASSERT(fd < 0 && errno == EINVAL,
				    "KVM_CREATE_VM(%lu) should fail with EINVAL, supported types = 0x%lx",
				    type, supported_types);
		}
	}

	return 0;
}
