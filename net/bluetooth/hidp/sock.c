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

#include <linux/export.h>
#include <linux/file.h>

#include "hidp.h"

static struct bt_sock_list hidp_sk_list = {
	.lock = __RW_LOCK_UNLOCKED(hidp_sk_list.lock)
};

static int hidp_sock_release(struct socket *sock)
{
	struct sock *sk = sock->sk;

	BT_DBG("sock %p sk %p", sock, sk);

	if (!sk)
		return 0;

	bt_sock_unlink(&hidp_sk_list, sk);

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
			return -EPERM;

		if (copy_from_user(&ca, argp, sizeof(ca)))
			return -EFAULT;

		csock = sockfd_lookup(ca.ctrl_sock, &err);
		if (!csock)
			return err;

		isock = sockfd_lookup(ca.intr_sock, &err);
		if (!isock) {
			sockfd_put(csock);
			return err;
		}

		err = hidp_connection_add(&ca, csock, isock);
		if (!err && copy_to_user(argp, &ca, sizeof(ca)))
			err = -EFAULT;

		sockfd_put(csock);
		sockfd_put(isock);

		return err;

	case HIDPCONNDEL:
		if (!capable(CAP_NET_ADMIN))
			return -EPERM;

		if (copy_from_user(&cd, argp, sizeof(cd)))
			return -EFAULT;

		return hidp_connection_del(&cd);

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

#ifdef CONFIG_COMPAT
struct compat_hidp_connadd_req {
	int   ctrl_sock;	/* Connected control socket */
	int   intr_sock;	/* Connected interrupt socket */
	__u16 parser;
	__u16 rd_size;
	compat_uptr_t rd_data;
	__u8  country;
	__u8  subclass;
	__u16 vendor;
	__u16 product;
	__u16 version;
	__u32 flags;
	__u32 idle_to;
	char  name[128];
};

static int hidp_sock_compat_ioctl(struct socket *sock, unsigned int cmd, unsigned long arg)
{
	if (cmd == HIDPGETCONNLIST) {
		struct hidp_connlist_req cl;
		u32 uci;
		int err;

		if (get_user(cl.cnum, (u32 __user *) arg) ||
				get_user(uci, (u32 __user *) (arg + 4)))
			return -EFAULT;

		cl.ci = compat_ptr(uci);

		if (cl.cnum <= 0)
			return -EINVAL;

		err = hidp_get_connlist(&cl);

		if (!err && put_user(cl.cnum, (u32 __user *) arg))
			err = -EFAULT;

		return err;
	} else if (cmd == HIDPCONNADD) {
		struct compat_hidp_connadd_req ca;
		struct hidp_connadd_req __user *uca;

		uca = compat_alloc_user_space(sizeof(*uca));

		if (copy_from_user(&ca, (void __user *) arg, sizeof(ca)))
			return -EFAULT;

		if (put_user(ca.ctrl_sock, &uca->ctrl_sock) ||
				put_user(ca.intr_sock, &uca->intr_sock) ||
				put_user(ca.parser, &uca->parser) ||
				put_user(ca.rd_size, &uca->rd_size) ||
				put_user(compat_ptr(ca.rd_data), &uca->rd_data) ||
				put_user(ca.country, &uca->country) ||
				put_user(ca.subclass, &uca->subclass) ||
				put_user(ca.vendor, &uca->vendor) ||
				put_user(ca.product, &uca->product) ||
				put_user(ca.version, &uca->version) ||
				put_user(ca.flags, &uca->flags) ||
				put_user(ca.idle_to, &uca->idle_to) ||
				copy_to_user(&uca->name[0], &ca.name[0], 128))
			return -EFAULT;

		arg = (unsigned long) uca;

		/* Fall through. We don't actually write back any _changes_
		   to the structure anyway, so there's no need to copy back
		   into the original compat version */
	}

	return hidp_sock_ioctl(sock, cmd, arg);
}
#endif

static const struct proto_ops hidp_sock_ops = {
	.family		= PF_BLUETOOTH,
	.owner		= THIS_MODULE,
	.release	= hidp_sock_release,
	.ioctl		= hidp_sock_ioctl,
#ifdef CONFIG_COMPAT
	.compat_ioctl	= hidp_sock_compat_ioctl,
#endif
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

static int hidp_sock_create(struct net *net, struct socket *sock, int protocol,
			    int kern)
{
	struct sock *sk;

	BT_DBG("sock %p", sock);

	if (sock->type != SOCK_RAW)
		return -ESOCKTNOSUPPORT;

	sk = sk_alloc(net, PF_BLUETOOTH, GFP_ATOMIC, &hidp_proto);
	if (!sk)
		return -ENOMEM;

	sock_init_data(sock, sk);

	sock->ops = &hidp_sock_ops;

	sock->state = SS_UNCONNECTED;

	sock_reset_flag(sk, SOCK_ZAPPED);

	sk->sk_protocol = protocol;
	sk->sk_state	= BT_OPEN;

	bt_sock_link(&hidp_sk_list, sk);

	return 0;
}

static const struct net_proto_family hidp_sock_family_ops = {
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
	if (err < 0) {
		BT_ERR("Can't register HIDP socket");
		goto error;
	}

	err = bt_procfs_init(&init_net, "hidp", &hidp_sk_list, NULL);
	if (err < 0) {
		BT_ERR("Failed to create HIDP proc file");
		bt_sock_unregister(BTPROTO_HIDP);
		goto error;
	}

	BT_INFO("HIDP socket layer initialized");

	return 0;

error:
	proto_unregister(&hidp_proto);
	return err;
}

void __exit hidp_cleanup_sockets(void)
{
	bt_procfs_cleanup(&init_net, "hidp");
	bt_sock_unregister(BTPROTO_HIDP);
	proto_unregister(&hidp_proto);
}
