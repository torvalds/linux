/* SPDX-License-Identifier: GPL-2.0 WITH Linux-syscall-note */

#ifndef __QCOM_FASTRPC_H__
#define __QCOM_FASTRPC_H__

#include <linux/types.h>

#define FASTRPC_IOCTL_ALLOC_DMA_BUFF	_IOWR('R', 1, struct fastrpc_alloc_dma_buf)
#define FASTRPC_IOCTL_FREE_DMA_BUFF	_IOWR('R', 2, __u32)
#define FASTRPC_IOCTL_INVOKE		_IOWR('R', 3, struct fastrpc_invoke)
#define FASTRPC_IOCTL_INIT_ATTACH	_IO('R', 4)
#define FASTRPC_IOCTL_INIT_CREATE	_IOWR('R', 5, struct fastrpc_init_create)
#define FASTRPC_IOCTL_MMAP		_IOWR('R', 6, struct fastrpc_req_mmap)
#define FASTRPC_IOCTL_MUNMAP		_IOWR('R', 7, struct fastrpc_req_munmap)
#define FASTRPC_IOCTL_INIT_ATTACH_SNS	_IO('R', 8)

struct fastrpc_invoke_args {
	__u64 ptr;
	__u64 length;
	__s32 fd;
	__u32 reserved;
};

struct fastrpc_invoke {
	__u32 handle;
	__u32 sc;
	__u64 args;
};

struct fastrpc_init_create {
	__u32 filelen;	/* elf file length */
	__s32 filefd;	/* fd for the file */
	__u32 attrs;
	__u32 siglen;
	__u64 file;	/* pointer to elf file */
};

struct fastrpc_alloc_dma_buf {
	__s32 fd;	/* fd */
	__u32 flags;	/* flags to map with */
	__u64 size;	/* size */
};

struct fastrpc_req_mmap {
	__s32 fd;
	__u32 flags;	/* flags for dsp to map with */
	__u64 vaddrin;	/* optional virtual address */
	__u64 size;	/* size */
	__u64 vaddrout;	/* dsp virtual address */
};

struct fastrpc_req_munmap {
	__u64 vaddrout;	/* address to unmap */
	__u64 size;	/* size */
};

#endif /* __QCOM_FASTRPC_H__ */
