/*
 * Copyright (c) 2012 NVIDIA CORPORATION.  All rights reserved.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; version 2 of the License.
 *
 * This program is distributed in the hope that it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License for
 * more details.
 *
 * You should have received a copy of the GNU General Public License along
 * with this program; if not, write to the Free Software Foundation, Inc.,
 * 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301, USA.
 *
 */

#ifndef _LINUX_CPUONLINE_H
#define _LINUX_CPUONLINE_H

#include <linux/sysfs.h>
#include <linux/kobject.h>

#define CPUQUIET_NAME_LEN 16

struct cpuquiet_governor {
	char			name[CPUQUIET_NAME_LEN];
	struct list_head	governor_list;
	int (*start)		(void);
	void (*stop)		(void);
	int (*store_active)	(unsigned int cpu, bool active);
	void (*device_free_notification) (void);
	void (*device_busy_notification) (void);
	struct module		*owner;
};

struct cpuquiet_driver {
	char			name[CPUQUIET_NAME_LEN];
	int (*quiesence_cpu)	(unsigned int cpunumber);
	int (*wake_cpu)		(unsigned int cpunumber);
};

extern int cpuquiet_register_governor(struct cpuquiet_governor *gov);
extern void cpuquiet_unregister_governor(struct cpuquiet_governor *gov);
extern int cpuquiet_quiesence_cpu(unsigned int cpunumber);
extern int cpuquiet_wake_cpu(unsigned int cpunumber);
extern int cpuquiet_register_driver(struct cpuquiet_driver *drv);
extern void cpuquiet_unregister_driver(struct cpuquiet_driver *drv);
extern int cpuquiet_add_group(struct attribute_group *attrs);
extern void cpuquiet_remove_group(struct attribute_group *attrs);
extern void cpuquiet_device_busy(void);
extern void cpuquiet_device_free(void);
int cpuquiet_kobject_init(struct kobject *kobj, struct kobj_type *type,
				char *name);
extern unsigned int nr_cluster_ids;

/* Sysfs support */
struct cpuquiet_attribute {
	struct attribute attr;
	ssize_t (*show)(struct cpuquiet_attribute *attr, char *buf);
	ssize_t (*store)(struct cpuquiet_attribute *attr, const char *buf,
				size_t count);
	/* Optional. Called after store is called */
	void (*store_callback)(struct cpuquiet_attribute *attr);
	void *param;
};

#define CPQ_ATTRIBUTE(_name, _mode, _type, _callback) \
	static struct cpuquiet_attribute _name ## _attr = {		\
		.attr = {.name = __stringify(_name), .mode = _mode },	\
		.show = show_ ## _type ## _attribute,			\
		.store = store_ ## _type ## _attribute,			\
		.store_callback = _callback,				\
		.param = &_name,					\
}

#define CPQ_BASIC_ATTRIBUTE(_name, _mode, _type)			\
	CPQ_ATTRIBUTE(_name, _mode, _type, NULL)

#define CPQ_ATTRIBUTE_CUSTOM(_name, _mode, _show, _store) \
	static struct cpuquiet_attribute _name ## _attr = {		\
		.attr = {.name = __stringify(_name), .mode = _mode },	\
		.show = _show,						\
		.store = _store						\
		.store_callback = NULL,					\
		.param = &_name,					\
}


extern ssize_t show_int_attribute(struct cpuquiet_attribute *cattr, char *buf);
extern ssize_t store_int_attribute(struct cpuquiet_attribute *cattr,
					const char *buf, size_t count);
extern ssize_t show_bool_attribute(struct cpuquiet_attribute *cattr, char *buf);
extern ssize_t store_bool_attribute(struct cpuquiet_attribute *cattr,
					const char *buf, size_t count);
extern ssize_t store_uint_attribute(struct cpuquiet_attribute *cattr,
					const char *buf, size_t count);
extern ssize_t show_uint_attribute(struct cpuquiet_attribute *cattr, char *buf);
extern ssize_t store_ulong_attribute(struct cpuquiet_attribute *cattr,
					const char *buf, size_t count);
extern ssize_t show_ulong_attribute(struct cpuquiet_attribute *cattr,
					char *buf);
extern ssize_t cpuquiet_auto_sysfs_show(struct kobject *kobj,
					struct attribute *attr, char *buf);
extern ssize_t cpuquiet_auto_sysfs_store(struct kobject *kobj,
					struct attribute *attr, const char *buf,
					size_t count);
#endif
