/*
   BlueZ - Bluetooth protocol stack for Linux
   Copyright (C) 2000-2001 Qualcomm Incorporated

   Written 2000,2001 by Maxim Krasnyansky <maxk@qualcomm.com>

   This program is free software; you can redistribute it and/or modify
   it under the terms of the GNU General Public License version 2 as
   published by the Free Software Foundation;

   THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS
   OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
   FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT OF THIRD PARTY RIGHTS.
   IN NO EVENT SHALL THE COPYRIGHT HOLDER(S) AND AUTHOR(S) BE LIABLE FOR ANY
   CLAIM, OR ANY SPECIAL INDIRECT OR CONSEQUENTIAL DAMAGES, OR ANY DAMAGES
   WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN
   ACTION OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF
   OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.

   ALL LIABILITY, INCLUDING LIABILITY FOR INFRINGEMENT OF ANY PATENTS,
   COPYRIGHTS, TRADEMARKS OR OTHER RIGHTS, RELATING TO USE OF THIS
   SOFTWARE IS DISCLAIMED.
*/

#ifndef __HCI_SOCK_H
#define __HCI_SOCK_H

/* Socket options */
#define HCI_DATA_DIR	1
#define HCI_FILTER	2
#define HCI_TIME_STAMP	3

/* CMSG flags */
#define HCI_CMSG_DIR	0x0001
#define HCI_CMSG_TSTAMP	0x0002

struct sockaddr_hci {
	sa_family_t    hci_family;
	unsigned short hci_dev;
	unsigned short hci_channel;
};
#define HCI_DEV_NONE	0xffff

#define HCI_CHANNEL_RAW		0
#define HCI_CHANNEL_USER	1
#define HCI_CHANNEL_MONITOR	2
#define HCI_CHANNEL_CONTROL	3
#define HCI_CHANNEL_LOGGING	4

struct hci_filter {
	unsigned long type_mask;
	unsigned long event_mask[2];
	__le16 opcode;
};

struct hci_ufilter {
	__u32  type_mask;
	__u32  event_mask[2];
	__le16 opcode;
};

#define HCI_FLT_TYPE_BITS	31
#define HCI_FLT_EVENT_BITS	63
#define HCI_FLT_OGF_BITS	63
#define HCI_FLT_OCF_BITS	127

/* Ioctl defines */
#define HCIDEVUP	_IOW('H', 201, int)
#define HCIDEVDOWN	_IOW('H', 202, int)
#define HCIDEVRESET	_IOW('H', 203, int)
#define HCIDEVRESTAT	_IOW('H', 204, int)

#define HCIGETDEVLIST	_IOR('H', 210, int)
#define HCIGETDEVINFO	_IOR('H', 211, int)
#define HCIGETCONNLIST	_IOR('H', 212, int)
#define HCIGETCONNINFO	_IOR('H', 213, int)
#define HCIGETAUTHINFO	_IOR('H', 215, int)

#define HCISETRAW	_IOW('H', 220, int)
#define HCISETSCAN	_IOW('H', 221, int)
#define HCISETAUTH	_IOW('H', 222, int)
#define HCISETENCRYPT	_IOW('H', 223, int)
#define HCISETPTYPE	_IOW('H', 224, int)
#define HCISETLINKPOL	_IOW('H', 225, int)
#define HCISETLINKMODE	_IOW('H', 226, int)
#define HCISETACLMTU	_IOW('H', 227, int)
#define HCISETSCOMTU	_IOW('H', 228, int)

#define HCIBLOCKADDR	_IOW('H', 230, int)
#define HCIUNBLOCKADDR	_IOW('H', 231, int)

#define HCIINQUIRY	_IOR('H', 240, int)

/* Ioctl requests structures */
struct hci_dev_stats {
	__u32 err_rx;
	__u32 err_tx;
	__u32 cmd_tx;
	__u32 evt_rx;
	__u32 acl_tx;
	__u32 acl_rx;
	__u32 sco_tx;
	__u32 sco_rx;
	__u32 byte_rx;
	__u32 byte_tx;
};

struct hci_dev_info {
	__u16 dev_id;
	char  name[8];

	bdaddr_t bdaddr;

	__u32 flags;
	__u8  type;

	__u8  features[8];

	__u32 pkt_type;
	__u32 link_policy;
	__u32 link_mode;

	__u16 acl_mtu;
	__u16 acl_pkts;
	__u16 sco_mtu;
	__u16 sco_pkts;

	struct hci_dev_stats stat;
};

struct hci_conn_info {
	__u16    handle;
	bdaddr_t bdaddr;
	__u8     type;
	__u8     out;
	__u16    state;
	__u32    link_mode;
};

struct hci_dev_req {
	__u16  dev_id;
	__u32  dev_opt;
};

struct hci_dev_list_req {
	__u16  dev_num;
	struct hci_dev_req dev_req[];	/* hci_dev_req structures */
};

struct hci_conn_list_req {
	__u16  dev_id;
	__u16  conn_num;
	struct hci_conn_info conn_info[];
};

struct hci_conn_info_req {
	bdaddr_t bdaddr;
	__u8     type;
	struct   hci_conn_info conn_info[];
};

struct hci_auth_info_req {
	bdaddr_t bdaddr;
	__u8     type;
};

struct hci_inquiry_req {
	__u16 dev_id;
	__u16 flags;
	__u8  lap[3];
	__u8  length;
	__u8  num_rsp;
};
#define IREQ_CACHE_FLUSH 0x0001

#endif /* __HCI_SOCK_H */
