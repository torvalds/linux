/*
 * linux/net/sunrpc/svcsock.c
 *
 * These are the RPC server socket internals.
 *
 * The server scheduling algorithm does not always distribute the load
 * evenly when servicing a single client. May need to modify the
 * svc_sock_enqueue procedure...
 *
 * TCP support is largely untested and may be a little slow. The problem
 * is that we currently do two separate recvfrom's, one for the 4-byte
 * record length, and the second for the actual record. This could possibly
 * be improved by always reading a minimum size of around 100 bytes and
 * tucking any superfluous bytes away in a temporary store. Still, that
 * leaves write requests out in the rain. An alternative may be to peek at
 * the first skb in the queue, and if it matches the next TCP sequence
 * number, to extract the record marker. Yuck.
 *
 * Copyright (C) 1995, 1996 Olaf Kirch <okir@monad.swb.de>
 */

#include <linux/sched.h>
#include <linux/errno.h>
#include <linux/fcntl.h>
#include <linux/net.h>
#include <linux/in.h>
#include <linux/inet.h>
#include <linux/udp.h>
#include <linux/tcp.h>
#include <linux/unistd.h>
#include <linux/slab.h>
#include <linux/netdevice.h>
#include <linux/skbuff.h>
#include <linux/file.h>
#include <linux/freezer.h>
#include <net/sock.h>
#include <net/checksum.h>
#include <net/ip.h>
#include <net/ipv6.h>
#include <net/tcp_states.h>
#include <asm/uaccess.h>
#include <asm/ioctls.h>

#include <linux/sunrpc/types.h>
#include <linux/sunrpc/clnt.h>
#include <linux/sunrpc/xdr.h>
#include <linux/sunrpc/svcsock.h>
#include <linux/sunrpc/stats.h>

/* SMP locking strategy:
 *
 *	svc_pool->sp_lock protects most of the fields of that pool.
 * 	svc_serv->sv_lock protects sv_tempsocks, sv_permsocks, sv_tmpcnt.
 *	when both need to be taken (rare), svc_serv->sv_lock is first.
 *	BKL protects svc_serv->sv_nrthread.
 *	svc_sock->sk_defer_lock protects the svc_sock->sk_deferred list
 *	svc_sock->sk_flags.SK_BUSY prevents a svc_sock being enqueued multiply.
 *
 *	Some flags can be set to certain values at any time
 *	providing that certain rules are followed:
 *
 *	SK_CONN, SK_DATA, can be set or cleared at any time.
 *		after a set, svc_sock_enqueue must be called.
 *		after a clear, the socket must be read/accepted
 *		 if this succeeds, it must be set again.
 *	SK_CLOSE can set at any time. It is never cleared.
 *      sk_inuse contains a bias of '1' until SK_DEAD is set.
 *             so when sk_inuse hits zero, we know the socket is dead
 *             and no-one is using it.
 *      SK_DEAD can only be set while SK_BUSY is held which ensures
 *             no other thread will be using the socket or will try to
 *	       set SK_DEAD.
 *
 */

#define RPCDBG_FACILITY	RPCDBG_SVCSOCK


static struct svc_sock *svc_setup_socket(struct svc_serv *, struct socket *,
					 int *errp, int flags);
static void		svc_delete_socket(struct svc_sock *svsk);
static void		svc_udp_data_ready(struct sock *, int);
static int		svc_udp_recvfrom(struct svc_rqst *);
static int		svc_udp_sendto(struct svc_rqst *);
static void		svc_close_socket(struct svc_sock *svsk);

static struct svc_deferred_req *svc_deferred_dequeue(struct svc_sock *svsk);
static int svc_deferred_recv(struct svc_rqst *rqstp);
static struct cache_deferred_req *svc_defer(struct cache_req *req);

/* apparently the "standard" is that clients close
 * idle connections after 5 minutes, servers after
 * 6 minutes
 *   http://www.connectathon.org/talks96/nfstcp.pdf
 */
static int svc_conn_age_period = 6*60;

#ifdef CONFIG_DEBUG_LOCK_ALLOC
static struct lock_class_key svc_key[2];
static struct lock_class_key svc_slock_key[2];

static inline void svc_reclassify_socket(struct socket *sock)
{
	struct sock *sk = sock->sk;
	BUG_ON(sk->sk_lock.owner != NULL);
	switch (sk->sk_family) {
	case AF_INET:
		sock_lock_init_class_and_name(sk, "slock-AF_INET-NFSD",
		    &svc_slock_key[0], "sk_lock-AF_INET-NFSD", &svc_key[0]);
		break;

	case AF_INET6:
		sock_lock_init_class_and_name(sk, "slock-AF_INET6-NFSD",
		    &svc_slock_key[1], "sk_lock-AF_INET6-NFSD", &svc_key[1]);
		break;

	default:
		BUG();
	}
}
#else
static inline void svc_reclassify_socket(struct socket *sock)
{
}
#endif

static char *__svc_print_addr(struct sockaddr *addr, char *buf, size_t len)
{
	switch (addr->sa_family) {
	case AF_INET:
		snprintf(buf, len, "%u.%u.%u.%u, port=%u",
			NIPQUAD(((struct sockaddr_in *) addr)->sin_addr),
			htons(((struct sockaddr_in *) addr)->sin_port));
		break;

	case AF_INET6:
		snprintf(buf, len, "%x:%x:%x:%x:%x:%x:%x:%x, port=%u",
			NIP6(((struct sockaddr_in6 *) addr)->sin6_addr),
			htons(((struct sockaddr_in6 *) addr)->sin6_port));
		break;

	default:
		snprintf(buf, len, "unknown address type: %d", addr->sa_family);
		break;
	}
	return buf;
}

/**
 * svc_print_addr - Format rq_addr field for printing
 * @rqstp: svc_rqst struct containing address to print
 * @buf: target buffer for formatted address
 * @len: length of target buffer
 *
 */
char *svc_print_addr(struct svc_rqst *rqstp, char *buf, size_t len)
{
	return __svc_print_addr(svc_addr(rqstp), buf, len);
}
EXPORT_SYMBOL_GPL(svc_print_addr);

/*
 * Queue up an idle server thread.  Must have pool->sp_lock held.
 * Note: this is really a stack rather than a queue, so that we only
 * use as many different threads as we need, and the rest don't pollute
 * the cache.
 */
static inline void
svc_thread_enqueue(struct svc_pool *pool, struct svc_rqst *rqstp)
{
	list_add(&rqstp->rq_list, &pool->sp_threads);
}

/*
 * Dequeue an nfsd thread.  Must have pool->sp_lock held.
 */
static inline void
svc_thread_dequeue(struct svc_pool *pool, struct svc_rqst *rqstp)
{
	list_del(&rqstp->rq_list);
}

/*
 * Release an skbuff after use
 */
static inline void
svc_release_skb(struct svc_rqst *rqstp)
{
	struct sk_buff *skb = rqstp->rq_skbuff;
	struct svc_deferred_req *dr = rqstp->rq_deferred;

	if (skb) {
		rqstp->rq_skbuff = NULL;

		dprintk("svc: service %p, releasing skb %p\n", rqstp, skb);
		skb_free_datagram(rqstp->rq_sock->sk_sk, skb);
	}
	if (dr) {
		rqstp->rq_deferred = NULL;
		kfree(dr);
	}
}

/*
 * Any space to write?
 */
static inline unsigned long
svc_sock_wspace(struct svc_sock *svsk)
{
	int wspace;

	if (svsk->sk_sock->type == SOCK_STREAM)
		wspace = sk_stream_wspace(svsk->sk_sk);
	else
		wspace = sock_wspace(svsk->sk_sk);

	return wspace;
}

/*
 * Queue up a socket with data pending. If there are idle nfsd
 * processes, wake 'em up.
 *
 */
static void
svc_sock_enqueue(struct svc_sock *svsk)
{
	struct svc_serv	*serv = svsk->sk_server;
	struct svc_pool *pool;
	struct svc_rqst	*rqstp;
	int cpu;

	if (!(svsk->sk_flags &
	      ( (1<<SK_CONN)|(1<<SK_DATA)|(1<<SK_CLOSE)|(1<<SK_DEFERRED)) ))
		return;
	if (test_bit(SK_DEAD, &svsk->sk_flags))
		return;

	cpu = get_cpu();
	pool = svc_pool_for_cpu(svsk->sk_server, cpu);
	put_cpu();

	spin_lock_bh(&pool->sp_lock);

	if (!list_empty(&pool->sp_threads) &&
	    !list_empty(&pool->sp_sockets))
		printk(KERN_ERR
			"svc_sock_enqueue: threads and sockets both waiting??\n");

	if (test_bit(SK_DEAD, &svsk->sk_flags)) {
		/* Don't enqueue dead sockets */
		dprintk("svc: socket %p is dead, not enqueued\n", svsk->sk_sk);
		goto out_unlock;
	}

	/* Mark socket as busy. It will remain in this state until the
	 * server has processed all pending data and put the socket back
	 * on the idle list.  We update SK_BUSY atomically because
	 * it also guards against trying to enqueue the svc_sock twice.
	 */
	if (test_and_set_bit(SK_BUSY, &svsk->sk_flags)) {
		/* Don't enqueue socket while already enqueued */
		dprintk("svc: socket %p busy, not enqueued\n", svsk->sk_sk);
		goto out_unlock;
	}
	BUG_ON(svsk->sk_pool != NULL);
	svsk->sk_pool = pool;

	set_bit(SOCK_NOSPACE, &svsk->sk_sock->flags);
	if (((atomic_read(&svsk->sk_reserved) + serv->sv_max_mesg)*2
	     > svc_sock_wspace(svsk))
	    && !test_bit(SK_CLOSE, &svsk->sk_flags)
	    && !test_bit(SK_CONN, &svsk->sk_flags)) {
		/* Don't enqueue while not enough space for reply */
		dprintk("svc: socket %p  no space, %d*2 > %ld, not enqueued\n",
			svsk->sk_sk, atomic_read(&svsk->sk_reserved)+serv->sv_max_mesg,
			svc_sock_wspace(svsk));
		svsk->sk_pool = NULL;
		clear_bit(SK_BUSY, &svsk->sk_flags);
		goto out_unlock;
	}
	clear_bit(SOCK_NOSPACE, &svsk->sk_sock->flags);


	if (!list_empty(&pool->sp_threads)) {
		rqstp = list_entry(pool->sp_threads.next,
				   struct svc_rqst,
				   rq_list);
		dprintk("svc: socket %p served by daemon %p\n",
			svsk->sk_sk, rqstp);
		svc_thread_dequeue(pool, rqstp);
		if (rqstp->rq_sock)
			printk(KERN_ERR
				"svc_sock_enqueue: server %p, rq_sock=%p!\n",
				rqstp, rqstp->rq_sock);
		rqstp->rq_sock = svsk;
		atomic_inc(&svsk->sk_inuse);
		rqstp->rq_reserved = serv->sv_max_mesg;
		atomic_add(rqstp->rq_reserved, &svsk->sk_reserved);
		BUG_ON(svsk->sk_pool != pool);
		wake_up(&rqstp->rq_wait);
	} else {
		dprintk("svc: socket %p put into queue\n", svsk->sk_sk);
		list_add_tail(&svsk->sk_ready, &pool->sp_sockets);
		BUG_ON(svsk->sk_pool != pool);
	}

out_unlock:
	spin_unlock_bh(&pool->sp_lock);
}

/*
 * Dequeue the first socket.  Must be called with the pool->sp_lock held.
 */
static inline struct svc_sock *
svc_sock_dequeue(struct svc_pool *pool)
{
	struct svc_sock	*svsk;

	if (list_empty(&pool->sp_sockets))
		return NULL;

	svsk = list_entry(pool->sp_sockets.next,
			  struct svc_sock, sk_ready);
	list_del_init(&svsk->sk_ready);

	dprintk("svc: socket %p dequeued, inuse=%d\n",
		svsk->sk_sk, atomic_read(&svsk->sk_inuse));

	return svsk;
}

/*
 * Having read something from a socket, check whether it
 * needs to be re-enqueued.
 * Note: SK_DATA only gets cleared when a read-attempt finds
 * no (or insufficient) data.
 */
static inline void
svc_sock_received(struct svc_sock *svsk)
{
	svsk->sk_pool = NULL;
	clear_bit(SK_BUSY, &svsk->sk_flags);
	svc_sock_enqueue(svsk);
}


/**
 * svc_reserve - change the space reserved for the reply to a request.
 * @rqstp:  The request in question
 * @space: new max space to reserve
 *
 * Each request reserves some space on the output queue of the socket
 * to make sure the reply fits.  This function reduces that reserved
 * space to be the amount of space used already, plus @space.
 *
 */
void svc_reserve(struct svc_rqst *rqstp, int space)
{
	space += rqstp->rq_res.head[0].iov_len;

	if (space < rqstp->rq_reserved) {
		struct svc_sock *svsk = rqstp->rq_sock;
		atomic_sub((rqstp->rq_reserved - space), &svsk->sk_reserved);
		rqstp->rq_reserved = space;

		svc_sock_enqueue(svsk);
	}
}

/*
 * Release a socket after use.
 */
static inline void
svc_sock_put(struct svc_sock *svsk)
{
	if (atomic_dec_and_test(&svsk->sk_inuse)) {
		BUG_ON(! test_bit(SK_DEAD, &svsk->sk_flags));

		dprintk("svc: releasing dead socket\n");
		if (svsk->sk_sock->file)
			sockfd_put(svsk->sk_sock);
		else
			sock_release(svsk->sk_sock);
		if (svsk->sk_info_authunix != NULL)
			svcauth_unix_info_release(svsk->sk_info_authunix);
		kfree(svsk);
	}
}

static void
svc_sock_release(struct svc_rqst *rqstp)
{
	struct svc_sock	*svsk = rqstp->rq_sock;

	svc_release_skb(rqstp);

	svc_free_res_pages(rqstp);
	rqstp->rq_res.page_len = 0;
	rqstp->rq_res.page_base = 0;


	/* Reset response buffer and release
	 * the reservation.
	 * But first, check that enough space was reserved
	 * for the reply, otherwise we have a bug!
	 */
	if ((rqstp->rq_res.len) >  rqstp->rq_reserved)
		printk(KERN_ERR "RPC request reserved %d but used %d\n",
		       rqstp->rq_reserved,
		       rqstp->rq_res.len);

	rqstp->rq_res.head[0].iov_len = 0;
	svc_reserve(rqstp, 0);
	rqstp->rq_sock = NULL;

	svc_sock_put(svsk);
}

/*
 * External function to wake up a server waiting for data
 * This really only makes sense for services like lockd
 * which have exactly one thread anyway.
 */
void
svc_wake_up(struct svc_serv *serv)
{
	struct svc_rqst	*rqstp;
	unsigned int i;
	struct svc_pool *pool;

	for (i = 0; i < serv->sv_nrpools; i++) {
		pool = &serv->sv_pools[i];

		spin_lock_bh(&pool->sp_lock);
		if (!list_empty(&pool->sp_threads)) {
			rqstp = list_entry(pool->sp_threads.next,
					   struct svc_rqst,
					   rq_list);
			dprintk("svc: daemon %p woken up.\n", rqstp);
			/*
			svc_thread_dequeue(pool, rqstp);
			rqstp->rq_sock = NULL;
			 */
			wake_up(&rqstp->rq_wait);
		}
		spin_unlock_bh(&pool->sp_lock);
	}
}

union svc_pktinfo_u {
	struct in_pktinfo pkti;
	struct in6_pktinfo pkti6;
};
#define SVC_PKTINFO_SPACE \
	CMSG_SPACE(sizeof(union svc_pktinfo_u))

static void svc_set_cmsg_data(struct svc_rqst *rqstp, struct cmsghdr *cmh)
{
	switch (rqstp->rq_sock->sk_sk->sk_family) {
	case AF_INET: {
			struct in_pktinfo *pki = CMSG_DATA(cmh);

			cmh->cmsg_level = SOL_IP;
			cmh->cmsg_type = IP_PKTINFO;
			pki->ipi_ifindex = 0;
			pki->ipi_spec_dst.s_addr = rqstp->rq_daddr.addr.s_addr;
			cmh->cmsg_len = CMSG_LEN(sizeof(*pki));
		}
		break;

	case AF_INET6: {
			struct in6_pktinfo *pki = CMSG_DATA(cmh);

			cmh->cmsg_level = SOL_IPV6;
			cmh->cmsg_type = IPV6_PKTINFO;
			pki->ipi6_ifindex = 0;
			ipv6_addr_copy(&pki->ipi6_addr,
					&rqstp->rq_daddr.addr6);
			cmh->cmsg_len = CMSG_LEN(sizeof(*pki));
		}
		break;
	}
	return;
}

/*
 * Generic sendto routine
 */
static int
svc_sendto(struct svc_rqst *rqstp, struct xdr_buf *xdr)
{
	struct svc_sock	*svsk = rqstp->rq_sock;
	struct socket	*sock = svsk->sk_sock;
	int		slen;
	union {
		struct cmsghdr	hdr;
		long		all[SVC_PKTINFO_SPACE / sizeof(long)];
	} buffer;
	struct cmsghdr *cmh = &buffer.hdr;
	int		len = 0;
	int		result;
	int		size;
	struct page	**ppage = xdr->pages;
	size_t		base = xdr->page_base;
	unsigned int	pglen = xdr->page_len;
	unsigned int	flags = MSG_MORE;
	char		buf[RPC_MAX_ADDRBUFLEN];

	slen = xdr->len;

	if (rqstp->rq_prot == IPPROTO_UDP) {
		struct msghdr msg = {
			.msg_name	= &rqstp->rq_addr,
			.msg_namelen	= rqstp->rq_addrlen,
			.msg_control	= cmh,
			.msg_controllen	= sizeof(buffer),
			.msg_flags	= MSG_MORE,
		};

		svc_set_cmsg_data(rqstp, cmh);

		if (sock_sendmsg(sock, &msg, 0) < 0)
			goto out;
	}

	/* send head */
	if (slen == xdr->head[0].iov_len)
		flags = 0;
	len = kernel_sendpage(sock, rqstp->rq_respages[0], 0,
				  xdr->head[0].iov_len, flags);
	if (len != xdr->head[0].iov_len)
		goto out;
	slen -= xdr->head[0].iov_len;
	if (slen == 0)
		goto out;

	/* send page data */
	size = PAGE_SIZE - base < pglen ? PAGE_SIZE - base : pglen;
	while (pglen > 0) {
		if (slen == size)
			flags = 0;
		result = kernel_sendpage(sock, *ppage, base, size, flags);
		if (result > 0)
			len += result;
		if (result != size)
			goto out;
		slen -= size;
		pglen -= size;
		size = PAGE_SIZE < pglen ? PAGE_SIZE : pglen;
		base = 0;
		ppage++;
	}
	/* send tail */
	if (xdr->tail[0].iov_len) {
		result = kernel_sendpage(sock, rqstp->rq_respages[0],
					     ((unsigned long)xdr->tail[0].iov_base)
						& (PAGE_SIZE-1),
					     xdr->tail[0].iov_len, 0);

		if (result > 0)
			len += result;
	}
out:
	dprintk("svc: socket %p sendto([%p %Zu... ], %d) = %d (addr %s)\n",
		rqstp->rq_sock, xdr->head[0].iov_base, xdr->head[0].iov_len,
		xdr->len, len, svc_print_addr(rqstp, buf, sizeof(buf)));

	return len;
}

/*
 * Report socket names for nfsdfs
 */
static int one_sock_name(char *buf, struct svc_sock *svsk)
{
	int len;

	switch(svsk->sk_sk->sk_family) {
	case AF_INET:
		len = sprintf(buf, "ipv4 %s %u.%u.%u.%u %d\n",
			      svsk->sk_sk->sk_protocol==IPPROTO_UDP?
			      "udp" : "tcp",
			      NIPQUAD(inet_sk(svsk->sk_sk)->rcv_saddr),
			      inet_sk(svsk->sk_sk)->num);
		break;
	default:
		len = sprintf(buf, "*unknown-%d*\n",
			       svsk->sk_sk->sk_family);
	}
	return len;
}

int
svc_sock_names(char *buf, struct svc_serv *serv, char *toclose)
{
	struct svc_sock *svsk, *closesk = NULL;
	int len = 0;

	if (!serv)
		return 0;
	spin_lock_bh(&serv->sv_lock);
	list_for_each_entry(svsk, &serv->sv_permsocks, sk_list) {
		int onelen = one_sock_name(buf+len, svsk);
		if (toclose && strcmp(toclose, buf+len) == 0)
			closesk = svsk;
		else
			len += onelen;
	}
	spin_unlock_bh(&serv->sv_lock);
	if (closesk)
		/* Should unregister with portmap, but you cannot
		 * unregister just one protocol...
		 */
		svc_close_socket(closesk);
	else if (toclose)
		return -ENOENT;
	return len;
}
EXPORT_SYMBOL(svc_sock_names);

/*
 * Check input queue length
 */
static int
svc_recv_available(struct svc_sock *svsk)
{
	struct socket	*sock = svsk->sk_sock;
	int		avail, err;

	err = kernel_sock_ioctl(sock, TIOCINQ, (unsigned long) &avail);

	return (err >= 0)? avail : err;
}

/*
 * Generic recvfrom routine.
 */
static int
svc_recvfrom(struct svc_rqst *rqstp, struct kvec *iov, int nr, int buflen)
{
	struct svc_sock *svsk = rqstp->rq_sock;
	struct msghdr msg = {
		.msg_flags	= MSG_DONTWAIT,
	};
	int len;

	len = kernel_recvmsg(svsk->sk_sock, &msg, iov, nr, buflen,
				msg.msg_flags);

	/* sock_recvmsg doesn't fill in the name/namelen, so we must..
	 */
	memcpy(&rqstp->rq_addr, &svsk->sk_remote, svsk->sk_remotelen);
	rqstp->rq_addrlen = svsk->sk_remotelen;

	dprintk("svc: socket %p recvfrom(%p, %Zu) = %d\n",
		svsk, iov[0].iov_base, iov[0].iov_len, len);

	return len;
}

/*
 * Set socket snd and rcv buffer lengths
 */
static inline void
svc_sock_setbufsize(struct socket *sock, unsigned int snd, unsigned int rcv)
{
#if 0
	mm_segment_t	oldfs;
	oldfs = get_fs(); set_fs(KERNEL_DS);
	sock_setsockopt(sock, SOL_SOCKET, SO_SNDBUF,
			(char*)&snd, sizeof(snd));
	sock_setsockopt(sock, SOL_SOCKET, SO_RCVBUF,
			(char*)&rcv, sizeof(rcv));
#else
	/* sock_setsockopt limits use to sysctl_?mem_max,
	 * which isn't acceptable.  Until that is made conditional
	 * on not having CAP_SYS_RESOURCE or similar, we go direct...
	 * DaveM said I could!
	 */
	lock_sock(sock->sk);
	sock->sk->sk_sndbuf = snd * 2;
	sock->sk->sk_rcvbuf = rcv * 2;
	sock->sk->sk_userlocks |= SOCK_SNDBUF_LOCK|SOCK_RCVBUF_LOCK;
	release_sock(sock->sk);
#endif
}
/*
 * INET callback when data has been received on the socket.
 */
static void
svc_udp_data_ready(struct sock *sk, int count)
{
	struct svc_sock	*svsk = (struct svc_sock *)sk->sk_user_data;

	if (svsk) {
		dprintk("svc: socket %p(inet %p), count=%d, busy=%d\n",
			svsk, sk, count, test_bit(SK_BUSY, &svsk->sk_flags));
		set_bit(SK_DATA, &svsk->sk_flags);
		svc_sock_enqueue(svsk);
	}
	if (sk->sk_sleep && waitqueue_active(sk->sk_sleep))
		wake_up_interruptible(sk->sk_sleep);
}

/*
 * INET callback when space is newly available on the socket.
 */
static void
svc_write_space(struct sock *sk)
{
	struct svc_sock	*svsk = (struct svc_sock *)(sk->sk_user_data);

	if (svsk) {
		dprintk("svc: socket %p(inet %p), write_space busy=%d\n",
			svsk, sk, test_bit(SK_BUSY, &svsk->sk_flags));
		svc_sock_enqueue(svsk);
	}

	if (sk->sk_sleep && waitqueue_active(sk->sk_sleep)) {
		dprintk("RPC svc_write_space: someone sleeping on %p\n",
		       svsk);
		wake_up_interruptible(sk->sk_sleep);
	}
}

static inline void svc_udp_get_dest_address(struct svc_rqst *rqstp,
					    struct cmsghdr *cmh)
{
	switch (rqstp->rq_sock->sk_sk->sk_family) {
	case AF_INET: {
		struct in_pktinfo *pki = CMSG_DATA(cmh);
		rqstp->rq_daddr.addr.s_addr = pki->ipi_spec_dst.s_addr;
		break;
		}
	case AF_INET6: {
		struct in6_pktinfo *pki = CMSG_DATA(cmh);
		ipv6_addr_copy(&rqstp->rq_daddr.addr6, &pki->ipi6_addr);
		break;
		}
	}
}

/*
 * Receive a datagram from a UDP socket.
 */
static int
svc_udp_recvfrom(struct svc_rqst *rqstp)
{
	struct svc_sock	*svsk = rqstp->rq_sock;
	struct svc_serv	*serv = svsk->sk_server;
	struct sk_buff	*skb;
	union {
		struct cmsghdr	hdr;
		long		all[SVC_PKTINFO_SPACE / sizeof(long)];
	} buffer;
	struct cmsghdr *cmh = &buffer.hdr;
	int		err, len;
	struct msghdr msg = {
		.msg_name = svc_addr(rqstp),
		.msg_control = cmh,
		.msg_controllen = sizeof(buffer),
		.msg_flags = MSG_DONTWAIT,
	};

	if (test_and_clear_bit(SK_CHNGBUF, &svsk->sk_flags))
	    /* udp sockets need large rcvbuf as all pending
	     * requests are still in that buffer.  sndbuf must
	     * also be large enough that there is enough space
	     * for one reply per thread.  We count all threads
	     * rather than threads in a particular pool, which
	     * provides an upper bound on the number of threads
	     * which will access the socket.
	     */
	    svc_sock_setbufsize(svsk->sk_sock,
				(serv->sv_nrthreads+3) * serv->sv_max_mesg,
				(serv->sv_nrthreads+3) * serv->sv_max_mesg);

	if ((rqstp->rq_deferred = svc_deferred_dequeue(svsk))) {
		svc_sock_received(svsk);
		return svc_deferred_recv(rqstp);
	}

	if (test_bit(SK_CLOSE, &svsk->sk_flags)) {
		svc_delete_socket(svsk);
		return 0;
	}

	clear_bit(SK_DATA, &svsk->sk_flags);
	while ((err = kernel_recvmsg(svsk->sk_sock, &msg, NULL,
				     0, 0, MSG_PEEK | MSG_DONTWAIT)) < 0 ||
	       (skb = skb_recv_datagram(svsk->sk_sk, 0, 1, &err)) == NULL) {
		if (err == -EAGAIN) {
			svc_sock_received(svsk);
			return err;
		}
		/* possibly an icmp error */
		dprintk("svc: recvfrom returned error %d\n", -err);
	}
	rqstp->rq_addrlen = sizeof(rqstp->rq_addr);
	if (skb->tstamp.off_sec == 0) {
		struct timeval tv;

		tv.tv_sec = xtime.tv_sec;
		tv.tv_usec = xtime.tv_nsec / NSEC_PER_USEC;
		skb_set_timestamp(skb, &tv);
		/* Don't enable netstamp, sunrpc doesn't
		   need that much accuracy */
	}
	skb_get_timestamp(skb, &svsk->sk_sk->sk_stamp);
	set_bit(SK_DATA, &svsk->sk_flags); /* there may be more data... */

	/*
	 * Maybe more packets - kick another thread ASAP.
	 */
	svc_sock_received(svsk);

	len  = skb->len - sizeof(struct udphdr);
	rqstp->rq_arg.len = len;

	rqstp->rq_prot = IPPROTO_UDP;

	if (cmh->cmsg_level != IPPROTO_IP ||
	    cmh->cmsg_type != IP_PKTINFO) {
		if (net_ratelimit())
			printk("rpcsvc: received unknown control message:"
			       "%d/%d\n",
			       cmh->cmsg_level, cmh->cmsg_type);
		skb_free_datagram(svsk->sk_sk, skb);
		return 0;
	}
	svc_udp_get_dest_address(rqstp, cmh);

	if (skb_is_nonlinear(skb)) {
		/* we have to copy */
		local_bh_disable();
		if (csum_partial_copy_to_xdr(&rqstp->rq_arg, skb)) {
			local_bh_enable();
			/* checksum error */
			skb_free_datagram(svsk->sk_sk, skb);
			return 0;
		}
		local_bh_enable();
		skb_free_datagram(svsk->sk_sk, skb);
	} else {
		/* we can use it in-place */
		rqstp->rq_arg.head[0].iov_base = skb->data + sizeof(struct udphdr);
		rqstp->rq_arg.head[0].iov_len = len;
		if (skb_checksum_complete(skb)) {
			skb_free_datagram(svsk->sk_sk, skb);
			return 0;
		}
		rqstp->rq_skbuff = skb;
	}

	rqstp->rq_arg.page_base = 0;
	if (len <= rqstp->rq_arg.head[0].iov_len) {
		rqstp->rq_arg.head[0].iov_len = len;
		rqstp->rq_arg.page_len = 0;
		rqstp->rq_respages = rqstp->rq_pages+1;
	} else {
		rqstp->rq_arg.page_len = len - rqstp->rq_arg.head[0].iov_len;
		rqstp->rq_respages = rqstp->rq_pages + 1 +
			(rqstp->rq_arg.page_len + PAGE_SIZE - 1)/ PAGE_SIZE;
	}

	if (serv->sv_stats)
		serv->sv_stats->netudpcnt++;

	return len;
}

static int
svc_udp_sendto(struct svc_rqst *rqstp)
{
	int		error;

	error = svc_sendto(rqstp, &rqstp->rq_res);
	if (error == -ECONNREFUSED)
		/* ICMP error on earlier request. */
		error = svc_sendto(rqstp, &rqstp->rq_res);

	return error;
}

static void
svc_udp_init(struct svc_sock *svsk)
{
	int one = 1;
	mm_segment_t oldfs;

	svsk->sk_sk->sk_data_ready = svc_udp_data_ready;
	svsk->sk_sk->sk_write_space = svc_write_space;
	svsk->sk_recvfrom = svc_udp_recvfrom;
	svsk->sk_sendto = svc_udp_sendto;

	/* initialise setting must have enough space to
	 * receive and respond to one request.
	 * svc_udp_recvfrom will re-adjust if necessary
	 */
	svc_sock_setbufsize(svsk->sk_sock,
			    3 * svsk->sk_server->sv_max_mesg,
			    3 * svsk->sk_server->sv_max_mesg);

	set_bit(SK_DATA, &svsk->sk_flags); /* might have come in before data_ready set up */
	set_bit(SK_CHNGBUF, &svsk->sk_flags);

	oldfs = get_fs();
	set_fs(KERNEL_DS);
	/* make sure we get destination address info */
	svsk->sk_sock->ops->setsockopt(svsk->sk_sock, IPPROTO_IP, IP_PKTINFO,
				       (char __user *)&one, sizeof(one));
	set_fs(oldfs);
}

/*
 * A data_ready event on a listening socket means there's a connection
 * pending. Do not use state_change as a substitute for it.
 */
static void
svc_tcp_listen_data_ready(struct sock *sk, int count_unused)
{
	struct svc_sock	*svsk = (struct svc_sock *)sk->sk_user_data;

	dprintk("svc: socket %p TCP (listen) state change %d\n",
		sk, sk->sk_state);

	/*
	 * This callback may called twice when a new connection
	 * is established as a child socket inherits everything
	 * from a parent LISTEN socket.
	 * 1) data_ready method of the parent socket will be called
	 *    when one of child sockets become ESTABLISHED.
	 * 2) data_ready method of the child socket may be called
	 *    when it receives data before the socket is accepted.
	 * In case of 2, we should ignore it silently.
	 */
	if (sk->sk_state == TCP_LISTEN) {
		if (svsk) {
			set_bit(SK_CONN, &svsk->sk_flags);
			svc_sock_enqueue(svsk);
		} else
			printk("svc: socket %p: no user data\n", sk);
	}

	if (sk->sk_sleep && waitqueue_active(sk->sk_sleep))
		wake_up_interruptible_all(sk->sk_sleep);
}

/*
 * A state change on a connected socket means it's dying or dead.
 */
static void
svc_tcp_state_change(struct sock *sk)
{
	struct svc_sock	*svsk = (struct svc_sock *)sk->sk_user_data;

	dprintk("svc: socket %p TCP (connected) state change %d (svsk %p)\n",
		sk, sk->sk_state, sk->sk_user_data);

	if (!svsk)
		printk("svc: socket %p: no user data\n", sk);
	else {
		set_bit(SK_CLOSE, &svsk->sk_flags);
		svc_sock_enqueue(svsk);
	}
	if (sk->sk_sleep && waitqueue_active(sk->sk_sleep))
		wake_up_interruptible_all(sk->sk_sleep);
}

static void
svc_tcp_data_ready(struct sock *sk, int count)
{
	struct svc_sock *svsk = (struct svc_sock *)sk->sk_user_data;

	dprintk("svc: socket %p TCP data ready (svsk %p)\n",
		sk, sk->sk_user_data);
	if (svsk) {
		set_bit(SK_DATA, &svsk->sk_flags);
		svc_sock_enqueue(svsk);
	}
	if (sk->sk_sleep && waitqueue_active(sk->sk_sleep))
		wake_up_interruptible(sk->sk_sleep);
}

static inline int svc_port_is_privileged(struct sockaddr *sin)
{
	switch (sin->sa_family) {
	case AF_INET:
		return ntohs(((struct sockaddr_in *)sin)->sin_port)
			< PROT_SOCK;
	case AF_INET6:
		return ntohs(((struct sockaddr_in6 *)sin)->sin6_port)
			< PROT_SOCK;
	default:
		return 0;
	}
}

/*
 * Accept a TCP connection
 */
static void
svc_tcp_accept(struct svc_sock *svsk)
{
	struct sockaddr_storage addr;
	struct sockaddr	*sin = (struct sockaddr *) &addr;
	struct svc_serv	*serv = svsk->sk_server;
	struct socket	*sock = svsk->sk_sock;
	struct socket	*newsock;
	struct svc_sock	*newsvsk;
	int		err, slen;
	char		buf[RPC_MAX_ADDRBUFLEN];

	dprintk("svc: tcp_accept %p sock %p\n", svsk, sock);
	if (!sock)
		return;

	clear_bit(SK_CONN, &svsk->sk_flags);
	err = kernel_accept(sock, &newsock, O_NONBLOCK);
	if (err < 0) {
		if (err == -ENOMEM)
			printk(KERN_WARNING "%s: no more sockets!\n",
			       serv->sv_name);
		else if (err != -EAGAIN && net_ratelimit())
			printk(KERN_WARNING "%s: accept failed (err %d)!\n",
				   serv->sv_name, -err);
		return;
	}

	set_bit(SK_CONN, &svsk->sk_flags);
	svc_sock_enqueue(svsk);

	err = kernel_getpeername(newsock, sin, &slen);
	if (err < 0) {
		if (net_ratelimit())
			printk(KERN_WARNING "%s: peername failed (err %d)!\n",
				   serv->sv_name, -err);
		goto failed;		/* aborted connection or whatever */
	}

	/* Ideally, we would want to reject connections from unauthorized
	 * hosts here, but when we get encryption, the IP of the host won't
	 * tell us anything.  For now just warn about unpriv connections.
	 */
	if (!svc_port_is_privileged(sin)) {
		dprintk(KERN_WARNING
			"%s: connect from unprivileged port: %s\n",
			serv->sv_name,
			__svc_print_addr(sin, buf, sizeof(buf)));
	}
	dprintk("%s: connect from %s\n", serv->sv_name,
		__svc_print_addr(sin, buf, sizeof(buf)));

	/* make sure that a write doesn't block forever when
	 * low on memory
	 */
	newsock->sk->sk_sndtimeo = HZ*30;

	if (!(newsvsk = svc_setup_socket(serv, newsock, &err,
				 (SVC_SOCK_ANONYMOUS | SVC_SOCK_TEMPORARY))))
		goto failed;
	memcpy(&newsvsk->sk_remote, sin, slen);
	newsvsk->sk_remotelen = slen;

	svc_sock_received(newsvsk);

	/* make sure that we don't have too many active connections.
	 * If we have, something must be dropped.
	 *
	 * There's no point in trying to do random drop here for
	 * DoS prevention. The NFS clients does 1 reconnect in 15
	 * seconds. An attacker can easily beat that.
	 *
	 * The only somewhat efficient mechanism would be if drop
	 * old connections from the same IP first. But right now
	 * we don't even record the client IP in svc_sock.
	 */
	if (serv->sv_tmpcnt > (serv->sv_nrthreads+3)*20) {
		struct svc_sock *svsk = NULL;
		spin_lock_bh(&serv->sv_lock);
		if (!list_empty(&serv->sv_tempsocks)) {
			if (net_ratelimit()) {
				/* Try to help the admin */
				printk(KERN_NOTICE "%s: too many open TCP "
					"sockets, consider increasing the "
					"number of nfsd threads\n",
						   serv->sv_name);
				printk(KERN_NOTICE
				       "%s: last TCP connect from %s\n",
				       serv->sv_name, buf);
			}
			/*
			 * Always select the oldest socket. It's not fair,
			 * but so is life
			 */
			svsk = list_entry(serv->sv_tempsocks.prev,
					  struct svc_sock,
					  sk_list);
			set_bit(SK_CLOSE, &svsk->sk_flags);
			atomic_inc(&svsk->sk_inuse);
		}
		spin_unlock_bh(&serv->sv_lock);

		if (svsk) {
			svc_sock_enqueue(svsk);
			svc_sock_put(svsk);
		}

	}

	if (serv->sv_stats)
		serv->sv_stats->nettcpconn++;

	return;

failed:
	sock_release(newsock);
	return;
}

/*
 * Receive data from a TCP socket.
 */
static int
svc_tcp_recvfrom(struct svc_rqst *rqstp)
{
	struct svc_sock	*svsk = rqstp->rq_sock;
	struct svc_serv	*serv = svsk->sk_server;
	int		len;
	struct kvec *vec;
	int pnum, vlen;

	dprintk("svc: tcp_recv %p data %d conn %d close %d\n",
		svsk, test_bit(SK_DATA, &svsk->sk_flags),
		test_bit(SK_CONN, &svsk->sk_flags),
		test_bit(SK_CLOSE, &svsk->sk_flags));

	if ((rqstp->rq_deferred = svc_deferred_dequeue(svsk))) {
		svc_sock_received(svsk);
		return svc_deferred_recv(rqstp);
	}

	if (test_bit(SK_CLOSE, &svsk->sk_flags)) {
		svc_delete_socket(svsk);
		return 0;
	}

	if (svsk->sk_sk->sk_state == TCP_LISTEN) {
		svc_tcp_accept(svsk);
		svc_sock_received(svsk);
		return 0;
	}

	if (test_and_clear_bit(SK_CHNGBUF, &svsk->sk_flags))
		/* sndbuf needs to have room for one request
		 * per thread, otherwise we can stall even when the
		 * network isn't a bottleneck.
		 *
		 * We count all threads rather than threads in a
		 * particular pool, which provides an upper bound
		 * on the number of threads which will access the socket.
		 *
		 * rcvbuf just needs to be able to hold a few requests.
		 * Normally they will be removed from the queue
		 * as soon a a complete request arrives.
		 */
		svc_sock_setbufsize(svsk->sk_sock,
				    (serv->sv_nrthreads+3) * serv->sv_max_mesg,
				    3 * serv->sv_max_mesg);

	clear_bit(SK_DATA, &svsk->sk_flags);

	/* Receive data. If we haven't got the record length yet, get
	 * the next four bytes. Otherwise try to gobble up as much as
	 * possible up to the complete record length.
	 */
	if (svsk->sk_tcplen < 4) {
		unsigned long	want = 4 - svsk->sk_tcplen;
		struct kvec	iov;

		iov.iov_base = ((char *) &svsk->sk_reclen) + svsk->sk_tcplen;
		iov.iov_len  = want;
		if ((len = svc_recvfrom(rqstp, &iov, 1, want)) < 0)
			goto error;
		svsk->sk_tcplen += len;

		if (len < want) {
			dprintk("svc: short recvfrom while reading record length (%d of %lu)\n",
				len, want);
			svc_sock_received(svsk);
			return -EAGAIN; /* record header not complete */
		}

		svsk->sk_reclen = ntohl(svsk->sk_reclen);
		if (!(svsk->sk_reclen & 0x80000000)) {
			/* FIXME: technically, a record can be fragmented,
			 *  and non-terminal fragments will not have the top
			 *  bit set in the fragment length header.
			 *  But apparently no known nfs clients send fragmented
			 *  records. */
			if (net_ratelimit())
				printk(KERN_NOTICE "RPC: bad TCP reclen 0x%08lx"
				       " (non-terminal)\n",
				       (unsigned long) svsk->sk_reclen);
			goto err_delete;
		}
		svsk->sk_reclen &= 0x7fffffff;
		dprintk("svc: TCP record, %d bytes\n", svsk->sk_reclen);
		if (svsk->sk_reclen > serv->sv_max_mesg) {
			if (net_ratelimit())
				printk(KERN_NOTICE "RPC: bad TCP reclen 0x%08lx"
				       " (large)\n",
				       (unsigned long) svsk->sk_reclen);
			goto err_delete;
		}
	}

	/* Check whether enough data is available */
	len = svc_recv_available(svsk);
	if (len < 0)
		goto error;

	if (len < svsk->sk_reclen) {
		dprintk("svc: incomplete TCP record (%d of %d)\n",
			len, svsk->sk_reclen);
		svc_sock_received(svsk);
		return -EAGAIN;	/* record not complete */
	}
	len = svsk->sk_reclen;
	set_bit(SK_DATA, &svsk->sk_flags);

	vec = rqstp->rq_vec;
	vec[0] = rqstp->rq_arg.head[0];
	vlen = PAGE_SIZE;
	pnum = 1;
	while (vlen < len) {
		vec[pnum].iov_base = page_address(rqstp->rq_pages[pnum]);
		vec[pnum].iov_len = PAGE_SIZE;
		pnum++;
		vlen += PAGE_SIZE;
	}
	rqstp->rq_respages = &rqstp->rq_pages[pnum];

	/* Now receive data */
	len = svc_recvfrom(rqstp, vec, pnum, len);
	if (len < 0)
		goto error;

	dprintk("svc: TCP complete record (%d bytes)\n", len);
	rqstp->rq_arg.len = len;
	rqstp->rq_arg.page_base = 0;
	if (len <= rqstp->rq_arg.head[0].iov_len) {
		rqstp->rq_arg.head[0].iov_len = len;
		rqstp->rq_arg.page_len = 0;
	} else {
		rqstp->rq_arg.page_len = len - rqstp->rq_arg.head[0].iov_len;
	}

	rqstp->rq_skbuff      = NULL;
	rqstp->rq_prot	      = IPPROTO_TCP;

	/* Reset TCP read info */
	svsk->sk_reclen = 0;
	svsk->sk_tcplen = 0;

	svc_sock_received(svsk);
	if (serv->sv_stats)
		serv->sv_stats->nettcpcnt++;

	return len;

 err_delete:
	svc_delete_socket(svsk);
	return -EAGAIN;

 error:
	if (len == -EAGAIN) {
		dprintk("RPC: TCP recvfrom got EAGAIN\n");
		svc_sock_received(svsk);
	} else {
		printk(KERN_NOTICE "%s: recvfrom returned errno %d\n",
					svsk->sk_server->sv_name, -len);
		goto err_delete;
	}

	return len;
}

/*
 * Send out data on TCP socket.
 */
static int
svc_tcp_sendto(struct svc_rqst *rqstp)
{
	struct xdr_buf	*xbufp = &rqstp->rq_res;
	int sent;
	__be32 reclen;

	/* Set up the first element of the reply kvec.
	 * Any other kvecs that may be in use have been taken
	 * care of by the server implementation itself.
	 */
	reclen = htonl(0x80000000|((xbufp->len ) - 4));
	memcpy(xbufp->head[0].iov_base, &reclen, 4);

	if (test_bit(SK_DEAD, &rqstp->rq_sock->sk_flags))
		return -ENOTCONN;

	sent = svc_sendto(rqstp, &rqstp->rq_res);
	if (sent != xbufp->len) {
		printk(KERN_NOTICE "rpc-srv/tcp: %s: %s %d when sending %d bytes - shutting down socket\n",
		       rqstp->rq_sock->sk_server->sv_name,
		       (sent<0)?"got error":"sent only",
		       sent, xbufp->len);
		set_bit(SK_CLOSE, &rqstp->rq_sock->sk_flags);
		svc_sock_enqueue(rqstp->rq_sock);
		sent = -EAGAIN;
	}
	return sent;
}

static void
svc_tcp_init(struct svc_sock *svsk)
{
	struct sock	*sk = svsk->sk_sk;
	struct tcp_sock *tp = tcp_sk(sk);

	svsk->sk_recvfrom = svc_tcp_recvfrom;
	svsk->sk_sendto = svc_tcp_sendto;

	if (sk->sk_state == TCP_LISTEN) {
		dprintk("setting up TCP socket for listening\n");
		sk->sk_data_ready = svc_tcp_listen_data_ready;
		set_bit(SK_CONN, &svsk->sk_flags);
	} else {
		dprintk("setting up TCP socket for reading\n");
		sk->sk_state_change = svc_tcp_state_change;
		sk->sk_data_ready = svc_tcp_data_ready;
		sk->sk_write_space = svc_write_space;

		svsk->sk_reclen = 0;
		svsk->sk_tcplen = 0;

		tp->nonagle = 1;        /* disable Nagle's algorithm */

		/* initialise setting must have enough space to
		 * receive and respond to one request.
		 * svc_tcp_recvfrom will re-adjust if necessary
		 */
		svc_sock_setbufsize(svsk->sk_sock,
				    3 * svsk->sk_server->sv_max_mesg,
				    3 * svsk->sk_server->sv_max_mesg);

		set_bit(SK_CHNGBUF, &svsk->sk_flags);
		set_bit(SK_DATA, &svsk->sk_flags);
		if (sk->sk_state != TCP_ESTABLISHED)
			set_bit(SK_CLOSE, &svsk->sk_flags);
	}
}

void
svc_sock_update_bufs(struct svc_serv *serv)
{
	/*
	 * The number of server threads has changed. Update
	 * rcvbuf and sndbuf accordingly on all sockets
	 */
	struct list_head *le;

	spin_lock_bh(&serv->sv_lock);
	list_for_each(le, &serv->sv_permsocks) {
		struct svc_sock *svsk =
			list_entry(le, struct svc_sock, sk_list);
		set_bit(SK_CHNGBUF, &svsk->sk_flags);
	}
	list_for_each(le, &serv->sv_tempsocks) {
		struct svc_sock *svsk =
			list_entry(le, struct svc_sock, sk_list);
		set_bit(SK_CHNGBUF, &svsk->sk_flags);
	}
	spin_unlock_bh(&serv->sv_lock);
}

/*
 * Receive the next request on any socket.  This code is carefully
 * organised not to touch any cachelines in the shared svc_serv
 * structure, only cachelines in the local svc_pool.
 */
int
svc_recv(struct svc_rqst *rqstp, long timeout)
{
	struct svc_sock		*svsk = NULL;
	struct svc_serv		*serv = rqstp->rq_server;
	struct svc_pool		*pool = rqstp->rq_pool;
	int			len, i;
	int 			pages;
	struct xdr_buf		*arg;
	DECLARE_WAITQUEUE(wait, current);

	dprintk("svc: server %p waiting for data (to = %ld)\n",
		rqstp, timeout);

	if (rqstp->rq_sock)
		printk(KERN_ERR
			"svc_recv: service %p, socket not NULL!\n",
			 rqstp);
	if (waitqueue_active(&rqstp->rq_wait))
		printk(KERN_ERR
			"svc_recv: service %p, wait queue active!\n",
			 rqstp);


	/* now allocate needed pages.  If we get a failure, sleep briefly */
	pages = (serv->sv_max_mesg + PAGE_SIZE) / PAGE_SIZE;
	for (i=0; i < pages ; i++)
		while (rqstp->rq_pages[i] == NULL) {
			struct page *p = alloc_page(GFP_KERNEL);
			if (!p)
				schedule_timeout_uninterruptible(msecs_to_jiffies(500));
			rqstp->rq_pages[i] = p;
		}
	rqstp->rq_pages[i++] = NULL; /* this might be seen in nfs_read_actor */
	BUG_ON(pages >= RPCSVC_MAXPAGES);

	/* Make arg->head point to first page and arg->pages point to rest */
	arg = &rqstp->rq_arg;
	arg->head[0].iov_base = page_address(rqstp->rq_pages[0]);
	arg->head[0].iov_len = PAGE_SIZE;
	arg->pages = rqstp->rq_pages + 1;
	arg->page_base = 0;
	/* save at least one page for response */
	arg->page_len = (pages-2)*PAGE_SIZE;
	arg->len = (pages-1)*PAGE_SIZE;
	arg->tail[0].iov_len = 0;

	try_to_freeze();
	cond_resched();
	if (signalled())
		return -EINTR;

	spin_lock_bh(&pool->sp_lock);
	if ((svsk = svc_sock_dequeue(pool)) != NULL) {
		rqstp->rq_sock = svsk;
		atomic_inc(&svsk->sk_inuse);
		rqstp->rq_reserved = serv->sv_max_mesg;
		atomic_add(rqstp->rq_reserved, &svsk->sk_reserved);
	} else {
		/* No data pending. Go to sleep */
		svc_thread_enqueue(pool, rqstp);

		/*
		 * We have to be able to interrupt this wait
		 * to bring down the daemons ...
		 */
		set_current_state(TASK_INTERRUPTIBLE);
		add_wait_queue(&rqstp->rq_wait, &wait);
		spin_unlock_bh(&pool->sp_lock);

		schedule_timeout(timeout);

		try_to_freeze();

		spin_lock_bh(&pool->sp_lock);
		remove_wait_queue(&rqstp->rq_wait, &wait);

		if (!(svsk = rqstp->rq_sock)) {
			svc_thread_dequeue(pool, rqstp);
			spin_unlock_bh(&pool->sp_lock);
			dprintk("svc: server %p, no data yet\n", rqstp);
			return signalled()? -EINTR : -EAGAIN;
		}
	}
	spin_unlock_bh(&pool->sp_lock);

	dprintk("svc: server %p, pool %u, socket %p, inuse=%d\n",
		 rqstp, pool->sp_id, svsk, atomic_read(&svsk->sk_inuse));
	len = svsk->sk_recvfrom(rqstp);
	dprintk("svc: got len=%d\n", len);

	/* No data, incomplete (TCP) read, or accept() */
	if (len == 0 || len == -EAGAIN) {
		rqstp->rq_res.len = 0;
		svc_sock_release(rqstp);
		return -EAGAIN;
	}
	svsk->sk_lastrecv = get_seconds();
	clear_bit(SK_OLD, &svsk->sk_flags);

	rqstp->rq_secure = svc_port_is_privileged(svc_addr(rqstp));
	rqstp->rq_chandle.defer = svc_defer;

	if (serv->sv_stats)
		serv->sv_stats->netcnt++;
	return len;
}

/*
 * Drop request
 */
void
svc_drop(struct svc_rqst *rqstp)
{
	dprintk("svc: socket %p dropped request\n", rqstp->rq_sock);
	svc_sock_release(rqstp);
}

/*
 * Return reply to client.
 */
int
svc_send(struct svc_rqst *rqstp)
{
	struct svc_sock	*svsk;
	int		len;
	struct xdr_buf	*xb;

	if ((svsk = rqstp->rq_sock) == NULL) {
		printk(KERN_WARNING "NULL socket pointer in %s:%d\n",
				__FILE__, __LINE__);
		return -EFAULT;
	}

	/* release the receive skb before sending the reply */
	svc_release_skb(rqstp);

	/* calculate over-all length */
	xb = & rqstp->rq_res;
	xb->len = xb->head[0].iov_len +
		xb->page_len +
		xb->tail[0].iov_len;

	/* Grab svsk->sk_mutex to serialize outgoing data. */
	mutex_lock(&svsk->sk_mutex);
	if (test_bit(SK_DEAD, &svsk->sk_flags))
		len = -ENOTCONN;
	else
		len = svsk->sk_sendto(rqstp);
	mutex_unlock(&svsk->sk_mutex);
	svc_sock_release(rqstp);

	if (len == -ECONNREFUSED || len == -ENOTCONN || len == -EAGAIN)
		return 0;
	return len;
}

/*
 * Timer function to close old temporary sockets, using
 * a mark-and-sweep algorithm.
 */
static void
svc_age_temp_sockets(unsigned long closure)
{
	struct svc_serv *serv = (struct svc_serv *)closure;
	struct svc_sock *svsk;
	struct list_head *le, *next;
	LIST_HEAD(to_be_aged);

	dprintk("svc_age_temp_sockets\n");

	if (!spin_trylock_bh(&serv->sv_lock)) {
		/* busy, try again 1 sec later */
		dprintk("svc_age_temp_sockets: busy\n");
		mod_timer(&serv->sv_temptimer, jiffies + HZ);
		return;
	}

	list_for_each_safe(le, next, &serv->sv_tempsocks) {
		svsk = list_entry(le, struct svc_sock, sk_list);

		if (!test_and_set_bit(SK_OLD, &svsk->sk_flags))
			continue;
		if (atomic_read(&svsk->sk_inuse) || test_bit(SK_BUSY, &svsk->sk_flags))
			continue;
		atomic_inc(&svsk->sk_inuse);
		list_move(le, &to_be_aged);
		set_bit(SK_CLOSE, &svsk->sk_flags);
		set_bit(SK_DETACHED, &svsk->sk_flags);
	}
	spin_unlock_bh(&serv->sv_lock);

	while (!list_empty(&to_be_aged)) {
		le = to_be_aged.next;
		/* fiddling the sk_list node is safe 'cos we're SK_DETACHED */
		list_del_init(le);
		svsk = list_entry(le, struct svc_sock, sk_list);

		dprintk("queuing svsk %p for closing, %lu seconds old\n",
			svsk, get_seconds() - svsk->sk_lastrecv);

		/* a thread will dequeue and close it soon */
		svc_sock_enqueue(svsk);
		svc_sock_put(svsk);
	}

	mod_timer(&serv->sv_temptimer, jiffies + svc_conn_age_period * HZ);
}

/*
 * Initialize socket for RPC use and create svc_sock struct
 * XXX: May want to setsockopt SO_SNDBUF and SO_RCVBUF.
 */
static struct svc_sock *svc_setup_socket(struct svc_serv *serv,
						struct socket *sock,
						int *errp, int flags)
{
	struct svc_sock	*svsk;
	struct sock	*inet;
	int		pmap_register = !(flags & SVC_SOCK_ANONYMOUS);
	int		is_temporary = flags & SVC_SOCK_TEMPORARY;

	dprintk("svc: svc_setup_socket %p\n", sock);
	if (!(svsk = kzalloc(sizeof(*svsk), GFP_KERNEL))) {
		*errp = -ENOMEM;
		return NULL;
	}

	inet = sock->sk;

	/* Register socket with portmapper */
	if (*errp >= 0 && pmap_register)
		*errp = svc_register(serv, inet->sk_protocol,
				     ntohs(inet_sk(inet)->sport));

	if (*errp < 0) {
		kfree(svsk);
		return NULL;
	}

	set_bit(SK_BUSY, &svsk->sk_flags);
	inet->sk_user_data = svsk;
	svsk->sk_sock = sock;
	svsk->sk_sk = inet;
	svsk->sk_ostate = inet->sk_state_change;
	svsk->sk_odata = inet->sk_data_ready;
	svsk->sk_owspace = inet->sk_write_space;
	svsk->sk_server = serv;
	atomic_set(&svsk->sk_inuse, 1);
	svsk->sk_lastrecv = get_seconds();
	spin_lock_init(&svsk->sk_defer_lock);
	INIT_LIST_HEAD(&svsk->sk_deferred);
	INIT_LIST_HEAD(&svsk->sk_ready);
	mutex_init(&svsk->sk_mutex);

	/* Initialize the socket */
	if (sock->type == SOCK_DGRAM)
		svc_udp_init(svsk);
	else
		svc_tcp_init(svsk);

	spin_lock_bh(&serv->sv_lock);
	if (is_temporary) {
		set_bit(SK_TEMP, &svsk->sk_flags);
		list_add(&svsk->sk_list, &serv->sv_tempsocks);
		serv->sv_tmpcnt++;
		if (serv->sv_temptimer.function == NULL) {
			/* setup timer to age temp sockets */
			setup_timer(&serv->sv_temptimer, svc_age_temp_sockets,
					(unsigned long)serv);
			mod_timer(&serv->sv_temptimer,
					jiffies + svc_conn_age_period * HZ);
		}
	} else {
		clear_bit(SK_TEMP, &svsk->sk_flags);
		list_add(&svsk->sk_list, &serv->sv_permsocks);
	}
	spin_unlock_bh(&serv->sv_lock);

	dprintk("svc: svc_setup_socket created %p (inet %p)\n",
				svsk, svsk->sk_sk);

	return svsk;
}

int svc_addsock(struct svc_serv *serv,
		int fd,
		char *name_return,
		int *proto)
{
	int err = 0;
	struct socket *so = sockfd_lookup(fd, &err);
	struct svc_sock *svsk = NULL;

	if (!so)
		return err;
	if (so->sk->sk_family != AF_INET)
		err =  -EAFNOSUPPORT;
	else if (so->sk->sk_protocol != IPPROTO_TCP &&
	    so->sk->sk_protocol != IPPROTO_UDP)
		err =  -EPROTONOSUPPORT;
	else if (so->state > SS_UNCONNECTED)
		err = -EISCONN;
	else {
		svsk = svc_setup_socket(serv, so, &err, SVC_SOCK_DEFAULTS);
		if (svsk) {
			svc_sock_received(svsk);
			err = 0;
		}
	}
	if (err) {
		sockfd_put(so);
		return err;
	}
	if (proto) *proto = so->sk->sk_protocol;
	return one_sock_name(name_return, svsk);
}
EXPORT_SYMBOL_GPL(svc_addsock);

/*
 * Create socket for RPC service.
 */
static int svc_create_socket(struct svc_serv *serv, int protocol,
				struct sockaddr *sin, int len, int flags)
{
	struct svc_sock	*svsk;
	struct socket	*sock;
	int		error;
	int		type;
	char		buf[RPC_MAX_ADDRBUFLEN];

	dprintk("svc: svc_create_socket(%s, %d, %s)\n",
			serv->sv_program->pg_name, protocol,
			__svc_print_addr(sin, buf, sizeof(buf)));

	if (protocol != IPPROTO_UDP && protocol != IPPROTO_TCP) {
		printk(KERN_WARNING "svc: only UDP and TCP "
				"sockets supported\n");
		return -EINVAL;
	}
	type = (protocol == IPPROTO_UDP)? SOCK_DGRAM : SOCK_STREAM;

	error = sock_create_kern(sin->sa_family, type, protocol, &sock);
	if (error < 0)
		return error;

	svc_reclassify_socket(sock);

	if (type == SOCK_STREAM)
		sock->sk->sk_reuse = 1;		/* allow address reuse */
	error = kernel_bind(sock, sin, len);
	if (error < 0)
		goto bummer;

	if (protocol == IPPROTO_TCP) {
		if ((error = kernel_listen(sock, 64)) < 0)
			goto bummer;
	}

	if ((svsk = svc_setup_socket(serv, sock, &error, flags)) != NULL) {
		svc_sock_received(svsk);
		return ntohs(inet_sk(svsk->sk_sk)->sport);
	}

bummer:
	dprintk("svc: svc_create_socket error = %d\n", -error);
	sock_release(sock);
	return error;
}

/*
 * Remove a dead socket
 */
static void
svc_delete_socket(struct svc_sock *svsk)
{
	struct svc_serv	*serv;
	struct sock	*sk;

	dprintk("svc: svc_delete_socket(%p)\n", svsk);

	serv = svsk->sk_server;
	sk = svsk->sk_sk;

	sk->sk_state_change = svsk->sk_ostate;
	sk->sk_data_ready = svsk->sk_odata;
	sk->sk_write_space = svsk->sk_owspace;

	spin_lock_bh(&serv->sv_lock);

	if (!test_and_set_bit(SK_DETACHED, &svsk->sk_flags))
		list_del_init(&svsk->sk_list);
	/*
	 * We used to delete the svc_sock from whichever list
	 * it's sk_ready node was on, but we don't actually
	 * need to.  This is because the only time we're called
	 * while still attached to a queue, the queue itself
	 * is about to be destroyed (in svc_destroy).
	 */
	if (!test_and_set_bit(SK_DEAD, &svsk->sk_flags)) {
		BUG_ON(atomic_read(&svsk->sk_inuse)<2);
		atomic_dec(&svsk->sk_inuse);
		if (test_bit(SK_TEMP, &svsk->sk_flags))
			serv->sv_tmpcnt--;
	}

	spin_unlock_bh(&serv->sv_lock);
}

static void svc_close_socket(struct svc_sock *svsk)
{
	set_bit(SK_CLOSE, &svsk->sk_flags);
	if (test_and_set_bit(SK_BUSY, &svsk->sk_flags))
		/* someone else will have to effect the close */
		return;

	atomic_inc(&svsk->sk_inuse);
	svc_delete_socket(svsk);
	clear_bit(SK_BUSY, &svsk->sk_flags);
	svc_sock_put(svsk);
}

void svc_force_close_socket(struct svc_sock *svsk)
{
	set_bit(SK_CLOSE, &svsk->sk_flags);
	if (test_bit(SK_BUSY, &svsk->sk_flags)) {
		/* Waiting to be processed, but no threads left,
		 * So just remove it from the waiting list
		 */
		list_del_init(&svsk->sk_ready);
		clear_bit(SK_BUSY, &svsk->sk_flags);
	}
	svc_close_socket(svsk);
}

/**
 * svc_makesock - Make a socket for nfsd and lockd
 * @serv: RPC server structure
 * @protocol: transport protocol to use
 * @port: port to use
 * @flags: requested socket characteristics
 *
 */
int svc_makesock(struct svc_serv *serv, int protocol, unsigned short port,
			int flags)
{
	struct sockaddr_in sin = {
		.sin_family		= AF_INET,
		.sin_addr.s_addr	= INADDR_ANY,
		.sin_port		= htons(port),
	};

	dprintk("svc: creating socket proto = %d\n", protocol);
	return svc_create_socket(serv, protocol, (struct sockaddr *) &sin,
							sizeof(sin), flags);
}

/*
 * Handle defer and revisit of requests
 */

static void svc_revisit(struct cache_deferred_req *dreq, int too_many)
{
	struct svc_deferred_req *dr = container_of(dreq, struct svc_deferred_req, handle);
	struct svc_sock *svsk;

	if (too_many) {
		svc_sock_put(dr->svsk);
		kfree(dr);
		return;
	}
	dprintk("revisit queued\n");
	svsk = dr->svsk;
	dr->svsk = NULL;
	spin_lock_bh(&svsk->sk_defer_lock);
	list_add(&dr->handle.recent, &svsk->sk_deferred);
	spin_unlock_bh(&svsk->sk_defer_lock);
	set_bit(SK_DEFERRED, &svsk->sk_flags);
	svc_sock_enqueue(svsk);
	svc_sock_put(svsk);
}

static struct cache_deferred_req *
svc_defer(struct cache_req *req)
{
	struct svc_rqst *rqstp = container_of(req, struct svc_rqst, rq_chandle);
	int size = sizeof(struct svc_deferred_req) + (rqstp->rq_arg.len);
	struct svc_deferred_req *dr;

	if (rqstp->rq_arg.page_len)
		return NULL; /* if more than a page, give up FIXME */
	if (rqstp->rq_deferred) {
		dr = rqstp->rq_deferred;
		rqstp->rq_deferred = NULL;
	} else {
		int skip  = rqstp->rq_arg.len - rqstp->rq_arg.head[0].iov_len;
		/* FIXME maybe discard if size too large */
		dr = kmalloc(size, GFP_KERNEL);
		if (dr == NULL)
			return NULL;

		dr->handle.owner = rqstp->rq_server;
		dr->prot = rqstp->rq_prot;
		memcpy(&dr->addr, &rqstp->rq_addr, rqstp->rq_addrlen);
		dr->addrlen = rqstp->rq_addrlen;
		dr->daddr = rqstp->rq_daddr;
		dr->argslen = rqstp->rq_arg.len >> 2;
		memcpy(dr->args, rqstp->rq_arg.head[0].iov_base-skip, dr->argslen<<2);
	}
	atomic_inc(&rqstp->rq_sock->sk_inuse);
	dr->svsk = rqstp->rq_sock;

	dr->handle.revisit = svc_revisit;
	return &dr->handle;
}

/*
 * recv data from a deferred request into an active one
 */
static int svc_deferred_recv(struct svc_rqst *rqstp)
{
	struct svc_deferred_req *dr = rqstp->rq_deferred;

	rqstp->rq_arg.head[0].iov_base = dr->args;
	rqstp->rq_arg.head[0].iov_len = dr->argslen<<2;
	rqstp->rq_arg.page_len = 0;
	rqstp->rq_arg.len = dr->argslen<<2;
	rqstp->rq_prot        = dr->prot;
	memcpy(&rqstp->rq_addr, &dr->addr, dr->addrlen);
	rqstp->rq_addrlen     = dr->addrlen;
	rqstp->rq_daddr       = dr->daddr;
	rqstp->rq_respages    = rqstp->rq_pages;
	return dr->argslen<<2;
}


static struct svc_deferred_req *svc_deferred_dequeue(struct svc_sock *svsk)
{
	struct svc_deferred_req *dr = NULL;

	if (!test_bit(SK_DEFERRED, &svsk->sk_flags))
		return NULL;
	spin_lock_bh(&svsk->sk_defer_lock);
	clear_bit(SK_DEFERRED, &svsk->sk_flags);
	if (!list_empty(&svsk->sk_deferred)) {
		dr = list_entry(svsk->sk_deferred.next,
				struct svc_deferred_req,
				handle.recent);
		list_del_init(&dr->handle.recent);
		set_bit(SK_DEFERRED, &svsk->sk_flags);
	}
	spin_unlock_bh(&svsk->sk_defer_lock);
	return dr;
}
