// SPDX-License-Identifier: GPL-2.0-only
/*
 * Resctrl PMU support
 * - Enables perf event access to resctrl cache occupancy monitoring
 *
 * This provides a perf PMU interface to read cache occupancy from resctrl
 * monitoring groups using file descriptors for group identification.
 */

#define pr_fmt(fmt) "resctrl_pmu: " fmt

#include <linux/kernel.h>
#include <linux/perf_event.h>
#include <linux/errno.h>
#include <linux/file.h>
#include <linux/slab.h>
#include <linux/err.h>
#include <linux/seq_file.h>
#include "internal.h"

/*
 * PMU type will be dynamically assigned by perf_pmu_register
 */
static struct pmu resctrl_pmu;

/*
 * Event private data - stores information about the monitored resctrl group
 */
struct resctrl_pmu_event {
	struct rdtgroup *rdtgrp;	/* Reference to rdtgroup being monitored */
};


/*
 * Get rdtgroup from file descriptor with proper mutual exclusion
 * Takes an additional reference on the rdtgroup which must be released
 */
static struct rdtgroup *get_rdtgroup_from_fd(int fd)
{
	struct file *file;
	struct kernfs_open_file *of;
	struct rdtgroup *rdtgrp;
	struct rdtgroup *ret = ERR_PTR(-EBADF);
	
	file = fget(fd);
	if (!file)
		goto out;
	
	/* Basic validation that this is a kernfs file with seq_file */
	ret = ERR_PTR(-EINVAL);
	if (!file->f_op || !file->private_data)
		goto out_fput;
	
	/* For kernfs files, private_data points to seq_file, and seq_file->private is kernfs_open_file */
	of = ((struct seq_file *)file->private_data)->private;
	if (!of)
		goto out_fput;
	
	/* Validate that this is actually a resctrl monitoring file */
	if (!of->kn || of->kn->attr.ops != &kf_mondata_ops)
		goto out_fput;
	
	/* CRITICAL: Hold rdtgroup_mutex to prevent race with release callback */
	mutex_lock(&rdtgroup_mutex);
	
	/* Get rdtgroup from kernfs_open_file - similar to pseudo_lock pattern */
	ret = ERR_PTR(-ENOENT);
	rdtgrp = of->priv;
	if (!rdtgrp)
		/* File was drained - release callback already called */
		goto out_unlock;
	
	if (rdtgrp->flags & RDT_DELETED)
		/* rdtgroup marked for deletion */
		goto out_unlock;
	
	/* Take reference using the rdtgroup API */
	rdtgroup_get(rdtgrp);
	ret = rdtgrp;
	/* Fall through to cleanup */

out_unlock:
	mutex_unlock(&rdtgroup_mutex);
out_fput:
	fput(file);
out:
	return ret;
}

/*
 * Clean up event resources - called when event is destroyed
 */
static void resctrl_event_destroy(struct perf_event *event)
{
	struct resctrl_pmu_event *resctrl_event = event->pmu_private;

	if (resctrl_event) {
		struct rdtgroup *rdtgrp = resctrl_event->rdtgrp;
		
		if (rdtgrp) {
			/* Log rdtgroup state before cleanup */
			pr_info("PMU event cleanup\n");
			pr_info("  rdtgroup: closid=%u, rmid=%u, waitcount=%d\n",
				rdtgrp->closid, rdtgrp->mon.rmid, atomic_read(&rdtgrp->waitcount));
			pr_info("  type=%s, mode=%d, flags=0x%x\n",
				rdtgrp->type == RDTCTRL_GROUP ? "CTRL" : "MON",
				rdtgrp->mode, rdtgrp->flags);
			pr_info("  cpu_mask=%*pbl\n", cpumask_pr_args(&rdtgrp->cpu_mask));
			
			/* Release the reference we took during init */
			rdtgroup_put(rdtgrp);
		}
		
		kfree(resctrl_event);
		event->pmu_private = NULL;
	}
}

/*
 * Initialize a new resctrl perf event
 * The config field contains the file descriptor of the monitoring file
 */
static int resctrl_event_init(struct perf_event *event)
{
	struct resctrl_pmu_event *resctrl_event;
	struct rdtgroup *rdtgrp;
	int fd;

	/* Only accept events for this PMU */
	if (event->attr.type != event->pmu->type)
		return -ENOENT;

	/* No sampling support */
	if (is_sampling_event(event))
		return -EINVAL;

	/* No filtering support */
	if (event->attr.exclude_user || event->attr.exclude_kernel ||
	    event->attr.exclude_hv || event->attr.exclude_idle)
		return -EINVAL;

	/* Extract file descriptor from config */
	fd = (int)event->attr.config;
	if (fd < 0)
		return -EINVAL;

	/* Get rdtgroup with proper protection and reference counting */
	rdtgrp = get_rdtgroup_from_fd(fd);
	if (IS_ERR(rdtgrp))
		return PTR_ERR(rdtgrp);

	/* Allocate our private event data */
	resctrl_event = kzalloc(sizeof(*resctrl_event), GFP_KERNEL);
	if (!resctrl_event) {
		rdtgroup_put(rdtgrp);
		return -ENOMEM;
	}

	resctrl_event->rdtgrp = rdtgrp;
	event->pmu_private = resctrl_event;

	/* Set destroy callback for proper cleanup */
	event->destroy = resctrl_event_destroy;

	/* Log comprehensive rdtgroup information */
	pr_info("PMU event initialized: fd=%d\n", fd);
	pr_info("  rdtgroup: closid=%u, rmid=%u, waitcount=%d\n",
		rdtgrp->closid, rdtgrp->mon.rmid, atomic_read(&rdtgrp->waitcount));
	pr_info("  type=%s, mode=%d, flags=0x%x\n",
		rdtgrp->type == RDTCTRL_GROUP ? "CTRL" : "MON",
		rdtgrp->mode, rdtgrp->flags);
	pr_info("  cpu_mask=%*pbl\n", cpumask_pr_args(&rdtgrp->cpu_mask));

	return 0;
}


/*
 * Add event to PMU (enable monitoring)
 */
static int resctrl_event_add(struct perf_event *event, int flags)
{
	/* Currently just a stub - would setup actual monitoring here */
	return 0;
}

/*
 * Remove event from PMU (disable monitoring)
 */
static void resctrl_event_del(struct perf_event *event, int flags)
{
	/* Currently just a stub - would disable monitoring here */
}

/*
 * Start event counting
 */
static void resctrl_event_start(struct perf_event *event, int flags)
{
	/* Currently just a stub - would start monitoring here */
}

/*
 * Stop event counting
 */
static void resctrl_event_stop(struct perf_event *event, int flags)
{
	/* Currently just a stub - would stop monitoring here */
}

/*
 * Read current counter value
 */
static void resctrl_event_update(struct perf_event *event)
{
	/* Currently just a stub - would read actual cache occupancy here */
	local64_set(&event->hw.prev_count, 0);
}

/*
 * Main PMU structure
 */
static struct pmu resctrl_pmu = {
	.task_ctx_nr	= perf_invalid_context,  /* System-wide only */
	.event_init	= resctrl_event_init,
	.add		= resctrl_event_add,
	.del		= resctrl_event_del,
	.start		= resctrl_event_start,
	.stop		= resctrl_event_stop,
	.read		= resctrl_event_update,
	.capabilities	= PERF_PMU_CAP_NO_INTERRUPT | PERF_PMU_CAP_NO_EXCLUDE,
};

/*
 * Initialize and register the resctrl PMU
 */
int resctrl_pmu_init(void)
{
	int ret;

	/* Register the PMU with perf subsystem */
	ret = perf_pmu_register(&resctrl_pmu, "resctrl", -1);
	if (ret) {
		pr_err("Failed to register resctrl PMU: %d\n", ret);
		return ret;
	}

	pr_info("Registered resctrl PMU with type %d\n", resctrl_pmu.type);
	return 0;
}

/*
 * Cleanup the resctrl PMU
 */
void resctrl_pmu_exit(void)
{
	perf_pmu_unregister(&resctrl_pmu);
	pr_info("Unregistered resctrl PMU\n");
}
