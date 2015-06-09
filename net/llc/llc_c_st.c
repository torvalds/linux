/*
 * llc_c_st.c - This module contains state transition of connection component.
 *
 * Description of event functions and actions there is in 802.2 LLC standard,
 * or in "llc_c_ac.c" and "llc_c_ev.c" modules.
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
#include <linux/types.h>
#include <net/llc_if.h>
#include <net/llc_sap.h>
#include <net/llc_c_ev.h>
#include <net/llc_c_ac.h>
#include <net/llc_c_st.h>

#define NONE NULL

/* COMMON CONNECTION STATE transitions
 * Common transitions for
 * LLC_CONN_STATE_NORMAL,
 * LLC_CONN_STATE_BUSY,
 * LLC_CONN_STATE_REJ,
 * LLC_CONN_STATE_AWAIT,
 * LLC_CONN_STATE_AWAIT_BUSY and
 * LLC_CONN_STATE_AWAIT_REJ states
 */
/* State transitions for LLC_CONN_EV_DISC_REQ event */
static const llc_conn_action_t llc_common_actions_1[] = {
	[0] = llc_conn_ac_send_disc_cmd_p_set_x,
	[1] = llc_conn_ac_start_ack_timer,
	[2] = llc_conn_ac_stop_other_timers,
	[3] = llc_conn_ac_set_retry_cnt_0,
	[4] = llc_conn_ac_set_cause_flag_1,
	[5] = NULL,
};

static struct llc_conn_state_trans llc_common_state_trans_1 = {
	.ev	       = llc_conn_ev_disc_req,
	.next_state    = LLC_CONN_STATE_D_CONN,
	.ev_qualifiers = NONE,
	.ev_actions    = llc_common_actions_1,
};

/* State transitions for LLC_CONN_EV_RESET_REQ event */
static const llc_conn_action_t llc_common_actions_2[] = {
	[0] = llc_conn_ac_send_sabme_cmd_p_set_x,
	[1] = llc_conn_ac_start_ack_timer,
	[2] = llc_conn_ac_stop_other_timers,
	[3] = llc_conn_ac_set_retry_cnt_0,
	[4] = llc_conn_ac_set_cause_flag_1,
	[5] = NULL,
};

static struct llc_conn_state_trans llc_common_state_trans_2 = {
	.ev	       = llc_conn_ev_rst_req,
	.next_state    = LLC_CONN_STATE_RESET,
	.ev_qualifiers = NONE,
	.ev_actions    = llc_common_actions_2,
};

/* State transitions for LLC_CONN_EV_RX_SABME_CMD_Pbit_SET_X event */
static const llc_conn_action_t llc_common_actions_3[] = {
	[0] = llc_conn_ac_stop_all_timers,
	[1] = llc_conn_ac_set_vs_0,
	[2] = llc_conn_ac_set_vr_0,
	[3] = llc_conn_ac_send_ua_rsp_f_set_p,
	[4] = llc_conn_ac_rst_ind,
	[5] = llc_conn_ac_set_p_flag_0,
	[6] = llc_conn_ac_set_remote_busy_0,
	[7] = llc_conn_reset,
	[8] = NULL,
};

static struct llc_conn_state_trans llc_common_state_trans_3 = {
	.ev	       = llc_conn_ev_rx_sabme_cmd_pbit_set_x,
	.next_state    = LLC_CONN_STATE_NORMAL,
	.ev_qualifiers = NONE,
	.ev_actions    = llc_common_actions_3,
};

/* State transitions for LLC_CONN_EV_RX_DISC_CMD_Pbit_SET_X event */
static const llc_conn_action_t llc_common_actions_4[] = {
	[0] = llc_conn_ac_stop_all_timers,
	[1] = llc_conn_ac_send_ua_rsp_f_set_p,
	[2] = llc_conn_ac_disc_ind,
	[3] = llc_conn_disc,
	[4] = NULL,
};

static struct llc_conn_state_trans llc_common_state_trans_4 = {
	.ev	       = llc_conn_ev_rx_disc_cmd_pbit_set_x,
	.next_state    = LLC_CONN_STATE_ADM,
	.ev_qualifiers = NONE,
	.ev_actions    = llc_common_actions_4,
};

/* State transitions for LLC_CONN_EV_RX_FRMR_RSP_Fbit_SET_X event */
static const llc_conn_action_t llc_common_actions_5[] = {
	[0] = llc_conn_ac_send_sabme_cmd_p_set_x,
	[1] = llc_conn_ac_start_ack_timer,
	[2] = llc_conn_ac_stop_other_timers,
	[3] = llc_conn_ac_set_retry_cnt_0,
	[4] = llc_conn_ac_rst_ind,
	[5] = llc_conn_ac_set_cause_flag_0,
	[6] = llc_conn_reset,
	[7] = NULL,
};

static struct llc_conn_state_trans llc_common_state_trans_5 = {
	.ev	       = llc_conn_ev_rx_frmr_rsp_fbit_set_x,
	.next_state    = LLC_CONN_STATE_RESET,
	.ev_qualifiers = NONE,
	.ev_actions    = llc_common_actions_5,
};

/* State transitions for LLC_CONN_EV_RX_DM_RSP_Fbit_SET_X event */
static const llc_conn_action_t llc_common_actions_6[] = {
	[0] = llc_conn_ac_disc_ind,
	[1] = llc_conn_ac_stop_all_timers,
	[2] = llc_conn_disc,
	[3] = NULL,
};

static struct llc_conn_state_trans llc_common_state_trans_6 = {
	.ev	       = llc_conn_ev_rx_dm_rsp_fbit_set_x,
	.next_state    = LLC_CONN_STATE_ADM,
	.ev_qualifiers = NONE,
	.ev_actions    = llc_common_actions_6,
};

/* State transitions for LLC_CONN_EV_RX_ZZZ_CMD_Pbit_SET_X_INVAL_Nr event */
static const llc_conn_action_t llc_common_actions_7a[] = {
	[0] = llc_conn_ac_send_frmr_rsp_f_set_x,
	[1] = llc_conn_ac_start_ack_timer,
	[2] = llc_conn_ac_stop_other_timers,
	[3] = llc_conn_ac_set_retry_cnt_0,
	[4] = NULL,
};

static struct llc_conn_state_trans llc_common_state_trans_7a = {
	.ev	       = llc_conn_ev_rx_zzz_cmd_pbit_set_x_inval_nr,
	.next_state    = LLC_CONN_STATE_ERROR,
	.ev_qualifiers = NONE,
	.ev_actions    = llc_common_actions_7a,
};

/* State transitions for LLC_CONN_EV_RX_I_CMD_Pbit_SET_X_INVAL_Ns event */
static const llc_conn_action_t llc_common_actions_7b[] = {
	[0] = llc_conn_ac_send_frmr_rsp_f_set_x,
	[1] = llc_conn_ac_start_ack_timer,
	[2] = llc_conn_ac_stop_other_timers,
	[3] = llc_conn_ac_set_retry_cnt_0,
	[4] = NULL,
};

static struct llc_conn_state_trans llc_common_state_trans_7b = {
	.ev	       = llc_conn_ev_rx_i_cmd_pbit_set_x_inval_ns,
	.next_state    = LLC_CONN_STATE_ERROR,
	.ev_qualifiers = NONE,
	.ev_actions    = llc_common_actions_7b,
};

/* State transitions for LLC_CONN_EV_RX_ZZZ_RSP_Fbit_SET_X_INVAL_Nr event */
static const llc_conn_action_t llc_common_actions_8a[] = {
	[0] = llc_conn_ac_send_frmr_rsp_f_set_x,
	[1] = llc_conn_ac_start_ack_timer,
	[2] = llc_conn_ac_stop_other_timers,
	[3] = llc_conn_ac_set_retry_cnt_0,
	[4] = NULL,
};

static struct llc_conn_state_trans llc_common_state_trans_8a = {
	.ev	       = llc_conn_ev_rx_zzz_rsp_fbit_set_x_inval_nr,
	.next_state    = LLC_CONN_STATE_ERROR,
	.ev_qualifiers = NONE,
	.ev_actions    = llc_common_actions_8a,
};

/* State transitions for LLC_CONN_EV_RX_I_RSP_Fbit_SET_X_INVAL_Ns event */
static const llc_conn_action_t llc_common_actions_8b[] = {
	[0] = llc_conn_ac_send_frmr_rsp_f_set_x,
	[1] = llc_conn_ac_start_ack_timer,
	[2] = llc_conn_ac_stop_other_timers,
	[3] = llc_conn_ac_set_retry_cnt_0,
	[4] = NULL,
};

static struct llc_conn_state_trans llc_common_state_trans_8b = {
	.ev	       = llc_conn_ev_rx_i_rsp_fbit_set_x_inval_ns,
	.next_state    = LLC_CONN_STATE_ERROR,
	.ev_qualifiers = NONE,
	.ev_actions    = llc_common_actions_8b,
};

/* State transitions for LLC_CONN_EV_RX_BAD_PDU event */
static const llc_conn_action_t llc_common_actions_8c[] = {
	[0] = llc_conn_ac_send_frmr_rsp_f_set_x,
	[1] = llc_conn_ac_start_ack_timer,
	[2] = llc_conn_ac_stop_other_timers,
	[3] = llc_conn_ac_set_retry_cnt_0,
	[4] = NULL,
};

static struct llc_conn_state_trans llc_common_state_trans_8c = {
	.ev	       = llc_conn_ev_rx_bad_pdu,
	.next_state    = LLC_CONN_STATE_ERROR,
	.ev_qualifiers = NONE,
	.ev_actions    = llc_common_actions_8c,
};

/* State transitions for LLC_CONN_EV_RX_UA_RSP_Fbit_SET_X event */
static const llc_conn_action_t llc_common_actions_9[] = {
	[0] = llc_conn_ac_send_frmr_rsp_f_set_x,
	[1] = llc_conn_ac_start_ack_timer,
	[2] = llc_conn_ac_stop_other_timers,
	[3] = llc_conn_ac_set_retry_cnt_0,
	[4] = NULL,
};

static struct llc_conn_state_trans llc_common_state_trans_9 = {
	.ev	       = llc_conn_ev_rx_ua_rsp_fbit_set_x,
	.next_state    = LLC_CONN_STATE_ERROR,
	.ev_qualifiers = NONE,
	.ev_actions    = llc_common_actions_9,
};

/* State transitions for LLC_CONN_EV_RX_XXX_RSP_Fbit_SET_1 event */
#if 0
static const llc_conn_ev_qfyr_t llc_common_ev_qfyrs_10[] = {
	[0] = llc_conn_ev_qlfy_p_flag_eq_0,
	[1] = NULL,
};

static const llc_conn_action_t llc_common_actions_10[] = {
	[0] = llc_conn_ac_send_frmr_rsp_f_set_x,
	[1] = llc_conn_ac_start_ack_timer,
	[2] = llc_conn_ac_stop_other_timers,
	[3] = llc_conn_ac_set_retry_cnt_0,
	[4] = NULL,
};

static struct llc_conn_state_trans llc_common_state_trans_10 = {
	.ev	       = llc_conn_ev_rx_xxx_rsp_fbit_set_1,
	.next_state    = LLC_CONN_STATE_ERROR,
	.ev_qualifiers = llc_common_ev_qfyrs_10,
	.ev_actions    = llc_common_actions_10,
};
#endif

/* State transitions for LLC_CONN_EV_P_TMR_EXP event */
static const llc_conn_ev_qfyr_t llc_common_ev_qfyrs_11a[] = {
	[0] = llc_conn_ev_qlfy_retry_cnt_gte_n2,
	[1] = NULL,
};

static const llc_conn_action_t llc_common_actions_11a[] = {
	[0] = llc_conn_ac_send_sabme_cmd_p_set_x,
	[1] = llc_conn_ac_start_ack_timer,
	[2] = llc_conn_ac_stop_other_timers,
	[3] = llc_conn_ac_set_retry_cnt_0,
	[4] = llc_conn_ac_set_cause_flag_0,
	[5] = NULL,
};

static struct llc_conn_state_trans llc_common_state_trans_11a = {
	.ev	       = llc_conn_ev_p_tmr_exp,
	.next_state    = LLC_CONN_STATE_RESET,
	.ev_qualifiers = llc_common_ev_qfyrs_11a,
	.ev_actions    = llc_common_actions_11a,
};

/* State transitions for LLC_CONN_EV_ACK_TMR_EXP event */
static const llc_conn_ev_qfyr_t llc_common_ev_qfyrs_11b[] = {
	[0] = llc_conn_ev_qlfy_retry_cnt_gte_n2,
	[1] = NULL,
};

static const llc_conn_action_t llc_common_actions_11b[] = {
	[0] = llc_conn_ac_send_sabme_cmd_p_set_x,
	[1] = llc_conn_ac_start_ack_timer,
	[2] = llc_conn_ac_stop_other_timers,
	[3] = llc_conn_ac_set_retry_cnt_0,
	[4] = llc_conn_ac_set_cause_flag_0,
	[5] = NULL,
};

static struct llc_conn_state_trans llc_common_state_trans_11b = {
	.ev	       = llc_conn_ev_ack_tmr_exp,
	.next_state    = LLC_CONN_STATE_RESET,
	.ev_qualifiers = llc_common_ev_qfyrs_11b,
	.ev_actions    = llc_common_actions_11b,
};

/* State transitions for LLC_CONN_EV_REJ_TMR_EXP event */
static const llc_conn_ev_qfyr_t llc_common_ev_qfyrs_11c[] = {
	[0] = llc_conn_ev_qlfy_retry_cnt_gte_n2,
	[1] = NULL,
};

static const llc_conn_action_t llc_common_actions_11c[] = {
	[0] = llc_conn_ac_send_sabme_cmd_p_set_x,
	[1] = llc_conn_ac_start_ack_timer,
	[2] = llc_conn_ac_stop_other_timers,
	[3] = llc_conn_ac_set_retry_cnt_0,
	[4] = llc_conn_ac_set_cause_flag_0,
	[5] = NULL,
};

static struct llc_conn_state_trans llc_common_state_trans_11c = {
	.ev	       = llc_conn_ev_rej_tmr_exp,
	.next_state    = LLC_CONN_STATE_RESET,
	.ev_qualifiers = llc_common_ev_qfyrs_11c,
	.ev_actions    = llc_common_actions_11c,
};

/* State transitions for LLC_CONN_EV_BUSY_TMR_EXP event */
static const llc_conn_ev_qfyr_t llc_common_ev_qfyrs_11d[] = {
	[0] = llc_conn_ev_qlfy_retry_cnt_gte_n2,
	[1] = NULL,
};

static const llc_conn_action_t llc_common_actions_11d[] = {
	[0] = llc_conn_ac_send_sabme_cmd_p_set_x,
	[1] = llc_conn_ac_start_ack_timer,
	[2] = llc_conn_ac_stop_other_timers,
	[3] = llc_conn_ac_set_retry_cnt_0,
	[4] = llc_conn_ac_set_cause_flag_0,
	[5] = NULL,
};

static struct llc_conn_state_trans llc_common_state_trans_11d = {
	.ev	       = llc_conn_ev_busy_tmr_exp,
	.next_state    = LLC_CONN_STATE_RESET,
	.ev_qualifiers = llc_common_ev_qfyrs_11d,
	.ev_actions    = llc_common_actions_11d,
};

/*
 * Common dummy state transition; must be last entry for all state
 * transition groups - it'll be on .bss, so will be zeroed.
 */
static struct llc_conn_state_trans llc_common_state_trans_end;

/* LLC_CONN_STATE_ADM transitions */
/* State transitions for LLC_CONN_EV_CONN_REQ event */
static const llc_conn_action_t llc_adm_actions_1[] = {
	[0] = llc_conn_ac_send_sabme_cmd_p_set_x,
	[1] = llc_conn_ac_start_ack_timer,
	[2] = llc_conn_ac_set_retry_cnt_0,
	[3] = llc_conn_ac_set_s_flag_0,
	[4] = NULL,
};

static struct llc_conn_state_trans llc_adm_state_trans_1 = {
	.ev	       = llc_conn_ev_conn_req,
	.next_state    = LLC_CONN_STATE_SETUP,
	.ev_qualifiers = NONE,
	.ev_actions    = llc_adm_actions_1,
};

/* State transitions for LLC_CONN_EV_RX_SABME_CMD_Pbit_SET_X event */
static const llc_conn_action_t llc_adm_actions_2[] = {
	[0] = llc_conn_ac_send_ua_rsp_f_set_p,
	[1] = llc_conn_ac_set_vs_0,
	[2] = llc_conn_ac_set_vr_0,
	[3] = llc_conn_ac_set_retry_cnt_0,
	[4] = llc_conn_ac_set_p_flag_0,
	[5] = llc_conn_ac_set_remote_busy_0,
	[6] = llc_conn_ac_conn_ind,
	[7] = NULL,
};

static struct llc_conn_state_trans llc_adm_state_trans_2 = {
	.ev	       = llc_conn_ev_rx_sabme_cmd_pbit_set_x,
	.next_state    = LLC_CONN_STATE_NORMAL,
	.ev_qualifiers = NONE,
	.ev_actions    = llc_adm_actions_2,
};

/* State transitions for LLC_CONN_EV_RX_DISC_CMD_Pbit_SET_X event */
static const llc_conn_action_t llc_adm_actions_3[] = {
	[0] = llc_conn_ac_send_dm_rsp_f_set_p,
	[1] = llc_conn_disc,
	[2] = NULL,
};

static struct llc_conn_state_trans llc_adm_state_trans_3 = {
	.ev	       = llc_conn_ev_rx_disc_cmd_pbit_set_x,
	.next_state    = LLC_CONN_STATE_ADM,
	.ev_qualifiers = NONE,
	.ev_actions    = llc_adm_actions_3,
};

/* State transitions for LLC_CONN_EV_RX_XXX_CMD_Pbit_SET_1 event */
static const llc_conn_action_t llc_adm_actions_4[] = {
	[0] = llc_conn_ac_send_dm_rsp_f_set_1,
	[1] = llc_conn_disc,
	[2] = NULL,
};

static struct llc_conn_state_trans llc_adm_state_trans_4 = {
	.ev	       = llc_conn_ev_rx_xxx_cmd_pbit_set_1,
	.next_state    = LLC_CONN_STATE_ADM,
	.ev_qualifiers = NONE,
	.ev_actions    = llc_adm_actions_4,
};

/* State transitions for LLC_CONN_EV_RX_XXX_YYY event */
static const llc_conn_action_t llc_adm_actions_5[] = {
	[0] = llc_conn_disc,
	[1] = NULL,
};

static struct llc_conn_state_trans llc_adm_state_trans_5 = {
	.ev	       = llc_conn_ev_rx_any_frame,
	.next_state    = LLC_CONN_OUT_OF_SVC,
	.ev_qualifiers = NONE,
	.ev_actions    = llc_adm_actions_5,
};

/*
 * Array of pointers;
 * one to each transition
 */
static struct llc_conn_state_trans *llc_adm_state_transitions[] = {
	[0] = &llc_adm_state_trans_1,		/* Request */
	[1] = &llc_common_state_trans_end,
	[2] = &llc_common_state_trans_end,	/* local_busy */
	[3] = &llc_common_state_trans_end,	/* init_pf_cycle */
	[4] = &llc_common_state_trans_end,	/* timer */
	[5] = &llc_adm_state_trans_2,		/* Receive frame */
	[6] = &llc_adm_state_trans_3,
	[7] = &llc_adm_state_trans_4,
	[8] = &llc_adm_state_trans_5,
	[9] = &llc_common_state_trans_end,
};

/* LLC_CONN_STATE_SETUP transitions */
/* State transitions for LLC_CONN_EV_RX_SABME_CMD_Pbit_SET_X event */
static const llc_conn_action_t llc_setup_actions_1[] = {
	[0] = llc_conn_ac_send_ua_rsp_f_set_p,
	[1] = llc_conn_ac_set_vs_0,
	[2] = llc_conn_ac_set_vr_0,
	[3] = llc_conn_ac_set_s_flag_1,
	[4] = NULL,
};

static struct llc_conn_state_trans llc_setup_state_trans_1 = {
	.ev	       = llc_conn_ev_rx_sabme_cmd_pbit_set_x,
	.next_state    = LLC_CONN_STATE_SETUP,
	.ev_qualifiers = NONE,
	.ev_actions    = llc_setup_actions_1,
};

/* State transitions for LLC_CONN_EV_RX_UA_RSP_Fbit_SET_X event */
static const llc_conn_ev_qfyr_t llc_setup_ev_qfyrs_2[] = {
	[0] = llc_conn_ev_qlfy_p_flag_eq_f,
	[1] = llc_conn_ev_qlfy_set_status_conn,
	[2] = NULL,
};

static const llc_conn_action_t llc_setup_actions_2[] = {
	[0] = llc_conn_ac_stop_ack_timer,
	[1] = llc_conn_ac_set_vs_0,
	[2] = llc_conn_ac_set_vr_0,
	[3] = llc_conn_ac_upd_p_flag,
	[4] = llc_conn_ac_set_remote_busy_0,
	[5] = llc_conn_ac_conn_confirm,
	[6] = NULL,
};

static struct llc_conn_state_trans llc_setup_state_trans_2 = {
	.ev	       = llc_conn_ev_rx_ua_rsp_fbit_set_x,
	.next_state    = LLC_CONN_STATE_NORMAL,
	.ev_qualifiers = llc_setup_ev_qfyrs_2,
	.ev_actions    = llc_setup_actions_2,
};

/* State transitions for LLC_CONN_EV_ACK_TMR_EXP event */
static const llc_conn_ev_qfyr_t llc_setup_ev_qfyrs_3[] = {
	[0] = llc_conn_ev_qlfy_s_flag_eq_1,
	[1] = llc_conn_ev_qlfy_set_status_conn,
	[2] = NULL,
};

static const llc_conn_action_t llc_setup_actions_3[] = {
	[0] = llc_conn_ac_set_p_flag_0,
	[1] = llc_conn_ac_set_remote_busy_0,
	[2] = llc_conn_ac_conn_confirm,
	[3] = NULL,
};

static struct llc_conn_state_trans llc_setup_state_trans_3 = {
	.ev	       = llc_conn_ev_ack_tmr_exp,
	.next_state    = LLC_CONN_STATE_NORMAL,
	.ev_qualifiers = llc_setup_ev_qfyrs_3,
	.ev_actions    = llc_setup_actions_3,
};

/* State transitions for LLC_CONN_EV_RX_DISC_CMD_Pbit_SET_X event */
static const llc_conn_ev_qfyr_t llc_setup_ev_qfyrs_4[] = {
	[0] = llc_conn_ev_qlfy_set_status_disc,
	[1] = NULL,
};

static const llc_conn_action_t llc_setup_actions_4[] = {
	[0] = llc_conn_ac_send_dm_rsp_f_set_p,
	[1] = llc_conn_ac_stop_ack_timer,
	[2] = llc_conn_ac_conn_confirm,
	[3] = llc_conn_disc,
	[4] = NULL,
};

static struct llc_conn_state_trans llc_setup_state_trans_4 = {
	.ev	       = llc_conn_ev_rx_disc_cmd_pbit_set_x,
	.next_state    = LLC_CONN_STATE_ADM,
	.ev_qualifiers = llc_setup_ev_qfyrs_4,
	.ev_actions    = llc_setup_actions_4,
};

/* State transitions for LLC_CONN_EV_RX_DM_RSP_Fbit_SET_X event */
static const llc_conn_ev_qfyr_t llc_setup_ev_qfyrs_5[] = {
	[0] = llc_conn_ev_qlfy_set_status_disc,
	[1] = NULL,
};

static const llc_conn_action_t llc_setup_actions_5[] = {
	[0] = llc_conn_ac_stop_ack_timer,
	[1] = llc_conn_ac_conn_confirm,
	[2] = llc_conn_disc,
	[3] = NULL,
};

static struct llc_conn_state_trans llc_setup_state_trans_5 = {
	.ev	       = llc_conn_ev_rx_dm_rsp_fbit_set_x,
	.next_state    = LLC_CONN_STATE_ADM,
	.ev_qualifiers = llc_setup_ev_qfyrs_5,
	.ev_actions    = llc_setup_actions_5,
};

/* State transitions for LLC_CONN_EV_ACK_TMR_EXP event */
static const llc_conn_ev_qfyr_t llc_setup_ev_qfyrs_7[] = {
	[0] = llc_conn_ev_qlfy_retry_cnt_lt_n2,
	[1] = llc_conn_ev_qlfy_s_flag_eq_0,
	[2] = NULL,
};

static const llc_conn_action_t llc_setup_actions_7[] = {
	[0] = llc_conn_ac_send_sabme_cmd_p_set_x,
	[1] = llc_conn_ac_start_ack_timer,
	[2] = llc_conn_ac_inc_retry_cnt_by_1,
	[3] = NULL,
};

static struct llc_conn_state_trans llc_setup_state_trans_7 = {
	.ev	       = llc_conn_ev_ack_tmr_exp,
	.next_state    = LLC_CONN_STATE_SETUP,
	.ev_qualifiers = llc_setup_ev_qfyrs_7,
	.ev_actions    = llc_setup_actions_7,
};

/* State transitions for LLC_CONN_EV_ACK_TMR_EXP event */
static const llc_conn_ev_qfyr_t llc_setup_ev_qfyrs_8[] = {
	[0] = llc_conn_ev_qlfy_retry_cnt_gte_n2,
	[1] = llc_conn_ev_qlfy_s_flag_eq_0,
	[2] = llc_conn_ev_qlfy_set_status_failed,
	[3] = NULL,
};

static const llc_conn_action_t llc_setup_actions_8[] = {
	[0] = llc_conn_ac_conn_confirm,
	[1] = llc_conn_disc,
	[2] = NULL,
};

static struct llc_conn_state_trans llc_setup_state_trans_8 = {
	.ev	       = llc_conn_ev_ack_tmr_exp,
	.next_state    = LLC_CONN_STATE_ADM,
	.ev_qualifiers = llc_setup_ev_qfyrs_8,
	.ev_actions    = llc_setup_actions_8,
};

/*
 * Array of pointers;
 * one to each transition
 */
static struct llc_conn_state_trans *llc_setup_state_transitions[] = {
	 [0] = &llc_common_state_trans_end,	/* Request */
	 [1] = &llc_common_state_trans_end,	/* local busy */
	 [2] = &llc_common_state_trans_end,	/* init_pf_cycle */
	 [3] = &llc_setup_state_trans_3,	/* Timer */
	 [4] = &llc_setup_state_trans_7,
	 [5] = &llc_setup_state_trans_8,
	 [6] = &llc_common_state_trans_end,
	 [7] = &llc_setup_state_trans_1,	/* Receive frame */
	 [8] = &llc_setup_state_trans_2,
	 [9] = &llc_setup_state_trans_4,
	[10] = &llc_setup_state_trans_5,
	[11] = &llc_common_state_trans_end,
};

/* LLC_CONN_STATE_NORMAL transitions */
/* State transitions for LLC_CONN_EV_DATA_REQ event */
static const llc_conn_ev_qfyr_t llc_normal_ev_qfyrs_1[] = {
	[0] = llc_conn_ev_qlfy_remote_busy_eq_0,
	[1] = llc_conn_ev_qlfy_p_flag_eq_0,
	[2] = llc_conn_ev_qlfy_last_frame_eq_0,
	[3] = NULL,
};

static const llc_conn_action_t llc_normal_actions_1[] = {
	[0] = llc_conn_ac_send_i_as_ack,
	[1] = llc_conn_ac_start_ack_tmr_if_not_running,
	[2] = NULL,
};

static struct llc_conn_state_trans llc_normal_state_trans_1 = {
	.ev	       = llc_conn_ev_data_req,
	.next_state    = LLC_CONN_STATE_NORMAL,
	.ev_qualifiers = llc_normal_ev_qfyrs_1,
	.ev_actions    = llc_normal_actions_1,
};

/* State transitions for LLC_CONN_EV_DATA_REQ event */
static const llc_conn_ev_qfyr_t llc_normal_ev_qfyrs_2[] = {
	[0] = llc_conn_ev_qlfy_remote_busy_eq_0,
	[1] = llc_conn_ev_qlfy_p_flag_eq_0,
	[2] = llc_conn_ev_qlfy_last_frame_eq_1,
	[3] = NULL,
};

static const llc_conn_action_t llc_normal_actions_2[] = {
	[0] = llc_conn_ac_send_i_cmd_p_set_1,
	[1] = llc_conn_ac_start_p_timer,
	[2] = NULL,
};

static struct llc_conn_state_trans llc_normal_state_trans_2 = {
	.ev	       = llc_conn_ev_data_req,
	.next_state    = LLC_CONN_STATE_NORMAL,
	.ev_qualifiers = llc_normal_ev_qfyrs_2,
	.ev_actions    = llc_normal_actions_2,
};

/* State transitions for LLC_CONN_EV_DATA_REQ event */
static const llc_conn_ev_qfyr_t llc_normal_ev_qfyrs_2_1[] = {
	[0] = llc_conn_ev_qlfy_remote_busy_eq_1,
	[1] = llc_conn_ev_qlfy_set_status_remote_busy,
	[2] = NULL,
};

/* just one member, NULL, .bss zeroes it */
static const llc_conn_action_t llc_normal_actions_2_1[1];

static struct llc_conn_state_trans llc_normal_state_trans_2_1 = {
	.ev	       = llc_conn_ev_data_req,
	.next_state    = LLC_CONN_STATE_NORMAL,
	.ev_qualifiers = llc_normal_ev_qfyrs_2_1,
	.ev_actions    = llc_normal_actions_2_1,
};

/* State transitions for LLC_CONN_EV_LOCAL_BUSY_DETECTED event */
static const llc_conn_ev_qfyr_t llc_normal_ev_qfyrs_3[] = {
	[0] = llc_conn_ev_qlfy_p_flag_eq_0,
	[1] = NULL,
};

static const llc_conn_action_t llc_normal_actions_3[] = {
	[0] = llc_conn_ac_rst_sendack_flag,
	[1] = llc_conn_ac_send_rnr_xxx_x_set_0,
	[2] = llc_conn_ac_set_data_flag_0,
	[3] = NULL,
};

static struct llc_conn_state_trans llc_normal_state_trans_3 = {
	.ev	       = llc_conn_ev_local_busy_detected,
	.next_state    = LLC_CONN_STATE_BUSY,
	.ev_qualifiers = llc_normal_ev_qfyrs_3,
	.ev_actions    = llc_normal_actions_3,
};

/* State transitions for LLC_CONN_EV_LOCAL_BUSY_DETECTED event */
static const llc_conn_ev_qfyr_t llc_normal_ev_qfyrs_4[] = {
	[0] = llc_conn_ev_qlfy_p_flag_eq_1,
	[1] = NULL,
};

static const llc_conn_action_t llc_normal_actions_4[] = {
	[0] = llc_conn_ac_rst_sendack_flag,
	[1] = llc_conn_ac_send_rnr_xxx_x_set_0,
	[2] = llc_conn_ac_set_data_flag_0,
	[3] = NULL,
};

static struct llc_conn_state_trans llc_normal_state_trans_4 = {
	.ev	       = llc_conn_ev_local_busy_detected,
	.next_state    = LLC_CONN_STATE_BUSY,
	.ev_qualifiers = llc_normal_ev_qfyrs_4,
	.ev_actions    = llc_normal_actions_4,
};

/* State transitions for LLC_CONN_EV_RX_I_CMD_Pbit_SET_0_UNEXPD_Ns event */
static const llc_conn_ev_qfyr_t llc_normal_ev_qfyrs_5a[] = {
	[0] = llc_conn_ev_qlfy_p_flag_eq_0,
	[1] = NULL,
};

static const llc_conn_action_t llc_normal_actions_5a[] = {
	[0] = llc_conn_ac_rst_sendack_flag,
	[1] = llc_conn_ac_send_rej_xxx_x_set_0,
	[2] = llc_conn_ac_upd_nr_received,
	[3] = llc_conn_ac_upd_p_flag,
	[4] = llc_conn_ac_start_rej_timer,
	[5] = llc_conn_ac_clear_remote_busy_if_f_eq_1,
	[6] = NULL,
};

static struct llc_conn_state_trans llc_normal_state_trans_5a = {
	.ev	       = llc_conn_ev_rx_i_cmd_pbit_set_0_unexpd_ns,
	.next_state    = LLC_CONN_STATE_REJ,
	.ev_qualifiers = llc_normal_ev_qfyrs_5a,
	.ev_actions    = llc_normal_actions_5a,
};

/* State transitions for LLC_CONN_EV_RX_I_RSP_Fbit_SET_0_UNEXPD_Ns event */
static const llc_conn_ev_qfyr_t llc_normal_ev_qfyrs_5b[] = {
	[0] = llc_conn_ev_qlfy_p_flag_eq_0,
	[1] = NULL,
};

static const llc_conn_action_t llc_normal_actions_5b[] = {
	[0] = llc_conn_ac_rst_sendack_flag,
	[1] = llc_conn_ac_send_rej_xxx_x_set_0,
	[2] = llc_conn_ac_upd_nr_received,
	[3] = llc_conn_ac_upd_p_flag,
	[4] = llc_conn_ac_start_rej_timer,
	[5] = llc_conn_ac_clear_remote_busy_if_f_eq_1,
	[6] = NULL,
};

static struct llc_conn_state_trans llc_normal_state_trans_5b = {
	.ev	       = llc_conn_ev_rx_i_rsp_fbit_set_0_unexpd_ns,
	.next_state    = LLC_CONN_STATE_REJ,
	.ev_qualifiers = llc_normal_ev_qfyrs_5b,
	.ev_actions    = llc_normal_actions_5b,
};

/* State transitions for LLC_CONN_EV_RX_I_RSP_Fbit_SET_1_UNEXPD_Ns event */
static const llc_conn_ev_qfyr_t llc_normal_ev_qfyrs_5c[] = {
	[0] = llc_conn_ev_qlfy_p_flag_eq_1,
	[1] = NULL,
};

static const llc_conn_action_t llc_normal_actions_5c[] = {
	[0] = llc_conn_ac_rst_sendack_flag,
	[1] = llc_conn_ac_send_rej_xxx_x_set_0,
	[2] = llc_conn_ac_upd_nr_received,
	[3] = llc_conn_ac_upd_p_flag,
	[4] = llc_conn_ac_start_rej_timer,
	[5] = llc_conn_ac_clear_remote_busy_if_f_eq_1,
	[6] = NULL,
};

static struct llc_conn_state_trans llc_normal_state_trans_5c = {
	.ev	       = llc_conn_ev_rx_i_rsp_fbit_set_1_unexpd_ns,
	.next_state    = LLC_CONN_STATE_REJ,
	.ev_qualifiers = llc_normal_ev_qfyrs_5c,
	.ev_actions    = llc_normal_actions_5c,
};

/* State transitions for LLC_CONN_EV_RX_I_CMD_Pbit_SET_0_UNEXPD_Ns event */
static const llc_conn_ev_qfyr_t llc_normal_ev_qfyrs_6a[] = {
	[0] = llc_conn_ev_qlfy_p_flag_eq_1,
	[1] = NULL,
};

static const llc_conn_action_t llc_normal_actions_6a[] = {
	[0] = llc_conn_ac_rst_sendack_flag,
	[1] = llc_conn_ac_send_rej_xxx_x_set_0,
	[2] = llc_conn_ac_upd_nr_received,
	[3] = llc_conn_ac_start_rej_timer,
	[4] = NULL,
};

static struct llc_conn_state_trans llc_normal_state_trans_6a = {
	.ev	       = llc_conn_ev_rx_i_cmd_pbit_set_0_unexpd_ns,
	.next_state    = LLC_CONN_STATE_REJ,
	.ev_qualifiers = llc_normal_ev_qfyrs_6a,
	.ev_actions    = llc_normal_actions_6a,
};

/* State transitions for LLC_CONN_EV_RX_I_RSP_Fbit_SET_0_UNEXPD_Ns event */
static const llc_conn_ev_qfyr_t llc_normal_ev_qfyrs_6b[] = {
	[0] = llc_conn_ev_qlfy_p_flag_eq_1,
	[1] = NULL,
};

static const llc_conn_action_t llc_normal_actions_6b[] = {
	[0] = llc_conn_ac_rst_sendack_flag,
	[1] = llc_conn_ac_send_rej_xxx_x_set_0,
	[2] = llc_conn_ac_upd_nr_received,
	[3] = llc_conn_ac_start_rej_timer,
	[4] = NULL,
};

static struct llc_conn_state_trans llc_normal_state_trans_6b = {
	.ev	       = llc_conn_ev_rx_i_rsp_fbit_set_0_unexpd_ns,
	.next_state    = LLC_CONN_STATE_REJ,
	.ev_qualifiers = llc_normal_ev_qfyrs_6b,
	.ev_actions    = llc_normal_actions_6b,
};

/* State transitions for LLC_CONN_EV_RX_I_CMD_Pbit_SET_1_UNEXPD_Ns event */
static const llc_conn_action_t llc_normal_actions_7[] = {
	[0] = llc_conn_ac_rst_sendack_flag,
	[1] = llc_conn_ac_send_rej_rsp_f_set_1,
	[2] = llc_conn_ac_upd_nr_received,
	[3] = llc_conn_ac_start_rej_timer,
	[4] = NULL,
};

static struct llc_conn_state_trans llc_normal_state_trans_7 = {
	.ev	       = llc_conn_ev_rx_i_cmd_pbit_set_1_unexpd_ns,
	.next_state    = LLC_CONN_STATE_REJ,
	.ev_qualifiers = NONE,
	.ev_actions    = llc_normal_actions_7,
};

/* State transitions for LLC_CONN_EV_RX_I_RSP_Fbit_SET_X event */
static const llc_conn_ev_qfyr_t llc_normal_ev_qfyrs_8a[] = {
	[0] = llc_conn_ev_qlfy_p_flag_eq_f,
	[1] = NULL,
};

static const llc_conn_action_t llc_normal_actions_8[] = {
	[0] = llc_conn_ac_inc_vr_by_1,
	[1] = llc_conn_ac_data_ind,
	[2] = llc_conn_ac_upd_p_flag,
	[3] = llc_conn_ac_upd_nr_received,
	[4] = llc_conn_ac_clear_remote_busy_if_f_eq_1,
	[5] = llc_conn_ac_send_ack_if_needed,
	[6] = NULL,
};

static struct llc_conn_state_trans llc_normal_state_trans_8a = {
	.ev	       = llc_conn_ev_rx_i_rsp_fbit_set_x,
	.next_state    = LLC_CONN_STATE_NORMAL,
	.ev_qualifiers = llc_normal_ev_qfyrs_8a,
	.ev_actions    = llc_normal_actions_8,
};

/* State transitions for LLC_CONN_EV_RX_I_CMD_Pbit_SET_0 event */
static const llc_conn_ev_qfyr_t llc_normal_ev_qfyrs_8b[] = {
	[0] = llc_conn_ev_qlfy_p_flag_eq_0,
	[1] = NULL,
};

static struct llc_conn_state_trans llc_normal_state_trans_8b = {
	.ev	       = llc_conn_ev_rx_i_cmd_pbit_set_0,
	.next_state    = LLC_CONN_STATE_NORMAL,
	.ev_qualifiers = llc_normal_ev_qfyrs_8b,
	.ev_actions    = llc_normal_actions_8,
};

/* State transitions for LLC_CONN_EV_RX_I_RSP_Fbit_SET_0 event */
static const llc_conn_ev_qfyr_t llc_normal_ev_qfyrs_9a[] = {
	[0] = llc_conn_ev_qlfy_p_flag_eq_1,
	[1] = NULL,
};

static const llc_conn_action_t llc_normal_actions_9a[] = {
	[0] = llc_conn_ac_inc_vr_by_1,
	[1] = llc_conn_ac_upd_nr_received,
	[2] = llc_conn_ac_data_ind,
	[3] = llc_conn_ac_send_ack_if_needed,
	[4] = NULL,
};

static struct llc_conn_state_trans llc_normal_state_trans_9a = {
	.ev	       = llc_conn_ev_rx_i_rsp_fbit_set_0,
	.next_state    = LLC_CONN_STATE_NORMAL,
	.ev_qualifiers = llc_normal_ev_qfyrs_9a,
	.ev_actions    = llc_normal_actions_9a,
};

/* State transitions for LLC_CONN_EV_RX_I_CMD_Pbit_SET_0 event */
static const llc_conn_ev_qfyr_t llc_normal_ev_qfyrs_9b[] = {
	[0] = llc_conn_ev_qlfy_p_flag_eq_1,
	[1] = NULL,
};

static const llc_conn_action_t llc_normal_actions_9b[] = {
	[0] = llc_conn_ac_inc_vr_by_1,
	[1] = llc_conn_ac_upd_nr_received,
	[2] = llc_conn_ac_data_ind,
	[3] = llc_conn_ac_send_ack_if_needed,
	[4] = NULL,
};

static struct llc_conn_state_trans llc_normal_state_trans_9b = {
	.ev	       = llc_conn_ev_rx_i_cmd_pbit_set_0,
	.next_state    = LLC_CONN_STATE_NORMAL,
	.ev_qualifiers = llc_normal_ev_qfyrs_9b,
	.ev_actions    = llc_normal_actions_9b,
};

/* State transitions for LLC_CONN_EV_RX_I_CMD_Pbit_SET_1 event */
static const llc_conn_action_t llc_normal_actions_10[] = {
	[0] = llc_conn_ac_inc_vr_by_1,
	[1] = llc_conn_ac_send_ack_rsp_f_set_1,
	[2] = llc_conn_ac_rst_sendack_flag,
	[3] = llc_conn_ac_upd_nr_received,
	[4] = llc_conn_ac_data_ind,
	[5] = NULL,
};

static struct llc_conn_state_trans llc_normal_state_trans_10 = {
	.ev	       = llc_conn_ev_rx_i_cmd_pbit_set_1,
	.next_state    = LLC_CONN_STATE_NORMAL,
	.ev_qualifiers = NONE,
	.ev_actions    = llc_normal_actions_10,
};

/* State transitions for * LLC_CONN_EV_RX_RR_CMD_Pbit_SET_0 event */
static const llc_conn_action_t llc_normal_actions_11a[] = {
	[0] = llc_conn_ac_upd_p_flag,
	[1] = llc_conn_ac_upd_nr_received,
	[2] = llc_conn_ac_clear_remote_busy,
	[3] = NULL,
};

static struct llc_conn_state_trans llc_normal_state_trans_11a = {
	.ev	       = llc_conn_ev_rx_rr_cmd_pbit_set_0,
	.next_state    = LLC_CONN_STATE_NORMAL,
	.ev_qualifiers = NONE,
	.ev_actions    = llc_normal_actions_11a,
};

/* State transitions for LLC_CONN_EV_RX_RR_RSP_Fbit_SET_0 event */
static const llc_conn_action_t llc_normal_actions_11b[] = {
	[0] = llc_conn_ac_upd_p_flag,
	[1] = llc_conn_ac_upd_nr_received,
	[2] = llc_conn_ac_clear_remote_busy,
	[3] = NULL,
};

static struct llc_conn_state_trans llc_normal_state_trans_11b = {
	.ev	       = llc_conn_ev_rx_rr_rsp_fbit_set_0,
	.next_state    = LLC_CONN_STATE_NORMAL,
	.ev_qualifiers = NONE,
	.ev_actions    = llc_normal_actions_11b,
};

/* State transitions for LLC_CONN_EV_RX_RR_RSP_Fbit_SET_1 event */
static const llc_conn_ev_qfyr_t llc_normal_ev_qfyrs_11c[] = {
	[0] = llc_conn_ev_qlfy_p_flag_eq_1,
	[1] = NULL,
};

static const llc_conn_action_t llc_normal_actions_11c[] = {
	[0] = llc_conn_ac_upd_p_flag,
	[1] = llc_conn_ac_upd_nr_received,
	[2] = llc_conn_ac_inc_tx_win_size,
	[3] = llc_conn_ac_clear_remote_busy,
	[4] = NULL,
};

static struct llc_conn_state_trans llc_normal_state_trans_11c = {
	.ev	       = llc_conn_ev_rx_rr_rsp_fbit_set_1,
	.next_state    = LLC_CONN_STATE_NORMAL,
	.ev_qualifiers = llc_normal_ev_qfyrs_11c,
	.ev_actions    = llc_normal_actions_11c,
};

/* State transitions for LLC_CONN_EV_RX_RR_CMD_Pbit_SET_1 event */
static const llc_conn_action_t llc_normal_actions_12[] = {
	[0] = llc_conn_ac_send_ack_rsp_f_set_1,
	[1] = llc_conn_ac_upd_nr_received,
	[2] = llc_conn_ac_adjust_npta_by_rr,
	[3] = llc_conn_ac_rst_sendack_flag,
	[4] = llc_conn_ac_clear_remote_busy,
	[5] = NULL,
};

static struct llc_conn_state_trans llc_normal_state_trans_12 = {
	.ev	       = llc_conn_ev_rx_rr_cmd_pbit_set_1,
	.next_state    = LLC_CONN_STATE_NORMAL,
	.ev_qualifiers = NONE,
	.ev_actions    = llc_normal_actions_12,
};

/* State transitions for LLC_CONN_EV_RX_RNR_CMD_Pbit_SET_0 event */
static const llc_conn_action_t llc_normal_actions_13a[] = {
	[0] = llc_conn_ac_upd_p_flag,
	[1] = llc_conn_ac_upd_nr_received,
	[2] = llc_conn_ac_set_remote_busy,
	[3] = NULL,
};

static struct llc_conn_state_trans llc_normal_state_trans_13a = {
	.ev	       = llc_conn_ev_rx_rnr_cmd_pbit_set_0,
	.next_state    = LLC_CONN_STATE_NORMAL,
	.ev_qualifiers = NONE,
	.ev_actions    = llc_normal_actions_13a,
};

/* State transitions for LLC_CONN_EV_RX_RNR_RSP_Fbit_SET_0 event */
static const llc_conn_action_t llc_normal_actions_13b[] = {
	[0] = llc_conn_ac_upd_p_flag,
	[1] = llc_conn_ac_upd_nr_received,
	[2] = llc_conn_ac_set_remote_busy,
	[3] = NULL,
};

static struct llc_conn_state_trans llc_normal_state_trans_13b = {
	.ev	       = llc_conn_ev_rx_rnr_rsp_fbit_set_0,
	.next_state    = LLC_CONN_STATE_NORMAL,
	.ev_qualifiers = NONE,
	.ev_actions    = llc_normal_actions_13b,
};

/* State transitions for LLC_CONN_EV_RX_RNR_RSP_Fbit_SET_1 event */
static const llc_conn_ev_qfyr_t llc_normal_ev_qfyrs_13c[] = {
	[0] = llc_conn_ev_qlfy_p_flag_eq_1,
	[1] = NULL,
};

static const llc_conn_action_t llc_normal_actions_13c[] = {
	[0] = llc_conn_ac_upd_p_flag,
	[1] = llc_conn_ac_upd_nr_received,
	[2] = llc_conn_ac_set_remote_busy,
	[3] = NULL,
};

static struct llc_conn_state_trans llc_normal_state_trans_13c = {
	.ev	       = llc_conn_ev_rx_rnr_rsp_fbit_set_1,
	.next_state    = LLC_CONN_STATE_NORMAL,
	.ev_qualifiers = llc_normal_ev_qfyrs_13c,
	.ev_actions    = llc_normal_actions_13c,
};

/* State transitions for LLC_CONN_EV_RX_RNR_CMD_Pbit_SET_1 event */
static const llc_conn_action_t llc_normal_actions_14[] = {
	[0] = llc_conn_ac_send_rr_rsp_f_set_1,
	[1] = llc_conn_ac_upd_nr_received,
	[2] = llc_conn_ac_adjust_npta_by_rnr,
	[3] = llc_conn_ac_rst_sendack_flag,
	[4] = llc_conn_ac_set_remote_busy,
	[5] = NULL,
};

static struct llc_conn_state_trans llc_normal_state_trans_14 = {
	.ev	       = llc_conn_ev_rx_rnr_cmd_pbit_set_1,
	.next_state    = LLC_CONN_STATE_NORMAL,
	.ev_qualifiers = NONE,
	.ev_actions    = llc_normal_actions_14,
};

/* State transitions for LLC_CONN_EV_RX_REJ_CMD_Pbit_SET_0 event */
static const llc_conn_ev_qfyr_t llc_normal_ev_qfyrs_15a[] = {
	[0] = llc_conn_ev_qlfy_p_flag_eq_0,
	[1] = NULL,
};

static const llc_conn_action_t llc_normal_actions_15a[] = {
	[0] = llc_conn_ac_set_vs_nr,
	[1] = llc_conn_ac_upd_nr_received,
	[2] = llc_conn_ac_upd_p_flag,
	[3] = llc_conn_ac_dec_tx_win_size,
	[4] = llc_conn_ac_resend_i_xxx_x_set_0,
	[5] = llc_conn_ac_clear_remote_busy,
	[6] = NULL,
};

static struct llc_conn_state_trans llc_normal_state_trans_15a = {
	.ev	       = llc_conn_ev_rx_rej_cmd_pbit_set_0,
	.next_state    = LLC_CONN_STATE_NORMAL,
	.ev_qualifiers = llc_normal_ev_qfyrs_15a,
	.ev_actions    = llc_normal_actions_15a,
};

/* State transitions for LLC_CONN_EV_RX_REJ_RSP_Fbit_SET_X event */
static const llc_conn_ev_qfyr_t llc_normal_ev_qfyrs_15b[] = {
	[0] = llc_conn_ev_qlfy_p_flag_eq_f,
	[1] = NULL,
};

static const llc_conn_action_t llc_normal_actions_15b[] = {
	[0] = llc_conn_ac_set_vs_nr,
	[1] = llc_conn_ac_upd_nr_received,
	[2] = llc_conn_ac_upd_p_flag,
	[3] = llc_conn_ac_dec_tx_win_size,
	[4] = llc_conn_ac_resend_i_xxx_x_set_0,
	[5] = llc_conn_ac_clear_remote_busy,
	[6] = NULL,
};

static struct llc_conn_state_trans llc_normal_state_trans_15b = {
	.ev	       = llc_conn_ev_rx_rej_rsp_fbit_set_x,
	.next_state    = LLC_CONN_STATE_NORMAL,
	.ev_qualifiers = llc_normal_ev_qfyrs_15b,
	.ev_actions    = llc_normal_actions_15b,
};

/* State transitions for LLC_CONN_EV_RX_REJ_CMD_Pbit_SET_0 event */
static const llc_conn_ev_qfyr_t llc_normal_ev_qfyrs_16a[] = {
	[0] = llc_conn_ev_qlfy_p_flag_eq_1,
	[1] = NULL,
};

static const llc_conn_action_t llc_normal_actions_16a[] = {
	[0] = llc_conn_ac_set_vs_nr,
	[1] = llc_conn_ac_upd_nr_received,
	[2] = llc_conn_ac_dec_tx_win_size,
	[3] = llc_conn_ac_resend_i_xxx_x_set_0,
	[4] = llc_conn_ac_clear_remote_busy,
	[5] = NULL,
};

static struct llc_conn_state_trans llc_normal_state_trans_16a = {
	.ev	       = llc_conn_ev_rx_rej_cmd_pbit_set_0,
	.next_state    = LLC_CONN_STATE_NORMAL,
	.ev_qualifiers = llc_normal_ev_qfyrs_16a,
	.ev_actions    = llc_normal_actions_16a,
};

/* State transitions for LLC_CONN_EV_RX_REJ_RSP_Fbit_SET_0 event */
static const llc_conn_ev_qfyr_t llc_normal_ev_qfyrs_16b[] = {
	[0] = llc_conn_ev_qlfy_p_flag_eq_1,
	[1] = NULL,
};

static const llc_conn_action_t llc_normal_actions_16b[] = {
	[0] = llc_conn_ac_set_vs_nr,
	[1] = llc_conn_ac_upd_nr_received,
	[2] = llc_conn_ac_dec_tx_win_size,
	[3] = llc_conn_ac_resend_i_xxx_x_set_0,
	[4] = llc_conn_ac_clear_remote_busy,
	[5] = NULL,
};

static struct llc_conn_state_trans llc_normal_state_trans_16b = {
	.ev	       = llc_conn_ev_rx_rej_rsp_fbit_set_0,
	.next_state    = LLC_CONN_STATE_NORMAL,
	.ev_qualifiers = llc_normal_ev_qfyrs_16b,
	.ev_actions    = llc_normal_actions_16b,
};

/* State transitions for LLC_CONN_EV_RX_REJ_CMD_Pbit_SET_1 event */
static const llc_conn_action_t llc_normal_actions_17[] = {
	[0] = llc_conn_ac_set_vs_nr,
	[1] = llc_conn_ac_upd_nr_received,
	[2] = llc_conn_ac_dec_tx_win_size,
	[3] = llc_conn_ac_resend_i_rsp_f_set_1,
	[4] = llc_conn_ac_clear_remote_busy,
	[5] = NULL,
};

static struct llc_conn_state_trans llc_normal_state_trans_17 = {
	.ev	       = llc_conn_ev_rx_rej_cmd_pbit_set_1,
	.next_state    = LLC_CONN_STATE_NORMAL,
	.ev_qualifiers = NONE,
	.ev_actions    = llc_normal_actions_17,
};

/* State transitions for LLC_CONN_EV_INIT_P_F_CYCLE event */
static const llc_conn_ev_qfyr_t llc_normal_ev_qfyrs_18[] = {
	[0] = llc_conn_ev_qlfy_p_flag_eq_0,
	[1] = NULL,
};

static const llc_conn_action_t llc_normal_actions_18[] = {
	[0] = llc_conn_ac_send_rr_cmd_p_set_1,
	[1] = llc_conn_ac_start_p_timer,
	[2] = NULL,
};

static struct llc_conn_state_trans llc_normal_state_trans_18 = {
	.ev	       = llc_conn_ev_init_p_f_cycle,
	.next_state    = LLC_CONN_STATE_NORMAL,
	.ev_qualifiers = llc_normal_ev_qfyrs_18,
	.ev_actions    = llc_normal_actions_18,
};

/* State transitions for LLC_CONN_EV_P_TMR_EXP event */
static const llc_conn_ev_qfyr_t llc_normal_ev_qfyrs_19[] = {
	[0] = llc_conn_ev_qlfy_retry_cnt_lt_n2,
	[1] = NULL,
};

static const llc_conn_action_t llc_normal_actions_19[] = {
	[0] = llc_conn_ac_rst_sendack_flag,
	[1] = llc_conn_ac_send_rr_cmd_p_set_1,
	[2] = llc_conn_ac_rst_vs,
	[3] = llc_conn_ac_start_p_timer,
	[4] = llc_conn_ac_inc_retry_cnt_by_1,
	[5] = NULL,
};

static struct llc_conn_state_trans llc_normal_state_trans_19 = {
	.ev	       = llc_conn_ev_p_tmr_exp,
	.next_state    = LLC_CONN_STATE_AWAIT,
	.ev_qualifiers = llc_normal_ev_qfyrs_19,
	.ev_actions    = llc_normal_actions_19,
};

/* State transitions for LLC_CONN_EV_ACK_TMR_EXP event */
static const llc_conn_ev_qfyr_t llc_normal_ev_qfyrs_20a[] = {
	[0] = llc_conn_ev_qlfy_p_flag_eq_0,
	[1] = llc_conn_ev_qlfy_retry_cnt_lt_n2,
	[2] = NULL,
};

static const llc_conn_action_t llc_normal_actions_20a[] = {
	[0] = llc_conn_ac_rst_sendack_flag,
	[1] = llc_conn_ac_send_rr_cmd_p_set_1,
	[2] = llc_conn_ac_rst_vs,
	[3] = llc_conn_ac_start_p_timer,
	[4] = llc_conn_ac_inc_retry_cnt_by_1,
	[5] = NULL,
};

static struct llc_conn_state_trans llc_normal_state_trans_20a = {
	.ev	       = llc_conn_ev_ack_tmr_exp,
	.next_state    = LLC_CONN_STATE_AWAIT,
	.ev_qualifiers = llc_normal_ev_qfyrs_20a,
	.ev_actions    = llc_normal_actions_20a,
};

/* State transitions for LLC_CONN_EV_BUSY_TMR_EXP event */
static const llc_conn_ev_qfyr_t llc_normal_ev_qfyrs_20b[] = {
	[0] = llc_conn_ev_qlfy_p_flag_eq_0,
	[1] = llc_conn_ev_qlfy_retry_cnt_lt_n2,
	[2] = NULL,
};

static const llc_conn_action_t llc_normal_actions_20b[] = {
	[0] = llc_conn_ac_rst_sendack_flag,
	[1] = llc_conn_ac_send_rr_cmd_p_set_1,
	[2] = llc_conn_ac_rst_vs,
	[3] = llc_conn_ac_start_p_timer,
	[4] = llc_conn_ac_inc_retry_cnt_by_1,
	[5] = NULL,
};

static struct llc_conn_state_trans llc_normal_state_trans_20b = {
	.ev	       = llc_conn_ev_busy_tmr_exp,
	.next_state    = LLC_CONN_STATE_AWAIT,
	.ev_qualifiers = llc_normal_ev_qfyrs_20b,
	.ev_actions    = llc_normal_actions_20b,
};

/* State transitions for LLC_CONN_EV_TX_BUFF_FULL event */
static const llc_conn_ev_qfyr_t llc_normal_ev_qfyrs_21[] = {
	[0] = llc_conn_ev_qlfy_p_flag_eq_0,
	[1] = NULL,
};

static const llc_conn_action_t llc_normal_actions_21[] = {
	[0] = llc_conn_ac_send_rr_cmd_p_set_1,
	[1] = llc_conn_ac_start_p_timer,
	[2] = NULL,
};

static struct llc_conn_state_trans llc_normal_state_trans_21 = {
	.ev	       = llc_conn_ev_tx_buffer_full,
	.next_state    = LLC_CONN_STATE_NORMAL,
	.ev_qualifiers = llc_normal_ev_qfyrs_21,
	.ev_actions    = llc_normal_actions_21,
};

/*
 * Array of pointers;
 * one to each transition
 */
static struct llc_conn_state_trans *llc_normal_state_transitions[] = {
	 [0] = &llc_normal_state_trans_1,	/* Requests */
	 [1] = &llc_normal_state_trans_2,
	 [2] = &llc_normal_state_trans_2_1,
	 [3] = &llc_common_state_trans_1,
	 [4] = &llc_common_state_trans_2,
	 [5] = &llc_common_state_trans_end,
	 [6] = &llc_normal_state_trans_21,
	 [7] = &llc_normal_state_trans_3,	/* Local busy */
	 [8] = &llc_normal_state_trans_4,
	 [9] = &llc_common_state_trans_end,
	[10] = &llc_normal_state_trans_18,	/* Init pf cycle */
	[11] = &llc_common_state_trans_end,
	[12] = &llc_common_state_trans_11a,	/* Timers */
	[13] = &llc_common_state_trans_11b,
	[14] = &llc_common_state_trans_11c,
	[15] = &llc_common_state_trans_11d,
	[16] = &llc_normal_state_trans_19,
	[17] = &llc_normal_state_trans_20a,
	[18] = &llc_normal_state_trans_20b,
	[19] = &llc_common_state_trans_end,
	[20] = &llc_normal_state_trans_8b,	/* Receive frames */
	[21] = &llc_normal_state_trans_9b,
	[22] = &llc_normal_state_trans_10,
	[23] = &llc_normal_state_trans_11b,
	[24] = &llc_normal_state_trans_11c,
	[25] = &llc_normal_state_trans_5a,
	[26] = &llc_normal_state_trans_5b,
	[27] = &llc_normal_state_trans_5c,
	[28] = &llc_normal_state_trans_6a,
	[29] = &llc_normal_state_trans_6b,
	[30] = &llc_normal_state_trans_7,
	[31] = &llc_normal_state_trans_8a,
	[32] = &llc_normal_state_trans_9a,
	[33] = &llc_normal_state_trans_11a,
	[34] = &llc_normal_state_trans_12,
	[35] = &llc_normal_state_trans_13a,
	[36] = &llc_normal_state_trans_13b,
	[37] = &llc_normal_state_trans_13c,
	[38] = &llc_normal_state_trans_14,
	[39] = &llc_normal_state_trans_15a,
	[40] = &llc_normal_state_trans_15b,
	[41] = &llc_normal_state_trans_16a,
	[42] = &llc_normal_state_trans_16b,
	[43] = &llc_normal_state_trans_17,
	[44] = &llc_common_state_trans_3,
	[45] = &llc_common_state_trans_4,
	[46] = &llc_common_state_trans_5,
	[47] = &llc_common_state_trans_6,
	[48] = &llc_common_state_trans_7a,
	[49] = &llc_common_state_trans_7b,
	[50] = &llc_common_state_trans_8a,
	[51] = &llc_common_state_trans_8b,
	[52] = &llc_common_state_trans_8c,
	[53] = &llc_common_state_trans_9,
	/* [54] = &llc_common_state_trans_10, */
	[54] = &llc_common_state_trans_end,
};

/* LLC_CONN_STATE_BUSY transitions */
/* State transitions for LLC_CONN_EV_DATA_REQ event */
static const llc_conn_ev_qfyr_t llc_busy_ev_qfyrs_1[] = {
	[0] = llc_conn_ev_qlfy_remote_busy_eq_0,
	[1] = llc_conn_ev_qlfy_p_flag_eq_0,
	[2] = NULL,
};

static const llc_conn_action_t llc_busy_actions_1[] = {
	[0] = llc_conn_ac_send_i_xxx_x_set_0,
	[1] = llc_conn_ac_start_ack_tmr_if_not_running,
	[2] = NULL,
};

static struct llc_conn_state_trans llc_busy_state_trans_1 = {
	.ev	       = llc_conn_ev_data_req,
	.next_state    = LLC_CONN_STATE_BUSY,
	.ev_qualifiers = llc_busy_ev_qfyrs_1,
	.ev_actions    = llc_busy_actions_1,
};

/* State transitions for LLC_CONN_EV_DATA_REQ event */
static const llc_conn_ev_qfyr_t llc_busy_ev_qfyrs_2[] = {
	[0] = llc_conn_ev_qlfy_remote_busy_eq_0,
	[1] = llc_conn_ev_qlfy_p_flag_eq_1,
	[2] = NULL,
};

static const llc_conn_action_t llc_busy_actions_2[] = {
	[0] = llc_conn_ac_send_i_xxx_x_set_0,
	[1] = llc_conn_ac_start_ack_tmr_if_not_running,
	[2] = NULL,
};

static struct llc_conn_state_trans llc_busy_state_trans_2 = {
	.ev	       = llc_conn_ev_data_req,
	.next_state    = LLC_CONN_STATE_BUSY,
	.ev_qualifiers = llc_busy_ev_qfyrs_2,
	.ev_actions    = llc_busy_actions_2,
};

/* State transitions for LLC_CONN_EV_DATA_REQ event */
static const llc_conn_ev_qfyr_t llc_busy_ev_qfyrs_2_1[] = {
	[0] = llc_conn_ev_qlfy_remote_busy_eq_1,
	[1] = llc_conn_ev_qlfy_set_status_remote_busy,
	[2] = NULL,
};

/* just one member, NULL, .bss zeroes it */
static const llc_conn_action_t llc_busy_actions_2_1[1];

static struct llc_conn_state_trans llc_busy_state_trans_2_1 = {
	.ev	       = llc_conn_ev_data_req,
	.next_state    = LLC_CONN_STATE_BUSY,
	.ev_qualifiers = llc_busy_ev_qfyrs_2_1,
	.ev_actions    = llc_busy_actions_2_1,
};

/* State transitions for LLC_CONN_EV_LOCAL_BUSY_CLEARED event */
static const llc_conn_ev_qfyr_t llc_busy_ev_qfyrs_3[] = {
	[0] = llc_conn_ev_qlfy_data_flag_eq_1,
	[1] = llc_conn_ev_qlfy_p_flag_eq_0,
	[2] = NULL,
};

static const llc_conn_action_t llc_busy_actions_3[] = {
	[0] = llc_conn_ac_send_rej_xxx_x_set_0,
	[1] = llc_conn_ac_start_rej_timer,
	[2] = NULL,
};

static struct llc_conn_state_trans llc_busy_state_trans_3 = {
	.ev	       = llc_conn_ev_local_busy_cleared,
	.next_state    = LLC_CONN_STATE_REJ,
	.ev_qualifiers = llc_busy_ev_qfyrs_3,
	.ev_actions    = llc_busy_actions_3,
};

/* State transitions for LLC_CONN_EV_LOCAL_BUSY_CLEARED event */
static const llc_conn_ev_qfyr_t llc_busy_ev_qfyrs_4[] = {
	[0] = llc_conn_ev_qlfy_data_flag_eq_1,
	[1] = llc_conn_ev_qlfy_p_flag_eq_1,
	[2] = NULL,
};

static const llc_conn_action_t llc_busy_actions_4[] = {
	[0] = llc_conn_ac_send_rej_xxx_x_set_0,
	[1] = llc_conn_ac_start_rej_timer,
	[2] = NULL,
};

static struct llc_conn_state_trans llc_busy_state_trans_4 = {
	.ev	       = llc_conn_ev_local_busy_cleared,
	.next_state    = LLC_CONN_STATE_REJ,
	.ev_qualifiers = llc_busy_ev_qfyrs_4,
	.ev_actions    = llc_busy_actions_4,
};

/* State transitions for LLC_CONN_EV_LOCAL_BUSY_CLEARED event */
static const llc_conn_ev_qfyr_t llc_busy_ev_qfyrs_5[] = {
	[0] = llc_conn_ev_qlfy_data_flag_eq_0,
	[1] = llc_conn_ev_qlfy_p_flag_eq_0,
	[2] = NULL,
};

static const llc_conn_action_t llc_busy_actions_5[] = {
	[0] = llc_conn_ac_send_rr_xxx_x_set_0,
	[1] = NULL,
};

static struct llc_conn_state_trans llc_busy_state_trans_5 = {
	.ev	       = llc_conn_ev_local_busy_cleared,
	.next_state    = LLC_CONN_STATE_NORMAL,
	.ev_qualifiers = llc_busy_ev_qfyrs_5,
	.ev_actions    = llc_busy_actions_5,
};

/* State transitions for LLC_CONN_EV_LOCAL_BUSY_CLEARED event */
static const llc_conn_ev_qfyr_t llc_busy_ev_qfyrs_6[] = {
	[0] = llc_conn_ev_qlfy_data_flag_eq_0,
	[1] = llc_conn_ev_qlfy_p_flag_eq_1,
	[2] = NULL,
};

static const llc_conn_action_t llc_busy_actions_6[] = {
	[0] = llc_conn_ac_send_rr_xxx_x_set_0,
	[1] = NULL,
};

static struct llc_conn_state_trans llc_busy_state_trans_6 = {
	.ev	       = llc_conn_ev_local_busy_cleared,
	.next_state    = LLC_CONN_STATE_NORMAL,
	.ev_qualifiers = llc_busy_ev_qfyrs_6,
	.ev_actions    = llc_busy_actions_6,
};

/* State transitions for LLC_CONN_EV_LOCAL_BUSY_CLEARED event */
static const llc_conn_ev_qfyr_t llc_busy_ev_qfyrs_7[] = {
	[0] = llc_conn_ev_qlfy_data_flag_eq_2,
	[1] = llc_conn_ev_qlfy_p_flag_eq_0,
	[2] = NULL,
};

static const llc_conn_action_t llc_busy_actions_7[] = {
	[0] = llc_conn_ac_send_rr_xxx_x_set_0,
	[1] = NULL,
};

static struct llc_conn_state_trans llc_busy_state_trans_7 = {
	.ev	       = llc_conn_ev_local_busy_cleared,
	.next_state    = LLC_CONN_STATE_REJ,
	.ev_qualifiers = llc_busy_ev_qfyrs_7,
	.ev_actions    = llc_busy_actions_7,
};

/* State transitions for LLC_CONN_EV_LOCAL_BUSY_CLEARED event */
static const llc_conn_ev_qfyr_t llc_busy_ev_qfyrs_8[] = {
	[0] = llc_conn_ev_qlfy_data_flag_eq_2,
	[1] = llc_conn_ev_qlfy_p_flag_eq_1,
	[2] = NULL,
};

static const llc_conn_action_t llc_busy_actions_8[] = {
	[0] = llc_conn_ac_send_rr_xxx_x_set_0,
	[1] = NULL,
};

static struct llc_conn_state_trans llc_busy_state_trans_8 = {
	.ev	       = llc_conn_ev_local_busy_cleared,
	.next_state    = LLC_CONN_STATE_REJ,
	.ev_qualifiers = llc_busy_ev_qfyrs_8,
	.ev_actions    = llc_busy_actions_8,
};

/* State transitions for LLC_CONN_EV_RX_I_RSP_Fbit_SET_X_UNEXPD_Ns event */
static const llc_conn_ev_qfyr_t llc_busy_ev_qfyrs_9a[] = {
	[0] = llc_conn_ev_qlfy_p_flag_eq_f,
	[1] = NULL,
};

static const llc_conn_action_t llc_busy_actions_9a[] = {
	[0] = llc_conn_ac_opt_send_rnr_xxx_x_set_0,
	[1] = llc_conn_ac_upd_p_flag,
	[2] = llc_conn_ac_upd_nr_received,
	[3] = llc_conn_ac_set_data_flag_1_if_data_flag_eq_0,
	[4] = llc_conn_ac_clear_remote_busy_if_f_eq_1,
	[5] = NULL,
};

static struct llc_conn_state_trans llc_busy_state_trans_9a = {
	.ev	       = llc_conn_ev_rx_i_rsp_fbit_set_x_unexpd_ns,
	.next_state    = LLC_CONN_STATE_BUSY,
	.ev_qualifiers = llc_busy_ev_qfyrs_9a,
	.ev_actions    = llc_busy_actions_9a,
};

/* State transitions for LLC_CONN_EV_RX_I_CMD_Pbit_SET_0_UNEXPD_Ns event */
static const llc_conn_ev_qfyr_t llc_busy_ev_qfyrs_9b[] = {
	[0] = llc_conn_ev_qlfy_p_flag_eq_0,
	[1] = NULL,
};

static const llc_conn_action_t llc_busy_actions_9b[] = {
	[0] = llc_conn_ac_opt_send_rnr_xxx_x_set_0,
	[1] = llc_conn_ac_upd_p_flag,
	[2] = llc_conn_ac_upd_nr_received,
	[3] = llc_conn_ac_set_data_flag_1_if_data_flag_eq_0,
	[4] = llc_conn_ac_clear_remote_busy_if_f_eq_1,
	[5] = NULL,
};

static struct llc_conn_state_trans llc_busy_state_trans_9b = {
	.ev	       = llc_conn_ev_rx_i_cmd_pbit_set_0_unexpd_ns,
	.next_state    = LLC_CONN_STATE_BUSY,
	.ev_qualifiers = llc_busy_ev_qfyrs_9b,
	.ev_actions    = llc_busy_actions_9b,
};

/* State transitions for LLC_CONN_EV_RX_I_RSP_Fbit_SET_0_UNEXPD_Ns event */
static const llc_conn_ev_qfyr_t llc_busy_ev_qfyrs_10a[] = {
	[0] = llc_conn_ev_qlfy_p_flag_eq_1,
	[1] = NULL,
};

static const llc_conn_action_t llc_busy_actions_10a[] = {
	[0] = llc_conn_ac_opt_send_rnr_xxx_x_set_0,
	[1] = llc_conn_ac_upd_nr_received,
	[2] = llc_conn_ac_set_data_flag_1_if_data_flag_eq_0,
	[3] = NULL,
};

static struct llc_conn_state_trans llc_busy_state_trans_10a = {
	.ev	       = llc_conn_ev_rx_i_rsp_fbit_set_0_unexpd_ns,
	.next_state    = LLC_CONN_STATE_BUSY,
	.ev_qualifiers = llc_busy_ev_qfyrs_10a,
	.ev_actions    = llc_busy_actions_10a,
};

/* State transitions for LLC_CONN_EV_RX_I_CMD_Pbit_SET_0_UNEXPD_Ns event */
static const llc_conn_ev_qfyr_t llc_busy_ev_qfyrs_10b[] = {
	[0] = llc_conn_ev_qlfy_p_flag_eq_1,
	[1] = NULL,
};

static const llc_conn_action_t llc_busy_actions_10b[] = {
	[0] = llc_conn_ac_opt_send_rnr_xxx_x_set_0,
	[1] = llc_conn_ac_upd_nr_received,
	[2] = llc_conn_ac_set_data_flag_1_if_data_flag_eq_0,
	[3] = NULL,
};

static struct llc_conn_state_trans llc_busy_state_trans_10b = {
	.ev	       = llc_conn_ev_rx_i_cmd_pbit_set_0_unexpd_ns,
	.next_state    = LLC_CONN_STATE_BUSY,
	.ev_qualifiers = llc_busy_ev_qfyrs_10b,
	.ev_actions    = llc_busy_actions_10b,
};

/* State transitions for LLC_CONN_EV_RX_I_CMD_Pbit_SET_1_UNEXPD_Ns event */
static const llc_conn_action_t llc_busy_actions_11[] = {
	[0] = llc_conn_ac_send_rnr_rsp_f_set_1,
	[1] = llc_conn_ac_upd_nr_received,
	[2] = llc_conn_ac_set_data_flag_1_if_data_flag_eq_0,
	[3] = NULL,
};

static struct llc_conn_state_trans llc_busy_state_trans_11 = {
	.ev	       = llc_conn_ev_rx_i_cmd_pbit_set_1_unexpd_ns,
	.next_state    = LLC_CONN_STATE_BUSY,
	.ev_qualifiers = NONE,
	.ev_actions    = llc_busy_actions_11,
};

/* State transitions for LLC_CONN_EV_RX_I_CMD_Pbit_SET_1 event */
static const llc_conn_action_t llc_busy_actions_12[] = {
	[0] = llc_conn_ac_inc_vr_by_1,
	[1] = llc_conn_ac_data_ind,
	[2] = llc_conn_ac_send_rnr_rsp_f_set_1,
	[3] = llc_conn_ac_upd_nr_received,
	[4] = llc_conn_ac_stop_rej_tmr_if_data_flag_eq_2,
	[5] = llc_conn_ac_set_data_flag_0,
	[6] = NULL,
};

static struct llc_conn_state_trans llc_busy_state_trans_12 = {
	.ev	       = llc_conn_ev_rx_i_cmd_pbit_set_1,
	.next_state    = LLC_CONN_STATE_BUSY,
	.ev_qualifiers = NONE,
	.ev_actions    = llc_busy_actions_12,
};

/* State transitions for LLC_CONN_EV_RX_I_RSP_Fbit_SET_X event */
static const llc_conn_ev_qfyr_t llc_busy_ev_qfyrs_13a[] = {
	[0] = llc_conn_ev_qlfy_p_flag_eq_f,
	[1] = NULL,
};

static const llc_conn_action_t llc_busy_actions_13a[] = {
	[0] = llc_conn_ac_inc_vr_by_1,
	[1] = llc_conn_ac_data_ind,
	[2] = llc_conn_ac_upd_p_flag,
	[3] = llc_conn_ac_opt_send_rnr_xxx_x_set_0,
	[4] = llc_conn_ac_upd_nr_received,
	[5] = llc_conn_ac_stop_rej_tmr_if_data_flag_eq_2,
	[6] = llc_conn_ac_set_data_flag_0,
	[7] = llc_conn_ac_clear_remote_busy_if_f_eq_1,
	[8] = NULL,
};

static struct llc_conn_state_trans llc_busy_state_trans_13a = {
	.ev	       = llc_conn_ev_rx_i_rsp_fbit_set_x,
	.next_state    = LLC_CONN_STATE_BUSY,
	.ev_qualifiers = llc_busy_ev_qfyrs_13a,
	.ev_actions    = llc_busy_actions_13a,
};

/* State transitions for LLC_CONN_EV_RX_I_CMD_Pbit_SET_0 event */
static const llc_conn_ev_qfyr_t llc_busy_ev_qfyrs_13b[] = {
	[0] = llc_conn_ev_qlfy_p_flag_eq_0,
	[1] = NULL,
};

static const llc_conn_action_t llc_busy_actions_13b[] = {
	[0] = llc_conn_ac_inc_vr_by_1,
	[1] = llc_conn_ac_data_ind,
	[2] = llc_conn_ac_upd_p_flag,
	[3] = llc_conn_ac_opt_send_rnr_xxx_x_set_0,
	[4] = llc_conn_ac_upd_nr_received,
	[5] = llc_conn_ac_stop_rej_tmr_if_data_flag_eq_2,
	[6] = llc_conn_ac_set_data_flag_0,
	[7] = llc_conn_ac_clear_remote_busy_if_f_eq_1,
	[8] = NULL,
};

static struct llc_conn_state_trans llc_busy_state_trans_13b = {
	.ev	       = llc_conn_ev_rx_i_cmd_pbit_set_0,
	.next_state    = LLC_CONN_STATE_BUSY,
	.ev_qualifiers = llc_busy_ev_qfyrs_13b,
	.ev_actions    = llc_busy_actions_13b,
};

/* State transitions for LLC_CONN_EV_RX_I_RSP_Fbit_SET_0 event */
static const llc_conn_ev_qfyr_t llc_busy_ev_qfyrs_14a[] = {
	[0] = llc_conn_ev_qlfy_p_flag_eq_1,
	[1] = NULL,
};

static const llc_conn_action_t llc_busy_actions_14a[] = {
	[0] = llc_conn_ac_inc_vr_by_1,
	[1] = llc_conn_ac_data_ind,
	[2] = llc_conn_ac_opt_send_rnr_xxx_x_set_0,
	[3] = llc_conn_ac_upd_nr_received,
	[4] = llc_conn_ac_stop_rej_tmr_if_data_flag_eq_2,
	[5] = llc_conn_ac_set_data_flag_0,
	[6] = NULL,
};

static struct llc_conn_state_trans llc_busy_state_trans_14a = {
	.ev	       = llc_conn_ev_rx_i_rsp_fbit_set_0,
	.next_state    = LLC_CONN_STATE_BUSY,
	.ev_qualifiers = llc_busy_ev_qfyrs_14a,
	.ev_actions    = llc_busy_actions_14a,
};

/* State transitions for LLC_CONN_EV_RX_I_CMD_Pbit_SET_0 event */
static const llc_conn_ev_qfyr_t llc_busy_ev_qfyrs_14b[] = {
	[0] = llc_conn_ev_qlfy_p_flag_eq_1,
	[1] = NULL,
};

static const llc_conn_action_t llc_busy_actions_14b[] = {
	[0] = llc_conn_ac_inc_vr_by_1,
	[1] = llc_conn_ac_data_ind,
	[2] = llc_conn_ac_opt_send_rnr_xxx_x_set_0,
	[3] = llc_conn_ac_upd_nr_received,
	[4] = llc_conn_ac_stop_rej_tmr_if_data_flag_eq_2,
	[5] = llc_conn_ac_set_data_flag_0,
	[6] = NULL,
};

static struct llc_conn_state_trans llc_busy_state_trans_14b = {
	.ev	       = llc_conn_ev_rx_i_cmd_pbit_set_0,
	.next_state    = LLC_CONN_STATE_BUSY,
	.ev_qualifiers = llc_busy_ev_qfyrs_14b,
	.ev_actions    = llc_busy_actions_14b,
};

/* State transitions for LLC_CONN_EV_RX_RR_CMD_Pbit_SET_0 event */
static const llc_conn_action_t llc_busy_actions_15a[] = {
	[0] = llc_conn_ac_upd_p_flag,
	[1] = llc_conn_ac_upd_nr_received,
	[2] = llc_conn_ac_clear_remote_busy,
	[3] = NULL,
};

static struct llc_conn_state_trans llc_busy_state_trans_15a = {
	.ev	       = llc_conn_ev_rx_rr_cmd_pbit_set_0,
	.next_state    = LLC_CONN_STATE_BUSY,
	.ev_qualifiers = NONE,
	.ev_actions    = llc_busy_actions_15a,
};

/* State transitions for LLC_CONN_EV_RX_RR_RSP_Fbit_SET_0 event */
static const llc_conn_action_t llc_busy_actions_15b[] = {
	[0] = llc_conn_ac_upd_p_flag,
	[1] = llc_conn_ac_upd_nr_received,
	[2] = llc_conn_ac_clear_remote_busy,
	[3] = NULL,
};

static struct llc_conn_state_trans llc_busy_state_trans_15b = {
	.ev	       = llc_conn_ev_rx_rr_rsp_fbit_set_0,
	.next_state    = LLC_CONN_STATE_BUSY,
	.ev_qualifiers = NONE,
	.ev_actions    = llc_busy_actions_15b,
};

/* State transitions for LLC_CONN_EV_RX_RR_RSP_Fbit_SET_1 event */
static const llc_conn_ev_qfyr_t llc_busy_ev_qfyrs_15c[] = {
	[0] = llc_conn_ev_qlfy_p_flag_eq_1,
	[1] = NULL,
};

static const llc_conn_action_t llc_busy_actions_15c[] = {
	[0] = llc_conn_ac_upd_p_flag,
	[1] = llc_conn_ac_upd_nr_received,
	[2] = llc_conn_ac_clear_remote_busy,
	[3] = NULL,
};

static struct llc_conn_state_trans llc_busy_state_trans_15c = {
	.ev	       = llc_conn_ev_rx_rr_rsp_fbit_set_1,
	.next_state    = LLC_CONN_STATE_BUSY,
	.ev_qualifiers = llc_busy_ev_qfyrs_15c,
	.ev_actions    = llc_busy_actions_15c,
};

/* State transitions for LLC_CONN_EV_RX_RR_CMD_Pbit_SET_1 event */
static const llc_conn_action_t llc_busy_actions_16[] = {
	[0] = llc_conn_ac_send_rnr_rsp_f_set_1,
	[1] = llc_conn_ac_upd_nr_received,
	[2] = llc_conn_ac_clear_remote_busy,
	[3] = NULL,
};

static struct llc_conn_state_trans llc_busy_state_trans_16 = {
	.ev	       = llc_conn_ev_rx_rr_cmd_pbit_set_1,
	.next_state    = LLC_CONN_STATE_BUSY,
	.ev_qualifiers = NONE,
	.ev_actions    = llc_busy_actions_16,
};

/* State transitions for LLC_CONN_EV_RX_RNR_CMD_Pbit_SET_0 event */
static const llc_conn_action_t llc_busy_actions_17a[] = {
	[0] = llc_conn_ac_upd_p_flag,
	[1] = llc_conn_ac_upd_nr_received,
	[2] = llc_conn_ac_set_remote_busy,
	[3] = NULL,
};

static struct llc_conn_state_trans llc_busy_state_trans_17a = {
	.ev	       = llc_conn_ev_rx_rnr_cmd_pbit_set_0,
	.next_state    = LLC_CONN_STATE_BUSY,
	.ev_qualifiers = NONE,
	.ev_actions    = llc_busy_actions_17a,
};

/* State transitions for LLC_CONN_EV_RX_RNR_RSP_Fbit_SET_0 event */
static const llc_conn_action_t llc_busy_actions_17b[] = {
	[0] = llc_conn_ac_upd_p_flag,
	[1] = llc_conn_ac_upd_nr_received,
	[2] = llc_conn_ac_set_remote_busy,
	[3] = NULL,
};

static struct llc_conn_state_trans llc_busy_state_trans_17b = {
	.ev	       = llc_conn_ev_rx_rnr_rsp_fbit_set_0,
	.next_state    = LLC_CONN_STATE_BUSY,
	.ev_qualifiers = NONE,
	.ev_actions    = llc_busy_actions_17b,
};

/* State transitions for LLC_CONN_EV_RX_RNR_RSP_Fbit_SET_1 event */
static const llc_conn_ev_qfyr_t llc_busy_ev_qfyrs_17c[] = {
	[0] = llc_conn_ev_qlfy_p_flag_eq_1,
	[1] = NULL,
};

static const llc_conn_action_t llc_busy_actions_17c[] = {
	[0] = llc_conn_ac_upd_p_flag,
	[1] = llc_conn_ac_upd_nr_received,
	[2] = llc_conn_ac_set_remote_busy,
	[3] = NULL,
};

static struct llc_conn_state_trans llc_busy_state_trans_17c = {
	.ev	       = llc_conn_ev_rx_rnr_rsp_fbit_set_1,
	.next_state    = LLC_CONN_STATE_BUSY,
	.ev_qualifiers = llc_busy_ev_qfyrs_17c,
	.ev_actions    = llc_busy_actions_17c,
};

/* State transitions for LLC_CONN_EV_RX_RNR_CMD_Pbit_SET_1 event */
static const llc_conn_action_t llc_busy_actions_18[] = {
	[0] = llc_conn_ac_send_rnr_rsp_f_set_1,
	[1] = llc_conn_ac_upd_nr_received,
	[2] = llc_conn_ac_set_remote_busy,
	[3] = NULL,
};

static struct llc_conn_state_trans llc_busy_state_trans_18 = {
	.ev	       = llc_conn_ev_rx_rnr_cmd_pbit_set_1,
	.next_state    = LLC_CONN_STATE_BUSY,
	.ev_qualifiers = NONE,
	.ev_actions    = llc_busy_actions_18,
};

/* State transitions for LLC_CONN_EV_RX_REJ_CMD_Pbit_SET_0 event */
static const llc_conn_ev_qfyr_t llc_busy_ev_qfyrs_19a[] = {
	[0] = llc_conn_ev_qlfy_p_flag_eq_0,
	[1] = NULL,
};

static const llc_conn_action_t llc_busy_actions_19a[] = {
	[0] = llc_conn_ac_set_vs_nr,
	[1] = llc_conn_ac_upd_nr_received,
	[2] = llc_conn_ac_upd_p_flag,
	[3] = llc_conn_ac_resend_i_xxx_x_set_0,
	[4] = llc_conn_ac_clear_remote_busy,
	[5] = NULL,
};

static struct llc_conn_state_trans llc_busy_state_trans_19a = {
	.ev	       = llc_conn_ev_rx_rej_cmd_pbit_set_0,
	.next_state    = LLC_CONN_STATE_BUSY,
	.ev_qualifiers = llc_busy_ev_qfyrs_19a,
	.ev_actions    = llc_busy_actions_19a,
};

/* State transitions for LLC_CONN_EV_RX_REJ_RSP_Fbit_SET_X event */
static const llc_conn_ev_qfyr_t llc_busy_ev_qfyrs_19b[] = {
	[0] = llc_conn_ev_qlfy_p_flag_eq_f,
	[1] = NULL,
};

static const llc_conn_action_t llc_busy_actions_19b[] = {
	[0] = llc_conn_ac_set_vs_nr,
	[1] = llc_conn_ac_upd_nr_received,
	[2] = llc_conn_ac_upd_p_flag,
	[3] = llc_conn_ac_resend_i_xxx_x_set_0,
	[4] = llc_conn_ac_clear_remote_busy,
	[5] = NULL,
};

static struct llc_conn_state_trans llc_busy_state_trans_19b = {
	.ev	       = llc_conn_ev_rx_rej_rsp_fbit_set_x,
	.next_state    = LLC_CONN_STATE_BUSY,
	.ev_qualifiers = llc_busy_ev_qfyrs_19b,
	.ev_actions    = llc_busy_actions_19b,
};

/* State transitions for LLC_CONN_EV_RX_REJ_CMD_Pbit_SET_0 event */
static const llc_conn_ev_qfyr_t llc_busy_ev_qfyrs_20a[] = {
	[0] = llc_conn_ev_qlfy_p_flag_eq_1,
	[1] = NULL,
};

static const llc_conn_action_t llc_busy_actions_20a[] = {
	[0] = llc_conn_ac_set_vs_nr,
	[1] = llc_conn_ac_upd_nr_received,
	[2] = llc_conn_ac_resend_i_xxx_x_set_0,
	[3] = llc_conn_ac_clear_remote_busy,
	[4] = NULL,
};

static struct llc_conn_state_trans llc_busy_state_trans_20a = {
	.ev	       = llc_conn_ev_rx_rej_cmd_pbit_set_0,
	.next_state    = LLC_CONN_STATE_BUSY,
	.ev_qualifiers = llc_busy_ev_qfyrs_20a,
	.ev_actions    = llc_busy_actions_20a,
};

/* State transitions for LLC_CONN_EV_RX_REJ_RSP_Fbit_SET_0 event */
static const llc_conn_ev_qfyr_t llc_busy_ev_qfyrs_20b[] = {
	[0] = llc_conn_ev_qlfy_p_flag_eq_1,
	[1] = NULL,
};

static const llc_conn_action_t llc_busy_actions_20b[] = {
	[0] = llc_conn_ac_set_vs_nr,
	[1] = llc_conn_ac_upd_nr_received,
	[2] = llc_conn_ac_resend_i_xxx_x_set_0,
	[3] = llc_conn_ac_clear_remote_busy,
	[4] = NULL,
};

static struct llc_conn_state_trans llc_busy_state_trans_20b = {
	.ev	       = llc_conn_ev_rx_rej_rsp_fbit_set_0,
	.next_state    = LLC_CONN_STATE_BUSY,
	.ev_qualifiers = llc_busy_ev_qfyrs_20b,
	.ev_actions    = llc_busy_actions_20b,
};

/* State transitions for LLC_CONN_EV_RX_REJ_CMD_Pbit_SET_1 event */
static const llc_conn_action_t llc_busy_actions_21[] = {
	[0] = llc_conn_ac_set_vs_nr,
	[1] = llc_conn_ac_upd_nr_received,
	[2] = llc_conn_ac_send_rnr_rsp_f_set_1,
	[3] = llc_conn_ac_resend_i_xxx_x_set_0,
	[4] = llc_conn_ac_clear_remote_busy,
	[5] = NULL,
};

static struct llc_conn_state_trans llc_busy_state_trans_21 = {
	.ev	       = llc_conn_ev_rx_rej_cmd_pbit_set_1,
	.next_state    = LLC_CONN_STATE_BUSY,
	.ev_qualifiers = NONE,
	.ev_actions    = llc_busy_actions_21,
};

/* State transitions for LLC_CONN_EV_INIT_P_F_CYCLE event */
static const llc_conn_ev_qfyr_t llc_busy_ev_qfyrs_22[] = {
	[0] = llc_conn_ev_qlfy_p_flag_eq_0,
	[1] = NULL,
};

static const llc_conn_action_t llc_busy_actions_22[] = {
	[0] = llc_conn_ac_send_rnr_cmd_p_set_1,
	[1] = llc_conn_ac_start_p_timer,
	[2] = NULL,
};

static struct llc_conn_state_trans llc_busy_state_trans_22 = {
	.ev	       = llc_conn_ev_init_p_f_cycle,
	.next_state    = LLC_CONN_STATE_BUSY,
	.ev_qualifiers = llc_busy_ev_qfyrs_22,
	.ev_actions    = llc_busy_actions_22,
};

/* State transitions for LLC_CONN_EV_P_TMR_EXP event */
static const llc_conn_ev_qfyr_t llc_busy_ev_qfyrs_23[] = {
	[0] = llc_conn_ev_qlfy_retry_cnt_lt_n2,
	[1] = NULL,
};

static const llc_conn_action_t llc_busy_actions_23[] = {
	[0] = llc_conn_ac_send_rnr_cmd_p_set_1,
	[1] = llc_conn_ac_rst_vs,
	[2] = llc_conn_ac_start_p_timer,
	[3] = llc_conn_ac_inc_retry_cnt_by_1,
	[4] = NULL,
};

static struct llc_conn_state_trans llc_busy_state_trans_23 = {
	.ev	       = llc_conn_ev_p_tmr_exp,
	.next_state    = LLC_CONN_STATE_AWAIT_BUSY,
	.ev_qualifiers = llc_busy_ev_qfyrs_23,
	.ev_actions    = llc_busy_actions_23,
};

/* State transitions for LLC_CONN_EV_ACK_TMR_EXP event */
static const llc_conn_ev_qfyr_t llc_busy_ev_qfyrs_24a[] = {
	[0] = llc_conn_ev_qlfy_p_flag_eq_0,
	[1] = llc_conn_ev_qlfy_retry_cnt_lt_n2,
	[2] = NULL,
};

static const llc_conn_action_t llc_busy_actions_24a[] = {
	[0] = llc_conn_ac_send_rnr_cmd_p_set_1,
	[1] = llc_conn_ac_start_p_timer,
	[2] = llc_conn_ac_inc_retry_cnt_by_1,
	[3] = llc_conn_ac_rst_vs,
	[4] = NULL,
};

static struct llc_conn_state_trans llc_busy_state_trans_24a = {
	.ev	       = llc_conn_ev_ack_tmr_exp,
	.next_state    = LLC_CONN_STATE_AWAIT_BUSY,
	.ev_qualifiers = llc_busy_ev_qfyrs_24a,
	.ev_actions    = llc_busy_actions_24a,
};

/* State transitions for LLC_CONN_EV_BUSY_TMR_EXP event */
static const llc_conn_ev_qfyr_t llc_busy_ev_qfyrs_24b[] = {
	[0] = llc_conn_ev_qlfy_p_flag_eq_0,
	[1] = llc_conn_ev_qlfy_retry_cnt_lt_n2,
	[2] = NULL,
};

static const llc_conn_action_t llc_busy_actions_24b[] = {
	[0] = llc_conn_ac_send_rnr_cmd_p_set_1,
	[1] = llc_conn_ac_start_p_timer,
	[2] = llc_conn_ac_inc_retry_cnt_by_1,
	[3] = llc_conn_ac_rst_vs,
	[4] = NULL,
};

static struct llc_conn_state_trans llc_busy_state_trans_24b = {
	.ev	       = llc_conn_ev_busy_tmr_exp,
	.next_state    = LLC_CONN_STATE_AWAIT_BUSY,
	.ev_qualifiers = llc_busy_ev_qfyrs_24b,
	.ev_actions    = llc_busy_actions_24b,
};

/* State transitions for LLC_CONN_EV_REJ_TMR_EXP event */
static const llc_conn_ev_qfyr_t llc_busy_ev_qfyrs_25[] = {
	[0] = llc_conn_ev_qlfy_p_flag_eq_0,
	[1] = llc_conn_ev_qlfy_retry_cnt_lt_n2,
	[2] = NULL,
};

static const llc_conn_action_t llc_busy_actions_25[] = {
	[0] = llc_conn_ac_send_rnr_cmd_p_set_1,
	[1] = llc_conn_ac_start_p_timer,
	[2] = llc_conn_ac_inc_retry_cnt_by_1,
	[3] = llc_conn_ac_rst_vs,
	[4] = llc_conn_ac_set_data_flag_1,
	[5] = NULL,
};

static struct llc_conn_state_trans llc_busy_state_trans_25 = {
	.ev	       = llc_conn_ev_rej_tmr_exp,
	.next_state    = LLC_CONN_STATE_AWAIT_BUSY,
	.ev_qualifiers = llc_busy_ev_qfyrs_25,
	.ev_actions    = llc_busy_actions_25,
};

/* State transitions for LLC_CONN_EV_REJ_TMR_EXP event */
static const llc_conn_ev_qfyr_t llc_busy_ev_qfyrs_26[] = {
	[0] = llc_conn_ev_qlfy_p_flag_eq_1,
	[1] = llc_conn_ev_qlfy_retry_cnt_lt_n2,
	[2] = NULL,
};

static const llc_conn_action_t llc_busy_actions_26[] = {
	[0] = llc_conn_ac_set_data_flag_1,
	[1] = NULL,
};

static struct llc_conn_state_trans llc_busy_state_trans_26 = {
	.ev	       = llc_conn_ev_rej_tmr_exp,
	.next_state    = LLC_CONN_STATE_BUSY,
	.ev_qualifiers = llc_busy_ev_qfyrs_26,
	.ev_actions    = llc_busy_actions_26,
};

/*
 * Array of pointers;
 * one to each transition
 */
static struct llc_conn_state_trans *llc_busy_state_transitions[] = {
	 [0] = &llc_common_state_trans_1,	/* Request */
	 [1] = &llc_common_state_trans_2,
	 [2] = &llc_busy_state_trans_1,
	 [3] = &llc_busy_state_trans_2,
	 [4] = &llc_busy_state_trans_2_1,
	 [5] = &llc_common_state_trans_end,
	 [6] = &llc_busy_state_trans_3,		/* Local busy */
	 [7] = &llc_busy_state_trans_4,
	 [8] = &llc_busy_state_trans_5,
	 [9] = &llc_busy_state_trans_6,
	[10] = &llc_busy_state_trans_7,
	[11] = &llc_busy_state_trans_8,
	[12] = &llc_common_state_trans_end,
	[13] = &llc_busy_state_trans_22,	/* Initiate PF cycle */
	[14] = &llc_common_state_trans_end,
	[15] = &llc_common_state_trans_11a,	/* Timer */
	[16] = &llc_common_state_trans_11b,
	[17] = &llc_common_state_trans_11c,
	[18] = &llc_common_state_trans_11d,
	[19] = &llc_busy_state_trans_23,
	[20] = &llc_busy_state_trans_24a,
	[21] = &llc_busy_state_trans_24b,
	[22] = &llc_busy_state_trans_25,
	[23] = &llc_busy_state_trans_26,
	[24] = &llc_common_state_trans_end,
	[25] = &llc_busy_state_trans_9a,	/* Receive frame */
	[26] = &llc_busy_state_trans_9b,
	[27] = &llc_busy_state_trans_10a,
	[28] = &llc_busy_state_trans_10b,
	[29] = &llc_busy_state_trans_11,
	[30] = &llc_busy_state_trans_12,
	[31] = &llc_busy_state_trans_13a,
	[32] = &llc_busy_state_trans_13b,
	[33] = &llc_busy_state_trans_14a,
	[34] = &llc_busy_state_trans_14b,
	[35] = &llc_busy_state_trans_15a,
	[36] = &llc_busy_state_trans_15b,
	[37] = &llc_busy_state_trans_15c,
	[38] = &llc_busy_state_trans_16,
	[39] = &llc_busy_state_trans_17a,
	[40] = &llc_busy_state_trans_17b,
	[41] = &llc_busy_state_trans_17c,
	[42] = &llc_busy_state_trans_18,
	[43] = &llc_busy_state_trans_19a,
	[44] = &llc_busy_state_trans_19b,
	[45] = &llc_busy_state_trans_20a,
	[46] = &llc_busy_state_trans_20b,
	[47] = &llc_busy_state_trans_21,
	[48] = &llc_common_state_trans_3,
	[49] = &llc_common_state_trans_4,
	[50] = &llc_common_state_trans_5,
	[51] = &llc_common_state_trans_6,
	[52] = &llc_common_state_trans_7a,
	[53] = &llc_common_state_trans_7b,
	[54] = &llc_common_state_trans_8a,
	[55] = &llc_common_state_trans_8b,
	[56] = &llc_common_state_trans_8c,
	[57] = &llc_common_state_trans_9,
	/* [58] = &llc_common_state_trans_10, */
	[58] = &llc_common_state_trans_end,
};

/* LLC_CONN_STATE_REJ transitions */
/* State transitions for LLC_CONN_EV_DATA_REQ event */
static const llc_conn_ev_qfyr_t llc_reject_ev_qfyrs_1[] = {
	[0] = llc_conn_ev_qlfy_remote_busy_eq_0,
	[1] = llc_conn_ev_qlfy_p_flag_eq_0,
	[2] = NULL,
};

static const llc_conn_action_t llc_reject_actions_1[] = {
	[0] = llc_conn_ac_send_i_xxx_x_set_0,
	[1] = NULL,
};

static struct llc_conn_state_trans llc_reject_state_trans_1 = {
	.ev	       = llc_conn_ev_data_req,
	.next_state    = LLC_CONN_STATE_REJ,
	.ev_qualifiers = llc_reject_ev_qfyrs_1,
	.ev_actions    = llc_reject_actions_1,
};

/* State transitions for LLC_CONN_EV_DATA_REQ event */
static const llc_conn_ev_qfyr_t llc_reject_ev_qfyrs_2[] = {
	[0] = llc_conn_ev_qlfy_remote_busy_eq_0,
	[1] = llc_conn_ev_qlfy_p_flag_eq_1,
	[2] = NULL,
};

static const llc_conn_action_t llc_reject_actions_2[] = {
	[0] = llc_conn_ac_send_i_xxx_x_set_0,
	[1] = NULL,
};

static struct llc_conn_state_trans llc_reject_state_trans_2 = {
	.ev	       = llc_conn_ev_data_req,
	.next_state    = LLC_CONN_STATE_REJ,
	.ev_qualifiers = llc_reject_ev_qfyrs_2,
	.ev_actions    = llc_reject_actions_2,
};

/* State transitions for LLC_CONN_EV_DATA_REQ event */
static const llc_conn_ev_qfyr_t llc_reject_ev_qfyrs_2_1[] = {
	[0] = llc_conn_ev_qlfy_remote_busy_eq_1,
	[1] = llc_conn_ev_qlfy_set_status_remote_busy,
	[2] = NULL,
};

/* just one member, NULL, .bss zeroes it */
static const llc_conn_action_t llc_reject_actions_2_1[1];

static struct llc_conn_state_trans llc_reject_state_trans_2_1 = {
	.ev	       = llc_conn_ev_data_req,
	.next_state    = LLC_CONN_STATE_REJ,
	.ev_qualifiers = llc_reject_ev_qfyrs_2_1,
	.ev_actions    = llc_reject_actions_2_1,
};


/* State transitions for LLC_CONN_EV_LOCAL_BUSY_DETECTED event */
static const llc_conn_ev_qfyr_t llc_reject_ev_qfyrs_3[] = {
	[0] = llc_conn_ev_qlfy_p_flag_eq_0,
	[1] = NULL,
};

static const llc_conn_action_t llc_reject_actions_3[] = {
	[0] = llc_conn_ac_send_rnr_xxx_x_set_0,
	[1] = llc_conn_ac_set_data_flag_2,
	[2] = NULL,
};

static struct llc_conn_state_trans llc_reject_state_trans_3 = {
	.ev	       = llc_conn_ev_local_busy_detected,
	.next_state    = LLC_CONN_STATE_BUSY,
	.ev_qualifiers = llc_reject_ev_qfyrs_3,
	.ev_actions    = llc_reject_actions_3,
};

/* State transitions for LLC_CONN_EV_LOCAL_BUSY_DETECTED event */
static const llc_conn_ev_qfyr_t llc_reject_ev_qfyrs_4[] = {
	[0] = llc_conn_ev_qlfy_p_flag_eq_1,
	[1] = NULL,
};

static const llc_conn_action_t llc_reject_actions_4[] = {
	[0] = llc_conn_ac_send_rnr_xxx_x_set_0,
	[1] = llc_conn_ac_set_data_flag_2,
	[2] = NULL,
};

static struct llc_conn_state_trans llc_reject_state_trans_4 = {
	.ev	       = llc_conn_ev_local_busy_detected,
	.next_state    = LLC_CONN_STATE_BUSY,
	.ev_qualifiers = llc_reject_ev_qfyrs_4,
	.ev_actions    = llc_reject_actions_4,
};

/* State transitions for LLC_CONN_EV_RX_I_CMD_Pbit_SET_0_UNEXPD_Ns event */
static const llc_conn_action_t llc_reject_actions_5a[] = {
	[0] = llc_conn_ac_upd_nr_received,
	[1] = llc_conn_ac_upd_p_flag,
	[2] = llc_conn_ac_clear_remote_busy_if_f_eq_1,
	[3] = NULL,
};

static struct llc_conn_state_trans llc_reject_state_trans_5a = {
	.ev	       = llc_conn_ev_rx_i_cmd_pbit_set_0_unexpd_ns,
	.next_state    = LLC_CONN_STATE_REJ,
	.ev_qualifiers = NONE,
	.ev_actions    = llc_reject_actions_5a,
};

/* State transitions for LLC_CONN_EV_RX_I_RSP_Fbit_SET_0_UNEXPD_Ns event */
static const llc_conn_action_t llc_reject_actions_5b[] = {
	[0] = llc_conn_ac_upd_nr_received,
	[1] = llc_conn_ac_upd_p_flag,
	[2] = llc_conn_ac_clear_remote_busy_if_f_eq_1,
	[3] = NULL,
};

static struct llc_conn_state_trans llc_reject_state_trans_5b = {
	.ev	       = llc_conn_ev_rx_i_rsp_fbit_set_0_unexpd_ns,
	.next_state    = LLC_CONN_STATE_REJ,
	.ev_qualifiers = NONE,
	.ev_actions    = llc_reject_actions_5b,
};

/* State transitions for LLC_CONN_EV_RX_I_RSP_Fbit_SET_1_UNEXPD_Ns event */
static const llc_conn_ev_qfyr_t llc_reject_ev_qfyrs_5c[] = {
	[0] = llc_conn_ev_qlfy_p_flag_eq_1,
	[1] = NULL,
};

static const llc_conn_action_t llc_reject_actions_5c[] = {
	[0] = llc_conn_ac_upd_nr_received,
	[1] = llc_conn_ac_upd_p_flag,
	[2] = llc_conn_ac_clear_remote_busy_if_f_eq_1,
	[3] = NULL,
};

static struct llc_conn_state_trans llc_reject_state_trans_5c = {
	.ev	       = llc_conn_ev_rx_i_rsp_fbit_set_1_unexpd_ns,
	.next_state    = LLC_CONN_STATE_REJ,
	.ev_qualifiers = llc_reject_ev_qfyrs_5c,
	.ev_actions    = llc_reject_actions_5c,
};

/* State transitions for LLC_CONN_EV_RX_I_CMD_Pbit_SET_1_UNEXPD_Ns event */
static const llc_conn_action_t llc_reject_actions_6[] = {
	[0] = llc_conn_ac_send_rr_rsp_f_set_1,
	[1] = llc_conn_ac_upd_nr_received,
	[2] = NULL,
};

static struct llc_conn_state_trans llc_reject_state_trans_6 = {
	.ev	       = llc_conn_ev_rx_i_cmd_pbit_set_1_unexpd_ns,
	.next_state    = LLC_CONN_STATE_REJ,
	.ev_qualifiers = NONE,
	.ev_actions    = llc_reject_actions_6,
};

/* State transitions for LLC_CONN_EV_RX_I_RSP_Fbit_SET_X event */
static const llc_conn_ev_qfyr_t llc_reject_ev_qfyrs_7a[] = {
	[0] = llc_conn_ev_qlfy_p_flag_eq_f,
	[1] = NULL,
};

static const llc_conn_action_t llc_reject_actions_7a[] = {
	[0] = llc_conn_ac_inc_vr_by_1,
	[1] = llc_conn_ac_data_ind,
	[2] = llc_conn_ac_upd_p_flag,
	[3] = llc_conn_ac_send_ack_xxx_x_set_0,
	[4] = llc_conn_ac_upd_nr_received,
	[5] = llc_conn_ac_clear_remote_busy_if_f_eq_1,
	[6] = llc_conn_ac_stop_rej_timer,
	[7] = NULL,

};

static struct llc_conn_state_trans llc_reject_state_trans_7a = {
	.ev	       = llc_conn_ev_rx_i_rsp_fbit_set_x,
	.next_state    = LLC_CONN_STATE_NORMAL,
	.ev_qualifiers = llc_reject_ev_qfyrs_7a,
	.ev_actions    = llc_reject_actions_7a,
};

/* State transitions for LLC_CONN_EV_RX_I_CMD_Pbit_SET_0 event */
static const llc_conn_ev_qfyr_t llc_reject_ev_qfyrs_7b[] = {
	[0] = llc_conn_ev_qlfy_p_flag_eq_0,
	[1] = NULL,
};

static const llc_conn_action_t llc_reject_actions_7b[] = {
	[0] = llc_conn_ac_inc_vr_by_1,
	[1] = llc_conn_ac_data_ind,
	[2] = llc_conn_ac_upd_p_flag,
	[3] = llc_conn_ac_send_ack_xxx_x_set_0,
	[4] = llc_conn_ac_upd_nr_received,
	[5] = llc_conn_ac_clear_remote_busy_if_f_eq_1,
	[6] = llc_conn_ac_stop_rej_timer,
	[7] = NULL,
};

static struct llc_conn_state_trans llc_reject_state_trans_7b = {
	.ev	       = llc_conn_ev_rx_i_cmd_pbit_set_0,
	.next_state    = LLC_CONN_STATE_NORMAL,
	.ev_qualifiers = llc_reject_ev_qfyrs_7b,
	.ev_actions    = llc_reject_actions_7b,
};

/* State transitions for LLC_CONN_EV_RX_I_RSP_Fbit_SET_0 event */
static const llc_conn_ev_qfyr_t llc_reject_ev_qfyrs_8a[] = {
	[0] = llc_conn_ev_qlfy_p_flag_eq_1,
	[1] = NULL,
};

static const llc_conn_action_t llc_reject_actions_8a[] = {
	[0] = llc_conn_ac_inc_vr_by_1,
	[1] = llc_conn_ac_data_ind,
	[2] = llc_conn_ac_send_ack_xxx_x_set_0,
	[3] = llc_conn_ac_upd_nr_received,
	[4] = llc_conn_ac_stop_rej_timer,
	[5] = NULL,
};

static struct llc_conn_state_trans llc_reject_state_trans_8a = {
	.ev	       = llc_conn_ev_rx_i_rsp_fbit_set_0,
	.next_state    = LLC_CONN_STATE_NORMAL,
	.ev_qualifiers = llc_reject_ev_qfyrs_8a,
	.ev_actions    = llc_reject_actions_8a,
};

/* State transitions for LLC_CONN_EV_RX_I_CMD_Pbit_SET_0 event */
static const llc_conn_ev_qfyr_t llc_reject_ev_qfyrs_8b[] = {
	[0] = llc_conn_ev_qlfy_p_flag_eq_1,
	[1] = NULL,
};

static const llc_conn_action_t llc_reject_actions_8b[] = {
	[0] = llc_conn_ac_inc_vr_by_1,
	[1] = llc_conn_ac_data_ind,
	[2] = llc_conn_ac_send_ack_xxx_x_set_0,
	[3] = llc_conn_ac_upd_nr_received,
	[4] = llc_conn_ac_stop_rej_timer,
	[5] = NULL,
};

static struct llc_conn_state_trans llc_reject_state_trans_8b = {
	.ev	       = llc_conn_ev_rx_i_cmd_pbit_set_0,
	.next_state    = LLC_CONN_STATE_NORMAL,
	.ev_qualifiers = llc_reject_ev_qfyrs_8b,
	.ev_actions    = llc_reject_actions_8b,
};

/* State transitions for LLC_CONN_EV_RX_I_CMD_Pbit_SET_1 event */
static const llc_conn_action_t llc_reject_actions_9[] = {
	[0] = llc_conn_ac_inc_vr_by_1,
	[1] = llc_conn_ac_data_ind,
	[2] = llc_conn_ac_send_ack_rsp_f_set_1,
	[3] = llc_conn_ac_upd_nr_received,
	[4] = llc_conn_ac_stop_rej_timer,
	[5] = NULL,
};

static struct llc_conn_state_trans llc_reject_state_trans_9 = {
	.ev	       = llc_conn_ev_rx_i_cmd_pbit_set_1,
	.next_state    = LLC_CONN_STATE_NORMAL,
	.ev_qualifiers = NONE,
	.ev_actions    = llc_reject_actions_9,
};

/* State transitions for LLC_CONN_EV_RX_RR_CMD_Pbit_SET_0 event */
static const llc_conn_action_t llc_reject_actions_10a[] = {
	[0] = llc_conn_ac_upd_p_flag,
	[1] = llc_conn_ac_upd_nr_received,
	[2] = llc_conn_ac_clear_remote_busy,
	[3] = NULL,
};

static struct llc_conn_state_trans llc_reject_state_trans_10a = {
	.ev	       = llc_conn_ev_rx_rr_cmd_pbit_set_0,
	.next_state    = LLC_CONN_STATE_REJ,
	.ev_qualifiers = NONE,
	.ev_actions    = llc_reject_actions_10a,
};

/* State transitions for LLC_CONN_EV_RX_RR_RSP_Fbit_SET_0 event */
static const llc_conn_action_t llc_reject_actions_10b[] = {
	[0] = llc_conn_ac_upd_p_flag,
	[1] = llc_conn_ac_upd_nr_received,
	[2] = llc_conn_ac_clear_remote_busy,
	[3] = NULL,
};

static struct llc_conn_state_trans llc_reject_state_trans_10b = {
	.ev	       = llc_conn_ev_rx_rr_rsp_fbit_set_0,
	.next_state    = LLC_CONN_STATE_REJ,
	.ev_qualifiers = NONE,
	.ev_actions    = llc_reject_actions_10b,
};

/* State transitions for LLC_CONN_EV_RX_RR_RSP_Fbit_SET_1 event */
static const llc_conn_ev_qfyr_t llc_reject_ev_qfyrs_10c[] = {
	[0] = llc_conn_ev_qlfy_p_flag_eq_1,
	[1] = NULL,
};

static const llc_conn_action_t llc_reject_actions_10c[] = {
	[0] = llc_conn_ac_upd_p_flag,
	[1] = llc_conn_ac_upd_nr_received,
	[2] = llc_conn_ac_clear_remote_busy,
	[3] = NULL,
};

static struct llc_conn_state_trans llc_reject_state_trans_10c = {
	.ev	       = llc_conn_ev_rx_rr_rsp_fbit_set_1,
	.next_state    = LLC_CONN_STATE_REJ,
	.ev_qualifiers = llc_reject_ev_qfyrs_10c,
	.ev_actions    = llc_reject_actions_10c,
};

/* State transitions for LLC_CONN_EV_RX_RR_CMD_Pbit_SET_1 event */
static const llc_conn_action_t llc_reject_actions_11[] = {
	[0] = llc_conn_ac_send_ack_rsp_f_set_1,
	[1] = llc_conn_ac_upd_nr_received,
	[2] = llc_conn_ac_clear_remote_busy,
	[3] = NULL,
};

static struct llc_conn_state_trans llc_reject_state_trans_11 = {
	.ev	       = llc_conn_ev_rx_rr_cmd_pbit_set_1,
	.next_state    = LLC_CONN_STATE_REJ,
	.ev_qualifiers = NONE,
	.ev_actions    = llc_reject_actions_11,
};

/* State transitions for LLC_CONN_EV_RX_RNR_CMD_Pbit_SET_0 event */
static const llc_conn_action_t llc_reject_actions_12a[] = {
	[0] = llc_conn_ac_upd_p_flag,
	[1] = llc_conn_ac_upd_nr_received,
	[2] = llc_conn_ac_set_remote_busy,
	[3] = NULL,
};

static struct llc_conn_state_trans llc_reject_state_trans_12a = {
	.ev	       = llc_conn_ev_rx_rnr_cmd_pbit_set_0,
	.next_state    = LLC_CONN_STATE_REJ,
	.ev_qualifiers = NONE,
	.ev_actions    = llc_reject_actions_12a,
};

/* State transitions for LLC_CONN_EV_RX_RNR_RSP_Fbit_SET_0 event */
static const llc_conn_action_t llc_reject_actions_12b[] = {
	[0] = llc_conn_ac_upd_p_flag,
	[1] = llc_conn_ac_upd_nr_received,
	[2] = llc_conn_ac_set_remote_busy,
	[3] = NULL,
};

static struct llc_conn_state_trans llc_reject_state_trans_12b = {
	.ev	       = llc_conn_ev_rx_rnr_rsp_fbit_set_0,
	.next_state    = LLC_CONN_STATE_REJ,
	.ev_qualifiers = NONE,
	.ev_actions    = llc_reject_actions_12b,
};

/* State transitions for LLC_CONN_EV_RX_RNR_RSP_Fbit_SET_1 event */
static const llc_conn_ev_qfyr_t llc_reject_ev_qfyrs_12c[] = {
	[0] = llc_conn_ev_qlfy_p_flag_eq_1,
	[1] = NULL,
};

static const llc_conn_action_t llc_reject_actions_12c[] = {
	[0] = llc_conn_ac_upd_p_flag,
	[1] = llc_conn_ac_upd_nr_received,
	[2] = llc_conn_ac_set_remote_busy,
	[3] = NULL,
};

static struct llc_conn_state_trans llc_reject_state_trans_12c = {
	.ev	       = llc_conn_ev_rx_rnr_rsp_fbit_set_1,
	.next_state    = LLC_CONN_STATE_REJ,
	.ev_qualifiers = llc_reject_ev_qfyrs_12c,
	.ev_actions    = llc_reject_actions_12c,
};

/* State transitions for LLC_CONN_EV_RX_RNR_CMD_Pbit_SET_1 event */
static const llc_conn_action_t llc_reject_actions_13[] = {
	[0] = llc_conn_ac_send_rr_rsp_f_set_1,
	[1] = llc_conn_ac_upd_nr_received,
	[2] = llc_conn_ac_set_remote_busy,
	[3] = NULL,
};

static struct llc_conn_state_trans llc_reject_state_trans_13 = {
	.ev	       = llc_conn_ev_rx_rnr_cmd_pbit_set_1,
	.next_state    = LLC_CONN_STATE_REJ,
	.ev_qualifiers = NONE,
	.ev_actions    = llc_reject_actions_13,
};

/* State transitions for LLC_CONN_EV_RX_REJ_CMD_Pbit_SET_0 event */
static const llc_conn_ev_qfyr_t llc_reject_ev_qfyrs_14a[] = {
	[0] = llc_conn_ev_qlfy_p_flag_eq_0,
	[1] = NULL,
};

static const llc_conn_action_t llc_reject_actions_14a[] = {
	[0] = llc_conn_ac_set_vs_nr,
	[1] = llc_conn_ac_upd_nr_received,
	[2] = llc_conn_ac_upd_p_flag,
	[3] = llc_conn_ac_resend_i_xxx_x_set_0,
	[4] = llc_conn_ac_clear_remote_busy,
	[5] = NULL,
};

static struct llc_conn_state_trans llc_reject_state_trans_14a = {
	.ev	       = llc_conn_ev_rx_rej_cmd_pbit_set_0,
	.next_state    = LLC_CONN_STATE_REJ,
	.ev_qualifiers = llc_reject_ev_qfyrs_14a,
	.ev_actions    = llc_reject_actions_14a,
};

/* State transitions for LLC_CONN_EV_RX_REJ_RSP_Fbit_SET_X event */
static const llc_conn_ev_qfyr_t llc_reject_ev_qfyrs_14b[] = {
	[0] = llc_conn_ev_qlfy_p_flag_eq_f,
	[1] = NULL,
};

static const llc_conn_action_t llc_reject_actions_14b[] = {
	[0] = llc_conn_ac_set_vs_nr,
	[1] = llc_conn_ac_upd_nr_received,
	[2] = llc_conn_ac_upd_p_flag,
	[3] = llc_conn_ac_resend_i_xxx_x_set_0,
	[4] = llc_conn_ac_clear_remote_busy,
	[5] = NULL,
};

static struct llc_conn_state_trans llc_reject_state_trans_14b = {
	.ev	       = llc_conn_ev_rx_rej_rsp_fbit_set_x,
	.next_state    = LLC_CONN_STATE_REJ,
	.ev_qualifiers = llc_reject_ev_qfyrs_14b,
	.ev_actions    = llc_reject_actions_14b,
};

/* State transitions for LLC_CONN_EV_RX_REJ_CMD_Pbit_SET_0 event */
static const llc_conn_ev_qfyr_t llc_reject_ev_qfyrs_15a[] = {
	[0] = llc_conn_ev_qlfy_p_flag_eq_1,
	[1] = NULL,
};

static const llc_conn_action_t llc_reject_actions_15a[] = {
	[0] = llc_conn_ac_set_vs_nr,
	[1] = llc_conn_ac_upd_nr_received,
	[2] = llc_conn_ac_resend_i_xxx_x_set_0,
	[3] = llc_conn_ac_clear_remote_busy,
	[4] = NULL,
};

static struct llc_conn_state_trans llc_reject_state_trans_15a = {
	.ev	       = llc_conn_ev_rx_rej_cmd_pbit_set_0,
	.next_state    = LLC_CONN_STATE_REJ,
	.ev_qualifiers = llc_reject_ev_qfyrs_15a,
	.ev_actions    = llc_reject_actions_15a,
};

/* State transitions for LLC_CONN_EV_RX_REJ_RSP_Fbit_SET_0 event */
static const llc_conn_ev_qfyr_t llc_reject_ev_qfyrs_15b[] = {
	[0] = llc_conn_ev_qlfy_p_flag_eq_1,
	[1] = NULL,
};

static const llc_conn_action_t llc_reject_actions_15b[] = {
	[0] = llc_conn_ac_set_vs_nr,
	[1] = llc_conn_ac_upd_nr_received,
	[2] = llc_conn_ac_resend_i_xxx_x_set_0,
	[3] = llc_conn_ac_clear_remote_busy,
	[4] = NULL,
};

static struct llc_conn_state_trans llc_reject_state_trans_15b = {
	.ev	       = llc_conn_ev_rx_rej_rsp_fbit_set_0,
	.next_state    = LLC_CONN_STATE_REJ,
	.ev_qualifiers = llc_reject_ev_qfyrs_15b,
	.ev_actions    = llc_reject_actions_15b,
};

/* State transitions for LLC_CONN_EV_RX_REJ_CMD_Pbit_SET_1 event */
static const llc_conn_action_t llc_reject_actions_16[] = {
	[0] = llc_conn_ac_set_vs_nr,
	[1] = llc_conn_ac_upd_nr_received,
	[2] = llc_conn_ac_resend_i_rsp_f_set_1,
	[3] = llc_conn_ac_clear_remote_busy,
	[4] = NULL,
};

static struct llc_conn_state_trans llc_reject_state_trans_16 = {
	.ev	       = llc_conn_ev_rx_rej_cmd_pbit_set_1,
	.next_state    = LLC_CONN_STATE_REJ,
	.ev_qualifiers = NONE,
	.ev_actions    = llc_reject_actions_16,
};

/* State transitions for LLC_CONN_EV_INIT_P_F_CYCLE event */
static const llc_conn_ev_qfyr_t llc_reject_ev_qfyrs_17[] = {
	[0] = llc_conn_ev_qlfy_p_flag_eq_0,
	[1] = NULL,
};

static const llc_conn_action_t llc_reject_actions_17[] = {
	[0] = llc_conn_ac_send_rr_cmd_p_set_1,
	[1] = llc_conn_ac_start_p_timer,
	[2] = NULL,
};

static struct llc_conn_state_trans llc_reject_state_trans_17 = {
	.ev	       = llc_conn_ev_init_p_f_cycle,
	.next_state    = LLC_CONN_STATE_REJ,
	.ev_qualifiers = llc_reject_ev_qfyrs_17,
	.ev_actions    = llc_reject_actions_17,
};

/* State transitions for LLC_CONN_EV_REJ_TMR_EXP event */
static const llc_conn_ev_qfyr_t llc_reject_ev_qfyrs_18[] = {
	[0] = llc_conn_ev_qlfy_p_flag_eq_0,
	[1] = llc_conn_ev_qlfy_retry_cnt_lt_n2,
	[2] = NULL,
};

static const llc_conn_action_t llc_reject_actions_18[] = {
	[0] = llc_conn_ac_send_rej_cmd_p_set_1,
	[1] = llc_conn_ac_start_p_timer,
	[2] = llc_conn_ac_start_rej_timer,
	[3] = llc_conn_ac_inc_retry_cnt_by_1,
	[4] = NULL,
};

static struct llc_conn_state_trans llc_reject_state_trans_18 = {
	.ev	       = llc_conn_ev_rej_tmr_exp,
	.next_state    = LLC_CONN_STATE_REJ,
	.ev_qualifiers = llc_reject_ev_qfyrs_18,
	.ev_actions    = llc_reject_actions_18,
};

/* State transitions for LLC_CONN_EV_P_TMR_EXP event */
static const llc_conn_ev_qfyr_t llc_reject_ev_qfyrs_19[] = {
	[0] = llc_conn_ev_qlfy_retry_cnt_lt_n2,
	[1] = NULL,
};

static const llc_conn_action_t llc_reject_actions_19[] = {
	[0] = llc_conn_ac_send_rr_cmd_p_set_1,
	[1] = llc_conn_ac_start_p_timer,
	[2] = llc_conn_ac_start_rej_timer,
	[3] = llc_conn_ac_inc_retry_cnt_by_1,
	[4] = llc_conn_ac_rst_vs,
	[5] = NULL,
};

static struct llc_conn_state_trans llc_reject_state_trans_19 = {
	.ev	       = llc_conn_ev_p_tmr_exp,
	.next_state    = LLC_CONN_STATE_AWAIT_REJ,
	.ev_qualifiers = llc_reject_ev_qfyrs_19,
	.ev_actions    = llc_reject_actions_19,
};

/* State transitions for LLC_CONN_EV_ACK_TMR_EXP event */
static const llc_conn_ev_qfyr_t llc_reject_ev_qfyrs_20a[] = {
	[0] = llc_conn_ev_qlfy_p_flag_eq_0,
	[1] = llc_conn_ev_qlfy_retry_cnt_lt_n2,
	[2] = NULL,
};

static const llc_conn_action_t llc_reject_actions_20a[] = {
	[0] = llc_conn_ac_send_rr_cmd_p_set_1,
	[1] = llc_conn_ac_start_p_timer,
	[2] = llc_conn_ac_start_rej_timer,
	[3] = llc_conn_ac_inc_retry_cnt_by_1,
	[4] = llc_conn_ac_rst_vs,
	[5] = NULL,
};

static struct llc_conn_state_trans llc_reject_state_trans_20a = {
	.ev	       = llc_conn_ev_ack_tmr_exp,
	.next_state    = LLC_CONN_STATE_AWAIT_REJ,
	.ev_qualifiers = llc_reject_ev_qfyrs_20a,
	.ev_actions    = llc_reject_actions_20a,
};

/* State transitions for LLC_CONN_EV_BUSY_TMR_EXP event */
static const llc_conn_ev_qfyr_t llc_reject_ev_qfyrs_20b[] = {
	[0] = llc_conn_ev_qlfy_p_flag_eq_0,
	[1] = llc_conn_ev_qlfy_retry_cnt_lt_n2,
	[2] = NULL,
};

static const llc_conn_action_t llc_reject_actions_20b[] = {
	[0] = llc_conn_ac_send_rr_cmd_p_set_1,
	[1] = llc_conn_ac_start_p_timer,
	[2] = llc_conn_ac_start_rej_timer,
	[3] = llc_conn_ac_inc_retry_cnt_by_1,
	[4] = llc_conn_ac_rst_vs,
	[5] = NULL,
};

static struct llc_conn_state_trans llc_reject_state_trans_20b = {
	.ev	       = llc_conn_ev_busy_tmr_exp,
	.next_state    = LLC_CONN_STATE_AWAIT_REJ,
	.ev_qualifiers = llc_reject_ev_qfyrs_20b,
	.ev_actions    = llc_reject_actions_20b,
};

/*
 * Array of pointers;
 * one to each transition
 */
static struct llc_conn_state_trans *llc_reject_state_transitions[] = {
	 [0] = &llc_common_state_trans_1,	/* Request */
	 [1] = &llc_common_state_trans_2,
	 [2] = &llc_common_state_trans_end,
	 [3] = &llc_reject_state_trans_1,
	 [4] = &llc_reject_state_trans_2,
	 [5] = &llc_reject_state_trans_2_1,
	 [6] = &llc_reject_state_trans_3,	/* Local busy */
	 [7] = &llc_reject_state_trans_4,
	 [8] = &llc_common_state_trans_end,
	 [9] = &llc_reject_state_trans_17,	/* Initiate PF cycle */
	[10] = &llc_common_state_trans_end,
	[11] = &llc_common_state_trans_11a,	/* Timer */
	[12] = &llc_common_state_trans_11b,
	[13] = &llc_common_state_trans_11c,
	[14] = &llc_common_state_trans_11d,
	[15] = &llc_reject_state_trans_18,
	[16] = &llc_reject_state_trans_19,
	[17] = &llc_reject_state_trans_20a,
	[18] = &llc_reject_state_trans_20b,
	[19] = &llc_common_state_trans_end,
	[20] = &llc_common_state_trans_3,	/* Receive frame */
	[21] = &llc_common_state_trans_4,
	[22] = &llc_common_state_trans_5,
	[23] = &llc_common_state_trans_6,
	[24] = &llc_common_state_trans_7a,
	[25] = &llc_common_state_trans_7b,
	[26] = &llc_common_state_trans_8a,
	[27] = &llc_common_state_trans_8b,
	[28] = &llc_common_state_trans_8c,
	[29] = &llc_common_state_trans_9,
	/* [30] = &llc_common_state_trans_10, */
	[30] = &llc_reject_state_trans_5a,
	[31] = &llc_reject_state_trans_5b,
	[32] = &llc_reject_state_trans_5c,
	[33] = &llc_reject_state_trans_6,
	[34] = &llc_reject_state_trans_7a,
	[35] = &llc_reject_state_trans_7b,
	[36] = &llc_reject_state_trans_8a,
	[37] = &llc_reject_state_trans_8b,
	[38] = &llc_reject_state_trans_9,
	[39] = &llc_reject_state_trans_10a,
	[40] = &llc_reject_state_trans_10b,
	[41] = &llc_reject_state_trans_10c,
	[42] = &llc_reject_state_trans_11,
	[43] = &llc_reject_state_trans_12a,
	[44] = &llc_reject_state_trans_12b,
	[45] = &llc_reject_state_trans_12c,
	[46] = &llc_reject_state_trans_13,
	[47] = &llc_reject_state_trans_14a,
	[48] = &llc_reject_state_trans_14b,
	[49] = &llc_reject_state_trans_15a,
	[50] = &llc_reject_state_trans_15b,
	[51] = &llc_reject_state_trans_16,
	[52] = &llc_common_state_trans_end,
};

/* LLC_CONN_STATE_AWAIT transitions */
/* State transitions for LLC_CONN_EV_DATA_REQ event */
static const llc_conn_ev_qfyr_t llc_await_ev_qfyrs_1_0[] = {
	[0] = llc_conn_ev_qlfy_set_status_refuse,
	[1] = NULL,
};

/* just one member, NULL, .bss zeroes it */
static const llc_conn_action_t llc_await_actions_1_0[1];

static struct llc_conn_state_trans llc_await_state_trans_1_0 = {
	.ev	       = llc_conn_ev_data_req,
	.next_state    = LLC_CONN_STATE_AWAIT,
	.ev_qualifiers = llc_await_ev_qfyrs_1_0,
	.ev_actions    = llc_await_actions_1_0,
};

/* State transitions for LLC_CONN_EV_LOCAL_BUSY_DETECTED event */
static const llc_conn_action_t llc_await_actions_1[] = {
	[0] = llc_conn_ac_send_rnr_xxx_x_set_0,
	[1] = llc_conn_ac_set_data_flag_0,
	[2] = NULL,
};

static struct llc_conn_state_trans llc_await_state_trans_1 = {
	.ev	       = llc_conn_ev_local_busy_detected,
	.next_state    = LLC_CONN_STATE_AWAIT_BUSY,
	.ev_qualifiers = NONE,
	.ev_actions    = llc_await_actions_1,
};

/* State transitions for LLC_CONN_EV_RX_I_RSP_Fbit_SET_1_UNEXPD_Ns event */
static const llc_conn_action_t llc_await_actions_2[] = {
	[0] = llc_conn_ac_send_rej_xxx_x_set_0,
	[1] = llc_conn_ac_upd_nr_received,
	[2] = llc_conn_ac_upd_vs,
	[3] = llc_conn_ac_stop_p_timer,
	[4] = llc_conn_ac_resend_i_xxx_x_set_0,
	[5] = llc_conn_ac_start_rej_timer,
	[6] = llc_conn_ac_clear_remote_busy,
	[7] = NULL,
};

static struct llc_conn_state_trans llc_await_state_trans_2 = {
	.ev	       = llc_conn_ev_rx_i_rsp_fbit_set_1_unexpd_ns,
	.next_state    = LLC_CONN_STATE_REJ,
	.ev_qualifiers = NONE,
	.ev_actions    = llc_await_actions_2,
};

/* State transitions for LLC_CONN_EV_RX_I_CMD_Pbit_SET_0_UNEXPD_Ns event */
static const llc_conn_action_t llc_await_actions_3a[] = {
	[0] = llc_conn_ac_send_rej_xxx_x_set_0,
	[1] = llc_conn_ac_upd_nr_received,
	[2] = llc_conn_ac_upd_vs,
	[3] = llc_conn_ac_start_rej_timer,
	[4] = NULL,
};

static struct llc_conn_state_trans llc_await_state_trans_3a = {
	.ev	       = llc_conn_ev_rx_i_cmd_pbit_set_0_unexpd_ns,
	.next_state    = LLC_CONN_STATE_AWAIT_REJ,
	.ev_qualifiers = NONE,
	.ev_actions    = llc_await_actions_3a,
};

/* State transitions for LLC_CONN_EV_RX_I_RSP_Fbit_SET_0_UNEXPD_Ns event */
static const llc_conn_action_t llc_await_actions_3b[] = {
	[0] = llc_conn_ac_send_rej_xxx_x_set_0,
	[1] = llc_conn_ac_upd_nr_received,
	[2] = llc_conn_ac_upd_vs,
	[3] = llc_conn_ac_start_rej_timer,
	[4] = NULL,
};

static struct llc_conn_state_trans llc_await_state_trans_3b = {
	.ev	       = llc_conn_ev_rx_i_rsp_fbit_set_0_unexpd_ns,
	.next_state    = LLC_CONN_STATE_AWAIT_REJ,
	.ev_qualifiers = NONE,
	.ev_actions    = llc_await_actions_3b,
};

/* State transitions for LLC_CONN_EV_RX_I_CMD_Pbit_SET_1_UNEXPD_Ns event */
static const llc_conn_action_t llc_await_actions_4[] = {
	[0] = llc_conn_ac_send_rej_rsp_f_set_1,
	[1] = llc_conn_ac_upd_nr_received,
	[2] = llc_conn_ac_upd_vs,
	[3] = llc_conn_ac_start_rej_timer,
	[4] = llc_conn_ac_start_p_timer,
	[5] = NULL,
};

static struct llc_conn_state_trans llc_await_state_trans_4 = {
	.ev	       = llc_conn_ev_rx_i_cmd_pbit_set_1_unexpd_ns,
	.next_state    = LLC_CONN_STATE_AWAIT_REJ,
	.ev_qualifiers = NONE,
	.ev_actions    = llc_await_actions_4,
};

/* State transitions for LLC_CONN_EV_RX_I_RSP_Fbit_SET_1 event */
static const llc_conn_action_t llc_await_actions_5[] = {
	[0] = llc_conn_ac_inc_vr_by_1,
	[1] = llc_conn_ac_data_ind,
	[2] = llc_conn_ac_stop_p_timer,
	[3] = llc_conn_ac_upd_nr_received,
	[4] = llc_conn_ac_upd_vs,
	[5] = llc_conn_ac_resend_i_xxx_x_set_0_or_send_rr,
	[6] = llc_conn_ac_clear_remote_busy,
	[7] = NULL,
};

static struct llc_conn_state_trans llc_await_state_trans_5 = {
	.ev	       = llc_conn_ev_rx_i_rsp_fbit_set_1,
	.next_state    = LLC_CONN_STATE_NORMAL,
	.ev_qualifiers = NONE,
	.ev_actions    = llc_await_actions_5,
};

/* State transitions for LLC_CONN_EV_RX_I_RSP_Fbit_SET_0 event */
static const llc_conn_action_t llc_await_actions_6a[] = {
	[0] = llc_conn_ac_inc_vr_by_1,
	[1] = llc_conn_ac_data_ind,
	[2] = llc_conn_ac_send_rr_xxx_x_set_0,
	[3] = llc_conn_ac_upd_nr_received,
	[4] = llc_conn_ac_upd_vs,
	[5] = NULL,
};

static struct llc_conn_state_trans llc_await_state_trans_6a = {
	.ev	       = llc_conn_ev_rx_i_rsp_fbit_set_0,
	.next_state    = LLC_CONN_STATE_AWAIT,
	.ev_qualifiers = NONE,
	.ev_actions    = llc_await_actions_6a,
};

/* State transitions for LLC_CONN_EV_RX_I_CMD_Pbit_SET_0 event */
static const llc_conn_action_t llc_await_actions_6b[] = {
	[0] = llc_conn_ac_inc_vr_by_1,
	[1] = llc_conn_ac_data_ind,
	[2] = llc_conn_ac_send_rr_xxx_x_set_0,
	[3] = llc_conn_ac_upd_nr_received,
	[4] = llc_conn_ac_upd_vs,
	[5] = NULL,
};

static struct llc_conn_state_trans llc_await_state_trans_6b = {
	.ev	       = llc_conn_ev_rx_i_cmd_pbit_set_0,
	.next_state    = LLC_CONN_STATE_AWAIT,
	.ev_qualifiers = NONE,
	.ev_actions    = llc_await_actions_6b,
};

/* State transitions for LLC_CONN_EV_RX_I_CMD_Pbit_SET_1 event */
static const llc_conn_action_t llc_await_actions_7[] = {
	[0] = llc_conn_ac_inc_vr_by_1,
	[1] = llc_conn_ac_data_ind,
	[2] = llc_conn_ac_send_rr_rsp_f_set_1,
	[3] = llc_conn_ac_upd_nr_received,
	[4] = llc_conn_ac_upd_vs,
	[5] = NULL,
};

static struct llc_conn_state_trans llc_await_state_trans_7 = {
	.ev	       = llc_conn_ev_rx_i_cmd_pbit_set_1,
	.next_state    = LLC_CONN_STATE_AWAIT,
	.ev_qualifiers = NONE,
	.ev_actions    = llc_await_actions_7,
};

/* State transitions for LLC_CONN_EV_RX_RR_RSP_Fbit_SET_1 event */
static const llc_conn_action_t llc_await_actions_8a[] = {
	[0] = llc_conn_ac_upd_nr_received,
	[1] = llc_conn_ac_upd_vs,
	[2] = llc_conn_ac_stop_p_timer,
	[3] = llc_conn_ac_resend_i_xxx_x_set_0,
	[4] = llc_conn_ac_clear_remote_busy,
	[5] = NULL,
};

static struct llc_conn_state_trans llc_await_state_trans_8a = {
	.ev	       = llc_conn_ev_rx_rr_rsp_fbit_set_1,
	.next_state    = LLC_CONN_STATE_NORMAL,
	.ev_qualifiers = NONE,
	.ev_actions    = llc_await_actions_8a,
};

/* State transitions for LLC_CONN_EV_RX_REJ_RSP_Fbit_SET_1 event */
static const llc_conn_action_t llc_await_actions_8b[] = {
	[0] = llc_conn_ac_upd_nr_received,
	[1] = llc_conn_ac_upd_vs,
	[2] = llc_conn_ac_stop_p_timer,
	[3] = llc_conn_ac_resend_i_xxx_x_set_0,
	[4] = llc_conn_ac_clear_remote_busy,
	[5] = NULL,
};

static struct llc_conn_state_trans llc_await_state_trans_8b = {
	.ev	       = llc_conn_ev_rx_rej_rsp_fbit_set_1,
	.next_state    = LLC_CONN_STATE_NORMAL,
	.ev_qualifiers = NONE,
	.ev_actions    = llc_await_actions_8b,
};

/* State transitions for LLC_CONN_EV_RX_RR_CMD_Pbit_SET_0 event */
static const llc_conn_action_t llc_await_actions_9a[] = {
	[0] = llc_conn_ac_upd_nr_received,
	[1] = llc_conn_ac_upd_vs,
	[2] = llc_conn_ac_clear_remote_busy,
	[3] = NULL,
};

static struct llc_conn_state_trans llc_await_state_trans_9a = {
	.ev	       = llc_conn_ev_rx_rr_cmd_pbit_set_0,
	.next_state    = LLC_CONN_STATE_AWAIT,
	.ev_qualifiers = NONE,
	.ev_actions    = llc_await_actions_9a,
};

/* State transitions for LLC_CONN_EV_RX_RR_RSP_Fbit_SET_0 event */
static const llc_conn_action_t llc_await_actions_9b[] = {
	[0] = llc_conn_ac_upd_nr_received,
	[1] = llc_conn_ac_upd_vs,
	[2] = llc_conn_ac_clear_remote_busy,
	[3] = NULL,
};

static struct llc_conn_state_trans llc_await_state_trans_9b = {
	.ev	       = llc_conn_ev_rx_rr_rsp_fbit_set_0,
	.next_state    = LLC_CONN_STATE_AWAIT,
	.ev_qualifiers = NONE,
	.ev_actions    = llc_await_actions_9b,
};

/* State transitions for LLC_CONN_EV_RX_REJ_CMD_Pbit_SET_0 event */
static const llc_conn_action_t llc_await_actions_9c[] = {
	[0] = llc_conn_ac_upd_nr_received,
	[1] = llc_conn_ac_upd_vs,
	[2] = llc_conn_ac_clear_remote_busy,
	[3] = NULL,
};

static struct llc_conn_state_trans llc_await_state_trans_9c = {
	.ev	       = llc_conn_ev_rx_rej_cmd_pbit_set_0,
	.next_state    = LLC_CONN_STATE_AWAIT,
	.ev_qualifiers = NONE,
	.ev_actions    = llc_await_actions_9c,
};

/* State transitions for LLC_CONN_EV_RX_REJ_RSP_Fbit_SET_0 event */
static const llc_conn_action_t llc_await_actions_9d[] = {
	[0] = llc_conn_ac_upd_nr_received,
	[1] = llc_conn_ac_upd_vs,
	[2] = llc_conn_ac_clear_remote_busy,
	[3] = NULL,
};

static struct llc_conn_state_trans llc_await_state_trans_9d = {
	.ev	       = llc_conn_ev_rx_rej_rsp_fbit_set_0,
	.next_state    = LLC_CONN_STATE_AWAIT,
	.ev_qualifiers = NONE,
	.ev_actions    = llc_await_actions_9d,
};

/* State transitions for LLC_CONN_EV_RX_RR_CMD_Pbit_SET_1 event */
static const llc_conn_action_t llc_await_actions_10a[] = {
	[0] = llc_conn_ac_send_rr_rsp_f_set_1,
	[1] = llc_conn_ac_upd_nr_received,
	[2] = llc_conn_ac_upd_vs,
	[3] = llc_conn_ac_clear_remote_busy,
	[4] = NULL,
};

static struct llc_conn_state_trans llc_await_state_trans_10a = {
	.ev	       = llc_conn_ev_rx_rr_cmd_pbit_set_1,
	.next_state    = LLC_CONN_STATE_AWAIT,
	.ev_qualifiers = NONE,
	.ev_actions    = llc_await_actions_10a,
};

/* State transitions for LLC_CONN_EV_RX_REJ_CMD_Pbit_SET_1 event */
static const llc_conn_action_t llc_await_actions_10b[] = {
	[0] = llc_conn_ac_send_rr_rsp_f_set_1,
	[1] = llc_conn_ac_upd_nr_received,
	[2] = llc_conn_ac_upd_vs,
	[3] = llc_conn_ac_clear_remote_busy,
	[4] = NULL,
};

static struct llc_conn_state_trans llc_await_state_trans_10b = {
	.ev	       = llc_conn_ev_rx_rej_cmd_pbit_set_1,
	.next_state    = LLC_CONN_STATE_AWAIT,
	.ev_qualifiers = NONE,
	.ev_actions    = llc_await_actions_10b,
};

/* State transitions for LLC_CONN_EV_RX_RNR_RSP_Fbit_SET_1 event */
static const llc_conn_action_t llc_await_actions_11[] = {
	[0] = llc_conn_ac_upd_nr_received,
	[1] = llc_conn_ac_upd_vs,
	[2] = llc_conn_ac_stop_p_timer,
	[3] = llc_conn_ac_set_remote_busy,
	[4] = NULL,
};

static struct llc_conn_state_trans llc_await_state_trans_11 = {
	.ev	       = llc_conn_ev_rx_rnr_rsp_fbit_set_1,
	.next_state    = LLC_CONN_STATE_NORMAL,
	.ev_qualifiers = NONE,
	.ev_actions    = llc_await_actions_11,
};

/* State transitions for LLC_CONN_EV_RX_RNR_CMD_Pbit_SET_0 event */
static const llc_conn_action_t llc_await_actions_12a[] = {
	[0] = llc_conn_ac_upd_nr_received,
	[1] = llc_conn_ac_upd_vs,
	[2] = llc_conn_ac_set_remote_busy,
	[3] = NULL,
};

static struct llc_conn_state_trans llc_await_state_trans_12a = {
	.ev	       = llc_conn_ev_rx_rnr_cmd_pbit_set_0,
	.next_state    = LLC_CONN_STATE_AWAIT,
	.ev_qualifiers = NONE,
	.ev_actions    = llc_await_actions_12a,
};

/* State transitions for LLC_CONN_EV_RX_RNR_RSP_Fbit_SET_0 event */
static const llc_conn_action_t llc_await_actions_12b[] = {
	[0] = llc_conn_ac_upd_nr_received,
	[1] = llc_conn_ac_upd_vs,
	[2] = llc_conn_ac_set_remote_busy,
	[3] = NULL,
};

static struct llc_conn_state_trans llc_await_state_trans_12b = {
	.ev	       = llc_conn_ev_rx_rnr_rsp_fbit_set_0,
	.next_state    = LLC_CONN_STATE_AWAIT,
	.ev_qualifiers = NONE,
	.ev_actions    = llc_await_actions_12b,
};

/* State transitions for LLC_CONN_EV_RX_RNR_CMD_Pbit_SET_1 event */
static const llc_conn_action_t llc_await_actions_13[] = {
	[0] = llc_conn_ac_send_rr_rsp_f_set_1,
	[1] = llc_conn_ac_upd_nr_received,
	[2] = llc_conn_ac_upd_vs,
	[3] = llc_conn_ac_set_remote_busy,
	[4] = NULL,
};

static struct llc_conn_state_trans llc_await_state_trans_13 = {
	.ev	       = llc_conn_ev_rx_rnr_cmd_pbit_set_1,
	.next_state    = LLC_CONN_STATE_AWAIT,
	.ev_qualifiers = NONE,
	.ev_actions    = llc_await_actions_13,
};

/* State transitions for LLC_CONN_EV_P_TMR_EXP event */
static const llc_conn_ev_qfyr_t llc_await_ev_qfyrs_14[] = {
	[0] = llc_conn_ev_qlfy_retry_cnt_lt_n2,
	[1] = NULL,
};

static const llc_conn_action_t llc_await_actions_14[] = {
	[0] = llc_conn_ac_send_rr_cmd_p_set_1,
	[1] = llc_conn_ac_start_p_timer,
	[2] = llc_conn_ac_inc_retry_cnt_by_1,
	[3] = NULL,
};

static struct llc_conn_state_trans llc_await_state_trans_14 = {
	.ev	       = llc_conn_ev_p_tmr_exp,
	.next_state    = LLC_CONN_STATE_AWAIT,
	.ev_qualifiers = llc_await_ev_qfyrs_14,
	.ev_actions    = llc_await_actions_14,
};

/*
 * Array of pointers;
 * one to each transition
 */
static struct llc_conn_state_trans *llc_await_state_transitions[] = {
	 [0] = &llc_common_state_trans_1,	/* Request */
	 [1] = &llc_common_state_trans_2,
	 [2] = &llc_await_state_trans_1_0,
	 [3] = &llc_common_state_trans_end,
	 [4] = &llc_await_state_trans_1,	/* Local busy */
	 [5] = &llc_common_state_trans_end,
	 [6] = &llc_common_state_trans_end,	/* Initiate PF Cycle */
	 [7] = &llc_common_state_trans_11a,	/* Timer */
	 [8] = &llc_common_state_trans_11b,
	 [9] = &llc_common_state_trans_11c,
	[10] = &llc_common_state_trans_11d,
	[11] = &llc_await_state_trans_14,
	[12] = &llc_common_state_trans_end,
	[13] = &llc_common_state_trans_3,	/* Receive frame */
	[14] = &llc_common_state_trans_4,
	[15] = &llc_common_state_trans_5,
	[16] = &llc_common_state_trans_6,
	[17] = &llc_common_state_trans_7a,
	[18] = &llc_common_state_trans_7b,
	[19] = &llc_common_state_trans_8a,
	[20] = &llc_common_state_trans_8b,
	[21] = &llc_common_state_trans_8c,
	[22] = &llc_common_state_trans_9,
	/* [23] = &llc_common_state_trans_10, */
	[23] = &llc_await_state_trans_2,
	[24] = &llc_await_state_trans_3a,
	[25] = &llc_await_state_trans_3b,
	[26] = &llc_await_state_trans_4,
	[27] = &llc_await_state_trans_5,
	[28] = &llc_await_state_trans_6a,
	[29] = &llc_await_state_trans_6b,
	[30] = &llc_await_state_trans_7,
	[31] = &llc_await_state_trans_8a,
	[32] = &llc_await_state_trans_8b,
	[33] = &llc_await_state_trans_9a,
	[34] = &llc_await_state_trans_9b,
	[35] = &llc_await_state_trans_9c,
	[36] = &llc_await_state_trans_9d,
	[37] = &llc_await_state_trans_10a,
	[38] = &llc_await_state_trans_10b,
	[39] = &llc_await_state_trans_11,
	[40] = &llc_await_state_trans_12a,
	[41] = &llc_await_state_trans_12b,
	[42] = &llc_await_state_trans_13,
	[43] = &llc_common_state_trans_end,
};

/* LLC_CONN_STATE_AWAIT_BUSY transitions */
/* State transitions for LLC_CONN_EV_DATA_CONN_REQ event */
static const llc_conn_ev_qfyr_t llc_await_busy_ev_qfyrs_1_0[] = {
	[0] = llc_conn_ev_qlfy_set_status_refuse,
	[1] = NULL,
};

/* just one member, NULL, .bss zeroes it */
static const llc_conn_action_t llc_await_busy_actions_1_0[1];

static struct llc_conn_state_trans llc_await_busy_state_trans_1_0 = {
	.ev	       = llc_conn_ev_data_req,
	.next_state    = LLC_CONN_STATE_AWAIT_BUSY,
	.ev_qualifiers = llc_await_busy_ev_qfyrs_1_0,
	.ev_actions    = llc_await_busy_actions_1_0,
};

/* State transitions for LLC_CONN_EV_LOCAL_BUSY_CLEARED event */
static const llc_conn_ev_qfyr_t llc_await_busy_ev_qfyrs_1[] = {
	[0] = llc_conn_ev_qlfy_data_flag_eq_1,
	[1] = NULL,
};

static const llc_conn_action_t llc_await_busy_actions_1[] = {
	[0] = llc_conn_ac_send_rej_xxx_x_set_0,
	[1] = llc_conn_ac_start_rej_timer,
	[2] = NULL,
};

static struct llc_conn_state_trans llc_await_busy_state_trans_1 = {
	.ev	       = llc_conn_ev_local_busy_cleared,
	.next_state    = LLC_CONN_STATE_AWAIT_REJ,
	.ev_qualifiers = llc_await_busy_ev_qfyrs_1,
	.ev_actions    = llc_await_busy_actions_1,
};

/* State transitions for LLC_CONN_EV_LOCAL_BUSY_CLEARED event */
static const llc_conn_ev_qfyr_t llc_await_busy_ev_qfyrs_2[] = {
	[0] = llc_conn_ev_qlfy_data_flag_eq_0,
	[1] = NULL,
};

static const llc_conn_action_t llc_await_busy_actions_2[] = {
	[0] = llc_conn_ac_send_rr_xxx_x_set_0,
	[1] = NULL,
};

static struct llc_conn_state_trans llc_await_busy_state_trans_2 = {
	.ev	       = llc_conn_ev_local_busy_cleared,
	.next_state    = LLC_CONN_STATE_AWAIT,
	.ev_qualifiers = llc_await_busy_ev_qfyrs_2,
	.ev_actions    = llc_await_busy_actions_2,
};

/* State transitions for LLC_CONN_EV_LOCAL_BUSY_CLEARED event */
static const llc_conn_ev_qfyr_t llc_await_busy_ev_qfyrs_3[] = {
	[0] = llc_conn_ev_qlfy_data_flag_eq_2,
	[1] = NULL,
};

static const llc_conn_action_t llc_await_busy_actions_3[] = {
	[0] = llc_conn_ac_send_rr_xxx_x_set_0,
	[1] = NULL,
};

static struct llc_conn_state_trans llc_await_busy_state_trans_3 = {
	.ev	       = llc_conn_ev_local_busy_cleared,
	.next_state    = LLC_CONN_STATE_AWAIT_REJ,
	.ev_qualifiers = llc_await_busy_ev_qfyrs_3,
	.ev_actions    = llc_await_busy_actions_3,
};

/* State transitions for LLC_CONN_EV_RX_I_RSP_Fbit_SET_1_UNEXPD_Ns event */
static const llc_conn_action_t llc_await_busy_actions_4[] = {
	[0] = llc_conn_ac_opt_send_rnr_xxx_x_set_0,
	[1] = llc_conn_ac_upd_nr_received,
	[2] = llc_conn_ac_upd_vs,
	[3] = llc_conn_ac_stop_p_timer,
	[4] = llc_conn_ac_set_data_flag_1,
	[5] = llc_conn_ac_clear_remote_busy,
	[6] = llc_conn_ac_resend_i_xxx_x_set_0,
	[7] = NULL,
};

static struct llc_conn_state_trans llc_await_busy_state_trans_4 = {
	.ev	       = llc_conn_ev_rx_i_rsp_fbit_set_1_unexpd_ns,
	.next_state    = LLC_CONN_STATE_BUSY,
	.ev_qualifiers = NONE,
	.ev_actions    = llc_await_busy_actions_4,
};

/* State transitions for LLC_CONN_EV_RX_I_CMD_Pbit_SET_0_UNEXPD_Ns event */
static const llc_conn_action_t llc_await_busy_actions_5a[] = {
	[0] = llc_conn_ac_opt_send_rnr_xxx_x_set_0,
	[1] = llc_conn_ac_upd_nr_received,
	[2] = llc_conn_ac_upd_vs,
	[3] = llc_conn_ac_set_data_flag_1,
	[4] = NULL,
};

static struct llc_conn_state_trans llc_await_busy_state_trans_5a = {
	.ev	       = llc_conn_ev_rx_i_cmd_pbit_set_0_unexpd_ns,
	.next_state    = LLC_CONN_STATE_AWAIT_BUSY,
	.ev_qualifiers = NONE,
	.ev_actions    = llc_await_busy_actions_5a,
};

/* State transitions for LLC_CONN_EV_RX_I_RSP_Fbit_SET_0_UNEXPD_Ns event */
static const llc_conn_action_t llc_await_busy_actions_5b[] = {
	[0] = llc_conn_ac_opt_send_rnr_xxx_x_set_0,
	[1] = llc_conn_ac_upd_nr_received,
	[2] = llc_conn_ac_upd_vs,
	[3] = llc_conn_ac_set_data_flag_1,
	[4] = NULL,
};

static struct llc_conn_state_trans llc_await_busy_state_trans_5b = {
	.ev	       = llc_conn_ev_rx_i_rsp_fbit_set_0_unexpd_ns,
	.next_state    = LLC_CONN_STATE_AWAIT_BUSY,
	.ev_qualifiers = NONE,
	.ev_actions    = llc_await_busy_actions_5b,
};

/* State transitions for LLC_CONN_EV_RX_I_CMD_Pbit_SET_1_UNEXPD_Ns event */
static const llc_conn_action_t llc_await_busy_actions_6[] = {
	[0] = llc_conn_ac_send_rnr_rsp_f_set_1,
	[1] = llc_conn_ac_upd_nr_received,
	[2] = llc_conn_ac_upd_vs,
	[3] = llc_conn_ac_set_data_flag_1,
	[4] = NULL,
};

static struct llc_conn_state_trans llc_await_busy_state_trans_6 = {
	.ev	       = llc_conn_ev_rx_i_cmd_pbit_set_1_unexpd_ns,
	.next_state    = LLC_CONN_STATE_AWAIT_BUSY,
	.ev_qualifiers = NONE,
	.ev_actions    = llc_await_busy_actions_6,
};

/* State transitions for LLC_CONN_EV_RX_I_RSP_Fbit_SET_1 event */
static const llc_conn_action_t llc_await_busy_actions_7[] = {
	[0] = llc_conn_ac_opt_send_rnr_xxx_x_set_0,
	[1] = llc_conn_ac_inc_vr_by_1,
	[2] = llc_conn_ac_data_ind,
	[3] = llc_conn_ac_stop_p_timer,
	[4] = llc_conn_ac_upd_nr_received,
	[5] = llc_conn_ac_upd_vs,
	[6] = llc_conn_ac_set_data_flag_0,
	[7] = llc_conn_ac_clear_remote_busy,
	[8] = llc_conn_ac_resend_i_xxx_x_set_0,
	[9] = NULL,
};

static struct llc_conn_state_trans llc_await_busy_state_trans_7 = {
	.ev	       = llc_conn_ev_rx_i_rsp_fbit_set_1,
	.next_state    = LLC_CONN_STATE_BUSY,
	.ev_qualifiers = NONE,
	.ev_actions    = llc_await_busy_actions_7,
};

/* State transitions for LLC_CONN_EV_RX_I_RSP_Fbit_SET_0 event */
static const llc_conn_action_t llc_await_busy_actions_8a[] = {
	[0] = llc_conn_ac_opt_send_rnr_xxx_x_set_0,
	[1] = llc_conn_ac_inc_vr_by_1,
	[2] = llc_conn_ac_data_ind,
	[3] = llc_conn_ac_upd_nr_received,
	[4] = llc_conn_ac_upd_vs,
	[5] = llc_conn_ac_set_data_flag_0,
	[6] = NULL,
};

static struct llc_conn_state_trans llc_await_busy_state_trans_8a = {
	.ev	       = llc_conn_ev_rx_i_rsp_fbit_set_0,
	.next_state    = LLC_CONN_STATE_AWAIT_BUSY,
	.ev_qualifiers = NONE,
	.ev_actions    = llc_await_busy_actions_8a,
};

/* State transitions for LLC_CONN_EV_RX_I_CMD_Pbit_SET_0 event */
static const llc_conn_action_t llc_await_busy_actions_8b[] = {
	[0] = llc_conn_ac_opt_send_rnr_xxx_x_set_0,
	[1] = llc_conn_ac_inc_vr_by_1,
	[2] = llc_conn_ac_data_ind,
	[3] = llc_conn_ac_upd_nr_received,
	[4] = llc_conn_ac_upd_vs,
	[5] = llc_conn_ac_set_data_flag_0,
	[6] = NULL,
};

static struct llc_conn_state_trans llc_await_busy_state_trans_8b = {
	.ev	       = llc_conn_ev_rx_i_cmd_pbit_set_0,
	.next_state    = LLC_CONN_STATE_AWAIT_BUSY,
	.ev_qualifiers = NONE,
	.ev_actions    = llc_await_busy_actions_8b,
};

/* State transitions for LLC_CONN_EV_RX_I_CMD_Pbit_SET_1 event */
static const llc_conn_action_t llc_await_busy_actions_9[] = {
	[0] = llc_conn_ac_send_rnr_rsp_f_set_1,
	[1] = llc_conn_ac_inc_vr_by_1,
	[2] = llc_conn_ac_data_ind,
	[3] = llc_conn_ac_upd_nr_received,
	[4] = llc_conn_ac_upd_vs,
	[5] = llc_conn_ac_set_data_flag_0,
	[6] = NULL,
};

static struct llc_conn_state_trans llc_await_busy_state_trans_9 = {
	.ev	       = llc_conn_ev_rx_i_cmd_pbit_set_1,
	.next_state    = LLC_CONN_STATE_AWAIT_BUSY,
	.ev_qualifiers = NONE,
	.ev_actions    = llc_await_busy_actions_9,
};

/* State transitions for LLC_CONN_EV_RX_RR_RSP_Fbit_SET_1 event */
static const llc_conn_action_t llc_await_busy_actions_10a[] = {
	[0] = llc_conn_ac_upd_nr_received,
	[1] = llc_conn_ac_upd_vs,
	[2] = llc_conn_ac_stop_p_timer,
	[3] = llc_conn_ac_resend_i_xxx_x_set_0,
	[4] = llc_conn_ac_clear_remote_busy,
	[5] = NULL,
};

static struct llc_conn_state_trans llc_await_busy_state_trans_10a = {
	.ev	       = llc_conn_ev_rx_rr_rsp_fbit_set_1,
	.next_state    = LLC_CONN_STATE_BUSY,
	.ev_qualifiers = NONE,
	.ev_actions    = llc_await_busy_actions_10a,
};

/* State transitions for LLC_CONN_EV_RX_REJ_RSP_Fbit_SET_1 event */
static const llc_conn_action_t llc_await_busy_actions_10b[] = {
	[0] = llc_conn_ac_upd_nr_received,
	[1] = llc_conn_ac_upd_vs,
	[2] = llc_conn_ac_stop_p_timer,
	[3] = llc_conn_ac_resend_i_xxx_x_set_0,
	[4] = llc_conn_ac_clear_remote_busy,
	[5] = NULL,
};

static struct llc_conn_state_trans llc_await_busy_state_trans_10b = {
	.ev	       = llc_conn_ev_rx_rej_rsp_fbit_set_1,
	.next_state    = LLC_CONN_STATE_BUSY,
	.ev_qualifiers = NONE,
	.ev_actions    = llc_await_busy_actions_10b,
};

/* State transitions for LLC_CONN_EV_RX_RR_CMD_Pbit_SET_0 event */
static const llc_conn_action_t llc_await_busy_actions_11a[] = {
	[0] = llc_conn_ac_upd_nr_received,
	[1] = llc_conn_ac_upd_vs,
	[2] = llc_conn_ac_clear_remote_busy,
	[3] = NULL,
};

static struct llc_conn_state_trans llc_await_busy_state_trans_11a = {
	.ev	       = llc_conn_ev_rx_rr_cmd_pbit_set_0,
	.next_state    = LLC_CONN_STATE_AWAIT_BUSY,
	.ev_qualifiers = NONE,
	.ev_actions    = llc_await_busy_actions_11a,
};

/* State transitions for LLC_CONN_EV_RX_RR_RSP_Fbit_SET_0 event */
static const llc_conn_action_t llc_await_busy_actions_11b[] = {
	[0] = llc_conn_ac_upd_nr_received,
	[1] = llc_conn_ac_upd_vs,
	[2] = llc_conn_ac_clear_remote_busy,
	[3] = NULL,
};

static struct llc_conn_state_trans llc_await_busy_state_trans_11b = {
	.ev	       = llc_conn_ev_rx_rr_rsp_fbit_set_0,
	.next_state    = LLC_CONN_STATE_AWAIT_BUSY,
	.ev_qualifiers = NONE,
	.ev_actions    = llc_await_busy_actions_11b,
};

/* State transitions for LLC_CONN_EV_RX_REJ_CMD_Pbit_SET_0 event */
static const llc_conn_action_t llc_await_busy_actions_11c[] = {
	[0] = llc_conn_ac_upd_nr_received,
	[1] = llc_conn_ac_upd_vs,
	[2] = llc_conn_ac_clear_remote_busy,
	[3] = NULL,
};

static struct llc_conn_state_trans llc_await_busy_state_trans_11c = {
	.ev	       = llc_conn_ev_rx_rej_cmd_pbit_set_0,
	.next_state    = LLC_CONN_STATE_AWAIT_BUSY,
	.ev_qualifiers = NONE,
	.ev_actions    = llc_await_busy_actions_11c,
};

/* State transitions for LLC_CONN_EV_RX_REJ_RSP_Fbit_SET_0 event */
static const llc_conn_action_t llc_await_busy_actions_11d[] = {
	[0] = llc_conn_ac_upd_nr_received,
	[1] = llc_conn_ac_upd_vs,
	[2] = llc_conn_ac_clear_remote_busy,
	[3] = NULL,
};

static struct llc_conn_state_trans llc_await_busy_state_trans_11d = {
	.ev	       = llc_conn_ev_rx_rej_rsp_fbit_set_0,
	.next_state    = LLC_CONN_STATE_AWAIT_BUSY,
	.ev_qualifiers = NONE,
	.ev_actions    = llc_await_busy_actions_11d,
};

/* State transitions for LLC_CONN_EV_RX_RR_CMD_Pbit_SET_1 event */
static const llc_conn_action_t llc_await_busy_actions_12a[] = {
	[0] = llc_conn_ac_send_rnr_rsp_f_set_1,
	[1] = llc_conn_ac_upd_nr_received,
	[2] = llc_conn_ac_upd_vs,
	[3] = llc_conn_ac_clear_remote_busy,
	[4] = NULL,
};

static struct llc_conn_state_trans llc_await_busy_state_trans_12a = {
	.ev	       = llc_conn_ev_rx_rr_cmd_pbit_set_1,
	.next_state    = LLC_CONN_STATE_AWAIT_BUSY,
	.ev_qualifiers = NONE,
	.ev_actions    = llc_await_busy_actions_12a,
};

/* State transitions for LLC_CONN_EV_RX_REJ_CMD_Pbit_SET_1 event */
static const llc_conn_action_t llc_await_busy_actions_12b[] = {
	[0] = llc_conn_ac_send_rnr_rsp_f_set_1,
	[1] = llc_conn_ac_upd_nr_received,
	[2] = llc_conn_ac_upd_vs,
	[3] = llc_conn_ac_clear_remote_busy,
	[4] = NULL,
};

static struct llc_conn_state_trans llc_await_busy_state_trans_12b = {
	.ev	       = llc_conn_ev_rx_rej_cmd_pbit_set_1,
	.next_state    = LLC_CONN_STATE_AWAIT_BUSY,
	.ev_qualifiers = NONE,
	.ev_actions    = llc_await_busy_actions_12b,
};

/* State transitions for LLC_CONN_EV_RX_RNR_RSP_Fbit_SET_1 event */
static const llc_conn_action_t llc_await_busy_actions_13[] = {
	[0] = llc_conn_ac_upd_nr_received,
	[1] = llc_conn_ac_upd_vs,
	[2] = llc_conn_ac_stop_p_timer,
	[3] = llc_conn_ac_set_remote_busy,
	[4] = NULL,
};

static struct llc_conn_state_trans llc_await_busy_state_trans_13 = {
	.ev	       = llc_conn_ev_rx_rnr_rsp_fbit_set_1,
	.next_state    = LLC_CONN_STATE_BUSY,
	.ev_qualifiers = NONE,
	.ev_actions    = llc_await_busy_actions_13,
};

/* State transitions for LLC_CONN_EV_RX_RNR_CMD_Pbit_SET_0 event */
static const llc_conn_action_t llc_await_busy_actions_14a[] = {
	[0] = llc_conn_ac_upd_nr_received,
	[1] = llc_conn_ac_upd_vs,
	[2] = llc_conn_ac_set_remote_busy,
	[3] = NULL,
};

static struct llc_conn_state_trans llc_await_busy_state_trans_14a = {
	.ev	       = llc_conn_ev_rx_rnr_cmd_pbit_set_0,
	.next_state    = LLC_CONN_STATE_AWAIT_BUSY,
	.ev_qualifiers = NONE,
	.ev_actions    = llc_await_busy_actions_14a,
};

/* State transitions for LLC_CONN_EV_RX_RNR_RSP_Fbit_SET_0 event */
static const llc_conn_action_t llc_await_busy_actions_14b[] = {
	[0] = llc_conn_ac_upd_nr_received,
	[1] = llc_conn_ac_upd_vs,
	[2] = llc_conn_ac_set_remote_busy,
	[3] = NULL,
};

static struct llc_conn_state_trans llc_await_busy_state_trans_14b = {
	.ev	       = llc_conn_ev_rx_rnr_rsp_fbit_set_0,
	.next_state    = LLC_CONN_STATE_AWAIT_BUSY,
	.ev_qualifiers = NONE,
	.ev_actions    = llc_await_busy_actions_14b,
};

/* State transitions for LLC_CONN_EV_RX_RNR_CMD_Pbit_SET_1 event */
static const llc_conn_action_t llc_await_busy_actions_15[] = {
	[0] = llc_conn_ac_send_rnr_rsp_f_set_1,
	[1] = llc_conn_ac_upd_nr_received,
	[2] = llc_conn_ac_upd_vs,
	[3] = llc_conn_ac_set_remote_busy,
	[4] = NULL,
};

static struct llc_conn_state_trans llc_await_busy_state_trans_15 = {
	.ev	       = llc_conn_ev_rx_rnr_cmd_pbit_set_1,
	.next_state    = LLC_CONN_STATE_AWAIT_BUSY,
	.ev_qualifiers = NONE,
	.ev_actions    = llc_await_busy_actions_15,
};

/* State transitions for LLC_CONN_EV_P_TMR_EXP event */
static const llc_conn_ev_qfyr_t llc_await_busy_ev_qfyrs_16[] = {
	[0] = llc_conn_ev_qlfy_retry_cnt_lt_n2,
	[1] = NULL,
};

static const llc_conn_action_t llc_await_busy_actions_16[] = {
	[0] = llc_conn_ac_send_rnr_cmd_p_set_1,
	[1] = llc_conn_ac_start_p_timer,
	[2] = llc_conn_ac_inc_retry_cnt_by_1,
	[3] = NULL,
};

static struct llc_conn_state_trans llc_await_busy_state_trans_16 = {
	.ev	       = llc_conn_ev_p_tmr_exp,
	.next_state    = LLC_CONN_STATE_AWAIT_BUSY,
	.ev_qualifiers = llc_await_busy_ev_qfyrs_16,
	.ev_actions    = llc_await_busy_actions_16,
};

/*
 * Array of pointers;
 * one to each transition
 */
static struct llc_conn_state_trans *llc_await_busy_state_transitions[] = {
	 [0] = &llc_common_state_trans_1,		/* Request */
	 [1] = &llc_common_state_trans_2,
	 [2] = &llc_await_busy_state_trans_1_0,
	 [3] = &llc_common_state_trans_end,
	 [4] = &llc_await_busy_state_trans_1,		/* Local busy */
	 [5] = &llc_await_busy_state_trans_2,
	 [6] = &llc_await_busy_state_trans_3,
	 [7] = &llc_common_state_trans_end,
	 [8] = &llc_common_state_trans_end,		/* Initiate PF cycle */
	 [9] = &llc_common_state_trans_11a,		/* Timer */
	[10] = &llc_common_state_trans_11b,
	[11] = &llc_common_state_trans_11c,
	[12] = &llc_common_state_trans_11d,
	[13] = &llc_await_busy_state_trans_16,
	[14] = &llc_common_state_trans_end,
	[15] = &llc_await_busy_state_trans_4,		/* Receive frame */
	[16] = &llc_await_busy_state_trans_5a,
	[17] = &llc_await_busy_state_trans_5b,
	[18] = &llc_await_busy_state_trans_6,
	[19] = &llc_await_busy_state_trans_7,
	[20] = &llc_await_busy_state_trans_8a,
	[21] = &llc_await_busy_state_trans_8b,
	[22] = &llc_await_busy_state_trans_9,
	[23] = &llc_await_busy_state_trans_10a,
	[24] = &llc_await_busy_state_trans_10b,
	[25] = &llc_await_busy_state_trans_11a,
	[26] = &llc_await_busy_state_trans_11b,
	[27] = &llc_await_busy_state_trans_11c,
	[28] = &llc_await_busy_state_trans_11d,
	[29] = &llc_await_busy_state_trans_12a,
	[30] = &llc_await_busy_state_trans_12b,
	[31] = &llc_await_busy_state_trans_13,
	[32] = &llc_await_busy_state_trans_14a,
	[33] = &llc_await_busy_state_trans_14b,
	[34] = &llc_await_busy_state_trans_15,
	[35] = &llc_common_state_trans_3,
	[36] = &llc_common_state_trans_4,
	[37] = &llc_common_state_trans_5,
	[38] = &llc_common_state_trans_6,
	[39] = &llc_common_state_trans_7a,
	[40] = &llc_common_state_trans_7b,
	[41] = &llc_common_state_trans_8a,
	[42] = &llc_common_state_trans_8b,
	[43] = &llc_common_state_trans_8c,
	[44] = &llc_common_state_trans_9,
	/* [45] = &llc_common_state_trans_10, */
	[45] = &llc_common_state_trans_end,
};

/* ----------------- LLC_CONN_STATE_AWAIT_REJ transitions --------------- */
/* State transitions for LLC_CONN_EV_DATA_CONN_REQ event */
static const llc_conn_ev_qfyr_t llc_await_reject_ev_qfyrs_1_0[] = {
	[0] = llc_conn_ev_qlfy_set_status_refuse,
	[1] = NULL,
};

/* just one member, NULL, .bss zeroes it */
static const llc_conn_action_t llc_await_reject_actions_1_0[1];

static struct llc_conn_state_trans llc_await_reject_state_trans_1_0 = {
	.ev	       = llc_conn_ev_data_req,
	.next_state    = LLC_CONN_STATE_AWAIT_REJ,
	.ev_qualifiers = llc_await_reject_ev_qfyrs_1_0,
	.ev_actions    = llc_await_reject_actions_1_0,
};

/* State transitions for LLC_CONN_EV_LOCAL_BUSY_DETECTED event */
static const llc_conn_action_t llc_await_rejct_actions_1[] = {
	[0] = llc_conn_ac_send_rnr_xxx_x_set_0,
	[1] = llc_conn_ac_set_data_flag_2,
	[2] = NULL
};

static struct llc_conn_state_trans llc_await_rejct_state_trans_1 = {
	.ev	       = llc_conn_ev_local_busy_detected,
	.next_state    = LLC_CONN_STATE_AWAIT_BUSY,
	.ev_qualifiers = NONE,
	.ev_actions    = llc_await_rejct_actions_1,
};

/* State transitions for LLC_CONN_EV_RX_I_CMD_Pbit_SET_0_UNEXPD_Ns event */
static const llc_conn_action_t llc_await_rejct_actions_2a[] = {
	[0] = llc_conn_ac_upd_nr_received,
	[1] = llc_conn_ac_upd_vs,
	[2] = NULL
};

static struct llc_conn_state_trans llc_await_rejct_state_trans_2a = {
	.ev	       = llc_conn_ev_rx_i_cmd_pbit_set_0_unexpd_ns,
	.next_state    = LLC_CONN_STATE_AWAIT_REJ,
	.ev_qualifiers = NONE,
	.ev_actions    = llc_await_rejct_actions_2a,
};

/* State transitions for LLC_CONN_EV_RX_I_RSP_Fbit_SET_0_UNEXPD_Ns event */
static const llc_conn_action_t llc_await_rejct_actions_2b[] = {
	[0] = llc_conn_ac_upd_nr_received,
	[1] = llc_conn_ac_upd_vs,
	[2] = NULL
};

static struct llc_conn_state_trans llc_await_rejct_state_trans_2b = {
	.ev	       = llc_conn_ev_rx_i_rsp_fbit_set_0_unexpd_ns,
	.next_state    = LLC_CONN_STATE_AWAIT_REJ,
	.ev_qualifiers = NONE,
	.ev_actions    = llc_await_rejct_actions_2b,
};

/* State transitions for LLC_CONN_EV_RX_I_CMD_Pbit_SET_1_UNEXPD_Ns event */
static const llc_conn_action_t llc_await_rejct_actions_3[] = {
	[0] = llc_conn_ac_send_rr_rsp_f_set_1,
	[1] = llc_conn_ac_upd_nr_received,
	[2] = llc_conn_ac_upd_vs,
	[3] = NULL
};

static struct llc_conn_state_trans llc_await_rejct_state_trans_3 = {
	.ev	       = llc_conn_ev_rx_i_cmd_pbit_set_1_unexpd_ns,
	.next_state    = LLC_CONN_STATE_AWAIT_REJ,
	.ev_qualifiers = NONE,
	.ev_actions    = llc_await_rejct_actions_3,
};

/* State transitions for LLC_CONN_EV_RX_I_RSP_Fbit_SET_1 event */
static const llc_conn_action_t llc_await_rejct_actions_4[] = {
	[0] = llc_conn_ac_inc_vr_by_1,
	[1] = llc_conn_ac_data_ind,
	[2] = llc_conn_ac_stop_p_timer,
	[3] = llc_conn_ac_stop_rej_timer,
	[4] = llc_conn_ac_upd_nr_received,
	[5] = llc_conn_ac_upd_vs,
	[6] = llc_conn_ac_resend_i_xxx_x_set_0_or_send_rr,
	[7] = llc_conn_ac_clear_remote_busy,
	[8] = NULL,
};

static struct llc_conn_state_trans llc_await_rejct_state_trans_4 = {
	.ev	       = llc_conn_ev_rx_i_rsp_fbit_set_1,
	.next_state    = LLC_CONN_STATE_NORMAL,
	.ev_qualifiers = NONE,
	.ev_actions    = llc_await_rejct_actions_4,
};

/* State transitions for LLC_CONN_EV_RX_I_RSP_Fbit_SET_0 event */
static const llc_conn_action_t llc_await_rejct_actions_5a[] = {
	[0] = llc_conn_ac_inc_vr_by_1,
	[1] = llc_conn_ac_data_ind,
	[2] = llc_conn_ac_send_rr_xxx_x_set_0,
	[3] = llc_conn_ac_stop_rej_timer,
	[4] = llc_conn_ac_upd_nr_received,
	[5] = llc_conn_ac_upd_vs,
	[6] = NULL,
};

static struct llc_conn_state_trans llc_await_rejct_state_trans_5a = {
	.ev	       = llc_conn_ev_rx_i_rsp_fbit_set_0,
	.next_state    = LLC_CONN_STATE_AWAIT,
	.ev_qualifiers = NONE,
	.ev_actions    = llc_await_rejct_actions_5a,
};

/* State transitions for LLC_CONN_EV_RX_I_CMD_Pbit_SET_0 event */
static const llc_conn_action_t llc_await_rejct_actions_5b[] = {
	[0] = llc_conn_ac_inc_vr_by_1,
	[1] = llc_conn_ac_data_ind,
	[2] = llc_conn_ac_send_rr_xxx_x_set_0,
	[3] = llc_conn_ac_stop_rej_timer,
	[4] = llc_conn_ac_upd_nr_received,
	[5] = llc_conn_ac_upd_vs,
	[6] = NULL,
};

static struct llc_conn_state_trans llc_await_rejct_state_trans_5b = {
	.ev	       = llc_conn_ev_rx_i_cmd_pbit_set_0,
	.next_state    = LLC_CONN_STATE_AWAIT,
	.ev_qualifiers = NONE,
	.ev_actions    = llc_await_rejct_actions_5b,
};

/* State transitions for LLC_CONN_EV_RX_I_CMD_Pbit_SET_1 event */
static const llc_conn_action_t llc_await_rejct_actions_6[] = {
	[0] = llc_conn_ac_inc_vr_by_1,
	[1] = llc_conn_ac_data_ind,
	[2] = llc_conn_ac_send_rr_rsp_f_set_1,
	[3] = llc_conn_ac_stop_rej_timer,
	[4] = llc_conn_ac_upd_nr_received,
	[5] = llc_conn_ac_upd_vs,
	[6] = NULL,
};

static struct llc_conn_state_trans llc_await_rejct_state_trans_6 = {
	.ev	       = llc_conn_ev_rx_i_cmd_pbit_set_1,
	.next_state    = LLC_CONN_STATE_AWAIT,
	.ev_qualifiers = NONE,
	.ev_actions    = llc_await_rejct_actions_6,
};

/* State transitions for LLC_CONN_EV_RX_RR_RSP_Fbit_SET_1 event */
static const llc_conn_action_t llc_await_rejct_actions_7a[] = {
	[0] = llc_conn_ac_upd_nr_received,
	[1] = llc_conn_ac_upd_vs,
	[2] = llc_conn_ac_stop_p_timer,
	[3] = llc_conn_ac_resend_i_xxx_x_set_0,
	[4] = llc_conn_ac_clear_remote_busy,
	[5] = NULL,
};

static struct llc_conn_state_trans llc_await_rejct_state_trans_7a = {
	.ev	       = llc_conn_ev_rx_rr_rsp_fbit_set_1,
	.next_state    = LLC_CONN_STATE_REJ,
	.ev_qualifiers = NONE,
	.ev_actions    = llc_await_rejct_actions_7a,
};

/* State transitions for LLC_CONN_EV_RX_REJ_RSP_Fbit_SET_1 event */
static const llc_conn_action_t llc_await_rejct_actions_7b[] = {
	[0] = llc_conn_ac_upd_nr_received,
	[1] = llc_conn_ac_upd_vs,
	[2] = llc_conn_ac_stop_p_timer,
	[3] = llc_conn_ac_resend_i_xxx_x_set_0,
	[4] = llc_conn_ac_clear_remote_busy,
	[5] = NULL,
};

static struct llc_conn_state_trans llc_await_rejct_state_trans_7b = {
	.ev	       = llc_conn_ev_rx_rej_rsp_fbit_set_1,
	.next_state    = LLC_CONN_STATE_REJ,
	.ev_qualifiers = NONE,
	.ev_actions    = llc_await_rejct_actions_7b,
};

/* State transitions for LLC_CONN_EV_RX_I_RSP_Fbit_SET_1_UNEXPD_Ns event */
static const llc_conn_action_t llc_await_rejct_actions_7c[] = {
	[0] = llc_conn_ac_upd_nr_received,
	[1] = llc_conn_ac_upd_vs,
	[2] = llc_conn_ac_stop_p_timer,
	[3] = llc_conn_ac_resend_i_xxx_x_set_0,
	[4] = llc_conn_ac_clear_remote_busy,
	[5] = NULL,
};

static struct llc_conn_state_trans llc_await_rejct_state_trans_7c = {
	.ev	       = llc_conn_ev_rx_i_rsp_fbit_set_1_unexpd_ns,
	.next_state    = LLC_CONN_STATE_REJ,
	.ev_qualifiers = NONE,
	.ev_actions    = llc_await_rejct_actions_7c,
};

/* State transitions for LLC_CONN_EV_RX_RR_CMD_Pbit_SET_0 event */
static const llc_conn_action_t llc_await_rejct_actions_8a[] = {
	[0] = llc_conn_ac_upd_nr_received,
	[1] = llc_conn_ac_upd_vs,
	[2] = llc_conn_ac_clear_remote_busy,
	[3] = NULL,
};

static struct llc_conn_state_trans llc_await_rejct_state_trans_8a = {
	.ev	       = llc_conn_ev_rx_rr_cmd_pbit_set_0,
	.next_state    = LLC_CONN_STATE_AWAIT_REJ,
	.ev_qualifiers = NONE,
	.ev_actions    = llc_await_rejct_actions_8a,
};

/* State transitions for LLC_CONN_EV_RX_RR_RSP_Fbit_SET_0 event */
static const llc_conn_action_t llc_await_rejct_actions_8b[] = {
	[0] = llc_conn_ac_upd_nr_received,
	[1] = llc_conn_ac_upd_vs,
	[2] = llc_conn_ac_clear_remote_busy,
	[3] = NULL,
};

static struct llc_conn_state_trans llc_await_rejct_state_trans_8b = {
	.ev	       = llc_conn_ev_rx_rr_rsp_fbit_set_0,
	.next_state    = LLC_CONN_STATE_AWAIT_REJ,
	.ev_qualifiers = NONE,
	.ev_actions    = llc_await_rejct_actions_8b,
};

/* State transitions for LLC_CONN_EV_RX_REJ_CMD_Pbit_SET_0 event */
static const llc_conn_action_t llc_await_rejct_actions_8c[] = {
	[0] = llc_conn_ac_upd_nr_received,
	[1] = llc_conn_ac_upd_vs,
	[2] = llc_conn_ac_clear_remote_busy,
	[3] = NULL,
};

static struct llc_conn_state_trans llc_await_rejct_state_trans_8c = {
	.ev	       = llc_conn_ev_rx_rej_cmd_pbit_set_0,
	.next_state    = LLC_CONN_STATE_AWAIT_REJ,
	.ev_qualifiers = NONE,
	.ev_actions    = llc_await_rejct_actions_8c,
};

/* State transitions for LLC_CONN_EV_RX_REJ_RSP_Fbit_SET_0 event */
static const llc_conn_action_t llc_await_rejct_actions_8d[] = {
	[0] = llc_conn_ac_upd_nr_received,
	[1] = llc_conn_ac_upd_vs,
	[2] = llc_conn_ac_clear_remote_busy,
	[3] = NULL,
};

static struct llc_conn_state_trans llc_await_rejct_state_trans_8d = {
	.ev	       = llc_conn_ev_rx_rej_rsp_fbit_set_0,
	.next_state    = LLC_CONN_STATE_AWAIT_REJ,
	.ev_qualifiers = NONE,
	.ev_actions    = llc_await_rejct_actions_8d,
};

/* State transitions for LLC_CONN_EV_RX_RR_CMD_Pbit_SET_1 event */
static const llc_conn_action_t llc_await_rejct_actions_9a[] = {
	[0] = llc_conn_ac_send_rr_rsp_f_set_1,
	[1] = llc_conn_ac_upd_nr_received,
	[2] = llc_conn_ac_upd_vs,
	[3] = llc_conn_ac_clear_remote_busy,
	[4] = NULL,
};

static struct llc_conn_state_trans llc_await_rejct_state_trans_9a = {
	.ev	       = llc_conn_ev_rx_rr_cmd_pbit_set_1,
	.next_state    = LLC_CONN_STATE_AWAIT_REJ,
	.ev_qualifiers = NONE,
	.ev_actions    = llc_await_rejct_actions_9a,
};

/* State transitions for LLC_CONN_EV_RX_REJ_CMD_Pbit_SET_1 event */
static const llc_conn_action_t llc_await_rejct_actions_9b[] = {
	[0] = llc_conn_ac_send_rr_rsp_f_set_1,
	[1] = llc_conn_ac_upd_nr_received,
	[2] = llc_conn_ac_upd_vs,
	[3] = llc_conn_ac_clear_remote_busy,
	[4] = NULL,
};

static struct llc_conn_state_trans llc_await_rejct_state_trans_9b = {
	.ev	       = llc_conn_ev_rx_rej_cmd_pbit_set_1,
	.next_state    = LLC_CONN_STATE_AWAIT_REJ,
	.ev_qualifiers = NONE,
	.ev_actions    = llc_await_rejct_actions_9b,
};

/* State transitions for LLC_CONN_EV_RX_RNR_RSP_Fbit_SET_1 event */
static const llc_conn_action_t llc_await_rejct_actions_10[] = {
	[0] = llc_conn_ac_upd_nr_received,
	[1] = llc_conn_ac_upd_vs,
	[2] = llc_conn_ac_stop_p_timer,
	[3] = llc_conn_ac_set_remote_busy,
	[4] = NULL,
};

static struct llc_conn_state_trans llc_await_rejct_state_trans_10 = {
	.ev	       = llc_conn_ev_rx_rnr_rsp_fbit_set_1,
	.next_state    = LLC_CONN_STATE_REJ,
	.ev_qualifiers = NONE,
	.ev_actions    = llc_await_rejct_actions_10,
};

/* State transitions for LLC_CONN_EV_RX_RNR_CMD_Pbit_SET_0 event */
static const llc_conn_action_t llc_await_rejct_actions_11a[] = {
	[0] = llc_conn_ac_upd_nr_received,
	[1] = llc_conn_ac_upd_vs,
	[2] = llc_conn_ac_set_remote_busy,
	[3] = NULL,
};

static struct llc_conn_state_trans llc_await_rejct_state_trans_11a = {
	.ev	       = llc_conn_ev_rx_rnr_cmd_pbit_set_0,
	.next_state    = LLC_CONN_STATE_AWAIT_REJ,
	.ev_qualifiers = NONE,
	.ev_actions    = llc_await_rejct_actions_11a,
};

/* State transitions for LLC_CONN_EV_RX_RNR_RSP_Fbit_SET_0 event */
static const llc_conn_action_t llc_await_rejct_actions_11b[] = {
	[0] = llc_conn_ac_upd_nr_received,
	[1] = llc_conn_ac_upd_vs,
	[2] = llc_conn_ac_set_remote_busy,
	[3] = NULL,
};

static struct llc_conn_state_trans llc_await_rejct_state_trans_11b = {
	.ev	       = llc_conn_ev_rx_rnr_rsp_fbit_set_0,
	.next_state    = LLC_CONN_STATE_AWAIT_REJ,
	.ev_qualifiers = NONE,
	.ev_actions    = llc_await_rejct_actions_11b,
};

/* State transitions for LLC_CONN_EV_RX_RNR_CMD_Pbit_SET_1 event */
static const llc_conn_action_t llc_await_rejct_actions_12[] = {
	[0] = llc_conn_ac_send_rr_rsp_f_set_1,
	[1] = llc_conn_ac_upd_nr_received,
	[2] = llc_conn_ac_upd_vs,
	[3] = llc_conn_ac_set_remote_busy,
	[4] = NULL,
};

static struct llc_conn_state_trans llc_await_rejct_state_trans_12 = {
	.ev	       = llc_conn_ev_rx_rnr_cmd_pbit_set_1,
	.next_state    = LLC_CONN_STATE_AWAIT_REJ,
	.ev_qualifiers = NONE,
	.ev_actions    = llc_await_rejct_actions_12,
};

/* State transitions for LLC_CONN_EV_P_TMR_EXP event */
static const llc_conn_ev_qfyr_t llc_await_rejct_ev_qfyrs_13[] = {
	[0] = llc_conn_ev_qlfy_retry_cnt_lt_n2,
	[1] = NULL,
};

static const llc_conn_action_t llc_await_rejct_actions_13[] = {
	[0] = llc_conn_ac_send_rej_cmd_p_set_1,
	[1] = llc_conn_ac_stop_p_timer,
	[2] = llc_conn_ac_inc_retry_cnt_by_1,
	[3] = NULL,
};

static struct llc_conn_state_trans llc_await_rejct_state_trans_13 = {
	.ev	       = llc_conn_ev_p_tmr_exp,
	.next_state    = LLC_CONN_STATE_AWAIT_REJ,
	.ev_qualifiers = llc_await_rejct_ev_qfyrs_13,
	.ev_actions    = llc_await_rejct_actions_13,
};

/*
 * Array of pointers;
 * one to each transition
 */
static struct llc_conn_state_trans *llc_await_rejct_state_transitions[] = {
	 [0] = &llc_await_reject_state_trans_1_0,
	 [1] = &llc_common_state_trans_1,		/* requests */
	 [2] = &llc_common_state_trans_2,
	 [3] = &llc_common_state_trans_end,
	 [4] = &llc_await_rejct_state_trans_1,		/* local busy */
	 [5] = &llc_common_state_trans_end,
	 [6] = &llc_common_state_trans_end,		/* Initiate PF cycle */
	 [7] = &llc_await_rejct_state_trans_13,	/* timers */
	 [8] = &llc_common_state_trans_11a,
	 [9] = &llc_common_state_trans_11b,
	[10] = &llc_common_state_trans_11c,
	[11] = &llc_common_state_trans_11d,
	[12] = &llc_common_state_trans_end,
	[13] = &llc_await_rejct_state_trans_2a,	/* receive frames */
	[14] = &llc_await_rejct_state_trans_2b,
	[15] = &llc_await_rejct_state_trans_3,
	[16] = &llc_await_rejct_state_trans_4,
	[17] = &llc_await_rejct_state_trans_5a,
	[18] = &llc_await_rejct_state_trans_5b,
	[19] = &llc_await_rejct_state_trans_6,
	[20] = &llc_await_rejct_state_trans_7a,
	[21] = &llc_await_rejct_state_trans_7b,
	[22] = &llc_await_rejct_state_trans_7c,
	[23] = &llc_await_rejct_state_trans_8a,
	[24] = &llc_await_rejct_state_trans_8b,
	[25] = &llc_await_rejct_state_trans_8c,
	[26] = &llc_await_rejct_state_trans_8d,
	[27] = &llc_await_rejct_state_trans_9a,
	[28] = &llc_await_rejct_state_trans_9b,
	[29] = &llc_await_rejct_state_trans_10,
	[30] = &llc_await_rejct_state_trans_11a,
	[31] = &llc_await_rejct_state_trans_11b,
	[32] = &llc_await_rejct_state_trans_12,
	[33] = &llc_common_state_trans_3,
	[34] = &llc_common_state_trans_4,
	[35] = &llc_common_state_trans_5,
	[36] = &llc_common_state_trans_6,
	[37] = &llc_common_state_trans_7a,
	[38] = &llc_common_state_trans_7b,
	[39] = &llc_common_state_trans_8a,
	[40] = &llc_common_state_trans_8b,
	[41] = &llc_common_state_trans_8c,
	[42] = &llc_common_state_trans_9,
	/* [43] = &llc_common_state_trans_10, */
	[43] = &llc_common_state_trans_end,
};

/* LLC_CONN_STATE_D_CONN transitions */
/* State transitions for LLC_CONN_EV_RX_SABME_CMD_Pbit_SET_X event,
 * cause_flag = 1 */
static const llc_conn_ev_qfyr_t llc_d_conn_ev_qfyrs_1[] = {
	[0] = llc_conn_ev_qlfy_cause_flag_eq_1,
	[1] = llc_conn_ev_qlfy_set_status_conflict,
	[2] = NULL,
};

static const llc_conn_action_t llc_d_conn_actions_1[] = {
	[0] = llc_conn_ac_send_dm_rsp_f_set_p,
	[1] = llc_conn_ac_stop_ack_timer,
	[2] = llc_conn_ac_disc_confirm,
	[3] = llc_conn_disc,
	[4] = NULL,
};

static struct llc_conn_state_trans llc_d_conn_state_trans_1 = {
	.ev	       = llc_conn_ev_rx_sabme_cmd_pbit_set_x,
	.next_state    = LLC_CONN_STATE_ADM,
	.ev_qualifiers = llc_d_conn_ev_qfyrs_1,
	.ev_actions    = llc_d_conn_actions_1,
};

/* State transitions for LLC_CONN_EV_RX_SABME_CMD_Pbit_SET_X event,
 * cause_flag = 0
 */
static const llc_conn_ev_qfyr_t llc_d_conn_ev_qfyrs_1_1[] = {
	[0] = llc_conn_ev_qlfy_cause_flag_eq_0,
	[1] = llc_conn_ev_qlfy_set_status_conflict,
	[2] = NULL,
};

static const llc_conn_action_t llc_d_conn_actions_1_1[] = {
	[0] = llc_conn_ac_send_dm_rsp_f_set_p,
	[1] = llc_conn_ac_stop_ack_timer,
	[2] = llc_conn_disc,
	[3] = NULL,
};

static struct llc_conn_state_trans llc_d_conn_state_trans_1_1 = {
	.ev	       = llc_conn_ev_rx_sabme_cmd_pbit_set_x,
	.next_state    = LLC_CONN_STATE_ADM,
	.ev_qualifiers = llc_d_conn_ev_qfyrs_1_1,
	.ev_actions    = llc_d_conn_actions_1_1,
};

/* State transitions for LLC_CONN_EV_RX_UA_RSP_Fbit_SET_X event,
 * cause_flag = 1
 */
static const llc_conn_ev_qfyr_t llc_d_conn_ev_qfyrs_2[] = {
	[0] = llc_conn_ev_qlfy_p_flag_eq_f,
	[1] = llc_conn_ev_qlfy_cause_flag_eq_1,
	[2] = llc_conn_ev_qlfy_set_status_disc,
	[3] = NULL,
};

static const llc_conn_action_t llc_d_conn_actions_2[] = {
	[0] = llc_conn_ac_stop_ack_timer,
	[1] = llc_conn_ac_disc_confirm,
	[2] = llc_conn_disc,
	[3] = NULL,
};

static struct llc_conn_state_trans llc_d_conn_state_trans_2 = {
	.ev	       = llc_conn_ev_rx_ua_rsp_fbit_set_x,
	.next_state    = LLC_CONN_STATE_ADM,
	.ev_qualifiers = llc_d_conn_ev_qfyrs_2,
	.ev_actions    = llc_d_conn_actions_2,
};

/* State transitions for LLC_CONN_EV_RX_UA_RSP_Fbit_SET_X event,
 * cause_flag = 0
 */
static const llc_conn_ev_qfyr_t llc_d_conn_ev_qfyrs_2_1[] = {
	[0] = llc_conn_ev_qlfy_p_flag_eq_f,
	[1] = llc_conn_ev_qlfy_cause_flag_eq_0,
	[2] = llc_conn_ev_qlfy_set_status_disc,
	[3] = NULL,
};

static const llc_conn_action_t llc_d_conn_actions_2_1[] = {
	[0] = llc_conn_ac_stop_ack_timer,
	[1] = llc_conn_disc,
	[2] = NULL,
};

static struct llc_conn_state_trans llc_d_conn_state_trans_2_1 = {
	.ev	       = llc_conn_ev_rx_ua_rsp_fbit_set_x,
	.next_state    = LLC_CONN_STATE_ADM,
	.ev_qualifiers = llc_d_conn_ev_qfyrs_2_1,
	.ev_actions    = llc_d_conn_actions_2_1,
};

/* State transitions for LLC_CONN_EV_RX_DISC_CMD_Pbit_SET_X event */
static const llc_conn_action_t llc_d_conn_actions_3[] = {
	[0] = llc_conn_ac_send_ua_rsp_f_set_p,
	[1] = NULL,
};

static struct llc_conn_state_trans llc_d_conn_state_trans_3 = {
	.ev	       = llc_conn_ev_rx_disc_cmd_pbit_set_x,
	.next_state    = LLC_CONN_STATE_D_CONN,
	.ev_qualifiers = NONE,
	.ev_actions    = llc_d_conn_actions_3,
};

/* State transitions for LLC_CONN_EV_RX_DM_RSP_Fbit_SET_X event,
 * cause_flag = 1
 */
static const llc_conn_ev_qfyr_t llc_d_conn_ev_qfyrs_4[] = {
	[0] = llc_conn_ev_qlfy_cause_flag_eq_1,
	[1] = llc_conn_ev_qlfy_set_status_disc,
	[2] = NULL,
};

static const llc_conn_action_t llc_d_conn_actions_4[] = {
	[0] = llc_conn_ac_stop_ack_timer,
	[1] = llc_conn_ac_disc_confirm,
	[2] = llc_conn_disc,
	[3] = NULL,
};

static struct llc_conn_state_trans llc_d_conn_state_trans_4 = {
	.ev	       = llc_conn_ev_rx_dm_rsp_fbit_set_x,
	.next_state    = LLC_CONN_STATE_ADM,
	.ev_qualifiers = llc_d_conn_ev_qfyrs_4,
	.ev_actions    = llc_d_conn_actions_4,
};

/* State transitions for LLC_CONN_EV_RX_DM_RSP_Fbit_SET_X event,
 * cause_flag = 0
 */
static const llc_conn_ev_qfyr_t llc_d_conn_ev_qfyrs_4_1[] = {
	[0] = llc_conn_ev_qlfy_cause_flag_eq_0,
	[1] = llc_conn_ev_qlfy_set_status_disc,
	[2] = NULL,
};

static const llc_conn_action_t llc_d_conn_actions_4_1[] = {
	[0] = llc_conn_ac_stop_ack_timer,
	[1] = llc_conn_disc,
	[2] = NULL,
};

static struct llc_conn_state_trans llc_d_conn_state_trans_4_1 = {
	.ev	       = llc_conn_ev_rx_dm_rsp_fbit_set_x,
	.next_state    = LLC_CONN_STATE_ADM,
	.ev_qualifiers = llc_d_conn_ev_qfyrs_4_1,
	.ev_actions    = llc_d_conn_actions_4_1,
};

/*
 * State transition for
 * LLC_CONN_EV_DATA_CONN_REQ event
 */
static const llc_conn_ev_qfyr_t llc_d_conn_ev_qfyrs_5[] = {
	[0] = llc_conn_ev_qlfy_set_status_refuse,
	[1] = NULL,
};

/* just one member, NULL, .bss zeroes it */
static const llc_conn_action_t llc_d_conn_actions_5[1];

static struct llc_conn_state_trans llc_d_conn_state_trans_5 = {
	.ev	       = llc_conn_ev_data_req,
	.next_state    = LLC_CONN_STATE_D_CONN,
	.ev_qualifiers = llc_d_conn_ev_qfyrs_5,
	.ev_actions    = llc_d_conn_actions_5,
};

/* State transitions for LLC_CONN_EV_ACK_TMR_EXP event */
static const llc_conn_ev_qfyr_t llc_d_conn_ev_qfyrs_6[] = {
	[0] = llc_conn_ev_qlfy_retry_cnt_lt_n2,
	[1] = NULL,
};

static const llc_conn_action_t llc_d_conn_actions_6[] = {
	[0] = llc_conn_ac_send_disc_cmd_p_set_x,
	[1] = llc_conn_ac_start_ack_timer,
	[2] = llc_conn_ac_inc_retry_cnt_by_1,
	[3] = NULL,
};

static struct llc_conn_state_trans llc_d_conn_state_trans_6 = {
	.ev	       = llc_conn_ev_ack_tmr_exp,
	.next_state    = LLC_CONN_STATE_D_CONN,
	.ev_qualifiers = llc_d_conn_ev_qfyrs_6,
	.ev_actions    = llc_d_conn_actions_6,
};

/* State transitions for LLC_CONN_EV_ACK_TMR_EXP event, cause_flag = 1 */
static const llc_conn_ev_qfyr_t llc_d_conn_ev_qfyrs_7[] = {
	[0] = llc_conn_ev_qlfy_retry_cnt_gte_n2,
	[1] = llc_conn_ev_qlfy_cause_flag_eq_1,
	[2] = llc_conn_ev_qlfy_set_status_failed,
	[3] = NULL,
};

static const llc_conn_action_t llc_d_conn_actions_7[] = {
	[0] = llc_conn_ac_disc_confirm,
	[1] = llc_conn_disc,
	[2] = NULL,
};

static struct llc_conn_state_trans llc_d_conn_state_trans_7 = {
	.ev	       = llc_conn_ev_ack_tmr_exp,
	.next_state    = LLC_CONN_STATE_ADM,
	.ev_qualifiers = llc_d_conn_ev_qfyrs_7,
	.ev_actions    = llc_d_conn_actions_7,
};

/* State transitions for LLC_CONN_EV_ACK_TMR_EXP event, cause_flag = 0 */
static const llc_conn_ev_qfyr_t llc_d_conn_ev_qfyrs_8[] = {
	[0] = llc_conn_ev_qlfy_retry_cnt_gte_n2,
	[1] = llc_conn_ev_qlfy_cause_flag_eq_0,
	[2] = llc_conn_ev_qlfy_set_status_failed,
	[3] = NULL,
};

static const llc_conn_action_t llc_d_conn_actions_8[] = {
	[0] = llc_conn_disc,
	[1] = NULL,
};

static struct llc_conn_state_trans llc_d_conn_state_trans_8 = {
	.ev	       = llc_conn_ev_ack_tmr_exp,
	.next_state    = LLC_CONN_STATE_ADM,
	.ev_qualifiers = llc_d_conn_ev_qfyrs_8,
	.ev_actions    = llc_d_conn_actions_8,
};

/*
 * Array of pointers;
 * one to each transition
 */
static struct llc_conn_state_trans *llc_d_conn_state_transitions[] = {
	 [0] = &llc_d_conn_state_trans_5,	/* Request */
	 [1] = &llc_common_state_trans_end,
	 [2] = &llc_common_state_trans_end,	/* Local busy */
	 [3] = &llc_common_state_trans_end,	/* Initiate PF cycle */
	 [4] = &llc_d_conn_state_trans_6,	/* Timer */
	 [5] = &llc_d_conn_state_trans_7,
	 [6] = &llc_d_conn_state_trans_8,
	 [7] = &llc_common_state_trans_end,
	 [8] = &llc_d_conn_state_trans_1,	/* Receive frame */
	 [9] = &llc_d_conn_state_trans_1_1,
	[10] = &llc_d_conn_state_trans_2,
	[11] = &llc_d_conn_state_trans_2_1,
	[12] = &llc_d_conn_state_trans_3,
	[13] = &llc_d_conn_state_trans_4,
	[14] = &llc_d_conn_state_trans_4_1,
	[15] = &llc_common_state_trans_end,
};

/* LLC_CONN_STATE_RESET transitions */
/* State transitions for LLC_CONN_EV_RX_SABME_CMD_Pbit_SET_X event */
static const llc_conn_action_t llc_rst_actions_1[] = {
	[0] = llc_conn_ac_set_vs_0,
	[1] = llc_conn_ac_set_vr_0,
	[2] = llc_conn_ac_set_s_flag_1,
	[3] = llc_conn_ac_send_ua_rsp_f_set_p,
	[4] = NULL,
};

static struct llc_conn_state_trans llc_rst_state_trans_1 = {
	.ev	       = llc_conn_ev_rx_sabme_cmd_pbit_set_x,
	.next_state    = LLC_CONN_STATE_RESET,
	.ev_qualifiers = NONE,
	.ev_actions    = llc_rst_actions_1,
};

/* State transitions for LLC_CONN_EV_RX_UA_RSP_Fbit_SET_X event,
 * cause_flag = 1
 */
static const llc_conn_ev_qfyr_t llc_rst_ev_qfyrs_2[] = {
	[0] = llc_conn_ev_qlfy_p_flag_eq_f,
	[1] = llc_conn_ev_qlfy_cause_flag_eq_1,
	[2] = llc_conn_ev_qlfy_set_status_conn,
	[3] = NULL,
};

static const llc_conn_action_t llc_rst_actions_2[] = {
	[0] = llc_conn_ac_stop_ack_timer,
	[1] = llc_conn_ac_set_vs_0,
	[2] = llc_conn_ac_set_vr_0,
	[3] = llc_conn_ac_upd_p_flag,
	[4] = llc_conn_ac_rst_confirm,
	[5] = llc_conn_ac_set_remote_busy_0,
	[6] = llc_conn_reset,
	[7] = NULL,
};

static struct llc_conn_state_trans llc_rst_state_trans_2 = {
	.ev	       = llc_conn_ev_rx_ua_rsp_fbit_set_x,
	.next_state    = LLC_CONN_STATE_NORMAL,
	.ev_qualifiers = llc_rst_ev_qfyrs_2,
	.ev_actions    = llc_rst_actions_2,
};

/* State transitions for LLC_CONN_EV_RX_UA_RSP_Fbit_SET_X event,
 * cause_flag = 0
 */
static const llc_conn_ev_qfyr_t llc_rst_ev_qfyrs_2_1[] = {
	[0] = llc_conn_ev_qlfy_p_flag_eq_f,
	[1] = llc_conn_ev_qlfy_cause_flag_eq_0,
	[2] = llc_conn_ev_qlfy_set_status_rst_done,
	[3] = NULL,
};

static const llc_conn_action_t llc_rst_actions_2_1[] = {
	[0] = llc_conn_ac_stop_ack_timer,
	[1] = llc_conn_ac_set_vs_0,
	[2] = llc_conn_ac_set_vr_0,
	[3] = llc_conn_ac_upd_p_flag,
	[4] = llc_conn_ac_rst_confirm,
	[5] = llc_conn_ac_set_remote_busy_0,
	[6] = llc_conn_reset,
	[7] = NULL,
};

static struct llc_conn_state_trans llc_rst_state_trans_2_1 = {
	.ev	       = llc_conn_ev_rx_ua_rsp_fbit_set_x,
	.next_state    = LLC_CONN_STATE_NORMAL,
	.ev_qualifiers = llc_rst_ev_qfyrs_2_1,
	.ev_actions    = llc_rst_actions_2_1,
};

/* State transitions for LLC_CONN_EV_ACK_TMR_EXP event */
static const llc_conn_ev_qfyr_t llc_rst_ev_qfyrs_3[] = {
	[0] = llc_conn_ev_qlfy_s_flag_eq_1,
	[1] = llc_conn_ev_qlfy_set_status_rst_done,
	[2] = NULL,
};

static const llc_conn_action_t llc_rst_actions_3[] = {
	[0] = llc_conn_ac_set_p_flag_0,
	[1] = llc_conn_ac_set_remote_busy_0,
	[2] = NULL,
};

static struct llc_conn_state_trans llc_rst_state_trans_3 = {
	.ev	       = llc_conn_ev_ack_tmr_exp,
	.next_state    = LLC_CONN_STATE_NORMAL,
	.ev_qualifiers = llc_rst_ev_qfyrs_3,
	.ev_actions    = llc_rst_actions_3,
};

/* State transitions for LLC_CONN_EV_RX_DISC_CMD_Pbit_SET_X event,
 * cause_flag = 1
 */
static const llc_conn_ev_qfyr_t llc_rst_ev_qfyrs_4[] = {
	[0] = llc_conn_ev_qlfy_cause_flag_eq_1,
	[1] = llc_conn_ev_qlfy_set_status_disc,
	[2] = NULL,
};
static const llc_conn_action_t llc_rst_actions_4[] = {
	[0] = llc_conn_ac_send_dm_rsp_f_set_p,
	[1] = llc_conn_ac_disc_ind,
	[2] = llc_conn_ac_stop_ack_timer,
	[3] = llc_conn_disc,
	[4] = NULL,
};

static struct llc_conn_state_trans llc_rst_state_trans_4 = {
	.ev	       = llc_conn_ev_rx_disc_cmd_pbit_set_x,
	.next_state    = LLC_CONN_STATE_ADM,
	.ev_qualifiers = llc_rst_ev_qfyrs_4,
	.ev_actions    = llc_rst_actions_4,
};

/* State transitions for LLC_CONN_EV_RX_DISC_CMD_Pbit_SET_X event,
 * cause_flag = 0
 */
static const llc_conn_ev_qfyr_t llc_rst_ev_qfyrs_4_1[] = {
	[0] = llc_conn_ev_qlfy_cause_flag_eq_0,
	[1] = llc_conn_ev_qlfy_set_status_refuse,
	[2] = NULL,
};

static const llc_conn_action_t llc_rst_actions_4_1[] = {
	[0] = llc_conn_ac_send_dm_rsp_f_set_p,
	[1] = llc_conn_ac_stop_ack_timer,
	[2] = llc_conn_disc,
	[3] = NULL,
};

static struct llc_conn_state_trans llc_rst_state_trans_4_1 = {
	.ev	       = llc_conn_ev_rx_disc_cmd_pbit_set_x,
	.next_state    = LLC_CONN_STATE_ADM,
	.ev_qualifiers = llc_rst_ev_qfyrs_4_1,
	.ev_actions    = llc_rst_actions_4_1,
};

/* State transitions for LLC_CONN_EV_RX_DM_RSP_Fbit_SET_X event,
 * cause_flag = 1
 */
static const llc_conn_ev_qfyr_t llc_rst_ev_qfyrs_5[] = {
	[0] = llc_conn_ev_qlfy_cause_flag_eq_1,
	[1] = llc_conn_ev_qlfy_set_status_disc,
	[2] = NULL,
};

static const llc_conn_action_t llc_rst_actions_5[] = {
	[0] = llc_conn_ac_disc_ind,
	[1] = llc_conn_ac_stop_ack_timer,
	[2] = llc_conn_disc,
	[3] = NULL,
};

static struct llc_conn_state_trans llc_rst_state_trans_5 = {
	.ev	       = llc_conn_ev_rx_dm_rsp_fbit_set_x,
	.next_state    = LLC_CONN_STATE_ADM,
	.ev_qualifiers = llc_rst_ev_qfyrs_5,
	.ev_actions    = llc_rst_actions_5,
};

/* State transitions for LLC_CONN_EV_RX_DM_RSP_Fbit_SET_X event,
 * cause_flag = 0
 */
static const llc_conn_ev_qfyr_t llc_rst_ev_qfyrs_5_1[] = {
	[0] = llc_conn_ev_qlfy_cause_flag_eq_0,
	[1] = llc_conn_ev_qlfy_set_status_refuse,
	[2] = NULL,
};

static const llc_conn_action_t llc_rst_actions_5_1[] = {
	[0] = llc_conn_ac_stop_ack_timer,
	[1] = llc_conn_disc,
	[2] = NULL,
};

static struct llc_conn_state_trans llc_rst_state_trans_5_1 = {
	.ev	       = llc_conn_ev_rx_dm_rsp_fbit_set_x,
	.next_state    = LLC_CONN_STATE_ADM,
	.ev_qualifiers = llc_rst_ev_qfyrs_5_1,
	.ev_actions    = llc_rst_actions_5_1,
};

/* State transitions for DATA_CONN_REQ event */
static const llc_conn_ev_qfyr_t llc_rst_ev_qfyrs_6[] = {
	[0] = llc_conn_ev_qlfy_set_status_refuse,
	[1] = NULL,
};

/* just one member, NULL, .bss zeroes it */
static const llc_conn_action_t llc_rst_actions_6[1];

static struct llc_conn_state_trans llc_rst_state_trans_6 = {
	.ev	       = llc_conn_ev_data_req,
	.next_state    = LLC_CONN_STATE_RESET,
	.ev_qualifiers = llc_rst_ev_qfyrs_6,
	.ev_actions    = llc_rst_actions_6,
};

/* State transitions for LLC_CONN_EV_ACK_TMR_EXP event */
static const llc_conn_ev_qfyr_t llc_rst_ev_qfyrs_7[] = {
	[0] = llc_conn_ev_qlfy_retry_cnt_lt_n2,
	[1] = llc_conn_ev_qlfy_s_flag_eq_0,
	[2] = NULL,
};

static const llc_conn_action_t llc_rst_actions_7[] = {
	[0] = llc_conn_ac_send_sabme_cmd_p_set_x,
	[1] = llc_conn_ac_start_ack_timer,
	[2] = llc_conn_ac_inc_retry_cnt_by_1,
	[3] = NULL,
};

static struct llc_conn_state_trans llc_rst_state_trans_7 = {
	.ev	       = llc_conn_ev_ack_tmr_exp,
	.next_state    = LLC_CONN_STATE_RESET,
	.ev_qualifiers = llc_rst_ev_qfyrs_7,
	.ev_actions    = llc_rst_actions_7,
};

/* State transitions for LLC_CONN_EV_ACK_TMR_EXP event */
static const llc_conn_ev_qfyr_t llc_rst_ev_qfyrs_8[] = {
	[0] = llc_conn_ev_qlfy_retry_cnt_gte_n2,
	[1] = llc_conn_ev_qlfy_s_flag_eq_0,
	[2] = llc_conn_ev_qlfy_cause_flag_eq_1,
	[3] = llc_conn_ev_qlfy_set_status_failed,
	[4] = NULL,
};
static const llc_conn_action_t llc_rst_actions_8[] = {
	[0] = llc_conn_ac_disc_ind,
	[1] = llc_conn_disc,
	[2] = NULL,
};

static struct llc_conn_state_trans llc_rst_state_trans_8 = {
	.ev	       = llc_conn_ev_ack_tmr_exp,
	.next_state    = LLC_CONN_STATE_ADM,
	.ev_qualifiers = llc_rst_ev_qfyrs_8,
	.ev_actions    = llc_rst_actions_8,
};

/* State transitions for LLC_CONN_EV_ACK_TMR_EXP event */
static const llc_conn_ev_qfyr_t llc_rst_ev_qfyrs_8_1[] = {
	[0] = llc_conn_ev_qlfy_retry_cnt_gte_n2,
	[1] = llc_conn_ev_qlfy_s_flag_eq_0,
	[2] = llc_conn_ev_qlfy_cause_flag_eq_0,
	[3] = llc_conn_ev_qlfy_set_status_failed,
	[4] = NULL,
};
static const llc_conn_action_t llc_rst_actions_8_1[] = {
	[0] = llc_conn_ac_disc_ind,
	[1] = llc_conn_disc,
	[2] = NULL,
};

static struct llc_conn_state_trans llc_rst_state_trans_8_1 = {
	.ev	       = llc_conn_ev_ack_tmr_exp,
	.next_state    = LLC_CONN_STATE_ADM,
	.ev_qualifiers = llc_rst_ev_qfyrs_8_1,
	.ev_actions    = llc_rst_actions_8_1,
};

/*
 * Array of pointers;
 * one to each transition
 */
static struct llc_conn_state_trans *llc_rst_state_transitions[] = {
	 [0] = &llc_rst_state_trans_6,		/* Request */
	 [1] = &llc_common_state_trans_end,
	 [2] = &llc_common_state_trans_end,	/* Local busy */
	 [3] = &llc_common_state_trans_end,	/* Initiate PF cycle */
	 [4] = &llc_rst_state_trans_3,		/* Timer */
	 [5] = &llc_rst_state_trans_7,
	 [6] = &llc_rst_state_trans_8,
	 [7] = &llc_rst_state_trans_8_1,
	 [8] = &llc_common_state_trans_end,
	 [9] = &llc_rst_state_trans_1,		/* Receive frame */
	[10] = &llc_rst_state_trans_2,
	[11] = &llc_rst_state_trans_2_1,
	[12] = &llc_rst_state_trans_4,
	[13] = &llc_rst_state_trans_4_1,
	[14] = &llc_rst_state_trans_5,
	[15] = &llc_rst_state_trans_5_1,
	[16] = &llc_common_state_trans_end,
};

/* LLC_CONN_STATE_ERROR transitions */
/* State transitions for LLC_CONN_EV_RX_SABME_CMD_Pbit_SET_X event */
static const llc_conn_action_t llc_error_actions_1[] = {
	[0] = llc_conn_ac_set_vs_0,
	[1] = llc_conn_ac_set_vr_0,
	[2] = llc_conn_ac_send_ua_rsp_f_set_p,
	[3] = llc_conn_ac_rst_ind,
	[4] = llc_conn_ac_set_p_flag_0,
	[5] = llc_conn_ac_set_remote_busy_0,
	[6] = llc_conn_ac_stop_ack_timer,
	[7] = llc_conn_reset,
	[8] = NULL,
};

static struct llc_conn_state_trans llc_error_state_trans_1 = {
	.ev	       = llc_conn_ev_rx_sabme_cmd_pbit_set_x,
	.next_state    = LLC_CONN_STATE_NORMAL,
	.ev_qualifiers = NONE,
	.ev_actions    = llc_error_actions_1,
};

/* State transitions for LLC_CONN_EV_RX_DISC_CMD_Pbit_SET_X event */
static const llc_conn_action_t llc_error_actions_2[] = {
	[0] = llc_conn_ac_send_ua_rsp_f_set_p,
	[1] = llc_conn_ac_disc_ind,
	[2] = llc_conn_ac_stop_ack_timer,
	[3] = llc_conn_disc,
	[4] = NULL,
};

static struct llc_conn_state_trans llc_error_state_trans_2 = {
	.ev	       = llc_conn_ev_rx_disc_cmd_pbit_set_x,
	.next_state    = LLC_CONN_STATE_ADM,
	.ev_qualifiers = NONE,
	.ev_actions    = llc_error_actions_2,
};

/* State transitions for LLC_CONN_EV_RX_DM_RSP_Fbit_SET_X event */
static const llc_conn_action_t llc_error_actions_3[] = {
	[0] = llc_conn_ac_disc_ind,
	[1] = llc_conn_ac_stop_ack_timer,
	[2] = llc_conn_disc,
	[3] = NULL,
};

static struct llc_conn_state_trans llc_error_state_trans_3 = {
	.ev	       = llc_conn_ev_rx_dm_rsp_fbit_set_x,
	.next_state    = LLC_CONN_STATE_ADM,
	.ev_qualifiers = NONE,
	.ev_actions    = llc_error_actions_3,
};

/* State transitions for LLC_CONN_EV_RX_FRMR_RSP_Fbit_SET_X event */
static const llc_conn_action_t llc_error_actions_4[] = {
	[0] = llc_conn_ac_send_sabme_cmd_p_set_x,
	[1] = llc_conn_ac_start_ack_timer,
	[2] = llc_conn_ac_set_retry_cnt_0,
	[3] = llc_conn_ac_set_cause_flag_0,
	[4] = NULL,
};

static struct llc_conn_state_trans llc_error_state_trans_4 = {
	.ev	       = llc_conn_ev_rx_frmr_rsp_fbit_set_x,
	.next_state    = LLC_CONN_STATE_RESET,
	.ev_qualifiers = NONE,
	.ev_actions    = llc_error_actions_4,
};

/* State transitions for LLC_CONN_EV_RX_XXX_CMD_Pbit_SET_X event */
static const llc_conn_action_t llc_error_actions_5[] = {
	[0] = llc_conn_ac_resend_frmr_rsp_f_set_p,
	[1] = NULL,
};

static struct llc_conn_state_trans llc_error_state_trans_5 = {
	.ev	       = llc_conn_ev_rx_xxx_cmd_pbit_set_x,
	.next_state    = LLC_CONN_STATE_ERROR,
	.ev_qualifiers = NONE,
	.ev_actions    = llc_error_actions_5,
};

/* State transitions for LLC_CONN_EV_RX_XXX_RSP_Fbit_SET_X event */
static struct llc_conn_state_trans llc_error_state_trans_6 = {
	.ev	       = llc_conn_ev_rx_xxx_rsp_fbit_set_x,
	.next_state    = LLC_CONN_STATE_ERROR,
	.ev_qualifiers = NONE,
	.ev_actions    = NONE,
};

/* State transitions for LLC_CONN_EV_ACK_TMR_EXP event */
static const llc_conn_ev_qfyr_t llc_error_ev_qfyrs_7[] = {
	[0] = llc_conn_ev_qlfy_retry_cnt_lt_n2,
	[1] = NULL,
};

static const llc_conn_action_t llc_error_actions_7[] = {
	[0] = llc_conn_ac_resend_frmr_rsp_f_set_0,
	[1] = llc_conn_ac_start_ack_timer,
	[2] = llc_conn_ac_inc_retry_cnt_by_1,
	[3] = NULL,
};

static struct llc_conn_state_trans llc_error_state_trans_7 = {
	.ev	       = llc_conn_ev_ack_tmr_exp,
	.next_state    = LLC_CONN_STATE_ERROR,
	.ev_qualifiers = llc_error_ev_qfyrs_7,
	.ev_actions    = llc_error_actions_7,
};

/* State transitions for LLC_CONN_EV_ACK_TMR_EXP event */
static const llc_conn_ev_qfyr_t llc_error_ev_qfyrs_8[] = {
	[0] = llc_conn_ev_qlfy_retry_cnt_gte_n2,
	[1] = NULL,
};

static const llc_conn_action_t llc_error_actions_8[] = {
	[0] = llc_conn_ac_send_sabme_cmd_p_set_x,
	[1] = llc_conn_ac_set_s_flag_0,
	[2] = llc_conn_ac_start_ack_timer,
	[3] = llc_conn_ac_set_retry_cnt_0,
	[4] = llc_conn_ac_set_cause_flag_0,
	[5] = NULL,
};

static struct llc_conn_state_trans llc_error_state_trans_8 = {
	.ev	       = llc_conn_ev_ack_tmr_exp,
	.next_state    = LLC_CONN_STATE_RESET,
	.ev_qualifiers = llc_error_ev_qfyrs_8,
	.ev_actions    = llc_error_actions_8,
};

/* State transitions for LLC_CONN_EV_DATA_CONN_REQ event */
static const llc_conn_ev_qfyr_t llc_error_ev_qfyrs_9[] = {
	[0] = llc_conn_ev_qlfy_set_status_refuse,
	[1] = NULL,
};

/* just one member, NULL, .bss zeroes it */
static const llc_conn_action_t llc_error_actions_9[1];

static struct llc_conn_state_trans llc_error_state_trans_9 = {
	.ev	       = llc_conn_ev_data_req,
	.next_state    = LLC_CONN_STATE_ERROR,
	.ev_qualifiers = llc_error_ev_qfyrs_9,
	.ev_actions    = llc_error_actions_9,
};

/*
 * Array of pointers;
 * one to each transition
 */
static struct llc_conn_state_trans *llc_error_state_transitions[] = {
	 [0] = &llc_error_state_trans_9,	/* Request */
	 [1] = &llc_common_state_trans_end,
	 [2] = &llc_common_state_trans_end,	/* Local busy */
	 [3] = &llc_common_state_trans_end,	/* Initiate PF cycle */
	 [4] = &llc_error_state_trans_7,	/* Timer */
	 [5] = &llc_error_state_trans_8,
	 [6] = &llc_common_state_trans_end,
	 [7] = &llc_error_state_trans_1,	/* Receive frame */
	 [8] = &llc_error_state_trans_2,
	 [9] = &llc_error_state_trans_3,
	[10] = &llc_error_state_trans_4,
	[11] = &llc_error_state_trans_5,
	[12] = &llc_error_state_trans_6,
	[13] = &llc_common_state_trans_end,
};

/* LLC_CONN_STATE_TEMP transitions */
/* State transitions for LLC_CONN_EV_DISC_REQ event */
static const llc_conn_action_t llc_temp_actions_1[] = {
	[0] = llc_conn_ac_stop_all_timers,
	[1] = llc_conn_ac_send_disc_cmd_p_set_x,
	[2] = llc_conn_disc,
	[3] = NULL,
};

static struct llc_conn_state_trans llc_temp_state_trans_1 = {
	.ev	       = llc_conn_ev_disc_req,
	.next_state    = LLC_CONN_STATE_ADM,
	.ev_qualifiers = NONE,
	.ev_actions    = llc_temp_actions_1,
};

/*
 * Array of pointers;
 * one to each transition
 */
static struct llc_conn_state_trans *llc_temp_state_transitions[] = {
	[0] = &llc_temp_state_trans_1,		/* requests */
	[1] = &llc_common_state_trans_end,
	[2] = &llc_common_state_trans_end,	/* local busy */
	[3] = &llc_common_state_trans_end,	/* init_pf_cycle */
	[4] = &llc_common_state_trans_end,	/* timer */
	[5] = &llc_common_state_trans_end,	/* receive */
};

/* Connection State Transition Table */
struct llc_conn_state llc_conn_state_table[NBR_CONN_STATES] = {
	[LLC_CONN_STATE_ADM - 1] = {
		.current_state	= LLC_CONN_STATE_ADM,
		.transitions	= llc_adm_state_transitions,
	},
	[LLC_CONN_STATE_SETUP - 1] = {
		.current_state	= LLC_CONN_STATE_SETUP,
		.transitions	= llc_setup_state_transitions,
	},
	[LLC_CONN_STATE_NORMAL - 1] = {
		.current_state	= LLC_CONN_STATE_NORMAL,
		.transitions	= llc_normal_state_transitions,
	},
	[LLC_CONN_STATE_BUSY - 1] = {
		.current_state	= LLC_CONN_STATE_BUSY,
		.transitions	= llc_busy_state_transitions,
	},
	[LLC_CONN_STATE_REJ - 1] = {
		.current_state	= LLC_CONN_STATE_REJ,
		.transitions	= llc_reject_state_transitions,
	},
	[LLC_CONN_STATE_AWAIT - 1] = {
		.current_state	= LLC_CONN_STATE_AWAIT,
		.transitions	= llc_await_state_transitions,
	},
	[LLC_CONN_STATE_AWAIT_BUSY - 1] = {
		.current_state	= LLC_CONN_STATE_AWAIT_BUSY,
		.transitions	= llc_await_busy_state_transitions,
	},
	[LLC_CONN_STATE_AWAIT_REJ - 1] = {
		.current_state	= LLC_CONN_STATE_AWAIT_REJ,
		.transitions	= llc_await_rejct_state_transitions,
	},
	[LLC_CONN_STATE_D_CONN - 1] = {
		.current_state	= LLC_CONN_STATE_D_CONN,
		.transitions	= llc_d_conn_state_transitions,
	},
	[LLC_CONN_STATE_RESET - 1] = {
		.current_state	= LLC_CONN_STATE_RESET,
		.transitions	= llc_rst_state_transitions,
	},
	[LLC_CONN_STATE_ERROR - 1] = {
		.current_state	= LLC_CONN_STATE_ERROR,
		.transitions	= llc_error_state_transitions,
	},
	[LLC_CONN_STATE_TEMP - 1] = {
		.current_state	= LLC_CONN_STATE_TEMP,
		.transitions	= llc_temp_state_transitions,
	},
};
