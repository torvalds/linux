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

#ifndef __L2CAP_H
#define __L2CAP_H

/* L2CAP defaults */
#define L2CAP_DEFAULT_MTU		672
#define L2CAP_DEFAULT_MIN_MTU		48
#define L2CAP_DEFAULT_FLUSH_TO		0xffff
#define L2CAP_DEFAULT_TX_WINDOW		63
#define L2CAP_DEFAULT_NUM_TO_ACK        (L2CAP_DEFAULT_TX_WINDOW/5)
#define L2CAP_DEFAULT_MAX_TX		3
#define L2CAP_DEFAULT_RETRANS_TO	1000    /* 1 second */
#define L2CAP_DEFAULT_MONITOR_TO	12000   /* 12 seconds */
#define L2CAP_DEFAULT_MAX_PDU_SIZE	672

#define L2CAP_CONN_TIMEOUT	(40000) /* 40 seconds */
#define L2CAP_INFO_TIMEOUT	(4000)  /*  4 seconds */

/* L2CAP socket address */
struct sockaddr_l2 {
	sa_family_t	l2_family;
	__le16		l2_psm;
	bdaddr_t	l2_bdaddr;
	__le16		l2_cid;
};

/* L2CAP socket options */
#define L2CAP_OPTIONS	0x01
struct l2cap_options {
	__u16 omtu;
	__u16 imtu;
	__u16 flush_to;
	__u8  mode;
	__u8  fcs;
};

#define L2CAP_CONNINFO	0x02
struct l2cap_conninfo {
	__u16 hci_handle;
	__u8  dev_class[3];
};

#define L2CAP_LM	0x03
#define L2CAP_LM_MASTER		0x0001
#define L2CAP_LM_AUTH		0x0002
#define L2CAP_LM_ENCRYPT	0x0004
#define L2CAP_LM_TRUSTED	0x0008
#define L2CAP_LM_RELIABLE	0x0010
#define L2CAP_LM_SECURE		0x0020
#define L2CAP_LM_FLUSHABLE	0x0040

/* L2CAP command codes */
#define L2CAP_COMMAND_REJ	0x01
#define L2CAP_CONN_REQ		0x02
#define L2CAP_CONN_RSP		0x03
#define L2CAP_CONF_REQ		0x04
#define L2CAP_CONF_RSP		0x05
#define L2CAP_DISCONN_REQ	0x06
#define L2CAP_DISCONN_RSP	0x07
#define L2CAP_ECHO_REQ		0x08
#define L2CAP_ECHO_RSP		0x09
#define L2CAP_INFO_REQ		0x0a
#define L2CAP_INFO_RSP		0x0b

/* L2CAP feature mask */
#define L2CAP_FEAT_FLOWCTL	0x00000001
#define L2CAP_FEAT_RETRANS	0x00000002
#define L2CAP_FEAT_ERTM		0x00000008
#define L2CAP_FEAT_STREAMING	0x00000010
#define L2CAP_FEAT_FCS		0x00000020
#define L2CAP_FEAT_FIXED_CHAN	0x00000080

/* L2CAP checksum option */
#define L2CAP_FCS_NONE		0x00
#define L2CAP_FCS_CRC16		0x01

/* L2CAP Control Field bit masks */
#define L2CAP_CTRL_SAR               0xC000
#define L2CAP_CTRL_REQSEQ            0x3F00
#define L2CAP_CTRL_TXSEQ             0x007E
#define L2CAP_CTRL_RETRANS           0x0080
#define L2CAP_CTRL_FINAL             0x0080
#define L2CAP_CTRL_POLL              0x0010
#define L2CAP_CTRL_SUPERVISE         0x000C
#define L2CAP_CTRL_FRAME_TYPE        0x0001 /* I- or S-Frame */

#define L2CAP_CTRL_TXSEQ_SHIFT      1
#define L2CAP_CTRL_REQSEQ_SHIFT     8
#define L2CAP_CTRL_SAR_SHIFT       14

/* L2CAP Supervisory Function */
#define L2CAP_SUPER_RCV_READY           0x0000
#define L2CAP_SUPER_REJECT              0x0004
#define L2CAP_SUPER_RCV_NOT_READY       0x0008
#define L2CAP_SUPER_SELECT_REJECT       0x000C

/* L2CAP Segmentation and Reassembly */
#define L2CAP_SDU_UNSEGMENTED       0x0000
#define L2CAP_SDU_START             0x4000
#define L2CAP_SDU_END               0x8000
#define L2CAP_SDU_CONTINUE          0xC000

/* L2CAP structures */
struct l2cap_hdr {
	__le16     len;
	__le16     cid;
} __attribute__ ((packed));
#define L2CAP_HDR_SIZE		4

struct l2cap_cmd_hdr {
	__u8       code;
	__u8       ident;
	__le16     len;
} __attribute__ ((packed));
#define L2CAP_CMD_HDR_SIZE	4

struct l2cap_cmd_rej {
	__le16     reason;
} __attribute__ ((packed));

struct l2cap_conn_req {
	__le16     psm;
	__le16     scid;
} __attribute__ ((packed));

struct l2cap_conn_rsp {
	__le16     dcid;
	__le16     scid;
	__le16     result;
	__le16     status;
} __attribute__ ((packed));

/* channel indentifier */
#define L2CAP_CID_SIGNALING	0x0001
#define L2CAP_CID_CONN_LESS	0x0002
#define L2CAP_CID_DYN_START	0x0040
#define L2CAP_CID_DYN_END	0xffff

/* connect result */
#define L2CAP_CR_SUCCESS	0x0000
#define L2CAP_CR_PEND		0x0001
#define L2CAP_CR_BAD_PSM	0x0002
#define L2CAP_CR_SEC_BLOCK	0x0003
#define L2CAP_CR_NO_MEM		0x0004

/* connect status */
#define L2CAP_CS_NO_INFO	0x0000
#define L2CAP_CS_AUTHEN_PEND	0x0001
#define L2CAP_CS_AUTHOR_PEND	0x0002

struct l2cap_conf_req {
	__le16     dcid;
	__le16     flags;
	__u8       data[0];
} __attribute__ ((packed));

struct l2cap_conf_rsp {
	__le16     scid;
	__le16     flags;
	__le16     result;
	__u8       data[0];
} __attribute__ ((packed));

#define L2CAP_CONF_SUCCESS	0x0000
#define L2CAP_CONF_UNACCEPT	0x0001
#define L2CAP_CONF_REJECT	0x0002
#define L2CAP_CONF_UNKNOWN	0x0003

struct l2cap_conf_opt {
	__u8       type;
	__u8       len;
	__u8       val[0];
} __attribute__ ((packed));
#define L2CAP_CONF_OPT_SIZE	2

#define L2CAP_CONF_HINT		0x80
#define L2CAP_CONF_MASK		0x7f

#define L2CAP_CONF_MTU		0x01
#define L2CAP_CONF_FLUSH_TO	0x02
#define L2CAP_CONF_QOS		0x03
#define L2CAP_CONF_RFC		0x04
#define L2CAP_CONF_FCS		0x05

#define L2CAP_CONF_MAX_SIZE	22

struct l2cap_conf_rfc {
	__u8       mode;
	__u8       txwin_size;
	__u8       max_transmit;
	__le16     retrans_timeout;
	__le16     monitor_timeout;
	__le16     max_pdu_size;
} __attribute__ ((packed));

#define L2CAP_MODE_BASIC	0x00
#define L2CAP_MODE_RETRANS	0x01
#define L2CAP_MODE_FLOWCTL	0x02
#define L2CAP_MODE_ERTM		0x03
#define L2CAP_MODE_STREAMING	0x04

struct l2cap_disconn_req {
	__le16     dcid;
	__le16     scid;
} __attribute__ ((packed));

struct l2cap_disconn_rsp {
	__le16     dcid;
	__le16     scid;
} __attribute__ ((packed));

struct l2cap_info_req {
	__le16      type;
} __attribute__ ((packed));

struct l2cap_info_rsp {
	__le16      type;
	__le16      result;
	__u8        data[0];
} __attribute__ ((packed));

/* info type */
#define L2CAP_IT_CL_MTU     0x0001
#define L2CAP_IT_FEAT_MASK  0x0002
#define L2CAP_IT_FIXED_CHAN 0x0003

/* info result */
#define L2CAP_IR_SUCCESS    0x0000
#define L2CAP_IR_NOTSUPP    0x0001

/* ----- L2CAP connections ----- */
struct l2cap_chan_list {
	struct sock	*head;
	rwlock_t	lock;
	long		num;
};

struct l2cap_conn {
	struct hci_conn	*hcon;

	bdaddr_t	*dst;
	bdaddr_t	*src;

	unsigned int	mtu;

	__u32		feat_mask;

	__u8		info_state;
	__u8		info_ident;

	struct timer_list info_timer;

	spinlock_t	lock;

	struct sk_buff *rx_skb;
	__u32		rx_len;
	__u8		rx_ident;
	__u8		tx_ident;

	__u8		disc_reason;

	struct l2cap_chan_list chan_list;
};

#define L2CAP_INFO_CL_MTU_REQ_SENT	0x01
#define L2CAP_INFO_FEAT_MASK_REQ_SENT	0x04
#define L2CAP_INFO_FEAT_MASK_REQ_DONE	0x08

/* ----- L2CAP channel and socket info ----- */
#define l2cap_pi(sk) ((struct l2cap_pinfo *) sk)
#define TX_QUEUE(sk) (&l2cap_pi(sk)->tx_queue)
#define SREJ_QUEUE(sk) (&l2cap_pi(sk)->srej_queue)
#define SREJ_LIST(sk) (&l2cap_pi(sk)->srej_l.list)

struct srej_list {
	__u8	tx_seq;
	struct list_head list;
};

struct l2cap_pinfo {
	struct bt_sock	bt;
	__le16		psm;
	__u16		dcid;
	__u16		scid;

	__u16		imtu;
	__u16		omtu;
	__u16		flush_to;
	__u8		mode;
	__u8		num_conf_req;
	__u8		num_conf_rsp;

	__u8		fcs;
	__u8		sec_level;
	__u8		role_switch;
	__u8		force_reliable;
	__u8		flushable;

	__u8		conf_req[64];
	__u8		conf_len;
	__u8		conf_state;
	__u8		conn_state;

	__u8		next_tx_seq;
	__u8		expected_ack_seq;
	__u8		req_seq;
	__u8		expected_tx_seq;
	__u8		buffer_seq;
	__u8		buffer_seq_srej;
	__u8		srej_save_reqseq;
	__u8		unacked_frames;
	__u8		retry_count;
	__u8		num_to_ack;
	__u16		sdu_len;
	__u16		partial_sdu_len;
	struct sk_buff	*sdu;

	__u8		ident;

	__u8		remote_tx_win;
	__u8		remote_max_tx;
	__u16		retrans_timeout;
	__u16		monitor_timeout;
	__u16		max_pdu_size;

	__le16		sport;

	struct timer_list	retrans_timer;
	struct timer_list	monitor_timer;
	struct sk_buff_head	tx_queue;
	struct sk_buff_head	srej_queue;
	struct srej_list	srej_l;
	struct l2cap_conn	*conn;
	struct sock		*next_c;
	struct sock		*prev_c;
};

#define L2CAP_CONF_REQ_SENT       0x01
#define L2CAP_CONF_INPUT_DONE     0x02
#define L2CAP_CONF_OUTPUT_DONE    0x04
#define L2CAP_CONF_MTU_DONE       0x08
#define L2CAP_CONF_MODE_DONE      0x10
#define L2CAP_CONF_CONNECT_PEND   0x20
#define L2CAP_CONF_NO_FCS_RECV    0x40
#define L2CAP_CONF_STATE2_DEVICE  0x80

#define L2CAP_CONF_MAX_CONF_REQ 2
#define L2CAP_CONF_MAX_CONF_RSP 2

#define L2CAP_CONN_SAR_SDU         0x01
#define L2CAP_CONN_SREJ_SENT       0x02
#define L2CAP_CONN_WAIT_F          0x04
#define L2CAP_CONN_SREJ_ACT        0x08
#define L2CAP_CONN_SEND_PBIT       0x10
#define L2CAP_CONN_REMOTE_BUSY     0x20
#define L2CAP_CONN_LOCAL_BUSY      0x40

#define __mod_retrans_timer() mod_timer(&l2cap_pi(sk)->retrans_timer, \
		jiffies +  msecs_to_jiffies(L2CAP_DEFAULT_RETRANS_TO));
#define __mod_monitor_timer() mod_timer(&l2cap_pi(sk)->monitor_timer, \
		jiffies + msecs_to_jiffies(L2CAP_DEFAULT_MONITOR_TO));

static inline int l2cap_tx_window_full(struct sock *sk)
{
	struct l2cap_pinfo *pi = l2cap_pi(sk);
	int sub;

	sub = (pi->next_tx_seq - pi->expected_ack_seq) % 64;

	if (sub < 0)
		sub += 64;

	return (sub == pi->remote_tx_win);
}

#define __get_txseq(ctrl) ((ctrl) & L2CAP_CTRL_TXSEQ) >> 1
#define __get_reqseq(ctrl) ((ctrl) & L2CAP_CTRL_REQSEQ) >> 8
#define __is_iframe(ctrl) !((ctrl) & L2CAP_CTRL_FRAME_TYPE)
#define __is_sframe(ctrl) (ctrl) & L2CAP_CTRL_FRAME_TYPE
#define __is_sar_start(ctrl) ((ctrl) & L2CAP_CTRL_SAR) == L2CAP_SDU_START

void l2cap_load(void);

#endif /* __L2CAP_H */
