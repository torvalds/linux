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

static int kvm_num_index_msrs(int kvm_fd, int nmsrs)
{
	struct kvm_msr_list *list;
	int r;

	list = malloc(sizeof(*list) + nmsrs * sizeof(list->indices[0]));
	list->nmsrs = nmsrs;
	r = ioctl(kvm_fd, KVM_GET_MSR_INDEX_LIST, list);
	TEST_ASSERT(r == -1 && errno == E2BIG,
				"Unexpected result from KVM_GET_MSR_INDEX_LIST probe, r: %i",
				r);

	r = list->nmsrs;
	free(list);
	return r;
}

static void test_get_msr_index(void)
{
	int old_res, res, kvm_fd, r;
	struct kvm_msr_list *list;

	kvm_fd = open(KVM_DEV_PATH, O_RDONLY);
	if (kvm_fd < 0)
		exit(KSFT_SKIP);

	old_res = kvm_num_index_msrs(kvm_fd, 0);
	TEST_ASSERT(old_res != 0, "Expecting nmsrs to be > 0");

	if (old_res != 1) {
		res = kvm_num_index_msrs(kvm_fd, 1);
		TEST_ASSERT(res > 1, "Expecting nmsrs to be > 1");
		TEST_ASSERT(res == old_res, "Expecting nmsrs to be identical");
	}

	list = malloc(sizeof(*list) + old_res * sizeof(list->indices[0]));
	list->nmsrs = old_res;
	r = ioctl(kvm_fd, KVM_GET_MSR_INDEX_LIST, list);

	TEST_ASSERT(r == 0,
		    "Unexpected result from KVM_GET_MSR_FEATURE_INDEX_LIST, r: %i",
		    r);
	TEST_ASSERT(list->nmsrs == old_res, "Expecting nmsrs to be identical");
	free(list);

	close(kvm_fd);
}

static int kvm_num_feature_msrs(int kvm_fd, int nmsrs)
{
	struct kvm_msr_list *list;
	int r;

	list = malloc(sizeof(*list) + nmsrs * sizeof(list->indices[0]));
	list->nmsrs = nmsrs;
	r = ioctl(kvm_fd, KVM_GET_MSR_FEATURE_INDEX_LIST, list);
	TEST_ASSERT(r == -1 && errno == E2BIG,
		"Unexpected result from KVM_GET_MSR_FEATURE_INDEX_LIST probe, r: %i",
				r);

	r = list->nmsrs;
	free(list);
	return r;
}

struct kvm_msr_list *kvm_get_msr_feature_list(int kvm_fd, int nmsrs)
{
	struct kvm_msr_list *list;
	int r;

	list = malloc(sizeof(*list) + nmsrs * sizeof(list->indices[0]));
	list->nmsrs = nmsrs;
	r = ioctl(kvm_fd, KVM_GET_MSR_FEATURE_INDEX_LIST, list);

	TEST_ASSERT(r == 0,
		"Unexpected result from KVM_GET_MSR_FEATURE_INDEX_LIST, r: %i",
		r);

	return list;
}

static void test_get_msr_feature(void)
{
	int res, old_res, i, kvm_fd;
	struct kvm_msr_list *feature_list;

	kvm_fd = open(KVM_DEV_PATH, O_RDONLY);
	if (kvm_fd < 0)
		exit(KSFT_SKIP);

	old_res = kvm_num_feature_msrs(kvm_fd, 0);
	TEST_ASSERT(old_res != 0, "Expecting nmsrs to be > 0");

	if (old_res != 1) {
		res = kvm_num_feature_msrs(kvm_fd, 1);
		TEST_ASSERT(res > 1, "Expecting nmsrs to be > 1");
		TEST_ASSERT(res == old_res, "Expecting nmsrs to be identical");
	}

	feature_list = kvm_get_msr_feature_list(kvm_fd, old_res);
	TEST_ASSERT(old_res == feature_list->nmsrs,
				"Unmatching number of msr indexes");

	for (i = 0; i < feature_list->nmsrs; i++)
		kvm_get_feature_msr(feature_list->indices[i]);

	free(feature_list);
	close(kvm_fd);
}

int main(int argc, char *argv[])
{
	if (kvm_check_cap(KVM_CAP_GET_MSR_FEATURES))
		test_get_msr_feature();

	test_get_msr_index();
}
