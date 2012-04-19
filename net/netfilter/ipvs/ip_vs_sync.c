/*
 * IPVS         An implementation of the IP virtual server support for the
 *              LINUX operating system.  IPVS is now implemented as a module
 *              over the NetFilter framework. IPVS can be used to build a
 *              high-performance and highly available server based on a
 *              cluster of servers.
 *
 * Version 1,   is capable of handling both version 0 and 1 messages.
 *              Version 0 is the plain old format.
 *              Note Version 0 receivers will just drop Ver 1 messages.
 *              Version 1 is capable of handle IPv6, Persistence data,
 *              time-outs, and firewall marks.
 *              In ver.1 "ip_vs_sync_conn_options" will be sent in netw. order.
 *              Ver. 0 can be turned on by sysctl -w net.ipv4.vs.sync_version=0
 *
 * Definitions  Message: is a complete datagram
 *              Sync_conn: is a part of a Message
 *              Param Data is an option to a Sync_conn.
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
 *	Hans Schillstrom	:	Added Version 1: i.e. IPv6,
 *					Persistence support, fwmark and time-out.
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

#include <asm/unaligned.h>		/* Used for ntoh_seq and hton_seq */

#include <net/ip.h>
#include <net/sock.h>

#include <net/ip_vs.h>

#define IP_VS_SYNC_GROUP 0xe0000051    /* multicast addr - 224.0.0.81 */
#define IP_VS_SYNC_PORT  8848          /* multicast port */

#define SYNC_PROTO_VER  1		/* Protocol version in header */

static struct lock_class_key __ipvs_sync_key;
/*
 *	IPVS sync connection entry
 *	Version 0, i.e. original version.
 */
struct ip_vs_sync_conn_v0 {
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

/*
     Sync Connection format (sync_conn)

       0                   1                   2                   3
       0 1 2 3 4 5 6 7 8 9 0 1 2 3 4 5 6 7 8 9 0 1 2 3 4 5 6 7 8 9 0 1
      +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
      |    Type       |    Protocol   | Ver.  |        Size           |
      +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
      |                             Flags                             |
      +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
      |            State              |         cport                 |
      +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
      |            vport              |         dport                 |
      +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
      |                             fwmark                            |
      +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
      |                             timeout  (in sec.)                |
      +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
      |                              ...                              |
      |                        IP-Addresses  (v4 or v6)               |
      |                              ...                              |
      +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
  Optional Parameters.
      +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
      | Param. Type    | Param. Length |   Param. data                |
      +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+                               |
      |                              ...                              |
      |                               +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
      |                               | Param Type    | Param. Length |
      +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
      |                           Param  data                         |
      |         Last Param data should be padded for 32 bit alignment |
      +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
*/

/*
 *  Type 0, IPv4 sync connection format
 */
struct ip_vs_sync_v4 {
	__u8			type;
	__u8			protocol;	/* Which protocol (TCP/UDP) */
	__be16			ver_size;	/* Version msb 4 bits */
	/* Flags and state transition */
	__be32			flags;		/* status flags */
	__be16			state;		/* state info 	*/
	/* Protocol, addresses and port numbers */
	__be16			cport;
	__be16			vport;
	__be16			dport;
	__be32			fwmark;		/* Firewall mark from skb */
	__be32			timeout;	/* cp timeout */
	__be32			caddr;		/* client address */
	__be32			vaddr;		/* virtual address */
	__be32			daddr;		/* destination address */
	/* The sequence options start here */
	/* PE data padded to 32bit alignment after seq. options */
};
/*
 * Type 2 messages IPv6
 */
struct ip_vs_sync_v6 {
	__u8			type;
	__u8			protocol;	/* Which protocol (TCP/UDP) */
	__be16			ver_size;	/* Version msb 4 bits */
	/* Flags and state transition */
	__be32			flags;		/* status flags */
	__be16			state;		/* state info 	*/
	/* Protocol, addresses and port numbers */
	__be16			cport;
	__be16			vport;
	__be16			dport;
	__be32			fwmark;		/* Firewall mark from skb */
	__be32			timeout;	/* cp timeout */
	struct in6_addr		caddr;		/* client address */
	struct in6_addr		vaddr;		/* virtual address */
	struct in6_addr		daddr;		/* destination address */
	/* The sequence options start here */
	/* PE data padded to 32bit alignment after seq. options */
};

union ip_vs_sync_conn {
	struct ip_vs_sync_v4	v4;
	struct ip_vs_sync_v6	v6;
};

/* Bits in Type field in above */
#define STYPE_INET6		0
#define STYPE_F_INET6		(1 << STYPE_INET6)

#define SVER_SHIFT		12		/* Shift to get version */
#define SVER_MASK		0x0fff		/* Mask to strip version */

#define IPVS_OPT_SEQ_DATA	1
#define IPVS_OPT_PE_DATA	2
#define IPVS_OPT_PE_NAME	3
#define IPVS_OPT_PARAM		7

#define IPVS_OPT_F_SEQ_DATA	(1 << (IPVS_OPT_SEQ_DATA-1))
#define IPVS_OPT_F_PE_DATA	(1 << (IPVS_OPT_PE_DATA-1))
#define IPVS_OPT_F_PE_NAME	(1 << (IPVS_OPT_PE_NAME-1))
#define IPVS_OPT_F_PARAM	(1 << (IPVS_OPT_PARAM-1))

struct ip_vs_sync_thread_data {
	struct net *net;
	struct socket *sock;
	char *buf;
};

/* Version 0 definition of packet sizes */
#define SIMPLE_CONN_SIZE  (sizeof(struct ip_vs_sync_conn_v0))
#define FULL_CONN_SIZE  \
(sizeof(struct ip_vs_sync_conn_v0) + sizeof(struct ip_vs_sync_conn_options))


/*
  The master mulitcasts messages (Datagrams) to the backup load balancers
  in the following format.

 Version 1:
  Note, first byte should be Zero, so ver 0 receivers will drop the packet.

       0                   1                   2                   3
       0 1 2 3 4 5 6 7 8 9 0 1 2 3 4 5 6 7 8 9 0 1 2 3 4 5 6 7 8 9 0 1
      +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
      |      0        |    SyncID     |            Size               |
      +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
      |  Count Conns  |    Version    |    Reserved, set to Zero      |
      +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
      |                                                               |
      |                    IPVS Sync Connection (1)                   |
      +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
      |                            .                                  |
      ~                            .                                  ~
      |                            .                                  |
      +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
      |                                                               |
      |                    IPVS Sync Connection (n)                   |
      +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+

 Version 0 Header
       0                   1                   2                   3
       0 1 2 3 4 5 6 7 8 9 0 1 2 3 4 5 6 7 8 9 0 1 2 3 4 5 6 7 8 9 0 1
      +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
      |  Count Conns  |    SyncID     |            Size               |
      +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
      |                    IPVS Sync Connection (1)                   |
*/

#define SYNC_MESG_HEADER_LEN	4
#define MAX_CONNS_PER_SYNCBUFF	255 /* nr_conns in ip_vs_sync_mesg is 8 bit */

/* Version 0 header */
struct ip_vs_sync_mesg_v0 {
	__u8                    nr_conns;
	__u8                    syncid;
	__u16                   size;

	/* ip_vs_sync_conn entries start here */
};

/* Version 1 header */
struct ip_vs_sync_mesg {
	__u8			reserved;	/* must be zero */
	__u8			syncid;
	__u16			size;
	__u8			nr_conns;
	__s8			version;	/* SYNC_PROTO_VER  */
	__u16			spare;
	/* ip_vs_sync_conn entries start here */
};

struct ip_vs_sync_buff {
	struct list_head        list;
	unsigned long           firstuse;

	/* pointers for the message data */
	struct ip_vs_sync_mesg  *mesg;
	unsigned char           *head;
	unsigned char           *end;
};

/* multicast addr */
static struct sockaddr_in mcast_addr = {
	.sin_family		= AF_INET,
	.sin_port		= cpu_to_be16(IP_VS_SYNC_PORT),
	.sin_addr.s_addr	= cpu_to_be32(IP_VS_SYNC_GROUP),
};

/*
 * Copy of struct ip_vs_seq
 * From unaligned network order to aligned host order
 */
static void ntoh_seq(struct ip_vs_seq *no, struct ip_vs_seq *ho)
{
	ho->init_seq       = get_unaligned_be32(&no->init_seq);
	ho->delta          = get_unaligned_be32(&no->delta);
	ho->previous_delta = get_unaligned_be32(&no->previous_delta);
}

/*
 * Copy of struct ip_vs_seq
 * From Aligned host order to unaligned network order
 */
static void hton_seq(struct ip_vs_seq *ho, struct ip_vs_seq *no)
{
	put_unaligned_be32(ho->init_seq, &no->init_seq);
	put_unaligned_be32(ho->delta, &no->delta);
	put_unaligned_be32(ho->previous_delta, &no->previous_delta);
}

static inline struct ip_vs_sync_buff *sb_dequeue(struct netns_ipvs *ipvs)
{
	struct ip_vs_sync_buff *sb;

	spin_lock_bh(&ipvs->sync_lock);
	if (list_empty(&ipvs->sync_queue)) {
		sb = NULL;
	} else {
		sb = list_entry(ipvs->sync_queue.next,
				struct ip_vs_sync_buff,
				list);
		list_del(&sb->list);
	}
	spin_unlock_bh(&ipvs->sync_lock);

	return sb;
}

/*
 * Create a new sync buffer for Version 1 proto.
 */
static inline struct ip_vs_sync_buff *
ip_vs_sync_buff_create(struct netns_ipvs *ipvs)
{
	struct ip_vs_sync_buff *sb;

	if (!(sb=kmalloc(sizeof(struct ip_vs_sync_buff), GFP_ATOMIC)))
		return NULL;

	sb->mesg = kmalloc(ipvs->send_mesg_maxlen, GFP_ATOMIC);
	if (!sb->mesg) {
		kfree(sb);
		return NULL;
	}
	sb->mesg->reserved = 0;  /* old nr_conns i.e. must be zeo now */
	sb->mesg->version = SYNC_PROTO_VER;
	sb->mesg->syncid = ipvs->master_syncid;
	sb->mesg->size = sizeof(struct ip_vs_sync_mesg);
	sb->mesg->nr_conns = 0;
	sb->mesg->spare = 0;
	sb->head = (unsigned char *)sb->mesg + sizeof(struct ip_vs_sync_mesg);
	sb->end = (unsigned char *)sb->mesg + ipvs->send_mesg_maxlen;

	sb->firstuse = jiffies;
	return sb;
}

static inline void ip_vs_sync_buff_release(struct ip_vs_sync_buff *sb)
{
	kfree(sb->mesg);
	kfree(sb);
}

static inline void sb_queue_tail(struct netns_ipvs *ipvs)
{
	struct ip_vs_sync_buff *sb = ipvs->sync_buff;

	spin_lock(&ipvs->sync_lock);
	if (ipvs->sync_state & IP_VS_STATE_MASTER)
		list_add_tail(&sb->list, &ipvs->sync_queue);
	else
		ip_vs_sync_buff_release(sb);
	spin_unlock(&ipvs->sync_lock);
}

/*
 *	Get the current sync buffer if it has been created for more
 *	than the specified time or the specified time is zero.
 */
static inline struct ip_vs_sync_buff *
get_curr_sync_buff(struct netns_ipvs *ipvs, unsigned long time)
{
	struct ip_vs_sync_buff *sb;

	spin_lock_bh(&ipvs->sync_buff_lock);
	if (ipvs->sync_buff &&
	    time_after_eq(jiffies - ipvs->sync_buff->firstuse, time)) {
		sb = ipvs->sync_buff;
		ipvs->sync_buff = NULL;
	} else
		sb = NULL;
	spin_unlock_bh(&ipvs->sync_buff_lock);
	return sb;
}

/*
 * Switch mode from sending version 0 or 1
 *  - must handle sync_buf
 */
void ip_vs_sync_switch_mode(struct net *net, int mode)
{
	struct netns_ipvs *ipvs = net_ipvs(net);

	if (!(ipvs->sync_state & IP_VS_STATE_MASTER))
		return;
	if (mode == sysctl_sync_ver(ipvs) || !ipvs->sync_buff)
		return;

	spin_lock_bh(&ipvs->sync_buff_lock);
	/* Buffer empty ? then let buf_create do the job  */
	if (ipvs->sync_buff->mesg->size <=  sizeof(struct ip_vs_sync_mesg)) {
		kfree(ipvs->sync_buff);
		ipvs->sync_buff = NULL;
	} else {
		spin_lock_bh(&ipvs->sync_lock);
		if (ipvs->sync_state & IP_VS_STATE_MASTER)
			list_add_tail(&ipvs->sync_buff->list,
				      &ipvs->sync_queue);
		else
			ip_vs_sync_buff_release(ipvs->sync_buff);
		spin_unlock_bh(&ipvs->sync_lock);
	}
	spin_unlock_bh(&ipvs->sync_buff_lock);
}

/*
 * Create a new sync buffer for Version 0 proto.
 */
static inline struct ip_vs_sync_buff *
ip_vs_sync_buff_create_v0(struct netns_ipvs *ipvs)
{
	struct ip_vs_sync_buff *sb;
	struct ip_vs_sync_mesg_v0 *mesg;

	if (!(sb=kmalloc(sizeof(struct ip_vs_sync_buff), GFP_ATOMIC)))
		return NULL;

	sb->mesg = kmalloc(ipvs->send_mesg_maxlen, GFP_ATOMIC);
	if (!sb->mesg) {
		kfree(sb);
		return NULL;
	}
	mesg = (struct ip_vs_sync_mesg_v0 *)sb->mesg;
	mesg->nr_conns = 0;
	mesg->syncid = ipvs->master_syncid;
	mesg->size = sizeof(struct ip_vs_sync_mesg_v0);
	sb->head = (unsigned char *)mesg + sizeof(struct ip_vs_sync_mesg_v0);
	sb->end = (unsigned char *)mesg + ipvs->send_mesg_maxlen;
	sb->firstuse = jiffies;
	return sb;
}

/*
 *      Version 0 , could be switched in by sys_ctl.
 *      Add an ip_vs_conn information into the current sync_buff.
 */
void ip_vs_sync_conn_v0(struct net *net, struct ip_vs_conn *cp)
{
	struct netns_ipvs *ipvs = net_ipvs(net);
	struct ip_vs_sync_mesg_v0 *m;
	struct ip_vs_sync_conn_v0 *s;
	int len;

	if (unlikely(cp->af != AF_INET))
		return;
	/* Do not sync ONE PACKET */
	if (cp->flags & IP_VS_CONN_F_ONE_PACKET)
		return;

	spin_lock(&ipvs->sync_buff_lock);
	if (!ipvs->sync_buff) {
		ipvs->sync_buff =
			ip_vs_sync_buff_create_v0(ipvs);
		if (!ipvs->sync_buff) {
			spin_unlock(&ipvs->sync_buff_lock);
			pr_err("ip_vs_sync_buff_create failed.\n");
			return;
		}
	}

	len = (cp->flags & IP_VS_CONN_F_SEQ_MASK) ? FULL_CONN_SIZE :
		SIMPLE_CONN_SIZE;
	m = (struct ip_vs_sync_mesg_v0 *)ipvs->sync_buff->mesg;
	s = (struct ip_vs_sync_conn_v0 *)ipvs->sync_buff->head;

	/* copy members */
	s->reserved = 0;
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
	ipvs->sync_buff->head += len;

	/* check if there is a space for next one */
	if (ipvs->sync_buff->head + FULL_CONN_SIZE > ipvs->sync_buff->end) {
		sb_queue_tail(ipvs);
		ipvs->sync_buff = NULL;
	}
	spin_unlock(&ipvs->sync_buff_lock);

	/* synchronize its controller if it has */
	if (cp->control)
		ip_vs_sync_conn(net, cp->control);
}

/*
 *      Add an ip_vs_conn information into the current sync_buff.
 *      Called by ip_vs_in.
 *      Sending Version 1 messages
 */
void ip_vs_sync_conn(struct net *net, struct ip_vs_conn *cp)
{
	struct netns_ipvs *ipvs = net_ipvs(net);
	struct ip_vs_sync_mesg *m;
	union ip_vs_sync_conn *s;
	__u8 *p;
	unsigned int len, pe_name_len, pad;

	/* Handle old version of the protocol */
	if (sysctl_sync_ver(ipvs) == 0) {
		ip_vs_sync_conn_v0(net, cp);
		return;
	}
	/* Do not sync ONE PACKET */
	if (cp->flags & IP_VS_CONN_F_ONE_PACKET)
		goto control;
sloop:
	/* Sanity checks */
	pe_name_len = 0;
	if (cp->pe_data_len) {
		if (!cp->pe_data || !cp->dest) {
			IP_VS_ERR_RL("SYNC, connection pe_data invalid\n");
			return;
		}
		pe_name_len = strnlen(cp->pe->name, IP_VS_PENAME_MAXLEN);
	}

	spin_lock(&ipvs->sync_buff_lock);

#ifdef CONFIG_IP_VS_IPV6
	if (cp->af == AF_INET6)
		len = sizeof(struct ip_vs_sync_v6);
	else
#endif
		len = sizeof(struct ip_vs_sync_v4);

	if (cp->flags & IP_VS_CONN_F_SEQ_MASK)
		len += sizeof(struct ip_vs_sync_conn_options) + 2;

	if (cp->pe_data_len)
		len += cp->pe_data_len + 2;	/* + Param hdr field */
	if (pe_name_len)
		len += pe_name_len + 2;

	/* check if there is a space for this one  */
	pad = 0;
	if (ipvs->sync_buff) {
		pad = (4 - (size_t)ipvs->sync_buff->head) & 3;
		if (ipvs->sync_buff->head + len + pad > ipvs->sync_buff->end) {
			sb_queue_tail(ipvs);
			ipvs->sync_buff = NULL;
			pad = 0;
		}
	}

	if (!ipvs->sync_buff) {
		ipvs->sync_buff = ip_vs_sync_buff_create(ipvs);
		if (!ipvs->sync_buff) {
			spin_unlock(&ipvs->sync_buff_lock);
			pr_err("ip_vs_sync_buff_create failed.\n");
			return;
		}
	}

	m = ipvs->sync_buff->mesg;
	p = ipvs->sync_buff->head;
	ipvs->sync_buff->head += pad + len;
	m->size += pad + len;
	/* Add ev. padding from prev. sync_conn */
	while (pad--)
		*(p++) = 0;

	s = (union ip_vs_sync_conn *)p;

	/* Set message type  & copy members */
	s->v4.type = (cp->af == AF_INET6 ? STYPE_F_INET6 : 0);
	s->v4.ver_size = htons(len & SVER_MASK);	/* Version 0 */
	s->v4.flags = htonl(cp->flags & ~IP_VS_CONN_F_HASHED);
	s->v4.state = htons(cp->state);
	s->v4.protocol = cp->protocol;
	s->v4.cport = cp->cport;
	s->v4.vport = cp->vport;
	s->v4.dport = cp->dport;
	s->v4.fwmark = htonl(cp->fwmark);
	s->v4.timeout = htonl(cp->timeout / HZ);
	m->nr_conns++;

#ifdef CONFIG_IP_VS_IPV6
	if (cp->af == AF_INET6) {
		p += sizeof(struct ip_vs_sync_v6);
		s->v6.caddr = cp->caddr.in6;
		s->v6.vaddr = cp->vaddr.in6;
		s->v6.daddr = cp->daddr.in6;
	} else
#endif
	{
		p += sizeof(struct ip_vs_sync_v4);	/* options ptr */
		s->v4.caddr = cp->caddr.ip;
		s->v4.vaddr = cp->vaddr.ip;
		s->v4.daddr = cp->daddr.ip;
	}
	if (cp->flags & IP_VS_CONN_F_SEQ_MASK) {
		*(p++) = IPVS_OPT_SEQ_DATA;
		*(p++) = sizeof(struct ip_vs_sync_conn_options);
		hton_seq((struct ip_vs_seq *)p, &cp->in_seq);
		p += sizeof(struct ip_vs_seq);
		hton_seq((struct ip_vs_seq *)p, &cp->out_seq);
		p += sizeof(struct ip_vs_seq);
	}
	/* Handle pe data */
	if (cp->pe_data_len && cp->pe_data) {
		*(p++) = IPVS_OPT_PE_DATA;
		*(p++) = cp->pe_data_len;
		memcpy(p, cp->pe_data, cp->pe_data_len);
		p += cp->pe_data_len;
		if (pe_name_len) {
			/* Add PE_NAME */
			*(p++) = IPVS_OPT_PE_NAME;
			*(p++) = pe_name_len;
			memcpy(p, cp->pe->name, pe_name_len);
			p += pe_name_len;
		}
	}

	spin_unlock(&ipvs->sync_buff_lock);

control:
	/* synchronize its controller if it has */
	cp = cp->control;
	if (!cp)
		return;
	/*
	 * Reduce sync rate for templates
	 * i.e only increment in_pkts for Templates.
	 */
	if (cp->flags & IP_VS_CONN_F_TEMPLATE) {
		int pkts = atomic_add_return(1, &cp->in_pkts);

		if (pkts % sysctl_sync_period(ipvs) != 1)
			return;
	}
	goto sloop;
}

/*
 *  fill_param used by version 1
 */
static inline int
ip_vs_conn_fill_param_sync(struct net *net, int af, union ip_vs_sync_conn *sc,
			   struct ip_vs_conn_param *p,
			   __u8 *pe_data, unsigned int pe_data_len,
			   __u8 *pe_name, unsigned int pe_name_len)
{
#ifdef CONFIG_IP_VS_IPV6
	if (af == AF_INET6)
		ip_vs_conn_fill_param(net, af, sc->v6.protocol,
				      (const union nf_inet_addr *)&sc->v6.caddr,
				      sc->v6.cport,
				      (const union nf_inet_addr *)&sc->v6.vaddr,
				      sc->v6.vport, p);
	else
#endif
		ip_vs_conn_fill_param(net, af, sc->v4.protocol,
				      (const union nf_inet_addr *)&sc->v4.caddr,
				      sc->v4.cport,
				      (const union nf_inet_addr *)&sc->v4.vaddr,
				      sc->v4.vport, p);
	/* Handle pe data */
	if (pe_data_len) {
		if (pe_name_len) {
			char buff[IP_VS_PENAME_MAXLEN+1];

			memcpy(buff, pe_name, pe_name_len);
			buff[pe_name_len]=0;
			p->pe = __ip_vs_pe_getbyname(buff);
			if (!p->pe) {
				IP_VS_DBG(3, "BACKUP, no %s engine found/loaded\n",
					     buff);
				return 1;
			}
		} else {
			IP_VS_ERR_RL("BACKUP, Invalid PE parameters\n");
			return 1;
		}

		p->pe_data = kmemdup(pe_data, pe_data_len, GFP_ATOMIC);
		if (!p->pe_data) {
			if (p->pe->module)
				module_put(p->pe->module);
			return -ENOMEM;
		}
		p->pe_data_len = pe_data_len;
	}
	return 0;
}

/*
 *  Connection Add / Update.
 *  Common for version 0 and 1 reception of backup sync_conns.
 *  Param: ...
 *         timeout is in sec.
 */
static void ip_vs_proc_conn(struct net *net, struct ip_vs_conn_param *param,
			    unsigned int flags, unsigned int state,
			    unsigned int protocol, unsigned int type,
			    const union nf_inet_addr *daddr, __be16 dport,
			    unsigned long timeout, __u32 fwmark,
			    struct ip_vs_sync_conn_options *opt)
{
	struct ip_vs_dest *dest;
	struct ip_vs_conn *cp;
	struct netns_ipvs *ipvs = net_ipvs(net);

	if (!(flags & IP_VS_CONN_F_TEMPLATE))
		cp = ip_vs_conn_in_get(param);
	else
		cp = ip_vs_ct_in_get(param);

	if (cp && param->pe_data) 	/* Free pe_data */
		kfree(param->pe_data);
	if (!cp) {
		/*
		 * Find the appropriate destination for the connection.
		 * If it is not found the connection will remain unbound
		 * but still handled.
		 */
		dest = ip_vs_find_dest(net, type, daddr, dport, param->vaddr,
				       param->vport, protocol, fwmark, flags);

		/*  Set the approprite ativity flag */
		if (protocol == IPPROTO_TCP) {
			if (state != IP_VS_TCP_S_ESTABLISHED)
				flags |= IP_VS_CONN_F_INACTIVE;
			else
				flags &= ~IP_VS_CONN_F_INACTIVE;
		} else if (protocol == IPPROTO_SCTP) {
			if (state != IP_VS_SCTP_S_ESTABLISHED)
				flags |= IP_VS_CONN_F_INACTIVE;
			else
				flags &= ~IP_VS_CONN_F_INACTIVE;
		}
		cp = ip_vs_conn_new(param, daddr, dport, flags, dest, fwmark);
		if (dest)
			atomic_dec(&dest->refcnt);
		if (!cp) {
			if (param->pe_data)
				kfree(param->pe_data);
			IP_VS_DBG(2, "BACKUP, add new conn. failed\n");
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
	atomic_set(&cp->in_pkts, sysctl_sync_threshold(ipvs));
	cp->state = state;
	cp->old_state = cp->state;
	/*
	 * For Ver 0 messages style
	 *  - Not possible to recover the right timeout for templates
	 *  - can not find the right fwmark
	 *    virtual service. If needed, we can do it for
	 *    non-fwmark persistent services.
	 * Ver 1 messages style.
	 *  - No problem.
	 */
	if (timeout) {
		if (timeout > MAX_SCHEDULE_TIMEOUT / HZ)
			timeout = MAX_SCHEDULE_TIMEOUT / HZ;
		cp->timeout = timeout*HZ;
	} else {
		struct ip_vs_proto_data *pd;

		pd = ip_vs_proto_data_get(net, protocol);
		if (!(flags & IP_VS_CONN_F_TEMPLATE) && pd && pd->timeout_table)
			cp->timeout = pd->timeout_table[state];
		else
			cp->timeout = (3*60*HZ);
	}
	ip_vs_conn_put(cp);
}

/*
 *  Process received multicast message for Version 0
 */
static void ip_vs_process_message_v0(struct net *net, const char *buffer,
				     const size_t buflen)
{
	struct ip_vs_sync_mesg_v0 *m = (struct ip_vs_sync_mesg_v0 *)buffer;
	struct ip_vs_sync_conn_v0 *s;
	struct ip_vs_sync_conn_options *opt;
	struct ip_vs_protocol *pp;
	struct ip_vs_conn_param param;
	char *p;
	int i;

	p = (char *)buffer + sizeof(struct ip_vs_sync_mesg_v0);
	for (i=0; i<m->nr_conns; i++) {
		unsigned int flags, state;

		if (p + SIMPLE_CONN_SIZE > buffer+buflen) {
			IP_VS_ERR_RL("BACKUP v0, bogus conn\n");
			return;
		}
		s = (struct ip_vs_sync_conn_v0 *) p;
		flags = ntohs(s->flags) | IP_VS_CONN_F_SYNC;
		flags &= ~IP_VS_CONN_F_HASHED;
		if (flags & IP_VS_CONN_F_SEQ_MASK) {
			opt = (struct ip_vs_sync_conn_options *)&s[1];
			p += FULL_CONN_SIZE;
			if (p > buffer+buflen) {
				IP_VS_ERR_RL("BACKUP v0, Dropping buffer bogus conn options\n");
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
				IP_VS_DBG(2, "BACKUP v0, Unsupported protocol %u\n",
					s->protocol);
				continue;
			}
			if (state >= pp->num_states) {
				IP_VS_DBG(2, "BACKUP v0, Invalid %s state %u\n",
					pp->name, state);
				continue;
			}
		} else {
			/* protocol in templates is not used for state/timeout */
			if (state > 0) {
				IP_VS_DBG(2, "BACKUP v0, Invalid template state %u\n",
					state);
				state = 0;
			}
		}

		ip_vs_conn_fill_param(net, AF_INET, s->protocol,
				      (const union nf_inet_addr *)&s->caddr,
				      s->cport,
				      (const union nf_inet_addr *)&s->vaddr,
				      s->vport, &param);

		/* Send timeout as Zero */
		ip_vs_proc_conn(net, &param, flags, state, s->protocol, AF_INET,
				(union nf_inet_addr *)&s->daddr, s->dport,
				0, 0, opt);
	}
}

/*
 * Handle options
 */
static inline int ip_vs_proc_seqopt(__u8 *p, unsigned int plen,
				    __u32 *opt_flags,
				    struct ip_vs_sync_conn_options *opt)
{
	struct ip_vs_sync_conn_options *topt;

	topt = (struct ip_vs_sync_conn_options *)p;

	if (plen != sizeof(struct ip_vs_sync_conn_options)) {
		IP_VS_DBG(2, "BACKUP, bogus conn options length\n");
		return -EINVAL;
	}
	if (*opt_flags & IPVS_OPT_F_SEQ_DATA) {
		IP_VS_DBG(2, "BACKUP, conn options found twice\n");
		return -EINVAL;
	}
	ntoh_seq(&topt->in_seq, &opt->in_seq);
	ntoh_seq(&topt->out_seq, &opt->out_seq);
	*opt_flags |= IPVS_OPT_F_SEQ_DATA;
	return 0;
}

static int ip_vs_proc_str(__u8 *p, unsigned int plen, unsigned int *data_len,
			  __u8 **data, unsigned int maxlen,
			  __u32 *opt_flags, __u32 flag)
{
	if (plen > maxlen) {
		IP_VS_DBG(2, "BACKUP, bogus par.data len > %d\n", maxlen);
		return -EINVAL;
	}
	if (*opt_flags & flag) {
		IP_VS_DBG(2, "BACKUP, Par.data found twice 0x%x\n", flag);
		return -EINVAL;
	}
	*data_len = plen;
	*data = p;
	*opt_flags |= flag;
	return 0;
}
/*
 *   Process a Version 1 sync. connection
 */
static inline int ip_vs_proc_sync_conn(struct net *net, __u8 *p, __u8 *msg_end)
{
	struct ip_vs_sync_conn_options opt;
	union  ip_vs_sync_conn *s;
	struct ip_vs_protocol *pp;
	struct ip_vs_conn_param param;
	__u32 flags;
	unsigned int af, state, pe_data_len=0, pe_name_len=0;
	__u8 *pe_data=NULL, *pe_name=NULL;
	__u32 opt_flags=0;
	int retc=0;

	s = (union ip_vs_sync_conn *) p;

	if (s->v6.type & STYPE_F_INET6) {
#ifdef CONFIG_IP_VS_IPV6
		af = AF_INET6;
		p += sizeof(struct ip_vs_sync_v6);
#else
		IP_VS_DBG(3,"BACKUP, IPv6 msg received, and IPVS is not compiled for IPv6\n");
		retc = 10;
		goto out;
#endif
	} else if (!s->v4.type) {
		af = AF_INET;
		p += sizeof(struct ip_vs_sync_v4);
	} else {
		return -10;
	}
	if (p > msg_end)
		return -20;

	/* Process optional params check Type & Len. */
	while (p < msg_end) {
		int ptype;
		int plen;

		if (p+2 > msg_end)
			return -30;
		ptype = *(p++);
		plen  = *(p++);

		if (!plen || ((p + plen) > msg_end))
			return -40;
		/* Handle seq option  p = param data */
		switch (ptype & ~IPVS_OPT_F_PARAM) {
		case IPVS_OPT_SEQ_DATA:
			if (ip_vs_proc_seqopt(p, plen, &opt_flags, &opt))
				return -50;
			break;

		case IPVS_OPT_PE_DATA:
			if (ip_vs_proc_str(p, plen, &pe_data_len, &pe_data,
					   IP_VS_PEDATA_MAXLEN, &opt_flags,
					   IPVS_OPT_F_PE_DATA))
				return -60;
			break;

		case IPVS_OPT_PE_NAME:
			if (ip_vs_proc_str(p, plen,&pe_name_len, &pe_name,
					   IP_VS_PENAME_MAXLEN, &opt_flags,
					   IPVS_OPT_F_PE_NAME))
				return -70;
			break;

		default:
			/* Param data mandatory ? */
			if (!(ptype & IPVS_OPT_F_PARAM)) {
				IP_VS_DBG(3, "BACKUP, Unknown mandatory param %d found\n",
					  ptype & ~IPVS_OPT_F_PARAM);
				retc = 20;
				goto out;
			}
		}
		p += plen;  /* Next option */
	}

	/* Get flags and Mask off unsupported */
	flags  = ntohl(s->v4.flags) & IP_VS_CONN_F_BACKUP_MASK;
	flags |= IP_VS_CONN_F_SYNC;
	state = ntohs(s->v4.state);

	if (!(flags & IP_VS_CONN_F_TEMPLATE)) {
		pp = ip_vs_proto_get(s->v4.protocol);
		if (!pp) {
			IP_VS_DBG(3,"BACKUP, Unsupported protocol %u\n",
				s->v4.protocol);
			retc = 30;
			goto out;
		}
		if (state >= pp->num_states) {
			IP_VS_DBG(3, "BACKUP, Invalid %s state %u\n",
				pp->name, state);
			retc = 40;
			goto out;
		}
	} else {
		/* protocol in templates is not used for state/timeout */
		if (state > 0) {
			IP_VS_DBG(3, "BACKUP, Invalid template state %u\n",
				state);
			state = 0;
		}
	}
	if (ip_vs_conn_fill_param_sync(net, af, s, &param, pe_data,
				       pe_data_len, pe_name, pe_name_len)) {
		retc = 50;
		goto out;
	}
	/* If only IPv4, just silent skip IPv6 */
	if (af == AF_INET)
		ip_vs_proc_conn(net, &param, flags, state, s->v4.protocol, af,
				(union nf_inet_addr *)&s->v4.daddr, s->v4.dport,
				ntohl(s->v4.timeout), ntohl(s->v4.fwmark),
				(opt_flags & IPVS_OPT_F_SEQ_DATA ? &opt : NULL)
				);
#ifdef CONFIG_IP_VS_IPV6
	else
		ip_vs_proc_conn(net, &param, flags, state, s->v6.protocol, af,
				(union nf_inet_addr *)&s->v6.daddr, s->v6.dport,
				ntohl(s->v6.timeout), ntohl(s->v6.fwmark),
				(opt_flags & IPVS_OPT_F_SEQ_DATA ? &opt : NULL)
				);
#endif
	return 0;
	/* Error exit */
out:
	IP_VS_DBG(2, "BACKUP, Single msg dropped err:%d\n", retc);
	return retc;

}
/*
 *      Process received multicast message and create the corresponding
 *      ip_vs_conn entries.
 *      Handles Version 0 & 1
 */
static void ip_vs_process_message(struct net *net, __u8 *buffer,
				  const size_t buflen)
{
	struct netns_ipvs *ipvs = net_ipvs(net);
	struct ip_vs_sync_mesg *m2 = (struct ip_vs_sync_mesg *)buffer;
	__u8 *p, *msg_end;
	int i, nr_conns;

	if (buflen < sizeof(struct ip_vs_sync_mesg_v0)) {
		IP_VS_DBG(2, "BACKUP, message header too short\n");
		return;
	}
	/* Convert size back to host byte order */
	m2->size = ntohs(m2->size);

	if (buflen != m2->size) {
		IP_VS_DBG(2, "BACKUP, bogus message size\n");
		return;
	}
	/* SyncID sanity check */
	if (ipvs->backup_syncid != 0 && m2->syncid != ipvs->backup_syncid) {
		IP_VS_DBG(7, "BACKUP, Ignoring syncid = %d\n", m2->syncid);
		return;
	}
	/* Handle version 1  message */
	if ((m2->version == SYNC_PROTO_VER) && (m2->reserved == 0)
	    && (m2->spare == 0)) {

		msg_end = buffer + sizeof(struct ip_vs_sync_mesg);
		nr_conns = m2->nr_conns;

		for (i=0; i<nr_conns; i++) {
			union ip_vs_sync_conn *s;
			unsigned int size;
			int retc;

			p = msg_end;
			if (p + sizeof(s->v4) > buffer+buflen) {
				IP_VS_ERR_RL("BACKUP, Dropping buffer, to small\n");
				return;
			}
			s = (union ip_vs_sync_conn *)p;
			size = ntohs(s->v4.ver_size) & SVER_MASK;
			msg_end = p + size;
			/* Basic sanity checks */
			if (msg_end  > buffer+buflen) {
				IP_VS_ERR_RL("BACKUP, Dropping buffer, msg > buffer\n");
				return;
			}
			if (ntohs(s->v4.ver_size) >> SVER_SHIFT) {
				IP_VS_ERR_RL("BACKUP, Dropping buffer, Unknown version %d\n",
					      ntohs(s->v4.ver_size) >> SVER_SHIFT);
				return;
			}
			/* Process a single sync_conn */
			retc = ip_vs_proc_sync_conn(net, p, msg_end);
			if (retc < 0) {
				IP_VS_ERR_RL("BACKUP, Dropping buffer, Err: %d in decoding\n",
					     retc);
				return;
			}
			/* Make sure we have 32 bit alignment */
			msg_end = p + ((size + 3) & ~3);
		}
	} else {
		/* Old type of message */
		ip_vs_process_message_v0(net, buffer, buflen);
		return;
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
	struct net *net = sock_net(sk);

	dev = __dev_get_by_name(net, ifname);
	if (!dev)
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
static int set_sync_mesg_maxlen(struct net *net, int sync_state)
{
	struct netns_ipvs *ipvs = net_ipvs(net);
	struct net_device *dev;
	int num;

	if (sync_state == IP_VS_STATE_MASTER) {
		dev = __dev_get_by_name(net, ipvs->master_mcast_ifn);
		if (!dev)
			return -ENODEV;

		num = (dev->mtu - sizeof(struct iphdr) -
		       sizeof(struct udphdr) -
		       SYNC_MESG_HEADER_LEN - 20) / SIMPLE_CONN_SIZE;
		ipvs->send_mesg_maxlen = SYNC_MESG_HEADER_LEN +
			SIMPLE_CONN_SIZE * min(num, MAX_CONNS_PER_SYNCBUFF);
		IP_VS_DBG(7, "setting the maximum length of sync sending "
			  "message %d.\n", ipvs->send_mesg_maxlen);
	} else if (sync_state == IP_VS_STATE_BACKUP) {
		dev = __dev_get_by_name(net, ipvs->backup_mcast_ifn);
		if (!dev)
			return -ENODEV;

		ipvs->recv_mesg_maxlen = dev->mtu -
			sizeof(struct iphdr) - sizeof(struct udphdr);
		IP_VS_DBG(7, "setting the maximum length of sync receiving "
			  "message %d.\n", ipvs->recv_mesg_maxlen);
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
	struct net *net = sock_net(sk);
	struct ip_mreqn mreq;
	struct net_device *dev;
	int ret;

	memset(&mreq, 0, sizeof(mreq));
	memcpy(&mreq.imr_multiaddr, addr, sizeof(struct in_addr));

	dev = __dev_get_by_name(net, ifname);
	if (!dev)
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
	struct net *net = sock_net(sock->sk);
	struct net_device *dev;
	__be32 addr;
	struct sockaddr_in sin;

	dev = __dev_get_by_name(net, ifname);
	if (!dev)
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
static struct socket *make_send_sock(struct net *net)
{
	struct netns_ipvs *ipvs = net_ipvs(net);
	struct socket *sock;
	int result;

	/* First create a socket move it to right name space later */
	result = sock_create_kern(PF_INET, SOCK_DGRAM, IPPROTO_UDP, &sock);
	if (result < 0) {
		pr_err("Error during creation of socket; terminating\n");
		return ERR_PTR(result);
	}
	/*
	 * Kernel sockets that are a part of a namespace, should not
	 * hold a reference to a namespace in order to allow to stop it.
	 * After sk_change_net should be released using sk_release_kernel.
	 */
	sk_change_net(sock->sk, net);
	result = set_mcast_if(sock->sk, ipvs->master_mcast_ifn);
	if (result < 0) {
		pr_err("Error setting outbound mcast interface\n");
		goto error;
	}

	set_mcast_loop(sock->sk, 0);
	set_mcast_ttl(sock->sk, 1);

	result = bind_mcastif_addr(sock, ipvs->master_mcast_ifn);
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
	sk_release_kernel(sock->sk);
	return ERR_PTR(result);
}


/*
 *      Set up receiving multicast socket over UDP
 */
static struct socket *make_receive_sock(struct net *net)
{
	struct netns_ipvs *ipvs = net_ipvs(net);
	struct socket *sock;
	int result;

	/* First create a socket */
	result = sock_create_kern(PF_INET, SOCK_DGRAM, IPPROTO_UDP, &sock);
	if (result < 0) {
		pr_err("Error during creation of socket; terminating\n");
		return ERR_PTR(result);
	}
	/*
	 * Kernel sockets that are a part of a namespace, should not
	 * hold a reference to a namespace in order to allow to stop it.
	 * After sk_change_net should be released using sk_release_kernel.
	 */
	sk_change_net(sock->sk, net);
	/* it is equivalent to the REUSEADDR option in user-space */
	sock->sk->sk_reuse = SK_CAN_REUSE;

	result = sock->ops->bind(sock, (struct sockaddr *) &mcast_addr,
			sizeof(struct sockaddr));
	if (result < 0) {
		pr_err("Error binding to the multicast addr\n");
		goto error;
	}

	/* join the multicast group */
	result = join_mcast_group(sock->sk,
			(struct in_addr *) &mcast_addr.sin_addr,
			ipvs->backup_mcast_ifn);
	if (result < 0) {
		pr_err("Error joining to the multicast group\n");
		goto error;
	}

	return sock;

error:
	sk_release_kernel(sock->sk);
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
	struct netns_ipvs *ipvs = net_ipvs(tinfo->net);
	struct ip_vs_sync_buff *sb;

	pr_info("sync thread started: state = MASTER, mcast_ifn = %s, "
		"syncid = %d\n",
		ipvs->master_mcast_ifn, ipvs->master_syncid);

	while (!kthread_should_stop()) {
		while ((sb = sb_dequeue(ipvs))) {
			ip_vs_send_sync_msg(tinfo->sock, sb->mesg);
			ip_vs_sync_buff_release(sb);
		}

		/* check if entries stay in ipvs->sync_buff for 2 seconds */
		sb = get_curr_sync_buff(ipvs, 2 * HZ);
		if (sb) {
			ip_vs_send_sync_msg(tinfo->sock, sb->mesg);
			ip_vs_sync_buff_release(sb);
		}

		schedule_timeout_interruptible(HZ);
	}

	/* clean up the sync_buff queue */
	while ((sb = sb_dequeue(ipvs)))
		ip_vs_sync_buff_release(sb);

	/* clean up the current sync_buff */
	sb = get_curr_sync_buff(ipvs, 0);
	if (sb)
		ip_vs_sync_buff_release(sb);

	/* release the sending multicast socket */
	sk_release_kernel(tinfo->sock->sk);
	kfree(tinfo);

	return 0;
}


static int sync_thread_backup(void *data)
{
	struct ip_vs_sync_thread_data *tinfo = data;
	struct netns_ipvs *ipvs = net_ipvs(tinfo->net);
	int len;

	pr_info("sync thread started: state = BACKUP, mcast_ifn = %s, "
		"syncid = %d\n",
		ipvs->backup_mcast_ifn, ipvs->backup_syncid);

	while (!kthread_should_stop()) {
		wait_event_interruptible(*sk_sleep(tinfo->sock->sk),
			 !skb_queue_empty(&tinfo->sock->sk->sk_receive_queue)
			 || kthread_should_stop());

		/* do we have data now? */
		while (!skb_queue_empty(&(tinfo->sock->sk->sk_receive_queue))) {
			len = ip_vs_receive(tinfo->sock, tinfo->buf,
					ipvs->recv_mesg_maxlen);
			if (len <= 0) {
				pr_err("receiving message error\n");
				break;
			}

			/* disable bottom half, because it accesses the data
			   shared by softirq while getting/creating conns */
			local_bh_disable();
			ip_vs_process_message(tinfo->net, tinfo->buf, len);
			local_bh_enable();
		}
	}

	/* release the sending multicast socket */
	sk_release_kernel(tinfo->sock->sk);
	kfree(tinfo->buf);
	kfree(tinfo);

	return 0;
}


int start_sync_thread(struct net *net, int state, char *mcast_ifn, __u8 syncid)
{
	struct ip_vs_sync_thread_data *tinfo;
	struct task_struct **realtask, *task;
	struct socket *sock;
	struct netns_ipvs *ipvs = net_ipvs(net);
	char *name, *buf = NULL;
	int (*threadfn)(void *data);
	int result = -ENOMEM;

	IP_VS_DBG(7, "%s(): pid %d\n", __func__, task_pid_nr(current));
	IP_VS_DBG(7, "Each ip_vs_sync_conn entry needs %Zd bytes\n",
		  sizeof(struct ip_vs_sync_conn_v0));


	if (state == IP_VS_STATE_MASTER) {
		if (ipvs->master_thread)
			return -EEXIST;

		strlcpy(ipvs->master_mcast_ifn, mcast_ifn,
			sizeof(ipvs->master_mcast_ifn));
		ipvs->master_syncid = syncid;
		realtask = &ipvs->master_thread;
		name = "ipvs_master:%d";
		threadfn = sync_thread_master;
		sock = make_send_sock(net);
	} else if (state == IP_VS_STATE_BACKUP) {
		if (ipvs->backup_thread)
			return -EEXIST;

		strlcpy(ipvs->backup_mcast_ifn, mcast_ifn,
			sizeof(ipvs->backup_mcast_ifn));
		ipvs->backup_syncid = syncid;
		realtask = &ipvs->backup_thread;
		name = "ipvs_backup:%d";
		threadfn = sync_thread_backup;
		sock = make_receive_sock(net);
	} else {
		return -EINVAL;
	}

	if (IS_ERR(sock)) {
		result = PTR_ERR(sock);
		goto out;
	}

	set_sync_mesg_maxlen(net, state);
	if (state == IP_VS_STATE_BACKUP) {
		buf = kmalloc(ipvs->recv_mesg_maxlen, GFP_KERNEL);
		if (!buf)
			goto outsocket;
	}

	tinfo = kmalloc(sizeof(*tinfo), GFP_KERNEL);
	if (!tinfo)
		goto outbuf;

	tinfo->net = net;
	tinfo->sock = sock;
	tinfo->buf = buf;

	task = kthread_run(threadfn, tinfo, name, ipvs->gen);
	if (IS_ERR(task)) {
		result = PTR_ERR(task);
		goto outtinfo;
	}

	/* mark as active */
	*realtask = task;
	ipvs->sync_state |= state;

	/* increase the module use count */
	ip_vs_use_count_inc();

	return 0;

outtinfo:
	kfree(tinfo);
outbuf:
	kfree(buf);
outsocket:
	sk_release_kernel(sock->sk);
out:
	return result;
}


int stop_sync_thread(struct net *net, int state)
{
	struct netns_ipvs *ipvs = net_ipvs(net);
	int retc = -EINVAL;

	IP_VS_DBG(7, "%s(): pid %d\n", __func__, task_pid_nr(current));

	if (state == IP_VS_STATE_MASTER) {
		if (!ipvs->master_thread)
			return -ESRCH;

		pr_info("stopping master sync thread %d ...\n",
			task_pid_nr(ipvs->master_thread));

		/*
		 * The lock synchronizes with sb_queue_tail(), so that we don't
		 * add sync buffers to the queue, when we are already in
		 * progress of stopping the master sync daemon.
		 */

		spin_lock_bh(&ipvs->sync_lock);
		ipvs->sync_state &= ~IP_VS_STATE_MASTER;
		spin_unlock_bh(&ipvs->sync_lock);
		retc = kthread_stop(ipvs->master_thread);
		ipvs->master_thread = NULL;
	} else if (state == IP_VS_STATE_BACKUP) {
		if (!ipvs->backup_thread)
			return -ESRCH;

		pr_info("stopping backup sync thread %d ...\n",
			task_pid_nr(ipvs->backup_thread));

		ipvs->sync_state &= ~IP_VS_STATE_BACKUP;
		retc = kthread_stop(ipvs->backup_thread);
		ipvs->backup_thread = NULL;
	}

	/* decrease the module use count */
	ip_vs_use_count_dec();

	return retc;
}

/*
 * Initialize data struct for each netns
 */
int __net_init ip_vs_sync_net_init(struct net *net)
{
	struct netns_ipvs *ipvs = net_ipvs(net);

	__mutex_init(&ipvs->sync_mutex, "ipvs->sync_mutex", &__ipvs_sync_key);
	INIT_LIST_HEAD(&ipvs->sync_queue);
	spin_lock_init(&ipvs->sync_lock);
	spin_lock_init(&ipvs->sync_buff_lock);

	ipvs->sync_mcast_addr.sin_family = AF_INET;
	ipvs->sync_mcast_addr.sin_port = cpu_to_be16(IP_VS_SYNC_PORT);
	ipvs->sync_mcast_addr.sin_addr.s_addr = cpu_to_be32(IP_VS_SYNC_GROUP);
	return 0;
}

void ip_vs_sync_net_cleanup(struct net *net)
{
	int retc;
	struct netns_ipvs *ipvs = net_ipvs(net);

	mutex_lock(&ipvs->sync_mutex);
	retc = stop_sync_thread(net, IP_VS_STATE_MASTER);
	if (retc && retc != -ESRCH)
		pr_err("Failed to stop Master Daemon\n");

	retc = stop_sync_thread(net, IP_VS_STATE_BACKUP);
	if (retc && retc != -ESRCH)
		pr_err("Failed to stop Backup Daemon\n");
	mutex_unlock(&ipvs->sync_mutex);
}
