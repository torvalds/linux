/*
 * $FreeBSD$
 *
 * Testing program for schedulers
 *
 * The framework include a simple controller which, at each
 * iteration, decides whether we can enqueue and/or dequeue.
 * Then the mainloop runs the required number of tests,
 * keeping track of statistics.
 */

// #define USE_BURST	// what is this for ?

#include "dn_test.h"

struct cfg_s {
	int ac;
	char * const *av;

	const char *name;
	int loops;
	struct timeval time;

	/* running counters */
	uint32_t	_enqueue;
	uint32_t	drop;
	uint32_t	pending;
	uint32_t	dequeue;

	/* generator parameters */
	int32_t th_min, th_max;	/* thresholds for hysteresis; negative means per flow */
#ifdef USE_BURST
	int maxburst;
#endif /* USE_BURST */
	int lmin, lmax;	/* packet len */
	int flows;	/* number of flows */
	int flowsets;	/* number of flowsets */
	int wsum;	/* sum of weights of all flows */
#ifdef USE_CUR
	int max_y;	/* max random number in the generation */
	int cur_y
	int cur_fs;	/* used in generation, between 0 and max_y - 1 */
#endif /* USE_CUR */
	const char *fs_config; /* flowset config */
	int can_dequeue;
	int burst;	/* count of packets sent in a burst */
	struct mbuf *tosend;	/* packet to send -- also flag to enqueue */

	struct mbuf *freelist;

	struct mbuf *head, *tail;	/* a simple tailq */

	/* scheduler hooks */
	int (*enq)(struct dn_sch_inst *, struct dn_queue *,
		struct mbuf *);
	struct mbuf * (*deq)(struct dn_sch_inst *);
	/* size of the three fields including sched-specific areas */
	uint32_t schk_len;
	uint32_t q_len; /* size of a queue including sched-fields */
	uint32_t si_len; /* size of a sch_inst including sched-fields */
	char *q;	/* array of flow queues */
		/* use a char* because size is variable */
	/*
	 * The scheduler template (one) followd by schk_datalen bytes
	 * for scheduler-specific parameters, total size is schk_len
	 */
	struct dn_schk *sched;
	/*
	 * one scheduler instance, followed by si_datalen bytes
	 * for scheduler specific parameters of this instance,
	 * total size is si_len. si->sched points to sched
	 */
	struct dn_sch_inst *si;
	struct dn_fsk *fs;	/* array of flowsets */

	/* generator state */
	int state;	/* 0 = going up (enqueue), 1: going down (dequeue) */

	/*
	 * We keep lists for each backlog level, and always serve
	 * the one with shortest backlog. llmask contains a bitmap
	 * of lists, and ll are the heads of the lists. The last
	 * entry (BACKLOG) contains all entries considered 'full'
	 * XXX to optimize things, entry i could contain queues with
	 * 2^{i-1}+1 .. 2^i entries.
	 */
#define BACKLOG	30 /* this many backlogged classes, we only need BACKLOG+1 */
	uint64_t	llmask;
	struct list_head ll[BACKLOG + 10];

	double *q_wfi;	/* (byte) Worst-case Fair Index of the flows  */
	double wfi;	/* (byte) Worst-case Fair Index of the system */
};

/* FI2Q and Q2FI converts from flow_id (i.e. queue index)
 * to dn_queue and back. We cannot simply use pointer arithmetic
 * because the queu has variable size, q_len
 */
#define FI2Q(c, i)	((struct dn_queue *)((c)->q + (c)->q_len * (i)))
#define Q2FI(c, q)	(((char *)(q) - (c)->q)/(c)->q_len)

int debug = 0;

struct dn_parms dn_cfg;

static void controller(struct cfg_s *c);

/* release a packet for a given flow_id.
 * Put the mbuf in the freelist, and in case move the
 * flow to the end of the bucket.
 */
static int
drop(struct cfg_s *c, struct mbuf *m)
{
	struct dn_queue *q;
	int i;

	c->drop++;
	q = FI2Q(c, m->flow_id);
	i = q->ni.length; // XXX or ffs...

	ND("q %p id %d current length %d", q, m->flow_id, i);
	if (i < BACKLOG) {
		struct list_head *h = &q->ni.h;
		c->llmask &= ~(1<<(i+1));
		c->llmask |= (1<<(i));
		list_del(h);
		list_add_tail(h, &c->ll[i]);
	}
	m->m_nextpkt = c->freelist;
	c->freelist = m;
	return 0;
}


/*
 * dn_sch_inst does not have a queue, for the RR we
 * allocate a mq right after si
 */
static int
default_enqueue(struct dn_sch_inst *si, struct dn_queue *q, struct mbuf *m)
{
	struct mq *mq = (struct mq *)si;

	(void)q;
	/* this is the default function if no scheduler is provided */
	if (mq->head == NULL)
		mq->head = m;
	else
		mq->tail->m_nextpkt = m;
	mq->tail = m;
	return 0; /* default - success */
}


static struct mbuf *
default_dequeue(struct dn_sch_inst *si)
{
	struct mq *mq = (struct mq *)si;
	struct mbuf *m;
	/* this is the default function if no scheduler is provided */
	if ((m = mq->head)) {
		m = mq->head;
		mq->head = m->m_nextpkt;
		m->m_nextpkt = NULL;
	}
	return m;
}

static void
gnet_stats_enq(struct cfg_s *c, struct mbuf *mb)
{
	struct dn_sch_inst *si = c->si;
	struct dn_queue *_q = FI2Q(c, mb->flow_id);

	if (_q->ni.length == 1) {
		_q->ni.bytes = 0;
		_q->ni.sch_bytes = si->ni.bytes;
	}
}

static void
gnet_stats_deq(struct cfg_s *c, struct mbuf *mb)
{
	struct dn_sch_inst *si = c->si;
	struct dn_queue *_q = FI2Q(c, mb->flow_id);
	int len = mb->m_pkthdr.len;

	_q->ni.bytes += len;
	si->ni.bytes += len;

	if (_q->ni.length == 0) {
		double bytes = (double)_q->ni.bytes;
		double sch_bytes = (double)si->ni.bytes - _q->ni.sch_bytes;
		double weight = (double)_q->fs->fs.par[0] / c->wsum;
		double wfi = sch_bytes * weight - bytes;

		if (c->q_wfi[mb->flow_id] < wfi)
			c->q_wfi[mb->flow_id] = wfi;
	}
}

static int
mainloop(struct cfg_s *c)
{
	int i;
	struct mbuf *m;

	for (i=0; i < c->loops; i++) {
		/* implement histeresis */
		controller(c);
		DX(3, "loop %d enq %d send %p rx %d",
			i, c->_enqueue, c->tosend, c->can_dequeue);
		if ( (m = c->tosend) ) {
			int ret;
			struct dn_queue *q = FI2Q(c, m->flow_id);
			c->_enqueue++;
			ret = c->enq(c->si, q, m);
			if (ret) {
				drop(c, m);
				D("loop %d enqueue fail", i );
				/*
				 * XXX do not insist; rather, try dequeue
				 */
				goto do_dequeue;
			} else {
				ND("enqueue ok");
				c->pending++;
				gnet_stats_enq(c, m);
			}
		} else if (c->can_dequeue) {
do_dequeue:
			c->dequeue++;
			m = c->deq(c->si);
			if (m) {
				c->pending--;
				drop(c, m);
				c->drop--;	/* compensate */
				gnet_stats_deq(c, m);
			} else {
				D("--- ouch, cannot operate on iteration %d, pending %d", i, c->pending);
				break;
			}
		}
	}
	DX(1, "mainloop ends %d", i);
	return 0;
}

int
dump(struct cfg_s *c)
{
	int i;

	for (i=0; i < c->flows; i++) {
		//struct dn_queue *q = FI2Q(c, i);
		ND(1, "queue %4d tot %10llu", i,
		    (unsigned long long)q->ni.tot_bytes);
	}
	DX(1, "done %d loops\n", c->loops);
	return 0;
}

/* interpret a number in human form */
static long
getnum(const char *s, char **next, const char *key)
{
	char *end = NULL;
	long l;

	if (next)	/* default */
		*next = NULL;
	if (s && *s) {
		DX(3, "token is <%s> %s", s, key ? key : "-");
		l = strtol(s, &end, 0);
	} else {
		DX(3, "empty string");
		l = -1;
	}
	if (l < 0) {
		DX(2, "invalid %s for %s", s ? s : "NULL", (key ? key : "") );
		return 0;	// invalid 
	}
	if (!end || !*end)
		return l;
	if (*end == 'n')
		l = -l;	/* multiply by n */
	else if (*end == 'K')
		l = l*1000;
	else if (*end == 'M')
		l = l*1000000;
	else if (*end == 'k')
		l = l*1024;
	else if (*end == 'm')
		l = l*1024*1024;
	else if (*end == 'w')
		;
	else {/* not recognized */
		D("suffix %s for %s, next %p", end, key, next);
		end--;
	}
	end++;
	DX(3, "suffix now %s for %s, next %p", end, key, next);
	if (next && *end) {
		DX(3, "setting next to %s for %s", end, key);
		*next = end;
	}
	return l;
}

/*
 * flowsets are a comma-separated list of
 *     weight:maxlen:flows
 * indicating how many flows are hooked to that fs.
 * Both weight and range can be min-max-steps.
 * The first pass (fs != NULL) justs count the number of flowsets and flows,
 * the second pass (fs == NULL) we complete the setup.
 */
static void
parse_flowsets(struct cfg_s *c, const char *fs)
{
	char *s, *cur, *next;
	int n_flows = 0, n_fs = 0, wsum = 0;
	int i, j;
	struct dn_fs *prev = NULL;
	int pass = (fs == NULL);

	DX(3, "--- pass %d flows %d flowsets %d", pass, c->flows, c->flowsets);
	if (fs != NULL) { /* first pass */
		if (c->fs_config)
			D("warning, overwriting fs %s with %s",
				c->fs_config, fs);
		c->fs_config = fs;
	}
	s = c->fs_config ? strdup(c->fs_config) : NULL;
	if (s == NULL) {
		if (pass == 0)
			D("no fsconfig");
		return;
	}
	for (next = s; (cur = strsep(&next, ","));) {
		char *p = NULL;
		int w, w_h, w_steps, wi;
		int len, len_h, l_steps, li;
		int flows;

		w = getnum(strsep(&cur, ":"), &p, "weight");
		if (w <= 0)
			w = 1;
		w_h = p ? getnum(p+1, &p, "weight_max") : w;
		w_steps = p ? getnum(p+1, &p, "w_steps") : (w_h == w ?1:2);
		len = getnum(strsep(&cur, ":"), &p, "len");
		if (len <= 0)
			len = 1000;
		len_h = p ? getnum(p+1, &p, "len_max") : len;
		l_steps = p ? getnum(p+1, &p, "l_steps") : (len_h == len ? 1 : 2);
		flows = getnum(strsep(&cur, ":"), NULL, "flows");
		if (flows == 0)
			flows = 1;
		DX(4, "weight %d..%d (%d) len %d..%d (%d) flows %d",
			w, w_h, w_steps, len, len_h, l_steps, flows);
		if (w == 0 || w_h < w || len == 0 || len_h < len ||
				flows == 0) {
			DX(4,"wrong parameters %s", s);
			return;
		}
		n_flows += flows * w_steps * l_steps;
		for (i = 0; i < w_steps; i++) {
			wi = w + ((w_h - w)* i)/(w_steps == 1 ? 1 : (w_steps-1));
			for (j = 0; j < l_steps; j++, n_fs++) {
				struct dn_fs *fs = &c->fs[n_fs].fs; // tentative
				int x;

				li = len + ((len_h - len)* j)/(l_steps == 1 ? 1 : (l_steps-1));
				x = (wi*2048)/li;
				DX(3, "----- fs %4d weight %4d lmax %4d X %4d flows %d",
					n_fs, wi, li, x, flows);
				if (pass == 0)
					continue;
				if (c->fs == NULL || c->flowsets <= n_fs) {
					D("error in number of flowsets");
					return;
				}
				wsum += wi * flows;
				fs->par[0] = wi;
				fs->par[1] = li;
				fs->index = n_fs;
				fs->n_flows = flows;
				fs->cur = fs->first_flow = prev==NULL ? 0 : prev->next_flow;
				fs->next_flow = fs->first_flow + fs->n_flows;
				fs->y = x * flows;
				fs->base_y = (prev == NULL) ? 0 : prev->next_y;
				fs->next_y = fs->base_y + fs->y;
				prev = fs;
			}
		}
	}
	c->flows = n_flows;
	c->flowsets = n_fs;
	c->wsum = wsum;
	if (pass == 0)
		return;

	/* now link all flows to their parent flowsets */
	DX(1,"%d flows on %d flowsets", c->flows, c->flowsets);
#ifdef USE_CUR
	c->max_y = prev ? prev->base_y + prev->y : 0;
	DX(1,"%d flows on %d flowsets max_y %d", c->flows, c->flowsets, c->max_y);
#endif /* USE_CUR */
	for (i=0; i < c->flowsets; i++) {
		struct dn_fs *fs = &c->fs[i].fs;
		DX(1, "fs %3d w %5d l %4d flow %5d .. %5d y %6d .. %6d",
			i, fs->par[0], fs->par[1],
			fs->first_flow, fs->next_flow,
			fs->base_y, fs->next_y);
		for (j = fs->first_flow; j < fs->next_flow; j++) {
			struct dn_queue *q = FI2Q(c, j);
			q->fs = &c->fs[i];
		}
	}
}

/* available schedulers */
extern moduledata_t *_g_dn_fifo;
extern moduledata_t *_g_dn_wf2qp;
extern moduledata_t *_g_dn_rr;
extern moduledata_t *_g_dn_qfq;
#ifdef WITH_QFQP
extern moduledata_t *_g_dn_qfqp;
#endif
#ifdef WITH_KPS
extern moduledata_t *_g_dn_kps;
#endif

static int
init(struct cfg_s *c)
{
	int i;
	int ac = c->ac;
	char * const *av = c->av;

	c->si_len = sizeof(struct dn_sch_inst);
	c->q_len = sizeof(struct dn_queue);
	moduledata_t *mod = NULL;
	struct dn_alg *p = NULL;

	c->th_min = -1; /* 1 packet per flow */
	c->th_max = -20;/* 20 packets per flow */
	c->lmin = c->lmax = 1280;	/* packet len */
	c->flows = 1;
	c->flowsets = 1;
	c->name = "null";
	ac--; av++;
	while (ac > 1) {
		if (!strcmp(*av, "-n")) {
			c->loops = getnum(av[1], NULL, av[0]);
		} else if (!strcmp(*av, "-d")) {
			debug = atoi(av[1]);
		} else if (!strcmp(*av, "-alg")) {
			if (!strcmp(av[1], "rr"))
				mod = _g_dn_rr;
			else if (!strcmp(av[1], "wf2qp"))
				mod = _g_dn_wf2qp;
			else if (!strcmp(av[1], "fifo"))
				mod = _g_dn_fifo;
			else if (!strcmp(av[1], "qfq"))
				mod = _g_dn_qfq;
#ifdef WITH_QFQP
			else if (!strcmp(av[1], "qfq+") ||
			    !strcmp(av[1], "qfqp") )
				mod = _g_dn_qfqp;
#endif
#ifdef WITH_KPS
			else if (!strcmp(av[1], "kps"))
				mod = _g_dn_kps;
#endif
			else
				mod = NULL;
			c->name = mod ? mod->name : "NULL";
			DX(3, "using scheduler %s", c->name);
		} else if (!strcmp(*av, "-len")) {
			c->lmin = getnum(av[1], NULL, av[0]);
			c->lmax = c->lmin;
			DX(3, "setting max to %d", c->th_max);
#ifdef USE_BURST
		} else if (!strcmp(*av, "-burst")) {
			c->maxburst = getnum(av[1], NULL, av[0]);
			DX(3, "setting max to %d", c->th_max);
#endif /* USE_BURST */
		} else if (!strcmp(*av, "-qmax")) {
			c->th_max = getnum(av[1], NULL, av[0]);
			DX(3, "setting max to %d", c->th_max);
		} else if (!strcmp(*av, "-qmin")) {
			c->th_min = getnum(av[1], NULL, av[0]);
			DX(3, "setting min to %d", c->th_min);
		} else if (!strcmp(*av, "-flows")) {
			c->flows = getnum(av[1], NULL, av[0]);
			DX(3, "setting flows to %d", c->flows);
		} else if (!strcmp(*av, "-flowsets")) {
			parse_flowsets(c, av[1]); /* first pass */
			DX(3, "setting flowsets to %d", c->flowsets);
		} else {
			D("option %s not recognised, ignore", *av);
		}
		ac -= 2; av += 2;
	}
#ifdef USE_BURST
	if (c->maxburst <= 0)
		c->maxburst = 1;
#endif /* USE_BURST */
	if (c->loops <= 0)
		c->loops = 1;
	if (c->flows <= 0)
		c->flows = 1;
	if (c->flowsets <= 0)
		c->flowsets = 1;
	if (c->lmin <= 0)
		c->lmin = 1;
	if (c->lmax <= 0)
		c->lmax = 1;
	/* multiply by N */
	if (c->th_min < 0)
		c->th_min = c->flows * -c->th_min;
	if (c->th_max < 0)
		c->th_max = c->flows * -c->th_max;
	if (c->th_max <= c->th_min)
		c->th_max = c->th_min + 1;

	/* now load parameters from the module */
	if (mod) {
		p = mod->p;
		DX(3, "using module %s f %p p %p", mod->name, mod->f, mod->p);
		DX(3, "modname %s ty %d", p->name, p->type);
		// XXX check enq and deq not null
		c->enq = p->enqueue;
		c->deq = p->dequeue;
		c->si_len += p->si_datalen;
		c->q_len += p->q_datalen;
		c->schk_len += p->schk_datalen;
	} else {
		/* make sure c->si has room for a queue */
		c->enq = default_enqueue;
		c->deq = default_dequeue;
	}

	/* allocate queues, flowsets and one scheduler */
	D("using %d flows, %d flowsets", c->flows, c->flowsets);
	D("q_len %d dn_fsk %d si %d sched %d",
		c->q_len, (int)sizeof(struct dn_fsk),
		c->si_len, c->schk_len);
	c->sched = calloc(1, c->schk_len); /* one parent scheduler */
	c->si = calloc(1, c->si_len); /* one scheduler instance */
	c->fs = calloc(c->flowsets, sizeof(struct dn_fsk));
	c->q = calloc(c->flows, c->q_len);	/* one queue per flow */
	c->q_wfi = calloc(c->flows, sizeof(double)); /* stats, one per flow */
	if (!c->sched || !c->si || !c->fs || !c->q || !c->q_wfi) {
		D("error allocating memory");
		exit(1);
	}
	c->si->sched = c->sched; /* link scheduler instance to template */
	if (p) {
		/* run initialization code if needed */
		if (p->config)
			p->config(c->si->sched);
		if (p->new_sched)
			p->new_sched(c->si);
	}
	/* parse_flowsets links queues to their flowsets */
	parse_flowsets(c, NULL); /* second pass */
	/* complete the work calling new_fsk */
	for (i = 0; i < c->flowsets; i++) {
		struct dn_fsk *fsk = &c->fs[i];
		if (fsk->fs.par[1] == 0)
			fsk->fs.par[1] = 1000;	/* default pkt len */
		fsk->sched = c->si->sched;
		if (p && p->new_fsk)
			p->new_fsk(fsk);
	}
	/* --- now the scheduler is initialized --- */

	/*
	 * initialize the lists for the generator, and put
	 * all flows in the list for backlog = 0
	 */
	for (i=0; i <= BACKLOG+5; i++)
		INIT_LIST_HEAD(&c->ll[i]);

	for (i = 0; i < c->flows; i++) {
		struct dn_queue *q = FI2Q(c, i);
		if (q->fs == NULL)
			q->fs = &c->fs[0]; /* XXX */
		q->_si = c->si;
		if (p && p->new_queue)
			p->new_queue(q);
		INIT_LIST_HEAD(&q->ni.h);
		list_add_tail(&q->ni.h, &c->ll[0]);
	}
	c->llmask = 1; /* all flows are in the first list */
	return 0;
}


int
main(int ac, char *av[])
{
	struct cfg_s c;
	double ll;
	int i;
	char msg[40];

	bzero(&c, sizeof(c));
	c.ac = ac;
	c.av = av;
	init(&c);
	gettimeofday(&c.time, NULL);
	D("th_min %d th_max %d", c.th_min, c.th_max);

	mainloop(&c);
	{
		struct timeval end;
		gettimeofday(&end, NULL);
		timersub(&end, &c.time, &c.time);
	}
	ll = c.time.tv_sec*1000000 + c.time.tv_usec;
	ll *= 1000;	/* convert to nanoseconds */
	ll /= c._enqueue;
	sprintf(msg, "1::%d", c.flows);
	for (i = 0; i < c.flows; i++) {
		if (c.wfi < c.q_wfi[i])
			c.wfi = c.q_wfi[i];
	}
	D("sched=%-12s\ttime=%d.%03d sec (%.0f nsec) enq %lu %lu deq\n"
	   "\twfi=%.02f\tflow=%-16s",
	   c.name, (int)c.time.tv_sec, (int)c.time.tv_usec / 1000, ll,
	   (unsigned long)c._enqueue, (unsigned long)c.dequeue, c.wfi,
	   c.fs_config ? c.fs_config : msg);
	dump(&c);
	DX(1, "done ac %d av %p", ac, av);
	for (i=0; i < ac; i++)
		DX(1, "arg %d %s", i, av[i]);
	return 0;
}

/*
 * The controller decides whether in this iteration we should send
 * (the packet is in c->tosend) and/or receive (flag c->can_dequeue)
 */
static void
controller(struct cfg_s *c)
{
	struct mbuf *m;
	struct dn_fs *fs;
	int flow_id;

	/* hysteresis between max and min */
	if (c->state == 0 && c->pending >= (uint32_t)c->th_max)
		c->state = 1;
	else if (c->state == 1 && c->pending <= (uint32_t)c->th_min)
		c->state = 0;
	ND(1, "state %d pending %2d", c->state, c->pending);
	c->can_dequeue = c->state;
	c->tosend = NULL;
	if (c->can_dequeue)
		return;

	/*
	 * locate the flow to use for enqueueing
	 * We take the queue with the lowest number of queued packets,
	 * generate a packet for it, and put the queue in the next highest.
	 */
    if (1) {
	int i;
	struct dn_queue *q;
	struct list_head *h;

	i = ffs(c->llmask) - 1;
	if (i < 0) {
		D("no candidate");
		c->can_dequeue = 1;
		return;
	}
	h = &c->ll[i];
	ND(1, "backlog %d p %p prev %p next %p", i, h, h->prev, h->next);
	q = list_first_entry(h, struct dn_queue, ni.h);
	list_del(&q->ni.h);
	flow_id = Q2FI(c, q);
	DX(2, "extracted flow %p %d backlog %d", q, flow_id, i);
	if (list_empty(h)) {
		ND(2, "backlog %d empty", i);
		c->llmask &= ~(1<<i);
	}
	ND(1, "before %d p %p prev %p next %p", i+1, h+1, h[1].prev, h[1].next);
	list_add_tail(&q->ni.h, h+1);
	ND(1, " after %d p %p prev %p next %p", i+1, h+1, h[1].prev, h[1].next);
	if (i < BACKLOG) {
		ND(2, "backlog %d full", i+1);
		c->llmask |= 1<<(1+i);
	}
	fs = &q->fs->fs;
	fs->cur = flow_id;
#ifdef USE_CUR
	c->cur_fs = q->fs - c->fs;
    } else {
	/* XXX this does not work ? */
	/* now decide whom to send the packet, and the length */
	/* lookup in the flow table */
	if (c->cur_y >= c->max_y) {	/* handle wraparound */
		c->cur_y = 0;
		c->cur_fs = 0;
	}
	fs = &c->fs[c->cur_fs].fs;
	flow_id = fs->cur++;
	if (fs->cur >= fs->next_flow)
		fs->cur = fs->first_flow;
	c->cur_y++;
	if (c->cur_y >= fs->next_y)
		c->cur_fs++;
#endif /* USE_CUR */
    }

	/* construct a packet */
	if (c->freelist) {
		m = c->tosend = c->freelist;
		c->freelist = c->freelist->m_nextpkt;
	} else {
		m = c->tosend = calloc(1, sizeof(struct mbuf));
	}
	if (m == NULL)
		return;

	//m->cfg = c;
	m->m_nextpkt = NULL;
	m->m_pkthdr.len = fs->par[1]; // XXX maxlen
	m->flow_id = flow_id;

	ND(2,"y %6d flow %5d fs %3d weight %4d len %4d",
		c->cur_y, m->flow_id, c->cur_fs,
		fs->par[0], m->m_pkthdr.len);

}
