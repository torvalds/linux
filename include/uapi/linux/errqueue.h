/* SPDX-License-Identifier: GPL-2.0 WITH Linux-syscall-note */
#ifndef _UAPI_LINUX_ERRQUEUE_H
#define _UAPI_LINUX_ERRQUEUE_H

#include <linux/types.h>

struct sock_extended_err {
	__u32	ee_errno;	
	__u8	ee_origin;
	__u8	ee_type;
	__u8	ee_code;
	__u8	ee_pad;
	__u32   ee_info;
	__u32   ee_data;
};

#define SO_EE_ORIGIN_NONE	0
#define SO_EE_ORIGIN_LOCAL	1
#define SO_EE_ORIGIN_ICMP	2
#define SO_EE_ORIGIN_ICMP6	3
#define SO_EE_ORIGIN_TXSTATUS	4
#define SO_EE_ORIGIN_ZEROCOPY	5
#define SO_EE_ORIGIN_TXTIME	6
#define SO_EE_ORIGIN_TIMESTAMPING SO_EE_ORIGIN_TXSTATUS

#define SO_EE_OFFENDER(ee)	((struct sockaddr*)((ee)+1))

#define SO_EE_CODE_ZEROCOPY_COPIED	1

#define SO_EE_CODE_TXTIME_INVALID_PARAM	1
#define SO_EE_CODE_TXTIME_MISSED	2

/**
 *	struct scm_timestamping - timestamps exposed through cmsg
 *
 *	The timestamping interfaces SO_TIMESTAMPING, MSG_TSTAMP_*
 *	communicate network timestamps by passing this struct in a cmsg with
 *	recvmsg(). See Documentation/networking/timestamping.txt for details.
 */
struct scm_timestamping {
	struct timespec ts[3];
};

/* The type of scm_timestamping, passed in sock_extended_err ee_info.
 * This defines the type of ts[0]. For SCM_TSTAMP_SND only, if ts[0]
 * is zero, then this is a hardware timestamp and recorded in ts[2].
 */
enum {
	SCM_TSTAMP_SND,		/* driver passed skb to NIC, or HW */
	SCM_TSTAMP_SCHED,	/* data entered the packet scheduler */
	SCM_TSTAMP_ACK,		/* data acknowledged by peer */
};

#endif /* _UAPI_LINUX_ERRQUEUE_H */
