/*
 * device_cgroup.c - device cgroup subsystem
 *
 * Copyright 2007 IBM Corp
 */

#include <linux/device_cgroup.h>
#include <linux/cgroup.h>
#include <linux/ctype.h>
#include <linux/list.h>
#include <linux/uaccess.h>
#include <linux/seq_file.h>
#include <linux/slab.h>
#include <linux/rcupdate.h>
#include <linux/mutex.h>

#define ACC_MKNOD 1
#define ACC_READ  2
#define ACC_WRITE 4
#define ACC_MASK (ACC_MKNOD | ACC_READ | ACC_WRITE)

#define DEV_BLOCK 1
#define DEV_CHAR  2
#define DEV_ALL   4  /* this represents all devices */

static DEFINE_MUTEX(devcgroup_mutex);

/*
 * whitelist locking rules:
 * hold devcgroup_mutex for update/read.
 * hold rcu_read_lock() for read.
 */

struct dev_whitelist_item {
	u32 major, minor;
	short type;
	short access;
	struct list_head list;
	struct rcu_head rcu;
};

struct dev_cgroup {
	struct cgroup_subsys_state css;
	struct list_head whitelist;
};

static inline struct dev_cgroup *css_to_devcgroup(struct cgroup_subsys_state *s)
{
	return container_of(s, struct dev_cgroup, css);
}

static inline struct dev_cgroup *cgroup_to_devcgroup(struct cgroup *cgroup)
{
	return css_to_devcgroup(cgroup_subsys_state(cgroup, devices_subsys_id));
}

static inline struct dev_cgroup *task_devcgroup(struct task_struct *task)
{
	return css_to_devcgroup(task_subsys_state(task, devices_subsys_id));
}

struct cgroup_subsys devices_subsys;

static int devcgroup_can_attach(struct cgroup *new_cgrp,
				struct cgroup_taskset *set)
{
	struct task_struct *task = cgroup_taskset_first(set);

	if (current != task && !capable(CAP_SYS_ADMIN))
		return -EPERM;
	return 0;
}

/*
 * called under devcgroup_mutex
 */
static int dev_whitelist_copy(struct list_head *dest, struct list_head *orig)
{
	struct dev_whitelist_item *wh, *tmp, *new;

	list_for_each_entry(wh, orig, list) {
		new = kmemdup(wh, sizeof(*wh), GFP_KERNEL);
		if (!new)
			goto free_and_exit;
		list_add_tail(&new->list, dest);
	}

	return 0;

free_and_exit:
	list_for_each_entry_safe(wh, tmp, dest, list) {
		list_del(&wh->list);
		kfree(wh);
	}
	return -ENOMEM;
}

/* Stupid prototype - don't bother combining existing entries */
/*
 * called under devcgroup_mutex
 */
static int dev_whitelist_add(struct dev_cgroup *dev_cgroup,
			struct dev_whitelist_item *wh)
{
	struct dev_whitelist_item *whcopy, *walk;

	whcopy = kmemdup(wh, sizeof(*wh), GFP_KERNEL);
	if (!whcopy)
		return -ENOMEM;

	list_for_each_entry(walk, &dev_cgroup->whitelist, list) {
		if (walk->type != wh->type)
			continue;
		if (walk->major != wh->major)
			continue;
		if (walk->minor != wh->minor)
			continue;

		walk->access |= wh->access;
		kfree(whcopy);
		whcopy = NULL;
	}

	if (whcopy != NULL)
		list_add_tail_rcu(&whcopy->list, &dev_cgroup->whitelist);
	return 0;
}

/*
 * called under devcgroup_mutex
 */
static void dev_whitelist_rm(struct dev_cgroup *dev_cgroup,
			struct dev_whitelist_item *wh)
{
	struct dev_whitelist_item *walk, *tmp;

	list_for_each_entry_safe(walk, tmp, &dev_cgroup->whitelist, list) {
		if (walk->type == DEV_ALL)
			goto remove;
		if (walk->type != wh->type)
			continue;
		if (walk->major != ~0 && walk->major != wh->major)
			continue;
		if (walk->minor != ~0 && walk->minor != wh->minor)
			continue;

remove:
		walk->access &= ~wh->access;
		if (!walk->access) {
			list_del_rcu(&walk->list);
			kfree_rcu(walk, rcu);
		}
	}
}

/*
 * called from kernel/cgroup.c with cgroup_lock() held.
 */
static struct cgroup_subsys_state *devcgroup_create(struct cgroup *cgroup)
{
	struct dev_cgroup *dev_cgroup, *parent_dev_cgroup;
	struct cgroup *parent_cgroup;
	int ret;

	dev_cgroup = kzalloc(sizeof(*dev_cgroup), GFP_KERNEL);
	if (!dev_cgroup)
		return ERR_PTR(-ENOMEM);
	INIT_LIST_HEAD(&dev_cgroup->whitelist);
	parent_cgroup = cgroup->parent;

	if (parent_cgroup == NULL) {
		struct dev_whitelist_item *wh;
		wh = kmalloc(sizeof(*wh), GFP_KERNEL);
		if (!wh) {
			kfree(dev_cgroup);
			return ERR_PTR(-ENOMEM);
		}
		wh->minor = wh->major = ~0;
		wh->type = DEV_ALL;
		wh->access = ACC_MASK;
		list_add(&wh->list, &dev_cgroup->whitelist);
	} else {
		parent_dev_cgroup = cgroup_to_devcgroup(parent_cgroup);
		mutex_lock(&devcgroup_mutex);
		ret = dev_whitelist_copy(&dev_cgroup->whitelist,
				&parent_dev_cgroup->whitelist);
		mutex_unlock(&devcgroup_mutex);
		if (ret) {
			kfree(dev_cgroup);
			return ERR_PTR(ret);
		}
	}

	return &dev_cgroup->css;
}

static void devcgroup_destroy(struct cgroup *cgroup)
{
	struct dev_cgroup *dev_cgroup;
	struct dev_whitelist_item *wh, *tmp;

	dev_cgroup = cgroup_to_devcgroup(cgroup);
	list_for_each_entry_safe(wh, tmp, &dev_cgroup->whitelist, list) {
		list_del(&wh->list);
		kfree(wh);
	}
	kfree(dev_cgroup);
}

#define DEVCG_ALLOW 1
#define DEVCG_DENY 2
#define DEVCG_LIST 3

#define MAJMINLEN 13
#define ACCLEN 4

static void set_access(char *acc, short access)
{
	int idx = 0;
	memset(acc, 0, ACCLEN);
	if (access & ACC_READ)
		acc[idx++] = 'r';
	if (access & ACC_WRITE)
		acc[idx++] = 'w';
	if (access & ACC_MKNOD)
		acc[idx++] = 'm';
}

static char type_to_char(short type)
{
	if (type == DEV_ALL)
		return 'a';
	if (type == DEV_CHAR)
		return 'c';
	if (type == DEV_BLOCK)
		return 'b';
	return 'X';
}

static void set_majmin(char *str, unsigned m)
{
	if (m == ~0)
		strcpy(str, "*");
	else
		sprintf(str, "%u", m);
}

static int devcgroup_seq_read(struct cgroup *cgroup, struct cftype *cft,
				struct seq_file *m)
{
	struct dev_cgroup *devcgroup = cgroup_to_devcgroup(cgroup);
	struct dev_whitelist_item *wh;
	char maj[MAJMINLEN], min[MAJMINLEN], acc[ACCLEN];

	rcu_read_lock();
	list_for_each_entry_rcu(wh, &devcgroup->whitelist, list) {
		set_access(acc, wh->access);
		set_majmin(maj, wh->major);
		set_majmin(min, wh->minor);
		seq_printf(m, "%c %s:%s %s\n", type_to_char(wh->type),
			   maj, min, acc);
	}
	rcu_read_unlock();

	return 0;
}

/*
 * may_access_whitelist:
 * does the access granted to dev_cgroup c contain the access
 * requested in whitelist item refwh.
 * return 1 if yes, 0 if no.
 * call with devcgroup_mutex held
 */
static int may_access_whitelist(struct dev_cgroup *c,
				       struct dev_whitelist_item *refwh)
{
	struct dev_whitelist_item *whitem;

	list_for_each_entry(whitem, &c->whitelist, list) {
		if (whitem->type & DEV_ALL)
			return 1;
		if ((refwh->type & DEV_BLOCK) && !(whitem->type & DEV_BLOCK))
			continue;
		if ((refwh->type & DEV_CHAR) && !(whitem->type & DEV_CHAR))
			continue;
		if (whitem->major != ~0 && whitem->major != refwh->major)
			continue;
		if (whitem->minor != ~0 && whitem->minor != refwh->minor)
			continue;
		if (refwh->access & (~whitem->access))
			continue;
		return 1;
	}
	return 0;
}

/*
 * parent_has_perm:
 * when adding a new allow rule to a device whitelist, the rule
 * must be allowed in the parent device
 */
static int parent_has_perm(struct dev_cgroup *childcg,
				  struct dev_whitelist_item *wh)
{
	struct cgroup *pcg = childcg->css.cgroup->parent;
	struct dev_cgroup *parent;

	if (!pcg)
		return 1;
	parent = cgroup_to_devcgroup(pcg);
	return may_access_whitelist(parent, wh);
}

/*
 * Modify the whitelist using allow/deny rules.
 * CAP_SYS_ADMIN is needed for this.  It's at least separate from CAP_MKNOD
 * so we can give a container CAP_MKNOD to let it create devices but not
 * modify the whitelist.
 * It seems likely we'll want to add a CAP_CONTAINER capability to allow
 * us to also grant CAP_SYS_ADMIN to containers without giving away the
 * device whitelist controls, but for now we'll stick with CAP_SYS_ADMIN
 *
 * Taking rules away is always allowed (given CAP_SYS_ADMIN).  Granting
 * new access is only allowed if you're in the top-level cgroup, or your
 * parent cgroup has the access you're asking for.
 */
static int devcgroup_update_access(struct dev_cgroup *devcgroup,
				   int filetype, const char *buffer)
{
	const char *b;
	char *endp;
	int count;
	struct dev_whitelist_item wh;

	if (!capable(CAP_SYS_ADMIN))
		return -EPERM;

	memset(&wh, 0, sizeof(wh));
	b = buffer;

	switch (*b) {
	case 'a':
		wh.type = DEV_ALL;
		wh.access = ACC_MASK;
		wh.major = ~0;
		wh.minor = ~0;
		goto handle;
	case 'b':
		wh.type = DEV_BLOCK;
		break;
	case 'c':
		wh.type = DEV_CHAR;
		break;
	default:
		return -EINVAL;
	}
	b++;
	if (!isspace(*b))
		return -EINVAL;
	b++;
	if (*b == '*') {
		wh.major = ~0;
		b++;
	} else if (isdigit(*b)) {
		wh.major = simple_strtoul(b, &endp, 10);
		b = endp;
	} else {
		return -EINVAL;
	}
	if (*b != ':')
		return -EINVAL;
	b++;

	/* read minor */
	if (*b == '*') {
		wh.minor = ~0;
		b++;
	} else if (isdigit(*b)) {
		wh.minor = simple_strtoul(b, &endp, 10);
		b = endp;
	} else {
		return -EINVAL;
	}
	if (!isspace(*b))
		return -EINVAL;
	for (b++, count = 0; count < 3; count++, b++) {
		switch (*b) {
		case 'r':
			wh.access |= ACC_READ;
			break;
		case 'w':
			wh.access |= ACC_WRITE;
			break;
		case 'm':
			wh.access |= ACC_MKNOD;
			break;
		case '\n':
		case '\0':
			count = 3;
			break;
		default:
			return -EINVAL;
		}
	}

handle:
	switch (filetype) {
	case DEVCG_ALLOW:
		if (!parent_has_perm(devcgroup, &wh))
			return -EPERM;
		return dev_whitelist_add(devcgroup, &wh);
	case DEVCG_DENY:
		dev_whitelist_rm(devcgroup, &wh);
		break;
	default:
		return -EINVAL;
	}
	return 0;
}

static int devcgroup_access_write(struct cgroup *cgrp, struct cftype *cft,
				  const char *buffer)
{
	int retval;

	mutex_lock(&devcgroup_mutex);
	retval = devcgroup_update_access(cgroup_to_devcgroup(cgrp),
					 cft->private, buffer);
	mutex_unlock(&devcgroup_mutex);
	return retval;
}

static struct cftype dev_cgroup_files[] = {
	{
		.name = "allow",
		.write_string  = devcgroup_access_write,
		.private = DEVCG_ALLOW,
	},
	{
		.name = "deny",
		.write_string = devcgroup_access_write,
		.private = DEVCG_DENY,
	},
	{
		.name = "list",
		.read_seq_string = devcgroup_seq_read,
		.private = DEVCG_LIST,
	},
	{ }	/* terminate */
};

struct cgroup_subsys devices_subsys = {
	.name = "devices",
	.can_attach = devcgroup_can_attach,
	.create = devcgroup_create,
	.destroy = devcgroup_destroy,
	.subsys_id = devices_subsys_id,
	.base_cftypes = dev_cgroup_files,
};

int __devcgroup_inode_permission(struct inode *inode, int mask)
{
	struct dev_cgroup *dev_cgroup;
	struct dev_whitelist_item *wh;

	rcu_read_lock();

	dev_cgroup = task_devcgroup(current);

	list_for_each_entry_rcu(wh, &dev_cgroup->whitelist, list) {
		if (wh->type & DEV_ALL)
			goto found;
		if ((wh->type & DEV_BLOCK) && !S_ISBLK(inode->i_mode))
			continue;
		if ((wh->type & DEV_CHAR) && !S_ISCHR(inode->i_mode))
			continue;
		if (wh->major != ~0 && wh->major != imajor(inode))
			continue;
		if (wh->minor != ~0 && wh->minor != iminor(inode))
			continue;

		if ((mask & MAY_WRITE) && !(wh->access & ACC_WRITE))
			continue;
		if ((mask & MAY_READ) && !(wh->access & ACC_READ))
			continue;
found:
		rcu_read_unlock();
		return 0;
	}

	rcu_read_unlock();

	return -EPERM;
}

int devcgroup_inode_mknod(int mode, dev_t dev)
{
	struct dev_cgroup *dev_cgroup;
	struct dev_whitelist_item *wh;

	if (!S_ISBLK(mode) && !S_ISCHR(mode))
		return 0;

	rcu_read_lock();

	dev_cgroup = task_devcgroup(current);

	list_for_each_entry_rcu(wh, &dev_cgroup->whitelist, list) {
		if (wh->type & DEV_ALL)
			goto found;
		if ((wh->type & DEV_BLOCK) && !S_ISBLK(mode))
			continue;
		if ((wh->type & DEV_CHAR) && !S_ISCHR(mode))
			continue;
		if (wh->major != ~0 && wh->major != MAJOR(dev))
			continue;
		if (wh->minor != ~0 && wh->minor != MINOR(dev))
			continue;

		if (!(wh->access & ACC_MKNOD))
			continue;
found:
		rcu_read_unlock();
		return 0;
	}

	rcu_read_unlock();

	return -EPERM;
}
