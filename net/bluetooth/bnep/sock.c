/* 
   BNEP implementation for Linux Bluetooth stack (BlueZ).
   Copyright (C) 2001-2002 Inventel Systemes
   Written 2001-2002 by
	David Libault  <david.libault@inventel.fr>

   Copyright (C) 2002 Maxim Krasnyansky <maxk@qualcomm.com>

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

/*
 * $Id: sock.c,v 1.4 2002/08/04 21:23:58 maxk Exp $
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

#include <asm/system.h>
#include <asm/uaccess.h>

#include "bnep.h"

#ifndef CONFIG_BT_BNEP_DEBUG
#undef  BT_DBG
#define BT_DBG( A... )
#endif

static int bnep_sock_release(struct socket *sock)
{
	struct sock *sk = sock->sk;

	BT_DBG("sock %p sk %p", sock, sk);

	if (!sk)
		return 0;

	sock_orphan(sk);
	sock_put(sk);
	return 0;
}

static int bnep_sock_ioctl(struct socket *sock, unsigned int cmd, unsigned long arg)
{
	struct bnep_connlist_req cl;
	struct bnep_connadd_req  ca;
	struct bnep_conndel_req  cd;
	struct bnep_conninfo ci;
	struct socket *nsock;
	void __user *argp = (void __user *)arg;
	int err;

	BT_DBG("cmd %x arg %lx", cmd, arg);

	switch (cmd) {
	case BNEPCONNADD:
		if (!capable(CAP_NET_ADMIN))
			return -EACCES;

		if (copy_from_user(&ca, argp, sizeof(ca)))
			return -EFAULT;
	
		nsock = sockfd_lookup(ca.sock, &err);
		if (!nsock)
			return err;

		if (nsock->sk->sk_state != BT_CONNECTED) {
			fput(nsock->file);
			return -EBADFD;
		}

		err = bnep_add_connection(&ca, nsock);
		if (!err) {
    			if (copy_to_user(argp, &ca, sizeof(ca)))
				err = -EFAULT;
		} else
			fput(nsock->file);

		return err;
	
	case BNEPCONNDEL:
		if (!capable(CAP_NET_ADMIN))
			return -EACCES;

		if (copy_from_user(&cd, argp, sizeof(cd)))
			return -EFAULT;
	
		return bnep_del_connection(&cd);

	case BNEPGETCONNLIST:
		if (copy_from_user(&cl, argp, sizeof(cl)))
			return -EFAULT;

		if (cl.cnum <= 0)
			return -EINVAL;
	
		err = bnep_get_connlist(&cl);
		if (!err && copy_to_user(argp, &cl, sizeof(cl)))
			return -EFAULT;

		return err;

	case BNEPGETCONNINFO:
		if (copy_from_user(&ci, argp, sizeof(ci)))
			return -EFAULT;

		err = bnep_get_conninfo(&ci);
		if (!err && copy_to_user(argp, &ci, sizeof(ci)))
			return -EFAULT;

		return err;

	default:
		return -EINVAL;
	}

	return 0;
}

static const struct proto_ops bnep_sock_ops = {
	.family     = PF_BLUETOOTH,
	.owner      = THIS_MODULE,
	.release    = bnep_sock_release,
	.ioctl      = bnep_sock_ioctl,
	.bind       = sock_no_bind,
	.getname    = sock_no_getname,
	.sendmsg    = sock_no_sendmsg,
	.recvmsg    = sock_no_recvmsg,
	.poll       = sock_no_poll,
	.listen     = sock_no_listen,
	.shutdown   = sock_no_shutdown,
	.setsockopt = sock_no_setsockopt,
	.getsockopt = sock_no_getsockopt,
	.connect    = sock_no_connect,
	.socketpair = sock_no_socketpair,
	.accept     = sock_no_accept,
	.mmap       = sock_no_mmap
};

static struct proto bnep_proto = {
	.name		= "BNEP",
	.owner		= THIS_MODULE,
	.obj_size	= sizeof(struct bt_sock)
};

static int bnep_sock_create(struct socket *sock, int protocol)
{
	struct sock *sk;

	BT_DBG("sock %p", sock);

	if (sock->type != SOCK_RAW)
		return -ESOCKTNOSUPPORT;

	sk = sk_alloc(PF_BLUETOOTH, GFP_KERNEL, &bnep_proto, 1);
	if (!sk)
		return -ENOMEM;

	sock_init_data(sock, sk);

	sock->ops = &bnep_sock_ops;

	sock->state = SS_UNCONNECTED;

	sock_reset_flag(sk, SOCK_ZAPPED);

	sk->sk_protocol = protocol;
	sk->sk_state	= BT_OPEN;

	return 0;
}

static struct net_proto_family bnep_sock_family_ops = {
	.family = PF_BLUETOOTH,
	.owner	= THIS_MODULE,
	.create = bnep_sock_create
};

int __init bnep_sock_init(void)
{
	int err;

	err = proto_register(&bnep_proto, 0);
	if (err < 0)
		return err;

	err = bt_sock_register(BTPROTO_BNEP, &bnep_sock_family_ops);
	if (err < 0)
		goto error;

	return 0;

error:
	BT_ERR("Can't register BNEP socket");
	proto_unregister(&bnep_proto);
	return err;
}

int __exit bnep_sock_cleanup(void)
{
	if (bt_sock_unregister(BTPROTO_BNEP) < 0)
		BT_ERR("Can't unregister BNEP socket");

	proto_unregister(&bnep_proto);

	return 0;
}
