#include <linux/fs.h>
#include <linux/sched.h>
#include <linux/slab.h>
#include "internal.h"
#include "mount.h"

static DEFINE_SPINLOCK(pin_lock);

void pin_remove(struct fs_pin *pin)
{
	spin_lock(&pin_lock);
	hlist_del(&pin->m_list);
	hlist_del(&pin->s_list);
	spin_unlock(&pin_lock);
	spin_lock_irq(&pin->wait.lock);
	pin->done = 1;
	wake_up_locked(&pin->wait);
	spin_unlock_irq(&pin->wait.lock);
}

void pin_insert_group(struct fs_pin *pin, struct vfsmount *m, struct hlist_head *p)
{
	spin_lock(&pin_lock);
	if (p)
		hlist_add_head(&pin->s_list, p);
	hlist_add_head(&pin->m_list, &real_mount(m)->mnt_pins);
	spin_unlock(&pin_lock);
}

void pin_insert(struct fs_pin *pin, struct vfsmount *m)
{
	pin_insert_group(pin, m, &m->mnt_sb->s_pins);
}

void pin_kill(struct fs_pin *p)
{
	wait_queue_t wait;

	if (!p) {
		rcu_read_unlock();
		return;
	}
	init_wait(&wait);
	spin_lock_irq(&p->wait.lock);
	if (likely(!p->done)) {
		p->done = -1;
		spin_unlock_irq(&p->wait.lock);
		rcu_read_unlock();
		p->kill(p);
		return;
	}
	if (p->done > 0) {
		spin_unlock_irq(&p->wait.lock);
		rcu_read_unlock();
		return;
	}
	__add_wait_queue(&p->wait, &wait);
	while (1) {
		set_current_state(TASK_UNINTERRUPTIBLE);
		spin_unlock_irq(&p->wait.lock);
		rcu_read_unlock();
		schedule();
		rcu_read_lock();
		if (likely(list_empty(&wait.task_list)))
			break;
		/* OK, we know p couldn't have been freed yet */
		spin_lock_irq(&p->wait.lock);
		if (p->done > 0) {
			spin_unlock_irq(&p->wait.lock);
			break;
		}
	}
	rcu_read_unlock();
}

void mnt_pin_kill(struct mount *m)
{
	while (1) {
		struct hlist_node *p;
		rcu_read_lock();
		p = ACCESS_ONCE(m->mnt_pins.first);
		if (!p) {
			rcu_read_unlock();
			break;
		}
		pin_kill(hlist_entry(p, struct fs_pin, m_list));
	}
}

void group_pin_kill(struct hlist_head *p)
{
	while (1) {
		struct hlist_node *q;
		rcu_read_lock();
		q = ACCESS_ONCE(p->first);
		if (!q) {
			rcu_read_unlock();
			break;
		}
		pin_kill(hlist_entry(q, struct fs_pin, s_list));
	}
}
