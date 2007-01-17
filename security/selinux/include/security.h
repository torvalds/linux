/*
 * Security server interface.
 *
 * Author : Stephen Smalley, <sds@epoch.ncsc.mil>
 *
 */

#ifndef _SELINUX_SECURITY_H_
#define _SELINUX_SECURITY_H_

#include "flask.h"

#define SECSID_NULL			0x00000000 /* unspecified SID */
#define SECSID_WILD			0xffffffff /* wildcard SID */
#define SECCLASS_NULL			0x0000 /* no class */

#define SELINUX_MAGIC 0xf97cff8c

/* Identify specific policy version changes */
#define POLICYDB_VERSION_BASE		15
#define POLICYDB_VERSION_BOOL		16
#define POLICYDB_VERSION_IPV6		17
#define POLICYDB_VERSION_NLCLASS	18
#define POLICYDB_VERSION_VALIDATETRANS	19
#define POLICYDB_VERSION_MLS		19
#define POLICYDB_VERSION_AVTAB		20
#define POLICYDB_VERSION_RANGETRANS	21

/* Range of policy versions we understand*/
#define POLICYDB_VERSION_MIN   POLICYDB_VERSION_BASE
#ifdef CONFIG_SECURITY_SELINUX_POLICYDB_VERSION_MAX
#define POLICYDB_VERSION_MAX	CONFIG_SECURITY_SELINUX_POLICYDB_VERSION_MAX_VALUE
#else
#define POLICYDB_VERSION_MAX	POLICYDB_VERSION_RANGETRANS
#endif

struct sk_buff;

extern int selinux_enabled;
extern int selinux_mls_enabled;

int security_load_policy(void * data, size_t len);

struct av_decision {
	u32 allowed;
	u32 decided;
	u32 auditallow;
	u32 auditdeny;
	u32 seqno;
};

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

int security_context_to_sid(char *scontext, u32 scontext_len,
	u32 *out_sid);

int security_context_to_sid_default(char *scontext, u32 scontext_len, u32 *out_sid, u32 def_sid);

int security_get_user_sids(u32 callsid, char *username,
			   u32 **sids, u32 *nel);

int security_port_sid(u16 domain, u16 type, u8 protocol, u16 port,
	u32 *out_sid);

int security_netif_sid(char *name, u32 *if_sid,
	u32 *msg_sid);

int security_node_sid(u16 domain, void *addr, u32 addrlen,
	u32 *out_sid);

void security_skb_extlbl_sid(struct sk_buff *skb, u32 base_sid, u32 *sid);

int security_validate_transition(u32 oldsid, u32 newsid, u32 tasksid,
                                 u16 tclass);

int security_sid_mls_copy(u32 sid, u32 mls_sid, u32 *new_sid);

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

#endif /* _SELINUX_SECURITY_H_ */

