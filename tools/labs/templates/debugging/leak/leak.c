#include <linux/module.h>
#include <linux/init.h>
#include <linux/sched.h>
#include <linux/string.h>
#include <linux/list.h>
#include <linux/slab.h>

struct task_info {
	char *name;
	pid_t pid;
	unsigned long timestamp;
	struct list_head list;
};

static struct list_head head;

static struct task_info *task_info_alloc(pid_t pid, char *name)
{
	struct task_info *ti;

	ti = kmalloc(sizeof(*ti), GFP_KERNEL);
	if (!ti)
		return NULL;
	ti->name = kstrdup(name, GFP_KERNEL);
	ti->pid = pid;
	ti->timestamp = jiffies;

	return ti;
}

static void task_info_add_to_list(pid_t pid, char *name)
{
	struct task_info *ti;

	ti = task_info_alloc(pid, name);
	list_add(&ti->list, &head);
}

static void task_info_purge_list(void)
{
	struct list_head *p, *q;
	struct task_info *ti;

	list_for_each_safe(p, q, &head) {
		ti = list_entry(p, struct task_info, list);
		list_del(p);
		kfree(ti);
	}
}

static int so2_leak_init(void)
{
	pr_info("leak_init\n");

	INIT_LIST_HEAD(&head);

	task_info_add_to_list(current->pid, current->comm);

	return 0;
}

static void so2_leak_exit(void)
{
	pr_info("leak exit\n");
	task_info_purge_list();
}

MODULE_LICENSE("GPL v2");
module_init(so2_leak_init);
module_exit(so2_leak_exit);
