/*-
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD
 *
 * Codel/FQ_Codel and PIE/FQ-PIE Code:
 * Copyright (C) 2016 Centre for Advanced Internet Architectures,
 *  Swinburne University of Technology, Melbourne, Australia.
 * Portions of this code were made possible in part by a gift from 
 *  The Comcast Innovation Fund.
 * Implemented by Rasool Al-Saadi <ralsaadi@swin.edu.au>
 * 
 * Copyright (c) 1998-2002,2010 Luigi Rizzo, Universita` di Pisa
 * Portions Copyright (c) 2000 Akamba Corp.
 * All rights reserved
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
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE AUTHOR OR CONTRIBUTORS BE LIABLE
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

/*
 * Configuration and internal object management for dummynet.
 */

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
#include <sys/socketvar.h>
#include <sys/time.h>
#include <sys/taskqueue.h>
#include <net/if.h>	/* IFNAMSIZ, struct ifaddr, ifq head, lock.h mutex.h */
#include <netinet/in.h>
#include <netinet/ip_var.h>	/* ip_output(), IP_FORWARDING */
#include <netinet/ip_fw.h>
#include <netinet/ip_dummynet.h>

#include <netpfil/ipfw/ip_fw_private.h>
#include <netpfil/ipfw/dn_heap.h>
#include <netpfil/ipfw/ip_dn_private.h>
#ifdef NEW_AQM
#include <netpfil/ipfw/dn_aqm.h>
#endif
#include <netpfil/ipfw/dn_sched.h>

/* which objects to copy */
#define DN_C_LINK 	0x01
#define DN_C_SCH	0x02
#define DN_C_FLOW	0x04
#define DN_C_FS		0x08
#define DN_C_QUEUE	0x10

/* we use this argument in case of a schk_new */
struct schk_new_arg {
	struct dn_alg *fp;
	struct dn_sch *sch;
};

/*---- callout hooks. ----*/
static struct callout dn_timeout;
static int dn_gone;
static struct task	dn_task;
static struct taskqueue	*dn_tq = NULL;

static void
dummynet(void *arg)
{

	(void)arg;	/* UNUSED */
	taskqueue_enqueue(dn_tq, &dn_task);
}

void
dn_reschedule(void)
{

	if (dn_gone != 0)
		return;
	callout_reset_sbt(&dn_timeout, tick_sbt, 0, dummynet, NULL,
	    C_HARDCLOCK | C_DIRECT_EXEC);
}
/*----- end of callout hooks -----*/

#ifdef NEW_AQM
/* Return AQM descriptor for given type or name. */
static struct dn_aqm *
find_aqm_type(int type, char *name)
{
	struct dn_aqm *d;

	SLIST_FOREACH(d, &dn_cfg.aqmlist, next) {
		if (d->type == type || (name && !strcasecmp(d->name, name)))
			return d;
	}
	return NULL; /* not found */
}
#endif

/* Return a scheduler descriptor given the type or name. */
static struct dn_alg *
find_sched_type(int type, char *name)
{
	struct dn_alg *d;

	SLIST_FOREACH(d, &dn_cfg.schedlist, next) {
		if (d->type == type || (name && !strcasecmp(d->name, name)))
			return d;
	}
	return NULL; /* not found */
}

int
ipdn_bound_var(int *v, int dflt, int lo, int hi, const char *msg)
{
	int oldv = *v;
	const char *op = NULL;
	if (dflt < lo)
		dflt = lo;
	if (dflt > hi)
		dflt = hi;
	if (oldv < lo) {
		*v = dflt;
		op = "Bump";
	} else if (oldv > hi) {
		*v = hi;
		op = "Clamp";
	} else
		return *v;
	if (op && msg)
		printf("%s %s to %d (was %d)\n", op, msg, *v, oldv);
	return *v;
}

/*---- flow_id mask, hash and compare functions ---*/
/*
 * The flow_id includes the 5-tuple, the queue/pipe number
 * which we store in the extra area in host order,
 * and for ipv6 also the flow_id6.
 * XXX see if we want the tos byte (can store in 'flags')
 */
static struct ipfw_flow_id *
flow_id_mask(struct ipfw_flow_id *mask, struct ipfw_flow_id *id)
{
	int is_v6 = IS_IP6_FLOW_ID(id);

	id->dst_port &= mask->dst_port;
	id->src_port &= mask->src_port;
	id->proto &= mask->proto;
	id->extra &= mask->extra;
	if (is_v6) {
		APPLY_MASK(&id->dst_ip6, &mask->dst_ip6);
		APPLY_MASK(&id->src_ip6, &mask->src_ip6);
		id->flow_id6 &= mask->flow_id6;
	} else {
		id->dst_ip &= mask->dst_ip;
		id->src_ip &= mask->src_ip;
	}
	return id;
}

/* computes an OR of two masks, result in dst and also returned */
static struct ipfw_flow_id *
flow_id_or(struct ipfw_flow_id *src, struct ipfw_flow_id *dst)
{
	int is_v6 = IS_IP6_FLOW_ID(dst);

	dst->dst_port |= src->dst_port;
	dst->src_port |= src->src_port;
	dst->proto |= src->proto;
	dst->extra |= src->extra;
	if (is_v6) {
#define OR_MASK(_d, _s)                          \
    (_d)->__u6_addr.__u6_addr32[0] |= (_s)->__u6_addr.__u6_addr32[0]; \
    (_d)->__u6_addr.__u6_addr32[1] |= (_s)->__u6_addr.__u6_addr32[1]; \
    (_d)->__u6_addr.__u6_addr32[2] |= (_s)->__u6_addr.__u6_addr32[2]; \
    (_d)->__u6_addr.__u6_addr32[3] |= (_s)->__u6_addr.__u6_addr32[3];
		OR_MASK(&dst->dst_ip6, &src->dst_ip6);
		OR_MASK(&dst->src_ip6, &src->src_ip6);
#undef OR_MASK
		dst->flow_id6 |= src->flow_id6;
	} else {
		dst->dst_ip |= src->dst_ip;
		dst->src_ip |= src->src_ip;
	}
	return dst;
}

static int
nonzero_mask(struct ipfw_flow_id *m)
{
	if (m->dst_port || m->src_port || m->proto || m->extra)
		return 1;
	if (IS_IP6_FLOW_ID(m)) {
		return
			m->dst_ip6.__u6_addr.__u6_addr32[0] ||
			m->dst_ip6.__u6_addr.__u6_addr32[1] ||
			m->dst_ip6.__u6_addr.__u6_addr32[2] ||
			m->dst_ip6.__u6_addr.__u6_addr32[3] ||
			m->src_ip6.__u6_addr.__u6_addr32[0] ||
			m->src_ip6.__u6_addr.__u6_addr32[1] ||
			m->src_ip6.__u6_addr.__u6_addr32[2] ||
			m->src_ip6.__u6_addr.__u6_addr32[3] ||
			m->flow_id6;
	} else {
		return m->dst_ip || m->src_ip;
	}
}

/* XXX we may want a better hash function */
static uint32_t
flow_id_hash(struct ipfw_flow_id *id)
{
    uint32_t i;

    if (IS_IP6_FLOW_ID(id)) {
	uint32_t *d = (uint32_t *)&id->dst_ip6;
	uint32_t *s = (uint32_t *)&id->src_ip6;
        i = (d[0]      ) ^ (d[1])       ^
            (d[2]      ) ^ (d[3])       ^
            (d[0] >> 15) ^ (d[1] >> 15) ^
            (d[2] >> 15) ^ (d[3] >> 15) ^
            (s[0] <<  1) ^ (s[1] <<  1) ^
            (s[2] <<  1) ^ (s[3] <<  1) ^
            (s[0] << 16) ^ (s[1] << 16) ^
            (s[2] << 16) ^ (s[3] << 16) ^
            (id->dst_port << 1) ^ (id->src_port) ^
	    (id->extra) ^
            (id->proto ) ^ (id->flow_id6);
    } else {
        i = (id->dst_ip)        ^ (id->dst_ip >> 15) ^
            (id->src_ip << 1)   ^ (id->src_ip >> 16) ^
	    (id->extra) ^
            (id->dst_port << 1) ^ (id->src_port)     ^ (id->proto);
    }
    return i;
}

/* Like bcmp, returns 0 if ids match, 1 otherwise. */
static int
flow_id_cmp(struct ipfw_flow_id *id1, struct ipfw_flow_id *id2)
{
	int is_v6 = IS_IP6_FLOW_ID(id1);

	if (!is_v6) {
	    if (IS_IP6_FLOW_ID(id2))
		return 1; /* different address families */

	    return (id1->dst_ip == id2->dst_ip &&
		    id1->src_ip == id2->src_ip &&
		    id1->dst_port == id2->dst_port &&
		    id1->src_port == id2->src_port &&
		    id1->proto == id2->proto &&
		    id1->extra == id2->extra) ? 0 : 1;
	}
	/* the ipv6 case */
	return (
	    !bcmp(&id1->dst_ip6,&id2->dst_ip6, sizeof(id1->dst_ip6)) &&
	    !bcmp(&id1->src_ip6,&id2->src_ip6, sizeof(id1->src_ip6)) &&
	    id1->dst_port == id2->dst_port &&
	    id1->src_port == id2->src_port &&
	    id1->proto == id2->proto &&
	    id1->extra == id2->extra &&
	    id1->flow_id6 == id2->flow_id6) ? 0 : 1;
}
/*--------- end of flow-id mask, hash and compare ---------*/

/*--- support functions for the qht hashtable ----
 * Entries are hashed by flow-id
 */
static uint32_t
q_hash(uintptr_t key, int flags, void *arg)
{
	/* compute the hash slot from the flow id */
	struct ipfw_flow_id *id = (flags & DNHT_KEY_IS_OBJ) ?
		&((struct dn_queue *)key)->ni.fid :
		(struct ipfw_flow_id *)key;

	return flow_id_hash(id);
}

static int
q_match(void *obj, uintptr_t key, int flags, void *arg)
{
	struct dn_queue *o = (struct dn_queue *)obj;
	struct ipfw_flow_id *id2;

	if (flags & DNHT_KEY_IS_OBJ) {
		/* compare pointers */
		id2 = &((struct dn_queue *)key)->ni.fid;
	} else {
		id2 = (struct ipfw_flow_id *)key;
	}
	return (0 == flow_id_cmp(&o->ni.fid,  id2));
}

/*
 * create a new queue instance for the given 'key'.
 */
static void *
q_new(uintptr_t key, int flags, void *arg)
{   
	struct dn_queue *q, *template = arg;
	struct dn_fsk *fs = template->fs;
	int size = sizeof(*q) + fs->sched->fp->q_datalen;

	q = malloc(size, M_DUMMYNET, M_NOWAIT | M_ZERO);
	if (q == NULL) {
		D("no memory for new queue");
		return NULL;
	}

	set_oid(&q->ni.oid, DN_QUEUE, size);
	if (fs->fs.flags & DN_QHT_HASH)
		q->ni.fid = *(struct ipfw_flow_id *)key;
	q->fs = fs;
	q->_si = template->_si;
	q->_si->q_count++;

	if (fs->sched->fp->new_queue)
		fs->sched->fp->new_queue(q);

#ifdef NEW_AQM
	/* call AQM init function after creating a queue*/
	if (fs->aqmfp && fs->aqmfp->init)
		if(fs->aqmfp->init(q))
			D("unable to init AQM for fs %d", fs->fs.fs_nr);
#endif
	dn_cfg.queue_count++;

	return q;
}

/*
 * Notify schedulers that a queue is going away.
 * If (flags & DN_DESTROY), also free the packets.
 * The version for callbacks is called q_delete_cb().
 */
static void
dn_delete_queue(struct dn_queue *q, int flags)
{
	struct dn_fsk *fs = q->fs;

#ifdef NEW_AQM
	/* clean up AQM status for queue 'q'
	 * cleanup here is called just with MULTIQUEUE
	 */
	if (fs && fs->aqmfp && fs->aqmfp->cleanup)
		fs->aqmfp->cleanup(q);
#endif
	// D("fs %p si %p\n", fs, q->_si);
	/* notify the parent scheduler that the queue is going away */
	if (fs && fs->sched->fp->free_queue)
		fs->sched->fp->free_queue(q);
	q->_si->q_count--;
	q->_si = NULL;
	if (flags & DN_DESTROY) {
		if (q->mq.head)
			dn_free_pkts(q->mq.head);
		bzero(q, sizeof(*q));	// safety
		free(q, M_DUMMYNET);
		dn_cfg.queue_count--;
	}
}

static int
q_delete_cb(void *q, void *arg)
{
	int flags = (int)(uintptr_t)arg;
	dn_delete_queue(q, flags);
	return (flags & DN_DESTROY) ? DNHT_SCAN_DEL : 0;
}

/*
 * calls dn_delete_queue/q_delete_cb on all queues,
 * which notifies the parent scheduler and possibly drains packets.
 * flags & DN_DESTROY: drains queues and destroy qht;
 */
static void
qht_delete(struct dn_fsk *fs, int flags)
{
	ND("fs %d start flags %d qht %p",
		fs->fs.fs_nr, flags, fs->qht);
	if (!fs->qht)
		return;
	if (fs->fs.flags & DN_QHT_HASH) {
		dn_ht_scan(fs->qht, q_delete_cb, (void *)(uintptr_t)flags);
		if (flags & DN_DESTROY) {
			dn_ht_free(fs->qht, 0);
			fs->qht = NULL;
		}
	} else {
		dn_delete_queue((struct dn_queue *)(fs->qht), flags);
		if (flags & DN_DESTROY)
			fs->qht = NULL;
	}
}

/*
 * Find and possibly create the queue for a MULTIQUEUE scheduler.
 * We never call it for !MULTIQUEUE (the queue is in the sch_inst).
 */
struct dn_queue *
ipdn_q_find(struct dn_fsk *fs, struct dn_sch_inst *si,
	struct ipfw_flow_id *id)
{
	struct dn_queue template;

	template._si = si;
	template.fs = fs;

	if (fs->fs.flags & DN_QHT_HASH) {
		struct ipfw_flow_id masked_id;
		if (fs->qht == NULL) {
			fs->qht = dn_ht_init(NULL, fs->fs.buckets,
				offsetof(struct dn_queue, q_next),
				q_hash, q_match, q_new);
			if (fs->qht == NULL)
				return NULL;
		}
		masked_id = *id;
		flow_id_mask(&fs->fsk_mask, &masked_id);
		return dn_ht_find(fs->qht, (uintptr_t)&masked_id,
			DNHT_INSERT, &template);
	} else {
		if (fs->qht == NULL)
			fs->qht = q_new(0, 0, &template);
		return (struct dn_queue *)fs->qht;
	}
}
/*--- end of queue hash table ---*/

/*--- support functions for the sch_inst hashtable ----
 *
 * These are hashed by flow-id
 */
static uint32_t
si_hash(uintptr_t key, int flags, void *arg)
{
	/* compute the hash slot from the flow id */
	struct ipfw_flow_id *id = (flags & DNHT_KEY_IS_OBJ) ?
		&((struct dn_sch_inst *)key)->ni.fid :
		(struct ipfw_flow_id *)key;

	return flow_id_hash(id);
}

static int
si_match(void *obj, uintptr_t key, int flags, void *arg)
{
	struct dn_sch_inst *o = obj;
	struct ipfw_flow_id *id2;

	id2 = (flags & DNHT_KEY_IS_OBJ) ?
		&((struct dn_sch_inst *)key)->ni.fid :
		(struct ipfw_flow_id *)key;
	return flow_id_cmp(&o->ni.fid,  id2) == 0;
}

/*
 * create a new instance for the given 'key'
 * Allocate memory for instance, delay line and scheduler private data.
 */
static void *
si_new(uintptr_t key, int flags, void *arg)
{
	struct dn_schk *s = arg;
	struct dn_sch_inst *si;
	int l = sizeof(*si) + s->fp->si_datalen;

	si = malloc(l, M_DUMMYNET, M_NOWAIT | M_ZERO);
	if (si == NULL)
		goto error;

	/* Set length only for the part passed up to userland. */
	set_oid(&si->ni.oid, DN_SCH_I, sizeof(struct dn_flow));
	set_oid(&(si->dline.oid), DN_DELAY_LINE,
		sizeof(struct delay_line));
	/* mark si and dline as outside the event queue */
	si->ni.oid.id = si->dline.oid.id = -1;

	si->sched = s;
	si->dline.si = si;

	if (s->fp->new_sched && s->fp->new_sched(si)) {
		D("new_sched error");
		goto error;
	}
	if (s->sch.flags & DN_HAVE_MASK)
		si->ni.fid = *(struct ipfw_flow_id *)key;

#ifdef NEW_AQM
	/* init AQM status for !DN_MULTIQUEUE sched*/
	if (!(s->fp->flags & DN_MULTIQUEUE))
		if (s->fs->aqmfp && s->fs->aqmfp->init)
			if(s->fs->aqmfp->init((struct dn_queue *)(si + 1))) {
				D("unable to init AQM for fs %d", s->fs->fs.fs_nr);
				goto error;
			}
#endif

	dn_cfg.si_count++;
	return si;

error:
	if (si) {
		bzero(si, sizeof(*si)); // safety
		free(si, M_DUMMYNET);
	}
        return NULL;
}

/*
 * Callback from siht to delete all scheduler instances. Remove
 * si and delay line from the system heap, destroy all queues.
 * We assume that all flowset have been notified and do not
 * point to us anymore.
 */
static int
si_destroy(void *_si, void *arg)
{
	struct dn_sch_inst *si = _si;
	struct dn_schk *s = si->sched;
	struct delay_line *dl = &si->dline;

	if (dl->oid.subtype) /* remove delay line from event heap */
		heap_extract(&dn_cfg.evheap, dl);
	dn_free_pkts(dl->mq.head);	/* drain delay line */
	if (si->kflags & DN_ACTIVE) /* remove si from event heap */
		heap_extract(&dn_cfg.evheap, si);

#ifdef NEW_AQM
	/* clean up AQM status for !DN_MULTIQUEUE sched
	 * Note that all queues belong to fs were cleaned up in fsk_detach.
	 * When drain_scheduler is called s->fs and q->fs are pointing 
	 * to a correct fs, so we can use fs in this case.
	 */
	if (!(s->fp->flags & DN_MULTIQUEUE)) {
		struct dn_queue *q = (struct dn_queue *)(si + 1);
		if (q->aqm_status && q->fs->aqmfp)
			if (q->fs->aqmfp->cleanup)
				q->fs->aqmfp->cleanup(q);
	}
#endif
	if (s->fp->free_sched)
		s->fp->free_sched(si);
	bzero(si, sizeof(*si));	/* safety */
	free(si, M_DUMMYNET);
	dn_cfg.si_count--;
	return DNHT_SCAN_DEL;
}

/*
 * Find the scheduler instance for this packet. If we need to apply
 * a mask, do on a local copy of the flow_id to preserve the original.
 * Assume siht is always initialized if we have a mask.
 */
struct dn_sch_inst *
ipdn_si_find(struct dn_schk *s, struct ipfw_flow_id *id)
{

	if (s->sch.flags & DN_HAVE_MASK) {
		struct ipfw_flow_id id_t = *id;
		flow_id_mask(&s->sch.sched_mask, &id_t);
		return dn_ht_find(s->siht, (uintptr_t)&id_t,
			DNHT_INSERT, s);
	}
	if (!s->siht)
		s->siht = si_new(0, 0, s);
	return (struct dn_sch_inst *)s->siht;
}

/* callback to flush credit for the scheduler instance */
static int
si_reset_credit(void *_si, void *arg)
{
	struct dn_sch_inst *si = _si;
	struct dn_link *p = &si->sched->link;

	si->credit = p->burst + (dn_cfg.io_fast ?  p->bandwidth : 0);
	return 0;
}

static void
schk_reset_credit(struct dn_schk *s)
{
	if (s->sch.flags & DN_HAVE_MASK)
		dn_ht_scan(s->siht, si_reset_credit, NULL);
	else if (s->siht)
		si_reset_credit(s->siht, NULL);
}
/*---- end of sch_inst hashtable ---------------------*/

/*-------------------------------------------------------
 * flowset hash (fshash) support. Entries are hashed by fs_nr.
 * New allocations are put in the fsunlinked list, from which
 * they are removed when they point to a specific scheduler.
 */
static uint32_t
fsk_hash(uintptr_t key, int flags, void *arg)
{
	uint32_t i = !(flags & DNHT_KEY_IS_OBJ) ? key :
		((struct dn_fsk *)key)->fs.fs_nr;

	return ( (i>>8)^(i>>4)^i );
}

static int
fsk_match(void *obj, uintptr_t key, int flags, void *arg)
{
	struct dn_fsk *fs = obj;
	int i = !(flags & DNHT_KEY_IS_OBJ) ? key :
		((struct dn_fsk *)key)->fs.fs_nr;

	return (fs->fs.fs_nr == i);
}

static void *
fsk_new(uintptr_t key, int flags, void *arg)
{
	struct dn_fsk *fs;

	fs = malloc(sizeof(*fs), M_DUMMYNET, M_NOWAIT | M_ZERO);
	if (fs) {
		set_oid(&fs->fs.oid, DN_FS, sizeof(fs->fs));
		dn_cfg.fsk_count++;
		fs->drain_bucket = 0;
		SLIST_INSERT_HEAD(&dn_cfg.fsu, fs, sch_chain);
	}
	return fs;
}

#ifdef NEW_AQM
/* callback function for cleaning up AQM queue status belongs to a flowset
 * connected to scheduler instance '_si' (for !DN_MULTIQUEUE only).
 */
static int
si_cleanup_q(void *_si, void *arg)
{
	struct dn_sch_inst *si = _si;

	if (!(si->sched->fp->flags & DN_MULTIQUEUE)) {
		if (si->sched->fs->aqmfp && si->sched->fs->aqmfp->cleanup)
			si->sched->fs->aqmfp->cleanup((struct dn_queue *) (si+1));
	}
	return 0;
}

/* callback to clean up queue AQM status.*/
static int
q_cleanup_q(void *_q, void *arg)
{
	struct dn_queue *q = _q;
	q->fs->aqmfp->cleanup(q);
	return 0;
}

/* Clean up all AQM queues status belongs to flowset 'fs' and then
 * deconfig AQM for flowset 'fs'
 */
static void 
aqm_cleanup_deconfig_fs(struct dn_fsk *fs)
{
	struct dn_sch_inst *si;

	/* clean up AQM status for all queues for !DN_MULTIQUEUE sched*/
	if (fs->fs.fs_nr > DN_MAX_ID) {
		if (fs->sched && !(fs->sched->fp->flags & DN_MULTIQUEUE)) {
			if (fs->sched->sch.flags & DN_HAVE_MASK)
				dn_ht_scan(fs->sched->siht, si_cleanup_q, NULL);
			else {
					/* single si i.e. no sched mask */
					si = (struct dn_sch_inst *) fs->sched->siht;
					if (si && fs->aqmfp && fs->aqmfp->cleanup)
						fs->aqmfp->cleanup((struct dn_queue *) (si+1));
			}
		} 
	}

	/* clean up AQM status for all queues for DN_MULTIQUEUE sched*/
	if (fs->sched && fs->sched->fp->flags & DN_MULTIQUEUE && fs->qht) {
			if (fs->fs.flags & DN_QHT_HASH)
				dn_ht_scan(fs->qht, q_cleanup_q, NULL);
			else
				fs->aqmfp->cleanup((struct dn_queue *)(fs->qht));
	}

	/* deconfig AQM */
	if(fs->aqmcfg && fs->aqmfp && fs->aqmfp->deconfig)
		fs->aqmfp->deconfig(fs);
}
#endif

/*
 * detach flowset from its current scheduler. Flags as follows:
 * DN_DETACH removes from the fsk_list
 * DN_DESTROY deletes individual queues
 * DN_DELETE_FS destroys the flowset (otherwise goes in unlinked).
 */
static void
fsk_detach(struct dn_fsk *fs, int flags)
{
	if (flags & DN_DELETE_FS)
		flags |= DN_DESTROY;
	ND("fs %d from sched %d flags %s %s %s",
		fs->fs.fs_nr, fs->fs.sched_nr,
		(flags & DN_DELETE_FS) ? "DEL_FS":"",
		(flags & DN_DESTROY) ? "DEL":"",
		(flags & DN_DETACH) ? "DET":"");
	if (flags & DN_DETACH) { /* detach from the list */
		struct dn_fsk_head *h;
		h = fs->sched ? &fs->sched->fsk_list : &dn_cfg.fsu;
		SLIST_REMOVE(h, fs, dn_fsk, sch_chain);
	}
	/* Free the RED parameters, they will be recomputed on
	 * subsequent attach if needed.
	 */
	if (fs->w_q_lookup)
		free(fs->w_q_lookup, M_DUMMYNET);
	fs->w_q_lookup = NULL;
	qht_delete(fs, flags);
#ifdef NEW_AQM
	aqm_cleanup_deconfig_fs(fs);
#endif

	if (fs->sched && fs->sched->fp->free_fsk)
		fs->sched->fp->free_fsk(fs);
	fs->sched = NULL;
	if (flags & DN_DELETE_FS) {
		bzero(fs, sizeof(*fs));	/* safety */
		free(fs, M_DUMMYNET);
		dn_cfg.fsk_count--;
	} else {
		SLIST_INSERT_HEAD(&dn_cfg.fsu, fs, sch_chain);
	}
}

/*
 * Detach or destroy all flowsets in a list.
 * flags specifies what to do:
 * DN_DESTROY:	flush all queues
 * DN_DELETE_FS:	DN_DESTROY + destroy flowset
 *	DN_DELETE_FS implies DN_DESTROY
 */
static void
fsk_detach_list(struct dn_fsk_head *h, int flags)
{
	struct dn_fsk *fs;
	int n = 0; /* only for stats */

	ND("head %p flags %x", h, flags);
	while ((fs = SLIST_FIRST(h))) {
		SLIST_REMOVE_HEAD(h, sch_chain);
		n++;
		fsk_detach(fs, flags);
	}
	ND("done %d flowsets", n);
}

/*
 * called on 'queue X delete' -- removes the flowset from fshash,
 * deletes all queues for the flowset, and removes the flowset.
 */
static int
delete_fs(int i, int locked)
{
	struct dn_fsk *fs;
	int err = 0;

	if (!locked)
		DN_BH_WLOCK();
	fs = dn_ht_find(dn_cfg.fshash, i, DNHT_REMOVE, NULL);
	ND("fs %d found %p", i, fs);
	if (fs) {
		fsk_detach(fs, DN_DETACH | DN_DELETE_FS);
		err = 0;
	} else
		err = EINVAL;
	if (!locked)
		DN_BH_WUNLOCK();
	return err;
}

/*----- end of flowset hashtable support -------------*/

/*------------------------------------------------------------
 * Scheduler hash. When searching by index we pass sched_nr,
 * otherwise we pass struct dn_sch * which is the first field in
 * struct dn_schk so we can cast between the two. We use this trick
 * because in the create phase (but it should be fixed).
 */
static uint32_t
schk_hash(uintptr_t key, int flags, void *_arg)
{
	uint32_t i = !(flags & DNHT_KEY_IS_OBJ) ? key :
		((struct dn_schk *)key)->sch.sched_nr;
	return ( (i>>8)^(i>>4)^i );
}

static int
schk_match(void *obj, uintptr_t key, int flags, void *_arg)
{
	struct dn_schk *s = (struct dn_schk *)obj;
	int i = !(flags & DNHT_KEY_IS_OBJ) ? key :
		((struct dn_schk *)key)->sch.sched_nr;
	return (s->sch.sched_nr == i);
}

/*
 * Create the entry and intialize with the sched hash if needed.
 * Leave s->fp unset so we can tell whether a dn_ht_find() returns
 * a new object or a previously existing one.
 */
static void *
schk_new(uintptr_t key, int flags, void *arg)
{
	struct schk_new_arg *a = arg;
	struct dn_schk *s;
	int l = sizeof(*s) +a->fp->schk_datalen;

	s = malloc(l, M_DUMMYNET, M_NOWAIT | M_ZERO);
	if (s == NULL)
		return NULL;
	set_oid(&s->link.oid, DN_LINK, sizeof(s->link));
	s->sch = *a->sch; // copy initial values
	s->link.link_nr = s->sch.sched_nr;
	SLIST_INIT(&s->fsk_list);
	/* initialize the hash table or create the single instance */
	s->fp = a->fp;	/* si_new needs this */
	s->drain_bucket = 0;
	if (s->sch.flags & DN_HAVE_MASK) {
		s->siht = dn_ht_init(NULL, s->sch.buckets,
			offsetof(struct dn_sch_inst, si_next),
			si_hash, si_match, si_new);
		if (s->siht == NULL) {
			free(s, M_DUMMYNET);
			return NULL;
		}
	}
	s->fp = NULL;	/* mark as a new scheduler */
	dn_cfg.schk_count++;
	return s;
}

/*
 * Callback for sched delete. Notify all attached flowsets to
 * detach from the scheduler, destroy the internal flowset, and
 * all instances. The scheduler goes away too.
 * arg is 0 (only detach flowsets and destroy instances)
 * DN_DESTROY (detach & delete queues, delete schk)
 * or DN_DELETE_FS (delete queues and flowsets, delete schk)
 */
static int
schk_delete_cb(void *obj, void *arg)
{
	struct dn_schk *s = obj;
#if 0
	int a = (int)arg;
	ND("sched %d arg %s%s",
		s->sch.sched_nr,
		a&DN_DESTROY ? "DEL ":"",
		a&DN_DELETE_FS ? "DEL_FS":"");
#endif
	fsk_detach_list(&s->fsk_list, arg ? DN_DESTROY : 0);
	/* no more flowset pointing to us now */
	if (s->sch.flags & DN_HAVE_MASK) {
		dn_ht_scan(s->siht, si_destroy, NULL);
		dn_ht_free(s->siht, 0);
	} else if (s->siht)
		si_destroy(s->siht, NULL);
	if (s->profile) {
		free(s->profile, M_DUMMYNET);
		s->profile = NULL;
	}
	s->siht = NULL;
	if (s->fp->destroy)
		s->fp->destroy(s);
	bzero(s, sizeof(*s));	// safety
	free(obj, M_DUMMYNET);
	dn_cfg.schk_count--;
	return DNHT_SCAN_DEL;
}

/*
 * called on a 'sched X delete' command. Deletes a single scheduler.
 * This is done by removing from the schedhash, unlinking all
 * flowsets and deleting their traffic.
 */
static int
delete_schk(int i)
{
	struct dn_schk *s;

	s = dn_ht_find(dn_cfg.schedhash, i, DNHT_REMOVE, NULL);
	ND("%d %p", i, s);
	if (!s)
		return EINVAL;
	delete_fs(i + DN_MAX_ID, 1); /* first delete internal fs */
	/* then detach flowsets, delete traffic */
	schk_delete_cb(s, (void*)(uintptr_t)DN_DESTROY);
	return 0;
}
/*--- end of schk hashtable support ---*/

static int
copy_obj(char **start, char *end, void *_o, const char *msg, int i)
{
	struct dn_id o;
	union {
		struct dn_link l;
		struct dn_schk s;
	} dn;
	int have = end - *start;

	memcpy(&o, _o, sizeof(o));
	if (have < o.len || o.len == 0 || o.type == 0) {
		D("(WARN) type %d %s %d have %d need %d",
		    o.type, msg, i, have, o.len);
		return 1;
	}
	ND("type %d %s %d len %d", o.type, msg, i, o.len);
	if (o.type == DN_LINK) {
		memcpy(&dn.l, _o, sizeof(dn.l));
		/* Adjust burst parameter for link */
		dn.l.burst = div64(dn.l.burst, 8 * hz);
		dn.l.delay = dn.l.delay * 1000 / hz;
		memcpy(*start, &dn.l, sizeof(dn.l));
	} else if (o.type == DN_SCH) {
		/* Set dn.s.sch.oid.id to the number of instances */
		memcpy(&dn.s, _o, sizeof(dn.s));
		dn.s.sch.oid.id = (dn.s.sch.flags & DN_HAVE_MASK) ?
		    dn_ht_entries(dn.s.siht) : (dn.s.siht ? 1 : 0);
		memcpy(*start, &dn.s, sizeof(dn.s));
	} else
		memcpy(*start, _o, o.len);
	*start += o.len;
	return 0;
}

/* Specific function to copy a queue.
 * Copies only the user-visible part of a queue (which is in
 * a struct dn_flow), and sets len accordingly.
 */
static int
copy_obj_q(char **start, char *end, void *_o, const char *msg, int i)
{
	struct dn_id *o = _o;
	int have = end - *start;
	int len = sizeof(struct dn_flow); /* see above comment */

	if (have < len || o->len == 0 || o->type != DN_QUEUE) {
		D("ERROR type %d %s %d have %d need %d",
			o->type, msg, i, have, len);
		return 1;
	}
	ND("type %d %s %d len %d", o->type, msg, i, len);
	memcpy(*start, _o, len);
	((struct dn_id*)(*start))->len = len;
	*start += len;
	return 0;
}

static int
copy_q_cb(void *obj, void *arg)
{
	struct dn_queue *q = obj;
	struct copy_args *a = arg;
	struct dn_flow *ni = (struct dn_flow *)(*a->start);
        if (copy_obj_q(a->start, a->end, &q->ni, "queue", -1))
                return DNHT_SCAN_END;
        ni->oid.type = DN_FLOW; /* override the DN_QUEUE */
        ni->oid.id = si_hash((uintptr_t)&ni->fid, 0, NULL);
        return 0;
}

static int
copy_q(struct copy_args *a, struct dn_fsk *fs, int flags)
{
	if (!fs->qht)
		return 0;
	if (fs->fs.flags & DN_QHT_HASH)
		dn_ht_scan(fs->qht, copy_q_cb, a);
	else
		copy_q_cb(fs->qht, a);
	return 0;
}

/*
 * This routine only copies the initial part of a profile ? XXX
 */
static int
copy_profile(struct copy_args *a, struct dn_profile *p)
{
	int have = a->end - *a->start;
	/* XXX here we check for max length */
	int profile_len = sizeof(struct dn_profile) - 
		ED_MAX_SAMPLES_NO*sizeof(int);

	if (p == NULL)
		return 0;
	if (have < profile_len) {
		D("error have %d need %d", have, profile_len);
		return 1;
	}
	memcpy(*a->start, p, profile_len);
	((struct dn_id *)(*a->start))->len = profile_len;
	*a->start += profile_len;
	return 0;
}

static int
copy_flowset(struct copy_args *a, struct dn_fsk *fs, int flags)
{
	struct dn_fs *ufs = (struct dn_fs *)(*a->start);
	if (!fs)
		return 0;
	ND("flowset %d", fs->fs.fs_nr);
	if (copy_obj(a->start, a->end, &fs->fs, "flowset", fs->fs.fs_nr))
		return DNHT_SCAN_END;
	ufs->oid.id = (fs->fs.flags & DN_QHT_HASH) ?
		dn_ht_entries(fs->qht) : (fs->qht ? 1 : 0);
	if (flags) {	/* copy queues */
		copy_q(a, fs, 0);
	}
	return 0;
}

static int
copy_si_cb(void *obj, void *arg)
{
	struct dn_sch_inst *si = obj;
	struct copy_args *a = arg;
	struct dn_flow *ni = (struct dn_flow *)(*a->start);
	if (copy_obj(a->start, a->end, &si->ni, "inst",
			si->sched->sch.sched_nr))
		return DNHT_SCAN_END;
	ni->oid.type = DN_FLOW; /* override the DN_SCH_I */
	ni->oid.id = si_hash((uintptr_t)si, DNHT_KEY_IS_OBJ, NULL);
	return 0;
}

static int
copy_si(struct copy_args *a, struct dn_schk *s, int flags)
{
	if (s->sch.flags & DN_HAVE_MASK)
		dn_ht_scan(s->siht, copy_si_cb, a);
	else if (s->siht)
		copy_si_cb(s->siht, a);
	return 0;
}

/*
 * compute a list of children of a scheduler and copy up
 */
static int
copy_fsk_list(struct copy_args *a, struct dn_schk *s, int flags)
{
	struct dn_fsk *fs;
	struct dn_id *o;
	uint32_t *p;

	int n = 0, space = sizeof(*o);
	SLIST_FOREACH(fs, &s->fsk_list, sch_chain) {
		if (fs->fs.fs_nr < DN_MAX_ID)
			n++;
	}
	space += n * sizeof(uint32_t);
	DX(3, "sched %d has %d flowsets", s->sch.sched_nr, n);
	if (a->end - *(a->start) < space)
		return DNHT_SCAN_END;
	o = (struct dn_id *)(*(a->start));
	o->len = space;
	*a->start += o->len;
	o->type = DN_TEXT;
	p = (uint32_t *)(o+1);
	SLIST_FOREACH(fs, &s->fsk_list, sch_chain)
		if (fs->fs.fs_nr < DN_MAX_ID)
			*p++ = fs->fs.fs_nr;
	return 0;
}

static int
copy_data_helper(void *_o, void *_arg)
{
	struct copy_args *a = _arg;
	uint32_t *r = a->extra->r; /* start of first range */
	uint32_t *lim;	/* first invalid pointer */
	int n;

	lim = (uint32_t *)((char *)(a->extra) + a->extra->o.len);

	if (a->type == DN_LINK || a->type == DN_SCH) {
		/* pipe|sched show, we receive a dn_schk */
		struct dn_schk *s = _o;

		n = s->sch.sched_nr;
		if (a->type == DN_SCH && n >= DN_MAX_ID)
			return 0;	/* not a scheduler */
		if (a->type == DN_LINK && n <= DN_MAX_ID)
		    return 0;	/* not a pipe */

		/* see if the object is within one of our ranges */
		for (;r < lim; r += 2) {
			if (n < r[0] || n > r[1])
				continue;
			/* Found a valid entry, copy and we are done */
			if (a->flags & DN_C_LINK) {
				if (copy_obj(a->start, a->end,
				    &s->link, "link", n))
					return DNHT_SCAN_END;
				if (copy_profile(a, s->profile))
					return DNHT_SCAN_END;
				if (copy_flowset(a, s->fs, 0))
					return DNHT_SCAN_END;
			}
			if (a->flags & DN_C_SCH) {
				if (copy_obj(a->start, a->end,
				    &s->sch, "sched", n))
					return DNHT_SCAN_END;
				/* list all attached flowsets */
				if (copy_fsk_list(a, s, 0))
					return DNHT_SCAN_END;
			}
			if (a->flags & DN_C_FLOW)
				copy_si(a, s, 0);
			break;
		}
	} else if (a->type == DN_FS) {
		/* queue show, skip internal flowsets */
		struct dn_fsk *fs = _o;

		n = fs->fs.fs_nr;
		if (n >= DN_MAX_ID)
			return 0;
		/* see if the object is within one of our ranges */
		for (;r < lim; r += 2) {
			if (n < r[0] || n > r[1])
				continue;
			if (copy_flowset(a, fs, 0))
				return DNHT_SCAN_END;
			copy_q(a, fs, 0);
			break; /* we are done */
		}
	}
	return 0;
}

static inline struct dn_schk *
locate_scheduler(int i)
{
	return dn_ht_find(dn_cfg.schedhash, i, 0, NULL);
}

/*
 * red parameters are in fixed point arithmetic.
 */
static int
config_red(struct dn_fsk *fs)
{
	int64_t s, idle, weight, w0;
	int t, i;

	fs->w_q = fs->fs.w_q;
	fs->max_p = fs->fs.max_p;
	ND("called");
	/* Doing stuff that was in userland */
	i = fs->sched->link.bandwidth;
	s = (i <= 0) ? 0 :
		hz * dn_cfg.red_avg_pkt_size * 8 * SCALE(1) / i;

	idle = div64((s * 3) , fs->w_q); /* s, fs->w_q scaled; idle not scaled */
	fs->lookup_step = div64(idle , dn_cfg.red_lookup_depth);
	/* fs->lookup_step not scaled, */
	if (!fs->lookup_step)
		fs->lookup_step = 1;
	w0 = weight = SCALE(1) - fs->w_q; //fs->w_q scaled

	for (t = fs->lookup_step; t > 1; --t)
		weight = SCALE_MUL(weight, w0);
	fs->lookup_weight = (int)(weight); // scaled

	/* Now doing stuff that was in kerneland */
	fs->min_th = SCALE(fs->fs.min_th);
	fs->max_th = SCALE(fs->fs.max_th);

	if (fs->fs.max_th == fs->fs.min_th)
		fs->c_1 = fs->max_p;
	else
		fs->c_1 = SCALE((int64_t)(fs->max_p)) / (fs->fs.max_th - fs->fs.min_th);
	fs->c_2 = SCALE_MUL(fs->c_1, SCALE(fs->fs.min_th));

	if (fs->fs.flags & DN_IS_GENTLE_RED) {
		fs->c_3 = (SCALE(1) - fs->max_p) / fs->fs.max_th;
		fs->c_4 = SCALE(1) - 2 * fs->max_p;
	}

	/* If the lookup table already exist, free and create it again. */
	if (fs->w_q_lookup) {
		free(fs->w_q_lookup, M_DUMMYNET);
		fs->w_q_lookup = NULL;
	}
	if (dn_cfg.red_lookup_depth == 0) {
		printf("\ndummynet: net.inet.ip.dummynet.red_lookup_depth"
		    "must be > 0\n");
		fs->fs.flags &= ~DN_IS_RED;
		fs->fs.flags &= ~DN_IS_GENTLE_RED;
		return (EINVAL);
	}
	fs->lookup_depth = dn_cfg.red_lookup_depth;
	fs->w_q_lookup = (u_int *)malloc(fs->lookup_depth * sizeof(int),
	    M_DUMMYNET, M_NOWAIT);
	if (fs->w_q_lookup == NULL) {
		printf("dummynet: sorry, cannot allocate red lookup table\n");
		fs->fs.flags &= ~DN_IS_RED;
		fs->fs.flags &= ~DN_IS_GENTLE_RED;
		return(ENOSPC);
	}

	/* Fill the lookup table with (1 - w_q)^x */
	fs->w_q_lookup[0] = SCALE(1) - fs->w_q;

	for (i = 1; i < fs->lookup_depth; i++)
		fs->w_q_lookup[i] =
		    SCALE_MUL(fs->w_q_lookup[i - 1], fs->lookup_weight);

	if (dn_cfg.red_avg_pkt_size < 1)
		dn_cfg.red_avg_pkt_size = 512;
	fs->avg_pkt_size = dn_cfg.red_avg_pkt_size;
	if (dn_cfg.red_max_pkt_size < 1)
		dn_cfg.red_max_pkt_size = 1500;
	fs->max_pkt_size = dn_cfg.red_max_pkt_size;
	ND("exit");
	return 0;
}

/* Scan all flowset attached to this scheduler and update red */
static void
update_red(struct dn_schk *s)
{
	struct dn_fsk *fs;
	SLIST_FOREACH(fs, &s->fsk_list, sch_chain) {
		if (fs && (fs->fs.flags & DN_IS_RED))
			config_red(fs);
	}
}

/* attach flowset to scheduler s, possibly requeue */
static void
fsk_attach(struct dn_fsk *fs, struct dn_schk *s)
{
	ND("remove fs %d from fsunlinked, link to sched %d",
		fs->fs.fs_nr, s->sch.sched_nr);
	SLIST_REMOVE(&dn_cfg.fsu, fs, dn_fsk, sch_chain);
	fs->sched = s;
	SLIST_INSERT_HEAD(&s->fsk_list, fs, sch_chain);
	if (s->fp->new_fsk)
		s->fp->new_fsk(fs);
	/* XXX compute fsk_mask */
	fs->fsk_mask = fs->fs.flow_mask;
	if (fs->sched->sch.flags & DN_HAVE_MASK)
		flow_id_or(&fs->sched->sch.sched_mask, &fs->fsk_mask);
	if (fs->qht) {
		/*
		 * we must drain qht according to the old
		 * type, and reinsert according to the new one.
		 * The requeue is complex -- in general we need to
		 * reclassify every single packet.
		 * For the time being, let's hope qht is never set
		 * when we reach this point.
		 */
		D("XXX TODO requeue from fs %d to sch %d",
			fs->fs.fs_nr, s->sch.sched_nr);
		fs->qht = NULL;
	}
	/* set the new type for qht */
	if (nonzero_mask(&fs->fsk_mask))
		fs->fs.flags |= DN_QHT_HASH;
	else
		fs->fs.flags &= ~DN_QHT_HASH;

	/* XXX config_red() can fail... */
	if (fs->fs.flags & DN_IS_RED)
		config_red(fs);
}

/* update all flowsets which may refer to this scheduler */
static void
update_fs(struct dn_schk *s)
{
	struct dn_fsk *fs, *tmp;

	SLIST_FOREACH_SAFE(fs, &dn_cfg.fsu, sch_chain, tmp) {
		if (s->sch.sched_nr != fs->fs.sched_nr) {
			D("fs %d for sch %d not %d still unlinked",
				fs->fs.fs_nr, fs->fs.sched_nr,
				s->sch.sched_nr);
			continue;
		}
		fsk_attach(fs, s);
	}
}

#ifdef NEW_AQM
/* Retrieve AQM configurations to ipfw userland 
 */
static int
get_aqm_parms(struct sockopt *sopt)
{
	struct dn_extra_parms  *ep;
	struct dn_fsk *fs;
	size_t sopt_valsize;
	int l, err = 0;
	
	sopt_valsize = sopt->sopt_valsize;
	l = sizeof(*ep);
	if (sopt->sopt_valsize < l) {
		D("bad len sopt->sopt_valsize %d len %d",
			(int) sopt->sopt_valsize , l);
		err = EINVAL;
		return err;
	}
	ep = malloc(l, M_DUMMYNET, M_WAITOK);
	if(!ep) {
		err = ENOMEM ;
		return err;
	}
	do {
		err = sooptcopyin(sopt, ep, l, l);
		if(err)
			break;
		sopt->sopt_valsize = sopt_valsize;
		if (ep->oid.len < l) {
			err = EINVAL;
			break;
		}

		fs = dn_ht_find(dn_cfg.fshash, ep->nr, 0, NULL);
		if (!fs) {
			D("fs %d not found", ep->nr);
			err = EINVAL;
			break;
		}

		if (fs->aqmfp && fs->aqmfp->getconfig) {
			if(fs->aqmfp->getconfig(fs, ep)) {
				D("Error while trying to get AQM params");
				err = EINVAL;
				break;
			}
			ep->oid.len = l;
			err = sooptcopyout(sopt, ep, l);
		}
	}while(0);

	free(ep, M_DUMMYNET);
	return err;
}

/* Retrieve AQM configurations to ipfw userland
 */
static int
get_sched_parms(struct sockopt *sopt)
{
	struct dn_extra_parms  *ep;
	struct dn_schk *schk;
	size_t sopt_valsize;
	int l, err = 0;
	
	sopt_valsize = sopt->sopt_valsize;
	l = sizeof(*ep);
	if (sopt->sopt_valsize < l) {
		D("bad len sopt->sopt_valsize %d len %d",
			(int) sopt->sopt_valsize , l);
		err = EINVAL;
		return err;
	}
	ep = malloc(l, M_DUMMYNET, M_WAITOK);
	if(!ep) {
		err = ENOMEM ;
		return err;
	}
	do {
		err = sooptcopyin(sopt, ep, l, l);
		if(err)
			break;
		sopt->sopt_valsize = sopt_valsize;
		if (ep->oid.len < l) {
			err = EINVAL;
			break;
		}

		schk = locate_scheduler(ep->nr);
		if (!schk) {
			D("sched %d not found", ep->nr);
			err = EINVAL;
			break;
		}
		
		if (schk->fp && schk->fp->getconfig) {
			if(schk->fp->getconfig(schk, ep)) {
				D("Error while trying to get sched params");
				err = EINVAL;
				break;
			}
			ep->oid.len = l;
			err = sooptcopyout(sopt, ep, l);
		}
	}while(0);
	free(ep, M_DUMMYNET);

	return err;
}

/* Configure AQM for flowset 'fs'.
 * extra parameters are passed from userland.
 */
static int
config_aqm(struct dn_fsk *fs, struct  dn_extra_parms *ep, int busy)
{
	int err = 0;

	do {
		/* no configurations */
		if (!ep) {
			err = 0;
			break;
		}

		/* no AQM for this flowset*/
		if (!strcmp(ep->name,"")) {
			err = 0;
			break;
		}
		if (ep->oid.len < sizeof(*ep)) {
			D("short aqm len %d", ep->oid.len);
				err = EINVAL;
				break;
		}

		if (busy) {
			D("Unable to configure flowset, flowset busy!");
			err = EINVAL;
			break;
		}

		/* deconfigure old aqm if exist */
		if (fs->aqmcfg && fs->aqmfp && fs->aqmfp->deconfig) {
			aqm_cleanup_deconfig_fs(fs);
		}

		if (!(fs->aqmfp = find_aqm_type(0, ep->name))) {
			D("AQM functions not found for type %s!", ep->name);
			fs->fs.flags &= ~DN_IS_AQM;
			err = EINVAL;
			break;
		} else
			fs->fs.flags |= DN_IS_AQM;

		if (ep->oid.subtype != DN_AQM_PARAMS) {
				D("Wrong subtype");
				err = EINVAL;
				break;
		}

		if (fs->aqmfp->config) {
			err = fs->aqmfp->config(fs, ep, ep->oid.len);
			if (err) {
					D("Unable to configure AQM for FS %d", fs->fs.fs_nr );
					fs->fs.flags &= ~DN_IS_AQM;
					fs->aqmfp = NULL;
					break;
			}
		}
	} while(0);

	return err;
}
#endif

/*
 * Configuration -- to preserve backward compatibility we use
 * the following scheme (N is 65536)
 *	NUMBER		SCHED	LINK	FLOWSET
 *	   1 ..  N-1	(1)WFQ	(2)WFQ	(3)queue
 *	 N+1 .. 2N-1	(4)FIFO (5)FIFO	(6)FIFO for sched 1..N-1
 *	2N+1 .. 3N-1	--	--	(7)FIFO for sched N+1..2N-1
 *
 * "pipe i config" configures #1, #2 and #3
 * "sched i config" configures #1 and possibly #6
 * "queue i config" configures #3
 * #1 is configured with 'pipe i config' or 'sched i config'
 * #2 is configured with 'pipe i config', and created if not
 *	existing with 'sched i config'
 * #3 is configured with 'queue i config'
 * #4 is automatically configured after #1, can only be FIFO
 * #5 is automatically configured after #2
 * #6 is automatically created when #1 is !MULTIQUEUE,
 *	and can be updated.
 * #7 is automatically configured after #2
 */

/*
 * configure a link (and its FIFO instance)
 */
static int
config_link(struct dn_link *p, struct dn_id *arg)
{
	int i;

	if (p->oid.len != sizeof(*p)) {
		D("invalid pipe len %d", p->oid.len);
		return EINVAL;
	}
	i = p->link_nr;
	if (i <= 0 || i >= DN_MAX_ID)
		return EINVAL;
	/*
	 * The config program passes parameters as follows:
	 * bw = bits/second (0 means no limits),
	 * delay = ms, must be translated into ticks.
	 * qsize = slots/bytes
	 * burst ???
	 */
	p->delay = (p->delay * hz) / 1000;
	/* Scale burst size: bytes -> bits * hz */
	p->burst *= 8 * hz;

	DN_BH_WLOCK();
	/* do it twice, base link and FIFO link */
	for (; i < 2*DN_MAX_ID; i += DN_MAX_ID) {
	    struct dn_schk *s = locate_scheduler(i);
	    if (s == NULL) {
		DN_BH_WUNLOCK();
		D("sched %d not found", i);
		return EINVAL;
	    }
	    /* remove profile if exists */
	    if (s->profile) {
		free(s->profile, M_DUMMYNET);
		s->profile = NULL;
	    }
	    /* copy all parameters */
	    s->link.oid = p->oid;
	    s->link.link_nr = i;
	    s->link.delay = p->delay;
	    if (s->link.bandwidth != p->bandwidth) {
		/* XXX bandwidth changes, need to update red params */
	    s->link.bandwidth = p->bandwidth;
		update_red(s);
	    }
	    s->link.burst = p->burst;
	    schk_reset_credit(s);
	}
	dn_cfg.id++;
	DN_BH_WUNLOCK();
	return 0;
}

/*
 * configure a flowset. Can be called from inside with locked=1,
 */
static struct dn_fsk *
config_fs(struct dn_fs *nfs, struct dn_id *arg, int locked)
{
	int i;
	struct dn_fsk *fs;
#ifdef NEW_AQM
	struct dn_extra_parms *ep;
#endif

	if (nfs->oid.len != sizeof(*nfs)) {
		D("invalid flowset len %d", nfs->oid.len);
		return NULL;
	}
	i = nfs->fs_nr;
	if (i <= 0 || i >= 3*DN_MAX_ID)
		return NULL;
#ifdef NEW_AQM
	ep = NULL;
	if (arg != NULL) {
		ep = malloc(sizeof(*ep), M_TEMP, locked ? M_NOWAIT : M_WAITOK);
		if (ep == NULL)
			return (NULL);
		memcpy(ep, arg, sizeof(*ep));
	}
#endif
	ND("flowset %d", i);
	/* XXX other sanity checks */
        if (nfs->flags & DN_QSIZE_BYTES) {
		ipdn_bound_var(&nfs->qsize, 16384,
		    1500, dn_cfg.byte_limit, NULL); // "queue byte size");
        } else {
		ipdn_bound_var(&nfs->qsize, 50,
		    1, dn_cfg.slot_limit, NULL); // "queue slot size");
        }
	if (nfs->flags & DN_HAVE_MASK) {
		/* make sure we have some buckets */
		ipdn_bound_var((int *)&nfs->buckets, dn_cfg.hash_size,
			1, dn_cfg.max_hash_size, "flowset buckets");
	} else {
		nfs->buckets = 1;	/* we only need 1 */
	}
	if (!locked)
		DN_BH_WLOCK();
	do { /* exit with break when done */
	    struct dn_schk *s;
	    int flags = nfs->sched_nr ? DNHT_INSERT : 0;
	    int j;
	    int oldc = dn_cfg.fsk_count;
	    fs = dn_ht_find(dn_cfg.fshash, i, flags, NULL);
	    if (fs == NULL) {
		D("missing sched for flowset %d", i);
	        break;
	    }
	    /* grab some defaults from the existing one */
	    if (nfs->sched_nr == 0) /* reuse */
		nfs->sched_nr = fs->fs.sched_nr;
	    for (j = 0; j < sizeof(nfs->par)/sizeof(nfs->par[0]); j++) {
		if (nfs->par[j] == -1) /* reuse */
		    nfs->par[j] = fs->fs.par[j];
	    }
	    if (bcmp(&fs->fs, nfs, sizeof(*nfs)) == 0) {
		ND("flowset %d unchanged", i);
#ifdef NEW_AQM
		if (ep != NULL) {
			/*
			 * Reconfigure AQM as the parameters can be changed.
			 * We consider the flowset as busy if it has scheduler
			 * instance(s).
			 */ 
			s = locate_scheduler(nfs->sched_nr);
			config_aqm(fs, ep, s != NULL && s->siht != NULL);
		}
#endif
		break; /* no change, nothing to do */
	    }
	    if (oldc != dn_cfg.fsk_count)	/* new item */
		dn_cfg.id++;
	    s = locate_scheduler(nfs->sched_nr);
	    /* detach from old scheduler if needed, preserving
	     * queues if we need to reattach. Then update the
	     * configuration, and possibly attach to the new sched.
	     */
	    DX(2, "fs %d changed sched %d@%p to %d@%p",
		fs->fs.fs_nr,
		fs->fs.sched_nr, fs->sched, nfs->sched_nr, s);
	    if (fs->sched) {
		int flags = s ? DN_DETACH : (DN_DETACH | DN_DESTROY);
		flags |= DN_DESTROY; /* XXX temporary */
		fsk_detach(fs, flags);
	    }
	    fs->fs = *nfs; /* copy configuration */
#ifdef NEW_AQM
			fs->aqmfp = NULL;
			if (ep != NULL)
				config_aqm(fs, ep, s != NULL &&
				    s->siht != NULL);
#endif
	    if (s != NULL)
		fsk_attach(fs, s);
	} while (0);
	if (!locked)
		DN_BH_WUNLOCK();
#ifdef NEW_AQM
	if (ep != NULL)
		free(ep, M_TEMP);
#endif
	return fs;
}

/*
 * config/reconfig a scheduler and its FIFO variant.
 * For !MULTIQUEUE schedulers, also set up the flowset.
 *
 * On reconfigurations (detected because s->fp is set),
 * detach existing flowsets preserving traffic, preserve link,
 * and delete the old scheduler creating a new one.
 */
static int
config_sched(struct dn_sch *_nsch, struct dn_id *arg)
{
	struct dn_schk *s;
	struct schk_new_arg a; /* argument for schk_new */
	int i;
	struct dn_link p;	/* copy of oldlink */
	struct dn_profile *pf = NULL;	/* copy of old link profile */
	/* Used to preserv mask parameter */
	struct ipfw_flow_id new_mask;
	int new_buckets = 0;
	int new_flags = 0;
	int pipe_cmd;
	int err = ENOMEM;

	a.sch = _nsch;
	if (a.sch->oid.len != sizeof(*a.sch)) {
		D("bad sched len %d", a.sch->oid.len);
		return EINVAL;
	}
	i = a.sch->sched_nr;
	if (i <= 0 || i >= DN_MAX_ID)
		return EINVAL;
	/* make sure we have some buckets */
	if (a.sch->flags & DN_HAVE_MASK)
		ipdn_bound_var((int *)&a.sch->buckets, dn_cfg.hash_size,
			1, dn_cfg.max_hash_size, "sched buckets");
	/* XXX other sanity checks */
	bzero(&p, sizeof(p));

	pipe_cmd = a.sch->flags & DN_PIPE_CMD;
	a.sch->flags &= ~DN_PIPE_CMD; //XXX do it even if is not set?
	if (pipe_cmd) {
		/* Copy mask parameter */
		new_mask = a.sch->sched_mask;
		new_buckets = a.sch->buckets;
		new_flags = a.sch->flags;
	}
	DN_BH_WLOCK();
again: /* run twice, for wfq and fifo */
	/*
	 * lookup the type. If not supplied, use the previous one
	 * or default to WF2Q+. Otherwise, return an error.
	 */
	dn_cfg.id++;
	a.fp = find_sched_type(a.sch->oid.subtype, a.sch->name);
	if (a.fp != NULL) {
		/* found. Lookup or create entry */
		s = dn_ht_find(dn_cfg.schedhash, i, DNHT_INSERT, &a);
	} else if (a.sch->oid.subtype == 0 && !a.sch->name[0]) {
		/* No type. search existing s* or retry with WF2Q+ */
		s = dn_ht_find(dn_cfg.schedhash, i, 0, &a);
		if (s != NULL) {
			a.fp = s->fp;
			/* Scheduler exists, skip to FIFO scheduler 
			 * if command was pipe config...
			 */
			if (pipe_cmd)
				goto next;
		} else {
			/* New scheduler, create a wf2q+ with no mask
			 * if command was pipe config...
			 */
			if (pipe_cmd) {
				/* clear mask parameter */
				bzero(&a.sch->sched_mask, sizeof(new_mask));
				a.sch->buckets = 0;
				a.sch->flags &= ~DN_HAVE_MASK;
			}
			a.sch->oid.subtype = DN_SCHED_WF2QP;
			goto again;
		}
	} else {
		D("invalid scheduler type %d %s",
			a.sch->oid.subtype, a.sch->name);
		err = EINVAL;
		goto error;
	}
	/* normalize name and subtype */
	a.sch->oid.subtype = a.fp->type;
	bzero(a.sch->name, sizeof(a.sch->name));
	strlcpy(a.sch->name, a.fp->name, sizeof(a.sch->name));
	if (s == NULL) {
		D("cannot allocate scheduler %d", i);
		goto error;
	}
	/* restore existing link if any */
	if (p.link_nr) {
		s->link = p;
		if (!pf || pf->link_nr != p.link_nr) { /* no saved value */
			s->profile = NULL; /* XXX maybe not needed */
		} else {
			s->profile = malloc(sizeof(struct dn_profile),
					     M_DUMMYNET, M_NOWAIT | M_ZERO);
			if (s->profile == NULL) {
				D("cannot allocate profile");
				goto error; //XXX
			}
			memcpy(s->profile, pf, sizeof(*pf));
		}
	}
	p.link_nr = 0;
	if (s->fp == NULL) {
		DX(2, "sched %d new type %s", i, a.fp->name);
	} else if (s->fp != a.fp ||
			bcmp(a.sch, &s->sch, sizeof(*a.sch)) ) {
		/* already existing. */
		DX(2, "sched %d type changed from %s to %s",
			i, s->fp->name, a.fp->name);
		DX(4, "   type/sub %d/%d -> %d/%d",
			s->sch.oid.type, s->sch.oid.subtype, 
			a.sch->oid.type, a.sch->oid.subtype);
		if (s->link.link_nr == 0)
			D("XXX WARNING link 0 for sched %d", i);
		p = s->link;	/* preserve link */
		if (s->profile) {/* preserve profile */
			if (!pf)
				pf = malloc(sizeof(*pf),
				    M_DUMMYNET, M_NOWAIT | M_ZERO);
			if (pf)	/* XXX should issue a warning otherwise */
				memcpy(pf, s->profile, sizeof(*pf));
		}
		/* remove from the hash */
		dn_ht_find(dn_cfg.schedhash, i, DNHT_REMOVE, NULL);
		/* Detach flowsets, preserve queues. */
		// schk_delete_cb(s, NULL);
		// XXX temporarily, kill queues
		schk_delete_cb(s, (void *)DN_DESTROY);
		goto again;
	} else {
		DX(4, "sched %d unchanged type %s", i, a.fp->name);
	}
	/* complete initialization */
	s->sch = *a.sch;
	s->fp = a.fp;
	s->cfg = arg;
	// XXX schk_reset_credit(s);
	/* create the internal flowset if needed,
	 * trying to reuse existing ones if available
	 */
	if (!(s->fp->flags & DN_MULTIQUEUE) && !s->fs) {
	        s->fs = dn_ht_find(dn_cfg.fshash, i, 0, NULL);
		if (!s->fs) {
			struct dn_fs fs;
			bzero(&fs, sizeof(fs));
			set_oid(&fs.oid, DN_FS, sizeof(fs));
			fs.fs_nr = i + DN_MAX_ID;
			fs.sched_nr = i;
			s->fs = config_fs(&fs, NULL, 1 /* locked */);
		}
		if (!s->fs) {
			schk_delete_cb(s, (void *)DN_DESTROY);
			D("error creating internal fs for %d", i);
			goto error;
		}
	}
	/* call init function after the flowset is created */
	if (s->fp->config)
		s->fp->config(s);
	update_fs(s);
next:
	if (i < DN_MAX_ID) { /* now configure the FIFO instance */
		i += DN_MAX_ID;
		if (pipe_cmd) {
			/* Restore mask parameter for FIFO */
			a.sch->sched_mask = new_mask;
			a.sch->buckets = new_buckets;
			a.sch->flags = new_flags;
		} else {
			/* sched config shouldn't modify the FIFO scheduler */
			if (dn_ht_find(dn_cfg.schedhash, i, 0, &a) != NULL) {
				/* FIFO already exist, don't touch it */
				err = 0; /* and this is not an error */
				goto error;
			}
		}
		a.sch->sched_nr = i;
		a.sch->oid.subtype = DN_SCHED_FIFO;
		bzero(a.sch->name, sizeof(a.sch->name));
		goto again;
	}
	err = 0;
error:
	DN_BH_WUNLOCK();
	if (pf)
		free(pf, M_DUMMYNET);
	return err;
}

/*
 * attach a profile to a link
 */
static int
config_profile(struct dn_profile *pf, struct dn_id *arg)
{
	struct dn_schk *s;
	int i, olen, err = 0;

	if (pf->oid.len < sizeof(*pf)) {
		D("short profile len %d", pf->oid.len);
		return EINVAL;
	}
	i = pf->link_nr;
	if (i <= 0 || i >= DN_MAX_ID)
		return EINVAL;
	/* XXX other sanity checks */
	DN_BH_WLOCK();
	for (; i < 2*DN_MAX_ID; i += DN_MAX_ID) {
		s = locate_scheduler(i);

		if (s == NULL) {
			err = EINVAL;
			break;
		}
		dn_cfg.id++;
		/*
		 * If we had a profile and the new one does not fit,
		 * or it is deleted, then we need to free memory.
		 */
		if (s->profile && (pf->samples_no == 0 ||
		    s->profile->oid.len < pf->oid.len)) {
			free(s->profile, M_DUMMYNET);
			s->profile = NULL;
		}
		if (pf->samples_no == 0)
			continue;
		/*
		 * new profile, possibly allocate memory
		 * and copy data.
		 */
		if (s->profile == NULL)
			s->profile = malloc(pf->oid.len,
				    M_DUMMYNET, M_NOWAIT | M_ZERO);
		if (s->profile == NULL) {
			D("no memory for profile %d", i);
			err = ENOMEM;
			break;
		}
		/* preserve larger length XXX double check */
		olen = s->profile->oid.len;
		if (olen < pf->oid.len)
			olen = pf->oid.len;
		memcpy(s->profile, pf, pf->oid.len);
		s->profile->oid.len = olen;
	}
	DN_BH_WUNLOCK();
	return err;
}

/*
 * Delete all objects:
 */
static void
dummynet_flush(void)
{

	/* delete all schedulers and related links/queues/flowsets */
	dn_ht_scan(dn_cfg.schedhash, schk_delete_cb,
		(void *)(uintptr_t)DN_DELETE_FS);
	/* delete all remaining (unlinked) flowsets */
	DX(4, "still %d unlinked fs", dn_cfg.fsk_count);
	dn_ht_free(dn_cfg.fshash, DNHT_REMOVE);
	fsk_detach_list(&dn_cfg.fsu, DN_DELETE_FS);
	/* Reinitialize system heap... */
	heap_init(&dn_cfg.evheap, 16, offsetof(struct dn_id, id));
}

/*
 * Main handler for configuration. We are guaranteed to be called
 * with an oid which is at least a dn_id.
 * - the first object is the command (config, delete, flush, ...)
 * - config_link must be issued after the corresponding config_sched
 * - parameters (DN_TXT) for an object must precede the object
 *   processed on a config_sched.
 */
int
do_config(void *p, int l)
{
	struct dn_id o;
	union {
		struct dn_profile profile;
		struct dn_fs fs;
		struct dn_link link;
		struct dn_sch sched;
	} *dn;
	struct dn_id *arg;
	uintptr_t a;
	int err, err2, off;

	memcpy(&o, p, sizeof(o));
	if (o.id != DN_API_VERSION) {
		D("invalid api version got %d need %d", o.id, DN_API_VERSION);
		return EINVAL;
	}
	arg = NULL;
	dn = NULL;
	for (off = 0; l >= sizeof(o); memcpy(&o, (char *)p + off, sizeof(o))) {
		if (o.len < sizeof(o) || l < o.len) {
			D("bad len o.len %d len %d", o.len, l);
			err = EINVAL;
			break;
		}
		l -= o.len;
		err = 0;
		switch (o.type) {
		default:
			D("cmd %d not implemented", o.type);
			break;

#ifdef EMULATE_SYSCTL
		/* sysctl emulation.
		 * if we recognize the command, jump to the correct
		 * handler and return
		 */
		case DN_SYSCTL_SET:
			err = kesysctl_emu_set(p, l);
			return err;
#endif

		case DN_CMD_CONFIG: /* simply a header */
			break;

		case DN_CMD_DELETE:
			/* the argument is in the first uintptr_t after o */
			if (o.len < sizeof(o) + sizeof(a)) {
				err = EINVAL;
				break;
			}
			memcpy(&a, (char *)p + off + sizeof(o), sizeof(a));
			switch (o.subtype) {
			case DN_LINK:
				/* delete base and derived schedulers */
				DN_BH_WLOCK();
				err = delete_schk(a);
				err2 = delete_schk(a + DN_MAX_ID);
				DN_BH_WUNLOCK();
				if (!err)
					err = err2;
				break;

			default:
				D("invalid delete type %d", o.subtype);
				err = EINVAL;
				break;

			case DN_FS:
				err = (a < 1 || a >= DN_MAX_ID) ?
				    EINVAL : delete_fs(a, 0) ;
				break;
			}
			break;

		case DN_CMD_FLUSH:
			DN_BH_WLOCK();
			dummynet_flush();
			DN_BH_WUNLOCK();
			break;
		case DN_TEXT:	/* store argument of next block */
			if (arg != NULL)
				free(arg, M_TEMP);
			arg = malloc(o.len, M_TEMP, M_WAITOK);
			memcpy(arg, (char *)p + off, o.len);
			break;
		case DN_LINK:
			if (dn == NULL)
				dn = malloc(sizeof(*dn), M_TEMP, M_WAITOK);
			memcpy(&dn->link, (char *)p + off, sizeof(dn->link));
			err = config_link(&dn->link, arg);
			break;
		case DN_PROFILE:
			if (dn == NULL)
				dn = malloc(sizeof(*dn), M_TEMP, M_WAITOK);
			memcpy(&dn->profile, (char *)p + off,
			    sizeof(dn->profile));
			err = config_profile(&dn->profile, arg);
			break;
		case DN_SCH:
			if (dn == NULL)
				dn = malloc(sizeof(*dn), M_TEMP, M_WAITOK);
			memcpy(&dn->sched, (char *)p + off,
			    sizeof(dn->sched));
			err = config_sched(&dn->sched, arg);
			break;
		case DN_FS:
			if (dn == NULL)
				dn = malloc(sizeof(*dn), M_TEMP, M_WAITOK);
			memcpy(&dn->fs, (char *)p + off, sizeof(dn->fs));
			err = (NULL == config_fs(&dn->fs, arg, 0));
			break;
		}
		if (err != 0)
			break;
		off += o.len;
	}
	if (arg != NULL)
		free(arg, M_TEMP);
	if (dn != NULL)
		free(dn, M_TEMP);
	return err;
}

static int
compute_space(struct dn_id *cmd, struct copy_args *a)
{
	int x = 0, need = 0;
	int profile_size = sizeof(struct dn_profile) - 
		ED_MAX_SAMPLES_NO*sizeof(int);

	/* NOTE about compute space:
	 * NP 	= dn_cfg.schk_count
	 * NSI 	= dn_cfg.si_count
	 * NF 	= dn_cfg.fsk_count
	 * NQ 	= dn_cfg.queue_count
	 * - ipfw pipe show
	 *   (NP/2)*(dn_link + dn_sch + dn_id + dn_fs) only half scheduler
	 *                             link, scheduler template, flowset
	 *                             integrated in scheduler and header
	 *                             for flowset list
	 *   (NSI)*(dn_flow) all scheduler instance (includes
	 *                              the queue instance)
	 * - ipfw sched show
	 *   (NP/2)*(dn_link + dn_sch + dn_id + dn_fs) only half scheduler
	 *                             link, scheduler template, flowset
	 *                             integrated in scheduler and header
	 *                             for flowset list
	 *   (NSI * dn_flow) all scheduler instances
	 *   (NF * sizeof(uint_32)) space for flowset list linked to scheduler
	 *   (NQ * dn_queue) all queue [XXXfor now not listed]
	 * - ipfw queue show
	 *   (NF * dn_fs) all flowset
	 *   (NQ * dn_queue) all queues
	 */
	switch (cmd->subtype) {
	default:
		return -1;
	/* XXX where do LINK and SCH differ ? */
	/* 'ipfw sched show' could list all queues associated to
	 * a scheduler. This feature for now is disabled
	 */
	case DN_LINK:	/* pipe show */
		x = DN_C_LINK | DN_C_SCH | DN_C_FLOW;
		need += dn_cfg.schk_count *
			(sizeof(struct dn_fs) + profile_size) / 2;
		need += dn_cfg.fsk_count * sizeof(uint32_t);
		break;
	case DN_SCH:	/* sched show */
		need += dn_cfg.schk_count *
			(sizeof(struct dn_fs) + profile_size) / 2;
		need += dn_cfg.fsk_count * sizeof(uint32_t);
		x = DN_C_SCH | DN_C_LINK | DN_C_FLOW;
		break;
	case DN_FS:	/* queue show */
		x = DN_C_FS | DN_C_QUEUE;
		break;
	case DN_GET_COMPAT:	/* compatibility mode */
		need =  dn_compat_calc_size(); 
		break;
	}
	a->flags = x;
	if (x & DN_C_SCH) {
		need += dn_cfg.schk_count * sizeof(struct dn_sch) / 2;
		/* NOT also, each fs might be attached to a sched */
		need += dn_cfg.schk_count * sizeof(struct dn_id) / 2;
	}
	if (x & DN_C_FS)
		need += dn_cfg.fsk_count * sizeof(struct dn_fs);
	if (x & DN_C_LINK) {
		need += dn_cfg.schk_count * sizeof(struct dn_link) / 2;
	}
	/*
	 * When exporting a queue to userland, only pass up the
	 * struct dn_flow, which is the only visible part.
	 */

	if (x & DN_C_QUEUE)
		need += dn_cfg.queue_count * sizeof(struct dn_flow);
	if (x & DN_C_FLOW)
		need += dn_cfg.si_count * (sizeof(struct dn_flow));
	return need;
}

/*
 * If compat != NULL dummynet_get is called in compatibility mode.
 * *compat will be the pointer to the buffer to pass to ipfw
 */
int
dummynet_get(struct sockopt *sopt, void **compat)
{
	int have, i, need, error;
	char *start = NULL, *buf;
	size_t sopt_valsize;
	struct dn_id *cmd;
	struct copy_args a;
	struct copy_range r;
	int l = sizeof(struct dn_id);

	bzero(&a, sizeof(a));
	bzero(&r, sizeof(r));

	/* save and restore original sopt_valsize around copyin */
	sopt_valsize = sopt->sopt_valsize;

	cmd = &r.o;

	if (!compat) {
		/* copy at least an oid, and possibly a full object */
		error = sooptcopyin(sopt, cmd, sizeof(r), sizeof(*cmd));
		sopt->sopt_valsize = sopt_valsize;
		if (error)
			goto done;
		l = cmd->len;
#ifdef EMULATE_SYSCTL
		/* sysctl emulation. */
		if (cmd->type == DN_SYSCTL_GET)
			return kesysctl_emu_get(sopt);
#endif
		if (l > sizeof(r)) {
			/* request larger than default, allocate buffer */
			cmd = malloc(l,  M_DUMMYNET, M_WAITOK);
			error = sooptcopyin(sopt, cmd, l, l);
			sopt->sopt_valsize = sopt_valsize;
			if (error)
				goto done;
		}
	} else { /* compatibility */
		error = 0;
		cmd->type = DN_CMD_GET;
		cmd->len = sizeof(struct dn_id);
		cmd->subtype = DN_GET_COMPAT;
		// cmd->id = sopt_valsize;
		D("compatibility mode");
	}

#ifdef NEW_AQM
	/* get AQM params */
	if(cmd->subtype == DN_AQM_PARAMS) {
		error = get_aqm_parms(sopt);
		goto done;
	/* get Scheduler params */
	} else if (cmd->subtype == DN_SCH_PARAMS) {
		error = get_sched_parms(sopt);
		goto done;
	}
#endif

	a.extra = (struct copy_range *)cmd;
	if (cmd->len == sizeof(*cmd)) { /* no range, create a default */
		uint32_t *rp = (uint32_t *)(cmd + 1);
		cmd->len += 2* sizeof(uint32_t);
		rp[0] = 1;
		rp[1] = DN_MAX_ID - 1;
		if (cmd->subtype == DN_LINK) {
			rp[0] += DN_MAX_ID;
			rp[1] += DN_MAX_ID;
		}
	}
	/* Count space (under lock) and allocate (outside lock).
	 * Exit with lock held if we manage to get enough buffer.
	 * Try a few times then give up.
	 */
	for (have = 0, i = 0; i < 10; i++) {
		DN_BH_WLOCK();
		need = compute_space(cmd, &a);

		/* if there is a range, ignore value from compute_space() */
		if (l > sizeof(*cmd))
			need = sopt_valsize - sizeof(*cmd);

		if (need < 0) {
			DN_BH_WUNLOCK();
			error = EINVAL;
			goto done;
		}
		need += sizeof(*cmd);
		cmd->id = need;
		if (have >= need)
			break;

		DN_BH_WUNLOCK();
		if (start)
			free(start, M_DUMMYNET);
		start = NULL;
		if (need > sopt_valsize)
			break;

		have = need;
		start = malloc(have, M_DUMMYNET, M_WAITOK | M_ZERO);
	}

	if (start == NULL) {
		if (compat) {
			*compat = NULL;
			error =  1; // XXX
		} else {
			error = sooptcopyout(sopt, cmd, sizeof(*cmd));
		}
		goto done;
	}
	ND("have %d:%d sched %d, %d:%d links %d, %d:%d flowsets %d, "
		"%d:%d si %d, %d:%d queues %d",
		dn_cfg.schk_count, sizeof(struct dn_sch), DN_SCH,
		dn_cfg.schk_count, sizeof(struct dn_link), DN_LINK,
		dn_cfg.fsk_count, sizeof(struct dn_fs), DN_FS,
		dn_cfg.si_count, sizeof(struct dn_flow), DN_SCH_I,
		dn_cfg.queue_count, sizeof(struct dn_queue), DN_QUEUE);
	sopt->sopt_valsize = sopt_valsize;
	a.type = cmd->subtype;

	if (compat == NULL) {
		memcpy(start, cmd, sizeof(*cmd));
		((struct dn_id*)(start))->len = sizeof(struct dn_id);
		buf = start + sizeof(*cmd);
	} else
		buf = start;
	a.start = &buf;
	a.end = start + have;
	/* start copying other objects */
	if (compat) {
		a.type = DN_COMPAT_PIPE;
		dn_ht_scan(dn_cfg.schedhash, copy_data_helper_compat, &a);
		a.type = DN_COMPAT_QUEUE;
		dn_ht_scan(dn_cfg.fshash, copy_data_helper_compat, &a);
	} else if (a.type == DN_FS) {
		dn_ht_scan(dn_cfg.fshash, copy_data_helper, &a);
	} else {
		dn_ht_scan(dn_cfg.schedhash, copy_data_helper, &a);
	}
	DN_BH_WUNLOCK();

	if (compat) {
		*compat = start;
		sopt->sopt_valsize = buf - start;
		/* free() is done by ip_dummynet_compat() */
		start = NULL; //XXX hack
	} else {
		error = sooptcopyout(sopt, start, buf - start);
	}
done:
	if (cmd && cmd != &r.o)
		free(cmd, M_DUMMYNET);
	if (start)
		free(start, M_DUMMYNET);
	return error;
}

/* Callback called on scheduler instance to delete it if idle */
static int
drain_scheduler_cb(void *_si, void *arg)
{
	struct dn_sch_inst *si = _si;

	if ((si->kflags & DN_ACTIVE) || si->dline.mq.head != NULL)
		return 0;

	if (si->sched->fp->flags & DN_MULTIQUEUE) {
		if (si->q_count == 0)
			return si_destroy(si, NULL);
		else
			return 0;
	} else { /* !DN_MULTIQUEUE */
		if ((si+1)->ni.length == 0)
			return si_destroy(si, NULL);
		else
			return 0;
	}
	return 0; /* unreachable */
}

/* Callback called on scheduler to check if it has instances */
static int
drain_scheduler_sch_cb(void *_s, void *arg)
{
	struct dn_schk *s = _s;

	if (s->sch.flags & DN_HAVE_MASK) {
		dn_ht_scan_bucket(s->siht, &s->drain_bucket,
				drain_scheduler_cb, NULL);
		s->drain_bucket++;
	} else {
		if (s->siht) {
			if (drain_scheduler_cb(s->siht, NULL) == DNHT_SCAN_DEL)
				s->siht = NULL;
		}
	}
	return 0;
}

/* Called every tick, try to delete a 'bucket' of scheduler */
void
dn_drain_scheduler(void)
{
	dn_ht_scan_bucket(dn_cfg.schedhash, &dn_cfg.drain_sch,
			   drain_scheduler_sch_cb, NULL);
	dn_cfg.drain_sch++;
}

/* Callback called on queue to delete if it is idle */
static int
drain_queue_cb(void *_q, void *arg)
{
	struct dn_queue *q = _q;

	if (q->ni.length == 0) {
		dn_delete_queue(q, DN_DESTROY);
		return DNHT_SCAN_DEL; /* queue is deleted */
	}

	return 0; /* queue isn't deleted */
}

/* Callback called on flowset used to check if it has queues */
static int
drain_queue_fs_cb(void *_fs, void *arg)
{
	struct dn_fsk *fs = _fs;

	if (fs->fs.flags & DN_QHT_HASH) {
		/* Flowset has a hash table for queues */
		dn_ht_scan_bucket(fs->qht, &fs->drain_bucket,
				drain_queue_cb, NULL);
		fs->drain_bucket++;
	} else {
		/* No hash table for this flowset, null the pointer 
		 * if the queue is deleted
		 */
		if (fs->qht) {
			if (drain_queue_cb(fs->qht, NULL) == DNHT_SCAN_DEL)
				fs->qht = NULL;
		}
	}
	return 0;
}

/* Called every tick, try to delete a 'bucket' of queue */
void
dn_drain_queue(void)
{
	/* scan a bucket of flowset */
	dn_ht_scan_bucket(dn_cfg.fshash, &dn_cfg.drain_fs,
                               drain_queue_fs_cb, NULL);
	dn_cfg.drain_fs++;
}

/*
 * Handler for the various dummynet socket options
 */
static int
ip_dn_ctl(struct sockopt *sopt)
{
	void *p = NULL;
	int error, l;

	error = priv_check(sopt->sopt_td, PRIV_NETINET_DUMMYNET);
	if (error)
		return (error);

	/* Disallow sets in really-really secure mode. */
	if (sopt->sopt_dir == SOPT_SET) {
		error =  securelevel_ge(sopt->sopt_td->td_ucred, 3);
		if (error)
			return (error);
	}

	switch (sopt->sopt_name) {
	default :
		D("dummynet: unknown option %d", sopt->sopt_name);
		error = EINVAL;
		break;

	case IP_DUMMYNET_FLUSH:
	case IP_DUMMYNET_CONFIGURE:
	case IP_DUMMYNET_DEL:	/* remove a pipe or queue */
	case IP_DUMMYNET_GET:
		D("dummynet: compat option %d", sopt->sopt_name);
		error = ip_dummynet_compat(sopt);
		break;

	case IP_DUMMYNET3 :
		if (sopt->sopt_dir == SOPT_GET) {
			error = dummynet_get(sopt, NULL);
			break;
		}
		l = sopt->sopt_valsize;
		if (l < sizeof(struct dn_id) || l > 12000) {
			D("argument len %d invalid", l);
			break;
		}
		p = malloc(l, M_TEMP, M_WAITOK); // XXX can it fail ?
		error = sooptcopyin(sopt, p, l, l);
		if (error)
			break ;
		error = do_config(p, l);
		break;
	}

	if (p != NULL)
		free(p, M_TEMP);

	return error ;
}


static void
ip_dn_init(void)
{
	if (dn_cfg.init_done)
		return;
	printf("DUMMYNET %p with IPv6 initialized (100409)\n", curvnet);
	dn_cfg.init_done = 1;
	/* Set defaults here. MSVC does not accept initializers,
	 * and this is also useful for vimages
	 */
	/* queue limits */
	dn_cfg.slot_limit = 100; /* Foot shooting limit for queues. */
	dn_cfg.byte_limit = 1024 * 1024;
	dn_cfg.expire = 1;

	/* RED parameters */
	dn_cfg.red_lookup_depth = 256;	/* default lookup table depth */
	dn_cfg.red_avg_pkt_size = 512;	/* default medium packet size */
	dn_cfg.red_max_pkt_size = 1500;	/* default max packet size */

	/* hash tables */
	dn_cfg.max_hash_size = 65536;	/* max in the hash tables */
	dn_cfg.hash_size = 64;		/* default hash size */

	/* create hash tables for schedulers and flowsets.
	 * In both we search by key and by pointer.
	 */
	dn_cfg.schedhash = dn_ht_init(NULL, dn_cfg.hash_size,
		offsetof(struct dn_schk, schk_next),
		schk_hash, schk_match, schk_new);
	dn_cfg.fshash = dn_ht_init(NULL, dn_cfg.hash_size,
		offsetof(struct dn_fsk, fsk_next),
		fsk_hash, fsk_match, fsk_new);

	/* bucket index to drain object */
	dn_cfg.drain_fs = 0;
	dn_cfg.drain_sch = 0;

	heap_init(&dn_cfg.evheap, 16, offsetof(struct dn_id, id));
	SLIST_INIT(&dn_cfg.fsu);
	SLIST_INIT(&dn_cfg.schedlist);

	DN_LOCK_INIT();

	TASK_INIT(&dn_task, 0, dummynet_task, curvnet);
	dn_tq = taskqueue_create_fast("dummynet", M_WAITOK,
	    taskqueue_thread_enqueue, &dn_tq);
	taskqueue_start_threads(&dn_tq, 1, PI_NET, "dummynet");

	callout_init(&dn_timeout, 1);
	dn_reschedule();

	/* Initialize curr_time adjustment mechanics. */
	getmicrouptime(&dn_cfg.prev_t);
}

static void
ip_dn_destroy(int last)
{
	DN_BH_WLOCK();
	/* ensure no more callouts are started */
	dn_gone = 1;

	/* check for last */
	if (last) {
		ND("removing last instance\n");
		ip_dn_ctl_ptr = NULL;
		ip_dn_io_ptr = NULL;
	}

	dummynet_flush();
	DN_BH_WUNLOCK();

	callout_drain(&dn_timeout);
	taskqueue_drain(dn_tq, &dn_task);
	taskqueue_free(dn_tq);

	dn_ht_free(dn_cfg.schedhash, 0);
	dn_ht_free(dn_cfg.fshash, 0);
	heap_free(&dn_cfg.evheap);

	DN_LOCK_DESTROY();
}

static int
dummynet_modevent(module_t mod, int type, void *data)
{

	if (type == MOD_LOAD) {
		if (ip_dn_io_ptr) {
			printf("DUMMYNET already loaded\n");
			return EEXIST ;
		}
		ip_dn_init();
		ip_dn_ctl_ptr = ip_dn_ctl;
		ip_dn_io_ptr = dummynet_io;
		return 0;
	} else if (type == MOD_UNLOAD) {
		ip_dn_destroy(1 /* last */);
		return 0;
	} else
		return EOPNOTSUPP;
}

/* modevent helpers for the modules */
static int
load_dn_sched(struct dn_alg *d)
{
	struct dn_alg *s;

	if (d == NULL)
		return 1; /* error */
	ip_dn_init();	/* just in case, we need the lock */

	/* Check that mandatory funcs exists */
	if (d->enqueue == NULL || d->dequeue == NULL) {
		D("missing enqueue or dequeue for %s", d->name);
		return 1;
	}

	/* Search if scheduler already exists */
	DN_BH_WLOCK();
	SLIST_FOREACH(s, &dn_cfg.schedlist, next) {
		if (strcmp(s->name, d->name) == 0) {
			D("%s already loaded", d->name);
			break; /* scheduler already exists */
		}
	}
	if (s == NULL)
		SLIST_INSERT_HEAD(&dn_cfg.schedlist, d, next);
	DN_BH_WUNLOCK();
	D("dn_sched %s %sloaded", d->name, s ? "not ":"");
	return s ? 1 : 0;
}

static int
unload_dn_sched(struct dn_alg *s)
{
	struct dn_alg *tmp, *r;
	int err = EINVAL;

	ND("called for %s", s->name);

	DN_BH_WLOCK();
	SLIST_FOREACH_SAFE(r, &dn_cfg.schedlist, next, tmp) {
		if (strcmp(s->name, r->name) != 0)
			continue;
		ND("ref_count = %d", r->ref_count);
		err = (r->ref_count != 0) ? EBUSY : 0;
		if (err == 0)
			SLIST_REMOVE(&dn_cfg.schedlist, r, dn_alg, next);
		break;
	}
	DN_BH_WUNLOCK();
	D("dn_sched %s %sunloaded", s->name, err ? "not ":"");
	return err;
}

int
dn_sched_modevent(module_t mod, int cmd, void *arg)
{
	struct dn_alg *sch = arg;

	if (cmd == MOD_LOAD)
		return load_dn_sched(sch);
	else if (cmd == MOD_UNLOAD)
		return unload_dn_sched(sch);
	else
		return EINVAL;
}

static moduledata_t dummynet_mod = {
	"dummynet", dummynet_modevent, NULL
};

#define	DN_SI_SUB	SI_SUB_PROTO_FIREWALL
#define	DN_MODEV_ORD	(SI_ORDER_ANY - 128) /* after ipfw */
DECLARE_MODULE(dummynet, dummynet_mod, DN_SI_SUB, DN_MODEV_ORD);
MODULE_DEPEND(dummynet, ipfw, 3, 3, 3);
MODULE_VERSION(dummynet, 3);

/*
 * Starting up. Done in order after dummynet_modevent() has been called.
 * VNET_SYSINIT is also called for each existing vnet and each new vnet.
 */
//VNET_SYSINIT(vnet_dn_init, DN_SI_SUB, DN_MODEV_ORD+2, ip_dn_init, NULL);

/*
 * Shutdown handlers up shop. These are done in REVERSE ORDER, but still
 * after dummynet_modevent() has been called. Not called on reboot.
 * VNET_SYSUNINIT is also called for each exiting vnet as it exits.
 * or when the module is unloaded.
 */
//VNET_SYSUNINIT(vnet_dn_uninit, DN_SI_SUB, DN_MODEV_ORD+2, ip_dn_destroy, NULL);

#ifdef NEW_AQM

/* modevent helpers for the AQM modules */
static int
load_dn_aqm(struct dn_aqm *d)
{
	struct dn_aqm *aqm=NULL;

	if (d == NULL)
		return 1; /* error */
	ip_dn_init();	/* just in case, we need the lock */

	/* Check that mandatory funcs exists */
	if (d->enqueue == NULL || d->dequeue == NULL) {
		D("missing enqueue or dequeue for %s", d->name);
		return 1;
	}

	/* Search if AQM already exists */
	DN_BH_WLOCK();
	SLIST_FOREACH(aqm, &dn_cfg.aqmlist, next) {
		if (strcmp(aqm->name, d->name) == 0) {
			D("%s already loaded", d->name);
			break; /* AQM already exists */
		}
	}
	if (aqm == NULL)
		SLIST_INSERT_HEAD(&dn_cfg.aqmlist, d, next);
	DN_BH_WUNLOCK();
	D("dn_aqm %s %sloaded", d->name, aqm ? "not ":"");
	return aqm ? 1 : 0;
}


/* Callback to clean up AQM status for queues connected to a flowset
 * and then deconfigure the flowset.
 * This function is called before an AQM module is unloaded
 */
static int
fs_cleanup(void *_fs, void *arg)
{
	struct dn_fsk *fs = _fs;
	uint32_t type = *(uint32_t *)arg;

	if (fs->aqmfp && fs->aqmfp->type == type)
		aqm_cleanup_deconfig_fs(fs);

	return 0;
}

static int
unload_dn_aqm(struct dn_aqm *aqm)
{
	struct dn_aqm *tmp, *r;
	int err = EINVAL;
	err = 0;
	ND("called for %s", aqm->name);

	DN_BH_WLOCK();

	/* clean up AQM status and deconfig flowset */
	dn_ht_scan(dn_cfg.fshash, fs_cleanup, &aqm->type);

	SLIST_FOREACH_SAFE(r, &dn_cfg.aqmlist, next, tmp) {
		if (strcmp(aqm->name, r->name) != 0)
			continue;
		ND("ref_count = %d", r->ref_count);
		err = (r->ref_count != 0 || r->cfg_ref_count != 0) ? EBUSY : 0;
		if (err == 0)
			SLIST_REMOVE(&dn_cfg.aqmlist, r, dn_aqm, next);
		break;
	}
	DN_BH_WUNLOCK();
	D("%s %sunloaded", aqm->name, err ? "not ":"");
	if (err)
		D("ref_count=%d, cfg_ref_count=%d", r->ref_count, r->cfg_ref_count);
	return err;
}

int
dn_aqm_modevent(module_t mod, int cmd, void *arg)
{
	struct dn_aqm *aqm = arg;

	if (cmd == MOD_LOAD)
		return load_dn_aqm(aqm);
	else if (cmd == MOD_UNLOAD)
		return unload_dn_aqm(aqm);
	else
		return EINVAL;
}
#endif

/* end of file */

