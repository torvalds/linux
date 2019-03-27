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
#include <netpfil/ipfw/dn_aqm_codel.h>
#include <netpfil/ipfw/dn_sched.h>

#define DN_AQM_CODEL 1

static struct dn_aqm codel_desc;

/* default codel parameters */
struct dn_aqm_codel_parms codel_sysctl = {5000 * AQM_TIME_1US,
	100000 * AQM_TIME_1US, 0};

static int
codel_sysctl_interval_handler(SYSCTL_HANDLER_ARGS)
{
	int error;
	long  value;

	value = codel_sysctl.interval;
	value /= AQM_TIME_1US;
	error = sysctl_handle_long(oidp, &value, 0, req);
	if (error != 0 || req->newptr == NULL)
		return (error);
	if (value < 1 || value > 100 * AQM_TIME_1S)
		return (EINVAL);
	codel_sysctl.interval = value * AQM_TIME_1US ;
	return (0);
}

static int
codel_sysctl_target_handler(SYSCTL_HANDLER_ARGS)
{
	int error;
	long  value;

	value = codel_sysctl.target;
	value /= AQM_TIME_1US;
	error = sysctl_handle_long(oidp, &value, 0, req);
	if (error != 0 || req->newptr == NULL)
		return (error);
	D("%ld", value);
	if (value < 1 || value > 5 * AQM_TIME_1S)
		return (EINVAL);
	codel_sysctl.target = value * AQM_TIME_1US ;
	return (0);
}

/* defining Codel sysctl variables */
SYSBEGIN(f4)

SYSCTL_DECL(_net_inet);
SYSCTL_DECL(_net_inet_ip);
SYSCTL_DECL(_net_inet_ip_dummynet);
static SYSCTL_NODE(_net_inet_ip_dummynet, OID_AUTO, 
	codel, CTLFLAG_RW, 0, "CODEL");

#ifdef SYSCTL_NODE
SYSCTL_PROC(_net_inet_ip_dummynet_codel, OID_AUTO, target,
	CTLTYPE_LONG | CTLFLAG_RW, NULL, 0,codel_sysctl_target_handler, "L",
	"CoDel target in microsecond");

SYSCTL_PROC(_net_inet_ip_dummynet_codel, OID_AUTO, interval,
	CTLTYPE_LONG | CTLFLAG_RW, NULL, 0, codel_sysctl_interval_handler, "L",
	"CoDel interval in microsecond");
#endif

/* This function computes codel_interval/sqrt(count) 
 *  Newton's method of approximation is used to compute 1/sqrt(count).
 * http://betterexplained.com/articles/
 * 	understanding-quakes-fast-inverse-square-root/ 
 */
aqm_time_t 
control_law(struct codel_status *cst, struct dn_aqm_codel_parms *cprms,
	aqm_time_t t)
{
	uint32_t count;
	uint64_t temp;
	count = cst->count;

	/* we don't calculate isqrt(1) to get more accurate result*/
	if (count == 1) {
		/* prepare isqrt (old guess) for the next iteration i.e. 1/sqrt(2)*/
		cst->isqrt = (1UL<< FIX_POINT_BITS) * 7/10;
		/* return time + isqrt(1)*interval */
		return t + cprms->interval;
	}

	/* newguess = g(1.5 - 0.5*c*g^2)
	 * Multiplying both sides by 2 to make all the constants intergers
	 * newguess * 2  = g(3 - c*g^2) g=old guess, c=count
	 * So, newguess = newguess /2
	 * Fixed point operations are used here.  
	 */

	/* Calculate g^2 */
	temp = (uint32_t) cst->isqrt * cst->isqrt;
	/* Calculate (3 - c*g^2) i.e. (3 - c * temp) */
	temp = (3ULL<< (FIX_POINT_BITS*2)) - (count * temp);

	/* 
	 * Divide by 2 because we multiplied the original equation by two 
	 * Also, we shift the result by 8 bits to prevent overflow. 
	 * */
	temp >>= (1 + 8); 

	/*  Now, temp = (1.5 - 0.5*c*g^2)
	 * Calculate g (1.5 - 0.5*c*g^2) i.e. g * temp 
	 */
	temp = (cst->isqrt * temp) >> (FIX_POINT_BITS + FIX_POINT_BITS - 8);
	cst->isqrt = temp;

	 /* calculate codel_interval/sqrt(count) */
	 return t + ((cprms->interval * temp) >> FIX_POINT_BITS);
}

/*
 * Extract a packet from the head of queue 'q'
 * Return a packet or NULL if the queue is empty.
 * Also extract packet's timestamp from mtag.
 */
struct mbuf *
codel_extract_head(struct dn_queue *q, aqm_time_t *pkt_ts)
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

	/* extract packet TS*/
	mtag = m_tag_locate(m, MTAG_ABI_COMPAT, DN_AQM_MTAG_TS, NULL);
	if (mtag == NULL) {
		D("Codel timestamp mtag not found!");
		*pkt_ts = 0;
	} else {
		*pkt_ts = *(aqm_time_t *)(mtag + 1);
		m_tag_delete(m,mtag); 
	}

	return m;
}

/*
 * Enqueue a packet 'm' in queue 'q'
 */
static int
aqm_codel_enqueue(struct dn_queue *q, struct mbuf *m)
{
	struct dn_fs *f;
	uint64_t len;
	struct codel_status *cst;	/*codel status variables */
	struct m_tag *mtag;

	f = &(q->fs->fs);
	len = m->m_pkthdr.len;
	cst = q->aqm_status;
	if(!cst) {
		D("Codel queue is not initialized\n");
		goto drop;
	}

	/* Finding maximum packet size */
	// XXX we can get MTU from driver instead 
	if (len > cst->maxpkt_size)
		cst->maxpkt_size = len;

	/* check for queue size and drop the tail if exceed queue limit*/
	if (f->flags & DN_QSIZE_BYTES) {
		if ( q->ni.len_bytes > f->qsize)
			goto drop;
	}
	else {
		if ( q->ni.length >= f->qsize)
			goto drop;
	}

	/* Add timestamp as mtag */
	mtag = m_tag_locate(m, MTAG_ABI_COMPAT, DN_AQM_MTAG_TS, NULL);
	if (mtag == NULL)
		mtag = m_tag_alloc(MTAG_ABI_COMPAT, DN_AQM_MTAG_TS,
			sizeof(aqm_time_t), M_NOWAIT);
	if (mtag == NULL) {
		m_freem(m); 
		goto drop;
	}

	*(aqm_time_t *)(mtag + 1) = AQM_UNOW;
	m_tag_prepend(m, mtag);

	mq_append(&q->mq, m);
	update_stats(q, len, 0);
	return (0);

drop:
	update_stats(q, 0, 1);
	FREE_PKT(m);
	return (1);
}


/* Dequeue a pcaket from queue q */
static struct mbuf * 
aqm_codel_dequeue(struct dn_queue *q)
{
	return codel_dequeue(q);
}

/* 
 * initialize Codel for queue 'q' 
 * First allocate memory for codel status.
 */
static int 
aqm_codel_init(struct dn_queue *q)
{
	struct codel_status *cst;

	if (!q->fs->aqmcfg) {
		D("Codel is not configure!d");
		return EINVAL;
	}

	q->aqm_status = malloc(sizeof(struct codel_status),
			 M_DUMMYNET, M_NOWAIT | M_ZERO);
	if (q->aqm_status == NULL) {
		D("Cannot allocate AQM_codel private data");
		return ENOMEM ; 
	}

	/* init codel status variables */
	cst = q->aqm_status;
	cst->dropping=0;
	cst->first_above_time=0;
	cst->drop_next_time=0;
	cst->count=0;
	cst->maxpkt_size = 500;

	/* increase reference counters */
	codel_desc.ref_count++;

	return 0;
}

/* 
 * Clean up Codel status for queue 'q' 
 * Destroy memory allocated for codel status.
 */
static int
aqm_codel_cleanup(struct dn_queue *q)
{

	if (q && q->aqm_status) {
		free(q->aqm_status, M_DUMMYNET);
		q->aqm_status = NULL;
		/* decrease reference counters */
		codel_desc.ref_count--;
	}
	else
		D("Codel already cleaned up");
	return 0;
}

/* 
 * Config codel parameters
 * also allocate memory for codel configurations
 */
static int
aqm_codel_config(struct dn_fsk* fs, struct dn_extra_parms *ep, int len)
{
	struct dn_aqm_codel_parms *ccfg;

	int l = sizeof(struct dn_extra_parms);
	if (len < l) {
		D("invalid sched parms length got %d need %d", len, l);
		return EINVAL;
	}
	/* we free the old cfg because maybe the original allocation 
	 * not the same size as the new one (different AQM type).
	 */
	if (fs->aqmcfg) {
		free(fs->aqmcfg, M_DUMMYNET);
		fs->aqmcfg = NULL;
	}

	fs->aqmcfg = malloc(sizeof(struct dn_aqm_codel_parms),
			 M_DUMMYNET, M_NOWAIT | M_ZERO);
	if (fs->aqmcfg== NULL) {
		D("cannot allocate AQM_codel configuration parameters");
		return ENOMEM; 
	}
	
	/* configure codel parameters */
	ccfg = fs->aqmcfg;
	
	if (ep->par[0] < 0)
		ccfg->target = codel_sysctl.target;
	else
		ccfg->target = ep->par[0] * AQM_TIME_1US;

	if (ep->par[1] < 0)
		ccfg->interval = codel_sysctl.interval;
	else
		ccfg->interval = ep->par[1] * AQM_TIME_1US;

	if (ep->par[2] < 0)
		ccfg->flags = 0;
	else
		ccfg->flags = ep->par[2];

	/* bound codel configurations */
	ccfg->target = BOUND_VAR(ccfg->target,1, 5 * AQM_TIME_1S);
	ccfg->interval = BOUND_VAR(ccfg->interval,1, 5 * AQM_TIME_1S);
	/* increase config reference counter */
	codel_desc.cfg_ref_count++;

	return 0;
}

/*
 * Deconfigure Codel and free memory allocation
 */
static int
aqm_codel_deconfig(struct dn_fsk* fs)
{

	if (fs && fs->aqmcfg) {
		free(fs->aqmcfg, M_DUMMYNET);
		fs->aqmcfg = NULL;
		fs->aqmfp = NULL;
		/* decrease config reference counter */
		codel_desc.cfg_ref_count--;
	}

	return 0;
}

/* 
 * Retrieve Codel configuration parameters.
 */ 
static int
aqm_codel_getconfig(struct dn_fsk *fs, struct dn_extra_parms * ep)
{
	struct dn_aqm_codel_parms *ccfg;

	if (fs->aqmcfg) {
		strlcpy(ep->name, codel_desc.name, sizeof(ep->name));
		ccfg = fs->aqmcfg;
		ep->par[0] = ccfg->target / AQM_TIME_1US;
		ep->par[1] = ccfg->interval / AQM_TIME_1US;
		ep->par[2] = ccfg->flags;
		return 0;
	}
	return 1;
}

static struct dn_aqm codel_desc = {
	_SI( .type = )  DN_AQM_CODEL,
	_SI( .name = )  "CODEL",
	_SI( .enqueue = )  aqm_codel_enqueue,
	_SI( .dequeue = )  aqm_codel_dequeue,
	_SI( .config = )  aqm_codel_config,
	_SI( .getconfig = )  aqm_codel_getconfig,
	_SI( .deconfig = )  aqm_codel_deconfig,
	_SI( .init = )  aqm_codel_init,
	_SI( .cleanup = )  aqm_codel_cleanup,
};

DECLARE_DNAQM_MODULE(dn_aqm_codel, &codel_desc);


#endif
