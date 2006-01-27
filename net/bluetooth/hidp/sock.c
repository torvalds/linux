/* 
   HIDP implementation for Linux Bluetooth stack (BlueZ).
   Copyright (C) 2003-2004 Marcel Holtmann <marcel@holtmann.org>

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

#include <linux/config.h>
#include <linux/module.h>

#include <linux/types.h>
#include <linux/capability.h>
#include <linux/errno.h>
#include <linux/kernel.h>
#include <linux/sched.h>
#include <linux/slab.h>
#include <linux/poll.h>
#include <linux/fcntl.h>
#include <linux/skbuff.h>
#include <linux/socket.h>
#include <linux/ioctl.h>
#include <linux/file.h>
#include <linux/init.h>
#include <net/sock.h>

#include "hidp.h"

#ifndef CONFIG_BT_HIDP_DEBUG
#undef  BT_DBG
#define BT_DBG(D...)
#endif

static int hidp_sock_release(struct socket *sock)
{
	struct sock *sk = sock->sk;

	BT_DBG("sock %p sk %p", sock, sk);

	if (!sk)
		return 0;

	sock_orphan(sk);
	sock_put(sk);

	return 0;
}

static int hidp_sock_ioctl(struct socket *sock, unsigned int cmd, unsigned long arg)
{
	void __user *argp = (void __user *) arg;
	struct hidp_connadd_req ca;
	struct hidp_conndel_req cd;
	struct hidp_connlist_req cl;
	struct hidp_conninfo ci;
	struct socket *csock;
	struct socket *isock;
	int err;

	BT_DBG("cmd %x arg %lx", cmd, arg);

	switch (cmd) {
	case HIDPCONNADD:
		if (!capable(CAP_NET_ADMIN))
			return -EACCES;

		if (copy_from_user(&ca, argp, sizeof(ca)))
			return -EFAULT;

		csock = sockfd_lookup(ca.ctrl_sock, &err);
		if (!csock)
			return err;

		isock = sockfd_lookup(ca.intr_sock, &err);
		if (!isock) {
			fput(csock->file);
			return err;
		}

		if (csock->sk->sk_state != BT_CONNECTED || isock->sk->sk_state != BT_CONNECTED) {
			fput(csock->file);
			fput(isock->file);
			return -EBADFD;
		}

		err = hidp_add_connection(&ca, csock, isock);
		if (!err) {
			if (copy_to_user(argp, &ca, sizeof(ca)))
				err = -EFAULT;
		} else {
			fput(csock->file);
			fput(isock->file);
		}

		return err;

	case HIDPCONNDEL:
		if (!capable(CAP_NET_ADMIN))
			return -EACCES;

		if (copy_from_user(&cd, argp, sizeof(cd)))
			return -EFAULT;

		return hidp_del_connection(&cd);

	case HIDPGETCONNLIST:
		if (copy_from_user(&cl, argp, sizeof(cl)))
			return -EFAULT;

		if (cl.cnum <= 0)
			return -EINVAL;

		err = hidp_get_connlist(&cl);
		if (!err && copy_to_user(argp, &cl, sizeof(cl)))
			return -EFAULT;

		return err;

	case HIDPGETCONNINFO:
		if (copy_from_user(&ci, argp, sizeof(ci)))
			return -EFAULT;

		err = hidp_get_conninfo(&ci);
		if (!err && copy_to_user(argp, &ci, sizeof(ci)))
			return -EFAULT;

		return err;
	}

	return -EINVAL;
}

static const struct proto_ops hidp_sock_ops = {
	.family		= PF_BLUETOOTH,
	.owner		= THIS_MODULE,
	.release	= hidp_sock_release,
	.ioctl		= hidp_sock_ioctl,
	.bind		= sock_no_bind,
	.getname	= sock_no_getname,
	.sendmsg	= sock_no_sendmsg,
	.recvmsg	= sock_no_recvmsg,
	.poll		= sock_no_poll,
	.listen		= sock_no_listen,
	.shutdown	= sock_no_shutdown,
	.setsockopt	= sock_no_setsockopt,
	.getsockopt	= sock_no_getsockopt,
	.connect	= sock_no_connect,
	.socketpair	= sock_no_socketpair,
	.accept		= sock_no_accept,
	.mmap		= sock_no_mmap
};

static struct proto hidp_proto = {
	.name		= "HIDP",
	.owner		= THIS_MODULE,
	.obj_size	= sizeof(struct bt_sock)
};

static int hidp_sock_create(struct socket *sock, int protocol)
{
	struct sock *sk;

	BT_DBG("sock %p", sock);

	if (sock->type != SOCK_RAW)
		return -ESOCKTNOSUPPORT;

	sk = sk_alloc(PF_BLUETOOTH, GFP_KERNEL, &hidp_proto, 1);
	if (!sk)
		return -ENOMEM;

	sock_init_data(sock, sk);

	sock->ops = &hidp_sock_ops;

	sock->state = SS_UNCONNECTED;

	sock_reset_flag(sk, SOCK_ZAPPED);

	sk->sk_protocol = protocol;
	sk->sk_state	= BT_OPEN;

	return 0;
}

static struct net_proto_family hidp_sock_family_ops = {
	.family	= PF_BLUETOOTH,
	.owner	= THIS_MODULE,
	.create	= hidp_sock_create
};

int __init hidp_init_sockets(void)
{
	int err;

	err = proto_register(&hidp_proto, 0);
	if (err < 0)
		return err;

	err = bt_sock_register(BTPROTO_HIDP, &hidp_sock_family_ops);
	if (err < 0)
		goto error;

	return 0;

error:
	BT_ERR("Can't register HIDP socket");
	proto_unregister(&hidp_proto);
	return err;
}

void __exit hidp_cleanup_sockets(void)
{
	if (bt_sock_unregister(BTPROTO_HIDP) < 0)
		BT_ERR("Can't unregister HIDP socket");

	proto_unregister(&hidp_proto);
}
