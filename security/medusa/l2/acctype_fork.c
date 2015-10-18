#include <linux/medusa/l3/registry.h>
#include "kobject_process.h"
#include <linux/medusa/l1/task.h>
#include <linux/init.h>

/* let's define the 'fork' access type, with object=task and subject=task. */

struct fork_access {
	MEDUSA_ACCESS_HEADER;
	unsigned long clone_flags;
};

MED_ATTRS(fork_access) {
	MED_ATTR_RO (fork_access, clone_flags, "clone_flags", MED_UNSIGNED),
	MED_ATTR_END
};

MED_ACCTYPE(fork_access, "fork", process_kobject, "parent",
		process_kobject, "child");

int __init fork_acctype_init(void) {
	MED_REGISTER_ACCTYPE(fork_access,MEDUSA_ACCTYPE_TRIGGEREDATSUBJECT);
	return 0;
}

medusa_answer_t medusa_fork(struct task_struct *new, unsigned long clone_flags)
{
	medusa_answer_t retval = MED_OK;
	struct fork_access access;
	struct process_kobject parent;
	struct process_kobject child;

	if (!MED_MAGIC_VALID(&task_security(current)) &&
		process_kobj_validate_task(current) <= 0)
		return MED_OK;
	if (!MED_MAGIC_VALID(&task_security(new)) &&
		process_kobj_validate_task(new) <= 0)
		return MED_OK;

#ifdef CONFIG_MEDUSA_FORCE
	task_security(new).force_code = NULL;
#endif

	if (MEDUSA_MONITORED_ACCESS_S(fork_access, &task_security(current))) {
		access.clone_flags = clone_flags;
		process_kern2kobj(&parent, current);
		process_kern2kobj(&child, new);
		retval = MED_DECIDE(fork_access, &access, &parent, &child);
		if (retval == MED_ERR)
			retval = MED_OK;
	}
	return retval;
}
__initcall(fork_acctype_init);
