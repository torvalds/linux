#ifndef _SPARC64_HVTRAP_H
#define _SPARC64_HVTRAP_H

#ifndef __ASSEMBLY__

#include <linux/types.h>

struct hvtramp_mapping {
	__u64		vaddr;
	__u64		tte;
};

struct hvtramp_descr {
	__u32			cpu;
	__u32			num_mappings;
	__u64			fault_info_va;
	__u64			fault_info_pa;
	__u64			thread_reg;
	struct hvtramp_mapping	maps[1];
};

extern void hv_cpu_startup(unsigned long hvdescr_pa);

#endif

#define HVTRAMP_DESCR_CPU		0x00
#define HVTRAMP_DESCR_NUM_MAPPINGS	0x04
#define HVTRAMP_DESCR_FAULT_INFO_VA	0x08
#define HVTRAMP_DESCR_FAULT_INFO_PA	0x10
#define HVTRAMP_DESCR_THREAD_REG	0x18
#define HVTRAMP_DESCR_MAPS		0x20

#define HVTRAMP_MAPPING_VADDR		0x00
#define HVTRAMP_MAPPING_TTE		0x08
#define HVTRAMP_MAPPING_SIZE		0x10

#endif /* _SPARC64_HVTRAP_H */
