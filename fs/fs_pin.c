#include <linux/fs.h>
#include <linux/slab.h>
#include <linux/fs_pin.h>
#include "internal.h"
#include "mount.h"

static void pin_free_rcu(struct rcu_head *head)
{
	kfree(container_of(head, struct fs_pin, rcu));
}

static DEFINE_SPINLOCK(pin_lock);

void pin_put(struct fs_pin *p)
{
	if (atomic_long_dec_and_test(&p->count))
		call_rcu(&p->rcu, pin_free_rcu);
}

void pin_remove(struct fs_pin *pin)
{
	spin_lock(&pin_lock);
	hlist_del(&pin->m_list);
	hlist_del(&pin->s_list);
	spin_unlock(&pin_lock);
}

void pin_insert(struct fs_pin *pin, struct vfsmount *m)
{
	spin_lock(&pin_lock);
	hlist_add_head(&pin->s_list, &m->mnt_sb->s_pins);
	hlist_add_head(&pin->m_list, &real_mount(m)->mnt_pins);
	spin_unlock(&pin_lock);
}

void mnt_pin_kill(struct mount *m)
{
	while (1) {
		struct hlist_node *p;
		struct fs_pin *pin;
		rcu_read_lock();
		p = ACCESS_ONCE(m->mnt_pins.first);
		if (!p) {
			rcu_read_unlock();
			break;
		}
		pin = hlist_entry(p, struct fs_pin, m_list);
		if (!atomic_long_inc_not_zero(&pin->count)) {
			rcu_read_unlock();
			cpu_relax();
			continue;
		}
		rcu_read_unlock();
		pin->kill(pin);
	}
}

void sb_pin_kill(struct super_block *sb)
{
	while (1) {
		struct hlist_node *p;
		struct fs_pin *pin;
		rcu_read_lock();
		p = ACCESS_ONCE(sb->s_pins.first);
		if (!p) {
			rcu_read_unlock();
			break;
		}
		pin = hlist_entry(p, struct fs_pin, s_list);
		if (!atomic_long_inc_not_zero(&pin->count)) {
			rcu_read_unlock();
			cpu_relax();
			continue;
		}
		rcu_read_unlock();
		pin->kill(pin);
	}
}
