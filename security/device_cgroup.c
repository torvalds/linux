/*
 * dev_cgroup.c - device cgroup subsystem
 *
 * Copyright 2007 IBM Corp
 */

#include <linux/device_cgroup.h>
#include <linux/cgroup.h>
#include <linux/ctype.h>
#include <linux/list.h>
#include <linux/uaccess.h>
#include <linux/seq_file.h>

#define ACC_MKNOD 1
#define ACC_READ  2
#define ACC_WRITE 4
#define ACC_MASK (ACC_MKNOD | ACC_READ | ACC_WRITE)

#define DEV_BLOCK 1
#define DEV_CHAR  2
#define DEV_ALL   4  /* this represents all devices */

/*
 * whitelist locking rules:
 * cgroup_lock() cannot be taken under dev_cgroup->lock.
 * dev_cgroup->lock can be taken with or without cgroup_lock().
 *
 * modifications always require cgroup_lock
 * modifications to a list which is visible require the
 *   dev_cgroup->lock *and* cgroup_lock()
 * walking the list requires dev_cgroup->lock or cgroup_lock().
 *
 * reasoning: dev_whitelist_copy() needs to kmalloc, so needs
 *   a mutex, which the cgroup_lock() is.  Since modifying
 *   a visible list requires both locks, either lock can be
 *   taken for walking the list.
 */

struct dev_whitelist_item {
	u32 major, minor;
	short type;
	short access;
	struct list_head list;
};

struct dev_cgroup {
	struct cgroup_subsys_state css;
	struct list_head whitelist;
	spinlock_t lock;
};

static inline struct dev_cgroup *cgroup_to_devcgroup(struct cgroup *cgroup)
{
	return container_of(cgroup_subsys_state(cgroup, devices_subsys_id),
			    struct dev_cgroup, css);
}

struct cgroup_subsys devices_subsys;

static int devcgroup_can_attach(struct cgroup_subsys *ss,
		struct cgroup *new_cgroup, struct task_struct *task)
{
	if (current != task && !capable(CAP_SYS_ADMIN))
			return -EPERM;

	return 0;
}

/*
 * called under cgroup_lock()
 */
static int dev_whitelist_copy(struct list_head *dest, struct list_head *orig)
{
	struct dev_whitelist_item *wh, *tmp, *new;

	list_for_each_entry(wh, orig, list) {
		new = kmalloc(sizeof(*wh), GFP_KERNEL);
		if (!new)
			goto free_and_exit;
		new->major = wh->major;
		new->minor = wh->minor;
		new->type = wh->type;
		new->access = wh->access;
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
 * called under cgroup_lock()
 * since the list is visible to other tasks, we need the spinlock also
 */
static int dev_whitelist_add(struct dev_cgroup *dev_cgroup,
			struct dev_whitelist_item *wh)
{
	struct dev_whitelist_item *whcopy;

	whcopy = kmalloc(sizeof(*whcopy), GFP_KERNEL);
	if (!whcopy)
		return -ENOMEM;

	memcpy(whcopy, wh, sizeof(*whcopy));
	spin_lock(&dev_cgroup->lock);
	list_add_tail(&whcopy->list, &dev_cgroup->whitelist);
	spin_unlock(&dev_cgroup->lock);
	return 0;
}

/*
 * called under cgroup_lock()
 * since the list is visible to other tasks, we need the spinlock also
 */
static void dev_whitelist_rm(struct dev_cgroup *dev_cgroup,
			struct dev_whitelist_item *wh)
{
	struct dev_whitelist_item *walk, *tmp;

	spin_lock(&dev_cgroup->lock);
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
			list_del(&walk->list);
			kfree(walk);
		}
	}
	spin_unlock(&dev_cgroup->lock);
}

/*
 * called from kernel/cgroup.c with cgroup_lock() held.
 */
static struct cgroup_subsys_state *devcgroup_create(struct cgroup_subsys *ss,
						struct cgroup *cgroup)
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
		wh->access = ACC_MKNOD | ACC_READ | ACC_WRITE;
		list_add(&wh->list, &dev_cgroup->whitelist);
	} else {
		parent_dev_cgroup = cgroup_to_devcgroup(parent_cgroup);
		ret = dev_whitelist_copy(&dev_cgroup->whitelist,
				&parent_dev_cgroup->whitelist);
		if (ret) {
			kfree(dev_cgroup);
			return ERR_PTR(ret);
		}
	}

	spin_lock_init(&dev_cgroup->lock);
	return &dev_cgroup->css;
}

static void devcgroup_destroy(struct cgroup_subsys *ss,
			struct cgroup *cgroup)
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

#define MAJMINLEN 10
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
	memset(str, 0, MAJMINLEN);
	if (m == ~0)
		sprintf(str, "*");
	else
		snprintf(str, MAJMINLEN, "%d", m);
}

static int devcgroup_seq_read(struct cgroup *cgroup, struct cftype *cft,
				struct seq_file *m)
{
	struct dev_cgroup *devcgroup = cgroup_to_devcgroup(cgroup);
	struct dev_whitelist_item *wh;
	char maj[MAJMINLEN], min[MAJMINLEN], acc[ACCLEN];

	spin_lock(&devcgroup->lock);
	list_for_each_entry(wh, &devcgroup->whitelist, list) {
		set_access(acc, wh->access);
		set_majmin(maj, wh->major);
		set_majmin(min, wh->minor);
		seq_printf(m, "%c %s:%s %s\n", type_to_char(wh->type),
			   maj, min, acc);
	}
	spin_unlock(&devcgroup->lock);

	return 0;
}

/*
 * may_access_whitelist:
 * does the access granted to dev_cgroup c contain the access
 * requested in whitelist item refwh.
 * return 1 if yes, 0 if no.
 * call with c->lock held
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
		if (refwh->access & (~(whitem->access | ACC_MASK)))
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
static int parent_has_perm(struct cgroup *childcg,
				  struct dev_whitelist_item *wh)
{
	struct cgroup *pcg = childcg->parent;
	struct dev_cgroup *parent;
	int ret;

	if (!pcg)
		return 1;
	parent = cgroup_to_devcgroup(pcg);
	spin_lock(&parent->lock);
	ret = may_access_whitelist(parent, wh);
	spin_unlock(&parent->lock);
	return ret;
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
static ssize_t devcgroup_access_write(struct cgroup *cgroup, struct cftype *cft,
				struct file *file, const char __user *userbuf,
				size_t nbytes, loff_t *ppos)
{
	struct cgroup *cur_cgroup;
	struct dev_cgroup *devcgroup, *cur_devcgroup;
	int filetype = cft->private;
	char *buffer, *b;
	int retval = 0, count;
	struct dev_whitelist_item wh;

	if (!capable(CAP_SYS_ADMIN))
		return -EPERM;

	devcgroup = cgroup_to_devcgroup(cgroup);
	cur_cgroup = task_cgroup(current, devices_subsys.subsys_id);
	cur_devcgroup = cgroup_to_devcgroup(cur_cgroup);

	buffer = kmalloc(nbytes+1, GFP_KERNEL);
	if (!buffer)
		return -ENOMEM;

	if (copy_from_user(buffer, userbuf, nbytes)) {
		retval = -EFAULT;
		goto out1;
	}
	buffer[nbytes] = 0;	/* nul-terminate */

	cgroup_lock();
	if (cgroup_is_removed(cgroup)) {
		retval = -ENODEV;
		goto out2;
	}

	memset(&wh, 0, sizeof(wh));
	b = buffer;

	switch (*b) {
	case 'a':
		wh.type = DEV_ALL;
		wh.access = ACC_MASK;
		goto handle;
	case 'b':
		wh.type = DEV_BLOCK;
		break;
	case 'c':
		wh.type = DEV_CHAR;
		break;
	default:
		retval = -EINVAL;
		goto out2;
	}
	b++;
	if (!isspace(*b)) {
		retval = -EINVAL;
		goto out2;
	}
	b++;
	if (*b == '*') {
		wh.major = ~0;
		b++;
	} else if (isdigit(*b)) {
		wh.major = 0;
		while (isdigit(*b)) {
			wh.major = wh.major*10+(*b-'0');
			b++;
		}
	} else {
		retval = -EINVAL;
		goto out2;
	}
	if (*b != ':') {
		retval = -EINVAL;
		goto out2;
	}
	b++;

	/* read minor */
	if (*b == '*') {
		wh.minor = ~0;
		b++;
	} else if (isdigit(*b)) {
		wh.minor = 0;
		while (isdigit(*b)) {
			wh.minor = wh.minor*10+(*b-'0');
			b++;
		}
	} else {
		retval = -EINVAL;
		goto out2;
	}
	if (!isspace(*b)) {
		retval = -EINVAL;
		goto out2;
	}
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
			retval = -EINVAL;
			goto out2;
		}
	}

handle:
	retval = 0;
	switch (filetype) {
	case DEVCG_ALLOW:
		if (!parent_has_perm(cgroup, &wh))
			retval = -EPERM;
		else
			retval = dev_whitelist_add(devcgroup, &wh);
		break;
	case DEVCG_DENY:
		dev_whitelist_rm(devcgroup, &wh);
		break;
	default:
		retval = -EINVAL;
		goto out2;
	}

	if (retval == 0)
		retval = nbytes;

out2:
	cgroup_unlock();
out1:
	kfree(buffer);
	return retval;
}

static struct cftype dev_cgroup_files[] = {
	{
		.name = "allow",
		.write  = devcgroup_access_write,
		.private = DEVCG_ALLOW,
	},
	{
		.name = "deny",
		.write = devcgroup_access_write,
		.private = DEVCG_DENY,
	},
	{
		.name = "list",
		.read_seq_string = devcgroup_seq_read,
		.private = DEVCG_LIST,
	},
};

static int devcgroup_populate(struct cgroup_subsys *ss,
				struct cgroup *cgroup)
{
	return cgroup_add_files(cgroup, ss, dev_cgroup_files,
					ARRAY_SIZE(dev_cgroup_files));
}

struct cgroup_subsys devices_subsys = {
	.name = "devices",
	.can_attach = devcgroup_can_attach,
	.create = devcgroup_create,
	.destroy  = devcgroup_destroy,
	.populate = devcgroup_populate,
	.subsys_id = devices_subsys_id,
};

int devcgroup_inode_permission(struct inode *inode, int mask)
{
	struct cgroup *cgroup;
	struct dev_cgroup *dev_cgroup;
	struct dev_whitelist_item *wh;

	dev_t device = inode->i_rdev;
	if (!device)
		return 0;
	if (!S_ISBLK(inode->i_mode) && !S_ISCHR(inode->i_mode))
		return 0;
	cgroup = task_cgroup(current, devices_subsys.subsys_id);
	dev_cgroup = cgroup_to_devcgroup(cgroup);
	if (!dev_cgroup)
		return 0;

	spin_lock(&dev_cgroup->lock);
	list_for_each_entry(wh, &dev_cgroup->whitelist, list) {
		if (wh->type & DEV_ALL)
			goto acc_check;
		if ((wh->type & DEV_BLOCK) && !S_ISBLK(inode->i_mode))
			continue;
		if ((wh->type & DEV_CHAR) && !S_ISCHR(inode->i_mode))
			continue;
		if (wh->major != ~0 && wh->major != imajor(inode))
			continue;
		if (wh->minor != ~0 && wh->minor != iminor(inode))
			continue;
acc_check:
		if ((mask & MAY_WRITE) && !(wh->access & ACC_WRITE))
			continue;
		if ((mask & MAY_READ) && !(wh->access & ACC_READ))
			continue;
		spin_unlock(&dev_cgroup->lock);
		return 0;
	}
	spin_unlock(&dev_cgroup->lock);

	return -EPERM;
}

int devcgroup_inode_mknod(int mode, dev_t dev)
{
	struct cgroup *cgroup;
	struct dev_cgroup *dev_cgroup;
	struct dev_whitelist_item *wh;

	cgroup = task_cgroup(current, devices_subsys.subsys_id);
	dev_cgroup = cgroup_to_devcgroup(cgroup);
	if (!dev_cgroup)
		return 0;

	spin_lock(&dev_cgroup->lock);
	list_for_each_entry(wh, &dev_cgroup->whitelist, list) {
		if (wh->type & DEV_ALL)
			goto acc_check;
		if ((wh->type & DEV_BLOCK) && !S_ISBLK(mode))
			continue;
		if ((wh->type & DEV_CHAR) && !S_ISCHR(mode))
			continue;
		if (wh->major != ~0 && wh->major != MAJOR(dev))
			continue;
		if (wh->minor != ~0 && wh->minor != MINOR(dev))
			continue;
acc_check:
		if (!(wh->access & ACC_MKNOD))
			continue;
		spin_unlock(&dev_cgroup->lock);
		return 0;
	}
	spin_unlock(&dev_cgroup->lock);
	return -EPERM;
}
