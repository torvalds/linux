// SPDX-License-Identifier: GPL-2.0
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "event-parse.h"

#define __HYPERVISOR_set_trap_table			0
#define __HYPERVISOR_mmu_update				1
#define __HYPERVISOR_set_gdt				2
#define __HYPERVISOR_stack_switch			3
#define __HYPERVISOR_set_callbacks			4
#define __HYPERVISOR_fpu_taskswitch			5
#define __HYPERVISOR_sched_op_compat			6
#define __HYPERVISOR_dom0_op				7
#define __HYPERVISOR_set_debugreg			8
#define __HYPERVISOR_get_debugreg			9
#define __HYPERVISOR_update_descriptor			10
#define __HYPERVISOR_memory_op				12
#define __HYPERVISOR_multicall				13
#define __HYPERVISOR_update_va_mapping			14
#define __HYPERVISOR_set_timer_op			15
#define __HYPERVISOR_event_channel_op_compat		16
#define __HYPERVISOR_xen_version			17
#define __HYPERVISOR_console_io				18
#define __HYPERVISOR_physdev_op_compat			19
#define __HYPERVISOR_grant_table_op			20
#define __HYPERVISOR_vm_assist				21
#define __HYPERVISOR_update_va_mapping_otherdomain	22
#define __HYPERVISOR_iret				23 /* x86 only */
#define __HYPERVISOR_vcpu_op				24
#define __HYPERVISOR_set_segment_base			25 /* x86/64 only */
#define __HYPERVISOR_mmuext_op				26
#define __HYPERVISOR_acm_op				27
#define __HYPERVISOR_nmi_op				28
#define __HYPERVISOR_sched_op				29
#define __HYPERVISOR_callback_op			30
#define __HYPERVISOR_xenoprof_op			31
#define __HYPERVISOR_event_channel_op			32
#define __HYPERVISOR_physdev_op				33
#define __HYPERVISOR_hvm_op				34
#define __HYPERVISOR_tmem_op				38

/* Architecture-specific hypercall definitions. */
#define __HYPERVISOR_arch_0				48
#define __HYPERVISOR_arch_1				49
#define __HYPERVISOR_arch_2				50
#define __HYPERVISOR_arch_3				51
#define __HYPERVISOR_arch_4				52
#define __HYPERVISOR_arch_5				53
#define __HYPERVISOR_arch_6				54
#define __HYPERVISOR_arch_7				55

#define N(x)	[__HYPERVISOR_##x] = "("#x")"
static const char *xen_hypercall_names[] = {
	N(set_trap_table),
	N(mmu_update),
	N(set_gdt),
	N(stack_switch),
	N(set_callbacks),
	N(fpu_taskswitch),
	N(sched_op_compat),
	N(dom0_op),
	N(set_debugreg),
	N(get_debugreg),
	N(update_descriptor),
	N(memory_op),
	N(multicall),
	N(update_va_mapping),
	N(set_timer_op),
	N(event_channel_op_compat),
	N(xen_version),
	N(console_io),
	N(physdev_op_compat),
	N(grant_table_op),
	N(vm_assist),
	N(update_va_mapping_otherdomain),
	N(iret),
	N(vcpu_op),
	N(set_segment_base),
	N(mmuext_op),
	N(acm_op),
	N(nmi_op),
	N(sched_op),
	N(callback_op),
	N(xenoprof_op),
	N(event_channel_op),
	N(physdev_op),
	N(hvm_op),

/* Architecture-specific hypercall definitions. */
	N(arch_0),
	N(arch_1),
	N(arch_2),
	N(arch_3),
	N(arch_4),
	N(arch_5),
	N(arch_6),
	N(arch_7),
};
#undef N

#define ARRAY_SIZE(arr) (sizeof(arr) / sizeof((arr)[0]))

static const char *xen_hypercall_name(unsigned op)
{
	if (op < ARRAY_SIZE(xen_hypercall_names) &&
	    xen_hypercall_names[op] != NULL)
		return xen_hypercall_names[op];

	return "";
}

unsigned long long process_xen_hypercall_name(struct trace_seq *s,
					      unsigned long long *args)
{
	unsigned int op = args[0];

	trace_seq_printf(s, "%s", xen_hypercall_name(op));
	return 0;
}

int PEVENT_PLUGIN_LOADER(struct pevent *pevent)
{
	pevent_register_print_function(pevent,
				       process_xen_hypercall_name,
				       PEVENT_FUNC_ARG_STRING,
				       "xen_hypercall_name",
				       PEVENT_FUNC_ARG_INT,
				       PEVENT_FUNC_ARG_VOID);
	return 0;
}

void PEVENT_PLUGIN_UNLOADER(struct pevent *pevent)
{
	pevent_unregister_print_function(pevent, process_xen_hypercall_name,
					 "xen_hypercall_name");
}
