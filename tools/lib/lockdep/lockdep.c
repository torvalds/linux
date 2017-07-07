#include <linux/lockdep.h>
#include <stdlib.h>

/* Trivial API wrappers, we don't (yet) have RCU in user-space: */
#define hlist_for_each_entry_rcu	hlist_for_each_entry
#define hlist_add_head_rcu		hlist_add_head
#define hlist_del_rcu			hlist_del
#define list_for_each_entry_rcu		list_for_each_entry
#define list_add_tail_rcu		list_add_tail

u32 prandom_u32(void)
{
	/* Used only by lock_pin_lock() which is dead code */
	abort();
}

static struct new_utsname *init_utsname(void)
{
	static struct new_utsname n = (struct new_utsname) {
		.release = "liblockdep",
		.version = LIBLOCKDEP_VERSION,
	};

	return &n;
}

#include "../../../kernel/locking/lockdep.c"
