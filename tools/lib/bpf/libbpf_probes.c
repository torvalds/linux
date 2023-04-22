// SPDX-License-Identifier: (LGPL-2.1 OR BSD-2-Clause)
/* Copyright (c) 2019 Netronome Systems, Inc. */

#include <errno.h>
#include <fcntl.h>
#include <string.h>
#include <stdlib.h>
#include <unistd.h>
#include <net/if.h>
#include <sys/utsname.h>

#include <linux/btf.h>
#include <linux/filter.h>
#include <linux/kernel.h>
#include <linux/version.h>

#include "bpf.h"
#include "libbpf.h"
#include "libbpf_internal.h"

/* On Ubuntu LINUX_VERSION_CODE doesn't correspond to info.release,
 * but Ubuntu provides /proc/version_signature file, as described at
 * https://ubuntu.com/kernel, with an example contents below, which we
 * can use to get a proper LINUX_VERSION_CODE.
 *
 *   Ubuntu 5.4.0-12.15-generic 5.4.8
 *
 * In the above, 5.4.8 is what kernel is actually expecting, while
 * uname() call will return 5.4.0 in info.release.
 */
static __u32 get_ubuntu_kernel_version(void)
{
	const char *ubuntu_kver_file = "/proc/version_signature";
	__u32 major, minor, patch;
	int ret;
	FILE *f;

	if (faccessat(AT_FDCWD, ubuntu_kver_file, R_OK, AT_EACCESS) != 0)
		return 0;

	f = fopen(ubuntu_kver_file, "r");
	if (!f)
		return 0;

	ret = fscanf(f, "%*s %*s %u.%u.%u\n", &major, &minor, &patch);
	fclose(f);
	if (ret != 3)
		return 0;

	return KERNEL_VERSION(major, minor, patch);
}

/* On Debian LINUX_VERSION_CODE doesn't correspond to info.release.
 * Instead, it is provided in info.version. An example content of
 * Debian 10 looks like the below.
 *
 *   utsname::release   4.19.0-22-amd64
 *   utsname::version   #1 SMP Debian 4.19.260-1 (2022-09-29)
 *
 * In the above, 4.19.260 is what kernel is actually expecting, while
 * uname() call will return 4.19.0 in info.release.
 */
static __u32 get_debian_kernel_version(struct utsname *info)
{
	__u32 major, minor, patch;
	char *p;

	p = strstr(info->version, "Debian ");
	if (!p) {
		/* This is not a Debian kernel. */
		return 0;
	}

	if (sscanf(p, "Debian %u.%u.%u", &major, &minor, &patch) != 3)
		return 0;

	return KERNEL_VERSION(major, minor, patch);
}

__u32 get_kernel_version(void)
{
	__u32 major, minor, patch, version;
	struct utsname info;

	/* Check if this is an Ubuntu kernel. */
	version = get_ubuntu_kernel_version();
	if (version != 0)
		return version;

	uname(&info);

	/* Check if this is a Debian kernel. */
	version = get_debian_kernel_version(&info);
	if (version != 0)
		return version;

	if (sscanf(info.release, "%u.%u.%u", &major, &minor, &patch) != 3)
		return 0;

	return KERNEL_VERSION(major, minor, patch);
}

static int probe_prog_load(enum bpf_prog_type prog_type,
			   const struct bpf_insn *insns, size_t insns_cnt,
			   char *log_buf, size_t log_buf_sz)
{
	LIBBPF_OPTS(bpf_prog_load_opts, opts,
		.log_buf = log_buf,
		.log_size = log_buf_sz,
		.log_level = log_buf ? 1 : 0,
	);
	int fd, err, exp_err = 0;
	const char *exp_msg = NULL;
	char buf[4096];

	switch (prog_type) {
	case BPF_PROG_TYPE_CGROUP_SOCK_ADDR:
		opts.expected_attach_type = BPF_CGROUP_INET4_CONNECT;
		break;
	case BPF_PROG_TYPE_CGROUP_SOCKOPT:
		opts.expected_attach_type = BPF_CGROUP_GETSOCKOPT;
		break;
	case BPF_PROG_TYPE_SK_LOOKUP:
		opts.expected_attach_type = BPF_SK_LOOKUP;
		break;
	case BPF_PROG_TYPE_KPROBE:
		opts.kern_version = get_kernel_version();
		break;
	case BPF_PROG_TYPE_LIRC_MODE2:
		opts.expected_attach_type = BPF_LIRC_MODE2;
		break;
	case BPF_PROG_TYPE_TRACING:
	case BPF_PROG_TYPE_LSM:
		opts.log_buf = buf;
		opts.log_size = sizeof(buf);
		opts.log_level = 1;
		if (prog_type == BPF_PROG_TYPE_TRACING)
			opts.expected_attach_type = BPF_TRACE_FENTRY;
		else
			opts.expected_attach_type = BPF_MODIFY_RETURN;
		opts.attach_btf_id = 1;

		exp_err = -EINVAL;
		exp_msg = "attach_btf_id 1 is not a function";
		break;
	case BPF_PROG_TYPE_EXT:
		opts.log_buf = buf;
		opts.log_size = sizeof(buf);
		opts.log_level = 1;
		opts.attach_btf_id = 1;

		exp_err = -EINVAL;
		exp_msg = "Cannot replace kernel functions";
		break;
	case BPF_PROG_TYPE_SYSCALL:
		opts.prog_flags = BPF_F_SLEEPABLE;
		break;
	case BPF_PROG_TYPE_STRUCT_OPS:
		exp_err = -524; /* -ENOTSUPP */
		break;
	case BPF_PROG_TYPE_UNSPEC:
	case BPF_PROG_TYPE_SOCKET_FILTER:
	case BPF_PROG_TYPE_SCHED_CLS:
	case BPF_PROG_TYPE_SCHED_ACT:
	case BPF_PROG_TYPE_TRACEPOINT:
	case BPF_PROG_TYPE_XDP:
	case BPF_PROG_TYPE_PERF_EVENT:
	case BPF_PROG_TYPE_CGROUP_SKB:
	case BPF_PROG_TYPE_CGROUP_SOCK:
	case BPF_PROG_TYPE_LWT_IN:
	case BPF_PROG_TYPE_LWT_OUT:
	case BPF_PROG_TYPE_LWT_XMIT:
	case BPF_PROG_TYPE_SOCK_OPS:
	case BPF_PROG_TYPE_SK_SKB:
	case BPF_PROG_TYPE_CGROUP_DEVICE:
	case BPF_PROG_TYPE_SK_MSG:
	case BPF_PROG_TYPE_RAW_TRACEPOINT:
	case BPF_PROG_TYPE_RAW_TRACEPOINT_WRITABLE:
	case BPF_PROG_TYPE_LWT_SEG6LOCAL:
	case BPF_PROG_TYPE_SK_REUSEPORT:
	case BPF_PROG_TYPE_FLOW_DISSECTOR:
	case BPF_PROG_TYPE_CGROUP_SYSCTL:
	case BPF_PROG_TYPE_NETFILTER:
		break;
	default:
		return -EOPNOTSUPP;
	}

	fd = bpf_prog_load(prog_type, NULL, "GPL", insns, insns_cnt, &opts);
	err = -errno;
	if (fd >= 0)
		close(fd);
	if (exp_err) {
		if (fd >= 0 || err != exp_err)
			return 0;
		if (exp_msg && !strstr(buf, exp_msg))
			return 0;
		return 1;
	}
	return fd >= 0 ? 1 : 0;
}

int libbpf_probe_bpf_prog_type(enum bpf_prog_type prog_type, const void *opts)
{
	struct bpf_insn insns[] = {
		BPF_MOV64_IMM(BPF_REG_0, 0),
		BPF_EXIT_INSN()
	};
	const size_t insn_cnt = ARRAY_SIZE(insns);
	int ret;

	if (opts)
		return libbpf_err(-EINVAL);

	ret = probe_prog_load(prog_type, insns, insn_cnt, NULL, 0);
	return libbpf_err(ret);
}

int libbpf__load_raw_btf(const char *raw_types, size_t types_len,
			 const char *str_sec, size_t str_len)
{
	struct btf_header hdr = {
		.magic = BTF_MAGIC,
		.version = BTF_VERSION,
		.hdr_len = sizeof(struct btf_header),
		.type_len = types_len,
		.str_off = types_len,
		.str_len = str_len,
	};
	int btf_fd, btf_len;
	__u8 *raw_btf;

	btf_len = hdr.hdr_len + hdr.type_len + hdr.str_len;
	raw_btf = malloc(btf_len);
	if (!raw_btf)
		return -ENOMEM;

	memcpy(raw_btf, &hdr, sizeof(hdr));
	memcpy(raw_btf + hdr.hdr_len, raw_types, hdr.type_len);
	memcpy(raw_btf + hdr.hdr_len + hdr.type_len, str_sec, hdr.str_len);

	btf_fd = bpf_btf_load(raw_btf, btf_len, NULL);

	free(raw_btf);
	return btf_fd;
}

static int load_local_storage_btf(void)
{
	const char strs[] = "\0bpf_spin_lock\0val\0cnt\0l";
	/* struct bpf_spin_lock {
	 *   int val;
	 * };
	 * struct val {
	 *   int cnt;
	 *   struct bpf_spin_lock l;
	 * };
	 */
	__u32 types[] = {
		/* int */
		BTF_TYPE_INT_ENC(0, BTF_INT_SIGNED, 0, 32, 4),  /* [1] */
		/* struct bpf_spin_lock */                      /* [2] */
		BTF_TYPE_ENC(1, BTF_INFO_ENC(BTF_KIND_STRUCT, 0, 1), 4),
		BTF_MEMBER_ENC(15, 1, 0), /* int val; */
		/* struct val */                                /* [3] */
		BTF_TYPE_ENC(15, BTF_INFO_ENC(BTF_KIND_STRUCT, 0, 2), 8),
		BTF_MEMBER_ENC(19, 1, 0), /* int cnt; */
		BTF_MEMBER_ENC(23, 2, 32),/* struct bpf_spin_lock l; */
	};

	return libbpf__load_raw_btf((char *)types, sizeof(types),
				     strs, sizeof(strs));
}

static int probe_map_create(enum bpf_map_type map_type)
{
	LIBBPF_OPTS(bpf_map_create_opts, opts);
	int key_size, value_size, max_entries;
	__u32 btf_key_type_id = 0, btf_value_type_id = 0;
	int fd = -1, btf_fd = -1, fd_inner = -1, exp_err = 0, err = 0;

	key_size	= sizeof(__u32);
	value_size	= sizeof(__u32);
	max_entries	= 1;

	switch (map_type) {
	case BPF_MAP_TYPE_STACK_TRACE:
		value_size	= sizeof(__u64);
		break;
	case BPF_MAP_TYPE_LPM_TRIE:
		key_size	= sizeof(__u64);
		value_size	= sizeof(__u64);
		opts.map_flags	= BPF_F_NO_PREALLOC;
		break;
	case BPF_MAP_TYPE_CGROUP_STORAGE:
	case BPF_MAP_TYPE_PERCPU_CGROUP_STORAGE:
		key_size	= sizeof(struct bpf_cgroup_storage_key);
		value_size	= sizeof(__u64);
		max_entries	= 0;
		break;
	case BPF_MAP_TYPE_QUEUE:
	case BPF_MAP_TYPE_STACK:
		key_size	= 0;
		break;
	case BPF_MAP_TYPE_SK_STORAGE:
	case BPF_MAP_TYPE_INODE_STORAGE:
	case BPF_MAP_TYPE_TASK_STORAGE:
	case BPF_MAP_TYPE_CGRP_STORAGE:
		btf_key_type_id = 1;
		btf_value_type_id = 3;
		value_size = 8;
		max_entries = 0;
		opts.map_flags = BPF_F_NO_PREALLOC;
		btf_fd = load_local_storage_btf();
		if (btf_fd < 0)
			return btf_fd;
		break;
	case BPF_MAP_TYPE_RINGBUF:
	case BPF_MAP_TYPE_USER_RINGBUF:
		key_size = 0;
		value_size = 0;
		max_entries = sysconf(_SC_PAGE_SIZE);
		break;
	case BPF_MAP_TYPE_STRUCT_OPS:
		/* we'll get -ENOTSUPP for invalid BTF type ID for struct_ops */
		opts.btf_vmlinux_value_type_id = 1;
		exp_err = -524; /* -ENOTSUPP */
		break;
	case BPF_MAP_TYPE_BLOOM_FILTER:
		key_size = 0;
		max_entries = 1;
		break;
	case BPF_MAP_TYPE_HASH:
	case BPF_MAP_TYPE_ARRAY:
	case BPF_MAP_TYPE_PROG_ARRAY:
	case BPF_MAP_TYPE_PERF_EVENT_ARRAY:
	case BPF_MAP_TYPE_PERCPU_HASH:
	case BPF_MAP_TYPE_PERCPU_ARRAY:
	case BPF_MAP_TYPE_CGROUP_ARRAY:
	case BPF_MAP_TYPE_LRU_HASH:
	case BPF_MAP_TYPE_LRU_PERCPU_HASH:
	case BPF_MAP_TYPE_ARRAY_OF_MAPS:
	case BPF_MAP_TYPE_HASH_OF_MAPS:
	case BPF_MAP_TYPE_DEVMAP:
	case BPF_MAP_TYPE_DEVMAP_HASH:
	case BPF_MAP_TYPE_SOCKMAP:
	case BPF_MAP_TYPE_CPUMAP:
	case BPF_MAP_TYPE_XSKMAP:
	case BPF_MAP_TYPE_SOCKHASH:
	case BPF_MAP_TYPE_REUSEPORT_SOCKARRAY:
		break;
	case BPF_MAP_TYPE_UNSPEC:
	default:
		return -EOPNOTSUPP;
	}

	if (map_type == BPF_MAP_TYPE_ARRAY_OF_MAPS ||
	    map_type == BPF_MAP_TYPE_HASH_OF_MAPS) {
		fd_inner = bpf_map_create(BPF_MAP_TYPE_HASH, NULL,
					  sizeof(__u32), sizeof(__u32), 1, NULL);
		if (fd_inner < 0)
			goto cleanup;

		opts.inner_map_fd = fd_inner;
	}

	if (btf_fd >= 0) {
		opts.btf_fd = btf_fd;
		opts.btf_key_type_id = btf_key_type_id;
		opts.btf_value_type_id = btf_value_type_id;
	}

	fd = bpf_map_create(map_type, NULL, key_size, value_size, max_entries, &opts);
	err = -errno;

cleanup:
	if (fd >= 0)
		close(fd);
	if (fd_inner >= 0)
		close(fd_inner);
	if (btf_fd >= 0)
		close(btf_fd);

	if (exp_err)
		return fd < 0 && err == exp_err ? 1 : 0;
	else
		return fd >= 0 ? 1 : 0;
}

int libbpf_probe_bpf_map_type(enum bpf_map_type map_type, const void *opts)
{
	int ret;

	if (opts)
		return libbpf_err(-EINVAL);

	ret = probe_map_create(map_type);
	return libbpf_err(ret);
}

int libbpf_probe_bpf_helper(enum bpf_prog_type prog_type, enum bpf_func_id helper_id,
			    const void *opts)
{
	struct bpf_insn insns[] = {
		BPF_EMIT_CALL((__u32)helper_id),
		BPF_EXIT_INSN(),
	};
	const size_t insn_cnt = ARRAY_SIZE(insns);
	char buf[4096];
	int ret;

	if (opts)
		return libbpf_err(-EINVAL);

	/* we can't successfully load all prog types to check for BPF helper
	 * support, so bail out with -EOPNOTSUPP error
	 */
	switch (prog_type) {
	case BPF_PROG_TYPE_TRACING:
	case BPF_PROG_TYPE_EXT:
	case BPF_PROG_TYPE_LSM:
	case BPF_PROG_TYPE_STRUCT_OPS:
		return -EOPNOTSUPP;
	default:
		break;
	}

	buf[0] = '\0';
	ret = probe_prog_load(prog_type, insns, insn_cnt, buf, sizeof(buf));
	if (ret < 0)
		return libbpf_err(ret);

	/* If BPF verifier doesn't recognize BPF helper ID (enum bpf_func_id)
	 * at all, it will emit something like "invalid func unknown#181".
	 * If BPF verifier recognizes BPF helper but it's not supported for
	 * given BPF program type, it will emit "unknown func bpf_sys_bpf#166".
	 * In both cases, provided combination of BPF program type and BPF
	 * helper is not supported by the kernel.
	 * In all other cases, probe_prog_load() above will either succeed (e.g.,
	 * because BPF helper happens to accept no input arguments or it
	 * accepts one input argument and initial PTR_TO_CTX is fine for
	 * that), or we'll get some more specific BPF verifier error about
	 * some unsatisfied conditions.
	 */
	if (ret == 0 && (strstr(buf, "invalid func ") || strstr(buf, "unknown func ")))
		return 0;
	return 1; /* assume supported */
}
