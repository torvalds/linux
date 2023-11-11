/* SPDX-License-Identifier: ((GPL-2.0 WITH Linux-syscall-note) OR CDDL-1.0) */
/*
 * Virtual Device for Guest <-> VMM/Host communication, type definitions
 * which are also used for the vboxguest ioctl interface / by vboxsf
 *
 * Copyright (C) 2006-2016 Oracle Corporation
 */

#ifndef __UAPI_VBOX_VMMDEV_TYPES_H__
#define __UAPI_VBOX_VMMDEV_TYPES_H__

#include <asm/bitsperlong.h>
#include <linux/types.h>

/*
 * We cannot use linux' compiletime_assert here because it expects to be used
 * inside a function only. Use a typedef to a char array with a negative size.
 */
#define VMMDEV_ASSERT_SIZE(type, size) \
	typedef char type ## _asrt_size[1 - 2*!!(sizeof(struct type) != (size))]

/** enum vmmdev_request_type - VMMDev request types. */
enum vmmdev_request_type {
	VMMDEVREQ_INVALID_REQUEST              =  0,
	VMMDEVREQ_GET_MOUSE_STATUS             =  1,
	VMMDEVREQ_SET_MOUSE_STATUS             =  2,
	VMMDEVREQ_SET_POINTER_SHAPE            =  3,
	VMMDEVREQ_GET_HOST_VERSION             =  4,
	VMMDEVREQ_IDLE                         =  5,
	VMMDEVREQ_GET_HOST_TIME                = 10,
	VMMDEVREQ_GET_HYPERVISOR_INFO          = 20,
	VMMDEVREQ_SET_HYPERVISOR_INFO          = 21,
	VMMDEVREQ_REGISTER_PATCH_MEMORY        = 22, /* since version 3.0.6 */
	VMMDEVREQ_DEREGISTER_PATCH_MEMORY      = 23, /* since version 3.0.6 */
	VMMDEVREQ_SET_POWER_STATUS             = 30,
	VMMDEVREQ_ACKNOWLEDGE_EVENTS           = 41,
	VMMDEVREQ_CTL_GUEST_FILTER_MASK        = 42,
	VMMDEVREQ_REPORT_GUEST_INFO            = 50,
	VMMDEVREQ_REPORT_GUEST_INFO2           = 58, /* since version 3.2.0 */
	VMMDEVREQ_REPORT_GUEST_STATUS          = 59, /* since version 3.2.8 */
	VMMDEVREQ_REPORT_GUEST_USER_STATE      = 74, /* since version 4.3 */
	/* Retrieve a display resize request sent by the host, deprecated. */
	VMMDEVREQ_GET_DISPLAY_CHANGE_REQ       = 51,
	VMMDEVREQ_VIDEMODE_SUPPORTED           = 52,
	VMMDEVREQ_GET_HEIGHT_REDUCTION         = 53,
	/**
	 * @VMMDEVREQ_GET_DISPLAY_CHANGE_REQ2:
	 * Retrieve a display resize request sent by the host.
	 *
	 * Queries a display resize request sent from the host.  If the
	 * event_ack member is sent to true and there is an unqueried request
	 * available for one of the virtual display then that request will
	 * be returned.  If several displays have unqueried requests the lowest
	 * numbered display will be chosen first.  Only the most recent unseen
	 * request for each display is remembered.
	 * If event_ack is set to false, the last host request queried with
	 * event_ack set is resent, or failing that the most recent received
	 * from the host.  If no host request was ever received then all zeros
	 * are returned.
	 */
	VMMDEVREQ_GET_DISPLAY_CHANGE_REQ2      = 54,
	VMMDEVREQ_REPORT_GUEST_CAPABILITIES    = 55,
	VMMDEVREQ_SET_GUEST_CAPABILITIES       = 56,
	VMMDEVREQ_VIDEMODE_SUPPORTED2          = 57, /* since version 3.2.0 */
	VMMDEVREQ_GET_DISPLAY_CHANGE_REQEX     = 80, /* since version 4.2.4 */
	VMMDEVREQ_GET_DISPLAY_CHANGE_REQ_MULTI = 81,
	VMMDEVREQ_HGCM_CONNECT                 = 60,
	VMMDEVREQ_HGCM_DISCONNECT              = 61,
	VMMDEVREQ_HGCM_CALL32                  = 62,
	VMMDEVREQ_HGCM_CALL64                  = 63,
	VMMDEVREQ_HGCM_CANCEL                  = 64,
	VMMDEVREQ_HGCM_CANCEL2                 = 65,
	VMMDEVREQ_VIDEO_ACCEL_ENABLE           = 70,
	VMMDEVREQ_VIDEO_ACCEL_FLUSH            = 71,
	VMMDEVREQ_VIDEO_SET_VISIBLE_REGION     = 72,
	VMMDEVREQ_GET_SEAMLESS_CHANGE_REQ      = 73,
	VMMDEVREQ_QUERY_CREDENTIALS            = 100,
	VMMDEVREQ_REPORT_CREDENTIALS_JUDGEMENT = 101,
	VMMDEVREQ_REPORT_GUEST_STATS           = 110,
	VMMDEVREQ_GET_MEMBALLOON_CHANGE_REQ    = 111,
	VMMDEVREQ_GET_STATISTICS_CHANGE_REQ    = 112,
	VMMDEVREQ_CHANGE_MEMBALLOON            = 113,
	VMMDEVREQ_GET_VRDPCHANGE_REQ           = 150,
	VMMDEVREQ_LOG_STRING                   = 200,
	VMMDEVREQ_GET_CPU_HOTPLUG_REQ          = 210,
	VMMDEVREQ_SET_CPU_HOTPLUG_STATUS       = 211,
	VMMDEVREQ_REGISTER_SHARED_MODULE       = 212,
	VMMDEVREQ_UNREGISTER_SHARED_MODULE     = 213,
	VMMDEVREQ_CHECK_SHARED_MODULES         = 214,
	VMMDEVREQ_GET_PAGE_SHARING_STATUS      = 215,
	VMMDEVREQ_DEBUG_IS_PAGE_SHARED         = 216,
	VMMDEVREQ_GET_SESSION_ID               = 217, /* since version 3.2.8 */
	VMMDEVREQ_WRITE_COREDUMP               = 218,
	VMMDEVREQ_GUEST_HEARTBEAT              = 219,
	VMMDEVREQ_HEARTBEAT_CONFIGURE          = 220,
	VMMDEVREQ_NT_BUG_CHECK                 = 221,
	VMMDEVREQ_VIDEO_UPDATE_MONITOR_POSITIONS = 222,
	/* Ensure the enum is a 32 bit data-type */
	VMMDEVREQ_SIZEHACK                     = 0x7fffffff
};

#if __BITS_PER_LONG == 64
#define VMMDEVREQ_HGCM_CALL VMMDEVREQ_HGCM_CALL64
#else
#define VMMDEVREQ_HGCM_CALL VMMDEVREQ_HGCM_CALL32
#endif

/* vmmdev_request_header.requestor defines */

/* Requestor user not given. */
#define VMMDEV_REQUESTOR_USR_NOT_GIVEN                      0x00000000
/* The kernel driver (vboxguest) is the requestor. */
#define VMMDEV_REQUESTOR_USR_DRV                            0x00000001
/* Some other kernel driver is the requestor. */
#define VMMDEV_REQUESTOR_USR_DRV_OTHER                      0x00000002
/* The root or a admin user is the requestor. */
#define VMMDEV_REQUESTOR_USR_ROOT                           0x00000003
/* Regular joe user is making the request. */
#define VMMDEV_REQUESTOR_USR_USER                           0x00000006
/* User classification mask. */
#define VMMDEV_REQUESTOR_USR_MASK                           0x00000007

/* Kernel mode request. Note this is 0, check for !USERMODE instead. */
#define VMMDEV_REQUESTOR_KERNEL                             0x00000000
/* User mode request. */
#define VMMDEV_REQUESTOR_USERMODE                           0x00000008
/* User or kernel mode classification mask. */
#define VMMDEV_REQUESTOR_MODE_MASK                          0x00000008

/* Don't know the physical console association of the requestor. */
#define VMMDEV_REQUESTOR_CON_DONT_KNOW                      0x00000000
/*
 * The request originates with a process that is NOT associated with the
 * physical console.
 */
#define VMMDEV_REQUESTOR_CON_NO                             0x00000010
/* Requestor process is associated with the physical console. */
#define VMMDEV_REQUESTOR_CON_YES                            0x00000020
/* Console classification mask. */
#define VMMDEV_REQUESTOR_CON_MASK                           0x00000030

/* Requestor is member of special VirtualBox user group. */
#define VMMDEV_REQUESTOR_GRP_VBOX                           0x00000080

/* Note: trust level is for windows guests only, linux always uses not-given */
/* Requestor trust level: Unspecified */
#define VMMDEV_REQUESTOR_TRUST_NOT_GIVEN                    0x00000000
/* Requestor trust level: Untrusted (SID S-1-16-0) */
#define VMMDEV_REQUESTOR_TRUST_UNTRUSTED                    0x00001000
/* Requestor trust level: Untrusted (SID S-1-16-4096) */
#define VMMDEV_REQUESTOR_TRUST_LOW                          0x00002000
/* Requestor trust level: Medium (SID S-1-16-8192) */
#define VMMDEV_REQUESTOR_TRUST_MEDIUM                       0x00003000
/* Requestor trust level: Medium plus (SID S-1-16-8448) */
#define VMMDEV_REQUESTOR_TRUST_MEDIUM_PLUS                  0x00004000
/* Requestor trust level: High (SID S-1-16-12288) */
#define VMMDEV_REQUESTOR_TRUST_HIGH                         0x00005000
/* Requestor trust level: System (SID S-1-16-16384) */
#define VMMDEV_REQUESTOR_TRUST_SYSTEM                       0x00006000
/* Requestor trust level >= Protected (SID S-1-16-20480, S-1-16-28672) */
#define VMMDEV_REQUESTOR_TRUST_PROTECTED                    0x00007000
/* Requestor trust level mask */
#define VMMDEV_REQUESTOR_TRUST_MASK                         0x00007000

/* Requestor is using the less trusted user device node (/dev/vboxuser) */
#define VMMDEV_REQUESTOR_USER_DEVICE                        0x00008000

/** HGCM service location types. */
enum vmmdev_hgcm_service_location_type {
	VMMDEV_HGCM_LOC_INVALID    = 0,
	VMMDEV_HGCM_LOC_LOCALHOST  = 1,
	VMMDEV_HGCM_LOC_LOCALHOST_EXISTING = 2,
	/* Ensure the enum is a 32 bit data-type */
	VMMDEV_HGCM_LOC_SIZEHACK   = 0x7fffffff
};

/** HGCM host service location. */
struct vmmdev_hgcm_service_location_localhost {
	/** Service name */
	char service_name[128];
};
VMMDEV_ASSERT_SIZE(vmmdev_hgcm_service_location_localhost, 128);

/** HGCM service location. */
struct vmmdev_hgcm_service_location {
	/** Type of the location. */
	enum vmmdev_hgcm_service_location_type type;

	union {
		struct vmmdev_hgcm_service_location_localhost localhost;
	} u;
};
VMMDEV_ASSERT_SIZE(vmmdev_hgcm_service_location, 128 + 4);

/** HGCM function parameter type. */
enum vmmdev_hgcm_function_parameter_type {
	VMMDEV_HGCM_PARM_TYPE_INVALID            = 0,
	VMMDEV_HGCM_PARM_TYPE_32BIT              = 1,
	VMMDEV_HGCM_PARM_TYPE_64BIT              = 2,
	/** Deprecated Doesn't work, use PAGELIST. */
	VMMDEV_HGCM_PARM_TYPE_PHYSADDR           = 3,
	/** In and Out, user-memory */
	VMMDEV_HGCM_PARM_TYPE_LINADDR            = 4,
	/** In, user-memory  (read;  host<-guest) */
	VMMDEV_HGCM_PARM_TYPE_LINADDR_IN         = 5,
	/** Out, user-memory (write; host->guest) */
	VMMDEV_HGCM_PARM_TYPE_LINADDR_OUT        = 6,
	/** In and Out, kernel-memory */
	VMMDEV_HGCM_PARM_TYPE_LINADDR_KERNEL     = 7,
	/** In, kernel-memory  (read;  host<-guest) */
	VMMDEV_HGCM_PARM_TYPE_LINADDR_KERNEL_IN  = 8,
	/** Out, kernel-memory (write; host->guest) */
	VMMDEV_HGCM_PARM_TYPE_LINADDR_KERNEL_OUT = 9,
	/** Physical addresses of locked pages for a buffer. */
	VMMDEV_HGCM_PARM_TYPE_PAGELIST           = 10,
	/* Ensure the enum is a 32 bit data-type */
	VMMDEV_HGCM_PARM_TYPE_SIZEHACK           = 0x7fffffff
};

/** HGCM function parameter, 32-bit client. */
struct vmmdev_hgcm_function_parameter32 {
	enum vmmdev_hgcm_function_parameter_type type;
	union {
		__u32 value32;
		__u64 value64;
		struct {
			__u32 size;
			union {
				__u32 phys_addr;
				__u32 linear_addr;
			} u;
		} pointer;
		struct {
			/** Size of the buffer described by the page list. */
			__u32 size;
			/** Relative to the request header. */
			__u32 offset;
		} page_list;
	} u;
} __packed;
VMMDEV_ASSERT_SIZE(vmmdev_hgcm_function_parameter32, 4 + 8);

/** HGCM function parameter, 64-bit client. */
struct vmmdev_hgcm_function_parameter64 {
	enum vmmdev_hgcm_function_parameter_type type;
	union {
		__u32 value32;
		__u64 value64;
		struct {
			__u32 size;
			union {
				__u64 phys_addr;
				__u64 linear_addr;
			} u;
		} __packed pointer;
		struct {
			/** Size of the buffer described by the page list. */
			__u32 size;
			/** Relative to the request header. */
			__u32 offset;
		} page_list;
	} __packed u;
} __packed;
VMMDEV_ASSERT_SIZE(vmmdev_hgcm_function_parameter64, 4 + 12);

#if __BITS_PER_LONG == 64
#define vmmdev_hgcm_function_parameter vmmdev_hgcm_function_parameter64
#else
#define vmmdev_hgcm_function_parameter vmmdev_hgcm_function_parameter32
#endif

#define VMMDEV_HGCM_F_PARM_DIRECTION_NONE      0x00000000U
#define VMMDEV_HGCM_F_PARM_DIRECTION_TO_HOST   0x00000001U
#define VMMDEV_HGCM_F_PARM_DIRECTION_FROM_HOST 0x00000002U
#define VMMDEV_HGCM_F_PARM_DIRECTION_BOTH      0x00000003U

/**
 * struct vmmdev_hgcm_pagelist - VMMDEV_HGCM_PARM_TYPE_PAGELIST parameters
 * point to this structure to actually describe the buffer.
 */
struct vmmdev_hgcm_pagelist {
	__u32 flags;             /** VMMDEV_HGCM_F_PARM_*. */
	__u16 offset_first_page; /** Data offset in the first page. */
	__u16 page_count;        /** Number of pages. */
	__u64 pages[1];          /** Page addresses. */
};
VMMDEV_ASSERT_SIZE(vmmdev_hgcm_pagelist, 4 + 2 + 2 + 8);

#endif
