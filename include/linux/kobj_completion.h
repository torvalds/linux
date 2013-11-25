#ifndef _KOBJ_COMPLETION_H_
#define _KOBJ_COMPLETION_H_

#include <linux/kobject.h>
#include <linux/completion.h>

struct kobj_completion {
	struct kobject kc_kobj;
	struct completion kc_unregister;
};

#define kobj_to_kobj_completion(kobj) \
	container_of(kobj, struct kobj_completion, kc_kobj)

void kobj_completion_init(struct kobj_completion *kc, struct kobj_type *ktype);
void kobj_completion_release(struct kobject *kobj);
void kobj_completion_del_and_wait(struct kobj_completion *kc);
#endif /* _KOBJ_COMPLETION_H_ */
