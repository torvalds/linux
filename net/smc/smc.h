/*
 *  Shared Memory Communications over RDMA (SMC-R) and RoCE
 *
 *  Definitions for the SMC module (socket related)
 *
 *  Copyright IBM Corp. 2016
 *
 *  Author(s):  Ursula Braun <ubraun@linux.vnet.ibm.com>
 */
#ifndef __SMC_H
#define __SMC_H

#include <linux/socket.h>
#include <linux/types.h>
#include <net/sock.h>

#include "smc_ib.h"

#define SMCPROTO_SMC		0	/* SMC protocol */

#define SMC_MAX_PORTS		2	/* Max # of ports */

enum smc_state {		/* possible states of an SMC socket */
	SMC_ACTIVE	= 1,
	SMC_INIT	= 2,
	SMC_CLOSED	= 7,
	SMC_LISTEN	= 10,
};

struct smc_link_group;

struct smc_connection {
	struct rb_node		alert_node;
	struct smc_link_group	*lgr;		/* link group of connection */
	u32			alert_token_local; /* unique conn. id */
	u8			peer_conn_idx;	/* from tcp handshake */
};

struct smc_sock {				/* smc sock container */
	struct sock		sk;
	struct socket		*clcsock;	/* internal tcp socket */
	struct smc_connection	conn;		/* smc connection */
	struct sockaddr		*addr;		/* inet connect address */
	struct smc_sock		*listen_smc;	/* listen parent */
	struct work_struct	tcp_listen_work;/* handle tcp socket accepts */
	struct work_struct	smc_listen_work;/* prepare new accept socket */
	struct list_head	accept_q;	/* sockets to be accepted */
	spinlock_t		accept_q_lock;	/* protects accept_q */
	bool			use_fallback;	/* fallback to tcp */
};

static inline struct smc_sock *smc_sk(const struct sock *sk)
{
	return (struct smc_sock *)sk;
}

#define SMC_SYSTEMID_LEN		8

extern u8	local_systemid[SMC_SYSTEMID_LEN]; /* unique system identifier */

/* convert an u32 value into network byte order, store it into a 3 byte field */
static inline void hton24(u8 *net, u32 host)
{
	__be32 t;

	t = cpu_to_be32(host);
	memcpy(net, ((u8 *)&t) + 1, 3);
}

/* convert a received 3 byte field into host byte order*/
static inline u32 ntoh24(u8 *net)
{
	__be32 t = 0;

	memcpy(((u8 *)&t) + 1, net, 3);
	return be32_to_cpu(t);
}

#ifdef CONFIG_XFRM
static inline bool using_ipsec(struct smc_sock *smc)
{
	return (smc->clcsock->sk->sk_policy[0] ||
		smc->clcsock->sk->sk_policy[1]) ? 1 : 0;
}
#else
static inline bool using_ipsec(struct smc_sock *smc)
{
	return 0;
}
#endif

struct smc_clc_msg_local;

int smc_netinfo_by_tcpsk(struct socket *clcsock, __be32 *subnet,
			 u8 *prefix_len);
void smc_conn_free(struct smc_connection *conn);
int smc_conn_create(struct smc_sock *smc, __be32 peer_in_addr,
		    struct smc_ib_device *smcibdev, u8 ibport,
		    struct smc_clc_msg_local *lcl, int srv_first_contact);

#endif	/* __SMC_H */
