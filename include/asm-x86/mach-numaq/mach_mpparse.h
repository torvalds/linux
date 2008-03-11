#ifndef __ASM_MACH_MPPARSE_H
#define __ASM_MACH_MPPARSE_H

extern int mp_bus_id_to_local[MAX_MP_BUSSES];
extern int mp_bus_id_to_node[MAX_MP_BUSSES];

static inline void mpc_oem_bus_info(struct mpc_config_bus *m, char *name, 
				struct mpc_config_translation *translation)
{
	int quad = translation->trans_quad;
	int local = translation->trans_local;

	mp_bus_id_to_node[m->mpc_busid] = quad;
	mp_bus_id_to_local[m->mpc_busid] = local;
	printk("Bus #%d is %s (node %d)\n", m->mpc_busid, name, quad);
}

extern int quad_local_to_mp_bus_id[NR_CPUS/4][4];

static inline void mpc_oem_pci_bus(struct mpc_config_bus *m, 
				struct mpc_config_translation *translation)
{
	int quad = translation->trans_quad;
	int local = translation->trans_local;

	quad_local_to_mp_bus_id[quad][local] = m->mpc_busid;
}

/* Hook from generic ACPI tables.c */
static inline void acpi_madt_oem_check(char *oem_id, char *oem_table_id)
{
}

#endif /* __ASM_MACH_MPPARSE_H */
