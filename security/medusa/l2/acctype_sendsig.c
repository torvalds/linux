#include <linux/sched.h>
#include <linux/signal.h>
#include <linux/interrupt.h>
#include <linux/medusa/l3/registry.h>
#include <linux/medusa/l3/model.h>
#include "kobject_process.h"
#include <linux/medusa/l1/task.h>
#include <linux/init.h>

/* let's define the 'kill' access type, with object=task and subject=task. */

struct send_signal {
	MEDUSA_ACCESS_HEADER;
	int signal_number;
};

MED_ATTRS(send_signal) {
	MED_ATTR_RO (send_signal, signal_number, "signal_number", MED_SIGNED),
	MED_ATTR_END
};

MED_ACCTYPE(send_signal, "kill", process_kobject, "sender", process_kobject, "receiver");

int __init sendsig_acctype_init(void) {
	MED_REGISTER_ACCTYPE(send_signal,MEDUSA_ACCTYPE_TRIGGEREDATSUBJECT);
	return 0;
}
/* TODO: add the same type, triggered at OBJECT */

medusa_answer_t medusa_sendsig(int sig, struct siginfo *info, struct task_struct *p)
{
	medusa_answer_t retval;
	struct send_signal access;
	struct process_kobject sender;
	struct process_kobject receiver;

	if (in_interrupt())
		return MED_OK;
/* always allow signals coming from kernel - see kernel/signal.c:send_signalnal() */
	if ((unsigned long) info == 1)
		return MED_OK;
	if (info) switch (info->si_code) {
		case CLD_TRAPPED:
		case CLD_STOPPED:
		case CLD_DUMPED:
		case CLD_KILLED:
		case CLD_EXITED:
		case SI_KERNEL:
			return MED_OK;
	}
	if (!MED_MAGIC_VALID(&task_security(current)) &&
		process_kobj_validate_task(current) <= 0)
		return MED_OK;

	if (!MED_MAGIC_VALID(&task_security(p)) &&
		process_kobj_validate_task(p) <= 0)
		return MED_OK;

	if (!VS_INTERSECT(VSS(&task_security(current)), VS(&task_security(p))) ||
			!VS_INTERSECT(VSW(&task_security(current)), VS(&task_security(p))))
		return MED_NO;

	if (MEDUSA_MONITORED_ACCESS_S(send_signal, &task_security(current))) {
		access.signal_number = sig;
		process_kern2kobj(&sender, current);
		process_kern2kobj(&receiver, p);
		retval = MED_DECIDE(send_signal, &access, &sender, &receiver);
		if (retval != MED_ERR)
			return retval;
	}
	return MED_OK;
}

__initcall(sendsig_acctype_init);
