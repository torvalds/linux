/*
   CMTP implementation for Linux Bluetooth stack (BlueZ).
   Copyright (C) 2002-2003 Marcel Holtmann <marcel@holtmann.org>

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

#include <linux/module.h>

#include <linux/types.h>
#include <linux/errno.h>
#include <linux/kernel.h>
#include <linux/sched.h>
#include <linux/slab.h>
#include <linux/poll.h>
#include <linux/fcntl.h>
#include <linux/freezer.h>
#include <linux/skbuff.h>
#include <linux/socket.h>
#include <linux/ioctl.h>
#include <linux/file.h>
#include <linux/init.h>
#include <net/sock.h>

#include <linux/isdn/capilli.h>

#include <net/bluetooth/bluetooth.h>
#include <net/bluetooth/l2cap.h>

#include "cmtp.h"

#define VERSION "1.0"

static DECLARE_RWSEM(cmtp_session_sem);
static LIST_HEAD(cmtp_session_list);

static struct cmtp_session *__cmtp_get_session(bdaddr_t *bdaddr)
{
	struct cmtp_session *session;
	struct list_head *p;

	BT_DBG("");

	list_for_each(p, &cmtp_session_list) {
		session = list_entry(p, struct cmtp_session, list);
		if (!bacmp(bdaddr, &session->bdaddr))
			return session;
	}
	return NULL;
}

static void __cmtp_link_session(struct cmtp_session *session)
{
	__module_get(THIS_MODULE);
	list_add(&session->list, &cmtp_session_list);
}

static void __cmtp_unlink_session(struct cmtp_session *session)
{
	list_del(&session->list);
	module_put(THIS_MODULE);
}

static void __cmtp_copy_session(struct cmtp_session *session, struct cmtp_conninfo *ci)
{
	memset(ci, 0, sizeof(*ci));
	bacpy(&ci->bdaddr, &session->bdaddr);

	ci->flags = session->flags;
	ci->state = session->state;

	ci->num = session->num;
}


static inline int cmtp_alloc_block_id(struct cmtp_session *session)
{
	int i, id = -1;

	for (i = 0; i < 16; i++)
		if (!test_and_set_bit(i, &session->blockids)) {
			id = i;
			break;
		}

	return id;
}

static inline void cmtp_free_block_id(struct cmtp_session *session, int id)
{
	clear_bit(id, &session->blockids);
}

static inline void cmtp_add_msgpart(struct cmtp_session *session, int id, const unsigned char *buf, int count)
{
	struct sk_buff *skb = session->reassembly[id], *nskb;
	int size;

	BT_DBG("session %p buf %p count %d", session, buf, count);

	size = (skb) ? skb->len + count : count;

	nskb = alloc_skb(size, GFP_ATOMIC);
	if (!nskb) {
		BT_ERR("Can't allocate memory for CAPI message");
		return;
	}

	if (skb && (skb->len > 0))
		skb_copy_from_linear_data(skb, skb_put(nskb, skb->len), skb->len);

	memcpy(skb_put(nskb, count), buf, count);

	session->reassembly[id] = nskb;

	kfree_skb(skb);
}

static inline int cmtp_recv_frame(struct cmtp_session *session, struct sk_buff *skb)
{
	__u8 hdr, hdrlen, id;
	__u16 len;

	BT_DBG("session %p skb %p len %d", session, skb, skb->len);

	while (skb->len > 0) {
		hdr = skb->data[0];

		switch (hdr & 0xc0) {
		case 0x40:
			hdrlen = 2;
			len = skb->data[1];
			break;
		case 0x80:
			hdrlen = 3;
			len = skb->data[1] | (skb->data[2] << 8);
			break;
		default:
			hdrlen = 1;
			len = 0;
			break;
		}

		id = (hdr & 0x3c) >> 2;

		BT_DBG("hdr 0x%02x hdrlen %d len %d id %d", hdr, hdrlen, len, id);

		if (hdrlen + len > skb->len) {
			BT_ERR("Wrong size or header information in CMTP frame");
			break;
		}

		if (len == 0) {
			skb_pull(skb, hdrlen);
			continue;
		}

		switch (hdr & 0x03) {
		case 0x00:
			cmtp_add_msgpart(session, id, skb->data + hdrlen, len);
			cmtp_recv_capimsg(session, session->reassembly[id]);
			session->reassembly[id] = NULL;
			break;
		case 0x01:
			cmtp_add_msgpart(session, id, skb->data + hdrlen, len);
			break;
		default:
			if (session->reassembly[id] != NULL)
				kfree_skb(session->reassembly[id]);
			session->reassembly[id] = NULL;
			break;
		}

		skb_pull(skb, hdrlen + len);
	}

	kfree_skb(skb);
	return 0;
}

static int cmtp_send_frame(struct cmtp_session *session, unsigned char *data, int len)
{
	struct socket *sock = session->sock;
	struct kvec iv = { data, len };
	struct msghdr msg;

	BT_DBG("session %p data %p len %d", session, data, len);

	if (!len)
		return 0;

	memset(&msg, 0, sizeof(msg));

	return kernel_sendmsg(sock, &msg, &iv, 1, len);
}

static void cmtp_process_transmit(struct cmtp_session *session)
{
	struct sk_buff *skb, *nskb;
	unsigned char *hdr;
	unsigned int size, tail;

	BT_DBG("session %p", session);

	nskb = alloc_skb(session->mtu, GFP_ATOMIC);
	if (!nskb) {
		BT_ERR("Can't allocate memory for new frame");
		return;
	}

	while ((skb = skb_dequeue(&session->transmit))) {
		struct cmtp_scb *scb = (void *) skb->cb;

		tail = session->mtu - nskb->len;
		if (tail < 5) {
			cmtp_send_frame(session, nskb->data, nskb->len);
			skb_trim(nskb, 0);
			tail = session->mtu;
		}

		size = min_t(uint, ((tail < 258) ? (tail - 2) : (tail - 3)), skb->len);

		if ((scb->id < 0) && ((scb->id = cmtp_alloc_block_id(session)) < 0)) {
			skb_queue_head(&session->transmit, skb);
			break;
		}

		if (size < 256) {
			hdr = skb_put(nskb, 2);
			hdr[0] = 0x40
				| ((scb->id << 2) & 0x3c)
				| ((skb->len == size) ? 0x00 : 0x01);
			hdr[1] = size;
		} else {
			hdr = skb_put(nskb, 3);
			hdr[0] = 0x80
				| ((scb->id << 2) & 0x3c)
				| ((skb->len == size) ? 0x00 : 0x01);
			hdr[1] = size & 0xff;
			hdr[2] = size >> 8;
		}

		skb_copy_from_linear_data(skb, skb_put(nskb, size), size);
		skb_pull(skb, size);

		if (skb->len > 0) {
			skb_queue_head(&session->transmit, skb);
		} else {
			cmtp_free_block_id(session, scb->id);
			if (scb->data) {
				cmtp_send_frame(session, nskb->data, nskb->len);
				skb_trim(nskb, 0);
			}
			kfree_skb(skb);
		}
	}

	cmtp_send_frame(session, nskb->data, nskb->len);

	kfree_skb(nskb);
}

static int cmtp_session(void *arg)
{
	struct cmtp_session *session = arg;
	struct sock *sk = session->sock->sk;
	struct sk_buff *skb;
	wait_queue_t wait;

	BT_DBG("session %p", session);

	daemonize("kcmtpd_ctr_%d", session->num);
	set_user_nice(current, -15);

	init_waitqueue_entry(&wait, current);
	add_wait_queue(sk_sleep(sk), &wait);
	while (!atomic_read(&session->terminate)) {
		set_current_state(TASK_INTERRUPTIBLE);

		if (sk->sk_state != BT_CONNECTED)
			break;

		while ((skb = skb_dequeue(&sk->sk_receive_queue))) {
			skb_orphan(skb);
			cmtp_recv_frame(session, skb);
		}

		cmtp_process_transmit(session);

		schedule();
	}
	set_current_state(TASK_RUNNING);
	remove_wait_queue(sk_sleep(sk), &wait);

	down_write(&cmtp_session_sem);

	if (!(session->flags & (1 << CMTP_LOOPBACK)))
		cmtp_detach_device(session);

	fput(session->sock->file);

	__cmtp_unlink_session(session);

	up_write(&cmtp_session_sem);

	kfree(session);
	return 0;
}

int cmtp_add_connection(struct cmtp_connadd_req *req, struct socket *sock)
{
	struct cmtp_session *session, *s;
	int i, err;

	BT_DBG("");

	session = kzalloc(sizeof(struct cmtp_session), GFP_KERNEL);
	if (!session)
		return -ENOMEM;

	down_write(&cmtp_session_sem);

	s = __cmtp_get_session(&bt_sk(sock->sk)->dst);
	if (s && s->state == BT_CONNECTED) {
		err = -EEXIST;
		goto failed;
	}

	bacpy(&session->bdaddr, &bt_sk(sock->sk)->dst);

	session->mtu = min_t(uint, l2cap_pi(sock->sk)->omtu, l2cap_pi(sock->sk)->imtu);

	BT_DBG("mtu %d", session->mtu);

	sprintf(session->name, "%s", batostr(&bt_sk(sock->sk)->dst));

	session->sock  = sock;
	session->state = BT_CONFIG;

	init_waitqueue_head(&session->wait);

	session->msgnum = CMTP_INITIAL_MSGNUM;

	INIT_LIST_HEAD(&session->applications);

	skb_queue_head_init(&session->transmit);

	for (i = 0; i < 16; i++)
		session->reassembly[i] = NULL;

	session->flags = req->flags;

	__cmtp_link_session(session);

	err = kernel_thread(cmtp_session, session, CLONE_KERNEL);
	if (err < 0)
		goto unlink;

	if (!(session->flags & (1 << CMTP_LOOPBACK))) {
		err = cmtp_attach_device(session);
		if (err < 0)
			goto detach;
	}

	up_write(&cmtp_session_sem);
	return 0;

detach:
	cmtp_detach_device(session);

unlink:
	__cmtp_unlink_session(session);

failed:
	up_write(&cmtp_session_sem);
	kfree(session);
	return err;
}

int cmtp_del_connection(struct cmtp_conndel_req *req)
{
	struct cmtp_session *session;
	int err = 0;

	BT_DBG("");

	down_read(&cmtp_session_sem);

	session = __cmtp_get_session(&req->bdaddr);
	if (session) {
		/* Flush the transmit queue */
		skb_queue_purge(&session->transmit);

		/* Kill session thread */
		atomic_inc(&session->terminate);
		cmtp_schedule(session);
	} else
		err = -ENOENT;

	up_read(&cmtp_session_sem);
	return err;
}

int cmtp_get_connlist(struct cmtp_connlist_req *req)
{
	struct list_head *p;
	int err = 0, n = 0;

	BT_DBG("");

	down_read(&cmtp_session_sem);

	list_for_each(p, &cmtp_session_list) {
		struct cmtp_session *session;
		struct cmtp_conninfo ci;

		session = list_entry(p, struct cmtp_session, list);

		__cmtp_copy_session(session, &ci);

		if (copy_to_user(req->ci, &ci, sizeof(ci))) {
			err = -EFAULT;
			break;
		}

		if (++n >= req->cnum)
			break;

		req->ci++;
	}
	req->cnum = n;

	up_read(&cmtp_session_sem);
	return err;
}

int cmtp_get_conninfo(struct cmtp_conninfo *ci)
{
	struct cmtp_session *session;
	int err = 0;

	down_read(&cmtp_session_sem);

	session = __cmtp_get_session(&ci->bdaddr);
	if (session)
		__cmtp_copy_session(session, ci);
	else
		err = -ENOENT;

	up_read(&cmtp_session_sem);
	return err;
}


static int __init cmtp_init(void)
{
	BT_INFO("CMTP (CAPI Emulation) ver %s", VERSION);

	cmtp_init_sockets();

	return 0;
}

static void __exit cmtp_exit(void)
{
	cmtp_cleanup_sockets();
}

module_init(cmtp_init);
module_exit(cmtp_exit);

MODULE_AUTHOR("Marcel Holtmann <marcel@holtmann.org>");
MODULE_DESCRIPTION("Bluetooth CMTP ver " VERSION);
MODULE_VERSION(VERSION);
MODULE_LICENSE("GPL");
MODULE_ALIAS("bt-proto-5");
