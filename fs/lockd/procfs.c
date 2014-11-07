/*
 * Procfs support for lockd
 *
 * Copyright (c) 2014 Jeff Layton <jlayton@primarydata.com>
 */

#include <linux/fs.h>
#include <linux/proc_fs.h>
#include <linux/module.h>
#include <linux/nsproxy.h>
#include <net/net_namespace.h>

#include "netns.h"
#include "procfs.h"

/*
 * We only allow strings that start with 'Y', 'y', or '1'.
 */
static ssize_t
nlm_end_grace_write(struct file *file, const char __user *buf, size_t size,
		    loff_t *pos)
{
	char *data;
	struct lockd_net *ln = net_generic(current->nsproxy->net_ns,
					   lockd_net_id);

	if (size < 1)
		return -EINVAL;

	data = simple_transaction_get(file, buf, size);
	if (IS_ERR(data))
		return PTR_ERR(data);

	switch(data[0]) {
	case 'Y':
	case 'y':
	case '1':
		locks_end_grace(&ln->lockd_manager);
		break;
	default:
		return -EINVAL;
	}

	return size;
}

static ssize_t
nlm_end_grace_read(struct file *file, char __user *buf, size_t size,
		   loff_t *pos)
{
	struct lockd_net *ln = net_generic(current->nsproxy->net_ns,
					   lockd_net_id);
	char resp[3];

	resp[0] = list_empty(&ln->lockd_manager.list) ? 'Y' : 'N';
	resp[1] = '\n';
	resp[2] = '\0';

	return simple_read_from_buffer(buf, size, pos, resp, sizeof(resp));
}

static const struct file_operations lockd_end_grace_operations = {
	.write		= nlm_end_grace_write,
	.read		= nlm_end_grace_read,
	.llseek		= default_llseek,
	.release	= simple_transaction_release,
	.owner		= THIS_MODULE,
};

int __init
lockd_create_procfs(void)
{
	struct proc_dir_entry *entry;

	entry = proc_mkdir("fs/lockd", NULL);
	if (!entry)
		return -ENOMEM;
	entry = proc_create("nlm_end_grace", S_IRUGO|S_IWUSR, entry,
				 &lockd_end_grace_operations);
	if (!entry) {
		remove_proc_entry("fs/lockd", NULL);
		return -ENOMEM;
	}
	return 0;
}

void __exit
lockd_remove_procfs(void)
{
	remove_proc_entry("fs/lockd/nlm_end_grace", NULL);
	remove_proc_entry("fs/lockd", NULL);
}
