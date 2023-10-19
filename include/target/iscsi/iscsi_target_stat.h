/* SPDX-License-Identifier: GPL-2.0 */
#ifndef ISCSI_TARGET_STAT_H
#define ISCSI_TARGET_STAT_H

#include <linux/types.h>
#include <linux/spinlock.h>
#include <linux/socket.h>

/*
 * For struct iscsi_tiqn->tiqn_wwn default groups
 */
extern const struct config_item_type iscsi_stat_instance_cit;
extern const struct config_item_type iscsi_stat_sess_err_cit;
extern const struct config_item_type iscsi_stat_tgt_attr_cit;
extern const struct config_item_type iscsi_stat_login_cit;
extern const struct config_item_type iscsi_stat_logout_cit;

/*
 * For struct iscsi_session->se_sess default groups
 */
extern const struct config_item_type iscsi_stat_sess_cit;

/* iSCSI session error types */
#define ISCSI_SESS_ERR_UNKNOWN		0
#define ISCSI_SESS_ERR_DIGEST		1
#define ISCSI_SESS_ERR_CXN_TIMEOUT	2
#define ISCSI_SESS_ERR_PDU_FORMAT	3

/* iSCSI session error stats */
struct iscsi_sess_err_stats {
	spinlock_t	lock;
	u32		digest_errors;
	u32		cxn_timeout_errors;
	u32		pdu_format_errors;
	u32		last_sess_failure_type;
	char		last_sess_fail_rem_name[ISCSI_IQN_LEN];
} ____cacheline_aligned;

/* iSCSI login failure types (sub oids) */
#define ISCSI_LOGIN_FAIL_OTHER		2
#define ISCSI_LOGIN_FAIL_REDIRECT	3
#define ISCSI_LOGIN_FAIL_AUTHORIZE	4
#define ISCSI_LOGIN_FAIL_AUTHENTICATE	5
#define ISCSI_LOGIN_FAIL_NEGOTIATE	6

/* iSCSI login stats */
struct iscsi_login_stats {
	spinlock_t	lock;
	u32		accepts;
	u32		other_fails;
	u32		redirects;
	u32		authorize_fails;
	u32		authenticate_fails;
	u32		negotiate_fails;	/* used for notifications */
	u64		last_fail_time;		/* time stamp (jiffies) */
	u32		last_fail_type;
	int		last_intr_fail_ip_family;
	struct sockaddr_storage last_intr_fail_sockaddr;
	char		last_intr_fail_name[ISCSI_IQN_LEN];
} ____cacheline_aligned;

/* iSCSI logout stats */
struct iscsi_logout_stats {
	spinlock_t	lock;
	u32		normal_logouts;
	u32		abnormal_logouts;
} ____cacheline_aligned;

#endif   /*** ISCSI_TARGET_STAT_H ***/
