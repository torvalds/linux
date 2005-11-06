/*
 * llc_c_ac.c - actions performed during connection state transition.
 *
 * Description:
 *   Functions in this module are implementation of connection component actions
 *   Details of actions can be found in IEEE-802.2 standard document.
 *   All functions have one connection and one event as input argument. All of
 *   them return 0 On success and 1 otherwise.
 *
 * Copyright (c) 1997 by Procom Technology, Inc.
 * 		 2001-2003 by Arnaldo Carvalho de Melo <acme@conectiva.com.br>
 *
 * This program can be redistributed or modified under the terms of the
 * GNU General Public License as published by the Free Software Foundation.
 * This program is distributed without any warranty or implied warranty
 * of merchantability or fitness for a particular purpose.
 *
 * See the GNU General Public License for more details.
 */
#include <linux/netdevice.h>
#include <net/llc_conn.h>
#include <net/llc_sap.h>
#include <net/sock.h>
#include <net/llc_c_ev.h>
#include <net/llc_c_ac.h>
#include <net/llc_c_st.h>
#include <net/llc_pdu.h>
#include <net/llc.h>

#include "llc_output.h"

static int llc_conn_ac_inc_vs_by_1(struct sock *sk, struct sk_buff *skb);
static void llc_process_tmr_ev(struct sock *sk, struct sk_buff *skb);
static int llc_conn_ac_data_confirm(struct sock *sk, struct sk_buff *ev);

static int llc_conn_ac_inc_npta_value(struct sock *sk, struct sk_buff *skb);

static int llc_conn_ac_send_rr_rsp_f_set_ackpf(struct sock *sk,
					       struct sk_buff *skb);

static int llc_conn_ac_set_p_flag_1(struct sock *sk, struct sk_buff *skb);

#define INCORRECT 0

int llc_conn_ac_clear_remote_busy(struct sock *sk, struct sk_buff *skb)
{
	struct llc_sock *llc = llc_sk(sk);

	if (llc->remote_busy_flag) {
		u8 nr;
		struct llc_pdu_sn *pdu = llc_pdu_sn_hdr(skb);

		llc->remote_busy_flag = 0;
		del_timer(&llc->busy_state_timer.timer);
		nr = LLC_I_GET_NR(pdu);
		llc_conn_resend_i_pdu_as_cmd(sk, nr, 0);
	}
	return 0;
}

int llc_conn_ac_conn_ind(struct sock *sk, struct sk_buff *skb)
{
	struct llc_conn_state_ev *ev = llc_conn_ev(skb);

	ev->ind_prim = LLC_CONN_PRIM;
	return 0;
}

int llc_conn_ac_conn_confirm(struct sock *sk, struct sk_buff *skb)
{
	struct llc_conn_state_ev *ev = llc_conn_ev(skb);

	ev->cfm_prim = LLC_CONN_PRIM;
	return 0;
}

static int llc_conn_ac_data_confirm(struct sock *sk, struct sk_buff *skb)
{
	struct llc_conn_state_ev *ev = llc_conn_ev(skb);

	ev->cfm_prim = LLC_DATA_PRIM;
	return 0;
}

int llc_conn_ac_data_ind(struct sock *sk, struct sk_buff *skb)
{
	llc_conn_rtn_pdu(sk, skb);
	return 0;
}

int llc_conn_ac_disc_ind(struct sock *sk, struct sk_buff *skb)
{
	struct llc_conn_state_ev *ev = llc_conn_ev(skb);
	u8 reason = 0;
	int rc = 0;

	if (ev->type == LLC_CONN_EV_TYPE_PDU) {
		struct llc_pdu_un *pdu = llc_pdu_un_hdr(skb);

		if (LLC_PDU_IS_RSP(pdu) &&
		    LLC_PDU_TYPE_IS_U(pdu) &&
		    LLC_U_PDU_RSP(pdu) == LLC_2_PDU_RSP_DM)
			reason = LLC_DISC_REASON_RX_DM_RSP_PDU;
		else if (LLC_PDU_IS_CMD(pdu) &&
			   LLC_PDU_TYPE_IS_U(pdu) &&
			   LLC_U_PDU_CMD(pdu) == LLC_2_PDU_CMD_DISC)
			reason = LLC_DISC_REASON_RX_DISC_CMD_PDU;
	} else if (ev->type == LLC_CONN_EV_TYPE_ACK_TMR)
		reason = LLC_DISC_REASON_ACK_TMR_EXP;
	else
		rc = -EINVAL;
	if (!rc) {
		ev->reason   = reason;
		ev->ind_prim = LLC_DISC_PRIM;
	}
	return rc;
}

int llc_conn_ac_disc_confirm(struct sock *sk, struct sk_buff *skb)
{
	struct llc_conn_state_ev *ev = llc_conn_ev(skb);

	ev->reason   = ev->status;
	ev->cfm_prim = LLC_DISC_PRIM;
	return 0;
}

int llc_conn_ac_rst_ind(struct sock *sk, struct sk_buff *skb)
{
	u8 reason = 0;
	int rc = 1;
	struct llc_conn_state_ev *ev = llc_conn_ev(skb);
	struct llc_pdu_un *pdu = llc_pdu_un_hdr(skb);
	struct llc_sock *llc = llc_sk(sk);

	switch (ev->type) {
	case LLC_CONN_EV_TYPE_PDU:
		if (LLC_PDU_IS_RSP(pdu) &&
		    LLC_PDU_TYPE_IS_U(pdu) &&
		    LLC_U_PDU_RSP(pdu) == LLC_2_PDU_RSP_FRMR) {
			reason = LLC_RESET_REASON_LOCAL;
			rc = 0;
		} else if (LLC_PDU_IS_CMD(pdu) &&
			   LLC_PDU_TYPE_IS_U(pdu) &&
			   LLC_U_PDU_CMD(pdu) == LLC_2_PDU_CMD_SABME) {
			reason = LLC_RESET_REASON_REMOTE;
			rc = 0;
		}
		break;
	case LLC_CONN_EV_TYPE_ACK_TMR:
	case LLC_CONN_EV_TYPE_P_TMR:
	case LLC_CONN_EV_TYPE_REJ_TMR:
	case LLC_CONN_EV_TYPE_BUSY_TMR:
		if (llc->retry_count > llc->n2) {
			reason = LLC_RESET_REASON_LOCAL;
			rc = 0;
		}
		break;
	}
	if (!rc) {
		ev->reason   = reason;
		ev->ind_prim = LLC_RESET_PRIM;
	}
	return rc;
}

int llc_conn_ac_rst_confirm(struct sock *sk, struct sk_buff *skb)
{
	struct llc_conn_state_ev *ev = llc_conn_ev(skb);

	ev->reason   = 0;
	ev->cfm_prim = LLC_RESET_PRIM;
	return 0;
}

int llc_conn_ac_clear_remote_busy_if_f_eq_1(struct sock *sk,
					    struct sk_buff *skb)
{
	struct llc_pdu_sn *pdu = llc_pdu_sn_hdr(skb);

	if (LLC_PDU_IS_RSP(pdu) &&
	    LLC_PDU_TYPE_IS_I(pdu) &&
	    LLC_I_PF_IS_1(pdu) && llc_sk(sk)->ack_pf)
		llc_conn_ac_clear_remote_busy(sk, skb);
	return 0;
}

int llc_conn_ac_stop_rej_tmr_if_data_flag_eq_2(struct sock *sk,
					       struct sk_buff *skb)
{
	struct llc_sock *llc = llc_sk(sk);

	if (llc->data_flag == 2)
		del_timer(&llc->rej_sent_timer.timer);
	return 0;
}

int llc_conn_ac_send_disc_cmd_p_set_x(struct sock *sk, struct sk_buff *skb)
{
	int rc = -ENOBUFS;
	struct llc_sock *llc = llc_sk(sk);
	struct sk_buff *nskb = llc_alloc_frame(sk, llc->dev);

	if (nskb) {
		struct llc_sap *sap = llc->sap;

		llc_pdu_header_init(nskb, LLC_PDU_TYPE_U, sap->laddr.lsap,
				    llc->daddr.lsap, LLC_PDU_CMD);
		llc_pdu_init_as_disc_cmd(nskb, 1);
		rc = llc_mac_hdr_init(nskb, llc->dev->dev_addr, llc->daddr.mac);
		if (unlikely(rc))
			goto free;
		llc_conn_send_pdu(sk, nskb);
		llc_conn_ac_set_p_flag_1(sk, skb);
	}
out:
	return rc;
free:
	kfree_skb(nskb);
	goto out;
}

int llc_conn_ac_send_dm_rsp_f_set_p(struct sock *sk, struct sk_buff *skb)
{
	int rc = -ENOBUFS;
	struct llc_sock *llc = llc_sk(sk);
	struct sk_buff *nskb = llc_alloc_frame(sk, llc->dev);

	if (nskb) {
		struct llc_sap *sap = llc->sap;
		u8 f_bit;

		llc_pdu_decode_pf_bit(skb, &f_bit);
		llc_pdu_header_init(nskb, LLC_PDU_TYPE_U, sap->laddr.lsap,
				    llc->daddr.lsap, LLC_PDU_RSP);
		llc_pdu_init_as_dm_rsp(nskb, f_bit);
		rc = llc_mac_hdr_init(nskb, llc->dev->dev_addr, llc->daddr.mac);
		if (unlikely(rc))
			goto free;
		llc_conn_send_pdu(sk, nskb);
	}
out:
	return rc;
free:
	kfree_skb(nskb);
	goto out;
}

int llc_conn_ac_send_dm_rsp_f_set_1(struct sock *sk, struct sk_buff *skb)
{
	int rc = -ENOBUFS;
	struct llc_sock *llc = llc_sk(sk);
	struct sk_buff *nskb = llc_alloc_frame(sk, llc->dev);

	if (nskb) {
		struct llc_sap *sap = llc->sap;

		llc_pdu_header_init(nskb, LLC_PDU_TYPE_U, sap->laddr.lsap,
				    llc->daddr.lsap, LLC_PDU_RSP);
		llc_pdu_init_as_dm_rsp(nskb, 1);
		rc = llc_mac_hdr_init(nskb, llc->dev->dev_addr, llc->daddr.mac);
		if (unlikely(rc))
			goto free;
		llc_conn_send_pdu(sk, nskb);
	}
out:
	return rc;
free:
	kfree_skb(nskb);
	goto out;
}

int llc_conn_ac_send_frmr_rsp_f_set_x(struct sock *sk, struct sk_buff *skb)
{
	u8 f_bit;
	int rc = -ENOBUFS;
	struct sk_buff *nskb;
	struct llc_pdu_sn *pdu = llc_pdu_sn_hdr(skb);
	struct llc_sock *llc = llc_sk(sk);

	llc->rx_pdu_hdr = *((u32 *)pdu);
	if (LLC_PDU_IS_CMD(pdu))
		llc_pdu_decode_pf_bit(skb, &f_bit);
	else
		f_bit = 0;
	nskb = llc_alloc_frame(sk, llc->dev);
	if (nskb) {
		struct llc_sap *sap = llc->sap;

		llc_pdu_header_init(nskb, LLC_PDU_TYPE_U, sap->laddr.lsap,
				    llc->daddr.lsap, LLC_PDU_RSP);
		llc_pdu_init_as_frmr_rsp(nskb, pdu, f_bit, llc->vS,
					 llc->vR, INCORRECT);
		rc = llc_mac_hdr_init(nskb, llc->dev->dev_addr, llc->daddr.mac);
		if (unlikely(rc))
			goto free;
		llc_conn_send_pdu(sk, nskb);
	}
out:
	return rc;
free:
	kfree_skb(nskb);
	goto out;
}

int llc_conn_ac_resend_frmr_rsp_f_set_0(struct sock *sk, struct sk_buff *skb)
{
	int rc = -ENOBUFS;
	struct llc_sock *llc = llc_sk(sk);
	struct sk_buff *nskb = llc_alloc_frame(sk, llc->dev);

	if (nskb) {
		struct llc_sap *sap = llc->sap;
		struct llc_pdu_sn *pdu = (struct llc_pdu_sn *)&llc->rx_pdu_hdr;

		llc_pdu_header_init(nskb, LLC_PDU_TYPE_U, sap->laddr.lsap,
				    llc->daddr.lsap, LLC_PDU_RSP);
		llc_pdu_init_as_frmr_rsp(nskb, pdu, 0, llc->vS,
					 llc->vR, INCORRECT);
		rc = llc_mac_hdr_init(nskb, llc->dev->dev_addr, llc->daddr.mac);
		if (unlikely(rc))
			goto free;
		llc_conn_send_pdu(sk, nskb);
	}
out:
	return rc;
free:
	kfree_skb(nskb);
	goto out;
}

int llc_conn_ac_resend_frmr_rsp_f_set_p(struct sock *sk, struct sk_buff *skb)
{
	u8 f_bit;
	int rc = -ENOBUFS;
	struct sk_buff *nskb;
	struct llc_sock *llc = llc_sk(sk);

	llc_pdu_decode_pf_bit(skb, &f_bit);
	nskb = llc_alloc_frame(sk, llc->dev);
	if (nskb) {
		struct llc_sap *sap = llc->sap;
		struct llc_pdu_sn *pdu = llc_pdu_sn_hdr(skb);

		llc_pdu_header_init(nskb, LLC_PDU_TYPE_U, sap->laddr.lsap,
				    llc->daddr.lsap, LLC_PDU_RSP);
		llc_pdu_init_as_frmr_rsp(nskb, pdu, f_bit, llc->vS,
					 llc->vR, INCORRECT);
		rc = llc_mac_hdr_init(nskb, llc->dev->dev_addr, llc->daddr.mac);
		if (unlikely(rc))
			goto free;
		llc_conn_send_pdu(sk, nskb);
	}
out:
	return rc;
free:
	kfree_skb(nskb);
	goto out;
}

int llc_conn_ac_send_i_cmd_p_set_1(struct sock *sk, struct sk_buff *skb)
{
	int rc;
	struct llc_sock *llc = llc_sk(sk);
	struct llc_sap *sap = llc->sap;

	llc_pdu_header_init(skb, LLC_PDU_TYPE_I, sap->laddr.lsap,
			    llc->daddr.lsap, LLC_PDU_CMD);
	llc_pdu_init_as_i_cmd(skb, 1, llc->vS, llc->vR);
	rc = llc_mac_hdr_init(skb, llc->dev->dev_addr, llc->daddr.mac);
	if (likely(!rc)) {
		llc_conn_send_pdu(sk, skb);
		llc_conn_ac_inc_vs_by_1(sk, skb);
	}
	return rc;
}

static int llc_conn_ac_send_i_cmd_p_set_0(struct sock *sk, struct sk_buff *skb)
{
	int rc;
	struct llc_sock *llc = llc_sk(sk);
	struct llc_sap *sap = llc->sap;

	llc_pdu_header_init(skb, LLC_PDU_TYPE_I, sap->laddr.lsap,
			    llc->daddr.lsap, LLC_PDU_CMD);
	llc_pdu_init_as_i_cmd(skb, 0, llc->vS, llc->vR);
	rc = llc_mac_hdr_init(skb, llc->dev->dev_addr, llc->daddr.mac);
	if (likely(!rc)) {
		llc_conn_send_pdu(sk, skb);
		llc_conn_ac_inc_vs_by_1(sk, skb);
	}
	return rc;
}

int llc_conn_ac_send_i_xxx_x_set_0(struct sock *sk, struct sk_buff *skb)
{
	int rc;
	struct llc_sock *llc = llc_sk(sk);
	struct llc_sap *sap = llc->sap;

	llc_pdu_header_init(skb, LLC_PDU_TYPE_I, sap->laddr.lsap,
			    llc->daddr.lsap, LLC_PDU_CMD);
	llc_pdu_init_as_i_cmd(skb, 0, llc->vS, llc->vR);
	rc = llc_mac_hdr_init(skb, llc->dev->dev_addr, llc->daddr.mac);
	if (likely(!rc)) {
		llc_conn_send_pdu(sk, skb);
		llc_conn_ac_inc_vs_by_1(sk, skb);
	}
	return 0;
}

int llc_conn_ac_resend_i_xxx_x_set_0(struct sock *sk, struct sk_buff *skb)
{
	struct llc_pdu_sn *pdu = llc_pdu_sn_hdr(skb);
	u8 nr = LLC_I_GET_NR(pdu);

	llc_conn_resend_i_pdu_as_cmd(sk, nr, 0);
	return 0;
}

int llc_conn_ac_resend_i_xxx_x_set_0_or_send_rr(struct sock *sk,
						struct sk_buff *skb)
{
	u8 nr;
	struct llc_pdu_sn *pdu = llc_pdu_sn_hdr(skb);
	int rc = -ENOBUFS;
	struct llc_sock *llc = llc_sk(sk);
	struct sk_buff *nskb = llc_alloc_frame(sk, llc->dev);

	if (nskb) {
		struct llc_sap *sap = llc->sap;

		llc_pdu_header_init(nskb, LLC_PDU_TYPE_U, sap->laddr.lsap,
				    llc->daddr.lsap, LLC_PDU_RSP);
		llc_pdu_init_as_rr_rsp(nskb, 0, llc->vR);
		rc = llc_mac_hdr_init(nskb, llc->dev->dev_addr, llc->daddr.mac);
		if (likely(!rc))
			llc_conn_send_pdu(sk, nskb);
		else
			kfree_skb(skb);
	}
	if (rc) {
		nr = LLC_I_GET_NR(pdu);
		rc = 0;
		llc_conn_resend_i_pdu_as_cmd(sk, nr, 0);
	}
	return rc;
}

int llc_conn_ac_resend_i_rsp_f_set_1(struct sock *sk, struct sk_buff *skb)
{
	struct llc_pdu_sn *pdu = llc_pdu_sn_hdr(skb);
	u8 nr = LLC_I_GET_NR(pdu);

	llc_conn_resend_i_pdu_as_rsp(sk, nr, 1);
	return 0;
}

int llc_conn_ac_send_rej_cmd_p_set_1(struct sock *sk, struct sk_buff *skb)
{
	int rc = -ENOBUFS;
	struct llc_sock *llc = llc_sk(sk);
	struct sk_buff *nskb = llc_alloc_frame(sk, llc->dev);

	if (nskb) {
		struct llc_sap *sap = llc->sap;

		llc_pdu_header_init(nskb, LLC_PDU_TYPE_S, sap->laddr.lsap,
				    llc->daddr.lsap, LLC_PDU_CMD);
		llc_pdu_init_as_rej_cmd(nskb, 1, llc->vR);
		rc = llc_mac_hdr_init(nskb, llc->dev->dev_addr, llc->daddr.mac);
		if (unlikely(rc))
			goto free;
		llc_conn_send_pdu(sk, nskb);
	}
out:
	return rc;
free:
	kfree_skb(nskb);
	goto out;
}

int llc_conn_ac_send_rej_rsp_f_set_1(struct sock *sk, struct sk_buff *skb)
{
	int rc = -ENOBUFS;
	struct llc_sock *llc = llc_sk(sk);
	struct sk_buff *nskb = llc_alloc_frame(sk, llc->dev);

	if (nskb) {
		struct llc_sap *sap = llc->sap;

		llc_pdu_header_init(nskb, LLC_PDU_TYPE_S, sap->laddr.lsap,
				    llc->daddr.lsap, LLC_PDU_RSP);
		llc_pdu_init_as_rej_rsp(nskb, 1, llc->vR);
		rc = llc_mac_hdr_init(nskb, llc->dev->dev_addr, llc->daddr.mac);
		if (unlikely(rc))
			goto free;
		llc_conn_send_pdu(sk, nskb);
	}
out:
	return rc;
free:
	kfree_skb(nskb);
	goto out;
}

int llc_conn_ac_send_rej_xxx_x_set_0(struct sock *sk, struct sk_buff *skb)
{
	int rc = -ENOBUFS;
	struct llc_sock *llc = llc_sk(sk);
	struct sk_buff *nskb = llc_alloc_frame(sk, llc->dev);

	if (nskb) {
		struct llc_sap *sap = llc->sap;

		llc_pdu_header_init(nskb, LLC_PDU_TYPE_S, sap->laddr.lsap,
				    llc->daddr.lsap, LLC_PDU_RSP);
		llc_pdu_init_as_rej_rsp(nskb, 0, llc->vR);
		rc = llc_mac_hdr_init(nskb, llc->dev->dev_addr, llc->daddr.mac);
		if (unlikely(rc))
			goto free;
		llc_conn_send_pdu(sk, nskb);
	}
out:
	return rc;
free:
	kfree_skb(nskb);
	goto out;
}

int llc_conn_ac_send_rnr_cmd_p_set_1(struct sock *sk, struct sk_buff *skb)
{
	int rc = -ENOBUFS;
	struct llc_sock *llc = llc_sk(sk);
	struct sk_buff *nskb = llc_alloc_frame(sk, llc->dev);

	if (nskb) {
		struct llc_sap *sap = llc->sap;

		llc_pdu_header_init(nskb, LLC_PDU_TYPE_S, sap->laddr.lsap,
				    llc->daddr.lsap, LLC_PDU_CMD);
		llc_pdu_init_as_rnr_cmd(nskb, 1, llc->vR);
		rc = llc_mac_hdr_init(nskb, llc->dev->dev_addr, llc->daddr.mac);
		if (unlikely(rc))
			goto free;
		llc_conn_send_pdu(sk, nskb);
	}
out:
	return rc;
free:
	kfree_skb(nskb);
	goto out;
}

int llc_conn_ac_send_rnr_rsp_f_set_1(struct sock *sk, struct sk_buff *skb)
{
	int rc = -ENOBUFS;
	struct llc_sock *llc = llc_sk(sk);
	struct sk_buff *nskb = llc_alloc_frame(sk, llc->dev);

	if (nskb) {
		struct llc_sap *sap = llc->sap;

		llc_pdu_header_init(nskb, LLC_PDU_TYPE_S, sap->laddr.lsap,
				    llc->daddr.lsap, LLC_PDU_RSP);
		llc_pdu_init_as_rnr_rsp(nskb, 1, llc->vR);
		rc = llc_mac_hdr_init(nskb, llc->dev->dev_addr, llc->daddr.mac);
		if (unlikely(rc))
			goto free;
		llc_conn_send_pdu(sk, nskb);
	}
out:
	return rc;
free:
	kfree_skb(nskb);
	goto out;
}

int llc_conn_ac_send_rnr_xxx_x_set_0(struct sock *sk, struct sk_buff *skb)
{
	int rc = -ENOBUFS;
	struct llc_sock *llc = llc_sk(sk);
	struct sk_buff *nskb = llc_alloc_frame(sk, llc->dev);

	if (nskb) {
		struct llc_sap *sap = llc->sap;

		llc_pdu_header_init(nskb, LLC_PDU_TYPE_S, sap->laddr.lsap,
				    llc->daddr.lsap, LLC_PDU_RSP);
		llc_pdu_init_as_rnr_rsp(nskb, 0, llc->vR);
		rc = llc_mac_hdr_init(nskb, llc->dev->dev_addr, llc->daddr.mac);
		if (unlikely(rc))
			goto free;
		llc_conn_send_pdu(sk, nskb);
	}
out:
	return rc;
free:
	kfree_skb(nskb);
	goto out;
}

int llc_conn_ac_set_remote_busy(struct sock *sk, struct sk_buff *skb)
{
	struct llc_sock *llc = llc_sk(sk);

	if (!llc->remote_busy_flag) {
		llc->remote_busy_flag = 1;
		mod_timer(&llc->busy_state_timer.timer,
			 jiffies + llc->busy_state_timer.expire);
	}
	return 0;
}

int llc_conn_ac_opt_send_rnr_xxx_x_set_0(struct sock *sk, struct sk_buff *skb)
{
	int rc = -ENOBUFS;
	struct llc_sock *llc = llc_sk(sk);
	struct sk_buff *nskb = llc_alloc_frame(sk, llc->dev);

	if (nskb) {
		struct llc_sap *sap = llc->sap;

		llc_pdu_header_init(nskb, LLC_PDU_TYPE_S, sap->laddr.lsap,
				    llc->daddr.lsap, LLC_PDU_RSP);
		llc_pdu_init_as_rnr_rsp(nskb, 0, llc->vR);
		rc = llc_mac_hdr_init(nskb, llc->dev->dev_addr, llc->daddr.mac);
		if (unlikely(rc))
			goto free;
		llc_conn_send_pdu(sk, nskb);
	}
out:
	return rc;
free:
	kfree_skb(nskb);
	goto out;
}

int llc_conn_ac_send_rr_cmd_p_set_1(struct sock *sk, struct sk_buff *skb)
{
	int rc = -ENOBUFS;
	struct llc_sock *llc = llc_sk(sk);
	struct sk_buff *nskb = llc_alloc_frame(sk, llc->dev);

	if (nskb) {
		struct llc_sap *sap = llc->sap;

		llc_pdu_header_init(nskb, LLC_PDU_TYPE_S, sap->laddr.lsap,
				    llc->daddr.lsap, LLC_PDU_CMD);
		llc_pdu_init_as_rr_cmd(nskb, 1, llc->vR);
		rc = llc_mac_hdr_init(nskb, llc->dev->dev_addr, llc->daddr.mac);
		if (unlikely(rc))
			goto free;
		llc_conn_send_pdu(sk, nskb);
	}
out:
	return rc;
free:
	kfree_skb(nskb);
	goto out;
}

int llc_conn_ac_send_rr_rsp_f_set_1(struct sock *sk, struct sk_buff *skb)
{
	int rc = -ENOBUFS;
	struct llc_sock *llc = llc_sk(sk);
	struct sk_buff *nskb = llc_alloc_frame(sk, llc->dev);

	if (nskb) {
		struct llc_sap *sap = llc->sap;
		u8 f_bit = 1;

		llc_pdu_header_init(nskb, LLC_PDU_TYPE_S, sap->laddr.lsap,
				    llc->daddr.lsap, LLC_PDU_RSP);
		llc_pdu_init_as_rr_rsp(nskb, f_bit, llc->vR);
		rc = llc_mac_hdr_init(nskb, llc->dev->dev_addr, llc->daddr.mac);
		if (unlikely(rc))
			goto free;
		llc_conn_send_pdu(sk, nskb);
	}
out:
	return rc;
free:
	kfree_skb(nskb);
	goto out;
}

int llc_conn_ac_send_ack_rsp_f_set_1(struct sock *sk, struct sk_buff *skb)
{
	int rc = -ENOBUFS;
	struct llc_sock *llc = llc_sk(sk);
	struct sk_buff *nskb = llc_alloc_frame(sk, llc->dev);

	if (nskb) {
		struct llc_sap *sap = llc->sap;

		llc_pdu_header_init(nskb, LLC_PDU_TYPE_S, sap->laddr.lsap,
				    llc->daddr.lsap, LLC_PDU_RSP);
		llc_pdu_init_as_rr_rsp(nskb, 1, llc->vR);
		rc = llc_mac_hdr_init(nskb, llc->dev->dev_addr, llc->daddr.mac);
		if (unlikely(rc))
			goto free;
		llc_conn_send_pdu(sk, nskb);
	}
out:
	return rc;
free:
	kfree_skb(nskb);
	goto out;
}

int llc_conn_ac_send_rr_xxx_x_set_0(struct sock *sk, struct sk_buff *skb)
{
	int rc = -ENOBUFS;
	struct llc_sock *llc = llc_sk(sk);
	struct sk_buff *nskb = llc_alloc_frame(sk, llc->dev);

	if (nskb) {
		struct llc_sap *sap = llc->sap;

		llc_pdu_header_init(nskb, LLC_PDU_TYPE_S, sap->laddr.lsap,
				    llc->daddr.lsap, LLC_PDU_RSP);
		llc_pdu_init_as_rr_rsp(nskb, 0, llc->vR);
		rc = llc_mac_hdr_init(nskb, llc->dev->dev_addr, llc->daddr.mac);
		if (unlikely(rc))
			goto free;
		llc_conn_send_pdu(sk, nskb);
	}
out:
	return rc;
free:
	kfree_skb(nskb);
	goto out;
}

int llc_conn_ac_send_ack_xxx_x_set_0(struct sock *sk, struct sk_buff *skb)
{
	int rc = -ENOBUFS;
	struct llc_sock *llc = llc_sk(sk);
	struct sk_buff *nskb = llc_alloc_frame(sk, llc->dev);

	if (nskb) {
		struct llc_sap *sap = llc->sap;

		llc_pdu_header_init(nskb, LLC_PDU_TYPE_S, sap->laddr.lsap,
				    llc->daddr.lsap, LLC_PDU_RSP);
		llc_pdu_init_as_rr_rsp(nskb, 0, llc->vR);
		rc = llc_mac_hdr_init(nskb, llc->dev->dev_addr, llc->daddr.mac);
		if (unlikely(rc))
			goto free;
		llc_conn_send_pdu(sk, nskb);
	}
out:
	return rc;
free:
	kfree_skb(nskb);
	goto out;
}

void llc_conn_set_p_flag(struct sock *sk, u8 value)
{
	int state_changed = llc_sk(sk)->p_flag && !value;

	llc_sk(sk)->p_flag = value;

	if (state_changed)
		sk->sk_state_change(sk);
}

int llc_conn_ac_send_sabme_cmd_p_set_x(struct sock *sk, struct sk_buff *skb)
{
	int rc = -ENOBUFS;
	struct llc_sock *llc = llc_sk(sk);
	struct sk_buff *nskb = llc_alloc_frame(sk, llc->dev);

	if (nskb) {
		struct llc_sap *sap = llc->sap;
		u8 *dmac = llc->daddr.mac;

		if (llc->dev->flags & IFF_LOOPBACK)
			dmac = llc->dev->dev_addr;
		llc_pdu_header_init(nskb, LLC_PDU_TYPE_U, sap->laddr.lsap,
				    llc->daddr.lsap, LLC_PDU_CMD);
		llc_pdu_init_as_sabme_cmd(nskb, 1);
		rc = llc_mac_hdr_init(nskb, llc->dev->dev_addr, dmac);
		if (unlikely(rc))
			goto free;
		llc_conn_send_pdu(sk, nskb);
		llc_conn_set_p_flag(sk, 1);
	}
out:
	return rc;
free:
	kfree_skb(nskb);
	goto out;
}

int llc_conn_ac_send_ua_rsp_f_set_p(struct sock *sk, struct sk_buff *skb)
{
	u8 f_bit;
	int rc = -ENOBUFS;
	struct llc_sock *llc = llc_sk(sk);
	struct sk_buff *nskb = llc_alloc_frame(sk, llc->dev);

	llc_pdu_decode_pf_bit(skb, &f_bit);
	if (nskb) {
		struct llc_sap *sap = llc->sap;

		nskb->dev = llc->dev;
		llc_pdu_header_init(nskb, LLC_PDU_TYPE_U, sap->laddr.lsap,
				    llc->daddr.lsap, LLC_PDU_RSP);
		llc_pdu_init_as_ua_rsp(nskb, f_bit);
		rc = llc_mac_hdr_init(nskb, llc->dev->dev_addr, llc->daddr.mac);
		if (unlikely(rc))
			goto free;
		llc_conn_send_pdu(sk, nskb);
	}
out:
	return rc;
free:
	kfree_skb(nskb);
	goto out;
}

int llc_conn_ac_set_s_flag_0(struct sock *sk, struct sk_buff *skb)
{
	llc_sk(sk)->s_flag = 0;
	return 0;
}

int llc_conn_ac_set_s_flag_1(struct sock *sk, struct sk_buff *skb)
{
	llc_sk(sk)->s_flag = 1;
	return 0;
}

int llc_conn_ac_start_p_timer(struct sock *sk, struct sk_buff *skb)
{
	struct llc_sock *llc = llc_sk(sk);

	llc_conn_set_p_flag(sk, 1);
	mod_timer(&llc->pf_cycle_timer.timer,
		  jiffies + llc->pf_cycle_timer.expire);
	return 0;
}

/**
 *	llc_conn_ac_send_ack_if_needed - check if ack is needed
 *	@sk: current connection structure
 *	@skb: current event
 *
 *	Checks number of received PDUs which have not been acknowledged, yet,
 *	If number of them reaches to "npta"(Number of PDUs To Acknowledge) then
 *	sends an RR response as acknowledgement for them.  Returns 0 for
 *	success, 1 otherwise.
 */
int llc_conn_ac_send_ack_if_needed(struct sock *sk, struct sk_buff *skb)
{
	u8 pf_bit;
	struct llc_sock *llc = llc_sk(sk);

	llc_pdu_decode_pf_bit(skb, &pf_bit);
	llc->ack_pf |= pf_bit & 1;
	if (!llc->ack_must_be_send) {
		llc->first_pdu_Ns = llc->vR;
		llc->ack_must_be_send = 1;
		llc->ack_pf = pf_bit & 1;
	}
	if (((llc->vR - llc->first_pdu_Ns + 129) % 128) >= llc->npta) {
		llc_conn_ac_send_rr_rsp_f_set_ackpf(sk, skb);
		llc->ack_must_be_send	= 0;
		llc->ack_pf		= 0;
		llc_conn_ac_inc_npta_value(sk, skb);
	}
	return 0;
}

/**
 *	llc_conn_ac_rst_sendack_flag - resets ack_must_be_send flag
 *	@sk: current connection structure
 *	@skb: current event
 *
 *	This action resets ack_must_be_send flag of given connection, this flag
 *	indicates if there is any PDU which has not been acknowledged yet.
 *	Returns 0 for success, 1 otherwise.
 */
int llc_conn_ac_rst_sendack_flag(struct sock *sk, struct sk_buff *skb)
{
	llc_sk(sk)->ack_must_be_send = llc_sk(sk)->ack_pf = 0;
	return 0;
}

/**
 *	llc_conn_ac_send_i_rsp_f_set_ackpf - acknowledge received PDUs
 *	@sk: current connection structure
 *	@skb: current event
 *
 *	Sends an I response PDU with f-bit set to ack_pf flag as acknowledge to
 *	all received PDUs which have not been acknowledged, yet. ack_pf flag is
 *	set to one if one PDU with p-bit set to one is received.  Returns 0 for
 *	success, 1 otherwise.
 */
static int llc_conn_ac_send_i_rsp_f_set_ackpf(struct sock *sk,
					      struct sk_buff *skb)
{
	int rc;
	struct llc_sock *llc = llc_sk(sk);
	struct llc_sap *sap = llc->sap;

	llc_pdu_header_init(skb, LLC_PDU_TYPE_I, sap->laddr.lsap,
			    llc->daddr.lsap, LLC_PDU_RSP);
	llc_pdu_init_as_i_cmd(skb, llc->ack_pf, llc->vS, llc->vR);
	rc = llc_mac_hdr_init(skb, llc->dev->dev_addr, llc->daddr.mac);
	if (likely(!rc)) {
		llc_conn_send_pdu(sk, skb);
		llc_conn_ac_inc_vs_by_1(sk, skb);
	}
	return rc;
}

/**
 *	llc_conn_ac_send_i_as_ack - sends an I-format PDU to acknowledge rx PDUs
 *	@sk: current connection structure.
 *	@skb: current event.
 *
 *	This action sends an I-format PDU as acknowledge to received PDUs which
 *	have not been acknowledged, yet, if there is any. By using of this
 *	action number of acknowledgements decreases, this technic is called
 *	piggy backing. Returns 0 for success, 1 otherwise.
 */
int llc_conn_ac_send_i_as_ack(struct sock *sk, struct sk_buff *skb)
{
	struct llc_sock *llc = llc_sk(sk);

	if (llc->ack_must_be_send) {
		llc_conn_ac_send_i_rsp_f_set_ackpf(sk, skb);
		llc->ack_must_be_send = 0 ;
		llc->ack_pf = 0;
	} else
		llc_conn_ac_send_i_cmd_p_set_0(sk, skb);
	return 0;
}

/**
 *	llc_conn_ac_send_rr_rsp_f_set_ackpf - ack all rx PDUs not yet acked
 *	@sk: current connection structure.
 *	@skb: current event.
 *
 *	This action sends an RR response with f-bit set to ack_pf flag as
 *	acknowledge to all received PDUs which have not been acknowledged, yet,
 *	if there is any. ack_pf flag indicates if a PDU has been received with
 *	p-bit set to one. Returns 0 for success, 1 otherwise.
 */
static int llc_conn_ac_send_rr_rsp_f_set_ackpf(struct sock *sk,
					       struct sk_buff *skb)
{
	int rc = -ENOBUFS;
	struct llc_sock *llc = llc_sk(sk);
	struct sk_buff *nskb = llc_alloc_frame(sk, llc->dev);

	if (nskb) {
		struct llc_sap *sap = llc->sap;

		llc_pdu_header_init(nskb, LLC_PDU_TYPE_S, sap->laddr.lsap,
				    llc->daddr.lsap, LLC_PDU_RSP);
		llc_pdu_init_as_rr_rsp(nskb, llc->ack_pf, llc->vR);
		rc = llc_mac_hdr_init(nskb, llc->dev->dev_addr, llc->daddr.mac);
		if (unlikely(rc))
			goto free;
		llc_conn_send_pdu(sk, nskb);
	}
out:
	return rc;
free:
	kfree_skb(nskb);
	goto out;
}

/**
 *	llc_conn_ac_inc_npta_value - tries to make value of npta greater
 *	@sk: current connection structure.
 *	@skb: current event.
 *
 *	After "inc_cntr" times calling of this action, "npta" increase by one.
 *	this action tries to make vale of "npta" greater as possible; number of
 *	acknowledgements decreases by increasing of "npta". Returns 0 for
 *	success, 1 otherwise.
 */
static int llc_conn_ac_inc_npta_value(struct sock *sk, struct sk_buff *skb)
{
	struct llc_sock *llc = llc_sk(sk);

	if (!llc->inc_cntr) {
		llc->dec_step = 0;
		llc->dec_cntr = llc->inc_cntr = 2;
		++llc->npta;
		if (llc->npta > 127)
			llc->npta = 127 ;
	} else
		--llc->inc_cntr;
	return 0;
}

/**
 *	llc_conn_ac_adjust_npta_by_rr - decreases "npta" by one
 *	@sk: current connection structure.
 *	@skb: current event.
 *
 *	After receiving "dec_cntr" times RR command, this action decreases
 *	"npta" by one. Returns 0 for success, 1 otherwise.
 */
int llc_conn_ac_adjust_npta_by_rr(struct sock *sk, struct sk_buff *skb)
{
	struct llc_sock *llc = llc_sk(sk);

	if (!llc->connect_step && !llc->remote_busy_flag) {
		if (!llc->dec_step) {
			if (!llc->dec_cntr) {
				llc->inc_cntr = llc->dec_cntr = 2;
				if (llc->npta > 0)
					llc->npta = llc->npta - 1;
			} else
				llc->dec_cntr -=1;
		}
	} else
		llc->connect_step = 0 ;
	return 0;
}

/**
 *	llc_conn_ac_adjust_npta_by_rnr - decreases "npta" by one
 *	@sk: current connection structure.
 *	@skb: current event.
 *
 *	After receiving "dec_cntr" times RNR command, this action decreases
 *	"npta" by one. Returns 0 for success, 1 otherwise.
 */
int llc_conn_ac_adjust_npta_by_rnr(struct sock *sk, struct sk_buff *skb)
{
	struct llc_sock *llc = llc_sk(sk);

	if (llc->remote_busy_flag)
		if (!llc->dec_step) {
			if (!llc->dec_cntr) {
				llc->inc_cntr = llc->dec_cntr = 2;
				if (llc->npta > 0)
					--llc->npta;
			} else
				--llc->dec_cntr;
		}
	return 0;
}

/**
 *	llc_conn_ac_dec_tx_win_size - decreases tx window size
 *	@sk: current connection structure.
 *	@skb: current event.
 *
 *	After receiving of a REJ command or response, transmit window size is
 *	decreased by number of PDUs which are outstanding yet. Returns 0 for
 *	success, 1 otherwise.
 */
int llc_conn_ac_dec_tx_win_size(struct sock *sk, struct sk_buff *skb)
{
	struct llc_sock *llc = llc_sk(sk);
	u8 unacked_pdu = skb_queue_len(&llc->pdu_unack_q);

	llc->k -= unacked_pdu;
	if (llc->k < 2)
		llc->k = 2;
	return 0;
}

/**
 *	llc_conn_ac_inc_tx_win_size - tx window size is inc by 1
 *	@sk: current connection structure.
 *	@skb: current event.
 *
 *	After receiving an RR response with f-bit set to one, transmit window
 *	size is increased by one. Returns 0 for success, 1 otherwise.
 */
int llc_conn_ac_inc_tx_win_size(struct sock *sk, struct sk_buff *skb)
{
	struct llc_sock *llc = llc_sk(sk);

	llc->k += 1;
	if (llc->k > 128)
		llc->k = 128 ;
	return 0;
}

int llc_conn_ac_stop_all_timers(struct sock *sk, struct sk_buff *skb)
{
	struct llc_sock *llc = llc_sk(sk);

	del_timer(&llc->pf_cycle_timer.timer);
	del_timer(&llc->ack_timer.timer);
	del_timer(&llc->rej_sent_timer.timer);
	del_timer(&llc->busy_state_timer.timer);
	llc->ack_must_be_send = 0;
	llc->ack_pf = 0;
	return 0;
}

int llc_conn_ac_stop_other_timers(struct sock *sk, struct sk_buff *skb)
{
	struct llc_sock *llc = llc_sk(sk);

	del_timer(&llc->rej_sent_timer.timer);
	del_timer(&llc->pf_cycle_timer.timer);
	del_timer(&llc->busy_state_timer.timer);
	llc->ack_must_be_send = 0;
	llc->ack_pf = 0;
	return 0;
}

int llc_conn_ac_start_ack_timer(struct sock *sk, struct sk_buff *skb)
{
	struct llc_sock *llc = llc_sk(sk);

	mod_timer(&llc->ack_timer.timer, jiffies + llc->ack_timer.expire);
	return 0;
}

int llc_conn_ac_start_rej_timer(struct sock *sk, struct sk_buff *skb)
{
	struct llc_sock *llc = llc_sk(sk);

	mod_timer(&llc->rej_sent_timer.timer,
		  jiffies + llc->rej_sent_timer.expire);
	return 0;
}

int llc_conn_ac_start_ack_tmr_if_not_running(struct sock *sk,
					     struct sk_buff *skb)
{
	struct llc_sock *llc = llc_sk(sk);

	if (!timer_pending(&llc->ack_timer.timer))
		mod_timer(&llc->ack_timer.timer,
			  jiffies + llc->ack_timer.expire);
	return 0;
}

int llc_conn_ac_stop_ack_timer(struct sock *sk, struct sk_buff *skb)
{
	del_timer(&llc_sk(sk)->ack_timer.timer);
	return 0;
}

int llc_conn_ac_stop_p_timer(struct sock *sk, struct sk_buff *skb)
{
	struct llc_sock *llc = llc_sk(sk);

	del_timer(&llc->pf_cycle_timer.timer);
	llc_conn_set_p_flag(sk, 0);
	return 0;
}

int llc_conn_ac_stop_rej_timer(struct sock *sk, struct sk_buff *skb)
{
	del_timer(&llc_sk(sk)->rej_sent_timer.timer);
	return 0;
}

int llc_conn_ac_upd_nr_received(struct sock *sk, struct sk_buff *skb)
{
	int acked;
	u16 unacked = 0;
	struct llc_pdu_sn *pdu = llc_pdu_sn_hdr(skb);
	struct llc_sock *llc = llc_sk(sk);

	llc->last_nr = PDU_SUPV_GET_Nr(pdu);
	acked = llc_conn_remove_acked_pdus(sk, llc->last_nr, &unacked);
	/* On loopback we don't queue I frames in unack_pdu_q queue. */
	if (acked > 0 || (llc->dev->flags & IFF_LOOPBACK)) {
		llc->retry_count = 0;
		del_timer(&llc->ack_timer.timer);
		if (llc->failed_data_req) {
			/* already, we did not accept data from upper layer
			 * (tx_window full or unacceptable state). Now, we
			 * can send data and must inform to upper layer.
			 */
			llc->failed_data_req = 0;
			llc_conn_ac_data_confirm(sk, skb);
		}
		if (unacked)
			mod_timer(&llc->ack_timer.timer,
				  jiffies + llc->ack_timer.expire);
	} else if (llc->failed_data_req) {
		u8 f_bit;

		llc_pdu_decode_pf_bit(skb, &f_bit);
		if (f_bit == 1) {
			llc->failed_data_req = 0;
			llc_conn_ac_data_confirm(sk, skb);
		}
	}
	return 0;
}

int llc_conn_ac_upd_p_flag(struct sock *sk, struct sk_buff *skb)
{
	struct llc_pdu_sn *pdu = llc_pdu_sn_hdr(skb);

	if (LLC_PDU_IS_RSP(pdu)) {
		u8 f_bit;

		llc_pdu_decode_pf_bit(skb, &f_bit);
		if (f_bit) {
			llc_conn_set_p_flag(sk, 0);
			llc_conn_ac_stop_p_timer(sk, skb);
		}
	}
	return 0;
}

int llc_conn_ac_set_data_flag_2(struct sock *sk, struct sk_buff *skb)
{
	llc_sk(sk)->data_flag = 2;
	return 0;
}

int llc_conn_ac_set_data_flag_0(struct sock *sk, struct sk_buff *skb)
{
	llc_sk(sk)->data_flag = 0;
	return 0;
}

int llc_conn_ac_set_data_flag_1(struct sock *sk, struct sk_buff *skb)
{
	llc_sk(sk)->data_flag = 1;
	return 0;
}

int llc_conn_ac_set_data_flag_1_if_data_flag_eq_0(struct sock *sk,
						  struct sk_buff *skb)
{
	if (!llc_sk(sk)->data_flag)
		llc_sk(sk)->data_flag = 1;
	return 0;
}

int llc_conn_ac_set_p_flag_0(struct sock *sk, struct sk_buff *skb)
{
	llc_conn_set_p_flag(sk, 0);
	return 0;
}

static int llc_conn_ac_set_p_flag_1(struct sock *sk, struct sk_buff *skb)
{
	llc_conn_set_p_flag(sk, 1);
	return 0;
}

int llc_conn_ac_set_remote_busy_0(struct sock *sk, struct sk_buff *skb)
{
	llc_sk(sk)->remote_busy_flag = 0;
	return 0;
}

int llc_conn_ac_set_cause_flag_0(struct sock *sk, struct sk_buff *skb)
{
	llc_sk(sk)->cause_flag = 0;
	return 0;
}

int llc_conn_ac_set_cause_flag_1(struct sock *sk, struct sk_buff *skb)
{
	llc_sk(sk)->cause_flag = 1;
	return 0;
}

int llc_conn_ac_set_retry_cnt_0(struct sock *sk, struct sk_buff *skb)
{
	llc_sk(sk)->retry_count = 0;
	return 0;
}

int llc_conn_ac_inc_retry_cnt_by_1(struct sock *sk, struct sk_buff *skb)
{
	llc_sk(sk)->retry_count++;
	return 0;
}

int llc_conn_ac_set_vr_0(struct sock *sk, struct sk_buff *skb)
{
	llc_sk(sk)->vR = 0;
	return 0;
}

int llc_conn_ac_inc_vr_by_1(struct sock *sk, struct sk_buff *skb)
{
	llc_sk(sk)->vR = PDU_GET_NEXT_Vr(llc_sk(sk)->vR);
	return 0;
}

int llc_conn_ac_set_vs_0(struct sock *sk, struct sk_buff *skb)
{
	llc_sk(sk)->vS = 0;
	return 0;
}

int llc_conn_ac_set_vs_nr(struct sock *sk, struct sk_buff *skb)
{
	llc_sk(sk)->vS = llc_sk(sk)->last_nr;
	return 0;
}

static int llc_conn_ac_inc_vs_by_1(struct sock *sk, struct sk_buff *skb)
{
	llc_sk(sk)->vS = (llc_sk(sk)->vS + 1) % 128;
	return 0;
}

static void llc_conn_tmr_common_cb(unsigned long timeout_data, u8 type)
{
	struct sock *sk = (struct sock *)timeout_data;
	struct sk_buff *skb = alloc_skb(0, GFP_ATOMIC);

	bh_lock_sock(sk);
	if (skb) {
		struct llc_conn_state_ev *ev = llc_conn_ev(skb);

		skb_set_owner_r(skb, sk);
		ev->type = type;
		llc_process_tmr_ev(sk, skb);
	}
	bh_unlock_sock(sk);
}

void llc_conn_pf_cycle_tmr_cb(unsigned long timeout_data)
{
	llc_conn_tmr_common_cb(timeout_data, LLC_CONN_EV_TYPE_P_TMR);
}

void llc_conn_busy_tmr_cb(unsigned long timeout_data)
{
	llc_conn_tmr_common_cb(timeout_data, LLC_CONN_EV_TYPE_BUSY_TMR);
}

void llc_conn_ack_tmr_cb(unsigned long timeout_data)
{
	llc_conn_tmr_common_cb(timeout_data, LLC_CONN_EV_TYPE_ACK_TMR);
}

void llc_conn_rej_tmr_cb(unsigned long timeout_data)
{
	llc_conn_tmr_common_cb(timeout_data, LLC_CONN_EV_TYPE_REJ_TMR);
}

int llc_conn_ac_rst_vs(struct sock *sk, struct sk_buff *skb)
{
	llc_sk(sk)->X = llc_sk(sk)->vS;
	llc_conn_ac_set_vs_nr(sk, skb);
	return 0;
}

int llc_conn_ac_upd_vs(struct sock *sk, struct sk_buff *skb)
{
	struct llc_pdu_sn *pdu = llc_pdu_sn_hdr(skb);
	u8 nr = PDU_SUPV_GET_Nr(pdu);

	if (llc_circular_between(llc_sk(sk)->vS, nr, llc_sk(sk)->X))
		llc_conn_ac_set_vs_nr(sk, skb);
	return 0;
}

/*
 * Non-standard actions; these not contained in IEEE specification; for
 * our own usage
 */
/**
 *	llc_conn_disc - removes connection from SAP list and frees it
 *	@sk: closed connection
 *	@skb: occurred event
 */
int llc_conn_disc(struct sock *sk, struct sk_buff *skb)
{
	/* FIXME: this thing seems to want to die */
	return 0;
}

/**
 *	llc_conn_reset - resets connection
 *	@sk : reseting connection.
 *	@skb: occurred event.
 *
 *	Stop all timers, empty all queues and reset all flags.
 */
int llc_conn_reset(struct sock *sk, struct sk_buff *skb)
{
	llc_sk_reset(sk);
	return 0;
}

/**
 *	llc_circular_between - designates that b is between a and c or not
 *	@a: lower bound
 *	@b: element to see if is between a and b
 *	@c: upper bound
 *
 *	This function designates that b is between a and c or not (for example,
 *	0 is between 127 and 1). Returns 1 if b is between a and c, 0
 *	otherwise.
 */
u8 llc_circular_between(u8 a, u8 b, u8 c)
{
	b = b - a;
	c = c - a;
	return b <= c;
}

/**
 *	llc_process_tmr_ev - timer backend
 *	@sk: active connection
 *	@skb: occurred event
 *
 *	This function is called from timer callback functions. When connection
 *	is busy (during sending a data frame) timer expiration event must be
 *	queued. Otherwise this event can be sent to connection state machine.
 *	Queued events will process by llc_backlog_rcv function after sending
 *	data frame.
 */
static void llc_process_tmr_ev(struct sock *sk, struct sk_buff *skb)
{
	if (llc_sk(sk)->state == LLC_CONN_OUT_OF_SVC) {
		printk(KERN_WARNING "%s: timer called on closed connection\n",
		       __FUNCTION__);
		kfree_skb(skb);
	} else {
		if (!sock_owned_by_user(sk))
			llc_conn_state_process(sk, skb);
		else {
			llc_set_backlog_type(skb, LLC_EVENT);
			sk_add_backlog(sk, skb);
		}
	}
}
