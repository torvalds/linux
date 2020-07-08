/* SPDX-License-Identifier: GPL-2.0 */
/*
 *  Shared Memory Communications over RDMA (SMC-R) and RoCE
 *
 *  Definitions for LLC (link layer control) message handling
 *
 *  Copyright IBM Corp. 2016
 *
 *  Author(s):  Klaus Wacker <Klaus.Wacker@de.ibm.com>
 *              Ursula Braun <ubraun@linux.vnet.ibm.com>
 */

#ifndef SMC_LLC_H
#define SMC_LLC_H

#include "smc_wr.h"

#define SMC_LLC_FLAG_RESP		0x80

#define SMC_LLC_WAIT_FIRST_TIME		(5 * HZ)
#define SMC_LLC_WAIT_TIME		(2 * HZ)

enum smc_llc_reqresp {
	SMC_LLC_REQ,
	SMC_LLC_RESP
};

enum smc_llc_msg_type {
	SMC_LLC_CONFIRM_LINK		= 0x01,
	SMC_LLC_ADD_LINK		= 0x02,
	SMC_LLC_ADD_LINK_CONT		= 0x03,
	SMC_LLC_DELETE_LINK		= 0x04,
	SMC_LLC_CONFIRM_RKEY		= 0x06,
	SMC_LLC_TEST_LINK		= 0x07,
	SMC_LLC_CONFIRM_RKEY_CONT	= 0x08,
	SMC_LLC_DELETE_RKEY		= 0x09,
};

#define smc_link_downing(state) \
	(cmpxchg(state, SMC_LNK_ACTIVE, SMC_LNK_INACTIVE) == SMC_LNK_ACTIVE)

/* LLC DELETE LINK Request Reason Codes */
#define SMC_LLC_DEL_LOST_PATH		0x00010000
#define SMC_LLC_DEL_OP_INIT_TERM	0x00020000
#define SMC_LLC_DEL_PROG_INIT_TERM	0x00030000
#define SMC_LLC_DEL_PROT_VIOL		0x00040000
#define SMC_LLC_DEL_NO_ASYM_NEEDED	0x00050000
/* LLC DELETE LINK Response Reason Codes */
#define SMC_LLC_DEL_NOLNK	0x00100000  /* Unknown Link ID (no link) */
#define SMC_LLC_DEL_NOLGR	0x00200000  /* Unknown Link Group */

/* returns a usable link of the link group, or NULL */
static inline struct smc_link *smc_llc_usable_link(struct smc_link_group *lgr)
{
	int i;

	for (i = 0; i < SMC_LINKS_PER_LGR_MAX; i++)
		if (smc_link_usable(&lgr->lnk[i]))
			return &lgr->lnk[i];
	return NULL;
}

/* set the termination reason code for the link group */
static inline void smc_llc_set_termination_rsn(struct smc_link_group *lgr,
					       u32 rsn)
{
	if (!lgr->llc_termination_rsn)
		lgr->llc_termination_rsn = rsn;
}

/* transmit */
int smc_llc_send_confirm_link(struct smc_link *lnk,
			      enum smc_llc_reqresp reqresp);
int smc_llc_send_add_link(struct smc_link *link, u8 mac[], u8 gid[],
			  struct smc_link *link_new,
			  enum smc_llc_reqresp reqresp);
int smc_llc_send_delete_link(struct smc_link *link, u8 link_del_id,
			     enum smc_llc_reqresp reqresp, bool orderly,
			     u32 reason);
void smc_llc_srv_delete_link_local(struct smc_link *link, u8 del_link_id);
void smc_llc_lgr_init(struct smc_link_group *lgr, struct smc_sock *smc);
void smc_llc_lgr_clear(struct smc_link_group *lgr);
int smc_llc_link_init(struct smc_link *link);
void smc_llc_link_active(struct smc_link *link);
void smc_llc_link_clear(struct smc_link *link, bool log);
int smc_llc_do_confirm_rkey(struct smc_link *send_link,
			    struct smc_buf_desc *rmb_desc);
int smc_llc_do_delete_rkey(struct smc_link_group *lgr,
			   struct smc_buf_desc *rmb_desc);
int smc_llc_flow_initiate(struct smc_link_group *lgr,
			  enum smc_llc_flowtype type);
void smc_llc_flow_stop(struct smc_link_group *lgr, struct smc_llc_flow *flow);
int smc_llc_eval_conf_link(struct smc_llc_qentry *qentry,
			   enum smc_llc_reqresp type);
void smc_llc_link_set_uid(struct smc_link *link);
void smc_llc_save_peer_uid(struct smc_llc_qentry *qentry);
struct smc_llc_qentry *smc_llc_wait(struct smc_link_group *lgr,
				    struct smc_link *lnk,
				    int time_out, u8 exp_msg);
struct smc_llc_qentry *smc_llc_flow_qentry_clr(struct smc_llc_flow *flow);
void smc_llc_flow_qentry_del(struct smc_llc_flow *flow);
void smc_llc_send_link_delete_all(struct smc_link_group *lgr, bool ord,
				  u32 rsn);
int smc_llc_cli_add_link(struct smc_link *link, struct smc_llc_qentry *qentry);
int smc_llc_srv_add_link(struct smc_link *link);
void smc_llc_srv_add_link_local(struct smc_link *link);
int smc_llc_init(void) __init;

#endif /* SMC_LLC_H */
