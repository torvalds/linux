#ifndef _ASM_X86_ACPI_H
#define _ASM_X86_ACPI_H

#ifdef CONFIG_X86_32
# include "acpi_32.h"
#else
# include "acpi_64.h"
#endif

#include <asm/processor.h>

/*
 * Check if the CPU can handle C2 and deeper
 */
static inline unsigned int acpi_processor_cstate_check(unsigned int max_cstate)
{
	/*
	 * Early models (<=5) of AMD Opterons are not supposed to go into
	 * C2 state.
	 *
	 * Steppings 0x0A and later are good
	 */
	if (boot_cpu_data.x86 == 0x0F &&
	    boot_cpu_data.x86_vendor == X86_VENDOR_AMD &&
	    boot_cpu_data.x86_model <= 0x05 &&
	    boot_cpu_data.x86_mask < 0x0A)
		return 1;
	else
		return max_cstate;
}

#endif
