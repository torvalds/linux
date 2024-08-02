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

/* Bluetooth address family and sockets. */

#include <linux/module.h>
#include <linux/debugfs.h>
#include <linux/stringify.h>
#include <linux/sched/signal.h>

#include <asm/ioctls.h>

#include <net/bluetooth/bluetooth.h>
#include <linux/proc_fs.h>

#include "leds.h"
#include "selftest.h"

/* Bluetooth sockets */
#define BT_MAX_PROTO	(BTPROTO_LAST + 1)
static const struct net_proto_family *bt_proto[BT_MAX_PROTO];
static DEFINE_RWLOCK(bt_proto_lock);

static struct lock_class_key bt_lock_key[BT_MAX_PROTO];
static const char *const bt_key_strings[BT_MAX_PROTO] = {
	"sk_lock-AF_BLUETOOTH-BTPROTO_L2CAP",
	"sk_lock-AF_BLUETOOTH-BTPROTO_HCI",
	"sk_lock-AF_BLUETOOTH-BTPROTO_SCO",
	"sk_lock-AF_BLUETOOTH-BTPROTO_RFCOMM",
	"sk_lock-AF_BLUETOOTH-BTPROTO_BNEP",
	"sk_lock-AF_BLUETOOTH-BTPROTO_CMTP",
	"sk_lock-AF_BLUETOOTH-BTPROTO_HIDP",
	"sk_lock-AF_BLUETOOTH-BTPROTO_AVDTP",
	"sk_lock-AF_BLUETOOTH-BTPROTO_ISO",
};

static struct lock_class_key bt_slock_key[BT_MAX_PROTO];
static const char *const bt_slock_key_strings[BT_MAX_PROTO] = {
	"slock-AF_BLUETOOTH-BTPROTO_L2CAP",
	"slock-AF_BLUETOOTH-BTPROTO_HCI",
	"slock-AF_BLUETOOTH-BTPROTO_SCO",
	"slock-AF_BLUETOOTH-BTPROTO_RFCOMM",
	"slock-AF_BLUETOOTH-BTPROTO_BNEP",
	"slock-AF_BLUETOOTH-BTPROTO_CMTP",
	"slock-AF_BLUETOOTH-BTPROTO_HIDP",
	"slock-AF_BLUETOOTH-BTPROTO_AVDTP",
	"slock-AF_BLUETOOTH-BTPROTO_ISO",
};

void bt_sock_reclassify_lock(struct sock *sk, int proto)
{
	BUG_ON(!sk);
	BUG_ON(!sock_allow_reclassification(sk));

	sock_lock_init_class_and_name(sk,
				      bt_slock_key_strings[proto], &bt_slock_key[proto],
				      bt_key_strings[proto], &bt_lock_key[proto]);
}
EXPORT_SYMBOL(bt_sock_reclassify_lock);

int bt_sock_register(int proto, const struct net_proto_family *ops)
{
	int err = 0;

	if (proto < 0 || proto >= BT_MAX_PROTO)
		return -EINVAL;

	write_lock(&bt_proto_lock);

	if (bt_proto[proto])
		err = -EEXIST;
	else
		bt_proto[proto] = ops;

	write_unlock(&bt_proto_lock);

	return err;
}
EXPORT_SYMBOL(bt_sock_register);

void bt_sock_unregister(int proto)
{
	if (proto < 0 || proto >= BT_MAX_PROTO)
		return;

	write_lock(&bt_proto_lock);
	bt_proto[proto] = NULL;
	write_unlock(&bt_proto_lock);
}
EXPORT_SYMBOL(bt_sock_unregister);

static int bt_sock_create(struct net *net, struct socket *sock, int proto,
			  int kern)
{
	int err;

	if (net != &init_net)
		return -EAFNOSUPPORT;

	if (proto < 0 || proto >= BT_MAX_PROTO)
		return -EINVAL;

	if (!bt_proto[proto])
		request_module("bt-proto-%d", proto);

	err = -EPROTONOSUPPORT;

	read_lock(&bt_proto_lock);

	if (bt_proto[proto] && try_module_get(bt_proto[proto]->owner)) {
		err = bt_proto[proto]->create(net, sock, proto, kern);
		if (!err)
			bt_sock_reclassify_lock(sock->sk, proto);
		module_put(bt_proto[proto]->owner);
	}

	read_unlock(&bt_proto_lock);

	return err;
}

void bt_sock_link(struct bt_sock_list *l, struct sock *sk)
{
	write_lock(&l->lock);
	sk_add_node(sk, &l->head);
	write_unlock(&l->lock);
}
EXPORT_SYMBOL(bt_sock_link);

void bt_sock_unlink(struct bt_sock_list *l, struct sock *sk)
{
	write_lock(&l->lock);
	sk_del_node_init(sk);
	write_unlock(&l->lock);
}
EXPORT_SYMBOL(bt_sock_unlink);

void bt_accept_enqueue(struct sock *parent, struct sock *sk, bool bh)
{
	BT_DBG("parent %p, sk %p", parent, sk);

	sock_hold(sk);

	if (bh)
		bh_lock_sock_nested(sk);
	else
		lock_sock_nested(sk, SINGLE_DEPTH_NESTING);

	list_add_tail(&bt_sk(sk)->accept_q, &bt_sk(parent)->accept_q);
	bt_sk(sk)->parent = parent;

	if (bh)
		bh_unlock_sock(sk);
	else
		release_sock(sk);

	sk_acceptq_added(parent);
}
EXPORT_SYMBOL(bt_accept_enqueue);

/* Calling function must hold the sk lock.
 * bt_sk(sk)->parent must be non-NULL meaning sk is in the parent list.
 */
void bt_accept_unlink(struct sock *sk)
{
	BT_DBG("sk %p state %d", sk, sk->sk_state);

	list_del_init(&bt_sk(sk)->accept_q);
	sk_acceptq_removed(bt_sk(sk)->parent);
	bt_sk(sk)->parent = NULL;
	sock_put(sk);
}
EXPORT_SYMBOL(bt_accept_unlink);

struct sock *bt_accept_dequeue(struct sock *parent, struct socket *newsock)
{
	struct bt_sock *s, *n;
	struct sock *sk;

	BT_DBG("parent %p", parent);

restart:
	list_for_each_entry_safe(s, n, &bt_sk(parent)->accept_q, accept_q) {
		sk = (struct sock *)s;

		/* Prevent early freeing of sk due to unlink and sock_kill */
		sock_hold(sk);
		lock_sock(sk);

		/* Check sk has not already been unlinked via
		 * bt_accept_unlink() due to serialisation caused by sk locking
		 */
		if (!bt_sk(sk)->parent) {
			BT_DBG("sk %p, already unlinked", sk);
			release_sock(sk);
			sock_put(sk);

			/* Restart the loop as sk is no longer in the list
			 * and also avoid a potential infinite loop because
			 * list_for_each_entry_safe() is not thread safe.
			 */
			goto restart;
		}

		/* sk is safely in the parent list so reduce reference count */
		sock_put(sk);

		/* FIXME: Is this check still needed */
		if (sk->sk_state == BT_CLOSED) {
			bt_accept_unlink(sk);
			release_sock(sk);
			continue;
		}

		if (sk->sk_state == BT_CONNECTED || !newsock ||
		    test_bit(BT_SK_DEFER_SETUP, &bt_sk(parent)->flags)) {
			bt_accept_unlink(sk);
			if (newsock)
				sock_graft(sk, newsock);

			release_sock(sk);
			return sk;
		}

		release_sock(sk);
	}

	return NULL;
}
EXPORT_SYMBOL(bt_accept_dequeue);

int bt_sock_recvmsg(struct socket *sock, struct msghdr *msg, size_t len,
		    int flags)
{
	struct sock *sk = sock->sk;
	struct sk_buff *skb;
	size_t copied;
	size_t skblen;
	int err;

	BT_DBG("sock %p sk %p len %zu", sock, sk, len);

	if (flags & MSG_OOB)
		return -EOPNOTSUPP;

	lock_sock(sk);

	skb = skb_recv_datagram(sk, flags, &err);
	if (!skb) {
		if (sk->sk_shutdown & RCV_SHUTDOWN)
			err = 0;

		release_sock(sk);
		return err;
	}

	skblen = skb->len;
	copied = skb->len;
	if (len < copied) {
		msg->msg_flags |= MSG_TRUNC;
		copied = len;
	}

	skb_reset_transport_header(skb);
	err = skb_copy_datagram_msg(skb, 0, msg, copied);
	if (err == 0) {
		sock_recv_cmsgs(msg, sk, skb);

		if (msg->msg_name && bt_sk(sk)->skb_msg_name)
			bt_sk(sk)->skb_msg_name(skb, msg->msg_name,
						&msg->msg_namelen);

		if (bt_sk(sk)->skb_put_cmsg)
			bt_sk(sk)->skb_put_cmsg(skb, msg, sk);
	}

	skb_free_datagram(sk, skb);

	release_sock(sk);

	if (flags & MSG_TRUNC)
		copied = skblen;

	return err ? : copied;
}
EXPORT_SYMBOL(bt_sock_recvmsg);

static long bt_sock_data_wait(struct sock *sk, long timeo)
{
	DECLARE_WAITQUEUE(wait, current);

	add_wait_queue(sk_sleep(sk), &wait);
	for (;;) {
		set_current_state(TASK_INTERRUPTIBLE);

		if (!skb_queue_empty(&sk->sk_receive_queue))
			break;

		if (sk->sk_err || (sk->sk_shutdown & RCV_SHUTDOWN))
			break;

		if (signal_pending(current) || !timeo)
			break;

		sk_set_bit(SOCKWQ_ASYNC_WAITDATA, sk);
		release_sock(sk);
		timeo = schedule_timeout(timeo);
		lock_sock(sk);
		sk_clear_bit(SOCKWQ_ASYNC_WAITDATA, sk);
	}

	__set_current_state(TASK_RUNNING);
	remove_wait_queue(sk_sleep(sk), &wait);
	return timeo;
}

int bt_sock_stream_recvmsg(struct socket *sock, struct msghdr *msg,
			   size_t size, int flags)
{
	struct sock *sk = sock->sk;
	int err = 0;
	size_t target, copied = 0;
	long timeo;

	if (flags & MSG_OOB)
		return -EOPNOTSUPP;

	BT_DBG("sk %p size %zu", sk, size);

	lock_sock(sk);

	target = sock_rcvlowat(sk, flags & MSG_WAITALL, size);
	timeo  = sock_rcvtimeo(sk, flags & MSG_DONTWAIT);

	do {
		struct sk_buff *skb;
		int chunk;

		skb = skb_dequeue(&sk->sk_receive_queue);
		if (!skb) {
			if (copied >= target)
				break;

			err = sock_error(sk);
			if (err)
				break;
			if (sk->sk_shutdown & RCV_SHUTDOWN)
				break;

			err = -EAGAIN;
			if (!timeo)
				break;

			timeo = bt_sock_data_wait(sk, timeo);

			if (signal_pending(current)) {
				err = sock_intr_errno(timeo);
				goto out;
			}
			continue;
		}

		chunk = min_t(unsigned int, skb->len, size);
		if (skb_copy_datagram_msg(skb, 0, msg, chunk)) {
			skb_queue_head(&sk->sk_receive_queue, skb);
			if (!copied)
				copied = -EFAULT;
			break;
		}
		copied += chunk;
		size   -= chunk;

		sock_recv_cmsgs(msg, sk, skb);

		if (!(flags & MSG_PEEK)) {
			int skb_len = skb_headlen(skb);

			if (chunk <= skb_len) {
				__skb_pull(skb, chunk);
			} else {
				struct sk_buff *frag;

				__skb_pull(skb, skb_len);
				chunk -= skb_len;

				skb_walk_frags(skb, frag) {
					if (chunk <= frag->len) {
						/* Pulling partial data */
						skb->len -= chunk;
						skb->data_len -= chunk;
						__skb_pull(frag, chunk);
						break;
					} else if (frag->len) {
						/* Pulling all frag data */
						chunk -= frag->len;
						skb->len -= frag->len;
						skb->data_len -= frag->len;
						__skb_pull(frag, frag->len);
					}
				}
			}

			if (skb->len) {
				skb_queue_head(&sk->sk_receive_queue, skb);
				break;
			}
			kfree_skb(skb);

		} else {
			/* put message back and return */
			skb_queue_head(&sk->sk_receive_queue, skb);
			break;
		}
	} while (size);

out:
	release_sock(sk);
	return copied ? : err;
}
EXPORT_SYMBOL(bt_sock_stream_recvmsg);

static inline __poll_t bt_accept_poll(struct sock *parent)
{
	struct bt_sock *s, *n;
	struct sock *sk;

	list_for_each_entry_safe(s, n, &bt_sk(parent)->accept_q, accept_q) {
		sk = (struct sock *)s;
		if (sk->sk_state == BT_CONNECTED ||
		    (test_bit(BT_SK_DEFER_SETUP, &bt_sk(parent)->flags) &&
		     sk->sk_state == BT_CONNECT2))
			return EPOLLIN | EPOLLRDNORM;
	}

	return 0;
}

__poll_t bt_sock_poll(struct file *file, struct socket *sock,
		      poll_table *wait)
{
	struct sock *sk = sock->sk;
	__poll_t mask = 0;

	poll_wait(file, sk_sleep(sk), wait);

	if (sk->sk_state == BT_LISTEN)
		return bt_accept_poll(sk);

	if (sk->sk_err || !skb_queue_empty_lockless(&sk->sk_error_queue))
		mask |= EPOLLERR |
			(sock_flag(sk, SOCK_SELECT_ERR_QUEUE) ? EPOLLPRI : 0);

	if (sk->sk_shutdown & RCV_SHUTDOWN)
		mask |= EPOLLRDHUP | EPOLLIN | EPOLLRDNORM;

	if (sk->sk_shutdown == SHUTDOWN_MASK)
		mask |= EPOLLHUP;

	if (!skb_queue_empty_lockless(&sk->sk_receive_queue))
		mask |= EPOLLIN | EPOLLRDNORM;

	if (sk->sk_state == BT_CLOSED)
		mask |= EPOLLHUP;

	if (sk->sk_state == BT_CONNECT ||
	    sk->sk_state == BT_CONNECT2 ||
	    sk->sk_state == BT_CONFIG)
		return mask;

	if (!test_bit(BT_SK_SUSPEND, &bt_sk(sk)->flags) && sock_writeable(sk))
		mask |= EPOLLOUT | EPOLLWRNORM | EPOLLWRBAND;
	else
		sk_set_bit(SOCKWQ_ASYNC_NOSPACE, sk);

	return mask;
}
EXPORT_SYMBOL(bt_sock_poll);

int bt_sock_ioctl(struct socket *sock, unsigned int cmd, unsigned long arg)
{
	struct sock *sk = sock->sk;
	struct sk_buff *skb;
	long amount;
	int err;

	BT_DBG("sk %p cmd %x arg %lx", sk, cmd, arg);

	switch (cmd) {
	case TIOCOUTQ:
		if (sk->sk_state == BT_LISTEN)
			return -EINVAL;

		amount = sk->sk_sndbuf - sk_wmem_alloc_get(sk);
		if (amount < 0)
			amount = 0;
		err = put_user(amount, (int __user *)arg);
		break;

	case TIOCINQ:
		if (sk->sk_state == BT_LISTEN)
			return -EINVAL;

		lock_sock(sk);
		skb = skb_peek(&sk->sk_receive_queue);
		amount = skb ? skb->len : 0;
		release_sock(sk);
		err = put_user(amount, (int __user *)arg);
		break;

	default:
		err = -ENOIOCTLCMD;
		break;
	}

	return err;
}
EXPORT_SYMBOL(bt_sock_ioctl);

/* This function expects the sk lock to be held when called */
int bt_sock_wait_state(struct sock *sk, int state, unsigned long timeo)
{
	DECLARE_WAITQUEUE(wait, current);
	int err = 0;

	BT_DBG("sk %p", sk);

	add_wait_queue(sk_sleep(sk), &wait);
	set_current_state(TASK_INTERRUPTIBLE);
	while (sk->sk_state != state) {
		if (!timeo) {
			err = -EINPROGRESS;
			break;
		}

		if (signal_pending(current)) {
			err = sock_intr_errno(timeo);
			break;
		}

		release_sock(sk);
		timeo = schedule_timeout(timeo);
		lock_sock(sk);
		set_current_state(TASK_INTERRUPTIBLE);

		err = sock_error(sk);
		if (err)
			break;
	}
	__set_current_state(TASK_RUNNING);
	remove_wait_queue(sk_sleep(sk), &wait);
	return err;
}
EXPORT_SYMBOL(bt_sock_wait_state);

/* This function expects the sk lock to be held when called */
int bt_sock_wait_ready(struct sock *sk, unsigned int msg_flags)
{
	DECLARE_WAITQUEUE(wait, current);
	unsigned long timeo;
	int err = 0;

	BT_DBG("sk %p", sk);

	timeo = sock_sndtimeo(sk, !!(msg_flags & MSG_DONTWAIT));

	add_wait_queue(sk_sleep(sk), &wait);
	set_current_state(TASK_INTERRUPTIBLE);
	while (test_bit(BT_SK_SUSPEND, &bt_sk(sk)->flags)) {
		if (!timeo) {
			err = -EAGAIN;
			break;
		}

		if (signal_pending(current)) {
			err = sock_intr_errno(timeo);
			break;
		}

		release_sock(sk);
		timeo = schedule_timeout(timeo);
		lock_sock(sk);
		set_current_state(TASK_INTERRUPTIBLE);

		err = sock_error(sk);
		if (err)
			break;
	}
	__set_current_state(TASK_RUNNING);
	remove_wait_queue(sk_sleep(sk), &wait);

	return err;
}
EXPORT_SYMBOL(bt_sock_wait_ready);

#ifdef CONFIG_PROC_FS
static void *bt_seq_start(struct seq_file *seq, loff_t *pos)
	__acquires(seq->private->l->lock)
{
	struct bt_sock_list *l = pde_data(file_inode(seq->file));

	read_lock(&l->lock);
	return seq_hlist_start_head(&l->head, *pos);
}

static void *bt_seq_next(struct seq_file *seq, void *v, loff_t *pos)
{
	struct bt_sock_list *l = pde_data(file_inode(seq->file));

	return seq_hlist_next(v, &l->head, pos);
}

static void bt_seq_stop(struct seq_file *seq, void *v)
	__releases(seq->private->l->lock)
{
	struct bt_sock_list *l = pde_data(file_inode(seq->file));

	read_unlock(&l->lock);
}

static int bt_seq_show(struct seq_file *seq, void *v)
{
	struct bt_sock_list *l = pde_data(file_inode(seq->file));

	if (v == SEQ_START_TOKEN) {
		seq_puts(seq, "sk               RefCnt Rmem   Wmem   User   Inode  Parent");

		if (l->custom_seq_show) {
			seq_putc(seq, ' ');
			l->custom_seq_show(seq, v);
		}

		seq_putc(seq, '\n');
	} else {
		struct sock *sk = sk_entry(v);
		struct bt_sock *bt = bt_sk(sk);

		seq_printf(seq,
			   "%pK %-6d %-6u %-6u %-6u %-6lu %-6lu",
			   sk,
			   refcount_read(&sk->sk_refcnt),
			   sk_rmem_alloc_get(sk),
			   sk_wmem_alloc_get(sk),
			   from_kuid(seq_user_ns(seq), sock_i_uid(sk)),
			   sock_i_ino(sk),
			   bt->parent ? sock_i_ino(bt->parent) : 0LU);

		if (l->custom_seq_show) {
			seq_putc(seq, ' ');
			l->custom_seq_show(seq, v);
		}

		seq_putc(seq, '\n');
	}
	return 0;
}

static const struct seq_operations bt_seq_ops = {
	.start = bt_seq_start,
	.next  = bt_seq_next,
	.stop  = bt_seq_stop,
	.show  = bt_seq_show,
};

int bt_procfs_init(struct net *net, const char *name,
		   struct bt_sock_list *sk_list,
		   int (*seq_show)(struct seq_file *, void *))
{
	sk_list->custom_seq_show = seq_show;

	if (!proc_create_seq_data(name, 0, net->proc_net, &bt_seq_ops, sk_list))
		return -ENOMEM;
	return 0;
}

void bt_procfs_cleanup(struct net *net, const char *name)
{
	remove_proc_entry(name, net->proc_net);
}
#else
int bt_procfs_init(struct net *net, const char *name,
		   struct bt_sock_list *sk_list,
		   int (*seq_show)(struct seq_file *, void *))
{
	return 0;
}

void bt_procfs_cleanup(struct net *net, const char *name)
{
}
#endif
EXPORT_SYMBOL(bt_procfs_init);
EXPORT_SYMBOL(bt_procfs_cleanup);

static const struct net_proto_family bt_sock_family_ops = {
	.owner	= THIS_MODULE,
	.family	= PF_BLUETOOTH,
	.create	= bt_sock_create,
};

struct dentry *bt_debugfs;
EXPORT_SYMBOL_GPL(bt_debugfs);

#define VERSION __stringify(BT_SUBSYS_VERSION) "." \
		__stringify(BT_SUBSYS_REVISION)

static int __init bt_init(void)
{
	int err;

	sock_skb_cb_check_size(sizeof(struct bt_skb_cb));

	BT_INFO("Core ver %s", VERSION);

	err = bt_selftest();
	if (err < 0)
		return err;

	bt_debugfs = debugfs_create_dir("bluetooth", NULL);

	bt_leds_init();

	err = bt_sysfs_init();
	if (err < 0)
		goto cleanup_led;

	err = sock_register(&bt_sock_family_ops);
	if (err)
		goto cleanup_sysfs;

	BT_INFO("HCI device and connection manager initialized");

	err = hci_sock_init();
	if (err)
		goto unregister_socket;

	err = l2cap_init();
	if (err)
		goto cleanup_socket;

	err = sco_init();
	if (err)
		goto cleanup_cap;

	err = mgmt_init();
	if (err)
		goto cleanup_sco;

	return 0;

cleanup_sco:
	sco_exit();
cleanup_cap:
	l2cap_exit();
cleanup_socket:
	hci_sock_cleanup();
unregister_socket:
	sock_unregister(PF_BLUETOOTH);
cleanup_sysfs:
	bt_sysfs_cleanup();
cleanup_led:
	bt_leds_cleanup();
	return err;
}

static void __exit bt_exit(void)
{
	mgmt_exit();

	sco_exit();

	l2cap_exit();

	hci_sock_cleanup();

	sock_unregister(PF_BLUETOOTH);

	bt_sysfs_cleanup();

	bt_leds_cleanup();

	debugfs_remove_recursive(bt_debugfs);
}

subsys_initcall(bt_init);
module_exit(bt_exit);

MODULE_AUTHOR("Marcel Holtmann <marcel@holtmann.org>");
MODULE_DESCRIPTION("Bluetooth Core ver " VERSION);
MODULE_VERSION(VERSION);
MODULE_LICENSE("GPL");
MODULE_ALIAS_NETPROTO(PF_BLUETOOTH);
