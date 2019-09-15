/* kobject_cstrmem.c, (C) 2004 Vaclav Lorenc */

/* This kclass allows a user to read from a given process's memory using
 * `fetch'. Kobjects of this type are never used as a subject or object of any
 * `access' (and thus they don't need to contain object or subject vars).
 *
 * This is just slightly modified kobject_memory.c with 'update' method removed 
 * and updated returned object size 
 */

/* And as it isn't really necessary, it's a perfect example of loadable L2
 * module.
 */
#include <linux/module.h>
#include <linux/init.h>
#include <linux/sched.h>
#include <linux/kernel.h>
#include <linux/sched/task.h>
#include <linux/mm.h>
#include <linux/errno.h>
#include <linux/medusa/l3/registry.h>

struct cstrmem_kobject {
	pid_t  pid;		/* pid of process to read/write */
	void * address;		/* address for read/write */
	size_t size;		/* size of the data, must be <= 512 */
	ssize_t retval;		/* either the size of data, or errno */
	char data[512];		/* data to be written, or data read */
};

MED_ATTRS(cstrmem_kobject) {
	MED_ATTR_KEY_RO	(cstrmem_kobject, pid, "pid", MED_UNSIGNED),
	MED_ATTR_KEY_RO	(cstrmem_kobject, address, "address", MED_UNSIGNED),
	MED_ATTR_KEY_RO	(cstrmem_kobject, size, "size", MED_UNSIGNED),
	MED_ATTR_RO	(cstrmem_kobject, retval, "retval", MED_SIGNED),
	MED_ATTR	(cstrmem_kobject, data, "data", MED_STRING),
	MED_ATTR_END
};

static struct medusa_kobject_s * cstrmem_fetch(struct medusa_kobject_s *);

MED_KCLASS(cstrmem_kobject) {
	MEDUSA_KCLASS_HEADER(cstrmem_kobject),
	"cstrmem",
	NULL,		/* init kclass */
	NULL,		/* destroy kclass */
	cstrmem_fetch,	/* fetch kobject */
	NULL,		/* update kobject */
	NULL,		/* unmonitor */
};

/* module stuff */

#ifdef MODULE
static int cstrmem_kobject_unload_check(void) __exit;
#endif

int __init cstrmem_kobject_init(void)
{
#ifdef MODULE
	THIS_MODULE->can_unload = cstrmem_kobject_unload_check;
#endif
	MED_REGISTER_KCLASS(cstrmem_kobject);
	return 1;
}

/* After this is called, and returns 0, cstrmem_kobject_rmmod should be. */
static int __exit cstrmem_kobject_unload_check(void)
{
	if (med_unlink_kclass(&MED_KCLASSOF(cstrmem_kobject)) != MED_OK)
		return -EBUSY;
	return 0;
}

void __exit cstrmem_kobject_rmmod(void)
{
	med_unregister_kclass(&MED_KCLASSOF(cstrmem_kobject));
}

module_init(cstrmem_kobject_init);
module_exit(cstrmem_kobject_rmmod);
/* quoting the comment in module.h:
 *
 * There are dual licensed components, but when running with Linux it is the
 * GPL that is relevant so this is a non issue. 
 */
MODULE_LICENSE("GPL");

/* implementation */

// static struct cstrmem_kobject storage;

static struct medusa_kobject_s * cstrmem_fetch(struct medusa_kobject_s * key_obj)
{
	int i, ret;
	struct task_struct * p;
	struct cstrmem_kobject* kobj = (struct cstrmem_kobject*) key_obj;

	rcu_read_lock();
	//p = find_task_by_pid(storage.pid);
	p = pid_task(find_vpid(kobj->pid), PIDTYPE_PID);
	if (p) {
		get_task_struct(p);
		rcu_read_unlock();
		ret = access_process_vm(p, (unsigned long)kobj->address, kobj->data, kobj->size, 0);
		/* TODO: here it should count characters until the first #0 found in (0,size) boundary */
		for (i = 0; (i < kobj->size) && (((char*)kobj->data)[i]); i++);
		/* end - hope it works... */
		//free_task_struct(p);
		free_task(p);
		/* original: storage.retval = ret; */
		kobj->retval = i;
		return key_obj;
	}
	rcu_read_unlock();
	/* subject to change */
	kobj->retval = -ESRCH;
	return key_obj;
}

