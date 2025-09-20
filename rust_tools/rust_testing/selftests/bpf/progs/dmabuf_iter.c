// SPDX-License-Identifier: GPL-2.0
/* Copyright (c) 2025 Google LLC */
#include <vmlinux.h>
#include <bpf/bpf_core_read.h>
#include <bpf/bpf_helpers.h>

/* From uapi/linux/dma-buf.h */
#define DMA_BUF_NAME_LEN 32

char _license[] SEC("license") = "GPL";

struct {
	__uint(type, BPF_MAP_TYPE_HASH);
	__uint(key_size, DMA_BUF_NAME_LEN);
	__type(value, bool);
	__uint(max_entries, 5);
} testbuf_hash SEC(".maps");

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

SEC("syscall")
int iter_dmabuf_for_each(const void *ctx)
{
	struct dma_buf *d;

	bpf_for_each(dmabuf, d) {
		char name[DMA_BUF_NAME_LEN];
		const char *pname;
		bool *found;
		long len;
		int i;

		if (bpf_core_read(&pname, sizeof(pname), &d->name))
			return 1;

		/* Buffers are not required to be named */
		if (!pname)
			continue;

		len = bpf_probe_read_kernel_str(name, sizeof(name), pname);
		if (len < 0)
			return 1;

		/*
		 * The entire name buffer is used as a map key.
		 * Zeroize any uninitialized trailing bytes after the NUL.
		 */
		bpf_for(i, len, DMA_BUF_NAME_LEN)
			name[i] = 0;

		found = bpf_map_lookup_elem(&testbuf_hash, name);
		if (found) {
			bool t = true;

			bpf_map_update_elem(&testbuf_hash, name, &t, BPF_EXIST);
		}
	}

	return 0;
}
