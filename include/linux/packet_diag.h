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

#define PACKET_SHOW_INFO	0x00000001 /* Basic packet_sk information */
#define PACKET_SHOW_MCLIST	0x00000002 /* A set of packet_diag_mclist-s */

struct packet_diag_msg {
	__u8	pdiag_family;
	__u8	pdiag_type;
	__u16	pdiag_num;

	__u32	pdiag_ino;
	__u32	pdiag_cookie[2];
};

enum {
	PACKET_DIAG_INFO,
	PACKET_DIAG_MCLIST,

	PACKET_DIAG_MAX,
};

struct packet_diag_info {
	__u32	pdi_index;
	__u32	pdi_version;
	__u32	pdi_reserve;
	__u32	pdi_copy_thresh;
	__u32	pdi_tstamp;
	__u32	pdi_flags;

#define PDI_RUNNING	0x1
#define PDI_AUXDATA	0x2
#define PDI_ORIGDEV	0x4
#define PDI_VNETHDR	0x8
#define PDI_LOSS	0x10
};

struct packet_diag_mclist {
	__u32	pdmc_index;
	__u32	pdmc_count;
	__u16	pdmc_type;
	__u16	pdmc_alen;
	__u8	pdmc_addr[MAX_ADDR_LEN];
};

#endif
