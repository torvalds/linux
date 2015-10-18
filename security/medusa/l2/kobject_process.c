/* process_kobject.c, (C) 2002 Milan Pikula */

#include <linux/sched.h>
#include <linux/module.h>
#include <linux/init.h>

#include <linux/medusa/l3/registry.h>

#include <linux/medusa/l1/task.h> /* in fact, linux/sched includes that ;) */

/* first, we will create the data storage structure, also known as 'kobject',
 * and provide simple conversion routines */

/* (that's for L2, e.g. for us:) */

#include "kobject_process.h"

medusa_answer_t process_kobj2kern(struct process_kobject * tk, struct task_struct * ts)
{
	// ts->pgrp = tk->pgrp;
	struct cred* new = (struct cred*)ts->cred;
	kuid_t tsuid;

	tsuid = task_uid(ts);
	if (uid_eq(tsuid, tk->uid)) { /* copied from sys.c:set_user() */
		struct user_struct * old_user, * new_user;

		new_user = alloc_uid(tk->uid);
		if (!new_user)
			return MED_SKIP;
		old_user = find_user(tsuid);
		atomic_dec(&old_user->processes);
		atomic_inc(&new_user->processes);
		if (!uid_valid(task_security(ts).luid))
			task_security(ts).luid = (! uid_valid(tk->uid) ? KUIDT_INIT(-2) : tk->uid);
		new->uid = tk->uid;
		new->user =  new_user;
		free_uid(old_user);
	}
	
	new->euid = tk->euid;
	new->suid = tk->suid; new->fsuid = tk->fsuid;

	new->gid = tk->gid; new->egid = tk->egid; new->sgid = tk->sgid;
	new->fsgid = tk->fsgid;
	new->cap_effective = tk->ecap;
	new->cap_inheritable = tk->icap;
	new->cap_permitted = tk->pcap;
	
	if(! uid_valid(task_security(ts).luid) )
		task_security(ts).luid = tk->luid;
	COPY_MEDUSA_SUBJECT_VARS(&task_security(ts),tk);
	COPY_MEDUSA_OBJECT_VARS(&task_security(ts),tk);
	task_security(ts).user = tk->user;
#ifdef CONFIG_MEDUSA_SYSCALL
	memcpy(task_security(ts).med_syscall, tk->med_syscall, sizeof(task_security(ts).med_syscall));
#endif
	MED_MAGIC_VALIDATE(&task_security(ts));
	return MED_OK;
}
int process_kern2kobj(struct process_kobject * tk, struct task_struct * ts)
{
	tk->parent_pid = tk->child_pid = tk->sibling_pid = 0;

	tk->pid = ts->pid;
	#define TO_TS(ts) ((struct task_struct*)ts)
	if (ts->real_parent) tk->parent_pid = ts->real_parent->pid;
	if (ts->children.next) tk->child_pid = TO_TS(ts->children.next)->pid;
	if (ts->sibling.next) tk->sibling_pid = TO_TS(ts->sibling.next)->pid;
	tk->pgrp = task_pgrp(ts); tk->uid = task_uid(ts); tk->euid = task_euid(ts);
	tk->suid = task_suid(ts); tk->fsuid = task_fsuid(ts);
	tk->gid = task_gid(ts); tk->egid = task_egid(ts); tk->sgid = task_sgid(ts);
	tk->fsgid = task_fsgid(ts);
	tk->ecap = task_cap_effective(ts);
	tk->icap = task_cap_inheritable(ts);
	tk->pcap = task_cap_permitted(ts);
	
	tk->luid = task_security(ts).luid;
	COPY_MEDUSA_SUBJECT_VARS(tk,&task_security(ts));
	COPY_MEDUSA_OBJECT_VARS(tk,&task_security(ts));
	tk->user = task_security(ts).user;
#ifdef CONFIG_MEDUSA_SYSCALL
	memcpy(tk->med_syscall, task_security(ts).med_syscall, sizeof(tk->med_syscall));
#endif
	return 0;
}

/* second, we will describe its attributes, and provide fetch and update
 * routines */
/* (that's for l4, they will be working with those descriptions) */


MED_ATTRS(process_kobject) {
	MED_ATTR_KEY_RO	(process_kobject, pid, "pid", MED_SIGNED),
	MED_ATTR_RO	(process_kobject, parent_pid, "parent_pid", MED_SIGNED),
	MED_ATTR_RO	(process_kobject, child_pid, "child_pid", MED_SIGNED),
	MED_ATTR_RO	(process_kobject, sibling_pid, "sibling_pid", MED_SIGNED),
	MED_ATTR_RO	(process_kobject, pgrp, "pgrp", MED_SIGNED),
	MED_ATTR	(process_kobject, uid, "uid", MED_UNSIGNED),
	MED_ATTR	(process_kobject, uid, "ruid", MED_UNSIGNED),
	MED_ATTR	(process_kobject, euid, "euid", MED_UNSIGNED),
	MED_ATTR	(process_kobject, suid, "suid", MED_UNSIGNED),
	MED_ATTR	(process_kobject, fsuid, "fsuid", MED_UNSIGNED),
	MED_ATTR	(process_kobject, gid, "rgid", MED_UNSIGNED),
	MED_ATTR	(process_kobject, gid, "gid", MED_UNSIGNED),
	MED_ATTR	(process_kobject, egid, "egid", MED_UNSIGNED),
	MED_ATTR	(process_kobject, sgid, "sgid", MED_UNSIGNED),
	MED_ATTR	(process_kobject, fsgid, "fsgid", MED_UNSIGNED),
	MED_ATTR	(process_kobject, ecap, "ecap", MED_BITMAP),
	MED_ATTR	(process_kobject, icap, "icap", MED_BITMAP),
	MED_ATTR	(process_kobject, pcap, "pcap", MED_BITMAP),

	MED_ATTR	(process_kobject, luid, "luid", MED_UNSIGNED),
	MED_ATTR_SUBJECT(process_kobject),
	MED_ATTR_OBJECT	(process_kobject),
	MED_ATTR	(process_kobject, user, "user", MED_UNSIGNED),
#ifdef CONFIG_MEDUSA_SYSCALL
	MED_ATTR	(process_kobject, med_syscall, "syscall", MED_BITMAP),
#endif

	MED_ATTR_END
};

static struct process_kobject storage;

struct task_struct* find_task_by_pid(pid_t pid) {
	return pid_task(find_vpid(pid), PIDTYPE_PID);
}

static struct medusa_kobject_s * process_fetch(struct medusa_kobject_s * key_obj)
{
	struct task_struct * p;

	read_lock_irq(&tasklist_lock);
	p = find_task_by_pid(((struct process_kobject *)key_obj)->pid);
	if (!p)
		goto out_err;
	process_kern2kobj(&storage, p);
	read_unlock_irq(&tasklist_lock);
	return (struct medusa_kobject_s *)&storage;
out_err:
	read_unlock_irq(&tasklist_lock);
	return NULL;
}
static medusa_answer_t process_update(struct medusa_kobject_s * kobj)
{
	struct task_struct * p;
	medusa_answer_t retval;

	read_lock_irq(&tasklist_lock);
	p = find_task_by_pid(((struct process_kobject *)kobj)->pid);
	if (p) {
		retval = process_kobj2kern((struct process_kobject *)kobj, p);
		read_unlock_irq(&tasklist_lock);
		return retval;
	}
	read_unlock_irq(&tasklist_lock);
	return MED_ERR;
}

static void process_unmonitor(struct medusa_kobject_s * kobj)
{
	struct task_struct * p;

	read_lock_irq(&tasklist_lock);
	p = find_task_by_pid(((struct process_kobject *)kobj)->pid);
	if (p) {
		UNMONITOR_MEDUSA_OBJECT_VARS(&task_security(p));
		UNMONITOR_MEDUSA_SUBJECT_VARS(&task_security(p));
		MED_MAGIC_VALIDATE(&task_security(p));
	}
	read_unlock_irq(&tasklist_lock);
	return;
}

/* third, we will define the kclass, describing such objects */
/* that's for L3, to make it happy */

MED_KCLASS(process_kobject) {
	MEDUSA_KCLASS_HEADER(process_kobject),
	"process",
	NULL,
	NULL,
	process_fetch,
	process_update,
	process_unmonitor,
};

int __init process_kobject_init(void) {
	MED_REGISTER_KCLASS(process_kobject);
	return 0;
}

/* voila, we're done. */
__initcall(process_kobject_init);
