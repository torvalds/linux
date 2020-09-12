/* Copyright (c) 2015 PLUMgrid, http://plumgrid.com
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of version 2 of the GNU General Public
 * License as published by the Free Software Foundation.
 */
#include <linux/ptrace.h>
#include <linux/version.h>
#include <uapi/linux/bpf.h>
#include <uapi/linux/seccomp.h>
#include <uapi/linux/unistd.h>
#include "syscall_nrs.h"
#include <bpf/bpf_helpers.h>
#include <bpf/bpf_tracing.h>

#define PROG(F) SEC("kprobe/"__stringify(F)) int bpf_func_##F

struct {
	__uint(type, BPF_MAP_TYPE_PROG_ARRAY);
	__uint(key_size, sizeof(u32));
	__uint(value_size, sizeof(u32));
#ifdef __mips__
	__uint(max_entries, 6000); /* MIPS n64 syscalls start at 5000 */
#else
	__uint(max_entries, 1024);
#endif
} progs SEC(".maps");

SEC("kprobe/__seccomp_filter")
int bpf_prog1(struct pt_regs *ctx)
{
	int sc_nr = (int)PT_REGS_PARM1(ctx);

	/* dispatch into next BPF program depending on syscall number */
	bpf_tail_call(ctx, &progs, sc_nr);

	/* fall through -> unknown syscall */
	if (sc_nr >= __NR_getuid && sc_nr <= __NR_getsid) {
		char fmt[] = "syscall=%d (one of get/set uid/pid/gid)\n";
		bpf_trace_printk(fmt, sizeof(fmt), sc_nr);
	}
	return 0;
}

/* we jump here when syscall number == __NR_write */
PROG(SYS__NR_write)(struct pt_regs *ctx)
{
	struct seccomp_data sd;

	bpf_probe_read_kernel(&sd, sizeof(sd), (void *)PT_REGS_PARM2(ctx));
	if (sd.args[2] == 512) {
		char fmt[] = "write(fd=%d, buf=%p, size=%d)\n";
		bpf_trace_printk(fmt, sizeof(fmt),
				 sd.args[0], sd.args[1], sd.args[2]);
	}
	return 0;
}

PROG(SYS__NR_read)(struct pt_regs *ctx)
{
	struct seccomp_data sd;

	bpf_probe_read_kernel(&sd, sizeof(sd), (void *)PT_REGS_PARM2(ctx));
	if (sd.args[2] > 128 && sd.args[2] <= 1024) {
		char fmt[] = "read(fd=%d, buf=%p, size=%d)\n";
		bpf_trace_printk(fmt, sizeof(fmt),
				 sd.args[0], sd.args[1], sd.args[2]);
	}
	return 0;
}

#ifdef __NR_mmap2
PROG(SYS__NR_mmap2)(struct pt_regs *ctx)
{
	char fmt[] = "mmap2\n";

	bpf_trace_printk(fmt, sizeof(fmt));
	return 0;
}
#endif

#ifdef __NR_mmap
PROG(SYS__NR_mmap)(struct pt_regs *ctx)
{
	char fmt[] = "mmap\n";

	bpf_trace_printk(fmt, sizeof(fmt));
	return 0;
}
#endif

char _license[] SEC("license") = "GPL";
u32 _version SEC("version") = LINUX_VERSION_CODE;
