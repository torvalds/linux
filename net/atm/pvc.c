// SPDX-License-Identifier: GPL-2.0
/* net/atm/pvc.c - ATM PVC sockets */

/* Written 1995-2000 by Werner Almesberger, EPFL LRC/ICA */


#include <linux/net.h>		/* struct socket, struct proto_ops */
#include <linux/atm.h>		/* ATM stuff */
#include <linux/atmdev.h>	/* ATM devices */
#include <linux/errno.h>	/* error codes */
#include <linux/kernel.h>	/* printk */
#include <linux/init.h>
#include <linux/skbuff.h>
#include <linux/bitops.h>
#include <linux/export.h>
#include <net/sock.h>		/* for sock_no_* */

#include "resources.h"		/* devs and vccs */
#include "common.h"		/* common for PVCs and SVCs */


static int pvc_shutdown(struct socket *sock, int how)
{
	return 0;
}

static int pvc_bind(struct socket *sock, struct sockaddr *sockaddr,
		    int sockaddr_len)
{
	struct sock *sk = sock->sk;
	struct sockaddr_atmpvc *addr;
	struct atm_vcc *vcc;
	int error;

	if (sockaddr_len != sizeof(struct sockaddr_atmpvc))
		return -EINVAL;
	addr = (struct sockaddr_atmpvc *)sockaddr;
	if (addr->sap_family != AF_ATMPVC)
		return -EAFNOSUPPORT;
	lock_sock(sk);
	vcc = ATM_SD(sock);
	if (!test_bit(ATM_VF_HASQOS, &vcc->flags)) {
		error = -EBADFD;
		goto out;
	}
	if (test_bit(ATM_VF_PARTIAL, &vcc->flags)) {
		if (vcc->vpi != ATM_VPI_UNSPEC)
			addr->sap_addr.vpi = vcc->vpi;
		if (vcc->vci != ATM_VCI_UNSPEC)
			addr->sap_addr.vci = vcc->vci;
	}
	error = vcc_connect(sock, addr->sap_addr.itf, addr->sap_addr.vpi,
			    addr->sap_addr.vci);
out:
	release_sock(sk);
	return error;
}

static int pvc_connect(struct socket *sock, struct sockaddr *sockaddr,
		       int sockaddr_len, int flags)
{
	return pvc_bind(sock, sockaddr, sockaddr_len);
}

static int pvc_setsockopt(struct socket *sock, int level, int optname,
			  char __user *optval, unsigned int optlen)
{
	struct sock *sk = sock->sk;
	int error;

	lock_sock(sk);
	error = vcc_setsockopt(sock, level, optname, optval, optlen);
	release_sock(sk);
	return error;
}

static int pvc_getsockopt(struct socket *sock, int level, int optname,
			  char __user *optval, int __user *optlen)
{
	struct sock *sk = sock->sk;
	int error;

	lock_sock(sk);
	error = vcc_getsockopt(sock, level, optname, optval, optlen);
	release_sock(sk);
	return error;
}

static int pvc_getname(struct socket *sock, struct sockaddr *sockaddr,
		       int *sockaddr_len, int peer)
{
	struct sockaddr_atmpvc *addr;
	struct atm_vcc *vcc = ATM_SD(sock);

	if (!vcc->dev || !test_bit(ATM_VF_ADDR, &vcc->flags))
		return -ENOTCONN;
	*sockaddr_len = sizeof(struct sockaddr_atmpvc);
	addr = (struct sockaddr_atmpvc *)sockaddr;
	memset(addr, 0, sizeof(*addr));
	addr->sap_family = AF_ATMPVC;
	addr->sap_addr.itf = vcc->dev->number;
	addr->sap_addr.vpi = vcc->vpi;
	addr->sap_addr.vci = vcc->vci;
	return 0;
}

static const struct proto_ops pvc_proto_ops = {
	.family =	PF_ATMPVC,
	.owner =	THIS_MODULE,

	.release =	vcc_release,
	.bind =		pvc_bind,
	.connect =	pvc_connect,
	.socketpair =	sock_no_socketpair,
	.accept =	sock_no_accept,
	.getname =	pvc_getname,
	.poll =		vcc_poll,
	.ioctl =	vcc_ioctl,
#ifdef CONFIG_COMPAT
	.compat_ioctl = vcc_compat_ioctl,
#endif
	.listen =	sock_no_listen,
	.shutdown =	pvc_shutdown,
	.setsockopt =	pvc_setsockopt,
	.getsockopt =	pvc_getsockopt,
	.sendmsg =	vcc_sendmsg,
	.recvmsg =	vcc_recvmsg,
	.mmap =		sock_no_mmap,
	.sendpage =	sock_no_sendpage,
};


static int pvc_create(struct net *net, struct socket *sock, int protocol,
		      int kern)
{
	if (net != &init_net)
		return -EAFNOSUPPORT;

	sock->ops = &pvc_proto_ops;
	return vcc_create(net, sock, protocol, PF_ATMPVC, kern);
}

static const struct net_proto_family pvc_family_ops = {
	.family = PF_ATMPVC,
	.create = pvc_create,
	.owner = THIS_MODULE,
};


/*
 *	Initialize the ATM PVC protocol family
 */


int __init atmpvc_init(void)
{
	return sock_register(&pvc_family_ops);
}

void atmpvc_exit(void)
{
	sock_unregister(PF_ATMPVC);
}
