/* SPDX-License-Identifier: GPL-2.0 WITH Linux-syscall-note */
/*
 * Userspace interfaces for /dev/mshv* devices and derived fds
 *
 * This file is divided into sections containing data structures and IOCTLs for
 * a particular set of related devices or derived file descriptors.
 *
 * The IOCTL definitions are at the end of each section. They are grouped by
 * device/fd, so that new IOCTLs can easily be added with a monotonically
 * increasing number.
 */
#ifndef _UAPI_LINUX_MSHV_H
#define _UAPI_LINUX_MSHV_H

#include <linux/types.h>

#define MSHV_IOCTL	0xB8

/*
 *******************************************
 * Entry point to main VMM APIs: /dev/mshv *
 *******************************************
 */

enum {
	MSHV_PT_BIT_LAPIC,
	MSHV_PT_BIT_X2APIC,
	MSHV_PT_BIT_GPA_SUPER_PAGES,
	MSHV_PT_BIT_COUNT,
};

#define MSHV_PT_FLAGS_MASK ((1 << MSHV_PT_BIT_COUNT) - 1)

enum {
	MSHV_PT_ISOLATION_NONE,
	MSHV_PT_ISOLATION_COUNT,
};

/**
 * struct mshv_create_partition - arguments for MSHV_CREATE_PARTITION
 * @pt_flags: Bitmask of 1 << MSHV_PT_BIT_*
 * @pt_isolation: MSHV_PT_ISOLATION_*
 *
 * Returns a file descriptor to act as a handle to a guest partition.
 * At this point the partition is not yet initialized in the hypervisor.
 * Some operations must be done with the partition in this state, e.g. setting
 * so-called "early" partition properties. The partition can then be
 * initialized with MSHV_INITIALIZE_PARTITION.
 */
struct mshv_create_partition {
	__u64 pt_flags;
	__u64 pt_isolation;
};

/* /dev/mshv */
#define MSHV_CREATE_PARTITION	_IOW(MSHV_IOCTL, 0x00, struct mshv_create_partition)

/*
 ************************
 * Child partition APIs *
 ************************
 */

struct mshv_create_vp {
	__u32 vp_index;
};

enum {
	MSHV_SET_MEM_BIT_WRITABLE,
	MSHV_SET_MEM_BIT_EXECUTABLE,
	MSHV_SET_MEM_BIT_UNMAP,
	MSHV_SET_MEM_BIT_COUNT
};

#define MSHV_SET_MEM_FLAGS_MASK ((1 << MSHV_SET_MEM_BIT_COUNT) - 1)

/* The hypervisor's "native" page size */
#define MSHV_HV_PAGE_SIZE	0x1000

/**
 * struct mshv_user_mem_region - arguments for MSHV_SET_GUEST_MEMORY
 * @size: Size of the memory region (bytes). Must be aligned to
 *        MSHV_HV_PAGE_SIZE
 * @guest_pfn: Base guest page number to map
 * @userspace_addr: Base address of userspace memory. Must be aligned to
 *                  MSHV_HV_PAGE_SIZE
 * @flags: Bitmask of 1 << MSHV_SET_MEM_BIT_*. If (1 << MSHV_SET_MEM_BIT_UNMAP)
 *         is set, ignore other bits.
 * @rsvd: MBZ
 *
 * Map or unmap a region of userspace memory to Guest Physical Addresses (GPA).
 * Mappings can't overlap in GPA space or userspace.
 * To unmap, these fields must match an existing mapping.
 */
struct mshv_user_mem_region {
	__u64 size;
	__u64 guest_pfn;
	__u64 userspace_addr;
	__u8 flags;
	__u8 rsvd[7];
};

enum {
	MSHV_IRQFD_BIT_DEASSIGN,
	MSHV_IRQFD_BIT_RESAMPLE,
	MSHV_IRQFD_BIT_COUNT,
};

#define MSHV_IRQFD_FLAGS_MASK	((1 << MSHV_IRQFD_BIT_COUNT) - 1)

struct mshv_user_irqfd {
	__s32 fd;
	__s32 resamplefd;
	__u32 gsi;
	__u32 flags;
};

enum {
	MSHV_IOEVENTFD_BIT_DATAMATCH,
	MSHV_IOEVENTFD_BIT_PIO,
	MSHV_IOEVENTFD_BIT_DEASSIGN,
	MSHV_IOEVENTFD_BIT_COUNT,
};

#define MSHV_IOEVENTFD_FLAGS_MASK	((1 << MSHV_IOEVENTFD_BIT_COUNT) - 1)

struct mshv_user_ioeventfd {
	__u64 datamatch;
	__u64 addr;	   /* legal pio/mmio address */
	__u32 len;	   /* 1, 2, 4, or 8 bytes    */
	__s32 fd;
	__u32 flags;
	__u8  rsvd[4];
};

struct mshv_user_irq_entry {
	__u32 gsi;
	__u32 address_lo;
	__u32 address_hi;
	__u32 data;
};

struct mshv_user_irq_table {
	__u32 nr;
	__u32 rsvd; /* MBZ */
	struct mshv_user_irq_entry entries[];
};

enum {
	MSHV_GPAP_ACCESS_TYPE_ACCESSED,
	MSHV_GPAP_ACCESS_TYPE_DIRTY,
	MSHV_GPAP_ACCESS_TYPE_COUNT		/* Count of enum members */
};

enum {
	MSHV_GPAP_ACCESS_OP_NOOP,
	MSHV_GPAP_ACCESS_OP_CLEAR,
	MSHV_GPAP_ACCESS_OP_SET,
	MSHV_GPAP_ACCESS_OP_COUNT		/* Count of enum members */
};

/**
 * struct mshv_gpap_access_bitmap - arguments for MSHV_GET_GPAP_ACCESS_BITMAP
 * @access_type: MSHV_GPAP_ACCESS_TYPE_* - The type of access to record in the
 *               bitmap
 * @access_op: MSHV_GPAP_ACCESS_OP_* - Allows an optional clear or set of all
 *             the access states in the range, after retrieving the current
 *             states.
 * @rsvd: MBZ
 * @page_count: Number of pages
 * @gpap_base: Base gpa page number
 * @bitmap_ptr: Output buffer for bitmap, at least (page_count + 7) / 8 bytes
 *
 * Retrieve a bitmap of either ACCESSED or DIRTY bits for a given range of guest
 * memory, and optionally clear or set the bits.
 */
struct mshv_gpap_access_bitmap {
	__u8 access_type;
	__u8 access_op;
	__u8 rsvd[6];
	__u64 page_count;
	__u64 gpap_base;
	__u64 bitmap_ptr;
};

/**
 * struct mshv_root_hvcall - arguments for MSHV_ROOT_HVCALL
 * @code: Hypercall code (HVCALL_*)
 * @reps: in: Rep count ('repcount')
 *	  out: Reps completed ('repcomp'). MBZ unless rep hvcall
 * @in_sz: Size of input incl rep data. <= MSHV_HV_PAGE_SIZE
 * @out_sz: Size of output buffer. <= MSHV_HV_PAGE_SIZE. MBZ if out_ptr is 0
 * @status: in: MBZ
 *	    out: HV_STATUS_* from hypercall
 * @rsvd: MBZ
 * @in_ptr: Input data buffer (struct hv_input_*). If used with partition or
 *	    vp fd, partition id field is populated by kernel.
 * @out_ptr: Output data buffer (optional)
 */
struct mshv_root_hvcall {
	__u16 code;
	__u16 reps;
	__u16 in_sz;
	__u16 out_sz;
	__u16 status;
	__u8 rsvd[6];
	__u64 in_ptr;
	__u64 out_ptr;
};

/* Partition fds created with MSHV_CREATE_PARTITION */
#define MSHV_INITIALIZE_PARTITION	_IO(MSHV_IOCTL, 0x00)
#define MSHV_CREATE_VP			_IOW(MSHV_IOCTL, 0x01, struct mshv_create_vp)
#define MSHV_SET_GUEST_MEMORY		_IOW(MSHV_IOCTL, 0x02, struct mshv_user_mem_region)
#define MSHV_IRQFD			_IOW(MSHV_IOCTL, 0x03, struct mshv_user_irqfd)
#define MSHV_IOEVENTFD			_IOW(MSHV_IOCTL, 0x04, struct mshv_user_ioeventfd)
#define MSHV_SET_MSI_ROUTING		_IOW(MSHV_IOCTL, 0x05, struct mshv_user_irq_table)
#define MSHV_GET_GPAP_ACCESS_BITMAP	_IOWR(MSHV_IOCTL, 0x06, struct mshv_gpap_access_bitmap)
/* Generic hypercall */
#define MSHV_ROOT_HVCALL		_IOWR(MSHV_IOCTL, 0x07, struct mshv_root_hvcall)

/*
 ********************************
 * VP APIs for child partitions *
 ********************************
 */

#define MSHV_RUN_VP_BUF_SZ 256

/*
 * VP state pages may be mapped to userspace via mmap().
 * To specify which state page, use MSHV_VP_MMAP_OFFSET_ values multiplied by
 * the system page size.
 * e.g.
 * long page_size = sysconf(_SC_PAGE_SIZE);
 * void *reg_page = mmap(NULL, MSHV_HV_PAGE_SIZE, PROT_READ|PROT_WRITE,
 *                       MAP_SHARED, vp_fd,
 *                       MSHV_VP_MMAP_OFFSET_REGISTERS * page_size);
 */
enum {
	MSHV_VP_MMAP_OFFSET_REGISTERS,
	MSHV_VP_MMAP_OFFSET_INTERCEPT_MESSAGE,
	MSHV_VP_MMAP_OFFSET_GHCB,
	MSHV_VP_MMAP_OFFSET_COUNT
};

/**
 * struct mshv_run_vp - argument for MSHV_RUN_VP
 * @msg_buf: On success, the intercept message is copied here. It can be
 *           interpreted using the relevant hypervisor definitions.
 */
struct mshv_run_vp {
	__u8 msg_buf[MSHV_RUN_VP_BUF_SZ];
};

enum {
	MSHV_VP_STATE_LAPIC,		/* Local interrupt controller state (either arch) */
	MSHV_VP_STATE_XSAVE,		/* XSAVE data in compacted form (x86_64) */
	MSHV_VP_STATE_SIMP,
	MSHV_VP_STATE_SIEFP,
	MSHV_VP_STATE_SYNTHETIC_TIMERS,
	MSHV_VP_STATE_COUNT,
};

/**
 * struct mshv_get_set_vp_state - arguments for MSHV_[GET,SET]_VP_STATE
 * @type: MSHV_VP_STATE_*
 * @rsvd: MBZ
 * @buf_sz: in: 4k page-aligned size of buffer
 *          out: Actual size of data (on EINVAL, check this to see if buffer
 *               was too small)
 * @buf_ptr: 4k page-aligned data buffer
 */
struct mshv_get_set_vp_state {
	__u8 type;
	__u8 rsvd[3];
	__u32 buf_sz;
	__u64 buf_ptr;
};

/* VP fds created with MSHV_CREATE_VP */
#define MSHV_RUN_VP			_IOR(MSHV_IOCTL, 0x00, struct mshv_run_vp)
#define MSHV_GET_VP_STATE		_IOWR(MSHV_IOCTL, 0x01, struct mshv_get_set_vp_state)
#define MSHV_SET_VP_STATE		_IOWR(MSHV_IOCTL, 0x02, struct mshv_get_set_vp_state)
/*
 * Generic hypercall
 * Defined above in partition IOCTLs, avoid redefining it here
 * #define MSHV_ROOT_HVCALL			_IOWR(MSHV_IOCTL, 0x07, struct mshv_root_hvcall)
 */

#endif
