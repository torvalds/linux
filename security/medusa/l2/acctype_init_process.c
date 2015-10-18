#include <linux/medusa/l3/registry.h>
#include "kobject_process.h"
#include <linux/init.h>
#include <linux/medusa/l1/task.h>

struct init_process {
	MEDUSA_ACCESS_HEADER;
};

MED_ATTRS(init_process) {
	MED_ATTR_END
};

MED_ACCTYPE(init_process, "init", process_kobject, "process", process_kobject, "parent");

int __init init_process_acctype_init(void) {
	MED_REGISTER_ACCTYPE(init_process,MEDUSA_ACCTYPE_TRIGGEREDATSUBJECT);
	return 0;
}

medusa_answer_t medusa_init_process(struct task_struct *new)
{
	medusa_answer_t retval = MED_OK;
	struct init_process access;
	struct process_kobject process;
	struct process_kobject parent;

	if (!MED_MAGIC_VALID(&task_security(new)) &&
		process_kobj_validate_task(new) <= 0)
		return MED_OK;

	/* inherit from parent if the action isn't monitored? */
	if (MEDUSA_MONITORED_ACCESS_S(init_process, &task_security(new))) {
		process_kern2kobj(&process, new);
		process_kern2kobj(&parent, current);
		retval = MED_DECIDE(init_process, &access, &process, &parent);
		if (retval == MED_ERR)
			retval = MED_OK;
	}
	return retval;
}

void medusa_kernel_thread(int (*fn) (void *))
{
	INIT_MEDUSA_OBJECT_VARS(&task_security(current));
	INIT_MEDUSA_SUBJECT_VARS(&task_security(current));
	task_security(current).luid = INVALID_UID;
}
__initcall(init_process_acctype_init);
