#include <linux/linkage.h>
#include <linux/medusa/l3/registry.h>
#include <linux/medusa/l3/model.h>
#include "kobject_process.h"
#include <linux/medusa/l1/task.h>
#include <linux/init.h>

/* let's define the 'syscall' access type, with subject=task and object=task. */

struct syscall_access {
	MEDUSA_ACCESS_HEADER;
	unsigned int sysnr;
	unsigned int arg1;
	unsigned int arg2;
	unsigned int arg3;
	unsigned int arg4;
	unsigned int arg5;
	unsigned int arg6;
	unsigned int arg7;
	/* is that enough on all archs? */
};

MED_ATTRS(syscall_access) {
	MED_ATTR_RO (syscall_access, sysnr, "sysnr", MED_UNSIGNED),
	MED_ATTR (syscall_access, arg1, "arg1", MED_UNSIGNED),
	MED_ATTR (syscall_access, arg2, "arg2", MED_UNSIGNED),
	MED_ATTR (syscall_access, arg3, "arg3", MED_UNSIGNED),
	MED_ATTR (syscall_access, arg4, "arg4", MED_UNSIGNED),
	MED_ATTR (syscall_access, arg5, "arg5", MED_UNSIGNED),
	MED_ATTR (syscall_access, arg6, "arg6", MED_UNSIGNED),
	MED_ATTR (syscall_access, arg7, "arg7", MED_UNSIGNED),
	MED_ATTR_END
};

MED_ACCTYPE(syscall_access, "syscall", process_kobject, "process", process_kobject, "process");

int __init syscall_acctype_init(void) {
	MED_REGISTER_ACCTYPE(syscall_access,MEDUSA_ACCTYPE_TRIGGEREDATSUBJECT);
	return 0;
}

medusa_answer_t asmlinkage medusa_syscall_i386(
	unsigned int eax,  /* in: syscall #, out: retval */
	struct task_struct *curr,
	volatile unsigned int p1,
	volatile unsigned int p2,
	volatile unsigned int p3,
	volatile unsigned int p4,
	volatile unsigned int p5)
{
	medusa_answer_t retval = MED_OK;
	struct syscall_access access;
	struct process_kobject proc;

	if (!MED_MAGIC_VALID(&task_security(current)) &&
		process_kobj_validate_task(current) <= 0)
		return MED_OK;

	if (MEDUSA_MONITORED_ACCESS_S(syscall_access, &task_security(current))) {
		access.sysnr = eax;
		access.arg1 = p1; access.arg2 = p2;
		access.arg3 = p3; access.arg4 = p4;
		access.arg5 = p5;
		access.arg6 = access.arg7 = 0;
		process_kern2kobj(&proc, current);
		retval = MED_DECIDE(syscall_access, &access, &proc, &proc);
	}
	/* this needs more optimization some day */
	if (retval == MED_NO)
			return 0; /* deny */
	if (retval != MED_SKIP)
		return 1; /* allow */
	return 2; /* skip trace code */
}

__initcall(syscall_acctype_init);

