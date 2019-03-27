/*-
 * Copyright (c) 2015,2016 Annapurna Labs Ltd. and affiliates
 * All rights reserved.
 *
 * Developed by Semihalf.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED. IN NO EVENT SHALL THE AUTHOR OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 */

#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");

#include "al_init_eth_kr.h"
#include "al_serdes.h"

/**
 *  Ethernet
 *  @{
 * @file   al_init_eth_kr.c
 *
 * @brief  auto-negotiation and link training algorithms and state machines
 *
 * The link training algorithm implemented in this file going over the
 * coefficients and looking for the best eye measurement possible for every one
 * of them. it's using state machine to move between the different states.
 * the state machine has 3 parts:
 *	- preparation - waiting till the link partner (lp) will be ready and
 *			change his state to preset.
 *	- measurement (per coefficient) - issue decrement for the coefficient
 *			under control till the eye measurement not increasing
 *			and remains in the optimum.
 *	- completion - indicate the receiver is ready and wait for the lp to
 *		       finish his work.
 */

/* TODO: fix with more reasonable numbers */
/* timeout in mSec before auto-negotiation will be terminated */
#define AL_ETH_KR_AN_TIMEOUT		(500)
#define AL_ETH_KR_EYE_MEASURE_TIMEOUT	(100)
/* timeout in uSec before the process will be terminated */
#define AL_ETH_KR_FRAME_LOCK_TIMEOUT	(500 * 1000)
#define AL_ETH_KR_LT_DONE_TIMEOUT	(500 * 1000)
/* number of times the receiver and transmitter tasks will be called before the
 * algorithm will be terminated */
#define AL_ETH_KR_LT_MAX_ROUNDS		(50000)

/* mac algorithm state machine */
enum al_eth_kr_mac_lt_state {
	TX_INIT = 0,	/* start of all */
	WAIT_BEGIN,	/* wait for initial training lock */
	DO_PRESET,	/* issue PRESET to link partner */
	DO_HOLD,	/* issue HOLD to link partner */
	/* preparation is done, start testing the coefficient. */
	QMEASURE,	/* EyeQ measurement. */
	QCHECK,		/* Check if measurement shows best value. */
	DO_NEXT_TRY,	/* issue DEC command to coeff for next measurement. */
	END_STEPS,	/* perform last steps to go back to optimum. */
	END_STEPS_HOLD,	/* perform last steps HOLD command. */
	COEFF_DONE,	/* done with the current coefficient updates.
			 * Check if another should be done. */
	/* end of training to all coefficients */
	SET_READY,	/* indicate local receiver ready */
	TX_DONE		/* transmit process completed, training can end. */
};

static const char * const al_eth_kr_mac_sm_name[] = {
	"TX_INIT", "WAIT_BEGIN", "DO_PRESET",
	"DO_HOLD", "QMEASURE", "QCHECK",
	"DO_NEXT_TRY", "END_STEPS", "END_STEPS_HOLD",
	"COEFF_DONE", "SET_READY", "TX_DONE"
};

/* Constants used for the measurement. */
enum al_eth_kr_coef {
	AL_ETH_KR_COEF_C_MINUS,
	AL_ETH_KR_COEF_C_ZERO,
	AL_ETH_KR_COEF_C_PLUS,
};

/*
 * test coefficients from COEFF_TO_MANIPULATE to COEFF_TO_MANIPULATE_LAST.
 */
#define COEFF_TO_MANIPULATE AL_ETH_KR_COEF_C_MINUS
#define COEFF_TO_MANIPULATE_LAST AL_ETH_KR_COEF_C_MINUS
#define QARRAY_SIZE	3 /**< how many entries we want in our history array. */

struct al_eth_kr_data {
	struct al_hal_eth_adapter	*adapter;
	struct al_serdes_grp_obj	*serdes_obj;
	enum al_serdes_lane		lane;

	/* Receiver side data */
	struct al_eth_kr_status_report_data status_report; /* report to response */
	struct al_eth_kr_coef_up_data last_lpcoeff; /* last coeff received */

	/* Transmitter side data */
	enum al_eth_kr_mac_lt_state algo_state;	/* Statemachine. */
	unsigned int qarray[QARRAY_SIZE];	/* EyeQ measurements history */
	/* How many entries in the array are valid for compares yet. */
	unsigned int qarray_cnt;
	enum al_eth_kr_coef curr_coeff;
	/*
	 * Status of coefficient during the last
	 * DEC/INC command (before issuing HOLD again).
	 */
	unsigned int coeff_status_step;
	unsigned int end_steps_cnt;     /* Number of end steps needed */
};

static int
al_eth_kr_an_run(struct al_eth_kr_data *kr_data, struct al_eth_an_adv *an_adv,
    struct al_eth_an_adv *an_partner_adv)
{
	int rc;
	boolean_t page_received = FALSE;
	boolean_t an_completed = FALSE;
	boolean_t error = FALSE;
	int timeout = AL_ETH_KR_AN_TIMEOUT;

	rc = al_eth_kr_an_init(kr_data->adapter, an_adv);
	if (rc != 0) {
		al_err("%s %s autonegotiation init failed\n",
		    kr_data->adapter->name, __func__);
		return (rc);
	}

	rc = al_eth_kr_an_start(kr_data->adapter, AL_ETH_AN__LT_LANE_0,
	    FALSE, TRUE);
	if (rc != 0) {
		al_err("%s %s autonegotiation enable failed\n",
		    kr_data->adapter->name, __func__);
		return (rc);
	}

	do {
		DELAY(10000);
		timeout -= 10;
		if (timeout <= 0) {
			al_info("%s %s autonegotiation failed on timeout\n",
			    kr_data->adapter->name, __func__);

			return (ETIMEDOUT);
		}

		al_eth_kr_an_status_check(kr_data->adapter, &page_received,
		    &an_completed, &error);
	} while (page_received == FALSE);

	if (error != 0) {
		al_info("%s %s autonegotiation failed (status error)\n",
		    kr_data->adapter->name, __func__);

		return (EIO);
	}

	al_eth_kr_an_read_adv(kr_data->adapter, an_partner_adv);

	al_dbg("%s %s autonegotiation completed. error = %d\n",
	    kr_data->adapter->name, __func__, error);

	return (0);
}

/***************************** receiver side *********************************/
static enum al_eth_kr_cl72_cstate
al_eth_lt_coeff_set(struct al_eth_kr_data *kr_data,
    enum al_serdes_tx_deemph_param param, uint32_t op)
{
	enum al_eth_kr_cl72_cstate status = 0;

	switch (op) {
	case AL_PHY_KR_COEF_UP_HOLD:
		/* no need to update the serdes - return not updated*/
		status = C72_CSTATE_NOT_UPDATED;
		break;
	case AL_PHY_KR_COEF_UP_INC:
		status = C72_CSTATE_UPDATED;

		if (kr_data->serdes_obj->tx_deemph_inc(
					kr_data->serdes_obj,
					kr_data->lane,
					param) == 0)
			status = C72_CSTATE_MAX;
		break;
	case AL_PHY_KR_COEF_UP_DEC:
		status = C72_CSTATE_UPDATED;

		if (kr_data->serdes_obj->tx_deemph_dec(
					kr_data->serdes_obj,
					kr_data->lane,
					param) == 0)
			status = C72_CSTATE_MIN;
		break;
	default: /* 3=reserved */
		break;
	}

	return (status);
}

/*
 * Inspect the received coefficient update request and update all coefficients
 * in the serdes accordingly.
 */
static void
al_eth_coeff_req_handle(struct al_eth_kr_data *kr_data,
    struct al_eth_kr_coef_up_data *lpcoeff)
{
	struct al_eth_kr_status_report_data *report = &kr_data->status_report;

	/* First check for Init and Preset commands. */
	if ((lpcoeff->preset != 0) || (lpcoeff->initialize) != 0) {
		kr_data->serdes_obj->tx_deemph_preset(
					kr_data->serdes_obj,
					kr_data->lane);

		/*
		 * in case of preset c(0) should be set to maximum and both c(1)
		 * and c(-1) should be updated
		 */
		report->c_minus = C72_CSTATE_UPDATED;

		report->c_plus = C72_CSTATE_UPDATED;

		report->c_zero = C72_CSTATE_MAX;

		return;
	}

	/*
	 * in case preset and initialize are false need to perform per
	 * coefficient action.
	 */
	report->c_minus = al_eth_lt_coeff_set(kr_data,
	    AL_SERDES_TX_DEEMP_C_MINUS, lpcoeff->c_minus);

	report->c_zero = al_eth_lt_coeff_set(kr_data,
	    AL_SERDES_TX_DEEMP_C_ZERO, lpcoeff->c_zero);

	report->c_plus = al_eth_lt_coeff_set(kr_data,
	    AL_SERDES_TX_DEEMP_C_PLUS, lpcoeff->c_plus);

	al_dbg("%s: c(0) = 0x%x c(-1) = 0x%x c(1) = 0x%x\n",
	    __func__, report->c_zero, report->c_plus, report->c_minus);
}

static void
al_eth_kr_lt_receiver_task_init(struct al_eth_kr_data *kr_data)
{

	al_memset(&kr_data->last_lpcoeff, 0,
	    sizeof(struct al_eth_kr_coef_up_data));
	al_memset(&kr_data->status_report, 0,
	    sizeof(struct al_eth_kr_status_report_data));
}

static boolean_t
al_eth_lp_coeff_up_change(struct al_eth_kr_data *kr_data,
    struct al_eth_kr_coef_up_data *lpcoeff)
{
	struct al_eth_kr_coef_up_data *last_lpcoeff = &kr_data->last_lpcoeff;

	if (al_memcmp(last_lpcoeff, lpcoeff,
	    sizeof(struct al_eth_kr_coef_up_data)) == 0) {
		return (FALSE);
	}

	al_memcpy(last_lpcoeff, lpcoeff, sizeof(struct al_eth_kr_coef_up_data));

	return (TRUE);
}

/*
 * Run the receiver task for one cycle.
 * The receiver task continuously inspects the received coefficient update
 * requests and acts upon.
 *
 * @return <0 if error occur
 */
static int
al_eth_kr_lt_receiver_task_run(struct al_eth_kr_data *kr_data)
{
	struct al_eth_kr_coef_up_data new_lpcoeff;

	/*
	 * First inspect status of the link. It may have dropped frame lock as
	 * the remote did some reconfiguration of its serdes.
	 * Then we simply have nothing to do and return immediately as caller
	 * will call us continuously until lock comes back.
	 */

	if (al_eth_kr_receiver_frame_lock_get(kr_data->adapter,
	    AL_ETH_AN__LT_LANE_0) != 0) {
		return (0);
	}

	/* check if a new update command was received */
	al_eth_lp_coeff_up_get(kr_data->adapter,
	    AL_ETH_AN__LT_LANE_0, &new_lpcoeff);

	if (al_eth_lp_coeff_up_change(kr_data, &new_lpcoeff) != 0) {
		/* got some new coefficient update request. */
		al_eth_coeff_req_handle(kr_data, &new_lpcoeff);
	}

	return (0);
}

/******************************** transmitter side ***************************/
static int
al_eth_kr_lt_transmitter_task_init(struct al_eth_kr_data *kr_data)
{
	int i;
	int rc;
	unsigned int temp_val;

	for (i = 0; i < QARRAY_SIZE; i++)
		kr_data->qarray[i] = 0;

	kr_data->qarray_cnt = 0;
	kr_data->algo_state = TX_INIT;
	kr_data->curr_coeff = COEFF_TO_MANIPULATE;  /* first coeff to test. */
	kr_data->coeff_status_step  = C72_CSTATE_NOT_UPDATED;
	kr_data->end_steps_cnt = QARRAY_SIZE-1;  /* go back to first entry */

	/*
	 * Perform measure eye here to run the rx equalizer
	 * for the first time to get init values
	 */
	rc = kr_data->serdes_obj->eye_measure_run(
				kr_data->serdes_obj,
				kr_data->lane,
				AL_ETH_KR_EYE_MEASURE_TIMEOUT,
				&temp_val);
	if (rc != 0) {
		al_warn("%s: Failed to run Rx equalizer (rc = 0x%x)\n",
		    __func__, rc);

		return (rc);
	}

	return (0);
}

static boolean_t
al_eth_kr_lt_all_not_updated(struct al_eth_kr_status_report_data *report)
{

	if ((report->c_zero == C72_CSTATE_NOT_UPDATED) &&
	    (report->c_minus == C72_CSTATE_NOT_UPDATED) &&
	    (report->c_plus == C72_CSTATE_NOT_UPDATED)) {
		return (TRUE);
	}

	return (FALSE);
}

static void
al_eth_kr_lt_coef_set(struct al_eth_kr_coef_up_data *ldcoeff,
    enum al_eth_kr_coef coef, enum al_eth_kr_cl72_coef_op op)
{

	switch (coef) {
	case AL_ETH_KR_COEF_C_MINUS:
		ldcoeff->c_minus = op;
		break;
	case AL_ETH_KR_COEF_C_PLUS:
		ldcoeff->c_plus = op;
		break;
	case AL_ETH_KR_COEF_C_ZERO:
		ldcoeff->c_zero = op;
		break;
	}
}

static enum al_eth_kr_cl72_cstate
al_eth_kr_lt_coef_report_get(struct al_eth_kr_status_report_data *report,
    enum al_eth_kr_coef coef)
{

	switch (coef) {
	case AL_ETH_KR_COEF_C_MINUS:
		return (report->c_minus);
	case AL_ETH_KR_COEF_C_PLUS:
		return (report->c_plus);
	case AL_ETH_KR_COEF_C_ZERO:
		return (report->c_zero);
	}

	return (0);
}

/*
 * Run the transmitter_task for one cycle.
 *
 * @return <0 if error occurs
 */
static int
al_eth_kr_lt_transmitter_task_run(struct al_eth_kr_data *kr_data)
{
	struct al_eth_kr_status_report_data report;
	unsigned int coeff_status_cur;
	struct al_eth_kr_coef_up_data ldcoeff = { 0, 0, 0, 0, 0 };
	unsigned int val;
	int i;
	enum al_eth_kr_mac_lt_state nextstate;
	int rc = 0;

	/*
	 * do nothing if currently there is no frame lock (which may happen
	 * when remote updates its analogs).
	 */
	if (al_eth_kr_receiver_frame_lock_get(kr_data->adapter,
	    AL_ETH_AN__LT_LANE_0) == 0) {
		return (0);
	}

	al_eth_lp_status_report_get(kr_data->adapter,
	    AL_ETH_AN__LT_LANE_0, &report);

	/* extract curr status of the coefficient in use */
	coeff_status_cur = al_eth_kr_lt_coef_report_get(&report,
	    kr_data->curr_coeff);

	nextstate = kr_data->algo_state; /* default we stay in curr state; */

	switch (kr_data->algo_state) {
	case TX_INIT:
		/* waiting for start */
		if (al_eth_kr_startup_proto_prog_get(kr_data->adapter,
		    AL_ETH_AN__LT_LANE_0) != 0) {
			/* training is on and frame lock */
			nextstate = WAIT_BEGIN;
		}
		break;
	case WAIT_BEGIN:
		kr_data->qarray_cnt = 0;
		kr_data->curr_coeff = COEFF_TO_MANIPULATE;
		kr_data->coeff_status_step = C72_CSTATE_NOT_UPDATED;
		coeff_status_cur = C72_CSTATE_NOT_UPDATED;
		kr_data->end_steps_cnt = QARRAY_SIZE-1;

		/* Wait for not_updated for all coefficients from remote */
		if (al_eth_kr_lt_all_not_updated(&report) != 0) {
			ldcoeff.preset = TRUE;
			nextstate = DO_PRESET;
		}
		break;
	case DO_PRESET:
		/*
		 * Send PRESET and wait for for updated for all
		 * coefficients from remote
		 */
		if (al_eth_kr_lt_all_not_updated(&report) == 0)
			nextstate = DO_HOLD;
		else /* as long as the lp didn't response to the preset
		      * we should continue sending it */
			ldcoeff.preset = TRUE;
		break;
	case DO_HOLD:
		/*
		 * clear the PRESET, issue HOLD command and wait for
		 * hold handshake
		 */
		if (al_eth_kr_lt_all_not_updated(&report) != 0)
			nextstate = QMEASURE;
		break;

	case QMEASURE:
		/* makes a measurement and fills the new value into the array */
		rc = kr_data->serdes_obj->eye_measure_run(
					kr_data->serdes_obj,
					kr_data->lane,
					AL_ETH_KR_EYE_MEASURE_TIMEOUT,
					&val);
		if (rc != 0) {
			al_warn("%s: Rx eye measurement failed\n", __func__);

			return (rc);
		}

		al_dbg("%s: Rx Measure eye returned 0x%x\n", __func__, val);

		/* put the new value into the array at the top. */
		for (i = 0; i < QARRAY_SIZE-1; i++)
			kr_data->qarray[i] = kr_data->qarray[i+1];

		kr_data->qarray[QARRAY_SIZE-1] = val;

		if (kr_data->qarray_cnt < QARRAY_SIZE)
			kr_data->qarray_cnt++;

		nextstate = QCHECK;
		break;
	case QCHECK:
		/* check if we reached the best link quality yet. */
		if (kr_data->qarray_cnt < QARRAY_SIZE) {
			/* keep going until at least the history is
			 * filled. check that we can keep going or if
			 * coefficient has already reached minimum.
			 */

			if (kr_data->coeff_status_step == C72_CSTATE_MIN)
				nextstate = COEFF_DONE;
			else {
				/*
				 * request a DECREMENT of the
				 * coefficient under control
				 */
				al_eth_kr_lt_coef_set(&ldcoeff,
				    kr_data->curr_coeff, AL_PHY_KR_COEF_UP_DEC);

				nextstate = DO_NEXT_TRY;
			}
		} else {
			/*
			 * check if current value and last both are worse than
			 * the 2nd last. This we take as an ending condition
			 * assuming the minimum was reached two tries before
			 * so we will now go back to that point.
			 */
			if ((kr_data->qarray[0] < kr_data->qarray[1]) &&
			    (kr_data->qarray[0] < kr_data->qarray[2])) {
				/*
				 * request a INCREMENT of the
				 * coefficient under control
				 */
				al_eth_kr_lt_coef_set(&ldcoeff,
				    kr_data->curr_coeff, AL_PHY_KR_COEF_UP_INC);

				/* start going back to the maximum */
				nextstate = END_STEPS;
				if (kr_data->end_steps_cnt > 0)
					kr_data->end_steps_cnt--;
			} else {
				if (kr_data->coeff_status_step ==
				    C72_CSTATE_MIN) {
					nextstate = COEFF_DONE;
				} else {
					/*
					 * request a DECREMENT of the
					 * coefficient under control
					 */
					al_eth_kr_lt_coef_set(&ldcoeff,
					    kr_data->curr_coeff,
					    AL_PHY_KR_COEF_UP_DEC);

					nextstate = DO_NEXT_TRY;
				}
			}
		}
		break;
	case DO_NEXT_TRY:
		/*
		 * save the status when we issue the DEC step to the remote,
		 * before the HOLD is done again.
		 */
		kr_data->coeff_status_step = coeff_status_cur;

		if (coeff_status_cur != C72_CSTATE_NOT_UPDATED)
			nextstate = DO_HOLD;  /* go to next measurement round */
		else
			al_eth_kr_lt_coef_set(&ldcoeff,
			    kr_data->curr_coeff, AL_PHY_KR_COEF_UP_DEC);
		break;
	/*
	 * Coefficient iteration completed, go back to the optimum step
	 * In this algorithm we assume 2 before curr was best hence need to do
	 * two INC runs.
	 */
	case END_STEPS:
		if (coeff_status_cur != C72_CSTATE_NOT_UPDATED)
			nextstate = END_STEPS_HOLD;
		else
			al_eth_kr_lt_coef_set(&ldcoeff,
			    kr_data->curr_coeff, AL_PHY_KR_COEF_UP_INC);
		break;
	case END_STEPS_HOLD:
		if (coeff_status_cur == C72_CSTATE_NOT_UPDATED) {
			if (kr_data->end_steps_cnt != 0) {
				/*
				 * request a INCREMENT of the
				 * coefficient under control
				 */
				al_eth_kr_lt_coef_set(&ldcoeff,
				    kr_data->curr_coeff, AL_PHY_KR_COEF_UP_INC);

				/* go 2nd time - dec the end step count */
				nextstate = END_STEPS;

				if (kr_data->end_steps_cnt > 0)
					kr_data->end_steps_cnt--;

			} else {
				nextstate = COEFF_DONE;
			}
		}
		break;
	case COEFF_DONE:
		/*
		 * now this coefficient is done.
		 * We can now either choose to finish here,
		 * or keep going with another coefficient.
		 */
		if ((int)kr_data->curr_coeff < COEFF_TO_MANIPULATE_LAST) {
			int i;

			for (i = 0; i < QARRAY_SIZE; i++)
				kr_data->qarray[i] = 0;

			kr_data->qarray_cnt = 0;
			kr_data->end_steps_cnt = QARRAY_SIZE-1;
			kr_data->coeff_status_step = C72_CSTATE_NOT_UPDATED;
			kr_data->curr_coeff++;

			al_dbg("[%s]: doing next coefficient: %d ---\n\n",
			    kr_data->adapter->name, kr_data->curr_coeff);

			nextstate = QMEASURE;
		} else {
			nextstate = SET_READY;
		}
		break;
	case SET_READY:
		/*
		 * our receiver is ready for data.
		 * no training will occur any more.
		 */
		kr_data->status_report.receiver_ready = TRUE;
		/*
		 * in addition to the status we transmit, we also must tell our
		 * local hardware state-machine that we are done, so the
		 * training can eventually complete when the remote indicates
		 * it is ready also. The hardware will then automatically
		 * give control to the PCS layer completing training.
		 */
		al_eth_receiver_ready_set(kr_data->adapter,
		    AL_ETH_AN__LT_LANE_0);

		nextstate = TX_DONE;
		break;
	case TX_DONE:
		break;  /* nothing else to do */
	default:
		nextstate = kr_data->algo_state;
		break;
	}

	/*
	 * The status we want to transmit to remote.
	 * Note that the status combines the receiver status of all coefficients
	 * with the transmitter's rx ready status.
	 */
	if (kr_data->algo_state != nextstate) {
		al_dbg("[%s] [al_eth_kr_lt_transmit_run] STM changes %s -> %s: "
		    " Qarray=%d/%d/%d\n", kr_data->adapter->name,
		    al_eth_kr_mac_sm_name[kr_data->algo_state],
		    al_eth_kr_mac_sm_name[nextstate],
		    kr_data->qarray[0], kr_data->qarray[1], kr_data->qarray[2]);
	}

	kr_data->algo_state = nextstate;

	/*
	 * write fields for transmission into hardware.
	 * Important: this must be done always, as the receiver may have
	 * received update commands and wants to return its status.
	 */
	al_eth_ld_coeff_up_set(kr_data->adapter, AL_ETH_AN__LT_LANE_0, &ldcoeff);
	al_eth_ld_status_report_set(kr_data->adapter, AL_ETH_AN__LT_LANE_0,
	    &kr_data->status_report);

	return (0);
}

/*****************************************************************************/
static int
al_eth_kr_run_lt(struct al_eth_kr_data *kr_data)
{
	unsigned int cnt;
	int ret = 0;
	boolean_t page_received = FALSE;
	boolean_t an_completed = FALSE;
	boolean_t error = FALSE;
	boolean_t training_failure = FALSE;

	al_eth_kr_lt_initialize(kr_data->adapter, AL_ETH_AN__LT_LANE_0);

	if (al_eth_kr_lt_frame_lock_wait(kr_data->adapter, AL_ETH_AN__LT_LANE_0,
	    AL_ETH_KR_FRAME_LOCK_TIMEOUT) == TRUE) {

		/*
		 * when locked, for the first time initialize the receiver and
		 * transmitter tasks to prepare it for detecting coefficient
		 * update requests.
		 */
		al_eth_kr_lt_receiver_task_init(kr_data);
		ret = al_eth_kr_lt_transmitter_task_init(kr_data);
		if (ret != 0)
			goto error;

		cnt = 0;
		do {
			ret = al_eth_kr_lt_receiver_task_run(kr_data);
			if (ret != 0)
				break; /* stop the link training */

			ret = al_eth_kr_lt_transmitter_task_run(kr_data);
			if (ret != 0)
				break;  /* stop the link training */

			cnt++;
			DELAY(100);

		} while ((al_eth_kr_startup_proto_prog_get(kr_data->adapter,
		    AL_ETH_AN__LT_LANE_0)) && (cnt <= AL_ETH_KR_LT_MAX_ROUNDS));

		training_failure =
		    al_eth_kr_training_status_fail_get(kr_data->adapter,
		    AL_ETH_AN__LT_LANE_0);
		al_dbg("[%s] training ended after %d rounds, failed = %s\n",
		    kr_data->adapter->name, cnt,
		    (training_failure) ? "Yes" : "No");
		if (training_failure || cnt > AL_ETH_KR_LT_MAX_ROUNDS) {
			al_warn("[%s] Training Fail: status: %s, timeout: %s\n",
			    kr_data->adapter->name,
			    (training_failure) ? "Failed" : "OK",
			    (cnt > AL_ETH_KR_LT_MAX_ROUNDS) ? "Yes" : "No");

			/*
			 * note: link is now disabled,
			 * until training becomes disabled (see below).
			 */
			ret = EIO;
			goto error;
		}

	} else {

		al_info("[%s] FAILED: did not achieve initial frame lock...\n",
		    kr_data->adapter->name);

		ret = EIO;
		goto error;
	}

	/*
	 * ensure to stop link training at the end to allow normal PCS
	 * datapath to operate in case of training failure.
	 */
	al_eth_kr_lt_stop(kr_data->adapter, AL_ETH_AN__LT_LANE_0);

	cnt = AL_ETH_KR_LT_DONE_TIMEOUT;
	while (an_completed == FALSE) {
		al_eth_kr_an_status_check(kr_data->adapter, &page_received,
		    &an_completed, &error);
		DELAY(1);
		if ((cnt--) == 0) {
			al_info("%s: wait for an complete timeout!\n", __func__);
			ret = ETIMEDOUT;
			goto error;
		}
	}

error:
	al_eth_kr_an_stop(kr_data->adapter);

	return (ret);
}

/* execute Autonegotiation process */
int al_eth_an_lt_execute(struct al_hal_eth_adapter	*adapter,
			 struct al_serdes_grp_obj	*serdes_obj,
			 enum al_serdes_lane		lane,
			 struct al_eth_an_adv		*an_adv,
			 struct al_eth_an_adv		*partner_adv)
{
	struct al_eth_kr_data kr_data;
	int rc;
	struct al_serdes_adv_rx_params rx_params;

	al_memset(&kr_data, 0, sizeof(struct al_eth_kr_data));

	kr_data.adapter = adapter;
	kr_data.serdes_obj = serdes_obj;
	kr_data.lane = lane;

	/*
	 * the link training progress will run rx equalization so need to make
	 * sure rx parameters is not been override
	 */
	rx_params.override = FALSE;
	kr_data.serdes_obj->rx_advanced_params_set(
					kr_data.serdes_obj,
					kr_data.lane,
					&rx_params);

	rc = al_eth_kr_an_run(&kr_data, an_adv, partner_adv);
	if (rc != 0) {
		al_eth_kr_lt_stop(adapter, AL_ETH_AN__LT_LANE_0);
		al_eth_kr_an_stop(adapter);
		al_dbg("%s: auto-negotiation failed!\n", __func__);
		return (rc);
	}

	if (partner_adv->technology != AL_ETH_AN_TECH_10GBASE_KR) {
		al_eth_kr_lt_stop(adapter, AL_ETH_AN__LT_LANE_0);
		al_eth_kr_an_stop(adapter);
		al_dbg("%s: link partner isn't 10GBASE_KR.\n", __func__);
		return (rc);
	}

	rc = al_eth_kr_run_lt(&kr_data);
	if (rc != 0) {
		al_eth_kr_lt_stop(adapter, AL_ETH_AN__LT_LANE_0);
		al_eth_kr_an_stop(adapter);
		al_dbg("%s: Link-training failed!\n", __func__);
		return (rc);
	}

	return (0);
}
