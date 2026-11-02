// SPDX-License-Identifier: GPL-2.0

#include <test_progs.h>
#include <bpf/btf.h>
#include "bpftool_helpers.h"

#define FUNC_NAME "write_fmt<scx_simple::BpfStream>"
#define KSYM_NAME "write_fmt_scx_simple__BpfStream_"

/* The name of a function of Rust is a part of the name of the program in kallsyms */
static void test_func_name(void)
{
	struct bpf_insn insns[] = {
		BPF_MOV64_IMM(BPF_REG_0, 0),
		BPF_EXIT_INSN(),
	};
	LIBBPF_OPTS(bpf_prog_load_opts, opts);
	int int_id, proto_id, prog_fd = -1, i;
	struct bpf_func_info func_info = {};
	struct bpf_prog_info info = {};
	unsigned long long addr;
	__u32 len = sizeof(info);
	char sym[128], *p = sym;
	struct btf *btf;

	btf = btf__new_empty();
	if (!ASSERT_OK_PTR(btf, "btf"))
		return;
	int_id = btf__add_int(btf, "i32", 4, BTF_INT_SIGNED);
	ASSERT_GT(int_id, 0, "int");
	proto_id = btf__add_func_proto(btf, int_id);
	ASSERT_GT(proto_id, 0, "proto");
	ASSERT_OK(btf__add_func_param(btf, "ctx", int_id), "param");
	func_info.type_id = btf__add_func(btf, FUNC_NAME, BTF_FUNC_STATIC, proto_id);
	ASSERT_GT(func_info.type_id, 0, "func");
	if (!ASSERT_OK(btf__load_into_kernel(btf), "btf load"))
		goto out;

	opts.prog_btf_fd = btf__fd(btf);
	opts.func_info = &func_info;
	opts.func_info_cnt = 1;
	opts.func_info_rec_size = sizeof(func_info);
	prog_fd = bpf_prog_load(BPF_PROG_TYPE_SOCKET_FILTER, NULL, "GPL", insns,
				ARRAY_SIZE(insns), &opts);
	if (!ASSERT_GE(prog_fd, 0, "prog load"))
		goto out;
	if (!ASSERT_OK(bpf_prog_get_info_by_fd(prog_fd, &info, &len), "prog info"))
		goto out;
	if (!info.jited_prog_len) {
		test__skip();
		goto out;
	}

	p += sprintf(p, "bpf_prog_");
	for (i = 0; i < BPF_TAG_SIZE; i++)
		p += sprintf(p, "%02x", info.tag[i]);
	sprintf(p, "_%s", KSYM_NAME);
	ASSERT_OK(kallsyms_find(sym, &addr), sym);
out:
	if (prog_fd >= 0)
		close(prog_fd);
	btf__free(btf);
}

#define PIN_PATH "/sys/fs/bpf/btf_rust_piece"

/*
 * A piece of a static that LLVM split has the type of the whole static.
 * It's the last variable in the section, so its type ends past the map value.
 */
static void test_piece(void)
{
	LIBBPF_OPTS(bpf_map_create_opts, opts);
	int int_id, struct_id, var_id, piece_id, sec_id, map_fd = -1, key = 0;
	__u32 value[2] = { 0x11111111, 0x22222222 };
	char line[256] = {}, out[1024] = {};
	struct btf *btf;
	FILE *f = NULL;

	btf = btf__new_empty();
	if (!ASSERT_OK_PTR(btf, "btf"))
		return;
	int_id = btf__add_int(btf, "u32", 4, 0);
	ASSERT_GT(int_id, 0, "int");
	struct_id = btf__add_struct(btf, "Whole", 8);
	ASSERT_GT(struct_id, 0, "struct");
	ASSERT_OK(btf__add_field(btf, "a", int_id, 0, 0), "field");
	ASSERT_OK(btf__add_field(btf, "b", int_id, 32, 0), "field");
	var_id = btf__add_var(btf, "CNT", BTF_VAR_STATIC, int_id);
	ASSERT_GT(var_id, 0, "var");
	piece_id = btf__add_var(btf, "WHOLE.1", BTF_VAR_STATIC, struct_id);
	ASSERT_GT(piece_id, 0, "piece");
	sec_id = btf__add_datasec(btf, ".bss", sizeof(value));
	ASSERT_GT(sec_id, 0, "datasec");
	ASSERT_OK(btf__add_datasec_var_info(btf, var_id, 0, 4), "var info");
	ASSERT_OK(btf__add_datasec_var_info(btf, piece_id, 4, 4), "piece info");
	if (!ASSERT_OK(btf__load_into_kernel(btf), "btf load"))
		goto out;

	opts.btf_fd = btf__fd(btf);
	opts.btf_value_type_id = sec_id;
	map_fd = bpf_map_create(BPF_MAP_TYPE_ARRAY, ".bss", sizeof(key), sizeof(value), 1, &opts);
	if (!ASSERT_GE(map_fd, 0, "map create"))
		goto out;
	if (!ASSERT_OK(bpf_map_update_elem(map_fd, &key, value, 0), "map update"))
		goto out;

	/* the variable is printed, the piece is not */
	unlink(PIN_PATH);
	if (!ASSERT_OK(bpf_obj_pin(map_fd, PIN_PATH), "pin"))
		goto out;
	f = fopen(PIN_PATH, "r");
	if (!ASSERT_OK_PTR(f, "open"))
		goto out;
	while (fgets(line, sizeof(line), f) && line[0] == '#')
		;
	ASSERT_HAS_SUBSTR(line, "286331153", "var");
	ASSERT_NULL(strstr(line, "572662306"), "piece");

	/* the same for bpftool */
	if (!ASSERT_OK(get_bpftool_command_output("map dump pinned " PIN_PATH, out, sizeof(out)),
		       "bpftool"))
		goto out;
	ASSERT_HAS_SUBSTR(out, "CNT", "var");
	ASSERT_NULL(strstr(out, "WHOLE.1"), "piece");
out:
	if (f)
		fclose(f);
	unlink(PIN_PATH);
	if (map_fd >= 0)
		close(map_fd);
	btf__free(btf);
}

/* Serial: programs are not in kallsyms while another test sets bpf_jit_harden */
void serial_test_btf_rust(void)
{
	if (test__start_subtest("func_name"))
		test_func_name();
	if (test__start_subtest("piece"))
		test_piece();
}
