/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Security server interface.
 *
 * Author : Stephen Smalley, <sds@tycho.nsa.gov>
 *
 */

#ifndef _SELINUX_SECURITY_H_
#define _SELINUX_SECURITY_H_

#include <linux/compiler.h>
#include <linux/dcache.h>
#include <linux/magic.h>
#include <linux/types.h>
#include <linux/refcount.h>
#include <linux/workqueue.h>
#include "flask.h"

#define SECSID_NULL			0x00000000 /* unspecified SID */
#define SECSID_WILD			0xffffffff /* wildcard SID */
#define SECCLASS_NULL			0x0000 /* no class */

/* Identify specific policy version changes */
#define POLICYDB_VERSION_BASE		15
#define POLICYDB_VERSION_BOOL		16
#define POLICYDB_VERSION_IPV6		17
#define POLICYDB_VERSION_NLCLASS	18
#define POLICYDB_VERSION_VALIDATETRANS	19
#define POLICYDB_VERSION_MLS		19
#define POLICYDB_VERSION_AVTAB		20
#define POLICYDB_VERSION_RANGETRANS	21
#define POLICYDB_VERSION_POLCAP		22
#define POLICYDB_VERSION_PERMISSIVE	23
#define POLICYDB_VERSION_BOUNDARY	24
#define POLICYDB_VERSION_FILENAME_TRANS	25
#define POLICYDB_VERSION_ROLETRANS	26
#define POLICYDB_VERSION_NEW_OBJECT_DEFAULTS	27
#define POLICYDB_VERSION_DEFAULT_TYPE	28
#define POLICYDB_VERSION_CONSTRAINT_NAMES	29
#define POLICYDB_VERSION_XPERMS_IOCTL	30
#define POLICYDB_VERSION_INFINIBAND		31

/* Range of policy versions we understand*/
#define POLICYDB_VERSION_MIN   POLICYDB_VERSION_BASE
#define POLICYDB_VERSION_MAX   POLICYDB_VERSION_INFINIBAND

/* Mask for just the mount related flags */
#define SE_MNTMASK	0x0f
/* Super block security struct flags for mount options */
/* BE CAREFUL, these need to be the low order bits for selinux_get_mnt_opts */
#define CONTEXT_MNT	0x01
#define FSCONTEXT_MNT	0x02
#define ROOTCONTEXT_MNT	0x04
#define DEFCONTEXT_MNT	0x08
#define SBLABEL_MNT	0x10
/* Non-mount related flags */
#define SE_SBINITIALIZED	0x0100
#define SE_SBPROC		0x0200
#define SE_SBGENFS		0x0400

#define CONTEXT_STR	"context="
#define FSCONTEXT_STR	"fscontext="
#define ROOTCONTEXT_STR	"rootcontext="
#define DEFCONTEXT_STR	"defcontext="
#define LABELSUPP_STR "seclabel"

struct netlbl_lsm_secattr;

extern int selinux_enabled;

/* Policy capabilities */
enum {
	POLICYDB_CAPABILITY_NETPEER,
	POLICYDB_CAPABILITY_OPENPERM,
	POLICYDB_CAPABILITY_EXTSOCKCLASS,
	POLICYDB_CAPABILITY_ALWAYSNETWORK,
	POLICYDB_CAPABILITY_CGROUPSECLABEL,
	POLICYDB_CAPABILITY_NNP_NOSUID_TRANSITION,
	__POLICYDB_CAPABILITY_MAX
};
#define POLICYDB_CAPABILITY_MAX (__POLICYDB_CAPABILITY_MAX - 1)

extern const char *selinux_policycap_names[__POLICYDB_CAPABILITY_MAX];

/*
 * type_datum properties
 * available at the kernel policy version >= POLICYDB_VERSION_BOUNDARY
 */
#define TYPEDATUM_PROPERTY_PRIMARY	0x0001
#define TYPEDATUM_PROPERTY_ATTRIBUTE	0x0002

/* limitation of boundary depth  */
#define POLICYDB_BOUNDS_MAXDEPTH	4

struct selinux_avc;
struct selinux_ss;

struct selinux_state {
	bool disabled;
#ifdef CONFIG_SECURITY_SELINUX_DEVELOP
	bool enforcing;
#endif
	bool checkreqprot;
	bool initialized;
	bool policycap[__POLICYDB_CAPABILITY_MAX];
	struct selinux_avc *avc;
	struct selinux_ss *ss;
};

void selinux_ss_init(struct selinux_ss **ss);
void selinux_avc_init(struct selinux_avc **avc);

extern struct selinux_state selinux_state;

#ifdef CONFIG_SECURITY_SELINUX_DEVELOP
static inline bool enforcing_enabled(struct selinux_state *state)
{
	return state->enforcing;
}

static inline void enforcing_set(struct selinux_state *state, bool value)
{
	state->enforcing = value;
}
#else
static inline bool enforcing_enabled(struct selinux_state *state)
{
	return true;
}

static inline void enforcing_set(struct selinux_state *state, bool value)
{
}
#endif

static inline bool selinux_policycap_netpeer(void)
{
	struct selinux_state *state = &selinux_state;

	return state->policycap[POLICYDB_CAPABILITY_NETPEER];
}

static inline bool selinux_policycap_openperm(void)
{
	struct selinux_state *state = &selinux_state;

	return state->policycap[POLICYDB_CAPABILITY_OPENPERM];
}

static inline bool selinux_policycap_extsockclass(void)
{
	struct selinux_state *state = &selinux_state;

	return state->policycap[POLICYDB_CAPABILITY_EXTSOCKCLASS];
}

static inline bool selinux_policycap_alwaysnetwork(void)
{
	struct selinux_state *state = &selinux_state;

	return state->policycap[POLICYDB_CAPABILITY_ALWAYSNETWORK];
}

static inline bool selinux_policycap_cgroupseclabel(void)
{
	struct selinux_state *state = &selinux_state;

	return state->policycap[POLICYDB_CAPABILITY_CGROUPSECLABEL];
}

static inline bool selinux_policycap_nnp_nosuid_transition(void)
{
	struct selinux_state *state = &selinux_state;

	return state->policycap[POLICYDB_CAPABILITY_NNP_NOSUID_TRANSITION];
}

int security_mls_enabled(struct selinux_state *state);
int security_load_policy(struct selinux_state *state,
			 void *data, size_t len);
int security_read_policy(struct selinux_state *state,
			 void **data, size_t *len);
size_t security_policydb_len(struct selinux_state *state);

int security_policycap_supported(struct selinux_state *state,
				 unsigned int req_cap);

#define SEL_VEC_MAX 32
struct av_decision {
	u32 allowed;
	u32 auditallow;
	u32 auditdeny;
	u32 seqno;
	u32 flags;
};

#define XPERMS_ALLOWED 1
#define XPERMS_AUDITALLOW 2
#define XPERMS_DONTAUDIT 4

#define security_xperm_set(perms, x) (perms[x >> 5] |= 1 << (x & 0x1f))
#define security_xperm_test(perms, x) (1 & (perms[x >> 5] >> (x & 0x1f)))
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
	u16 len;	/* length associated decision chain */
	struct extended_perms_data drivers; /* flag drivers that are used */
};

/* definitions of av_decision.flags */
#define AVD_FLAGS_PERMISSIVE	0x0001

void security_compute_av(struct selinux_state *state,
			 u32 ssid, u32 tsid,
			 u16 tclass, struct av_decision *avd,
			 struct extended_perms *xperms);

void security_compute_xperms_decision(struct selinux_state *state,
				      u32 ssid, u32 tsid, u16 tclass,
				      u8 driver,
				      struct extended_perms_decision *xpermd);

void security_compute_av_user(struct selinux_state *state,
			      u32 ssid, u32 tsid,
			      u16 tclass, struct av_decision *avd);

int security_transition_sid(struct selinux_state *state,
			    u32 ssid, u32 tsid, u16 tclass,
			    const struct qstr *qstr, u32 *out_sid);

int security_transition_sid_user(struct selinux_state *state,
				 u32 ssid, u32 tsid, u16 tclass,
				 const char *objname, u32 *out_sid);

int security_member_sid(struct selinux_state *state, u32 ssid, u32 tsid,
			u16 tclass, u32 *out_sid);

int security_change_sid(struct selinux_state *state, u32 ssid, u32 tsid,
			u16 tclass, u32 *out_sid);

int security_sid_to_context(struct selinux_state *state, u32 sid,
			    char **scontext, u32 *scontext_len);

int security_sid_to_context_force(struct selinux_state *state,
				  u32 sid, char **scontext, u32 *scontext_len);

int security_context_to_sid(struct selinux_state *state,
			    const char *scontext, u32 scontext_len,
			    u32 *out_sid, gfp_t gfp);

int security_context_str_to_sid(struct selinux_state *state,
				const char *scontext, u32 *out_sid, gfp_t gfp);

int security_context_to_sid_default(struct selinux_state *state,
				    const char *scontext, u32 scontext_len,
				    u32 *out_sid, u32 def_sid, gfp_t gfp_flags);

int security_context_to_sid_force(struct selinux_state *state,
				  const char *scontext, u32 scontext_len,
				  u32 *sid);

int security_get_user_sids(struct selinux_state *state,
			   u32 callsid, char *username,
			   u32 **sids, u32 *nel);

int security_port_sid(struct selinux_state *state,
		      u8 protocol, u16 port, u32 *out_sid);

int security_ib_pkey_sid(struct selinux_state *state,
			 u64 subnet_prefix, u16 pkey_num, u32 *out_sid);

int security_ib_endport_sid(struct selinux_state *state,
			    const char *dev_name, u8 port_num, u32 *out_sid);

int security_netif_sid(struct selinux_state *state,
		       char *name, u32 *if_sid);

int security_node_sid(struct selinux_state *state,
		      u16 domain, void *addr, u32 addrlen,
		      u32 *out_sid);

int security_validate_transition(struct selinux_state *state,
				 u32 oldsid, u32 newsid, u32 tasksid,
				 u16 tclass);

int security_validate_transition_user(struct selinux_state *state,
				      u32 oldsid, u32 newsid, u32 tasksid,
				      u16 tclass);

int security_bounded_transition(struct selinux_state *state,
				u32 oldsid, u32 newsid);

int security_sid_mls_copy(struct selinux_state *state,
			  u32 sid, u32 mls_sid, u32 *new_sid);

int security_net_peersid_resolve(struct selinux_state *state,
				 u32 nlbl_sid, u32 nlbl_type,
				 u32 xfrm_sid,
				 u32 *peer_sid);

int security_get_classes(struct selinux_state *state,
			 char ***classes, int *nclasses);
int security_get_permissions(struct selinux_state *state,
			     char *class, char ***perms, int *nperms);
int security_get_reject_unknown(struct selinux_state *state);
int security_get_allow_unknown(struct selinux_state *state);

#define SECURITY_FS_USE_XATTR		1 /* use xattr */
#define SECURITY_FS_USE_TRANS		2 /* use transition SIDs, e.g. devpts/tmpfs */
#define SECURITY_FS_USE_TASK		3 /* use task SIDs, e.g. pipefs/sockfs */
#define SECURITY_FS_USE_GENFS		4 /* use the genfs support */
#define SECURITY_FS_USE_NONE		5 /* no labeling support */
#define SECURITY_FS_USE_MNTPOINT	6 /* use mountpoint labeling */
#define SECURITY_FS_USE_NATIVE		7 /* use native label support */
#define SECURITY_FS_USE_MAX		7 /* Highest SECURITY_FS_USE_XXX */

int security_fs_use(struct selinux_state *state, struct super_block *sb);

int security_genfs_sid(struct selinux_state *state,
		       const char *fstype, char *name, u16 sclass,
		       u32 *sid);

#ifdef CONFIG_NETLABEL
int security_netlbl_secattr_to_sid(struct selinux_state *state,
				   struct netlbl_lsm_secattr *secattr,
				   u32 *sid);

int security_netlbl_sid_to_secattr(struct selinux_state *state,
				   u32 sid,
				   struct netlbl_lsm_secattr *secattr);
#else
static inline int security_netlbl_secattr_to_sid(struct selinux_state *state,
					    struct netlbl_lsm_secattr *secattr,
					    u32 *sid)
{
	return -EIDRM;
}

static inline int security_netlbl_sid_to_secattr(struct selinux_state *state,
					 u32 sid,
					 struct netlbl_lsm_secattr *secattr)
{
	return -ENOENT;
}
#endif /* CONFIG_NETLABEL */

const char *security_get_initial_sid_context(u32 sid);

/*
 * status notifier using mmap interface
 */
extern struct page *selinux_kernel_status_page(struct selinux_state *state);

#define SELINUX_KERNEL_STATUS_VERSION	1
struct selinux_kernel_status {
	u32	version;	/* version number of thie structure */
	u32	sequence;	/* sequence number of seqlock logic */
	u32	enforcing;	/* current setting of enforcing mode */
	u32	policyload;	/* times of policy reloaded */
	u32	deny_unknown;	/* current setting of deny_unknown */
	/*
	 * The version > 0 supports above members.
	 */
} __packed;

extern void selinux_status_update_setenforce(struct selinux_state *state,
					     int enforcing);
extern void selinux_status_update_policyload(struct selinux_state *state,
					     int seqno);
extern void selinux_complete_init(void);
extern int selinux_disable(struct selinux_state *state);
extern void exit_sel_fs(void);
extern struct path selinux_null;
extern struct vfsmount *selinuxfs_mount;
extern void selnl_notify_setenforce(int val);
extern void selnl_notify_policyload(u32 seqno);
extern int selinux_nlmsg_lookup(u16 sclass, u16 nlmsg_type, u32 *perm);

extern void avtab_cache_init(void);
extern void ebitmap_cache_init(void);
extern void hashtab_cache_init(void);
extern int security_sidtab_hash_stats(struct selinux_state *state, char *page);

#endif /* _SELINUX_SECURITY_H_ */
