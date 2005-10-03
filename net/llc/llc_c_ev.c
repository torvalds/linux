/*
 * llc_c_ev.c - Connection component state transition event qualifiers
 *
 * A 'state' consists of a number of possible event matching functions,
 * the actions associated with each being executed when that event is
 * matched; a 'state machine' accepts events in a serial fashion from an
 * event queue. Each event is passed to each successive event matching
 * function until a match is made (the event matching function returns
 * success, or '0') or the list of event matching functions is exhausted.
 * If a match is made, the actions associated with the event are executed
 * and the state is changed to that event's transition state. Before some
 * events are recognized, even after a match has been made, a certain
 * number of 'event qualifier' functions must also be executed. If these
 * all execute successfully, then the event is finally executed.
 *
 * These event functions must return 0 for success, to show a matched
 * event, of 1 if the event does not match. Event qualifier functions
 * must return a 0 for success or a non-zero for failure. Each function
 * is simply responsible for verifying one single thing and returning
 * either a success or failure.
 *
 * All of followed event functions are described in 802.2 LLC Protocol
 * standard document except two functions that we added that will explain
 * in their comments, at below.
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
#include <net/llc_c_ac.h>
#include <net/llc_c_ev.h>
#include <net/llc_pdu.h>

#if 1
#define dprintk(args...) printk(KERN_DEBUG args)
#else
#define dprintk(args...)
#endif

/**
 *	llc_util_ns_inside_rx_window - check if sequence number is in rx window
 *	@ns: sequence number of received pdu.
 *	@vr: sequence number which receiver expects to receive.
 *	@rw: receive window size of receiver.
 *
 *	Checks if sequence number of received PDU is in range of receive
 *	window. Returns 0 for success, 1 otherwise
 */
static u16 llc_util_ns_inside_rx_window(u8 ns, u8 vr, u8 rw)
{
	return !llc_circular_between(vr, ns,
				     (vr + rw - 1) % LLC_2_SEQ_NBR_MODULO);
}

/**
 *	llc_util_nr_inside_tx_window - check if sequence number is in tx window
 *	@sk: current connection.
 *	@nr: N(R) of received PDU.
 *
 *	This routine checks if N(R) of received PDU is in range of transmit
 *	window; on the other hand checks if received PDU acknowledges some
 *	outstanding PDUs that are in transmit window. Returns 0 for success, 1
 *	otherwise.
 */
static u16 llc_util_nr_inside_tx_window(struct sock *sk, u8 nr)
{
	u8 nr1, nr2;
	struct sk_buff *skb;
	struct llc_pdu_sn *pdu;
	struct llc_sock *llc = llc_sk(sk);
	int rc = 0;

	if (llc->dev->flags & IFF_LOOPBACK)
		goto out;
	rc = 1;
	if (skb_queue_empty(&llc->pdu_unack_q))
		goto out;
	skb = skb_peek(&llc->pdu_unack_q);
	pdu = llc_pdu_sn_hdr(skb);
	nr1 = LLC_I_GET_NS(pdu);
	skb = skb_peek_tail(&llc->pdu_unack_q);
	pdu = llc_pdu_sn_hdr(skb);
	nr2 = LLC_I_GET_NS(pdu);
	rc = !llc_circular_between(nr1, nr, (nr2 + 1) % LLC_2_SEQ_NBR_MODULO);
out:
	return rc;
}

int llc_conn_ev_conn_req(struct sock *sk, struct sk_buff *skb)
{
	const struct llc_conn_state_ev *ev = llc_conn_ev(skb);

	return ev->prim == LLC_CONN_PRIM &&
	       ev->prim_type == LLC_PRIM_TYPE_REQ ? 0 : 1;
}

int llc_conn_ev_data_req(struct sock *sk, struct sk_buff *skb)
{
	const struct llc_conn_state_ev *ev = llc_conn_ev(skb);

	return ev->prim == LLC_DATA_PRIM &&
	       ev->prim_type == LLC_PRIM_TYPE_REQ ? 0 : 1;
}

int llc_conn_ev_disc_req(struct sock *sk, struct sk_buff *skb)
{
	const struct llc_conn_state_ev *ev = llc_conn_ev(skb);

	return ev->prim == LLC_DISC_PRIM &&
	       ev->prim_type == LLC_PRIM_TYPE_REQ ? 0 : 1;
}

int llc_conn_ev_rst_req(struct sock *sk, struct sk_buff *skb)
{
	const struct llc_conn_state_ev *ev = llc_conn_ev(skb);

	return ev->prim == LLC_RESET_PRIM &&
	       ev->prim_type == LLC_PRIM_TYPE_REQ ? 0 : 1;
}

int llc_conn_ev_local_busy_detected(struct sock *sk, struct sk_buff *skb)
{
	const struct llc_conn_state_ev *ev = llc_conn_ev(skb);

	return ev->type == LLC_CONN_EV_TYPE_SIMPLE &&
	       ev->prim_type == LLC_CONN_EV_LOCAL_BUSY_DETECTED ? 0 : 1;
}

int llc_conn_ev_local_busy_cleared(struct sock *sk, struct sk_buff *skb)
{
	const struct llc_conn_state_ev *ev = llc_conn_ev(skb);

	return ev->type == LLC_CONN_EV_TYPE_SIMPLE &&
	       ev->prim_type == LLC_CONN_EV_LOCAL_BUSY_CLEARED ? 0 : 1;
}

int llc_conn_ev_rx_bad_pdu(struct sock *sk, struct sk_buff *skb)
{
	return 1;
}

int llc_conn_ev_rx_disc_cmd_pbit_set_x(struct sock *sk, struct sk_buff *skb)
{
	const struct llc_pdu_un *pdu = llc_pdu_un_hdr(skb);

	return LLC_PDU_IS_CMD(pdu) && LLC_PDU_TYPE_IS_U(pdu) &&
	       LLC_U_PDU_CMD(pdu) == LLC_2_PDU_CMD_DISC ? 0 : 1;
}

int llc_conn_ev_rx_dm_rsp_fbit_set_x(struct sock *sk, struct sk_buff *skb)
{
	const struct llc_pdu_un *pdu = llc_pdu_un_hdr(skb);

	return LLC_PDU_IS_RSP(pdu) && LLC_PDU_TYPE_IS_U(pdu) &&
	       LLC_U_PDU_RSP(pdu) == LLC_2_PDU_RSP_DM ? 0 : 1;
}

int llc_conn_ev_rx_frmr_rsp_fbit_set_x(struct sock *sk, struct sk_buff *skb)
{
	const struct llc_pdu_un *pdu = llc_pdu_un_hdr(skb);

	return LLC_PDU_IS_RSP(pdu) && LLC_PDU_TYPE_IS_U(pdu) &&
	       LLC_U_PDU_RSP(pdu) == LLC_2_PDU_RSP_FRMR ? 0 : 1;
}

int llc_conn_ev_rx_i_cmd_pbit_set_0(struct sock *sk, struct sk_buff *skb)
{
	const struct llc_pdu_sn *pdu = llc_pdu_sn_hdr(skb);

	return llc_conn_space(sk, skb) &&
	       LLC_PDU_IS_CMD(pdu) && LLC_PDU_TYPE_IS_I(pdu) &&
	       LLC_I_PF_IS_0(pdu) &&
	       LLC_I_GET_NS(pdu) == llc_sk(sk)->vR ? 0 : 1;
}

int llc_conn_ev_rx_i_cmd_pbit_set_1(struct sock *sk, struct sk_buff *skb)
{
	const struct llc_pdu_sn *pdu = llc_pdu_sn_hdr(skb);

	return llc_conn_space(sk, skb) &&
	       LLC_PDU_IS_CMD(pdu) && LLC_PDU_TYPE_IS_I(pdu) &&
	       LLC_I_PF_IS_1(pdu) &&
	       LLC_I_GET_NS(pdu) == llc_sk(sk)->vR ? 0 : 1;
}

int llc_conn_ev_rx_i_cmd_pbit_set_0_unexpd_ns(struct sock *sk,
					      struct sk_buff *skb)
{
	const struct llc_pdu_sn *pdu = llc_pdu_sn_hdr(skb);
	const u8 vr = llc_sk(sk)->vR;
	const u8 ns = LLC_I_GET_NS(pdu);

	return LLC_PDU_IS_CMD(pdu) && LLC_PDU_TYPE_IS_I(pdu) &&
	       LLC_I_PF_IS_0(pdu) && ns != vr &&
	       !llc_util_ns_inside_rx_window(ns, vr, llc_sk(sk)->rw) ? 0 : 1;
}

int llc_conn_ev_rx_i_cmd_pbit_set_1_unexpd_ns(struct sock *sk,
					      struct sk_buff *skb)
{
	const struct llc_pdu_sn *pdu = llc_pdu_sn_hdr(skb);
	const u8 vr = llc_sk(sk)->vR;
	const u8 ns = LLC_I_GET_NS(pdu);

	return LLC_PDU_IS_CMD(pdu) && LLC_PDU_TYPE_IS_I(pdu) &&
	       LLC_I_PF_IS_1(pdu) && ns != vr &&
	       !llc_util_ns_inside_rx_window(ns, vr, llc_sk(sk)->rw) ? 0 : 1;
}

int llc_conn_ev_rx_i_cmd_pbit_set_x_inval_ns(struct sock *sk,
					     struct sk_buff *skb)
{
	const struct llc_pdu_sn * pdu = llc_pdu_sn_hdr(skb);
	const u8 vr = llc_sk(sk)->vR;
	const u8 ns = LLC_I_GET_NS(pdu);
	const u16 rc = LLC_PDU_IS_CMD(pdu) && LLC_PDU_TYPE_IS_I(pdu) &&
		ns != vr &&
		 llc_util_ns_inside_rx_window(ns, vr, llc_sk(sk)->rw) ? 0 : 1;
	if (!rc)
		dprintk("%s: matched, state=%d, ns=%d, vr=%d\n",
			__FUNCTION__, llc_sk(sk)->state, ns, vr);
	return rc;
}

int llc_conn_ev_rx_i_rsp_fbit_set_0(struct sock *sk, struct sk_buff *skb)
{
	const struct llc_pdu_sn *pdu = llc_pdu_sn_hdr(skb);

	return llc_conn_space(sk, skb) &&
	       LLC_PDU_IS_RSP(pdu) && LLC_PDU_TYPE_IS_I(pdu) &&
	       LLC_I_PF_IS_0(pdu) &&
	       LLC_I_GET_NS(pdu) == llc_sk(sk)->vR ? 0 : 1;
}

int llc_conn_ev_rx_i_rsp_fbit_set_1(struct sock *sk, struct sk_buff *skb)
{
	const struct llc_pdu_sn *pdu = llc_pdu_sn_hdr(skb);

	return LLC_PDU_IS_RSP(pdu) && LLC_PDU_TYPE_IS_I(pdu) &&
	       LLC_I_PF_IS_1(pdu) &&
	       LLC_I_GET_NS(pdu) == llc_sk(sk)->vR ? 0 : 1;
}

int llc_conn_ev_rx_i_rsp_fbit_set_x(struct sock *sk, struct sk_buff *skb)
{
	const struct llc_pdu_sn *pdu = llc_pdu_sn_hdr(skb);

	return llc_conn_space(sk, skb) &&
	       LLC_PDU_IS_RSP(pdu) && LLC_PDU_TYPE_IS_I(pdu) &&
	       LLC_I_GET_NS(pdu) == llc_sk(sk)->vR ? 0 : 1;
}

int llc_conn_ev_rx_i_rsp_fbit_set_0_unexpd_ns(struct sock *sk,
					      struct sk_buff *skb)
{
	const struct llc_pdu_sn *pdu = llc_pdu_sn_hdr(skb);
	const u8 vr = llc_sk(sk)->vR;
	const u8 ns = LLC_I_GET_NS(pdu);

	return LLC_PDU_IS_RSP(pdu) && LLC_PDU_TYPE_IS_I(pdu) &&
	       LLC_I_PF_IS_0(pdu) && ns != vr &&
	       !llc_util_ns_inside_rx_window(ns, vr, llc_sk(sk)->rw) ? 0 : 1;
}

int llc_conn_ev_rx_i_rsp_fbit_set_1_unexpd_ns(struct sock *sk,
					      struct sk_buff *skb)
{
	const struct llc_pdu_sn *pdu = llc_pdu_sn_hdr(skb);
	const u8 vr = llc_sk(sk)->vR;
	const u8 ns = LLC_I_GET_NS(pdu);

	return LLC_PDU_IS_RSP(pdu) && LLC_PDU_TYPE_IS_I(pdu) &&
	       LLC_I_PF_IS_1(pdu) && ns != vr &&
	       !llc_util_ns_inside_rx_window(ns, vr, llc_sk(sk)->rw) ? 0 : 1;
}

int llc_conn_ev_rx_i_rsp_fbit_set_x_unexpd_ns(struct sock *sk,
					      struct sk_buff *skb)
{
	const struct llc_pdu_sn *pdu = llc_pdu_sn_hdr(skb);
	const u8 vr = llc_sk(sk)->vR;
	const u8 ns = LLC_I_GET_NS(pdu);

	return LLC_PDU_IS_RSP(pdu) && LLC_PDU_TYPE_IS_I(pdu) && ns != vr &&
	       !llc_util_ns_inside_rx_window(ns, vr, llc_sk(sk)->rw) ? 0 : 1;
}

int llc_conn_ev_rx_i_rsp_fbit_set_x_inval_ns(struct sock *sk,
					     struct sk_buff *skb)
{
	const struct llc_pdu_sn *pdu = llc_pdu_sn_hdr(skb);
	const u8 vr = llc_sk(sk)->vR;
	const u8 ns = LLC_I_GET_NS(pdu);
	const u16 rc = LLC_PDU_IS_RSP(pdu) && LLC_PDU_TYPE_IS_I(pdu) &&
		ns != vr &&
		 llc_util_ns_inside_rx_window(ns, vr, llc_sk(sk)->rw) ? 0 : 1;
	if (!rc)
		dprintk("%s: matched, state=%d, ns=%d, vr=%d\n",
			__FUNCTION__, llc_sk(sk)->state, ns, vr);
	return rc;
}

int llc_conn_ev_rx_rej_cmd_pbit_set_0(struct sock *sk, struct sk_buff *skb)
{
	const struct llc_pdu_sn *pdu = llc_pdu_sn_hdr(skb);

	return LLC_PDU_IS_CMD(pdu) && LLC_PDU_TYPE_IS_S(pdu) &&
	       LLC_S_PF_IS_0(pdu) &&
	       LLC_S_PDU_CMD(pdu) == LLC_2_PDU_CMD_REJ ? 0 : 1;
}

int llc_conn_ev_rx_rej_cmd_pbit_set_1(struct sock *sk, struct sk_buff *skb)
{
	const struct llc_pdu_sn *pdu = llc_pdu_sn_hdr(skb);

	return LLC_PDU_IS_CMD(pdu) && LLC_PDU_TYPE_IS_S(pdu) &&
	       LLC_S_PF_IS_1(pdu) &&
	       LLC_S_PDU_CMD(pdu) == LLC_2_PDU_CMD_REJ ? 0 : 1;
}

int llc_conn_ev_rx_rej_rsp_fbit_set_0(struct sock *sk, struct sk_buff *skb)
{
	const struct llc_pdu_sn *pdu = llc_pdu_sn_hdr(skb);

	return LLC_PDU_IS_RSP(pdu) && LLC_PDU_TYPE_IS_S(pdu) &&
	       LLC_S_PF_IS_0(pdu) &&
	       LLC_S_PDU_RSP(pdu) == LLC_2_PDU_RSP_REJ ? 0 : 1;
}

int llc_conn_ev_rx_rej_rsp_fbit_set_1(struct sock *sk, struct sk_buff *skb)
{
	const struct llc_pdu_sn *pdu = llc_pdu_sn_hdr(skb);

	return LLC_PDU_IS_RSP(pdu) && LLC_PDU_TYPE_IS_S(pdu) &&
	       LLC_S_PF_IS_1(pdu) &&
	       LLC_S_PDU_RSP(pdu) == LLC_2_PDU_RSP_REJ ? 0 : 1;
}

int llc_conn_ev_rx_rej_rsp_fbit_set_x(struct sock *sk, struct sk_buff *skb)
{
	const struct llc_pdu_un *pdu = llc_pdu_un_hdr(skb);

	return LLC_PDU_IS_RSP(pdu) && LLC_PDU_TYPE_IS_S(pdu) &&
	       LLC_S_PDU_RSP(pdu) == LLC_2_PDU_RSP_REJ ? 0 : 1;
}

int llc_conn_ev_rx_rnr_cmd_pbit_set_0(struct sock *sk, struct sk_buff *skb)
{
	const struct llc_pdu_sn *pdu = llc_pdu_sn_hdr(skb);

	return LLC_PDU_IS_CMD(pdu) && LLC_PDU_TYPE_IS_S(pdu) &&
	       LLC_S_PF_IS_0(pdu) &&
	       LLC_S_PDU_CMD(pdu) == LLC_2_PDU_CMD_RNR ? 0 : 1;
}

int llc_conn_ev_rx_rnr_cmd_pbit_set_1(struct sock *sk, struct sk_buff *skb)
{
	const struct llc_pdu_sn *pdu = llc_pdu_sn_hdr(skb);

	return LLC_PDU_IS_CMD(pdu) && LLC_PDU_TYPE_IS_S(pdu) &&
	       LLC_S_PF_IS_1(pdu) &&
	       LLC_S_PDU_CMD(pdu) == LLC_2_PDU_CMD_RNR ? 0 : 1;
}

int llc_conn_ev_rx_rnr_rsp_fbit_set_0(struct sock *sk, struct sk_buff *skb)
{
	const struct llc_pdu_sn *pdu = llc_pdu_sn_hdr(skb);

	return LLC_PDU_IS_RSP(pdu) && LLC_PDU_TYPE_IS_S(pdu) &&
	       LLC_S_PF_IS_0(pdu) &&
	       LLC_S_PDU_RSP(pdu) == LLC_2_PDU_RSP_RNR ? 0 : 1;
}

int llc_conn_ev_rx_rnr_rsp_fbit_set_1(struct sock *sk, struct sk_buff *skb)
{
	const struct llc_pdu_sn *pdu = llc_pdu_sn_hdr(skb);

	return LLC_PDU_IS_RSP(pdu) && LLC_PDU_TYPE_IS_S(pdu) &&
	       LLC_S_PF_IS_1(pdu) &&
	       LLC_S_PDU_RSP(pdu) == LLC_2_PDU_RSP_RNR ? 0 : 1;
}

int llc_conn_ev_rx_rr_cmd_pbit_set_0(struct sock *sk, struct sk_buff *skb)
{
	const struct llc_pdu_sn *pdu = llc_pdu_sn_hdr(skb);

	return LLC_PDU_IS_CMD(pdu) && LLC_PDU_TYPE_IS_S(pdu) &&
	       LLC_S_PF_IS_0(pdu) &&
	       LLC_S_PDU_CMD(pdu) == LLC_2_PDU_CMD_RR ? 0 : 1;
}

int llc_conn_ev_rx_rr_cmd_pbit_set_1(struct sock *sk, struct sk_buff *skb)
{
	const struct llc_pdu_sn *pdu = llc_pdu_sn_hdr(skb);

	return LLC_PDU_IS_CMD(pdu) && LLC_PDU_TYPE_IS_S(pdu) &&
	       LLC_S_PF_IS_1(pdu) &&
	       LLC_S_PDU_CMD(pdu) == LLC_2_PDU_CMD_RR ? 0 : 1;
}

int llc_conn_ev_rx_rr_rsp_fbit_set_0(struct sock *sk, struct sk_buff *skb)
{
	const struct llc_pdu_sn *pdu = llc_pdu_sn_hdr(skb);

	return llc_conn_space(sk, skb) &&
	       LLC_PDU_IS_RSP(pdu) && LLC_PDU_TYPE_IS_S(pdu) &&
	       LLC_S_PF_IS_0(pdu) &&
	       LLC_S_PDU_RSP(pdu) == LLC_2_PDU_RSP_RR ? 0 : 1;
}

int llc_conn_ev_rx_rr_rsp_fbit_set_1(struct sock *sk, struct sk_buff *skb)
{
	const struct llc_pdu_sn *pdu = llc_pdu_sn_hdr(skb);

	return llc_conn_space(sk, skb) &&
	       LLC_PDU_IS_RSP(pdu) && LLC_PDU_TYPE_IS_S(pdu) &&
	       LLC_S_PF_IS_1(pdu) &&
	       LLC_S_PDU_RSP(pdu) == LLC_2_PDU_RSP_RR ? 0 : 1;
}

int llc_conn_ev_rx_sabme_cmd_pbit_set_x(struct sock *sk, struct sk_buff *skb)
{
	const struct llc_pdu_un *pdu = llc_pdu_un_hdr(skb);

	return LLC_PDU_IS_CMD(pdu) && LLC_PDU_TYPE_IS_U(pdu) &&
	       LLC_U_PDU_CMD(pdu) == LLC_2_PDU_CMD_SABME ? 0 : 1;
}

int llc_conn_ev_rx_ua_rsp_fbit_set_x(struct sock *sk, struct sk_buff *skb)
{
	struct llc_pdu_un *pdu = llc_pdu_un_hdr(skb);

	return LLC_PDU_IS_RSP(pdu) && LLC_PDU_TYPE_IS_U(pdu) &&
	       LLC_U_PDU_RSP(pdu) == LLC_2_PDU_RSP_UA ? 0 : 1;
}

int llc_conn_ev_rx_xxx_cmd_pbit_set_1(struct sock *sk, struct sk_buff *skb)
{
	u16 rc = 1;
	const struct llc_pdu_sn *pdu = llc_pdu_sn_hdr(skb);

	if (LLC_PDU_IS_CMD(pdu)) {
		if (LLC_PDU_TYPE_IS_I(pdu) || LLC_PDU_TYPE_IS_S(pdu)) {
			if (LLC_I_PF_IS_1(pdu))
				rc = 0;
		} else if (LLC_PDU_TYPE_IS_U(pdu) && LLC_U_PF_IS_1(pdu))
			rc = 0;
	}
	return rc;
}

int llc_conn_ev_rx_xxx_cmd_pbit_set_x(struct sock *sk, struct sk_buff *skb)
{
	u16 rc = 1;
	const struct llc_pdu_un *pdu = llc_pdu_un_hdr(skb);

	if (LLC_PDU_IS_CMD(pdu)) {
		if (LLC_PDU_TYPE_IS_I(pdu) || LLC_PDU_TYPE_IS_S(pdu))
			rc = 0;
		else if (LLC_PDU_TYPE_IS_U(pdu))
			switch (LLC_U_PDU_CMD(pdu)) {
			case LLC_2_PDU_CMD_SABME:
			case LLC_2_PDU_CMD_DISC:
				rc = 0;
				break;
			}
	}
	return rc;
}

int llc_conn_ev_rx_xxx_rsp_fbit_set_x(struct sock *sk, struct sk_buff *skb)
{
	u16 rc = 1;
	const struct llc_pdu_un *pdu = llc_pdu_un_hdr(skb);

	if (LLC_PDU_IS_RSP(pdu)) {
		if (LLC_PDU_TYPE_IS_I(pdu) || LLC_PDU_TYPE_IS_S(pdu))
			rc = 0;
		else if (LLC_PDU_TYPE_IS_U(pdu))
			switch (LLC_U_PDU_RSP(pdu)) {
			case LLC_2_PDU_RSP_UA:
			case LLC_2_PDU_RSP_DM:
			case LLC_2_PDU_RSP_FRMR:
				rc = 0;
				break;
			}
	}

	return rc;
}

int llc_conn_ev_rx_zzz_cmd_pbit_set_x_inval_nr(struct sock *sk,
					       struct sk_buff *skb)
{
	u16 rc = 1;
	const struct llc_pdu_sn *pdu = llc_pdu_sn_hdr(skb);
	const u8 vs = llc_sk(sk)->vS;
	const u8 nr = LLC_I_GET_NR(pdu);

	if (LLC_PDU_IS_CMD(pdu) &&
	    (LLC_PDU_TYPE_IS_I(pdu) || LLC_PDU_TYPE_IS_S(pdu)) &&
	    nr != vs && llc_util_nr_inside_tx_window(sk, nr)) {
		dprintk("%s: matched, state=%d, vs=%d, nr=%d\n",
			__FUNCTION__, llc_sk(sk)->state, vs, nr);
		rc = 0;
	}
	return rc;
}

int llc_conn_ev_rx_zzz_rsp_fbit_set_x_inval_nr(struct sock *sk,
					       struct sk_buff *skb)
{
	u16 rc = 1;
	const struct llc_pdu_sn *pdu = llc_pdu_sn_hdr(skb);
	const u8 vs = llc_sk(sk)->vS;
	const u8 nr = LLC_I_GET_NR(pdu);

	if (LLC_PDU_IS_RSP(pdu) &&
	    (LLC_PDU_TYPE_IS_I(pdu) || LLC_PDU_TYPE_IS_S(pdu)) &&
	    nr != vs && llc_util_nr_inside_tx_window(sk, nr)) {
		rc = 0;
		dprintk("%s: matched, state=%d, vs=%d, nr=%d\n",
			__FUNCTION__, llc_sk(sk)->state, vs, nr);
	}
	return rc;
}

int llc_conn_ev_rx_any_frame(struct sock *sk, struct sk_buff *skb)
{
	return 0;
}

int llc_conn_ev_p_tmr_exp(struct sock *sk, struct sk_buff *skb)
{
	const struct llc_conn_state_ev *ev = llc_conn_ev(skb);

	return ev->type != LLC_CONN_EV_TYPE_P_TMR;
}

int llc_conn_ev_ack_tmr_exp(struct sock *sk, struct sk_buff *skb)
{
	const struct llc_conn_state_ev *ev = llc_conn_ev(skb);

	return ev->type != LLC_CONN_EV_TYPE_ACK_TMR;
}

int llc_conn_ev_rej_tmr_exp(struct sock *sk, struct sk_buff *skb)
{
	const struct llc_conn_state_ev *ev = llc_conn_ev(skb);

	return ev->type != LLC_CONN_EV_TYPE_REJ_TMR;
}

int llc_conn_ev_busy_tmr_exp(struct sock *sk, struct sk_buff *skb)
{
	const struct llc_conn_state_ev *ev = llc_conn_ev(skb);

	return ev->type != LLC_CONN_EV_TYPE_BUSY_TMR;
}

int llc_conn_ev_init_p_f_cycle(struct sock *sk, struct sk_buff *skb)
{
	return 1;
}

int llc_conn_ev_tx_buffer_full(struct sock *sk, struct sk_buff *skb)
{
	const struct llc_conn_state_ev *ev = llc_conn_ev(skb);

	return ev->type == LLC_CONN_EV_TYPE_SIMPLE &&
	       ev->prim_type == LLC_CONN_EV_TX_BUFF_FULL ? 0 : 1;
}

/* Event qualifier functions
 *
 * these functions simply verify the value of a state flag associated with
 * the connection and return either a 0 for success or a non-zero value
 * for not-success; verify the event is the type we expect
 */
int llc_conn_ev_qlfy_data_flag_eq_1(struct sock *sk, struct sk_buff *skb)
{
	return llc_sk(sk)->data_flag != 1;
}

int llc_conn_ev_qlfy_data_flag_eq_0(struct sock *sk, struct sk_buff *skb)
{
	return llc_sk(sk)->data_flag;
}

int llc_conn_ev_qlfy_data_flag_eq_2(struct sock *sk, struct sk_buff *skb)
{
	return llc_sk(sk)->data_flag != 2;
}

int llc_conn_ev_qlfy_p_flag_eq_1(struct sock *sk, struct sk_buff *skb)
{
	return llc_sk(sk)->p_flag != 1;
}

/**
 *	conn_ev_qlfy_last_frame_eq_1 - checks if frame is last in tx window
 *	@sk: current connection structure.
 *	@skb: current event.
 *
 *	This function determines when frame which is sent, is last frame of
 *	transmit window, if it is then this function return zero else return
 *	one.  This function is used for sending last frame of transmit window
 *	as I-format command with p-bit set to one. Returns 0 if frame is last
 *	frame, 1 otherwise.
 */
int llc_conn_ev_qlfy_last_frame_eq_1(struct sock *sk, struct sk_buff *skb)
{
	return !(skb_queue_len(&llc_sk(sk)->pdu_unack_q) + 1 == llc_sk(sk)->k);
}

/**
 *	conn_ev_qlfy_last_frame_eq_0 - checks if frame isn't last in tx window
 *	@sk: current connection structure.
 *	@skb: current event.
 *
 *	This function determines when frame which is sent, isn't last frame of
 *	transmit window, if it isn't then this function return zero else return
 *	one. Returns 0 if frame isn't last frame, 1 otherwise.
 */
int llc_conn_ev_qlfy_last_frame_eq_0(struct sock *sk, struct sk_buff *skb)
{
	return skb_queue_len(&llc_sk(sk)->pdu_unack_q) + 1 == llc_sk(sk)->k;
}

int llc_conn_ev_qlfy_p_flag_eq_0(struct sock *sk, struct sk_buff *skb)
{
	return llc_sk(sk)->p_flag;
}

int llc_conn_ev_qlfy_p_flag_eq_f(struct sock *sk, struct sk_buff *skb)
{
	u8 f_bit;

	llc_pdu_decode_pf_bit(skb, &f_bit);
	return llc_sk(sk)->p_flag == f_bit ? 0 : 1;
}

int llc_conn_ev_qlfy_remote_busy_eq_0(struct sock *sk, struct sk_buff *skb)
{
	return llc_sk(sk)->remote_busy_flag;
}

int llc_conn_ev_qlfy_remote_busy_eq_1(struct sock *sk, struct sk_buff *skb)
{
	return !llc_sk(sk)->remote_busy_flag;
}

int llc_conn_ev_qlfy_retry_cnt_lt_n2(struct sock *sk, struct sk_buff *skb)
{
	return !(llc_sk(sk)->retry_count < llc_sk(sk)->n2);
}

int llc_conn_ev_qlfy_retry_cnt_gte_n2(struct sock *sk, struct sk_buff *skb)
{
	return !(llc_sk(sk)->retry_count >= llc_sk(sk)->n2);
}

int llc_conn_ev_qlfy_s_flag_eq_1(struct sock *sk, struct sk_buff *skb)
{
	return !llc_sk(sk)->s_flag;
}

int llc_conn_ev_qlfy_s_flag_eq_0(struct sock *sk, struct sk_buff *skb)
{
	return llc_sk(sk)->s_flag;
}

int llc_conn_ev_qlfy_cause_flag_eq_1(struct sock *sk, struct sk_buff *skb)
{
	return !llc_sk(sk)->cause_flag;
}

int llc_conn_ev_qlfy_cause_flag_eq_0(struct sock *sk, struct sk_buff *skb)
{
	return llc_sk(sk)->cause_flag;
}

int llc_conn_ev_qlfy_set_status_conn(struct sock *sk, struct sk_buff *skb)
{
	struct llc_conn_state_ev *ev = llc_conn_ev(skb);

	ev->status = LLC_STATUS_CONN;
	return 0;
}

int llc_conn_ev_qlfy_set_status_disc(struct sock *sk, struct sk_buff *skb)
{
	struct llc_conn_state_ev *ev = llc_conn_ev(skb);

	ev->status = LLC_STATUS_DISC;
	return 0;
}

int llc_conn_ev_qlfy_set_status_failed(struct sock *sk, struct sk_buff *skb)
{
	struct llc_conn_state_ev *ev = llc_conn_ev(skb);

	ev->status = LLC_STATUS_FAILED;
	return 0;
}

int llc_conn_ev_qlfy_set_status_remote_busy(struct sock *sk,
					    struct sk_buff *skb)
{
	struct llc_conn_state_ev *ev = llc_conn_ev(skb);

	ev->status = LLC_STATUS_REMOTE_BUSY;
	return 0;
}

int llc_conn_ev_qlfy_set_status_refuse(struct sock *sk, struct sk_buff *skb)
{
	struct llc_conn_state_ev *ev = llc_conn_ev(skb);

	ev->status = LLC_STATUS_REFUSE;
	return 0;
}

int llc_conn_ev_qlfy_set_status_conflict(struct sock *sk, struct sk_buff *skb)
{
	struct llc_conn_state_ev *ev = llc_conn_ev(skb);

	ev->status = LLC_STATUS_CONFLICT;
	return 0;
}

int llc_conn_ev_qlfy_set_status_rst_done(struct sock *sk, struct sk_buff *skb)
{
	struct llc_conn_state_ev *ev = llc_conn_ev(skb);

	ev->status = LLC_STATUS_RESET_DONE;
	return 0;
}
