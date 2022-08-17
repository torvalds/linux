// SPDX-License-Identifier: GPL-2.0
/*
 * Test that KVM_GET_MSR_INDEX_LIST and
 * KVM_GET_MSR_FEATURE_INDEX_LIST work as intended
 *
 * Copyright (C) 2020, Red Hat, Inc.
 */
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>

#include "test_util.h"
#include "kvm_util.h"
#include "processor.h"

int main(int argc, char *argv[])
{
	const struct kvm_msr_list *feature_list;
	int i;

	/*
	 * Skip the entire test if MSR_FEATURES isn't supported, other tests
	 * will cover the "regular" list of MSRs, the coverage here is purely
	 * opportunistic and not interesting on its own.
	 */
	TEST_REQUIRE(kvm_has_cap(KVM_CAP_GET_MSR_FEATURES));

	(void)kvm_get_msr_index_list();

	feature_list = kvm_get_feature_msr_index_list();
	for (i = 0; i < feature_list->nmsrs; i++)
		kvm_get_feature_msr(feature_list->indices[i]);
}
