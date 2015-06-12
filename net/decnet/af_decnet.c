
/*
 * DECnet       An implementation of the DECnet protocol suite for the LINUX
 *              operating system.  DECnet is implemented using the  BSD Socket
 *              interface as the means of communication with the user level.
 *
 *              DECnet Socket Layer Interface
 *
 * Authors:     Eduardo Marcelo Serrat <emserrat@geocities.com>
 *              Patrick Caulfield <patrick@pandh.demon.co.uk>
 *
 * Changes:
 *        Steve Whitehouse: Copied from Eduardo Serrat and Patrick Caulfield's
 *                          version of the code. Original copyright preserved
 *                          below.
 *        Steve Whitehouse: Some bug fixes, cleaning up some code to make it
 *                          compatible with my routing layer.
 *        Steve Whitehouse: Merging changes from Eduardo Serrat and Patrick
 *                          Caulfield.
 *        Steve Whitehouse: Further bug fixes, checking module code still works
 *                          with new routing layer.
 *        Steve Whitehouse: Additional set/get_sockopt() calls.
 *        Steve Whitehouse: Fixed TIOCINQ ioctl to be same as Eduardo's new
 *                          code.
 *        Steve Whitehouse: recvmsg() changed to try and behave in a POSIX like
 *                          way. Didn't manage it entirely, but its better.
 *        Steve Whitehouse: ditto for sendmsg().
 *        Steve Whitehouse: A selection of bug fixes to various things.
 *        Steve Whitehouse: Added TIOCOUTQ ioctl.
 *        Steve Whitehouse: Fixes to username2sockaddr & sockaddr2username.
 *        Steve Whitehouse: Fixes to connect() error returns.
 *       Patrick Caulfield: Fixes to delayed acceptance logic.
 *         David S. Miller: New socket locking
 *        Steve Whitehouse: Socket list hashing/locking
 *         Arnaldo C. Melo: use capable, not suser
 *        Steve Whitehouse: Removed unused code. Fix to use sk->allocation
 *                          when required.
 *       Patrick Caulfield: /proc/net/decnet now has object name/number
 *        Steve Whitehouse: Fixed local port allocation, hashed sk list
 *          Matthew Wilcox: Fixes for dn_ioctl()
 *        Steve Whitehouse: New connect/accept logic to allow timeouts and
 *                          prepare for sendpage etc.
 */


/******************************************************************************
    (c) 1995-1998 E.M. Serrat		emserrat@geocities.com

    This program is free software; you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation; either version 2 of the License, or
    any later version.

    This program is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU General Public License for more details.

HISTORY:

Version           Kernel     Date       Author/Comments
-------           ------     ----       ---------------
Version 0.0.1     2.0.30    01-dic-97	Eduardo Marcelo Serrat
					(emserrat@geocities.com)

					First Development of DECnet Socket La-
					yer for Linux. Only supports outgoing
					connections.

Version 0.0.2	  2.1.105   20-jun-98   Patrick J. Caulfield
					(patrick@pandh.demon.co.uk)

					Port to new kernel development version.

Version 0.0.3     2.1.106   25-jun-98   Eduardo Marcelo Serrat
					(emserrat@geocities.com)
					_
					Added support for incoming connections
					so we can start developing server apps
					on Linux.
					-
					Module Support
Version 0.0.4     2.1.109   21-jul-98   Eduardo Marcelo Serrat
				       (emserrat@geocities.com)
				       _
					Added support for X11R6.4. Now we can
					use DECnet transport for X on Linux!!!
				       -
Version 0.0.5    2.1.110   01-aug-98   Eduardo Marcelo Serrat
				       (emserrat@geocities.com)
				       Removed bugs on flow control
				       Removed bugs on incoming accessdata
				       order
				       -
Version 0.0.6    2.1.110   07-aug-98   Eduardo Marcelo Serrat
				       dn_recvmsg fixes

					Patrick J. Caulfield
				       dn_bind fixes
*******************************************************************************/

#include <linux/module.h>
#include <linux/errno.h>
#include <linux/types.h>
#include <linux/slab.h>
#include <linux/socket.h>
#include <linux/in.h>
#include <linux/kernel.h>
#include <linux/sched.h>
#include <linux/timer.h>
#include <linux/string.h>
#include <linux/sockios.h>
#include <linux/net.h>
#include <linux/netdevice.h>
#include <linux/inet.h>
#include <linux/route.h>
#include <linux/netfilter.h>
#include <linux/seq_file.h>
#include <net/sock.h>
#include <net/tcp_states.h>
#include <net/flow.h>
#include <asm/ioctls.h>
#include <linux/capability.h>
#include <linux/mm.h>
#include <linux/interrupt.h>
#include <linux/proc_fs.h>
#include <linux/stat.h>
#include <linux/init.h>
#include <linux/poll.h>
#include <linux/jiffies.h>
#include <net/net_namespace.h>
#include <net/neighbour.h>
#include <net/dst.h>
#include <net/fib_rules.h>
#include <net/dn.h>
#include <net/dn_nsp.h>
#include <net/dn_dev.h>
#include <net/dn_route.h>
#include <net/dn_fib.h>
#include <net/dn_neigh.h>

struct dn_sock {
	struct sock sk;
	struct dn_scp scp;
};

static void dn_keepalive(struct sock *sk);

#define DN_SK_HASH_SHIFT 8
#define DN_SK_HASH_SIZE (1 << DN_SK_HASH_SHIFT)
#define DN_SK_HASH_MASK (DN_SK_HASH_SIZE - 1)


static const struct proto_ops dn_proto_ops;
static DEFINE_RWLOCK(dn_hash_lock);
static struct hlist_head dn_sk_hash[DN_SK_HASH_SIZE];
static struct hlist_head dn_wild_sk;
static atomic_long_t decnet_memory_allocated;

static int __dn_setsockopt(struct socket *sock, int level, int optname, char __user *optval, unsigned int optlen, int flags);
static int __dn_getsockopt(struct socket *sock, int level, int optname, char __user *optval, int __user *optlen, int flags);

static struct hlist_head *dn_find_list(struct sock *sk)
{
	struct dn_scp *scp = DN_SK(sk);

	if (scp->addr.sdn_flags & SDF_WILD)
		return hlist_empty(&dn_wild_sk) ? &dn_wild_sk : NULL;

	return &dn_sk_hash[le16_to_cpu(scp->addrloc) & DN_SK_HASH_MASK];
}

/*
 * Valid ports are those greater than zero and not already in use.
 */
static int check_port(__le16 port)
{
	struct sock *sk;

	if (port == 0)
		return -1;

	sk_for_each(sk, &dn_sk_hash[le16_to_cpu(port) & DN_SK_HASH_MASK]) {
		struct dn_scp *scp = DN_SK(sk);
		if (scp->addrloc == port)
			return -1;
	}
	return 0;
}

static unsigned short port_alloc(struct sock *sk)
{
	struct dn_scp *scp = DN_SK(sk);
static unsigned short port = 0x2000;
	unsigned short i_port = port;

	while(check_port(cpu_to_le16(++port)) != 0) {
		if (port == i_port)
			return 0;
	}

	scp->addrloc = cpu_to_le16(port);

	return 1;
}

/*
 * Since this is only ever called from user
 * level, we don't need a write_lock() version
 * of this.
 */
static int dn_hash_sock(struct sock *sk)
{
	struct dn_scp *scp = DN_SK(sk);
	struct hlist_head *list;
	int rv = -EUSERS;

	BUG_ON(sk_hashed(sk));

	write_lock_bh(&dn_hash_lock);

	if (!scp->addrloc && !port_alloc(sk))
		goto out;

	rv = -EADDRINUSE;
	if ((list = dn_find_list(sk)) == NULL)
		goto out;

	sk_add_node(sk, list);
	rv = 0;
out:
	write_unlock_bh(&dn_hash_lock);
	return rv;
}

static void dn_unhash_sock(struct sock *sk)
{
	write_lock(&dn_hash_lock);
	sk_del_node_init(sk);
	write_unlock(&dn_hash_lock);
}

static void dn_unhash_sock_bh(struct sock *sk)
{
	write_lock_bh(&dn_hash_lock);
	sk_del_node_init(sk);
	write_unlock_bh(&dn_hash_lock);
}

static struct hlist_head *listen_hash(struct sockaddr_dn *addr)
{
	int i;
	unsigned int hash = addr->sdn_objnum;

	if (hash == 0) {
		hash = addr->sdn_objnamel;
		for(i = 0; i < le16_to_cpu(addr->sdn_objnamel); i++) {
			hash ^= addr->sdn_objname[i];
			hash ^= (hash << 3);
		}
	}

	return &dn_sk_hash[hash & DN_SK_HASH_MASK];
}

/*
 * Called to transform a socket from bound (i.e. with a local address)
 * into a listening socket (doesn't need a local port number) and rehashes
 * based upon the object name/number.
 */
static void dn_rehash_sock(struct sock *sk)
{
	struct hlist_head *list;
	struct dn_scp *scp = DN_SK(sk);

	if (scp->addr.sdn_flags & SDF_WILD)
		return;

	write_lock_bh(&dn_hash_lock);
	sk_del_node_init(sk);
	DN_SK(sk)->addrloc = 0;
	list = listen_hash(&DN_SK(sk)->addr);
	sk_add_node(sk, list);
	write_unlock_bh(&dn_hash_lock);
}

int dn_sockaddr2username(struct sockaddr_dn *sdn, unsigned char *buf, unsigned char type)
{
	int len = 2;

	*buf++ = type;

	switch (type) {
	case 0:
		*buf++ = sdn->sdn_objnum;
		break;
	case 1:
		*buf++ = 0;
		*buf++ = le16_to_cpu(sdn->sdn_objnamel);
		memcpy(buf, sdn->sdn_objname, le16_to_cpu(sdn->sdn_objnamel));
		len = 3 + le16_to_cpu(sdn->sdn_objnamel);
		break;
	case 2:
		memset(buf, 0, 5);
		buf += 5;
		*buf++ = le16_to_cpu(sdn->sdn_objnamel);
		memcpy(buf, sdn->sdn_objname, le16_to_cpu(sdn->sdn_objnamel));
		len = 7 + le16_to_cpu(sdn->sdn_objnamel);
		break;
	}

	return len;
}

/*
 * On reception of usernames, we handle types 1 and 0 for destination
 * addresses only. Types 2 and 4 are used for source addresses, but the
 * UIC, GIC are ignored and they are both treated the same way. Type 3
 * is never used as I've no idea what its purpose might be or what its
 * format is.
 */
int dn_username2sockaddr(unsigned char *data, int len, struct sockaddr_dn *sdn, unsigned char *fmt)
{
	unsigned char type;
	int size = len;
	int namel = 12;

	sdn->sdn_objnum = 0;
	sdn->sdn_objnamel = cpu_to_le16(0);
	memset(sdn->sdn_objname, 0, DN_MAXOBJL);

	if (len < 2)
		return -1;

	len -= 2;
	*fmt = *data++;
	type = *data++;

	switch (*fmt) {
	case 0:
		sdn->sdn_objnum = type;
		return 2;
	case 1:
		namel = 16;
		break;
	case 2:
		len  -= 4;
		data += 4;
		break;
	case 4:
		len  -= 8;
		data += 8;
		break;
	default:
		return -1;
	}

	len -= 1;

	if (len < 0)
		return -1;

	sdn->sdn_objnamel = cpu_to_le16(*data++);
	len -= le16_to_cpu(sdn->sdn_objnamel);

	if ((len < 0) || (le16_to_cpu(sdn->sdn_objnamel) > namel))
		return -1;

	memcpy(sdn->sdn_objname, data, le16_to_cpu(sdn->sdn_objnamel));

	return size - len;
}

struct sock *dn_sklist_find_listener(struct sockaddr_dn *addr)
{
	struct hlist_head *list = listen_hash(addr);
	struct sock *sk;

	read_lock(&dn_hash_lock);
	sk_for_each(sk, list) {
		struct dn_scp *scp = DN_SK(sk);
		if (sk->sk_state != TCP_LISTEN)
			continue;
		if (scp->addr.sdn_objnum) {
			if (scp->addr.sdn_objnum != addr->sdn_objnum)
				continue;
		} else {
			if (addr->sdn_objnum)
				continue;
			if (scp->addr.sdn_objnamel != addr->sdn_objnamel)
				continue;
			if (memcmp(scp->addr.sdn_objname, addr->sdn_objname, le16_to_cpu(addr->sdn_objnamel)) != 0)
				continue;
		}
		sock_hold(sk);
		read_unlock(&dn_hash_lock);
		return sk;
	}

	sk = sk_head(&dn_wild_sk);
	if (sk) {
		if (sk->sk_state == TCP_LISTEN)
			sock_hold(sk);
		else
			sk = NULL;
	}

	read_unlock(&dn_hash_lock);
	return sk;
}

struct sock *dn_find_by_skb(struct sk_buff *skb)
{
	struct dn_skb_cb *cb = DN_SKB_CB(skb);
	struct sock *sk;
	struct dn_scp *scp;

	read_lock(&dn_hash_lock);
	sk_for_each(sk, &dn_sk_hash[le16_to_cpu(cb->dst_port) & DN_SK_HASH_MASK]) {
		scp = DN_SK(sk);
		if (cb->src != dn_saddr2dn(&scp->peer))
			continue;
		if (cb->dst_port != scp->addrloc)
			continue;
		if (scp->addrrem && (cb->src_port != scp->addrrem))
			continue;
		sock_hold(sk);
		goto found;
	}
	sk = NULL;
found:
	read_unlock(&dn_hash_lock);
	return sk;
}



static void dn_destruct(struct sock *sk)
{
	struct dn_scp *scp = DN_SK(sk);

	skb_queue_purge(&scp->data_xmit_queue);
	skb_queue_purge(&scp->other_xmit_queue);
	skb_queue_purge(&scp->other_receive_queue);

	dst_release(rcu_dereference_check(sk->sk_dst_cache, 1));
}

static int dn_memory_pressure;

static void dn_enter_memory_pressure(struct sock *sk)
{
	if (!dn_memory_pressure) {
		dn_memory_pressure = 1;
	}
}

static struct proto dn_proto = {
	.name			= "NSP",
	.owner			= THIS_MODULE,
	.enter_memory_pressure	= dn_enter_memory_pressure,
	.memory_pressure	= &dn_memory_pressure,
	.memory_allocated	= &decnet_memory_allocated,
	.sysctl_mem		= sysctl_decnet_mem,
	.sysctl_wmem		= sysctl_decnet_wmem,
	.sysctl_rmem		= sysctl_decnet_rmem,
	.max_header		= DN_MAX_NSP_DATA_HEADER + 64,
	.obj_size		= sizeof(struct dn_sock),
};

static struct sock *dn_alloc_sock(struct net *net, struct socket *sock, gfp_t gfp)
{
	struct dn_scp *scp;
	struct sock *sk = sk_alloc(net, PF_DECnet, gfp, &dn_proto);

	if  (!sk)
		goto out;

	if (sock)
		sock->ops = &dn_proto_ops;
	sock_init_data(sock, sk);

	sk->sk_backlog_rcv = dn_nsp_backlog_rcv;
	sk->sk_destruct    = dn_destruct;
	sk->sk_no_check_tx = 1;
	sk->sk_family      = PF_DECnet;
	sk->sk_protocol    = 0;
	sk->sk_allocation  = gfp;
	sk->sk_sndbuf	   = sysctl_decnet_wmem[1];
	sk->sk_rcvbuf	   = sysctl_decnet_rmem[1];

	/* Initialization of DECnet Session Control Port		*/
	scp = DN_SK(sk);
	scp->state	= DN_O;		/* Open			*/
	scp->numdat	= 1;		/* Next data seg to tx	*/
	scp->numoth	= 1;		/* Next oth data to tx  */
	scp->ackxmt_dat = 0;		/* Last data seg ack'ed */
	scp->ackxmt_oth = 0;		/* Last oth data ack'ed */
	scp->ackrcv_dat = 0;		/* Highest data ack recv*/
	scp->ackrcv_oth = 0;		/* Last oth data ack rec*/
	scp->flowrem_sw = DN_SEND;
	scp->flowloc_sw = DN_SEND;
	scp->flowrem_dat = 0;
	scp->flowrem_oth = 1;
	scp->flowloc_dat = 0;
	scp->flowloc_oth = 1;
	scp->services_rem = 0;
	scp->services_loc = 1 | NSP_FC_NONE;
	scp->info_rem = 0;
	scp->info_loc = 0x03; /* NSP version 4.1 */
	scp->segsize_rem = 230 - DN_MAX_NSP_DATA_HEADER; /* Default: Updated by remote segsize */
	scp->nonagle = 0;
	scp->multi_ireq = 1;
	scp->accept_mode = ACC_IMMED;
	scp->addr.sdn_family    = AF_DECnet;
	scp->peer.sdn_family    = AF_DECnet;
	scp->accessdata.acc_accl = 5;
	memcpy(scp->accessdata.acc_acc, "LINUX", 5);

	scp->max_window   = NSP_MAX_WINDOW;
	scp->snd_window   = NSP_MIN_WINDOW;
	scp->nsp_srtt     = NSP_INITIAL_SRTT;
	scp->nsp_rttvar   = NSP_INITIAL_RTTVAR;
	scp->nsp_rxtshift = 0;

	skb_queue_head_init(&scp->data_xmit_queue);
	skb_queue_head_init(&scp->other_xmit_queue);
	skb_queue_head_init(&scp->other_receive_queue);

	scp->persist = 0;
	scp->persist_fxn = NULL;
	scp->keepalive = 10 * HZ;
	scp->keepalive_fxn = dn_keepalive;

	init_timer(&scp->delack_timer);
	scp->delack_pending = 0;
	scp->delack_fxn = dn_nsp_delayed_ack;

	dn_start_slow_timer(sk);
out:
	return sk;
}

/*
 * Keepalive timer.
 * FIXME: Should respond to SO_KEEPALIVE etc.
 */
static void dn_keepalive(struct sock *sk)
{
	struct dn_scp *scp = DN_SK(sk);

	/*
	 * By checking the other_data transmit queue is empty
	 * we are double checking that we are not sending too
	 * many of these keepalive frames.
	 */
	if (skb_queue_empty(&scp->other_xmit_queue))
		dn_nsp_send_link(sk, DN_NOCHANGE, 0);
}


/*
 * Timer for shutdown/destroyed sockets.
 * When socket is dead & no packets have been sent for a
 * certain amount of time, they are removed by this
 * routine. Also takes care of sending out DI & DC
 * frames at correct times.
 */
int dn_destroy_timer(struct sock *sk)
{
	struct dn_scp *scp = DN_SK(sk);

	scp->persist = dn_nsp_persist(sk);

	switch (scp->state) {
	case DN_DI:
		dn_nsp_send_disc(sk, NSP_DISCINIT, 0, GFP_ATOMIC);
		if (scp->nsp_rxtshift >= decnet_di_count)
			scp->state = DN_CN;
		return 0;

	case DN_DR:
		dn_nsp_send_disc(sk, NSP_DISCINIT, 0, GFP_ATOMIC);
		if (scp->nsp_rxtshift >= decnet_dr_count)
			scp->state = DN_DRC;
		return 0;

	case DN_DN:
		if (scp->nsp_rxtshift < decnet_dn_count) {
			/* printk(KERN_DEBUG "dn_destroy_timer: DN\n"); */
			dn_nsp_send_disc(sk, NSP_DISCCONF, NSP_REASON_DC,
					 GFP_ATOMIC);
			return 0;
		}
	}

	scp->persist = (HZ * decnet_time_wait);

	if (sk->sk_socket)
		return 0;

	if (time_after_eq(jiffies, scp->stamp + HZ * decnet_time_wait)) {
		dn_unhash_sock(sk);
		sock_put(sk);
		return 1;
	}

	return 0;
}

static void dn_destroy_sock(struct sock *sk)
{
	struct dn_scp *scp = DN_SK(sk);

	scp->nsp_rxtshift = 0; /* reset back off */

	if (sk->sk_socket) {
		if (sk->sk_socket->state != SS_UNCONNECTED)
			sk->sk_socket->state = SS_DISCONNECTING;
	}

	sk->sk_state = TCP_CLOSE;

	switch (scp->state) {
	case DN_DN:
		dn_nsp_send_disc(sk, NSP_DISCCONF, NSP_REASON_DC,
				 sk->sk_allocation);
		scp->persist_fxn = dn_destroy_timer;
		scp->persist = dn_nsp_persist(sk);
		break;
	case DN_CR:
		scp->state = DN_DR;
		goto disc_reject;
	case DN_RUN:
		scp->state = DN_DI;
	case DN_DI:
	case DN_DR:
disc_reject:
		dn_nsp_send_disc(sk, NSP_DISCINIT, 0, sk->sk_allocation);
	case DN_NC:
	case DN_NR:
	case DN_RJ:
	case DN_DIC:
	case DN_CN:
	case DN_DRC:
	case DN_CI:
	case DN_CD:
		scp->persist_fxn = dn_destroy_timer;
		scp->persist = dn_nsp_persist(sk);
		break;
	default:
		printk(KERN_DEBUG "DECnet: dn_destroy_sock passed socket in invalid state\n");
	case DN_O:
		dn_stop_slow_timer(sk);

		dn_unhash_sock_bh(sk);
		sock_put(sk);

		break;
	}
}

char *dn_addr2asc(__u16 addr, char *buf)
{
	unsigned short node, area;

	node = addr & 0x03ff;
	area = addr >> 10;
	sprintf(buf, "%hd.%hd", area, node);

	return buf;
}



static int dn_create(struct net *net, struct socket *sock, int protocol,
		     int kern)
{
	struct sock *sk;

	if (!net_eq(net, &init_net))
		return -EAFNOSUPPORT;

	switch (sock->type) {
	case SOCK_SEQPACKET:
		if (protocol != DNPROTO_NSP)
			return -EPROTONOSUPPORT;
		break;
	case SOCK_STREAM:
		break;
	default:
		return -ESOCKTNOSUPPORT;
	}


	if ((sk = dn_alloc_sock(net, sock, GFP_KERNEL)) == NULL)
		return -ENOBUFS;

	sk->sk_protocol = protocol;

	return 0;
}


static int
dn_release(struct socket *sock)
{
	struct sock *sk = sock->sk;

	if (sk) {
		sock_orphan(sk);
		sock_hold(sk);
		lock_sock(sk);
		dn_destroy_sock(sk);
		release_sock(sk);
		sock_put(sk);
	}

	return 0;
}

static int dn_bind(struct socket *sock, struct sockaddr *uaddr, int addr_len)
{
	struct sock *sk = sock->sk;
	struct dn_scp *scp = DN_SK(sk);
	struct sockaddr_dn *saddr = (struct sockaddr_dn *)uaddr;
	struct net_device *dev, *ldev;
	int rv;

	if (addr_len != sizeof(struct sockaddr_dn))
		return -EINVAL;

	if (saddr->sdn_family != AF_DECnet)
		return -EINVAL;

	if (le16_to_cpu(saddr->sdn_nodeaddrl) && (le16_to_cpu(saddr->sdn_nodeaddrl) != 2))
		return -EINVAL;

	if (le16_to_cpu(saddr->sdn_objnamel) > DN_MAXOBJL)
		return -EINVAL;

	if (saddr->sdn_flags & ~SDF_WILD)
		return -EINVAL;

	if (!capable(CAP_NET_BIND_SERVICE) && (saddr->sdn_objnum ||
	    (saddr->sdn_flags & SDF_WILD)))
		return -EACCES;

	if (!(saddr->sdn_flags & SDF_WILD)) {
		if (le16_to_cpu(saddr->sdn_nodeaddrl)) {
			rcu_read_lock();
			ldev = NULL;
			for_each_netdev_rcu(&init_net, dev) {
				if (!dev->dn_ptr)
					continue;
				if (dn_dev_islocal(dev, dn_saddr2dn(saddr))) {
					ldev = dev;
					break;
				}
			}
			rcu_read_unlock();
			if (ldev == NULL)
				return -EADDRNOTAVAIL;
		}
	}

	rv = -EINVAL;
	lock_sock(sk);
	if (sock_flag(sk, SOCK_ZAPPED)) {
		memcpy(&scp->addr, saddr, addr_len);
		sock_reset_flag(sk, SOCK_ZAPPED);

		rv = dn_hash_sock(sk);
		if (rv)
			sock_set_flag(sk, SOCK_ZAPPED);
	}
	release_sock(sk);

	return rv;
}


static int dn_auto_bind(struct socket *sock)
{
	struct sock *sk = sock->sk;
	struct dn_scp *scp = DN_SK(sk);
	int rv;

	sock_reset_flag(sk, SOCK_ZAPPED);

	scp->addr.sdn_flags  = 0;
	scp->addr.sdn_objnum = 0;

	/*
	 * This stuff is to keep compatibility with Eduardo's
	 * patch. I hope I can dispense with it shortly...
	 */
	if ((scp->accessdata.acc_accl != 0) &&
		(scp->accessdata.acc_accl <= 12)) {

		scp->addr.sdn_objnamel = cpu_to_le16(scp->accessdata.acc_accl);
		memcpy(scp->addr.sdn_objname, scp->accessdata.acc_acc, le16_to_cpu(scp->addr.sdn_objnamel));

		scp->accessdata.acc_accl = 0;
		memset(scp->accessdata.acc_acc, 0, 40);
	}
	/* End of compatibility stuff */

	scp->addr.sdn_add.a_len = cpu_to_le16(2);
	rv = dn_dev_bind_default((__le16 *)scp->addr.sdn_add.a_addr);
	if (rv == 0) {
		rv = dn_hash_sock(sk);
		if (rv)
			sock_set_flag(sk, SOCK_ZAPPED);
	}

	return rv;
}

static int dn_confirm_accept(struct sock *sk, long *timeo, gfp_t allocation)
{
	struct dn_scp *scp = DN_SK(sk);
	DEFINE_WAIT(wait);
	int err;

	if (scp->state != DN_CR)
		return -EINVAL;

	scp->state = DN_CC;
	scp->segsize_loc = dst_metric_advmss(__sk_dst_get(sk));
	dn_send_conn_conf(sk, allocation);

	prepare_to_wait(sk_sleep(sk), &wait, TASK_INTERRUPTIBLE);
	for(;;) {
		release_sock(sk);
		if (scp->state == DN_CC)
			*timeo = schedule_timeout(*timeo);
		lock_sock(sk);
		err = 0;
		if (scp->state == DN_RUN)
			break;
		err = sock_error(sk);
		if (err)
			break;
		err = sock_intr_errno(*timeo);
		if (signal_pending(current))
			break;
		err = -EAGAIN;
		if (!*timeo)
			break;
		prepare_to_wait(sk_sleep(sk), &wait, TASK_INTERRUPTIBLE);
	}
	finish_wait(sk_sleep(sk), &wait);
	if (err == 0) {
		sk->sk_socket->state = SS_CONNECTED;
	} else if (scp->state != DN_CC) {
		sk->sk_socket->state = SS_UNCONNECTED;
	}
	return err;
}

static int dn_wait_run(struct sock *sk, long *timeo)
{
	struct dn_scp *scp = DN_SK(sk);
	DEFINE_WAIT(wait);
	int err = 0;

	if (scp->state == DN_RUN)
		goto out;

	if (!*timeo)
		return -EALREADY;

	prepare_to_wait(sk_sleep(sk), &wait, TASK_INTERRUPTIBLE);
	for(;;) {
		release_sock(sk);
		if (scp->state == DN_CI || scp->state == DN_CC)
			*timeo = schedule_timeout(*timeo);
		lock_sock(sk);
		err = 0;
		if (scp->state == DN_RUN)
			break;
		err = sock_error(sk);
		if (err)
			break;
		err = sock_intr_errno(*timeo);
		if (signal_pending(current))
			break;
		err = -ETIMEDOUT;
		if (!*timeo)
			break;
		prepare_to_wait(sk_sleep(sk), &wait, TASK_INTERRUPTIBLE);
	}
	finish_wait(sk_sleep(sk), &wait);
out:
	if (err == 0) {
		sk->sk_socket->state = SS_CONNECTED;
	} else if (scp->state != DN_CI && scp->state != DN_CC) {
		sk->sk_socket->state = SS_UNCONNECTED;
	}
	return err;
}

static int __dn_connect(struct sock *sk, struct sockaddr_dn *addr, int addrlen, long *timeo, int flags)
{
	struct socket *sock = sk->sk_socket;
	struct dn_scp *scp = DN_SK(sk);
	int err = -EISCONN;
	struct flowidn fld;
	struct dst_entry *dst;

	if (sock->state == SS_CONNECTED)
		goto out;

	if (sock->state == SS_CONNECTING) {
		err = 0;
		if (scp->state == DN_RUN) {
			sock->state = SS_CONNECTED;
			goto out;
		}
		err = -ECONNREFUSED;
		if (scp->state != DN_CI && scp->state != DN_CC) {
			sock->state = SS_UNCONNECTED;
			goto out;
		}
		return dn_wait_run(sk, timeo);
	}

	err = -EINVAL;
	if (scp->state != DN_O)
		goto out;

	if (addr == NULL || addrlen != sizeof(struct sockaddr_dn))
		goto out;
	if (addr->sdn_family != AF_DECnet)
		goto out;
	if (addr->sdn_flags & SDF_WILD)
		goto out;

	if (sock_flag(sk, SOCK_ZAPPED)) {
		err = dn_auto_bind(sk->sk_socket);
		if (err)
			goto out;
	}

	memcpy(&scp->peer, addr, sizeof(struct sockaddr_dn));

	err = -EHOSTUNREACH;
	memset(&fld, 0, sizeof(fld));
	fld.flowidn_oif = sk->sk_bound_dev_if;
	fld.daddr = dn_saddr2dn(&scp->peer);
	fld.saddr = dn_saddr2dn(&scp->addr);
	dn_sk_ports_copy(&fld, scp);
	fld.flowidn_proto = DNPROTO_NSP;
	if (dn_route_output_sock(&sk->sk_dst_cache, &fld, sk, flags) < 0)
		goto out;
	dst = __sk_dst_get(sk);
	sk->sk_route_caps = dst->dev->features;
	sock->state = SS_CONNECTING;
	scp->state = DN_CI;
	scp->segsize_loc = dst_metric_advmss(dst);

	dn_nsp_send_conninit(sk, NSP_CI);
	err = -EINPROGRESS;
	if (*timeo) {
		err = dn_wait_run(sk, timeo);
	}
out:
	return err;
}

static int dn_connect(struct socket *sock, struct sockaddr *uaddr, int addrlen, int flags)
{
	struct sockaddr_dn *addr = (struct sockaddr_dn *)uaddr;
	struct sock *sk = sock->sk;
	int err;
	long timeo = sock_sndtimeo(sk, flags & O_NONBLOCK);

	lock_sock(sk);
	err = __dn_connect(sk, addr, addrlen, &timeo, 0);
	release_sock(sk);

	return err;
}

static inline int dn_check_state(struct sock *sk, struct sockaddr_dn *addr, int addrlen, long *timeo, int flags)
{
	struct dn_scp *scp = DN_SK(sk);

	switch (scp->state) {
	case DN_RUN:
		return 0;
	case DN_CR:
		return dn_confirm_accept(sk, timeo, sk->sk_allocation);
	case DN_CI:
	case DN_CC:
		return dn_wait_run(sk, timeo);
	case DN_O:
		return __dn_connect(sk, addr, addrlen, timeo, flags);
	}

	return -EINVAL;
}


static void dn_access_copy(struct sk_buff *skb, struct accessdata_dn *acc)
{
	unsigned char *ptr = skb->data;

	acc->acc_userl = *ptr++;
	memcpy(&acc->acc_user, ptr, acc->acc_userl);
	ptr += acc->acc_userl;

	acc->acc_passl = *ptr++;
	memcpy(&acc->acc_pass, ptr, acc->acc_passl);
	ptr += acc->acc_passl;

	acc->acc_accl = *ptr++;
	memcpy(&acc->acc_acc, ptr, acc->acc_accl);

	skb_pull(skb, acc->acc_accl + acc->acc_passl + acc->acc_userl + 3);

}

static void dn_user_copy(struct sk_buff *skb, struct optdata_dn *opt)
{
	unsigned char *ptr = skb->data;
	u16 len = *ptr++; /* yes, it's 8bit on the wire */

	BUG_ON(len > 16); /* we've checked the contents earlier */
	opt->opt_optl   = cpu_to_le16(len);
	opt->opt_status = 0;
	memcpy(opt->opt_data, ptr, len);
	skb_pull(skb, len + 1);
}

static struct sk_buff *dn_wait_for_connect(struct sock *sk, long *timeo)
{
	DEFINE_WAIT(wait);
	struct sk_buff *skb = NULL;
	int err = 0;

	prepare_to_wait(sk_sleep(sk), &wait, TASK_INTERRUPTIBLE);
	for(;;) {
		release_sock(sk);
		skb = skb_dequeue(&sk->sk_receive_queue);
		if (skb == NULL) {
			*timeo = schedule_timeout(*timeo);
			skb = skb_dequeue(&sk->sk_receive_queue);
		}
		lock_sock(sk);
		if (skb != NULL)
			break;
		err = -EINVAL;
		if (sk->sk_state != TCP_LISTEN)
			break;
		err = sock_intr_errno(*timeo);
		if (signal_pending(current))
			break;
		err = -EAGAIN;
		if (!*timeo)
			break;
		prepare_to_wait(sk_sleep(sk), &wait, TASK_INTERRUPTIBLE);
	}
	finish_wait(sk_sleep(sk), &wait);

	return skb == NULL ? ERR_PTR(err) : skb;
}

static int dn_accept(struct socket *sock, struct socket *newsock, int flags)
{
	struct sock *sk = sock->sk, *newsk;
	struct sk_buff *skb = NULL;
	struct dn_skb_cb *cb;
	unsigned char menuver;
	int err = 0;
	unsigned char type;
	long timeo = sock_rcvtimeo(sk, flags & O_NONBLOCK);
	struct dst_entry *dst;

	lock_sock(sk);

	if (sk->sk_state != TCP_LISTEN || DN_SK(sk)->state != DN_O) {
		release_sock(sk);
		return -EINVAL;
	}

	skb = skb_dequeue(&sk->sk_receive_queue);
	if (skb == NULL) {
		skb = dn_wait_for_connect(sk, &timeo);
		if (IS_ERR(skb)) {
			release_sock(sk);
			return PTR_ERR(skb);
		}
	}

	cb = DN_SKB_CB(skb);
	sk->sk_ack_backlog--;
	newsk = dn_alloc_sock(sock_net(sk), newsock, sk->sk_allocation);
	if (newsk == NULL) {
		release_sock(sk);
		kfree_skb(skb);
		return -ENOBUFS;
	}
	release_sock(sk);

	dst = skb_dst(skb);
	sk_dst_set(newsk, dst);
	skb_dst_set(skb, NULL);

	DN_SK(newsk)->state        = DN_CR;
	DN_SK(newsk)->addrrem      = cb->src_port;
	DN_SK(newsk)->services_rem = cb->services;
	DN_SK(newsk)->info_rem     = cb->info;
	DN_SK(newsk)->segsize_rem  = cb->segsize;
	DN_SK(newsk)->accept_mode  = DN_SK(sk)->accept_mode;

	if (DN_SK(newsk)->segsize_rem < 230)
		DN_SK(newsk)->segsize_rem = 230;

	if ((DN_SK(newsk)->services_rem & NSP_FC_MASK) == NSP_FC_NONE)
		DN_SK(newsk)->max_window = decnet_no_fc_max_cwnd;

	newsk->sk_state  = TCP_LISTEN;
	memcpy(&(DN_SK(newsk)->addr), &(DN_SK(sk)->addr), sizeof(struct sockaddr_dn));

	/*
	 * If we are listening on a wild socket, we don't want
	 * the newly created socket on the wrong hash queue.
	 */
	DN_SK(newsk)->addr.sdn_flags &= ~SDF_WILD;

	skb_pull(skb, dn_username2sockaddr(skb->data, skb->len, &(DN_SK(newsk)->addr), &type));
	skb_pull(skb, dn_username2sockaddr(skb->data, skb->len, &(DN_SK(newsk)->peer), &type));
	*(__le16 *)(DN_SK(newsk)->peer.sdn_add.a_addr) = cb->src;
	*(__le16 *)(DN_SK(newsk)->addr.sdn_add.a_addr) = cb->dst;

	menuver = *skb->data;
	skb_pull(skb, 1);

	if (menuver & DN_MENUVER_ACC)
		dn_access_copy(skb, &(DN_SK(newsk)->accessdata));

	if (menuver & DN_MENUVER_USR)
		dn_user_copy(skb, &(DN_SK(newsk)->conndata_in));

	if (menuver & DN_MENUVER_PRX)
		DN_SK(newsk)->peer.sdn_flags |= SDF_PROXY;

	if (menuver & DN_MENUVER_UIC)
		DN_SK(newsk)->peer.sdn_flags |= SDF_UICPROXY;

	kfree_skb(skb);

	memcpy(&(DN_SK(newsk)->conndata_out), &(DN_SK(sk)->conndata_out),
		sizeof(struct optdata_dn));
	memcpy(&(DN_SK(newsk)->discdata_out), &(DN_SK(sk)->discdata_out),
		sizeof(struct optdata_dn));

	lock_sock(newsk);
	err = dn_hash_sock(newsk);
	if (err == 0) {
		sock_reset_flag(newsk, SOCK_ZAPPED);
		dn_send_conn_ack(newsk);

		/*
		 * Here we use sk->sk_allocation since although the conn conf is
		 * for the newsk, the context is the old socket.
		 */
		if (DN_SK(newsk)->accept_mode == ACC_IMMED)
			err = dn_confirm_accept(newsk, &timeo,
						sk->sk_allocation);
	}
	release_sock(newsk);
	return err;
}


static int dn_getname(struct socket *sock, struct sockaddr *uaddr,int *uaddr_len,int peer)
{
	struct sockaddr_dn *sa = (struct sockaddr_dn *)uaddr;
	struct sock *sk = sock->sk;
	struct dn_scp *scp = DN_SK(sk);

	*uaddr_len = sizeof(struct sockaddr_dn);

	lock_sock(sk);

	if (peer) {
		if ((sock->state != SS_CONNECTED &&
		     sock->state != SS_CONNECTING) &&
		    scp->accept_mode == ACC_IMMED) {
			release_sock(sk);
			return -ENOTCONN;
		}

		memcpy(sa, &scp->peer, sizeof(struct sockaddr_dn));
	} else {
		memcpy(sa, &scp->addr, sizeof(struct sockaddr_dn));
	}

	release_sock(sk);

	return 0;
}


static unsigned int dn_poll(struct file *file, struct socket *sock, poll_table  *wait)
{
	struct sock *sk = sock->sk;
	struct dn_scp *scp = DN_SK(sk);
	int mask = datagram_poll(file, sock, wait);

	if (!skb_queue_empty(&scp->other_receive_queue))
		mask |= POLLRDBAND;

	return mask;
}

static int dn_ioctl(struct socket *sock, unsigned int cmd, unsigned long arg)
{
	struct sock *sk = sock->sk;
	struct dn_scp *scp = DN_SK(sk);
	int err = -EOPNOTSUPP;
	long amount = 0;
	struct sk_buff *skb;
	int val;

	switch(cmd)
	{
	case SIOCGIFADDR:
	case SIOCSIFADDR:
		return dn_dev_ioctl(cmd, (void __user *)arg);

	case SIOCATMARK:
		lock_sock(sk);
		val = !skb_queue_empty(&scp->other_receive_queue);
		if (scp->state != DN_RUN)
			val = -ENOTCONN;
		release_sock(sk);
		return val;

	case TIOCOUTQ:
		amount = sk->sk_sndbuf - sk_wmem_alloc_get(sk);
		if (amount < 0)
			amount = 0;
		err = put_user(amount, (int __user *)arg);
		break;

	case TIOCINQ:
		lock_sock(sk);
		skb = skb_peek(&scp->other_receive_queue);
		if (skb) {
			amount = skb->len;
		} else {
			skb_queue_walk(&sk->sk_receive_queue, skb)
				amount += skb->len;
		}
		release_sock(sk);
		err = put_user(amount, (int __user *)arg);
		break;

	default:
		err = -ENOIOCTLCMD;
		break;
	}

	return err;
}

static int dn_listen(struct socket *sock, int backlog)
{
	struct sock *sk = sock->sk;
	int err = -EINVAL;

	lock_sock(sk);

	if (sock_flag(sk, SOCK_ZAPPED))
		goto out;

	if ((DN_SK(sk)->state != DN_O) || (sk->sk_state == TCP_LISTEN))
		goto out;

	sk->sk_max_ack_backlog = backlog;
	sk->sk_ack_backlog     = 0;
	sk->sk_state           = TCP_LISTEN;
	err                 = 0;
	dn_rehash_sock(sk);

out:
	release_sock(sk);

	return err;
}


static int dn_shutdown(struct socket *sock, int how)
{
	struct sock *sk = sock->sk;
	struct dn_scp *scp = DN_SK(sk);
	int err = -ENOTCONN;

	lock_sock(sk);

	if (sock->state == SS_UNCONNECTED)
		goto out;

	err = 0;
	if (sock->state == SS_DISCONNECTING)
		goto out;

	err = -EINVAL;
	if (scp->state == DN_O)
		goto out;

	if (how != SHUT_RDWR)
		goto out;

	sk->sk_shutdown = SHUTDOWN_MASK;
	dn_destroy_sock(sk);
	err = 0;

out:
	release_sock(sk);

	return err;
}

static int dn_setsockopt(struct socket *sock, int level, int optname, char __user *optval, unsigned int optlen)
{
	struct sock *sk = sock->sk;
	int err;

	lock_sock(sk);
	err = __dn_setsockopt(sock, level, optname, optval, optlen, 0);
	release_sock(sk);

	return err;
}

static int __dn_setsockopt(struct socket *sock, int level,int optname, char __user *optval, unsigned int optlen, int flags)
{
	struct	sock *sk = sock->sk;
	struct dn_scp *scp = DN_SK(sk);
	long timeo;
	union {
		struct optdata_dn opt;
		struct accessdata_dn acc;
		int mode;
		unsigned long win;
		int val;
		unsigned char services;
		unsigned char info;
	} u;
	int err;

	if (optlen && !optval)
		return -EINVAL;

	if (optlen > sizeof(u))
		return -EINVAL;

	if (copy_from_user(&u, optval, optlen))
		return -EFAULT;

	switch (optname) {
	case DSO_CONDATA:
		if (sock->state == SS_CONNECTED)
			return -EISCONN;
		if ((scp->state != DN_O) && (scp->state != DN_CR))
			return -EINVAL;

		if (optlen != sizeof(struct optdata_dn))
			return -EINVAL;

		if (le16_to_cpu(u.opt.opt_optl) > 16)
			return -EINVAL;

		memcpy(&scp->conndata_out, &u.opt, optlen);
		break;

	case DSO_DISDATA:
		if (sock->state != SS_CONNECTED &&
		    scp->accept_mode == ACC_IMMED)
			return -ENOTCONN;

		if (optlen != sizeof(struct optdata_dn))
			return -EINVAL;

		if (le16_to_cpu(u.opt.opt_optl) > 16)
			return -EINVAL;

		memcpy(&scp->discdata_out, &u.opt, optlen);
		break;

	case DSO_CONACCESS:
		if (sock->state == SS_CONNECTED)
			return -EISCONN;
		if (scp->state != DN_O)
			return -EINVAL;

		if (optlen != sizeof(struct accessdata_dn))
			return -EINVAL;

		if ((u.acc.acc_accl > DN_MAXACCL) ||
		    (u.acc.acc_passl > DN_MAXACCL) ||
		    (u.acc.acc_userl > DN_MAXACCL))
			return -EINVAL;

		memcpy(&scp->accessdata, &u.acc, optlen);
		break;

	case DSO_ACCEPTMODE:
		if (sock->state == SS_CONNECTED)
			return -EISCONN;
		if (scp->state != DN_O)
			return -EINVAL;

		if (optlen != sizeof(int))
			return -EINVAL;

		if ((u.mode != ACC_IMMED) && (u.mode != ACC_DEFER))
			return -EINVAL;

		scp->accept_mode = (unsigned char)u.mode;
		break;

	case DSO_CONACCEPT:
		if (scp->state != DN_CR)
			return -EINVAL;
		timeo = sock_rcvtimeo(sk, 0);
		err = dn_confirm_accept(sk, &timeo, sk->sk_allocation);
		return err;

	case DSO_CONREJECT:
		if (scp->state != DN_CR)
			return -EINVAL;

		scp->state = DN_DR;
		sk->sk_shutdown = SHUTDOWN_MASK;
		dn_nsp_send_disc(sk, 0x38, 0, sk->sk_allocation);
		break;

	default:
#ifdef CONFIG_NETFILTER
		return nf_setsockopt(sk, PF_DECnet, optname, optval, optlen);
#endif
	case DSO_LINKINFO:
	case DSO_STREAM:
	case DSO_SEQPACKET:
		return -ENOPROTOOPT;

	case DSO_MAXWINDOW:
		if (optlen != sizeof(unsigned long))
			return -EINVAL;
		if (u.win > NSP_MAX_WINDOW)
			u.win = NSP_MAX_WINDOW;
		if (u.win == 0)
			return -EINVAL;
		scp->max_window = u.win;
		if (scp->snd_window > u.win)
			scp->snd_window = u.win;
		break;

	case DSO_NODELAY:
		if (optlen != sizeof(int))
			return -EINVAL;
		if (scp->nonagle == 2)
			return -EINVAL;
		scp->nonagle = (u.val == 0) ? 0 : 1;
		/* if (scp->nonagle == 1) { Push pending frames } */
		break;

	case DSO_CORK:
		if (optlen != sizeof(int))
			return -EINVAL;
		if (scp->nonagle == 1)
			return -EINVAL;
		scp->nonagle = (u.val == 0) ? 0 : 2;
		/* if (scp->nonagle == 0) { Push pending frames } */
		break;

	case DSO_SERVICES:
		if (optlen != sizeof(unsigned char))
			return -EINVAL;
		if ((u.services & ~NSP_FC_MASK) != 0x01)
			return -EINVAL;
		if ((u.services & NSP_FC_MASK) == NSP_FC_MASK)
			return -EINVAL;
		scp->services_loc = u.services;
		break;

	case DSO_INFO:
		if (optlen != sizeof(unsigned char))
			return -EINVAL;
		if (u.info & 0xfc)
			return -EINVAL;
		scp->info_loc = u.info;
		break;
	}

	return 0;
}

static int dn_getsockopt(struct socket *sock, int level, int optname, char __user *optval, int __user *optlen)
{
	struct sock *sk = sock->sk;
	int err;

	lock_sock(sk);
	err = __dn_getsockopt(sock, level, optname, optval, optlen, 0);
	release_sock(sk);

	return err;
}

static int __dn_getsockopt(struct socket *sock, int level,int optname, char __user *optval,int __user *optlen, int flags)
{
	struct	sock *sk = sock->sk;
	struct dn_scp *scp = DN_SK(sk);
	struct linkinfo_dn link;
	unsigned int r_len;
	void *r_data = NULL;
	unsigned int val;

	if(get_user(r_len , optlen))
		return -EFAULT;

	switch (optname) {
	case DSO_CONDATA:
		if (r_len > sizeof(struct optdata_dn))
			r_len = sizeof(struct optdata_dn);
		r_data = &scp->conndata_in;
		break;

	case DSO_DISDATA:
		if (r_len > sizeof(struct optdata_dn))
			r_len = sizeof(struct optdata_dn);
		r_data = &scp->discdata_in;
		break;

	case DSO_CONACCESS:
		if (r_len > sizeof(struct accessdata_dn))
			r_len = sizeof(struct accessdata_dn);
		r_data = &scp->accessdata;
		break;

	case DSO_ACCEPTMODE:
		if (r_len > sizeof(unsigned char))
			r_len = sizeof(unsigned char);
		r_data = &scp->accept_mode;
		break;

	case DSO_LINKINFO:
		if (r_len > sizeof(struct linkinfo_dn))
			r_len = sizeof(struct linkinfo_dn);

		memset(&link, 0, sizeof(link));

		switch (sock->state) {
		case SS_CONNECTING:
			link.idn_linkstate = LL_CONNECTING;
			break;
		case SS_DISCONNECTING:
			link.idn_linkstate = LL_DISCONNECTING;
			break;
		case SS_CONNECTED:
			link.idn_linkstate = LL_RUNNING;
			break;
		default:
			link.idn_linkstate = LL_INACTIVE;
		}

		link.idn_segsize = scp->segsize_rem;
		r_data = &link;
		break;

	default:
#ifdef CONFIG_NETFILTER
	{
		int ret, len;

		if (get_user(len, optlen))
			return -EFAULT;

		ret = nf_getsockopt(sk, PF_DECnet, optname, optval, &len);
		if (ret >= 0)
			ret = put_user(len, optlen);
		return ret;
	}
#endif
	case DSO_STREAM:
	case DSO_SEQPACKET:
	case DSO_CONACCEPT:
	case DSO_CONREJECT:
		return -ENOPROTOOPT;

	case DSO_MAXWINDOW:
		if (r_len > sizeof(unsigned long))
			r_len = sizeof(unsigned long);
		r_data = &scp->max_window;
		break;

	case DSO_NODELAY:
		if (r_len > sizeof(int))
			r_len = sizeof(int);
		val = (scp->nonagle == 1);
		r_data = &val;
		break;

	case DSO_CORK:
		if (r_len > sizeof(int))
			r_len = sizeof(int);
		val = (scp->nonagle == 2);
		r_data = &val;
		break;

	case DSO_SERVICES:
		if (r_len > sizeof(unsigned char))
			r_len = sizeof(unsigned char);
		r_data = &scp->services_rem;
		break;

	case DSO_INFO:
		if (r_len > sizeof(unsigned char))
			r_len = sizeof(unsigned char);
		r_data = &scp->info_rem;
		break;
	}

	if (r_data) {
		if (copy_to_user(optval, r_data, r_len))
			return -EFAULT;
		if (put_user(r_len, optlen))
			return -EFAULT;
	}

	return 0;
}


static int dn_data_ready(struct sock *sk, struct sk_buff_head *q, int flags, int target)
{
	struct sk_buff *skb;
	int len = 0;

	if (flags & MSG_OOB)
		return !skb_queue_empty(q) ? 1 : 0;

	skb_queue_walk(q, skb) {
		struct dn_skb_cb *cb = DN_SKB_CB(skb);
		len += skb->len;

		if (cb->nsp_flags & 0x40) {
			/* SOCK_SEQPACKET reads to EOM */
			if (sk->sk_type == SOCK_SEQPACKET)
				return 1;
			/* so does SOCK_STREAM unless WAITALL is specified */
			if (!(flags & MSG_WAITALL))
				return 1;
		}

		/* minimum data length for read exceeded */
		if (len >= target)
			return 1;
	}

	return 0;
}


static int dn_recvmsg(struct socket *sock, struct msghdr *msg, size_t size,
		      int flags)
{
	struct sock *sk = sock->sk;
	struct dn_scp *scp = DN_SK(sk);
	struct sk_buff_head *queue = &sk->sk_receive_queue;
	size_t target = size > 1 ? 1 : 0;
	size_t copied = 0;
	int rv = 0;
	struct sk_buff *skb, *n;
	struct dn_skb_cb *cb = NULL;
	unsigned char eor = 0;
	long timeo = sock_rcvtimeo(sk, flags & MSG_DONTWAIT);

	lock_sock(sk);

	if (sock_flag(sk, SOCK_ZAPPED)) {
		rv = -EADDRNOTAVAIL;
		goto out;
	}

	if (sk->sk_shutdown & RCV_SHUTDOWN) {
		rv = 0;
		goto out;
	}

	rv = dn_check_state(sk, NULL, 0, &timeo, flags);
	if (rv)
		goto out;

	if (flags & ~(MSG_CMSG_COMPAT|MSG_PEEK|MSG_OOB|MSG_WAITALL|MSG_DONTWAIT|MSG_NOSIGNAL)) {
		rv = -EOPNOTSUPP;
		goto out;
	}

	if (flags & MSG_OOB)
		queue = &scp->other_receive_queue;

	if (flags & MSG_WAITALL)
		target = size;


	/*
	 * See if there is data ready to read, sleep if there isn't
	 */
	for(;;) {
		DEFINE_WAIT(wait);

		if (sk->sk_err)
			goto out;

		if (!skb_queue_empty(&scp->other_receive_queue)) {
			if (!(flags & MSG_OOB)) {
				msg->msg_flags |= MSG_OOB;
				if (!scp->other_report) {
					scp->other_report = 1;
					goto out;
				}
			}
		}

		if (scp->state != DN_RUN)
			goto out;

		if (signal_pending(current)) {
			rv = sock_intr_errno(timeo);
			goto out;
		}

		if (dn_data_ready(sk, queue, flags, target))
			break;

		if (flags & MSG_DONTWAIT) {
			rv = -EWOULDBLOCK;
			goto out;
		}

		prepare_to_wait(sk_sleep(sk), &wait, TASK_INTERRUPTIBLE);
		set_bit(SOCK_ASYNC_WAITDATA, &sk->sk_socket->flags);
		sk_wait_event(sk, &timeo, dn_data_ready(sk, queue, flags, target));
		clear_bit(SOCK_ASYNC_WAITDATA, &sk->sk_socket->flags);
		finish_wait(sk_sleep(sk), &wait);
	}

	skb_queue_walk_safe(queue, skb, n) {
		unsigned int chunk = skb->len;
		cb = DN_SKB_CB(skb);

		if ((chunk + copied) > size)
			chunk = size - copied;

		if (memcpy_to_msg(msg, skb->data, chunk)) {
			rv = -EFAULT;
			break;
		}
		copied += chunk;

		if (!(flags & MSG_PEEK))
			skb_pull(skb, chunk);

		eor = cb->nsp_flags & 0x40;

		if (skb->len == 0) {
			skb_unlink(skb, queue);
			kfree_skb(skb);
			/*
			 * N.B. Don't refer to skb or cb after this point
			 * in loop.
			 */
			if ((scp->flowloc_sw == DN_DONTSEND) && !dn_congested(sk)) {
				scp->flowloc_sw = DN_SEND;
				dn_nsp_send_link(sk, DN_SEND, 0);
			}
		}

		if (eor) {
			if (sk->sk_type == SOCK_SEQPACKET)
				break;
			if (!(flags & MSG_WAITALL))
				break;
		}

		if (flags & MSG_OOB)
			break;

		if (copied >= target)
			break;
	}

	rv = copied;


	if (eor && (sk->sk_type == SOCK_SEQPACKET))
		msg->msg_flags |= MSG_EOR;

out:
	if (rv == 0)
		rv = (flags & MSG_PEEK) ? -sk->sk_err : sock_error(sk);

	if ((rv >= 0) && msg->msg_name) {
		__sockaddr_check_size(sizeof(struct sockaddr_dn));
		memcpy(msg->msg_name, &scp->peer, sizeof(struct sockaddr_dn));
		msg->msg_namelen = sizeof(struct sockaddr_dn);
	}

	release_sock(sk);

	return rv;
}


static inline int dn_queue_too_long(struct dn_scp *scp, struct sk_buff_head *queue, int flags)
{
	unsigned char fctype = scp->services_rem & NSP_FC_MASK;
	if (skb_queue_len(queue) >= scp->snd_window)
		return 1;
	if (fctype != NSP_FC_NONE) {
		if (flags & MSG_OOB) {
			if (scp->flowrem_oth == 0)
				return 1;
		} else {
			if (scp->flowrem_dat == 0)
				return 1;
		}
	}
	return 0;
}

/*
 * The DECnet spec requires that the "routing layer" accepts packets which
 * are at least 230 bytes in size. This excludes any headers which the NSP
 * layer might add, so we always assume that we'll be using the maximal
 * length header on data packets. The variation in length is due to the
 * inclusion (or not) of the two 16 bit acknowledgement fields so it doesn't
 * make much practical difference.
 */
unsigned int dn_mss_from_pmtu(struct net_device *dev, int mtu)
{
	unsigned int mss = 230 - DN_MAX_NSP_DATA_HEADER;
	if (dev) {
		struct dn_dev *dn_db = rcu_dereference_raw(dev->dn_ptr);
		mtu -= LL_RESERVED_SPACE(dev);
		if (dn_db->use_long)
			mtu -= 21;
		else
			mtu -= 6;
		mtu -= DN_MAX_NSP_DATA_HEADER;
	} else {
		/*
		 * 21 = long header, 16 = guess at MAC header length
		 */
		mtu -= (21 + DN_MAX_NSP_DATA_HEADER + 16);
	}
	if (mtu > mss)
		mss = mtu;
	return mss;
}

static inline unsigned int dn_current_mss(struct sock *sk, int flags)
{
	struct dst_entry *dst = __sk_dst_get(sk);
	struct dn_scp *scp = DN_SK(sk);
	int mss_now = min_t(int, scp->segsize_loc, scp->segsize_rem);

	/* Other data messages are limited to 16 bytes per packet */
	if (flags & MSG_OOB)
		return 16;

	/* This works out the maximum size of segment we can send out */
	if (dst) {
		u32 mtu = dst_mtu(dst);
		mss_now = min_t(int, dn_mss_from_pmtu(dst->dev, mtu), mss_now);
	}

	return mss_now;
}

/*
 * N.B. We get the timeout wrong here, but then we always did get it
 * wrong before and this is another step along the road to correcting
 * it. It ought to get updated each time we pass through the routine,
 * but in practise it probably doesn't matter too much for now.
 */
static inline struct sk_buff *dn_alloc_send_pskb(struct sock *sk,
			      unsigned long datalen, int noblock,
			      int *errcode)
{
	struct sk_buff *skb = sock_alloc_send_skb(sk, datalen,
						   noblock, errcode);
	if (skb) {
		skb->protocol = htons(ETH_P_DNA_RT);
		skb->pkt_type = PACKET_OUTGOING;
	}
	return skb;
}

static int dn_sendmsg(struct socket *sock, struct msghdr *msg, size_t size)
{
	struct sock *sk = sock->sk;
	struct dn_scp *scp = DN_SK(sk);
	size_t mss;
	struct sk_buff_head *queue = &scp->data_xmit_queue;
	int flags = msg->msg_flags;
	int err = 0;
	size_t sent = 0;
	int addr_len = msg->msg_namelen;
	DECLARE_SOCKADDR(struct sockaddr_dn *, addr, msg->msg_name);
	struct sk_buff *skb = NULL;
	struct dn_skb_cb *cb;
	size_t len;
	unsigned char fctype;
	long timeo;

	if (flags & ~(MSG_TRYHARD|MSG_OOB|MSG_DONTWAIT|MSG_EOR|MSG_NOSIGNAL|MSG_MORE|MSG_CMSG_COMPAT))
		return -EOPNOTSUPP;

	if (addr_len && (addr_len != sizeof(struct sockaddr_dn)))
		return -EINVAL;

	lock_sock(sk);
	timeo = sock_sndtimeo(sk, flags & MSG_DONTWAIT);
	/*
	 * The only difference between stream sockets and sequenced packet
	 * sockets is that the stream sockets always behave as if MSG_EOR
	 * has been set.
	 */
	if (sock->type == SOCK_STREAM) {
		if (flags & MSG_EOR) {
			err = -EINVAL;
			goto out;
		}
		flags |= MSG_EOR;
	}


	err = dn_check_state(sk, addr, addr_len, &timeo, flags);
	if (err)
		goto out_err;

	if (sk->sk_shutdown & SEND_SHUTDOWN) {
		err = -EPIPE;
		if (!(flags & MSG_NOSIGNAL))
			send_sig(SIGPIPE, current, 0);
		goto out_err;
	}

	if ((flags & MSG_TRYHARD) && sk->sk_dst_cache)
		dst_negative_advice(sk);

	mss = scp->segsize_rem;
	fctype = scp->services_rem & NSP_FC_MASK;

	mss = dn_current_mss(sk, flags);

	if (flags & MSG_OOB) {
		queue = &scp->other_xmit_queue;
		if (size > mss) {
			err = -EMSGSIZE;
			goto out;
		}
	}

	scp->persist_fxn = dn_nsp_xmit_timeout;

	while(sent < size) {
		err = sock_error(sk);
		if (err)
			goto out;

		if (signal_pending(current)) {
			err = sock_intr_errno(timeo);
			goto out;
		}

		/*
		 * Calculate size that we wish to send.
		 */
		len = size - sent;

		if (len > mss)
			len = mss;

		/*
		 * Wait for queue size to go down below the window
		 * size.
		 */
		if (dn_queue_too_long(scp, queue, flags)) {
			DEFINE_WAIT(wait);

			if (flags & MSG_DONTWAIT) {
				err = -EWOULDBLOCK;
				goto out;
			}

			prepare_to_wait(sk_sleep(sk), &wait, TASK_INTERRUPTIBLE);
			set_bit(SOCK_ASYNC_WAITDATA, &sk->sk_socket->flags);
			sk_wait_event(sk, &timeo,
				      !dn_queue_too_long(scp, queue, flags));
			clear_bit(SOCK_ASYNC_WAITDATA, &sk->sk_socket->flags);
			finish_wait(sk_sleep(sk), &wait);
			continue;
		}

		/*
		 * Get a suitably sized skb.
		 * 64 is a bit of a hack really, but its larger than any
		 * link-layer headers and has served us well as a good
		 * guess as to their real length.
		 */
		skb = dn_alloc_send_pskb(sk, len + 64 + DN_MAX_NSP_DATA_HEADER,
					 flags & MSG_DONTWAIT, &err);

		if (err)
			break;

		if (!skb)
			continue;

		cb = DN_SKB_CB(skb);

		skb_reserve(skb, 64 + DN_MAX_NSP_DATA_HEADER);

		if (memcpy_from_msg(skb_put(skb, len), msg, len)) {
			err = -EFAULT;
			goto out;
		}

		if (flags & MSG_OOB) {
			cb->nsp_flags = 0x30;
			if (fctype != NSP_FC_NONE)
				scp->flowrem_oth--;
		} else {
			cb->nsp_flags = 0x00;
			if (scp->seg_total == 0)
				cb->nsp_flags |= 0x20;

			scp->seg_total += len;

			if (((sent + len) == size) && (flags & MSG_EOR)) {
				cb->nsp_flags |= 0x40;
				scp->seg_total = 0;
				if (fctype == NSP_FC_SCMC)
					scp->flowrem_dat--;
			}
			if (fctype == NSP_FC_SRC)
				scp->flowrem_dat--;
		}

		sent += len;
		dn_nsp_queue_xmit(sk, skb, sk->sk_allocation, flags & MSG_OOB);
		skb = NULL;

		scp->persist = dn_nsp_persist(sk);

	}
out:

	kfree_skb(skb);

	release_sock(sk);

	return sent ? sent : err;

out_err:
	err = sk_stream_error(sk, flags, err);
	release_sock(sk);
	return err;
}

static int dn_device_event(struct notifier_block *this, unsigned long event,
			   void *ptr)
{
	struct net_device *dev = netdev_notifier_info_to_dev(ptr);

	if (!net_eq(dev_net(dev), &init_net))
		return NOTIFY_DONE;

	switch (event) {
	case NETDEV_UP:
		dn_dev_up(dev);
		break;
	case NETDEV_DOWN:
		dn_dev_down(dev);
		break;
	default:
		break;
	}

	return NOTIFY_DONE;
}

static struct notifier_block dn_dev_notifier = {
	.notifier_call = dn_device_event,
};

static struct packet_type dn_dix_packet_type __read_mostly = {
	.type =		cpu_to_be16(ETH_P_DNA_RT),
	.func =		dn_route_rcv,
};

#ifdef CONFIG_PROC_FS
struct dn_iter_state {
	int bucket;
};

static struct sock *dn_socket_get_first(struct seq_file *seq)
{
	struct dn_iter_state *state = seq->private;
	struct sock *n = NULL;

	for(state->bucket = 0;
	    state->bucket < DN_SK_HASH_SIZE;
	    ++state->bucket) {
		n = sk_head(&dn_sk_hash[state->bucket]);
		if (n)
			break;
	}

	return n;
}

static struct sock *dn_socket_get_next(struct seq_file *seq,
				       struct sock *n)
{
	struct dn_iter_state *state = seq->private;

	n = sk_next(n);
try_again:
	if (n)
		goto out;
	if (++state->bucket >= DN_SK_HASH_SIZE)
		goto out;
	n = sk_head(&dn_sk_hash[state->bucket]);
	goto try_again;
out:
	return n;
}

static struct sock *socket_get_idx(struct seq_file *seq, loff_t *pos)
{
	struct sock *sk = dn_socket_get_first(seq);

	if (sk) {
		while(*pos && (sk = dn_socket_get_next(seq, sk)))
			--*pos;
	}
	return *pos ? NULL : sk;
}

static void *dn_socket_get_idx(struct seq_file *seq, loff_t pos)
{
	void *rc;
	read_lock_bh(&dn_hash_lock);
	rc = socket_get_idx(seq, &pos);
	if (!rc) {
		read_unlock_bh(&dn_hash_lock);
	}
	return rc;
}

static void *dn_socket_seq_start(struct seq_file *seq, loff_t *pos)
{
	return *pos ? dn_socket_get_idx(seq, *pos - 1) : SEQ_START_TOKEN;
}

static void *dn_socket_seq_next(struct seq_file *seq, void *v, loff_t *pos)
{
	void *rc;

	if (v == SEQ_START_TOKEN) {
		rc = dn_socket_get_idx(seq, 0);
		goto out;
	}

	rc = dn_socket_get_next(seq, v);
	if (rc)
		goto out;
	read_unlock_bh(&dn_hash_lock);
out:
	++*pos;
	return rc;
}

static void dn_socket_seq_stop(struct seq_file *seq, void *v)
{
	if (v && v != SEQ_START_TOKEN)
		read_unlock_bh(&dn_hash_lock);
}

#define IS_NOT_PRINTABLE(x) ((x) < 32 || (x) > 126)

static void dn_printable_object(struct sockaddr_dn *dn, unsigned char *buf)
{
	int i;

	switch (le16_to_cpu(dn->sdn_objnamel)) {
	case 0:
		sprintf(buf, "%d", dn->sdn_objnum);
		break;
	default:
		for (i = 0; i < le16_to_cpu(dn->sdn_objnamel); i++) {
			buf[i] = dn->sdn_objname[i];
			if (IS_NOT_PRINTABLE(buf[i]))
				buf[i] = '.';
		}
		buf[i] = 0;
	}
}

static char *dn_state2asc(unsigned char state)
{
	switch (state) {
	case DN_O:
		return "OPEN";
	case DN_CR:
		return "  CR";
	case DN_DR:
		return "  DR";
	case DN_DRC:
		return " DRC";
	case DN_CC:
		return "  CC";
	case DN_CI:
		return "  CI";
	case DN_NR:
		return "  NR";
	case DN_NC:
		return "  NC";
	case DN_CD:
		return "  CD";
	case DN_RJ:
		return "  RJ";
	case DN_RUN:
		return " RUN";
	case DN_DI:
		return "  DI";
	case DN_DIC:
		return " DIC";
	case DN_DN:
		return "  DN";
	case DN_CL:
		return "  CL";
	case DN_CN:
		return "  CN";
	}

	return "????";
}

static inline void dn_socket_format_entry(struct seq_file *seq, struct sock *sk)
{
	struct dn_scp *scp = DN_SK(sk);
	char buf1[DN_ASCBUF_LEN];
	char buf2[DN_ASCBUF_LEN];
	char local_object[DN_MAXOBJL+3];
	char remote_object[DN_MAXOBJL+3];

	dn_printable_object(&scp->addr, local_object);
	dn_printable_object(&scp->peer, remote_object);

	seq_printf(seq,
		   "%6s/%04X %04d:%04d %04d:%04d %01d %-16s "
		   "%6s/%04X %04d:%04d %04d:%04d %01d %-16s %4s %s\n",
		   dn_addr2asc(le16_to_cpu(dn_saddr2dn(&scp->addr)), buf1),
		   scp->addrloc,
		   scp->numdat,
		   scp->numoth,
		   scp->ackxmt_dat,
		   scp->ackxmt_oth,
		   scp->flowloc_sw,
		   local_object,
		   dn_addr2asc(le16_to_cpu(dn_saddr2dn(&scp->peer)), buf2),
		   scp->addrrem,
		   scp->numdat_rcv,
		   scp->numoth_rcv,
		   scp->ackrcv_dat,
		   scp->ackrcv_oth,
		   scp->flowrem_sw,
		   remote_object,
		   dn_state2asc(scp->state),
		   ((scp->accept_mode == ACC_IMMED) ? "IMMED" : "DEFER"));
}

static int dn_socket_seq_show(struct seq_file *seq, void *v)
{
	if (v == SEQ_START_TOKEN) {
		seq_puts(seq, "Local                                              Remote\n");
	} else {
		dn_socket_format_entry(seq, v);
	}
	return 0;
}

static const struct seq_operations dn_socket_seq_ops = {
	.start	= dn_socket_seq_start,
	.next	= dn_socket_seq_next,
	.stop	= dn_socket_seq_stop,
	.show	= dn_socket_seq_show,
};

static int dn_socket_seq_open(struct inode *inode, struct file *file)
{
	return seq_open_private(file, &dn_socket_seq_ops,
			sizeof(struct dn_iter_state));
}

static const struct file_operations dn_socket_seq_fops = {
	.owner		= THIS_MODULE,
	.open		= dn_socket_seq_open,
	.read		= seq_read,
	.llseek		= seq_lseek,
	.release	= seq_release_private,
};
#endif

static const struct net_proto_family	dn_family_ops = {
	.family =	AF_DECnet,
	.create =	dn_create,
	.owner	=	THIS_MODULE,
};

static const struct proto_ops dn_proto_ops = {
	.family =	AF_DECnet,
	.owner =	THIS_MODULE,
	.release =	dn_release,
	.bind =		dn_bind,
	.connect =	dn_connect,
	.socketpair =	sock_no_socketpair,
	.accept =	dn_accept,
	.getname =	dn_getname,
	.poll =		dn_poll,
	.ioctl =	dn_ioctl,
	.listen =	dn_listen,
	.shutdown =	dn_shutdown,
	.setsockopt =	dn_setsockopt,
	.getsockopt =	dn_getsockopt,
	.sendmsg =	dn_sendmsg,
	.recvmsg =	dn_recvmsg,
	.mmap =		sock_no_mmap,
	.sendpage =	sock_no_sendpage,
};

MODULE_DESCRIPTION("The Linux DECnet Network Protocol");
MODULE_AUTHOR("Linux DECnet Project Team");
MODULE_LICENSE("GPL");
MODULE_ALIAS_NETPROTO(PF_DECnet);

static char banner[] __initdata = KERN_INFO "NET4: DECnet for Linux: V.2.5.68s (C) 1995-2003 Linux DECnet Project Team\n";

static int __init decnet_init(void)
{
	int rc;

	printk(banner);

	rc = proto_register(&dn_proto, 1);
	if (rc != 0)
		goto out;

	dn_neigh_init();
	dn_dev_init();
	dn_route_init();
	dn_fib_init();

	sock_register(&dn_family_ops);
	dev_add_pack(&dn_dix_packet_type);
	register_netdevice_notifier(&dn_dev_notifier);

	proc_create("decnet", S_IRUGO, init_net.proc_net, &dn_socket_seq_fops);
	dn_register_sysctl();
out:
	return rc;

}
module_init(decnet_init);

/*
 * Prevent DECnet module unloading until its fixed properly.
 * Requires an audit of the code to check for memory leaks and
 * initialisation problems etc.
 */
#if 0
static void __exit decnet_exit(void)
{
	sock_unregister(AF_DECnet);
	rtnl_unregister_all(PF_DECnet);
	dev_remove_pack(&dn_dix_packet_type);

	dn_unregister_sysctl();

	unregister_netdevice_notifier(&dn_dev_notifier);

	dn_route_cleanup();
	dn_dev_cleanup();
	dn_neigh_cleanup();
	dn_fib_cleanup();

	remove_proc_entry("decnet", init_net.proc_net);

	proto_unregister(&dn_proto);

	rcu_barrier_bh(); /* Wait for completion of call_rcu_bh()'s */
}
module_exit(decnet_exit);
#endif
