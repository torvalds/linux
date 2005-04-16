/*
 * linux/include/linux/lockd/sm_inter.h
 *
 * Declarations for the kernel statd client.
 *
 * Copyright (C) 1996, Olaf Kirch <okir@monad.swb.de>
 */

#ifndef LINUX_LOCKD_SM_INTER_H
#define LINUX_LOCKD_SM_INTER_H

#define SM_PROGRAM	100024
#define SM_VERSION	1
#define SM_STAT		1
#define SM_MON		2
#define SM_UNMON	3
#define SM_UNMON_ALL	4
#define SM_SIMU_CRASH	5
#define SM_NOTIFY	6

#define SM_MAXSTRLEN	1024

/*
 * Arguments for all calls to statd
 */
struct nsm_args {
	u32		addr;		/* remote address */
	u32		prog;		/* RPC callback info */
	u32		vers;
	u32		proc;
	u32		proto;		/* protocol (udp/tcp) plus server/client flag */
};

/*
 * Result returned by statd
 */
struct nsm_res {
	u32		status;
	u32		state;
};

int		nsm_monitor(struct nlm_host *);
int		nsm_unmonitor(struct nlm_host *);
extern u32	nsm_local_state;

#endif /* LINUX_LOCKD_SM_INTER_H */
