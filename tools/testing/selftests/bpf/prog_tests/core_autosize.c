// SPDX-License-Identifier: GPL-2.0
/* Copyright (c) 2020 Facebook */

#include <test_progs.h>
#include <bpf/btf.h>

/* real layout and sizes according to test's (32-bit) BTF
 * needs to be defined before skeleton is included */
struct test_struct___real {
	unsigned int ptr; /* can't use `void *`, it is always 8 byte in BPF target */
	unsigned int val2;
	unsigned long long val1;
	unsigned short val3;
	unsigned char val4;
	unsigned char _pad;
};

#include "test_core_autosize.skel.h"

static int duration = 0;

static struct {
	unsigned long long ptr_samesized;
	unsigned long long val1_samesized;
	unsigned long long val2_samesized;
	unsigned long long val3_samesized;
	unsigned long long val4_samesized;
	struct test_struct___real output_samesized;

	unsigned long long ptr_downsized;
	unsigned long long val1_downsized;
	unsigned long long val2_downsized;
	unsigned long long val3_downsized;
	unsigned long long val4_downsized;
	struct test_struct___real output_downsized;

	unsigned long long ptr_probed;
	unsigned long long val1_probed;
	unsigned long long val2_probed;
	unsigned long long val3_probed;
	unsigned long long val4_probed;

	unsigned long long ptr_signed;
	unsigned long long val1_signed;
	unsigned long long val2_signed;
	unsigned long long val3_signed;
	unsigned long long val4_signed;
	struct test_struct___real output_signed;
} out;

void test_core_autosize(void)
{
	char btf_file[] = "/tmp/core_autosize.btf.XXXXXX";
	int err, fd = -1, zero = 0;
	int char_id, short_id, int_id, long_long_id, void_ptr_id, id;
	DECLARE_LIBBPF_OPTS(bpf_object_open_opts, open_opts);
	struct test_core_autosize* skel = NULL;
	struct bpf_program *prog;
	struct bpf_map *bss_map;
	struct btf *btf = NULL;
	size_t written;
	const void *raw_data;
	__u32 raw_sz;
	FILE *f = NULL;

	btf = btf__new_empty();
	if (!ASSERT_OK_PTR(btf, "empty_btf"))
		return;
	/* Emit the following struct with 32-bit pointer size:
	 *
	 * struct test_struct {
	 *     void *ptr;
	 *     unsigned long val2;
	 *     unsigned long long val1;
	 *     unsigned short val3;
	 *     unsigned char val4;
	 *     char: 8;
	 * };
	 *
	 * This struct is going to be used as the "kernel BTF" for this test.
	 * It's equivalent memory-layout-wise to test_struct__real above.
	 */

	/* force 32-bit pointer size */
	btf__set_pointer_size(btf, 4);

	char_id = btf__add_int(btf, "unsigned char", 1, 0);
	ASSERT_EQ(char_id, 1, "char_id");
	short_id = btf__add_int(btf, "unsigned short", 2, 0);
	ASSERT_EQ(short_id, 2, "short_id");
	/* "long unsigned int" of 4 byte size tells BTF that sizeof(void *) == 4 */
	int_id = btf__add_int(btf, "long unsigned int", 4, 0);
	ASSERT_EQ(int_id, 3, "int_id");
	long_long_id = btf__add_int(btf, "unsigned long long", 8, 0);
	ASSERT_EQ(long_long_id, 4, "long_long_id");
	void_ptr_id = btf__add_ptr(btf, 0);
	ASSERT_EQ(void_ptr_id, 5, "void_ptr_id");

	id = btf__add_struct(btf, "test_struct", 20 /* bytes */);
	ASSERT_EQ(id, 6, "struct_id");
	err = btf__add_field(btf, "ptr", void_ptr_id, 0, 0);
	err = err ?: btf__add_field(btf, "val2", int_id, 32, 0);
	err = err ?: btf__add_field(btf, "val1", long_long_id, 64, 0);
	err = err ?: btf__add_field(btf, "val3", short_id, 128, 0);
	err = err ?: btf__add_field(btf, "val4", char_id, 144, 0);
	ASSERT_OK(err, "struct_fields");

	fd = mkstemp(btf_file);
	if (CHECK(fd < 0, "btf_tmp", "failed to create file: %d\n", fd))
		goto cleanup;
	f = fdopen(fd, "w");
	if (!ASSERT_OK_PTR(f, "btf_fdopen"))
		goto cleanup;

	raw_data = btf__raw_data(btf, &raw_sz);
	if (!ASSERT_OK_PTR(raw_data, "raw_data"))
		goto cleanup;
	written = fwrite(raw_data, 1, raw_sz, f);
	if (CHECK(written != raw_sz, "btf_write", "written: %zu, errno: %d\n", written, errno))
		goto cleanup;
	fflush(f);
	fclose(f);
	f = NULL;
	close(fd);
	fd = -1;

	/* open and load BPF program with custom BTF as the kernel BTF */
	open_opts.btf_custom_path = btf_file;
	skel = test_core_autosize__open_opts(&open_opts);
	if (!ASSERT_OK_PTR(skel, "skel_open"))
		goto cleanup;

	/* disable handle_signed() for now */
	prog = bpf_object__find_program_by_name(skel->obj, "handle_signed");
	if (!ASSERT_OK_PTR(prog, "prog_find"))
		goto cleanup;
	bpf_program__set_autoload(prog, false);

	err = bpf_object__load(skel->obj);
	if (!ASSERT_OK(err, "prog_load"))
		goto cleanup;

	prog = bpf_object__find_program_by_name(skel->obj, "handle_samesize");
	if (!ASSERT_OK_PTR(prog, "prog_find"))
		goto cleanup;
	skel->links.handle_samesize = bpf_program__attach(prog);
	if (!ASSERT_OK_PTR(skel->links.handle_samesize, "prog_attach"))
		goto cleanup;

	prog = bpf_object__find_program_by_name(skel->obj, "handle_downsize");
	if (!ASSERT_OK_PTR(prog, "prog_find"))
		goto cleanup;
	skel->links.handle_downsize = bpf_program__attach(prog);
	if (!ASSERT_OK_PTR(skel->links.handle_downsize, "prog_attach"))
		goto cleanup;

	prog = bpf_object__find_program_by_name(skel->obj, "handle_probed");
	if (!ASSERT_OK_PTR(prog, "prog_find"))
		goto cleanup;
	skel->links.handle_probed = bpf_program__attach(prog);
	if (!ASSERT_OK_PTR(skel->links.handle_probed, "prog_attach"))
		goto cleanup;

	usleep(1);

	bss_map = bpf_object__find_map_by_name(skel->obj, ".bss");
	if (!ASSERT_OK_PTR(bss_map, "bss_map_find"))
		goto cleanup;

	err = bpf_map__lookup_elem(bss_map, &zero, sizeof(zero), &out, sizeof(out), 0);
	if (!ASSERT_OK(err, "bss_lookup"))
		goto cleanup;

	ASSERT_EQ(out.ptr_samesized, 0x01020304, "ptr_samesized");
	ASSERT_EQ(out.val1_samesized, 0x1020304050607080, "val1_samesized");
	ASSERT_EQ(out.val2_samesized, 0x0a0b0c0d, "val2_samesized");
	ASSERT_EQ(out.val3_samesized, 0xfeed, "val3_samesized");
	ASSERT_EQ(out.val4_samesized, 0xb9, "val4_samesized");
	ASSERT_EQ(out.output_samesized.ptr, 0x01020304, "ptr_samesized");
	ASSERT_EQ(out.output_samesized.val1, 0x1020304050607080, "val1_samesized");
	ASSERT_EQ(out.output_samesized.val2, 0x0a0b0c0d, "val2_samesized");
	ASSERT_EQ(out.output_samesized.val3, 0xfeed, "val3_samesized");
	ASSERT_EQ(out.output_samesized.val4, 0xb9, "val4_samesized");

	ASSERT_EQ(out.ptr_downsized, 0x01020304, "ptr_downsized");
	ASSERT_EQ(out.val1_downsized, 0x1020304050607080, "val1_downsized");
	ASSERT_EQ(out.val2_downsized, 0x0a0b0c0d, "val2_downsized");
	ASSERT_EQ(out.val3_downsized, 0xfeed, "val3_downsized");
	ASSERT_EQ(out.val4_downsized, 0xb9, "val4_downsized");
	ASSERT_EQ(out.output_downsized.ptr, 0x01020304, "ptr_downsized");
	ASSERT_EQ(out.output_downsized.val1, 0x1020304050607080, "val1_downsized");
	ASSERT_EQ(out.output_downsized.val2, 0x0a0b0c0d, "val2_downsized");
	ASSERT_EQ(out.output_downsized.val3, 0xfeed, "val3_downsized");
	ASSERT_EQ(out.output_downsized.val4, 0xb9, "val4_downsized");

	ASSERT_EQ(out.ptr_probed, 0x01020304, "ptr_probed");
	ASSERT_EQ(out.val1_probed, 0x1020304050607080, "val1_probed");
	ASSERT_EQ(out.val2_probed, 0x0a0b0c0d, "val2_probed");
	ASSERT_EQ(out.val3_probed, 0xfeed, "val3_probed");
	ASSERT_EQ(out.val4_probed, 0xb9, "val4_probed");

	test_core_autosize__destroy(skel);
	skel = NULL;

	/* now re-load with handle_signed() enabled, it should fail loading */
	open_opts.btf_custom_path = btf_file;
	skel = test_core_autosize__open_opts(&open_opts);
	if (!ASSERT_OK_PTR(skel, "skel_open"))
		goto cleanup;

	err = test_core_autosize__load(skel);
	if (!ASSERT_ERR(err, "skel_load"))
		goto cleanup;

cleanup:
	if (f)
		fclose(f);
	if (fd >= 0)
		close(fd);
	remove(btf_file);
	btf__free(btf);
	test_core_autosize__destroy(skel);
}
