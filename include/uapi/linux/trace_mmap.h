/* SPDX-License-Identifier: GPL-2.0 WITH Linux-syscall-note */
#ifndef _TRACE_MMAP_H_
#define _TRACE_MMAP_H_

#include <linux/types.h>

/**
 * struct trace_buffer_meta - Ring-buffer Meta-page description
 * @meta_page_size:	Size of this meta-page.
 * @meta_struct_len:	Size of this structure.
 * @subbuf_size:	Size of each sub-buffer.
 * @nr_subbufs:		Number of subbfs in the ring-buffer, including the reader.
 * @reader.lost_events:	Number of events lost at the time of the reader swap.
 * @reader.id:		subbuf ID of the current reader. ID range [0 : @nr_subbufs - 1]
 * @reader.read:	Number of bytes read on the reader subbuf.
 * @flags:		Placeholder for now, 0 until new features are supported.
 * @entries:		Number of entries in the ring-buffer.
 * @overrun:		Number of entries lost in the ring-buffer.
 * @read:		Number of entries that have been read.
 * @Reserved1:		Internal use only.
 * @Reserved2:		Internal use only.
 */
struct trace_buffer_meta {
	__u32		meta_page_size;
	__u32		meta_struct_len;

	__u32		subbuf_size;
	__u32		nr_subbufs;

	struct {
		__u64	lost_events;
		__u32	id;
		__u32	read;
	} reader;

	__u64	flags;

	__u64	entries;
	__u64	overrun;
	__u64	read;

	__u64	Reserved1;
	__u64	Reserved2;
};

#define TRACE_MMAP_IOCTL_GET_READER		_IO('R', 0x20)

#endif /* _TRACE_MMAP_H_ */
