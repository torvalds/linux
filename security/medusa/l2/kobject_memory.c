/* kobject_memory.c, (C) 2002 Martin Ockajak, Milan Pikula */

/* This kclass allows a user to read from a given process's memory, or
 * write there, using `fetch' and `update'. Kobjects of this type are
 * never used as a subject or object of any `access' (and thus they don't
 * need to contain object or subject vars).
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

struct memory_kobject {
	pid_t  pid;		/* pid of process to read/write */
	void * address;		/* address for read/write */
	size_t size;		/* size of the data, must be <= 512 */
	ssize_t retval;		/* either the size of data, or errno */
	char data[512];		/* data to be written, or data read */
};

MED_ATTRS(memory_kobject) {
	MED_ATTR_KEY_RO	(memory_kobject, pid, "pid", MED_UNSIGNED),
	MED_ATTR_KEY_RO	(memory_kobject, address, "address", MED_UNSIGNED),
	MED_ATTR_KEY_RO	(memory_kobject, size, "size", MED_UNSIGNED),
	MED_ATTR_RO	(memory_kobject, retval, "retval", MED_SIGNED),
	MED_ATTR	(memory_kobject, data, "data", MED_STRING),
	MED_ATTR_END
};

static struct medusa_kobject_s * memory_fetch(struct medusa_kobject_s *);
static medusa_answer_t memory_update(struct medusa_kobject_s *);

MED_KCLASS(memory_kobject) {
	MEDUSA_KCLASS_HEADER(memory_kobject),
	"memory",
	NULL,		/* init kclass */
	NULL,		/* destroy kclass */
	memory_fetch,	/* fetch kobject */
	memory_update,	/* update kobject */
	NULL,		/* unmonitor */
};

/* module stuff */

#ifdef MODULE
static int memory_kobject_unload_check(void) __exit;
#endif

int __init memory_kobject_init(void)
{
#ifdef MODULE
	THIS_MODULE->can_unload = memory_kobject_unload_check;
#endif
	MED_REGISTER_KCLASS(memory_kobject);
	return 1;
}

/* After this is called, and returns 0, memory_kobject_rmmod should be. */
static int __exit memory_kobject_unload_check(void)
{
	if (med_unlink_kclass(&MED_KCLASSOF(memory_kobject)) != MED_OK)
		return -EBUSY;
	return 0;
}

void __exit memory_kobject_rmmod(void)
{
	med_unregister_kclass(&MED_KCLASSOF(memory_kobject));
}

module_init(memory_kobject_init);
module_exit(memory_kobject_rmmod);
/* quoting the comment in module.h:
 *
 * There are dual licensed components, but when running with Linux it is the
 * GPL that is relevant so this is a non issue. 
 */
MODULE_LICENSE("GPL");

/* implementation */

// static struct memory_kobject storage;

static struct medusa_kobject_s * memory_fetch(struct medusa_kobject_s * key_obj)
{
	int ret;
	struct task_struct * p;
	struct memory_kobject* kobj = (struct memory_kobject *) key_obj;

	rcu_read_lock();
	//p = find_task_by_pid(storage.pid);
	p = pid_task(find_vpid(kobj->pid), PIDTYPE_PID);
	if (p) {
		get_task_struct(p);
		rcu_read_unlock();
		ret = access_process_vm(p, (unsigned long)kobj->address, kobj->data, kobj->size, 0);
		//free_task_struct(p);
		free_task(p);
		kobj->retval = ret;
		return key_obj;
	}
	rcu_read_unlock();
	/* subject to change */
	kobj->retval = -ESRCH;
	return key_obj;
}

static medusa_answer_t memory_update(struct medusa_kobject_s * kobj)
{
	int ret;
	struct task_struct * p;

	rcu_read_lock();
	//p = find_task_by_pid(((struct memory_kobject *) kobj)->pid);
	p = pid_task(find_vpid(((struct memory_kobject *) kobj)->pid), PIDTYPE_PID);
	if (p) {
		get_task_struct(p);
		rcu_read_unlock();
		ret = access_process_vm(p,
			(unsigned long)
				((struct memory_kobject *) kobj)->address,
			((struct memory_kobject *) kobj)->data,
			((struct memory_kobject *) kobj)->size,
			1);
		//free_task_struct(p);
		free_task(p);
		return (ret == ((struct memory_kobject *) kobj)->size) ?
			MED_OK : MED_ERR;
	}
	rcu_read_unlock();
	return MED_ERR;
}

