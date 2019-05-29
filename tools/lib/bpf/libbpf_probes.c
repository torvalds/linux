// SPDX-License-Identifier: (LGPL-2.1 OR BSD-2-Clause)
/* Copyright (c) 2019 Netronome Systems, Inc. */

#include <errno.h>
#include <unistd.h>
#include <sys/utsname.h>

#include <linux/filter.h>
#include <linux/kernel.h>

#include "bpf.h"
#include "libbpf.h"

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
	case BPF_PROG_TYPE_LWT_SEG6LOCAL:
	case BPF_PROG_TYPE_LIRC_MODE2:
	case BPF_PROG_TYPE_SK_REUSEPORT:
	case BPF_PROG_TYPE_FLOW_DISSECTOR:
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
