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
#include <linux/audit.h>
#include <linux/lsm_audit.h>
#include <linux/in6.h>
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
struct inode;
struct sock;
struct sk_buff;

/*
 * AVC statistics
 */
struct avc_cache_stats {
	unsigned int lookups;
	unsigned int misses;
	unsigned int allocations;
	unsigned int reclaims;
	unsigned int frees;
};

/*
 * We only need this data after we have decided to send an audit message.
 */
struct selinux_late_audit_data {
	u32 ssid;
	u32 tsid;
	u16 tclass;
	u32 requested;
	u32 audited;
	u32 denied;
	int result;
};

/*
 * We collect this at the beginning or during an selinux security operation
 */
struct selinux_audit_data {
	/*
	 * auditdeny is a bit tricky and unintuitive.  See the
	 * comments in avc.c for it's meaning and usage.
	 */
	u32 auditdeny;
	struct selinux_late_audit_data *slad;
};

/*
 * AVC operations
 */

void __init avc_init(void);

int avc_audit(u32 ssid, u32 tsid,
	       u16 tclass, u32 requested,
	       struct av_decision *avd,
	       int result,
	      struct common_audit_data *a, unsigned flags);

#define AVC_STRICT 1 /* Ignore permissive mode. */
int avc_has_perm_noaudit(u32 ssid, u32 tsid,
			 u16 tclass, u32 requested,
			 unsigned flags,
			 struct av_decision *avd);

int avc_has_perm_flags(u32 ssid, u32 tsid,
		       u16 tclass, u32 requested,
		       struct common_audit_data *auditdata,
		       unsigned);

static inline int avc_has_perm(u32 ssid, u32 tsid,
			       u16 tclass, u32 requested,
			       struct common_audit_data *auditdata)
{
	return avc_has_perm_flags(ssid, tsid, tclass, requested, auditdata, 0);
}

u32 avc_policy_seqno(void);

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

/* Attempt to free avc node cache */
void avc_disable(void);

#ifdef CONFIG_SECURITY_SELINUX_AVC_STATS
DECLARE_PER_CPU(struct avc_cache_stats, avc_cache_stats);
#endif

#endif /* _SELINUX_AVC_H_ */

