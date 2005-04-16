/*
 * net/sched/sch_gred.c	Generic Random Early Detection queue.
 *
 *
 *              This program is free software; you can redistribute it and/or
 *              modify it under the terms of the GNU General Public License
 *              as published by the Free Software Foundation; either version
 *              2 of the License, or (at your option) any later version.
 *
 * Authors:    J Hadi Salim (hadi@cyberus.ca) 1998-2002
 *
 *             991129: -  Bug fix with grio mode
 *		       - a better sing. AvgQ mode with Grio(WRED)
 *		       - A finer grained VQ dequeue based on sugestion
 *		         from Ren Liu
 *		       - More error checks
 *
 *
 *
 *  For all the glorious comments look at Alexey's sch_red.c
 */

#include <linux/config.h>
#include <linux/module.h>
#include <asm/uaccess.h>
#include <asm/system.h>
#include <linux/bitops.h>
#include <linux/types.h>
#include <linux/kernel.h>
#include <linux/sched.h>
#include <linux/string.h>
#include <linux/mm.h>
#include <linux/socket.h>
#include <linux/sockios.h>
#include <linux/in.h>
#include <linux/errno.h>
#include <linux/interrupt.h>
#include <linux/if_ether.h>
#include <linux/inet.h>
#include <linux/netdevice.h>
#include <linux/etherdevice.h>
#include <linux/notifier.h>
#include <net/ip.h>
#include <net/route.h>
#include <linux/skbuff.h>
#include <net/sock.h>
#include <net/pkt_sched.h>

#if 1 /* control */
#define DPRINTK(format,args...) printk(KERN_DEBUG format,##args)
#else
#define DPRINTK(format,args...)
#endif

#if 0 /* data */
#define D2PRINTK(format,args...) printk(KERN_DEBUG format,##args)
#else
#define D2PRINTK(format,args...)
#endif

struct gred_sched_data;
struct gred_sched;

struct gred_sched_data
{
/* Parameters */
	u32		limit;		/* HARD maximal queue length	*/
	u32		qth_min;	/* Min average length threshold: A scaled */
	u32		qth_max;	/* Max average length threshold: A scaled */
	u32      	DP;		/* the drop pramaters */
	char		Wlog;		/* log(W)		*/
	char		Plog;		/* random number bits	*/
	u32		Scell_max;
	u32		Rmask;
	u32		bytesin;	/* bytes seen on virtualQ so far*/
	u32		packetsin;	/* packets seen on virtualQ so far*/
	u32		backlog;	/* bytes on the virtualQ */
	u32		forced;	/* packets dropped for exceeding limits */
	u32		early;	/* packets dropped as a warning */
	u32		other;	/* packets dropped by invoking drop() */
	u32		pdrop;	/* packets dropped because we exceeded physical queue limits */
	char		Scell_log;
	u8		Stab[256];
	u8              prio;        /* the prio of this vq */

/* Variables */
	unsigned long	qave;		/* Average queue length: A scaled */
	int		qcount;		/* Packets since last random number generation */
	u32		qR;		/* Cached random number */

	psched_time_t	qidlestart;	/* Start of idle period	*/
};

struct gred_sched
{
	struct gred_sched_data *tab[MAX_DPs];
	u32 		DPs;   
	u32 		def; 
	u8 		initd; 
	u8 		grio; 
	u8 		eqp; 
};

static int
gred_enqueue(struct sk_buff *skb, struct Qdisc* sch)
{
	psched_time_t now;
	struct gred_sched_data *q=NULL;
	struct gred_sched *t= qdisc_priv(sch);
	unsigned long	qave=0;	
	int i=0;

	if (!t->initd && skb_queue_len(&sch->q) < (sch->dev->tx_queue_len ? : 1)) {
		D2PRINTK("NO GRED Queues setup yet! Enqueued anyway\n");
		goto do_enqueue;
	}


	if ( ((skb->tc_index&0xf) > (t->DPs -1)) || !(q=t->tab[skb->tc_index&0xf])) {
		printk("GRED: setting to default (%d)\n ",t->def);
		if (!(q=t->tab[t->def])) {
			DPRINTK("GRED: setting to default FAILED! dropping!! "
			    "(%d)\n ", t->def);
			goto drop;
		}
		/* fix tc_index? --could be controvesial but needed for
		   requeueing */
		skb->tc_index=(skb->tc_index&0xfffffff0) | t->def;
	}

	D2PRINTK("gred_enqueue virtualQ 0x%x classid %x backlog %d "
	    "general backlog %d\n",skb->tc_index&0xf,sch->handle,q->backlog,
	    sch->qstats.backlog);
	/* sum up all the qaves of prios <= to ours to get the new qave*/
	if (!t->eqp && t->grio) {
		for (i=0;i<t->DPs;i++) {
			if ((!t->tab[i]) || (i==q->DP))	
				continue; 
				
			if ((t->tab[i]->prio < q->prio) && (PSCHED_IS_PASTPERFECT(t->tab[i]->qidlestart)))
				qave +=t->tab[i]->qave;
		}
			
	}

	q->packetsin++;
	q->bytesin+=skb->len;

	if (t->eqp && t->grio) {
		qave=0;
		q->qave=t->tab[t->def]->qave;
		q->qidlestart=t->tab[t->def]->qidlestart;
	}

	if (!PSCHED_IS_PASTPERFECT(q->qidlestart)) {
		long us_idle;
		PSCHED_GET_TIME(now);
		us_idle = PSCHED_TDIFF_SAFE(now, q->qidlestart, q->Scell_max);
		PSCHED_SET_PASTPERFECT(q->qidlestart);

		q->qave >>= q->Stab[(us_idle>>q->Scell_log)&0xFF];
	} else {
		if (t->eqp) {
			q->qave += sch->qstats.backlog - (q->qave >> q->Wlog);
		} else {
			q->qave += q->backlog - (q->qave >> q->Wlog);
		}

	}
	

	if (t->eqp && t->grio) 
		t->tab[t->def]->qave=q->qave;

	if ((q->qave+qave) < q->qth_min) {
		q->qcount = -1;
enqueue:
		if (q->backlog + skb->len <= q->limit) {
			q->backlog += skb->len;
do_enqueue:
			__skb_queue_tail(&sch->q, skb);
			sch->qstats.backlog += skb->len;
			sch->bstats.bytes += skb->len;
			sch->bstats.packets++;
			return 0;
		} else {
			q->pdrop++;
		}

drop:
		kfree_skb(skb);
		sch->qstats.drops++;
		return NET_XMIT_DROP;
	}
	if ((q->qave+qave) >= q->qth_max) {
		q->qcount = -1;
		sch->qstats.overlimits++;
		q->forced++;
		goto drop;
	}
	if (++q->qcount) {
		if ((((qave+q->qave) - q->qth_min)>>q->Wlog)*q->qcount < q->qR)
			goto enqueue;
		q->qcount = 0;
		q->qR = net_random()&q->Rmask;
		sch->qstats.overlimits++;
		q->early++;
		goto drop;
	}
	q->qR = net_random()&q->Rmask;
	goto enqueue;
}

static int
gred_requeue(struct sk_buff *skb, struct Qdisc* sch)
{
	struct gred_sched_data *q;
	struct gred_sched *t= qdisc_priv(sch);
	q= t->tab[(skb->tc_index&0xf)];
/* error checking here -- probably unnecessary */
	PSCHED_SET_PASTPERFECT(q->qidlestart);

	__skb_queue_head(&sch->q, skb);
	sch->qstats.backlog += skb->len;
	sch->qstats.requeues++;
	q->backlog += skb->len;
	return 0;
}

static struct sk_buff *
gred_dequeue(struct Qdisc* sch)
{
	struct sk_buff *skb;
	struct gred_sched_data *q;
	struct gred_sched *t= qdisc_priv(sch);

	skb = __skb_dequeue(&sch->q);
	if (skb) {
		sch->qstats.backlog -= skb->len;
		q= t->tab[(skb->tc_index&0xf)];
		if (q) {
			q->backlog -= skb->len;
			if (!q->backlog && !t->eqp)
				PSCHED_GET_TIME(q->qidlestart);
		} else {
			D2PRINTK("gred_dequeue: skb has bad tcindex %x\n",skb->tc_index&0xf); 
		}
		return skb;
	}

	if (t->eqp) {
			q= t->tab[t->def];
			if (!q)	
				D2PRINTK("no default VQ set: Results will be "
				       "screwed up\n");
			else
				PSCHED_GET_TIME(q->qidlestart);
	}

	return NULL;
}

static unsigned int gred_drop(struct Qdisc* sch)
{
	struct sk_buff *skb;

	struct gred_sched_data *q;
	struct gred_sched *t= qdisc_priv(sch);

	skb = __skb_dequeue_tail(&sch->q);
	if (skb) {
		unsigned int len = skb->len;
		sch->qstats.backlog -= len;
		sch->qstats.drops++;
		q= t->tab[(skb->tc_index&0xf)];
		if (q) {
			q->backlog -= len;
			q->other++;
			if (!q->backlog && !t->eqp)
				PSCHED_GET_TIME(q->qidlestart);
		} else {
			D2PRINTK("gred_dequeue: skb has bad tcindex %x\n",skb->tc_index&0xf); 
		}

		kfree_skb(skb);
		return len;
	}

	q=t->tab[t->def];
	if (!q) {
		D2PRINTK("no default VQ set: Results might be screwed up\n");
		return 0;
	}

	PSCHED_GET_TIME(q->qidlestart);
	return 0;

}

static void gred_reset(struct Qdisc* sch)
{
	int i;
	struct gred_sched_data *q;
	struct gred_sched *t= qdisc_priv(sch);

	__skb_queue_purge(&sch->q);

	sch->qstats.backlog = 0;

        for (i=0;i<t->DPs;i++) {
	        q= t->tab[i];
		if (!q)	
			continue; 
		PSCHED_SET_PASTPERFECT(q->qidlestart);
		q->qave = 0;
		q->qcount = -1;
		q->backlog = 0;
		q->other=0;
		q->forced=0;
		q->pdrop=0;
		q->early=0;
	}
}

static int gred_change(struct Qdisc *sch, struct rtattr *opt)
{
	struct gred_sched *table = qdisc_priv(sch);
	struct gred_sched_data *q;
	struct tc_gred_qopt *ctl;
	struct tc_gred_sopt *sopt;
	struct rtattr *tb[TCA_GRED_STAB];
	struct rtattr *tb2[TCA_GRED_DPS];
	int i;

	if (opt == NULL || rtattr_parse_nested(tb, TCA_GRED_STAB, opt))
		return -EINVAL;

	if (tb[TCA_GRED_PARMS-1] == 0 && tb[TCA_GRED_STAB-1] == 0) {
		rtattr_parse_nested(tb2, TCA_GRED_DPS, opt);

	    if (tb2[TCA_GRED_DPS-1] == 0) 
			return -EINVAL;

		sopt = RTA_DATA(tb2[TCA_GRED_DPS-1]);
		table->DPs=sopt->DPs;   
		table->def=sopt->def_DP; 
		table->grio=sopt->grio; 
		table->initd=0;
		/* probably need to clear all the table DP entries as well */
		return 0;
	    }


	if (!table->DPs || tb[TCA_GRED_PARMS-1] == 0 || tb[TCA_GRED_STAB-1] == 0 ||
		RTA_PAYLOAD(tb[TCA_GRED_PARMS-1]) < sizeof(*ctl) ||
		RTA_PAYLOAD(tb[TCA_GRED_STAB-1]) < 256)
			return -EINVAL;

	ctl = RTA_DATA(tb[TCA_GRED_PARMS-1]);
	if (ctl->DP > MAX_DPs-1 ) {
		/* misbehaving is punished! Put in the default drop probability */
		DPRINTK("\nGRED: DP %u not in  the proper range fixed. New DP "
			"set to default at %d\n",ctl->DP,table->def);
		ctl->DP=table->def;
	}
	
	if (table->tab[ctl->DP] == NULL) {
		table->tab[ctl->DP]=kmalloc(sizeof(struct gred_sched_data),
					    GFP_KERNEL);
		if (NULL == table->tab[ctl->DP])
			return -ENOMEM;
		memset(table->tab[ctl->DP], 0, (sizeof(struct gred_sched_data)));
	}
	q= table->tab[ctl->DP]; 

	if (table->grio) {
		if (ctl->prio <=0) {
			if (table->def && table->tab[table->def]) {
				DPRINTK("\nGRED: DP %u does not have a prio"
					"setting default to %d\n",ctl->DP,
					table->tab[table->def]->prio);
				q->prio=table->tab[table->def]->prio;
			} else { 
				DPRINTK("\nGRED: DP %u does not have a prio"
					" setting default to 8\n",ctl->DP);
				q->prio=8;
			}
		} else {
			q->prio=ctl->prio;
		}
	} else {
		q->prio=8;
	}


	q->DP=ctl->DP;
	q->Wlog = ctl->Wlog;
	q->Plog = ctl->Plog;
	q->limit = ctl->limit;
	q->Scell_log = ctl->Scell_log;
	q->Rmask = ctl->Plog < 32 ? ((1<<ctl->Plog) - 1) : ~0UL;
	q->Scell_max = (255<<q->Scell_log);
	q->qth_min = ctl->qth_min<<ctl->Wlog;
	q->qth_max = ctl->qth_max<<ctl->Wlog;
	q->qave=0;
	q->backlog=0;
	q->qcount = -1;
	q->other=0;
	q->forced=0;
	q->pdrop=0;
	q->early=0;

	PSCHED_SET_PASTPERFECT(q->qidlestart);
	memcpy(q->Stab, RTA_DATA(tb[TCA_GRED_STAB-1]), 256);

	if ( table->initd && table->grio) {
	/* this looks ugly but it's not in the fast path */
		for (i=0;i<table->DPs;i++) {
			if ((!table->tab[i]) || (i==q->DP) )    
				continue; 
			if (table->tab[i]->prio == q->prio ){
				/* WRED mode detected */
				table->eqp=1;
				break;
			}
		}
	}

	if (!table->initd) {
		table->initd=1;
		/* 
        	the first entry also goes into the default until
        	over-written 
		*/

		if (table->tab[table->def] == NULL) {
			table->tab[table->def]=
				kmalloc(sizeof(struct gred_sched_data), GFP_KERNEL);
			if (NULL == table->tab[table->def])
				return -ENOMEM;

			memset(table->tab[table->def], 0,
			       (sizeof(struct gred_sched_data)));
		}
		q= table->tab[table->def]; 
		q->DP=table->def;
		q->Wlog = ctl->Wlog;
		q->Plog = ctl->Plog;
		q->limit = ctl->limit;
		q->Scell_log = ctl->Scell_log;
		q->Rmask = ctl->Plog < 32 ? ((1<<ctl->Plog) - 1) : ~0UL;
		q->Scell_max = (255<<q->Scell_log);
		q->qth_min = ctl->qth_min<<ctl->Wlog;
		q->qth_max = ctl->qth_max<<ctl->Wlog;

		if (table->grio)
			q->prio=table->tab[ctl->DP]->prio;
		else
			q->prio=8;

		q->qcount = -1;
		PSCHED_SET_PASTPERFECT(q->qidlestart);
		memcpy(q->Stab, RTA_DATA(tb[TCA_GRED_STAB-1]), 256);
	}
	return 0;

}

static int gred_init(struct Qdisc *sch, struct rtattr *opt)
{
	struct gred_sched *table = qdisc_priv(sch);
	struct tc_gred_sopt *sopt;
	struct rtattr *tb[TCA_GRED_STAB];
	struct rtattr *tb2[TCA_GRED_DPS];

	if (opt == NULL || rtattr_parse_nested(tb, TCA_GRED_STAB, opt))
		return -EINVAL;

	if (tb[TCA_GRED_PARMS-1] == 0 && tb[TCA_GRED_STAB-1] == 0) {
		rtattr_parse_nested(tb2, TCA_GRED_DPS, opt);

	    if (tb2[TCA_GRED_DPS-1] == 0) 
			return -EINVAL;

		sopt = RTA_DATA(tb2[TCA_GRED_DPS-1]);
		table->DPs=sopt->DPs;   
		table->def=sopt->def_DP; 
		table->grio=sopt->grio; 
		table->initd=0;
		return 0;
	}

	DPRINTK("\n GRED_INIT error!\n");
	return -EINVAL;
}

static int gred_dump(struct Qdisc *sch, struct sk_buff *skb)
{
	unsigned long qave;
	struct rtattr *rta;
	struct tc_gred_qopt *opt = NULL ;
	struct tc_gred_qopt *dst;
	struct gred_sched *table = qdisc_priv(sch);
	struct gred_sched_data *q;
	int i;
	unsigned char	 *b = skb->tail;

	rta = (struct rtattr*)b;
	RTA_PUT(skb, TCA_OPTIONS, 0, NULL);

	opt=kmalloc(sizeof(struct tc_gred_qopt)*MAX_DPs, GFP_KERNEL);

	if (opt  == NULL) {
		DPRINTK("gred_dump:failed to malloc for %Zd\n",
		    sizeof(struct tc_gred_qopt)*MAX_DPs);
		goto rtattr_failure;
	}

	memset(opt, 0, (sizeof(struct tc_gred_qopt))*table->DPs);

	if (!table->initd) {
		DPRINTK("NO GRED Queues setup!\n");
	}

	for (i=0;i<MAX_DPs;i++) {
		dst= &opt[i]; 
		q= table->tab[i]; 

		if (!q) {
			/* hack -- fix at some point with proper message
			   This is how we indicate to tc that there is no VQ
			   at this DP */

			dst->DP=MAX_DPs+i;
			continue;
		}

		dst->limit=q->limit;
		dst->qth_min=q->qth_min>>q->Wlog;
		dst->qth_max=q->qth_max>>q->Wlog;
		dst->DP=q->DP;
		dst->backlog=q->backlog;
		if (q->qave) {
			if (table->eqp && table->grio) {
				q->qidlestart=table->tab[table->def]->qidlestart;
				q->qave=table->tab[table->def]->qave;
			}
			if (!PSCHED_IS_PASTPERFECT(q->qidlestart)) {
				long idle;
				psched_time_t now;
				PSCHED_GET_TIME(now);
				idle = PSCHED_TDIFF_SAFE(now, q->qidlestart, q->Scell_max);
				qave  = q->qave >> q->Stab[(idle>>q->Scell_log)&0xFF];
				dst->qave = qave >> q->Wlog;

			} else {
				dst->qave = q->qave >> q->Wlog;
			}
		} else {
			dst->qave = 0;
		}
		

		dst->Wlog = q->Wlog;
		dst->Plog = q->Plog;
		dst->Scell_log = q->Scell_log;
		dst->other = q->other;
		dst->forced = q->forced;
		dst->early = q->early;
		dst->pdrop = q->pdrop;
		dst->prio = q->prio;
		dst->packets=q->packetsin;
		dst->bytesin=q->bytesin;
	}

	RTA_PUT(skb, TCA_GRED_PARMS, sizeof(struct tc_gred_qopt)*MAX_DPs, opt);
	rta->rta_len = skb->tail - b;

	kfree(opt);
	return skb->len;

rtattr_failure:
	if (opt)
		kfree(opt);
	DPRINTK("gred_dump: FAILURE!!!!\n");

/* also free the opt struct here */
	skb_trim(skb, b - skb->data);
	return -1;
}

static void gred_destroy(struct Qdisc *sch)
{
	struct gred_sched *table = qdisc_priv(sch);
	int i;

	for (i = 0;i < table->DPs; i++) {
		if (table->tab[i])
			kfree(table->tab[i]);
	}
}

static struct Qdisc_ops gred_qdisc_ops = {
	.next		=	NULL,
	.cl_ops		=	NULL,
	.id		=	"gred",
	.priv_size	=	sizeof(struct gred_sched),
	.enqueue	=	gred_enqueue,
	.dequeue	=	gred_dequeue,
	.requeue	=	gred_requeue,
	.drop		=	gred_drop,
	.init		=	gred_init,
	.reset		=	gred_reset,
	.destroy	=	gred_destroy,
	.change		=	gred_change,
	.dump		=	gred_dump,
	.owner		=	THIS_MODULE,
};

static int __init gred_module_init(void)
{
	return register_qdisc(&gred_qdisc_ops);
}
static void __exit gred_module_exit(void) 
{
	unregister_qdisc(&gred_qdisc_ops);
}
module_init(gred_module_init)
module_exit(gred_module_exit)
MODULE_LICENSE("GPL");
