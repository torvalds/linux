/*
 * transport_class.h - a generic container for all transport classes
 *
 * Copyright (c) 2005 - James Bottomley <James.Bottomley@steeleye.com>
 *
 * This file is licensed under GPLv2
 */

#ifndef _TRANSPORT_CLASS_H_
#define _TRANSPORT_CLASS_H_

#include <linux/device.h>
#include <linux/attribute_container.h>

struct transport_container;

struct transport_class {
	struct class class;
	int (*setup)(struct transport_container *, struct device *,
		     struct class_device *);
	int (*configure)(struct transport_container *, struct device *,
			 struct class_device *);
	int (*remove)(struct transport_container *, struct device *,
		      struct class_device *);
};

#define DECLARE_TRANSPORT_CLASS(cls, nm, su, rm, cfg)			\
struct transport_class cls = {						\
	.class = {							\
		.name = nm,						\
	},								\
	.setup = su,							\
	.remove = rm,							\
	.configure = cfg,						\
}


struct anon_transport_class {
	struct transport_class tclass;
	struct attribute_container container;
};

#define DECLARE_ANON_TRANSPORT_CLASS(cls, mtch, cfg)		\
struct anon_transport_class cls = {				\
	.tclass = {						\
		.configure = cfg,				\
	},							\
	. container = {						\
		.match = mtch,					\
	},							\
}

#define class_to_transport_class(x) \
	container_of(x, struct transport_class, class)

struct transport_container {
	struct attribute_container ac;
	struct attribute_group *statistics;
};

#define attribute_container_to_transport_container(x) \
	container_of(x, struct transport_container, ac)

void transport_remove_device(struct device *);
void transport_add_device(struct device *);
void transport_setup_device(struct device *);
void transport_configure_device(struct device *);
void transport_destroy_device(struct device *);

static inline void
transport_register_device(struct device *dev)
{
	transport_setup_device(dev);
	transport_add_device(dev);
}

static inline void
transport_unregister_device(struct device *dev)
{
	transport_remove_device(dev);
	transport_destroy_device(dev);
}

static inline int transport_container_register(struct transport_container *tc)
{
	return attribute_container_register(&tc->ac);
}

static inline int transport_container_unregister(struct transport_container *tc)
{
	return attribute_container_unregister(&tc->ac);
}

int transport_class_register(struct transport_class *);
int anon_transport_class_register(struct anon_transport_class *);
void transport_class_unregister(struct transport_class *);
void anon_transport_class_unregister(struct anon_transport_class *);


#endif
