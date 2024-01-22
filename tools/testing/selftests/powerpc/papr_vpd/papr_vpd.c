// SPDX-License-Identifier: GPL-2.0-only
#define _GNU_SOURCE
#include <errno.h>
#include <fcntl.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <unistd.h>

#include <asm/papr-vpd.h>

#include "utils.h"

#define DEVPATH "/dev/papr-vpd"

static int dev_papr_vpd_open_close(void)
{
	const int devfd = open(DEVPATH, O_RDONLY);

	SKIP_IF_MSG(devfd < 0 && errno == ENOENT,
		    DEVPATH " not present");

	FAIL_IF(devfd < 0);
	FAIL_IF(close(devfd) != 0);

	return 0;
}

static int dev_papr_vpd_get_handle_all(void)
{
	const int devfd = open(DEVPATH, O_RDONLY);
	struct papr_location_code lc = { .str = "", };
	off_t size;
	int fd;

	SKIP_IF_MSG(devfd < 0 && errno == ENOENT,
		    DEVPATH " not present");

	FAIL_IF(devfd < 0);

	errno = 0;
	fd = ioctl(devfd, PAPR_VPD_IOC_CREATE_HANDLE, &lc);
	FAIL_IF(errno != 0);
	FAIL_IF(fd < 0);

	FAIL_IF(close(devfd) != 0);

	size = lseek(fd, 0, SEEK_END);
	FAIL_IF(size <= 0);

	void *buf = malloc((size_t)size);
	FAIL_IF(!buf);

	ssize_t consumed = pread(fd, buf, size, 0);
	FAIL_IF(consumed != size);

	/* Ensure EOF */
	FAIL_IF(read(fd, buf, size) != 0);
	FAIL_IF(close(fd));

	/* Verify that the buffer looks like VPD */
	static const char needle[] = "System VPD";
	FAIL_IF(!memmem(buf, size, needle, strlen(needle)));

	return 0;
}

static int dev_papr_vpd_get_handle_byte_at_a_time(void)
{
	const int devfd = open(DEVPATH, O_RDONLY);
	struct papr_location_code lc = { .str = "", };
	int fd;

	SKIP_IF_MSG(devfd < 0 && errno == ENOENT,
		    DEVPATH " not present");

	FAIL_IF(devfd < 0);

	errno = 0;
	fd = ioctl(devfd, PAPR_VPD_IOC_CREATE_HANDLE, &lc);
	FAIL_IF(errno != 0);
	FAIL_IF(fd < 0);

	FAIL_IF(close(devfd) != 0);

	size_t consumed = 0;
	while (1) {
		ssize_t res;
		char c;

		errno = 0;
		res = read(fd, &c, sizeof(c));
		FAIL_IF(res > sizeof(c));
		FAIL_IF(res < 0);
		FAIL_IF(errno != 0);
		consumed += res;
		if (res == 0)
			break;
	}

	FAIL_IF(consumed != lseek(fd, 0, SEEK_END));

	FAIL_IF(close(fd));

	return 0;
}


static int dev_papr_vpd_unterm_loc_code(void)
{
	const int devfd = open(DEVPATH, O_RDONLY);
	struct papr_location_code lc = {};
	int fd;

	SKIP_IF_MSG(devfd < 0 && errno == ENOENT,
		    DEVPATH " not present");

	FAIL_IF(devfd < 0);

	/*
	 * Place a non-null byte in every element of loc_code; the
	 * driver should reject this input.
	 */
	memset(lc.str, 'x', ARRAY_SIZE(lc.str));

	errno = 0;
	fd = ioctl(devfd, PAPR_VPD_IOC_CREATE_HANDLE, &lc);
	FAIL_IF(fd != -1);
	FAIL_IF(errno != EINVAL);

	FAIL_IF(close(devfd) != 0);
	return 0;
}

static int dev_papr_vpd_null_handle(void)
{
	const int devfd = open(DEVPATH, O_RDONLY);
	int rc;

	SKIP_IF_MSG(devfd < 0 && errno == ENOENT,
		    DEVPATH " not present");

	FAIL_IF(devfd < 0);

	errno = 0;
	rc = ioctl(devfd, PAPR_VPD_IOC_CREATE_HANDLE, NULL);
	FAIL_IF(rc != -1);
	FAIL_IF(errno != EFAULT);

	FAIL_IF(close(devfd) != 0);
	return 0;
}

static int papr_vpd_close_handle_without_reading(void)
{
	const int devfd = open(DEVPATH, O_RDONLY);
	struct papr_location_code lc;
	int fd;

	SKIP_IF_MSG(devfd < 0 && errno == ENOENT,
		    DEVPATH " not present");

	FAIL_IF(devfd < 0);

	errno = 0;
	fd = ioctl(devfd, PAPR_VPD_IOC_CREATE_HANDLE, &lc);
	FAIL_IF(errno != 0);
	FAIL_IF(fd < 0);

	/* close the handle without reading it */
	FAIL_IF(close(fd) != 0);

	FAIL_IF(close(devfd) != 0);
	return 0;
}

static int papr_vpd_reread(void)
{
	const int devfd = open(DEVPATH, O_RDONLY);
	struct papr_location_code lc = { .str = "", };
	int fd;

	SKIP_IF_MSG(devfd < 0 && errno == ENOENT,
		    DEVPATH " not present");

	FAIL_IF(devfd < 0);

	errno = 0;
	fd = ioctl(devfd, PAPR_VPD_IOC_CREATE_HANDLE, &lc);
	FAIL_IF(errno != 0);
	FAIL_IF(fd < 0);

	FAIL_IF(close(devfd) != 0);

	const off_t size = lseek(fd, 0, SEEK_END);
	FAIL_IF(size <= 0);

	char *bufs[2];

	for (size_t i = 0; i < ARRAY_SIZE(bufs); ++i) {
		bufs[i] = malloc(size);
		FAIL_IF(!bufs[i]);
		ssize_t consumed = pread(fd, bufs[i], size, 0);
		FAIL_IF(consumed != size);
	}

	FAIL_IF(memcmp(bufs[0], bufs[1], size));

	FAIL_IF(close(fd) != 0);

	return 0;
}

static int get_system_loc_code(struct papr_location_code *lc)
{
	static const char system_id_path[] = "/sys/firmware/devicetree/base/system-id";
	static const char model_path[] = "/sys/firmware/devicetree/base/model";
	char *system_id;
	char *model;
	int err = -1;

	if (read_file_alloc(model_path, &model, NULL))
		return err;

	if (read_file_alloc(system_id_path, &system_id, NULL))
		goto free_model;

	char *mtm;
	int sscanf_ret = sscanf(model, "IBM,%ms", &mtm);
	if (sscanf_ret != 1)
		goto free_system_id;

	char *plant_and_seq;
	if (sscanf(system_id, "IBM,%*c%*c%ms", &plant_and_seq) != 1)
		goto free_mtm;
	/*
	 * Replace - with . to build location code.
	 */
	char *sep = strchr(mtm, '-');
	if (!sep)
		goto free_mtm;
	else
		*sep = '.';

	snprintf(lc->str, sizeof(lc->str),
		 "U%s.%s", mtm, plant_and_seq);
	err = 0;

	free(plant_and_seq);
free_mtm:
	free(mtm);
free_system_id:
	free(system_id);
free_model:
	free(model);
	return err;
}

static int papr_vpd_system_loc_code(void)
{
	struct papr_location_code lc;
	const int devfd = open(DEVPATH, O_RDONLY);
	off_t size;
	int fd;

	SKIP_IF_MSG(get_system_loc_code(&lc),
		    "Cannot determine system location code");
	SKIP_IF_MSG(devfd < 0 && errno == ENOENT,
		    DEVPATH " not present");

	FAIL_IF(devfd < 0);

	errno = 0;
	fd = ioctl(devfd, PAPR_VPD_IOC_CREATE_HANDLE, &lc);
	FAIL_IF(errno != 0);
	FAIL_IF(fd < 0);

	FAIL_IF(close(devfd) != 0);

	size = lseek(fd, 0, SEEK_END);
	FAIL_IF(size <= 0);

	void *buf = malloc((size_t)size);
	FAIL_IF(!buf);

	ssize_t consumed = pread(fd, buf, size, 0);
	FAIL_IF(consumed != size);

	/* Ensure EOF */
	FAIL_IF(read(fd, buf, size) != 0);
	FAIL_IF(close(fd));

	/* Verify that the buffer looks like VPD */
	static const char needle[] = "System VPD";
	FAIL_IF(!memmem(buf, size, needle, strlen(needle)));

	return 0;
}

struct vpd_test {
	int (*function)(void);
	const char *description;
};

static const struct vpd_test vpd_tests[] = {
	{
		.function = dev_papr_vpd_open_close,
		.description = "open/close " DEVPATH,
	},
	{
		.function = dev_papr_vpd_unterm_loc_code,
		.description = "ensure EINVAL on unterminated location code",
	},
	{
		.function = dev_papr_vpd_null_handle,
		.description = "ensure EFAULT on bad handle addr",
	},
	{
		.function = dev_papr_vpd_get_handle_all,
		.description = "get handle for all VPD"
	},
	{
		.function = papr_vpd_close_handle_without_reading,
		.description = "close handle without consuming VPD"
	},
	{
		.function = dev_papr_vpd_get_handle_byte_at_a_time,
		.description = "read all VPD one byte at a time"
	},
	{
		.function = papr_vpd_reread,
		.description = "ensure re-read yields same results"
	},
	{
		.function = papr_vpd_system_loc_code,
		.description = "get handle for system VPD"
	},
};

int main(void)
{
	size_t fails = 0;

	for (size_t i = 0; i < ARRAY_SIZE(vpd_tests); ++i) {
		const struct vpd_test *t = &vpd_tests[i];

		if (test_harness(t->function, t->description))
			++fails;
	}

	return fails == 0 ? EXIT_SUCCESS : EXIT_FAILURE;
}
