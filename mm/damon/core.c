// SPDX-License-Identifier: GPL-2.0
/*
 * Data Access Monitor
 *
 * Author: SeongJae Park <sjpark@amazon.de>
 */

#define pr_fmt(fmt) "damon: " fmt

#include <linux/damon.h>
#include <linux/delay.h>
#include <linux/kthread.h>
#include <linux/slab.h>

static DEFINE_MUTEX(damon_lock);
static int nr_running_ctxs;

struct damon_ctx *damon_new_ctx(void)
{
	struct damon_ctx *ctx;

	ctx = kzalloc(sizeof(*ctx), GFP_KERNEL);
	if (!ctx)
		return NULL;

	ctx->sample_interval = 5 * 1000;
	ctx->aggr_interval = 100 * 1000;
	ctx->primitive_update_interval = 60 * 1000 * 1000;

	ktime_get_coarse_ts64(&ctx->last_aggregation);
	ctx->last_primitive_update = ctx->last_aggregation;

	mutex_init(&ctx->kdamond_lock);

	ctx->target = NULL;

	return ctx;
}

void damon_destroy_ctx(struct damon_ctx *ctx)
{
	if (ctx->primitive.cleanup)
		ctx->primitive.cleanup(ctx);
	kfree(ctx);
}

/**
 * damon_set_attrs() - Set attributes for the monitoring.
 * @ctx:		monitoring context
 * @sample_int:		time interval between samplings
 * @aggr_int:		time interval between aggregations
 * @primitive_upd_int:	time interval between monitoring primitive updates
 *
 * This function should not be called while the kdamond is running.
 * Every time interval is in micro-seconds.
 *
 * Return: 0 on success, negative error code otherwise.
 */
int damon_set_attrs(struct damon_ctx *ctx, unsigned long sample_int,
		    unsigned long aggr_int, unsigned long primitive_upd_int)
{
	ctx->sample_interval = sample_int;
	ctx->aggr_interval = aggr_int;
	ctx->primitive_update_interval = primitive_upd_int;

	return 0;
}

static bool damon_kdamond_running(struct damon_ctx *ctx)
{
	bool running;

	mutex_lock(&ctx->kdamond_lock);
	running = ctx->kdamond != NULL;
	mutex_unlock(&ctx->kdamond_lock);

	return running;
}

static int kdamond_fn(void *data);

/*
 * __damon_start() - Starts monitoring with given context.
 * @ctx:	monitoring context
 *
 * This function should be called while damon_lock is hold.
 *
 * Return: 0 on success, negative error code otherwise.
 */
static int __damon_start(struct damon_ctx *ctx)
{
	int err = -EBUSY;

	mutex_lock(&ctx->kdamond_lock);
	if (!ctx->kdamond) {
		err = 0;
		ctx->kdamond_stop = false;
		ctx->kdamond = kthread_run(kdamond_fn, ctx, "kdamond.%d",
				nr_running_ctxs);
		if (IS_ERR(ctx->kdamond)) {
			err = PTR_ERR(ctx->kdamond);
			ctx->kdamond = 0;
		}
	}
	mutex_unlock(&ctx->kdamond_lock);

	return err;
}

/**
 * damon_start() - Starts the monitorings for a given group of contexts.
 * @ctxs:	an array of the pointers for contexts to start monitoring
 * @nr_ctxs:	size of @ctxs
 *
 * This function starts a group of monitoring threads for a group of monitoring
 * contexts.  One thread per each context is created and run in parallel.  The
 * caller should handle synchronization between the threads by itself.  If a
 * group of threads that created by other 'damon_start()' call is currently
 * running, this function does nothing but returns -EBUSY.
 *
 * Return: 0 on success, negative error code otherwise.
 */
int damon_start(struct damon_ctx **ctxs, int nr_ctxs)
{
	int i;
	int err = 0;

	mutex_lock(&damon_lock);
	if (nr_running_ctxs) {
		mutex_unlock(&damon_lock);
		return -EBUSY;
	}

	for (i = 0; i < nr_ctxs; i++) {
		err = __damon_start(ctxs[i]);
		if (err)
			break;
		nr_running_ctxs++;
	}
	mutex_unlock(&damon_lock);

	return err;
}

/*
 * __damon_stop() - Stops monitoring of given context.
 * @ctx:	monitoring context
 *
 * Return: 0 on success, negative error code otherwise.
 */
static int __damon_stop(struct damon_ctx *ctx)
{
	mutex_lock(&ctx->kdamond_lock);
	if (ctx->kdamond) {
		ctx->kdamond_stop = true;
		mutex_unlock(&ctx->kdamond_lock);
		while (damon_kdamond_running(ctx))
			usleep_range(ctx->sample_interval,
					ctx->sample_interval * 2);
		return 0;
	}
	mutex_unlock(&ctx->kdamond_lock);

	return -EPERM;
}

/**
 * damon_stop() - Stops the monitorings for a given group of contexts.
 * @ctxs:	an array of the pointers for contexts to stop monitoring
 * @nr_ctxs:	size of @ctxs
 *
 * Return: 0 on success, negative error code otherwise.
 */
int damon_stop(struct damon_ctx **ctxs, int nr_ctxs)
{
	int i, err = 0;

	for (i = 0; i < nr_ctxs; i++) {
		/* nr_running_ctxs is decremented in kdamond_fn */
		err = __damon_stop(ctxs[i]);
		if (err)
			return err;
	}

	return err;
}

/*
 * damon_check_reset_time_interval() - Check if a time interval is elapsed.
 * @baseline:	the time to check whether the interval has elapsed since
 * @interval:	the time interval (microseconds)
 *
 * See whether the given time interval has passed since the given baseline
 * time.  If so, it also updates the baseline to current time for next check.
 *
 * Return:	true if the time interval has passed, or false otherwise.
 */
static bool damon_check_reset_time_interval(struct timespec64 *baseline,
		unsigned long interval)
{
	struct timespec64 now;

	ktime_get_coarse_ts64(&now);
	if ((timespec64_to_ns(&now) - timespec64_to_ns(baseline)) <
			interval * 1000)
		return false;
	*baseline = now;
	return true;
}

/*
 * Check whether it is time to flush the aggregated information
 */
static bool kdamond_aggregate_interval_passed(struct damon_ctx *ctx)
{
	return damon_check_reset_time_interval(&ctx->last_aggregation,
			ctx->aggr_interval);
}

/*
 * Check whether it is time to check and apply the target monitoring regions
 *
 * Returns true if it is.
 */
static bool kdamond_need_update_primitive(struct damon_ctx *ctx)
{
	return damon_check_reset_time_interval(&ctx->last_primitive_update,
			ctx->primitive_update_interval);
}

/*
 * Check whether current monitoring should be stopped
 *
 * The monitoring is stopped when either the user requested to stop, or all
 * monitoring targets are invalid.
 *
 * Returns true if need to stop current monitoring.
 */
static bool kdamond_need_stop(struct damon_ctx *ctx)
{
	bool stop;

	mutex_lock(&ctx->kdamond_lock);
	stop = ctx->kdamond_stop;
	mutex_unlock(&ctx->kdamond_lock);
	if (stop)
		return true;

	if (!ctx->primitive.target_valid)
		return false;

	return !ctx->primitive.target_valid(ctx->target);
}

static void set_kdamond_stop(struct damon_ctx *ctx)
{
	mutex_lock(&ctx->kdamond_lock);
	ctx->kdamond_stop = true;
	mutex_unlock(&ctx->kdamond_lock);
}

/*
 * The monitoring daemon that runs as a kernel thread
 */
static int kdamond_fn(void *data)
{
	struct damon_ctx *ctx = (struct damon_ctx *)data;

	mutex_lock(&ctx->kdamond_lock);
	pr_info("kdamond (%d) starts\n", ctx->kdamond->pid);
	mutex_unlock(&ctx->kdamond_lock);

	if (ctx->primitive.init)
		ctx->primitive.init(ctx);
	if (ctx->callback.before_start && ctx->callback.before_start(ctx))
		set_kdamond_stop(ctx);

	while (!kdamond_need_stop(ctx)) {
		if (ctx->primitive.prepare_access_checks)
			ctx->primitive.prepare_access_checks(ctx);
		if (ctx->callback.after_sampling &&
				ctx->callback.after_sampling(ctx))
			set_kdamond_stop(ctx);

		usleep_range(ctx->sample_interval, ctx->sample_interval + 1);

		if (ctx->primitive.check_accesses)
			ctx->primitive.check_accesses(ctx);

		if (kdamond_aggregate_interval_passed(ctx)) {
			if (ctx->callback.after_aggregation &&
					ctx->callback.after_aggregation(ctx))
				set_kdamond_stop(ctx);
			if (ctx->primitive.reset_aggregated)
				ctx->primitive.reset_aggregated(ctx);
		}

		if (kdamond_need_update_primitive(ctx)) {
			if (ctx->primitive.update)
				ctx->primitive.update(ctx);
		}
	}

	if (ctx->callback.before_terminate &&
			ctx->callback.before_terminate(ctx))
		set_kdamond_stop(ctx);
	if (ctx->primitive.cleanup)
		ctx->primitive.cleanup(ctx);

	pr_debug("kdamond (%d) finishes\n", ctx->kdamond->pid);
	mutex_lock(&ctx->kdamond_lock);
	ctx->kdamond = NULL;
	mutex_unlock(&ctx->kdamond_lock);

	mutex_lock(&damon_lock);
	nr_running_ctxs--;
	mutex_unlock(&damon_lock);

	do_exit(0);
}
