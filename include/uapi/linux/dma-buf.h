/* SPDX-License-Identifier: GPL-2.0 WITH Linux-syscall-note */
/*
 * Framework for buffer objects that can be shared across devices/subsystems.
 *
 * Copyright(C) 2015 Intel Ltd
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License version 2 as published by
 * the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License for
 * more details.
 *
 * You should have received a copy of the GNU General Public License along with
 * this program.  If not, see <http://www.gnu.org/licenses/>.
 */

#ifndef _DMA_BUF_UAPI_H_
#define _DMA_BUF_UAPI_H_

#include <linux/types.h>

/**
 * struct dma_buf_sync - Synchronize with CPU access.
 *
 * When a DMA buffer is accessed from the CPU via mmap, it is not always
 * possible to guarantee coherency between the CPU-visible map and underlying
 * memory.  To manage coherency, DMA_BUF_IOCTL_SYNC must be used to bracket
 * any CPU access to give the kernel the chance to shuffle memory around if
 * needed.
 *
 * Prior to accessing the map, the client must call DMA_BUF_IOCTL_SYNC
 * with DMA_BUF_SYNC_START and the appropriate read/write flags.  Once the
 * access is complete, the client should call DMA_BUF_IOCTL_SYNC with
 * DMA_BUF_SYNC_END and the same read/write flags.
 *
 * The synchronization provided via DMA_BUF_IOCTL_SYNC only provides cache
 * coherency.  It does not prevent other processes or devices from
 * accessing the memory at the same time.  If synchronization with a GPU or
 * other device driver is required, it is the client's responsibility to
 * wait for buffer to be ready for reading or writing before calling this
 * ioctl with DMA_BUF_SYNC_START.  Likewise, the client must ensure that
 * follow-up work is not submitted to GPU or other device driver until
 * after this ioctl has been called with DMA_BUF_SYNC_END?
 *
 * If the driver or API with which the client is interacting uses implicit
 * synchronization, waiting for prior work to complete can be done via
 * poll() on the DMA buffer file descriptor.  If the driver or API requires
 * explicit synchronization, the client may have to wait on a sync_file or
 * other synchronization primitive outside the scope of the DMA buffer API.
 */
struct dma_buf_sync {
	/**
	 * @flags: Set of access flags
	 *
	 * DMA_BUF_SYNC_START:
	 *     Indicates the start of a map access session.
	 *
	 * DMA_BUF_SYNC_END:
	 *     Indicates the end of a map access session.
	 *
	 * DMA_BUF_SYNC_READ:
	 *     Indicates that the mapped DMA buffer will be read by the
	 *     client via the CPU map.
	 *
	 * DMA_BUF_SYNC_WRITE:
	 *     Indicates that the mapped DMA buffer will be written by the
	 *     client via the CPU map.
	 *
	 * DMA_BUF_SYNC_RW:
	 *     An alias for DMA_BUF_SYNC_READ | DMA_BUF_SYNC_WRITE.
	 */
	__u64 flags;
};

#define DMA_BUF_SYNC_READ      (1 << 0)
#define DMA_BUF_SYNC_WRITE     (2 << 0)
#define DMA_BUF_SYNC_RW        (DMA_BUF_SYNC_READ | DMA_BUF_SYNC_WRITE)
#define DMA_BUF_SYNC_START     (0 << 2)
#define DMA_BUF_SYNC_END       (1 << 2)
#define DMA_BUF_SYNC_VALID_FLAGS_MASK \
	(DMA_BUF_SYNC_RW | DMA_BUF_SYNC_END)

#define DMA_BUF_NAME_LEN	32

/**
 * struct dma_buf_export_sync_file - Get a sync_file from a dma-buf
 *
 * Userspace can perform a DMA_BUF_IOCTL_EXPORT_SYNC_FILE to retrieve the
 * current set of fences on a dma-buf file descriptor as a sync_file.  CPU
 * waits via poll() or other driver-specific mechanisms typically wait on
 * whatever fences are on the dma-buf at the time the wait begins.  This
 * is similar except that it takes a snapshot of the current fences on the
 * dma-buf for waiting later instead of waiting immediately.  This is
 * useful for modern graphics APIs such as Vulkan which assume an explicit
 * synchronization model but still need to inter-operate with dma-buf.
 *
 * The intended usage pattern is the following:
 *
 *  1. Export a sync_file with flags corresponding to the expected GPU usage
 *     via DMA_BUF_IOCTL_EXPORT_SYNC_FILE.
 *
 *  2. Submit rendering work which uses the dma-buf.  The work should wait on
 *     the exported sync file before rendering and produce another sync_file
 *     when complete.
 *
 *  3. Import the rendering-complete sync_file into the dma-buf with flags
 *     corresponding to the GPU usage via DMA_BUF_IOCTL_IMPORT_SYNC_FILE.
 *
 * Unlike doing implicit synchronization via a GPU kernel driver's exec ioctl,
 * the above is not a single atomic operation.  If userspace wants to ensure
 * ordering via these fences, it is the respnosibility of userspace to use
 * locks or other mechanisms to ensure that no other context adds fences or
 * submits work between steps 1 and 3 above.
 */
struct dma_buf_export_sync_file {
	/**
	 * @flags: Read/write flags
	 *
	 * Must be DMA_BUF_SYNC_READ, DMA_BUF_SYNC_WRITE, or both.
	 *
	 * If DMA_BUF_SYNC_READ is set and DMA_BUF_SYNC_WRITE is not set,
	 * the returned sync file waits on any writers of the dma-buf to
	 * complete.  Waiting on the returned sync file is equivalent to
	 * poll() with POLLIN.
	 *
	 * If DMA_BUF_SYNC_WRITE is set, the returned sync file waits on
	 * any users of the dma-buf (read or write) to complete.  Waiting
	 * on the returned sync file is equivalent to poll() with POLLOUT.
	 * If both DMA_BUF_SYNC_WRITE and DMA_BUF_SYNC_READ are set, this
	 * is equivalent to just DMA_BUF_SYNC_WRITE.
	 */
	__u32 flags;
	/** @fd: Returned sync file descriptor */
	__s32 fd;
};

/**
 * struct dma_buf_import_sync_file - Insert a sync_file into a dma-buf
 *
 * Userspace can perform a DMA_BUF_IOCTL_IMPORT_SYNC_FILE to insert a
 * sync_file into a dma-buf for the purposes of implicit synchronization
 * with other dma-buf consumers.  This allows clients using explicitly
 * synchronized APIs such as Vulkan to inter-op with dma-buf consumers
 * which expect implicit synchronization such as OpenGL or most media
 * drivers/video.
 */
struct dma_buf_import_sync_file {
	/**
	 * @flags: Read/write flags
	 *
	 * Must be DMA_BUF_SYNC_READ, DMA_BUF_SYNC_WRITE, or both.
	 *
	 * If DMA_BUF_SYNC_READ is set and DMA_BUF_SYNC_WRITE is not set,
	 * this inserts the sync_file as a read-only fence.  Any subsequent
	 * implicitly synchronized writes to this dma-buf will wait on this
	 * fence but reads will not.
	 *
	 * If DMA_BUF_SYNC_WRITE is set, this inserts the sync_file as a
	 * write fence.  All subsequent implicitly synchronized access to
	 * this dma-buf will wait on this fence.
	 */
	__u32 flags;
	/** @fd: Sync file descriptor */
	__s32 fd;
};

#define DMA_BUF_BASE		'b'
#define DMA_BUF_IOCTL_SYNC	_IOW(DMA_BUF_BASE, 0, struct dma_buf_sync)

/* 32/64bitness of this uapi was botched in android, there's no difference
 * between them in actual uapi, they're just different numbers.
 */
#define DMA_BUF_SET_NAME	_IOW(DMA_BUF_BASE, 1, const char *)
#define DMA_BUF_SET_NAME_A	_IOW(DMA_BUF_BASE, 1, __u32)
#define DMA_BUF_SET_NAME_B	_IOW(DMA_BUF_BASE, 1, __u64)
#define DMA_BUF_IOCTL_EXPORT_SYNC_FILE	_IOWR(DMA_BUF_BASE, 2, struct dma_buf_export_sync_file)
#define DMA_BUF_IOCTL_IMPORT_SYNC_FILE	_IOW(DMA_BUF_BASE, 3, struct dma_buf_import_sync_file)

#endif
