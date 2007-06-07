/*
 * Access vector cache interface for object managers.
 *
 * Author : Stephen Smalley, <sds@epoch.ncsc.mil>
 */
#ifndef _SELINUX_AVC_H_
#define _SELINUX_AVC_H_

#include <linux/stddef.h>
#include <linux/errno.h>
#include <linux/kernel.h>
#include <linux/kdev_t.h>
#include <linux/spinlock.h>
#include <linux/init.h>
#include <linux/in6.h>
#include <asm/system.h>
#include "flask.h"
#include "av_permissions.h"
#include "security.h"

#ifdef CONFIG_SECURITY_SELINUX_DEVELOP
extern int selinux_enforcing;
#else
#define selinux_enforcing 1
#endif

/*
 * An entry in the AVC.
 */
struct avc_entry;

struct task_struct;
struct vfsmount;
struct dentry;
struct inode;
struct sock;
struct sk_buff;

/* Auxiliary data to use in generating the audit record. */
struct avc_audit_data {
	char    type;
#define AVC_AUDIT_DATA_FS   1
#define AVC_AUDIT_DATA_NET  2
#define AVC_AUDIT_DATA_CAP  3
#define AVC_AUDIT_DATA_IPC  4
	struct task_struct *tsk;
	union 	{
		struct {
			struct vfsmount *mnt;
			struct dentry *dentry;
			struct inode *inode;
		} fs;
		struct {
			char *netif;
			struct sock *sk;
			u16 family;
			__be16 dport;
			__be16 sport;
			union {
				struct {
					__be32 daddr;
					__be32 saddr;
				} v4;
				struct {
					struct in6_addr daddr;
					struct in6_addr saddr;
				} v6;
			} fam;
		} net;
		int cap;
		int ipc_id;
	} u;
};

#define v4info fam.v4
#define v6info fam.v6

/* Initialize an AVC audit data structure. */
#define AVC_AUDIT_DATA_INIT(_d,_t) \
        { memset((_d), 0, sizeof(struct avc_audit_data)); (_d)->type = AVC_AUDIT_DATA_##_t; }

/*
 * AVC statistics
 */
struct avc_cache_stats
{
	unsigned int lookups;
	unsigned int hits;
	unsigned int misses;
	unsigned int allocations;
	unsigned int reclaims;
	unsigned int frees;
};

/*
 * AVC operations
 */

void __init avc_init(void);

void avc_audit(u32 ssid, u32 tsid,
               u16 tclass, u32 requested,
               struct av_decision *avd, int result, struct avc_audit_data *auditdata);

#define AVC_STRICT 1 /* Ignore permissive mode. */
int avc_has_perm_noaudit(u32 ssid, u32 tsid,
			 u16 tclass, u32 requested,
			 unsigned flags,
			 struct av_decision *avd);

int avc_has_perm(u32 ssid, u32 tsid,
                 u16 tclass, u32 requested,
                 struct avc_audit_data *auditdata);

#define AVC_CALLBACK_GRANT		1
#define AVC_CALLBACK_TRY_REVOKE		2
#define AVC_CALLBACK_REVOKE		4
#define AVC_CALLBACK_RESET		8
#define AVC_CALLBACK_AUDITALLOW_ENABLE	16
#define AVC_CALLBACK_AUDITALLOW_DISABLE	32
#define AVC_CALLBACK_AUDITDENY_ENABLE	64
#define AVC_CALLBACK_AUDITDENY_DISABLE	128

int avc_add_callback(int (*callback)(u32 event, u32 ssid, u32 tsid,
                                     u16 tclass, u32 perms,
				     u32 *out_retained),
		     u32 events, u32 ssid, u32 tsid,
		     u16 tclass, u32 perms);

/* Exported to selinuxfs */
int avc_get_hash_stats(char *page);
extern unsigned int avc_cache_threshold;

#ifdef CONFIG_SECURITY_SELINUX_AVC_STATS
DECLARE_PER_CPU(struct avc_cache_stats, avc_cache_stats);
#endif

#endif /* _SELINUX_AVC_H_ */

