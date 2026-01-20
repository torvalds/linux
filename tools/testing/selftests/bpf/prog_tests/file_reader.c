// SPDX-License-Identifier: GPL-2.0
/* Copyright (c) 2025 Meta Platforms, Inc. and affiliates. */

#include <test_progs.h>
#include <network_helpers.h>
#include "file_reader.skel.h"
#include "file_reader_fail.skel.h"
#include <dlfcn.h>
#include <sys/mman.h>

const char *user_ptr = "hello world";
char file_contents[256000];

void *get_executable_base_addr(void)
{
	Dl_info info;

	if (!dladdr((void *)&get_executable_base_addr, &info)) {
		fprintf(stderr, "dladdr failed\n");
		return NULL;
	}

	return info.dli_fbase;
}

static int initialize_file_contents(void)
{
	int fd, page_sz = sysconf(_SC_PAGESIZE);
	ssize_t n = 0, cur, off;
	void *addr;

	fd = open("/proc/self/exe", O_RDONLY);
	if (!ASSERT_OK_FD(fd, "Open /proc/self/exe\n"))
		return 1;

	do {
		cur = read(fd, file_contents + n, sizeof(file_contents) - n);
		if (!ASSERT_GT(cur, 0, "read success"))
			break;
		n += cur;
	} while (n < sizeof(file_contents));

	close(fd);

	if (!ASSERT_EQ(n, sizeof(file_contents), "Read /proc/self/exe\n"))
		return 1;

	addr = get_executable_base_addr();
	if (!ASSERT_NEQ(addr, NULL, "get executable address"))
		return 1;

	/* page-align base file address */
	addr = (void *)((unsigned long)addr & ~(page_sz - 1));

	/*
	 * Page out range 0..512K, use 0..256K for positive tests and
	 * 256K..512K for negative tests expecting page faults
	 */
	for (off = 0; off < sizeof(file_contents) * 2; off += page_sz) {
		if (!ASSERT_OK(madvise(addr + off, page_sz, MADV_PAGEOUT),
			       "madvise pageout"))
			return errno;
	}

	return 0;
}

static void run_test(const char *prog_name)
{
	struct file_reader *skel;
	struct bpf_program *prog;
	int err, fd;

	err = initialize_file_contents();
	if (!ASSERT_OK(err, "initialize file contents"))
		return;

	skel = file_reader__open();
	if (!ASSERT_OK_PTR(skel, "file_reader__open"))
		return;

	bpf_object__for_each_program(prog, skel->obj) {
		bpf_program__set_autoload(prog, strcmp(bpf_program__name(prog), prog_name) == 0);
	}

	memcpy(skel->bss->user_buf, file_contents, sizeof(file_contents));
	skel->bss->pid = getpid();

	err = file_reader__load(skel);
	if (!ASSERT_OK(err, "file_reader__load"))
		goto cleanup;

	err = file_reader__attach(skel);
	if (!ASSERT_OK(err, "file_reader__attach"))
		goto cleanup;

	fd = open("/proc/self/exe", O_RDONLY);
	if (fd >= 0)
		close(fd);

	ASSERT_EQ(skel->bss->err, 0, "err");
	ASSERT_EQ(skel->bss->run_success, 1, "run_success");
cleanup:
	file_reader__destroy(skel);
}

void test_file_reader(void)
{
	if (test__start_subtest("on_open_expect_fault"))
		run_test("on_open_expect_fault");

	if (test__start_subtest("on_open_validate_file_read"))
		run_test("on_open_validate_file_read");

	if (test__start_subtest("negative"))
		RUN_TESTS(file_reader_fail);
}
