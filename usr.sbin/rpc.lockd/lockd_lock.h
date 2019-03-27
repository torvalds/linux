/*	$NetBSD: lockd_lock.h,v 1.2 2000/06/09 14:00:54 fvdl Exp $	*/
/*	$FreeBSD$ */

/* Headers and function declarations for file-locking utilities */

struct nlm4_holder *	testlock(struct nlm4_lock *lock, bool_t exclusive,
    int flags);
enum nlm_stats	getlock(nlm4_lockargs *lckarg, struct svc_req *rqstp,
    const int flags);
enum nlm_stats	unlock(nlm4_lock *lock, const int flags);
int	lock_answer(int pid, netobj *netcookie, int result, int *pid_p,
    int version);

void notify(const char *hostname, const int state);

/* flags for testlock, getlock & unlock */
#define LOCK_ASYNC	0x01 /* async version (getlock only) */
#define LOCK_V4 	0x02 /* v4 version */
#define LOCK_MON 	0x04 /* monitored lock (getlock only) */
#define LOCK_CANCEL 0x08 /* cancel, not unlock request (unlock only) */

/* callbacks from lock_proc.c */
void	transmit_result(int, nlm_res *, struct sockaddr *);
void	transmit4_result(int, nlm4_res *, struct sockaddr *);
CLIENT  *get_client(struct sockaddr *, rpcvers_t);
