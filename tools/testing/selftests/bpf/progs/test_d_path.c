// SPDX-License-Identifier: GPL-2.0

#include "vmlinux.h"
#include <bpf/bpf_helpers.h>
#include <bpf/bpf_tracing.h>

#define MAX_PATH_LEN		128
#define MAX_FILES		7

pid_t my_pid = 0;
__u32 cnt_stat = 0;
__u32 cnt_close = 0;
char paths_stat[MAX_FILES][MAX_PATH_LEN] = {};
char paths_close[MAX_FILES][MAX_PATH_LEN] = {};
int rets_stat[MAX_FILES] = {};
int rets_close[MAX_FILES] = {};

int called_stat = 0;
int called_close = 0;

SEC("fentry/security_inode_getattr")
int BPF_PROG(prog_stat, struct path *path, struct kstat *stat,
	     __u32 request_mask, unsigned int query_flags)
{
	pid_t pid = bpf_get_current_pid_tgid() >> 32;
	__u32 cnt = cnt_stat;
	int ret;

	called_stat = 1;

	if (pid != my_pid)
		return 0;

	if (cnt >= MAX_FILES)
		return 0;
	ret = bpf_d_path(path, paths_stat[cnt], MAX_PATH_LEN);

	rets_stat[cnt] = ret;
	cnt_stat++;
	return 0;
}

SEC("fentry/filp_close")
int BPF_PROG(prog_close, struct file *file, void *id)
{
	pid_t pid = bpf_get_current_pid_tgid() >> 32;
	__u32 cnt = cnt_close;
	int ret;

	called_close = 1;

	if (pid != my_pid)
		return 0;

	if (cnt >= MAX_FILES)
		return 0;
	ret = bpf_d_path(&file->f_path,
			 paths_close[cnt], MAX_PATH_LEN);

	rets_close[cnt] = ret;
	cnt_close++;
	return 0;
}

char _license[] SEC("license") = "GPL";
