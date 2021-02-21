// SPDX-License-Identifier: GPL-2.0
/* Copyright (c) 2020 Facebook */
#include "bpf_iter.h"
#include <bpf/bpf_helpers.h>
#include <bpf/bpf_tracing.h>

char _license[] SEC("license") = "GPL";

/* Copied from mm.h */
#define VM_READ		0x00000001
#define VM_WRITE	0x00000002
#define VM_EXEC		0x00000004
#define VM_MAYSHARE	0x00000080

/* Copied from kdev_t.h */
#define MINORBITS	20
#define MINORMASK	((1U << MINORBITS) - 1)
#define MAJOR(dev)	((unsigned int) ((dev) >> MINORBITS))
#define MINOR(dev)	((unsigned int) ((dev) & MINORMASK))

#define D_PATH_BUF_SIZE 1024
char d_path_buf[D_PATH_BUF_SIZE] = {};
__u32 pid = 0;

SEC("iter/task_vma") int proc_maps(struct bpf_iter__task_vma *ctx)
{
	struct vm_area_struct *vma = ctx->vma;
	struct seq_file *seq = ctx->meta->seq;
	struct task_struct *task = ctx->task;
	struct file *file;
	char perm_str[] = "----";

	if (task == (void *)0 || vma == (void *)0)
		return 0;

	file = vma->vm_file;
	if (task->tgid != pid)
		return 0;
	perm_str[0] = (vma->vm_flags & VM_READ) ? 'r' : '-';
	perm_str[1] = (vma->vm_flags & VM_WRITE) ? 'w' : '-';
	perm_str[2] = (vma->vm_flags & VM_EXEC) ? 'x' : '-';
	perm_str[3] = (vma->vm_flags & VM_MAYSHARE) ? 's' : 'p';
	BPF_SEQ_PRINTF(seq, "%08llx-%08llx %s ", vma->vm_start, vma->vm_end, perm_str);

	if (file) {
		__u32 dev = file->f_inode->i_sb->s_dev;

		bpf_d_path(&file->f_path, d_path_buf, D_PATH_BUF_SIZE);

		BPF_SEQ_PRINTF(seq, "%08llx ", vma->vm_pgoff << 12);
		BPF_SEQ_PRINTF(seq, "%02x:%02x %u", MAJOR(dev), MINOR(dev),
			       file->f_inode->i_ino);
		BPF_SEQ_PRINTF(seq, "\t%s\n", d_path_buf);
	} else {
		BPF_SEQ_PRINTF(seq, "%08llx 00:00 0\n", 0ULL);
	}
	return 0;
}
