/*
 *  Routines for Gravis UltraSound soundcards - Sample support
 *  Copyright (c) by Jaroslav Kysela <perex@suse.cz>
 *
 *
 *   This program is free software; you can redistribute it and/or modify
 *   it under the terms of the GNU General Public License as published by
 *   the Free Software Foundation; either version 2 of the License, or
 *   (at your option) any later version.
 *
 *   This program is distributed in the hope that it will be useful,
 *   but WITHOUT ANY WARRANTY; without even the implied warranty of
 *   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *   GNU General Public License for more details.
 *
 *   You should have received a copy of the GNU General Public License
 *   along with this program; if not, write to the Free Software
 *   Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307 USA
 *
 */

#include <sound/driver.h>
#include <linux/time.h>
#include <sound/core.h>
#include <sound/gus.h>

/*
 *
 */

static void select_instrument(struct snd_gus_card * gus, struct snd_gus_voice * v)
{
	struct snd_seq_kinstr *instr;

#if 0
	printk("select instrument: cluster = %li, std = 0x%x, bank = %i, prg = %i\n",
					v->instr.cluster,
					v->instr.std,
					v->instr.bank,
					v->instr.prg);
#endif
	instr = snd_seq_instr_find(gus->gf1.ilist, &v->instr, 0, 1);
	if (instr != NULL) {
		if (instr->ops) {
			if (!strcmp(instr->ops->instr_type, SNDRV_SEQ_INSTR_ID_SIMPLE))
				snd_gf1_simple_init(v);
		}
		snd_seq_instr_free_use(gus->gf1.ilist, instr);
	}
}

/*
 *
 */

static void event_sample(struct snd_seq_event *ev, struct snd_gus_port *p,
			 struct snd_gus_voice *v)
{
	if (v->sample_ops && v->sample_ops->sample_stop)
		v->sample_ops->sample_stop(p->gus, v, SAMPLE_STOP_IMMEDIATELY);
	v->instr.std = ev->data.sample.param.sample.std;
	if (v->instr.std & 0xff000000) {        /* private instrument */
		v->instr.std &= 0x00ffffff;
		v->instr.std |= (unsigned int)ev->source.client << 24;
	}                                                
	v->instr.bank = ev->data.sample.param.sample.bank;
	v->instr.prg = ev->data.sample.param.sample.prg;
	select_instrument(p->gus, v);
}

static void event_cluster(struct snd_seq_event *ev, struct snd_gus_port *p,
			  struct snd_gus_voice *v)
{
	if (v->sample_ops && v->sample_ops->sample_stop)
		v->sample_ops->sample_stop(p->gus, v, SAMPLE_STOP_IMMEDIATELY);
	v->instr.cluster = ev->data.sample.param.cluster.cluster;
	select_instrument(p->gus, v);
}

static void event_start(struct snd_seq_event *ev, struct snd_gus_port *p,
			struct snd_gus_voice *v)
{
	if (v->sample_ops && v->sample_ops->sample_start)
		v->sample_ops->sample_start(p->gus, v, ev->data.sample.param.position);
}

static void event_stop(struct snd_seq_event *ev, struct snd_gus_port *p,
		       struct snd_gus_voice *v)
{
	if (v->sample_ops && v->sample_ops->sample_stop)
		v->sample_ops->sample_stop(p->gus, v, ev->data.sample.param.stop_mode);
}

static void event_freq(struct snd_seq_event *ev, struct snd_gus_port *p,
		       struct snd_gus_voice *v)
{
	if (v->sample_ops && v->sample_ops->sample_freq)
		v->sample_ops->sample_freq(p->gus, v, ev->data.sample.param.frequency);
}

static void event_volume(struct snd_seq_event *ev, struct snd_gus_port *p,
			 struct snd_gus_voice *v)
{
	if (v->sample_ops && v->sample_ops->sample_volume)
		v->sample_ops->sample_volume(p->gus, v, &ev->data.sample.param.volume);
}

static void event_loop(struct snd_seq_event *ev, struct snd_gus_port *p,
		       struct snd_gus_voice *v)
{
	if (v->sample_ops && v->sample_ops->sample_loop)
		v->sample_ops->sample_loop(p->gus, v, &ev->data.sample.param.loop);
}

static void event_position(struct snd_seq_event *ev, struct snd_gus_port *p,
			   struct snd_gus_voice *v)
{
	if (v->sample_ops && v->sample_ops->sample_pos)
		v->sample_ops->sample_pos(p->gus, v, ev->data.sample.param.position);
}

static void event_private1(struct snd_seq_event *ev, struct snd_gus_port *p,
			   struct snd_gus_voice *v)
{
	if (v->sample_ops && v->sample_ops->sample_private1)
		v->sample_ops->sample_private1(p->gus, v, (unsigned char *)&ev->data.sample.param.raw8);
}

typedef void (gus_sample_event_handler_t)(struct snd_seq_event *ev,
					  struct snd_gus_port *p,
					  struct snd_gus_voice *v);
static gus_sample_event_handler_t *gus_sample_event_handlers[9] = {
	event_sample,
	event_cluster,
	event_start,
	event_stop,
	event_freq,
	event_volume,
	event_loop,
	event_position,
	event_private1
};

void snd_gus_sample_event(struct snd_seq_event *ev, struct snd_gus_port *p)
{
	int idx, voice;
	struct snd_gus_card *gus = p->gus;
	struct snd_gus_voice *v;
	unsigned long flags;
	
	idx = ev->type - SNDRV_SEQ_EVENT_SAMPLE;
	if (idx < 0 || idx > 8)
		return;
	for (voice = 0; voice < 32; voice++) {
		v = &gus->gf1.voices[voice];
		if (v->use && v->client == ev->source.client &&
		    v->port == ev->source.port &&
		    v->index == ev->data.sample.channel) {
		    	spin_lock_irqsave(&gus->event_lock, flags);
			gus_sample_event_handlers[idx](ev, p, v);
			spin_unlock_irqrestore(&gus->event_lock, flags);
			return;
		}
	}
}
