#include <linux/fs.h>

struct fs_pin {
	atomic_long_t		count;
	union {
		struct {
			struct hlist_node	s_list;
			struct hlist_node	m_list;
		};
		struct rcu_head rcu;
	};
	void (*kill)(struct fs_pin *);
};

void pin_put(struct fs_pin *);
void pin_remove(struct fs_pin *);
void pin_insert(struct fs_pin *, struct vfsmount *);
