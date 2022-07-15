// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright 2021 Google LLC
 *
 * This program provides commands that dump certain types of output from the
 * fips140 kernel module, as required by the FIPS lab for evaluation purposes.
 *
 * While the fips140 kernel module can only be accessed directly by other kernel
 * code, an easy-to-use userspace utility program was desired for lab testing.
 * When possible, this program uses AF_ALG to access the crypto algorithms; this
 * requires that the kernel has AF_ALG enabled.  Where AF_ALG isn't sufficient,
 * a custom device node /dev/fips140 is used instead; this requires that the
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
#include <linux/if_alg.h>
#include <stdarg.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/socket.h>
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

static void __attribute__((noreturn))
assertion_failed(const char *expr, const char *file, int line)
{
	die("Assertion failed: %s at %s:%d", expr, file, line);
}

#define ASSERT(e) ({ if (!(e)) assertion_failed(#e, __FILE__, __LINE__); })

static void rand_bytes(uint8_t *bytes, size_t count)
{
	size_t i;

	for (i = 0; i < count; i++)
		bytes[i] = rand();
}

static const char *booltostr(bool b)
{
	return b ? "true" : "false";
}

static const char *bytes_to_hex(const uint8_t *bytes, size_t count)
{
	static char hex[1025];
	size_t i;

	ASSERT(count <= 512);
	for (i = 0; i < count; i++)
		sprintf(&hex[2*i], "%02x", bytes[i]);
	return hex;
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
 *				AF_ALG utilities
 * ---------------------------------------------------------------------------*/

#define AF_ALG_MAX_RNG_REQUEST_SIZE	128

static int get_alg_fd(const char *alg_type, const char *alg_name)
{
	struct sockaddr_alg addr = {};
	int alg_fd;

	alg_fd = socket(AF_ALG, SOCK_SEQPACKET, 0);
	if (alg_fd < 0)
		die("Failed to create AF_ALG socket.\n"
"AF_ALG is only available when it has been enabled in the kernel.\n");

	strncpy((char *)addr.salg_type, alg_type, sizeof(addr.salg_type) - 1);
	strncpy((char *)addr.salg_name, alg_name, sizeof(addr.salg_name) - 1);

	if (bind(alg_fd, (void *)&addr, sizeof(addr)) != 0)
		die_errno("Failed to bind AF_ALG socket to %s %s",
			  alg_type, alg_name);
	return alg_fd;
}

static int get_req_fd(int alg_fd, const char *alg_name)
{
	int req_fd = accept(alg_fd, NULL, NULL);

	if (req_fd < 0)
		die_errno("Failed to get request file descriptor for %s",
			  alg_name);
	return req_fd;
}

/* ---------------------------------------------------------------------------
 *			  show_invalid_inputs command
 * ---------------------------------------------------------------------------*/

enum direction {
	UNSPECIFIED,
	DECRYPT,
	ENCRYPT,
};

static const struct invalid_input_test {
	const char *alg_type;
	const char *alg_name;
	const char *key;
	size_t key_size;
	const char *msg;
	size_t msg_size;
	const char *iv;
	size_t iv_size;
	enum direction direction;
	int setkey_error;
	int crypt_error;
} invalid_input_tests[] = {
	{
		.alg_type = "skcipher",
		.alg_name = "cbc(aes)",
		.key_size = 16,
	}, {
		.alg_type = "skcipher",
		.alg_name = "cbc(aes)",
		.key_size = 17,
		.setkey_error = EINVAL,
	}, {
		.alg_type = "skcipher",
		.alg_name = "cbc(aes)",
		.key_size = 24,
	}, {
		.alg_type = "skcipher",
		.alg_name = "cbc(aes)",
		.key_size = 32,
	}, {
		.alg_type = "skcipher",
		.alg_name = "cbc(aes)",
		.key_size = 33,
		.setkey_error = EINVAL,
	}, {
		.alg_type = "skcipher",
		.alg_name = "cbc(aes)",
		.key_size = 16,
		.msg_size = 1,
		.direction = DECRYPT,
		.crypt_error = EINVAL,
	}, {
		.alg_type = "skcipher",
		.alg_name = "cbc(aes)",
		.key_size = 16,
		.msg_size = 16,
		.direction = ENCRYPT,
	}, {
		.alg_type = "skcipher",
		.alg_name = "cbc(aes)",
		.key_size = 16,
		.msg_size = 17,
		.direction = ENCRYPT,
		.crypt_error = EINVAL,
	}, {
		.alg_type = "hash",
		.alg_name = "cmac(aes)",
		.key_size = 29,
		.setkey_error = EINVAL,
	}, {
		.alg_type = "skcipher",
		.alg_name = "xts(aes)",
		.key_size = 32,
	}, {
		.alg_type = "skcipher",
		.alg_name = "xts(aes)",
		.key = "\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0"
		       "\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0",
		.key_size = 32,
		.setkey_error = EINVAL,
	}
};

static const char *describe_crypt_op(const struct invalid_input_test *t)
{
	if (t->direction == ENCRYPT)
		return "encryption";
	if (t->direction == DECRYPT)
		return "decryption";
	if (strcmp(t->alg_type, "hash") == 0)
		return "hashing";
	ASSERT(0);
}

static bool af_alg_setkey(const struct invalid_input_test *t, int alg_fd)
{
	const uint8_t *key = (const uint8_t *)t->key;
	uint8_t _key[t->key_size];

	if (t->key_size == 0)
		return true;

	if (t->key == NULL) {
		rand_bytes(_key, t->key_size);
		key = _key;
	}
	if (setsockopt(alg_fd, SOL_ALG, ALG_SET_KEY, key, t->key_size) != 0) {
		printf("%s: setting %zu-byte key failed with error '%s'\n",
		       t->alg_name, t->key_size, strerror(errno));
		printf("\tkey was %s\n\n", bytes_to_hex(key, t->key_size));
		ASSERT(t->setkey_error == errno);
		return false;
	}
	printf("%s: setting %zu-byte key succeeded\n",
	       t->alg_name, t->key_size);
	printf("\tkey was %s\n\n", bytes_to_hex(key, t->key_size));
	ASSERT(t->setkey_error == 0);
	return true;
}

static void af_alg_process_msg(const struct invalid_input_test *t, int alg_fd)
{
	struct iovec iov;
	struct msghdr hdr = {
		.msg_iov = &iov,
		.msg_iovlen = 1,
	};
	const uint8_t *msg = (const uint8_t *)t->msg;
	uint8_t *_msg = NULL;
	uint8_t *output = NULL;
	uint8_t *control = NULL;
	size_t controllen = 0;
	struct cmsghdr *cmsg;
	int req_fd;

	if (t->msg_size == 0)
		return;

	req_fd = get_req_fd(alg_fd, t->alg_name);

	if (t->msg == NULL) {
		_msg = malloc(t->msg_size);
		rand_bytes(_msg, t->msg_size);
		msg = _msg;
	}
	output = malloc(t->msg_size);
	iov.iov_base = (void *)msg;
	iov.iov_len = t->msg_size;

	if (t->direction != UNSPECIFIED)
		controllen += CMSG_SPACE(sizeof(uint32_t));
	if (t->iv_size)
		controllen += CMSG_SPACE(sizeof(struct af_alg_iv) + t->iv_size);
	control = calloc(1, controllen);
	hdr.msg_control = control;
	hdr.msg_controllen = controllen;
	cmsg = CMSG_FIRSTHDR(&hdr);
	if (t->direction != UNSPECIFIED) {
		cmsg->cmsg_level = SOL_ALG;
		cmsg->cmsg_type = ALG_SET_OP;
		cmsg->cmsg_len = CMSG_LEN(sizeof(uint32_t));
		*(uint32_t *)CMSG_DATA(cmsg) = t->direction == DECRYPT ?
				ALG_OP_DECRYPT : ALG_OP_ENCRYPT;
		cmsg = CMSG_NXTHDR(&hdr, cmsg);
	}
	if (t->iv_size) {
		struct af_alg_iv *alg_iv;

		cmsg->cmsg_level = SOL_ALG;
		cmsg->cmsg_type = ALG_SET_IV;
		cmsg->cmsg_len = CMSG_LEN(sizeof(*alg_iv) + t->iv_size);
		alg_iv = (struct af_alg_iv *)CMSG_DATA(cmsg);
		alg_iv->ivlen = t->iv_size;
		memcpy(alg_iv->iv, t->iv, t->iv_size);
	}

	if (sendmsg(req_fd, &hdr, 0) != t->msg_size)
		die_errno("sendmsg failed");

	if (read(req_fd, output, t->msg_size) != t->msg_size) {
		printf("%s: %s of %zu-byte message failed with error '%s'\n",
		       t->alg_name, describe_crypt_op(t), t->msg_size,
		       strerror(errno));
		printf("\tmessage was %s\n\n", bytes_to_hex(msg, t->msg_size));
		ASSERT(t->crypt_error == errno);
	} else {
		printf("%s: %s of %zu-byte message succeeded\n",
		       t->alg_name, describe_crypt_op(t), t->msg_size);
		printf("\tmessage was %s\n\n", bytes_to_hex(msg, t->msg_size));
		ASSERT(t->crypt_error == 0);
	}
	free(_msg);
	free(output);
	free(control);
	close(req_fd);
}

static void test_invalid_input(const struct invalid_input_test *t)
{
	int alg_fd = get_alg_fd(t->alg_type, t->alg_name);

	if (af_alg_setkey(t, alg_fd))
		af_alg_process_msg(t, alg_fd);

	close(alg_fd);
}

static int cmd_show_invalid_inputs(int argc, char *argv[])
{
	int i;

	for (i = 0; i < ARRAY_SIZE(invalid_input_tests); i++)
		test_invalid_input(&invalid_input_tests[i]);
	return 0;
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
	{ "show_invalid_inputs", cmd_show_invalid_inputs },
	{ "show_module_version", cmd_show_module_version },
	{ "show_service_indicators", cmd_show_service_indicators },
};

static void usage(void)
{
	fprintf(stderr,
"Usage:\n"
"       fips140_lab_util show_invalid_inputs\n"
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
