/*-
 * Copyright (c) 2010 Isilon Systems, Inc.
 * Copyright (c) 2010 iX Systems, Inc.
 * Copyright (c) 2010 Panasas, Inc.
 * Copyright (c) 2013, 2014 Mellanox Technologies, Ltd.
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice unmodified, this list of conditions, and the following
 *    disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR ``AS IS'' AND ANY EXPRESS OR
 * IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES
 * OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED.
 * IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR ANY DIRECT, INDIRECT,
 * INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT
 * NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 * DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
 * THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF
 * THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 *
 * $FreeBSD$
 */
#ifndef	_LINUX_KOBJECT_H_
#define	_LINUX_KOBJECT_H_

#include <machine/stdarg.h>

#include <linux/kernel.h>
#include <linux/kref.h>
#include <linux/list.h>
#include <linux/slab.h>

struct kobject;
struct sysctl_oid;

struct kobj_type {
	void (*release)(struct kobject *kobj);
	const struct sysfs_ops *sysfs_ops;
	struct attribute **default_attrs;
};

extern const struct kobj_type linux_kfree_type;

struct kobject {
	struct kobject		*parent;
	char			*name;
	struct kref		kref;
	const struct kobj_type	*ktype;
	struct list_head	entry;
	struct sysctl_oid	*oidp;
};

extern struct kobject *mm_kobj;

struct attribute {
	const char	*name;
	struct module	*owner;
	mode_t		mode;
};

struct kobj_attribute {
	struct attribute attr;
	ssize_t (*show)(struct kobject *kobj, struct kobj_attribute *attr,
	    char *buf);
	ssize_t (*store)(struct kobject *kobj, struct kobj_attribute *attr,
	    const char *buf, size_t count);
};

static inline void
kobject_init(struct kobject *kobj, const struct kobj_type *ktype)
{

	kref_init(&kobj->kref);
	INIT_LIST_HEAD(&kobj->entry);
	kobj->ktype = ktype;
	kobj->oidp = NULL;
}

void linux_kobject_release(struct kref *kref);

static inline void
kobject_put(struct kobject *kobj)
{

	if (kobj)
		kref_put(&kobj->kref, linux_kobject_release);
}

static inline struct kobject *
kobject_get(struct kobject *kobj)
{

	if (kobj)
		kref_get(&kobj->kref);
	return kobj;
}

int	kobject_set_name_vargs(struct kobject *kobj, const char *fmt, va_list);
int	kobject_add(struct kobject *kobj, struct kobject *parent,
	    const char *fmt, ...);

static inline struct kobject *
kobject_create(void)
{
	struct kobject *kobj;

	kobj = kzalloc(sizeof(*kobj), GFP_KERNEL);
	if (kobj == NULL)
		return (NULL);
	kobject_init(kobj, &linux_kfree_type);

	return (kobj);
}

static inline struct kobject *
kobject_create_and_add(const char *name, struct kobject *parent)
{
	struct kobject *kobj;

	kobj = kobject_create();
	if (kobj == NULL)
		return (NULL);
	if (kobject_add(kobj, parent, "%s", name) == 0)
		return (kobj);
	kobject_put(kobj);

	return (NULL);
}

static inline void
kobject_del(struct kobject *kobj __unused)
{
}

static inline char *
kobject_name(const struct kobject *kobj)
{

	return kobj->name;
}

int	kobject_set_name(struct kobject *kobj, const char *fmt, ...);
int	kobject_init_and_add(struct kobject *kobj, const struct kobj_type *ktype,
	    struct kobject *parent, const char *fmt, ...);

#endif /* _LINUX_KOBJECT_H_ */
