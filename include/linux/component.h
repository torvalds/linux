#ifndef COMPONENT_H
#define COMPONENT_H

struct device;

struct component_ops {
	int (*bind)(struct device *, struct device *, void *);
	void (*unbind)(struct device *, struct device *, void *);
};

int component_add(struct device *, const struct component_ops *);
void component_del(struct device *, const struct component_ops *);

int component_bind_all(struct device *, void *);
void component_unbind_all(struct device *, void *);

struct master;

struct component_master_ops {
	int (*add_components)(struct device *, struct master *);
	int (*bind)(struct device *);
	void (*unbind)(struct device *);
};

int component_master_add(struct device *, const struct component_master_ops *);
void component_master_del(struct device *,
	const struct component_master_ops *);

int component_master_add_child(struct master *master,
	int (*compare)(struct device *, void *), void *compare_data);

struct component_match;

int component_master_add_with_match(struct device *,
	const struct component_master_ops *, struct component_match *);
void component_match_add(struct device *, struct component_match **,
	int (*compare)(struct device *, void *), void *compare_data);

#endif
