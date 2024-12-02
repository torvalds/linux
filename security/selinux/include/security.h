/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Security server interface.
 *
 * Author : Stephen Smalley, <stephen.smalley.work@gmail.com>
 *
 */

#ifndef _SELINUX_SECURITY_H_
#define _SELINUX_SECURITY_H_

#include <linux/compiler.h>
#include <linux/dcache.h>
#include <linux/magic.h>
#include <linux/types.h>
#include <linux/rcupdate.h>
#include <linux/refcount.h>
#include <linux/workqueue.h>
#include <linux/delay.h>
#include <linux/printk.h>
#include "flask.h"
#include "policycap.h"

#define SECSID_NULL   0x00000000 /* unspecified SID */
#define SECSID_WILD   0xffffffff /* wildcard SID */
#define SECCLASS_NULL 0x0000 /* no class */

/* Identify specific policy version changes */
#define POLICYDB_VERSION_BASE		     15
#define POLICYDB_VERSION_BOOL		     16
#define POLICYDB_VERSION_IPV6		     17
#define POLICYDB_VERSION_NLCLASS	     18
#define POLICYDB_VERSION_VALIDATETRANS	     19
#define POLICYDB_VERSION_MLS		     19
#define POLICYDB_VERSION_AVTAB		     20
#define POLICYDB_VERSION_RANGETRANS	     21
#define POLICYDB_VERSION_POLCAP		     22
#define POLICYDB_VERSION_PERMISSIVE	     23
#define POLICYDB_VERSION_BOUNDARY	     24
#define POLICYDB_VERSION_FILENAME_TRANS	     25
#define POLICYDB_VERSION_ROLETRANS	     26
#define POLICYDB_VERSION_NEW_OBJECT_DEFAULTS 27
#define POLICYDB_VERSION_DEFAULT_TYPE	     28
#define POLICYDB_VERSION_CONSTRAINT_NAMES    29
#define POLICYDB_VERSION_XPERMS_IOCTL	     30
#define POLICYDB_VERSION_INFINIBAND	     31
#define POLICYDB_VERSION_GLBLUB		     32
#define POLICYDB_VERSION_COMP_FTRANS	     33 /* compressed filename transitions */

/* Range of policy versions we understand*/
#define POLICYDB_VERSION_MIN POLICYDB_VERSION_BASE
#define POLICYDB_VERSION_MAX POLICYDB_VERSION_COMP_FTRANS

/* Mask for just the mount related flags */
#define SE_MNTMASK 0x0f
/* Super block security struct flags for mount options */
/* BE CAREFUL, these need to be the low order bits for selinux_get_mnt_opts */
#define CONTEXT_MNT	0x01
#define FSCONTEXT_MNT	0x02
#define ROOTCONTEXT_MNT 0x04
#define DEFCONTEXT_MNT	0x08
#define SBLABEL_MNT	0x10
/* Non-mount related flags */
#define SE_SBINITIALIZED 0x0100
#define SE_SBPROC	 0x0200
#define SE_SBGENFS	 0x0400
#define SE_SBGENFS_XATTR 0x0800
#define SE_SBNATIVE	 0x1000

#define CONTEXT_STR	"context"
#define FSCONTEXT_STR	"fscontext"
#define ROOTCONTEXT_STR "rootcontext"
#define DEFCONTEXT_STR	"defcontext"
#define SECLABEL_STR	"seclabel"

struct netlbl_lsm_secattr;

extern int selinux_enabled_boot;

/*
 * type_datum properties
 * available at the kernel policy version >= POLICYDB_VERSION_BOUNDARY
 */
#define TYPEDATUM_PROPERTY_PRIMARY   0x0001
#define TYPEDATUM_PROPERTY_ATTRIBUTE 0x0002

/* limitation of boundary depth  */
#define POLICYDB_BOUNDS_MAXDEPTH 4

struct selinux_policy;

struct selinux_state {
#ifdef CONFIG_SECURITY_SELINUX_DEVELOP
	bool enforcing;
#endif
	bool initialized;
	bool policycap[__POLICYDB_CAP_MAX];

	struct page *status_page;
	struct mutex status_lock;

	struct selinux_policy __rcu *policy;
	struct mutex policy_mutex;
} __randomize_layout;

void selinux_avc_init(void);

extern struct selinux_state selinux_state;

static inline bool selinux_initialized(void)
{
	/* do a synchronized load to avoid race conditions */
	return smp_load_acquire(&selinux_state.initialized);
}

static inline void selinux_mark_initialized(void)
{
	/* do a synchronized write to avoid race conditions */
	smp_store_release(&selinux_state.initialized, true);
}

#ifdef CONFIG_SECURITY_SELINUX_DEVELOP
static inline bool enforcing_enabled(void)
{
	return READ_ONCE(selinux_state.enforcing);
}

static inline void enforcing_set(bool value)
{
	WRITE_ONCE(selinux_state.enforcing, value);
}
#else
static inline bool enforcing_enabled(void)
{
	return true;
}

static inline void enforcing_set(bool value)
{
}
#endif

static inline bool checkreqprot_get(void)
{
	/* non-zero/true checkreqprot values are no longer supported */
	return 0;
}

static inline bool selinux_policycap_netpeer(void)
{
	return READ_ONCE(selinux_state.policycap[POLICYDB_CAP_NETPEER]);
}

static inline bool selinux_policycap_openperm(void)
{
	return READ_ONCE(selinux_state.policycap[POLICYDB_CAP_OPENPERM]);
}

static inline bool selinux_policycap_extsockclass(void)
{
	return READ_ONCE(selinux_state.policycap[POLICYDB_CAP_EXTSOCKCLASS]);
}

static inline bool selinux_policycap_alwaysnetwork(void)
{
	return READ_ONCE(selinux_state.policycap[POLICYDB_CAP_ALWAYSNETWORK]);
}

static inline bool selinux_policycap_cgroupseclabel(void)
{
	return READ_ONCE(selinux_state.policycap[POLICYDB_CAP_CGROUPSECLABEL]);
}

static inline bool selinux_policycap_nnp_nosuid_transition(void)
{
	return READ_ONCE(
		selinux_state.policycap[POLICYDB_CAP_NNP_NOSUID_TRANSITION]);
}

static inline bool selinux_policycap_genfs_seclabel_symlinks(void)
{
	return READ_ONCE(
		selinux_state.policycap[POLICYDB_CAP_GENFS_SECLABEL_SYMLINKS]);
}

static inline bool selinux_policycap_ioctl_skip_cloexec(void)
{
	return READ_ONCE(
		selinux_state.policycap[POLICYDB_CAP_IOCTL_SKIP_CLOEXEC]);
}

static inline bool selinux_policycap_userspace_initial_context(void)
{
	return READ_ONCE(
		selinux_state.policycap[POLICYDB_CAP_USERSPACE_INITIAL_CONTEXT]);
}

static inline bool selinux_policycap_netlink_xperm(void)
{
	return READ_ONCE(
		selinux_state.policycap[POLICYDB_CAP_NETLINK_XPERM]);
}

struct selinux_policy_convert_data;

struct selinux_load_state {
	struct selinux_policy *policy;
	struct selinux_policy_convert_data *convert_data;
};

int security_mls_enabled(void);
int security_load_policy(void *data, size_t len,
			 struct selinux_load_state *load_state);
void selinux_policy_commit(struct selinux_load_state *load_state);
void selinux_policy_cancel(struct selinux_load_state *load_state);
int security_read_policy(void **data, size_t *len);
int security_read_state_kernel(void **data, size_t *len);
int security_policycap_supported(unsigned int req_cap);

#define SEL_VEC_MAX 32
struct av_decision {
	u32 allowed;
	u32 auditallow;
	u32 auditdeny;
	u32 seqno;
	u32 flags;
};

#define XPERMS_ALLOWED	  1
#define XPERMS_AUDITALLOW 2
#define XPERMS_DONTAUDIT  4

#define security_xperm_set(perms, x)  ((perms)[(x) >> 5] |= 1 << ((x)&0x1f))
#define security_xperm_test(perms, x) (1 & ((perms)[(x) >> 5] >> ((x)&0x1f)))
struct extended_perms_data {
	u32 p[8];
};

struct extended_perms_decision {
	u8 used;
	u8 driver;
	struct extended_perms_data *allowed;
	struct extended_perms_data *auditallow;
	struct extended_perms_data *dontaudit;
};

struct extended_perms {
	u16 len; /* length associated decision chain */
	struct extended_perms_data drivers; /* flag drivers that are used */
};

/* definitions of av_decision.flags */
#define AVD_FLAGS_PERMISSIVE 0x0001

void security_compute_av(u32 ssid, u32 tsid, u16 tclass,
			 struct av_decision *avd,
			 struct extended_perms *xperms);

void security_compute_xperms_decision(u32 ssid, u32 tsid, u16 tclass, u8 driver,
				      struct extended_perms_decision *xpermd);

void security_compute_av_user(u32 ssid, u32 tsid, u16 tclass,
			      struct av_decision *avd);

int security_transition_sid(u32 ssid, u32 tsid, u16 tclass,
			    const struct qstr *qstr, u32 *out_sid);

int security_transition_sid_user(u32 ssid, u32 tsid, u16 tclass,
				 const char *objname, u32 *out_sid);

int security_member_sid(u32 ssid, u32 tsid, u16 tclass, u32 *out_sid);

int security_change_sid(u32 ssid, u32 tsid, u16 tclass, u32 *out_sid);

int security_sid_to_context(u32 sid, char **scontext, u32 *scontext_len);

int security_sid_to_context_force(u32 sid, char **scontext, u32 *scontext_len);

int security_sid_to_context_inval(u32 sid, char **scontext, u32 *scontext_len);

int security_context_to_sid(const char *scontext, u32 scontext_len,
			    u32 *out_sid, gfp_t gfp);

int security_context_str_to_sid(const char *scontext, u32 *out_sid, gfp_t gfp);

int security_context_to_sid_default(const char *scontext, u32 scontext_len,
				    u32 *out_sid, u32 def_sid, gfp_t gfp_flags);

int security_context_to_sid_force(const char *scontext, u32 scontext_len,
				  u32 *sid);

int security_get_user_sids(u32 callsid, char *username, u32 **sids, u32 *nel);

int security_port_sid(u8 protocol, u16 port, u32 *out_sid);

int security_ib_pkey_sid(u64 subnet_prefix, u16 pkey_num, u32 *out_sid);

int security_ib_endport_sid(const char *dev_name, u8 port_num, u32 *out_sid);

int security_netif_sid(char *name, u32 *if_sid);

int security_node_sid(u16 domain, void *addr, u32 addrlen, u32 *out_sid);

int security_validate_transition(u32 oldsid, u32 newsid, u32 tasksid,
				 u16 tclass);

int security_validate_transition_user(u32 oldsid, u32 newsid, u32 tasksid,
				      u16 tclass);

int security_bounded_transition(u32 oldsid, u32 newsid);

int security_sid_mls_copy(u32 sid, u32 mls_sid, u32 *new_sid);

int security_net_peersid_resolve(u32 nlbl_sid, u32 nlbl_type, u32 xfrm_sid,
				 u32 *peer_sid);

int security_get_classes(struct selinux_policy *policy, char ***classes,
			 u32 *nclasses);
int security_get_permissions(struct selinux_policy *policy, const char *class,
			     char ***perms, u32 *nperms);
int security_get_reject_unknown(void);
int security_get_allow_unknown(void);

#define SECURITY_FS_USE_XATTR	 1 /* use xattr */
#define SECURITY_FS_USE_TRANS	 2 /* use transition SIDs, e.g. devpts/tmpfs */
#define SECURITY_FS_USE_TASK	 3 /* use task SIDs, e.g. pipefs/sockfs */
#define SECURITY_FS_USE_GENFS	 4 /* use the genfs support */
#define SECURITY_FS_USE_NONE	 5 /* no labeling support */
#define SECURITY_FS_USE_MNTPOINT 6 /* use mountpoint labeling */
#define SECURITY_FS_USE_NATIVE	 7 /* use native label support */
#define SECURITY_FS_USE_MAX	 7 /* Highest SECURITY_FS_USE_XXX */

int security_fs_use(struct super_block *sb);

int security_genfs_sid(const char *fstype, const char *path, u16 sclass,
		       u32 *sid);

int selinux_policy_genfs_sid(struct selinux_policy *policy, const char *fstype,
			     const char *path, u16 sclass, u32 *sid);

#ifdef CONFIG_NETLABEL
int security_netlbl_secattr_to_sid(struct netlbl_lsm_secattr *secattr,
				   u32 *sid);

int security_netlbl_sid_to_secattr(u32 sid, struct netlbl_lsm_secattr *secattr);
#else
static inline int
security_netlbl_secattr_to_sid(struct netlbl_lsm_secattr *secattr, u32 *sid)
{
	return -EIDRM;
}

static inline int
security_netlbl_sid_to_secattr(u32 sid, struct netlbl_lsm_secattr *secattr)
{
	return -ENOENT;
}
#endif /* CONFIG_NETLABEL */

const char *security_get_initial_sid_context(u32 sid);

/*
 * status notifier using mmap interface
 */
extern struct page *selinux_kernel_status_page(void);

#define SELINUX_KERNEL_STATUS_VERSION 1
struct selinux_kernel_status {
	u32 version; /* version number of the structure */
	u32 sequence; /* sequence number of seqlock logic */
	u32 enforcing; /* current setting of enforcing mode */
	u32 policyload; /* times of policy reloaded */
	u32 deny_unknown; /* current setting of deny_unknown */
	/*
	 * The version > 0 supports above members.
	 */
} __packed;

extern void selinux_status_update_setenforce(bool enforcing);
extern void selinux_status_update_policyload(u32 seqno);
extern void selinux_complete_init(void);
extern struct path selinux_null;
extern void selnl_notify_setenforce(int val);
extern void selnl_notify_policyload(u32 seqno);
extern int selinux_nlmsg_lookup(u16 sclass, u16 nlmsg_type, u32 *perm);

extern void avtab_cache_init(void);
extern void ebitmap_cache_init(void);
extern void hashtab_cache_init(void);
extern int security_sidtab_hash_stats(char *page);

#endif /* _SELINUX_SECURITY_H_ */
