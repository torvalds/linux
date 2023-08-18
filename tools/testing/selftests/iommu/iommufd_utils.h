/* SPDX-License-Identifier: GPL-2.0-only */
/* Copyright (c) 2021-2022, NVIDIA CORPORATION & AFFILIATES */
#ifndef __SELFTEST_IOMMUFD_UTILS
#define __SELFTEST_IOMMUFD_UTILS

#include <unistd.h>
#include <stddef.h>
#include <sys/fcntl.h>
#include <sys/ioctl.h>
#include <stdint.h>
#include <assert.h>

#include "../kselftest_harness.h"
#include "../../../../drivers/iommu/iommufd/iommufd_test.h"

/* Hack to make assertions more readable */
#define _IOMMU_TEST_CMD(x) IOMMU_TEST_CMD

static void *buffer;
static unsigned long BUFFER_SIZE;

static unsigned long PAGE_SIZE;

#define sizeof_field(TYPE, MEMBER) sizeof((((TYPE *)0)->MEMBER))
#define offsetofend(TYPE, MEMBER) \
	(offsetof(TYPE, MEMBER) + sizeof_field(TYPE, MEMBER))

/*
 * Have the kernel check the refcount on pages. I don't know why a freshly
 * mmap'd anon non-compound page starts out with a ref of 3
 */
#define check_refs(_ptr, _length, _refs)                                      \
	({                                                                    \
		struct iommu_test_cmd test_cmd = {                            \
			.size = sizeof(test_cmd),                             \
			.op = IOMMU_TEST_OP_MD_CHECK_REFS,                    \
			.check_refs = { .length = _length,                    \
					.uptr = (uintptr_t)(_ptr),            \
					.refs = _refs },                      \
		};                                                            \
		ASSERT_EQ(0,                                                  \
			  ioctl(self->fd,                                     \
				_IOMMU_TEST_CMD(IOMMU_TEST_OP_MD_CHECK_REFS), \
				&test_cmd));                                  \
	})

static int _test_cmd_mock_domain(int fd, unsigned int ioas_id, __u32 *stdev_id,
				 __u32 *hwpt_id, __u32 *idev_id)
{
	struct iommu_test_cmd cmd = {
		.size = sizeof(cmd),
		.op = IOMMU_TEST_OP_MOCK_DOMAIN,
		.id = ioas_id,
		.mock_domain = {},
	};
	int ret;

	ret = ioctl(fd, IOMMU_TEST_CMD, &cmd);
	if (ret)
		return ret;
	if (stdev_id)
		*stdev_id = cmd.mock_domain.out_stdev_id;
	assert(cmd.id != 0);
	if (hwpt_id)
		*hwpt_id = cmd.mock_domain.out_hwpt_id;
	if (idev_id)
		*idev_id = cmd.mock_domain.out_idev_id;
	return 0;
}
#define test_cmd_mock_domain(ioas_id, stdev_id, hwpt_id, idev_id)       \
	ASSERT_EQ(0, _test_cmd_mock_domain(self->fd, ioas_id, stdev_id, \
					   hwpt_id, idev_id))
#define test_err_mock_domain(_errno, ioas_id, stdev_id, hwpt_id)      \
	EXPECT_ERRNO(_errno, _test_cmd_mock_domain(self->fd, ioas_id, \
						   stdev_id, hwpt_id, NULL))

static int _test_cmd_mock_domain_replace(int fd, __u32 stdev_id, __u32 pt_id,
					 __u32 *hwpt_id)
{
	struct iommu_test_cmd cmd = {
		.size = sizeof(cmd),
		.op = IOMMU_TEST_OP_MOCK_DOMAIN_REPLACE,
		.id = stdev_id,
		.mock_domain_replace = {
			.pt_id = pt_id,
		},
	};
	int ret;

	ret = ioctl(fd, IOMMU_TEST_CMD, &cmd);
	if (ret)
		return ret;
	if (hwpt_id)
		*hwpt_id = cmd.mock_domain_replace.pt_id;
	return 0;
}

#define test_cmd_mock_domain_replace(stdev_id, pt_id)                         \
	ASSERT_EQ(0, _test_cmd_mock_domain_replace(self->fd, stdev_id, pt_id, \
						   NULL))
#define test_err_mock_domain_replace(_errno, stdev_id, pt_id)                  \
	EXPECT_ERRNO(_errno, _test_cmd_mock_domain_replace(self->fd, stdev_id, \
							   pt_id, NULL))

static int _test_cmd_hwpt_alloc(int fd, __u32 device_id, __u32 pt_id,
					 __u32 *hwpt_id)
{
	struct iommu_hwpt_alloc cmd = {
		.size = sizeof(cmd),
		.dev_id = device_id,
		.pt_id = pt_id,
	};
	int ret;

	ret = ioctl(fd, IOMMU_HWPT_ALLOC, &cmd);
	if (ret)
		return ret;
	if (hwpt_id)
		*hwpt_id = cmd.out_hwpt_id;
	return 0;
}

#define test_cmd_hwpt_alloc(device_id, pt_id, hwpt_id) \
	ASSERT_EQ(0, _test_cmd_hwpt_alloc(self->fd, device_id, pt_id, hwpt_id))

static int _test_cmd_access_replace_ioas(int fd, __u32 access_id,
					 unsigned int ioas_id)
{
	struct iommu_test_cmd cmd = {
		.size = sizeof(cmd),
		.op = IOMMU_TEST_OP_ACCESS_REPLACE_IOAS,
		.id = access_id,
		.access_replace_ioas = { .ioas_id = ioas_id },
	};
	int ret;

	ret = ioctl(fd, IOMMU_TEST_CMD, &cmd);
	if (ret)
		return ret;
	return 0;
}
#define test_cmd_access_replace_ioas(access_id, ioas_id) \
	ASSERT_EQ(0, _test_cmd_access_replace_ioas(self->fd, access_id, ioas_id))

static int _test_cmd_create_access(int fd, unsigned int ioas_id,
				   __u32 *access_id, unsigned int flags)
{
	struct iommu_test_cmd cmd = {
		.size = sizeof(cmd),
		.op = IOMMU_TEST_OP_CREATE_ACCESS,
		.id = ioas_id,
		.create_access = { .flags = flags },
	};
	int ret;

	ret = ioctl(fd, IOMMU_TEST_CMD, &cmd);
	if (ret)
		return ret;
	*access_id = cmd.create_access.out_access_fd;
	return 0;
}
#define test_cmd_create_access(ioas_id, access_id, flags)                  \
	ASSERT_EQ(0, _test_cmd_create_access(self->fd, ioas_id, access_id, \
					     flags))

static int _test_cmd_destroy_access(unsigned int access_id)
{
	return close(access_id);
}
#define test_cmd_destroy_access(access_id) \
	ASSERT_EQ(0, _test_cmd_destroy_access(access_id))

static int _test_cmd_destroy_access_pages(int fd, unsigned int access_id,
					  unsigned int access_pages_id)
{
	struct iommu_test_cmd cmd = {
		.size = sizeof(cmd),
		.op = IOMMU_TEST_OP_DESTROY_ACCESS_PAGES,
		.id = access_id,
		.destroy_access_pages = { .access_pages_id = access_pages_id },
	};
	return ioctl(fd, IOMMU_TEST_CMD, &cmd);
}
#define test_cmd_destroy_access_pages(access_id, access_pages_id)        \
	ASSERT_EQ(0, _test_cmd_destroy_access_pages(self->fd, access_id, \
						    access_pages_id))
#define test_err_destroy_access_pages(_errno, access_id, access_pages_id) \
	EXPECT_ERRNO(_errno, _test_cmd_destroy_access_pages(              \
				     self->fd, access_id, access_pages_id))

static int _test_ioctl_destroy(int fd, unsigned int id)
{
	struct iommu_destroy cmd = {
		.size = sizeof(cmd),
		.id = id,
	};
	return ioctl(fd, IOMMU_DESTROY, &cmd);
}
#define test_ioctl_destroy(id) ASSERT_EQ(0, _test_ioctl_destroy(self->fd, id))

static int _test_ioctl_ioas_alloc(int fd, __u32 *id)
{
	struct iommu_ioas_alloc cmd = {
		.size = sizeof(cmd),
	};
	int ret;

	ret = ioctl(fd, IOMMU_IOAS_ALLOC, &cmd);
	if (ret)
		return ret;
	*id = cmd.out_ioas_id;
	return 0;
}
#define test_ioctl_ioas_alloc(id)                                   \
	({                                                          \
		ASSERT_EQ(0, _test_ioctl_ioas_alloc(self->fd, id)); \
		ASSERT_NE(0, *(id));                                \
	})

static int _test_ioctl_ioas_map(int fd, unsigned int ioas_id, void *buffer,
				size_t length, __u64 *iova, unsigned int flags)
{
	struct iommu_ioas_map cmd = {
		.size = sizeof(cmd),
		.flags = flags,
		.ioas_id = ioas_id,
		.user_va = (uintptr_t)buffer,
		.length = length,
	};
	int ret;

	if (flags & IOMMU_IOAS_MAP_FIXED_IOVA)
		cmd.iova = *iova;

	ret = ioctl(fd, IOMMU_IOAS_MAP, &cmd);
	*iova = cmd.iova;
	return ret;
}
#define test_ioctl_ioas_map(buffer, length, iova_p)                        \
	ASSERT_EQ(0, _test_ioctl_ioas_map(self->fd, self->ioas_id, buffer, \
					  length, iova_p,                  \
					  IOMMU_IOAS_MAP_WRITEABLE |       \
						  IOMMU_IOAS_MAP_READABLE))

#define test_err_ioctl_ioas_map(_errno, buffer, length, iova_p)            \
	EXPECT_ERRNO(_errno,                                               \
		     _test_ioctl_ioas_map(self->fd, self->ioas_id, buffer, \
					  length, iova_p,                  \
					  IOMMU_IOAS_MAP_WRITEABLE |       \
						  IOMMU_IOAS_MAP_READABLE))

#define test_ioctl_ioas_map_id(ioas_id, buffer, length, iova_p)              \
	ASSERT_EQ(0, _test_ioctl_ioas_map(self->fd, ioas_id, buffer, length, \
					  iova_p,                            \
					  IOMMU_IOAS_MAP_WRITEABLE |         \
						  IOMMU_IOAS_MAP_READABLE))

#define test_ioctl_ioas_map_fixed(buffer, length, iova)                       \
	({                                                                    \
		__u64 __iova = iova;                                          \
		ASSERT_EQ(0, _test_ioctl_ioas_map(                            \
				     self->fd, self->ioas_id, buffer, length, \
				     &__iova,                                 \
				     IOMMU_IOAS_MAP_FIXED_IOVA |              \
					     IOMMU_IOAS_MAP_WRITEABLE |       \
					     IOMMU_IOAS_MAP_READABLE));       \
	})

#define test_err_ioctl_ioas_map_fixed(_errno, buffer, length, iova)           \
	({                                                                    \
		__u64 __iova = iova;                                          \
		EXPECT_ERRNO(_errno,                                          \
			     _test_ioctl_ioas_map(                            \
				     self->fd, self->ioas_id, buffer, length, \
				     &__iova,                                 \
				     IOMMU_IOAS_MAP_FIXED_IOVA |              \
					     IOMMU_IOAS_MAP_WRITEABLE |       \
					     IOMMU_IOAS_MAP_READABLE));       \
	})

static int _test_ioctl_ioas_unmap(int fd, unsigned int ioas_id, uint64_t iova,
				  size_t length, uint64_t *out_len)
{
	struct iommu_ioas_unmap cmd = {
		.size = sizeof(cmd),
		.ioas_id = ioas_id,
		.iova = iova,
		.length = length,
	};
	int ret;

	ret = ioctl(fd, IOMMU_IOAS_UNMAP, &cmd);
	if (out_len)
		*out_len = cmd.length;
	return ret;
}
#define test_ioctl_ioas_unmap(iova, length)                                \
	ASSERT_EQ(0, _test_ioctl_ioas_unmap(self->fd, self->ioas_id, iova, \
					    length, NULL))

#define test_ioctl_ioas_unmap_id(ioas_id, iova, length)                      \
	ASSERT_EQ(0, _test_ioctl_ioas_unmap(self->fd, ioas_id, iova, length, \
					    NULL))

#define test_err_ioctl_ioas_unmap(_errno, iova, length)                      \
	EXPECT_ERRNO(_errno, _test_ioctl_ioas_unmap(self->fd, self->ioas_id, \
						    iova, length, NULL))

static int _test_ioctl_set_temp_memory_limit(int fd, unsigned int limit)
{
	struct iommu_test_cmd memlimit_cmd = {
		.size = sizeof(memlimit_cmd),
		.op = IOMMU_TEST_OP_SET_TEMP_MEMORY_LIMIT,
		.memory_limit = { .limit = limit },
	};

	return ioctl(fd, _IOMMU_TEST_CMD(IOMMU_TEST_OP_SET_TEMP_MEMORY_LIMIT),
		     &memlimit_cmd);
}

#define test_ioctl_set_temp_memory_limit(limit) \
	ASSERT_EQ(0, _test_ioctl_set_temp_memory_limit(self->fd, limit))

#define test_ioctl_set_default_memory_limit() \
	test_ioctl_set_temp_memory_limit(65536)

static void teardown_iommufd(int fd, struct __test_metadata *_metadata)
{
	struct iommu_test_cmd test_cmd = {
		.size = sizeof(test_cmd),
		.op = IOMMU_TEST_OP_MD_CHECK_REFS,
		.check_refs = { .length = BUFFER_SIZE,
				.uptr = (uintptr_t)buffer },
	};

	if (fd == -1)
		return;

	EXPECT_EQ(0, close(fd));

	fd = open("/dev/iommu", O_RDWR);
	EXPECT_NE(-1, fd);
	EXPECT_EQ(0, ioctl(fd, _IOMMU_TEST_CMD(IOMMU_TEST_OP_MD_CHECK_REFS),
			   &test_cmd));
	EXPECT_EQ(0, close(fd));
}

#define EXPECT_ERRNO(expected_errno, cmd)         \
	({                                        \
		ASSERT_EQ(-1, cmd);               \
		EXPECT_EQ(expected_errno, errno); \
	})

#endif

/* @data can be NULL */
static int _test_cmd_get_hw_info(int fd, __u32 device_id,
				 void *data, size_t data_len)
{
	struct iommu_test_hw_info *info = (struct iommu_test_hw_info *)data;
	struct iommu_hw_info cmd = {
		.size = sizeof(cmd),
		.dev_id = device_id,
		.data_len = data_len,
		.data_uptr = (uint64_t)data,
	};
	int ret;

	ret = ioctl(fd, IOMMU_GET_HW_INFO, &cmd);
	if (ret)
		return ret;

	assert(cmd.out_data_type == IOMMU_HW_INFO_TYPE_SELFTEST);

	/*
	 * The struct iommu_test_hw_info should be the one defined
	 * by the current kernel.
	 */
	assert(cmd.data_len == sizeof(struct iommu_test_hw_info));

	/*
	 * Trailing bytes should be 0 if user buffer is larger than
	 * the data that kernel reports.
	 */
	if (data_len > cmd.data_len) {
		char *ptr = (char *)(data + cmd.data_len);
		int idx = 0;

		while (idx < data_len - cmd.data_len) {
			assert(!*(ptr + idx));
			idx++;
		}
	}

	if (info) {
		if (data_len >= offsetofend(struct iommu_test_hw_info, test_reg))
			assert(info->test_reg == IOMMU_HW_INFO_SELFTEST_REGVAL);
		if (data_len >= offsetofend(struct iommu_test_hw_info, flags))
			assert(!info->flags);
	}

	return 0;
}

#define test_cmd_get_hw_info(device_id, data, data_len)         \
	ASSERT_EQ(0, _test_cmd_get_hw_info(self->fd, device_id, \
					   data, data_len))

#define test_err_get_hw_info(_errno, device_id, data, data_len) \
	EXPECT_ERRNO(_errno,                                    \
		     _test_cmd_get_hw_info(self->fd, device_id, \
					   data, data_len))
