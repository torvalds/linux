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

/* L2CAP defaults */
#define L2CAP_DEFAULT_MTU		672
#define L2CAP_DEFAULT_MIN_MTU		48
#define L2CAP_DEFAULT_FLUSH_TO		0xffff
#define L2CAP_DEFAULT_TX_WINDOW		63
#define L2CAP_DEFAULT_EXT_WINDOW	0x3FFF
#define L2CAP_DEFAULT_MAX_TX		3
#define L2CAP_DEFAULT_RETRANS_TO	2000    /* 2 seconds */
#define L2CAP_DEFAULT_MONITOR_TO	12000   /* 12 seconds */
#define L2CAP_DEFAULT_MAX_PDU_SIZE	1009    /* Sized for 3-DH5 packet */
#define L2CAP_DEFAULT_ACK_TO		200
#define L2CAP_LE_DEFAULT_MTU		23
#define L2CAP_DEFAULT_MAX_SDU_SIZE	0xFFFF
#define L2CAP_DEFAULT_SDU_ITIME		0xFFFFFFFF
#define L2CAP_DEFAULT_ACC_LAT		0xFFFFFFFF

#define L2CAP_DISC_TIMEOUT             (100)
#define L2CAP_DISC_REJ_TIMEOUT         (5000)  /*  5 seconds */
#define L2CAP_ENC_TIMEOUT              (5000)  /*  5 seconds */
#define L2CAP_CONN_TIMEOUT             (40000) /* 40 seconds */
#define L2CAP_INFO_TIMEOUT             (4000)  /*  4 seconds */

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
#define L2CAP_CREATE_CHAN_REQ	0x0c
#define L2CAP_CREATE_CHAN_RSP	0x0d
#define L2CAP_MOVE_CHAN_REQ	0x0e
#define L2CAP_MOVE_CHAN_RSP	0x0f
#define L2CAP_MOVE_CHAN_CFM	0x10
#define L2CAP_MOVE_CHAN_CFM_RSP	0x11
#define L2CAP_CONN_PARAM_UPDATE_REQ	0x12
#define L2CAP_CONN_PARAM_UPDATE_RSP	0x13

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
#define L2CAP_FC_L2CAP		0x02
#define L2CAP_FC_A2MP		0x08

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

#define L2CAP_EXT_CTRL_REQSEQ_SHIFT	2
#define L2CAP_EXT_CTRL_SAR_SHIFT	16
#define L2CAP_EXT_CTRL_SUPER_SHIFT	16
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
#define L2CAP_HDR_SIZE		4
#define L2CAP_ENH_HDR_SIZE	6
#define L2CAP_EXT_HDR_SIZE	8

#define L2CAP_FCS_SIZE		2
#define L2CAP_SDULEN_SIZE	2
#define L2CAP_PSMLEN_SIZE	2

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

/* channel indentifier */
#define L2CAP_CID_SIGNALING	0x0001
#define L2CAP_CID_CONN_LESS	0x0002
#define L2CAP_CID_LE_DATA	0x0004
#define L2CAP_CID_LE_SIGNALING	0x0005
#define L2CAP_CID_SMP		0x0006
#define L2CAP_CID_DYN_START	0x0040
#define L2CAP_CID_DYN_END	0xffff

/* connect/create channel results */
#define L2CAP_CR_SUCCESS	0x0000
#define L2CAP_CR_PEND		0x0001
#define L2CAP_CR_BAD_PSM	0x0002
#define L2CAP_CR_SEC_BLOCK	0x0003
#define L2CAP_CR_NO_MEM		0x0004
#define L2CAP_CR_BAD_AMP	0x0005

/* connect/create channel status */
#define L2CAP_CS_NO_INFO	0x0000
#define L2CAP_CS_AUTHEN_PEND	0x0001
#define L2CAP_CS_AUTHOR_PEND	0x0002

struct l2cap_conf_req {
	__le16     dcid;
	__le16     flags;
	__u8       data[0];
} __packed;

struct l2cap_conf_rsp {
	__le16     scid;
	__le16     flags;
	__le16     result;
	__u8       data[0];
} __packed;

#define L2CAP_CONF_SUCCESS	0x0000
#define L2CAP_CONF_UNACCEPT	0x0001
#define L2CAP_CONF_REJECT	0x0002
#define L2CAP_CONF_UNKNOWN	0x0003
#define L2CAP_CONF_PENDING	0x0004
#define L2CAP_CONF_EFS_REJECT	0x0005

struct l2cap_conf_opt {
	__u8       type;
	__u8       len;
	__u8       val[0];
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
	__u8        data[0];
} __packed;

struct l2cap_create_chan_req {
	__le16      psm;
	__le16      scid;
	__u8        amp_id;
} __packed;

struct l2cap_create_chan_rsp {
	__le16      dcid;
	__le16      scid;
	__le16      result;
	__le16      status;
} __packed;

struct l2cap_move_chan_req {
	__le16      icid;
	__u8        dest_amp_id;
} __packed;

struct l2cap_move_chan_rsp {
	__le16      icid;
	__le16      result;
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

/* ----- L2CAP channels and connections ----- */
struct srej_list {
	__u16	tx_seq;
	struct list_head list;
};

struct l2cap_chan {
	struct sock *sk;

	struct l2cap_conn	*conn;

	__u8		state;

	atomic_t	refcnt;

	__le16		psm;
	__u16		dcid;
	__u16		scid;

	__u16		imtu;
	__u16		omtu;
	__u16		flush_to;
	__u8		mode;
	__u8		chan_type;
	__u8		chan_policy;

	__le16		sport;

	__u8		sec_level;

	__u8		ident;

	__u8		conf_req[64];
	__u8		conf_len;
	__u8		num_conf_req;
	__u8		num_conf_rsp;

	__u8		fcs;

	__u16		tx_win;
	__u16		tx_win_max;
	__u8		max_tx;
	__u16		retrans_timeout;
	__u16		monitor_timeout;
	__u16		mps;

	unsigned long	conf_state;
	unsigned long	conn_state;
	unsigned long	flags;

	__u16		next_tx_seq;
	__u16		expected_ack_seq;
	__u16		expected_tx_seq;
	__u16		buffer_seq;
	__u16		buffer_seq_srej;
	__u16		srej_save_reqseq;
	__u16		frames_sent;
	__u16		unacked_frames;
	__u8		retry_count;
	__u8		num_acked;
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
	struct list_head	srej_l;

	struct list_head list;
	struct list_head global_l;

	void		*data;
	struct l2cap_ops *ops;
};

struct l2cap_ops {
	char		*name;

	struct l2cap_chan	*(*new_connection) (void *data);
	int			(*recv) (void *data, struct sk_buff *skb);
	void			(*close) (void *data);
	void			(*state_change) (void *data, int state);
};

struct l2cap_conn {
	struct hci_conn	*hcon;
	struct hci_chan	*hchan;

	bdaddr_t	*dst;
	bdaddr_t	*src;

	unsigned int	mtu;

	__u32		feat_mask;

	__u8		info_state;
	__u8		info_ident;

	struct delayed_work info_timer;

	spinlock_t	lock;

	struct sk_buff *rx_skb;
	__u32		rx_len;
	__u8		tx_ident;

	__u8		disc_reason;

	struct delayed_work  security_timer;
	struct smp_chan *smp_chan;

	struct list_head chan_l;
	struct mutex	chan_lock;
};

#define L2CAP_INFO_CL_MTU_REQ_SENT	0x01
#define L2CAP_INFO_FEAT_MASK_REQ_SENT	0x04
#define L2CAP_INFO_FEAT_MASK_REQ_DONE	0x08

#define L2CAP_CHAN_RAW			1
#define L2CAP_CHAN_CONN_LESS		2
#define L2CAP_CHAN_CONN_ORIENTED	3

/* ----- L2CAP socket info ----- */
#define l2cap_pi(sk) ((struct l2cap_pinfo *) sk)

struct l2cap_pinfo {
	struct bt_sock	bt;
	struct l2cap_chan	*chan;
	struct sk_buff	*rx_busy_skb;
};

enum {
	CONF_REQ_SENT,
	CONF_INPUT_DONE,
	CONF_OUTPUT_DONE,
	CONF_MTU_DONE,
	CONF_MODE_DONE,
	CONF_CONNECT_PEND,
	CONF_NO_FCS_RECV,
	CONF_STATE2_DEVICE,
	CONF_EWS_RECV,
	CONF_LOC_CONF_PEND,
	CONF_REM_CONF_PEND,
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
};

static inline void l2cap_chan_hold(struct l2cap_chan *c)
{
	atomic_inc(&c->refcnt);
}

static inline void l2cap_chan_put(struct l2cap_chan *c)
{
	if (atomic_dec_and_test(&c->refcnt))
		kfree(c);
}

static inline void l2cap_set_timer(struct l2cap_chan *chan,
					struct delayed_work *work, long timeout)
{
	BT_DBG("chan %p state %d timeout %ld", chan, chan->state, timeout);

	if (!cancel_delayed_work(work))
		l2cap_chan_hold(chan);
	schedule_delayed_work(work, timeout);
}

static inline void l2cap_clear_timer(struct l2cap_chan *chan,
					struct delayed_work *work)
{
	if (cancel_delayed_work(work))
		l2cap_chan_put(chan);
}

#define __set_chan_timer(c, t) l2cap_set_timer(c, &c->chan_timer, (t))
#define __clear_chan_timer(c) l2cap_clear_timer(c, &c->chan_timer)
#define __set_retrans_timer(c) l2cap_set_timer(c, &c->retrans_timer, \
		msecs_to_jiffies(L2CAP_DEFAULT_RETRANS_TO));
#define __clear_retrans_timer(c) l2cap_clear_timer(c, &c->retrans_timer)
#define __set_monitor_timer(c) l2cap_set_timer(c, &c->monitor_timer, \
		msecs_to_jiffies(L2CAP_DEFAULT_MONITOR_TO));
#define __clear_monitor_timer(c) l2cap_clear_timer(c, &c->monitor_timer)
#define __set_ack_timer(c) l2cap_set_timer(c, &chan->ack_timer, \
		msecs_to_jiffies(L2CAP_DEFAULT_ACK_TO));
#define __clear_ack_timer(c) l2cap_clear_timer(c, &c->ack_timer)

static inline int __seq_offset(struct l2cap_chan *chan, __u16 seq1, __u16 seq2)
{
	int offset;

	offset = (seq1 - seq2) % (chan->tx_win_max + 1);
	if (offset < 0)
		offset += (chan->tx_win_max + 1);

	return offset;
}

static inline __u16 __next_seq(struct l2cap_chan *chan, __u16 seq)
{
	return (seq + 1) % (chan->tx_win_max + 1);
}

static inline int l2cap_tx_window_full(struct l2cap_chan *ch)
{
	int sub;

	sub = (ch->next_tx_seq - ch->expected_ack_seq) % 64;

	if (sub < 0)
		sub += 64;

	return sub == ch->remote_tx_win;
}

static inline __u16 __get_reqseq(struct l2cap_chan *chan, __u32 ctrl)
{
	if (test_bit(FLAG_EXT_CTRL, &chan->flags))
		return (ctrl & L2CAP_EXT_CTRL_REQSEQ) >>
						L2CAP_EXT_CTRL_REQSEQ_SHIFT;
	else
		return (ctrl & L2CAP_CTRL_REQSEQ) >> L2CAP_CTRL_REQSEQ_SHIFT;
}

static inline __u32 __set_reqseq(struct l2cap_chan *chan, __u32 reqseq)
{
	if (test_bit(FLAG_EXT_CTRL, &chan->flags))
		return (reqseq << L2CAP_EXT_CTRL_REQSEQ_SHIFT) &
							L2CAP_EXT_CTRL_REQSEQ;
	else
		return (reqseq << L2CAP_CTRL_REQSEQ_SHIFT) & L2CAP_CTRL_REQSEQ;
}

static inline __u16 __get_txseq(struct l2cap_chan *chan, __u32 ctrl)
{
	if (test_bit(FLAG_EXT_CTRL, &chan->flags))
		return (ctrl & L2CAP_EXT_CTRL_TXSEQ) >>
						L2CAP_EXT_CTRL_TXSEQ_SHIFT;
	else
		return (ctrl & L2CAP_CTRL_TXSEQ) >> L2CAP_CTRL_TXSEQ_SHIFT;
}

static inline __u32 __set_txseq(struct l2cap_chan *chan, __u32 txseq)
{
	if (test_bit(FLAG_EXT_CTRL, &chan->flags))
		return (txseq << L2CAP_EXT_CTRL_TXSEQ_SHIFT) &
							L2CAP_EXT_CTRL_TXSEQ;
	else
		return (txseq << L2CAP_CTRL_TXSEQ_SHIFT) & L2CAP_CTRL_TXSEQ;
}

static inline bool __is_sframe(struct l2cap_chan *chan, __u32 ctrl)
{
	if (test_bit(FLAG_EXT_CTRL, &chan->flags))
		return ctrl & L2CAP_EXT_CTRL_FRAME_TYPE;
	else
		return ctrl & L2CAP_CTRL_FRAME_TYPE;
}

static inline __u32 __set_sframe(struct l2cap_chan *chan)
{
	if (test_bit(FLAG_EXT_CTRL, &chan->flags))
		return L2CAP_EXT_CTRL_FRAME_TYPE;
	else
		return L2CAP_CTRL_FRAME_TYPE;
}

static inline __u8 __get_ctrl_sar(struct l2cap_chan *chan, __u32 ctrl)
{
	if (test_bit(FLAG_EXT_CTRL, &chan->flags))
		return (ctrl & L2CAP_EXT_CTRL_SAR) >> L2CAP_EXT_CTRL_SAR_SHIFT;
	else
		return (ctrl & L2CAP_CTRL_SAR) >> L2CAP_CTRL_SAR_SHIFT;
}

static inline __u32 __set_ctrl_sar(struct l2cap_chan *chan, __u32 sar)
{
	if (test_bit(FLAG_EXT_CTRL, &chan->flags))
		return (sar << L2CAP_EXT_CTRL_SAR_SHIFT) & L2CAP_EXT_CTRL_SAR;
	else
		return (sar << L2CAP_CTRL_SAR_SHIFT) & L2CAP_CTRL_SAR;
}

static inline bool __is_sar_start(struct l2cap_chan *chan, __u32 ctrl)
{
	return __get_ctrl_sar(chan, ctrl) == L2CAP_SAR_START;
}

static inline __u32 __get_sar_mask(struct l2cap_chan *chan)
{
	if (test_bit(FLAG_EXT_CTRL, &chan->flags))
		return L2CAP_EXT_CTRL_SAR;
	else
		return L2CAP_CTRL_SAR;
}

static inline __u8 __get_ctrl_super(struct l2cap_chan *chan, __u32 ctrl)
{
	if (test_bit(FLAG_EXT_CTRL, &chan->flags))
		return (ctrl & L2CAP_EXT_CTRL_SUPERVISE) >>
						L2CAP_EXT_CTRL_SUPER_SHIFT;
	else
		return (ctrl & L2CAP_CTRL_SUPERVISE) >> L2CAP_CTRL_SUPER_SHIFT;
}

static inline __u32 __set_ctrl_super(struct l2cap_chan *chan, __u32 super)
{
	if (test_bit(FLAG_EXT_CTRL, &chan->flags))
		return (super << L2CAP_EXT_CTRL_SUPER_SHIFT) &
						L2CAP_EXT_CTRL_SUPERVISE;
	else
		return (super << L2CAP_CTRL_SUPER_SHIFT) &
							L2CAP_CTRL_SUPERVISE;
}

static inline __u32 __set_ctrl_final(struct l2cap_chan *chan)
{
	if (test_bit(FLAG_EXT_CTRL, &chan->flags))
		return L2CAP_EXT_CTRL_FINAL;
	else
		return L2CAP_CTRL_FINAL;
}

static inline bool __is_ctrl_final(struct l2cap_chan *chan, __u32 ctrl)
{
	if (test_bit(FLAG_EXT_CTRL, &chan->flags))
		return ctrl & L2CAP_EXT_CTRL_FINAL;
	else
		return ctrl & L2CAP_CTRL_FINAL;
}

static inline __u32 __set_ctrl_poll(struct l2cap_chan *chan)
{
	if (test_bit(FLAG_EXT_CTRL, &chan->flags))
		return L2CAP_EXT_CTRL_POLL;
	else
		return L2CAP_CTRL_POLL;
}

static inline bool __is_ctrl_poll(struct l2cap_chan *chan, __u32 ctrl)
{
	if (test_bit(FLAG_EXT_CTRL, &chan->flags))
		return ctrl & L2CAP_EXT_CTRL_POLL;
	else
		return ctrl & L2CAP_CTRL_POLL;
}

static inline __u32 __get_control(struct l2cap_chan *chan, void *p)
{
	if (test_bit(FLAG_EXT_CTRL, &chan->flags))
		return get_unaligned_le32(p);
	else
		return get_unaligned_le16(p);
}

static inline void __put_control(struct l2cap_chan *chan, __u32 control,
								void *p)
{
	if (test_bit(FLAG_EXT_CTRL, &chan->flags))
		return put_unaligned_le32(control, p);
	else
		return put_unaligned_le16(control, p);
}

static inline __u8 __ctrl_size(struct l2cap_chan *chan)
{
	if (test_bit(FLAG_EXT_CTRL, &chan->flags))
		return L2CAP_EXT_HDR_SIZE - L2CAP_HDR_SIZE;
	else
		return L2CAP_ENH_HDR_SIZE - L2CAP_HDR_SIZE;
}

extern bool disable_ertm;

int l2cap_init_sockets(void);
void l2cap_cleanup_sockets(void);

void __l2cap_connect_rsp_defer(struct l2cap_chan *chan);
int __l2cap_wait_ack(struct sock *sk);

int l2cap_add_psm(struct l2cap_chan *chan, bdaddr_t *src, __le16 psm);
int l2cap_add_scid(struct l2cap_chan *chan,  __u16 scid);

struct l2cap_chan *l2cap_chan_create(struct sock *sk);
void l2cap_chan_close(struct l2cap_chan *chan, int reason);
void l2cap_chan_destroy(struct l2cap_chan *chan);
int l2cap_chan_connect(struct l2cap_chan *chan, __le16 psm, u16 cid,
								bdaddr_t *dst);
int l2cap_chan_send(struct l2cap_chan *chan, struct msghdr *msg, size_t len,
								u32 priority);
void l2cap_chan_busy(struct l2cap_chan *chan, int busy);
int l2cap_chan_check_security(struct l2cap_chan *chan);

#endif /* __L2CAP_H */
