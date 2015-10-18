#include <linux/medusa/l3/registry.h>
#include <linux/init.h>
#include "kobject_process.h"
#include <linux/medusa/l1/task.h>

/* let's define the 'ptrace' access type, with object=task and subject=task. */

struct ptrace_access {
	MEDUSA_ACCESS_HEADER;
};

MED_ATTRS(ptrace_access) {
	MED_ATTR_END
};

MED_ACCTYPE(ptrace_access, "ptrace", process_kobject, "tracer",
		process_kobject, "tracee");

int __init ptrace_acctype_init(void) {
	MED_REGISTER_ACCTYPE(ptrace_access,
		/* to object or not to object? now THAT is a question ;). */
			MEDUSA_ACCTYPE_TRIGGEREDATSUBJECT);
	return 0;
}

medusa_answer_t medusa_ptrace(struct task_struct * tracer, struct task_struct * tracee)
{
	struct ptrace_access access;
	struct process_kobject tracer_p;
	struct process_kobject tracee_p;
	medusa_answer_t retval;

	if (!MED_MAGIC_VALID(&task_security(tracer)) &&
		process_kobj_validate_task(tracer) <= 0)
		return MED_OK;

	if (!MED_MAGIC_VALID(&task_security(tracee)) &&
		process_kobj_validate_task(tracee) <= 0)
		return MED_OK;

	if (!VS_INTERSECT(VSS(&task_security(tracer)), VS(&task_security(tracee))) ||
		!VS_INTERSECT(VSW(&task_security(tracer)), VS(&task_security(tracee))))
		return MED_NO;
	if (MEDUSA_MONITORED_ACCESS_S(ptrace_access, &task_security(tracer))) {
		process_kern2kobj(&tracer_p, tracer);
		process_kern2kobj(&tracee_p, tracee);
		retval = MED_DECIDE(ptrace_access, &access, &tracer_p, &tracee_p);
		if (retval != MED_ERR)
			return retval;
	}
	return MED_OK;
}
__initcall(ptrace_acctype_init);
