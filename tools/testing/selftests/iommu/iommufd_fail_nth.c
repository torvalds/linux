// SPDX-License-Identifier: GPL-2.0-only
/* Copyright (c) 2021-2022, NVIDIA CORPORATION & AFFILIATES
 *
 * These tests are "kernel integrity" tests. They are looking for kernel
 * WARN/OOPS/kasn/etc splats triggered by kernel sanitizers & debugging
 * features. It does not attempt to verify that the system calls are doing what
 * they are supposed to do.
 *
 * The basic philosophy is to run a sequence of calls that will succeed and then
 * sweep every failure injection point on that call chain to look for
 * interesting things in error handling.
 *
 * This test is best run with:
 *  echo 1 > /proc/sys/kernel/panic_on_warn
 * If something is actually going wrong.
 */
#include <fcntl.h>
#include <dirent.h>

#define __EXPORTED_HEADERS__
#include <linux/vfio.h>

#include "iommufd_utils.h"

static bool have_fault_injection;

static int writeat(int dfd, const char *fn, const char *val)
{
	size_t val_len = strlen(val);
	ssize_t res;
	int fd;

	fd = openat(dfd, fn, O_WRONLY);
	if (fd == -1)
		return -1;
	res = write(fd, val, val_len);
	assert(res == val_len);
	close(fd);
	return 0;
}

static __attribute__((constructor)) void setup_buffer(void)
{
	PAGE_SIZE = sysconf(_SC_PAGE_SIZE);

	BUFFER_SIZE = 2*1024*1024;

	buffer = mmap(0, BUFFER_SIZE, PROT_READ | PROT_WRITE,
		      MAP_SHARED | MAP_ANONYMOUS, -1, 0);

	mfd_buffer = memfd_mmap(BUFFER_SIZE, PROT_READ | PROT_WRITE, MAP_SHARED,
				&mfd);
}

/*
 * This sets up fail_injection in a way that is useful for this test.
 * It does not attempt to restore things back to how they were.
 */
static __attribute__((constructor)) void setup_fault_injection(void)
{
	DIR *debugfs = opendir("/sys/kernel/debug/");
	struct dirent *dent;

	if (!debugfs)
		return;

	/* Allow any allocation call to be fault injected */
	if (writeat(dirfd(debugfs), "failslab/ignore-gfp-wait", "N"))
		return;
	writeat(dirfd(debugfs), "fail_page_alloc/ignore-gfp-wait", "N");
	writeat(dirfd(debugfs), "fail_page_alloc/ignore-gfp-highmem", "N");

	while ((dent = readdir(debugfs))) {
		char fn[300];

		if (strncmp(dent->d_name, "fail", 4) != 0)
			continue;

		/* We are looking for kernel splats, quiet down the log */
		snprintf(fn, sizeof(fn), "%s/verbose", dent->d_name);
		writeat(dirfd(debugfs), fn, "0");
	}
	closedir(debugfs);
	have_fault_injection = true;
}

struct fail_nth_state {
	int proc_fd;
	unsigned int iteration;
};

static void fail_nth_first(struct __test_metadata *_metadata,
			   struct fail_nth_state *nth_state)
{
	char buf[300];

	snprintf(buf, sizeof(buf), "/proc/self/task/%u/fail-nth", getpid());
	nth_state->proc_fd = open(buf, O_RDWR);
	ASSERT_NE(-1, nth_state->proc_fd);
}

static bool fail_nth_next(struct __test_metadata *_metadata,
			  struct fail_nth_state *nth_state,
			  int test_result)
{
	static const char disable_nth[] = "0";
	char buf[300];

	/*
	 * This is just an arbitrary limit based on the current kernel
	 * situation. Changes in the kernel can dramatically change the number of
	 * required fault injection sites, so if this hits it doesn't
	 * necessarily mean a test failure, just that the limit has to be made
	 * bigger.
	 */
	ASSERT_GT(400, nth_state->iteration);
	if (nth_state->iteration != 0) {
		ssize_t res;
		ssize_t res2;

		buf[0] = 0;
		/*
		 * Annoyingly disabling the nth can also fail. This means
		 * the test passed without triggering failure
		 */
		res = pread(nth_state->proc_fd, buf, sizeof(buf), 0);
		if (res == -1 && errno == EFAULT) {
			buf[0] = '1';
			buf[1] = '\n';
			res = 2;
		}

		res2 = pwrite(nth_state->proc_fd, disable_nth,
			      ARRAY_SIZE(disable_nth) - 1, 0);
		if (res2 == -1 && errno == EFAULT) {
			res2 = pwrite(nth_state->proc_fd, disable_nth,
				      ARRAY_SIZE(disable_nth) - 1, 0);
			buf[0] = '1';
			buf[1] = '\n';
		}
		ASSERT_EQ(ARRAY_SIZE(disable_nth) - 1, res2);

		/* printf("  nth %u result=%d nth=%u\n", nth_state->iteration,
		       test_result, atoi(buf)); */
		fflush(stdout);
		ASSERT_LT(1, res);
		if (res != 2 || buf[0] != '0' || buf[1] != '\n')
			return false;
	} else {
		/* printf("  nth %u result=%d\n", nth_state->iteration,
		       test_result); */
	}
	nth_state->iteration++;
	return true;
}

/*
 * This is called during the test to start failure injection. It allows the test
 * to do some setup that has already been swept and thus reduce the required
 * iterations.
 */
void __fail_nth_enable(struct __test_metadata *_metadata,
		       struct fail_nth_state *nth_state)
{
	char buf[300];
	size_t len;

	if (!nth_state->iteration)
		return;

	len = snprintf(buf, sizeof(buf), "%u", nth_state->iteration);
	ASSERT_EQ(len, pwrite(nth_state->proc_fd, buf, len, 0));
}
#define fail_nth_enable() __fail_nth_enable(_metadata, _nth_state)

#define TEST_FAIL_NTH(fixture_name, name)                                           \
	static int test_nth_##name(struct __test_metadata *_metadata,               \
				   FIXTURE_DATA(fixture_name) *self,                \
				   const FIXTURE_VARIANT(fixture_name)              \
					   *variant,                                \
				   struct fail_nth_state *_nth_state);              \
	TEST_F(fixture_name, name)                                                  \
	{                                                                           \
		struct fail_nth_state nth_state = {};                               \
		int test_result = 0;                                                \
										    \
		if (!have_fault_injection)                                          \
			SKIP(return,                                                \
				   "fault injection is not enabled in the kernel"); \
		fail_nth_first(_metadata, &nth_state);                              \
		ASSERT_EQ(0, test_nth_##name(_metadata, self, variant,              \
					     &nth_state));                          \
		while (fail_nth_next(_metadata, &nth_state, test_result)) {         \
			fixture_name##_teardown(_metadata, self, variant);          \
			fixture_name##_setup(_metadata, self, variant);             \
			test_result = test_nth_##name(_metadata, self,              \
						      variant, &nth_state);         \
		};                                                                  \
		ASSERT_EQ(0, test_result);                                          \
	}                                                                           \
	static int test_nth_##name(                                                 \
		struct __test_metadata __attribute__((unused)) *_metadata,          \
		FIXTURE_DATA(fixture_name) __attribute__((unused)) *self,           \
		const FIXTURE_VARIANT(fixture_name) __attribute__((unused))         \
			*variant,                                                   \
		struct fail_nth_state *_nth_state)

FIXTURE(basic_fail_nth)
{
	int fd;
	uint32_t access_id;
};

FIXTURE_SETUP(basic_fail_nth)
{
	self->fd = -1;
	self->access_id = 0;
}

FIXTURE_TEARDOWN(basic_fail_nth)
{
	int rc;

	if (self->access_id) {
		/* The access FD holds the iommufd open until it closes */
		rc = _test_cmd_destroy_access(self->access_id);
		assert(rc == 0);
	}
	teardown_iommufd(self->fd, _metadata);
}

/* Cover ioas.c */
TEST_FAIL_NTH(basic_fail_nth, basic)
{
	struct iommu_iova_range ranges[10];
	uint32_t ioas_id;
	__u64 iova;

	fail_nth_enable();

	self->fd = open("/dev/iommu", O_RDWR);
	if (self->fd == -1)
		return -1;

	if (_test_ioctl_ioas_alloc(self->fd, &ioas_id))
		return -1;

	{
		struct iommu_ioas_iova_ranges ranges_cmd = {
			.size = sizeof(ranges_cmd),
			.num_iovas = ARRAY_SIZE(ranges),
			.ioas_id = ioas_id,
			.allowed_iovas = (uintptr_t)ranges,
		};
		if (ioctl(self->fd, IOMMU_IOAS_IOVA_RANGES, &ranges_cmd))
			return -1;
	}

	{
		struct iommu_ioas_allow_iovas allow_cmd = {
			.size = sizeof(allow_cmd),
			.ioas_id = ioas_id,
			.num_iovas = 1,
			.allowed_iovas = (uintptr_t)ranges,
		};

		ranges[0].start = 16*1024;
		ranges[0].last = BUFFER_SIZE + 16 * 1024 * 600 - 1;
		if (ioctl(self->fd, IOMMU_IOAS_ALLOW_IOVAS, &allow_cmd))
			return -1;
	}

	if (_test_ioctl_ioas_map(self->fd, ioas_id, buffer, BUFFER_SIZE, &iova,
				 IOMMU_IOAS_MAP_WRITEABLE |
					 IOMMU_IOAS_MAP_READABLE))
		return -1;

	{
		struct iommu_ioas_copy copy_cmd = {
			.size = sizeof(copy_cmd),
			.flags = IOMMU_IOAS_MAP_WRITEABLE |
				 IOMMU_IOAS_MAP_READABLE,
			.dst_ioas_id = ioas_id,
			.src_ioas_id = ioas_id,
			.src_iova = iova,
			.length = sizeof(ranges),
		};

		if (ioctl(self->fd, IOMMU_IOAS_COPY, &copy_cmd))
			return -1;
	}

	if (_test_ioctl_ioas_unmap(self->fd, ioas_id, iova, BUFFER_SIZE,
				   NULL))
		return -1;
	/* Failure path of no IOVA to unmap */
	_test_ioctl_ioas_unmap(self->fd, ioas_id, iova, BUFFER_SIZE, NULL);
	return 0;
}

/* iopt_area_fill_domains() and iopt_area_fill_domain() */
TEST_FAIL_NTH(basic_fail_nth, map_domain)
{
	uint32_t ioas_id;
	__u32 stdev_id;
	__u32 hwpt_id;
	__u64 iova;

	self->fd = open("/dev/iommu", O_RDWR);
	if (self->fd == -1)
		return -1;

	if (_test_ioctl_ioas_alloc(self->fd, &ioas_id))
		return -1;

	if (_test_ioctl_set_temp_memory_limit(self->fd, 32))
		return -1;

	fail_nth_enable();

	if (_test_cmd_mock_domain(self->fd, ioas_id, &stdev_id, &hwpt_id, NULL))
		return -1;

	if (_test_ioctl_ioas_map(self->fd, ioas_id, buffer, 262144, &iova,
				 IOMMU_IOAS_MAP_WRITEABLE |
					 IOMMU_IOAS_MAP_READABLE))
		return -1;

	if (_test_ioctl_destroy(self->fd, stdev_id))
		return -1;

	if (_test_cmd_mock_domain(self->fd, ioas_id, &stdev_id, &hwpt_id, NULL))
		return -1;
	return 0;
}

/* iopt_area_fill_domains() and iopt_area_fill_domain() */
TEST_FAIL_NTH(basic_fail_nth, map_file_domain)
{
	uint32_t ioas_id;
	__u32 stdev_id;
	__u32 hwpt_id;
	__u64 iova;

	self->fd = open("/dev/iommu", O_RDWR);
	if (self->fd == -1)
		return -1;

	if (_test_ioctl_ioas_alloc(self->fd, &ioas_id))
		return -1;

	if (_test_ioctl_set_temp_memory_limit(self->fd, 32))
		return -1;

	fail_nth_enable();

	if (_test_cmd_mock_domain(self->fd, ioas_id, &stdev_id, &hwpt_id, NULL))
		return -1;

	if (_test_ioctl_ioas_map_file(self->fd, ioas_id, mfd, 0, 262144, &iova,
				      IOMMU_IOAS_MAP_WRITEABLE |
					      IOMMU_IOAS_MAP_READABLE))
		return -1;

	if (_test_ioctl_destroy(self->fd, stdev_id))
		return -1;

	if (_test_cmd_mock_domain(self->fd, ioas_id, &stdev_id, &hwpt_id, NULL))
		return -1;
	return 0;
}

TEST_FAIL_NTH(basic_fail_nth, map_two_domains)
{
	uint32_t ioas_id;
	__u32 stdev_id2;
	__u32 stdev_id;
	__u32 hwpt_id2;
	__u32 hwpt_id;
	__u64 iova;

	self->fd = open("/dev/iommu", O_RDWR);
	if (self->fd == -1)
		return -1;

	if (_test_ioctl_ioas_alloc(self->fd, &ioas_id))
		return -1;

	if (_test_ioctl_set_temp_memory_limit(self->fd, 32))
		return -1;

	if (_test_cmd_mock_domain(self->fd, ioas_id, &stdev_id, &hwpt_id, NULL))
		return -1;

	fail_nth_enable();

	if (_test_cmd_mock_domain(self->fd, ioas_id, &stdev_id2, &hwpt_id2,
				  NULL))
		return -1;

	if (_test_ioctl_ioas_map(self->fd, ioas_id, buffer, 262144, &iova,
				 IOMMU_IOAS_MAP_WRITEABLE |
					 IOMMU_IOAS_MAP_READABLE))
		return -1;

	if (_test_ioctl_destroy(self->fd, stdev_id))
		return -1;

	if (_test_ioctl_destroy(self->fd, stdev_id2))
		return -1;

	if (_test_cmd_mock_domain(self->fd, ioas_id, &stdev_id, &hwpt_id, NULL))
		return -1;
	if (_test_cmd_mock_domain(self->fd, ioas_id, &stdev_id2, &hwpt_id2,
				  NULL))
		return -1;
	return 0;
}

TEST_FAIL_NTH(basic_fail_nth, access_rw)
{
	uint64_t tmp_big[4096];
	uint32_t ioas_id;
	uint16_t tmp[32];
	__u64 iova;

	self->fd = open("/dev/iommu", O_RDWR);
	if (self->fd == -1)
		return -1;

	if (_test_ioctl_ioas_alloc(self->fd, &ioas_id))
		return -1;

	if (_test_ioctl_set_temp_memory_limit(self->fd, 32))
		return -1;

	if (_test_ioctl_ioas_map(self->fd, ioas_id, buffer, 262144, &iova,
				 IOMMU_IOAS_MAP_WRITEABLE |
					 IOMMU_IOAS_MAP_READABLE))
		return -1;

	fail_nth_enable();

	if (_test_cmd_create_access(self->fd, ioas_id, &self->access_id, 0))
		return -1;

	{
		struct iommu_test_cmd access_cmd = {
			.size = sizeof(access_cmd),
			.op = IOMMU_TEST_OP_ACCESS_RW,
			.id = self->access_id,
			.access_rw = { .iova = iova,
				       .length = sizeof(tmp),
				       .uptr = (uintptr_t)tmp },
		};

		// READ
		if (ioctl(self->fd, _IOMMU_TEST_CMD(IOMMU_TEST_OP_ACCESS_RW),
			  &access_cmd))
			return -1;

		access_cmd.access_rw.flags = MOCK_ACCESS_RW_WRITE;
		if (ioctl(self->fd, _IOMMU_TEST_CMD(IOMMU_TEST_OP_ACCESS_RW),
			  &access_cmd))
			return -1;

		access_cmd.access_rw.flags = MOCK_ACCESS_RW_SLOW_PATH;
		if (ioctl(self->fd, _IOMMU_TEST_CMD(IOMMU_TEST_OP_ACCESS_RW),
			  &access_cmd))
			return -1;
		access_cmd.access_rw.flags = MOCK_ACCESS_RW_SLOW_PATH |
					     MOCK_ACCESS_RW_WRITE;
		if (ioctl(self->fd, _IOMMU_TEST_CMD(IOMMU_TEST_OP_ACCESS_RW),
			  &access_cmd))
			return -1;
	}

	{
		struct iommu_test_cmd access_cmd = {
			.size = sizeof(access_cmd),
			.op = IOMMU_TEST_OP_ACCESS_RW,
			.id = self->access_id,
			.access_rw = { .iova = iova,
				       .flags = MOCK_ACCESS_RW_SLOW_PATH,
				       .length = sizeof(tmp_big),
				       .uptr = (uintptr_t)tmp_big },
		};

		if (ioctl(self->fd, _IOMMU_TEST_CMD(IOMMU_TEST_OP_ACCESS_RW),
			  &access_cmd))
			return -1;
	}
	if (_test_cmd_destroy_access(self->access_id))
		return -1;
	self->access_id = 0;
	return 0;
}

/* pages.c access functions */
TEST_FAIL_NTH(basic_fail_nth, access_pin)
{
	uint32_t access_pages_id;
	uint32_t ioas_id;
	__u64 iova;

	self->fd = open("/dev/iommu", O_RDWR);
	if (self->fd == -1)
		return -1;

	if (_test_ioctl_ioas_alloc(self->fd, &ioas_id))
		return -1;

	if (_test_ioctl_set_temp_memory_limit(self->fd, 32))
		return -1;

	if (_test_ioctl_ioas_map(self->fd, ioas_id, buffer, BUFFER_SIZE, &iova,
				 IOMMU_IOAS_MAP_WRITEABLE |
					 IOMMU_IOAS_MAP_READABLE))
		return -1;

	if (_test_cmd_create_access(self->fd, ioas_id, &self->access_id,
				    MOCK_FLAGS_ACCESS_CREATE_NEEDS_PIN_PAGES))
		return -1;

	fail_nth_enable();

	{
		struct iommu_test_cmd access_cmd = {
			.size = sizeof(access_cmd),
			.op = IOMMU_TEST_OP_ACCESS_PAGES,
			.id = self->access_id,
			.access_pages = { .iova = iova,
					  .length = BUFFER_SIZE,
					  .uptr = (uintptr_t)buffer },
		};

		if (ioctl(self->fd, _IOMMU_TEST_CMD(IOMMU_TEST_OP_ACCESS_RW),
			  &access_cmd))
			return -1;
		access_pages_id = access_cmd.access_pages.out_access_pages_id;
	}

	if (_test_cmd_destroy_access_pages(self->fd, self->access_id,
					   access_pages_id))
		return -1;

	if (_test_cmd_destroy_access(self->access_id))
		return -1;
	self->access_id = 0;
	return 0;
}

/* iopt_pages_fill_xarray() */
TEST_FAIL_NTH(basic_fail_nth, access_pin_domain)
{
	uint32_t access_pages_id;
	uint32_t ioas_id;
	__u32 stdev_id;
	__u32 hwpt_id;
	__u64 iova;

	self->fd = open("/dev/iommu", O_RDWR);
	if (self->fd == -1)
		return -1;

	if (_test_ioctl_ioas_alloc(self->fd, &ioas_id))
		return -1;

	if (_test_ioctl_set_temp_memory_limit(self->fd, 32))
		return -1;

	if (_test_cmd_mock_domain(self->fd, ioas_id, &stdev_id, &hwpt_id, NULL))
		return -1;

	if (_test_ioctl_ioas_map(self->fd, ioas_id, buffer, BUFFER_SIZE, &iova,
				 IOMMU_IOAS_MAP_WRITEABLE |
					 IOMMU_IOAS_MAP_READABLE))
		return -1;

	if (_test_cmd_create_access(self->fd, ioas_id, &self->access_id,
				    MOCK_FLAGS_ACCESS_CREATE_NEEDS_PIN_PAGES))
		return -1;

	fail_nth_enable();

	{
		struct iommu_test_cmd access_cmd = {
			.size = sizeof(access_cmd),
			.op = IOMMU_TEST_OP_ACCESS_PAGES,
			.id = self->access_id,
			.access_pages = { .iova = iova,
					  .length = BUFFER_SIZE,
					  .uptr = (uintptr_t)buffer },
		};

		if (ioctl(self->fd, _IOMMU_TEST_CMD(IOMMU_TEST_OP_ACCESS_RW),
			  &access_cmd))
			return -1;
		access_pages_id = access_cmd.access_pages.out_access_pages_id;
	}

	if (_test_cmd_destroy_access_pages(self->fd, self->access_id,
					   access_pages_id))
		return -1;

	if (_test_cmd_destroy_access(self->access_id))
		return -1;
	self->access_id = 0;

	if (_test_ioctl_destroy(self->fd, stdev_id))
		return -1;
	return 0;
}

/* device.c */
TEST_FAIL_NTH(basic_fail_nth, device)
{
	struct iommu_hwpt_selftest data = {
		.iotlb = IOMMU_TEST_IOTLB_DEFAULT,
	};
	struct iommu_test_hw_info info;
	uint32_t fault_id, fault_fd;
	uint32_t fault_hwpt_id;
	uint32_t ioas_id;
	uint32_t ioas_id2;
	uint32_t stdev_id;
	uint32_t idev_id;
	uint32_t hwpt_id;
	uint32_t viommu_id;
	uint32_t vdev_id;
	__u64 iova;

	self->fd = open("/dev/iommu", O_RDWR);
	if (self->fd == -1)
		return -1;

	if (_test_ioctl_ioas_alloc(self->fd, &ioas_id))
		return -1;

	if (_test_ioctl_ioas_alloc(self->fd, &ioas_id2))
		return -1;

	iova = MOCK_APERTURE_START;
	if (_test_ioctl_ioas_map(self->fd, ioas_id, buffer, PAGE_SIZE, &iova,
				 IOMMU_IOAS_MAP_FIXED_IOVA |
					 IOMMU_IOAS_MAP_WRITEABLE |
					 IOMMU_IOAS_MAP_READABLE))
		return -1;
	if (_test_ioctl_ioas_map(self->fd, ioas_id2, buffer, PAGE_SIZE, &iova,
				 IOMMU_IOAS_MAP_FIXED_IOVA |
					 IOMMU_IOAS_MAP_WRITEABLE |
					 IOMMU_IOAS_MAP_READABLE))
		return -1;

	fail_nth_enable();

	if (_test_cmd_mock_domain(self->fd, ioas_id, &stdev_id, NULL,
				  &idev_id))
		return -1;

	if (_test_cmd_get_hw_info(self->fd, idev_id, &info, sizeof(info), NULL))
		return -1;

	if (_test_cmd_hwpt_alloc(self->fd, idev_id, ioas_id, 0, 0, &hwpt_id,
				 IOMMU_HWPT_DATA_NONE, 0, 0))
		return -1;

	if (_test_cmd_mock_domain_replace(self->fd, stdev_id, ioas_id2, NULL))
		return -1;

	if (_test_cmd_mock_domain_replace(self->fd, stdev_id, hwpt_id, NULL))
		return -1;

	if (_test_cmd_hwpt_alloc(self->fd, idev_id, ioas_id, 0,
				 IOMMU_HWPT_ALLOC_NEST_PARENT, &hwpt_id,
				 IOMMU_HWPT_DATA_NONE, 0, 0))
		return -1;

	if (_test_cmd_viommu_alloc(self->fd, idev_id, hwpt_id,
				   IOMMU_VIOMMU_TYPE_SELFTEST, 0, &viommu_id))
		return -1;

	if (_test_cmd_vdevice_alloc(self->fd, viommu_id, idev_id, 0, &vdev_id))
		return -1;

	if (_test_ioctl_fault_alloc(self->fd, &fault_id, &fault_fd))
		return -1;
	close(fault_fd);

	if (_test_cmd_hwpt_alloc(self->fd, idev_id, hwpt_id, fault_id,
				 IOMMU_HWPT_FAULT_ID_VALID, &fault_hwpt_id,
				 IOMMU_HWPT_DATA_SELFTEST, &data, sizeof(data)))
		return -1;

	return 0;
}

TEST_HARNESS_MAIN
