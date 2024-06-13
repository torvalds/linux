#ifndef LLC_C_AC_H
#define LLC_C_AC_H
/*
 * Copyright (c) 1997 by Procom Technology,Inc.
 * 		 2001 by Arnaldo Carvalho de Melo <acme@conectiva.com.br>
 *
 * This program can be redistributed or modified under the terms of the
 * GNU General Public License as published by the Free Software Foundation.
 * This program is distributed without any warranty or implied warranty
 * of merchantability or fitness for a particular purpose.
 *
 * See the GNU General Public License for more details.
 */
/* Connection component state transition actions */
/*
 * Connection state transition actions
 * (Fb = F bit; Pb = P bit; Xb = X bit)
 */

#include <linux/types.h>

struct sk_buff;
struct sock;
struct timer_list;

#define LLC_CONN_AC_CLR_REMOTE_BUSY			 1
#define LLC_CONN_AC_CONN_IND				 2
#define LLC_CONN_AC_CONN_CONFIRM			 3
#define LLC_CONN_AC_DATA_IND				 4
#define LLC_CONN_AC_DISC_IND				 5
#define LLC_CONN_AC_RESET_IND				 6
#define LLC_CONN_AC_RESET_CONFIRM			 7
#define LLC_CONN_AC_REPORT_STATUS			 8
#define LLC_CONN_AC_CLR_REMOTE_BUSY_IF_Fb_EQ_1		 9
#define LLC_CONN_AC_STOP_REJ_TMR_IF_DATA_FLAG_EQ_2	10
#define LLC_CONN_AC_SEND_DISC_CMD_Pb_SET_X		11
#define LLC_CONN_AC_SEND_DM_RSP_Fb_SET_Pb		12
#define LLC_CONN_AC_SEND_DM_RSP_Fb_SET_1		13
#define LLC_CONN_AC_SEND_DM_RSP_Fb_SET_F_FLAG		14
#define LLC_CONN_AC_SEND_FRMR_RSP_Fb_SET_X		15
#define LLC_CONN_AC_RESEND_FRMR_RSP_Fb_SET_0		16
#define LLC_CONN_AC_RESEND_FRMR_RSP_Fb_SET_Pb		17
#define LLC_CONN_AC_SEND_I_CMD_Pb_SET_1			18
#define LLC_CONN_AC_RESEND_I_CMD_Pb_SET_1		19
#define LLC_CONN_AC_RESEND_I_CMD_Pb_SET_1_OR_SEND_RR	20
#define LLC_CONN_AC_SEND_I_XXX_Xb_SET_0			21
#define LLC_CONN_AC_RESEND_I_XXX_Xb_SET_0		22
#define LLC_CONN_AC_RESEND_I_XXX_Xb_SET_0_OR_SEND_RR	23
#define LLC_CONN_AC_RESEND_I_RSP_Fb_SET_1		24
#define LLC_CONN_AC_SEND_REJ_CMD_Pb_SET_1		25
#define LLC_CONN_AC_SEND_REJ_RSP_Fb_SET_1		26
#define LLC_CONN_AC_SEND_REJ_XXX_Xb_SET_0		27
#define LLC_CONN_AC_SEND_RNR_CMD_Pb_SET_1		28
#define LLC_CONN_AC_SEND_RNR_RSP_Fb_SET_1		29
#define LLC_CONN_AC_SEND_RNR_XXX_Xb_SET_0		30
#define LLC_CONN_AC_SET_REMOTE_BUSY			31
#define LLC_CONN_AC_OPTIONAL_SEND_RNR_XXX_Xb_SET_0	32
#define LLC_CONN_AC_SEND_RR_CMD_Pb_SET_1		33
#define LLC_CONN_AC_SEND_ACK_CMD_Pb_SET_1		34
#define LLC_CONN_AC_SEND_RR_RSP_Fb_SET_1		35
#define LLC_CONN_AC_SEND_ACK_RSP_Fb_SET_1		36
#define LLC_CONN_AC_SEND_RR_XXX_Xb_SET_0		37
#define LLC_CONN_AC_SEND_ACK_XXX_Xb_SET_0		38
#define LLC_CONN_AC_SEND_SABME_CMD_Pb_SET_X		39
#define LLC_CONN_AC_SEND_UA_RSP_Fb_SET_Pb		40
#define LLC_CONN_AC_SEND_UA_RSP_Fb_SET_F_FLAG		41
#define LLC_CONN_AC_S_FLAG_SET_0			42
#define LLC_CONN_AC_S_FLAG_SET_1			43
#define LLC_CONN_AC_START_P_TMR				44
#define LLC_CONN_AC_START_ACK_TMR			45
#define LLC_CONN_AC_START_REJ_TMR			46
#define LLC_CONN_AC_START_ACK_TMR_IF_NOT_RUNNING	47
#define LLC_CONN_AC_STOP_ACK_TMR			48
#define LLC_CONN_AC_STOP_P_TMR				49
#define LLC_CONN_AC_STOP_REJ_TMR			50
#define LLC_CONN_AC_STOP_ALL_TMRS			51
#define LLC_CONN_AC_STOP_OTHER_TMRS			52
#define LLC_CONN_AC_UPDATE_Nr_RECEIVED			53
#define LLC_CONN_AC_UPDATE_P_FLAG			54
#define LLC_CONN_AC_DATA_FLAG_SET_2			55
#define LLC_CONN_AC_DATA_FLAG_SET_0			56
#define LLC_CONN_AC_DATA_FLAG_SET_1			57
#define LLC_CONN_AC_DATA_FLAG_SET_1_IF_DATA_FLAG_EQ_0	58
#define LLC_CONN_AC_P_FLAG_SET_0			59
#define LLC_CONN_AC_P_FLAG_SET_P			60
#define LLC_CONN_AC_REMOTE_BUSY_SET_0			61
#define LLC_CONN_AC_RETRY_CNT_SET_0			62
#define LLC_CONN_AC_RETRY_CNT_INC_BY_1			63
#define LLC_CONN_AC_Vr_SET_0				64
#define LLC_CONN_AC_Vr_INC_BY_1				65
#define LLC_CONN_AC_Vs_SET_0				66
#define LLC_CONN_AC_Vs_SET_Nr				67
#define LLC_CONN_AC_F_FLAG_SET_P			68
#define LLC_CONN_AC_STOP_SENDACK_TMR			70
#define LLC_CONN_AC_START_SENDACK_TMR_IF_NOT_RUNNING	71

typedef int (*llc_conn_action_t)(struct sock *sk, struct sk_buff *skb);

int llc_conn_ac_clear_remote_busy(struct sock *sk, struct sk_buff *skb);
int llc_conn_ac_conn_ind(struct sock *sk, struct sk_buff *skb);
int llc_conn_ac_conn_confirm(struct sock *sk, struct sk_buff *skb);
int llc_conn_ac_data_ind(struct sock *sk, struct sk_buff *skb);
int llc_conn_ac_disc_ind(struct sock *sk, struct sk_buff *skb);
int llc_conn_ac_rst_ind(struct sock *sk, struct sk_buff *skb);
int llc_conn_ac_rst_confirm(struct sock *sk, struct sk_buff *skb);
int llc_conn_ac_clear_remote_busy_if_f_eq_1(struct sock *sk,
					    struct sk_buff *skb);
int llc_conn_ac_stop_rej_tmr_if_data_flag_eq_2(struct sock *sk,
					       struct sk_buff *skb);
int llc_conn_ac_send_disc_cmd_p_set_x(struct sock *sk, struct sk_buff *skb);
int llc_conn_ac_send_dm_rsp_f_set_p(struct sock *sk, struct sk_buff *skb);
int llc_conn_ac_send_dm_rsp_f_set_1(struct sock *sk, struct sk_buff *skb);
int llc_conn_ac_send_frmr_rsp_f_set_x(struct sock *sk, struct sk_buff *skb);
int llc_conn_ac_resend_frmr_rsp_f_set_0(struct sock *sk, struct sk_buff *skb);
int llc_conn_ac_resend_frmr_rsp_f_set_p(struct sock *sk, struct sk_buff *skb);
int llc_conn_ac_send_i_cmd_p_set_1(struct sock *sk, struct sk_buff *skb);
int llc_conn_ac_send_i_xxx_x_set_0(struct sock *sk, struct sk_buff *skb);
int llc_conn_ac_resend_i_xxx_x_set_0(struct sock *sk, struct sk_buff *skb);
int llc_conn_ac_resend_i_xxx_x_set_0_or_send_rr(struct sock *sk,
						struct sk_buff *skb);
int llc_conn_ac_resend_i_rsp_f_set_1(struct sock *sk, struct sk_buff *skb);
int llc_conn_ac_send_rej_cmd_p_set_1(struct sock *sk, struct sk_buff *skb);
int llc_conn_ac_send_rej_rsp_f_set_1(struct sock *sk, struct sk_buff *skb);
int llc_conn_ac_send_rej_xxx_x_set_0(struct sock *sk, struct sk_buff *skb);
int llc_conn_ac_send_rnr_cmd_p_set_1(struct sock *sk, struct sk_buff *skb);
int llc_conn_ac_send_rnr_rsp_f_set_1(struct sock *sk, struct sk_buff *skb);
int llc_conn_ac_send_rnr_xxx_x_set_0(struct sock *sk, struct sk_buff *skb);
int llc_conn_ac_set_remote_busy(struct sock *sk, struct sk_buff *skb);
int llc_conn_ac_opt_send_rnr_xxx_x_set_0(struct sock *sk, struct sk_buff *skb);
int llc_conn_ac_send_rr_cmd_p_set_1(struct sock *sk, struct sk_buff *skb);
int llc_conn_ac_send_rr_rsp_f_set_1(struct sock *sk, struct sk_buff *skb);
int llc_conn_ac_send_ack_rsp_f_set_1(struct sock *sk, struct sk_buff *skb);
int llc_conn_ac_send_rr_xxx_x_set_0(struct sock *sk, struct sk_buff *skb);
int llc_conn_ac_send_ack_xxx_x_set_0(struct sock *sk, struct sk_buff *skb);
int llc_conn_ac_send_sabme_cmd_p_set_x(struct sock *sk, struct sk_buff *skb);
int llc_conn_ac_send_ua_rsp_f_set_p(struct sock *sk, struct sk_buff *skb);
int llc_conn_ac_set_s_flag_0(struct sock *sk, struct sk_buff *skb);
int llc_conn_ac_set_s_flag_1(struct sock *sk, struct sk_buff *skb);
int llc_conn_ac_start_p_timer(struct sock *sk, struct sk_buff *skb);
int llc_conn_ac_start_ack_timer(struct sock *sk, struct sk_buff *skb);
int llc_conn_ac_start_rej_timer(struct sock *sk, struct sk_buff *skb);
int llc_conn_ac_start_ack_tmr_if_not_running(struct sock *sk,
					     struct sk_buff *skb);
int llc_conn_ac_stop_ack_timer(struct sock *sk, struct sk_buff *skb);
int llc_conn_ac_stop_p_timer(struct sock *sk, struct sk_buff *skb);
int llc_conn_ac_stop_rej_timer(struct sock *sk, struct sk_buff *skb);
int llc_conn_ac_stop_all_timers(struct sock *sk, struct sk_buff *skb);
int llc_conn_ac_stop_other_timers(struct sock *sk, struct sk_buff *skb);
int llc_conn_ac_upd_nr_received(struct sock *sk, struct sk_buff *skb);
int llc_conn_ac_inc_tx_win_size(struct sock *sk, struct sk_buff *skb);
int llc_conn_ac_dec_tx_win_size(struct sock *sk, struct sk_buff *skb);
int llc_conn_ac_upd_p_flag(struct sock *sk, struct sk_buff *skb);
int llc_conn_ac_set_data_flag_2(struct sock *sk, struct sk_buff *skb);
int llc_conn_ac_set_data_flag_0(struct sock *sk, struct sk_buff *skb);
int llc_conn_ac_set_data_flag_1(struct sock *sk, struct sk_buff *skb);
int llc_conn_ac_set_data_flag_1_if_data_flag_eq_0(struct sock *sk,
						  struct sk_buff *skb);
int llc_conn_ac_set_p_flag_0(struct sock *sk, struct sk_buff *skb);
int llc_conn_ac_set_remote_busy_0(struct sock *sk, struct sk_buff *skb);
int llc_conn_ac_set_retry_cnt_0(struct sock *sk, struct sk_buff *skb);
int llc_conn_ac_set_cause_flag_0(struct sock *sk, struct sk_buff *skb);
int llc_conn_ac_set_cause_flag_1(struct sock *sk, struct sk_buff *skb);
int llc_conn_ac_inc_retry_cnt_by_1(struct sock *sk, struct sk_buff *skb);
int llc_conn_ac_set_vr_0(struct sock *sk, struct sk_buff *skb);
int llc_conn_ac_inc_vr_by_1(struct sock *sk, struct sk_buff *skb);
int llc_conn_ac_set_vs_0(struct sock *sk, struct sk_buff *skb);
int llc_conn_ac_set_vs_nr(struct sock *sk, struct sk_buff *skb);
int llc_conn_ac_rst_vs(struct sock *sk, struct sk_buff *skb);
int llc_conn_ac_upd_vs(struct sock *sk, struct sk_buff *skb);
int llc_conn_disc(struct sock *sk, struct sk_buff *skb);
int llc_conn_reset(struct sock *sk, struct sk_buff *skb);
int llc_conn_ac_disc_confirm(struct sock *sk, struct sk_buff *skb);
u8 llc_circular_between(u8 a, u8 b, u8 c);
int llc_conn_ac_send_ack_if_needed(struct sock *sk, struct sk_buff *skb);
int llc_conn_ac_adjust_npta_by_rr(struct sock *sk, struct sk_buff *skb);
int llc_conn_ac_adjust_npta_by_rnr(struct sock *sk, struct sk_buff *skb);
int llc_conn_ac_rst_sendack_flag(struct sock *sk, struct sk_buff *skb);
int llc_conn_ac_send_i_rsp_as_ack(struct sock *sk, struct sk_buff *skb);
int llc_conn_ac_send_i_as_ack(struct sock *sk, struct sk_buff *skb);

void llc_conn_busy_tmr_cb(struct timer_list *t);
void llc_conn_pf_cycle_tmr_cb(struct timer_list *t);
void llc_conn_ack_tmr_cb(struct timer_list *t);
void llc_conn_rej_tmr_cb(struct timer_list *t);

void llc_conn_set_p_flag(struct sock *sk, u8 value);
#endif /* LLC_C_AC_H */
