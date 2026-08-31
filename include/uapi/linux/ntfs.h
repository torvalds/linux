/* SPDX-License-Identifier: GPL-2.0 WITH Linux-syscall-note */
/*
 * Copyright (c) 2026 LG Electronics Co., Ltd.
 */

#ifndef _UAPI_LINUX_NTFS_H
#define _UAPI_LINUX_NTFS_H
#include <linux/types.h>
#include <linux/ioctl.h>

#define NTFS_IOC_MAGIC	0xEF

/*
 * Flags for ntfs_stream.flags and ntfs_list_streams.flags.
 *
 * NTFS_STREAM_FL_UTF16 makes stream names raw UTF-16LE, exactly as stored on
 * disk, instead of encoding them with the mounted filesystem NLS. This
 * allows lossless round-tripping of stream names whose characters are not
 * representable by the mount NLS (e.g. for Wine, which works in UTF-16
 * natively). When set, the name length fields count bytes of UTF-16LE and
 * must therefore be even.
 */
#define NTFS_STREAM_FL_UTF16		0x1

/*
 * ntfs named stream read, write, and remove ioctl structure.
 *
 * @stream_offset:	Offset within the named stream for read/write.
 * @io_len:		Number of bytes to read/write. Must be zero for remove.
 * @bytes_returned:	Actual bytes transferred on success (out).
 * @name_len:		Stream name length in bytes, not including any
 *			terminating NUL. When NTFS_STREAM_FL_UTF16 is set,
 *			this counts UTF-16LE bytes and must be even.
 * @flags:		Bit mask of NTFS_STREAM_FL_* flags. Other bits must
 *			be zero.
 * @reserved:		Must be zero.
 * @buffer:		Bare stream name followed by stream data for read/write.
 *
 * The stream name is encoded with the mounted filesystem NLS, or as raw
 * UTF-16LE when NTFS_STREAM_FL_UTF16 is set. A write creates the stream if
 * it does not already exist. A write failure after creation may leave the
 * new stream behind; it can be removed separately.
 *
 * For NTFS_IOC_STREAM_READ, a successful short read indicates EOF. If a read
 * fails, the ioctl returns an error and copies no stream data to userspace.
 *
 * NTFS_IOC_STREAM_WRITE is not atomic. Write errors may leave partial data
 * changes and an extended stream size. On error, bytes_returned is not valid.
 * The ioctl does not report the completed byte count.
 */
struct ntfs_stream {
	__aligned_u64 stream_offset;
	__aligned_u64 io_len;
	__aligned_u64 bytes_returned;
	__u32 name_len;
	__u32 flags;
	__aligned_u64 reserved;
	__u8 buffer[];
};

/*
 * Single stream entry returned by NTFS_IOC_LIST_STREAMS.
 *
 * @next_entry_off:	Byte offset from the start of this entry to the next
 *			entry, or zero for the last entry. Always a multiple
 *			of 8. Consumers must use this to advance instead of
 *			computing the stride themselves.
 * @size:		Stream data size in bytes.
 * @alloc_size:		Bytes allocated for the stream (cluster aligned for
 *			non-resident streams).
 * @name_len:		Stream name length in bytes, not including a NUL
 *			terminator (none is stored). Counts UTF-16LE bytes
 *			when NTFS_STREAM_FL_UTF16 was requested.
 * @name_offset:	Byte offset from the start of this entry to @name.
 * @reserved:		Must be zero.
 * @name:		Bare stream name; NLS encoded, or raw UTF-16LE when
 *			NTFS_STREAM_FL_UTF16 was requested.
 *
 * New fixed fields may be added after @reserved and before @name. Consumers
 * must use @name_offset to locate the name and @next_entry_off to advance to
 * the next entry.
 */
struct ntfs_stream_entry {
	__aligned_u64 next_entry_off;
	__aligned_u64 size;
	__aligned_u64 alloc_size;
	__u32 name_len;
	__u32 name_offset;
	__u32 reserved;
	__u8 name[];
};

/*
 * ntfs list streams ioctl structure.
 *
 * @buffer_size:	user buffer size(in).
 * @bytes_returned:	actual bytes written or required(out).
 * @stream_count:	number of streams(out).
 * @flags:		Bit mask of NTFS_STREAM_FL_* flags controlling the
 *			encoding of the returned names; other bits must be zero.
 * @reserved:		Must be zero.
 * @buffer:		ntfs_stream_entry array.
 *
 * The variable-length entries follow the header in @buffer, each aligned on
 * an 8-byte boundary and chained via ntfs_stream_entry.next_entry_off. If
 * @buffer_size is too small, no data is copied, @stream_count and
 * @bytes_returned report the required values and the ioctl fails with
 * -ENOSPC. In NLS mode, names that cannot be represented by the mounted
 * character set are omitted; use NTFS_STREAM_FL_UTF16 to list all
 * names without conversion loss.
 */
struct ntfs_list_streams {
	__aligned_u64 buffer_size;
	__aligned_u64 bytes_returned;
	__aligned_u64 stream_count;
	__u32 flags;
	__u32 reserved;
	__u8 buffer[];
};

#define NTFS_IOC_STREAM_READ \
	_IOWR(NTFS_IOC_MAGIC, 1, struct ntfs_stream)
#define NTFS_IOC_STREAM_WRITE \
	_IOWR(NTFS_IOC_MAGIC, 2, struct ntfs_stream)
#define NTFS_IOC_STREAM_REMOVE \
	_IOWR(NTFS_IOC_MAGIC, 3, struct ntfs_stream)
#define NTFS_IOC_LIST_STREAMS \
	_IOWR(NTFS_IOC_MAGIC, 4, struct ntfs_list_streams)

#endif /* _UAPI_LINUX_NTFS_H */
