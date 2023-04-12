// SPDX-License-Identifier: GPL-2.0-only
/*
 * Userfaultfd unit tests.
 *
 *  Copyright (C) 2015-2023  Red Hat, Inc.
 */

#include "uffd-common.h"

#ifdef __NR_userfaultfd

/* The unit test doesn't need a large or random size, make it 32MB for now */
#define  UFFD_TEST_MEM_SIZE               (32UL << 20)

#define  MEM_ANON                         BIT_ULL(0)
#define  MEM_SHMEM                        BIT_ULL(1)
#define  MEM_SHMEM_PRIVATE                BIT_ULL(2)
#define  MEM_HUGETLB                      BIT_ULL(3)
#define  MEM_HUGETLB_PRIVATE              BIT_ULL(4)

struct mem_type {
	const char *name;
	unsigned int mem_flag;
	uffd_test_ops_t *mem_ops;
	bool shared;
};
typedef struct mem_type mem_type_t;

mem_type_t mem_types[] = {
	{
		.name = "anon",
		.mem_flag = MEM_ANON,
		.mem_ops = &anon_uffd_test_ops,
		.shared = false,
	},
	{
		.name = "shmem",
		.mem_flag = MEM_SHMEM,
		.mem_ops = &shmem_uffd_test_ops,
		.shared = true,
	},
	{
		.name = "shmem-private",
		.mem_flag = MEM_SHMEM_PRIVATE,
		.mem_ops = &shmem_uffd_test_ops,
		.shared = false,
	},
	{
		.name = "hugetlb",
		.mem_flag = MEM_HUGETLB,
		.mem_ops = &hugetlb_uffd_test_ops,
		.shared = true,
	},
	{
		.name = "hugetlb-private",
		.mem_flag = MEM_HUGETLB_PRIVATE,
		.mem_ops = &hugetlb_uffd_test_ops,
		.shared = false,
	},
};

/* Returns: UFFD_TEST_* */
typedef void (*uffd_test_fn)(void);

typedef struct {
	const char *name;
	uffd_test_fn uffd_fn;
	unsigned int mem_targets;
	uint64_t uffd_feature_required;
} uffd_test_case_t;

static void uffd_test_report(void)
{
	printf("Userfaults unit tests: pass=%u, skip=%u, fail=%u (total=%u)\n",
	       ksft_get_pass_cnt(),
	       ksft_get_xskip_cnt(),
	       ksft_get_fail_cnt(),
	       ksft_test_num());
}

static void uffd_test_pass(void)
{
	printf("done\n");
	ksft_inc_pass_cnt();
}

#define  uffd_test_start(...)  do {		\
		printf("Testing ");		\
		printf(__VA_ARGS__);		\
		printf("... ");			\
		fflush(stdout);			\
	} while (0)

#define  uffd_test_fail(...)  do {		\
		printf("failed [reason: ");	\
		printf(__VA_ARGS__);		\
		printf("]\n");			\
		ksft_inc_fail_cnt();		\
	} while (0)

#define  uffd_test_skip(...)  do {		\
		printf("skipped [reason: ");	\
		printf(__VA_ARGS__);		\
		printf("]\n");			\
		ksft_inc_xskip_cnt();		\
	} while (0)

/*
 * Returns 1 if specific userfaultfd supported, 0 otherwise.  Note, we'll
 * return 1 even if some test failed as long as uffd supported, because in
 * that case we still want to proceed with the rest uffd unit tests.
 */
static int test_uffd_api(bool use_dev)
{
	struct uffdio_api uffdio_api;
	int uffd;

	uffd_test_start("UFFDIO_API (with %s)",
			use_dev ? "/dev/userfaultfd" : "syscall");

	if (use_dev)
		uffd = uffd_open_dev(UFFD_FLAGS);
	else
		uffd = uffd_open_sys(UFFD_FLAGS);
	if (uffd < 0) {
		uffd_test_skip("cannot open userfaultfd handle");
		return 0;
	}

	/* Test wrong UFFD_API */
	uffdio_api.api = 0xab;
	uffdio_api.features = 0;
	if (ioctl(uffd, UFFDIO_API, &uffdio_api) == 0) {
		uffd_test_fail("UFFDIO_API should fail with wrong api but didn't");
		goto out;
	}

	/* Test wrong feature bit */
	uffdio_api.api = UFFD_API;
	uffdio_api.features = BIT_ULL(63);
	if (ioctl(uffd, UFFDIO_API, &uffdio_api) == 0) {
		uffd_test_fail("UFFDIO_API should fail with wrong feature but didn't");
		goto out;
	}

	/* Test normal UFFDIO_API */
	uffdio_api.api = UFFD_API;
	uffdio_api.features = 0;
	if (ioctl(uffd, UFFDIO_API, &uffdio_api)) {
		uffd_test_fail("UFFDIO_API should succeed but failed");
		goto out;
	}

	/* Test double requests of UFFDIO_API with a random feature set */
	uffdio_api.features = BIT_ULL(0);
	if (ioctl(uffd, UFFDIO_API, &uffdio_api) == 0) {
		uffd_test_fail("UFFDIO_API should reject initialized uffd");
		goto out;
	}

	uffd_test_pass();
out:
	close(uffd);
	/* We have a valid uffd handle */
	return 1;
}

/*
 * This function initializes the global variables.  TODO: remove global
 * vars and then remove this.
 */
static int uffd_setup_environment(uffd_test_case_t *test, mem_type_t *mem_type)
{
	map_shared = mem_type->shared;
	uffd_test_ops = mem_type->mem_ops;

	if (mem_type->mem_flag & (MEM_HUGETLB_PRIVATE | MEM_HUGETLB))
		page_size = default_huge_page_size();
	else
		page_size = psize();

	nr_pages = UFFD_TEST_MEM_SIZE / page_size;
	/* TODO: remove this global var.. it's so ugly */
	nr_cpus = 1;

	return uffd_test_ctx_init(test->uffd_feature_required);
}

static bool uffd_feature_supported(uffd_test_case_t *test)
{
	uint64_t features;

	if (uffd_get_features(&features))
		return false;

	return (features & test->uffd_feature_required) ==
	    test->uffd_feature_required;
}

uffd_test_case_t uffd_tests[] = {
};

int main(int argc, char *argv[])
{
	int n_tests = sizeof(uffd_tests) / sizeof(uffd_test_case_t);
	int n_mems = sizeof(mem_types) / sizeof(mem_type_t);
	uffd_test_case_t *test;
	mem_type_t *mem_type;
	char test_name[128];
	int has_uffd;
	int i, j;

	has_uffd = test_uffd_api(false);
	has_uffd |= test_uffd_api(true);

	if (!has_uffd) {
		printf("Userfaultfd not supported or unprivileged, skip all tests\n");
		exit(KSFT_SKIP);
	}

	for (i = 0; i < n_tests; i++) {
		test = &uffd_tests[i];
		for (j = 0; j < n_mems; j++) {
			mem_type = &mem_types[j];
			if (!(test->mem_targets & mem_type->mem_flag))
				continue;
			snprintf(test_name, sizeof(test_name),
				 "%s on %s", test->name, mem_type->name);

			uffd_test_start(test_name);
			if (!uffd_feature_supported(test)) {
				uffd_test_skip("feature missing");
				continue;
			}
			if (uffd_setup_environment(test, mem_type)) {
				uffd_test_skip("environment setup failed");
				continue;
			}
			test->uffd_fn();
		}
	}

	uffd_test_report();

	return ksft_get_fail_cnt() ? KSFT_FAIL : KSFT_PASS;
}

#else /* __NR_userfaultfd */

#warning "missing __NR_userfaultfd definition"

int main(void)
{
	printf("Skipping %s (missing __NR_userfaultfd)\n", __file__);
	return KSFT_SKIP;
}

#endif /* __NR_userfaultfd */
