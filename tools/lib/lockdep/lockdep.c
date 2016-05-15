#include <linux/lockdep.h>

/* Trivial API wrappers, we don't (yet) have RCU in user-space: */
#define hlist_for_each_entry_rcu	hlist_for_each_entry
#define hlist_add_head_rcu		hlist_add_head
#define hlist_del_rcu			hlist_del

#include "../../../kernel/locking/lockdep.c"
