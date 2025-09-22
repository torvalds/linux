/* $OpenBSD: fq_codel.c,v 1.17 2025/07/07 02:28:50 jsg Exp $ */

/*
 * Copyright (c) 2017 Mike Belopuhov
 *
 * Permission to use, copy, modify, and distribute this software for any
 * purpose with or without fee is hereby granted, provided that the above
 * copyright notice and this permission notice appear in all copies.
 *
 * THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
 * WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF
 * MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR
 * ANY SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
 * WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN
 * ACTION OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF
 * OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
 */

/*
 * Codel - The Controlled-Delay Active Queue Management algorithm
 * IETF draft-ietf-aqm-codel-07
 *
 * Based on the algorithm by Kathleen Nichols and Van Jacobson with
 * improvements from Dave Taht and Eric Dumazet.
 */

/*
 * The FlowQueue-CoDel Packet Scheduler and Active Queue Management
 * IETF draft-ietf-aqm-fq-codel-06
 *
 * Based on the implementation by Rasool Al-Saadi, Centre for Advanced
 * Internet Architectures, Swinburne University of Technology, Melbourne,
 * Australia.
 */

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/mbuf.h>
#include <sys/queue.h>

#include <net/if.h>
#include <net/if_var.h>
#include <net/pfvar.h>
#include <net/fq_codel.h>

/* #define FQCODEL_DEBUG 1 */

#ifdef FQCODEL_DEBUG
#define DPRINTF(x...)		printf(x)
#else
#define DPRINTF(x...)
#endif

struct codel {
	struct mbuf_list q;

	unsigned int	 dropping:1;	/* Dropping state */
	unsigned int	 backlog:31;	/* Number of bytes in the queue */

	unsigned short	 drops;		/* Free running counter of drops */
	unsigned short	 ldrops;	/* Value from the previous run */

	int64_t		 start;		/* The moment queue was above target */
	int64_t		 next;		/* Next interval */
	int64_t		 delay;		/* Delay incurred by the last packet */
};

struct codel_params {
	int64_t		 target;
	int64_t		 interval;
	int		 quantum;

	uint32_t	*intervals;
};

void		 codel_initparams(struct codel_params *, unsigned int,
		    unsigned int, int);
void		 codel_freeparams(struct codel_params *);
void		 codel_enqueue(struct codel *, int64_t, struct mbuf *);
struct mbuf	*codel_dequeue(struct codel *, struct codel_params *, int64_t,
		    struct mbuf_list *, uint64_t *, uint64_t *);
struct mbuf	*codel_commit(struct codel *, struct mbuf *);
void		 codel_purge(struct codel *, struct mbuf_list *ml);

struct flow {
	struct codel		 cd;
	int			 active:1;
	int			 deficit:31;
#ifdef FQCODEL_DEBUG
	uint16_t		 id;
#endif
	SIMPLEQ_ENTRY(flow)	 flowentry;
};
SIMPLEQ_HEAD(flowq, flow);

struct fqcodel {
	struct flowq		 newq;
	struct flowq		 oldq;

	struct flow		*flows;
	unsigned int		 qlength;

	struct ifnet		*ifp;

	struct codel_params	 cparams;

	unsigned int		 nflows;
	unsigned int		 qlimit;
	int			 quantum;

	unsigned int		 flags;
#define FQCF_FIXED_QUANTUM	  0x1

	/* stats */
	struct fqcodel_pktcntr   xmit_cnt;
	struct fqcodel_pktcntr 	 drop_cnt;
};

unsigned int	 fqcodel_idx(unsigned int, const struct mbuf *);
void		*fqcodel_alloc(unsigned int, void *);
void		 fqcodel_free(unsigned int, void *);
struct mbuf	*fqcodel_if_enq(struct ifqueue *, struct mbuf *);
struct mbuf	*fqcodel_if_deq_begin(struct ifqueue *, void **);
void		 fqcodel_if_deq_commit(struct ifqueue *, struct mbuf *, void *);
void		 fqcodel_if_purge(struct ifqueue *, struct mbuf_list *);

struct mbuf	*fqcodel_enq(struct fqcodel *, struct mbuf *);
struct mbuf	*fqcodel_deq_begin(struct fqcodel *, void **,
		    struct mbuf_list *);
void		 fqcodel_deq_commit(struct fqcodel *, struct mbuf *, void *);
void		 fqcodel_purge(struct fqcodel *, struct mbuf_list *);

/*
 * ifqueue glue.
 */

static const struct ifq_ops fqcodel_ops = {
	fqcodel_idx,
	fqcodel_if_enq,
	fqcodel_if_deq_begin,
	fqcodel_if_deq_commit,
	fqcodel_if_purge,
	fqcodel_alloc,
	fqcodel_free
};

const struct ifq_ops * const ifq_fqcodel_ops = &fqcodel_ops;

void		*fqcodel_pf_alloc(struct ifnet *);
int		 fqcodel_pf_addqueue(void *, struct pf_queuespec *);
void		 fqcodel_pf_free(void *);
int		 fqcodel_pf_qstats(struct pf_queuespec *, void *, int *);
unsigned int	 fqcodel_pf_qlength(void *);
struct mbuf *	 fqcodel_pf_enqueue(void *, struct mbuf *);
struct mbuf *	 fqcodel_pf_deq_begin(void *, void **, struct mbuf_list *);
void		 fqcodel_pf_deq_commit(void *, struct mbuf *, void *);
void		 fqcodel_pf_purge(void *, struct mbuf_list *);

/*
 * pf queue glue.
 */

static const struct pfq_ops fqcodel_pf_ops = {
	fqcodel_pf_alloc,
	fqcodel_pf_addqueue,
	fqcodel_pf_free,
	fqcodel_pf_qstats,
	fqcodel_pf_qlength,
	fqcodel_pf_enqueue,
	fqcodel_pf_deq_begin,
	fqcodel_pf_deq_commit,
	fqcodel_pf_purge
};

const struct pfq_ops * const pfq_fqcodel_ops = &fqcodel_pf_ops;

/* Default aggregate queue depth */
static const unsigned int fqcodel_qlimit = 1024;

/*
 * CoDel implementation
 */

/* Delay target, 5ms */
static const int64_t codel_target = 5000000;

/* Grace period after last drop, 16 100ms intervals */
static const int64_t codel_grace = 1600000000;

/* First 399 "100 / sqrt(x)" intervals, ns precision */
static const uint32_t codel_intervals[] = {
	100000000, 70710678, 57735027, 50000000, 44721360, 40824829, 37796447,
	35355339,  33333333, 31622777, 30151134, 28867513, 27735010, 26726124,
	25819889,  25000000, 24253563, 23570226, 22941573, 22360680, 21821789,
	21320072,  20851441, 20412415, 20000000, 19611614, 19245009, 18898224,
	18569534,  18257419, 17960530, 17677670, 17407766, 17149859, 16903085,
	16666667,  16439899, 16222142, 16012815, 15811388, 15617376, 15430335,
	15249857,  15075567, 14907120, 14744196, 14586499, 14433757, 14285714,
	14142136,  14002801, 13867505, 13736056, 13608276, 13483997, 13363062,
	13245324,  13130643, 13018891, 12909944, 12803688, 12700013, 12598816,
	12500000,  12403473, 12309149, 12216944, 12126781, 12038585, 11952286,
	11867817,  11785113, 11704115, 11624764, 11547005, 11470787, 11396058,
	11322770,  11250879, 11180340, 11111111, 11043153, 10976426, 10910895,
	10846523,  10783277, 10721125, 10660036, 10599979, 10540926, 10482848,
	10425721,  10369517, 10314212, 10259784, 10206207, 10153462, 10101525,
	10050378,  10000000, 9950372,  9901475,  9853293,  9805807,  9759001,
	9712859,   9667365,  9622504,  9578263,  9534626,  9491580,  9449112,
	9407209,   9365858,  9325048,  9284767,  9245003,  9205746,  9166985,
	9128709,   9090909,  9053575,  9016696,  8980265,  8944272,  8908708,
	8873565,   8838835,  8804509,  8770580,  8737041,  8703883,  8671100,
	8638684,   8606630,  8574929,  8543577,  8512565,  8481889,  8451543,
	8421519,   8391814,  8362420,  8333333,  8304548,  8276059,  8247861,
	8219949,   8192319,  8164966,  8137885,  8111071,  8084521,  8058230,
	8032193,   8006408,  7980869,  7955573,  7930516,  7905694,  7881104,
	7856742,   7832604,  7808688,  7784989,  7761505,  7738232,  7715167,
	7692308,   7669650,  7647191,  7624929,  7602859,  7580980,  7559289,
	7537784,   7516460,  7495317,  7474351,  7453560,  7432941,  7412493,
	7392213,   7372098,  7352146,  7332356,  7312724,  7293250,  7273930,
	7254763,   7235746,  7216878,  7198158,  7179582,  7161149,  7142857,
	7124705,   7106691,  7088812,  7071068,  7053456,  7035975,  7018624,
	7001400,   6984303,  6967330,  6950480,  6933752,  6917145,  6900656,
	6884284,   6868028,  6851887,  6835859,  6819943,  6804138,  6788442,
	6772855,   6757374,  6741999,  6726728,  6711561,  6696495,  6681531,
	6666667,   6651901,  6637233,  6622662,  6608186,  6593805,  6579517,
	6565322,   6551218,  6537205,  6523281,  6509446,  6495698,  6482037,
	6468462,   6454972,  6441566,  6428243,  6415003,  6401844,  6388766,
	6375767,   6362848,  6350006,  6337243,  6324555,  6311944,  6299408,
	6286946,   6274558,  6262243,  6250000,  6237829,  6225728,  6213698,
	6201737,   6189845,  6178021,  6166264,  6154575,  6142951,  6131393,
	6119901,   6108472,  6097108,  6085806,  6074567,  6063391,  6052275,
	6041221,   6030227,  6019293,  6008418,  5997601,  5986843,  5976143,
	5965500,   5954913,  5944383,  5933908,  5923489,  5913124,  5902813,
	5892557,   5882353,  5872202,  5862104,  5852057,  5842062,  5832118,
	5822225,   5812382,  5802589,  5792844,  5783149,  5773503,  5763904,
	5754353,   5744850,  5735393,  5725983,  5716620,  5707301,  5698029,
	5688801,   5679618,  5670480,  5661385,  5652334,  5643326,  5634362,
	5625440,   5616560,  5607722,  5598925,  5590170,  5581456,  5572782,
	5564149,   5555556,  5547002,  5538488,  5530013,  5521576,  5513178,
	5504819,   5496497,  5488213,  5479966,  5471757,  5463584,  5455447,
	5447347,   5439283,  5431254,  5423261,  5415304,  5407381,  5399492,
	5391639,   5383819,  5376033,  5368281,  5360563,  5352877,  5345225,
	5337605,   5330018,  5322463,  5314940,  5307449,  5299989,  5292561,
	5285164,   5277798,  5270463,  5263158,  5255883,  5248639,  5241424,
	5234239,   5227084,  5219958,  5212860,  5205792,  5198752,  5191741,
	5184758,   5177804,  5170877,  5163978,  5157106,  5150262,  5143445,
	5136655,   5129892,  5123155,  5116445,  5109761,  5103104,  5096472,
	5089866,   5083286,  5076731,  5070201,  5063697,  5057217,  5050763,
	5044333,   5037927,  5031546,  5025189,  5018856,  5012547,  5006262
};

void
codel_initparams(struct codel_params *cp, unsigned int target,
    unsigned int interval, int quantum)
{
	uint64_t mult;
	unsigned int i;

	/*
	 * Update observation intervals table according to the configured
	 * initial interval value.
	 */
	if (interval > codel_intervals[0]) {
		/* Select either specified target or 5% of an interval */
		cp->target = MAX(target, interval / 5);
		cp->interval = interval;

		/* The coefficient is scaled up by a 1000 */
		mult = ((uint64_t)cp->interval * 1000) / codel_intervals[0];

		/* Prepare table of intervals */
		cp->intervals = mallocarray(nitems(codel_intervals),
		    sizeof(codel_intervals[0]), M_DEVBUF, M_WAITOK | M_ZERO);
		for (i = 0; i < nitems(codel_intervals); i++)
			cp->intervals[i] = ((uint64_t)codel_intervals[i] *
			    mult) / 1000;
	} else {
		cp->target = MAX(target, codel_target);
		cp->interval = codel_intervals[0];
		cp->intervals = (uint32_t *)codel_intervals;
	}

	cp->quantum = quantum;
}

void
codel_freeparams(struct codel_params *cp)
{
	if (cp->intervals != codel_intervals)
		free(cp->intervals, M_DEVBUF, nitems(codel_intervals) *
		    sizeof(codel_intervals[0]));
	cp->intervals = NULL;
}

static inline unsigned int
codel_backlog(struct codel *cd)
{
	return (cd->backlog);
}

static inline unsigned int
codel_qlength(struct codel *cd)
{
	return (ml_len(&cd->q));
}

static inline int64_t
codel_delay(struct codel *cd)
{
	return (cd->delay);
}

void
codel_enqueue(struct codel *cd, int64_t now, struct mbuf *m)
{
	m->m_pkthdr.ph_timestamp = now;

	ml_enqueue(&cd->q, m);
	cd->backlog += m->m_pkthdr.len;
}

/*
 * Select the next interval according to the number of drops
 * in the current one relative to the provided timestamp.
 */
static inline void
control_law(struct codel *cd, struct codel_params *cp, int64_t rts)
{
	unsigned int idx;

	idx = min(cd->drops, nitems(codel_intervals) - 1);
	cd->next = rts + cp->intervals[idx];
}

/*
 * Pick the next enqueued packet and determine the queueing delay
 * as well as whether or not it's a good candidate for dropping
 * from the queue.
 *
 * The decision whether to drop the packet or not is made based
 * on the queueing delay target of 5ms and on the current queue
 * length in bytes which shouldn't be less than the amount of data
 * that arrives in a typical interarrival time (MTU-sized packets
 * arriving spaced by the amount of time it takes to send such a
 * packet on the bottleneck).
 */
static inline struct mbuf *
codel_next_packet(struct codel *cd, struct codel_params *cp, int64_t now,
    int *drop)
{
	struct mbuf *m;

	*drop = 0;

	m = MBUF_LIST_FIRST(&cd->q);
	if (m == NULL) {
		KASSERT(cd->backlog == 0);
		/* Empty queue, reset interval */
		cd->start = 0;
		return (NULL);
	}

	if (now - m->m_pkthdr.ph_timestamp < cp->target ||
	    cd->backlog <= cp->quantum) {
		/*
		 * The minimum delay decreased below the target, reset
		 * the current observation interval.
		 */
		cd->start = 0;
		return (m);
	}

	if (cd->start == 0) {
		/*
		 * This is the first packet to be delayed for more than
		 * the target, start the first observation interval after
		 * a single RTT and see if the minimum delay goes below
		 * the target within the interval, otherwise punish the
		 * next packet.
		 */
		cd->start = now + cp->interval;
	} else if (now > cd->start) {
		*drop = 1;
	}
	return (m);
}

enum { INITIAL, ACCEPTING, FIRSTDROP, DROPPING, CONTROL, RECOVERY };

static inline int
codel_state_change(struct codel *cd, int64_t now, struct mbuf *m, int drop,
    int state)
{
	if (state == FIRSTDROP)
		return (ACCEPTING);

	if (cd->dropping) {
		if (!drop)
			return (RECOVERY);
		else if (now >= cd->next)
			return (state == DROPPING ? CONTROL : DROPPING);
	} else if (drop)
		return (FIRSTDROP);

	if (m == NULL)
		return (RECOVERY);

	return (ACCEPTING);
}

struct mbuf *
codel_dequeue(struct codel *cd, struct codel_params *cp, int64_t now,
    struct mbuf_list *free_ml, uint64_t *dpkts, uint64_t *dbytes)
{
	struct mbuf *m;
	unsigned short delta;
	int drop, state, done = 0;

	state = INITIAL;

	while (!done) {
		m = codel_next_packet(cd, cp, now, &drop);
		state = codel_state_change(cd, now, m, drop, state);

		switch (state) {
		case FIRSTDROP:
			m = codel_commit(cd, m);
			ml_enqueue(free_ml, m);

			*dpkts += 1;
			*dbytes += m->m_pkthdr.len;

			cd->dropping = 1;

			/*
			 * If we're still within the grace period and not
			 * meeting our minimal delay target we treat this
			 * as a continuation of the previous observation
			 * interval and shrink it further.  Otherwise we
			 * start from the initial one.
			 */
			delta = cd->drops - cd->ldrops;
			if (delta > 1) {
				if (now < cd->next ||
				    now - cd->next < codel_grace)
					cd->drops = delta;
				else
					cd->drops = 1;
			} else
				cd->drops = 1;
			control_law(cd, cp, now);
			cd->ldrops = cd->drops;

			/* fetches the next packet and goes to ACCEPTING */
			break;
		case DROPPING:
			m = codel_commit(cd, m);
			ml_enqueue(free_ml, m);
			cd->drops++;

			*dpkts += 1;
			*dbytes += m->m_pkthdr.len;

			/* fetches the next packet and goes to CONTROL */
			break;
		case CONTROL:
			if (drop) {
				control_law(cd, cp, cd->next);
				continue;
			}
			/* FALLTHROUGH */
		case RECOVERY:
			cd->dropping = 0;
			/* FALLTHROUGH */
		case ACCEPTING:
			done = 1;
			break;
		}
	}

	if (m != NULL)
		cd->delay = now - m->m_pkthdr.ph_timestamp;

	return (m);
}

struct mbuf *
codel_commit(struct codel *cd, struct mbuf *m)
{
	struct mbuf *n;

	n = ml_dequeue(&cd->q);
	if (m)
		KASSERT(n == m);
	KASSERT(n != NULL);
	KASSERT(cd->backlog >= n->m_pkthdr.len);
	cd->backlog -= n->m_pkthdr.len;
	return (n);
}

void
codel_purge(struct codel *cd, struct mbuf_list *ml)
{
	ml_enlist(ml, &cd->q);
	cd->backlog = 0;
}

/*
 * FQ-CoDel implementation
 */

static inline struct flow *
classify_flow(struct fqcodel *fqc, struct mbuf *m)
{
	unsigned int index = 0;

	if (m->m_pkthdr.csum_flags & M_FLOWID)
		index = m->m_pkthdr.ph_flowid % fqc->nflows;

	DPRINTF("%s: %u\n", __func__, index);

	return (&fqc->flows[index]);
}

struct mbuf *
fqcodel_enq(struct fqcodel *fqc, struct mbuf *m)
{
	struct flow *flow;
	unsigned int backlog = 0;
	int64_t now;
	int i;

	flow = classify_flow(fqc, m);
	if (flow == NULL)
		return (m);

	now = nsecuptime();
	codel_enqueue(&flow->cd, now, m);
	fqc->qlength++;

	if (!flow->active) {
		SIMPLEQ_INSERT_TAIL(&fqc->newq, flow, flowentry);
		flow->deficit = fqc->quantum;
		flow->active = 1;
		DPRINTF("%s: flow %u active deficit %d\n", __func__,
		    flow->id, flow->deficit);
	}

	/*
	 * Check the limit for all queues and remove a packet
	 * from the longest one.
	 */
	if (fqc->qlength >= fqcodel_qlimit) {
		for (i = 0; i < fqc->nflows; i++) {
			if (codel_backlog(&fqc->flows[i].cd) > backlog) {
				flow = &fqc->flows[i];
				backlog = codel_backlog(&flow->cd);
			}
		}

		KASSERT(flow != NULL);
		m = codel_commit(&flow->cd, NULL);

		fqc->drop_cnt.packets++;
		fqc->drop_cnt.bytes += m->m_pkthdr.len;

		fqc->qlength--;

		DPRINTF("%s: dropping from flow %u\n", __func__,
		    flow->id);
		return (m);
	}

	return (NULL);
}

static inline struct flowq *
select_queue(struct fqcodel *fqc)
{
	struct flowq *fq = NULL;

	if (!SIMPLEQ_EMPTY(&fqc->newq))
		fq = &fqc->newq;
	else if (!SIMPLEQ_EMPTY(&fqc->oldq))
		fq = &fqc->oldq;
	return (fq);
}

static inline struct flow *
first_flow(struct fqcodel *fqc, struct flowq **fq)
{
	struct flow *flow;

	while ((*fq = select_queue(fqc)) != NULL) {
		while ((flow = SIMPLEQ_FIRST(*fq)) != NULL) {
			if (flow->deficit <= 0) {
				flow->deficit += fqc->quantum;
				SIMPLEQ_REMOVE_HEAD(*fq, flowentry);
				SIMPLEQ_INSERT_TAIL(&fqc->oldq, flow,
				    flowentry);
				DPRINTF("%s: flow %u deficit %d\n", __func__,
				    flow->id, flow->deficit);
			} else
				return (flow);
		}
	}

	return (NULL);
}

static inline struct flow *
next_flow(struct fqcodel *fqc, struct flow *flow, struct flowq **fq)
{
	SIMPLEQ_REMOVE_HEAD(*fq, flowentry);

	if (*fq == &fqc->newq && !SIMPLEQ_EMPTY(&fqc->oldq)) {
		/* A packet was dropped, starve the queue */
		SIMPLEQ_INSERT_TAIL(&fqc->oldq, flow, flowentry);
		DPRINTF("%s: flow %u ->oldq deficit %d\n", __func__,
		    flow->id, flow->deficit);
	} else {
		/* A packet was dropped on a starved queue, disable it */
		flow->active = 0;
		DPRINTF("%s: flow %u inactive deficit %d\n", __func__,
		    flow->id, flow->deficit);
	}

	return (first_flow(fqc, fq));
}

struct mbuf *
fqcodel_deq_begin(struct fqcodel *fqc, void **cookiep,
    struct mbuf_list *free_ml)
{
	struct mbuf_list ml = MBUF_LIST_INITIALIZER();
	struct flowq *fq;
	struct flow *flow;
	struct mbuf *m;
	int64_t now;

	if ((fqc->flags & FQCF_FIXED_QUANTUM) == 0)
		fqc->quantum = fqc->ifp->if_mtu + max_linkhdr;

	now = nsecuptime();

	for (flow = first_flow(fqc, &fq); flow != NULL;
	     flow = next_flow(fqc, flow, &fq)) {
		m = codel_dequeue(&flow->cd, &fqc->cparams, now, &ml,
		    &fqc->drop_cnt.packets, &fqc->drop_cnt.bytes);

		KASSERT(fqc->qlength >= ml_len(&ml));
		fqc->qlength -= ml_len(&ml);

		ml_enlist(free_ml, &ml);

		if (m != NULL) {
			flow->deficit -= m->m_pkthdr.len;
			DPRINTF("%s: flow %u deficit %d\n", __func__,
			    flow->id, flow->deficit);
			*cookiep = flow;
			return (m);
		}
	}

	return (NULL);
}

void
fqcodel_deq_commit(struct fqcodel *fqc, struct mbuf *m, void *cookie)
{
	struct flow *flow = cookie;

	KASSERT(fqc->qlength > 0);
	fqc->qlength--;

	fqc->xmit_cnt.packets++;
	fqc->xmit_cnt.bytes += m->m_pkthdr.len;

	(void)codel_commit(&flow->cd, m);
}

void
fqcodel_purge(struct fqcodel *fqc, struct mbuf_list *ml)
{
	unsigned int i;

	for (i = 0; i < fqc->nflows; i++)
		codel_purge(&fqc->flows[i].cd, ml);
	fqc->qlength = 0;
}

struct mbuf *
fqcodel_if_enq(struct ifqueue *ifq, struct mbuf *m)
{
	return fqcodel_enq(ifq->ifq_q, m);
}

struct mbuf *
fqcodel_if_deq_begin(struct ifqueue *ifq, void **cookiep)
{
	struct mbuf_list free_ml = MBUF_LIST_INITIALIZER();
	struct mbuf *m;

	m = fqcodel_deq_begin(ifq->ifq_q, cookiep, &free_ml);
	ifq_mfreeml(ifq, &free_ml);
	return (m);
}

void
fqcodel_if_deq_commit(struct ifqueue *ifq, struct mbuf *m, void *cookie)
{
	return fqcodel_deq_commit(ifq->ifq_q, m, cookie);
}

void
fqcodel_if_purge(struct ifqueue *ifq, struct mbuf_list *ml)
{
	return fqcodel_purge(ifq->ifq_q, ml);
}

void *
fqcodel_pf_alloc(struct ifnet *ifp)
{
	struct fqcodel *fqc;

	fqc = malloc(sizeof(struct fqcodel), M_DEVBUF, M_WAITOK | M_ZERO);

	SIMPLEQ_INIT(&fqc->newq);
	SIMPLEQ_INIT(&fqc->oldq);

	return (fqc);
}

int
fqcodel_pf_addqueue(void *arg, struct pf_queuespec *qs)
{
	struct ifnet *ifp = qs->kif->pfik_ifp;
	struct fqcodel *fqc = arg;

	if (qs->flowqueue.flows == 0 || qs->flowqueue.flows > 0xffff)
		return (EINVAL);

	fqc->nflows = qs->flowqueue.flows;
	fqc->quantum = qs->flowqueue.quantum;
	if (qs->qlimit > 0)
		fqc->qlimit = qs->qlimit;
	else
		fqc->qlimit = fqcodel_qlimit;
	if (fqc->quantum > 0)
		fqc->flags |= FQCF_FIXED_QUANTUM;
	else
		fqc->quantum = ifp->if_mtu + max_linkhdr;

	codel_initparams(&fqc->cparams, qs->flowqueue.target,
	    qs->flowqueue.interval, fqc->quantum);

	fqc->flows = mallocarray(fqc->nflows, sizeof(struct flow),
	    M_DEVBUF, M_WAITOK | M_ZERO);

#ifdef FQCODEL_DEBUG
	{
		unsigned int i;

		for (i = 0; i < fqc->nflows; i++)
			fqc->flows[i].id = i;
	}
#endif

	fqc->ifp = ifp;

	DPRINTF("fq-codel on %s: %d queues %d deep, quantum %d target %llums "
	    "interval %llums\n", ifp->if_xname, fqc->nflows, fqc->qlimit,
	    fqc->quantum, fqc->cparams.target / 1000000,
	    fqc->cparams.interval / 1000000);

	return (0);
}

void
fqcodel_pf_free(void *arg)
{
	struct fqcodel *fqc = arg;

	codel_freeparams(&fqc->cparams);
	free(fqc->flows, M_DEVBUF, fqc->nflows * sizeof(struct flow));
	free(fqc, M_DEVBUF, sizeof(struct fqcodel));
}

int
fqcodel_pf_qstats(struct pf_queuespec *qs, void *ubuf, int *nbytes)
{
	struct ifnet *ifp = qs->kif->pfik_ifp;
	struct fqcodel_stats stats;
	struct fqcodel *fqc;
	int64_t delay;
	unsigned int i;
	int error = 0;

	if (ifp == NULL)
		return (EBADF);

	if (*nbytes < sizeof(stats))
		return (EINVAL);

	memset(&stats, 0, sizeof(stats));

	/* XXX: multi-q? */
	fqc = ifq_q_enter(&ifp->if_snd, ifq_fqcodel_ops);
	if (fqc == NULL)
		return (EBADF);

	stats.xmit_cnt = fqc->xmit_cnt;
	stats.drop_cnt = fqc->drop_cnt;

	stats.qlength = ifq_len(&ifp->if_snd);
	stats.qlimit = fqc->qlimit;

	stats.flows = 0;
	stats.delaysum = stats.delaysumsq = 0;

	for (i = 0; i < fqc->nflows; i++) {
		if (codel_qlength(&fqc->flows[i].cd) == 0)
			continue;
		/* Scale down to microseconds to avoid overflows */
		delay = codel_delay(&fqc->flows[i].cd) / 1000;
		stats.delaysum += delay;
		stats.delaysumsq += delay * delay;
		stats.flows++;
	}

	ifq_q_leave(&ifp->if_snd, fqc);

	if ((error = copyout((caddr_t)&stats, ubuf, sizeof(stats))) != 0)
		return (error);

	*nbytes = sizeof(stats);
	return (0);
}

unsigned int
fqcodel_pf_qlength(void *fqc)
{
	return ((struct fqcodel *)fqc)->qlength;
}

struct mbuf *
fqcodel_pf_enqueue(void *fqc, struct mbuf *m)
{
	return fqcodel_enq(fqc, m);
}

struct mbuf *
fqcodel_pf_deq_begin(void *fqc, void **cookiep, struct mbuf_list *free_ml)
{
	return fqcodel_deq_begin(fqc, cookiep, free_ml);
}

void
fqcodel_pf_deq_commit(void *fqc, struct mbuf *m, void *cookie)
{
	return fqcodel_deq_commit(fqc, m, cookie);
}

void
fqcodel_pf_purge(void *fqc, struct mbuf_list *ml)
{
	return fqcodel_purge(fqc, ml);
}

unsigned int
fqcodel_idx(unsigned int nqueues, const struct mbuf *m)
{
	return (0);
}

void *
fqcodel_alloc(unsigned int idx, void *arg)
{
	/* Allocation is done in fqcodel_pf_alloc */
	return (arg);
}

void
fqcodel_free(unsigned int idx, void *arg)
{
	/* nothing to do here */
}
