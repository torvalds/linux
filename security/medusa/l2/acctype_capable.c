/* capable_acctype.c, (C) 2002 Milan Pikula
 *
 * This file defines the 'capable' call.
 */

#include <linux/medusa/l3/arch.h>
#include <linux/medusa/l3/registry.h>
#include <linux/init.h>
#include <linux/mm.h>

#include "kobject_process.h"

struct capable_access {
	MEDUSA_ACCESS_HEADER;
	int cap;
};

MED_ATTRS(capable_access) {
	MED_ATTR_RO (capable_access, cap, "cap", MED_BITMAP | MED_LE),
	MED_ATTR_END
};

MED_ACCTYPE(capable_access, "capable",
		process_kobject, "process",
		process_kobject, "process");

int __init capable_acctype_init(void) {
	MED_REGISTER_ACCTYPE(capable_access, MEDUSA_ACCTYPE_TRIGGEREDATSUBJECT);
	return 0;
}

medusa_answer_t medusa_capable(int cap)
{
	struct capable_access access;
	struct process_kobject process;
	medusa_answer_t retval;

        memset(&access, '\0', sizeof(struct capable_access));
        /* process_kobject process is zeroed by process_kern2kobj function */

	if (in_interrupt()) {
		med_pr_warn("CAPABLE IN INTERRUPT\n");
#warning "finish me"
		return MED_OK;
	}
	if (!MED_MAGIC_VALID(&task_security(current)) &&
		process_kobj_validate_task(current) <= 0)
		return MED_OK;

	if (MEDUSA_MONITORED_ACCESS_S(capable_access,&task_security(current))) {
		access.cap = CAP_TO_MASK(cap);
		process_kern2kobj(&process, current);
		retval = MED_DECIDE(capable_access, &access, &process, &process);
		if (retval != MED_ERR)
			return retval;
	}
	return MED_OK;
}
__initcall(capable_acctype_init);
