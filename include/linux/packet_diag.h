#ifndef __PACKET_DIAG_H__
#define __PACKET_DIAG_H__

#include <linux/types.h>

struct packet_diag_req {
	__u8	sdiag_family;
	__u8	sdiag_protocol;
	__u16	pad;
	__u32	pdiag_ino;
	__u32	pdiag_show;
	__u32	pdiag_cookie[2];
};

struct packet_diag_msg {
	__u8	pdiag_family;
	__u8	pdiag_type;
	__u16	pdiag_num;

	__u32	pdiag_ino;
	__u32	pdiag_cookie[2];
};

#endif
