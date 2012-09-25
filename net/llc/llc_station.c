/*
 * llc_station.c - station component of LLC
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
#include <linux/init.h>
#include <linux/module.h>
#include <linux/slab.h>
#include <net/llc.h>
#include <net/llc_sap.h>
#include <net/llc_conn.h>
#include <net/llc_c_ac.h>
#include <net/llc_s_ac.h>
#include <net/llc_c_ev.h>
#include <net/llc_c_st.h>
#include <net/llc_s_ev.h>
#include <net/llc_s_st.h>
#include <net/llc_pdu.h>

/**
 * struct llc_station - LLC station component
 *
 * SAP and connection resource manager, one per adapter.
 *
 * @state: state of station
 * @xid_r_count: XID response PDU counter
 * @mac_sa: MAC source address
 * @sap_list: list of related SAPs
 * @ev_q: events entering state mach.
 * @mac_pdu_q: PDUs ready to send to MAC
 */
struct llc_station {
	u8			    state;
	u8			    xid_r_count;
	struct timer_list	    ack_timer;
	u8			    retry_count;
	u8			    maximum_retry;
	struct {
		struct sk_buff_head list;
		spinlock_t	    lock;
	} ev_q;
	struct sk_buff_head	    mac_pdu_q;
};

#define LLC_STATION_ACK_TIME (3 * HZ)

int sysctl_llc_station_ack_timeout = LLC_STATION_ACK_TIME;

/* Types of events (possible values in 'ev->type') */
#define LLC_STATION_EV_TYPE_SIMPLE	1
#define LLC_STATION_EV_TYPE_CONDITION	2
#define LLC_STATION_EV_TYPE_PRIM	3
#define LLC_STATION_EV_TYPE_PDU		4       /* command/response PDU */
#define LLC_STATION_EV_TYPE_ACK_TMR	5
#define LLC_STATION_EV_TYPE_RPT_STATUS	6

/* Events */
#define LLC_STATION_EV_ENABLE_WITH_DUP_ADDR_CHECK		1
#define LLC_STATION_EV_ENABLE_WITHOUT_DUP_ADDR_CHECK		2
#define LLC_STATION_EV_ACK_TMR_EXP_LT_RETRY_CNT_MAX_RETRY	3
#define LLC_STATION_EV_ACK_TMR_EXP_EQ_RETRY_CNT_MAX_RETRY	4
#define LLC_STATION_EV_RX_NULL_DSAP_XID_C			5
#define LLC_STATION_EV_RX_NULL_DSAP_0_XID_R_XID_R_CNT_EQ	6
#define LLC_STATION_EV_RX_NULL_DSAP_1_XID_R_XID_R_CNT_EQ	7
#define LLC_STATION_EV_RX_NULL_DSAP_TEST_C			8
#define LLC_STATION_EV_DISABLE_REQ				9

struct llc_station_state_ev {
	u8		 type;
	u8		 prim;
	u8		 prim_type;
	u8		 reason;
	struct list_head node; /* node in station->ev_q.list */
};

static __inline__ struct llc_station_state_ev *
					llc_station_ev(struct sk_buff *skb)
{
	return (struct llc_station_state_ev *)skb->cb;
}

typedef int (*llc_station_ev_t)(struct sk_buff *skb);

#define LLC_STATION_STATE_DOWN		1	/* initial state */
#define LLC_STATION_STATE_DUP_ADDR_CHK	2
#define LLC_STATION_STATE_UP		3

#define LLC_NBR_STATION_STATES		3	/* size of state table */

typedef int (*llc_station_action_t)(struct sk_buff *skb);

/* Station component state table structure */
struct llc_station_state_trans {
	llc_station_ev_t ev;
	u8 next_state;
	llc_station_action_t *ev_actions;
};

struct llc_station_state {
	u8 curr_state;
	struct llc_station_state_trans **transitions;
};

static struct llc_station llc_main_station;

static int llc_stat_ev_enable_with_dup_addr_check(struct sk_buff *skb)
{
	struct llc_station_state_ev *ev = llc_station_ev(skb);

	return ev->type == LLC_STATION_EV_TYPE_SIMPLE &&
	       ev->prim_type ==
			      LLC_STATION_EV_ENABLE_WITH_DUP_ADDR_CHECK ? 0 : 1;
}

static int llc_stat_ev_enable_without_dup_addr_check(struct sk_buff *skb)
{
	struct llc_station_state_ev *ev = llc_station_ev(skb);

	return ev->type == LLC_STATION_EV_TYPE_SIMPLE &&
	       ev->prim_type ==
			LLC_STATION_EV_ENABLE_WITHOUT_DUP_ADDR_CHECK ? 0 : 1;
}

static int llc_stat_ev_ack_tmr_exp_lt_retry_cnt_max_retry(struct sk_buff *skb)
{
	struct llc_station_state_ev *ev = llc_station_ev(skb);

	return ev->type == LLC_STATION_EV_TYPE_ACK_TMR &&
		llc_main_station.retry_count <
		llc_main_station.maximum_retry ? 0 : 1;
}

static int llc_stat_ev_ack_tmr_exp_eq_retry_cnt_max_retry(struct sk_buff *skb)
{
	struct llc_station_state_ev *ev = llc_station_ev(skb);

	return ev->type == LLC_STATION_EV_TYPE_ACK_TMR &&
		llc_main_station.retry_count ==
		llc_main_station.maximum_retry ? 0 : 1;
}

static int llc_stat_ev_rx_null_dsap_xid_c(struct sk_buff *skb)
{
	struct llc_station_state_ev *ev = llc_station_ev(skb);
	struct llc_pdu_un *pdu = llc_pdu_un_hdr(skb);

	return ev->type == LLC_STATION_EV_TYPE_PDU &&
	       LLC_PDU_IS_CMD(pdu) &&			/* command PDU */
	       LLC_PDU_TYPE_IS_U(pdu) &&		/* U type PDU */
	       LLC_U_PDU_CMD(pdu) == LLC_1_PDU_CMD_XID &&
	       !pdu->dsap ? 0 : 1;			/* NULL DSAP value */
}

static int llc_stat_ev_rx_null_dsap_0_xid_r_xid_r_cnt_eq(struct sk_buff *skb)
{
	struct llc_station_state_ev *ev = llc_station_ev(skb);
	struct llc_pdu_un *pdu = llc_pdu_un_hdr(skb);

	return ev->type == LLC_STATION_EV_TYPE_PDU &&
	       LLC_PDU_IS_RSP(pdu) &&			/* response PDU */
	       LLC_PDU_TYPE_IS_U(pdu) &&		/* U type PDU */
	       LLC_U_PDU_RSP(pdu) == LLC_1_PDU_CMD_XID &&
	       !pdu->dsap &&				/* NULL DSAP value */
	       !llc_main_station.xid_r_count ? 0 : 1;
}

static int llc_stat_ev_rx_null_dsap_1_xid_r_xid_r_cnt_eq(struct sk_buff *skb)
{
	struct llc_station_state_ev *ev = llc_station_ev(skb);
	struct llc_pdu_un *pdu = llc_pdu_un_hdr(skb);

	return ev->type == LLC_STATION_EV_TYPE_PDU &&
	       LLC_PDU_IS_RSP(pdu) &&			/* response PDU */
	       LLC_PDU_TYPE_IS_U(pdu) &&		/* U type PDU */
	       LLC_U_PDU_RSP(pdu) == LLC_1_PDU_CMD_XID &&
	       !pdu->dsap &&				/* NULL DSAP value */
	       llc_main_station.xid_r_count == 1 ? 0 : 1;
}

static int llc_stat_ev_rx_null_dsap_test_c(struct sk_buff *skb)
{
	struct llc_station_state_ev *ev = llc_station_ev(skb);
	struct llc_pdu_un *pdu = llc_pdu_un_hdr(skb);

	return ev->type == LLC_STATION_EV_TYPE_PDU &&
	       LLC_PDU_IS_CMD(pdu) &&			/* command PDU */
	       LLC_PDU_TYPE_IS_U(pdu) &&		/* U type PDU */
	       LLC_U_PDU_CMD(pdu) == LLC_1_PDU_CMD_TEST &&
	       !pdu->dsap ? 0 : 1;			/* NULL DSAP */
}

static int llc_stat_ev_disable_req(struct sk_buff *skb)
{
	struct llc_station_state_ev *ev = llc_station_ev(skb);

	return ev->type == LLC_STATION_EV_TYPE_PRIM &&
	       ev->prim == LLC_DISABLE_PRIM &&
	       ev->prim_type == LLC_PRIM_TYPE_REQ ? 0 : 1;
}

/**
 *	llc_station_send_pdu - queues PDU to send
 *	@skb: Address of the PDU
 *
 *	Queues a PDU to send to the MAC layer.
 */
static void llc_station_send_pdu(struct sk_buff *skb)
{
	skb_queue_tail(&llc_main_station.mac_pdu_q, skb);
	while ((skb = skb_dequeue(&llc_main_station.mac_pdu_q)) != NULL)
		if (dev_queue_xmit(skb))
			break;
}

static int llc_station_ac_start_ack_timer(struct sk_buff *skb)
{
	mod_timer(&llc_main_station.ack_timer,
		  jiffies + sysctl_llc_station_ack_timeout);
	return 0;
}

static int llc_station_ac_set_retry_cnt_0(struct sk_buff *skb)
{
	llc_main_station.retry_count = 0;
	return 0;
}

static int llc_station_ac_inc_retry_cnt_by_1(struct sk_buff *skb)
{
	llc_main_station.retry_count++;
	return 0;
}

static int llc_station_ac_set_xid_r_cnt_0(struct sk_buff *skb)
{
	llc_main_station.xid_r_count = 0;
	return 0;
}

static int llc_station_ac_inc_xid_r_cnt_by_1(struct sk_buff *skb)
{
	llc_main_station.xid_r_count++;
	return 0;
}

static int llc_station_ac_send_null_dsap_xid_c(struct sk_buff *skb)
{
	int rc = 1;
	struct sk_buff *nskb = llc_alloc_frame(NULL, skb->dev, LLC_PDU_TYPE_U,
					       sizeof(struct llc_xid_info));

	if (!nskb)
		goto out;
	llc_pdu_header_init(nskb, LLC_PDU_TYPE_U, 0, 0, LLC_PDU_CMD);
	llc_pdu_init_as_xid_cmd(nskb, LLC_XID_NULL_CLASS_2, 127);
	rc = llc_mac_hdr_init(nskb, skb->dev->dev_addr, skb->dev->dev_addr);
	if (unlikely(rc))
		goto free;
	llc_station_send_pdu(nskb);
out:
	return rc;
free:
	kfree_skb(nskb);
	goto out;
}

static int llc_station_ac_send_xid_r(struct sk_buff *skb)
{
	u8 mac_da[ETH_ALEN], dsap;
	int rc = 1;
	struct sk_buff *nskb = llc_alloc_frame(NULL, skb->dev, LLC_PDU_TYPE_U,
					       sizeof(struct llc_xid_info));

	if (!nskb)
		goto out;
	rc = 0;
	llc_pdu_decode_sa(skb, mac_da);
	llc_pdu_decode_ssap(skb, &dsap);
	llc_pdu_header_init(nskb, LLC_PDU_TYPE_U, 0, dsap, LLC_PDU_RSP);
	llc_pdu_init_as_xid_rsp(nskb, LLC_XID_NULL_CLASS_2, 127);
	rc = llc_mac_hdr_init(nskb, skb->dev->dev_addr, mac_da);
	if (unlikely(rc))
		goto free;
	llc_station_send_pdu(nskb);
out:
	return rc;
free:
	kfree_skb(nskb);
	goto out;
}

static int llc_station_ac_send_test_r(struct sk_buff *skb)
{
	u8 mac_da[ETH_ALEN], dsap;
	int rc = 1;
	u32 data_size;
	struct sk_buff *nskb;

	/* The test request command is type U (llc_len = 3) */
	data_size = ntohs(eth_hdr(skb)->h_proto) - 3;
	nskb = llc_alloc_frame(NULL, skb->dev, LLC_PDU_TYPE_U, data_size);

	if (!nskb)
		goto out;
	rc = 0;
	llc_pdu_decode_sa(skb, mac_da);
	llc_pdu_decode_ssap(skb, &dsap);
	llc_pdu_header_init(nskb, LLC_PDU_TYPE_U, 0, dsap, LLC_PDU_RSP);
	llc_pdu_init_as_test_rsp(nskb, skb);
	rc = llc_mac_hdr_init(nskb, skb->dev->dev_addr, mac_da);
	if (unlikely(rc))
		goto free;
	llc_station_send_pdu(nskb);
out:
	return rc;
free:
	kfree_skb(nskb);
	goto out;
}

static int llc_station_ac_report_status(struct sk_buff *skb)
{
	return 0;
}

/* COMMON STATION STATE transitions */

/* dummy last-transition indicator; common to all state transition groups
 * last entry for this state
 * all members are zeros, .bss zeroes it
 */
static struct llc_station_state_trans llc_stat_state_trans_end;

/* DOWN STATE transitions */

/* state transition for LLC_STATION_EV_ENABLE_WITH_DUP_ADDR_CHECK event */
static llc_station_action_t llc_stat_down_state_actions_1[] = {
	[0] = llc_station_ac_start_ack_timer,
	[1] = llc_station_ac_set_retry_cnt_0,
	[2] = llc_station_ac_set_xid_r_cnt_0,
	[3] = llc_station_ac_send_null_dsap_xid_c,
	[4] = NULL,
};

static struct llc_station_state_trans llc_stat_down_state_trans_1 = {
	.ev	    = llc_stat_ev_enable_with_dup_addr_check,
	.next_state = LLC_STATION_STATE_DUP_ADDR_CHK,
	.ev_actions = llc_stat_down_state_actions_1,
};

/* state transition for LLC_STATION_EV_ENABLE_WITHOUT_DUP_ADDR_CHECK event */
static llc_station_action_t llc_stat_down_state_actions_2[] = {
	[0] = llc_station_ac_report_status,	/* STATION UP */
	[1] = NULL,
};

static struct llc_station_state_trans llc_stat_down_state_trans_2 = {
	.ev	    = llc_stat_ev_enable_without_dup_addr_check,
	.next_state = LLC_STATION_STATE_UP,
	.ev_actions = llc_stat_down_state_actions_2,
};

/* array of pointers; one to each transition */
static struct llc_station_state_trans *llc_stat_dwn_state_trans[] = {
	[0] = &llc_stat_down_state_trans_1,
	[1] = &llc_stat_down_state_trans_2,
	[2] = &llc_stat_state_trans_end,
};

/* UP STATE transitions */
/* state transition for LLC_STATION_EV_DISABLE_REQ event */
static llc_station_action_t llc_stat_up_state_actions_1[] = {
	[0] = llc_station_ac_report_status,	/* STATION DOWN */
	[1] = NULL,
};

static struct llc_station_state_trans llc_stat_up_state_trans_1 = {
	.ev	    = llc_stat_ev_disable_req,
	.next_state = LLC_STATION_STATE_DOWN,
	.ev_actions = llc_stat_up_state_actions_1,
};

/* state transition for LLC_STATION_EV_RX_NULL_DSAP_XID_C event */
static llc_station_action_t llc_stat_up_state_actions_2[] = {
	[0] = llc_station_ac_send_xid_r,
	[1] = NULL,
};

static struct llc_station_state_trans llc_stat_up_state_trans_2 = {
	.ev	    = llc_stat_ev_rx_null_dsap_xid_c,
	.next_state = LLC_STATION_STATE_UP,
	.ev_actions = llc_stat_up_state_actions_2,
};

/* state transition for LLC_STATION_EV_RX_NULL_DSAP_TEST_C event */
static llc_station_action_t llc_stat_up_state_actions_3[] = {
	[0] = llc_station_ac_send_test_r,
	[1] = NULL,
};

static struct llc_station_state_trans llc_stat_up_state_trans_3 = {
	.ev	    = llc_stat_ev_rx_null_dsap_test_c,
	.next_state = LLC_STATION_STATE_UP,
	.ev_actions = llc_stat_up_state_actions_3,
};

/* array of pointers; one to each transition */
static struct llc_station_state_trans *llc_stat_up_state_trans [] = {
	[0] = &llc_stat_up_state_trans_1,
	[1] = &llc_stat_up_state_trans_2,
	[2] = &llc_stat_up_state_trans_3,
	[3] = &llc_stat_state_trans_end,
};

/* DUP ADDR CHK STATE transitions */
/* state transition for LLC_STATION_EV_RX_NULL_DSAP_0_XID_R_XID_R_CNT_EQ
 * event
 */
static llc_station_action_t llc_stat_dupaddr_state_actions_1[] = {
	[0] = llc_station_ac_inc_xid_r_cnt_by_1,
	[1] = NULL,
};

static struct llc_station_state_trans llc_stat_dupaddr_state_trans_1 = {
	.ev	    = llc_stat_ev_rx_null_dsap_0_xid_r_xid_r_cnt_eq,
	.next_state = LLC_STATION_STATE_DUP_ADDR_CHK,
	.ev_actions = llc_stat_dupaddr_state_actions_1,
};

/* state transition for LLC_STATION_EV_RX_NULL_DSAP_1_XID_R_XID_R_CNT_EQ
 * event
 */
static llc_station_action_t llc_stat_dupaddr_state_actions_2[] = {
	[0] = llc_station_ac_report_status,	/* DUPLICATE ADDRESS FOUND */
	[1] = NULL,
};

static struct llc_station_state_trans llc_stat_dupaddr_state_trans_2 = {
	.ev	    = llc_stat_ev_rx_null_dsap_1_xid_r_xid_r_cnt_eq,
	.next_state = LLC_STATION_STATE_DOWN,
	.ev_actions = llc_stat_dupaddr_state_actions_2,
};

/* state transition for LLC_STATION_EV_RX_NULL_DSAP_XID_C event */
static llc_station_action_t llc_stat_dupaddr_state_actions_3[] = {
	[0] = llc_station_ac_send_xid_r,
	[1] = NULL,
};

static struct llc_station_state_trans llc_stat_dupaddr_state_trans_3 = {
	.ev	    = llc_stat_ev_rx_null_dsap_xid_c,
	.next_state = LLC_STATION_STATE_DUP_ADDR_CHK,
	.ev_actions = llc_stat_dupaddr_state_actions_3,
};

/* state transition for LLC_STATION_EV_ACK_TMR_EXP_LT_RETRY_CNT_MAX_RETRY
 * event
 */
static llc_station_action_t llc_stat_dupaddr_state_actions_4[] = {
	[0] = llc_station_ac_start_ack_timer,
	[1] = llc_station_ac_inc_retry_cnt_by_1,
	[2] = llc_station_ac_set_xid_r_cnt_0,
	[3] = llc_station_ac_send_null_dsap_xid_c,
	[4] = NULL,
};

static struct llc_station_state_trans llc_stat_dupaddr_state_trans_4 = {
	.ev	    = llc_stat_ev_ack_tmr_exp_lt_retry_cnt_max_retry,
	.next_state = LLC_STATION_STATE_DUP_ADDR_CHK,
	.ev_actions = llc_stat_dupaddr_state_actions_4,
};

/* state transition for LLC_STATION_EV_ACK_TMR_EXP_EQ_RETRY_CNT_MAX_RETRY
 * event
 */
static llc_station_action_t llc_stat_dupaddr_state_actions_5[] = {
	[0] = llc_station_ac_report_status,	/* STATION UP */
	[1] = NULL,
};

static struct llc_station_state_trans llc_stat_dupaddr_state_trans_5 = {
	.ev	    = llc_stat_ev_ack_tmr_exp_eq_retry_cnt_max_retry,
	.next_state = LLC_STATION_STATE_UP,
	.ev_actions = llc_stat_dupaddr_state_actions_5,
};

/* state transition for LLC_STATION_EV_DISABLE_REQ event */
static llc_station_action_t llc_stat_dupaddr_state_actions_6[] = {
	[0] = llc_station_ac_report_status,	/* STATION DOWN */
	[1] = NULL,
};

static struct llc_station_state_trans llc_stat_dupaddr_state_trans_6 = {
	.ev	    = llc_stat_ev_disable_req,
	.next_state = LLC_STATION_STATE_DOWN,
	.ev_actions = llc_stat_dupaddr_state_actions_6,
};

/* array of pointers; one to each transition */
static struct llc_station_state_trans *llc_stat_dupaddr_state_trans[] = {
	[0] = &llc_stat_dupaddr_state_trans_6,	/* Request */
	[1] = &llc_stat_dupaddr_state_trans_4,	/* Timer */
	[2] = &llc_stat_dupaddr_state_trans_5,
	[3] = &llc_stat_dupaddr_state_trans_1,	/* Receive frame */
	[4] = &llc_stat_dupaddr_state_trans_2,
	[5] = &llc_stat_dupaddr_state_trans_3,
	[6] = &llc_stat_state_trans_end,
};

static struct llc_station_state
			llc_station_state_table[LLC_NBR_STATION_STATES] = {
	[LLC_STATION_STATE_DOWN - 1] = {
		.curr_state  = LLC_STATION_STATE_DOWN,
		.transitions = llc_stat_dwn_state_trans,
	},
	[LLC_STATION_STATE_DUP_ADDR_CHK - 1] = {
		.curr_state  = LLC_STATION_STATE_DUP_ADDR_CHK,
		.transitions = llc_stat_dupaddr_state_trans,
	},
	[LLC_STATION_STATE_UP - 1] = {
		.curr_state  = LLC_STATION_STATE_UP,
		.transitions = llc_stat_up_state_trans,
	},
};

/**
 *	llc_exec_station_trans_actions - executes actions for transition
 *	@trans: Address of the transition
 *	@skb: Address of the event that caused the transition
 *
 *	Executes actions of a transition of the station state machine. Returns
 *	0 if all actions complete successfully, nonzero otherwise.
 */
static u16 llc_exec_station_trans_actions(struct llc_station_state_trans *trans,
					  struct sk_buff *skb)
{
	u16 rc = 0;
	llc_station_action_t *next_action = trans->ev_actions;

	for (; next_action && *next_action; next_action++)
		if ((*next_action)(skb))
			rc = 1;
	return rc;
}

/**
 *	llc_find_station_trans - finds transition for this event
 *	@skb: Address of the event
 *
 *	Search thru events of the current state of the station until list
 *	exhausted or it's obvious that the event is not valid for the current
 *	state. Returns the address of the transition if cound, %NULL otherwise.
 */
static struct llc_station_state_trans *
				llc_find_station_trans(struct sk_buff *skb)
{
	int i = 0;
	struct llc_station_state_trans *rc = NULL;
	struct llc_station_state_trans **next_trans;
	struct llc_station_state *curr_state =
				&llc_station_state_table[llc_main_station.state - 1];

	for (next_trans = curr_state->transitions; next_trans[i]->ev; i++)
		if (!next_trans[i]->ev(skb)) {
			rc = next_trans[i];
			break;
		}
	return rc;
}

/**
 *	llc_station_free_ev - frees an event
 *	@skb: Address of the event
 *
 *	Frees an event.
 */
static void llc_station_free_ev(struct sk_buff *skb)
{
	struct llc_station_state_ev *ev = llc_station_ev(skb);

	if (ev->type == LLC_STATION_EV_TYPE_PDU)
		kfree_skb(skb);
}

/**
 *	llc_station_next_state - processes event and goes to the next state
 *	@skb: Address of the event
 *
 *	Processes an event, executes any transitions related to that event and
 *	updates the state of the station.
 */
static u16 llc_station_next_state(struct sk_buff *skb)
{
	u16 rc = 1;
	struct llc_station_state_trans *trans;

	if (llc_main_station.state > LLC_NBR_STATION_STATES)
		goto out;
	trans = llc_find_station_trans(skb);
	if (trans) {
		/* got the state to which we next transition; perform the
		 * actions associated with this transition before actually
		 * transitioning to the next state
		 */
		rc = llc_exec_station_trans_actions(trans, skb);
		if (!rc)
			/* transition station to next state if all actions
			 * execute successfully; done; wait for next event
			 */
			llc_main_station.state = trans->next_state;
	} else
		/* event not recognized in current state; re-queue it for
		 * processing again at a later time; return failure
		 */
		rc = 0;
out:
	llc_station_free_ev(skb);
	return rc;
}

/**
 *	llc_station_service_events - service events in the queue
 *
 *	Get an event from the station event queue (if any); attempt to service
 *	the event; if event serviced, get the next event (if any) on the event
 *	queue; if event not service, re-queue the event on the event queue and
 *	attempt to service the next event; when serviced all events in queue,
 *	finished; if don't transition to different state, just service all
 *	events once; if transition to new state, service all events again.
 *	Caller must hold llc_main_station.ev_q.lock.
 */
static void llc_station_service_events(void)
{
	struct sk_buff *skb;

	while ((skb = skb_dequeue(&llc_main_station.ev_q.list)) != NULL)
		llc_station_next_state(skb);
}

/**
 *	llc_station_state_process - queue event and try to process queue.
 *	@skb: Address of the event
 *
 *	Queues an event (on the station event queue) for handling by the
 *	station state machine and attempts to process any queued-up events.
 */
static void llc_station_state_process(struct sk_buff *skb)
{
	spin_lock_bh(&llc_main_station.ev_q.lock);
	skb_queue_tail(&llc_main_station.ev_q.list, skb);
	llc_station_service_events();
	spin_unlock_bh(&llc_main_station.ev_q.lock);
}

static void llc_station_ack_tmr_cb(unsigned long timeout_data)
{
	struct sk_buff *skb = alloc_skb(0, GFP_ATOMIC);

	if (skb) {
		struct llc_station_state_ev *ev = llc_station_ev(skb);

		ev->type = LLC_STATION_EV_TYPE_ACK_TMR;
		llc_station_state_process(skb);
	}
}

/**
 *	llc_station_rcv - send received pdu to the station state machine
 *	@skb: received frame.
 *
 *	Sends data unit to station state machine.
 */
static void llc_station_rcv(struct sk_buff *skb)
{
	struct llc_station_state_ev *ev = llc_station_ev(skb);

	ev->type   = LLC_STATION_EV_TYPE_PDU;
	ev->reason = 0;
	llc_station_state_process(skb);
}

void __init llc_station_init(void)
{
	skb_queue_head_init(&llc_main_station.mac_pdu_q);
	skb_queue_head_init(&llc_main_station.ev_q.list);
	spin_lock_init(&llc_main_station.ev_q.lock);
	setup_timer(&llc_main_station.ack_timer, llc_station_ack_tmr_cb,
			(unsigned long)&llc_main_station);
	llc_main_station.ack_timer.expires  = jiffies +
						sysctl_llc_station_ack_timeout;
	llc_main_station.maximum_retry	= 1;
	llc_main_station.state		= LLC_STATION_STATE_UP;
	llc_set_station_handler(llc_station_rcv);
}

void llc_station_exit(void)
{
	llc_set_station_handler(NULL);
}
