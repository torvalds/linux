#ifndef __NETLINK_DIAG_H__
#define __NETLINK_DIAG_H__

#include <linux/types.h>

struct netlink_diag_req {
	__u8	sdiag_family;
	__u8	sdiag_protocol;
	__u16	pad;
	__u32	ndiag_ino;
	__u32	ndiag_show;
	__u32	ndiag_cookie[2];
};

struct netlink_diag_msg {
	__u8	ndiag_family;
	__u8	ndiag_type;
	__u8	ndiag_protocol;
	__u8	ndiag_state;

	__u32	ndiag_portid;
	__u32	ndiag_dst_portid;
	__u32	ndiag_dst_group;
	__u32	ndiag_ino;
	__u32	ndiag_cookie[2];
};

struct netlink_diag_ring {
	__u32	ndr_block_size;
	__u32	ndr_block_nr;
	__u32	ndr_frame_size;
	__u32	ndr_frame_nr;
};

enum {
	/* NETLINK_DIAG_NONE, standard nl API requires this attribute!  */
	NETLINK_DIAG_MEMINFO,
	NETLINK_DIAG_GROUPS,
	NETLINK_DIAG_RX_RING,
	NETLINK_DIAG_TX_RING,

	__NETLINK_DIAG_MAX,
};

#define NETLINK_DIAG_MAX (__NETLINK_DIAG_MAX - 1)

#define NDIAG_PROTO_ALL		((__u8) ~0)

#define NDIAG_SHOW_MEMINFO	0x00000001 /* show memory info of a socket */
#define NDIAG_SHOW_GROUPS	0x00000002 /* show groups of a netlink socket */
#ifndef __KERNEL__
/* deprecated since 4.6 */
#define NDIAG_SHOW_RING_CFG	0x00000004 /* show ring configuration */
#endif

#endif
