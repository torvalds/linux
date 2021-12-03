// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright 2021 Google LLC
 *
 * This is a sample program which calls some ioctls on /dev/fips140 and prints
 * the results.  The purpose of this program is to allow the FIPS certification
 * lab to test some services of fips140.ko, which they are required to do.  This
 * is a sample program only, and it can be modified by the lab as needed.  This
 * program must be run as root, and it only works if the system has loaded a
 * build of fips140.ko with evaluation testing support enabled.
 *
 * This program can be compiled and run on an Android device as follows:
 *
 *	NDK_DIR=$HOME/android-ndk-r23b  # adjust directory path as needed
 *	$NDK_DIR/toolchains/llvm/prebuilt/linux-x86_64/bin/aarch64-linux-android31-clang \
 *		fips140_lab_test.c -O2 -Wall -o fips140_lab_test
 *	adb push fips140_lab_test /data/local/tmp/
 *	adb root
 *	adb shell /data/local/tmp/fips140_lab_test
 */

#include <errno.h>
#include <fcntl.h>
#include <stdarg.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/stat.h>
#include <sys/sysmacros.h>
#include <unistd.h>

#include "../../crypto/fips140-eval-testing-uapi.h"

static int fips140_dev_fd = -1;

#define ARRAY_SIZE(A) (sizeof(A) / sizeof((A)[0]))

static const char *booltostr(bool b)
{
	return b ? "true" : "false";
}

static void __attribute__((noreturn))
do_die(const char *format, va_list va, int err)
{
	fputs("ERROR: ", stderr);
	vfprintf(stderr, format, va);
	if (err)
		fprintf(stderr, ": %s", strerror(err));
	putc('\n', stderr);
	exit(1);
}

static void __attribute__((noreturn, format(printf, 1, 2)))
die_errno(const char *format, ...)
{
	va_list va;

	va_start(va, format);
	do_die(format, va, errno);
	va_end(va);
}

static void __attribute__((noreturn, format(printf, 1, 2)))
die(const char *format, ...)
{
	va_list va;

	va_start(va, format);
	do_die(format, va, 0);
	va_end(va);
}

static int get_fips140_device_number(void)
{
	FILE *f;
	char line[128];
	int number;
	char name[32];

	f = fopen("/proc/devices", "r");
	if (!f)
		die_errno("failed to open /proc/devices");
	while (fgets(line, sizeof(line), f)) {
		if (sscanf(line, "%d %31s", &number, name) == 2 &&
		    strcmp(name, "fips140") == 0)
			return number;
	}
	fclose(f);
	die("fips140 device node is unavailable.\n"
"The fips140 device node is only available when the fips140 module is loaded\n"
"and has been built with evaluation testing support.");
}

static void create_fips140_node_if_needed(void)
{
	struct stat stbuf;
	int major;

	if (stat("/dev/fips140", &stbuf) == 0)
		return;

	major = get_fips140_device_number();
	if (mknod("/dev/fips140", S_IFCHR | 0600, makedev(major, 1)) != 0)
		die_errno("failed to create fips140 device node");
}

static bool fips140_is_approved_service(const char *name)
{
	int ret = ioctl(fips140_dev_fd, FIPS140_IOCTL_IS_APPROVED_SERVICE, name);

	if (ret < 0)
		die_errno("FIPS140_IOCTL_IS_APPROVED_SERVICE unexpectedly failed");
	if (ret == 1)
		return true;
	if (ret == 0)
		return false;
	die("FIPS140_IOCTL_IS_APPROVED_SERVICE returned unexpected value %d",
	    ret);
}

static const char *fips140_module_version(void)
{
	char buf[256];
	char *str;
	int ret = ioctl(fips140_dev_fd, FIPS140_IOCTL_MODULE_VERSION, buf);

	if (ret < 0)
		die_errno("FIPS140_IOCTL_MODULE_VERSION unexpectedly failed");
	if (ret != 0)
		die("FIPS140_IOCTL_MODULE_VERSION returned unexpected value %d", ret);
	str = strdup(buf);
	if (!str)
		die("out of memory");
	return str;
}

static const char * const services_to_check[] = {
	"aes",
	"cbc(aes)",
	"cbcmac(aes)",
	"cmac(aes)",
	"ctr(aes)",
	"cts(cbc(aes))",
	"ecb(aes)",
	"essiv(cbc(aes),sha256)",
	"gcm(aes)",
	"hmac(sha1)",
	"hmac(sha224)",
	"hmac(sha256)",
	"hmac(sha384)",
	"hmac(sha512)",
	"jitterentropy_rng",
	"sha1",
	"sha224",
	"sha256",
	"sha384",
	"sha512",
	"stdrng",
	"xcbc(aes)",
	"xts(aes)",
};

int main(void)
{
	size_t i;

	if (getuid() != 0)
		die("This program requires root.  Run 'adb root' first.");

	create_fips140_node_if_needed();

	fips140_dev_fd = open("/dev/fips140", O_RDONLY);
	if (fips140_dev_fd < 0)
		die_errno("failed to open /dev/fips140");

	printf("fips140_module_version() => \"%s\"\n", fips140_module_version());
	for (i = 0; i < ARRAY_SIZE(services_to_check); i++) {
		const char *service = services_to_check[i];

		printf("fips140_is_approved_service(\"%s\") => %s\n", service,
		       booltostr(fips140_is_approved_service(service)));
	}
	return 0;
}
