// SPDX-License-Identifier: GPL-2.0
/* Copyright (c) 2025 Google LLC */
#include <vmlinux.h>
#include <bpf/bpf_core_read.h>
#include <bpf/bpf_helpers.h>

/* From uapi/linux/dma-buf.h */
#define DMA_BUF_NAME_LEN 32

char _license[] SEC("license") = "GPL";

/*
 * Fields output by this iterator are delimited by newlines. Convert any
 * newlines in user-provided printed strings to spaces.
 */
static void sanitize_string(char *src, size_t size)
{
	for (char *c = src; (size_t)(c - src) < size && *c; ++c)
		if (*c == '\n')
			*c = ' ';
}

SEC("iter/dmabuf")
int dmabuf_collector(struct bpf_iter__dmabuf *ctx)
{
	const struct dma_buf *dmabuf = ctx->dmabuf;
	struct seq_file *seq = ctx->meta->seq;
	unsigned long inode = 0;
	size_t size;
	const char *pname, *exporter;
	char name[DMA_BUF_NAME_LEN] = {'\0'};

	if (!dmabuf)
		return 0;

	if (BPF_CORE_READ_INTO(&inode, dmabuf, file, f_inode, i_ino) ||
	    bpf_core_read(&size, sizeof(size), &dmabuf->size) ||
	    bpf_core_read(&pname, sizeof(pname), &dmabuf->name) ||
	    bpf_core_read(&exporter, sizeof(exporter), &dmabuf->exp_name))
		return 1;

	/* Buffers are not required to be named */
	if (pname) {
		if (bpf_probe_read_kernel(name, sizeof(name), pname))
			return 1;

		/* Name strings can be provided by userspace */
		sanitize_string(name, sizeof(name));
	}

	BPF_SEQ_PRINTF(seq, "%lu\n%llu\n%s\n%s\n", inode, size, name, exporter);
	return 0;
}
