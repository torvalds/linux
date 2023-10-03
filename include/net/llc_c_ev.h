#ifndef LLC_C_EV_H
#define LLC_C_EV_H
/*
 * Copyright (c) 1997 by Procom Technology,Inc.
 *		 2001 by Arnaldo Carvalho de Melo <acme@conectiva.com.br>
 *
 * This program can be redistributed or modified under the terms of the
 * GNU General Public License as published by the Free Software Foundation.
 * This program is distributed without any warranty or implied warranty
 * of merchantability or fitness for a particular purpose.
 *
 * See the GNU General Public License for more details.
 */

#include <net/sock.h>

/* Connection component state transition event qualifiers */
/* Types of events (possible values in 'ev->type') */
#define LLC_CONN_EV_TYPE_SIMPLE		 1
#define LLC_CONN_EV_TYPE_CONDITION	 2
#define LLC_CONN_EV_TYPE_PRIM		 3
#define LLC_CONN_EV_TYPE_PDU		 4	/* command/response PDU */
#define LLC_CONN_EV_TYPE_ACK_TMR	 5
#define LLC_CONN_EV_TYPE_P_TMR		 6
#define LLC_CONN_EV_TYPE_REJ_TMR	 7
#define LLC_CONN_EV_TYPE_BUSY_TMR	 8
#define LLC_CONN_EV_TYPE_RPT_STATUS	 9
#define LLC_CONN_EV_TYPE_SENDACK_TMR	10

#define NBR_CONN_EV		   5
/* Connection events which cause state transitions when fully qualified */

#define LLC_CONN_EV_CONN_REQ				 1
#define LLC_CONN_EV_CONN_RESP				 2
#define LLC_CONN_EV_DATA_REQ				 3
#define LLC_CONN_EV_DISC_REQ				 4
#define LLC_CONN_EV_RESET_REQ				 5
#define LLC_CONN_EV_RESET_RESP				 6
#define LLC_CONN_EV_LOCAL_BUSY_DETECTED			 7
#define LLC_CONN_EV_LOCAL_BUSY_CLEARED			 8
#define LLC_CONN_EV_RX_BAD_PDU				 9
#define LLC_CONN_EV_RX_DISC_CMD_Pbit_SET_X		10
#define LLC_CONN_EV_RX_DM_RSP_Fbit_SET_X		11
#define LLC_CONN_EV_RX_FRMR_RSP_Fbit_SET_X		12
#define LLC_CONN_EV_RX_I_CMD_Pbit_SET_X			13
#define LLC_CONN_EV_RX_I_CMD_Pbit_SET_X_UNEXPD_Ns	14
#define LLC_CONN_EV_RX_I_CMD_Pbit_SET_X_INVAL_Ns	15
#define LLC_CONN_EV_RX_I_RSP_Fbit_SET_X			16
#define LLC_CONN_EV_RX_I_RSP_Fbit_SET_X_UNEXPD_Ns	17
#define LLC_CONN_EV_RX_I_RSP_Fbit_SET_X_INVAL_Ns	18
#define LLC_CONN_EV_RX_REJ_CMD_Pbit_SET_X		19
#define LLC_CONN_EV_RX_REJ_RSP_Fbit_SET_X		20
#define LLC_CONN_EV_RX_RNR_CMD_Pbit_SET_X		21
#define LLC_CONN_EV_RX_RNR_RSP_Fbit_SET_X		22
#define LLC_CONN_EV_RX_RR_CMD_Pbit_SET_X		23
#define LLC_CONN_EV_RX_RR_RSP_Fbit_SET_X		24
#define LLC_CONN_EV_RX_SABME_CMD_Pbit_SET_X		25
#define LLC_CONN_EV_RX_UA_RSP_Fbit_SET_X		26
#define LLC_CONN_EV_RX_XXX_CMD_Pbit_SET_X		27
#define LLC_CONN_EV_RX_XXX_RSP_Fbit_SET_X		28
#define LLC_CONN_EV_RX_XXX_YYY				29
#define LLC_CONN_EV_RX_ZZZ_CMD_Pbit_SET_X_INVAL_Nr	30
#define LLC_CONN_EV_RX_ZZZ_RSP_Fbit_SET_X_INVAL_Nr	31
#define LLC_CONN_EV_P_TMR_EXP				32
#define LLC_CONN_EV_ACK_TMR_EXP				33
#define LLC_CONN_EV_REJ_TMR_EXP				34
#define LLC_CONN_EV_BUSY_TMR_EXP			35
#define LLC_CONN_EV_RX_XXX_CMD_Pbit_SET_1		36
#define LLC_CONN_EV_RX_XXX_CMD_Pbit_SET_0		37
#define LLC_CONN_EV_RX_I_CMD_Pbit_SET_0_UNEXPD_Ns	38
#define LLC_CONN_EV_RX_I_RSP_Fbit_SET_0_UNEXPD_Ns	39
#define LLC_CONN_EV_RX_I_RSP_Fbit_SET_1_UNEXPD_Ns	40
#define LLC_CONN_EV_RX_I_CMD_Pbit_SET_1_UNEXPD_Ns	41
#define LLC_CONN_EV_RX_I_CMD_Pbit_SET_0			42
#define LLC_CONN_EV_RX_I_RSP_Fbit_SET_0			43
#define LLC_CONN_EV_RX_I_CMD_Pbit_SET_1			44
#define LLC_CONN_EV_RX_RR_CMD_Pbit_SET_0		45
#define LLC_CONN_EV_RX_RR_RSP_Fbit_SET_0		46
#define LLC_CONN_EV_RX_RR_RSP_Fbit_SET_1		47
#define LLC_CONN_EV_RX_RR_CMD_Pbit_SET_1		48
#define LLC_CONN_EV_RX_RNR_CMD_Pbit_SET_0		49
#define LLC_CONN_EV_RX_RNR_RSP_Fbit_SET_0		50
#define LLC_CONN_EV_RX_RNR_RSP_Fbit_SET_1		51
#define LLC_CONN_EV_RX_RNR_CMD_Pbit_SET_1		52
#define LLC_CONN_EV_RX_REJ_CMD_Pbit_SET_0		53
#define LLC_CONN_EV_RX_REJ_RSP_Fbit_SET_0		54
#define LLC_CONN_EV_RX_REJ_CMD_Pbit_SET_1		55
#define LLC_CONN_EV_RX_I_RSP_Fbit_SET_1			56
#define LLC_CONN_EV_RX_REJ_RSP_Fbit_SET_1		57
#define LLC_CONN_EV_RX_XXX_RSP_Fbit_SET_1		58
#define LLC_CONN_EV_TX_BUFF_FULL			59

#define LLC_CONN_EV_INIT_P_F_CYCLE			100
/*
 * Connection event qualifiers; for some events a certain combination of
 * these qualifiers must be TRUE before event recognized valid for state;
 * these constants act as indexes into the Event Qualifier function
 * table
 */
#define LLC_CONN_EV_QFY_DATA_FLAG_EQ_1		 1
#define LLC_CONN_EV_QFY_DATA_FLAG_EQ_0		 2
#define LLC_CONN_EV_QFY_DATA_FLAG_EQ_2		 3
#define LLC_CONN_EV_QFY_P_FLAG_EQ_1		 4
#define LLC_CONN_EV_QFY_P_FLAG_EQ_0		 5
#define LLC_CONN_EV_QFY_P_FLAG_EQ_Fbit		 6
#define LLC_CONN_EV_QFY_REMOTE_BUSY_EQ_0	 7
#define LLC_CONN_EV_QFY_RETRY_CNT_LT_N2		 8
#define LLC_CONN_EV_QFY_RETRY_CNT_GTE_N2	 9
#define LLC_CONN_EV_QFY_S_FLAG_EQ_1		10
#define LLC_CONN_EV_QFY_S_FLAG_EQ_0		11
#define LLC_CONN_EV_QFY_INIT_P_F_CYCLE		12

struct llc_conn_state_ev {
	u8 type;
	u8 prim;
	u8 prim_type;
	u8 reason;
	u8 status;
	u8 ind_prim;
	u8 cfm_prim;
};

static __inline__ struct llc_conn_state_ev *llc_conn_ev(struct sk_buff *skb)
{
	return (struct llc_conn_state_ev *)skb->cb;
}

typedef int (*llc_conn_ev_t)(struct sock *sk, struct sk_buff *skb);
typedef int (*llc_conn_ev_qfyr_t)(struct sock *sk, struct sk_buff *skb);

int llc_conn_ev_conn_req(struct sock *sk, struct sk_buff *skb);
int llc_conn_ev_data_req(struct sock *sk, struct sk_buff *skb);
int llc_conn_ev_disc_req(struct sock *sk, struct sk_buff *skb);
int llc_conn_ev_rst_req(struct sock *sk, struct sk_buff *skb);
int llc_conn_ev_local_busy_detected(struct sock *sk, struct sk_buff *skb);
int llc_conn_ev_local_busy_cleared(struct sock *sk, struct sk_buff *skb);
int llc_conn_ev_rx_bad_pdu(struct sock *sk, struct sk_buff *skb);
int llc_conn_ev_rx_disc_cmd_pbit_set_x(struct sock *sk, struct sk_buff *skb);
int llc_conn_ev_rx_dm_rsp_fbit_set_x(struct sock *sk, struct sk_buff *skb);
int llc_conn_ev_rx_frmr_rsp_fbit_set_x(struct sock *sk, struct sk_buff *skb);
int llc_conn_ev_rx_i_cmd_pbit_set_x_inval_ns(struct sock *sk,
					     struct sk_buff *skb);
int llc_conn_ev_rx_i_rsp_fbit_set_x(struct sock *sk, struct sk_buff *skb);
int llc_conn_ev_rx_i_rsp_fbit_set_x_unexpd_ns(struct sock *sk,
					      struct sk_buff *skb);
int llc_conn_ev_rx_i_rsp_fbit_set_x_inval_ns(struct sock *sk,
					     struct sk_buff *skb);
int llc_conn_ev_rx_rej_rsp_fbit_set_x(struct sock *sk, struct sk_buff *skb);
int llc_conn_ev_rx_sabme_cmd_pbit_set_x(struct sock *sk, struct sk_buff *skb);
int llc_conn_ev_rx_ua_rsp_fbit_set_x(struct sock *sk, struct sk_buff *skb);
int llc_conn_ev_rx_xxx_cmd_pbit_set_x(struct sock *sk, struct sk_buff *skb);
int llc_conn_ev_rx_xxx_rsp_fbit_set_x(struct sock *sk, struct sk_buff *skb);
int llc_conn_ev_rx_zzz_cmd_pbit_set_x_inval_nr(struct sock *sk,
					       struct sk_buff *skb);
int llc_conn_ev_rx_zzz_rsp_fbit_set_x_inval_nr(struct sock *sk,
					       struct sk_buff *skb);
int llc_conn_ev_p_tmr_exp(struct sock *sk, struct sk_buff *skb);
int llc_conn_ev_ack_tmr_exp(struct sock *sk, struct sk_buff *skb);
int llc_conn_ev_rej_tmr_exp(struct sock *sk, struct sk_buff *skb);
int llc_conn_ev_busy_tmr_exp(struct sock *sk, struct sk_buff *skb);
/* NOT_USED functions and their variations */
int llc_conn_ev_rx_xxx_cmd_pbit_set_1(struct sock *sk, struct sk_buff *skb);
int llc_conn_ev_rx_xxx_rsp_fbit_set_1(struct sock *sk, struct sk_buff *skb);
int llc_conn_ev_rx_i_cmd_pbit_set_0_unexpd_ns(struct sock *sk,
					      struct sk_buff *skb);
int llc_conn_ev_rx_i_cmd_pbit_set_1_unexpd_ns(struct sock *sk,
					      struct sk_buff *skb);
int llc_conn_ev_rx_i_cmd_pbit_set_0(struct sock *sk, struct sk_buff *skb);
int llc_conn_ev_rx_i_cmd_pbit_set_1(struct sock *sk, struct sk_buff *skb);
int llc_conn_ev_rx_i_rsp_fbit_set_0_unexpd_ns(struct sock *sk,
					      struct sk_buff *skb);
int llc_conn_ev_rx_i_rsp_fbit_set_1_unexpd_ns(struct sock *sk,
					      struct sk_buff *skb);
int llc_conn_ev_rx_i_rsp_fbit_set_0(struct sock *sk, struct sk_buff *skb);
int llc_conn_ev_rx_i_rsp_fbit_set_1(struct sock *sk, struct sk_buff *skb);
int llc_conn_ev_rx_rr_cmd_pbit_set_0(struct sock *sk, struct sk_buff *skb);
int llc_conn_ev_rx_rr_cmd_pbit_set_1(struct sock *sk, struct sk_buff *skb);
int llc_conn_ev_rx_rr_rsp_fbit_set_0(struct sock *sk, struct sk_buff *skb);
int llc_conn_ev_rx_rr_rsp_fbit_set_1(struct sock *sk, struct sk_buff *skb);
int llc_conn_ev_rx_rnr_cmd_pbit_set_0(struct sock *sk, struct sk_buff *skb);
int llc_conn_ev_rx_rnr_cmd_pbit_set_1(struct sock *sk, struct sk_buff *skb);
int llc_conn_ev_rx_rnr_rsp_fbit_set_0(struct sock *sk, struct sk_buff *skb);
int llc_conn_ev_rx_rnr_rsp_fbit_set_1(struct sock *sk, struct sk_buff *skb);
int llc_conn_ev_rx_rej_cmd_pbit_set_0(struct sock *sk, struct sk_buff *skb);
int llc_conn_ev_rx_rej_cmd_pbit_set_1(struct sock *sk, struct sk_buff *skb);
int llc_conn_ev_rx_rej_rsp_fbit_set_0(struct sock *sk, struct sk_buff *skb);
int llc_conn_ev_rx_rej_rsp_fbit_set_1(struct sock *sk, struct sk_buff *skb);
int llc_conn_ev_rx_any_frame(struct sock *sk, struct sk_buff *skb);
int llc_conn_ev_tx_buffer_full(struct sock *sk, struct sk_buff *skb);
int llc_conn_ev_init_p_f_cycle(struct sock *sk, struct sk_buff *skb);

/* Available connection action qualifiers */
int llc_conn_ev_qlfy_data_flag_eq_1(struct sock *sk, struct sk_buff *skb);
int llc_conn_ev_qlfy_data_flag_eq_0(struct sock *sk, struct sk_buff *skb);
int llc_conn_ev_qlfy_data_flag_eq_2(struct sock *sk, struct sk_buff *skb);
int llc_conn_ev_qlfy_p_flag_eq_1(struct sock *sk, struct sk_buff *skb);
int llc_conn_ev_qlfy_last_frame_eq_1(struct sock *sk, struct sk_buff *skb);
int llc_conn_ev_qlfy_last_frame_eq_0(struct sock *sk, struct sk_buff *skb);
int llc_conn_ev_qlfy_p_flag_eq_0(struct sock *sk, struct sk_buff *skb);
int llc_conn_ev_qlfy_p_flag_eq_f(struct sock *sk, struct sk_buff *skb);
int llc_conn_ev_qlfy_remote_busy_eq_0(struct sock *sk, struct sk_buff *skb);
int llc_conn_ev_qlfy_remote_busy_eq_1(struct sock *sk, struct sk_buff *skb);
int llc_conn_ev_qlfy_retry_cnt_lt_n2(struct sock *sk, struct sk_buff *skb);
int llc_conn_ev_qlfy_retry_cnt_gte_n2(struct sock *sk, struct sk_buff *skb);
int llc_conn_ev_qlfy_s_flag_eq_1(struct sock *sk, struct sk_buff *skb);
int llc_conn_ev_qlfy_s_flag_eq_0(struct sock *sk, struct sk_buff *skb);
int llc_conn_ev_qlfy_cause_flag_eq_1(struct sock *sk, struct sk_buff *skb);
int llc_conn_ev_qlfy_cause_flag_eq_0(struct sock *sk, struct sk_buff *skb);
int llc_conn_ev_qlfy_set_status_conn(struct sock *sk, struct sk_buff *skb);
int llc_conn_ev_qlfy_set_status_disc(struct sock *sk, struct sk_buff *skb);
int llc_conn_ev_qlfy_set_status_failed(struct sock *sk, struct sk_buff *skb);
int llc_conn_ev_qlfy_set_status_remote_busy(struct sock *sk,
					    struct sk_buff *skb);
int llc_conn_ev_qlfy_set_status_refuse(struct sock *sk, struct sk_buff *skb);
int llc_conn_ev_qlfy_set_status_conflict(struct sock *sk, struct sk_buff *skb);
int llc_conn_ev_qlfy_set_status_rst_done(struct sock *sk, struct sk_buff *skb);

static __inline__ int llc_conn_space(struct sock *sk, struct sk_buff *skb)
{
	return atomic_read(&sk->sk_rmem_alloc) + skb->truesize <
	       (unsigned int)sk->sk_rcvbuf;
}
#endif /* LLC_C_EV_H */
