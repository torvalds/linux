/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
 *  ALSA sequencer Timer
 *  Copyright (c) 1998-1999 by Frank van de Pol <fvdpol@coil.demon.nl>
 */
#ifndef __SND_SEQ_TIMER_H
#define __SND_SEQ_TIMER_H

#include <sound/timer.h>
#include <sound/seq_kernel.h>

struct snd_seq_timer_tick {
	snd_seq_tick_time_t	cur_tick;	/* current tick */
	unsigned long		resolution;	/* time per tick in nsec */
	unsigned long		fraction;	/* current time per tick in nsec */
};

struct snd_seq_timer {
	/* ... tempo / offset / running state */

	unsigned int		running:1,	/* running state of queue */	
				initialized:1;	/* timer is initialized */

	unsigned int		tempo;		/* current tempo, us/tick */
	int			ppq;		/* time resolution, ticks/quarter */

	snd_seq_real_time_t	cur_time;	/* current time */
	struct snd_seq_timer_tick	tick;	/* current tick */
	int tick_updated;
	
	int			type;		/* timer type */
	struct snd_timer_id	alsa_id;	/* ALSA's timer ID */
	struct snd_timer_instance	*timeri;	/* timer instance */
	unsigned int		ticks;
	unsigned long		preferred_resolution; /* timer resolution, ticks/sec */

	unsigned int skew;
	unsigned int skew_base;
	unsigned int tempo_base;

	struct timespec64	last_update;	 /* time of last clock update, used for interpolation */

	spinlock_t lock;
};


/* create new timer (constructor) */
struct snd_seq_timer *snd_seq_timer_new(void);

/* delete timer (destructor) */
void snd_seq_timer_delete(struct snd_seq_timer **tmr);

/* */
static inline void snd_seq_timer_update_tick(struct snd_seq_timer_tick *tick,
					     unsigned long resolution)
{
	if (tick->resolution > 0) {
		tick->fraction += resolution;
		tick->cur_tick += (unsigned int)(tick->fraction / tick->resolution);
		tick->fraction %= tick->resolution;
	}
}


/* compare timestamp between events */
/* return 1 if a >= b; otherwise return 0 */
static inline int snd_seq_compare_tick_time(snd_seq_tick_time_t *a, snd_seq_tick_time_t *b)
{
	/* compare ticks */
	return (*a >= *b);
}

static inline int snd_seq_compare_real_time(snd_seq_real_time_t *a, snd_seq_real_time_t *b)
{
	/* compare real time */
	if (a->tv_sec > b->tv_sec)
		return 1;
	if ((a->tv_sec == b->tv_sec) && (a->tv_nsec >= b->tv_nsec))
		return 1;
	return 0;
}


static inline void snd_seq_sanity_real_time(snd_seq_real_time_t *tm)
{
	while (tm->tv_nsec >= 1000000000) {
		/* roll-over */
		tm->tv_nsec -= 1000000000;
                tm->tv_sec++;
        }
}


/* increment timestamp */
static inline void snd_seq_inc_real_time(snd_seq_real_time_t *tm, snd_seq_real_time_t *inc)
{
	tm->tv_sec  += inc->tv_sec;
	tm->tv_nsec += inc->tv_nsec;
	snd_seq_sanity_real_time(tm);
}

static inline void snd_seq_inc_time_nsec(snd_seq_real_time_t *tm, unsigned long nsec)
{
	tm->tv_nsec  += nsec;
	snd_seq_sanity_real_time(tm);
}

/* called by timer isr */
struct snd_seq_queue;
int snd_seq_timer_open(struct snd_seq_queue *q);
int snd_seq_timer_close(struct snd_seq_queue *q);
int snd_seq_timer_midi_open(struct snd_seq_queue *q);
int snd_seq_timer_midi_close(struct snd_seq_queue *q);
void snd_seq_timer_defaults(struct snd_seq_timer *tmr);
void snd_seq_timer_reset(struct snd_seq_timer *tmr);
int snd_seq_timer_stop(struct snd_seq_timer *tmr);
int snd_seq_timer_start(struct snd_seq_timer *tmr);
int snd_seq_timer_continue(struct snd_seq_timer *tmr);
int snd_seq_timer_set_tempo(struct snd_seq_timer *tmr, int tempo);
int snd_seq_timer_set_tempo_ppq(struct snd_seq_timer *tmr, int tempo, int ppq,
				unsigned int tempo_base);
int snd_seq_timer_set_position_tick(struct snd_seq_timer *tmr, snd_seq_tick_time_t position);
int snd_seq_timer_set_position_time(struct snd_seq_timer *tmr, snd_seq_real_time_t position);
int snd_seq_timer_set_skew(struct snd_seq_timer *tmr, unsigned int skew, unsigned int base);
snd_seq_real_time_t snd_seq_timer_get_cur_time(struct snd_seq_timer *tmr,
					       bool adjust_ktime);
snd_seq_tick_time_t snd_seq_timer_get_cur_tick(struct snd_seq_timer *tmr);

extern int seq_default_timer_class;
extern int seq_default_timer_sclass;
extern int seq_default_timer_card;
extern int seq_default_timer_device;
extern int seq_default_timer_subdevice;
extern int seq_default_timer_resolution;

#endif
