#ifndef _TRANSP_V6_H
#define _TRANSP_V6_H

#include <net/checksum.h>

/*
 *	IPv6 transport protocols
 */

#ifdef __KERNEL__

extern struct proto rawv6_prot;
extern struct proto udpv6_prot;
extern struct proto udplitev6_prot;
extern struct proto tcpv6_prot;

struct flowi;

/* extention headers */
extern int				ipv6_exthdrs_init(void);
extern void				ipv6_exthdrs_exit(void);
extern int				ipv6_frag_init(void);
extern void				ipv6_frag_exit(void);

/* transport protocols */
extern int				rawv6_init(void);
extern void				rawv6_exit(void);
extern int				udpv6_init(void);
extern void				udpv6_exit(void);
extern int 				udplitev6_init(void);
extern void 				udplitev6_exit(void);
extern int				tcpv6_init(void);
extern void				tcpv6_exit(void);

extern int				udpv6_connect(struct sock *sk,
						      struct sockaddr *uaddr,
						      int addr_len);

extern int			datagram_recv_ctl(struct sock *sk,
						  struct msghdr *msg,
						  struct sk_buff *skb);

extern int			datagram_send_ctl(struct net *net,
						  struct msghdr *msg,
						  struct flowi *fl,
						  struct ipv6_txoptions *opt,
						  int *hlimit, int *tclass,
						  int *dontfrag);

#define		LOOPBACK4_IPV6		cpu_to_be32(0x7f000006)

/*
 *	address family specific functions
 */
extern const struct inet_connection_sock_af_ops ipv4_specific;

extern void inet6_destroy_sock(struct sock *sk);

#endif

#endif
