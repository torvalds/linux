/*
 * IPVS         An implementation of the IP virtual server support for the
 *              LINUX operating system.  IPVS is now implemented as a module
 *              over the NetFilter framework. IPVS can be used to build a
 *              high-performance and highly available server based on a
 *              cluster of servers.
 *
 * Version:     $Id: ip_vs_sync.c,v 1.13 2003/06/08 09:31:19 wensong Exp $
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

#include <net/ip.h>
#include <net/sock.h>
#include <asm/uaccess.h>                /* for get_fs and set_fs */

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

#define IP_VS_SYNC_CONN_TIMEOUT (3*60*HZ)
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

/* multicast addr */
static struct sockaddr_in mcast_addr;


static inline void sb_queue_tail(struct ip_vs_sync_buff *sb)
{
	spin_lock(&ip_vs_sync_lock);
	list_add_tail(&sb->list, &ip_vs_sync_queue);
	spin_unlock(&ip_vs_sync_lock);
}

static inline struct ip_vs_sync_buff * sb_dequeue(void)
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
void ip_vs_sync_conn(struct ip_vs_conn *cp)
{
	struct ip_vs_sync_mesg *m;
	struct ip_vs_sync_conn *s;
	int len;

	spin_lock(&curr_sb_lock);
	if (!curr_sb) {
		if (!(curr_sb=ip_vs_sync_buff_create())) {
			spin_unlock(&curr_sb_lock);
			IP_VS_ERR("ip_vs_sync_buff_create failed.\n");
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
	s->caddr = cp->caddr;
	s->vaddr = cp->vaddr;
	s->daddr = cp->daddr;
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


/*
 *      Process received multicast message and create the corresponding
 *      ip_vs_conn entries.
 */
static void ip_vs_process_message(const char *buffer, const size_t buflen)
{
	struct ip_vs_sync_mesg *m = (struct ip_vs_sync_mesg *)buffer;
	struct ip_vs_sync_conn *s;
	struct ip_vs_sync_conn_options *opt;
	struct ip_vs_conn *cp;
	char *p;
	int i;

	/* Convert size back to host byte order */
	m->size = ntohs(m->size);

	if (buflen != m->size) {
		IP_VS_ERR("bogus message\n");
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
		unsigned flags;

		s = (struct ip_vs_sync_conn *)p;
		flags = ntohs(s->flags);
		if (!(flags & IP_VS_CONN_F_TEMPLATE))
			cp = ip_vs_conn_in_get(s->protocol,
					       s->caddr, s->cport,
					       s->vaddr, s->vport);
		else
			cp = ip_vs_ct_in_get(s->protocol,
					       s->caddr, s->cport,
					       s->vaddr, s->vport);
		if (!cp) {
			cp = ip_vs_conn_new(s->protocol,
					    s->caddr, s->cport,
					    s->vaddr, s->vport,
					    s->daddr, s->dport,
					    flags, NULL);
			if (!cp) {
				IP_VS_ERR("ip_vs_conn_new failed\n");
				return;
			}
			cp->state = ntohs(s->state);
		} else if (!cp->dest) {
			/* it is an entry created by the synchronization */
			cp->state = ntohs(s->state);
			cp->flags = flags | IP_VS_CONN_F_HASHED;
		}	/* Note that we don't touch its state and flags
			   if it is a normal entry. */

		if (flags & IP_VS_CONN_F_SEQ_MASK) {
			opt = (struct ip_vs_sync_conn_options *)&s[1];
			memcpy(&cp->in_seq, opt, sizeof(*opt));
			p += FULL_CONN_SIZE;
		} else
			p += SIMPLE_CONN_SIZE;

		atomic_set(&cp->in_pkts, sysctl_ip_vs_sync_threshold[0]);
		cp->timeout = IP_VS_SYNC_CONN_TIMEOUT;
		ip_vs_conn_put(cp);

		if (p > buffer+buflen) {
			IP_VS_ERR("bogus message\n");
			return;
		}
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

	if ((dev = __dev_get_by_name(ifname)) == NULL)
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
		if ((dev = __dev_get_by_name(ip_vs_master_mcast_ifn)) == NULL)
			return -ENODEV;

		num = (dev->mtu - sizeof(struct iphdr) -
		       sizeof(struct udphdr) -
		       SYNC_MESG_HEADER_LEN - 20) / SIMPLE_CONN_SIZE;
		sync_send_mesg_maxlen =
			SYNC_MESG_HEADER_LEN + SIMPLE_CONN_SIZE * num;
		IP_VS_DBG(7, "setting the maximum length of sync sending "
			  "message %d.\n", sync_send_mesg_maxlen);
	} else if (sync_state == IP_VS_STATE_BACKUP) {
		if ((dev = __dev_get_by_name(ip_vs_backup_mcast_ifn)) == NULL)
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

	if ((dev = __dev_get_by_name(ifname)) == NULL)
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

	if ((dev = __dev_get_by_name(ifname)) == NULL)
		return -ENODEV;

	addr = inet_select_addr(dev, 0, RT_SCOPE_UNIVERSE);
	if (!addr)
		IP_VS_ERR("You probably need to specify IP address on "
			  "multicast interface.\n");

	IP_VS_DBG(7, "binding socket with (%s) %u.%u.%u.%u\n",
		  ifname, NIPQUAD(addr));

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

	/* First create a socket */
	if (sock_create_kern(PF_INET, SOCK_DGRAM, IPPROTO_UDP, &sock) < 0) {
		IP_VS_ERR("Error during creation of socket; terminating\n");
		return NULL;
	}

	if (set_mcast_if(sock->sk, ip_vs_master_mcast_ifn) < 0) {
		IP_VS_ERR("Error setting outbound mcast interface\n");
		goto error;
	}

	set_mcast_loop(sock->sk, 0);
	set_mcast_ttl(sock->sk, 1);

	if (bind_mcastif_addr(sock, ip_vs_master_mcast_ifn) < 0) {
		IP_VS_ERR("Error binding address of the mcast interface\n");
		goto error;
	}

	if (sock->ops->connect(sock,
			       (struct sockaddr*)&mcast_addr,
			       sizeof(struct sockaddr), 0) < 0) {
		IP_VS_ERR("Error connecting to the multicast addr\n");
		goto error;
	}

	return sock;

  error:
	sock_release(sock);
	return NULL;
}


/*
 *      Set up receiving multicast socket over UDP
 */
static struct socket * make_receive_sock(void)
{
	struct socket *sock;

	/* First create a socket */
	if (sock_create_kern(PF_INET, SOCK_DGRAM, IPPROTO_UDP, &sock) < 0) {
		IP_VS_ERR("Error during creation of socket; terminating\n");
		return NULL;
	}

	/* it is equivalent to the REUSEADDR option in user-space */
	sock->sk->sk_reuse = 1;

	if (sock->ops->bind(sock,
			    (struct sockaddr*)&mcast_addr,
			    sizeof(struct sockaddr)) < 0) {
		IP_VS_ERR("Error binding to the multicast addr\n");
		goto error;
	}

	/* join the multicast group */
	if (join_mcast_group(sock->sk,
			     (struct in_addr*)&mcast_addr.sin_addr,
			     ip_vs_backup_mcast_ifn) < 0) {
		IP_VS_ERR("Error joining to the multicast group\n");
		goto error;
	}

	return sock;

  error:
	sock_release(sock);
	return NULL;
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
		IP_VS_ERR("ip_vs_send_async error\n");
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


static DECLARE_WAIT_QUEUE_HEAD(sync_wait);
static pid_t sync_master_pid = 0;
static pid_t sync_backup_pid = 0;

static DECLARE_WAIT_QUEUE_HEAD(stop_sync_wait);
static int stop_master_sync = 0;
static int stop_backup_sync = 0;

static void sync_master_loop(void)
{
	struct socket *sock;
	struct ip_vs_sync_buff *sb;

	/* create the sending multicast socket */
	sock = make_send_sock();
	if (!sock)
		return;

	IP_VS_INFO("sync thread started: state = MASTER, mcast_ifn = %s, "
		   "syncid = %d\n",
		   ip_vs_master_mcast_ifn, ip_vs_master_syncid);

	for (;;) {
		while ((sb=sb_dequeue())) {
			ip_vs_send_sync_msg(sock, sb->mesg);
			ip_vs_sync_buff_release(sb);
		}

		/* check if entries stay in curr_sb for 2 seconds */
		if ((sb = get_curr_sync_buff(2*HZ))) {
			ip_vs_send_sync_msg(sock, sb->mesg);
			ip_vs_sync_buff_release(sb);
		}

		if (stop_master_sync)
			break;

		msleep_interruptible(1000);
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
	sock_release(sock);
}


static void sync_backup_loop(void)
{
	struct socket *sock;
	char *buf;
	int len;

	if (!(buf = kmalloc(sync_recv_mesg_maxlen, GFP_ATOMIC))) {
		IP_VS_ERR("sync_backup_loop: kmalloc error\n");
		return;
	}

	/* create the receiving multicast socket */
	sock = make_receive_sock();
	if (!sock)
		goto out;

	IP_VS_INFO("sync thread started: state = BACKUP, mcast_ifn = %s, "
		   "syncid = %d\n",
		   ip_vs_backup_mcast_ifn, ip_vs_backup_syncid);

	for (;;) {
		/* do you have data now? */
		while (!skb_queue_empty(&(sock->sk->sk_receive_queue))) {
			if ((len =
			     ip_vs_receive(sock, buf,
					   sync_recv_mesg_maxlen)) <= 0) {
				IP_VS_ERR("receiving message error\n");
				break;
			}
			/* disable bottom half, because it accessed the data
			   shared by softirq while getting/creating conns */
			local_bh_disable();
			ip_vs_process_message(buf, len);
			local_bh_enable();
		}

		if (stop_backup_sync)
			break;

		msleep_interruptible(1000);
	}

	/* release the sending multicast socket */
	sock_release(sock);

  out:
	kfree(buf);
}


static void set_sync_pid(int sync_state, pid_t sync_pid)
{
	if (sync_state == IP_VS_STATE_MASTER)
		sync_master_pid = sync_pid;
	else if (sync_state == IP_VS_STATE_BACKUP)
		sync_backup_pid = sync_pid;
}

static void set_stop_sync(int sync_state, int set)
{
	if (sync_state == IP_VS_STATE_MASTER)
		stop_master_sync = set;
	else if (sync_state == IP_VS_STATE_BACKUP)
		stop_backup_sync = set;
	else {
		stop_master_sync = set;
		stop_backup_sync = set;
	}
}

static int sync_thread(void *startup)
{
	DECLARE_WAITQUEUE(wait, current);
	mm_segment_t oldmm;
	int state;
	const char *name;

	/* increase the module use count */
	ip_vs_use_count_inc();

	if (ip_vs_sync_state & IP_VS_STATE_MASTER && !sync_master_pid) {
		state = IP_VS_STATE_MASTER;
		name = "ipvs_syncmaster";
	} else if (ip_vs_sync_state & IP_VS_STATE_BACKUP && !sync_backup_pid) {
		state = IP_VS_STATE_BACKUP;
		name = "ipvs_syncbackup";
	} else {
		IP_VS_BUG();
		ip_vs_use_count_dec();
		return -EINVAL;
	}

	daemonize(name);

	oldmm = get_fs();
	set_fs(KERNEL_DS);

	/* Block all signals */
	spin_lock_irq(&current->sighand->siglock);
	siginitsetinv(&current->blocked, 0);
	recalc_sigpending();
	spin_unlock_irq(&current->sighand->siglock);

	/* set the maximum length of sync message */
	set_sync_mesg_maxlen(state);

	/* set up multicast address */
	mcast_addr.sin_family = AF_INET;
	mcast_addr.sin_port = htons(IP_VS_SYNC_PORT);
	mcast_addr.sin_addr.s_addr = htonl(IP_VS_SYNC_GROUP);

	add_wait_queue(&sync_wait, &wait);

	set_sync_pid(state, current->pid);
	complete((struct completion *)startup);

	/* processing master/backup loop here */
	if (state == IP_VS_STATE_MASTER)
		sync_master_loop();
	else if (state == IP_VS_STATE_BACKUP)
		sync_backup_loop();
	else IP_VS_BUG();

	remove_wait_queue(&sync_wait, &wait);

	/* thread exits */
	set_sync_pid(state, 0);
	IP_VS_INFO("sync thread stopped!\n");

	set_fs(oldmm);

	/* decrease the module use count */
	ip_vs_use_count_dec();

	set_stop_sync(state, 0);
	wake_up(&stop_sync_wait);

	return 0;
}


static int fork_sync_thread(void *startup)
{
	pid_t pid;

	/* fork the sync thread here, then the parent process of the
	   sync thread is the init process after this thread exits. */
  repeat:
	if ((pid = kernel_thread(sync_thread, startup, 0)) < 0) {
		IP_VS_ERR("could not create sync_thread due to %d... "
			  "retrying.\n", pid);
		msleep_interruptible(1000);
		goto repeat;
	}

	return 0;
}


int start_sync_thread(int state, char *mcast_ifn, __u8 syncid)
{
	DECLARE_COMPLETION_ONSTACK(startup);
	pid_t pid;

	if ((state == IP_VS_STATE_MASTER && sync_master_pid) ||
	    (state == IP_VS_STATE_BACKUP && sync_backup_pid))
		return -EEXIST;

	IP_VS_DBG(7, "%s: pid %d\n", __FUNCTION__, current->pid);
	IP_VS_DBG(7, "Each ip_vs_sync_conn entry need %Zd bytes\n",
		  sizeof(struct ip_vs_sync_conn));

	ip_vs_sync_state |= state;
	if (state == IP_VS_STATE_MASTER) {
		strlcpy(ip_vs_master_mcast_ifn, mcast_ifn, sizeof(ip_vs_master_mcast_ifn));
		ip_vs_master_syncid = syncid;
	} else {
		strlcpy(ip_vs_backup_mcast_ifn, mcast_ifn, sizeof(ip_vs_backup_mcast_ifn));
		ip_vs_backup_syncid = syncid;
	}

  repeat:
	if ((pid = kernel_thread(fork_sync_thread, &startup, 0)) < 0) {
		IP_VS_ERR("could not create fork_sync_thread due to %d... "
			  "retrying.\n", pid);
		msleep_interruptible(1000);
		goto repeat;
	}

	wait_for_completion(&startup);

	return 0;
}


int stop_sync_thread(int state)
{
	DECLARE_WAITQUEUE(wait, current);

	if ((state == IP_VS_STATE_MASTER && !sync_master_pid) ||
	    (state == IP_VS_STATE_BACKUP && !sync_backup_pid))
		return -ESRCH;

	IP_VS_DBG(7, "%s: pid %d\n", __FUNCTION__, current->pid);
	IP_VS_INFO("stopping sync thread %d ...\n",
		   (state == IP_VS_STATE_MASTER) ? sync_master_pid : sync_backup_pid);

	__set_current_state(TASK_UNINTERRUPTIBLE);
	add_wait_queue(&stop_sync_wait, &wait);
	set_stop_sync(state, 1);
	ip_vs_sync_state -= state;
	wake_up(&sync_wait);
	schedule();
	__set_current_state(TASK_RUNNING);
	remove_wait_queue(&stop_sync_wait, &wait);

	/* Note: no need to reap the sync thread, because its parent
	   process is the init process */

	if ((state == IP_VS_STATE_MASTER && stop_master_sync) ||
	    (state == IP_VS_STATE_BACKUP && stop_backup_sync))
		IP_VS_BUG();

	return 0;
}
