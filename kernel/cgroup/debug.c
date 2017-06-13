#include <linux/ctype.h>
#include <linux/mm.h>
#include <linux/slab.h>

#include "cgroup-internal.h"

static struct cgroup_subsys_state *
debug_css_alloc(struct cgroup_subsys_state *parent_css)
{
	struct cgroup_subsys_state *css = kzalloc(sizeof(*css), GFP_KERNEL);

	if (!css)
		return ERR_PTR(-ENOMEM);

	return css;
}

static void debug_css_free(struct cgroup_subsys_state *css)
{
	kfree(css);
}

/*
 * debug_taskcount_read - return the number of tasks in a cgroup.
 * @cgrp: the cgroup in question
 */
static u64 debug_taskcount_read(struct cgroup_subsys_state *css,
				struct cftype *cft)
{
	return cgroup_task_count(css->cgroup);
}

static u64 current_css_set_read(struct cgroup_subsys_state *css,
				struct cftype *cft)
{
	return (u64)(unsigned long)current->cgroups;
}

static u64 current_css_set_refcount_read(struct cgroup_subsys_state *css,
					 struct cftype *cft)
{
	u64 count;

	rcu_read_lock();
	count = refcount_read(&task_css_set(current)->refcount);
	rcu_read_unlock();
	return count;
}

static int current_css_set_cg_links_read(struct seq_file *seq, void *v)
{
	struct cgrp_cset_link *link;
	struct css_set *cset;
	char *name_buf;

	name_buf = kmalloc(NAME_MAX + 1, GFP_KERNEL);
	if (!name_buf)
		return -ENOMEM;

	spin_lock_irq(&css_set_lock);
	rcu_read_lock();
	cset = rcu_dereference(current->cgroups);
	list_for_each_entry(link, &cset->cgrp_links, cgrp_link) {
		struct cgroup *c = link->cgrp;

		cgroup_name(c, name_buf, NAME_MAX + 1);
		seq_printf(seq, "Root %d group %s\n",
			   c->root->hierarchy_id, name_buf);
	}
	rcu_read_unlock();
	spin_unlock_irq(&css_set_lock);
	kfree(name_buf);
	return 0;
}

#define MAX_TASKS_SHOWN_PER_CSS 25
static int cgroup_css_links_read(struct seq_file *seq, void *v)
{
	struct cgroup_subsys_state *css = seq_css(seq);
	struct cgrp_cset_link *link;

	spin_lock_irq(&css_set_lock);
	list_for_each_entry(link, &css->cgroup->cset_links, cset_link) {
		struct css_set *cset = link->cset;
		struct task_struct *task;
		int count = 0;

		seq_printf(seq, "css_set %pK\n", cset);

		list_for_each_entry(task, &cset->tasks, cg_list) {
			if (count++ > MAX_TASKS_SHOWN_PER_CSS)
				goto overflow;
			seq_printf(seq, "  task %d\n", task_pid_vnr(task));
		}

		list_for_each_entry(task, &cset->mg_tasks, cg_list) {
			if (count++ > MAX_TASKS_SHOWN_PER_CSS)
				goto overflow;
			seq_printf(seq, "  task %d\n", task_pid_vnr(task));
		}
		continue;
	overflow:
		seq_puts(seq, "  ...\n");
	}
	spin_unlock_irq(&css_set_lock);
	return 0;
}

static u64 releasable_read(struct cgroup_subsys_state *css, struct cftype *cft)
{
	return (!cgroup_is_populated(css->cgroup) &&
		!css_has_online_children(&css->cgroup->self));
}

static struct cftype debug_files[] =  {
	{
		.name = "taskcount",
		.read_u64 = debug_taskcount_read,
	},

	{
		.name = "current_css_set",
		.read_u64 = current_css_set_read,
	},

	{
		.name = "current_css_set_refcount",
		.read_u64 = current_css_set_refcount_read,
	},

	{
		.name = "current_css_set_cg_links",
		.seq_show = current_css_set_cg_links_read,
	},

	{
		.name = "cgroup_css_links",
		.seq_show = cgroup_css_links_read,
	},

	{
		.name = "releasable",
		.read_u64 = releasable_read,
	},

	{ }	/* terminate */
};

struct cgroup_subsys debug_cgrp_subsys = {
	.css_alloc = debug_css_alloc,
	.css_free = debug_css_free,
	.legacy_cftypes = debug_files,
};
