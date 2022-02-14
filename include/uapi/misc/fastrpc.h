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
#define FASTRPC_IOCTL_MEM_MAP		_IOWR('R', 10, struct fastrpc_mem_map)
#define FASTRPC_IOCTL_MEM_UNMAP		_IOWR('R', 11, struct fastrpc_mem_unmap)
#define FASTRPC_IOCTL_GET_DSP_INFO	_IOWR('R', 13, struct fastrpc_ioctl_capability)

/**
 * enum fastrpc_map_flags - control flags for mapping memory on DSP user process
 * @FASTRPC_MAP_STATIC: Map memory pages with RW- permission and CACHE WRITEBACK.
 * The driver is responsible for cache maintenance when passed
 * the buffer to FastRPC calls. Same virtual address will be
 * assigned for subsequent FastRPC calls.
 * @FASTRPC_MAP_RESERVED: Reserved
 * @FASTRPC_MAP_FD: Map memory pages with RW- permission and CACHE WRITEBACK.
 * Mapping tagged with a file descriptor. User is responsible for
 * CPU and DSP cache maintenance for the buffer. Get virtual address
 * of buffer on DSP using HAP_mmap_get() and HAP_mmap_put() APIs.
 * @FASTRPC_MAP_FD_DELAYED: Mapping delayed until user call HAP_mmap() and HAP_munmap()
 * functions on DSP. It is useful to map a buffer with cache modes
 * other than default modes. User is responsible for CPU and DSP
 * cache maintenance for the buffer.
 * @FASTRPC_MAP_FD_NOMAP: This flag is used to skip CPU mapping,
 * otherwise behaves similar to FASTRPC_MAP_FD_DELAYED flag.
 * @FASTRPC_MAP_MAX: max count for flags
 *
 */
enum fastrpc_map_flags {
	FASTRPC_MAP_STATIC = 0,
	FASTRPC_MAP_RESERVED,
	FASTRPC_MAP_FD = 2,
	FASTRPC_MAP_FD_DELAYED,
	FASTRPC_MAP_FD_NOMAP = 16,
	FASTRPC_MAP_MAX,
};

enum fastrpc_proc_attr {
	/* Macro for Debug attr */
	FASTRPC_MODE_DEBUG		= (1 << 0),
	/* Macro for Ptrace */
	FASTRPC_MODE_PTRACE		= (1 << 1),
	/* Macro for CRC Check */
	FASTRPC_MODE_CRC		= (1 << 2),
	/* Macro for Unsigned PD */
	FASTRPC_MODE_UNSIGNED_MODULE	= (1 << 3),
	/* Macro for Adaptive QoS */
	FASTRPC_MODE_ADAPTIVE_QOS	= (1 << 4),
	/* Macro for System Process */
	FASTRPC_MODE_SYSTEM_PROCESS	= (1 << 5),
	/* Macro for Prvileged Process */
	FASTRPC_MODE_PRIVILEGED		= (1 << 6),
};

/* Fastrpc attribute for memory protection of buffers */
#define FASTRPC_ATTR_SECUREMAP	(1)

struct fastrpc_invoke_args {
	__u64 ptr;
	__u64 length;
	__s32 fd;
	__u32 attr;
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

struct fastrpc_mem_map {
	__s32 version;
	__s32 fd;		/* fd */
	__s32 offset;		/* buffer offset */
	__u32 flags;		/* flags defined in enum fastrpc_map_flags */
	__u64 vaddrin;		/* buffer virtual address */
	__u64 length;		/* buffer length */
	__u64 vaddrout;		/* [out] remote virtual address */
	__s32 attrs;		/* buffer attributes used for SMMU mapping */
	__s32 reserved[4];
};

struct fastrpc_req_munmap {
	__u64 vaddrout;	/* address to unmap */
	__u64 size;	/* size */
};

struct fastrpc_mem_unmap {
	__s32 vesion;
	__s32 fd;		/* fd */
	__u64 vaddr;		/* remote process (dsp) virtual address */
	__u64 length;		/* buffer size */
	__s32 reserved[5];
};

struct fastrpc_ioctl_capability {
	__u32 domain;
	__u32 attribute_id;
	__u32 capability;   /* dsp capability */
	__u32 reserved[4];
};

#endif /* __QCOM_FASTRPC_H__ */
