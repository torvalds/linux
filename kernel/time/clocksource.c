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
static LIST_HEAD(clocksource_list);
static DEFINE_SPINLOCK(clocksource_lock);
static char override_name[32];
static int finished_booting;

/* clocksource_done_booting - Called near the end of bootup
 *
 * Hack to avoid lots of clocksource churn at boot time
 */
static int __init clocksource_done_booting(void)
{
	finished_booting = 1;
	return 0;
}

late_initcall(clocksource_done_booting);

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
 * select_clocksource - Finds the best registered clocksource.
 *
 * Private function. Must hold clocksource_lock when called.
 *
 * Looks through the list of registered clocksources, returning
 * the one with the highest rating value. If there is a clocksource
 * name that matches the override string, it returns that clocksource.
 */
static struct clocksource *select_clocksource(void)
{
	struct clocksource *best = NULL;
	struct list_head *tmp;

	list_for_each(tmp, &clocksource_list) {
		struct clocksource *src;

		src = list_entry(tmp, struct clocksource, list);
		if (!best)
			best = src;

		/* check for override: */
		if (strlen(src->name) == strlen(override_name) &&
		    !strcmp(src->name, override_name)) {
			best = src;
			break;
		}
		/* pick the highest rating: */
		if (src->rating > best->rating)
		 	best = src;
	}

	return best;
}

/**
 * is_registered_source - Checks if clocksource is registered
 * @c:		pointer to a clocksource
 *
 * Private helper function. Must hold clocksource_lock when called.
 *
 * Returns one if the clocksource is already registered, zero otherwise.
 */
static int is_registered_source(struct clocksource *c)
{
	int len = strlen(c->name);
	struct list_head *tmp;

	list_for_each(tmp, &clocksource_list) {
		struct clocksource *src;

		src = list_entry(tmp, struct clocksource, list);
		if (strlen(src->name) == len &&	!strcmp(src->name, c->name))
			return 1;
	}

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
	int ret = 0;
	unsigned long flags;

	spin_lock_irqsave(&clocksource_lock, flags);
	/* check if clocksource is already registered */
	if (is_registered_source(c)) {
		printk("register_clocksource: Cannot register %s. "
			"Already registered!", c->name);
		ret = -EBUSY;
	} else {
		/* register it */
 		list_add(&c->list, &clocksource_list);
		/* scan the registered clocksources, and pick the best one */
		next_clocksource = select_clocksource();
	}
	spin_unlock_irqrestore(&clocksource_lock, flags);
	return ret;
}
EXPORT_SYMBOL(clocksource_register);

/**
 * clocksource_reselect - Rescan list for next clocksource
 *
 * A quick helper function to be used if a clocksource changes its
 * rating. Forces the clocksource list to be re-scanned for the best
 * clocksource.
 */
void clocksource_reselect(void)
{
	unsigned long flags;

	spin_lock_irqsave(&clocksource_lock, flags);
	next_clocksource = select_clocksource();
	spin_unlock_irqrestore(&clocksource_lock, flags);
}
EXPORT_SYMBOL(clocksource_reselect);

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
	size_t ret = count;
	/* strings from sysfs write are not 0 terminated! */
	if (count >= sizeof(override_name))
		return -EINVAL;

	/* strip of \n: */
	if (buf[count-1] == '\n')
		count--;
	if (count < 1)
		return -EINVAL;

	spin_lock_irq(&clocksource_lock);

	/* copy the name given: */
	memcpy(override_name, buf, count);
	override_name[count] = 0;

	/* try to select it: */
	next_clocksource = select_clocksource();

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
	struct list_head *tmp;
	char *curr = buf;

	spin_lock_irq(&clocksource_lock);
	list_for_each(tmp, &clocksource_list) {
		struct clocksource *src;

		src = list_entry(tmp, struct clocksource, list);
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
	set_kset_name("clocksource"),
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
