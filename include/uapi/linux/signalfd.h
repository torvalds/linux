/*
 *  include/linux/signalfd.h
 *
 *  Copyright (C) 2007  Davide Libenzi <davidel@xmailserver.org>
 *
 */

#ifndef _UAPI_LINUX_SIGNALFD_H
#define _UAPI_LINUX_SIGNALFD_H

#include <linux/types.h>
/* For O_CLOEXEC and O_NONBLOCK */
#include <linux/fcntl.h>

/* Flags for signalfd4.  */
#define SFD_CLOEXEC O_CLOEXEC
#define SFD_NONBLOCK O_NONBLOCK

struct signalfd_siginfo {
	__u32 ssi_signo;
	__s32 ssi_errno;
	__s32 ssi_code;
	__u32 ssi_pid;
	__u32 ssi_uid;
	__s32 ssi_fd;
	__u32 ssi_tid;
	__u32 ssi_band;
	__u32 ssi_overrun;
	__u32 ssi_trapno;
	__s32 ssi_status;
	__s32 ssi_int;
	__u64 ssi_ptr;
	__u64 ssi_utime;
	__u64 ssi_stime;
	__u64 ssi_addr;
	__u16 ssi_addr_lsb;

	/*
	 * Pad strcture to 128 bytes. Remember to update the
	 * pad size when you add new members. We use a fixed
	 * size structure to avoid compatibility problems with
	 * future versions, and we leave extra space for additional
	 * members. We use fixed size members because this strcture
	 * comes out of a read(2) and we really don't want to have
	 * a compat on read(2).
	 */
	__u8 __pad[46];
};



#endif /* _UAPI_LINUX_SIGNALFD_H */
