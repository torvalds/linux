/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Return hooking with list-based shadow stack.
 */
#ifndef _LINUX_RETHOOK_H
#define _LINUX_RETHOOK_H

#include <linux/compiler.h>
#include <linux/freelist.h>
#include <linux/kallsyms.h>
#include <linux/llist.h>
#include <linux/rcupdate.h>
#include <linux/refcount.h>

struct rethook_node;

typedef void (*rethook_handler_t) (struct rethook_node *, void *, struct pt_regs *);

/**
 * struct rethook - The rethook management data structure.
 * @data: The user-defined data storage.
 * @handler: The user-defined return hook handler.
 * @pool: The pool of struct rethook_node.
 * @ref: The reference counter.
 * @rcu: The rcu_head for deferred freeing.
 *
 * Don't embed to another data structure, because this is a self-destructive
 * data structure when all rethook_node are freed.
 */
struct rethook {
	void			*data;
	/*
	 * To avoid sparse warnings, this uses a raw function pointer with
	 * __rcu, instead of rethook_handler_t. But this must be same as
	 * rethook_handler_t.
	 */
	void (__rcu *handler) (struct rethook_node *, void *, struct pt_regs *);
	struct freelist_head	pool;
	refcount_t		ref;
	struct rcu_head		rcu;
};

/**
 * struct rethook_node - The rethook shadow-stack entry node.
 * @freelist: The freelist, linked to struct rethook::pool.
 * @rcu: The rcu_head for deferred freeing.
 * @llist: The llist, linked to a struct task_struct::rethooks.
 * @rethook: The pointer to the struct rethook.
 * @ret_addr: The storage for the real return address.
 * @frame: The storage for the frame pointer.
 *
 * You can embed this to your extended data structure to store any data
 * on each entry of the shadow stack.
 */
struct rethook_node {
	union {
		struct freelist_node freelist;
		struct rcu_head      rcu;
	};
	struct llist_node	llist;
	struct rethook		*rethook;
	unsigned long		ret_addr;
	unsigned long		frame;
};

struct rethook *rethook_alloc(void *data, rethook_handler_t handler);
void rethook_stop(struct rethook *rh);
void rethook_free(struct rethook *rh);
void rethook_add_node(struct rethook *rh, struct rethook_node *node);
struct rethook_node *rethook_try_get(struct rethook *rh);
void rethook_recycle(struct rethook_node *node);
void rethook_hook(struct rethook_node *node, struct pt_regs *regs, bool mcount);
unsigned long rethook_find_ret_addr(struct task_struct *tsk, unsigned long frame,
				    struct llist_node **cur);

/* Arch dependent code must implement arch_* and trampoline code */
void arch_rethook_prepare(struct rethook_node *node, struct pt_regs *regs, bool mcount);
void arch_rethook_trampoline(void);

/**
 * is_rethook_trampoline() - Check whether the address is rethook trampoline
 * @addr: The address to be checked
 *
 * Return true if the @addr is the rethook trampoline address.
 */
static inline bool is_rethook_trampoline(unsigned long addr)
{
	return addr == (unsigned long)dereference_symbol_descriptor(arch_rethook_trampoline);
}

/* If the architecture needs to fixup the return address, implement it. */
void arch_rethook_fixup_return(struct pt_regs *regs,
			       unsigned long correct_ret_addr);

/* Generic trampoline handler, arch code must prepare asm stub */
unsigned long rethook_trampoline_handler(struct pt_regs *regs,
					 unsigned long frame);

#ifdef CONFIG_RETHOOK
void rethook_flush_task(struct task_struct *tk);
#else
#define rethook_flush_task(tsk)	do { } while (0)
#endif

#endif

