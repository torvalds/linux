// SPDX-License-Identifier: (LGPL-2.1 OR BSD-2-Clause)
/* Copyright (c) 2023 Meta Platforms, Inc. and affiliates. */
#include <linux/kernel.h>
#include <linux/filter.h>
#include "bpf.h"
#include "libbpf.h"
#include "libbpf_common.h"
#include "libbpf_internal.h"

static inline __u64 ptr_to_u64(const void *ptr)
{
	return (__u64)(unsigned long)ptr;
}

int probe_fd(int fd)
{
	if (fd >= 0)
		close(fd);
	return fd >= 0;
}

static int probe_kern_prog_name(int token_fd)
{
	const size_t attr_sz = offsetofend(union bpf_attr, prog_token_fd);
	struct bpf_insn insns[] = {
		BPF_MOV64_IMM(BPF_REG_0, 0),
		BPF_EXIT_INSN(),
	};
	union bpf_attr attr;
	int ret;

	memset(&attr, 0, attr_sz);
	attr.prog_type = BPF_PROG_TYPE_SOCKET_FILTER;
	attr.license = ptr_to_u64("GPL");
	attr.insns = ptr_to_u64(insns);
	attr.insn_cnt = (__u32)ARRAY_SIZE(insns);
	attr.prog_token_fd = token_fd;
	if (token_fd)
		attr.prog_flags |= BPF_F_TOKEN_FD;
	libbpf_strlcpy(attr.prog_name, "libbpf_nametest", sizeof(attr.prog_name));

	/* make sure loading with name works */
	ret = sys_bpf_prog_load(&attr, attr_sz, PROG_LOAD_ATTEMPTS);
	return probe_fd(ret);
}

static int probe_kern_global_data(int token_fd)
{
	struct bpf_insn insns[] = {
		BPF_LD_MAP_VALUE(BPF_REG_1, 0, 16),
		BPF_ST_MEM(BPF_DW, BPF_REG_1, 0, 42),
		BPF_MOV64_IMM(BPF_REG_0, 0),
		BPF_EXIT_INSN(),
	};
	LIBBPF_OPTS(bpf_map_create_opts, map_opts,
		.token_fd = token_fd,
		.map_flags = token_fd ? BPF_F_TOKEN_FD : 0,
	);
	LIBBPF_OPTS(bpf_prog_load_opts, prog_opts,
		.token_fd = token_fd,
		.prog_flags = token_fd ? BPF_F_TOKEN_FD : 0,
	);
	int ret, map, insn_cnt = ARRAY_SIZE(insns);

	map = bpf_map_create(BPF_MAP_TYPE_ARRAY, "libbpf_global", sizeof(int), 32, 1, &map_opts);
	if (map < 0) {
		ret = -errno;
		pr_warn("Error in %s(): %s. Couldn't create simple array map.\n",
			__func__, errstr(ret));
		return ret;
	}

	insns[0].imm = map;

	ret = bpf_prog_load(BPF_PROG_TYPE_SOCKET_FILTER, NULL, "GPL", insns, insn_cnt, &prog_opts);
	close(map);
	return probe_fd(ret);
}

static int probe_kern_btf(int token_fd)
{
	static const char strs[] = "\0int";
	__u32 types[] = {
		/* int */
		BTF_TYPE_INT_ENC(1, BTF_INT_SIGNED, 0, 32, 4),
	};

	return probe_fd(libbpf__load_raw_btf((char *)types, sizeof(types),
					     strs, sizeof(strs), token_fd));
}

static int probe_kern_btf_func(int token_fd)
{
	static const char strs[] = "\0int\0x\0a";
	/* void x(int a) {} */
	__u32 types[] = {
		/* int */
		BTF_TYPE_INT_ENC(1, BTF_INT_SIGNED, 0, 32, 4),  /* [1] */
		/* FUNC_PROTO */                                /* [2] */
		BTF_TYPE_ENC(0, BTF_INFO_ENC(BTF_KIND_FUNC_PROTO, 0, 1), 0),
		BTF_PARAM_ENC(7, 1),
		/* FUNC x */                                    /* [3] */
		BTF_TYPE_ENC(5, BTF_INFO_ENC(BTF_KIND_FUNC, 0, 0), 2),
	};

	return probe_fd(libbpf__load_raw_btf((char *)types, sizeof(types),
					     strs, sizeof(strs), token_fd));
}

static int probe_kern_btf_func_global(int token_fd)
{
	static const char strs[] = "\0int\0x\0a";
	/* static void x(int a) {} */
	__u32 types[] = {
		/* int */
		BTF_TYPE_INT_ENC(1, BTF_INT_SIGNED, 0, 32, 4),  /* [1] */
		/* FUNC_PROTO */                                /* [2] */
		BTF_TYPE_ENC(0, BTF_INFO_ENC(BTF_KIND_FUNC_PROTO, 0, 1), 0),
		BTF_PARAM_ENC(7, 1),
		/* FUNC x BTF_FUNC_GLOBAL */                    /* [3] */
		BTF_TYPE_ENC(5, BTF_INFO_ENC(BTF_KIND_FUNC, 0, BTF_FUNC_GLOBAL), 2),
	};

	return probe_fd(libbpf__load_raw_btf((char *)types, sizeof(types),
					     strs, sizeof(strs), token_fd));
}

static int probe_kern_btf_datasec(int token_fd)
{
	static const char strs[] = "\0x\0.data";
	/* static int a; */
	__u32 types[] = {
		/* int */
		BTF_TYPE_INT_ENC(0, BTF_INT_SIGNED, 0, 32, 4),  /* [1] */
		/* VAR x */                                     /* [2] */
		BTF_TYPE_ENC(1, BTF_INFO_ENC(BTF_KIND_VAR, 0, 0), 1),
		BTF_VAR_STATIC,
		/* DATASEC val */                               /* [3] */
		BTF_TYPE_ENC(3, BTF_INFO_ENC(BTF_KIND_DATASEC, 0, 1), 4),
		BTF_VAR_SECINFO_ENC(2, 0, 4),
	};

	return probe_fd(libbpf__load_raw_btf((char *)types, sizeof(types),
					     strs, sizeof(strs), token_fd));
}

static int probe_kern_btf_qmark_datasec(int token_fd)
{
	static const char strs[] = "\0x\0?.data";
	/* static int a; */
	__u32 types[] = {
		/* int */
		BTF_TYPE_INT_ENC(0, BTF_INT_SIGNED, 0, 32, 4),  /* [1] */
		/* VAR x */                                     /* [2] */
		BTF_TYPE_ENC(1, BTF_INFO_ENC(BTF_KIND_VAR, 0, 0), 1),
		BTF_VAR_STATIC,
		/* DATASEC ?.data */                            /* [3] */
		BTF_TYPE_ENC(3, BTF_INFO_ENC(BTF_KIND_DATASEC, 0, 1), 4),
		BTF_VAR_SECINFO_ENC(2, 0, 4),
	};

	return probe_fd(libbpf__load_raw_btf((char *)types, sizeof(types),
					     strs, sizeof(strs), token_fd));
}

static int probe_kern_btf_float(int token_fd)
{
	static const char strs[] = "\0float";
	__u32 types[] = {
		/* float */
		BTF_TYPE_FLOAT_ENC(1, 4),
	};

	return probe_fd(libbpf__load_raw_btf((char *)types, sizeof(types),
					     strs, sizeof(strs), token_fd));
}

static int probe_kern_btf_decl_tag(int token_fd)
{
	static const char strs[] = "\0tag";
	__u32 types[] = {
		/* int */
		BTF_TYPE_INT_ENC(0, BTF_INT_SIGNED, 0, 32, 4),  /* [1] */
		/* VAR x */                                     /* [2] */
		BTF_TYPE_ENC(1, BTF_INFO_ENC(BTF_KIND_VAR, 0, 0), 1),
		BTF_VAR_STATIC,
		/* attr */
		BTF_TYPE_DECL_TAG_ENC(1, 2, -1),
	};

	return probe_fd(libbpf__load_raw_btf((char *)types, sizeof(types),
					     strs, sizeof(strs), token_fd));
}

static int probe_kern_btf_type_tag(int token_fd)
{
	static const char strs[] = "\0tag";
	__u32 types[] = {
		/* int */
		BTF_TYPE_INT_ENC(0, BTF_INT_SIGNED, 0, 32, 4),		/* [1] */
		/* attr */
		BTF_TYPE_TYPE_TAG_ENC(1, 1),				/* [2] */
		/* ptr */
		BTF_TYPE_ENC(0, BTF_INFO_ENC(BTF_KIND_PTR, 0, 0), 2),	/* [3] */
	};

	return probe_fd(libbpf__load_raw_btf((char *)types, sizeof(types),
					     strs, sizeof(strs), token_fd));
}

static int probe_kern_array_mmap(int token_fd)
{
	LIBBPF_OPTS(bpf_map_create_opts, opts,
		.map_flags = BPF_F_MMAPABLE | (token_fd ? BPF_F_TOKEN_FD : 0),
		.token_fd = token_fd,
	);
	int fd;

	fd = bpf_map_create(BPF_MAP_TYPE_ARRAY, "libbpf_mmap", sizeof(int), sizeof(int), 1, &opts);
	return probe_fd(fd);
}

static int probe_kern_exp_attach_type(int token_fd)
{
	LIBBPF_OPTS(bpf_prog_load_opts, opts,
		.expected_attach_type = BPF_CGROUP_INET_SOCK_CREATE,
		.token_fd = token_fd,
		.prog_flags = token_fd ? BPF_F_TOKEN_FD : 0,
	);
	struct bpf_insn insns[] = {
		BPF_MOV64_IMM(BPF_REG_0, 0),
		BPF_EXIT_INSN(),
	};
	int fd, insn_cnt = ARRAY_SIZE(insns);

	/* use any valid combination of program type and (optional)
	 * non-zero expected attach type (i.e., not a BPF_CGROUP_INET_INGRESS)
	 * to see if kernel supports expected_attach_type field for
	 * BPF_PROG_LOAD command
	 */
	fd = bpf_prog_load(BPF_PROG_TYPE_CGROUP_SOCK, NULL, "GPL", insns, insn_cnt, &opts);
	return probe_fd(fd);
}

static int probe_kern_probe_read_kernel(int token_fd)
{
	LIBBPF_OPTS(bpf_prog_load_opts, opts,
		.token_fd = token_fd,
		.prog_flags = token_fd ? BPF_F_TOKEN_FD : 0,
	);
	struct bpf_insn insns[] = {
		BPF_MOV64_REG(BPF_REG_1, BPF_REG_10),	/* r1 = r10 (fp) */
		BPF_ALU64_IMM(BPF_ADD, BPF_REG_1, -8),	/* r1 += -8 */
		BPF_MOV64_IMM(BPF_REG_2, 8),		/* r2 = 8 */
		BPF_MOV64_IMM(BPF_REG_3, 0),		/* r3 = 0 */
		BPF_RAW_INSN(BPF_JMP | BPF_CALL, 0, 0, 0, BPF_FUNC_probe_read_kernel),
		BPF_EXIT_INSN(),
	};
	int fd, insn_cnt = ARRAY_SIZE(insns);

	fd = bpf_prog_load(BPF_PROG_TYPE_TRACEPOINT, NULL, "GPL", insns, insn_cnt, &opts);
	return probe_fd(fd);
}

static int probe_prog_bind_map(int token_fd)
{
	struct bpf_insn insns[] = {
		BPF_MOV64_IMM(BPF_REG_0, 0),
		BPF_EXIT_INSN(),
	};
	LIBBPF_OPTS(bpf_map_create_opts, map_opts,
		.token_fd = token_fd,
		.map_flags = token_fd ? BPF_F_TOKEN_FD : 0,
	);
	LIBBPF_OPTS(bpf_prog_load_opts, prog_opts,
		.token_fd = token_fd,
		.prog_flags = token_fd ? BPF_F_TOKEN_FD : 0,
	);
	int ret, map, prog, insn_cnt = ARRAY_SIZE(insns);

	map = bpf_map_create(BPF_MAP_TYPE_ARRAY, "libbpf_det_bind", sizeof(int), 32, 1, &map_opts);
	if (map < 0) {
		ret = -errno;
		pr_warn("Error in %s(): %s. Couldn't create simple array map.\n",
			__func__, errstr(ret));
		return ret;
	}

	prog = bpf_prog_load(BPF_PROG_TYPE_SOCKET_FILTER, NULL, "GPL", insns, insn_cnt, &prog_opts);
	if (prog < 0) {
		close(map);
		return 0;
	}

	ret = bpf_prog_bind_map(prog, map, NULL);

	close(map);
	close(prog);

	return ret >= 0;
}

static int probe_module_btf(int token_fd)
{
	static const char strs[] = "\0int";
	__u32 types[] = {
		/* int */
		BTF_TYPE_INT_ENC(1, BTF_INT_SIGNED, 0, 32, 4),
	};
	struct bpf_btf_info info;
	__u32 len = sizeof(info);
	char name[16];
	int fd, err;

	fd = libbpf__load_raw_btf((char *)types, sizeof(types), strs, sizeof(strs), token_fd);
	if (fd < 0)
		return 0; /* BTF not supported at all */

	memset(&info, 0, sizeof(info));
	info.name = ptr_to_u64(name);
	info.name_len = sizeof(name);

	/* check that BPF_OBJ_GET_INFO_BY_FD supports specifying name pointer;
	 * kernel's module BTF support coincides with support for
	 * name/name_len fields in struct bpf_btf_info.
	 */
	err = bpf_btf_get_info_by_fd(fd, &info, &len);
	close(fd);
	return !err;
}

static int probe_perf_link(int token_fd)
{
	struct bpf_insn insns[] = {
		BPF_MOV64_IMM(BPF_REG_0, 0),
		BPF_EXIT_INSN(),
	};
	LIBBPF_OPTS(bpf_prog_load_opts, opts,
		.token_fd = token_fd,
		.prog_flags = token_fd ? BPF_F_TOKEN_FD : 0,
	);
	int prog_fd, link_fd, err;

	prog_fd = bpf_prog_load(BPF_PROG_TYPE_TRACEPOINT, NULL, "GPL",
				insns, ARRAY_SIZE(insns), &opts);
	if (prog_fd < 0)
		return -errno;

	/* use invalid perf_event FD to get EBADF, if link is supported;
	 * otherwise EINVAL should be returned
	 */
	link_fd = bpf_link_create(prog_fd, -1, BPF_PERF_EVENT, NULL);
	err = -errno; /* close() can clobber errno */

	if (link_fd >= 0)
		close(link_fd);
	close(prog_fd);

	return link_fd < 0 && err == -EBADF;
}

static int probe_uprobe_multi_link(int token_fd)
{
	LIBBPF_OPTS(bpf_prog_load_opts, load_opts,
		.expected_attach_type = BPF_TRACE_UPROBE_MULTI,
		.token_fd = token_fd,
		.prog_flags = token_fd ? BPF_F_TOKEN_FD : 0,
	);
	LIBBPF_OPTS(bpf_link_create_opts, link_opts);
	struct bpf_insn insns[] = {
		BPF_MOV64_IMM(BPF_REG_0, 0),
		BPF_EXIT_INSN(),
	};
	int prog_fd, link_fd, err;
	unsigned long offset = 0;

	prog_fd = bpf_prog_load(BPF_PROG_TYPE_KPROBE, NULL, "GPL",
				insns, ARRAY_SIZE(insns), &load_opts);
	if (prog_fd < 0)
		return -errno;

	/* Creating uprobe in '/' binary should fail with -EBADF. */
	link_opts.uprobe_multi.path = "/";
	link_opts.uprobe_multi.offsets = &offset;
	link_opts.uprobe_multi.cnt = 1;

	link_fd = bpf_link_create(prog_fd, -1, BPF_TRACE_UPROBE_MULTI, &link_opts);
	err = -errno; /* close() can clobber errno */

	if (link_fd >= 0 || err != -EBADF) {
		if (link_fd >= 0)
			close(link_fd);
		close(prog_fd);
		return 0;
	}

	/* Initial multi-uprobe support in kernel didn't handle PID filtering
	 * correctly (it was doing thread filtering, not process filtering).
	 * So now we'll detect if PID filtering logic was fixed, and, if not,
	 * we'll pretend multi-uprobes are not supported, if not.
	 * Multi-uprobes are used in USDT attachment logic, and we need to be
	 * conservative here, because multi-uprobe selection happens early at
	 * load time, while the use of PID filtering is known late at
	 * attachment time, at which point it's too late to undo multi-uprobe
	 * selection.
	 *
	 * Creating uprobe with pid == -1 for (invalid) '/' binary will fail
	 * early with -EINVAL on kernels with fixed PID filtering logic;
	 * otherwise -ESRCH would be returned if passed correct binary path
	 * (but we'll just get -BADF, of course).
	 */
	link_opts.uprobe_multi.pid = -1; /* invalid PID */
	link_opts.uprobe_multi.path = "/"; /* invalid path */
	link_opts.uprobe_multi.offsets = &offset;
	link_opts.uprobe_multi.cnt = 1;

	link_fd = bpf_link_create(prog_fd, -1, BPF_TRACE_UPROBE_MULTI, &link_opts);
	err = -errno; /* close() can clobber errno */

	if (link_fd >= 0)
		close(link_fd);
	close(prog_fd);

	return link_fd < 0 && err == -EINVAL;
}

static int probe_kern_bpf_cookie(int token_fd)
{
	struct bpf_insn insns[] = {
		BPF_RAW_INSN(BPF_JMP | BPF_CALL, 0, 0, 0, BPF_FUNC_get_attach_cookie),
		BPF_EXIT_INSN(),
	};
	LIBBPF_OPTS(bpf_prog_load_opts, opts,
		.token_fd = token_fd,
		.prog_flags = token_fd ? BPF_F_TOKEN_FD : 0,
	);
	int ret, insn_cnt = ARRAY_SIZE(insns);

	ret = bpf_prog_load(BPF_PROG_TYPE_TRACEPOINT, NULL, "GPL", insns, insn_cnt, &opts);
	return probe_fd(ret);
}

static int probe_kern_btf_enum64(int token_fd)
{
	static const char strs[] = "\0enum64";
	__u32 types[] = {
		BTF_TYPE_ENC(1, BTF_INFO_ENC(BTF_KIND_ENUM64, 0, 0), 8),
	};

	return probe_fd(libbpf__load_raw_btf((char *)types, sizeof(types),
					     strs, sizeof(strs), token_fd));
}

static int probe_kern_arg_ctx_tag(int token_fd)
{
	static const char strs[] = "\0a\0b\0arg:ctx\0";
	const __u32 types[] = {
		/* [1] INT */
		BTF_TYPE_INT_ENC(1 /* "a" */, BTF_INT_SIGNED, 0, 32, 4),
		/* [2] PTR -> VOID */
		BTF_TYPE_ENC(0, BTF_INFO_ENC(BTF_KIND_PTR, 0, 0), 0),
		/* [3] FUNC_PROTO `int(void *a)` */
		BTF_TYPE_ENC(0, BTF_INFO_ENC(BTF_KIND_FUNC_PROTO, 0, 1), 1),
		BTF_PARAM_ENC(1 /* "a" */, 2),
		/* [4] FUNC 'a' -> FUNC_PROTO (main prog) */
		BTF_TYPE_ENC(1 /* "a" */, BTF_INFO_ENC(BTF_KIND_FUNC, 0, BTF_FUNC_GLOBAL), 3),
		/* [5] FUNC_PROTO `int(void *b __arg_ctx)` */
		BTF_TYPE_ENC(0, BTF_INFO_ENC(BTF_KIND_FUNC_PROTO, 0, 1), 1),
		BTF_PARAM_ENC(3 /* "b" */, 2),
		/* [6] FUNC 'b' -> FUNC_PROTO (subprog) */
		BTF_TYPE_ENC(3 /* "b" */, BTF_INFO_ENC(BTF_KIND_FUNC, 0, BTF_FUNC_GLOBAL), 5),
		/* [7] DECL_TAG 'arg:ctx' -> func 'b' arg 'b' */
		BTF_TYPE_DECL_TAG_ENC(5 /* "arg:ctx" */, 6, 0),
	};
	const struct bpf_insn insns[] = {
		/* main prog */
		BPF_CALL_REL(+1),
		BPF_EXIT_INSN(),
		/* global subprog */
		BPF_EMIT_CALL(BPF_FUNC_get_func_ip), /* needs PTR_TO_CTX */
		BPF_EXIT_INSN(),
	};
	const struct bpf_func_info_min func_infos[] = {
		{ 0, 4 }, /* main prog -> FUNC 'a' */
		{ 2, 6 }, /* subprog -> FUNC 'b' */
	};
	LIBBPF_OPTS(bpf_prog_load_opts, opts,
		.token_fd = token_fd,
		.prog_flags = token_fd ? BPF_F_TOKEN_FD : 0,
	);
	int prog_fd, btf_fd, insn_cnt = ARRAY_SIZE(insns);

	btf_fd = libbpf__load_raw_btf((char *)types, sizeof(types), strs, sizeof(strs), token_fd);
	if (btf_fd < 0)
		return 0;

	opts.prog_btf_fd = btf_fd;
	opts.func_info = &func_infos;
	opts.func_info_cnt = ARRAY_SIZE(func_infos);
	opts.func_info_rec_size = sizeof(func_infos[0]);

	prog_fd = bpf_prog_load(BPF_PROG_TYPE_KPROBE, "det_arg_ctx",
				"GPL", insns, insn_cnt, &opts);
	close(btf_fd);

	return probe_fd(prog_fd);
}

typedef int (*feature_probe_fn)(int /* token_fd */);

static struct kern_feature_cache feature_cache;

static struct kern_feature_desc {
	const char *desc;
	feature_probe_fn probe;
} feature_probes[__FEAT_CNT] = {
	[FEAT_PROG_NAME] = {
		"BPF program name", probe_kern_prog_name,
	},
	[FEAT_GLOBAL_DATA] = {
		"global variables", probe_kern_global_data,
	},
	[FEAT_BTF] = {
		"minimal BTF", probe_kern_btf,
	},
	[FEAT_BTF_FUNC] = {
		"BTF functions", probe_kern_btf_func,
	},
	[FEAT_BTF_GLOBAL_FUNC] = {
		"BTF global function", probe_kern_btf_func_global,
	},
	[FEAT_BTF_DATASEC] = {
		"BTF data section and variable", probe_kern_btf_datasec,
	},
	[FEAT_ARRAY_MMAP] = {
		"ARRAY map mmap()", probe_kern_array_mmap,
	},
	[FEAT_EXP_ATTACH_TYPE] = {
		"BPF_PROG_LOAD expected_attach_type attribute",
		probe_kern_exp_attach_type,
	},
	[FEAT_PROBE_READ_KERN] = {
		"bpf_probe_read_kernel() helper", probe_kern_probe_read_kernel,
	},
	[FEAT_PROG_BIND_MAP] = {
		"BPF_PROG_BIND_MAP support", probe_prog_bind_map,
	},
	[FEAT_MODULE_BTF] = {
		"module BTF support", probe_module_btf,
	},
	[FEAT_BTF_FLOAT] = {
		"BTF_KIND_FLOAT support", probe_kern_btf_float,
	},
	[FEAT_PERF_LINK] = {
		"BPF perf link support", probe_perf_link,
	},
	[FEAT_BTF_DECL_TAG] = {
		"BTF_KIND_DECL_TAG support", probe_kern_btf_decl_tag,
	},
	[FEAT_BTF_TYPE_TAG] = {
		"BTF_KIND_TYPE_TAG support", probe_kern_btf_type_tag,
	},
	[FEAT_MEMCG_ACCOUNT] = {
		"memcg-based memory accounting", probe_memcg_account,
	},
	[FEAT_BPF_COOKIE] = {
		"BPF cookie support", probe_kern_bpf_cookie,
	},
	[FEAT_BTF_ENUM64] = {
		"BTF_KIND_ENUM64 support", probe_kern_btf_enum64,
	},
	[FEAT_SYSCALL_WRAPPER] = {
		"Kernel using syscall wrapper", probe_kern_syscall_wrapper,
	},
	[FEAT_UPROBE_MULTI_LINK] = {
		"BPF multi-uprobe link support", probe_uprobe_multi_link,
	},
	[FEAT_ARG_CTX_TAG] = {
		"kernel-side __arg_ctx tag", probe_kern_arg_ctx_tag,
	},
	[FEAT_BTF_QMARK_DATASEC] = {
		"BTF DATASEC names starting from '?'", probe_kern_btf_qmark_datasec,
	},
};

bool feat_supported(struct kern_feature_cache *cache, enum kern_feature_id feat_id)
{
	struct kern_feature_desc *feat = &feature_probes[feat_id];
	int ret;

	/* assume global feature cache, unless custom one is provided */
	if (!cache)
		cache = &feature_cache;

	if (READ_ONCE(cache->res[feat_id]) == FEAT_UNKNOWN) {
		ret = feat->probe(cache->token_fd);
		if (ret > 0) {
			WRITE_ONCE(cache->res[feat_id], FEAT_SUPPORTED);
		} else if (ret == 0) {
			WRITE_ONCE(cache->res[feat_id], FEAT_MISSING);
		} else {
			pr_warn("Detection of kernel %s support failed: %s\n",
				feat->desc, errstr(ret));
			WRITE_ONCE(cache->res[feat_id], FEAT_MISSING);
		}
	}

	return READ_ONCE(cache->res[feat_id]) == FEAT_SUPPORTED;
}
