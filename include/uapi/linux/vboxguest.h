/* SPDX-License-Identifier: (GPL-2.0 OR CDDL-1.0) */
/*
 * VBoxGuest - VirtualBox Guest Additions Driver Interface.
 *
 * Copyright (C) 2006-2016 Oracle Corporation
 */

#ifndef __UAPI_VBOXGUEST_H__
#define __UAPI_VBOXGUEST_H__

#include <asm/bitsperlong.h>
#include <linux/ioctl.h>
#include <linux/vbox_err.h>
#include <linux/vbox_vmmdev_types.h>

/* Version of vbg_ioctl_hdr structure. */
#define VBG_IOCTL_HDR_VERSION		0x10001
/* Default request type.  Use this for non-VMMDev requests. */
#define VBG_IOCTL_HDR_TYPE_DEFAULT		0

/**
 * Common ioctl header.
 *
 * This is a mirror of vmmdev_request_header to prevent duplicating data and
 * needing to verify things multiple times.
 */
struct vbg_ioctl_hdr {
	/** IN: The request input size, and output size if size_out is zero. */
	__u32 size_in;
	/** IN: Structure version (VBG_IOCTL_HDR_VERSION) */
	__u32 version;
	/** IN: The VMMDev request type or VBG_IOCTL_HDR_TYPE_DEFAULT. */
	__u32 type;
	/**
	 * OUT: The VBox status code of the operation, out direction only.
	 * This is a VINF_ or VERR_ value as defined in vbox_err.h.
	 */
	__s32 rc;
	/** IN: Output size. Set to zero to use size_in as output size. */
	__u32 size_out;
	/** Reserved, MBZ. */
	__u32 reserved;
};
VMMDEV_ASSERT_SIZE(vbg_ioctl_hdr, 24);


/*
 * The VBoxGuest I/O control version.
 *
 * As usual, the high word contains the major version and changes to it
 * signifies incompatible changes.
 *
 * The lower word is the minor version number, it is increased when new
 * functions are added or existing changed in a backwards compatible manner.
 */
#define VBG_IOC_VERSION		0x00010000u

/**
 * VBG_IOCTL_DRIVER_VERSION_INFO data structure
 *
 * Note VBG_IOCTL_DRIVER_VERSION_INFO may switch the session to a backwards
 * compatible interface version if uClientVersion indicates older client code.
 */
struct vbg_ioctl_driver_version_info {
	/** The header. */
	struct vbg_ioctl_hdr hdr;
	union {
		struct {
			/** Requested interface version (VBG_IOC_VERSION). */
			__u32 req_version;
			/**
			 * Minimum interface version number (typically the
			 * major version part of VBG_IOC_VERSION).
			 */
			__u32 min_version;
			/** Reserved, MBZ. */
			__u32 reserved1;
			/** Reserved, MBZ. */
			__u32 reserved2;
		} in;
		struct {
			/** Version for this session (typ. VBG_IOC_VERSION). */
			__u32 session_version;
			/** Version of the IDC interface (VBG_IOC_VERSION). */
			__u32 driver_version;
			/** The SVN revision of the driver, or 0. */
			__u32 driver_revision;
			/** Reserved \#1 (zero until defined). */
			__u32 reserved1;
			/** Reserved \#2 (zero until defined). */
			__u32 reserved2;
		} out;
	} u;
};
VMMDEV_ASSERT_SIZE(vbg_ioctl_driver_version_info, 24 + 20);

#define VBG_IOCTL_DRIVER_VERSION_INFO \
	_IOWR('V', 0, struct vbg_ioctl_driver_version_info)


/* IOCTL to perform a VMM Device request less than 1KB in size. */
#define VBG_IOCTL_VMMDEV_REQUEST(s)	_IOC(_IOC_READ | _IOC_WRITE, 'V', 2, s)


/* IOCTL to perform a VMM Device request larger then 1KB. */
#define VBG_IOCTL_VMMDEV_REQUEST_BIG	_IOC(_IOC_READ | _IOC_WRITE, 'V', 3, 0)


/** VBG_IOCTL_HGCM_CONNECT data structure. */
struct vbg_ioctl_hgcm_connect {
	struct vbg_ioctl_hdr hdr;
	union {
		struct {
			struct vmmdev_hgcm_service_location loc;
		} in;
		struct {
			__u32 client_id;
		} out;
	} u;
};
VMMDEV_ASSERT_SIZE(vbg_ioctl_hgcm_connect, 24 + 132);

#define VBG_IOCTL_HGCM_CONNECT \
	_IOWR('V', 4, struct vbg_ioctl_hgcm_connect)


/** VBG_IOCTL_HGCM_DISCONNECT data structure. */
struct vbg_ioctl_hgcm_disconnect {
	struct vbg_ioctl_hdr hdr;
	union {
		struct {
			__u32 client_id;
		} in;
	} u;
};
VMMDEV_ASSERT_SIZE(vbg_ioctl_hgcm_disconnect, 24 + 4);

#define VBG_IOCTL_HGCM_DISCONNECT \
	_IOWR('V', 5, struct vbg_ioctl_hgcm_disconnect)


/** VBG_IOCTL_HGCM_CALL data structure. */
struct vbg_ioctl_hgcm_call {
	/** The header. */
	struct vbg_ioctl_hdr hdr;
	/** Input: The id of the caller. */
	__u32 client_id;
	/** Input: Function number. */
	__u32 function;
	/**
	 * Input: How long to wait (milliseconds) for completion before
	 * cancelling the call. Set to -1 to wait indefinitely.
	 */
	__u32 timeout_ms;
	/** Interruptable flag, ignored for userspace calls. */
	__u8 interruptible;
	/** Explicit padding, MBZ. */
	__u8 reserved;
	/**
	 * Input: How many parameters following this structure.
	 *
	 * The parameters are either HGCMFunctionParameter64 or 32,
	 * depending on whether we're receiving a 64-bit or 32-bit request.
	 *
	 * The current maximum is 61 parameters (given a 1KB max request size,
	 * and a 64-bit parameter size of 16 bytes).
	 */
	__u16 parm_count;
	/*
	 * Parameters follow in form:
	 * struct hgcm_function_parameter<32|64> parms[parm_count]
	 */
};
VMMDEV_ASSERT_SIZE(vbg_ioctl_hgcm_call, 24 + 16);

#define VBG_IOCTL_HGCM_CALL_32(s)	_IOC(_IOC_READ | _IOC_WRITE, 'V', 6, s)
#define VBG_IOCTL_HGCM_CALL_64(s)	_IOC(_IOC_READ | _IOC_WRITE, 'V', 7, s)
#if __BITS_PER_LONG == 64
#define VBG_IOCTL_HGCM_CALL(s)		VBG_IOCTL_HGCM_CALL_64(s)
#else
#define VBG_IOCTL_HGCM_CALL(s)		VBG_IOCTL_HGCM_CALL_32(s)
#endif


/** VBG_IOCTL_LOG data structure. */
struct vbg_ioctl_log {
	/** The header. */
	struct vbg_ioctl_hdr hdr;
	union {
		struct {
			/**
			 * The log message, this may be zero terminated. If it
			 * is not zero terminated then the length is determined
			 * from the input size.
			 */
			char msg[1];
		} in;
	} u;
};

#define VBG_IOCTL_LOG(s)		_IOC(_IOC_READ | _IOC_WRITE, 'V', 9, s)


/** VBG_IOCTL_WAIT_FOR_EVENTS data structure. */
struct vbg_ioctl_wait_for_events {
	/** The header. */
	struct vbg_ioctl_hdr hdr;
	union {
		struct {
			/** Timeout in milliseconds. */
			__u32 timeout_ms;
			/** Events to wait for. */
			__u32 events;
		} in;
		struct {
			/** Events that occurred. */
			__u32 events;
		} out;
	} u;
};
VMMDEV_ASSERT_SIZE(vbg_ioctl_wait_for_events, 24 + 8);

#define VBG_IOCTL_WAIT_FOR_EVENTS \
	_IOWR('V', 10, struct vbg_ioctl_wait_for_events)


/*
 * IOCTL to VBoxGuest to interrupt (cancel) any pending
 * VBG_IOCTL_WAIT_FOR_EVENTS and return.
 *
 * Handled inside the vboxguest driver and not seen by the host at all.
 * After calling this, VBG_IOCTL_WAIT_FOR_EVENTS should no longer be called in
 * the same session. Any VBOXGUEST_IOCTL_WAITEVENT calls in the same session
 * done after calling this will directly exit with -EINTR.
 */
#define VBG_IOCTL_INTERRUPT_ALL_WAIT_FOR_EVENTS \
	_IOWR('V', 11, struct vbg_ioctl_hdr)


/** VBG_IOCTL_CHANGE_FILTER_MASK data structure. */
struct vbg_ioctl_change_filter {
	/** The header. */
	struct vbg_ioctl_hdr hdr;
	union {
		struct {
			/** Flags to set. */
			__u32 or_mask;
			/** Flags to remove. */
			__u32 not_mask;
		} in;
	} u;
};
VMMDEV_ASSERT_SIZE(vbg_ioctl_change_filter, 24 + 8);

/* IOCTL to VBoxGuest to control the event filter mask. */
#define VBG_IOCTL_CHANGE_FILTER_MASK \
	_IOWR('V', 12, struct vbg_ioctl_change_filter)


/** VBG_IOCTL_CHANGE_GUEST_CAPABILITIES data structure. */
struct vbg_ioctl_set_guest_caps {
	/** The header. */
	struct vbg_ioctl_hdr hdr;
	union {
		struct {
			/** Capabilities to set (VMMDEV_GUEST_SUPPORTS_XXX). */
			__u32 or_mask;
			/** Capabilities to drop (VMMDEV_GUEST_SUPPORTS_XXX). */
			__u32 not_mask;
		} in;
		struct {
			/** Capabilities held by the session after the call. */
			__u32 session_caps;
			/** Capabilities for all the sessions after the call. */
			__u32 global_caps;
		} out;
	} u;
};
VMMDEV_ASSERT_SIZE(vbg_ioctl_set_guest_caps, 24 + 8);

#define VBG_IOCTL_CHANGE_GUEST_CAPABILITIES \
	_IOWR('V', 14, struct vbg_ioctl_set_guest_caps)


/** VBG_IOCTL_CHECK_BALLOON data structure. */
struct vbg_ioctl_check_balloon {
	/** The header. */
	struct vbg_ioctl_hdr hdr;
	union {
		struct {
			/** The size of the balloon in chunks of 1MB. */
			__u32 balloon_chunks;
			/**
			 * false = handled in R0, no further action required.
			 *  true = allocate balloon memory in R3.
			 */
			__u8 handle_in_r3;
			/** Explicit padding, MBZ. */
			__u8 padding[3];
		} out;
	} u;
};
VMMDEV_ASSERT_SIZE(vbg_ioctl_check_balloon, 24 + 8);

/*
 * IOCTL to check memory ballooning.
 *
 * The guest kernel module will ask the host for the current size of the
 * balloon and adjust the size. Or it will set handle_in_r3 = true and R3 is
 * responsible for allocating memory and calling VBG_IOCTL_CHANGE_BALLOON.
 */
#define VBG_IOCTL_CHECK_BALLOON \
	_IOWR('V', 17, struct vbg_ioctl_check_balloon)


/** VBG_IOCTL_WRITE_CORE_DUMP data structure. */
struct vbg_ioctl_write_coredump {
	struct vbg_ioctl_hdr hdr;
	union {
		struct {
			__u32 flags; /** Flags (reserved, MBZ). */
		} in;
	} u;
};
VMMDEV_ASSERT_SIZE(vbg_ioctl_write_coredump, 24 + 4);

#define VBG_IOCTL_WRITE_CORE_DUMP \
	_IOWR('V', 19, struct vbg_ioctl_write_coredump)

#endif
