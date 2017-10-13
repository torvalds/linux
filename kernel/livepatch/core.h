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
	int ret;

	ret = (obj->callbacks.pre_patch) ?
		(*obj->callbacks.pre_patch)(obj) : 0;

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
}

#endif /* _LIVEPATCH_CORE_H */
