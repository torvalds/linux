/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Entry points by which the knfsd core drives the optional NFSv4
 * subsystem: state lifecycle, the laundromat workqueue, the recovery
 * directory, junctions, the CLD notifier, and leases-net setup.
 *
 * Separated from nfsd.h so that the many translation units that
 * include nfsd.h but call none of these -- among them the NFSv2 and
 * NFSv3 paths -- do not have to parse them. The CONFIG_NFSD_V4=n
 * stubs let the version-agnostic callers invoke the routines
 * unconditionally.
 */

#ifndef LINUX_NFSD_NFS4CTL_H
#define LINUX_NFSD_NFS4CTL_H

#include <linux/stddef.h>
#include <linux/types.h>

struct net;
struct inode;
struct dentry;
struct svc_rqst;
struct nfsd_net;

#ifdef CONFIG_NFSD_V4
extern unsigned long max_delegations;
int nfsd4_init_slabs(void);
void nfsd4_free_slabs(void);
int nfs4_state_start(void);
int nfs4_state_start_net(struct net *net);
void nfs4_state_shutdown(void);
void nfs4_state_shutdown_net(struct net *net);
int nfs4_reset_recoverydir(char *recdir);
char * nfs4_recoverydir(void);
bool nfsd4_spo_must_allow(struct svc_rqst *rqstp);
int nfsd4_create_laundry_wq(void);
void nfsd4_destroy_laundry_wq(void);
bool nfsd_wait_for_delegreturn(struct svc_rqst *rqstp, struct inode *inode);

extern int nfsd4_is_junction(struct dentry *dentry);
extern int register_cld_notifier(void);
extern void unregister_cld_notifier(void);
#ifdef CONFIG_NFSD_V4_2_INTER_SSC
extern void nfsd4_ssc_init_umount_work(struct nfsd_net *nn);
#endif

extern void nfsd4_init_leases_net(struct nfsd_net *nn);

#else /* CONFIG_NFSD_V4 */
static inline int nfsd4_init_slabs(void) { return 0; }
static inline void nfsd4_free_slabs(void) { }
static inline int nfs4_state_start(void) { return 0; }
static inline int nfs4_state_start_net(struct net *net) { return 0; }
static inline void nfs4_state_shutdown(void) { }
static inline void nfs4_state_shutdown_net(struct net *net) { }
static inline int nfs4_reset_recoverydir(char *recdir) { return 0; }
static inline char * nfs4_recoverydir(void) {return NULL; }
static inline bool nfsd4_spo_must_allow(struct svc_rqst *rqstp)
{
	return false;
}
static inline int nfsd4_create_laundry_wq(void) { return 0; };
static inline void nfsd4_destroy_laundry_wq(void) {};
static inline bool nfsd_wait_for_delegreturn(struct svc_rqst *rqstp,
					      struct inode *inode)
{
	return false;
}

static inline int nfsd4_is_junction(struct dentry *dentry)
{
	return 0;
}

static inline void nfsd4_init_leases_net(struct nfsd_net *nn) { };

#define register_cld_notifier() 0
#define unregister_cld_notifier() do { } while(0)

#endif /* CONFIG_NFSD_V4 */

#endif /* LINUX_NFSD_NFS4CTL_H */
