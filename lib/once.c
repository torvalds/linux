// SPDX-License-Identifier: GPL-2.0
#include <linux/slab.h>
#include <linux/spinlock.h>
#include <linux/once.h>
#include <linux/random.h>
#include <linux/module.h>

struct once_work {
	struct work_struct work;
	struct static_key_true *key;
	struct module *module;
};

static void once_deferred(struct work_struct *w)
{
	struct once_work *work;

	work = container_of(w, struct once_work, work);
	BUG_ON(!static_key_enabled(work->key));
	static_branch_disable(work->key);
	module_put(work->module);
	kfree(work);
}

static void once_disable_jump(struct static_key_true *key, struct module *mod)
{
	struct once_work *w;

	w = kmalloc(sizeof(*w), GFP_ATOMIC);
	if (!w)
		return;

	INIT_WORK(&w->work, once_deferred);
	w->key = key;
	w->module = mod;
	__module_get(mod);
	schedule_work(&w->work);
}

static DEFINE_SPINLOCK(once_lock);

bool __do_once_start(bool *done, unsigned long *flags)
	__acquires(once_lock)
{
	spin_lock_irqsave(&once_lock, *flags);
	if (*done) {
		spin_unlock_irqrestore(&once_lock, *flags);
		/* Keep sparse happy by restoring an even lock count on
		 * this lock. In case we return here, we don't call into
		 * __do_once_done but return early in the DO_ONCE() macro.
		 */
		__acquire(once_lock);
		return false;
	}

	return true;
}
EXPORT_SYMBOL(__do_once_start);

void __do_once_done(bool *done, struct static_key_true *once_key,
		    unsigned long *flags, struct module *mod)
	__releases(once_lock)
{
	*done = true;
	spin_unlock_irqrestore(&once_lock, *flags);
	once_disable_jump(once_key, mod);
}
EXPORT_SYMBOL(__do_once_done);

static DEFINE_MUTEX(once_mutex);

bool __do_once_sleepable_start(bool *done)
	__acquires(once_mutex)
{
	mutex_lock(&once_mutex);
	if (*done) {
		mutex_unlock(&once_mutex);
		/* Keep sparse happy by restoring an even lock count on
		 * this mutex. In case we return here, we don't call into
		 * __do_once_done but return early in the DO_ONCE_SLEEPABLE() macro.
		 */
		__acquire(once_mutex);
		return false;
	}

	return true;
}
EXPORT_SYMBOL(__do_once_sleepable_start);

void __do_once_sleepable_done(bool *done, struct static_key_true *once_key,
			 struct module *mod)
	__releases(once_mutex)
{
	*done = true;
	mutex_unlock(&once_mutex);
	once_disable_jump(once_key, mod);
}
EXPORT_SYMBOL(__do_once_sleepable_done);
