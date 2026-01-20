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
	MSHV_PT_BIT_CPU_AND_XSAVE_FEATURES,
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
 * This is the initial/v1 version for backward compatibility.
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

#define MSHV_NUM_CPU_FEATURES_BANKS 2

/**
 * struct mshv_create_partition_v2
 *
 * This is extended version of the above initial MSHV_CREATE_PARTITION
 * ioctl and allows for following additional parameters:
 *
 * @pt_num_cpu_fbanks: Must be set to MSHV_NUM_CPU_FEATURES_BANKS.
 * @pt_cpu_fbanks: Disabled processor feature banks array.
 * @pt_disabled_xsave: Disabled xsave feature bits.
 *
 * pt_cpu_fbanks and pt_disabled_xsave are passed through as-is to the create
 * partition hypercall.
 *
 * Returns : same as above original mshv_create_partition
 */
struct mshv_create_partition_v2 {
	__u64 pt_flags;
	__u64 pt_isolation;
	__u16 pt_num_cpu_fbanks;
	__u8  pt_rsvd[6];		/* MBZ */
	__u64 pt_cpu_fbanks[MSHV_NUM_CPU_FEATURES_BANKS];
	__u64 pt_rsvd1[2];		/* MBZ */
#if defined(__x86_64__)
	__u64 pt_disabled_xsave;
#else
	__u64 pt_rsvd2;			/* MBZ */
#endif
} __packed;

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
 * Mappings can't overlap in GPA space.
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

/* Structure definitions, macros and IOCTLs for mshv_vtl */

#define MSHV_CAP_CORE_API_STABLE        0x0
#define MSHV_CAP_REGISTER_PAGE          0x1
#define MSHV_CAP_VTL_RETURN_ACTION      0x2
#define MSHV_CAP_DR6_SHARED             0x3
#define MSHV_MAX_RUN_MSG_SIZE                256

struct mshv_vp_registers {
	__u32 count;	/* supports only 1 register at a time */
	__u32 reserved; /* Reserved for alignment or future use */
	__u64 regs_ptr;	/* pointer to struct hv_register_assoc */
};

struct mshv_vtl_set_eventfd {
	__s32 fd;
	__u32 flag;
};

struct mshv_vtl_signal_event {
	__u32 connection_id;
	__u32 flag;
};

struct mshv_vtl_sint_post_msg {
	__u64 message_type;
	__u32 connection_id;
	__u32 payload_size; /* Must not exceed HV_MESSAGE_PAYLOAD_BYTE_COUNT */
	__u64 payload_ptr; /* pointer to message payload (bytes) */
};

struct mshv_vtl_ram_disposition {
	__u64 start_pfn;
	__u64 last_pfn;
};

struct mshv_vtl_set_poll_file {
	__u32 cpu;
	__u32 fd;
};

struct mshv_vtl_hvcall_setup {
	__u64 bitmap_array_size; /* stores number of bytes */
	__u64 allow_bitmap_ptr;
};

struct mshv_vtl_hvcall {
	__u64 control;      /* Hypercall control code */
	__u64 input_size;   /* Size of the input data */
	__u64 input_ptr;    /* Pointer to the input struct */
	__u64 status;       /* Status of the hypercall (output) */
	__u64 output_size;  /* Size of the output data */
	__u64 output_ptr;   /* Pointer to the output struct */
};

struct mshv_sint_mask {
	__u8 mask;
	__u8 reserved[7];
};

/* /dev/mshv device IOCTL */
#define MSHV_CHECK_EXTENSION    _IOW(MSHV_IOCTL, 0x00, __u32)

/* vtl device */
#define MSHV_CREATE_VTL			_IOR(MSHV_IOCTL, 0x1D, char)
#define MSHV_ADD_VTL0_MEMORY	_IOW(MSHV_IOCTL, 0x21, struct mshv_vtl_ram_disposition)
#define MSHV_SET_POLL_FILE		_IOW(MSHV_IOCTL, 0x25, struct mshv_vtl_set_poll_file)
#define MSHV_RETURN_TO_LOWER_VTL	_IO(MSHV_IOCTL, 0x27)
#define MSHV_GET_VP_REGISTERS		_IOWR(MSHV_IOCTL, 0x05, struct mshv_vp_registers)
#define MSHV_SET_VP_REGISTERS		_IOW(MSHV_IOCTL, 0x06, struct mshv_vp_registers)

/* VMBus device IOCTLs */
#define MSHV_SINT_SIGNAL_EVENT    _IOW(MSHV_IOCTL, 0x22, struct mshv_vtl_signal_event)
#define MSHV_SINT_POST_MESSAGE    _IOW(MSHV_IOCTL, 0x23, struct mshv_vtl_sint_post_msg)
#define MSHV_SINT_SET_EVENTFD     _IOW(MSHV_IOCTL, 0x24, struct mshv_vtl_set_eventfd)
#define MSHV_SINT_PAUSE_MESSAGE_STREAM     _IOW(MSHV_IOCTL, 0x25, struct mshv_sint_mask)

/* hv_hvcall device */
#define MSHV_HVCALL_SETUP        _IOW(MSHV_IOCTL, 0x1E, struct mshv_vtl_hvcall_setup)
#define MSHV_HVCALL              _IOWR(MSHV_IOCTL, 0x1F, struct mshv_vtl_hvcall)
#endif
