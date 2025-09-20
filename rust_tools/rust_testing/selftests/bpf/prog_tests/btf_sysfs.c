// SPDX-License-Identifier: GPL-2.0 OR BSD-3-Clause
/* Copyright (c) 2025 Isovalent */

#include <test_progs.h>
#include <bpf/btf.h>
#include <sys/stat.h>
#include <sys/mman.h>
#include <fcntl.h>
#include <unistd.h>

static void test_btf_mmap_sysfs(const char *path, struct btf *base)
{
	struct stat st;
	__u64 btf_size, end;
	void *raw_data = NULL;
	int fd = -1;
	long page_size;
	struct btf *btf = NULL;

	page_size = sysconf(_SC_PAGESIZE);
	if (!ASSERT_GE(page_size, 0, "get_page_size"))
		goto cleanup;

	if (!ASSERT_OK(stat(path, &st), "stat_btf"))
		goto cleanup;

	btf_size = st.st_size;
	end = (btf_size + page_size - 1) / page_size * page_size;

	fd = open(path, O_RDONLY);
	if (!ASSERT_GE(fd, 0, "open_btf"))
		goto cleanup;

	raw_data = mmap(NULL, btf_size, PROT_READ | PROT_WRITE, MAP_PRIVATE, fd, 0);
	if (!ASSERT_EQ(raw_data, MAP_FAILED, "mmap_btf_writable"))
		goto cleanup;

	raw_data = mmap(NULL, btf_size, PROT_READ, MAP_SHARED, fd, 0);
	if (!ASSERT_EQ(raw_data, MAP_FAILED, "mmap_btf_shared"))
		goto cleanup;

	raw_data = mmap(NULL, end + 1, PROT_READ, MAP_PRIVATE, fd, 0);
	if (!ASSERT_EQ(raw_data, MAP_FAILED, "mmap_btf_invalid_size"))
		goto cleanup;

	raw_data = mmap(NULL, end, PROT_READ, MAP_PRIVATE, fd, 0);
	if (!ASSERT_OK_PTR(raw_data, "mmap_btf"))
		goto cleanup;

	if (!ASSERT_EQ(mprotect(raw_data, btf_size, PROT_READ | PROT_WRITE), -1,
	    "mprotect_writable"))
		goto cleanup;

	if (!ASSERT_EQ(mprotect(raw_data, btf_size, PROT_READ | PROT_EXEC), -1,
	    "mprotect_executable"))
		goto cleanup;

	/* Check padding is zeroed */
	for (int i = btf_size; i < end; i++) {
		if (((__u8 *)raw_data)[i] != 0) {
			PRINT_FAIL("tail of BTF is not zero at page offset %d\n", i);
			goto cleanup;
		}
	}

	btf = btf__new_split(raw_data, btf_size, base);
	if (!ASSERT_OK_PTR(btf, "parse_btf"))
		goto cleanup;

cleanup:
	btf__free(btf);
	if (raw_data && raw_data != MAP_FAILED)
		munmap(raw_data, btf_size);
	if (fd >= 0)
		close(fd);
}

void test_btf_sysfs(void)
{
	test_btf_mmap_sysfs("/sys/kernel/btf/vmlinux", NULL);
}
