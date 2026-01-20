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

#include <linux/device.h>
#include <linux/spinlock.h>
#include <linux/types.h>
#include <linux/wait.h>
#include <linux/dibs.h>

struct tcp_sock;
struct inet_request_sock;
struct sock;

#define SMC_MAX_PNETID_LEN	16	/* Max. length of PNET id */

struct smc_hashinfo {
	rwlock_t lock;
	struct hlist_head ht;
};

/* SMCD/ISM device driver interface */
#define ISM_RESERVED_VLANID	0x1FFF

struct smcd_gid {
	u64	gid;
	u64	gid_ext;
};

struct smcd_dev {
	struct dibs_dev *dibs;
	struct list_head list;
	spinlock_t lock;
	struct smc_connection **conn;
	struct list_head vlan;
	struct workqueue_struct *event_wq;
	u8 pnetid[SMC_MAX_PNETID_LEN];
	bool pnetid_by_user;
	struct list_head lgr_list;
	spinlock_t lgr_lock;
	atomic_t lgr_cnt;
	wait_queue_head_t lgrs_deleted;
	u8 going_away : 1;
};

#define SMC_HS_CTRL_NAME_MAX 16

enum {
	/* ops can be inherit from init_net */
	SMC_HS_CTRL_FLAG_INHERITABLE = 0x1,

	SMC_HS_CTRL_ALL_FLAGS = SMC_HS_CTRL_FLAG_INHERITABLE,
};

struct smc_hs_ctrl {
	/* private */

	struct list_head list;
	struct module *owner;

	/* public */

	/* unique name */
	char name[SMC_HS_CTRL_NAME_MAX];
	int flags;

	/* Invoked before computing SMC option for SYN packets.
	 * We can control whether to set SMC options by returning various value.
	 * Return 0 to disable SMC, or return any other value to enable it.
	 */
	int (*syn_option)(struct tcp_sock *tp);

	/* Invoked before Set up SMC options for SYN-ACK packets
	 * We can control whether to respond SMC options by returning various
	 * value. Return 0 to disable SMC, or return any other value to enable
	 * it.
	 */
	int (*synack_option)(const struct tcp_sock *tp,
			     struct inet_request_sock *ireq);
};

#if IS_ENABLED(CONFIG_SMC_HS_CTRL_BPF)
#define smc_call_hsbpf(init_val, tp, func, ...) ({				\
	typeof(init_val) __ret = (init_val);					\
	struct smc_hs_ctrl *ctrl;						\
	rcu_read_lock();							\
	ctrl = rcu_dereference(sock_net((struct sock *)(tp))->smc.hs_ctrl);	\
	if (ctrl && ctrl->func)							\
		__ret = ctrl->func(tp, ##__VA_ARGS__);				\
	rcu_read_unlock();							\
	__ret;									\
})
#else
#define smc_call_hsbpf(init_val, tp, ...)  ({ (void)(tp); (init_val); })
#endif /* CONFIG_SMC_HS_CTRL_BPF */

#endif	/* _SMC_H */
