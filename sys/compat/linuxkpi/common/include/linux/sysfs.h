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
#ifndef	_LINUX_SYSFS_H_
#define	_LINUX_SYSFS_H_

#include <sys/types.h>
#include <sys/sysctl.h>
#include <sys/errno.h>

#include <linux/kobject.h>

struct sysfs_ops {
	ssize_t (*show)(struct kobject *, struct attribute *, char *);
	ssize_t (*store)(struct kobject *, struct attribute *, const char *,
	    size_t);
};

struct attribute_group {
	const char		*name;
	mode_t			(*is_visible)(struct kobject *,
				    struct attribute *, int);
	struct attribute	**attrs;
};

#define	__ATTR(_name, _mode, _show, _store) {				\
	.attr = { .name = __stringify(_name), .mode = _mode },		\
	.show = _show, .store  = _store,				\
}
#define	__ATTR_RO(_name)	__ATTR(_name, 0444, _name##_show, NULL)
#define	__ATTR_WO(_name)	__ATTR(_name, 0200, NULL, _name##_store)
#define	__ATTR_RW(_name)	__ATTR(_name, 0644, _name##_show, _name##_store)

#define	__ATTR_NULL	{ .attr = { .name = NULL } }

#define	ATTRIBUTE_GROUPS(_name)						\
	static struct attribute_group _name##_group = {			\
		.attrs = _name##_attrs,					\
	};								\
	static struct attribute_group *_name##_groups[] = {		\
		&_name##_group,						\
		NULL,							\
	};

/*
 * Handle our generic '\0' terminated 'C' string.
 * Two cases:
 *      a variable string:  point arg1 at it, arg2 is max length.
 *      a constant string:  point arg1 at it, arg2 is zero.
 */

static inline int
sysctl_handle_attr(SYSCTL_HANDLER_ARGS)
{
	struct kobject *kobj;
	struct attribute *attr;
	const struct sysfs_ops *ops;
	char *buf;
	int error;
	ssize_t len;

	kobj = arg1;
	attr = (struct attribute *)(intptr_t)arg2;
	if (kobj->ktype == NULL || kobj->ktype->sysfs_ops == NULL)
		return (ENODEV);
	buf = (char *)get_zeroed_page(GFP_KERNEL);
	if (buf == NULL)
		return (ENOMEM);
	ops = kobj->ktype->sysfs_ops;
	if (ops->show) {
		len = ops->show(kobj, attr, buf);
		/*
		 * It's valid to not have a 'show' so just return an
		 * empty string.
		 */
		if (len < 0) {
			error = -len;
			if (error != EIO)
				goto out;
			buf[0] = '\0';
		} else if (len) {
			len--;
			if (len >= PAGE_SIZE)
				len = PAGE_SIZE - 1;
			/* Trim trailing newline. */
			buf[len] = '\0';
		}
	}

	/* Leave one trailing byte to append a newline. */
	error = sysctl_handle_string(oidp, buf, PAGE_SIZE - 1, req);
	if (error != 0 || req->newptr == NULL || ops->store == NULL)
		goto out;
	len = strlcat(buf, "\n", PAGE_SIZE);
	KASSERT(len < PAGE_SIZE, ("new attribute truncated"));
	len = ops->store(kobj, attr, buf, len);
	if (len < 0)
		error = -len;
out:
	free_page((unsigned long)buf);

	return (error);
}

static inline int
sysfs_create_file(struct kobject *kobj, const struct attribute *attr)
{
	struct sysctl_oid *oid;

	oid = SYSCTL_ADD_OID(NULL, SYSCTL_CHILDREN(kobj->oidp), OID_AUTO,
	    attr->name, CTLTYPE_STRING|CTLFLAG_RW|CTLFLAG_MPSAFE, kobj,
	    (uintptr_t)attr, sysctl_handle_attr, "A", "");
	if (!oid) {
		return (-ENOMEM);
	}

	return (0);
}

static inline void
sysfs_remove_file(struct kobject *kobj, const struct attribute *attr)
{

	if (kobj->oidp)
		sysctl_remove_name(kobj->oidp, attr->name, 1, 1);
}

static inline void
sysfs_remove_group(struct kobject *kobj, const struct attribute_group *grp)
{

	if (kobj->oidp)
		sysctl_remove_name(kobj->oidp, grp->name, 1, 1);
}

static inline int
sysfs_create_group(struct kobject *kobj, const struct attribute_group *grp)
{
	struct attribute **attr;
	struct sysctl_oid *oidp;

	oidp = SYSCTL_ADD_NODE(NULL, SYSCTL_CHILDREN(kobj->oidp),
	    OID_AUTO, grp->name, CTLFLAG_RD|CTLFLAG_MPSAFE, NULL, grp->name);
	for (attr = grp->attrs; *attr != NULL; attr++) {
		SYSCTL_ADD_OID(NULL, SYSCTL_CHILDREN(oidp), OID_AUTO,
		    (*attr)->name, CTLTYPE_STRING|CTLFLAG_RW|CTLFLAG_MPSAFE,
		    kobj, (uintptr_t)*attr, sysctl_handle_attr, "A", "");
	}

	return (0);
}

static inline int
sysfs_create_dir(struct kobject *kobj)
{
	struct sysctl_oid *oid;

	oid = SYSCTL_ADD_NODE(NULL, SYSCTL_CHILDREN(kobj->parent->oidp),
	    OID_AUTO, kobj->name, CTLFLAG_RD|CTLFLAG_MPSAFE, NULL, kobj->name);
	if (!oid) {
		return (-ENOMEM);
	}
	kobj->oidp = oid;

	return (0);
}

static inline void
sysfs_remove_dir(struct kobject *kobj)
{

	if (kobj->oidp == NULL)
		return;
	sysctl_remove_oid(kobj->oidp, 1, 1);
}

#define sysfs_attr_init(attr) do {} while(0)

#endif	/* _LINUX_SYSFS_H_ */
