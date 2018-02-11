#include <linux/medusa/l3/kobject.h>
#include <linux/medusa/l3/constants.h>

struct medusa_kobject_s * ipc_fetch(unsigned int id, unsigned int ipc_class, int (*ipc_kern2kobj)(struct medusa_kobject_s *, struct kern_ipc_perm *));
medusa_answer_t ipc_update(unsigned int id, unsigned int ipc_class, struct medusa_kobject_s * kobj, int (*ipc_kobj2kern)(struct medusa_kobject_s *, struct kern_ipc_perm *));
