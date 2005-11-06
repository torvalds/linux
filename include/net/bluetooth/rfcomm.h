/* 
   RFCOMM implementation for Linux Bluetooth stack (BlueZ).
   Copyright (C) 2002 Maxim Krasnyansky <maxk@qualcomm.com>
   Copyright (C) 2002 Marcel Holtmann <marcel@holtmann.org>

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

#ifndef __RFCOMM_H
#define __RFCOMM_H

#define RFCOMM_PSM 3

#define RFCOMM_CONN_TIMEOUT (HZ * 30)
#define RFCOMM_DISC_TIMEOUT (HZ * 20)
#define RFCOMM_AUTH_TIMEOUT (HZ * 25)

#define RFCOMM_DEFAULT_MTU	127
#define RFCOMM_DEFAULT_CREDITS	7

#define RFCOMM_MAX_L2CAP_MTU	1024
#define RFCOMM_MAX_CREDITS	40

#define RFCOMM_SKB_HEAD_RESERVE	8
#define RFCOMM_SKB_TAIL_RESERVE	2
#define RFCOMM_SKB_RESERVE  (RFCOMM_SKB_HEAD_RESERVE + RFCOMM_SKB_TAIL_RESERVE)

#define RFCOMM_SABM	0x2f
#define RFCOMM_DISC	0x43
#define RFCOMM_UA	0x63
#define RFCOMM_DM	0x0f
#define RFCOMM_UIH	0xef

#define RFCOMM_TEST	0x08
#define RFCOMM_FCON	0x28
#define RFCOMM_FCOFF	0x18
#define RFCOMM_MSC	0x38
#define RFCOMM_RPN	0x24
#define RFCOMM_RLS	0x14
#define RFCOMM_PN	0x20
#define RFCOMM_NSC	0x04

#define RFCOMM_V24_FC	0x02
#define RFCOMM_V24_RTC	0x04
#define RFCOMM_V24_RTR	0x08
#define RFCOMM_V24_IC	0x40
#define RFCOMM_V24_DV	0x80

#define RFCOMM_RPN_BR_2400	0x0
#define RFCOMM_RPN_BR_4800	0x1
#define RFCOMM_RPN_BR_7200	0x2
#define RFCOMM_RPN_BR_9600	0x3
#define RFCOMM_RPN_BR_19200	0x4
#define RFCOMM_RPN_BR_38400	0x5
#define RFCOMM_RPN_BR_57600	0x6
#define RFCOMM_RPN_BR_115200	0x7
#define RFCOMM_RPN_BR_230400	0x8

#define RFCOMM_RPN_DATA_5	0x0
#define RFCOMM_RPN_DATA_6	0x1
#define RFCOMM_RPN_DATA_7	0x2
#define RFCOMM_RPN_DATA_8	0x3

#define RFCOMM_RPN_STOP_1	0
#define RFCOMM_RPN_STOP_15	1

#define RFCOMM_RPN_PARITY_NONE	0x0
#define RFCOMM_RPN_PARITY_ODD	0x1
#define RFCOMM_RPN_PARITY_EVEN	0x3
#define RFCOMM_RPN_PARITY_MARK	0x5
#define RFCOMM_RPN_PARITY_SPACE	0x7

#define RFCOMM_RPN_FLOW_NONE	0x00

#define RFCOMM_RPN_XON_CHAR	0x11
#define RFCOMM_RPN_XOFF_CHAR	0x13

#define RFCOMM_RPN_PM_BITRATE		0x0001
#define RFCOMM_RPN_PM_DATA		0x0002
#define RFCOMM_RPN_PM_STOP		0x0004
#define RFCOMM_RPN_PM_PARITY		0x0008
#define RFCOMM_RPN_PM_PARITY_TYPE	0x0010
#define RFCOMM_RPN_PM_XON		0x0020
#define RFCOMM_RPN_PM_XOFF		0x0040
#define RFCOMM_RPN_PM_FLOW		0x3F00

#define RFCOMM_RPN_PM_ALL		0x3F7F

struct rfcomm_hdr {
	u8 addr;
	u8 ctrl;
	u8 len;    // Actual size can be 2 bytes
} __attribute__ ((packed));

struct rfcomm_cmd {
	u8 addr;
	u8 ctrl;
	u8 len;
	u8 fcs;
} __attribute__ ((packed));

struct rfcomm_mcc {
	u8 type;
	u8 len;
} __attribute__ ((packed));

struct rfcomm_pn {
	u8  dlci;
	u8  flow_ctrl;
	u8  priority;
	u8  ack_timer;
	u16 mtu;
	u8  max_retrans;
	u8  credits;
} __attribute__ ((packed));

struct rfcomm_rpn {
	u8  dlci;
	u8  bit_rate;
	u8  line_settings;
	u8  flow_ctrl;
	u8  xon_char;
	u8  xoff_char;
	u16 param_mask;
} __attribute__ ((packed));

struct rfcomm_rls {
	u8  dlci;
	u8  status;
} __attribute__ ((packed));

struct rfcomm_msc {
	u8  dlci;
	u8  v24_sig;
} __attribute__ ((packed));

/* ---- Core structures, flags etc ---- */

struct rfcomm_session {
	struct list_head list;
	struct socket   *sock;
	unsigned long    state;
	unsigned long    flags;
	atomic_t         refcnt;
	int              initiator;

	/* Default DLC parameters */
	int    cfc;
	uint   mtu;

	struct list_head dlcs;
};

struct rfcomm_dlc {
	struct list_head      list;
	struct rfcomm_session *session;
	struct sk_buff_head   tx_queue;
	struct timer_list     timer;

	spinlock_t    lock;
	unsigned long state;
	unsigned long flags;
	atomic_t      refcnt;
	u8            dlci;
	u8            addr;
	u8            priority;
	u8            v24_sig;
	u8            mscex;

	u32           link_mode;

	uint          mtu;
	uint          cfc;
	uint          rx_credits;
	uint          tx_credits;

	void          *owner;

	void (*data_ready)(struct rfcomm_dlc *d, struct sk_buff *skb);
	void (*state_change)(struct rfcomm_dlc *d, int err);
	void (*modem_status)(struct rfcomm_dlc *d, u8 v24_sig);
};

/* DLC and session flags */
#define RFCOMM_RX_THROTTLED 0
#define RFCOMM_TX_THROTTLED 1
#define RFCOMM_TIMED_OUT    2
#define RFCOMM_MSC_PENDING  3 
#define RFCOMM_AUTH_PENDING 4
#define RFCOMM_AUTH_ACCEPT  5
#define RFCOMM_AUTH_REJECT  6

/* Scheduling flags and events */
#define RFCOMM_SCHED_STATE  0
#define RFCOMM_SCHED_RX     1
#define RFCOMM_SCHED_TX     2
#define RFCOMM_SCHED_TIMEO  3
#define RFCOMM_SCHED_AUTH   4
#define RFCOMM_SCHED_WAKEUP 31

/* MSC exchange flags */
#define RFCOMM_MSCEX_TX     1
#define RFCOMM_MSCEX_RX     2
#define RFCOMM_MSCEX_OK     (RFCOMM_MSCEX_TX + RFCOMM_MSCEX_RX)

/* CFC states */
#define RFCOMM_CFC_UNKNOWN  -1
#define RFCOMM_CFC_DISABLED 0
#define RFCOMM_CFC_ENABLED  RFCOMM_MAX_CREDITS

/* ---- RFCOMM SEND RPN ---- */
int rfcomm_send_rpn(struct rfcomm_session *s, int cr, u8 dlci,
			u8 bit_rate, u8 data_bits, u8 stop_bits,
			u8 parity, u8 flow_ctrl_settings, 
			u8 xon_char, u8 xoff_char, u16 param_mask);

/* ---- RFCOMM DLCs (channels) ---- */
struct rfcomm_dlc *rfcomm_dlc_alloc(gfp_t prio);
void rfcomm_dlc_free(struct rfcomm_dlc *d);
int  rfcomm_dlc_open(struct rfcomm_dlc *d, bdaddr_t *src, bdaddr_t *dst, u8 channel);
int  rfcomm_dlc_close(struct rfcomm_dlc *d, int reason);
int  rfcomm_dlc_send(struct rfcomm_dlc *d, struct sk_buff *skb);
int  rfcomm_dlc_set_modem_status(struct rfcomm_dlc *d, u8 v24_sig);
int  rfcomm_dlc_get_modem_status(struct rfcomm_dlc *d, u8 *v24_sig);

#define rfcomm_dlc_lock(d)     spin_lock(&d->lock)
#define rfcomm_dlc_unlock(d)   spin_unlock(&d->lock)

static inline void rfcomm_dlc_hold(struct rfcomm_dlc *d)
{
	atomic_inc(&d->refcnt);
}

static inline void rfcomm_dlc_put(struct rfcomm_dlc *d)
{
	if (atomic_dec_and_test(&d->refcnt))
		rfcomm_dlc_free(d);
}

extern void FASTCALL(__rfcomm_dlc_throttle(struct rfcomm_dlc *d));
extern void FASTCALL(__rfcomm_dlc_unthrottle(struct rfcomm_dlc *d));

static inline void rfcomm_dlc_throttle(struct rfcomm_dlc *d)
{
	if (!test_and_set_bit(RFCOMM_RX_THROTTLED, &d->flags))
		__rfcomm_dlc_throttle(d);
}

static inline void rfcomm_dlc_unthrottle(struct rfcomm_dlc *d)
{
	if (test_and_clear_bit(RFCOMM_RX_THROTTLED, &d->flags))
		__rfcomm_dlc_unthrottle(d);
}

/* ---- RFCOMM sessions ---- */
void   rfcomm_session_getaddr(struct rfcomm_session *s, bdaddr_t *src, bdaddr_t *dst);

static inline void rfcomm_session_hold(struct rfcomm_session *s)
{
	atomic_inc(&s->refcnt);
}

/* ---- RFCOMM sockets ---- */
struct sockaddr_rc {
	sa_family_t	rc_family;
	bdaddr_t	rc_bdaddr;
	u8		rc_channel;
};

#define RFCOMM_CONNINFO	0x02
struct rfcomm_conninfo {
	__u16 hci_handle;
	__u8  dev_class[3];
};

#define RFCOMM_LM	0x03
#define RFCOMM_LM_MASTER	0x0001
#define RFCOMM_LM_AUTH		0x0002
#define RFCOMM_LM_ENCRYPT	0x0004
#define RFCOMM_LM_TRUSTED	0x0008
#define RFCOMM_LM_RELIABLE	0x0010
#define RFCOMM_LM_SECURE	0x0020

#define rfcomm_pi(sk) ((struct rfcomm_pinfo *) sk)

struct rfcomm_pinfo {
	struct bt_sock bt;
	struct rfcomm_dlc   *dlc;
	u8     channel;
	u32    link_mode;
};

int  rfcomm_init_sockets(void);
void rfcomm_cleanup_sockets(void);

int  rfcomm_connect_ind(struct rfcomm_session *s, u8 channel, struct rfcomm_dlc **d);

/* ---- RFCOMM TTY ---- */
#define RFCOMM_MAX_DEV  256

#define RFCOMMCREATEDEV		_IOW('R', 200, int)
#define RFCOMMRELEASEDEV	_IOW('R', 201, int)
#define RFCOMMGETDEVLIST	_IOR('R', 210, int)
#define RFCOMMGETDEVINFO	_IOR('R', 211, int)
#define RFCOMMSTEALDLC		_IOW('R', 220, int)

#define RFCOMM_REUSE_DLC      0
#define RFCOMM_RELEASE_ONHUP  1
#define RFCOMM_HANGUP_NOW     2
#define RFCOMM_TTY_ATTACHED   3

struct rfcomm_dev_req {
	s16      dev_id;
	u32      flags;
	bdaddr_t src;
	bdaddr_t dst;
	u8       channel;
	
};

struct rfcomm_dev_info {
	s16      id;
	u32      flags;
	u16      state;
	bdaddr_t src;
	bdaddr_t dst;
	u8       channel;
};

struct rfcomm_dev_list_req {
	u16      dev_num;
	struct   rfcomm_dev_info dev_info[0];
};

int  rfcomm_dev_ioctl(struct sock *sk, unsigned int cmd, void __user *arg);
int  rfcomm_init_ttys(void);
void rfcomm_cleanup_ttys(void);

extern struct proc_dir_entry *proc_bt_rfcomm;

#endif /* __RFCOMM_H */
