#ifndef _GENAPIC_MACH_APICDEF_H
#define _GENAPIC_MACH_APICDEF_H 1

#ifndef APIC_DEFINITION
#include <asm/genapic.h>

#define GET_APIC_ID (genapic->get_apic_id)
#define APIC_ID_MASK (genapic->apic_id_mask)
#endif

#endif
