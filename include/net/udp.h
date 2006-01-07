/*
 * INET		An implementation of the TCP/IP protocol suite for the LINUX
 *		operating system.  INET is implemented using the  BSD Socket
 *		interface as the means of communication with the user level.
 *
 *		Definitions for the UDP module.
 *
 * Version:	@(#)udp.h	1.0.2	05/07/93
 *
 * Authors:	Ross Biro
 *		Fred N. van Kempen, <waltje@uWalt.NL.Mugnet.ORG>
 *
 * Fixes:
 *		Alan Cox	: Turned on udp checksums. I don't want to
 *				  chase 'memory corruption' bugs that aren't!
 *
 *		This program is free software; you can redistribute it and/or
 *		modify it under the terms of the GNU General Public License
 *		as published by the Free Software Foundation; either version
 *		2 of the License, or (at your option) any later version.
 */
#ifndef _UDP_H
#define _UDP_H

#include <linux/list.h>
#include <net/inet_sock.h>
#include <net/sock.h>
#include <net/snmp.h>
#include <linux/seq_file.h>

#define UDP_HTABLE_SIZE		128

/* udp.c: This needs to be shared by v4 and v6 because the lookup
 *        and hashing code needs to work with different AF's yet
 *        the port space is shared.
 */
extern struct hlist_head udp_hash[UDP_HTABLE_SIZE];
extern rwlock_t udp_hash_lock;

extern int udp_port_rover;

static inline int udp_lport_inuse(u16 num)
{
	struct sock *sk;
	struct hlist_node *node;

	sk_for_each(sk, node, &udp_hash[num & (UDP_HTABLE_SIZE - 1)])
		if (inet_sk(sk)->num == num)
			return 1;
	return 0;
}

/* Note: this must match 'valbool' in sock_setsockopt */
#define UDP_CSUM_NOXMIT		1

/* Used by SunRPC/xprt layer. */
#define UDP_CSUM_NORCV		2

/* Default, as per the RFC, is to always do csums. */
#define UDP_CSUM_DEFAULT	0

extern struct proto udp_prot;

struct sk_buff;

extern void	udp_err(struct sk_buff *, u32);

extern int	udp_sendmsg(struct kiocb *iocb, struct sock *sk,
			    struct msghdr *msg, size_t len);

extern int	udp_rcv(struct sk_buff *skb);
extern int	udp_ioctl(struct sock *sk, int cmd, unsigned long arg);
extern int	udp_disconnect(struct sock *sk, int flags);
extern unsigned int udp_poll(struct file *file, struct socket *sock,
			     poll_table *wait);

DECLARE_SNMP_STAT(struct udp_mib, udp_statistics);
#define UDP_INC_STATS(field)		SNMP_INC_STATS(udp_statistics, field)
#define UDP_INC_STATS_BH(field)		SNMP_INC_STATS_BH(udp_statistics, field)
#define UDP_INC_STATS_USER(field) 	SNMP_INC_STATS_USER(udp_statistics, field)

/* /proc */
struct udp_seq_afinfo {
	struct module		*owner;
	char			*name;
	sa_family_t		family;
	int 			(*seq_show) (struct seq_file *m, void *v);
	struct file_operations	*seq_fops;
};

struct udp_iter_state {
	sa_family_t		family;
	int			bucket;
	struct seq_operations	seq_ops;
};

#ifdef CONFIG_PROC_FS
extern int udp_proc_register(struct udp_seq_afinfo *afinfo);
extern void udp_proc_unregister(struct udp_seq_afinfo *afinfo);

extern int  udp4_proc_init(void);
extern void udp4_proc_exit(void);
#endif
#endif	/* _UDP_H */
