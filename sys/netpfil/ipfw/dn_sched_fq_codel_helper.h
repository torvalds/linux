/*
 * Codel - The Controlled-Delay Active Queue Management algorithm.
 *
 * $FreeBSD$
 * 
 * Copyright (C) 2016 Centre for Advanced Internet Architectures,
 *  Swinburne University of Technology, Melbourne, Australia.
 * Portions of this code were made possible in part by a gift from 
 *  The Comcast Innovation Fund.
 * Implemented by Rasool Al-Saadi <ralsaadi@swin.edu.au>
 *
 * Copyright (C) 2011-2014 Kathleen Nichols <nichols@pollere.com>.
 * 
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 *
 * o  Redistributions of source code must retain the above copyright
 *  notice, this list of conditions, and the following disclaimer,
 *  without modification.
 *
 * o  Redistributions in binary form must reproduce the above copyright
 *  notice, this list of conditions and the following disclaimer in
 *  the documentation and/or other materials provided with the
 *  distribution.
 * 
 * o  The names of the authors may not be used to endorse or promote
 *  products derived from this software without specific prior written
 *  permission.
 *
 * Alternatively, provided that this notice is retained in full, this
 * software may be distributed under the terms of the GNU General Public
 * License ("GPL") version 2, in which case the provisions of the GPL
 * apply INSTEAD OF those given above.

 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 * "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 * LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
 * A PARTICULAR PURPOSE ARE DISCLAIMED.  IN NO EVENT SHALL THE COPYRIGHT
 * OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
 * SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
 * LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 * DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
 * THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
 * OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

#ifndef _IP_DN_SCHED_FQ_CODEL_HELPER_H
#define _IP_DN_SCHED_FQ_CODEL_HELPER_H

__inline static struct mbuf *
fqc_dodequeue(struct fq_codel_flow *q, aqm_time_t now, uint16_t *ok_to_drop,
	struct fq_codel_si *si)
{
	struct mbuf * m;
	struct fq_codel_schk *schk = (struct fq_codel_schk *)(si->_si.sched+1);
	aqm_time_t  pkt_ts, sojourn_time;

	*ok_to_drop = 0;
	m = fq_codel_extract_head(q, &pkt_ts, si);

	if (m == NULL) {
		/*queue is empty - we can't be above target*/
		q->cst.first_above_time= 0;
		return m;
	}

	/* To span a large range of bandwidths, CoDel runs two
	 * different AQMs in parallel. One is sojourn-time-based
	 * and takes effect when the time to send an MTU-sized
	 * packet is less than target.  The 1st term of the "if"
	 * below does this.  The other is backlog-based and takes
	 * effect when the time to send an MTU-sized packet is >=
	* target. The goal here is to keep the output link
	* utilization high by never allowing the queue to get
	* smaller than the amount that arrives in a typical
	 * interarrival time (MTU-sized packets arriving spaced
	 * by the amount of time it takes to send such a packet on
	 * the bottleneck). The 2nd term of the "if" does this.
	 */
	sojourn_time = now - pkt_ts;
	if (sojourn_time < schk->cfg.ccfg.target || q->stats.len_bytes <= q->cst.maxpkt_size) {
		/* went below - stay below for at least interval */
		q->cst.first_above_time = 0;
	} else {
		if (q->cst.first_above_time == 0) {
			/* just went above from below. if still above at
			 * first_above_time, will say it's ok to drop. */
			q->cst.first_above_time = now + schk->cfg.ccfg.interval;
		} else if (now >= q->cst.first_above_time) {
			*ok_to_drop = 1;
		}
	}
	return m;
}

/* Codel dequeue function */
__inline static struct mbuf * 
fqc_codel_dequeue(struct fq_codel_flow *q, struct fq_codel_si *si)
{
	struct mbuf *m;
	struct dn_aqm_codel_parms *cprms;
	struct codel_status *cst;
	aqm_time_t now;
	uint16_t ok_to_drop;
	struct fq_codel_schk *schk = (struct fq_codel_schk *)(si->_si.sched+1);

	cst = &q->cst;
	cprms = &schk->cfg.ccfg;

	now = AQM_UNOW;
	m = fqc_dodequeue(q, now, &ok_to_drop, si);

	if (cst->dropping) {
		if (!ok_to_drop) {
			/* sojourn time below target - leave dropping state */
			cst->dropping = false;
		}

		/* Time for the next drop. Drop current packet and dequeue
		 * next.  If the dequeue doesn't take us out of dropping
		 * state, schedule the next drop. A large backlog might
		 * result in drop rates so high that the next drop should
		 * happen now, hence the 'while' loop.
		 */
		while (now >= cst->drop_next_time && cst->dropping) {

			/* mark the packet */
			if (cprms->flags & CODEL_ECN_ENABLED && ecn_mark(m)) {
				cst->count++;
				/* schedule the next mark. */
				cst->drop_next_time = control_law(cst, cprms, cst->drop_next_time);
				return m;
			}

			/* drop the packet */
			fq_update_stats(q, si, 0, 1);
			m_freem(m);
			m = fqc_dodequeue(q, now, &ok_to_drop, si);

			if (!ok_to_drop) {
				/* leave dropping state */
				cst->dropping = false;
			} else {
				cst->count++;
				/* schedule the next drop. */
				cst->drop_next_time = control_law(cst, cprms, cst->drop_next_time);
			}
		}
	/* If we get here we're not in dropping state. The 'ok_to_drop'
	 * return from dodequeue means that the sojourn time has been
	 * above 'target' for 'interval' so enter dropping state.
	 */
	} else if (ok_to_drop) {

		/* if ECN option is disabled or the packet cannot be marked,
		 * drop the packet and extract another.
		 */
		if (!(cprms->flags & CODEL_ECN_ENABLED) || !ecn_mark(m)) {
			fq_update_stats(q, si, 0, 1);
			m_freem(m);
			m = fqc_dodequeue(q, now, &ok_to_drop,si);
		}

		cst->dropping = true;

		/* If min went above target close to when it last went
		 * below, assume that the drop rate that controlled the
		 * queue on the last cycle is a good starting point to
		 * control it now. ('drop_next' will be at most 'interval'
		 * later than the time of the last drop so 'now - drop_next'
		 * is a good approximation of the time from the last drop
		 * until now.)
		 */
		cst->count = (cst->count > 2 && ((aqm_stime_t)now - 
			(aqm_stime_t)cst->drop_next_time) < 8* cprms->interval)? cst->count - 2 : 1;

		/* we don't have to set initial guess for Newton's method isqrt as
		 * we initilaize  isqrt in control_law function when count == 1 */
		cst->drop_next_time = control_law(cst, cprms, now);
	}

	return m;
}

#endif
