/*-
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD
 *
 * Copyright (c) 2010 Riccardo Panicucci, Universita` di Pisa
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

/*
 * $FreeBSD$
 *
 * Binary compatibility support for /sbin/ipfw RELENG_7 and RELENG_8
 */

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

/* FREEBSD7.2 ip_dummynet.h r191715*/

struct dn_heap_entry7 {
	int64_t key;        /* sorting key. Topmost element is smallest one */
	void *object;      /* object pointer */
};

struct dn_heap7 {
	int size;
	int elements;
	int offset; /* XXX if > 0 this is the offset of direct ptr to obj */
	struct dn_heap_entry7 *p;   /* really an array of "size" entries */
};

/* Common to 7.2 and 8 */
struct dn_flow_set {
	SLIST_ENTRY(dn_flow_set)    next;   /* linked list in a hash slot */

	u_short fs_nr ;             /* flow_set number       */
	u_short flags_fs;
#define DNOLD_HAVE_FLOW_MASK   0x0001
#define DNOLD_IS_RED       0x0002
#define DNOLD_IS_GENTLE_RED    0x0004
#define DNOLD_QSIZE_IS_BYTES   0x0008  /* queue size is measured in bytes */
#define DNOLD_NOERROR      0x0010  /* do not report ENOBUFS on drops  */
#define DNOLD_HAS_PROFILE      0x0020  /* the pipe has a delay profile. */
#define DNOLD_IS_PIPE      0x4000
#define DNOLD_IS_QUEUE     0x8000

	struct dn_pipe7 *pipe ;  /* pointer to parent pipe */
	u_short parent_nr ;     /* parent pipe#, 0 if local to a pipe */

	int weight ;        /* WFQ queue weight */
	int qsize ;         /* queue size in slots or bytes */
	int plr ;           /* pkt loss rate (2^31-1 means 100%) */

	struct ipfw_flow_id flow_mask ;

	/* hash table of queues onto this flow_set */
	int rq_size ;       /* number of slots */
	int rq_elements ;       /* active elements */
	struct dn_flow_queue7 **rq;  /* array of rq_size entries */

	u_int32_t last_expired ;    /* do not expire too frequently */
	int backlogged ;        /* #active queues for this flowset */

        /* RED parameters */
#define SCALE_RED               16
#define SCALE(x)                ( (x) << SCALE_RED )
#define SCALE_VAL(x)            ( (x) >> SCALE_RED )
#define SCALE_MUL(x,y)          ( ( (x) * (y) ) >> SCALE_RED )
	int w_q ;           /* queue weight (scaled) */
	int max_th ;        /* maximum threshold for queue (scaled) */
	int min_th ;        /* minimum threshold for queue (scaled) */
	int max_p ;         /* maximum value for p_b (scaled) */
	u_int c_1 ;         /* max_p/(max_th-min_th) (scaled) */
	u_int c_2 ;         /* max_p*min_th/(max_th-min_th) (scaled) */
	u_int c_3 ;         /* for GRED, (1-max_p)/max_th (scaled) */
	u_int c_4 ;         /* for GRED, 1 - 2*max_p (scaled) */
	u_int * w_q_lookup ;    /* lookup table for computing (1-w_q)^t */
	u_int lookup_depth ;    /* depth of lookup table */
	int lookup_step ;       /* granularity inside the lookup table */
	int lookup_weight ;     /* equal to (1-w_q)^t / (1-w_q)^(t+1) */
	int avg_pkt_size ;      /* medium packet size */
	int max_pkt_size ;      /* max packet size */
};
SLIST_HEAD(dn_flow_set_head, dn_flow_set);

#define DN_IS_PIPE		0x4000
#define DN_IS_QUEUE		0x8000
struct dn_flow_queue7 {
	struct dn_flow_queue7 *next ;
	struct ipfw_flow_id id ;

	struct mbuf *head, *tail ;  /* queue of packets */
	u_int len ;
	u_int len_bytes ;

	u_long numbytes;

	u_int64_t tot_pkts ;    /* statistics counters  */
	u_int64_t tot_bytes ;
	u_int32_t drops ;

	int hash_slot ;     /* debugging/diagnostic */

	/* RED parameters */
	int avg ;                   /* average queue length est. (scaled) */
	int count ;                 /* arrivals since last RED drop */
	int random ;                /* random value (scaled) */
	u_int32_t q_time;      /* start of queue idle time */

	/* WF2Q+ support */
	struct dn_flow_set *fs ;    /* parent flow set */
	int heap_pos ;      /* position (index) of struct in heap */
	int64_t sched_time ;     /* current time when queue enters ready_heap */

	int64_t S,F ;        /* start time, finish time */
};

struct dn_pipe7 {        /* a pipe */
	SLIST_ENTRY(dn_pipe7)    next;   /* linked list in a hash slot */

	int pipe_nr ;       /* number   */
	int bandwidth;      /* really, bytes/tick.  */
	int delay ;         /* really, ticks    */

	struct  mbuf *head, *tail ; /* packets in delay line */

	/* WF2Q+ */
	struct dn_heap7 scheduler_heap ; /* top extract - key Finish time*/
	struct dn_heap7 not_eligible_heap; /* top extract- key Start time */
	struct dn_heap7 idle_heap ; /* random extract - key Start=Finish time */

	int64_t V ;          /* virtual time */
	int sum;            /* sum of weights of all active sessions */

	int numbytes;

	int64_t sched_time ;     /* time pipe was scheduled in ready_heap */

	/*
	* When the tx clock come from an interface (if_name[0] != '\0'), its name
	* is stored below, whereas the ifp is filled when the rule is configured.
	*/
	char if_name[IFNAMSIZ];
	struct ifnet *ifp ;
	int ready ; /* set if ifp != NULL and we got a signal from it */

	struct dn_flow_set fs ; /* used with fixed-rate flows */
};
SLIST_HEAD(dn_pipe_head7, dn_pipe7);


/* FREEBSD8 ip_dummynet.h r196045 */
struct dn_flow_queue8 {
	struct dn_flow_queue8 *next ;
	struct ipfw_flow_id id ;

	struct mbuf *head, *tail ;  /* queue of packets */
	u_int len ;
	u_int len_bytes ;

	uint64_t numbytes ;     /* credit for transmission (dynamic queues) */
	int64_t extra_bits;     /* extra bits simulating unavailable channel */

	u_int64_t tot_pkts ;    /* statistics counters  */
	u_int64_t tot_bytes ;
	u_int32_t drops ;

	int hash_slot ;     /* debugging/diagnostic */

	/* RED parameters */
	int avg ;                   /* average queue length est. (scaled) */
	int count ;                 /* arrivals since last RED drop */
	int random ;                /* random value (scaled) */
	int64_t idle_time;       /* start of queue idle time */

	/* WF2Q+ support */
	struct dn_flow_set *fs ;    /* parent flow set */
	int heap_pos ;      /* position (index) of struct in heap */
	int64_t sched_time ;     /* current time when queue enters ready_heap */

	int64_t S,F ;        /* start time, finish time */
};

struct dn_pipe8 {        /* a pipe */
	SLIST_ENTRY(dn_pipe8)    next;   /* linked list in a hash slot */

	int pipe_nr ;       /* number   */
	int bandwidth;      /* really, bytes/tick.  */
	int delay ;         /* really, ticks    */

	struct  mbuf *head, *tail ; /* packets in delay line */

	/* WF2Q+ */
	struct dn_heap7 scheduler_heap ; /* top extract - key Finish time*/
	struct dn_heap7 not_eligible_heap; /* top extract- key Start time */
	struct dn_heap7 idle_heap ; /* random extract - key Start=Finish time */

	int64_t V ;          /* virtual time */
	int sum;            /* sum of weights of all active sessions */

	/* Same as in dn_flow_queue, numbytes can become large */
	int64_t numbytes;       /* bits I can transmit (more or less). */
	uint64_t burst;     /* burst size, scaled: bits * hz */

	int64_t sched_time ;     /* time pipe was scheduled in ready_heap */
	int64_t idle_time;       /* start of pipe idle time */

	char if_name[IFNAMSIZ];
	struct ifnet *ifp ;
	int ready ; /* set if ifp != NULL and we got a signal from it */

	struct dn_flow_set fs ; /* used with fixed-rate flows */

    /* fields to simulate a delay profile */
#define ED_MAX_NAME_LEN     32
	char name[ED_MAX_NAME_LEN];
	int loss_level;
	int samples_no;
	int *samples;
};

#define ED_MAX_SAMPLES_NO   1024
struct dn_pipe_max8 {
	struct dn_pipe8 pipe;
	int samples[ED_MAX_SAMPLES_NO];
};
SLIST_HEAD(dn_pipe_head8, dn_pipe8);

/*
 * Changes from 7.2 to 8:
 * dn_pipe:
 *      numbytes from int to int64_t
 *      add burst (int64_t)
 *      add idle_time (int64_t)
 *      add profile
 *      add struct dn_pipe_max
 *      add flag DN_HAS_PROFILE
 *
 * dn_flow_queue
 *      numbytes from u_long to int64_t
 *      add extra_bits (int64_t)
 *      q_time from u_int32_t to int64_t and name idle_time
 *
 * dn_flow_set unchanged
 *
 */

/* NOTE:XXX copied from dummynet.c */
#define O_NEXT(p, len) ((void *)((char *)p + len))
static void
oid_fill(struct dn_id *oid, int len, int type, uintptr_t id)
{
	oid->len = len;
	oid->type = type;
	oid->subtype = 0;
	oid->id = id;
}
/* make room in the buffer and move the pointer forward */
static void *
o_next(struct dn_id **o, int len, int type)
{
	struct dn_id *ret = *o;
	oid_fill(ret, len, type, 0);
	*o = O_NEXT(*o, len);
	return ret;
}


static size_t pipesize7 = sizeof(struct dn_pipe7);
static size_t pipesize8 = sizeof(struct dn_pipe8);
static size_t pipesizemax8 = sizeof(struct dn_pipe_max8);

/* Indicate 'ipfw' version
 * 1: from FreeBSD 7.2
 * 0: from FreeBSD 8
 * -1: unknown (for now is unused)
 *
 * It is update when a IP_DUMMYNET_DEL or IP_DUMMYNET_CONFIGURE request arrives
 * NOTE: if a IP_DUMMYNET_GET arrives and the 'ipfw' version is unknown,
 *       it is suppose to be the FreeBSD 8 version.
 */
static int is7 = 0;

static int
convertflags2new(int src)
{
	int dst = 0;

	if (src & DNOLD_HAVE_FLOW_MASK)
		dst |= DN_HAVE_MASK;
	if (src & DNOLD_QSIZE_IS_BYTES)
		dst |= DN_QSIZE_BYTES;
	if (src & DNOLD_NOERROR)
		dst |= DN_NOERROR;
	if (src & DNOLD_IS_RED)
		dst |= DN_IS_RED;
	if (src & DNOLD_IS_GENTLE_RED)
		dst |= DN_IS_GENTLE_RED;
	if (src & DNOLD_HAS_PROFILE)
		dst |= DN_HAS_PROFILE;

	return dst;
}

static int
convertflags2old(int src)
{
	int dst = 0;

	if (src & DN_HAVE_MASK)
		dst |= DNOLD_HAVE_FLOW_MASK;
	if (src & DN_IS_RED)
		dst |= DNOLD_IS_RED;
	if (src & DN_IS_GENTLE_RED)
		dst |= DNOLD_IS_GENTLE_RED;
	if (src & DN_NOERROR)
		dst |= DNOLD_NOERROR;
	if (src & DN_HAS_PROFILE)
		dst |= DNOLD_HAS_PROFILE;
	if (src & DN_QSIZE_BYTES)
		dst |= DNOLD_QSIZE_IS_BYTES;

	return dst;
}

static int
dn_compat_del(void *v)
{
	struct dn_pipe7 *p = (struct dn_pipe7 *) v;
	struct dn_pipe8 *p8 = (struct dn_pipe8 *) v;
	struct {
		struct dn_id oid;
		uintptr_t a[1];	/* add more if we want a list */
	} cmd;

	/* XXX DN_API_VERSION ??? */
	oid_fill((void *)&cmd, sizeof(cmd), DN_CMD_DELETE, DN_API_VERSION);

	if (is7) {
		if (p->pipe_nr == 0 && p->fs.fs_nr == 0)
			return EINVAL;
		if (p->pipe_nr != 0 && p->fs.fs_nr != 0)
			return EINVAL;
	} else {
		if (p8->pipe_nr == 0 && p8->fs.fs_nr == 0)
			return EINVAL;
		if (p8->pipe_nr != 0 && p8->fs.fs_nr != 0)
			return EINVAL;
	}

	if (p->pipe_nr != 0) { /* pipe x delete */
		cmd.a[0] = p->pipe_nr;
		cmd.oid.subtype = DN_LINK;
	} else { /* queue x delete */
		cmd.oid.subtype = DN_FS;
		cmd.a[0] = (is7) ? p->fs.fs_nr : p8->fs.fs_nr;
	}

	return do_config(&cmd, cmd.oid.len);
}

static int
dn_compat_config_queue(struct dn_fs *fs, void* v)
{
	struct dn_pipe7 *p7 = (struct dn_pipe7 *)v;
	struct dn_pipe8 *p8 = (struct dn_pipe8 *)v;
	struct dn_flow_set *f;

	if (is7)
		f = &p7->fs;
	else
		f = &p8->fs;

	fs->fs_nr = f->fs_nr;
	fs->sched_nr = f->parent_nr;
	fs->flow_mask = f->flow_mask;
	fs->buckets = f->rq_size;
	fs->qsize = f->qsize;
	fs->plr = f->plr;
	fs->par[0] = f->weight;
	fs->flags = convertflags2new(f->flags_fs);
	if (fs->flags & DN_IS_GENTLE_RED || fs->flags & DN_IS_RED) {
		fs->w_q = f->w_q;
		fs->max_th = f->max_th;
		fs->min_th = f->min_th;
		fs->max_p = f->max_p;
	}

	return 0;
}

static int
dn_compat_config_pipe(struct dn_sch *sch, struct dn_link *p, 
		      struct dn_fs *fs, void* v)
{
	struct dn_pipe7 *p7 = (struct dn_pipe7 *)v;
	struct dn_pipe8 *p8 = (struct dn_pipe8 *)v;
	int i = p7->pipe_nr;

	sch->sched_nr = i;
	sch->oid.subtype = 0;
	p->link_nr = i;
	fs->fs_nr = i + 2*DN_MAX_ID;
	fs->sched_nr = i + DN_MAX_ID;

	/* Common to 7 and 8 */
	p->bandwidth = p7->bandwidth;
	p->delay = p7->delay;
	if (!is7) {
		/* FreeBSD 8 has burst  */
		p->burst = p8->burst;
	}

	/* fill the fifo flowset */
	dn_compat_config_queue(fs, v);
	fs->fs_nr = i + 2*DN_MAX_ID;
	fs->sched_nr = i + DN_MAX_ID;

	/* Move scheduler related parameter from fs to sch */
	sch->buckets = fs->buckets; /*XXX*/
	fs->buckets = 0;
	if (fs->flags & DN_HAVE_MASK) {
		sch->flags |= DN_HAVE_MASK;
		fs->flags &= ~DN_HAVE_MASK;
		sch->sched_mask = fs->flow_mask;
		bzero(&fs->flow_mask, sizeof(struct ipfw_flow_id));
	}

	return 0;
}

static int
dn_compat_config_profile(struct dn_profile *pf, struct dn_link *p,
			 void *v)
{
	struct dn_pipe8 *p8 = (struct dn_pipe8 *)v;

	p8->samples = &(((struct dn_pipe_max8 *)p8)->samples[0]);
	
	pf->link_nr = p->link_nr;
	pf->loss_level = p8->loss_level;
// 	pf->bandwidth = p->bandwidth; //XXX bandwidth redundant?
	pf->samples_no = p8->samples_no;
	strncpy(pf->name, p8->name,sizeof(pf->name));
	bcopy(p8->samples, pf->samples, sizeof(pf->samples));

	return 0;
}

/*
 * If p->pipe_nr != 0 the command is 'pipe x config', so need to create
 * the three main struct, else only a flowset is created
 */
static int
dn_compat_configure(void *v)
{
	struct dn_id *buf = NULL, *base;
	struct dn_sch *sch = NULL;
	struct dn_link *p = NULL;
	struct dn_fs *fs = NULL;
	struct dn_profile *pf = NULL;
	int lmax;
	int error;

	struct dn_pipe7 *p7 = (struct dn_pipe7 *)v;
	struct dn_pipe8 *p8 = (struct dn_pipe8 *)v;

	int i; /* number of object to configure */

	lmax = sizeof(struct dn_id);	/* command header */
	lmax += sizeof(struct dn_sch) + sizeof(struct dn_link) +
		sizeof(struct dn_fs) + sizeof(struct dn_profile);

	base = buf = malloc(lmax, M_DUMMYNET, M_WAITOK|M_ZERO);
	o_next(&buf, sizeof(struct dn_id), DN_CMD_CONFIG);
	base->id = DN_API_VERSION;

	/* pipe_nr is the same in p7 and p8 */
	i = p7->pipe_nr;
	if (i != 0) { /* pipe config */
		sch = o_next(&buf, sizeof(*sch), DN_SCH);
		p = o_next(&buf, sizeof(*p), DN_LINK);
		fs = o_next(&buf, sizeof(*fs), DN_FS);

		error = dn_compat_config_pipe(sch, p, fs, v);
		if (error) {
			free(buf, M_DUMMYNET);
			return error;
		}
		if (!is7 && p8->samples_no > 0) {
			/* Add profiles*/
			pf = o_next(&buf, sizeof(*pf), DN_PROFILE);
			error = dn_compat_config_profile(pf, p, v);
			if (error) {
				free(buf, M_DUMMYNET);
				return error;
			}
		}
	} else { /* queue config */
		fs = o_next(&buf, sizeof(*fs), DN_FS);
		error = dn_compat_config_queue(fs, v);
		if (error) {
			free(buf, M_DUMMYNET);
			return error;
		}
	}
	error = do_config(base, (char *)buf - (char *)base);

	if (buf)
		free(buf, M_DUMMYNET);
	return error;
}

int
dn_compat_calc_size(void)
{
	int need = 0;
	/* XXX use FreeBSD 8 struct size */
	/* NOTE:
	 * - half scheduler: 		schk_count/2
	 * - all flowset:		fsk_count
	 * - all flowset queues:	queue_count
	 * - all pipe queue:		si_count
	 */
	need += dn_cfg.schk_count * sizeof(struct dn_pipe8) / 2;
	need += dn_cfg.fsk_count * sizeof(struct dn_flow_set);
	need += dn_cfg.si_count * sizeof(struct dn_flow_queue8);
	need += dn_cfg.queue_count * sizeof(struct dn_flow_queue8);

	return need;
}

int
dn_c_copy_q (void *_ni, void *arg)
{
	struct copy_args *a = arg;
	struct dn_flow_queue7 *fq7 = (struct dn_flow_queue7 *)*a->start;
	struct dn_flow_queue8 *fq8 = (struct dn_flow_queue8 *)*a->start;
	struct dn_flow *ni = (struct dn_flow *)_ni;
	int size = 0;

	/* XXX hash slot not set */
	/* No difference between 7.2/8 */
	fq7->len = ni->length;
	fq7->len_bytes = ni->len_bytes;
	fq7->id = ni->fid;

	if (is7) {
		size = sizeof(struct dn_flow_queue7);
		fq7->tot_pkts = ni->tot_pkts;
		fq7->tot_bytes = ni->tot_bytes;
		fq7->drops = ni->drops;
	} else {
		size = sizeof(struct dn_flow_queue8);
		fq8->tot_pkts = ni->tot_pkts;
		fq8->tot_bytes = ni->tot_bytes;
		fq8->drops = ni->drops;
	}

	*a->start += size;
	return 0;
}

int
dn_c_copy_pipe(struct dn_schk *s, struct copy_args *a, int nq)
{
	struct dn_link *l = &s->link;
	struct dn_fsk *f = s->fs;

	struct dn_pipe7 *pipe7 = (struct dn_pipe7 *)*a->start;
	struct dn_pipe8 *pipe8 = (struct dn_pipe8 *)*a->start;
	struct dn_flow_set *fs;
	int size = 0;

	if (is7) {
		fs = &pipe7->fs;
		size = sizeof(struct dn_pipe7);
	} else {
		fs = &pipe8->fs;
		size = sizeof(struct dn_pipe8);
	}

	/* These 4 field are the same in pipe7 and pipe8 */
	pipe7->next.sle_next = (struct dn_pipe7 *)DN_IS_PIPE;
	pipe7->bandwidth = l->bandwidth;
	pipe7->delay = l->delay * 1000 / hz;
	pipe7->pipe_nr = l->link_nr - DN_MAX_ID;

	if (!is7) {
		if (s->profile) {
			struct dn_profile *pf = s->profile;
			strncpy(pipe8->name, pf->name, sizeof(pf->name));
			pipe8->loss_level = pf->loss_level;
			pipe8->samples_no = pf->samples_no;
		}
		pipe8->burst = div64(l->burst , 8 * hz);
	}

	fs->flow_mask = s->sch.sched_mask;
	fs->rq_size = s->sch.buckets ? s->sch.buckets : 1;

	fs->parent_nr = l->link_nr - DN_MAX_ID;
	fs->qsize = f->fs.qsize;
	fs->plr = f->fs.plr;
	fs->w_q = f->fs.w_q;
	fs->max_th = f->max_th;
	fs->min_th = f->min_th;
	fs->max_p = f->fs.max_p;
	fs->rq_elements = nq;

	fs->flags_fs = convertflags2old(f->fs.flags);

	*a->start += size;
	return 0;
}


int
dn_compat_copy_pipe(struct copy_args *a, void *_o)
{
	int have = a->end - *a->start;
	int need = 0;
	int pipe_size = sizeof(struct dn_pipe8);
	int queue_size = sizeof(struct dn_flow_queue8);
	int n_queue = 0; /* number of queues */

	struct dn_schk *s = (struct dn_schk *)_o;
	/* calculate needed space:
	 * - struct dn_pipe
	 * - if there are instances, dn_queue * n_instances
	 */
	n_queue = (s->sch.flags & DN_HAVE_MASK ? dn_ht_entries(s->siht) :
						(s->siht ? 1 : 0));
	need = pipe_size + queue_size * n_queue;
	if (have < need) {
		D("have %d < need %d", have, need);
		return 1;
	}
	/* copy pipe */
	dn_c_copy_pipe(s, a, n_queue);

	/* copy queues */
	if (s->sch.flags & DN_HAVE_MASK)
		dn_ht_scan(s->siht, dn_c_copy_q, a);
	else if (s->siht)
		dn_c_copy_q(s->siht, a);
	return 0;
}

int
dn_c_copy_fs(struct dn_fsk *f, struct copy_args *a, int nq)
{
	struct dn_flow_set *fs = (struct dn_flow_set *)*a->start;

	fs->next.sle_next = (struct dn_flow_set *)DN_IS_QUEUE;
	fs->fs_nr = f->fs.fs_nr;
	fs->qsize = f->fs.qsize;
	fs->plr = f->fs.plr;
	fs->w_q = f->fs.w_q;
	fs->max_th = f->max_th;
	fs->min_th = f->min_th;
	fs->max_p = f->fs.max_p;
	fs->flow_mask = f->fs.flow_mask;
	fs->rq_elements = nq;
	fs->rq_size = (f->fs.buckets ? f->fs.buckets : 1);
	fs->parent_nr = f->fs.sched_nr;
	fs->weight = f->fs.par[0];

	fs->flags_fs = convertflags2old(f->fs.flags);
	*a->start += sizeof(struct dn_flow_set);
	return 0;
}

int
dn_compat_copy_queue(struct copy_args *a, void *_o)
{
	int have = a->end - *a->start;
	int need = 0;
	int fs_size = sizeof(struct dn_flow_set);
	int queue_size = sizeof(struct dn_flow_queue8);

	struct dn_fsk *fs = (struct dn_fsk *)_o;
	int n_queue = 0; /* number of queues */

	n_queue = (fs->fs.flags & DN_HAVE_MASK ? dn_ht_entries(fs->qht) :
						(fs->qht ? 1 : 0));

	need = fs_size + queue_size * n_queue;
	if (have < need) {
		D("have < need");
		return 1;
	}

	/* copy flowset */
	dn_c_copy_fs(fs, a, n_queue);

	/* copy queues */
	if (fs->fs.flags & DN_HAVE_MASK)
		dn_ht_scan(fs->qht, dn_c_copy_q, a);
	else if (fs->qht)
		dn_c_copy_q(fs->qht, a);

	return 0;
}

int
copy_data_helper_compat(void *_o, void *_arg)
{
	struct copy_args *a = _arg;

	if (a->type == DN_COMPAT_PIPE) {
		struct dn_schk *s = _o;
		if (s->sch.oid.subtype != 1 || s->sch.sched_nr <= DN_MAX_ID) {
			return 0;	/* not old type */
		}
		/* copy pipe parameters, and if instance exists, copy
		 * other parameters and eventually queues.
		 */
		if(dn_compat_copy_pipe(a, _o))
			return DNHT_SCAN_END;
	} else if (a->type == DN_COMPAT_QUEUE) {
		struct dn_fsk *fs = _o;
		if (fs->fs.fs_nr >= DN_MAX_ID)
			return 0;
		if (dn_compat_copy_queue(a, _o))
			return DNHT_SCAN_END;
	}
	return 0;
}

/* Main function to manage old requests */
int
ip_dummynet_compat(struct sockopt *sopt)
{
	int error=0;
	void *v = NULL;
	struct dn_id oid;

	/* Length of data, used to found ipfw version... */
	int len = sopt->sopt_valsize;

	/* len can be 0 if command was dummynet_flush */
	if (len == pipesize7) {
		D("setting compatibility with FreeBSD 7.2");
		is7 = 1;
	}
	else if (len == pipesize8 || len == pipesizemax8) {
		D("setting compatibility with FreeBSD 8");
		is7 = 0;
	}

	switch (sopt->sopt_name) {
	default:
		printf("dummynet: -- unknown option %d", sopt->sopt_name);
		error = EINVAL;
		break;

	case IP_DUMMYNET_FLUSH:
		oid_fill(&oid, sizeof(oid), DN_CMD_FLUSH, DN_API_VERSION);
		do_config(&oid, oid.len);
		break;

	case IP_DUMMYNET_DEL:
		v = malloc(len, M_TEMP, M_WAITOK);
		error = sooptcopyin(sopt, v, len, len);
		if (error)
			break;
		error = dn_compat_del(v);
		free(v, M_TEMP);
		break;

	case IP_DUMMYNET_CONFIGURE:
		v = malloc(len, M_TEMP, M_WAITOK);
		error = sooptcopyin(sopt, v, len, len);
		if (error)
			break;
		error = dn_compat_configure(v);
		free(v, M_TEMP);
		break;

	case IP_DUMMYNET_GET: {
		void *buf;
		int ret;
		int original_size = sopt->sopt_valsize;
		int size;

		ret = dummynet_get(sopt, &buf);
		if (ret)
			return 0;//XXX ?
		size = sopt->sopt_valsize;
		sopt->sopt_valsize = original_size;
		D("size=%d, buf=%p", size, buf);
		ret = sooptcopyout(sopt, buf, size);
		if (ret)
			printf("  %s ERROR sooptcopyout\n", __FUNCTION__);
		if (buf)
			free(buf, M_DUMMYNET);
	    }
	}

	return error;
}


