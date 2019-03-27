/*-
 * SPDX-License-Identifier: BSD-3-Clause
 *
 * Copyright (c) 2001-2007, by Cisco Systems, Inc. All rights reserved.
 * Copyright (c) 2008-2012, by Randall Stewart. All rights reserved.
 * Copyright (c) 2008-2012, by Michael Tuexen. All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are met:
 *
 * a) Redistributions of source code must retain the above copyright notice,
 *    this list of conditions and the following disclaimer.
 *
 * b) Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in
 *    the documentation and/or other materials provided with the distribution.
 *
 * c) Neither the name of Cisco Systems, Inc. nor the names of its
 *    contributors may be used to endorse or promote products derived
 *    from this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 * "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO,
 * THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT OWNER OR CONTRIBUTORS BE
 * LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
 * CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 * SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
 * INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
 * CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 * ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF
 * THE POSSIBILITY OF SUCH DAMAGE.
 */

#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");

#include <netinet/sctp_os.h>
#include <netinet/sctp_var.h>
#include <netinet/sctp_sysctl.h>
#include <netinet/sctp_pcb.h>
#include <netinet/sctp_header.h>
#include <netinet/sctputil.h>
#include <netinet/sctp_output.h>
#include <netinet/sctp_input.h>
#include <netinet/sctp_indata.h>
#include <netinet/sctp_uio.h>
#include <netinet/sctp_timer.h>
#include <netinet/sctp_auth.h>
#include <netinet/sctp_asconf.h>
#include <netinet/sctp_dtrace_declare.h>

#define SHIFT_MPTCP_MULTI_N 40
#define SHIFT_MPTCP_MULTI_Z 16
#define SHIFT_MPTCP_MULTI 8

static void
sctp_enforce_cwnd_limit(struct sctp_association *assoc, struct sctp_nets *net)
{
	if ((assoc->max_cwnd > 0) &&
	    (net->cwnd > assoc->max_cwnd) &&
	    (net->cwnd > (net->mtu - sizeof(struct sctphdr)))) {
		net->cwnd = assoc->max_cwnd;
		if (net->cwnd < (net->mtu - sizeof(struct sctphdr))) {
			net->cwnd = net->mtu - sizeof(struct sctphdr);
		}
	}
}

static void
sctp_set_initial_cc_param(struct sctp_tcb *stcb, struct sctp_nets *net)
{
	struct sctp_association *assoc;
	uint32_t cwnd_in_mtu;

	assoc = &stcb->asoc;
	cwnd_in_mtu = SCTP_BASE_SYSCTL(sctp_initial_cwnd);
	if (cwnd_in_mtu == 0) {
		/* Using 0 means that the value of RFC 4960 is used. */
		net->cwnd = min((net->mtu * 4), max((2 * net->mtu), SCTP_INITIAL_CWND));
	} else {
		/*
		 * We take the minimum of the burst limit and the initial
		 * congestion window.
		 */
		if ((assoc->max_burst > 0) && (cwnd_in_mtu > assoc->max_burst))
			cwnd_in_mtu = assoc->max_burst;
		net->cwnd = (net->mtu - sizeof(struct sctphdr)) * cwnd_in_mtu;
	}
	if ((stcb->asoc.sctp_cmt_on_off == SCTP_CMT_RPV1) ||
	    (stcb->asoc.sctp_cmt_on_off == SCTP_CMT_RPV2)) {
		/* In case of resource pooling initialize appropriately */
		net->cwnd /= assoc->numnets;
		if (net->cwnd < (net->mtu - sizeof(struct sctphdr))) {
			net->cwnd = net->mtu - sizeof(struct sctphdr);
		}
	}
	sctp_enforce_cwnd_limit(assoc, net);
	net->ssthresh = assoc->peers_rwnd;
	SDT_PROBE5(sctp, cwnd, net, init,
	    stcb->asoc.my_vtag, ((stcb->sctp_ep->sctp_lport << 16) | (stcb->rport)), net,
	    0, net->cwnd);
	if (SCTP_BASE_SYSCTL(sctp_logging_level) &
	    (SCTP_CWND_MONITOR_ENABLE | SCTP_CWND_LOGGING_ENABLE)) {
		sctp_log_cwnd(stcb, net, 0, SCTP_CWND_INITIALIZATION);
	}
}

static void
sctp_cwnd_update_after_fr(struct sctp_tcb *stcb,
    struct sctp_association *asoc)
{
	struct sctp_nets *net;
	uint32_t t_ssthresh, t_cwnd;
	uint64_t t_ucwnd_sbw;

	/* MT FIXME: Don't compute this over and over again */
	t_ssthresh = 0;
	t_cwnd = 0;
	t_ucwnd_sbw = 0;
	if ((asoc->sctp_cmt_on_off == SCTP_CMT_RPV1) ||
	    (asoc->sctp_cmt_on_off == SCTP_CMT_RPV2)) {
		TAILQ_FOREACH(net, &asoc->nets, sctp_next) {
			t_ssthresh += net->ssthresh;
			t_cwnd += net->cwnd;
			if (net->lastsa > 0) {
				t_ucwnd_sbw += (uint64_t)net->cwnd / (uint64_t)net->lastsa;
			}
		}
		if (t_ucwnd_sbw == 0) {
			t_ucwnd_sbw = 1;
		}
	}

	/*-
	 * CMT fast recovery code. Need to debug. ((sctp_cmt_on_off > 0) &&
	 * (net->fast_retran_loss_recovery == 0)))
	 */
	TAILQ_FOREACH(net, &asoc->nets, sctp_next) {
		if ((asoc->fast_retran_loss_recovery == 0) ||
		    (asoc->sctp_cmt_on_off > 0)) {
			/* out of a RFC2582 Fast recovery window? */
			if (net->net_ack > 0) {
				/*
				 * per section 7.2.3, are there any
				 * destinations that had a fast retransmit
				 * to them. If so what we need to do is
				 * adjust ssthresh and cwnd.
				 */
				struct sctp_tmit_chunk *lchk;
				int old_cwnd = net->cwnd;

				if ((asoc->sctp_cmt_on_off == SCTP_CMT_RPV1) ||
				    (asoc->sctp_cmt_on_off == SCTP_CMT_RPV2)) {
					if (asoc->sctp_cmt_on_off == SCTP_CMT_RPV1) {
						net->ssthresh = (uint32_t)(((uint64_t)4 *
						    (uint64_t)net->mtu *
						    (uint64_t)net->ssthresh) /
						    (uint64_t)t_ssthresh);

					}
					if (asoc->sctp_cmt_on_off == SCTP_CMT_RPV2) {
						uint32_t srtt;

						srtt = net->lastsa;
						/*
						 * lastsa>>3;  we don't need
						 * to devide ...
						 */
						if (srtt == 0) {
							srtt = 1;
						}
						/*
						 * Short Version => Equal to
						 * Contel Version MBe
						 */
						net->ssthresh = (uint32_t)(((uint64_t)4 *
						    (uint64_t)net->mtu *
						    (uint64_t)net->cwnd) /
						    ((uint64_t)srtt *
						    t_ucwnd_sbw));
						 /* INCREASE FACTOR */ ;
					}
					if ((net->cwnd > t_cwnd / 2) &&
					    (net->ssthresh < net->cwnd - t_cwnd / 2)) {
						net->ssthresh = net->cwnd - t_cwnd / 2;
					}
					if (net->ssthresh < net->mtu) {
						net->ssthresh = net->mtu;
					}
				} else {
					net->ssthresh = net->cwnd / 2;
					if (net->ssthresh < (net->mtu * 2)) {
						net->ssthresh = 2 * net->mtu;
					}
				}
				net->cwnd = net->ssthresh;
				sctp_enforce_cwnd_limit(asoc, net);
				SDT_PROBE5(sctp, cwnd, net, fr,
				    stcb->asoc.my_vtag, ((stcb->sctp_ep->sctp_lport << 16) | (stcb->rport)), net,
				    old_cwnd, net->cwnd);
				if (SCTP_BASE_SYSCTL(sctp_logging_level) & SCTP_CWND_MONITOR_ENABLE) {
					sctp_log_cwnd(stcb, net, (net->cwnd - old_cwnd),
					    SCTP_CWND_LOG_FROM_FR);
				}
				lchk = TAILQ_FIRST(&asoc->send_queue);

				net->partial_bytes_acked = 0;
				/* Turn on fast recovery window */
				asoc->fast_retran_loss_recovery = 1;
				if (lchk == NULL) {
					/* Mark end of the window */
					asoc->fast_recovery_tsn = asoc->sending_seq - 1;
				} else {
					asoc->fast_recovery_tsn = lchk->rec.data.tsn - 1;
				}

				/*
				 * CMT fast recovery -- per destination
				 * recovery variable.
				 */
				net->fast_retran_loss_recovery = 1;

				if (lchk == NULL) {
					/* Mark end of the window */
					net->fast_recovery_tsn = asoc->sending_seq - 1;
				} else {
					net->fast_recovery_tsn = lchk->rec.data.tsn - 1;
				}

				sctp_timer_stop(SCTP_TIMER_TYPE_SEND,
				    stcb->sctp_ep, stcb, net,
				    SCTP_FROM_SCTP_CC_FUNCTIONS + SCTP_LOC_1);
				sctp_timer_start(SCTP_TIMER_TYPE_SEND,
				    stcb->sctp_ep, stcb, net);
			}
		} else if (net->net_ack > 0) {
			/*
			 * Mark a peg that we WOULD have done a cwnd
			 * reduction but RFC2582 prevented this action.
			 */
			SCTP_STAT_INCR(sctps_fastretransinrtt);
		}
	}
}

/* Defines for instantaneous bw decisions */
#define SCTP_INST_LOOSING 1	/* Losing to other flows */
#define SCTP_INST_NEUTRAL 2	/* Neutral, no indication */
#define SCTP_INST_GAINING 3	/* Gaining, step down possible */


static int
cc_bw_same(struct sctp_tcb *stcb, struct sctp_nets *net, uint64_t nbw,
    uint64_t rtt_offset, uint64_t vtag, uint8_t inst_ind)
{
	uint64_t oth, probepoint;

	probepoint = (((uint64_t)net->cwnd) << 32);
	if (net->rtt > net->cc_mod.rtcc.lbw_rtt + rtt_offset) {
		/*
		 * rtt increased we don't update bw.. so we don't update the
		 * rtt either.
		 */
		/* Probe point 5 */
		probepoint |= ((5 << 16) | 1);
		SDT_PROBE5(sctp, cwnd, net, rttvar,
		    vtag,
		    ((net->cc_mod.rtcc.lbw << 32) | nbw),
		    ((net->cc_mod.rtcc.lbw_rtt << 32) | net->rtt),
		    net->flight_size,
		    probepoint);
		if ((net->cc_mod.rtcc.steady_step) && (inst_ind != SCTP_INST_LOOSING)) {
			if (net->cc_mod.rtcc.last_step_state == 5)
				net->cc_mod.rtcc.step_cnt++;
			else
				net->cc_mod.rtcc.step_cnt = 1;
			net->cc_mod.rtcc.last_step_state = 5;
			if ((net->cc_mod.rtcc.step_cnt == net->cc_mod.rtcc.steady_step) ||
			    ((net->cc_mod.rtcc.step_cnt > net->cc_mod.rtcc.steady_step) &&
			    ((net->cc_mod.rtcc.step_cnt % net->cc_mod.rtcc.steady_step) == 0))) {
				/* Try a step down */
				oth = net->cc_mod.rtcc.vol_reduce;
				oth <<= 16;
				oth |= net->cc_mod.rtcc.step_cnt;
				oth <<= 16;
				oth |= net->cc_mod.rtcc.last_step_state;
				SDT_PROBE5(sctp, cwnd, net, rttstep,
				    vtag,
				    ((net->cc_mod.rtcc.lbw << 32) | nbw),
				    ((net->cc_mod.rtcc.lbw_rtt << 32) | net->rtt),
				    oth,
				    probepoint);
				if (net->cwnd > (4 * net->mtu)) {
					net->cwnd -= net->mtu;
					net->cc_mod.rtcc.vol_reduce++;
				} else {
					net->cc_mod.rtcc.step_cnt = 0;
				}
			}
		}
		return (1);
	}
	if (net->rtt < net->cc_mod.rtcc.lbw_rtt - rtt_offset) {
		/*
		 * rtt decreased, there could be more room. we update both
		 * the bw and the rtt here to lock this in as a good step
		 * down.
		 */
		/* Probe point 6 */
		probepoint |= ((6 << 16) | 0);
		SDT_PROBE5(sctp, cwnd, net, rttvar,
		    vtag,
		    ((net->cc_mod.rtcc.lbw << 32) | nbw),
		    ((net->cc_mod.rtcc.lbw_rtt << 32) | net->rtt),
		    net->flight_size,
		    probepoint);
		if (net->cc_mod.rtcc.steady_step) {
			oth = net->cc_mod.rtcc.vol_reduce;
			oth <<= 16;
			oth |= net->cc_mod.rtcc.step_cnt;
			oth <<= 16;
			oth |= net->cc_mod.rtcc.last_step_state;
			SDT_PROBE5(sctp, cwnd, net, rttstep,
			    vtag,
			    ((net->cc_mod.rtcc.lbw << 32) | nbw),
			    ((net->cc_mod.rtcc.lbw_rtt << 32) | net->rtt),
			    oth,
			    probepoint);
			if ((net->cc_mod.rtcc.last_step_state == 5) &&
			    (net->cc_mod.rtcc.step_cnt > net->cc_mod.rtcc.steady_step)) {
				/* Step down worked */
				net->cc_mod.rtcc.step_cnt = 0;
				return (1);
			} else {
				net->cc_mod.rtcc.last_step_state = 6;
				net->cc_mod.rtcc.step_cnt = 0;
			}
		}
		net->cc_mod.rtcc.lbw = nbw;
		net->cc_mod.rtcc.lbw_rtt = net->rtt;
		net->cc_mod.rtcc.cwnd_at_bw_set = net->cwnd;
		if (inst_ind == SCTP_INST_GAINING)
			return (1);
		else if (inst_ind == SCTP_INST_NEUTRAL)
			return (1);
		else
			return (0);
	}
	/*
	 * Ok bw and rtt remained the same .. no update to any
	 */
	/* Probe point 7 */
	probepoint |= ((7 << 16) | net->cc_mod.rtcc.ret_from_eq);
	SDT_PROBE5(sctp, cwnd, net, rttvar,
	    vtag,
	    ((net->cc_mod.rtcc.lbw << 32) | nbw),
	    ((net->cc_mod.rtcc.lbw_rtt << 32) | net->rtt),
	    net->flight_size,
	    probepoint);
	if ((net->cc_mod.rtcc.steady_step) && (inst_ind != SCTP_INST_LOOSING)) {
		if (net->cc_mod.rtcc.last_step_state == 5)
			net->cc_mod.rtcc.step_cnt++;
		else
			net->cc_mod.rtcc.step_cnt = 1;
		net->cc_mod.rtcc.last_step_state = 5;
		if ((net->cc_mod.rtcc.step_cnt == net->cc_mod.rtcc.steady_step) ||
		    ((net->cc_mod.rtcc.step_cnt > net->cc_mod.rtcc.steady_step) &&
		    ((net->cc_mod.rtcc.step_cnt % net->cc_mod.rtcc.steady_step) == 0))) {
			/* Try a step down */
			if (net->cwnd > (4 * net->mtu)) {
				net->cwnd -= net->mtu;
				net->cc_mod.rtcc.vol_reduce++;
				return (1);
			} else {
				net->cc_mod.rtcc.step_cnt = 0;
			}
		}
	}
	if (inst_ind == SCTP_INST_GAINING)
		return (1);
	else if (inst_ind == SCTP_INST_NEUTRAL)
		return (1);
	else
		return ((int)net->cc_mod.rtcc.ret_from_eq);
}

static int
cc_bw_decrease(struct sctp_tcb *stcb, struct sctp_nets *net, uint64_t nbw, uint64_t rtt_offset,
    uint64_t vtag, uint8_t inst_ind)
{
	uint64_t oth, probepoint;

	/* Bandwidth decreased. */
	probepoint = (((uint64_t)net->cwnd) << 32);
	if (net->rtt > net->cc_mod.rtcc.lbw_rtt + rtt_offset) {
		/* rtt increased */
		/* Did we add more */
		if ((net->cwnd > net->cc_mod.rtcc.cwnd_at_bw_set) &&
		    (inst_ind != SCTP_INST_LOOSING)) {
			/* We caused it maybe.. back off? */
			/* PROBE POINT 1 */
			probepoint |= ((1 << 16) | 1);
			SDT_PROBE5(sctp, cwnd, net, rttvar,
			    vtag,
			    ((net->cc_mod.rtcc.lbw << 32) | nbw),
			    ((net->cc_mod.rtcc.lbw_rtt << 32) | net->rtt),
			    net->flight_size,
			    probepoint);
			if (net->cc_mod.rtcc.ret_from_eq) {
				/*
				 * Switch over to CA if we are less
				 * aggressive
				 */
				net->ssthresh = net->cwnd - 1;
				net->partial_bytes_acked = 0;
			}
			return (1);
		}
		/* Probe point 2 */
		probepoint |= ((2 << 16) | 0);
		SDT_PROBE5(sctp, cwnd, net, rttvar,
		    vtag,
		    ((net->cc_mod.rtcc.lbw << 32) | nbw),
		    ((net->cc_mod.rtcc.lbw_rtt << 32) | net->rtt),
		    net->flight_size,
		    probepoint);
		/* Someone else - fight for more? */
		if (net->cc_mod.rtcc.steady_step) {
			oth = net->cc_mod.rtcc.vol_reduce;
			oth <<= 16;
			oth |= net->cc_mod.rtcc.step_cnt;
			oth <<= 16;
			oth |= net->cc_mod.rtcc.last_step_state;
			SDT_PROBE5(sctp, cwnd, net, rttstep,
			    vtag,
			    ((net->cc_mod.rtcc.lbw << 32) | nbw),
			    ((net->cc_mod.rtcc.lbw_rtt << 32) | net->rtt),
			    oth,
			    probepoint);
			/*
			 * Did we voluntarily give up some? if so take one
			 * back please
			 */
			if ((net->cc_mod.rtcc.vol_reduce) &&
			    (inst_ind != SCTP_INST_GAINING)) {
				net->cwnd += net->mtu;
				sctp_enforce_cwnd_limit(&stcb->asoc, net);
				net->cc_mod.rtcc.vol_reduce--;
			}
			net->cc_mod.rtcc.last_step_state = 2;
			net->cc_mod.rtcc.step_cnt = 0;
		}
		goto out_decision;
	} else if (net->rtt < net->cc_mod.rtcc.lbw_rtt - rtt_offset) {
		/* bw & rtt decreased */
		/* Probe point 3 */
		probepoint |= ((3 << 16) | 0);
		SDT_PROBE5(sctp, cwnd, net, rttvar,
		    vtag,
		    ((net->cc_mod.rtcc.lbw << 32) | nbw),
		    ((net->cc_mod.rtcc.lbw_rtt << 32) | net->rtt),
		    net->flight_size,
		    probepoint);
		if (net->cc_mod.rtcc.steady_step) {
			oth = net->cc_mod.rtcc.vol_reduce;
			oth <<= 16;
			oth |= net->cc_mod.rtcc.step_cnt;
			oth <<= 16;
			oth |= net->cc_mod.rtcc.last_step_state;
			SDT_PROBE5(sctp, cwnd, net, rttstep,
			    vtag,
			    ((net->cc_mod.rtcc.lbw << 32) | nbw),
			    ((net->cc_mod.rtcc.lbw_rtt << 32) | net->rtt),
			    oth,
			    probepoint);
			if ((net->cc_mod.rtcc.vol_reduce) &&
			    (inst_ind != SCTP_INST_GAINING)) {
				net->cwnd += net->mtu;
				sctp_enforce_cwnd_limit(&stcb->asoc, net);
				net->cc_mod.rtcc.vol_reduce--;
			}
			net->cc_mod.rtcc.last_step_state = 3;
			net->cc_mod.rtcc.step_cnt = 0;
		}
		goto out_decision;
	}
	/* The bw decreased but rtt stayed the same */
	/* Probe point 4 */
	probepoint |= ((4 << 16) | 0);
	SDT_PROBE5(sctp, cwnd, net, rttvar,
	    vtag,
	    ((net->cc_mod.rtcc.lbw << 32) | nbw),
	    ((net->cc_mod.rtcc.lbw_rtt << 32) | net->rtt),
	    net->flight_size,
	    probepoint);
	if (net->cc_mod.rtcc.steady_step) {
		oth = net->cc_mod.rtcc.vol_reduce;
		oth <<= 16;
		oth |= net->cc_mod.rtcc.step_cnt;
		oth <<= 16;
		oth |= net->cc_mod.rtcc.last_step_state;
		SDT_PROBE5(sctp, cwnd, net, rttstep,
		    vtag,
		    ((net->cc_mod.rtcc.lbw << 32) | nbw),
		    ((net->cc_mod.rtcc.lbw_rtt << 32) | net->rtt),
		    oth,
		    probepoint);
		if ((net->cc_mod.rtcc.vol_reduce) &&
		    (inst_ind != SCTP_INST_GAINING)) {
			net->cwnd += net->mtu;
			sctp_enforce_cwnd_limit(&stcb->asoc, net);
			net->cc_mod.rtcc.vol_reduce--;
		}
		net->cc_mod.rtcc.last_step_state = 4;
		net->cc_mod.rtcc.step_cnt = 0;
	}
out_decision:
	net->cc_mod.rtcc.lbw = nbw;
	net->cc_mod.rtcc.lbw_rtt = net->rtt;
	net->cc_mod.rtcc.cwnd_at_bw_set = net->cwnd;
	if (inst_ind == SCTP_INST_GAINING) {
		return (1);
	} else {
		return (0);
	}
}

static int
cc_bw_increase(struct sctp_tcb *stcb, struct sctp_nets *net, uint64_t nbw, uint64_t vtag)
{
	uint64_t oth, probepoint;

	/*
	 * BW increased, so update and return 0, since all actions in our
	 * table say to do the normal CC update. Note that we pay no
	 * attention to the inst_ind since our overall sum is increasing.
	 */
	/* PROBE POINT 0 */
	probepoint = (((uint64_t)net->cwnd) << 32);
	SDT_PROBE5(sctp, cwnd, net, rttvar,
	    vtag,
	    ((net->cc_mod.rtcc.lbw << 32) | nbw),
	    ((net->cc_mod.rtcc.lbw_rtt << 32) | net->rtt),
	    net->flight_size,
	    probepoint);
	if (net->cc_mod.rtcc.steady_step) {
		oth = net->cc_mod.rtcc.vol_reduce;
		oth <<= 16;
		oth |= net->cc_mod.rtcc.step_cnt;
		oth <<= 16;
		oth |= net->cc_mod.rtcc.last_step_state;
		SDT_PROBE5(sctp, cwnd, net, rttstep,
		    vtag,
		    ((net->cc_mod.rtcc.lbw << 32) | nbw),
		    ((net->cc_mod.rtcc.lbw_rtt << 32) | net->rtt),
		    oth,
		    probepoint);
		net->cc_mod.rtcc.last_step_state = 0;
		net->cc_mod.rtcc.step_cnt = 0;
		net->cc_mod.rtcc.vol_reduce = 0;
	}
	net->cc_mod.rtcc.lbw = nbw;
	net->cc_mod.rtcc.lbw_rtt = net->rtt;
	net->cc_mod.rtcc.cwnd_at_bw_set = net->cwnd;
	return (0);
}

/* RTCC Algorithm to limit growth of cwnd, return
 * true if you want to NOT allow cwnd growth
 */
static int
cc_bw_limit(struct sctp_tcb *stcb, struct sctp_nets *net, uint64_t nbw)
{
	uint64_t bw_offset, rtt_offset;
	uint64_t probepoint, rtt, vtag;
	uint64_t bytes_for_this_rtt, inst_bw;
	uint64_t div, inst_off;
	int bw_shift;
	uint8_t inst_ind;
	int ret;

	/*-
	 * Here we need to see if we want
	 * to limit cwnd growth due to increase
	 * in overall rtt but no increase in bw.
	 * We use the following table to figure
	 * out what we should do. When we return
	 * 0, cc update goes on as planned. If we
	 * return 1, then no cc update happens and cwnd
	 * stays where it is at.
	 * ----------------------------------
	 *   BW    |    RTT   | Action
	 * *********************************
	 *   INC   |    INC   | return 0
	 * ----------------------------------
	 *   INC   |    SAME  | return 0
	 * ----------------------------------
	 *   INC   |    DECR  | return 0
	 * ----------------------------------
	 *   SAME  |    INC   | return 1
	 * ----------------------------------
	 *   SAME  |    SAME  | return 1
	 * ----------------------------------
	 *   SAME  |    DECR  | return 0
	 * ----------------------------------
	 *   DECR  |    INC   | return 0 or 1 based on if we caused.
	 * ----------------------------------
	 *   DECR  |    SAME  | return 0
	 * ----------------------------------
	 *   DECR  |    DECR  | return 0
	 * ----------------------------------
	 *
	 * We are a bit fuzz on what an increase or
	 * decrease is. For BW it is the same if
	 * it did not change within 1/64th. For
	 * RTT it stayed the same if it did not
	 * change within 1/32nd
	 */
	bw_shift = SCTP_BASE_SYSCTL(sctp_rttvar_bw);
	rtt = stcb->asoc.my_vtag;
	vtag = (rtt << 32) | (((uint32_t)(stcb->sctp_ep->sctp_lport)) << 16) | (stcb->rport);
	probepoint = (((uint64_t)net->cwnd) << 32);
	rtt = net->rtt;
	if (net->cc_mod.rtcc.rtt_set_this_sack) {
		net->cc_mod.rtcc.rtt_set_this_sack = 0;
		bytes_for_this_rtt = net->cc_mod.rtcc.bw_bytes - net->cc_mod.rtcc.bw_bytes_at_last_rttc;
		net->cc_mod.rtcc.bw_bytes_at_last_rttc = net->cc_mod.rtcc.bw_bytes;
		if (net->rtt) {
			div = net->rtt / 1000;
			if (div) {
				inst_bw = bytes_for_this_rtt / div;
				inst_off = inst_bw >> bw_shift;
				if (inst_bw > nbw)
					inst_ind = SCTP_INST_GAINING;
				else if ((inst_bw + inst_off) < nbw)
					inst_ind = SCTP_INST_LOOSING;
				else
					inst_ind = SCTP_INST_NEUTRAL;
				probepoint |= ((0xb << 16) | inst_ind);
			} else {
				inst_ind = net->cc_mod.rtcc.last_inst_ind;
				inst_bw = bytes_for_this_rtt / (uint64_t)(net->rtt);
				/* Can't determine do not change */
				probepoint |= ((0xc << 16) | inst_ind);
			}
		} else {
			inst_ind = net->cc_mod.rtcc.last_inst_ind;
			inst_bw = bytes_for_this_rtt;
			/* Can't determine do not change */
			probepoint |= ((0xd << 16) | inst_ind);
		}
		SDT_PROBE5(sctp, cwnd, net, rttvar,
		    vtag,
		    ((nbw << 32) | inst_bw),
		    ((net->cc_mod.rtcc.lbw_rtt << 32) | rtt),
		    net->flight_size,
		    probepoint);
	} else {
		/* No rtt measurement, use last one */
		inst_ind = net->cc_mod.rtcc.last_inst_ind;
	}
	bw_offset = net->cc_mod.rtcc.lbw >> bw_shift;
	if (nbw > net->cc_mod.rtcc.lbw + bw_offset) {
		ret = cc_bw_increase(stcb, net, nbw, vtag);
		goto out;
	}
	rtt_offset = net->cc_mod.rtcc.lbw_rtt >> SCTP_BASE_SYSCTL(sctp_rttvar_rtt);
	if (nbw < net->cc_mod.rtcc.lbw - bw_offset) {
		ret = cc_bw_decrease(stcb, net, nbw, rtt_offset, vtag, inst_ind);
		goto out;
	}
	/*
	 * If we reach here then we are in a situation where the bw stayed
	 * the same.
	 */
	ret = cc_bw_same(stcb, net, nbw, rtt_offset, vtag, inst_ind);
out:
	net->cc_mod.rtcc.last_inst_ind = inst_ind;
	return (ret);
}

static void
sctp_cwnd_update_after_sack_common(struct sctp_tcb *stcb,
    struct sctp_association *asoc,
    int accum_moved, int reneged_all SCTP_UNUSED, int will_exit, int use_rtcc)
{
	struct sctp_nets *net;
	int old_cwnd;
	uint32_t t_ssthresh, t_cwnd, incr;
	uint64_t t_ucwnd_sbw;
	uint64_t t_path_mptcp;
	uint64_t mptcp_like_alpha;
	uint32_t srtt;
	uint64_t max_path;

	/* MT FIXME: Don't compute this over and over again */
	t_ssthresh = 0;
	t_cwnd = 0;
	t_ucwnd_sbw = 0;
	t_path_mptcp = 0;
	mptcp_like_alpha = 1;
	if ((stcb->asoc.sctp_cmt_on_off == SCTP_CMT_RPV1) ||
	    (stcb->asoc.sctp_cmt_on_off == SCTP_CMT_RPV2) ||
	    (stcb->asoc.sctp_cmt_on_off == SCTP_CMT_MPTCP)) {
		max_path = 0;
		TAILQ_FOREACH(net, &stcb->asoc.nets, sctp_next) {
			t_ssthresh += net->ssthresh;
			t_cwnd += net->cwnd;
			/* lastsa>>3;  we don't need to devide ... */
			srtt = net->lastsa;
			if (srtt > 0) {
				uint64_t tmp;

				t_ucwnd_sbw += (uint64_t)net->cwnd / (uint64_t)srtt;
				t_path_mptcp += (((uint64_t)net->cwnd) << SHIFT_MPTCP_MULTI_Z) /
				    (((uint64_t)net->mtu) * (uint64_t)srtt);
				tmp = (((uint64_t)net->cwnd) << SHIFT_MPTCP_MULTI_N) /
				    ((uint64_t)net->mtu * (uint64_t)(srtt * srtt));
				if (tmp > max_path) {
					max_path = tmp;
				}
			}
		}
		if (t_path_mptcp > 0) {
			mptcp_like_alpha = max_path / (t_path_mptcp * t_path_mptcp);
		} else {
			mptcp_like_alpha = 1;
		}
	}
	if (t_ssthresh == 0) {
		t_ssthresh = 1;
	}
	if (t_ucwnd_sbw == 0) {
		t_ucwnd_sbw = 1;
	}
	/******************************/
	/* update cwnd and Early FR   */
	/******************************/
	TAILQ_FOREACH(net, &asoc->nets, sctp_next) {

#ifdef JANA_CMT_FAST_RECOVERY
		/*
		 * CMT fast recovery code. Need to debug.
		 */
		if (net->fast_retran_loss_recovery && net->new_pseudo_cumack) {
			if (SCTP_TSN_GE(asoc->last_acked_seq, net->fast_recovery_tsn) ||
			    SCTP_TSN_GE(net->pseudo_cumack, net->fast_recovery_tsn)) {
				net->will_exit_fast_recovery = 1;
			}
		}
#endif
		/* if nothing was acked on this destination skip it */
		if (net->net_ack == 0) {
			if (SCTP_BASE_SYSCTL(sctp_logging_level) & SCTP_CWND_LOGGING_ENABLE) {
				sctp_log_cwnd(stcb, net, 0, SCTP_CWND_LOG_FROM_SACK);
			}
			continue;
		}
#ifdef JANA_CMT_FAST_RECOVERY
		/*
		 * CMT fast recovery code
		 */
		/*
		 * if (sctp_cmt_on_off > 0 && net->fast_retran_loss_recovery
		 * && net->will_exit_fast_recovery == 0) { @@@ Do something
		 * } else if (sctp_cmt_on_off == 0 &&
		 * asoc->fast_retran_loss_recovery && will_exit == 0) {
		 */
#endif

		if (asoc->fast_retran_loss_recovery &&
		    (will_exit == 0) &&
		    (asoc->sctp_cmt_on_off == 0)) {
			/*
			 * If we are in loss recovery we skip any cwnd
			 * update
			 */
			return;
		}
		/*
		 * Did any measurements go on for this network?
		 */
		if (use_rtcc && (net->cc_mod.rtcc.tls_needs_set > 0)) {
			uint64_t nbw;

			/*
			 * At this point our bw_bytes has been updated by
			 * incoming sack information.
			 *
			 * But our bw may not yet be set.
			 *
			 */
			if ((net->cc_mod.rtcc.new_tot_time / 1000) > 0) {
				nbw = net->cc_mod.rtcc.bw_bytes / (net->cc_mod.rtcc.new_tot_time / 1000);
			} else {
				nbw = net->cc_mod.rtcc.bw_bytes;
			}
			if (net->cc_mod.rtcc.lbw) {
				if (cc_bw_limit(stcb, net, nbw)) {
					/* Hold here, no update */
					continue;
				}
			} else {
				uint64_t vtag, probepoint;

				probepoint = (((uint64_t)net->cwnd) << 32);
				probepoint |= ((0xa << 16) | 0);
				vtag = (net->rtt << 32) |
				    (((uint32_t)(stcb->sctp_ep->sctp_lport)) << 16) |
				    (stcb->rport);

				SDT_PROBE5(sctp, cwnd, net, rttvar,
				    vtag,
				    nbw,
				    ((net->cc_mod.rtcc.lbw_rtt << 32) | net->rtt),
				    net->flight_size,
				    probepoint);
				net->cc_mod.rtcc.lbw = nbw;
				net->cc_mod.rtcc.lbw_rtt = net->rtt;
				if (net->cc_mod.rtcc.rtt_set_this_sack) {
					net->cc_mod.rtcc.rtt_set_this_sack = 0;
					net->cc_mod.rtcc.bw_bytes_at_last_rttc = net->cc_mod.rtcc.bw_bytes;
				}
			}
		}
		/*
		 * CMT: CUC algorithm. Update cwnd if pseudo-cumack has
		 * moved.
		 */
		if (accum_moved ||
		    ((asoc->sctp_cmt_on_off > 0) && net->new_pseudo_cumack)) {
			/* If the cumulative ack moved we can proceed */
			if (net->cwnd <= net->ssthresh) {
				/* We are in slow start */
				if (net->flight_size + net->net_ack >= net->cwnd) {
					uint32_t limit;

					old_cwnd = net->cwnd;
					switch (asoc->sctp_cmt_on_off) {
					case SCTP_CMT_RPV1:
						limit = (uint32_t)(((uint64_t)net->mtu *
						    (uint64_t)SCTP_BASE_SYSCTL(sctp_L2_abc_variable) *
						    (uint64_t)net->ssthresh) /
						    (uint64_t)t_ssthresh);
						incr = (uint32_t)(((uint64_t)net->net_ack *
						    (uint64_t)net->ssthresh) /
						    (uint64_t)t_ssthresh);
						if (incr > limit) {
							incr = limit;
						}
						if (incr == 0) {
							incr = 1;
						}
						break;
					case SCTP_CMT_RPV2:
						/*
						 * lastsa>>3;  we don't need
						 * to divide ...
						 */
						srtt = net->lastsa;
						if (srtt == 0) {
							srtt = 1;
						}
						limit = (uint32_t)(((uint64_t)net->mtu *
						    (uint64_t)SCTP_BASE_SYSCTL(sctp_L2_abc_variable) *
						    (uint64_t)net->cwnd) /
						    ((uint64_t)srtt * t_ucwnd_sbw));
						/* INCREASE FACTOR */
						incr = (uint32_t)(((uint64_t)net->net_ack *
						    (uint64_t)net->cwnd) /
						    ((uint64_t)srtt * t_ucwnd_sbw));
						/* INCREASE FACTOR */
						if (incr > limit) {
							incr = limit;
						}
						if (incr == 0) {
							incr = 1;
						}
						break;
					case SCTP_CMT_MPTCP:
						limit = (uint32_t)(((uint64_t)net->mtu *
						    mptcp_like_alpha *
						    (uint64_t)SCTP_BASE_SYSCTL(sctp_L2_abc_variable)) >>
						    SHIFT_MPTCP_MULTI);
						incr = (uint32_t)(((uint64_t)net->net_ack *
						    mptcp_like_alpha) >>
						    SHIFT_MPTCP_MULTI);
						if (incr > limit) {
							incr = limit;
						}
						if (incr > net->net_ack) {
							incr = net->net_ack;
						}
						if (incr > net->mtu) {
							incr = net->mtu;
						}
						break;
					default:
						incr = net->net_ack;
						if (incr > net->mtu * SCTP_BASE_SYSCTL(sctp_L2_abc_variable)) {
							incr = net->mtu * SCTP_BASE_SYSCTL(sctp_L2_abc_variable);
						}
						break;
					}
					net->cwnd += incr;
					sctp_enforce_cwnd_limit(asoc, net);
					if (SCTP_BASE_SYSCTL(sctp_logging_level) & SCTP_CWND_MONITOR_ENABLE) {
						sctp_log_cwnd(stcb, net, incr,
						    SCTP_CWND_LOG_FROM_SS);
					}
					SDT_PROBE5(sctp, cwnd, net, ack,
					    stcb->asoc.my_vtag,
					    ((stcb->sctp_ep->sctp_lport << 16) | (stcb->rport)),
					    net,
					    old_cwnd, net->cwnd);
				} else {
					if (SCTP_BASE_SYSCTL(sctp_logging_level) & SCTP_CWND_LOGGING_ENABLE) {
						sctp_log_cwnd(stcb, net, net->net_ack,
						    SCTP_CWND_LOG_NOADV_SS);
					}
				}
			} else {
				/* We are in congestion avoidance */
				/*
				 * Add to pba
				 */
				net->partial_bytes_acked += net->net_ack;

				if ((net->flight_size + net->net_ack >= net->cwnd) &&
				    (net->partial_bytes_acked >= net->cwnd)) {
					net->partial_bytes_acked -= net->cwnd;
					old_cwnd = net->cwnd;
					switch (asoc->sctp_cmt_on_off) {
					case SCTP_CMT_RPV1:
						incr = (uint32_t)(((uint64_t)net->mtu *
						    (uint64_t)net->ssthresh) /
						    (uint64_t)t_ssthresh);
						if (incr == 0) {
							incr = 1;
						}
						break;
					case SCTP_CMT_RPV2:
						/*
						 * lastsa>>3;  we don't need
						 * to divide ...
						 */
						srtt = net->lastsa;
						if (srtt == 0) {
							srtt = 1;
						}
						incr = (uint32_t)((uint64_t)net->mtu *
						    (uint64_t)net->cwnd /
						    ((uint64_t)srtt *
						    t_ucwnd_sbw));
						/* INCREASE FACTOR */
						if (incr == 0) {
							incr = 1;
						}
						break;
					case SCTP_CMT_MPTCP:
						incr = (uint32_t)((mptcp_like_alpha *
						    (uint64_t)net->cwnd) >>
						    SHIFT_MPTCP_MULTI);
						if (incr > net->mtu) {
							incr = net->mtu;
						}
						break;
					default:
						incr = net->mtu;
						break;
					}
					net->cwnd += incr;
					sctp_enforce_cwnd_limit(asoc, net);
					SDT_PROBE5(sctp, cwnd, net, ack,
					    stcb->asoc.my_vtag,
					    ((stcb->sctp_ep->sctp_lport << 16) | (stcb->rport)),
					    net,
					    old_cwnd, net->cwnd);
					if (SCTP_BASE_SYSCTL(sctp_logging_level) & SCTP_CWND_MONITOR_ENABLE) {
						sctp_log_cwnd(stcb, net, net->mtu,
						    SCTP_CWND_LOG_FROM_CA);
					}
				} else {
					if (SCTP_BASE_SYSCTL(sctp_logging_level) & SCTP_CWND_LOGGING_ENABLE) {
						sctp_log_cwnd(stcb, net, net->net_ack,
						    SCTP_CWND_LOG_NOADV_CA);
					}
				}
			}
		} else {
			if (SCTP_BASE_SYSCTL(sctp_logging_level) & SCTP_CWND_LOGGING_ENABLE) {
				sctp_log_cwnd(stcb, net, net->mtu,
				    SCTP_CWND_LOG_NO_CUMACK);
			}
		}
	}
}

static void
sctp_cwnd_update_exit_pf_common(struct sctp_tcb *stcb, struct sctp_nets *net)
{
	int old_cwnd;

	old_cwnd = net->cwnd;
	net->cwnd = net->mtu;
	SDT_PROBE5(sctp, cwnd, net, ack,
	    stcb->asoc.my_vtag, ((stcb->sctp_ep->sctp_lport << 16) | (stcb->rport)), net,
	    old_cwnd, net->cwnd);
	SCTPDBG(SCTP_DEBUG_INDATA1, "Destination %p moved from PF to reachable with cwnd %d.\n",
	    (void *)net, net->cwnd);
}


static void
sctp_cwnd_update_after_timeout(struct sctp_tcb *stcb, struct sctp_nets *net)
{
	int old_cwnd = net->cwnd;
	uint32_t t_ssthresh, t_cwnd;
	uint64_t t_ucwnd_sbw;

	/* MT FIXME: Don't compute this over and over again */
	t_ssthresh = 0;
	t_cwnd = 0;
	if ((stcb->asoc.sctp_cmt_on_off == SCTP_CMT_RPV1) ||
	    (stcb->asoc.sctp_cmt_on_off == SCTP_CMT_RPV2)) {
		struct sctp_nets *lnet;
		uint32_t srtt;

		t_ucwnd_sbw = 0;
		TAILQ_FOREACH(lnet, &stcb->asoc.nets, sctp_next) {
			t_ssthresh += lnet->ssthresh;
			t_cwnd += lnet->cwnd;
			srtt = lnet->lastsa;
			/* lastsa>>3;  we don't need to divide ... */
			if (srtt > 0) {
				t_ucwnd_sbw += (uint64_t)lnet->cwnd / (uint64_t)srtt;
			}
		}
		if (t_ssthresh < 1) {
			t_ssthresh = 1;
		}
		if (t_ucwnd_sbw < 1) {
			t_ucwnd_sbw = 1;
		}
		if (stcb->asoc.sctp_cmt_on_off == SCTP_CMT_RPV1) {
			net->ssthresh = (uint32_t)(((uint64_t)4 *
			    (uint64_t)net->mtu *
			    (uint64_t)net->ssthresh) /
			    (uint64_t)t_ssthresh);
		} else {
			uint64_t cc_delta;

			srtt = net->lastsa;
			/* lastsa>>3;  we don't need to divide ... */
			if (srtt == 0) {
				srtt = 1;
			}
			cc_delta = t_ucwnd_sbw * (uint64_t)srtt / 2;
			if (cc_delta < t_cwnd) {
				net->ssthresh = (uint32_t)((uint64_t)t_cwnd - cc_delta);
			} else {
				net->ssthresh = net->mtu;
			}
		}
		if ((net->cwnd > t_cwnd / 2) &&
		    (net->ssthresh < net->cwnd - t_cwnd / 2)) {
			net->ssthresh = net->cwnd - t_cwnd / 2;
		}
		if (net->ssthresh < net->mtu) {
			net->ssthresh = net->mtu;
		}
	} else {
		net->ssthresh = max(net->cwnd / 2, 4 * net->mtu);
	}
	net->cwnd = net->mtu;
	net->partial_bytes_acked = 0;
	SDT_PROBE5(sctp, cwnd, net, to,
	    stcb->asoc.my_vtag,
	    ((stcb->sctp_ep->sctp_lport << 16) | (stcb->rport)),
	    net,
	    old_cwnd, net->cwnd);
	if (SCTP_BASE_SYSCTL(sctp_logging_level) & SCTP_CWND_MONITOR_ENABLE) {
		sctp_log_cwnd(stcb, net, net->cwnd - old_cwnd, SCTP_CWND_LOG_FROM_RTX);
	}
}

static void
sctp_cwnd_update_after_ecn_echo_common(struct sctp_tcb *stcb, struct sctp_nets *net,
    int in_window, int num_pkt_lost, int use_rtcc)
{
	int old_cwnd = net->cwnd;

	if ((use_rtcc) && (net->lan_type == SCTP_LAN_LOCAL) && (net->cc_mod.rtcc.use_dccc_ecn)) {
		/* Data center Congestion Control */
		if (in_window == 0) {
			/*
			 * Go to CA with the cwnd at the point we sent the
			 * TSN that was marked with a CE.
			 */
			if (net->ecn_prev_cwnd < net->cwnd) {
				/* Restore to prev cwnd */
				net->cwnd = net->ecn_prev_cwnd - (net->mtu * num_pkt_lost);
			} else {
				/* Just cut in 1/2 */
				net->cwnd /= 2;
			}
			/* Drop to CA */
			net->ssthresh = net->cwnd - (num_pkt_lost * net->mtu);
			if (SCTP_BASE_SYSCTL(sctp_logging_level) & SCTP_CWND_MONITOR_ENABLE) {
				sctp_log_cwnd(stcb, net, (net->cwnd - old_cwnd), SCTP_CWND_LOG_FROM_SAT);
			}
		} else {
			/*
			 * Further tuning down required over the drastic
			 * original cut
			 */
			net->ssthresh -= (net->mtu * num_pkt_lost);
			net->cwnd -= (net->mtu * num_pkt_lost);
			if (SCTP_BASE_SYSCTL(sctp_logging_level) & SCTP_CWND_MONITOR_ENABLE) {
				sctp_log_cwnd(stcb, net, (net->cwnd - old_cwnd), SCTP_CWND_LOG_FROM_SAT);
			}

		}
		SCTP_STAT_INCR(sctps_ecnereducedcwnd);
	} else {
		if (in_window == 0) {
			SCTP_STAT_INCR(sctps_ecnereducedcwnd);
			net->ssthresh = net->cwnd / 2;
			if (net->ssthresh < net->mtu) {
				net->ssthresh = net->mtu;
				/*
				 * here back off the timer as well, to slow
				 * us down
				 */
				net->RTO <<= 1;
			}
			net->cwnd = net->ssthresh;
			SDT_PROBE5(sctp, cwnd, net, ecn,
			    stcb->asoc.my_vtag,
			    ((stcb->sctp_ep->sctp_lport << 16) | (stcb->rport)),
			    net,
			    old_cwnd, net->cwnd);
			if (SCTP_BASE_SYSCTL(sctp_logging_level) & SCTP_CWND_MONITOR_ENABLE) {
				sctp_log_cwnd(stcb, net, (net->cwnd - old_cwnd), SCTP_CWND_LOG_FROM_SAT);
			}
		}
	}

}

static void
sctp_cwnd_update_after_packet_dropped(struct sctp_tcb *stcb,
    struct sctp_nets *net, struct sctp_pktdrop_chunk *cp,
    uint32_t *bottle_bw, uint32_t *on_queue)
{
	uint32_t bw_avail;
	unsigned int incr;
	int old_cwnd = net->cwnd;

	/* get bottle neck bw */
	*bottle_bw = ntohl(cp->bottle_bw);
	/* and whats on queue */
	*on_queue = ntohl(cp->current_onq);
	/*
	 * adjust the on-queue if our flight is more it could be that the
	 * router has not yet gotten data "in-flight" to it
	 */
	if (*on_queue < net->flight_size) {
		*on_queue = net->flight_size;
	}
	/* rtt is measured in micro seconds, bottle_bw in bytes per second */
	bw_avail = (uint32_t)(((uint64_t)(*bottle_bw) * net->rtt) / (uint64_t)1000000);
	if (bw_avail > *bottle_bw) {
		/*
		 * Cap the growth to no more than the bottle neck. This can
		 * happen as RTT slides up due to queues. It also means if
		 * you have more than a 1 second RTT with a empty queue you
		 * will be limited to the bottle_bw per second no matter if
		 * other points have 1/2 the RTT and you could get more
		 * out...
		 */
		bw_avail = *bottle_bw;
	}
	if (*on_queue > bw_avail) {
		/*
		 * No room for anything else don't allow anything else to be
		 * "added to the fire".
		 */
		int seg_inflight, seg_onqueue, my_portion;

		net->partial_bytes_acked = 0;
		/* how much are we over queue size? */
		incr = *on_queue - bw_avail;
		if (stcb->asoc.seen_a_sack_this_pkt) {
			/*
			 * undo any cwnd adjustment that the sack might have
			 * made
			 */
			net->cwnd = net->prev_cwnd;
		}
		/* Now how much of that is mine? */
		seg_inflight = net->flight_size / net->mtu;
		seg_onqueue = *on_queue / net->mtu;
		my_portion = (incr * seg_inflight) / seg_onqueue;

		/* Have I made an adjustment already */
		if (net->cwnd > net->flight_size) {
			/*
			 * for this flight I made an adjustment we need to
			 * decrease the portion by a share our previous
			 * adjustment.
			 */
			int diff_adj;

			diff_adj = net->cwnd - net->flight_size;
			if (diff_adj > my_portion)
				my_portion = 0;
			else
				my_portion -= diff_adj;
		}
		/*
		 * back down to the previous cwnd (assume we have had a sack
		 * before this packet). minus what ever portion of the
		 * overage is my fault.
		 */
		net->cwnd -= my_portion;

		/* we will NOT back down more than 1 MTU */
		if (net->cwnd <= net->mtu) {
			net->cwnd = net->mtu;
		}
		/* force into CA */
		net->ssthresh = net->cwnd - 1;
	} else {
		/*
		 * Take 1/4 of the space left or max burst up .. whichever
		 * is less.
		 */
		incr = (bw_avail - *on_queue) >> 2;
		if ((stcb->asoc.max_burst > 0) &&
		    (stcb->asoc.max_burst * net->mtu < incr)) {
			incr = stcb->asoc.max_burst * net->mtu;
		}
		net->cwnd += incr;
	}
	if (net->cwnd > bw_avail) {
		/* We can't exceed the pipe size */
		net->cwnd = bw_avail;
	}
	if (net->cwnd < net->mtu) {
		/* We always have 1 MTU */
		net->cwnd = net->mtu;
	}
	sctp_enforce_cwnd_limit(&stcb->asoc, net);
	if (net->cwnd - old_cwnd != 0) {
		/* log only changes */
		SDT_PROBE5(sctp, cwnd, net, pd,
		    stcb->asoc.my_vtag,
		    ((stcb->sctp_ep->sctp_lport << 16) | (stcb->rport)),
		    net,
		    old_cwnd, net->cwnd);
		if (SCTP_BASE_SYSCTL(sctp_logging_level) & SCTP_CWND_MONITOR_ENABLE) {
			sctp_log_cwnd(stcb, net, (net->cwnd - old_cwnd),
			    SCTP_CWND_LOG_FROM_SAT);
		}
	}
}

static void
sctp_cwnd_update_after_output(struct sctp_tcb *stcb,
    struct sctp_nets *net, int burst_limit)
{
	int old_cwnd = net->cwnd;

	if (net->ssthresh < net->cwnd)
		net->ssthresh = net->cwnd;
	if (burst_limit) {
		net->cwnd = (net->flight_size + (burst_limit * net->mtu));
		sctp_enforce_cwnd_limit(&stcb->asoc, net);
		SDT_PROBE5(sctp, cwnd, net, bl,
		    stcb->asoc.my_vtag,
		    ((stcb->sctp_ep->sctp_lport << 16) | (stcb->rport)),
		    net,
		    old_cwnd, net->cwnd);
		if (SCTP_BASE_SYSCTL(sctp_logging_level) & SCTP_CWND_MONITOR_ENABLE) {
			sctp_log_cwnd(stcb, net, (net->cwnd - old_cwnd), SCTP_CWND_LOG_FROM_BRST);
		}
	}
}

static void
sctp_cwnd_update_after_sack(struct sctp_tcb *stcb,
    struct sctp_association *asoc,
    int accum_moved, int reneged_all, int will_exit)
{
	/* Passing a zero argument in last disables the rtcc algorithm */
	sctp_cwnd_update_after_sack_common(stcb, asoc, accum_moved, reneged_all, will_exit, 0);
}

static void
sctp_cwnd_update_after_ecn_echo(struct sctp_tcb *stcb, struct sctp_nets *net,
    int in_window, int num_pkt_lost)
{
	/* Passing a zero argument in last disables the rtcc algorithm */
	sctp_cwnd_update_after_ecn_echo_common(stcb, net, in_window, num_pkt_lost, 0);
}

/* Here starts the RTCCVAR type CC invented by RRS which
 * is a slight mod to RFC2581. We reuse a common routine or
 * two since these algorithms are so close and need to
 * remain the same.
 */
static void
sctp_cwnd_update_rtcc_after_ecn_echo(struct sctp_tcb *stcb, struct sctp_nets *net,
    int in_window, int num_pkt_lost)
{
	sctp_cwnd_update_after_ecn_echo_common(stcb, net, in_window, num_pkt_lost, 1);
}


static
void
sctp_cwnd_update_rtcc_tsn_acknowledged(struct sctp_nets *net,
    struct sctp_tmit_chunk *tp1)
{
	net->cc_mod.rtcc.bw_bytes += tp1->send_size;
}

static void
sctp_cwnd_prepare_rtcc_net_for_sack(struct sctp_tcb *stcb SCTP_UNUSED,
    struct sctp_nets *net)
{
	if (net->cc_mod.rtcc.tls_needs_set > 0) {
		/* We had a bw measurment going on */
		struct timeval ltls;

		SCTP_GETPTIME_TIMEVAL(&ltls);
		timevalsub(&ltls, &net->cc_mod.rtcc.tls);
		net->cc_mod.rtcc.new_tot_time = (ltls.tv_sec * 1000000) + ltls.tv_usec;
	}
}

static void
sctp_cwnd_new_rtcc_transmission_begins(struct sctp_tcb *stcb,
    struct sctp_nets *net)
{
	uint64_t vtag, probepoint;

	if (net->cc_mod.rtcc.lbw) {
		/* Clear the old bw.. we went to 0 in-flight */
		vtag = (net->rtt << 32) | (((uint32_t)(stcb->sctp_ep->sctp_lport)) << 16) |
		    (stcb->rport);
		probepoint = (((uint64_t)net->cwnd) << 32);
		/* Probe point 8 */
		probepoint |= ((8 << 16) | 0);
		SDT_PROBE5(sctp, cwnd, net, rttvar,
		    vtag,
		    ((net->cc_mod.rtcc.lbw << 32) | 0),
		    ((net->cc_mod.rtcc.lbw_rtt << 32) | net->rtt),
		    net->flight_size,
		    probepoint);
		net->cc_mod.rtcc.lbw_rtt = 0;
		net->cc_mod.rtcc.cwnd_at_bw_set = 0;
		net->cc_mod.rtcc.lbw = 0;
		net->cc_mod.rtcc.bw_bytes_at_last_rttc = 0;
		net->cc_mod.rtcc.vol_reduce = 0;
		net->cc_mod.rtcc.bw_tot_time = 0;
		net->cc_mod.rtcc.bw_bytes = 0;
		net->cc_mod.rtcc.tls_needs_set = 0;
		if (net->cc_mod.rtcc.steady_step) {
			net->cc_mod.rtcc.vol_reduce = 0;
			net->cc_mod.rtcc.step_cnt = 0;
			net->cc_mod.rtcc.last_step_state = 0;
		}
		if (net->cc_mod.rtcc.ret_from_eq) {
			/* less aggressive one - reset cwnd too */
			uint32_t cwnd_in_mtu, cwnd;

			cwnd_in_mtu = SCTP_BASE_SYSCTL(sctp_initial_cwnd);
			if (cwnd_in_mtu == 0) {
				/*
				 * Using 0 means that the value of RFC 4960
				 * is used.
				 */
				cwnd = min((net->mtu * 4), max((2 * net->mtu), SCTP_INITIAL_CWND));
			} else {
				/*
				 * We take the minimum of the burst limit
				 * and the initial congestion window.
				 */
				if ((stcb->asoc.max_burst > 0) && (cwnd_in_mtu > stcb->asoc.max_burst))
					cwnd_in_mtu = stcb->asoc.max_burst;
				cwnd = (net->mtu - sizeof(struct sctphdr)) * cwnd_in_mtu;
			}
			if (net->cwnd > cwnd) {
				/*
				 * Only set if we are not a timeout (i.e.
				 * down to 1 mtu)
				 */
				net->cwnd = cwnd;
			}
		}
	}
}

static void
sctp_set_rtcc_initial_cc_param(struct sctp_tcb *stcb,
    struct sctp_nets *net)
{
	uint64_t vtag, probepoint;

	sctp_set_initial_cc_param(stcb, net);
	stcb->asoc.use_precise_time = 1;
	probepoint = (((uint64_t)net->cwnd) << 32);
	probepoint |= ((9 << 16) | 0);
	vtag = (net->rtt << 32) |
	    (((uint32_t)(stcb->sctp_ep->sctp_lport)) << 16) |
	    (stcb->rport);
	SDT_PROBE5(sctp, cwnd, net, rttvar,
	    vtag,
	    0,
	    0,
	    0,
	    probepoint);
	net->cc_mod.rtcc.lbw_rtt = 0;
	net->cc_mod.rtcc.cwnd_at_bw_set = 0;
	net->cc_mod.rtcc.vol_reduce = 0;
	net->cc_mod.rtcc.lbw = 0;
	net->cc_mod.rtcc.vol_reduce = 0;
	net->cc_mod.rtcc.bw_bytes_at_last_rttc = 0;
	net->cc_mod.rtcc.bw_tot_time = 0;
	net->cc_mod.rtcc.bw_bytes = 0;
	net->cc_mod.rtcc.tls_needs_set = 0;
	net->cc_mod.rtcc.ret_from_eq = SCTP_BASE_SYSCTL(sctp_rttvar_eqret);
	net->cc_mod.rtcc.steady_step = SCTP_BASE_SYSCTL(sctp_steady_step);
	net->cc_mod.rtcc.use_dccc_ecn = SCTP_BASE_SYSCTL(sctp_use_dccc_ecn);
	net->cc_mod.rtcc.step_cnt = 0;
	net->cc_mod.rtcc.last_step_state = 0;


}

static int
sctp_cwnd_rtcc_socket_option(struct sctp_tcb *stcb, int setorget,
    struct sctp_cc_option *cc_opt)
{
	struct sctp_nets *net;

	if (setorget == 1) {
		/* a set */
		if (cc_opt->option == SCTP_CC_OPT_RTCC_SETMODE) {
			if ((cc_opt->aid_value.assoc_value != 0) &&
			    (cc_opt->aid_value.assoc_value != 1)) {
				return (EINVAL);
			}
			TAILQ_FOREACH(net, &stcb->asoc.nets, sctp_next) {
				net->cc_mod.rtcc.ret_from_eq = cc_opt->aid_value.assoc_value;
			}
		} else if (cc_opt->option == SCTP_CC_OPT_USE_DCCC_ECN) {
			if ((cc_opt->aid_value.assoc_value != 0) &&
			    (cc_opt->aid_value.assoc_value != 1)) {
				return (EINVAL);
			}
			TAILQ_FOREACH(net, &stcb->asoc.nets, sctp_next) {
				net->cc_mod.rtcc.use_dccc_ecn = cc_opt->aid_value.assoc_value;
			}
		} else if (cc_opt->option == SCTP_CC_OPT_STEADY_STEP) {
			TAILQ_FOREACH(net, &stcb->asoc.nets, sctp_next) {
				net->cc_mod.rtcc.steady_step = cc_opt->aid_value.assoc_value;
			}
		} else {
			return (EINVAL);
		}
	} else {
		/* a get */
		if (cc_opt->option == SCTP_CC_OPT_RTCC_SETMODE) {
			net = TAILQ_FIRST(&stcb->asoc.nets);
			if (net == NULL) {
				return (EFAULT);
			}
			cc_opt->aid_value.assoc_value = net->cc_mod.rtcc.ret_from_eq;
		} else if (cc_opt->option == SCTP_CC_OPT_USE_DCCC_ECN) {
			net = TAILQ_FIRST(&stcb->asoc.nets);
			if (net == NULL) {
				return (EFAULT);
			}
			cc_opt->aid_value.assoc_value = net->cc_mod.rtcc.use_dccc_ecn;
		} else if (cc_opt->option == SCTP_CC_OPT_STEADY_STEP) {
			net = TAILQ_FIRST(&stcb->asoc.nets);
			if (net == NULL) {
				return (EFAULT);
			}
			cc_opt->aid_value.assoc_value = net->cc_mod.rtcc.steady_step;
		} else {
			return (EINVAL);
		}
	}
	return (0);
}

static void
sctp_cwnd_update_rtcc_packet_transmitted(struct sctp_tcb *stcb SCTP_UNUSED,
    struct sctp_nets *net)
{
	if (net->cc_mod.rtcc.tls_needs_set == 0) {
		SCTP_GETPTIME_TIMEVAL(&net->cc_mod.rtcc.tls);
		net->cc_mod.rtcc.tls_needs_set = 2;
	}
}

static void
sctp_cwnd_update_rtcc_after_sack(struct sctp_tcb *stcb,
    struct sctp_association *asoc,
    int accum_moved, int reneged_all, int will_exit)
{
	/* Passing a one argument at the last enables the rtcc algorithm */
	sctp_cwnd_update_after_sack_common(stcb, asoc, accum_moved, reneged_all, will_exit, 1);
}

static void
sctp_rtt_rtcc_calculated(struct sctp_tcb *stcb SCTP_UNUSED,
    struct sctp_nets *net,
    struct timeval *now SCTP_UNUSED)
{
	net->cc_mod.rtcc.rtt_set_this_sack = 1;
}

/* Here starts Sally Floyds HS-TCP */

struct sctp_hs_raise_drop {
	int32_t cwnd;
	int8_t increase;
	int8_t drop_percent;
};

#define SCTP_HS_TABLE_SIZE 73

static const struct sctp_hs_raise_drop sctp_cwnd_adjust[SCTP_HS_TABLE_SIZE] = {
	{38, 1, 50},		/* 0   */
	{118, 2, 44},		/* 1   */
	{221, 3, 41},		/* 2   */
	{347, 4, 38},		/* 3   */
	{495, 5, 37},		/* 4   */
	{663, 6, 35},		/* 5   */
	{851, 7, 34},		/* 6   */
	{1058, 8, 33},		/* 7   */
	{1284, 9, 32},		/* 8   */
	{1529, 10, 31},		/* 9   */
	{1793, 11, 30},		/* 10  */
	{2076, 12, 29},		/* 11  */
	{2378, 13, 28},		/* 12  */
	{2699, 14, 28},		/* 13  */
	{3039, 15, 27},		/* 14  */
	{3399, 16, 27},		/* 15  */
	{3778, 17, 26},		/* 16  */
	{4177, 18, 26},		/* 17  */
	{4596, 19, 25},		/* 18  */
	{5036, 20, 25},		/* 19  */
	{5497, 21, 24},		/* 20  */
	{5979, 22, 24},		/* 21  */
	{6483, 23, 23},		/* 22  */
	{7009, 24, 23},		/* 23  */
	{7558, 25, 22},		/* 24  */
	{8130, 26, 22},		/* 25  */
	{8726, 27, 22},		/* 26  */
	{9346, 28, 21},		/* 27  */
	{9991, 29, 21},		/* 28  */
	{10661, 30, 21},	/* 29  */
	{11358, 31, 20},	/* 30  */
	{12082, 32, 20},	/* 31  */
	{12834, 33, 20},	/* 32  */
	{13614, 34, 19},	/* 33  */
	{14424, 35, 19},	/* 34  */
	{15265, 36, 19},	/* 35  */
	{16137, 37, 19},	/* 36  */
	{17042, 38, 18},	/* 37  */
	{17981, 39, 18},	/* 38  */
	{18955, 40, 18},	/* 39  */
	{19965, 41, 17},	/* 40  */
	{21013, 42, 17},	/* 41  */
	{22101, 43, 17},	/* 42  */
	{23230, 44, 17},	/* 43  */
	{24402, 45, 16},	/* 44  */
	{25618, 46, 16},	/* 45  */
	{26881, 47, 16},	/* 46  */
	{28193, 48, 16},	/* 47  */
	{29557, 49, 15},	/* 48  */
	{30975, 50, 15},	/* 49  */
	{32450, 51, 15},	/* 50  */
	{33986, 52, 15},	/* 51  */
	{35586, 53, 14},	/* 52  */
	{37253, 54, 14},	/* 53  */
	{38992, 55, 14},	/* 54  */
	{40808, 56, 14},	/* 55  */
	{42707, 57, 13},	/* 56  */
	{44694, 58, 13},	/* 57  */
	{46776, 59, 13},	/* 58  */
	{48961, 60, 13},	/* 59  */
	{51258, 61, 13},	/* 60  */
	{53677, 62, 12},	/* 61  */
	{56230, 63, 12},	/* 62  */
	{58932, 64, 12},	/* 63  */
	{61799, 65, 12},	/* 64  */
	{64851, 66, 11},	/* 65  */
	{68113, 67, 11},	/* 66  */
	{71617, 68, 11},	/* 67  */
	{75401, 69, 10},	/* 68  */
	{79517, 70, 10},	/* 69  */
	{84035, 71, 10},	/* 70  */
	{89053, 72, 10},	/* 71  */
	{94717, 73, 9}		/* 72  */
};

static void
sctp_hs_cwnd_increase(struct sctp_tcb *stcb, struct sctp_nets *net)
{
	int cur_val, i, indx, incr;
	int old_cwnd = net->cwnd;

	cur_val = net->cwnd >> 10;
	indx = SCTP_HS_TABLE_SIZE - 1;

	if (cur_val < sctp_cwnd_adjust[0].cwnd) {
		/* normal mode */
		if (net->net_ack > net->mtu) {
			net->cwnd += net->mtu;
		} else {
			net->cwnd += net->net_ack;
		}
	} else {
		for (i = net->last_hs_used; i < SCTP_HS_TABLE_SIZE; i++) {
			if (cur_val < sctp_cwnd_adjust[i].cwnd) {
				indx = i;
				break;
			}
		}
		net->last_hs_used = indx;
		incr = (((int32_t)sctp_cwnd_adjust[indx].increase) << 10);
		net->cwnd += incr;
	}
	sctp_enforce_cwnd_limit(&stcb->asoc, net);
	if (SCTP_BASE_SYSCTL(sctp_logging_level) & SCTP_CWND_MONITOR_ENABLE) {
		sctp_log_cwnd(stcb, net, (net->cwnd - old_cwnd), SCTP_CWND_LOG_FROM_SS);
	}
}

static void
sctp_hs_cwnd_decrease(struct sctp_tcb *stcb, struct sctp_nets *net)
{
	int cur_val, i, indx;
	int old_cwnd = net->cwnd;

	cur_val = net->cwnd >> 10;
	if (cur_val < sctp_cwnd_adjust[0].cwnd) {
		/* normal mode */
		net->ssthresh = net->cwnd / 2;
		if (net->ssthresh < (net->mtu * 2)) {
			net->ssthresh = 2 * net->mtu;
		}
		net->cwnd = net->ssthresh;
	} else {
		/* drop by the proper amount */
		net->ssthresh = net->cwnd - (int)((net->cwnd / 100) *
		    (int32_t)sctp_cwnd_adjust[net->last_hs_used].drop_percent);
		net->cwnd = net->ssthresh;
		/* now where are we */
		indx = net->last_hs_used;
		cur_val = net->cwnd >> 10;
		/* reset where we are in the table */
		if (cur_val < sctp_cwnd_adjust[0].cwnd) {
			/* feel out of hs */
			net->last_hs_used = 0;
		} else {
			for (i = indx; i >= 1; i--) {
				if (cur_val > sctp_cwnd_adjust[i - 1].cwnd) {
					break;
				}
			}
			net->last_hs_used = indx;
		}
	}
	sctp_enforce_cwnd_limit(&stcb->asoc, net);
	if (SCTP_BASE_SYSCTL(sctp_logging_level) & SCTP_CWND_MONITOR_ENABLE) {
		sctp_log_cwnd(stcb, net, (net->cwnd - old_cwnd), SCTP_CWND_LOG_FROM_FR);
	}
}

static void
sctp_hs_cwnd_update_after_fr(struct sctp_tcb *stcb,
    struct sctp_association *asoc)
{
	struct sctp_nets *net;

	/*
	 * CMT fast recovery code. Need to debug. ((sctp_cmt_on_off > 0) &&
	 * (net->fast_retran_loss_recovery == 0)))
	 */
	TAILQ_FOREACH(net, &asoc->nets, sctp_next) {
		if ((asoc->fast_retran_loss_recovery == 0) ||
		    (asoc->sctp_cmt_on_off > 0)) {
			/* out of a RFC2582 Fast recovery window? */
			if (net->net_ack > 0) {
				/*
				 * per section 7.2.3, are there any
				 * destinations that had a fast retransmit
				 * to them. If so what we need to do is
				 * adjust ssthresh and cwnd.
				 */
				struct sctp_tmit_chunk *lchk;

				sctp_hs_cwnd_decrease(stcb, net);

				lchk = TAILQ_FIRST(&asoc->send_queue);

				net->partial_bytes_acked = 0;
				/* Turn on fast recovery window */
				asoc->fast_retran_loss_recovery = 1;
				if (lchk == NULL) {
					/* Mark end of the window */
					asoc->fast_recovery_tsn = asoc->sending_seq - 1;
				} else {
					asoc->fast_recovery_tsn = lchk->rec.data.tsn - 1;
				}

				/*
				 * CMT fast recovery -- per destination
				 * recovery variable.
				 */
				net->fast_retran_loss_recovery = 1;

				if (lchk == NULL) {
					/* Mark end of the window */
					net->fast_recovery_tsn = asoc->sending_seq - 1;
				} else {
					net->fast_recovery_tsn = lchk->rec.data.tsn - 1;
				}

				sctp_timer_stop(SCTP_TIMER_TYPE_SEND,
				    stcb->sctp_ep, stcb, net,
				    SCTP_FROM_SCTP_CC_FUNCTIONS + SCTP_LOC_2);
				sctp_timer_start(SCTP_TIMER_TYPE_SEND,
				    stcb->sctp_ep, stcb, net);
			}
		} else if (net->net_ack > 0) {
			/*
			 * Mark a peg that we WOULD have done a cwnd
			 * reduction but RFC2582 prevented this action.
			 */
			SCTP_STAT_INCR(sctps_fastretransinrtt);
		}
	}
}

static void
sctp_hs_cwnd_update_after_sack(struct sctp_tcb *stcb,
    struct sctp_association *asoc,
    int accum_moved, int reneged_all SCTP_UNUSED, int will_exit)
{
	struct sctp_nets *net;

	/******************************/
	/* update cwnd and Early FR   */
	/******************************/
	TAILQ_FOREACH(net, &asoc->nets, sctp_next) {

#ifdef JANA_CMT_FAST_RECOVERY
		/*
		 * CMT fast recovery code. Need to debug.
		 */
		if (net->fast_retran_loss_recovery && net->new_pseudo_cumack) {
			if (SCTP_TSN_GE(asoc->last_acked_seq, net->fast_recovery_tsn) ||
			    SCTP_TSN_GE(net->pseudo_cumack, net->fast_recovery_tsn)) {
				net->will_exit_fast_recovery = 1;
			}
		}
#endif
		/* if nothing was acked on this destination skip it */
		if (net->net_ack == 0) {
			if (SCTP_BASE_SYSCTL(sctp_logging_level) & SCTP_CWND_LOGGING_ENABLE) {
				sctp_log_cwnd(stcb, net, 0, SCTP_CWND_LOG_FROM_SACK);
			}
			continue;
		}
#ifdef JANA_CMT_FAST_RECOVERY
		/*
		 * CMT fast recovery code
		 */
		/*
		 * if (sctp_cmt_on_off > 0 && net->fast_retran_loss_recovery
		 * && net->will_exit_fast_recovery == 0) { @@@ Do something
		 * } else if (sctp_cmt_on_off == 0 &&
		 * asoc->fast_retran_loss_recovery && will_exit == 0) {
		 */
#endif

		if (asoc->fast_retran_loss_recovery &&
		    (will_exit == 0) &&
		    (asoc->sctp_cmt_on_off == 0)) {
			/*
			 * If we are in loss recovery we skip any cwnd
			 * update
			 */
			return;
		}
		/*
		 * CMT: CUC algorithm. Update cwnd if pseudo-cumack has
		 * moved.
		 */
		if (accum_moved ||
		    ((asoc->sctp_cmt_on_off > 0) && net->new_pseudo_cumack)) {
			/* If the cumulative ack moved we can proceed */
			if (net->cwnd <= net->ssthresh) {
				/* We are in slow start */
				if (net->flight_size + net->net_ack >= net->cwnd) {
					sctp_hs_cwnd_increase(stcb, net);
				} else {
					if (SCTP_BASE_SYSCTL(sctp_logging_level) & SCTP_CWND_LOGGING_ENABLE) {
						sctp_log_cwnd(stcb, net, net->net_ack,
						    SCTP_CWND_LOG_NOADV_SS);
					}
				}
			} else {
				/* We are in congestion avoidance */
				net->partial_bytes_acked += net->net_ack;
				if ((net->flight_size + net->net_ack >= net->cwnd) &&
				    (net->partial_bytes_acked >= net->cwnd)) {
					net->partial_bytes_acked -= net->cwnd;
					net->cwnd += net->mtu;
					sctp_enforce_cwnd_limit(asoc, net);
					if (SCTP_BASE_SYSCTL(sctp_logging_level) & SCTP_CWND_MONITOR_ENABLE) {
						sctp_log_cwnd(stcb, net, net->mtu,
						    SCTP_CWND_LOG_FROM_CA);
					}
				} else {
					if (SCTP_BASE_SYSCTL(sctp_logging_level) & SCTP_CWND_LOGGING_ENABLE) {
						sctp_log_cwnd(stcb, net, net->net_ack,
						    SCTP_CWND_LOG_NOADV_CA);
					}
				}
			}
		} else {
			if (SCTP_BASE_SYSCTL(sctp_logging_level) & SCTP_CWND_LOGGING_ENABLE) {
				sctp_log_cwnd(stcb, net, net->mtu,
				    SCTP_CWND_LOG_NO_CUMACK);
			}
		}
	}
}


/*
 * H-TCP congestion control. The algorithm is detailed in:
 * R.N.Shorten, D.J.Leith:
 *   "H-TCP: TCP for high-speed and long-distance networks"
 *   Proc. PFLDnet, Argonne, 2004.
 * http://www.hamilton.ie/net/htcp3.pdf
 */


static int use_rtt_scaling = 1;
static int use_bandwidth_switch = 1;

static inline int
between(uint32_t seq1, uint32_t seq2, uint32_t seq3)
{
	return (seq3 - seq2 >= seq1 - seq2);
}

static inline uint32_t
htcp_cong_time(struct htcp *ca)
{
	return (sctp_get_tick_count() - ca->last_cong);
}

static inline uint32_t
htcp_ccount(struct htcp *ca)
{
	return (htcp_cong_time(ca) / ca->minRTT);
}

static inline void
htcp_reset(struct htcp *ca)
{
	ca->undo_last_cong = ca->last_cong;
	ca->undo_maxRTT = ca->maxRTT;
	ca->undo_old_maxB = ca->old_maxB;
	ca->last_cong = sctp_get_tick_count();
}

#ifdef SCTP_NOT_USED

static uint32_t
htcp_cwnd_undo(struct sctp_tcb *stcb, struct sctp_nets *net)
{
	net->cc_mod.htcp_ca.last_cong = net->cc_mod.htcp_ca.undo_last_cong;
	net->cc_mod.htcp_ca.maxRTT = net->cc_mod.htcp_ca.undo_maxRTT;
	net->cc_mod.htcp_ca.old_maxB = net->cc_mod.htcp_ca.undo_old_maxB;
	return (max(net->cwnd, ((net->ssthresh / net->mtu << 7) / net->cc_mod.htcp_ca.beta) * net->mtu));
}

#endif

static inline void
measure_rtt(struct sctp_nets *net)
{
	uint32_t srtt = net->lastsa >> SCTP_RTT_SHIFT;

	/* keep track of minimum RTT seen so far, minRTT is zero at first */
	if (net->cc_mod.htcp_ca.minRTT > srtt || !net->cc_mod.htcp_ca.minRTT)
		net->cc_mod.htcp_ca.minRTT = srtt;

	/* max RTT */
	if (net->fast_retran_ip == 0 && net->ssthresh < 0xFFFF && htcp_ccount(&net->cc_mod.htcp_ca) > 3) {
		if (net->cc_mod.htcp_ca.maxRTT < net->cc_mod.htcp_ca.minRTT)
			net->cc_mod.htcp_ca.maxRTT = net->cc_mod.htcp_ca.minRTT;
		if (net->cc_mod.htcp_ca.maxRTT < srtt && srtt <= net->cc_mod.htcp_ca.maxRTT + MSEC_TO_TICKS(20))
			net->cc_mod.htcp_ca.maxRTT = srtt;
	}
}

static void
measure_achieved_throughput(struct sctp_nets *net)
{
	uint32_t now = sctp_get_tick_count();

	if (net->fast_retran_ip == 0)
		net->cc_mod.htcp_ca.bytes_acked = net->net_ack;

	if (!use_bandwidth_switch)
		return;

	/* achieved throughput calculations */
	/* JRS - not 100% sure of this statement */
	if (net->fast_retran_ip == 1) {
		net->cc_mod.htcp_ca.bytecount = 0;
		net->cc_mod.htcp_ca.lasttime = now;
		return;
	}

	net->cc_mod.htcp_ca.bytecount += net->net_ack;
	if ((net->cc_mod.htcp_ca.bytecount >= net->cwnd - (((net->cc_mod.htcp_ca.alpha >> 7) ? (net->cc_mod.htcp_ca.alpha >> 7) : 1) * net->mtu)) &&
	    (now - net->cc_mod.htcp_ca.lasttime >= net->cc_mod.htcp_ca.minRTT) &&
	    (net->cc_mod.htcp_ca.minRTT > 0)) {
		uint32_t cur_Bi = net->cc_mod.htcp_ca.bytecount / net->mtu * hz / (now - net->cc_mod.htcp_ca.lasttime);

		if (htcp_ccount(&net->cc_mod.htcp_ca) <= 3) {
			/* just after backoff */
			net->cc_mod.htcp_ca.minB = net->cc_mod.htcp_ca.maxB = net->cc_mod.htcp_ca.Bi = cur_Bi;
		} else {
			net->cc_mod.htcp_ca.Bi = (3 * net->cc_mod.htcp_ca.Bi + cur_Bi) / 4;
			if (net->cc_mod.htcp_ca.Bi > net->cc_mod.htcp_ca.maxB)
				net->cc_mod.htcp_ca.maxB = net->cc_mod.htcp_ca.Bi;
			if (net->cc_mod.htcp_ca.minB > net->cc_mod.htcp_ca.maxB)
				net->cc_mod.htcp_ca.minB = net->cc_mod.htcp_ca.maxB;
		}
		net->cc_mod.htcp_ca.bytecount = 0;
		net->cc_mod.htcp_ca.lasttime = now;
	}
}

static inline void
htcp_beta_update(struct htcp *ca, uint32_t minRTT, uint32_t maxRTT)
{
	if (use_bandwidth_switch) {
		uint32_t maxB = ca->maxB;
		uint32_t old_maxB = ca->old_maxB;

		ca->old_maxB = ca->maxB;

		if (!between(5 * maxB, 4 * old_maxB, 6 * old_maxB)) {
			ca->beta = BETA_MIN;
			ca->modeswitch = 0;
			return;
		}
	}

	if (ca->modeswitch && minRTT > (uint32_t)MSEC_TO_TICKS(10) && maxRTT) {
		ca->beta = (minRTT << 7) / maxRTT;
		if (ca->beta < BETA_MIN)
			ca->beta = BETA_MIN;
		else if (ca->beta > BETA_MAX)
			ca->beta = BETA_MAX;
	} else {
		ca->beta = BETA_MIN;
		ca->modeswitch = 1;
	}
}

static inline void
htcp_alpha_update(struct htcp *ca)
{
	uint32_t minRTT = ca->minRTT;
	uint32_t factor = 1;
	uint32_t diff = htcp_cong_time(ca);

	if (diff > (uint32_t)hz) {
		diff -= hz;
		factor = 1 + (10 * diff + ((diff / 2) * (diff / 2) / hz)) / hz;
	}

	if (use_rtt_scaling && minRTT) {
		uint32_t scale = (hz << 3) / (10 * minRTT);

		scale = min(max(scale, 1U << 2), 10U << 3);	/* clamping ratio to
								 * interval [0.5,10]<<3 */
		factor = (factor << 3) / scale;
		if (!factor)
			factor = 1;
	}

	ca->alpha = 2 * factor * ((1 << 7) - ca->beta);
	if (!ca->alpha)
		ca->alpha = ALPHA_BASE;
}

/* After we have the rtt data to calculate beta, we'd still prefer to wait one
 * rtt before we adjust our beta to ensure we are working from a consistent
 * data.
 *
 * This function should be called when we hit a congestion event since only at
 * that point do we really have a real sense of maxRTT (the queues en route
 * were getting just too full now).
 */
static void
htcp_param_update(struct sctp_nets *net)
{
	uint32_t minRTT = net->cc_mod.htcp_ca.minRTT;
	uint32_t maxRTT = net->cc_mod.htcp_ca.maxRTT;

	htcp_beta_update(&net->cc_mod.htcp_ca, minRTT, maxRTT);
	htcp_alpha_update(&net->cc_mod.htcp_ca);

	/*
	 * add slowly fading memory for maxRTT to accommodate routing
	 * changes etc
	 */
	if (minRTT > 0 && maxRTT > minRTT)
		net->cc_mod.htcp_ca.maxRTT = minRTT + ((maxRTT - minRTT) * 95) / 100;
}

static uint32_t
htcp_recalc_ssthresh(struct sctp_nets *net)
{
	htcp_param_update(net);
	return (max(((net->cwnd / net->mtu * net->cc_mod.htcp_ca.beta) >> 7) * net->mtu, 2U * net->mtu));
}

static void
htcp_cong_avoid(struct sctp_tcb *stcb, struct sctp_nets *net)
{
	/*-
	 * How to handle these functions?
         *	if (!tcp_is_cwnd_limited(sk, in_flight)) RRS - good question.
	 *		return;
	 */
	if (net->cwnd <= net->ssthresh) {
		/* We are in slow start */
		if (net->flight_size + net->net_ack >= net->cwnd) {
			if (net->net_ack > (net->mtu * SCTP_BASE_SYSCTL(sctp_L2_abc_variable))) {
				net->cwnd += (net->mtu * SCTP_BASE_SYSCTL(sctp_L2_abc_variable));
				if (SCTP_BASE_SYSCTL(sctp_logging_level) & SCTP_CWND_MONITOR_ENABLE) {
					sctp_log_cwnd(stcb, net, net->mtu,
					    SCTP_CWND_LOG_FROM_SS);
				}

			} else {
				net->cwnd += net->net_ack;
				if (SCTP_BASE_SYSCTL(sctp_logging_level) & SCTP_CWND_MONITOR_ENABLE) {
					sctp_log_cwnd(stcb, net, net->net_ack,
					    SCTP_CWND_LOG_FROM_SS);
				}

			}
			sctp_enforce_cwnd_limit(&stcb->asoc, net);
		} else {
			if (SCTP_BASE_SYSCTL(sctp_logging_level) & SCTP_CWND_LOGGING_ENABLE) {
				sctp_log_cwnd(stcb, net, net->net_ack,
				    SCTP_CWND_LOG_NOADV_SS);
			}
		}
	} else {
		measure_rtt(net);

		/*
		 * In dangerous area, increase slowly. In theory this is
		 * net->cwnd += alpha / net->cwnd
		 */
		/* What is snd_cwnd_cnt?? */
		if (((net->partial_bytes_acked / net->mtu * net->cc_mod.htcp_ca.alpha) >> 7) * net->mtu >= net->cwnd) {
			/*-
			 * Does SCTP have a cwnd clamp?
			 * if (net->snd_cwnd < net->snd_cwnd_clamp) - Nope (RRS).
			 */
			net->cwnd += net->mtu;
			net->partial_bytes_acked = 0;
			sctp_enforce_cwnd_limit(&stcb->asoc, net);
			htcp_alpha_update(&net->cc_mod.htcp_ca);
			if (SCTP_BASE_SYSCTL(sctp_logging_level) & SCTP_CWND_MONITOR_ENABLE) {
				sctp_log_cwnd(stcb, net, net->mtu,
				    SCTP_CWND_LOG_FROM_CA);
			}
		} else {
			net->partial_bytes_acked += net->net_ack;
			if (SCTP_BASE_SYSCTL(sctp_logging_level) & SCTP_CWND_LOGGING_ENABLE) {
				sctp_log_cwnd(stcb, net, net->net_ack,
				    SCTP_CWND_LOG_NOADV_CA);
			}
		}

		net->cc_mod.htcp_ca.bytes_acked = net->mtu;
	}
}

#ifdef SCTP_NOT_USED
/* Lower bound on congestion window. */
static uint32_t
htcp_min_cwnd(struct sctp_tcb *stcb, struct sctp_nets *net)
{
	return (net->ssthresh);
}
#endif

static void
htcp_init(struct sctp_nets *net)
{
	memset(&net->cc_mod.htcp_ca, 0, sizeof(struct htcp));
	net->cc_mod.htcp_ca.alpha = ALPHA_BASE;
	net->cc_mod.htcp_ca.beta = BETA_MIN;
	net->cc_mod.htcp_ca.bytes_acked = net->mtu;
	net->cc_mod.htcp_ca.last_cong = sctp_get_tick_count();
}

static void
sctp_htcp_set_initial_cc_param(struct sctp_tcb *stcb, struct sctp_nets *net)
{
	/*
	 * We take the max of the burst limit times a MTU or the
	 * INITIAL_CWND. We then limit this to 4 MTU's of sending.
	 */
	net->cwnd = min((net->mtu * 4), max((2 * net->mtu), SCTP_INITIAL_CWND));
	net->ssthresh = stcb->asoc.peers_rwnd;
	sctp_enforce_cwnd_limit(&stcb->asoc, net);
	htcp_init(net);

	if (SCTP_BASE_SYSCTL(sctp_logging_level) & (SCTP_CWND_MONITOR_ENABLE | SCTP_CWND_LOGGING_ENABLE)) {
		sctp_log_cwnd(stcb, net, 0, SCTP_CWND_INITIALIZATION);
	}
}

static void
sctp_htcp_cwnd_update_after_sack(struct sctp_tcb *stcb,
    struct sctp_association *asoc,
    int accum_moved, int reneged_all SCTP_UNUSED, int will_exit)
{
	struct sctp_nets *net;

	/******************************/
	/* update cwnd and Early FR   */
	/******************************/
	TAILQ_FOREACH(net, &asoc->nets, sctp_next) {

#ifdef JANA_CMT_FAST_RECOVERY
		/*
		 * CMT fast recovery code. Need to debug.
		 */
		if (net->fast_retran_loss_recovery && net->new_pseudo_cumack) {
			if (SCTP_TSN_GE(asoc->last_acked_seq, net->fast_recovery_tsn) ||
			    SCTP_TSN_GE(net->pseudo_cumack, net->fast_recovery_tsn)) {
				net->will_exit_fast_recovery = 1;
			}
		}
#endif
		/* if nothing was acked on this destination skip it */
		if (net->net_ack == 0) {
			if (SCTP_BASE_SYSCTL(sctp_logging_level) & SCTP_CWND_LOGGING_ENABLE) {
				sctp_log_cwnd(stcb, net, 0, SCTP_CWND_LOG_FROM_SACK);
			}
			continue;
		}
#ifdef JANA_CMT_FAST_RECOVERY
		/*
		 * CMT fast recovery code
		 */
		/*
		 * if (sctp_cmt_on_off > 0 && net->fast_retran_loss_recovery
		 * && net->will_exit_fast_recovery == 0) { @@@ Do something
		 * } else if (sctp_cmt_on_off == 0 &&
		 * asoc->fast_retran_loss_recovery && will_exit == 0) {
		 */
#endif

		if (asoc->fast_retran_loss_recovery &&
		    will_exit == 0 &&
		    (asoc->sctp_cmt_on_off == 0)) {
			/*
			 * If we are in loss recovery we skip any cwnd
			 * update
			 */
			return;
		}
		/*
		 * CMT: CUC algorithm. Update cwnd if pseudo-cumack has
		 * moved.
		 */
		if (accum_moved ||
		    ((asoc->sctp_cmt_on_off > 0) && net->new_pseudo_cumack)) {
			htcp_cong_avoid(stcb, net);
			measure_achieved_throughput(net);
		} else {
			if (SCTP_BASE_SYSCTL(sctp_logging_level) & SCTP_CWND_LOGGING_ENABLE) {
				sctp_log_cwnd(stcb, net, net->mtu,
				    SCTP_CWND_LOG_NO_CUMACK);
			}
		}
	}
}

static void
sctp_htcp_cwnd_update_after_fr(struct sctp_tcb *stcb,
    struct sctp_association *asoc)
{
	struct sctp_nets *net;

	/*
	 * CMT fast recovery code. Need to debug. ((sctp_cmt_on_off > 0) &&
	 * (net->fast_retran_loss_recovery == 0)))
	 */
	TAILQ_FOREACH(net, &asoc->nets, sctp_next) {
		if ((asoc->fast_retran_loss_recovery == 0) ||
		    (asoc->sctp_cmt_on_off > 0)) {
			/* out of a RFC2582 Fast recovery window? */
			if (net->net_ack > 0) {
				/*
				 * per section 7.2.3, are there any
				 * destinations that had a fast retransmit
				 * to them. If so what we need to do is
				 * adjust ssthresh and cwnd.
				 */
				struct sctp_tmit_chunk *lchk;
				int old_cwnd = net->cwnd;

				/* JRS - reset as if state were changed */
				htcp_reset(&net->cc_mod.htcp_ca);
				net->ssthresh = htcp_recalc_ssthresh(net);
				net->cwnd = net->ssthresh;
				sctp_enforce_cwnd_limit(asoc, net);
				if (SCTP_BASE_SYSCTL(sctp_logging_level) & SCTP_CWND_MONITOR_ENABLE) {
					sctp_log_cwnd(stcb, net, (net->cwnd - old_cwnd),
					    SCTP_CWND_LOG_FROM_FR);
				}
				lchk = TAILQ_FIRST(&asoc->send_queue);

				net->partial_bytes_acked = 0;
				/* Turn on fast recovery window */
				asoc->fast_retran_loss_recovery = 1;
				if (lchk == NULL) {
					/* Mark end of the window */
					asoc->fast_recovery_tsn = asoc->sending_seq - 1;
				} else {
					asoc->fast_recovery_tsn = lchk->rec.data.tsn - 1;
				}

				/*
				 * CMT fast recovery -- per destination
				 * recovery variable.
				 */
				net->fast_retran_loss_recovery = 1;

				if (lchk == NULL) {
					/* Mark end of the window */
					net->fast_recovery_tsn = asoc->sending_seq - 1;
				} else {
					net->fast_recovery_tsn = lchk->rec.data.tsn - 1;
				}

				sctp_timer_stop(SCTP_TIMER_TYPE_SEND,
				    stcb->sctp_ep, stcb, net,
				    SCTP_FROM_SCTP_CC_FUNCTIONS + SCTP_LOC_3);
				sctp_timer_start(SCTP_TIMER_TYPE_SEND,
				    stcb->sctp_ep, stcb, net);
			}
		} else if (net->net_ack > 0) {
			/*
			 * Mark a peg that we WOULD have done a cwnd
			 * reduction but RFC2582 prevented this action.
			 */
			SCTP_STAT_INCR(sctps_fastretransinrtt);
		}
	}
}

static void
sctp_htcp_cwnd_update_after_timeout(struct sctp_tcb *stcb,
    struct sctp_nets *net)
{
	int old_cwnd = net->cwnd;

	/* JRS - reset as if the state were being changed to timeout */
	htcp_reset(&net->cc_mod.htcp_ca);
	net->ssthresh = htcp_recalc_ssthresh(net);
	net->cwnd = net->mtu;
	net->partial_bytes_acked = 0;
	if (SCTP_BASE_SYSCTL(sctp_logging_level) & SCTP_CWND_MONITOR_ENABLE) {
		sctp_log_cwnd(stcb, net, net->cwnd - old_cwnd, SCTP_CWND_LOG_FROM_RTX);
	}
}

static void
sctp_htcp_cwnd_update_after_ecn_echo(struct sctp_tcb *stcb,
    struct sctp_nets *net, int in_window, int num_pkt_lost SCTP_UNUSED)
{
	int old_cwnd;

	old_cwnd = net->cwnd;

	/* JRS - reset hctp as if state changed */
	if (in_window == 0) {
		htcp_reset(&net->cc_mod.htcp_ca);
		SCTP_STAT_INCR(sctps_ecnereducedcwnd);
		net->ssthresh = htcp_recalc_ssthresh(net);
		if (net->ssthresh < net->mtu) {
			net->ssthresh = net->mtu;
			/* here back off the timer as well, to slow us down */
			net->RTO <<= 1;
		}
		net->cwnd = net->ssthresh;
		sctp_enforce_cwnd_limit(&stcb->asoc, net);
		if (SCTP_BASE_SYSCTL(sctp_logging_level) & SCTP_CWND_MONITOR_ENABLE) {
			sctp_log_cwnd(stcb, net, (net->cwnd - old_cwnd), SCTP_CWND_LOG_FROM_SAT);
		}
	}
}

const struct sctp_cc_functions sctp_cc_functions[] = {
	{
		.sctp_set_initial_cc_param = sctp_set_initial_cc_param,
		.sctp_cwnd_update_after_sack = sctp_cwnd_update_after_sack,
		.sctp_cwnd_update_exit_pf = sctp_cwnd_update_exit_pf_common,
		.sctp_cwnd_update_after_fr = sctp_cwnd_update_after_fr,
		.sctp_cwnd_update_after_timeout = sctp_cwnd_update_after_timeout,
		.sctp_cwnd_update_after_ecn_echo = sctp_cwnd_update_after_ecn_echo,
		.sctp_cwnd_update_after_packet_dropped = sctp_cwnd_update_after_packet_dropped,
		.sctp_cwnd_update_after_output = sctp_cwnd_update_after_output,
	},
	{
		.sctp_set_initial_cc_param = sctp_set_initial_cc_param,
		.sctp_cwnd_update_after_sack = sctp_hs_cwnd_update_after_sack,
		.sctp_cwnd_update_exit_pf = sctp_cwnd_update_exit_pf_common,
		.sctp_cwnd_update_after_fr = sctp_hs_cwnd_update_after_fr,
		.sctp_cwnd_update_after_timeout = sctp_cwnd_update_after_timeout,
		.sctp_cwnd_update_after_ecn_echo = sctp_cwnd_update_after_ecn_echo,
		.sctp_cwnd_update_after_packet_dropped = sctp_cwnd_update_after_packet_dropped,
		.sctp_cwnd_update_after_output = sctp_cwnd_update_after_output,
	},
	{
		.sctp_set_initial_cc_param = sctp_htcp_set_initial_cc_param,
		.sctp_cwnd_update_after_sack = sctp_htcp_cwnd_update_after_sack,
		.sctp_cwnd_update_exit_pf = sctp_cwnd_update_exit_pf_common,
		.sctp_cwnd_update_after_fr = sctp_htcp_cwnd_update_after_fr,
		.sctp_cwnd_update_after_timeout = sctp_htcp_cwnd_update_after_timeout,
		.sctp_cwnd_update_after_ecn_echo = sctp_htcp_cwnd_update_after_ecn_echo,
		.sctp_cwnd_update_after_packet_dropped = sctp_cwnd_update_after_packet_dropped,
		.sctp_cwnd_update_after_output = sctp_cwnd_update_after_output,
	},
	{
		.sctp_set_initial_cc_param = sctp_set_rtcc_initial_cc_param,
		.sctp_cwnd_update_after_sack = sctp_cwnd_update_rtcc_after_sack,
		.sctp_cwnd_update_exit_pf = sctp_cwnd_update_exit_pf_common,
		.sctp_cwnd_update_after_fr = sctp_cwnd_update_after_fr,
		.sctp_cwnd_update_after_timeout = sctp_cwnd_update_after_timeout,
		.sctp_cwnd_update_after_ecn_echo = sctp_cwnd_update_rtcc_after_ecn_echo,
		.sctp_cwnd_update_after_packet_dropped = sctp_cwnd_update_after_packet_dropped,
		.sctp_cwnd_update_after_output = sctp_cwnd_update_after_output,
		.sctp_cwnd_update_packet_transmitted = sctp_cwnd_update_rtcc_packet_transmitted,
		.sctp_cwnd_update_tsn_acknowledged = sctp_cwnd_update_rtcc_tsn_acknowledged,
		.sctp_cwnd_new_transmission_begins = sctp_cwnd_new_rtcc_transmission_begins,
		.sctp_cwnd_prepare_net_for_sack = sctp_cwnd_prepare_rtcc_net_for_sack,
		.sctp_cwnd_socket_option = sctp_cwnd_rtcc_socket_option,
		.sctp_rtt_calculated = sctp_rtt_rtcc_calculated
	}
};
