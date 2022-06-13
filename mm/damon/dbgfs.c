// SPDX-License-Identifier: GPL-2.0
/*
 * DAMON Debugfs Interface
 *
 * Author: SeongJae Park <sjpark@amazon.de>
 */

#define pr_fmt(fmt) "damon-dbgfs: " fmt

#include <linux/damon.h>
#include <linux/debugfs.h>
#include <linux/file.h>
#include <linux/mm.h>
#include <linux/module.h>
#include <linux/page_idle.h>
#include <linux/slab.h>

static struct damon_ctx **dbgfs_ctxs;
static int dbgfs_nr_ctxs;
static struct dentry **dbgfs_dirs;
static DEFINE_MUTEX(damon_dbgfs_lock);

/*
 * Returns non-empty string on success, negative error code otherwise.
 */
static char *user_input_str(const char __user *buf, size_t count, loff_t *ppos)
{
	char *kbuf;
	ssize_t ret;

	/* We do not accept continuous write */
	if (*ppos)
		return ERR_PTR(-EINVAL);

	kbuf = kmalloc(count + 1, GFP_KERNEL | __GFP_NOWARN);
	if (!kbuf)
		return ERR_PTR(-ENOMEM);

	ret = simple_write_to_buffer(kbuf, count + 1, ppos, buf, count);
	if (ret != count) {
		kfree(kbuf);
		return ERR_PTR(-EIO);
	}
	kbuf[ret] = '\0';

	return kbuf;
}

static ssize_t dbgfs_attrs_read(struct file *file,
		char __user *buf, size_t count, loff_t *ppos)
{
	struct damon_ctx *ctx = file->private_data;
	char kbuf[128];
	int ret;

	mutex_lock(&ctx->kdamond_lock);
	ret = scnprintf(kbuf, ARRAY_SIZE(kbuf), "%lu %lu %lu %lu %lu\n",
			ctx->sample_interval, ctx->aggr_interval,
			ctx->ops_update_interval, ctx->min_nr_regions,
			ctx->max_nr_regions);
	mutex_unlock(&ctx->kdamond_lock);

	return simple_read_from_buffer(buf, count, ppos, kbuf, ret);
}

static ssize_t dbgfs_attrs_write(struct file *file,
		const char __user *buf, size_t count, loff_t *ppos)
{
	struct damon_ctx *ctx = file->private_data;
	unsigned long s, a, r, minr, maxr;
	char *kbuf;
	ssize_t ret;

	kbuf = user_input_str(buf, count, ppos);
	if (IS_ERR(kbuf))
		return PTR_ERR(kbuf);

	if (sscanf(kbuf, "%lu %lu %lu %lu %lu",
				&s, &a, &r, &minr, &maxr) != 5) {
		ret = -EINVAL;
		goto out;
	}

	mutex_lock(&ctx->kdamond_lock);
	if (ctx->kdamond) {
		ret = -EBUSY;
		goto unlock_out;
	}

	ret = damon_set_attrs(ctx, s, a, r, minr, maxr);
	if (!ret)
		ret = count;
unlock_out:
	mutex_unlock(&ctx->kdamond_lock);
out:
	kfree(kbuf);
	return ret;
}

/*
 * Return corresponding dbgfs' scheme action value (int) for the given
 * damos_action if the given damos_action value is valid and supported by
 * dbgfs, negative error code otherwise.
 */
static int damos_action_to_dbgfs_scheme_action(enum damos_action action)
{
	switch (action) {
	case DAMOS_WILLNEED:
		return 0;
	case DAMOS_COLD:
		return 1;
	case DAMOS_PAGEOUT:
		return 2;
	case DAMOS_HUGEPAGE:
		return 3;
	case DAMOS_NOHUGEPAGE:
		return 4;
	case DAMOS_STAT:
		return 5;
	default:
		return -EINVAL;
	}
}

static ssize_t sprint_schemes(struct damon_ctx *c, char *buf, ssize_t len)
{
	struct damos *s;
	int written = 0;
	int rc;

	damon_for_each_scheme(s, c) {
		rc = scnprintf(&buf[written], len - written,
				"%lu %lu %u %u %u %u %d %lu %lu %lu %u %u %u %d %lu %lu %lu %lu %lu %lu %lu %lu %lu\n",
				s->min_sz_region, s->max_sz_region,
				s->min_nr_accesses, s->max_nr_accesses,
				s->min_age_region, s->max_age_region,
				damos_action_to_dbgfs_scheme_action(s->action),
				s->quota.ms, s->quota.sz,
				s->quota.reset_interval,
				s->quota.weight_sz,
				s->quota.weight_nr_accesses,
				s->quota.weight_age,
				s->wmarks.metric, s->wmarks.interval,
				s->wmarks.high, s->wmarks.mid, s->wmarks.low,
				s->stat.nr_tried, s->stat.sz_tried,
				s->stat.nr_applied, s->stat.sz_applied,
				s->stat.qt_exceeds);
		if (!rc)
			return -ENOMEM;

		written += rc;
	}
	return written;
}

static ssize_t dbgfs_schemes_read(struct file *file, char __user *buf,
		size_t count, loff_t *ppos)
{
	struct damon_ctx *ctx = file->private_data;
	char *kbuf;
	ssize_t len;

	kbuf = kmalloc(count, GFP_KERNEL | __GFP_NOWARN);
	if (!kbuf)
		return -ENOMEM;

	mutex_lock(&ctx->kdamond_lock);
	len = sprint_schemes(ctx, kbuf, count);
	mutex_unlock(&ctx->kdamond_lock);
	if (len < 0)
		goto out;
	len = simple_read_from_buffer(buf, count, ppos, kbuf, len);

out:
	kfree(kbuf);
	return len;
}

static void free_schemes_arr(struct damos **schemes, ssize_t nr_schemes)
{
	ssize_t i;

	for (i = 0; i < nr_schemes; i++)
		kfree(schemes[i]);
	kfree(schemes);
}

/*
 * Return corresponding damos_action for the given dbgfs input for a scheme
 * action if the input is valid, negative error code otherwise.
 */
static enum damos_action dbgfs_scheme_action_to_damos_action(int dbgfs_action)
{
	switch (dbgfs_action) {
	case 0:
		return DAMOS_WILLNEED;
	case 1:
		return DAMOS_COLD;
	case 2:
		return DAMOS_PAGEOUT;
	case 3:
		return DAMOS_HUGEPAGE;
	case 4:
		return DAMOS_NOHUGEPAGE;
	case 5:
		return DAMOS_STAT;
	default:
		return -EINVAL;
	}
}

/*
 * Converts a string into an array of struct damos pointers
 *
 * Returns an array of struct damos pointers that converted if the conversion
 * success, or NULL otherwise.
 */
static struct damos **str_to_schemes(const char *str, ssize_t len,
				ssize_t *nr_schemes)
{
	struct damos *scheme, **schemes;
	const int max_nr_schemes = 256;
	int pos = 0, parsed, ret;
	unsigned long min_sz, max_sz;
	unsigned int min_nr_a, max_nr_a, min_age, max_age;
	unsigned int action_input;
	enum damos_action action;

	schemes = kmalloc_array(max_nr_schemes, sizeof(scheme),
			GFP_KERNEL);
	if (!schemes)
		return NULL;

	*nr_schemes = 0;
	while (pos < len && *nr_schemes < max_nr_schemes) {
		struct damos_quota quota = {};
		struct damos_watermarks wmarks;

		ret = sscanf(&str[pos],
				"%lu %lu %u %u %u %u %u %lu %lu %lu %u %u %u %u %lu %lu %lu %lu%n",
				&min_sz, &max_sz, &min_nr_a, &max_nr_a,
				&min_age, &max_age, &action_input, &quota.ms,
				&quota.sz, &quota.reset_interval,
				&quota.weight_sz, &quota.weight_nr_accesses,
				&quota.weight_age, &wmarks.metric,
				&wmarks.interval, &wmarks.high, &wmarks.mid,
				&wmarks.low, &parsed);
		if (ret != 18)
			break;
		action = dbgfs_scheme_action_to_damos_action(action_input);
		if ((int)action < 0)
			goto fail;

		if (min_sz > max_sz || min_nr_a > max_nr_a || min_age > max_age)
			goto fail;

		if (wmarks.high < wmarks.mid || wmarks.high < wmarks.low ||
		    wmarks.mid <  wmarks.low)
			goto fail;

		pos += parsed;
		scheme = damon_new_scheme(min_sz, max_sz, min_nr_a, max_nr_a,
				min_age, max_age, action, &quota, &wmarks);
		if (!scheme)
			goto fail;

		schemes[*nr_schemes] = scheme;
		*nr_schemes += 1;
	}
	return schemes;
fail:
	free_schemes_arr(schemes, *nr_schemes);
	return NULL;
}

static ssize_t dbgfs_schemes_write(struct file *file, const char __user *buf,
		size_t count, loff_t *ppos)
{
	struct damon_ctx *ctx = file->private_data;
	char *kbuf;
	struct damos **schemes;
	ssize_t nr_schemes = 0, ret;

	kbuf = user_input_str(buf, count, ppos);
	if (IS_ERR(kbuf))
		return PTR_ERR(kbuf);

	schemes = str_to_schemes(kbuf, count, &nr_schemes);
	if (!schemes) {
		ret = -EINVAL;
		goto out;
	}

	mutex_lock(&ctx->kdamond_lock);
	if (ctx->kdamond) {
		ret = -EBUSY;
		goto unlock_out;
	}

	ret = damon_set_schemes(ctx, schemes, nr_schemes);
	if (!ret) {
		ret = count;
		nr_schemes = 0;
	}

unlock_out:
	mutex_unlock(&ctx->kdamond_lock);
	free_schemes_arr(schemes, nr_schemes);
out:
	kfree(kbuf);
	return ret;
}

static ssize_t sprint_target_ids(struct damon_ctx *ctx, char *buf, ssize_t len)
{
	struct damon_target *t;
	int id;
	int written = 0;
	int rc;

	damon_for_each_target(t, ctx) {
		if (damon_target_has_pid(ctx))
			/* Show pid numbers to debugfs users */
			id = pid_vnr(t->pid);
		else
			/* Show 42 for physical address space, just for fun */
			id = 42;

		rc = scnprintf(&buf[written], len - written, "%d ", id);
		if (!rc)
			return -ENOMEM;
		written += rc;
	}
	if (written)
		written -= 1;
	written += scnprintf(&buf[written], len - written, "\n");
	return written;
}

static ssize_t dbgfs_target_ids_read(struct file *file,
		char __user *buf, size_t count, loff_t *ppos)
{
	struct damon_ctx *ctx = file->private_data;
	ssize_t len;
	char ids_buf[320];

	mutex_lock(&ctx->kdamond_lock);
	len = sprint_target_ids(ctx, ids_buf, 320);
	mutex_unlock(&ctx->kdamond_lock);
	if (len < 0)
		return len;

	return simple_read_from_buffer(buf, count, ppos, ids_buf, len);
}

/*
 * Converts a string into an integers array
 *
 * Returns an array of integers array if the conversion success, or NULL
 * otherwise.
 */
static int *str_to_ints(const char *str, ssize_t len, ssize_t *nr_ints)
{
	int *array;
	const int max_nr_ints = 32;
	int nr;
	int pos = 0, parsed, ret;

	*nr_ints = 0;
	array = kmalloc_array(max_nr_ints, sizeof(*array), GFP_KERNEL);
	if (!array)
		return NULL;
	while (*nr_ints < max_nr_ints && pos < len) {
		ret = sscanf(&str[pos], "%d%n", &nr, &parsed);
		pos += parsed;
		if (ret != 1)
			break;
		array[*nr_ints] = nr;
		*nr_ints += 1;
	}

	return array;
}

static void dbgfs_put_pids(struct pid **pids, int nr_pids)
{
	int i;

	for (i = 0; i < nr_pids; i++)
		put_pid(pids[i]);
}

/*
 * Converts a string into an struct pid pointers array
 *
 * Returns an array of struct pid pointers if the conversion success, or NULL
 * otherwise.
 */
static struct pid **str_to_pids(const char *str, ssize_t len, ssize_t *nr_pids)
{
	int *ints;
	ssize_t nr_ints;
	struct pid **pids;

	*nr_pids = 0;

	ints = str_to_ints(str, len, &nr_ints);
	if (!ints)
		return NULL;

	pids = kmalloc_array(nr_ints, sizeof(*pids), GFP_KERNEL);
	if (!pids)
		goto out;

	for (; *nr_pids < nr_ints; (*nr_pids)++) {
		pids[*nr_pids] = find_get_pid(ints[*nr_pids]);
		if (!pids[*nr_pids]) {
			dbgfs_put_pids(pids, *nr_pids);
			kfree(ints);
			kfree(pids);
			return NULL;
		}
	}

out:
	kfree(ints);
	return pids;
}

/*
 * dbgfs_set_targets() - Set monitoring targets.
 * @ctx:	monitoring context
 * @nr_targets:	number of targets
 * @pids:	array of target pids (size is same to @nr_targets)
 *
 * This function should not be called while the kdamond is running.  @pids is
 * ignored if the context is not configured to have pid in each target.  On
 * failure, reference counts of all pids in @pids are decremented.
 *
 * Return: 0 on success, negative error code otherwise.
 */
static int dbgfs_set_targets(struct damon_ctx *ctx, ssize_t nr_targets,
		struct pid **pids)
{
	ssize_t i;
	struct damon_target *t, *next;

	damon_for_each_target_safe(t, next, ctx) {
		if (damon_target_has_pid(ctx))
			put_pid(t->pid);
		damon_destroy_target(t);
	}

	for (i = 0; i < nr_targets; i++) {
		t = damon_new_target();
		if (!t) {
			damon_for_each_target_safe(t, next, ctx)
				damon_destroy_target(t);
			if (damon_target_has_pid(ctx))
				dbgfs_put_pids(pids, nr_targets);
			return -ENOMEM;
		}
		if (damon_target_has_pid(ctx))
			t->pid = pids[i];
		damon_add_target(ctx, t);
	}

	return 0;
}

static ssize_t dbgfs_target_ids_write(struct file *file,
		const char __user *buf, size_t count, loff_t *ppos)
{
	struct damon_ctx *ctx = file->private_data;
	bool id_is_pid = true;
	char *kbuf;
	struct pid **target_pids = NULL;
	ssize_t nr_targets;
	ssize_t ret;

	kbuf = user_input_str(buf, count, ppos);
	if (IS_ERR(kbuf))
		return PTR_ERR(kbuf);

	if (!strncmp(kbuf, "paddr\n", count)) {
		id_is_pid = false;
		nr_targets = 1;
	}

	if (id_is_pid) {
		target_pids = str_to_pids(kbuf, count, &nr_targets);
		if (!target_pids) {
			ret = -ENOMEM;
			goto out;
		}
	}

	mutex_lock(&ctx->kdamond_lock);
	if (ctx->kdamond) {
		if (id_is_pid)
			dbgfs_put_pids(target_pids, nr_targets);
		ret = -EBUSY;
		goto unlock_out;
	}

	/* remove previously set targets */
	dbgfs_set_targets(ctx, 0, NULL);
	if (!nr_targets) {
		ret = count;
		goto unlock_out;
	}

	/* Configure the context for the address space type */
	if (id_is_pid)
		ret = damon_select_ops(ctx, DAMON_OPS_VADDR);
	else
		ret = damon_select_ops(ctx, DAMON_OPS_PADDR);
	if (ret)
		goto unlock_out;

	ret = dbgfs_set_targets(ctx, nr_targets, target_pids);
	if (!ret)
		ret = count;

unlock_out:
	mutex_unlock(&ctx->kdamond_lock);
	kfree(target_pids);
out:
	kfree(kbuf);
	return ret;
}

static ssize_t sprint_init_regions(struct damon_ctx *c, char *buf, ssize_t len)
{
	struct damon_target *t;
	struct damon_region *r;
	int target_idx = 0;
	int written = 0;
	int rc;

	damon_for_each_target(t, c) {
		damon_for_each_region(r, t) {
			rc = scnprintf(&buf[written], len - written,
					"%d %lu %lu\n",
					target_idx, r->ar.start, r->ar.end);
			if (!rc)
				return -ENOMEM;
			written += rc;
		}
		target_idx++;
	}
	return written;
}

static ssize_t dbgfs_init_regions_read(struct file *file, char __user *buf,
		size_t count, loff_t *ppos)
{
	struct damon_ctx *ctx = file->private_data;
	char *kbuf;
	ssize_t len;

	kbuf = kmalloc(count, GFP_KERNEL | __GFP_NOWARN);
	if (!kbuf)
		return -ENOMEM;

	mutex_lock(&ctx->kdamond_lock);
	if (ctx->kdamond) {
		mutex_unlock(&ctx->kdamond_lock);
		len = -EBUSY;
		goto out;
	}

	len = sprint_init_regions(ctx, kbuf, count);
	mutex_unlock(&ctx->kdamond_lock);
	if (len < 0)
		goto out;
	len = simple_read_from_buffer(buf, count, ppos, kbuf, len);

out:
	kfree(kbuf);
	return len;
}

static int add_init_region(struct damon_ctx *c, int target_idx,
		struct damon_addr_range *ar)
{
	struct damon_target *t;
	struct damon_region *r, *prev;
	unsigned long idx = 0;
	int rc = -EINVAL;

	if (ar->start >= ar->end)
		return -EINVAL;

	damon_for_each_target(t, c) {
		if (idx++ == target_idx) {
			r = damon_new_region(ar->start, ar->end);
			if (!r)
				return -ENOMEM;
			damon_add_region(r, t);
			if (damon_nr_regions(t) > 1) {
				prev = damon_prev_region(r);
				if (prev->ar.end > r->ar.start) {
					damon_destroy_region(r, t);
					return -EINVAL;
				}
			}
			rc = 0;
		}
	}
	return rc;
}

static int set_init_regions(struct damon_ctx *c, const char *str, ssize_t len)
{
	struct damon_target *t;
	struct damon_region *r, *next;
	int pos = 0, parsed, ret;
	int target_idx;
	struct damon_addr_range ar;
	int err;

	damon_for_each_target(t, c) {
		damon_for_each_region_safe(r, next, t)
			damon_destroy_region(r, t);
	}

	while (pos < len) {
		ret = sscanf(&str[pos], "%d %lu %lu%n",
				&target_idx, &ar.start, &ar.end, &parsed);
		if (ret != 3)
			break;
		err = add_init_region(c, target_idx, &ar);
		if (err)
			goto fail;
		pos += parsed;
	}

	return 0;

fail:
	damon_for_each_target(t, c) {
		damon_for_each_region_safe(r, next, t)
			damon_destroy_region(r, t);
	}
	return err;
}

static ssize_t dbgfs_init_regions_write(struct file *file,
					  const char __user *buf, size_t count,
					  loff_t *ppos)
{
	struct damon_ctx *ctx = file->private_data;
	char *kbuf;
	ssize_t ret = count;
	int err;

	kbuf = user_input_str(buf, count, ppos);
	if (IS_ERR(kbuf))
		return PTR_ERR(kbuf);

	mutex_lock(&ctx->kdamond_lock);
	if (ctx->kdamond) {
		ret = -EBUSY;
		goto unlock_out;
	}

	err = set_init_regions(ctx, kbuf, ret);
	if (err)
		ret = err;

unlock_out:
	mutex_unlock(&ctx->kdamond_lock);
	kfree(kbuf);
	return ret;
}

static ssize_t dbgfs_kdamond_pid_read(struct file *file,
		char __user *buf, size_t count, loff_t *ppos)
{
	struct damon_ctx *ctx = file->private_data;
	char *kbuf;
	ssize_t len;

	kbuf = kmalloc(count, GFP_KERNEL | __GFP_NOWARN);
	if (!kbuf)
		return -ENOMEM;

	mutex_lock(&ctx->kdamond_lock);
	if (ctx->kdamond)
		len = scnprintf(kbuf, count, "%d\n", ctx->kdamond->pid);
	else
		len = scnprintf(kbuf, count, "none\n");
	mutex_unlock(&ctx->kdamond_lock);
	if (!len)
		goto out;
	len = simple_read_from_buffer(buf, count, ppos, kbuf, len);

out:
	kfree(kbuf);
	return len;
}

static int damon_dbgfs_open(struct inode *inode, struct file *file)
{
	file->private_data = inode->i_private;

	return nonseekable_open(inode, file);
}

static const struct file_operations attrs_fops = {
	.open = damon_dbgfs_open,
	.read = dbgfs_attrs_read,
	.write = dbgfs_attrs_write,
};

static const struct file_operations schemes_fops = {
	.open = damon_dbgfs_open,
	.read = dbgfs_schemes_read,
	.write = dbgfs_schemes_write,
};

static const struct file_operations target_ids_fops = {
	.open = damon_dbgfs_open,
	.read = dbgfs_target_ids_read,
	.write = dbgfs_target_ids_write,
};

static const struct file_operations init_regions_fops = {
	.open = damon_dbgfs_open,
	.read = dbgfs_init_regions_read,
	.write = dbgfs_init_regions_write,
};

static const struct file_operations kdamond_pid_fops = {
	.open = damon_dbgfs_open,
	.read = dbgfs_kdamond_pid_read,
};

static void dbgfs_fill_ctx_dir(struct dentry *dir, struct damon_ctx *ctx)
{
	const char * const file_names[] = {"attrs", "schemes", "target_ids",
		"init_regions", "kdamond_pid"};
	const struct file_operations *fops[] = {&attrs_fops, &schemes_fops,
		&target_ids_fops, &init_regions_fops, &kdamond_pid_fops};
	int i;

	for (i = 0; i < ARRAY_SIZE(file_names); i++)
		debugfs_create_file(file_names[i], 0600, dir, ctx, fops[i]);
}

static void dbgfs_before_terminate(struct damon_ctx *ctx)
{
	struct damon_target *t, *next;

	if (!damon_target_has_pid(ctx))
		return;

	mutex_lock(&ctx->kdamond_lock);
	damon_for_each_target_safe(t, next, ctx) {
		put_pid(t->pid);
		damon_destroy_target(t);
	}
	mutex_unlock(&ctx->kdamond_lock);
}

static struct damon_ctx *dbgfs_new_ctx(void)
{
	struct damon_ctx *ctx;

	ctx = damon_new_ctx();
	if (!ctx)
		return NULL;

	if (damon_select_ops(ctx, DAMON_OPS_VADDR) &&
			damon_select_ops(ctx, DAMON_OPS_PADDR)) {
		damon_destroy_ctx(ctx);
		return NULL;
	}
	ctx->callback.before_terminate = dbgfs_before_terminate;
	return ctx;
}

static void dbgfs_destroy_ctx(struct damon_ctx *ctx)
{
	damon_destroy_ctx(ctx);
}

/*
 * Make a context of @name and create a debugfs directory for it.
 *
 * This function should be called while holding damon_dbgfs_lock.
 *
 * Returns 0 on success, negative error code otherwise.
 */
static int dbgfs_mk_context(char *name)
{
	struct dentry *root, **new_dirs, *new_dir;
	struct damon_ctx **new_ctxs, *new_ctx;

	if (damon_nr_running_ctxs())
		return -EBUSY;

	new_ctxs = krealloc(dbgfs_ctxs, sizeof(*dbgfs_ctxs) *
			(dbgfs_nr_ctxs + 1), GFP_KERNEL);
	if (!new_ctxs)
		return -ENOMEM;
	dbgfs_ctxs = new_ctxs;

	new_dirs = krealloc(dbgfs_dirs, sizeof(*dbgfs_dirs) *
			(dbgfs_nr_ctxs + 1), GFP_KERNEL);
	if (!new_dirs)
		return -ENOMEM;
	dbgfs_dirs = new_dirs;

	root = dbgfs_dirs[0];
	if (!root)
		return -ENOENT;

	new_dir = debugfs_create_dir(name, root);
	dbgfs_dirs[dbgfs_nr_ctxs] = new_dir;

	new_ctx = dbgfs_new_ctx();
	if (!new_ctx) {
		debugfs_remove(new_dir);
		dbgfs_dirs[dbgfs_nr_ctxs] = NULL;
		return -ENOMEM;
	}

	dbgfs_ctxs[dbgfs_nr_ctxs] = new_ctx;
	dbgfs_fill_ctx_dir(dbgfs_dirs[dbgfs_nr_ctxs],
			dbgfs_ctxs[dbgfs_nr_ctxs]);
	dbgfs_nr_ctxs++;

	return 0;
}

static ssize_t dbgfs_mk_context_write(struct file *file,
		const char __user *buf, size_t count, loff_t *ppos)
{
	char *kbuf;
	char *ctx_name;
	ssize_t ret;

	kbuf = user_input_str(buf, count, ppos);
	if (IS_ERR(kbuf))
		return PTR_ERR(kbuf);
	ctx_name = kmalloc(count + 1, GFP_KERNEL);
	if (!ctx_name) {
		kfree(kbuf);
		return -ENOMEM;
	}

	/* Trim white space */
	if (sscanf(kbuf, "%s", ctx_name) != 1) {
		ret = -EINVAL;
		goto out;
	}

	mutex_lock(&damon_dbgfs_lock);
	ret = dbgfs_mk_context(ctx_name);
	if (!ret)
		ret = count;
	mutex_unlock(&damon_dbgfs_lock);

out:
	kfree(kbuf);
	kfree(ctx_name);
	return ret;
}

/*
 * Remove a context of @name and its debugfs directory.
 *
 * This function should be called while holding damon_dbgfs_lock.
 *
 * Return 0 on success, negative error code otherwise.
 */
static int dbgfs_rm_context(char *name)
{
	struct dentry *root, *dir, **new_dirs;
	struct damon_ctx **new_ctxs;
	int i, j;

	if (damon_nr_running_ctxs())
		return -EBUSY;

	root = dbgfs_dirs[0];
	if (!root)
		return -ENOENT;

	dir = debugfs_lookup(name, root);
	if (!dir)
		return -ENOENT;

	new_dirs = kmalloc_array(dbgfs_nr_ctxs - 1, sizeof(*dbgfs_dirs),
			GFP_KERNEL);
	if (!new_dirs)
		return -ENOMEM;

	new_ctxs = kmalloc_array(dbgfs_nr_ctxs - 1, sizeof(*dbgfs_ctxs),
			GFP_KERNEL);
	if (!new_ctxs) {
		kfree(new_dirs);
		return -ENOMEM;
	}

	for (i = 0, j = 0; i < dbgfs_nr_ctxs; i++) {
		if (dbgfs_dirs[i] == dir) {
			debugfs_remove(dbgfs_dirs[i]);
			dbgfs_destroy_ctx(dbgfs_ctxs[i]);
			continue;
		}
		new_dirs[j] = dbgfs_dirs[i];
		new_ctxs[j++] = dbgfs_ctxs[i];
	}

	kfree(dbgfs_dirs);
	kfree(dbgfs_ctxs);

	dbgfs_dirs = new_dirs;
	dbgfs_ctxs = new_ctxs;
	dbgfs_nr_ctxs--;

	return 0;
}

static ssize_t dbgfs_rm_context_write(struct file *file,
		const char __user *buf, size_t count, loff_t *ppos)
{
	char *kbuf;
	ssize_t ret;
	char *ctx_name;

	kbuf = user_input_str(buf, count, ppos);
	if (IS_ERR(kbuf))
		return PTR_ERR(kbuf);
	ctx_name = kmalloc(count + 1, GFP_KERNEL);
	if (!ctx_name) {
		kfree(kbuf);
		return -ENOMEM;
	}

	/* Trim white space */
	if (sscanf(kbuf, "%s", ctx_name) != 1) {
		ret = -EINVAL;
		goto out;
	}

	mutex_lock(&damon_dbgfs_lock);
	ret = dbgfs_rm_context(ctx_name);
	if (!ret)
		ret = count;
	mutex_unlock(&damon_dbgfs_lock);

out:
	kfree(kbuf);
	kfree(ctx_name);
	return ret;
}

static ssize_t dbgfs_monitor_on_read(struct file *file,
		char __user *buf, size_t count, loff_t *ppos)
{
	char monitor_on_buf[5];
	bool monitor_on = damon_nr_running_ctxs() != 0;
	int len;

	len = scnprintf(monitor_on_buf, 5, monitor_on ? "on\n" : "off\n");

	return simple_read_from_buffer(buf, count, ppos, monitor_on_buf, len);
}

static ssize_t dbgfs_monitor_on_write(struct file *file,
		const char __user *buf, size_t count, loff_t *ppos)
{
	ssize_t ret;
	char *kbuf;

	kbuf = user_input_str(buf, count, ppos);
	if (IS_ERR(kbuf))
		return PTR_ERR(kbuf);

	/* Remove white space */
	if (sscanf(kbuf, "%s", kbuf) != 1) {
		kfree(kbuf);
		return -EINVAL;
	}

	mutex_lock(&damon_dbgfs_lock);
	if (!strncmp(kbuf, "on", count)) {
		int i;

		for (i = 0; i < dbgfs_nr_ctxs; i++) {
			if (damon_targets_empty(dbgfs_ctxs[i])) {
				kfree(kbuf);
				mutex_unlock(&damon_dbgfs_lock);
				return -EINVAL;
			}
		}
		ret = damon_start(dbgfs_ctxs, dbgfs_nr_ctxs, true);
	} else if (!strncmp(kbuf, "off", count)) {
		ret = damon_stop(dbgfs_ctxs, dbgfs_nr_ctxs);
	} else {
		ret = -EINVAL;
	}
	mutex_unlock(&damon_dbgfs_lock);

	if (!ret)
		ret = count;
	kfree(kbuf);
	return ret;
}

static const struct file_operations mk_contexts_fops = {
	.write = dbgfs_mk_context_write,
};

static const struct file_operations rm_contexts_fops = {
	.write = dbgfs_rm_context_write,
};

static const struct file_operations monitor_on_fops = {
	.read = dbgfs_monitor_on_read,
	.write = dbgfs_monitor_on_write,
};

static int __init __damon_dbgfs_init(void)
{
	struct dentry *dbgfs_root;
	const char * const file_names[] = {"mk_contexts", "rm_contexts",
		"monitor_on"};
	const struct file_operations *fops[] = {&mk_contexts_fops,
		&rm_contexts_fops, &monitor_on_fops};
	int i;

	dbgfs_root = debugfs_create_dir("damon", NULL);

	for (i = 0; i < ARRAY_SIZE(file_names); i++)
		debugfs_create_file(file_names[i], 0600, dbgfs_root, NULL,
				fops[i]);
	dbgfs_fill_ctx_dir(dbgfs_root, dbgfs_ctxs[0]);

	dbgfs_dirs = kmalloc_array(1, sizeof(dbgfs_root), GFP_KERNEL);
	if (!dbgfs_dirs) {
		debugfs_remove(dbgfs_root);
		return -ENOMEM;
	}
	dbgfs_dirs[0] = dbgfs_root;

	return 0;
}

/*
 * Functions for the initialization
 */

static int __init damon_dbgfs_init(void)
{
	int rc = -ENOMEM;

	mutex_lock(&damon_dbgfs_lock);
	dbgfs_ctxs = kmalloc(sizeof(*dbgfs_ctxs), GFP_KERNEL);
	if (!dbgfs_ctxs)
		goto out;
	dbgfs_ctxs[0] = dbgfs_new_ctx();
	if (!dbgfs_ctxs[0]) {
		kfree(dbgfs_ctxs);
		goto out;
	}
	dbgfs_nr_ctxs = 1;

	rc = __damon_dbgfs_init();
	if (rc) {
		kfree(dbgfs_ctxs[0]);
		kfree(dbgfs_ctxs);
		pr_err("%s: dbgfs init failed\n", __func__);
	}

out:
	mutex_unlock(&damon_dbgfs_lock);
	return rc;
}

module_init(damon_dbgfs_init);

#include "dbgfs-test.h"
