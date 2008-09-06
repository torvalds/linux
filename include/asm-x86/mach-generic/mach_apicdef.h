#ifndef ASM_X86__MACH_GENERIC__MACH_APICDEF_H
#define ASM_X86__MACH_GENERIC__MACH_APICDEF_H

#ifndef APIC_DEFINITION
#include <asm/genapic.h>

#define GET_APIC_ID (genapic->get_apic_id)
#define APIC_ID_MASK (genapic->apic_id_mask)
#endif

#endif /* ASM_X86__MACH_GENERIC__MACH_APICDEF_H */
