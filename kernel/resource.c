/*
 *	linux/kernel/resource.c
 *
 * Copyright (C) 1999	Linus Torvalds
 * Copyright (C) 1999	Martin Mares <mj@ucw.cz>
 *
 * Arbitrary resource management.
 */

#include <linux/config.h>
#include <linux/module.h>
#include <linux/sched.h>
#include <linux/errno.h>
#include <linux/ioport.h>
#include <linux/init.h>
#include <linux/slab.h>
#include <linux/spinlock.h>
#include <linux/fs.h>
#include <linux/proc_fs.h>
#include <linux/seq_file.h>
#include <asm/io.h>


struct resource ioport_resource = {
	.name	= "PCI IO",
	.start	= 0x0000,
	.end	= IO_SPACE_LIMIT,
	.flags	= IORESOURCE_IO,
};

EXPORT_SYMBOL(ioport_resource);

struct resource iomem_resource = {
	.name	= "PCI mem",
	.start	= 0UL,
	.end	= ~0UL,
	.flags	= IORESOURCE_MEM,
};

EXPORT_SYMBOL(iomem_resource);

static DEFINE_RWLOCK(resource_lock);

#ifdef CONFIG_PROC_FS

enum { MAX_IORES_LEVEL = 5 };

static void *r_next(struct seq_file *m, void *v, loff_t *pos)
{
	struct resource *p = v;
	(*pos)++;
	if (p->child)
		return p->child;
	while (!p->sibling && p->parent)
		p = p->parent;
	return p->sibling;
}

static void *r_start(struct seq_file *m, loff_t *pos)
	__acquires(resource_lock)
{
	struct resource *p = m->private;
	loff_t l = 0;
	read_lock(&resource_lock);
	for (p = p->child; p && l < *pos; p = r_next(m, p, &l))
		;
	return p;
}

static void r_stop(struct seq_file *m, void *v)
	__releases(resource_lock)
{
	read_unlock(&resource_lock);
}

static int r_show(struct seq_file *m, void *v)
{
	struct resource *root = m->private;
	struct resource *r = v, *p;
	int width = root->end < 0x10000 ? 4 : 8;
	int depth;

	for (depth = 0, p = r; depth < MAX_IORES_LEVEL; depth++, p = p->parent)
		if (p->parent == root)
			break;
	seq_printf(m, "%*s%0*lx-%0*lx : %s\n",
			depth * 2, "",
			width, r->start,
			width, r->end,
			r->name ? r->name : "<BAD>");
	return 0;
}

static struct seq_operations resource_op = {
	.start	= r_start,
	.next	= r_next,
	.stop	= r_stop,
	.show	= r_show,
};

static int ioports_open(struct inode *inode, struct file *file)
{
	int res = seq_open(file, &resource_op);
	if (!res) {
		struct seq_file *m = file->private_data;
		m->private = &ioport_resource;
	}
	return res;
}

static int iomem_open(struct inode *inode, struct file *file)
{
	int res = seq_open(file, &resource_op);
	if (!res) {
		struct seq_file *m = file->private_data;
		m->private = &iomem_resource;
	}
	return res;
}

static struct file_operations proc_ioports_operations = {
	.open		= ioports_open,
	.read		= seq_read,
	.llseek		= seq_lseek,
	.release	= seq_release,
};

static struct file_operations proc_iomem_operations = {
	.open		= iomem_open,
	.read		= seq_read,
	.llseek		= seq_lseek,
	.release	= seq_release,
};

static int __init ioresources_init(void)
{
	struct proc_dir_entry *entry;

	entry = create_proc_entry("ioports", 0, NULL);
	if (entry)
		entry->proc_fops = &proc_ioports_operations;
	entry = create_proc_entry("iomem", 0, NULL);
	if (entry)
		entry->proc_fops = &proc_iomem_operations;
	return 0;
}
__initcall(ioresources_init);

#endif /* CONFIG_PROC_FS */

/* Return the conflict entry if you can't request it */
static struct resource * __request_resource(struct resource *root, struct resource *new)
{
	unsigned long start = new->start;
	unsigned long end = new->end;
	struct resource *tmp, **p;

	if (end < start)
		return root;
	if (start < root->start)
		return root;
	if (end > root->end)
		return root;
	p = &root->child;
	for (;;) {
		tmp = *p;
		if (!tmp || tmp->start > end) {
			new->sibling = tmp;
			*p = new;
			new->parent = root;
			return NULL;
		}
		p = &tmp->sibling;
		if (tmp->end < start)
			continue;
		return tmp;
	}
}

static int __release_resource(struct resource *old)
{
	struct resource *tmp, **p;

	p = &old->parent->child;
	for (;;) {
		tmp = *p;
		if (!tmp)
			break;
		if (tmp == old) {
			*p = tmp->sibling;
			old->parent = NULL;
			return 0;
		}
		p = &tmp->sibling;
	}
	return -EINVAL;
}

int request_resource(struct resource *root, struct resource *new)
{
	struct resource *conflict;

	write_lock(&resource_lock);
	conflict = __request_resource(root, new);
	write_unlock(&resource_lock);
	return conflict ? -EBUSY : 0;
}

EXPORT_SYMBOL(request_resource);

struct resource *____request_resource(struct resource *root, struct resource *new)
{
	struct resource *conflict;

	write_lock(&resource_lock);
	conflict = __request_resource(root, new);
	write_unlock(&resource_lock);
	return conflict;
}

EXPORT_SYMBOL(____request_resource);

int release_resource(struct resource *old)
{
	int retval;

	write_lock(&resource_lock);
	retval = __release_resource(old);
	write_unlock(&resource_lock);
	return retval;
}

EXPORT_SYMBOL(release_resource);

/*
 * Find empty slot in the resource tree given range and alignment.
 */
static int find_resource(struct resource *root, struct resource *new,
			 unsigned long size,
			 unsigned long min, unsigned long max,
			 unsigned long align,
			 void (*alignf)(void *, struct resource *,
					unsigned long, unsigned long),
			 void *alignf_data)
{
	struct resource *this = root->child;

	new->start = root->start;
	/*
	 * Skip past an allocated resource that starts at 0, since the assignment
	 * of this->start - 1 to new->end below would cause an underflow.
	 */
	if (this && this->start == 0) {
		new->start = this->end + 1;
		this = this->sibling;
	}
	for(;;) {
		if (this)
			new->end = this->start - 1;
		else
			new->end = root->end;
		if (new->start < min)
			new->start = min;
		if (new->end > max)
			new->end = max;
		new->start = ALIGN(new->start, align);
		if (alignf)
			alignf(alignf_data, new, size, align);
		if (new->start < new->end && new->end - new->start >= size - 1) {
			new->end = new->start + size - 1;
			return 0;
		}
		if (!this)
			break;
		new->start = this->end + 1;
		this = this->sibling;
	}
	return -EBUSY;
}

/*
 * Allocate empty slot in the resource tree given range and alignment.
 */
int allocate_resource(struct resource *root, struct resource *new,
		      unsigned long size,
		      unsigned long min, unsigned long max,
		      unsigned long align,
		      void (*alignf)(void *, struct resource *,
				     unsigned long, unsigned long),
		      void *alignf_data)
{
	int err;

	write_lock(&resource_lock);
	err = find_resource(root, new, size, min, max, align, alignf, alignf_data);
	if (err >= 0 && __request_resource(root, new))
		err = -EBUSY;
	write_unlock(&resource_lock);
	return err;
}

EXPORT_SYMBOL(allocate_resource);

/**
 * insert_resource - Inserts a resource in the resource tree
 * @parent: parent of the new resource
 * @new: new resource to insert
 *
 * Returns 0 on success, -EBUSY if the resource can't be inserted.
 *
 * This function is equivalent of request_resource when no conflict
 * happens. If a conflict happens, and the conflicting resources
 * entirely fit within the range of the new resource, then the new
 * resource is inserted and the conflicting resources become childs of
 * the new resource.  Otherwise the new resource becomes the child of
 * the conflicting resource
 */
int insert_resource(struct resource *parent, struct resource *new)
{
	int result;
	struct resource *first, *next;

	write_lock(&resource_lock);
 begin:
 	result = 0;
	first = __request_resource(parent, new);
	if (!first)
		goto out;

	result = -EBUSY;
	if (first == parent)
		goto out;

	/* Resource fully contained by the clashing resource? Recurse into it */
	if (first->start <= new->start && first->end >= new->end) {
		parent = first;
		goto begin;
	}

	for (next = first; ; next = next->sibling) {
		/* Partial overlap? Bad, and unfixable */
		if (next->start < new->start || next->end > new->end)
			goto out;
		if (!next->sibling)
			break;
		if (next->sibling->start > new->end)
			break;
	}

	result = 0;

	new->parent = parent;
	new->sibling = next->sibling;
	new->child = first;

	next->sibling = NULL;
	for (next = first; next; next = next->sibling)
		next->parent = new;

	if (parent->child == first) {
		parent->child = new;
	} else {
		next = parent->child;
		while (next->sibling != first)
			next = next->sibling;
		next->sibling = new;
	}

 out:
	write_unlock(&resource_lock);
	return result;
}

EXPORT_SYMBOL(insert_resource);

/*
 * Given an existing resource, change its start and size to match the
 * arguments.  Returns -EBUSY if it can't fit.  Existing children of
 * the resource are assumed to be immutable.
 */
int adjust_resource(struct resource *res, unsigned long start, unsigned long size)
{
	struct resource *tmp, *parent = res->parent;
	unsigned long end = start + size - 1;
	int result = -EBUSY;

	write_lock(&resource_lock);

	if ((start < parent->start) || (end > parent->end))
		goto out;

	for (tmp = res->child; tmp; tmp = tmp->sibling) {
		if ((tmp->start < start) || (tmp->end > end))
			goto out;
	}

	if (res->sibling && (res->sibling->start <= end))
		goto out;

	tmp = parent->child;
	if (tmp != res) {
		while (tmp->sibling != res)
			tmp = tmp->sibling;
		if (start <= tmp->end)
			goto out;
	}

	res->start = start;
	res->end = end;
	result = 0;

 out:
	write_unlock(&resource_lock);
	return result;
}

EXPORT_SYMBOL(adjust_resource);

/*
 * This is compatibility stuff for IO resources.
 *
 * Note how this, unlike the above, knows about
 * the IO flag meanings (busy etc).
 *
 * Request-region creates a new busy region.
 *
 * Check-region returns non-zero if the area is already busy
 *
 * Release-region releases a matching busy region.
 */
struct resource * __request_region(struct resource *parent, unsigned long start, unsigned long n, const char *name)
{
	struct resource *res = kzalloc(sizeof(*res), GFP_KERNEL);

	if (res) {
		res->name = name;
		res->start = start;
		res->end = start + n - 1;
		res->flags = IORESOURCE_BUSY;

		write_lock(&resource_lock);

		for (;;) {
			struct resource *conflict;

			conflict = __request_resource(parent, res);
			if (!conflict)
				break;
			if (conflict != parent) {
				parent = conflict;
				if (!(conflict->flags & IORESOURCE_BUSY))
					continue;
			}

			/* Uhhuh, that didn't work out.. */
			kfree(res);
			res = NULL;
			break;
		}
		write_unlock(&resource_lock);
	}
	return res;
}

EXPORT_SYMBOL(__request_region);

int __check_region(struct resource *parent, unsigned long start, unsigned long n)
{
	struct resource * res;

	res = __request_region(parent, start, n, "check-region");
	if (!res)
		return -EBUSY;

	release_resource(res);
	kfree(res);
	return 0;
}

EXPORT_SYMBOL(__check_region);

void __release_region(struct resource *parent, unsigned long start, unsigned long n)
{
	struct resource **p;
	unsigned long end;

	p = &parent->child;
	end = start + n - 1;

	write_lock(&resource_lock);

	for (;;) {
		struct resource *res = *p;

		if (!res)
			break;
		if (res->start <= start && res->end >= end) {
			if (!(res->flags & IORESOURCE_BUSY)) {
				p = &res->child;
				continue;
			}
			if (res->start != start || res->end != end)
				break;
			*p = res->sibling;
			write_unlock(&resource_lock);
			kfree(res);
			return;
		}
		p = &res->sibling;
	}

	write_unlock(&resource_lock);

	printk(KERN_WARNING "Trying to free nonexistent resource <%08lx-%08lx>\n", start, end);
}

EXPORT_SYMBOL(__release_region);

/*
 * Called from init/main.c to reserve IO ports.
 */
#define MAXRESERVE 4
static int __init reserve_setup(char *str)
{
	static int reserved;
	static struct resource reserve[MAXRESERVE];

	for (;;) {
		int io_start, io_num;
		int x = reserved;

		if (get_option (&str, &io_start) != 2)
			break;
		if (get_option (&str, &io_num)   == 0)
			break;
		if (x < MAXRESERVE) {
			struct resource *res = reserve + x;
			res->name = "reserved";
			res->start = io_start;
			res->end = io_start + io_num - 1;
			res->flags = IORESOURCE_BUSY;
			res->child = NULL;
			if (request_resource(res->start >= 0x10000 ? &iomem_resource : &ioport_resource, res) == 0)
				reserved = x+1;
		}
	}
	return 1;
}

__setup("reserve=", reserve_setup);
