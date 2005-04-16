#ifndef _LINUX_CDEV_H
#define _LINUX_CDEV_H
#ifdef __KERNEL__

struct cdev {
	struct kobject kobj;
	struct module *owner;
	struct file_operations *ops;
	struct list_head list;
	dev_t dev;
	unsigned int count;
};

void cdev_init(struct cdev *, struct file_operations *);

struct cdev *cdev_alloc(void);

void cdev_put(struct cdev *p);

int cdev_add(struct cdev *, dev_t, unsigned);

void cdev_del(struct cdev *);

void cd_forget(struct inode *);

#endif
#endif
