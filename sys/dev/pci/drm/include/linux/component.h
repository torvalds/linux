/* Public domain. */

#ifndef _LINUX_COMPONENT_H
#define _LINUX_COMPONENT_H

#include <sys/_null.h>

struct component_match;
struct device;

struct component_ops {
	int (*bind)(struct device *, struct device *, void *);
	void (*unbind)(struct device *, struct device *, void *);
};

struct component_master_ops {
	int (*bind)(struct device *);
	void (*unbind)(struct device *);
};

int	component_add(struct device *, const struct component_ops *);
int	component_add_typed(struct device *, const struct component_ops *, int);
#define component_del(a, b)

int	component_bind_all(struct device *, void *);
#define component_unbind_all(a, b)

int	component_compare_of(struct device *, void *);
int	component_master_add_with_match(struct device *,
	    const struct component_master_ops *, struct component_match *);

#endif
