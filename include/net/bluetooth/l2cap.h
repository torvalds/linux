/*
   BlueZ - Bluetooth protocol stack for Linux
   Copyright (C) 2000-2001 Qualcomm Incorporated
   Copyright (C) 2009-2010 Gustavo F. Padovan <gustavo@padovan.org>
   Copyright (C) 2010 Google Inc.

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

#include <asm/unaligned.h>
#include <linux/atomic.h>

/* L2CAP defaults */
#define L2CAP_DEFAULT_MTU		672
#define L2CAP_DEFAULT_MIN_MTU		48
#define L2CAP_DEFAULT_FLUSH_TO		0xFFFF
#define L2CAP_EFS_DEFAULT_FLUSH_TO	0xFFFFFFFF
#define L2CAP_DEFAULT_TX_WINDOW		63
#define L2CAP_DEFAULT_EXT_WINDOW	0x3FFF
#define L2CAP_DEFAULT_MAX_TX		3
#define L2CAP_DEFAULT_RETRANS_TO	2000    /* 2 seconds */
#define L2CAP_DEFAULT_MONITOR_TO	12000   /* 12 seconds */
#define L2CAP_DEFAULT_MAX_PDU_SIZE	1492    /* Sized for AMP packet */
#define L2CAP_DEFAULT_ACK_TO		200
#define L2CAP_DEFAULT_MAX_SDU_SIZE	0xFFFF
#define L2CAP_DEFAULT_SDU_ITIME		0xFFFFFFFF
#define L2CAP_DEFAULT_ACC_LAT		0xFFFFFFFF
#define L2CAP_BREDR_MAX_PAYLOAD		1019    /* 3-DH5 packet */
#define L2CAP_LE_MIN_MTU		23
#define L2CAP_ECRED_CONN_SCID_MAX	5

#define L2CAP_DISC_TIMEOUT		msecs_to_jiffies(100)
#define L2CAP_DISC_REJ_TIMEOUT		msecs_to_jiffies(5000)
#define L2CAP_ENC_TIMEOUT		msecs_to_jiffies(5000)
#define L2CAP_CONN_TIMEOUT		msecs_to_jiffies(40000)
#define L2CAP_INFO_TIMEOUT		msecs_to_jiffies(4000)
#define L2CAP_MOVE_TIMEOUT		msecs_to_jiffies(4000)
#define L2CAP_MOVE_ERTX_TIMEOUT		msecs_to_jiffies(60000)
#define L2CAP_WAIT_ACK_POLL_PERIOD	msecs_to_jiffies(200)
#define L2CAP_WAIT_ACK_TIMEOUT		msecs_to_jiffies(10000)

/* L2CAP socket address */
struct sockaddr_l2 {
	sa_family_t	l2_family;
	__le16		l2_psm;
	bdaddr_t	l2_bdaddr;
	__le16		l2_cid;
	__u8		l2_bdaddr_type;
};

/* L2CAP socket options */
#define L2CAP_OPTIONS	0x01
struct l2cap_options {
	__u16 omtu;
	__u16 imtu;
	__u16 flush_to;
	__u8  mode;
	__u8  fcs;
	__u8  max_tx;
	__u16 txwin_size;
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
#define L2CAP_LM_FIPS		0x0040

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
#define L2CAP_CONN_PARAM_UPDATE_REQ	0x12
#define L2CAP_CONN_PARAM_UPDATE_RSP	0x13
#define L2CAP_LE_CONN_REQ	0x14
#define L2CAP_LE_CONN_RSP	0x15
#define L2CAP_LE_CREDITS	0x16
#define L2CAP_ECRED_CONN_REQ	0x17
#define L2CAP_ECRED_CONN_RSP	0x18
#define L2CAP_ECRED_RECONF_REQ	0x19
#define L2CAP_ECRED_RECONF_RSP	0x1a

/* L2CAP extended feature mask */
#define L2CAP_FEAT_FLOWCTL	0x00000001
#define L2CAP_FEAT_RETRANS	0x00000002
#define L2CAP_FEAT_BIDIR_QOS	0x00000004
#define L2CAP_FEAT_ERTM		0x00000008
#define L2CAP_FEAT_STREAMING	0x00000010
#define L2CAP_FEAT_FCS		0x00000020
#define L2CAP_FEAT_EXT_FLOW	0x00000040
#define L2CAP_FEAT_FIXED_CHAN	0x00000080
#define L2CAP_FEAT_EXT_WINDOW	0x00000100
#define L2CAP_FEAT_UCD		0x00000200

/* L2CAP checksum option */
#define L2CAP_FCS_NONE		0x00
#define L2CAP_FCS_CRC16		0x01

/* L2CAP fixed channels */
#define L2CAP_FC_SIG_BREDR	0x02
#define L2CAP_FC_CONNLESS	0x04
#define L2CAP_FC_ATT		0x10
#define L2CAP_FC_SIG_LE		0x20
#define L2CAP_FC_SMP_LE		0x40
#define L2CAP_FC_SMP_BREDR	0x80

/* L2CAP Control Field bit masks */
#define L2CAP_CTRL_SAR			0xC000
#define L2CAP_CTRL_REQSEQ		0x3F00
#define L2CAP_CTRL_TXSEQ		0x007E
#define L2CAP_CTRL_SUPERVISE		0x000C

#define L2CAP_CTRL_RETRANS		0x0080
#define L2CAP_CTRL_FINAL		0x0080
#define L2CAP_CTRL_POLL			0x0010
#define L2CAP_CTRL_FRAME_TYPE		0x0001 /* I- or S-Frame */

#define L2CAP_CTRL_TXSEQ_SHIFT		1
#define L2CAP_CTRL_SUPER_SHIFT		2
#define L2CAP_CTRL_POLL_SHIFT		4
#define L2CAP_CTRL_FINAL_SHIFT		7
#define L2CAP_CTRL_REQSEQ_SHIFT		8
#define L2CAP_CTRL_SAR_SHIFT		14

/* L2CAP Extended Control Field bit mask */
#define L2CAP_EXT_CTRL_TXSEQ		0xFFFC0000
#define L2CAP_EXT_CTRL_SAR		0x00030000
#define L2CAP_EXT_CTRL_SUPERVISE	0x00030000
#define L2CAP_EXT_CTRL_REQSEQ		0x0000FFFC

#define L2CAP_EXT_CTRL_POLL		0x00040000
#define L2CAP_EXT_CTRL_FINAL		0x00000002
#define L2CAP_EXT_CTRL_FRAME_TYPE	0x00000001 /* I- or S-Frame */

#define L2CAP_EXT_CTRL_FINAL_SHIFT	1
#define L2CAP_EXT_CTRL_REQSEQ_SHIFT	2
#define L2CAP_EXT_CTRL_SAR_SHIFT	16
#define L2CAP_EXT_CTRL_SUPER_SHIFT	16
#define L2CAP_EXT_CTRL_POLL_SHIFT	18
#define L2CAP_EXT_CTRL_TXSEQ_SHIFT	18

/* L2CAP Supervisory Function */
#define L2CAP_SUPER_RR		0x00
#define L2CAP_SUPER_REJ		0x01
#define L2CAP_SUPER_RNR		0x02
#define L2CAP_SUPER_SREJ	0x03

/* L2CAP Segmentation and Reassembly */
#define L2CAP_SAR_UNSEGMENTED	0x00
#define L2CAP_SAR_START		0x01
#define L2CAP_SAR_END		0x02
#define L2CAP_SAR_CONTINUE	0x03

/* L2CAP Command rej. reasons */
#define L2CAP_REJ_NOT_UNDERSTOOD	0x0000
#define L2CAP_REJ_MTU_EXCEEDED		0x0001
#define L2CAP_REJ_INVALID_CID		0x0002

/* L2CAP structures */
struct l2cap_hdr {
	__le16     len;
	__le16     cid;
} __packed;
#define L2CAP_LEN_SIZE		2
#define L2CAP_HDR_SIZE		4
#define L2CAP_ENH_HDR_SIZE	6
#define L2CAP_EXT_HDR_SIZE	8

#define L2CAP_FCS_SIZE		2
#define L2CAP_SDULEN_SIZE	2
#define L2CAP_PSMLEN_SIZE	2
#define L2CAP_ENH_CTRL_SIZE	2
#define L2CAP_EXT_CTRL_SIZE	4

struct l2cap_cmd_hdr {
	__u8       code;
	__u8       ident;
	__le16     len;
} __packed;
#define L2CAP_CMD_HDR_SIZE	4

struct l2cap_cmd_rej_unk {
	__le16     reason;
} __packed;

struct l2cap_cmd_rej_mtu {
	__le16     reason;
	__le16     max_mtu;
} __packed;

struct l2cap_cmd_rej_cid {
	__le16     reason;
	__le16     scid;
	__le16     dcid;
} __packed;

struct l2cap_conn_req {
	__le16     psm;
	__le16     scid;
} __packed;

struct l2cap_conn_rsp {
	__le16     dcid;
	__le16     scid;
	__le16     result;
	__le16     status;
} __packed;

/* protocol/service multiplexer (PSM) */
#define L2CAP_PSM_SDP		0x0001
#define L2CAP_PSM_RFCOMM	0x0003
#define L2CAP_PSM_3DSP		0x0021
#define L2CAP_PSM_IPSP		0x0023 /* 6LoWPAN */

#define L2CAP_PSM_DYN_START	0x1001
#define L2CAP_PSM_DYN_END	0xffff
#define L2CAP_PSM_AUTO_END	0x10ff
#define L2CAP_PSM_LE_DYN_START  0x0080
#define L2CAP_PSM_LE_DYN_END	0x00ff

/* channel identifier */
#define L2CAP_CID_SIGNALING	0x0001
#define L2CAP_CID_CONN_LESS	0x0002
#define L2CAP_CID_ATT		0x0004
#define L2CAP_CID_LE_SIGNALING	0x0005
#define L2CAP_CID_SMP		0x0006
#define L2CAP_CID_SMP_BREDR	0x0007
#define L2CAP_CID_DYN_START	0x0040
#define L2CAP_CID_DYN_END	0xffff
#define L2CAP_CID_LE_DYN_END	0x007f

/* connect/create channel results */
#define L2CAP_CR_SUCCESS	0x0000
#define L2CAP_CR_PEND		0x0001
#define L2CAP_CR_BAD_PSM	0x0002
#define L2CAP_CR_SEC_BLOCK	0x0003
#define L2CAP_CR_NO_MEM		0x0004
#define L2CAP_CR_INVALID_SCID	0x0006
#define L2CAP_CR_SCID_IN_USE	0x0007

/* credit based connect results */
#define L2CAP_CR_LE_SUCCESS		0x0000
#define L2CAP_CR_LE_BAD_PSM		0x0002
#define L2CAP_CR_LE_NO_MEM		0x0004
#define L2CAP_CR_LE_AUTHENTICATION	0x0005
#define L2CAP_CR_LE_AUTHORIZATION	0x0006
#define L2CAP_CR_LE_BAD_KEY_SIZE	0x0007
#define L2CAP_CR_LE_ENCRYPTION		0x0008
#define L2CAP_CR_LE_INVALID_SCID	0x0009
#define L2CAP_CR_LE_SCID_IN_USE		0X000A
#define L2CAP_CR_LE_UNACCEPT_PARAMS	0X000B
#define L2CAP_CR_LE_INVALID_PARAMS	0X000C

/* connect/create channel status */
#define L2CAP_CS_NO_INFO	0x0000
#define L2CAP_CS_AUTHEN_PEND	0x0001
#define L2CAP_CS_AUTHOR_PEND	0x0002

struct l2cap_conf_req {
	__le16     dcid;
	__le16     flags;
	__u8       data[];
} __packed;

struct l2cap_conf_rsp {
	__le16     scid;
	__le16     flags;
	__le16     result;
	__u8       data[];
} __packed;

#define L2CAP_CONF_SUCCESS	0x0000
#define L2CAP_CONF_UNACCEPT	0x0001
#define L2CAP_CONF_REJECT	0x0002
#define L2CAP_CONF_UNKNOWN	0x0003
#define L2CAP_CONF_PENDING	0x0004
#define L2CAP_CONF_EFS_REJECT	0x0005

/* configuration req/rsp continuation flag */
#define L2CAP_CONF_FLAG_CONTINUATION	0x0001

struct l2cap_conf_opt {
	__u8       type;
	__u8       len;
	__u8       val[];
} __packed;
#define L2CAP_CONF_OPT_SIZE	2

#define L2CAP_CONF_HINT		0x80
#define L2CAP_CONF_MASK		0x7f

#define L2CAP_CONF_MTU		0x01
#define L2CAP_CONF_FLUSH_TO	0x02
#define L2CAP_CONF_QOS		0x03
#define L2CAP_CONF_RFC		0x04
#define L2CAP_CONF_FCS		0x05
#define L2CAP_CONF_EFS		0x06
#define L2CAP_CONF_EWS		0x07

#define L2CAP_CONF_MAX_SIZE	22

struct l2cap_conf_rfc {
	__u8       mode;
	__u8       txwin_size;
	__u8       max_transmit;
	__le16     retrans_timeout;
	__le16     monitor_timeout;
	__le16     max_pdu_size;
} __packed;

#define L2CAP_MODE_BASIC	0x00
#define L2CAP_MODE_RETRANS	0x01
#define L2CAP_MODE_FLOWCTL	0x02
#define L2CAP_MODE_ERTM		0x03
#define L2CAP_MODE_STREAMING	0x04

/* Unlike the above this one doesn't actually map to anything that would
 * ever be sent over the air. Therefore, use a value that's unlikely to
 * ever be used in the BR/EDR configuration phase.
 */
#define L2CAP_MODE_LE_FLOWCTL	0x80
#define L2CAP_MODE_EXT_FLOWCTL	0x81

struct l2cap_conf_efs {
	__u8	id;
	__u8	stype;
	__le16	msdu;
	__le32	sdu_itime;
	__le32	acc_lat;
	__le32	flush_to;
} __packed;

#define L2CAP_SERV_NOTRAFIC	0x00
#define L2CAP_SERV_BESTEFFORT	0x01
#define L2CAP_SERV_GUARANTEED	0x02

#define L2CAP_BESTEFFORT_ID	0x01

struct l2cap_disconn_req {
	__le16     dcid;
	__le16     scid;
} __packed;

struct l2cap_disconn_rsp {
	__le16     dcid;
	__le16     scid;
} __packed;

struct l2cap_info_req {
	__le16      type;
} __packed;

struct l2cap_info_rsp {
	__le16      type;
	__le16      result;
	__u8        data[];
} __packed;

#define L2CAP_MR_SUCCESS	0x0000
#define L2CAP_MR_PEND		0x0001
#define L2CAP_MR_BAD_ID		0x0002
#define L2CAP_MR_SAME_ID	0x0003
#define L2CAP_MR_NOT_SUPP	0x0004
#define L2CAP_MR_COLLISION	0x0005
#define L2CAP_MR_NOT_ALLOWED	0x0006

struct l2cap_move_chan_cfm {
	__le16      icid;
	__le16      result;
} __packed;

#define L2CAP_MC_CONFIRMED	0x0000
#define L2CAP_MC_UNCONFIRMED	0x0001

struct l2cap_move_chan_cfm_rsp {
	__le16      icid;
} __packed;

/* info type */
#define L2CAP_IT_CL_MTU		0x0001
#define L2CAP_IT_FEAT_MASK	0x0002
#define L2CAP_IT_FIXED_CHAN	0x0003

/* info result */
#define L2CAP_IR_SUCCESS	0x0000
#define L2CAP_IR_NOTSUPP	0x0001

struct l2cap_conn_param_update_req {
	__le16      min;
	__le16      max;
	__le16      latency;
	__le16      to_multiplier;
} __packed;

struct l2cap_conn_param_update_rsp {
	__le16      result;
} __packed;

/* Connection Parameters result */
#define L2CAP_CONN_PARAM_ACCEPTED	0x0000
#define L2CAP_CONN_PARAM_REJECTED	0x0001

struct l2cap_le_conn_req {
	__le16     psm;
	__le16     scid;
	__le16     mtu;
	__le16     mps;
	__le16     credits;
} __packed;

struct l2cap_le_conn_rsp {
	__le16     dcid;
	__le16     mtu;
	__le16     mps;
	__le16     credits;
	__le16     result;
} __packed;

struct l2cap_le_credits {
	__le16     cid;
	__le16     credits;
} __packed;

#define L2CAP_ECRED_MIN_MTU		64
#define L2CAP_ECRED_MIN_MPS		64
#define L2CAP_ECRED_MAX_CID		5

struct l2cap_ecred_conn_req {
	__le16 psm;
	__le16 mtu;
	__le16 mps;
	__le16 credits;
	__le16 scid[];
} __packed;

struct l2cap_ecred_conn_rsp {
	__le16 mtu;
	__le16 mps;
	__le16 credits;
	__le16 result;
	__le16 dcid[];
};

struct l2cap_ecred_reconf_req {
	__le16 mtu;
	__le16 mps;
	__le16 scid[];
} __packed;

#define L2CAP_RECONF_SUCCESS		0x0000
#define L2CAP_RECONF_INVALID_MTU	0x0001
#define L2CAP_RECONF_INVALID_MPS	0x0002

struct l2cap_ecred_reconf_rsp {
	__le16 result;
} __packed;

/* ----- L2CAP channels and connections ----- */
struct l2cap_seq_list {
	__u16	head;
	__u16	tail;
	__u16	mask;
	__u16	*list;
};

#define L2CAP_SEQ_LIST_CLEAR	0xFFFF
#define L2CAP_SEQ_LIST_TAIL	0x8000

struct l2cap_chan {
	struct l2cap_conn	*conn;
	struct kref	kref;
	atomic_t	nesting;

	__u8		state;

	bdaddr_t	dst;
	__u8		dst_type;
	bdaddr_t	src;
	__u8		src_type;
	__le16		psm;
	__le16		sport;
	__u16		dcid;
	__u16		scid;

	__u16		imtu;
	__u16		omtu;
	__u16		flush_to;
	__u8		mode;
	__u8		chan_type;
	__u8		chan_policy;

	__u8		sec_level;

	__u8		ident;

	__u8		conf_req[64];
	__u8		conf_len;
	__u8		num_conf_req;
	__u8		num_conf_rsp;

	__u8		fcs;

	__u16		tx_win;
	__u16		tx_win_max;
	__u16		ack_win;
	__u8		max_tx;
	__u16		retrans_timeout;
	__u16		monitor_timeout;
	__u16		mps;

	__u16		tx_credits;
	__u16		rx_credits;

	/* estimated available receive buffer space or -1 if unknown */
	ssize_t		rx_avail;

	__u8		tx_state;
	__u8		rx_state;

	unsigned long	conf_state;
	unsigned long	conn_state;
	unsigned long	flags;

	__u16		next_tx_seq;
	__u16		expected_ack_seq;
	__u16		expected_tx_seq;
	__u16		buffer_seq;
	__u16		srej_save_reqseq;
	__u16		last_acked_seq;
	__u16		frames_sent;
	__u16		unacked_frames;
	__u8		retry_count;
	__u16		sdu_len;
	struct sk_buff	*sdu;
	struct sk_buff	*sdu_last_frag;

	__u16		remote_tx_win;
	__u8		remote_max_tx;
	__u16		remote_mps;

	__u8		local_id;
	__u8		local_stype;
	__u16		local_msdu;
	__u32		local_sdu_itime;
	__u32		local_acc_lat;
	__u32		local_flush_to;

	__u8		remote_id;
	__u8		remote_stype;
	__u16		remote_msdu;
	__u32		remote_sdu_itime;
	__u32		remote_acc_lat;
	__u32		remote_flush_to;

	struct delayed_work	chan_timer;
	struct delayed_work	retrans_timer;
	struct delayed_work	monitor_timer;
	struct delayed_work	ack_timer;

	struct sk_buff		*tx_send_head;
	struct sk_buff_head	tx_q;
	struct sk_buff_head	srej_q;
	struct l2cap_seq_list	srej_list;
	struct l2cap_seq_list	retrans_list;

	struct list_head	list;
	struct list_head	global_l;

	void			*data;
	const struct l2cap_ops	*ops;
	struct mutex		lock;
};

struct l2cap_ops {
	char			*name;

	struct l2cap_chan	*(*new_connection) (struct l2cap_chan *chan);
	int			(*recv) (struct l2cap_chan * chan,
					 struct sk_buff *skb);
	void			(*teardown) (struct l2cap_chan *chan, int err);
	void			(*close) (struct l2cap_chan *chan);
	void			(*state_change) (struct l2cap_chan *chan,
						 int state, int err);
	void			(*ready) (struct l2cap_chan *chan);
	void			(*defer) (struct l2cap_chan *chan);
	void			(*resume) (struct l2cap_chan *chan);
	void			(*suspend) (struct l2cap_chan *chan);
	void			(*set_shutdown) (struct l2cap_chan *chan);
	long			(*get_sndtimeo) (struct l2cap_chan *chan);
	struct pid		*(*get_peer_pid) (struct l2cap_chan *chan);
	struct sk_buff		*(*alloc_skb) (struct l2cap_chan *chan,
					       unsigned long hdr_len,
					       unsigned long len, int nb);
	int			(*filter) (struct l2cap_chan * chan,
					   struct sk_buff *skb);
};

struct l2cap_conn {
	struct hci_conn		*hcon;
	struct hci_chan		*hchan;

	unsigned int		mtu;

	__u32			feat_mask;
	__u8			remote_fixed_chan;
	__u8			local_fixed_chan;

	__u8			info_state;
	__u8			info_ident;

	struct delayed_work	info_timer;

	struct sk_buff		*rx_skb;
	__u32			rx_len;
	__u8			tx_ident;
	struct mutex		ident_lock;

	struct sk_buff_head	pending_rx;
	struct work_struct	pending_rx_work;

	struct delayed_work	id_addr_timer;

	__u8			disc_reason;

	struct l2cap_chan	*smp;

	struct list_head	chan_l;
	struct mutex		chan_lock;
	struct kref		ref;
	struct list_head	users;
};

struct l2cap_user {
	struct list_head list;
	int (*probe) (struct l2cap_conn *conn, struct l2cap_user *user);
	void (*remove) (struct l2cap_conn *conn, struct l2cap_user *user);
};

#define L2CAP_INFO_CL_MTU_REQ_SENT	0x01
#define L2CAP_INFO_FEAT_MASK_REQ_SENT	0x04
#define L2CAP_INFO_FEAT_MASK_REQ_DONE	0x08

#define L2CAP_CHAN_RAW			1
#define L2CAP_CHAN_CONN_LESS		2
#define L2CAP_CHAN_CONN_ORIENTED	3
#define L2CAP_CHAN_FIXED		4

/* ----- L2CAP socket info ----- */
#define l2cap_pi(sk) ((struct l2cap_pinfo *) sk)

struct l2cap_rx_busy {
	struct list_head	list;
	struct sk_buff		*skb;
};

struct l2cap_pinfo {
	struct bt_sock		bt;
	struct l2cap_chan	*chan;
	struct list_head	rx_busy;
};

enum {
	CONF_REQ_SENT,
	CONF_INPUT_DONE,
	CONF_OUTPUT_DONE,
	CONF_MTU_DONE,
	CONF_MODE_DONE,
	CONF_CONNECT_PEND,
	CONF_RECV_NO_FCS,
	CONF_STATE2_DEVICE,
	CONF_EWS_RECV,
	CONF_LOC_CONF_PEND,
	CONF_REM_CONF_PEND,
	CONF_NOT_COMPLETE,
};

#define L2CAP_CONF_MAX_CONF_REQ 2
#define L2CAP_CONF_MAX_CONF_RSP 2

enum {
	CONN_SREJ_SENT,
	CONN_WAIT_F,
	CONN_SREJ_ACT,
	CONN_SEND_PBIT,
	CONN_REMOTE_BUSY,
	CONN_LOCAL_BUSY,
	CONN_REJ_ACT,
	CONN_SEND_FBIT,
	CONN_RNR_SENT,
};

/* Definitions for flags in l2cap_chan */
enum {
	FLAG_ROLE_SWITCH,
	FLAG_FORCE_ACTIVE,
	FLAG_FORCE_RELIABLE,
	FLAG_FLUSHABLE,
	FLAG_EXT_CTRL,
	FLAG_EFS_ENABLE,
	FLAG_DEFER_SETUP,
	FLAG_LE_CONN_REQ_SENT,
	FLAG_ECRED_CONN_REQ_SENT,
	FLAG_PENDING_SECURITY,
	FLAG_HOLD_HCI_CONN,
};

/* Lock nesting levels for L2CAP channels. We need these because lockdep
 * otherwise considers all channels equal and will e.g. complain about a
 * connection oriented channel triggering SMP procedures or a listening
 * channel creating and locking a child channel.
 */
enum {
	L2CAP_NESTING_SMP,
	L2CAP_NESTING_NORMAL,
	L2CAP_NESTING_PARENT,
};

enum {
	L2CAP_TX_STATE_XMIT,
	L2CAP_TX_STATE_WAIT_F,
};

enum {
	L2CAP_RX_STATE_RECV,
	L2CAP_RX_STATE_SREJ_SENT,
	L2CAP_RX_STATE_MOVE,
	L2CAP_RX_STATE_WAIT_P,
	L2CAP_RX_STATE_WAIT_F,
};

enum {
	L2CAP_TXSEQ_EXPECTED,
	L2CAP_TXSEQ_EXPECTED_SREJ,
	L2CAP_TXSEQ_UNEXPECTED,
	L2CAP_TXSEQ_UNEXPECTED_SREJ,
	L2CAP_TXSEQ_DUPLICATE,
	L2CAP_TXSEQ_DUPLICATE_SREJ,
	L2CAP_TXSEQ_INVALID,
	L2CAP_TXSEQ_INVALID_IGNORE,
};

enum {
	L2CAP_EV_DATA_REQUEST,
	L2CAP_EV_LOCAL_BUSY_DETECTED,
	L2CAP_EV_LOCAL_BUSY_CLEAR,
	L2CAP_EV_RECV_REQSEQ_AND_FBIT,
	L2CAP_EV_RECV_FBIT,
	L2CAP_EV_RETRANS_TO,
	L2CAP_EV_MONITOR_TO,
	L2CAP_EV_EXPLICIT_POLL,
	L2CAP_EV_RECV_IFRAME,
	L2CAP_EV_RECV_RR,
	L2CAP_EV_RECV_REJ,
	L2CAP_EV_RECV_RNR,
	L2CAP_EV_RECV_SREJ,
	L2CAP_EV_RECV_FRAME,
};

enum {
	L2CAP_MOVE_ROLE_NONE,
	L2CAP_MOVE_ROLE_INITIATOR,
	L2CAP_MOVE_ROLE_RESPONDER,
};

enum {
	L2CAP_MOVE_STABLE,
	L2CAP_MOVE_WAIT_REQ,
	L2CAP_MOVE_WAIT_RSP,
	L2CAP_MOVE_WAIT_RSP_SUCCESS,
	L2CAP_MOVE_WAIT_CONFIRM,
	L2CAP_MOVE_WAIT_CONFIRM_RSP,
	L2CAP_MOVE_WAIT_LOGICAL_COMP,
	L2CAP_MOVE_WAIT_LOGICAL_CFM,
	L2CAP_MOVE_WAIT_LOCAL_BUSY,
	L2CAP_MOVE_WAIT_PREPARE,
};

void l2cap_chan_hold(struct l2cap_chan *c);
struct l2cap_chan *l2cap_chan_hold_unless_zero(struct l2cap_chan *c);
void l2cap_chan_put(struct l2cap_chan *c);

static inline void l2cap_chan_lock(struct l2cap_chan *chan)
{
	mutex_lock_nested(&chan->lock, atomic_read(&chan->nesting));
}

static inline void l2cap_chan_unlock(struct l2cap_chan *chan)
{
	mutex_unlock(&chan->lock);
}

static inline void l2cap_set_timer(struct l2cap_chan *chan,
				   struct delayed_work *work, long timeout)
{
	BT_DBG("chan %p state %s timeout %ld", chan,
	       state_to_string(chan->state), timeout);

	/* If delayed work cancelled do not hold(chan)
	   since it is already done with previous set_timer */
	if (!cancel_delayed_work(work))
		l2cap_chan_hold(chan);

	schedule_delayed_work(work, timeout);
}

static inline bool l2cap_clear_timer(struct l2cap_chan *chan,
				     struct delayed_work *work)
{
	bool ret;

	/* put(chan) if delayed work cancelled otherwise it
	   is done in delayed work function */
	ret = cancel_delayed_work(work);
	if (ret)
		l2cap_chan_put(chan);

	return ret;
}

#define __set_chan_timer(c, t) l2cap_set_timer(c, &c->chan_timer, (t))
#define __clear_chan_timer(c) l2cap_clear_timer(c, &c->chan_timer)
#define __clear_retrans_timer(c) l2cap_clear_timer(c, &c->retrans_timer)
#define __clear_monitor_timer(c) l2cap_clear_timer(c, &c->monitor_timer)
#define __set_ack_timer(c) l2cap_set_timer(c, &chan->ack_timer, \
		msecs_to_jiffies(L2CAP_DEFAULT_ACK_TO));
#define __clear_ack_timer(c) l2cap_clear_timer(c, &c->ack_timer)

static inline int __seq_offset(struct l2cap_chan *chan, __u16 seq1, __u16 seq2)
{
	if (seq1 >= seq2)
		return seq1 - seq2;
	else
		return chan->tx_win_max + 1 - seq2 + seq1;
}

static inline __u16 __next_seq(struct l2cap_chan *chan, __u16 seq)
{
	return (seq + 1) % (chan->tx_win_max + 1);
}

static inline struct l2cap_chan *l2cap_chan_no_new_connection(struct l2cap_chan *chan)
{
	return NULL;
}

static inline int l2cap_chan_no_recv(struct l2cap_chan *chan, struct sk_buff *skb)
{
	return -ENOSYS;
}

static inline struct sk_buff *l2cap_chan_no_alloc_skb(struct l2cap_chan *chan,
						      unsigned long hdr_len,
						      unsigned long len, int nb)
{
	return ERR_PTR(-ENOSYS);
}

static inline void l2cap_chan_no_teardown(struct l2cap_chan *chan, int err)
{
}

static inline void l2cap_chan_no_close(struct l2cap_chan *chan)
{
}

static inline void l2cap_chan_no_ready(struct l2cap_chan *chan)
{
}

static inline void l2cap_chan_no_state_change(struct l2cap_chan *chan,
					      int state, int err)
{
}

static inline void l2cap_chan_no_defer(struct l2cap_chan *chan)
{
}

static inline void l2cap_chan_no_suspend(struct l2cap_chan *chan)
{
}

static inline void l2cap_chan_no_resume(struct l2cap_chan *chan)
{
}

static inline void l2cap_chan_no_set_shutdown(struct l2cap_chan *chan)
{
}

static inline long l2cap_chan_no_get_sndtimeo(struct l2cap_chan *chan)
{
	return 0;
}

extern bool disable_ertm;
extern bool enable_ecred;

int l2cap_init_sockets(void);
void l2cap_cleanup_sockets(void);
bool l2cap_is_socket(struct socket *sock);

void __l2cap_le_connect_rsp_defer(struct l2cap_chan *chan);
void __l2cap_ecred_conn_rsp_defer(struct l2cap_chan *chan);
void __l2cap_connect_rsp_defer(struct l2cap_chan *chan);

int l2cap_add_psm(struct l2cap_chan *chan, bdaddr_t *src, __le16 psm);
int l2cap_add_scid(struct l2cap_chan *chan,  __u16 scid);

struct l2cap_chan *l2cap_chan_create(void);
void l2cap_chan_close(struct l2cap_chan *chan, int reason);
int l2cap_chan_connect(struct l2cap_chan *chan, __le16 psm, u16 cid,
		       bdaddr_t *dst, u8 dst_type, u16 timeout);
int l2cap_chan_reconfigure(struct l2cap_chan *chan, __u16 mtu);
int l2cap_chan_send(struct l2cap_chan *chan, struct msghdr *msg, size_t len);
void l2cap_chan_busy(struct l2cap_chan *chan, int busy);
void l2cap_chan_rx_avail(struct l2cap_chan *chan, ssize_t rx_avail);
int l2cap_chan_check_security(struct l2cap_chan *chan, bool initiator);
void l2cap_chan_set_defaults(struct l2cap_chan *chan);
int l2cap_ertm_init(struct l2cap_chan *chan);
void l2cap_chan_add(struct l2cap_conn *conn, struct l2cap_chan *chan);
void __l2cap_chan_add(struct l2cap_conn *conn, struct l2cap_chan *chan);
typedef void (*l2cap_chan_func_t)(struct l2cap_chan *chan, void *data);
void l2cap_chan_list(struct l2cap_conn *conn, l2cap_chan_func_t func,
		     void *data);
void l2cap_chan_del(struct l2cap_chan *chan, int err);
void l2cap_send_conn_req(struct l2cap_chan *chan);
void l2cap_move_start(struct l2cap_chan *chan);
void l2cap_logical_cfm(struct l2cap_chan *chan, struct hci_chan *hchan,
		       u8 status);
void __l2cap_physical_cfm(struct l2cap_chan *chan, int result);

struct l2cap_conn *l2cap_conn_get(struct l2cap_conn *conn);
void l2cap_conn_put(struct l2cap_conn *conn);

int l2cap_register_user(struct l2cap_conn *conn, struct l2cap_user *user);
void l2cap_unregister_user(struct l2cap_conn *conn, struct l2cap_user *user);

#endif /* __L2CAP_H */
