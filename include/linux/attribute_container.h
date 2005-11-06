/*
 * class_container.h - a generic container for all classes
 *
 * Copyright (c) 2005 - James Bottomley <James.Bottomley@steeleye.com>
 *
 * This file is licensed under GPLv2
 */

#ifndef _ATTRIBUTE_CONTAINER_H_
#define _ATTRIBUTE_CONTAINER_H_

#include <linux/device.h>
#include <linux/list.h>
#include <linux/klist.h>
#include <linux/spinlock.h>

struct attribute_container {
	struct list_head	node;
	struct klist		containers;
	struct class		*class;
	struct class_device_attribute **attrs;
	int (*match)(struct attribute_container *, struct device *);
#define	ATTRIBUTE_CONTAINER_NO_CLASSDEVS	0x01
	unsigned long		flags;
};

static inline int
attribute_container_no_classdevs(struct attribute_container *atc)
{
	return atc->flags & ATTRIBUTE_CONTAINER_NO_CLASSDEVS;
}

static inline void
attribute_container_set_no_classdevs(struct attribute_container *atc)
{
	atc->flags |= ATTRIBUTE_CONTAINER_NO_CLASSDEVS;
}

int attribute_container_register(struct attribute_container *cont);
int attribute_container_unregister(struct attribute_container *cont);
void attribute_container_create_device(struct device *dev,
				       int (*fn)(struct attribute_container *,
						 struct device *,
						 struct class_device *));
void attribute_container_add_device(struct device *dev,
				    int (*fn)(struct attribute_container *,
					      struct device *,
					      struct class_device *));
void attribute_container_remove_device(struct device *dev,
				       void (*fn)(struct attribute_container *,
						  struct device *,
						  struct class_device *));
void attribute_container_device_trigger(struct device *dev, 
					int (*fn)(struct attribute_container *,
						  struct device *,
						  struct class_device *));
void attribute_container_trigger(struct device *dev, 
				 int (*fn)(struct attribute_container *,
					   struct device *));
int attribute_container_add_attrs(struct class_device *classdev);
int attribute_container_add_class_device(struct class_device *classdev);
int attribute_container_add_class_device_adapter(struct attribute_container *cont,
						 struct device *dev,
						 struct class_device *classdev);
void attribute_container_remove_attrs(struct class_device *classdev);
void attribute_container_class_device_del(struct class_device *classdev);
struct attribute_container *attribute_container_classdev_to_container(struct class_device *);
struct class_device *attribute_container_find_class_device(struct attribute_container *, struct device *);
struct class_device_attribute **attribute_container_classdev_to_attrs(const struct class_device *classdev);

#endif
