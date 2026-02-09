// SPDX-License-Identifier: GPL-2.0
/*
 * fprobe - Simple ftrace probe wrapper for function entry.
 */
#define pr_fmt(fmt) "fprobe: " fmt

#include <linux/err.h>
#include <linux/fprobe.h>
#include <linux/kallsyms.h>
#include <linux/kprobes.h>
#include <linux/list.h>
#include <linux/mutex.h>
#include <linux/rhashtable.h>
#include <linux/slab.h>
#include <linux/sort.h>

#include <asm/fprobe.h>

#include "trace.h"

#define FPROBE_IP_HASH_BITS 8
#define FPROBE_IP_TABLE_SIZE (1 << FPROBE_IP_HASH_BITS)

#define FPROBE_HASH_BITS 6
#define FPROBE_TABLE_SIZE (1 << FPROBE_HASH_BITS)

#define SIZE_IN_LONG(x) ((x + sizeof(long) - 1) >> (sizeof(long) == 8 ? 3 : 2))

/*
 * fprobe_table: hold 'fprobe_hlist::hlist' for checking the fprobe still
 *   exists. The key is the address of fprobe instance.
 * fprobe_ip_table: hold 'fprobe_hlist::array[*]' for searching the fprobe
 *   instance related to the function address. The key is the ftrace IP
 *   address.
 *
 * When unregistering the fprobe, fprobe_hlist::fp and fprobe_hlist::array[*].fp
 * are set NULL and delete those from both hash tables (by hlist_del_rcu).
 * After an RCU grace period, the fprobe_hlist itself will be released.
 *
 * fprobe_table and fprobe_ip_table can be accessed from either
 *  - Normal hlist traversal and RCU add/del under 'fprobe_mutex' is held.
 *  - RCU hlist traversal under disabling preempt
 */
static struct hlist_head fprobe_table[FPROBE_TABLE_SIZE];
static struct rhltable fprobe_ip_table;
static DEFINE_MUTEX(fprobe_mutex);
static struct fgraph_ops fprobe_graph_ops;

static u32 fprobe_node_hashfn(const void *data, u32 len, u32 seed)
{
	return hash_ptr(*(unsigned long **)data, 32);
}

static int fprobe_node_cmp(struct rhashtable_compare_arg *arg,
			   const void *ptr)
{
	unsigned long key = *(unsigned long *)arg->key;
	const struct fprobe_hlist_node *n = ptr;

	return n->addr != key;
}

static u32 fprobe_node_obj_hashfn(const void *data, u32 len, u32 seed)
{
	const struct fprobe_hlist_node *n = data;

	return hash_ptr((void *)n->addr, 32);
}

static const struct rhashtable_params fprobe_rht_params = {
	.head_offset		= offsetof(struct fprobe_hlist_node, hlist),
	.key_offset		= offsetof(struct fprobe_hlist_node, addr),
	.key_len		= sizeof_field(struct fprobe_hlist_node, addr),
	.hashfn			= fprobe_node_hashfn,
	.obj_hashfn		= fprobe_node_obj_hashfn,
	.obj_cmpfn		= fprobe_node_cmp,
	.automatic_shrinking	= true,
};

/* Node insertion and deletion requires the fprobe_mutex */
static int insert_fprobe_node(struct fprobe_hlist_node *node)
{
	lockdep_assert_held(&fprobe_mutex);

	return rhltable_insert(&fprobe_ip_table, &node->hlist, fprobe_rht_params);
}

/* Return true if there are synonims */
static bool delete_fprobe_node(struct fprobe_hlist_node *node)
{
	lockdep_assert_held(&fprobe_mutex);
	bool ret;

	/* Avoid double deleting */
	if (READ_ONCE(node->fp) != NULL) {
		WRITE_ONCE(node->fp, NULL);
		rhltable_remove(&fprobe_ip_table, &node->hlist,
				fprobe_rht_params);
	}

	rcu_read_lock();
	ret = !!rhltable_lookup(&fprobe_ip_table, &node->addr,
				fprobe_rht_params);
	rcu_read_unlock();

	return ret;
}

/* Check existence of the fprobe */
static bool is_fprobe_still_exist(struct fprobe *fp)
{
	struct hlist_head *head;
	struct fprobe_hlist *fph;

	head = &fprobe_table[hash_ptr(fp, FPROBE_HASH_BITS)];
	hlist_for_each_entry_rcu(fph, head, hlist,
				 lockdep_is_held(&fprobe_mutex)) {
		if (fph->fp == fp)
			return true;
	}
	return false;
}
NOKPROBE_SYMBOL(is_fprobe_still_exist);

static int add_fprobe_hash(struct fprobe *fp)
{
	struct fprobe_hlist *fph = fp->hlist_array;
	struct hlist_head *head;

	lockdep_assert_held(&fprobe_mutex);

	if (WARN_ON_ONCE(!fph))
		return -EINVAL;

	if (is_fprobe_still_exist(fp))
		return -EEXIST;

	head = &fprobe_table[hash_ptr(fp, FPROBE_HASH_BITS)];
	hlist_add_head_rcu(&fp->hlist_array->hlist, head);
	return 0;
}

static int del_fprobe_hash(struct fprobe *fp)
{
	struct fprobe_hlist *fph = fp->hlist_array;

	lockdep_assert_held(&fprobe_mutex);

	if (WARN_ON_ONCE(!fph))
		return -EINVAL;

	if (!is_fprobe_still_exist(fp))
		return -ENOENT;

	fph->fp = NULL;
	hlist_del_rcu(&fph->hlist);
	return 0;
}

#ifdef ARCH_DEFINE_ENCODE_FPROBE_HEADER

/* The arch should encode fprobe_header info into one unsigned long */
#define FPROBE_HEADER_SIZE_IN_LONG	1

static inline bool write_fprobe_header(unsigned long *stack,
					struct fprobe *fp, unsigned int size_words)
{
	if (WARN_ON_ONCE(size_words > MAX_FPROBE_DATA_SIZE_WORD ||
			 !arch_fprobe_header_encodable(fp)))
		return false;

	*stack = arch_encode_fprobe_header(fp, size_words);
	return true;
}

static inline void read_fprobe_header(unsigned long *stack,
					struct fprobe **fp, unsigned int *size_words)
{
	*fp = arch_decode_fprobe_header_fp(*stack);
	*size_words = arch_decode_fprobe_header_size(*stack);
}

#else

/* Generic fprobe_header */
struct __fprobe_header {
	struct fprobe *fp;
	unsigned long size_words;
} __packed;

#define FPROBE_HEADER_SIZE_IN_LONG	SIZE_IN_LONG(sizeof(struct __fprobe_header))

static inline bool write_fprobe_header(unsigned long *stack,
					struct fprobe *fp, unsigned int size_words)
{
	struct __fprobe_header *fph = (struct __fprobe_header *)stack;

	if (WARN_ON_ONCE(size_words > MAX_FPROBE_DATA_SIZE_WORD))
		return false;

	fph->fp = fp;
	fph->size_words = size_words;
	return true;
}

static inline void read_fprobe_header(unsigned long *stack,
					struct fprobe **fp, unsigned int *size_words)
{
	struct __fprobe_header *fph = (struct __fprobe_header *)stack;

	*fp = fph->fp;
	*size_words = fph->size_words;
}

#endif

/*
 * fprobe shadow stack management:
 * Since fprobe shares a single fgraph_ops, it needs to share the stack entry
 * among the probes on the same function exit. Note that a new probe can be
 * registered before a target function is returning, we can not use the hash
 * table to find the corresponding probes. Thus the probe address is stored on
 * the shadow stack with its entry data size.
 *
 */
static inline int __fprobe_handler(unsigned long ip, unsigned long parent_ip,
				   struct fprobe *fp, struct ftrace_regs *fregs,
				   void *data)
{
	if (!fp->entry_handler)
		return 0;

	return fp->entry_handler(fp, ip, parent_ip, fregs, data);
}

static inline int __fprobe_kprobe_handler(unsigned long ip, unsigned long parent_ip,
					  struct fprobe *fp, struct ftrace_regs *fregs,
					  void *data)
{
	int ret;
	/*
	 * This user handler is shared with other kprobes and is not expected to be
	 * called recursively. So if any other kprobe handler is running, this will
	 * exit as kprobe does. See the section 'Share the callbacks with kprobes'
	 * in Documentation/trace/fprobe.rst for more information.
	 */
	if (unlikely(kprobe_running())) {
		fp->nmissed++;
		return 0;
	}

	kprobe_busy_begin();
	ret = __fprobe_handler(ip, parent_ip, fp, fregs, data);
	kprobe_busy_end();
	return ret;
}

#if defined(CONFIG_DYNAMIC_FTRACE_WITH_ARGS) || defined(CONFIG_DYNAMIC_FTRACE_WITH_REGS)
/* ftrace_ops callback, this processes fprobes which have only entry_handler. */
static void fprobe_ftrace_entry(unsigned long ip, unsigned long parent_ip,
	struct ftrace_ops *ops, struct ftrace_regs *fregs)
{
	struct fprobe_hlist_node *node;
	struct rhlist_head *head, *pos;
	struct fprobe *fp;
	int bit;

	bit = ftrace_test_recursion_trylock(ip, parent_ip);
	if (bit < 0)
		return;

	/*
	 * ftrace_test_recursion_trylock() disables preemption, but
	 * rhltable_lookup() checks whether rcu_read_lcok is held.
	 * So we take rcu_read_lock() here.
	 */
	rcu_read_lock();
	head = rhltable_lookup(&fprobe_ip_table, &ip, fprobe_rht_params);

	rhl_for_each_entry_rcu(node, pos, head, hlist) {
		if (node->addr != ip)
			break;
		fp = READ_ONCE(node->fp);
		if (unlikely(!fp || fprobe_disabled(fp) || fp->exit_handler))
			continue;

		if (fprobe_shared_with_kprobes(fp))
			__fprobe_kprobe_handler(ip, parent_ip, fp, fregs, NULL);
		else
			__fprobe_handler(ip, parent_ip, fp, fregs, NULL);
	}
	rcu_read_unlock();
	ftrace_test_recursion_unlock(bit);
}
NOKPROBE_SYMBOL(fprobe_ftrace_entry);

static struct ftrace_ops fprobe_ftrace_ops = {
	.func	= fprobe_ftrace_entry,
	.flags	= FTRACE_OPS_FL_SAVE_ARGS,
};
static int fprobe_ftrace_active;

static int fprobe_ftrace_add_ips(unsigned long *addrs, int num)
{
	int ret;

	lockdep_assert_held(&fprobe_mutex);

	ret = ftrace_set_filter_ips(&fprobe_ftrace_ops, addrs, num, 0, 0);
	if (ret)
		return ret;

	if (!fprobe_ftrace_active) {
		ret = register_ftrace_function(&fprobe_ftrace_ops);
		if (ret) {
			ftrace_free_filter(&fprobe_ftrace_ops);
			return ret;
		}
	}
	fprobe_ftrace_active++;
	return 0;
}

static void fprobe_ftrace_remove_ips(unsigned long *addrs, int num)
{
	lockdep_assert_held(&fprobe_mutex);

	fprobe_ftrace_active--;
	if (!fprobe_ftrace_active)
		unregister_ftrace_function(&fprobe_ftrace_ops);
	if (num)
		ftrace_set_filter_ips(&fprobe_ftrace_ops, addrs, num, 1, 0);
}

static bool fprobe_is_ftrace(struct fprobe *fp)
{
	return !fp->exit_handler;
}

#ifdef CONFIG_MODULES
static void fprobe_set_ips(unsigned long *ips, unsigned int cnt, int remove,
			   int reset)
{
	ftrace_set_filter_ips(&fprobe_graph_ops.ops, ips, cnt, remove, reset);
	ftrace_set_filter_ips(&fprobe_ftrace_ops, ips, cnt, remove, reset);
}
#endif
#else
static int fprobe_ftrace_add_ips(unsigned long *addrs, int num)
{
	return -ENOENT;
}

static void fprobe_ftrace_remove_ips(unsigned long *addrs, int num)
{
}

static bool fprobe_is_ftrace(struct fprobe *fp)
{
	return false;
}

#ifdef CONFIG_MODULES
static void fprobe_set_ips(unsigned long *ips, unsigned int cnt, int remove,
			   int reset)
{
	ftrace_set_filter_ips(&fprobe_graph_ops.ops, ips, cnt, remove, reset);
}
#endif
#endif /* !CONFIG_DYNAMIC_FTRACE_WITH_ARGS && !CONFIG_DYNAMIC_FTRACE_WITH_REGS */

/* fgraph_ops callback, this processes fprobes which have exit_handler. */
static int fprobe_fgraph_entry(struct ftrace_graph_ent *trace, struct fgraph_ops *gops,
			       struct ftrace_regs *fregs)
{
	unsigned long *fgraph_data = NULL;
	unsigned long func = trace->func;
	struct fprobe_hlist_node *node;
	struct rhlist_head *head, *pos;
	unsigned long ret_ip;
	int reserved_words;
	struct fprobe *fp;
	int used, ret;

	if (WARN_ON_ONCE(!fregs))
		return 0;

	guard(rcu)();
	head = rhltable_lookup(&fprobe_ip_table, &func, fprobe_rht_params);
	reserved_words = 0;
	rhl_for_each_entry_rcu(node, pos, head, hlist) {
		if (node->addr != func)
			continue;
		fp = READ_ONCE(node->fp);
		if (!fp || !fp->exit_handler)
			continue;
		/*
		 * Since fprobe can be enabled until the next loop, we ignore the
		 * fprobe's disabled flag in this loop.
		 */
		reserved_words +=
			FPROBE_HEADER_SIZE_IN_LONG + SIZE_IN_LONG(fp->entry_data_size);
	}
	if (reserved_words) {
		fgraph_data = fgraph_reserve_data(gops->idx, reserved_words * sizeof(long));
		if (unlikely(!fgraph_data)) {
			rhl_for_each_entry_rcu(node, pos, head, hlist) {
				if (node->addr != func)
					continue;
				fp = READ_ONCE(node->fp);
				if (fp && !fprobe_disabled(fp) && !fprobe_is_ftrace(fp))
					fp->nmissed++;
			}
			return 0;
		}
	}

	/*
	 * TODO: recursion detection has been done in the fgraph. Thus we need
	 * to add a callback to increment missed counter.
	 */
	ret_ip = ftrace_regs_get_return_address(fregs);
	used = 0;
	rhl_for_each_entry_rcu(node, pos, head, hlist) {
		int data_size;
		void *data;

		if (node->addr != func)
			continue;
		fp = READ_ONCE(node->fp);
		if (unlikely(!fp || fprobe_disabled(fp) || fprobe_is_ftrace(fp)))
			continue;

		data_size = fp->entry_data_size;
		if (data_size && fp->exit_handler)
			data = fgraph_data + used + FPROBE_HEADER_SIZE_IN_LONG;
		else
			data = NULL;

		if (fprobe_shared_with_kprobes(fp))
			ret = __fprobe_kprobe_handler(func, ret_ip, fp, fregs, data);
		else
			ret = __fprobe_handler(func, ret_ip, fp, fregs, data);

		/* If entry_handler returns !0, nmissed is not counted but skips exit_handler. */
		if (!ret && fp->exit_handler) {
			int size_words = SIZE_IN_LONG(data_size);

			if (write_fprobe_header(&fgraph_data[used], fp, size_words))
				used += FPROBE_HEADER_SIZE_IN_LONG + size_words;
		}
	}
	if (used < reserved_words)
		memset(fgraph_data + used, 0, reserved_words - used);

	/* If any exit_handler is set, data must be used. */
	return used != 0;
}
NOKPROBE_SYMBOL(fprobe_fgraph_entry);

static void fprobe_return(struct ftrace_graph_ret *trace,
			  struct fgraph_ops *gops,
			  struct ftrace_regs *fregs)
{
	unsigned long *fgraph_data = NULL;
	unsigned long ret_ip;
	struct fprobe *fp;
	int size, curr;
	int size_words;

	fgraph_data = (unsigned long *)fgraph_retrieve_data(gops->idx, &size);
	if (WARN_ON_ONCE(!fgraph_data))
		return;
	size_words = SIZE_IN_LONG(size);
	ret_ip = ftrace_regs_get_instruction_pointer(fregs);

	preempt_disable_notrace();

	curr = 0;
	while (size_words > curr) {
		read_fprobe_header(&fgraph_data[curr], &fp, &size);
		if (!fp)
			break;
		curr += FPROBE_HEADER_SIZE_IN_LONG;
		if (is_fprobe_still_exist(fp) && !fprobe_disabled(fp)) {
			if (WARN_ON_ONCE(curr + size > size_words))
				break;
			fp->exit_handler(fp, trace->func, ret_ip, fregs,
					 size ? fgraph_data + curr : NULL);
		}
		curr += size;
	}
	preempt_enable_notrace();
}
NOKPROBE_SYMBOL(fprobe_return);

static struct fgraph_ops fprobe_graph_ops = {
	.entryfunc	= fprobe_fgraph_entry,
	.retfunc	= fprobe_return,
};
static int fprobe_graph_active;

/* Add @addrs to the ftrace filter and register fgraph if needed. */
static int fprobe_graph_add_ips(unsigned long *addrs, int num)
{
	int ret;

	lockdep_assert_held(&fprobe_mutex);

	ret = ftrace_set_filter_ips(&fprobe_graph_ops.ops, addrs, num, 0, 0);
	if (ret)
		return ret;

	if (!fprobe_graph_active) {
		ret = register_ftrace_graph(&fprobe_graph_ops);
		if (WARN_ON_ONCE(ret)) {
			ftrace_free_filter(&fprobe_graph_ops.ops);
			return ret;
		}
	}
	fprobe_graph_active++;
	return 0;
}

/* Remove @addrs from the ftrace filter and unregister fgraph if possible. */
static void fprobe_graph_remove_ips(unsigned long *addrs, int num)
{
	lockdep_assert_held(&fprobe_mutex);

	fprobe_graph_active--;
	/* Q: should we unregister it ? */
	if (!fprobe_graph_active)
		unregister_ftrace_graph(&fprobe_graph_ops);

	if (num)
		ftrace_set_filter_ips(&fprobe_graph_ops.ops, addrs, num, 1, 0);
}

#ifdef CONFIG_MODULES

#define FPROBE_IPS_BATCH_INIT 8
/* instruction pointer address list */
struct fprobe_addr_list {
	int index;
	int size;
	unsigned long *addrs;
};

static int fprobe_addr_list_add(struct fprobe_addr_list *alist, unsigned long addr)
{
	unsigned long *addrs;

	/* Previously we failed to expand the list. */
	if (alist->index == alist->size)
		return -ENOSPC;

	alist->addrs[alist->index++] = addr;
	if (alist->index < alist->size)
		return 0;

	/* Expand the address list */
	addrs = kcalloc(alist->size * 2, sizeof(*addrs), GFP_KERNEL);
	if (!addrs)
		return -ENOMEM;

	memcpy(addrs, alist->addrs, alist->size * sizeof(*addrs));
	alist->size *= 2;
	kfree(alist->addrs);
	alist->addrs = addrs;

	return 0;
}

static void fprobe_remove_node_in_module(struct module *mod, struct fprobe_hlist_node *node,
					 struct fprobe_addr_list *alist)
{
	if (!within_module(node->addr, mod))
		return;
	if (delete_fprobe_node(node))
		return;
	/*
	 * If failed to update alist, just continue to update hlist.
	 * Therefore, at list user handler will not hit anymore.
	 */
	fprobe_addr_list_add(alist, node->addr);
}

/* Handle module unloading to manage fprobe_ip_table. */
static int fprobe_module_callback(struct notifier_block *nb,
				  unsigned long val, void *data)
{
	struct fprobe_addr_list alist = {.size = FPROBE_IPS_BATCH_INIT};
	struct fprobe_hlist_node *node;
	struct rhashtable_iter iter;
	struct module *mod = data;

	if (val != MODULE_STATE_GOING)
		return NOTIFY_DONE;

	alist.addrs = kcalloc(alist.size, sizeof(*alist.addrs), GFP_KERNEL);
	/* If failed to alloc memory, we can not remove ips from hash. */
	if (!alist.addrs)
		return NOTIFY_DONE;

	mutex_lock(&fprobe_mutex);
	rhltable_walk_enter(&fprobe_ip_table, &iter);
	do {
		rhashtable_walk_start(&iter);

		while ((node = rhashtable_walk_next(&iter)) && !IS_ERR(node))
			fprobe_remove_node_in_module(mod, node, &alist);

		rhashtable_walk_stop(&iter);
	} while (node == ERR_PTR(-EAGAIN));
	rhashtable_walk_exit(&iter);

	if (alist.index > 0)
		fprobe_set_ips(alist.addrs, alist.index, 1, 0);
	mutex_unlock(&fprobe_mutex);

	kfree(alist.addrs);

	return NOTIFY_DONE;
}

static struct notifier_block fprobe_module_nb = {
	.notifier_call = fprobe_module_callback,
	.priority = 0,
};

static int __init init_fprobe_module(void)
{
	return register_module_notifier(&fprobe_module_nb);
}
early_initcall(init_fprobe_module);
#endif

static int symbols_cmp(const void *a, const void *b)
{
	const char **str_a = (const char **) a;
	const char **str_b = (const char **) b;

	return strcmp(*str_a, *str_b);
}

/* Convert ftrace location address from symbols */
static unsigned long *get_ftrace_locations(const char **syms, int num)
{
	unsigned long *addrs;

	/* Convert symbols to symbol address */
	addrs = kcalloc(num, sizeof(*addrs), GFP_KERNEL);
	if (!addrs)
		return ERR_PTR(-ENOMEM);

	/* ftrace_lookup_symbols expects sorted symbols */
	sort(syms, num, sizeof(*syms), symbols_cmp, NULL);

	if (!ftrace_lookup_symbols(syms, num, addrs))
		return addrs;

	kfree(addrs);
	return ERR_PTR(-ENOENT);
}

struct filter_match_data {
	const char *filter;
	const char *notfilter;
	size_t index;
	size_t size;
	unsigned long *addrs;
	struct module **mods;
};

static int filter_match_callback(void *data, const char *name, unsigned long addr)
{
	struct filter_match_data *match = data;

	if (!glob_match(match->filter, name) ||
	    (match->notfilter && glob_match(match->notfilter, name)))
		return 0;

	if (!ftrace_location(addr))
		return 0;

	if (match->addrs) {
		struct module *mod = __module_text_address(addr);

		if (mod && !try_module_get(mod))
			return 0;

		match->mods[match->index] = mod;
		match->addrs[match->index] = addr;
	}
	match->index++;
	return match->index == match->size;
}

/*
 * Make IP list from the filter/no-filter glob patterns.
 * Return the number of matched symbols, or errno.
 * If @addrs == NULL, this just counts the number of matched symbols. If @addrs
 * is passed with an array, we need to pass the an @mods array of the same size
 * to increment the module refcount for each symbol.
 * This means we also need to call `module_put` for each element of @mods after
 * using the @addrs.
 */
static int get_ips_from_filter(const char *filter, const char *notfilter,
			       unsigned long *addrs, struct module **mods,
			       size_t size)
{
	struct filter_match_data match = { .filter = filter, .notfilter = notfilter,
		.index = 0, .size = size, .addrs = addrs, .mods = mods};
	int ret;

	if (addrs && !mods)
		return -EINVAL;

	ret = kallsyms_on_each_symbol(filter_match_callback, &match);
	if (ret < 0)
		return ret;
	if (IS_ENABLED(CONFIG_MODULES)) {
		ret = module_kallsyms_on_each_symbol(NULL, filter_match_callback, &match);
		if (ret < 0)
			return ret;
	}

	return match.index ?: -ENOENT;
}

static void fprobe_fail_cleanup(struct fprobe *fp)
{
	kfree(fp->hlist_array);
	fp->hlist_array = NULL;
}

/* Initialize the fprobe data structure. */
static int fprobe_init(struct fprobe *fp, unsigned long *addrs, int num)
{
	struct fprobe_hlist *hlist_array;
	unsigned long addr;
	int size, i;

	if (!fp || !addrs || num <= 0)
		return -EINVAL;

	size = ALIGN(fp->entry_data_size, sizeof(long));
	if (size > MAX_FPROBE_DATA_SIZE)
		return -E2BIG;
	fp->entry_data_size = size;

	hlist_array = kzalloc(struct_size(hlist_array, array, num), GFP_KERNEL);
	if (!hlist_array)
		return -ENOMEM;

	fp->nmissed = 0;

	hlist_array->size = num;
	fp->hlist_array = hlist_array;
	hlist_array->fp = fp;
	for (i = 0; i < num; i++) {
		hlist_array->array[i].fp = fp;
		addr = ftrace_location(addrs[i]);
		if (!addr) {
			fprobe_fail_cleanup(fp);
			return -ENOENT;
		}
		hlist_array->array[i].addr = addr;
	}
	return 0;
}

#define FPROBE_IPS_MAX	INT_MAX

int fprobe_count_ips_from_filter(const char *filter, const char *notfilter)
{
	return get_ips_from_filter(filter, notfilter, NULL, NULL, FPROBE_IPS_MAX);
}

/**
 * register_fprobe() - Register fprobe to ftrace by pattern.
 * @fp: A fprobe data structure to be registered.
 * @filter: A wildcard pattern of probed symbols.
 * @notfilter: A wildcard pattern of NOT probed symbols.
 *
 * Register @fp to ftrace for enabling the probe on the symbols matched to @filter.
 * If @notfilter is not NULL, the symbols matched the @notfilter are not probed.
 *
 * Return 0 if @fp is registered successfully, -errno if not.
 */
int register_fprobe(struct fprobe *fp, const char *filter, const char *notfilter)
{
	unsigned long *addrs __free(kfree) = NULL;
	struct module **mods __free(kfree) = NULL;
	int ret, num;

	if (!fp || !filter)
		return -EINVAL;

	num = get_ips_from_filter(filter, notfilter, NULL, NULL, FPROBE_IPS_MAX);
	if (num < 0)
		return num;

	addrs = kcalloc(num, sizeof(*addrs), GFP_KERNEL);
	if (!addrs)
		return -ENOMEM;

	mods = kcalloc(num, sizeof(*mods), GFP_KERNEL);
	if (!mods)
		return -ENOMEM;

	ret = get_ips_from_filter(filter, notfilter, addrs, mods, num);
	if (ret < 0)
		return ret;

	ret = register_fprobe_ips(fp, addrs, ret);

	for (int i = 0; i < num; i++) {
		if (mods[i])
			module_put(mods[i]);
	}
	return ret;
}
EXPORT_SYMBOL_GPL(register_fprobe);

/**
 * register_fprobe_ips() - Register fprobe to ftrace by address.
 * @fp: A fprobe data structure to be registered.
 * @addrs: An array of target function address.
 * @num: The number of entries of @addrs.
 *
 * Register @fp to ftrace for enabling the probe on the address given by @addrs.
 * The @addrs must be the addresses of ftrace location address, which may be
 * the symbol address + arch-dependent offset.
 * If you unsure what this mean, please use other registration functions.
 *
 * Return 0 if @fp is registered successfully, -errno if not.
 */
int register_fprobe_ips(struct fprobe *fp, unsigned long *addrs, int num)
{
	struct fprobe_hlist *hlist_array;
	int ret, i;

	ret = fprobe_init(fp, addrs, num);
	if (ret)
		return ret;

	mutex_lock(&fprobe_mutex);

	hlist_array = fp->hlist_array;
	if (fprobe_is_ftrace(fp))
		ret = fprobe_ftrace_add_ips(addrs, num);
	else
		ret = fprobe_graph_add_ips(addrs, num);

	if (!ret) {
		add_fprobe_hash(fp);
		for (i = 0; i < hlist_array->size; i++) {
			ret = insert_fprobe_node(&hlist_array->array[i]);
			if (ret)
				break;
		}
		/* fallback on insert error */
		if (ret) {
			for (i--; i >= 0; i--)
				delete_fprobe_node(&hlist_array->array[i]);
		}
	}
	mutex_unlock(&fprobe_mutex);

	if (ret)
		fprobe_fail_cleanup(fp);

	return ret;
}
EXPORT_SYMBOL_GPL(register_fprobe_ips);

/**
 * register_fprobe_syms() - Register fprobe to ftrace by symbols.
 * @fp: A fprobe data structure to be registered.
 * @syms: An array of target symbols.
 * @num: The number of entries of @syms.
 *
 * Register @fp to the symbols given by @syms array. This will be useful if
 * you are sure the symbols exist in the kernel.
 *
 * Return 0 if @fp is registered successfully, -errno if not.
 */
int register_fprobe_syms(struct fprobe *fp, const char **syms, int num)
{
	unsigned long *addrs;
	int ret;

	if (!fp || !syms || num <= 0)
		return -EINVAL;

	addrs = get_ftrace_locations(syms, num);
	if (IS_ERR(addrs))
		return PTR_ERR(addrs);

	ret = register_fprobe_ips(fp, addrs, num);

	kfree(addrs);

	return ret;
}
EXPORT_SYMBOL_GPL(register_fprobe_syms);

bool fprobe_is_registered(struct fprobe *fp)
{
	if (!fp || !fp->hlist_array)
		return false;
	return true;
}

/**
 * unregister_fprobe() - Unregister fprobe.
 * @fp: A fprobe data structure to be unregistered.
 *
 * Unregister fprobe (and remove ftrace hooks from the function entries).
 *
 * Return 0 if @fp is unregistered successfully, -errno if not.
 */
int unregister_fprobe(struct fprobe *fp)
{
	struct fprobe_hlist *hlist_array;
	unsigned long *addrs = NULL;
	int ret = 0, i, count;

	mutex_lock(&fprobe_mutex);
	if (!fp || !is_fprobe_still_exist(fp)) {
		ret = -EINVAL;
		goto out;
	}

	hlist_array = fp->hlist_array;
	addrs = kcalloc(hlist_array->size, sizeof(unsigned long), GFP_KERNEL);
	if (!addrs) {
		ret = -ENOMEM;	/* TODO: Fallback to one-by-one loop */
		goto out;
	}

	/* Remove non-synonim ips from table and hash */
	count = 0;
	for (i = 0; i < hlist_array->size; i++) {
		if (!delete_fprobe_node(&hlist_array->array[i]))
			addrs[count++] = hlist_array->array[i].addr;
	}
	del_fprobe_hash(fp);

	if (fprobe_is_ftrace(fp))
		fprobe_ftrace_remove_ips(addrs, count);
	else
		fprobe_graph_remove_ips(addrs, count);

	kfree_rcu(hlist_array, rcu);
	fp->hlist_array = NULL;

out:
	mutex_unlock(&fprobe_mutex);

	kfree(addrs);
	return ret;
}
EXPORT_SYMBOL_GPL(unregister_fprobe);

static int __init fprobe_initcall(void)
{
	rhltable_init(&fprobe_ip_table, &fprobe_rht_params);
	return 0;
}
core_initcall(fprobe_initcall);
