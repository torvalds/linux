/*
 * PIE - Proportional Integral controller Enhanced AQM algorithm.
 *
 * $FreeBSD$
 * 
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

#include <sys/cdefs.h>
#include "opt_inet6.h"

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/malloc.h>
#include <sys/mbuf.h>
#include <sys/kernel.h>
#include <sys/lock.h>
#include <sys/module.h>
#include <sys/mutex.h>
#include <sys/priv.h>
#include <sys/proc.h>
#include <sys/rwlock.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <sys/sysctl.h>

#include <net/if.h>	/* IFNAMSIZ, struct ifaddr, ifq head, lock.h mutex.h */
#include <net/netisr.h>
#include <net/vnet.h>

#include <netinet/in.h>
#include <netinet/ip.h>		/* ip_len, ip_off */
#include <netinet/ip_var.h>	/* ip_output(), IP_FORWARDING */
#include <netinet/ip_fw.h>
#include <netinet/ip_dummynet.h>
#include <netinet/if_ether.h> /* various ether_* routines */
#include <netinet/ip6.h>       /* for ip6_input, ip6_output prototypes */
#include <netinet6/ip6_var.h>
#include <netpfil/ipfw/dn_heap.h>

#ifdef NEW_AQM
#include <netpfil/ipfw/ip_fw_private.h>
#include <netpfil/ipfw/ip_dn_private.h>
#include <netpfil/ipfw/dn_aqm.h>
#include <netpfil/ipfw/dn_aqm_pie.h>
#include <netpfil/ipfw/dn_sched.h>

/* for debugging */
#include <sys/syslog.h>

static struct dn_aqm pie_desc;

/*  PIE defaults
 * target=15ms, tupdate=15ms, max_burst=150ms, 
 * max_ecnth=0.1, alpha=0.125, beta=1.25, 
 */
struct dn_aqm_pie_parms pie_sysctl = 
	{ 15 * AQM_TIME_1MS,  15 * AQM_TIME_1MS, 150 * AQM_TIME_1MS,
	PIE_SCALE/10 , PIE_SCALE * 0.125,  PIE_SCALE * 1.25 ,
	PIE_CAPDROP_ENABLED | PIE_DEPRATEEST_ENABLED | PIE_DERAND_ENABLED };

static int
pie_sysctl_alpha_beta_handler(SYSCTL_HANDLER_ARGS)
{
	int error;
	long  value;

	if (!strcmp(oidp->oid_name,"alpha"))
		value = pie_sysctl.alpha;
	else
		value = pie_sysctl.beta;
		
	value = value * 1000 / PIE_SCALE;
	error = sysctl_handle_long(oidp, &value, 0, req);
	if (error != 0 || req->newptr == NULL)
		return (error);
	if (value < 1 || value > 7 * PIE_SCALE)
		return (EINVAL);
	value = (value * PIE_SCALE) / 1000;
	if (!strcmp(oidp->oid_name,"alpha"))
			pie_sysctl.alpha = value;
	else
		pie_sysctl.beta = value;
	return (0);
}

static int
pie_sysctl_target_tupdate_maxb_handler(SYSCTL_HANDLER_ARGS)
{
	int error;
	long  value;

	if (!strcmp(oidp->oid_name,"target"))
		value = pie_sysctl.qdelay_ref;
	else if (!strcmp(oidp->oid_name,"tupdate"))
		value = pie_sysctl.tupdate;
	else
		value = pie_sysctl.max_burst;
	
	value = value / AQM_TIME_1US;
	error = sysctl_handle_long(oidp, &value, 0, req);
	if (error != 0 || req->newptr == NULL)
		return (error);
	if (value < 1 || value > 10 * AQM_TIME_1S)
		return (EINVAL);
	value = value * AQM_TIME_1US;
	
	if (!strcmp(oidp->oid_name,"target"))
		pie_sysctl.qdelay_ref  = value;
	else if (!strcmp(oidp->oid_name,"tupdate"))
		pie_sysctl.tupdate  = value;
	else
		pie_sysctl.max_burst = value;
	return (0);
}

static int
pie_sysctl_max_ecnth_handler(SYSCTL_HANDLER_ARGS)
{
	int error;
	long  value;

	value = pie_sysctl.max_ecnth;
	value = value * 1000 / PIE_SCALE;
	error = sysctl_handle_long(oidp, &value, 0, req);
	if (error != 0 || req->newptr == NULL)
		return (error);
	if (value < 1 || value > PIE_SCALE)
		return (EINVAL);
	value = (value * PIE_SCALE) / 1000;
	pie_sysctl.max_ecnth = value;
	return (0);
}

/* define PIE sysctl variables */
SYSBEGIN(f4)
SYSCTL_DECL(_net_inet);
SYSCTL_DECL(_net_inet_ip);
SYSCTL_DECL(_net_inet_ip_dummynet);
static SYSCTL_NODE(_net_inet_ip_dummynet, OID_AUTO, 
	pie, CTLFLAG_RW, 0, "PIE");

#ifdef SYSCTL_NODE
SYSCTL_PROC(_net_inet_ip_dummynet_pie, OID_AUTO, target,
	CTLTYPE_LONG | CTLFLAG_RW, NULL, 0, 
	pie_sysctl_target_tupdate_maxb_handler, "L",
	"queue target in microsecond");
SYSCTL_PROC(_net_inet_ip_dummynet_pie, OID_AUTO, tupdate,
	CTLTYPE_LONG | CTLFLAG_RW, NULL, 0,
	pie_sysctl_target_tupdate_maxb_handler, "L",
	"the frequency of drop probability calculation in microsecond");
SYSCTL_PROC(_net_inet_ip_dummynet_pie, OID_AUTO, max_burst,
	CTLTYPE_LONG | CTLFLAG_RW, NULL, 0,
	pie_sysctl_target_tupdate_maxb_handler, "L",
	"Burst allowance interval in microsecond");

SYSCTL_PROC(_net_inet_ip_dummynet_pie, OID_AUTO, max_ecnth,
	CTLTYPE_LONG | CTLFLAG_RW, NULL, 0,
	pie_sysctl_max_ecnth_handler, "L",
	"ECN safeguard threshold scaled by 1000");

SYSCTL_PROC(_net_inet_ip_dummynet_pie, OID_AUTO, alpha,
	CTLTYPE_LONG | CTLFLAG_RW, NULL, 0,
	pie_sysctl_alpha_beta_handler, "L",
	"PIE alpha scaled by 1000");
SYSCTL_PROC(_net_inet_ip_dummynet_pie, OID_AUTO, beta,
	CTLTYPE_LONG | CTLFLAG_RW, NULL, 0,
	pie_sysctl_alpha_beta_handler, "L",
	"beta scaled by 1000");
#endif


/*
 * Callout function for drop probability calculation 
 * This function is called over tupdate ms and takes pointer of PIE
 * status variables as an argument
  */
static void
calculate_drop_prob(void *x)
{
	int64_t p, prob, oldprob;
	struct dn_aqm_pie_parms *pprms;
	struct pie_status *pst = (struct pie_status *) x;
	int p_isneg;

	pprms = pst->parms;
	prob = pst->drop_prob;

	/* calculate current qdelay using DRE method.
	 * If TS is used and no data in the queue, reset current_qdelay
	 * as it stays at last value during dequeue process. 
	*/
	if (pprms->flags & PIE_DEPRATEEST_ENABLED)
		pst->current_qdelay = ((uint64_t)pst->pq->ni.len_bytes *
			pst->avg_dq_time) >> PIE_DQ_THRESHOLD_BITS;
	else 
		if (!pst->pq->ni.len_bytes)
			 pst->current_qdelay = 0;

	/* calculate drop probability */
	p = (int64_t)pprms->alpha * 
		((int64_t)pst->current_qdelay - (int64_t)pprms->qdelay_ref); 
	p +=(int64_t) pprms->beta * 
		((int64_t)pst->current_qdelay - (int64_t)pst->qdelay_old); 

	/* take absolute value so right shift result is well defined */
	p_isneg = p < 0;
	if (p_isneg) {
		p = -p;
	}
		
	/* We PIE_MAX_PROB shift by 12-bits to increase the division precision */
	p *= (PIE_MAX_PROB << 12) / AQM_TIME_1S;

	/* auto-tune drop probability */
	if (prob < (PIE_MAX_PROB / 1000000)) /* 0.000001 */
		p >>= 11 + PIE_FIX_POINT_BITS + 12;
	else if (prob < (PIE_MAX_PROB / 100000)) /* 0.00001 */
		p >>= 9 + PIE_FIX_POINT_BITS + 12;
	else if (prob < (PIE_MAX_PROB / 10000)) /* 0.0001 */
		p >>= 7 + PIE_FIX_POINT_BITS + 12;
	else if (prob < (PIE_MAX_PROB / 1000)) /* 0.001 */
		p >>= 5 + PIE_FIX_POINT_BITS + 12;
	else if (prob < (PIE_MAX_PROB / 100)) /* 0.01 */
		p >>= 3 + PIE_FIX_POINT_BITS + 12;
	else if (prob < (PIE_MAX_PROB / 10)) /* 0.1 */
		p >>= 1 + PIE_FIX_POINT_BITS + 12;
	else
		p >>= PIE_FIX_POINT_BITS + 12;

	oldprob = prob;

	if (p_isneg) {
		prob = prob - p;

		/* check for multiplication underflow */
		if (prob > oldprob) {
			prob= 0;
			D("underflow");
		}
	} else {
		/* Cap Drop adjustment */
		if ((pprms->flags & PIE_CAPDROP_ENABLED) &&
		    prob >= PIE_MAX_PROB / 10 &&
		    p > PIE_MAX_PROB / 50 ) {
			p = PIE_MAX_PROB / 50;
		}

		prob = prob + p;

		/* check for multiplication overflow */
		if (prob<oldprob) {
			D("overflow");
			prob= PIE_MAX_PROB;
		}
	}

	/*
	 * decay the drop probability exponentially
	 * and restrict it to range 0 to PIE_MAX_PROB
	 */
	if (prob < 0) {
		prob = 0;
	} else {
		if (pst->current_qdelay == 0 && pst->qdelay_old == 0) {
			/* 0.98 ~= 1- 1/64 */
			prob = prob - (prob >> 6); 
		}

		if (prob > PIE_MAX_PROB) {
			prob = PIE_MAX_PROB;
		}
	}

	pst->drop_prob = prob;
	
	/* store current queue delay value in old queue delay*/
	pst->qdelay_old = pst->current_qdelay;

	/* update burst allowance */
	if ((pst->sflags & PIE_ACTIVE) && pst->burst_allowance>0) {
		
		if (pst->burst_allowance > pprms->tupdate )
			pst->burst_allowance -= pprms->tupdate;
		else 
			pst->burst_allowance = 0;
	}

	/* reschedule calculate_drop_prob function */
	if (pst->sflags & PIE_ACTIVE)
		callout_reset_sbt(&pst->aqm_pie_callout,
			(uint64_t)pprms->tupdate * SBT_1US, 0, calculate_drop_prob, pst, 0);

	mtx_unlock(&pst->lock_mtx);
}

/*
 * Extract a packet from the head of queue 'q'
 * Return a packet or NULL if the queue is empty.
 * If getts is set, also extract packet's timestamp from mtag.
 */
static struct mbuf *
pie_extract_head(struct dn_queue *q, aqm_time_t *pkt_ts, int getts)
{
	struct m_tag *mtag;
	struct mbuf *m = q->mq.head;

	if (m == NULL)
		return m;
	q->mq.head = m->m_nextpkt;

	/* Update stats */
	update_stats(q, -m->m_pkthdr.len, 0);

	if (q->ni.length == 0) /* queue is now idle */
			q->q_time = dn_cfg.curr_time;

	if (getts) {
		/* extract packet TS*/
		mtag = m_tag_locate(m, MTAG_ABI_COMPAT, DN_AQM_MTAG_TS, NULL);
		if (mtag == NULL) {
			D("PIE timestamp mtag not found!");
			*pkt_ts = 0;
		} else {
			*pkt_ts = *(aqm_time_t *)(mtag + 1);
			m_tag_delete(m,mtag); 
		}
	}
	return m;
}

/* 
 * Initiate PIE  variable and optionally activate it
 */
__inline static void
init_activate_pie(struct pie_status *pst, int resettimer)
{
	struct dn_aqm_pie_parms *pprms;

	mtx_lock(&pst->lock_mtx);
	pprms = pst->parms;
	pst->drop_prob = 0;
	pst->qdelay_old = 0;
	pst->burst_allowance = pprms->max_burst;
	pst->accu_prob = 0;
	pst->dq_count = 0;
	pst->avg_dq_time = 0;
	pst->sflags = PIE_INMEASUREMENT;
	pst->measurement_start = AQM_UNOW;

	if (resettimer) {
		pst->sflags |= PIE_ACTIVE;
		callout_reset_sbt(&pst->aqm_pie_callout,
			(uint64_t)pprms->tupdate * SBT_1US,
			0, calculate_drop_prob, pst, 0);
	}
	//DX(2, "PIE Activated");
	mtx_unlock(&pst->lock_mtx);
}

/* 
 * Deactivate PIE and stop probe update callout 
 */
__inline static void
deactivate_pie(struct pie_status *pst)
{
	mtx_lock(&pst->lock_mtx);
	pst->sflags &= ~(PIE_ACTIVE | PIE_INMEASUREMENT);
	callout_stop(&pst->aqm_pie_callout);
	//D("PIE Deactivated");
	mtx_unlock(&pst->lock_mtx);
}

/* 
 * Dequeue and return a pcaket from queue 'q' or NULL if 'q' is empty.
 * Also, caculate depature time or queue delay using timestamp
 */
static struct mbuf *
aqm_pie_dequeue(struct dn_queue *q)
{
	struct mbuf *m;
	struct dn_flow *ni;	/* stats for scheduler instance */	
	struct dn_aqm_pie_parms *pprms;
	struct pie_status *pst;
	aqm_time_t now;
	aqm_time_t pkt_ts, dq_time;
	int32_t w;

	pst  = q->aqm_status;
	pprms = pst->parms;
	ni = &q->_si->ni;

	/*we extarct packet ts only when Departure Rate Estimation dis not used*/
	m = pie_extract_head(q, &pkt_ts, !(pprms->flags & PIE_DEPRATEEST_ENABLED));

	if (!m || !(pst->sflags & PIE_ACTIVE))
		return m;

	now = AQM_UNOW;
	if (pprms->flags & PIE_DEPRATEEST_ENABLED) {
		/* calculate average depature time */
		if(pst->sflags & PIE_INMEASUREMENT) {
			pst->dq_count += m->m_pkthdr.len;

			if (pst->dq_count >= PIE_DQ_THRESHOLD) {
				dq_time = now - pst->measurement_start;

				/* 
				 * if we don't have old avg dq_time i.e PIE is (re)initialized, 
				 * don't use weight to calculate new avg_dq_time
				 */
				if(pst->avg_dq_time == 0)
					pst->avg_dq_time = dq_time;
				else {
					/* 
					 * weight = PIE_DQ_THRESHOLD/2^6, but we scaled 
					 * weight by 2^8. Thus, scaled 
					 * weight = PIE_DQ_THRESHOLD /2^8 
					 * */
					w = PIE_DQ_THRESHOLD >> 8;
					pst->avg_dq_time = (dq_time* w
						+ (pst->avg_dq_time * ((1L << 8) - w))) >> 8;
					pst->sflags &= ~PIE_INMEASUREMENT;
				}
			}
		}

		/* 
		 * Start new measurment cycle when the queue has
		 *  PIE_DQ_THRESHOLD worth of bytes.
		 */
		if(!(pst->sflags & PIE_INMEASUREMENT) && 
			q->ni.len_bytes >= PIE_DQ_THRESHOLD) {
			pst->sflags |= PIE_INMEASUREMENT;
			pst->measurement_start = now;
			pst->dq_count = 0;
		}
	}
	/* Optionally, use packet timestamp to estimate queue delay */
	else
		pst->current_qdelay = now - pkt_ts;

	return m;	
}

/*
 * Enqueue a packet in q, subject to space and  PIE queue management policy
 * (whose parameters are in q->fs).
 * Update stats for the queue and the scheduler.
 * Return 0 on success, 1 on drop. The packet is consumed anyways.
 */
static int
aqm_pie_enqueue(struct dn_queue *q, struct mbuf* m)
{
	struct dn_fs *f;
	uint64_t len;
	uint32_t qlen;
	struct pie_status *pst;
	struct dn_aqm_pie_parms *pprms;
	int t;

	len = m->m_pkthdr.len;
	pst  = q->aqm_status;
	if(!pst) {
		DX(2, "PIE queue is not initialized\n");
		update_stats(q, 0, 1);
		FREE_PKT(m);
		return 1;
	}

	f = &(q->fs->fs);
	pprms = pst->parms;
	t = ENQUE;

	/* get current queue length in bytes or packets*/
	qlen = (f->flags & DN_QSIZE_BYTES) ?
		q->ni.len_bytes : q->ni.length;

	/* check for queue size and drop the tail if exceed queue limit*/
	if (qlen >= f->qsize)
		t = DROP;
	/* drop/mark the packet when PIE is active and burst time elapsed */
	else if ((pst->sflags & PIE_ACTIVE) && pst->burst_allowance==0
			&& drop_early(pst, q->ni.len_bytes) == DROP) {
				/* 
				 * if drop_prob over ECN threshold, drop the packet 
				 * otherwise mark and enqueue it.
				 */
				if ((pprms->flags & PIE_ECN_ENABLED) && pst->drop_prob <
					(pprms->max_ecnth << (PIE_PROB_BITS - PIE_FIX_POINT_BITS))
					&& ecn_mark(m))
					t = ENQUE;
				else
					t = DROP;
	}

	/* Turn PIE on when 1/3 of the queue is full */ 
	if (!(pst->sflags & PIE_ACTIVE) && qlen >= pst->one_third_q_size) {
		init_activate_pie(pst, 1);
	}

	/*  Reset burst tolerance and optinally turn PIE off*/
	if ((pst->sflags & PIE_ACTIVE) && pst->drop_prob == 0 &&
		pst->current_qdelay < (pprms->qdelay_ref >> 1) &&
		pst->qdelay_old < (pprms->qdelay_ref >> 1)) {

			pst->burst_allowance = pprms->max_burst;
			if ((pprms->flags & PIE_ON_OFF_MODE_ENABLED) && qlen<=0)
				deactivate_pie(pst);
	}

	/* Timestamp the packet if Departure Rate Estimation is disabled */
	if (t != DROP && !(pprms->flags & PIE_DEPRATEEST_ENABLED)) {
		/* Add TS to mbuf as a TAG */
		struct m_tag *mtag;
		mtag = m_tag_locate(m, MTAG_ABI_COMPAT, DN_AQM_MTAG_TS, NULL);
		if (mtag == NULL)
			mtag = m_tag_alloc(MTAG_ABI_COMPAT, DN_AQM_MTAG_TS,
				sizeof(aqm_time_t), M_NOWAIT);
		if (mtag == NULL) {
			m_freem(m); 
			t = DROP;
		}
		*(aqm_time_t *)(mtag + 1) = AQM_UNOW;
		m_tag_prepend(m, mtag);
	}

	if (t != DROP) {
		mq_append(&q->mq, m);
		update_stats(q, len, 0);
		return (0);
	} else {
		update_stats(q, 0, 1);

		/* reset accu_prob after packet drop */
		pst->accu_prob = 0;
		FREE_PKT(m);
		return 1;
	}
	return 0;
}

/* 
 * initialize PIE for queue 'q' 
 * First allocate memory for PIE status.
 */
static int
aqm_pie_init(struct dn_queue *q)
{
	struct pie_status *pst;
	struct dn_aqm_pie_parms *pprms;
	int err = 0;
	
	pprms = q->fs->aqmcfg;
	
	do { /* exit with break when error occurs*/
		if (!pprms){
			DX(2, "AQM_PIE is not configured");
			err = EINVAL;
			break;
		}

		q->aqm_status = malloc(sizeof(struct pie_status),
				 M_DUMMYNET, M_NOWAIT | M_ZERO);
		if (q->aqm_status == NULL) {
			D("cannot allocate PIE private data");
			err =  ENOMEM ; 
			break;
		}

		pst = q->aqm_status;
		/* increase reference count for PIE module */
		pie_desc.ref_count++;
		
		pst->pq = q;
		pst->parms = pprms;
		
		/* For speed optimization, we caculate 1/3 queue size once here */
		// we can use x/3 = (x >>2) + (x >>4) + (x >>7)
		pst->one_third_q_size = q->fs->fs.qsize/3;
		
		mtx_init(&pst->lock_mtx, "mtx_pie", NULL, MTX_DEF);
		callout_init_mtx(&pst->aqm_pie_callout, &pst->lock_mtx,
			CALLOUT_RETURNUNLOCKED);
		
		pst->current_qdelay = 0;
		init_activate_pie(pst, !(pprms->flags & PIE_ON_OFF_MODE_ENABLED));
		
		//DX(2, "aqm_PIE_init");

	} while(0);
	
	return err;
}

/* 
 * Callout function to destroy pie mtx and free PIE status memory
 */
static void
pie_callout_cleanup(void *x)
{
	struct pie_status *pst = (struct pie_status *) x;

	mtx_unlock(&pst->lock_mtx);
	mtx_destroy(&pst->lock_mtx);
	free(x, M_DUMMYNET);
	DN_BH_WLOCK();
	pie_desc.ref_count--;
	DN_BH_WUNLOCK();
}

/* 
 * Clean up PIE status for queue 'q' 
 * Destroy memory allocated for PIE status.
 */
static int
aqm_pie_cleanup(struct dn_queue *q)
{

	if(!q) {
		D("q is null");
		return 0;
	}
	struct pie_status *pst  = q->aqm_status;
	if(!pst) {
		//D("queue is already cleaned up");
		return 0;
	}
	if(!q->fs || !q->fs->aqmcfg) {
		D("fs is null or no cfg");
		return 1;
	}
	if (q->fs->aqmfp && q->fs->aqmfp->type !=DN_AQM_PIE) {
		D("Not PIE fs (%d)", q->fs->fs.fs_nr);
		return 1;
	}

	/* 
	 * Free PIE status allocated memory using pie_callout_cleanup() callout
	 * function to avoid any potential race.
	 * We reset aqm_pie_callout to call pie_callout_cleanup() in next 1um. This
	 * stops the scheduled calculate_drop_prob() callout and call pie_callout_cleanup() 
	 * which does memory freeing.
	 */
	mtx_lock(&pst->lock_mtx);
	callout_reset_sbt(&pst->aqm_pie_callout,
		SBT_1US, 0, pie_callout_cleanup, pst, 0);
	q->aqm_status = NULL;
	mtx_unlock(&pst->lock_mtx);

	return 0;
}

/* 
 * Config PIE parameters
 * also allocate memory for PIE configurations
 */
static int 
aqm_pie_config(struct dn_fsk* fs, struct dn_extra_parms *ep, int len)
{ 
	struct dn_aqm_pie_parms *pcfg;

	int l = sizeof(struct dn_extra_parms);
	if (len < l) {
		D("invalid sched parms length got %d need %d", len, l);
		return EINVAL;
	}
	/* we free the old cfg because maybe the orignal allocation 
	 * was used for diffirent AQM type.
	 */
	if (fs->aqmcfg) {
		free(fs->aqmcfg, M_DUMMYNET);
		fs->aqmcfg = NULL;
	}
	
	fs->aqmcfg = malloc(sizeof(struct dn_aqm_pie_parms),
			 M_DUMMYNET, M_NOWAIT | M_ZERO);
	if (fs->aqmcfg== NULL) {
		D("cannot allocate PIE configuration parameters");
		return ENOMEM; 
	}

	/* par array contains pie configuration as follow
	 * 0- qdelay_ref,1- tupdate, 2- max_burst
	 * 3- max_ecnth, 4- alpha, 5- beta, 6- flags
	 */

	/* configure PIE parameters */
	pcfg = fs->aqmcfg;
	
	if (ep->par[0] < 0)
		pcfg->qdelay_ref = pie_sysctl.qdelay_ref * AQM_TIME_1US;
	else
		pcfg->qdelay_ref = ep->par[0];
	if (ep->par[1] < 0)
		pcfg->tupdate = pie_sysctl.tupdate * AQM_TIME_1US;
	else
		pcfg->tupdate = ep->par[1];
	if (ep->par[2] < 0)
		pcfg->max_burst = pie_sysctl.max_burst * AQM_TIME_1US;
	else
		pcfg->max_burst = ep->par[2];
	if (ep->par[3] < 0)
		pcfg->max_ecnth = pie_sysctl.max_ecnth;
	else
		pcfg->max_ecnth = ep->par[3];
	if (ep->par[4] < 0)
		pcfg->alpha = pie_sysctl.alpha;
	else
		pcfg->alpha = ep->par[4];
	if (ep->par[5] < 0)
		pcfg->beta = pie_sysctl.beta;
	else
		pcfg->beta = ep->par[5];
	if (ep->par[6] < 0)
		pcfg->flags = pie_sysctl.flags;
	else
		pcfg->flags = ep->par[6];

	/* bound PIE configurations */
	pcfg->qdelay_ref = BOUND_VAR(pcfg->qdelay_ref, 1, 10 * AQM_TIME_1S);
	pcfg->tupdate = BOUND_VAR(pcfg->tupdate, 1, 10 * AQM_TIME_1S);
	pcfg->max_burst = BOUND_VAR(pcfg->max_burst, 0, 10 * AQM_TIME_1S);
	pcfg->max_ecnth = BOUND_VAR(pcfg->max_ecnth, 0, PIE_SCALE);
	pcfg->alpha = BOUND_VAR(pcfg->alpha, 0, 7 * PIE_SCALE);
	pcfg->beta = BOUND_VAR(pcfg->beta, 0 , 7 * PIE_SCALE);

	pie_desc.cfg_ref_count++;
	//D("pie cfg_ref_count=%d", pie_desc.cfg_ref_count);
	return 0;
}

/*
 * Deconfigure PIE and free memory allocation
 */
static int
aqm_pie_deconfig(struct dn_fsk* fs)
{
	if (fs && fs->aqmcfg) {
		free(fs->aqmcfg, M_DUMMYNET);
		fs->aqmcfg = NULL;
		pie_desc.cfg_ref_count--;
	}
	return 0;
}

/* 
 * Retrieve PIE configuration parameters.
 */ 
static int 
aqm_pie_getconfig (struct dn_fsk *fs, struct dn_extra_parms * ep)
{
	struct dn_aqm_pie_parms *pcfg;
	if (fs->aqmcfg) {
		strlcpy(ep->name, pie_desc.name, sizeof(ep->name));
		pcfg = fs->aqmcfg;
		ep->par[0] = pcfg->qdelay_ref / AQM_TIME_1US;
		ep->par[1] = pcfg->tupdate / AQM_TIME_1US;
		ep->par[2] = pcfg->max_burst / AQM_TIME_1US;
		ep->par[3] = pcfg->max_ecnth;
		ep->par[4] = pcfg->alpha;
		ep->par[5] = pcfg->beta;
		ep->par[6] = pcfg->flags;

		return 0;
	}
	return 1;
}

static struct dn_aqm pie_desc = {
	_SI( .type = )  DN_AQM_PIE,
	_SI( .name = )  "PIE",
	_SI( .ref_count = )  0,
	_SI( .cfg_ref_count = )  0,
	_SI( .enqueue = )  aqm_pie_enqueue,
	_SI( .dequeue = )  aqm_pie_dequeue,
	_SI( .config = )  aqm_pie_config,
	_SI( .deconfig = )  aqm_pie_deconfig,
	_SI( .getconfig = )  aqm_pie_getconfig,
	_SI( .init = )  aqm_pie_init,
	_SI( .cleanup = )  aqm_pie_cleanup,
};

DECLARE_DNAQM_MODULE(dn_aqm_pie, &pie_desc);
#endif
