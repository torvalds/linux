/*
 * File: socket.c
 *
 * Phonet sockets
 *
 * Copyright (C) 2008 Nokia Corporation.
 *
 * Contact: Remi Denis-Courmont <remi.denis-courmont@nokia.com>
 * Original author: Sakari Ailus <sakari.ailus@nokia.com>
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * version 2 as published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA
 * 02110-1301 USA
 */

#include <linux/gfp.h>
#include <linux/kernel.h>
#include <linux/net.h>
#include <linux/poll.h>
#include <net/sock.h>
#include <net/tcp_states.h>

#include <linux/phonet.h>
#include <net/phonet/phonet.h>
#include <net/phonet/pep.h>
#include <net/phonet/pn_dev.h>

static int pn_socket_release(struct socket *sock)
{
	struct sock *sk = sock->sk;

	if (sk) {
		sock->sk = NULL;
		sk->sk_prot->close(sk, 0);
	}
	return 0;
}

#define PN_HASHSIZE	16
#define PN_HASHMASK	(PN_HASHSIZE-1)


static struct  {
	struct hlist_head hlist[PN_HASHSIZE];
	spinlock_t lock;
} pnsocks;

void __init pn_sock_init(void)
{
	unsigned i;

	for (i = 0; i < PN_HASHSIZE; i++)
		INIT_HLIST_HEAD(pnsocks.hlist + i);
	spin_lock_init(&pnsocks.lock);
}

static struct hlist_head *pn_hash_list(u16 obj)
{
	return pnsocks.hlist + (obj & PN_HASHMASK);
}

/*
 * Find address based on socket address, match only certain fields.
 * Also grab sock if it was found. Remember to sock_put it later.
 */
struct sock *pn_find_sock_by_sa(struct net *net, const struct sockaddr_pn *spn)
{
	struct hlist_node *node;
	struct sock *sknode;
	struct sock *rval = NULL;
	u16 obj = pn_sockaddr_get_object(spn);
	u8 res = spn->spn_resource;
	struct hlist_head *hlist = pn_hash_list(obj);

	spin_lock_bh(&pnsocks.lock);

	sk_for_each(sknode, node, hlist) {
		struct pn_sock *pn = pn_sk(sknode);
		BUG_ON(!pn->sobject); /* unbound socket */

		if (!net_eq(sock_net(sknode), net))
			continue;
		if (pn_port(obj)) {
			/* Look up socket by port */
			if (pn_port(pn->sobject) != pn_port(obj))
				continue;
		} else {
			/* If port is zero, look up by resource */
			if (pn->resource != res)
				continue;
		}
		if (pn_addr(pn->sobject) &&
		    pn_addr(pn->sobject) != pn_addr(obj))
			continue;

		rval = sknode;
		sock_hold(sknode);
		break;
	}

	spin_unlock_bh(&pnsocks.lock);

	return rval;
}

/* Deliver a broadcast packet (only in bottom-half) */
void pn_deliver_sock_broadcast(struct net *net, struct sk_buff *skb)
{
	struct hlist_head *hlist = pnsocks.hlist;
	unsigned h;

	spin_lock(&pnsocks.lock);
	for (h = 0; h < PN_HASHSIZE; h++) {
		struct hlist_node *node;
		struct sock *sknode;

		sk_for_each(sknode, node, hlist) {
			struct sk_buff *clone;

			if (!net_eq(sock_net(sknode), net))
				continue;
			if (!sock_flag(sknode, SOCK_BROADCAST))
				continue;

			clone = skb_clone(skb, GFP_ATOMIC);
			if (clone) {
				sock_hold(sknode);
				sk_receive_skb(sknode, clone, 0);
			}
		}
		hlist++;
	}
	spin_unlock(&pnsocks.lock);
}

void pn_sock_hash(struct sock *sk)
{
	struct hlist_head *hlist = pn_hash_list(pn_sk(sk)->sobject);

	spin_lock_bh(&pnsocks.lock);
	sk_add_node(sk, hlist);
	spin_unlock_bh(&pnsocks.lock);
}
EXPORT_SYMBOL(pn_sock_hash);

void pn_sock_unhash(struct sock *sk)
{
	spin_lock_bh(&pnsocks.lock);
	sk_del_node_init(sk);
	spin_unlock_bh(&pnsocks.lock);
	pn_sock_unbind_all_res(sk);
}
EXPORT_SYMBOL(pn_sock_unhash);

static DEFINE_MUTEX(port_mutex);

static int pn_socket_bind(struct socket *sock, struct sockaddr *addr, int len)
{
	struct sock *sk = sock->sk;
	struct pn_sock *pn = pn_sk(sk);
	struct sockaddr_pn *spn = (struct sockaddr_pn *)addr;
	int err;
	u16 handle;
	u8 saddr;

	if (sk->sk_prot->bind)
		return sk->sk_prot->bind(sk, addr, len);

	if (len < sizeof(struct sockaddr_pn))
		return -EINVAL;
	if (spn->spn_family != AF_PHONET)
		return -EAFNOSUPPORT;

	handle = pn_sockaddr_get_object((struct sockaddr_pn *)addr);
	saddr = pn_addr(handle);
	if (saddr && phonet_address_lookup(sock_net(sk), saddr))
		return -EADDRNOTAVAIL;

	lock_sock(sk);
	if (sk->sk_state != TCP_CLOSE || pn_port(pn->sobject)) {
		err = -EINVAL; /* attempt to rebind */
		goto out;
	}
	WARN_ON(sk_hashed(sk));
	mutex_lock(&port_mutex);
	err = sk->sk_prot->get_port(sk, pn_port(handle));
	if (err)
		goto out_port;

	/* get_port() sets the port, bind() sets the address if applicable */
	pn->sobject = pn_object(saddr, pn_port(pn->sobject));
	pn->resource = spn->spn_resource;

	/* Enable RX on the socket */
	sk->sk_prot->hash(sk);
out_port:
	mutex_unlock(&port_mutex);
out:
	release_sock(sk);
	return err;
}

static int pn_socket_autobind(struct socket *sock)
{
	struct sockaddr_pn sa;
	int err;

	memset(&sa, 0, sizeof(sa));
	sa.spn_family = AF_PHONET;
	err = pn_socket_bind(sock, (struct sockaddr *)&sa,
				sizeof(struct sockaddr_pn));
	if (err != -EINVAL)
		return err;
	BUG_ON(!pn_port(pn_sk(sock->sk)->sobject));
	return 0; /* socket was already bound */
}

static int pn_socket_connect(struct socket *sock, struct sockaddr *addr,
		int len, int flags)
{
	struct sock *sk = sock->sk;
	struct pn_sock *pn = pn_sk(sk);
	struct sockaddr_pn *spn = (struct sockaddr_pn *)addr;
	struct task_struct *tsk = current;
	long timeo = sock_rcvtimeo(sk, flags & O_NONBLOCK);
	int err;

	if (pn_socket_autobind(sock))
		return -ENOBUFS;
	if (len < sizeof(struct sockaddr_pn))
		return -EINVAL;
	if (spn->spn_family != AF_PHONET)
		return -EAFNOSUPPORT;

	lock_sock(sk);

	switch (sock->state) {
	case SS_UNCONNECTED:
		if (sk->sk_state != TCP_CLOSE) {
			err = -EISCONN;
			goto out;
		}
		break;
	case SS_CONNECTING:
		err = -EALREADY;
		goto out;
	default:
		err = -EISCONN;
		goto out;
	}

	pn->dobject = pn_sockaddr_get_object(spn);
	pn->resource = pn_sockaddr_get_resource(spn);
	sock->state = SS_CONNECTING;

	err = sk->sk_prot->connect(sk, addr, len);
	if (err) {
		sock->state = SS_UNCONNECTED;
		pn->dobject = 0;
		goto out;
	}

	while (sk->sk_state == TCP_SYN_SENT) {
		DEFINE_WAIT(wait);

		if (!timeo) {
			err = -EINPROGRESS;
			goto out;
		}
		if (signal_pending(tsk)) {
			err = sock_intr_errno(timeo);
			goto out;
		}

		prepare_to_wait_exclusive(sk_sleep(sk), &wait,
						TASK_INTERRUPTIBLE);
		release_sock(sk);
		timeo = schedule_timeout(timeo);
		lock_sock(sk);
		finish_wait(sk_sleep(sk), &wait);
	}

	if ((1 << sk->sk_state) & (TCPF_SYN_RECV|TCPF_ESTABLISHED))
		err = 0;
	else if (sk->sk_state == TCP_CLOSE_WAIT)
		err = -ECONNRESET;
	else
		err = -ECONNREFUSED;
	sock->state = err ? SS_UNCONNECTED : SS_CONNECTED;
out:
	release_sock(sk);
	return err;
}

static int pn_socket_accept(struct socket *sock, struct socket *newsock,
				int flags)
{
	struct sock *sk = sock->sk;
	struct sock *newsk;
	int err;

	if (unlikely(sk->sk_state != TCP_LISTEN))
		return -EINVAL;

	newsk = sk->sk_prot->accept(sk, flags, &err);
	if (!newsk)
		return err;

	lock_sock(newsk);
	sock_graft(newsk, newsock);
	newsock->state = SS_CONNECTED;
	release_sock(newsk);
	return 0;
}

static int pn_socket_getname(struct socket *sock, struct sockaddr *addr,
				int *sockaddr_len, int peer)
{
	struct sock *sk = sock->sk;
	struct pn_sock *pn = pn_sk(sk);

	memset(addr, 0, sizeof(struct sockaddr_pn));
	addr->sa_family = AF_PHONET;
	if (!peer) /* Race with bind() here is userland's problem. */
		pn_sockaddr_set_object((struct sockaddr_pn *)addr,
					pn->sobject);

	*sockaddr_len = sizeof(struct sockaddr_pn);
	return 0;
}

static unsigned int pn_socket_poll(struct file *file, struct socket *sock,
					poll_table *wait)
{
	struct sock *sk = sock->sk;
	struct pep_sock *pn = pep_sk(sk);
	unsigned int mask = 0;

	poll_wait(file, sk_sleep(sk), wait);

	if (sk->sk_state == TCP_CLOSE)
		return POLLERR;
	if (!skb_queue_empty(&sk->sk_receive_queue))
		mask |= POLLIN | POLLRDNORM;
	if (!skb_queue_empty(&pn->ctrlreq_queue))
		mask |= POLLPRI;
	if (!mask && sk->sk_state == TCP_CLOSE_WAIT)
		return POLLHUP;

	if (sk->sk_state == TCP_ESTABLISHED &&
		atomic_read(&sk->sk_wmem_alloc) < sk->sk_sndbuf &&
		atomic_read(&pn->tx_credits))
		mask |= POLLOUT | POLLWRNORM | POLLWRBAND;

	return mask;
}

static int pn_socket_ioctl(struct socket *sock, unsigned int cmd,
				unsigned long arg)
{
	struct sock *sk = sock->sk;
	struct pn_sock *pn = pn_sk(sk);

	if (cmd == SIOCPNGETOBJECT) {
		struct net_device *dev;
		u16 handle;
		u8 saddr;

		if (get_user(handle, (__u16 __user *)arg))
			return -EFAULT;

		lock_sock(sk);
		if (sk->sk_bound_dev_if)
			dev = dev_get_by_index(sock_net(sk),
						sk->sk_bound_dev_if);
		else
			dev = phonet_device_get(sock_net(sk));
		if (dev && (dev->flags & IFF_UP))
			saddr = phonet_address_get(dev, pn_addr(handle));
		else
			saddr = PN_NO_ADDR;
		release_sock(sk);

		if (dev)
			dev_put(dev);
		if (saddr == PN_NO_ADDR)
			return -EHOSTUNREACH;

		handle = pn_object(saddr, pn_port(pn->sobject));
		return put_user(handle, (__u16 __user *)arg);
	}

	return sk->sk_prot->ioctl(sk, cmd, arg);
}

static int pn_socket_listen(struct socket *sock, int backlog)
{
	struct sock *sk = sock->sk;
	int err = 0;

	if (pn_socket_autobind(sock))
		return -ENOBUFS;

	lock_sock(sk);
	if (sock->state != SS_UNCONNECTED) {
		err = -EINVAL;
		goto out;
	}

	if (sk->sk_state != TCP_LISTEN) {
		sk->sk_state = TCP_LISTEN;
		sk->sk_ack_backlog = 0;
	}
	sk->sk_max_ack_backlog = backlog;
out:
	release_sock(sk);
	return err;
}

static int pn_socket_sendmsg(struct kiocb *iocb, struct socket *sock,
				struct msghdr *m, size_t total_len)
{
	struct sock *sk = sock->sk;

	if (pn_socket_autobind(sock))
		return -EAGAIN;

	return sk->sk_prot->sendmsg(iocb, sk, m, total_len);
}

const struct proto_ops phonet_dgram_ops = {
	.family		= AF_PHONET,
	.owner		= THIS_MODULE,
	.release	= pn_socket_release,
	.bind		= pn_socket_bind,
	.connect	= sock_no_connect,
	.socketpair	= sock_no_socketpair,
	.accept		= sock_no_accept,
	.getname	= pn_socket_getname,
	.poll		= datagram_poll,
	.ioctl		= pn_socket_ioctl,
	.listen		= sock_no_listen,
	.shutdown	= sock_no_shutdown,
	.setsockopt	= sock_no_setsockopt,
	.getsockopt	= sock_no_getsockopt,
#ifdef CONFIG_COMPAT
	.compat_setsockopt = sock_no_setsockopt,
	.compat_getsockopt = sock_no_getsockopt,
#endif
	.sendmsg	= pn_socket_sendmsg,
	.recvmsg	= sock_common_recvmsg,
	.mmap		= sock_no_mmap,
	.sendpage	= sock_no_sendpage,
};

const struct proto_ops phonet_stream_ops = {
	.family		= AF_PHONET,
	.owner		= THIS_MODULE,
	.release	= pn_socket_release,
	.bind		= pn_socket_bind,
	.connect	= pn_socket_connect,
	.socketpair	= sock_no_socketpair,
	.accept		= pn_socket_accept,
	.getname	= pn_socket_getname,
	.poll		= pn_socket_poll,
	.ioctl		= pn_socket_ioctl,
	.listen		= pn_socket_listen,
	.shutdown	= sock_no_shutdown,
	.setsockopt	= sock_common_setsockopt,
	.getsockopt	= sock_common_getsockopt,
#ifdef CONFIG_COMPAT
	.compat_setsockopt = compat_sock_common_setsockopt,
	.compat_getsockopt = compat_sock_common_getsockopt,
#endif
	.sendmsg	= pn_socket_sendmsg,
	.recvmsg	= sock_common_recvmsg,
	.mmap		= sock_no_mmap,
	.sendpage	= sock_no_sendpage,
};
EXPORT_SYMBOL(phonet_stream_ops);

/* allocate port for a socket */
int pn_sock_get_port(struct sock *sk, unsigned short sport)
{
	static int port_cur;
	struct net *net = sock_net(sk);
	struct pn_sock *pn = pn_sk(sk);
	struct sockaddr_pn try_sa;
	struct sock *tmpsk;

	memset(&try_sa, 0, sizeof(struct sockaddr_pn));
	try_sa.spn_family = AF_PHONET;
	WARN_ON(!mutex_is_locked(&port_mutex));
	if (!sport) {
		/* search free port */
		int port, pmin, pmax;

		phonet_get_local_port_range(&pmin, &pmax);
		for (port = pmin; port <= pmax; port++) {
			port_cur++;
			if (port_cur < pmin || port_cur > pmax)
				port_cur = pmin;

			pn_sockaddr_set_port(&try_sa, port_cur);
			tmpsk = pn_find_sock_by_sa(net, &try_sa);
			if (tmpsk == NULL) {
				sport = port_cur;
				goto found;
			} else
				sock_put(tmpsk);
		}
	} else {
		/* try to find specific port */
		pn_sockaddr_set_port(&try_sa, sport);
		tmpsk = pn_find_sock_by_sa(net, &try_sa);
		if (tmpsk == NULL)
			/* No sock there! We can use that port... */
			goto found;
		else
			sock_put(tmpsk);
	}
	/* the port must be in use already */
	return -EADDRINUSE;

found:
	pn->sobject = pn_object(pn_addr(pn->sobject), sport);
	return 0;
}
EXPORT_SYMBOL(pn_sock_get_port);

#ifdef CONFIG_PROC_FS
static struct sock *pn_sock_get_idx(struct seq_file *seq, loff_t pos)
{
	struct net *net = seq_file_net(seq);
	struct hlist_head *hlist = pnsocks.hlist;
	struct hlist_node *node;
	struct sock *sknode;
	unsigned h;

	for (h = 0; h < PN_HASHSIZE; h++) {
		sk_for_each(sknode, node, hlist) {
			if (!net_eq(net, sock_net(sknode)))
				continue;
			if (!pos)
				return sknode;
			pos--;
		}
		hlist++;
	}
	return NULL;
}

static struct sock *pn_sock_get_next(struct seq_file *seq, struct sock *sk)
{
	struct net *net = seq_file_net(seq);

	do
		sk = sk_next(sk);
	while (sk && !net_eq(net, sock_net(sk)));

	return sk;
}

static void *pn_sock_seq_start(struct seq_file *seq, loff_t *pos)
	__acquires(pnsocks.lock)
{
	spin_lock_bh(&pnsocks.lock);
	return *pos ? pn_sock_get_idx(seq, *pos - 1) : SEQ_START_TOKEN;
}

static void *pn_sock_seq_next(struct seq_file *seq, void *v, loff_t *pos)
{
	struct sock *sk;

	if (v == SEQ_START_TOKEN)
		sk = pn_sock_get_idx(seq, 0);
	else
		sk = pn_sock_get_next(seq, v);
	(*pos)++;
	return sk;
}

static void pn_sock_seq_stop(struct seq_file *seq, void *v)
	__releases(pnsocks.lock)
{
	spin_unlock_bh(&pnsocks.lock);
}

static int pn_sock_seq_show(struct seq_file *seq, void *v)
{
	int len;

	if (v == SEQ_START_TOKEN)
		seq_printf(seq, "%s%n", "pt  loc  rem rs st tx_queue rx_queue "
			"  uid inode ref pointer drops", &len);
	else {
		struct sock *sk = v;
		struct pn_sock *pn = pn_sk(sk);

		seq_printf(seq, "%2d %04X:%04X:%02X %02X %08X:%08X %5d %lu "
			"%d %p %d%n",
			sk->sk_protocol, pn->sobject, pn->dobject,
			pn->resource, sk->sk_state,
			sk_wmem_alloc_get(sk), sk_rmem_alloc_get(sk),
			sock_i_uid(sk), sock_i_ino(sk),
			atomic_read(&sk->sk_refcnt), sk,
			atomic_read(&sk->sk_drops), &len);
	}
	seq_printf(seq, "%*s\n", 127 - len, "");
	return 0;
}

static const struct seq_operations pn_sock_seq_ops = {
	.start = pn_sock_seq_start,
	.next = pn_sock_seq_next,
	.stop = pn_sock_seq_stop,
	.show = pn_sock_seq_show,
};

static int pn_sock_open(struct inode *inode, struct file *file)
{
	return seq_open_net(inode, file, &pn_sock_seq_ops,
				sizeof(struct seq_net_private));
}

const struct file_operations pn_sock_seq_fops = {
	.owner = THIS_MODULE,
	.open = pn_sock_open,
	.read = seq_read,
	.llseek = seq_lseek,
	.release = seq_release_net,
};
#endif

static struct  {
	struct sock *sk[256];
} pnres;

/*
 * Find and hold socket based on resource.
 */
struct sock *pn_find_sock_by_res(struct net *net, u8 res)
{
	struct sock *sk;

	if (!net_eq(net, &init_net))
		return NULL;

	rcu_read_lock();
	sk = rcu_dereference(pnres.sk[res]);
	if (sk)
		sock_hold(sk);
	rcu_read_unlock();
	return sk;
}

static DEFINE_MUTEX(resource_mutex);

int pn_sock_bind_res(struct sock *sk, u8 res)
{
	int ret = -EADDRINUSE;

	if (!net_eq(sock_net(sk), &init_net))
		return -ENOIOCTLCMD;
	if (!capable(CAP_SYS_ADMIN))
		return -EPERM;
	if (pn_socket_autobind(sk->sk_socket))
		return -EAGAIN;

	mutex_lock(&resource_mutex);
	if (pnres.sk[res] == NULL) {
		sock_hold(sk);
		rcu_assign_pointer(pnres.sk[res], sk);
		ret = 0;
	}
	mutex_unlock(&resource_mutex);
	return ret;
}

int pn_sock_unbind_res(struct sock *sk, u8 res)
{
	int ret = -ENOENT;

	if (!capable(CAP_SYS_ADMIN))
		return -EPERM;

	mutex_lock(&resource_mutex);
	if (pnres.sk[res] == sk) {
		rcu_assign_pointer(pnres.sk[res], NULL);
		ret = 0;
	}
	mutex_unlock(&resource_mutex);

	if (ret == 0) {
		synchronize_rcu();
		sock_put(sk);
	}
	return ret;
}

void pn_sock_unbind_all_res(struct sock *sk)
{
	unsigned res, match = 0;

	mutex_lock(&resource_mutex);
	for (res = 0; res < 256; res++) {
		if (pnres.sk[res] == sk) {
			rcu_assign_pointer(pnres.sk[res], NULL);
			match++;
		}
	}
	mutex_unlock(&resource_mutex);

	if (match == 0)
		return;
	synchronize_rcu();
	while (match > 0) {
		sock_put(sk);
		match--;
	}
}

#ifdef CONFIG_PROC_FS
static struct sock **pn_res_get_idx(struct seq_file *seq, loff_t pos)
{
	struct net *net = seq_file_net(seq);
	unsigned i;

	if (!net_eq(net, &init_net))
		return NULL;

	for (i = 0; i < 256; i++) {
		if (pnres.sk[i] == NULL)
			continue;
		if (!pos)
			return pnres.sk + i;
		pos--;
	}
	return NULL;
}

static struct sock **pn_res_get_next(struct seq_file *seq, struct sock **sk)
{
	struct net *net = seq_file_net(seq);
	unsigned i;

	BUG_ON(!net_eq(net, &init_net));

	for (i = (sk - pnres.sk) + 1; i < 256; i++)
		if (pnres.sk[i])
			return pnres.sk + i;
	return NULL;
}

static void *pn_res_seq_start(struct seq_file *seq, loff_t *pos)
	__acquires(resource_mutex)
{
	mutex_lock(&resource_mutex);
	return *pos ? pn_res_get_idx(seq, *pos - 1) : SEQ_START_TOKEN;
}

static void *pn_res_seq_next(struct seq_file *seq, void *v, loff_t *pos)
{
	struct sock **sk;

	if (v == SEQ_START_TOKEN)
		sk = pn_res_get_idx(seq, 0);
	else
		sk = pn_res_get_next(seq, v);
	(*pos)++;
	return sk;
}

static void pn_res_seq_stop(struct seq_file *seq, void *v)
	__releases(resource_mutex)
{
	mutex_unlock(&resource_mutex);
}

static int pn_res_seq_show(struct seq_file *seq, void *v)
{
	int len;

	if (v == SEQ_START_TOKEN)
		seq_printf(seq, "%s%n", "rs   uid inode", &len);
	else {
		struct sock **psk = v;
		struct sock *sk = *psk;

		seq_printf(seq, "%02X %5d %lu%n",
			   (int) (psk - pnres.sk), sock_i_uid(sk),
			   sock_i_ino(sk), &len);
	}
	seq_printf(seq, "%*s\n", 63 - len, "");
	return 0;
}

static const struct seq_operations pn_res_seq_ops = {
	.start = pn_res_seq_start,
	.next = pn_res_seq_next,
	.stop = pn_res_seq_stop,
	.show = pn_res_seq_show,
};

static int pn_res_open(struct inode *inode, struct file *file)
{
	return seq_open_net(inode, file, &pn_res_seq_ops,
				sizeof(struct seq_net_private));
}

const struct file_operations pn_res_seq_fops = {
	.owner = THIS_MODULE,
	.open = pn_res_open,
	.read = seq_read,
	.llseek = seq_lseek,
	.release = seq_release_net,
};
#endif
