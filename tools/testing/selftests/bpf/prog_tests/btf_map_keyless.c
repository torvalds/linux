// SPDX-License-Identifier: GPL-2.0
#include <test_progs.h>
#include <bpf/btf.h>

/*
 * A hash map with a key-less BTF (btf_key_type_id == 0) used to be accepted
 * and then NULL-deref in btf_type_show() when dumped through bpffs. A fixed
 * kernel rejects it at creation; verify that rejection, with a keyed positive
 * control so the -EINVAL is about the missing key type and not some unrelated
 * failure.
 */
static void check_keyless(int map_type, __u32 map_flags, int btf_fd, int val_id)
{
	LIBBPF_OPTS(bpf_map_create_opts, opts);
	int map_fd;

	opts.map_flags = map_flags;
	opts.btf_fd = btf_fd;
	opts.btf_value_type_id = val_id;

	/* Positive control: the same map with a real key type is accepted. */
	opts.btf_key_type_id = val_id;
	map_fd = bpf_map_create(map_type, "keyed_map", 4, 4, 8, &opts);
	if (!ASSERT_GE(map_fd, 0, "keyed create is accepted"))
		return;
	close(map_fd);

	/* A key-less BTF must be rejected. */
	opts.btf_key_type_id = 0;
	map_fd = bpf_map_create(map_type, "keyless_map", 4, 4, 8, &opts);
	ASSERT_EQ(map_fd, -EINVAL, "key-less create is rejected");
	if (map_fd >= 0)
		close(map_fd);
}

void test_btf_map_keyless(void)
{
	int btf_fd, val_id;
	struct btf *btf;

	btf = btf__new_empty();
	if (!ASSERT_OK_PTR(btf, "btf__new_empty"))
		return;

	val_id = btf__add_int(btf, "int", 4, BTF_INT_SIGNED);
	if (!ASSERT_GT(val_id, 0, "btf__add_int"))
		goto out;

	if (!ASSERT_OK(btf__load_into_kernel(btf), "btf__load_into_kernel"))
		goto out;
	btf_fd = btf__fd(btf);

	if (test__start_subtest("hash"))
		check_keyless(BPF_MAP_TYPE_HASH, 0, btf_fd, val_id);
	if (test__start_subtest("rhash"))
		check_keyless(BPF_MAP_TYPE_RHASH, BPF_F_NO_PREALLOC, btf_fd, val_id);
out:
	btf__free(btf);
}
