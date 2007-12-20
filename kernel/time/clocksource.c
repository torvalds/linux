/*
 * linux/kernel/time/clocksource.c
 *
 * This file contains the functions which manage clocksource drivers.
 *
 * Copyright (C) 2004, 2005 IBM, John Stultz (johnstul@us.ibm.com)
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
 *
 * TODO WishList:
 *   o Allow clocksource drivers to be unregistered
 *   o get rid of clocksource_jiffies extern
 */

#include <linux/clocksource.h>
#include <linux/sysdev.h>
#include <linux/init.h>
#include <linux/module.h>
#include <linux/sched.h> /* for spin_unlock_irq() using preempt_count() m68k */
#include <linux/tick.h>

/* XXX - Would like a better way for initializing curr_clocksource */
extern struct clocksource clocksource_jiffies;

/*[Clocksource internal variables]---------
 * curr_clocksource:
 *	currently selected clocksource. Initialized to clocksource_jiffies.
 * next_clocksource:
 *	pending next selected clocksource.
 * clocksource_list:
 *	linked list with the registered clocksources
 * clocksource_lock:
 *	protects manipulations to curr_clocksource and next_clocksource
 *	and the clocksource_list
 * override_name:
 *	Name of the user-specified clocksource.
 */
static struct clocksource *curr_clocksource = &clocksource_jiffies;
static struct clocksource *next_clocksource;
static struct clocksource *clocksource_override;
static LIST_HEAD(clocksource_list);
static DEFINE_SPINLOCK(clocksource_lock);
static char override_name[32];
static int finished_booting;

/* clocksource_done_booting - Called near the end of core bootup
 *
 * Hack to avoid lots of clocksource churn at boot time.
 * We use fs_initcall because we want this to start before
 * device_initcall but after subsys_initcall.
 */
static int __init clocksource_done_booting(void)
{
	finished_booting = 1;
	return 0;
}
fs_initcall(clocksource_done_booting);

#ifdef CONFIG_CLOCKSOURCE_WATCHDOG
static LIST_HEAD(watchdog_list);
static struct clocksource *watchdog;
static struct timer_list watchdog_timer;
static DEFINE_SPINLOCK(watchdog_lock);
static cycle_t watchdog_last;
static unsigned long watchdog_resumed;

/*
 * Interval: 0.5sec Threshold: 0.0625s
 */
#define WATCHDOG_INTERVAL (HZ >> 1)
#define WATCHDOG_THRESHOLD (NSEC_PER_SEC >> 4)

static void clocksource_ratewd(struct clocksource *cs, int64_t delta)
{
	if (delta > -WATCHDOG_THRESHOLD && delta < WATCHDOG_THRESHOLD)
		return;

	printk(KERN_WARNING "Clocksource %s unstable (delta = %Ld ns)\n",
	       cs->name, delta);
	cs->flags &= ~(CLOCK_SOURCE_VALID_FOR_HRES | CLOCK_SOURCE_WATCHDOG);
	clocksource_change_rating(cs, 0);
	cs->flags &= ~CLOCK_SOURCE_WATCHDOG;
	list_del(&cs->wd_list);
}

static void clocksource_watchdog(unsigned long data)
{
	struct clocksource *cs, *tmp;
	cycle_t csnow, wdnow;
	int64_t wd_nsec, cs_nsec;
	int resumed;

	spin_lock(&watchdog_lock);

	resumed = test_and_clear_bit(0, &watchdog_resumed);

	wdnow = watchdog->read();
	wd_nsec = cyc2ns(watchdog, (wdnow - watchdog_last) & watchdog->mask);
	watchdog_last = wdnow;

	list_for_each_entry_safe(cs, tmp, &watchdog_list, wd_list) {
		csnow = cs->read();

		if (unlikely(resumed)) {
			cs->wd_last = csnow;
			continue;
		}

		/* Initialized ? */
		if (!(cs->flags & CLOCK_SOURCE_WATCHDOG)) {
			if ((cs->flags & CLOCK_SOURCE_IS_CONTINUOUS) &&
			    (watchdog->flags & CLOCK_SOURCE_IS_CONTINUOUS)) {
				cs->flags |= CLOCK_SOURCE_VALID_FOR_HRES;
				/*
				 * We just marked the clocksource as
				 * highres-capable, notify the rest of the
				 * system as well so that we transition
				 * into high-res mode:
				 */
				tick_clock_notify();
			}
			cs->flags |= CLOCK_SOURCE_WATCHDOG;
			cs->wd_last = csnow;
		} else {
			cs_nsec = cyc2ns(cs, (csnow - cs->wd_last) & cs->mask);
			cs->wd_last = csnow;
			/* Check the delta. Might remove from the list ! */
			clocksource_ratewd(cs, cs_nsec - wd_nsec);
		}
	}

	if (!list_empty(&watchdog_list)) {
		__mod_timer(&watchdog_timer,
			    watchdog_timer.expires + WATCHDOG_INTERVAL);
	}
	spin_unlock(&watchdog_lock);
}
static void clocksource_resume_watchdog(void)
{
	set_bit(0, &watchdog_resumed);
}

static void clocksource_check_watchdog(struct clocksource *cs)
{
	struct clocksource *cse;
	unsigned long flags;

	spin_lock_irqsave(&watchdog_lock, flags);
	if (cs->flags & CLOCK_SOURCE_MUST_VERIFY) {
		int started = !list_empty(&watchdog_list);

		list_add(&cs->wd_list, &watchdog_list);
		if (!started && watchdog) {
			watchdog_last = watchdog->read();
			watchdog_timer.expires = jiffies + WATCHDOG_INTERVAL;
			add_timer(&watchdog_timer);
		}
	} else {
		if (cs->flags & CLOCK_SOURCE_IS_CONTINUOUS)
			cs->flags |= CLOCK_SOURCE_VALID_FOR_HRES;

		if (!watchdog || cs->rating > watchdog->rating) {
			if (watchdog)
				del_timer(&watchdog_timer);
			watchdog = cs;
			init_timer(&watchdog_timer);
			watchdog_timer.function = clocksource_watchdog;

			/* Reset watchdog cycles */
			list_for_each_entry(cse, &watchdog_list, wd_list)
				cse->flags &= ~CLOCK_SOURCE_WATCHDOG;
			/* Start if list is not empty */
			if (!list_empty(&watchdog_list)) {
				watchdog_last = watchdog->read();
				watchdog_timer.expires =
					jiffies + WATCHDOG_INTERVAL;
				add_timer(&watchdog_timer);
			}
		}
	}
	spin_unlock_irqrestore(&watchdog_lock, flags);
}
#else
static void clocksource_check_watchdog(struct clocksource *cs)
{
	if (cs->flags & CLOCK_SOURCE_IS_CONTINUOUS)
		cs->flags |= CLOCK_SOURCE_VALID_FOR_HRES;
}

static inline void clocksource_resume_watchdog(void) { }
#endif

/**
 * clocksource_resume - resume the clocksource(s)
 */
void clocksource_resume(void)
{
	struct clocksource *cs;
	unsigned long flags;

	spin_lock_irqsave(&clocksource_lock, flags);

	list_for_each_entry(cs, &clocksource_list, list) {
		if (cs->resume)
			cs->resume();
	}

	clocksource_resume_watchdog();

	spin_unlock_irqrestore(&clocksource_lock, flags);
}

/**
 * clocksource_get_next - Returns the selected clocksource
 *
 */
struct clocksource *clocksource_get_next(void)
{
	unsigned long flags;

	spin_lock_irqsave(&clocksource_lock, flags);
	if (next_clocksource && finished_booting) {
		curr_clocksource = next_clocksource;
		next_clocksource = NULL;
	}
	spin_unlock_irqrestore(&clocksource_lock, flags);

	return curr_clocksource;
}

/**
 * select_clocksource - Selects the best registered clocksource.
 *
 * Private function. Must hold clocksource_lock when called.
 *
 * Select the clocksource with the best rating, or the clocksource,
 * which is selected by userspace override.
 */
static struct clocksource *select_clocksource(void)
{
	struct clocksource *next;

	if (list_empty(&clocksource_list))
		return NULL;

	if (clocksource_override)
		next = clocksource_override;
	else
		next = list_entry(clocksource_list.next, struct clocksource,
				  list);

	if (next == curr_clocksource)
		return NULL;

	return next;
}

/*
 * Enqueue the clocksource sorted by rating
 */
static int clocksource_enqueue(struct clocksource *c)
{
	struct list_head *tmp, *entry = &clocksource_list;

	list_for_each(tmp, &clocksource_list) {
		struct clocksource *cs;

		cs = list_entry(tmp, struct clocksource, list);
		if (cs == c)
			return -EBUSY;
		/* Keep track of the place, where to insert */
		if (cs->rating >= c->rating)
			entry = tmp;
	}
	list_add(&c->list, entry);

	if (strlen(c->name) == strlen(override_name) &&
	    !strcmp(c->name, override_name))
		clocksource_override = c;

	return 0;
}

/**
 * clocksource_register - Used to install new clocksources
 * @t:		clocksource to be registered
 *
 * Returns -EBUSY if registration fails, zero otherwise.
 */
int clocksource_register(struct clocksource *c)
{
	unsigned long flags;
	int ret;

	spin_lock_irqsave(&clocksource_lock, flags);
	ret = clocksource_enqueue(c);
	if (!ret)
		next_clocksource = select_clocksource();
	spin_unlock_irqrestore(&clocksource_lock, flags);
	if (!ret)
		clocksource_check_watchdog(c);
	return ret;
}
EXPORT_SYMBOL(clocksource_register);

/**
 * clocksource_change_rating - Change the rating of a registered clocksource
 *
 */
void clocksource_change_rating(struct clocksource *cs, int rating)
{
	unsigned long flags;

	spin_lock_irqsave(&clocksource_lock, flags);
	list_del(&cs->list);
	cs->rating = rating;
	clocksource_enqueue(cs);
	next_clocksource = select_clocksource();
	spin_unlock_irqrestore(&clocksource_lock, flags);
}

#ifdef CONFIG_SYSFS
/**
 * sysfs_show_current_clocksources - sysfs interface for current clocksource
 * @dev:	unused
 * @buf:	char buffer to be filled with clocksource list
 *
 * Provides sysfs interface for listing current clocksource.
 */
static ssize_t
sysfs_show_current_clocksources(struct sys_device *dev, char *buf)
{
	char *curr = buf;

	spin_lock_irq(&clocksource_lock);
	curr += sprintf(curr, "%s ", curr_clocksource->name);
	spin_unlock_irq(&clocksource_lock);

	curr += sprintf(curr, "\n");

	return curr - buf;
}

/**
 * sysfs_override_clocksource - interface for manually overriding clocksource
 * @dev:	unused
 * @buf:	name of override clocksource
 * @count:	length of buffer
 *
 * Takes input from sysfs interface for manually overriding the default
 * clocksource selction.
 */
static ssize_t sysfs_override_clocksource(struct sys_device *dev,
					  const char *buf, size_t count)
{
	struct clocksource *ovr = NULL;
	size_t ret = count;
	int len;

	/* strings from sysfs write are not 0 terminated! */
	if (count >= sizeof(override_name))
		return -EINVAL;

	/* strip of \n: */
	if (buf[count-1] == '\n')
		count--;

	spin_lock_irq(&clocksource_lock);

	if (count > 0)
		memcpy(override_name, buf, count);
	override_name[count] = 0;

	len = strlen(override_name);
	if (len) {
		struct clocksource *cs;

		ovr = clocksource_override;
		/* try to select it: */
		list_for_each_entry(cs, &clocksource_list, list) {
			if (strlen(cs->name) == len &&
			    !strcmp(cs->name, override_name))
				ovr = cs;
		}
	}

	/* Reselect, when the override name has changed */
	if (ovr != clocksource_override) {
		clocksource_override = ovr;
		next_clocksource = select_clocksource();
	}

	spin_unlock_irq(&clocksource_lock);

	return ret;
}

/**
 * sysfs_show_available_clocksources - sysfs interface for listing clocksource
 * @dev:	unused
 * @buf:	char buffer to be filled with clocksource list
 *
 * Provides sysfs interface for listing registered clocksources
 */
static ssize_t
sysfs_show_available_clocksources(struct sys_device *dev, char *buf)
{
	struct clocksource *src;
	char *curr = buf;

	spin_lock_irq(&clocksource_lock);
	list_for_each_entry(src, &clocksource_list, list) {
		curr += sprintf(curr, "%s ", src->name);
	}
	spin_unlock_irq(&clocksource_lock);

	curr += sprintf(curr, "\n");

	return curr - buf;
}

/*
 * Sysfs setup bits:
 */
static SYSDEV_ATTR(current_clocksource, 0600, sysfs_show_current_clocksources,
		   sysfs_override_clocksource);

static SYSDEV_ATTR(available_clocksource, 0600,
		   sysfs_show_available_clocksources, NULL);

static struct sysdev_class clocksource_sysclass = {
	.name = "clocksource",
};

static struct sys_device device_clocksource = {
	.id	= 0,
	.cls	= &clocksource_sysclass,
};

static int __init init_clocksource_sysfs(void)
{
	int error = sysdev_class_register(&clocksource_sysclass);

	if (!error)
		error = sysdev_register(&device_clocksource);
	if (!error)
		error = sysdev_create_file(
				&device_clocksource,
				&attr_current_clocksource);
	if (!error)
		error = sysdev_create_file(
				&device_clocksource,
				&attr_available_clocksource);
	return error;
}

device_initcall(init_clocksource_sysfs);
#endif /* CONFIG_SYSFS */

/**
 * boot_override_clocksource - boot clock override
 * @str:	override name
 *
 * Takes a clocksource= boot argument and uses it
 * as the clocksource override name.
 */
static int __init boot_override_clocksource(char* str)
{
	unsigned long flags;
	spin_lock_irqsave(&clocksource_lock, flags);
	if (str)
		strlcpy(override_name, str, sizeof(override_name));
	spin_unlock_irqrestore(&clocksource_lock, flags);
	return 1;
}

__setup("clocksource=", boot_override_clocksource);

/**
 * boot_override_clock - Compatibility layer for deprecated boot option
 * @str:	override name
 *
 * DEPRECATED! Takes a clock= boot argument and uses it
 * as the clocksource override name
 */
static int __init boot_override_clock(char* str)
{
	if (!strcmp(str, "pmtmr")) {
		printk("Warning: clock=pmtmr is deprecated. "
			"Use clocksource=acpi_pm.\n");
		return boot_override_clocksource("acpi_pm");
	}
	printk("Warning! clock= boot option is deprecated. "
		"Use clocksource=xyz\n");
	return boot_override_clocksource(str);
}

__setup("clock=", boot_override_clock);
