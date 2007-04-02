/*
 * include/linux/cpu.h - generic cpu definition
 *
 * This is mainly for topological representation. We define the 
 * basic 'struct cpu' here, which can be embedded in per-arch 
 * definitions of processors.
 *
 * Basic handling of the devices is done in drivers/base/cpu.c
 * and system devices are handled in drivers/base/sys.c. 
 *
 * CPUs are exported via sysfs in the class/cpu/devices/
 * directory. 
 *
 * Per-cpu interfaces can be implemented using a struct device_interface. 
 * See the following for how to do this: 
 * - drivers/base/intf.c 
 * - Documentation/driver-model/interface.txt
 */
#ifndef _LINUX_CPU_H_
#define _LINUX_CPU_H_

#include <linux/sysdev.h>
#include <linux/node.h>
#include <linux/compiler.h>
#include <linux/cpumask.h>
#include <asm/semaphore.h>
#include <linux/mutex.h>

struct cpu {
	int node_id;		/* The node which contains the CPU */
	int hotpluggable;	/* creates sysfs control file if hotpluggable */
	struct sys_device sysdev;
};

extern int register_cpu(struct cpu *cpu, int num);
extern struct sys_device *get_cpu_sysdev(unsigned cpu);

extern int cpu_add_sysdev_attr(struct sysdev_attribute *attr);
extern void cpu_remove_sysdev_attr(struct sysdev_attribute *attr);

extern int cpu_add_sysdev_attr_group(struct attribute_group *attrs);
extern void cpu_remove_sysdev_attr_group(struct attribute_group *attrs);


#ifdef CONFIG_HOTPLUG_CPU
extern void unregister_cpu(struct cpu *cpu);
#endif
struct notifier_block;

#ifdef CONFIG_SMP
/* Need to know about CPUs going up/down? */
#ifdef CONFIG_HOTPLUG_CPU
extern int register_cpu_notifier(struct notifier_block *nb);
extern void unregister_cpu_notifier(struct notifier_block *nb);
#else

#ifndef MODULE
extern int register_cpu_notifier(struct notifier_block *nb);
#else
static inline int register_cpu_notifier(struct notifier_block *nb)
{
	return 0;
}
#endif

static inline void unregister_cpu_notifier(struct notifier_block *nb)
{
}
#endif

int cpu_up(unsigned int cpu);

#else

static inline int register_cpu_notifier(struct notifier_block *nb)
{
	return 0;
}
static inline void unregister_cpu_notifier(struct notifier_block *nb)
{
}

#endif /* CONFIG_SMP */
extern struct sysdev_class cpu_sysdev_class;

#ifdef CONFIG_HOTPLUG_CPU
/* Stop CPUs going up and down. */

static inline void cpuhotplug_mutex_lock(struct mutex *cpu_hp_mutex)
{
	mutex_lock(cpu_hp_mutex);
}

static inline void cpuhotplug_mutex_unlock(struct mutex *cpu_hp_mutex)
{
	mutex_unlock(cpu_hp_mutex);
}

extern void lock_cpu_hotplug(void);
extern void unlock_cpu_hotplug(void);
#define hotcpu_notifier(fn, pri) {				\
	static struct notifier_block fn##_nb =			\
		{ .notifier_call = fn, .priority = pri };	\
	register_cpu_notifier(&fn##_nb);			\
}
#define register_hotcpu_notifier(nb)	register_cpu_notifier(nb)
#define unregister_hotcpu_notifier(nb)	unregister_cpu_notifier(nb)
int cpu_down(unsigned int cpu);
#define cpu_is_offline(cpu) unlikely(!cpu_online(cpu))

#else		/* CONFIG_HOTPLUG_CPU */

static inline void cpuhotplug_mutex_lock(struct mutex *cpu_hp_mutex)
{ }
static inline void cpuhotplug_mutex_unlock(struct mutex *cpu_hp_mutex)
{ }

#define lock_cpu_hotplug()	do { } while (0)
#define unlock_cpu_hotplug()	do { } while (0)
#define lock_cpu_hotplug_interruptible() 0
#define hotcpu_notifier(fn, pri)	do { (void)(fn); } while (0)
#define register_hotcpu_notifier(nb)	do { (void)(nb); } while (0)
#define unregister_hotcpu_notifier(nb)	do { (void)(nb); } while (0)

/* CPUs don't go offline once they're online w/o CONFIG_HOTPLUG_CPU */
static inline int cpu_is_offline(int cpu) { return 0; }
#endif		/* CONFIG_HOTPLUG_CPU */

#ifdef CONFIG_SUSPEND_SMP
extern int suspend_cpu_hotplug;

extern int disable_nonboot_cpus(void);
extern void enable_nonboot_cpus(void);
#else
#define suspend_cpu_hotplug	0

static inline int disable_nonboot_cpus(void) { return 0; }
static inline void enable_nonboot_cpus(void) {}
#endif

#endif /* _LINUX_CPU_H_ */
