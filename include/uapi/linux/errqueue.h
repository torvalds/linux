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
#define SO_EE_ORIGIN_TIMESTAMPING SO_EE_ORIGIN_TXSTATUS

#define SO_EE_OFFENDER(ee)	((struct sockaddr*)((ee)+1))


#endif /* _UAPI_LINUX_ERRQUEUE_H */
