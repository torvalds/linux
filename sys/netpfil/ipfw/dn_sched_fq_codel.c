/* 
 * FQ_Codel - The FlowQueue-Codel scheduler/AQM
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

#ifdef _KERNEL
#include <sys/malloc.h>
#include <sys/socket.h>
//#include <sys/socketvar.h>
#include <sys/kernel.h>
#include <sys/mbuf.h>
#include <sys/module.h>
#include <net/if.h>	/* IFNAMSIZ */
#include <netinet/in.h>
#include <netinet/ip_var.h>		/* ipfw_rule_ref */
#include <netinet/ip_fw.h>	/* flow_id */
#include <netinet/ip_dummynet.h>

#include <sys/lock.h>
#include <sys/proc.h>
#include <sys/rwlock.h>

#include <netpfil/ipfw/ip_fw_private.h>
#include <sys/sysctl.h>
#include <netinet/ip.h>
#include <netinet/ip6.h>
#include <netinet/ip_icmp.h>
#include <netinet/tcp.h>
#include <netinet/udp.h>
#include <sys/queue.h>
#include <sys/hash.h>

#include <netpfil/ipfw/dn_heap.h>
#include <netpfil/ipfw/ip_dn_private.h>

#include <netpfil/ipfw/dn_aqm.h>
#include <netpfil/ipfw/dn_aqm_codel.h>
#include <netpfil/ipfw/dn_sched.h>
#include <netpfil/ipfw/dn_sched_fq_codel.h>
#include <netpfil/ipfw/dn_sched_fq_codel_helper.h>

#else
#include <dn_test.h>
#endif

/* NOTE: In fq_codel module, we reimplements CoDel AQM functions 
 * because fq_codel use different flows (sub-queues) structure and 
 * dn_queue includes many variables not needed by a flow (sub-queue 
 * )i.e. avoid extra overhead (88 bytes vs 208 bytes).
 * Also, CoDel functions manages stats of sub-queues as well as the main queue.
 */

#define DN_SCHED_FQ_CODEL 6

static struct dn_alg fq_codel_desc;

/* fq_codel default parameters including codel */
struct dn_sch_fq_codel_parms 
fq_codel_sysctl = {{5000 * AQM_TIME_1US, 100000 * AQM_TIME_1US,
	CODEL_ECN_ENABLED}, 1024, 10240, 1514};

static int
fqcodel_sysctl_interval_handler(SYSCTL_HANDLER_ARGS)
{
	int error;
	long  value;

	value = fq_codel_sysctl.ccfg.interval;
	value /= AQM_TIME_1US;
	error = sysctl_handle_long(oidp, &value, 0, req);
	if (error != 0 || req->newptr == NULL)
		return (error);
	if (value < 1 || value > 100 * AQM_TIME_1S)
		return (EINVAL);
	fq_codel_sysctl.ccfg.interval = value * AQM_TIME_1US ;

	return (0);
}

static int
fqcodel_sysctl_target_handler(SYSCTL_HANDLER_ARGS)
{
	int error;
	long  value;

	value = fq_codel_sysctl.ccfg.target;
	value /= AQM_TIME_1US;
	error = sysctl_handle_long(oidp, &value, 0, req);
	if (error != 0 || req->newptr == NULL)
		return (error);
	if (value < 1 || value > 5 * AQM_TIME_1S)
		return (EINVAL);
	fq_codel_sysctl.ccfg.target = value * AQM_TIME_1US ;

	return (0);
}


SYSBEGIN(f4)

SYSCTL_DECL(_net_inet);
SYSCTL_DECL(_net_inet_ip);
SYSCTL_DECL(_net_inet_ip_dummynet);
static SYSCTL_NODE(_net_inet_ip_dummynet, OID_AUTO, fqcodel,
	CTLFLAG_RW, 0, "FQ_CODEL");

#ifdef SYSCTL_NODE
	
SYSCTL_PROC(_net_inet_ip_dummynet_fqcodel, OID_AUTO, target,
	CTLTYPE_LONG | CTLFLAG_RW, NULL, 0, fqcodel_sysctl_target_handler, "L",
	"FQ_CoDel target in microsecond");
SYSCTL_PROC(_net_inet_ip_dummynet_fqcodel, OID_AUTO, interval,
	CTLTYPE_LONG | CTLFLAG_RW, NULL, 0, fqcodel_sysctl_interval_handler, "L",
	"FQ_CoDel interval in microsecond");
	
SYSCTL_UINT(_net_inet_ip_dummynet_fqcodel, OID_AUTO, quantum,
	CTLFLAG_RW, &fq_codel_sysctl.quantum, 1514, "FQ_CoDel quantum");
SYSCTL_UINT(_net_inet_ip_dummynet_fqcodel, OID_AUTO, flows,
	CTLFLAG_RW, &fq_codel_sysctl.flows_cnt, 1024, 
	"Number of queues for FQ_CoDel");
SYSCTL_UINT(_net_inet_ip_dummynet_fqcodel, OID_AUTO, limit,
	CTLFLAG_RW, &fq_codel_sysctl.limit, 10240, "FQ_CoDel queues size limit");
#endif

/* Drop a packet form the head of codel queue */
static void
codel_drop_head(struct fq_codel_flow *q, struct fq_codel_si *si)
{
	struct mbuf *m = q->mq.head;

	if (m == NULL)
		return;
	q->mq.head = m->m_nextpkt;

	fq_update_stats(q, si, -m->m_pkthdr.len, 1);

	if (si->main_q.ni.length == 0) /* queue is now idle */
			si->main_q.q_time = dn_cfg.curr_time;

	FREE_PKT(m);
}

/* Enqueue a packet 'm' to a queue 'q' and add timestamp to that packet.
 * Return 1 when unable to add timestamp, otherwise return 0 
 */
static int
codel_enqueue(struct fq_codel_flow *q, struct mbuf *m, struct fq_codel_si *si)
{
	uint64_t len;

	len = m->m_pkthdr.len;
	/* finding maximum packet size */
	if (len > q->cst.maxpkt_size)
		q->cst.maxpkt_size = len;

	/* Add timestamp to mbuf as MTAG */
	struct m_tag *mtag;
	mtag = m_tag_locate(m, MTAG_ABI_COMPAT, DN_AQM_MTAG_TS, NULL);
	if (mtag == NULL)
		mtag = m_tag_alloc(MTAG_ABI_COMPAT, DN_AQM_MTAG_TS, sizeof(aqm_time_t),
			M_NOWAIT);
	if (mtag == NULL) {
		m_freem(m); 
		goto drop;
	}
	*(aqm_time_t *)(mtag + 1) = AQM_UNOW;
	m_tag_prepend(m, mtag);

	mq_append(&q->mq, m);
	fq_update_stats(q, si, len, 0);
	return 0;

drop:
	fq_update_stats(q, si, len, 1);
	m_freem(m);
	return 1;
}

/*
 * Classify a packet to queue number using Jenkins hash function.
 * Return: queue number 
 * the input of the hash are protocol no, perturbation, src IP, dst IP,
 * src port, dst port,
 */
static inline int
fq_codel_classify_flow(struct mbuf *m, uint16_t fcount, struct fq_codel_si *si)
{
	struct ip *ip;
	struct tcphdr *th;
	struct udphdr *uh;
	uint8_t tuple[41];
	uint16_t hash=0;

	ip = (struct ip *)mtodo(m, dn_tag_get(m)->iphdr_off);
//#ifdef INET6
	struct ip6_hdr *ip6;
	int isip6;
	isip6 = (ip->ip_v == 6);

	if(isip6) {
		ip6 = (struct ip6_hdr *)ip;
		*((uint8_t *) &tuple[0]) = ip6->ip6_nxt;
		*((uint32_t *) &tuple[1]) = si->perturbation;
		memcpy(&tuple[5], ip6->ip6_src.s6_addr, 16);
		memcpy(&tuple[21], ip6->ip6_dst.s6_addr, 16);

		switch (ip6->ip6_nxt) {
		case IPPROTO_TCP:
			th = (struct tcphdr *)(ip6 + 1);
			*((uint16_t *) &tuple[37]) = th->th_dport;
			*((uint16_t *) &tuple[39]) = th->th_sport;
			break;

		case IPPROTO_UDP:
			uh = (struct udphdr *)(ip6 + 1);
			*((uint16_t *) &tuple[37]) = uh->uh_dport;
			*((uint16_t *) &tuple[39]) = uh->uh_sport;
			break;
		default:
			memset(&tuple[37], 0, 4);

		}

		hash = jenkins_hash(tuple, 41, HASHINIT) %  fcount;
		return hash;
	} 
//#endif

	/* IPv4 */
	*((uint8_t *) &tuple[0]) = ip->ip_p;
	*((uint32_t *) &tuple[1]) = si->perturbation;
	*((uint32_t *) &tuple[5]) = ip->ip_src.s_addr;
	*((uint32_t *) &tuple[9]) = ip->ip_dst.s_addr;

	switch (ip->ip_p) {
		case IPPROTO_TCP:
			th = (struct tcphdr *)(ip + 1);
			*((uint16_t *) &tuple[13]) = th->th_dport;
			*((uint16_t *) &tuple[15]) = th->th_sport;
			break;

		case IPPROTO_UDP:
			uh = (struct udphdr *)(ip + 1);
			*((uint16_t *) &tuple[13]) = uh->uh_dport;
			*((uint16_t *) &tuple[15]) = uh->uh_sport;
			break;
		default:
			memset(&tuple[13], 0, 4);

	}
	hash = jenkins_hash(tuple, 17, HASHINIT) %  fcount;

	return hash;
}

/*
 * Enqueue a packet into an appropriate queue according to
 * FQ_CODEL algorithm.
 */
static int 
fq_codel_enqueue(struct dn_sch_inst *_si, struct dn_queue *_q, 
	struct mbuf *m)
{
	struct fq_codel_si *si;
	struct fq_codel_schk *schk;
	struct dn_sch_fq_codel_parms *param;
	struct dn_queue *mainq;
	int idx, drop, i, maxidx;

	mainq = (struct dn_queue *)(_si + 1);
	si = (struct fq_codel_si *)_si;
	schk = (struct fq_codel_schk *)(si->_si.sched+1);
	param = &schk->cfg;

	 /* classify a packet to queue number*/
	idx = fq_codel_classify_flow(m, param->flows_cnt, si);
	/* enqueue packet into appropriate queue using CoDel AQM.
	 * Note: 'codel_enqueue' function returns 1 only when it unable to 
	 * add timestamp to packet (no limit check)*/
	drop = codel_enqueue(&si->flows[idx], m, si);
	
	/* codel unable to timestamp a packet */ 
	if (drop)
		return 1;
	
	/* If the flow (sub-queue) is not active ,then add it to the tail of
	 * new flows list, initialize and activate it.
	 */
	if (!si->flows[idx].active ) {
		STAILQ_INSERT_TAIL(&si->newflows, &si->flows[idx], flowchain);
		si->flows[idx].deficit = param->quantum;
		si->flows[idx].cst.dropping = false;
		si->flows[idx].cst.first_above_time = 0;
		si->flows[idx].active = 1;
		//D("activate %d",idx);
	}

	/* check the limit for all queues and remove a packet from the
	 * largest one 
	 */
	if (mainq->ni.length > schk->cfg.limit) { D("over limit");
		/* find first active flow */
		for (maxidx = 0; maxidx < schk->cfg.flows_cnt; maxidx++)
			if (si->flows[maxidx].active)
				break;
		if (maxidx < schk->cfg.flows_cnt) {
			/* find the largest sub- queue */
			for (i = maxidx + 1; i < schk->cfg.flows_cnt; i++) 
				if (si->flows[i].active && si->flows[i].stats.length >
					si->flows[maxidx].stats.length)
					maxidx = i;
			codel_drop_head(&si->flows[maxidx], si);
			D("maxidx = %d",maxidx);
			drop = 1;
		}
	}

	return drop;
}

/*
 * Dequeue a packet from an appropriate queue according to
 * FQ_CODEL algorithm.
 */
static struct mbuf *
fq_codel_dequeue(struct dn_sch_inst *_si)
{
	struct fq_codel_si *si;
	struct fq_codel_schk *schk;
	struct dn_sch_fq_codel_parms *param;
	struct fq_codel_flow *f;
	struct mbuf *mbuf;
	struct fq_codel_list *fq_codel_flowlist;

	si = (struct fq_codel_si *)_si;
	schk = (struct fq_codel_schk *)(si->_si.sched+1);
	param = &schk->cfg;

	do {
		/* select a list to start with */
		if (STAILQ_EMPTY(&si->newflows))
			fq_codel_flowlist = &si->oldflows;
		else
			fq_codel_flowlist = &si->newflows;

		/* Both new and old queue lists are empty, return NULL */
		if (STAILQ_EMPTY(fq_codel_flowlist)) 
			return NULL;

		f = STAILQ_FIRST(fq_codel_flowlist);
		while (f != NULL)	{
			/* if there is no flow(sub-queue) deficit, increase deficit
			 * by quantum, move the flow to the tail of old flows list
			 * and try another flow.
			 * Otherwise, the flow will be used for dequeue.
			 */
			if (f->deficit < 0) {
				 f->deficit += param->quantum;
				 STAILQ_REMOVE_HEAD(fq_codel_flowlist, flowchain);
				 STAILQ_INSERT_TAIL(&si->oldflows, f, flowchain);
			 } else 
				 break;

			f = STAILQ_FIRST(fq_codel_flowlist);
		}
		
		/* the new flows list is empty, try old flows list */
		if (STAILQ_EMPTY(fq_codel_flowlist)) 
			continue;

		/* Dequeue a packet from the selected flow */
		mbuf = fqc_codel_dequeue(f, si);

		/* Codel did not return a packet */
		if (!mbuf) {
			/* If the selected flow belongs to new flows list, then move 
			 * it to the tail of old flows list. Otherwise, deactivate it and
			 * remove it from the old list and
			 */
			if (fq_codel_flowlist == &si->newflows) {
				STAILQ_REMOVE_HEAD(fq_codel_flowlist, flowchain);
				STAILQ_INSERT_TAIL(&si->oldflows, f, flowchain);
			}	else {
				f->active = 0;
				STAILQ_REMOVE_HEAD(fq_codel_flowlist, flowchain);
			}
			/* start again */
			continue;
		}

		/* we have a packet to return, 
		 * update flow deficit and return the packet*/
		f->deficit -= mbuf->m_pkthdr.len;
		return mbuf;

	} while (1);
	
	/* unreachable point */
	return NULL;
}

/*
 * Initialize fq_codel scheduler instance.
 * also, allocate memory for flows array.
 */
static int
fq_codel_new_sched(struct dn_sch_inst *_si)
{
	struct fq_codel_si *si;
	struct dn_queue *q;
	struct fq_codel_schk *schk;
	int i;

	si = (struct fq_codel_si *)_si;
	schk = (struct fq_codel_schk *)(_si->sched+1);

	if(si->flows) {
		D("si already configured!");
		return 0;
	}

	/* init the main queue */
	q = &si->main_q;
	set_oid(&q->ni.oid, DN_QUEUE, sizeof(*q));
	q->_si = _si;
	q->fs = _si->sched->fs;

	/* allocate memory for flows array */
	si->flows = mallocarray(schk->cfg.flows_cnt,
	    sizeof(struct fq_codel_flow), M_DUMMYNET, M_NOWAIT | M_ZERO);
	if (si->flows == NULL) {
		D("cannot allocate memory for fq_codel configuration parameters");
		return ENOMEM ; 
	}

	/* init perturbation for this si */
	si->perturbation = random();

	/* init the old and new flows lists */
	STAILQ_INIT(&si->newflows);
	STAILQ_INIT(&si->oldflows);

	/* init the flows (sub-queues) */
	for (i = 0; i < schk->cfg.flows_cnt; i++) {
		/* init codel */
		si->flows[i].cst.maxpkt_size = 500;
	}

	fq_codel_desc.ref_count++;
	return 0;
}

/*
 * Free fq_codel scheduler instance.
 */
static int
fq_codel_free_sched(struct dn_sch_inst *_si)
{
	struct fq_codel_si *si = (struct fq_codel_si *)_si ;

	/* free the flows array */
	free(si->flows , M_DUMMYNET);
	si->flows = NULL;
	fq_codel_desc.ref_count--;

	return 0;
}

/*
 * Configure fq_codel scheduler.
 * the configurations for the scheduler is passed from userland.
 */
static int
fq_codel_config(struct dn_schk *_schk)
{
	struct fq_codel_schk *schk;
	struct dn_extra_parms *ep;
	struct dn_sch_fq_codel_parms *fqc_cfg;
	
	schk = (struct fq_codel_schk *)(_schk+1);
	ep = (struct dn_extra_parms *) _schk->cfg;

	/* par array contains fq_codel configuration as follow
	 * Codel: 0- target,1- interval, 2- flags
	 * FQ_CODEL: 3- quantum, 4- limit, 5- flows
	 */
	if (ep && ep->oid.len ==sizeof(*ep) &&
		ep->oid.subtype == DN_SCH_PARAMS) {

		fqc_cfg = &schk->cfg;
		if (ep->par[0] < 0)
			fqc_cfg->ccfg.target = fq_codel_sysctl.ccfg.target;
		else
			fqc_cfg->ccfg.target = ep->par[0] * AQM_TIME_1US;

		if (ep->par[1] < 0)
			fqc_cfg->ccfg.interval = fq_codel_sysctl.ccfg.interval;
		else
			fqc_cfg->ccfg.interval = ep->par[1] * AQM_TIME_1US;

		if (ep->par[2] < 0)
			fqc_cfg->ccfg.flags = 0;
		else
			fqc_cfg->ccfg.flags = ep->par[2];

		/* FQ configurations */
		if (ep->par[3] < 0)
			fqc_cfg->quantum = fq_codel_sysctl.quantum;
		else
			fqc_cfg->quantum = ep->par[3];

		if (ep->par[4] < 0)
			fqc_cfg->limit = fq_codel_sysctl.limit;
		else
			fqc_cfg->limit = ep->par[4];

		if (ep->par[5] < 0)
			fqc_cfg->flows_cnt = fq_codel_sysctl.flows_cnt;
		else
			fqc_cfg->flows_cnt = ep->par[5];

		/* Bound the configurations */
		fqc_cfg->ccfg.target = BOUND_VAR(fqc_cfg->ccfg.target, 1 , 
			5 * AQM_TIME_1S); ;
		fqc_cfg->ccfg.interval = BOUND_VAR(fqc_cfg->ccfg.interval, 1,
			100 * AQM_TIME_1S);

		fqc_cfg->quantum = BOUND_VAR(fqc_cfg->quantum,1, 9000);
		fqc_cfg->limit= BOUND_VAR(fqc_cfg->limit,1,20480);
		fqc_cfg->flows_cnt= BOUND_VAR(fqc_cfg->flows_cnt,1,65536);
	}
	else
		return 1;

	return 0;
}

/*
 * Return fq_codel scheduler configurations
 * the configurations for the scheduler is passed to userland.
 */
static int 
fq_codel_getconfig (struct dn_schk *_schk, struct dn_extra_parms *ep) {
	
	struct fq_codel_schk *schk = (struct fq_codel_schk *)(_schk+1);
	struct dn_sch_fq_codel_parms *fqc_cfg;

	fqc_cfg = &schk->cfg;

	strcpy(ep->name, fq_codel_desc.name);
	ep->par[0] = fqc_cfg->ccfg.target / AQM_TIME_1US;
	ep->par[1] = fqc_cfg->ccfg.interval / AQM_TIME_1US;
	ep->par[2] = fqc_cfg->ccfg.flags;

	ep->par[3] = fqc_cfg->quantum;
	ep->par[4] = fqc_cfg->limit;
	ep->par[5] = fqc_cfg->flows_cnt;

	return 0;
}

/*
 * fq_codel scheduler descriptor
 * contains the type of the scheduler, the name, the size of extra
 * data structures, and function pointers.
 */
static struct dn_alg fq_codel_desc = {
	_SI( .type = )  DN_SCHED_FQ_CODEL,
	_SI( .name = ) "FQ_CODEL",
	_SI( .flags = ) 0,

	_SI( .schk_datalen = ) sizeof(struct fq_codel_schk),
	_SI( .si_datalen = ) sizeof(struct fq_codel_si) - sizeof(struct dn_sch_inst),
	_SI( .q_datalen = ) 0,

	_SI( .enqueue = ) fq_codel_enqueue,
	_SI( .dequeue = ) fq_codel_dequeue,
	_SI( .config = ) fq_codel_config, /* new sched i.e. sched X config ...*/
	_SI( .destroy = ) NULL,  /*sched x delete */
	_SI( .new_sched = ) fq_codel_new_sched, /* new schd instance */
	_SI( .free_sched = ) fq_codel_free_sched,	/* delete schd instance */
	_SI( .new_fsk = ) NULL,
	_SI( .free_fsk = ) NULL,
	_SI( .new_queue = ) NULL,
	_SI( .free_queue = ) NULL,
	_SI( .getconfig = )  fq_codel_getconfig,
	_SI( .ref_count = ) 0
};

DECLARE_DNSCHED_MODULE(dn_fq_codel, &fq_codel_desc);
