// SPDX-License-Identifier: GPL-2.0

/*
 * Copyright (C) 2022 Huawei Technologies Duesseldorf GmbH
 *
 * Author: Roberto Sassu <roberto.sassu@huawei.com>
 */

#include <stdio.h>
#include <errno.h>
#include <stdlib.h>
#include <unistd.h>
#include <endian.h>
#include <limits.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <sys/mman.h>
#include <linux/keyctl.h>
#include <sys/xattr.h>
#include <linux/fsverity.h>
#include <test_progs.h>

#include "test_verify_pkcs7_sig.skel.h"
#include "test_sig_in_xattr.skel.h"

#define MAX_DATA_SIZE (1024 * 1024)
#define MAX_SIG_SIZE 1024

#define VERIFY_USE_SECONDARY_KEYRING (1UL)
#define VERIFY_USE_PLATFORM_KEYRING  (2UL)

#ifndef SHA256_DIGEST_SIZE
#define SHA256_DIGEST_SIZE      32
#endif

/* In stripped ARM and x86-64 modules, ~ is surprisingly rare. */
#define MODULE_SIG_STRING "~Module signature appended~\n"

/*
 * Module signature information block.
 *
 * The constituents of the signature section are, in order:
 *
 *	- Signer's name
 *	- Key identifier
 *	- Signature data
 *	- Information block
 */
struct module_signature {
	__u8	algo;		/* Public-key crypto algorithm [0] */
	__u8	hash;		/* Digest algorithm [0] */
	__u8	id_type;	/* Key identifier type [PKEY_ID_PKCS7] */
	__u8	signer_len;	/* Length of signer's name [0] */
	__u8	key_id_len;	/* Length of key identifier [0] */
	__u8	__pad[3];
	__be32	sig_len;	/* Length of signature data */
};

struct data {
	__u8 data[MAX_DATA_SIZE];
	__u32 data_len;
	__u8 sig[MAX_SIG_SIZE];
	__u32 sig_len;
};

static bool kfunc_not_supported;

static int libbpf_print_cb(enum libbpf_print_level level, const char *fmt,
			   va_list args)
{
	if (level == LIBBPF_WARN)
		vprintf(fmt, args);

	if (strcmp(fmt, "libbpf: extern (func ksym) '%s': not found in kernel or module BTFs\n"))
		return 0;

	if (strcmp(va_arg(args, char *), "bpf_verify_pkcs7_signature"))
		return 0;

	kfunc_not_supported = true;
	return 0;
}

static int _run_setup_process(const char *setup_dir, const char *cmd)
{
	int child_pid, child_status;

	child_pid = fork();
	if (child_pid == 0) {
		execlp("./verify_sig_setup.sh", "./verify_sig_setup.sh", cmd,
		       setup_dir, NULL);
		exit(errno);

	} else if (child_pid > 0) {
		waitpid(child_pid, &child_status, 0);
		return WEXITSTATUS(child_status);
	}

	return -EINVAL;
}

static int populate_data_item_str(const char *tmp_dir, struct data *data_item)
{
	struct stat st;
	char data_template[] = "/tmp/dataXXXXXX";
	char path[PATH_MAX];
	int ret, fd, child_status, child_pid;

	data_item->data_len = 4;
	memcpy(data_item->data, "test", data_item->data_len);

	fd = mkstemp(data_template);
	if (fd == -1)
		return -errno;

	ret = write(fd, data_item->data, data_item->data_len);

	close(fd);

	if (ret != data_item->data_len) {
		ret = -EIO;
		goto out;
	}

	child_pid = fork();

	if (child_pid == -1) {
		ret = -errno;
		goto out;
	}

	if (child_pid == 0) {
		snprintf(path, sizeof(path), "%s/signing_key.pem", tmp_dir);

		return execlp("./sign-file", "./sign-file", "-d", "sha256",
			      path, path, data_template, NULL);
	}

	waitpid(child_pid, &child_status, 0);

	ret = WEXITSTATUS(child_status);
	if (ret)
		goto out;

	snprintf(path, sizeof(path), "%s.p7s", data_template);

	ret = stat(path, &st);
	if (ret == -1) {
		ret = -errno;
		goto out;
	}

	if (st.st_size > sizeof(data_item->sig)) {
		ret = -EINVAL;
		goto out_sig;
	}

	data_item->sig_len = st.st_size;

	fd = open(path, O_RDONLY);
	if (fd == -1) {
		ret = -errno;
		goto out_sig;
	}

	ret = read(fd, data_item->sig, data_item->sig_len);

	close(fd);

	if (ret != data_item->sig_len) {
		ret = -EIO;
		goto out_sig;
	}

	ret = 0;
out_sig:
	unlink(path);
out:
	unlink(data_template);
	return ret;
}

static int populate_data_item_mod(struct data *data_item)
{
	char mod_path[PATH_MAX], *mod_path_ptr;
	struct stat st;
	void *mod;
	FILE *fp;
	struct module_signature ms;
	int ret, fd, modlen, marker_len, sig_len;

	data_item->data_len = 0;

	if (stat("/lib/modules", &st) == -1)
		return 0;

	/* Requires CONFIG_TCP_CONG_BIC=m. */
	fp = popen("find /lib/modules/$(uname -r) -name tcp_bic.ko", "r");
	if (!fp)
		return 0;

	mod_path_ptr = fgets(mod_path, sizeof(mod_path), fp);
	pclose(fp);

	if (!mod_path_ptr)
		return 0;

	mod_path_ptr = strchr(mod_path, '\n');
	if (!mod_path_ptr)
		return 0;

	*mod_path_ptr = '\0';

	if (stat(mod_path, &st) == -1)
		return 0;

	modlen = st.st_size;
	marker_len = sizeof(MODULE_SIG_STRING) - 1;

	fd = open(mod_path, O_RDONLY);
	if (fd == -1)
		return -errno;

	mod = mmap(NULL, st.st_size, PROT_READ, MAP_PRIVATE, fd, 0);

	close(fd);

	if (mod == MAP_FAILED)
		return -errno;

	if (strncmp(mod + modlen - marker_len, MODULE_SIG_STRING, marker_len)) {
		ret = -EINVAL;
		goto out;
	}

	modlen -= marker_len;

	memcpy(&ms, mod + (modlen - sizeof(ms)), sizeof(ms));

	sig_len = __be32_to_cpu(ms.sig_len);
	modlen -= sig_len + sizeof(ms);

	if (modlen > sizeof(data_item->data)) {
		ret = -E2BIG;
		goto out;
	}

	memcpy(data_item->data, mod, modlen);
	data_item->data_len = modlen;

	if (sig_len > sizeof(data_item->sig)) {
		ret = -E2BIG;
		goto out;
	}

	memcpy(data_item->sig, mod + modlen, sig_len);
	data_item->sig_len = sig_len;
	ret = 0;
out:
	munmap(mod, st.st_size);
	return ret;
}

static void test_verify_pkcs7_sig_from_map(void)
{
	libbpf_print_fn_t old_print_cb;
	char tmp_dir_template[] = "/tmp/verify_sigXXXXXX";
	char *tmp_dir;
	struct test_verify_pkcs7_sig *skel = NULL;
	struct bpf_map *map;
	struct data data = {};
	int ret, zero = 0;

	/* Trigger creation of session keyring. */
	syscall(__NR_request_key, "keyring", "_uid.0", NULL,
		KEY_SPEC_SESSION_KEYRING);

	tmp_dir = mkdtemp(tmp_dir_template);
	if (!ASSERT_OK_PTR(tmp_dir, "mkdtemp"))
		return;

	ret = _run_setup_process(tmp_dir, "setup");
	if (!ASSERT_OK(ret, "_run_setup_process"))
		goto close_prog;

	skel = test_verify_pkcs7_sig__open();
	if (!ASSERT_OK_PTR(skel, "test_verify_pkcs7_sig__open"))
		goto close_prog;

	old_print_cb = libbpf_set_print(libbpf_print_cb);
	ret = test_verify_pkcs7_sig__load(skel);
	libbpf_set_print(old_print_cb);

	if (ret < 0 && kfunc_not_supported) {
		printf(
		  "%s:SKIP:bpf_verify_pkcs7_signature() kfunc not supported\n",
		  __func__);
		test__skip();
		goto close_prog;
	}

	if (!ASSERT_OK(ret, "test_verify_pkcs7_sig__load"))
		goto close_prog;

	ret = test_verify_pkcs7_sig__attach(skel);
	if (!ASSERT_OK(ret, "test_verify_pkcs7_sig__attach"))
		goto close_prog;

	map = bpf_object__find_map_by_name(skel->obj, "data_input");
	if (!ASSERT_OK_PTR(map, "data_input not found"))
		goto close_prog;

	skel->bss->monitored_pid = getpid();

	/* Test without data and signature. */
	skel->bss->user_keyring_serial = KEY_SPEC_SESSION_KEYRING;

	ret = bpf_map_update_elem(bpf_map__fd(map), &zero, &data, BPF_ANY);
	if (!ASSERT_LT(ret, 0, "bpf_map_update_elem data_input"))
		goto close_prog;

	/* Test successful signature verification with session keyring. */
	ret = populate_data_item_str(tmp_dir, &data);
	if (!ASSERT_OK(ret, "populate_data_item_str"))
		goto close_prog;

	ret = bpf_map_update_elem(bpf_map__fd(map), &zero, &data, BPF_ANY);
	if (!ASSERT_OK(ret, "bpf_map_update_elem data_input"))
		goto close_prog;

	/* Test successful signature verification with testing keyring. */
	skel->bss->user_keyring_serial = syscall(__NR_request_key, "keyring",
						 "ebpf_testing_keyring", NULL,
						 KEY_SPEC_SESSION_KEYRING);

	ret = bpf_map_update_elem(bpf_map__fd(map), &zero, &data, BPF_ANY);
	if (!ASSERT_OK(ret, "bpf_map_update_elem data_input"))
		goto close_prog;

	/*
	 * Ensure key_task_permission() is called and rejects the keyring
	 * (no Search permission).
	 */
	syscall(__NR_keyctl, KEYCTL_SETPERM, skel->bss->user_keyring_serial,
		0x37373737);

	ret = bpf_map_update_elem(bpf_map__fd(map), &zero, &data, BPF_ANY);
	if (!ASSERT_LT(ret, 0, "bpf_map_update_elem data_input"))
		goto close_prog;

	syscall(__NR_keyctl, KEYCTL_SETPERM, skel->bss->user_keyring_serial,
		0x3f3f3f3f);

	/*
	 * Ensure key_validate() is called and rejects the keyring (key expired)
	 */
	syscall(__NR_keyctl, KEYCTL_SET_TIMEOUT,
		skel->bss->user_keyring_serial, 1);
	sleep(1);

	ret = bpf_map_update_elem(bpf_map__fd(map), &zero, &data, BPF_ANY);
	if (!ASSERT_LT(ret, 0, "bpf_map_update_elem data_input"))
		goto close_prog;

	skel->bss->user_keyring_serial = KEY_SPEC_SESSION_KEYRING;

	/* Test with corrupted data (signature verification should fail). */
	data.data[0] = 'a';
	ret = bpf_map_update_elem(bpf_map__fd(map), &zero, &data, BPF_ANY);
	if (!ASSERT_LT(ret, 0, "bpf_map_update_elem data_input"))
		goto close_prog;

	ret = populate_data_item_mod(&data);
	if (!ASSERT_OK(ret, "populate_data_item_mod"))
		goto close_prog;

	/* Test signature verification with system keyrings. */
	if (data.data_len) {
		skel->bss->user_keyring_serial = 0;
		skel->bss->system_keyring_id = 0;

		ret = bpf_map_update_elem(bpf_map__fd(map), &zero, &data,
					  BPF_ANY);
		if (!ASSERT_OK(ret, "bpf_map_update_elem data_input"))
			goto close_prog;

		skel->bss->system_keyring_id = VERIFY_USE_SECONDARY_KEYRING;

		ret = bpf_map_update_elem(bpf_map__fd(map), &zero, &data,
					  BPF_ANY);
		if (!ASSERT_OK(ret, "bpf_map_update_elem data_input"))
			goto close_prog;

		skel->bss->system_keyring_id = VERIFY_USE_PLATFORM_KEYRING;

		ret = bpf_map_update_elem(bpf_map__fd(map), &zero, &data,
					  BPF_ANY);
		ASSERT_LT(ret, 0, "bpf_map_update_elem data_input");
	}

close_prog:
	_run_setup_process(tmp_dir, "cleanup");

	if (!skel)
		return;

	skel->bss->monitored_pid = 0;
	test_verify_pkcs7_sig__destroy(skel);
}

static int get_signature_size(const char *sig_path)
{
	struct stat st;

	if (stat(sig_path, &st) == -1)
		return -1;

	return st.st_size;
}

static int add_signature_to_xattr(const char *data_path, const char *sig_path)
{
	char sig[MAX_SIG_SIZE] = {0};
	int fd, size, ret;

	if (sig_path) {
		fd = open(sig_path, O_RDONLY);
		if (fd < 0)
			return -1;

		size = read(fd, sig, MAX_SIG_SIZE);
		close(fd);
		if (size <= 0)
			return -1;
	} else {
		/* no sig_path, just write 32 bytes of zeros */
		size = 32;
	}
	ret = setxattr(data_path, "user.sig", sig, size, 0);
	if (!ASSERT_OK(ret, "setxattr"))
		return -1;

	return 0;
}

static int test_open_file(struct test_sig_in_xattr *skel, char *data_path,
			  pid_t pid, bool should_success, char *name)
{
	int ret;

	skel->bss->monitored_pid = pid;
	ret = open(data_path, O_RDONLY);
	close(ret);
	skel->bss->monitored_pid = 0;

	if (should_success) {
		if (!ASSERT_GE(ret, 0, name))
			return -1;
	} else {
		if (!ASSERT_LT(ret, 0, name))
			return -1;
	}
	return 0;
}

static void test_pkcs7_sig_fsverity(void)
{
	char data_path[PATH_MAX];
	char sig_path[PATH_MAX];
	char tmp_dir_template[] = "/tmp/verify_sigXXXXXX";
	char *tmp_dir;
	struct test_sig_in_xattr *skel = NULL;
	pid_t pid;
	int ret;

	tmp_dir = mkdtemp(tmp_dir_template);
	if (!ASSERT_OK_PTR(tmp_dir, "mkdtemp"))
		return;

	snprintf(data_path, PATH_MAX, "%s/data-file", tmp_dir);
	snprintf(sig_path, PATH_MAX, "%s/sig-file", tmp_dir);

	ret = _run_setup_process(tmp_dir, "setup");
	if (!ASSERT_OK(ret, "_run_setup_process"))
		goto out;

	ret = _run_setup_process(tmp_dir, "fsverity-create-sign");

	if (ret) {
		printf("%s: SKIP: fsverity [sign|enable] doesn't work.\n"
		       "To run this test, try enable CONFIG_FS_VERITY and enable FSVerity for the filesystem.\n",
		       __func__);
		test__skip();
		goto out;
	}

	skel = test_sig_in_xattr__open();
	if (!ASSERT_OK_PTR(skel, "test_sig_in_xattr__open"))
		goto out;
	ret = get_signature_size(sig_path);
	if (!ASSERT_GT(ret, 0, "get_signature_size"))
		goto out;
	skel->bss->sig_size = ret;
	skel->bss->user_keyring_serial = syscall(__NR_request_key, "keyring",
						 "ebpf_testing_keyring", NULL,
						 KEY_SPEC_SESSION_KEYRING);
	memcpy(skel->bss->digest, "FSVerity", 8);

	ret = test_sig_in_xattr__load(skel);
	if (!ASSERT_OK(ret, "test_sig_in_xattr__load"))
		goto out;

	ret = test_sig_in_xattr__attach(skel);
	if (!ASSERT_OK(ret, "test_sig_in_xattr__attach"))
		goto out;

	pid = getpid();

	/* Case 1: fsverity is not enabled, open should succeed */
	if (test_open_file(skel, data_path, pid, true, "open_1"))
		goto out;

	/* Case 2: fsverity is enabled, xattr is missing, open should
	 * fail
	 */
	ret = _run_setup_process(tmp_dir, "fsverity-enable");
	if (!ASSERT_OK(ret, "fsverity-enable"))
		goto out;
	if (test_open_file(skel, data_path, pid, false, "open_2"))
		goto out;

	/* Case 3: fsverity is enabled, xattr has valid signature, open
	 * should succeed
	 */
	ret = add_signature_to_xattr(data_path, sig_path);
	if (!ASSERT_OK(ret, "add_signature_to_xattr_1"))
		goto out;

	if (test_open_file(skel, data_path, pid, true, "open_3"))
		goto out;

	/* Case 4: fsverity is enabled, xattr has invalid signature, open
	 * should fail
	 */
	ret = add_signature_to_xattr(data_path, NULL);
	if (!ASSERT_OK(ret, "add_signature_to_xattr_2"))
		goto out;
	test_open_file(skel, data_path, pid, false, "open_4");

out:
	_run_setup_process(tmp_dir, "cleanup");
	if (!skel)
		return;

	skel->bss->monitored_pid = 0;
	test_sig_in_xattr__destroy(skel);
}

void test_verify_pkcs7_sig(void)
{
	if (test__start_subtest("pkcs7_sig_from_map"))
		test_verify_pkcs7_sig_from_map();
	if (test__start_subtest("pkcs7_sig_fsverity"))
		test_pkcs7_sig_fsverity();
}
