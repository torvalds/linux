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

	kbuf = kmalloc(count + 1, GFP_KERNEL);
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
			ctx->primitive_update_interval, ctx->min_nr_regions,
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

static ssize_t sprint_schemes(struct damon_ctx *c, char *buf, ssize_t len)
{
	struct damos *s;
	int written = 0;
	int rc;

	damon_for_each_scheme(s, c) {
		rc = scnprintf(&buf[written], len - written,
				"%lu %lu %u %u %u %u %d %lu %lu %lu %lu %lu\n",
				s->min_sz_region, s->max_sz_region,
				s->min_nr_accesses, s->max_nr_accesses,
				s->min_age_region, s->max_age_region,
				s->action,
				s->quota.ms, s->quota.sz,
				s->quota.reset_interval,
				s->stat_count, s->stat_sz);
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

	kbuf = kmalloc(count, GFP_KERNEL);
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

static bool damos_action_valid(int action)
{
	switch (action) {
	case DAMOS_WILLNEED:
	case DAMOS_COLD:
	case DAMOS_PAGEOUT:
	case DAMOS_HUGEPAGE:
	case DAMOS_NOHUGEPAGE:
	case DAMOS_STAT:
		return true;
	default:
		return false;
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
	unsigned int action;

	schemes = kmalloc_array(max_nr_schemes, sizeof(scheme),
			GFP_KERNEL);
	if (!schemes)
		return NULL;

	*nr_schemes = 0;
	while (pos < len && *nr_schemes < max_nr_schemes) {
		struct damos_quota quota = {};

		ret = sscanf(&str[pos], "%lu %lu %u %u %u %u %u %lu %lu %lu%n",
				&min_sz, &max_sz, &min_nr_a, &max_nr_a,
				&min_age, &max_age, &action, &quota.ms,
				&quota.sz, &quota.reset_interval, &parsed);
		if (ret != 10)
			break;
		if (!damos_action_valid(action)) {
			pr_err("wrong action %d\n", action);
			goto fail;
		}

		pos += parsed;
		scheme = damon_new_scheme(min_sz, max_sz, min_nr_a, max_nr_a,
				min_age, max_age, action, &quota);
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

static inline bool targetid_is_pid(const struct damon_ctx *ctx)
{
	return ctx->primitive.target_valid == damon_va_target_valid;
}

static ssize_t sprint_target_ids(struct damon_ctx *ctx, char *buf, ssize_t len)
{
	struct damon_target *t;
	unsigned long id;
	int written = 0;
	int rc;

	damon_for_each_target(t, ctx) {
		id = t->id;
		if (targetid_is_pid(ctx))
			/* Show pid numbers to debugfs users */
			id = (unsigned long)pid_vnr((struct pid *)id);

		rc = scnprintf(&buf[written], len - written, "%lu ", id);
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
 * Converts a string into an array of unsigned long integers
 *
 * Returns an array of unsigned long integers if the conversion success, or
 * NULL otherwise.
 */
static unsigned long *str_to_target_ids(const char *str, ssize_t len,
					ssize_t *nr_ids)
{
	unsigned long *ids;
	const int max_nr_ids = 32;
	unsigned long id;
	int pos = 0, parsed, ret;

	*nr_ids = 0;
	ids = kmalloc_array(max_nr_ids, sizeof(id), GFP_KERNEL);
	if (!ids)
		return NULL;
	while (*nr_ids < max_nr_ids && pos < len) {
		ret = sscanf(&str[pos], "%lu%n", &id, &parsed);
		pos += parsed;
		if (ret != 1)
			break;
		ids[*nr_ids] = id;
		*nr_ids += 1;
	}

	return ids;
}

static void dbgfs_put_pids(unsigned long *ids, int nr_ids)
{
	int i;

	for (i = 0; i < nr_ids; i++)
		put_pid((struct pid *)ids[i]);
}

static ssize_t dbgfs_target_ids_write(struct file *file,
		const char __user *buf, size_t count, loff_t *ppos)
{
	struct damon_ctx *ctx = file->private_data;
	bool id_is_pid = true;
	char *kbuf, *nrs;
	unsigned long *targets;
	ssize_t nr_targets;
	ssize_t ret;
	int i;

	kbuf = user_input_str(buf, count, ppos);
	if (IS_ERR(kbuf))
		return PTR_ERR(kbuf);

	nrs = kbuf;
	if (!strncmp(kbuf, "paddr\n", count)) {
		id_is_pid = false;
		/* target id is meaningless here, but we set it just for fun */
		scnprintf(kbuf, count, "42    ");
	}

	targets = str_to_target_ids(nrs, count, &nr_targets);
	if (!targets) {
		ret = -ENOMEM;
		goto out;
	}

	if (id_is_pid) {
		for (i = 0; i < nr_targets; i++) {
			targets[i] = (unsigned long)find_get_pid(
					(int)targets[i]);
			if (!targets[i]) {
				dbgfs_put_pids(targets, i);
				ret = -EINVAL;
				goto free_targets_out;
			}
		}
	}

	mutex_lock(&ctx->kdamond_lock);
	if (ctx->kdamond) {
		if (id_is_pid)
			dbgfs_put_pids(targets, nr_targets);
		ret = -EBUSY;
		goto unlock_out;
	}

	/* remove targets with previously-set primitive */
	damon_set_targets(ctx, NULL, 0);

	/* Configure the context for the address space type */
	if (id_is_pid)
		damon_va_set_primitives(ctx);
	else
		damon_pa_set_primitives(ctx);

	ret = damon_set_targets(ctx, targets, nr_targets);
	if (ret) {
		if (id_is_pid)
			dbgfs_put_pids(targets, nr_targets);
	} else {
		ret = count;
	}

unlock_out:
	mutex_unlock(&ctx->kdamond_lock);
free_targets_out:
	kfree(targets);
out:
	kfree(kbuf);
	return ret;
}

static ssize_t sprint_init_regions(struct damon_ctx *c, char *buf, ssize_t len)
{
	struct damon_target *t;
	struct damon_region *r;
	int written = 0;
	int rc;

	damon_for_each_target(t, c) {
		damon_for_each_region(r, t) {
			rc = scnprintf(&buf[written], len - written,
					"%lu %lu %lu\n",
					t->id, r->ar.start, r->ar.end);
			if (!rc)
				return -ENOMEM;
			written += rc;
		}
	}
	return written;
}

static ssize_t dbgfs_init_regions_read(struct file *file, char __user *buf,
		size_t count, loff_t *ppos)
{
	struct damon_ctx *ctx = file->private_data;
	char *kbuf;
	ssize_t len;

	kbuf = kmalloc(count, GFP_KERNEL);
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

static int add_init_region(struct damon_ctx *c,
			 unsigned long target_id, struct damon_addr_range *ar)
{
	struct damon_target *t;
	struct damon_region *r, *prev;
	unsigned long id;
	int rc = -EINVAL;

	if (ar->start >= ar->end)
		return -EINVAL;

	damon_for_each_target(t, c) {
		id = t->id;
		if (targetid_is_pid(c))
			id = (unsigned long)pid_vnr((struct pid *)id);
		if (id == target_id) {
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
	unsigned long target_id;
	struct damon_addr_range ar;
	int err;

	damon_for_each_target(t, c) {
		damon_for_each_region_safe(r, next, t)
			damon_destroy_region(r, t);
	}

	while (pos < len) {
		ret = sscanf(&str[pos], "%lu %lu %lu%n",
				&target_id, &ar.start, &ar.end, &parsed);
		if (ret != 3)
			break;
		err = add_init_region(c, target_id, &ar);
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

	kbuf = kmalloc(count, GFP_KERNEL);
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

static int dbgfs_before_terminate(struct damon_ctx *ctx)
{
	struct damon_target *t, *next;

	if (!targetid_is_pid(ctx))
		return 0;

	damon_for_each_target_safe(t, next, ctx) {
		put_pid((struct pid *)t->id);
		damon_destroy_target(t);
	}
	return 0;
}

static struct damon_ctx *dbgfs_new_ctx(void)
{
	struct damon_ctx *ctx;

	ctx = damon_new_ctx();
	if (!ctx)
		return NULL;

	damon_va_set_primitives(ctx);
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

	if (!strncmp(kbuf, "on", count))
		ret = damon_start(dbgfs_ctxs, dbgfs_nr_ctxs);
	else if (!strncmp(kbuf, "off", count))
		ret = damon_stop(dbgfs_ctxs, dbgfs_nr_ctxs);
	else
		ret = -EINVAL;

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
	int rc;

	dbgfs_ctxs = kmalloc(sizeof(*dbgfs_ctxs), GFP_KERNEL);
	if (!dbgfs_ctxs)
		return -ENOMEM;
	dbgfs_ctxs[0] = dbgfs_new_ctx();
	if (!dbgfs_ctxs[0]) {
		kfree(dbgfs_ctxs);
		return -ENOMEM;
	}
	dbgfs_nr_ctxs = 1;

	rc = __damon_dbgfs_init();
	if (rc) {
		kfree(dbgfs_ctxs[0]);
		kfree(dbgfs_ctxs);
		pr_err("%s: dbgfs init failed\n", __func__);
	}

	return rc;
}

module_init(damon_dbgfs_init);

#include "dbgfs-test.h"
