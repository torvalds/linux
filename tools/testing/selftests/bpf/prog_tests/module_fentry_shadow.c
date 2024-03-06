// SPDX-License-Identifier: GPL-2.0
/* Copyright (c) 2022 Red Hat */
#include <test_progs.h>
#include <bpf/btf.h>
#include "bpf/libbpf_internal.h"
#include "cgroup_helpers.h"

static const char *module_name = "bpf_testmod";
static const char *symbol_name = "bpf_fentry_shadow_test";

static int get_bpf_testmod_btf_fd(void)
{
	struct bpf_btf_info info;
	char name[64];
	__u32 id = 0, len;
	int err, fd;

	while (true) {
		err = bpf_btf_get_next_id(id, &id);
		if (err) {
			log_err("failed to iterate BTF objects");
			return err;
		}

		fd = bpf_btf_get_fd_by_id(id);
		if (fd < 0) {
			if (errno == ENOENT)
				continue; /* expected race: BTF was unloaded */
			err = -errno;
			log_err("failed to get FD for BTF object #%d", id);
			return err;
		}

		len = sizeof(info);
		memset(&info, 0, sizeof(info));
		info.name = ptr_to_u64(name);
		info.name_len = sizeof(name);

		err = bpf_obj_get_info_by_fd(fd, &info, &len);
		if (err) {
			err = -errno;
			log_err("failed to get info for BTF object #%d", id);
			close(fd);
			return err;
		}

		if (strcmp(name, module_name) == 0)
			return fd;

		close(fd);
	}
	return -ENOENT;
}

void test_module_fentry_shadow(void)
{
	struct btf *vmlinux_btf = NULL, *mod_btf = NULL;
	int err, i;
	int btf_fd[2] = {};
	int prog_fd[2] = {};
	int link_fd[2] = {};
	__s32 btf_id[2] = {};

	if (!env.has_testmod) {
		test__skip();
		return;
	}

	LIBBPF_OPTS(bpf_prog_load_opts, load_opts,
		.expected_attach_type = BPF_TRACE_FENTRY,
	);

	const struct bpf_insn trace_program[] = {
		BPF_MOV64_IMM(BPF_REG_0, 0),
		BPF_EXIT_INSN(),
	};

	vmlinux_btf = btf__load_vmlinux_btf();
	if (!ASSERT_OK_PTR(vmlinux_btf, "load_vmlinux_btf"))
		return;

	btf_fd[1] = get_bpf_testmod_btf_fd();
	if (!ASSERT_GE(btf_fd[1], 0, "get_bpf_testmod_btf_fd"))
		goto out;

	mod_btf = btf_get_from_fd(btf_fd[1], vmlinux_btf);
	if (!ASSERT_OK_PTR(mod_btf, "btf_get_from_fd"))
		goto out;

	btf_id[0] = btf__find_by_name_kind(vmlinux_btf, symbol_name, BTF_KIND_FUNC);
	if (!ASSERT_GT(btf_id[0], 0, "btf_find_by_name"))
		goto out;

	btf_id[1] = btf__find_by_name_kind(mod_btf, symbol_name, BTF_KIND_FUNC);
	if (!ASSERT_GT(btf_id[1], 0, "btf_find_by_name"))
		goto out;

	for (i = 0; i < 2; i++) {
		load_opts.attach_btf_id = btf_id[i];
		load_opts.attach_btf_obj_fd = btf_fd[i];
		prog_fd[i] = bpf_prog_load(BPF_PROG_TYPE_TRACING, NULL, "GPL",
					   trace_program,
					   sizeof(trace_program) / sizeof(struct bpf_insn),
					   &load_opts);
		if (!ASSERT_GE(prog_fd[i], 0, "bpf_prog_load"))
			goto out;

		/* If the verifier incorrectly resolves addresses of the
		 * shadowed functions and uses the same address for both the
		 * vmlinux and the bpf_testmod functions, this will fail on
		 * attempting to create two trampolines for the same address,
		 * which is forbidden.
		 */
		link_fd[i] = bpf_link_create(prog_fd[i], 0, BPF_TRACE_FENTRY, NULL);
		if (!ASSERT_GE(link_fd[i], 0, "bpf_link_create"))
			goto out;
	}

	err = bpf_prog_test_run_opts(prog_fd[0], NULL);
	ASSERT_OK(err, "running test");

out:
	btf__free(vmlinux_btf);
	btf__free(mod_btf);
	for (i = 0; i < 2; i++) {
		if (btf_fd[i])
			close(btf_fd[i]);
		if (prog_fd[i] > 0)
			close(prog_fd[i]);
		if (link_fd[i] > 0)
			close(link_fd[i]);
	}
}
