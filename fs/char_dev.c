/*
 *  linux/fs/char_dev.c
 *
 *  Copyright (C) 1991, 1992  Linus Torvalds
 */

#include <linux/config.h>
#include <linux/init.h>
#include <linux/fs.h>
#include <linux/slab.h>
#include <linux/string.h>

#include <linux/major.h>
#include <linux/errno.h>
#include <linux/module.h>
#include <linux/smp_lock.h>
#include <linux/devfs_fs_kernel.h>

#include <linux/kobject.h>
#include <linux/kobj_map.h>
#include <linux/cdev.h>
#include <linux/mutex.h>

#ifdef CONFIG_KMOD
#include <linux/kmod.h>
#endif

static struct kobj_map *cdev_map;

#define MAX_PROBE_HASH 255	/* random */

static DEFINE_MUTEX(chrdevs_lock);

static struct char_device_struct {
	struct char_device_struct *next;
	unsigned int major;
	unsigned int baseminor;
	int minorct;
	char name[64];
	struct file_operations *fops;
	struct cdev *cdev;		/* will die */
} *chrdevs[MAX_PROBE_HASH];

/* index in the above */
static inline int major_to_index(int major)
{
	return major % MAX_PROBE_HASH;
}

struct chrdev_info {
	int index;
	struct char_device_struct *cd;
};

void *get_next_chrdev(void *dev)
{
	struct chrdev_info *info;

	if (dev == NULL) {
		info = kmalloc(sizeof(*info), GFP_KERNEL);
		if (!info)
			goto out;
		info->index=0;
		info->cd = chrdevs[info->index];
		if (info->cd)
			goto out;
	} else {
		info = dev;
	}

	while (info->index < ARRAY_SIZE(chrdevs)) {
		if (info->cd)
			info->cd = info->cd->next;
		if (info->cd)
			goto out;
		/*
		 * No devices on this chain, move to the next
		 */
		info->index++;
		info->cd = (info->index < ARRAY_SIZE(chrdevs)) ?
			chrdevs[info->index] : NULL;
		if (info->cd)
			goto out;
	}

out:
	return info;
}

void *acquire_chrdev_list(void)
{
	mutex_lock(&chrdevs_lock);
	return get_next_chrdev(NULL);
}

void release_chrdev_list(void *dev)
{
	mutex_unlock(&chrdevs_lock);
	kfree(dev);
}


int count_chrdev_list(void)
{
	struct char_device_struct *cd;
	int i, count;

	count = 0;

	for (i = 0; i < ARRAY_SIZE(chrdevs) ; i++) {
		for (cd = chrdevs[i]; cd; cd = cd->next)
			count++;
	}

	return count;
}

int get_chrdev_info(void *dev, int *major, char **name)
{
	struct chrdev_info *info = dev;

	if (info->cd == NULL)
		return 1;

	*major = info->cd->major;
	*name = info->cd->name;
	return 0;
}

/*
 * Register a single major with a specified minor range.
 *
 * If major == 0 this functions will dynamically allocate a major and return
 * its number.
 *
 * If major > 0 this function will attempt to reserve the passed range of
 * minors and will return zero on success.
 *
 * Returns a -ve errno on failure.
 */
static struct char_device_struct *
__register_chrdev_region(unsigned int major, unsigned int baseminor,
			   int minorct, const char *name)
{
	struct char_device_struct *cd, **cp;
	int ret = 0;
	int i;

	cd = kzalloc(sizeof(struct char_device_struct), GFP_KERNEL);
	if (cd == NULL)
		return ERR_PTR(-ENOMEM);

	mutex_lock(&chrdevs_lock);

	/* temporary */
	if (major == 0) {
		for (i = ARRAY_SIZE(chrdevs)-1; i > 0; i--) {
			if (chrdevs[i] == NULL)
				break;
		}

		if (i == 0) {
			ret = -EBUSY;
			goto out;
		}
		major = i;
		ret = major;
	}

	cd->major = major;
	cd->baseminor = baseminor;
	cd->minorct = minorct;
	strncpy(cd->name,name, 64);

	i = major_to_index(major);

	for (cp = &chrdevs[i]; *cp; cp = &(*cp)->next)
		if ((*cp)->major > major ||
		    ((*cp)->major == major && (*cp)->baseminor >= baseminor))
			break;
	if (*cp && (*cp)->major == major &&
	    (*cp)->baseminor < baseminor + minorct) {
		ret = -EBUSY;
		goto out;
	}
	cd->next = *cp;
	*cp = cd;
	mutex_unlock(&chrdevs_lock);
	return cd;
out:
	mutex_unlock(&chrdevs_lock);
	kfree(cd);
	return ERR_PTR(ret);
}

static struct char_device_struct *
__unregister_chrdev_region(unsigned major, unsigned baseminor, int minorct)
{
	struct char_device_struct *cd = NULL, **cp;
	int i = major_to_index(major);

	mutex_lock(&chrdevs_lock);
	for (cp = &chrdevs[i]; *cp; cp = &(*cp)->next)
		if ((*cp)->major == major &&
		    (*cp)->baseminor == baseminor &&
		    (*cp)->minorct == minorct)
			break;
	if (*cp) {
		cd = *cp;
		*cp = cd->next;
	}
	mutex_unlock(&chrdevs_lock);
	return cd;
}

int register_chrdev_region(dev_t from, unsigned count, const char *name)
{
	struct char_device_struct *cd;
	dev_t to = from + count;
	dev_t n, next;

	for (n = from; n < to; n = next) {
		next = MKDEV(MAJOR(n)+1, 0);
		if (next > to)
			next = to;
		cd = __register_chrdev_region(MAJOR(n), MINOR(n),
			       next - n, name);
		if (IS_ERR(cd))
			goto fail;
	}
	return 0;
fail:
	to = n;
	for (n = from; n < to; n = next) {
		next = MKDEV(MAJOR(n)+1, 0);
		kfree(__unregister_chrdev_region(MAJOR(n), MINOR(n), next - n));
	}
	return PTR_ERR(cd);
}

int alloc_chrdev_region(dev_t *dev, unsigned baseminor, unsigned count,
			const char *name)
{
	struct char_device_struct *cd;
	cd = __register_chrdev_region(0, baseminor, count, name);
	if (IS_ERR(cd))
		return PTR_ERR(cd);
	*dev = MKDEV(cd->major, cd->baseminor);
	return 0;
}

int register_chrdev(unsigned int major, const char *name,
		    const struct file_operations *fops)
{
	struct char_device_struct *cd;
	struct cdev *cdev;
	char *s;
	int err = -ENOMEM;

	cd = __register_chrdev_region(major, 0, 256, name);
	if (IS_ERR(cd))
		return PTR_ERR(cd);
	
	cdev = cdev_alloc();
	if (!cdev)
		goto out2;

	cdev->owner = fops->owner;
	cdev->ops = fops;
	kobject_set_name(&cdev->kobj, "%s", name);
	for (s = strchr(kobject_name(&cdev->kobj),'/'); s; s = strchr(s, '/'))
		*s = '!';
		
	err = cdev_add(cdev, MKDEV(cd->major, 0), 256);
	if (err)
		goto out;

	cd->cdev = cdev;

	return major ? 0 : cd->major;
out:
	kobject_put(&cdev->kobj);
out2:
	kfree(__unregister_chrdev_region(cd->major, 0, 256));
	return err;
}

void unregister_chrdev_region(dev_t from, unsigned count)
{
	dev_t to = from + count;
	dev_t n, next;

	for (n = from; n < to; n = next) {
		next = MKDEV(MAJOR(n)+1, 0);
		if (next > to)
			next = to;
		kfree(__unregister_chrdev_region(MAJOR(n), MINOR(n), next - n));
	}
}

int unregister_chrdev(unsigned int major, const char *name)
{
	struct char_device_struct *cd;
	cd = __unregister_chrdev_region(major, 0, 256);
	if (cd && cd->cdev)
		cdev_del(cd->cdev);
	kfree(cd);
	return 0;
}

static DEFINE_SPINLOCK(cdev_lock);

static struct kobject *cdev_get(struct cdev *p)
{
	struct module *owner = p->owner;
	struct kobject *kobj;

	if (owner && !try_module_get(owner))
		return NULL;
	kobj = kobject_get(&p->kobj);
	if (!kobj)
		module_put(owner);
	return kobj;
}

void cdev_put(struct cdev *p)
{
	if (p) {
		struct module *owner = p->owner;
		kobject_put(&p->kobj);
		module_put(owner);
	}
}

/*
 * Called every time a character special file is opened
 */
int chrdev_open(struct inode * inode, struct file * filp)
{
	struct cdev *p;
	struct cdev *new = NULL;
	int ret = 0;

	spin_lock(&cdev_lock);
	p = inode->i_cdev;
	if (!p) {
		struct kobject *kobj;
		int idx;
		spin_unlock(&cdev_lock);
		kobj = kobj_lookup(cdev_map, inode->i_rdev, &idx);
		if (!kobj)
			return -ENXIO;
		new = container_of(kobj, struct cdev, kobj);
		spin_lock(&cdev_lock);
		p = inode->i_cdev;
		if (!p) {
			inode->i_cdev = p = new;
			inode->i_cindex = idx;
			list_add(&inode->i_devices, &p->list);
			new = NULL;
		} else if (!cdev_get(p))
			ret = -ENXIO;
	} else if (!cdev_get(p))
		ret = -ENXIO;
	spin_unlock(&cdev_lock);
	cdev_put(new);
	if (ret)
		return ret;
	filp->f_op = fops_get(p->ops);
	if (!filp->f_op) {
		cdev_put(p);
		return -ENXIO;
	}
	if (filp->f_op->open) {
		lock_kernel();
		ret = filp->f_op->open(inode,filp);
		unlock_kernel();
	}
	if (ret)
		cdev_put(p);
	return ret;
}

void cd_forget(struct inode *inode)
{
	spin_lock(&cdev_lock);
	list_del_init(&inode->i_devices);
	inode->i_cdev = NULL;
	spin_unlock(&cdev_lock);
}

static void cdev_purge(struct cdev *cdev)
{
	spin_lock(&cdev_lock);
	while (!list_empty(&cdev->list)) {
		struct inode *inode;
		inode = container_of(cdev->list.next, struct inode, i_devices);
		list_del_init(&inode->i_devices);
		inode->i_cdev = NULL;
	}
	spin_unlock(&cdev_lock);
}

/*
 * Dummy default file-operations: the only thing this does
 * is contain the open that then fills in the correct operations
 * depending on the special file...
 */
const struct file_operations def_chr_fops = {
	.open = chrdev_open,
};

static struct kobject *exact_match(dev_t dev, int *part, void *data)
{
	struct cdev *p = data;
	return &p->kobj;
}

static int exact_lock(dev_t dev, void *data)
{
	struct cdev *p = data;
	return cdev_get(p) ? 0 : -1;
}

int cdev_add(struct cdev *p, dev_t dev, unsigned count)
{
	p->dev = dev;
	p->count = count;
	return kobj_map(cdev_map, dev, count, NULL, exact_match, exact_lock, p);
}

static void cdev_unmap(dev_t dev, unsigned count)
{
	kobj_unmap(cdev_map, dev, count);
}

void cdev_del(struct cdev *p)
{
	cdev_unmap(p->dev, p->count);
	kobject_put(&p->kobj);
}


static void cdev_default_release(struct kobject *kobj)
{
	struct cdev *p = container_of(kobj, struct cdev, kobj);
	cdev_purge(p);
}

static void cdev_dynamic_release(struct kobject *kobj)
{
	struct cdev *p = container_of(kobj, struct cdev, kobj);
	cdev_purge(p);
	kfree(p);
}

static struct kobj_type ktype_cdev_default = {
	.release	= cdev_default_release,
};

static struct kobj_type ktype_cdev_dynamic = {
	.release	= cdev_dynamic_release,
};

struct cdev *cdev_alloc(void)
{
	struct cdev *p = kzalloc(sizeof(struct cdev), GFP_KERNEL);
	if (p) {
		p->kobj.ktype = &ktype_cdev_dynamic;
		INIT_LIST_HEAD(&p->list);
		kobject_init(&p->kobj);
	}
	return p;
}

void cdev_init(struct cdev *cdev, const struct file_operations *fops)
{
	memset(cdev, 0, sizeof *cdev);
	INIT_LIST_HEAD(&cdev->list);
	cdev->kobj.ktype = &ktype_cdev_default;
	kobject_init(&cdev->kobj);
	cdev->ops = fops;
}

static struct kobject *base_probe(dev_t dev, int *part, void *data)
{
	if (request_module("char-major-%d-%d", MAJOR(dev), MINOR(dev)) > 0)
		/* Make old-style 2.4 aliases work */
		request_module("char-major-%d", MAJOR(dev));
	return NULL;
}

void __init chrdev_init(void)
{
	cdev_map = kobj_map_init(base_probe, &chrdevs_lock);
}


/* Let modules do char dev stuff */
EXPORT_SYMBOL(register_chrdev_region);
EXPORT_SYMBOL(unregister_chrdev_region);
EXPORT_SYMBOL(alloc_chrdev_region);
EXPORT_SYMBOL(cdev_init);
EXPORT_SYMBOL(cdev_alloc);
EXPORT_SYMBOL(cdev_del);
EXPORT_SYMBOL(cdev_add);
EXPORT_SYMBOL(register_chrdev);
EXPORT_SYMBOL(unregister_chrdev);
