/*
   BlueZ - Bluetooth protocol stack for Linux

   Copyright (C) 2011-2012  Intel Corporation

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

#ifndef __HCI_MON_H
#define __HCI_MON_H

struct hci_mon_hdr {
	__le16	opcode;
	__le16	index;
	__le16	len;
} __packed;
#define HCI_MON_HDR_SIZE 6

#define HCI_MON_NEW_INDEX	0
#define HCI_MON_DEL_INDEX	1
#define HCI_MON_COMMAND_PKT	2
#define HCI_MON_EVENT_PKT	3
#define HCI_MON_ACL_TX_PKT	4
#define HCI_MON_ACL_RX_PKT	5
#define HCI_MON_SCO_TX_PKT	6
#define HCI_MON_SCO_RX_PKT	7
#define HCI_MON_OPEN_INDEX	8
#define HCI_MON_CLOSE_INDEX	9
#define HCI_MON_INDEX_INFO	10
#define HCI_MON_VENDOR_DIAG	11
#define HCI_MON_SYSTEM_NOTE	12
#define HCI_MON_USER_LOGGING	13

struct hci_mon_new_index {
	__u8		type;
	__u8		bus;
	bdaddr_t	bdaddr;
	char		name[8];
} __packed;
#define HCI_MON_NEW_INDEX_SIZE 16

struct hci_mon_index_info {
	bdaddr_t	bdaddr;
	__le16		manufacturer;
} __packed;
#define HCI_MON_INDEX_INFO_SIZE 8

#endif /* __HCI_MON_H */
