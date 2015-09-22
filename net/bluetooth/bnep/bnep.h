/*
  BNEP protocol definition for Linux Bluetooth stack (BlueZ).
  Copyright (C) 2002 Maxim Krasnyansky <maxk@qualcomm.com>

  This program is free software; you can redistribute it and/or modify
  it under the terms of the GNU General Public License, version 2, as
  published by the Free Software Foundation.

  This program is distributed in the hope that it will be useful,
  but WITHOUT ANY WARRANTY; without even the implied warranty of
  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
  GNU General Public License for more details.

  You should have received a copy of the GNU General Public License
  along with this program; if not, see <http://www.gnu.org/licenses/>.
*/

#ifndef _BNEP_H
#define _BNEP_H

#include <linux/types.h>
#include <linux/crc32.h>
#include <net/bluetooth/bluetooth.h>

/* Limits */
#define BNEP_MAX_PROTO_FILTERS		5
#define BNEP_MAX_MULTICAST_FILTERS	20

/* UUIDs */
#define BNEP_BASE_UUID	0x0000000000001000800000805F9B34FB
#define BNEP_UUID16	0x02
#define BNEP_UUID32	0x04
#define BNEP_UUID128	0x16

#define BNEP_SVC_PANU	0x1115
#define BNEP_SVC_NAP	0x1116
#define BNEP_SVC_GN	0x1117

/* Packet types */
#define BNEP_GENERAL			0x00
#define BNEP_CONTROL			0x01
#define BNEP_COMPRESSED			0x02
#define BNEP_COMPRESSED_SRC_ONLY	0x03
#define BNEP_COMPRESSED_DST_ONLY	0x04

/* Control types */
#define BNEP_CMD_NOT_UNDERSTOOD		0x00
#define BNEP_SETUP_CONN_REQ		0x01
#define BNEP_SETUP_CONN_RSP		0x02
#define BNEP_FILTER_NET_TYPE_SET	0x03
#define BNEP_FILTER_NET_TYPE_RSP	0x04
#define BNEP_FILTER_MULTI_ADDR_SET	0x05
#define BNEP_FILTER_MULTI_ADDR_RSP	0x06

/* Extension types */
#define BNEP_EXT_CONTROL 0x00

/* Response messages */
#define BNEP_SUCCESS 0x00

#define BNEP_CONN_INVALID_DST 0x01
#define BNEP_CONN_INVALID_SRC 0x02
#define BNEP_CONN_INVALID_SVC 0x03
#define BNEP_CONN_NOT_ALLOWED 0x04

#define BNEP_FILTER_UNSUPPORTED_REQ	0x01
#define BNEP_FILTER_INVALID_RANGE	0x02
#define BNEP_FILTER_INVALID_MCADDR	0x02
#define BNEP_FILTER_LIMIT_REACHED	0x03
#define BNEP_FILTER_DENIED_SECURITY	0x04

/* L2CAP settings */
#define BNEP_MTU	1691
#define BNEP_PSM	0x0f
#define BNEP_FLUSH_TO	0xffff
#define BNEP_CONNECT_TO	15
#define BNEP_FILTER_TO	15

/* Headers */
#define BNEP_TYPE_MASK	0x7f
#define BNEP_EXT_HEADER	0x80

struct bnep_setup_conn_req {
	__u8 type;
	__u8 ctrl;
	__u8 uuid_size;
	__u8 service[0];
} __packed;

struct bnep_set_filter_req {
	__u8 type;
	__u8 ctrl;
	__be16 len;
	__u8 list[0];
} __packed;

struct bnep_control_rsp {
	__u8 type;
	__u8 ctrl;
	__be16 resp;
} __packed;

struct bnep_ext_hdr {
	__u8 type;
	__u8 len;
	__u8 data[0];
} __packed;

/* BNEP ioctl defines */
#define BNEPCONNADD	_IOW('B', 200, int)
#define BNEPCONNDEL	_IOW('B', 201, int)
#define BNEPGETCONNLIST	_IOR('B', 210, int)
#define BNEPGETCONNINFO	_IOR('B', 211, int)
#define BNEPGETSUPPFEAT	_IOR('B', 212, int)

#define BNEP_SETUP_RESPONSE	0
#define BNEP_SETUP_RSP_SENT	10

struct bnep_connadd_req {
	int   sock;		/* Connected socket */
	__u32 flags;
	__u16 role;
	char  device[16];	/* Name of the Ethernet device */
};

struct bnep_conndel_req {
	__u32 flags;
	__u8  dst[ETH_ALEN];
};

struct bnep_conninfo {
	__u32 flags;
	__u16 role;
	__u16 state;
	__u8  dst[ETH_ALEN];
	char  device[16];
};

struct bnep_connlist_req {
	__u32  cnum;
	struct bnep_conninfo __user *ci;
};

struct bnep_proto_filter {
	__u16 start;
	__u16 end;
};

int bnep_add_connection(struct bnep_connadd_req *req, struct socket *sock);
int bnep_del_connection(struct bnep_conndel_req *req);
int bnep_get_connlist(struct bnep_connlist_req *req);
int bnep_get_conninfo(struct bnep_conninfo *ci);

/* BNEP sessions */
struct bnep_session {
	struct list_head list;

	unsigned int  role;
	unsigned long state;
	unsigned long flags;
	atomic_t      terminate;
	struct task_struct *task;

	struct ethhdr eh;
	struct msghdr msg;

	struct bnep_proto_filter proto_filter[BNEP_MAX_PROTO_FILTERS];
	unsigned long long mc_filter;

	struct socket    *sock;
	struct net_device *dev;
};

void bnep_net_setup(struct net_device *dev);
int bnep_sock_init(void);
void bnep_sock_cleanup(void);

static inline int bnep_mc_hash(__u8 *addr)
{
	return crc32_be(~0, addr, ETH_ALEN) >> 26;
}

#endif
