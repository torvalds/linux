// SPDX-License-Identifier: GPL-2.0-only
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
#include <linux/pseudo_fs.h>
#include <linux/sched.h>
#include <linux/seq_file.h>
#include <linux/device.h>
#include <linux/pfn.h>
#include <linux/mm.h>
#include <linux/mount.h>
#include <linux/resource_ext.h>
#include <uapi/linux/magic.h>
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

static struct resource *next_resource(struct resource *p, bool skip_children)
{
	if (!skip_children && p->child)
		return p->child;
	while (!p->sibling && p->parent)
		p = p->parent;
	return p->sibling;
}

#define for_each_resource(_root, _p, _skip_children) \
	for ((_p) = (_root)->child; (_p); (_p) = next_resource(_p, _skip_children))

#ifdef CONFIG_PROC_FS

enum { MAX_IORES_LEVEL = 5 };

static void *r_start(struct seq_file *m, loff_t *pos)
	__acquires(resource_lock)
{
	struct resource *root = pde_data(file_inode(m->file));
	struct resource *p;
	loff_t l = *pos;

	read_lock(&resource_lock);
	for_each_resource(root, p, false) {
		if (l-- == 0)
			break;
	}

	return p;
}

static void *r_next(struct seq_file *m, void *v, loff_t *pos)
{
	struct resource *p = v;

	(*pos)++;

	return (void *)next_resource(p, false);
}

static void r_stop(struct seq_file *m, void *v)
	__releases(resource_lock)
{
	read_unlock(&resource_lock);
}

static int r_show(struct seq_file *m, void *v)
{
	struct resource *root = pde_data(file_inode(m->file));
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

static int __init ioresources_init(void)
{
	proc_create_seq_data("ioports", 0, NULL, &resource_op,
			&ioport_resource);
	proc_create_seq_data("iomem", 0, NULL, &resource_op, &iomem_resource);
	return 0;
}
__initcall(ioresources_init);

#endif /* CONFIG_PROC_FS */

static void free_resource(struct resource *res)
{
	/**
	 * If the resource was allocated using memblock early during boot
	 * we'll leak it here: we can only return full pages back to the
	 * buddy and trying to be smart and reusing them eventually in
	 * alloc_resource() overcomplicates resource handling.
	 */
	if (res && PageSlab(virt_to_head_page(res)))
		kfree(res);
}

static struct resource *alloc_resource(gfp_t flags)
{
	return kzalloc(sizeof(struct resource), flags);
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

/**
 * find_next_iomem_res - Finds the lowest iomem resource that covers part of
 *			 [@start..@end].
 *
 * If a resource is found, returns 0 and @*res is overwritten with the part
 * of the resource that's within [@start..@end]; if none is found, returns
 * -ENODEV.  Returns -EINVAL for invalid parameters.
 *
 * @start:	start address of the resource searched for
 * @end:	end address of same resource
 * @flags:	flags which the resource must have
 * @desc:	descriptor the resource must have
 * @res:	return ptr, if resource found
 *
 * The caller must specify @start, @end, @flags, and @desc
 * (which may be IORES_DESC_NONE).
 */
static int find_next_iomem_res(resource_size_t start, resource_size_t end,
			       unsigned long flags, unsigned long desc,
			       struct resource *res)
{
	struct resource *p;

	if (!res)
		return -EINVAL;

	if (start >= end)
		return -EINVAL;

	read_lock(&resource_lock);

	for_each_resource(&iomem_resource, p, false) {
		/* If we passed the resource we are looking for, stop */
		if (p->start > end) {
			p = NULL;
			break;
		}

		/* Skip until we find a range that matches what we look for */
		if (p->end < start)
			continue;

		if ((p->flags & flags) != flags)
			continue;
		if ((desc != IORES_DESC_NONE) && (desc != p->desc))
			continue;

		/* Found a match, break */
		break;
	}

	if (p) {
		/* copy data */
		*res = (struct resource) {
			.start = max(start, p->start),
			.end = min(end, p->end),
			.flags = p->flags,
			.desc = p->desc,
			.parent = p->parent,
		};
	}

	read_unlock(&resource_lock);
	return p ? 0 : -ENODEV;
}

static int __walk_iomem_res_desc(resource_size_t start, resource_size_t end,
				 unsigned long flags, unsigned long desc,
				 void *arg,
				 int (*func)(struct resource *, void *))
{
	struct resource res;
	int ret = -EINVAL;

	while (start < end &&
	       !find_next_iomem_res(start, end, flags, desc, &res)) {
		ret = (*func)(&res, arg);
		if (ret)
			break;

		start = res.end + 1;
	}

	return ret;
}

/**
 * walk_iomem_res_desc - Walks through iomem resources and calls func()
 *			 with matching resource ranges.
 * *
 * @desc: I/O resource descriptor. Use IORES_DESC_NONE to skip @desc check.
 * @flags: I/O resource flags
 * @start: start addr
 * @end: end addr
 * @arg: function argument for the callback @func
 * @func: callback function that is called for each qualifying resource area
 *
 * All the memory ranges which overlap start,end and also match flags and
 * desc are valid candidates.
 *
 * NOTE: For a new descriptor search, define a new IORES_DESC in
 * <linux/ioport.h> and set it in 'desc' of a target resource entry.
 */
int walk_iomem_res_desc(unsigned long desc, unsigned long flags, u64 start,
		u64 end, void *arg, int (*func)(struct resource *, void *))
{
	return __walk_iomem_res_desc(start, end, flags, desc, arg, func);
}
EXPORT_SYMBOL_GPL(walk_iomem_res_desc);

/*
 * This function calls the @func callback against all memory ranges of type
 * System RAM which are marked as IORESOURCE_SYSTEM_RAM and IORESOUCE_BUSY.
 * Now, this function is only for System RAM, it deals with full ranges and
 * not PFNs. If resources are not PFN-aligned, dealing with PFNs can truncate
 * ranges.
 */
int walk_system_ram_res(u64 start, u64 end, void *arg,
			int (*func)(struct resource *, void *))
{
	unsigned long flags = IORESOURCE_SYSTEM_RAM | IORESOURCE_BUSY;

	return __walk_iomem_res_desc(start, end, flags, IORES_DESC_NONE, arg,
				     func);
}

/*
 * This function calls the @func callback against all memory ranges, which
 * are ranges marked as IORESOURCE_MEM and IORESOUCE_BUSY.
 */
int walk_mem_res(u64 start, u64 end, void *arg,
		 int (*func)(struct resource *, void *))
{
	unsigned long flags = IORESOURCE_MEM | IORESOURCE_BUSY;

	return __walk_iomem_res_desc(start, end, flags, IORES_DESC_NONE, arg,
				     func);
}

/*
 * This function calls the @func callback against all memory ranges of type
 * System RAM which are marked as IORESOURCE_SYSTEM_RAM and IORESOUCE_BUSY.
 * It is to be used only for System RAM.
 */
int walk_system_ram_range(unsigned long start_pfn, unsigned long nr_pages,
			  void *arg, int (*func)(unsigned long, unsigned long, void *))
{
	resource_size_t start, end;
	unsigned long flags;
	struct resource res;
	unsigned long pfn, end_pfn;
	int ret = -EINVAL;

	start = (u64) start_pfn << PAGE_SHIFT;
	end = ((u64)(start_pfn + nr_pages) << PAGE_SHIFT) - 1;
	flags = IORESOURCE_SYSTEM_RAM | IORESOURCE_BUSY;
	while (start < end &&
	       !find_next_iomem_res(start, end, flags, IORES_DESC_NONE, &res)) {
		pfn = PFN_UP(res.start);
		end_pfn = PFN_DOWN(res.end + 1);
		if (end_pfn > pfn)
			ret = (*func)(pfn, end_pfn - pfn, arg);
		if (ret)
			break;
		start = res.end + 1;
	}
	return ret;
}

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

static int __region_intersects(struct resource *parent, resource_size_t start,
			       size_t size, unsigned long flags,
			       unsigned long desc)
{
	struct resource res;
	int type = 0; int other = 0;
	struct resource *p;

	res.start = start;
	res.end = start + size - 1;

	for (p = parent->child; p ; p = p->sibling) {
		bool is_type = (((p->flags & flags) == flags) &&
				((desc == IORES_DESC_NONE) ||
				 (desc == p->desc)));

		if (resource_overlaps(p, &res))
			is_type ? type++ : other++;
	}

	if (type == 0)
		return REGION_DISJOINT;

	if (other == 0)
		return REGION_INTERSECTS;

	return REGION_MIXED;
}

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
	int ret;

	read_lock(&resource_lock);
	ret = __region_intersects(&iomem_resource, start, size, flags, desc);
	read_unlock(&resource_lock);

	return ret;
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
			       struct resource_constraint *constraint)
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

		pr_info("Expanded resource %s due to conflict with %s\n", new->name, conflict->name);
	}
	write_unlock(&resource_lock);
}
/*
 * Not for general consumption, only early boot memory map parsing, PCI
 * resource discovery, and late discovery of CXL resources are expected
 * to use this interface. The former are built-in and only the latter,
 * CXL, is a module.
 */
EXPORT_SYMBOL_NS_GPL(insert_resource_expand_to_fit, CXL);

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

static void __init
__reserve_region_with_split(struct resource *root, resource_size_t start,
			    resource_size_t end, const char *name)
{
	struct resource *parent = root;
	struct resource *conflict;
	struct resource *res = alloc_resource(GFP_ATOMIC);
	struct resource *next_res = NULL;
	int type = resource_type(root);

	if (!res)
		return;

	res->name = name;
	res->start = start;
	res->end = end;
	res->flags = type | IORESOURCE_BUSY;
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
				next_res->flags = type | IORESOURCE_BUSY;
				next_res->desc = IORES_DESC_NONE;
			}
		} else {
			res->start = conflict->end + 1;
		}
	}

}

void __init
reserve_region_with_split(struct resource *root, resource_size_t start,
			  resource_size_t end, const char *name)
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

static struct inode *iomem_inode;

#ifdef CONFIG_IO_STRICT_DEVMEM
static void revoke_iomem(struct resource *res)
{
	/* pairs with smp_store_release() in iomem_init_inode() */
	struct inode *inode = smp_load_acquire(&iomem_inode);

	/*
	 * Check that the initialization has completed. Losing the race
	 * is ok because it means drivers are claiming resources before
	 * the fs_initcall level of init and prevent iomem_get_mapping users
	 * from establishing mappings.
	 */
	if (!inode)
		return;

	/*
	 * The expectation is that the driver has successfully marked
	 * the resource busy by this point, so devmem_is_allowed()
	 * should start returning false, however for performance this
	 * does not iterate the entire resource range.
	 */
	if (devmem_is_allowed(PHYS_PFN(res->start)) &&
	    devmem_is_allowed(PHYS_PFN(res->end))) {
		/*
		 * *cringe* iomem=relaxed says "go ahead, what's the
		 * worst that can happen?"
		 */
		return;
	}

	unmap_mapping_range(inode->i_mapping, res->start, resource_size(res), 1);
}
#else
static void revoke_iomem(struct resource *res) {}
#endif

struct address_space *iomem_get_mapping(void)
{
	/*
	 * This function is only called from file open paths, hence guaranteed
	 * that fs_initcalls have completed and no need to check for NULL. But
	 * since revoke_iomem can be called before the initcall we still need
	 * the barrier to appease checkers.
	 */
	return smp_load_acquire(&iomem_inode)->i_mapping;
}

static int __request_region_locked(struct resource *res, struct resource *parent,
				   resource_size_t start, resource_size_t n,
				   const char *name, int flags)
{
	DECLARE_WAITQUEUE(wait, current);

	res->name = name;
	res->start = start;
	res->end = start + n - 1;

	for (;;) {
		struct resource *conflict;

		res->flags = resource_type(parent) | resource_ext_type(parent);
		res->flags |= IORESOURCE_BUSY | flags;
		res->desc = parent->desc;

		conflict = __request_resource(parent, res);
		if (!conflict)
			break;
		/*
		 * mm/hmm.c reserves physical addresses which then
		 * become unavailable to other users.  Conflicts are
		 * not expected.  Warn to aid debugging if encountered.
		 */
		if (conflict->desc == IORES_DESC_DEVICE_PRIVATE_MEMORY) {
			pr_warn("Unaddressable device %s %pR conflicts with %pR",
				conflict->name, conflict, res);
		}
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
		return -EBUSY;
	}

	return 0;
}

/**
 * __request_region - create a new busy resource region
 * @parent: parent resource descriptor
 * @start: resource start address
 * @n: resource region size
 * @name: reserving caller's ID string
 * @flags: IO resource flags
 */
struct resource *__request_region(struct resource *parent,
				  resource_size_t start, resource_size_t n,
				  const char *name, int flags)
{
	struct resource *res = alloc_resource(GFP_KERNEL);
	int ret;

	if (!res)
		return NULL;

	write_lock(&resource_lock);
	ret = __request_region_locked(res, parent, start, n, name, flags);
	write_unlock(&resource_lock);

	if (ret) {
		free_resource(res);
		return NULL;
	}

	if (parent == &iomem_resource)
		revoke_iomem(res);

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

	pr_warn("Trying to free nonexistent resource <%pa-%pa>\n", &start, &end);
}
EXPORT_SYMBOL(__release_region);

#ifdef CONFIG_MEMORY_HOTREMOVE
/**
 * release_mem_region_adjustable - release a previously reserved memory region
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
void release_mem_region_adjustable(resource_size_t start, resource_size_t size)
{
	struct resource *parent = &iomem_resource;
	struct resource *new_res = NULL;
	bool alloc_nofail = false;
	struct resource **p;
	struct resource *res;
	resource_size_t end;

	end = start + size - 1;
	if (WARN_ON_ONCE((start < parent->start) || (end > parent->end)))
		return;

	/*
	 * We free up quite a lot of memory on memory hotunplug (esp., memap),
	 * just before releasing the region. This is highly unlikely to
	 * fail - let's play save and make it never fail as the caller cannot
	 * perform any error handling (e.g., trying to re-add memory will fail
	 * similarly).
	 */
retry:
	new_res = alloc_resource(GFP_KERNEL | (alloc_nofail ? __GFP_NOFAIL : 0));

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
		} else if (res->start == start && res->end != end) {
			/* adjust the start */
			WARN_ON_ONCE(__adjust_resource(res, end + 1,
						       res->end - end));
		} else if (res->start != start && res->end == end) {
			/* adjust the end */
			WARN_ON_ONCE(__adjust_resource(res, res->start,
						       start - res->start));
		} else {
			/* split into two entries - we need a new resource */
			if (!new_res) {
				new_res = alloc_resource(GFP_ATOMIC);
				if (!new_res) {
					alloc_nofail = true;
					write_unlock(&resource_lock);
					goto retry;
				}
			}
			new_res->name = res->name;
			new_res->start = end + 1;
			new_res->end = res->end;
			new_res->flags = res->flags;
			new_res->desc = res->desc;
			new_res->parent = res->parent;
			new_res->sibling = res->sibling;
			new_res->child = NULL;

			if (WARN_ON_ONCE(__adjust_resource(res, res->start,
							   start - res->start)))
				break;
			res->sibling = new_res;
			new_res = NULL;
		}

		break;
	}

	write_unlock(&resource_lock);
	free_resource(new_res);
}
#endif	/* CONFIG_MEMORY_HOTREMOVE */

#ifdef CONFIG_MEMORY_HOTPLUG
static bool system_ram_resources_mergeable(struct resource *r1,
					   struct resource *r2)
{
	/* We assume either r1 or r2 is IORESOURCE_SYSRAM_MERGEABLE. */
	return r1->flags == r2->flags && r1->end + 1 == r2->start &&
	       r1->name == r2->name && r1->desc == r2->desc &&
	       !r1->child && !r2->child;
}

/**
 * merge_system_ram_resource - mark the System RAM resource mergeable and try to
 *	merge it with adjacent, mergeable resources
 * @res: resource descriptor
 *
 * This interface is intended for memory hotplug, whereby lots of contiguous
 * system ram resources are added (e.g., via add_memory*()) by a driver, and
 * the actual resource boundaries are not of interest (e.g., it might be
 * relevant for DIMMs). Only resources that are marked mergeable, that have the
 * same parent, and that don't have any children are considered. All mergeable
 * resources must be immutable during the request.
 *
 * Note:
 * - The caller has to make sure that no pointers to resources that are
 *   marked mergeable are used anymore after this call - the resource might
 *   be freed and the pointer might be stale!
 * - release_mem_region_adjustable() will split on demand on memory hotunplug
 */
void merge_system_ram_resource(struct resource *res)
{
	const unsigned long flags = IORESOURCE_SYSTEM_RAM | IORESOURCE_BUSY;
	struct resource *cur;

	if (WARN_ON_ONCE((res->flags & flags) != flags))
		return;

	write_lock(&resource_lock);
	res->flags |= IORESOURCE_SYSRAM_MERGEABLE;

	/* Try to merge with next item in the list. */
	cur = res->sibling;
	if (cur && system_ram_resources_mergeable(res, cur)) {
		res->end = cur->end;
		res->sibling = cur->sibling;
		free_resource(cur);
	}

	/* Try to merge with previous item in the list. */
	cur = res->parent->child;
	while (cur && cur->sibling != res)
		cur = cur->sibling;
	if (cur && system_ram_resources_mergeable(cur, res)) {
		cur->end = res->end;
		cur->sibling = res->sibling;
		free_resource(res);
	}
	write_unlock(&resource_lock);
}
#endif	/* CONFIG_MEMORY_HOTPLUG */

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

struct resource *
__devm_request_region(struct device *dev, struct resource *parent,
		      resource_size_t start, resource_size_t n, const char *name)
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
 * Reserve I/O ports or memory based on "reserve=" kernel parameter.
 */
#define MAXRESERVE 4
static int __init reserve_setup(char *str)
{
	static int reserved;
	static struct resource reserve[MAXRESERVE];

	for (;;) {
		unsigned int io_start, io_num;
		int x = reserved;
		struct resource *parent;

		if (get_option(&str, &io_start) != 2)
			break;
		if (get_option(&str, &io_num) == 0)
			break;
		if (x < MAXRESERVE) {
			struct resource *res = reserve + x;

			/*
			 * If the region starts below 0x10000, we assume it's
			 * I/O port space; otherwise assume it's memory.
			 */
			if (io_start < 0x10000) {
				res->flags = IORESOURCE_IO;
				parent = &ioport_resource;
			} else {
				res->flags = IORESOURCE_MEM;
				parent = &iomem_resource;
			}
			res->name = "reserved";
			res->start = io_start;
			res->end = io_start + io_num - 1;
			res->flags |= IORESOURCE_BUSY;
			res->desc = IORES_DESC_NONE;
			res->child = NULL;
			if (request_resource(parent, res) == 0)
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
	resource_size_t end = addr + size - 1;
	struct resource *p;
	int err = 0;

	read_lock(&resource_lock);
	for_each_resource(&iomem_resource, p, false) {
		/*
		 * We can probably skip the resources without
		 * IORESOURCE_IO attribute?
		 */
		if (p->start > end)
			continue;
		if (p->end < addr)
			continue;
		if (PFN_DOWN(p->start) <= PFN_DOWN(addr) &&
		    PFN_DOWN(p->end) >= PFN_DOWN(end))
			continue;
		/*
		 * if a resource is "BUSY", it's not a hardware resource
		 * but a driver mapping of such a resource; we don't want
		 * to warn for those; some drivers legitimately map only
		 * partial hardware resources. (example: vesafb)
		 */
		if (p->flags & IORESOURCE_BUSY)
			continue;

		pr_warn("resource sanity check: requesting [mem %pa-%pa], which spans more than %s %pR\n",
			&addr, &end, p->name, p);
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
 * Check if an address is exclusive to the kernel and must not be mapped to
 * user space, for example, via /dev/mem.
 *
 * Returns true if exclusive to the kernel, otherwise returns false.
 */
bool resource_is_exclusive(struct resource *root, u64 addr, resource_size_t size)
{
	const unsigned int exclusive_system_ram = IORESOURCE_SYSTEM_RAM |
						  IORESOURCE_EXCLUSIVE;
	bool skip_children = false, err = false;
	struct resource *p;

	read_lock(&resource_lock);
	for_each_resource(root, p, skip_children) {
		if (p->start >= addr + size)
			break;
		if (p->end < addr) {
			skip_children = true;
			continue;
		}
		skip_children = false;

		/*
		 * IORESOURCE_SYSTEM_RAM resources are exclusive if
		 * IORESOURCE_EXCLUSIVE is set, even if they
		 * are not busy and even if "iomem=relaxed" is set. The
		 * responsible driver dynamically adds/removes system RAM within
		 * such an area and uncontrolled access is dangerous.
		 */
		if ((p->flags & exclusive_system_ram) == exclusive_system_ram) {
			err = true;
			break;
		}

		/*
		 * A resource is exclusive if IORESOURCE_EXCLUSIVE is set
		 * or CONFIG_IO_STRICT_DEVMEM is enabled and the
		 * resource is busy.
		 */
		if (!strict_iomem_checks || !(p->flags & IORESOURCE_BUSY))
			continue;
		if (IS_ENABLED(CONFIG_IO_STRICT_DEVMEM)
				|| p->flags & IORESOURCE_EXCLUSIVE) {
			err = true;
			break;
		}
	}
	read_unlock(&resource_lock);

	return err;
}

bool iomem_is_exclusive(u64 addr)
{
	return resource_is_exclusive(&iomem_resource, addr & PAGE_MASK,
				     PAGE_SIZE);
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

#ifdef CONFIG_GET_FREE_REGION
#define GFR_DESCENDING		(1UL << 0)
#define GFR_REQUEST_REGION	(1UL << 1)
#define GFR_DEFAULT_ALIGN (1UL << PA_SECTION_SHIFT)

static resource_size_t gfr_start(struct resource *base, resource_size_t size,
				 resource_size_t align, unsigned long flags)
{
	if (flags & GFR_DESCENDING) {
		resource_size_t end;

		end = min_t(resource_size_t, base->end,
			    (1ULL << MAX_PHYSMEM_BITS) - 1);
		return end - size + 1;
	}

	return ALIGN(base->start, align);
}

static bool gfr_continue(struct resource *base, resource_size_t addr,
			 resource_size_t size, unsigned long flags)
{
	if (flags & GFR_DESCENDING)
		return addr > size && addr >= base->start;
	/*
	 * In the ascend case be careful that the last increment by
	 * @size did not wrap 0.
	 */
	return addr > addr - size &&
	       addr <= min_t(resource_size_t, base->end,
			     (1ULL << MAX_PHYSMEM_BITS) - 1);
}

static resource_size_t gfr_next(resource_size_t addr, resource_size_t size,
				unsigned long flags)
{
	if (flags & GFR_DESCENDING)
		return addr - size;
	return addr + size;
}

static void remove_free_mem_region(void *_res)
{
	struct resource *res = _res;

	if (res->parent)
		remove_resource(res);
	free_resource(res);
}

static struct resource *
get_free_mem_region(struct device *dev, struct resource *base,
		    resource_size_t size, const unsigned long align,
		    const char *name, const unsigned long desc,
		    const unsigned long flags)
{
	resource_size_t addr;
	struct resource *res;
	struct region_devres *dr = NULL;

	size = ALIGN(size, align);

	res = alloc_resource(GFP_KERNEL);
	if (!res)
		return ERR_PTR(-ENOMEM);

	if (dev && (flags & GFR_REQUEST_REGION)) {
		dr = devres_alloc(devm_region_release,
				sizeof(struct region_devres), GFP_KERNEL);
		if (!dr) {
			free_resource(res);
			return ERR_PTR(-ENOMEM);
		}
	} else if (dev) {
		if (devm_add_action_or_reset(dev, remove_free_mem_region, res))
			return ERR_PTR(-ENOMEM);
	}

	write_lock(&resource_lock);
	for (addr = gfr_start(base, size, align, flags);
	     gfr_continue(base, addr, align, flags);
	     addr = gfr_next(addr, align, flags)) {
		if (__region_intersects(base, addr, size, 0, IORES_DESC_NONE) !=
		    REGION_DISJOINT)
			continue;

		if (flags & GFR_REQUEST_REGION) {
			if (__request_region_locked(res, &iomem_resource, addr,
						    size, name, 0))
				break;

			if (dev) {
				dr->parent = &iomem_resource;
				dr->start = addr;
				dr->n = size;
				devres_add(dev, dr);
			}

			res->desc = desc;
			write_unlock(&resource_lock);


			/*
			 * A driver is claiming this region so revoke any
			 * mappings.
			 */
			revoke_iomem(res);
		} else {
			res->start = addr;
			res->end = addr + size - 1;
			res->name = name;
			res->desc = desc;
			res->flags = IORESOURCE_MEM;

			/*
			 * Only succeed if the resource hosts an exclusive
			 * range after the insert
			 */
			if (__insert_resource(base, res) || res->child)
				break;

			write_unlock(&resource_lock);
		}

		return res;
	}
	write_unlock(&resource_lock);

	if (flags & GFR_REQUEST_REGION) {
		free_resource(res);
		devres_free(dr);
	} else if (dev)
		devm_release_action(dev, remove_free_mem_region, res);

	return ERR_PTR(-ERANGE);
}

/**
 * devm_request_free_mem_region - find free region for device private memory
 *
 * @dev: device struct to bind the resource to
 * @size: size in bytes of the device memory to add
 * @base: resource tree to look in
 *
 * This function tries to find an empty range of physical address big enough to
 * contain the new resource, so that it can later be hotplugged as ZONE_DEVICE
 * memory, which in turn allocates struct pages.
 */
struct resource *devm_request_free_mem_region(struct device *dev,
		struct resource *base, unsigned long size)
{
	unsigned long flags = GFR_DESCENDING | GFR_REQUEST_REGION;

	return get_free_mem_region(dev, base, size, GFR_DEFAULT_ALIGN,
				   dev_name(dev),
				   IORES_DESC_DEVICE_PRIVATE_MEMORY, flags);
}
EXPORT_SYMBOL_GPL(devm_request_free_mem_region);

struct resource *request_free_mem_region(struct resource *base,
		unsigned long size, const char *name)
{
	unsigned long flags = GFR_DESCENDING | GFR_REQUEST_REGION;

	return get_free_mem_region(NULL, base, size, GFR_DEFAULT_ALIGN, name,
				   IORES_DESC_DEVICE_PRIVATE_MEMORY, flags);
}
EXPORT_SYMBOL_GPL(request_free_mem_region);

/**
 * alloc_free_mem_region - find a free region relative to @base
 * @base: resource that will parent the new resource
 * @size: size in bytes of memory to allocate from @base
 * @align: alignment requirements for the allocation
 * @name: resource name
 *
 * Buses like CXL, that can dynamically instantiate new memory regions,
 * need a method to allocate physical address space for those regions.
 * Allocate and insert a new resource to cover a free, unclaimed by a
 * descendant of @base, range in the span of @base.
 */
struct resource *alloc_free_mem_region(struct resource *base,
				       unsigned long size, unsigned long align,
				       const char *name)
{
	/* Default of ascending direction and insert resource */
	unsigned long flags = 0;

	return get_free_mem_region(NULL, base, size, align, name,
				   IORES_DESC_NONE, flags);
}
EXPORT_SYMBOL_NS_GPL(alloc_free_mem_region, CXL);
#endif /* CONFIG_GET_FREE_REGION */

static int __init strict_iomem(char *str)
{
	if (strstr(str, "relaxed"))
		strict_iomem_checks = 0;
	if (strstr(str, "strict"))
		strict_iomem_checks = 1;
	return 1;
}

static int iomem_fs_init_fs_context(struct fs_context *fc)
{
	return init_pseudo(fc, DEVMEM_MAGIC) ? 0 : -ENOMEM;
}

static struct file_system_type iomem_fs_type = {
	.name		= "iomem",
	.owner		= THIS_MODULE,
	.init_fs_context = iomem_fs_init_fs_context,
	.kill_sb	= kill_anon_super,
};

static int __init iomem_init_inode(void)
{
	static struct vfsmount *iomem_vfs_mount;
	static int iomem_fs_cnt;
	struct inode *inode;
	int rc;

	rc = simple_pin_fs(&iomem_fs_type, &iomem_vfs_mount, &iomem_fs_cnt);
	if (rc < 0) {
		pr_err("Cannot mount iomem pseudo filesystem: %d\n", rc);
		return rc;
	}

	inode = alloc_anon_inode(iomem_vfs_mount->mnt_sb);
	if (IS_ERR(inode)) {
		rc = PTR_ERR(inode);
		pr_err("Cannot allocate inode for iomem: %d\n", rc);
		simple_release_fs(&iomem_vfs_mount, &iomem_fs_cnt);
		return rc;
	}

	/*
	 * Publish iomem revocation inode initialized.
	 * Pairs with smp_load_acquire() in revoke_iomem().
	 */
	smp_store_release(&iomem_inode, inode);

	return 0;
}

fs_initcall(iomem_init_inode);

__setup("iomem=", strict_iomem);
