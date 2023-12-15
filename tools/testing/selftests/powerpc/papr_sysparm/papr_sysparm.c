// SPDX-License-Identifier: GPL-2.0-only
#include <errno.h>
#include <fcntl.h>
#include <stdlib.h>
#include <sys/ioctl.h>
#include <unistd.h>
#include <asm/papr-sysparm.h>

#include "utils.h"

#define DEVPATH "/dev/papr-sysparm"

static int open_close(void)
{
	const int devfd = open(DEVPATH, O_RDONLY);

	SKIP_IF_MSG(devfd < 0 && errno == ENOENT,
		    DEVPATH " not present");

	FAIL_IF(devfd < 0);
	FAIL_IF(close(devfd) != 0);

	return 0;
}

static int get_splpar(void)
{
	struct papr_sysparm_io_block sp = {
		.parameter = 20, // SPLPAR characteristics
	};
	const int devfd = open(DEVPATH, O_RDONLY);

	SKIP_IF_MSG(devfd < 0 && errno == ENOENT,
		    DEVPATH " not present");

	FAIL_IF(devfd < 0);
	FAIL_IF(ioctl(devfd, PAPR_SYSPARM_IOC_GET, &sp) != 0);
	FAIL_IF(sp.length == 0);
	FAIL_IF(sp.length > sizeof(sp.data));
	FAIL_IF(close(devfd) != 0);

	return 0;
}

static int get_bad_parameter(void)
{
	struct papr_sysparm_io_block sp = {
		.parameter = UINT32_MAX, // there are only ~60 specified parameters
	};
	const int devfd = open(DEVPATH, O_RDONLY);

	SKIP_IF_MSG(devfd < 0 && errno == ENOENT,
		    DEVPATH " not present");

	FAIL_IF(devfd < 0);

	// Ensure expected error
	FAIL_IF(ioctl(devfd, PAPR_SYSPARM_IOC_GET, &sp) != -1);
	FAIL_IF(errno != EOPNOTSUPP);

	// Ensure the buffer is unchanged
	FAIL_IF(sp.length != 0);
	for (size_t i = 0; i < ARRAY_SIZE(sp.data); ++i)
		FAIL_IF(sp.data[i] != 0);

	FAIL_IF(close(devfd) != 0);

	return 0;
}

static int check_efault_common(unsigned long cmd)
{
	const int devfd = open(DEVPATH, O_RDWR);

	SKIP_IF_MSG(devfd < 0 && errno == ENOENT,
		    DEVPATH " not present");

	FAIL_IF(devfd < 0);

	// Ensure expected error
	FAIL_IF(ioctl(devfd, cmd, NULL) != -1);
	FAIL_IF(errno != EFAULT);

	FAIL_IF(close(devfd) != 0);

	return 0;
}

static int check_efault_get(void)
{
	return check_efault_common(PAPR_SYSPARM_IOC_GET);
}

static int check_efault_set(void)
{
	return check_efault_common(PAPR_SYSPARM_IOC_SET);
}

static int set_hmc0(void)
{
	struct papr_sysparm_io_block sp = {
		.parameter = 0, // HMC0, not a settable parameter
	};
	const int devfd = open(DEVPATH, O_RDWR);

	SKIP_IF_MSG(devfd < 0 && errno == ENOENT,
		    DEVPATH " not present");

	FAIL_IF(devfd < 0);

	// Ensure expected error
	FAIL_IF(ioctl(devfd, PAPR_SYSPARM_IOC_SET, &sp) != -1);
	SKIP_IF_MSG(errno == EOPNOTSUPP, "operation not supported");
	FAIL_IF(errno != EPERM);

	FAIL_IF(close(devfd) != 0);

	return 0;
}

static int set_with_ro_fd(void)
{
	struct papr_sysparm_io_block sp = {
		.parameter = 0, // HMC0, not a settable parameter.
	};
	const int devfd = open(DEVPATH, O_RDONLY);

	SKIP_IF_MSG(devfd < 0 && errno == ENOENT,
		    DEVPATH " not present");

	FAIL_IF(devfd < 0);

	// Ensure expected error
	FAIL_IF(ioctl(devfd, PAPR_SYSPARM_IOC_SET, &sp) != -1);
	SKIP_IF_MSG(errno == EOPNOTSUPP, "operation not supported");

	// HMC0 isn't a settable parameter and we would normally
	// expect to get EPERM on attempts to modify it. However, when
	// the file is open read-only, we expect the driver to prevent
	// the attempt with a distinct error.
	FAIL_IF(errno != EBADF);

	FAIL_IF(close(devfd) != 0);

	return 0;
}

struct sysparm_test {
	int (*function)(void);
	const char *description;
};

static const struct sysparm_test sysparm_tests[] = {
	{
		.function = open_close,
		.description = "open and close " DEVPATH " without issuing commands",
	},
	{
		.function = get_splpar,
		.description = "retrieve SPLPAR characteristics",
	},
	{
		.function = get_bad_parameter,
		.description = "verify EOPNOTSUPP for known-bad parameter",
	},
	{
		.function = check_efault_get,
		.description = "PAPR_SYSPARM_IOC_GET returns EFAULT on bad address",
	},
	{
		.function = check_efault_set,
		.description = "PAPR_SYSPARM_IOC_SET returns EFAULT on bad address",
	},
	{
		.function = set_hmc0,
		.description = "ensure EPERM on attempt to update HMC0",
	},
	{
		.function = set_with_ro_fd,
		.description = "PAPR_IOC_SYSPARM_SET returns EACCES on read-only fd",
	},
};

int main(void)
{
	size_t fails = 0;

	for (size_t i = 0; i < ARRAY_SIZE(sysparm_tests); ++i) {
		const struct sysparm_test *t = &sysparm_tests[i];

		if (test_harness(t->function, t->description))
			++fails;
	}

	return fails == 0 ? EXIT_SUCCESS : EXIT_FAILURE;
}
