#ifndef _ASM_GENAPIC_H
#define _ASM_GENAPIC_H 1

#include <asm/mpspec.h>

/*
 * Generic APIC driver interface.
 *
 * An straight forward mapping of the APIC related parts of the
 * x86 subarchitecture interface to a dynamic object.
 *
 * This is used by the "generic" x86 subarchitecture.
 *
 * Copyright 2003 Andi Kleen, SuSE Labs.
 */

struct mpc_config_bus;
struct mp_config_table;
struct mpc_config_processor;

struct genapic {
	char *name;
	int (*probe)(void);

	int (*apic_id_registered)(void);
	cpumask_t (*target_cpus)(void);
	int int_delivery_mode;
	int int_dest_mode;
	int ESR_DISABLE;
	int apic_destination_logical;
	unsigned long (*check_apicid_used)(physid_mask_t bitmap, int apicid);
	unsigned long (*check_apicid_present)(int apicid);
	int no_balance_irq;
	int no_ioapic_check;
	void (*init_apic_ldr)(void);
	physid_mask_t (*ioapic_phys_id_map)(physid_mask_t map);

	void (*setup_apic_routing)(void);
	int (*multi_timer_check)(int apic, int irq);
	int (*apicid_to_node)(int logical_apicid);
	int (*cpu_to_logical_apicid)(int cpu);
	int (*cpu_present_to_apicid)(int mps_cpu);
	physid_mask_t (*apicid_to_cpu_present)(int phys_apicid);
	void (*setup_portio_remap)(void);
	int (*check_phys_apicid_present)(int boot_cpu_physical_apicid);
	void (*enable_apic_mode)(void);
	u32 (*phys_pkg_id)(u32 cpuid_apic, int index_msb);

	/* mpparse */
	/* When one of the next two hooks returns 1 the genapic
	   is switched to this. Essentially they are additional probe
	   functions. */
	int (*mps_oem_check)(struct mp_config_table *mpc, char *oem,
			     char *productid);
	int (*acpi_madt_oem_check)(char *oem_id, char *oem_table_id);

	unsigned (*get_apic_id)(unsigned long x);
	unsigned long apic_id_mask;
	unsigned int (*cpu_mask_to_apicid)(cpumask_t cpumask);

#ifdef CONFIG_SMP
	/* ipi */
	void (*send_IPI_mask)(cpumask_t mask, int vector);
	void (*send_IPI_allbutself)(int vector);
	void (*send_IPI_all)(int vector);
#endif
};

#define APICFUNC(x) .x = x,

/* More functions could be probably marked IPIFUNC and save some space
   in UP GENERICARCH kernels, but I don't have the nerve right now
   to untangle this mess. -AK  */
#ifdef CONFIG_SMP
#define IPIFUNC(x) APICFUNC(x)
#else
#define IPIFUNC(x)
#endif

#define APIC_INIT(aname, aprobe)			\
{							\
	.name = aname,					\
	.probe = aprobe,				\
	.int_delivery_mode = INT_DELIVERY_MODE,		\
	.int_dest_mode = INT_DEST_MODE,			\
	.no_balance_irq = NO_BALANCE_IRQ,		\
	.ESR_DISABLE = esr_disable,			\
	.apic_destination_logical = APIC_DEST_LOGICAL,	\
	APICFUNC(apic_id_registered)			\
	APICFUNC(target_cpus)				\
	APICFUNC(check_apicid_used)			\
	APICFUNC(check_apicid_present)			\
	APICFUNC(init_apic_ldr)				\
	APICFUNC(ioapic_phys_id_map)			\
	APICFUNC(setup_apic_routing)			\
	APICFUNC(multi_timer_check)			\
	APICFUNC(apicid_to_node)			\
	APICFUNC(cpu_to_logical_apicid)			\
	APICFUNC(cpu_present_to_apicid)			\
	APICFUNC(apicid_to_cpu_present)			\
	APICFUNC(setup_portio_remap)			\
	APICFUNC(check_phys_apicid_present)		\
	APICFUNC(mps_oem_check)				\
	APICFUNC(get_apic_id)				\
	.apic_id_mask = APIC_ID_MASK,			\
	APICFUNC(cpu_mask_to_apicid)			\
	APICFUNC(acpi_madt_oem_check)			\
	IPIFUNC(send_IPI_mask)				\
	IPIFUNC(send_IPI_allbutself)			\
	IPIFUNC(send_IPI_all)				\
	APICFUNC(enable_apic_mode)			\
	APICFUNC(phys_pkg_id)				\
}

extern struct genapic *genapic;

enum uv_system_type {UV_NONE, UV_LEGACY_APIC, UV_X2APIC, UV_NON_UNIQUE_APIC};
#define get_uv_system_type()		UV_NONE
#define is_uv_system()			0
#define uv_wakeup_secondary(a, b)	1


#endif
