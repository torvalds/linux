/*
   BlueZ - Bluetooth protocol stack for Linux
   Copyright (C) 2000-2001 Qualcomm Incorporated
   Copyright 2023 NXP

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

#ifndef __BLUETOOTH_H
#define __BLUETOOTH_H

#include <linux/poll.h>
#include <net/sock.h>
#include <linux/seq_file.h>

#define BT_SUBSYS_VERSION	2
#define BT_SUBSYS_REVISION	22

#ifndef AF_BLUETOOTH
#define AF_BLUETOOTH	31
#define PF_BLUETOOTH	AF_BLUETOOTH
#endif

/* Bluetooth versions */
#define BLUETOOTH_VER_1_1	1
#define BLUETOOTH_VER_1_2	2
#define BLUETOOTH_VER_2_0	3
#define BLUETOOTH_VER_2_1	4
#define BLUETOOTH_VER_4_0	6

/* Reserv for core and drivers use */
#define BT_SKB_RESERVE	8

#define BTPROTO_L2CAP	0
#define BTPROTO_HCI	1
#define BTPROTO_SCO	2
#define BTPROTO_RFCOMM	3
#define BTPROTO_BNEP	4
#define BTPROTO_CMTP	5
#define BTPROTO_HIDP	6
#define BTPROTO_AVDTP	7
#define BTPROTO_ISO	8
#define BTPROTO_LAST	BTPROTO_ISO

#define SOL_HCI		0
#define SOL_L2CAP	6
#define SOL_SCO		17
#define SOL_RFCOMM	18

#define BT_SECURITY	4
struct bt_security {
	__u8 level;
	__u8 key_size;
};
#define BT_SECURITY_SDP		0
#define BT_SECURITY_LOW		1
#define BT_SECURITY_MEDIUM	2
#define BT_SECURITY_HIGH	3
#define BT_SECURITY_FIPS	4

#define BT_DEFER_SETUP	7

#define BT_FLUSHABLE	8

#define BT_FLUSHABLE_OFF	0
#define BT_FLUSHABLE_ON		1

#define BT_POWER	9
struct bt_power {
	__u8 force_active;
};
#define BT_POWER_FORCE_ACTIVE_OFF 0
#define BT_POWER_FORCE_ACTIVE_ON  1

#define BT_CHANNEL_POLICY	10

/* BR/EDR only (default policy)
 *   AMP controllers cannot be used.
 *   Channel move requests from the remote device are denied.
 *   If the L2CAP channel is currently using AMP, move the channel to BR/EDR.
 */
#define BT_CHANNEL_POLICY_BREDR_ONLY		0

/* BR/EDR Preferred
 *   Allow use of AMP controllers.
 *   If the L2CAP channel is currently on AMP, move it to BR/EDR.
 *   Channel move requests from the remote device are allowed.
 */
#define BT_CHANNEL_POLICY_BREDR_PREFERRED	1

/* AMP Preferred
 *   Allow use of AMP controllers
 *   If the L2CAP channel is currently on BR/EDR and AMP controller
 *     resources are available, initiate a channel move to AMP.
 *   Channel move requests from the remote device are allowed.
 *   If the L2CAP socket has not been connected yet, try to create
 *     and configure the channel directly on an AMP controller rather
 *     than BR/EDR.
 */
#define BT_CHANNEL_POLICY_AMP_PREFERRED		2

#define BT_VOICE		11
struct bt_voice {
	__u16 setting;
};

#define BT_VOICE_TRANSPARENT			0x0003
#define BT_VOICE_CVSD_16BIT			0x0060

#define BT_SNDMTU		12
#define BT_RCVMTU		13
#define BT_PHY			14

#define BT_PHY_BR_1M_1SLOT	0x00000001
#define BT_PHY_BR_1M_3SLOT	0x00000002
#define BT_PHY_BR_1M_5SLOT	0x00000004
#define BT_PHY_EDR_2M_1SLOT	0x00000008
#define BT_PHY_EDR_2M_3SLOT	0x00000010
#define BT_PHY_EDR_2M_5SLOT	0x00000020
#define BT_PHY_EDR_3M_1SLOT	0x00000040
#define BT_PHY_EDR_3M_3SLOT	0x00000080
#define BT_PHY_EDR_3M_5SLOT	0x00000100
#define BT_PHY_LE_1M_TX		0x00000200
#define BT_PHY_LE_1M_RX		0x00000400
#define BT_PHY_LE_2M_TX		0x00000800
#define BT_PHY_LE_2M_RX		0x00001000
#define BT_PHY_LE_CODED_TX	0x00002000
#define BT_PHY_LE_CODED_RX	0x00004000

#define BT_MODE			15

#define BT_MODE_BASIC		0x00
#define BT_MODE_ERTM		0x01
#define BT_MODE_STREAMING	0x02
#define BT_MODE_LE_FLOWCTL	0x03
#define BT_MODE_EXT_FLOWCTL	0x04

#define BT_PKT_STATUS           16

#define BT_SCM_PKT_STATUS	0x03

#define BT_ISO_QOS		17

#define BT_ISO_QOS_CIG_UNSET	0xff
#define BT_ISO_QOS_CIS_UNSET	0xff

#define BT_ISO_QOS_BIG_UNSET	0xff
#define BT_ISO_QOS_BIS_UNSET	0xff

#define BT_ISO_SYNC_TIMEOUT	0x07d0 /* 20 secs */

struct bt_iso_io_qos {
	__u32 interval;
	__u16 latency;
	__u16 sdu;
	__u8  phy;
	__u8  rtn;
};

struct bt_iso_ucast_qos {
	__u8  cig;
	__u8  cis;
	__u8  sca;
	__u8  packing;
	__u8  framing;
	struct bt_iso_io_qos in;
	struct bt_iso_io_qos out;
};

struct bt_iso_bcast_qos {
	__u8  big;
	__u8  bis;
	__u8  sync_factor;
	__u8  packing;
	__u8  framing;
	struct bt_iso_io_qos in;
	struct bt_iso_io_qos out;
	__u8  encryption;
	__u8  bcode[16];
	__u8  options;
	__u16 skip;
	__u16 sync_timeout;
	__u8  sync_cte_type;
	__u8  mse;
	__u16 timeout;
};

struct bt_iso_qos {
	union {
		struct bt_iso_ucast_qos ucast;
		struct bt_iso_bcast_qos bcast;
	};
};

#define BT_ISO_PHY_1M		0x01
#define BT_ISO_PHY_2M		0x02
#define BT_ISO_PHY_CODED	0x04
#define BT_ISO_PHY_ANY		(BT_ISO_PHY_1M | BT_ISO_PHY_2M | \
				 BT_ISO_PHY_CODED)

#define BT_CODEC	19

struct	bt_codec_caps {
	__u8	len;
	__u8	data[];
} __packed;

struct bt_codec {
	__u8	id;
	__u16	cid;
	__u16	vid;
	__u8	data_path;
	__u8	num_caps;
} __packed;

struct bt_codecs {
	__u8		num_codecs;
	struct bt_codec	codecs[];
} __packed;

#define BT_CODEC_CVSD		0x02
#define BT_CODEC_TRANSPARENT	0x03
#define BT_CODEC_MSBC		0x05

#define BT_ISO_BASE		20

__printf(1, 2)
void bt_info(const char *fmt, ...);
__printf(1, 2)
void bt_warn(const char *fmt, ...);
__printf(1, 2)
void bt_err(const char *fmt, ...);
#if IS_ENABLED(CONFIG_BT_FEATURE_DEBUG)
void bt_dbg_set(bool enable);
bool bt_dbg_get(void);
__printf(1, 2)
void bt_dbg(const char *fmt, ...);
#endif
__printf(1, 2)
void bt_warn_ratelimited(const char *fmt, ...);
__printf(1, 2)
void bt_err_ratelimited(const char *fmt, ...);

#define BT_INFO(fmt, ...)	bt_info(fmt "\n", ##__VA_ARGS__)
#define BT_WARN(fmt, ...)	bt_warn(fmt "\n", ##__VA_ARGS__)
#define BT_ERR(fmt, ...)	bt_err(fmt "\n", ##__VA_ARGS__)

#if IS_ENABLED(CONFIG_BT_FEATURE_DEBUG)
#define BT_DBG(fmt, ...)	bt_dbg(fmt "\n", ##__VA_ARGS__)
#else
#define BT_DBG(fmt, ...)	pr_debug(fmt "\n", ##__VA_ARGS__)
#endif

#define bt_dev_name(hdev) ((hdev) ? (hdev)->name : "null")

#define bt_dev_info(hdev, fmt, ...)				\
	BT_INFO("%s: " fmt, bt_dev_name(hdev), ##__VA_ARGS__)
#define bt_dev_warn(hdev, fmt, ...)				\
	BT_WARN("%s: " fmt, bt_dev_name(hdev), ##__VA_ARGS__)
#define bt_dev_err(hdev, fmt, ...)				\
	BT_ERR("%s: " fmt, bt_dev_name(hdev), ##__VA_ARGS__)
#define bt_dev_dbg(hdev, fmt, ...)				\
	BT_DBG("%s: " fmt, bt_dev_name(hdev), ##__VA_ARGS__)

#define bt_dev_warn_ratelimited(hdev, fmt, ...)			\
	bt_warn_ratelimited("%s: " fmt, bt_dev_name(hdev), ##__VA_ARGS__)
#define bt_dev_err_ratelimited(hdev, fmt, ...)			\
	bt_err_ratelimited("%s: " fmt, bt_dev_name(hdev), ##__VA_ARGS__)

/* Connection and socket states */
enum bt_sock_state {
	BT_CONNECTED = 1, /* Equal to TCP_ESTABLISHED to make net code happy */
	BT_OPEN,
	BT_BOUND,
	BT_LISTEN,
	BT_CONNECT,
	BT_CONNECT2,
	BT_CONFIG,
	BT_DISCONN,
	BT_CLOSED
};

/* If unused will be removed by compiler */
static inline const char *state_to_string(int state)
{
	switch (state) {
	case BT_CONNECTED:
		return "BT_CONNECTED";
	case BT_OPEN:
		return "BT_OPEN";
	case BT_BOUND:
		return "BT_BOUND";
	case BT_LISTEN:
		return "BT_LISTEN";
	case BT_CONNECT:
		return "BT_CONNECT";
	case BT_CONNECT2:
		return "BT_CONNECT2";
	case BT_CONFIG:
		return "BT_CONFIG";
	case BT_DISCONN:
		return "BT_DISCONN";
	case BT_CLOSED:
		return "BT_CLOSED";
	}

	return "invalid state";
}

/* BD Address */
typedef struct {
	__u8 b[6];
} __packed bdaddr_t;

/* BD Address type */
#define BDADDR_BREDR		0x00
#define BDADDR_LE_PUBLIC	0x01
#define BDADDR_LE_RANDOM	0x02

static inline bool bdaddr_type_is_valid(u8 type)
{
	switch (type) {
	case BDADDR_BREDR:
	case BDADDR_LE_PUBLIC:
	case BDADDR_LE_RANDOM:
		return true;
	}

	return false;
}

static inline bool bdaddr_type_is_le(u8 type)
{
	switch (type) {
	case BDADDR_LE_PUBLIC:
	case BDADDR_LE_RANDOM:
		return true;
	}

	return false;
}

#define BDADDR_ANY  (&(bdaddr_t) {{0, 0, 0, 0, 0, 0}})
#define BDADDR_NONE (&(bdaddr_t) {{0xff, 0xff, 0xff, 0xff, 0xff, 0xff}})

/* Copy, swap, convert BD Address */
static inline int bacmp(const bdaddr_t *ba1, const bdaddr_t *ba2)
{
	return memcmp(ba1, ba2, sizeof(bdaddr_t));
}
static inline void bacpy(bdaddr_t *dst, const bdaddr_t *src)
{
	memcpy(dst, src, sizeof(bdaddr_t));
}

void baswap(bdaddr_t *dst, const bdaddr_t *src);

/* Common socket structures and functions */

#define bt_sk(__sk) ((struct bt_sock *) __sk)

struct bt_sock {
	struct sock sk;
	struct list_head accept_q;
	struct sock *parent;
	unsigned long flags;
	void (*skb_msg_name)(struct sk_buff *, void *, int *);
	void (*skb_put_cmsg)(struct sk_buff *, struct msghdr *, struct sock *);
};

enum {
	BT_SK_DEFER_SETUP,
	BT_SK_SUSPEND,
	BT_SK_PKT_STATUS
};

struct bt_sock_list {
	struct hlist_head head;
	rwlock_t          lock;
#ifdef CONFIG_PROC_FS
        int (* custom_seq_show)(struct seq_file *, void *);
#endif
};

int  bt_sock_register(int proto, const struct net_proto_family *ops);
void bt_sock_unregister(int proto);
void bt_sock_link(struct bt_sock_list *l, struct sock *s);
void bt_sock_unlink(struct bt_sock_list *l, struct sock *s);
bool bt_sock_linked(struct bt_sock_list *l, struct sock *s);
struct sock *bt_sock_alloc(struct net *net, struct socket *sock,
			   struct proto *prot, int proto, gfp_t prio, int kern);
int  bt_sock_recvmsg(struct socket *sock, struct msghdr *msg, size_t len,
		     int flags);
int  bt_sock_stream_recvmsg(struct socket *sock, struct msghdr *msg,
			    size_t len, int flags);
__poll_t bt_sock_poll(struct file *file, struct socket *sock, poll_table *wait);
int  bt_sock_ioctl(struct socket *sock, unsigned int cmd, unsigned long arg);
int  bt_sock_wait_state(struct sock *sk, int state, unsigned long timeo);
int  bt_sock_wait_ready(struct sock *sk, unsigned int msg_flags);

void bt_accept_enqueue(struct sock *parent, struct sock *sk, bool bh);
void bt_accept_unlink(struct sock *sk);
struct sock *bt_accept_dequeue(struct sock *parent, struct socket *newsock);

/* Skb helpers */
struct l2cap_ctrl {
	u8	sframe:1,
		poll:1,
		final:1,
		fcs:1,
		sar:2,
		super:2;

	u16	reqseq;
	u16	txseq;
	u8	retries;
	__le16  psm;
	bdaddr_t bdaddr;
	struct l2cap_chan *chan;
};

struct hci_dev;

typedef void (*hci_req_complete_t)(struct hci_dev *hdev, u8 status, u16 opcode);
typedef void (*hci_req_complete_skb_t)(struct hci_dev *hdev, u8 status,
				       u16 opcode, struct sk_buff *skb);

void hci_req_cmd_complete(struct hci_dev *hdev, u16 opcode, u8 status,
			  hci_req_complete_t *req_complete,
			  hci_req_complete_skb_t *req_complete_skb);

#define HCI_REQ_START	BIT(0)
#define HCI_REQ_SKB	BIT(1)

struct hci_ctrl {
	struct sock *sk;
	u16 opcode;
	u8 req_flags;
	u8 req_event;
	union {
		hci_req_complete_t req_complete;
		hci_req_complete_skb_t req_complete_skb;
	};
};

struct mgmt_ctrl {
	struct hci_dev *hdev;
	u16 opcode;
};

struct bt_skb_cb {
	u8 pkt_type;
	u8 force_active;
	u16 expect;
	u8 incoming:1;
	u8 pkt_status:2;
	union {
		struct l2cap_ctrl l2cap;
		struct hci_ctrl hci;
		struct mgmt_ctrl mgmt;
		struct scm_creds creds;
	};
};
#define bt_cb(skb) ((struct bt_skb_cb *)((skb)->cb))

#define hci_skb_pkt_type(skb) bt_cb((skb))->pkt_type
#define hci_skb_pkt_status(skb) bt_cb((skb))->pkt_status
#define hci_skb_expect(skb) bt_cb((skb))->expect
#define hci_skb_opcode(skb) bt_cb((skb))->hci.opcode
#define hci_skb_event(skb) bt_cb((skb))->hci.req_event
#define hci_skb_sk(skb) bt_cb((skb))->hci.sk

static inline struct sk_buff *bt_skb_alloc(unsigned int len, gfp_t how)
{
	struct sk_buff *skb;

	skb = alloc_skb(len + BT_SKB_RESERVE, how);
	if (skb)
		skb_reserve(skb, BT_SKB_RESERVE);
	return skb;
}

static inline struct sk_buff *bt_skb_send_alloc(struct sock *sk,
					unsigned long len, int nb, int *err)
{
	struct sk_buff *skb;

	skb = sock_alloc_send_skb(sk, len + BT_SKB_RESERVE, nb, err);
	if (skb)
		skb_reserve(skb, BT_SKB_RESERVE);

	if (!skb && *err)
		return NULL;

	*err = sock_error(sk);
	if (*err)
		goto out;

	if (sk->sk_shutdown) {
		*err = -ECONNRESET;
		goto out;
	}

	return skb;

out:
	kfree_skb(skb);
	return NULL;
}

/* Shall not be called with lock_sock held */
static inline struct sk_buff *bt_skb_sendmsg(struct sock *sk,
					     struct msghdr *msg,
					     size_t len, size_t mtu,
					     size_t headroom, size_t tailroom)
{
	struct sk_buff *skb;
	size_t size = min_t(size_t, len, mtu);
	int err;

	skb = bt_skb_send_alloc(sk, size + headroom + tailroom,
				msg->msg_flags & MSG_DONTWAIT, &err);
	if (!skb)
		return ERR_PTR(err);

	skb_reserve(skb, headroom);
	skb_tailroom_reserve(skb, mtu, tailroom);

	if (!copy_from_iter_full(skb_put(skb, size), size, &msg->msg_iter)) {
		kfree_skb(skb);
		return ERR_PTR(-EFAULT);
	}

	skb->priority = READ_ONCE(sk->sk_priority);

	return skb;
}

/* Similar to bt_skb_sendmsg but can split the msg into multiple fragments
 * accourding to the MTU.
 */
static inline struct sk_buff *bt_skb_sendmmsg(struct sock *sk,
					      struct msghdr *msg,
					      size_t len, size_t mtu,
					      size_t headroom, size_t tailroom)
{
	struct sk_buff *skb, **frag;

	skb = bt_skb_sendmsg(sk, msg, len, mtu, headroom, tailroom);
	if (IS_ERR(skb))
		return skb;

	len -= skb->len;
	if (!len)
		return skb;

	/* Add remaining data over MTU as continuation fragments */
	frag = &skb_shinfo(skb)->frag_list;
	while (len) {
		struct sk_buff *tmp;

		tmp = bt_skb_sendmsg(sk, msg, len, mtu, headroom, tailroom);
		if (IS_ERR(tmp)) {
			return skb;
		}

		len -= tmp->len;

		*frag = tmp;
		frag = &(*frag)->next;
	}

	return skb;
}

static inline int bt_copy_from_sockptr(void *dst, size_t dst_size,
				       sockptr_t src, size_t src_size)
{
	if (dst_size > src_size)
		return -EINVAL;

	return copy_from_sockptr(dst, src, dst_size);
}

int bt_to_errno(u16 code);
__u8 bt_status(int err);

void hci_sock_set_flag(struct sock *sk, int nr);
void hci_sock_clear_flag(struct sock *sk, int nr);
int hci_sock_test_flag(struct sock *sk, int nr);
unsigned short hci_sock_get_channel(struct sock *sk);
u32 hci_sock_get_cookie(struct sock *sk);

int hci_sock_init(void);
void hci_sock_cleanup(void);

int bt_sysfs_init(void);
void bt_sysfs_cleanup(void);

int bt_procfs_init(struct net *net, const char *name,
		   struct bt_sock_list *sk_list,
		   int (*seq_show)(struct seq_file *, void *));
void bt_procfs_cleanup(struct net *net, const char *name);

extern struct dentry *bt_debugfs;

int l2cap_init(void);
void l2cap_exit(void);

#if IS_ENABLED(CONFIG_BT_BREDR)
int sco_init(void);
void sco_exit(void);
#else
static inline int sco_init(void)
{
	return 0;
}

static inline void sco_exit(void)
{
}
#endif

#if IS_ENABLED(CONFIG_BT_LE)
int iso_init(void);
int iso_exit(void);
bool iso_enabled(void);
#else
static inline int iso_init(void)
{
	return 0;
}

static inline int iso_exit(void)
{
	return 0;
}

static inline bool iso_enabled(void)
{
	return false;
}
#endif

int mgmt_init(void);
void mgmt_exit(void);
void mgmt_cleanup(struct sock *sk);

void bt_sock_reclassify_lock(struct sock *sk, int proto);

#endif /* __BLUETOOTH_H */
