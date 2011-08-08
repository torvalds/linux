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
#include <linux/kernel.h>

#include <linux/uaccess.h>

/*
 * locking rule: all changes to requests or notifiers lists
 * or pm_qos_object list and pm_qos_objects need to happen with pm_qos_lock
 * held, taken with _irqsave.  One lock to rule them all
 */
enum pm_qos_type {
	PM_QOS_MAX,		/* return the largest value */
	PM_QOS_MIN		/* return the smallest value */
};

/*
 * Note: The lockless read path depends on the CPU accessing
 * target_value atomically.  Atomic access is only guaranteed on all CPU
 * types linux supports for 32 bit quantites
 */
struct pm_qos_object {
	struct plist_head requests;
	struct blocking_notifier_head *notifiers;
	struct miscdevice pm_qos_power_miscdev;
	char *name;
	s32 target_value;	/* Do not change to 64 bit */
	s32 default_value;
	enum pm_qos_type type;
};

static DEFINE_SPINLOCK(pm_qos_lock);

static struct pm_qos_object null_pm_qos;
static BLOCKING_NOTIFIER_HEAD(cpu_dma_lat_notifier);
static struct pm_qos_object cpu_dma_pm_qos = {
	.requests = PLIST_HEAD_INIT(cpu_dma_pm_qos.requests),
	.notifiers = &cpu_dma_lat_notifier,
	.name = "cpu_dma_latency",
	.target_value = PM_QOS_CPU_DMA_LAT_DEFAULT_VALUE,
	.default_value = PM_QOS_CPU_DMA_LAT_DEFAULT_VALUE,
	.type = PM_QOS_MIN,
};

static BLOCKING_NOTIFIER_HEAD(network_lat_notifier);
static struct pm_qos_object network_lat_pm_qos = {
	.requests = PLIST_HEAD_INIT(network_lat_pm_qos.requests),
	.notifiers = &network_lat_notifier,
	.name = "network_latency",
	.target_value = PM_QOS_NETWORK_LAT_DEFAULT_VALUE,
	.default_value = PM_QOS_NETWORK_LAT_DEFAULT_VALUE,
	.type = PM_QOS_MIN
};


static BLOCKING_NOTIFIER_HEAD(network_throughput_notifier);
static struct pm_qos_object network_throughput_pm_qos = {
	.requests = PLIST_HEAD_INIT(network_throughput_pm_qos.requests),
	.notifiers = &network_throughput_notifier,
	.name = "network_throughput",
	.target_value = PM_QOS_NETWORK_THROUGHPUT_DEFAULT_VALUE,
	.default_value = PM_QOS_NETWORK_THROUGHPUT_DEFAULT_VALUE,
	.type = PM_QOS_MAX,
};


static struct pm_qos_object *pm_qos_array[] = {
	&null_pm_qos,
	&cpu_dma_pm_qos,
	&network_lat_pm_qos,
	&network_throughput_pm_qos
};

static ssize_t pm_qos_power_write(struct file *filp, const char __user *buf,
		size_t count, loff_t *f_pos);
static ssize_t pm_qos_power_read(struct file *filp, char __user *buf,
		size_t count, loff_t *f_pos);
static int pm_qos_power_open(struct inode *inode, struct file *filp);
static int pm_qos_power_release(struct inode *inode, struct file *filp);

static const struct file_operations pm_qos_power_fops = {
	.write = pm_qos_power_write,
	.read = pm_qos_power_read,
	.open = pm_qos_power_open,
	.release = pm_qos_power_release,
	.llseek = noop_llseek,
};

/* unlocked internal variant */
static inline int pm_qos_get_value(struct pm_qos_object *o)
{
	if (plist_head_empty(&o->requests))
		return o->default_value;

	switch (o->type) {
	case PM_QOS_MIN:
		return plist_first(&o->requests)->prio;

	case PM_QOS_MAX:
		return plist_last(&o->requests)->prio;

	default:
		/* runtime check for not using enum */
		BUG();
	}
}

static inline s32 pm_qos_read_value(struct pm_qos_object *o)
{
	return o->target_value;
}

static inline void pm_qos_set_value(struct pm_qos_object *o, s32 value)
{
	o->target_value = value;
}

static void update_target(struct pm_qos_object *o, struct plist_node *node,
			  int del, int value)
{
	unsigned long flags;
	int prev_value, curr_value;

	spin_lock_irqsave(&pm_qos_lock, flags);
	prev_value = pm_qos_get_value(o);
	/* PM_QOS_DEFAULT_VALUE is a signal that the value is unchanged */
	if (value != PM_QOS_DEFAULT_VALUE) {
		/*
		 * to change the list, we atomically remove, reinit
		 * with new value and add, then see if the extremal
		 * changed
		 */
		plist_del(node, &o->requests);
		plist_node_init(node, value);
		plist_add(node, &o->requests);
	} else if (del) {
		plist_del(node, &o->requests);
	} else {
		plist_add(node, &o->requests);
	}
	curr_value = pm_qos_get_value(o);
	pm_qos_set_value(o, curr_value);
	spin_unlock_irqrestore(&pm_qos_lock, flags);

	if (prev_value != curr_value)
		blocking_notifier_call_chain(o->notifiers,
					     (unsigned long)curr_value,
					     NULL);
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
 * This function returns the current target value.
 */
int pm_qos_request(int pm_qos_class)
{
	return pm_qos_read_value(pm_qos_array[pm_qos_class]);
}
EXPORT_SYMBOL_GPL(pm_qos_request);

int pm_qos_request_active(struct pm_qos_request_list *req)
{
	return req->pm_qos_class != 0;
}
EXPORT_SYMBOL_GPL(pm_qos_request_active);

/**
 * pm_qos_add_request - inserts new qos request into the list
 * @dep: pointer to a preallocated handle
 * @pm_qos_class: identifies which list of qos request to use
 * @value: defines the qos request
 *
 * This function inserts a new entry in the pm_qos_class list of requested qos
 * performance characteristics.  It recomputes the aggregate QoS expectations
 * for the pm_qos_class of parameters and initializes the pm_qos_request_list
 * handle.  Caller needs to save this handle for later use in updates and
 * removal.
 */

void pm_qos_add_request(struct pm_qos_request_list *dep,
			int pm_qos_class, s32 value)
{
	struct pm_qos_object *o =  pm_qos_array[pm_qos_class];
	int new_value;

	if (pm_qos_request_active(dep)) {
		WARN(1, KERN_ERR "pm_qos_add_request() called for already added request\n");
		return;
	}
	if (value == PM_QOS_DEFAULT_VALUE)
		new_value = o->default_value;
	else
		new_value = value;
	plist_node_init(&dep->list, new_value);
	dep->pm_qos_class = pm_qos_class;
	update_target(o, &dep->list, 0, PM_QOS_DEFAULT_VALUE);
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
	s32 temp;
	struct pm_qos_object *o;

	if (!pm_qos_req) /*guard against callers passing in null */
		return;

	if (!pm_qos_request_active(pm_qos_req)) {
		WARN(1, KERN_ERR "pm_qos_update_request() called for unknown object\n");
		return;
	}

	o = pm_qos_array[pm_qos_req->pm_qos_class];

	if (new_value == PM_QOS_DEFAULT_VALUE)
		temp = o->default_value;
	else
		temp = new_value;

	if (temp != pm_qos_req->list.prio)
		update_target(o, &pm_qos_req->list, 0, temp);
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
	struct pm_qos_object *o;

	if (pm_qos_req == NULL)
		return;
		/* silent return to keep pcm code cleaner */

	if (!pm_qos_request_active(pm_qos_req)) {
		WARN(1, KERN_ERR "pm_qos_remove_request() called for unknown object\n");
		return;
	}

	o = pm_qos_array[pm_qos_req->pm_qos_class];
	update_target(o, &pm_qos_req->list, 1, PM_QOS_DEFAULT_VALUE);
	memset(pm_qos_req, 0, sizeof(*pm_qos_req));
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
               struct pm_qos_request_list *req = kzalloc(sizeof(*req), GFP_KERNEL);
		if (!req)
			return -ENOMEM;

		pm_qos_add_request(req, pm_qos_class, PM_QOS_DEFAULT_VALUE);
		filp->private_data = req;

		if (filp->private_data)
			return 0;
	}
	return -EPERM;
}

static int pm_qos_power_release(struct inode *inode, struct file *filp)
{
	struct pm_qos_request_list *req;

	req = filp->private_data;
	pm_qos_remove_request(req);
	kfree(req);

	return 0;
}


static ssize_t pm_qos_power_read(struct file *filp, char __user *buf,
		size_t count, loff_t *f_pos)
{
	s32 value;
	unsigned long flags;
	struct pm_qos_object *o;
	struct pm_qos_request_list *pm_qos_req = filp->private_data;

	if (!pm_qos_req)
		return -EINVAL;
	if (!pm_qos_request_active(pm_qos_req))
		return -EINVAL;

	o = pm_qos_array[pm_qos_req->pm_qos_class];
	spin_lock_irqsave(&pm_qos_lock, flags);
	value = pm_qos_get_value(o);
	spin_unlock_irqrestore(&pm_qos_lock, flags);

	return simple_read_from_buffer(buf, count, f_pos, &value, sizeof(s32));
}

static ssize_t pm_qos_power_write(struct file *filp, const char __user *buf,
		size_t count, loff_t *f_pos)
{
	s32 value;
	struct pm_qos_request_list *pm_qos_req;

	if (count == sizeof(s32)) {
		if (copy_from_user(&value, buf, sizeof(s32)))
			return -EFAULT;
	} else if (count <= 11) { /* ASCII perhaps? */
		char ascii_value[11];
		unsigned long int ulval;
		int ret;

		if (copy_from_user(ascii_value, buf, count))
			return -EFAULT;

		if (count > 10) {
			if (ascii_value[10] == '\n')
				ascii_value[10] = '\0';
			else
				return -EINVAL;
		} else {
			ascii_value[count] = '\0';
		}
		ret = strict_strtoul(ascii_value, 16, &ulval);
		if (ret) {
			pr_debug("%s, 0x%lx, 0x%x\n", ascii_value, ulval, ret);
			return -EINVAL;
		}
		value = (s32)lower_32_bits(ulval);
	} else {
		return -EINVAL;
	}

	pm_qos_req = filp->private_data;
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
