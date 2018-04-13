/*
 *	linux/kernel/resource.c
 *
 * Copyright (C) 1999	Linus Torvalds
 * Copyright (C) 1999	Martin Mares <mj@ucw.cz>
 *
 * Arbitrary resource management.
 */

#define pr_fmt(fmt) KBUILD_MODNAME ": " fmt

#include <linux/export.h>
#include <linux/errno.h>
#include <linux/ioport.h>
#include <linux/init.h>
#include <linux/slab.h>
#include <linux/spinlock.h>
#include <linux/fs.h>
#include <linux/proc_fs.h>
#include <linux/sched.h>
#include <linux/seq_file.h>
#include <linux/device.h>
#include <linux/pfn.h>
#include <linux/mm.h>
#include <linux/resource_ext.h>
#include <asm/io.h>


struct resource ioport_resource = {
	.name	= "PCI IO",
	.start	= 0,
	.end	= IO_SPACE_LIMIT,
	.flags	= IORESOURCE_IO,
};
EXPORT_SYMBOL(ioport_resource);

struct resource iomem_resource = {
	.name	= "PCI mem",
	.start	= 0,
	.end	= -1,
	.flags	= IORESOURCE_MEM,
};
EXPORT_SYMBOL(iomem_resource);

/* constraints to be met while allocating resources */
struct resource_constraint {
	resource_size_t min, max, align;
	resource_size_t (*alignf)(void *, const struct resource *,
			resource_size_t, resource_size_t);
	void *alignf_data;
};

static DEFINE_RWLOCK(resource_lock);

/*
 * For memory hotplug, there is no way to free resource entries allocated
 * by boot mem after the system is up. So for reusing the resource entry
 * we need to remember the resource.
 */
static struct resource *bootmem_resource_free;
static DEFINE_SPINLOCK(bootmem_resource_lock);

static struct resource *next_resource(struct resource *p, bool sibling_only)
{
	/* Caller wants to traverse through siblings only */
	if (sibling_only)
		return p->sibling;

	if (p->child)
		return p->child;
	while (!p->sibling && p->parent)
		p = p->parent;
	return p->sibling;
}

static void *r_next(struct seq_file *m, void *v, loff_t *pos)
{
	struct resource *p = v;
	(*pos)++;
	return (void *)next_resource(p, false);
}

#ifdef CONFIG_PROC_FS

enum { MAX_IORES_LEVEL = 5 };

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
	unsigned long long start, end;
	int width = root->end < 0x10000 ? 4 : 8;
	int depth;

	for (depth = 0, p = r; depth < MAX_IORES_LEVEL; depth++, p = p->parent)
		if (p->parent == root)
			break;

	if (file_ns_capable(m->file, &init_user_ns, CAP_SYS_ADMIN)) {
		start = r->start;
		end = r->end;
	} else {
		start = end = 0;
	}

	seq_printf(m, "%*s%0*llx-%0*llx : %s\n",
			depth * 2, "",
			width, start,
			width, end,
			r->name ? r->name : "<BAD>");
	return 0;
}

static const struct seq_operations resource_op = {
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

static const struct file_operations proc_ioports_operations = {
	.open		= ioports_open,
	.read		= seq_read,
	.llseek		= seq_lseek,
	.release	= seq_release,
};

static const struct file_operations proc_iomem_operations = {
	.open		= iomem_open,
	.read		= seq_read,
	.llseek		= seq_lseek,
	.release	= seq_release,
};

static int __init ioresources_init(void)
{
	proc_create("ioports", 0, NULL, &proc_ioports_operations);
	proc_create("iomem", 0, NULL, &proc_iomem_operations);
	return 0;
}
__initcall(ioresources_init);

#endif /* CONFIG_PROC_FS */

static void free_resource(struct resource *res)
{
	if (!res)
		return;

	if (!PageSlab(virt_to_head_page(res))) {
		spin_lock(&bootmem_resource_lock);
		res->sibling = bootmem_resource_free;
		bootmem_resource_free = res;
		spin_unlock(&bootmem_resource_lock);
	} else {
		kfree(res);
	}
}

static struct resource *alloc_resource(gfp_t flags)
{
	struct resource *res = NULL;

	spin_lock(&bootmem_resource_lock);
	if (bootmem_resource_free) {
		res = bootmem_resource_free;
		bootmem_resource_free = res->sibling;
	}
	spin_unlock(&bootmem_resource_lock);

	if (res)
		memset(res, 0, sizeof(struct resource));
	else
		res = kzalloc(sizeof(struct resource), flags);

	return res;
}

/* Return the conflict entry if you can't request it */
static struct resource * __request_resource(struct resource *root, struct resource *new)
{
	resource_size_t start = new->start;
	resource_size_t end = new->end;
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

static int __release_resource(struct resource *old, bool release_child)
{
	struct resource *tmp, **p, *chd;

	p = &old->parent->child;
	for (;;) {
		tmp = *p;
		if (!tmp)
			break;
		if (tmp == old) {
			if (release_child || !(tmp->child)) {
				*p = tmp->sibling;
			} else {
				for (chd = tmp->child;; chd = chd->sibling) {
					chd->parent = tmp->parent;
					if (!(chd->sibling))
						break;
				}
				*p = tmp->child;
				chd->sibling = tmp->sibling;
			}
			old->parent = NULL;
			return 0;
		}
		p = &tmp->sibling;
	}
	return -EINVAL;
}

static void __release_child_resources(struct resource *r)
{
	struct resource *tmp, *p;
	resource_size_t size;

	p = r->child;
	r->child = NULL;
	while (p) {
		tmp = p;
		p = p->sibling;

		tmp->parent = NULL;
		tmp->sibling = NULL;
		__release_child_resources(tmp);

		printk(KERN_DEBUG "release child resource %pR\n", tmp);
		/* need to restore size, and keep flags */
		size = resource_size(tmp);
		tmp->start = 0;
		tmp->end = size - 1;
	}
}

void release_child_resources(struct resource *r)
{
	write_lock(&resource_lock);
	__release_child_resources(r);
	write_unlock(&resource_lock);
}

/**
 * request_resource_conflict - request and reserve an I/O or memory resource
 * @root: root resource descriptor
 * @new: resource descriptor desired by caller
 *
 * Returns 0 for success, conflict resource on error.
 */
struct resource *request_resource_conflict(struct resource *root, struct resource *new)
{
	struct resource *conflict;

	write_lock(&resource_lock);
	conflict = __request_resource(root, new);
	write_unlock(&resource_lock);
	return conflict;
}

/**
 * request_resource - request and reserve an I/O or memory resource
 * @root: root resource descriptor
 * @new: resource descriptor desired by caller
 *
 * Returns 0 for success, negative error code on error.
 */
int request_resource(struct resource *root, struct resource *new)
{
	struct resource *conflict;

	conflict = request_resource_conflict(root, new);
	return conflict ? -EBUSY : 0;
}

EXPORT_SYMBOL(request_resource);

/**
 * release_resource - release a previously reserved resource
 * @old: resource pointer
 */
int release_resource(struct resource *old)
{
	int retval;

	write_lock(&resource_lock);
	retval = __release_resource(old, true);
	write_unlock(&resource_lock);
	return retval;
}

EXPORT_SYMBOL(release_resource);

/*
 * Finds the lowest iomem resource existing within [res->start.res->end).
 * The caller must specify res->start, res->end, res->flags, and optionally
 * desc.  If found, returns 0, res is overwritten, if not found, returns -1.
 * This function walks the whole tree and not just first level children until
 * and unless first_level_children_only is true.
 */
static int find_next_iomem_res(struct resource *res, unsigned long desc,
			       bool first_level_children_only)
{
	resource_size_t start, end;
	struct resource *p;
	bool sibling_only = false;

	BUG_ON(!res);

	start = res->start;
	end = res->end;
	BUG_ON(start >= end);

	if (first_level_children_only)
		sibling_only = true;

	read_lock(&resource_lock);

	for (p = iomem_resource.child; p; p = next_resource(p, sibling_only)) {
		if ((p->flags & res->flags) != res->flags)
			continue;
		if ((desc != IORES_DESC_NONE) && (desc != p->desc))
			continue;
		if (p->start > end) {
			p = NULL;
			break;
		}
		if ((p->end >= start) && (p->start < end))
			break;
	}

	read_unlock(&resource_lock);
	if (!p)
		return -1;
	/* copy data */
	if (res->start < p->start)
		res->start = p->start;
	if (res->end > p->end)
		res->end = p->end;
	return 0;
}

/*
 * Walks through iomem resources and calls func() with matching resource
 * ranges. This walks through whole tree and not just first level children.
 * All the memory ranges which overlap start,end and also match flags and
 * desc are valid candidates.
 *
 * @desc: I/O resource descriptor. Use IORES_DESC_NONE to skip @desc check.
 * @flags: I/O resource flags
 * @start: start addr
 * @end: end addr
 *
 * NOTE: For a new descriptor search, define a new IORES_DESC in
 * <linux/ioport.h> and set it in 'desc' of a target resource entry.
 */
int walk_iomem_res_desc(unsigned long desc, unsigned long flags, u64 start,
		u64 end, void *arg, int (*func)(u64, u64, void *))
{
	struct resource res;
	u64 orig_end;
	int ret = -1;

	res.start = start;
	res.end = end;
	res.flags = flags;
	orig_end = res.end;

	while ((res.start < res.end) &&
		(!find_next_iomem_res(&res, desc, false))) {

		ret = (*func)(res.start, res.end, arg);
		if (ret)
			break;

		res.start = res.end + 1;
		res.end = orig_end;
	}

	return ret;
}

/*
 * This function calls the @func callback against all memory ranges of type
 * System RAM which are marked as IORESOURCE_SYSTEM_RAM and IORESOUCE_BUSY.
 * Now, this function is only for System RAM, it deals with full ranges and
 * not PFNs. If resources are not PFN-aligned, dealing with PFNs can truncate
 * ranges.
 */
int walk_system_ram_res(u64 start, u64 end, void *arg,
				int (*func)(u64, u64, void *))
{
	struct resource res;
	u64 orig_end;
	int ret = -1;

	res.start = start;
	res.end = end;
	res.flags = IORESOURCE_SYSTEM_RAM | IORESOURCE_BUSY;
	orig_end = res.end;
	while ((res.start < res.end) &&
		(!find_next_iomem_res(&res, IORES_DESC_NONE, true))) {
		ret = (*func)(res.start, res.end, arg);
		if (ret)
			break;
		res.start = res.end + 1;
		res.end = orig_end;
	}
	return ret;
}

#if !defined(CONFIG_ARCH_HAS_WALK_MEMORY)

/*
 * This function calls the @func callback against all memory ranges of type
 * System RAM which are marked as IORESOURCE_SYSTEM_RAM and IORESOUCE_BUSY.
 * It is to be used only for System RAM.
 */
int walk_system_ram_range(unsigned long start_pfn, unsigned long nr_pages,
		void *arg, int (*func)(unsigned long, unsigned long, void *))
{
	struct resource res;
	unsigned long pfn, end_pfn;
	u64 orig_end;
	int ret = -1;

	res.start = (u64) start_pfn << PAGE_SHIFT;
	res.end = ((u64)(start_pfn + nr_pages) << PAGE_SHIFT) - 1;
	res.flags = IORESOURCE_SYSTEM_RAM | IORESOURCE_BUSY;
	orig_end = res.end;
	while ((res.start < res.end) &&
		(find_next_iomem_res(&res, IORES_DESC_NONE, true) >= 0)) {
		pfn = (res.start + PAGE_SIZE - 1) >> PAGE_SHIFT;
		end_pfn = (res.end + 1) >> PAGE_SHIFT;
		if (end_pfn > pfn)
			ret = (*func)(pfn, end_pfn - pfn, arg);
		if (ret)
			break;
		res.start = res.end + 1;
		res.end = orig_end;
	}
	return ret;
}

#endif

static int __is_ram(unsigned long pfn, unsigned long nr_pages, void *arg)
{
	return 1;
}
/*
 * This generic page_is_ram() returns true if specified address is
 * registered as System RAM in iomem_resource list.
 */
int __weak page_is_ram(unsigned long pfn)
{
	return walk_system_ram_range(pfn, 1, NULL, __is_ram) == 1;
}
EXPORT_SYMBOL_GPL(page_is_ram);

/**
 * region_intersects() - determine intersection of region with known resources
 * @start: region start address
 * @size: size of region
 * @flags: flags of resource (in iomem_resource)
 * @desc: descriptor of resource (in iomem_resource) or IORES_DESC_NONE
 *
 * Check if the specified region partially overlaps or fully eclipses a
 * resource identified by @flags and @desc (optional with IORES_DESC_NONE).
 * Return REGION_DISJOINT if the region does not overlap @flags/@desc,
 * return REGION_MIXED if the region overlaps @flags/@desc and another
 * resource, and return REGION_INTERSECTS if the region overlaps @flags/@desc
 * and no other defined resource. Note that REGION_INTERSECTS is also
 * returned in the case when the specified region overlaps RAM and undefined
 * memory holes.
 *
 * region_intersect() is used by memory remapping functions to ensure
 * the user is not remapping RAM and is a vast speed up over walking
 * through the resource table page by page.
 */
int region_intersects(resource_size_t start, size_t size, unsigned long flags,
		      unsigned long desc)
{
	resource_size_t end = start + size - 1;
	int type = 0; int other = 0;
	struct resource *p;

	read_lock(&resource_lock);
	for (p = iomem_resource.child; p ; p = p->sibling) {
		bool is_type = (((p->flags & flags) == flags) &&
				((desc == IORES_DESC_NONE) ||
				 (desc == p->desc)));

		if (start >= p->start && start <= p->end)
			is_type ? type++ : other++;
		if (end >= p->start && end <= p->end)
			is_type ? type++ : other++;
		if (p->start >= start && p->end <= end)
			is_type ? type++ : other++;
	}
	read_unlock(&resource_lock);

	if (other == 0)
		return type ? REGION_INTERSECTS : REGION_DISJOINT;

	if (type)
		return REGION_MIXED;

	return REGION_DISJOINT;
}
EXPORT_SYMBOL_GPL(region_intersects);

void __weak arch_remove_reservations(struct resource *avail)
{
}

static resource_size_t simple_align_resource(void *data,
					     const struct resource *avail,
					     resource_size_t size,
					     resource_size_t align)
{
	return avail->start;
}

static void resource_clip(struct resource *res, resource_size_t min,
			  resource_size_t max)
{
	if (res->start < min)
		res->start = min;
	if (res->end > max)
		res->end = max;
}

/*
 * Find empty slot in the resource tree with the given range and
 * alignment constraints
 */
static int __find_resource(struct resource *root, struct resource *old,
			 struct resource *new,
			 resource_size_t  size,
			 struct resource_constraint *constraint)
{
	struct resource *this = root->child;
	struct resource tmp = *new, avail, alloc;

	tmp.start = root->start;
	/*
	 * Skip past an allocated resource that starts at 0, since the assignment
	 * of this->start - 1 to tmp->end below would cause an underflow.
	 */
	if (this && this->start == root->start) {
		tmp.start = (this == old) ? old->start : this->end + 1;
		this = this->sibling;
	}
	for(;;) {
		if (this)
			tmp.end = (this == old) ?  this->end : this->start - 1;
		else
			tmp.end = root->end;

		if (tmp.end < tmp.start)
			goto next;

		resource_clip(&tmp, constraint->min, constraint->max);
		arch_remove_reservations(&tmp);

		/* Check for overflow after ALIGN() */
		avail.start = ALIGN(tmp.start, constraint->align);
		avail.end = tmp.end;
		avail.flags = new->flags & ~IORESOURCE_UNSET;
		if (avail.start >= tmp.start) {
			alloc.flags = avail.flags;
			alloc.start = constraint->alignf(constraint->alignf_data, &avail,
					size, constraint->align);
			alloc.end = alloc.start + size - 1;
			if (alloc.start <= alloc.end &&
			    resource_contains(&avail, &alloc)) {
				new->start = alloc.start;
				new->end = alloc.end;
				return 0;
			}
		}

next:		if (!this || this->end == root->end)
			break;

		if (this != old)
			tmp.start = this->end + 1;
		this = this->sibling;
	}
	return -EBUSY;
}

/*
 * Find empty slot in the resource tree given range and alignment.
 */
static int find_resource(struct resource *root, struct resource *new,
			resource_size_t size,
			struct resource_constraint  *constraint)
{
	return  __find_resource(root, NULL, new, size, constraint);
}

/**
 * reallocate_resource - allocate a slot in the resource tree given range & alignment.
 *	The resource will be relocated if the new size cannot be reallocated in the
 *	current location.
 *
 * @root: root resource descriptor
 * @old:  resource descriptor desired by caller
 * @newsize: new size of the resource descriptor
 * @constraint: the size and alignment constraints to be met.
 */
static int reallocate_resource(struct resource *root, struct resource *old,
			resource_size_t newsize,
			struct resource_constraint  *constraint)
{
	int err=0;
	struct resource new = *old;
	struct resource *conflict;

	write_lock(&resource_lock);

	if ((err = __find_resource(root, old, &new, newsize, constraint)))
		goto out;

	if (resource_contains(&new, old)) {
		old->start = new.start;
		old->end = new.end;
		goto out;
	}

	if (old->child) {
		err = -EBUSY;
		goto out;
	}

	if (resource_contains(old, &new)) {
		old->start = new.start;
		old->end = new.end;
	} else {
		__release_resource(old, true);
		*old = new;
		conflict = __request_resource(root, old);
		BUG_ON(conflict);
	}
out:
	write_unlock(&resource_lock);
	return err;
}


/**
 * allocate_resource - allocate empty slot in the resource tree given range & alignment.
 * 	The resource will be reallocated with a new size if it was already allocated
 * @root: root resource descriptor
 * @new: resource descriptor desired by caller
 * @size: requested resource region size
 * @min: minimum boundary to allocate
 * @max: maximum boundary to allocate
 * @align: alignment requested, in bytes
 * @alignf: alignment function, optional, called if not NULL
 * @alignf_data: arbitrary data to pass to the @alignf function
 */
int allocate_resource(struct resource *root, struct resource *new,
		      resource_size_t size, resource_size_t min,
		      resource_size_t max, resource_size_t align,
		      resource_size_t (*alignf)(void *,
						const struct resource *,
						resource_size_t,
						resource_size_t),
		      void *alignf_data)
{
	int err;
	struct resource_constraint constraint;

	if (!alignf)
		alignf = simple_align_resource;

	constraint.min = min;
	constraint.max = max;
	constraint.align = align;
	constraint.alignf = alignf;
	constraint.alignf_data = alignf_data;

	if ( new->parent ) {
		/* resource is already allocated, try reallocating with
		   the new constraints */
		return reallocate_resource(root, new, size, &constraint);
	}

	write_lock(&resource_lock);
	err = find_resource(root, new, size, &constraint);
	if (err >= 0 && __request_resource(root, new))
		err = -EBUSY;
	write_unlock(&resource_lock);
	return err;
}

EXPORT_SYMBOL(allocate_resource);

/**
 * lookup_resource - find an existing resource by a resource start address
 * @root: root resource descriptor
 * @start: resource start address
 *
 * Returns a pointer to the resource if found, NULL otherwise
 */
struct resource *lookup_resource(struct resource *root, resource_size_t start)
{
	struct resource *res;

	read_lock(&resource_lock);
	for (res = root->child; res; res = res->sibling) {
		if (res->start == start)
			break;
	}
	read_unlock(&resource_lock);

	return res;
}

/*
 * Insert a resource into the resource tree. If successful, return NULL,
 * otherwise return the conflicting resource (compare to __request_resource())
 */
static struct resource * __insert_resource(struct resource *parent, struct resource *new)
{
	struct resource *first, *next;

	for (;; parent = first) {
		first = __request_resource(parent, new);
		if (!first)
			return first;

		if (first == parent)
			return first;
		if (WARN_ON(first == new))	/* duplicated insertion */
			return first;

		if ((first->start > new->start) || (first->end < new->end))
			break;
		if ((first->start == new->start) && (first->end == new->end))
			break;
	}

	for (next = first; ; next = next->sibling) {
		/* Partial overlap? Bad, and unfixable */
		if (next->start < new->start || next->end > new->end)
			return next;
		if (!next->sibling)
			break;
		if (next->sibling->start > new->end)
			break;
	}

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
	return NULL;
}

/**
 * insert_resource_conflict - Inserts resource in the resource tree
 * @parent: parent of the new resource
 * @new: new resource to insert
 *
 * Returns 0 on success, conflict resource if the resource can't be inserted.
 *
 * This function is equivalent to request_resource_conflict when no conflict
 * happens. If a conflict happens, and the conflicting resources
 * entirely fit within the range of the new resource, then the new
 * resource is inserted and the conflicting resources become children of
 * the new resource.
 *
 * This function is intended for producers of resources, such as FW modules
 * and bus drivers.
 */
struct resource *insert_resource_conflict(struct resource *parent, struct resource *new)
{
	struct resource *conflict;

	write_lock(&resource_lock);
	conflict = __insert_resource(parent, new);
	write_unlock(&resource_lock);
	return conflict;
}

/**
 * insert_resource - Inserts a resource in the resource tree
 * @parent: parent of the new resource
 * @new: new resource to insert
 *
 * Returns 0 on success, -EBUSY if the resource can't be inserted.
 *
 * This function is intended for producers of resources, such as FW modules
 * and bus drivers.
 */
int insert_resource(struct resource *parent, struct resource *new)
{
	struct resource *conflict;

	conflict = insert_resource_conflict(parent, new);
	return conflict ? -EBUSY : 0;
}
EXPORT_SYMBOL_GPL(insert_resource);

/**
 * insert_resource_expand_to_fit - Insert a resource into the resource tree
 * @root: root resource descriptor
 * @new: new resource to insert
 *
 * Insert a resource into the resource tree, possibly expanding it in order
 * to make it encompass any conflicting resources.
 */
void insert_resource_expand_to_fit(struct resource *root, struct resource *new)
{
	if (new->parent)
		return;

	write_lock(&resource_lock);
	for (;;) {
		struct resource *conflict;

		conflict = __insert_resource(root, new);
		if (!conflict)
			break;
		if (conflict == root)
			break;

		/* Ok, expand resource to cover the conflict, then try again .. */
		if (conflict->start < new->start)
			new->start = conflict->start;
		if (conflict->end > new->end)
			new->end = conflict->end;

		printk("Expanded resource %s due to conflict with %s\n", new->name, conflict->name);
	}
	write_unlock(&resource_lock);
}

/**
 * remove_resource - Remove a resource in the resource tree
 * @old: resource to remove
 *
 * Returns 0 on success, -EINVAL if the resource is not valid.
 *
 * This function removes a resource previously inserted by insert_resource()
 * or insert_resource_conflict(), and moves the children (if any) up to
 * where they were before.  insert_resource() and insert_resource_conflict()
 * insert a new resource, and move any conflicting resources down to the
 * children of the new resource.
 *
 * insert_resource(), insert_resource_conflict() and remove_resource() are
 * intended for producers of resources, such as FW modules and bus drivers.
 */
int remove_resource(struct resource *old)
{
	int retval;

	write_lock(&resource_lock);
	retval = __release_resource(old, false);
	write_unlock(&resource_lock);
	return retval;
}
EXPORT_SYMBOL_GPL(remove_resource);

static int __adjust_resource(struct resource *res, resource_size_t start,
				resource_size_t size)
{
	struct resource *tmp, *parent = res->parent;
	resource_size_t end = start + size - 1;
	int result = -EBUSY;

	if (!parent)
		goto skip;

	if ((start < parent->start) || (end > parent->end))
		goto out;

	if (res->sibling && (res->sibling->start <= end))
		goto out;

	tmp = parent->child;
	if (tmp != res) {
		while (tmp->sibling != res)
			tmp = tmp->sibling;
		if (start <= tmp->end)
			goto out;
	}

skip:
	for (tmp = res->child; tmp; tmp = tmp->sibling)
		if ((tmp->start < start) || (tmp->end > end))
			goto out;

	res->start = start;
	res->end = end;
	result = 0;

 out:
	return result;
}

/**
 * adjust_resource - modify a resource's start and size
 * @res: resource to modify
 * @start: new start value
 * @size: new size
 *
 * Given an existing resource, change its start and size to match the
 * arguments.  Returns 0 on success, -EBUSY if it can't fit.
 * Existing children of the resource are assumed to be immutable.
 */
int adjust_resource(struct resource *res, resource_size_t start,
			resource_size_t size)
{
	int result;

	write_lock(&resource_lock);
	result = __adjust_resource(res, start, size);
	write_unlock(&resource_lock);
	return result;
}
EXPORT_SYMBOL(adjust_resource);

static void __init __reserve_region_with_split(struct resource *root,
		resource_size_t start, resource_size_t end,
		const char *name)
{
	struct resource *parent = root;
	struct resource *conflict;
	struct resource *res = alloc_resource(GFP_ATOMIC);
	struct resource *next_res = NULL;

	if (!res)
		return;

	res->name = name;
	res->start = start;
	res->end = end;
	res->flags = IORESOURCE_BUSY;
	res->desc = IORES_DESC_NONE;

	while (1) {

		conflict = __request_resource(parent, res);
		if (!conflict) {
			if (!next_res)
				break;
			res = next_res;
			next_res = NULL;
			continue;
		}

		/* conflict covered whole area */
		if (conflict->start <= res->start &&
				conflict->end >= res->end) {
			free_resource(res);
			WARN_ON(next_res);
			break;
		}

		/* failed, split and try again */
		if (conflict->start > res->start) {
			end = res->end;
			res->end = conflict->start - 1;
			if (conflict->end < end) {
				next_res = alloc_resource(GFP_ATOMIC);
				if (!next_res) {
					free_resource(res);
					break;
				}
				next_res->name = name;
				next_res->start = conflict->end + 1;
				next_res->end = end;
				next_res->flags = IORESOURCE_BUSY;
				next_res->desc = IORES_DESC_NONE;
			}
		} else {
			res->start = conflict->end + 1;
		}
	}

}

void __init reserve_region_with_split(struct resource *root,
		resource_size_t start, resource_size_t end,
		const char *name)
{
	int abort = 0;

	write_lock(&resource_lock);
	if (root->start > start || root->end < end) {
		pr_err("requested range [0x%llx-0x%llx] not in root %pr\n",
		       (unsigned long long)start, (unsigned long long)end,
		       root);
		if (start > root->end || end < root->start)
			abort = 1;
		else {
			if (end > root->end)
				end = root->end;
			if (start < root->start)
				start = root->start;
			pr_err("fixing request to [0x%llx-0x%llx]\n",
			       (unsigned long long)start,
			       (unsigned long long)end);
		}
		dump_stack();
	}
	if (!abort)
		__reserve_region_with_split(root, start, end, name);
	write_unlock(&resource_lock);
}

/**
 * resource_alignment - calculate resource's alignment
 * @res: resource pointer
 *
 * Returns alignment on success, 0 (invalid alignment) on failure.
 */
resource_size_t resource_alignment(struct resource *res)
{
	switch (res->flags & (IORESOURCE_SIZEALIGN | IORESOURCE_STARTALIGN)) {
	case IORESOURCE_SIZEALIGN:
		return resource_size(res);
	case IORESOURCE_STARTALIGN:
		return res->start;
	default:
		return 0;
	}
}

/*
 * This is compatibility stuff for IO resources.
 *
 * Note how this, unlike the above, knows about
 * the IO flag meanings (busy etc).
 *
 * request_region creates a new busy region.
 *
 * release_region releases a matching busy region.
 */

static DECLARE_WAIT_QUEUE_HEAD(muxed_resource_wait);

/**
 * __request_region - create a new busy resource region
 * @parent: parent resource descriptor
 * @start: resource start address
 * @n: resource region size
 * @name: reserving caller's ID string
 * @flags: IO resource flags
 */
struct resource * __request_region(struct resource *parent,
				   resource_size_t start, resource_size_t n,
				   const char *name, int flags)
{
	DECLARE_WAITQUEUE(wait, current);
	struct resource *res = alloc_resource(GFP_KERNEL);

	if (!res)
		return NULL;

	res->name = name;
	res->start = start;
	res->end = start + n - 1;

	write_lock(&resource_lock);

	for (;;) {
		struct resource *conflict;

		res->flags = resource_type(parent) | resource_ext_type(parent);
		res->flags |= IORESOURCE_BUSY | flags;
		res->desc = parent->desc;

		conflict = __request_resource(parent, res);
		if (!conflict)
			break;
		if (conflict != parent) {
			if (!(conflict->flags & IORESOURCE_BUSY)) {
				parent = conflict;
				continue;
			}
		}
		if (conflict->flags & flags & IORESOURCE_MUXED) {
			add_wait_queue(&muxed_resource_wait, &wait);
			write_unlock(&resource_lock);
			set_current_state(TASK_UNINTERRUPTIBLE);
			schedule();
			remove_wait_queue(&muxed_resource_wait, &wait);
			write_lock(&resource_lock);
			continue;
		}
		/* Uhhuh, that didn't work out.. */
		free_resource(res);
		res = NULL;
		break;
	}
	write_unlock(&resource_lock);
	return res;
}
EXPORT_SYMBOL(__request_region);

/**
 * __release_region - release a previously reserved resource region
 * @parent: parent resource descriptor
 * @start: resource start address
 * @n: resource region size
 *
 * The described resource region must match a currently busy region.
 */
void __release_region(struct resource *parent, resource_size_t start,
			resource_size_t n)
{
	struct resource **p;
	resource_size_t end;

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
			if (res->flags & IORESOURCE_MUXED)
				wake_up(&muxed_resource_wait);
			free_resource(res);
			return;
		}
		p = &res->sibling;
	}

	write_unlock(&resource_lock);

	printk(KERN_WARNING "Trying to free nonexistent resource "
		"<%016llx-%016llx>\n", (unsigned long long)start,
		(unsigned long long)end);
}
EXPORT_SYMBOL(__release_region);

#ifdef CONFIG_MEMORY_HOTREMOVE
/**
 * release_mem_region_adjustable - release a previously reserved memory region
 * @parent: parent resource descriptor
 * @start: resource start address
 * @size: resource region size
 *
 * This interface is intended for memory hot-delete.  The requested region
 * is released from a currently busy memory resource.  The requested region
 * must either match exactly or fit into a single busy resource entry.  In
 * the latter case, the remaining resource is adjusted accordingly.
 * Existing children of the busy memory resource must be immutable in the
 * request.
 *
 * Note:
 * - Additional release conditions, such as overlapping region, can be
 *   supported after they are confirmed as valid cases.
 * - When a busy memory resource gets split into two entries, the code
 *   assumes that all children remain in the lower address entry for
 *   simplicity.  Enhance this logic when necessary.
 */
int release_mem_region_adjustable(struct resource *parent,
			resource_size_t start, resource_size_t size)
{
	struct resource **p;
	struct resource *res;
	struct resource *new_res;
	resource_size_t end;
	int ret = -EINVAL;

	end = start + size - 1;
	if ((start < parent->start) || (end > parent->end))
		return ret;

	/* The alloc_resource() result gets checked later */
	new_res = alloc_resource(GFP_KERNEL);

	p = &parent->child;
	write_lock(&resource_lock);

	while ((res = *p)) {
		if (res->start >= end)
			break;

		/* look for the next resource if it does not fit into */
		if (res->start > start || res->end < end) {
			p = &res->sibling;
			continue;
		}

		if (!(res->flags & IORESOURCE_MEM))
			break;

		if (!(res->flags & IORESOURCE_BUSY)) {
			p = &res->child;
			continue;
		}

		/* found the target resource; let's adjust accordingly */
		if (res->start == start && res->end == end) {
			/* free the whole entry */
			*p = res->sibling;
			free_resource(res);
			ret = 0;
		} else if (res->start == start && res->end != end) {
			/* adjust the start */
			ret = __adjust_resource(res, end + 1,
						res->end - end);
		} else if (res->start != start && res->end == end) {
			/* adjust the end */
			ret = __adjust_resource(res, res->start,
						start - res->start);
		} else {
			/* split into two entries */
			if (!new_res) {
				ret = -ENOMEM;
				break;
			}
			new_res->name = res->name;
			new_res->start = end + 1;
			new_res->end = res->end;
			new_res->flags = res->flags;
			new_res->desc = res->desc;
			new_res->parent = res->parent;
			new_res->sibling = res->sibling;
			new_res->child = NULL;

			ret = __adjust_resource(res, res->start,
						start - res->start);
			if (ret)
				break;
			res->sibling = new_res;
			new_res = NULL;
		}

		break;
	}

	write_unlock(&resource_lock);
	free_resource(new_res);
	return ret;
}
#endif	/* CONFIG_MEMORY_HOTREMOVE */

/*
 * Managed region resource
 */
static void devm_resource_release(struct device *dev, void *ptr)
{
	struct resource **r = ptr;

	release_resource(*r);
}

/**
 * devm_request_resource() - request and reserve an I/O or memory resource
 * @dev: device for which to request the resource
 * @root: root of the resource tree from which to request the resource
 * @new: descriptor of the resource to request
 *
 * This is a device-managed version of request_resource(). There is usually
 * no need to release resources requested by this function explicitly since
 * that will be taken care of when the device is unbound from its driver.
 * If for some reason the resource needs to be released explicitly, because
 * of ordering issues for example, drivers must call devm_release_resource()
 * rather than the regular release_resource().
 *
 * When a conflict is detected between any existing resources and the newly
 * requested resource, an error message will be printed.
 *
 * Returns 0 on success or a negative error code on failure.
 */
int devm_request_resource(struct device *dev, struct resource *root,
			  struct resource *new)
{
	struct resource *conflict, **ptr;

	ptr = devres_alloc(devm_resource_release, sizeof(*ptr), GFP_KERNEL);
	if (!ptr)
		return -ENOMEM;

	*ptr = new;

	conflict = request_resource_conflict(root, new);
	if (conflict) {
		dev_err(dev, "resource collision: %pR conflicts with %s %pR\n",
			new, conflict->name, conflict);
		devres_free(ptr);
		return -EBUSY;
	}

	devres_add(dev, ptr);
	return 0;
}
EXPORT_SYMBOL(devm_request_resource);

static int devm_resource_match(struct device *dev, void *res, void *data)
{
	struct resource **ptr = res;

	return *ptr == data;
}

/**
 * devm_release_resource() - release a previously requested resource
 * @dev: device for which to release the resource
 * @new: descriptor of the resource to release
 *
 * Releases a resource previously requested using devm_request_resource().
 */
void devm_release_resource(struct device *dev, struct resource *new)
{
	WARN_ON(devres_release(dev, devm_resource_release, devm_resource_match,
			       new));
}
EXPORT_SYMBOL(devm_release_resource);

struct region_devres {
	struct resource *parent;
	resource_size_t start;
	resource_size_t n;
};

static void devm_region_release(struct device *dev, void *res)
{
	struct region_devres *this = res;

	__release_region(this->parent, this->start, this->n);
}

static int devm_region_match(struct device *dev, void *res, void *match_data)
{
	struct region_devres *this = res, *match = match_data;

	return this->parent == match->parent &&
		this->start == match->start && this->n == match->n;
}

struct resource * __devm_request_region(struct device *dev,
				struct resource *parent, resource_size_t start,
				resource_size_t n, const char *name)
{
	struct region_devres *dr = NULL;
	struct resource *res;

	dr = devres_alloc(devm_region_release, sizeof(struct region_devres),
			  GFP_KERNEL);
	if (!dr)
		return NULL;

	dr->parent = parent;
	dr->start = start;
	dr->n = n;

	res = __request_region(parent, start, n, name, 0);
	if (res)
		devres_add(dev, dr);
	else
		devres_free(dr);

	return res;
}
EXPORT_SYMBOL(__devm_request_region);

void __devm_release_region(struct device *dev, struct resource *parent,
			   resource_size_t start, resource_size_t n)
{
	struct region_devres match_data = { parent, start, n };

	__release_region(parent, start, n);
	WARN_ON(devres_destroy(dev, devm_region_release, devm_region_match,
			       &match_data));
}
EXPORT_SYMBOL(__devm_release_region);

/*
 * Called from init/main.c to reserve IO ports.
 */
#define MAXRESERVE 4
static int __init reserve_setup(char *str)
{
	static int reserved;
	static struct resource reserve[MAXRESERVE];

	for (;;) {
		unsigned int io_start, io_num;
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
			res->desc = IORES_DESC_NONE;
			res->child = NULL;
			if (request_resource(res->start >= 0x10000 ? &iomem_resource : &ioport_resource, res) == 0)
				reserved = x+1;
		}
	}
	return 1;
}

__setup("reserve=", reserve_setup);

/*
 * Check if the requested addr and size spans more than any slot in the
 * iomem resource tree.
 */
int iomem_map_sanity_check(resource_size_t addr, unsigned long size)
{
	struct resource *p = &iomem_resource;
	int err = 0;
	loff_t l;

	read_lock(&resource_lock);
	for (p = p->child; p ; p = r_next(NULL, p, &l)) {
		/*
		 * We can probably skip the resources without
		 * IORESOURCE_IO attribute?
		 */
		if (p->start >= addr + size)
			continue;
		if (p->end < addr)
			continue;
		if (PFN_DOWN(p->start) <= PFN_DOWN(addr) &&
		    PFN_DOWN(p->end) >= PFN_DOWN(addr + size - 1))
			continue;
		/*
		 * if a resource is "BUSY", it's not a hardware resource
		 * but a driver mapping of such a resource; we don't want
		 * to warn for those; some drivers legitimately map only
		 * partial hardware resources. (example: vesafb)
		 */
		if (p->flags & IORESOURCE_BUSY)
			continue;

		printk(KERN_WARNING "resource sanity check: requesting [mem %#010llx-%#010llx], which spans more than %s %pR\n",
		       (unsigned long long)addr,
		       (unsigned long long)(addr + size - 1),
		       p->name, p);
		err = -1;
		break;
	}
	read_unlock(&resource_lock);

	return err;
}

#ifdef CONFIG_STRICT_DEVMEM
static int strict_iomem_checks = 1;
#else
static int strict_iomem_checks;
#endif

/*
 * check if an address is reserved in the iomem resource tree
 * returns 1 if reserved, 0 if not reserved.
 */
int iomem_is_exclusive(u64 addr)
{
	struct resource *p = &iomem_resource;
	int err = 0;
	loff_t l;
	int size = PAGE_SIZE;

	if (!strict_iomem_checks)
		return 0;

	addr = addr & PAGE_MASK;

	read_lock(&resource_lock);
	for (p = p->child; p ; p = r_next(NULL, p, &l)) {
		/*
		 * We can probably skip the resources without
		 * IORESOURCE_IO attribute?
		 */
		if (p->start >= addr + size)
			break;
		if (p->end < addr)
			continue;
		/*
		 * A resource is exclusive if IORESOURCE_EXCLUSIVE is set
		 * or CONFIG_IO_STRICT_DEVMEM is enabled and the
		 * resource is busy.
		 */
		if ((p->flags & IORESOURCE_BUSY) == 0)
			continue;
		if (IS_ENABLED(CONFIG_IO_STRICT_DEVMEM)
				|| p->flags & IORESOURCE_EXCLUSIVE) {
			err = 1;
			break;
		}
	}
	read_unlock(&resource_lock);

	return err;
}

struct resource_entry *resource_list_create_entry(struct resource *res,
						  size_t extra_size)
{
	struct resource_entry *entry;

	entry = kzalloc(sizeof(*entry) + extra_size, GFP_KERNEL);
	if (entry) {
		INIT_LIST_HEAD(&entry->node);
		entry->res = res ? res : &entry->__res;
	}

	return entry;
}
EXPORT_SYMBOL(resource_list_create_entry);

void resource_list_free(struct list_head *head)
{
	struct resource_entry *entry, *tmp;

	list_for_each_entry_safe(entry, tmp, head, node)
		resource_list_destroy_entry(entry);
}
EXPORT_SYMBOL(resource_list_free);

static int __init strict_iomem(char *str)
{
	if (strstr(str, "relaxed"))
		strict_iomem_checks = 0;
	if (strstr(str, "strict"))
		strict_iomem_checks = 1;
	return 1;
}

__setup("iomem=", strict_iomem);
