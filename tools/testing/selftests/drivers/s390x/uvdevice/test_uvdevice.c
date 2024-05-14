// SPDX-License-Identifier: GPL-2.0
/*
 *  selftest for the Ultravisor UAPI device
 *
 *  Copyright IBM Corp. 2022
 *  Author(s): Steffen Eiden <seiden@linux.ibm.com>
 */

#include <stdint.h>
#include <fcntl.h>
#include <errno.h>
#include <sys/ioctl.h>
#include <sys/mman.h>

#include <asm/uvdevice.h>

#include "../../../kselftest_harness.h"

#define UV_PATH  "/dev/uv"
#define BUFFER_SIZE 0x200
FIXTURE(uvio_fixture) {
	int uv_fd;
	struct uvio_ioctl_cb uvio_ioctl;
	uint8_t buffer[BUFFER_SIZE];
	__u64 fault_page;
};

FIXTURE_VARIANT(uvio_fixture) {
	unsigned long ioctl_cmd;
	uint32_t arg_size;
};

FIXTURE_VARIANT_ADD(uvio_fixture, att) {
	.ioctl_cmd = UVIO_IOCTL_ATT,
	.arg_size = sizeof(struct uvio_attest),
};

FIXTURE_SETUP(uvio_fixture)
{
	self->uv_fd = open(UV_PATH, O_ACCMODE);

	self->uvio_ioctl.argument_addr = (__u64)self->buffer;
	self->uvio_ioctl.argument_len = variant->arg_size;
	self->fault_page =
		(__u64)mmap(NULL, (size_t)getpagesize(), PROT_NONE, MAP_ANONYMOUS, -1, 0);
}

FIXTURE_TEARDOWN(uvio_fixture)
{
	if (self->uv_fd)
		close(self->uv_fd);
	munmap((void *)self->fault_page, (size_t)getpagesize());
}

TEST_F(uvio_fixture, fault_ioctl_arg)
{
	int rc, errno_cache;

	rc = ioctl(self->uv_fd, variant->ioctl_cmd, NULL);
	errno_cache = errno;
	ASSERT_EQ(rc, -1);
	ASSERT_EQ(errno_cache, EFAULT);

	rc = ioctl(self->uv_fd, variant->ioctl_cmd, self->fault_page);
	errno_cache = errno;
	ASSERT_EQ(rc, -1);
	ASSERT_EQ(errno_cache, EFAULT);
}

TEST_F(uvio_fixture, fault_uvio_arg)
{
	int rc, errno_cache;

	self->uvio_ioctl.argument_addr = 0;
	rc = ioctl(self->uv_fd, variant->ioctl_cmd, &self->uvio_ioctl);
	errno_cache = errno;
	ASSERT_EQ(rc, -1);
	ASSERT_EQ(errno_cache, EFAULT);

	self->uvio_ioctl.argument_addr = self->fault_page;
	rc = ioctl(self->uv_fd, variant->ioctl_cmd, &self->uvio_ioctl);
	errno_cache = errno;
	ASSERT_EQ(rc, -1);
	ASSERT_EQ(errno_cache, EFAULT);
}

/*
 * Test to verify that IOCTLs with invalid values in the ioctl_control block
 * are rejected.
 */
TEST_F(uvio_fixture, inval_ioctl_cb)
{
	int rc, errno_cache;

	self->uvio_ioctl.argument_len = 0;
	rc = ioctl(self->uv_fd, variant->ioctl_cmd, &self->uvio_ioctl);
	errno_cache = errno;
	ASSERT_EQ(rc, -1);
	ASSERT_EQ(errno_cache, EINVAL);

	self->uvio_ioctl.argument_len = (uint32_t)-1;
	rc = ioctl(self->uv_fd, variant->ioctl_cmd, &self->uvio_ioctl);
	errno_cache = errno;
	ASSERT_EQ(rc, -1);
	ASSERT_EQ(errno_cache, EINVAL);
	self->uvio_ioctl.argument_len = variant->arg_size;

	self->uvio_ioctl.flags = (uint32_t)-1;
	rc = ioctl(self->uv_fd, variant->ioctl_cmd, &self->uvio_ioctl);
	errno_cache = errno;
	ASSERT_EQ(rc, -1);
	ASSERT_EQ(errno_cache, EINVAL);
	self->uvio_ioctl.flags = 0;

	memset(self->uvio_ioctl.reserved14, 0xff, sizeof(self->uvio_ioctl.reserved14));
	rc = ioctl(self->uv_fd, variant->ioctl_cmd, &self->uvio_ioctl);
	errno_cache = errno;
	ASSERT_EQ(rc, -1);
	ASSERT_EQ(errno_cache, EINVAL);

	memset(&self->uvio_ioctl, 0x11, sizeof(self->uvio_ioctl));
	rc = ioctl(self->uv_fd, variant->ioctl_cmd, &self->uvio_ioctl);
	ASSERT_EQ(rc, -1);
}

TEST_F(uvio_fixture, inval_ioctl_cmd)
{
	int rc, errno_cache;
	uint8_t nr = _IOC_NR(variant->ioctl_cmd);
	unsigned long cmds[] = {
		_IOWR('a', nr, struct uvio_ioctl_cb),
		_IOWR(UVIO_TYPE_UVC, nr, int),
		_IO(UVIO_TYPE_UVC, nr),
		_IOR(UVIO_TYPE_UVC, nr, struct uvio_ioctl_cb),
		_IOW(UVIO_TYPE_UVC, nr, struct uvio_ioctl_cb),
	};

	for (size_t i = 0; i < ARRAY_SIZE(cmds); i++) {
		rc = ioctl(self->uv_fd, cmds[i], &self->uvio_ioctl);
		errno_cache = errno;
		ASSERT_EQ(rc, -1);
		ASSERT_EQ(errno_cache, ENOTTY);
	}
}

struct test_attest_buffer {
	uint8_t arcb[0x180];
	uint8_t meas[64];
	uint8_t add[32];
};

FIXTURE(attest_fixture) {
	int uv_fd;
	struct uvio_ioctl_cb uvio_ioctl;
	struct uvio_attest uvio_attest;
	struct test_attest_buffer attest_buffer;
	__u64 fault_page;
};

FIXTURE_SETUP(attest_fixture)
{
	self->uv_fd = open(UV_PATH, O_ACCMODE);

	self->uvio_ioctl.argument_addr = (__u64)&self->uvio_attest;
	self->uvio_ioctl.argument_len = sizeof(self->uvio_attest);

	self->uvio_attest.arcb_addr = (__u64)&self->attest_buffer.arcb;
	self->uvio_attest.arcb_len = sizeof(self->attest_buffer.arcb);

	self->uvio_attest.meas_addr = (__u64)&self->attest_buffer.meas;
	self->uvio_attest.meas_len = sizeof(self->attest_buffer.meas);

	self->uvio_attest.add_data_addr = (__u64)&self->attest_buffer.add;
	self->uvio_attest.add_data_len = sizeof(self->attest_buffer.add);
	self->fault_page =
		(__u64)mmap(NULL, (size_t)getpagesize(), PROT_NONE, MAP_ANONYMOUS, -1, 0);
}

FIXTURE_TEARDOWN(attest_fixture)
{
	if (self->uv_fd)
		close(self->uv_fd);
	munmap((void *)self->fault_page, (size_t)getpagesize());
}

static void att_inval_sizes_test(uint32_t *size, uint32_t max_size, bool test_zero,
				 struct __test_metadata *_metadata,
				 FIXTURE_DATA(attest_fixture) *self)
{
	int rc, errno_cache;
	uint32_t tmp = *size;

	if (test_zero) {
		*size = 0;
		rc = ioctl(self->uv_fd, UVIO_IOCTL_ATT, &self->uvio_ioctl);
		errno_cache = errno;
		ASSERT_EQ(rc, -1);
		ASSERT_EQ(errno_cache, EINVAL);
	}
	*size = max_size + 1;
	rc = ioctl(self->uv_fd, UVIO_IOCTL_ATT, &self->uvio_ioctl);
	errno_cache = errno;
	ASSERT_EQ(rc, -1);
	ASSERT_EQ(errno_cache, EINVAL);
	*size = tmp;
}

/*
 * Test to verify that attestation IOCTLs with invalid values in the UVIO
 * attestation control block are rejected.
 */
TEST_F(attest_fixture, att_inval_request)
{
	int rc, errno_cache;

	att_inval_sizes_test(&self->uvio_attest.add_data_len, UVIO_ATT_ADDITIONAL_MAX_LEN,
			     false, _metadata, self);
	att_inval_sizes_test(&self->uvio_attest.meas_len, UVIO_ATT_MEASUREMENT_MAX_LEN,
			     true, _metadata, self);
	att_inval_sizes_test(&self->uvio_attest.arcb_len, UVIO_ATT_ARCB_MAX_LEN,
			     true, _metadata, self);

	self->uvio_attest.reserved136 = (uint16_t)-1;
	rc = ioctl(self->uv_fd, UVIO_IOCTL_ATT, &self->uvio_ioctl);
	errno_cache = errno;
	ASSERT_EQ(rc, -1);
	ASSERT_EQ(errno_cache, EINVAL);

	memset(&self->uvio_attest, 0x11, sizeof(self->uvio_attest));
	rc = ioctl(self->uv_fd, UVIO_IOCTL_ATT, &self->uvio_ioctl);
	ASSERT_EQ(rc, -1);
}

static void att_inval_addr_test(__u64 *addr, struct __test_metadata *_metadata,
				FIXTURE_DATA(attest_fixture) *self)
{
	int rc, errno_cache;
	__u64 tmp = *addr;

	*addr = 0;
	rc = ioctl(self->uv_fd, UVIO_IOCTL_ATT, &self->uvio_ioctl);
	errno_cache = errno;
	ASSERT_EQ(rc, -1);
	ASSERT_EQ(errno_cache, EFAULT);
	*addr = self->fault_page;
	rc = ioctl(self->uv_fd, UVIO_IOCTL_ATT, &self->uvio_ioctl);
	errno_cache = errno;
	ASSERT_EQ(rc, -1);
	ASSERT_EQ(errno_cache, EFAULT);
	*addr = tmp;
}

TEST_F(attest_fixture, att_inval_addr)
{
	att_inval_addr_test(&self->uvio_attest.arcb_addr, _metadata, self);
	att_inval_addr_test(&self->uvio_attest.add_data_addr, _metadata, self);
	att_inval_addr_test(&self->uvio_attest.meas_addr, _metadata, self);
}

static void __attribute__((constructor)) __constructor_order_last(void)
{
	if (!__constructor_order)
		__constructor_order = _CONSTRUCTOR_ORDER_BACKWARD;
}

int main(int argc, char **argv)
{
	int fd = open(UV_PATH, O_ACCMODE);

	if (fd < 0)
		ksft_exit_skip("No uv-device or cannot access " UV_PATH  "\n"
			       "Enable CONFIG_S390_UV_UAPI and check the access rights on "
			       UV_PATH ".\n");
	close(fd);
	return test_harness_run(argc, argv);
}
