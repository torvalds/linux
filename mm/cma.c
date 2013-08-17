/*
 * Contiguous Memory Allocator framework
 * Copyright (c) 2010 by Samsung Electronics.
 * Written by Michal Nazarewicz (m.nazarewicz@samsung.com)
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License as
 * published by the Free Software Foundation; either version 2 of the
 * License or (at your optional) any later version of the license.
 */

/*
 * See Documentation/contiguous-memory.txt for details.
 */

#define pr_fmt(fmt) "cma: " fmt

#ifdef CONFIG_CMA_DEBUG
#  define DEBUG
#endif

#ifndef CONFIG_NO_BOOTMEM
#  include <linux/bootmem.h>   /* alloc_bootmem_pages_nopanic() */
#endif
#ifdef CONFIG_HAVE_MEMBLOCK
#  include <linux/memblock.h>  /* memblock*() */
#endif
#include <linux/device.h>      /* struct device, dev_name() */
#include <linux/errno.h>       /* Error numbers */
#include <linux/err.h>         /* IS_ERR, PTR_ERR, etc. */
#include <linux/mm.h>          /* PAGE_ALIGN() */
#include <linux/module.h>      /* EXPORT_SYMBOL_GPL() */
#include <linux/mutex.h>       /* mutex */
#include <linux/slab.h>        /* kmalloc() */
#include <linux/string.h>      /* str*() */

#include <linux/cma.h>
#include <linux/vmalloc.h>

/*
 * Protects cma_regions, cma_allocators, cma_map, cma_map_length,
 * cma_kobj, cma_sysfs_regions and cma_chunks_by_start.
 */
static DEFINE_MUTEX(cma_mutex);



/************************* Map attribute *************************/

static const char *cma_map;
static size_t cma_map_length;

/*
 * map-attr      ::= [ rules [ ';' ] ]
 * rules         ::= rule [ ';' rules ]
 * rule          ::= patterns '=' regions
 * patterns      ::= pattern [ ',' patterns ]
 * regions       ::= REG-NAME [ ',' regions ]
 * pattern       ::= dev-pattern [ '/' TYPE-NAME ] | '/' TYPE-NAME
 *
 * See Documentation/contiguous-memory.txt for details.
 */
static ssize_t cma_map_validate(const char *param)
{
	const char *ch = param;

	if (*ch == '\0' || *ch == '\n')
		return 0;

	for (;;) {
		const char *start = ch;

		while (*ch && *ch != '\n' && *ch != ';' && *ch != '=')
			++ch;

		if (*ch != '=' || start == ch) {
			pr_err("map: expecting \"<patterns>=<regions>\" near %s\n",
			       start);
			return -EINVAL;
		}

		while (*++ch != ';')
			if (*ch == '\0' || *ch == '\n')
				return ch - param;
		if (ch[1] == '\0' || ch[1] == '\n')
			return ch - param;
		++ch;
	}
}

static int __init cma_map_param(char *param)
{
	ssize_t len;

	pr_debug("param: map: %s\n", param);

	len = cma_map_validate(param);
	if (len < 0)
		return len;

	cma_map = param;
	cma_map_length = len;
	return 0;
}

#if defined CONFIG_CMA_CMDLINE

early_param("cma.map", cma_map_param);

#endif



/************************* Early regions *************************/

struct list_head cma_early_regions __initdata =
	LIST_HEAD_INIT(cma_early_regions);

#ifdef CONFIG_CMA_CMDLINE

/*
 * regions-attr ::= [ regions [ ';' ] ]
 * regions      ::= region [ ';' regions ]
 *
 * region       ::= [ '-' ] reg-name
 *                    '=' size
 *                  [ '@' start ]
 *                  [ '/' alignment ]
 *                  [ ':' alloc-name ]
 *
 * See Documentation/contiguous-memory.txt for details.
 *
 * Example:
 * cma=reg1=64M:bf;reg2=32M@0x100000:bf;reg3=64M/1M:bf
 *
 * If allocator is ommited the first available allocater will be used.
 */

#define NUMPARSE(cond_ch, type, cond) ({				\
		unsigned long long v = 0;				\
		if (*param == (cond_ch)) {				\
			const char *const msg = param + 1;		\
			v = memparse(msg, &param);			\
			if (!v || v > ~(type)0 || !(cond)) {		\
				pr_err("param: invalid value near %s\n", msg); \
				ret = -EINVAL;				\
				break;					\
			}						\
		}							\
		v;							\
	})

static int __init cma_param_parse(char *param)
{
	static struct cma_region regions[16];

	size_t left = ARRAY_SIZE(regions);
	struct cma_region *reg = regions;
	int ret = 0;

	pr_debug("param: %s\n", param);

	for (; *param; ++reg) {
		dma_addr_t start, alignment;
		size_t size;

		if (unlikely(!--left)) {
			pr_err("param: too many early regions\n");
			return -ENOSPC;
		}

		/* Parse name */
		reg->name = param;
		param = strchr(param, '=');
		if (!param || param == reg->name) {
			pr_err("param: expected \"<name>=\" near %s\n",
			       reg->name);
			ret = -EINVAL;
			break;
		}
		*param = '\0';

		/* Parse numbers */
		size      = NUMPARSE('\0', size_t, true);
		start     = NUMPARSE('@', dma_addr_t, true);
		alignment = NUMPARSE('/', dma_addr_t, (v & (v - 1)) == 0);

		alignment = max(alignment, (dma_addr_t)PAGE_SIZE);
		start     = ALIGN(start, alignment);
		size      = PAGE_ALIGN(size);
		if (start + size < start) {
			pr_err("param: invalid start, size combination\n");
			ret = -EINVAL;
			break;
		}

		/* Parse allocator */
		if (*param == ':') {
			reg->alloc_name = ++param;
			while (*param && *param != ';')
				++param;
			if (param == reg->alloc_name)
				reg->alloc_name = NULL;
		}

		/* Go to next */
		if (*param == ';') {
			*param = '\0';
			++param;
		} else if (*param) {
			pr_err("param: expecting ';' or end of parameter near %s\n",
			       param);
			ret = -EINVAL;
			break;
		}

		/* Add */
		reg->size      = size;
		reg->start     = start;
		reg->alignment = alignment;
		reg->copy_name = 1;

		list_add_tail(&reg->list, &cma_early_regions);

		pr_debug("param: registering early region %s (%p@%p/%p)\n",
			 reg->name, (void *)reg->size, (void *)reg->start,
			 (void *)reg->alignment);
	}

	return ret;
}
early_param("cma", cma_param_parse);

#undef NUMPARSE

#endif


int __init __must_check cma_early_region_register(struct cma_region *reg)
{
	dma_addr_t start, alignment;
	size_t size;

	if (reg->alignment & (reg->alignment - 1))
		return -EINVAL;

	alignment = max(reg->alignment, (dma_addr_t)PAGE_SIZE);
	start     = ALIGN(reg->start, alignment);
	size      = PAGE_ALIGN(reg->size);

	if (start + size < start)
		return -EINVAL;

	reg->size      = size;
	reg->start     = start;
	reg->alignment = alignment;

	list_add_tail(&reg->list, &cma_early_regions);

	pr_debug("param: registering early region %s (%p@%p/%p)\n",
		 reg->name, (void *)reg->size, (void *)reg->start,
		 (void *)reg->alignment);

	return 0;
}



/************************* Regions & Allocators *************************/

static void __cma_sysfs_region_add(struct cma_region *reg);

static int __cma_region_attach_alloc(struct cma_region *reg);
static void __maybe_unused __cma_region_detach_alloc(struct cma_region *reg);


/* List of all regions.  Named regions are kept before unnamed. */
static LIST_HEAD(cma_regions);

#define cma_foreach_region(reg) \
	list_for_each_entry(reg, &cma_regions, list)

bool cma_is_registered_region(phys_addr_t start, size_t size)
{
	struct cma_region *reg;

	if (start + size <= start)
		return false;

	cma_foreach_region(reg) {
		if ((start >= reg->start) &&
			((start + size) <= (reg->start + reg->size)) &&
			(size <= reg->size) &&
			(start < (reg->start + reg->size)))
			return true;
	}
	return false;
}

int __must_check cma_region_register(struct cma_region *reg)
{
	const char *name, *alloc_name;
	struct cma_region *r;
	char *ch = NULL;
	int ret = 0;

	if (!reg->size || reg->start + reg->size < reg->start)
		return -EINVAL;

	reg->users = 0;
	reg->used = 0;
	reg->private_data = NULL;
	reg->registered = 0;
	reg->free_space = reg->size;

	/* Copy name and alloc_name */
	name = reg->name;
	alloc_name = reg->alloc_name;
	if (reg->copy_name && (reg->name || reg->alloc_name)) {
		size_t name_size, alloc_size;

		name_size  = reg->name       ? strlen(reg->name) + 1       : 0;
		alloc_size = reg->alloc_name ? strlen(reg->alloc_name) + 1 : 0;

		ch = kmalloc(name_size + alloc_size, GFP_KERNEL);
		if (!ch) {
			pr_err("%s: not enough memory to allocate name\n",
			       reg->name ?: "(private)");
			return -ENOMEM;
		}

		if (name_size) {
			memcpy(ch, reg->name, name_size);
			name = ch;
			ch += name_size;
		}

		if (alloc_size) {
			memcpy(ch, reg->alloc_name, alloc_size);
			alloc_name = ch;
		}
	}

	mutex_lock(&cma_mutex);

	/* Don't let regions overlap */
	cma_foreach_region(r)
		if (r->start + r->size > reg->start &&
		    r->start < reg->start + reg->size) {
			ret = -EADDRINUSE;
			goto done;
		}

	if (reg->alloc) {
		ret = __cma_region_attach_alloc(reg);
		if (unlikely(ret < 0))
			goto done;
	}

	reg->name = name;
	reg->alloc_name = alloc_name;
	reg->registered = 1;
	ch = NULL;

	/*
	 * Keep named at the beginning and unnamed (private) at the
	 * end.  This helps in traversal when named region is looked
	 * for.
	 */
	if (name)
		list_add(&reg->list, &cma_regions);
	else
		list_add_tail(&reg->list, &cma_regions);

	__cma_sysfs_region_add(reg);

done:
	mutex_unlock(&cma_mutex);

	pr_debug("%s: region %sregistered\n",
		 reg->name ?: "(private)", ret ? "not " : "");
	kfree(ch);

	return ret;
}
EXPORT_SYMBOL_GPL(cma_region_register);

static struct cma_region *__must_check
__cma_region_find(const char **namep)
{
	struct cma_region *reg;
	const char *ch, *name;
	size_t n;

	ch = *namep;
	while (*ch && *ch != ',' && *ch != ';')
		++ch;
	name = *namep;
	*namep = *ch == ',' ? ch + 1 : ch;
	n = ch - name;

	/*
	 * Named regions are kept in front of unnamed so if we
	 * encounter unnamed region we can stop.
	 */
	cma_foreach_region(reg)
		if (!reg->name)
			break;
		else if (!strncmp(name, reg->name, n) && !reg->name[n])
			return reg;

	return NULL;
}


/* List of all allocators. */
static LIST_HEAD(cma_allocators);

#define cma_foreach_allocator(alloc) \
	list_for_each_entry(alloc, &cma_allocators, list)

int cma_allocator_register(struct cma_allocator *alloc)
{
	struct cma_region *reg;
	int first;

	if (!alloc->alloc || !alloc->free)
		return -EINVAL;

	mutex_lock(&cma_mutex);

	first = list_empty(&cma_allocators);

	list_add_tail(&alloc->list, &cma_allocators);

	/*
	 * Attach this allocator to all allocator-less regions that
	 * request this particular allocator (reg->alloc_name equals
	 * alloc->name) or if region wants the first available
	 * allocator and we are the first.
	 */
	cma_foreach_region(reg) {
		if (reg->alloc)
			continue;
		if (reg->alloc_name
		  ? alloc->name && !strcmp(alloc->name, reg->alloc_name)
		  : (!reg->used && first))
			continue;

		reg->alloc = alloc;
		__cma_region_attach_alloc(reg);
	}

	mutex_unlock(&cma_mutex);

	pr_debug("%s: allocator registered\n", alloc->name ?: "(unnamed)");

	return 0;
}
EXPORT_SYMBOL_GPL(cma_allocator_register);

static struct cma_allocator *__must_check
__cma_allocator_find(const char *name)
{
	struct cma_allocator *alloc;

	if (!name)
		return list_empty(&cma_allocators)
			? NULL
			: list_entry(cma_allocators.next,
				     struct cma_allocator, list);

	cma_foreach_allocator(alloc)
		if (alloc->name && !strcmp(name, alloc->name))
			return alloc;

	return NULL;
}



/************************* Initialise CMA *************************/

int __init cma_set_defaults(struct cma_region *regions, const char *map)
{
	if (map) {
		int ret = cma_map_param((char *)map);
		if (unlikely(ret < 0))
			return ret;
	}

	if (!regions)
		return 0;

	for (; regions->size; ++regions) {
		int ret = cma_early_region_register(regions);
		if (unlikely(ret < 0))
			return ret;
	}

	return 0;
}


int __init cma_early_region_reserve(struct cma_region *reg)
{
	int tried = 0;

	if (!reg->size || (reg->alignment & (reg->alignment - 1)) ||
	    reg->reserved)
		return -EINVAL;

#ifndef CONFIG_NO_BOOTMEM

	tried = 1;

	{
		void *ptr = __alloc_bootmem_nopanic(reg->size, reg->alignment,
						    reg->start);
		if (ptr) {
			reg->start = virt_to_phys(ptr);
			reg->reserved = 1;
			return 0;
		}
	}

#endif

#ifdef CONFIG_HAVE_MEMBLOCK

	tried = 1;

	if (reg->start) {
		if (!memblock_is_region_reserved(reg->start, reg->size) &&
		    memblock_reserve(reg->start, reg->size) >= 0) {
			reg->reserved = 1;
			return 0;
		}
	} else {
		/*
		 * Use __memblock_alloc_base() since
		 * memblock_alloc_base() panic()s.
		 */
		u64 ret = __memblock_alloc_base(reg->size, reg->alignment, 0);
		if (ret &&
		    ret < ~(dma_addr_t)0 &&
		    ret + reg->size < ~(dma_addr_t)0 &&
		    ret + reg->size > ret) {
			reg->start = ret;
			reg->reserved = 1;
			return 0;
		}

		if (ret)
			memblock_free(ret, reg->size);
	}

#endif

	return tried ? -ENOMEM : -EOPNOTSUPP;
}

void __init cma_early_regions_reserve(int (*reserve)(struct cma_region *reg))
{
	struct cma_region *reg;

	pr_debug("init: reserving early regions\n");

	if (!reserve)
		reserve = cma_early_region_reserve;

	list_for_each_entry(reg, &cma_early_regions, list) {
		if (reg->reserved) {
			/* nothing */
		} else if (reserve(reg) >= 0) {
			pr_debug("init: %s: reserved %p@%p\n",
				 reg->name ?: "(private)",
				 (void *)reg->size, (void *)reg->start);
			reg->reserved = 1;
		} else {
			pr_warn("init: %s: unable to reserve %p@%p/%p\n",
				reg->name ?: "(private)",
				(void *)reg->size, (void *)reg->start,
				(void *)reg->alignment);
		}
	}
}


static int __init cma_init(void)
{
	struct cma_region *reg, *n;

	pr_debug("init: initialising\n");

	if (cma_map) {
		char *val = kmemdup(cma_map, cma_map_length + 1, GFP_KERNEL);
		cma_map = val;
		if (!val)
			return -ENOMEM;
		val[cma_map_length] = '\0';
	}

	list_for_each_entry_safe(reg, n, &cma_early_regions, list) {
		INIT_LIST_HEAD(&reg->list);
		/*
		 * We don't care if there was an error.  It's a pity
		 * but there's not much we can do about it any way.
		 * If the error is on a region that was parsed from
		 * command line then it will stay and waste a bit of
		 * space; if it was registered using
		 * cma_early_region_register() it's caller's
		 * responsibility to do something about it.
		 */
		if (reg->reserved && cma_region_register(reg) < 0)
			/* ignore error */;
	}

	INIT_LIST_HEAD(&cma_early_regions);

	return 0;
}
/*
 * We want to be initialised earlier than module_init/__initcall so
 * that drivers that want to grab memory at boot time will get CMA
 * ready.  subsys_initcall() seems early enough and not too early at
 * the same time.
 */
subsys_initcall(cma_init);



/************************* SysFS *************************/

#if defined CONFIG_CMA_SYSFS

static struct kobject cma_sysfs_regions;
static int cma_sysfs_regions_ready;


#define CMA_ATTR_INLINE(_type, _name)					\
	(&((struct cma_ ## _type ## _attribute){			\
		.attr	= {						\
			.name	= __stringify(_name),			\
			.mode	= 0644,					\
		},							\
		.show	= cma_sysfs_ ## _type ## _ ## _name ## _show,	\
		.store	= cma_sysfs_ ## _type ## _ ## _name ## _store,	\
	}).attr)

#define CMA_ATTR_RO_INLINE(_type, _name)				\
	(&((struct cma_ ## _type ## _attribute){			\
		.attr	= {						\
			.name	= __stringify(_name),			\
			.mode	= 0444,					\
		},							\
		.show	= cma_sysfs_ ## _type ## _ ## _name ## _show,	\
	}).attr)


struct cma_root_attribute {
	struct attribute attr;
	ssize_t (*show)(char *buf);
	int (*store)(const char *buf);
};

static ssize_t cma_sysfs_root_map_show(char *page)
{
	ssize_t len;

	len = cma_map_length;
	if (!len) {
		*page = 0;
		len = 0;
	} else {
		if (len > (size_t)PAGE_SIZE - 1)
			len = (size_t)PAGE_SIZE - 1;
		memcpy(page, cma_map, len);
		page[len++] = '\n';
	}

	return len;
}

static int cma_sysfs_root_map_store(const char *page)
{
	ssize_t len = cma_map_validate(page);
	char *val = NULL;

	if (len < 0)
		return len;

	if (len) {
		val = kmemdup(page, len + 1, GFP_KERNEL);
		if (!val)
			return -ENOMEM;
		val[len] = '\0';
	}

	kfree(cma_map);
	cma_map = val;
	cma_map_length = len;

	return 0;
}

static ssize_t cma_sysfs_root_allocators_show(char *page)
{
	struct cma_allocator *alloc;
	size_t left = PAGE_SIZE;
	char *ch = page;

	cma_foreach_allocator(alloc) {
		ssize_t l = snprintf(ch, left, "%s ", alloc->name ?: "-");
		ch   += l;
		left -= l;
	}

	if (ch != page)
		ch[-1] = '\n';
	return ch - page;
}

static ssize_t
cma_sysfs_root_show(struct kobject *kobj, struct attribute *attr, char *buf)
{
	struct cma_root_attribute *rattr =
		container_of(attr, struct cma_root_attribute, attr);
	ssize_t ret;

	mutex_lock(&cma_mutex);
	ret = rattr->show(buf);
	mutex_unlock(&cma_mutex);

	return ret;
}

static ssize_t
cma_sysfs_root_store(struct kobject *kobj, struct attribute *attr,
		       const char *buf, size_t count)
{
	struct cma_root_attribute *rattr =
		container_of(attr, struct cma_root_attribute, attr);
	int ret;

	mutex_lock(&cma_mutex);
	ret = rattr->store(buf);
	mutex_unlock(&cma_mutex);

	return ret < 0 ? ret : count;
}

static struct kobj_type cma_sysfs_root_type = {
	.sysfs_ops	= &(const struct sysfs_ops){
		.show	= cma_sysfs_root_show,
		.store	= cma_sysfs_root_store,
	},
	.default_attrs	= (struct attribute * []) {
		CMA_ATTR_INLINE(root, map),
		CMA_ATTR_RO_INLINE(root, allocators),
		NULL
	},
};

static int __init cma_sysfs_init(void)
{
	static struct kobject root;
	static struct kobj_type fake_type;

	struct cma_region *reg;
	int ret;

	/* Root */
	ret = kobject_init_and_add(&root, &cma_sysfs_root_type,
				   mm_kobj, "contiguous");
	if (unlikely(ret < 0)) {
		pr_err("init: unable to add root kobject: %d\n", ret);
		return ret;
	}

	/* Regions */
	ret = kobject_init_and_add(&cma_sysfs_regions, &fake_type,
				   &root, "regions");
	if (unlikely(ret < 0)) {
		pr_err("init: unable to add regions kobject: %d\n", ret);
		return ret;
	}

	mutex_lock(&cma_mutex);
	cma_sysfs_regions_ready = 1;
	cma_foreach_region(reg)
		__cma_sysfs_region_add(reg);
	mutex_unlock(&cma_mutex);

	return 0;
}
device_initcall(cma_sysfs_init);



struct cma_region_attribute {
	struct attribute attr;
	ssize_t (*show)(struct cma_region *reg, char *buf);
	int (*store)(struct cma_region *reg, const char *buf);
};


static ssize_t cma_sysfs_region_name_show(struct cma_region *reg, char *page)
{
	return reg->name ? snprintf(page, PAGE_SIZE, "%s\n", reg->name) : 0;
}

static ssize_t cma_sysfs_region_start_show(struct cma_region *reg, char *page)
{
	return snprintf(page, PAGE_SIZE, "%p\n", (void *)reg->start);
}

static ssize_t cma_sysfs_region_size_show(struct cma_region *reg, char *page)
{
	return snprintf(page, PAGE_SIZE, "%zu\n", reg->size);
}

static ssize_t cma_sysfs_region_free_show(struct cma_region *reg, char *page)
{
	return snprintf(page, PAGE_SIZE, "%zu\n", reg->free_space);
}

static ssize_t cma_sysfs_region_users_show(struct cma_region *reg, char *page)
{
	return snprintf(page, PAGE_SIZE, "%u\n", reg->users);
}

static ssize_t cma_sysfs_region_alloc_show(struct cma_region *reg, char *page)
{
	if (reg->alloc)
		return snprintf(page, PAGE_SIZE, "%s\n",
				reg->alloc->name ?: "-");
	else if (reg->alloc_name)
		return snprintf(page, PAGE_SIZE, "[%s]\n", reg->alloc_name);
	else
		return 0;
}

static int
cma_sysfs_region_alloc_store(struct cma_region *reg, const char *page)
{
	char *s;

	if (reg->alloc && reg->users)
		return -EBUSY;

	if (!*page || *page == '\n') {
		s = NULL;
	} else {
		size_t len;

		for (s = (char *)page; *++s && *s != '\n'; )
			/* nop */;

		len = s - page;
		s = kmemdup(page, len + 1, GFP_KERNEL);
		if (!s)
			return -ENOMEM;
		s[len] = '\0';
	}

	if (reg->alloc)
		__cma_region_detach_alloc(reg);

	if (reg->free_alloc_name)
		kfree(reg->alloc_name);

	reg->alloc_name = s;
	reg->free_alloc_name = !!s;

	return 0;
}


static ssize_t
cma_sysfs_region_show(struct kobject *kobj, struct attribute *attr,
		      char *buf)
{
	struct cma_region *reg = container_of(kobj, struct cma_region, kobj);
	struct cma_region_attribute *rattr =
		container_of(attr, struct cma_region_attribute, attr);
	ssize_t ret;

	mutex_lock(&cma_mutex);
	ret = rattr->show(reg, buf);
	mutex_unlock(&cma_mutex);

	return ret;
}

static int
cma_sysfs_region_store(struct kobject *kobj, struct attribute *attr,
		       const char *buf, size_t count)
{
	struct cma_region *reg = container_of(kobj, struct cma_region, kobj);
	struct cma_region_attribute *rattr =
		container_of(attr, struct cma_region_attribute, attr);
	int ret;

	mutex_lock(&cma_mutex);
	ret = rattr->store(reg, buf);
	mutex_unlock(&cma_mutex);

	return ret < 0 ? ret : count;
}

static struct kobj_type cma_sysfs_region_type = {
	.sysfs_ops	= &(const struct sysfs_ops){
		.show	= cma_sysfs_region_show,
		.store	= cma_sysfs_region_store,
	},
	.default_attrs	= (struct attribute * []) {
		CMA_ATTR_RO_INLINE(region, name),
		CMA_ATTR_RO_INLINE(region, start),
		CMA_ATTR_RO_INLINE(region, size),
		CMA_ATTR_RO_INLINE(region, free),
		CMA_ATTR_RO_INLINE(region, users),
		CMA_ATTR_INLINE(region, alloc),
		NULL
	},
};

static void __cma_sysfs_region_add(struct cma_region *reg)
{
	int ret;

	if (!cma_sysfs_regions_ready)
		return;

	memset(&reg->kobj, 0, sizeof reg->kobj);

	ret = kobject_init_and_add(&reg->kobj, &cma_sysfs_region_type,
				   &cma_sysfs_regions,
				   "%p", (void *)reg->start);

	if (reg->name &&
	    sysfs_create_link(&cma_sysfs_regions, &reg->kobj, reg->name) < 0)
		/* Ignore any errors. */;
}

#else

static void __cma_sysfs_region_add(struct cma_region *reg)
{
	/* nop */
}

#endif


/************************* Chunks *************************/

/* All chunks sorted by start address. */
static struct rb_root cma_chunks_by_start;

static struct cma_chunk *__must_check __cma_chunk_find(dma_addr_t addr)
{
	struct cma_chunk *chunk;
	struct rb_node *n;

	for (n = cma_chunks_by_start.rb_node; n; ) {
		chunk = rb_entry(n, struct cma_chunk, by_start);
		if (addr < chunk->start)
			n = n->rb_left;
		else if (addr > chunk->start)
			n = n->rb_right;
		else
			return chunk;
	}
	WARN(1, KERN_WARNING "no chunk starting at %p\n", (void *)addr);
	return NULL;
}

static int __must_check __cma_chunk_insert(struct cma_chunk *chunk)
{
	struct rb_node **new, *parent = NULL;
	typeof(chunk->start) addr = chunk->start;

	for (new = &cma_chunks_by_start.rb_node; *new; ) {
		struct cma_chunk *c =
			container_of(*new, struct cma_chunk, by_start);

		parent = *new;
		if (addr < c->start) {
			new = &(*new)->rb_left;
		} else if (addr > c->start) {
			new = &(*new)->rb_right;
		} else {
			/*
			 * We should never be here.  If we are it
			 * means allocator gave us an invalid chunk
			 * (one that has already been allocated) so we
			 * refuse to accept it.  Our caller will
			 * recover by freeing the chunk.
			 */
			WARN_ON(1);
			return -EADDRINUSE;
		}
	}

	rb_link_node(&chunk->by_start, parent, new);
	rb_insert_color(&chunk->by_start, &cma_chunks_by_start);

	return 0;
}

static void __cma_chunk_free(struct cma_chunk *chunk)
{
	rb_erase(&chunk->by_start, &cma_chunks_by_start);

	chunk->reg->free_space += chunk->size;
	--chunk->reg->users;

	chunk->reg->alloc->free(chunk);
}


/************************* The Device API *************************/

static const char *__must_check
__cma_where_from(const struct device *dev, const char *type);


/* Allocate. */

static dma_addr_t __must_check
__cma_alloc_from_region(struct cma_region *reg,
			size_t size, dma_addr_t alignment)
{
	struct cma_chunk *chunk;

	pr_debug("allocate %p/%p from %s\n",
		 (void *)size, (void *)alignment,
		 reg ? reg->name ?: "(private)" : "(null)");

	if (!reg || reg->free_space < size)
		return -ENOMEM;

	if (!reg->alloc) {
		if (!reg->used)
			__cma_region_attach_alloc(reg);
		if (!reg->alloc)
			return -ENOMEM;
	}

	chunk = reg->alloc->alloc(reg, size, alignment);
	if (!chunk)
		return -ENOMEM;

	if (unlikely(__cma_chunk_insert(chunk) < 0)) {
		/* We should *never* be here. */
		chunk->reg->alloc->free(chunk);
		kfree(chunk);
		return -EADDRINUSE;
	}

	chunk->reg = reg;
	++reg->users;
	reg->free_space -= chunk->size;
	pr_debug("allocated at %p\n", (void *)chunk->start);
	return chunk->start;
}

dma_addr_t __must_check
cma_alloc_from_region(struct cma_region *reg,
		      size_t size, dma_addr_t alignment)
{
	dma_addr_t addr;

	pr_debug("allocate %p/%p from %s\n",
		 (void *)size, (void *)alignment,
		 reg ? reg->name ?: "(private)" : "(null)");

	if (!size || alignment & (alignment - 1) || !reg)
		return -EINVAL;

	mutex_lock(&cma_mutex);

	addr = reg->registered ?
		__cma_alloc_from_region(reg, PAGE_ALIGN(size),
					max(alignment, (dma_addr_t)PAGE_SIZE)) :
		-EINVAL;

	mutex_unlock(&cma_mutex);

	return addr;
}
EXPORT_SYMBOL_GPL(cma_alloc_from_region);

dma_addr_t __must_check
__cma_alloc(const struct device *dev, const char *type,
	    dma_addr_t size, dma_addr_t alignment)
{
	struct cma_region *reg;
	const char *from;
	dma_addr_t addr;

	if (dev)
		pr_debug("allocate %p/%p for %s/%s\n",
			 (void *)size, (void *)alignment,
			 dev_name(dev), type ?: "");

	if (!size || (alignment & ~alignment))
		return -EINVAL;

	if (alignment < PAGE_SIZE)
		alignment = PAGE_SIZE;

	if (!IS_ALIGNED(size, alignment))
		size = ALIGN(size, alignment);

	mutex_lock(&cma_mutex);

	from = __cma_where_from(dev, type);
	if (unlikely(IS_ERR(from))) {
		addr = PTR_ERR(from);
		goto done;
	}

	pr_debug("allocate %p/%p from one of %s\n",
		 (void *)size, (void *)alignment, from);

	while (*from && *from != ';') {
		reg = __cma_region_find(&from);
		addr = __cma_alloc_from_region(reg, size, alignment);
		if (!IS_ERR_VALUE(addr))
			goto done;
	}

	pr_debug("not enough memory\n");
	addr = -ENOMEM;

done:
	mutex_unlock(&cma_mutex);

	return addr;
}
EXPORT_SYMBOL_GPL(__cma_alloc);


void *cma_get_virt(dma_addr_t phys, dma_addr_t size, int noncached)
{
	unsigned long num_pages, i;
	struct page **pages;
	void *virt;

	if (noncached) {
		num_pages = size >> PAGE_SHIFT;
		pages = kmalloc(num_pages * sizeof(struct page *), GFP_KERNEL);

		if (!pages)
			return ERR_PTR(-ENOMEM);

		for (i = 0; i < num_pages; i++)
			pages[i] = pfn_to_page((phys >> PAGE_SHIFT) + i);

		virt = vmap(pages, num_pages, VM_MAP,
			pgprot_writecombine(PAGE_KERNEL));

		if (!virt) {
			kfree(pages);
			return ERR_PTR(-ENOMEM);
		}

		kfree(pages);
	} else {
		virt = phys_to_virt((unsigned long)phys);
	}

	return virt;
}
EXPORT_SYMBOL_GPL(cma_get_virt);

/* Query information about regions. */
static void __cma_info_add(struct cma_info *infop, struct cma_region *reg)
{
	infop->total_size += reg->size;
	infop->free_size += reg->free_space;
	if (infop->lower_bound > reg->start)
		infop->lower_bound = reg->start;
	if (infop->upper_bound < reg->start + reg->size)
		infop->upper_bound = reg->start + reg->size;
	++infop->count;
}

int
__cma_info(struct cma_info *infop, const struct device *dev, const char *type)
{
	struct cma_info info = { ~(dma_addr_t)0, 0, 0, 0, 0 };
	struct cma_region *reg;
	const char *from;
	int ret;

	if (unlikely(!infop))
		return -EINVAL;

	mutex_lock(&cma_mutex);

	from = __cma_where_from(dev, type);
	if (IS_ERR(from)) {
		ret = PTR_ERR(from);
		info.lower_bound = 0;
		goto done;
	}

	while (*from && *from != ';') {
		reg = __cma_region_find(&from);
		if (reg)
			__cma_info_add(&info, reg);
	}

	ret = 0;
done:
	mutex_unlock(&cma_mutex);

	memcpy(infop, &info, sizeof info);
	return ret;
}
EXPORT_SYMBOL_GPL(__cma_info);


/* Freeing. */
int cma_free(dma_addr_t addr)
{
	struct cma_chunk *c;
	int ret;

	mutex_lock(&cma_mutex);

	c = __cma_chunk_find(addr);

	if (c) {
		__cma_chunk_free(c);
		ret = 0;
	} else {
		ret = -ENOENT;
	}

	mutex_unlock(&cma_mutex);

	if (c)
		pr_debug("free(%p): freed\n", (void *)addr);
	else
		pr_err("free(%p): not found\n", (void *)addr);
	return ret;
}
EXPORT_SYMBOL_GPL(cma_free);


/************************* Miscellaneous *************************/

static int __cma_region_attach_alloc(struct cma_region *reg)
{
	struct cma_allocator *alloc;
	int ret;

	/*
	 * If reg->alloc is set then caller wants us to use this
	 * allocator.  Otherwise we need to find one by name.
	 */
	if (reg->alloc) {
		alloc = reg->alloc;
	} else {
		alloc = __cma_allocator_find(reg->alloc_name);
		if (!alloc) {
			pr_warn("init: %s: %s: no such allocator\n",
				reg->name ?: "(private)",
				reg->alloc_name ?: "(default)");
			reg->used = 1;
			return -ENOENT;
		}
	}

	/* Try to initialise the allocator. */
	reg->private_data = NULL;
	ret = alloc->init ? alloc->init(reg) : 0;
	if (unlikely(ret < 0)) {
		pr_err("init: %s: %s: unable to initialise allocator\n",
		       reg->name ?: "(private)", alloc->name ?: "(unnamed)");
		reg->alloc = NULL;
		reg->used = 1;
	} else {
		reg->alloc = alloc;
		pr_debug("init: %s: %s: initialised allocator\n",
			 reg->name ?: "(private)", alloc->name ?: "(unnamed)");
	}
	return ret;
}

static void __cma_region_detach_alloc(struct cma_region *reg)
{
	if (!reg->alloc)
		return;

	if (reg->alloc->cleanup)
		reg->alloc->cleanup(reg);

	reg->alloc = NULL;
	reg->used = 1;
}


/*
 * s            ::= rules
 * rules        ::= rule [ ';' rules ]
 * rule         ::= patterns '=' regions
 * patterns     ::= pattern [ ',' patterns ]
 * regions      ::= REG-NAME [ ',' regions ]
 * pattern      ::= dev-pattern [ '/' TYPE-NAME ] | '/' TYPE-NAME
 */
static const char *__must_check
__cma_where_from(const struct device *dev, const char *type)
{
	/*
	 * This function matches the pattern from the map attribute
	 * agains given device name and type.  Type may be of course
	 * NULL or an emtpy string.
	 */

	const char *s, *name;
	int name_matched = 0;

	/*
	 * If dev is NULL we were called in alternative form where
	 * type is the from string.  All we have to do is return it.
	 */
	if (!dev)
		return type ?: ERR_PTR(-EINVAL);

	if (!cma_map)
		return ERR_PTR(-ENOENT);

	name = dev_name(dev);
	if (WARN_ON(!name || !*name))
		return ERR_PTR(-EINVAL);

	if (!type)
		type = "common";

	/*
	 * Now we go throught the cma_map attribute.
	 */
	for (s = cma_map; *s; ++s) {
		const char *c;

		/*
		 * If the pattern starts with a slash, the device part of the
		 * pattern matches if it matched previously.
		 */
		if (*s == '/') {
			if (!name_matched)
				goto look_for_next;
			goto match_type;
		}

		/*
		 * We are now trying to match the device name.  This also
		 * updates the name_matched variable.  If, while reading the
		 * spec, we ecnounter comma it means that the pattern does not
		 * match and we need to start over with another pattern (the
		 * one afther the comma).  If we encounter equal sign we need
		 * to start over with another rule.  If there is a character
		 * that does not match, we neet to look for a comma (to get
		 * another pattern) or semicolon (to get another rule) and try
		 * again if there is one somewhere.
		 */

		name_matched = 0;

		for (c = name; *s != '*' && *c; ++c, ++s)
			if (*s == '=')
				goto next_rule;
			else if (*s == ',')
				goto next_pattern;
			else if (*s != '?' && *c != *s)
				goto look_for_next;
		if (*s == '*')
			++s;

		name_matched = 1;

		/*
		 * Now we need to match the type part of the pattern.  If the
		 * pattern is missing it we match only if type points to an
		 * empty string.  Otherwise wy try to match it just like name.
		 */
		if (*s == '/') {
match_type:		/* s points to '/' */
			++s;

			for (c = type; *s && *c; ++c, ++s)
				if (*s == '=')
					goto next_rule;
				else if (*s == ',')
					goto next_pattern;
				else if (*c != *s)
					goto look_for_next;
		}

		/* Return the string behind the '=' sign of the rule. */
		if (*s == '=')
			return s + 1;
		else if (*s == ',')
			return strchr(s, '=') + 1;

		/* Pattern did not match */

look_for_next:
		do {
			++s;
		} while (*s != ',' && *s != '=');
		if (*s == ',')
			continue;

next_rule:	/* s points to '=' */
		s = strchr(s, ';');
		if (!s)
			break;

next_pattern:
		continue;
	}

	return ERR_PTR(-ENOENT);
}
