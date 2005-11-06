/*
 */
#include <linux/transport_class.h>

struct raid_template {
	struct transport_container raid_attrs;
};

struct raid_function_template {
	void *cookie;
	int (*is_raid)(struct device *);
	void (*get_resync)(struct device *);
	void (*get_state)(struct device *);
};

enum raid_state {
	RAID_ACTIVE = 1,
	RAID_DEGRADED,
	RAID_RESYNCING,
	RAID_OFFLINE,
};

struct raid_data {
	struct list_head component_list;
	int component_count;
	int level;
	enum raid_state state;
	int resync;
};

#define DEFINE_RAID_ATTRIBUTE(type, attr)				      \
static inline void							      \
raid_set_##attr(struct raid_template *r, struct device *dev, type value) {    \
	struct class_device *cdev =					      \
		attribute_container_find_class_device(&r->raid_attrs.ac, dev);\
	struct raid_data *rd;						      \
	BUG_ON(!cdev);							      \
	rd = class_get_devdata(cdev);					      \
	rd->attr = value;						      \
}									      \
static inline type							      \
raid_get_##attr(struct raid_template *r, struct device *dev) {		      \
	struct class_device *cdev =					      \
		attribute_container_find_class_device(&r->raid_attrs.ac, dev);\
	struct raid_data *rd;						      \
	BUG_ON(!cdev);							      \
	rd = class_get_devdata(cdev);					      \
	return rd->attr;						      \
}

DEFINE_RAID_ATTRIBUTE(int, level)
DEFINE_RAID_ATTRIBUTE(int, resync)
DEFINE_RAID_ATTRIBUTE(enum raid_state, state)
	
struct raid_template *raid_class_attach(struct raid_function_template *);
void raid_class_release(struct raid_template *);

void raid_component_add(struct raid_template *, struct device *,
			struct device *);
