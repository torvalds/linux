/*
 * IPVS         An implementation of the IP virtual server support for the
 *              LINUX operating system.  IPVS is now implemented as a module
 *              over the NetFilter framework. IPVS can be used to build a
 *              high-performance and highly available server based on a
 *              cluster of servers.
 *
 * Authors:     Wensong Zhang <wensong@linuxvirtualserver.org>
 *
 * ip_vs_sync:  sync connection info from master load balancer to backups
 *              through multicast
 *
 * Changes:
 *	Alexandre Cassen	:	Added master & backup support at a time.
 *	Alexandre Cassen	:	Added SyncID support for incoming sync
 *					messages filtering.
 *	Justin Ossevoort	:	Fix endian problem on sync message size.
 */

#define KMSG_COMPONENT "IPVS"
#define pr_fmt(fmt) KMSG_COMPONENT ": " fmt

#include <linux/module.h>
#include <linux/slab.h>
#include <linux/inetdevice.h>
#include <linux/net.h>
#include <linux/completion.h>
#include <linux/delay.h>
#include <linux/skbuff.h>
#include <linux/in.h>
#include <linux/igmp.h>                 /* for ip_mc_join_group */
#include <linux/udp.h>
#include <linux/err.h>
#include <linux/kthread.h>
#include <linux/wait.h>
#include <linux/kernel.h>

#include <net/ip.h>
#include <net/sock.h>

#include <net/ip_vs.h>

#define IP_VS_SYNC_GROUP 0xe0000051    /* multicast addr - 224.0.0.81 */
#define IP_VS_SYNC_PORT  8848          /* multicast port */


/*
 *	IPVS sync connection entry
 */
struct ip_vs_sync_conn {
	__u8			reserved;

	/* Protocol, addresses and port numbers */
	__u8			protocol;       /* Which protocol (TCP/UDP) */
	__be16			cport;
	__be16                  vport;
	__be16                  dport;
	__be32                  caddr;          /* client address */
	__be32                  vaddr;          /* virtual address */
	__be32                  daddr;          /* destination address */

	/* Flags and state transition */
	__be16                  flags;          /* status flags */
	__be16                  state;          /* state info */

	/* The sequence options start here */
};

struct ip_vs_sync_conn_options {
	struct ip_vs_seq        in_seq;         /* incoming seq. struct */
	struct ip_vs_seq        out_seq;        /* outgoing seq. struct */
};

struct ip_vs_sync_thread_data {
	struct socket *sock;
	char *buf;
};

#define SIMPLE_CONN_SIZE  (sizeof(struct ip_vs_sync_conn))
#define FULL_CONN_SIZE  \
(sizeof(struct ip_vs_sync_conn) + sizeof(struct ip_vs_sync_conn_options))


/*
  The master mulitcasts messages to the backup load balancers in the
  following format.

       0                   1                   2                   3
       0 1 2 3 4 5 6 7 8 9 0 1 2 3 4 5 6 7 8 9 0 1 2 3 4 5 6 7 8 9 0 1
      +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
      |  Count Conns  |    SyncID     |            Size               |
      +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
      |                                                               |
      |                    IPVS Sync Connection (1)                   |
      +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
      |                            .                                  |
      |                            .                                  |
      |                            .                                  |
      +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
      |                                                               |
      |                    IPVS Sync Connection (n)                   |
      +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
*/

#define SYNC_MESG_HEADER_LEN	4
#define MAX_CONNS_PER_SYNCBUFF	255 /* nr_conns in ip_vs_sync_mesg is 8 bit */

struct ip_vs_sync_mesg {
	__u8                    nr_conns;
	__u8                    syncid;
	__u16                   size;

	/* ip_vs_sync_conn entries start here */
};

/* the maximum length of sync (sending/receiving) message */
static int sync_send_mesg_maxlen;
static int sync_recv_mesg_maxlen;

struct ip_vs_sync_buff {
	struct list_head        list;
	unsigned long           firstuse;

	/* pointers for the message data */
	struct ip_vs_sync_mesg  *mesg;
	unsigned char           *head;
	unsigned char           *end;
};


/* the sync_buff list head and the lock */
static LIST_HEAD(ip_vs_sync_queue);
static DEFINE_SPINLOCK(ip_vs_sync_lock);

/* current sync_buff for accepting new conn entries */
static struct ip_vs_sync_buff   *curr_sb = NULL;
static DEFINE_SPINLOCK(curr_sb_lock);

/* ipvs sync daemon state */
volatile int ip_vs_sync_state = IP_VS_STATE_NONE;
volatile int ip_vs_master_syncid = 0;
volatile int ip_vs_backup_syncid = 0;

/* multicast interface name */
char ip_vs_master_mcast_ifn[IP_VS_IFNAME_MAXLEN];
char ip_vs_backup_mcast_ifn[IP_VS_IFNAME_MAXLEN];

/* sync daemon tasks */
static struct task_struct *sync_master_thread;
static struct task_struct *sync_backup_thread;

/* multicast addr */
static struct sockaddr_in mcast_addr = {
	.sin_family		= AF_INET,
	.sin_port		= cpu_to_be16(IP_VS_SYNC_PORT),
	.sin_addr.s_addr	= cpu_to_be32(IP_VS_SYNC_GROUP),
};


static inline struct ip_vs_sync_buff *sb_dequeue(void)
{
	struct ip_vs_sync_buff *sb;

	spin_lock_bh(&ip_vs_sync_lock);
	if (list_empty(&ip_vs_sync_queue)) {
		sb = NULL;
	} else {
		sb = list_entry(ip_vs_sync_queue.next,
				struct ip_vs_sync_buff,
				list);
		list_del(&sb->list);
	}
	spin_unlock_bh(&ip_vs_sync_lock);

	return sb;
}

static inline struct ip_vs_sync_buff * ip_vs_sync_buff_create(void)
{
	struct ip_vs_sync_buff *sb;

	if (!(sb=kmalloc(sizeof(struct ip_vs_sync_buff), GFP_ATOMIC)))
		return NULL;

	if (!(sb->mesg=kmalloc(sync_send_mesg_maxlen, GFP_ATOMIC))) {
		kfree(sb);
		return NULL;
	}
	sb->mesg->nr_conns = 0;
	sb->mesg->syncid = ip_vs_master_syncid;
	sb->mesg->size = 4;
	sb->head = (unsigned char *)sb->mesg + 4;
	sb->end = (unsigned char *)sb->mesg + sync_send_mesg_maxlen;
	sb->firstuse = jiffies;
	return sb;
}

static inline void ip_vs_sync_buff_release(struct ip_vs_sync_buff *sb)
{
	kfree(sb->mesg);
	kfree(sb);
}

static inline void sb_queue_tail(struct ip_vs_sync_buff *sb)
{
	spin_lock(&ip_vs_sync_lock);
	if (ip_vs_sync_state & IP_VS_STATE_MASTER)
		list_add_tail(&sb->list, &ip_vs_sync_queue);
	else
		ip_vs_sync_buff_release(sb);
	spin_unlock(&ip_vs_sync_lock);
}

/*
 *	Get the current sync buffer if it has been created for more
 *	than the specified time or the specified time is zero.
 */
static inline struct ip_vs_sync_buff *
get_curr_sync_buff(unsigned long time)
{
	struct ip_vs_sync_buff *sb;

	spin_lock_bh(&curr_sb_lock);
	if (curr_sb && (time == 0 ||
			time_before(jiffies - curr_sb->firstuse, time))) {
		sb = curr_sb;
		curr_sb = NULL;
	} else
		sb = NULL;
	spin_unlock_bh(&curr_sb_lock);
	return sb;
}


/*
 *      Add an ip_vs_conn information into the current sync_buff.
 *      Called by ip_vs_in.
 */
void ip_vs_sync_conn(const struct ip_vs_conn *cp)
{
	struct ip_vs_sync_mesg *m;
	struct ip_vs_sync_conn *s;
	int len;

	spin_lock(&curr_sb_lock);
	if (!curr_sb) {
		if (!(curr_sb=ip_vs_sync_buff_create())) {
			spin_unlock(&curr_sb_lock);
			pr_err("ip_vs_sync_buff_create failed.\n");
			return;
		}
	}

	len = (cp->flags & IP_VS_CONN_F_SEQ_MASK) ? FULL_CONN_SIZE :
		SIMPLE_CONN_SIZE;
	m = curr_sb->mesg;
	s = (struct ip_vs_sync_conn *)curr_sb->head;

	/* copy members */
	s->protocol = cp->protocol;
	s->cport = cp->cport;
	s->vport = cp->vport;
	s->dport = cp->dport;
	s->caddr = cp->caddr.ip;
	s->vaddr = cp->vaddr.ip;
	s->daddr = cp->daddr.ip;
	s->flags = htons(cp->flags & ~IP_VS_CONN_F_HASHED);
	s->state = htons(cp->state);
	if (cp->flags & IP_VS_CONN_F_SEQ_MASK) {
		struct ip_vs_sync_conn_options *opt =
			(struct ip_vs_sync_conn_options *)&s[1];
		memcpy(opt, &cp->in_seq, sizeof(*opt));
	}

	m->nr_conns++;
	m->size += len;
	curr_sb->head += len;

	/* check if there is a space for next one */
	if (curr_sb->head+FULL_CONN_SIZE > curr_sb->end) {
		sb_queue_tail(curr_sb);
		curr_sb = NULL;
	}
	spin_unlock(&curr_sb_lock);

	/* synchronize its controller if it has */
	if (cp->control)
		ip_vs_sync_conn(cp->control);
}

static inline int
ip_vs_conn_fill_param_sync(int af, int protocol,
			   const union nf_inet_addr *caddr, __be16 cport,
			   const union nf_inet_addr *vaddr, __be16 vport,
			   struct ip_vs_conn_param *p)
{
	/* XXX: Need to take into account persistence engine */
	ip_vs_conn_fill_param(af, protocol, caddr, cport, vaddr, vport, p);
	return 0;
}

/*
 *      Process received multicast message and create the corresponding
 *      ip_vs_conn entries.
 */
static void ip_vs_process_message(char *buffer, const size_t buflen)
{
	struct ip_vs_sync_mesg *m = (struct ip_vs_sync_mesg *)buffer;
	struct ip_vs_sync_conn *s;
	struct ip_vs_sync_conn_options *opt;
	struct ip_vs_conn *cp;
	struct ip_vs_protocol *pp;
	struct ip_vs_dest *dest;
	struct ip_vs_conn_param param;
	char *p;
	int i;

	if (buflen < sizeof(struct ip_vs_sync_mesg)) {
		IP_VS_ERR_RL("sync message header too short\n");
		return;
	}

	/* Convert size back to host byte order */
	m->size = ntohs(m->size);

	if (buflen != m->size) {
		IP_VS_ERR_RL("bogus sync message size\n");
		return;
	}

	/* SyncID sanity check */
	if (ip_vs_backup_syncid != 0 && m->syncid != ip_vs_backup_syncid) {
		IP_VS_DBG(7, "Ignoring incoming msg with syncid = %d\n",
			  m->syncid);
		return;
	}

	p = (char *)buffer + sizeof(struct ip_vs_sync_mesg);
	for (i=0; i<m->nr_conns; i++) {
		unsigned flags, state;

		if (p + SIMPLE_CONN_SIZE > buffer+buflen) {
			IP_VS_ERR_RL("bogus conn in sync message\n");
			return;
		}
		s = (struct ip_vs_sync_conn *) p;
		flags = ntohs(s->flags) | IP_VS_CONN_F_SYNC;
		flags &= ~IP_VS_CONN_F_HASHED;
		if (flags & IP_VS_CONN_F_SEQ_MASK) {
			opt = (struct ip_vs_sync_conn_options *)&s[1];
			p += FULL_CONN_SIZE;
			if (p > buffer+buflen) {
				IP_VS_ERR_RL("bogus conn options in sync message\n");
				return;
			}
		} else {
			opt = NULL;
			p += SIMPLE_CONN_SIZE;
		}

		state = ntohs(s->state);
		if (!(flags & IP_VS_CONN_F_TEMPLATE)) {
			pp = ip_vs_proto_get(s->protocol);
			if (!pp) {
				IP_VS_ERR_RL("Unsupported protocol %u in sync msg\n",
					s->protocol);
				continue;
			}
			if (state >= pp->num_states) {
				IP_VS_DBG(2, "Invalid %s state %u in sync msg\n",
					pp->name, state);
				continue;
			}
		} else {
			/* protocol in templates is not used for state/timeout */
			pp = NULL;
			if (state > 0) {
				IP_VS_DBG(2, "Invalid template state %u in sync msg\n",
					state);
				state = 0;
			}
		}

		if (ip_vs_conn_fill_param_sync(AF_INET, s->protocol,
					       (union nf_inet_addr *)&s->caddr,
					       s->cport,
					       (union nf_inet_addr *)&s->vaddr,
					       s->vport, &param)) {
			pr_err("ip_vs_conn_fill_param_sync failed");
			return;
		}
		if (!(flags & IP_VS_CONN_F_TEMPLATE))
			cp = ip_vs_conn_in_get(&param);
		else
			cp = ip_vs_ct_in_get(&param);
		if (!cp) {
			/*
			 * Find the appropriate destination for the connection.
			 * If it is not found the connection will remain unbound
			 * but still handled.
			 */
			dest = ip_vs_find_dest(AF_INET,
					       (union nf_inet_addr *)&s->daddr,
					       s->dport,
					       (union nf_inet_addr *)&s->vaddr,
					       s->vport,
					       s->protocol, 0);
			/*  Set the approprite ativity flag */
			if (s->protocol == IPPROTO_TCP) {
				if (state != IP_VS_TCP_S_ESTABLISHED)
					flags |= IP_VS_CONN_F_INACTIVE;
				else
					flags &= ~IP_VS_CONN_F_INACTIVE;
			} else if (s->protocol == IPPROTO_SCTP) {
				if (state != IP_VS_SCTP_S_ESTABLISHED)
					flags |= IP_VS_CONN_F_INACTIVE;
				else
					flags &= ~IP_VS_CONN_F_INACTIVE;
			}
			cp = ip_vs_conn_new(&param,
					    (union nf_inet_addr *)&s->daddr,
					    s->dport, flags, dest, 0);
			if (dest)
				atomic_dec(&dest->refcnt);
			if (!cp) {
				pr_err("ip_vs_conn_new failed\n");
				return;
			}
		} else if (!cp->dest) {
			dest = ip_vs_try_bind_dest(cp);
			if (dest)
				atomic_dec(&dest->refcnt);
		} else if ((cp->dest) && (cp->protocol == IPPROTO_TCP) &&
			   (cp->state != state)) {
			/* update active/inactive flag for the connection */
			dest = cp->dest;
			if (!(cp->flags & IP_VS_CONN_F_INACTIVE) &&
				(state != IP_VS_TCP_S_ESTABLISHED)) {
				atomic_dec(&dest->activeconns);
				atomic_inc(&dest->inactconns);
				cp->flags |= IP_VS_CONN_F_INACTIVE;
			} else if ((cp->flags & IP_VS_CONN_F_INACTIVE) &&
				(state == IP_VS_TCP_S_ESTABLISHED)) {
				atomic_inc(&dest->activeconns);
				atomic_dec(&dest->inactconns);
				cp->flags &= ~IP_VS_CONN_F_INACTIVE;
			}
		} else if ((cp->dest) && (cp->protocol == IPPROTO_SCTP) &&
			   (cp->state != state)) {
			dest = cp->dest;
			if (!(cp->flags & IP_VS_CONN_F_INACTIVE) &&
			     (state != IP_VS_SCTP_S_ESTABLISHED)) {
			    atomic_dec(&dest->activeconns);
			    atomic_inc(&dest->inactconns);
			    cp->flags &= ~IP_VS_CONN_F_INACTIVE;
			}
		}

		if (opt)
			memcpy(&cp->in_seq, opt, sizeof(*opt));
		atomic_set(&cp->in_pkts, sysctl_ip_vs_sync_threshold[0]);
		cp->state = state;
		cp->old_state = cp->state;
		/*
		 * We can not recover the right timeout for templates
		 * in all cases, we can not find the right fwmark
		 * virtual service. If needed, we can do it for
		 * non-fwmark persistent services.
		 */
		if (!(flags & IP_VS_CONN_F_TEMPLATE) && pp->timeout_table)
			cp->timeout = pp->timeout_table[state];
		else
			cp->timeout = (3*60*HZ);
		ip_vs_conn_put(cp);
	}
}


/*
 *      Setup loopback of outgoing multicasts on a sending socket
 */
static void set_mcast_loop(struct sock *sk, u_char loop)
{
	struct inet_sock *inet = inet_sk(sk);

	/* setsockopt(sock, SOL_IP, IP_MULTICAST_LOOP, &loop, sizeof(loop)); */
	lock_sock(sk);
	inet->mc_loop = loop ? 1 : 0;
	release_sock(sk);
}

/*
 *      Specify TTL for outgoing multicasts on a sending socket
 */
static void set_mcast_ttl(struct sock *sk, u_char ttl)
{
	struct inet_sock *inet = inet_sk(sk);

	/* setsockopt(sock, SOL_IP, IP_MULTICAST_TTL, &ttl, sizeof(ttl)); */
	lock_sock(sk);
	inet->mc_ttl = ttl;
	release_sock(sk);
}

/*
 *      Specifiy default interface for outgoing multicasts
 */
static int set_mcast_if(struct sock *sk, char *ifname)
{
	struct net_device *dev;
	struct inet_sock *inet = inet_sk(sk);

	if ((dev = __dev_get_by_name(&init_net, ifname)) == NULL)
		return -ENODEV;

	if (sk->sk_bound_dev_if && dev->ifindex != sk->sk_bound_dev_if)
		return -EINVAL;

	lock_sock(sk);
	inet->mc_index = dev->ifindex;
	/*  inet->mc_addr  = 0; */
	release_sock(sk);

	return 0;
}


/*
 *	Set the maximum length of sync message according to the
 *	specified interface's MTU.
 */
static int set_sync_mesg_maxlen(int sync_state)
{
	struct net_device *dev;
	int num;

	if (sync_state == IP_VS_STATE_MASTER) {
		if ((dev = __dev_get_by_name(&init_net, ip_vs_master_mcast_ifn)) == NULL)
			return -ENODEV;

		num = (dev->mtu - sizeof(struct iphdr) -
		       sizeof(struct udphdr) -
		       SYNC_MESG_HEADER_LEN - 20) / SIMPLE_CONN_SIZE;
		sync_send_mesg_maxlen = SYNC_MESG_HEADER_LEN +
			SIMPLE_CONN_SIZE * min(num, MAX_CONNS_PER_SYNCBUFF);
		IP_VS_DBG(7, "setting the maximum length of sync sending "
			  "message %d.\n", sync_send_mesg_maxlen);
	} else if (sync_state == IP_VS_STATE_BACKUP) {
		if ((dev = __dev_get_by_name(&init_net, ip_vs_backup_mcast_ifn)) == NULL)
			return -ENODEV;

		sync_recv_mesg_maxlen = dev->mtu -
			sizeof(struct iphdr) - sizeof(struct udphdr);
		IP_VS_DBG(7, "setting the maximum length of sync receiving "
			  "message %d.\n", sync_recv_mesg_maxlen);
	}

	return 0;
}


/*
 *      Join a multicast group.
 *      the group is specified by a class D multicast address 224.0.0.0/8
 *      in the in_addr structure passed in as a parameter.
 */
static int
join_mcast_group(struct sock *sk, struct in_addr *addr, char *ifname)
{
	struct ip_mreqn mreq;
	struct net_device *dev;
	int ret;

	memset(&mreq, 0, sizeof(mreq));
	memcpy(&mreq.imr_multiaddr, addr, sizeof(struct in_addr));

	if ((dev = __dev_get_by_name(&init_net, ifname)) == NULL)
		return -ENODEV;
	if (sk->sk_bound_dev_if && dev->ifindex != sk->sk_bound_dev_if)
		return -EINVAL;

	mreq.imr_ifindex = dev->ifindex;

	lock_sock(sk);
	ret = ip_mc_join_group(sk, &mreq);
	release_sock(sk);

	return ret;
}


static int bind_mcastif_addr(struct socket *sock, char *ifname)
{
	struct net_device *dev;
	__be32 addr;
	struct sockaddr_in sin;

	if ((dev = __dev_get_by_name(&init_net, ifname)) == NULL)
		return -ENODEV;

	addr = inet_select_addr(dev, 0, RT_SCOPE_UNIVERSE);
	if (!addr)
		pr_err("You probably need to specify IP address on "
		       "multicast interface.\n");

	IP_VS_DBG(7, "binding socket with (%s) %pI4\n",
		  ifname, &addr);

	/* Now bind the socket with the address of multicast interface */
	sin.sin_family	     = AF_INET;
	sin.sin_addr.s_addr  = addr;
	sin.sin_port         = 0;

	return sock->ops->bind(sock, (struct sockaddr*)&sin, sizeof(sin));
}

/*
 *      Set up sending multicast socket over UDP
 */
static struct socket * make_send_sock(void)
{
	struct socket *sock;
	int result;

	/* First create a socket */
	result = sock_create_kern(PF_INET, SOCK_DGRAM, IPPROTO_UDP, &sock);
	if (result < 0) {
		pr_err("Error during creation of socket; terminating\n");
		return ERR_PTR(result);
	}

	result = set_mcast_if(sock->sk, ip_vs_master_mcast_ifn);
	if (result < 0) {
		pr_err("Error setting outbound mcast interface\n");
		goto error;
	}

	set_mcast_loop(sock->sk, 0);
	set_mcast_ttl(sock->sk, 1);

	result = bind_mcastif_addr(sock, ip_vs_master_mcast_ifn);
	if (result < 0) {
		pr_err("Error binding address of the mcast interface\n");
		goto error;
	}

	result = sock->ops->connect(sock, (struct sockaddr *) &mcast_addr,
			sizeof(struct sockaddr), 0);
	if (result < 0) {
		pr_err("Error connecting to the multicast addr\n");
		goto error;
	}

	return sock;

  error:
	sock_release(sock);
	return ERR_PTR(result);
}


/*
 *      Set up receiving multicast socket over UDP
 */
static struct socket * make_receive_sock(void)
{
	struct socket *sock;
	int result;

	/* First create a socket */
	result = sock_create_kern(PF_INET, SOCK_DGRAM, IPPROTO_UDP, &sock);
	if (result < 0) {
		pr_err("Error during creation of socket; terminating\n");
		return ERR_PTR(result);
	}

	/* it is equivalent to the REUSEADDR option in user-space */
	sock->sk->sk_reuse = 1;

	result = sock->ops->bind(sock, (struct sockaddr *) &mcast_addr,
			sizeof(struct sockaddr));
	if (result < 0) {
		pr_err("Error binding to the multicast addr\n");
		goto error;
	}

	/* join the multicast group */
	result = join_mcast_group(sock->sk,
			(struct in_addr *) &mcast_addr.sin_addr,
			ip_vs_backup_mcast_ifn);
	if (result < 0) {
		pr_err("Error joining to the multicast group\n");
		goto error;
	}

	return sock;

  error:
	sock_release(sock);
	return ERR_PTR(result);
}


static int
ip_vs_send_async(struct socket *sock, const char *buffer, const size_t length)
{
	struct msghdr	msg = {.msg_flags = MSG_DONTWAIT|MSG_NOSIGNAL};
	struct kvec	iov;
	int		len;

	EnterFunction(7);
	iov.iov_base     = (void *)buffer;
	iov.iov_len      = length;

	len = kernel_sendmsg(sock, &msg, &iov, 1, (size_t)(length));

	LeaveFunction(7);
	return len;
}

static void
ip_vs_send_sync_msg(struct socket *sock, struct ip_vs_sync_mesg *msg)
{
	int msize;

	msize = msg->size;

	/* Put size in network byte order */
	msg->size = htons(msg->size);

	if (ip_vs_send_async(sock, (char *)msg, msize) != msize)
		pr_err("ip_vs_send_async error\n");
}

static int
ip_vs_receive(struct socket *sock, char *buffer, const size_t buflen)
{
	struct msghdr		msg = {NULL,};
	struct kvec		iov;
	int			len;

	EnterFunction(7);

	/* Receive a packet */
	iov.iov_base     = buffer;
	iov.iov_len      = (size_t)buflen;

	len = kernel_recvmsg(sock, &msg, &iov, 1, buflen, 0);

	if (len < 0)
		return -1;

	LeaveFunction(7);
	return len;
}


static int sync_thread_master(void *data)
{
	struct ip_vs_sync_thread_data *tinfo = data;
	struct ip_vs_sync_buff *sb;

	pr_info("sync thread started: state = MASTER, mcast_ifn = %s, "
		"syncid = %d\n",
		ip_vs_master_mcast_ifn, ip_vs_master_syncid);

	while (!kthread_should_stop()) {
		while ((sb = sb_dequeue())) {
			ip_vs_send_sync_msg(tinfo->sock, sb->mesg);
			ip_vs_sync_buff_release(sb);
		}

		/* check if entries stay in curr_sb for 2 seconds */
		sb = get_curr_sync_buff(2 * HZ);
		if (sb) {
			ip_vs_send_sync_msg(tinfo->sock, sb->mesg);
			ip_vs_sync_buff_release(sb);
		}

		schedule_timeout_interruptible(HZ);
	}

	/* clean up the sync_buff queue */
	while ((sb=sb_dequeue())) {
		ip_vs_sync_buff_release(sb);
	}

	/* clean up the current sync_buff */
	if ((sb = get_curr_sync_buff(0))) {
		ip_vs_sync_buff_release(sb);
	}

	/* release the sending multicast socket */
	sock_release(tinfo->sock);
	kfree(tinfo);

	return 0;
}


static int sync_thread_backup(void *data)
{
	struct ip_vs_sync_thread_data *tinfo = data;
	int len;

	pr_info("sync thread started: state = BACKUP, mcast_ifn = %s, "
		"syncid = %d\n",
		ip_vs_backup_mcast_ifn, ip_vs_backup_syncid);

	while (!kthread_should_stop()) {
		wait_event_interruptible(*sk_sleep(tinfo->sock->sk),
			 !skb_queue_empty(&tinfo->sock->sk->sk_receive_queue)
			 || kthread_should_stop());

		/* do we have data now? */
		while (!skb_queue_empty(&(tinfo->sock->sk->sk_receive_queue))) {
			len = ip_vs_receive(tinfo->sock, tinfo->buf,
					sync_recv_mesg_maxlen);
			if (len <= 0) {
				pr_err("receiving message error\n");
				break;
			}

			/* disable bottom half, because it accesses the data
			   shared by softirq while getting/creating conns */
			local_bh_disable();
			ip_vs_process_message(tinfo->buf, len);
			local_bh_enable();
		}
	}

	/* release the sending multicast socket */
	sock_release(tinfo->sock);
	kfree(tinfo->buf);
	kfree(tinfo);

	return 0;
}


int start_sync_thread(int state, char *mcast_ifn, __u8 syncid)
{
	struct ip_vs_sync_thread_data *tinfo;
	struct task_struct **realtask, *task;
	struct socket *sock;
	char *name, *buf = NULL;
	int (*threadfn)(void *data);
	int result = -ENOMEM;

	IP_VS_DBG(7, "%s(): pid %d\n", __func__, task_pid_nr(current));
	IP_VS_DBG(7, "Each ip_vs_sync_conn entry needs %Zd bytes\n",
		  sizeof(struct ip_vs_sync_conn));

	if (state == IP_VS_STATE_MASTER) {
		if (sync_master_thread)
			return -EEXIST;

		strlcpy(ip_vs_master_mcast_ifn, mcast_ifn,
			sizeof(ip_vs_master_mcast_ifn));
		ip_vs_master_syncid = syncid;
		realtask = &sync_master_thread;
		name = "ipvs_syncmaster";
		threadfn = sync_thread_master;
		sock = make_send_sock();
	} else if (state == IP_VS_STATE_BACKUP) {
		if (sync_backup_thread)
			return -EEXIST;

		strlcpy(ip_vs_backup_mcast_ifn, mcast_ifn,
			sizeof(ip_vs_backup_mcast_ifn));
		ip_vs_backup_syncid = syncid;
		realtask = &sync_backup_thread;
		name = "ipvs_syncbackup";
		threadfn = sync_thread_backup;
		sock = make_receive_sock();
	} else {
		return -EINVAL;
	}

	if (IS_ERR(sock)) {
		result = PTR_ERR(sock);
		goto out;
	}

	set_sync_mesg_maxlen(state);
	if (state == IP_VS_STATE_BACKUP) {
		buf = kmalloc(sync_recv_mesg_maxlen, GFP_KERNEL);
		if (!buf)
			goto outsocket;
	}

	tinfo = kmalloc(sizeof(*tinfo), GFP_KERNEL);
	if (!tinfo)
		goto outbuf;

	tinfo->sock = sock;
	tinfo->buf = buf;

	task = kthread_run(threadfn, tinfo, name);
	if (IS_ERR(task)) {
		result = PTR_ERR(task);
		goto outtinfo;
	}

	/* mark as active */
	*realtask = task;
	ip_vs_sync_state |= state;

	/* increase the module use count */
	ip_vs_use_count_inc();

	return 0;

outtinfo:
	kfree(tinfo);
outbuf:
	kfree(buf);
outsocket:
	sock_release(sock);
out:
	return result;
}


int stop_sync_thread(int state)
{
	IP_VS_DBG(7, "%s(): pid %d\n", __func__, task_pid_nr(current));

	if (state == IP_VS_STATE_MASTER) {
		if (!sync_master_thread)
			return -ESRCH;

		pr_info("stopping master sync thread %d ...\n",
			task_pid_nr(sync_master_thread));

		/*
		 * The lock synchronizes with sb_queue_tail(), so that we don't
		 * add sync buffers to the queue, when we are already in
		 * progress of stopping the master sync daemon.
		 */

		spin_lock_bh(&ip_vs_sync_lock);
		ip_vs_sync_state &= ~IP_VS_STATE_MASTER;
		spin_unlock_bh(&ip_vs_sync_lock);
		kthread_stop(sync_master_thread);
		sync_master_thread = NULL;
	} else if (state == IP_VS_STATE_BACKUP) {
		if (!sync_backup_thread)
			return -ESRCH;

		pr_info("stopping backup sync thread %d ...\n",
			task_pid_nr(sync_backup_thread));

		ip_vs_sync_state &= ~IP_VS_STATE_BACKUP;
		kthread_stop(sync_backup_thread);
		sync_backup_thread = NULL;
	} else {
		return -EINVAL;
	}

	/* decrease the module use count */
	ip_vs_use_count_dec();

	return 0;
}
