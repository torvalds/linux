// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * IUCV base infrastructure.
 *
 * Copyright IBM Corp. 2001, 2009
 *
 * Author(s):
 *    Original source:
 *	Alan Altmark (Alan_Altmark@us.ibm.com)	Sept. 2000
 *	Xenia Tkatschow (xenia@us.ibm.com)
 *    2Gb awareness and general cleanup:
 *	Fritz Elfert (elfert@de.ibm.com, felfert@millenux.com)
 *    Rewritten for af_iucv:
 *	Martin Schwidefsky <schwidefsky@de.ibm.com>
 *    PM functions:
 *	Ursula Braun (ursula.braun@de.ibm.com)
 *
 * Documentation used:
 *    The original source
 *    CP Programming Service, IBM document # SC24-5760
 */

#define KMSG_COMPONENT "iucv"
#define pr_fmt(fmt) KMSG_COMPONENT ": " fmt

#include <linux/kernel_stat.h>
#include <linux/module.h>
#include <linux/moduleparam.h>
#include <linux/spinlock.h>
#include <linux/kernel.h>
#include <linux/slab.h>
#include <linux/init.h>
#include <linux/interrupt.h>
#include <linux/list.h>
#include <linux/errno.h>
#include <linux/err.h>
#include <linux/device.h>
#include <linux/cpu.h>
#include <linux/reboot.h>
#include <net/iucv/iucv.h>
#include <linux/atomic.h>
#include <asm/ebcdic.h>
#include <asm/io.h>
#include <asm/irq.h>
#include <asm/smp.h>

/*
 * FLAGS:
 * All flags are defined in the field IPFLAGS1 of each function
 * and can be found in CP Programming Services.
 * IPSRCCLS - Indicates you have specified a source class.
 * IPTRGCLS - Indicates you have specified a target class.
 * IPFGPID  - Indicates you have specified a pathid.
 * IPFGMID  - Indicates you have specified a message ID.
 * IPNORPY  - Indicates a one-way message. No reply expected.
 * IPALL    - Indicates that all paths are affected.
 */
#define IUCV_IPSRCCLS	0x01
#define IUCV_IPTRGCLS	0x01
#define IUCV_IPFGPID	0x02
#define IUCV_IPFGMID	0x04
#define IUCV_IPNORPY	0x10
#define IUCV_IPALL	0x80

static int iucv_bus_match(struct device *dev, struct device_driver *drv)
{
	return 0;
}

enum iucv_pm_states {
	IUCV_PM_INITIAL = 0,
	IUCV_PM_FREEZING = 1,
	IUCV_PM_THAWING = 2,
	IUCV_PM_RESTORING = 3,
};
static enum iucv_pm_states iucv_pm_state;

static int iucv_pm_prepare(struct device *);
static void iucv_pm_complete(struct device *);
static int iucv_pm_freeze(struct device *);
static int iucv_pm_thaw(struct device *);
static int iucv_pm_restore(struct device *);

static const struct dev_pm_ops iucv_pm_ops = {
	.prepare = iucv_pm_prepare,
	.complete = iucv_pm_complete,
	.freeze = iucv_pm_freeze,
	.thaw = iucv_pm_thaw,
	.restore = iucv_pm_restore,
};

struct bus_type iucv_bus = {
	.name = "iucv",
	.match = iucv_bus_match,
	.pm = &iucv_pm_ops,
};
EXPORT_SYMBOL(iucv_bus);

struct device *iucv_root;
EXPORT_SYMBOL(iucv_root);

static int iucv_available;

/* General IUCV interrupt structure */
struct iucv_irq_data {
	u16 ippathid;
	u8  ipflags1;
	u8  iptype;
	u32 res2[8];
};

struct iucv_irq_list {
	struct list_head list;
	struct iucv_irq_data data;
};

static struct iucv_irq_data *iucv_irq_data[NR_CPUS];
static cpumask_t iucv_buffer_cpumask = { CPU_BITS_NONE };
static cpumask_t iucv_irq_cpumask = { CPU_BITS_NONE };

/*
 * Queue of interrupt buffers lock for delivery via the tasklet
 * (fast but can't call smp_call_function).
 */
static LIST_HEAD(iucv_task_queue);

/*
 * The tasklet for fast delivery of iucv interrupts.
 */
static void iucv_tasklet_fn(unsigned long);
static DECLARE_TASKLET(iucv_tasklet, iucv_tasklet_fn,0);

/*
 * Queue of interrupt buffers for delivery via a work queue
 * (slower but can call smp_call_function).
 */
static LIST_HEAD(iucv_work_queue);

/*
 * The work element to deliver path pending interrupts.
 */
static void iucv_work_fn(struct work_struct *work);
static DECLARE_WORK(iucv_work, iucv_work_fn);

/*
 * Spinlock protecting task and work queue.
 */
static DEFINE_SPINLOCK(iucv_queue_lock);

enum iucv_command_codes {
	IUCV_QUERY = 0,
	IUCV_RETRIEVE_BUFFER = 2,
	IUCV_SEND = 4,
	IUCV_RECEIVE = 5,
	IUCV_REPLY = 6,
	IUCV_REJECT = 8,
	IUCV_PURGE = 9,
	IUCV_ACCEPT = 10,
	IUCV_CONNECT = 11,
	IUCV_DECLARE_BUFFER = 12,
	IUCV_QUIESCE = 13,
	IUCV_RESUME = 14,
	IUCV_SEVER = 15,
	IUCV_SETMASK = 16,
	IUCV_SETCONTROLMASK = 17,
};

/*
 * Error messages that are used with the iucv_sever function. They get
 * converted to EBCDIC.
 */
static char iucv_error_no_listener[16] = "NO LISTENER";
static char iucv_error_no_memory[16] = "NO MEMORY";
static char iucv_error_pathid[16] = "INVALID PATHID";

/*
 * iucv_handler_list: List of registered handlers.
 */
static LIST_HEAD(iucv_handler_list);

/*
 * iucv_path_table: an array of iucv_path structures.
 */
static struct iucv_path **iucv_path_table;
static unsigned long iucv_max_pathid;

/*
 * iucv_lock: spinlock protecting iucv_handler_list and iucv_pathid_table
 */
static DEFINE_SPINLOCK(iucv_table_lock);

/*
 * iucv_active_cpu: contains the number of the cpu executing the tasklet
 * or the work handler. Needed for iucv_path_sever called from tasklet.
 */
static int iucv_active_cpu = -1;

/*
 * Mutex and wait queue for iucv_register/iucv_unregister.
 */
static DEFINE_MUTEX(iucv_register_mutex);

/*
 * Counter for number of non-smp capable handlers.
 */
static int iucv_nonsmp_handler;

/*
 * IUCV control data structure. Used by iucv_path_accept, iucv_path_connect,
 * iucv_path_quiesce and iucv_path_sever.
 */
struct iucv_cmd_control {
	u16 ippathid;
	u8  ipflags1;
	u8  iprcode;
	u16 ipmsglim;
	u16 res1;
	u8  ipvmid[8];
	u8  ipuser[16];
	u8  iptarget[8];
} __attribute__ ((packed,aligned(8)));

/*
 * Data in parameter list iucv structure. Used by iucv_message_send,
 * iucv_message_send2way and iucv_message_reply.
 */
struct iucv_cmd_dpl {
	u16 ippathid;
	u8  ipflags1;
	u8  iprcode;
	u32 ipmsgid;
	u32 iptrgcls;
	u8  iprmmsg[8];
	u32 ipsrccls;
	u32 ipmsgtag;
	u32 ipbfadr2;
	u32 ipbfln2f;
	u32 res;
} __attribute__ ((packed,aligned(8)));

/*
 * Data in buffer iucv structure. Used by iucv_message_receive,
 * iucv_message_reject, iucv_message_send, iucv_message_send2way
 * and iucv_declare_cpu.
 */
struct iucv_cmd_db {
	u16 ippathid;
	u8  ipflags1;
	u8  iprcode;
	u32 ipmsgid;
	u32 iptrgcls;
	u32 ipbfadr1;
	u32 ipbfln1f;
	u32 ipsrccls;
	u32 ipmsgtag;
	u32 ipbfadr2;
	u32 ipbfln2f;
	u32 res;
} __attribute__ ((packed,aligned(8)));

/*
 * Purge message iucv structure. Used by iucv_message_purge.
 */
struct iucv_cmd_purge {
	u16 ippathid;
	u8  ipflags1;
	u8  iprcode;
	u32 ipmsgid;
	u8  ipaudit[3];
	u8  res1[5];
	u32 res2;
	u32 ipsrccls;
	u32 ipmsgtag;
	u32 res3[3];
} __attribute__ ((packed,aligned(8)));

/*
 * Set mask iucv structure. Used by iucv_enable_cpu.
 */
struct iucv_cmd_set_mask {
	u8  ipmask;
	u8  res1[2];
	u8  iprcode;
	u32 res2[9];
} __attribute__ ((packed,aligned(8)));

union iucv_param {
	struct iucv_cmd_control ctrl;
	struct iucv_cmd_dpl dpl;
	struct iucv_cmd_db db;
	struct iucv_cmd_purge purge;
	struct iucv_cmd_set_mask set_mask;
};

/*
 * Anchor for per-cpu IUCV command parameter block.
 */
static union iucv_param *iucv_param[NR_CPUS];
static union iucv_param *iucv_param_irq[NR_CPUS];

/**
 * iucv_call_b2f0
 * @code: identifier of IUCV call to CP.
 * @parm: pointer to a struct iucv_parm block
 *
 * Calls CP to execute IUCV commands.
 *
 * Returns the result of the CP IUCV call.
 */
static inline int __iucv_call_b2f0(int command, union iucv_param *parm)
{
	register unsigned long reg0 asm ("0");
	register unsigned long reg1 asm ("1");
	int ccode;

	reg0 = command;
	reg1 = (unsigned long)parm;
	asm volatile(
		"	.long 0xb2f01000\n"
		"	ipm	%0\n"
		"	srl	%0,28\n"
		: "=d" (ccode), "=m" (*parm), "+d" (reg0), "+a" (reg1)
		:  "m" (*parm) : "cc");
	return ccode;
}

static inline int iucv_call_b2f0(int command, union iucv_param *parm)
{
	int ccode;

	ccode = __iucv_call_b2f0(command, parm);
	return ccode == 1 ? parm->ctrl.iprcode : ccode;
}

/**
 * iucv_query_maxconn
 *
 * Determines the maximum number of connections that may be established.
 *
 * Returns the maximum number of connections or -EPERM is IUCV is not
 * available.
 */
static int __iucv_query_maxconn(void *param, unsigned long *max_pathid)
{
	register unsigned long reg0 asm ("0");
	register unsigned long reg1 asm ("1");
	int ccode;

	reg0 = IUCV_QUERY;
	reg1 = (unsigned long) param;
	asm volatile (
		"	.long	0xb2f01000\n"
		"	ipm	%0\n"
		"	srl	%0,28\n"
		: "=d" (ccode), "+d" (reg0), "+d" (reg1) : : "cc");
	*max_pathid = reg1;
	return ccode;
}

static int iucv_query_maxconn(void)
{
	unsigned long max_pathid;
	void *param;
	int ccode;

	param = kzalloc(sizeof(union iucv_param), GFP_KERNEL | GFP_DMA);
	if (!param)
		return -ENOMEM;
	ccode = __iucv_query_maxconn(param, &max_pathid);
	if (ccode == 0)
		iucv_max_pathid = max_pathid;
	kfree(param);
	return ccode ? -EPERM : 0;
}

/**
 * iucv_allow_cpu
 * @data: unused
 *
 * Allow iucv interrupts on this cpu.
 */
static void iucv_allow_cpu(void *data)
{
	int cpu = smp_processor_id();
	union iucv_param *parm;

	/*
	 * Enable all iucv interrupts.
	 * ipmask contains bits for the different interrupts
	 *	0x80 - Flag to allow nonpriority message pending interrupts
	 *	0x40 - Flag to allow priority message pending interrupts
	 *	0x20 - Flag to allow nonpriority message completion interrupts
	 *	0x10 - Flag to allow priority message completion interrupts
	 *	0x08 - Flag to allow IUCV control interrupts
	 */
	parm = iucv_param_irq[cpu];
	memset(parm, 0, sizeof(union iucv_param));
	parm->set_mask.ipmask = 0xf8;
	iucv_call_b2f0(IUCV_SETMASK, parm);

	/*
	 * Enable all iucv control interrupts.
	 * ipmask contains bits for the different interrupts
	 *	0x80 - Flag to allow pending connections interrupts
	 *	0x40 - Flag to allow connection complete interrupts
	 *	0x20 - Flag to allow connection severed interrupts
	 *	0x10 - Flag to allow connection quiesced interrupts
	 *	0x08 - Flag to allow connection resumed interrupts
	 */
	memset(parm, 0, sizeof(union iucv_param));
	parm->set_mask.ipmask = 0xf8;
	iucv_call_b2f0(IUCV_SETCONTROLMASK, parm);
	/* Set indication that iucv interrupts are allowed for this cpu. */
	cpumask_set_cpu(cpu, &iucv_irq_cpumask);
}

/**
 * iucv_block_cpu
 * @data: unused
 *
 * Block iucv interrupts on this cpu.
 */
static void iucv_block_cpu(void *data)
{
	int cpu = smp_processor_id();
	union iucv_param *parm;

	/* Disable all iucv interrupts. */
	parm = iucv_param_irq[cpu];
	memset(parm, 0, sizeof(union iucv_param));
	iucv_call_b2f0(IUCV_SETMASK, parm);

	/* Clear indication that iucv interrupts are allowed for this cpu. */
	cpumask_clear_cpu(cpu, &iucv_irq_cpumask);
}

/**
 * iucv_block_cpu_almost
 * @data: unused
 *
 * Allow connection-severed interrupts only on this cpu.
 */
static void iucv_block_cpu_almost(void *data)
{
	int cpu = smp_processor_id();
	union iucv_param *parm;

	/* Allow iucv control interrupts only */
	parm = iucv_param_irq[cpu];
	memset(parm, 0, sizeof(union iucv_param));
	parm->set_mask.ipmask = 0x08;
	iucv_call_b2f0(IUCV_SETMASK, parm);
	/* Allow iucv-severed interrupt only */
	memset(parm, 0, sizeof(union iucv_param));
	parm->set_mask.ipmask = 0x20;
	iucv_call_b2f0(IUCV_SETCONTROLMASK, parm);

	/* Clear indication that iucv interrupts are allowed for this cpu. */
	cpumask_clear_cpu(cpu, &iucv_irq_cpumask);
}

/**
 * iucv_declare_cpu
 * @data: unused
 *
 * Declare a interrupt buffer on this cpu.
 */
static void iucv_declare_cpu(void *data)
{
	int cpu = smp_processor_id();
	union iucv_param *parm;
	int rc;

	if (cpumask_test_cpu(cpu, &iucv_buffer_cpumask))
		return;

	/* Declare interrupt buffer. */
	parm = iucv_param_irq[cpu];
	memset(parm, 0, sizeof(union iucv_param));
	parm->db.ipbfadr1 = virt_to_phys(iucv_irq_data[cpu]);
	rc = iucv_call_b2f0(IUCV_DECLARE_BUFFER, parm);
	if (rc) {
		char *err = "Unknown";
		switch (rc) {
		case 0x03:
			err = "Directory error";
			break;
		case 0x0a:
			err = "Invalid length";
			break;
		case 0x13:
			err = "Buffer already exists";
			break;
		case 0x3e:
			err = "Buffer overlap";
			break;
		case 0x5c:
			err = "Paging or storage error";
			break;
		}
		pr_warn("Defining an interrupt buffer on CPU %i failed with 0x%02x (%s)\n",
			cpu, rc, err);
		return;
	}

	/* Set indication that an iucv buffer exists for this cpu. */
	cpumask_set_cpu(cpu, &iucv_buffer_cpumask);

	if (iucv_nonsmp_handler == 0 || cpumask_empty(&iucv_irq_cpumask))
		/* Enable iucv interrupts on this cpu. */
		iucv_allow_cpu(NULL);
	else
		/* Disable iucv interrupts on this cpu. */
		iucv_block_cpu(NULL);
}

/**
 * iucv_retrieve_cpu
 * @data: unused
 *
 * Retrieve interrupt buffer on this cpu.
 */
static void iucv_retrieve_cpu(void *data)
{
	int cpu = smp_processor_id();
	union iucv_param *parm;

	if (!cpumask_test_cpu(cpu, &iucv_buffer_cpumask))
		return;

	/* Block iucv interrupts. */
	iucv_block_cpu(NULL);

	/* Retrieve interrupt buffer. */
	parm = iucv_param_irq[cpu];
	iucv_call_b2f0(IUCV_RETRIEVE_BUFFER, parm);

	/* Clear indication that an iucv buffer exists for this cpu. */
	cpumask_clear_cpu(cpu, &iucv_buffer_cpumask);
}

/**
 * iucv_setmask_smp
 *
 * Allow iucv interrupts on all cpus.
 */
static void iucv_setmask_mp(void)
{
	int cpu;

	get_online_cpus();
	for_each_online_cpu(cpu)
		/* Enable all cpus with a declared buffer. */
		if (cpumask_test_cpu(cpu, &iucv_buffer_cpumask) &&
		    !cpumask_test_cpu(cpu, &iucv_irq_cpumask))
			smp_call_function_single(cpu, iucv_allow_cpu,
						 NULL, 1);
	put_online_cpus();
}

/**
 * iucv_setmask_up
 *
 * Allow iucv interrupts on a single cpu.
 */
static void iucv_setmask_up(void)
{
	cpumask_t cpumask;
	int cpu;

	/* Disable all cpu but the first in cpu_irq_cpumask. */
	cpumask_copy(&cpumask, &iucv_irq_cpumask);
	cpumask_clear_cpu(cpumask_first(&iucv_irq_cpumask), &cpumask);
	for_each_cpu(cpu, &cpumask)
		smp_call_function_single(cpu, iucv_block_cpu, NULL, 1);
}

/**
 * iucv_enable
 *
 * This function makes iucv ready for use. It allocates the pathid
 * table, declares an iucv interrupt buffer and enables the iucv
 * interrupts. Called when the first user has registered an iucv
 * handler.
 */
static int iucv_enable(void)
{
	size_t alloc_size;
	int cpu, rc;

	get_online_cpus();
	rc = -ENOMEM;
	alloc_size = iucv_max_pathid * sizeof(struct iucv_path);
	iucv_path_table = kzalloc(alloc_size, GFP_KERNEL);
	if (!iucv_path_table)
		goto out;
	/* Declare per cpu buffers. */
	rc = -EIO;
	for_each_online_cpu(cpu)
		smp_call_function_single(cpu, iucv_declare_cpu, NULL, 1);
	if (cpumask_empty(&iucv_buffer_cpumask))
		/* No cpu could declare an iucv buffer. */
		goto out;
	put_online_cpus();
	return 0;
out:
	kfree(iucv_path_table);
	iucv_path_table = NULL;
	put_online_cpus();
	return rc;
}

/**
 * iucv_disable
 *
 * This function shuts down iucv. It disables iucv interrupts, retrieves
 * the iucv interrupt buffer and frees the pathid table. Called after the
 * last user unregister its iucv handler.
 */
static void iucv_disable(void)
{
	get_online_cpus();
	on_each_cpu(iucv_retrieve_cpu, NULL, 1);
	kfree(iucv_path_table);
	iucv_path_table = NULL;
	put_online_cpus();
}

static int iucv_cpu_dead(unsigned int cpu)
{
	kfree(iucv_param_irq[cpu]);
	iucv_param_irq[cpu] = NULL;
	kfree(iucv_param[cpu]);
	iucv_param[cpu] = NULL;
	kfree(iucv_irq_data[cpu]);
	iucv_irq_data[cpu] = NULL;
	return 0;
}

static int iucv_cpu_prepare(unsigned int cpu)
{
	/* Note: GFP_DMA used to get memory below 2G */
	iucv_irq_data[cpu] = kmalloc_node(sizeof(struct iucv_irq_data),
			     GFP_KERNEL|GFP_DMA, cpu_to_node(cpu));
	if (!iucv_irq_data[cpu])
		goto out_free;

	/* Allocate parameter blocks. */
	iucv_param[cpu] = kmalloc_node(sizeof(union iucv_param),
			  GFP_KERNEL|GFP_DMA, cpu_to_node(cpu));
	if (!iucv_param[cpu])
		goto out_free;

	iucv_param_irq[cpu] = kmalloc_node(sizeof(union iucv_param),
			  GFP_KERNEL|GFP_DMA, cpu_to_node(cpu));
	if (!iucv_param_irq[cpu])
		goto out_free;

	return 0;

out_free:
	iucv_cpu_dead(cpu);
	return -ENOMEM;
}

static int iucv_cpu_online(unsigned int cpu)
{
	if (!iucv_path_table)
		return 0;
	iucv_declare_cpu(NULL);
	return 0;
}

static int iucv_cpu_down_prep(unsigned int cpu)
{
	cpumask_t cpumask;

	if (!iucv_path_table)
		return 0;

	cpumask_copy(&cpumask, &iucv_buffer_cpumask);
	cpumask_clear_cpu(cpu, &cpumask);
	if (cpumask_empty(&cpumask))
		/* Can't offline last IUCV enabled cpu. */
		return -EINVAL;

	iucv_retrieve_cpu(NULL);
	if (!cpumask_empty(&iucv_irq_cpumask))
		return 0;
	smp_call_function_single(cpumask_first(&iucv_buffer_cpumask),
				 iucv_allow_cpu, NULL, 1);
	return 0;
}

/**
 * iucv_sever_pathid
 * @pathid: path identification number.
 * @userdata: 16-bytes of user data.
 *
 * Sever an iucv path to free up the pathid. Used internally.
 */
static int iucv_sever_pathid(u16 pathid, u8 *userdata)
{
	union iucv_param *parm;

	parm = iucv_param_irq[smp_processor_id()];
	memset(parm, 0, sizeof(union iucv_param));
	if (userdata)
		memcpy(parm->ctrl.ipuser, userdata, sizeof(parm->ctrl.ipuser));
	parm->ctrl.ippathid = pathid;
	return iucv_call_b2f0(IUCV_SEVER, parm);
}

/**
 * __iucv_cleanup_queue
 * @dummy: unused dummy argument
 *
 * Nop function called via smp_call_function to force work items from
 * pending external iucv interrupts to the work queue.
 */
static void __iucv_cleanup_queue(void *dummy)
{
}

/**
 * iucv_cleanup_queue
 *
 * Function called after a path has been severed to find all remaining
 * work items for the now stale pathid. The caller needs to hold the
 * iucv_table_lock.
 */
static void iucv_cleanup_queue(void)
{
	struct iucv_irq_list *p, *n;

	/*
	 * When a path is severed, the pathid can be reused immediately
	 * on a iucv connect or a connection pending interrupt. Remove
	 * all entries from the task queue that refer to a stale pathid
	 * (iucv_path_table[ix] == NULL). Only then do the iucv connect
	 * or deliver the connection pending interrupt. To get all the
	 * pending interrupts force them to the work queue by calling
	 * an empty function on all cpus.
	 */
	smp_call_function(__iucv_cleanup_queue, NULL, 1);
	spin_lock_irq(&iucv_queue_lock);
	list_for_each_entry_safe(p, n, &iucv_task_queue, list) {
		/* Remove stale work items from the task queue. */
		if (iucv_path_table[p->data.ippathid] == NULL) {
			list_del(&p->list);
			kfree(p);
		}
	}
	spin_unlock_irq(&iucv_queue_lock);
}

/**
 * iucv_register:
 * @handler: address of iucv handler structure
 * @smp: != 0 indicates that the handler can deal with out of order messages
 *
 * Registers a driver with IUCV.
 *
 * Returns 0 on success, -ENOMEM if the memory allocation for the pathid
 * table failed, or -EIO if IUCV_DECLARE_BUFFER failed on all cpus.
 */
int iucv_register(struct iucv_handler *handler, int smp)
{
	int rc;

	if (!iucv_available)
		return -ENOSYS;
	mutex_lock(&iucv_register_mutex);
	if (!smp)
		iucv_nonsmp_handler++;
	if (list_empty(&iucv_handler_list)) {
		rc = iucv_enable();
		if (rc)
			goto out_mutex;
	} else if (!smp && iucv_nonsmp_handler == 1)
		iucv_setmask_up();
	INIT_LIST_HEAD(&handler->paths);

	spin_lock_bh(&iucv_table_lock);
	list_add_tail(&handler->list, &iucv_handler_list);
	spin_unlock_bh(&iucv_table_lock);
	rc = 0;
out_mutex:
	mutex_unlock(&iucv_register_mutex);
	return rc;
}
EXPORT_SYMBOL(iucv_register);

/**
 * iucv_unregister
 * @handler:  address of iucv handler structure
 * @smp: != 0 indicates that the handler can deal with out of order messages
 *
 * Unregister driver from IUCV.
 */
void iucv_unregister(struct iucv_handler *handler, int smp)
{
	struct iucv_path *p, *n;

	mutex_lock(&iucv_register_mutex);
	spin_lock_bh(&iucv_table_lock);
	/* Remove handler from the iucv_handler_list. */
	list_del_init(&handler->list);
	/* Sever all pathids still referring to the handler. */
	list_for_each_entry_safe(p, n, &handler->paths, list) {
		iucv_sever_pathid(p->pathid, NULL);
		iucv_path_table[p->pathid] = NULL;
		list_del(&p->list);
		iucv_path_free(p);
	}
	spin_unlock_bh(&iucv_table_lock);
	if (!smp)
		iucv_nonsmp_handler--;
	if (list_empty(&iucv_handler_list))
		iucv_disable();
	else if (!smp && iucv_nonsmp_handler == 0)
		iucv_setmask_mp();
	mutex_unlock(&iucv_register_mutex);
}
EXPORT_SYMBOL(iucv_unregister);

static int iucv_reboot_event(struct notifier_block *this,
			     unsigned long event, void *ptr)
{
	int i;

	if (cpumask_empty(&iucv_irq_cpumask))
		return NOTIFY_DONE;

	get_online_cpus();
	on_each_cpu_mask(&iucv_irq_cpumask, iucv_block_cpu, NULL, 1);
	preempt_disable();
	for (i = 0; i < iucv_max_pathid; i++) {
		if (iucv_path_table[i])
			iucv_sever_pathid(i, NULL);
	}
	preempt_enable();
	put_online_cpus();
	iucv_disable();
	return NOTIFY_DONE;
}

static struct notifier_block iucv_reboot_notifier = {
	.notifier_call = iucv_reboot_event,
};

/**
 * iucv_path_accept
 * @path: address of iucv path structure
 * @handler: address of iucv handler structure
 * @userdata: 16 bytes of data reflected to the communication partner
 * @private: private data passed to interrupt handlers for this path
 *
 * This function is issued after the user received a connection pending
 * external interrupt and now wishes to complete the IUCV communication path.
 *
 * Returns the result of the CP IUCV call.
 */
int iucv_path_accept(struct iucv_path *path, struct iucv_handler *handler,
		     u8 *userdata, void *private)
{
	union iucv_param *parm;
	int rc;

	local_bh_disable();
	if (cpumask_empty(&iucv_buffer_cpumask)) {
		rc = -EIO;
		goto out;
	}
	/* Prepare parameter block. */
	parm = iucv_param[smp_processor_id()];
	memset(parm, 0, sizeof(union iucv_param));
	parm->ctrl.ippathid = path->pathid;
	parm->ctrl.ipmsglim = path->msglim;
	if (userdata)
		memcpy(parm->ctrl.ipuser, userdata, sizeof(parm->ctrl.ipuser));
	parm->ctrl.ipflags1 = path->flags;

	rc = iucv_call_b2f0(IUCV_ACCEPT, parm);
	if (!rc) {
		path->private = private;
		path->msglim = parm->ctrl.ipmsglim;
		path->flags = parm->ctrl.ipflags1;
	}
out:
	local_bh_enable();
	return rc;
}
EXPORT_SYMBOL(iucv_path_accept);

/**
 * iucv_path_connect
 * @path: address of iucv path structure
 * @handler: address of iucv handler structure
 * @userid: 8-byte user identification
 * @system: 8-byte target system identification
 * @userdata: 16 bytes of data reflected to the communication partner
 * @private: private data passed to interrupt handlers for this path
 *
 * This function establishes an IUCV path. Although the connect may complete
 * successfully, you are not able to use the path until you receive an IUCV
 * Connection Complete external interrupt.
 *
 * Returns the result of the CP IUCV call.
 */
int iucv_path_connect(struct iucv_path *path, struct iucv_handler *handler,
		      u8 *userid, u8 *system, u8 *userdata,
		      void *private)
{
	union iucv_param *parm;
	int rc;

	spin_lock_bh(&iucv_table_lock);
	iucv_cleanup_queue();
	if (cpumask_empty(&iucv_buffer_cpumask)) {
		rc = -EIO;
		goto out;
	}
	parm = iucv_param[smp_processor_id()];
	memset(parm, 0, sizeof(union iucv_param));
	parm->ctrl.ipmsglim = path->msglim;
	parm->ctrl.ipflags1 = path->flags;
	if (userid) {
		memcpy(parm->ctrl.ipvmid, userid, sizeof(parm->ctrl.ipvmid));
		ASCEBC(parm->ctrl.ipvmid, sizeof(parm->ctrl.ipvmid));
		EBC_TOUPPER(parm->ctrl.ipvmid, sizeof(parm->ctrl.ipvmid));
	}
	if (system) {
		memcpy(parm->ctrl.iptarget, system,
		       sizeof(parm->ctrl.iptarget));
		ASCEBC(parm->ctrl.iptarget, sizeof(parm->ctrl.iptarget));
		EBC_TOUPPER(parm->ctrl.iptarget, sizeof(parm->ctrl.iptarget));
	}
	if (userdata)
		memcpy(parm->ctrl.ipuser, userdata, sizeof(parm->ctrl.ipuser));

	rc = iucv_call_b2f0(IUCV_CONNECT, parm);
	if (!rc) {
		if (parm->ctrl.ippathid < iucv_max_pathid) {
			path->pathid = parm->ctrl.ippathid;
			path->msglim = parm->ctrl.ipmsglim;
			path->flags = parm->ctrl.ipflags1;
			path->handler = handler;
			path->private = private;
			list_add_tail(&path->list, &handler->paths);
			iucv_path_table[path->pathid] = path;
		} else {
			iucv_sever_pathid(parm->ctrl.ippathid,
					  iucv_error_pathid);
			rc = -EIO;
		}
	}
out:
	spin_unlock_bh(&iucv_table_lock);
	return rc;
}
EXPORT_SYMBOL(iucv_path_connect);

/**
 * iucv_path_quiesce:
 * @path: address of iucv path structure
 * @userdata: 16 bytes of data reflected to the communication partner
 *
 * This function temporarily suspends incoming messages on an IUCV path.
 * You can later reactivate the path by invoking the iucv_resume function.
 *
 * Returns the result from the CP IUCV call.
 */
int iucv_path_quiesce(struct iucv_path *path, u8 *userdata)
{
	union iucv_param *parm;
	int rc;

	local_bh_disable();
	if (cpumask_empty(&iucv_buffer_cpumask)) {
		rc = -EIO;
		goto out;
	}
	parm = iucv_param[smp_processor_id()];
	memset(parm, 0, sizeof(union iucv_param));
	if (userdata)
		memcpy(parm->ctrl.ipuser, userdata, sizeof(parm->ctrl.ipuser));
	parm->ctrl.ippathid = path->pathid;
	rc = iucv_call_b2f0(IUCV_QUIESCE, parm);
out:
	local_bh_enable();
	return rc;
}
EXPORT_SYMBOL(iucv_path_quiesce);

/**
 * iucv_path_resume:
 * @path: address of iucv path structure
 * @userdata: 16 bytes of data reflected to the communication partner
 *
 * This function resumes incoming messages on an IUCV path that has
 * been stopped with iucv_path_quiesce.
 *
 * Returns the result from the CP IUCV call.
 */
int iucv_path_resume(struct iucv_path *path, u8 *userdata)
{
	union iucv_param *parm;
	int rc;

	local_bh_disable();
	if (cpumask_empty(&iucv_buffer_cpumask)) {
		rc = -EIO;
		goto out;
	}
	parm = iucv_param[smp_processor_id()];
	memset(parm, 0, sizeof(union iucv_param));
	if (userdata)
		memcpy(parm->ctrl.ipuser, userdata, sizeof(parm->ctrl.ipuser));
	parm->ctrl.ippathid = path->pathid;
	rc = iucv_call_b2f0(IUCV_RESUME, parm);
out:
	local_bh_enable();
	return rc;
}

/**
 * iucv_path_sever
 * @path: address of iucv path structure
 * @userdata: 16 bytes of data reflected to the communication partner
 *
 * This function terminates an IUCV path.
 *
 * Returns the result from the CP IUCV call.
 */
int iucv_path_sever(struct iucv_path *path, u8 *userdata)
{
	int rc;

	preempt_disable();
	if (cpumask_empty(&iucv_buffer_cpumask)) {
		rc = -EIO;
		goto out;
	}
	if (iucv_active_cpu != smp_processor_id())
		spin_lock_bh(&iucv_table_lock);
	rc = iucv_sever_pathid(path->pathid, userdata);
	iucv_path_table[path->pathid] = NULL;
	list_del_init(&path->list);
	if (iucv_active_cpu != smp_processor_id())
		spin_unlock_bh(&iucv_table_lock);
out:
	preempt_enable();
	return rc;
}
EXPORT_SYMBOL(iucv_path_sever);

/**
 * iucv_message_purge
 * @path: address of iucv path structure
 * @msg: address of iucv msg structure
 * @srccls: source class of message
 *
 * Cancels a message you have sent.
 *
 * Returns the result from the CP IUCV call.
 */
int iucv_message_purge(struct iucv_path *path, struct iucv_message *msg,
		       u32 srccls)
{
	union iucv_param *parm;
	int rc;

	local_bh_disable();
	if (cpumask_empty(&iucv_buffer_cpumask)) {
		rc = -EIO;
		goto out;
	}
	parm = iucv_param[smp_processor_id()];
	memset(parm, 0, sizeof(union iucv_param));
	parm->purge.ippathid = path->pathid;
	parm->purge.ipmsgid = msg->id;
	parm->purge.ipsrccls = srccls;
	parm->purge.ipflags1 = IUCV_IPSRCCLS | IUCV_IPFGMID | IUCV_IPFGPID;
	rc = iucv_call_b2f0(IUCV_PURGE, parm);
	if (!rc) {
		msg->audit = (*(u32 *) &parm->purge.ipaudit) >> 8;
		msg->tag = parm->purge.ipmsgtag;
	}
out:
	local_bh_enable();
	return rc;
}
EXPORT_SYMBOL(iucv_message_purge);

/**
 * iucv_message_receive_iprmdata
 * @path: address of iucv path structure
 * @msg: address of iucv msg structure
 * @flags: how the message is received (IUCV_IPBUFLST)
 * @buffer: address of data buffer or address of struct iucv_array
 * @size: length of data buffer
 * @residual:
 *
 * Internal function used by iucv_message_receive and __iucv_message_receive
 * to receive RMDATA data stored in struct iucv_message.
 */
static int iucv_message_receive_iprmdata(struct iucv_path *path,
					 struct iucv_message *msg,
					 u8 flags, void *buffer,
					 size_t size, size_t *residual)
{
	struct iucv_array *array;
	u8 *rmmsg;
	size_t copy;

	/*
	 * Message is 8 bytes long and has been stored to the
	 * message descriptor itself.
	 */
	if (residual)
		*residual = abs(size - 8);
	rmmsg = msg->rmmsg;
	if (flags & IUCV_IPBUFLST) {
		/* Copy to struct iucv_array. */
		size = (size < 8) ? size : 8;
		for (array = buffer; size > 0; array++) {
			copy = min_t(size_t, size, array->length);
			memcpy((u8 *)(addr_t) array->address,
				rmmsg, copy);
			rmmsg += copy;
			size -= copy;
		}
	} else {
		/* Copy to direct buffer. */
		memcpy(buffer, rmmsg, min_t(size_t, size, 8));
	}
	return 0;
}

/**
 * __iucv_message_receive
 * @path: address of iucv path structure
 * @msg: address of iucv msg structure
 * @flags: how the message is received (IUCV_IPBUFLST)
 * @buffer: address of data buffer or address of struct iucv_array
 * @size: length of data buffer
 * @residual:
 *
 * This function receives messages that are being sent to you over
 * established paths. This function will deal with RMDATA messages
 * embedded in struct iucv_message as well.
 *
 * Locking:	no locking
 *
 * Returns the result from the CP IUCV call.
 */
int __iucv_message_receive(struct iucv_path *path, struct iucv_message *msg,
			   u8 flags, void *buffer, size_t size, size_t *residual)
{
	union iucv_param *parm;
	int rc;

	if (msg->flags & IUCV_IPRMDATA)
		return iucv_message_receive_iprmdata(path, msg, flags,
						     buffer, size, residual);
	 if (cpumask_empty(&iucv_buffer_cpumask)) {
		rc = -EIO;
		goto out;
	}
	parm = iucv_param[smp_processor_id()];
	memset(parm, 0, sizeof(union iucv_param));
	parm->db.ipbfadr1 = (u32)(addr_t) buffer;
	parm->db.ipbfln1f = (u32) size;
	parm->db.ipmsgid = msg->id;
	parm->db.ippathid = path->pathid;
	parm->db.iptrgcls = msg->class;
	parm->db.ipflags1 = (flags | IUCV_IPFGPID |
			     IUCV_IPFGMID | IUCV_IPTRGCLS);
	rc = iucv_call_b2f0(IUCV_RECEIVE, parm);
	if (!rc || rc == 5) {
		msg->flags = parm->db.ipflags1;
		if (residual)
			*residual = parm->db.ipbfln1f;
	}
out:
	return rc;
}
EXPORT_SYMBOL(__iucv_message_receive);

/**
 * iucv_message_receive
 * @path: address of iucv path structure
 * @msg: address of iucv msg structure
 * @flags: how the message is received (IUCV_IPBUFLST)
 * @buffer: address of data buffer or address of struct iucv_array
 * @size: length of data buffer
 * @residual:
 *
 * This function receives messages that are being sent to you over
 * established paths. This function will deal with RMDATA messages
 * embedded in struct iucv_message as well.
 *
 * Locking:	local_bh_enable/local_bh_disable
 *
 * Returns the result from the CP IUCV call.
 */
int iucv_message_receive(struct iucv_path *path, struct iucv_message *msg,
			 u8 flags, void *buffer, size_t size, size_t *residual)
{
	int rc;

	if (msg->flags & IUCV_IPRMDATA)
		return iucv_message_receive_iprmdata(path, msg, flags,
						     buffer, size, residual);
	local_bh_disable();
	rc = __iucv_message_receive(path, msg, flags, buffer, size, residual);
	local_bh_enable();
	return rc;
}
EXPORT_SYMBOL(iucv_message_receive);

/**
 * iucv_message_reject
 * @path: address of iucv path structure
 * @msg: address of iucv msg structure
 *
 * The reject function refuses a specified message. Between the time you
 * are notified of a message and the time that you complete the message,
 * the message may be rejected.
 *
 * Returns the result from the CP IUCV call.
 */
int iucv_message_reject(struct iucv_path *path, struct iucv_message *msg)
{
	union iucv_param *parm;
	int rc;

	local_bh_disable();
	if (cpumask_empty(&iucv_buffer_cpumask)) {
		rc = -EIO;
		goto out;
	}
	parm = iucv_param[smp_processor_id()];
	memset(parm, 0, sizeof(union iucv_param));
	parm->db.ippathid = path->pathid;
	parm->db.ipmsgid = msg->id;
	parm->db.iptrgcls = msg->class;
	parm->db.ipflags1 = (IUCV_IPTRGCLS | IUCV_IPFGMID | IUCV_IPFGPID);
	rc = iucv_call_b2f0(IUCV_REJECT, parm);
out:
	local_bh_enable();
	return rc;
}
EXPORT_SYMBOL(iucv_message_reject);

/**
 * iucv_message_reply
 * @path: address of iucv path structure
 * @msg: address of iucv msg structure
 * @flags: how the reply is sent (IUCV_IPRMDATA, IUCV_IPPRTY, IUCV_IPBUFLST)
 * @reply: address of reply data buffer or address of struct iucv_array
 * @size: length of reply data buffer
 *
 * This function responds to the two-way messages that you receive. You
 * must identify completely the message to which you wish to reply. ie,
 * pathid, msgid, and trgcls. Prmmsg signifies the data is moved into
 * the parameter list.
 *
 * Returns the result from the CP IUCV call.
 */
int iucv_message_reply(struct iucv_path *path, struct iucv_message *msg,
		       u8 flags, void *reply, size_t size)
{
	union iucv_param *parm;
	int rc;

	local_bh_disable();
	if (cpumask_empty(&iucv_buffer_cpumask)) {
		rc = -EIO;
		goto out;
	}
	parm = iucv_param[smp_processor_id()];
	memset(parm, 0, sizeof(union iucv_param));
	if (flags & IUCV_IPRMDATA) {
		parm->dpl.ippathid = path->pathid;
		parm->dpl.ipflags1 = flags;
		parm->dpl.ipmsgid = msg->id;
		parm->dpl.iptrgcls = msg->class;
		memcpy(parm->dpl.iprmmsg, reply, min_t(size_t, size, 8));
	} else {
		parm->db.ipbfadr1 = (u32)(addr_t) reply;
		parm->db.ipbfln1f = (u32) size;
		parm->db.ippathid = path->pathid;
		parm->db.ipflags1 = flags;
		parm->db.ipmsgid = msg->id;
		parm->db.iptrgcls = msg->class;
	}
	rc = iucv_call_b2f0(IUCV_REPLY, parm);
out:
	local_bh_enable();
	return rc;
}
EXPORT_SYMBOL(iucv_message_reply);

/**
 * __iucv_message_send
 * @path: address of iucv path structure
 * @msg: address of iucv msg structure
 * @flags: how the message is sent (IUCV_IPRMDATA, IUCV_IPPRTY, IUCV_IPBUFLST)
 * @srccls: source class of message
 * @buffer: address of send buffer or address of struct iucv_array
 * @size: length of send buffer
 *
 * This function transmits data to another application. Data to be
 * transmitted is in a buffer and this is a one-way message and the
 * receiver will not reply to the message.
 *
 * Locking:	no locking
 *
 * Returns the result from the CP IUCV call.
 */
int __iucv_message_send(struct iucv_path *path, struct iucv_message *msg,
		      u8 flags, u32 srccls, void *buffer, size_t size)
{
	union iucv_param *parm;
	int rc;

	if (cpumask_empty(&iucv_buffer_cpumask)) {
		rc = -EIO;
		goto out;
	}
	parm = iucv_param[smp_processor_id()];
	memset(parm, 0, sizeof(union iucv_param));
	if (flags & IUCV_IPRMDATA) {
		/* Message of 8 bytes can be placed into the parameter list. */
		parm->dpl.ippathid = path->pathid;
		parm->dpl.ipflags1 = flags | IUCV_IPNORPY;
		parm->dpl.iptrgcls = msg->class;
		parm->dpl.ipsrccls = srccls;
		parm->dpl.ipmsgtag = msg->tag;
		memcpy(parm->dpl.iprmmsg, buffer, 8);
	} else {
		parm->db.ipbfadr1 = (u32)(addr_t) buffer;
		parm->db.ipbfln1f = (u32) size;
		parm->db.ippathid = path->pathid;
		parm->db.ipflags1 = flags | IUCV_IPNORPY;
		parm->db.iptrgcls = msg->class;
		parm->db.ipsrccls = srccls;
		parm->db.ipmsgtag = msg->tag;
	}
	rc = iucv_call_b2f0(IUCV_SEND, parm);
	if (!rc)
		msg->id = parm->db.ipmsgid;
out:
	return rc;
}
EXPORT_SYMBOL(__iucv_message_send);

/**
 * iucv_message_send
 * @path: address of iucv path structure
 * @msg: address of iucv msg structure
 * @flags: how the message is sent (IUCV_IPRMDATA, IUCV_IPPRTY, IUCV_IPBUFLST)
 * @srccls: source class of message
 * @buffer: address of send buffer or address of struct iucv_array
 * @size: length of send buffer
 *
 * This function transmits data to another application. Data to be
 * transmitted is in a buffer and this is a one-way message and the
 * receiver will not reply to the message.
 *
 * Locking:	local_bh_enable/local_bh_disable
 *
 * Returns the result from the CP IUCV call.
 */
int iucv_message_send(struct iucv_path *path, struct iucv_message *msg,
		      u8 flags, u32 srccls, void *buffer, size_t size)
{
	int rc;

	local_bh_disable();
	rc = __iucv_message_send(path, msg, flags, srccls, buffer, size);
	local_bh_enable();
	return rc;
}
EXPORT_SYMBOL(iucv_message_send);

/**
 * iucv_message_send2way
 * @path: address of iucv path structure
 * @msg: address of iucv msg structure
 * @flags: how the message is sent and the reply is received
 *	   (IUCV_IPRMDATA, IUCV_IPBUFLST, IUCV_IPPRTY, IUCV_ANSLST)
 * @srccls: source class of message
 * @buffer: address of send buffer or address of struct iucv_array
 * @size: length of send buffer
 * @ansbuf: address of answer buffer or address of struct iucv_array
 * @asize: size of reply buffer
 *
 * This function transmits data to another application. Data to be
 * transmitted is in a buffer. The receiver of the send is expected to
 * reply to the message and a buffer is provided into which IUCV moves
 * the reply to this message.
 *
 * Returns the result from the CP IUCV call.
 */
int iucv_message_send2way(struct iucv_path *path, struct iucv_message *msg,
			  u8 flags, u32 srccls, void *buffer, size_t size,
			  void *answer, size_t asize, size_t *residual)
{
	union iucv_param *parm;
	int rc;

	local_bh_disable();
	if (cpumask_empty(&iucv_buffer_cpumask)) {
		rc = -EIO;
		goto out;
	}
	parm = iucv_param[smp_processor_id()];
	memset(parm, 0, sizeof(union iucv_param));
	if (flags & IUCV_IPRMDATA) {
		parm->dpl.ippathid = path->pathid;
		parm->dpl.ipflags1 = path->flags;	/* priority message */
		parm->dpl.iptrgcls = msg->class;
		parm->dpl.ipsrccls = srccls;
		parm->dpl.ipmsgtag = msg->tag;
		parm->dpl.ipbfadr2 = (u32)(addr_t) answer;
		parm->dpl.ipbfln2f = (u32) asize;
		memcpy(parm->dpl.iprmmsg, buffer, 8);
	} else {
		parm->db.ippathid = path->pathid;
		parm->db.ipflags1 = path->flags;	/* priority message */
		parm->db.iptrgcls = msg->class;
		parm->db.ipsrccls = srccls;
		parm->db.ipmsgtag = msg->tag;
		parm->db.ipbfadr1 = (u32)(addr_t) buffer;
		parm->db.ipbfln1f = (u32) size;
		parm->db.ipbfadr2 = (u32)(addr_t) answer;
		parm->db.ipbfln2f = (u32) asize;
	}
	rc = iucv_call_b2f0(IUCV_SEND, parm);
	if (!rc)
		msg->id = parm->db.ipmsgid;
out:
	local_bh_enable();
	return rc;
}
EXPORT_SYMBOL(iucv_message_send2way);

/**
 * iucv_path_pending
 * @data: Pointer to external interrupt buffer
 *
 * Process connection pending work item. Called from tasklet while holding
 * iucv_table_lock.
 */
struct iucv_path_pending {
	u16 ippathid;
	u8  ipflags1;
	u8  iptype;
	u16 ipmsglim;
	u16 res1;
	u8  ipvmid[8];
	u8  ipuser[16];
	u32 res3;
	u8  ippollfg;
	u8  res4[3];
} __packed;

static void iucv_path_pending(struct iucv_irq_data *data)
{
	struct iucv_path_pending *ipp = (void *) data;
	struct iucv_handler *handler;
	struct iucv_path *path;
	char *error;

	BUG_ON(iucv_path_table[ipp->ippathid]);
	/* New pathid, handler found. Create a new path struct. */
	error = iucv_error_no_memory;
	path = iucv_path_alloc(ipp->ipmsglim, ipp->ipflags1, GFP_ATOMIC);
	if (!path)
		goto out_sever;
	path->pathid = ipp->ippathid;
	iucv_path_table[path->pathid] = path;
	EBCASC(ipp->ipvmid, 8);

	/* Call registered handler until one is found that wants the path. */
	list_for_each_entry(handler, &iucv_handler_list, list) {
		if (!handler->path_pending)
			continue;
		/*
		 * Add path to handler to allow a call to iucv_path_sever
		 * inside the path_pending function. If the handler returns
		 * an error remove the path from the handler again.
		 */
		list_add(&path->list, &handler->paths);
		path->handler = handler;
		if (!handler->path_pending(path, ipp->ipvmid, ipp->ipuser))
			return;
		list_del(&path->list);
		path->handler = NULL;
	}
	/* No handler wanted the path. */
	iucv_path_table[path->pathid] = NULL;
	iucv_path_free(path);
	error = iucv_error_no_listener;
out_sever:
	iucv_sever_pathid(ipp->ippathid, error);
}

/**
 * iucv_path_complete
 * @data: Pointer to external interrupt buffer
 *
 * Process connection complete work item. Called from tasklet while holding
 * iucv_table_lock.
 */
struct iucv_path_complete {
	u16 ippathid;
	u8  ipflags1;
	u8  iptype;
	u16 ipmsglim;
	u16 res1;
	u8  res2[8];
	u8  ipuser[16];
	u32 res3;
	u8  ippollfg;
	u8  res4[3];
} __packed;

static void iucv_path_complete(struct iucv_irq_data *data)
{
	struct iucv_path_complete *ipc = (void *) data;
	struct iucv_path *path = iucv_path_table[ipc->ippathid];

	if (path)
		path->flags = ipc->ipflags1;
	if (path && path->handler && path->handler->path_complete)
		path->handler->path_complete(path, ipc->ipuser);
}

/**
 * iucv_path_severed
 * @data: Pointer to external interrupt buffer
 *
 * Process connection severed work item. Called from tasklet while holding
 * iucv_table_lock.
 */
struct iucv_path_severed {
	u16 ippathid;
	u8  res1;
	u8  iptype;
	u32 res2;
	u8  res3[8];
	u8  ipuser[16];
	u32 res4;
	u8  ippollfg;
	u8  res5[3];
} __packed;

static void iucv_path_severed(struct iucv_irq_data *data)
{
	struct iucv_path_severed *ips = (void *) data;
	struct iucv_path *path = iucv_path_table[ips->ippathid];

	if (!path || !path->handler)	/* Already severed */
		return;
	if (path->handler->path_severed)
		path->handler->path_severed(path, ips->ipuser);
	else {
		iucv_sever_pathid(path->pathid, NULL);
		iucv_path_table[path->pathid] = NULL;
		list_del(&path->list);
		iucv_path_free(path);
	}
}

/**
 * iucv_path_quiesced
 * @data: Pointer to external interrupt buffer
 *
 * Process connection quiesced work item. Called from tasklet while holding
 * iucv_table_lock.
 */
struct iucv_path_quiesced {
	u16 ippathid;
	u8  res1;
	u8  iptype;
	u32 res2;
	u8  res3[8];
	u8  ipuser[16];
	u32 res4;
	u8  ippollfg;
	u8  res5[3];
} __packed;

static void iucv_path_quiesced(struct iucv_irq_data *data)
{
	struct iucv_path_quiesced *ipq = (void *) data;
	struct iucv_path *path = iucv_path_table[ipq->ippathid];

	if (path && path->handler && path->handler->path_quiesced)
		path->handler->path_quiesced(path, ipq->ipuser);
}

/**
 * iucv_path_resumed
 * @data: Pointer to external interrupt buffer
 *
 * Process connection resumed work item. Called from tasklet while holding
 * iucv_table_lock.
 */
struct iucv_path_resumed {
	u16 ippathid;
	u8  res1;
	u8  iptype;
	u32 res2;
	u8  res3[8];
	u8  ipuser[16];
	u32 res4;
	u8  ippollfg;
	u8  res5[3];
} __packed;

static void iucv_path_resumed(struct iucv_irq_data *data)
{
	struct iucv_path_resumed *ipr = (void *) data;
	struct iucv_path *path = iucv_path_table[ipr->ippathid];

	if (path && path->handler && path->handler->path_resumed)
		path->handler->path_resumed(path, ipr->ipuser);
}

/**
 * iucv_message_complete
 * @data: Pointer to external interrupt buffer
 *
 * Process message complete work item. Called from tasklet while holding
 * iucv_table_lock.
 */
struct iucv_message_complete {
	u16 ippathid;
	u8  ipflags1;
	u8  iptype;
	u32 ipmsgid;
	u32 ipaudit;
	u8  iprmmsg[8];
	u32 ipsrccls;
	u32 ipmsgtag;
	u32 res;
	u32 ipbfln2f;
	u8  ippollfg;
	u8  res2[3];
} __packed;

static void iucv_message_complete(struct iucv_irq_data *data)
{
	struct iucv_message_complete *imc = (void *) data;
	struct iucv_path *path = iucv_path_table[imc->ippathid];
	struct iucv_message msg;

	if (path && path->handler && path->handler->message_complete) {
		msg.flags = imc->ipflags1;
		msg.id = imc->ipmsgid;
		msg.audit = imc->ipaudit;
		memcpy(msg.rmmsg, imc->iprmmsg, 8);
		msg.class = imc->ipsrccls;
		msg.tag = imc->ipmsgtag;
		msg.length = imc->ipbfln2f;
		path->handler->message_complete(path, &msg);
	}
}

/**
 * iucv_message_pending
 * @data: Pointer to external interrupt buffer
 *
 * Process message pending work item. Called from tasklet while holding
 * iucv_table_lock.
 */
struct iucv_message_pending {
	u16 ippathid;
	u8  ipflags1;
	u8  iptype;
	u32 ipmsgid;
	u32 iptrgcls;
	union {
		u32 iprmmsg1_u32;
		u8  iprmmsg1[4];
	} ln1msg1;
	union {
		u32 ipbfln1f;
		u8  iprmmsg2[4];
	} ln1msg2;
	u32 res1[3];
	u32 ipbfln2f;
	u8  ippollfg;
	u8  res2[3];
} __packed;

static void iucv_message_pending(struct iucv_irq_data *data)
{
	struct iucv_message_pending *imp = (void *) data;
	struct iucv_path *path = iucv_path_table[imp->ippathid];
	struct iucv_message msg;

	if (path && path->handler && path->handler->message_pending) {
		msg.flags = imp->ipflags1;
		msg.id = imp->ipmsgid;
		msg.class = imp->iptrgcls;
		if (imp->ipflags1 & IUCV_IPRMDATA) {
			memcpy(msg.rmmsg, imp->ln1msg1.iprmmsg1, 8);
			msg.length = 8;
		} else
			msg.length = imp->ln1msg2.ipbfln1f;
		msg.reply_size = imp->ipbfln2f;
		path->handler->message_pending(path, &msg);
	}
}

/**
 * iucv_tasklet_fn:
 *
 * This tasklet loops over the queue of irq buffers created by
 * iucv_external_interrupt, calls the appropriate action handler
 * and then frees the buffer.
 */
static void iucv_tasklet_fn(unsigned long ignored)
{
	typedef void iucv_irq_fn(struct iucv_irq_data *);
	static iucv_irq_fn *irq_fn[] = {
		[0x02] = iucv_path_complete,
		[0x03] = iucv_path_severed,
		[0x04] = iucv_path_quiesced,
		[0x05] = iucv_path_resumed,
		[0x06] = iucv_message_complete,
		[0x07] = iucv_message_complete,
		[0x08] = iucv_message_pending,
		[0x09] = iucv_message_pending,
	};
	LIST_HEAD(task_queue);
	struct iucv_irq_list *p, *n;

	/* Serialize tasklet, iucv_path_sever and iucv_path_connect. */
	if (!spin_trylock(&iucv_table_lock)) {
		tasklet_schedule(&iucv_tasklet);
		return;
	}
	iucv_active_cpu = smp_processor_id();

	spin_lock_irq(&iucv_queue_lock);
	list_splice_init(&iucv_task_queue, &task_queue);
	spin_unlock_irq(&iucv_queue_lock);

	list_for_each_entry_safe(p, n, &task_queue, list) {
		list_del_init(&p->list);
		irq_fn[p->data.iptype](&p->data);
		kfree(p);
	}

	iucv_active_cpu = -1;
	spin_unlock(&iucv_table_lock);
}

/**
 * iucv_work_fn:
 *
 * This work function loops over the queue of path pending irq blocks
 * created by iucv_external_interrupt, calls the appropriate action
 * handler and then frees the buffer.
 */
static void iucv_work_fn(struct work_struct *work)
{
	LIST_HEAD(work_queue);
	struct iucv_irq_list *p, *n;

	/* Serialize tasklet, iucv_path_sever and iucv_path_connect. */
	spin_lock_bh(&iucv_table_lock);
	iucv_active_cpu = smp_processor_id();

	spin_lock_irq(&iucv_queue_lock);
	list_splice_init(&iucv_work_queue, &work_queue);
	spin_unlock_irq(&iucv_queue_lock);

	iucv_cleanup_queue();
	list_for_each_entry_safe(p, n, &work_queue, list) {
		list_del_init(&p->list);
		iucv_path_pending(&p->data);
		kfree(p);
	}

	iucv_active_cpu = -1;
	spin_unlock_bh(&iucv_table_lock);
}

/**
 * iucv_external_interrupt
 * @code: irq code
 *
 * Handles external interrupts coming in from CP.
 * Places the interrupt buffer on a queue and schedules iucv_tasklet_fn().
 */
static void iucv_external_interrupt(struct ext_code ext_code,
				    unsigned int param32, unsigned long param64)
{
	struct iucv_irq_data *p;
	struct iucv_irq_list *work;

	inc_irq_stat(IRQEXT_IUC);
	p = iucv_irq_data[smp_processor_id()];
	if (p->ippathid >= iucv_max_pathid) {
		WARN_ON(p->ippathid >= iucv_max_pathid);
		iucv_sever_pathid(p->ippathid, iucv_error_no_listener);
		return;
	}
	BUG_ON(p->iptype  < 0x01 || p->iptype > 0x09);
	work = kmalloc(sizeof(struct iucv_irq_list), GFP_ATOMIC);
	if (!work) {
		pr_warn("iucv_external_interrupt: out of memory\n");
		return;
	}
	memcpy(&work->data, p, sizeof(work->data));
	spin_lock(&iucv_queue_lock);
	if (p->iptype == 0x01) {
		/* Path pending interrupt. */
		list_add_tail(&work->list, &iucv_work_queue);
		schedule_work(&iucv_work);
	} else {
		/* The other interrupts. */
		list_add_tail(&work->list, &iucv_task_queue);
		tasklet_schedule(&iucv_tasklet);
	}
	spin_unlock(&iucv_queue_lock);
}

static int iucv_pm_prepare(struct device *dev)
{
	int rc = 0;

#ifdef CONFIG_PM_DEBUG
	printk(KERN_INFO "iucv_pm_prepare\n");
#endif
	if (dev->driver && dev->driver->pm && dev->driver->pm->prepare)
		rc = dev->driver->pm->prepare(dev);
	return rc;
}

static void iucv_pm_complete(struct device *dev)
{
#ifdef CONFIG_PM_DEBUG
	printk(KERN_INFO "iucv_pm_complete\n");
#endif
	if (dev->driver && dev->driver->pm && dev->driver->pm->complete)
		dev->driver->pm->complete(dev);
}

/**
 * iucv_path_table_empty() - determine if iucv path table is empty
 *
 * Returns 0 if there are still iucv pathes defined
 *	   1 if there are no iucv pathes defined
 */
static int iucv_path_table_empty(void)
{
	int i;

	for (i = 0; i < iucv_max_pathid; i++) {
		if (iucv_path_table[i])
			return 0;
	}
	return 1;
}

/**
 * iucv_pm_freeze() - Freeze PM callback
 * @dev:	iucv-based device
 *
 * disable iucv interrupts
 * invoke callback function of the iucv-based driver
 * shut down iucv, if no iucv-pathes are established anymore
 */
static int iucv_pm_freeze(struct device *dev)
{
	int cpu;
	struct iucv_irq_list *p, *n;
	int rc = 0;

#ifdef CONFIG_PM_DEBUG
	printk(KERN_WARNING "iucv_pm_freeze\n");
#endif
	if (iucv_pm_state != IUCV_PM_FREEZING) {
		for_each_cpu(cpu, &iucv_irq_cpumask)
			smp_call_function_single(cpu, iucv_block_cpu_almost,
						 NULL, 1);
		cancel_work_sync(&iucv_work);
		list_for_each_entry_safe(p, n, &iucv_work_queue, list) {
			list_del_init(&p->list);
			iucv_sever_pathid(p->data.ippathid,
					  iucv_error_no_listener);
			kfree(p);
		}
	}
	iucv_pm_state = IUCV_PM_FREEZING;
	if (dev->driver && dev->driver->pm && dev->driver->pm->freeze)
		rc = dev->driver->pm->freeze(dev);
	if (iucv_path_table_empty())
		iucv_disable();
	return rc;
}

/**
 * iucv_pm_thaw() - Thaw PM callback
 * @dev:	iucv-based device
 *
 * make iucv ready for use again: allocate path table, declare interrupt buffers
 *				  and enable iucv interrupts
 * invoke callback function of the iucv-based driver
 */
static int iucv_pm_thaw(struct device *dev)
{
	int rc = 0;

#ifdef CONFIG_PM_DEBUG
	printk(KERN_WARNING "iucv_pm_thaw\n");
#endif
	iucv_pm_state = IUCV_PM_THAWING;
	if (!iucv_path_table) {
		rc = iucv_enable();
		if (rc)
			goto out;
	}
	if (cpumask_empty(&iucv_irq_cpumask)) {
		if (iucv_nonsmp_handler)
			/* enable interrupts on one cpu */
			iucv_allow_cpu(NULL);
		else
			/* enable interrupts on all cpus */
			iucv_setmask_mp();
	}
	if (dev->driver && dev->driver->pm && dev->driver->pm->thaw)
		rc = dev->driver->pm->thaw(dev);
out:
	return rc;
}

/**
 * iucv_pm_restore() - Restore PM callback
 * @dev:	iucv-based device
 *
 * make iucv ready for use again: allocate path table, declare interrupt buffers
 *				  and enable iucv interrupts
 * invoke callback function of the iucv-based driver
 */
static int iucv_pm_restore(struct device *dev)
{
	int rc = 0;

#ifdef CONFIG_PM_DEBUG
	printk(KERN_WARNING "iucv_pm_restore %p\n", iucv_path_table);
#endif
	if ((iucv_pm_state != IUCV_PM_RESTORING) && iucv_path_table)
		pr_warn("Suspending Linux did not completely close all IUCV connections\n");
	iucv_pm_state = IUCV_PM_RESTORING;
	if (cpumask_empty(&iucv_irq_cpumask)) {
		rc = iucv_query_maxconn();
		rc = iucv_enable();
		if (rc)
			goto out;
	}
	if (dev->driver && dev->driver->pm && dev->driver->pm->restore)
		rc = dev->driver->pm->restore(dev);
out:
	return rc;
}

struct iucv_interface iucv_if = {
	.message_receive = iucv_message_receive,
	.__message_receive = __iucv_message_receive,
	.message_reply = iucv_message_reply,
	.message_reject = iucv_message_reject,
	.message_send = iucv_message_send,
	.__message_send = __iucv_message_send,
	.message_send2way = iucv_message_send2way,
	.message_purge = iucv_message_purge,
	.path_accept = iucv_path_accept,
	.path_connect = iucv_path_connect,
	.path_quiesce = iucv_path_quiesce,
	.path_resume = iucv_path_resume,
	.path_sever = iucv_path_sever,
	.iucv_register = iucv_register,
	.iucv_unregister = iucv_unregister,
	.bus = NULL,
	.root = NULL,
};
EXPORT_SYMBOL(iucv_if);

static enum cpuhp_state iucv_online;
/**
 * iucv_init
 *
 * Allocates and initializes various data structures.
 */
static int __init iucv_init(void)
{
	int rc;

	if (!MACHINE_IS_VM) {
		rc = -EPROTONOSUPPORT;
		goto out;
	}
	ctl_set_bit(0, 1);
	rc = iucv_query_maxconn();
	if (rc)
		goto out_ctl;
	rc = register_external_irq(EXT_IRQ_IUCV, iucv_external_interrupt);
	if (rc)
		goto out_ctl;
	iucv_root = root_device_register("iucv");
	if (IS_ERR(iucv_root)) {
		rc = PTR_ERR(iucv_root);
		goto out_int;
	}

	rc = cpuhp_setup_state(CPUHP_NET_IUCV_PREPARE, "net/iucv:prepare",
			       iucv_cpu_prepare, iucv_cpu_dead);
	if (rc)
		goto out_dev;
	rc = cpuhp_setup_state(CPUHP_AP_ONLINE_DYN, "net/iucv:online",
			       iucv_cpu_online, iucv_cpu_down_prep);
	if (rc < 0)
		goto out_prep;
	iucv_online = rc;

	rc = register_reboot_notifier(&iucv_reboot_notifier);
	if (rc)
		goto out_remove_hp;
	ASCEBC(iucv_error_no_listener, 16);
	ASCEBC(iucv_error_no_memory, 16);
	ASCEBC(iucv_error_pathid, 16);
	iucv_available = 1;
	rc = bus_register(&iucv_bus);
	if (rc)
		goto out_reboot;
	iucv_if.root = iucv_root;
	iucv_if.bus = &iucv_bus;
	return 0;

out_reboot:
	unregister_reboot_notifier(&iucv_reboot_notifier);
out_remove_hp:
	cpuhp_remove_state(iucv_online);
out_prep:
	cpuhp_remove_state(CPUHP_NET_IUCV_PREPARE);
out_dev:
	root_device_unregister(iucv_root);
out_int:
	unregister_external_irq(EXT_IRQ_IUCV, iucv_external_interrupt);
out_ctl:
	ctl_clear_bit(0, 1);
out:
	return rc;
}

/**
 * iucv_exit
 *
 * Frees everything allocated from iucv_init.
 */
static void __exit iucv_exit(void)
{
	struct iucv_irq_list *p, *n;

	spin_lock_irq(&iucv_queue_lock);
	list_for_each_entry_safe(p, n, &iucv_task_queue, list)
		kfree(p);
	list_for_each_entry_safe(p, n, &iucv_work_queue, list)
		kfree(p);
	spin_unlock_irq(&iucv_queue_lock);
	unregister_reboot_notifier(&iucv_reboot_notifier);

	cpuhp_remove_state_nocalls(iucv_online);
	cpuhp_remove_state(CPUHP_NET_IUCV_PREPARE);
	root_device_unregister(iucv_root);
	bus_unregister(&iucv_bus);
	unregister_external_irq(EXT_IRQ_IUCV, iucv_external_interrupt);
}

subsys_initcall(iucv_init);
module_exit(iucv_exit);

MODULE_AUTHOR("(C) 2001 IBM Corp. by Fritz Elfert (felfert@millenux.com)");
MODULE_DESCRIPTION("Linux for S/390 IUCV lowlevel driver");
MODULE_LICENSE("GPL");
