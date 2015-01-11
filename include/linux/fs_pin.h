#include <linux/fs.h>

struct fs_pin {
	struct hlist_node	s_list;
	struct hlist_node	m_list;
	void (*kill)(struct fs_pin *);
};

void pin_remove(struct fs_pin *);
void pin_insert_group(struct fs_pin *, struct vfsmount *, struct hlist_head *);
void pin_insert(struct fs_pin *, struct vfsmount *);
