#include <linux/medusa/l3/registry.h>
#include <linux/dcache.h>
#include <linux/init.h>

#include "kobject_process.h"

/* let's define the 'exec' access type, with subj=task and obj=inode */

/* in fact, there are 2 of them. They're exactly the same, and differ
 * only in the place where they are triggered.
 */

struct afterexec_access {
	MEDUSA_ACCESS_HEADER;
};

MED_ATTRS(afterexec_access) {
	MED_ATTR_END
};

MED_ACCTYPE(afterexec_access, "after_exec", process_kobject, "process",
		process_kobject, "process");

int __init afterexec_acctype_init(void) {
	MED_REGISTER_ACCTYPE(afterexec_access,
			MEDUSA_ACCTYPE_TRIGGEREDATSUBJECT);
	return 0;
}

medusa_answer_t medusa_afterexec(char *filename, char **argv, char **envp)
{
	struct afterexec_access access;
	struct process_kobject process;
	medusa_answer_t retval;

	if (!MED_MAGIC_VALID(&task_security(current)) &&
		process_kobj_validate_task(current) <= 0)
		return MED_OK;

	if (MEDUSA_MONITORED_ACCESS_S(afterexec_access, &task_security(current))) {
		process_kern2kobj(&process, current);
		retval = MED_DECIDE(afterexec_access, &access,
				&process, &process);
		if (retval != MED_ERR)
			return retval;
	}
	return MED_OK;
}
int medusa_monitored_afterexec(void)
{
	return MEDUSA_MONITORED_ACCESS_S(afterexec_access, &task_security(current));
}

void medusa_monitor_afterexec(int flag)
{
	if (flag)
		MEDUSA_MONITOR_ACCESS_S(afterexec_access,
				&task_security(current));
	else
		MEDUSA_UNMONITOR_ACCESS_S(afterexec_access,
				&task_security(current));
}
__initcall(afterexec_acctype_init);
