/*
 * Security server interface.
 *
 * Author : Stephen Smalley, <sds@epoch.ncsc.mil>
 *
 */

#ifndef _SELINUX_SECURITY_H_
#define _SELINUX_SECURITY_H_

#include <linux/magic.h>
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

/* Range of policy versions we understand*/
#define POLICYDB_VERSION_MIN   POLICYDB_VERSION_BASE
#ifdef CONFIG_SECURITY_SELINUX_POLICYDB_VERSION_MAX
#define POLICYDB_VERSION_MAX	CONFIG_SECURITY_SELINUX_POLICYDB_VERSION_MAX_VALUE
#else
#define POLICYDB_VERSION_MAX	POLICYDB_VERSION_BOUNDARY
#endif

/* Mask for just the mount related flags */
#define SE_MNTMASK	0x0f
/* Super block security struct flags for mount options */
#define CONTEXT_MNT	0x01
#define FSCONTEXT_MNT	0x02
#define ROOTCONTEXT_MNT	0x04
#define DEFCONTEXT_MNT	0x08
/* Non-mount related flags */
#define SE_SBINITIALIZED	0x10
#define SE_SBPROC		0x20
#define SE_SBLABELSUPP	0x40

#define CONTEXT_STR	"context="
#define FSCONTEXT_STR	"fscontext="
#define ROOTCONTEXT_STR	"rootcontext="
#define DEFCONTEXT_STR	"defcontext="
#define LABELSUPP_STR "seclabel"

struct netlbl_lsm_secattr;

extern int selinux_enabled;
extern int selinux_mls_enabled;

/* Policy capabilities */
enum {
	POLICYDB_CAPABILITY_NETPEER,
	POLICYDB_CAPABILITY_OPENPERM,
	__POLICYDB_CAPABILITY_MAX
};
#define POLICYDB_CAPABILITY_MAX (__POLICYDB_CAPABILITY_MAX - 1)

extern int selinux_policycap_netpeer;
extern int selinux_policycap_openperm;

/*
 * type_datum properties
 * available at the kernel policy version >= POLICYDB_VERSION_BOUNDARY
 */
#define TYPEDATUM_PROPERTY_PRIMARY	0x0001
#define TYPEDATUM_PROPERTY_ATTRIBUTE	0x0002

/* limitation of boundary depth  */
#define POLICYDB_BOUNDS_MAXDEPTH	4

int security_load_policy(void *data, size_t len);

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

int security_compute_av(u32 ssid, u32 tsid,
	u16 tclass, u32 requested,
	struct av_decision *avd);

int security_transition_sid(u32 ssid, u32 tsid,
	u16 tclass, u32 *out_sid);

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

int security_fs_use(const char *fstype, unsigned int *behavior,
	u32 *sid);

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

#endif /* _SELINUX_SECURITY_H_ */

