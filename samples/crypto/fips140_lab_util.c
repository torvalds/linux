// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright 2021 Google LLC
 *
 * This program provides commands that dump certain types of output from the
 * fips140 kernel module, as required by the FIPS lab for evaluation purposes.
 *
 * While the fips140 kernel module can only be accessed directly by other kernel
 * code, an easy-to-use userspace utility program was desired for lab testing.
 * For this, a custom device node /dev/fips140 is used; this requires that the
 * fips140 module is loaded and has evaluation testing support compiled in.
 *
 * This program can be compiled and run on an Android device as follows:
 *
 *	NDK_DIR=$HOME/android-ndk-r23b  # adjust directory path as needed
 *	$NDK_DIR/toolchains/llvm/prebuilt/linux-x86_64/bin/aarch64-linux-android31-clang \
 *		fips140_lab_util.c -O2 -Wall -o fips140_lab_util
 *	adb push fips140_lab_util /data/local/tmp/
 *	adb root
 *	adb shell /data/local/tmp/fips140_lab_util
 */

#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <stdarg.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/stat.h>
#include <sys/sysmacros.h>
#include <unistd.h>

#include "../../crypto/fips140-eval-testing-uapi.h"

/* ---------------------------------------------------------------------------
 *			       Utility functions
 * ---------------------------------------------------------------------------*/

#define ARRAY_SIZE(A)	(sizeof(A) / sizeof((A)[0]))

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

static const char *booltostr(bool b)
{
	return b ? "true" : "false";
}

static void usage(void);

/* ---------------------------------------------------------------------------
 *			      /dev/fips140 ioctls
 * ---------------------------------------------------------------------------*/

static int get_fips140_device_number(void)
{
	FILE *f;
	char line[128];
	int number;
	char name[32];

	f = fopen("/proc/devices", "r");
	if (!f)
		die_errno("Failed to open /proc/devices");
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
		die_errno("Failed to create fips140 device node");
}

static int fips140_dev_fd = -1;

static int fips140_ioctl(int cmd, const void *arg)
{
	if (fips140_dev_fd < 0) {
		create_fips140_node_if_needed();
		fips140_dev_fd = open("/dev/fips140", O_RDONLY);
		if (fips140_dev_fd < 0)
			die_errno("Failed to open /dev/fips140");
	}
	return ioctl(fips140_dev_fd, cmd, arg);
}

static bool fips140_is_approved_service(const char *name)
{
	int ret = fips140_ioctl(FIPS140_IOCTL_IS_APPROVED_SERVICE, name);

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
	static char buf[256];
	int ret;

	memset(buf, 0, sizeof(buf));
	ret = fips140_ioctl(FIPS140_IOCTL_MODULE_VERSION, buf);
	if (ret < 0)
		die_errno("FIPS140_IOCTL_MODULE_VERSION unexpectedly failed");
	if (ret != 0)
		die("FIPS140_IOCTL_MODULE_VERSION returned unexpected value %d",
		    ret);
	return buf;
}

/* ---------------------------------------------------------------------------
 *			  show_module_version command
 * ---------------------------------------------------------------------------*/

static int cmd_show_module_version(int argc, char *argv[])
{
	printf("fips140_module_version() => \"%s\"\n",
	       fips140_module_version());
	return 0;
}

/* ---------------------------------------------------------------------------
 *			show_service_indicators command
 * ---------------------------------------------------------------------------*/

static const char * const default_services_to_show[] = {
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

static int cmd_show_service_indicators(int argc, char *argv[])
{
	const char * const *services = default_services_to_show;
	int count = ARRAY_SIZE(default_services_to_show);
	int i;

	if (argc > 1) {
		services = (const char **)(argv + 1);
		count = argc - 1;
	}
	for (i = 0; i < count; i++) {
		printf("fips140_is_approved_service(\"%s\") => %s\n",
		       services[i],
		       booltostr(fips140_is_approved_service(services[i])));
	}
	return 0;
}

/* ---------------------------------------------------------------------------
 *				     main()
 * ---------------------------------------------------------------------------*/

static const struct command {
	const char *name;
	int (*func)(int argc, char *argv[]);
} commands[] = {
	{ "show_module_version", cmd_show_module_version },
	{ "show_service_indicators", cmd_show_service_indicators },
};

static void usage(void)
{
	fprintf(stderr,
"Usage:\n"
"       fips140_lab_util show_module_version\n"
"       fips140_lab_util show_service_indicators [SERVICE]...\n"
	);
}

int main(int argc, char *argv[])
{
	int i;

	if (argc < 2) {
		usage();
		return 2;
	}
	for (i = 1; i < argc; i++) {
		if (strcmp(argv[i], "--help") == 0) {
			usage();
			return 2;
		}
	}

	for (i = 0; i < ARRAY_SIZE(commands); i++) {
		if (strcmp(commands[i].name, argv[1]) == 0)
			return commands[i].func(argc - 1, argv + 1);
	}
	fprintf(stderr, "Unknown command: %s\n\n", argv[1]);
	usage();
	return 2;
}
