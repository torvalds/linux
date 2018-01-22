/* SPDX-License-Identifier: GPL-2.0 */
/*
 *  Shared Memory Communications over RDMA (SMC-R) and RoCE
 *
 *  Definitions for the SMC module (socket related)
 *
 *  Copyright IBM Corp. 2016
 *
 *  Author(s):  Ursula Braun <ubraun@linux.vnet.ibm.com>
 */
#ifndef _SMC_H
#define _SMC_H

struct smc_hashinfo {
	rwlock_t lock;
	struct hlist_head ht;
};

int smc_hash_sk(struct sock *sk);
void smc_unhash_sk(struct sock *sk);
#endif	/* _SMC_H */
