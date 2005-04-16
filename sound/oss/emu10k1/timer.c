
/*
 **********************************************************************
 *     timer.c
 *     Copyright (C) 1999, 2000 Creative Labs, inc.
 *
 **********************************************************************
 *
 *     This program is free software; you can redistribute it and/or
 *     modify it under the terms of the GNU General Public License as
 *     published by the Free Software Foundation; either version 2 of
 *     the License, or (at your option) any later version.
 *
 *     This program is distributed in the hope that it will be useful,
 *     but WITHOUT ANY WARRANTY; without even the implied warranty of
 *     MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *     GNU General Public License for more details.
 *
 *     You should have received a copy of the GNU General Public
 *     License along with this program; if not, write to the Free
 *     Software Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139,
 *     USA.
 *
 **********************************************************************
 */

/* 3/6/2000	Improved support for different timer delays  Rui Sousa */

/* 4/3/2000	Implemented timer list using list.h 	     Rui Sousa */

#include "hwaccess.h"
#include "8010.h"
#include "irqmgr.h"
#include "timer.h"

/* Try to schedule only once per fragment */

void emu10k1_timer_irqhandler(struct emu10k1_card *card)
{
	struct emu_timer *t;
	struct list_head *entry;

	spin_lock(&card->timer_lock);

	list_for_each(entry, &card->timers) {
		t = list_entry(entry, struct emu_timer, list);

		if (t->state & TIMER_STATE_ACTIVE) {
			t->count++;
			if (t->count == t->count_max) {
				t->count = 0;
				tasklet_hi_schedule(&t->tasklet);
			}
		}
	}

	spin_unlock(&card->timer_lock);

	return;
}

void emu10k1_timer_install(struct emu10k1_card *card, struct emu_timer *timer, u16 delay)
{
	struct emu_timer *t;
	struct list_head *entry;
	unsigned long flags;

	if (delay < 5)
		delay = 5;

	timer->delay = delay;
	timer->state = TIMER_STATE_INSTALLED;

	spin_lock_irqsave(&card->timer_lock, flags);

	timer->count_max = timer->delay / (card->timer_delay < 1024 ? card->timer_delay : 1024);
	timer->count = timer->count_max - 1;

	list_add(&timer->list, &card->timers);

	if (card->timer_delay > delay) {
		if (card->timer_delay == TIMER_STOPPED)
			emu10k1_irq_enable(card, INTE_INTERVALTIMERENB);

		card->timer_delay = delay;
		delay = (delay < 1024 ? delay : 1024);

		emu10k1_timer_set(card, delay);

		list_for_each(entry, &card->timers) {
			t = list_entry(entry, struct emu_timer, list);

			t->count_max = t->delay / delay;
			/* don't want to think much, just force scheduling 
			   on the next interrupt */
			t->count = t->count_max - 1;
		}

		DPD(2, "timer rate --> %u\n", delay);
	}

	spin_unlock_irqrestore(&card->timer_lock, flags);

	return;
}

void emu10k1_timer_uninstall(struct emu10k1_card *card, struct emu_timer *timer)
{
	struct emu_timer *t;
	struct list_head *entry;
	u16 delay = TIMER_STOPPED;
	unsigned long flags;

	if (timer->state == TIMER_STATE_UNINSTALLED)
		return;

	spin_lock_irqsave(&card->timer_lock, flags);

	list_del(&timer->list);

	list_for_each(entry, &card->timers) {
		t = list_entry(entry, struct emu_timer, list);

		if (t->delay < delay)
			delay = t->delay;
	}

	if (card->timer_delay != delay) {
		card->timer_delay = delay;

		if (delay == TIMER_STOPPED)
			emu10k1_irq_disable(card, INTE_INTERVALTIMERENB);
		else {
			delay = (delay < 1024 ? delay : 1024);

			emu10k1_timer_set(card, delay);

			list_for_each(entry, &card->timers) {
				t = list_entry(entry, struct emu_timer, list);

				t->count_max = t->delay / delay;
				t->count = t->count_max - 1;
			}
		}

		DPD(2, "timer rate --> %u\n", delay);
	}

	spin_unlock_irqrestore(&card->timer_lock, flags);

	timer->state = TIMER_STATE_UNINSTALLED;

	return;
}

void emu10k1_timer_enable(struct emu10k1_card *card, struct emu_timer *timer)
{
	unsigned long flags;

	spin_lock_irqsave(&card->timer_lock, flags);
	timer->state |= TIMER_STATE_ACTIVE;
	spin_unlock_irqrestore(&card->timer_lock, flags);

	return;
}

void emu10k1_timer_disable(struct emu10k1_card *card, struct emu_timer *timer)
{
	unsigned long flags;

	spin_lock_irqsave(&card->timer_lock, flags);
	timer->state &= ~TIMER_STATE_ACTIVE;
	spin_unlock_irqrestore(&card->timer_lock, flags);

	return;
}
