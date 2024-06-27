// SPDX-License-Identifier: GPL-2.0-only
/* Copyright (c) 2021-2022, NVIDIA CORPORATION & AFFILIATES */
#include <stdlib.h>
#include <sys/mman.h>
#include <sys/eventfd.h>

#define __EXPORTED_HEADERS__
#include <linux/vfio.h>

#include "iommufd_utils.h"

static unsigned long HUGEPAGE_SIZE;

#define MOCK_PAGE_SIZE (PAGE_SIZE / 2)
#define MOCK_HUGE_PAGE_SIZE (512 * MOCK_PAGE_SIZE)

static unsigned long get_huge_page_size(void)
{
	char buf[80];
	int ret;
	int fd;

	fd = open("/sys/kernel/mm/transparent_hugepage/hpage_pmd_size",
		  O_RDONLY);
	if (fd < 0)
		return 2 * 1024 * 1024;

	ret = read(fd, buf, sizeof(buf));
	close(fd);
	if (ret <= 0 || ret == sizeof(buf))
		return 2 * 1024 * 1024;
	buf[ret] = 0;
	return strtoul(buf, NULL, 10);
}

static __attribute__((constructor)) void setup_sizes(void)
{
	void *vrc;
	int rc;

	PAGE_SIZE = sysconf(_SC_PAGE_SIZE);
	HUGEPAGE_SIZE = get_huge_page_size();

	BUFFER_SIZE = PAGE_SIZE * 16;
	rc = posix_memalign(&buffer, HUGEPAGE_SIZE, BUFFER_SIZE);
	assert(!rc);
	assert(buffer);
	assert((uintptr_t)buffer % HUGEPAGE_SIZE == 0);
	vrc = mmap(buffer, BUFFER_SIZE, PROT_READ | PROT_WRITE,
		   MAP_SHARED | MAP_ANONYMOUS | MAP_FIXED, -1, 0);
	assert(vrc == buffer);
}

FIXTURE(iommufd)
{
	int fd;
};

FIXTURE_SETUP(iommufd)
{
	self->fd = open("/dev/iommu", O_RDWR);
	ASSERT_NE(-1, self->fd);
}

FIXTURE_TEARDOWN(iommufd)
{
	teardown_iommufd(self->fd, _metadata);
}

TEST_F(iommufd, simple_close)
{
}

TEST_F(iommufd, cmd_fail)
{
	struct iommu_destroy cmd = { .size = sizeof(cmd), .id = 0 };

	/* object id is invalid */
	EXPECT_ERRNO(ENOENT, _test_ioctl_destroy(self->fd, 0));
	/* Bad pointer */
	EXPECT_ERRNO(EFAULT, ioctl(self->fd, IOMMU_DESTROY, NULL));
	/* Unknown ioctl */
	EXPECT_ERRNO(ENOTTY,
		     ioctl(self->fd, _IO(IOMMUFD_TYPE, IOMMUFD_CMD_BASE - 1),
			   &cmd));
}

TEST_F(iommufd, cmd_length)
{
#define TEST_LENGTH(_struct, _ioctl, _last)                              \
	{                                                                \
		size_t min_size = offsetofend(struct _struct, _last);    \
		struct {                                                 \
			struct _struct cmd;                              \
			uint8_t extra;                                   \
		} cmd = { .cmd = { .size = min_size - 1 },               \
			  .extra = UINT8_MAX };                          \
		int old_errno;                                           \
		int rc;                                                  \
									 \
		EXPECT_ERRNO(EINVAL, ioctl(self->fd, _ioctl, &cmd));     \
		cmd.cmd.size = sizeof(struct _struct) + 1;               \
		EXPECT_ERRNO(E2BIG, ioctl(self->fd, _ioctl, &cmd));      \
		cmd.cmd.size = sizeof(struct _struct);                   \
		rc = ioctl(self->fd, _ioctl, &cmd);                      \
		old_errno = errno;                                       \
		cmd.cmd.size = sizeof(struct _struct) + 1;               \
		cmd.extra = 0;                                           \
		if (rc) {                                                \
			EXPECT_ERRNO(old_errno,                          \
				     ioctl(self->fd, _ioctl, &cmd));     \
		} else {                                                 \
			ASSERT_EQ(0, ioctl(self->fd, _ioctl, &cmd));     \
		}                                                        \
	}

	TEST_LENGTH(iommu_destroy, IOMMU_DESTROY, id);
	TEST_LENGTH(iommu_hw_info, IOMMU_GET_HW_INFO, __reserved);
	TEST_LENGTH(iommu_hwpt_alloc, IOMMU_HWPT_ALLOC, __reserved);
	TEST_LENGTH(iommu_hwpt_invalidate, IOMMU_HWPT_INVALIDATE, __reserved);
	TEST_LENGTH(iommu_ioas_alloc, IOMMU_IOAS_ALLOC, out_ioas_id);
	TEST_LENGTH(iommu_ioas_iova_ranges, IOMMU_IOAS_IOVA_RANGES,
		    out_iova_alignment);
	TEST_LENGTH(iommu_ioas_allow_iovas, IOMMU_IOAS_ALLOW_IOVAS,
		    allowed_iovas);
	TEST_LENGTH(iommu_ioas_map, IOMMU_IOAS_MAP, iova);
	TEST_LENGTH(iommu_ioas_copy, IOMMU_IOAS_COPY, src_iova);
	TEST_LENGTH(iommu_ioas_unmap, IOMMU_IOAS_UNMAP, length);
	TEST_LENGTH(iommu_option, IOMMU_OPTION, val64);
	TEST_LENGTH(iommu_vfio_ioas, IOMMU_VFIO_IOAS, __reserved);
#undef TEST_LENGTH
}

TEST_F(iommufd, cmd_ex_fail)
{
	struct {
		struct iommu_destroy cmd;
		__u64 future;
	} cmd = { .cmd = { .size = sizeof(cmd), .id = 0 } };

	/* object id is invalid and command is longer */
	EXPECT_ERRNO(ENOENT, ioctl(self->fd, IOMMU_DESTROY, &cmd));
	/* future area is non-zero */
	cmd.future = 1;
	EXPECT_ERRNO(E2BIG, ioctl(self->fd, IOMMU_DESTROY, &cmd));
	/* Original command "works" */
	cmd.cmd.size = sizeof(cmd.cmd);
	EXPECT_ERRNO(ENOENT, ioctl(self->fd, IOMMU_DESTROY, &cmd));
	/* Short command fails */
	cmd.cmd.size = sizeof(cmd.cmd) - 1;
	EXPECT_ERRNO(EINVAL, ioctl(self->fd, IOMMU_DESTROY, &cmd));
}

TEST_F(iommufd, global_options)
{
	struct iommu_option cmd = {
		.size = sizeof(cmd),
		.option_id = IOMMU_OPTION_RLIMIT_MODE,
		.op = IOMMU_OPTION_OP_GET,
		.val64 = 1,
	};

	cmd.option_id = IOMMU_OPTION_RLIMIT_MODE;
	ASSERT_EQ(0, ioctl(self->fd, IOMMU_OPTION, &cmd));
	ASSERT_EQ(0, cmd.val64);

	/* This requires root */
	cmd.op = IOMMU_OPTION_OP_SET;
	cmd.val64 = 1;
	ASSERT_EQ(0, ioctl(self->fd, IOMMU_OPTION, &cmd));
	cmd.val64 = 2;
	EXPECT_ERRNO(EINVAL, ioctl(self->fd, IOMMU_OPTION, &cmd));

	cmd.op = IOMMU_OPTION_OP_GET;
	ASSERT_EQ(0, ioctl(self->fd, IOMMU_OPTION, &cmd));
	ASSERT_EQ(1, cmd.val64);

	cmd.op = IOMMU_OPTION_OP_SET;
	cmd.val64 = 0;
	ASSERT_EQ(0, ioctl(self->fd, IOMMU_OPTION, &cmd));

	cmd.op = IOMMU_OPTION_OP_GET;
	cmd.option_id = IOMMU_OPTION_HUGE_PAGES;
	EXPECT_ERRNO(ENOENT, ioctl(self->fd, IOMMU_OPTION, &cmd));
	cmd.op = IOMMU_OPTION_OP_SET;
	EXPECT_ERRNO(ENOENT, ioctl(self->fd, IOMMU_OPTION, &cmd));
}

FIXTURE(iommufd_ioas)
{
	int fd;
	uint32_t ioas_id;
	uint32_t stdev_id;
	uint32_t hwpt_id;
	uint32_t device_id;
	uint64_t base_iova;
};

FIXTURE_VARIANT(iommufd_ioas)
{
	unsigned int mock_domains;
	unsigned int memory_limit;
};

FIXTURE_SETUP(iommufd_ioas)
{
	unsigned int i;


	self->fd = open("/dev/iommu", O_RDWR);
	ASSERT_NE(-1, self->fd);
	test_ioctl_ioas_alloc(&self->ioas_id);

	if (!variant->memory_limit) {
		test_ioctl_set_default_memory_limit();
	} else {
		test_ioctl_set_temp_memory_limit(variant->memory_limit);
	}

	for (i = 0; i != variant->mock_domains; i++) {
		test_cmd_mock_domain(self->ioas_id, &self->stdev_id,
				     &self->hwpt_id, &self->device_id);
		self->base_iova = MOCK_APERTURE_START;
	}
}

FIXTURE_TEARDOWN(iommufd_ioas)
{
	test_ioctl_set_default_memory_limit();
	teardown_iommufd(self->fd, _metadata);
}

FIXTURE_VARIANT_ADD(iommufd_ioas, no_domain)
{
};

FIXTURE_VARIANT_ADD(iommufd_ioas, mock_domain)
{
	.mock_domains = 1,
};

FIXTURE_VARIANT_ADD(iommufd_ioas, two_mock_domain)
{
	.mock_domains = 2,
};

FIXTURE_VARIANT_ADD(iommufd_ioas, mock_domain_limit)
{
	.mock_domains = 1,
	.memory_limit = 16,
};

TEST_F(iommufd_ioas, ioas_auto_destroy)
{
}

TEST_F(iommufd_ioas, ioas_destroy)
{
	if (self->stdev_id) {
		/* IOAS cannot be freed while a device has a HWPT using it */
		EXPECT_ERRNO(EBUSY,
			     _test_ioctl_destroy(self->fd, self->ioas_id));
	} else {
		/* Can allocate and manually free an IOAS table */
		test_ioctl_destroy(self->ioas_id);
	}
}

TEST_F(iommufd_ioas, alloc_hwpt_nested)
{
	const uint32_t min_data_len =
		offsetofend(struct iommu_hwpt_selftest, iotlb);
	struct iommu_hwpt_selftest data = {
		.iotlb = IOMMU_TEST_IOTLB_DEFAULT,
	};
	struct iommu_hwpt_invalidate_selftest inv_reqs[2] = {};
	uint32_t nested_hwpt_id[2] = {};
	uint32_t num_inv;
	uint32_t parent_hwpt_id = 0;
	uint32_t parent_hwpt_id_not_work = 0;
	uint32_t test_hwpt_id = 0;

	if (self->device_id) {
		/* Negative tests */
		test_err_hwpt_alloc(ENOENT, self->ioas_id, self->device_id, 0,
				    &test_hwpt_id);
		test_err_hwpt_alloc(EINVAL, self->device_id, self->device_id, 0,
				    &test_hwpt_id);

		test_cmd_hwpt_alloc(self->device_id, self->ioas_id,
				    IOMMU_HWPT_ALLOC_NEST_PARENT,
				    &parent_hwpt_id);

		test_cmd_hwpt_alloc(self->device_id, self->ioas_id, 0,
				    &parent_hwpt_id_not_work);

		/* Negative nested tests */
		test_err_hwpt_alloc_nested(EINVAL, self->device_id,
					   parent_hwpt_id, 0,
					   &nested_hwpt_id[0],
					   IOMMU_HWPT_DATA_NONE, &data,
					   sizeof(data));
		test_err_hwpt_alloc_nested(EOPNOTSUPP, self->device_id,
					   parent_hwpt_id, 0,
					   &nested_hwpt_id[0],
					   IOMMU_HWPT_DATA_SELFTEST + 1, &data,
					   sizeof(data));
		test_err_hwpt_alloc_nested(EINVAL, self->device_id,
					   parent_hwpt_id, 0,
					   &nested_hwpt_id[0],
					   IOMMU_HWPT_DATA_SELFTEST, &data,
					   min_data_len - 1);
		test_err_hwpt_alloc_nested(EFAULT, self->device_id,
					   parent_hwpt_id, 0,
					   &nested_hwpt_id[0],
					   IOMMU_HWPT_DATA_SELFTEST, NULL,
					   sizeof(data));
		test_err_hwpt_alloc_nested(
			EOPNOTSUPP, self->device_id, parent_hwpt_id,
			IOMMU_HWPT_ALLOC_NEST_PARENT, &nested_hwpt_id[0],
			IOMMU_HWPT_DATA_SELFTEST, &data, sizeof(data));
		test_err_hwpt_alloc_nested(EINVAL, self->device_id,
					   parent_hwpt_id_not_work, 0,
					   &nested_hwpt_id[0],
					   IOMMU_HWPT_DATA_SELFTEST, &data,
					   sizeof(data));

		/* Allocate two nested hwpts sharing one common parent hwpt */
		test_cmd_hwpt_alloc_nested(self->device_id, parent_hwpt_id, 0,
					   &nested_hwpt_id[0],
					   IOMMU_HWPT_DATA_SELFTEST, &data,
					   sizeof(data));
		test_cmd_hwpt_alloc_nested(self->device_id, parent_hwpt_id, 0,
					   &nested_hwpt_id[1],
					   IOMMU_HWPT_DATA_SELFTEST, &data,
					   sizeof(data));
		test_cmd_hwpt_check_iotlb_all(nested_hwpt_id[0],
					      IOMMU_TEST_IOTLB_DEFAULT);
		test_cmd_hwpt_check_iotlb_all(nested_hwpt_id[1],
					      IOMMU_TEST_IOTLB_DEFAULT);

		/* Negative test: a nested hwpt on top of a nested hwpt */
		test_err_hwpt_alloc_nested(EINVAL, self->device_id,
					   nested_hwpt_id[0], 0, &test_hwpt_id,
					   IOMMU_HWPT_DATA_SELFTEST, &data,
					   sizeof(data));
		/* Negative test: parent hwpt now cannot be freed */
		EXPECT_ERRNO(EBUSY,
			     _test_ioctl_destroy(self->fd, parent_hwpt_id));

		/* hwpt_invalidate only supports a user-managed hwpt (nested) */
		num_inv = 1;
		test_err_hwpt_invalidate(ENOENT, parent_hwpt_id, inv_reqs,
					 IOMMU_HWPT_INVALIDATE_DATA_SELFTEST,
					 sizeof(*inv_reqs), &num_inv);
		assert(!num_inv);

		/* Check data_type by passing zero-length array */
		num_inv = 0;
		test_cmd_hwpt_invalidate(nested_hwpt_id[0], inv_reqs,
					 IOMMU_HWPT_INVALIDATE_DATA_SELFTEST,
					 sizeof(*inv_reqs), &num_inv);
		assert(!num_inv);

		/* Negative test: Invalid data_type */
		num_inv = 1;
		test_err_hwpt_invalidate(EINVAL, nested_hwpt_id[0], inv_reqs,
					 IOMMU_HWPT_INVALIDATE_DATA_SELFTEST_INVALID,
					 sizeof(*inv_reqs), &num_inv);
		assert(!num_inv);

		/* Negative test: structure size sanity */
		num_inv = 1;
		test_err_hwpt_invalidate(EINVAL, nested_hwpt_id[0], inv_reqs,
					 IOMMU_HWPT_INVALIDATE_DATA_SELFTEST,
					 sizeof(*inv_reqs) + 1, &num_inv);
		assert(!num_inv);

		num_inv = 1;
		test_err_hwpt_invalidate(EINVAL, nested_hwpt_id[0], inv_reqs,
					 IOMMU_HWPT_INVALIDATE_DATA_SELFTEST,
					 1, &num_inv);
		assert(!num_inv);

		/* Negative test: invalid flag is passed */
		num_inv = 1;
		inv_reqs[0].flags = 0xffffffff;
		test_err_hwpt_invalidate(EOPNOTSUPP, nested_hwpt_id[0], inv_reqs,
					 IOMMU_HWPT_INVALIDATE_DATA_SELFTEST,
					 sizeof(*inv_reqs), &num_inv);
		assert(!num_inv);

		/* Negative test: invalid data_uptr when array is not empty */
		num_inv = 1;
		inv_reqs[0].flags = 0;
		test_err_hwpt_invalidate(EINVAL, nested_hwpt_id[0], NULL,
					 IOMMU_HWPT_INVALIDATE_DATA_SELFTEST,
					 sizeof(*inv_reqs), &num_inv);
		assert(!num_inv);

		/* Negative test: invalid entry_len when array is not empty */
		num_inv = 1;
		inv_reqs[0].flags = 0;
		test_err_hwpt_invalidate(EINVAL, nested_hwpt_id[0], inv_reqs,
					 IOMMU_HWPT_INVALIDATE_DATA_SELFTEST,
					 0, &num_inv);
		assert(!num_inv);

		/* Negative test: invalid iotlb_id */
		num_inv = 1;
		inv_reqs[0].flags = 0;
		inv_reqs[0].iotlb_id = MOCK_NESTED_DOMAIN_IOTLB_ID_MAX + 1;
		test_err_hwpt_invalidate(EINVAL, nested_hwpt_id[0], inv_reqs,
					 IOMMU_HWPT_INVALIDATE_DATA_SELFTEST,
					 sizeof(*inv_reqs), &num_inv);
		assert(!num_inv);

		/*
		 * Invalidate the 1st iotlb entry but fail the 2nd request
		 * due to invalid flags configuration in the 2nd request.
		 */
		num_inv = 2;
		inv_reqs[0].flags = 0;
		inv_reqs[0].iotlb_id = 0;
		inv_reqs[1].flags = 0xffffffff;
		inv_reqs[1].iotlb_id = 1;
		test_err_hwpt_invalidate(EOPNOTSUPP, nested_hwpt_id[0], inv_reqs,
					 IOMMU_HWPT_INVALIDATE_DATA_SELFTEST,
					 sizeof(*inv_reqs), &num_inv);
		assert(num_inv == 1);
		test_cmd_hwpt_check_iotlb(nested_hwpt_id[0], 0, 0);
		test_cmd_hwpt_check_iotlb(nested_hwpt_id[0], 1,
					  IOMMU_TEST_IOTLB_DEFAULT);
		test_cmd_hwpt_check_iotlb(nested_hwpt_id[0], 2,
					  IOMMU_TEST_IOTLB_DEFAULT);
		test_cmd_hwpt_check_iotlb(nested_hwpt_id[0], 3,
					  IOMMU_TEST_IOTLB_DEFAULT);

		/*
		 * Invalidate the 1st iotlb entry but fail the 2nd request
		 * due to invalid iotlb_id configuration in the 2nd request.
		 */
		num_inv = 2;
		inv_reqs[0].flags = 0;
		inv_reqs[0].iotlb_id = 0;
		inv_reqs[1].flags = 0;
		inv_reqs[1].iotlb_id = MOCK_NESTED_DOMAIN_IOTLB_ID_MAX + 1;
		test_err_hwpt_invalidate(EINVAL, nested_hwpt_id[0], inv_reqs,
					 IOMMU_HWPT_INVALIDATE_DATA_SELFTEST,
					 sizeof(*inv_reqs), &num_inv);
		assert(num_inv == 1);
		test_cmd_hwpt_check_iotlb(nested_hwpt_id[0], 0, 0);
		test_cmd_hwpt_check_iotlb(nested_hwpt_id[0], 1,
					  IOMMU_TEST_IOTLB_DEFAULT);
		test_cmd_hwpt_check_iotlb(nested_hwpt_id[0], 2,
					  IOMMU_TEST_IOTLB_DEFAULT);
		test_cmd_hwpt_check_iotlb(nested_hwpt_id[0], 3,
					  IOMMU_TEST_IOTLB_DEFAULT);

		/* Invalidate the 2nd iotlb entry and verify */
		num_inv = 1;
		inv_reqs[0].flags = 0;
		inv_reqs[0].iotlb_id = 1;
		test_cmd_hwpt_invalidate(nested_hwpt_id[0], inv_reqs,
					 IOMMU_HWPT_INVALIDATE_DATA_SELFTEST,
					 sizeof(*inv_reqs), &num_inv);
		assert(num_inv == 1);
		test_cmd_hwpt_check_iotlb(nested_hwpt_id[0], 0, 0);
		test_cmd_hwpt_check_iotlb(nested_hwpt_id[0], 1, 0);
		test_cmd_hwpt_check_iotlb(nested_hwpt_id[0], 2,
					  IOMMU_TEST_IOTLB_DEFAULT);
		test_cmd_hwpt_check_iotlb(nested_hwpt_id[0], 3,
					  IOMMU_TEST_IOTLB_DEFAULT);

		/* Invalidate the 3rd and 4th iotlb entries and verify */
		num_inv = 2;
		inv_reqs[0].flags = 0;
		inv_reqs[0].iotlb_id = 2;
		inv_reqs[1].flags = 0;
		inv_reqs[1].iotlb_id = 3;
		test_cmd_hwpt_invalidate(nested_hwpt_id[0], inv_reqs,
					 IOMMU_HWPT_INVALIDATE_DATA_SELFTEST,
					 sizeof(*inv_reqs), &num_inv);
		assert(num_inv == 2);
		test_cmd_hwpt_check_iotlb_all(nested_hwpt_id[0], 0);

		/* Invalidate all iotlb entries for nested_hwpt_id[1] and verify */
		num_inv = 1;
		inv_reqs[0].flags = IOMMU_TEST_INVALIDATE_FLAG_ALL;
		test_cmd_hwpt_invalidate(nested_hwpt_id[1], inv_reqs,
					 IOMMU_HWPT_INVALIDATE_DATA_SELFTEST,
					 sizeof(*inv_reqs), &num_inv);
		assert(num_inv == 1);
		test_cmd_hwpt_check_iotlb_all(nested_hwpt_id[1], 0);

		/* Attach device to nested_hwpt_id[0] that then will be busy */
		test_cmd_mock_domain_replace(self->stdev_id, nested_hwpt_id[0]);
		EXPECT_ERRNO(EBUSY,
			     _test_ioctl_destroy(self->fd, nested_hwpt_id[0]));

		/* Switch from nested_hwpt_id[0] to nested_hwpt_id[1] */
		test_cmd_mock_domain_replace(self->stdev_id, nested_hwpt_id[1]);
		EXPECT_ERRNO(EBUSY,
			     _test_ioctl_destroy(self->fd, nested_hwpt_id[1]));
		test_ioctl_destroy(nested_hwpt_id[0]);

		/* Detach from nested_hwpt_id[1] and destroy it */
		test_cmd_mock_domain_replace(self->stdev_id, parent_hwpt_id);
		test_ioctl_destroy(nested_hwpt_id[1]);

		/* Detach from the parent hw_pagetable and destroy it */
		test_cmd_mock_domain_replace(self->stdev_id, self->ioas_id);
		test_ioctl_destroy(parent_hwpt_id);
		test_ioctl_destroy(parent_hwpt_id_not_work);
	} else {
		test_err_hwpt_alloc(ENOENT, self->device_id, self->ioas_id, 0,
				    &parent_hwpt_id);
		test_err_hwpt_alloc_nested(ENOENT, self->device_id,
					   parent_hwpt_id, 0,
					   &nested_hwpt_id[0],
					   IOMMU_HWPT_DATA_SELFTEST, &data,
					   sizeof(data));
		test_err_hwpt_alloc_nested(ENOENT, self->device_id,
					   parent_hwpt_id, 0,
					   &nested_hwpt_id[1],
					   IOMMU_HWPT_DATA_SELFTEST, &data,
					   sizeof(data));
		test_err_mock_domain_replace(ENOENT, self->stdev_id,
					     nested_hwpt_id[0]);
		test_err_mock_domain_replace(ENOENT, self->stdev_id,
					     nested_hwpt_id[1]);
	}
}

TEST_F(iommufd_ioas, hwpt_attach)
{
	/* Create a device attached directly to a hwpt */
	if (self->stdev_id) {
		test_cmd_mock_domain(self->hwpt_id, NULL, NULL, NULL);
	} else {
		test_err_mock_domain(ENOENT, self->hwpt_id, NULL, NULL);
	}
}

TEST_F(iommufd_ioas, ioas_area_destroy)
{
	/* Adding an area does not change ability to destroy */
	test_ioctl_ioas_map_fixed(buffer, PAGE_SIZE, self->base_iova);
	if (self->stdev_id)
		EXPECT_ERRNO(EBUSY,
			     _test_ioctl_destroy(self->fd, self->ioas_id));
	else
		test_ioctl_destroy(self->ioas_id);
}

TEST_F(iommufd_ioas, ioas_area_auto_destroy)
{
	int i;

	/* Can allocate and automatically free an IOAS table with many areas */
	for (i = 0; i != 10; i++) {
		test_ioctl_ioas_map_fixed(buffer, PAGE_SIZE,
					  self->base_iova + i * PAGE_SIZE);
	}
}

TEST_F(iommufd_ioas, get_hw_info)
{
	struct iommu_test_hw_info buffer_exact;
	struct iommu_test_hw_info_buffer_larger {
		struct iommu_test_hw_info info;
		uint64_t trailing_bytes;
	} buffer_larger;
	struct iommu_test_hw_info_buffer_smaller {
		__u32 flags;
	} buffer_smaller;

	if (self->device_id) {
		/* Provide a zero-size user_buffer */
		test_cmd_get_hw_info(self->device_id, NULL, 0);
		/* Provide a user_buffer with exact size */
		test_cmd_get_hw_info(self->device_id, &buffer_exact, sizeof(buffer_exact));
		/*
		 * Provide a user_buffer with size larger than the exact size to check if
		 * kernel zero the trailing bytes.
		 */
		test_cmd_get_hw_info(self->device_id, &buffer_larger, sizeof(buffer_larger));
		/*
		 * Provide a user_buffer with size smaller than the exact size to check if
		 * the fields within the size range still gets updated.
		 */
		test_cmd_get_hw_info(self->device_id, &buffer_smaller, sizeof(buffer_smaller));
	} else {
		test_err_get_hw_info(ENOENT, self->device_id,
				     &buffer_exact, sizeof(buffer_exact));
		test_err_get_hw_info(ENOENT, self->device_id,
				     &buffer_larger, sizeof(buffer_larger));
	}
}

TEST_F(iommufd_ioas, area)
{
	int i;

	/* Unmap fails if nothing is mapped */
	for (i = 0; i != 10; i++)
		test_err_ioctl_ioas_unmap(ENOENT, i * PAGE_SIZE, PAGE_SIZE);

	/* Unmap works */
	for (i = 0; i != 10; i++)
		test_ioctl_ioas_map_fixed(buffer, PAGE_SIZE,
					  self->base_iova + i * PAGE_SIZE);
	for (i = 0; i != 10; i++)
		test_ioctl_ioas_unmap(self->base_iova + i * PAGE_SIZE,
				      PAGE_SIZE);

	/* Split fails */
	test_ioctl_ioas_map_fixed(buffer, PAGE_SIZE * 2,
				  self->base_iova + 16 * PAGE_SIZE);
	test_err_ioctl_ioas_unmap(ENOENT, self->base_iova + 16 * PAGE_SIZE,
				  PAGE_SIZE);
	test_err_ioctl_ioas_unmap(ENOENT, self->base_iova + 17 * PAGE_SIZE,
				  PAGE_SIZE);

	/* Over map fails */
	test_err_ioctl_ioas_map_fixed(EEXIST, buffer, PAGE_SIZE * 2,
				      self->base_iova + 16 * PAGE_SIZE);
	test_err_ioctl_ioas_map_fixed(EEXIST, buffer, PAGE_SIZE,
				      self->base_iova + 16 * PAGE_SIZE);
	test_err_ioctl_ioas_map_fixed(EEXIST, buffer, PAGE_SIZE,
				      self->base_iova + 17 * PAGE_SIZE);
	test_err_ioctl_ioas_map_fixed(EEXIST, buffer, PAGE_SIZE * 2,
				      self->base_iova + 15 * PAGE_SIZE);
	test_err_ioctl_ioas_map_fixed(EEXIST, buffer, PAGE_SIZE * 3,
				      self->base_iova + 15 * PAGE_SIZE);

	/* unmap all works */
	test_ioctl_ioas_unmap(0, UINT64_MAX);

	/* Unmap all succeeds on an empty IOAS */
	test_ioctl_ioas_unmap(0, UINT64_MAX);
}

TEST_F(iommufd_ioas, unmap_fully_contained_areas)
{
	uint64_t unmap_len;
	int i;

	/* Give no_domain some space to rewind base_iova */
	self->base_iova += 4 * PAGE_SIZE;

	for (i = 0; i != 4; i++)
		test_ioctl_ioas_map_fixed(buffer, 8 * PAGE_SIZE,
					  self->base_iova + i * 16 * PAGE_SIZE);

	/* Unmap not fully contained area doesn't work */
	test_err_ioctl_ioas_unmap(ENOENT, self->base_iova - 4 * PAGE_SIZE,
				  8 * PAGE_SIZE);
	test_err_ioctl_ioas_unmap(ENOENT,
				  self->base_iova + 3 * 16 * PAGE_SIZE +
					  8 * PAGE_SIZE - 4 * PAGE_SIZE,
				  8 * PAGE_SIZE);

	/* Unmap fully contained areas works */
	ASSERT_EQ(0, _test_ioctl_ioas_unmap(self->fd, self->ioas_id,
					    self->base_iova - 4 * PAGE_SIZE,
					    3 * 16 * PAGE_SIZE + 8 * PAGE_SIZE +
						    4 * PAGE_SIZE,
					    &unmap_len));
	ASSERT_EQ(32 * PAGE_SIZE, unmap_len);
}

TEST_F(iommufd_ioas, area_auto_iova)
{
	struct iommu_test_cmd test_cmd = {
		.size = sizeof(test_cmd),
		.op = IOMMU_TEST_OP_ADD_RESERVED,
		.id = self->ioas_id,
		.add_reserved = { .start = PAGE_SIZE * 4,
				  .length = PAGE_SIZE * 100 },
	};
	struct iommu_iova_range ranges[1] = {};
	struct iommu_ioas_allow_iovas allow_cmd = {
		.size = sizeof(allow_cmd),
		.ioas_id = self->ioas_id,
		.num_iovas = 1,
		.allowed_iovas = (uintptr_t)ranges,
	};
	__u64 iovas[10];
	int i;

	/* Simple 4k pages */
	for (i = 0; i != 10; i++)
		test_ioctl_ioas_map(buffer, PAGE_SIZE, &iovas[i]);
	for (i = 0; i != 10; i++)
		test_ioctl_ioas_unmap(iovas[i], PAGE_SIZE);

	/* Kernel automatically aligns IOVAs properly */
	for (i = 0; i != 10; i++) {
		size_t length = PAGE_SIZE * (i + 1);

		if (self->stdev_id) {
			test_ioctl_ioas_map(buffer, length, &iovas[i]);
		} else {
			test_ioctl_ioas_map((void *)(1UL << 31), length,
					    &iovas[i]);
		}
		EXPECT_EQ(0, iovas[i] % (1UL << (ffs(length) - 1)));
	}
	for (i = 0; i != 10; i++)
		test_ioctl_ioas_unmap(iovas[i], PAGE_SIZE * (i + 1));

	/* Avoids a reserved region */
	ASSERT_EQ(0,
		  ioctl(self->fd, _IOMMU_TEST_CMD(IOMMU_TEST_OP_ADD_RESERVED),
			&test_cmd));
	for (i = 0; i != 10; i++) {
		size_t length = PAGE_SIZE * (i + 1);

		test_ioctl_ioas_map(buffer, length, &iovas[i]);
		EXPECT_EQ(0, iovas[i] % (1UL << (ffs(length) - 1)));
		EXPECT_EQ(false,
			  iovas[i] > test_cmd.add_reserved.start &&
				  iovas[i] <
					  test_cmd.add_reserved.start +
						  test_cmd.add_reserved.length);
	}
	for (i = 0; i != 10; i++)
		test_ioctl_ioas_unmap(iovas[i], PAGE_SIZE * (i + 1));

	/* Allowed region intersects with a reserved region */
	ranges[0].start = PAGE_SIZE;
	ranges[0].last = PAGE_SIZE * 600;
	EXPECT_ERRNO(EADDRINUSE,
		     ioctl(self->fd, IOMMU_IOAS_ALLOW_IOVAS, &allow_cmd));

	/* Allocate from an allowed region */
	if (self->stdev_id) {
		ranges[0].start = MOCK_APERTURE_START + PAGE_SIZE;
		ranges[0].last = MOCK_APERTURE_START + PAGE_SIZE * 600 - 1;
	} else {
		ranges[0].start = PAGE_SIZE * 200;
		ranges[0].last = PAGE_SIZE * 600 - 1;
	}
	ASSERT_EQ(0, ioctl(self->fd, IOMMU_IOAS_ALLOW_IOVAS, &allow_cmd));
	for (i = 0; i != 10; i++) {
		size_t length = PAGE_SIZE * (i + 1);

		test_ioctl_ioas_map(buffer, length, &iovas[i]);
		EXPECT_EQ(0, iovas[i] % (1UL << (ffs(length) - 1)));
		EXPECT_EQ(true, iovas[i] >= ranges[0].start);
		EXPECT_EQ(true, iovas[i] <= ranges[0].last);
		EXPECT_EQ(true, iovas[i] + length > ranges[0].start);
		EXPECT_EQ(true, iovas[i] + length <= ranges[0].last + 1);
	}
	for (i = 0; i != 10; i++)
		test_ioctl_ioas_unmap(iovas[i], PAGE_SIZE * (i + 1));
}

TEST_F(iommufd_ioas, area_allowed)
{
	struct iommu_test_cmd test_cmd = {
		.size = sizeof(test_cmd),
		.op = IOMMU_TEST_OP_ADD_RESERVED,
		.id = self->ioas_id,
		.add_reserved = { .start = PAGE_SIZE * 4,
				  .length = PAGE_SIZE * 100 },
	};
	struct iommu_iova_range ranges[1] = {};
	struct iommu_ioas_allow_iovas allow_cmd = {
		.size = sizeof(allow_cmd),
		.ioas_id = self->ioas_id,
		.num_iovas = 1,
		.allowed_iovas = (uintptr_t)ranges,
	};

	/* Reserved intersects an allowed */
	allow_cmd.num_iovas = 1;
	ranges[0].start = self->base_iova;
	ranges[0].last = ranges[0].start + PAGE_SIZE * 600;
	ASSERT_EQ(0, ioctl(self->fd, IOMMU_IOAS_ALLOW_IOVAS, &allow_cmd));
	test_cmd.add_reserved.start = ranges[0].start + PAGE_SIZE;
	test_cmd.add_reserved.length = PAGE_SIZE;
	EXPECT_ERRNO(EADDRINUSE,
		     ioctl(self->fd,
			   _IOMMU_TEST_CMD(IOMMU_TEST_OP_ADD_RESERVED),
			   &test_cmd));
	allow_cmd.num_iovas = 0;
	ASSERT_EQ(0, ioctl(self->fd, IOMMU_IOAS_ALLOW_IOVAS, &allow_cmd));

	/* Allowed intersects a reserved */
	ASSERT_EQ(0,
		  ioctl(self->fd, _IOMMU_TEST_CMD(IOMMU_TEST_OP_ADD_RESERVED),
			&test_cmd));
	allow_cmd.num_iovas = 1;
	ranges[0].start = self->base_iova;
	ranges[0].last = ranges[0].start + PAGE_SIZE * 600;
	EXPECT_ERRNO(EADDRINUSE,
		     ioctl(self->fd, IOMMU_IOAS_ALLOW_IOVAS, &allow_cmd));
}

TEST_F(iommufd_ioas, copy_area)
{
	struct iommu_ioas_copy copy_cmd = {
		.size = sizeof(copy_cmd),
		.flags = IOMMU_IOAS_MAP_FIXED_IOVA,
		.dst_ioas_id = self->ioas_id,
		.src_ioas_id = self->ioas_id,
		.length = PAGE_SIZE,
	};

	test_ioctl_ioas_map_fixed(buffer, PAGE_SIZE, self->base_iova);

	/* Copy inside a single IOAS */
	copy_cmd.src_iova = self->base_iova;
	copy_cmd.dst_iova = self->base_iova + PAGE_SIZE;
	ASSERT_EQ(0, ioctl(self->fd, IOMMU_IOAS_COPY, &copy_cmd));

	/* Copy between IOAS's */
	copy_cmd.src_iova = self->base_iova;
	copy_cmd.dst_iova = 0;
	test_ioctl_ioas_alloc(&copy_cmd.dst_ioas_id);
	ASSERT_EQ(0, ioctl(self->fd, IOMMU_IOAS_COPY, &copy_cmd));
}

TEST_F(iommufd_ioas, iova_ranges)
{
	struct iommu_test_cmd test_cmd = {
		.size = sizeof(test_cmd),
		.op = IOMMU_TEST_OP_ADD_RESERVED,
		.id = self->ioas_id,
		.add_reserved = { .start = PAGE_SIZE, .length = PAGE_SIZE },
	};
	struct iommu_iova_range *ranges = buffer;
	struct iommu_ioas_iova_ranges ranges_cmd = {
		.size = sizeof(ranges_cmd),
		.ioas_id = self->ioas_id,
		.num_iovas = BUFFER_SIZE / sizeof(*ranges),
		.allowed_iovas = (uintptr_t)ranges,
	};

	/* Range can be read */
	ASSERT_EQ(0, ioctl(self->fd, IOMMU_IOAS_IOVA_RANGES, &ranges_cmd));
	EXPECT_EQ(1, ranges_cmd.num_iovas);
	if (!self->stdev_id) {
		EXPECT_EQ(0, ranges[0].start);
		EXPECT_EQ(SIZE_MAX, ranges[0].last);
		EXPECT_EQ(1, ranges_cmd.out_iova_alignment);
	} else {
		EXPECT_EQ(MOCK_APERTURE_START, ranges[0].start);
		EXPECT_EQ(MOCK_APERTURE_LAST, ranges[0].last);
		EXPECT_EQ(MOCK_PAGE_SIZE, ranges_cmd.out_iova_alignment);
	}

	/* Buffer too small */
	memset(ranges, 0, BUFFER_SIZE);
	ranges_cmd.num_iovas = 0;
	EXPECT_ERRNO(EMSGSIZE,
		     ioctl(self->fd, IOMMU_IOAS_IOVA_RANGES, &ranges_cmd));
	EXPECT_EQ(1, ranges_cmd.num_iovas);
	EXPECT_EQ(0, ranges[0].start);
	EXPECT_EQ(0, ranges[0].last);

	/* 2 ranges */
	ASSERT_EQ(0,
		  ioctl(self->fd, _IOMMU_TEST_CMD(IOMMU_TEST_OP_ADD_RESERVED),
			&test_cmd));
	ranges_cmd.num_iovas = BUFFER_SIZE / sizeof(*ranges);
	ASSERT_EQ(0, ioctl(self->fd, IOMMU_IOAS_IOVA_RANGES, &ranges_cmd));
	if (!self->stdev_id) {
		EXPECT_EQ(2, ranges_cmd.num_iovas);
		EXPECT_EQ(0, ranges[0].start);
		EXPECT_EQ(PAGE_SIZE - 1, ranges[0].last);
		EXPECT_EQ(PAGE_SIZE * 2, ranges[1].start);
		EXPECT_EQ(SIZE_MAX, ranges[1].last);
	} else {
		EXPECT_EQ(1, ranges_cmd.num_iovas);
		EXPECT_EQ(MOCK_APERTURE_START, ranges[0].start);
		EXPECT_EQ(MOCK_APERTURE_LAST, ranges[0].last);
	}

	/* Buffer too small */
	memset(ranges, 0, BUFFER_SIZE);
	ranges_cmd.num_iovas = 1;
	if (!self->stdev_id) {
		EXPECT_ERRNO(EMSGSIZE, ioctl(self->fd, IOMMU_IOAS_IOVA_RANGES,
					     &ranges_cmd));
		EXPECT_EQ(2, ranges_cmd.num_iovas);
		EXPECT_EQ(0, ranges[0].start);
		EXPECT_EQ(PAGE_SIZE - 1, ranges[0].last);
	} else {
		ASSERT_EQ(0,
			  ioctl(self->fd, IOMMU_IOAS_IOVA_RANGES, &ranges_cmd));
		EXPECT_EQ(1, ranges_cmd.num_iovas);
		EXPECT_EQ(MOCK_APERTURE_START, ranges[0].start);
		EXPECT_EQ(MOCK_APERTURE_LAST, ranges[0].last);
	}
	EXPECT_EQ(0, ranges[1].start);
	EXPECT_EQ(0, ranges[1].last);
}

TEST_F(iommufd_ioas, access_domain_destory)
{
	struct iommu_test_cmd access_cmd = {
		.size = sizeof(access_cmd),
		.op = IOMMU_TEST_OP_ACCESS_PAGES,
		.access_pages = { .iova = self->base_iova + PAGE_SIZE,
				  .length = PAGE_SIZE},
	};
	size_t buf_size = 2 * HUGEPAGE_SIZE;
	uint8_t *buf;

	buf = mmap(0, buf_size, PROT_READ | PROT_WRITE,
		   MAP_SHARED | MAP_ANONYMOUS | MAP_HUGETLB | MAP_POPULATE, -1,
		   0);
	ASSERT_NE(MAP_FAILED, buf);
	test_ioctl_ioas_map_fixed(buf, buf_size, self->base_iova);

	test_cmd_create_access(self->ioas_id, &access_cmd.id,
			       MOCK_FLAGS_ACCESS_CREATE_NEEDS_PIN_PAGES);
	access_cmd.access_pages.uptr = (uintptr_t)buf + PAGE_SIZE;
	ASSERT_EQ(0,
		  ioctl(self->fd, _IOMMU_TEST_CMD(IOMMU_TEST_OP_ACCESS_PAGES),
			&access_cmd));

	/* Causes a complicated unpin across a huge page boundary */
	if (self->stdev_id)
		test_ioctl_destroy(self->stdev_id);

	test_cmd_destroy_access_pages(
		access_cmd.id, access_cmd.access_pages.out_access_pages_id);
	test_cmd_destroy_access(access_cmd.id);
	ASSERT_EQ(0, munmap(buf, buf_size));
}

TEST_F(iommufd_ioas, access_pin)
{
	struct iommu_test_cmd access_cmd = {
		.size = sizeof(access_cmd),
		.op = IOMMU_TEST_OP_ACCESS_PAGES,
		.access_pages = { .iova = MOCK_APERTURE_START,
				  .length = BUFFER_SIZE,
				  .uptr = (uintptr_t)buffer },
	};
	struct iommu_test_cmd check_map_cmd = {
		.size = sizeof(check_map_cmd),
		.op = IOMMU_TEST_OP_MD_CHECK_MAP,
		.check_map = { .iova = MOCK_APERTURE_START,
			       .length = BUFFER_SIZE,
			       .uptr = (uintptr_t)buffer },
	};
	uint32_t access_pages_id;
	unsigned int npages;

	test_cmd_create_access(self->ioas_id, &access_cmd.id,
			       MOCK_FLAGS_ACCESS_CREATE_NEEDS_PIN_PAGES);

	for (npages = 1; npages < BUFFER_SIZE / PAGE_SIZE; npages++) {
		uint32_t mock_stdev_id;
		uint32_t mock_hwpt_id;

		access_cmd.access_pages.length = npages * PAGE_SIZE;

		/* Single map/unmap */
		test_ioctl_ioas_map_fixed(buffer, BUFFER_SIZE,
					  MOCK_APERTURE_START);
		ASSERT_EQ(0, ioctl(self->fd,
				   _IOMMU_TEST_CMD(IOMMU_TEST_OP_ACCESS_PAGES),
				   &access_cmd));
		test_cmd_destroy_access_pages(
			access_cmd.id,
			access_cmd.access_pages.out_access_pages_id);

		/* Double user */
		ASSERT_EQ(0, ioctl(self->fd,
				   _IOMMU_TEST_CMD(IOMMU_TEST_OP_ACCESS_PAGES),
				   &access_cmd));
		access_pages_id = access_cmd.access_pages.out_access_pages_id;
		ASSERT_EQ(0, ioctl(self->fd,
				   _IOMMU_TEST_CMD(IOMMU_TEST_OP_ACCESS_PAGES),
				   &access_cmd));
		test_cmd_destroy_access_pages(
			access_cmd.id,
			access_cmd.access_pages.out_access_pages_id);
		test_cmd_destroy_access_pages(access_cmd.id, access_pages_id);

		/* Add/remove a domain with a user */
		ASSERT_EQ(0, ioctl(self->fd,
				   _IOMMU_TEST_CMD(IOMMU_TEST_OP_ACCESS_PAGES),
				   &access_cmd));
		test_cmd_mock_domain(self->ioas_id, &mock_stdev_id,
				     &mock_hwpt_id, NULL);
		check_map_cmd.id = mock_hwpt_id;
		ASSERT_EQ(0, ioctl(self->fd,
				   _IOMMU_TEST_CMD(IOMMU_TEST_OP_MD_CHECK_MAP),
				   &check_map_cmd));

		test_ioctl_destroy(mock_stdev_id);
		test_cmd_destroy_access_pages(
			access_cmd.id,
			access_cmd.access_pages.out_access_pages_id);

		test_ioctl_ioas_unmap(MOCK_APERTURE_START, BUFFER_SIZE);
	}
	test_cmd_destroy_access(access_cmd.id);
}

TEST_F(iommufd_ioas, access_pin_unmap)
{
	struct iommu_test_cmd access_pages_cmd = {
		.size = sizeof(access_pages_cmd),
		.op = IOMMU_TEST_OP_ACCESS_PAGES,
		.access_pages = { .iova = MOCK_APERTURE_START,
				  .length = BUFFER_SIZE,
				  .uptr = (uintptr_t)buffer },
	};

	test_cmd_create_access(self->ioas_id, &access_pages_cmd.id,
			       MOCK_FLAGS_ACCESS_CREATE_NEEDS_PIN_PAGES);
	test_ioctl_ioas_map_fixed(buffer, BUFFER_SIZE, MOCK_APERTURE_START);
	ASSERT_EQ(0,
		  ioctl(self->fd, _IOMMU_TEST_CMD(IOMMU_TEST_OP_ACCESS_PAGES),
			&access_pages_cmd));

	/* Trigger the unmap op */
	test_ioctl_ioas_unmap(MOCK_APERTURE_START, BUFFER_SIZE);

	/* kernel removed the item for us */
	test_err_destroy_access_pages(
		ENOENT, access_pages_cmd.id,
		access_pages_cmd.access_pages.out_access_pages_id);
}

static void check_access_rw(struct __test_metadata *_metadata, int fd,
			    unsigned int access_id, uint64_t iova,
			    unsigned int def_flags)
{
	uint16_t tmp[32];
	struct iommu_test_cmd access_cmd = {
		.size = sizeof(access_cmd),
		.op = IOMMU_TEST_OP_ACCESS_RW,
		.id = access_id,
		.access_rw = { .uptr = (uintptr_t)tmp },
	};
	uint16_t *buffer16 = buffer;
	unsigned int i;
	void *tmp2;

	for (i = 0; i != BUFFER_SIZE / sizeof(*buffer16); i++)
		buffer16[i] = rand();

	for (access_cmd.access_rw.iova = iova + PAGE_SIZE - 50;
	     access_cmd.access_rw.iova < iova + PAGE_SIZE + 50;
	     access_cmd.access_rw.iova++) {
		for (access_cmd.access_rw.length = 1;
		     access_cmd.access_rw.length < sizeof(tmp);
		     access_cmd.access_rw.length++) {
			access_cmd.access_rw.flags = def_flags;
			ASSERT_EQ(0, ioctl(fd,
					   _IOMMU_TEST_CMD(
						   IOMMU_TEST_OP_ACCESS_RW),
					   &access_cmd));
			ASSERT_EQ(0,
				  memcmp(buffer + (access_cmd.access_rw.iova -
						   iova),
					 tmp, access_cmd.access_rw.length));

			for (i = 0; i != ARRAY_SIZE(tmp); i++)
				tmp[i] = rand();
			access_cmd.access_rw.flags = def_flags |
						     MOCK_ACCESS_RW_WRITE;
			ASSERT_EQ(0, ioctl(fd,
					   _IOMMU_TEST_CMD(
						   IOMMU_TEST_OP_ACCESS_RW),
					   &access_cmd));
			ASSERT_EQ(0,
				  memcmp(buffer + (access_cmd.access_rw.iova -
						   iova),
					 tmp, access_cmd.access_rw.length));
		}
	}

	/* Multi-page test */
	tmp2 = malloc(BUFFER_SIZE);
	ASSERT_NE(NULL, tmp2);
	access_cmd.access_rw.iova = iova;
	access_cmd.access_rw.length = BUFFER_SIZE;
	access_cmd.access_rw.flags = def_flags;
	access_cmd.access_rw.uptr = (uintptr_t)tmp2;
	ASSERT_EQ(0, ioctl(fd, _IOMMU_TEST_CMD(IOMMU_TEST_OP_ACCESS_RW),
			   &access_cmd));
	ASSERT_EQ(0, memcmp(buffer, tmp2, access_cmd.access_rw.length));
	free(tmp2);
}

TEST_F(iommufd_ioas, access_rw)
{
	__u32 access_id;
	__u64 iova;

	test_cmd_create_access(self->ioas_id, &access_id, 0);
	test_ioctl_ioas_map(buffer, BUFFER_SIZE, &iova);
	check_access_rw(_metadata, self->fd, access_id, iova, 0);
	check_access_rw(_metadata, self->fd, access_id, iova,
			MOCK_ACCESS_RW_SLOW_PATH);
	test_ioctl_ioas_unmap(iova, BUFFER_SIZE);
	test_cmd_destroy_access(access_id);
}

TEST_F(iommufd_ioas, access_rw_unaligned)
{
	__u32 access_id;
	__u64 iova;

	test_cmd_create_access(self->ioas_id, &access_id, 0);

	/* Unaligned pages */
	iova = self->base_iova + MOCK_PAGE_SIZE;
	test_ioctl_ioas_map_fixed(buffer, BUFFER_SIZE, iova);
	check_access_rw(_metadata, self->fd, access_id, iova, 0);
	test_ioctl_ioas_unmap(iova, BUFFER_SIZE);
	test_cmd_destroy_access(access_id);
}

TEST_F(iommufd_ioas, fork_gone)
{
	__u32 access_id;
	pid_t child;

	test_cmd_create_access(self->ioas_id, &access_id, 0);

	/* Create a mapping with a different mm */
	child = fork();
	if (!child) {
		test_ioctl_ioas_map_fixed(buffer, BUFFER_SIZE,
					  MOCK_APERTURE_START);
		exit(0);
	}
	ASSERT_NE(-1, child);
	ASSERT_EQ(child, waitpid(child, NULL, 0));

	if (self->stdev_id) {
		/*
		 * If a domain already existed then everything was pinned within
		 * the fork, so this copies from one domain to another.
		 */
		test_cmd_mock_domain(self->ioas_id, NULL, NULL, NULL);
		check_access_rw(_metadata, self->fd, access_id,
				MOCK_APERTURE_START, 0);

	} else {
		/*
		 * Otherwise we need to actually pin pages which can't happen
		 * since the fork is gone.
		 */
		test_err_mock_domain(EFAULT, self->ioas_id, NULL, NULL);
	}

	test_cmd_destroy_access(access_id);
}

TEST_F(iommufd_ioas, fork_present)
{
	__u32 access_id;
	int pipefds[2];
	uint64_t tmp;
	pid_t child;
	int efd;

	test_cmd_create_access(self->ioas_id, &access_id, 0);

	ASSERT_EQ(0, pipe2(pipefds, O_CLOEXEC));
	efd = eventfd(0, EFD_CLOEXEC);
	ASSERT_NE(-1, efd);

	/* Create a mapping with a different mm */
	child = fork();
	if (!child) {
		__u64 iova;
		uint64_t one = 1;

		close(pipefds[1]);
		test_ioctl_ioas_map_fixed(buffer, BUFFER_SIZE,
					  MOCK_APERTURE_START);
		if (write(efd, &one, sizeof(one)) != sizeof(one))
			exit(100);
		if (read(pipefds[0], &iova, 1) != 1)
			exit(100);
		exit(0);
	}
	close(pipefds[0]);
	ASSERT_NE(-1, child);
	ASSERT_EQ(8, read(efd, &tmp, sizeof(tmp)));

	/* Read pages from the remote process */
	test_cmd_mock_domain(self->ioas_id, NULL, NULL, NULL);
	check_access_rw(_metadata, self->fd, access_id, MOCK_APERTURE_START, 0);

	ASSERT_EQ(0, close(pipefds[1]));
	ASSERT_EQ(child, waitpid(child, NULL, 0));

	test_cmd_destroy_access(access_id);
}

TEST_F(iommufd_ioas, ioas_option_huge_pages)
{
	struct iommu_option cmd = {
		.size = sizeof(cmd),
		.option_id = IOMMU_OPTION_HUGE_PAGES,
		.op = IOMMU_OPTION_OP_GET,
		.val64 = 3,
		.object_id = self->ioas_id,
	};

	ASSERT_EQ(0, ioctl(self->fd, IOMMU_OPTION, &cmd));
	ASSERT_EQ(1, cmd.val64);

	cmd.op = IOMMU_OPTION_OP_SET;
	cmd.val64 = 0;
	ASSERT_EQ(0, ioctl(self->fd, IOMMU_OPTION, &cmd));

	cmd.op = IOMMU_OPTION_OP_GET;
	cmd.val64 = 3;
	ASSERT_EQ(0, ioctl(self->fd, IOMMU_OPTION, &cmd));
	ASSERT_EQ(0, cmd.val64);

	cmd.op = IOMMU_OPTION_OP_SET;
	cmd.val64 = 2;
	EXPECT_ERRNO(EINVAL, ioctl(self->fd, IOMMU_OPTION, &cmd));

	cmd.op = IOMMU_OPTION_OP_SET;
	cmd.val64 = 1;
	ASSERT_EQ(0, ioctl(self->fd, IOMMU_OPTION, &cmd));
}

TEST_F(iommufd_ioas, ioas_iova_alloc)
{
	unsigned int length;
	__u64 iova;

	for (length = 1; length != PAGE_SIZE * 2; length++) {
		if (variant->mock_domains && (length % MOCK_PAGE_SIZE)) {
			test_err_ioctl_ioas_map(EINVAL, buffer, length, &iova);
		} else {
			test_ioctl_ioas_map(buffer, length, &iova);
			test_ioctl_ioas_unmap(iova, length);
		}
	}
}

TEST_F(iommufd_ioas, ioas_align_change)
{
	struct iommu_option cmd = {
		.size = sizeof(cmd),
		.option_id = IOMMU_OPTION_HUGE_PAGES,
		.op = IOMMU_OPTION_OP_SET,
		.object_id = self->ioas_id,
		/* 0 means everything must be aligned to PAGE_SIZE */
		.val64 = 0,
	};

	/*
	 * We cannot upgrade the alignment using OPTION_HUGE_PAGES when a domain
	 * and map are present.
	 */
	if (variant->mock_domains)
		return;

	/*
	 * We can upgrade to PAGE_SIZE alignment when things are aligned right
	 */
	test_ioctl_ioas_map_fixed(buffer, PAGE_SIZE, MOCK_APERTURE_START);
	ASSERT_EQ(0, ioctl(self->fd, IOMMU_OPTION, &cmd));

	/* Misalignment is rejected at map time */
	test_err_ioctl_ioas_map_fixed(EINVAL, buffer + MOCK_PAGE_SIZE,
				      PAGE_SIZE,
				      MOCK_APERTURE_START + PAGE_SIZE);
	ASSERT_EQ(0, ioctl(self->fd, IOMMU_OPTION, &cmd));

	/* Reduce alignment */
	cmd.val64 = 1;
	ASSERT_EQ(0, ioctl(self->fd, IOMMU_OPTION, &cmd));

	/* Confirm misalignment is rejected during alignment upgrade */
	test_ioctl_ioas_map_fixed(buffer + MOCK_PAGE_SIZE, PAGE_SIZE,
				  MOCK_APERTURE_START + PAGE_SIZE);
	cmd.val64 = 0;
	EXPECT_ERRNO(EADDRINUSE, ioctl(self->fd, IOMMU_OPTION, &cmd));

	test_ioctl_ioas_unmap(MOCK_APERTURE_START + PAGE_SIZE, PAGE_SIZE);
	test_ioctl_ioas_unmap(MOCK_APERTURE_START, PAGE_SIZE);
}

TEST_F(iommufd_ioas, copy_sweep)
{
	struct iommu_ioas_copy copy_cmd = {
		.size = sizeof(copy_cmd),
		.flags = IOMMU_IOAS_MAP_FIXED_IOVA,
		.src_ioas_id = self->ioas_id,
		.dst_iova = MOCK_APERTURE_START,
		.length = MOCK_PAGE_SIZE,
	};
	unsigned int dst_ioas_id;
	uint64_t last_iova;
	uint64_t iova;

	test_ioctl_ioas_alloc(&dst_ioas_id);
	copy_cmd.dst_ioas_id = dst_ioas_id;

	if (variant->mock_domains)
		last_iova = MOCK_APERTURE_START + BUFFER_SIZE - 1;
	else
		last_iova = MOCK_APERTURE_START + BUFFER_SIZE - 2;

	test_ioctl_ioas_map_fixed(buffer, last_iova - MOCK_APERTURE_START + 1,
				  MOCK_APERTURE_START);

	for (iova = MOCK_APERTURE_START - PAGE_SIZE; iova <= last_iova;
	     iova += 511) {
		copy_cmd.src_iova = iova;
		if (iova < MOCK_APERTURE_START ||
		    iova + copy_cmd.length - 1 > last_iova) {
			EXPECT_ERRNO(ENOENT, ioctl(self->fd, IOMMU_IOAS_COPY,
						   &copy_cmd));
		} else {
			ASSERT_EQ(0,
				  ioctl(self->fd, IOMMU_IOAS_COPY, &copy_cmd));
			test_ioctl_ioas_unmap_id(dst_ioas_id, copy_cmd.dst_iova,
						 copy_cmd.length);
		}
	}

	test_ioctl_destroy(dst_ioas_id);
}

FIXTURE(iommufd_mock_domain)
{
	int fd;
	uint32_t ioas_id;
	uint32_t hwpt_id;
	uint32_t hwpt_ids[2];
	uint32_t stdev_ids[2];
	uint32_t idev_ids[2];
	int mmap_flags;
	size_t mmap_buf_size;
};

FIXTURE_VARIANT(iommufd_mock_domain)
{
	unsigned int mock_domains;
	bool hugepages;
};

FIXTURE_SETUP(iommufd_mock_domain)
{
	unsigned int i;

	self->fd = open("/dev/iommu", O_RDWR);
	ASSERT_NE(-1, self->fd);
	test_ioctl_ioas_alloc(&self->ioas_id);

	ASSERT_GE(ARRAY_SIZE(self->hwpt_ids), variant->mock_domains);

	for (i = 0; i != variant->mock_domains; i++)
		test_cmd_mock_domain(self->ioas_id, &self->stdev_ids[i],
				     &self->hwpt_ids[i], &self->idev_ids[i]);
	self->hwpt_id = self->hwpt_ids[0];

	self->mmap_flags = MAP_SHARED | MAP_ANONYMOUS;
	self->mmap_buf_size = PAGE_SIZE * 8;
	if (variant->hugepages) {
		/*
		 * MAP_POPULATE will cause the kernel to fail mmap if THPs are
		 * not available.
		 */
		self->mmap_flags |= MAP_HUGETLB | MAP_POPULATE;
		self->mmap_buf_size = HUGEPAGE_SIZE * 2;
	}
}

FIXTURE_TEARDOWN(iommufd_mock_domain)
{
	teardown_iommufd(self->fd, _metadata);
}

FIXTURE_VARIANT_ADD(iommufd_mock_domain, one_domain)
{
	.mock_domains = 1,
	.hugepages = false,
};

FIXTURE_VARIANT_ADD(iommufd_mock_domain, two_domains)
{
	.mock_domains = 2,
	.hugepages = false,
};

FIXTURE_VARIANT_ADD(iommufd_mock_domain, one_domain_hugepage)
{
	.mock_domains = 1,
	.hugepages = true,
};

FIXTURE_VARIANT_ADD(iommufd_mock_domain, two_domains_hugepage)
{
	.mock_domains = 2,
	.hugepages = true,
};

/* Have the kernel check that the user pages made it to the iommu_domain */
#define check_mock_iova(_ptr, _iova, _length)                                \
	({                                                                   \
		struct iommu_test_cmd check_map_cmd = {                      \
			.size = sizeof(check_map_cmd),                       \
			.op = IOMMU_TEST_OP_MD_CHECK_MAP,                    \
			.id = self->hwpt_id,                                 \
			.check_map = { .iova = _iova,                        \
				       .length = _length,                    \
				       .uptr = (uintptr_t)(_ptr) },          \
		};                                                           \
		ASSERT_EQ(0,                                                 \
			  ioctl(self->fd,                                    \
				_IOMMU_TEST_CMD(IOMMU_TEST_OP_MD_CHECK_MAP), \
				&check_map_cmd));                            \
		if (self->hwpt_ids[1]) {                                     \
			check_map_cmd.id = self->hwpt_ids[1];                \
			ASSERT_EQ(0,                                         \
				  ioctl(self->fd,                            \
					_IOMMU_TEST_CMD(                     \
						IOMMU_TEST_OP_MD_CHECK_MAP), \
					&check_map_cmd));                    \
		}                                                            \
	})

TEST_F(iommufd_mock_domain, basic)
{
	size_t buf_size = self->mmap_buf_size;
	uint8_t *buf;
	__u64 iova;

	/* Simple one page map */
	test_ioctl_ioas_map(buffer, PAGE_SIZE, &iova);
	check_mock_iova(buffer, iova, PAGE_SIZE);

	buf = mmap(0, buf_size, PROT_READ | PROT_WRITE, self->mmap_flags, -1,
		   0);
	ASSERT_NE(MAP_FAILED, buf);

	/* EFAULT half way through mapping */
	ASSERT_EQ(0, munmap(buf + buf_size / 2, buf_size / 2));
	test_err_ioctl_ioas_map(EFAULT, buf, buf_size, &iova);

	/* EFAULT on first page */
	ASSERT_EQ(0, munmap(buf, buf_size / 2));
	test_err_ioctl_ioas_map(EFAULT, buf, buf_size, &iova);
}

TEST_F(iommufd_mock_domain, ro_unshare)
{
	uint8_t *buf;
	__u64 iova;
	int fd;

	fd = open("/proc/self/exe", O_RDONLY);
	ASSERT_NE(-1, fd);

	buf = mmap(0, PAGE_SIZE, PROT_READ | PROT_WRITE, MAP_PRIVATE, fd, 0);
	ASSERT_NE(MAP_FAILED, buf);
	close(fd);

	/*
	 * There have been lots of changes to the "unshare" mechanism in
	 * get_user_pages(), make sure it works right. The write to the page
	 * after we map it for reading should not change the assigned PFN.
	 */
	ASSERT_EQ(0,
		  _test_ioctl_ioas_map(self->fd, self->ioas_id, buf, PAGE_SIZE,
				       &iova, IOMMU_IOAS_MAP_READABLE));
	check_mock_iova(buf, iova, PAGE_SIZE);
	memset(buf, 1, PAGE_SIZE);
	check_mock_iova(buf, iova, PAGE_SIZE);
	ASSERT_EQ(0, munmap(buf, PAGE_SIZE));
}

TEST_F(iommufd_mock_domain, all_aligns)
{
	size_t test_step = variant->hugepages ? (self->mmap_buf_size / 16) :
						MOCK_PAGE_SIZE;
	size_t buf_size = self->mmap_buf_size;
	unsigned int start;
	unsigned int end;
	uint8_t *buf;

	buf = mmap(0, buf_size, PROT_READ | PROT_WRITE, self->mmap_flags, -1,
		   0);
	ASSERT_NE(MAP_FAILED, buf);
	check_refs(buf, buf_size, 0);

	/*
	 * Map every combination of page size and alignment within a big region,
	 * less for hugepage case as it takes so long to finish.
	 */
	for (start = 0; start < buf_size; start += test_step) {
		if (variant->hugepages)
			end = buf_size;
		else
			end = start + MOCK_PAGE_SIZE;
		for (; end < buf_size; end += MOCK_PAGE_SIZE) {
			size_t length = end - start;
			__u64 iova;

			test_ioctl_ioas_map(buf + start, length, &iova);
			check_mock_iova(buf + start, iova, length);
			check_refs(buf + start / PAGE_SIZE * PAGE_SIZE,
				   end / PAGE_SIZE * PAGE_SIZE -
					   start / PAGE_SIZE * PAGE_SIZE,
				   1);

			test_ioctl_ioas_unmap(iova, length);
		}
	}
	check_refs(buf, buf_size, 0);
	ASSERT_EQ(0, munmap(buf, buf_size));
}

TEST_F(iommufd_mock_domain, all_aligns_copy)
{
	size_t test_step = variant->hugepages ? self->mmap_buf_size / 16 :
						MOCK_PAGE_SIZE;
	size_t buf_size = self->mmap_buf_size;
	unsigned int start;
	unsigned int end;
	uint8_t *buf;

	buf = mmap(0, buf_size, PROT_READ | PROT_WRITE, self->mmap_flags, -1,
		   0);
	ASSERT_NE(MAP_FAILED, buf);
	check_refs(buf, buf_size, 0);

	/*
	 * Map every combination of page size and alignment within a big region,
	 * less for hugepage case as it takes so long to finish.
	 */
	for (start = 0; start < buf_size; start += test_step) {
		if (variant->hugepages)
			end = buf_size;
		else
			end = start + MOCK_PAGE_SIZE;
		for (; end < buf_size; end += MOCK_PAGE_SIZE) {
			size_t length = end - start;
			unsigned int old_id;
			uint32_t mock_stdev_id;
			__u64 iova;

			test_ioctl_ioas_map(buf + start, length, &iova);

			/* Add and destroy a domain while the area exists */
			old_id = self->hwpt_ids[1];
			test_cmd_mock_domain(self->ioas_id, &mock_stdev_id,
					     &self->hwpt_ids[1], NULL);

			check_mock_iova(buf + start, iova, length);
			check_refs(buf + start / PAGE_SIZE * PAGE_SIZE,
				   end / PAGE_SIZE * PAGE_SIZE -
					   start / PAGE_SIZE * PAGE_SIZE,
				   1);

			test_ioctl_destroy(mock_stdev_id);
			self->hwpt_ids[1] = old_id;

			test_ioctl_ioas_unmap(iova, length);
		}
	}
	check_refs(buf, buf_size, 0);
	ASSERT_EQ(0, munmap(buf, buf_size));
}

TEST_F(iommufd_mock_domain, user_copy)
{
	struct iommu_test_cmd access_cmd = {
		.size = sizeof(access_cmd),
		.op = IOMMU_TEST_OP_ACCESS_PAGES,
		.access_pages = { .length = BUFFER_SIZE,
				  .uptr = (uintptr_t)buffer },
	};
	struct iommu_ioas_copy copy_cmd = {
		.size = sizeof(copy_cmd),
		.flags = IOMMU_IOAS_MAP_FIXED_IOVA,
		.dst_ioas_id = self->ioas_id,
		.dst_iova = MOCK_APERTURE_START,
		.length = BUFFER_SIZE,
	};
	struct iommu_ioas_unmap unmap_cmd = {
		.size = sizeof(unmap_cmd),
		.ioas_id = self->ioas_id,
		.iova = MOCK_APERTURE_START,
		.length = BUFFER_SIZE,
	};
	unsigned int new_ioas_id, ioas_id;

	/* Pin the pages in an IOAS with no domains then copy to an IOAS with domains */
	test_ioctl_ioas_alloc(&ioas_id);
	test_ioctl_ioas_map_id(ioas_id, buffer, BUFFER_SIZE,
			       &copy_cmd.src_iova);

	test_cmd_create_access(ioas_id, &access_cmd.id,
			       MOCK_FLAGS_ACCESS_CREATE_NEEDS_PIN_PAGES);

	access_cmd.access_pages.iova = copy_cmd.src_iova;
	ASSERT_EQ(0,
		  ioctl(self->fd, _IOMMU_TEST_CMD(IOMMU_TEST_OP_ACCESS_PAGES),
			&access_cmd));
	copy_cmd.src_ioas_id = ioas_id;
	ASSERT_EQ(0, ioctl(self->fd, IOMMU_IOAS_COPY, &copy_cmd));
	check_mock_iova(buffer, MOCK_APERTURE_START, BUFFER_SIZE);

	/* Now replace the ioas with a new one */
	test_ioctl_ioas_alloc(&new_ioas_id);
	test_ioctl_ioas_map_id(new_ioas_id, buffer, BUFFER_SIZE,
			       &copy_cmd.src_iova);
	test_cmd_access_replace_ioas(access_cmd.id, new_ioas_id);

	/* Destroy the old ioas and cleanup copied mapping */
	ASSERT_EQ(0, ioctl(self->fd, IOMMU_IOAS_UNMAP, &unmap_cmd));
	test_ioctl_destroy(ioas_id);

	/* Then run the same test again with the new ioas */
	access_cmd.access_pages.iova = copy_cmd.src_iova;
	ASSERT_EQ(0,
		  ioctl(self->fd, _IOMMU_TEST_CMD(IOMMU_TEST_OP_ACCESS_PAGES),
			&access_cmd));
	copy_cmd.src_ioas_id = new_ioas_id;
	ASSERT_EQ(0, ioctl(self->fd, IOMMU_IOAS_COPY, &copy_cmd));
	check_mock_iova(buffer, MOCK_APERTURE_START, BUFFER_SIZE);

	test_cmd_destroy_access_pages(
		access_cmd.id, access_cmd.access_pages.out_access_pages_id);
	test_cmd_destroy_access(access_cmd.id);

	test_ioctl_destroy(new_ioas_id);
}

TEST_F(iommufd_mock_domain, replace)
{
	uint32_t ioas_id;

	test_ioctl_ioas_alloc(&ioas_id);

	test_cmd_mock_domain_replace(self->stdev_ids[0], ioas_id);

	/*
	 * Replacing the IOAS causes the prior HWPT to be deallocated, thus we
	 * should get enoent when we try to use it.
	 */
	if (variant->mock_domains == 1)
		test_err_mock_domain_replace(ENOENT, self->stdev_ids[0],
					     self->hwpt_ids[0]);

	test_cmd_mock_domain_replace(self->stdev_ids[0], ioas_id);
	if (variant->mock_domains >= 2) {
		test_cmd_mock_domain_replace(self->stdev_ids[0],
					     self->hwpt_ids[1]);
		test_cmd_mock_domain_replace(self->stdev_ids[0],
					     self->hwpt_ids[1]);
		test_cmd_mock_domain_replace(self->stdev_ids[0],
					     self->hwpt_ids[0]);
	}

	test_cmd_mock_domain_replace(self->stdev_ids[0], self->ioas_id);
	test_ioctl_destroy(ioas_id);
}

TEST_F(iommufd_mock_domain, alloc_hwpt)
{
	int i;

	for (i = 0; i != variant->mock_domains; i++) {
		uint32_t hwpt_id[2];
		uint32_t stddev_id;

		test_err_hwpt_alloc(EOPNOTSUPP,
				    self->idev_ids[i], self->ioas_id,
				    ~IOMMU_HWPT_ALLOC_NEST_PARENT, &hwpt_id[0]);
		test_cmd_hwpt_alloc(self->idev_ids[i], self->ioas_id,
				    0, &hwpt_id[0]);
		test_cmd_hwpt_alloc(self->idev_ids[i], self->ioas_id,
				    IOMMU_HWPT_ALLOC_NEST_PARENT, &hwpt_id[1]);

		/* Do a hw_pagetable rotation test */
		test_cmd_mock_domain_replace(self->stdev_ids[i], hwpt_id[0]);
		EXPECT_ERRNO(EBUSY, _test_ioctl_destroy(self->fd, hwpt_id[0]));
		test_cmd_mock_domain_replace(self->stdev_ids[i], hwpt_id[1]);
		EXPECT_ERRNO(EBUSY, _test_ioctl_destroy(self->fd, hwpt_id[1]));
		test_cmd_mock_domain_replace(self->stdev_ids[i], self->ioas_id);
		test_ioctl_destroy(hwpt_id[1]);

		test_cmd_mock_domain(hwpt_id[0], &stddev_id, NULL, NULL);
		test_ioctl_destroy(stddev_id);
		test_ioctl_destroy(hwpt_id[0]);
	}
}

FIXTURE(iommufd_dirty_tracking)
{
	int fd;
	uint32_t ioas_id;
	uint32_t hwpt_id;
	uint32_t stdev_id;
	uint32_t idev_id;
	unsigned long page_size;
	unsigned long bitmap_size;
	void *bitmap;
	void *buffer;
};

FIXTURE_VARIANT(iommufd_dirty_tracking)
{
	unsigned long buffer_size;
	bool hugepages;
};

FIXTURE_SETUP(iommufd_dirty_tracking)
{
	unsigned long size;
	int mmap_flags;
	void *vrc;
	int rc;

	if (variant->buffer_size < MOCK_PAGE_SIZE) {
		SKIP(return,
		     "Skipping buffer_size=%lu, less than MOCK_PAGE_SIZE=%lu",
		     variant->buffer_size, MOCK_PAGE_SIZE);
	}

	self->fd = open("/dev/iommu", O_RDWR);
	ASSERT_NE(-1, self->fd);

	rc = posix_memalign(&self->buffer, HUGEPAGE_SIZE, variant->buffer_size);
	if (rc || !self->buffer) {
		SKIP(return, "Skipping buffer_size=%lu due to errno=%d",
			   variant->buffer_size, rc);
	}

	mmap_flags = MAP_SHARED | MAP_ANONYMOUS | MAP_FIXED;
	if (variant->hugepages) {
		/*
		 * MAP_POPULATE will cause the kernel to fail mmap if THPs are
		 * not available.
		 */
		mmap_flags |= MAP_HUGETLB | MAP_POPULATE;
	}
	assert((uintptr_t)self->buffer % HUGEPAGE_SIZE == 0);
	vrc = mmap(self->buffer, variant->buffer_size, PROT_READ | PROT_WRITE,
		   mmap_flags, -1, 0);
	assert(vrc == self->buffer);

	self->page_size = MOCK_PAGE_SIZE;
	self->bitmap_size = variant->buffer_size / self->page_size;

	/* Provision with an extra (PAGE_SIZE) for the unaligned case */
	size = DIV_ROUND_UP(self->bitmap_size, BITS_PER_BYTE);
	rc = posix_memalign(&self->bitmap, PAGE_SIZE, size + PAGE_SIZE);
	assert(!rc);
	assert(self->bitmap);
	assert((uintptr_t)self->bitmap % PAGE_SIZE == 0);

	test_ioctl_ioas_alloc(&self->ioas_id);
	/* Enable 1M mock IOMMU hugepages */
	if (variant->hugepages) {
		test_cmd_mock_domain_flags(self->ioas_id,
					   MOCK_FLAGS_DEVICE_HUGE_IOVA,
					   &self->stdev_id, &self->hwpt_id,
					   &self->idev_id);
	} else {
		test_cmd_mock_domain(self->ioas_id, &self->stdev_id,
				     &self->hwpt_id, &self->idev_id);
	}
}

FIXTURE_TEARDOWN(iommufd_dirty_tracking)
{
	munmap(self->buffer, variant->buffer_size);
	munmap(self->bitmap, DIV_ROUND_UP(self->bitmap_size, BITS_PER_BYTE));
	teardown_iommufd(self->fd, _metadata);
}

FIXTURE_VARIANT_ADD(iommufd_dirty_tracking, domain_dirty8k)
{
	/* half of an u8 index bitmap */
	.buffer_size = 8UL * 1024UL,
};

FIXTURE_VARIANT_ADD(iommufd_dirty_tracking, domain_dirty16k)
{
	/* one u8 index bitmap */
	.buffer_size = 16UL * 1024UL,
};

FIXTURE_VARIANT_ADD(iommufd_dirty_tracking, domain_dirty128k)
{
	/* one u32 index bitmap */
	.buffer_size = 128UL * 1024UL,
};

FIXTURE_VARIANT_ADD(iommufd_dirty_tracking, domain_dirty256k)
{
	/* one u64 index bitmap */
	.buffer_size = 256UL * 1024UL,
};

FIXTURE_VARIANT_ADD(iommufd_dirty_tracking, domain_dirty640k)
{
	/* two u64 index and trailing end bitmap */
	.buffer_size = 640UL * 1024UL,
};

FIXTURE_VARIANT_ADD(iommufd_dirty_tracking, domain_dirty128M)
{
	/* 4K bitmap (128M IOVA range) */
	.buffer_size = 128UL * 1024UL * 1024UL,
};

FIXTURE_VARIANT_ADD(iommufd_dirty_tracking, domain_dirty128M_huge)
{
	/* 4K bitmap (128M IOVA range) */
	.buffer_size = 128UL * 1024UL * 1024UL,
	.hugepages = true,
};

FIXTURE_VARIANT_ADD(iommufd_dirty_tracking, domain_dirty256M)
{
	/* 8K bitmap (256M IOVA range) */
	.buffer_size = 256UL * 1024UL * 1024UL,
};

FIXTURE_VARIANT_ADD(iommufd_dirty_tracking, domain_dirty256M_huge)
{
	/* 8K bitmap (256M IOVA range) */
	.buffer_size = 256UL * 1024UL * 1024UL,
	.hugepages = true,
};

TEST_F(iommufd_dirty_tracking, enforce_dirty)
{
	uint32_t ioas_id, stddev_id, idev_id;
	uint32_t hwpt_id, _hwpt_id;
	uint32_t dev_flags;

	/* Regular case */
	dev_flags = MOCK_FLAGS_DEVICE_NO_DIRTY;
	test_cmd_hwpt_alloc(self->idev_id, self->ioas_id,
			    IOMMU_HWPT_ALLOC_DIRTY_TRACKING, &hwpt_id);
	test_cmd_mock_domain(hwpt_id, &stddev_id, NULL, NULL);
	test_err_mock_domain_flags(EINVAL, hwpt_id, dev_flags, &stddev_id,
				   NULL);
	test_ioctl_destroy(stddev_id);
	test_ioctl_destroy(hwpt_id);

	/* IOMMU device does not support dirty tracking */
	test_ioctl_ioas_alloc(&ioas_id);
	test_cmd_mock_domain_flags(ioas_id, dev_flags, &stddev_id, &_hwpt_id,
				   &idev_id);
	test_err_hwpt_alloc(EOPNOTSUPP, idev_id, ioas_id,
			    IOMMU_HWPT_ALLOC_DIRTY_TRACKING, &hwpt_id);
	test_ioctl_destroy(stddev_id);
}

TEST_F(iommufd_dirty_tracking, set_dirty_tracking)
{
	uint32_t stddev_id;
	uint32_t hwpt_id;

	test_cmd_hwpt_alloc(self->idev_id, self->ioas_id,
			    IOMMU_HWPT_ALLOC_DIRTY_TRACKING, &hwpt_id);
	test_cmd_mock_domain(hwpt_id, &stddev_id, NULL, NULL);
	test_cmd_set_dirty_tracking(hwpt_id, true);
	test_cmd_set_dirty_tracking(hwpt_id, false);

	test_ioctl_destroy(stddev_id);
	test_ioctl_destroy(hwpt_id);
}

TEST_F(iommufd_dirty_tracking, device_dirty_capability)
{
	uint32_t caps = 0;
	uint32_t stddev_id;
	uint32_t hwpt_id;

	test_cmd_hwpt_alloc(self->idev_id, self->ioas_id, 0, &hwpt_id);
	test_cmd_mock_domain(hwpt_id, &stddev_id, NULL, NULL);
	test_cmd_get_hw_capabilities(self->idev_id, caps,
				     IOMMU_HW_CAP_DIRTY_TRACKING);
	ASSERT_EQ(IOMMU_HW_CAP_DIRTY_TRACKING,
		  caps & IOMMU_HW_CAP_DIRTY_TRACKING);

	test_ioctl_destroy(stddev_id);
	test_ioctl_destroy(hwpt_id);
}

TEST_F(iommufd_dirty_tracking, get_dirty_bitmap)
{
	uint32_t page_size = MOCK_PAGE_SIZE;
	uint32_t hwpt_id;
	uint32_t ioas_id;

	if (variant->hugepages)
		page_size = MOCK_HUGE_PAGE_SIZE;

	test_ioctl_ioas_alloc(&ioas_id);
	test_ioctl_ioas_map_fixed_id(ioas_id, self->buffer,
				     variant->buffer_size, MOCK_APERTURE_START);

	test_cmd_hwpt_alloc(self->idev_id, ioas_id,
			    IOMMU_HWPT_ALLOC_DIRTY_TRACKING, &hwpt_id);

	test_cmd_set_dirty_tracking(hwpt_id, true);

	test_mock_dirty_bitmaps(hwpt_id, variant->buffer_size,
				MOCK_APERTURE_START, self->page_size, page_size,
				self->bitmap, self->bitmap_size, 0, _metadata);

	/* PAGE_SIZE unaligned bitmap */
	test_mock_dirty_bitmaps(hwpt_id, variant->buffer_size,
				MOCK_APERTURE_START, self->page_size, page_size,
				self->bitmap + MOCK_PAGE_SIZE,
				self->bitmap_size, 0, _metadata);

	/* u64 unaligned bitmap */
	test_mock_dirty_bitmaps(hwpt_id, variant->buffer_size,
				MOCK_APERTURE_START, self->page_size, page_size,
				self->bitmap + 0xff1, self->bitmap_size, 0,
				_metadata);

	test_ioctl_destroy(hwpt_id);
}

TEST_F(iommufd_dirty_tracking, get_dirty_bitmap_no_clear)
{
	uint32_t page_size = MOCK_PAGE_SIZE;
	uint32_t hwpt_id;
	uint32_t ioas_id;

	if (variant->hugepages)
		page_size = MOCK_HUGE_PAGE_SIZE;

	test_ioctl_ioas_alloc(&ioas_id);
	test_ioctl_ioas_map_fixed_id(ioas_id, self->buffer,
				     variant->buffer_size, MOCK_APERTURE_START);

	test_cmd_hwpt_alloc(self->idev_id, ioas_id,
			    IOMMU_HWPT_ALLOC_DIRTY_TRACKING, &hwpt_id);

	test_cmd_set_dirty_tracking(hwpt_id, true);

	test_mock_dirty_bitmaps(hwpt_id, variant->buffer_size,
				MOCK_APERTURE_START, self->page_size, page_size,
				self->bitmap, self->bitmap_size,
				IOMMU_HWPT_GET_DIRTY_BITMAP_NO_CLEAR,
				_metadata);

	/* Unaligned bitmap */
	test_mock_dirty_bitmaps(hwpt_id, variant->buffer_size,
				MOCK_APERTURE_START, self->page_size, page_size,
				self->bitmap + MOCK_PAGE_SIZE,
				self->bitmap_size,
				IOMMU_HWPT_GET_DIRTY_BITMAP_NO_CLEAR,
				_metadata);

	/* u64 unaligned bitmap */
	test_mock_dirty_bitmaps(hwpt_id, variant->buffer_size,
				MOCK_APERTURE_START, self->page_size, page_size,
				self->bitmap + 0xff1, self->bitmap_size,
				IOMMU_HWPT_GET_DIRTY_BITMAP_NO_CLEAR,
				_metadata);

	test_ioctl_destroy(hwpt_id);
}

/* VFIO compatibility IOCTLs */

TEST_F(iommufd, simple_ioctls)
{
	ASSERT_EQ(VFIO_API_VERSION, ioctl(self->fd, VFIO_GET_API_VERSION));
	ASSERT_EQ(1, ioctl(self->fd, VFIO_CHECK_EXTENSION, VFIO_TYPE1v2_IOMMU));
}

TEST_F(iommufd, unmap_cmd)
{
	struct vfio_iommu_type1_dma_unmap unmap_cmd = {
		.iova = MOCK_APERTURE_START,
		.size = PAGE_SIZE,
	};

	unmap_cmd.argsz = 1;
	EXPECT_ERRNO(EINVAL, ioctl(self->fd, VFIO_IOMMU_UNMAP_DMA, &unmap_cmd));

	unmap_cmd.argsz = sizeof(unmap_cmd);
	unmap_cmd.flags = 1 << 31;
	EXPECT_ERRNO(EINVAL, ioctl(self->fd, VFIO_IOMMU_UNMAP_DMA, &unmap_cmd));

	unmap_cmd.flags = 0;
	EXPECT_ERRNO(ENODEV, ioctl(self->fd, VFIO_IOMMU_UNMAP_DMA, &unmap_cmd));
}

TEST_F(iommufd, map_cmd)
{
	struct vfio_iommu_type1_dma_map map_cmd = {
		.iova = MOCK_APERTURE_START,
		.size = PAGE_SIZE,
		.vaddr = (__u64)buffer,
	};

	map_cmd.argsz = 1;
	EXPECT_ERRNO(EINVAL, ioctl(self->fd, VFIO_IOMMU_MAP_DMA, &map_cmd));

	map_cmd.argsz = sizeof(map_cmd);
	map_cmd.flags = 1 << 31;
	EXPECT_ERRNO(EINVAL, ioctl(self->fd, VFIO_IOMMU_MAP_DMA, &map_cmd));

	/* Requires a domain to be attached */
	map_cmd.flags = VFIO_DMA_MAP_FLAG_READ | VFIO_DMA_MAP_FLAG_WRITE;
	EXPECT_ERRNO(ENODEV, ioctl(self->fd, VFIO_IOMMU_MAP_DMA, &map_cmd));
}

TEST_F(iommufd, info_cmd)
{
	struct vfio_iommu_type1_info info_cmd = {};

	/* Invalid argsz */
	info_cmd.argsz = 1;
	EXPECT_ERRNO(EINVAL, ioctl(self->fd, VFIO_IOMMU_GET_INFO, &info_cmd));

	info_cmd.argsz = sizeof(info_cmd);
	EXPECT_ERRNO(ENODEV, ioctl(self->fd, VFIO_IOMMU_GET_INFO, &info_cmd));
}

TEST_F(iommufd, set_iommu_cmd)
{
	/* Requires a domain to be attached */
	EXPECT_ERRNO(ENODEV,
		     ioctl(self->fd, VFIO_SET_IOMMU, VFIO_TYPE1v2_IOMMU));
	EXPECT_ERRNO(ENODEV, ioctl(self->fd, VFIO_SET_IOMMU, VFIO_TYPE1_IOMMU));
}

TEST_F(iommufd, vfio_ioas)
{
	struct iommu_vfio_ioas vfio_ioas_cmd = {
		.size = sizeof(vfio_ioas_cmd),
		.op = IOMMU_VFIO_IOAS_GET,
	};
	__u32 ioas_id;

	/* ENODEV if there is no compat ioas */
	EXPECT_ERRNO(ENODEV, ioctl(self->fd, IOMMU_VFIO_IOAS, &vfio_ioas_cmd));

	/* Invalid id for set */
	vfio_ioas_cmd.op = IOMMU_VFIO_IOAS_SET;
	EXPECT_ERRNO(ENOENT, ioctl(self->fd, IOMMU_VFIO_IOAS, &vfio_ioas_cmd));

	/* Valid id for set*/
	test_ioctl_ioas_alloc(&ioas_id);
	vfio_ioas_cmd.ioas_id = ioas_id;
	ASSERT_EQ(0, ioctl(self->fd, IOMMU_VFIO_IOAS, &vfio_ioas_cmd));

	/* Same id comes back from get */
	vfio_ioas_cmd.op = IOMMU_VFIO_IOAS_GET;
	ASSERT_EQ(0, ioctl(self->fd, IOMMU_VFIO_IOAS, &vfio_ioas_cmd));
	ASSERT_EQ(ioas_id, vfio_ioas_cmd.ioas_id);

	/* Clear works */
	vfio_ioas_cmd.op = IOMMU_VFIO_IOAS_CLEAR;
	ASSERT_EQ(0, ioctl(self->fd, IOMMU_VFIO_IOAS, &vfio_ioas_cmd));
	vfio_ioas_cmd.op = IOMMU_VFIO_IOAS_GET;
	EXPECT_ERRNO(ENODEV, ioctl(self->fd, IOMMU_VFIO_IOAS, &vfio_ioas_cmd));
}

FIXTURE(vfio_compat_mock_domain)
{
	int fd;
	uint32_t ioas_id;
};

FIXTURE_VARIANT(vfio_compat_mock_domain)
{
	unsigned int version;
};

FIXTURE_SETUP(vfio_compat_mock_domain)
{
	struct iommu_vfio_ioas vfio_ioas_cmd = {
		.size = sizeof(vfio_ioas_cmd),
		.op = IOMMU_VFIO_IOAS_SET,
	};

	self->fd = open("/dev/iommu", O_RDWR);
	ASSERT_NE(-1, self->fd);

	/* Create what VFIO would consider a group */
	test_ioctl_ioas_alloc(&self->ioas_id);
	test_cmd_mock_domain(self->ioas_id, NULL, NULL, NULL);

	/* Attach it to the vfio compat */
	vfio_ioas_cmd.ioas_id = self->ioas_id;
	ASSERT_EQ(0, ioctl(self->fd, IOMMU_VFIO_IOAS, &vfio_ioas_cmd));
	ASSERT_EQ(0, ioctl(self->fd, VFIO_SET_IOMMU, variant->version));
}

FIXTURE_TEARDOWN(vfio_compat_mock_domain)
{
	teardown_iommufd(self->fd, _metadata);
}

FIXTURE_VARIANT_ADD(vfio_compat_mock_domain, Ver1v2)
{
	.version = VFIO_TYPE1v2_IOMMU,
};

FIXTURE_VARIANT_ADD(vfio_compat_mock_domain, Ver1v0)
{
	.version = VFIO_TYPE1_IOMMU,
};

TEST_F(vfio_compat_mock_domain, simple_close)
{
}

TEST_F(vfio_compat_mock_domain, option_huge_pages)
{
	struct iommu_option cmd = {
		.size = sizeof(cmd),
		.option_id = IOMMU_OPTION_HUGE_PAGES,
		.op = IOMMU_OPTION_OP_GET,
		.val64 = 3,
		.object_id = self->ioas_id,
	};

	ASSERT_EQ(0, ioctl(self->fd, IOMMU_OPTION, &cmd));
	if (variant->version == VFIO_TYPE1_IOMMU) {
		ASSERT_EQ(0, cmd.val64);
	} else {
		ASSERT_EQ(1, cmd.val64);
	}
}

/*
 * Execute an ioctl command stored in buffer and check that the result does not
 * overflow memory.
 */
static bool is_filled(const void *buf, uint8_t c, size_t len)
{
	const uint8_t *cbuf = buf;

	for (; len; cbuf++, len--)
		if (*cbuf != c)
			return false;
	return true;
}

#define ioctl_check_buf(fd, cmd)                                         \
	({                                                               \
		size_t _cmd_len = *(__u32 *)buffer;                      \
									 \
		memset(buffer + _cmd_len, 0xAA, BUFFER_SIZE - _cmd_len); \
		ASSERT_EQ(0, ioctl(fd, cmd, buffer));                    \
		ASSERT_EQ(true, is_filled(buffer + _cmd_len, 0xAA,       \
					  BUFFER_SIZE - _cmd_len));      \
	})

static void check_vfio_info_cap_chain(struct __test_metadata *_metadata,
				      struct vfio_iommu_type1_info *info_cmd)
{
	const struct vfio_info_cap_header *cap;

	ASSERT_GE(info_cmd->argsz, info_cmd->cap_offset + sizeof(*cap));
	cap = buffer + info_cmd->cap_offset;
	while (true) {
		size_t cap_size;

		if (cap->next)
			cap_size = (buffer + cap->next) - (void *)cap;
		else
			cap_size = (buffer + info_cmd->argsz) - (void *)cap;

		switch (cap->id) {
		case VFIO_IOMMU_TYPE1_INFO_CAP_IOVA_RANGE: {
			struct vfio_iommu_type1_info_cap_iova_range *data =
				(void *)cap;

			ASSERT_EQ(1, data->header.version);
			ASSERT_EQ(1, data->nr_iovas);
			EXPECT_EQ(MOCK_APERTURE_START,
				  data->iova_ranges[0].start);
			EXPECT_EQ(MOCK_APERTURE_LAST, data->iova_ranges[0].end);
			break;
		}
		case VFIO_IOMMU_TYPE1_INFO_DMA_AVAIL: {
			struct vfio_iommu_type1_info_dma_avail *data =
				(void *)cap;

			ASSERT_EQ(1, data->header.version);
			ASSERT_EQ(sizeof(*data), cap_size);
			break;
		}
		default:
			ASSERT_EQ(false, true);
			break;
		}
		if (!cap->next)
			break;

		ASSERT_GE(info_cmd->argsz, cap->next + sizeof(*cap));
		ASSERT_GE(buffer + cap->next, (void *)cap);
		cap = buffer + cap->next;
	}
}

TEST_F(vfio_compat_mock_domain, get_info)
{
	struct vfio_iommu_type1_info *info_cmd = buffer;
	unsigned int i;
	size_t caplen;

	/* Pre-cap ABI */
	*info_cmd = (struct vfio_iommu_type1_info){
		.argsz = offsetof(struct vfio_iommu_type1_info, cap_offset),
	};
	ioctl_check_buf(self->fd, VFIO_IOMMU_GET_INFO);
	ASSERT_NE(0, info_cmd->iova_pgsizes);
	ASSERT_EQ(VFIO_IOMMU_INFO_PGSIZES | VFIO_IOMMU_INFO_CAPS,
		  info_cmd->flags);

	/* Read the cap chain size */
	*info_cmd = (struct vfio_iommu_type1_info){
		.argsz = sizeof(*info_cmd),
	};
	ioctl_check_buf(self->fd, VFIO_IOMMU_GET_INFO);
	ASSERT_NE(0, info_cmd->iova_pgsizes);
	ASSERT_EQ(VFIO_IOMMU_INFO_PGSIZES | VFIO_IOMMU_INFO_CAPS,
		  info_cmd->flags);
	ASSERT_EQ(0, info_cmd->cap_offset);
	ASSERT_LT(sizeof(*info_cmd), info_cmd->argsz);

	/* Read the caps, kernel should never create a corrupted caps */
	caplen = info_cmd->argsz;
	for (i = sizeof(*info_cmd); i < caplen; i++) {
		*info_cmd = (struct vfio_iommu_type1_info){
			.argsz = i,
		};
		ioctl_check_buf(self->fd, VFIO_IOMMU_GET_INFO);
		ASSERT_EQ(VFIO_IOMMU_INFO_PGSIZES | VFIO_IOMMU_INFO_CAPS,
			  info_cmd->flags);
		if (!info_cmd->cap_offset)
			continue;
		check_vfio_info_cap_chain(_metadata, info_cmd);
	}
}

static void shuffle_array(unsigned long *array, size_t nelms)
{
	unsigned int i;

	/* Shuffle */
	for (i = 0; i != nelms; i++) {
		unsigned long tmp = array[i];
		unsigned int other = rand() % (nelms - i);

		array[i] = array[other];
		array[other] = tmp;
	}
}

TEST_F(vfio_compat_mock_domain, map)
{
	struct vfio_iommu_type1_dma_map map_cmd = {
		.argsz = sizeof(map_cmd),
		.flags = VFIO_DMA_MAP_FLAG_READ | VFIO_DMA_MAP_FLAG_WRITE,
		.vaddr = (uintptr_t)buffer,
		.size = BUFFER_SIZE,
		.iova = MOCK_APERTURE_START,
	};
	struct vfio_iommu_type1_dma_unmap unmap_cmd = {
		.argsz = sizeof(unmap_cmd),
		.size = BUFFER_SIZE,
		.iova = MOCK_APERTURE_START,
	};
	unsigned long pages_iova[BUFFER_SIZE / PAGE_SIZE];
	unsigned int i;

	/* Simple map/unmap */
	ASSERT_EQ(0, ioctl(self->fd, VFIO_IOMMU_MAP_DMA, &map_cmd));
	ASSERT_EQ(0, ioctl(self->fd, VFIO_IOMMU_UNMAP_DMA, &unmap_cmd));
	ASSERT_EQ(BUFFER_SIZE, unmap_cmd.size);

	/* UNMAP_FLAG_ALL requires 0 iova/size */
	ASSERT_EQ(0, ioctl(self->fd, VFIO_IOMMU_MAP_DMA, &map_cmd));
	unmap_cmd.flags = VFIO_DMA_UNMAP_FLAG_ALL;
	EXPECT_ERRNO(EINVAL, ioctl(self->fd, VFIO_IOMMU_UNMAP_DMA, &unmap_cmd));

	unmap_cmd.iova = 0;
	unmap_cmd.size = 0;
	ASSERT_EQ(0, ioctl(self->fd, VFIO_IOMMU_UNMAP_DMA, &unmap_cmd));
	ASSERT_EQ(BUFFER_SIZE, unmap_cmd.size);

	/* Small pages */
	for (i = 0; i != ARRAY_SIZE(pages_iova); i++) {
		map_cmd.iova = pages_iova[i] =
			MOCK_APERTURE_START + i * PAGE_SIZE;
		map_cmd.vaddr = (uintptr_t)buffer + i * PAGE_SIZE;
		map_cmd.size = PAGE_SIZE;
		ASSERT_EQ(0, ioctl(self->fd, VFIO_IOMMU_MAP_DMA, &map_cmd));
	}
	shuffle_array(pages_iova, ARRAY_SIZE(pages_iova));

	unmap_cmd.flags = 0;
	unmap_cmd.size = PAGE_SIZE;
	for (i = 0; i != ARRAY_SIZE(pages_iova); i++) {
		unmap_cmd.iova = pages_iova[i];
		ASSERT_EQ(0, ioctl(self->fd, VFIO_IOMMU_UNMAP_DMA, &unmap_cmd));
	}
}

TEST_F(vfio_compat_mock_domain, huge_map)
{
	size_t buf_size = HUGEPAGE_SIZE * 2;
	struct vfio_iommu_type1_dma_map map_cmd = {
		.argsz = sizeof(map_cmd),
		.flags = VFIO_DMA_MAP_FLAG_READ | VFIO_DMA_MAP_FLAG_WRITE,
		.size = buf_size,
		.iova = MOCK_APERTURE_START,
	};
	struct vfio_iommu_type1_dma_unmap unmap_cmd = {
		.argsz = sizeof(unmap_cmd),
	};
	unsigned long pages_iova[16];
	unsigned int i;
	void *buf;

	/* Test huge pages and splitting */
	buf = mmap(0, buf_size, PROT_READ | PROT_WRITE,
		   MAP_SHARED | MAP_ANONYMOUS | MAP_HUGETLB | MAP_POPULATE, -1,
		   0);
	ASSERT_NE(MAP_FAILED, buf);
	map_cmd.vaddr = (uintptr_t)buf;
	ASSERT_EQ(0, ioctl(self->fd, VFIO_IOMMU_MAP_DMA, &map_cmd));

	unmap_cmd.size = buf_size / ARRAY_SIZE(pages_iova);
	for (i = 0; i != ARRAY_SIZE(pages_iova); i++)
		pages_iova[i] = MOCK_APERTURE_START + (i * unmap_cmd.size);
	shuffle_array(pages_iova, ARRAY_SIZE(pages_iova));

	/* type1 mode can cut up larger mappings, type1v2 always fails */
	for (i = 0; i != ARRAY_SIZE(pages_iova); i++) {
		unmap_cmd.iova = pages_iova[i];
		unmap_cmd.size = buf_size / ARRAY_SIZE(pages_iova);
		if (variant->version == VFIO_TYPE1_IOMMU) {
			ASSERT_EQ(0, ioctl(self->fd, VFIO_IOMMU_UNMAP_DMA,
					   &unmap_cmd));
		} else {
			EXPECT_ERRNO(ENOENT,
				     ioctl(self->fd, VFIO_IOMMU_UNMAP_DMA,
					   &unmap_cmd));
		}
	}
}

TEST_HARNESS_MAIN
