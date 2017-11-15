#ifndef _LIVEPATCH_CORE_H
#define _LIVEPATCH_CORE_H

#include <linux/livepatch.h>

extern struct mutex klp_mutex;

static inline bool klp_is_object_loaded(struct klp_object *obj)
{
	return !obj->name || obj->mod;
}

static inline int klp_pre_patch_callback(struct klp_object *obj)
{
	int ret = 0;

	if (obj->callbacks.pre_patch)
		ret = (*obj->callbacks.pre_patch)(obj);

	obj->callbacks.post_unpatch_enabled = !ret;

	return ret;
}

static inline void klp_post_patch_callback(struct klp_object *obj)
{
	if (obj->callbacks.post_patch)
		(*obj->callbacks.post_patch)(obj);
}

static inline void klp_pre_unpatch_callback(struct klp_object *obj)
{
	if (obj->callbacks.pre_unpatch)
		(*obj->callbacks.pre_unpatch)(obj);
}

static inline void klp_post_unpatch_callback(struct klp_object *obj)
{
	if (obj->callbacks.post_unpatch_enabled &&
	    obj->callbacks.post_unpatch)
		(*obj->callbacks.post_unpatch)(obj);

	obj->callbacks.post_unpatch_enabled = false;
}

#endif /* _LIVEPATCH_CORE_H */
