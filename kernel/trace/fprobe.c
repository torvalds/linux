// SPDX-License-Identifier: GPL-2.0
/*
 * fprobe - Simple ftrace probe wrapper for function entry.
 */
#define pr_fmt(fmt) "fprobe: " fmt

#include <linux/err.h>
#include <linux/fprobe.h>
#include <linux/kallsyms.h>
#include <linux/kprobes.h>
#include <linux/rethook.h>
#include <linux/slab.h>
#include <linux/sort.h>

#include "trace.h"

struct fprobe_rethook_node {
	struct rethook_node node;
	unsigned long entry_ip;
	unsigned long entry_parent_ip;
	char data[];
};

static inline void __fprobe_handler(unsigned long ip, unsigned long parent_ip,
			struct ftrace_ops *ops, struct ftrace_regs *fregs)
{
	struct fprobe_rethook_node *fpr;
	struct rethook_node *rh = NULL;
	struct fprobe *fp;
	void *entry_data = NULL;
	int ret = 0;

	fp = container_of(ops, struct fprobe, ops);

	if (fp->exit_handler) {
		rh = rethook_try_get(fp->rethook);
		if (!rh) {
			fp->nmissed++;
			return;
		}
		fpr = container_of(rh, struct fprobe_rethook_node, node);
		fpr->entry_ip = ip;
		fpr->entry_parent_ip = parent_ip;
		if (fp->entry_data_size)
			entry_data = fpr->data;
	}

	if (fp->entry_handler)
		ret = fp->entry_handler(fp, ip, parent_ip, ftrace_get_regs(fregs), entry_data);

	/* If entry_handler returns !0, nmissed is not counted. */
	if (rh) {
		if (ret)
			rethook_recycle(rh);
		else
			rethook_hook(rh, ftrace_get_regs(fregs), true);
	}
}

static void fprobe_handler(unsigned long ip, unsigned long parent_ip,
		struct ftrace_ops *ops, struct ftrace_regs *fregs)
{
	struct fprobe *fp;
	int bit;

	fp = container_of(ops, struct fprobe, ops);
	if (fprobe_disabled(fp))
		return;

	/* recursion detection has to go before any traceable function and
	 * all functions before this point should be marked as notrace
	 */
	bit = ftrace_test_recursion_trylock(ip, parent_ip);
	if (bit < 0) {
		fp->nmissed++;
		return;
	}
	__fprobe_handler(ip, parent_ip, ops, fregs);
	ftrace_test_recursion_unlock(bit);

}
NOKPROBE_SYMBOL(fprobe_handler);

static void fprobe_kprobe_handler(unsigned long ip, unsigned long parent_ip,
				  struct ftrace_ops *ops, struct ftrace_regs *fregs)
{
	struct fprobe *fp;
	int bit;

	fp = container_of(ops, struct fprobe, ops);
	if (fprobe_disabled(fp))
		return;

	/* recursion detection has to go before any traceable function and
	 * all functions called before this point should be marked as notrace
	 */
	bit = ftrace_test_recursion_trylock(ip, parent_ip);
	if (bit < 0) {
		fp->nmissed++;
		return;
	}

	/*
	 * This user handler is shared with other kprobes and is not expected to be
	 * called recursively. So if any other kprobe handler is running, this will
	 * exit as kprobe does. See the section 'Share the callbacks with kprobes'
	 * in Documentation/trace/fprobe.rst for more information.
	 */
	if (unlikely(kprobe_running())) {
		fp->nmissed++;
		goto recursion_unlock;
	}

	kprobe_busy_begin();
	__fprobe_handler(ip, parent_ip, ops, fregs);
	kprobe_busy_end();

recursion_unlock:
	ftrace_test_recursion_unlock(bit);
}

static void fprobe_exit_handler(struct rethook_node *rh, void *data,
				unsigned long ret_ip, struct pt_regs *regs)
{
	struct fprobe *fp = (struct fprobe *)data;
	struct fprobe_rethook_node *fpr;
	int bit;

	if (!fp || fprobe_disabled(fp))
		return;

	fpr = container_of(rh, struct fprobe_rethook_node, node);

	/*
	 * we need to assure no calls to traceable functions in-between the
	 * end of fprobe_handler and the beginning of fprobe_exit_handler.
	 */
	bit = ftrace_test_recursion_trylock(fpr->entry_ip, fpr->entry_parent_ip);
	if (bit < 0) {
		fp->nmissed++;
		return;
	}

	fp->exit_handler(fp, fpr->entry_ip, ret_ip, regs,
			 fp->entry_data_size ? (void *)fpr->data : NULL);
	ftrace_test_recursion_unlock(bit);
}
NOKPROBE_SYMBOL(fprobe_exit_handler);

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

static void fprobe_init(struct fprobe *fp)
{
	fp->nmissed = 0;
	if (fprobe_shared_with_kprobes(fp))
		fp->ops.func = fprobe_kprobe_handler;
	else
		fp->ops.func = fprobe_handler;
	fp->ops.flags |= FTRACE_OPS_FL_SAVE_REGS;
}

static int fprobe_init_rethook(struct fprobe *fp, int num)
{
	int i, size;

	if (num <= 0)
		return -EINVAL;

	if (!fp->exit_handler) {
		fp->rethook = NULL;
		return 0;
	}

	/* Initialize rethook if needed */
	if (fp->nr_maxactive)
		size = fp->nr_maxactive;
	else
		size = num * num_possible_cpus() * 2;
	if (size <= 0)
		return -EINVAL;

	fp->rethook = rethook_alloc((void *)fp, fprobe_exit_handler);
	if (!fp->rethook)
		return -ENOMEM;
	for (i = 0; i < size; i++) {
		struct fprobe_rethook_node *node;

		node = kzalloc(sizeof(*node) + fp->entry_data_size, GFP_KERNEL);
		if (!node) {
			rethook_free(fp->rethook);
			fp->rethook = NULL;
			return -ENOMEM;
		}
		rethook_add_node(fp->rethook, &node->node);
	}
	return 0;
}

static void fprobe_fail_cleanup(struct fprobe *fp)
{
	if (fp->rethook) {
		/* Don't need to cleanup rethook->handler because this is not used. */
		rethook_free(fp->rethook);
		fp->rethook = NULL;
	}
	ftrace_free_filter(&fp->ops);
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
	struct ftrace_hash *hash;
	unsigned char *str;
	int ret, len;

	if (!fp || !filter)
		return -EINVAL;

	fprobe_init(fp);

	len = strlen(filter);
	str = kstrdup(filter, GFP_KERNEL);
	ret = ftrace_set_filter(&fp->ops, str, len, 0);
	kfree(str);
	if (ret)
		return ret;

	if (notfilter) {
		len = strlen(notfilter);
		str = kstrdup(notfilter, GFP_KERNEL);
		ret = ftrace_set_notrace(&fp->ops, str, len, 0);
		kfree(str);
		if (ret)
			goto out;
	}

	/* TODO:
	 * correctly calculate the total number of filtered symbols
	 * from both filter and notfilter.
	 */
	hash = rcu_access_pointer(fp->ops.local_hash.filter_hash);
	if (WARN_ON_ONCE(!hash))
		goto out;

	ret = fprobe_init_rethook(fp, (int)hash->count);
	if (!ret)
		ret = register_ftrace_function(&fp->ops);

out:
	if (ret)
		fprobe_fail_cleanup(fp);
	return ret;
}
EXPORT_SYMBOL_GPL(register_fprobe);

/**
 * register_fprobe_ips() - Register fprobe to ftrace by address.
 * @fp: A fprobe data structure to be registered.
 * @addrs: An array of target ftrace location addresses.
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
	int ret;

	if (!fp || !addrs || num <= 0)
		return -EINVAL;

	fprobe_init(fp);

	ret = ftrace_set_filter_ips(&fp->ops, addrs, num, 0, 0);
	if (ret)
		return ret;

	ret = fprobe_init_rethook(fp, num);
	if (!ret)
		ret = register_ftrace_function(&fp->ops);

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
	if (!fp || (fp->ops.saved_func != fprobe_handler &&
		    fp->ops.saved_func != fprobe_kprobe_handler))
		return false;
	return true;
}

/**
 * unregister_fprobe() - Unregister fprobe from ftrace
 * @fp: A fprobe data structure to be unregistered.
 *
 * Unregister fprobe (and remove ftrace hooks from the function entries).
 *
 * Return 0 if @fp is unregistered successfully, -errno if not.
 */
int unregister_fprobe(struct fprobe *fp)
{
	int ret;

	if (!fprobe_is_registered(fp))
		return -EINVAL;

	if (fp->rethook)
		rethook_stop(fp->rethook);

	ret = unregister_ftrace_function(&fp->ops);
	if (ret < 0)
		return ret;

	if (fp->rethook)
		rethook_free(fp->rethook);

	ftrace_free_filter(&fp->ops);

	return ret;
}
EXPORT_SYMBOL_GPL(unregister_fprobe);
