#include <linux/stat.h>
#include <linux/sysctl.h>
#include "../fs/xfs/linux-2.6/xfs_sysctl.h"
#include <linux/sunrpc/debug.h>
#include <linux/string.h>
#include <net/ip_vs.h>


static int sysctl_depth(struct ctl_table *table)
{
	struct ctl_table *tmp;
	int depth;

	depth = 0;
	for (tmp = table; tmp->parent; tmp = tmp->parent)
		depth++;

	return depth;
}

static struct ctl_table *sysctl_parent(struct ctl_table *table, int n)
{
	int i;

	for (i = 0; table && i < n; i++)
		table = table->parent;

	return table;
}


static void sysctl_print_path(struct ctl_table *table)
{
	struct ctl_table *tmp;
	int depth, i;
	depth = sysctl_depth(table);
	if (table->procname) {
		for (i = depth; i >= 0; i--) {
			tmp = sysctl_parent(table, i);
			printk("/%s", tmp->procname?tmp->procname:"");
		}
	}
	printk(" ");
}

static struct ctl_table *sysctl_check_lookup(struct nsproxy *namespaces,
						struct ctl_table *table)
{
	struct ctl_table_header *head;
	struct ctl_table *ref, *test;
	int depth, cur_depth;

	depth = sysctl_depth(table);

	for (head = __sysctl_head_next(namespaces, NULL); head;
	     head = __sysctl_head_next(namespaces, head)) {
		cur_depth = depth;
		ref = head->ctl_table;
repeat:
		test = sysctl_parent(table, cur_depth);
		for (; ref->procname; ref++) {
			int match = 0;
			if (cur_depth && !ref->child)
				continue;

			if (test->procname && ref->procname &&
			    (strcmp(test->procname, ref->procname) == 0))
					match++;

			if (match) {
				if (cur_depth != 0) {
					cur_depth--;
					ref = ref->child;
					goto repeat;
				}
				goto out;
			}
		}
	}
	ref = NULL;
out:
	sysctl_head_finish(head);
	return ref;
}

static void set_fail(const char **fail, struct ctl_table *table, const char *str)
{
	if (*fail) {
		printk(KERN_ERR "sysctl table check failed: ");
		sysctl_print_path(table);
		printk(" %s\n", *fail);
		dump_stack();
	}
	*fail = str;
}

static void sysctl_check_leaf(struct nsproxy *namespaces,
				struct ctl_table *table, const char **fail)
{
	struct ctl_table *ref;

	ref = sysctl_check_lookup(namespaces, table);
	if (ref && (ref != table))
		set_fail(fail, table, "Sysctl already exists");
}

int sysctl_check_table(struct nsproxy *namespaces, struct ctl_table *table)
{
	int error = 0;
	for (; table->procname; table++) {
		const char *fail = NULL;

		if (table->parent) {
			if (table->procname && !table->parent->procname)
				set_fail(&fail, table, "Parent without procname");
		}
		if (!table->procname)
			set_fail(&fail, table, "No procname");
		if (table->child) {
			if (table->data)
				set_fail(&fail, table, "Directory with data?");
			if (table->maxlen)
				set_fail(&fail, table, "Directory with maxlen?");
			if ((table->mode & (S_IRUGO|S_IXUGO)) != table->mode)
				set_fail(&fail, table, "Writable sysctl directory");
			if (table->proc_handler)
				set_fail(&fail, table, "Directory with proc_handler");
			if (table->extra1)
				set_fail(&fail, table, "Directory with extra1");
			if (table->extra2)
				set_fail(&fail, table, "Directory with extra2");
		} else {
			if ((table->proc_handler == proc_dostring) ||
			    (table->proc_handler == proc_dointvec) ||
			    (table->proc_handler == proc_dointvec_minmax) ||
			    (table->proc_handler == proc_dointvec_jiffies) ||
			    (table->proc_handler == proc_dointvec_userhz_jiffies) ||
			    (table->proc_handler == proc_dointvec_ms_jiffies) ||
			    (table->proc_handler == proc_doulongvec_minmax) ||
			    (table->proc_handler == proc_doulongvec_ms_jiffies_minmax)) {
				if (!table->data)
					set_fail(&fail, table, "No data");
				if (!table->maxlen)
					set_fail(&fail, table, "No maxlen");
			}
			if ((table->proc_handler == proc_doulongvec_minmax) ||
			    (table->proc_handler == proc_doulongvec_ms_jiffies_minmax)) {
				if (table->maxlen > sizeof (unsigned long)) {
					if (!table->extra1)
						set_fail(&fail, table, "No min");
					if (!table->extra2)
						set_fail(&fail, table, "No max");
				}
			}
#ifdef CONFIG_PROC_SYSCTL
			if (table->procname && !table->proc_handler)
				set_fail(&fail, table, "No proc_handler");
#endif
#if 0
			if (!table->procname && table->proc_handler)
				set_fail(&fail, table, "proc_handler without procname");
#endif
			sysctl_check_leaf(namespaces, table, &fail);
		}
		if (table->mode > 0777)
			set_fail(&fail, table, "bogus .mode");
		if (fail) {
			set_fail(&fail, table, NULL);
			error = -EINVAL;
		}
		if (table->child)
			error |= sysctl_check_table(namespaces, table->child);
	}
	return error;
}
