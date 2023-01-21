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

#include "bpf.h"
#include "libbpf.h"
#include "libbpf_internal.h"

static bool grep(const char *buffer, const char *pattern)
{
	return !!strstr(buffer, pattern);
}

static int get_vendor_id(int ifindex)
{
	char ifname[IF_NAMESIZE], path[64], buf[8];
	ssize_t len;
	int fd;

	if (!if_indextoname(ifindex, ifname))
		return -1;

	snprintf(path, sizeof(path), "/sys/class/net/%s/device/vendor", ifname);

	fd = open(path, O_RDONLY);
	if (fd < 0)
		return -1;

	len = read(fd, buf, sizeof(buf));
	close(fd);
	if (len < 0)
		return -1;
	if (len >= (ssize_t)sizeof(buf))
		return -1;
	buf[len] = '\0';

	return strtol(buf, NULL, 0);
}

static int get_kernel_version(void)
{
	int version, subversion, patchlevel;
	struct utsname utsn;

	/* Return 0 on failure, and attempt to probe with empty kversion */
	if (uname(&utsn))
		return 0;

	if (sscanf(utsn.release, "%d.%d.%d",
		   &version, &subversion, &patchlevel) != 3)
		return 0;

	return (version << 16) + (subversion << 8) + patchlevel;
}

static void
probe_load(enum bpf_prog_type prog_type, const struct bpf_insn *insns,
	   size_t insns_cnt, char *buf, size_t buf_len, __u32 ifindex)
{
	struct bpf_load_program_attr xattr = {};
	int fd;

	switch (prog_type) {
	case BPF_PROG_TYPE_CGROUP_SOCK_ADDR:
		xattr.expected_attach_type = BPF_CGROUP_INET4_CONNECT;
		break;
	case BPF_PROG_TYPE_CGROUP_SOCKOPT:
		xattr.expected_attach_type = BPF_CGROUP_GETSOCKOPT;
		break;
	case BPF_PROG_TYPE_SK_LOOKUP:
		xattr.expected_attach_type = BPF_SK_LOOKUP;
		break;
	case BPF_PROG_TYPE_KPROBE:
		xattr.kern_version = get_kernel_version();
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
	case BPF_PROG_TYPE_LIRC_MODE2:
	case BPF_PROG_TYPE_SK_REUSEPORT:
	case BPF_PROG_TYPE_FLOW_DISSECTOR:
	case BPF_PROG_TYPE_CGROUP_SYSCTL:
	case BPF_PROG_TYPE_TRACING:
	case BPF_PROG_TYPE_STRUCT_OPS:
	case BPF_PROG_TYPE_EXT:
	case BPF_PROG_TYPE_LSM:
	default:
		break;
	}

	xattr.prog_type = prog_type;
	xattr.insns = insns;
	xattr.insns_cnt = insns_cnt;
	xattr.license = "GPL";
	xattr.prog_ifindex = ifindex;

	fd = bpf_load_program_xattr(&xattr, buf, buf_len);
	if (fd >= 0)
		close(fd);
}

bool bpf_probe_prog_type(enum bpf_prog_type prog_type, __u32 ifindex)
{
	struct bpf_insn insns[2] = {
		BPF_MOV64_IMM(BPF_REG_0, 0),
		BPF_EXIT_INSN()
	};

	if (ifindex && prog_type == BPF_PROG_TYPE_SCHED_CLS)
		/* nfp returns -EINVAL on exit(0) with TC offload */
		insns[0].imm = 2;

	errno = 0;
	probe_load(prog_type, insns, ARRAY_SIZE(insns), NULL, 0, ifindex);

	return errno != EINVAL && errno != EOPNOTSUPP;
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

	btf_fd = bpf_load_btf(raw_btf, btf_len, NULL, 0, false);

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

bool bpf_probe_map_type(enum bpf_map_type map_type, __u32 ifindex)
{
	int key_size, value_size, max_entries, map_flags;
	__u32 btf_key_type_id = 0, btf_value_type_id = 0;
	struct bpf_create_map_attr attr = {};
	int fd = -1, btf_fd = -1, fd_inner;

	key_size	= sizeof(__u32);
	value_size	= sizeof(__u32);
	max_entries	= 1;
	map_flags	= 0;

	switch (map_type) {
	case BPF_MAP_TYPE_STACK_TRACE:
		value_size	= sizeof(__u64);
		break;
	case BPF_MAP_TYPE_LPM_TRIE:
		key_size	= sizeof(__u64);
		value_size	= sizeof(__u64);
		map_flags	= BPF_F_NO_PREALLOC;
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
		btf_key_type_id = 1;
		btf_value_type_id = 3;
		value_size = 8;
		max_entries = 0;
		map_flags = BPF_F_NO_PREALLOC;
		btf_fd = load_local_storage_btf();
		if (btf_fd < 0)
			return false;
		break;
	case BPF_MAP_TYPE_RINGBUF:
		key_size = 0;
		value_size = 0;
		max_entries = sysconf(_SC_PAGE_SIZE);
		break;
	case BPF_MAP_TYPE_UNSPEC:
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
	case BPF_MAP_TYPE_STRUCT_OPS:
	default:
		break;
	}

	if (map_type == BPF_MAP_TYPE_ARRAY_OF_MAPS ||
	    map_type == BPF_MAP_TYPE_HASH_OF_MAPS) {
		/* TODO: probe for device, once libbpf has a function to create
		 * map-in-map for offload
		 */
		if (ifindex)
			return false;

		fd_inner = bpf_create_map(BPF_MAP_TYPE_HASH,
					  sizeof(__u32), sizeof(__u32), 1, 0);
		if (fd_inner < 0)
			return false;
		fd = bpf_create_map_in_map(map_type, NULL, sizeof(__u32),
					   fd_inner, 1, 0);
		close(fd_inner);
	} else {
		/* Note: No other restriction on map type probes for offload */
		attr.map_type = map_type;
		attr.key_size = key_size;
		attr.value_size = value_size;
		attr.max_entries = max_entries;
		attr.map_flags = map_flags;
		attr.map_ifindex = ifindex;
		if (btf_fd >= 0) {
			attr.btf_fd = btf_fd;
			attr.btf_key_type_id = btf_key_type_id;
			attr.btf_value_type_id = btf_value_type_id;
		}

		fd = bpf_create_map_xattr(&attr);
	}
	if (fd >= 0)
		close(fd);
	if (btf_fd >= 0)
		close(btf_fd);

	return fd >= 0;
}

bool bpf_probe_helper(enum bpf_func_id id, enum bpf_prog_type prog_type,
		      __u32 ifindex)
{
	struct bpf_insn insns[2] = {
		BPF_EMIT_CALL(id),
		BPF_EXIT_INSN()
	};
	char buf[4096] = {};
	bool res;

	probe_load(prog_type, insns, ARRAY_SIZE(insns), buf, sizeof(buf),
		   ifindex);
	res = !grep(buf, "invalid func ") && !grep(buf, "unknown func ");

	if (ifindex) {
		switch (get_vendor_id(ifindex)) {
		case 0x19ee: /* Netronome specific */
			res = res && !grep(buf, "not supported by FW") &&
				!grep(buf, "unsupported function id");
			break;
		default:
			break;
		}
	}

	return res;
}

/*
 * Probe for availability of kernel commit (5.3):
 *
 * c04c0d2b968a ("bpf: increase complexity limit and maximum program size")
 */
bool bpf_probe_large_insn_limit(__u32 ifindex)
{
	struct bpf_insn insns[BPF_MAXINSNS + 1];
	int i;

	for (i = 0; i < BPF_MAXINSNS; i++)
		insns[i] = BPF_MOV64_IMM(BPF_REG_0, 1);
	insns[BPF_MAXINSNS] = BPF_EXIT_INSN();

	errno = 0;
	probe_load(BPF_PROG_TYPE_SCHED_CLS, insns, ARRAY_SIZE(insns), NULL, 0,
		   ifindex);

	return errno != E2BIG && errno != EINVAL;
}
