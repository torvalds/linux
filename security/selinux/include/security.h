/*
 * Security server interface.
 *
 * Author : Stephen Smalley, <sds@epoch.ncsc.mil>
 *
 */

#ifndef _SELINUX_SECURITY_H_
#define _SELINUX_SECURITY_H_

#include <linux/dcache.h>
#include <linux/magic.h>
#include <linux/types.h>
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

/* Range of policy versions we understand*/
#define POLICYDB_VERSION_MIN   POLICYDB_VERSION_BASE
#ifdef CONFIG_SECURITY_SELINUX_POLICYDB_VERSION_MAX
#define POLICYDB_VERSION_MAX	CONFIG_SECURITY_SELINUX_POLICYDB_VERSION_MAX_VALUE
#else
#define POLICYDB_VERSION_MAX	POLICYDB_VERSION_CONSTRAINT_NAMES
#endif

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
	POLICYDB_CAPABILITY_REDHAT1,
	POLICYDB_CAPABILITY_ALWAYSNETWORK,
	__POLICYDB_CAPABILITY_MAX
};
#define POLICYDB_CAPABILITY_MAX (__POLICYDB_CAPABILITY_MAX - 1)

extern int selinux_policycap_netpeer;
extern int selinux_policycap_openperm;
extern int selinux_policycap_alwaysnetwork;

/*
 * type_datum properties
 * available at the kernel policy version >= POLICYDB_VERSION_BOUNDARY
 */
#define TYPEDATUM_PROPERTY_PRIMARY	0x0001
#define TYPEDATUM_PROPERTY_ATTRIBUTE	0x0002

/* limitation of boundary depth  */
#define POLICYDB_BOUNDS_MAXDEPTH	4

int security_mls_enabled(void);

int security_load_policy(void *data, size_t len);
int security_read_policy(void **data, size_t *len);
size_t security_policydb_len(void);

int security_policycap_supported(unsigned int req_cap);

#define SEL_VEC_MAX 32
struct av_decision {
	u32 allowed;
	u32 auditallow;
	u32 auditdeny;
	u32 seqno;
	u32 flags;
};

/* definitions of av_decision.flags */
#define AVD_FLAGS_PERMISSIVE	0x0001

void security_compute_av(u32 ssid, u32 tsid,
			 u16 tclass, struct av_decision *avd);

void security_compute_av_user(u32 ssid, u32 tsid,
			     u16 tclass, struct av_decision *avd);

int security_transition_sid(u32 ssid, u32 tsid, u16 tclass,
			    const struct qstr *qstr, u32 *out_sid);

int security_transition_sid_user(u32 ssid, u32 tsid, u16 tclass,
				 const char *objname, u32 *out_sid);

int security_member_sid(u32 ssid, u32 tsid,
	u16 tclass, u32 *out_sid);

int security_change_sid(u32 ssid, u32 tsid,
	u16 tclass, u32 *out_sid);

int security_sid_to_context(u32 sid, char **scontext,
	u32 *scontext_len);

int security_sid_to_context_force(u32 sid, char **scontext, u32 *scontext_len);

int security_context_to_sid(const char *scontext, u32 scontext_len,
	u32 *out_sid);

int security_context_to_sid_default(const char *scontext, u32 scontext_len,
				    u32 *out_sid, u32 def_sid, gfp_t gfp_flags);

int security_context_to_sid_force(const char *scontext, u32 scontext_len,
				  u32 *sid);

int security_get_user_sids(u32 callsid, char *username,
			   u32 **sids, u32 *nel);

int security_port_sid(u8 protocol, u16 port, u32 *out_sid);

int security_netif_sid(char *name, u32 *if_sid);

int security_node_sid(u16 domain, void *addr, u32 addrlen,
	u32 *out_sid);

int security_validate_transition(u32 oldsid, u32 newsid, u32 tasksid,
				 u16 tclass);

int security_bounded_transition(u32 oldsid, u32 newsid);

int security_sid_mls_copy(u32 sid, u32 mls_sid, u32 *new_sid);

int security_net_peersid_resolve(u32 nlbl_sid, u32 nlbl_type,
				 u32 xfrm_sid,
				 u32 *peer_sid);

int security_get_classes(char ***classes, int *nclasses);
int security_get_permissions(char *class, char ***perms, int *nperms);
int security_get_reject_unknown(void);
int security_get_allow_unknown(void);

#define SECURITY_FS_USE_XATTR		1 /* use xattr */
#define SECURITY_FS_USE_TRANS		2 /* use transition SIDs, e.g. devpts/tmpfs */
#define SECURITY_FS_USE_TASK		3 /* use task SIDs, e.g. pipefs/sockfs */
#define SECURITY_FS_USE_GENFS		4 /* use the genfs support */
#define SECURITY_FS_USE_NONE		5 /* no labeling support */
#define SECURITY_FS_USE_MNTPOINT	6 /* use mountpoint labeling */
#define SECURITY_FS_USE_NATIVE		7 /* use native label support */
#define SECURITY_FS_USE_MAX		7 /* Highest SECURITY_FS_USE_XXX */

int security_fs_use(struct super_block *sb);

int security_genfs_sid(const char *fstype, char *name, u16 sclass,
	u32 *sid);

#ifdef CONFIG_NETLABEL
int security_netlbl_secattr_to_sid(struct netlbl_lsm_secattr *secattr,
				   u32 *sid);

int security_netlbl_sid_to_secattr(u32 sid,
				   struct netlbl_lsm_secattr *secattr);
#else
static inline int security_netlbl_secattr_to_sid(
					    struct netlbl_lsm_secattr *secattr,
					    u32 *sid)
{
	return -EIDRM;
}

static inline int security_netlbl_sid_to_secattr(u32 sid,
					   struct netlbl_lsm_secattr *secattr)
{
	return -ENOENT;
}
#endif /* CONFIG_NETLABEL */

const char *security_get_initial_sid_context(u32 sid);

/*
 * status notifier using mmap interface
 */
extern struct page *selinux_kernel_status_page(void);

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
} __attribute__((packed));

extern void selinux_status_update_setenforce(int enforcing);
extern void selinux_status_update_policyload(int seqno);
extern void selinux_complete_init(void);
extern int selinux_disable(void);
extern void exit_sel_fs(void);
extern struct path selinux_null;
extern struct vfsmount *selinuxfs_mount;
extern void selnl_notify_setenforce(int val);
extern void selnl_notify_policyload(u32 seqno);
extern int selinux_nlmsg_lookup(u16 sclass, u16 nlmsg_type, u32 *perm);

#endif /* _SELINUX_SECURITY_H_ */

