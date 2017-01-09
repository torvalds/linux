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

#define SMCPROTO_SMC		0	/* SMC protocol */

#define SMC_MAX_PORTS		2	/* Max # of ports */

enum smc_state {		/* possible states of an SMC socket */
	SMC_ACTIVE	= 1,
	SMC_INIT	= 2,
	SMC_CLOSED	= 7,
	SMC_LISTEN	= 10,
};

struct smc_sock {				/* smc sock container */
	struct sock		sk;
	struct socket		*clcsock;	/* internal tcp socket */
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

int smc_netinfo_by_tcpsk(struct socket *clcsock, __be32 *subnet,
			 u8 *prefix_len);

#endif	/* __SMC_H */
