#include <linux/lockdep.h>
#include <stdlib.h>

/* Trivial API wrappers, we don't (yet) have RCU in user-space: */
#define hlist_for_each_entry_rcu	hlist_for_each_entry
#define hlist_add_head_rcu		hlist_add_head
#define hlist_del_rcu			hlist_del

u32 prandom_u32(void)
{
	/* Used only by lock_pin_lock() which is dead code */
	abort();
}

#include "../../../kernel/locking/lockdep.c"
