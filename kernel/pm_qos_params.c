/*
 * This module exposes the interface to kernel space for specifying
 * QoS dependencies.  It provides infrastructure for registration of:
 *
 * Dependents on a QoS value : register requests
 * Watchers of QoS value : get notified when target QoS value changes
 *
 * This QoS design is best effort based.  Dependents register their QoS needs.
 * Watchers register to keep track of the current QoS needs of the system.
 *
 * There are 3 basic classes of QoS parameter: latency, timeout, throughput
 * each have defined units:
 * latency: usec
 * timeout: usec <-- currently not used.
 * throughput: kbs (kilo byte / sec)
 *
 * There are lists of pm_qos_objects each one wrapping requests, notifiers
 *
 * User mode requests on a QOS parameter register themselves to the
 * subsystem by opening the device node /dev/... and writing there request to
 * the node.  As long as the process holds a file handle open to the node the
 * client continues to be accounted for.  Upon file release the usermode
 * request is removed and a new qos target is computed.  This way when the
 * request that the application has is cleaned up when closes the file
 * pointer or exits the pm_qos_object will get an opportunity to clean up.
 *
 * Mark Gross <mgross@linux.intel.com>
 */

/*#define DEBUG*/

#include <linux/pm_qos_params.h>
#include <linux/sched.h>
#include <linux/spinlock.h>
#include <linux/slab.h>
#include <linux/time.h>
#include <linux/fs.h>
#include <linux/device.h>
#include <linux/miscdevice.h>
#include <linux/string.h>
#include <linux/platform_device.h>
#include <linux/init.h>

#include <linux/uaccess.h>

/*
 * locking rule: all changes to requests or notifiers lists
 * or pm_qos_object list and pm_qos_objects need to happen with pm_qos_lock
 * held, taken with _irqsave.  One lock to rule them all
 */
struct pm_qos_request_list {
	struct list_head list;
	union {
		s32 value;
		s32 usec;
		s32 kbps;
	};
	int pm_qos_class;
};

static s32 max_compare(s32 v1, s32 v2);
static s32 min_compare(s32 v1, s32 v2);

struct pm_qos_object {
	struct pm_qos_request_list requests;
	struct blocking_notifier_head *notifiers;
	struct miscdevice pm_qos_power_miscdev;
	char *name;
	s32 default_value;
	atomic_t target_value;
	s32 (*comparitor)(s32, s32);
};

static struct pm_qos_object null_pm_qos;
static BLOCKING_NOTIFIER_HEAD(cpu_dma_lat_notifier);
static struct pm_qos_object cpu_dma_pm_qos = {
	.requests = {LIST_HEAD_INIT(cpu_dma_pm_qos.requests.list)},
	.notifiers = &cpu_dma_lat_notifier,
	.name = "cpu_dma_latency",
	.default_value = 2000 * USEC_PER_SEC,
	.target_value = ATOMIC_INIT(2000 * USEC_PER_SEC),
	.comparitor = min_compare
};

static BLOCKING_NOTIFIER_HEAD(network_lat_notifier);
static struct pm_qos_object network_lat_pm_qos = {
	.requests = {LIST_HEAD_INIT(network_lat_pm_qos.requests.list)},
	.notifiers = &network_lat_notifier,
	.name = "network_latency",
	.default_value = 2000 * USEC_PER_SEC,
	.target_value = ATOMIC_INIT(2000 * USEC_PER_SEC),
	.comparitor = min_compare
};


static BLOCKING_NOTIFIER_HEAD(network_throughput_notifier);
static struct pm_qos_object network_throughput_pm_qos = {
	.requests = {LIST_HEAD_INIT(network_throughput_pm_qos.requests.list)},
	.notifiers = &network_throughput_notifier,
	.name = "network_throughput",
	.default_value = 0,
	.target_value = ATOMIC_INIT(0),
	.comparitor = max_compare
};


static struct pm_qos_object *pm_qos_array[] = {
	&null_pm_qos,
	&cpu_dma_pm_qos,
	&network_lat_pm_qos,
	&network_throughput_pm_qos
};

static DEFINE_SPINLOCK(pm_qos_lock);

static ssize_t pm_qos_power_write(struct file *filp, const char __user *buf,
		size_t count, loff_t *f_pos);
static int pm_qos_power_open(struct inode *inode, struct file *filp);
static int pm_qos_power_release(struct inode *inode, struct file *filp);

static const struct file_operations pm_qos_power_fops = {
	.write = pm_qos_power_write,
	.open = pm_qos_power_open,
	.release = pm_qos_power_release,
};

/* static helper functions */
static s32 max_compare(s32 v1, s32 v2)
{
	return max(v1, v2);
}

static s32 min_compare(s32 v1, s32 v2)
{
	return min(v1, v2);
}


static void update_target(int pm_qos_class)
{
	s32 extreme_value;
	struct pm_qos_request_list *node;
	unsigned long flags;
	int call_notifier = 0;

	spin_lock_irqsave(&pm_qos_lock, flags);
	extreme_value = pm_qos_array[pm_qos_class]->default_value;
	list_for_each_entry(node,
			&pm_qos_array[pm_qos_class]->requests.list, list) {
		extreme_value = pm_qos_array[pm_qos_class]->comparitor(
				extreme_value, node->value);
	}
	if (atomic_read(&pm_qos_array[pm_qos_class]->target_value) !=
			extreme_value) {
		call_notifier = 1;
		atomic_set(&pm_qos_array[pm_qos_class]->target_value,
				extreme_value);
		pr_debug(KERN_ERR "new target for qos %d is %d\n", pm_qos_class,
			atomic_read(&pm_qos_array[pm_qos_class]->target_value));
	}
	spin_unlock_irqrestore(&pm_qos_lock, flags);

	if (call_notifier)
		blocking_notifier_call_chain(
				pm_qos_array[pm_qos_class]->notifiers,
					(unsigned long) extreme_value, NULL);
}

static int register_pm_qos_misc(struct pm_qos_object *qos)
{
	qos->pm_qos_power_miscdev.minor = MISC_DYNAMIC_MINOR;
	qos->pm_qos_power_miscdev.name = qos->name;
	qos->pm_qos_power_miscdev.fops = &pm_qos_power_fops;

	return misc_register(&qos->pm_qos_power_miscdev);
}

static int find_pm_qos_object_by_minor(int minor)
{
	int pm_qos_class;

	for (pm_qos_class = 0;
		pm_qos_class < PM_QOS_NUM_CLASSES; pm_qos_class++) {
		if (minor ==
			pm_qos_array[pm_qos_class]->pm_qos_power_miscdev.minor)
			return pm_qos_class;
	}
	return -1;
}

/**
 * pm_qos_request - returns current system wide qos expectation
 * @pm_qos_class: identification of which qos value is requested
 *
 * This function returns the current target value in an atomic manner.
 */
int pm_qos_request(int pm_qos_class)
{
	return atomic_read(&pm_qos_array[pm_qos_class]->target_value);
}
EXPORT_SYMBOL_GPL(pm_qos_request);

/**
 * pm_qos_add_request - inserts new qos request into the list
 * @pm_qos_class: identifies which list of qos request to us
 * @value: defines the qos request
 *
 * This function inserts a new entry in the pm_qos_class list of requested qos
 * performance characteristics.  It recomputes the aggregate QoS expectations
 * for the pm_qos_class of parameters, and returns the pm_qos_request list
 * element as a handle for use in updating and removal.  Call needs to save
 * this handle for later use.
 */
struct pm_qos_request_list *pm_qos_add_request(int pm_qos_class, s32 value)
{
	struct pm_qos_request_list *dep;
	unsigned long flags;

	dep = kzalloc(sizeof(struct pm_qos_request_list), GFP_KERNEL);
	if (dep) {
		if (value == PM_QOS_DEFAULT_VALUE)
			dep->value = pm_qos_array[pm_qos_class]->default_value;
		else
			dep->value = value;
		dep->pm_qos_class = pm_qos_class;

		spin_lock_irqsave(&pm_qos_lock, flags);
		list_add(&dep->list,
			&pm_qos_array[pm_qos_class]->requests.list);
		spin_unlock_irqrestore(&pm_qos_lock, flags);
		update_target(pm_qos_class);
	}

	return dep;
}
EXPORT_SYMBOL_GPL(pm_qos_add_request);

/**
 * pm_qos_update_request - modifies an existing qos request
 * @pm_qos_req : handle to list element holding a pm_qos request to use
 * @value: defines the qos request
 *
 * Updates an existing qos request for the pm_qos_class of parameters along
 * with updating the target pm_qos_class value.
 *
 * Attempts are made to make this code callable on hot code paths.
 */
void pm_qos_update_request(struct pm_qos_request_list *pm_qos_req,
		s32 new_value)
{
	unsigned long flags;
	int pending_update = 0;
	s32 temp;

	if (pm_qos_req) { /*guard against callers passing in null */
		spin_lock_irqsave(&pm_qos_lock, flags);
		if (new_value == PM_QOS_DEFAULT_VALUE)
			temp = pm_qos_array[pm_qos_req->pm_qos_class]->default_value;
		else
			temp = new_value;

		if (temp != pm_qos_req->value) {
			pending_update = 1;
			pm_qos_req->value = temp;
		}
		spin_unlock_irqrestore(&pm_qos_lock, flags);
		if (pending_update)
			update_target(pm_qos_req->pm_qos_class);
	}
}
EXPORT_SYMBOL_GPL(pm_qos_update_request);

/**
 * pm_qos_remove_request - modifies an existing qos request
 * @pm_qos_req: handle to request list element
 *
 * Will remove pm qos request from the list of requests and
 * recompute the current target value for the pm_qos_class.  Call this
 * on slow code paths.
 */
void pm_qos_remove_request(struct pm_qos_request_list *pm_qos_req)
{
	unsigned long flags;
	int qos_class;

	if (pm_qos_req == NULL)
		return;
		/* silent return to keep pcm code cleaner */

	qos_class = pm_qos_req->pm_qos_class;
	spin_lock_irqsave(&pm_qos_lock, flags);
	list_del(&pm_qos_req->list);
	kfree(pm_qos_req);
	spin_unlock_irqrestore(&pm_qos_lock, flags);
	update_target(qos_class);
}
EXPORT_SYMBOL_GPL(pm_qos_remove_request);

/**
 * pm_qos_add_notifier - sets notification entry for changes to target value
 * @pm_qos_class: identifies which qos target changes should be notified.
 * @notifier: notifier block managed by caller.
 *
 * will register the notifier into a notification chain that gets called
 * upon changes to the pm_qos_class target value.
 */
int pm_qos_add_notifier(int pm_qos_class, struct notifier_block *notifier)
{
	int retval;

	retval = blocking_notifier_chain_register(
			pm_qos_array[pm_qos_class]->notifiers, notifier);

	return retval;
}
EXPORT_SYMBOL_GPL(pm_qos_add_notifier);

/**
 * pm_qos_remove_notifier - deletes notification entry from chain.
 * @pm_qos_class: identifies which qos target changes are notified.
 * @notifier: notifier block to be removed.
 *
 * will remove the notifier from the notification chain that gets called
 * upon changes to the pm_qos_class target value.
 */
int pm_qos_remove_notifier(int pm_qos_class, struct notifier_block *notifier)
{
	int retval;

	retval = blocking_notifier_chain_unregister(
			pm_qos_array[pm_qos_class]->notifiers, notifier);

	return retval;
}
EXPORT_SYMBOL_GPL(pm_qos_remove_notifier);

static int pm_qos_power_open(struct inode *inode, struct file *filp)
{
	long pm_qos_class;

	pm_qos_class = find_pm_qos_object_by_minor(iminor(inode));
	if (pm_qos_class >= 0) {
		filp->private_data = (void *) pm_qos_add_request(pm_qos_class,
				PM_QOS_DEFAULT_VALUE);

		if (filp->private_data)
			return 0;
	}
	return -EPERM;
}

static int pm_qos_power_release(struct inode *inode, struct file *filp)
{
	struct pm_qos_request_list *req;

	req = (struct pm_qos_request_list *)filp->private_data;
	pm_qos_remove_request(req);

	return 0;
}


static ssize_t pm_qos_power_write(struct file *filp, const char __user *buf,
		size_t count, loff_t *f_pos)
{
	s32 value;
	int x;
	char ascii_value[11];
	struct pm_qos_request_list *pm_qos_req;

	if (count == sizeof(s32)) {
		if (copy_from_user(&value, buf, sizeof(s32)))
			return -EFAULT;
	} else if (count == 11) { /* len('0x12345678/0') */
		if (copy_from_user(ascii_value, buf, 11))
			return -EFAULT;
		x = sscanf(ascii_value, "%x", &value);
		if (x != 1)
			return -EINVAL;
		pr_debug(KERN_ERR "%s, %d, 0x%x\n", ascii_value, x, value);
	} else
		return -EINVAL;

	pm_qos_req = (struct pm_qos_request_list *)filp->private_data;
	pm_qos_update_request(pm_qos_req, value);

	return count;
}


static int __init pm_qos_power_init(void)
{
	int ret = 0;

	ret = register_pm_qos_misc(&cpu_dma_pm_qos);
	if (ret < 0) {
		printk(KERN_ERR "pm_qos_param: cpu_dma_latency setup failed\n");
		return ret;
	}
	ret = register_pm_qos_misc(&network_lat_pm_qos);
	if (ret < 0) {
		printk(KERN_ERR "pm_qos_param: network_latency setup failed\n");
		return ret;
	}
	ret = register_pm_qos_misc(&network_throughput_pm_qos);
	if (ret < 0)
		printk(KERN_ERR
			"pm_qos_param: network_throughput setup failed\n");

	return ret;
}

late_initcall(pm_qos_power_init);
