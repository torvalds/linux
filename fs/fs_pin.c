#include <linux/fs.h>
#include <linux/slab.h>
#include <linux/fs_pin.h>
#include "internal.h"
#include "mount.h"

static DEFINE_SPINLOCK(pin_lock);

void pin_remove(struct fs_pin *pin)
{
	spin_lock(&pin_lock);
	hlist_del(&pin->m_list);
	hlist_del(&pin->s_list);
	spin_unlock(&pin_lock);
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
		pin->kill(pin);
	}
}

void group_pin_kill(struct hlist_head *p)
{
	while (1) {
		struct hlist_node *q;
		struct fs_pin *pin;
		rcu_read_lock();
		q = ACCESS_ONCE(p->first);
		if (!q) {
			rcu_read_unlock();
			break;
		}
		pin = hlist_entry(q, struct fs_pin, s_list);
		pin->kill(pin);
	}
}
