/*
 *  linux/include/linux/sunrpc/xprtsock.h
 *
 *  Declarations for the RPC transport socket provider.
 */

#ifndef _LINUX_SUNRPC_XPRTSOCK_H
#define _LINUX_SUNRPC_XPRTSOCK_H

#ifdef __KERNEL__

/*
 * Socket transport setup operations
 */
struct rpc_xprt *xs_setup_udp(struct xprt_create *args);
struct rpc_xprt *xs_setup_tcp(struct xprt_create *args);

int		init_socket_xprt(void);
void		cleanup_socket_xprt(void);

/*
 * RPC transport identifiers for UDP, TCP
 *
 * To preserve compatibility with the historical use of raw IP protocol
 * id's for transport selection, these are specified with the previous
 * values. No such restriction exists for new transports, except that
 * they may not collide with these values (17 and 6, respectively).
 */
#define XPRT_TRANSPORT_UDP	IPPROTO_UDP
#define XPRT_TRANSPORT_TCP	IPPROTO_TCP

/*
 * RPC slot table sizes for UDP, TCP transports
 */
extern unsigned int xprt_udp_slot_table_entries;
extern unsigned int xprt_tcp_slot_table_entries;

/*
 * Parameters for choosing a free port
 */
extern unsigned int xprt_min_resvport;
extern unsigned int xprt_max_resvport;

#define RPC_MIN_RESVPORT	(1U)
#define RPC_MAX_RESVPORT	(65535U)
#define RPC_DEF_MIN_RESVPORT	(665U)
#define RPC_DEF_MAX_RESVPORT	(1023U)

#endif /* __KERNEL__ */

#endif /* _LINUX_SUNRPC_XPRTSOCK_H */
