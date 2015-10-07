#include <linux/slab.h>
#include <linux/spinlock.h>
#include <linux/once.h>
#include <linux/random.h>

struct __random_once_work {
	struct work_struct work;
	struct static_key *key;
};

static void __random_once_deferred(struct work_struct *w)
{
	struct __random_once_work *work;

	work = container_of(w, struct __random_once_work, work);
	BUG_ON(!static_key_enabled(work->key));
	static_key_slow_dec(work->key);
	kfree(work);
}

static void __random_once_disable_jump(struct static_key *key)
{
	struct __random_once_work *w;

	w = kmalloc(sizeof(*w), GFP_ATOMIC);
	if (!w)
		return;

	INIT_WORK(&w->work, __random_once_deferred);
	w->key = key;
	schedule_work(&w->work);
}

bool __get_random_once(void *buf, int nbytes, bool *done,
		       struct static_key *once_key)
{
	static DEFINE_SPINLOCK(lock);
	unsigned long flags;

	spin_lock_irqsave(&lock, flags);
	if (*done) {
		spin_unlock_irqrestore(&lock, flags);
		return false;
	}

	get_random_bytes(buf, nbytes);
	*done = true;
	spin_unlock_irqrestore(&lock, flags);

	__random_once_disable_jump(once_key);

	return true;
}
EXPORT_SYMBOL(__get_random_once);
