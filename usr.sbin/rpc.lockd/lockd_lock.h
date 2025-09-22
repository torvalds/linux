/*	$OpenBSD: lockd_lock.h,v 1.2 2008/06/13 21:34:24 sturm Exp $	*/

/* Headers and function declarations for file-locking utilities */

struct nlm4_holder *testlock(struct nlm4_lock *, int);

enum nlm_stats getlock(nlm4_lockargs *, struct svc_req *, int);
enum nlm_stats unlock(nlm4_lock *, int);
void notify(const char *, int);

/* flags for testlock, getlock & unlock */
#define LOCK_ASYNC	0x01 /* async version (getlock only) */
#define LOCK_V4 	0x02 /* v4 version */
#define LOCK_MON 	0x04 /* monitored lock (getlock only) */
#define LOCK_CANCEL 0x08 /* cancel, not unlock request (unlock only) */

/* callbacks from lock_proc.c */
void	transmit_result(int, nlm_res *, struct sockaddr_in *);
void	transmit4_result(int, nlm4_res *, struct sockaddr_in *);
CLIENT  *get_client(struct sockaddr_in *, u_long);
