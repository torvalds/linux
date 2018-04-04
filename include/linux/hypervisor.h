/* SPDX-License-Identifier: GPL-2.0 */
#ifndef __LINUX_HYPEVISOR_H
#define __LINUX_HYPEVISOR_H

/*
 *	Generic Hypervisor support
 *		Juergen Gross <jgross@suse.com>
 */

#ifdef CONFIG_X86
#include <asm/x86_init.h>
static inline void hypervisor_pin_vcpu(int cpu)
{
	x86_platform.hyper.pin_vcpu(cpu);
}
#else
static inline void hypervisor_pin_vcpu(int cpu)
{
}
#endif

#endif /* __LINUX_HYPEVISOR_H */
