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

#ifndef __BLUETOOTH_H
#define __BLUETOOTH_H

#include <linux/poll.h>
#include <net/sock.h>
#include <linux/seq_file.h>

#ifndef AF_BLUETOOTH
#define AF_BLUETOOTH	31
#define PF_BLUETOOTH	AF_BLUETOOTH
#endif

/* Bluetooth versions */
#define BLUETOOTH_VER_1_1	1
#define BLUETOOTH_VER_1_2	2
#define BLUETOOTH_VER_2_0	3

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

__printf(1, 2)
int bt_info(const char *fmt, ...);
__printf(1, 2)
int bt_err(const char *fmt, ...);

#define BT_INFO(fmt, ...)	bt_info(fmt "\n", ##__VA_ARGS__)
#define BT_ERR(fmt, ...)	bt_err(fmt "\n", ##__VA_ARGS__)
#define BT_DBG(fmt, ...)	pr_debug(fmt "\n", ##__VA_ARGS__)

/* Connection and socket states */
enum {
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

static inline bool bdaddr_type_is_valid(__u8 type)
{
	switch (type) {
	case BDADDR_BREDR:
	case BDADDR_LE_PUBLIC:
	case BDADDR_LE_RANDOM:
		return true;
	}

	return false;
}

static inline bool bdaddr_type_is_le(__u8 type)
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

void baswap(bdaddr_t *dst, bdaddr_t *src);

/* Common socket structures and functions */

#define bt_sk(__sk) ((struct bt_sock *) __sk)

struct bt_sock {
	struct sock sk;
	struct list_head accept_q;
	struct sock *parent;
	unsigned long flags;
	void (*skb_msg_name)(struct sk_buff *, void *, int *);
};

enum {
	BT_SK_DEFER_SETUP,
	BT_SK_SUSPEND,
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
int  bt_sock_recvmsg(struct kiocb *iocb, struct socket *sock,
				struct msghdr *msg, size_t len, int flags);
int  bt_sock_stream_recvmsg(struct kiocb *iocb, struct socket *sock,
			struct msghdr *msg, size_t len, int flags);
uint bt_sock_poll(struct file *file, struct socket *sock, poll_table *wait);
int  bt_sock_ioctl(struct socket *sock, unsigned int cmd, unsigned long arg);
int  bt_sock_wait_state(struct sock *sk, int state, unsigned long timeo);
int  bt_sock_wait_ready(struct sock *sk, unsigned long flags);

void bt_accept_enqueue(struct sock *parent, struct sock *sk);
void bt_accept_unlink(struct sock *sk);
struct sock *bt_accept_dequeue(struct sock *parent, struct socket *newsock);

/* Skb helpers */
struct l2cap_ctrl {
	__u8	sframe:1,
		poll:1,
		final:1,
		fcs:1,
		sar:2,
		super:2;
	__u16	reqseq;
	__u16	txseq;
	__u8	retries;
};

struct hci_dev;

typedef void (*hci_req_complete_t)(struct hci_dev *hdev, u8 status);

struct hci_req_ctrl {
	bool			start;
	u8			event;
	hci_req_complete_t	complete;
};

struct bt_skb_cb {
	__u8 pkt_type;
	__u8 incoming;
	__u16 expect;
	__u8 force_active;
	struct l2cap_chan *chan;
	struct l2cap_ctrl control;
	struct hci_req_ctrl req;
	bdaddr_t bdaddr;
	__le16 psm;
};
#define bt_cb(skb) ((struct bt_skb_cb *)((skb)->cb))

static inline struct sk_buff *bt_skb_alloc(unsigned int len, gfp_t how)
{
	struct sk_buff *skb;

	skb = alloc_skb(len + BT_SKB_RESERVE, how);
	if (skb) {
		skb_reserve(skb, BT_SKB_RESERVE);
		bt_cb(skb)->incoming  = 0;
	}
	return skb;
}

static inline struct sk_buff *bt_skb_send_alloc(struct sock *sk,
					unsigned long len, int nb, int *err)
{
	struct sk_buff *skb;

	skb = sock_alloc_send_skb(sk, len + BT_SKB_RESERVE, nb, err);
	if (skb) {
		skb_reserve(skb, BT_SKB_RESERVE);
		bt_cb(skb)->incoming  = 0;
	}

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

int bt_to_errno(__u16 code);

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

int sco_init(void);
void sco_exit(void);

void bt_sock_reclassify_lock(struct sock *sk, int proto);

#endif /* __BLUETOOTH_H */
