/*
 *  ALSA sequencer Timer
 *  Copyright (c) 1998-1999 by Frank van de Pol <fvdpol@coil.demon.nl>
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
#ifndef __SND_SEQ_TIMER_H
#define __SND_SEQ_TIMER_H

#include <sound/timer.h>
#include <sound/seq_kernel.h>

typedef struct {
	snd_seq_tick_time_t	cur_tick;	/* current tick */
	unsigned long		resolution;	/* time per tick in nsec */
	unsigned long		fraction;	/* current time per tick in nsec */
} seq_timer_tick_t;

typedef struct {
	/* ... tempo / offset / running state */

	unsigned int		running:1,	/* running state of queue */	
				initialized:1;	/* timer is initialized */

	unsigned int		tempo;		/* current tempo, us/tick */
	int			ppq;		/* time resolution, ticks/quarter */

	snd_seq_real_time_t	cur_time;	/* current time */
	seq_timer_tick_t	tick;		/* current tick */
	int tick_updated;
	
	int			type;		/* timer type */
	snd_timer_id_t		alsa_id;	/* ALSA's timer ID */
	snd_timer_instance_t	*timeri;	/* timer instance */
	unsigned int		ticks;
	unsigned long		preferred_resolution; /* timer resolution, ticks/sec */

	unsigned int skew;
	unsigned int skew_base;

	struct timeval 		last_update;	 /* time of last clock update, used for interpolation */

	spinlock_t lock;
} seq_timer_t;


/* create new timer (constructor) */
extern seq_timer_t *snd_seq_timer_new(void);

/* delete timer (destructor) */
extern void snd_seq_timer_delete(seq_timer_t **tmr);

/* */
static inline void snd_seq_timer_update_tick(seq_timer_tick_t *tick, unsigned long resolution)
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
int snd_seq_timer_open(queue_t *q);
int snd_seq_timer_close(queue_t *q);
int snd_seq_timer_midi_open(queue_t *q);
int snd_seq_timer_midi_close(queue_t *q);
void snd_seq_timer_defaults(seq_timer_t *tmr);
void snd_seq_timer_reset(seq_timer_t *tmr);
int snd_seq_timer_stop(seq_timer_t *tmr);
int snd_seq_timer_start(seq_timer_t *tmr);
int snd_seq_timer_continue(seq_timer_t *tmr);
int snd_seq_timer_set_tempo(seq_timer_t *tmr, int tempo);
int snd_seq_timer_set_ppq(seq_timer_t *tmr, int ppq);
int snd_seq_timer_set_position_tick(seq_timer_t *tmr, snd_seq_tick_time_t position);
int snd_seq_timer_set_position_time(seq_timer_t *tmr, snd_seq_real_time_t position);
int snd_seq_timer_set_skew(seq_timer_t *tmr, unsigned int skew, unsigned int base);
snd_seq_real_time_t snd_seq_timer_get_cur_time(seq_timer_t *tmr);
snd_seq_tick_time_t snd_seq_timer_get_cur_tick(seq_timer_t *tmr);

#endif
