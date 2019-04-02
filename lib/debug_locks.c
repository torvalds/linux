/*
 * lib/de_locks.c
 *
 * Generic place for common deging facilities for various locks:
 * spinlocks, rwlocks, mutexes and rwsems.
 *
 * Started by Ingo Molnar:
 *
 *  Copyright (C) 2006 Red Hat, Inc., Ingo Molnar <mingo@redhat.com>
 */
#include <linux/rwsem.h>
#include <linux/mutex.h>
#include <linux/export.h>
#include <linux/spinlock.h>
#include <linux/de_locks.h>

/*
 * We want to turn all lock-deging facilities on/off at once,
 * via a global flag. The reason is that once a single  has been
 * detected and reported, there might be cascade of followup s
 * that would just muddy the log. So we report the first one and
 * shut up after that.
 */
int de_locks __read_mostly = 1;
EXPORT_SYMBOL_GPL(de_locks);

/*
 * The locking-testsuite uses <de_locks_silent> to get a
 * 'silent failure': nothing is printed to the console when
 * a locking  is detected.
 */
int de_locks_silent __read_mostly;
EXPORT_SYMBOL_GPL(de_locks_silent);

/*
 * Generic 'turn off all lock deging' function:
 */
int de_locks_off(void)
{
	if (de_locks && __de_locks_off()) {
		if (!de_locks_silent) {
			console_verbose();
			return 1;
		}
	}
	return 0;
}
EXPORT_SYMBOL_GPL(de_locks_off);
