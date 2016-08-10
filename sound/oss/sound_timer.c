/*
 * sound/oss/sound_timer.c
 */
/*
 * Copyright (C) by Hannu Savolainen 1993-1997
 *
 * OSS/Free for Linux is distributed under the GNU GENERAL PUBLIC LICENSE (GPL)
 * Version 2 (June 1991). See the "COPYING" file distributed with this software
 * for more info.
 */
/*
 * Thomas Sailer   : ioctl code reworked (vmalloc/vfree removed)
 */
#include <linux/string.h>
#include <linux/spinlock.h>

#include "sound_config.h"

static volatile int initialized, opened, tmr_running;
static volatile unsigned int tmr_offs, tmr_ctr;
static volatile unsigned long ticks_offs;
static volatile int curr_tempo, curr_timebase;
static volatile unsigned long curr_ticks;
static volatile unsigned long next_event_time;
static unsigned long prev_event_time;
static volatile unsigned long usecs_per_tmr;	/* Length of the current interval */

static struct sound_lowlev_timer *tmr;
static DEFINE_SPINLOCK(lock);

static unsigned long tmr2ticks(int tmr_value)
{
	/*
	 *    Convert timer ticks to MIDI ticks
	 */

	unsigned long tmp;
	unsigned long scale;

	tmp = tmr_value * usecs_per_tmr;	/* Convert to usecs */
	scale = (60 * 1000000) / (curr_tempo * curr_timebase);	/* usecs per MIDI tick */
	return (tmp + (scale / 2)) / scale;
}

void reprogram_timer(void)
{
	unsigned long   usecs_per_tick;

	/*
	 *	The user is changing the timer rate before setting a timer
	 *	slap, bad bad not allowed.
	 */
	 
	if(!tmr)
		return;
		
	usecs_per_tick = (60 * 1000000) / (curr_tempo * curr_timebase);

	/*
	 * Don't kill the system by setting too high timer rate
	 */
	if (usecs_per_tick < 2000)
		usecs_per_tick = 2000;

	usecs_per_tmr = tmr->tmr_start(tmr->dev, usecs_per_tick);
}

void sound_timer_syncinterval(unsigned int new_usecs)
{
	/*
	 *    This routine is called by the hardware level if
	 *      the clock frequency has changed for some reason.
	 */
	tmr_offs = tmr_ctr;
	ticks_offs += tmr2ticks(tmr_ctr);
	tmr_ctr = 0;
	usecs_per_tmr = new_usecs;
}
EXPORT_SYMBOL(sound_timer_syncinterval);

static void tmr_reset(void)
{
	unsigned long   flags;

	spin_lock_irqsave(&lock,flags);
	tmr_offs = 0;
	ticks_offs = 0;
	tmr_ctr = 0;
	next_event_time = (unsigned long) -1;
	prev_event_time = 0;
	curr_ticks = 0;
	spin_unlock_irqrestore(&lock,flags);
}

static int timer_open(int dev, int mode)
{
	if (opened)
		return -EBUSY;
	tmr_reset();
	curr_tempo = 60;
	curr_timebase = 100;
	opened = 1;
	reprogram_timer();
	return 0;
}

static void timer_close(int dev)
{
	opened = tmr_running = 0;
	tmr->tmr_disable(tmr->dev);
}

static int timer_event(int dev, unsigned char *event)
{
	unsigned char cmd = event[1];
	unsigned long parm = *(int *) &event[4];

	switch (cmd)
	{
		case TMR_WAIT_REL:
			parm += prev_event_time;
		case TMR_WAIT_ABS:
			if (parm > 0)
			{
				long time;

				if (parm <= curr_ticks)	/* It's the time */
					return TIMER_NOT_ARMED;
				time = parm;
				next_event_time = prev_event_time = time;
				return TIMER_ARMED;
			}
			break;

		case TMR_START:
			tmr_reset();
			tmr_running = 1;
			reprogram_timer();
			break;

		case TMR_STOP:
			tmr_running = 0;
			break;

		case TMR_CONTINUE:
			tmr_running = 1;
			reprogram_timer();
			break;

		case TMR_TEMPO:
			if (parm)
			{
				if (parm < 8)
					parm = 8;
				if (parm > 250)
					parm = 250;
				tmr_offs = tmr_ctr;
				ticks_offs += tmr2ticks(tmr_ctr);
				tmr_ctr = 0;
				curr_tempo = parm;
				reprogram_timer();
			}
			break;

		case TMR_ECHO:
			seq_copy_to_input(event, 8);
			break;

		default:;
	}
	return TIMER_NOT_ARMED;
}

static unsigned long timer_get_time(int dev)
{
	if (!opened)
		return 0;
	return curr_ticks;
}

static int timer_ioctl(int dev, unsigned int cmd, void __user *arg)
{
	int __user *p = arg;
	int val;

	switch (cmd) 
	{
		case SNDCTL_TMR_SOURCE:
			val = TMR_INTERNAL;
			break;

		case SNDCTL_TMR_START:
			tmr_reset();
			tmr_running = 1;
			return 0;
		
		case SNDCTL_TMR_STOP:
			tmr_running = 0;
			return 0;

		case SNDCTL_TMR_CONTINUE:
			tmr_running = 1;
			return 0;

		case SNDCTL_TMR_TIMEBASE:
			if (get_user(val, p))
				return -EFAULT;
			if (val) 
			{
				if (val < 1)
					val = 1;
				if (val > 1000)
					val = 1000;
				curr_timebase = val;
			}
			val = curr_timebase;
			break;

		case SNDCTL_TMR_TEMPO:
			if (get_user(val, p))
				return -EFAULT;
			if (val) 
			{
				if (val < 8)
					val = 8;
				if (val > 250)
					val = 250;
				tmr_offs = tmr_ctr;
				ticks_offs += tmr2ticks(tmr_ctr);
				tmr_ctr = 0;
				curr_tempo = val;
				reprogram_timer();
			}
			val = curr_tempo;
			break;

		case SNDCTL_SEQ_CTRLRATE:
			if (get_user(val, p))
				return -EFAULT;
			if (val != 0)	/* Can't change */
				return -EINVAL;
			val = ((curr_tempo * curr_timebase) + 30) / 60;
			break;
		
		case SNDCTL_SEQ_GETTIME:
			val = curr_ticks;
			break;
		
		case SNDCTL_TMR_METRONOME:
		default:
			return -EINVAL;
	}
	return put_user(val, p);
}

static void timer_arm(int dev, long time)
{
	if (time < 0)
		time = curr_ticks + 1;
	else if (time <= curr_ticks)	/* It's the time */
		return;

	next_event_time = prev_event_time = time;
	return;
}

static struct sound_timer_operations sound_timer =
{
	.owner		= THIS_MODULE,
	.info		= {"Sound Timer", 0},
	.priority	= 1,	/* Priority */
	.devlink	= 0,	/* Local device link */
	.open		= timer_open,
	.close		= timer_close,
	.event		= timer_event,
	.get_time	= timer_get_time,
	.ioctl		= timer_ioctl,
	.arm_timer	= timer_arm
};

void sound_timer_interrupt(void)
{
	unsigned long flags;
	
	if (!opened)
		return;

	tmr->tmr_restart(tmr->dev);

	if (!tmr_running)
		return;

	spin_lock_irqsave(&lock,flags);
	tmr_ctr++;
	curr_ticks = ticks_offs + tmr2ticks(tmr_ctr);

	if (curr_ticks >= next_event_time)
	{
		next_event_time = (unsigned long) -1;
		sequencer_timer(0);
	}
	spin_unlock_irqrestore(&lock,flags);
}
EXPORT_SYMBOL(sound_timer_interrupt);

void  sound_timer_init(struct sound_lowlev_timer *t, char *name)
{
	int n;

	if (initialized)
	{
		if (t->priority <= tmr->priority)
			return;	/* There is already a similar or better timer */
		tmr = t;
		return;
	}
	initialized = 1;
	tmr = t;

	n = sound_alloc_timerdev();
	if (n == -1)
		n = 0;		/* Overwrite the system timer */
	strlcpy(sound_timer.info.name, name, sizeof(sound_timer.info.name));
	sound_timer_devs[n] = &sound_timer;
}
EXPORT_SYMBOL(sound_timer_init);

