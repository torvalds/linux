#ifndef _ASM_I386_CPU_H_
#define _ASM_I386_CPU_H_

#include <linux/device.h>
#include <linux/cpu.h>
#include <linux/topology.h>
#include <linux/nodemask.h>

#include <asm/node.h>

struct i386_cpu {
	struct cpu cpu;
};
extern int arch_register_cpu(int num);
#ifdef CONFIG_HOTPLUG_CPU
extern void arch_unregister_cpu(int);
#endif

#endif /* _ASM_I386_CPU_H_ */
