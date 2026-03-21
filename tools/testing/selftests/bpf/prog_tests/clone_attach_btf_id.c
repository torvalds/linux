// SPDX-License-Identifier: GPL-2.0
/* Copyright (c) 2025 Meta */
#include <test_progs.h>
#include "clone_attach_btf_id.skel.h"

/*
 * Test that bpf_program__clone() respects caller-provided attach_btf_id
 * override via bpf_prog_load_opts.
 *
 * The BPF program has SEC("fentry/bpf_fentry_test1"). Clone it twice
 * from the same prepared object: first with no opts (callback resolves
 * attach_btf_id from sec_name), then with attach_btf_id overridden to
 * bpf_fentry_test2. Verify each loaded program's attach_btf_id via
 * bpf_prog_get_info_by_fd().
 */

static int get_prog_attach_btf_id(int prog_fd)
{
	struct bpf_prog_info info = {};
	__u32 info_len = sizeof(info);
	int err;

	err = bpf_prog_get_info_by_fd(prog_fd, &info, &info_len);
	if (err)
		return err;
	return info.attach_btf_id;
}

void test_clone_attach_btf_id(void)
{
	struct clone_attach_btf_id *skel;
	int fd1 = -1, fd2 = -1, err;
	int btf_id_test1, btf_id_test2;

	btf_id_test1 = libbpf_find_vmlinux_btf_id("bpf_fentry_test1", BPF_TRACE_FENTRY);
	if (!ASSERT_GT(btf_id_test1, 0, "find_btf_id_test1"))
		return;

	btf_id_test2 = libbpf_find_vmlinux_btf_id("bpf_fentry_test2", BPF_TRACE_FENTRY);
	if (!ASSERT_GT(btf_id_test2, 0, "find_btf_id_test2"))
		return;

	skel = clone_attach_btf_id__open();
	if (!ASSERT_OK_PTR(skel, "skel_open"))
		return;

	err = bpf_object__prepare(skel->obj);
	if (!ASSERT_OK(err, "obj_prepare"))
		goto out;

	/* Clone with no opts — callback resolves BTF from sec_name */
	fd1 = bpf_program__clone(skel->progs.fentry_handler, NULL);
	if (!ASSERT_GE(fd1, 0, "clone_default"))
		goto out;
	ASSERT_EQ(get_prog_attach_btf_id(fd1), btf_id_test1,
		  "attach_btf_id_default");

	/*
	 * Clone with attach_btf_id override pointing to a different
	 * function. The BPF program never accesses arguments, so the
	 * load succeeds regardless of signature mismatch.
	 */
	LIBBPF_OPTS(bpf_prog_load_opts, opts,
		    .attach_btf_id = btf_id_test2,
	);
	fd2 = bpf_program__clone(skel->progs.fentry_handler, &opts);
	if (!ASSERT_GE(fd2, 0, "clone_override"))
		goto out;
	ASSERT_EQ(get_prog_attach_btf_id(fd2), btf_id_test2,
		  "attach_btf_id_override");

out:
	if (fd1 >= 0)
		close(fd1);
	if (fd2 >= 0)
		close(fd2);
	clone_attach_btf_id__destroy(skel);
}
