// SPDX-License-Identifier: GPL-2.0
/*
 * XCR0 cpuid test
 *
 * Copyright (C) 2022, Google LLC.
 */
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>

#include "test_util.h"

#include "kvm_util.h"
#include "processor.h"

/*
 * Assert that architectural dependency rules are satisfied, e.g. that AVX is
 * supported if and only if SSE is supported.
 */
#define ASSERT_XFEATURE_DEPENDENCIES(supported_xcr0, xfeatures, dependencies)		\
do {											\
	uint64_t __supported = (supported_xcr0) & ((xfeatures) | (dependencies));	\
											\
	__GUEST_ASSERT((__supported & (xfeatures)) != (xfeatures) ||			\
		       __supported == ((xfeatures) | (dependencies)),			\
		       "supported = 0x%lx, xfeatures = 0x%llx, dependencies = 0x%llx",	\
		       __supported, (xfeatures), (dependencies));			\
} while (0)

/*
 * Assert that KVM reports a sane, usable as-is XCR0.  Architecturally, a CPU
 * isn't strictly required to _support_ all XFeatures related to a feature, but
 * at the same time XSETBV will #GP if bundled XFeatures aren't enabled and
 * disabled coherently.  E.g. a CPU can technically enumerate supported for
 * XTILE_CFG but not XTILE_DATA, but attempting to enable XTILE_CFG without
 * XTILE_DATA will #GP.
 */
#define ASSERT_ALL_OR_NONE_XFEATURE(supported_xcr0, xfeatures)		\
do {									\
	uint64_t __supported = (supported_xcr0) & (xfeatures);		\
									\
	__GUEST_ASSERT(!__supported || __supported == (xfeatures),	\
		       "supported = 0x%lx, xfeatures = 0x%llx",		\
		       __supported, (xfeatures));			\
} while (0)

static void guest_code(void)
{
	uint64_t xcr0_reset;
	uint64_t supported_xcr0;
	int i, vector;

	set_cr4(get_cr4() | X86_CR4_OSXSAVE);

	xcr0_reset = xgetbv(0);
	supported_xcr0 = this_cpu_supported_xcr0();

	GUEST_ASSERT(xcr0_reset == XFEATURE_MASK_FP);

	/* Check AVX */
	ASSERT_XFEATURE_DEPENDENCIES(supported_xcr0,
				     XFEATURE_MASK_YMM,
				     XFEATURE_MASK_SSE);

	/* Check MPX */
	ASSERT_ALL_OR_NONE_XFEATURE(supported_xcr0,
				    XFEATURE_MASK_BNDREGS | XFEATURE_MASK_BNDCSR);

	/* Check AVX-512 */
	ASSERT_XFEATURE_DEPENDENCIES(supported_xcr0,
				     XFEATURE_MASK_AVX512,
				     XFEATURE_MASK_SSE | XFEATURE_MASK_YMM);
	ASSERT_ALL_OR_NONE_XFEATURE(supported_xcr0,
				    XFEATURE_MASK_AVX512);

	/* Check AMX */
	ASSERT_ALL_OR_NONE_XFEATURE(supported_xcr0,
				    XFEATURE_MASK_XTILE);

	vector = xsetbv_safe(0, supported_xcr0);
	__GUEST_ASSERT(!vector,
		       "Expected success on XSETBV(0x%lx), got vector '0x%x'",
		       supported_xcr0, vector);

	for (i = 0; i < 64; i++) {
		if (supported_xcr0 & BIT_ULL(i))
			continue;

		vector = xsetbv_safe(0, supported_xcr0 | BIT_ULL(i));
		__GUEST_ASSERT(vector == GP_VECTOR,
			       "Expected #GP on XSETBV(0x%llx), supported XCR0 = %lx, got vector '0x%x'",
			       BIT_ULL(i), supported_xcr0, vector);
	}

	GUEST_DONE();
}

int main(int argc, char *argv[])
{
	struct kvm_vcpu *vcpu;
	struct kvm_run *run;
	struct kvm_vm *vm;
	struct ucall uc;

	TEST_REQUIRE(kvm_cpu_has(X86_FEATURE_XSAVE));

	vm = vm_create_with_one_vcpu(&vcpu, guest_code);
	run = vcpu->run;

	vm_init_descriptor_tables(vm);
	vcpu_init_descriptor_tables(vcpu);

	while (1) {
		vcpu_run(vcpu);

		TEST_ASSERT(run->exit_reason == KVM_EXIT_IO,
			    "Unexpected exit reason: %u (%s),",
			    run->exit_reason,
			    exit_reason_str(run->exit_reason));

		switch (get_ucall(vcpu, &uc)) {
		case UCALL_ABORT:
			REPORT_GUEST_ASSERT(uc);
			break;
		case UCALL_DONE:
			goto done;
		default:
			TEST_FAIL("Unknown ucall %lu", uc.cmd);
		}
	}

done:
	kvm_vm_free(vm);
	return 0;
}
