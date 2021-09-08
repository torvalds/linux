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
	ssize_t ret = count;
	int err;

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

	err = damon_set_attrs(ctx, s, a, r, minr, maxr);
	if (err)
		ret = err;
unlock_out:
	mutex_unlock(&ctx->kdamond_lock);
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
	char *kbuf, *nrs;
	unsigned long *targets;
	ssize_t nr_targets;
	ssize_t ret = count;
	int i;
	int err;

	kbuf = user_input_str(buf, count, ppos);
	if (IS_ERR(kbuf))
		return PTR_ERR(kbuf);

	nrs = kbuf;

	targets = str_to_target_ids(nrs, ret, &nr_targets);
	if (!targets) {
		ret = -ENOMEM;
		goto out;
	}

	if (targetid_is_pid(ctx)) {
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
		if (targetid_is_pid(ctx))
			dbgfs_put_pids(targets, nr_targets);
		ret = -EBUSY;
		goto unlock_out;
	}

	err = damon_set_targets(ctx, targets, nr_targets);
	if (err) {
		if (targetid_is_pid(ctx))
			dbgfs_put_pids(targets, nr_targets);
		ret = err;
	}

unlock_out:
	mutex_unlock(&ctx->kdamond_lock);
free_targets_out:
	kfree(targets);
out:
	kfree(kbuf);
	return ret;
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

static const struct file_operations target_ids_fops = {
	.open = damon_dbgfs_open,
	.read = dbgfs_target_ids_read,
	.write = dbgfs_target_ids_write,
};

static void dbgfs_fill_ctx_dir(struct dentry *dir, struct damon_ctx *ctx)
{
	const char * const file_names[] = {"attrs", "target_ids"};
	const struct file_operations *fops[] = {&attrs_fops, &target_ids_fops};
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
	ssize_t ret = count;
	char *kbuf;
	int err;

	kbuf = user_input_str(buf, count, ppos);
	if (IS_ERR(kbuf))
		return PTR_ERR(kbuf);

	/* Remove white space */
	if (sscanf(kbuf, "%s", kbuf) != 1) {
		kfree(kbuf);
		return -EINVAL;
	}

	if (!strncmp(kbuf, "on", count))
		err = damon_start(dbgfs_ctxs, dbgfs_nr_ctxs);
	else if (!strncmp(kbuf, "off", count))
		err = damon_stop(dbgfs_ctxs, dbgfs_nr_ctxs);
	else
		err = -EINVAL;

	if (err)
		ret = err;
	kfree(kbuf);
	return ret;
}

static const struct file_operations monitor_on_fops = {
	.read = dbgfs_monitor_on_read,
	.write = dbgfs_monitor_on_write,
};

static int __init __damon_dbgfs_init(void)
{
	struct dentry *dbgfs_root;
	const char * const file_names[] = {"monitor_on"};
	const struct file_operations *fops[] = {&monitor_on_fops};
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
