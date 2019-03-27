/*-
 * Copyright (C) 2016 Centre for Advanced Internet Architectures,
 *  Swinburne University of Technology, Melbourne, Australia.
 * Portions of this code were made possible in part by a gift from 
 *  The Comcast Innovation Fund.
 * Implemented by Rasool Al-Saadi <ralsaadi@swin.edu.au>
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

/*
 * FQ_Codel Structures and helper functions
 *
 * $FreeBSD$
 */

#ifndef _IP_DN_SCHED_FQ_CODEL_H
#define _IP_DN_SCHED_FQ_CODEL_H

/* list of queues */
STAILQ_HEAD(fq_codel_list, fq_codel_flow) ;

/* fq_codel parameters including codel */
struct dn_sch_fq_codel_parms {
	struct dn_aqm_codel_parms	ccfg;	/* CoDel Parameters */
	/* FQ_CODEL Parameters */
	uint32_t flows_cnt;	/* number of flows */
	uint32_t limit;	/* hard limit of fq_codel queue size*/
	uint32_t quantum;
};	/* defaults */

/* flow (sub-queue) stats */
struct flow_stats {
	uint64_t tot_pkts;	/* statistics counters  */
	uint64_t tot_bytes;
	uint32_t length;		/* Queue length, in packets */
	uint32_t len_bytes;	/* Queue length, in bytes */
	uint32_t drops;
};

/* A flow of packets (sub-queue).*/
struct fq_codel_flow {
	struct mq	mq;	/* list of packets */
	struct flow_stats stats;	/* statistics */
	int	deficit;
	int active;		/* 1: flow is active (in a list) */
	struct codel_status cst;
	STAILQ_ENTRY(fq_codel_flow) flowchain;
};

/* extra fq_codel scheduler configurations */
struct fq_codel_schk {
	struct dn_sch_fq_codel_parms cfg;
};

/* fq_codel scheduler instance */
struct fq_codel_si {
	struct dn_sch_inst _si;	/* standard scheduler instance */
	struct dn_queue main_q; /* main queue is after si directly */

	struct fq_codel_flow *flows; /* array of flows (queues) */
	uint32_t perturbation; /* random value */
	struct fq_codel_list newflows;	/* list of new queues */
	struct fq_codel_list oldflows;		/* list of old queues */
};

/* Helper function to update queue&main-queue and scheduler statistics.
 * negative len + drop -> drop
 * negative len -> dequeue
 * positive len -> enqueue
 * positive len + drop -> drop during enqueue
 */
__inline static void
fq_update_stats(struct fq_codel_flow *q, struct fq_codel_si *si, int len,
	int drop)
{
	int inc = 0;

	if (len < 0) 
		inc = -1;
	else if (len > 0)
		inc = 1;

	if (drop) {
		si->main_q.ni.drops ++;
		q->stats.drops ++;
		si->_si.ni.drops ++;
		io_pkt_drop ++;
	} 

	if (!drop || (drop && len < 0)) {
		/* Update stats for the main queue */
		si->main_q.ni.length += inc;
		si->main_q.ni.len_bytes += len;

		/*update sub-queue stats */
		q->stats.length += inc;
		q->stats.len_bytes += len;

		/*update scheduler instance stats */
		si->_si.ni.length += inc;
		si->_si.ni.len_bytes += len;
	}

	if (inc > 0) {
		si->main_q.ni.tot_bytes += len;
		si->main_q.ni.tot_pkts ++;
		
		q->stats.tot_bytes +=len;
		q->stats.tot_pkts++;
		
		si->_si.ni.tot_bytes +=len;
		si->_si.ni.tot_pkts ++;
	}

}

/* extract the head of fq_codel sub-queue */
__inline static struct mbuf *
fq_codel_extract_head(struct fq_codel_flow *q, aqm_time_t *pkt_ts, struct fq_codel_si *si)
{
	struct mbuf *m = q->mq.head;

	if (m == NULL)
		return m;
	q->mq.head = m->m_nextpkt;

	fq_update_stats(q, si, -m->m_pkthdr.len, 0);

	if (si->main_q.ni.length == 0) /* queue is now idle */
			si->main_q.q_time = dn_cfg.curr_time;

	/* extract packet timestamp*/
	struct m_tag *mtag;
	mtag = m_tag_locate(m, MTAG_ABI_COMPAT, DN_AQM_MTAG_TS, NULL);
	if (mtag == NULL){
		D("timestamp tag is not found!");
		*pkt_ts = 0;
	} else {
		*pkt_ts = *(aqm_time_t *)(mtag + 1);
		m_tag_delete(m,mtag); 
	}

	return m;
}


#endif
