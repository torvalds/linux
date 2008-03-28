#ifndef _ASM_GENAPIC_H
#define _ASM_GENAPIC_H 1

/*
 * Copyright 2004 James Cleverdon, IBM.
 * Subject to the GNU Public License, v.2
 *
 * Generic APIC sub-arch data struct.
 *
 * Hacked for x86-64 by James Cleverdon from i386 architecture code by
 * Martin Bligh, Andi Kleen, James Bottomley, John Stultz, and
 * James Cleverdon.
 */

struct genapic {
	char *name;
	u32 int_delivery_mode;
	u32 int_dest_mode;
	int (*apic_id_registered)(void);
	cpumask_t (*target_cpus)(void);
	cpumask_t (*vector_allocation_domain)(int cpu);
	void (*init_apic_ldr)(void);
	/* ipi */
	void (*send_IPI_mask)(cpumask_t mask, int vector);
	void (*send_IPI_allbutself)(int vector);
	void (*send_IPI_all)(int vector);
	/* */
	unsigned int (*cpu_mask_to_apicid)(cpumask_t cpumask);
	unsigned int (*phys_pkg_id)(int index_msb);
};

extern struct genapic *genapic;

extern struct genapic apic_flat;
extern struct genapic apic_physflat;
extern int acpi_madt_oem_check(char *, char *);

enum uv_system_type {UV_NONE, UV_LEGACY_APIC, UV_X2APIC, UV_NON_UNIQUE_APIC};
extern enum uv_system_type get_uv_system_type(void);
extern int is_uv_system(void);

extern struct genapic apic_x2apic_uv_x;
DECLARE_PER_CPU(int, x2apic_extra_bits);
extern void uv_cpu_init(void);
extern int uv_wakeup_secondary(int phys_apicid, unsigned int start_rip);

#endif
