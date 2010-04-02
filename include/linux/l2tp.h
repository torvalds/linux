/*
 * L2TP-over-IP socket for L2TPv3.
 *
 * Author: James Chapman <jchapman@katalix.com>
 */

#ifndef _LINUX_L2TP_H_
#define _LINUX_L2TP_H_

#include <linux/types.h>
#ifdef __KERNEL__
#include <linux/socket.h>
#include <linux/in.h>
#endif

#define IPPROTO_L2TP		115

/**
 * struct sockaddr_l2tpip - the sockaddr structure for L2TP-over-IP sockets
 * @l2tp_family:  address family number AF_L2TPIP.
 * @l2tp_addr:    protocol specific address information
 * @l2tp_conn_id: connection id of tunnel
 */
struct sockaddr_l2tpip {
	/* The first fields must match struct sockaddr_in */
	sa_family_t	l2tp_family;	/* AF_INET */
	__be16		l2tp_unused;	/* INET port number (unused) */
	struct in_addr	l2tp_addr;	/* Internet address */

	__u32		l2tp_conn_id;	/* Connection ID of tunnel */

	/* Pad to size of `struct sockaddr'. */
	unsigned char	__pad[sizeof(struct sockaddr) - sizeof(sa_family_t) -
			      sizeof(__be16) - sizeof(struct in_addr) -
			      sizeof(__u32)];
};

#endif
